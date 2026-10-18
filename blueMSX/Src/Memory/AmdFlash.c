/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/AmdFlash.c,v $
**
** $Revision: 1.15 $
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
#include "AmdFlash.h"
#include "SaveState.h"
#include "sramLoader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


// Minimal AMD flash emulation to support the obsonet flash

typedef struct {
    UInt32 address;
    UInt8  value;
} AmdCmd;

#define ST_IDLE     0
#define ST_IDENT    1
#define ST_CFI      2

/* WBP for S29GL064S uses a 256-byte page + 4 setup + 1 confirm = 261 slots.
** Round up for future headroom. */
#define AMDFLASH_WBP_PAGE_BYTES 256
#define AMDFLASH_CMD_SLOTS      280

struct AmdFlash
{
    UInt8* romData;
    AmdType amdType;
    UInt32 cmdAddr1;
    UInt32 cmdAddr2;
    int    state;
    int    flashSize;
    int    sectorSize;
    int    isX8X16;             /* x16 chip in x8 mode: cmd-decode address is byte_addr >> 1 */
    AmdCmd cmd[AMDFLASH_CMD_SLOTS];
    int    cmdIdx;
    UInt32 writeProtectMask;
    int    fastCommands;        /* M29W640-family 0x56 quad program enabled */
    char   sramFilename[512];
    char   flashFilename[520];
    int    persistMode;         /* PM_* below */
    UInt8* writtenMask;         /* one bit per byte the MSX erased or programmed */
    int    wroteThisSession;
};

#define PM_FULL     0           /* whole image in sramFilename */
#define PM_OVERLAY  1           /* written bytes only, in flashFilename */
#define PM_NONE     2           /* save file unusable: load and save nothing */

#define FLASHFILE_MAGIC   "BMXFLSH"
#define FLASHFILE_BLOCK   4096
#define FLASHFILE_MASK    (FLASHFILE_BLOCK / 8)
#define FLASHFILE_HEADER  24
#define FLASHFILE_RECORD  (4 + FLASHFILE_MASK + FLASHFILE_BLOCK)

static void (*problemHandler)(int problem, const char* fileName);

static void markWritten(AmdFlash* rm, UInt32 offset, UInt32 length)
{
    UInt32 i;

    if (rm->writtenMask == NULL) {
        return;
    }
    for (i = offset; i < offset + length; i++) {
        rm->writtenMask[i >> 3] |= (UInt8)(1 << (i & 7));
    }
    rm->wroteThisSession = 1;
}

/* On x8/x16 chips wired in x8 mode the internal command decoder inspects
** native word addresses; the byte address the host writes is right-shifted
** by one before matching against cmdAddr1/cmdAddr2. */
static UInt32 cmdAddrBits(AmdFlash* rm, UInt32 addr)
{
    if (rm->isX8X16) addr >>= 1;
    return addr & 0x7ff;
}

/* The mask covers the first 32 sectors; the address is folded into the
** flash first, so a mirrored address checks the sector it writes to. */
static int isProtected(AmdFlash* rm, UInt32 address)
{
    UInt32 sector = (address & (rm->flashSize - 1)) / rm->sectorSize;
    return sector < 32 && ((rm->writeProtectMask >> sector) & 1);
}

static int checkCommandEraseSector(AmdFlash* rm)
{
    if (rm->cmdIdx > 0 && (cmdAddrBits(rm, rm->cmd[0].address) != rm->cmdAddr1 || rm->cmd[0].value != 0xaa)) return 0;
    if (rm->cmdIdx > 1 && (cmdAddrBits(rm, rm->cmd[1].address) != rm->cmdAddr2 || rm->cmd[1].value != 0x55)) return 0;
    if (rm->cmdIdx > 2 && (cmdAddrBits(rm, rm->cmd[2].address) != rm->cmdAddr1 || rm->cmd[2].value != 0x80)) return 0;
    if (rm->cmdIdx > 3 && (cmdAddrBits(rm, rm->cmd[3].address) != rm->cmdAddr1 || rm->cmd[3].value != 0xaa)) return 0;
    if (rm->cmdIdx > 4 && (cmdAddrBits(rm, rm->cmd[4].address) != rm->cmdAddr2 || rm->cmd[4].value != 0x55)) return 0;
    if (rm->cmdIdx > 5 && (                                                       rm->cmd[5].value != 0x30)) return 0;

    if (rm->cmdIdx < 6) return 1;

    if (!isProtected(rm, rm->cmd[5].address)) {
        UInt32 base = rm->cmd[5].address & ~(rm->sectorSize - 1) & (rm->flashSize - 1);
        memset(rm->romData + base, 0xff, rm->sectorSize);
        markWritten(rm, base, rm->sectorSize);
    }
    return 0;
}

static int checkCommandEraseChip(AmdFlash* rm) 
{
    if (rm->cmdIdx > 0 && (cmdAddrBits(rm, rm->cmd[0].address) != rm->cmdAddr1 || rm->cmd[0].value != 0xaa)) return 0;
    if (rm->cmdIdx > 1 && (cmdAddrBits(rm, rm->cmd[1].address) != rm->cmdAddr2 || rm->cmd[1].value != 0x55)) return 0;
    if (rm->cmdIdx > 2 && (cmdAddrBits(rm, rm->cmd[2].address) != rm->cmdAddr1 || rm->cmd[2].value != 0x80)) return 0;
    if (rm->cmdIdx > 3 && (cmdAddrBits(rm, rm->cmd[3].address) != rm->cmdAddr1 || rm->cmd[3].value != 0xaa)) return 0;
    if (rm->cmdIdx > 4 && (cmdAddrBits(rm, rm->cmd[4].address) != rm->cmdAddr2 || rm->cmd[4].value != 0x55)) return 0;
    if (rm->cmdIdx > 5 && (                                                       rm->cmd[5].value != 0x10)) return 0;

    if (rm->cmdIdx < 6) return 1;

    {
        int offset;
        for (offset = 0; offset < rm->flashSize; offset += rm->sectorSize) {
            if (!isProtected(rm, offset)) {
                memset(rm->romData + offset, 0xff, rm->sectorSize);
                markWritten(rm, offset, rm->sectorSize);
            }
        }
    }
    return 0;
}

static int checkCommandProgram(AmdFlash* rm) 
{
    if (rm->cmdIdx > 0 && (cmdAddrBits(rm, rm->cmd[0].address) != rm->cmdAddr1 || rm->cmd[0].value != 0xaa)) return 0;
    if (rm->cmdIdx > 1 && (cmdAddrBits(rm, rm->cmd[1].address) != rm->cmdAddr2 || rm->cmd[1].value != 0x55)) return 0;
    if (rm->cmdIdx > 2 && (cmdAddrBits(rm, rm->cmd[2].address) != rm->cmdAddr1 || rm->cmd[2].value != 0xa0)) return 0;

    if (rm->cmdIdx < 4) return 1;

    if (!isProtected(rm, rm->cmd[3].address)) {
        UInt32 a = rm->cmd[3].address & (rm->flashSize - 1);
        rm->romData[a] &= rm->cmd[3].value;
        markWritten(rm, a, 1);
    }
    return 0;
}

/* Quadruple-Byte fast Program: 0x56 at the first command address, no unlock,
** then 4 data writes. Elsewhere 0x56 is plain data, such as a bank number.
** Only when the mapper opts in via amdFlashEnableFastCommands. */
static int checkCommandQuadrupleByteProgram(AmdFlash* rm)
{
    if (!rm->fastCommands) return 0;
    if (rm->cmdIdx > 0 && (cmdAddrBits(rm, rm->cmd[0].address) != rm->cmdAddr1 || rm->cmd[0].value != 0x56)) return 0;
    if (rm->cmdIdx < 5) return 1;

    {
        int i;
        for (i = 1; i <= 4; i++) {
            UInt32 a = rm->cmd[i].address & (rm->flashSize - 1);
            if (!isProtected(rm, a)) {
                rm->romData[a] &= rm->cmd[i].value;
                markWritten(rm, a, 1);
            }
        }
    }
    return 0;
}

static int checkCommandManifacturer(AmdFlash* rm) 
{
    if (rm->cmdIdx > 0 && (cmdAddrBits(rm, rm->cmd[0].address) != rm->cmdAddr1 || rm->cmd[0].value != 0xaa)) return 0;
    if (rm->cmdIdx > 1 && (cmdAddrBits(rm, rm->cmd[1].address) != rm->cmdAddr2 || rm->cmd[1].value != 0x55)) return 0;
    if (rm->cmdIdx > 2 && (cmdAddrBits(rm, rm->cmd[2].address) != rm->cmdAddr1 || rm->cmd[2].value != 0x90)) return 0;

    if (rm->cmdIdx == 3) {
        rm->state = ST_IDENT;
    }
    if (rm->cmdIdx < 4) return 1;

    return 0;
}

/* JEDEC CFI Query entry: write 0x98 to word address 0x55 (byte 0xAA in
** x8 mode).  Without the address check any 0x98 data write (e.g. an
** ASCII16-X bank select) would flip reads into CFI query mode. */
static int checkCommandCfi(AmdFlash* rm)
{
    if (rm->isX8X16 && rm->cmdIdx == 1 && rm->cmd[0].value == 0x98 &&
        (cmdAddrBits(rm, rm->cmd[0].address) & 0xFF) == 0x55) {
        rm->state = ST_CFI;
        rm->cmdIdx = 0;
        return 1;
    }
    return 0;
}

/* Write Buffer Program: aa/55, SA/25, SA/(N-1), N data bytes within one
** buffer page of one sector, then SA/29 confirm.  S29GL064S accepts up
** to 256-byte pages per WBP; smaller batches (e.g. 32 bytes) still work
** because the tool controls the count. */
static int checkCommandBufferProgram(AmdFlash* rm)
{
    const UInt32 pageMask = AMDFLASH_WBP_PAGE_BYTES - 1;
    UInt32 sectorMask;
    UInt32 sectorOfSetup;
    UInt32 pageBase;
    int    dataCount;
    int    i;

    if (rm->cmdIdx > 0 && (cmdAddrBits(rm, rm->cmd[0].address) != rm->cmdAddr1 || rm->cmd[0].value != 0xaa)) return 0;
    if (rm->cmdIdx > 1 && (cmdAddrBits(rm, rm->cmd[1].address) != rm->cmdAddr2 || rm->cmd[1].value != 0x55)) return 0;
    if (rm->cmdIdx > 2 && rm->cmd[2].value != 0x25) return 0;
    if (rm->cmdIdx < 4) return 1;

    sectorMask    = ~((UInt32)rm->sectorSize - 1);
    sectorOfSetup = rm->cmd[2].address & sectorMask;
    dataCount     = rm->cmd[3].value + 1;

    if (dataCount > AMDFLASH_WBP_PAGE_BYTES) return 0;
    if ((rm->cmd[3].address & sectorMask) != sectorOfSetup) return 0;
    if (rm->cmdIdx < 5) return 1;

    if ((rm->cmd[4].address & sectorMask) != sectorOfSetup) return 0;
    pageBase = rm->cmd[4].address & ~pageMask;

    if (rm->cmdIdx <= 4 + dataCount) {
        /* Still filling data buffer; each byte must stay in the same buffer page. */
        UInt32 lastAddr = rm->cmd[rm->cmdIdx - 1].address;
        if ((lastAddr & ~pageMask) != pageBase) return 0;
        return 1;
    }

    /* Confirm cycle: cmd[4+dataCount] value must be 0x29 within the setup sector. */
    if (rm->cmdIdx > 5 + dataCount) return 0;
    if (rm->cmd[rm->cmdIdx - 1].value != 0x29) return 0;
    if ((rm->cmd[rm->cmdIdx - 1].address & sectorMask) != sectorOfSetup) return 0;

    for (i = 4; i < 4 + dataCount; i++) {
        UInt32 addr = rm->cmd[i].address & (rm->flashSize - 1);
        if (!isProtected(rm, addr)) {
            rm->romData[addr] &= rm->cmd[i].value;
            markWritten(rm, addr, 1);
        }
    }
    return 0;
}

UInt8 amdFlashRead(AmdFlash* rm, UInt32 address)
{
    if (rm->state == ST_IDENT || rm->state == ST_CFI) {
        rm->cmdIdx = 0;
        /* M29W128 autoselect in x8 mode: even/odd byte pairs return the
        ** word's low byte (20 20 7E 7E ... 21 21 01 01).  In CFI mode the
        ** IDs fill the first 20h bytes, the CFI structure follows. */
        if (rm->amdType == AMD_TYPE_3) {
            if (rm->state == ST_IDENT || (address & 0x7F) < 0x20) {
                switch ((address >> 1) & 0x0f) {
                case 0x00: return 0x20;      /* Manufacturer: ST/Micron */
                case 0x01: return 0x7e;      /* Device ID 1 */
                case 0x02:                   /* sector protect status */
                    return (UInt8)isProtected(rm, address);
                case 0x03: return 0x19;
                case 0x0e: return 0x21;      /* Device ID 2 */
                case 0x0f: return 0x01;      /* Device ID 3 */
                default:   return 0x00;
                }
            }
            /* ST_CFI, offset 20h+: fall through to the shared CFI query
            ** structure (parameterized on flashSize/sectorSize). */
        }
        /* Autoselect IDs (Cypress S29GL064).  MFR=01, DEV=7E, ext=10/00.
        ** Byte offsets 00/02/1C/1E per datasheet; also visible in CFI. */
        if (rm->isX8X16) {
            switch (address & 0x7F) {
            case 0x00: return 0x01;
            case 0x02: return 0x7E;
            case 0x04: return (UInt8)isProtected(rm, address);
            case 0x06: return 0x08;
            case 0x1C: return 0x10;
            case 0x1E: return 0x00;
            }
            if (rm->state == ST_CFI) {
                /* S29GL064 CFI Query Structure (x8 byte addresses).  Odd
                ** byte offsets = high byte of x16 word = 0x00. */
                switch (address & 0x7F) {
                case 0x20: return 'Q';
                case 0x22: return 'R';
                case 0x24: return 'Y';
                case 0x26: return 0x02;      /* AMD/Spansion command set */
                case 0x28: return 0x00;
                case 0x2A: return 0x40;      /* Extended query offset */
                case 0x2C: return 0x00;
                case 0x2E: return 0x00;      /* Alt cmd set (none) */
                case 0x30: return 0x00;
                case 0x32: return 0x00;      /* Alt ext query offset (none) */
                case 0x34: return 0x00;
                case 0x36: return 0x27;      /* Vcc min 2.7 V */
                case 0x38: return 0x36;      /* Vcc max 3.6 V */
                case 0x3A: return 0x00;      /* Vpp min */
                case 0x3C: return 0x00;      /* Vpp max */
                case 0x3E: return 0x03;      /* typ single-byte prog time (log2 us) */
                case 0x40: return 0x05;      /* typ buffer prog time */
                case 0x42: return 0x0A;      /* typ block erase time (log2 ms) */
                case 0x44: return 0x0F;      /* typ chip erase time */
                case 0x46: return 0x02;      /* max prog timeout (2^N x typ) */
                case 0x48: return 0x01;      /* max buffer prog timeout */
                case 0x4A: return 0x03;      /* max block erase timeout */
                case 0x4C: return 0x02;      /* max chip erase timeout */
                case 0x4E: {                 /* device size = 2^N bytes */
                    int n = 0, s = rm->flashSize;
                    while (s > 1) { s >>= 1; n++; }
                    return (UInt8)n;
                }
                case 0x50: return 0x02;      /* interface: x8/x16 async */
                case 0x52: return 0x00;
                case 0x54: return 0x08;      /* max write buffer = 2^8 = 256 bytes (S29GL064S) */
                case 0x56: return 0x00;
                case 0x58: return 0x01;      /* one uniform erase region */
                case 0x5A: return (UInt8)((rm->flashSize / rm->sectorSize - 1) & 0xFF);
                case 0x5C: return (UInt8)(((rm->flashSize / rm->sectorSize - 1) >> 8) & 0xFF);
                case 0x5E: return (UInt8)((rm->sectorSize / 256) & 0xFF);
                case 0x60: return (UInt8)(((rm->sectorSize / 256) >> 8) & 0xFF);
                }
            }
            return 0x00;
        }
        switch (address & 0x03) {
        case 0: 
            return 0x01;
        case 1: 
            return 0xa4;
        case 2:
            return (UInt8)isProtected(rm, address);
        case 3:
            return 0x01;
        }
        return 0xff;
    }
//    printf("R %.4x: %.2x\n", address, rm->romData[address & (rm->flashSize - 1)]);

    address &= rm->flashSize - 1;

    return rm->romData[address];
}

void amdFlashWrite(AmdFlash* rm, UInt32 address, UInt8 value)
{
    if (rm->cmdIdx < sizeof(rm->cmd) / sizeof(rm->cmd[0])) {
        int stateValid = 0;

        rm->cmd[rm->cmdIdx].address = address;
        rm->cmd[rm->cmdIdx].value   = value;
        rm->cmdIdx++;
        stateValid |= checkCommandManifacturer(rm);
        stateValid |= checkCommandEraseSector(rm);
        stateValid |= checkCommandProgram(rm);
        stateValid |= checkCommandQuadrupleByteProgram(rm);
        stateValid |= checkCommandEraseChip(rm);
        stateValid |= checkCommandCfi(rm);
        stateValid |= checkCommandBufferProgram(rm);

        if (!stateValid) {
            rm->state = ST_IDLE;
            rm->cmdIdx = 0;
        }
    }
}

UInt8* amdFlashGetPage(AmdFlash* rm, UInt32 address)
{
    address &= rm->flashSize - 1;
    return rm->romData + address;
}

int amdFlashCmdInProgress(AmdFlash* rm)
{
    return rm->cmdIdx != 0;
}

void amdFlashEnableFastCommands(AmdFlash* rm)
{
    rm->fastCommands = 1;
}

void amdFlashReset(AmdFlash* rm)
{
    rm->cmdIdx = 0;
    rm->state = ST_IDLE;
}

void amdFlashSaveState(AmdFlash* rm)
{
    SaveState* state = saveStateOpenForWrite("amdFlash");
    int i;

    for (i = 0; i < AMDFLASH_CMD_SLOTS; i++) {
        char buf[32];
        sprintf(buf, "cmd_%d_address", i);
        saveStateSet(state, buf,   rm->cmd[i].address);
        sprintf(buf, "cmd_%d_value", i);
        saveStateSet(state, buf,   rm->cmd[i].value);
    }

    saveStateSet(state, "cmdIdx",   rm->cmdIdx);
    saveStateSet(state, "state",    rm->state);

    saveStateClose(state);
}

void amdFlashLoadState(AmdFlash* rm)
{
    SaveState* state = saveStateOpenForRead("amdFlash");
    int i;

    for (i = 0; i < AMDFLASH_CMD_SLOTS; i++) {
        char buf[32];
        sprintf(buf, "cmd_%d_address", i);
        rm->cmd[i].address = saveStateGet(state, buf,   0);
        sprintf(buf, "cmd_%d_value", i);
        rm->cmd[i].value = (UInt8)saveStateGet(state, buf,   0);
    }

    rm->cmdIdx = saveStateGet(state, "cmdIdx", 0);
    /* Older save states have no "state" field; ST_IDLE (0) matches the
    ** behavior they were saved with. */
    rm->state = saveStateGet(state, "state", ST_IDLE);

    saveStateClose(state);
}

void amdFlashSetProblemHandler(void (*handler)(int problem, const char* fileName))
{
    problemHandler = handler;
}

static void reportProblem(AmdFlash* rm, int problem, const char* fileName)
{
    rm->persistMode = PM_NONE;
    if (problemHandler != NULL) {
        problemHandler(problem, fileName);
    }
}

static UInt32 getLe32(const UInt8* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((UInt32)p[3] << 24);
}

static void putLe32(UInt8* p, UInt32 v)
{
    p[0] = (UInt8)v;
    p[1] = (UInt8)(v >> 8);
    p[2] = (UInt8)(v >> 16);
    p[3] = (UInt8)(v >> 24);
}

/* Little endian: magic, version 1, flash size, block size, block count, then
** per block with any written byte: block number, written mask, contents.
** Every field is checked before anything is applied. */
static int flashFileApply(AmdFlash* rm, const UInt8* buf, int length)
{
    UInt32 blocks = rm->flashSize / FLASHFILE_BLOCK;
    UInt32 count;
    UInt32 i;
    UInt32 j;
    const UInt8* rec;

    if (length < FLASHFILE_HEADER || memcmp(buf, FLASHFILE_MAGIC, 8) != 0) {
        return 0;
    }
    count = getLe32(buf + 20);
    if (getLe32(buf + 8) != 1 || getLe32(buf + 12) != (UInt32)rm->flashSize ||
        getLe32(buf + 16) != FLASHFILE_BLOCK || count > blocks ||
        (UInt32)length != FLASHFILE_HEADER + count * FLASHFILE_RECORD) {
        return 0;
    }
    for (i = 0, rec = buf + FLASHFILE_HEADER; i < count; i++, rec += FLASHFILE_RECORD) {
        if (getLe32(rec) >= blocks || (i > 0 && getLe32(rec) <= getLe32(rec - FLASHFILE_RECORD))) {
            return 0;
        }
    }

    /* Bytes in write-protected sectors are dropped: the MSX cannot have
    ** written them on this cartridge. */
    for (i = 0, rec = buf + FLASHFILE_HEADER; i < count; i++, rec += FLASHFILE_RECORD) {
        UInt32 base = getLe32(rec) * FLASHFILE_BLOCK;
        const UInt8* mask = rec + 4;
        const UInt8* data = rec + 4 + FLASHFILE_MASK;

        for (j = 0; j < FLASHFILE_BLOCK; j++) {
            if ((mask[j >> 3] & (1 << (j & 7))) && !isProtected(rm, base + j)) {
                rm->romData[base + j] = data[j];
                rm->writtenMask[(base + j) >> 3] |= (UInt8)(1 << (j & 7));
            }
        }
    }
    return 1;
}

static void flashFileSave(AmdFlash* rm)
{
    UInt32 blocks = rm->flashSize / FLASHFILE_BLOCK;
    UInt32 count = 0;
    UInt32 b;
    UInt8* buf = (UInt8*)malloc(FLASHFILE_HEADER + blocks * FLASHFILE_RECORD);
    UInt8* rec;

    if (buf == NULL) {
        return;
    }
    rec = buf + FLASHFILE_HEADER;
    for (b = 0; b < blocks; b++) {
        const UInt8* mask = rm->writtenMask + b * FLASHFILE_MASK;
        int k;

        for (k = 0; k < FLASHFILE_MASK && mask[k] == 0; k++) {
        }
        if (k == FLASHFILE_MASK) {
            continue;
        }
        putLe32(rec, b);
        memcpy(rec + 4, mask, FLASHFILE_MASK);
        memcpy(rec + 4 + FLASHFILE_MASK, rm->romData + b * FLASHFILE_BLOCK, FLASHFILE_BLOCK);
        rec += FLASHFILE_RECORD;
        count++;
    }
    memcpy(buf, FLASHFILE_MAGIC, 8);
    putLe32(buf + 8, 1);
    putLe32(buf + 12, rm->flashSize);
    putLe32(buf + 16, FLASHFILE_BLOCK);
    putLe32(buf + 20, count);
    sramSaveReplace(rm->flashFilename, buf, (int)(rec - buf));
    free(buf);
}

/* A .flash file wins; without one, a .sram of exactly the flash size is an
** image from before .flash files and keeps being used whole. Anything that
** cannot be used is reported and left untouched, and nothing is saved. */
static void loadWritten(AmdFlash* rm)
{
    size_t n = strlen(rm->sramFilename);
    int fileSize;
    int sramSize;
    int status;

    strcpy(rm->flashFilename, rm->sramFilename);
    if (n >= 5 && strcmp(rm->flashFilename + n - 5, ".sram") == 0) {
        n -= 5;
    }
    strcpy(rm->flashFilename + n, ".flash");

    status = sramFileStatus(rm->flashFilename, &fileSize);
    if (status == SRAM_FILE_ERROR) {
        reportProblem(rm, AMDFLASH_PROBLEM_UNREADABLE, rm->flashFilename);
        return;
    }
    if (status == SRAM_FILE_MISSING) {
        status = sramFileStatus(rm->sramFilename, &sramSize);
        if (status == SRAM_FILE_ERROR) {
            reportProblem(rm, AMDFLASH_PROBLEM_UNREADABLE, rm->sramFilename);
            return;
        }
        if (status == SRAM_FILE_PRESENT) {
            int length;
            UInt8* buf;

            if (sramSize != rm->flashSize) {
                reportProblem(rm, AMDFLASH_PROBLEM_SRAM_SIZE, rm->sramFilename);
                return;
            }
            /* Not sramLoad: it cannot report a failed read, and the whole
            ** image would then be saved over with the ROM at exit. */
            buf = sramLoadFile(rm->sramFilename, &length);
            if (buf == NULL) {
                reportProblem(rm, AMDFLASH_PROBLEM_UNREADABLE, rm->sramFilename);
                return;
            }
            if (length != rm->flashSize) {
                free(buf);
                reportProblem(rm, AMDFLASH_PROBLEM_SRAM_SIZE, rm->sramFilename);
                return;
            }
            memcpy(rm->romData, buf, length);
            free(buf);
            rm->persistMode = PM_FULL;
            return;
        }
    }

    rm->writtenMask = (UInt8*)calloc(rm->flashSize / 8, 1);
    if (rm->writtenMask == NULL) {
        rm->persistMode = PM_NONE;
        return;
    }
    rm->persistMode = PM_OVERLAY;
    if (status == SRAM_FILE_PRESENT) {
        int length;
        UInt8* buf = sramLoadFile(rm->flashFilename, &length);

        if (buf == NULL) {
            reportProblem(rm, AMDFLASH_PROBLEM_UNREADABLE, rm->flashFilename);
        }
        else if (!flashFileApply(rm, buf, length)) {
            reportProblem(rm, AMDFLASH_PROBLEM_FLASH_INVALID, rm->flashFilename);
        }
        free(buf);
    }
}

AmdFlash* amdFlashCreate(AmdType type, int flashSize, int sectorSize, UInt32 writeProtectMask, void* romData, int size, const char* sramFilename, int persistMode)
{
    AmdFlash* rm = (AmdFlash*)calloc(1, sizeof(AmdFlash));

    rm->writeProtectMask = writeProtectMask;
    rm->amdType = type;

    if (type == AMD_TYPE_1) {
        rm->cmdAddr1 = 0xaaa;
        rm->cmdAddr2 = 0x555;
    }
    else {
        /* AMD_TYPE_2 and AMD_TYPE_3: native word command addresses. */
        rm->cmdAddr1 = 0x555;
        rm->cmdAddr2 = 0x2aa;
    }

    /* 8 MB image size selects the x8/x16 dual-mode part MFR SCC+ SD
    ** ships with; MSX wiring runs it in x8 mode so command and ID
    ** addresses are word-shifted relative to the byte address the Z80
    ** puts on the bus.  AMD_TYPE_3 (M29W128) is an x8/x16 part as well. */
    rm->isX8X16 = (flashSize == 0x800000) || (type == AMD_TYPE_3);

    if (sramFilename != NULL) {
        strcpy(rm->sramFilename, sramFilename);
    }

    rm->flashSize = flashSize;
    rm->sectorSize = sectorSize;

    rm->romData = malloc(flashSize);
    if (size >= flashSize) {
        size = flashSize;
    }

    /* Seed: blank -> source ROM -> what the MSX wrote in earlier sessions. */
    memset(rm->romData, 0xff, flashSize);

    if (size > 0) {
        memcpy(rm->romData, romData, size);
    }

    rm->persistMode = PM_FULL;
    if (rm->sramFilename[0]) {
        if (persistMode == AMDFLASH_PERSIST_OVERLAY) {
            loadWritten(rm);
        }
        else {
            sramLoad(rm->sramFilename, rm->romData, rm->flashSize, NULL, 0);
        }
    }

    return rm;
}

void amdFlashDestroy(AmdFlash* rm)
{
    if (rm->sramFilename[0] && rm->persistMode == PM_FULL) {
        sramSave(rm->sramFilename, rm->romData, rm->flashSize, NULL, 0);
    }
    if (rm->persistMode == PM_OVERLAY && rm->wroteThisSession) {
        flashFileSave(rm);
    }
    free(rm->writtenMask);
    free(rm->romData);
    free(rm);
}
