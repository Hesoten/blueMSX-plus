/*****************************************************************************
**
** Signal level cassette tape playback.
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
#include "TapeSignal.h"
#include "Board.h"
#include "Led.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>

/* boardSystemTime64() counts at 6x the nominal Z80 clock scaled by
** HIRES_CYCLES_PER_LORES_CYCLE, so this divisor is exact. The 32 bit
** boardSystemTime() would wrap every 200 s with the motor left running. */
#define HIRES_PER_LORES   100000
#define TICKS_PER_TSTATE  ((UInt64)HIRES_PER_LORES * boardFrequency() / TAPE_TSTATE_FREQ)

/* One seek entry per this many UInt16 slots of the pulse array */
#define SEEK_STRIDE       8192

/* Re-arm the load boost at most this often, in emulated milliseconds. Well
** under the board's release tail, so a load in progress never lets it lapse. */
#define BOOST_ARM_MS      20

#define MAX_CONTENT       1024

/* Loading noise is background, not a voice, so it sits below the chip channels.
** The roll off in the render loop is what makes a raw square wave bearable at
** this level; the mixer meter reads amplitude and cannot see that. */
#define AUDIO_PEAK        40000

typedef struct {
    UInt64 timeT;
    UInt32 index;
    UInt8  level;
} SeekEntry;

typedef struct {
    UInt64 timeT;
    UInt32 byteOffset;
} ByteMark;

struct TapeSignalBuilder {
    UInt16*     pulse;
    UInt32      pulseCount;
    UInt32      pulseAlloc;

    SeekEntry*  seek;
    UInt32      seekCount;
    UInt32      seekAlloc;

    ByteMark*   byteMark;
    UInt32      byteMarkCount;
    UInt32      byteMarkAlloc;

    TapeContent content[MAX_CONTENT];
    int         contentCount;

    UInt64      timeT;      /* accumulated length */
    UInt8       level;      /* polarity after the last emitted pulse */
    UInt32      lastSeekAt; /* pulseCount when the last seek entry was added */
    int         failed;     /* set on allocation failure */
};

/* Mounted tape */
static TapeSignalBuilder* sig = NULL;
static TapeSignalSource   sigSource = TAPE_SIG_NONE;

/* Emulated time tracking */
static UInt64 tapeT;
static UInt64 lastSysTime;
static UInt64 sysFrac;
static int    motorOn;
static int    driving;
static int    refreshArmed;
static UInt64 lastBoostArmT;
static TapeSignalRefreshCb refreshCb = NULL;

/* Position read from a save state, applied once a waveform is mounted */
static int    stPending;
static UInt64 stPos;
static UInt64 stLen;

/* Playback cursor, driven by tapeSignalReadBit() */
static UInt32 curIndex;
static UInt64 curEdgeT;     /* time at which the pulse at curIndex ends */
static UInt8  curLevel;

/* Audio cursor, driven by the mixer. Kept separate so the two do not
** consume each other's position. */
static UInt64 audioT;
static UInt32 audIndex;
static UInt64 audEdgeT;
static UInt8  audLevel;
static Int32  audioDc;
static Int32  audioLp1;
static Int32  audioLp2;

/*****************************************************************************
** Builder
******************************************************************************/

static int growPulse(TapeSignalBuilder* b, UInt32 need)
{
    UInt16* p;
    UInt32  alloc;

    /* Checked before the sum below is formed, so the count cannot wrap and the
    ** doubling cannot reach the value where it would wrap to zero. */
    if (need > TAPE_MAX_PULSES - b->pulseCount) {
        b->failed = 1;
        return 0;
    }
    if (b->pulseCount + need <= b->pulseAlloc) {
        return 1;
    }
    alloc = b->pulseAlloc ? b->pulseAlloc * 2 : 4096;
    while (alloc < b->pulseCount + need) {
        alloc *= 2;
    }
    p = realloc(b->pulse, alloc * sizeof(UInt16));
    if (p == NULL) {
        b->failed = 1;
        return 0;
    }
    b->pulse      = p;
    b->pulseAlloc = alloc;
    return 1;
}

static void addSeekEntry(TapeSignalBuilder* b)
{
    SeekEntry* s;

    if (b->seekCount == b->seekAlloc) {
        UInt32 alloc = b->seekAlloc ? b->seekAlloc * 2 : 256;
        s = realloc(b->seek, alloc * sizeof(SeekEntry));
        if (s == NULL) {
            b->failed = 1;
            return;
        }
        b->seek      = s;
        b->seekAlloc = alloc;
    }
    b->seek[b->seekCount].timeT = b->timeT;
    b->seek[b->seekCount].index = b->pulseCount;
    b->seek[b->seekCount].level = b->level;
    b->seekCount++;
    b->lastSeekAt = b->pulseCount;
}

TapeSignalBuilder* tapeSignalBuilderCreate(void)
{
    TapeSignalBuilder* b = calloc(1, sizeof(TapeSignalBuilder));
    if (b != NULL) {
        addSeekEntry(b);
    }
    return b;
}

void tapeSignalBuilderDestroy(TapeSignalBuilder* b)
{
    if (b == NULL) {
        return;
    }
    free(b->pulse);
    free(b->seek);
    free(b->byteMark);
    free(b);
}

int tapeSignalBuilderFailed(const TapeSignalBuilder* b)
{
    return b == NULL || b->failed;
}

/* Size the pulse array up front. Without it a large image doubles its way up
** and briefly holds three times the memory it needs. */
void tapeSignalBuilderReserve(TapeSignalBuilder* b, UInt32 slots)
{
    UInt16* p;

    if (b == NULL || b->failed || slots <= b->pulseAlloc) {
        return;
    }
    p = realloc(b->pulse, slots * sizeof(UInt16));
    if (p == NULL) {
        b->failed = 1;
        return;
    }
    b->pulse      = p;
    b->pulseAlloc = slots;
}

void tapeSignalBuilderAddPulse(TapeSignalBuilder* b, UInt32 tstates)
{
    if (b == NULL || b->failed) {
        return;
    }
    if (tstates == 0) {
        tstates = 1;
    }

    if (tstates < 0x10000) {
        if (!growPulse(b, 1)) {
            return;
        }
        b->pulse[b->pulseCount++] = (UInt16)tstates;
    }
    else {
        /* Escape: a zero slot means the next two slots are a UInt32 */
        if (!growPulse(b, 3)) {
            return;
        }
        b->pulse[b->pulseCount++] = 0;
        b->pulse[b->pulseCount++] = (UInt16)(tstates & 0xffff);
        b->pulse[b->pulseCount++] = (UInt16)(tstates >> 16);
    }

    b->timeT += tstates;
    b->level ^= 1;

    if (b->pulseCount - b->lastSeekAt >= SEEK_STRIDE) {
        addSeekEntry(b);
    }
}

void tapeSignalBuilderAddPulses(TapeSignalBuilder* b, UInt32 tstates, UInt32 count)
{
    while (count-- > 0) {
        tapeSignalBuilderAddPulse(b, tstates);
    }
}

void tapeSignalBuilderAddSilenceMs(TapeSignalBuilder* b, UInt32 ms)
{
    if (b == NULL || b->failed || ms == 0) {
        return;
    }
    /* Two half pulses so the polarity is unchanged across the gap */
    tapeSignalBuilderAddPulse(b, (UInt32)((UInt64)ms * TAPE_TSTATE_FREQ / 2000));
    tapeSignalBuilderAddPulse(b, (UInt32)((UInt64)ms * TAPE_TSTATE_FREQ / 2000));
}

/* Formats with no byte stream count in 1/128 s units, which is what the tape
** position dialog already displays as a time */
static UInt32 timeAsByte(UInt64 t)
{
    return (UInt32)(t * 128 / TAPE_TSTATE_FREQ);
}

UInt32 tapeSignalBuilderTimeAsByte(const TapeSignalBuilder* b)
{
    return b != NULL ? timeAsByte(b->timeT) : 0;
}

void tapeSignalBuilderMarkBytePos(TapeSignalBuilder* b, UInt32 byteOffset)
{
    ByteMark* m;

    if (b == NULL || b->failed) {
        return;
    }
    if (b->byteMarkCount == b->byteMarkAlloc) {
        UInt32 alloc = b->byteMarkAlloc ? b->byteMarkAlloc * 2 : 256;
        m = realloc(b->byteMark, alloc * sizeof(ByteMark));
        if (m == NULL) {
            b->failed = 1;
            return;
        }
        b->byteMark      = m;
        b->byteMarkAlloc = alloc;
    }
    b->byteMark[b->byteMarkCount].timeT      = b->timeT;
    b->byteMark[b->byteMarkCount].byteOffset = byteOffset;
    b->byteMarkCount++;
}

void tapeSignalBuilderAddIndex(TapeSignalBuilder* b, TapeContentType type,
                               const char* name, UInt32 byteOffset)
{
    TapeContent* c;

    if (b == NULL || b->failed || b->contentCount >= MAX_CONTENT) {
        return;
    }
    c = b->content + b->contentCount++;
    c->type = type;
    c->pos  = (int)byteOffset;
    memset(c->fileName, 0, sizeof(c->fileName));
    if (name != NULL) {
        strncpy(c->fileName, name, sizeof(c->fileName) - 1);
    }
}

/*****************************************************************************
** Cursor helpers
******************************************************************************/

static UInt32 readPulse(const TapeSignalBuilder* b, UInt32* index)
{
    UInt32 v;

    if (*index >= b->pulseCount) {
        return 0;
    }
    v = b->pulse[(*index)++];
    if (v == 0) {
        UInt32 lo, hi;
        if (*index + 1 >= b->pulseCount) {
            *index = b->pulseCount;
            return 0;
        }
        lo = b->pulse[(*index)++];
        hi = b->pulse[(*index)++];
        v  = lo | (hi << 16);
    }
    return v;
}

/* Advance a cursor so that it covers time t, returning the level there */
static UInt8 advanceCursor(UInt64 t, UInt32* index, UInt64* edgeT, UInt8* level)
{
    while (*index < sig->pulseCount) {
        UInt32 saved = *index;
        UInt32 width = readPulse(sig, index);
        if (*edgeT + width > t) {
            *index = saved;
            break;
        }
        *edgeT += width;
        *level ^= 1;
    }
    return *level;
}

/* Position a cursor at time t, using the sparse index to skip ahead */
static void seekCursor(UInt64 t, UInt32* index, UInt64* edgeT, UInt8* level)
{
    UInt32 lo = 0;
    UInt32 hi;

    if (sig == NULL) {
        *index = 0;
        *edgeT = 0;
        *level = 0;
        return;
    }

    /* Lower bound over the seek entries; there is always at least one */
    hi = sig->seekCount - 1;
    while (lo < hi) {
        UInt32 mid = lo + (hi - lo + 1) / 2;
        if (sig->seek[mid].timeT <= t) {
            lo = mid;
        }
        else {
            hi = mid - 1;
        }
    }

    *index = sig->seek[lo].index;
    *edgeT = sig->seek[lo].timeT;
    *level = sig->seek[lo].level;

    advanceCursor(t, index, edgeT, level);
}

/*****************************************************************************
** Time base
******************************************************************************/

static void updateTime(void)
{
    UInt64 now   = boardSystemTime64();
    UInt64 delta = now - lastSysTime;

    lastSysTime = now;

    if (motorOn && sig != NULL) {
        delta  += sysFrac;
        tapeT  += delta / TICKS_PER_TSTATE;
        sysFrac = delta % TICKS_PER_TSTATE;
        if (tapeT > sig->timeT) {
            tapeT = sig->timeT;
        }
    }
}

/* Re-anchor without advancing, used after a seek or a state load */
static void reanchorTime(void)
{
    lastSysTime = boardSystemTime64();
    sysFrac     = 0;
}

/*****************************************************************************
** Mount and eject
******************************************************************************/

int tapeSignalInstall(TapeSignalBuilder* b, TapeSignalSource source)
{
    UInt16* p;

    /* Validate before ejecting, so a rejected build leaves the mounted tape
    ** alone instead of silently unloading it. */
    if (b == NULL || b->failed || b->pulseCount == 0) {
        tapeSignalBuilderDestroy(b);
        return 0;
    }

    /* Nothing appends after this point, so give back the growth slack */
    p = realloc(b->pulse, b->pulseCount * sizeof(UInt16));
    if (p != NULL) {
        b->pulse      = p;
        b->pulseAlloc = b->pulseCount;
    }

    tapeSignalEject();

    sig       = b;
    sigSource = source;

    tapeT   = 0;
    audioT  = 0;
    driving = 0;
    lastBoostArmT = 0;
    reanchorTime();

    curIndex = 0;  curEdgeT = 0;  curLevel = 0;
    audIndex = 0;  audEdgeT = 0;  audLevel = 0;
    audioDc  = 0;
    audioLp1 = 0;
    audioLp2 = 0;

    return 1;
}

/* motorOn is not touched here: it mirrors the PPI CASON pin, which is owned
** by the machine and outlives any tape change. */
void tapeSignalEject(void)
{
    tapeSignalBuilderDestroy(sig);
    sig       = NULL;
    sigSource = TAPE_SIG_NONE;
    driving   = 0;
    tapeT     = 0;
    audioT    = 0;

    /* A tape swapped in while the motor runs still gets built on the next read */
    refreshArmed = 1;
}

int tapeSignalIsActive(void)
{
    return sig != NULL;
}

int tapeSignalIsDriving(void)
{
    return sig != NULL && driving;
}

/*****************************************************************************
** Motor and signal read
******************************************************************************/

void tapeSignalSetRefreshCallback(TapeSignalRefreshCb cb)
{
    refreshCb = cb;
}

void tapeSignalSetMotor(int on)
{
    on = on ? 1 : 0;

    if (on == motorOn) {
        return;
    }

    /* Settle the elapsed time under the old motor state first */
    updateTime();
    motorOn      = on;
    sysFrac      = 0;
    refreshArmed = on;
}

UInt8 tapeSignalReadBit(void)
{
    UInt8 level;

    /* Build or rebuild the waveform on the first read after the motor starts.
    ** Machines whose BIOS trap serves the load never poll here, so they never
    ** pay for it, and one attempt per motor start bounds a failing build. */
    if (refreshArmed && motorOn) {
        if (refreshCb != NULL) {
            refreshCb();
            reanchorTime();
        }
        refreshArmed = 0;   /* after the call: install re-arms on eject */
    }

    if (sig == NULL) {
        return 0;
    }

    updateTime();

    if (!motorOn) {
        return curLevel;
    }

    level   = advanceCursor(tapeT, &curIndex, &curEdgeT, &curLevel);
    driving = 1;

    ledSetCas(1);

    /* Arm the load boost only while the BIOS is actually polling, so a tape
    ** left running at a BASIC prompt does not hold the machine at full speed. */
    if (tapeT - lastBoostArmT >= (UInt64)BOOST_ARM_MS * TAPE_TSTATE_FREQ / 1000) {
        lastBoostArmT = tapeT;
        boardSetCasActive();
    }

    return level;
}

/*****************************************************************************
** Position
******************************************************************************/

static UInt64 lengthT(void)
{
    return sig != NULL ? sig->timeT : 0;
}

UInt64 tapeSignalGetPosT(void)
{
    updateTime();
    return tapeT;
}

void tapeSignalSetPosT(UInt64 t)
{
    if (sig == NULL) {
        return;
    }
    if (t > sig->timeT) {
        t = sig->timeT;
    }

    tapeT   = t;
    audioT  = t;
    driving = 0;
    reanchorTime();

    seekCursor(tapeT, &curIndex, &curEdgeT, &curLevel);
    seekCursor(audioT, &audIndex, &audEdgeT, &audLevel);
    /* The level either side of a seek is unrelated, so a carried over offset
    ** would just come out as a click */
    audioDc       = 0;
    audioLp1      = 0;
    audioLp2      = 0;
    lastBoostArmT = tapeT;
}

/* Index of the last byte mark at or before the given key. Both fields grow
** monotonically, so the same bisection serves either direction. */
static UInt32 findByteMark(UInt64 key, int byTime)
{
    UInt32 lo = 0;
    UInt32 hi = sig->byteMarkCount - 1;

    while (lo < hi) {
        UInt32 mid = lo + (hi - lo + 1) / 2;
        UInt64 val = byTime ? sig->byteMark[mid].timeT
                            : (UInt64)sig->byteMark[mid].byteOffset;
        if (val <= key) {
            lo = mid;
        }
        else {
            hi = mid - 1;
        }
    }
    return lo;
}

/* An image with no byte stream is measured in time, so the conversion is exact
** and needs no mark to land on. Snapping would drag a resumed position back to
** the start of whatever block it sits in, and a save from there erases it. */
static int byteAxisIsTime(void)
{
    return sigSource == TAPE_SIG_TSX || sigSource == TAPE_SIG_WAV;
}

void tapeSignalSetPosByByte(UInt32 byteOffset)
{
    if (sig == NULL) {
        return;
    }
    if (byteAxisIsTime()) {
        tapeSignalSetPosT((UInt64)byteOffset * TAPE_TSTATE_FREQ / 128);
    }
    else if (sig->byteMarkCount > 0) {
        tapeSignalSetPosT(sig->byteMark[findByteMark(byteOffset, 0)].timeT);
    }
}

UInt32 tapeSignalGetPosAsByte(void)
{
    if (sig == NULL) {
        return 0;
    }
    if (byteAxisIsTime()) {
        return timeAsByte(tapeSignalGetPosT());
    }
    if (sig->byteMarkCount == 0) {
        return 0;
    }
    return sig->byteMark[findByteMark(tapeSignalGetPosT(), 1)].byteOffset;
}

/* The last mark is the end of the image, so it doubles as the length */
UInt32 tapeSignalGetLengthAsByte(void)
{
    if (sig == NULL) {
        return 0;
    }
    if (byteAxisIsTime()) {
        return timeAsByte(sig->timeT);
    }
    if (sig->byteMarkCount == 0) {
        return 0;
    }
    return sig->byteMark[sig->byteMarkCount - 1].byteOffset;
}

TapeContent* tapeSignalGetContent(int* count)
{
    static TapeContent empty[1];

    if (sig == NULL) {
        *count = 0;
        return empty;
    }
    *count = sig->contentCount;
    return sig->content;
}

/*****************************************************************************
** Audio
******************************************************************************/

/* Mean level over [from, to), scaled to +-AUDIO_PEAK */
static Int32 averageLevel(UInt64 from, UInt64 to)
{
    UInt64 high = 0;
    UInt64 t    = from;

    while (t < to) {
        UInt32 saved, width;
        UInt64 edgeEnd;

        advanceCursor(t, &audIndex, &audEdgeT, &audLevel);
        saved = audIndex;
        width = readPulse(sig, &audIndex);
        audIndex = saved;
        if (width == 0) {
            break;                      /* past the end of the tape */
        }
        edgeEnd = audEdgeT + width;
        if (edgeEnd > to) {
            edgeEnd = to;
        }
        if (audLevel) {
            high += edgeEnd - t;
        }
        t = edgeEnd;
    }

    if (to <= from) {
        return 0;
    }
    return (Int32)((Int64)(2 * high - (to - from)) * AUDIO_PEAK / (Int64)(to - from));
}

Int32* tapeSignalRenderAudio(Int32* buffer, UInt32 count)
{
    UInt64 step;
    UInt64 target;
    UInt32 i;

    if (sig == NULL || count == 0) {
        return NULL;
    }

    updateTime();
    target = tapeT;

    if (target <= audioT) {
        /* Motor stopped or nothing new to play */
        return NULL;
    }

    /* Recomputing the step every block keeps the audio locked to emulated
    ** time without knowing the sample rate, and cannot drift. */
    step = (target - audioT) / count;
    if (step == 0) {
        step = 1;
    }

    for (i = 0; i < count; i++) {
        UInt64 to = audioT + step;
        Int32  v;

        if (to > target) {
            to = target;
        }
        /* Averaging over the sample interval band limits the square wave, and
        ** at fast forward speeds it averages whole cycles away to silence. */
        v = averageLevel(audioT, to);
        audioT = to;

        /* A deck monitors the tape through a small speaker, so the upper
        ** harmonics that make a raw square wave shrill never reach the ear.
        ** Rolling them off is what buys the headroom for the level above. */
        audioLp1 += (v - audioLp1) >> 1;
        audioLp2 += (audioLp1 - audioLp2) >> 1;
        v = audioLp2;

        /* A silent stretch holds one level, so drain the offset it leaves */
        audioDc += (v - audioDc) >> 8;
        buffer[i] = v - audioDc;
    }

    audioT = target;

    return buffer;
}

/*****************************************************************************
** Reset and state
******************************************************************************/

/* The tape position is not rewound: a reset does not move a real cassette */
void tapeSignalReset(void)
{
    reanchorTime();
}

void tapeSignalSaveState(void)
{
    SaveState* state = saveStateOpenForWrite("tapeSignal");
    UInt64 len = lengthT();

    saveStateSet(state, "active",  sig != NULL);
    saveStateSet(state, "source",  sigSource);
    saveStateSet(state, "posLo",   (UInt32)(tapeT & 0xffffffff));
    saveStateSet(state, "posHi",   (UInt32)(tapeT >> 32));
    saveStateSet(state, "lenLo",   (UInt32)(len & 0xffffffff));
    saveStateSet(state, "lenHi",   (UInt32)(len >> 32));

    saveStateClose(state);
}

/* Runs from boardLoadState, before the machine exists and before the tape is
** remounted, so only stash the position here. */
void tapeSignalLoadState(void)
{
    SaveState* state = saveStateOpenForRead("tapeSignal");

    stPending = saveStateGet(state, "active", 0);
    stPos     = (UInt64)saveStateGet(state, "posLo", 0);
    stPos    |= (UInt64)saveStateGet(state, "posHi", 0) << 32;
    stLen     = (UInt64)saveStateGet(state, "lenLo", 0);
    stLen    |= (UInt64)saveStateGet(state, "lenHi", 0) << 32;

    saveStateClose(state);
}

/* No tape could be built, so the stash can never be matched. Forget it rather
** than let it seek a later, unrelated image that happens to be as long. */
void tapeSignalDropLoadedState(void)
{
    stPending = 0;
}

void tapeSignalApplyLoadedState(void)
{
    if (!stPending) {
        return;
    }
    stPending = 0;

    /* A different tape is mounted now; leave it where the caller put it */
    if (sig != NULL && stLen == sig->timeT) {
        tapeSignalSetPosT(stPos);
    }
}
