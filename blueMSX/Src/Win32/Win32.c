/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32.c,v $
**
** $Revision: 1.205 $
**
** $Date: 2009-07-07 02:38:25 $
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
*/
#define DIRECTINPUT_VERSION     0x0700
#define _WIN32_DCOM

#include <windows.h>
#include <direct.h>
#include <stdlib.h>
#include <stdio.h>
#include <CommCtrl.h>
#include <shlobj.h> 
#include <shlwapi.h>
#include <dbt.h>
#include "Win32FileTypes.h"
#include "Win32ThemeClassic.h"
#include "Board.h"
#include "Led.h"
#include "Switches.h"
#include "AudioMixer.h"
#include "VideoRender.h"
#include "CommandLine.h"
#include "Language.h"   
#include "resource.h"
#include "Casette.h"
#include "PrinterIO.h"
#include "UartIO.h"
#include "MidiIO.h"
#include "RomLoader.h"
#include "MediaDb.h"
#include "build_number.h"
#include "FrameBuffer.h"
#include "Win32Midi.h"
#include "Win32Sound.h"
#include "Win32Properties.h"
#include "Win32ToolLoader.h"
#include "Win32joystick.h"
#include "Win32keyboard.h"
#include "Win32Printer.h"
#include "Win32directx.h"
#include "Win32D3D12.h"
#include "Win32Avi.h"
#include "FileHistory.h"
#include "Win32Dir.h"
#include "Win32file.h"
#include "Win32Help.h"
#include "Win32Menu.h"
#include "Win32TextUtf8.h"
#include "ArchMenu.h"
#include "Win32Eth.h"
#include "Win32VideoIn.h"
#include "Win32ScreenShot.h"
#include "Win32MouseEmu.h"
#include "Win32machineConfig.h"
#include "Win32ShortcutsConfig.h"
#include "Win32Cdrom.h"
#include "Actions.h"
#include "LaunchFile.h"
#include "TokenExtract.h"
#include "Emulator.h"
#include "JoystickPort.h"
#include "Theme.h"
#include "VideoRender.h"
#include "ThemeLoader.h"
#include "Win32ThemeClassic.h"
#include "ArchNotifications.h"
#include "ArchEvent.h"
#include "ArchTimer.h"
#include "ArchFile.h"
#include "ArchInput.h"
#include "AppConfig.h"
#include "SlotManager.h"

#pragma warning(disable: 4996)
#pragma comment(lib, "dwmapi.lib")

// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"
#include <dwmapi.h>
#include <uxtheme.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* Dark mode palette: matches Windows 11 / Explorer dark theme background and
** controls; tuned to keep contrast against system-rendered focus rings and
** modal scrollbars. */
#define DARK_BG       RGB( 32,  32,  32)
#define DARK_FG       RGB(240, 240, 240)
#define DARK_EDIT_BG  RGB( 48,  48,  48)

#define WIN32_DARK_SUBCLASS_ID 0xD8B41

void vdpSetDisplayEnable(int enable);

static HBRUSH  s_darkBkBrush   = NULL;
static HBRUSH  s_darkEditBrush = NULL;

/* Cached AppsUseLightTheme (-1 = unprobed): per-message registry reads
** froze message floods for ~2 s.  Invalidated on WM_SETTINGCHANGE
** "ImmersiveColorSet". */
static volatile LONG g_darkModeCached = -1;

void win32InvalidateDarkModeCache(void) {
    g_darkModeCached = -1;
}

static BOOL win32IsDarkMode(void) {
    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = REG_DWORD;
    HKEY hKey;
    LONG cached = g_darkModeCached;
    if (cached >= 0) return (BOOL)cached;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, &type, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
    }
    g_darkModeCached = (value == 0) ? 1 : 0;
    return value == 0;
}

/* Opt the app into uxtheme dark mode via undocumented ordinals:
   1809 = 132 (AllowDarkModeForApp), 1903+ = 135 (SetPreferredAppMode) +
   104 (RefreshImmersiveColorPolicyState). Required for IFileDialog and
   common-menu dark theming on first show. */
typedef enum {
    BLUEMSX_AppMode_Default = 0,
    BLUEMSX_AppMode_AllowDark = 1,
    BLUEMSX_AppMode_ForceDark = 2,
    BLUEMSX_AppMode_ForceLight = 3
} BLUEMSX_PREFERREDAPPMODE;

static void win32EnableDarkModeForApp(void) {
    HMODULE huxtheme;
    DWORD build = 0;
    BOOL dark = win32IsDarkMode();

    /* RtlGetVersion: GetVersionEx caps to 6.2 with our Win10/11 manifest. */
    {
        HMODULE hntdll = GetModuleHandleW(L"ntdll.dll");
        if (hntdll) {
            typedef LONG (WINAPI *PFN_RtlGetVersion)(POSVERSIONINFOW);
            PFN_RtlGetVersion p = (PFN_RtlGetVersion)GetProcAddress(hntdll, "RtlGetVersion");
            if (p) {
                OSVERSIONINFOW os;
                ZeroMemory(&os, sizeof(os));
                os.dwOSVersionInfoSize = sizeof(os);
                if (p(&os) == 0) build = os.dwBuildNumber;
            }
        }
    }

    huxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!huxtheme) return;

    /* ForceDark/ForceLight (not AllowDark) so out-of-process IFileDialog
       shell content and popup menus follow the same polarity as the app. */
    if (build >= 18362) {
        typedef int (WINAPI *PFN_SetPreferredAppMode)(BLUEMSX_PREFERREDAPPMODE);
        PFN_SetPreferredAppMode p =
            (PFN_SetPreferredAppMode)GetProcAddress(huxtheme, MAKEINTRESOURCEA(135));
        if (p) p(dark ? BLUEMSX_AppMode_ForceDark : BLUEMSX_AppMode_ForceLight);
    } else if (build >= 17763) {
        typedef BOOL (WINAPI *PFN_AllowDarkModeForApp)(BOOL);
        PFN_AllowDarkModeForApp p =
            (PFN_AllowDarkModeForApp)GetProcAddress(huxtheme, MAKEINTRESOURCEA(132));
        if (p) p(dark);
    }

    {
        typedef void (WINAPI *PFN_RefreshImmersiveColorPolicyState)(void);
        PFN_RefreshImmersiveColorPolicyState p =
            (PFN_RefreshImmersiveColorPolicyState)GetProcAddress(huxtheme,
                                                                  MAKEINTRESOURCEA(104));
        if (p) p();
    }
    {
        /* uxtheme!FlushMenuThemes (ordinal 136): reset cached menu theme so
        ** the new app mode takes effect on subsequently opened menus and
        ** common dialogs without needing a focus-change repaint. */
        typedef void (WINAPI *PFN_FlushMenuThemes)(void);
        PFN_FlushMenuThemes p =
            (PFN_FlushMenuThemes)GetProcAddress(huxtheme, MAKEINTRESOURCEA(136));
        if (p) p();
    }
    /* Keep the library loaded; uxtheme uses its own globals after this. */
}

static void win32ApplyDarkTitle(HWND hwnd) {
    BOOL dark = win32IsDarkMode();
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

/* uxtheme!AllowDarkModeForWindow (ordinal 133): needed on top of
** SetPreferredAppMode so IFileDialog parts go dark on first show. */
static void win32AllowDarkForWindow(HWND hwnd, BOOL allow) {
    HMODULE huxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!huxtheme) return;
    {
        typedef BOOL (WINAPI *PFN_AllowDarkModeForWindow)(HWND, BOOL);
        PFN_AllowDarkModeForWindow p =
            (PFN_AllowDarkModeForWindow)GetProcAddress(huxtheme, MAKEINTRESOURCEA(133));
        if (p) p(hwnd, allow);
    }
}



static HBRUSH win32GetDarkBkBrush(void) {
    if (!s_darkBkBrush) s_darkBkBrush = CreateSolidBrush(DARK_BG);
    return s_darkBkBrush;
}

/* Public dark-mode helpers (declared in Win32Common.h) used by custom-paint
** child controls (msctls_hotkey32 etc.) that miss the WM_CTLCOLOR* path. */
BOOL win32CommonIsDarkMode(void) { return win32IsDarkMode(); }
COLORREF win32CommonDarkBg(void) { return DARK_BG; }
COLORREF win32CommonDarkFg(void) { return DARK_FG; }
HBRUSH   win32CommonDarkBgBrush(void) { return win32GetDarkBkBrush(); }

/* Forward declared so this wrapper can sit alongside the other public dark
** helpers; the actual win32ApplyDarkToDialog body is later in this file. */
static void win32ApplyDarkToDialog(HWND hwnd);
void win32CommonApplyDark(HWND hDlg) { win32ApplyDarkToDialog(hDlg); }

void win32CommonCenterOnOwner(HWND hDlg)
{
    HWND owner = GetWindow(hDlg, GW_OWNER);
    if (!owner) owner = getMainHwnd();
    if (!owner) return;

    RECT dr, orect;
    if (!GetWindowRect(hDlg, &dr))     return;
    if (!GetWindowRect(owner, &orect)) return;

    int dlgW = dr.right     - dr.left;
    int dlgH = dr.bottom    - dr.top;
    int ownW = orect.right  - orect.left;
    int ownH = orect.bottom - orect.top;

    int x = orect.left + (ownW - dlgW) / 2;
    int y = orect.top  + (ownH - dlgH) / 2;

    /* Clamp into the work area of the monitor containing the owner so the
    ** dialog can't slip off-screen when the main window is partially off
    ** an edge. */
    HMONITOR mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        if (x + dlgW > mi.rcWork.right)  x = mi.rcWork.right  - dlgW;
        if (y + dlgH > mi.rcWork.bottom) y = mi.rcWork.bottom - dlgH;
        if (x < mi.rcWork.left)          x = mi.rcWork.left;
        if (y < mi.rcWork.top)           y = mi.rcWork.top;
    }
    SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static HBRUSH win32GetDarkEditBrush(void) {
    if (!s_darkEditBrush) s_darkEditBrush = CreateSolidBrush(DARK_EDIT_BG);
    return s_darkEditBrush;
}

#define WIN32_DARK_TAB_SUBCLASS_ID      0xD8B42
#define WIN32_DARK_GROUPBOX_SUBCLASS_ID 0xD8B43
#define WIN32_DARK_HEADER_SUBCLASS_ID   0xD8B44

/* Tab control WM_PAINT subclass: NM_CUSTOMDRAW is unreliable for
** PropSheet tabs, so own the full painting cycle instead. */
static LRESULT CALLBACK win32DarkTabSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                 UINT_PTR id, DWORD_PTR data) {
    (void)data;
    if (win32IsDarkMode()) {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1; /* paint in WM_PAINT */
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rcClient;
            int count, sel, i;
            HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
            HFONT hOld  = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;

            GetClientRect(hwnd, &rcClient);
            FillRect(hdc, &rcClient, win32GetDarkBkBrush());

            count = TabCtrl_GetItemCount(hwnd);
            sel   = TabCtrl_GetCurSel(hwnd);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, DARK_FG);
            for (i = 0; i < count; i++) {
                RECT rcItem;
                wchar_t buf[256];
                TCITEMW tci = {0};
                HBRUSH bg = (i == sel) ? win32GetDarkEditBrush() : win32GetDarkBkBrush();

                if (!TabCtrl_GetItemRect(hwnd, i, &rcItem)) continue;
                tci.mask = TCIF_TEXT;
                tci.pszText = buf;
                tci.cchTextMax = (int)_countof(buf);
                SendMessageW(hwnd, TCM_GETITEMW, (WPARAM)i, (LPARAM)&tci);

                FillRect(hdc, &rcItem, bg);
                DrawTextW(hdc, buf, -1, &rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            if (hOld) SelectObject(hdc, hOld);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, win32DarkTabSubclassProc, id);
            break;
        }
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, win32DarkTabSubclassProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/* Radio / checkbox POSTPAINT: DarkMode_Explorer darkens the indicator
** but not the label, so redraw the caption in DARK_FG ourselves. */
static LRESULT win32DarkButtonCustomDraw(LPNMCUSTOMDRAW cd) {
    HWND hwnd = cd->hdr.hwndFrom;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    UINT type  = (UINT)(style & BS_TYPEMASK);

    /* GROUPBOX is owner-painted by its own subclass. Push buttons accept the
    ** themed dark text just fine in Win11, so leave them alone. */
    if (type != BS_AUTORADIOBUTTON && type != BS_RADIOBUTTON &&
        type != BS_AUTOCHECKBOX   && type != BS_CHECKBOX    &&
        type != BS_AUTO3STATE     && type != BS_3STATE) {
        return CDRF_DODEFAULT;
    }

    switch (cd->dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYPOSTPAINT;
    case CDDS_POSTPAINT: {
        wchar_t buf[256];
        int len = GetWindowTextW(hwnd, buf, (int)_countof(buf));
        if (len > 0) {
            HDC hdc = cd->hdc;
            RECT r = cd->rc;
            HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
            HFONT hOld  = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
            int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            /* Indicator size scales with DPI; classic 13 px at 96 DPI. */
            int indW = MulDiv(13, dpi, 96) + 4;
            BOOL leftText = (style & BS_LEFTTEXT) != 0;

            if (leftText) {
                /* Indicator on right, text on left. Erase to indicator left edge. */
                r.right -= indW;
            } else {
                /* Indicator on left, text on right. Erase from text start. */
                r.left += indW;
            }
            FillRect(hdc, &r, win32GetDarkBkBrush());
            SetTextColor(hdc, DARK_FG);
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, buf, len, &r,
                      DT_VCENTER | DT_SINGLELINE | (leftText ? DT_RIGHT : DT_LEFT));

            if (hOld) SelectObject(hdc, hOld);
        }
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

/* SysHeader32 direct subclass: ListView column headers stay light-themed
** even with SetWindowTheme; owner-draw via WM_PAINT to apply dark colors. */
static LRESULT CALLBACK win32DarkHeaderSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                    UINT_PTR id, DWORD_PTR data) {
    (void)data;
    if (win32IsDarkMode()) {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rcClient;
            int count, i;
            HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
            HFONT hOld  = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
            HPEN  pen   = CreatePen(PS_SOLID, 1, RGB(96, 96, 96));

            GetClientRect(hwnd, &rcClient);
            FillRect(hdc, &rcClient, win32GetDarkBkBrush());

            count = (int)SendMessageW(hwnd, HDM_GETITEMCOUNT, 0, 0);
            SetTextColor(hdc, DARK_FG);
            SetBkMode(hdc, TRANSPARENT);
            for (i = 0; i < count; i++) {
                RECT rc;
                wchar_t buf[256];
                HDITEMW hdi = {0};
                HPEN oldPen;

                if (!Header_GetItemRect(hwnd, i, &rc)) continue;
                hdi.mask = HDI_TEXT;
                hdi.pszText = buf;
                hdi.cchTextMax = (int)_countof(buf);
                SendMessageW(hwnd, HDM_GETITEMW, (WPARAM)i, (LPARAM)&hdi);

                FillRect(hdc, &rc, win32GetDarkBkBrush());

                /* Right and bottom 1 px borders for visual separation. */
                oldPen = (HPEN)SelectObject(hdc, pen);
                MoveToEx(hdc, rc.right - 1, rc.top, NULL);
                LineTo  (hdc, rc.right - 1, rc.bottom);
                MoveToEx(hdc, rc.left,      rc.bottom - 1, NULL);
                LineTo  (hdc, rc.right,     rc.bottom - 1);
                SelectObject(hdc, oldPen);

                {
                    RECT t = rc;
                    t.left += 8;
                    t.right -= 8;
                    DrawTextW(hdc, buf, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
            }

            DeleteObject(pen);
            if (hOld) SelectObject(hdc, hOld);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, win32DarkHeaderSubclassProc, id);
            break;
        }
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, win32DarkHeaderSubclassProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/* GROUPBOX direct subclass. The BUTTON-with-BS_GROUPBOX style draws its
** caption with hard-coded system text color, so WM_PAINT is intercepted
** to draw frame + caption in dark-mode colours. */
static LRESULT CALLBACK win32DarkGroupBoxSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                      UINT_PTR id, DWORD_PTR data) {
    (void)data;
    if (win32IsDarkMode()) {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            wchar_t text[256];
            int len;
            HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
            HFONT hOld  = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
            SIZE sz = {0, 0};

            GetClientRect(hwnd, &rc);
            len = GetWindowTextW(hwnd, text, (int)_countof(text));
            if (len > 0) GetTextExtentPoint32W(hdc, text, len, &sz);

            FillRect(hdc, &rc, win32GetDarkBkBrush());

            /* Frame, sunk by half-cap-height so the caption sits across the top edge. */
            {
                RECT frame = rc;
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                HPEN   pen   = CreatePen(PS_SOLID, 1, RGB(96, 96, 96));
                HPEN   oldPn = (HPEN)SelectObject(hdc, pen);
                if (sz.cy > 0) frame.top += sz.cy / 2;
                Rectangle(hdc, frame.left, frame.top, frame.right, frame.bottom);
                SelectObject(hdc, oldPn);
                DeleteObject(pen);
                SelectObject(hdc, oldBr);
            }

            /* Caption: erase a 4 px-padded box behind it so the frame line does
            ** not strike through, then draw the label in the dark foreground. */
            if (len > 0) {
                RECT t;
                t.left   = rc.left + 8;
                t.top    = rc.top;
                t.right  = t.left + sz.cx + 4;
                t.bottom = t.top + sz.cy;
                FillRect(hdc, &t, win32GetDarkBkBrush());
                SetTextColor(hdc, DARK_FG);
                SetBkMode(hdc, TRANSPARENT);
                DrawTextW(hdc, text, len, &t, DT_LEFT | DT_TOP | DT_SINGLELINE);
            }

            if (hOld) SelectObject(hdc, hOld);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, win32DarkGroupBoxSubclassProc, id);
            break;
        }
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, win32DarkGroupBoxSubclassProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/* WM_UAHDRAWMENU/ITEM (0x91/0x92) carry dark-menu paint requests once the
   window is opted into AllowDarkModeForWindow + ForceDark. Used for
   plugin / sub-windows; main window uses custom strip in Win32Menu.c. */
#ifndef WM_UAHDRAWMENU
#define WM_UAHDRAWMENU       0x0091
#define WM_UAHDRAWMENUITEM   0x0092
#endif

typedef union tagWIN32_UAHMENUITEMMETRICS {
    struct { DWORD cx; DWORD cy; } rgsizeBar[2];
    struct { DWORD cx; DWORD cy; } rgsizePopup[4];
} WIN32_UAHMENUITEMMETRICS;

typedef struct tagWIN32_UAHMENUPOPUPMETRICS {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
} WIN32_UAHMENUPOPUPMETRICS;

typedef struct tagWIN32_UAHMENU {
    HMENU hmenu;
    HDC   hdc;
    DWORD dwFlags;
} WIN32_UAHMENU;

typedef struct tagWIN32_UAHMENUITEM {
    int                       iPosition;
    WIN32_UAHMENUITEMMETRICS  umim;
    WIN32_UAHMENUPOPUPMETRICS umpm;
} WIN32_UAHMENUITEM;

typedef struct tagWIN32_UAHDRAWMENUITEM {
    DRAWITEMSTRUCT     dis;
    WIN32_UAHMENU      um;
    WIN32_UAHMENUITEM  umi;
} WIN32_UAHDRAWMENUITEM;

static HFONT s_darkMenuFont    = NULL;
static UINT  s_darkMenuFontDpi = 0;

/* Cache one menu font per DPI.  lfHeight is scaled at half the DPI
** ratio so sub-window menu bars don't overwhelm the strip when the
** monitor is > 96 DPI. */
static HFONT win32GetDarkMenuFont(UINT dpi) {
    NONCLIENTMETRICS ncm;
    if (dpi == 0) dpi = 96;
    if (s_darkMenuFont && s_darkMenuFontDpi == dpi) return s_darkMenuFont;
    if (s_darkMenuFont) DeleteObject(s_darkMenuFont);
    s_darkMenuFont = NULL;
    s_darkMenuFontDpi = 0;

    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (dpi > 96) {
        double factor = 1.0 + ((double)dpi / 96.0 - 1.0) * 0.5;
        ncm.lfMenuFont.lfHeight = (LONG)(ncm.lfMenuFont.lfHeight * factor);
    }
    s_darkMenuFont    = CreateFontIndirect(&ncm.lfMenuFont);
    s_darkMenuFontDpi = dpi;
    return s_darkMenuFont;
}

static UINT win32QueryWindowDpi(HWND hwnd) {
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow pGetDpi = (PFN_GetDpiForWindow)(LONG_PTR)-1;
    if (pGetDpi == (PFN_GetDpiForWindow)(LONG_PTR)-1) {
        pGetDpi = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                      "GetDpiForWindow");
    }
    if (pGetDpi) return pGetDpi(hwnd);
    return 96;
}

/* Repaint the 1-pixel light line that the system NC paint draws at the
** bottom of the menu bar. Called from WM_NCPAINT / WM_NCACTIVATE after
** DefSubclassProc returns (which is what draws the offending line). */
static void win32DarkOverpaintMenuBarLine(HWND hwnd) {
    MENUBARINFO mbi;
    RECT wndR, lineR;
    HDC hdc;
    if (!GetMenu(hwnd)) return;
    memset(&mbi, 0, sizeof(mbi));
    mbi.cbSize = sizeof(mbi);
    if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) return;
    GetWindowRect(hwnd, &wndR);
    lineR = mbi.rcBar;
    OffsetRect(&lineR, -wndR.left, -wndR.top);
    lineR.top    = lineR.bottom;
    lineR.bottom = lineR.top + 1;
    hdc = GetWindowDC(hwnd);
    if (hdc) {
        FillRect(hdc, &lineR, win32GetDarkBkBrush());
        ReleaseDC(hwnd, hdc);
    }
}

/* Dialog subclass: WM_CTLCOLOR* dark painting + radio/checkbox NM_CUSTOMDRAW
   forward + WM_UAHDRAWMENU* for plugin windows with a standard menu bar. */
static LRESULT CALLBACK win32DarkSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                              UINT_PTR id, DWORD_PTR data) {
    (void)data;
    if (win32IsDarkMode()) {
        switch (msg) {
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, DARK_FG);
            SetBkColor(hdc, DARK_BG);
            return (LRESULT)win32GetDarkBkBrush();
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, DARK_FG);
            SetBkColor(hdc, DARK_EDIT_BG);
            return (LRESULT)win32GetDarkEditBrush();
        }
        case WM_NOTIFY: {
            LPNMHDR nm = (LPNMHDR)lp;
            if (nm && nm->code == NM_CUSTOMDRAW) {
                wchar_t cls[64];
                if (GetClassNameW(nm->hwndFrom, cls, (int)_countof(cls)) > 0 &&
                    lstrcmpiW(cls, L"Button") == 0) {
                    return win32DarkButtonCustomDraw((LPNMCUSTOMDRAW)nm);
                }
            }
            break;
        }
        case WM_UAHDRAWMENU: {
            WIN32_UAHMENU* pUDM = (WIN32_UAHMENU*)lp;
            MENUBARINFO mbi;
            memset(&mbi, 0, sizeof(mbi));
            mbi.cbSize = sizeof(mbi);
            if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) {
                RECT wndR, rcBar;
                GetWindowRect(hwnd, &wndR);
                rcBar = mbi.rcBar;
                OffsetRect(&rcBar, -wndR.left, -wndR.top);
                FillRect(pUDM->hdc, &rcBar, win32GetDarkBkBrush());
            }
            return 0;
        }
        case WM_UAHDRAWMENUITEM: {
            WIN32_UAHDRAWMENUITEM* pUDMI = (WIN32_UAHDRAWMENUITEM*)lp;
            HDC hdc = pUDMI->um.hdc;
            BOOL hot      = (pUDMI->dis.itemState & ODS_HOTLIGHT) != 0;
            BOOL selected = (pUDMI->dis.itemState & ODS_SELECTED) != 0;
            BOOL disabled = (pUDMI->dis.itemState & (ODS_DISABLED | ODS_GRAYED | ODS_INACTIVE)) != 0;
            HBRUSH bgBrush;
            COLORREF fgColor;
            wchar_t menuStr[256];
            MENUITEMINFOW mii;
            UINT dpi;
            HFONT font, oldFont;
            UINT dtFlags = DT_CENTER | DT_VCENTER | DT_SINGLELINE;

            if (!hdc) hdc = pUDMI->dis.hDC;

            /* Hot/selected use the slightly-lighter edit-background color so
            ** there is a visible highlight against the menu strip's DARK_BG. */
            bgBrush = (hot || selected) ? win32GetDarkEditBrush()
                                        : win32GetDarkBkBrush();
            fgColor = disabled ? RGB(128, 128, 128) : DARK_FG;

            FillRect(hdc, &pUDMI->dis.rcItem, bgBrush);

            menuStr[0] = 0;
            memset(&mii, 0, sizeof(mii));
            mii.cbSize     = sizeof(mii);
            mii.fMask      = MIIM_STRING;
            mii.dwTypeData = menuStr;
            mii.cch        = (UINT)_countof(menuStr) - 1;
            GetMenuItemInfoW(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

            dpi     = win32QueryWindowDpi(hwnd);
            font    = win32GetDarkMenuFont(dpi);
            oldFont = font ? (HFONT)SelectObject(hdc, font) : NULL;
            SetTextColor(hdc, fgColor);
            SetBkMode(hdc, TRANSPARENT);

            /* Honor "hide accelerator underline until Alt is pressed" the same
            ** way the system does for the un-customized menu bar. */
            if (pUDMI->dis.itemState & ODS_NOACCEL) dtFlags |= DT_HIDEPREFIX;

            DrawTextW(hdc, menuStr, -1, &pUDMI->dis.rcItem, dtFlags);

            if (oldFont) SelectObject(hdc, oldFont);
            return 0;
        }
        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
            /* Default NC paint draws a 1-pixel light line at the bottom of
            ** the menu bar. Overpaint it dark. Cheap, no-op when no menu. */
            win32DarkOverpaintMenuBarLine(hwnd);
            return r;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, win32DarkSubclassProc, id);
            break;
        }
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, win32DarkSubclassProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/* STATIC/GROUPBOX/TabControl owner-paint; List/Tree get explicit colors;
   combo DarkMode_CFD; rest DarkMode_Explorer. */
static BOOL CALLBACK win32DarkApplyChild(HWND child, LPARAM lp) {
    BOOL dark = (BOOL)lp;
    wchar_t className[64];
    if (GetClassNameW(child, className, (int)_countof(className)) <= 0) return TRUE;

    /* Per-window opt-in for the dark visual style is required for every
    ** themed common control on Windows 10 1809+. */
    win32AllowDarkForWindow(child, dark);

    if (lstrcmpiW(className, L"STATIC") == 0) {
        return TRUE;
    }

    if (lstrcmpiW(className, L"SysTabControl32") == 0) {
        if (dark) SetWindowSubclass(child, win32DarkTabSubclassProc, WIN32_DARK_TAB_SUBCLASS_ID, 0);
        else      RemoveWindowSubclass(child, win32DarkTabSubclassProc, WIN32_DARK_TAB_SUBCLASS_ID);
        InvalidateRect(child, NULL, TRUE);
        return TRUE;
    }
    if (lstrcmpiW(className, L"Button") == 0) {
        LONG style = GetWindowLongW(child, GWL_STYLE);
        if ((style & BS_TYPEMASK) == BS_GROUPBOX) {
            if (dark) SetWindowSubclass(child, win32DarkGroupBoxSubclassProc, WIN32_DARK_GROUPBOX_SUBCLASS_ID, 0);
            else      RemoveWindowSubclass(child, win32DarkGroupBoxSubclassProc, WIN32_DARK_GROUPBOX_SUBCLASS_ID);
            InvalidateRect(child, NULL, TRUE);
            return TRUE;
        }
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
        return TRUE;
    }

    if (lstrcmpiW(className, L"SysHeader32") == 0) {
        if (dark) SetWindowSubclass(child, win32DarkHeaderSubclassProc, WIN32_DARK_HEADER_SUBCLASS_ID, 0);
        else      RemoveWindowSubclass(child, win32DarkHeaderSubclassProc, WIN32_DARK_HEADER_SUBCLASS_ID);
        SetWindowTheme(child, dark ? L"DarkMode_ItemsView" : L"Explorer", NULL);
        InvalidateRect(child, NULL, TRUE);
        return TRUE;
    }
    if (lstrcmpiW(className, L"SysListView32") == 0) {
        ListView_SetBkColor    (child, dark ? DARK_BG : GetSysColor(COLOR_WINDOW));
        ListView_SetTextBkColor(child, dark ? DARK_BG : GetSysColor(COLOR_WINDOW));
        ListView_SetTextColor  (child, dark ? DARK_FG : GetSysColor(COLOR_WINDOWTEXT));
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }
    else if (lstrcmpiW(className, L"SysTreeView32") == 0) {
        TreeView_SetBkColor  (child, dark ? DARK_BG : (COLORREF)-1);
        TreeView_SetTextColor(child, dark ? DARK_FG : (COLORREF)-1);
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }
    else if (lstrcmpiW(className, L"ComboBox") == 0) {
        SetWindowTheme(child, dark ? L"DarkMode_CFD" : L"Explorer", NULL);
        /* Clear edit selection so CBS_DROPDOWN combos don't open
           highlighted from CB_SETCURSEL during WM_INITDIALOG. */
        SendMessage(child, CB_SETEDITSEL, 0, (LPARAM)MAKELPARAM(-1, 0));
    }
    else {
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    }
    return TRUE;
}

static BOOL CALLBACK win32InvalidateChildProc(HWND child, LPARAM lp) {
    (void)lp;
    InvalidateRect(child, NULL, TRUE);
    return TRUE;
}

static void win32ApplyDarkToDialog(HWND hwnd) {
    BOOL dark = win32IsDarkMode();
    /* Titlebar via DWM. Skip for childless / parented dialogs without a
    ** caption (sub-panel pages mounted inside a parent dialog). */
    if (GetWindowLong(hwnd, GWL_STYLE) & WS_CAPTION) {
        win32ApplyDarkTitle(hwnd);
    }
    /* Walk up to the system-owned property sheet outer dialog (#32770) and
       apply dark there too -- we never see WM_INITDIALOG for that frame. */
    {
        HWND parent = GetParent(hwnd);
        if (parent) {
            wchar_t cls[32];
            if (GetClassNameW(parent, cls, (int)_countof(cls)) > 0 &&
                lstrcmpW(cls, L"#32770") == 0) {
                if (GetWindowLong(parent, GWL_STYLE) & WS_CAPTION) {
                    win32ApplyDarkTitle(parent);
                }
                win32AllowDarkForWindow(parent, dark);
                SetWindowTheme(parent, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
                EnumChildWindows(parent, win32DarkApplyChild, (LPARAM)dark);
                SetWindowSubclass(parent, win32DarkSubclassProc, WIN32_DARK_SUBCLASS_ID, 0);
                SendMessageW(parent, WM_THEMECHANGED, 0, 0);
                InvalidateRect(parent, NULL, TRUE);
            }
        }
    }
    win32AllowDarkForWindow(hwnd, dark);
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    EnumChildWindows(hwnd, win32DarkApplyChild, (LPARAM)dark);
    SetWindowSubclass(hwnd, win32DarkSubclassProc, WIN32_DARK_SUBCLASS_ID, 0);
    /* WM_THEMECHANGED forces every themed control under hwnd to re-query its
    ** colours. Without it the IFileDialog navigation pane / breadcrumb stay
    ** light until the first WM_ACTIVATE re-fires. */
    SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
    /* Invalidate the dialog and every child: subclass + theme switch only
    ** apply to subsequent paints, and WS_CLIPCHILDREN means parent
    ** invalidation does not propagate to children. */
    InvalidateRect(hwnd, NULL, TRUE);
    EnumChildWindows(hwnd, win32InvalidateChildProc, 0);
}



void win32SliderTooltipUpdate(HWND* phwndTip, HWND parent, int percent)
{
    /* Explicit TTM_*W: without UNICODE the unsuffixed macros expand to ANSI
       IDs and TOOLTIPS_CLASSW renders our wide string as ANSI
       (= truncated at first 0x00 byte). */
    static HFONT s_tipFont = NULL;
    wchar_t buf[24];
    POINT pt;
    TOOLINFOW ti;

    if (percent < 0) {
        if (phwndTip && *phwndTip) {
            TOOLINFOW tih = { 0 };
            tih.cbSize = sizeof(tih);
            tih.hwnd   = parent;
            tih.uId    = 0;
            SendMessageW(*phwndTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&tih);
        }
        return;
    }

    if (!phwndTip || !parent) return;

    /* Larger-than-default font so the percent value is easy to read. */
    if (s_tipFont == NULL) {
        s_tipFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD,
                                FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    if (*phwndTip == NULL) {
        INITCOMMONCONTROLSEX iccex = { sizeof(iccex), ICC_BAR_CLASSES };
        TOOLINFOW tin = { 0 };
        InitCommonControlsEx(&iccex);
        *phwndTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            parent, NULL, GetModuleHandle(NULL), NULL);
        if (*phwndTip == NULL) return;

        /* Themed (Comctl32 v6) tooltips ignore WM_SETFONT and pull their
           font from the visual style.  Disable the theme so WM_SETFONT applies. */
        SetWindowTheme(*phwndTip, L"", L"");

        if (s_tipFont) {
            SendMessageW(*phwndTip, WM_SETFONT, (WPARAM)s_tipFont, TRUE);
        }

        tin.cbSize   = sizeof(tin);
        tin.uFlags   = TTF_TRACK | TTF_ABSOLUTE;
        tin.hwnd     = parent;
        tin.uId      = 0;
        tin.lpszText = L"";
        SendMessageW(*phwndTip, TTM_ADDTOOLW, 0, (LPARAM)&tin);
    }

    swprintf(buf, 24, L" %d %% ", percent);

    GetCursorPos(&pt);
    pt.x += 20;
    pt.y -= 40;

    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = sizeof(ti);
    ti.hwnd     = parent;
    ti.uId      = 0;
    ti.lpszText = buf;
    SendMessageW(*phwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    SendMessageW(*phwndTip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
    SendMessageW(*phwndTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
}


static EmuLanguageType getLangType()
{
    LANGID langId = GetSystemDefaultLangID();

    switch (langId) {
        case 0x0404: return EMU_LANG_CHINESETRAD;
        case 0x0804: return EMU_LANG_CHINESESIMP;
        case 0x0c04: return EMU_LANG_CHINESETRAD;
        case 0x1004: return EMU_LANG_CHINESESIMP;
        case 0x1404: return EMU_LANG_CHINESETRAD;
    }

    switch (langId & 0xff) {
        case 0x04: return EMU_LANG_CHINESESIMP;
        case 0x13: return EMU_LANG_DUTCH;
        case 0x09: return EMU_LANG_ENGLISH;
        case 0x0b: return EMU_LANG_FINNISH;
        case 0x0c: return EMU_LANG_FRENCH;
        case 0x07: return EMU_LANG_GERMAN;
        case 0x10: return EMU_LANG_ITALIAN;
        case 0x11: return EMU_LANG_JAPANESE;
        case 0x12: return EMU_LANG_KOREAN;
        case 0x15: return EMU_LANG_POLISH;
        case 0x16: return EMU_LANG_PORTUGUESE;
        case 0x0a: return EMU_LANG_SPANISH;
        case 0x1d: return EMU_LANG_SWEDISH;
        case 0x19: return EMU_LANG_RUSSIAN;
        case 0x03: return EMU_LANG_CATALAN;
    }

    return EMU_LANG_ENGLISH;
}

int canHandleVblankSyncMode() {
    DWORD t1;
    DWORD t2;
    DWORD cnt = 0;
    int currPri;
    
    currPri= GetThreadPriority(GetCurrentThread());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    t1 = t2 = archGetSystemUpTime(100);
    while (t2 == t1) {
        t1 = t2;
        t2 = archGetSystemUpTime(100);
    }
    t1 = t2;
    while (t2 == t1) {
        t1 = t2;
        t2 = archGetSystemUpTime(100);
        cnt++;
    }
    SetThreadPriority(GetCurrentThread(), currPri);

    return cnt > 6000;
}

void centerDialog(HWND hwnd, int noActivate) {
    RECT r1;
    RECT r2;
    int x;
    int y;

    GetWindowRect(GetParent(hwnd), &r1);
    GetWindowRect(hwnd, &r2);

    x = r1.left + (r1.right - r1.left - r2.right + r2.left) / 2;
    y = r1.top  + (r1.bottom - r1.top - r2.bottom + r2.top) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | (noActivate ? SWP_NOACTIVATE : 0));
}

void updateDialogPos(HWND hwnd, int dialogID, int noMove, int noSize) 
{
    Properties* pProperties = propGetGlobalProperties();

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
	RECT r1;
    int x;
    int y;
    int w;
    int h;

    if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        noMove = 1;
        noSize = 1;
    }

    GetWindowRect(GetParent(hwnd), &r1);

	x = r1.left + pProperties->settings.windowPos[dialogID].left; 
    y = r1.top  + pProperties->settings.windowPos[dialogID].top; 
    w =           pProperties->settings.windowPos[dialogID].width; 
    h =           pProperties->settings.windowPos[dialogID].height; 

    if (noMove || 
	   (pProperties->settings.windowPos[dialogID].left == 0 && 
	    pProperties->settings.windowPos[dialogID].top == 0)) {
        RECT r2;
        GetWindowRect(hwnd, &r2);
        x = r1.left + (r1.right - r1.left - r2.right + r2.left) / 2;
        y  = r1.top  + (r1.bottom - r1.top - r2.bottom + r2.top) / 2;
    }
    
    if (w == 0 || h == 0) {
        RECT r2;
        GetWindowRect(hwnd, &r2);
        w = r2.right - r2.left;
        h = r2.bottom - r2.top;
    }
    /* Laurent Halter : Removed because window might be on a secondary display*/
    /*
    if (x + w > screenWidth) {
        x = screenWidth - w;
    }

    if (y + h > screenHeight) {
        y = screenHeight - h;
    }

    if (y < 0) {
        y = 0;
    }*/

    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | (noSize ? SWP_NOSIZE : 0));
}

void saveDialogPos(HWND hwnd, int dialogID)
{
    Properties* pProperties = propGetGlobalProperties();
    RECT r;
    RECT r1;

    if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
        GetWindowRect(hwnd, &r);
        GetWindowRect(GetParent(hwnd), &r1);
        pProperties->settings.windowPos[dialogID].left   = r.left   - r1.left;
        pProperties->settings.windowPos[dialogID].top    = r.top    - r1.top;
        pProperties->settings.windowPos[dialogID].width  = r.right  - r.left;
        pProperties->settings.windowPos[dialogID].height = r.bottom - r.top;
    }
}

///////////////////////////////////////////////////////////////////////////

static BOOL_DLG_RET CALLBACK langDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static int* lang = NULL;

    switch (iMsg) {
    case WM_INITDIALOG:
        {
            char buffer[64];
            HIMAGELIST himlSmall;
            int i;

            lang = (int*)lParam;

            SetWindowTextU(hDlg, langDlgLangTitle());
            SetDlgItemTextU(hDlg, IDC_LANGTXT, langDlgLangLangText());
            SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgOK());
            SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());

            ListView_SetExtendedListViewStyle(GetDlgItem(hDlg, IDC_LANGLIST), LVS_EX_FULLROWSELECT);

            himlSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), TRUE, 1, 1); 

            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_CATALONIA))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_CHINASIMP))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_CHINATRAD))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_NETHERLANDS))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_USA))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_FINLAND))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_FRANCE))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_GERMANY))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_ITALY))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_JAPAN))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_KOREA))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_POLAND))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_BRAZIL))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_RUSSIA))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_SPAIN))); 
            ImageList_AddIcon(himlSmall, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_FLAG_SWEDEN))); 
            
            ListView_SetImageList(GetDlgItem(hDlg, IDC_LANGLIST), himlSmall, LVSIL_SMALL);

            SetFocus(GetDlgItem(hDlg, IDC_LANGLIST));

            /* Insert via LVM_INSERTCOLUMNW / LVM_INSERTITEMW; UTF-8 source
               literals are converted to UTF-16 explicitly. */
            {
                HWND hList = GetDlgItem(hDlg, IDC_LANGLIST);
                wchar_t wbuf[64];
                LVCOLUMNW lvcw = {0};
                LVITEMW lviw = {0};

                SendMessageW(hList, LVM_SETUNICODEFORMAT, TRUE, 0);

                /* Fill the listview client width minus the vertical scrollbar. */
                int colWidth;
                {
                    RECT lr;
                    GetClientRect(hList, &lr);
                    colWidth = lr.right - lr.left - GetSystemMetrics(SM_CXVSCROLL);
                    if (colWidth < 100) colWidth = 100;
                }

                sprintf(buffer, "       %s", langMenuPropsLanguage());
                Utf8ToWide(buffer, wbuf, _countof(wbuf));
                lvcw.mask     = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
                lvcw.fmt      = LVCFMT_LEFT;
                lvcw.cx       = colWidth;
                lvcw.pszText  = wbuf;
                SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvcw);

                for (i = 0; langGetType(i) != EMU_LANG_UNKNOWN; i++) {
                    sprintf(buffer, "   %s", langToName(langGetType(i), 1));
                    Utf8ToWide(buffer, wbuf, _countof(wbuf));
                    lviw.mask    = LVIF_IMAGE | LVIF_TEXT;
                    lviw.iItem   = i;
                    lviw.iImage  = i;
                    lviw.pszText = wbuf;
                    SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lviw);
 
                    if (langGetType(i) == *lang) {
                        ListView_SetItemState(hList, i, LVIS_SELECTED, LVIS_SELECTED);
                    }
                }
            }
            win32CommonApplyDark(hDlg);
            win32CommonCenterOnOwner(hDlg);
            return FALSE;
        }

    case WM_NOTIFY:
        switch (wParam) {
        case IDC_LANGLIST:
            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMACTIVATE) {
                if (ListView_GetSelectedCount(GetDlgItem(hDlg, IDC_LANGLIST))) {
                    int index = ListView_GetNextItem(GetDlgItem(hDlg, IDC_LANGLIST), -1, LVNI_SELECTED);
                    if (index != -1) {
                        SendMessage(hDlg, WM_COMMAND, IDOK, 0);
                    }
                }
            }
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDOK:
            {
                if (ListView_GetSelectedCount(GetDlgItem(hDlg, IDC_LANGLIST))) {
                    int index = ListView_GetNextItem(GetDlgItem(hDlg, IDC_LANGLIST), -1, LVNI_SELECTED);
                    if (index != -1) {
                        *lang = langGetType(index);
                    }
                }
                EndDialog(hDlg, TRUE);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

int langShowDlg(HWND hwnd, int oldLanguage) {
    int lang = oldLanguage;
    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_LANGUAGE), hwnd, langDlgProc, (LPARAM)&lang);
    return lang;
}


///////////////////////////////////////////////////////////////////////////

#define WM_SHOWDSKWIN        (WM_USER + 1248)

#define TIMER_DSKDIALOGSHOW 20

static BOOL_DLG_RET CALLBACK dskProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static int show = 0;

    switch (iMsg) {
    case WM_INITDIALOG:
        centerDialog(hDlg, 0);
        win32CommonApplyDark(hDlg);
        return FALSE;

    case WM_SHOWDSKWIN:
        {
            Properties* pProperties = propGetGlobalProperties();
            RECT r1;
            RECT r2;
            int x;
            int y;

            GetWindowRect(GetParent(hDlg), &r1);
            GetWindowRect(hDlg, &r2);

            x = r1.left + (r1.right - r1.left - r2.right + r2.left) / 2;
            y = r1.top  + (r1.bottom - r1.top - r2.bottom + r2.top) / 2;

            {
                /* fileNameInZip is the zip entry in the zip's stored encoding
                ** (typically ACP / CP932). Pass it through AnyToUtf8 so the
                ** Unicode SetWindowTextW path renders the entry name correctly. */
                char displayName[512];
                const char* src = stripPath(*pProperties->media.disks[0].fileNameInZip ?
                                            pProperties->media.disks[0].fileNameInZip :
                                            pProperties->media.disks[0].fileName);
                AnyToUtf8(src, displayName, sizeof(displayName));
                SetWindowTextU(GetDlgItem(hDlg, IDC_DISKIMAGE), displayName);
            }
            if (!show) {
                enterDialogShow();
                SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                show = 1;
            }
            SetTimer(hDlg, TIMER_DSKDIALOGSHOW, 1000, NULL);
            
            centerDialog(hDlg, 1);
        }
        return TRUE;

    case WM_TIMER:
        ShowWindow(hDlg, SW_HIDE);
        KillTimer(hDlg, TIMER_DSKDIALOGSHOW);
    
        show = 0;
        exitDialogShow();
        return TRUE;
    }
    return FALSE;
}

typedef void* DiskQuickviewWindow;

DiskQuickviewWindow* diskQuickviewWindowCreate(HWND parent) {
    return (DiskQuickviewWindow*)CreateDialog(GetModuleHandle(0), MAKEINTRESOURCE(IDD_DISKIMAGE), parent, dskProc);
}

void diskQuickviewWindowDestroy(DiskQuickviewWindow* dqw)
{
    DestroyWindow((HWND)dqw);
}

void diskQuickviewWindowShow(DiskQuickviewWindow* dqw)
{
    SendMessage((HWND)dqw, WM_SHOWDSKWIN, 0, 0);
}

///////////////////////////////////////////////////////////////////////////

typedef struct {
    char        title[128];
    char        description[128];
    const char* fileList;
    int         fileListCount;
    int         autoReset;
    char        selectFile[512];
    char        zipFileName[512];
    int         selectFileIndex;
    RomType     openRomType;
} ZipFileDlgInfo;


static void updateRomTypeList(HWND hDlg, ZipFileDlgInfo* dlgInfo) {
    char fileName[MAX_PATH];
    int size;
    char* buf = NULL;
    int index;

    {
        /* LBS_SORT: convert sorted-display row to raw fileList index via the
        ** item data we bound at insertion. */
        LRESULT row = SendDlgItemMessage(hDlg, IDC_DSKLIST, LB_GETCURSEL, 0, 0);
        LRESULT rawIdx = (row == LB_ERR) ? LB_ERR :
            SendDlgItemMessage(hDlg, IDC_DSKLIST, LB_GETITEMDATA, (WPARAM)row, 0);
        index = (int)row;
        if (rawIdx == LB_ERR || rawIdx < 0 || rawIdx >= dlgInfo->fileListCount) {
            fileName[0] = 0;
        } else {
            const char* p = dlgInfo->fileList;
            int i;
            for (i = 0; i < (int)rawIdx; i++) p += strlen(p) + 1;
            strncpy(fileName, p, sizeof(fileName) - 1);
            fileName[sizeof(fileName) - 1] = 0;
        }
    }

    if (isFileExtension(fileName, ".rom") || isFileExtension(fileName, ".ri") ||
        isFileExtension(fileName, ".mx1") || isFileExtension(fileName, ".mx2") || 
        isFileExtension(fileName, ".sms") || isFileExtension(fileName, ".col") ||
        isFileExtension(fileName, ".sg") || isFileExtension(fileName, ".sc")) {
        buf = romLoad(dlgInfo->zipFileName, fileName, &size);
    }

    if (buf != NULL) {
        MediaType* mediaType = mediaDbLookupRom(buf, size);
        RomType romType = mediaType != NULL ? mediaDbGetRomType(mediaType) : ROM_UNKNOWN;
        int idx = 0;

        while (opendialog_getromtype(idx) != romType) {
            idx++;
        }

        SendMessage(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), CB_SETCURSEL, idx, 0);

        free(buf);

        dlgInfo->openRomType = romType;

        EnableWindow(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), 1);
        EnableWindow(GetDlgItem(hDlg, IDC_OPEN_ROMTEXT), 1);
    }    
    else {
        dlgInfo->openRomType = ROM_UNKNOWN;
        EnableWindow(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), 0);
        EnableWindow(GetDlgItem(hDlg, IDC_OPEN_ROMTEXT), 0);
        SendMessage(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), CB_SETCURSEL, -1, 0);
    }
}

static BOOL_DLG_RET CALLBACK dskZipDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static ZipFileDlgInfo* dlgInfo;

    switch (iMsg) {
    case WM_DESTROY:
        saveDialogPos(hDlg, DLG_ID_ZIPOPEN);
        return 0;

    case WM_INITDIALOG:
        {
            const char* fileList;
            int sel = 0;
            int i;
            
            updateDialogPos(hDlg, DLG_ID_ZIPOPEN, 0, 1);

            dlgInfo = (ZipFileDlgInfo*)lParam;

            dlgInfo->openRomType = ROM_UNKNOWN;

            SetWindowTextU(hDlg, dlgInfo->title);

            SetDlgItemTextU(hDlg, IDC_DSKLOADTXT, dlgInfo->description);
            SetWindowTextU(GetDlgItem(hDlg, IDC_DSKRESET), langDlgZipReset());
            SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgOK());
            SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());
            SetWindowTextU(GetDlgItem(hDlg, IDC_OPEN_ROMTEXT), langDlgRomType());

            fileList = dlgInfo->fileList;

            for (i = 0; opendialog_getromtype(i) != ROM_UNKNOWN; i++) {
                ComboAddStringU(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), romTypeToString(opendialog_getromtype(i)));
                SendDlgItemMessage(hDlg, IDC_ROMTYPE, CB_SETCURSEL, i, 0);
            }
            ComboAddStringU(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), romTypeToString(ROM_UNKNOWN));
            EnableWindow(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), 0);
            EnableWindow(GetDlgItem(hDlg, IDC_OPEN_ROMTEXT), 0);

            if (dlgInfo->selectFileIndex != -1) {
                sel = dlgInfo->selectFileIndex;
            }

            for (i = 0; i < dlgInfo->fileListCount; i++) {
                /* Zip entries are typically the host ACP on legacy archives.
                ** AnyToUtf8 leaves valid UTF-8 alone (modern EFS-flagged zips). */
                char displayName[512];
                LRESULT row;
                AnyToUtf8(fileList, displayName, sizeof(displayName));
                row = ListBoxAddStringU(GetDlgItem(hDlg, IDC_DSKLIST), displayName);
                /* IDC_DSKLIST has LBS_SORT, so the inserted row index is not i.
                ** Bind raw fileList index to the row so IDOK can recover the
                ** original (ACP) bytes for unzLocateFile. */
                if (row != LB_ERR) {
                    SendDlgItemMessage(hDlg, IDC_DSKLIST, LB_SETITEMDATA, (WPARAM)row, (LPARAM)i);
                    if (dlgInfo->selectFileIndex != -1 && 0 == strcmp(dlgInfo->selectFile, fileList)) {
                        sel = (int)row;
                    }
                }
                fileList += strlen(fileList) + 1;
            }

            if (dlgInfo->autoReset == -1) {
                ShowWindow(GetDlgItem(hDlg, IDC_DSKRESET), SW_HIDE);
            }
            else {
                SendMessage(GetDlgItem(hDlg, IDC_DSKRESET), BM_SETCHECK, dlgInfo->autoReset ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            SendDlgItemMessage(hDlg, IDC_DSKLIST, LB_SETCURSEL, sel, 0);
        
            updateRomTypeList(hDlg, dlgInfo);

            win32CommonApplyDark(hDlg);
            return FALSE;
        }

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDC_OPEN_ROMTYPE:
            if (HIWORD(wParam) == 1 || HIWORD(wParam) == 2) {
                int idx = SendMessage(GetDlgItem(hDlg, IDC_OPEN_ROMTYPE), CB_GETCURSEL, 0, 0);

                dlgInfo->openRomType = idx == CB_ERR ? -1 : opendialog_getromtype(idx);
            }
            return 0;
        case IDC_DSKRESET:
            if (dlgInfo->autoReset == 1) {
                SendMessage(GetDlgItem(hDlg, IDC_DSKRESET), BM_SETCHECK, BST_UNCHECKED, 0);
                dlgInfo->autoReset = 0;
            }
            else if (dlgInfo->autoReset == 0) {
                SendMessage(GetDlgItem(hDlg, IDC_DSKRESET), BM_SETCHECK, BST_CHECKED, 1);
                dlgInfo->autoReset = 1;
            }
            break;
        case IDC_DSKLIST:
            switch (HIWORD(wParam)) {
            case LBN_SELCHANGE:
                updateRomTypeList(hDlg, dlgInfo);
                break;
            }

            if (HIWORD(wParam) != LBN_DBLCLK) {
                break;
            }
            // else, fall through
        case IDOK:
            {
                /* LBS_SORT means LB_GETCURSEL returns a sorted-display row, not
                ** the insertion index.  Recover the raw fileList index from
                ** the item data we bound at insertion time. */
                LRESULT row = SendMessage(GetDlgItem(hDlg, IDC_DSKLIST), LB_GETCURSEL, 0, 0);
                LRESULT rawIdx = (row == LB_ERR) ? LB_ERR :
                    SendMessage(GetDlgItem(hDlg, IDC_DSKLIST), LB_GETITEMDATA, (WPARAM)row, 0);
                dlgInfo->selectFileIndex = (int)row;
                if (rawIdx == LB_ERR || rawIdx < 0 || rawIdx >= dlgInfo->fileListCount) {
                    dlgInfo->selectFile[0] = '\0';
                } else {
                    const char* p = dlgInfo->fileList;
                    int i;
                    for (i = 0; i < (int)rawIdx; i++) p += strlen(p) + 1;
                    strncpy(dlgInfo->selectFile, p, sizeof(dlgInfo->selectFile) - 1);
                    dlgInfo->selectFile[sizeof(dlgInfo->selectFile) - 1] = 0;
                }
            }
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            dlgInfo->selectFileIndex = -1;
            dlgInfo->selectFile[0] = '\0';
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        dlgInfo->selectFileIndex = -1;
        dlgInfo->selectFile[0] = '\0';
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

////////////////////////////////////////////////////////////////////

static char* convertTapePos(int tapePos)
{
    static char str[64];
    int pos = tapePos / 128;

    sprintf(str, "%dh %02dm %02ds", pos / 3600, (pos / 60) % 60, pos % 60);
    return str;
}

static void tapeDlgUpdate(HWND hwnd, TapeContent* tc, int tcCount, int showCustomFiles) 
{
    static char* typeNames[] = { "ASCII", "BIN", "BAS", "Custom" };
    int curPos;
    int i;
    int idx = 0;

    ListView_DeleteAllItems(hwnd);

    curPos = tapeGetCurrentPos();

    for (i = 0; i < tcCount; i++) {
        wchar_t wbuf[512] = {0};
        LVITEMW lviw = {0};

        if (showCustomFiles || tc[i].type != TAPE_CUSTOM) {
            lviw.mask       = LVIF_TEXT;
            lviw.iItem      = idx;
            lviw.pszText    = wbuf;
            lviw.cchTextMax = _countof(wbuf);
            
            Utf8ToWide(convertTapePos(tc[i].pos), wbuf, _countof(wbuf));
            SendMessageW(hwnd, LVM_INSERTITEMW, 0, (LPARAM)&lviw);
            lviw.iSubItem++;
            
            Utf8ToWide(typeNames[tc[i].type], wbuf, _countof(wbuf));
            SendMessageW(hwnd, LVM_SETITEMW, 0, (LPARAM)&lviw);
            lviw.iSubItem++;
            
            Utf8ToWide(tc[i].fileName, wbuf, _countof(wbuf));
            SendMessageW(hwnd, LVM_SETITEMW, 0, (LPARAM)&lviw);

            if (tc[i].pos <= curPos) {
                SetFocus(hwnd);
                ListView_SetItemState(hwnd, idx, LVIS_SELECTED, LVIS_SELECTED);
            }
            idx++;
        }
    }
}


static BOOL_DLG_RET CALLBACK tapePosDlg(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static int currIndex;
    static HWND hwnd;
    static TapeContent* tc;
    static int* showCustomFiles;
    static int tcCount;

    switch (iMsg) {
    case WM_DESTROY:
        saveDialogPos(hDlg, DLG_ID_TAPEPOS);
        return 0;

    case WM_INITDIALOG:
        {
            showCustomFiles = (int*)lParam;
         
            updateDialogPos(hDlg, DLG_ID_TAPEPOS, 0, 1);

            currIndex = -1;

            SetWindowTextU(hDlg, langDlgTapeTitle());

            SetDlgItemTextU(hDlg, IDC_SETTAPEPOSTXT, langDlgTapeSetPosText());
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTAPECUSTOM), langDlgTapeCustom());
            SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgOK());
            SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());

            SendMessage(GetDlgItem(hDlg, IDC_SETTAPECUSTOM), BM_SETCHECK, *showCustomFiles ? BST_CHECKED : BST_UNCHECKED, 0);

            tc = tapeGetContent(&tcCount);
    
            hwnd = GetDlgItem(hDlg, IDC_SETTAPELIST);

            EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);

            ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT);
            SendMessageW(hwnd, LVM_SETUNICODEFORMAT, TRUE, 0);

            {
                wchar_t wbuf[64];
                LVCOLUMNW lvcw = {0};
                lvcw.mask     = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
                lvcw.fmt      = LVCFMT_LEFT;
                lvcw.pszText  = wbuf;
                Utf8ToWide(langDlgTabPosition(), wbuf, _countof(wbuf));
                lvcw.cx = 95;
                SendMessageW(hwnd, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvcw);
                Utf8ToWide(langDlgTabType(), wbuf, _countof(wbuf));
                lvcw.cx = 65;
                SendMessageW(hwnd, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvcw);
                Utf8ToWide(langDlgTabFilename(), wbuf, _countof(wbuf));
                lvcw.cx = 105;
                SendMessageW(hwnd, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvcw);
            }
        }

        tapeDlgUpdate(hwnd, tc, tcCount, *showCustomFiles);
        win32CommonApplyDark(hDlg);
        return FALSE;

    case WM_NOTIFY:
        switch (wParam) {
        case IDC_SETTAPELIST:
            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMCHANGED) {
                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);

                    if (currIndex == -1 && index != -1) {
                        EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
                    }
                    currIndex = index;
                }
                else {
                    if (currIndex != -1) {
                        EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
                    }
                    currIndex = -1;
                }
            }
            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMACTIVATE) {
                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
                    SendMessage(hDlg, WM_COMMAND, IDOK, 0);
                }
                return TRUE;
            }
        }
        break;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDC_SETTAPECUSTOM:
            {
                *showCustomFiles = BST_CHECKED == SendMessage(GetDlgItem(hDlg, IDC_SETTAPECUSTOM), BM_GETCHECK, 0, 0);
                tapeDlgUpdate(GetDlgItem(hDlg, IDC_SETTAPELIST), tc, tcCount, *showCustomFiles);
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;

        case IDOK:
            {
                int index = 0;
                int i;

                if (ListView_GetSelectedCount(hwnd)) {
                    currIndex = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
                }

                for (i = 0; i < tcCount; i++) {
                    if (*showCustomFiles || tc[i].type != TAPE_CUSTOM) {
                        if (currIndex == index) {
                            tapeSetCurrentPos(tc[i].pos);
                        }
                        index++;
                    }
                }
             
                EndDialog(hDlg, TRUE);
            }
            return TRUE;
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

void setTapePosition(HWND parent, Properties* pProperties) {
    if (emulatorGetState() != EMU_STOPPED) {
        emulatorSuspend();
    }
    else {
        tapeSetReadOnly(1);
        boardChangeCassette(0, strlen(pProperties->media.tapes[0].fileName) ? pProperties->media.tapes[0].fileName : NULL, 
                            strlen(pProperties->media.tapes[0].fileNameInZip) ? pProperties->media.tapes[0].fileNameInZip : NULL);
    }

    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETTAPEPOS), parent, tapePosDlg, (LPARAM)&pProperties->cassette.showCustomFiles);

    if (emulatorGetState() != EMU_STOPPED) {
        emulatorResume();
    }
    else {
        boardChangeCassette(0, NULL, NULL);
        tapeSetReadOnly(pProperties->cassette.readOnly);
    }
}

///////////////////////////////////////////////////////////////////////////

#define hotkeyEq(hotkey1, hotkey2) (*(DWORD*)&hotkey1 == *(DWORD*)&hotkey2)

static int maxSpeedIsSet = 0;
static int reverseIsSet  = 0;

static void checkKeyDown(Shortcuts* s, ShotcutHotkey key) {
    if (hotkeyEq(key, s->emuSpeedFull)) {
        actionMaxSpeedSet();
        maxSpeedIsSet = 1;
    }
    if (hotkeyEq(key, s->emuPlayReverse)) {
        actionStartPlayReverse();
        reverseIsSet = 1;
    }
}

static void checkKeyUp(Shortcuts* s, ShotcutHotkey key) 
{
    if (maxSpeedIsSet) {
        actionMaxSpeedRelease();
        maxSpeedIsSet = 0;
    }

    if (reverseIsSet) {
        actionStopPlayReverse();
        reverseIsSet = 0;
    }

    if (hotkeyEq(key, s->spritesEnable))                actionToggleSpriteEnable();
    if (hotkeyEq(key, s->fdcTiming))                    actionToggleFdcTiming();
    if (hotkeyEq(key, s->noSpriteLimits))               actionToggleNoSpriteLimits();
    if (hotkeyEq(key, s->msxKeyboardQuirk))             actionToggleMsxKeyboardQuirk();
    if (hotkeyEq(key, s->msxAudioSwitch))               actionToggleMsxAudioSwitch();
    if (hotkeyEq(key, s->frontSwitch))                  actionToggleFrontSwitch();
    if (hotkeyEq(key, s->pauseSwitch))                  actionTogglePauseSwitch();
    if (hotkeyEq(key, s->quit))                         actionQuit();
    if (hotkeyEq(key, s->wavCapture))                   actionToggleWaveCapture();
    if (hotkeyEq(key, s->videoCapLoad))                 actionVideoCaptureLoad();
    if (hotkeyEq(key, s->videoCapPlay))                 actionVideoCapturePlay();
    if (hotkeyEq(key, s->videoCapRec))                  actionVideoCaptureRec();
    if (hotkeyEq(key, s->videoCapStop))                 actionVideoCaptureStop();
    if (hotkeyEq(key, s->videoCapSave))                 actionVideoCaptureSave();
    if (hotkeyEq(key, s->screenCapture))                actionScreenCapture();
    if (hotkeyEq(key, s->screenCaptureUnfilteredSmall)) actionScreenCaptureUnfilteredSmall();
    if (hotkeyEq(key, s->screenCaptureUnfilteredLarge)) actionScreenCaptureUnfilteredLarge();
    if (hotkeyEq(key, s->cpuStateLoad))                 actionLoadState();
    if (hotkeyEq(key, s->cpuStateSave))                 actionSaveState();
    if (hotkeyEq(key, s->cpuStateQuickLoad))            actionQuickLoadState();
    if (hotkeyEq(key, s->cpuStateQuickSave))            actionQuickSaveState();
    if (hotkeyEq(key, s->cpuStateQuickSaveUndo))        actionQuickSaveStateUndo();

    if (hotkeyEq(key, s->cartInsert[0]))                actionCartInsert1();
    if (hotkeyEq(key, s->cartInsert[1]))                actionCartInsert2();
    if (hotkeyEq(key, s->cartSpecialMenu[0]))           actionMenuSpecialCart1(-1, -1);
    if (hotkeyEq(key, s->cartSpecialMenu[1]))           actionMenuSpecialCart2(-1, -1);
    if (hotkeyEq(key, s->cartRemove[0]))                actionCartRemove1();
    if (hotkeyEq(key, s->cartRemove[1]))                actionCartRemove2();
    if (hotkeyEq(key, s->cartAutoReset[0]))             actionToggleCartAutoReset();

    if (hotkeyEq(key, s->diskInsert[0]))                actionDiskInsertA();
    if (hotkeyEq(key, s->diskInsert[1]))                actionDiskInsertB();
    if (hotkeyEq(key, s->diskDirInsert[0]))             actionDiskDirInsertA();
    if (hotkeyEq(key, s->diskDirInsert[1]))             actionDiskDirInsertB();
    if (hotkeyEq(key, s->diskChange[0]))                actionDiskQuickChange();
    if (hotkeyEq(key, s->diskRemove[0]))                actionDiskRemoveA();
    if (hotkeyEq(key, s->diskRemove[1]))                actionDiskRemoveB();
    if (hotkeyEq(key, s->diskAutoReset[0]))             actionToggleDiskAutoReset();

    if (hotkeyEq(key, s->casInsert))                    actionCasInsert();
    if (hotkeyEq(key, s->casRewind))                    actionCasRewind();
    if (hotkeyEq(key, s->casSetPos))                    actionCasSetPosition();
    if (hotkeyEq(key, s->casRemove))                    actionCasRemove();
    if (hotkeyEq(key, s->casToggleReadonly))            actionCasToggleReadonly();
    if (hotkeyEq(key, s->casAutoRewind))                actionToggleCasAutoRewind();
    if (hotkeyEq(key, s->casSave))                      actionCasSave();

    if (hotkeyEq(key, s->prnFormFeed))                  actionPrinterForceFormFeed();
    if (hotkeyEq(key, s->mouseLockToggle))              actionToggleMouseCapture();
    if (hotkeyEq(key, s->emulationRunPause))            actionEmuTogglePause();
    if (hotkeyEq(key, s->emulationStop))                actionEmuStop();
    if (hotkeyEq(key, s->emuSpeedNormal))               actionEmuSpeedNormal();
    if (hotkeyEq(key, s->emuSpeedInc))                  actionEmuSpeedIncrease();
    if (hotkeyEq(key, s->emuSpeedToggle))               actionMaxSpeedToggle();
    if (hotkeyEq(key, s->emuSpeedDec))                  actionEmuSpeedDecrease();
    if (hotkeyEq(key, s->windowSize1x))                 actionWindowSize1x();
    if (hotkeyEq(key, s->windowSize2x))                 actionWindowSize2x();
    if (hotkeyEq(key, s->windowSize3x))                 actionWindowSize3x();
    if (hotkeyEq(key, s->windowSize4x))                 actionWindowSize4x();
    if (hotkeyEq(key, s->windowSize5x))                 actionWindowSize5x();
    if (hotkeyEq(key, s->windowSize6x))                 actionWindowSize6x();
    if (hotkeyEq(key, s->windowSize7x))                 actionWindowSize7x();
    if (hotkeyEq(key, s->windowSize8x))                 actionWindowSize8x();
    if (hotkeyEq(key, s->windowSizeMinimized))          actionWindowSizeMinimized();
    if (hotkeyEq(key, s->windowSizeFullscreen))         actionWindowSizeFullscreen();
    if (hotkeyEq(key, s->windowSizeFullscreenToggle))   actionFullscreenToggle();
    if (hotkeyEq(key, s->resetSoft))                    actionEmuResetSoft();
    if (hotkeyEq(key, s->resetHard))                    actionEmuResetHard();
    if (hotkeyEq(key, s->resetClean))                   actionEmuResetClean();
    if (hotkeyEq(key, s->volumeIncrease))               actionVolumeIncrease();
    if (hotkeyEq(key, s->volumeDecrease))               actionVolumeDecrease();
    if (hotkeyEq(key, s->volumeMute))                   actionMuteToggleMaster();
    if (hotkeyEq(key, s->volumeStereo))                 actionVolumeToggleStereo();
    if (hotkeyEq(key, s->themeSwitch))                  actionNextTheme();
    if (hotkeyEq(key, s->propShowEmulation))            actionPropShowEmulation();
    if (hotkeyEq(key, s->propShowVideo))                actionPropShowVideo();
    if (hotkeyEq(key, s->propShowAudio))                actionPropShowAudio();
    if (hotkeyEq(key, s->propShowSettings))             actionPropShowSettings();
    if (hotkeyEq(key, s->propShowApearance))            actionPropShowApearance();
    if (hotkeyEq(key, s->propShowPorts))                actionPropShowPorts();
    if (hotkeyEq(key, s->propShowEffects))				actionPropShowEffects();
    if (hotkeyEq(key, s->optionsShowLanguage))          actionOptionsShowLanguage();
    if (hotkeyEq(key, s->toolsShowMachineEditor))       actionToolsShowMachineEditor();
    if (hotkeyEq(key, s->toolsShowShorcutEditor))       actionToolsShowShorcutEditor();
    if (hotkeyEq(key, s->toolsShowKeyboardEditor))      actionToolsShowKeyboardEditor();
    if (hotkeyEq(key, s->toolsShowMixer))               actionToolsShowMixer();
    if (hotkeyEq(key, s->toolsShowDebugger))            actionToolsShowDebugger();
    if (hotkeyEq(key, s->toolsShowTrainer))             actionToolsShowTrainer();
    if (hotkeyEq(key, s->helpShowHelp))                 actionHelpShowHelp();
    if (hotkeyEq(key, s->helpShowAbout))                actionHelpShowAbout();
}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////


#define WM_UPDATE            (WM_USER + 1245)
#define WM_LAUNCHFILE        (WM_USER + 1249)

#define TIMER_STATUSBAR_UPDATE              10
#define TIMER_POLL_INPUT                    11
#define TIMER_POLL_FRAMECOUNT               12
#define TIMER_SCREENUPDATE                  13
#define TIMER_SCREENSHOT                    14
#define TIMER_THEME                         17
#define TIMER_MENUUPDATE                    18
#define TIMER_CLIP_REGION                   19
#define TIMER_FULLSCREEN_MENU               21

void  PatchDiskSetBusy(int driveId, int busy);

void updateMenu(int show);

typedef void (*KbdLockFun)(); 

KbdLockFun kbdLockEnable = NULL;
KbdLockFun kbdLockDisable = NULL;

static Properties* pProperties;

typedef struct {
    HWND emuHwnd;
    HWND hwnd;
    HRGN hrgn;
    int      clipAlways;
    int      rgnSize;
    RGNDATA* rgnData;
    int      rgnSizeOrig;
    RGNDATA* rgnDataOrig;
    int      rgnEnable;
    HMENU hMenu;
    int showMenu;
    int trackMenu;
    int showDialog;
    BITMAPINFO bmInfo;
    DiskQuickviewWindow dskWnd;
    
    int active;
    //
    void* bmBitsGDI;
    int frameCount;
    int framesPerSecond;
    char pCurDir[MAX_PATH];
    Video* pVideo;
    int minimized;
    Mixer* mixer;
    int enteringFullscreen;
    Shortcuts* shortcuts;
    DWORD buttonState;

    HANDLE ddrawEvent;
    HANDLE ddrawAckEvent;
    int    diplayUpdated;
    int    diplaySync;
    int    diplayUpdateOnVblank;

    int renderVideo;

    HBITMAP hBitmap;
    HDC hdc;
    ThemePage* themePageActive;
    ThemeCollection** themeList;
    int themeIndex;
    HWND currentHwnd; // Used for arch specific theme events
    POINT currentHwndMouse;
    RECT currentHwndRect;


	//Precalc vars
	int clientWidth;
	int clientHeight;

	HWND hwndSliderTip;  /* lazily created tracking tooltip for sliders */
} WinState;



#define WIDTH  320
#define HEIGHT 240

#define LAUNCH_TEMP_FILE "launch.tmp"

static WinState st;


static void registerFileTypes() {
    registerFileType(".dsk", "blueMSXdsk", "DSK Image", 1);
    registerFileType(".di1", "blueMSXdsk", "DSK Image", 1);
    registerFileType(".di2", "blueMSXdsk", "DSK Image", 1);
    registerFileType(".360", "blueMSXdsk", "DSK Image", 1);
    registerFileType(".720", "blueMSXdsk", "DSK Image", 1);
    registerFileType(".sf7", "blueMSXdsk", "DSK Image", 1);
    registerFileType(".rom", "blueMSXrom", "ROM Image", 2);
    registerFileType(".ri",  "blueMSXrom", "ROM Image", 2);
    registerFileType(".mx1", "blueMSXrom", "ROM Image", 2);
    registerFileType(".mx2", "blueMSXrom", "ROM Image", 2);
    registerFileType(".sms", "blueMSXrom", "Sega ROM Image", 2);
    registerFileType(".col", "blueMSXrom", "Coleco ROM Image", 2);
    registerFileType(".sg",  "blueMSXrom", "Sega ROM Image", 2);
    registerFileType(".sc",  "blueMSXrom", "Sega ROM Image", 2);
    registerFileType(".cas", "blueMSXcas", "CAS Image", 3);
    registerFileType(".sta", "blueMSXsta", "blueMSX State", 4);
    registerFileType(".cap", "blueMSXcap", "blueMSX Video Capture", 4);
}

static void unregisterFileTypes() {
    unregisterFileType(".dsk", "blueMSXdsk", "DSK Image", 1);
    unregisterFileType(".di1", "blueMSXdsk", "DSK Image", 1);
    unregisterFileType(".di2", "blueMSXdsk", "DSK Image", 1);
    unregisterFileType(".360", "blueMSXdsk", "DSK Image", 1);
    unregisterFileType(".720", "blueMSXdsk", "DSK Image", 1);
    unregisterFileType(".sf7", "blueMSXdsk", "DSK Image", 1);
    unregisterFileType(".rom", "blueMSXrom", "ROM Image", 2);
    unregisterFileType(".ri",  "blueMSXrom", "ROM Image", 2);
    unregisterFileType(".mx1", "blueMSXrom", "ROM Image", 2);
    unregisterFileType(".mx2", "blueMSXrom", "ROM Image", 2);
    unregisterFileType(".sms", "blueMSXrom", "Sega ROM Image", 2);
    unregisterFileType(".col", "blueMSXrom", "Coleco ROM Image", 2);
    unregisterFileType(".sg",  "blueMSXrom", "Sega ROM Image", 2);
    unregisterFileType(".sc",  "blueMSXrom", "Sega ROM Image", 2);
    unregisterFileType(".cas", "blueMSXcas", "CAS Image", 3);
    unregisterFileType(".sta", "blueMSXsta", "blueMSX State", 4);
    unregisterFileType(".cap", "blueMSXcap", "blueMSX Video Capture", 4);
}

HWND getMainHwnd()
{
    return st.hwnd;
}

void archShowPropertiesDialog(PropPage  startPane) {
    Properties oldProp = *pProperties;
    int restart = 0;
    int changed;
    int i;

    emulatorSetFrequency(50, NULL);
    enterDialogShow();
    changed = showProperties(pProperties, st.hwnd, startPane, st.mixer, st.pVideo);
    exitDialogShow();
    emulatorSetFrequency(pProperties->emulation.speed, NULL);
    if (!changed) {
        return;
    }

    /* Save properties */
    propSave(pProperties);

    /* Always update video render */
    
    videoUpdateAll(st.pVideo, pProperties);
    
    mediaDbSetDefaultRomType(pProperties->cartridge.defaultType);

    printerIoSetType(pProperties->ports.Lpt.type, pProperties->ports.Lpt.fileName);
    uartIoSetType(pProperties->ports.Com.type, pProperties->ports.Com.fileName);
    midiIoSetMidiOutType(pProperties->sound.MidiOut.type, pProperties->sound.MidiOut.fileName);
    midiIoSetMidiInType(pProperties->sound.MidiIn.type, pProperties->sound.MidiIn.fileName);
    ykIoSetMidiInType(pProperties->sound.YkIn.type, pProperties->sound.YkIn.fileName);
    midiEnableMt32ToGmMapping(pProperties->sound.MidiOut.mt32ToGm);
    midiInSetChannelFilter(pProperties->sound.YkIn.channel);

    /* Update window size only if changed */
    if (pProperties->video.driver != oldProp.video.driver ||
        pProperties->video.fullscreen.width != oldProp.video.fullscreen.width ||
        pProperties->video.fullscreen.height != oldProp.video.fullscreen.height ||
        pProperties->video.fullscreen.bitDepth != oldProp.video.fullscreen.bitDepth ||
        pProperties->video.windowSize != oldProp.video.windowSize ||
        pProperties->video.horizontalStretch != oldProp.video.horizontalStretch ||
        pProperties->video.verticalStretch != oldProp.video.verticalStretch ||
        strcmp(pProperties->settings.themeName, oldProp.settings.themeName))
    {
        archUpdateWindow();
    }

    if (pProperties->cartridge.defaultType != oldProp.cartridge.defaultType) {
        for (i = 0; i < PROP_MAX_CARTS; i++) {
            if (pProperties->media.carts[i].fileName[0]) insertCartridge(pProperties, i, pProperties->media.carts[i].fileName, pProperties->media.carts[i].fileNameInZip, pProperties->media.carts[i].type, -1);
        }
    }
    /* Must restart MSX if Machine configuration changed */
    if (strcmp(oldProp.emulation.machineName, pProperties->emulation.machineName) ||
        oldProp.emulation.syncMethod != pProperties->emulation.syncMethod ||
        oldProp.emulation.vdpSyncMode != pProperties->emulation.vdpSyncMode)
    {
        restart = 1;
    }

    boardSetFdcTimingEnable(pProperties->emulation.enableFdcTiming);
    boardSetNoSpriteLimits(pProperties->emulation.noSpriteLimits);

    /* Update switches */
    switchSetAudio(pProperties->emulation.audioSwitch);
    switchSetFront(pProperties->emulation.frontSwitch);
    switchSetPause(pProperties->emulation.pauseSwitch);
    emulatorSetFrequency(pProperties->emulation.speed, NULL);

    /* Update sound only if changed, Must restart if changed */
    if (oldProp.sound.bufSize              != pProperties->sound.bufSize ||
        oldProp.sound.driver               != pProperties->sound.driver  ||
        oldProp.sound.chip.enableY8950     != pProperties->sound.chip.enableY8950 ||
        oldProp.sound.chip.enableYM2413    != pProperties->sound.chip.enableYM2413 ||
        oldProp.sound.chip.enableMoonsound != pProperties->sound.chip.enableMoonsound ||
        oldProp.sound.stereo               != pProperties->sound.stereo) 
    {
        soundDriverConfig(st.hwnd, pProperties->sound.driver);
        emulatorRestartSound();
    }

    if (oldProp.sound.chip.enableY8950     != pProperties->sound.chip.enableY8950 ||
        oldProp.sound.chip.enableYM2413    != pProperties->sound.chip.enableYM2413 ||
        oldProp.sound.chip.enableMoonsound != pProperties->sound.chip.enableMoonsound)
    {
        boardSetY8950Enable(pProperties->sound.chip.enableY8950);
        boardSetYm2413Enable(pProperties->sound.chip.enableYM2413);
        boardSetMoonsoundEnable(pProperties->sound.chip.enableMoonsound);
        restart = 1;
    }

    if (oldProp.emulation.syncMethod != pProperties->emulation.syncMethod) {
        /* All sync methods use 3 buffers (FlipViewFrame3); SYNCNONE = 1.
        ** FlipViewFrame4's tween path is incompatible with the D3D12
        ** backend's SM5/SM7 mid-frame transitions. */
        switch(pProperties->emulation.syncMethod) {
        case P_EMU_SYNCNONE:
            frameBufferSetFrameCount(1);
            break;
        default:
            frameBufferSetFrameCount(3);
        }
    }

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerSetChannelTypeVolume(st.mixer, i, pProperties->sound.mixerChannel[i].volume);
        mixerSetChannelTypePan(st.mixer, i, pProperties->sound.mixerChannel[i].pan);
        mixerEnableChannelType(st.mixer, i, pProperties->sound.mixerChannel[i].enable);
    }
    
    if(!pProperties->settings.portable) {
        if (pProperties->emulation.registerFileTypes && !oldProp.emulation.registerFileTypes) {
            registerFileTypes();
        }
        else if (!pProperties->emulation.registerFileTypes) {
            unregisterFileTypes();
        }
    }

    if (pProperties->emulation.disableWinKeys && !oldProp.emulation.disableWinKeys) {
        if (kbdLockEnable && emulatorGetState() == EMU_RUNNING && pProperties->emulation.disableWinKeys) {
            kbdLockEnable();
        }
        else if (kbdLockDisable) {
            kbdLockDisable();
        }
    }
    
    if (pProperties->emulation.priorityBoost && !oldProp.emulation.priorityBoost) {
        if (pProperties->emulation.priorityBoost) {
            SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
        }
        else {
            SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        }
    }

    mixerSetMasterVolume(st.mixer, pProperties->sound.masterVolume);
    mixerEnableMaster(st.mixer, pProperties->sound.masterEnable);

    if (restart) {
        emulatorRestart();
    }

    if (oldProp.settings.disableScreensaver != pProperties->settings.disableScreensaver) {
        POINT pt;
        SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, !pProperties->settings.disableScreensaver, 0, SPIF_SENDWININICHANGE); 
        GetCursorPos(&pt);
        SetCursorPos(pt.x + 1, pt.y);
    }

    updateMenu(0);

    InvalidateRect(st.hwnd, NULL, TRUE);
}


void enterDialogShow() {
    if (pProperties->video.driver != P_VIDEO_DRVGDI) {
        if (emulatorGetState() == EMU_RUNNING) {
            emulatorSuspend();
            DirectXSetGDISurface();
            emulatorResume();
        }
        else {
            DirectXSetGDISurface();
        }
    }
    st.showDialog++;
    if (st.showDialog == 1) {
        emuEnableSynchronousUpdate(0);
        SetTimer(st.hwnd, TIMER_SCREENUPDATE, 20 * (pProperties->video.frameSkip + 1), NULL);
    }
}

void exitDialogShow() {
    st.showDialog--;
    if (st.showDialog == 0) {
        emuEnableSynchronousUpdate(1);
        KillTimer(st.hwnd, TIMER_SCREENUPDATE);
        SetEvent(st.ddrawAckEvent);
    }
}

void updateMenu(int show) {
    int doDelay = show;
    int enableSpecial = 1;
    int emuState = emulatorGetState();

    if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
        show = 1;
    }

    /* DirectXSetGDISurface (DDraw FlipToGDISurface) needs the emulator
       paused; D3D12 / GDI don't, and skipping the suspend avoids an
       audible WASAPI click on every Properties apply. */
    int ddrawNeedsFlip = doDelay
                         && pProperties->video.driver != P_VIDEO_DRVGDI
                         && pProperties->video.driver != P_VIDEO_DRVDIRECTX_D3D12;

    if (ddrawNeedsFlip) {
        emulatorSuspend();
        DirectXSetGDISurface();
    }

    if (boardGetType() != BOARD_MSX) {
        enableSpecial = 0;
    }

    menuUpdate(pProperties, 
               st.shortcuts,
               emuState == EMU_RUNNING, 
               emuState == EMU_STOPPED, 
               mixerIsLogging(st.mixer),
               boardCaptureIsRecording() ? 1 : boardCaptureIsPlaying() ? 2 : 0,
               fileExist(pProperties->filehistory.quicksave, NULL),
               enableSpecial);

    st.showMenu = menuShow(show);

    if (ddrawNeedsFlip) {
        emulatorResume();
    }

    if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        mouseEmuActivate(!show);
    }
}

static void setClipRegion(int enable) {
    if (st.rgnEnable == enable) {
        return;
    }
    if ((!enable || st.rgnData == NULL || st.showDialog) && !st.clipAlways) {
        SetWindowRgn(st.hwnd, NULL, TRUE);
        st.rgnEnable = 0;
    }
    else {
        HRGN hrgn = ExtCreateRegion(NULL, st.rgnSize, st.rgnData);
        SetWindowRgn(st.hwnd, hrgn, TRUE);
        st.rgnEnable = 1;
    }
}

static void updateClipRegion() {
    if (st.rgnData != NULL && st.hrgn != NULL) {
        POINT pt;
        RECT r;

        GetCursorPos(&pt);
        GetWindowRect(st.hwnd, &r);
        setClipRegion(!PtInRegion(st.hrgn, pt.x - r.left, pt.y - r.top));
    }
}

static void checkClipRegion() {
    if (st.rgnData != NULL && st.hrgn != NULL && !st.clipAlways) {
        POINT pt;
        RECT r;

        GetCursorPos(&pt);
        GetWindowRect(st.hwnd, &r);
        if (st.rgnEnable == !PtInRegion(st.hrgn, pt.x - r.left, pt.y - r.top)) {
            SetTimer(st.hwnd, TIMER_CLIP_REGION, 500, NULL);
        }
    }
}

// DPI helpers: load Windows 10 1607+ APIs dynamically so the binary
// still runs on older Windows (where Per-Monitor V2 is not active anyway).
static UINT getDpiForWindow(HWND hwnd) {
    typedef UINT (WINAPI *PFN)(HWND);
    static PFN pfn = (PFN)(LONG_PTR)-1;
    if (pfn == (PFN)(LONG_PTR)-1)
        pfn = (PFN)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    return pfn ? pfn(hwnd) : 96;
}

static void adjustWindowRectForDpi(RECT* rc, DWORD style, UINT dpi) {
    typedef BOOL (WINAPI *PFN)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static PFN pfn = (PFN)(LONG_PTR)-1;
    if (pfn == (PFN)(LONG_PTR)-1)
        pfn = (PFN)GetProcAddress(GetModuleHandleA("user32.dll"), "AdjustWindowRectExForDpi");
    if (pfn)
        pfn(rc, style, FALSE, 0, dpi);
    else
        AdjustWindowRect(rc, style, FALSE);
}

/* Composite theme through the 640x480 buffer onto the emu area.  Uses
   MonitorFromWindow to avoid pre-SetWindowPos client size in fullscreen. */
typedef void (*ThemePageDrawFn)(ThemePage*, HDC);
static void themeDrawAllAdapter(ThemePage* page, HDC hdc) {
    themePageDraw(page, hdc, NULL);
}
static void drawThemeOnEmuArea(HWND hwnd, HDC hdc, ThemePageDrawFn drawFn) {
    HDC hMemDC = CreateCompatibleDC(hdc);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hMemDC, st.hBitmap);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    int dstW, dstH;
    if (hMon && GetMonitorInfo(hMon, &mi)) {
        dstW = mi.rcMonitor.right  - mi.rcMonitor.left;
        dstH = mi.rcMonitor.bottom - mi.rcMonitor.top;
    } else {
        RECT r;
        GetClientRect(hwnd, &r);
        dstW = r.right;
        dstH = r.bottom;
    }
    drawFn(st.themePageActive, hMemDC);
    StretchBlt(hdc, 0, 0, dstW, dstH,
               hMemDC, 0, 0,
               st.themePageActive->width,
               st.themePageActive->height, SRCCOPY);
    SelectObject(hMemDC, oldBmp);
    DeleteDC(hMemDC);
}

static int getZoom() {
    if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN && 
        (pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO || 
        pProperties->video.driver == P_VIDEO_DRVDIRECTX))
    {
        DxDisplayMode* ddm = DirectDrawGetDisplayMode();
        return min(min(ddm->width / 320, ddm->height / 240), 8);
    }
    return pProperties->video.windowSize + 1;
}


void themeSet(char* themeName, int forceMatch) {
    int x  = 0;
    int y  = 0;
    int w  = GetSystemMetrics(SM_CXSCREEN);
    int h  = GetSystemMetrics(SM_CYSCREEN);
    int ex = 0;
    int ey = 0;
    int ew = 0;
    int eh = 0;
    HWND z = 0;
    int index;

    if (themeName == NULL) {
        themeName = "";
    }
    
    index = getThemeListIndex(st.themeList, themeName, forceMatch);
    if (index == -1) {
        return;
    }

    if (st.themePageActive) {
        themePageActivate(st.themePageActive, NULL);
    }
    /* Drop the dangling pointer before unloading the theme it belongs to. */
    st.themePageActive = NULL;

    st.rgnEnable = -1;
    setClipRegion(0);
    st.themeIndex = index;

    {
        ThemeCollection* tc = st.themeList[st.themeIndex];
        /* Unload other external themes (~700 bitmaps each) before loading
           the new one; repeated switches otherwise exhaust GDI quota.
           themeList[0] stays loaded as Classic fallback. */
        for (int i = 0; st.themeList[i] != NULL; i++) {
            if (i == 0) continue;
            if (st.themeList[i] == tc) continue;
            themeCollectionUnload(st.themeList[i]);
        }
        /* Lazy parse on first selection.  Must precede themeName write:
           tc->name is the directory placeholder until EnsureLoaded reads
           the XML display name, and writing the placeholder back to INI
           used to silently fall back to Classic on next launch. */
        themeCollectionEnsureLoaded(tc);
        strcpy(pProperties->settings.themeName, tc->name);

        if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
            Theme* page = tc->fullscreen;
            if (page == NULL && st.themeList[0] != NULL) page = st.themeList[0]->fullscreen;
            st.themePageActive = themeGetCurrentPage(page);
        }
        else {
            int zoomIdx = pProperties->video.windowSize + 1;  /* P_VIDEO_SIZEX1..X8 -> 1..8 */
            /* External themes ship zoom[2]+fullscreen; synthesise
               zoom[3..8] from "normal".  z=1 falls back to Classic. */
            if (zoomIdx >= 3 && tc->zoom[zoomIdx] == NULL && tc->zoom[2] != NULL) {
                themeCollectionEnsureZoom(tc, zoomIdx);
            }
            Theme* page = tc->zoom[zoomIdx];
            if (page == NULL) page = tc->zoom[2];                    /* fallback to normal */
            if (page == NULL && st.themeList[0] != NULL) {
                /* Last-resort Classic fallback (typically zoom[1]). */
                page = st.themeList[0]->zoom[zoomIdx];
                if (page == NULL) page = st.themeList[0]->zoom[2];
            }
            st.themePageActive = themeGetCurrentPage(page);
        }
    }

    if (st.themePageActive) {
        themePageActivate(st.themePageActive, st.hwnd);
        themePageSetActive(st.themePageActive, NULL, st.active);
    }

    {
        WINDOWPLACEMENT p;
        LONG w, h;

        GetWindowPlacement(st.hwnd, &p);
        w = p.rcNormalPosition.right - p.rcNormalPosition.left;
        h = p.rcNormalPosition.bottom - p.rcNormalPosition.top;

        menuSetInfo(st.themePageActive->menu.color, st.themePageActive->menu.focusColor, 
                    st.themePageActive->menu.textColor, 
                    st.themePageActive->menu.x, st.themePageActive->menu.y,
                    pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN ? w : st.themePageActive->menu.width, 32);
    }

    if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
        int zoom = getZoom();
        int clientW, clientH;
        x = pProperties->video.windowX;
        y = pProperties->video.windowY;
        DWORD dwStyle = (DWORD)GetWindowLongPtr(st.hwnd, GWL_STYLE);
        ex = st.themePageActive->emuWinX;
        ey = st.themePageActive->emuWinY;
        ew = zoom * WIDTH;
        eh = zoom * HEIGHT;
        // Enclose both the theme bitmap and the emu rect: at zoom>=5 the
        // x2 fallback theme is smaller than the emu extent and would
        // otherwise clip it against the window frame.
        clientW = max((int)st.themePageActive->width,  ex + ew);
        clientH = max((int)st.themePageActive->height, ey + eh);
        {
            RECT rc = { 0, 0, clientW, clientH };
            adjustWindowRectForDpi(&rc, dwStyle, getDpiForWindow(st.hwnd));
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
        }
        z  = HWND_NOTOPMOST;

        if (pProperties->video.windowSize == P_VIDEO_SIZEX2) {
            ew = appConfigGetInt("screen.normal.width", 640);
            eh = appConfigGetInt("screen.normal.height", 480);
        }

        SetWindowPos(st.hwnd, z, x, y, w, h, SWP_SHOWWINDOW);
        SetWindowPos(st.emuHwnd, NULL, ex, ey, ew, eh, SWP_NOZORDER);
    }

    if (st.hBitmap) { DeleteObject(st.hBitmap); st.hBitmap=NULL; }
    if (st.hdc) { ReleaseDC(st.hwnd,st.hdc); st.hdc=NULL; }
    st.hdc=GetDC(st.hwnd);
    if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
        st.hBitmap = CreateCompatibleBitmap(st.hdc, w, h);
    }
    else {
        st.hBitmap = CreateCompatibleBitmap(st.hdc, 640, 480);
    }
    
    if (strcmp(themeName,"Classic")) SetWindowTextU(st.hwnd, "  blueMSX");

    if (st.rgnData != NULL) {
//        SetWindowRgn(st.hwnd, NULL, TRUE);
        free(st.rgnData);
        st.rgnData = NULL;
    }

    if (st.hrgn != NULL) {
        DeleteObject(st.hrgn);
        st.hrgn = NULL;
    }

    if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        st.rgnEnable = -1;
        setClipRegion(0);
    } 
    else {
        int clipCount = st.themePageActive->clipPoint.count;
        st.clipAlways = st.themePageActive->noFrame;

        if (clipCount > 0 || st.clipAlways) {
            int i;
            HRGN hrgn;
            POINT pt[512];
            /* SM_CXFIXEDFRAME is non-DPI-aware 3px; adjustWindowRectForDpi
               gives actual WS_DLGFRAME margins so SetWindowRgn doesn't
               clip the bitmap. */
            int dx, dy;
            {
                RECT frameRc = { 0, 0, 0, 0 };
                DWORD style = (DWORD)GetWindowLongPtr(st.hwnd, GWL_STYLE);
                adjustWindowRectForDpi(&frameRc, style, getDpiForWindow(st.hwnd));
                dx = -frameRc.left;
                dy = -frameRc.top;
            }

            if (clipCount == 0) {
                pt[0].x = 0 + dx;
                pt[0].y = 0 + dy;
                pt[1].x = st.themePageActive->width + dx;
                pt[1].y = 0 + dy;
                pt[2].x = st.themePageActive->width + dx;
                pt[2].y = st.themePageActive->height + dy;
                pt[3].x = 0 + dx;
                pt[3].y = st.themePageActive->height + dy;
                clipCount = 4;
            }
            else {
                for (i = 0; i < clipCount; i++) {
                    ClipPoint cp = st.themePageActive->clipPoint.list[i];
                    pt[i].x = cp.x + dx;
                    pt[i].y = cp.y + dy;
                }
            }

            hrgn = CreatePolygonRgn(pt, clipCount, WINDING);
            st.rgnSize = 0;
            if (hrgn != NULL) {
                st.rgnSize = GetRegionData(hrgn, 0, NULL);
                if (st.rgnSize > 0) {
                    st.rgnData = malloc(st.rgnSize);
                    st.rgnSize = GetRegionData(hrgn, st.rgnSize, st.rgnData);
                    if (st.rgnSize == 0) {
                        free(st.rgnData);
                        st.rgnData = NULL;
                    }
                }
                if (st.rgnSize == 0) {
                    st.rgnData = NULL;
                }
                else {
                    st.hrgn = CreateRectRgn(0, 0, w, h);
                    CombineRgn(st.hrgn, st.hrgn, hrgn, RGN_XOR);
                }
                DeleteObject(hrgn);
            }
            else {
                SetWindowRgn(st.hwnd, NULL, TRUE);
            }
        }

        st.rgnEnable = -1;
        setClipRegion(clipCount > 0);

        if (st.rgnData == NULL) {
            KillTimer(st.hwnd, TIMER_CLIP_REGION);
        }
        else {
            SetTimer(st.hwnd, TIMER_CLIP_REGION, 500, NULL);
        }
    }

    InvalidateRect(st.hwnd, NULL, TRUE);
}

void archUpdateWindow() {
    int zoom = getZoom();

    st.enteringFullscreen = 1;
    emulatorSuspend();

    // Tear down ALL drivers (each Exit no-ops if inactive); tearing down
    // only the current one leaks the previous swap chain on the HWND.
    D3D12ExitFullscreenMode();
    DirectXExitFullscreenMode();

    if (st.bmBitsGDI != NULL) {
        free(st.bmBitsGDI);
        st.bmBitsGDI = NULL;
    }

    if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        if (pProperties->video.driver == P_VIDEO_DRVGDI) {
            pProperties->video.windowSize = P_VIDEO_SIZEX2;
        }
        else {
            int rv;
            SetWindowLongPtr(st.hwnd, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE);

            if (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12)
                rv = D3D12EnterFullscreenMode(st.emuHwnd,
                                                pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO, 
                                                pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO);
            else
                rv = DirectXEnterFullscreenMode(st.emuHwnd, 
                                                pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO, 
                                                pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO);

            if (rv != DXE_OK) {
                MessageBoxU(NULL, langErrorEnterFullscreen(), langErrorTitle(), MB_OK);
                if (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12)
                    D3D12ExitFullscreenMode();
                else
                    DirectXExitFullscreenMode();
                pProperties->video.windowSize = P_VIDEO_SIZEX2;
            }
        }
    }

    if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
        if (GetWindowLongPtr(st.hwnd, GWL_STYLE) & WS_POPUP) {
            mouseEmuActivate(1);
            SetWindowLongPtr(st.hwnd, GWL_STYLE, WS_OVERLAPPED | WS_CLIPCHILDREN | WS_BORDER | WS_DLGFRAME |
                                WS_SYSMENU | WS_MINIMIZEBOX | (pProperties->video.maximizeIsFullscreen?WS_MAXIMIZEBOX:0));
        }

        if (pProperties->video.driver != P_VIDEO_DRVGDI) {
            int rv;

            if (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12)
                rv = D3D12EnterWindowedMode(st.emuHwnd, zoom * WIDTH, zoom * HEIGHT,
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO, 
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO);
            else
                rv = DirectXEnterWindowedMode(st.emuHwnd, zoom * WIDTH, zoom * HEIGHT, 
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO, 
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO);
            if (rv != DXE_OK) {
                MessageBoxU(NULL, langErrorDirectXFailed(), langErrorTitle(), MB_OK);
                pProperties->video.driver = P_VIDEO_DRVGDI;
            }
        }
    }

    if (st.rgnData != NULL) {
//        SetWindowRgn(st.hwnd, NULL, TRUE);
        free(st.rgnData);
        st.rgnData = NULL;
    }
    st.rgnEnable = -1;
    setClipRegion(0);
    themeSet(pProperties->settings.themeName, 1);
    updateMenu(0);

    /* Re-own open aux theme windows (Mixer etc.) for the new fullscreen /
    ** windowed state; they live in static caches, not st.themeList. */
    archWindowApplyOwnershipAll();

    /* Poll cursor position in fullscreen so the menu strip auto-shows
       on top-edge hover and auto-hides on cursor leave, regardless of
       whether mouse messages route through emuHwnd or menuHwnd. */
    if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        SetTimer(st.hwnd, TIMER_FULLSCREEN_MENU, 100, NULL);
    } else {
        KillTimer(st.hwnd, TIMER_FULLSCREEN_MENU);
    }

    {
        RECT r = { 0, 0, zoom * WIDTH, zoom * HEIGHT };
        RECT d = { 0, 0, zoom * WIDTH, zoom * HEIGHT };
        if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
            WINDOWPLACEMENT p;
            GetWindowPlacement(st.hwnd, &p);
            r.right  = p.rcNormalPosition.right - p.rcNormalPosition.left;
            r.bottom = p.rcNormalPosition.bottom - p.rcNormalPosition.top;
        }
        if (!pProperties->video.horizontalStretch) {
            d.left  += zoom * (320 - 272) / 2;
            d.right -= zoom * (320 - 272) / 2;
        }
        
        if (pProperties->video.windowSize == P_VIDEO_SIZEX2) {
            r.right = appConfigGetInt("screen.normal.width", 640);
            r.bottom = appConfigGetInt("screen.normal.height", 480);
        }

        mouseEmuSetCaptureInfo(&r, &d);
    }

    emulatorResume();

    st.enteringFullscreen = 0;
    SetEvent(st.ddrawEvent);

    InvalidateRect(NULL, NULL, TRUE);
}



static void emuWindowDraw(int onlyOnVblank)
{      
    static void* lock = NULL;
    int rv = 0;

    if (lock == NULL) {
        lock = archSemaphoreCreate(1);
    }

    archSemaphoreWait(lock, -1);

    if (!st.enteringFullscreen && 
        (pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO || 
        (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12) ||
        (pProperties->video.driver == P_VIDEO_DRVDIRECTX)))
    {
#define PRINT_RENDERING_TIME 0
#if PRINT_RENDERING_TIME
        LARGE_INTEGER	iCurrentTime;
        double fStartTime;
        if(QueryPerformanceCounter(&iCurrentTime))
            fStartTime = (long double) iCurrentTime.QuadPart;
#endif

        st.diplaySync |= onlyOnVblank;


        if (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12)
            rv = D3D12UpdateSurface(st.emuHwnd, st.pVideo, st.diplaySync, &pProperties->video.d3d);
        else
            rv = DirectXUpdateSurface(st.pVideo, 
                                      st.showMenu | st.showDialog || emulatorGetState() != EMU_RUNNING, 
                                      0, 0, getZoom(), 
                                      pProperties->video.horizontalStretch, 
                                      pProperties->video.verticalStretch,
                                      st.diplaySync,
                                      pProperties->video.windowSize == P_VIDEO_SIZEX2);

#if PRINT_RENDERING_TIME
        {
            char output[1024];
            double t;
            LARGE_INTEGER	iFrequency;
            QueryPerformanceFrequency(&iFrequency);
            QueryPerformanceCounter(&iCurrentTime);
            t = (double) (iCurrentTime.QuadPart - fStartTime) / (double) iFrequency.QuadPart;
            sprintf(output, "Rendering time: %fms\n", t*1000.0f);
            OutputDebugString(output);
        }
#endif
        st.diplaySync = 0;
        if (rv) {
            st.frameCount++;
        }
    }
    st.diplayUpdated = rv;

    archSemaphoreSignal(lock);
}

void* createScreenShot(int large, int* bitmapSize, int png)
{
    void* bitmap = NULL;

    int zoom = large ? 2 : 1;

    DWORD* bmBitsDst = malloc(zoom * zoom * WIDTH * HEIGHT * sizeof(UInt32));
    VideoPalMode palMode      = st.pVideo->palMode;
    int scanLinesEnable       = st.pVideo->scanLinesEnable;
    int colorSaturationEnable = st.pVideo->colorSaturationEnable;
    
    FrameBuffer* frameBuffer = frameBufferGetViewFrame();

    if (frameBuffer == NULL || frameBuffer->maxWidth <= 0 || frameBuffer->lines <= 0) {
        return NULL;
    }

    st.pVideo->palMode = VIDEO_PAL_FAST;
    st.pVideo->scanLinesEnable = 0;
    st.pVideo->colorSaturationEnable = 0;

    if (png) {
        videoRender(st.pVideo, frameBufferGetViewFrame(), 32, zoom, 
                    bmBitsDst, 0, zoom * WIDTH * sizeof(DWORD), 0);
    }
    else {
        videoRender(st.pVideo, frameBufferGetViewFrame(), 32, zoom, 
                    bmBitsDst + (zoom * HEIGHT - 1) * zoom * WIDTH, 
                    0, -1 * zoom * WIDTH * sizeof(DWORD), 0);
    }

    st.pVideo->palMode               = palMode;
    st.pVideo->scanLinesEnable       = scanLinesEnable;
    st.pVideo->colorSaturationEnable = colorSaturationEnable;

    if (bitmapSize != NULL) {
        bitmap = ScreenShot2(bmBitsDst, 320 * zoom, frameBuffer->maxWidth * zoom, 240 * zoom, bitmapSize, png);
    }
    else {
        ScreenShot3(pProperties, bmBitsDst, 320 * zoom, frameBuffer->maxWidth * zoom, 240 * zoom, png);
    }

    free(bmBitsDst);

    return bitmap;
}

static LRESULT CALLBACK emuWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE:
        return 0;

    case WM_SETCURSOR:
        return mouseEmuSetCursor();

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        mouseEmuOnUserMouseActivity();
        return SendMessage(GetParent(hwnd), iMsg, wParam, lParam);

	case WM_WINDOWPOSCHANGED :
		if ( pProperties->video.driver == P_VIDEO_DRVGDI  )
		{	
			int zoom = getZoom();
			RECT r;

			GetClientRect ( hwnd, &r );

			st.clientWidth = r.right - r.left;
			st.clientHeight = r.bottom - r.top;
			
            st.bmInfo.bmiHeader.biWidth    = zoom * WIDTH;
            st.bmInfo.bmiHeader.biHeight   = zoom * HEIGHT;
            st.bmInfo.bmiHeader.biBitCount = 32;
		}
    case WM_PAINT:
        if (pProperties->video.driver == P_VIDEO_DRVGDI && emulatorGetState() != EMU_STOPPED) 
		{
            PAINTSTRUCT ps;
            FrameBuffer* frameBuffer;
            int borderWidth;
            HDC hdc;   
            int zoom = getZoom();

            // Refresh client size + bmInfo every paint; WM_WINDOWPOSCHANGED
            // only updates them while driver==GDI, so they go stale across
            // a driver switch and StretchDIBits would paint a 0x0 rect.
            {
                RECT cr;
                GetClientRect(hwnd, &cr);
                st.clientWidth  = cr.right  - cr.left;
                st.clientHeight = cr.bottom - cr.top;
                st.bmInfo.bmiHeader.biWidth    = zoom * WIDTH;
                st.bmInfo.bmiHeader.biHeight   = zoom * HEIGHT;
                st.bmInfo.bmiHeader.biBitCount = 32;
            }

            if (st.bmBitsGDI == 0) {
                st.bmBitsGDI = malloc(4096 * 4096 * sizeof(UInt32));
            }

            frameBuffer = frameBufferGetViewFrame();
            if (frameBuffer == NULL) {
                frameBuffer = frameBufferGetWhiteNoiseFrame();
            }
        
			// clear borders
            borderWidth = (320 - frameBuffer->maxWidth) * zoom / 2;

            if (borderWidth > 0) {
                DWORD* ptr  = st.bmBitsGDI;
                int y;
                for (y = 0; y < zoom * HEIGHT; y++) {
                    memset(ptr, 0, borderWidth * sizeof(DWORD));
                    ptr += zoom * WIDTH;
                    memset(ptr - borderWidth, 0, borderWidth * sizeof(DWORD));
                }
            }

			// render image
            videoRender(st.pVideo, 
						frameBuffer, 
						32, 
						zoom, 
                        (char*)st.bmBitsGDI + borderWidth * sizeof(DWORD) + (zoom * HEIGHT - 1) * zoom * WIDTH * sizeof(DWORD), 
                        0, -1 * zoom * WIDTH * sizeof(DWORD), 0);

			// Beginpaint moved because it's only needed to output the framebuffer
			hdc = BeginPaint ( hwnd, &ps );

			StretchDIBits(hdc,
						  0, 0, 
						  st.clientWidth, st.clientHeight, 
						  0, 0, 
						  zoom * WIDTH, zoom * HEIGHT, 
						  st.bmBitsGDI, 
                          &st.bmInfo, 
						  DIB_RGB_COLORS, 
						  SRCCOPY);

            EndPaint(hwnd, &ps);
            st.frameCount++;

        }
        else if (pProperties->video.driver != P_VIDEO_DRVGDI && 
                 (emulatorGetState() == EMU_PAUSED || emulatorGetState() == EMU_SUSPENDED)) {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
		    EndPaint(hwnd, &ps);
            SetEvent(st.ddrawEvent);
        }
        else {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
        }
        return 0;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);    
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE:
        SetTimer(hwnd, TIMER_STATUSBAR_UPDATE, 100, NULL);
        SetTimer(hwnd, TIMER_POLL_INPUT, 50, NULL);
        SetTimer(hwnd, TIMER_POLL_FRAMECOUNT, 1000, NULL);
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_DROPFILES:
        {
            char fname[MAX_PATH * 4];
            wchar_t wfname[MAX_PATH];
            HDROP hDrop;
            DWORD fa;

            hDrop = (HDROP)wParam;
            DragQueryFileW(hDrop, 0, wfname, MAX_PATH);
            WideToUtf8(wfname, fname, sizeof(fname));
            DragFinish(hDrop);

            fa = GetFileAttributesA(fname);
            if (fa & FILE_ATTRIBUTE_DIRECTORY) {
                insertDiskette(pProperties, 0, fname, NULL, 0);
            }
            else {
                tryLaunchUnknownFile(pProperties, fname, 0);
            }
        }
        return 0;

    case WM_LAUNCHFILE:
        {
            char fileName[512];
            FILE* file = fopen(LAUNCH_TEMP_FILE, "r");
            if (file != NULL) {
                int size = fread(fileName, 1, 512, file);
                fclose(file);
                if (size > 0) {
                    char* argument;

                    fileName[size] = 0;                    

                    argument = extractToken(fileName, 0);
                    if (*argument) {
                        int i;

                        emulatorStop();

                        for (i = 0; i < PROP_MAX_CARTS; i++) {
                            pProperties->media.carts[i].fileName[0] = 0;
                            pProperties->media.carts[i].fileNameInZip[0] = 0;
                            pProperties->media.carts[i].type = ROM_UNKNOWN;
                            updateExtendedRomName(i, pProperties->media.carts[i].fileName, pProperties->media.carts[i].fileNameInZip);
                        }

                        for (i = 0; i < PROP_MAX_DISKS; i++) {
                            pProperties->media.disks[i].fileName[0] = 0;
                            pProperties->media.disks[i].fileNameInZip[0] = 0;
                            updateExtendedDiskName(i, pProperties->media.disks[i].fileName, pProperties->media.disks[i].fileNameInZip);
                        }

                        for (i = 0; i < PROP_MAX_TAPES; i++) {
                            pProperties->media.tapes[i].fileName[0] = 0;
                            pProperties->media.tapes[i].fileNameInZip[0] = 0;
                            updateExtendedCasName(i, pProperties->media.tapes[i].fileName, pProperties->media.tapes[i].fileNameInZip);
                        }

                        tryLaunchUnknownFile(pProperties, argument, 1);
                    }

                    SetActiveWindow(hwnd);
                }
            }
        }
        return 0;

    case WM_COMMAND:
        if ( menuCommand(pProperties, LOWORD(wParam))) {
            updateMenu(0);
        }
        break;

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        {
            ShotcutHotkey key;
            key.type = HOTKEY_TYPE_KEYBOARD;
            key.mods = keyboardGetModifiers();
            key.key  = wParam & 0xff;
            checkKeyDown(st.shortcuts, key);
        }
        return 0;

    case WM_SYSKEYUP:
    case WM_KEYUP:
        {
            ShotcutHotkey key;
            key.type = HOTKEY_TYPE_KEYBOARD;
            key.mods = keyboardGetModifiers();
            key.key  = wParam & 0xff;
            checkKeyUp(st.shortcuts, key);
        }
        return 0;

    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;

    case WM_SYSCOMMAND:
        switch(wParam) {
        case SC_MAXIMIZE:
            vdpSetDisplayEnable(1);
            st.minimized = 0;
            if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
                pProperties->video.windowSize = P_VIDEO_SIZEFULLSCREEN;
                archUpdateWindow();
            }
            return 0;
        case SC_MINIMIZE:
            vdpSetDisplayEnable(0);
            st.minimized = 1;
//            emulatorSuspend();
            break;
        case SC_RESTORE:
            vdpSetDisplayEnable(1);
            st.minimized = 0;
//            emulatorResume();
            break;
        }
        break;

    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION && pProperties->video.maximizeIsFullscreen) {
            if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
                pProperties->video.windowSize = P_VIDEO_SIZEFULLSCREEN;
                archUpdateWindow();
            }
            return 0;
        }
        break;

    case WM_ENTERSIZEMOVE:
        emulatorSuspend();
        mouseEmuActivate(0);
        st.showDialog++;
        break;

    case WM_EXITSIZEMOVE:
        st.showDialog--;
        emulatorResume();
        mouseEmuActivate(1);
        break;

    case WM_ENTERMENULOOP:
        emulatorSuspend();
        st.trackMenu = 1;
        SetTimer(st.hwnd, TIMER_MENUUPDATE, 250, NULL);
        mouseEmuActivate(0);
        return 0;

    case WM_EXITMENULOOP:
        {
            int moreMenu;
            
            if (!st.minimized) {
                emuWindowDraw(0);
            }
            moreMenu = menuExitMenuLoop();
            if (!moreMenu) {
                mouseEmuActivate(1);
                //KillTimer(st.hwnd, TIMER_MENUUPDATE);
                emulatorResume();
                updateMenu(0);
                if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
                    PostMessage(hwnd, WM_LBUTTONDOWN, 0, 0);
                    PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
                }
                st.trackMenu = 0;
            }
        }
        return 0;

    case WM_DPICHANGED:
        {
            RECT* r = (RECT*)lParam;
            /* Windowed: skip the suggested-rect resize (themeSet's own
               SetWindowPos lands the final size); fullscreen needs it. */
            if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
                SetWindowPos(hwnd, NULL,
                    r->left, r->top,
                    r->right - r->left, r->bottom - r->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            } else {
                pProperties->video.windowX = r->left;
                pProperties->video.windowY = r->top;
            }
            /* Pin archMenuStripHeight() to LOWORD(wParam) (GetDpiForWindow
               can still return the old DPI mid-transition); rebuild every
               built-in theme and force-unload external themes so they
               pick up the new DPI. */
            archSetDpiOverride(LOWORD(wParam));

            if (st.themePageActive) {
                themePageActivate(st.themePageActive, NULL);
            }
            st.themePageActive = NULL;

            /* Rebuild menu font for new DPI; otherwise strip keeps the
               startup-DPI font and overflows after DPI transitions. */
            menuRebuildForDpi(LOWORD(wParam));

            /* Rebuild built-ins (path=="") in place; drop externals so
               themeSet below lazy-reloads them at the new DPI. */
            if (st.themeList) {
                for (int i = 0; st.themeList[i] != NULL; i++) {
                    ThemeCollection* tc = st.themeList[i];
                    if (tc->path[0] == 0) {
                        themeClassicRebuild(tc);
                    } else {
                        themeCollectionUnload(tc);
                    }
                }
            }
            themeSet(pProperties->settings.themeName, 1);

            archSetDpiOverride(0);
        }
        return 0;

    case WM_MOVE:
        if (!st.enteringFullscreen && pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
            RECT r;
            GetWindowRect(hwnd, &r);
            pProperties->video.windowX = r.left;
            pProperties->video.windowY = r.top;
        }

    case WM_DISPLAYCHANGE:
        /* WM_MOVE above lacks `break`; st.enteringFullscreen guard below
           makes the fall-through harmless during normal moves. */
        if (pProperties->video.driver != P_VIDEO_DRVGDI) {
            int zoom = getZoom();
            if (st.enteringFullscreen) {
                if (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12)
                    D3D12UpdateWindowedMode(st.emuHwnd, zoom * WIDTH, zoom * HEIGHT,
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO, 
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO);
                else
                    DirectXUpdateWindowedMode(st.emuHwnd, zoom * WIDTH, zoom * HEIGHT,
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO, 
                                              pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO);
            }
        }
        break;

    case WM_GETMINMAXINFO:
        {
            LRESULT rv = DefWindowProc(hwnd, iMsg, wParam, lParam);
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            /* x8 zoom needs 2564x1971; allow 8K headroom so the window can
               exceed the physical screen without WM_GETMINMAXINFO clamping. */
            mmi->ptMaxSize.x      = 16384;
            mmi->ptMaxSize.y      = 16384;
            mmi->ptMaxTrackSize.x = 16384;
            mmi->ptMaxTrackSize.y = 16384;
            return 0;
        }

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, TRUE);
        break;
        
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (kbdLockDisable != NULL) {
                kbdLockDisable(0);
            }
            else {
                inputReset(hwnd);
            }
            mouseEmuActivate(0);
            actionMaxSpeedRelease();
        }
        else {
            if (kbdLockEnable != NULL && emulatorGetState() == EMU_RUNNING && pProperties->emulation.disableWinKeys) {
                kbdLockEnable();
            }
            mouseEmuActivate(1);
        }
        if (st.themePageActive) {
            HDC hdc = GetDC(hwnd);
            st.active = LOWORD(wParam) != WA_INACTIVE;
            themePageSetActive(st.themePageActive, hdc, st.active);
            ReleaseDC(hwnd, hdc);
        }
        break;

    case WM_NCMOUSEMOVE:
        if (st.themePageActive) {
            checkClipRegion();
        }
        break;

    case WM_MOUSEMOVE:
        mouseEmuOnUserMouseActivity();
        if (st.themePageActive) {
            HDC hdc = GetDC(hwnd);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            themePageMouseMove(st.themePageActive, hdc, pt.x, pt.y);
            win32SliderTooltipUpdate(&st.hwndSliderTip, hwnd,
                                     themePageHoverSliderPercent(st.themePageActive, pt.x, pt.y));
            ReleaseDC(hwnd, hdc);
            checkClipRegion();
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }
        if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
            /* Show on cursor at top edge, auto-hide on cursor leaving menu strip. */
            int menuStripH = archMenuStripHeight();
            int y = HIWORD(lParam);
            if (y < 8) {
                if (!st.showMenu) updateMenu(1);
            } else if (y > menuStripH + 4) {
                if (st.showMenu) updateMenu(0);
            }
        }
        archWindowMove();
        SetTimer(hwnd, TIMER_THEME, 250, NULL);

        break;

    case WM_MOUSELEAVE:
        win32SliderTooltipUpdate(&st.hwndSliderTip, hwnd, -1);
        return 0;

    case WM_LBUTTONDOWN:
        {
            POINT pt;
            RECT r;

            GetCursorPos(&pt);
            GetWindowRect(st.emuHwnd, &r);
            if (!IsWindowVisible(st.emuHwnd) || !PtInRect(&r, pt)) {
                SetCapture(hwnd);
                st.currentHwnd = hwnd;

                if (st.themePageActive) {
                    HDC hdc = GetDC(hwnd);
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(hwnd, &pt);
                    themePageMouseButtonDown(st.themePageActive, hdc, pt.x, pt.y);
                    ReleaseDC(hwnd, hdc);
                }
                if (st.showMenu && pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
                    updateMenu(0);
                }
            }
        }
        break;

    case WM_LBUTTONUP:
        ReleaseCapture();
        if (st.themePageActive) {
            HDC hdc = GetDC(hwnd);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            themePageMouseButtonUp(st.themePageActive, hdc, pt.x, pt.y);
            ReleaseDC(hwnd, hdc);
            st.currentHwnd = NULL;
        }

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        switch (wParam) {
        case TIMER_FULLSCREEN_MENU:
            /* st.trackMenu = popup submenu is open; skip the poll so we
               don't toggle mouseEmuActivate and re-hide the cursor. */
            if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN
                && !st.trackMenu) {
                POINT pt;
                int menuStripH = archMenuStripHeight();
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (pt.y < 8) {
                    if (!st.showMenu) updateMenu(1);
                } else if (pt.y > menuStripH + 4) {
                    if (st.showMenu) updateMenu(0);
                }
            }
            break;

        case TIMER_CLIP_REGION:
            updateClipRegion();
            break;

        case TIMER_STATUSBAR_UPDATE:
            if (!st.minimized) {
                static UInt32 resetCnt = 0;
                HDC hdc = GetDC(hwnd);

                if (emulatorGetState() == EMU_RUNNING) {
                    if ((resetCnt++ & 0x3f) == 0) {
                        mixerIsChannelTypeActive(st.mixer, MIXER_CHANNEL_MOONSOUND, 1);
                        mixerIsChannelTypeActive(st.mixer, MIXER_CHANNEL_YAMAHA_SFG, 1);
                        mixerIsChannelTypeActive(st.mixer, MIXER_CHANNEL_MSXAUDIO, 1);
                        mixerIsChannelTypeActive(st.mixer, MIXER_CHANNEL_MSXMUSIC, 1);
                        mixerIsChannelTypeActive(st.mixer, MIXER_CHANNEL_SCC, 1);
                    }
                }

                /* Classic / Classic Dark drive their full title from this
                   100ms poller; other themes set theirs at theme change. */
                if (!strcmp(pProperties->settings.themeName, "Classic") ||
                    !strcmp(pProperties->settings.themeName, "Classic Dark"))
                    themeClassicTitlebarUpdate(hwnd);

                if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
                    int drv = pProperties->video.driver;
                    /* DDraw: parent stays windowed; status update goes
                       direct to hdc (no monitor-sized stretch). */
                    if (drv == P_VIDEO_DRVDIRECTX_VIDEO ||
                        drv == P_VIDEO_DRVDIRECTX) {
                        themePageUpdate(st.themePageActive, hdc);
                    } else {
                        drawThemeOnEmuArea(hwnd, hdc, themePageUpdate);
                    }
                } else {
                    themePageUpdate(st.themePageActive, hdc);
                }
                ReleaseDC(hwnd, hdc);

                PatchDiskSetBusy(0, 0);
                PatchDiskSetBusy(1, 0);
                ledSetCas(0);

            }
            break;

        case TIMER_POLL_INPUT:
            {
                DWORD buttonState;
                DWORD buttons;
                ShotcutHotkey key;
                int i;
                HWND hwndFocus = GetFocus();

                keyboardSetFocus(1, hwndFocus == st.hwnd || hwndFocus == st.emuHwnd);
                
                if (emulatorGetState() != EMU_RUNNING) {
                    archPollInput();
                }

                buttonState = joystickGetButtonState();
                
                buttons = buttonState & ~st.buttonState;

                for (i = 1; buttons != 0; i++, buttons >>= 1) {
                    if (buttons & 1) {
                        key.type = HOTKEY_TYPE_JOYSTICK;
                        key.mods = 0;
                        key.key  = i;
                        checkKeyDown(st.shortcuts, key);
                    }
                }
                
                buttons = ~buttonState & st.buttonState;
                for (i = 1; buttons != 0; i++, buttons >>= 1) {
                    if (buttons & 1) {
                        key.type = HOTKEY_TYPE_JOYSTICK;
                        key.mods = 0;
                        key.key  = i;
                        checkKeyUp(st.shortcuts, key);
                    }
                }

                st.buttonState = buttonState;

                /* Pause cursor fade only on EMU_STOPPED; keep alive on
                   PAUSED/SUSPENDED so a paused game still fades. */
                mouseEmuSetRunState(emulatorGetState() != EMU_STOPPED);
            }
            break;

        case TIMER_POLL_FRAMECOUNT:
            st.framesPerSecond = st.frameCount;
            st.frameCount = 0;
            break;

        case TIMER_MENUUPDATE:
            if (!st.minimized) {
                emuWindowDraw(0);
            }
            if (!st.trackMenu) {
                KillTimer(st.hwnd, TIMER_MENUUPDATE);
            }
            break;

        case TIMER_SCREENUPDATE:
            if (emulatorGetState() != EMU_STOPPED) {
                DWORD rv = WaitForSingleObject(st.ddrawEvent, 0);
                if (rv == WAIT_OBJECT_0) {
                    if (!st.minimized) {
                        emuWindowDraw(0);
                    }
                    SetEvent(st.ddrawAckEvent);
                }
            }
            break;

        case TIMER_SCREENSHOT:
            {
                RECT r;
                GetWindowRect(st.emuHwnd, &r);
                KillTimer(hwnd, TIMER_SCREENSHOT);
			    ScreenShot(pProperties, st.emuHwnd, r.right - r.left, r.bottom - r.top, 0, 0, pProperties->settings.usePngScreenshots);
            }
            break;
            
        case TIMER_THEME:        
            if (!st.minimized) {
                POINT pt;
                RECT r;
                HDC hdc;

                GetCursorPos(&pt);
                GetWindowRect(hwnd, &r);

                if (!PtInRect(&r, pt)) {
                    KillTimer(hwnd, TIMER_THEME);
                }

                ScreenToClient(hwnd, &pt);

                hdc = GetDC(hwnd);
                themePageMouseMove(st.themePageActive, hdc, pt.x, pt.y);
                ReleaseDC(hwnd, hdc);
            }
            break;
        }
        return 0;

    case WM_UPDATE:
        frameBufferFlipViewFrame(0);
        InvalidateRect(st.emuHwnd, NULL, TRUE);
        return 0;

    case WM_INPUTLANGCHANGE:
        break;

    case WM_SETTINGCHANGE:
        if (lParam && strcmp((const char*)lParam, "ImmersiveColorSet") == 0) {
            /* System dark/light toggled -- drop the cached registry value so
            ** subclass procs pick up the new state on their next message. */
            win32InvalidateDarkModeCache();
            win32ApplyDarkTitle(hwnd);
            /* Re-evaluate ForceDark vs ForceLight against the new system
            ** theme, then refresh and flush the cached menu theme so already
            ** populated popup/submenu visuals don't keep the previous mode. */
            win32EnableDarkModeForApp();
        }
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!st.minimized) {
                HDC hMemDC = CreateCompatibleDC(hdc);
                HBITMAP hBitmap = (HBITMAP)SelectObject(hMemDC, st.hBitmap);

                if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
                    themePageDraw(st.themePageActive, hMemDC, NULL);
                    BitBlt(hdc, 0, 0, st.themePageActive->width, st.themePageActive->height, hMemDC, 0, 0, SRCCOPY);
                    SelectObject(hMemDC, hBitmap);
                    DeleteDC(hMemDC);
                }
                else {
                    int drv = pProperties->video.driver;
                    /* DDraw fullscreen keeps the parent at windowed size,
                       so drawThemeOnEmuArea's monitor-sized StretchBlt
                       clips and races DDraw flips; use GetClientRect. */
                    if (drv == P_VIDEO_DRVDIRECTX_VIDEO ||
                        drv == P_VIDEO_DRVDIRECTX) {
                        RECT r;
                        GetClientRect(hwnd, &r);
                        themePageDraw(st.themePageActive, hMemDC, NULL);
                        StretchBlt(hdc, 0, 0, r.right, r.bottom,
                                   hMemDC, 0, 0,
                                   st.themePageActive->width,
                                   st.themePageActive->height, SRCCOPY);
                        SelectObject(hMemDC, hBitmap);
                        DeleteDC(hMemDC);
                    } else {
                        /* D3D12: parent already at monitor pixels;
                           MonitorFromWindow avoids racing SetWindowPos. */
                        SelectObject(hMemDC, hBitmap);
                        DeleteDC(hMemDC);
                        drawThemeOnEmuArea(hwnd, hdc, themeDrawAllAdapter);
                    }
                }
            }
            EndPaint(hwnd, &ps);
        }
        return 0;
        
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        
        if (pProperties->emulation.ejectMediaOnExit) {
        	actionCartRemove1(); actionCartRemove2();
        	actionDiskRemoveA(); actionDiskRemoveB();
        	actionCasRemove();
        	actionHarddiskRemoveAll();
        }
        
        emulatorStop();
        toolUnLoadAll();
        if (pProperties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
            RECT r;
            
            GetWindowRect(hwnd, &r);
            pProperties->video.windowX = r.left;
            pProperties->video.windowY = r.top;
        }
        st.enteringFullscreen = 1;
        if (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12)
            D3D12ExitFullscreenMode();
        else
            DirectXExitFullscreenMode();
        PostQuitMessage(0);
        inputDestroy();
        return 0;

    case WM_DEVICECHANGE:
        {
            PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;

            switch(wParam) {
            case DBT_DEVICEARRIVAL:
            case DBT_DEVICEREMOVECOMPLETE:
                if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME) {
                    PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;

                    if (lpdbv->dbcv_flags & DBTF_MEDIA) {
                        cdromOnMediaChange(lpdbv->dbcv_unitmask);
                    }
                }
                break;
            }
        }
    }
    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

////////////////////////////////////////////////////////////////////

static int videoTimeAverage = 0;
static int videoTimeTotal = 0;
static int videoTimeIdle = 1;

UInt32 videoGetCpuUsage() {
    videoTimeAverage = 1000 * (videoTimeTotal - videoTimeIdle) / videoTimeTotal;

    if (videoTimeAverage >= 1000) {
        videoTimeAverage = 0;
    }

    videoTimeIdle  = 0;
    videoTimeTotal = 1;

    return videoTimeAverage;
}

void updateEmuWindow() {
    if (emulatorGetState() != EMU_STOPPED) {
        SetEvent(st.ddrawEvent);
    }
}

int setDefaultPath() {   
    char buffer[512];  
    char buffer2[512];
    char rootDir[512];
    int readOnlyDir;
    DWORD dirattr; 
    FILE* file;
    char* ptr;

    // Set current directory to where the exe is located
    GetModuleFileNameU((HINSTANCE)GetModuleHandle(NULL), buffer, 512);
    ptr = (char*)stripPath(buffer);
    *ptr = 0;
    chdirU(buffer);

    GetCurrentDirectoryU(MAX_PATH - 1, st.pCurDir);

    readOnlyDir = 0;

    // attribute check first 
    // ( check blueMSX run on the CD-ROM ) 
    dirattr = GetFileAttributes(buffer); 
    if((dirattr == -1) || (dirattr & FILE_ATTRIBUTE_READONLY)){ 
        readOnlyDir = 1; 
    }else{ 
        // Test if current directory is read only 
        // ( check removable disk write protected ) 
        // note: this check with Dialog warning 
        file = fopen("wrtest", "w"); 
        readOnlyDir = file == NULL; 
        if (file != NULL) { 
            fclose(file); 
            DeleteFile("wrtest"); // Delete test file 
        } 
    }

    if (!readOnlyDir) {
        GetCurrentDirectoryU(MAX_PATH - 1, rootDir); 
    }
    else {
        // Get user's My Documents folder 
        LPITEMIDLIST Root; 
        wchar_t wBuffer2[MAX_PATH];
        SHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &Root); 
        SHGetPathFromIDListW(Root, wBuffer2);
        WideToUtf8(wBuffer2, buffer2, 512);

        chdirU(buffer2); 
        sprintf(buffer, "%s\\blueMSX Temporary Files", buffer2); 
        mkdirU(buffer); 
        chdirU(buffer); 

        GetCurrentDirectoryU(MAX_PATH - 1, rootDir); 
        SetCurrentDirectoryU(st.pCurDir);
    }

    // Set up temp directories
    propertiesSetDirectory(st.pCurDir, rootDir);

    sprintf(buffer, "%s\\Machines", rootDir);
	machineSetDirectory(buffer);

    sprintf(buffer, "%s\\Audio Capture", rootDir);
    mkdirU(buffer);
    actionSetAudioCaptureSetDirectory(buffer, "");

    sprintf(buffer, "%s\\Video Capture", rootDir);
    mkdirU(buffer);
    actionSetVideoCaptureSetDirectory(buffer, "");

    sprintf(buffer, "%s\\QuickSave", rootDir);
    mkdirU(buffer);
    actionSetQuickSaveSetDirectory(buffer, "");

    sprintf(buffer, "%s\\SRAM", rootDir);
    mkdirU(buffer);
    boardSetDirectory(buffer);

    sprintf(buffer, "%s\\Keyboard Config", rootDir);
    mkdirU(buffer);
    keyboardSetDirectory(buffer);

    sprintf(buffer, "%s\\Screenshots", rootDir);
    mkdirU(buffer);
    screenshotSetDirectory(buffer, "");

    sprintf(buffer, "%s\\Casinfo", rootDir);
    mkdirU(buffer);
    tapeSetDirectory(buffer, "");

    sprintf(buffer, "%s\\Databases", rootDir);
    mkdirU(buffer);
    mediaDbLoad(buffer);

    mediaDbCreateRomdb();

    mediaDbCreateDiskdb();

    mediaDbCreateCasdb();

    return readOnlyDir;
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

int emuCheckLanguageArgument(char* cmdLine, int defaultLang){
    int i;
    int lang;
    char* argument;
    
    for (i = 0; argument = extractToken(cmdLine, i); i++) {
        if (strcmp(argument, "/language") == 0) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return defaultLang;
            lang = langFromName(argument, 0);
            return lang == EMU_LANG_UNKNOWN ? defaultLang : lang;
        }
    }

    return defaultLang;
}

////////////////////////////////////////////////////////////////////

static int getScreenBitDepth()
{
    HDC hdc;
    hdc = GetDC(GetDesktopWindow());
    return GetDeviceCaps(hdc, BITSPIXEL) * GetDeviceCaps(hdc, PLANES);
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

#ifdef _CONSOLE
int main(int argc, char **argv)
#else
WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, PSTR szLine, int iShow)
#endif
{
#ifdef _CONSOLE
    char szLine[8192] = "";
#endif
    static WNDCLASSEX wndClass;
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
    char buffer[512];  
    BOOL screensaverActive;
    int  resetRegistry;
    HWND hwnd;
    int doExit = 0;
    RECT wr;
    MSG msg;
    int i;
    HINSTANCE kbdLockInst;
    int readOnlyDir;
    const char* tempName;
    int scrDepth;

#ifdef _CONSOLE
    for (i = 1; i < argc; i++) {
        strcat(szLine, argv[i]);
        strcat(szLine, " ");
    }
#endif

    scrDepth = getScreenBitDepth();
    if (scrDepth != 16 && scrDepth != 32) {
        MessageBoxU(NULL, "blueMSX works best in 16 or 32 bits color depth", "blueMSX Info", MB_OK | MB_ICONINFORMATION);
    }

    hwnd = FindWindow("blueMSX", "  blueMSX");
    if (hwnd != NULL && *szLine) {
        char args[2048];
        char* cmdLine = args;

        if (0 == strncmp(szLine, "/onearg ", 8)) {
            char* ptr;
            sprintf(args, "\"%s", szLine + 8);
            ptr = args + strlen(args);
            while(*--ptr == ' ') {
                *ptr = 0; 
            }
            strcat(args, "\"");
        }
        else {
            cmdLine = szLine;
        }
        if (!extractToken(cmdLine, 1)) {
            FILE* file = fopen(LAUNCH_TEMP_FILE, "w");
            if (file != NULL) {
                fwrite(cmdLine, 1, strlen(cmdLine) + 1, file);
                fclose(file);
                SendMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
                SetForegroundWindow(hwnd);
                PostMessage(hwnd, WM_LAUNCHFILE, 0, 0);
            }
            return 0;
        }
    }

    InitCommonControls(); 

    DirectDrawInitDisplayModes();

    wndClass.cbSize         = sizeof(wndClass);
    wndClass.style          = CS_OWNDC;
    wndClass.lpfnWndProc    = wndProc;
    wndClass.cbClsExtra     = 0;
    wndClass.cbWndExtra     = 0;
    wndClass.hInstance      = hInstance;
    wndClass.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hIconSm        = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground  = NULL;
    wndClass.lpszMenuName   = NULL;
    wndClass.lpszClassName  = "blueMSX";

    RegisterClassEx(&wndClass);

    wndClass.lpfnWndProc    = emuWndProc;
    wndClass.lpszClassName  = "blueMSXemuWindow";
    wndClass.hIcon          = NULL;
    wndClass.hIconSm        = NULL;
    RegisterClassEx(&wndClass);

    {
        // Set current directory to where the exe is located
        char* ptr;
        GetModuleFileNameU((HINSTANCE)GetModuleHandle(NULL), buffer, 512);
        ptr = (char*)stripPath(buffer);
        *ptr = 0;
        SetCurrentDirectoryU(buffer);
    }

    pkg_load("Packages/BombaPack.bpk", NULL, 0);

    appConfigLoad();

    kbdLockInst = LoadLibraryU("kbdlock.dll");

    if (kbdLockInst != NULL) {
        kbdLockEnable  = (KbdLockFun)GetProcAddress(kbdLockInst, (LPCSTR)2);
        kbdLockDisable = (KbdLockFun)GetProcAddress(kbdLockInst, (LPCSTR)3);
    }

    readOnlyDir = setDefaultPath();

    {
        /* Modify scan code map if nessecary */
        PropKeyboardLanguage kbdLang = P_KBD_EUROPEAN;
        int syncMode = 0;
        char klId[KL_NAMELENGTH];
        if (GetKeyboardLayoutName(klId)) {
            if (0 == strcmp(klId + 4, "0411")) {
                kbdLang = P_KBD_JAPANESE;
            }
        }

        resetRegistry = emuCheckResetArgument(szLine);
        if (resetRegistry) {
            syncMode = canHandleVblankSyncMode() ? 1 : 0;
        }

        {
            char themeName[64];
            if (GetSystemMetrics(SM_CYSCREEN) > 600) {
                strcpy(themeName, "DIGIblue SUITE-X2");
            }
            else {
                strcpy(themeName, "Classic");
            }
            pProperties = propCreate(resetRegistry, getLangType(), kbdLang, syncMode, themeName);
        }

        pProperties->language = emuCheckLanguageArgument(szLine, pProperties->language);
        
        if (resetRegistry == 2) {
            propDestroy(pProperties);
            FreeLibrary(kbdLockInst);

            exit(0);
            return 0;
        }

        emuCheckFullscreenArgument(pProperties, szLine);
    }

    tempName = appConfigGetString("singlemachine", NULL);
    if (tempName != NULL) {
        strcpy(pProperties->emulation.machineName, tempName);
    }

    tempName = appConfigGetString("singletheme", NULL);
    if (tempName != NULL) {
        strcpy(pProperties->settings.themeName, tempName);
    }

    if (readOnlyDir && pProperties->settings.portable) {
        MessageBoxU(NULL, langErrorPortableReadonly(), langErrorTitle(), MB_OK);
        return 0;
    }

    // Load tools
    sprintf(buffer, "%s\\Tools", st.pCurDir);
    toolLoadAll(buffer, pProperties->language);


    CoInitializeEx(NULL,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE|COINIT_SPEED_OVER_MEMORY);

    videoInInitialize(pProperties);

    /* Init-time selection; same logic as the apply path above. */
    switch(pProperties->emulation.syncMethod) {
    case P_EMU_SYNCNONE:
        frameBufferSetFrameCount(1);
        break;
    default:
        frameBufferSetFrameCount(3);
    }

    midiInitialize();

    st.active = 1;
    st.buttonState = 0;
    st.showDialog = 0;
    st.enteringFullscreen = 1;
    st.frameCount = 0;
    st.framesPerSecond = 0;
    st.minimized = 0;
    st.bmBitsGDI     = NULL;
    st.bmInfo.bmiHeader.biSize           = sizeof(BITMAPINFOHEADER);
    st.bmInfo.bmiHeader.biWidth          = WIDTH;
    st.bmInfo.bmiHeader.biHeight         = HEIGHT;
    st.bmInfo.bmiHeader.biPlanes         = 1;
    st.bmInfo.bmiHeader.biBitCount       = 32;
    st.bmInfo.bmiHeader.biCompression    = BI_RGB;
    st.bmInfo.bmiHeader.biClrUsed        = 0;
    st.bmInfo.bmiHeader.biClrImportant   = 0;
    st.ddrawEvent    = CreateEvent(NULL, FALSE, FALSE, NULL);
    st.ddrawAckEvent = CreateEvent(NULL, FALSE, FALSE, NULL);    
	



    st.pVideo = videoCreate();
    videoSetColors(st.pVideo, pProperties->video.saturation, pProperties->video.brightness, 
                  pProperties->video.contrast, pProperties->video.gamma);
    videoSetScanLines(st.pVideo, pProperties->video.scanlinesEnable, pProperties->video.scanlinesPct);
    videoSetColorSaturation(st.pVideo, pProperties->video.colorSaturationEnable, pProperties->video.colorSaturationWidth);
    videoSetBlendFrames(st.pVideo, pProperties->video.blendFrames);

    DirectDrawSetDisplayMode(pProperties->video.fullscreen.width,
                             pProperties->video.fullscreen.height,
                             pProperties->video.fullscreen.bitDepth);

    st.mixer  = mixerCreate();

    emulatorInit(pProperties, st.mixer);
    actionInit(st.pVideo, pProperties, st.mixer);
    langInit();
    tapeSetReadOnly(pProperties->cassette.readOnly);
    
    ethIfInitialize(pProperties);
    cdromInitialize();

    // Initialize shortcuts profile
    if (!shortcutsIsProfileValid(pProperties->emulation.shortcutProfile)) {
        shortcutsGetAnyProfile(pProperties->emulation.shortcutProfile);
    }

    st.shortcuts = shortcutsCreateProfile(pProperties->emulation.shortcutProfile);

    SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, &screensaverActive, SPIF_SENDWININICHANGE); 
    SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, !pProperties->settings.disableScreensaver, 0, SPIF_SENDWININICHANGE); 

    if(!pProperties->settings.portable) {
        if (pProperties->emulation.registerFileTypes) {
            registerFileTypes();
        }
        else {
            unregisterFileTypes();
        }
    }

    pProperties->language = emuCheckLanguageArgument(szLine, pProperties->language);
    langSetLanguage(pProperties->language);

    /* Enable dark for process-wide themed controls before any dialog shows. */
    win32EnableDarkModeForApp();

    st.hwnd = CreateWindow("blueMSX", "  blueMSX",
                            WS_OVERLAPPED | WS_CLIPCHILDREN | WS_BORDER | WS_DLGFRAME | 
                            WS_SYSMENU | WS_MINIMIZEBOX | (pProperties->video.maximizeIsFullscreen?WS_MAXIMIZEBOX:0), 
                            CW_USEDEFAULT, CW_USEDEFAULT, 800, 200, NULL, NULL, hInstance, NULL);

    /* Main window is not a dialog so it doesn't go through win32CommonApplyDark.
    ** Apply the immersive dark titlebar directly. */
    win32ApplyDarkTitle(st.hwnd);

    menuCreate(st.hwnd);

    if (appConfigGetInt("menu.file", 1) != 0) {
        addMenuItem(langMenuFile(), actionMenuFile, 0);
    }
    if (appConfigGetInt("menu.emulation", 1) != 0) {
        addMenuItem(langMenuRun(), actionMenuRun, 1);
    }
    if (appConfigGetInt("menu.window", 1) != 0) {
        addMenuItem(langMenuWindow(), actionMenuZoom, 1);
    }
    if (appConfigGetInt("menu.options", 1) != 0) {
        addMenuItem(langMenuOptions(), actionMenuOptions, 1);
    }
    if (appConfigGetInt("menu.tools", 1) != 0) {
        addMenuItem(langMenuTools(), actionMenuTools, 1);
    }
    if (appConfigGetInt("menu.help", 1) != 0) {
        addMenuItem(langMenuHelp(), actionMenuHelp, 1);
    }

    st.emuHwnd = CreateWindow("blueMSXemuWindow", "", WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE, 0, 0, 0, 0, st.hwnd, NULL, hInstance, NULL);
    ShowWindow(st.emuHwnd, SW_HIDE);

    inputInit();
    inputReset(st.hwnd);
    keyboardLoadConfig(pProperties->keyboard.configFile);
    sprintf(pProperties->keyboard.configFile, keyboardGetCurrentConfig());
    
    mouseEmuInit(st.emuHwnd, 1);
    joystickPortSetType(0, pProperties->joy1.typeId);
    joystickPortSetType(1, pProperties->joy2.typeId);

    printerIoSetType(pProperties->ports.Lpt.type, pProperties->ports.Lpt.fileName);
    uartIoSetType(pProperties->ports.Com.type, pProperties->ports.Com.fileName);
    midiIoSetMidiOutType(pProperties->sound.MidiOut.type, pProperties->sound.MidiOut.fileName);
    midiIoSetMidiInType(pProperties->sound.MidiIn.type, pProperties->sound.MidiIn.fileName);
    ykIoSetMidiInType(pProperties->sound.YkIn.type, pProperties->sound.YkIn.fileName);
    midiEnableMt32ToGmMapping(pProperties->sound.MidiOut.mt32ToGm);
    midiInSetChannelFilter(pProperties->sound.YkIn.channel);

    st.dskWnd = diskQuickviewWindowCreate(st.hwnd);

    if (pProperties->video.windowX < 0 || pProperties->video.windowY < 0) {
        GetWindowRect(st.hwnd, &wr);
        pProperties->video.windowX = wr.left;
        pProperties->video.windowY = wr.top;
    }

    if (pProperties->video.windowX > GetSystemMetrics(SM_CXSCREEN) - 300) {
        pProperties->video.windowX = GetSystemMetrics(SM_CXSCREEN) - 300;
    }

    if (pProperties->video.windowY > GetSystemMetrics(SM_CYSCREEN) - 300) {
        pProperties->video.windowY = GetSystemMetrics(SM_CYSCREEN) - 300;
    }

    SetWindowPos(st.hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER);

    st.enteringFullscreen = 0;

    soundDriverConfig(st.hwnd, pProperties->sound.driver);
    emulatorRestartSound();

    /* Driver runs 2ch unconditionally; bring mixer's stereo flag in line
    ** with the user's saved preference now that the driver no longer
    ** sets it from its channel count. */
    mixerSetStereo(st.mixer, pProperties->sound.stereo);

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerSetChannelTypeVolume(st.mixer, i, pProperties->sound.mixerChannel[i].volume);
        mixerSetChannelTypePan(st.mixer, i, pProperties->sound.mixerChannel[i].pan);
        mixerEnableChannelType(st.mixer, i, pProperties->sound.mixerChannel[i].enable);
    }
    
    mixerSetMasterVolume(st.mixer, pProperties->sound.masterVolume);
    mixerEnableMaster(st.mixer, pProperties->sound.masterEnable);

    videoUpdateAll(st.pVideo, pProperties);
    
    mediaDbSetDefaultRomType(pProperties->cartridge.defaultType);

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        if (pProperties->media.carts[i].fileName[0]) insertCartridge(pProperties, i, pProperties->media.carts[i].fileName, pProperties->media.carts[i].fileNameInZip, pProperties->media.carts[i].type, -1);
        updateExtendedRomName(i, pProperties->media.carts[i].fileName, pProperties->media.carts[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_DISKS; i++) {
        if (pProperties->media.disks[i].fileName[0]) insertDiskette(pProperties, i, pProperties->media.disks[i].fileName, pProperties->media.disks[i].fileNameInZip, -1);
        updateExtendedDiskName(i, pProperties->media.disks[i].fileName, pProperties->media.disks[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_TAPES; i++) {
        if (pProperties->media.tapes[i].fileName[0]) insertCassette(pProperties, i, pProperties->media.tapes[i].fileName, pProperties->media.tapes[i].fileNameInZip, 0);
        updateExtendedCasName(i, pProperties->media.tapes[i].fileName, pProperties->media.tapes[i].fileNameInZip);
    }

    // Call initStatistics to get correct ram size and vram size for status bars
    {
        Machine* machine = machineCreate(pProperties->emulation.machineName);
        if (machine != NULL) {
            boardSetMachine(machine);
            machineDestroy(machine);
        }
    }
    boardSetFdcTimingEnable(pProperties->emulation.enableFdcTiming);
    boardSetNoSpriteLimits(pProperties->emulation.noSpriteLimits);
    boardSetY8950Enable(pProperties->sound.chip.enableY8950);
    boardSetYm2413Enable(pProperties->sound.chip.enableYM2413);
    boardSetMoonsoundEnable(pProperties->sound.chip.enableMoonsound);
    boardSetVideoAutodetect(pProperties->video.detectActiveMonitor);

    updateMenu(0);

    if (emuTryStartWithArguments(pProperties, szLine, NULL) < 0) {           
        exit(0);
        return 0;
    }

    st.themePageActive = NULL;
    {
        ThemeCollection* builtins[3];
        builtins[0] = themeClassicCreate();
        builtins[1] = themeClassicCreateDark();
        builtins[2] = NULL;
        st.themeList = createThemeList(builtins);
    }
    themeSet(emuCheckThemeArgument(szLine), 0);

    archUpdateWindow();
    ShowWindow(st.hwnd, SW_NORMAL);
    UpdateWindow(st.hwnd);

    if (pProperties->emulation.priorityBoost) {
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    }

    while (!doExit) {
        DWORD rv = MsgWaitForMultipleObjects(1, &st.ddrawEvent, FALSE, INFINITE, QS_ALLINPUT);    
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                doExit = 1;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (rv == WAIT_OBJECT_0) {
            if (!st.minimized) {
                emuWindowDraw(st.diplayUpdateOnVblank);
            }
            SetEvent(st.ddrawAckEvent);
        }
    }

    emulatorExit();
    sprintf(pProperties->keyboard.configFile, keyboardGetCurrentConfig());
    shortcutsDestroyProfile(st.shortcuts);
    videoDestroy(st.pVideo);
    
    SetCurrentDirectoryU(st.pCurDir);

    videoInCleanup(pProperties);
    ethIfCleanup(pProperties);
    cdromCleanup();

    pProperties->joy1.typeId = joystickPortGetType(0);
    pProperties->joy2.typeId = joystickPortGetType(1);
    propDestroy(pProperties);

    archSoundDestroy();
    Sleep(300);
    mixerDestroy(st.mixer);
    midiShutdown();

    SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, screensaverActive, 0, SPIF_SENDWININICHANGE); 

    if (kbdLockDisable) {
        kbdLockDisable();
    }
    FreeLibrary(kbdLockInst);

    CoUninitialize();

    exit(0);

    return 0;
}



////////////////////////////////////////////////////////////////////////////////////////

void archShowCassettePosDialog()
{
    enterDialogShow();
    setTapePosition(getMainHwnd(), pProperties);
    exitDialogShow();
}

void archShowHelpDialog()
{
    HINSTANCE rv = 0;
    /* NOTE: leaks 10 resource handles everytime ShellExecute is called, XP SP2 */
    if (pProperties->language == EMU_LANG_JAPANESE) {
         rv = ShellExecute(getMainHwnd(), "open", "blueMSXjp.chm", NULL, NULL, SW_SHOWNORMAL);
    }
    if (pProperties->language == EMU_LANG_DUTCH) {
         rv = ShellExecute(getMainHwnd(), "open", "blueMSXnl.chm", NULL, NULL, SW_SHOWNORMAL);
    }
    if (rv <= (HINSTANCE)32) {
        rv = ShellExecute(getMainHwnd(), "open", "blueMSX.chm", NULL, NULL, SW_SHOWNORMAL);
    }
    if (rv <= (HINSTANCE)32) {
        MessageBoxU(NULL, langErrorNoHelp(), langErrorTitle(), MB_OK);
    }
}

void archShowAboutDialog()
{
    enterDialogShow();
    helpShowAbout(getMainHwnd());
    exitDialogShow();
}

void archShowNoRomInZipDialog() {
    enterDialogShow();
    MessageBoxU(NULL, langErrorNoRomInZip(), langErrorTitle(), MB_OK);
    exitDialogShow();
}

void archShowNoDiskInZipDialog() {
    enterDialogShow();
    MessageBoxU(NULL, langErrorNoDskInZip(), langErrorTitle(), MB_OK);
    enterDialogShow();
}

void archShowNoCasInZipDialog() {
    enterDialogShow();
    MessageBoxU(NULL, langErrorNoCasInZip(), langErrorTitle(), MB_OK);
    enterDialogShow();
}

/* IDD_LARGEMSG custom dialog backing MessageBoxLargeU: 11pt, ~800px wide,
** auto-sized to text, optional 48px MB_ICON*; localized button captions. */
typedef struct {
    const wchar_t* mainInstr;
    const wchar_t* content;
    const wchar_t* caption;
    UINT           type;
} LargeMsgInfo;

static INT_PTR CALLBACK largeMsgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static LargeMsgInfo* info;
    switch (msg) {
    case WM_INITDIALOG: {
        info = (LargeMsgInfo*)lParam;
        SetWindowTextW(hDlg, info->caption ? info->caption : L"");

        /* Combine optional main instruction + body (Win32 needs \r\n). */
        wchar_t fullText[8192];
        if (info->mainInstr && info->mainInstr[0]) {
            _snwprintf(fullText, _countof(fullText) - 1, L"%s\r\n\r\n%s",
                       info->mainInstr,
                       info->content ? info->content : L"");
        } else {
            _snwprintf(fullText, _countof(fullText) - 1, L"%s",
                       info->content ? info->content : L"");
        }
        fullText[_countof(fullText) - 1] = 0;
        SetDlgItemTextW(hDlg, IDC_LARGEMSG_TEXT, fullText);

        /* SS_NOPREFIX: paths with '&' must not become accelerator hints. */
        HWND hStatic = GetDlgItem(hDlg, IDC_LARGEMSG_TEXT);
        SetWindowLong(hStatic, GWL_STYLE,
                      GetWindowLong(hStatic, GWL_STYLE) | SS_NOPREFIX);

        /* Localize Cancel/Yes/No; OK stays universal. */
        SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());
        SetWindowTextU(GetDlgItem(hDlg, IDYES),    langDlgYes());
        SetWindowTextU(GetDlgItem(hDlg, IDNO),     langDlgNo());

        /* MB_ICON* -> 48x48 system icon; NULL hides the icon column. */
        const int iconSize = 48;
        HICON hIcon = NULL;
        UINT iconFlag = info->type & 0xF0;
        switch (iconFlag) {
            case MB_ICONINFORMATION:
                hIcon = (HICON)LoadImageW(NULL, (LPCWSTR)IDI_INFORMATION, IMAGE_ICON,
                                          iconSize, iconSize, LR_SHARED); break;
            case MB_ICONWARNING:
                hIcon = (HICON)LoadImageW(NULL, (LPCWSTR)IDI_WARNING, IMAGE_ICON,
                                          iconSize, iconSize, LR_SHARED); break;
            case MB_ICONHAND:        /* alias of MB_ICONERROR */
                hIcon = (HICON)LoadImageW(NULL, (LPCWSTR)IDI_ERROR, IMAGE_ICON,
                                          iconSize, iconSize, LR_SHARED); break;
            case MB_ICONQUESTION:
                hIcon = (HICON)LoadImageW(NULL, (LPCWSTR)IDI_QUESTION, IMAGE_ICON,
                                          iconSize, iconSize, LR_SHARED); break;
        }
        HWND hIconCtrl = GetDlgItem(hDlg, IDC_LARGEMSG_ICON);
        if (hIcon) {
            SendMessageW(hIconCtrl, STM_SETICON, (WPARAM)hIcon, 0);
            ShowWindow(hIconCtrl, SW_SHOW);
        } else {
            ShowWindow(hIconCtrl, SW_HIDE);
        }

        /* Collect visible buttons in display order; hide the rest. */
        UINT btnFlags = info->type & 0x0F;
        int showOk     = (btnFlags == MB_OK || btnFlags == MB_OKCANCEL);
        int showCancel = (btnFlags == MB_OKCANCEL || btnFlags == MB_YESNOCANCEL);
        int showYes    = (btnFlags == MB_YESNO || btnFlags == MB_YESNOCANCEL);
        int showNo     = (btnFlags == MB_YESNO || btnFlags == MB_YESNOCANCEL);

        HWND btnHwnds[4];
        int  btnCount = 0;
        if (showOk)     btnHwnds[btnCount++] = GetDlgItem(hDlg, IDOK);
        if (showYes)    btnHwnds[btnCount++] = GetDlgItem(hDlg, IDYES);
        if (showNo)     btnHwnds[btnCount++] = GetDlgItem(hDlg, IDNO);
        if (showCancel) btnHwnds[btnCount++] = GetDlgItem(hDlg, IDCANCEL);
        if (btnCount == 0) {
            /* Fall back to OK so the dialog is dismissable. */
            btnHwnds[btnCount++] = GetDlgItem(hDlg, IDOK);
        }
        ShowWindow(GetDlgItem(hDlg, IDOK),     showOk     || btnCount == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDCANCEL), showCancel ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDYES),    showYes    ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDNO),     showNo     ? SW_SHOW : SW_HIDE);

        /* Measure text and size dialog to fit; maxTextW caps long paths. */
        HFONT font = (HFONT)SendMessageW(hStatic, WM_GETFONT, 0, 0);
        HDC   hdc  = GetDC(hStatic);
        HFONT oldFont = (HFONT)SelectObject(hdc, font);

        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        int lineHeight = tm.tmHeight + tm.tmExternalLeading;

        RECT  measureRect = { 0, 0, 0, 0 };
        DrawTextW(hdc, fullText, -1, &measureRect,
                  DT_CALCRECT | DT_LEFT | DT_NOPREFIX);
        int rawW = measureRect.right  - measureRect.left;
        int rawH = measureRect.bottom - measureRect.top;

        int minTextW = 200;
        int maxTextW = 720;
        int textW    = rawW;
        int textH    = rawH;
        if (textW > maxTextW) {
            textW = maxTextW;
            RECT wrapRect = { 0, 0, maxTextW, 10000 };
            DrawTextW(hdc, fullText, -1, &wrapRect,
                      DT_CALCRECT | DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
            textH = wrapRect.bottom - wrapRect.top;
        }
        if (textW < minTextW) textW = minTextW;

        SelectObject(hdc, oldFont);
        ReleaseDC(hStatic, hdc);

        /* Button geometry from .rc template (DPI / theme honoured). */
        RECT btnRect;
        GetWindowRect(btnHwnds[0], &btnRect);
        int btnW = btnRect.right  - btnRect.left;
        int btnH = btnRect.bottom - btnRect.top;

        int padX     = 20;   // left/right inset for content
        int padTop   = 14;   // top inset
        int padMid   = 18;   // gap between text row and button row
        int padBot   = 14;   // bottom inset under button row
        int btnGap   = 10;   // px between buttons
        int iconW    = hIcon ? iconSize : 0;
        int iconH    = hIcon ? iconSize : 0;
        int iconGap  = hIcon ? 16 : 0;   // space between icon and text

        /* Vertically align icon center with first text line. */
        int topShift = 4;
        int iconY    = padTop + topShift;
        int textY    = padTop + topShift;
        if (hIcon && iconH > lineHeight) {
            textY += (iconH - lineHeight) / 2;
        }
        int rowBottom = iconY + iconH;
        if (textY + textH > rowBottom) rowBottom = textY + textH;
        int rowH     = rowBottom - padTop;

        int buttonsW = btnCount * btnW + (btnCount - 1) * btnGap;
        int rowW     = iconW + iconGap + textW;
        int clientW  = (rowW > buttonsW ? rowW : buttonsW) + padX * 2;
        int clientH  = padTop + rowH + padMid + btnH + padBot;

        int rowX     = (clientW - rowW) / 2;
        int iconX    = rowX;
        int textX    = rowX + iconW + iconGap;
        int btnY     = padTop + rowH + padMid;
        int btnsX    = (clientW - buttonsW) / 2;

        if (hIcon) {
            SetWindowPos(hIconCtrl, NULL, iconX, iconY, iconW, iconH, SWP_NOZORDER);
        }
        SetWindowPos(hStatic, NULL, textX, textY, textW, textH, SWP_NOZORDER);
        for (int i = 0; i < btnCount; i++) {
            SetWindowPos(btnHwnds[i], NULL,
                         btnsX + i * (btnW + btnGap), btnY,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        /* Resize dialog to fit client area + non-client overhead. */
        RECT wr, cr;
        GetWindowRect(hDlg, &wr);
        GetClientRect(hDlg, &cr);
        int ncW = (wr.right - wr.left) - (cr.right - cr.left);
        int ncH = (wr.bottom - wr.top) - (cr.bottom - cr.top);
        int newW = clientW + ncW;
        int newH = clientH + ncH;

        /* Resize first, then center via shared helper (handles missing owner). */
        SetWindowPos(hDlg, NULL, 0, 0, newW, newH, SWP_NOMOVE | SWP_NOZORDER);
        win32CommonCenterOnOwner(hDlg);

        win32CommonApplyDark(hDlg);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK || id == IDCANCEL || id == IDYES || id == IDNO) {
            EndDialog(hDlg, id);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

int MessageBoxLargeU(HWND hwnd, const char* mainInstr, const char* content,
                     const char* caption, UINT type)
{
    wchar_t wmain[1024];
    wchar_t wcontent[8192];
    wchar_t wcap[256];
    if (mainInstr && *mainInstr) {
        Utf8ToWide(mainInstr, wmain, _countof(wmain));
    } else {
        wmain[0] = 0;
    }
    Utf8ToWide(content ? content : "", wcontent, _countof(wcontent));
    Utf8ToWide(caption ? caption : "", wcap,     _countof(wcap));

    LargeMsgInfo info;
    info.mainInstr = wmain[0] ? wmain : NULL;
    info.content   = wcontent;
    info.caption   = wcap;
    info.type      = type;

    INT_PTR rv = DialogBoxParamW(GetModuleHandle(NULL),
                                 MAKEINTRESOURCEW(IDD_LARGEMSG),
                                 hwnd, largeMsgProc, (LPARAM)&info);
    if (rv == -1 || rv == 0) {
        /* Dialog template missing or load failed: fall back to plain
        ** MessageBox so callers still get a visible dialog. */
        return MessageBoxW(hwnd, wcontent, wcap, type);
    }
    return (int)rv;
}

void archShowStartEmuFailDialog() {
    MessageBoxU(NULL, langErrorStartEmu(), langErrorTitle(), MB_ICONHAND | MB_OK);
}

void archShowLanguageDialog()
{
    int lang;
    int success;
    int i;

    enterDialogShow();
    lang = langShowDlg(getMainHwnd(), pProperties->language);
    exitDialogShow();
    success = langSetLanguage(lang);
    if (success) {
        pProperties->language = lang;
        if (appConfigGetInt("menu.file", 1) != 0) {
            addMenuItem(langMenuFile(), actionMenuFile, 0);
        }
        if (appConfigGetInt("menu.emulation", 1) != 0) {
            addMenuItem(langMenuRun(), actionMenuRun, 1);
        }
        if (appConfigGetInt("menu.window", 1) != 0) {
            addMenuItem(langMenuWindow(), actionMenuZoom, 1);
        }
        if (appConfigGetInt("menu.options", 1) != 0) {
            addMenuItem(langMenuOptions(), actionMenuOptions, 1);
        }
        if (appConfigGetInt("menu.tools", 1) != 0) {
            addMenuItem(langMenuTools(), actionMenuTools, 1);
        }
        if (appConfigGetInt("menu.help", 1) != 0) {
            addMenuItem(langMenuHelp(), actionMenuHelp, 1);
        }
    }
    
    for (i = 0; i < toolGetCount(); i++) {
        toolInfoSetLanguage(toolInfoGet(i), pProperties->language);
    }

    updateMenu(0);
}

void archShowShortcutsEditor() 
{
    int apply;
    enterDialogShow();
    apply = shortcutsShowDialog(getMainHwnd(), pProperties);
    shortcutsDestroyProfile(st.shortcuts);
    st.shortcuts = shortcutsCreateProfile(pProperties->emulation.shortcutProfile);
    updateMenu(0);
    exitDialogShow();
}

/* Tool-window scale = (mainZoom + 1) / 2 clamped to [1.0, 4.0]; cache
   keyed on halfSteps = (int)(scale * 2), range 2..8.  Non-DDraw
   fullscreen derives equivalent zoom from the host monitor. */
#define TOOLTHEME_MIN_HALFSTEPS 2  /* scale 1.0 */
#define TOOLTHEME_MAX_HALFSTEPS 8  /* scale 4.0 */
#define TOOLTHEME_CACHE_SIZE    (TOOLTHEME_MAX_HALFSTEPS + 1)

static int toolThemeHalfSteps(void)
{
    int isDDrawFs = pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN &&
                    (pProperties->video.driver == P_VIDEO_DRVDIRECTX_VIDEO ||
                     pProperties->video.driver == P_VIDEO_DRVDIRECTX);
    int halfSteps;
    if (pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN && !isDDrawFs) {
        /* D3D12 fullscreen lacks a display mode; derive zoom from the
           main window's monitor (matches DDraw's getZoom formula). */
        int eqZoom = 4;
        HMONITOR hMon = MonitorFromWindow(st.hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        if (hMon && GetMonitorInfo(hMon, &mi)) {
            int screenW = mi.rcMonitor.right  - mi.rcMonitor.left;
            int screenH = mi.rcMonitor.bottom - mi.rcMonitor.top;
            eqZoom = min(min(screenW / 320, screenH / 240), 8);
            if (eqZoom < 1) eqZoom = 1;
        }
        halfSteps = eqZoom + 1;
    }
    else {
        halfSteps = getZoom() + 1;
    }
    if (halfSteps < TOOLTHEME_MIN_HALFSTEPS) halfSteps = TOOLTHEME_MIN_HALFSTEPS;
    if (halfSteps > TOOLTHEME_MAX_HALFSTEPS) halfSteps = TOOLTHEME_MAX_HALFSTEPS;
    return halfSteps;
}

/* Walk every per-scale cache slot for an existing instance of the
   requested aux window so a zoom toggle doesn't spawn a duplicate. */
static int toolWindowBringExistingToFront(ThemeCollection** cache,
                                          int cacheSize,
                                          unsigned long hash)
{
    int h, i;
    for (h = 0; h < cacheSize; h++) {
        if (cache[h] == NULL) continue;
        for (i = 0; i < THEME_MAX_WINDOWS; i++) {
            if (cache[h]->theme[i] != NULL &&
                cache[h]->theme[i]->reference != NULL &&
                themeGetNameHash(cache[h]->theme[i]->name) == hash)
            {
                SetForegroundWindow((HWND)cache[h]->theme[i]->reference);
                return 1;
            }
        }
    }
    return 0;
}

void archShowKeyboardEditor()
{
    static ThemeCollection* tc[TOOLTHEME_CACHE_SIZE] = { NULL };
    unsigned long hash = themeGetNameHash("blueMSX - Input Editor");
    int hs;
    
    if (toolWindowBringExistingToFront(tc, TOOLTHEME_CACHE_SIZE, hash)) {
        return;
    }

    hs = toolThemeHalfSteps();
    if (tc[hs] == NULL) {
        char themePath[MAX_PATH];
        GetCurrentDirectoryU(MAX_PATH, themePath);
        strcat(themePath, "\\Keyboard Config\\Theme");
        tc[hs] = themeLoadAtScale(themePath, hs / 2.0);
    }

    if (tc[hs] == NULL) {
        MessageBoxU(NULL, "Could not find the Keyboard Editor Theme", langErrorTitle(), MB_ICONERROR | MB_OK);
    }
    else {
        themeCollectionOpenWindow(tc[hs], hash);
    }
}

void archShowMixer()
{
    static ThemeCollection* tc[TOOLTHEME_CACHE_SIZE] = { NULL };
    unsigned long hash = themeGetNameHash("blueMSX - Sound Mixer");
    int hs;
    
    if (toolWindowBringExistingToFront(tc, TOOLTHEME_CACHE_SIZE, hash)) {
        return;
    }

    hs = toolThemeHalfSteps();
    if (tc[hs] == NULL) {
        char themePath[MAX_PATH];
        GetCurrentDirectoryU(MAX_PATH, themePath);
        strcat(themePath, "\\Properties\\Mixer");
        tc[hs] = themeLoadAtScale(themePath, hs / 2.0);
    }

    if (tc[hs] == NULL) {
        MessageBoxU(NULL, "Could not find the Mixer Theme", langErrorTitle(), MB_ICONERROR | MB_OK);
    }
    else {
        themeCollectionOpenWindow(tc[hs], hash);
    }
}

void archShowDebugger()
{
    ToolInfo* ti = toolInfoFind("Debugger");
    if (ti != NULL) {
        toolInfoShowTool(ti);
    }
}

void archShowTrainer()
{
    ToolInfo* ti = toolInfoFind("Trainer");
    if (ti != NULL) {
        toolInfoShowTool(ti);
    }
}

void archShowMachineEditor()
{
    int apply;
    enterDialogShow();
    apply = confShowDialog(getMainHwnd(), pProperties->emulation.machineName);
    exitDialogShow();
    if (apply) {
        actionEmuResetHard();
    }
    updateMenu(0);
}

void* archScreenCapture(ScreenCaptureType type, int* bitmapSize, int onlyBmp)
{
    int png = onlyBmp ? 0 : pProperties->settings.usePngScreenshots;

    if (bitmapSize != NULL) {
        *bitmapSize = 0;
    }
    switch (type) {
    case SC_NORMAL:
        if (png) {
            createScreenShot(1, NULL, png);
        }
        else {
            SetTimer(getMainHwnd(), TIMER_SCREENSHOT, 50, NULL);
        }
        return NULL;
    case SC_SMALL:
        return createScreenShot(0, bitmapSize, png);
    case SC_LARGE:
        return createScreenShot(1, bitmapSize, png);
    }

    return NULL;
}

void archMinimizeMainWindow() {
    ShowWindow(getMainHwnd(), SW_MINIMIZE);
    updateMenu(0);
}

void archDiskQuickChangeNotify() 
{
    diskQuickviewWindowShow(st.dskWnd);
}

////////////////////////////////////////////////////////////////////
// File open/save stuff

static void replaceCharInString(char* str, char oldChar, char newChar) 
{
    while (*str) {
        if (*str == oldChar) {
            *str = newChar;
        }
        str++;
    }
}

char* archFileSave(char* title, char* extensionList, char* defaultDir, char* extensions, int* selectedExtension, char* defExt)
{
    char* fileName;

    enterDialogShow();
    fileName = saveFile(getMainHwnd(), title, extensionList, selectedExtension, defaultDir, defExt);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

const char* archGetCurrentDirectory()
{
    static char pathname[512];
    GetCurrentDirectoryU(512, pathname);
    return pathname;
}

int archCreateDirectory(const char* pathname)
{
    return mkdirU(pathname);
}

void archSetCurrentDirectory(const char* pathname)
{
    SetCurrentDirectoryU(pathname);
}

char* archDirnameGetOpenDisk(Properties* properties, int drive)
{
    char* title = drive == 1 ? langDlgInsertDiskB() : langDlgInsertDiskA();
    char* defaultDir = properties->media.disks[drive].directory;
    char* filename;

    enterDialogShow();
    filename = openDir(getMainHwnd(), title, defaultDir);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return filename;
}

char* archFileOpen(char* title, char* extensionList, char* defaultDir, char* extensions, int* selectedExtension, char* defautExtension, int createFileSize)
{
    char* fileName;

    enterDialogShow();
    fileName = openFile(getMainHwnd(), title, extensionList, defaultDir, createFileSize, defautExtension, selectedExtension);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

char* archFilenameGetOpenState(Properties* properties)
{
    char* title = langDlgLoadState();
    char extensionList[512];
    char* defaultDir = properties->emulation.statsDefDir;
    char* extensions = ".sta\0";
    int* selectedExtension = NULL;
    char* defautExtension = NULL;
    int createFileSize = -1;
    char* fileName;

    sprintf(extensionList, "%s   (*.sta)#*.sta#", langFileCpuState());
    replaceCharInString(extensionList, '#', 0);

    enterDialogShow();
    fileName = openStateFile(getMainHwnd(), title, extensionList, defaultDir, createFileSize, defautExtension, 
                             selectedExtension, &pProperties->settings.showStatePreview);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

char* archFilenameGetOpenCapture(Properties* properties)
{
    char* title = langDlgLoadVideoCapture();
    char extensionList[512];
    char* defaultDir = properties->emulation.statsDefDir;
    char* extensions = ".cap\0";
    int* selectedExtension = NULL;
    char* defautExtension = NULL;
    int createFileSize = -1;
    char* fileName;

    sprintf(extensionList, "%s   (*.cap)#*.cap#", langFileVideoCapture());
    replaceCharInString(extensionList, '#', 0);

    enterDialogShow();
    fileName = openStateFile(getMainHwnd(), title, extensionList, defaultDir, createFileSize, defautExtension, 
                             selectedExtension, &pProperties->settings.showStatePreview);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

char* archFilenameGetOpenRom(Properties* properties, int cartSlot, RomType* romType) 
{
    char* defaultDir = properties->cartridge.defDir;
    int* selectedExtension = &properties->media.carts[cartSlot].extensionFilter;
    char* defautExtension = ".rom";
    char* extensions = ".rom\0.ri\0.mx1\0.mx2\0.col\0.sms\0.sg\0.sc\0.zip\0.*\0";
    char* title = cartSlot == 1 ? langDlgInsertRom2() : langDlgInsertRom1();
    char* fileName;
    char extensionList[512];

    // DINK: different defDir depending on machine
    if (strcasestr(properties->emulation.machineName, "Coleco")!=0) {
        defaultDir = properties->cartridge.defDirCOLECO; }
    if (strcasestr(properties->emulation.machineName, "SEGA")!=0) {
        defaultDir = properties->cartridge.defDirSEGA; }
    if ((strcasestr(properties->emulation.machineName, "SVI-318")!=0) ||
        (strcasestr(properties->emulation.machineName, "SVI-328")!=0)) {
        defaultDir = properties->cartridge.defDirSVI; }

    sprintf(extensionList, "%s   (*.rom, *.ri, *.mx1, *.mx2, *.col, *.sms, *.sg, *.sc, *.zip)#*.rom; *.ri; *.mx1; *.mx2; *.col; *.sg; *.sc; *.zip#%s   (*.*)#*.*#", langFileRom(), langFileAll());
    replaceCharInString(extensionList, '#', 0);

    enterDialogShow();
    fileName = openRomFile(getMainHwnd(), title, extensionList, defaultDir, 1, defautExtension, selectedExtension, romType);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

char* archFilenameGetOpenCas(Properties* properties)
{
    char* title = langDlgInsertCas();
    char  extensionList[512];
    char* defaultDir = properties->cassette.defDir;
    char* extensions = ".cas\0.zip\0.*\0";
    int* selectedExtension = &properties->media.tapes[0].extensionFilter;
    char* defautExtension = ".cas";
    int createFileSize = 0;
    char* fileName;

    sprintf(extensionList, "%s   (*.cas, *.zip)#*.cas; *.zip#%s   (*.*)#*.*#", langFileCas(), langFileAll());
    replaceCharInString(extensionList, '#', 0);

    enterDialogShow();
    fileName = openFile(getMainHwnd(), title, extensionList, defaultDir, createFileSize, defautExtension, selectedExtension);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

char* archFilenameGetOpenDisk(Properties* properties, int drive, int allowCreate)
{
    char* title = drive == 1 ? langDlgInsertDiskB() : langDlgInsertDiskA();
    char  extensionList[512];
    char* defaultDir = properties->diskdrive.defDir;
    char* extensions = ".dsk\0.di1\0.di2\0.360\0.720\0.sf7\0.zip\0";
    int* selectedExtension = &properties->media.disks[drive].extensionFilter;
    char* defautExtension = ".dsk";
    int createFileSize = 720 * 1024;
    char* fileName;

    sprintf(extensionList, "%s   (*.dsk, *.di1, *.di2, *.360, *.720, *.sf7, *.zip)#*.dsk; *.di1; *.di2; *.360; *.720; *.sf7; *.zip#%s   (*.*)#*.*#", langFileDisk(), langFileAll());
    replaceCharInString(extensionList, '#', 0);

    enterDialogShow();
    if (allowCreate) {
        fileName = openNewDskFile(getMainHwnd(), title, extensionList, defaultDir, defautExtension, selectedExtension);
    }
    else {
        fileName = openFile(getMainHwnd(), title, extensionList, defaultDir, createFileSize, defautExtension, selectedExtension);
    }
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

char* archFilenameGetOpenHarddisk(Properties* properties, int drive, int allowCreate)
{
    char* title = langDlgInsertHarddisk();
    char  extensionList[512];
    char* defaultDir = properties->diskdrive.defHdDir;
    char* extensions = ".dsk\0.di1\0.di2\0.360\0.720\0.sf7\0.zip\0";
    int* selectedExtension = &properties->media.disks[drive].extensionFilter;
    char* defautExtension = ".dsk";
    char* fileName;

    sprintf(extensionList, "%s   (*.dsk, *.di1, *.di2, *.360, *.720, *.sf7, *.zip)#*.dsk; *.di1; *.di2; *.360; *.720; *.sf7; *.zip#%s   (*.*)#*.*#", langFileDisk(), langFileAll());
    replaceCharInString(extensionList, '#', 0);

    enterDialogShow();
    if (allowCreate) {
        fileName = openNewHdFile(getMainHwnd(), title, extensionList, defaultDir, defautExtension, selectedExtension);
    }
    else {
        fileName = openFile(getMainHwnd(), title, extensionList, defaultDir, -1, defautExtension, selectedExtension);
    }
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

char* archFilenameGetSaveCas(Properties* properties, int* type)
{
    char* title = langDlgSaveCassette();
    char  extensionList[512];
    char* defaultDir = properties->cassette.defDir;
    char* extensions = ".cas\0";
    int* selectedExtension = type;

    sprintf(extensionList, "%s - fMSX-DOS     (*.cas)#*.cas#%s - fMSX98/AT   (*.cas)#*.cas#%s - SVI-328         (*.cas)#*.cas#", langFileCas(), langFileCas(), langFileCas());
    replaceCharInString(extensionList, '#', 0);

    return archFileSave(title, extensionList, defaultDir, extensions, selectedExtension, ".cas");
}

char* archFilenameGetSaveState(Properties* properties)
{
    char* title = langDlgSaveState();
    char  extensionList[512];
    char* defaultDir = properties->emulation.statsDefDir;
    char* extensions = ".sta\0";
    int* selectedExtension = NULL;
    char* fileName;

    sprintf(extensionList, "%s   (*.sta)#*.sta#", langFileCpuState());
    replaceCharInString(extensionList, '#', 0);

    enterDialogShow();
    fileName = saveStateFile(getMainHwnd(), title, extensionList, selectedExtension, defaultDir, &pProperties->settings.showStatePreview);
    exitDialogShow();
    SetCurrentDirectoryU(st.pCurDir);

    return fileName;
}

void archVideoOutputChange()
{
    updateMenu(0);
}

void archUpdateMenu(int show) {
    updateMenu(show);
}

void archQuit() {
    DestroyWindow(getMainHwnd());
}

char* archFilenameGetOpenAnyZip(Properties* properties, const char* fname, const char* fileList, int count, int* autostart, int* romType)
{
    static char filename[512];
    ZipFileDlgInfo dlgInfo;

    sprintf(dlgInfo.title, "%s", langDlgLoadRomDskCas());
    sprintf(dlgInfo.description, "%s", langDlgLoadRomDskCasDesc());
    strcpy(dlgInfo.zipFileName, filename);
    dlgInfo.fileList = fileList;
    dlgInfo.fileListCount = count;
    dlgInfo.autoReset = properties->diskdrive.autostartA || properties->cartridge.autoReset || *autostart;
    dlgInfo.selectFileIndex = -1;
    dlgInfo.selectFile[0] = 0;

    enterDialogShow();
    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ZIPDSK), getMainHwnd(), dskZipDlgProc, (LPARAM)&dlgInfo);
    exitDialogShow();

    if (dlgInfo.selectFile[0] == '\0') {
        return NULL;
    }
    *romType = dlgInfo.openRomType;
    *autostart = dlgInfo.autoReset;
    strcpy(filename, dlgInfo.selectFile);
    return filename;
}

char* archFilenameGetOpenDiskZip(Properties* properties, int drive, const char* fname, const char* fileList, int count, int* autostart)
{
    static char filename[512];
    ZipFileDlgInfo dlgInfo;

    sprintf(dlgInfo.title, "%s", langDlgLoadDsk());
    sprintf(dlgInfo.description, "%s", langDlgLoadDskDesc());
    strcpy(dlgInfo.zipFileName, fname);
    dlgInfo.fileList = fileList;
    dlgInfo.fileListCount = count;
    dlgInfo.autoReset = *autostart;

    dlgInfo.selectFileIndex = -1;
    strcpy(dlgInfo.selectFile, drive == 0 ? properties->media.disks[0].fileNameInZip : properties->media.disks[1].fileNameInZip);

    enterDialogShow();
    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ZIPDSK), getMainHwnd(), dskZipDlgProc, (LPARAM)&dlgInfo);
    exitDialogShow();

    if (dlgInfo.selectFile[0] == '\0') {
        return NULL;
    }
    *autostart = dlgInfo.autoReset;
    strcpy(filename, dlgInfo.selectFile);
    return filename;
}

char* archFilenameGetOpenCasZip(Properties* properties, const char* fname, const char* fileList, int count, int* autostart)
{
    static char filename[512];
    ZipFileDlgInfo dlgInfo;

    sprintf(dlgInfo.title, "%s", langDlgLoadCas());
    sprintf(dlgInfo.description, "%s", langDlgLoadCasDesc());
    strcpy(dlgInfo.zipFileName, fname);
    dlgInfo.fileList = fileList;
    dlgInfo.fileListCount = count;
    dlgInfo.autoReset = *autostart;

    dlgInfo.selectFileIndex = -1;
    strcpy(dlgInfo.selectFile, properties->media.tapes[0].fileNameInZip);

    enterDialogShow();
    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ZIPDSK), getMainHwnd(), dskZipDlgProc, (LPARAM)&dlgInfo);
    exitDialogShow();

    if (dlgInfo.selectFile[0] == '\0') {
        return NULL;
    }
    *autostart = dlgInfo.autoReset;
    strcpy(filename, dlgInfo.selectFile);
    return filename;
}

char* archFilenameGetOpenRomZip(Properties* properties, int cartSlot, const char* fname, const char* fileList, int count, int* autostart, int* romType)
{
    static char filename[512];
    ZipFileDlgInfo dlgInfo;

    sprintf(dlgInfo.title, "%s", langDlgLoadRom());
    sprintf(dlgInfo.description, "%s", langDlgLoadRomDesc());
    strcpy(dlgInfo.zipFileName, fname);
    dlgInfo.fileList = fileList;
    dlgInfo.fileListCount = count;
    dlgInfo.autoReset = *autostart;

    dlgInfo.selectFileIndex = -1;
    strcpy(dlgInfo.selectFile, cartSlot == 0 ? properties->media.carts[0].fileNameInZip : properties->media.carts[1].fileNameInZip);

    enterDialogShow();
    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ZIPDSK), getMainHwnd(), dskZipDlgProc, (LPARAM)&dlgInfo);
    exitDialogShow();

    if (dlgInfo.selectFile[0] == '\0') {
        return NULL;
    }
    *romType = dlgInfo.openRomType;
    *autostart = dlgInfo.autoReset;
    strcpy(filename, dlgInfo.selectFile);
    return filename;
}


void archEmulationStartNotification() {
    if (st.renderVideo) {
        return;
    }
    /* The D3D12 swap chain's backbuffer still holds the previous run's last
    ** frame; SW_NORMAL would re-expose it briefly before the first new
    ** render lands. */
    if (pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12 && st.emuHwnd) {
        D3D12ClearToBlack(st.emuHwnd);
    }
    ShowWindow(st.emuHwnd, SW_NORMAL);

    if (kbdLockEnable != NULL && pProperties->emulation.disableWinKeys) {
        kbdLockEnable();
    }
}

void archEmulationStopNotification()
{
    if (st.renderVideo) {
        return;
    }

    DirectXSetGDISurface();
    ShowWindow(st.emuHwnd, SW_HIDE);

    if (kbdLockDisable != NULL) {
        kbdLockDisable();
    }
}

int archUpdateEmuDisplay(int syncMode) {
    st.diplayUpdateOnVblank = syncMode == 4;
    if (pProperties->video.driver == P_VIDEO_DRVGDI) {
        if (syncMode == 0) {
            PostMessage(getMainHwnd(), WM_UPDATE, 0, 0);
        }
    }
    else if (syncMode == 4) { // VBlank async
        SetEvent(st.ddrawEvent);
    }
    else if (syncMode == 3) { // VBlank sync
        st.diplaySync = 1;
        emuWindowDraw(0);
        return st.diplayUpdated;
    }
    else {
        SetEvent(st.ddrawEvent);
        if (syncMode > 1) {
            WaitForSingleObject(st.ddrawAckEvent, 500);
            return st.diplayUpdated;
        }
    }
    return 0;
}

void archUpdateEmuDisplayConfig() {
    updateEmuWindow();
}

void archThemeSetNext() {
    st.themeIndex++;
    if (st.themeList[st.themeIndex] == NULL) {
        st.themeIndex = 0;
    }
    
    strcpy(pProperties->settings.themeName, st.themeList[st.themeIndex]->name);

    archUpdateWindow();
}

void archThemeUpdate(Theme* theme) {
    if (theme->reference == NULL) {
        themeSet(pProperties->settings.themeName, 0);
    }
    else {
        SendMessage(theme->reference, WM_UPDATE, 0, 0);
    }
}

int archGetFramesPerSecond() {
    return st.framesPerSecond;
}

void archEmulationStartFailure() {
    aviStopRender();
    MessageBoxU(NULL, langErrorStartEmu(), langErrorTitle(), MB_ICONHAND | MB_OK);
}

int archFileExists(const char* fileName)
{
    /* fileName is UTF-8 (file dialog / history). PathFileExistsA interprets
    ** bytes as the runtime ACP, which mojibakes non-ACP filenames. */
    wchar_t wFileName[1024];
    PathToWide(fileName, wFileName, _countof(wFileName));
    return PathFileExistsW(wFileName);
}

int archFileDelete(const char* fileName)
{
    return DeleteFileU(fileName);
}

void archMaximizeWindow() {
    if (st.currentHwnd != NULL) {
//        ShowWindow(st.currentHwnd, SW_MAXIMIZE);
        SendMessage(st.currentHwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    }
}

void archMinimizeWindow() {
    if (st.currentHwnd != NULL) {
//        ShowWindow(st.currentHwnd, SW_MINIMIZE);
        SendMessage(st.currentHwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }
}

void archCloseWindow() {
    if (st.currentHwnd != NULL) {
        SendMessage(st.currentHwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
//        CloseWindow(st.currentHwnd);
    }
}

void archWindowStartMove() {
    if (st.currentHwnd != NULL) {
        GetCursorPos(&st.currentHwndMouse);
        GetWindowRect(st.currentHwnd, &st.currentHwndRect);
        st.currentHwndRect.right = 1; // Used flag to detect window move
    }
}

void archWindowMove() {
    if (st.currentHwnd != NULL && st.currentHwndRect.right) {
        POINT pt;
        int x;
        int y;
        GetCursorPos(&pt);
        x = st.currentHwndRect.left + pt.x - st.currentHwndMouse.x;
        y = st.currentHwndRect.top  + pt.y - st.currentHwndMouse.y;
        SetWindowPos(st.currentHwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    }
}

void archWindowEndMove() {
    st.currentHwndRect.right = 0;
}

void archVideoCaptureSave()
{
    actionEmuStop();

    st.renderVideo = 1;
    aviStartRender(getMainHwnd(), propGetGlobalProperties(), st.pVideo);
    st.renderVideo = 0;
}

void SetCurrentWindow(HWND hwnd) {
    st.currentHwnd = hwnd;
}

void archTrap(UInt8 value)
{
    if (appConfigGetInt("trap.quit", 0) != 0) {
        PostMessage(getMainHwnd(), WM_CLOSE, 0, 0);
    }
}


///////////////////////////////////////////////////////////////////////////


static int LoadMemory(const char* fileName, UInt16 address)
{
    FILE* file;

    file = fopen(fileName, "rb");
    if (file != NULL) {
        UInt8 data;
        while (address <= 0xffff && (fread(&data, 1, 1, file) == 1)) {
            slotWrite(NULL, address, data);
            address++;
        }
        fclose(file);
        return 1;
    }
    return 0;
}

static BOOL_DLG_RET CALLBACK loadMemorProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    static HICON hIconBtBrowse = NULL;

    switch (iMsg) {
    case WM_INITDIALOG:
        SetWindowTextU(hDlg, langMenuToolsLoadMemory());
        SetWindowTextU(GetDlgItem(hDlg, IDC_LDMEM_CAPFIL), langConfEditMemFile());
        SetWindowTextU(GetDlgItem(hDlg, IDC_LDMEM_CAPADR), langConfEditMemAddress());
        SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgOK());
        SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());

        if (hIconBtBrowse == NULL) {
            hIconBtBrowse = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_BROWSE));
        }
        SendMessage(GetDlgItem(hDlg, IDC_LDMEM_BROWSE), BM_SETIMAGE, IMAGE_ICON, (LPARAM)hIconBtBrowse);

        win32CommonApplyDark(hDlg);
        win32CommonCenterOnOwner(hDlg);
        return FALSE;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {

        case IDC_LDMEM_BROWSE: {
                static char  defDir[MAX_PATH] = { 0 };
                char  curDir[MAX_PATH];
                char* fileName;
                char extensionList[512];

                GetCurrentDirectoryU(MAX_PATH, curDir);
                if (strlen(defDir) == 0) {
                    strcpy(defDir, curDir);
                }
                sprintf(extensionList, "%s   (*.*)#*.*#", langFileAll());
                replaceCharInString(extensionList, '#', 0);

                fileName = openFile(hDlg, langConfOpenRom(), extensionList, defDir, -1, NULL, NULL);
                if (fileName != NULL) {
                   SetWindowTextU(GetDlgItem(hDlg, IDC_LDMEM_FILENAME), fileName);
                }

                SetFocus(GetDlgItem(hDlg, IDC_LDMEM_ADDRESS));
            }
            return TRUE;

            case IDOK: {
                char fileName[512];
                char data[5];
                int addr, rv;

                GetWindowTextU(GetDlgItem(hDlg, IDC_LDMEM_FILENAME), fileName, sizeof(fileName));
                GetWindowTextU(GetDlgItem(hDlg, IDC_LDMEM_ADDRESS), data, sizeof(data));

                rv = sscanf(data, "%x", &addr);
                if (rv == 1) {
//                    emulatorSuspend();
                    LoadMemory(fileName, (UInt16)addr);
//                    emulatorResume();
                }

                EndDialog(hDlg, TRUE);
                return TRUE;
            }
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

int showLoadMemoryDlg(HWND hwnd)
{
    int rv;

    rv = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_LOAD_MEMORY), hwnd, loadMemorProc);
    if (!rv) {
        return 0;
    }

    return 1;
}
