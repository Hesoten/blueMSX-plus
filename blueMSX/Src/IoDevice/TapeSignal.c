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
#include <stdio.h>

/* After stdio.h: pkg_fopen overrides fopen for UTF-8 paths. */
#include "PacketFileSystem.h"

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
static TapeSignalSource   blankSource = TAPE_SIG_WAV;

/* Emulated time tracking */
static UInt64 tapeT;
static UInt64 lastSysTime;
static UInt64 sysFrac;
static int    motorOn;
static int    driving;
static int    refreshArmed;
static UInt64 lastBoostArmT;
static TapeSignalRefreshCb refreshCb = NULL;

/* rec holds what was captured since the motor started, spliced in when it stops */
static int    recordable;
static int    saveMonitor;
static TapeSignalBuilder* rec;
static UInt64 recStartT;
static UInt64 recMotorT;
static UInt64 recEdgeT;
static int    recPinLevel;
static int    recDirty;

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

/* Marks must stay sorted by both fields for the bisection in findByteMark */
static void addMarkAt(TapeSignalBuilder* b, UInt64 timeT, UInt32 byteOffset)
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
    b->byteMark[b->byteMarkCount].timeT      = timeT;
    b->byteMark[b->byteMarkCount].byteOffset = byteOffset;
    b->byteMarkCount++;
}

void tapeSignalBuilderMarkBytePos(TapeSignalBuilder* b, UInt32 byteOffset)
{
    if (b != NULL) {
        addMarkAt(b, b->timeT, byteOffset);
    }
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
static UInt8 advanceCursor(const TapeSignalBuilder* b, UInt64 t, UInt32* index,
                           UInt64* edgeT, UInt8* level)
{
    while (*index < b->pulseCount) {
        UInt32 saved = *index;
        UInt32 width = readPulse(b, index);
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

    advanceCursor(sig, t, index, edgeT, level);
}

/*****************************************************************************
** Time base
******************************************************************************/

static void updateTime(void)
{
    UInt64 now   = boardSystemTime64();
    /* A state load moves the board clock, and while recording there is no end
    ** of tape to clamp against, so an underflow here would have no backstop. */
    UInt64 delta = now > lastSysTime ? now - lastSysTime : 0;

    lastSysTime = now;

    if (motorOn && (sig != NULL || rec != NULL)) {
        delta  += sysFrac;
        tapeT  += delta / TICKS_PER_TSTATE;
        sysFrac = delta % TICKS_PER_TSTATE;
        /* Only playback stops at the end; recording runs past it and grows the tape */
        if (rec == NULL && tapeT > sig->timeT) {
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
    /* A recording anchors on the motor start, but this tape has its own zero */
    recMotorT = 0;
    recEdgeT  = 0;
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

    /* A tape pulled out mid recording takes the unspliced material with it */
    tapeSignalBuilderDestroy(rec);
    rec       = NULL;
    recDirty  = 0;

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

/* Build or rebuild the waveform on the first transport access after the motor
** starts. Machines whose BIOS trap serves the tape never get here, so they
** never pay for it, and one attempt per motor start bounds a failing build. */
static void refreshIfArmed(void)
{
    /* Never mid recording: remounting would discard what was captured */
    if (!refreshArmed || !motorOn || rec != NULL) {
        return;
    }
    if (refreshCb != NULL) {
        refreshCb();
        reanchorTime();
    }
    refreshArmed = 0;   /* after the call: install re-arms on eject */
}

/*****************************************************************************
** Recording
******************************************************************************/

void tapeSignalSetRecordable(int on)
{
    recordable = on ? 1 : 0;
}

void tapeSignalSetBlankSource(TapeSignalSource source)
{
    blankSource = source;
}

void tapeSignalSetSaveMonitor(int on)
{
    saveMonitor = on ? 1 : 0;
}

int tapeSignalRecordDirty(void)
{
    return recDirty;
}

/* Clamp rather than truncate: a folded width would run the tape backwards */
static void addWidePulse(TapeSignalBuilder* out, UInt64 width)
{
    tapeSignalBuilderAddPulse(out, width > 0xfffffffful ? 0xfffffffful : (UInt32)width);
}

/* The splice keeps absolute time, so the marks either side of the recording
** still describe their own pulses; only the overwritten span loses its. The
** content list is a directory, kept whole so untouched files stay listed. */
static void carryIndex(TapeSignalBuilder* out, UInt64 recEndT,
                       TapeSignalSource source)
{
    UInt32 i;

    if (sig == NULL) {
        return;
    }
    /* Only a byte stream needs marks; a tape measured in time converts on its own */
    if (source == TAPE_SIG_CAS) {
        for (i = 0; i < sig->byteMarkCount; i++) {
            UInt64 t = sig->byteMark[i].timeT;
            if (t <= recStartT || t >= recEndT) {
                addMarkAt(out, t, sig->byteMark[i].byteOffset);
            }
        }
    }
    if (sig->contentCount > 0) {
        memcpy(out->content, sig->content, sig->contentCount * sizeof(TapeContent));
        out->contentCount = sig->contentCount;
    }
}

/* Overwrite the way a real deck does: nothing after the recording moves, and
** what it did not reach stays put, headless, instead of being truncated away.
** tailWidth is held out of src so the seam below can absorb it. */
static void spliceRecording(TapeSignalBuilder* src, UInt64 tailWidth)
{
    TapeSignalSource   source = sigSource != TAPE_SIG_NONE ? sigSource : blankSource;
    TapeSignalBuilder* out    = tapeSignalBuilderCreate();
    UInt64 recEndT = recStartT + src->timeT + tailWidth;
    UInt64 edgeT   = 0;
    UInt32 index   = 0;
    UInt32 i       = 0;

    if (out == NULL) {
        return;
    }

    /* Head: the tape ahead of where the deck started writing */
    while (sig != NULL && index < sig->pulseCount) {
        UInt32 saved = index;
        UInt32 width = readPulse(sig, &index);
        if (edgeT + width > recStartT) {
            index = saved;
            break;
        }
        tapeSignalBuilderAddPulse(out, width);
        edgeT += width;
    }
    if (edgeT < recStartT) {
        /* Cut the pulse straddling the start so the head ends exactly there */
        addWidePulse(out, recStartT - edgeT);
    }

    while (i < src->pulseCount) {
        tapeSignalBuilderAddPulse(out, readPulse(src, &i));
    }

    if (sig == NULL || sig->timeT <= recEndT) {
        addWidePulse(out, tailWidth);
    }
    else {
        UInt64 tailEdgeT;
        UInt32 tailIndex;
        UInt8  tailLevel;
        UInt64 rest;

        seekCursor(recEndT, &tailIndex, &tailEdgeT, &tailLevel);
        rest = tailEdgeT + readPulse(sig, &tailIndex) - recEndT;

        /* Grow the last pulse when the old level continues across the seam,
        ** so the splice does not invent an edge that was never on the tape. */
        if (out->level == tailLevel) {
            addWidePulse(out, tailWidth + rest);
        }
        else {
            addWidePulse(out, tailWidth);
            addWidePulse(out, rest);
        }
        while (tailIndex < sig->pulseCount) {
            tapeSignalBuilderAddPulse(out, readPulse(sig, &tailIndex));
        }
    }

    carryIndex(out, recEndT, source);

    if (tapeSignalInstall(out, source)) {
        recDirty = 1;
        tapeSignalSetPosT(recEndT);
        /* The recording is what put the tape here, so the signal cursor is the
        ** authority on the position even though nothing has played yet. */
        driving = 1;
    }
}

static void finishRecording(void)
{
    TapeSignalBuilder* done = rec;
    UInt64 tailWidth;

    if (done == NULL) {
        return;
    }
    tailWidth = tapeT > recEdgeT ? tapeT - recEdgeT : 0;

    rec = NULL;     /* the eject inside the splice frees whatever rec holds */

    if (!done->failed && (done->pulseCount > 0 || tailWidth > 0)) {
        spliceRecording(done, tailWidth);
    }
    tapeSignalBuilderDestroy(done);

    /* The monitor was following rec, which is gone now: put it back on the
    ** tape even when the splice never happened. */
    audioT = tapeT;
    seekCursor(audioT, &audIndex, &audEdgeT, &audLevel);
}

void tapeSignalWriteBit(int level)
{
    int changed;

    level       = level ? 1 : 0;
    changed     = level != recPinLevel;
    recPinLevel = level;

    if (!changed || !recordable || !motorOn) {
        return;
    }

    /* A save never polls the read side, so this is where a tape inserted
    ** under a running motor gets its waveform built to splice into. */
    refreshIfArmed();
    updateTime();

    if (rec == NULL) {
        rec = tapeSignalBuilderCreate();
        if (rec == NULL) {
            return;
        }
        /* A running deck writes from the moment the motor starts, so the quiet
        ** lead-in erases too. Without it the old signal shows through the gap
        ** the BIOS leaves between a file header and its data. */
        recStartT = recMotorT;
        if (tapeT > recStartT) {
            addWidePulse(rec, tapeT - recStartT);
        }
        /* The monitor now follows rec, whose time starts at recStartT */
        audioT   = recStartT;
        audIndex = 0;
        audEdgeT = 0;
        audLevel = 0;
    }
    else {
        /* Two flips inside one T-state still make an edge, so never skip one */
        addWidePulse(rec, tapeT > recEdgeT ? tapeT - recEdgeT : 1);
    }
    recEdgeT = tapeT;

    ledSetCas(1);

    /* Same throttle as the read side, which a save never reaches */
    if (tapeT - lastBoostArmT >= (UInt64)BOOST_ARM_MS * TAPE_TSTATE_FREQ / 1000) {
        lastBoostArmT = tapeT;
        boardSetCasActive();
    }
}

/*****************************************************************************
** WAV writer
******************************************************************************/

/* 8 bit mono, swinging inside the range so a reader's DC follower has room */
#define WAV_RATE   44100
#define WAV_HIGH   0xd0
#define WAV_LOW    0x30

static void putLe(UInt8* p, UInt32 value, int bytes)
{
    while (bytes--) {
        *p++ = (UInt8)value;
        value >>= 8;
    }
}

static int writeRun(FILE* file, UInt8 value, UInt32 count)
{
    UInt8 buf[4096];

    while (count > 0) {
        UInt32 n = count < sizeof(buf) ? count : (UInt32)sizeof(buf);
        memset(buf, value, n);
        if (fwrite(buf, 1, n, file) != n) {
            return 0;
        }
        count -= n;
    }
    return 1;
}

/* Rounding up throughout makes the per pulse counts telescope to the total */
static UInt32 sampleAt(UInt64 t)
{
    return (UInt32)((t * WAV_RATE + TAPE_TSTATE_FREQ - 1) / TAPE_TSTATE_FREQ);
}

void tapeSignalWavHeader(UInt8* hdr, UInt32 sampleCount)
{
    memcpy(hdr,      "RIFF", 4);   putLe(hdr +  4, 36 + sampleCount, 4);
    memcpy(hdr +  8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);   putLe(hdr + 16, 16, 4);
    putLe(hdr + 20, 1, 2);                      /* PCM */
    putLe(hdr + 22, 1, 2);                      /* mono */
    putLe(hdr + 24, WAV_RATE, 4);
    putLe(hdr + 28, WAV_RATE, 4);               /* byte rate, 8 bit mono */
    putLe(hdr + 32, 1, 2);                      /* block align */
    putLe(hdr + 34, 8, 2);                      /* bits per sample */
    memcpy(hdr + 36, "data", 4);   putLe(hdr + 40, sampleCount, 4);
}

int tapeSignalSaveWav(const char* name)
{
    UInt8  hdr[TAPE_WAV_HEADER_SIZE];
    FILE*  file;
    UInt64 edgeT = 0;
    UInt32 index = 0;
    UInt32 done  = 0;
    UInt32 total;
    UInt8  level = 0;

    if (sig == NULL) {
        return 0;
    }
    total = sampleAt(sig->timeT);

    file = fopen(name, "wb");
    if (file == NULL) {
        return 0;
    }
    tapeSignalWavHeader(hdr, total);

    if (fwrite(hdr, 1, sizeof(hdr), file) != sizeof(hdr)) {
        fclose(file);
        return 0;
    }

    while (index < sig->pulseCount) {
        edgeT += readPulse(sig, &index);
        {
            UInt32 upto = sampleAt(edgeT);
            if (!writeRun(file, level ? WAV_HIGH : WAV_LOW, upto - done)) {
                fclose(file);
                return 0;
            }
            done = upto;
        }
        level ^= 1;
    }

    /* The last block only reaches the disk here, so a full volume shows up
    ** as a close failure rather than as a silently short image. */
    return fclose(file) == 0;
}

/*****************************************************************************
** KCS decoder
******************************************************************************/

/* A lead-in shorter than this is noise rather than the start of a block */
#define KCS_MIN_LEADIN  256

typedef struct {
    UInt32 index;
    UInt32 unit;        /* half cycle of the lead-in tone, so any baud decodes */
} KcsReader;

/* One bit: a zero is two half cycles at half the tone rate, a one is four at
** the tone rate. Returns the bit, or -1 once the framing stops making sense. */
static int kcsBit(KcsReader* r)
{
    /* Every half cycle of the cell has to sit in the same band as the first,
    ** or a glitch passes as a short one and a dropout as a long one. */
    UInt32 narrow = r->unit / 2;
    UInt32 wide   = r->unit * 3 / 2;
    UInt32 top    = r->unit * 3;
    UInt32 save   = r->index;
    UInt32 w      = readPulse(sig, &r->index);
    int    n, i;

    if (w == 0 || w < narrow || w > top) {
        r->index = save;
        return -1;
    }
    n = w > wide ? 2 : 4;
    for (i = 1; i < n; i++) {
        UInt32 next = readPulse(sig, &r->index);
        if (next == 0 || next < narrow || next > top || (next > wide) != (w > wide)) {
            r->index = save;
            return -1;
        }
    }
    return n == 2 ? 0 : 1;
}

static int kcsByte(KcsReader* r, UInt8* value)
{
    UInt32 save = r->index;
    UInt8  v    = 0;
    int    i;

    if (kcsBit(r) != 0) {
        r->index = save;
        return 0;
    }
    for (i = 0; i < 8; i++) {
        int b = kcsBit(r);
        if (b < 0) {
            r->index = save;
            return 0;
        }
        v |= (UInt8)(b << i);
    }
    for (i = 0; i < 2; i++) {
        if (kcsBit(r) != 1) {
            r->index = save;
            return 0;
        }
    }
    *value = v;
    return 1;
}

/* Runs of equal width pulses are the lead-in; its width sets the bit scale */
static int kcsFindBlock(KcsReader* r)
{
    while (r->index < sig->pulseCount) {
        UInt32 start = r->index;
        UInt32 first = readPulse(sig, &r->index);
        UInt32 run   = 1;

        while (r->index < sig->pulseCount) {
            UInt32 save = r->index;
            UInt32 w    = readPulse(sig, &r->index);
            if (w < first - first / 4 || w > first + first / 4) {
                r->index = save;
                break;
            }
            run++;
        }
        if (run >= KCS_MIN_LEADIN) {
            r->unit = first;
            return 1;
        }
        if (r->index == start) {
            break;
        }
    }
    return 0;
}

static int casAppend(UInt8** buf, UInt32* size, UInt32* alloc,
                     const UInt8* data, UInt32 count)
{
    if (*size + count > *alloc) {
        UInt32 want = *alloc ? *alloc : 65536;
        UInt8* p;

        while (want < *size + count) {
            want *= 2;
        }
        p = realloc(*buf, want);
        if (p == NULL) {
            return 0;
        }
        *buf   = p;
        *alloc = want;
    }
    memcpy(*buf + *size, data, count);
    *size += count;
    return 1;
}

/* Decoded into memory first: opening the file would truncate it, and a
** waveform the decoder cannot read must not cost the caller its image. */
int tapeSignalSaveCas(const char* name, const UInt8* marker, int markerSize,
                      int align8)
{
    static const UInt8 pad[8] = { 0 };
    KcsReader r;
    FILE*  file;
    UInt8* buf    = NULL;
    UInt32 size   = 0;
    UInt32 alloc  = 0;
    int    blocks = 0;
    int    ok     = 1;

    if (sig == NULL || marker == NULL || markerSize <= 0) {
        return 0;
    }

    r.index = 0;
    r.unit  = 0;
    while (ok && kcsFindBlock(&r)) {
        UInt8 value;
        int   wrote = 0;

        while (ok && kcsByte(&r, &value)) {
            if (wrote == 0) {
                if (align8 && (size & 7) != 0) {
                    ok = casAppend(&buf, &size, &alloc, pad, 8 - (size & 7));
                }
                ok = ok && casAppend(&buf, &size, &alloc, marker, (UInt32)markerSize);
            }
            ok = ok && casAppend(&buf, &size, &alloc, &value, 1);
            wrote++;
        }
        if (wrote > 0) {
            blocks++;
        }
    }

    if (!ok || blocks == 0) {
        free(buf);
        return 0;
    }

    file = fopen(name, "wb");
    if (file == NULL) {
        free(buf);
        return 0;
    }
    ok = fwrite(buf, 1, size, file) == size;
    if (fclose(file) != 0) {
        ok = 0;
    }
    free(buf);
    return ok;
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
    recMotorT    = tapeT;

    /* The BIOS stops the motor when a save ends: the point to commit */
    if (!on) {
        finishRecording();
    }
}

UInt8 tapeSignalReadBit(void)
{
    UInt8 level;

    refreshIfArmed();

    if (sig == NULL) {
        return 0;
    }

    updateTime();

    if (!motorOn) {
        return curLevel;
    }

    level   = advanceCursor(sig, tapeT, &curIndex, &curEdgeT, &curLevel);
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
    /* Moving the tape under a recording would leave it anchored to a timeline
    ** that no longer exists, so commit what was captured before the jump. */
    finishRecording();

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
    /* Where a recording started under the motor is now here, not back where
    ** the deck was before the seek */
    recMotorT     = tapeT;
    recEdgeT      = tapeT;
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
static Int32 averageLevel(const TapeSignalBuilder* b, UInt64 from, UInt64 to)
{
    UInt64 high = 0;
    UInt64 t    = from;

    while (t < to) {
        UInt32 saved, width;
        UInt64 edgeEnd;

        advanceCursor(b, t, &audIndex, &audEdgeT, &audLevel);
        saved = audIndex;
        width = readPulse(b, &audIndex);
        audIndex = saved;
        if (width == 0) {
            /* No edge yet beyond here: a recording in progress ends on the
            ** level the pin is holding, and so does the end of a tape. */
            if (audLevel) {
                high += to - t;
            }
            break;
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
    /* A deck monitors what its head is doing, so a save is heard from the
    ** material being written rather than from the tape underneath it. */
    const TapeSignalBuilder* src  = rec != NULL ? rec : sig;
    UInt64                   base = rec != NULL ? recStartT : 0;
    UInt64 step;
    UInt64 target;
    UInt32 i;

    if (src == NULL || count == 0) {
        return NULL;
    }

    updateTime();

    /* Few decks could monitor what they were writing, so a save stays silent
    ** unless the user asks to hear it. */
    if (rec != NULL && !saveMonitor) {
        audioT = tapeT;
        return NULL;
    }
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
        v = averageLevel(src, audioT - base, to - base);
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
    /* Whatever was being captured belongs to the timeline that just ended */
    tapeSignalBuilderDestroy(rec);
    rec = NULL;
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
