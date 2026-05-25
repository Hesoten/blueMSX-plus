/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32VideoIn.cpp,v $
**
** $Revision: 1.9 $
**
** $Date: 2008-03-30 18:38:48 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
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
**
** Media Foundation IMFSourceReader on a dedicated MTA worker thread;
** the emu thread reads via a triple-buffer slot.
*/
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "Win32VideoIn.h"
#include "ArchVideoIn.h"
#include "Language.h"
}

#include "Win32MediaFoundation.h"

namespace {

struct DeviceInfo {
    std::wstring symLink;
    std::string  friendlyName;   // UTF-8
};

struct FrameSlot {
    std::vector<UInt16> px;      // packed RGB555, w*h entries
    int w = 0;
    int h = 0;
};

// Triple buffer: worker fills the free slot (!= published, != reading),
// consumer reads published into reading so the producer skips it next.
static FrameSlot         g_slots[3];
static std::atomic<int>  g_publishedSlot{-1};
static std::atomic<int>  g_readingSlot{-1};

static std::string wideToUtf8(const wchar_t* w, size_t len)
{
    if (!w || len == 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, (int)len, NULL, 0, NULL, NULL);
    if (n <= 0) return {};
    std::string out((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, (int)len, &out[0], n, NULL, NULL);
    return out;
}

static std::wstring utf8ToWide(const char* s)
{
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 1) return {};
    std::wstring out((size_t)(n - 1), 0);  // -1 to drop trailing NUL
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], n);
    return out;
}

class VideoInWorker {
public:
    void start();
    void stop();

    // Called from main/emu thread.
    std::vector<DeviceInfo> getDevices();
    void requestActiveSymLink(const std::wstring& symLink);  // empty = close

private:
    void run();
    void enumerateDevices();
    HRESULT openDeviceLocked(const std::wstring& symLink);
    void closeDeviceLocked();
    HRESULT readOnce();
    void publishRgb32(const BYTE* src, int srcStride, int w, int h);
    void publishNv12 (const BYTE* src, int w, int h);
    void publishYuy2 (const BYTE* src, int w, int h);
    void cacheCurrentMediaType();

    std::thread             th;
    std::atomic<bool>       stopFlag{false};
    std::condition_variable cmdCv;
    std::mutex              cmdMtx;
    enum Cmd { CMD_NONE, CMD_OPEN, CMD_CLOSE };
    Cmd                     pendingCmd = CMD_NONE;
    std::wstring            pendingSymLink;

    std::mutex                              devicesMtx;
    std::vector<DeviceInfo>                 devices;

    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    int                                     srcW = 0;
    int                                     srcH = 0;
    GUID                                    srcSubtype = GUID_NULL;
    int                                     producerNextSlot = 0;
};

// BT.601 YUV (limited range) -> RGB555.
static inline UInt16 yuvToRgb555(int y, int u, int v)
{
    int c = y - 16;
    int d = u - 128;
    int e = v - 128;
    int r = (298 * c           + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d           + 128) >> 8;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return (UInt16)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

void VideoInWorker::start()
{
    stopFlag = false;
    th = std::thread(&VideoInWorker::run, this);
}

void VideoInWorker::stop()
{
    {
        std::lock_guard<std::mutex> lg(cmdMtx);
        stopFlag = true;
    }
    cmdCv.notify_all();
    if (th.joinable()) th.join();
}

std::vector<DeviceInfo> VideoInWorker::getDevices()
{
    std::lock_guard<std::mutex> lg(devicesMtx);
    return devices;
}

void VideoInWorker::requestActiveSymLink(const std::wstring& symLink)
{
    {
        std::lock_guard<std::mutex> lg(cmdMtx);
        if (symLink.empty()) {
            pendingCmd = CMD_CLOSE;
            pendingSymLink.clear();
        } else {
            pendingCmd = CMD_OPEN;
            pendingSymLink = symLink;
        }
    }
    cmdCv.notify_all();
}

void VideoInWorker::run()
{
    HRESULT coHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool coOwned = SUCCEEDED(coHr) && coHr != RPC_E_CHANGED_MODE;

    ensureMFStartupOnce();
    if (!isMFStartupOk()) {
        if (coOwned) CoUninitialize();
        return;
    }

    enumerateDevices();

    while (!stopFlag.load()) {
        Cmd cmd = CMD_NONE;
        std::wstring symLink;
        {
            std::unique_lock<std::mutex> lk(cmdMtx);
            if (!reader) {
                // Idle: block until command or stop.
                cmdCv.wait(lk, [this] { return stopFlag.load() || pendingCmd != CMD_NONE; });
            }
            if (stopFlag.load()) break;
            cmd = pendingCmd;
            symLink = std::move(pendingSymLink);
            pendingCmd = CMD_NONE;
            pendingSymLink.clear();
        }

        switch (cmd) {
        case CMD_OPEN:
            closeDeviceLocked();
            (void)openDeviceLocked(symLink);
            break;
        case CMD_CLOSE:
            closeDeviceLocked();
            break;
        case CMD_NONE:
        default:
            break;
        }

        if (reader) {
            HRESULT hr = readOnce();
            if (FAILED(hr)) {
                closeDeviceLocked();
            }
        }
    }

    closeDeviceLocked();
    if (coOwned) CoUninitialize();
}

// Free helper -- callable from any COM-initialized thread that has MFStartup
// ok. Used both from the worker (initial enum) and the main thread
// (videoInOnDeviceChange refresh on WM_DEVICECHANGE).
static std::vector<DeviceInfo> enumerateDevicesNow()
{
    std::vector<DeviceInfo> tmp;
    Microsoft::WRL::ComPtr<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return tmp;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** activates = NULL;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attr.Get(), &activates, &count))) return tmp;

    tmp.reserve(count);
    for (UINT32 i = 0; i < count; i++) {
        DeviceInfo d;
        WCHAR* nameW = NULL; UINT32 nameLen = 0;
        if (SUCCEEDED(activates[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nameW, &nameLen))) {
            d.friendlyName = wideToUtf8(nameW, nameLen);
            CoTaskMemFree(nameW);
        }
        WCHAR* linkW = NULL; UINT32 linkLen = 0;
        if (SUCCEEDED(activates[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                &linkW, &linkLen))) {
            d.symLink.assign(linkW, linkLen);
            CoTaskMemFree(linkW);
        }
        activates[i]->Release();
        tmp.push_back(std::move(d));
    }
    CoTaskMemFree(activates);
    return tmp;
}

void VideoInWorker::enumerateDevices()
{
    auto tmp = enumerateDevicesNow();
    {
        std::lock_guard<std::mutex> lg(devicesMtx);
        devices = std::move(tmp);
    }
}

HRESULT VideoInWorker::openDeviceLocked(const std::wstring& symLink)
{
    // Re-enumerate to find IMFActivate matching symLink (refs kept worker-local).
    Microsoft::WRL::ComPtr<IMFAttributes> attr;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (FAILED(hr)) return hr;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** activates = NULL;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attr.Get(), &activates, &count);
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IMFActivate> chosen;
    for (UINT32 i = 0; i < count; i++) {
        WCHAR* linkW = NULL; UINT32 linkLen = 0;
        if (SUCCEEDED(activates[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                &linkW, &linkLen))) {
            if (!chosen && symLink == std::wstring(linkW, linkLen)) {
                chosen = activates[i];
            }
            CoTaskMemFree(linkW);
        }
    }
    for (UINT32 i = 0; i < count; i++) activates[i]->Release();
    CoTaskMemFree(activates);

    if (!chosen) return MF_E_NOT_FOUND;

    Microsoft::WRL::ComPtr<IMFMediaSource> source;
    hr = chosen->ActivateObject(IID_PPV_ARGS(&source));
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IMFAttributes> readerAttr;
    if (SUCCEEDED(MFCreateAttributes(&readerAttr, 1))) {
        readerAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }

    hr = MFCreateSourceReaderFromMediaSource(source.Get(), readerAttr.Get(), &reader);
    if (FAILED(hr)) {
        return hr;
    }

    // Format negotiation: prefer RGB32 (zero-conversion for our 16bpp
    // output path), fall back to NV12 (most modern UVC webcams), then
    // YUY2 (older webcams, OBS Virtual Camera default).
    static const GUID kPreferredSubtypes[] = {
        MFVideoFormat_RGB32, MFVideoFormat_NV12, MFVideoFormat_YUY2
    };
    HRESULT setHr = E_FAIL;
    for (const GUID& sub : kPreferredSubtypes) {
        Microsoft::WRL::ComPtr<IMFMediaType> outType;
        MFCreateMediaType(&outType);
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE,    sub);
        setHr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                            NULL, outType.Get());
        if (SUCCEEDED(setHr)) break;
    }
    if (FAILED(setHr)) {
        reader.Reset();
        return setHr;
    }

    cacheCurrentMediaType();
    return S_OK;
}

void VideoInWorker::closeDeviceLocked()
{
    if (reader) reader.Reset();
    srcW = 0;
    srcH = 0;
    srcSubtype = GUID_NULL;
    g_publishedSlot.store(-1, std::memory_order_release);
}

void VideoInWorker::cacheCurrentMediaType()
{
    if (!reader) { srcW = 0; srcH = 0; srcSubtype = GUID_NULL; return; }
    Microsoft::WRL::ComPtr<IMFMediaType> currentType;
    if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType))) {
        return;
    }
    UINT32 w = 0, h = 0;
    if (SUCCEEDED(MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &w, &h))) {
        srcW = (int)w;
        srcH = (int)h;
    }
    GUID sub = GUID_NULL;
    if (SUCCEEDED(currentType->GetGUID(MF_MT_SUBTYPE, &sub))) {
        srcSubtype = sub;
    }
}

HRESULT VideoInWorker::readOnce()
{
    if (!reader) return E_FAIL;

    DWORD streamIdx = 0, flags = 0;
    LONGLONG ts = 0;
    Microsoft::WRL::ComPtr<IMFSample> sample;
    HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                    &streamIdx, &flags, &ts, &sample);
    if (FAILED(hr)) return hr;
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return MF_E_END_OF_STREAM;
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
        cacheCurrentMediaType();
    }
    if (!sample) return S_OK;  // dropouts / non-fatal

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) return hr;

    BYTE* data = NULL;
    DWORD maxLen = 0, currentLen = 0;
    hr = buffer->Lock(&data, &maxLen, &currentLen);
    if (FAILED(hr)) return hr;

    if (srcW > 0 && srcH > 0) {
        DWORD need = 0;
        if (srcSubtype == MFVideoFormat_RGB32) {
            need = (DWORD)(srcW * srcH * 4);
            if (currentLen >= need) publishRgb32(data, srcW * 4, srcW, srcH);
        } else if (srcSubtype == MFVideoFormat_NV12) {
            need = (DWORD)(srcW * srcH * 3 / 2);
            if (currentLen >= need) publishNv12(data, srcW, srcH);
        } else if (srcSubtype == MFVideoFormat_YUY2) {
            need = (DWORD)(srcW * srcH * 2);
            if (currentLen >= need) publishYuy2(data, srcW, srcH);
        }
    }

    buffer->Unlock();
    return S_OK;
}

void VideoInWorker::publishRgb32(const BYTE* src, int srcStride, int w, int h)
{
    int avoid1 = g_publishedSlot.load(std::memory_order_acquire);
    int avoid2 = g_readingSlot.load(std::memory_order_acquire);
    int slot = producerNextSlot;
    while (slot == avoid1 || slot == avoid2) slot = (slot + 1) % 3;
    producerNextSlot = (slot + 1) % 3;

    FrameSlot& dst = g_slots[slot];
    if (dst.w != w || dst.h != h) {
        dst.px.assign((size_t)w * (size_t)h, 0);
        dst.w = w;
        dst.h = h;
    }

    // RGB32 from MF SourceReader = BGRA8 packed, top-down (positive stride).
    UInt16* dp = dst.px.data();
    for (int y = 0; y < h; y++) {
        const BYTE* sp = src + (size_t)y * (size_t)srcStride;
        UInt16* row = dp + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            int b = sp[x * 4 + 0];
            int g = sp[x * 4 + 1];
            int r = sp[x * 4 + 2];
            row[x] = (UInt16)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
        }
    }

    g_publishedSlot.store(slot, std::memory_order_release);
}

// NV12: planar 4:2:0. Y plane is w*h bytes, followed by interleaved UV
// plane of w*(h/2) bytes (U V U V ...). Each UV pair covers a 2x2 block
// of pixels.
void VideoInWorker::publishNv12(const BYTE* src, int w, int h)
{
    int avoid1 = g_publishedSlot.load(std::memory_order_acquire);
    int avoid2 = g_readingSlot.load(std::memory_order_acquire);
    int slot = producerNextSlot;
    while (slot == avoid1 || slot == avoid2) slot = (slot + 1) % 3;
    producerNextSlot = (slot + 1) % 3;

    FrameSlot& dst = g_slots[slot];
    if (dst.w != w || dst.h != h) {
        dst.px.assign((size_t)w * (size_t)h, 0);
        dst.w = w;
        dst.h = h;
    }

    const BYTE* yPlane  = src;
    const BYTE* uvPlane = src + (size_t)w * (size_t)h;
    UInt16* dp = dst.px.data();

    for (int y = 0; y < h; y++) {
        const BYTE* yRow  = yPlane  + (size_t)y * (size_t)w;
        const BYTE* uvRow = uvPlane + (size_t)(y / 2) * (size_t)w;
        UInt16*     dRow  = dp      + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            int yy = yRow[x];
            int uu = uvRow[(x / 2) * 2 + 0];
            int vv = uvRow[(x / 2) * 2 + 1];
            dRow[x] = yuvToRgb555(yy, uu, vv);
        }
    }

    g_publishedSlot.store(slot, std::memory_order_release);
}

// YUY2: packed 4:2:2. Each 4 bytes encodes 2 horizontally-adjacent pixels
// (Y0 U Y1 V). U and V are shared between the pixel pair.
void VideoInWorker::publishYuy2(const BYTE* src, int w, int h)
{
    int avoid1 = g_publishedSlot.load(std::memory_order_acquire);
    int avoid2 = g_readingSlot.load(std::memory_order_acquire);
    int slot = producerNextSlot;
    while (slot == avoid1 || slot == avoid2) slot = (slot + 1) % 3;
    producerNextSlot = (slot + 1) % 3;

    FrameSlot& dst = g_slots[slot];
    if (dst.w != w || dst.h != h) {
        dst.px.assign((size_t)w * (size_t)h, 0);
        dst.w = w;
        dst.h = h;
    }

    int srcStride = w * 2;
    UInt16* dp = dst.px.data();
    for (int y = 0; y < h; y++) {
        const BYTE* sRow = src + (size_t)y * (size_t)srcStride;
        UInt16*     dRow = dp  + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x += 2) {
            int y0 = sRow[x * 2 + 0];
            int u  = sRow[x * 2 + 1];
            int y1 = sRow[x * 2 + 2];
            int v  = sRow[x * 2 + 3];
            dRow[x]     = yuvToRgb555(y0, u, v);
            if (x + 1 < w) dRow[x + 1] = yuvToRgb555(y1, u, v);
        }
    }

    g_publishedSlot.store(slot, std::memory_order_release);
}

} // namespace

// -----------------------------------------------------------------------------
// Module-level state. Members hold C++ objects; access from extern "C" blocks
// is fine because the file is compiled as C++.
// -----------------------------------------------------------------------------
typedef struct {
    UInt16* buffer;
    int     width;
    int     height;
    int     inputIndex;
    int     inputCount;
    int     disabled;
    VideoInWorker*           worker;
    std::vector<DeviceInfo>* devices;   // snapshot for fast menu access
} VideoIn;

static VideoIn videoIn = {};

extern "C" int archVideoInIsVideoConnected()
{
    // Connected = worker opened the device AND published at least one frame.
    // Menu selection alone (inputIndex > 0) isn't enough -- open can fail silently.
    if (videoIn.inputIndex == 0) return 0;
    return g_publishedSlot.load(std::memory_order_acquire) >= 0 ? 1 : 0;
}

extern "C" UInt16* archVideoInBufferGet(int width, int height)
{
    if (videoIn.inputIndex == 0) return NULL;

    int slot = g_publishedSlot.load(std::memory_order_acquire);
    if (slot < 0) return NULL;
    g_readingSlot.store(slot, std::memory_order_release);

    const FrameSlot& src = g_slots[slot];
    if (src.w <= 0 || src.h <= 0 || src.px.empty()) return NULL;

    if (width != videoIn.width || height != videoIn.height) {
        if (videoIn.buffer != NULL) free(videoIn.buffer);
        videoIn.buffer = (UInt16*)calloc(1, sizeof(UInt16) * (size_t)width * (size_t)height);
        videoIn.width  = width;
        videoIn.height = height;
    }
    if (!videoIn.buffer) return NULL;

    // Aspect-preserving center crop then nearest-neighbor downscale; VDP
    // overlay expects a fully-filled frame (no padded borders).
    int srcCropW = src.w;
    int srcCropH = src.h;
    int srcOffX  = 0;
    int srcOffY  = 0;
    if ((long long)src.w * height > (long long)src.h * width) {
        // src is wider than target -> crop sides
        srcCropW = (int)((long long)src.h * width / height);
        if (srcCropW < 1) srcCropW = 1;
        srcOffX = (src.w - srcCropW) / 2;
    } else {
        // src is taller (or equal) -> crop top/bottom
        srcCropH = (int)((long long)src.w * height / width);
        if (srcCropH < 1) srcCropH = 1;
        srcOffY = (src.h - srcCropH) / 2;
    }

    for (int y = 0; y < height; y++) {
        int sy = srcOffY + (int)((long long)y * srcCropH / height);
        const UInt16* sp = src.px.data() + (size_t)sy * (size_t)src.w;
        UInt16* dp = videoIn.buffer + (size_t)y * (size_t)width;
        for (int x = 0; x < width; x++) {
            int sx = srcOffX + (int)((long long)x * srcCropW / width);
            dp[x] = sp[sx];
        }
    }

    return videoIn.buffer;
}

extern "C" void videoInInitialize(Properties* properties)
{
    videoIn.inputCount = 1;     // index 0 = "None"
    videoIn.inputIndex = 0;
    videoIn.disabled   = properties->videoIn.disabled;
    if (videoIn.disabled) return;

    // MFEnumDeviceSources requires MF to be up. ensureMFStartupOnce is
    // idempotent and process-wide (shared with the recorder).
    ensureMFStartupOnce();
    if (!isMFStartupOk()) {
        videoIn.disabled = 1;
        return;
    }

    videoIn.worker  = new VideoInWorker;
    videoIn.devices = new std::vector<DeviceInfo>;
    
    // Enumerate synchronously on the main thread so the device list is
    // ready by the first menu popup. Calling worker->getDevices() right
    // after start() races the worker's own initial enumeration and
    // typically returns an empty list. The worker still re-enumerates
    // inside openDeviceLocked for the IMFActivate lookup.
    *videoIn.devices = enumerateDevicesNow();
    videoIn.inputCount = 1 + (int)videoIn.devices->size();

    videoIn.worker->start();

    // Restore previous device (MF symlink first, friendly name as fallback).
    int restored = 0;
    if (properties->videoIn.inputName[0]) {
        std::wstring wantedLink = utf8ToWide(properties->videoIn.inputName);
        if (!wantedLink.empty()) {
            for (size_t i = 0; i < videoIn.devices->size(); i++) {
                if ((*videoIn.devices)[i].symLink == wantedLink) {
                    videoIn.inputIndex = (int)(i + 1);
                    restored = 1;
                    break;
                }
            }
        }
        if (!restored) {
            for (size_t i = 0; i < videoIn.devices->size(); i++) {
                if ((*videoIn.devices)[i].friendlyName == properties->videoIn.inputName) {
                    videoIn.inputIndex = (int)(i + 1);
                    restored = 1;
                    break;
                }
            }
        }
    }
    if (!restored) {
        videoIn.inputIndex = properties->videoIn.inputIndex;
        if (videoIn.inputIndex < 0 || videoIn.inputIndex >= videoIn.inputCount) {
            videoIn.inputIndex = 0;
        }
    }
    if (videoIn.inputIndex > 0) {
        videoIn.worker->requestActiveSymLink(
            (*videoIn.devices)[videoIn.inputIndex - 1].symLink);
    }
}

extern "C" void videoInCleanup(Properties* properties)
{
    if (videoIn.disabled) return;

    properties->videoIn.inputIndex = videoIn.inputIndex;
    if (videoIn.inputIndex > 0 && videoIn.devices &&
        videoIn.inputIndex <= (int)videoIn.devices->size()) {
        // Persist the MF symbolic link (stable identifier). Falls back to
        // friendly name if the link is unavailable for some reason.
        const DeviceInfo& d = (*videoIn.devices)[videoIn.inputIndex - 1];
        std::string utf8;
        if (!d.symLink.empty()) {
            utf8 = wideToUtf8(d.symLink.data(), d.symLink.size());
        }
        if (utf8.empty()) utf8 = d.friendlyName;
        size_t cap = sizeof(properties->videoIn.inputName);
        size_t n = utf8.size() < cap - 1 ? utf8.size() : cap - 1;
        memcpy(properties->videoIn.inputName, utf8.data(), n);
        properties->videoIn.inputName[n] = 0;
    } else {
        // User picked "None" -- clear the persisted device id so the next
        // startup doesn't restore the previously-used camera from the
        // stale inputName field.
        properties->videoIn.inputName[0] = 0;
    }

    if (videoIn.worker) {
        videoIn.worker->stop();
        delete videoIn.worker;
        videoIn.worker = NULL;
    }
    delete videoIn.devices;
    videoIn.devices = NULL;

    if (videoIn.buffer) {
        free(videoIn.buffer);
        videoIn.buffer = NULL;
    }
}

extern "C" int videoInGetCount()
{
    return videoIn.inputCount;
}

extern "C" int videoInIsActive(int index)
{
    return index == videoIn.inputIndex ? 1 : 0;
}

extern "C" int videoInGetActive()
{
    return videoIn.inputIndex;
}

extern "C" const char* videoInGetName(int index)
{
    if (index == 0) return langTextNone();
    if (videoIn.devices && index > 0 && index <= (int)videoIn.devices->size()) {
        return (*videoIn.devices)[index - 1].friendlyName.c_str();
    }
    return langTextUnknown();
}

extern "C" void videoInSetActive(int index)
{
    if (videoIn.disabled) return;
    if (index < 0 || index >= videoIn.inputCount) index = 0;
    videoIn.inputIndex = index;

    if (!videoIn.worker) return;
    if (index == 0) {
        videoIn.worker->requestActiveSymLink(L"");
    } else if (videoIn.devices && index <= (int)videoIn.devices->size()) {
        videoIn.worker->requestActiveSymLink((*videoIn.devices)[index - 1].symLink);
    }
}

// Called from the Win32.c WM_DEVICECHANGE handler. Re-enumerates the device
// list synchronously on the main thread and reconciles the active selection
// with what's actually attached. If the active camera was unplugged, falls
// back to inputIndex 0 (None) and requests the worker to close.
extern "C" void videoInOnDeviceChange(void)
{
    if (videoIn.disabled || !videoIn.devices) return;

    std::vector<DeviceInfo> fresh = enumerateDevicesNow();

    // Capture the active device's symbolic link before swapping the cache,
    // so we can find its new index (or detect its absence) post-refresh.
    std::wstring activeSymLink;
    if (videoIn.inputIndex > 0 &&
        videoIn.inputIndex <= (int)videoIn.devices->size()) {
        activeSymLink = (*videoIn.devices)[videoIn.inputIndex - 1].symLink;
    }

    *videoIn.devices = std::move(fresh);
    videoIn.inputCount = 1 + (int)videoIn.devices->size();

    if (videoIn.inputIndex > 0) {
        int newIdx = 0;
        for (size_t i = 0; i < videoIn.devices->size(); i++) {
            if ((*videoIn.devices)[i].symLink == activeSymLink) {
                newIdx = (int)(i + 1);
                break;
            }
        }
        if (newIdx == 0) {
            // Active device is gone -- close the stream gracefully.
            videoIn.inputIndex = 0;
            if (videoIn.worker) videoIn.worker->requestActiveSymLink(L"");
        } else {
            videoIn.inputIndex = newIdx;
        }
    }
}
