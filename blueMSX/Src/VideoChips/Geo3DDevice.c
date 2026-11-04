/*****************************************************************************
**
** geo3d 3D coprocessor of the V9968 on I/O ports 9Dh/9Fh or 8Dh/8Fh.
** on the V9968 cartridge.
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
#include "Emulator.h"
#include "DebugTrace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* 4: geo3d v2.0 (1024 entries, 10-bit counts). 3: the core carries the
** transform end. 2: the engine timing. 1: no timing. */
#define STATE_VERSION 4

/* Where a RUN stands: about to start, waiting for the VDP to finish a command,
** for the next command to land in R#46, or for RUN busy to clear. */
enum { PH_IDLE, PH_WAIT_CE = GEO3D_PHASE_WAIT_CE, PH_LAND, PH_DRAIN, PH_START };

/* When the latest RUNs began and ended, for the tool windows; by RUN number. */
#define RUN_STAMPS 32

typedef struct {
    UInt32 seq;
    UInt32 start;           /* board ticks; compared as ages back from now */
    UInt32 end;
    int    ended;
} RunStamp;

struct Geo3DDevice {
    int         deviceHandle;
    int         debugHandle;
    int         port;       /* index and status; port + 2 is the data */
    struct VDP* vdp;        /* NULL for the machine VDP */
    const char* stateName;
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
    int         faces;      /* the RUN fills faces: its LINEs are fill rows */
    RunStamp    stamps[RUN_STAMPS];
    UInt32      epoch;      /* new at each reset and state load, unique across devices */
    /* Trace only, never saved: the CPU's wait on RUN busy so far. */
    UInt16      traceSource;
    int         traceWaiting;
    UInt32      traceBusyReads;
    UInt32      traceEpoch;
    /* Trace only: the data bytes the CPU wrote since the last RUN, to the model RAMs and not. */
    UInt32      traceFeedModel;
    UInt32      traceFeedOther;
    UInt32      traceFeedEpoch;
};

#if RUN_STAMPS > GEO3D_DEBUG_RUNS
#error "a capture copies every RUN that still has its stamp"
#endif

/* The machine's, then a cartridge's, for the tool windows; and what each tool
** holds of its RUNs, epoch in the high half, written by geo3dDebugSince. */
static Geo3DDevice* volatile debugGeo3ds[2];
static volatile UInt64 debugSince[2];
static UInt32 debugEpochs;
static volatile Geo3DDebugRunSink debugRunSink;

static const DebugTraceField traceRunFields[] = {
    { "run",      DEBUG_TRACE_WORD_A,      0,  32, DEBUG_TRACE_DEC, 0,      NULL, 0 },
    { "items",    DEBUG_TRACE_WORD_B,      0,  32, DEBUG_TRACE_DEC, 1 << 1, NULL, 0 },
    { "commands", DEBUG_TRACE_WORD_B,      0,  32, DEBUG_TRACE_DEC, 1 << 2, NULL, 0 },
    { "first",    DEBUG_TRACE_WORD_CONT_A, 0,  16, DEBUG_TRACE_DEC, 0,      NULL, 0 },
    { "count",    DEBUG_TRACE_WORD_CONT_A, 16, 16, DEBUG_TRACE_DEC, 0,      NULL, 0 },
    { "vfirst",   DEBUG_TRACE_WORD_CONT_B, 0,  16, DEBUG_TRACE_DEC, 0,      NULL, 0 },
    { "nvert",    DEBUG_TRACE_WORD_CONT_B, 16, 16, DEBUG_TRACE_DEC, 0,      NULL, 0 },
};

static const DebugTraceField traceXformFields[] = {
    { "run", DEBUG_TRACE_WORD_A, 0, 32, DEBUG_TRACE_DEC, 0, NULL, 0 },
};

static const DebugTraceField traceIssueFields[] = {
    { "index", DEBUG_TRACE_WORD_A,    0, 32, DEBUG_TRACE_DEC, 0, NULL, 0 },
    { "run",   DEBUG_TRACE_WORD_B,    0, 32, DEBUG_TRACE_DEC, 0, NULL, 0 },
    { "link",  DEBUG_TRACE_WORD_LINK, 0, 32, DEBUG_TRACE_DEC, 0, NULL, 0 },
};

static const DebugTraceField traceWaitFields[] = {
    { "reads", DEBUG_TRACE_WORD_A, 0, 32, DEBUG_TRACE_DEC, 1 << 2, NULL, 0 },
    { "run",   DEBUG_TRACE_WORD_B, 0, 32, DEBUG_TRACE_DEC, 0,      NULL, 0 },
};

static const DebugTraceField traceFeedFields[] = {
    { "model", DEBUG_TRACE_WORD_A, 0, 32, DEBUG_TRACE_DEC, 0, NULL, 0 },
    { "other", DEBUG_TRACE_WORD_B, 0, 32, DEBUG_TRACE_DEC, 0, NULL, 0 },
};

static const DebugTraceKindDesc traceKinds[] = {
    { GEO3D_TRACE_RUN,       "run",       traceRunFields,   7 },
    { GEO3D_TRACE_XFORM_END, "xform end", traceXformFields, 1 },
    { GEO3D_TRACE_ISSUE,     "issue",     traceIssueFields, 3 },
    { GEO3D_TRACE_WAIT,      "wait",      traceWaitFields,  2 },
    { GEO3D_TRACE_FEED,      "feed",      traceFeedFields,  2 },
};

static const DebugTraceClassDesc traceClass = { "geo3d", traceKinds, 5 };

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
    int fill;
    int i;
    UInt32 link = 0;

    geo3dPeekCommand(dev->geo3d, &readyEc);
    count = geo3dNextCommand(dev->geo3d, regs, values);
    fill = dev->faces && count == 11;
    if (DEBUG_TRACE_ON(DEBUG_TRACE_BIT(DEBUG_TRACE_GEO3D) | DEBUG_TRACE_BIT(DEBUG_TRACE_VDPCMD))) {
        link = debugTraceNewLink();
        vdpSetCommandTraceLink(dev->vdp, link);
        DEBUG_TRACE(DEBUG_TRACE_GEO3D, dev->traceSource, GEO3D_TRACE_ISSUE,
                    boardSystemTime() - (UInt32)(late / UNITS_PER_TICK), link, dev->issued,
                    geo3dDebugLastSeq(dev->geo3d));
    }
    for (i = 0; i < count; i++) {
        vdpWriteCommandRegister(dev->vdp, regs[i], values[i], fill && regs[i] == 46);
    }
    if (link != 0) {
        vdpSetCommandTraceLink(dev->vdp, 0);
    }
    vdpAddCommandCredit(dev->vdp, late < MAX_CREDIT ? late : MAX_CREDIT);

    dev->issued++;
    dev->lastReady = readyEc;
    dev->ready     = dev->nextReady;
    dev->issueEnd  = dev->nextEnd;
    dev->phase     = PH_WAIT_CE;
}

/* Both are past by the time the RUN is seen to end, so neither is dated ahead. */
static void traceRunEnd(Geo3DDevice* dev, UInt32 seq, UInt32 time, UInt32 now)
{
    UInt32 items;
    UInt32 verts;
    UInt32 xfEnd;

    if (geo3dDebugRunRange(dev->geo3d, seq, &items, &verts, &xfEnd)) {
        xfEnd = dev->runTime + xfEnd * UNITS_PER_EC;
        debugTraceEmit(dev->traceSource, GEO3D_TRACE_XFORM_END,
                       time - (UInt32)((Int32)(now - xfEnd) / UNITS_PER_TICK), 0, seq, 0);
    }
    debugTraceEmit(dev->traceSource, GEO3D_TRACE_RUN | DEBUG_TRACE_END,
                   time - (UInt32)((Int32)(now - dev->due) / UNITS_PER_TICK), 0, seq, (UInt32)dev->issued);
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
            if (geo3dPeekCommand(dev->geo3d, &readyEc) != 0 && vdpGetCommandBusy(dev->vdp)) {
                dev->phase = PH_WAIT_CE;
                break;
            }
            dev->ceFall  = dev->runTime - 2 * UNITS_PER_EC;
            dev->endSeen = vdpGetCommandEndTime(dev->vdp);
            plan(dev);
            break;
        case PH_WAIT_CE:
            if (vdpGetCommandBusy(dev->vdp)) {
                boardTimerAdd(dev->timer, time + POLL_TICKS);
                return;
            }
            /* Read only after the engine was seen busy, so it is never stale. */
            dev->ceFall  = vdpGetCommandEndTime(dev->vdp);
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
                UInt32 seq = geo3dDebugLastSeq(dev->geo3d);
                RunStamp* s = &dev->stamps[seq % RUN_STAMPS];
                if (seq != 0 && s->seq == seq) {
                    s->end   = boardSystemTime() - (UInt32)((Int32)(now - dev->due) / UNITS_PER_TICK);
                    s->ended = 1;
                }
                if (DEBUG_TRACE_ON(DEBUG_TRACE_BIT(DEBUG_TRACE_GEO3D))) {
                    traceRunEnd(dev, seq, time, now);
                }
                geo3dFinishRun(dev->geo3d);
                dev->phase = PH_IDLE;
                boardTimerRemove(dev->timer);
                return;
            }
            /* A CPU command written while this one was overdue goes first, as
            ** if it had come first; ours waits for it rather than cutting in. */
            if (vdpGetCommandBusy(dev->vdp)) {
                dev->phase = PH_WAIT_CE;
                break;
            }
            /* One the CPU both started and finished since the plan moves it. */
            endTime = vdpGetCommandEndTime(dev->vdp);
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

/* Trace only: a new epoch counts the bytes fed from nothing again. */
static void traceFeedEpoch(Geo3DDevice* dev)
{
    UInt32 epoch = debugTraceEpoch();

    if (dev->traceFeedEpoch != epoch) {
        dev->traceFeedEpoch = epoch;
        dev->traceFeedModel = 0;
        dev->traceFeedOther = 0;
    }
}

static void startRun(Geo3DDevice* dev)
{
    UInt32 seq = geo3dDebugLastSeq(dev->geo3d);
    RunStamp* s = &dev->stamps[seq % RUN_STAMPS];

    s->seq   = seq;
    s->start = boardSystemTime();
    s->end   = 0;
    s->ended = 0;

    dev->runTime = boardSystemTime() * UNITS_PER_TICK;
    dev->issued  = 0;
    dev->faces   = geo3dFacesOn(dev->geo3d);
    dev->phase   = PH_START;
    if (debugRunSink != NULL && (dev == debugGeo3ds[0] || dev == debugGeo3ds[1])) {
        Geo3DDebugRunSink sink = debugRunSink;
        if (sink != NULL) {
            sink(dev->vdp != NULL, dev->epoch, seq, dev->geo3d, s->start);
        }
    }
    if (DEBUG_TRACE_ON(DEBUG_TRACE_BIT(DEBUG_TRACE_GEO3D))) {
        UInt32 items = 0;
        UInt32 verts = 0;
        UInt32 xfEnd;
        geo3dDebugRunRange(dev->geo3d, seq, &items, &verts, &xfEnd);
        traceFeedEpoch(dev);
        debugTraceEmit(dev->traceSource, GEO3D_TRACE_FEED, s->start, 0, dev->traceFeedModel, dev->traceFeedOther);
        dev->traceFeedModel = 0;
        dev->traceFeedOther = 0;
        debugTraceEmit(dev->traceSource, GEO3D_TRACE_RUN | DEBUG_TRACE_BEGIN, s->start, 0, seq, items >> 16);
        debugTraceEmit(dev->traceSource, GEO3D_TRACE_RUN | DEBUG_TRACE_CONT, s->start, 0, items, verts);
    }
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
    SaveState* state = saveStateOpenForWrite(dev->stateName);
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
    saveStateSet(state, "faces",     dev->faces);

    saveStateClose(state);
    free(data);
}

static void loadState(Geo3DDevice* dev)
{
    SaveState* state = saveStateOpenForRead(dev->stateName);
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
    dev->faces     = saveStateGet(state, "faces",     0);
    dev->nextReady = saveStateGet(state, "nextReady", 0);
    dev->nextEnd   = saveStateGet(state, "nextEnd",   0);
    dev->due       = saveStateGet(state, "due",       0);

    saveStateClose(state);

    /* The core forgot its RUNs too. */
    memset(dev->stamps, 0, sizeof(dev->stamps));
    dev->epoch = ++debugEpochs;
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
    int i;

    for (i = 0; i < 2; i++) {
        if (debugGeo3ds[i] == dev) {
            debugGeo3ds[i] = NULL;
        }
    }
    /* Removing the cartridge while paused runs this on the UI thread. */
    emulatorTapBarrier();
    debugTraceSourceUnregister(dev->traceSource);

    deviceManagerUnregister(dev->deviceHandle);
    debugDeviceUnregister(dev->debugHandle);

    ioPortUnregister(dev->port,     dev);
    ioPortUnregister(dev->port + 2, dev);

    boardTimerDestroy(dev->timer);
    geo3dDestroy(dev->geo3d);

    free(dev);
}

static void reset(Geo3DDevice* dev)
{
    boardTimerRemove(dev->timer);
    geo3dReset(dev->geo3d);
    dev->phase = PH_IDLE;
    memset(dev->stamps, 0, sizeof(dev->stamps));
    dev->epoch = ++debugEpochs;
}

/* Edges of the CPU polling RUN busy on the status port, from the status it is given. */
static void traceWait(Geo3DDevice* dev, int busy)
{
    UInt32 epoch = debugTraceEpoch();

    if (dev->traceEpoch != epoch) {
        dev->traceEpoch   = epoch;
        dev->traceWaiting = 0;
    }
    if (busy && dev->traceWaiting) {
        dev->traceBusyReads++;
    }
    else if (busy) {
        dev->traceWaiting   = 1;
        dev->traceBusyReads = 1;
        debugTraceEmit(dev->traceSource, GEO3D_TRACE_WAIT | DEBUG_TRACE_BEGIN, boardSystemTime(), 0, 0,
                       geo3dDebugLastSeq(dev->geo3d));
    }
    else if (dev->traceWaiting) {
        dev->traceWaiting = 0;
        debugTraceEmit(dev->traceSource, GEO3D_TRACE_WAIT | DEBUG_TRACE_END, boardSystemTime(), 0,
                       dev->traceBusyReads, geo3dDebugLastSeq(dev->geo3d));
    }
}

/* 9Dh is the index when written and the status when read, 9Fh the data; 8Dh
** and 8Fh on the cartridge. */
static UInt8 read(Geo3DDevice* dev, UInt16 ioPort)
{
    UInt32 ec;
    UInt32 edgeEnd;
    UInt8 status;

    if (geo3dIsRunning(dev->geo3d)) {
        pump(dev);
    }
    statusClock(dev, &ec, &edgeEnd);
    if (ioPort & 0x02) {
        return geo3dReadData(dev->geo3d, ec, edgeEnd);
    }
    status = geo3dReadStatus(dev->geo3d, ec, edgeEnd);
    if (DEBUG_TRACE_ON(DEBUG_TRACE_BIT(DEBUG_TRACE_GEO3D))) {
        traceWait(dev, status & 0x01);
    }
    return status;
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
    if (DEBUG_TRACE_ON(DEBUG_TRACE_BIT(DEBUG_TRACE_GEO3D))) {
        UInt8 index = geo3dDebugIndex(dev->geo3d);
        traceFeedEpoch(dev);
        if (index >= 0x50 && index <= 0x53) {
            dev->traceFeedModel++;
        }
        else {
            dev->traceFeedOther++;
        }
    }
    geo3dWriteData(dev->geo3d, value);
    if (!wasRunning && geo3dIsRunning(dev->geo3d)) {
        startRun(dev);
    }
}

static void getDebugInfo(Geo3DDevice* dev, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts;

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, "geo3d", 2);
    dbgIoPortsAddPort(ioPorts, 0, dev->port,     DBG_IO_READWRITE, peek(dev, dev->port));
    dbgIoPortsAddPort(ioPorts, 1, dev->port + 2, DBG_IO_READWRITE, peek(dev, dev->port + 2));
}

/* The machine's own registers with the device manager; a cartridge's is run
** by its mapper. */
static Geo3DDevice* create(int port, struct VDP* vdp, const char* stateName, const char* name, int machine)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    Geo3DDevice* dev = calloc(1, sizeof(Geo3DDevice));
    char traceName[32];

    dev->port      = port;
    dev->vdp       = vdp;
    dev->stateName = stateName;

    if (machine) {
        dev->deviceHandle = deviceManagerRegister(ROM_V9958, &callbacks, dev);
    }
    dev->debugHandle = debugDeviceRegister(DBGTYPE_VIDEO, name, &dbgCallbacks, dev);

    dev->geo3d = geo3dCreate();
    dev->timer = boardTimerCreate(onTimer, dev);

    /* Its VDP's trace name and " geo3d", as "VDP geo3d", so a tool can pair them. */
    _snprintf(traceName, sizeof(traceName) - 1, "%s geo3d", vdpGetTraceName(vdp));
    traceName[sizeof(traceName) - 1] = 0;
    dev->traceSource = debugTraceSourceRegister(DEBUG_TRACE_GEO3D, &traceClass, traceName);

    ioPortRegister(port,     read, write, dev);
    ioPortRegister(port + 2, read, write, dev);

    reset(dev);

    if (debugGeo3ds[machine ? 0 : 1] == NULL) {
        debugGeo3ds[machine ? 0 : 1] = dev;
    }
    return dev;
}

void geo3dDeviceCreate()
{
    create(0x9d, NULL, "Geo3DDevice", "geo3d", 1);
}

Geo3DDevice* geo3dDeviceCreateSecondary(int port, struct VDP* vdp, const char* stateName, const char* name)
{
    return create(port, vdp, stateName, name, 0);
}

void geo3dDeviceDestroy(Geo3DDevice* dev)
{
    destroy(dev);
}

void geo3dDeviceReset(Geo3DDevice* dev)
{
    reset(dev);
}

void geo3dDeviceSaveState(Geo3DDevice* dev)
{
    saveState(dev);
}

void geo3dDeviceLoadState(Geo3DDevice* dev)
{
    loadState(dev);
}

/* RUN seq, last the latest, still has its stamp and its record in the core. */
static int kept(Geo3DDevice* dev, UInt32 last, UInt32 seq)
{
    return seq != 0 && last - seq < RUN_STAMPS && dev->stamps[seq % RUN_STAMPS].seq == seq &&
           geo3dDebugHasRun(dev->geo3d, seq);
}

int geo3dDebugCapture(struct VDP* vdp, Geo3DDebugState* out)
{
    Geo3DDevice* dev = NULL;
    UInt32 seqs[GEO3D_DEBUG_RUNS];
    UInt64 hint = 0;
    UInt32 start;
    UInt32 now;
    UInt32 window;
    UInt32 last;
    UInt32 since;
    UInt32 pageFrom = 0;
    UInt32 lo;
    UInt32 seq;
    UInt32 ec;
    UInt32 edgeEnd;
    int frame = 0;
    int count = 0;
    int seen = 0;
    int lost = 0;
    int i;

    for (i = 0; i < 2 && dev == NULL; i++) {
        Geo3DDevice* d = debugGeo3ds[i];
        if (d != NULL && d->vdp == vdp) {
            dev = d;
            hint = debugSince[i];
        }
    }
    memset(out, 0, sizeof(*out));
    if (dev == NULL) {
        return 0;
    }
    now     = boardSystemTime();
    start   = vdpGetFrameStartTime(dev->vdp);
    window  = now - start;

    /* RUNs follow one another: walk back from the latest to the first that
    ** was over before the window. Ages back from now stay right for about
    ** 200 s of board time; one no longer recorded counts as lost. */
    last = geo3dDebugLastSeq(dev->geo3d);
    for (seq = last; seq != 0; seq--) {
        const RunStamp* s = &dev->stamps[seq % RUN_STAMPS];
        if (!kept(dev, last, seq)) {
            const RunStamp* after = &dev->stamps[(seq + 1) % RUN_STAMPS];
            lost += seq != last && (UInt32)(now - after->start) < window;
            break;
        }
        if (s->ended && (UInt32)(now - s->end) >= window) {
            break;
        }
        frame++;
    }

    /* Also every RUN after the one the tool holds, if it is of this epoch and
    ** not ahead; else, with none busy, the latest. Those it lacks, oldest
    ** first, get the room first, so a tool that asks again for one cut comes to have it whole. */
    since = last;
    if ((UInt32)(hint >> 32) == dev->epoch && (UInt32)hint < last) {
        since = (UInt32)hint;
    }
    /* Of another epoch, as after the state load of a reverse play step: from the start of the
    ** RUNs on the page drawn before the latest one's, so a replay that drew the picture on
    ** screen again hands it over whole before the one in progress. */
    else if ((UInt32)(hint >> 32) != dev->epoch) {
        int turns = 0, page = geo3dDebugRunYpage(dev->geo3d, last) >> 8;
        for (since = last; since != 0 && turns < 2; since--) {
            int at = geo3dDebugRunYpage(dev->geo3d, since - 1);
            if (at < 0 || (at >> 8) != page) {
                turns++;
                page = at >> 8;
                if (at < 0 || turns == 2) {
                    pageFrom = at < 0 ? 0 : since;
                    since--;
                    break;
                }
            }
        }
    }
    lo = last - frame + 1;
    if (since + 1 < lo) {
        lo = since + 1;
    }
    while (lo <= last && !kept(dev, last, lo)) {
        lo++;
    }
    if (lo > last && kept(dev, last, last)) {
        lo = last;
    }
    for (seq = lo; seq != 0 && seq <= last && count < GEO3D_DEBUG_RUNS; seq++) {
        seqs[count++] = seq;
    }
    if (since >= lo && since < last) {
        seen = (int)(since - lo + 1);
    }

    statusClock(dev, &ec, &edgeEnd);
    geo3dDebugCopy(dev->geo3d, ec, edgeEnd, seqs, count, seen, out);
    for (i = 0; i < out->runCount; i++) {
        const RunStamp* s = &dev->stamps[out->run[i].seq % RUN_STAMPS];
        if (out->run[i].seq != 0 && s->seq == out->run[i].seq) {
            out->run[i].startTime = s->start;
            out->run[i].endTime   = s->end;
            out->run[i].ended     = s->ended;
        }
    }
    out->valid       = 1;
    out->cartridge   = dev->vdp != NULL;
    out->port        = dev->port;
    out->devPhase    = dev->phase;
    out->epoch       = dev->epoch;
    out->windowStart = start;
    out->windowEnd   = now;
    out->frameFirst  = out->runCount - (frame < out->runCount ? frame : out->runCount);
    out->fallback    = frame == 0 && out->runCount > 0;
    out->runsLost    = lost;
    out->wholeSeq    = out->runCount > 0 ? seqs[seen] : 0;
    out->pageFrom    = pageFrom;
    return 1;
}

void geo3dDebugSince(int cartridge, UInt32 epoch, UInt32 seq)
{
    /* One aligned 64-bit store, so a capture never reads half of it. */
    debugSince[cartridge ? 1 : 0] = ((UInt64)epoch << 32) | seq;
}

void geo3dDebugSetRunSink(Geo3DDebugRunSink sink)
{
    debugRunSink = sink;
}
