/*****************************************************************************
**
** File path helpers for control requests.
** Copyright (C) 2026 Hesoten
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
#include "ControlPath.h"
#include <windows.h>
#include <ctype.h>
#include <string.h>
#include "Utf8Conv.h"

const char* ctrlPathLeaf(const char* path)
{
    const char* back  = strrchr(path, '\\');
    const char* slash = strrchr(path, '/');

    if (slash != NULL && (back == NULL || slash > back)) {
        back = slash;
    }
    return back != NULL ? back + 1 : path;
}

void ctrlPathLeafStem(const char* path, char* out, int outLen)
{
    const char* leaf = ctrlPathLeaf(path);
    const char* dot  = strrchr(leaf, '.');
    int length = dot != NULL ? (int)(dot - leaf) : (int)strlen(leaf);

    if (length >= outLen) {
        length = outLen - 1;
    }
    memcpy(out, leaf, (size_t)length);
    out[length] = 0;
}

int ctrlPathIsFull(const char* path)
{
    if (isalpha((unsigned char)path[0]) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/'))
    {
        return 1;
    }
    return (path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/');
}

static Int64 fileTimeValue(const FILETIME* time)
{
    return ((Int64)time->dwHighDateTime << 32) | time->dwLowDateTime;
}

Int64 ctrlFileWriteTime(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    wchar_t wide[1024];

    if (!PathToWide(path, wide, 1024) ||
        !GetFileAttributesExW(wide, GetFileExInfoStandard, &data))
    {
        return 0;
    }
    return fileTimeValue(&data.ftLastWriteTime);
}

Int64 ctrlFileTimeNow(void)
{
    FILETIME now;

    GetSystemTimeAsFileTime(&now);
    return fileTimeValue(&now);
}
