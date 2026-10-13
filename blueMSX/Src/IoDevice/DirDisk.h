/*****************************************************************************
**
** A host directory presented as a FAT disk.
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
#ifndef DIR_DISK_H
#define DIR_DISK_H

#include "MsxTypes.h"

typedef struct DirDisk DirDisk;

/* What was left out of the volume when it was built. */
typedef struct {
    int tooLarge;       /* files that did not fit */
    int onlineOnly;     /* files whose data is not on this computer */
    int subDirs;        /* subdirectories the chosen format cannot hold */
} DirSkipped;

/* format is a DiskFormatType. The files stay on the host; only the
** directory structure is kept in memory. Writes never reach the host. */
DirDisk* dirDiskCreate(const char* directory, int format);
void dirDiskDestroy(DirDisk* disk);

/* The path is UTF-8, which the C library does not take everywhere. */
int dirDiskIsDirPath(const char* path);

Int64 dirDiskGetSize(DirDisk* disk);
const char* dirDiskGetDirectory(DirDisk* disk);
int dirDiskGetFormat(DirDisk* disk);
void dirDiskGetSkipped(DirDisk* disk, DirSkipped* skipped);

/* offset and length are multiples of 512. Zero on failure. */
int dirDiskRead(void* disk, Int64 offset, UInt8* buffer, int length);
int dirDiskWrite(void* disk, Int64 offset, const UInt8* buffer, int length);

#endif
