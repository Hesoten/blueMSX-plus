/*****************************************************************************
**
** Event trace of the emulated chips, for the debug tools.
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
#include "DebugTrace.h"
#include "ArchThread.h"
#include <windows.h>
#include <intrin.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING_MASK (DEBUG_TRACE_RING - 1)

/* A slot is being written while its seq is 0; seqs themselves skip 0. */
typedef struct {
    volatile UInt32 seq;
    UInt32 pad;
    DebugTraceRecord rec;
} Slot;

#if defined(_M_IX86) || defined(_M_X64)
#define STORE_FENCE() _ReadWriteBarrier()
#else
#define STORE_FENCE() MemoryBarrier()
#endif

volatile UInt32 debugTraceArmed;
volatile UInt8  debugTracePorts[DEBUG_TRACE_PORT_BYTES];

static SRWLOCK lock = SRWLOCK_INIT;
static Slot* ring;
static volatile UInt32 head;
static volatile UInt32 epoch;
static volatile LONG linkCount;
static volatile DWORD writer;
static volatile int rearm;
static int armCount[DEBUG_TRACE_CLASSES];
static int portHolds[DEBUG_TRACE_PORT_BYTES * 8];
static DebugTraceArmCb armCb;
static UInt64 anchor;
static DebugTraceSource sources[DEBUG_TRACE_SOURCES];
static int sourceCount = 1;

static const char* const reasonNames[] = { "start", "load", "reset", "stop", "arm", "rewind" };

static const DebugTraceField discontinuityFields[] = {
    { "reason", DEBUG_TRACE_WORD_A, 0, 32, DEBUG_TRACE_ENUM, 0, reasonNames, 6 },
    { "epoch",  DEBUG_TRACE_WORD_B, 0, 32, DEBUG_TRACE_DEC,  0, NULL, 0 },
    { "resume", DEBUG_TRACE_WORD_LINK, 0, 1, DEBUG_TRACE_DEC, 0, NULL, 0 },
};

static const DebugTraceKindDesc coreKinds[] = {
    { DEBUG_TRACE_KIND_DISCONTINUITY, "discontinuity", discontinuityFields, 3 },
};

static const DebugTraceClassDesc coreClass = { "core", coreKinds, 1 };

static UInt64 widen(UInt32 time)
{
    UInt64 t = anchor + (UInt64)(Int64)(Int32)(time - (UInt32)anchor);

    if ((Int64)(t - anchor) > 0) {
        anchor = t;
    }
    return t;
}

/* The low word becomes now, and the whole only ever moves forward. */
static void reanchor(UInt32 now)
{
    UInt64 next = (anchor & ~(UInt64)0xFFFFFFFF) | now;

    if (next < anchor) {
        next += (UInt64)1 << 32;
    }
    anchor = next;
}

static void put(UInt64 time, UInt16 source, UInt16 kind, UInt32 link, UInt32 a, UInt32 b)
{
    UInt32 s = head + 1;
    Slot* slot;

    if (s == 0) {
        s = 1;
    }
    slot = &ring[s & RING_MASK];
    slot->seq = 0;
    STORE_FENCE();
    slot->rec.time   = time;
    slot->rec.source = source;
    slot->rec.kind   = kind;
    slot->rec.link   = link;
    slot->rec.a      = a;
    slot->rec.b      = b;
    STORE_FENCE();
    slot->seq = s;
    head = s;
}

void debugTraceEmit(UInt16 source, UInt16 kind, UInt32 time, UInt32 link, UInt32 a, UInt32 b)
{
    if (writer != GetCurrentThreadId() || source == DEBUG_TRACE_NO_SOURCE) {
        return;
    }
    if (rearm) {
        rearm = 0;
        reanchor(time);
        put(anchor, 0, DEBUG_TRACE_KIND_DISCONTINUITY, 0, DEBUG_TRACE_ARM, epoch);
    }
    put(widen(time), source, kind, link, a, b);
}

void debugTraceTick(UInt32 now)
{
    if (writer == GetCurrentThreadId() && !rearm) {
        widen(now);
    }
}

int debugTraceIsWriter(void)
{
    return writer == GetCurrentThreadId();
}

UInt32 debugTraceNewLink(void)
{
    UInt32 link = (UInt32)InterlockedIncrement(&linkCount);

    return link != 0 ? link : (UInt32)InterlockedIncrement(&linkCount);
}

UInt16 debugTraceSourceRegister(int classId, const DebugTraceClassDesc* desc, const char* name)
{
    UInt16 source = DEBUG_TRACE_NO_SOURCE;
    int i;

    AcquireSRWLockExclusive(&lock);
    for (i = 1; i < sourceCount; i++) {
        if (sources[i].classId == classId && strncmp(sources[i].name, name, sizeof(sources[i].name) - 1) == 0) {
            break;
        }
    }
    if (i == sourceCount && sourceCount < DEBUG_TRACE_SOURCES) {
        strncpy(sources[i].name, name, sizeof(sources[i].name) - 1);
        sources[i].classId = (UInt16)classId;
        sourceCount++;
    }
    if (i < sourceCount) {
        sources[i].desc = desc;
        sources[i].live = 1;
        source = (UInt16)i;
    }
    ReleaseSRWLockExclusive(&lock);
    return source;
}

void debugTraceSourceUnregister(UInt16 source)
{
    AcquireSRWLockExclusive(&lock);
    if (source != 0 && source < sourceCount) {
        sources[source].live = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

int debugTraceSourceGet(UInt16 source, DebugTraceSource* out)
{
    int found;

    AcquireSRWLockShared(&lock);
    found = source < sourceCount;
    if (found) {
        *out = sources[source];
    }
    if (source == 0) {
        strcpy(out->name, "core");
        out->desc    = &coreClass;
        out->classId = DEBUG_TRACE_CORE;
        out->live    = 1;
    }
    ReleaseSRWLockShared(&lock);
    return found;
}

void debugTraceArm(UInt32 classMask, int on)
{
    UInt32 mask = 0;
    UInt32 was;
    int i;

    AcquireSRWLockExclusive(&lock);
    if (on && ring == NULL) {
        ring = calloc(DEBUG_TRACE_RING, sizeof(Slot));
    }
    for (i = 0; i < DEBUG_TRACE_CLASSES; i++) {
        if (classMask & DEBUG_TRACE_BIT(i)) {
            armCount[i] += on ? 1 : (armCount[i] > 0 ? -1 : 0);
        }
        if (armCount[i] > 0 && ring != NULL) {
            mask |= DEBUG_TRACE_BIT(i);
        }
    }
    if (mask & ~debugTraceArmed) {
        epoch++;
        rearm = 1;
    }
    archMemoryBarrier();
    was = debugTraceArmed;
    debugTraceArmed = mask;
    if (armCb != NULL && mask != was) {
        armCb(mask);
    }
    ReleaseSRWLockExclusive(&lock);
}

void debugTraceSetArmCb(DebugTraceArmCb cb)
{
    AcquireSRWLockExclusive(&lock);
    armCb = cb;
    if (cb != NULL) {
        cb(debugTraceArmed);
    }
    ReleaseSRWLockExclusive(&lock);
}

void debugTracePortsHold(UInt8* held, const UInt8* want)
{
    int port;

    AcquireSRWLockExclusive(&lock);
    for (port = 0; port < DEBUG_TRACE_PORT_BYTES * 8; port++) {
        UInt8 bit = (UInt8)(1 << (port & 7));

        if ((want[port >> 3] & bit) && !(held[port >> 3] & bit)) {
            held[port >> 3] |= bit;
            if (portHolds[port]++ == 0) {
                debugTracePorts[port >> 3] |= bit;
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

void debugTracePortsRelease(UInt8* held)
{
    int port;

    AcquireSRWLockExclusive(&lock);
    for (port = 0; port < DEBUG_TRACE_PORT_BYTES * 8; port++) {
        UInt8 bit = (UInt8)(1 << (port & 7));

        if ((held[port >> 3] & bit) && --portHolds[port] == 0) {
            debugTracePorts[port >> 3] &= (UInt8)~bit;
        }
    }
    memset(held, 0, DEBUG_TRACE_PORT_BYTES);
    ReleaseSRWLockExclusive(&lock);
}

/* These may run on the UI thread while the emulator thread is parked. */
void debugTraceBreak(int reason)
{
    AcquireSRWLockExclusive(&lock);
    epoch++;
    if (debugTraceArmed != 0) {
        put(anchor, 0, DEBUG_TRACE_KIND_DISCONTINUITY, 0, (UInt32)reason, epoch);
    }
    if (reason == DEBUG_TRACE_STOP) {
        writer = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

void debugTraceResume(int reason, UInt32 now, int ownThread)
{
    AcquireSRWLockExclusive(&lock);
    epoch++;
    reanchor(now);
    rearm = 0;
    if (ownThread) {
        writer = GetCurrentThreadId();
    }
    if (debugTraceArmed != 0) {
        put(anchor, 0, DEBUG_TRACE_KIND_DISCONTINUITY, 1, (UInt32)reason, epoch);
    }
    ReleaseSRWLockExclusive(&lock);
}

UInt32 debugTraceSeq(void)
{
    return head;
}

UInt32 debugTraceEpoch(void)
{
    return epoch;
}

/* Copies the record of seq want, 0 when the writer has moved past it or is on it. */
static int fetch(UInt32 want, DebugTraceRecord* out)
{
    const Slot* slot = &ring[want & RING_MASK];

    if (slot->seq != want) {
        return 0;
    }
    _ReadWriteBarrier();
    *out = slot->rec;
    archMemoryBarrier();
    return slot->seq == want;
}

int debugTraceRead(UInt32 afterSeq, DebugTraceRecord* out, int max, UInt32* firstSeq, UInt32* dropped)
{
    UInt32 h = head;
    UInt32 start = afterSeq + 1;
    UInt32 lost = 0;
    int n = 0;
    int tries;

    if (start == 0) {
        start = 1;
    }
    *firstSeq = start;
    *dropped  = 0;
    if (ring == NULL || (Int32)(h - start) < 0) {
        return 0;
    }
    /* Too far behind, or lapped while reading: skip to an eighth of the ring
    ** past the oldest, so the writer does not take it at once. */
    for (tries = 0; tries < 4; tries++) {
        UInt32 oldest = h - (DEBUG_TRACE_RING - DEBUG_TRACE_RING / 8);
        if ((Int32)(oldest - start) > 0) {
            lost += oldest - start;
            start = oldest;
        }
        if (start == 0) {
            start = 1;
        }
        *firstSeq = start;
        n = 0;
        while (n < max && (Int32)(h - start - n) >= 0) {
            UInt32 want = start + n;
            if (want == 0 || !fetch(want, &out[n])) {
                break;
            }
            n++;
            if (want == 0xFFFFFFFF) {
                break;
            }
        }
        if (n > 0 || (Int32)(h - start) < 0) {
            break;
        }
        h = head;
    }
    *dropped = lost;
    return n;
}

/* Appends as _snprintf does, keeping out terminated and *length within size. */
static void append(char* out, int size, int* length, const char* format, ...)
{
    va_list args;
    int     written;

    if (*length >= size - 1) {
        return;
    }
    va_start(args, format);
    written = _vsnprintf(out + *length, (size_t)(size - 1 - *length), format, args);
    va_end(args);
    *length = written < 0 ? size - 1 : *length + written;
    out[*length] = 0;
}

static void formatField(const DebugTraceField* f, const DebugTraceRecord* rec,
                        const DebugTraceRecord* cont, char* out, int size, int* length)
{
    UInt32 word;
    UInt32 v;

    switch (f->word) {
    case DEBUG_TRACE_WORD_A:      word = rec->a;    break;
    case DEBUG_TRACE_WORD_B:      word = rec->b;    break;
    case DEBUG_TRACE_WORD_LINK:   word = rec->link; break;
    case DEBUG_TRACE_WORD_CONT_A: word = cont->a;   break;
    default:                      word = cont->b;   break;
    }
    if (f->shift >= 32 || f->width == 0) {
        append(out, size, length, " %s=?", f->name);
        return;
    }
    v = f->width >= 32 ? word >> f->shift : (word >> f->shift) & ((1u << f->width) - 1);
    if (f->format == DEBUG_TRACE_HEX) {
        append(out, size, length, " %s=0x%X", f->name, v);
    }
    else if (f->format == DEBUG_TRACE_SIGNED) {
        UInt32 sign = 1u << (f->width >= 32 ? 31 : f->width - 1);

        append(out, size, length, " %s=%d", f->name, (Int32)((v ^ sign) - sign));
    }
    else if (f->format == DEBUG_TRACE_ENUM && f->enumNames != NULL && v < (UInt32)f->enumCount) {
        append(out, size, length, " %s=%s", f->name, f->enumNames[v]);
    }
    else {
        append(out, size, length, " %s=%u", f->name, v);
    }
}

static const DebugTraceKindDesc* kindOf(const DebugTraceClassDesc* desc, const DebugTraceRecord* rec)
{
    int i;

    for (i = 0; desc != NULL && i < desc->kindCount; i++) {
        if (desc->kinds[i].kind == (rec->kind & DEBUG_TRACE_KIND_MASK)) {
            return &desc->kinds[i];
        }
    }
    return NULL;
}

static int fieldInPhase(const DebugTraceField* f, const DebugTraceRecord* rec)
{
    return f->phases == 0 || (f->phases & (1 << ((rec->kind & DEBUG_TRACE_PHASE_MASK) >> 14))) != 0;
}

int debugTraceHasCont(const DebugTraceSource* src, const DebugTraceRecord* rec)
{
    const DebugTraceKindDesc* kind = kindOf(src->desc, rec);
    int i;

    for (i = 0; kind != NULL && i < kind->fieldCount; i++) {
        if (kind->fields[i].word >= DEBUG_TRACE_WORD_CONT_A && fieldInPhase(&kind->fields[i], rec)) {
            return 1;
        }
    }
    return 0;
}

int debugTraceFormat(const DebugTraceSource* src, const DebugTraceRecord* rec,
                     const DebugTraceRecord* cont, char* out, int size)
{
    static const char* const phaseNames[] = { "", " begin", " end", " cont" };
    const DebugTraceClassDesc* desc = src->desc;
    const DebugTraceKindDesc*  kind = kindOf(desc, rec);
    UInt16 phase = (UInt16)(rec->kind & DEBUG_TRACE_PHASE_MASK);
    int    length = 0;
    int    i;

    if (size <= 0) {
        return 0;
    }
    out[0] = 0;
    if (kind == NULL) {
        append(out, size, &length, "%s kind%u%s a=0x%X b=0x%X link=%u",
               desc != NULL ? desc->name : "?", (unsigned)(rec->kind & DEBUG_TRACE_KIND_MASK),
               phaseNames[phase >> 14], rec->a, rec->b, rec->link);
        return length;
    }
    append(out, size, &length, "%s %s%s", desc->name, kind->name, phaseNames[phase >> 14]);
    for (i = 0; i < kind->fieldCount; i++) {
        const DebugTraceField* f = &kind->fields[i];

        if (!fieldInPhase(f, rec) || (cont == NULL && f->word >= DEBUG_TRACE_WORD_CONT_A)) {
            continue;
        }
        formatField(f, rec, cont, out, size, &length);
    }
    return length;
}
