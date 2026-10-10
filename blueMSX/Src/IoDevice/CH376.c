/*****************************************************************************
**
** CH376 USB host and file management chip.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
#include "CH376.h"
#include "Disk.h"
#include "Board.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>

#define CMD_GET_IC_VER      0x01
#define CMD_RESET_ALL       0x05
#define CMD_CHECK_EXIST     0x06
#define CMD_SET_RETRY       0x0b
#define CMD_GET_FILE_SIZE   0x0c
#define CMD_DELAY_100US     0x0f
#define CMD_SET_USB_ADDR    0x13
#define CMD_SET_USB_MODE    0x15
#define CMD_TEST_CONNECT    0x16
#define CMD_GET_STATUS      0x22
#define CMD_RD_USB_DATA0    0x27
#define CMD_WR_HOST_DATA    0x2c
#define CMD_SET_FILE_NAME   0x2f
#define CMD_DISK_CONNECT    0x30
#define CMD_DISK_MOUNT      0x31
#define CMD_FILE_OPEN       0x32
#define CMD_FILE_ENUM_GO    0x33
#define CMD_FILE_CREATE     0x34
#define CMD_FILE_ERASE      0x35
#define CMD_FILE_CLOSE      0x36
#define CMD_DIR_INFO_READ   0x37
#define CMD_BYTE_LOCATE     0x39
#define CMD_BYTE_READ       0x3a
#define CMD_BYTE_RD_GO      0x3b
#define CMD_BYTE_WRITE      0x3c
#define CMD_BYTE_WR_GO      0x3d
#define CMD_DISK_CAPACITY   0x3e
#define CMD_DISK_QUERY      0x3f
#define CMD_DIR_CREATE      0x40
#define CMD_SET_ADDRESS     0x45
#define CMD_GET_DESCR       0x46
#define CMD_SET_CONFIG      0x49
#define CMD_ISSUE_TKN_X     0x4e
#define CMD_DISK_INIT       0x51
#define CMD_DISK_READ       0x54
#define CMD_DISK_RD_GO      0x55
#define CMD_DISK_WRITE      0x56
#define CMD_DISK_WR_GO      0x57
#define CMD_DISK_INQUIRY    0x58
#define CMD_DISK_READY      0x59

#define CMD_RET_SUCCESS     0x51
#define CMD_RET_ABORT       0x5f

#define USB_INT_SUCCESS     0x14
#define USB_INT_CONNECT     0x15
#define USB_INT_DISCONNECT  0x16
#define USB_INT_DISK_READ   0x1d
#define USB_INT_DISK_WRITE  0x1e
#define USB_INT_DISK_ERR    0x1f
#define ERR_OPEN_DIR        0x41
#define ERR_MISS_FILE       0x42
#define ERR_DISK_DISCON     0x82

#define STATUS_INTB         0x80

#define SECTOR_SIZE         512
#define BLOCK_SIZE          64
#define INQUIRY_SIZE        36
#define DIR_ENTRY_SIZE      32
#define DIR_ENTRY_LIMIT     65536
#define IC_VERSION          0x43
#define NO_SECTOR           0xffffffff

#define ATTR_VOLUME         0x08
#define ATTR_DIRECTORY      0x10
#define ATTR_LONG_NAME      0x0f

/* DISK_QUERY reports these values to the host. */
#define FAT_NONE            0
#define FAT_12              1
#define FAT_16              2
#define FAT_32              3

#define NO_FILE             0
#define OPEN_FILE           1
#define OPEN_DIR            2

#define XFER_NONE           0
#define XFER_READ           1
#define XFER_WRITE          2

/* A command takes a fixed number of data bytes unless it is one of these. */
#define ARGS_STRING         -1
#define ARGS_COUNTED        -2

typedef struct {
    int    type;
    UInt32 fatStart;
    UInt32 rootStart;
    UInt32 rootEntries;
    UInt32 dataStart;
    UInt32 clusterSectors;
    UInt32 clusterCount;
    UInt32 rootCluster;
    UInt32 totalSectors;
} FatVolume;

struct CH376 {
    int    diskId;

    UInt8  command;
    int    argsNeeded;
    int    argCount;
    UInt8  args[256];

    UInt8  out[256];
    int    outLen;
    int    outPos;

    /* Waits for RD_USB_DATA0, while other commands come and go. */
    UInt8  block[255];
    int    blockLen;

    UInt8  hostData[BLOCK_SIZE];

    UInt32 delayStart;

    UInt8  usbMode;
    UInt8  intStatus;
    int    intPending;

    /* attached follows the disk image; diskReady needs a mount after that. */
    int    attached;
    int    diskReady;

    UInt8  sector[SECTOR_SIZE];
    int    transfer;
    int    sectorPos;
    UInt32 lba;
    int    sectorsLeft;

    FatVolume fat;
    char   fileName[16];
    char   pattern[11];
    int    openKind;
    UInt32 dirCluster;
    UInt32 fileCluster;
    UInt32 fileSize;
    UInt32 filePos;
    UInt32 bytesWanted;
    UInt32 enumIndex;

    /* Not saved: where the last chain walk got to, and two FAT sectors. */
    UInt32 walkStart;
    UInt32 walkCluster;
    UInt32 walkIndex;
    UInt32 fatCacheSector;
    UInt8  fatCache[SECTOR_SIZE * 2];
};

static const UInt8 deviceDescriptor[] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x86, 0x1a, 0x76, 0x53, 0x00, 0x01, 0, 0, 0, 1
};

static const UInt8 configDescriptor[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
    7, 5, 0x81, 0x02, 64, 0, 0,
    7, 5, 0x02, 0x02, 64, 0, 0
};

static UInt32 get16(const UInt8* p)
{
    return p[0] | (p[1] << 8);
}

static UInt32 get32(const UInt8* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((UInt32)p[3] << 24);
}

static void put32(UInt8* p, UInt32 value)
{
    p[0] = (UInt8)value;
    p[1] = (UInt8)(value >> 8);
    p[2] = (UInt8)(value >> 16);
    p[3] = (UInt8)(value >> 24);
}

static int hostEnabled(CH376* ch)
{
    return ch->usbMode == 5 || ch->usbMode == 6;
}

static int deviceConnected(CH376* ch)
{
    return hostEnabled(ch) && ch->attached;
}

static void raiseInterrupt(CH376* ch, UInt8 status)
{
    ch->intStatus  = status;
    ch->intPending = 1;
}

static void setOutput(CH376* ch, const UInt8* data, int length)
{
    memcpy(ch->out, data, length);
    ch->outLen = length;
    ch->outPos = 0;
}

static void setOutputByte(CH376* ch, UInt8 value)
{
    setOutput(ch, &value, 1);
}

static void setBlock(CH376* ch, const UInt8* data, int length)
{
    memcpy(ch->block, data, length);
    ch->blockLen = length;
}

static int readSector(CH376* ch, UInt32 lba, UInt8* buffer)
{
    return lba < (UInt32)_diskGetTotalSectors(ch->diskId) &&
           _diskRead2(ch->diskId, buffer, lba, 1);
}

/* The guest can rewrite the disk under the chip, so nothing read stays valid. */
static void forgetVolume(CH376* ch)
{
    ch->fat.type       = FAT_NONE;
    ch->openKind       = NO_FILE;
    ch->walkStart      = 0;
    ch->walkCluster    = 0;
    ch->walkIndex      = 0;
    ch->fatCacheSector = NO_SECTOR;
}

static void endTransfer(CH376* ch)
{
    ch->transfer    = XFER_NONE;
    ch->sectorsLeft = 0;
    ch->sectorPos   = 0;
}

/* ------------------------------- Hot plug -------------------------------- */

static void detachDevice(CH376* ch)
{
    ch->attached    = 0;
    ch->diskReady   = 0;
    ch->dirCluster  = 0;
    ch->bytesWanted = 0;
    endTransfer(ch);
    forgetVolume(ch);
}

static void attachDevice(CH376* ch)
{
    diskChanged(ch->diskId);
    ch->attached = 1;
}

/* An image that is removed or replaced is a device being unplugged. The new
** one is plugged in on a later call, after the host has been told. */
static void syncMedia(CH376* ch)
{
    if (ch->usbMode < 5) {
        return;
    }
    if (!ch->attached) {
        if (diskPresent(ch->diskId)) {
            attachDevice(ch);
        }
    }
    else if (!diskPresent(ch->diskId) || diskChanged(ch->diskId)) {
        detachDevice(ch);
        raiseInterrupt(ch, USB_INT_DISCONNECT);
    }
}

/* ------------------------------ FAT volume ------------------------------ */

static int parseBootSector(CH376* ch, const UInt8* boot, UInt32 start)
{
    FatVolume* fat        = &ch->fat;
    UInt32 imageSectors   = (UInt32)_diskGetTotalSectors(ch->diskId);
    UInt32 reserved       = get16(boot + 14);
    UInt32 fatCount       = boot[16];
    UInt32 fatSectors     = get16(boot + 22);
    UInt32 totalSectors   = get16(boot + 19);
    UInt32 clusterSectors = boot[13];
    UInt32 rootEntries    = get16(boot + 17);
    UInt32 rootSectors    = (rootEntries * DIR_ENTRY_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE;
    UInt64 dataStart;
    UInt64 volumeEnd;
    UInt64 clusters;
    UInt64 limit;
    int    type;

    if (get16(boot + 11) != SECTOR_SIZE || reserved == 0 || fatCount == 0 ||
        clusterSectors == 0 || (clusterSectors & (clusterSectors - 1)) != 0) {
        return 0;
    }
    if (fatSectors == 0) {
        fatSectors = get32(boot + 36);
    }
    if (totalSectors == 0) {
        totalSectors = get32(boot + 32);
    }
    dataStart = (UInt64)start + reserved + (UInt64)fatCount * fatSectors + rootSectors;
    volumeEnd = (UInt64)start + totalSectors;

    if (fatSectors == 0 || dataStart >= volumeEnd || dataStart >= imageSectors) {
        return 0;
    }
    clusters = (volumeEnd - dataStart) / clusterSectors;
    type     = clusters < 4085 ? FAT_12 : clusters < 65525 ? FAT_16 : FAT_32;

    /* Whatever the boot sector says, stay inside the image and inside the FAT. */
    limit = (imageSectors - dataStart) / clusterSectors;
    if (clusters > limit) {
        clusters = limit;
    }
    limit = (UInt64)fatSectors * SECTOR_SIZE * 8 / (type == FAT_12 ? 12 : type == FAT_16 ? 16 : 32);
    if (clusters + 2 > limit) {
        clusters = limit > 2 ? limit - 2 : 0;
    }
    fat->type           = type;
    fat->fatStart       = start + reserved;
    fat->rootStart      = (UInt32)(dataStart - rootSectors);
    fat->rootEntries    = rootEntries;
    fat->dataStart      = (UInt32)dataStart;
    fat->clusterSectors = clusterSectors;
    fat->clusterCount   = (UInt32)clusters;
    fat->rootCluster    = type == FAT_32 ? get32(boot + 44) : 0;
    fat->totalSectors   = totalSectors;

    return 1;
}

/* The volume is either the whole disk or the first partition of an MBR. */
static void mountVolume(CH376* ch)
{
    UInt8 boot[SECTOR_SIZE];
    UInt8 part[SECTOR_SIZE];
    int i;

    forgetVolume(ch);

    if (!readSector(ch, 0, boot) || boot[510] != 0x55 || boot[511] != 0xaa) {
        return;
    }
    if (parseBootSector(ch, boot, 0)) {
        return;
    }
    for (i = 0; i < 4; i++) {
        const UInt8* entry = boot + 446 + 16 * i;
        UInt32 start = get32(entry + 8);

        if (entry[4] != 0 && readSector(ch, start, part) && parseBootSector(ch, part, start)) {
            return;
        }
    }
}

static int clusterValid(FatVolume* fat, UInt32 cluster)
{
    return cluster >= 2 && cluster - 2 < fat->clusterCount;
}

static UInt32 nextCluster(CH376* ch, UInt32 cluster)
{
    FatVolume* fat = &ch->fat;
    UInt32 offset;
    UInt32 sector;
    UInt32 value;

    switch (fat->type) {
    case FAT_12: offset = cluster + cluster / 2; break;
    case FAT_16: offset = cluster * 2;           break;
    default:     offset = cluster * 4;           break;
    }
    sector = fat->fatStart + offset / SECTOR_SIZE;

    /* Two sectors, because a FAT12 entry can straddle the boundary. */
    if (sector != ch->fatCacheSector) {
        ch->fatCacheSector = NO_SECTOR;
        if (!readSector(ch, sector, ch->fatCache)) {
            return 0;
        }
        if (!readSector(ch, sector + 1, ch->fatCache + SECTOR_SIZE)) {
            memset(ch->fatCache + SECTOR_SIZE, 0, SECTOR_SIZE);
        }
        ch->fatCacheSector = sector;
    }
    offset %= SECTOR_SIZE;

    switch (fat->type) {
    case FAT_12:
        value = get16(ch->fatCache + offset);
        return (cluster & 1) ? value >> 4 : value & 0x0fff;
    case FAT_16:
        return get16(ch->fatCache + offset);
    default:
        return get32(ch->fatCache + offset) & 0x0fffffff;
    }
}

/* The cluster `index` links into a chain, or 0 past its end. Continues from
** the previous call when it can; a chain longer than the volume is a loop. */
static UInt32 walkChain(CH376* ch, UInt32 start, UInt32 index)
{
    FatVolume* fat = &ch->fat;

    if (index >= fat->clusterCount || !clusterValid(fat, start)) {
        return 0;
    }
    if (ch->walkStart != start || index < ch->walkIndex) {
        ch->walkStart   = start;
        ch->walkCluster = start;
        ch->walkIndex   = 0;
    }
    while (ch->walkIndex < index) {
        if (!clusterValid(fat, ch->walkCluster)) {
            return 0;
        }
        ch->walkCluster = nextCluster(ch, ch->walkCluster);
        ch->walkIndex++;
    }
    return clusterValid(fat, ch->walkCluster) ? ch->walkCluster : 0;
}

/* Sector that holds byte `pos` of the chain starting at `start`; 0 past its end. */
static UInt32 chainSector(CH376* ch, UInt32 start, UInt32 pos)
{
    FatVolume* fat = &ch->fat;
    UInt32 clusterBytes = fat->clusterSectors * SECTOR_SIZE;
    UInt32 cluster = walkChain(ch, start, pos / clusterBytes);

    if (cluster == 0) {
        return 0;
    }
    return fat->dataStart + (cluster - 2) * fat->clusterSectors + pos % clusterBytes / SECTOR_SIZE;
}

/* dirCluster 0 is the root directory: a fixed area, or a chain on FAT32. */
static int readDirEntry(CH376* ch, UInt32 dirCluster, UInt32 index, UInt8* entry)
{
    FatVolume* fat = &ch->fat;
    UInt8 buffer[SECTOR_SIZE];
    UInt32 pos = index * DIR_ENTRY_SIZE;
    UInt32 lba;

    if (index >= DIR_ENTRY_LIMIT) {
        return 0;
    }
    if (dirCluster == 0 && fat->type == FAT_32) {
        dirCluster = fat->rootCluster;
    }
    if (dirCluster == 0) {
        if (index >= fat->rootEntries) {
            return 0;
        }
        lba = fat->rootStart + pos / SECTOR_SIZE;
    }
    else {
        lba = chainSector(ch, dirCluster, pos);
    }
    if (lba == 0 || !readSector(ch, lba, buffer)) {
        return 0;
    }
    memcpy(entry, buffer + pos % SECTOR_SIZE, DIR_ENTRY_SIZE);
    return entry[0] != 0;
}

/* Turns "NAME.EXT" into the 11 character directory form; '*' matches the rest. */
static void makePattern(const char* name, char* pattern)
{
    int i = 0;

    memset(pattern, ' ', 11);

    /* "." and ".." are stored as they are. */
    while (*name == '.' && i < 2) {
        pattern[i++] = *name++;
    }
    for (; *name != 0 && *name != '.'; name++) {
        if (*name == '*') {
            memset(pattern + i, '?', 11 - i);
            return;
        }
        if (i < 8) {
            pattern[i++] = *name;
        }
    }
    if (*name == '.') {
        name++;
    }
    for (i = 8; *name != 0 && i < 11; name++) {
        if (*name == '*') {
            memset(pattern + i, '?', 11 - i);
            return;
        }
        pattern[i++] = *name;
    }
}

static int findEntry(CH376* ch, UInt32 dirCluster, const char* pattern, UInt32* index, UInt8* entry)
{
    for (; readDirEntry(ch, dirCluster, *index, entry); (*index)++) {
        int i;

        if (entry[0] == 0xe5 || (entry[11] & ATTR_LONG_NAME) == ATTR_LONG_NAME || (entry[11] & ATTR_VOLUME)) {
            continue;
        }
        for (i = 0; i < 11; i++) {
            if (pattern[i] != '?' && pattern[i] != (char)entry[i]) {
                break;
            }
        }
        if (i == 11) {
            return 1;
        }
    }
    return 0;
}

static UInt32 entryCluster(CH376* ch, const UInt8* entry)
{
    UInt32 cluster = get16(entry + 26);

    if (ch->fat.type == FAT_32) {
        cluster |= get16(entry + 20) << 16;
    }
    return cluster;
}

static void enumerateNext(CH376* ch)
{
    UInt8 entry[DIR_ENTRY_SIZE];

    if (ch->diskReady && ch->fat.type == FAT_NONE) {
        mountVolume(ch);
    }
    if (!ch->diskReady || ch->fat.type == FAT_NONE ||
        !findEntry(ch, ch->dirCluster, ch->pattern, &ch->enumIndex, entry)) {
        raiseInterrupt(ch, ERR_MISS_FILE);
        return;
    }
    ch->enumIndex++;
    setBlock(ch, entry, DIR_ENTRY_SIZE);
    raiseInterrupt(ch, USB_INT_DISK_READ);
}

static void openFile(CH376* ch)
{
    const char* name = ch->fileName;
    UInt8 entry[DIR_ENTRY_SIZE];
    UInt32 index = 0;

    ch->openKind = NO_FILE;

    if (!ch->diskReady) {
        raiseInterrupt(ch, ERR_DISK_DISCON);
        return;
    }
    if (ch->fat.type == FAT_NONE) {
        mountVolume(ch);
    }
    if (ch->fat.type == FAT_NONE) {
        raiseInterrupt(ch, ERR_MISS_FILE);
        return;
    }
    /* An empty name only goes back to the root directory. */
    if (*name == 0) {
        ch->dirCluster = 0;
        raiseInterrupt(ch, USB_INT_SUCCESS);
        return;
    }
    if (*name == '/' || *name == '\\') {
        ch->dirCluster = 0;
        name++;
        if (*name == 0) {
            ch->openKind = OPEN_DIR;
            raiseInterrupt(ch, ERR_OPEN_DIR);
            return;
        }
    }
    makePattern(name, ch->pattern);

    if (strchr(name, '*') != NULL) {
        ch->enumIndex = 0;
        enumerateNext(ch);
        return;
    }
    if (!findEntry(ch, ch->dirCluster, ch->pattern, &index, entry)) {
        raiseInterrupt(ch, ERR_MISS_FILE);
        return;
    }
    if (entry[11] & ATTR_DIRECTORY) {
        ch->openKind   = OPEN_DIR;
        ch->dirCluster = entryCluster(ch, entry);
        raiseInterrupt(ch, ERR_OPEN_DIR);
        return;
    }
    ch->openKind    = OPEN_FILE;
    ch->fileCluster = entryCluster(ch, entry);
    ch->fileSize    = get32(entry + 28);
    ch->filePos     = 0;
    ch->bytesWanted = 0;
    raiseInterrupt(ch, USB_INT_SUCCESS);
}

/* Hands out file data up to the next sector boundary, as one block. */
static void readFileBlock(CH376* ch)
{
    UInt8 buffer[SECTOR_SIZE];
    UInt32 offset = ch->filePos % SECTOR_SIZE;
    UInt32 length = ch->bytesWanted;
    UInt32 lba;

    if (ch->openKind != OPEN_FILE) {
        length = 0;
    }
    else if (length > ch->fileSize - ch->filePos) {
        length = ch->fileSize - ch->filePos;
    }
    if (length > SECTOR_SIZE - offset) {
        length = SECTOR_SIZE - offset;
    }
    if (length > sizeof(ch->block)) {
        length = sizeof(ch->block);
    }
    if (length == 0) {
        ch->bytesWanted = 0;
        raiseInterrupt(ch, USB_INT_SUCCESS);
        return;
    }
    lba = chainSector(ch, ch->fileCluster, ch->filePos);

    if (lba == 0 || !readSector(ch, lba, buffer)) {
        ch->bytesWanted = 0;
        raiseInterrupt(ch, USB_INT_DISK_ERR);
        return;
    }
    setBlock(ch, buffer + offset, length);
    ch->filePos     += length;
    ch->bytesWanted -= length;
    raiseInterrupt(ch, USB_INT_DISK_READ);
}

static void locateFile(CH376* ch)
{
    UInt8 data[4];
    UInt32 lba = 0xffffffff;

    if (ch->openKind != OPEN_FILE) {
        raiseInterrupt(ch, ERR_MISS_FILE);
        return;
    }
    ch->filePos = get32(ch->args);

    if (ch->filePos >= ch->fileSize) {
        ch->filePos = ch->fileSize;
    }
    else {
        lba = chainSector(ch, ch->fileCluster, ch->filePos);
        if (lba == 0) {
            raiseInterrupt(ch, USB_INT_DISK_ERR);
            return;
        }
    }
    put32(data, lba);
    setBlock(ch, data, 4);
    raiseInterrupt(ch, USB_INT_SUCCESS);
}

static void queryDisk(CH376* ch)
{
    FatVolume* fat = &ch->fat;
    UInt8 data[9];
    UInt32 freeClusters = 0;
    UInt32 i;

    if (ch->diskReady && fat->type == FAT_NONE) {
        mountVolume(ch);
    }
    if (!ch->diskReady || fat->type == FAT_NONE) {
        raiseInterrupt(ch, USB_INT_DISK_ERR);
        return;
    }
    for (i = 0; i < fat->clusterCount; i++) {
        if (nextCluster(ch, i + 2) == 0) {
            freeClusters++;
        }
    }
    put32(data,     fat->totalSectors);
    put32(data + 4, freeClusters * fat->clusterSectors);
    data[8] = (UInt8)fat->type;
    setBlock(ch, data, sizeof(data));
    raiseInterrupt(ch, USB_INT_SUCCESS);
}

/* ---------------------------- Sector transfers --------------------------- */

static void failDiskTransfer(CH376* ch)
{
    endTransfer(ch);
    raiseInterrupt(ch, USB_INT_DISK_ERR);
}

static void readNextBlock(CH376* ch)
{
    if (ch->transfer != XFER_READ) {
        failDiskTransfer(ch);
        return;
    }
    if (ch->sectorPos == 0) {
        if (ch->sectorsLeft == 0) {
            endTransfer(ch);
            raiseInterrupt(ch, USB_INT_SUCCESS);
            return;
        }
        if (!readSector(ch, ch->lba, ch->sector)) {
            failDiskTransfer(ch);
            return;
        }
        ch->lba++;
        ch->sectorsLeft--;
    }
    setBlock(ch, ch->sector + ch->sectorPos, BLOCK_SIZE);
    ch->sectorPos = (ch->sectorPos + BLOCK_SIZE) % SECTOR_SIZE;
    raiseInterrupt(ch, USB_INT_DISK_READ);
}

static void writeNextBlock(CH376* ch)
{
    if (ch->transfer != XFER_WRITE || ch->sectorsLeft == 0) {
        failDiskTransfer(ch);
        return;
    }
    memcpy(ch->sector + ch->sectorPos, ch->hostData, BLOCK_SIZE);
    ch->sectorPos = (ch->sectorPos + BLOCK_SIZE) % SECTOR_SIZE;

    if (ch->sectorPos == 0) {
        forgetVolume(ch);
        if (ch->lba >= (UInt32)_diskGetTotalSectors(ch->diskId) ||
            !_diskWrite2(ch->diskId, ch->sector, ch->lba, 1)) {
            failDiskTransfer(ch);
            return;
        }
        ch->lba++;
        ch->sectorsLeft--;
    }
    if (ch->sectorsLeft == 0) {
        endTransfer(ch);
        raiseInterrupt(ch, USB_INT_SUCCESS);
        return;
    }
    raiseInterrupt(ch, USB_INT_DISK_WRITE);
}

static void startDiskTransfer(CH376* ch, int transfer)
{
    endTransfer(ch);

    if (!ch->diskReady) {
        raiseInterrupt(ch, USB_INT_DISK_ERR);
        return;
    }
    ch->lba         = get32(ch->args);
    ch->sectorsLeft = ch->args[4];
    ch->transfer    = transfer;

    if (transfer == XFER_READ) {
        readNextBlock(ch);
    }
    else if (ch->sectorsLeft == 0) {
        endTransfer(ch);
        raiseInterrupt(ch, USB_INT_SUCCESS);
    }
    else {
        raiseInterrupt(ch, USB_INT_DISK_WRITE);
    }
}

/* -------------------------------- Commands ------------------------------- */

static void buildInquiry(UInt8* data)
{
    memset(data, 0, INQUIRY_SIZE);
    data[1] = 0x80;
    data[2] = 0x02;
    data[3] = 0x02;
    data[4] = INQUIRY_SIZE - 5;
    memcpy(data + 8,  "blueMSX ", 8);
    memcpy(data + 16, "USB Flash Disk  ", 16);
    memcpy(data + 32, "1.00", 4);
}

static void setUsbMode(CH376* ch, UInt8 mode)
{
    if (mode > 7) {
        setOutputByte(ch, CMD_RET_ABORT);
        return;
    }
    ch->usbMode = mode;
    detachDevice(ch);
    setOutputByte(ch, CMD_RET_SUCCESS);

    if (mode >= 5 && diskPresent(ch->diskId)) {
        attachDevice(ch);
        if (hostEnabled(ch)) {
            raiseInterrupt(ch, USB_INT_CONNECT);
        }
    }
}

static void execute(CH376* ch)
{
    UInt8 data[INQUIRY_SIZE];

    switch (ch->command) {
    case CMD_GET_IC_VER:
        setOutputByte(ch, IC_VERSION);
        break;

    case CMD_RESET_ALL:
        ch376Reset(ch);
        break;

    case CMD_CHECK_EXIST:
        setOutputByte(ch, (UInt8)~ch->args[0]);
        break;

    case CMD_GET_FILE_SIZE:
        put32(data, ch->openKind == OPEN_FILE ? ch->fileSize : 0xffffffff);
        setOutput(ch, data, 4);
        break;

    case CMD_DELAY_100US:
        ch->delayStart = boardSystemTime();
        break;

    case CMD_SET_USB_MODE:
        setUsbMode(ch, ch->args[0]);
        break;

    case CMD_TEST_CONNECT:
        setOutputByte(ch, deviceConnected(ch) ? USB_INT_CONNECT : USB_INT_DISCONNECT);
        break;

    case CMD_GET_STATUS:
        setOutputByte(ch, ch->intStatus);
        ch->intPending = 0;
        break;

    case CMD_RD_USB_DATA0:
        ch->out[0] = (UInt8)ch->blockLen;
        memcpy(ch->out + 1, ch->block, ch->blockLen);
        ch->outLen = ch->blockLen + 1;
        ch->outPos = 0;
        break;

    case CMD_WR_HOST_DATA:
        memset(ch->hostData, 0, sizeof(ch->hostData));
        memcpy(ch->hostData, ch->args + 1, ch->args[0] > BLOCK_SIZE ? BLOCK_SIZE : ch->args[0]);
        break;

    case CMD_SET_FILE_NAME:
        strncpy(ch->fileName, (const char*)ch->args, sizeof(ch->fileName) - 1);
        ch->fileName[sizeof(ch->fileName) - 1] = 0;
        break;

    case CMD_DISK_CONNECT:
        raiseInterrupt(ch, deviceConnected(ch) ? USB_INT_SUCCESS : USB_INT_DISCONNECT);
        break;

    case CMD_DISK_INIT:
    case CMD_DISK_MOUNT:
        if (!deviceConnected(ch)) {
            raiseInterrupt(ch, USB_INT_DISCONNECT);
            break;
        }
        ch->diskReady = 1;
        if (ch->command == CMD_DISK_MOUNT) {
            mountVolume(ch);
            ch->dirCluster = 0;
            buildInquiry(data);
            setBlock(ch, data, INQUIRY_SIZE);
        }
        raiseInterrupt(ch, USB_INT_SUCCESS);
        break;

    case CMD_DISK_READY:
        raiseInterrupt(ch, ch->diskReady ? USB_INT_SUCCESS : USB_INT_DISK_ERR);
        break;

    case CMD_DISK_INQUIRY:
        if (!ch->diskReady) {
            raiseInterrupt(ch, USB_INT_DISK_ERR);
            break;
        }
        buildInquiry(data);
        setBlock(ch, data, INQUIRY_SIZE);
        raiseInterrupt(ch, USB_INT_SUCCESS);
        break;

    case CMD_FILE_OPEN:
        openFile(ch);
        break;

    case CMD_FILE_ENUM_GO:
        enumerateNext(ch);
        break;

    case CMD_FILE_CLOSE:
        ch->openKind = NO_FILE;
        raiseInterrupt(ch, USB_INT_SUCCESS);
        break;

    case CMD_BYTE_LOCATE:
        locateFile(ch);
        break;

    case CMD_BYTE_READ:
        ch->bytesWanted = get16(ch->args);
        readFileBlock(ch);
        break;

    case CMD_BYTE_RD_GO:
        readFileBlock(ch);
        break;

    case CMD_DISK_CAPACITY:
        if (!ch->diskReady) {
            raiseInterrupt(ch, USB_INT_DISK_ERR);
            break;
        }
        put32(data, (UInt32)_diskGetTotalSectors(ch->diskId));
        setBlock(ch, data, 4);
        raiseInterrupt(ch, USB_INT_SUCCESS);
        break;

    case CMD_DISK_QUERY:
        queryDisk(ch);
        break;

    case CMD_GET_DESCR:
        if (!deviceConnected(ch)) {
            raiseInterrupt(ch, USB_INT_DISCONNECT);
            break;
        }
        if (ch->args[0] == 1) {
            setBlock(ch, deviceDescriptor, sizeof(deviceDescriptor));
        }
        else {
            setBlock(ch, configDescriptor, sizeof(configDescriptor));
        }
        raiseInterrupt(ch, USB_INT_SUCCESS);
        break;

    case CMD_DISK_READ:
        startDiskTransfer(ch, XFER_READ);
        break;

    case CMD_DISK_RD_GO:
        readNextBlock(ch);
        break;

    case CMD_DISK_WRITE:
        startDiskTransfer(ch, XFER_WRITE);
        break;

    case CMD_DISK_WR_GO:
        writeNextBlock(ch);
        break;

    /* Not emulated: the host waits for an interrupt, so it gets a failure. */
    case CMD_FILE_CREATE:
    case CMD_FILE_ERASE:
    case CMD_DIR_INFO_READ:
    case CMD_BYTE_WRITE:
    case CMD_BYTE_WR_GO:
    case CMD_DIR_CREATE:
    case CMD_SET_ADDRESS:
    case CMD_SET_CONFIG:
    case CMD_ISSUE_TKN_X:
        raiseInterrupt(ch, USB_INT_DISK_ERR);
        break;
    }
}

static int argumentCount(UInt8 command)
{
    switch (command) {
    case CMD_CHECK_EXIST:
    case CMD_SET_USB_ADDR:
    case CMD_SET_USB_MODE:
    case CMD_GET_FILE_SIZE:
    case CMD_FILE_CLOSE:
    case CMD_DIR_INFO_READ:
    case CMD_SET_ADDRESS:
    case CMD_GET_DESCR:
    case CMD_SET_CONFIG:
        return 1;
    case CMD_SET_RETRY:
    case CMD_BYTE_READ:
    case CMD_BYTE_WRITE:
    case CMD_ISSUE_TKN_X:
        return 2;
    case CMD_BYTE_LOCATE:
        return 4;
    case CMD_DISK_READ:
    case CMD_DISK_WRITE:
        return 5;
    case CMD_SET_FILE_NAME:
        return ARGS_STRING;
    case CMD_WR_HOST_DATA:
        return ARGS_COUNTED;
    }
    return 0;
}

void ch376WriteCommand(CH376* ch, UInt8 value)
{
    syncMedia(ch);

    ch->command    = value;
    ch->argCount   = 0;
    ch->argsNeeded = argumentCount(value);
    ch->outLen     = 0;
    ch->outPos     = 0;

    if (ch->argsNeeded == 0) {
        execute(ch);
    }
}

void ch376WriteData(CH376* ch, UInt8 value)
{
    int done;

    if (ch->argsNeeded == 0) {
        return;
    }
    if (ch->argCount >= (int)sizeof(ch->args)) {
        ch->argsNeeded = 0;
        return;
    }
    ch->args[ch->argCount++] = value;

    if (ch->argsNeeded == ARGS_STRING) {
        /* Leaves room for the terminator the name is used with. */
        done = value == 0 || ch->argCount == sizeof(ch->args) - 1;
        ch->args[ch->argCount] = 0;
    }
    else if (ch->argsNeeded == ARGS_COUNTED) {
        done = ch->argCount == 1 + ch->args[0];
    }
    else {
        done = ch->argCount == ch->argsNeeded;
    }
    if (done) {
        ch->argsNeeded = 0;
        execute(ch);
    }
}

UInt8 ch376ReadData(CH376* ch)
{
    UInt8 value = ch->outPos < ch->outLen ? ch->out[ch->outPos] : 0;

    if (ch->outPos < ch->outLen) {
        ch->outPos++;
    }
    /* DELAY_100US reads as zero until the time is up. */
    if (ch->command == CMD_DELAY_100US) {
        value = boardSystemTime() - ch->delayStart >= boardFrequency() / 10000;
    }
    return value;
}

UInt8 ch376ReadStatus(CH376* ch)
{
    syncMedia(ch);

    /* A plugged device keeps asking for attention until it is mounted, since
    ** a host that missed the first request would otherwise never mount it. */
    if (deviceConnected(ch) && !ch->diskReady && !ch->intPending &&
        ch->argsNeeded == 0 && ch->outPos >= ch->outLen) {
        raiseInterrupt(ch, USB_INT_CONNECT);
    }
    return ch->intPending ? 0 : STATUS_INTB;
}

void ch376Reset(CH376* ch)
{
    ch->command     = 0;
    ch->argsNeeded  = 0;
    ch->argCount    = 0;
    ch->outLen      = 0;
    ch->outPos      = 0;
    ch->blockLen    = 0;
    ch->usbMode     = 0;
    ch->intStatus   = 0;
    ch->intPending  = 0;
    ch->lba         = 0;
    ch->fileName[0] = 0;
    ch->enumIndex   = 0;
    ch->delayStart  = 0;
    detachDevice(ch);
}

CH376* ch376Create(int diskId)
{
    CH376* ch = calloc(1, sizeof(CH376));

    ch->diskId = diskId;
    ch376Reset(ch);

    return ch;
}

void ch376Destroy(CH376* ch)
{
    free(ch);
}

void ch376SaveState(CH376* ch)
{
    SaveState* state = saveStateOpenForWrite("ch376");

    saveStateSet(state, "command",        ch->command);
    saveStateSet(state, "argsNeeded",     ch->argsNeeded);
    saveStateSet(state, "argCount",       ch->argCount);
    saveStateSet(state, "outLen",         ch->outLen);
    saveStateSet(state, "outPos",         ch->outPos);
    saveStateSet(state, "blockLen",       ch->blockLen);
    saveStateSet(state, "usbMode",        ch->usbMode);
    saveStateSet(state, "intStatus",      ch->intStatus);
    saveStateSet(state, "intPending",     ch->intPending);
    saveStateSet(state, "attached",       ch->attached);
    saveStateSet(state, "diskReady",      ch->diskReady);
    saveStateSet(state, "transfer",       ch->transfer);
    saveStateSet(state, "sectorPos",      ch->sectorPos);
    saveStateSet(state, "lba",            ch->lba);
    saveStateSet(state, "sectorsLeft",    ch->sectorsLeft);
    saveStateSet(state, "openKind",       ch->openKind);
    saveStateSet(state, "dirCluster",     ch->dirCluster);
    saveStateSet(state, "fileCluster",    ch->fileCluster);
    saveStateSet(state, "fileSize",       ch->fileSize);
    saveStateSet(state, "filePos",        ch->filePos);
    saveStateSet(state, "bytesWanted",    ch->bytesWanted);
    saveStateSet(state, "enumIndex",      ch->enumIndex);
    saveStateSetBuffer(state, "args",     ch->args,     sizeof(ch->args));
    saveStateSetBuffer(state, "out",      ch->out,      sizeof(ch->out));
    saveStateSetBuffer(state, "block",    ch->block,    sizeof(ch->block));
    saveStateSetBuffer(state, "hostData", ch->hostData, sizeof(ch->hostData));
    saveStateSetBuffer(state, "sector",   ch->sector,   sizeof(ch->sector));
    saveStateSetBuffer(state, "fileName", (UInt8*)ch->fileName, sizeof(ch->fileName));
    saveStateSetBuffer(state, "pattern",  (UInt8*)ch->pattern,  sizeof(ch->pattern));

    saveStateClose(state);
}

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

void ch376LoadState(CH376* ch)
{
    SaveState* state = saveStateOpenForRead("ch376");
    int openKind;

    ch->command     = (UInt8)saveStateGet(state, "command",     0);
    ch->argsNeeded  = clamp(saveStateGet(state, "argsNeeded",  0), ARGS_COUNTED, 5);
    ch->argCount    = clamp(saveStateGet(state, "argCount",    0), 0, (int)sizeof(ch->args) - 2);
    ch->outLen      = clamp(saveStateGet(state, "outLen",      0), 0, (int)sizeof(ch->out));
    ch->outPos      = clamp(saveStateGet(state, "outPos",      0), 0, ch->outLen);
    ch->blockLen    = clamp(saveStateGet(state, "blockLen",    0), 0, (int)sizeof(ch->block));
    ch->usbMode     = (UInt8)saveStateGet(state, "usbMode",     0) & 7;
    ch->intStatus   = (UInt8)saveStateGet(state, "intStatus",   0);
    ch->intPending  =        saveStateGet(state, "intPending",  0);
    ch->attached    =        saveStateGet(state, "attached",    0);
    ch->diskReady   =        saveStateGet(state, "diskReady",   0);
    ch->transfer    = clamp(saveStateGet(state, "transfer",    XFER_NONE), XFER_NONE, XFER_WRITE);
    ch->sectorPos   = clamp(saveStateGet(state, "sectorPos",   0), 0, SECTOR_SIZE - BLOCK_SIZE) & ~(BLOCK_SIZE - 1);
    ch->lba         =        saveStateGet(state, "lba",         0);
    ch->sectorsLeft = clamp(saveStateGet(state, "sectorsLeft", 0), 0, 255);
    openKind        = clamp(saveStateGet(state, "openKind",    NO_FILE), NO_FILE, OPEN_DIR);
    ch->dirCluster  =        saveStateGet(state, "dirCluster",  0);
    ch->fileCluster =        saveStateGet(state, "fileCluster", 0);
    ch->fileSize    =        saveStateGet(state, "fileSize",    0);
    ch->filePos     =        saveStateGet(state, "filePos",     0);
    ch->bytesWanted =        saveStateGet(state, "bytesWanted", 0);
    ch->enumIndex   =        saveStateGet(state, "enumIndex",   0);
    saveStateGetBuffer(state, "args",     ch->args,     sizeof(ch->args));
    saveStateGetBuffer(state, "out",      ch->out,      sizeof(ch->out));
    saveStateGetBuffer(state, "block",    ch->block,    sizeof(ch->block));
    saveStateGetBuffer(state, "hostData", ch->hostData, sizeof(ch->hostData));
    saveStateGetBuffer(state, "sector",   ch->sector,   sizeof(ch->sector));
    saveStateGetBuffer(state, "fileName", (UInt8*)ch->fileName, sizeof(ch->fileName));
    saveStateGetBuffer(state, "pattern",  (UInt8*)ch->pattern,  sizeof(ch->pattern));

    saveStateClose(state);

    ch->fileName[sizeof(ch->fileName) - 1] = 0;

    if (!ch->attached) {
        ch->diskReady = 0;
    }
    ch->delayStart = boardSystemTime();

    /* The volume is read again from the image in the drive. */
    forgetVolume(ch);
    if (ch->diskReady) {
        mountVolume(ch);
        if (ch->fat.type != FAT_NONE) {
            ch->openKind = openKind;
        }
    }
    else {
        endTransfer(ch);
    }
    if (ch->filePos > ch->fileSize) {
        ch->filePos = ch->fileSize;
    }
    /* The image the state was saved with counts as the one still inserted. */
    if (ch->attached) {
        diskChanged(ch->diskId);
    }
}
