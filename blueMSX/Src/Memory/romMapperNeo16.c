/*****************************************************************************
**
** NEO-16 megarom mapper.
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
/* Reference: https://aoineko.org/msxgl/index.php?title=NEO_mapper (v1.2) */
#include "romMapperNeo16.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


typedef struct {
    int    deviceHandle;
    UInt8* romData;
    int    slot;
    int    sslot;
    int    startPage;
    UInt32 romMask;         /* (romSize / 0x4000) - 1 */
    UInt16 blockReg[3];
} RomMapperNeo16;

static void mapBank(RomMapperNeo16* rm, int region)
{
    UInt32 bank = rm->blockReg[region] & rm->romMask;
    UInt8* bankData = rm->romData + (UInt32)bank * 0x4000;
    int page = rm->startPage + region * 2;
    slotMapPage(rm->slot, rm->sslot, page,     bankData,          1, 0);
    slotMapPage(rm->slot, rm->sslot, page + 1, bankData + 0x2000, 1, 0);
}

static void destroy(RomMapperNeo16* rm)
{
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);
    free(rm->romData);
    free(rm);
}

static void saveState(RomMapperNeo16* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperNeo16");
    char tag[16];
    int i;

    for (i = 0; i < 3; i++) {
        sprintf(tag, "blockReg%d", i);
        saveStateSet(state, tag, rm->blockReg[i]);
    }
    saveStateClose(state);
}

static void loadState(RomMapperNeo16* rm)
{
    SaveState* state = saveStateOpenForRead("mapperNeo16");
    char tag[16];
    int i;

    for (i = 0; i < 3; i++) {
        sprintf(tag, "blockReg%d", i);
        rm->blockReg[i] = (UInt16)saveStateGet(state, tag, 0);
    }
    saveStateClose(state);

    for (i = 0; i < 3; i++) {
        mapBank(rm, i);
    }
}

static void write(RomMapperNeo16* rm, UInt16 address, UInt8 value)
{
    unsigned bbb;
    unsigned region;
    UInt16 prev;

    /* Callback address is relative to startPage (=0 for NEO), so it is
    ** the MSX absolute address here.  Register writes decode as
    ** (address >> 11) & 7 where even non-zero values select region 0..2. */
    bbb = (address >> 11) & 0x7;
    if ((bbb < 2) || (bbb & 1)) {
        return;
    }
    region = (bbb >> 1) - 1;

    prev = rm->blockReg[region];
    if ((address & 1) == 0) {
        rm->blockReg[region] = (UInt16)((prev & 0xFF00) | value);
    } else {
        /* Bits 14-15 reserved per spec.  Preserve only the 6 usable MSB
        ** bits so future 14-bit-segment ROMs work; the effective bank is
        ** further masked by romMask at map time. */
        rm->blockReg[region] = (UInt16)((prev & 0x00FF) | ((value & 0x3F) << 8));
    }

    if (rm->blockReg[region] != prev) {
        mapBank(rm, region);
    }
}

int romMapperNeo16Create(const char* filename, UInt8* romData,
                         int size, int slot, int sslot, int startPage)
{
    DeviceCallbacks callbacks = { destroy, NULL, saveState, loadState };
    RomMapperNeo16* rm;
    int i;
    int origSize;

    if (size <= 0) {
        return 0;
    }

    origSize = size;
    size = 0x8000;
    while (size < origSize) {
        size *= 2;
    }

    rm = calloc(1, sizeof(RomMapperNeo16));
    rm->deviceHandle = deviceManagerRegister(ROM_NEO16, &callbacks, rm);
    slotRegister(slot, sslot, startPage, 6, NULL, NULL, write, destroy, rm);

    rm->romData = calloc(1, size);
    memcpy(rm->romData, romData, origSize);
    rm->romMask = size / 0x4000 - 1;
    rm->slot  = slot;
    rm->sslot = sslot;
    rm->startPage = startPage;

    for (i = 0; i < 3; i++) {
        rm->blockReg[i] = 0;
    }

    for (i = 0; i < 3; i++) {
        mapBank(rm, i);
    }

    return 1;
}
