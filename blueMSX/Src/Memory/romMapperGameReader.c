/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/romMapperGameReader.c,v $
**
** $Revision: 1.8 $
**
** $Date: 2008-03-30 18:38:44 $
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
#include "romMapperGameReader.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "SaveState.h"
#include "IoPort.h"
#include "GameReader.h"
#include "SCC.h"
#include "Board.h"
#include <stdlib.h>
#include <string.h>

#define CACHE_LINE_BITS 8
#define CACHE_LINES     (0x10000 >> CACHE_LINE_BITS)
#define CACHE_LINE_SIZE (1 << CACHE_LINE_BITS)

typedef struct {
    int deviceHandle;
    GrHandle* gameReader;
    int slot;
    int sslot;
    int cartSlot;
    int sccEnable;
    SCC* scc;
    int cacheLineEnabled[CACHE_LINES];
    UInt8 cacheLineData[CACHE_LINES][1 << CACHE_LINE_BITS];
} RomMapperGameReader;

static void saveState(RomMapperGameReader* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperGameReader");
    saveStateSet(state, "sccEnable", rm->sccEnable);
    saveStateClose(state);
    sccSaveState(rm->scc);
}

static void loadState(RomMapperGameReader* rm)
{
    SaveState* state = saveStateOpenForRead("mapperGameReader");
    rm->sccEnable = saveStateGet(state, "sccEnable", 0);
    saveStateClose(state);
    sccLoadState(rm->scc);
}

static void reset(RomMapperGameReader* rm)
{
    rm->sccEnable = 0;
    sccReset(rm->scc);
}

static void destroy(RomMapperGameReader* rm)
{
    gameReaderDestroy(rm->gameReader);
    ioPortUnregisterUnused(rm->cartSlot);
    slotUnregister(rm->slot, rm->sslot, 0);
    deviceManagerUnregister(rm->deviceHandle);
    sccDestroy(rm->scc);

    free(rm);
}

static UInt8 readIo(RomMapperGameReader* rm, UInt16 port)
{
    UInt8 value = 0xff;

    if ((port & 0xf8) == 0xb8 ||
        (port & 0xf8) == 0xd8 ||
        (port & 0xfc) == 0x80 ||
        (port & 0xf0) == 0xf0)
    {
        return 0xff;
    }

    if (!gameReaderReadIo(rm->gameReader, port, &value)) {
        return 0xff;
    }

    return value;
}

static void writeIo(RomMapperGameReader* rm, UInt16 port, UInt8 value)
{
    if ((port & 0xf8) == 0xb8 ||
        (port & 0xf8) == 0xd8 ||
        (port & 0xfc) == 0x80 ||
        (port & 0xf0) == 0xf0)
    {
        return;
    }
    gameReaderWriteIo(rm->gameReader, port, value);
}

static UInt8 read(RomMapperGameReader* rm, UInt16 address) 
{
    int bank = address >> CACHE_LINE_BITS;
    if (rm->sccEnable && address >= 0x9800 && address < 0xa000) {
        return sccRead(rm->scc, (UInt8)(address & 0xff));
    }
    if (!rm->cacheLineEnabled[bank]) {
        if (!gameReaderRead(rm->gameReader, bank << CACHE_LINE_BITS, rm->cacheLineData[bank], CACHE_LINE_SIZE)) {
            memset(rm->cacheLineData[bank], 0xff, CACHE_LINE_SIZE);
        }
        rm->cacheLineEnabled[bank] = 1;
    }

    return rm->cacheLineData[bank][address & (CACHE_LINE_SIZE - 1)];
}

static UInt8 peek(RomMapperGameReader* rm, UInt16 address)
{
    if (rm->sccEnable && address >= 0x9800 && address < 0xa000) {
        return sccPeek(rm->scc, (UInt8)(address & 0xff));
    }
    return read(rm, address);
}

static void write(RomMapperGameReader* rm, UInt16 address, UInt8 value)
{
    int i;

    if (address >= 0x9000 && address < 0x9800) {
        rm->sccEnable = (value & 0x3f) == 0x3f;
    }
    if (rm->sccEnable && address >= 0x9800 && address < 0xa000) {
        sccWrite(rm->scc, (UInt8)(address & 0xff), value);
        return;
    }

    /* The cartridge decides what a write means, so nothing cached survives it. */
    for (i = 0; i < CACHE_LINES; i++) {
        rm->cacheLineEnabled[i] = 0;
    }

    gameReaderWrite(rm->gameReader, address, &value, 1);
}

int romMapperGameReaderCreate(int cartSlot, int slot, int sslot) 
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperGameReader* rm;
    int i;

    rm = malloc(sizeof(RomMapperGameReader));

    rm->deviceHandle = deviceManagerRegister(ROM_GAMEREADER, &callbacks, rm);

    rm->slot     = slot;
    rm->sslot    = sslot;
    rm->cartSlot = cartSlot;
    rm->gameReader = gameReaderCreate();

    if (rm->gameReader == NULL) {
        /* Freed here, as the eject callback never runs with nothing
        ** registered. Still a success, or the machine would not boot. */
        deviceManagerUnregister(rm->deviceHandle);
        free(rm);
        return 1;
    }

    rm->sccEnable = 0;
    rm->scc = sccCreate(boardGetMixer());
    sccSetMode(rm->scc, SCC_REAL);

    for (i = 0; i < CACHE_LINES; i++) {
        rm->cacheLineEnabled[i] = 0;
    }

    ioPortRegisterUnused(cartSlot, readIo, writeIo, rm);
    slotRegister(slot, sslot, 0, 8, read, peek, write, destroy, rm);
    for (i = 0; i < 8; i++) {
        slotMapPage(rm->slot, rm->sslot, i, NULL, 0, 0);
    }

    return 1;
}
