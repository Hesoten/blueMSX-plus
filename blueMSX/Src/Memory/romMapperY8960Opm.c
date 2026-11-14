/*****************************************************************************
**
** Y8960 cartridge - the OPM block: a YM2151 equivalent.
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
#include "Y8960Opm.h"
#include "AudioMixer.h"
#include <stdlib.h>

#define Y8960_OPM_ADDRESS 0x7E
#define Y8960_OPM_DATA    0x7F

typedef struct {
    int           deviceHandle;
    Y8960OpmChip* chip;
} RomMapperY8960Opm;

static RomMapperY8960Opm* theOpm = NULL;

/* The status is read at 7Fh only; 7Eh and the window are write only. */
static UInt8 readIo(RomMapperY8960Opm* rm, UInt16 port)
{
    if (!y8960IoEnabled(Y8960_IO_OPM)) {
        return 0xFF;
    }

    return y8960OpmReadStatus(rm->chip);
}

static void writeIo(RomMapperY8960Opm* rm, UInt16 port, UInt8 value)
{
    if (!y8960IoEnabled(Y8960_IO_OPM)) {
        return;
    }

    if (port == Y8960_OPM_DATA) {
        y8960OpmWriteData(rm->chip, value);
    }
    else {
        y8960OpmWriteAddress(rm->chip, value);
    }
}

/* The tunnel is not gated: the enablers live in the same window and could not
** be reached otherwise. */
static void tunnel(void* ref, int port, UInt8 value)
{
    RomMapperY8960Opm* rm = (RomMapperY8960Opm*)ref;

    if (port) {
        y8960OpmWriteData(rm->chip, value);
    }
    else {
        y8960OpmWriteAddress(rm->chip, value);
    }
}

static void reset(RomMapperY8960Opm* rm)
{
    y8960OpmReset(rm->chip);
}

static void saveState(RomMapperY8960Opm* rm)
{
    y8960OpmSaveState(rm->chip);
}

static void loadState(RomMapperY8960Opm* rm)
{
    y8960OpmLoadState(rm->chip);
}

static void destroy(RomMapperY8960Opm* rm)
{
    y8960UnregisterTunnel(Y8960_TUN_OPM, rm);

    ioPortUnregister(Y8960_OPM_ADDRESS, rm);
    ioPortUnregister(Y8960_OPM_DATA, rm);

    y8960OpmDestroy(rm->chip);

    deviceManagerUnregister(rm->deviceHandle);

    free(rm);

    theOpm = NULL;
}

void romMapperY8960OpmDestroy(void)
{
    if (theOpm != NULL) {
        destroy(theOpm);
    }
}

int romMapperY8960OpmCreate(void)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperY8960Opm* rm = (RomMapperY8960Opm*)calloc(1, sizeof(RomMapperY8960Opm));

    rm->deviceHandle = deviceManagerRegister(ROM_Y8960, &callbacks, rm);

    theOpm = rm;

    rm->chip = y8960OpmCreate(boardGetMixer());

    ioPortRegister(Y8960_OPM_ADDRESS, NULL, writeIo, rm);
    ioPortRegister(Y8960_OPM_DATA, readIo, writeIo, rm);

    y8960RegisterTunnel(Y8960_TUN_OPM, tunnel, rm);

    reset(rm);

    return 1;
}
