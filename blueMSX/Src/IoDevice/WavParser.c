/*****************************************************************************
**
** WAV tape recording parsing.
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
/* A recording is a level over time, so it is turned back into polarity edges
** with a drifting DC reference and a Schmitt trigger. */

#include "WavParser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAVE_FORMAT_PCM   1
/* Wraps plain PCM and keeps the same fmt layout for the fields read below, so
** it is accepted rather than rejected as a compressed format. */
#define WAVE_FORMAT_EXT   0xfffe

/* The DC reference tracks slow drift only, about 20 Hz */
#define DC_CORNER_HZ     20

/* Hysteresis, as a fraction of the estimated signal peak */
#define TRIGGER_DIV       8

/* A tape needs at least this much signal to be worth decoding */
#define MIN_PEAK        256

/* Fastest legitimate half cycle is 2400 baud, so anything far below that is
** noise rather than data. */
#define MIN_PULSE_US     20

/* An edge gap longer than this is a block boundary, not signal. The longest
** legitimate gap is one 1200 Hz half cycle, well under a millisecond. */
#define GAP_MS          300

typedef struct {
    const UInt8* data;
    UInt32       count;      /* frames */
    UInt32       rate;
    int          bits;
    int          channels;
} Wav;

/*****************************************************************************
** RIFF
******************************************************************************/

static void fail(char* err, int errSize, const char* msg)
{
    if (err != NULL && errSize > 0) {
        strncpy(err, msg, errSize - 1);
        err[errSize - 1] = 0;
    }
}

static UInt32 rd16(const UInt8* p) { return (UInt32)p[0] | ((UInt32)p[1] << 8); }
static UInt32 rd32(const UInt8* p)
{
    return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}

int wavIsWavImage(const UInt8* data, int size)
{
    return data != NULL && size >= 12 &&
           0 == memcmp(data, "RIFF", 4) && 0 == memcmp(data + 8, "WAVE", 4);
}

static int wavOpen(Wav* w, const UInt8* data, int size, char* err, int errSize)
{
    int pos = 12;
    int haveFmt = 0;
    UInt32 blockAlign;

    memset(w, 0, sizeof(*w));

    while (pos + 8 <= size) {
        const UInt8* id  = data + pos;
        UInt32       len = rd32(data + pos + 4);
        int          body = pos + 8;

        if (len > (UInt32)(size - body)) {
            len = (UInt32)(size - body);        /* tolerate a truncated tail */
        }

        if (0 == memcmp(id, "fmt ", 4) && len >= 16) {
            UInt32 tag = rd16(data + body);
            if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_EXT) {
                fail(err, errSize, "WAV is not uncompressed PCM");
                return 0;
            }
            w->channels = (int)rd16(data + body + 2);
            w->rate     = rd32(data + body + 4);
            w->bits     = (int)rd16(data + body + 14);
            haveFmt     = 1;
        }
        else if (0 == memcmp(id, "data", 4)) {
            if (!haveFmt) {
                fail(err, errSize, "WAV data chunk before fmt chunk");
                return 0;
            }
            w->data = data + body;
            blockAlign = (UInt32)(w->bits / 8) * (UInt32)w->channels;
            if (blockAlign == 0) {
                fail(err, errSize, "WAV has an unsupported sample format");
                return 0;
            }
            w->count = len / blockAlign;
            break;
        }

        /* Chunks are padded to an even length */
        pos = body + (int)len + ((len & 1) ? 1 : 0);
    }

    if (w->data == NULL || w->count == 0) {
        fail(err, errSize, "WAV has no sample data");
        return 0;
    }
    if ((w->bits != 8 && w->bits != 16) || w->channels < 1 || w->channels > 2) {
        fail(err, errSize, "WAV must be 8 or 16 bit, mono or stereo");
        return 0;
    }
    if (w->rate < 4000) {
        fail(err, errSize, "WAV sample rate is too low for a tape");
        return 0;
    }
    return 1;
}

/* One frame, centred on zero and averaged over the channels */
static Int32 frameAt(const Wav* w, UInt32 i)
{
    const UInt8* p;
    Int32 a, b;

    if (w->bits == 8) {
        p = w->data + (size_t)i * w->channels;
        a = ((Int32)p[0] - 128) << 8;
        if (w->channels == 1) {
            return a;
        }
        b = ((Int32)p[1] - 128) << 8;
        return (a + b) / 2;
    }

    p = w->data + (size_t)i * 2 * w->channels;
    a = (Int32)(Int16)(UInt16)rd16(p);
    if (w->channels == 1) {
        return a;
    }
    b = (Int32)(Int16)(UInt16)rd16(p + 2);
    return (a + b) / 2;
}

/*****************************************************************************
** Decoding
******************************************************************************/

/* Emits the span since the last edge as one pulse, carrying the division
** remainder so the rate conversion does not drift. A pulse is a UInt32 count of
** T-states, so a silent stretch beyond about 1200 s is capped, not wrapped. */
static void addSpan(TapeSignalBuilder* b, UInt64* acc, UInt32 samples, UInt32 rate)
{
    UInt64 t;

    *acc += (UInt64)samples * TAPE_TSTATE_FREQ;
    t     = *acc / rate;
    *acc %= rate;
    tapeSignalBuilderAddPulse(b, t > 0xfffffffful ? 0xfffffffful : (UInt32)t);
}

/* Shift for a one pole low pass at roughly DC_CORNER_HZ */
static int dcShiftFor(UInt32 rate)
{
    UInt32 target = rate / (2 * 3 * DC_CORNER_HZ);
    int    shift  = 1;

    while ((1u << (shift + 1)) <= target && shift < 16) {
        shift++;
    }
    return shift;
}

static Int32 estimatePeak(const Wav* w, int dcShift)
{
    Int32  dc   = frameAt(w, 0);
    Int32  peak = 0;
    UInt32 i;

    for (i = 0; i < w->count; i++) {
        Int32 v = frameAt(w, i);
        Int32 y;
        dc += (v - dc) >> dcShift;
        y = v - dc;
        if (y < 0) {
            y = -y;
        }
        if (y > peak) {
            peak = y;
        }
    }
    return peak;
}

TapeSignalBuilder* wavToWave(const UInt8* data, int size, char* err, int errSize)
{
    Wav    w;
    TapeSignalBuilder* b;
    Int32  dc, peak, thr;
    int    dcShift;
    int    level = 0, block = 1;
    UInt32 i, lastEdge = 0, minGap, gapT;
    UInt64 acc = 0, slots;

    if (err != NULL && errSize > 0) {
        *err = 0;
    }
    if (!wavIsWavImage(data, size) || !wavOpen(&w, data, size, err, errSize)) {
        return NULL;
    }

    dcShift = dcShiftFor(w.rate);
    peak    = estimatePeak(&w, dcShift);
    if (peak < MIN_PEAK) {
        fail(err, errSize, "WAV carries no usable tape signal");
        return NULL;
    }
    thr = peak / TRIGGER_DIV;

    minGap = w.rate / (1000000 / MIN_PULSE_US);
    if (minGap < 1) {
        minGap = 1;
    }

    b = tapeSignalBuilderCreate();
    if (b == NULL) {
        fail(err, errSize, "Out of memory");
        return NULL;
    }
    /* Roughly one edge per 2400 Hz half cycle, with room to spare */
    slots = (UInt64)(w.count / w.rate + 1) * 6000;
    tapeSignalBuilderReserve(b, slots > 0x20000000ul ? 0x20000000ul : (UInt32)slots);

    gapT = (UInt32)((UInt64)GAP_MS * w.rate / 1000);
    tapeSignalBuilderAddIndex(b, TAPE_CUSTOM, "1", 0);

    dc = frameAt(&w, 0);
    for (i = 0; i < w.count; i++) {
        Int32 v = frameAt(&w, i);
        Int32 y;
        int   want;

        dc += (v - dc) >> dcShift;
        y = v - dc;

        want = level;
        if (y > thr) {
            want = 1;
        }
        else if (y < -thr) {
            want = 0;
        }
        if (want == level || i - lastEdge < minGap) {
            continue;
        }

        /* A long flat stretch is the gap between blocks, and a natural place
        ** for the position dialog to seek to. Raw audio carries no names, so
        ** the blocks are just numbered; with no index entry at all the dialog
        ** has nothing to offer and the tape could only be rewound. */
        if (i - lastEdge > gapT) {
            char name[8];

            sprintf(name, "%d", ++block);
            tapeSignalBuilderAddIndex(b, TAPE_CUSTOM, name, tapeSignalBuilderTimeAsByte(b));
        }

        addSpan(b, &acc, i - lastEdge, w.rate);

        level    = want;
        lastEdge = i;
    }

    if (w.count > lastEdge) {
        addSpan(b, &acc, w.count - lastEdge, w.rate);
    }

    if (tapeSignalBuilderFailed(b)) {
        fail(err, errSize, "Out of memory");
        tapeSignalBuilderDestroy(b);
        return NULL;
    }
    return b;
}
