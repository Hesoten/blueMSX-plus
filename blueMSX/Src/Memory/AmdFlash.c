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
    int    writeProtectMask;
    int    fastCommands;        /* M29W640-family 0x56 quad program enabled */
    char   sramFilename[512];
};

/* On x8/x16 chips wired in x8 mode the internal command decoder inspects
** native word addresses; the byte address the host writes is right-shifted
** by one before matching against cmdAddr1/cmdAddr2. */
static UInt32 cmdAddrBits(AmdFlash* rm, UInt32 addr)
{
    if (rm->isX8X16) addr >>= 1;
    return addr & 0x7ff;
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

    if (((rm->writeProtectMask >> (rm->cmd[5].address / rm->sectorSize)) & 1) == 0) {
        memset(rm->romData + (rm->cmd[5].address & ~(rm->sectorSize - 1) & (rm->flashSize - 1)), 0xff, rm->sectorSize);
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

    memset(rm->romData, 0xff, rm->flashSize);
    return 0;
}

static int checkCommandProgram(AmdFlash* rm) 
{
    if (rm->cmdIdx > 0 && (cmdAddrBits(rm, rm->cmd[0].address) != rm->cmdAddr1 || rm->cmd[0].value != 0xaa)) return 0;
    if (rm->cmdIdx > 1 && (cmdAddrBits(rm, rm->cmd[1].address) != rm->cmdAddr2 || rm->cmd[1].value != 0x55)) return 0;
    if (rm->cmdIdx > 2 && (cmdAddrBits(rm, rm->cmd[2].address) != rm->cmdAddr1 || rm->cmd[2].value != 0xa0)) return 0;

    if (rm->cmdIdx < 4) return 1;

    if (((rm->writeProtectMask >> (rm->cmd[3].address / rm->sectorSize)) & 1) == 0) {
        rm->romData[rm->cmd[3].address & (rm->flashSize - 1)] &= rm->cmd[3].value;
    }
    return 0;
}

/* Quadruple-Byte fast Program (0x56, no unlock, then 4 data writes).
** M29W640-family only: recognized when the mapper opts in via
** amdFlashEnableFastCommands, else 0x56 stays plain data. */
static int checkCommandQuadrupleByteProgram(AmdFlash* rm)
{
    if (!rm->fastCommands) return 0;
    if (rm->cmdIdx > 0 && rm->cmd[0].value != 0x56) return 0;
    if (rm->cmdIdx < 5) return 1;

    {
        int i;
        for (i = 1; i <= 4; i++) {
            UInt32 a = rm->cmd[i].address & (rm->flashSize - 1);
            if (((rm->writeProtectMask >> (a / rm->sectorSize)) & 1) == 0) {
                rm->romData[a] &= rm->cmd[i].value;
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
        if (((rm->writeProtectMask >> (addr / rm->sectorSize)) & 1) == 0) {
            rm->romData[addr] &= rm->cmd[i].value;
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
                case 0x02: {                 /* sector protect status */
                    UInt32 sector = address / rm->sectorSize;
                    return sector < 32 ? (UInt8)((rm->writeProtectMask >> sector) & 1) : 0;
                }
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
            case 0x04: return (rm->writeProtectMask >> (address / rm->sectorSize)) & 1;
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
            return (rm->writeProtectMask >> (address / rm->sectorSize)) & 1;
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

AmdFlash* amdFlashCreate(AmdType type, int flashSize, int sectorSize, UInt32 writeProtectMask, void* romData, int size, const char* sramFilename, int loadSram)
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

    /* Seed: blank -> source ROM -> sram overlay.  Sram wins so MSX-side flash
    ** writes persist across sessions; delete bin/SRAM/*.sram to reset to the
    ** factory image.  Manbow2 relies on writeProtectMask instead of order. */
    memset(rm->romData, 0xff, flashSize);

    if (size > 0) {
        memcpy(rm->romData, romData, size);
    }

    if (rm->sramFilename[0]) {
        sramLoad(rm->sramFilename, rm->romData, rm->flashSize, NULL, 0);
    }
    (void)loadSram;

    return rm;
}

void amdFlashDestroy(AmdFlash* rm)
{
    if (rm->sramFilename[0]) {
        sramSave(rm->sramFilename, rm->romData, rm->flashSize, NULL, 0);
    }
    free(rm->romData);
    free(rm);
}
