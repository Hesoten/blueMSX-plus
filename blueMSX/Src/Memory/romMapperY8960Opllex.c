/*****************************************************************************
**
** Y8960 cartridge - the OPLLEX block: YM2413 with a per-channel preset bank.
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
#include "Y8960Opllex.h"
#include "AudioMixer.h"
#include <stdlib.h>

/* MSX-MUSIC's own addresses, so that software written for a built-in FM
** sound chip drives it unchanged. */
#define Y8960_OPLL_ADDRESS 0x7C
#define Y8960_OPLL_DATA    0x7D

typedef struct {
    int              deviceHandle;
    Y8960OpllexChip* chip;
} RomMapperY8960Opllex;

static RomMapperY8960Opllex* theOpllex = NULL;

static void writeIo(RomMapperY8960Opllex* rm, UInt16 port, UInt8 value)
{
    if (!y8960IoEnabled(Y8960_IO_OPLL)) {
        return;
    }

    if (port == Y8960_OPLL_DATA) {
        y8960OpllexWriteData(rm->chip, value);
    }
    else {
        y8960OpllexWriteAddress(rm->chip, value);
    }
}

/* The tunnel is not gated: the enablers live in the same window and could not
** be reached otherwise. */
static void tunnel(void* ref, int port, UInt8 value)
{
    RomMapperY8960Opllex* rm = (RomMapperY8960Opllex*)ref;

    if (port) {
        y8960OpllexWriteData(rm->chip, value);
    }
    else {
        y8960OpllexWriteAddress(rm->chip, value);
    }
}

static void reset(RomMapperY8960Opllex* rm)
{
    y8960OpllexReset(rm->chip);
}

static void saveState(RomMapperY8960Opllex* rm)
{
    y8960OpllexSaveState(rm->chip);
}

static void loadState(RomMapperY8960Opllex* rm)
{
    y8960OpllexLoadState(rm->chip);
}

static void destroy(RomMapperY8960Opllex* rm)
{
    y8960UnregisterTunnel(Y8960_TUN_OPLL, rm);

    ioPortUnregister(Y8960_OPLL_ADDRESS, rm);
    ioPortUnregister(Y8960_OPLL_DATA, rm);

    y8960OpllexDestroy(rm->chip);

    deviceManagerUnregister(rm->deviceHandle);

    free(rm);

    theOpllex = NULL;
}

void romMapperY8960OpllexDestroy(void)
{
    if (theOpllex != NULL) {
        destroy(theOpllex);
    }
}

int romMapperY8960OpllexCreate(void)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperY8960Opllex* rm = (RomMapperY8960Opllex*)calloc(1, sizeof(RomMapperY8960Opllex));

    rm->deviceHandle = deviceManagerRegister(ROM_Y8960, &callbacks, rm);

    theOpllex = rm;

    rm->chip = y8960OpllexCreate(boardGetMixer(), "Y8960 OPLLEX");

    /* Write only, as the FM-PAC's own 7Ch-7Dh are: these addresses are shared
    ** with the machine's MSX-MUSIC when one is present. */
    ioPortRegister(Y8960_OPLL_ADDRESS, NULL, writeIo, rm);
    ioPortRegister(Y8960_OPLL_DATA, NULL, writeIo, rm);

    y8960RegisterTunnel(Y8960_TUN_OPLL, tunnel, rm);

    reset(rm);

    return 1;
}
