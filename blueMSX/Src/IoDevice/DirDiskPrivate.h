/*****************************************************************************
**
** The inside of a directory volume, for the code that writes it back.
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
#ifndef DIR_DISK_PRIVATE_H
#define DIR_DISK_PRIVATE_H

#include "DirDisk.h"
#include <stdio.h>

#define DIR_SECTOR             512
#define DIR_OVERLAY_BLOCK      2048
#define DIR_ATTR_READONLY      0x01
#define DIR_ATTR_LABEL         0x08
#define DIR_ATTR_DIRECTORY     0x10
#define DIR_ATTR_ARCHIVE       0x20

/* One file or directory as it was on the host when the volume was built. */
typedef struct DirNode {
    char*   path;
    char*   hostName;
    UInt8   name[11];
    UInt8   attr;
    UInt16  time;
    UInt16  date;
    UInt32  size;
    Int64   hostTime;
    int     isDir;
    int     dropped;
    struct DirNode** child;
    int     childCount;
    int     childCapacity;
    UInt32  first;
    UInt32  clusters;
    UInt8*  data;
} DirNode;

typedef struct {
    int     fat16;
    UInt32  sizeK;
    UInt32  spc;
    UInt32  fatSectors;
    UInt32  clusters;
    UInt32  rootEntries;
    UInt32  total;
} DirLayout;

struct DirDisk {
    char*   directory;
    int     format;
    /* A floppy volume has no partition table: start, the sector of the boot sector, is 0 instead of 1. */
    int     floppy;
    UInt32  start;
    DirNode* root;
    DirLayout layout;
    UInt32  dataStart;
    UInt8   mbr[DIR_SECTOR];
    UInt8   boot[DIR_SECTOR];
    UInt8*  fat;
    DirNode** extent;
    int     extentCount;
    int     extentCapacity;
    DirSkipped skipped;
    int     disarmed;

    FILE*   openFile;
    DirNode* openNode;

    /* overlaySlot holds, for every sector of the volume, its place in the
    ** blocks plus one; zero while the MSX has not written the sector. */
    UInt32* overlaySlot;
    UInt8** overlayBlock;
    UInt32  overlaySectors;
    UInt32  overlayCount;
};

/* The sector as the MSX sees it now. Zero when a host file cannot be read. */
int dirDiskReadSector(DirDisk* disk, UInt32 sector, UInt8* buffer);

/* Closes the host file kept open for reading, so that it can be moved. */
void dirDiskReleaseHostFile(DirDisk* disk);

#ifdef _WIN32
#include <wchar.h>
/* The path in the form the host takes for names of any length; empty when it cannot be made. */
void dirDiskWidePath(const char* path, wchar_t* wide, int capacity);
#endif

#endif
