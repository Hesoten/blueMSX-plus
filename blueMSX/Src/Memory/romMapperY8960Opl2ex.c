/*****************************************************************************
**
** Y8960 cartridge - the OPL2EX block: YM3812 with ADPCM-B.
**
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
#include "romMapperY8960.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "SaveState.h"
#include "Board.h"
#include "IoPort.h"
#include "Y8960Opl2ex.h"
#include "AudioMixer.h"
#include <stdlib.h>

/* MSX-AUDIO's own addresses. Unlike the OPLL block this one reads back, so
** it registers a read as well. */
#define Y8960_OPL2_ADDRESS 0xC0
#define Y8960_OPL2_DATA    0xC1

typedef struct {
    int          deviceHandle;
    Y8960Opl2ex* chip;
} RomMapperY8960Opl2ex;

static RomMapperY8960Opl2ex* theOpl2ex = NULL;

static int portOf(UInt16 port)
{
    return port == Y8960_OPL2_DATA ? 1 : 0;
}

static void writeIo(RomMapperY8960Opl2ex* rm, UInt16 port, UInt8 value)
{
    if (!y8960IoEnabled(Y8960_IO_OPL2)) {
        return;
    }

    y8960Opl2exWrite(rm->chip, portOf(port), value);
}

static UInt8 readIo(RomMapperY8960Opl2ex* rm, UInt16 port)
{
    if (!y8960IoEnabled(Y8960_IO_OPL2)) {
        return 0xFF;
    }

    return y8960Opl2exRead(rm->chip, portOf(port));
}

/* The tunnel is not gated: the enablers live in the same window and could not
** be reached otherwise. */
static void tunnel(void* ref, int port, UInt8 value)
{
    RomMapperY8960Opl2ex* rm = (RomMapperY8960Opl2ex*)ref;

    y8960Opl2exWrite(rm->chip, port, value);
}

static void reset(RomMapperY8960Opl2ex* rm)
{
    y8960Opl2exReset(rm->chip);
}

static void saveState(RomMapperY8960Opl2ex* rm)
{
    y8960Opl2exSaveState(rm->chip);
}

static void loadState(RomMapperY8960Opl2ex* rm)
{
    y8960Opl2exLoadState(rm->chip);
}

static void destroy(RomMapperY8960Opl2ex* rm)
{
    y8960UnregisterTunnel(Y8960_TUN_OPL2, rm);

    ioPortUnregister(Y8960_OPL2_ADDRESS, rm);
    ioPortUnregister(Y8960_OPL2_DATA, rm);

    y8960Opl2exDestroy(rm->chip);

    deviceManagerUnregister(rm->deviceHandle);

    free(rm);

    theOpl2ex = NULL;
}

void romMapperY8960Opl2exDestroy(void)
{
    if (theOpl2ex != NULL) {
        destroy(theOpl2ex);
    }
}

int romMapperY8960Opl2exCreate(void)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperY8960Opl2ex* rm = (RomMapperY8960Opl2ex*)calloc(1, sizeof(RomMapperY8960Opl2ex));

    rm->deviceHandle = deviceManagerRegister(ROM_Y8960, &callbacks, rm);

    theOpl2ex = rm;

    rm->chip = y8960Opl2exCreate(boardGetMixer());

    ioPortRegister(Y8960_OPL2_ADDRESS, readIo, writeIo, rm);
    ioPortRegister(Y8960_OPL2_DATA, readIo, writeIo, rm);

    y8960RegisterTunnel(Y8960_TUN_OPL2, tunnel, rm);

    reset(rm);

    return 1;
}
