/*****************************************************************************
**
** Debugger breakpoint tables.
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
#include "Debugger.h"
#include "Board.h"
#include "SlotManager.h"
#include "ArchEvent.h"
#include "ArchThread.h"

/* No reference counts: plugins re-arm without clearing first, so arming must
** stay idempotent. An address is disarmed once nothing, plain or segment, wants it. */
#define MAX_SEGMENT_BREAKPOINTS 64

/* Read by the emulation thread with no lock, on every hit: an entry is filled in before it is
** in use and armed, and out of use before it is disarmed. A hit while a freed entry is reused
** may read it half old, half new, which can misjudge that one hit only. */
typedef struct {
    volatile int inUse;
    volatile int slot;
    volatile int subSlot;
    volatile int segment;
    volatile int offset;
    /* Kept, not recomputed: the slot layout may change after arming. */
    UInt16 armed[8];
    int    armedCount;
} SegmentBreakpoint;

static SegmentBreakpoint segmentBp[MAX_SEGMENT_BREAKPOINTS];
static volatile int   segmentBpCount;
static volatile UInt8 plainBreakpoint[0x10000];
/* Every writer takes it, whichever thread: the window's or the emulation one. */
static void* writerLock;

void dbgBreakpointsCreateLock(void)
{
    if (writerLock == NULL) {
        writerLock = archSemaphoreCreate(1);
    }
}

static void lockWriters(void)
{
    archSemaphoreWait(writerLock, -1);
}

static void unlockWriters(void)
{
    archSemaphoreSignal(writerLock);
}

/* Segment size in bytes, or one 8kB bank when the slot reports none. */
static int segmentSizeOrBankSize(int slot, int subSlot)
{
    int segmentSize = slotDebugGetSegmentSize(slot, subSlot);

    return segmentSize > 0 ? segmentSize : 0x2000;
}

static int someSegmentWants(UInt16 address)
{
    int i;
    int n;

    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (!segmentBp[i].inUse) {
            continue;
        }
        for (n = 0; n < segmentBp[i].armedCount; n++) {
            if (segmentBp[i].armed[n] == address) {
                return 1;
            }
        }
    }

    return 0;
}

/* Every bank that can show the segment, as a device claims it even while it reports none
** (SRAM paged in): a switch there arms nothing, so dbgBreakpointAllows tells the hits apart. */
static void armSegment(SegmentBreakpoint* bp)
{
    int unitSize = segmentSizeOrBankSize(bp->slot, bp->subSlot);
    int banksPerSegment = unitSize / 0x2000;
    int bank;

    bp->armedCount = 0;

    for (bank = 0; bank + banksPerSegment <= 8; bank += banksPerSegment) {
        int candidate;

        if (slotDebugGetBankSegment(bp->slot, bp->subSlot, bank) < 0 &&
            (slotDebugGetBankState(bp->slot, bp->subSlot, bank) & SLOT_DEBUG_REGISTERED) == 0)
        {
            continue;
        }
        candidate = bank * 0x2000 + bp->offset;

        if (candidate >= 0 && candidate < 0x10000) {
            bp->armed[bp->armedCount++] = (UInt16)candidate;
            boardSetBreakpoint((UInt16)candidate);
        }
    }
}

/* Arms bp again where its segment is now, or retires it, then clears the addresses it was
** armed at that no breakpoint wants any more. */
static void rearmSegment(SegmentBreakpoint* bp, int keep)
{
    UInt16 was[8];
    int count = bp->armedCount;
    int n;

    for (n = 0; n < count; n++) {
        was[n] = bp->armed[n];
    }

    if (keep) {
        armSegment(bp);
    }
    else {
        /* Retire it first so that someSegmentWants does not count it, nor a hit. */
        bp->armedCount = 0;
        bp->inUse = 0;
        archMemoryBarrier();
    }

    for (n = 0; n < count; n++) {
        if (!plainBreakpoint[was[n]] && !someSegmentWants(was[n])) {
            boardClearBreakpoint(was[n]);
        }
    }
}

static void disarmSegment(SegmentBreakpoint* bp)
{
    rearmSegment(bp, 0);
}

void dbgSetBreakpoint(UInt16 address)
{
    lockWriters();
    plainBreakpoint[address] = 1;
    boardSetBreakpoint(address);
    unlockWriters();
}

void dbgClearBreakpoint(UInt16 address)
{
    lockWriters();
    plainBreakpoint[address] = 0;
    if (!someSegmentWants(address)) {
        boardClearBreakpoint(address);
    }
    unlockWriters();
}

int dbgSetSegmentBreakpoint(int slot, int subSlot, int segment, int offset)
{
    int slotFree = -1;
    int alreadySet = 0;
    int i;

    if (slot < 0 || slot > 3 || subSlot < 0 || subSlot > 3 || segment < 0) {
        return 0;
    }
    if (offset < 0 || offset >= segmentSizeOrBankSize(slot, subSlot)) {
        return 0;
    }

    lockWriters();
    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (!segmentBp[i].inUse) {
            if (slotFree < 0) {
                slotFree = i;
            }
        }
        else if (segmentBp[i].slot    == slot &&
                 segmentBp[i].subSlot == subSlot &&
                 segmentBp[i].segment == segment &&
                 segmentBp[i].offset  == offset)
        {
            /* Already set: a duplicate would stay armed after one clear. */
            alreadySet = 1;
            break;
        }
    }

    if (!alreadySet && slotFree >= 0) {
        segmentBp[slotFree].slot    = slot;
        segmentBp[slotFree].subSlot = subSlot;
        segmentBp[slotFree].segment = segment;
        segmentBp[slotFree].offset  = offset;
        archMemoryBarrier();
        segmentBp[slotFree].inUse   = 1;
        segmentBpCount++;
        armSegment(&segmentBp[slotFree]);
    }
    unlockWriters();

    return alreadySet || slotFree >= 0;
}

int dbgClearSegmentBreakpoint(int slot, int subSlot, int segment, int offset)
{
    int found = 0;
    int i;

    lockWriters();
    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (segmentBp[i].inUse &&
            segmentBp[i].slot    == slot &&
            segmentBp[i].subSlot == subSlot &&
            segmentBp[i].segment == segment &&
            segmentBp[i].offset  == offset)
        {
            disarmSegment(&segmentBp[i]);
            segmentBpCount--;
            found = 1;
            break;
        }
    }
    unlockWriters();

    return found;
}

void dbgBreakpointsOnEmulatorStart(void)
{
    int i;

    /* The new CPU has none: plugins set theirs again after this. */
    lockWriters();
    for (i = 0; i < 0x10000; i++) {
        plainBreakpoint[i] = 0;
    }

    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (segmentBp[i].inUse) {
            armSegment(&segmentBp[i]);
        }
    }
    unlockWriters();
}

void dbgBreakpointsOnCartridgeChanged(void)
{
    int i;

    if (segmentBpCount == 0) {
        return;
    }
    /* A cartridge change reports other segments: arm where they are now, drop the rest. */
    lockWriters();
    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (segmentBp[i].inUse) {
            rearmSegment(&segmentBp[i], 1);
        }
    }
    unlockWriters();
}

int dbgBreakpointAllows(UInt16 address)
{
    int unitSize;
    int bank;
    int page;
    int slot;
    int subSlot;
    int segment;
    int i;

    if (segmentBpCount == 0 || plainBreakpoint[address]) {
        return 1;
    }

    page     = address >> 14;
    slot     = slotDebugGetPagePrimarySlot(page);
    subSlot  = slotDebugGetPageSubSlot(page);
    unitSize = segmentSizeOrBankSize(slot, subSlot);
    bank     = (address / unitSize) * (unitSize / 0x2000);
    segment  = slotDebugGetBankSegment(slot, subSlot, bank);

    if (segment < 0) {
        return 0;
    }

    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (segmentBp[i].inUse &&
            segmentBp[i].slot    == slot &&
            segmentBp[i].subSlot == subSlot &&
            segmentBp[i].segment == segment &&
            (UInt16)(bank * 0x2000 + segmentBp[i].offset) == address)
        {
            return 1;
        }
    }

    return 0;
}
