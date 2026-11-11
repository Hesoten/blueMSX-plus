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
#ifndef DEBUG_TRACE_H
#define DEBUG_TRACE_H

#include "MsxTypes.h"

/* Records kept, as a power of two. Readers have to drain faster than it fills. */
#ifndef DEBUG_TRACE_RING_LOG2
#define DEBUG_TRACE_RING_LOG2 16
#endif
#define DEBUG_TRACE_RING (1u << DEBUG_TRACE_RING_LOG2)

/* time is the board tick widened to 64 bits; CONT carries more of the record
** before it from the same source. */
typedef struct {
    UInt64 time;
    UInt16 source;
    UInt16 kind;
    UInt32 link;
    UInt32 a;
    UInt32 b;
} DebugTraceRecord;

/* Phase, in the top two bits of kind. */
#define DEBUG_TRACE_INSTANT    0x0000
#define DEBUG_TRACE_BEGIN      0x4000
#define DEBUG_TRACE_END        0x8000
#define DEBUG_TRACE_CONT       0xC000
#define DEBUG_TRACE_PHASE_MASK 0xC000
#define DEBUG_TRACE_KIND_MASK  0x3FFF

/* Classes; each is armed on its own. */
#define DEBUG_TRACE_CORE    0
#define DEBUG_TRACE_GEO3D   1
#define DEBUG_TRACE_VDPCMD  2
#define DEBUG_TRACE_VDPREG  3
#define DEBUG_TRACE_CPU     4
#define DEBUG_TRACE_CLASSES 5
#define DEBUG_TRACE_BIT(cls) (1u << (cls))

/* Class CORE, source 0: a = reason, b = the new epoch, link 1 at a resume, else 0. */
#define DEBUG_TRACE_KIND_DISCONTINUITY 1
#define DEBUG_TRACE_START 0
#define DEBUG_TRACE_LOAD  1
#define DEBUG_TRACE_RESET 2
#define DEBUG_TRACE_STOP  3
#define DEBUG_TRACE_ARM   4
/* A state load that steps reverse play back: the same machine, its clock going back. */
#define DEBUG_TRACE_REWIND 5

#define DEBUG_TRACE_SOURCES   64
#define DEBUG_TRACE_NO_SOURCE 0xFFFF

/* How a tool shows a field of a kind: bits shift..shift+width-1 of one word. */
#define DEBUG_TRACE_WORD_A      0
#define DEBUG_TRACE_WORD_B      1
#define DEBUG_TRACE_WORD_LINK   2
#define DEBUG_TRACE_WORD_CONT_A 3
#define DEBUG_TRACE_WORD_CONT_B 4

#define DEBUG_TRACE_DEC    0
#define DEBUG_TRACE_HEX    1
#define DEBUG_TRACE_SIGNED 2
#define DEBUG_TRACE_ENUM   3

typedef struct {
    const char* name;
    UInt8 word;
    UInt8 shift;
    UInt8 width;
    UInt8 format;
    UInt8 phases;                 /* 1 << (phase >> 14) for each it is in; 0 for all */
    const char* const* enumNames;
    int enumCount;
} DebugTraceField;

typedef struct {
    UInt16 kind;                  /* without the phase */
    const char* name;
    const DebugTraceField* fields;
    int fieldCount;
} DebugTraceKindDesc;

typedef struct DebugTraceClassDesc {
    const char* name;
    const DebugTraceKindDesc* kinds;
    int kindCount;
} DebugTraceClassDesc;

typedef struct {
    char name[32];
    const DebugTraceClassDesc* desc;
    UInt16 classId;
    UInt8 live;
} DebugTraceSource;

extern volatile UInt32 debugTraceArmed;

#define DEBUG_TRACE_ON(mask) ((debugTraceArmed & (mask)) != 0)
#define DEBUG_TRACE(cls, source, kind, time, link, a, b) do {                 \
    if (debugTraceArmed & DEBUG_TRACE_BIT(cls)) {                             \
        debugTraceEmit((source), (kind), (time), (link), (a), (b));           \
    }                                                                          \
} while (0)

/* Producers: time is the 32-bit board tick. Only the emulator thread's are kept. */
void   debugTraceEmit(UInt16 source, UInt16 kind, UInt32 time, UInt32 link, UInt32 a, UInt32 b);
UInt32 debugTraceNewLink(void);
int    debugTraceIsWriter(void);

/* Once a frame while armed, so 32-bit times stay placed after quiet spells. */
void   debugTraceTick(UInt32 now);

/* The same class and name give the same number again; NO_SOURCE when full. */
UInt16 debugTraceSourceRegister(int classId, const DebugTraceClassDesc* desc, const char* name);
void   debugTraceSourceUnregister(UInt16 source);
int    debugTraceSourceGet(UInt16 source, DebugTraceSource* out);

/* Nested per class; the first arm makes the ring, which is never freed. */
void   debugTraceArm(UInt32 classMask, int on);

/* Board: around anything that moves the clock or the state. ownThread makes
** the caller the emulator thread; STOP forgets it. Readers split on the epoch,
** never on time gaps: a step back moves time forward by 2^32. */
void   debugTraceBreak(int reason);
void   debugTraceResume(int reason, UInt32 now, int ownThread);

/* Readers, any thread. Read returns n records of seqs firstSeq..firstSeq+n-1,
** so the next call takes firstSeq + n - 1; dropped counts those lost before. */
UInt32 debugTraceSeq(void);
UInt32 debugTraceEpoch(void);
int    debugTraceRead(UInt32 afterSeq, DebugTraceRecord* out, int max, UInt32* firstSeq, UInt32* dropped);

#endif
