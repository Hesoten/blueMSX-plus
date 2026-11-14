/*****************************************************************************
**
** Y8960 cartridge - the System Controller block: the identification on
** I/O 40h-42h.
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
#include "DebugDeviceManager.h"
#include "SaveState.h"
#include "IoPort.h"
#include <stdlib.h>

/* I/O 40h-4Fh is the MSX switched I/O: a write to 40h picks a device by its
** ID, and the board then hands that device alone the read of 40h and every
** access to 41h-4Fh (IoPort.c). Writing 80h to 40h turns the System
** Controller on, so 80h is its ID here. It reads 40h as the complement, 7Fh.
**
** Only the identification is emulated. 41h takes a board ID, 02h being this
** cartridge, and 42h a function, 01h being the Config ROM reader/writer; each
** reads back the complement of the value that selected it, and FFh otherwise.
** The Config ROM reader/writer itself (43h-47h) is not: those ports read FFh
** and drop writes.
**
** The hardware forgets the board and the function when 40h is written with
** anything but 80h. The board does not tell a switched device that it was
** switched away, so here the selection lasts until the next write to 41h or a
** reset. Software that finds the cartridge writes 41h right after 40h, and
** sees no difference. */
#define Y8960_SYSCTRL_ID       0x80
#define Y8960_SYSCTRL_BOARD    0x02
#define Y8960_SYSCTRL_FUNC_ROM 0x01

typedef struct {
    int   deviceHandle;
    int   debugHandle;
    UInt8 boardSelected;
    UInt8 romFunction;
} RomMapperY8960SysCtrl;

static RomMapperY8960SysCtrl* theSysCtrl = NULL;

static UInt8 peek(RomMapperY8960SysCtrl* rm, UInt16 port)
{
    switch (port & 0x0F) {
    case 0x00:
        return (UInt8)~Y8960_SYSCTRL_ID;
    case 0x01:
        return rm->boardSelected ? (UInt8)~Y8960_SYSCTRL_BOARD : 0xFF;
    case 0x02:
        return (rm->boardSelected && rm->romFunction) ? (UInt8)~Y8960_SYSCTRL_FUNC_ROM : 0xFF;
    }

    return 0xFF;
}

static UInt8 read(RomMapperY8960SysCtrl* rm, UInt16 port)
{
    return peek(rm, port);
}

static void write(RomMapperY8960SysCtrl* rm, UInt16 port, UInt8 value)
{
    switch (port & 0x0F) {
    case 0x01:
        rm->boardSelected = (UInt8)(value == Y8960_SYSCTRL_BOARD);
        if (!rm->boardSelected) {
            rm->romFunction = 0;
        }
        break;
    case 0x02:
        if (rm->boardSelected) {
            rm->romFunction = (UInt8)(value == Y8960_SYSCTRL_FUNC_ROM);
        }
        break;
    }
}

static void reset(RomMapperY8960SysCtrl* rm)
{
    rm->boardSelected = 0;
    rm->romFunction   = 0;
}

static void saveState(RomMapperY8960SysCtrl* rm)
{
    SaveState* state = saveStateOpenForWrite("y8960sysctrl");

    saveStateSet(state, "boardSelected", rm->boardSelected);
    saveStateSet(state, "romFunction",   rm->romFunction);

    saveStateClose(state);
}

static void loadState(RomMapperY8960SysCtrl* rm)
{
    SaveState* state = saveStateOpenForRead("y8960sysctrl");

    rm->boardSelected = (UInt8)saveStateGet(state, "boardSelected", 0);
    rm->romFunction   = (UInt8)saveStateGet(state, "romFunction",   0);

    saveStateClose(state);
}

static void getDebugInfo(RomMapperY8960SysCtrl* rm, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts;
    int i;

    if (!ioPortCheckSub(Y8960_SYSCTRL_ID)) {
        return;
    }

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, "Y8960 System Controller", 3);

    /* A write to 40h selects the device and never reaches it. */
    dbgIoPortsAddPort(ioPorts, 0, 0x40, DBG_IO_READ, peek(rm, 0));

    for (i = 1; i < 3; i++) {
        dbgIoPortsAddPort(ioPorts, i, (UInt16)(0x40 + i), DBG_IO_READWRITE, peek(rm, (UInt16)i));
    }
}

static void destroy(RomMapperY8960SysCtrl* rm)
{
    ioPortUnregisterSub(Y8960_SYSCTRL_ID);

    debugDeviceUnregister(rm->debugHandle);
    deviceManagerUnregister(rm->deviceHandle);

    free(rm);

    theSysCtrl = NULL;
}

void romMapperY8960SysCtrlDestroy(void)
{
    if (theSysCtrl != NULL) {
        destroy(theSysCtrl);
    }
}

int romMapperY8960SysCtrlCreate(void)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    RomMapperY8960SysCtrl* rm = (RomMapperY8960SysCtrl*)calloc(1, sizeof(RomMapperY8960SysCtrl));

    rm->deviceHandle = deviceManagerRegister(ROM_Y8960, &callbacks, rm);
    rm->debugHandle  = debugDeviceRegister(DBGTYPE_PORT, "Y8960 System Controller", &dbgCallbacks, rm);

    theSysCtrl = rm;

    ioPortRegisterSub(Y8960_SYSCTRL_ID, read, write, rm);

    reset(rm);

    return 1;
}
