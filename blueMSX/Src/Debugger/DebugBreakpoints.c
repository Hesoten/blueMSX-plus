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
#include "DebugDeviceManager.h"
#include "Emulator.h"
#include "R800Debug.h"
#include <string.h>

/* No reference counts: plugins re-arm without clearing first, so arming must
** stay idempotent. An address is disarmed once nothing (plain, segment or a step stop) wants it. */
#define MAX_SEGMENT_BREAKPOINTS 64
/* Plain breakpoints that have a condition or skip; the others need no record. */
#define MAX_PLAIN_RECORDS DBG_COND_PLAIN_MAX

/* A condition or skip and its counts, read without a lock: a hit while it is replaced can
** misjudge that one hit only. Only the emulation thread writes the counts; a writer zeroes
** them by bumping resetAsked, and they read as 0 until a hit copies it to resetDone. */
typedef struct {
    volatile int      active;
    DbgBreakCondition cond;
    volatile UInt32   hits;
    volatile UInt32   matched;
    volatile UInt32   resetAsked;
    volatile UInt32   resetDone;
} BreakRecord;

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
    /* Who set it: the agent's entry has a record, and one a plugin set always stops. */
    volatile int byAgent;
    volatile int byOther;
    BreakRecord record;
} SegmentBreakpoint;

static SegmentBreakpoint segmentBp[MAX_SEGMENT_BREAKPOINTS];
static volatile int   segmentBpCount;
static volatile UInt8 plainBreakpoint[0x10000];
/* Of the plain ones, those the control socket set, which outlive a machine start, and
** those set through dbgSetBreakpoint, as by the debugger window. One can be both. */
static UInt8 keptBreakpoint[0x10000];
static UInt8 unkeptBreakpoint[0x10000];
static BreakRecord plainRecord[MAX_PLAIN_RECORDS];
/* For each kept plain breakpoint, 1 + the index of its record in plainRecord, or 0 for none. */
static volatile UInt8 plainRecordAt[0x10000];
/* Where a step over or out stops, whatever the breakpoints there decide. */
static volatile UInt8 stepStop[0x10000];
static DbgBreakHit lastHit;
/* Bumped by every fill, so each set of a condition has its own resetAsked. */
static UInt32 fillStamp;
/* Every writer takes it, whichever thread: the window's, the agent's, the emulation one. */
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

static int someoneWants(UInt16 address)
{
    return plainBreakpoint[address] || stepStop[address] || someSegmentWants(address);
}

/* Every bank that can show the segment, as a device claims it even while it reports none
** (SRAM paged in): a switch there arms nothing, so dbgBreakpointDecide tells the hits apart. */
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
        if (!someoneWants(was[n])) {
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
    unkeptBreakpoint[address] = 1;
    boardSetBreakpoint(address);
    unlockWriters();
}

static int hasCondition(const DbgBreakCondition* cond)
{
    return cond != NULL && (cond->opCount > 0 || cond->skip > 0);
}

/* Replaces the record with cond and zeroes its counts, then makes it active if cond has
** anything; a hit meanwhile sees it inactive, as a breakpoint with no condition. */
static void fillRecord(BreakRecord* record, const DbgBreakCondition* cond)
{
    record->active = 0;
    archMemoryBarrier();
    if (hasCondition(cond)) {
        record->cond = *cond;
        record->resetAsked = ++fillStamp;
        archMemoryBarrier();
        record->active = 1;
    }
}

/* Unhooks the record of a kept plain breakpoint before it is freed for reuse. */
static void dropPlainRecord(UInt16 address)
{
    int was = plainRecordAt[address];

    plainRecordAt[address] = 0;
    archMemoryBarrier();
    if (was > 0) {
        plainRecord[was - 1].active = 0;
    }
}

int dbgSetKeptBreakpoint(UInt16 address, const DbgBreakCondition* cond)
{
    int was;
    int index = -1;
    int i;

    lockWriters();
    was = plainRecordAt[address];
    if (hasCondition(cond)) {
        /* A fresh record keeps the old one whole for a hit meanwhile; with none left, in place. */
        for (i = 0; i < MAX_PLAIN_RECORDS && index < 0; i++) {
            if (!plainRecord[i].active) {
                index = i;
            }
        }
        if (index < 0 && was > 0) {
            index = was - 1;
        }
        if (index < 0) {
            unlockWriters();
            return 0;
        }
        fillRecord(&plainRecord[index], cond);
        plainRecordAt[address] = (UInt8)(index + 1);
        archMemoryBarrier();
        if (was > 0 && was - 1 != index) {
            plainRecord[was - 1].active = 0;
        }
    }
    else {
        dropPlainRecord(address);
    }
    plainBreakpoint[address] = 1;
    keptBreakpoint[address] = 1;
    boardSetBreakpoint(address);
    unlockWriters();
    return 1;
}

/* After one source dropped it: still plain if the other wants it, still armed if a segment one does. */
static void dropPlainUnlessWanted(UInt16 address)
{
    plainBreakpoint[address] = keptBreakpoint[address] || unkeptBreakpoint[address];
    if (!someoneWants(address)) {
        boardClearBreakpoint(address);
    }
}

int dbgClearKeptBreakpoint(UInt16 address)
{
    int wasKept;

    lockWriters();
    wasKept = keptBreakpoint[address];
    keptBreakpoint[address] = 0;
    dropPlainRecord(address);
    dropPlainUnlessWanted(address);
    unlockWriters();
    return wasKept;
}

void dbgClearKeptBreakpoints(void)
{
    int i;

    lockWriters();
    for (i = 0; i < 0x10000; i++) {
        if (keptBreakpoint[i]) {
            keptBreakpoint[i] = 0;
            dropPlainRecord((UInt16)i);
            dropPlainUnlessWanted((UInt16)i);
        }
    }
    unlockWriters();
}

void dbgSetStepStop(UInt16 address)
{
    lockWriters();
    stepStop[address] = 1;
    boardSetBreakpoint(address);
    unlockWriters();
}

void dbgClearStepStop(UInt16 address)
{
    lockWriters();
    stepStop[address] = 0;
    if (!someoneWants(address)) {
        boardClearBreakpoint(address);
    }
    unlockWriters();
}

int dbgHasBreakpoint(UInt16 address)
{
    return plainBreakpoint[address];
}

void dbgClearBreakpoint(UInt16 address)
{
    lockWriters();
    unkeptBreakpoint[address] = 0;
    dropPlainUnlessWanted(address);
    unlockWriters();
}

int dbgFindSegmentBreakpoint(int slot, int subSlot, int segment, int offset)
{
    int i;

    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (segmentBp[i].inUse &&
            segmentBp[i].slot    == slot &&
            segmentBp[i].subSlot == subSlot &&
            segmentBp[i].segment == segment &&
            segmentBp[i].offset  == offset)
        {
            return i;
        }
    }
    return -1;
}

/* The segment paged in at address, or -1, with the slot and the first bank it fills. */
static int segmentPagedAt(UInt16 address, int* slot, int* subSlot, int* bank)
{
    int page = address >> 14;
    int unitSize;

    *slot    = slotDebugGetPagePrimarySlot(page);
    *subSlot = slotDebugGetPageSubSlot(page);
    unitSize = segmentSizeOrBankSize(*slot, *subSlot);
    *bank    = (address / unitSize) * (unitSize / 0x2000);
    return slotDebugGetBankSegment(*slot, *subSlot, *bank);
}

/* For the agent (byAgent), with cond as its own record and zeroed counts; for a plugin,
** marked as one that always stops, leaving the agent's record as it is. */
static int setSegmentBreakpoint(int slot, int subSlot, int segment, int offset,
                                const DbgBreakCondition* cond, int byAgent)
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
    /* Already set: a duplicate would stay armed after one clear. */
    if ((i = dbgFindSegmentBreakpoint(slot, subSlot, segment, offset)) >= 0) {
        alreadySet = 1;
        if (byAgent) {
            fillRecord(&segmentBp[i].record, cond);
            segmentBp[i].byAgent = 1;
        }
        else {
            segmentBp[i].byOther = 1;
        }
    }
    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS && slotFree < 0; i++) {
        if (!segmentBp[i].inUse) {
            slotFree = i;
        }
    }

    if (!alreadySet && slotFree >= 0) {
        segmentBp[slotFree].slot    = slot;
        segmentBp[slotFree].subSlot = subSlot;
        segmentBp[slotFree].segment = segment;
        segmentBp[slotFree].offset  = offset;
        fillRecord(&segmentBp[slotFree].record, byAgent ? cond : NULL);
        segmentBp[slotFree].byAgent = byAgent;
        segmentBp[slotFree].byOther = !byAgent;
        archMemoryBarrier();
        segmentBp[slotFree].inUse   = 1;
        segmentBpCount++;
        armSegment(&segmentBp[slotFree]);
    }
    unlockWriters();

    return alreadySet || slotFree >= 0;
}

int dbgSetSegmentBreakpoint(int slot, int subSlot, int segment, int offset)
{
    return setSegmentBreakpoint(slot, subSlot, segment, offset, NULL, 0);
}

int dbgSetSegmentBreakpointIf(int slot, int subSlot, int segment, int offset,
                              const DbgBreakCondition* cond)
{
    return setSegmentBreakpoint(slot, subSlot, segment, offset, cond, 1);
}

int dbgSegmentBreakpointAlwaysStops(int slot, int subSlot, int segment, int offset)
{
    int index = dbgFindSegmentBreakpoint(slot, subSlot, segment, offset);

    return index >= 0 && segmentBp[index].byOther;
}

int dbgOtherBreakpointAt(UInt16 address)
{
    int slot;
    int subSlot;
    int bank;
    int segment;
    int index;

    if (unkeptBreakpoint[address]) {
        return 1;
    }
    if (segmentBpCount == 0 || (segment = segmentPagedAt(address, &slot, &subSlot, &bank)) < 0) {
        return 0;
    }
    index = dbgFindSegmentBreakpoint(slot, subSlot, segment, address - bank * 0x2000);
    return index >= 0 && segmentBp[index].byOther;
}

int dbgClearSegmentBreakpoint(int slot, int subSlot, int segment, int offset)
{
    int index;

    lockWriters();
    if ((index = dbgFindSegmentBreakpoint(slot, subSlot, segment, offset)) >= 0) {
        disarmSegment(&segmentBp[index]);
        segmentBpCount--;
    }
    unlockWriters();

    return index >= 0;
}

void dbgClearAllSegmentBreakpoints(void)
{
    int i;

    lockWriters();
    for (i = 0; i < MAX_SEGMENT_BREAKPOINTS; i++) {
        if (segmentBp[i].inUse) {
            disarmSegment(&segmentBp[i]);
            segmentBpCount--;
        }
    }
    unlockWriters();
}

int dbgSegmentBreakpointCount(void)
{
    return segmentBpCount;
}

/* Without the lock: a reader on another thread at worst sees an entry as it is retired. */
int dbgGetSegmentBreakpoint(int index, int* slot, int* subSlot, int* segment, int* offset)
{
    if (index < 0 || index >= MAX_SEGMENT_BREAKPOINTS || !segmentBp[index].inUse) {
        return 0;
    }
    *slot    = segmentBp[index].slot;
    *subSlot = segmentBp[index].subSlot;
    *segment = segmentBp[index].segment;
    *offset  = segmentBp[index].offset;
    return 1;
}

int dbgSegmentBreakpointCapacity(void)
{
    return MAX_SEGMENT_BREAKPOINTS;
}

static int listMarked(const volatile UInt8* marked, UInt16* out, int max)
{
    int count = 0;
    int address;

    for (address = 0; address < 0x10000 && count < max; address++) {
        if (marked[address]) {
            out[count++] = (UInt16)address;
        }
    }
    return count;
}

int dbgListBreakpoints(UInt16* out, int max)
{
    return listMarked(plainBreakpoint, out, max);
}

int dbgListKeptBreakpoints(UInt16* out, int max)
{
    return listMarked(keptBreakpoint, out, max);
}

/* The counts as of the last hit, or 0 when zeroed since. */
static void currentCounts(const BreakRecord* record, UInt32* asked, UInt32* hits,
                          UInt32* matched)
{
    *asked = record->resetAsked;
    if (record->resetDone == *asked) {
        *hits    = record->hits;
        *matched = record->matched;
    }
    else {
        *hits    = 0;
        *matched = 0;
    }
}

static int copyCounts(const BreakRecord* record, DbgBreakCounts* out)
{
    UInt32 asked;

    if (!record->active) {
        return 0;
    }
    currentCounts(record, &asked, &out->hits, &out->matched);
    out->skip  = record->cond.skip;
    out->stamp = asked;
    return 1;
}

int dbgGetKeptBreakpointCounts(UInt16 address, DbgBreakCounts* out)
{
    int index = plainRecordAt[address];

    return keptBreakpoint[address] && index > 0 && copyCounts(&plainRecord[index - 1], out);
}

int dbgGetSegmentBreakpointCounts(int index, DbgBreakCounts* out)
{
    return index >= 0 && index < MAX_SEGMENT_BREAKPOINTS && segmentBp[index].inUse &&
           segmentBp[index].byAgent && copyCounts(&segmentBp[index].record, out);
}

void dbgBreakpointsOnEmulatorStart(void)
{
    int i;

    /* The new CPU has none: the kept ones are set here, and plugins set theirs after this. */
    lockWriters();
    for (i = 0; i < 0x10000; i++) {
        plainBreakpoint[i] = keptBreakpoint[i];
        unkeptBreakpoint[i] = 0;
        if (keptBreakpoint[i] || stepStop[i]) {
            boardSetBreakpoint((UInt16)i);
        }
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

static Int32 registerValue(const CpuRegs* regs, int which, UInt16 pc)
{
    if (regs == NULL) {
        return 0;
    }
    switch (which) {
    case DBG_REG_A:  return regs->AF.B.h;
    case DBG_REG_F:  return regs->AF.B.l;
    case DBG_REG_B:  return regs->BC.B.h;
    case DBG_REG_C:  return regs->BC.B.l;
    case DBG_REG_D:  return regs->DE.B.h;
    case DBG_REG_E:  return regs->DE.B.l;
    case DBG_REG_H:  return regs->HL.B.h;
    case DBG_REG_L:  return regs->HL.B.l;
    case DBG_REG_AF: return regs->AF.W;
    case DBG_REG_BC: return regs->BC.W;
    case DBG_REG_DE: return regs->DE.W;
    case DBG_REG_HL: return regs->HL.W;
    case DBG_REG_IX: return regs->IX.W;
    case DBG_REG_IY: return regs->IY.W;
    case DBG_REG_SP: return regs->SP.W;
    case DBG_REG_PC: return pc;
    case DBG_REG_ZF: return (regs->AF.B.l >> 6) & 1;
    case DBG_REG_CF: return regs->AF.B.l & 1;
    default:         return 0;
    }
}

/* One operator on the top of the stack; 0 for an op it does not know or a stack too shallow. */
static int applyOp(int op, Int32* stack, int* depth)
{
    UInt32 a;
    UInt32 b;
    Int32  r;

    if (op >= DBG_COND_BYTE && op <= DBG_COND_NEG) {
        if (*depth < 1) {
            return 0;
        }
        a = (UInt32)stack[*depth - 1];
        switch (op) {
        case DBG_COND_BYTE: r = slotPeek(NULL, (UInt16)a); break;
        case DBG_COND_WORD:
            r = slotPeek(NULL, (UInt16)a) | (slotPeek(NULL, (UInt16)(a + 1)) << 8);
            break;
        case DBG_COND_NOT:  r = a == 0; break;
        case DBG_COND_CPL:  r = (Int32)~a; break;
        default:            r = (Int32)(0 - a); break;
        }
        stack[*depth - 1] = r;
        return 1;
    }
    if (*depth < 2) {
        return 0;
    }
    a = (UInt32)stack[*depth - 2];
    b = (UInt32)stack[*depth - 1];
    switch (op) {
    case DBG_COND_ADD:  r = (Int32)(a + b); break;
    case DBG_COND_SUB:  r = (Int32)(a - b); break;
    case DBG_COND_SHL:  r = b < 32 ? (Int32)(a << b) : 0; break;
    case DBG_COND_SHR:  r = b < 32 ? (Int32)a >> b : ((Int32)a < 0 ? -1 : 0); break;
    case DBG_COND_AND:  r = (Int32)(a & b); break;
    case DBG_COND_XOR:  r = (Int32)(a ^ b); break;
    case DBG_COND_OR:   r = (Int32)(a | b); break;
    case DBG_COND_EQ:   r = a == b; break;
    case DBG_COND_NE:   r = a != b; break;
    case DBG_COND_LT:   r = (Int32)a <  (Int32)b; break;
    case DBG_COND_LE:   r = (Int32)a <= (Int32)b; break;
    case DBG_COND_GT:   r = (Int32)a >  (Int32)b; break;
    case DBG_COND_GE:   r = (Int32)a >= (Int32)b; break;
    case DBG_COND_LAND: r = a != 0 && b != 0; break;
    case DBG_COND_LOR:  r = a != 0 || b != 0; break;
    default:            return 0;
    }
    stack[*depth - 2] = r;
    (*depth)--;
    return 1;
}

/* Runs the code with every op and the stack bounded, as a record being replaced may be
** half old, half new; code it cannot run counts as true. */
static int conditionHolds(const BreakRecord* record, UInt16 pc)
{
    Int32 stack[DBG_COND_MAX_STACK];
    const CpuRegs* regs = r800DebugRegisters();
    int count = record->cond.opCount;
    int depth = 0;
    int i;

    if (count <= 0 || count > DBG_COND_MAX_OPS) {
        return 1;
    }
    for (i = 0; i < count; i++) {
        int op = record->cond.ops[i].op;

        if (op == DBG_COND_CONST || op == DBG_COND_REG) {
            if (depth >= DBG_COND_MAX_STACK) {
                return 1;
            }
            stack[depth++] = op == DBG_COND_CONST
                           ? record->cond.ops[i].value
                           : registerValue(regs, record->cond.ops[i].value, pc);
        }
        else if (!applyOp(op, stack, &depth)) {
            return 1;
        }
    }
    return depth != 1 || stack[0] != 0;
}

#define DECIDE_PREDICT 0
#define DECIDE_COUNT   1
/* Each breakpoint as if it had no condition, with nothing counted. */
#define DECIDE_ANY     2

/* Whether one breakpoint stops, given its record or NULL. Returns 2 when it is the first
** with a record to stop, whose counts go to hit. */
static int breakpointStops(BreakRecord* record, UInt16 address, int mode, DbgBreakHit* hit)
{
    UInt32 asked;
    UInt32 hits;
    UInt32 matched;
    int    holds;

    if (record == NULL || !record->active || mode == DECIDE_ANY) {
        hit->byUser = 1;
        return 1;
    }
    holds = conditionHolds(record, address);
    currentCounts(record, &asked, &hits, &matched);
    hits++;
    matched += holds ? 1 : 0;
    if (mode == DECIDE_COUNT) {
        record->hits    = hits;
        record->matched = matched;
        archMemoryBarrier();
        record->resetDone = asked;
    }
    if (!holds || matched <= record->cond.skip) {
        return 0;
    }
    hit->byUser = 1;
    if (hit->counted) {
        return 1;
    }
    hit->counted = 1;
    hit->hits    = hits;
    hit->matched = matched;
    hit->stamp   = asked;
    return 2;
}

/* Every breakpoint at address is asked, so each counts its hit even after another stops. */
static int decide(UInt16 address, int mode, DbgBreakHit* hit)
{
    int stops = stepStop[address];
    int index;
    int slot;
    int subSlot;
    int bank;
    int segment;
    int stopped;
    int i;

    memset(hit, 0, sizeof(*hit));
    hit->address = address;
    hit->segment = -1;

    if (unkeptBreakpoint[address]) {
        stops = 1;
        hit->byUser = 1;
    }
    if (keptBreakpoint[address]) {
        index = plainRecordAt[address];
        if (breakpointStops(index > 0 && index <= MAX_PLAIN_RECORDS ? &plainRecord[index - 1]
                                                                    : NULL, address, mode, hit)) {
            stops = 1;
        }
    }
    if (segmentBpCount == 0 || (segment = segmentPagedAt(address, &slot, &subSlot, &bank)) < 0) {
        return stops;
    }
    /* One entry at most: a set finds the one there. */
    if ((i = dbgFindSegmentBreakpoint(slot, subSlot, segment, address - bank * 0x2000)) < 0) {
        return stops;
    }
    /* The agent's record is asked only for its own entry; a plugin's always stops. */
    stopped = segmentBp[i].byAgent ? breakpointStops(&segmentBp[i].record, address, mode, hit)
                                   : 0;
    if (segmentBp[i].byOther) {
        hit->byUser = 1;
        stopped = stopped != 0 ? stopped : 1;
    }
    if (stopped == 2) {
        hit->slot    = slot;
        hit->subSlot = subSlot;
        hit->segment = segment;
        hit->offset  = segmentBp[i].offset;
    }
    return stops || stopped != 0;
}

int dbgBreakpointDecide(UInt16 address, int count)
{
    DbgBreakHit hit;
    int stops;

    if (!count) {
        return decide(address, DECIDE_PREDICT, &hit);
    }
    /* No stop can park there, so it is no hit; nor is one a replay passes over. */
    if (debugDeviceIsInspecting()) {
        return 0;
    }
    if (emulatorDropsStops()) {
        return decide(address, DECIDE_ANY, &hit);
    }
    stops = decide(address, DECIDE_COUNT, &hit);
    /* Before the stop parks, as the pause reads it back. */
    if (stops) {
        lastHit = hit;
    }
    return stops;
}

void dbgBreakpointLastHit(DbgBreakHit* out)
{
    *out = lastHit;
}
