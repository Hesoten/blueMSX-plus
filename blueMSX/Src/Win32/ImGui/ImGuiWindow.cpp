/*****************************************************************************
**
** Top-level tool windows drawn with Dear ImGui on Direct3D 11.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "ImGuiWindow.h"
#include "ImGuiHost.h"
#include "ImGuiTheme.h"
extern "C" {
#include "../Win32Common.h"
#include "ArchNotifications.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

#pragma comment(lib, "d3d11.lib")

#define FRAME_TIMER_ID  1
/* The system timer ticks every 15.6 ms unless someone raises its rate, so a
** 16 ms timer fires every other tick. Ask for the shortest timer and skip
** ticks that come sooner than this instead. */
#define FRAME_MIN_MS    14
#define SIZING_MIN_MS   50

/* Each window has its own ImGui context, font atlas and swap chain; the
** Win32 and DX11 backends keep their state per context. */
struct ImGuiToolWindow {
    ImGuiToolDesc           desc;
    char                    id[32];
    char                    title[256];
    HWND                    hwnd;
    ImGuiContext*           ctx;
    ImGuiStyle              baseStyle;
    UiTokens                tokens;
    ImFont*                 fonts[UI_FONT_COUNT];
    float                   fontEm[UI_FONT_COUNT];  /* ImGui font size per px of em */
    bool                    dark;
    bool                    themeDirty;
    unsigned                generation;    /* of the device it was opened with */
    IDXGISwapChain1*        swapChain;
    ID3D11RenderTargetView* targetView;
    UINT                    bufferW, bufferH;
    bool                    resizePending; /* check the buffers against the client at the next frame */
    bool                    sizing;        /* inside the move/size loop */
    bool                    inFrame;       /* a modal loop inside draw must not start another */
    bool                    closePending;  /* closing inside draw waits for the frame to end */
    bool                    closing;       /* DestroyWindow has been asked for */
    bool                    destroyed;     /* the HWND is gone; the struct waits for the frame */
    float                   dpiScale;
    float                   zoom;
    float                   appliedZoom;
    float                   zoomWantW, zoomWantH;
    LONG                    zoomSetW, zoomSetH;
    ULONGLONG               lastFrame;
    ImGuiToolWindow*        next;
};

static ImGuiToolWindow*     windows;
/* Structs alive, destroyed or not: they all may still use the device. */
static int                  liveCount;
static ID3D11Device*        device;
static ID3D11DeviceContext* deviceContext;
static unsigned             deviceGeneration;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

/* ---- shared resources ------------------------------------------------- */

/* The font files are read once and lent to every window's atlas. */
struct FontFile {
    void* data;
    int   size;
};
static FontFile fontMain, fontJp;
static int      fontJpNo;
static char     fontMainName[40], fontJpName[40];
static bool     fontsRead;
/* The themed windows' set, read only once one of them opens. */
static FontFile fontBody, fontStrong, fontMono, fontJpBody;
static int      fontJpBodyNo, fontJpStrongNo;
static char     fontMonoName[40], fontJpBodyName[40];
static bool     themedFontsRead;

static void readFontFile(FontFile* f, const char* path)
{
    FILE* file = fopen(path, "rb");
    f->data = NULL;
    f->size = 0;
    if (file == NULL) {
        return;
    }
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        if (size > 0 && fseek(file, 0, SEEK_SET) == 0) {
            f->data = malloc(size);
            if (f->data && fread(f->data, 1, size, file) == (size_t)size) {
                f->size = (int)size;
            }
            else {
                free(f->data);
                f->data = NULL;
            }
        }
    }
    fclose(file);
}

/* Bold faces read better on the dark panels: Segoe UI Bold with Japanese
** from Yu Gothic UI Bold, or Meiryo UI Bold on systems without it. Face
** numbers were read from the font files. */
static void readFonts()
{
    char dir[MAX_PATH];
    char path[MAX_PATH];
    if (fontsRead) {
        return;
    }
    fontsRead = true;
    UINT len = GetWindowsDirectoryA(dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 32) {
        return;
    }
    wsprintfA(path, "%s\\Fonts\\segoeuib.ttf", dir);
    lstrcpyA(fontMainName, "segoeuib.ttf");
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        readFontFile(&fontMain, path);
    }

    fontJpNo = 1;   /* YuGothB.ttc: 0 Yu Gothic Bold, 1 Yu Gothic UI Bold */
    lstrcpyA(fontJpName, "YuGothB.ttc");
    wsprintfA(path, "%s\\Fonts\\YuGothB.ttc", dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        fontJpNo = 2;  /* meiryob.ttc: 2 Meiryo UI Bold */
        lstrcpyA(fontJpName, "meiryob.ttc");
        wsprintfA(path, "%s\\Fonts\\meiryob.ttc", dir);
    }
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        readFontFile(&fontJp, path);
    }
}

static void readThemedFonts()
{
    char dir[MAX_PATH];
    char path[MAX_PATH];
    readFonts();
    if (themedFontsRead) {
        return;
    }
    themedFontsRead = true;
    UINT len = GetWindowsDirectoryA(dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 32) {
        return;
    }
    wsprintfA(path, "%s\\Fonts\\segoeui.ttf", dir);
    readFontFile(&fontBody, path);
    /* Japanese in the weights of Segoe UI and Semibold: Yu Gothic UI Regular
    ** and Semibold, or Meiryo UI and Meiryo UI Bold on systems without them. */
    fontJpBodyNo = 1;   /* YuGothM.ttc: 0 Yu Gothic Medium, 1 Yu Gothic UI Regular */
    lstrcpyA(fontJpBodyName, "YuGothM.ttc");
    wsprintfA(path, "%s\\Fonts\\YuGothM.ttc", dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        fontJpBodyNo = 2;   /* meiryo.ttc: 2 Meiryo UI */
        lstrcpyA(fontJpBodyName, "meiryo.ttc");
        wsprintfA(path, "%s\\Fonts\\meiryo.ttc", dir);
    }
    readFontFile(&fontJpBody, path);
    /* YuGothB.ttc: 2 Yu Gothic UI Semibold; meiryob.ttc keeps Meiryo UI Bold. */
    fontJpStrongNo = lstrcmpA(fontJpName, "YuGothB.ttc") == 0 ? 2 : fontJpNo;
    wsprintfA(path, "%s\\Fonts\\seguisb.ttf", dir);
    readFontFile(&fontStrong, path);
    lstrcpyA(fontMonoName, "CascadiaMono.ttf");
    wsprintfA(path, "%s\\Fonts\\CascadiaMono.ttf", dir);
    readFontFile(&fontMono, path);
    if (fontMono.data == NULL) {
        lstrcpyA(fontMonoName, "consola.ttf");
        wsprintfA(path, "%s\\Fonts\\consola.ttf", dir);
        readFontFile(&fontMono, path);
    }
}

static void freeFontFile(FontFile* f)
{
    free(f->data);
    f->data = NULL;
    f->size = 0;
}

static void freeFonts()
{
    freeFontFile(&fontMain);
    freeFontFile(&fontJp);
    freeFontFile(&fontBody);
    freeFontFile(&fontStrong);
    freeFontFile(&fontMono);
    freeFontFile(&fontJpBody);
    fontsRead = false;
    themedFontsRead = false;
}

static unsigned readU16(const unsigned char* p) { return (p[0] << 8) | p[1]; }
static unsigned readU32(const unsigned char* p) { return ((unsigned)readU16(p) << 16) | readU16(p + 2); }

/* ImGui sizes a font by its ascent plus descent (hhea), Windows by its em:
** the ratio of the two, from the font's own tables. */
static float emRatio(const FontFile* f, float fallback)
{
    const unsigned char* d = (const unsigned char*)f->data;
    unsigned upem = 0;
    int asc = 0, desc = 0;
    if (d == NULL || f->size < 12) {
        return fallback;
    }
    unsigned n = readU16(d + 4);
    for (unsigned i = 0; i < n && 12 + 16 * (i + 1) <= (unsigned)f->size; i++) {
        const unsigned char* rec = d + 12 + 16 * i;
        unsigned off = readU32(rec + 8);
        if (memcmp(rec, "head", 4) == 0 && (unsigned)f->size >= 20 && off <= (unsigned)f->size - 20) {
            upem = readU16(d + off + 18);
        }
        if (memcmp(rec, "hhea", 4) == 0 && off <= (unsigned)f->size - 8) {
            asc  = (short)readU16(d + off + 4);
            desc = (short)readU16(d + off + 6);
        }
    }
    return upem && asc > desc ? (float)(asc - desc) / upem : fallback;
}

/* f with face jpNo of jp merged in for Japanese. */
static ImFont* addThemedFont(const FontFile* f, const char* name, float size, const FontFile* jp, int jpNo,
                             const char* jpName)
{
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = NULL;
    if (f->data) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        lstrcpynA(cfg.Name, name, IM_COUNTOF(cfg.Name));
        font = io.Fonts->AddFontFromMemoryTTF(f->data, f->size, size, &cfg);
    }
    if (font && jp->data) {
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.FontNo    = jpNo;
        merge.FontDataOwnedByAtlas = false;
        lstrcpynA(merge.Name, jpName, IM_COUNTOF(merge.Name));
        io.Fonts->AddFontFromMemoryTTF(jp->data, jp->size, size, &merge);
    }
    return font;
}

/* Segoe UI for text, Semibold for headings and Cascadia Mono for numbers,
** each with Japanese of the same weight merged in. The body comes first: ImGui's default. */
static void loadThemedFonts(ImGuiToolWindow* w)
{
    readThemedFonts();
    w->fontEm[UI_FONT_BODY]   = emRatio(&fontBody, 1.33f);
    w->fontEm[UI_FONT_STRONG] = emRatio(fontStrong.data ? &fontStrong : &fontBody, 1.33f);
    w->fontEm[UI_FONT_MONO]   = emRatio(&fontMono, 1.17f);
    w->fonts[UI_FONT_BODY]   = addThemedFont(&fontBody, "segoeui.ttf", 14 * w->fontEm[UI_FONT_BODY], &fontJpBody, fontJpBodyNo,
                                             fontJpBodyName);
    if (w->fonts[UI_FONT_BODY] == NULL) {
        w->fonts[UI_FONT_BODY] = ImGui::GetIO().Fonts->AddFontDefault();
        w->fontEm[UI_FONT_BODY] = w->fontEm[UI_FONT_STRONG] = 13.0f / 14;
    }
    w->fonts[UI_FONT_STRONG] = addThemedFont(&fontStrong, "seguisb.ttf", 14 * w->fontEm[UI_FONT_STRONG], &fontJp, fontJpStrongNo,
                                             fontJpName);
    w->fonts[UI_FONT_MONO]   = addThemedFont(&fontMono, fontMonoName, 13 * w->fontEm[UI_FONT_MONO], &fontJpBody, fontJpBodyNo,
                                             fontJpBodyName);
    if (w->fonts[UI_FONT_STRONG] == NULL) {
        w->fonts[UI_FONT_STRONG] = w->fonts[UI_FONT_BODY];
        w->fontEm[UI_FONT_STRONG] = w->fontEm[UI_FONT_BODY];
    }
    if (w->fonts[UI_FONT_MONO] == NULL) {
        w->fonts[UI_FONT_MONO] = w->fonts[UI_FONT_BODY];
        w->fontEm[UI_FONT_MONO] = w->fontEm[UI_FONT_BODY];
    }
}

/* Into the current context's atlas, which only borrows the bytes. */
static void loadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    readFonts();

    ImFont* font = NULL;
    if (fontMain.data) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        lstrcpynA(cfg.Name, fontMainName, IM_COUNTOF(cfg.Name));
        font = io.Fonts->AddFontFromMemoryTTF(fontMain.data, fontMain.size, 15.0f, &cfg);
    }
    if (font == NULL) {
        io.Fonts->AddFontDefault();
    }
    if (fontJp.data) {
        ImFontConfig jp;
        jp.MergeMode = true;
        jp.FontNo    = fontJpNo;
        jp.FontDataOwnedByAtlas = false;
        lstrcpynA(jp.Name, fontJpName, IM_COUNTOF(jp.Name));
        io.Fonts->AddFontFromMemoryTTF(fontJp.data, fontJp.size, 15.0f, &jp);
    }
}

static bool createDevice()
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 2,
                                   D3D11_SDK_VERSION, &device, NULL, &deviceContext);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, levels, 2,
                               D3D11_SDK_VERSION, &device, NULL, &deviceContext);
    }
    if (FAILED(hr)) {
        device = NULL;
        deviceContext = NULL;
        return false;
    }
    deviceGeneration++;
    return true;
}

/* Windows still open keep their own references until they go. */
static void releaseDevice()
{
    if (deviceContext) { deviceContext->Release(); deviceContext = NULL; }
    if (device)        { device->Release();        device = NULL; }
}

/* ---- one window ------------------------------------------------------- */

/* The ImGui context of a window that is gone must not be made current again. */
static void restoreContext(ImGuiContext* prev)
{
    ImGuiToolWindow* w;
    for (w = windows; w; w = w->next) {
        if (w->ctx == prev) {
            ImGui::SetCurrentContext(prev);
            return;
        }
    }
    ImGui::SetCurrentContext(NULL);
}

static void createTargetView(ImGuiToolWindow* w)
{
    ID3D11Texture2D* backBuffer = NULL;
    if (SUCCEEDED(w->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) {
        device->CreateRenderTargetView(backBuffer, NULL, &w->targetView);
        backBuffer->Release();
    }
}

static void releaseSwapChain(ImGuiToolWindow* w)
{
    if (w->targetView) { w->targetView->Release(); w->targetView = NULL; }
    if (w->swapChain)  { w->swapChain->Release();  w->swapChain = NULL; }
}

static void bufferSizeFor(HWND wnd, UINT* w, UINT* h)
{
    RECT client = { 0, 0, 0, 0 };
    GetClientRect(wnd, &client);
    *w = client.right > 0 ? (UINT)client.right : 1;
    *h = client.bottom > 0 ? (UINT)client.bottom : 1;
}

static bool createSwapChain(ImGuiToolWindow* w)
{
    IDXGIDevice*   dxgiDevice = NULL;
    IDXGIAdapter*  adapter    = NULL;
    IDXGIFactory2* factory    = NULL;
    device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (dxgiDevice) dxgiDevice->GetAdapter(&adapter);
    if (adapter)    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    /* Between frames of a resize the last one shows at its own size in the
    ** corner, not stretched over the new one. */
    desc.Scaling          = DXGI_SCALING_NONE;
    bufferSizeFor(w->hwnd, &w->bufferW, &w->bufferH);
    desc.Width            = w->bufferW;
    desc.Height           = w->bufferH;
    HRESULT hr = factory ? factory->CreateSwapChainForHwnd(device, w->hwnd, &desc, NULL, NULL, &w->swapChain) : E_FAIL;
    if (factory) {
        factory->MakeWindowAssociation(w->hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    if (adapter)    adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
    if (FAILED(hr)) {
        w->swapChain = NULL;
        return false;
    }
    createTargetView(w);
    return true;
}

/* The user's zoom rides on the DPI scale, so everything drawn in DPI units
** follows it. Runs with the window's context current. */
static void applyDpi(ImGuiToolWindow* w, float scale)
{
    ImGuiStyle& style = ImGui::GetStyle();
    w->dpiScale = scale;
    w->appliedZoom = w->zoom;
    style = w->baseStyle;
    style.ScaleAllSizes(scale * w->zoom);
    style.FontScaleDpi = scale * w->zoom;
    if (!(w->desc.flags & IMGUI_TOOL_ALWAYS_DARK)) {
        /* ScaleAllSizes truncates and ImGui rounds the font size; controls
        ** must be Px(24) high exactly, so an odd remainder keeps its half. */
        float s = scale * w->zoom;
        float font = floorf(14 * w->fontEm[UI_FONT_BODY] * s + 0.5f);
        style.FramePadding.y = (floorf(24 * s + 0.5f) - font) / 2;
    }
}

/* Runs with the window's context current, whose style starts as ImGui's dark one. */
static void buildBaseStyle(ImGuiToolWindow* w)
{
    w->themeDirty = false;
    if (w->desc.flags & IMGUI_TOOL_ALWAYS_DARK) {
        ImGuiStyle style;
        w->dark = true;
        ImGui::StyleColorsDark(&style);
        w->baseStyle = style;
    }
    else {
        w->dark = imguiThemeIsDark();
        imguiThemeBuildTokens(&w->tokens, w->dark, imguiThemeReadAccent(w->dark));
        imguiThemeBuildStyle(&w->baseStyle, w->tokens, 14 * w->fontEm[UI_FONT_BODY]);
    }
}

/* A window opens again at the zoom it was closed with, for the process lifetime. */
static struct { char id[32]; float zoom; } zoomMemory[8];

static float* zoomSlot(const char* id)
{
    int i;
    for (i = 0; i < (int)(sizeof(zoomMemory) / sizeof(zoomMemory[0])); i++) {
        if (zoomMemory[i].id[0] == 0) {
            lstrcpynA(zoomMemory[i].id, id, sizeof(zoomMemory[i].id));
            zoomMemory[i].zoom = 1.0f;
        }
        if (lstrcmpA(zoomMemory[i].id, id) == 0) {
            return &zoomMemory[i].zoom;
        }
    }
    return NULL;
}

static const float zoomSteps[] = { 0.5f, 0.67f, 0.75f, 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f };
#define ZOOM_STEPS ((int)(sizeof(zoomSteps) / sizeof(zoomSteps[0])))

static LONG maxOfLong(LONG a, LONG b)
{
    return a > b ? a : b;
}

/* Scale the client area by `ratio`, keeping the top-left corner, within the
** monitor's work area. The size it aims for is kept, so a step stopped by
** the work area comes back exactly; a size the user set starts afresh. */
static void resizeClientBy(ImGuiToolWindow* win, float ratio)
{
    HWND hwnd = win->hwnd;
    RECT outer, client;
    MONITORINFO mi;
    if (IsZoomed(hwnd) || IsIconic(hwnd) || !GetWindowRect(hwnd, &outer) || !GetClientRect(hwnd, &client)) {
        return;
    }
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        return;
    }
    if (client.right != win->zoomSetW || client.bottom != win->zoomSetH) {
        win->zoomWantW = (float)client.right;
        win->zoomWantH = (float)client.bottom;
    }
    win->zoomWantW *= ratio;
    win->zoomWantH *= ratio;
    int frameW = (outer.right - outer.left) - client.right;
    int frameH = (outer.bottom - outer.top) - client.bottom;
    int w = (int)(win->zoomWantW + 0.5f) + frameW;
    int h = (int)(win->zoomWantH + 0.5f) + frameH;
    int x = outer.left, y = outer.top;
    /* Growing never shrinks a window already wider than the work area. */
    LONG maxW = maxOfLong(mi.rcWork.right - mi.rcWork.left, ratio > 1 ? outer.right - outer.left : 0);
    LONG maxH = maxOfLong(mi.rcWork.bottom - mi.rcWork.top, ratio > 1 ? outer.bottom - outer.top : 0);
    if (w > maxW) w = maxW;
    if (h > maxH) h = maxH;
    if (x + w > mi.rcWork.right)  x = maxOfLong(mi.rcWork.left, mi.rcWork.right - w);
    if (y + h > mi.rcWork.bottom) y = maxOfLong(mi.rcWork.top, mi.rcWork.bottom - h);
    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    if (GetClientRect(hwnd, &client)) {
        win->zoomSetW = client.right;
        win->zoomSetH = client.bottom;
    }
}

/* One step up or down from the current zoom; 0 goes back to 100%. The
** window grows or shrinks with its contents. */
static void stepZoom(ImGuiToolWindow* w, int dir)
{
    float old = w->zoom;
    int i;
    if (dir == 0) {
        w->zoom = 1.0f;
    }
    else {
        if (dir > 0) {
            for (i = 0; i < ZOOM_STEPS - 1 && zoomSteps[i] <= w->zoom + 0.001f; i++) {}
        }
        else {
            for (i = ZOOM_STEPS - 1; i > 0 && zoomSteps[i] >= w->zoom - 0.001f; i--) {}
        }
        w->zoom = zoomSteps[i];
    }
    if (w->zoom != old) {
        float* kept = zoomSlot(w->id);
        if (kept) {
            *kept = w->zoom;
        }
        resizeClientBy(w, w->zoom / old);
    }
}

/* Ctrl+wheel, Ctrl+plus, Ctrl+minus and Ctrl+0 zoom like the debugger's
** views. Returns true when the message was used. */
static bool zoomMessage(ImGuiToolWindow* w, UINT msg, WPARAM wParam)
{
    if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)) {
        stepZoom(w, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
        return true;
    }
    if (msg == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0) {
        switch (wParam) {
        case VK_OEM_PLUS:  case VK_ADD:      stepZoom(w, 1);  return true;
        case VK_OEM_MINUS: case VK_SUBTRACT: stepZoom(w, -1); return true;
        case '0':          case VK_NUMPAD0:  stepZoom(w, 0);  return true;
        }
    }
    return false;
}


static bool deviceLost(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
}

/* False for a window left over from a lost device, waiting to close. */
static bool hasDevice(ImGuiToolWindow* w)
{
    return device != NULL && w->generation == deviceGeneration;
}

/* Frees the struct of a window whose HWND is gone. Not while it is in a frame. */
static void destroyInstance(ImGuiToolWindow* w)
{
    ImGuiToolWindow** p;
    if (w->ctx) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(w->ctx);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(w->ctx);
        w->ctx = NULL;
        if (prev != NULL) {
            restoreContext(prev);
        }
    }
    releaseSwapChain(w);
    for (p = &windows; *p; p = &(*p)->next) {
        if (*p == w) {
            *p = w->next;
            break;
        }
    }
    if (w->desc.closed) {
        w->desc.closed(w->desc.ref);
    }
    free(w);
    if (--liveCount == 0) {
        releaseDevice();
        freeFonts();
    }
}

static void closeWindow(ImGuiToolWindow* w);

/* Every window draws with the lost device, so all of them go. A new device
** comes with the next window opened. */
static void onDeviceLost()
{
    ImGuiToolWindow* w;
    releaseDevice();
    for (;;) {
        for (w = windows; w; w = w->next) {
            if (!w->destroyed && !w->closing && !w->closePending) {
                break;
            }
        }
        if (w == NULL) {
            break;
        }
        closeWindow(w);
    }
}

static bool mouseHeld(ImGuiToolWindow* w)
{
    for (bool down : w->ctx->IO.MouseDown) {
        if (down) return true;
    }
    return false;
}

static void renderFrame(ImGuiToolWindow* w)
{
    if (w->inFrame || w->destroyed || w->swapChain == NULL || !hasDevice(w) ||
        IsIconic(w->hwnd) || !IsWindowVisible(w->hwnd)) {
        return;
    }
    /* The buffers follow the client size once, at the next frame, rather
    ** than on every WM_SIZE. */
    if (w->resizePending) {
        UINT bw, bh;
        w->resizePending = false;
        bufferSizeFor(w->hwnd, &bw, &bh);
        if (bw != w->bufferW || bh != w->bufferH) {
            deviceContext->OMSetRenderTargets(0, NULL, NULL);
            if (w->targetView) { w->targetView->Release(); w->targetView = NULL; }
            if (FAILED(w->swapChain->ResizeBuffers(0, bw, bh, DXGI_FORMAT_UNKNOWN, 0))) {
                closeWindow(w);
                return;
            }
            w->bufferW = bw;
            w->bufferH = bh;
            createTargetView(w);
            if (w->targetView == NULL) {
                /* Try again next frame rather than freeze until a resize. */
                w->bufferW = w->bufferH = 0;
                w->resizePending = true;
                return;
            }
        }
    }
    if (w->targetView == NULL) {
        return;
    }
    w->lastFrame = GetTickCount64();
    /* A covered window still costs a frame; ask first and skip it. */
    HRESULT hr = w->swapChain->Present(0, DXGI_PRESENT_TEST);
    if (deviceLost(hr)) {
        onDeviceLost();
        return;
    }
    /* A button held when it was covered is let go only by a frame: the release waits in the queue. */
    bool occluded = hr == DXGI_STATUS_OCCLUDED;
    if (occluded && !mouseHeld(w)) {
        return;
    }

    /* A modal loop inside another window's draw may have brought us here. */
    ImGuiContext* prev = ImGui::GetCurrentContext();
    bool lost = false;
    w->inFrame = true;
    ImGui::SetCurrentContext(w->ctx);
    if (w->themeDirty) {
        buildBaseStyle(w);
        applyDpi(w, w->dpiScale);
    }
    else if (w->zoom != w->appliedZoom) {
        applyDpi(w, w->dpiScale);
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##imguiWindow", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (w->desc.draw) {
        w->desc.draw(w, w->desc.ref);
    }
    if (w->destroyed) {
        /* The HWND went inside a modal loop in draw, e.g. with its
        ** fullscreen owner; the context could not go before now. */
        ImGui::SetCurrentContext(w->ctx);
        ImGui::End();
        ImGui::EndFrame();
        w->inFrame = false;
        restoreContext(prev);
        destroyInstance(w);
        return;
    }
    /* Another window's frame inside a modal loop may have switched contexts. */
    ImGui::SetCurrentContext(w->ctx);
    ImGui::End();
    ImGui::Render();

    /* A resize inside draw may have failed to make a new target. */
    if (w->targetView && hasDevice(w)) {
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = { bg.x, bg.y, bg.z, 1.0f };
        deviceContext->OMSetRenderTargets(1, &w->targetView, NULL);
        deviceContext->ClearRenderTargetView(w->targetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        /* Never block: this thread also presents the emulator's window.
        ** A frame refused because presenting would block is redrawn next tick. */
        lost = deviceLost(w->swapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT));
    }
    w->inFrame = false;
    restoreContext(prev);
    if (lost) {
        onDeviceLost();
    }
    else if (w->closePending) {
        closeWindow(w);
    }
}

/* A frame unless the last one was too recent; the timer catches up. While
** the window is being sized, fewer: each frame there resizes the buffers. */
static void drawSoon(ImGuiToolWindow* w)
{
    if (GetTickCount64() - w->lastFrame >= (w->sizing ? SIZING_MIN_MS : FRAME_MIN_MS)) {
        renderFrame(w);
    }
}

/* Inside its own frame only marks it; the frame closes it at its end. */
static void closeWindow(ImGuiToolWindow* w)
{
    if (w->destroyed || w->closing) {
        return;
    }
    if (w->inFrame) {
        w->closePending = true;
        return;
    }
    w->closing = true;
    DestroyWindow(w->hwnd);
}

/* The HWND is going: free what belongs to it, and the rest unless a frame
** of this window is still running further up the stack. */
static void onDestroy(ImGuiToolWindow* w)
{
    if (w->sizing) {
        w->sizing = false;
        exitDialogShow();
    }
    KillTimer(w->hwnd, FRAME_TIMER_ID);
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, 0);
    releaseSwapChain(w);
    w->hwnd = NULL;
    w->destroyed = true;
    if (!w->inFrame) {
        destroyInstance(w);
    }
}

/* Makes the window's context current for one message and puts back the one
** that was current, which may belong to a window drawing further up. */
struct ContextScope {
    ImGuiContext* prev;
    explicit ContextScope(ImGuiContext* ctx) : prev(ImGui::GetCurrentContext()) {
        if (ctx) ImGui::SetCurrentContext(ctx);
    }
    ~ContextScope() {
        restoreContext(prev);
    }
};

static LRESULT CALLBACK wndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(wnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
    }
    ImGuiToolWindow* w = (ImGuiToolWindow*)GetWindowLongPtrW(wnd, GWLP_USERDATA);
    if (w == NULL) {
        return DefWindowProcW(wnd, msg, wParam, lParam);
    }
    if (zoomMessage(w, msg, wParam)) {
        return 0;
    }
    ContextScope scope(w->ctx);
    if (w->ctx && ImGui_ImplWin32_WndProcHandler(wnd, msg, wParam, lParam)) {
        return 1;
    }

    switch (msg) {
    case WM_MOVE:
        if (w->sizing) archPumpEmuDisplay();
        break;

    case WM_SIZE:
        if (w->sizing) archPumpEmuDisplay();
        if (w->swapChain && wParam != SIZE_MINIMIZED) {
            /* Draw soon: until then the old frame shows stretched. */
            w->resizePending = true;
            drawSoon(w);
        }
        return 0;

    case WM_PAINT:
        drawSoon(w);
        ValidateRect(wnd, NULL);
        return 0;

    /* The size loop keeps this thread from acking the emulator's frames, so let it
    ** run unsynchronised meanwhile, as a modal dialog does. The main window's timer
    ** starves behind the drag, so each move, size and frame tick here draws its frame. */
    case WM_ENTERSIZEMOVE:
        if (!w->sizing) {
            w->sizing = true;
            enterModalLoop();
        }
        break;

    case WM_EXITSIZEMOVE:
        if (w->sizing) {
            w->sizing = false;
            exitDialogShow();
        }
        renderFrame(w);
        break;

    case WM_DPICHANGED: {
        const RECT* r = (const RECT*)lParam;
        if (w->ctx && w->inFrame) {
            /* Inside a frame the style is in use: the next frame rebuilds it. */
            w->dpiScale = HIWORD(wParam) / 96.0f;
            w->themeDirty = true;
        }
        else if (w->ctx) {
            applyDpi(w, HIWORD(wParam) / 96.0f);
        }
        SetWindowPos(wnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_TIMER:
        if (wParam == FRAME_TIMER_ID) {
            if (w->sizing) archPumpEmuDisplay();
            drawSoon(w);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    /* The main window may see this after us, so drop the cached theme here too. */
    case WM_SETTINGCHANGE:
        if (lParam && lstrcmpW((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) {
            win32InvalidateDarkModeCache();
            win32CommonApplyDarkTitle(wnd);
            if (!(w->desc.flags & IMGUI_TOOL_ALWAYS_DARK)) {
                w->themeDirty = true;
            }
        }
        break;

    case WM_CLOSE:
        closeWindow(w);
        return 0;

    case WM_DESTROY:
        onDestroy(w);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wParam, lParam);
}

/* ---- public ----------------------------------------------------------- */

static ImGuiToolWindow* findOpen(const char* id)
{
    ImGuiToolWindow* w;
    char key[sizeof(w->id)];
    lstrcpynA(key, id, sizeof(key));
    for (w = windows; w; w = w->next) {
        if (!w->destroyed && !w->closing && strcmp(w->id, key) == 0) {
            return w;
        }
    }
    return NULL;
}

extern "C" int imguiToolIsOpen(const char* id)
{
    return id != NULL && findOpen(id) != NULL;
}

extern "C" void imguiToolClose(ImGuiToolWindow* w)
{
    if (w) {
        closeWindow(w);
    }
}

extern "C" void imguiToolCloseAll(void)
{
    ImGuiToolWindow* w;
    for (;;) {
        for (w = windows; w; w = w->next) {
            if (!w->destroyed && !w->closing && !w->closePending) {
                break;
            }
        }
        if (w == NULL) {
            return;
        }
        closeWindow(w);
    }
}

extern "C" float imguiToolScale(ImGuiToolWindow* w)
{
    return w ? w->dpiScale * w->zoom : 1.0f;
}

ID3D11Device* imguiHostDevice()
{
    return device;
}

ID3D11DeviceContext* imguiHostContext()
{
    return deviceContext;
}

unsigned imguiHostDeviceGeneration()
{
    return deviceGeneration;
}

ImGuiToolWindow* imguiHostCurrentWindow()
{
    return ImGui::GetCurrentContext() ? (ImGuiToolWindow*)ImGui::GetIO().UserData : NULL;
}

bool imguiHostIsDark(ImGuiToolWindow* w)
{
    return w ? w->dark : imguiThemeIsDark();
}

const UiTokens* imguiHostTokens(ImGuiToolWindow* w)
{
    return w && !(w->desc.flags & IMGUI_TOOL_ALWAYS_DARK) ? &w->tokens : NULL;
}

ImFont* imguiHostFont(ImGuiToolWindow* w, UiFont role)
{
    return w && role >= 0 && role < UI_FONT_COUNT ? w->fonts[role] : NULL;
}

float imguiHostFontSize(ImGuiToolWindow* w, UiFont role, float em)
{
    return em * (w && role >= 0 && role < UI_FONT_COUNT ? w->fontEm[role] : 1.0f);
}

static bool registerClass()
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = wndProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = (HICON)GetClassLongPtrW(getMainHwnd(), GCLP_HICON);
        wc.hIconSm       = (HICON)GetClassLongPtrW(getMainHwnd(), GCLP_HICONSM);
        /* Fullscreen finds the tool windows by this name to own them. */
        wc.lpszClassName = L"blueMSXImGuiWindow";
        registered = RegisterClassExW(&wc) != 0;
    }
    return registered;
}

/* Themed windows draw their popups with a shadow and an edge; the dark stock style keeps ImGui's. */
static void decoratePopups(ImGuiContext* ctx, ImGuiContextHook* hook)
{
    ImGuiToolWindow* w = (ImGuiToolWindow*)hook->UserData;
    if (!(w->desc.flags & IMGUI_TOOL_ALWAYS_DARK)) imguiThemeDecoratePopups(ctx, w->dark, w->dpiScale * w->appliedZoom);
}

/* Undoes a window that failed to open, without telling its owner. */
static void abandonOpen(ImGuiToolWindow* w, HWND wnd)
{
    if (wnd) {
        SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        DestroyWindow(wnd);
    }
    w->hwnd = NULL;
    w->destroyed = true;
    w->desc.closed = NULL;
    destroyInstance(w);
}

extern "C" ImGuiToolWindow* imguiToolOpen(const ImGuiToolDesc* desc)
{
    ImGuiToolWindow* w;
    int openCount = 0;

    if (desc == NULL || desc->id == NULL) {
        return NULL;
    }
    w = findOpen(desc->id);
    if (w) {
        if (IsIconic(w->hwnd)) ShowWindow(w->hwnd, SW_RESTORE);
        SetForegroundWindow(w->hwnd);
        return w;
    }
    if (!registerClass()) {
        return NULL;
    }
    for (w = windows; w; w = w->next) {
        openCount += !w->destroyed;
    }

    w = (ImGuiToolWindow*)calloc(1, sizeof(ImGuiToolWindow));
    if (w == NULL) {
        return NULL;
    }
    w->desc = *desc;
    lstrcpynA(w->id, desc->id, sizeof(w->id));
    lstrcpynA(w->title, desc->title ? desc->title : "", sizeof(w->title));
    w->desc.id    = w->id;
    w->desc.title = w->title;
    w->dpiScale = w->zoom = w->appliedZoom = 1.0f;
    if (zoomSlot(w->id)) {
        w->zoom = *zoomSlot(w->id);
    }
    w->zoomSetW = w->zoomSetH = -1;
    w->next = windows;
    windows = w;
    liveCount++;

    if (device == NULL && !createDevice()) {
        abandonOpen(w, NULL);
        return NULL;
    }
    w->generation = deviceGeneration;

    wchar_t wtitle[256];
    if (MultiByteToWideChar(CP_UTF8, 0, w->title, -1, wtitle, 256) == 0) {
        wtitle[0] = 0;
    }

    /* Open over the main window so the size matches its monitor's DPI;
    ** WM_DPICHANGED follows later moves. Further windows step down and right. */
    typedef BOOL (WINAPI *AdjustForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static AdjustForDpiFn adjustForDpi = (AdjustForDpiFn)
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    HWND mainWnd = getMainHwnd();
    float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(mainWnd);
    RECT r = { 0, 0, (LONG)(desc->width * scale * w->zoom), (LONG)(desc->height * scale * w->zoom) };
    if (adjustForDpi) {
        adjustForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)(scale * 96));
    }
    else {
        AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    }
    RECT mainRect = { 0, 0, 0, 0 };
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (mainWnd && GetWindowRect(mainWnd, &mainRect)) {
        x = mainRect.left + (int)((40 + 32 * openCount) * scale);
        y = mainRect.top  + (int)((40 + 32 * openCount) * scale);
    }
    /* Unowned like the themed tool windows, so the main window can come to
    ** the front; fullscreen gives it the main window as owner. */
    HWND wnd = CreateWindowExW(0, L"blueMSXImGuiWindow", wtitle, WS_OVERLAPPEDWINDOW,
                               x, y, r.right - r.left, r.bottom - r.top,
                               NULL, NULL, GetModuleHandleW(NULL), w);
    if (wnd == NULL) {
        abandonOpen(w, NULL);
        return NULL;
    }
    w->hwnd = wnd;
    win32CommonApplyDarkTitle(wnd);
    archWindowApplyOwnership(wnd);
    if (!createSwapChain(w)) {
        abandonOpen(w, wnd);
        return NULL;
    }

    ImGuiContext* prev = ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    w->ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(w->ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.UserData = w;
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_RenderPre;
    hook.Callback = decoratePopups;
    hook.UserData = w;
    ImGui::AddContextHook(w->ctx, &hook);
    if (w->desc.flags & IMGUI_TOOL_ALWAYS_DARK) {
        buildBaseStyle(w);
        loadFonts();
        for (int i = 0; i < UI_FONT_COUNT; i++) {
            w->fonts[i] = io.Fonts->Fonts[0];
            w->fontEm[i] = 1.0f;
        }
    }
    else {
        /* The style's control height follows the body font's. */
        loadThemedFonts(w);
        buildBaseStyle(w);
    }
    ImGui_ImplWin32_Init(wnd);
    ImGui_ImplDX11_Init(device, deviceContext);
    applyDpi(w, ImGui_ImplWin32_GetDpiScaleForHwnd(wnd));
    restoreContext(prev);

    SetTimer(wnd, FRAME_TIMER_ID, USER_TIMER_MINIMUM, NULL);
    ShowWindow(wnd, SW_SHOW);
    renderFrame(w);
    return findOpen(desc->id);
}
