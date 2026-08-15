/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32MouseEmu.h,v $
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
#ifndef WIN32_MOUSEEMU_H
#define WIN32_MOUSEEMU_H

#include <windows.h>

void mouseEmuInit(HWND hwnd, int timerId);
void mouseEmuSetCaptureInfo(RECT* captureRect, RECT* displayRect);
int mouseEmuSetCursor();
void mouseEmuActivate(int activate);
void mouseEmuSetRunState(int isRunning);

/* Scale physical mouse deltas to MSX-cursor space (msxPixels = visible MSX
** rows post-crop; physicalPixels = on-screen height they occupy). The final
** multiplier is combined with the user's Sensitivity slider (1..10). */
void mouseEmuSetScale(int msxPixels, int physicalPixels);

/* Recompute scale from cached dims after the sensitivity slider changes.
** Called from the keyboard-config dialog on WM_HSCROLL / slider notify. */
void mouseEmuRefreshSensitivity(void);

/* Called from emuWndProc's WM_INPUT handler with raw HID mouse deltas
** (dx/dy in mickeys). hDevice is currently unused. */
void mouseEmuHandleRawInput(int dx, int dy, HANDLE hDevice);

/* Called from emuWndProc's WM_LBUTTONDOWN handler. Sole user-initiated
** path to acquire the mouse capture; the timer never auto-locks. */
void mouseEmuOnClick(void);

/* Reset the auto-hide-cursor idle timer and unhide if currently hidden.
** Called from emuWndProc on any mouse activity (move / button) so the
** cursor reappears and the next idle countdown starts fresh. */
void mouseEmuOnUserMouseActivity(void);

#endif
