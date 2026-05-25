/*****************************************************************************
**
** MP4 (H.264 / AAC) video + audio recorder.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************
*/
// Media Foundation MP4 (H.264 + AAC) recorder. All IMF* objects live on
// a dedicated MTA worker thread; the STA UI / emu threads only push
// frames + audio through lock-free queues. MFStartup on STA poisons
// IFileDialog, so the worker owns the entire MF pipeline.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "Win32Recorder.h"
#include "Win32D3D12.h"
#include "Win32MediaFoundation.h"

extern "C" {
#include "Actions.h"
#include "Board.h"
#include "Win32Sound.h"
#include "Win32TextUtf8.h"
#include "Win32Common.h"
#include "ArchFile.h"
#include "ArchNotifications.h"
#include "Emulator.h"
#include "Resource.h"
#include "Language.h"
#include "PacketFileSystem.h"

// archFileExists is defined in Win32.c but not declared in any public header.
int archFileExists(const char* fileName);
}

using Microsoft::WRL::ComPtr;

// --- Encoder constants -------------------------------------------------------
// Video bitrate is computed per session in mfWorkerThreadEntry()
// (~0.10 bpp, clamped to [VIDEO_BITRATE_MIN, VIDEO_BITRATE_MAX]).
#define VIDEO_BITRATE_MIN  4000000U   /*  4 Mbps */
#define VIDEO_BITRATE_MAX 25000000U   /* 25 Mbps */
static const UINT32 AUDIO_BITRATE_BPS = 128000;
static const UINT32 AUDIO_SAMPLE_RATE = 44100;
static const UINT32 AUDIO_CHANNELS    = 2;
static const UINT32 AUDIO_BPS         = 16;
static const UINT32 AUDIO_BLOCK_ALIGN = AUDIO_CHANNELS * AUDIO_BPS / 8;  // 4

// --- Module state (touched from main / emu / worker, see comments) -----------

// Controls passed from main thread to worker on start.
struct WorkerInit {
    std::wstring filename;
    int          fps;
    int          width;
    int          height;
};

// Queue payloads. The worker takes ownership of the bytes.
enum class FrameKind { Video, Audio };
struct QueuedFrame {
    FrameKind            kind;
    std::vector<uint8_t> data;        // video: BGRA32 bottom-up; audio: PCM16 stereo
    int                  audioFrames; // for audio: # of stereo frames
};

static std::mutex                 g_mtx;
static std::condition_variable    g_cv;
static std::queue<QueuedFrame>    g_q;
static std::atomic<bool>          g_recActive{false};   // producer pushes only while true
static std::atomic<bool>          g_stopRequested{false};
static std::thread                g_worker;

// Worker-init handshake: main thread blocks until worker reports init success
// or failure (so a MFCreateSinkWriterFromURL error can show a MessageBox on
// the main UI thread before recorderStartRender returns).
static HANDLE                     g_initDoneEvent = NULL;
static std::atomic<bool>          g_initOk{false};
static std::wstring               g_initErrTag;
static HRESULT                    g_initErrHr     = S_OK;

// Producer-side state used by the recorderVideoCallback (emu thread) only.
static HWND        g_hwnd       = NULL;
static Properties* g_properties = NULL;
static Video*      g_video      = NULL;
static int         g_syncMethod = 0;
static int         g_emuSpeed   = 0;
static int         g_rendering  = 0;
static int         g_zoom       = 2;
static int         g_fps        = 60;

// 100-ns ticks per frame / per audio sample frame.
static LONGLONG frameDur100ns(int fps)         { return 10000000LL / (LONGLONG)fps; }
static LONGLONG audioSampleDur100ns(UINT32 hz) { return 10000000LL / (LONGLONG)hz; }

// --- Worker: build IMFSample from raw bytes and call WriteSample -------------
static HRESULT writeVideoSampleW(IMFSinkWriter* writer, DWORD streamIdx,
                                 const std::vector<uint8_t>& data,
                                 LONGLONG time100ns, LONGLONG dur100ns)
{
    HRESULT hr;
    ComPtr<IMFMediaBuffer> mfBuf;
    hr = MFCreateMemoryBuffer((DWORD)data.size(), &mfBuf);
    if (FAILED(hr)) return hr;

    BYTE* dst = NULL;
    hr = mfBuf->Lock(&dst, NULL, NULL);
    if (FAILED(hr)) return hr;
    memcpy(dst, data.data(), data.size());
    mfBuf->Unlock();
    mfBuf->SetCurrentLength((DWORD)data.size());

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return hr;
    sample->AddBuffer(mfBuf.Get());
    sample->SetSampleTime(time100ns);
    sample->SetSampleDuration(dur100ns);

    return writer->WriteSample(streamIdx, sample.Get());
}

// --- Worker thread entry: all IMF* activity is contained here. -------------
static void mfWorkerThreadEntry(WorkerInit init)
{
    // 1) MTA init for this thread. MF runtime itself was started once at
    //    process load (ensureMFStartupOnce, called from startWorker).
    HRESULT coHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool coOwned = SUCCEEDED(coHr) && coHr != RPC_E_CHANGED_MODE;

    if (!isMFStartupOk()) {
        g_initErrTag = L"MFStartup failed at process init";
        g_initErrHr  = E_FAIL;
        if (coOwned) CoUninitialize();
        SetEvent(g_initDoneEvent);
        return;
    }

    // 2) Build the sink writer + media types.
    DWORD videoStreamIdx = 0;
    DWORD audioStreamIdx = 0;
    ComPtr<IMFSinkWriter> writer;

    auto fail = [&](const wchar_t* tag, HRESULT errHr) {
        g_initErrTag = tag;
        g_initErrHr  = errHr;
        writer.Reset();
        if (coOwned) CoUninitialize();
        SetEvent(g_initDoneEvent);
    };

    // Prefer HW encoders (SW H.264 can't keep up at 1280x960x60) and ask
    // for low pipeline depth so Finalize() drains quickly at stop.
    ComPtr<IMFAttributes> writerAttrs;
    HRESULT hr = MFCreateAttributes(&writerAttrs, 2);
    if (SUCCEEDED(hr)) {
        writerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        writerAttrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    }
    hr = MFCreateSinkWriterFromURL(init.filename.c_str(), NULL, writerAttrs.Get(), &writer);
    if (FAILED(hr)) { fail(L"MFCreateSinkWriterFromURL failed", hr); return; }

    // ----- Video output: H.264 (bitrate ~0.10 bpp, clamped) -----
    {
        const UINT64 bppfTotal = (UINT64)init.width * (UINT64)init.height * (UINT64)init.fps;
        UINT32 videoBitrate    = (UINT32)((bppfTotal * 10) / 100);  // ~0.10 bpp
        if (videoBitrate < VIDEO_BITRATE_MIN) videoBitrate = VIDEO_BITRATE_MIN;
        if (videoBitrate > VIDEO_BITRATE_MAX) videoBitrate = VIDEO_BITRATE_MAX;

        ComPtr<IMFMediaType> outVid;
        hr = MFCreateMediaType(&outVid);
        if (FAILED(hr)) { fail(L"MFCreateMediaType (out video) failed", hr); return; }
        outVid->SetGUID  (MF_MT_MAJOR_TYPE,     MFMediaType_Video);
        outVid->SetGUID  (MF_MT_SUBTYPE,        MFVideoFormat_H264);
        outVid->SetUINT32(MF_MT_AVG_BITRATE,    videoBitrate);
        outVid->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize (outVid.Get(), MF_MT_FRAME_SIZE, (UINT32)init.width, (UINT32)init.height);
        MFSetAttributeRatio(outVid.Get(), MF_MT_FRAME_RATE, (UINT32)init.fps, 1);
        MFSetAttributeRatio(outVid.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->AddStream(outVid.Get(), &videoStreamIdx);
        if (FAILED(hr)) { fail(L"AddStream (video H.264) failed", hr); return; }
    }

    // ----- Video input: BGRA32 bottom-up (negative MF_MT_DEFAULT_STRIDE) -----
    {
        ComPtr<IMFMediaType> inVid;
        hr = MFCreateMediaType(&inVid);
        if (FAILED(hr)) { fail(L"MFCreateMediaType (in video) failed", hr); return; }
        inVid->SetGUID  (MF_MT_MAJOR_TYPE,     MFMediaType_Video);
        inVid->SetGUID  (MF_MT_SUBTYPE,        MFVideoFormat_RGB32);
        inVid->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        inVid->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(-init.width * 4));
        MFSetAttributeSize (inVid.Get(), MF_MT_FRAME_SIZE, (UINT32)init.width, (UINT32)init.height);
        MFSetAttributeRatio(inVid.Get(), MF_MT_FRAME_RATE, (UINT32)init.fps, 1);
        MFSetAttributeRatio(inVid.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->SetInputMediaType(videoStreamIdx, inVid.Get(), NULL);
        if (FAILED(hr)) { fail(L"SetInputMediaType (video BGRA32) failed", hr); return; }
    }

    // ----- Audio output: AAC -----
    {
        ComPtr<IMFMediaType> outAud;
        hr = MFCreateMediaType(&outAud);
        if (FAILED(hr)) { fail(L"MFCreateMediaType (out audio) failed", hr); return; }
        outAud->SetGUID  (MF_MT_MAJOR_TYPE,                 MFMediaType_Audio);
        outAud->SetGUID  (MF_MT_SUBTYPE,                    MFAudioFormat_AAC);
        outAud->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      AUDIO_BPS);
        outAud->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   AUDIO_SAMPLE_RATE);
        outAud->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         AUDIO_CHANNELS);
        outAud->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AUDIO_BITRATE_BPS / 8);
        hr = writer->AddStream(outAud.Get(), &audioStreamIdx);
        if (FAILED(hr)) { fail(L"AddStream (audio AAC) failed", hr); return; }
    }

    // ----- Audio input: PCM 16-bit stereo 44.1kHz -----
    {
        ComPtr<IMFMediaType> inAud;
        hr = MFCreateMediaType(&inAud);
        if (FAILED(hr)) { fail(L"MFCreateMediaType (in audio) failed", hr); return; }
        inAud->SetGUID  (MF_MT_MAJOR_TYPE,                 MFMediaType_Audio);
        inAud->SetGUID  (MF_MT_SUBTYPE,                    MFAudioFormat_PCM);
        inAud->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      AUDIO_BPS);
        inAud->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   AUDIO_SAMPLE_RATE);
        inAud->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         AUDIO_CHANNELS);
        inAud->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,      AUDIO_BLOCK_ALIGN);
        inAud->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AUDIO_SAMPLE_RATE * AUDIO_BLOCK_ALIGN);
        hr = writer->SetInputMediaType(audioStreamIdx, inAud.Get(), NULL);
        if (FAILED(hr)) { fail(L"SetInputMediaType (audio PCM) failed", hr); return; }
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) { fail(L"BeginWriting failed", hr); return; }

    // 3) Init OK -- release main thread.
    g_initOk = true;
    SetEvent(g_initDoneEvent);

    // 4) Drain queue until stop is requested AND queue is empty.
    int    frameCount   = 0;
    UInt64 audioSampleCnt = 0;

    while (true) {
        QueuedFrame qf;
        {
            std::unique_lock<std::mutex> lock(g_mtx);
            g_cv.wait(lock, [] { return !g_q.empty() || g_stopRequested.load(); });
            if (g_q.empty()) {
                // Stop requested and queue drained -> exit loop.
                if (g_stopRequested.load()) break;
                continue;
            }
            qf = std::move(g_q.front());
            g_q.pop();
        }

        if (qf.kind == FrameKind::Video) {
            writeVideoSampleW(
                writer.Get(), videoStreamIdx, qf.data,
                (LONGLONG)frameCount * frameDur100ns(init.fps),
                frameDur100ns(init.fps));
            frameCount++;
        } else {
            writeVideoSampleW(
                writer.Get(), audioStreamIdx, qf.data,
                (LONGLONG)audioSampleCnt * audioSampleDur100ns(AUDIO_SAMPLE_RATE),
                (LONGLONG)qf.audioFrames * audioSampleDur100ns(AUDIO_SAMPLE_RATE));
            audioSampleCnt += (UInt64)qf.audioFrames;
        }
    }

    // 5) Finalize. MFShutdown is NOT called here -- it's a one-shot at
    //    process exit (registered via std::atexit in ensureMFStartupOnce).
    if (frameCount > 0) {
        writer->Finalize();
    }
    writer.Reset();
    if (coOwned) CoUninitialize();
}

// --- Public producer-side helpers (emu / main thread) ------------------------

// Push a video frame to the worker queue. Caller-owned BGRA32 bytes are copied.
static void recorderAddFrame(const void* buffer, int length)
{
    if (!g_recActive.load()) return;
    QueuedFrame qf;
    qf.kind = FrameKind::Video;
    qf.data.assign((const uint8_t*)buffer, (const uint8_t*)buffer + length);
    qf.audioFrames = 0;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_q.push(std::move(qf));
    }
    g_cv.notify_one();
}

// audioFrames = stereo frames (NOT individual Int16 samples).
static void recorderAddSound(const Int16* samples, int audioFrames)
{
    if (!g_recActive.load()) return;
    if (audioFrames <= 0) return;
    int byteLen = audioFrames * (int)AUDIO_BLOCK_ALIGN;

    QueuedFrame qf;
    qf.kind = FrameKind::Audio;
    qf.data.assign((const uint8_t*)samples, (const uint8_t*)samples + byteLen);
    qf.audioFrames = audioFrames;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_q.push(std::move(qf));
    }
    g_cv.notify_one();
}

// Spawns the worker thread and blocks until init completes (success or fail).
// Returns 1 on success, 0 on failure (with error MessageBox already shown).
static int startWorker(const char* filename, int fps, int width, int height)
{
    // Ensure MF runtime is up. Idempotent across recordings -- only the first
    // call actually invokes MFStartup; subsequent calls are no-ops. Pairs with
    // an atexit() MFShutdown that runs at process exit.
    ensureMFStartupOnce();

    {
        std::lock_guard<std::mutex> lock(g_mtx);
        while (!g_q.empty()) g_q.pop();
    }
    g_recActive    = false;
    g_stopRequested= false;
    g_initOk       = false;
    g_initErrTag.clear();
    g_initErrHr    = S_OK;

    if (g_initDoneEvent == NULL) {
        g_initDoneEvent = CreateEvent(NULL, /*manualReset*/ TRUE, /*initial*/ FALSE, NULL);
        if (!g_initDoneEvent) return 0;
    } else {
        ResetEvent(g_initDoneEvent);
    }

    WorkerInit init;
    {
        wchar_t wpath[1024];
        PathToWide(filename, wpath, (int)_countof(wpath));
        init.filename = wpath;
        init.fps      = fps;
        init.width    = width;
        init.height   = height;
    }

    g_worker = std::thread(mfWorkerThreadEntry, std::move(init));

    // Wait for the worker to finish init; up to 10 s for slow disk / first-run
    // codec resolution. If the worker dies before signalling, join will work.
    DWORD wr = WaitForSingleObject(g_initDoneEvent, 10000);
    if (wr != WAIT_OBJECT_0) {
        // Best-effort tear-down: signal stop and join.
        g_stopRequested = true;
        g_cv.notify_one();
        if (g_worker.joinable()) g_worker.join();
        return 0;
    }

    if (!g_initOk.load()) {
        // Init failed; worker has already cleaned up and exited. Surface the
        // specific error to the user from the main (UI) thread.
        char tagUtf8[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, g_initErrTag.c_str(), -1,
                            tagUtf8, sizeof(tagUtf8), NULL, NULL);
        char msg[512];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                    "%s\nHRESULT = 0x%08lX",
                    tagUtf8, (long)g_initErrHr);
        MessageBoxU(g_hwnd, msg, langErrorRecorderTitle(), MB_ICONERROR | MB_OK);
        if (g_worker.joinable()) g_worker.join();
        return 0;
    }

    g_recActive = true;
    return 1;
}

// Signals the worker to drain and exit, then joins.
static void stopWorker(void)
{
    if (!g_worker.joinable()) return;
    g_recActive     = false;
    g_stopRequested = true;
    g_cv.notify_one();
    g_worker.join();
}

// --- Mixer sink wiring (recorder's audio capture path) -----------------------
struct RecorderSound {
    Mixer* mixer;
};

static Int32 recorderSoundWrite(void* dummy, Int16* buffer, UInt32 count)
{
    (void)dummy;
    // Mixer reports `count` as individual Int16 samples; convert to stereo frames.
    count /= 2;
    recorderAddSound(buffer, (int)count);
    return 0;
}

extern "C" RecorderSound* recorderSoundCreate(HWND hwnd, Mixer* mixer,
                                              UInt32 sampleRate, UInt32 bufferSize,
                                              Int16 channels)
{
    (void)hwnd; (void)sampleRate; (void)bufferSize; (void)channels;
    RecorderSound* s = (RecorderSound*)calloc(1, sizeof(RecorderSound));
    s->mixer = mixer;
    mixerSetStereo(mixer, 1);
    mixerSetWriteCallback(mixer, recorderSoundWrite, NULL, 128);
    return s;
}

extern "C" void recorderSoundDestroy(RecorderSound* s)
{
    mixerSetWriteCallback(s->mixer, NULL, NULL, 0);
    free(s);
}

extern "C" void recorderSoundSuspend(RecorderSound* s) { (void)s; }
extern "C" void recorderSoundResume (RecorderSound* s) { (void)s; }

// --- Pixel stretch helpers (unchanged from Win32Avi.c) -----------------------
#define stretchLine(TYPE, dst, dstLen, src, srcLen, M, N, R) do {           \
    TYPE*  d = (TYPE*)(dst);                                                \
    TYPE*  s = (TYPE*)(src);                                                \
    UInt32 a = *s++;                                                        \
    UInt32 b = *s++;                                                        \
    int    n = 0;                                                           \
    int    w;                                                               \
    for (w = 0; w < (dstLen); w++) {                                        \
        int q = (R) * n / (dstLen);                                         \
        int p = (R) - q;                                                    \
        d[w] = (TYPE)(((((a&(M))*p+(b&(M))*q)/(R))&(M))                     \
                    |((((a&(N))*p+(b&(N))*q)/(R))&(N)));                    \
        n += (srcLen);                                                      \
        if (n >= (dstLen)) {                                                \
            n -= (dstLen);                                                  \
            a = b;                                                          \
            b = *s++;                                                       \
        }                                                                   \
    }                                                                       \
} while (0)

static void stretchImage(char* dst, int dstWidth, char* src, int srcWidth,
                         int pitch, int height, int bitDepth)
{
    for (int h = 0; h < height; h++) {
        if (bitDepth == 16)
            stretchLine(UInt16, dst, dstWidth, src, srcWidth, 0xf81f, 0x07e0, 32);
        else
            stretchLine(UInt32, dst, dstWidth, src, srcWidth, 0x00ff00ff, 0x0000ff00, 256);
        dst += pitch;
        src += pitch;
    }
}

// --- Periodic frame callback (offline render path) ---------------------------
static void recorderVideoCallback(void* dummy, UInt32 time)
{
    (void)dummy; (void)time;
    // 4 BGRA bytes * max 640 * (480 + 1 spare row for stretchImage src offset)
    static char displayData[4 * 640 * (480 + 1)];

    int width        = 320 * g_zoom;
    int height       = 240 * g_zoom;
    int displayPitch = width * 4;

    int bitDepth      = 32;
    int bytesPerPixel = bitDepth / 8;
    char* dpyData     = displayData;

    FrameBuffer* frameBuffer = frameBufferFlipViewFrame(1);
    if (frameBuffer == NULL) frameBuffer = frameBufferGetWhiteNoiseFrame();

    int borderWidth = (320 - frameBuffer->maxWidth) * g_zoom / 2;

    if (g_properties->video.horizontalStretch) {
        if (borderWidth > 0) {
            // Render one row lower so stretchImage can pull from displayPitch offset.
            videoRender(g_video, frameBuffer, bitDepth, g_zoom,
                        dpyData + (height - 1 + 1) * displayPitch, 0,
                        -1 * displayPitch, -1);
            stretchImage(dpyData, width, dpyData + displayPitch,
                         width - 2 * borderWidth, displayPitch, height, bitDepth);
            borderWidth = 0;
        } else {
            videoRender(g_video, frameBuffer, bitDepth, g_zoom,
                        dpyData + (height - 1) * displayPitch, 0,
                        -1 * displayPitch, -1);
        }
    } else {
        videoRender(g_video, frameBuffer, bitDepth, g_zoom,
                    dpyData + (height - 1) * displayPitch + borderWidth * bytesPerPixel,
                    0, -1 * displayPitch, -1);
        if (borderWidth > 0) {
            int h = height;
            char* p = dpyData;
            while (h--) {
                memset(p, 0, borderWidth * bytesPerPixel);
                memset(p + (width - borderWidth) * bytesPerPixel, 0, borderWidth * bytesPerPixel);
                p += displayPitch;
            }
        }
    }

    recorderAddFrame(displayData, width * height * 4);
    /* Frame-granular finish check (see Board.c); cap.timer's 50s tick
    ** would otherwise let short replays overrun. */
    boardCaptureCheckFinish();
}

// --- File dialog -------------------------------------------------------------
static void replaceCharInString(char* str, char oldChar, char newChar)
{
    while (*str) { if (*str == oldChar) *str = newChar; str++; }
}

static char* recorderGetFilename(Properties* properties)
{
    (void)properties;
    char* title = langDlgSaveVideoClipAs();
    char  extensionList[512];
    char  defaultDir[512] = "";
    char* extensions = (char*)".mp4\0";
    char* filename;
    int   selectedExtension = 0;

    {
        const char* vdir = actionGetVideoCaptureDir();
        if (vdir && vdir[0]) {
            strncpy(defaultDir, vdir, sizeof(defaultDir) - 1);
            defaultDir[sizeof(defaultDir) - 1] = 0;
        }
    }

    sprintf(extensionList, "Video file   (*.mp4)#*.mp4#");
    replaceCharInString(extensionList, '#', 0);

    filename = archFileSave(title, extensionList, defaultDir, extensions, &selectedExtension, ".mp4");
    return filename;
}

// --- Status dialog -----------------------------------------------------------
static char* progressText()
{
    static char text[128];
    int amount = boardCaptureCompleteAmount();
    /* Clamp; emu can briefly overshoot endTime64 between callbacks. */
    if (amount < 0)    amount = 0;
    if (amount > 1000) amount = 1000;
    sprintf(text, "%s %d.%d%%", langDlgAmountCompleted(), amount / 10, amount % 10);
    return text;
}

static BOOL CALLBACK statusDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (iMsg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(hDlg, TRUE); return TRUE; }
        break;
    case WM_CLOSE:
        return TRUE;
    case WM_TIMER:
        SetDlgItemTextU(hDlg, IDC_VIDEOPROGRESSTEXT, progressText());
        if (!boardCaptureIsPlaying()) EndDialog(hDlg, TRUE);
        return FALSE;
    case WM_DESTROY:
        KillTimer(hDlg, 2);
        return 0;
    case WM_INITDIALOG:
        SetWindowTextU(hDlg, langDlgRenderVideoCapture());
        /* Show "0.0%": WM_INITDIALOG runs after emu started so progressText
        ** would already report mid-replay. */
        {
            char initText[128];
            sprintf(initText, "%s 0.0%%", langDlgAmountCompleted());
            SetDlgItemTextU(hDlg, IDC_VIDEOPROGRESSTEXT, initText);
        }
        SetTimer(hDlg, 2, 250, NULL);
        win32CommonApplyDark(hDlg);
        return FALSE;
    }
    return FALSE;
}

// --- Public lifecycle (replaces aviStartRender / aviStopRender) --------------
extern "C" void recorderStartRender(HWND hwndOwner, Properties* prop, Video* vid)
{
    g_hwnd       = hwndOwner;
    g_properties = prop;
    g_video      = vid;
    g_syncMethod = prop->emulation.syncMethod;
    g_emuSpeed   = prop->emulation.speed;

    g_zoom = (prop->video.captureSize == 0) ? 1 : 2;
    g_fps  = prop->video.captureFps;

    if (prop->filehistory.videocap[0] == 0 || !archFileExists(prop->filehistory.videocap)) {
        char msg[1024];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE, langErrorRecorderNoReplay(),
                    prop->filehistory.videocap[0] ? prop->filehistory.videocap : "");
        MessageBoxU(hwndOwner, msg, langErrorRecorderTitle(), MB_ICONWARNING | MB_OK);
        return;
    }

    actionEmuStop();

    char* filename = recorderGetFilename(prop);
    if (filename == NULL) return;

    int width  = 320 * g_zoom;
    int height = 240 * g_zoom;

    boardSetPeriodicCallback(recorderVideoCallback, NULL, prop->video.captureFps);
    prop->emulation.syncMethod = P_EMU_SYNCIGNORE;
    mixerSetBoardFrequencyFixed(3579545);
    actionEmuSpeedSet(100);
    frameBufferSetFrameCount(4);

    soundDriverConfig(g_hwnd, SOUND_DRV_AVI);
    emulatorRestartSound();

    if (!startWorker(filename, prop->video.captureFps, width, height)) {
        // Worker failed to init; restore emu state and bail.
        boardSetPeriodicCallback(NULL, NULL, 0);
        prop->emulation.syncMethod = g_syncMethod;
        soundDriverConfig(g_hwnd, (SoundDriver)prop->sound.driver);
        emulatorRestartSound();
        return;
    }

    /* Set g_rendering before play so recorderStopRender can fully roll
    ** back if replay state-load fails (missing ROM / disk / BIOS). */
    g_rendering = 1;
    actionVideoCapturePlay();

    /* Bail if emu start failed (pre-validation showed dialog, state is
    ** EMU_STOPPED) so we don't produce an empty MP4. */
    if (emulatorGetState() == EMU_STOPPED) {
        recorderStopRender();
        return;
    }

    DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_RENDERVIDEO), g_hwnd, (DLGPROC)statusDlgProc);

    actionEmuStop();
    recorderStopRender();
}

extern "C" void recorderStopRender(void)
{
    if (!g_rendering) return;
    g_rendering = 0;

    // Drain + finalize on worker, then join. Worker handles MFShutdown +
    // CoUninitialize for its thread; main STA stays untouched.
    stopWorker();

    soundDriverConfig(g_hwnd, (SoundDriver)g_properties->sound.driver);
    emulatorRestartSound();

    g_properties->emulation.syncMethod = g_syncMethod;
    /* Restore the live buffer count: SYNCNONE = 1, else 3. */
    switch (g_properties->emulation.syncMethod) {
    case P_EMU_SYNCNONE:
        frameBufferSetFrameCount(1); break;
    default:
        frameBufferSetFrameCount(3);
    }

    mixerSetBoardFrequencyFixed(0);
    actionEmuSpeedSet(g_emuSpeed);
    boardSetPeriodicCallback(NULL, NULL, 0);
}

extern "C" void recorderRestorePropsAtExit(void)
{
    // Restore syncMethod/speed snapshot if recorderStopRender never ran
    // (e.g. user closed app mid-render); otherwise the stomped values
    // would persist to INI and next launch boots at max speed.
    if (g_rendering && g_properties) {
        g_properties->emulation.syncMethod = g_syncMethod;
        g_properties->emulation.speed      = g_emuSpeed;
    }
}
