/*****************************************************************************
**
** geo3d 3D coprocessor in the V9968, on I/O ports 9Dh and 9Fh.
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
#include "Geo3DDevice.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "DebugDeviceManager.h"
#include "SaveState.h"
#include "Board.h"
#include "IoPort.h"
#include "Geo3D.h"
#include "VDP.h"
#include <stdlib.h>

/* How often a busy command engine is checked: 16 VDP clocks. */
#define POLL_TICKS 16

#define STATE_VERSION 1

typedef struct {
    int         deviceHandle;
    int         debugHandle;
    Geo3D*      geo3d;
    BoardTimer* timer;
} Geo3DDevice;

/* Hands the next commands of a RUN to the VDP whenever its command engine is
** idle, and clears RUN busy once the last one has finished. */
static void pump(Geo3DDevice* dev)
{
    UInt8 regs[19];
    UInt8 values[19];
    int count;
    int i;

    while (!vdpGetCommandBusy()) {
        count = geo3dNextCommand(dev->geo3d, regs, values);
        if (count == 0) {
            geo3dFinishRun(dev->geo3d);
            boardTimerRemove(dev->timer);
            return;
        }
        for (i = 0; i < count; i++) {
            vdpWriteCommandRegister(regs[i], values[i]);
        }
    }
    boardTimerAdd(dev->timer, boardSystemTime() + POLL_TICKS);
}

static void onTimer(Geo3DDevice* dev, UInt32 time)
{
    if (geo3dIsRunning(dev->geo3d)) {
        pump(dev);
    }
}

static void saveState(Geo3DDevice* dev)
{
    SaveState* state = saveStateOpenForWrite("Geo3DDevice");
    UInt32 size;
    UInt8* data = geo3dSaveState(dev->geo3d, &size);

    saveStateSet(state, "version", STATE_VERSION);
    saveStateSet(state, "size", size);
    saveStateSetBuffer(state, "core", data, size);

    saveStateClose(state);
    free(data);
}

static void loadState(Geo3DDevice* dev)
{
    SaveState* state = saveStateOpenForRead("Geo3DDevice");
    UInt32 version = saveStateGet(state, "version", 0);
    UInt32 size = saveStateGet(state, "size", 0);
    UInt8* data = NULL;

    if (version == STATE_VERSION && size != 0) {
        data = calloc(1, size);
    }
    if (data == NULL) {
        geo3dReset(dev->geo3d);
    }
    else {
        saveStateGetBuffer(state, "core", data, size);
        geo3dLoadState(dev->geo3d, data, size);
        free(data);
    }

    saveStateClose(state);

    if (geo3dIsRunning(dev->geo3d)) {
        boardTimerAdd(dev->timer, boardSystemTime() + POLL_TICKS);
    }
}

static void destroy(Geo3DDevice* dev)
{
    deviceManagerUnregister(dev->deviceHandle);
    debugDeviceUnregister(dev->debugHandle);

    ioPortUnregister(0x9d, dev);
    ioPortUnregister(0x9f, dev);

    boardTimerDestroy(dev->timer);
    geo3dDestroy(dev->geo3d);

    free(dev);
}

static void reset(Geo3DDevice* dev)
{
    boardTimerRemove(dev->timer);
    geo3dReset(dev->geo3d);
}

/* 9Dh is the index when written and the status when read, 9Fh the data. */
static UInt8 read(Geo3DDevice* dev, UInt16 ioPort)
{
    if (geo3dIsRunning(dev->geo3d)) {
        pump(dev);
    }
    if (ioPort & 0x02) {
        return geo3dReadData(dev->geo3d);
    }
    return geo3dReadStatus(dev->geo3d);
}

static UInt8 peek(Geo3DDevice* dev, UInt16 ioPort)
{
    if (ioPort & 0x02) {
        return geo3dPeekData(dev->geo3d);
    }
    return geo3dReadStatus(dev->geo3d);
}

static void write(Geo3DDevice* dev, UInt16 ioPort, UInt8 value)
{
    int wasRunning;

    if (!(ioPort & 0x02)) {
        geo3dWriteIndex(dev->geo3d, value);
        return;
    }
    wasRunning = geo3dIsRunning(dev->geo3d);
    geo3dWriteData(dev->geo3d, value);
    if (!wasRunning && geo3dIsRunning(dev->geo3d)) {
        pump(dev);
    }
}

static void getDebugInfo(Geo3DDevice* dev, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts;

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, "geo3d", 2);
    dbgIoPortsAddPort(ioPorts, 0, 0x9d, DBG_IO_READWRITE, peek(dev, 0x9d));
    dbgIoPortsAddPort(ioPorts, 1, 0x9f, DBG_IO_READWRITE, peek(dev, 0x9f));
}

void geo3dDeviceCreate()
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    Geo3DDevice* dev = malloc(sizeof(Geo3DDevice));

    dev->deviceHandle = deviceManagerRegister(ROM_V9958, &callbacks, dev);
    dev->debugHandle = debugDeviceRegister(DBGTYPE_VIDEO, "geo3d", &dbgCallbacks, dev);

    dev->geo3d = geo3dCreate();
    dev->timer = boardTimerCreate(onTimer, dev);

    ioPortRegister(0x9d, read, write, dev);
    ioPortRegister(0x9f, read, write, dev);

    reset(dev);
}
