/*****************************************************************************
**
** Non-modal pop-up toast for capture / save notifications.
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
/* Layered + topmost + WS_EX_NOACTIVATE so it does not steal focus
** from the running emulator. */
#include "Win32Toast.h"

#include <windows.h>
#include <string>
#include <stdio.h>

extern "C" {
#include "Win32TextUtf8.h"
#include "Language.h"
}

namespace {

constexpr DWORD kTotalDurationMs = 4000;
constexpr DWORD kFadeOutMs       = 600;
constexpr BYTE  kPeakAlpha       = 235;
constexpr int   kPaddingDu       = 12;
constexpr int   kBgRgb           = 0x282828;   // dark background
constexpr int   kFgRgb           = 0xE6E6E6;   // text
constexpr int   kBorderRgb       = 0x404040;
constexpr int   kCornerMargin    = 24;          // px from owner bottom-left
constexpr UINT_PTR kAnimTimerId  = 0xC0FFEE01;
/* Font sizing: linear scaling with anchor height (= emu zoom).  Ref
** 240 px (~ X1 active area).  Grows at kFontGrowth px per zoom step
** beyond X1, capped at kMaxFontPx so x6+ / fullscreen does not
** dominate the screen.  Tuned so X1 ~= 10 px (subtle in tiny window)
** and X6+ ~= 32 px (readable, not overpowering). */
constexpr int    kBaseFontPx     = 10;
constexpr int    kRefAnchorH     = 240;
constexpr double kFontGrowth     = 4.5;
constexpr int    kMaxFontPx      = 32;

const wchar_t* kClassName = L"BlueMSXCaptureToast";

struct ToastState {
    HWND  hwnd       = nullptr;
    HWND  anchorHwnd = nullptr;     // re-queried each timer tick to follow zoom / move
    HFONT hfont      = nullptr;
    int   fontPx     = 0;           // current cached font height in px
    DWORD startTick  = 0;
    int   width      = 0;
    int   height     = 0;
    std::string text;     // UTF-8
};

ToastState g_toast;

LRESULT CALLBACK toastWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void ensureClass()
{
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = toastWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr;        // we paint everything ourselves
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

/* Returns the toast font px for the supplied anchor (emu render hwnd).
** Adds kFontGrowth px per zoom step beyond X1, then clamps to
** kMaxFontPx so x6+ / fullscreen does not dominate. */
int computeFontPx(HWND anchor)
{
    if (!anchor || !IsWindow(anchor)) return kBaseFontPx;
    RECT rc;
    GetClientRect(anchor, &rc);
    int h = rc.bottom - rc.top;
    if (h <= 0) return kBaseFontPx;
    double mult = (double)h / (double)kRefAnchorH;
    if (mult < 1.0) mult = 1.0;
    int px = (int)((double)kBaseFontPx + (mult - 1.0) * kFontGrowth + 0.5);
    if (px > kMaxFontPx) px = kMaxFontPx;
    return px;
}

void ensureFont(int desiredPx = 0)
{
    if (desiredPx <= 0) {
        desiredPx = g_toast.fontPx > 0 ? g_toast.fontPx : kBaseFontPx;
    }
    if (g_toast.hfont && g_toast.fontPx == desiredPx) return;
    if (g_toast.hfont) {
        DeleteObject(g_toast.hfont);
        g_toast.hfont = nullptr;
    }
    /* Same body face as the rest of the UI; size scaled with the
    ** emulator zoom so the toast stays a similar visual fraction of
    ** the window at every zoom level. */
    LOGFONTW lf{};
    lf.lfHeight  = -desiredPx;
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_toast.hfont = CreateFontIndirectW(&lf);
    g_toast.fontPx = desiredPx;
}

/* Compute the toast box size required to fit g_toast.text. The width is
** capped so the toast doesn't run off-screen for very long paths. */
SIZE measure(HDC hdc, const std::string& text, int maxWidth)
{
    if (!g_toast.hfont) ensureFont();
    HGDIOBJ old = SelectObject(hdc, g_toast.hfont);

    std::wstring wide;
    {
        wchar_t buf[2048];
        Utf8ToWide(text.c_str(), buf, _countof(buf));
        wide = buf;
    }

    RECT rc{0, 0, maxWidth, 1};
    DrawTextW(hdc, wide.c_str(), (int)wide.size(), &rc,
              DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
    SelectObject(hdc, old);

    SIZE sz{rc.right + kPaddingDu * 2, rc.bottom + kPaddingDu * 2};
    /* Min box scales with font so the toast never looks sparse for
    ** very short messages.  Roughly 12 char widths and 2.5 line
    ** heights worth (matches the original 240x48 design at 19 px). */
    int fontPx = g_toast.fontPx > 0 ? g_toast.fontPx : kBaseFontPx;
    int minW = fontPx * 12;
    int minH = fontPx * 5 / 2;
    if (sz.cx < minW) sz.cx = minW;
    if (sz.cy < minH) sz.cy = minH;
    return sz;
}

void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);

    /* Background fill + 1px border. */
    HBRUSH bg = CreateSolidBrush(RGB((kBgRgb >> 16) & 0xFF,
                                     (kBgRgb >> 8) & 0xFF,
                                     kBgRgb & 0xFF));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN border = CreatePen(PS_SOLID, 1,
                            RGB((kBorderRgb >> 16) & 0xFF,
                                (kBorderRgb >> 8) & 0xFF,
                                kBorderRgb & 0xFF));
    HGDIOBJ oldPen   = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(border);

    /* Text. */
    if (!g_toast.hfont) ensureFont();
    HGDIOBJ oldFont = SelectObject(hdc, g_toast.hfont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB((kFgRgb >> 16) & 0xFF,
                          (kFgRgb >> 8) & 0xFF,
                          kFgRgb & 0xFF));

    RECT textRc = rc;
    textRc.left   += kPaddingDu;
    textRc.top    += kPaddingDu;
    textRc.right  -= kPaddingDu;
    textRc.bottom -= kPaddingDu;

    wchar_t wbuf[2048];
    Utf8ToWide(g_toast.text.c_str(), wbuf, _countof(wbuf));
    DrawTextW(hdc, wbuf, -1, &textRc,
              DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK toastWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_TIMER: {
        if (wp != kAnimTimerId) break;
        DWORD elapsed = GetTickCount() - g_toast.startTick;
        if (elapsed >= kTotalDurationMs) {
            KillTimer(hwnd, kAnimTimerId);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        /* Follow anchor: if the anchor window has moved or resized (e.g.
        ** the user changed zoom while the toast was visible), reposition
        ** AND rescale the font to maintain the bottom-left placement
        ** and similar visual weight. Cheap: only triggers on actual
        ** delta. */
        if (g_toast.anchorHwnd && IsWindow(g_toast.anchorHwnd)
            && g_toast.width > 0 && g_toast.height > 0)
        {
            /* Font: rescale on zoom change. Re-measures the box so it
            ** keeps fitting the message, then the reposition step
            ** below uses the fresh width/height. */
            int newFontPx = computeFontPx(g_toast.anchorHwnd);
            if (newFontPx != g_toast.fontPx) {
                ensureFont(newFontPx);
                RECT ac;
                GetWindowRect(g_toast.anchorHwnd, &ac);
                int anchorW = ac.right - ac.left;
                int maxW = (anchorW * 6) / 10;
                if (maxW < 320) maxW = 320;
                HDC hdcMeas = GetDC(hwnd);
                SIZE sz = measure(hdcMeas, g_toast.text, maxW - kPaddingDu * 2);
                ReleaseDC(hwnd, hdcMeas);
                g_toast.width  = sz.cx;
                g_toast.height = sz.cy;
                SetWindowPos(hwnd, NULL, 0, 0, sz.cx, sz.cy,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                InvalidateRect(hwnd, nullptr, TRUE);
            }

            RECT a;
            GetWindowRect(g_toast.anchorHwnd, &a);
            int newX = a.left + kCornerMargin;
            int newY = a.bottom - g_toast.height - kCornerMargin;
            if (newX + g_toast.width > a.right - kCornerMargin)
                newX = a.right - g_toast.width - kCornerMargin;
            if (newY < a.top + kCornerMargin)
                newY = a.top + kCornerMargin;
            RECT cur;
            GetWindowRect(hwnd, &cur);
            if (cur.left != newX || cur.top != newY) {
                SetWindowPos(hwnd, NULL, newX, newY, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        if (elapsed > (DWORD)(kTotalDurationMs - kFadeOutMs)) {
            DWORD progress = elapsed - (kTotalDurationMs - kFadeOutMs);
            if (progress >= kFadeOutMs) progress = kFadeOutMs;
            int alpha = (int)kPeakAlpha
                      - (int)((DWORD)kPeakAlpha * progress / kFadeOutMs);
            if (alpha < 0)   alpha = 0;
            if (alpha > 255) alpha = 255;
            SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        /* Click anywhere dismisses immediately. */
        KillTimer(hwnd, kAnimTimerId);
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_NCHITTEST:
        /* Always client-area so the toast is dismissable by click but the
        ** owner doesn't lose focus to the toast for dragging. */
        return HTCLIENT;

    case WM_DESTROY:
        if (g_toast.hwnd == hwnd) g_toast.hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} /* namespace */

extern "C" void toastShowMessage(HWND ownerHwnd, HWND anchorHwnd, const char* msg)
{
    if (!ownerHwnd || !msg || !msg[0]) return;

    g_toast.text = msg;

    ensureClass();
    /* Pick font size from the anchor (= emu render hwnd) up front so the
    ** measure() below works against the correctly-sized font. */
    HWND anchorForFont = anchorHwnd ? anchorHwnd : ownerHwnd;
    ensureFont(computeFontPx(anchorForFont));

    if (!g_toast.hwnd) {
        g_toast.hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kClassName, L"",
            WS_POPUP,
            0, 0, 400, 60,
            ownerHwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_toast.hwnd) return;
    }

    /* Position relative to the anchor (emu render hwnd) when supplied,
    ** otherwise fall back to the owner. Stash the anchor so the WM_TIMER
    ** loop can re-poll its rect and follow window moves / zoom changes
    ** while the toast is visible. Width capped to 60% of the anchor's
    ** width so long paths wrap rather than running off-screen. */
    HWND anchorForRect = anchorHwnd ? anchorHwnd : ownerHwnd;
    g_toast.anchorHwnd = anchorForRect;
    RECT ownerRect;
    GetWindowRect(anchorForRect, &ownerRect);
    int ownerW = ownerRect.right - ownerRect.left;
    int ownerH = ownerRect.bottom - ownerRect.top;
    int maxW = (ownerW * 6) / 10;
    if (maxW < 320) maxW = 320;

    HDC hdcMeas = GetDC(g_toast.hwnd);
    SIZE sz = measure(hdcMeas, g_toast.text, maxW - kPaddingDu * 2);
    ReleaseDC(g_toast.hwnd, hdcMeas);

    g_toast.width  = sz.cx;
    g_toast.height = sz.cy;

    int x = ownerRect.left + kCornerMargin;
    int y = ownerRect.bottom - sz.cy - kCornerMargin;
    /* Clamp so the toast stays on the owner rect when the window is small. */
    if (x + sz.cx > ownerRect.right - kCornerMargin)
        x = ownerRect.right - sz.cx - kCornerMargin;
    if (y < ownerRect.top  + kCornerMargin)
        y = ownerRect.top  + kCornerMargin;
    /* Avoid going under the desktop's right/bottom edge in fullscreen. */
    (void)ownerW; (void)ownerH;

    SetWindowPos(g_toast.hwnd, HWND_TOPMOST, x, y, sz.cx, sz.cy,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetLayeredWindowAttributes(g_toast.hwnd, 0, kPeakAlpha, LWA_ALPHA);
    InvalidateRect(g_toast.hwnd, nullptr, TRUE);

    g_toast.startTick = GetTickCount();
    /* Re-arm: KillTimer + SetTimer reset the elapsed counter so a second
    ** notification gets the full visible duration. */
    KillTimer(g_toast.hwnd, kAnimTimerId);
    SetTimer(g_toast.hwnd, kAnimTimerId, 50, nullptr);
}

extern "C" void toastShowSaved(HWND ownerHwnd, HWND anchorHwnd, const char* savedPath)
{
    if (!savedPath || !savedPath[0]) return;
    char msgBuf[2048];
    const char* fmt = langInfoToastSaved();
    if (!fmt || !fmt[0]) fmt = "Saved: %s";
    _snprintf_s(msgBuf, sizeof(msgBuf), _TRUNCATE, fmt, savedPath);
    toastShowMessage(ownerHwnd, anchorHwnd, msgBuf);
}

extern "C" void toastHide(void)
{
    if (g_toast.hwnd) {
        KillTimer(g_toast.hwnd, kAnimTimerId);
        ShowWindow(g_toast.hwnd, SW_HIDE);
    }
}

extern "C" void toastDestroy(void)
{
    if (g_toast.hwnd) {
        KillTimer(g_toast.hwnd, kAnimTimerId);
        DestroyWindow(g_toast.hwnd);
        g_toast.hwnd = nullptr;
    }
    if (g_toast.hfont) {
        DeleteObject(g_toast.hfont);
        g_toast.hfont = nullptr;
    }
}
