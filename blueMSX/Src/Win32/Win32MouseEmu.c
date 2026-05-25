/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32MouseEmu.c,v $
**
** $Revision: 1.8 $
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
*/
#include "Win32MouseEmu.h"
#include "ArchInput.h"
#include <stdlib.h>

static HWND mouseHwnd;
static HCURSOR hCurs;
static int mouseIsRunning = 0;
static int mouseTimerId;
static RECT mouseCapRect;
static RECT mouseDisplayRect;
static int mouseActive;
static int mouseEnable;
static AmEnableMode mouseMode;
static int mouseDX;
static int mouseDY;
static int mouseX;
static int mouseY;
static int mouseLockDX;
static int mouseLockDY;
static int hasMouseLock;
static int mouseForceLock;
static int cursorCnt = 0;

/* Auto-hide cursor when idle in the emu area (AM_DISABLE only).
** fadeStep: 0=visible, 1..FADE_STEPS-1=fading, FADE_STEPS=hidden
** (ShowCursor(FALSE), polled for re-show because WM_MOUSEMOVE stops). */
#define AUTO_HIDE_TIMEOUT_MS 3000
#define FADE_STEPS           12  /* 11 visible alpha frames + ShowCursor(FALSE) */
#define FADE_STEP_MS         50  /* ~600 ms total fade */
static DWORD lastMouseActivity;
static int   autoHideArmed;
static int   fadeStep;            /* 0 visible .. FADE_STEPS hidden */
static DWORD fadeNextStepTime;
static HCURSOR fadeCursors[FADE_STEPS]; /* fadeCursors[0] unused */
static POINT lastCursorPos;       /* polled while fully hidden because
                                  ** ShowCursor(FALSE) stops WM_MOUSEMOVE */

static void rebuildFadeCursors(void);

/* Read Mouse Pointer Size accessibility slider from registry
** (no public API; SM_CXCURSOR doesn't reflect it). */
static int getUserCursorBaseSize(void)
{
    HKEY hKey;
    DWORD size = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Cursors", 0,
                      KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD data = 0, type = 0, dataSize = sizeof(data);
        if (RegQueryValueExA(hKey, "CursorBaseSize", NULL, &type,
                             (BYTE*)&data, &dataSize) == ERROR_SUCCESS
            && type == REG_DWORD && data >= 16 && data <= 1024) {
            size = data;
        }
        RegCloseKey(hKey);
    }
    return (int)size;
}

/* Build alpha-blended copy of the live cursor at on-screen size.
** Size = max(DPI-aware SM_CXCURSOR, GetIconInfo bitmap, registry accessibility). */
static HCURSOR makeFadeArrowCursor(int alphaPct)
{
    CURSORINFO ci = { sizeof(ci) };
    HCURSOR hSrc = (GetCursorInfo(&ci) && ci.hCursor) ? ci.hCursor
                                                      : LoadCursor(NULL, IDC_ARROW);
    HCURSOR  result = NULL;
    ICONINFO origIi = {0};
    int      gotOrig = GetIconInfo(hSrc, &origIi);

    UINT dpi = mouseHwnd ? GetDpiForWindow(mouseHwnd) : 96;
    int  w = GetSystemMetricsForDpi(SM_CXCURSOR, dpi);
    int  h = GetSystemMetricsForDpi(SM_CYCURSOR, dpi);
    if (w <= 0) w = 32;
    if (h <= 0) h = 32;

    int origW = w, origH = h;     /* source cursor's natural bitmap size,
                                  ** needed to scale the hotspot below */
    if (gotOrig) {
        BITMAP bm;
        int bw = 0, bh = 0;
        if (origIi.hbmColor && GetObject(origIi.hbmColor, sizeof(bm), &bm)) {
            bw = bm.bmWidth;
            bh = bm.bmHeight;
        } else if (origIi.hbmMask && GetObject(origIi.hbmMask, sizeof(bm), &bm)) {
            bw = bm.bmWidth;
            bh = bm.bmHeight / 2;
        }
        if (bw > 0) origW = bw;
        if (bh > 0) origH = bh;
        if (bw > w) w = bw;
        if (bh > h) h = bh;
    }

    /* CursorBaseSize is 100% DPI; rescale for current DPI. */
    int regSize = getUserCursorBaseSize();
    if (regSize > 0) {
        int dpiScaled = (int)((INT64)regSize * dpi / 96);
        if (dpiScaled > w) { w = dpiScaled; h = dpiScaled; }
    }

    BITMAPV5HEADER bmh = {0};
    bmh.bV5Size        = sizeof(BITMAPV5HEADER);
    bmh.bV5Width       = w;
    bmh.bV5Height      = -h;
    bmh.bV5Planes      = 1;
    bmh.bV5BitCount    = 32;
    bmh.bV5Compression = BI_BITFIELDS;
    bmh.bV5RedMask     = 0x00FF0000;
    bmh.bV5GreenMask   = 0x0000FF00;
    bmh.bV5BlueMask    = 0x000000FF;
    bmh.bV5AlphaMask   = 0xFF000000;

    HDC      screenDC = GetDC(NULL);
    void*    bits     = NULL;
    HBITMAP  hbmColor = CreateDIBSection(screenDC, (BITMAPINFO*)&bmh, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC      memDC    = CreateCompatibleDC(screenDC);

    if (hbmColor && memDC && bits && gotOrig) {
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hbmColor);
        memset(bits, 0, (size_t)w * h * 4);
        DrawIconEx(memDC, 0, 0, hSrc, w, h, 0, NULL, DI_NORMAL);
        SelectObject(memDC, oldBmp);

        UInt32* px = (UInt32*)bits;
        int n = w * h;
        int i;
        for (i = 0; i < n; i++) {
            UInt32 p = px[i];
            UInt32 a = ((p >> 24) & 0xFF) * (UInt32)alphaPct / 100;
            UInt32 r = ((p >> 16) & 0xFF) * (UInt32)alphaPct / 100;
            UInt32 g = ((p >>  8) & 0xFF) * (UInt32)alphaPct / 100;
            UInt32 b = ((p      ) & 0xFF) * (UInt32)alphaPct / 100;
            px[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        /* hbmMask must match hbmColor dims (CreateIconIndirect requirement). */
        int maskStrideBytes = ((w + 15) / 16) * 2;       /* WORD-aligned */
        size_t maskTotal = (size_t)maskStrideBytes * h;
        BYTE* maskBits = (BYTE*)calloc(1, maskTotal);
        HBITMAP hbmMask = maskBits ? CreateBitmap(w, h, 1, 1, maskBits) : NULL;

        ICONINFO ii = {0};
        ii.fIcon    = FALSE;
        /* Scale hotspot so click point matches the OS cursor. */
        ii.xHotspot = (DWORD)((INT64)origIi.xHotspot * w / (origW > 0 ? origW : 1));
        ii.yHotspot = (DWORD)((INT64)origIi.yHotspot * h / (origH > 0 ? origH : 1));
        ii.hbmMask  = hbmMask;
        ii.hbmColor = hbmColor;
        result = CreateIconIndirect(&ii);
        if (hbmMask) DeleteObject(hbmMask);
        if (maskBits) free(maskBits);
    }

    if (gotOrig) {
        if (origIi.hbmColor) DeleteObject(origIi.hbmColor);
        if (origIi.hbmMask)  DeleteObject(origIi.hbmMask);
    }
    if (memDC)    DeleteDC(memDC);
    if (hbmColor) DeleteObject(hbmColor);
    ReleaseDC(NULL, screenDC);
    return result;
}

static void mouseShowCursor(BOOL show)
{
    if (show) {
        if (cursorCnt == -1) {
            ShowCursor(TRUE);
            cursorCnt++;
        }
    }
    else {
        if (cursorCnt == 0) {
            ShowCursor(FALSE);
            cursorCnt--;
        }
    }
}

static void CALLBACK mouseEmuTimerCallback(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    POINT pt;

    /* Auto-hide after idle: AM_DISABLE (no MSX mouse) + armed + running. */
    if (mouseMode == AM_DISABLE && mouseActive && autoHideArmed && mouseIsRunning) {
        DWORD now = GetTickCount();
        POINT cpos;
        GetCursorPos(&cpos);

        /* On motion during fade or full-hide, abort and restore arrow. */
        if (fadeStep > 0
            && (cpos.x != lastCursorPos.x || cpos.y != lastCursorPos.y)) {
            int wasFullyHidden = (fadeStep >= FADE_STEPS);
            fadeStep = 0;
            if (wasFullyHidden) ShowCursor(TRUE);
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            lastMouseActivity = now;
        } else if (fadeStep == 0) {
            /* Visible: arm the fade if idle long enough. */
            if ((now - lastMouseActivity) > AUTO_HIDE_TIMEOUT_MS) {
                POINT clientPos = cpos;
                ScreenToClient(mouseHwnd, &clientPos);
                if (PtInRect(&mouseCapRect, clientPos)) {
                    /* Snapshot live cursor so fade frames match pointer-size. */
                    rebuildFadeCursors();
                    fadeStep = 1;
                    fadeNextStepTime = now + FADE_STEP_MS;
                    SendMessage(mouseHwnd, WM_SETCURSOR, (WPARAM)mouseHwnd,
                                MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
                }
            }
        } else if (fadeStep < FADE_STEPS && now >= fadeNextStepTime) {
            /* Advance fade; cap at FADE_STEPS so one ShowCursor(TRUE) reshows. */
            fadeStep++;
            fadeNextStepTime = now + FADE_STEP_MS;
            if (fadeStep >= FADE_STEPS) {
                ShowCursor(FALSE);
            } else {
                SendMessage(mouseHwnd, WM_SETCURSOR, (WPARAM)mouseHwnd,
                            MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            }
        }
        lastCursorPos = cpos;
    }

    /* AM_DISABLE: no MSX mouse; skip capture and release any prior lock. */
    if (!mouseIsRunning || !mouseActive || mouseMode == AM_DISABLE) {
        if (hasMouseLock) {
            hasMouseLock = 0;
            mouseShowCursor(TRUE);
            ReleaseCapture();
        }
        return;
    }

    GetCursorPos(&pt);
    ScreenToClient(mouseHwnd, &pt);
    
    if (!(mouseForceLock && mouseMode == AM_ENABLE_MOUSE) && !PtInRect(&mouseCapRect, pt)) {
        if (hasMouseLock) {
            hasMouseLock = 0;
            mouseShowCursor(TRUE);
            ReleaseCapture();
        }
        return;
    }

    if (mouseMode == AM_ENABLE_LASER) {
        if (mouseDisplayRect.right - mouseDisplayRect.left > 0) {
            mouseX = 0x10000*(pt.x - mouseDisplayRect.left) / (mouseDisplayRect.right - mouseDisplayRect.left);
            mouseY = 0x10000*(pt.y - mouseDisplayRect.top)  / (mouseDisplayRect.bottom - mouseDisplayRect.top);
        }
        else {
            mouseX = 0;
            mouseY = 0;
        }
    }

    if (!hasMouseLock) {
        mouseLockDX = 0;
        mouseLockDY = 0;
        mouseDX = 0;
        mouseDY = 0;
        hasMouseLock = 1;
        mouseShowCursor(mouseMode == AM_ENABLE_LASER);
        SetCapture(mouseHwnd);
        if (mouseMode == AM_ENABLE_MOUSE) {
            pt.x = 100;
            pt.y = 100;
            ClientToScreen(mouseHwnd, &pt);
            SetCursorPos(pt.x, pt.y);
        }
    }
    else if (mouseMode == AM_ENABLE_MOUSE) {
        int DX = 100 - pt.x;
        int DY = 100 - pt.y;

        mouseDX += DX;
        mouseDY += DY;
        mouseLockDX -= DX;
        mouseLockDY -= DY;
    
        pt.x = 100;
        pt.y = 100;

        if (!mouseForceLock) {
            pt.x = mouseLockDX < -600 ? mouseCapRect.left   - 7  : mouseLockDX >  600 ? mouseCapRect.right  + 7  : pt.x;
            pt.y = mouseLockDY < -600 ? mouseCapRect.top    - 7  : mouseLockDY >  600 ? mouseCapRect.bottom + 7  : pt.y;

            if (mouseLockDX < -600 || mouseLockDX > 600 || mouseLockDY < -600 || mouseLockDY > 600) {
                hasMouseLock = 0;
                mouseShowCursor(TRUE);
                ReleaseCapture();
            }
        }

        ClientToScreen(mouseHwnd, &pt);
        SetCursorPos(pt.x, pt.y);
    }
}

void archMouseSetForceLock(int lock) {
    if (mouseForceLock == lock) {
        return;
    }

    if (lock) {
        if (!hasMouseLock) {
            POINT pt = { 100, 100 };
            ClientToScreen(mouseHwnd, &pt);
            SetCursorPos(pt.x, pt.y);
        }
    }
    else {
        if (hasMouseLock) {
            POINT pt = { 30, -3 };
            ClientToScreen(mouseHwnd, &pt);
            SetCursorPos(pt.x, pt.y);
        }
    }

    mouseForceLock = lock;
}

int mouseEmuSetCursor()
{
    if (mouseMode == AM_ENABLE_LASER) {
        SetCursor(hCurs);
        return 1;
    }
    /* fadeStep 1..FADE_STEPS-1: faded arrow; 0/FADE_STEPS handled elsewhere. */
    if (fadeStep > 0 && fadeStep < FADE_STEPS && fadeCursors[fadeStep]) {
        SetCursor(fadeCursors[fadeStep]);
        return 1;
    }
    return 0;
}

void mouseEmuOnUserMouseActivity(void)
{
    lastMouseActivity = GetTickCount();
    autoHideArmed = 1;
    if (fadeStep != 0) {
        int wasFullyHidden = (fadeStep >= FADE_STEPS);
        fadeStep = 0;
        if (wasFullyHidden) {
            /* Came back from full hide: undo the ShowCursor(FALSE). */
            ShowCursor(TRUE);
        } else {
            /* Was in fade; restore the arrow. */
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
    }
}

/* Rebuild fade-frame cursors each time fade starts so they track the
** live cursor and any pointer-size change. */
static void rebuildFadeCursors(void)
{
    int s;
    for (s = 1; s < FADE_STEPS; s++) {
        if (fadeCursors[s]) {
            DestroyCursor(fadeCursors[s]);
            fadeCursors[s] = NULL;
        }
        int alphaPct = (FADE_STEPS - s) * 100 / FADE_STEPS;
        fadeCursors[s] = makeFadeArrowCursor(alphaPct);
    }
}

void mouseEmuInit(HWND hwnd, int timerId)
{
    hCurs = LoadCursor(NULL, IDC_CROSS); 

    mouseHwnd = hwnd;
    mouseTimerId = timerId;
    lastMouseActivity = GetTickCount();
    SetTimer(mouseHwnd, mouseTimerId, 10, mouseEmuTimerCallback);
}

void mouseEmuSetCaptureInfo(RECT* captureRect, RECT* displayRect) {
    if (displayRect != NULL) {
        memcpy(&mouseDisplayRect, displayRect, sizeof(RECT));
    }
    else {
        mouseActive = 0;
        memset(&mouseDisplayRect, 0, sizeof(RECT));
    }

    if (captureRect != NULL) {
        memcpy(&mouseCapRect, captureRect, sizeof(RECT));
    }
    else {
        mouseActive = 0;
        memset(&mouseCapRect, 0, sizeof(RECT));
    }
}

void mouseEmuSetRunState(int isRunning) {
    /* On running->stopped while fading, undo fade so cursor stays usable. */
    if (mouseIsRunning && !isRunning && fadeStep > 0) {
        int wasFullyHidden = (fadeStep >= FADE_STEPS);
        fadeStep = 0;
        if (wasFullyHidden) ShowCursor(TRUE);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        lastMouseActivity = GetTickCount();
    }
    mouseIsRunning = isRunning;
}

void mouseEmuActivate(int activate) {
    /* On focus regain, reset idle timer and unhide cursor. */
    if (activate && !mouseActive) {
        lastMouseActivity = GetTickCount();
        if (fadeStep >= FADE_STEPS) {
            ShowCursor(TRUE);
        }
        fadeStep = 0;
    }
    /* On focus loss, snap cursor back so it's findable in the new window. */
    else if (!activate && mouseActive && fadeStep > 0) {
        int wasFullyHidden = (fadeStep >= FADE_STEPS);
        fadeStep = 0;
        if (wasFullyHidden) ShowCursor(TRUE);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
    mouseActive = activate;
}

void archMouseEmuEnable(AmEnableMode mode) {
    mouseMode = mode;
    if (hasMouseLock) {
        mouseShowCursor(TRUE);
        ReleaseCapture();
        hasMouseLock = 0;
    }
    if (fadeStep >= FADE_STEPS) {
        ShowCursor(TRUE);
    }
    fadeStep = 0;
}

void archMouseGetState(int* dx, int* dy) {
    *dx = 0;
    *dy = 0;

    if (hasMouseLock) {
        if (mouseMode == AM_ENABLE_LASER) {
            *dx = mouseX;
            *dy = mouseY;
        }
        else {
            *dx = mouseDX;
            *dy = mouseDY;

            mouseDX = 0;
            mouseDY = 0;
        }
    }
}

int  archMouseGetButtonState(int checkAlways) {
    int buttons = 0;

    if (hasMouseLock || checkAlways) {
        if (GetAsyncKeyState(VK_LBUTTON) > 1UL) {
            buttons |= 1;
        }

        if (GetAsyncKeyState(VK_MBUTTON) > 1UL || GetAsyncKeyState(VK_RBUTTON) > 1UL) {
            buttons |= 2;
        }
    }

    return buttons;
}


