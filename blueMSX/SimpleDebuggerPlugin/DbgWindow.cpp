/*****************************************************************************
** File:        DbgWindow.cpp
**
** Author:      Daniel Vik
**
** Copyright (C) 2003-2004 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
******************************************************************************
*/
#include "DbgWindow.h"
#include "Resource.h"
#include "IniFileParser.h"
#include "Win32TextUtf8.h"
#include "ToolInterface.h"
#include <uxtheme.h>
#include <map>

/* DPI-scale a 96-DPI pixel value for the given owner window so the default
** 800x740 sub-window layout stays consistent at 150%/200% monitor DPI. */
static int dbgDpiScale(HWND hRef, int px96)
{
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow pGetDpi = NULL;
    static BOOL resolved = FALSE;
    if (!resolved) {
        HMODULE h = GetModuleHandleW(L"user32.dll");
        if (h) pGetDpi = (PFN_GetDpiForWindow)GetProcAddress(h, "GetDpiForWindow");
        resolved = TRUE;
    }
    UINT dpi = (pGetDpi && hRef) ? pGetDpi(hRef) : 96;
    if (dpi <= 0) dpi = 96;
    return MulDiv(px96, dpi, 96);
}

using namespace std;

#define MAX_WINDOWS 16

typedef map<HWND, DbgWindow*> WindowMap;

static WindowMap windows;
static DbgWindow* isCreating = NULL;

#define DBG_FONT_MIN     6
#define DBG_FONT_MAX     24
#define DBG_FONT_DEFAULT 10

static int fontPoints = DBG_FONT_DEFAULT;

int dbgFontPoints()        { return fontPoints; }
int dbgFontPointsDefault() { return DBG_FONT_DEFAULT; }

void dbgSetFontPoints(int points)
{
    if (points < DBG_FONT_MIN) points = DBG_FONT_MIN;
    if (points > DBG_FONT_MAX) points = DBG_FONT_MAX;
    if (points == fontPoints) {
        return;
    }
    fontPoints = points;

    for (WindowMap::iterator i = windows.begin(); i != windows.end(); ++i) {
        i->second->onFontChanged();
    }
}

/* Turn a wheel notch into line scrolls, so every view that already handles
** WM_VSCROLL follows the wheel without its own scrolling code. */
static int wheelScroll(HWND hwnd, WPARAM wParam)
{
    UINT lines = 3;
    SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == 0) {
        return 1;
    }
    if (lines > 16) {
        lines = 16;
    }

    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) {
        notches = delta > 0 ? 1 : -1;
    }

    int action = notches > 0 ? SB_LINEUP : SB_LINEDOWN;
    int count  = (notches > 0 ? notches : -notches) * (int)lines;
    for (int i = 0; i < count; i++) {
        SendMessage(hwnd, WM_VSCROLL, action, 0);
    }
    return 1;
}

int dbgViewMessage(HWND hwnd, UINT iMsg, WPARAM wParam)
{
    if (iMsg == WM_MOUSEWHEEL && GetKeyState(VK_CONTROL) >= 0) {
        return wheelScroll(hwnd, wParam);
    }

    if (GetKeyState(VK_CONTROL) >= 0) {
        return 0;
    }

    if (iMsg == WM_MOUSEWHEEL) {
        dbgSetFontPoints(fontPoints + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1));
        return 1;
    }

    if (iMsg == WM_KEYDOWN) {
        switch (wParam) {
        case VK_OEM_PLUS:
        case VK_ADD:
            dbgSetFontPoints(fontPoints + 1);
            return 1;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            dbgSetFontPoints(fontPoints - 1);
            return 1;
        case '0':
        case VK_NUMPAD0:
            dbgSetFontPoints(DBG_FONT_DEFAULT);
            return 1;
        }
    }
    return 0;
}

int dbgGetScrollPos(HWND hwnd)
{
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask  = SIF_POS;
    /* GetScrollInfo leaves nPos alone when it fails, which it does until the
    ** window has a scroll bar, so start from the top rather than from junk. */
    si.nPos   = 0;
    GetScrollInfo(hwnd, SB_VERT, &si);
    return si.nPos;
}

void dbgSetScrollPos(HWND hwnd, int pos)
{
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask  = SIF_POS;
    si.nPos   = pos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

void dbgRebuildFont(HDC hMemdc, HFONT* hFont, HFONT* hFontBold,
                    int* textWidth, int* textHeight, int aveCharWidth)
{
    int height = -MulDiv(fontPoints, GetDeviceCaps(hMemdc, LOGPIXELSY), 72);

    /* Select the replacement first -- DeleteObject is a no-op on a font that
    ** is still selected into the DC. */
    HFONT hNew = CreateFont(height, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
    SelectObject(hMemdc, hNew);
    if (*hFont) DeleteObject(*hFont);
    *hFont = hNew;

    if (hFontBold != NULL) {
        hNew = CreateFont(height, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
        if (*hFontBold) DeleteObject(*hFontBold);
        *hFontBold = hNew;
    }

    TEXTMETRIC tm;
    if (GetTextMetrics(hMemdc, &tm)) {
        *textHeight = tm.tmHeight;
        *textWidth  = aveCharWidth ? tm.tmAveCharWidth : tm.tmMaxCharWidth;
    }
}

/* Repaint the NC area dark -- default WS_CAPTION/WS_THICKFRAME paint uses
** COLOR_3DLIGHT/3DSHADOW + system caption color which clashes in dark mode. */
static HFONT s_captionFont    = NULL;
static UINT  s_captionFontDpi = 0;
static HFONT getCaptionFont(HWND hwnd)
{
    typedef UINT (WINAPI *PFN_GDFW)(HWND);
    static PFN_GDFW pGdfw = NULL;
    static BOOL resolved = FALSE;
    if (!resolved) {
        HMODULE h = GetModuleHandleW(L"user32.dll");
        if (h) pGdfw = (PFN_GDFW)GetProcAddress(h, "GetDpiForWindow");
        resolved = TRUE;
    }
    UINT dpi = pGdfw ? pGdfw(hwnd) : 96;
    if (s_captionFont && s_captionFontDpi == dpi) return s_captionFont;
    if (s_captionFont) DeleteObject(s_captionFont);
    NONCLIENTMETRICS ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    s_captionFont    = CreateFontIndirect(&ncm.lfCaptionFont);
    s_captionFontDpi = dpi;
    return s_captionFont;
}

static void paintFrameDark(HWND hwnd)
{
    RECT wr;
    GetWindowRect(hwnd, &wr);
    POINT cTL = {0, 0};
    ClientToScreen(hwnd, &cTL);
    int wndW = wr.right  - wr.left;
    int wndH = wr.bottom - wr.top;
    int cl   = cTL.x - wr.left;
    int ct   = cTL.y - wr.top;
    RECT cr;
    GetClientRect(hwnd, &cr);
    int cr_right  = cl + cr.right;
    int cr_bottom = ct + cr.bottom;

    HDC hdc = GetWindowDC(hwnd);
    if (!hdc) return;
    /* Frame uses a slightly-lighter dark (48,48,48) so the view edge is
    ** still discernible against the host's main DARK_BG (32,32,32) inner
    ** content. Cached statically to avoid re-creation each NC paint. */
    static HBRUSH s_frameBrush = NULL;
    if (!s_frameBrush) s_frameBrush = CreateSolidBrush(RGB(48, 48, 48));
    HBRUSH br = s_frameBrush;
    if (br) {
        RECT band;
        SetRect(&band, 0, 0, wndW, ct);                FillRect(hdc, &band, br);
        SetRect(&band, 0, ct, cl, cr_bottom);          FillRect(hdc, &band, br);
        SetRect(&band, cr_right, ct, wndW, cr_bottom); FillRect(hdc, &band, br);
        SetRect(&band, 0, cr_bottom, wndW, wndH);      FillRect(hdc, &band, br);
    }
    /* Re-render the caption text on top of the dark fill. */
    wchar_t title[256] = {0};
    if (GetWindowTextW(hwnd, title, (int)_countof(title)) > 0) {
        HFONT hFont = getCaptionFont(hwnd);
        HFONT hOld  = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
        SetTextColor(hdc, GetDarkFg());
        SetBkMode(hdc, TRANSPARENT);
        RECT tr;
        SetRect(&tr, cl + 4, 0, wndW - 8, ct);
        DrawTextW(hdc, title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (hOld) SelectObject(hdc, hOld);
    }
    ReleaseDC(hwnd, hdc);
}

static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    if (isCreating != NULL && iMsg == WM_CREATE )
    {
        isCreating->hwnd = hwnd;
        windows[hwnd] = isCreating;
    }

    if (dbgViewMessage(hwnd, iMsg, wParam)) {
        return 0;
    }

    WindowMap::iterator i = windows.find(hwnd);
    if (i != windows.end()) {
        DbgWindow* window = i->second;
        if( iMsg == WM_WINDOWPOSCHANGED ) {
            window->updateWindowPos((WINDOWPOS*)lParam);
        }
        if ((iMsg == WM_NCPAINT || iMsg == WM_NCACTIVATE) && IsDarkMode()) {
            LRESULT r = window->wndProc(iMsg, wParam, lParam);
            paintFrameDark(hwnd);
            return r;
        }
        return window->wndProc(iMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

DbgWindow::DbgWindow(HINSTANCE hInst, HWND wndOwner, const std::string& name, const std::string& ininame,
                     int defX, int defY, int defW, int defH, int defV) : 
    hInstance(hInst), owner(wndOwner), editEnabled(false), iniName(ininame), winName(name)
{
    static WNDCLASSEX wndClass;

    wndClass.cbSize         = sizeof(wndClass);
    wndClass.style          = CS_VREDRAW;
    wndClass.lpfnWndProc    = staticWndProc;
    wndClass.cbClsExtra     = 0;
    wndClass.cbWndExtra     = 0;
    wndClass.hInstance      = hInstance;
    wndClass.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hIconSm        = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground  = NULL;
    wndClass.lpszMenuName   = NULL;
    wndClass.lpszClassName  = "msxdbgsub";

    RegisterClassEx(&wndClass);
    
    /* Defaults are 96-DPI logical pixels; scale for the parent's monitor so
    ** the layout stays usable at 150% / 200% DPI on first run. INI overrides
    ** (set after the user resizes) are used verbatim. */
    x       = iniFileGetInt( iniName.c_str(), "x",       dbgDpiScale(wndOwner, defX) );
    y       = iniFileGetInt( iniName.c_str(), "y",       dbgDpiScale(wndOwner, defY) );
    width   = iniFileGetInt( iniName.c_str(), "width",   dbgDpiScale(wndOwner, defW) );
    height  = iniFileGetInt( iniName.c_str(), "height",  dbgDpiScale(wndOwner, defH) );
    visible = iniFileGetInt( iniName.c_str(), "visible", defV );
}

void DbgWindow::init()
{
    isCreating = this;
    /* CreateWindowExA mangles UTF-8 captions through CP932 -- create with
    ** NULL then set the wide title via SetWindowTextU.  WS_EX_TOOLWINDOW
    ** dropped so the caption uses the regular lfCaptionFont. */
    hwnd = CreateWindowEx(0, "msxdbgsub", NULL,
                          WS_OVERLAPPED | WS_CLIPSIBLINGS | WS_CHILD | WS_CAPTION | WS_THICKFRAME,
                          x, y, width, height, owner, NULL, hInstance, NULL);
    if (hwnd) SetWindowTextU(hwnd, winName.c_str());
    isCreating = NULL;
    if( visible ) {
        show();
    }
}

DbgWindow::~DbgWindow()
{
    iniFileWriteInt( iniName.c_str(), "x",       x);
    iniFileWriteInt( iniName.c_str(), "y",       y);
    iniFileWriteInt( iniName.c_str(), "width",   width);
    iniFileWriteInt( iniName.c_str(), "height",  height);
    iniFileWriteInt( iniName.c_str(), "visible", visible);

    for (WindowMap::iterator i = windows.begin(); i != windows.end(); ++i) {
        if (i->first == hwnd) {
            windows.erase(i);
            break;
        }
    }
}

void DbgWindow::show()
{
    visible = 1;
    ShowWindow(hwnd, true);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void DbgWindow::hide()
{
    visible = 0;
    ShowWindow(hwnd, false);
}

bool DbgWindow::isVisible()
{
    return visible != 0;
}

void DbgWindow::enableEdit()
{
    editEnabled = true;
}

void DbgWindow::disableEdit()
{
    editEnabled = false;
}

void DbgWindow::updateWindowPos(WINDOWPOS* windowPos) 
{
    x       = windowPos->x;
    y       = windowPos->y;
    width   = windowPos->cx;
    height  = windowPos->cy;
}

void darkSubWindow(HWND hwnd)
{
    if (!hwnd || !IsDarkMode()) return;
    /* DarkMode_Explorer theme darkens scrollbars and inherited common
    ** controls. Plus the host's full ApplyDarkMode walk for child controls
    ** (richedit / static / etc.) and WM_CTLCOLOR subclass. */
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
    ApplyDarkMode(hwnd);
}

