/*****************************************************************************
**
** Makoto: a YM2608 (OPNA) sound cartridge on I/O ports 14h-17h.
** Copyright (C) 2026 madscient
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
#include "romMapperMakoto.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "DebugDeviceManager.h"
#include "RomLoader.h"
#include "IoPort.h"
#include "YM2608.h"
#include "Board.h"
#include <stdlib.h>

#define MAKOTO_PORT_BASE     0x14
#define MAKOTO_CLOCK         8000000

/* 1-bit DRAM mode addresses 4-byte units with 16 bits, which is as far as
** the chip reaches in the mode the cartridge's drivers select. */
#define MAKOTO_ADPCM_RAM     0x40000

/* The YM2608's internal rhythm ROM cannot be distributed; it is read from
** here when the user has put it there, and the rhythm is silent otherwise. */
#define MAKOTO_RHYTHM_ROM    "Machines/Shared Roms/ym2608_rhythm.rom"

/* Whether /IRQ reaches the MSX /INT is not known, so it is left unwired. */
#define MAKOTO_IRQ_MASK      0

typedef struct {
    YM2608* ym2608;
    int     deviceHandle;
    int     debugHandle;
} RomMapperMakoto;

static void saveState(RomMapperMakoto* rm)
{
    ym2608SaveState(rm->ym2608);
}

static void loadState(RomMapperMakoto* rm)
{
    ym2608LoadState(rm->ym2608);
}

static void destroy(RomMapperMakoto* rm)
{
    int i;

    for (i = 0; i < 4; i++) {
        ioPortUnregister(MAKOTO_PORT_BASE + i, rm);
    }

    deviceManagerUnregister(rm->deviceHandle);
    debugDeviceUnregister(rm->debugHandle);
    ym2608Destroy(rm->ym2608);

    free(rm);
}

static void reset(RomMapperMakoto* rm)
{
    ym2608Reset(rm->ym2608);
}

static UInt8 read(RomMapperMakoto* rm, UInt16 ioPort)
{
    return ym2608Read(rm->ym2608, ioPort & 3);
}

static UInt8 peek(RomMapperMakoto* rm, UInt16 ioPort)
{
    return ym2608Peek(rm->ym2608, ioPort & 3);
}

static void write(RomMapperMakoto* rm, UInt16 ioPort, UInt8 value)
{
    ym2608Write(rm->ym2608, ioPort & 3, value);
}

static void getDebugInfo(RomMapperMakoto* rm, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts;
    int i;

    ym2608GetDebugInfo(rm->ym2608, dbgDevice);

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, "Makoto", 4);
    for (i = 0; i < 4; i++) {
        dbgIoPortsAddPort(ioPorts, i, MAKOTO_PORT_BASE + i, DBG_IO_READWRITE, peek(rm, MAKOTO_PORT_BASE + i));
    }
}

int romMapperMakotoCreate()
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    RomMapperMakoto* rm = malloc(sizeof(RomMapperMakoto));
    UInt8* rhythmRom;
    int rhythmRomSize = 0;
    int i;

    rm->deviceHandle = deviceManagerRegister(ROM_MAKOTO, &callbacks, rm);
    rm->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, "Makoto", &dbgCallbacks, rm);

    rhythmRom = romLoad(MAKOTO_RHYTHM_ROM, NULL, &rhythmRomSize);
    rm->ym2608 = ym2608Create(boardGetMixer(), MAKOTO_CLOCK, MAKOTO_ADPCM_RAM,
                              rhythmRom, rhythmRomSize, MAKOTO_IRQ_MASK);
    free(rhythmRom);

    for (i = 0; i < 4; i++) {
        ioPortRegister(MAKOTO_PORT_BASE + i, read, write, rm);
    }

    reset(rm);

    return 1;
}
