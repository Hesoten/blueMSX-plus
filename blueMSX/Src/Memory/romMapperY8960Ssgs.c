/*****************************************************************************
**
** Y8960 cartridge - the SSGS block: two YM2149 equivalents with pan pots.
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
#include "Y8960Ssgs.h"
#include "AudioMixer.h"
#include <stdlib.h>

/* The machine's own PSG sits on the same three addresses. Both can be
** registered since the overlap was fixed upstream, and both receive the
** write. The read port at A2h is shared too: this block answers only for its
** second core's registers, which the machine's PSG leaves alone (Y8960Ssgs.h).
** Writes to A2h and A3h do nothing on the cartridge, so A2h is read only here
** and A3h is not claimed. */
#define Y8960_SSGS_ADDRESS 0xA0
#define Y8960_SSGS_DATA    0xA1
#define Y8960_SSGS_READ    0xA2

typedef struct {
    int             deviceHandle;
    Y8960SsgsChip*  chip;
} RomMapperY8960Ssgs;

static RomMapperY8960Ssgs* theSsgs = NULL;

static void writeIo(RomMapperY8960Ssgs* rm, UInt16 port, UInt8 value)
{
    if (!y8960IoEnabled(Y8960_IO_SSGS)) {
        return;
    }

    if (port == Y8960_SSGS_DATA) {
        y8960SsgsWriteData(rm->chip, value);
    }
    else {
        y8960SsgsWriteAddress(rm->chip, value);
    }
}

static UInt8 readIo(RomMapperY8960Ssgs* rm, UInt16 port)
{
    if (!y8960IoEnabled(Y8960_IO_SSGS)) {
        return 0xFF;
    }

    return y8960SsgsReadPort(rm->chip);
}

/* The tunnel is not gated: the enablers live in the same window and could not
** be reached otherwise. */
static void tunnel(void* ref, int port, UInt8 value)
{
    RomMapperY8960Ssgs* rm = (RomMapperY8960Ssgs*)ref;

    if (port) {
        y8960SsgsWriteData(rm->chip, value);
    }
    else {
        y8960SsgsWriteAddress(rm->chip, value);
    }
}

static void reset(RomMapperY8960Ssgs* rm)
{
    y8960SsgsReset(rm->chip);
}

static void saveState(RomMapperY8960Ssgs* rm)
{
    y8960SsgsSaveState(rm->chip);
}

static void loadState(RomMapperY8960Ssgs* rm)
{
    y8960SsgsLoadState(rm->chip);
}

static void destroy(RomMapperY8960Ssgs* rm)
{
    y8960UnregisterTunnel(Y8960_TUN_SSGS, rm);

    ioPortUnregister(Y8960_SSGS_ADDRESS, rm);
    ioPortUnregister(Y8960_SSGS_DATA, rm);
    ioPortUnregister(Y8960_SSGS_READ, rm);

    y8960SsgsDestroy(rm->chip);

    deviceManagerUnregister(rm->deviceHandle);

    free(rm);

    theSsgs = NULL;
}

void romMapperY8960SsgsDestroy(void)
{
    if (theSsgs != NULL) {
        destroy(theSsgs);
    }
}

int romMapperY8960SsgsCreate(void)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperY8960Ssgs* rm = (RomMapperY8960Ssgs*)calloc(1, sizeof(RomMapperY8960Ssgs));

    rm->deviceHandle = deviceManagerRegister(ROM_Y8960, &callbacks, rm);

    theSsgs = rm;

    rm->chip = y8960SsgsCreate(boardGetMixer(), "Y8960 SSGS");

    ioPortRegister(Y8960_SSGS_ADDRESS, NULL, writeIo, rm);
    ioPortRegister(Y8960_SSGS_DATA, NULL, writeIo, rm);
    ioPortRegister(Y8960_SSGS_READ, readIo, NULL, rm);

    y8960RegisterTunnel(Y8960_TUN_SSGS, tunnel, rm);

    reset(rm);

    return 1;
}
