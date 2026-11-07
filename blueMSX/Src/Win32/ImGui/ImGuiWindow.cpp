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
#include "Properties.h"
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

/* All that decides how a tooltip outside the window looks. */
struct TipLook {
    char    text[512];
    ImFont* font;
    float   size, scale;
    ImU32   bg, fg;
    bool    dark;
};

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
    bool                    lostDevice;    /* closed by a lost device, not by the user */
    float                   dpiScale;
    float                   zoom;
    float                   appliedZoom;
    float                   zoomWantW, zoomWantH;
    LONG                    zoomSetW, zoomSetH;
    bool                    wheelTaken, wheelTakenNext;  /* a view takes Ctrl+wheel where the mouse is */
    bool                    placing;       /* opened: moves and sizes go to desc.placement */
    int                     fitW, fitH;    /* FIT_CONTENT: client size the last frame asked for */
    bool                    hiddenWithMain; /* NO_ACTIVATE: hidden while the main window is minimised */
    ULONGLONG               lastFrame;
    /* TIPS_OUTSIDE: the tooltip window, what this frame asked it to show, what the copy
    ** read back from the GPU holds and what is on screen. */
    HWND                    tipHwnd;
    bool                    tipActive;
    bool                    tipWanted;
    TipLook                 tipWant, tipPend, tipShown;
    ImVec2                  tipMin, tipMax;
    ImDrawList*             tipList;       /* this frame's bubble, rendered and freed in the frame */
    bool                    tipPending;    /* tipStaging holds tipPend, not read yet */
    int                     tipW, tipH;    /* bitmap, bubble and shadow */
    ImVec2                  tipBubbleMin, tipBubbleSize;
    ID3D11Texture2D*        tipTex;
    ID3D11RenderTargetView* tipTarget;
    ID3D11Texture2D*        tipStaging;
    int                     tipTexW, tipTexH;
    HDC                     tipDC;
    HBITMAP                 tipBmp, tipOldBmp;
    void*                   tipBits;
    int                     tipBmpW, tipBmpH;
    ImGuiToolWindow*        next;
};

/* The window whose draw is running, for widgets that ask the host. */
static ImGuiToolWindow*     drawing;

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
static FontFile fontBody, fontStrong, fontMono, fontJpBody, fontIcon;
static int      fontJpBodyNo, fontJpStrongNo;
static char     fontMonoName[40], fontJpBodyName[40], fontIconName[40];
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
    /* Windows 10 has the same glyphs at the same code points in Segoe MDL2 Assets. */
    lstrcpyA(fontIconName, "SegoeIcons.ttf");
    wsprintfA(path, "%s\\Fonts\\SegoeIcons.ttf", dir);
    readFontFile(&fontIcon, path);
    if (fontIcon.data == NULL) {
        lstrcpyA(fontIconName, "segmdl2.ttf");
        wsprintfA(path, "%s\\Fonts\\segmdl2.ttf", dir);
        readFontFile(&fontIcon, path);
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
    freeFontFile(&fontIcon);
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
    /* Its own font, so no text font answers for its private-use glyphs. */
    static const FontFile none = { NULL, 0 };
    w->fontEm[UI_FONT_ICON] = emRatio(&fontIcon, 1.0f);
    w->fonts[UI_FONT_ICON]  = addThemedFont(&fontIcon, fontIconName, 16 * w->fontEm[UI_FONT_ICON], &none, 0, "");
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
        if (w->desc.placement) {
            w->desc.placement->zoomPct = (int)(w->zoom * 100 + 0.5f);
        }
        /* A fitted window is sized by its next frame instead. */
        if (!(w->desc.flags & IMGUI_TOOL_FIT_CONTENT)) {
            resizeClientBy(w, w->zoom / old);
        }
    }
}

/* The zoom step nearest pct percent; 100% when pct is out of the steps' range. */
static float zoomFromPct(int pct)
{
    float z = pct / 100.0f, best = 1.0f;
    int i;
    if (pct < 50 || pct > 250) {
        return 1.0f;
    }
    for (i = 0; i < ZOOM_STEPS; i++) {
        if (fabsf(zoomSteps[i] - z) < fabsf(best - z)) best = zoomSteps[i];
    }
    return best;
}

/* The window's normal place, not while minimised or maximised: the frame's
** top-left in screen px, and the client in px at its DPI. */
static void notePlacement(ImGuiToolWindow* w)
{
    ToolWindowPlacement* p = w->desc.placement;
    RECT outer, client;
    if (p == NULL || !w->placing || w->hwnd == NULL || IsZoomed(w->hwnd) || IsIconic(w->hwnd) ||
        !GetWindowRect(w->hwnd, &outer) || !GetClientRect(w->hwnd, &client)) {
        return;
    }
    p->x = outer.left;
    p->y = outer.top;
    p->width = client.right;
    p->height = client.bottom;
    p->dpi = (int)(w->dpiScale * 96 + 0.5f);
}

/* Ctrl+wheel (but over a view that zooms with it), Ctrl+plus, Ctrl+minus and
** Ctrl+0 zoom like the debugger's views. Returns true when the message was used. */
static bool zoomMessage(ImGuiToolWindow* w, UINT msg, WPARAM wParam)
{
    if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) && !w->wheelTaken) {
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

static DWORD windowStyle(int flags)
{
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (flags & IMGUI_TOOL_FIT_CONTENT) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    if (flags & IMGUI_TOOL_NO_ACTIVATE) style &= ~WS_MINIMIZEBOX;
    return style;
}

static DWORD windowExStyle(int flags)
{
    return (flags & IMGUI_TOOL_NO_ACTIVATE) ? WS_EX_NOACTIVATE : 0;
}

/* The content the host window got this frame, measured as ImGui measures
** an auto-fitting window but without clamping it to the current client. */
static void measureContent(ImGuiToolWindow* w)
{
    ImGuiWindow* host = ImGui::GetCurrentWindow();
    ImVec2 start = host->DC.CursorStartPos;
    float cw = ImMax(host->DC.CursorMaxPos.x, host->DC.IdealMaxPos.x) - start.x;
    float ch = ImMax(host->DC.CursorMaxPos.y, host->DC.IdealMaxPos.y) - start.y;
    w->fitW = (int)ceilf(cw + host->WindowPadding.x * 2);
    w->fitH = (int)ceilf(ch + host->WindowPadding.y * 2);
}

/* Outside the frame: the WM_SIZE it sends marks the buffers, and the next timer tick draws. */
static void fitClient(ImGuiToolWindow* w)
{
    RECT outer, client;
    if (w->fitW <= 0 || w->fitH <= 0 || w->destroyed || IsIconic(w->hwnd) ||
        !GetWindowRect(w->hwnd, &outer) || !GetClientRect(w->hwnd, &client) ||
        (client.right == w->fitW && client.bottom == w->fitH)) {
        return;
    }
    SetWindowPos(w->hwnd, NULL, 0, 0,
                 w->fitW + (outer.right - outer.left) - client.right,
                 w->fitH + (outer.bottom - outer.top) - client.bottom,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* The mouse-up of a button held when capture went elsewhere never comes. */
static void releaseHeldButtons(ImGuiToolWindow* w)
{
    if (w->ctx == NULL) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    for (int b = 0; b < ImGuiMouseButton_COUNT; b++) {
        io.AddMouseButtonEvent(b, false);
    }
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

static void releaseTip(ImGuiToolWindow* w);

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
    releaseTip(w);
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
        w->lostDevice = true;
        closeWindow(w);
    }
}

static bool sameLook(const TipLook& a, const TipLook& b)
{
    return a.font == b.font && a.size == b.size && a.scale == b.scale && a.bg == b.bg && a.fg == b.fg && a.dark == b.dark &&
           strcmp(a.text, b.text) == 0;
}

/* Also drops a copy still on its way back from the GPU, so it cannot show after this. */
static void hideTip(ImGuiToolWindow* w)
{
    w->tipPending = false;
    w->tipShown.text[0] = '\0';
    if (w->tipActive) {
        ShowWindow(w->tipHwnd, SW_HIDE);
        w->tipActive = false;
    }
}

static void releaseTip(ImGuiToolWindow* w)
{
    if (w->tipTarget)  { w->tipTarget->Release();  w->tipTarget = NULL; }
    if (w->tipTex)     { w->tipTex->Release();     w->tipTex = NULL; }
    if (w->tipStaging) { w->tipStaging->Release(); w->tipStaging = NULL; }
    if (w->tipDC) {
        if (w->tipOldBmp) SelectObject(w->tipDC, w->tipOldBmp);
        DeleteDC(w->tipDC);
        w->tipDC = NULL;
    }
    if (w->tipBmp) { DeleteObject(w->tipBmp); w->tipBmp = NULL; }
    w->tipOldBmp = NULL;
    w->tipBits = NULL;
    w->tipBmpW = w->tipBmpH = w->tipTexW = w->tipTexH = 0;
}

static LRESULT CALLBACK tipProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(wnd, msg, wParam, lParam);
}

static HWND createTipWindow(HWND owner)
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = tipProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"blueMSXImGuiTip";
        registered = RegisterClassExW(&wc) != 0;
        if (!registered) {
            return NULL;
        }
    }
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                           L"blueMSXImGuiTip", NULL, WS_POPUP, 0, 0, 0, 0, owner, NULL, GetModuleHandleW(NULL), NULL);
}

/* Screen y just below the visible part of the mouse pointer, or LONG_MIN when none shows. */
static LONG cursorBottom()
{
    CURSORINFO ci = {};
    ICONINFO   ii = {};
    BITMAP     bm = {};
    LONG       bottom = LONG_MIN;
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || ci.hCursor == NULL || !GetIconInfo(ci.hCursor, &ii)) {
        return LONG_MIN;
    }
    HBITMAP src = ii.hbmColor ? ii.hbmColor : ii.hbmMask;
    if (src && GetObject(src, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0 && bm.bmWidth <= 256 && bm.bmHeight <= 512) {
        /* A colour pointer shows where its alpha is set, or where its mask is black when it has
        ** no alpha; a monochrome one keeps the mask over the inverting half, twice as tall. */
        int w = bm.bmWidth, h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
        unsigned* mask = (unsigned*)malloc((size_t)w * h * 2 * 4);
        unsigned* color = ii.hbmColor ? (unsigned*)malloc((size_t)w * h * 4) : NULL;
        HDC dc = GetDC(NULL);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        bool ok = mask != NULL && (ii.hbmColor == NULL || color != NULL);
        if (ok) {
            bi.bmiHeader.biHeight = -(ii.hbmColor ? h : h * 2);
            ok = GetDIBits(dc, ii.hbmMask, 0, ii.hbmColor ? h : h * 2, mask, &bi, DIB_RGB_COLORS) > 0;
        }
        bool alpha = false;
        if (ok && color) {
            bi.bmiHeader.biHeight = -h;
            ok = GetDIBits(dc, ii.hbmColor, 0, h, color, &bi, DIB_RGB_COLORS) > 0;
            for (int i = 0; ok && i < w * h && !alpha; i++) alpha = (color[i] >> 24) != 0;
        }
        ReleaseDC(NULL, dc);
        for (int y = h - 1; ok && y >= 0 && bottom == LONG_MIN; y--) {
            for (int x = 0; x < w; x++) {
                int  i = y * w + x;
                bool seen = alpha ? (color[i] >> 24) != 0
                                  : (mask[i] & 0xffffff) == 0 || (!color && (mask[i + w * h] & 0xffffff) != 0);
                if (seen) {
                    bottom = ci.ptScreenPos.y - (LONG)ii.yHotspot + y + 1;
                    break;
                }
            }
        }
        free(mask);
        free(color);
    }
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    return bottom;
}

/* Below the item and the pointer, or above the window where the screen ends; it stays
** there until the text changes or it hides. */
static POINT placeTip(ImGuiToolWindow* w, float scale)
{
    RECT  wr = {};
    POINT pt = { (LONG)w->tipMin.x, (LONG)w->tipMax.y };
    LONG  gap = (LONG)(4 * scale), bw = (LONG)w->tipBubbleSize.x, bh = (LONG)w->tipBubbleSize.y;
    ClientToScreen(w->hwnd, &pt);
    GetWindowRect(w->hwnd, &wr);
    LONG below = cursorBottom();
    pt.y = (below != LONG_MIN && below > pt.y ? below : pt.y) + gap;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (pt.x + bw > mi.rcWork.right) pt.x = mi.rcWork.right - bw;
        if (pt.x < mi.rcWork.left) pt.x = mi.rcWork.left;
        if (pt.y + bh > mi.rcWork.bottom) pt.y = wr.top - gap - bh;
    }
    pt.x -= (LONG)w->tipBubbleMin.x;
    pt.y -= (LONG)w->tipBubbleMin.y;
    return pt;
}

/* Inside the frame, before Render: lays out what draw asked for, the way ImGui lays out
** its own tooltip, into a list rendered once the window's frame is drawn. Hidden once the
** mouse is off the window or no item asked this frame. */
static void updateTip(ImGuiToolWindow* w)
{
    POINT cur;
    /* Another window over this one under the mouse hides it too. */
    if (!w->tipWanted || w->hwnd == NULL || !GetCursorPos(&cur) || WindowFromPoint(cur) != w->hwnd) {
        hideTip(w);
        return;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    TipLook& want = w->tipWant;
    want.scale = w->dpiScale * w->appliedZoom;
    want.bg = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_PopupBg]);
    want.fg = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);
    want.dark = w->dark;
    if ((w->tipActive && sameLook(want, w->tipShown)) || (w->tipPending && sameLook(want, w->tipPend))) {
        return;
    }
    if (w->tipHwnd == NULL) {
        w->tipHwnd = createTipWindow(w->hwnd);
        if (w->tipHwnd == NULL) {
            return;
        }
    }
    float depth = 0, drop = 0;
    bool  framed = !(w->desc.flags & IMGUI_TOOL_ALWAYS_DARK);
    if (framed) {
        imguiThemePopupShadow(want.scale, &depth, &drop);
    }
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    float room = GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &mi)
                     ? (float)(mi.rcWork.right - mi.rcWork.left) - style.WindowPadding.x * 2 - depth * 2 : FLT_MAX;
    ImVec2 text = want.font->CalcTextSizeA(want.size, FLT_MAX, 0.0f, want.text);
    float  wrap = text.x > room && room > want.size ? room : 0.0f;
    if (wrap > 0) text = want.font->CalcTextSizeA(want.size, FLT_MAX, wrap, want.text);
    ImVec2 size(ceilf(text.x) + style.WindowPadding.x * 2, ceilf(text.y) + style.WindowPadding.y * 2);
    float least = fmaxf(4.0f, style.WindowRounding - 1);  /* an auto-sized window's least size */
    size = ImVec2(fmaxf(size.x, least), fmaxf(size.y, least));
    w->tipBubbleMin  = ImVec2(depth + 1, fmaxf(depth - drop, 0) + 1);
    w->tipBubbleSize = ImVec2(floorf(size.x), floorf(size.y));
    w->tipW = (int)(w->tipBubbleMin.x + w->tipBubbleSize.x + depth + 1);
    w->tipH = (int)(w->tipBubbleMin.y + w->tipBubbleSize.y + depth + drop + 1);

    ImVec2 a = w->tipBubbleMin, b(a.x + w->tipBubbleSize.x, a.y + w->tipBubbleSize.y);
    ImDrawList* dl = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
    dl->_ResetForNewFrame();
    dl->_SetPixelDensity(1.0f);
    dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
    dl->PushClipRect(ImVec2(0, 0), ImVec2((float)w->tipW, (float)w->tipH), false);
    dl->AddRectFilled(a, b, want.bg, style.WindowRounding);
    dl->AddText(want.font, want.size, ImVec2(floorf(a.x + style.WindowPadding.x), floorf(a.y + style.WindowPadding.y)), want.fg, want.text, NULL, wrap);
    if (framed) {
        imguiThemeDrawPopupFrame(dl, a, b, style.WindowRounding, w->dark, want.scale);
    }
    w->tipList = dl;
    w->tipPend = want;
    w->tipPending = false;
}

static bool makeTipTarget(ImGuiToolWindow* w)
{
    if (w->tipTex && w->tipTexW == w->tipW && w->tipTexH == w->tipH) {
        return true;
    }
    if (w->tipTarget)  { w->tipTarget->Release();  w->tipTarget = NULL; }
    if (w->tipTex)     { w->tipTex->Release();     w->tipTex = NULL; }
    if (w->tipStaging) { w->tipStaging->Release(); w->tipStaging = NULL; }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w->tipW;
    td.Height = (UINT)w->tipH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device->CreateTexture2D(&td, NULL, &w->tipTex)) ||
        FAILED(device->CreateRenderTargetView(w->tipTex, NULL, &w->tipTarget))) {
        return false;
    }
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&td, NULL, &w->tipStaging))) {
        return false;
    }
    w->tipTexW = w->tipW;
    w->tipTexH = w->tipH;
    return true;
}

/* After the window's own draw data, with its context current: the bubble goes to a
** texture cleared to nothing, which comes out with premultiplied alpha. */
static void renderTip(ImGuiToolWindow* w)
{
    if (!makeTipTarget(w)) {
        w->tipTexW = w->tipTexH = 0;
        hideTip(w);
        return;
    }
    const float clear[4] = { 0, 0, 0, 0 };
    deviceContext->OMSetRenderTargets(1, &w->tipTarget, NULL);
    deviceContext->ClearRenderTargetView(w->tipTarget, clear);
    ImDrawData data;
    data.Valid = true;
    data.DisplayPos = ImVec2(0, 0);
    data.DisplaySize = ImVec2((float)w->tipW, (float)w->tipH);
    data.FramebufferScale = ImVec2(1, 1);
    data.AddDrawList(w->tipList);
    ImGui_ImplDX11_RenderDrawData(&data);
    deviceContext->OMSetRenderTargets(0, NULL, NULL);
    deviceContext->CopyResource(w->tipStaging, w->tipTex);
    w->tipPending = true;
}

static bool makeTipBitmap(ImGuiToolWindow* w)
{
    if (w->tipBits && w->tipBmpW == w->tipW && w->tipBmpH == w->tipH) {
        return true;
    }
    if (w->tipDC == NULL && (w->tipDC = CreateCompatibleDC(NULL)) == NULL) {
        return false;
    }
    if (w->tipOldBmp) SelectObject(w->tipDC, w->tipOldBmp);
    if (w->tipBmp) DeleteObject(w->tipBmp);
    w->tipOldBmp = NULL;
    w->tipBits = NULL;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w->tipW;
    bi.bmiHeader.biHeight = -w->tipH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    w->tipBmp = CreateDIBSection(w->tipDC, &bi, DIB_RGB_COLORS, &w->tipBits, NULL, 0);
    if (w->tipBmp == NULL) {
        w->tipBits = NULL;
        return false;
    }
    w->tipOldBmp = (HBITMAP)SelectObject(w->tipDC, w->tipBmp);
    w->tipBmpW = w->tipW;
    w->tipBmpH = w->tipH;
    return true;
}

/* After Present, which sent the copy on its way. Never waits: a copy not back yet is
** tried again at the next frame. The new picture and place replace the old at once. */
static void showTip(ImGuiToolWindow* w)
{
    D3D11_MAPPED_SUBRESOURCE m;
    HRESULT hr = deviceContext->Map(w->tipStaging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
        return;
    }
    w->tipPending = false;
    if (FAILED(hr)) {
        hideTip(w);
        return;
    }
    bool ok = makeTipBitmap(w);
    for (int y = 0; ok && y < w->tipH; y++) {
        const unsigned* s = (const unsigned*)((const char*)m.pData + (size_t)y * m.RowPitch);
        unsigned*       d = (unsigned*)w->tipBits + (size_t)y * w->tipW;
        for (int x = 0; x < w->tipW; x++) {
            unsigned c = s[x];
            d[x] = (c & 0xff00ff00) | ((c & 0xff) << 16) | ((c >> 16) & 0xff);
        }
    }
    deviceContext->Unmap(w->tipStaging, 0);
    if (!ok) {
        hideTip(w);
        return;
    }
    if (!w->tipWanted || !sameLook(w->tipPend, w->tipWant)) {
        return;
    }
    POINT pos = placeTip(w, w->tipPend.scale), src = { 0, 0 };
    SIZE  size = { w->tipW, w->tipH };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!UpdateLayeredWindow(w->tipHwnd, NULL, &pos, &size, w->tipDC, &src, 0, &blend, ULW_ALPHA)) {
        hideTip(w);
        return;
    }
    if (!w->tipActive) {
        SetWindowPos(w->tipHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        w->tipActive = true;
    }
    w->tipShown = w->tipPend;
}

extern "C" void imguiToolHideTip(void)
{
    if (drawing != NULL) {
        hideTip(drawing);
    }
}

extern "C" int imguiToolTipOutside(const char* text)
{
    ImGuiToolWindow* w = drawing;
    if (w == NULL || !(w->desc.flags & IMGUI_TOOL_TIPS_OUTSIDE) || text == NULL || !*text) {
        return 0;
    }
    /* The font of the item, as ImGui's own tooltip would take it. */
    w->tipWanted = true;
    snprintf(w->tipWant.text, sizeof(w->tipWant.text), "%s", text);
    w->tipWant.font = ImGui::GetFont();
    w->tipWant.size = ImGui::GetFontSize();
    w->tipMin = ImGui::GetItemRectMin();
    w->tipMax = ImGui::GetItemRectMax();
    return 1;
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
        /* No frame means no item asks for the tooltip; inside one it stays. */
        if (!w->inFrame) {
            hideTip(w);
        }
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
            HRESULT hr = w->swapChain->ResizeBuffers(0, bw, bh, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr)) {
                hideTip(w);
                if (deviceLost(hr)) onDeviceLost();
                else closeWindow(w);
                return;
            }
            w->bufferW = bw;
            w->bufferH = bh;
            createTargetView(w);
            if (w->targetView == NULL) {
                /* Try again next frame rather than freeze until a resize. */
                w->bufferW = w->bufferH = 0;
                w->resizePending = true;
                hideTip(w);
                return;
            }
        }
    }
    if (w->targetView == NULL) {
        hideTip(w);
        return;
    }
    w->lastFrame = GetTickCount64();
    /* A covered window still costs a frame; ask first and skip it. */
    HRESULT hr = w->swapChain->Present(0, DXGI_PRESENT_TEST);
    if (deviceLost(hr)) {
        hideTip(w);
        onDeviceLost();
        return;
    }
    /* A button held when it was covered is let go only by a frame: the release waits in the queue. */
    bool occluded = hr == DXGI_STATUS_OCCLUDED;
    if (occluded) {
        hideTip(w);
        if (!mouseHeld(w)) return;
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
    w->wheelTakenNext = false;
    w->tipWanted = false;
    ImGuiToolWindow* prevDrawing = drawing;
    drawing = w;
    if (w->desc.draw) {
        w->desc.draw(w, w->desc.ref);
    }
    drawing = prevDrawing;
    w->wheelTaken = w->wheelTakenNext;
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
    updateTip(w);
    if (w->desc.flags & IMGUI_TOOL_FIT_CONTENT) {
        measureContent(w);
    }
    ImGui::End();
    ImGui::Render();

    /* A resize inside draw may have failed to make a new target. */
    if (w->targetView && hasDevice(w)) {
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = { bg.x, bg.y, bg.z, 1.0f };
        deviceContext->OMSetRenderTargets(1, &w->targetView, NULL);
        deviceContext->ClearRenderTargetView(w->targetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (w->tipList) {
            renderTip(w);
        }
        /* Never block: this thread also presents the emulator's window.
        ** A frame refused because presenting would block is redrawn next tick. */
        lost = deviceLost(w->swapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT));
        /* The tip is a topmost window of its own: it would show over what covers this one. */
        if (!lost && w->tipPending && !occluded) {
            showTip(w);
        }
    }
    /* Its texture references hold for this frame only. */
    if (w->tipList) {
        IM_DELETE(w->tipList);
        w->tipList = NULL;
    }
    w->inFrame = false;
    restoreContext(prev);
    if (lost) {
        onDeviceLost();
    }
    else if (w->closePending) {
        closeWindow(w);
    }
    else if (w->desc.flags & IMGUI_TOOL_FIT_CONTENT) {
        fitClient(w);
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

/* A NO_ACTIVATE window is never activated by a click, so it comes up whenever one of
** this program's own windows does: not over dialogs or owned popups. */
static HWINEVENTHOOK raiseHook;

static void CALLBACK onForeground(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
    wchar_t cls[16];
    HWND mainWnd = getMainHwnd();
    ImGuiToolWindow* w;
    /* The event comes late: by then another program may be in front, and must stay so. */
    if (event != EVENT_SYSTEM_FOREGROUND || idObject != OBJID_WINDOW || hwnd == NULL || !IsWindow(hwnd) ||
        GetForegroundWindow() != hwnd || mainWnd == NULL || !IsWindowEnabled(mainWnd)) {
        return;
    }
    if (GetWindow(hwnd, GW_OWNER) != NULL || (GetClassNameW(hwnd, cls, 16) > 0 && lstrcmpW(cls, L"#32770") == 0)) {
        return;
    }
    for (w = windows; w; w = w->next) {
        if ((w->desc.flags & IMGUI_TOOL_NO_ACTIVATE) && !w->destroyed && !w->closing && !w->hiddenWithMain &&
            w->hwnd != hwnd && IsWindow(w->hwnd) && IsWindowVisible(w->hwnd)) {
            /* HWND_TOP keeps a topmost window topmost, as it is under a fullscreen owner. */
            SetWindowPos(w->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
}

/* Hooked while any NO_ACTIVATE window is open. */
static void updateRaiseHook()
{
    ImGuiToolWindow* w;
    bool wanted = false;
    for (w = windows; w; w = w->next) {
        wanted |= (w->desc.flags & IMGUI_TOOL_NO_ACTIVATE) && !w->destroyed && !w->closing && w->hwnd != NULL;
    }
    if (wanted && raiseHook == NULL) {
        raiseHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, onForeground,
                                    GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    }
    else if (!wanted && raiseHook != NULL) {
        UnhookWinEvent(raiseHook);
        raiseHook = NULL;
    }
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
    /* Owned by this window, the tooltip goes with it. */
    w->tipHwnd = NULL;
    w->tipActive = false;
    w->tipPending = false;
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, 0);
    releaseSwapChain(w);
    w->hwnd = NULL;
    w->destroyed = true;
    updateRaiseHook();
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
        notePlacement(w);
        break;

    case WM_SIZE:
        if (w->sizing) archPumpEmuDisplay();
        notePlacement(w);
        if (w->swapChain && wParam != SIZE_MINIMIZED) {
            /* Draw soon: until then the old frame shows at its own size. */
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

    case WM_MOUSEACTIVATE:
        if (w->desc.flags & IMGUI_TOOL_NO_ACTIVATE) {
            HWND fg = GetForegroundWindow();
            HWND mainWnd = getMainHwnd();
            DWORD pid = 0;
            if (fg != NULL) {
                GetWindowThreadProcessId(fg, &pid);
            }
            /* Another program in front: a click here brings the main window forward, as its
            ** taskbar button would, and this window over it at once, not when the hook runs. */
            if (pid != GetCurrentProcessId() && mainWnd != NULL && IsWindowEnabled(mainWnd) &&
                IsWindowVisible(mainWnd) && !IsIconic(mainWnd)) {
                SetForegroundWindow(mainWnd);
                SetWindowPos(wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
            }
            return MA_NOACTIVATE;
        }
        break;

    case WM_CAPTURECHANGED:
        if ((HWND)lParam != wnd) {
            releaseHeldButtons(w);
        }
        break;

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

extern "C" void imguiToolMainMinimized(int minimized)
{
    ImGuiToolWindow* w;
    for (w = windows; w; w = w->next) {
        if (w->destroyed || w->closing || !(w->desc.flags & IMGUI_TOOL_NO_ACTIVATE)) {
            continue;
        }
        if (minimized && IsWindowVisible(w->hwnd)) {
            w->hiddenWithMain = true;
            hideTip(w);
            ShowWindow(w->hwnd, SW_HIDE);
        }
        else if (!minimized && w->hiddenWithMain) {
            w->hiddenWithMain = false;
            ShowWindow(w->hwnd, SW_SHOWNA);
            SetWindowPos(w->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

extern "C" int imguiToolLostDevice(ImGuiToolWindow* w)
{
    return w != NULL && w->lostDevice;
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

void imguiHostTakeWheel()
{
    ImGuiToolWindow* w = imguiHostCurrentWindow();
    if (w) {
        w->wheelTakenNext = true;
    }
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
        if (w->desc.flags & IMGUI_TOOL_NO_ACTIVATE) {
            if (IsIconic(w->hwnd)) ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(w->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return w;
        }
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
    if (desc->placement) {
        w->zoom = zoomFromPct(desc->placement->zoomPct);
        desc->placement->zoomPct = (int)(w->zoom * 100 + 0.5f);
    }
    else if (zoomSlot(w->id)) {
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
    DWORD style = windowStyle(desc->flags), exStyle = windowExStyle(desc->flags);
    float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(mainWnd);
    RECT r = { 0, 0, (LONG)(desc->width * scale * w->zoom), (LONG)(desc->height * scale * w->zoom) };
    if (adjustForDpi) {
        adjustForDpi(&r, style, FALSE, exStyle, (UINT)(scale * 96));
    }
    else {
        AdjustWindowRectEx(&r, style, FALSE, exStyle);
    }
    RECT mainRect = { 0, 0, 0, 0 };
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (mainWnd && GetWindowRect(mainWnd, &mainRect)) {
        x = mainRect.left + (int)((40 + 32 * openCount) * scale);
        y = mainRect.top  + (int)((40 + 32 * openCount) * scale);
    }
    /* Where it was last, its size scaled to that monitor's DPI; the zoom is already in the size. */
    const ToolWindowPlacement* kept = desc->placement;
    int keptDpi = kept && kept->dpi >= 48 && kept->dpi <= 960 ? kept->dpi : 96;
    if (kept && kept->width > 0 && kept->width <= 16384 && kept->height > 0 && kept->height <= 16384) {
        /* A window shrunk below 320 x 240 keeps its place and comes back that
        ** large; a fitted one was sized by its content, not shrunk. */
        int minW = (desc->flags & IMGUI_TOOL_FIT_CONTENT) ? 1 : 320 * keptDpi / 96;
        int minH = (desc->flags & IMGUI_TOOL_FIT_CONTENT) ? 1 : 240 * keptDpi / 96;
        int keptW = kept->width > minW ? kept->width : minW;
        int keptH = kept->height > minH ? kept->height : minH;
        int kx = kept->x < -100000 ? -100000 : kept->x > 100000 ? 100000 : kept->x;
        int ky = kept->y < -100000 ? -100000 : kept->y > 100000 ? 100000 : kept->y;
        RECT guess = { kx, ky, kx + keptW, ky + keptH };
        HMONITOR mon = MonitorFromRect(&guess, MONITOR_DEFAULTTONEAREST);
        float ms = ImGui_ImplWin32_GetDpiScaleForMonitor(mon), by = ms * 96 / keptDpi;
        RECT k = { 0, 0, (LONG)(keptW * by + 0.5f), (LONG)(keptH * by + 0.5f) };
        if (adjustForDpi) {
            adjustForDpi(&k, style, FALSE, exStyle, (UINT)(ms * 96));
        }
        else {
            AdjustWindowRectEx(&k, style, FALSE, exStyle);
        }
        int kw = k.right - k.left, kh = k.bottom - k.top;
        RECT title = { kx, ky, kx + kw, ky + (LONG)(32 * ms) };
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        /* With its title bar on no monitor, it comes back inside the nearest one's work area;
        ** a fitted window always does, as nobody can size it back into view. */
        if (((desc->flags & IMGUI_TOOL_FIT_CONTENT) || MonitorFromRect(&title, MONITOR_DEFAULTTONULL) == NULL) &&
            GetMonitorInfo(mon, &mi)) {
            kw = kw < mi.rcWork.right - mi.rcWork.left ? kw : mi.rcWork.right - mi.rcWork.left;
            kh = kh < mi.rcWork.bottom - mi.rcWork.top ? kh : mi.rcWork.bottom - mi.rcWork.top;
            x = kx < mi.rcWork.left ? mi.rcWork.left : kx + kw > mi.rcWork.right ? mi.rcWork.right - kw : kx;
            y = ky < mi.rcWork.top ? mi.rcWork.top : ky + kh > mi.rcWork.bottom ? mi.rcWork.bottom - kh : ky;
        }
        else {
            x = kx;
            y = ky;
        }
        r.left = r.top = 0;
        r.right = kw;
        r.bottom = kh;
    }
    /* Unowned, so the main window can come to the front; fullscreen gives
    ** it the main window as owner. */
    HWND wnd = CreateWindowExW(exStyle, L"blueMSXImGuiWindow", wtitle, style,
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
            w->fonts[i] = i == UI_FONT_ICON ? NULL : io.Fonts->Fonts[0];
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
    w->placing = true;
    notePlacement(w);

    updateRaiseHook();
    SetTimer(wnd, FRAME_TIMER_ID, USER_TIMER_MINIMUM, NULL);
    if (!(desc->flags & IMGUI_TOOL_NO_ACTIVATE)) {
        ShowWindow(wnd, SW_SHOW);
    }
    else if (mainWnd && IsIconic(mainWnd)) {
        w->hiddenWithMain = true;
    }
    else {
        ShowWindow(wnd, SW_SHOWNA);
    }
    renderFrame(w);
    return findOpen(desc->id);
}
