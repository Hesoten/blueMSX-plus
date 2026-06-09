/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32Dir.cpp,v $
**
** $Revision: 1.9 $
**
** $Date: 2008-06-06 23:55:57 $
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
#include <windows.h>
#include "Win32FileDialog.h"

/* Folder picker: forwards to ShellPickFolderDialog (IFileOpenDialog
** with FOS_PICKFOLDERS). */
extern "C" char* openDir(HWND hwnd, char* pTitle, char* defDir)
{
    static char pFileName[MAX_PATH * 4];
    if (!ShellPickFolderDialog(hwnd, pTitle, defDir, pFileName, sizeof(pFileName))) {
        return NULL;
    }
    return pFileName;
}
