/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/sramLoader.c,v $
**
** $Revision: 1.7 $
**
** $Date: 2008-03-30 18:38:44 $
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
#include "sramLoader.h"
#include "Board.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "Utf8Conv.h"

/* After stdio.h: pkg_fopen overrides fopen for UTF-8 paths. */
#include "PacketFileSystem.h"



const char* sramCreateFilenameWithSuffix(const char* romFilename, char* suffix, char* ext)
{
    static char SRAMfileName[512];
    char fileName[512];
    char* dst = fileName + 512;
    const char* src;

    *--dst = '\0';
    if (ext == NULL) {
        *--dst = 'm';
        *--dst = 'a';
        *--dst = 'r';
        *--dst = 's';
        *--dst = '.';
    }
    else {
        char* p = ext + strlen(ext);
        do {
            *--dst = *--p;
        } while (p != ext);
    }

    dst -= strlen(suffix);
    memcpy(dst, suffix, strlen(suffix));
    
    src = romFilename + strlen(romFilename);

    while (*src != '.' && src > romFilename) {
        src--;
    }
    src--;

    while (*src != '/' && *src != '\\' && src >= romFilename) {
        *--dst = *src--;
    }

    sprintf(SRAMfileName, "%s" DIR_SEPARATOR "%s", boardGetBaseDirectory(), dst);

    return SRAMfileName;
}


const char* sramCreateFilename(const char* romFilename) {
    return sramCreateFilenameWithSuffix(romFilename, "", NULL);
}
void sramLoad(const char* filename, UInt8* sram, int length, void* header, int headerLength) {
    FILE* file;

    file = fopen(filename, "rb");
    if (file != NULL) {
        if (headerLength > 0) {
            char* readHeader[256];

            fread(readHeader, 1, headerLength, file);
            if (memcmp(readHeader, header, headerLength)) {
                fclose(file);
                return;
            }
        }
        fread(sram, 1, length, file);
        fclose(file);
    }
}

void sramSave(const char* filename, UInt8* sram, int length, void* header, int headerLength) {
    FILE* file;

    file = fopen(filename, "wb");
    if (file != NULL) {
        if (headerLength > 0) {
            fwrite(header, 1, headerLength, file);
        }
        fwrite(sram, 1, length, file);
        fclose(file);
    }
}

int sramFileStatus(const char* filename, int* size)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    wchar_t wName[1024];
    DWORD error;

    *size = 0;
    if (!PathToWide(filename, wName, 1024)) {
        return SRAM_FILE_ERROR;
    }
    if (!GetFileAttributesExW(wName, GetFileExInfoStandard, &data)) {
        error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ?
               SRAM_FILE_MISSING : SRAM_FILE_ERROR;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.nFileSizeHigh != 0 ||
        data.nFileSizeLow > 0x7fffffff) {
        return SRAM_FILE_ERROR;
    }
    *size = (int)data.nFileSizeLow;
    return SRAM_FILE_PRESENT;
#else
    FILE* file = fopen(filename, "rb");
    long length;

    *size = 0;
    if (file == NULL) {
        return errno == ENOENT ? SRAM_FILE_MISSING : SRAM_FILE_ERROR;
    }
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fclose(file);
    if (length < 0) {
        return SRAM_FILE_ERROR;
    }
    *size = (int)length;
    return SRAM_FILE_PRESENT;
#endif
}

UInt8* sramLoadFile(const char* filename, int* length)
{
    FILE* file = fopen(filename, "rb");
    UInt8* data;
    long size;

    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0 || size > 64 * 1024 * 1024) {
        fclose(file);
        return NULL;
    }
    data = (UInt8*)malloc(size > 0 ? size : 1);
    if (data != NULL && (long)fread(data, 1, size, file) != size) {
        free(data);
        data = NULL;
    }
    fclose(file);
    *length = (int)size;
    return data;
}

int sramSaveReplace(const char* filename, const UInt8* data, int length)
{
    char tmpName[600];
    FILE* file;
    int ok;

    if (strlen(filename) + 5 > sizeof(tmpName)) {
        return 0;
    }
    sprintf(tmpName, "%s.tmp", filename);

    file = fopen(tmpName, "wb");
    if (file == NULL) {
        return 0;
    }
    ok = (int)fwrite(data, 1, length, file) == length;
    ok = fclose(file) == 0 && ok;

#ifdef _WIN32
    {
        wchar_t wTmp[1024];
        wchar_t wName[1024];

        if (!PathToWide(tmpName, wTmp, 1024)) {
            return 0;
        }
        if (ok) {
            ok = PathToWide(filename, wName, 1024) &&
                 MoveFileExW(wTmp, wName, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
        if (!ok) {
            DeleteFileW(wTmp);
        }
    }
#else
    if (ok) {
        ok = rename(tmpName, filename) == 0;
    }
    if (!ok) {
        remove(tmpName);
    }
#endif
    return ok;
}

