/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/romMapperJoyrexPsg.c,v $
**
** $Revision: 1.1 $
**
** $Date: 2008-10-26 19:48:18 $
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
#include "romMapperJoyrexPsg.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "DebugDeviceManager.h"
#include "SaveState.h"
#include "SN76489.h"
#include "Board.h"
#include "IoPort.h"
#include "SlotManager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    SN76489* sn76489;
    int    deviceHandle;
    int    debugHandle;
    int    slot;
    int    sslot;
} RomMapperJoyrexPsg;

static void saveState(RomMapperJoyrexPsg* rm)
{
    sn76489SaveState(rm->sn76489);
}

static void loadState(RomMapperJoyrexPsg* rm)
{
    sn76489LoadState(rm->sn76489);
}

static void destroy(RomMapperJoyrexPsg* rm)
{
    if (rm->slot >= 0) {
        slotUnregister(rm->slot, rm->sslot, 0);
    }
    deviceManagerUnregister(rm->deviceHandle);
    debugDeviceUnregister(rm->debugHandle);
    sn76489Destroy(rm->sn76489);

    ioPortUnregister(0xf0, rm);

    free(rm);
}

static UInt8 peek(RomMapperJoyrexPsg* rm, UInt16 ioPort)
{
    return 0xff;
}

static void write(RomMapperJoyrexPsg* rm, UInt16 ioPort, UInt8 value)
{
    sn76489WriteData(rm->sn76489, ioPort, value);
}

static void reset(RomMapperJoyrexPsg* rm)
{
    sn76489Reset(rm->sn76489);
}


static void getDebugInfo(RomMapperJoyrexPsg* rm, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts;

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, "SN76489", 1);
    dbgIoPortsAddPort(ioPorts, 0, 0xf0, DBG_IO_READWRITE, peek(rm, 0xf0));
}

int romMapperJoyrexPsgCreate(int slot, int sslot)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    RomMapperJoyrexPsg* rm = malloc(sizeof(RomMapperJoyrexPsg));

    rm->slot  = slot;
    rm->sslot = sslot;

    rm->deviceHandle = deviceManagerRegister(ROM_JOYREXPSG, &callbacks, rm);
    rm->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, "SN76489", &dbgCallbacks, rm);

    rm->sn76489 = sn76489Create(boardGetMixer());

    ioPortRegister(0xf0, NULL, write, rm);

    reset(rm);

    if (slot >= 0) {
        slotRegister(slot, sslot, 0, 8, NULL, NULL, NULL, destroy, rm);
    }

    return 1;
}

