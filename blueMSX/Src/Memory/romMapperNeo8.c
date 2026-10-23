/*****************************************************************************
**
** NEO-8 megarom mapper.
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
#include "romMapperNeo8.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "MapperDebugBanks.h"
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
    UInt32 romMask;         /* (romSize / 0x2000) - 1 */
    UInt16 blockReg[6];
    MapperBanks banks;
} RomMapperNeo8;

static void mapBank(RomMapperNeo8* rm, int region)
{
    UInt32 bank = rm->blockReg[region] & rm->romMask;
    UInt8* bankData = rm->romData + (UInt32)bank * 0x2000;
    slotMapPage(rm->slot, rm->sslot, rm->startPage + region, bankData, 1, 0);
    /* The masked value: what the slot map reports, not what the guest wrote. */
    slotSetBankSegment(rm->slot, rm->sslot, rm->startPage + region, (int)bank);
}

static int getBank(void* ref, int reg)
{
    RomMapperNeo8* rm = (RomMapperNeo8*)ref;

    return (int)(rm->blockReg[reg] & rm->romMask);
}

/* Both bytes: a later program write of one keeps the other, as on the hardware. */
static void setBank(void* ref, int reg, int segment)
{
    RomMapperNeo8* rm = (RomMapperNeo8*)ref;

    rm->blockReg[reg] = (UInt16)segment;
    mapBank(rm, reg);
}

static const MapperBanksType bankType = { 6, 1, 16384, 0, getBank, setBank, NULL, NULL };

static void destroy(RomMapperNeo8* rm)
{
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);
    free(rm->romData);
    free(rm);
}

static void saveState(RomMapperNeo8* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperNeo8");
    char tag[16];
    int i;

    for (i = 0; i < 6; i++) {
        sprintf(tag, "blockReg%d", i);
        saveStateSet(state, tag, rm->blockReg[i]);
    }
    saveStateClose(state);
}

static void loadState(RomMapperNeo8* rm)
{
    SaveState* state = saveStateOpenForRead("mapperNeo8");
    char tag[16];
    int i;

    for (i = 0; i < 6; i++) {
        sprintf(tag, "blockReg%d", i);
        rm->blockReg[i] = (UInt16)saveStateGet(state, tag, 0);
    }
    saveStateClose(state);

    for (i = 0; i < 6; i++) {
        mapBank(rm, i);
    }
    mapperBanksSync(&rm->banks);
}

static void write(RomMapperNeo8* rm, UInt16 address, UInt8 value)
{
    unsigned bbb;
    unsigned region;
    UInt16 prev;

    /* Callback address is relative to startPage (=0 for NEO), so it is
    ** the MSX absolute address here.  Register writes decode as
    ** (address >> 11) & 7; values 2-7 select region 0-5. */
    bbb = (address >> 11) & 0x7;
    if (bbb < 2) {
        return;
    }
    region = bbb - 2;

    prev = rm->blockReg[region];
    if ((address & 1) == 0) {
        rm->blockReg[region] = (UInt16)((prev & 0xFF00) | value);
    } else {
        rm->blockReg[region] = (UInt16)((prev & 0x00FF) | ((value & 0x3F) << 8));
    }

    if (rm->blockReg[region] != prev) {
        mapBank(rm, region);
    }
    mapperBanksProgramWrote(&rm->banks, region);
}

int romMapperNeo8Create(const char* filename, UInt8* romData,
                        int size, int slot, int sslot, int startPage)
{
    DeviceCallbacks callbacks = { destroy, NULL, saveState, loadState };
    RomMapperNeo8* rm;
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

    rm = calloc(1, sizeof(RomMapperNeo8));
    rm->deviceHandle = deviceManagerRegister(ROM_NEO8, &callbacks, rm);
    slotSetSegmentSize(slot, sslot, 0x2000);
    slotRegister(slot, sslot, startPage, 6, NULL, NULL, write, destroy, rm);

    rm->romData = calloc(1, size);
    memcpy(rm->romData, romData, origSize);
    rm->romMask = size / 0x2000 - 1;
    rm->slot  = slot;
    rm->sslot = sslot;
    rm->startPage = startPage;
    slotSetStorage(slot, sslot, rm->romData, size, 0, SLOT_STORAGE_ROM);

    for (i = 0; i < 6; i++) {
        rm->blockReg[i] = 0;
    }

    for (i = 0; i < 6; i++) {
        mapBank(rm, i);
    }
    mapperBanksInit(&rm->banks, &bankType, rm, slot, sslot, startPage);

    return 1;
}
