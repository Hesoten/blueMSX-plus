/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32Common.h,v $
**
** $Revision: 1.6 $
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
#ifndef WIN32_COMMON_H
#define WIN32_COMMON_H

#include <windows.h>

#include "StrcmpNoCase.h"
#include "IsFileExtension.h"

/* DLGPROC return slot for TRUE/FALSE replies; INT_PTR for x64 ABI safety.
** DLGPROCs returning HBRUSH / HCURSOR / etc. use plain INT_PTR. */
typedef INT_PTR BOOL_DLG_RET;

typedef enum {
    DLG_ID_PROPERTIES = 1,
    DLG_ID_JOYKEYS = 2,
    DLG_ID_ABOUT = 3,
    DLG_ID_OPEN = 4,
    DLG_ID_MACHINECONFIG = 6,
    DLG_ID_ZIPOPEN = 7,
    DLG_ID_TAPEPOS = 8,
    DLG_ID_OPENSTATE = 9,
} DialogIds;

void centerDialog(HWND hDlg, int noActivate);
void updateDialogPos(HWND hwnd, int dialogID, int noMove, int noSize);
void saveDialogPos(HWND hwnd, int dialogID);

HWND getMainHwnd();
HWND getEmuHwnd();


void enterDialogShow();
void exitDialogShow();

/* Show / update / hide a tracking tooltip showing a slider's value next to
** the cursor.  *phwndTip caches the tooltip HWND (one per parent window),
** created lazily on first show.  text is UTF-8; pass NULL to hide. */
void win32SliderTooltipUpdate(HWND* phwndTip, HWND parent, const char* text);

/* Theme query helpers for custom-paint controls (no WM_CTLCOLOR* path). */
BOOL win32CommonIsDarkMode(void);
COLORREF win32CommonDarkBg(void);
COLORREF win32CommonDarkFg(void);
HBRUSH   win32CommonDarkBgBrush(void);

/* Per-DLGPROC opt-in: apply dark titlebar / SetWindowTheme / WM_CTLCOLOR*
** subclass.  Call from WM_INITDIALOG.  Safe in light mode (no-op). */
void win32CommonApplyDark(HWND hDlg);

/* Center dialog over its owner (falls back to main HWND) instead of the
** monitor (DS_CENTER).  Call from WM_INITDIALOG. */
void win32CommonCenterOnOwner(HWND hDlg);

#endif
