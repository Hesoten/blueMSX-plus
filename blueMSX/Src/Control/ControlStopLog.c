/*****************************************************************************
**
** Machine stop log.
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
#include "ControlStopLog.h"
#include "ControlClient.h"
#include "ControlPeer.h"
#include "ArchEvent.h"
#include "Debugger.h"
#include "Emulator.h"
#include <string.h>

#define CTRL_STOP_RING 256

static CtrlStop ring[CTRL_STOP_RING];
static UInt32 nextSeq = 1;
static UInt32 dropped;
static void*  arrived;
/* A leaf lock over the ring: stops are posted from three threads and read from another. */
static void*  stopLock;

static void lock(void)
{
    archSemaphoreWait(stopLock, -1);
}

static void unlock(void)
{
    archSemaphoreSignal(stopLock);
}

void ctrlStopLogCreate(void)
{
    if (arrived == NULL) {
        arrived = archEventCreate(0);
        stopLock = archSemaphoreCreate(1);
    }
    memset(ring, 0, sizeof(ring));
    nextSeq = 1;
    dropped = 0;
}

const char* ctrlStopReasonText(int reason)
{
    switch (reason) {
    case CTRL_STOP_USER:       return "user";
    case CTRL_STOP_STEP:       return "step";
    case CTRL_STOP_BREAKPOINT: return "breakpoint";
    case CTRL_STOP_MACHINE:    return "machine";
    case CTRL_STOP_WATCH:      return "watchpoint";
    case CTRL_STOP_VRAM_TIMING: return "vram_timing";
    case CTRL_STOP_STEP_BACK:  return "step_back";
    default:                   return "unknown";
    }
}

int ctrlStopReasonOf(int cause)
{
    switch (cause) {
    case DBG_STOP_STEP:        return CTRL_STOP_STEP;
    case DBG_STOP_BREAKPOINT:  return CTRL_STOP_BREAKPOINT;
    case DBG_STOP_WATCH:       return CTRL_STOP_WATCH;
    case DBG_STOP_VRAM_TIMING: return CTRL_STOP_VRAM_TIMING;
    case DBG_STOP_STEP_BACK:   return CTRL_STOP_STEP_BACK;
    default:                   return CTRL_STOP_USER;
    }
}

void ctrlStopPost(int reason, UInt32 runGeneration, int watchType, UInt32 watchAddress,
                  int watchMode, int watchKind)
{
    UInt32 seq;
    CtrlStop* slot;

    lock();
    seq  = nextSeq;
    slot = &ring[seq % CTRL_STOP_RING];
    if (slot->seq != 0 && seq >= CTRL_STOP_RING) {
        dropped++;
    }

    slot->seq           = seq;
    slot->reason        = (UInt8)reason;
    slot->runGeneration = runGeneration;
    slot->watchType     = (UInt8)watchType;
    slot->watchAddress  = watchAddress;
    slot->watchMode     = (Int8)watchMode;
    slot->watchKind     = (UInt8)watchKind;

    nextSeq = seq + 1;
    unlock();

    if (arrived != NULL) {
        archEventSet(arrived);
    }
}

UInt32 ctrlStopSeq(void)
{
    UInt32 seq;

    lock();
    seq = nextSeq - 1;
    unlock();
    return seq;
}

UInt32 ctrlStopDropped(void)
{
    UInt32 count;

    lock();
    count = dropped;
    unlock();
    return count;
}

int ctrlStopGet(UInt32 afterSeq, CtrlStop* out)
{
    UInt32 want = afterSeq + 1;
    int    got = 0;

    lock();
    if (nextSeq > want) {
        /* Older stops have been overwritten; resume from the oldest still held. */
        UInt32 oldest = nextSeq > CTRL_STOP_RING ? nextSeq - CTRL_STOP_RING : 1;

        if (want < oldest) {
            want = oldest;
        }
        *out = ring[want % CTRL_STOP_RING];
        got = out->seq == want;
    }
    unlock();
    return got;
}

int ctrlStopWait(UInt32 afterSeq, int timeoutMs, CtrlStop* out)
{
    int waited = 0;

    if (ctrlStopGet(afterSeq, out)) {
        return 1;
    }
    if (arrived == NULL) {
        return 0;
    }

    /* Poll in slices: the event may have been set by a stop this caller already read. */
    for (;;) {
        int slice = timeoutMs - waited;
        if (slice <= 0) {
            return 0;
        }
        if (slice > 20) {
            slice = 20;
        }
        archEventWait(arrived, slice);
        waited += slice;

        if (ctrlStopGet(afterSeq, out)) {
            return 1;
        }
        /* Agent control was turned off, or the client hung up; the caller tells which. */
        if (ctrlClientAllBlocked() || ctrlPeerGone()) {
            return 0;
        }
    }
}

int ctrlStopLatest(CtrlStop* out)
{
    UInt32 seq;
    int    got = 0;

    lock();
    seq = nextSeq - 1;
    if (seq != 0) {
        *out = ring[seq % CTRL_STOP_RING];
        got = out->seq == seq;
    }
    unlock();
    /* A resume, start or reset since then means the machine has moved on. */
    return got && out->runGeneration == emulatorGetStateSeq();
}

void ctrlStopRelabel(UInt32 seq, int reason)
{
    CtrlStop* slot = &ring[seq % CTRL_STOP_RING];

    lock();
    if (slot->seq == seq) {
        slot->reason = (UInt8)reason;
    }
    unlock();
}
