/*****************************************************************************
** File:        StatusBar.cpp
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
#include "StatusBar.h"
#include "Resource.h"
#include "Win32TextUtf8.h"
#include "DbgWindow.h"
#include "ToolInterface.h"
#include <windows.h>
#include <CommCtrl.h>

#define STATUSBAR_DARK_SUBCLASS_ID 0xC4EAEE

/* Status bar (msctls_statusbar) does not respect SetWindowTheme for its
** background -- it always paints with the system 3D face color. Subclass
** WM_PAINT and render the bar / text manually with dark colors. */
static LRESULT CALLBACK statusBarDarkProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR data)
{
    (void)data;
    if (msg == WM_PAINT && IsDarkMode()) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = GetDarkBgBrush();
        if (bg) FillRect(hdc, &rc, bg);

        HFONT hFont = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        HFONT hOld  = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
        SetTextColor(hdc, GetDarkFg());
        SetBkMode(hdc, TRANSPARENT);

        int parts = (int)SendMessageW(hwnd, SB_GETPARTS, 0, 0);
        for (int i = 0; i < parts; i++) {
            int rawLen = (int)SendMessageW(hwnd, SB_GETTEXTLENGTHW, i, 0);
            int len = rawLen & 0xFFFF;
            if (len <= 0 || len > 255) continue;
            wchar_t buf[256] = {0};
            SendMessageW(hwnd, SB_GETTEXTW, i, (LPARAM)buf);
            RECT pr;
            SendMessageW(hwnd, SB_GETRECT, i, (LPARAM)&pr);
            pr.left += 4;
            DrawTextW(hdc, buf, len, &pr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        if (hOld) SelectObject(hdc, hOld);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND && IsDarkMode()) return 1;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, statusBarDarkProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}


StatusBar::StatusBar(HINSTANCE hInstance, HWND owner, std::vector<int>& fieldWidthVec) :
    fieldWidth(fieldWidthVec)
{
    hwnd = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE, 
                          0, 0, 50, 50, owner, (HMENU) 443, hInstance, NULL);

    /* Switch to Unicode so SB_SETTEXTW (sent by setField) renders the wide
    ** Japanese strings correctly instead of mojibaking the UTF-8 bytes
    ** through CP932. */
    SendMessage(hwnd, CCM_SETUNICODEFORMAT, TRUE, 0);

    SetWindowSubclass(hwnd, statusBarDarkProc, STATUSBAR_DARK_SUBCLASS_ID, 0);

    updatePosition();
}

StatusBar::~StatusBar()
{
    DestroyWindow(hwnd);
}

void StatusBar::setField(int fieldIndex, const char* text)
{
    wchar_t wbuf[256];
    Utf8ToWide(text ? text : "", wbuf, _countof(wbuf));
    SendMessageW(hwnd, SB_SETTEXTW, fieldIndex, (LPARAM)wbuf);
}

void StatusBar::updatePosition()
{
    MoveWindow(hwnd, 0, 0, 0, 0, TRUE);

    RECT cr;
    GetClientRect(GetParent(hwnd), &cr);

    int segments = fieldWidth.size();
    int parts[64];

    parts[segments - 1] = cr.right;

    for (int i = segments - 1; i > 0; i--) {
        parts[i - 1] = parts[i] - fieldWidth[i];
    }
    
    SendMessage(hwnd, SB_SETPARTS, (WPARAM)segments, (LPARAM)parts); 
}

void StatusBar::show()
{
    ShowWindow(hwnd, true);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void StatusBar::hide()
{
    ShowWindow(hwnd, false);
}

int StatusBar::getHeight()
{
    if (!IsWindowVisible(hwnd)) {
        return 0;
    }
    RECT r;
    GetWindowRect(hwnd, &r);
    return r.bottom - r.top;
}
