/*****************************************************************************
**
** GFX9000 V9990 graphics cartridge.
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
#include "romMapperGfx9000.h"
#include "V9990.h"
#include "Video9000.h"
#include "VideoManager.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "IoPort.h"
#include "Board.h"
#include <stdlib.h>

#define PORT_BASE  0x60
#define PORT_COUNT 16

typedef struct {
    int deviceHandle;
    int slot;
    int sslot;
    V9990* v9990;
    Video9000* video9000;
} RomMapperGfx9000;

static void saveState(RomMapperGfx9000* rm)
{
    v9990SaveState(rm->v9990);
    if (rm->video9000 != NULL) {
        video9000SaveState(rm->video9000);
    }
}

/* The V9990 latches superimpose from the Video9000 setting. */
static void loadState(RomMapperGfx9000* rm)
{
    if (rm->video9000 != NULL) {
        video9000LoadState(rm->video9000);
    }
    v9990LoadState(rm->v9990);
}

static void reset(RomMapperGfx9000* rm)
{
    if (rm->video9000 != NULL) {
        video9000Reset(rm->video9000);
    }
    v9990Reset(rm->v9990);
}

static void destroy(RomMapperGfx9000* rm)
{
    int i;

    slotUnregister(rm->slot, rm->sslot, 0);
    for (i = 0; i < PORT_COUNT; i++) {
        ioPortUnregister(PORT_BASE + i, rm);
    }
    if (rm->video9000 != NULL) {
        video9000Destroy(rm->video9000);
    }
    v9990Destroy(rm->v9990);
    deviceManagerUnregister(rm->deviceHandle);

    free(rm);
}

static UInt8 readIo(RomMapperGfx9000* rm, UInt16 port)
{
    return v9990ReadPort(rm->v9990, port);
}

/* The Video9000 switch takes writes to 0x6F; reads still go to the V9990. */
static void writeIo(RomMapperGfx9000* rm, UInt16 port, UInt8 value)
{
    if (rm->video9000 != NULL && (port & 0x0f) == 0x0f) {
        video9000Write(rm->video9000, value);
        return;
    }
    v9990WritePort(rm->v9990, port, value);
}

static int create(int slot, int sslot, int romType)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperGfx9000* rm;
    int i;

    /* The interrupt is an MSX /INT; a second V9990 or a GIDE would share
    ** these ports. */
    if (boardGetType() != BOARD_MSX) {
        return 0;
    }
    for (i = 0; i < PORT_COUNT; i++) {
        if (ioPortGetRef(PORT_BASE + i) != NULL) {
            return 0;
        }
    }
    /* The Video9000 shows the machine's VDP, the first video source. */
    if (romType == ROM_VIDEO9000 && videoManagerGetCount() == 0) {
        return 0;
    }

    rm = calloc(1, sizeof(RomMapperGfx9000));

    rm->slot  = slot;
    rm->sslot = sslot;
    rm->v9990 = v9990Create();
    /* The VDP registers first. It keeps drawing while the V9990 is shown, so
    ** its status flags stay right and switching back shows a current picture. */
    videoManagerSetDrawSource(v9990GetVideoHandle(rm->v9990), videoManagerGetHandle(0));
    if (romType == ROM_VIDEO9000) {
        rm->video9000 = video9000Create(rm->v9990);
        if (rm->video9000 == NULL) {
            v9990Destroy(rm->v9990);
            free(rm);
            return 0;
        }
    }
    else {
        v9990SetShowOnDisplay(rm->v9990, 1);
    }

    rm->deviceHandle = deviceManagerRegister(romType, &callbacks, rm);

    /* The cartridge has no memory: its slot reads 0xFF. */
    slotRegister(slot, sslot, 0, 8, NULL, NULL, NULL, destroy, rm);
    for (i = 0; i < 8; i++) {
        slotMapPage(slot, sslot, i, NULL, 0, 0);
    }

    for (i = 0; i < PORT_COUNT; i++) {
        ioPortRegister(PORT_BASE + i, readIo, writeIo, rm);
    }

    return 1;
}

int romMapperGfx9000Create(int slot, int sslot)
{
    return create(slot, sslot, ROM_GFX9000);
}

int romMapperVideo9000Create(int slot, int sslot)
{
    return create(slot, sslot, ROM_VIDEO9000);
}
