/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/IoDevice/Disk.h,v $
**
** $Revision: 1.20 $
**
** $Date: 2009-07-18 15:08:04 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik, Tomas Karlsson
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
#ifndef DISK_H
#define DISK_H

#include "MsxTypes.h"

#define MAX_FDC_COUNT            2

#define MAX_HD_COUNT             4
#define FIRST_INTERNAL_HD_INDEX  2

#define MAX_DRIVES_PER_HD        8

#define DISK_CDROM               "CD-Rom"

#define MAXDRIVES (MAX_FDC_COUNT + MAX_DRIVES_PER_HD * MAX_HD_COUNT)

typedef enum {
    DSKE_OK,
    DSKE_NO_DATA,
    DSKE_CRC_ERROR
} DSKE;

/* A medium the host serves in place of an image file. Offsets and lengths
** are in bytes; a medium without write is read only. */
typedef struct {
    int (*read)(void* ref, Int64 offset, UInt8* buffer, int length);
    int (*write)(void* ref, Int64 offset, const UInt8* buffer, int length);
} DiskHostMedium;

/* Starts the name a drive carries while a host medium is inserted. */
#define DISK_HOSTMEDIUM_PREFIX   "physical://"

UInt8 diskChange(int driveId, const char* fileName, const char* fileInZipFile);

/* Offers a medium of size bytes to a drive, or withdraws it with NULL. The
** drive uses it once diskChange gets a host medium name. */
void diskSetHostMedium(int driveId, const DiskHostMedium* medium, void* ref, Int64 size);
void diskWithdrawHostMedia(void);

/* A directory inserted in a hard disk drive shows as a partitioned FAT volume
** whose files stay on the host. format is a DiskFormatType, used when the
** drive has to build the volume itself. */
void diskSetDirFormat(int driveId, int format);
/* Reads the directory afresh so that inserting it shows it as it is now; zero if it cannot be read. */
int  diskPrepareDir(int driveId, const char* directory, int format);
/* What the MSX writes to the directory from now on is not written back until it starts afresh. */
void diskDirDisarm(int driveId);
/* Tells the directories that the machine is starting, from a saved state or not;
** stopped says that nothing is reading the drives meanwhile. */
void diskDirStart(int fromState, int stopped);
/* Zero after a power-on; otherwise it numbers the run of saved states, in which nothing is written back. */
int  diskDirStateSession(void);
/* The megabytes of memory a directory's changes have grown past, once for every 64 of them; else zero. */
int  diskDirMemoryNotice(void);
/* Whether the drive holds changes that are still to be written back. */
int  diskDirPending(int driveId);
int  diskIsDir(int driveId);
const char* diskDirPath(int driveId);

#define DISK_DIR_NOTHING 0
#define DISK_DIR_CHANGED 1
#define DISK_DIR_INVALID 2
#define DISK_DIR_ERROR   3
/* Works out what the MSX changed; the host directory is not touched. The plan
** is to be given to diskDirApply or diskDirFreePlan. */
int  diskDirPlan(int driveId, void** plan, int* created, int* changed, int* deleted, int* skipped, int* mass);
/* Zero when nothing could be written; the directory is then as it was. The
** volume has to be discarded afterwards. */
int  diskDirApply(int driveId, void* plan, int* failed, char* session, int sessionSize);
void diskDirFreePlan(void* plan);
/* Forgets the volume of the drive together with what the MSX wrote to it. */
void diskDiscardDir(int driveId);
/* Whether the name is that of a host directory, inserted or not. */
int  diskIsDirPath(const char* path);
/* Counts what the volume of the drive left out; zero when there is nothing to tell. */
int  diskGetDirSkipped(int driveId, int* tooLarge, int* onlineOnly, int* subDirs);
int  diskIsHostMediumName(const char* fileName);
int  diskIsHostMediumOffered(int driveId);
int  diskHasHostMedium(void);


/* Surface the 720 KB overflow dialog at insert time (not next emu
** start); marks (driveId, path) so the next diskChange remount of the
** same dir doesn't repeat it. */
void diskPreviewDirOverflow(int driveId, const char* path);
void diskSetInfo(int driveId, char* fileName, const char* fileInZipFile);
void  diskEnable(int driveId, int enable);
UInt8 diskEnabled(int driveId);
UInt8 diskReadOnly(int driveId);
UInt8 diskPresent(int driveId);
DSKE  diskRead(int driveId, UInt8* buffer, int sector);
DSKE  diskReadSector(int driveId, UInt8* buffer, int sector, int side, int track, int density, int *sectorSize);
UInt8 diskWrite(int driveId, UInt8* buffer, int sector);
UInt8 diskWriteSector(int driveId, UInt8 *buffer, int sector, int side, int track, int density);
int   diskGetSectorsPerTrack(int driveId);
int   diskGetSectorSize(int driveId, int side, int track, int density);
int   diskIsCdrom(int driveId);
int   diskGetSides(int driveId);
int   diskChanged(int driveId);
int   _diskRead2(int driveId, UInt8* buffer, int sector, int numSectors);
int   _diskWrite2(int driveId, UInt8* buffer, int sector, int numSectors);
int   _diskGetTotalSectors(int driveId);
static int diskGetHdDriveId(int hdId, int driveNo) {
    return MAX_FDC_COUNT + MAX_DRIVES_PER_HD * hdId + driveNo;
}
static int diskGetUsbDriveId(int driveId, int driveNo) { 
    return MAX_FDC_COUNT + MAX_DRIVES_PER_HD * driveId + driveNo;
}

#endif

