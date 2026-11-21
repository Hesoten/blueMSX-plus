/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/RomLoader.c,v $
**
** $Revision: 1.6 $
**
** $Date: 2008-03-30 18:38:42 $
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
#include "RomLoader.h"
#include "ziphelper.h"
#include "Board.h"
#include "Machine.h"
#include "ArchFile.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"


UInt8* romLoad(const char *fileName, const char *fileInZipFile, int* size)
{
    UInt8* buf = NULL;
    FILE *file;

    if (fileName == NULL || strlen(fileName) == 0) {
        return NULL;
    }

    if (fileInZipFile != NULL && strlen(fileInZipFile) == 0) {
        fileInZipFile = NULL;
    }

    if (fileInZipFile != NULL) {
        buf = zipLoadFile(fileName, fileInZipFile, size);
        return buf;
    }

    file = fopen(fileName, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    if (*size == 0) {
        fclose(file);
        return malloc(1);
    }

    fseek(file, 0, SEEK_SET);

    buf = malloc(*size);
    
    *size = (int)fread(buf, 1, *size, file);
    fclose(file);

    return buf;
}

/* Fills path with the place fileName is read from, or with the first place
** looked at when neither has it. */
static int sharedRomFind(const char* fileName, char* path)
{
    char oldPath[512];
    FILE* file;

    snprintf(oldPath, sizeof(oldPath), "Machines/Shared Roms/%s", fileName);
    strcpy(path, oldPath);
    machineExpandPath(path, 512);
    file = fopen(path, "rb");
    if (file == NULL) {
        file = fopen(oldPath, "rb");
        if (file != NULL) {
            snprintf(path, 512, "%s/%s", archGetCurrentDirectory(), oldPath);
        }
    }
    if (file != NULL) {
        fclose(file);
    }
    return file != NULL;
}

UInt8* romLoadShared(RomType device, const char* fileName, int* size)
{
    char path[512];
    UInt8* buf = NULL;

    if (sharedRomFind(fileName, path)) {
        buf = romLoad(path, NULL, size);
    }
    if (buf == NULL && device != ROM_UNKNOWN) {
        boardReportDeviceFile(device, path, DEVICE_FILE_MISSING);
    }
    return buf;
}

void romRejectShared(RomType device, const char* fileName)
{
    char path[512];

    sharedRomFind(fileName, path);
    boardReportDeviceFile(device, path, DEVICE_FILE_UNUSABLE);
}

