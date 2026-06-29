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
#include <codecapi.h>
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
#include "Win32Toast.h"
#include "Win32Common.h"
#include "ArchFile.h"
#include "ArchNotifications.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "Resource.h"
#include "Language.h"
#include "PacketFileSystem.h"

// archFileExists / archFileOpen are defined in Win32.c but not declared in
// ArchFile.h (only the Save side is). Forward-declare locally.
int   archFileExists(const char* fileName);
char* archFileOpen(char* title, char* extensionList, char* defaultDir,
                   char* extensions, int* selectedExtension,
                   char* defautExtension, int createFileSize);
}

using Microsoft::WRL::ComPtr;

// --- Encoder constants -------------------------------------------------------
// Video bitrate is computed per session in mfWorkerThreadEntry() and clamped
// to these bounds (HDR uses higher caps for the wider chroma + PQ overhead).
#define VIDEO_BITRATE_SDR_MIN  4000000U   /*  4 Mbps */
#define VIDEO_BITRATE_SDR_MAX 16000000U   /* 16 Mbps */
#define VIDEO_BITRATE_HDR_MIN  6000000U   /*  6 Mbps */
#define VIDEO_BITRATE_HDR_MAX 24000000U   /* 24 Mbps */
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
    bool         hdr;        // true = HEVC main10 + BT.2020 + PQ + A2R10G10B10 input
    int          codec;      // CAP_VIDEO_H264 or CAP_VIDEO_HEVC (Phase F).
                             // HDR forces HEVC regardless of this setting.
};

// Video frames live in a pre-allocated pool (no per-push multi-MB alloc).
// Audio chunks stay inline -- small enough that pooling is not worthwhile.
enum class FrameKind { Video, Audio };
struct QueuedFrame {
    FrameKind            kind;
    int                  videoSlotIdx; // -1 for audio
    size_t               videoSize;    // valid bytes in g_videoPool[slot]
    std::vector<uint8_t> audioData;    // empty for video
    int                  audioFrames;  // for audio: # of stereo frames
    LONGLONG             time100ns;    // wall-clock PTS for live mode (>=0 = use as-is);
                                       // <0 means "use sequential count" (offline path).
};

static std::mutex                 g_mtx;
static std::condition_variable    g_cv;       // worker waits for items
static std::condition_variable    g_qSpace;   // producers wait for a free video slot
static std::queue<QueuedFrame>    g_q;
// Video buffer pool. Each slot pre-sized to width*height*4 at session start.
static std::vector<std::vector<uint8_t>> g_videoPool;
static std::deque<int>                   g_videoFreeSlots;
static const int                  VIDEO_POOL_SLOTS = 64;
// Audio chunk backpressure: small per-chunk, but unbounded growth fills MF's
// internal queue and blocks WriteSample so fast-stop never reaches the Flush.
static size_t                     g_audioQueuedCount = 0;
static const size_t               AUDIO_QUEUE_MAX = 64;
static std::atomic<bool>          g_recActive{false};   // producer pushes only while true
static std::atomic<bool>          g_stopRequested{false};
/* Live stop wants a fast exit (drop pending + Flush) since the SW HEVC
** encoder can be slower than realtime. Offline leaves this false so the
** worker drains the queue naturally. */
static std::atomic<bool>          g_abandonQueue{false};
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
// Post-render readback path: enabled when the user is on the DX12 driver, so
// recorded MP4s carry the same shader effects (PAL filter, scanlines, monitor
// colour, etc.) as the live window. Disabled otherwise -- the legacy CPU
// framebuffer path stays as the fallback for DD / GDI users. Driven by
// recorderStartRender; the callback uses g_postRender as a runtime selector.
static int         g_postRender = 0;
/* 1 while a session uses the HDR capture path (PQ readback + P010 BT.2020);
** skips the SDR-only preview which assumes BGRA8 bottom-up. */
static int         g_recHdrActive = 0;
static std::vector<uint8_t> g_postRenderBuf;

// Live shares the offline capture path (1280x960 MP4); live taps Mixer for
// audio, offline replaces the sound driver.
static std::atomic<int> g_liveActive{0};
/* Set while recorderStopLive drains the encoder + tears down DX12. Guards
** recorderStartLive against racing a new session into a finalizing one. */
static std::atomic<int> g_stoppingLive{0};
static int              g_liveW = 0;
static int              g_liveH = 0;
static char             g_liveFilename[1024] = "";

// Live preview shown inside the modal status dialog while recording.
//
// Lock-free seqlock between producer (emu thread) and consumer (main
// thread): seq odd while writing, even when committed; consumer
// retries if seq changed.
#define WM_RECORDER_FRAME_READY (WM_USER + 1)
static std::vector<uint8_t>  g_previewBuf;          // producer-owned, fixed size
static int                   g_previewW   = 0;
static int                   g_previewH   = 0;
static std::atomic<uint64_t> g_previewSeq{0};
static size_t                g_previewBufBytes = 0; // pre-allocated capacity
static HWND                  g_previewDlg = NULL;
/* HDR offline preview is a child HWND inside the modal dialog hosting a
** D3D12 PQ swap chain.  GDI can't display HDR, so the SDR preview path
** (g_previewBuf + StretchDIBits) is bypassed when HDR recording is active. */
static HWND                  g_previewHdrChild = NULL;
static const int             PREVIEW_W        = 960;
static const int             PREVIEW_H        = 720;
static const int             PREVIEW_PAD      = 8;
// Snapshot every Nth frame so StretchDIBits + memcpy stay cheap;
// 10 -> ~6 fps preview at 60 fps record.
static const int             PREVIEW_DECIMATE = 10;
static int                   g_previewSnapCnt = 0;

static void recorderSnapshotPreview(const void* bgraBuf, int width, int height)
{
    if ((g_previewSnapCnt++ % PREVIEW_DECIMATE) != 0) return;

    size_t bytes = (size_t)width * (size_t)height * 4;
    if (bytes == 0 || bytes > g_previewBufBytes) return;

    uint64_t seq = g_previewSeq.load(std::memory_order_relaxed);
    // Begin write (odd seq).
    g_previewSeq.store(seq + 1, std::memory_order_release);

    memcpy(g_previewBuf.data(), bgraBuf, bytes);
    g_previewW = width;
    g_previewH = height;

    // End write (even seq).
    g_previewSeq.store(seq + 2, std::memory_order_release);

    HWND dlg = g_previewDlg;
    if (dlg) PostMessageW(dlg, WM_RECORDER_FRAME_READY, 0, 0);
}

// 100-ns tick at sample index `count` for stream rate `rate`; the
// 64-bit form (count*1e7)/rate avoids the drift the obvious 1e7/rate
// truncation accumulates.
static LONGLONG ticks100nsFromCount(UInt64 count, UINT32 rate)
{
    return (LONGLONG)((count * 10000000ULL) / (UInt64)rate);
}

// --- Worker: build IMFSample from raw bytes and call WriteSample -------------
static HRESULT writeVideoSampleW(IMFSinkWriter* writer, DWORD streamIdx,
                                 const uint8_t* data, size_t dataLen,
                                 LONGLONG time100ns, LONGLONG dur100ns)
{
    HRESULT hr;
    ComPtr<IMFMediaBuffer> mfBuf;
    hr = MFCreateMemoryBuffer((DWORD)dataLen, &mfBuf);
    if (FAILED(hr)) return hr;

    BYTE* dst = NULL;
    hr = mfBuf->Lock(&dst, NULL, NULL);
    if (FAILED(hr)) return hr;
    memcpy(dst, data, dataLen);
    mfBuf->Unlock();
    mfBuf->SetCurrentLength((DWORD)dataLen);

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return hr;
    sample->AddBuffer(mfBuf.Get());
    sample->SetSampleTime(time100ns);
    sample->SetSampleDuration(dur100ns);

    return writer->WriteSample(streamIdx, sample.Get());
}

// Convert one R10G10B10A2_UNORM PQ-encoded frame to P010 (10-bit YUV 4:2:0,
// BT.2020 limited range), the standard input for HEVC main10 + HDR10.
// Source: 32-bit pixels, w*h, top-down (DXGI native R10G10B10A2 layout =
// bits 0-9 R, 10-19 G, 20-29 B, 30-31 A in the DWORD).
// Destination layout (P010):
//   Y plane: w*h * 16-bit (10-bit value packed in upper 10 bits of each slot).
//   UV plane (interleaved Cb,Cr): (w/2)*(h/2) * 32-bit per pair.
//   Total bytes: w*h*2 + w*h = w*h*3.
// The dst buffer must be at least w*h*3 bytes.  w and h must be even.
static void convertR10G10B10A2_PQ_to_P010_BT2020(const uint8_t* src, int srcPitch,
                                                  int w, int h, uint8_t* dst)
{
    const int yPitchBytes  = w * 2;          // bytes per Y row
    const int uvPitchBytes = w * 2;          // bytes per UV row (interleaved Cb/Cr, half-res Y rows)
    uint16_t* yPlane  = (uint16_t*)dst;
    uint16_t* uvPlane = (uint16_t*)(dst + (size_t)w * h * 2);

    // Y plane: full resolution, BT.2020 luma in PQ-domain RGB.
    for (int y = 0; y < h; y++) {
        const uint32_t* srcRow = (const uint32_t*)(src + (size_t)y * srcPitch);
        uint16_t* yRow = (uint16_t*)((uint8_t*)yPlane + (size_t)y * yPitchBytes);
        for (int x = 0; x < w; x++) {
            uint32_t pix = srcRow[x];
            uint32_t R = pix & 0x3FF;
            uint32_t G = (pix >> 10) & 0x3FF;
            uint32_t B = (pix >> 20) & 0x3FF;
            float r = R / 1023.0f;
            float g = G / 1023.0f;
            float b = B / 1023.0f;
            float yPQ = 0.2627f * r + 0.6780f * g + 0.0593f * b;
            int yLim = 64 + (int)(yPQ * 876.0f + 0.5f);
            if (yLim < 0)    yLim = 0;
            if (yLim > 1023) yLim = 1023;
            yRow[x] = (uint16_t)(yLim << 6);
        }
    }

    // UV plane: 2x2 chroma subsampling.  BT.2020 NCL Cb/Cr from averaged RGB.
    int uvW = w / 2;
    int uvH = h / 2;
    for (int y = 0; y < uvH; y++) {
        const uint32_t* srcRow0 = (const uint32_t*)(src + (size_t)(y*2  ) * srcPitch);
        const uint32_t* srcRow1 = (const uint32_t*)(src + (size_t)(y*2+1) * srcPitch);
        uint16_t* uvRow = (uint16_t*)((uint8_t*)uvPlane + (size_t)y * uvPitchBytes);
        for (int x = 0; x < uvW; x++) {
            uint32_t p00 = srcRow0[x*2  ];
            uint32_t p01 = srcRow0[x*2+1];
            uint32_t p10 = srcRow1[x*2  ];
            uint32_t p11 = srcRow1[x*2+1];
            float r = (((p00      ) & 0x3FF) + ((p01      ) & 0x3FF)
                     + ((p10      ) & 0x3FF) + ((p11      ) & 0x3FF)) * (0.25f / 1023.0f);
            float g = (((p00 >> 10) & 0x3FF) + ((p01 >> 10) & 0x3FF)
                     + ((p10 >> 10) & 0x3FF) + ((p11 >> 10) & 0x3FF)) * (0.25f / 1023.0f);
            float b = (((p00 >> 20) & 0x3FF) + ((p01 >> 20) & 0x3FF)
                     + ((p10 >> 20) & 0x3FF) + ((p11 >> 20) & 0x3FF)) * (0.25f / 1023.0f);
            float yPQ = 0.2627f * r + 0.6780f * g + 0.0593f * b;
            float cbPQ = (b - yPQ) * (1.0f / 1.8814f);
            float crPQ = (r - yPQ) * (1.0f / 1.4746f);
            int cbLim = 512 + (int)(cbPQ * 896.0f + 0.5f);
            int crLim = 512 + (int)(crPQ * 896.0f + 0.5f);
            if (cbLim < 0)    cbLim = 0;
            if (cbLim > 1023) cbLim = 1023;
            if (crLim < 0)    crLim = 0;
            if (crLim > 1023) crLim = 1023;
            uvRow[x*2 + 0] = (uint16_t)(cbLim << 6);
            uvRow[x*2 + 1] = (uint16_t)(crLim << 6);
        }
    }
}

// --- Recorder-specific atexit safety net -------------------------------------
//
// Process-exit safety net: detach the recorder worker so std::thread's
// dtor doesn't terminate(); registered after ensureMFStartupOnce() so
// it runs before MFShutdown.
static void atexitDetachWorker(void)
{
    if (g_worker.joinable()) {
        // Tell producers / worker to stop spinning -- best effort, we don't
        // wait for the worker to actually finish since MF objects may already
        // be in indeterminate state at process-shutdown time.
        g_recActive.store(false);
        g_stopRequested.store(true);
        g_cv.notify_all();
        g_qSpace.notify_all();
        g_worker.detach();
    }
}

static std::once_flag g_workerAtexitOnce;
static void registerWorkerAtexitOnce(void)
{
    std::call_once(g_workerAtexitOnce, [] {
        std::atexit(atexitDetachWorker);
    });
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

    // Prefer HW encoders (SW H.264 can't keep up at 1280x960x60).
    // Default MF throttling is left on so a slow encoder back-pressures
    // the writer instead of letting the internal sample queue grow.
    ComPtr<IMFAttributes> writerAttrs;
    HRESULT hr = MFCreateAttributes(&writerAttrs, 1);
    if (SUCCEEDED(hr)) {
        writerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }
    hr = MFCreateSinkWriterFromURL(init.filename.c_str(), NULL, writerAttrs.Get(), &writer);
    if (FAILED(hr)) { fail(L"MFCreateSinkWriterFromURL failed", hr); return; }

    // ----- Video output -----
    // SDR: H.264 High BT.709.  HDR: HEVC main10 BT.2020 + PQ with HDR10
    // metadata.  Bitrate ~0.08 bpp SDR / ~0.10 bpp HDR.
    {
        const UINT64 bppfTotal = (UINT64)init.width * (UINT64)init.height * (UINT64)init.fps;
        UINT32 videoBitrate;
        if (init.hdr) {
            videoBitrate = (UINT32)((bppfTotal * 8) / 80);   // ~0.10 bpp
            if (videoBitrate < VIDEO_BITRATE_HDR_MIN) videoBitrate = VIDEO_BITRATE_HDR_MIN;
            if (videoBitrate > VIDEO_BITRATE_HDR_MAX) videoBitrate = VIDEO_BITRATE_HDR_MAX;
        } else {
            videoBitrate = (UINT32)((bppfTotal * 8) / 100);  // ~0.08 bpp
            if (videoBitrate < VIDEO_BITRATE_SDR_MIN) videoBitrate = VIDEO_BITRATE_SDR_MIN;
            if (videoBitrate > VIDEO_BITRATE_SDR_MAX) videoBitrate = VIDEO_BITRATE_SDR_MAX;
        }

        ComPtr<IMFMediaType> outVid;
        hr = MFCreateMediaType(&outVid);
        if (FAILED(hr)) { fail(L"MFCreateMediaType (out video) failed", hr); return; }
        outVid->SetGUID  (MF_MT_MAJOR_TYPE,     MFMediaType_Video);
        if (init.hdr) {
            outVid->SetGUID  (MF_MT_SUBTYPE,             MFVideoFormat_HEVC);
            outVid->SetUINT32(MF_MT_MPEG2_PROFILE,       eAVEncH265VProfile_Main_420_10);
            /* BT.2020 / PQ / limited-range -- HDR10 baseline. */
            outVid->SetUINT32(MF_MT_VIDEO_PRIMARIES,     MFVideoPrimaries_BT2020);
            outVid->SetUINT32(MF_MT_TRANSFER_FUNCTION,   MFVideoTransFunc_2084);
            outVid->SetUINT32(MF_MT_YUV_MATRIX,          MFVideoTransferMatrix_BT2020_10);
            outVid->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
            /* HDR10 mastering display + content light level metadata
            ** (Rec.2020 primaries + 1000/0.005 nits reference display +
            ** MaxCLL/MaxFALL = paperWhite, the practical content peak). */
            MT_CUSTOM_VIDEO_PRIMARIES prims = {};
            prims.fRx = 0.708f; prims.fRy = 0.292f;
            prims.fGx = 0.170f; prims.fGy = 0.797f;
            prims.fBx = 0.131f; prims.fBy = 0.046f;
            prims.fWx = 0.3127f; prims.fWy = 0.3290f;
            outVid->SetBlob(MF_MT_CUSTOM_VIDEO_PRIMARIES,
                            (const UINT8*)&prims, sizeof(prims));
            /* ST.2086 mastering luminance: max in cd/m^2, min in 0.0001 cd/m^2. */
            outVid->SetUINT32(MF_MT_MAX_MASTERING_LUMINANCE,             1000);
            outVid->SetUINT32(MF_MT_MIN_MASTERING_LUMINANCE,               50);
            /* CEA-861.3 content light level (nits): MaxCLL tracks paperWhite,
            ** MaxFALL = paperWhite/2. Mirrors vApplyHdrMetadata. */
            Properties* gpRec = propGetGlobalProperties();
            UINT32 paperWhite = (UINT32)(gpRec ? gpRec->video.hdrPaperWhiteNits : 200);
            if (paperWhite < 80)  paperWhite = 80;
            if (paperWhite > 400) paperWhite = 400;
            outVid->SetUINT32(MF_MT_MAX_LUMINANCE_LEVEL,                paperWhite);
            outVid->SetUINT32(MF_MT_MAX_FRAME_AVERAGE_LUMINANCE_LEVEL,  paperWhite / 2);
        } else if (init.codec == CAP_VIDEO_HEVC) {
            /* HEVC SDR: 8-bit main profile, BT.709. */
            outVid->SetGUID  (MF_MT_SUBTYPE,             MFVideoFormat_HEVC);
            outVid->SetUINT32(MF_MT_MPEG2_PROFILE,       eAVEncH265VProfile_Main_420_8);
            outVid->SetUINT32(MF_MT_VIDEO_PRIMARIES,     MFVideoPrimaries_BT709);
            outVid->SetUINT32(MF_MT_TRANSFER_FUNCTION,   MFVideoTransFunc_709);
            outVid->SetUINT32(MF_MT_YUV_MATRIX,          MFVideoTransferMatrix_BT709);
            outVid->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        } else {
            outVid->SetGUID  (MF_MT_SUBTYPE,        MFVideoFormat_H264);
            outVid->SetUINT32(MF_MT_MPEG2_PROFILE,  eAVEncH264VProfile_High);
        }
        outVid->SetUINT32(MF_MT_AVG_BITRATE,    videoBitrate);
        outVid->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize (outVid.Get(), MF_MT_FRAME_SIZE, (UINT32)init.width, (UINT32)init.height);
        MFSetAttributeRatio(outVid.Get(), MF_MT_FRAME_RATE, (UINT32)init.fps, 1);
        MFSetAttributeRatio(outVid.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->AddStream(outVid.Get(), &videoStreamIdx);
        if (FAILED(hr)) {
            const wchar_t* tag = init.hdr                       ? L"AddStream (video HEVC main10) failed"
                              : (init.codec == CAP_VIDEO_HEVC)  ? L"AddStream (video HEVC main) failed"
                                                                : L"AddStream (video H.264) failed";
            fail(tag, hr);
            return;
        }
    }

    // ----- Video input -----
    // SDR: BGRA32 bottom-up.  HDR: P010 top-down; MF's A2R10G10B10
    // auto-converter is broken, so we hand the encoder its native
    // P010 BT.2020 directly via convertR10G10B10A2_PQ_to_P010_BT2020.
    {
        ComPtr<IMFMediaType> inVid;
        hr = MFCreateMediaType(&inVid);
        if (FAILED(hr)) { fail(L"MFCreateMediaType (in video) failed", hr); return; }
        inVid->SetGUID  (MF_MT_MAJOR_TYPE,     MFMediaType_Video);
        if (init.hdr) {
            inVid->SetGUID  (MF_MT_SUBTYPE,            MFVideoFormat_P010);
            inVid->SetUINT32(MF_MT_DEFAULT_STRIDE,     (UINT32)(init.width * 2));  /* top-down, 16-bit Y */
            inVid->SetUINT32(MF_MT_VIDEO_PRIMARIES,    MFVideoPrimaries_BT2020);
            inVid->SetUINT32(MF_MT_TRANSFER_FUNCTION,  MFVideoTransFunc_2084);
            inVid->SetUINT32(MF_MT_YUV_MATRIX,         MFVideoTransferMatrix_BT2020_10);
            inVid->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235);
        } else {
            inVid->SetGUID  (MF_MT_SUBTYPE,            MFVideoFormat_RGB32);
            inVid->SetUINT32(MF_MT_DEFAULT_STRIDE,     (UINT32)(-init.width * 4));  /* bottom-up */
        }
        inVid->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize (inVid.Get(), MF_MT_FRAME_SIZE, (UINT32)init.width, (UINT32)init.height);
        MFSetAttributeRatio(inVid.Get(), MF_MT_FRAME_RATE, (UINT32)init.fps, 1);
        MFSetAttributeRatio(inVid.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->SetInputMediaType(videoStreamIdx, inVid.Get(), NULL);
        if (FAILED(hr)) { fail(init.hdr ? L"SetInputMediaType (video A2R10G10B10) failed"
                                        : L"SetInputMediaType (video BGRA32) failed", hr); return; }
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
    int      frameCount         = 0;
    UInt64   audioSampleCnt     = 0;
    LONGLONG lastVideoTime100ns = -1;

    // P010 scratch buffer for HDR -- worker-thread-local so the conversion
    // doesn't compete with emu thread / capture path for memory locality.
    // Sized w*h*3 (Y plane 16-bit + UV plane 4:2:0 16-bit interleaved).
    std::vector<uint8_t> p010Scratch;
    if (init.hdr) {
        p010Scratch.resize((size_t)init.width * (size_t)init.height * 3);
    }

    while (true) {
        QueuedFrame qf;
        {
            std::unique_lock<std::mutex> lock(g_mtx);
            g_cv.wait(lock, [] { return !g_q.empty() || g_stopRequested.load(); });
            if (g_stopRequested.load() && g_abandonQueue.load()) {
                /* Fast stop: drop pending so worker.join() returns even when
                ** the encoder is throttling WriteSample. Video slots are
                ** released back to the pool. */
                while (!g_q.empty()) {
                    QueuedFrame drop = std::move(g_q.front());
                    g_q.pop();
                    if (drop.kind == FrameKind::Video) {
                        g_videoFreeSlots.push_back(drop.videoSlotIdx);
                    } else if (g_audioQueuedCount > 0) {
                        --g_audioQueuedCount;
                    }
                }
                break;
            }
            if (g_q.empty()) {
                // Stop requested and queue drained -> exit loop.
                if (g_stopRequested.load()) break;
                continue;
            }
            qf = std::move(g_q.front());
            g_q.pop();
        }

        if (qf.kind == FrameKind::Video) {
            LONGLONG t0, dur;
            if (qf.time100ns >= 0) {
                // Live mode: PTS stamped at push time on the emu thread. Use
                // the previous frame's stamp to derive duration so a dropped
                // emu frame turns into a longer-held still frame in the MP4.
                t0  = qf.time100ns;
                dur = (lastVideoTime100ns >= 0 && t0 > lastVideoTime100ns)
                      ? (t0 - lastVideoTime100ns)
                      : ticks100nsFromCount(1, (UINT32)init.fps);
                lastVideoTime100ns = t0;
            } else {
                // Offline mode: replay drives the clock; deterministic.
                t0  = ticks100nsFromCount((UInt64)frameCount,     (UINT32)init.fps);
                LONGLONG t1 = ticks100nsFromCount((UInt64)frameCount + 1, (UINT32)init.fps);
                dur = t1 - t0;
            }
            const uint8_t* frameBytes = g_videoPool[qf.videoSlotIdx].data();
            size_t         frameSize  = qf.videoSize;
            if (init.hdr) {
                /* HDR path: convert R10G10B10A2_UNORM PQ readback to P010
                ** BT.2020 in worker thread, then feed encoder its native
                ** HDR10 input format. */
                convertR10G10B10A2_PQ_to_P010_BT2020(
                    frameBytes, init.width * 4,
                    init.width, init.height,
                    p010Scratch.data());
                writeVideoSampleW(writer.Get(), videoStreamIdx,
                                  p010Scratch.data(), p010Scratch.size(),
                                  t0, dur);
            } else {
                writeVideoSampleW(writer.Get(), videoStreamIdx,
                                  frameBytes, frameSize,
                                  t0, dur);
            }
            // Return video slot to free pool and wake any waiting producer.
            {
                std::lock_guard<std::mutex> lock(g_mtx);
                g_videoFreeSlots.push_back(qf.videoSlotIdx);
            }
            g_qSpace.notify_one();
            frameCount++;
        } else {
            LONGLONG t0, dur;
            LONGLONG audioDur = ticks100nsFromCount((UInt64)qf.audioFrames, AUDIO_SAMPLE_RATE);
            if (qf.time100ns >= 0) {
                // Live mode: PTS stamped by the mixer-tap thread. Duration is
                // the natural per-chunk audio duration; if frames were dropped
                // the next chunk's PTS jump produces a silence gap in the MP4.
                t0  = qf.time100ns;
                dur = audioDur;
            } else {
                // Offline mode: deterministic sequential count.
                t0  = ticks100nsFromCount(audioSampleCnt,                          AUDIO_SAMPLE_RATE);
                LONGLONG t1 = ticks100nsFromCount(audioSampleCnt + (UInt64)qf.audioFrames, AUDIO_SAMPLE_RATE);
                dur = t1 - t0;
            }
            writeVideoSampleW(writer.Get(), audioStreamIdx,
                              qf.audioData.data(), qf.audioData.size(),
                              t0, dur);
            audioSampleCnt += (UInt64)qf.audioFrames;
            {
                std::lock_guard<std::mutex> lock(g_mtx);
                if (g_audioQueuedCount > 0) --g_audioQueuedCount;
            }
        }
    }

    /* On fast-stop (live), drop MF's internal pending samples so Finalize
    ** doesn't sit on a slow encoder backlog. */
    if (g_abandonQueue.load()) {
        writer->Flush(MF_SINK_WRITER_ALL_STREAMS);
    }
    // 5) Finalize.  Always called, even with frameCount == 0; without
    //    it the SinkWriter destructor hangs on dangling work.
    writer->Finalize();
    writer.Reset();
    if (coOwned) CoUninitialize();
}

// --- Public producer-side helpers (emu / main thread) ------------------------

// Backpressure: block the producer when the worker queue is full so
// the emu thread freezes atomically and AV sync is preserved.

// Push a video frame to the worker queue. Caller-owned BGRA32 bytes are copied.
// dropOnFull: true (live) skips the push on full -> freeze frame in MP4;
// false (offline) blocks so the replay clock paces the encoder.
// time100ns: wall-clock PTS, or -1 for sequential numbering (offline).
static void recorderAddFrame(const void* buffer, int length, bool dropOnFull, LONGLONG time100ns)
{
    if (!g_recActive.load()) return;

    int slotIdx;
    {
        std::unique_lock<std::mutex> lock(g_mtx);
        if (g_videoFreeSlots.empty()) {
            if (dropOnFull) return;
            g_qSpace.wait(lock, [] {
                return !g_videoFreeSlots.empty() || !g_recActive.load();
            });
            if (!g_recActive.load() || g_videoFreeSlots.empty()) return;
        }
        slotIdx = g_videoFreeSlots.front();
        g_videoFreeSlots.pop_front();
    }

    /* Copy outside the lock; the slot is owned exclusively by this call. */
    if ((size_t)length > g_videoPool[slotIdx].size()) {
        /* Shouldn't happen if start sized the pool correctly. */
        std::lock_guard<std::mutex> lock(g_mtx);
        g_videoFreeSlots.push_back(slotIdx);
        return;
    }
    memcpy(g_videoPool[slotIdx].data(), buffer, (size_t)length);

    QueuedFrame qf;
    qf.kind         = FrameKind::Video;
    qf.videoSlotIdx = slotIdx;
    qf.videoSize    = (size_t)length;
    qf.audioFrames  = 0;
    qf.time100ns    = time100ns;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (!g_recActive.load()) {
            g_videoFreeSlots.push_back(slotIdx);
            return;
        }
        g_q.push(std::move(qf));
    }
    g_cv.notify_one();
}

// audioFrames = stereo frames (NOT individual Int16 samples).
// dropOnFull true: drop the chunk on full so the audio mixer doesn't
// stall the emu; the worker uses sequential sample counts so AV stays
// in sync.
static void recorderAddSound(const Int16* samples, int audioFrames,
                             bool dropOnFull, LONGLONG time100ns)
{
    if (!g_recActive.load()) return;
    if (audioFrames <= 0) return;
    int byteLen = audioFrames * (int)AUDIO_BLOCK_ALIGN;

    QueuedFrame qf;
    qf.kind         = FrameKind::Audio;
    qf.videoSlotIdx = -1;
    qf.videoSize    = 0;
    qf.audioData.assign((const uint8_t*)samples, (const uint8_t*)samples + byteLen);
    qf.audioFrames  = audioFrames;
    qf.time100ns    = time100ns;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (!g_recActive.load()) return;
        if (dropOnFull && g_audioQueuedCount >= AUDIO_QUEUE_MAX) return;
        g_q.push(std::move(qf));
        ++g_audioQueuedCount;
    }
    g_cv.notify_one();
}

// Pool allocation matched to the recording dimensions; reused across frames
// so the producer never alloc/frees a multi-MB buffer at runtime.
static void recorderVideoPoolInit(int width, int height)
{
    std::lock_guard<std::mutex> lock(g_mtx);
    g_videoPool.assign((size_t)VIDEO_POOL_SLOTS,
                       std::vector<uint8_t>((size_t)width * (size_t)height * 4u, 0));
    g_videoFreeSlots.clear();
    for (int i = 0; i < VIDEO_POOL_SLOTS; i++) g_videoFreeSlots.push_back(i);
}

static void recorderVideoPoolTeardown(void)
{
    std::lock_guard<std::mutex> lock(g_mtx);
    g_videoPool.clear();
    g_videoPool.shrink_to_fit();
    g_videoFreeSlots.clear();
}

// Spawns the worker thread and blocks until init completes (success or fail).
// Returns 1 on success, 0 on failure (with error MessageBox already shown).
static int startWorker(const char* filename, int fps, int width, int height, bool hdr, int codec)
{
    // Ensure MF runtime is up; idempotent across recordings.
    ensureMFStartupOnce();
    // Register the worker-detach atexit handler AFTER MFShutdown is registered,
    // so atexit's reverse-order semantics run our detach BEFORE MFShutdown.
    registerWorkerAtexitOnce();

    {
        std::lock_guard<std::mutex> lock(g_mtx);
        while (!g_q.empty()) g_q.pop();
        g_audioQueuedCount = 0;
    }
    recorderVideoPoolInit(width, height);
    g_recActive    = false;
    g_stopRequested= false;
    g_abandonQueue = false;
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
        init.hdr      = hdr;
        /* HDR forces HEVC; otherwise honour the user's videoCodec choice. */
        init.codec    = hdr ? CAP_VIDEO_HEVC : codec;
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
        recorderVideoPoolTeardown();
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
        recorderVideoPoolTeardown();
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
    g_qSpace.notify_all();   // unblock any producer waiting for queue space
    g_worker.join();
    recorderVideoPoolTeardown();
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
    // Offline render: replay-paced producer, sequential PTS, block-on-full.
    recorderAddSound(buffer, (int)count, /*dropOnFull=*/false, /*time100ns=*/-1);
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

    // mixFrames=0: skip inter-frame blend so the source is frame-accurate
    // and isolated from any main-thread live render racing on mixFrame's
    // static dst buffer.
    if (g_postRender) {
        FrameBuffer* fb = frameBufferFlipViewFrame(0);
        if (fb == NULL) fb = frameBufferGetWhiteNoiseFrame();
        if (D3D12RecordCaptureFromFrame(fb, g_video, &g_properties->video.d3d,
                                        g_postRenderBuf.data(), displayPitch)) {
            recorderAddFrame(g_postRenderBuf.data(), width * height * 4,
                             /*dropOnFull=*/false, /*time100ns=*/-1);
            if (g_recHdrActive) {
                /* HDR preview: blit g12_recRT to dialog's HDR swap chain
                ** (decimated to ~6 fps for GPU/CPU cost). */
                if ((g_previewSnapCnt++ % PREVIEW_DECIMATE) == 0) {
                    D3D12HdrPreviewBlit();
                }
            } else {
                recorderSnapshotPreview(g_postRenderBuf.data(), width, height);
            }
        }
        /* Polling here per frame bounds replay overrun to one frame
        ** (cap.timer alone reschedules at ~50 emu-sec resolution). */
        boardCaptureCheckFinish();
        return;
    }

    int bitDepth      = 32;
    int bytesPerPixel = bitDepth / 8;
    char* dpyData     = displayData;

    FrameBuffer* frameBuffer = frameBufferFlipViewFrame(0);
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

    recorderAddFrame(displayData, width * height * 4,
                     /*dropOnFull=*/false, /*time100ns=*/-1);
    recorderSnapshotPreview(displayData, width, height);
}

// --- File dialog -------------------------------------------------------------
static void replaceCharInString(char* str, char oldChar, char newChar)
{
    while (*str) { if (*str == oldChar) *str = newChar; str++; }
}

// Replace .cap (case-insensitive) at the end of `capPath` with .mp4, or
// append .mp4 when the source path has no .cap extension. Writes into
// `mp4Buf` (size mp4Sz) and leaves it empty on overflow.
static void recorderDeriveMp4FromCap(const char* capPath,
                                     char* mp4Buf, size_t mp4Sz)
{
    if (!capPath || mp4Sz == 0) return;
    size_t len = strlen(capPath);
    if (len >= mp4Sz) { mp4Buf[0] = 0; return; }
    memcpy(mp4Buf, capPath, len + 1);
    char* dot = strrchr(mp4Buf, '.');
    if (dot && _stricmp(dot, ".cap") == 0) {
        memcpy(dot, ".mp4", 4);
    } else if (len + 4 < mp4Sz) {
        strcat(mp4Buf, ".mp4");
    } else {
        mp4Buf[0] = 0;
    }
}

// Custom file-picker dialog state. capBuf / mp4Buf are filled in by the
// caller (initial values) and written back on IDOK.
struct RecorderPickFiles {
    char*  capBuf;
    char*  mp4Buf;
    size_t capSz;
    size_t mp4Sz;
};

static BOOL_DLG_RET CALLBACK pickFilesDlgProc(HWND hDlg, UINT iMsg,
                                      WPARAM wParam, LPARAM lParam)
{
    static RecorderPickFiles* pfd;
    switch (iMsg) {
    case WM_INITDIALOG:
        pfd = (RecorderPickFiles*)lParam;
        // Localize labels and the title; the .rc keeps English defaults so
        // resource compilation works without lang headers, but at runtime
        // we always overwrite via lang* lookups.
        SetWindowTextU(hDlg, langDlgRecorderPickTitle());
        SetDlgItemTextU(hDlg, IDC_RECORDER_CAPLABEL, langDlgRecorderPickSourceCap());
        SetDlgItemTextU(hDlg, IDC_RECORDER_MP4LABEL, langDlgRecorderPickOutputMp4());
        SetDlgItemTextU(hDlg, IDCANCEL,              langDlgCancel());
        SetDlgItemTextU(hDlg, IDC_RECORDER_CAPPATH,  pfd->capBuf);
        SetDlgItemTextU(hDlg, IDC_RECORDER_MP4PATH,  pfd->mp4Buf);
        win32CommonApplyDark(hDlg);
        win32CommonCenterOnOwner(hDlg);
        return TRUE;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_RECORDER_CAPBROWSE) {
            char  defaultDir[512] = "";
            const char* vdir = actionGetVideoCaptureDir();
            if (vdir && vdir[0]) {
                strncpy(defaultDir, vdir, sizeof(defaultDir) - 1);
                defaultDir[sizeof(defaultDir) - 1] = 0;
            }
            char extList[256];
            sprintf(extList, "%s   (*.cap)#*.cap#", langFileVideoCapture());
            replaceCharInString(extList, '#', 0);
            char* picked = archFileOpen(langDlgLoadVideoCapture(), extList,
                                         defaultDir, (char*)".cap\0",
                                         NULL, NULL, -1);
            if (picked && picked[0]) {
                SetDlgItemTextU(hDlg, IDC_RECORDER_CAPPATH, picked);
                // Auto-fill / refresh the mp4 field to match.
                char derived[1024];
                recorderDeriveMp4FromCap(picked, derived, sizeof(derived));
                if (derived[0]) {
                    SetDlgItemTextU(hDlg, IDC_RECORDER_MP4PATH, derived);
                }
            }
            return TRUE;
        }
        if (id == IDC_RECORDER_MP4BROWSE) {
            char  defaultDir[512] = "";
            const char* vdir = actionGetVideoCaptureDir();
            if (vdir && vdir[0]) {
                strncpy(defaultDir, vdir, sizeof(defaultDir) - 1);
                defaultDir[sizeof(defaultDir) - 1] = 0;
            }
            char extList[256];
            sprintf(extList, "Video file   (*.mp4)#*.mp4#");
            replaceCharInString(extList, '#', 0);
            int selExt = 0;
            char* picked = archFileSave(langDlgSaveVideoClipAs(), extList,
                                         defaultDir, (char*)".mp4\0",
                                         &selExt, (char*)".mp4");
            if (picked && picked[0]) {
                SetDlgItemTextU(hDlg, IDC_RECORDER_MP4PATH, picked);
            }
            return TRUE;
        }
        if (id == IDOK) {
            char cap[1024], mp4[1024];
            GetDlgItemTextU(hDlg, IDC_RECORDER_CAPPATH, cap, sizeof(cap));
            GetDlgItemTextU(hDlg, IDC_RECORDER_MP4PATH, mp4, sizeof(mp4));
            if (cap[0] == 0 || !archFileExists(cap)) {
                char msg[1280];
                _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                            langErrorRecorderReplayMissing(), cap);
                MessageBoxU(hDlg, msg, langErrorRecorderTitle(),
                            MB_ICONWARNING | MB_OK);
                return TRUE;
            }
            if (mp4[0] == 0) return TRUE;  // empty output -- ignore OK
            if (archFileExists(mp4)) {
                char msg[1280];
                _snprintf_s(msg, sizeof(msg), _TRUNCATE, "%s\n  %s",
                            langWarningOverwriteFile(), mp4);
                if (IDOK != MessageBoxU(hDlg, msg, langWarningTitle(),
                                        MB_OKCANCEL | MB_ICONWARNING)) {
                    return TRUE;
                }
            }
            strncpy(pfd->capBuf, cap, pfd->capSz - 1);
            pfd->capBuf[pfd->capSz - 1] = 0;
            strncpy(pfd->mp4Buf, mp4, pfd->mp4Sz - 1);
            pfd->mp4Buf[pfd->mp4Sz - 1] = 0;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// Run the file-picker dialog. capBuf / mp4Buf hold initial values (typically
// empty or the last-used .cap and a derived .mp4) and receive the user's
// final choices on success. Returns the .mp4 path on success, NULL on cancel.
static char* recorderPickFiles(Properties* properties)
{
    static char capBuf[1024];
    static char mp4Buf[1024];

    capBuf[0] = 0;
    mp4Buf[0] = 0;
    if (properties->filehistory.videocap[0]) {
        strncpy(capBuf, properties->filehistory.videocap, sizeof(capBuf) - 1);
        capBuf[sizeof(capBuf) - 1] = 0;
        recorderDeriveMp4FromCap(capBuf, mp4Buf, sizeof(mp4Buf));
    }

    RecorderPickFiles pfd;
    pfd.capBuf = capBuf;
    pfd.mp4Buf = mp4Buf;
    pfd.capSz  = sizeof(capBuf);
    pfd.mp4Sz  = sizeof(mp4Buf);

    INT_PTR rv = DialogBoxParam(GetModuleHandle(NULL),
                                MAKEINTRESOURCE(IDD_RECORDER_PICKFILES),
                                g_hwnd, (DLGPROC)pickFilesDlgProc,
                                (LPARAM)&pfd);
    if (rv != IDOK) return NULL;

    // Hand the chosen replay to Actions / Board via the shared Properties
    // history slot, the same way Load Replay does.
    strncpy(properties->filehistory.videocap, capBuf,
            sizeof(properties->filehistory.videocap) - 1);
    properties->filehistory.videocap[sizeof(properties->filehistory.videocap) - 1] = 0;

    return mp4Buf;
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

// Preview area sits at the top of the (enlarged) client rect; existing
// controls from the .rc template were shifted down at WM_INITDIALOG to make
// room. Computed at paint time so DPI changes / theme refreshes can't
// desync the rect from the actual layout.
static RECT statusDlgPreviewRect(HWND hDlg)
{
    RECT cr;
    GetClientRect(hDlg, &cr);
    int w = PREVIEW_W;
    int h = PREVIEW_H;
    int x = cr.left + (cr.right - cr.left - w) / 2;
    int y = cr.top + PREVIEW_PAD;
    if (x < cr.left) x = cr.left;
    if (y < cr.top)  y = cr.top;
    RECT r = { x, y, x + w, y + h };
    return r;
}

static void statusDlgPaintPreview(HWND hDlg, HDC hdc)
{
    RECT pr = statusDlgPreviewRect(hDlg);

    // Consumer state. localBuf is the seqlock retry workspace; stableBuf is
    // the most recent successfully-snapshotted frame. We always paint from
    // stableBuf, only updating it when a seqlock read confirms a stable
    // window. That way an unlucky paint that lands inside the producer's
    // ~1 ms memcpy redraws the previous frame instead of flashing to black.
    // Both buffers live across paints (main thread only).
    static std::vector<uint8_t> localBuf;
    static std::vector<uint8_t> stableBuf;
    static int stableW = 0, stableH = 0;

    int w = 0, h = 0;
    bool got = false;
    for (int retry = 0; retry < 16; retry++) {
        uint64_t v1 = g_previewSeq.load(std::memory_order_acquire);
        if (v1 & 1) continue;  // producer mid-write
        if (v1 == 0) break;    // no frame published yet

        w = g_previewW;
        h = g_previewH;
        if (w <= 0 || h <= 0) break;

        size_t bytes = (size_t)w * (size_t)h * 4;
        if (bytes > g_previewBufBytes) break;
        if (localBuf.size() < bytes) localBuf.resize(bytes);
        memcpy(localBuf.data(), g_previewBuf.data(), bytes);

        uint64_t v2 = g_previewSeq.load(std::memory_order_acquire);
        if (v1 == v2) { got = true; break; }
    }

    if (got) {
        // Promote the just-read frame to stable. std::swap is a constant-
        // time pointer swap, so this is essentially free.
        std::swap(stableBuf, localBuf);
        stableW = w;
        stableH = h;
    }

    if (stableW <= 0 || stableH <= 0 || stableBuf.empty()) {
        // Never had a successful read yet -- fall back to black.
        FillRect(hdc, &pr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = stableW;
    bmi.bmiHeader.biHeight      = stableH;     // positive = bottom-up; matches our buffer
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchDIBits(hdc,
                  pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top,
                  0, 0, stableW, stableH,
                  stableBuf.data(), &bmi,
                  DIB_RGB_COLORS, SRCCOPY);
}

static BOOL_DLG_RET CALLBACK statusDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (iMsg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    case WM_CLOSE:
        return TRUE;
    case WM_TIMER:
        SetDlgItemTextU(hDlg, IDC_VIDEOPROGRESSTEXT, progressText());
        // Distinguish natural completion (IDOK) from user cancel so the
        // caller can decide whether to surface the "saved successfully"
        // toast.
        if (!boardCaptureIsPlaying()) EndDialog(hDlg, IDOK);
        return FALSE;
    case WM_RECORDER_FRAME_READY: {
        RECT pr = statusDlgPreviewRect(hDlg);
        InvalidateRect(hDlg, &pr, FALSE);
        return TRUE;
    }
    case WM_ERASEBKGND: {
        // Default WM_ERASEBKGND fills the entire client area with the
        // dialog brush -- in dark mode that's solid black -- before
        // WM_PAINT runs, which makes the preview rect briefly flash
        // black between invalidations triggered by focus changes /
        // modal pump events. Erase only the strips around the preview
        // and leave the preview rect alone so its previous content
        // stays on screen until WM_PAINT redraws it.
        HDC hdc = (HDC)wParam;
        RECT cr;
        GetClientRect(hDlg, &cr);
        RECT pr = statusDlgPreviewRect(hDlg);

        HBRUSH bg = win32CommonIsDarkMode() ? win32CommonDarkBgBrush()
                                            : (HBRUSH)(COLOR_BTNFACE + 1);
        if (!bg) bg = (HBRUSH)(COLOR_BTNFACE + 1);

        RECT strips[4] = {
            { cr.left,  cr.top,    cr.right, pr.top    }, // above
            { cr.left,  pr.bottom, cr.right, cr.bottom }, // below
            { cr.left,  pr.top,    pr.left,  pr.bottom }, // left
            { pr.right, pr.top,    cr.right, pr.bottom }  // right
        };
        for (int i = 0; i < 4; i++) {
            if (strips[i].right > strips[i].left
                && strips[i].bottom > strips[i].top) {
                FillRect(hdc, &strips[i], bg);
            }
        }
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hDlg, &ps);
        /* HDR preview is owned by the child HWND's D3D12 swap chain.
        ** Skip the GDI paint there to avoid clobbering the HDR pixels. */
        if (!g_previewHdrChild) {
            statusDlgPaintPreview(hDlg, hdc);
        }
        EndPaint(hDlg, &ps);
        return TRUE;
    }
    case WM_DESTROY:
        if (g_previewHdrChild) {
            D3D12HdrPreviewEnd();
            DestroyWindow(g_previewHdrChild);
            g_previewHdrChild = NULL;
        }
        g_previewDlg = NULL;
        KillTimer(hDlg, 2);
        return 0;
    case WM_INITDIALOG:
        SetWindowTextU(hDlg, langDlgRenderVideoCapture());
        SetDlgItemTextU(hDlg, IDCANCEL, langDlgCancel());
        /* Hardcode "0.0%": WM_INITDIALOG fires after the emu started so
        ** progressText() would already show mid-recording or overshoot. */
        {
            char initText[128];
            sprintf(initText, "%s 0.0%%", langDlgAmountCompleted());
            SetDlgItemTextU(hDlg, IDC_VIDEOPROGRESSTEXT, initText);
        }

        // Lay the dialog out from scratch:
        //
        //   [ PAD ]
        //   [ preview (PREVIEW_W x PREVIEW_H) ]
        //   [ PAD ]
        //   [ progress text on the left | Cancel on the right ]
        //   [ PAD ]
        //
        // The .rc template put IDC_VIDEOPROGRESSTEXT and IDCANCEL on
        // overlapping x ranges (text 20..180, button 127..192 dlu) at
        // different y rows, so we ignore the template positions and
        // place both controls explicitly on a single row with non-
        // overlapping x ranges. The text and button keep their .rc
        // sizes (height matters; width matters for the static text so
        // the percent value isn't truncated).
        {
            HWND hText   = GetDlgItem(hDlg, IDC_VIDEOPROGRESSTEXT);
            HWND hCancel = GetDlgItem(hDlg, IDCANCEL);
            RECT rText = {}, rCancel = {};
            int textW = 0, textH = 0, cancelW = 0, cancelH = 0;
            if (hText)   { GetWindowRect(hText,   &rText);
                           textW   = rText.right   - rText.left;
                           textH   = rText.bottom  - rText.top; }
            if (hCancel) { GetWindowRect(hCancel, &rCancel);
                           cancelW = rCancel.right  - rCancel.left;
                           cancelH = rCancel.bottom - rCancel.top; }
            int rowH = (textH > cancelH) ? textH : cancelH;
            if (rowH <= 0) rowH = 20;  // sanity fallback

            int wantCliW = PREVIEW_W + PREVIEW_PAD * 2;
            int wantCliH = PREVIEW_PAD + PREVIEW_H + PREVIEW_PAD + rowH + PREVIEW_PAD;

            // Resize the dialog (compute non-client overhead from current).
            RECT wr, cr;
            GetWindowRect(hDlg, &wr);
            GetClientRect(hDlg, &cr);
            int ncW = (wr.right - wr.left)   - (cr.right - cr.left);
            int ncH = (wr.bottom - wr.top)   - (cr.bottom - cr.top);
            int newW = wantCliW + ncW;
            int newH = wantCliH + ncH;
            SetWindowPos(hDlg, NULL, 0, 0, newW, newH,
                         SWP_NOMOVE | SWP_NOZORDER);

            // Recenter the (now much larger) dialog over its owner window.
            HWND owner = GetWindow(hDlg, GW_OWNER);
            RECT ow;
            if (owner && GetWindowRect(owner, &ow)) {
                int x = ow.left + ((ow.right - ow.left) - newW) / 2;
                int y = ow.top  + ((ow.bottom - ow.top) - newH) / 2;
                SetWindowPos(hDlg, NULL, x, y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            }

            // Place text on the left, Cancel on the right of the bottom row.
            // Small inner pads (~8 px) keep text/button visually inside the
            // preview's own left/right edges. The text element is taller than
            // the visible glyph (LTEXT is top-aligned by default), so without
            // a downward nudge the rendered text floats near the top of its
            // rect and looks closer to the preview than to the dialog bottom.
            // ROW_TEXT_NUDGE_DN moves the element down so the visible glyph
            // sits roughly midway between preview-bottom and dialog-bottom.
            const int ROW_INNER_PAD     = 8;
            const int ROW_TEXT_NUDGE_DN = 12;
            int rowTop = PREVIEW_PAD + PREVIEW_H + PREVIEW_PAD;
            if (hText) {
                int textX = PREVIEW_PAD + ROW_INNER_PAD;
                int textY = rowTop + (rowH - textH) / 2 + ROW_TEXT_NUDGE_DN;
                SetWindowPos(hText, NULL, textX, textY, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            }
            if (hCancel) {
                int cancelX = wantCliW - PREVIEW_PAD - ROW_INNER_PAD - cancelW;
                int cancelY = rowTop + (rowH - cancelH) / 2;
                SetWindowPos(hCancel, NULL, cancelX, cancelY, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            }
        }

        SetTimer(hDlg, 2, 250, NULL);
        win32CommonApplyDark(hDlg);
        g_previewDlg = hDlg;

        /* HDR offline-render: GDI can't display HDR, so attach a D3D12 PQ
        ** swap chain to a child HWND. SDR fallback on failure. */
        if (g_recHdrActive) {
            RECT pr = statusDlgPreviewRect(hDlg);
            int  cw = pr.right  - pr.left;
            int  ch = pr.bottom - pr.top;
            g_previewHdrChild = CreateWindowExA(
                0, "STATIC", "",
                WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                pr.left, pr.top, cw, ch,
                hDlg, (HMENU)0, GetModuleHandle(NULL), NULL);
            if (g_previewHdrChild) {
                /* Swap chain at recording resolution -- DXGI_SCALING_STRETCH
                ** scales it to the child HWND on Present. */
                int recW = 320 * g_zoom;
                int recH = 240 * g_zoom;
                if (!D3D12HdrPreviewBegin(g_previewHdrChild, recW, recH)) {
                    DestroyWindow(g_previewHdrChild);
                    g_previewHdrChild = NULL;
                }
            }
        }
        return FALSE;
    }
    return FALSE;
}

// Recording goes through the DX12 post-render readback to carry shader
// effects (PAL / scanline / monitor color) into the saved video. Other
// drivers can't run that path. If the user is on DD / GDI we ask whether
// to switch to DX12 and continue; YES applies the change via the same
// archUpdateWindow that any Properties dialog change would, NO cancels.
// Returns 1 when DX12 is active and the caller should proceed, 0 to
// abort the recording start.
static int recorderEnsureDX12Driver(HWND owner, Properties* prop)
{
    if (prop->video.driver == P_VIDEO_DRVDIRECTX_D3D12) return 1;

    int rv = MessageBoxU(owner, langErrorRecorderRequiresDX12(),
                         langErrorRecorderRequiresDX12Title(),
                         MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (rv != IDYES) return 0;

    prop->video.driver = P_VIDEO_DRVDIRECTX_D3D12;
    // archUpdateWindow synchronously brings the DX12 device up via
    // D3D12EnsureReady before returning, so the recorder's subsequent
    // D3D12RecordBegin (and the offline render's CFF) sees g12_ready=true
    // immediately -- the recording proceeds in the same call rather than
    // silently failing and forcing the user to invoke the menu again.
    archUpdateWindow();
    return prop->video.driver == P_VIDEO_DRVDIRECTX_D3D12;
}

// --- Public lifecycle (replaces aviStartRender / aviStopRender) --------------
extern "C" void recorderStartRender(HWND hwndOwner, Properties* prop, Video* vid)
{
    // Offline render and live recording share the MF worker, post-render
    // buffer, and DX12 capture resources -- stop live first to avoid a crash.
    if (g_liveActive.load(std::memory_order_acquire)) {
        recorderStopLive();
    }

    if (!recorderEnsureDX12Driver(hwndOwner, prop)) return;

    g_hwnd       = hwndOwner;
    g_properties = prop;
    g_video      = vid;
    g_syncMethod = prop->emulation.syncMethod;
    g_emuSpeed   = prop->emulation.speed;

    // Post-render at x4 zoom (1280x960) -- scanline / PAL / monitor effects
    // need that pixel density to survive the encode without aliasing.
    g_postRender = 1;
    g_previewSnapCnt = 0;
    g_previewSeq.store(0, std::memory_order_release);
    g_previewW = 0;
    g_previewH = 0;
    g_zoom     = 4;
    g_fps      = prop->video.captureFps;

    // Pick the source replay (.cap) here and derive the matching .mp4
    // path next to it.  Cancelling at any step bails out before any emu
    // state is touched so the running session isn't stopped for nothing.
    char* filename = recorderPickFiles(prop);
    if (filename == NULL) return;

    actionEmuStop();

    int width  = 320 * g_zoom;
    int height = 240 * g_zoom;

    boardSetPeriodicCallback(recorderVideoCallback, NULL, prop->video.captureFps);
    prop->emulation.syncMethod = P_EMU_SYNCIGNORE;
    mixerSetBoardFrequencyFixed(3579545);
    actionEmuSpeedSet(100);
    frameBufferSetFrameCount(4);

    soundDriverConfig(g_hwnd, SOUND_DRV_AVI);
    emulatorRestartSound();

    /* Offline render honours recordHdr/hdrEnable; capture RT is independent
    ** of the live swap chain so HDR mode is picked per-capture via cb.hdrMode. */
    int recHdr = (prop->video.recordHdr && prop->video.hdrEnable) ? 1 : 0;
    g_recHdrActive = recHdr;
    if (!startWorker(filename, prop->video.captureFps, width, height, recHdr != 0,
                     prop->capture.videoCodec)) {
        // Worker failed to init; restore emu state and bail.
        boardSetPeriodicCallback(NULL, NULL, 0);
        prop->emulation.syncMethod = g_syncMethod;
        soundDriverConfig(g_hwnd, (SoundDriver)prop->sound.driver);
        emulatorRestartSound();
        return;
    }

    // Allocate the readback buffer and bring the DX12 capture resources up
    // at the chosen recording resolution. recorderEnsureDX12Driver above
    // guarantees the driver is DX12; failure here is a hard system error
    // (out of GPU memory, etc.) so abort cleanly instead of silently
    // dropping to a sub-quality path.
    g_postRenderBuf.assign((size_t)width * (size_t)height * 4, 0);
    if (!D3D12RecordBegin(width, height, recHdr)) {
        g_postRenderBuf.clear();
        g_postRenderBuf.shrink_to_fit();
        stopWorker();
        boardSetPeriodicCallback(NULL, NULL, 0);
        prop->emulation.syncMethod = g_syncMethod;
        soundDriverConfig(g_hwnd, (SoundDriver)prop->sound.driver);
        emulatorRestartSound();
        return;
    }

    // Pre-size the producer-side preview buffer once. Allocating at start
    // (rather than per-frame inside recorderSnapshotPreview) keeps the
    // seqlock writer path free of any reallocation: g_previewBuf.data()
    // stays stable for the entire session, so the consumer's seqlock read
    // never sees a moving pointer.
    g_previewBufBytes = (size_t)width * (size_t)height * 4;
    g_previewBuf.assign(g_previewBufBytes, 0);

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

    INT_PTR dlgRv = DialogBox(GetModuleHandle(NULL),
                              MAKEINTRESOURCE(IDD_RENDERVIDEO),
                              g_hwnd, (DLGPROC)statusDlgProc);

    actionEmuStop();
    recorderStopRender();

    // Surface a "video saved" toast on natural completion. IDCANCEL means
    // the user pressed the Cancel button mid-render, in which case the
    // partial MP4 is still on disk but probably not what they want -- skip
    // the toast there.
    if (dlgRv == IDOK && filename) {
        char msg[1280];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                    langInfoRecorderComplete(), filename);
        MessageBoxU(hwndOwner, msg, langErrorRecorderTitle(),
                    MB_OK | MB_ICONINFORMATION);
    }
}

extern "C" void recorderStopRender(void)
{
    if (!g_rendering) return;
    g_rendering = 0;

    if (g_postRender) {
        D3D12RecordEnd();
        g_postRender = 0;
        g_postRenderBuf.clear();
        g_postRenderBuf.shrink_to_fit();
    }

    // Tear down the seqlock preview buffer. Bumping the seq to 1 (odd)
    // would force any concurrent paint to retry; we instead rely on the
    // dialog being gone (g_previewDlg cleared in WM_DESTROY) so no
    // consumer can be inspecting g_previewBuf at this point.
    g_previewBuf.clear();
    g_previewBuf.shrink_to_fit();
    g_previewBufBytes = 0;
    g_previewW = 0;
    g_previewH = 0;
    g_previewSeq.store(0, std::memory_order_release);

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

// --- Live recording lifecycle -----------------------------------------------
//
// Live recording reuses the offline-render capture path: a board periodic
// callback fires at the chosen FPS on the emu thread, frameBufferFlipViewFrame
// hands us the current MSX frame, and D3D12RecordCaptureFromFrame runs the
// regular display PSO into a capture-owned 1280x960 RT (independent of the
// live render's swap chain) and reads back BGRA32. That gives us the same
// shader fidelity (PAL / scanline / monitor color) at a fixed encoder-
// friendly resolution, and runs entirely on the emu thread so it doesn't
// fight the main thread's live render for GPU. Audio comes through the
// Mixer tap so the user keeps hearing playback while the master mix is
// also routed to the AAC encoder.

// Audio tap: the global Mixer hands us each fragment after the playback
// driver has consumed it. Live mode drops on full so a stalled encoder
// doesn't block the DirectSound mixer thread (the latter would starve
// playback and visibly slow the emulator). PTS uses sequential sample
// counts, which is naturally suspend-aware: the mixer stops generating
// chunks while the emu is suspended (archSoundSuspend), so the audio
// timeline pauses in lock-step with video without producing a silence gap.
static Int32 recorderLiveAudioTap(void* dummy, Int16* buffer, UInt32 count)
{
    (void)dummy;
    if (!g_liveActive.load(std::memory_order_acquire)) return 0;
    int frames = (int)(count / 2);
    if (frames > 0) {
        recorderAddSound(buffer, frames, /*dropOnFull=*/true, /*time100ns=*/-1);
    }
    return 0;
}

// Emu-time based PTS for live recording. Driven by boardSystemTime which
// stops advancing while the emulator is suspended (menu / window move /
// dialog -- see Win32 WM_ENTERSIZEMOVE / WM_ENTERMENULOOP handlers calling
// emulatorSuspend()). Using emu-time PTS makes suspends transparent in the
// MP4: the recording timeline simply pauses with the emu and resumes
// seamlessly, with no wall-clock-induced freeze frame at each menu open.
//
// boardSystemTime() is a 32-bit free-running cycle counter at
// boardFrequency() = 6 * 3579545 = 21477270 Hz, so it wraps every
// (2^32 - 1) / 21477270 ~= 200 emu sec (3:20). Accumulating UInt32
// deltas into a UInt64 absorbs the wrap and gives a monotonic PTS for
// arbitrary-length recordings -- same pattern as boardSystemTime64() in
// Board.c, and the same lesson applied to .cap replay progress in
// commit e540bb8d.
static UInt32 g_liveEmuPrev       = 0;
static UInt64 g_liveCyclesAccum   = 0;
static int    g_liveEmuOriginSet  = 0;

static LONGLONG liveEmuTime100ns(UInt32 emuTime)
{
    if (!g_liveEmuOriginSet) {
        g_liveEmuPrev      = emuTime;
        g_liveCyclesAccum  = 0;
        g_liveEmuOriginSet = 1;
        return 0;
    }
    /* Hard reset restarts boardSystemTime near 0; bridge any >0.5 sec
    ** delta with one frame so the video timeline keeps advancing. */
    UInt32 freq  = boardFrequency();
    UInt32 delta = (UInt32)(emuTime - g_liveEmuPrev);
    if (freq != 0 && delta > freq / 2) {
        delta = (g_fps > 0) ? (freq / (UInt32)g_fps) : 0;
    }
    g_liveCyclesAccum += delta;
    g_liveEmuPrev      = emuTime;

    if (freq == 0) return 0;
    /* 100ns ticks = cycles * 10^7 / freq; split secs/sub-secs to avoid
    ** UInt64 overflow on long recordings. */
    UInt64 secs    = g_liveCyclesAccum / freq;
    UInt64 subSecs = g_liveCyclesAccum % freq;
    return (LONGLONG)(secs * 10000000ULL + subSecs * 10000000ULL / (UInt64)freq);
}

// Periodic video callback. Same body as recorderVideoCallback's post-render
// branch -- offline and live differ in their lifecycle (replay vs. live emu)
// but converge on the capture path. Drives video frame capture via the board
// periodic timer at emu-time 60Hz; PTS is derived from emu-time so menu /
// window-move suspends produce no freeze frame in the recorded MP4.
static void recorderLiveVideoCallback(void* dummy, UInt32 time)
{
    (void)dummy;
    if (!g_liveActive.load(std::memory_order_acquire)) return;

    int width  = g_liveW;
    int height = g_liveH;
    int displayPitch = width * 4;

    FrameBuffer* fb = frameBufferFlipViewFrame(0);
    if (fb == NULL) fb = frameBufferGetWhiteNoiseFrame();
    if (D3D12RecordCaptureFromFrame(fb, g_video, &g_properties->video.d3d,
                                    g_postRenderBuf.data(), displayPitch)) {
        recorderAddFrame(g_postRenderBuf.data(), width * height * 4,
                         /*dropOnFull=*/true, liveEmuTime100ns(time));
    }
}

extern "C" int recorderStartLive(HWND hwndOwner, Properties* prop, Video* vid,
                                  const char* overrideFilename)
{
    if (g_liveActive.load(std::memory_order_acquire)) return 0;
    if (g_stoppingLive.load(std::memory_order_acquire)) return 0;  // teardown in progress
    if (g_rendering) return 0;          // offline render in progress

    // Live recording captures through the DX12 post-render path. Offer to
    // switch the driver if the user is on DD/GDI; cancel the start otherwise.
    if (!recorderEnsureDX12Driver(hwndOwner, prop)) return 0;

    g_hwnd       = hwndOwner;
    g_properties = prop;
    g_video      = vid;

    // Fixed dimensions matching offline: zoom 4 = 1280x960 at the native
    // MSX 4:3 aspect. Encoder-friendly and independent of window size, so
    // the encoder pipeline stays in step with realtime regardless of how
    // big the user's window is.
    g_postRender = 1;
    g_zoom       = 4;
    g_fps        = 60;
    int width  = 320 * g_zoom;
    int height = 240 * g_zoom;
    g_liveW = width;
    g_liveH = height;

    // Filename: Save-As override wins, otherwise auto-name under the
    // Video Capture directory.
    if (overrideFilename && overrideFilename[0]) {
        strncpy(g_liveFilename, overrideFilename, sizeof(g_liveFilename) - 1);
        g_liveFilename[sizeof(g_liveFilename) - 1] = 0;
    } else {
        /* Auto-name "<machine>_NN.mp4" (matching .wav / .cap convention). */
        const char* vdir = actionGetVideoCaptureDir();
        if (!vdir || !vdir[0]) vdir = ".";
        char* fn = generateSaveFilename(prop, (char*)vdir, (char*)"", (char*)".mp4", 2);
        if (!fn || !fn[0]) return 0;
        strncpy(g_liveFilename, fn, sizeof(g_liveFilename) - 1);
        g_liveFilename[sizeof(g_liveFilename) - 1] = 0;
    }

    // HDR recording requires live HDR to be active (the capture path reuses
    // the live HDR shader's PQ-encoding branch).  Falling back to SDR if the
    // user has only ticked recordHdr without enabling live HDR keeps the
    // recording valid (just SDR) instead of failing.
    int recHdr = (prop->video.recordHdr && prop->video.hdrEnable && D3D12IsHdrActive()) ? 1 : 0;

    // Pre-allocate the post-render readback buffer (1280x960 ~= 5 MB
    // regardless of HDR -- 4 bytes/pixel for both BGRA8 and R10G10B10A2).
    g_postRenderBuf.assign((size_t)width * (size_t)height * 4, 0);

    // Bring up the DX12 capture resources (capture-owned RT / source texture
    // / SRV heap / cmd allocator + fence) at the recording resolution.
    if (!D3D12RecordBegin(width, height, recHdr)) {
        g_postRenderBuf.clear();
        g_postRenderBuf.shrink_to_fit();
        return 0;
    }

    // Spawn the MF worker.
    if (!startWorker(g_liveFilename, g_fps, width, height, recHdr != 0,
                     prop->capture.videoCodec)) {
        D3D12RecordEnd();
        g_postRenderBuf.clear();
        g_postRenderBuf.shrink_to_fit();
        g_liveW = g_liveH = 0;
        return 0;
    }

    // Hook the global Mixer for audio + the emu board timer for video.
    // The board periodic timer is rearmed unconditionally (Board.c keeps
    // it alive even when no callback is installed) so a mid-emulation
    // install -- which is what live recording does -- takes effect on
    // the next tick.
    Mixer* mixer = mixerGetGlobalMixer();
    if (mixer) mixerSetTapCallback(mixer, recorderLiveAudioTap, NULL);
    g_liveEmuOriginSet = 0;       /* first video callback samples emu time as origin */
    boardSetPeriodicCallback(recorderLiveVideoCallback, NULL, g_fps);

    g_liveActive.store(1, std::memory_order_release);
    return 1;
}

extern "C" void recorderStopLive(void)
{
    if (!g_liveActive.load(std::memory_order_acquire)) return;
    g_liveActive.store(0, std::memory_order_release);
    /* Guard against a new recording racing the still-tearing-down session;
    ** checked by recorderStartLive / recorderIsLiveRecording. */
    g_stoppingLive.store(1, std::memory_order_release);

    // Unhook first so no more frames / audio enter the queue while we drain.
    boardSetPeriodicCallback(NULL, NULL, 0);
    Mixer* mixer = mixerGetGlobalMixer();
    if (mixer) mixerSetTapCallback(mixer, NULL, NULL);

    /* SinkWriter Finalize() can take a moment (HW encoder drain). Run the
    ** join on a side thread and pump messages so the UI stays responsive. */
    HCURSOR oldC = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (g_worker.joinable()) {
        g_recActive     = false;
        g_abandonQueue  = true;   /* fast stop: drop pending, flush MF */
        g_stopRequested = true;
        g_cv.notify_one();
        g_qSpace.notify_all();   // unblock any producer waiting for queue space

        HANDLE done = CreateEventW(NULL, TRUE, FALSE, NULL);
        std::thread joiner([done]() {
            if (g_worker.joinable()) g_worker.join();
            SetEvent(done);
        });

        for (;;) {
            DWORD wr = MsgWaitForMultipleObjects(1, &done, FALSE, INFINITE, QS_ALLEVENTS);
            if (wr == WAIT_OBJECT_0) break;
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    /* Re-post so the main loop sees it once we return. */
                    PostQuitMessage((int)msg.wParam);
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (joiner.joinable()) joiner.join();
        CloseHandle(done);
        recorderVideoPoolTeardown();
    }

    SetCursor(oldC);

    // Tear down DX12 capture resources.
    D3D12RecordEnd();
    g_postRender = 0;
    g_postRenderBuf.clear();
    g_postRenderBuf.shrink_to_fit();

    // Tell the user where the file landed. Toast (non-modal) when the user
    // has it enabled in Capture properties; otherwise stay silent so the
    // toggle hotkey UX isn't interrupted.
    if (g_properties && g_properties->capture.showCompletionToast) {
        /* Anchor on emu hwnd so toast lands over the video, not the theme. */
        toastShowSaved(g_hwnd, getEmuHwnd(), g_liveFilename);
    }

    g_liveW = g_liveH = 0;

    /* All teardown complete: clear the guard so a new recording can start. */
    g_stoppingLive.store(0, std::memory_order_release);
}

extern "C" int recorderIsLiveRecording(void)
{
    /* True while stopping is in flight too, so the "Already recording"
    ** toast + menu Stop/Record state stays consistent during teardown. */
    return g_liveActive.load(std::memory_order_acquire)
        || g_stoppingLive.load(std::memory_order_acquire);
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
