/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32Glob.c,v $
**
** $Revision: 1.5 $
**
** $Date: 2008-03-31 19:42:24 $
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
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "ArchGlob.h"
#include "Utf8Conv.h"

// This glob only support very basic globbing, dirs in the patterns are only 
// supported without any wildcards

/* UTF-8 input via PathToWide; FindFirstFileW so non-ACP filenames match. */
ArchGlob* archGlob(const char* pattern, int flags)
{
    wchar_t oldPath[MAX_PATH];
    wchar_t patternW[MAX_PATH];
    ArchGlob* glob;
    WIN32_FIND_DATAW wfd;
    HANDLE handle;
    wchar_t* fileOnlyW;

    GetCurrentDirectoryW(MAX_PATH, oldPath);
    PathToWide(pattern, patternW, MAX_PATH);

    /* Split off the dir prefix (up to last /\\) and chdir into it; the
    ** remainder is passed to FindFirstFileW as the filename pattern. */
    {
        wchar_t* fwd = wcsrchr(patternW, L'/');
        wchar_t* bwd = wcsrchr(patternW, L'\\');
        wchar_t* sep = (fwd > bwd) ? fwd : bwd;
        if (sep == NULL) {
            fileOnlyW = patternW;
        } else {
            wchar_t relPathW[MAX_PATH];
            size_t dirLen = (size_t)(sep - patternW);
            if (dirLen >= _countof(relPathW)) dirLen = _countof(relPathW) - 1;
            wcsncpy(relPathW, patternW, dirLen);
            relPathW[dirLen] = 0;
            fileOnlyW = sep + 1;
            SetCurrentDirectoryW(relPathW);
        }
    }

    handle = FindFirstFileW(fileOnlyW, &wfd);
    if (handle == INVALID_HANDLE_VALUE) {
        SetCurrentDirectoryW(oldPath);
        return NULL;
    }

    glob = (ArchGlob*)calloc(1, sizeof(ArchGlob));

    do {
        DWORD fa;
        if (0 == wcscmp(wfd.cFileName, L".") || 0 == wcscmp(wfd.cFileName, L"..")) {
            continue;
        }
        fa = GetFileAttributesW(wfd.cFileName);
        if (fa == INVALID_FILE_ATTRIBUTES) continue;
        if (((flags & ARCH_GLOB_DIRS)  && (fa &  FILE_ATTRIBUTE_DIRECTORY) != 0) ||
            ((flags & ARCH_GLOB_FILES) && (fa &  FILE_ATTRIBUTE_DIRECTORY) == 0))
        {
            wchar_t pathW[MAX_PATH * 2];
            char*   path;
            int     pathBytes;

            GetCurrentDirectoryW(MAX_PATH, pathW);
            wcscat(pathW, L"\\");
            wcscat(pathW, wfd.cFileName);

            /* Store paths as UTF-8 -- the rest of the codebase expects UTF-8
            ** and PathToWide will convert back at the next OS call. */
            pathBytes = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, NULL, 0, NULL, NULL);
            if (pathBytes <= 0) continue;
            path = (char*)malloc((size_t)pathBytes);
            WideCharToMultiByte(CP_UTF8, 0, pathW, -1, path, pathBytes, NULL, NULL);

            glob->count++;
            glob->pathVector = (char**)realloc(glob->pathVector, sizeof(char*) * glob->count);
            glob->pathVector[glob->count - 1] = path;
        }
    } while (FindNextFileW(handle, &wfd));

    FindClose(handle);

    SetCurrentDirectoryW(oldPath);

    return glob;
}

void archGlobFree(ArchGlob* globHandle)
{
    int i;

    if (globHandle == NULL) {
        return;
    }
    
    for (i = 0; i < globHandle->count; i++) {
        free(globHandle->pathVector[i]);
    }
    free(globHandle->pathVector);
    free(globHandle);
}

