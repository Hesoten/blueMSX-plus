/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/romMapperASCII16nf.c,v $
**
** $Revision: 1.6 $
**
** $Date: 2008-03-30 18:38:42 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
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
#include "romMapperASCII16nf.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "MapperDebugBanks.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//  Uses ASCII16 but in different adresses :
//  Know Game : Super Pierrot


typedef struct {
    int deviceHandle;
    UInt8* romData;
    int slot;
    int sslot;
    int startPage;
    UInt32 romMask;
    int romMapper[4];
    MapperBanks banks;
} RomMapperASCII16nf;

/* Both 8kB banks of a 16kB segment report the same segment number. */
static void mapBank(RomMapperASCII16nf* rm, int bank)
{
    UInt8* bankData = rm->romData + (rm->romMapper[bank] << 14);

    slotMapPage(rm->slot, rm->sslot, rm->startPage + bank,     bankData, 1, 0);
    slotMapPage(rm->slot, rm->sslot, rm->startPage + bank + 1, bankData + 0x2000, 1, 0);
    slotSetBankSegment(rm->slot, rm->sslot, rm->startPage + bank,     rm->romMapper[bank]);
    slotSetBankSegment(rm->slot, rm->sslot, rm->startPage + bank + 1, rm->romMapper[bank]);
}

static int getBank(void* ref, int reg)
{
    return ((RomMapperASCII16nf*)ref)->romMapper[2 * reg];
}

static void setBank(void* ref, int reg, int segment)
{
    RomMapperASCII16nf* rm = (RomMapperASCII16nf*)ref;

    rm->romMapper[2 * reg] = segment;
    mapBank(rm, 2 * reg);
}

/* A ROM whose size is no power of two leaves bits out of the mask: segments it never selects. */
static const char* neverSelected(void* ref, int segment)
{
    static char why[96];
    RomMapperASCII16nf* rm = (RomMapperASCII16nf*)ref;

    if ((segment & ~rm->romMask) == 0) {
        return NULL;
    }
    snprintf(why, sizeof(why), "the mapper keeps only the bits 0x%02X of a bank number",
             (unsigned)rm->romMask);
    return why;
}

static const MapperBanksType bankType = {
    2, 2, 256, 0, getBank, setBank, NULL, NULL, neverSelected
};

static void saveState(RomMapperASCII16nf* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperASCII16nf");
    char tag[16];
    int i;

    for (i = 0; i < 4; i++) {
        sprintf(tag, "romMapper%d", i);
        saveStateSet(state, tag, rm->romMapper[i]);
    }

    saveStateClose(state);
}

static void loadState(RomMapperASCII16nf* rm)
{
    SaveState* state = saveStateOpenForRead("mapperASCII16nf");
    char tag[16];
    int i;

    for (i = 0; i < 4; i++) {
        sprintf(tag, "romMapper%d", i);
        rm->romMapper[i] = saveStateGet(state, tag, 0);
    }

    saveStateClose(state);

    for (i = 0; i < 4; i += 2) {
        mapBank(rm, i);
    }
    mapperBanksSync(&rm->banks);
}

static void destroy(RomMapperASCII16nf* rm)
{
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);

    free(rm->romData);
    free(rm);
}

static void write(RomMapperASCII16nf* rm, UInt16 address, UInt8 value) 
{
    int bank;

    address += 0x4000;

    if (address & 0x0800) {
        return;
    }

    bank = (address & 0x1000) >> 11;

    value &= rm->romMask;

    if (rm->romMapper[bank] != value) {
        rm->romMapper[bank] = value;
        mapBank(rm, bank);
    }
    mapperBanksProgramWrote(&rm->banks, bank / 2);
}

int romMapperASCII16nfCreate(const char* filename, UInt8* romData, 
                             int size, int slot, int sslot, int startPage) 
{
    DeviceCallbacks callbacks = { destroy, NULL, saveState, loadState };
    RomMapperASCII16nf* rm;
    int i;

    rm = malloc(sizeof(RomMapperASCII16nf));

    rm->deviceHandle = deviceManagerRegister(ROM_ASCII16NF, &callbacks, rm);
    slotSetSegmentSize(slot, sslot, 0x4000);
    slotRegister(slot, sslot, startPage, 4, NULL, NULL, write, destroy, rm);

    size = (size + 0x3fff) & ~0x3fff;

    rm->romData = malloc(size);
    memcpy(rm->romData, romData, size);
    rm->romMask = size / 0x4000 - 1;
    rm->slot  = slot;
    rm->sslot = sslot;
    rm->startPage  = startPage;
    slotSetStorage(slot, sslot, rm->romData, size, 0, SLOT_STORAGE_ROM);

    rm->romMapper[0] = 0;
    rm->romMapper[2] = 0;

    for (i = 0; i < 4; i += 2) {
        mapBank(rm, i);
    }
    mapperBanksInit(&rm->banks, &bankType, rm, slot, sslot, startPage);

    return 1;
}

