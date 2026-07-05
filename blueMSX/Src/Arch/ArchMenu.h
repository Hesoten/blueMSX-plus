/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Arch/ArchMenu.h,v $
**
** $Revision: 1.9 $
**
** $Date: 2008-03-30 18:38:39 $
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
#ifndef ARCH_MENU_H
#define ARCH_MENU_H

void archUpdateMenu(int show);

void archShowMenuSpecialCart1(int x, int y);
void archShowMenuSpecialCart2(int x, int y);
void archShowMenuReset(int x, int y);
void archShowMenuHelp(int x, int y);
void archShowMenuRun(int x, int y);
void archShowMenuFile(int x, int y);
void archShowMenuCart1(int x, int y);
void archShowMenuCart2(int x, int y);
void archShowMenuHarddisk(int x, int y);
void archShowMenuDiskA(int x, int y);
void archShowMenuDiskB(int x, int y);
void archShowMenuCassette(int x, int y);
void archShowMenuPrinter(int x, int y);
void archShowMenuZoom(int x, int y);
void archShowMenuOptions(int x, int y);
void archShowMenuTools(int x, int y);
void archShowMenuJoyPort1(int x, int y);
void archShowMenuJoyPort2(int x, int y);

/* Returns the actual menu strip height in pixels at the current system DPI.
   Used by themes to align content right below the menu strip rather than at
   a hardcoded design value (typically 22 px, but 28-32 at higher DPI). */
int archMenuStripHeight(void);

/* Override the DPI archMenuStripHeight() and the menu strip's own
   SetWindowPos consults during a transition (WM_DPICHANGED): for a brief
   window between the OS notifying the parent and the child window's own
   DPI tracking catching up, GetDpiForWindow(menuHwnd) can still return
   the previous monitor's DPI.  Pass LOWORD(wParam) here to force the
   authoritative new value; pass 0 to clear and resume live queries. */
void archSetDpiOverride(unsigned int dpi);

#endif
