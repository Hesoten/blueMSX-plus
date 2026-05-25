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

struct AmdFlash
{
    UInt8* romData;
    UInt32 cmdAddr1;
    UInt32 cmdAddr2;
    int    state;
    int    flashSize;
    int    sectorSize;
    int    isX8X16;             /* x16 chip in x8 mode: cmd-decode address is byte_addr >> 1 */
    AmdCmd cmd[8];
    int    cmdIdx;
    int    writeProtectMask;
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

/* Quadruple-Byte fast Program command (opcode 0x56, no unlock prefix,
** followed by 4 data byte writes).  MFR SCC+ SD's OPFXSD path relies
** on this to program 4 bytes at a time. */
static int checkCommandQuadrupleByteProgram(AmdFlash* rm)
{
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

UInt8 amdFlashRead(AmdFlash* rm, UInt32 address)
{
    if (rm->state == ST_IDENT) {
        rm->cmdIdx = 0;
//        printf("R %.4x: XX\n", address);
        /* Auto-select / device-id.  x8 mode: mfr @ 0x00, dev @ 0x01.
        ** x8-of-x16 mode: addresses are word-shifted per MFR SCC+ SD. */
        if (rm->isX8X16) {
            switch (address & 0x7F) {
            case 0x00: return 0x20;
            case 0x02: return 0x7E;
            case 0x04: return (rm->writeProtectMask >> (address / rm->sectorSize)) & 1;
            case 0x06: return 0x08;
            case 0x1C: return 0x10;
            case 0x1E: return 0x00;
            default:   return 0x00;
            }
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

//        { static int x = 0; if (++x < 220) printf("W %.4x: %.2x  %d\n", address, value, rm->cmdIdx);}

        rm->cmd[rm->cmdIdx].address = address;
        rm->cmd[rm->cmdIdx].value   = value;
        rm->cmdIdx++;
        stateValid |= checkCommandManifacturer(rm);
        stateValid |= checkCommandEraseSector(rm);
        stateValid |= checkCommandProgram(rm);
        stateValid |= checkCommandQuadrupleByteProgram(rm);
        stateValid |= checkCommandEraseChip(rm);
        /* 0xF0 resets only when stateValid=0; mid-sequence 0xF0 is data
        ** (e.g. QBP data phase) and must not drop the command buffer. */

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

void amdFlashReset(AmdFlash* rm)
{
    rm->cmdIdx = 0;
    rm->state = ST_IDLE;
}

void amdFlashSaveState(AmdFlash* rm)
{
    SaveState* state = saveStateOpenForWrite("amdFlash");
    int i;

    for (i = 0; i < 8; i++) {
        char buf[32];
        sprintf(buf, "cmd_%d_address", i);
        saveStateSet(state, buf,   rm->cmd[i].address);
        sprintf(buf, "cmd_%d_value", i);
        saveStateSet(state, buf,   rm->cmd[i].value);
    }

    saveStateSet(state, "cmdIdx",   rm->cmdIdx);

    saveStateClose(state);
}

void amdFlashLoadState(AmdFlash* rm)
{
    SaveState* state = saveStateOpenForRead("amdFlash");
    int i;

    for (i = 0; i < 8; i++) {
        char buf[32];
        sprintf(buf, "cmd_%d_address", i);
        rm->cmd[i].address = saveStateGet(state, buf,   0);
        sprintf(buf, "cmd_%d_value", i);
        rm->cmd[i].value = (UInt8)saveStateGet(state, buf,   0);
    }

    rm->cmdIdx = saveStateGet(state, "cmdIdx", 0);

    saveStateClose(state);
}

AmdFlash* amdFlashCreate(AmdType type, int flashSize, int sectorSize, UInt32 writeProtectMask, void* romData, int size, const char* sramFilename, int loadSram)
{
    AmdFlash* rm = (AmdFlash*)calloc(1, sizeof(AmdFlash));

    rm->writeProtectMask = writeProtectMask;

    if (type == 0) {
        rm->cmdAddr1 = 0xaaa;
        rm->cmdAddr2 = 0x555;
    }
    else {
        rm->cmdAddr1 = 0x555;
        rm->cmdAddr2 = 0x2aa;
    }

    /* 8 MB image size selects the x8/x16 dual-mode part MFR SCC+ SD
    ** ships with; MSX wiring runs it in x8 mode so command and ID
    ** addresses are word-shifted relative to the byte address the Z80
    ** puts on the bus. */
    rm->isX8X16 = (flashSize == 0x800000);

    if (sramFilename != NULL) {
        strcpy(rm->sramFilename, sramFilename);
    }

    rm->flashSize = flashSize;
    rm->sectorSize = sectorSize;

    rm->romData = malloc(flashSize);
    if (size >= flashSize) {
        size = flashSize;
    }

    /* Always start from a blank-flash baseline so callers can omit both
    ** the seed buffer and the SRAM filename without inheriting garbage. */
    memset(rm->romData, 0xff, flashSize);

    if (rm->sramFilename[0]) {
        sramLoad(rm->sramFilename, rm->romData, rm->flashSize, NULL, 0);
    }

    if (size > 0) {
        memcpy(rm->romData, romData, size);
    }
#if 0
    if (rm->sramFilename[0] && loadSram) {
        sramLoad(rm->sramFilename, rm->romData, rm->flashSize, NULL, 0);
    }
#endif

    return rm;
}

void amdFlashDestroy(AmdFlash* rm)
{
    if (rm->sramFilename[0]) {
        sramSave(rm->sramFilename, rm->romData, rm->flashSize, NULL, 0);
    }
    free(rm);
}
