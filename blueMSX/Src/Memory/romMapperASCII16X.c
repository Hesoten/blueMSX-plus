/*****************************************************************************
**
** ASCII16-X flashrom mapper.
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
/* Reference: https://www.grauw.nl/projects/ascii-x/ascii16-x/ */
#include "romMapperASCII16X.h"
#include "AmdFlash.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "sramLoader.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* S29GL064S (64 Mb, 8 MB) — reuses the shared x8/x16 flash path; MFR
** and DEV IDs returned by AmdFlash match the S29GL064 datasheet. */
#define ASCII16X_FLASH_SIZE     0x800000
#define ASCII16X_FLASH_SECTOR   0x10000

typedef struct {
    int       deviceHandle;
    AmdFlash* flash;
    int       slot;
    int       sslot;
    int       startPage;
    UInt32    romMask;      /* (FLASH_SIZE / 0x4000) - 1 = 0x1FF */
    UInt16    bankReg[2];   /* 12-bit each: bits[11:8] from address, [7:0] from data */
} RomMapperASCII16X;

static void mapBank(RomMapperASCII16X* rm, int bank)
{
    UInt32 bankNum = rm->bankReg[bank] & rm->romMask;
    UInt8* bankData = amdFlashGetPage(rm->flash, bankNum * 0x4000);
    int page = rm->startPage + bank * 2;
    /* readEnable=0 so reads route through the read callback: this preserves
    ** ident-mode responses when the flash chip is queried by flasher tools. */
    slotMapPage(rm->slot, rm->sslot, page,     bankData,          0, 0);
    slotMapPage(rm->slot, rm->sslot, page + 1, bankData + 0x2000, 0, 0);
}

static void destroy(RomMapperASCII16X* rm)
{
    amdFlashDestroy(rm->flash);
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);
    free(rm);
}

static void reset(RomMapperASCII16X* rm)
{
    amdFlashReset(rm->flash);
    rm->bankReg[0] = 0;
    rm->bankReg[1] = 0;
    mapBank(rm, 0);
    mapBank(rm, 1);
}

static void saveState(RomMapperASCII16X* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperASCII16X");
    saveStateSet(state, "bankReg0", rm->bankReg[0]);
    saveStateSet(state, "bankReg1", rm->bankReg[1]);
    saveStateClose(state);
    amdFlashSaveState(rm->flash);
}

static void loadState(RomMapperASCII16X* rm)
{
    SaveState* state = saveStateOpenForRead("mapperASCII16X");
    rm->bankReg[0] = (UInt16)saveStateGet(state, "bankReg0", 0);
    rm->bankReg[1] = (UInt16)saveStateGet(state, "bankReg1", 0);
    saveStateClose(state);
    amdFlashLoadState(rm->flash);
    mapBank(rm, 0);
    mapBank(rm, 1);
}

static UInt32 addrToFlash(RomMapperASCII16X* rm, UInt16 msxAddr)
{
    int bank = (msxAddr >= 0x8000) ? 1 : 0;
    UInt32 bankNum = rm->bankReg[bank] & rm->romMask;
    return bankNum * 0x4000 + (msxAddr & 0x3FFF);
}

static UInt8 read(RomMapperASCII16X* rm, UInt16 address)
{
    /* Callback address is startPage-relative (startPage=2), so add 0x4000
    ** to reach MSX absolute 0x4000-0xBFFF. */
    UInt16 msxAddr = address + 0x4000;
    return amdFlashRead(rm->flash, addrToFlash(rm, msxAddr));
}

static UInt8 peek(RomMapperASCII16X* rm, UInt16 address)
{
    /* Side-effect-free: bypass amdFlashRead's ident-state cmd-buffer reset by
    ** reading directly from the flash's internal ROM buffer. */
    UInt16 msxAddr = address + 0x4000;
    UInt8* p = amdFlashGetPage(rm->flash, addrToFlash(rm, msxAddr));
    return *p;
}

static void write(RomMapperASCII16X* rm, UInt16 address, UInt8 value)
{
    UInt16 msxAddr = address + 0x4000;
    int bank;
    UInt16 newVal;

    /* Every write in the ROM range is also a candidate flash-command write. */
    amdFlashWrite(rm->flash, addrToFlash(rm, msxAddr), value);

    /* Register write range: addr[13]=1 (i.e. 2000/3000, 6000/7000, A000/B000,
    ** E000/F000).  We only see 6000-7FFF and A000-BFFF because startPage=2
    ** does not register pages 0-1 or 6-7 with the slot manager. */
    if ((msxAddr & 0x2000) == 0) {
        return;
    }
    bank = (msxAddr & 0x1000) ? 1 : 0;

    /* Compose 12-bit bank: addr bits A11..A8 form the high nibble, data
    ** forms the low byte.  addr bits are already at position [11:8]. */
    newVal = (UInt16)((msxAddr & 0x0F00) | value);

    if (rm->bankReg[bank] != newVal) {
        rm->bankReg[bank] = newVal;
        mapBank(rm, bank);
    }
}

int romMapperASCII16XCreate(const char* filename, UInt8* romData,
                            int size, int slot, int sslot, int startPage)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperASCII16X* rm;
    int i;

    if (size < 0) {
        return 0;
    }
    if (size > ASCII16X_FLASH_SIZE) {
        size = ASCII16X_FLASH_SIZE;
    }

    rm = calloc(1, sizeof(RomMapperASCII16X));
    rm->deviceHandle = deviceManagerRegister(ROM_ASCII16X, &callbacks, rm);
    slotRegister(slot, sslot, startPage, 4, read, peek, write, destroy, rm);

    rm->flash = amdFlashCreate(AMD_TYPE_2, ASCII16X_FLASH_SIZE, ASCII16X_FLASH_SECTOR,
                               0, romData, size,
                               sramCreateFilenameWithSuffix(filename, "", ".sram"), 1);

    rm->romMask = ASCII16X_FLASH_SIZE / 0x4000 - 1;  /* 0x1FF for 8 MB */
    rm->slot  = slot;
    rm->sslot = sslot;
    rm->startPage = startPage;
    rm->bankReg[0] = 0;
    rm->bankReg[1] = 0;

    for (i = 0; i < 2; i++) {
        mapBank(rm, i);
    }

    return 1;
}
