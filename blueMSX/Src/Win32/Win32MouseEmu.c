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
#include "Win32Toast.h"
#include "Win32Common.h"
#include "Language.h"
#include "Properties.h"
#include "Actions.h"
#include <stdlib.h>
#include <math.h>

/* Suppress MSX-side left button reads briefly after lock acquisition so the
** click that triggered the lock isn't delivered as a mouse press to MSX. */
#define LOCK_LBUTTON_SUPPRESS_MS  300


static HWND mouseHwnd;
static HCURSOR hCurs;
static int mouseIsRunning = 0;
static int mouseTimerId;
static RECT mouseCapRect;
static RECT mouseDisplayRect;
static int mouseActive;
static AmEnableMode mouseMode;
static int mouseDX;
static int mouseDY;
static int mouseX;
static int mouseY;
static int hasMouseLock;
static int mouseForceLock;
static int cursorCnt = 0;
static int mouseScaleNum = 1;   /* msx_delta = physical_delta * num / den */
static int mouseScaleDen = 1;
static int mouseOnewayLimit = 0;
static int cachedMsxPixels     = 0;   /* remembered so scale recomputes on slider change */
static int cachedPhysicalPixels = 0;
static int mouseResidualX = 0;  /* fractional carry so sub-pixel movement isn't lost */
static int mouseResidualY = 0;
static int mousePendingDX = 0;  /* raw-input smoothing buffer: consume 2/3 per */
static int mousePendingDY = 0;  /* poll so bursts spread out (~25ms latency)   */
static int oneWaySegDX = 0;     /* same-direction segment sum for one-way escape */
static int oneWaySegDY = 0;
static POINT lockFrozenAt;       /* screen coords cursor is frozen at while locked */
static int   lockHidCursor = 0;  /* count of ShowCursor(FALSE) calls we owe TRUE for */
static DWORD lockLbuttonSuppressUntil = 0;
static int   lockFadeStep = 0;   /* 1..FADE_STEPS-1: capture-start fade in progress */
static DWORD lockFadeNextTime;

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
static void mouseAcquireLock(void);
static void mouseRecomputeScale(void);

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

/* Freeze the physical cursor at a single pixel via a 1x1 ClipCursor so it
** cannot wander while locked; raw-input keeps supplying deltas regardless.
** Nothing else in the emulator relies on cursor position while locked. */
static void mouseFreezeCursor(POINT screenPt)
{
    RECT r;
    r.left   = screenPt.x;
    r.top    = screenPt.y;
    r.right  = screenPt.x + 1;
    r.bottom = screenPt.y + 1;
    ClipCursor(&r);
}

/* Recenter cursor to the middle of the emu area, unfreeze, show cursor. */
static void mouseReleaseCursor(void)
{
    POINT c;
    ClipCursor(NULL);
    c.x = (mouseCapRect.left + mouseCapRect.right)  / 2;
    c.y = (mouseCapRect.top  + mouseCapRect.bottom) / 2;
    ClientToScreen(mouseHwnd, &c);
    SetCursorPos(c.x, c.y);
    lockFadeStep = 0;  /* cancel any in-progress capture fade */
    /* Force a fresh arrow so a mid-fade cursor image doesn't linger
    ** after ShowCursor(TRUE). */
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    while (lockHidCursor > 0) { ShowCursor(TRUE); lockHidCursor--; }
}

/* User-intent disconnect toast; separate from mouseReleaseCursor so that
** timer-driven releases (focus loss, mode switch) don't spam a toast the
** user didn't ask for. */
static void mouseNotifyDisconnected(void)
{
    toastShowMessage(getMainHwnd(), getEmuHwnd(), langInfoToastMouseDisconnected());
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

    /* Advance capture-start fade (2x speed) independent of mode branches. */
    if (lockFadeStep > 0 && GetTickCount() >= lockFadeNextTime) {
        lockFadeStep++;
        lockFadeNextTime = GetTickCount() + FADE_STEP_MS / 2;
        if (lockFadeStep >= FADE_STEPS) {
            if (!lockHidCursor) { ShowCursor(FALSE); lockHidCursor++; }
            lockFadeStep = 0;
        } else {
            SendMessage(mouseHwnd, WM_SETCURSOR, (WPARAM)mouseHwnd,
                        MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        }
    }

    /* Release only on definitive signals (mode off / focus lost). Don't
    ** react to mouseIsRunning transitions -- the state flag updates on a
    ** slower timer and would otherwise drop a fresh click-lock. */
    if (mouseMode == AM_DISABLE || !mouseActive) {
        if (hasMouseLock) {
            hasMouseLock = 0;
            ReleaseCapture();
            mouseReleaseCursor();
        }
        return;
    }
    if (!mouseIsRunning) return;

    GetCursorPos(&pt);
    ScreenToClient(mouseHwnd, &pt);

    if (mouseMode == AM_ENABLE_LASER) {
        if (!PtInRect(&mouseCapRect, pt)) {
            if (hasMouseLock) {
                hasMouseLock = 0;
                mouseShowCursor(TRUE);
                ReleaseCapture();
            }
            return;
        }
        if (mouseDisplayRect.right - mouseDisplayRect.left > 0) {
            mouseX = 0x10000*(pt.x - mouseDisplayRect.left) / (mouseDisplayRect.right - mouseDisplayRect.left);
            mouseY = 0x10000*(pt.y - mouseDisplayRect.top)  / (mouseDisplayRect.bottom - mouseDisplayRect.top);
        } else {
            mouseX = 0; mouseY = 0;
        }
        if (!hasMouseLock) {
            hasMouseLock = 1;
            mouseShowCursor(TRUE);
            SetCapture(mouseHwnd);
        }
        return;
    }

    /* AM_ENABLE_MOUSE: user-click and game-driven force-lock only; the
    ** timer never auto-acquires so a released cursor won't be recaptured.
    ** Shake detection runs inline in mouseEmuHandleRawInput. */
    if (!hasMouseLock) {
        if (mouseForceLock) mouseAcquireLock();
    }
}

static void mouseAcquireLock(void)
{
    /* Gate on isRunning so pre-emu-start clicks don't lock. Skip mouseActive
    ** (click implies activity); timer no longer force-releases on !running so
    ** a click that races the state flag will still take on the next tick. */
    if (hasMouseLock || mouseMode != AM_ENABLE_MOUSE || !mouseIsRunning) return;
    mouseDX = 0;
    mouseDY = 0;
    mouseResidualX = 0;
    mouseResidualY = 0;
    mousePendingDX = 0;
    mousePendingDY = 0;
    oneWaySegDX  = 0;
    oneWaySegDY  = 0;
    hasMouseLock = 1;
    lockLbuttonSuppressUntil = GetTickCount() + LOCK_LBUTTON_SUPPRESS_MS;
    SetCapture(mouseHwnd);
    GetCursorPos(&lockFrozenAt);
    mouseFreezeCursor(lockFrozenAt);
    /* Start capture-start fade at 2x speed for a visible "gotcha" cue.
    ** Cursor is frozen in place; hide flips to FALSE at fade end. */
    rebuildFadeCursors();
    lockFadeStep     = 1;
    lockFadeNextTime = GetTickCount() + FADE_STEP_MS / 2;
    SendMessage(mouseHwnd, WM_SETCURSOR, (WPARAM)mouseHwnd,
                MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    toastShowMessage(getMainHwnd(), getEmuHwnd(), langInfoToastMouseConnected());
    /* Sync shared state.mouseLock so the hotkey toggle inverts what the
    ** user actually sees, even when acquisition came from a click. */
    actionSetMouseCapture(1);
}

void mouseEmuOnClick(void)
{
    mouseAcquireLock();
}

void mouseEmuHandleRawInput(int dx, int dy, HANDLE hDevice)
{
    int dotSeg, adx, ady;
    (void)hDevice;

    if (!hasMouseLock || mouseMode != AM_ENABLE_MOUSE) return;
    /* Legacy sign: positive physical motion -> negative MSX counts. */
    mouseDX -= dx;
    mouseDY -= dy;
    if (dx == 0 && dy == 0) return;

    /* One-way escape: accumulate same-direction motion; a reversal resets
    ** the segment. If the segment magnitude on either axis exceeds
    ** mouseOnewayLimit, release the lock -- user pushed past the edge. */
    dotSeg = dx * oneWaySegDX + dy * oneWaySegDY;
    if (dotSeg < 0) {
        oneWaySegDX = dx;
        oneWaySegDY = dy;
        return;
    }
    oneWaySegDX += dx;
    oneWaySegDY += dy;
    adx = oneWaySegDX < 0 ? -oneWaySegDX : oneWaySegDX;
    ady = oneWaySegDY < 0 ? -oneWaySegDY : oneWaySegDY;
    if (mouseOnewayLimit > 0
        && (adx > mouseOnewayLimit || ady > mouseOnewayLimit)) {
        oneWaySegDX    = 0;
        oneWaySegDY    = 0;
        hasMouseLock   = 0;
        mouseForceLock = 0;  /* prevent timer from immediately re-acquiring */
        ReleaseCapture();
        mouseReleaseCursor();
        mouseNotifyDisconnected();
        actionSetMouseCapture(0);
    }
}

void archMouseSetForceLock(int lock) {
    /* Don't short-circuit on mouseForceLock alone: a click-acquired lock
    ** has hasMouseLock=1 with mouseForceLock=0, and a hotkey-release
    ** request (lock=0) must still tear down the click-lock. */
    if (lock) {
        mouseForceLock = 1;
        /* Timer picks up on the next tick and calls mouseAcquireLock. */
    } else {
        if (hasMouseLock) {
            hasMouseLock = 0;
            ReleaseCapture();
            mouseReleaseCursor();
            mouseNotifyDisconnected();
        }
        mouseForceLock = 0;
    }
}

int mouseEmuSetCursor()
{
    if (mouseMode == AM_ENABLE_LASER) {
        SetCursor(hCurs);
        return 1;
    }
    /* Capture-start fade (from click) takes priority so its 2x-speed frames
    ** show over any lingering idle-fade state. */
    if (lockFadeStep > 0 && lockFadeStep < FADE_STEPS && fadeCursors[lockFadeStep]) {
        SetCursor(fadeCursors[lockFadeStep]);
        return 1;
    }
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
    RAWINPUTDEVICE rid;
    hCurs = LoadCursor(NULL, IDC_CROSS);

    mouseHwnd = hwnd;
    mouseTimerId = timerId;
    lastMouseActivity = GetTickCount();
    SetTimer(mouseHwnd, mouseTimerId, 10, mouseEmuTimerCallback);

    /* Generic Desktop / Mouse. RIDEV_INPUTSINK routes WM_INPUT to us even
    ** when emuHwnd (child) doesn't have foreground focus, which it usually
    ** doesn't; without this raw input silently never arrives. */
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0x02;
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
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

    /* Fullscreen <-> windowed keeps hasMouseLock=1, but the old 1x1
    ** ClipCursor and lockFrozenAt point at coords that may now be far
    ** from the new emu rect (and Windows can restore cursor visibility
    ** during the window recreate). Re-freeze at the new emu centre and
    ** re-hide if we had already fade-hidden the cursor. */
    if (hasMouseLock && mouseHwnd) {
        POINT c;
        c.x = (mouseCapRect.left + mouseCapRect.right) / 2;
        c.y = (mouseCapRect.top  + mouseCapRect.bottom) / 2;
        ClientToScreen(mouseHwnd, &c);
        lockFrozenAt = c;
        SetCursorPos(c.x, c.y);
        mouseFreezeCursor(c);
        if (lockHidCursor) {
            CURSORINFO ci = { sizeof(ci) };
            if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
                /* Track this extra hide so release drains it too;
                ** otherwise repeated transitions leak negative counter
                ** and the cursor stays invisible after release. */
                ShowCursor(FALSE);
                lockHidCursor++;
            }
        }
    }
}

/* Recompute scale / one-way limit from cached pixel dims and the user's
** in-dialog sensitivity slider (1..10, default 5 == 1.0x baseline). */
static void mouseRecomputeScale(void)
{
    int sens = propGetGlobalProperties()->emulation.mouseSensitivity;
    if (sens < 1)  sens = 1;
    if (sens > 10) sens = 10;
    if (cachedMsxPixels <= 0 || cachedPhysicalPixels <= 0) {
        mouseScaleNum = 1;
        mouseScaleDen = 1;
        mouseOnewayLimit = 0;
        return;
    }
    /* MSX-count per mickey stays constant across zoom. Coefficients tuned
    ** so games feel natural at default zoom; linear in sens 1..10. */
    mouseScaleNum = 14 * (35 * sens + 37);
    mouseScaleDen = 5625;
    /* Push-to-edge escape: K(sens) * mickeys-to-cross-MSX-edge.
    ** Edge crossing = msxPixels * scaleDen / scaleNum. K goes ~1.56 at
    ** sens=1 to ~2.88 at sens=10 linearly (formula: (106 + 11*sens)/75).
    ** Keeps "N screens past edge" consistent across zoom. */
    mouseOnewayLimit = (int)((double)cachedMsxPixels * (106 + 11 * sens)
                             * mouseScaleDen / (75.0 * mouseScaleNum));
}

void mouseEmuSetScale(int msxPixels, int physicalPixels) {
    cachedMsxPixels     = msxPixels;
    cachedPhysicalPixels = physicalPixels;
    mouseRecomputeScale();
    mouseResidualX = 0;
    mouseResidualY = 0;
    mousePendingDX = 0;
    mousePendingDY = 0;
}

void mouseEmuRefreshSensitivity(void) {
    mouseRecomputeScale();
    mouseResidualX = 0;
    mouseResidualY = 0;
    mousePendingDX = 0;
    mousePendingDY = 0;
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
        hasMouseLock = 0;
        ReleaseCapture();
        mouseReleaseCursor();
    }
    mouseResidualX = 0;
    mouseResidualY = 0;
    mousePendingDX = 0;
    mousePendingDY = 0;
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
            /* Scale to MSX-cursor space, carrying the fractional remainder
            ** so slow movement still registers. Integer div toward zero is
            ** symmetric for +/- so residuals accumulate correctly. */
            mouseResidualX += mouseDX * mouseScaleNum;
            mouseResidualY += mouseDY * mouseScaleNum;
            mouseDX = 0;
            mouseDY = 0;

            *dx = mouseResidualX / mouseScaleDen;
            *dy = mouseResidualY / mouseScaleDen;
            mouseResidualX -= *dx * mouseScaleDen;
            mouseResidualY -= *dy * mouseScaleDen;
        }
    }
}

int  archMouseGetButtonState(int checkAlways) {
    int buttons = 0;

    if (hasMouseLock || checkAlways) {
        /* Mask LBUTTON right after lock acquire so the click that triggered
        ** the capture isn't delivered to MSX as a spurious button press. */
        if (GetTickCount() >= lockLbuttonSuppressUntil
            && GetAsyncKeyState(VK_LBUTTON) > 1UL) {
            buttons |= 1;
        }

        if (GetAsyncKeyState(VK_MBUTTON) > 1UL || GetAsyncKeyState(VK_RBUTTON) > 1UL) {
            buttons |= 2;
        }
    }

    return buttons;
}


