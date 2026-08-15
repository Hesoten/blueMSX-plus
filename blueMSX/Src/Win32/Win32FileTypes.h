/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32FileTypes.h,v $
**
** $Revision: 1.3 $
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
#ifndef WIN32_FILE_TYPES_H
#define WIN32_FILE_TYPES_H

#include <windows.h>


BOOL registerFileType(char* extension, char* appName, char* description, int iconIndex);
BOOL unregisterFileType(char* extension, char* appName, char* description, int iconIndex);

/* The extensions the exe registers for, NULL terminated. Shared so a caller
** asking whether the shell could have sent a name reads the same list. */
const char* const* fileTypesRegisteredExtensions(void);

/* Register the EXE under HKCU\Software\Classes\Applications as a
** first-class Open With target. */
void registerApplicationOpenWith(void);
void unregisterApplicationOpenWith(void);

/* Broadcast SHCNE_ASSOCCHANGED so Explorer / the "Open with" menu
** refresh after a batch of registerFileType / unregisterFileType calls. */
void fileTypesNotifyShell(void);

#endif
