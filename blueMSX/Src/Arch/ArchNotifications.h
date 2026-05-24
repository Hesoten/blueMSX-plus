/*****************************************************************************
** $Source: /cvsroot/bluemsx/blueMSX/Src/Arch/ArchNotifications.h,v $
**
** $Revision: 1.25 $
**
** $Date: 2008/03/30 18:38:39 $
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
#ifndef ARCH_NOTIFICATIONS_H
#define ARCH_NOTIFICATIONS_H

typedef enum { SC_NORMAL, SC_SMALL, SC_LARGE } ScreenCaptureType;
void* archScreenCapture(ScreenCaptureType type, int* bitmapSize, int onlyBmp);
#ifdef WII
int archScreenCaptureToFile(ScreenCaptureType type, const char *fname);
#endif

void archUpdateEmuDisplayConfig();
int  archUpdateEmuDisplay(int synchronous);

#ifdef WII
void archDiskQuickChangeNotify(int driveId, char* fileName, const char* fileInZipFile);
#else
void archDiskQuickChangeNotify();
#endif
void archEmulationStartNotification();
void archEmulationStopNotification();
void archEmulationStartFailure();

void archQuit();

struct Theme;
void archThemeSetNext();
void archThemeUpdate(struct Theme* theme);

void archVideoOutputChange();
void archUpdateWindow();
void archMinimizeMainWindow();

int archGetFramesPerSecond();

void* archWindowCreate(struct Theme* theme, int childWindow);
/* Sync an aux theme window's owner + Z-order with the current display
** mode; without it the window hides behind the topmost fullscreen main. */
void archWindowApplyOwnership(void* hwnd);

/* archWindowApplyOwnership for every open aux theme window (found via
** window class -- tool themes aren't reachable from st.themeList). */
void archWindowApplyOwnershipAll(void);
void archWindowStartMove();
void archWindowMove();
void archWindowEndMove();

#endif
