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

/* How often a busy command engine is checked while geo3d waits on it. Its end
** time is exact whenever it is seen, so this only bounds the host work. */
#define POLL_TICKS 16

/* Times are in VDP budget units, 8 per board tick and 4 per engine clock of
** geo3d (ec), absolute and wrapping, compared through Int32 differences. */
#define UNITS_PER_TICK 8
#define UNITS_PER_EC   4

/* The R800 runs some steps whole (an S1990 VDP port wait is 186 ticks), so the
** timer can fire that late; a command written late gets that time back, up to
** a cap above the longest such step. */
#define MAX_CREDIT (256 * UNITS_PER_TICK)

/* 3: the core carries the transform end. 2: the engine timing. 1: no timing. */
#define STATE_VERSION 3

/* Where a RUN stands: about to start, waiting for the VDP to finish a command,
** for the next command to land in R#46, or for RUN busy to clear. */
enum { PH_IDLE, PH_WAIT_CE, PH_LAND, PH_DRAIN, PH_START };

typedef struct {
    int         deviceHandle;
    int         debugHandle;
    Geo3D*      geo3d;
    BoardTimer* timer;
    int         phase;
    UInt32      runTime;    /* units, when RUN was written */
    int         issued;     /* commands of this RUN written so far */
    UInt32      lastReady;  /* producer clock (ec) of the last one written */
    UInt32      ready;      /* P: when the last one entered the holding register */
    UInt32      issueEnd;   /* E: when its last register was written */
    UInt32      ceFall;     /* C: when the VDP finished the command before */
    UInt32      endSeen;    /* the VDP's last end time when the plan was made */
    UInt32      nextReady;  /* P and E of the command due to land */
    UInt32      nextEnd;
    UInt32      due;        /* when it lands, or when RUN busy clears */
} Geo3DDevice;

static UInt32 later(UInt32 a, UInt32 b)
{
    return (Int32)(a - b) > 0 ? a : b;
}

/* P of the next command: the holding register takes it once the last one is
** written, and never earlier than the producer, unstalled, gets to it. */
static UInt32 pendingReady(Geo3DDevice* dev, UInt32 readyEc)
{
    if (dev->issued == 0) {
        return dev->runTime + readyEc * UNITS_PER_EC;
    }
    return later(dev->ready + (readyEc - dev->lastReady) * UNITS_PER_EC,
                 dev->issueEnd + UNITS_PER_EC);
}

/* When the producer reaches R_DRAIN, known once at most one command is left
** for it to hand over; 0 while it is not. */
static int producerEnd(Geo3DDevice* dev, UInt32* end)
{
    UInt32 drain = geo3dEndClock(dev->geo3d);
    UInt32 readyEc;

    switch (geo3dCommandsLeft(dev->geo3d)) {
    case 0:
        *end = dev->issued == 0 ? dev->runTime + drain * UNITS_PER_EC
                                : dev->ready + (drain - dev->lastReady) * UNITS_PER_EC;
        return 1;
    case 1:
        geo3dPeekCommand(dev->geo3d, &readyEc);
        *end = pendingReady(dev, readyEc) + (drain - readyEc) * UNITS_PER_EC;
        return 1;
    }
    return 0;
}

/* The engine clock since RUN and the producer's end on the same scale, for
** the status bits. Reads nothing but the state. */
static void statusClock(Geo3DDevice* dev, UInt32* ec, UInt32* edgeEnd)
{
    UInt32 end;
    Int32 elapsed = (Int32)(boardSystemTime() * UNITS_PER_TICK - dev->runTime);

    *ec = elapsed < 0 ? 0 : (UInt32)elapsed / UNITS_PER_EC;
    *edgeEnd = 0xffffffff;
    if (producerEnd(dev, &end)) {
        elapsed = (Int32)(end - dev->runTime);
        *edgeEnd = elapsed < 0 ? 0 : (UInt32)elapsed / UNITS_PER_EC;
    }
}

/* The issuer starts once the VDP's CE, synced over a clock, reads 0 and 4
** clocks have passed since the last write; R#46 lands one clock after its
** turn, so 14 ec after CE falls for LINE and 22 for LRMM. */
static void plan(Geo3DDevice* dev)
{
    UInt32 readyEc;
    UInt32 start;
    int size = geo3dPeekCommand(dev->geo3d, &readyEc);

    if (size == 0) {
        producerEnd(dev, &dev->due);
        if (dev->issued != 0) {
            /* RUN busy drops 1.5 ec after the last CE fall at the earliest. */
            dev->due = later(dev->due, dev->ceFall + 3 * UNITS_PER_EC / 2);
        }
        dev->phase = PH_DRAIN;
        return;
    }

    dev->nextReady = pendingReady(dev, readyEc);
    start = later(dev->nextReady + UNITS_PER_EC, dev->ceFall + 2 * UNITS_PER_EC);
    if (dev->issued != 0) {
        start = later(start, dev->issueEnd + 5 * UNITS_PER_EC);
    }
    dev->nextEnd = start + size * UNITS_PER_EC;
    dev->due     = dev->nextEnd + UNITS_PER_EC;
    dev->phase   = PH_LAND;
}

/* Writes the command due now, as if it had landed when it was due. */
static void issue(Geo3DDevice* dev, UInt32 now)
{
    UInt8 regs[19];
    UInt8 values[19];
    UInt32 readyEc = 0;
    int late = (Int32)(now - dev->due);
    int count;
    int i;

    geo3dPeekCommand(dev->geo3d, &readyEc);
    count = geo3dNextCommand(dev->geo3d, regs, values);
    for (i = 0; i < count; i++) {
        vdpWriteCommandRegister(regs[i], values[i]);
    }
    vdpAddCommandCredit(late < MAX_CREDIT ? late : MAX_CREDIT);

    dev->issued++;
    dev->lastReady = readyEc;
    dev->ready     = dev->nextReady;
    dev->issueEnd  = dev->nextEnd;
    dev->phase     = PH_WAIT_CE;
}

/* Plays the RUN up to now, then sets the timer for what comes next. */
static void pump(Geo3DDevice* dev)
{
    UInt32 time = boardSystemTime();
    UInt32 now = time * UNITS_PER_TICK;
    UInt32 endTime;
    UInt32 readyEc;

    for (;;) {
        switch (dev->phase) {
        case PH_START:
            /* A RUN with nothing to draw does not wait on the VDP. */
            if (geo3dPeekCommand(dev->geo3d, &readyEc) != 0 && vdpGetCommandBusy()) {
                dev->phase = PH_WAIT_CE;
                break;
            }
            dev->ceFall  = dev->runTime - 2 * UNITS_PER_EC;
            dev->endSeen = vdpGetCommandEndTime();
            plan(dev);
            break;
        case PH_WAIT_CE:
            if (vdpGetCommandBusy()) {
                boardTimerAdd(dev->timer, time + POLL_TICKS);
                return;
            }
            /* Read only after the engine was seen busy, so it is never stale. */
            dev->ceFall  = vdpGetCommandEndTime();
            dev->endSeen = dev->ceFall;
            plan(dev);
            break;
        case PH_LAND:
        case PH_DRAIN:
            if ((Int32)(dev->due - now) > 0) {
                boardTimerAdd(dev->timer,
                              time + (dev->due - now + UNITS_PER_TICK - 1) / UNITS_PER_TICK);
                return;
            }
            if (dev->phase == PH_DRAIN) {
                geo3dFinishRun(dev->geo3d);
                dev->phase = PH_IDLE;
                boardTimerRemove(dev->timer);
                return;
            }
            /* A CPU command written while this one was overdue goes first, as
            ** if it had come first; ours waits for it rather than cutting in. */
            if (vdpGetCommandBusy()) {
                dev->phase = PH_WAIT_CE;
                break;
            }
            /* One the CPU both started and finished since the plan moves it. */
            endTime = vdpGetCommandEndTime();
            if (endTime != dev->endSeen) {
                dev->ceFall  = endTime;
                dev->endSeen = endTime;
                plan(dev);
                break;
            }
            issue(dev, now);
            break;
        default:
            boardTimerRemove(dev->timer);
            return;
        }
    }
}

static void startRun(Geo3DDevice* dev)
{
    dev->runTime = boardSystemTime() * UNITS_PER_TICK;
    dev->issued  = 0;
    dev->phase   = PH_START;
    pump(dev);
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
    saveStateSet(state, "phase",     dev->phase);
    saveStateSet(state, "runTime",   dev->runTime);
    saveStateSet(state, "issued",    dev->issued);
    saveStateSet(state, "lastReady", dev->lastReady);
    saveStateSet(state, "ready",     dev->ready);
    saveStateSet(state, "issueEnd",  dev->issueEnd);
    saveStateSet(state, "ceFall",    dev->ceFall);
    saveStateSet(state, "endSeen",   dev->endSeen);
    saveStateSet(state, "nextReady", dev->nextReady);
    saveStateSet(state, "nextEnd",   dev->nextEnd);
    saveStateSet(state, "due",       dev->due);

    saveStateClose(state);
    free(data);
}

static void loadState(Geo3DDevice* dev)
{
    SaveState* state = saveStateOpenForRead("Geo3DDevice");
    UInt32 version = saveStateGet(state, "version", 0);
    UInt32 size = saveStateGet(state, "size", 0);
    UInt8* data = NULL;

    if (version >= 1 && version <= STATE_VERSION && size != 0) {
        data = calloc(1, size);
    }
    if (data == NULL) {
        geo3dReset(dev->geo3d);
    }
    else {
        /* The device version gives the layout of the core blob. */
        saveStateGetBuffer(state, "core", data, size);
        geo3dLoadState(dev->geo3d, data, size, (int)version);
        free(data);
    }
    dev->phase     = saveStateGet(state, "phase",     PH_IDLE);
    dev->runTime   = saveStateGet(state, "runTime",   0);
    dev->issued    = saveStateGet(state, "issued",    0);
    dev->lastReady = saveStateGet(state, "lastReady", 0);
    dev->ready     = saveStateGet(state, "ready",     0);
    dev->issueEnd  = saveStateGet(state, "issueEnd",  0);
    dev->ceFall    = saveStateGet(state, "ceFall",    0);
    dev->endSeen   = saveStateGet(state, "endSeen",   dev->ceFall);
    dev->nextReady = saveStateGet(state, "nextReady", 0);
    dev->nextEnd   = saveStateGet(state, "nextEnd",   0);
    dev->due       = saveStateGet(state, "due",       0);

    saveStateClose(state);

    boardTimerRemove(dev->timer);
    if (!geo3dIsRunning(dev->geo3d)) {
        dev->phase = PH_IDLE;
        return;
    }
    /* A RUN from before the timing restarts its issue from now, commands
    ** ready at once; so does one whose phase is damaged. */
    if (version < 2 || dev->phase < PH_WAIT_CE || dev->phase > PH_START) {
        dev->runTime = boardSystemTime() * UNITS_PER_TICK;
        dev->issued  = 0;
        dev->phase   = PH_START;
    }
    /* Taken up by the timer, once every device is loaded. */
    boardTimerAdd(dev->timer, boardSystemTime() + 1);
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
    dev->phase = PH_IDLE;
}

/* 9Dh is the index when written and the status when read, 9Fh the data. */
static UInt8 read(Geo3DDevice* dev, UInt16 ioPort)
{
    UInt32 ec;
    UInt32 edgeEnd;

    if (geo3dIsRunning(dev->geo3d)) {
        pump(dev);
    }
    statusClock(dev, &ec, &edgeEnd);
    if (ioPort & 0x02) {
        return geo3dReadData(dev->geo3d, ec, edgeEnd);
    }
    return geo3dReadStatus(dev->geo3d, ec, edgeEnd);
}

static UInt8 peek(Geo3DDevice* dev, UInt16 ioPort)
{
    UInt32 ec;
    UInt32 edgeEnd;

    statusClock(dev, &ec, &edgeEnd);
    if (ioPort & 0x02) {
        return geo3dPeekData(dev->geo3d, ec, edgeEnd);
    }
    return geo3dReadStatus(dev->geo3d, ec, edgeEnd);
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
        startRun(dev);
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
    Geo3DDevice* dev = calloc(1, sizeof(Geo3DDevice));

    dev->deviceHandle = deviceManagerRegister(ROM_V9958, &callbacks, dev);
    dev->debugHandle = debugDeviceRegister(DBGTYPE_VIDEO, "geo3d", &dbgCallbacks, dev);

    dev->geo3d = geo3dCreate();
    dev->timer = boardTimerCreate(onTimer, dev);

    ioPortRegister(0x9d, read, write, dev);
    ioPortRegister(0x9f, read, write, dev);

    reset(dev);
}
