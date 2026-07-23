/*****************************************************************************
**
** WASAPI audio output backend.
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
// Shared-mode event-driven WASAPI client with AUTOCONVERTPCM; always
// submits 44100 Hz Int16 regardless of mix format.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <avrt.h>
#include <initguid.h>
#include <ksmedia.h>
#include <string.h>
#include <stdlib.h>

// Project C headers -- wrap in extern "C" so the compiler uses C linkage
// for functions defined in .c translation units.
extern "C" {
#include "MsxTypes.h"
#include "AudioMixer.h"
}
#include "Win32WasapiSound.h"

//  constants

// Ring buffer depth in frames (power of two for fast modulo).
// 8192 frames at 44100 Hz stereo ~ 186 ms -- generous headroom.
#define RING_FRAMES     8192

// WASAPI buffer floor: 1 ms (100-ns units); engine period is typically
// 3-10 ms shared mode, fall back to DirectSound if Initialize fails.
#define WASAPI_BUF_DURATION  10000LL

// Frames to crossfade back in after an underrun gap (about 6 ms).
#define FADE_FRAMES     256

// Backlog the ring must reach before output resumes after a gap
// (about 46 ms); prevents click flutter from a struggling producer.
#define PRIME_FRAMES    2048

// Last measured endpoint buffer size in ms (0 = WASAPI not active).
static UInt32 g_actualBufferMs = 0;

//  types

struct WasapiSound {
    Mixer*              mixer;
    IAudioClient*       audioClient;
    IAudioRenderClient* renderClient;
    UINT32              bufferFrames; // WASAPI buffer size in frames (our PCM format)
    UINT32              channels;     // 1 or 2

    HANDLE              hFeedEvent;
    HANDLE              hStopEvent;
    HANDLE              hThread;

    volatile BOOL       suspended;

    // click-free underrun concealment (render thread only)
    Int16               lastFrame[2];
    UINT32              fadeInFrames;
    UINT32              primeFrames;

    // Lock-free SPSC ring buffer -- interleaved Int16 samples.
    Int16*              ringBuf;
    UINT32              ringSize;       // capacity in samples (frames * channels)
    volatile LONG       ringWritePos;   // index in samples
    volatile LONG       ringReadPos;    // index in samples
};

//  helpers

static inline UINT32 ringAvail(const WasapiSound* ws)
{
    LONG wp = ws->ringWritePos;
    LONG rp = ws->ringReadPos;
    return (UINT32)((wp - rp + (LONG)ws->ringSize) % (LONG)ws->ringSize);
}

static void ringWrite(WasapiSound* ws, const Int16* data, UINT32 samples)
{
    UINT32 wp = (UINT32)ws->ringWritePos;
    for (UINT32 i = 0; i < samples; i++) {
        ws->ringBuf[(wp + i) % ws->ringSize] = data[i];
    }
    MemoryBarrier();
    InterlockedExchange(&ws->ringWritePos, (LONG)((wp + samples) % ws->ringSize));
}

// Read up to `frames` PCM Int16 frames from ring into dst.
// Returns frames actually read (may be less on underrun).
static UINT32 ringReadFrames(WasapiSound* ws, BYTE* dst, UINT32 frames)
{
    UINT32 samplesAvail = ringAvail(ws);
    UINT32 framesAvail  = samplesAvail / ws->channels;
    if (framesAvail > frames) framesAvail = frames;

    UINT32 samplesToRead = framesAvail * ws->channels;
    UINT32 rp = (UINT32)ws->ringReadPos;
    Int16* out = (Int16*)dst;

    // Copy in up to two contiguous chunks to avoid per-sample modulo.
    UINT32 tail = ws->ringSize - rp;
    if (samplesToRead <= tail) {
        memcpy(out, ws->ringBuf + rp, samplesToRead * sizeof(Int16));
    } else {
        memcpy(out,        ws->ringBuf + rp, tail                       * sizeof(Int16));
        memcpy(out + tail, ws->ringBuf,      (samplesToRead - tail)     * sizeof(Int16));
    }

    MemoryBarrier();
    InterlockedExchange(&ws->ringReadPos, (LONG)((rp + samplesToRead) % ws->ringSize));
    return framesAvail;
}

//  write callback (emulator thread)

static Int32 wasapiWrite(void* ref, Int16* buf, UInt32 count)
{
    WasapiSound* ws = (WasapiSound*)ref;
    if (ws->suspended) return 0;

    // Drop silently on ring overflow rather than stalling the emulator.
    UINT32 free = ws->ringSize - ringAvail(ws);
    if (free < count) return 0;

    ringWrite(ws, buf, count);
    return 0;
}

//  render thread

static DWORD WINAPI wasapiRenderThread(LPVOID param)
{
    WasapiSound* ws = (WasapiSound*)param;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // Elevate thread to AVRT "Pro Audio" priority.
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);

    HANDLE events[2] = { ws->hFeedEvent, ws->hStopEvent };

    while (TRUE) {
        DWORD r = WaitForMultipleObjects(2, events, FALSE, 500);
        if (r == WAIT_OBJECT_0 + 1) break;
        if (r == WAIT_TIMEOUT) continue;
        if (r != WAIT_OBJECT_0) break;

        // While suspended, discard pending ring data here on the reader
        // side (which owns ringReadPos): the concealment path below then
        // decays smoothly instead of the hard Stop/Start click.
        if (ws->suspended) {
            InterlockedExchange(&ws->ringReadPos, ws->ringWritePos);
        }

        UINT32 padding = 0;
        if (FAILED(ws->audioClient->GetCurrentPadding(&padding))) continue;

        UINT32 available = ws->bufferFrames - padding;
        if (available == 0) continue;

        UINT32 ch = ws->channels;

        // after a gap, keep holding until the ring has refilled enough
        // that a struggling producer cannot cause a click flutter
        if (ws->primeFrames > 0 && ringAvail(ws) / ch < ws->primeFrames) {
            BYTE* pHold = NULL;
            if (FAILED(ws->renderClient->GetBuffer(available, &pHold))) continue;
            Int16* hold = (Int16*)pHold;
            for (UINT32 i = 0; i < available; i++) {
                for (UINT32 c = 0; c < ch; c++) {
                    ws->lastFrame[c] = (Int16)(((Int32)ws->lastFrame[c] * 15) / 16);
                    hold[i * ch + c] = ws->lastFrame[c];
                }
            }
            ws->renderClient->ReleaseBuffer(available, 0);
            continue;
        }
        ws->primeFrames = 0;

        BYTE* pData = NULL;
        if (FAILED(ws->renderClient->GetBuffer(available, &pData))) continue;

        UINT32 written = ringReadFrames(ws, pData, available);

        Int16* out = (Int16*)pData;

        // crossfade from the held decay into the fresh data after a gap
        if (written > 0 && ws->fadeInFrames > 0) {
            UINT32 n = ws->fadeInFrames < written ? ws->fadeInFrames : written;
            for (UINT32 i = 0; i < n; i++) {
                UINT32 k = FADE_FRAMES - ws->fadeInFrames + i + 1;
                for (UINT32 c = 0; c < ch; c++) {
                    Int32 mixv = ((Int32)out[i * ch + c] * (Int32)k +
                                  (Int32)ws->lastFrame[c] * (Int32)(FADE_FRAMES - k)) / FADE_FRAMES;
                    out[i * ch + c] = (Int16)mixv;
                    ws->lastFrame[c] = (Int16)(((Int32)ws->lastFrame[c] * 15) / 16);
                }
            }
            ws->fadeInFrames -= n;
        }
        if (written > 0) {
            for (UINT32 c = 0; c < ch; c++) {
                ws->lastFrame[c] = out[(written - 1) * ch + c];
            }
        }

        if (written < available) {
            // underrun: decay from the last sample towards silence
            // instead of jumping to zero, then crossfade back in
            Int16* fill = out + written * ch;
            UINT32 gap = available - written;
            for (UINT32 i = 0; i < gap; i++) {
                for (UINT32 c = 0; c < ch; c++) {
                    ws->lastFrame[c] = (Int16)(((Int32)ws->lastFrame[c] * 15) / 16);
                    fill[i * ch + c] = ws->lastFrame[c];
                }
            }
            ws->fadeInFrames = FADE_FRAMES;
            ws->primeFrames  = PRIME_FRAMES;
        }

        ws->renderClient->ReleaseBuffer(available, 0);
    }

    if (hTask) AvRevertMmThreadCharacteristics(hTask);
    CoUninitialize();
    return 0;
}

//  public API

WasapiSound* wasapiSoundCreate(HWND /*hwnd*/, Mixer* mixer,
                               UInt32 sampleRate, UInt32 bufferSizeMs,
                               Int16 channels)
{
    WasapiSound* ws = (WasapiSound*)calloc(1, sizeof(WasapiSound));
    if (!ws) return NULL;

    ws->mixer    = mixer;
    ws->channels = (UINT32)channels;

    HRESULT hr;
    // No CoInitializeEx here: the UI thread is already STA, and a second
    // MULTITHREADED init corrupts STA state and hangs IFileDialog.  WASAPI
    // only needs COM on the render thread, which inits its own MTA below.
    IMMDeviceEnumerator* pEnum = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
    if (FAILED(hr)) goto fail;

    IMMDevice* pDevice = NULL;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnum->Release();
    if (FAILED(hr)) goto fail;

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                           NULL, (void**)&ws->audioClient);
    pDevice->Release();
    if (FAILED(hr)) goto fail;

    // Build our native PCM format.  AUTOCONVERTPCM lets the audio engine
    // resample/convert to whatever the device mix format actually is, so
    // we never need to know or match it ourselves.
    {
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = (WORD)channels;
        wfx.nSamplesPerSec  = sampleRate;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = (WORD)(channels * 2);
        wfx.nAvgBytesPerSec = sampleRate * channels * 2;

        REFERENCE_TIME bufDuration = (REFERENCE_TIME)bufferSizeMs * 10000LL;
        if (bufDuration < WASAPI_BUF_DURATION) bufDuration = WASAPI_BUF_DURATION;

        hr = ws->audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK   |
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM  |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                bufDuration, 0, &wfx, NULL);
        if (FAILED(hr)) goto fail;
    }

    hr = ws->audioClient->GetBufferSize(&ws->bufferFrames);
    if (FAILED(hr)) goto fail;

    // Convert actual allocated frames to ms for UI display.
    g_actualBufferMs = (UInt32)(((UInt64)ws->bufferFrames * 1000 + sampleRate / 2) / sampleRate);

    hr = ws->audioClient->GetService(__uuidof(IAudioRenderClient),
                                     (void**)&ws->renderClient);
    if (FAILED(hr)) goto fail;

    // Events and render thread.
    ws->hFeedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    ws->hStopEvent = CreateEvent(NULL, TRUE,  FALSE, NULL);
    if (!ws->hFeedEvent || !ws->hStopEvent) goto fail;

    hr = ws->audioClient->SetEventHandle(ws->hFeedEvent);
    if (FAILED(hr)) goto fail;

    // Ring buffer.
    ws->ringSize = RING_FRAMES * ws->channels;
    ws->ringBuf  = (Int16*)malloc(ws->ringSize * sizeof(Int16));
    if (!ws->ringBuf) goto fail;
    memset(ws->ringBuf, 0, ws->ringSize * sizeof(Int16));

    // Start in the primed state so playback begins only once the mixer
    // has produced a solid backlog.
    ws->primeFrames = PRIME_FRAMES;

    ws->hThread = CreateThread(NULL, 0, wasapiRenderThread, ws, 0, NULL);
    if (!ws->hThread) goto fail;

    hr = ws->audioClient->Start();
    if (FAILED(hr)) goto fail;

    // Fragment size: target ~10 ms per mixer callback so the ring stays
    // well-filled between WASAPI events (which fire every ~10 ms).
    UINT32 fragmentSamples = sampleRate / 100 * (UINT32)channels; // 10 ms
    if (fragmentSamples < 64)   fragmentSamples = 64;
    if (fragmentSamples > 1024) fragmentSamples = 1024;

    /* Driver always runs 2ch; mixer stereo flag is a user preference. */
    mixerSetWriteCallback(mixer, wasapiWrite, ws, (int)fragmentSamples);

    return ws;

fail:
    wasapiSoundDestroy(ws);
    return NULL;
}

void wasapiSoundDestroy(WasapiSound* ws)
{
    if (!ws) return;

    mixerSetWriteCallback(ws->mixer, NULL, NULL, 0);

    if (ws->hThread) {
        if (ws->hStopEvent) SetEvent(ws->hStopEvent);
        WaitForSingleObject(ws->hThread, 2000);
        CloseHandle(ws->hThread);
    }
    if (ws->audioClient) {
        ws->audioClient->Stop();
        ws->audioClient->Release();
    }
    if (ws->renderClient) ws->renderClient->Release();
    if (ws->hFeedEvent)  CloseHandle(ws->hFeedEvent);
    if (ws->hStopEvent)  CloseHandle(ws->hStopEvent);
    if (ws->ringBuf)     free(ws->ringBuf);

    CoUninitialize();
    free(ws);

    g_actualBufferMs = 0;
}

UInt32 wasapiSoundGetActualBufferMs(void)
{
    return g_actualBufferMs;
}

void wasapiSoundSuspend(WasapiSound* ws)
{
    if (!ws) return;
    // Keep the audio client running: a hard Stop mid-buffer clicks. The
    // render thread discards pending input and decays to silence itself;
    // resetting the ring positions here would race the reader.
    ws->suspended = TRUE;
}

void wasapiSoundResume(WasapiSound* ws)
{
    if (!ws) return;
    // Nothing else to arm: the render thread primed itself and set up the
    // crossfade in the underrun path when the discarded ring ran dry, and
    // its fields must not be written from this thread anyway.
    ws->suspended = FALSE;
}
