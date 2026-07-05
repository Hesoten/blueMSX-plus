/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32ThemeClassic.h,v $
**
** $Revision: 1.5 $
**
** $Date: 2008-05-06 12:52:10 $
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
#ifndef THEME_CLASSIC_H
#define THEME_CLASSIC_H

#include <windows.h>
#include "Theme.h"

/* Two built-in variants: light (original artwork) and dark (the
   classic_dark/ resource set).  Both appear as separate entries in the
   theme combo so the user picks light or dark explicitly. */
ThemeCollection* themeClassicCreate();
ThemeCollection* themeClassicCreateDark();
/* Re-run themeCreateSmall/Zoom/Fullscreen against the current DPI so the
   built-in zoom variants pick up the new SM_CYMENU value after a monitor
   move (WM_DPICHANGED).  Inspects tc->name to keep the light / dark
   variant intact across the rebuild. */
void themeClassicRebuild(ThemeCollection* tc);
void themeClassicTitlebarUpdate(HWND);

#endif //WIN32_THEME_CLASSIC_H
