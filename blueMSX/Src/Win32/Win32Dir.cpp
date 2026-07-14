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
extern "C" {
#include "DiskFormat.h"
}

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

extern "C" char* openDirWithFormat(HWND hwnd, char* pTitle, char* defDir,
                                   int* fmtIndexInOut)
{
    static char pFileName[MAX_PATH * 4];
    static const ShellComboItem kFmtItems[] = {
        { "MSX-DOS 1", (int)DiskFormatMsxDos1 },
        { "MSX-DOS 2", (int)DiskFormatMsxDos2 },
        { "Nextor",    (int)DiskFormatNextor  }
    };
    int comboIndex = 1;

    if (fmtIndexInOut) {
        switch (*fmtIndexInOut) {
        case (int)DiskFormatMsxDos1: comboIndex = 0; break;
        case (int)DiskFormatNextor:  comboIndex = 2; break;
        default:                     comboIndex = 1; break;
        }
    }

    if (!ShellPickFolderWithFormatDialog(hwnd, pTitle, defDir,
                                         kFmtItems,
                                         (int)(sizeof(kFmtItems)/sizeof(kFmtItems[0])),
                                         &comboIndex,
                                         pFileName, sizeof(pFileName))) {
        return NULL;
    }

    if (fmtIndexInOut) {
        if (comboIndex >= 0 &&
            comboIndex < (int)(sizeof(kFmtItems)/sizeof(kFmtItems[0]))) {
            *fmtIndexInOut = kFmtItems[comboIndex].bytes;
        } else {
            *fmtIndexInOut = (int)DiskFormatMsxDos2;
        }
    }
    return pFileName;
}
