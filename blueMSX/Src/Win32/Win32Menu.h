/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32Menu.h,v $
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
#ifndef WIN32_MENU_H
#define WIN32_MENU_H

#include <windows.h>
#include "Win32Properties.h"
#include "Win32ShortcutsConfig.h"

void menuCreate(HWND parent);
int menuShow(int show);
void menuUpdate(Properties* pProperties, 
                Shortcuts* shortcuts, 
                int isRunning, 
                int isStopped, 
                int logSound,
                int logVideo,
                int tempStateExits,
                int enableSpecial);

int menuCommand(Properties* pProperties, int command);

void menuSetInfo(COLORREF color, COLORREF focusColor, COLORREF textColor, int x, int y, int width, int height);

void addMenuItem(char* text, void (*action)(int, int), int append);
int  menuExitMenuLoop();

/* Rebuild the menu strip's font (using SystemParametersInfoForDpi) and
   recompute every cached item width/height for the new DPI.  Pass 0 to
   take the DPI from archSetDpiOverride / GetDpiForWindow(menuHwnd).
   Caller is expected to follow up with menuSetInfo so the strip's
   SetWindowPos picks up the new height. */
void menuRebuildForDpi(unsigned int dpi);

#endif
