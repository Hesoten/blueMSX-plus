/*****************************************************************************
**
** TZX 1.2x / TSX tape image parsing.
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
#include "TsxParser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const UInt8 tsxSignature[8] = { 'Z','X','T','a','p','e','!', 0x1a };

/* TZX durations are quoted against the ZX Spectrum clock, not the MSX one.
** Skipping this conversion makes every block 2.27 percent too slow. */
#define TZX_TSTATE_FREQ  3500000

/* Standard speed block constants from the TZX specification */
#define STD_PILOT        2168
#define STD_SYNC1         667
#define STD_SYNC2         735
#define STD_ZERO          855
#define STD_ONE          1710
#define STD_PILOT_HEADER 8063
#define STD_PILOT_DATA   3223

/* Rough upper bound on pulses per data byte, used to size the array up front */
#define PULSES_PER_BYTE    64

typedef struct {
    const UInt8*       buf;
    int                size;
    int                pos;
    TapeSignalBuilder* b;
    char*              err;
    int                errSize;
    int                failed;
} Tsx;

/*****************************************************************************
** Reader
******************************************************************************/

static void fail(Tsx* t, const char* msg)
{
    if (t->failed) {
        return;
    }
    t->failed = 1;
    if (t->err != NULL && t->errSize > 0) {
        strncpy(t->err, msg, t->errSize - 1);
        t->err[t->errSize - 1] = 0;
    }
}

static int need(Tsx* t, int n)
{
    if (t->failed) {
        return 0;
    }
    if (n < 0 || n > t->size - t->pos) {
        fail(t, "Truncated TSX image");
        return 0;
    }
    return 1;
}

static UInt32 u8(Tsx* t)
{
    return need(t, 1) ? t->buf[t->pos++] : 0;
}

static UInt32 u16(Tsx* t)
{
    UInt32 v;
    if (!need(t, 2)) {
        return 0;
    }
    v = (UInt32)t->buf[t->pos] | ((UInt32)t->buf[t->pos + 1] << 8);
    t->pos += 2;
    return v;
}

static UInt32 u24(Tsx* t)
{
    UInt32 v;
    if (!need(t, 3)) {
        return 0;
    }
    v = (UInt32)t->buf[t->pos] | ((UInt32)t->buf[t->pos + 1] << 8)
      | ((UInt32)t->buf[t->pos + 2] << 16);
    t->pos += 3;
    return v;
}

static UInt32 u32(Tsx* t)
{
    UInt32 v;
    if (!need(t, 4)) {
        return 0;
    }
    v = (UInt32)t->buf[t->pos] | ((UInt32)t->buf[t->pos + 1] << 8)
      | ((UInt32)t->buf[t->pos + 2] << 16) | ((UInt32)t->buf[t->pos + 3] << 24);
    t->pos += 4;
    return v;
}

static const UInt8* block(Tsx* t, UInt32 n)
{
    const UInt8* p;
    if (n > 0x7fffffffu) {
        fail(t, "Truncated TSX image");
        return NULL;
    }
    if (!need(t, (int)n)) {
        return NULL;
    }
    p = t->buf + t->pos;
    t->pos += (int)n;
    return p;
}

static void skip(Tsx* t, UInt32 n)
{
    block(t, n);
}

/*****************************************************************************
** Waveform output
******************************************************************************/

/* Clamped before the multiply: a direct recording run can reach 2^43 T-states,
** which would overflow even 64 bits once scaled. */
static UInt32 tzx2msx(UInt64 tzx)
{
    UInt64 v;

    if (tzx > 0xfffffffful) {
        return 0xfffffffful;
    }
    v = (tzx * TAPE_TSTATE_FREQ + TZX_TSTATE_FREQ / 2) / TZX_TSTATE_FREQ;
    return v > 0xfffffffful ? 0xfffffffful : (UInt32)v;
}

static void pulse(Tsx* t, UInt64 tzx)
{
    tapeSignalBuilderAddPulse(t->b, tzx2msx(tzx));
}

static void pulses(Tsx* t, UInt32 count, UInt32 tzx)
{
    tapeSignalBuilderAddPulses(t->b, tzx2msx(tzx), count);
}

/* A pause keeps the current polarity, so the next block starts cleanly */
static void silence(Tsx* t, UInt32 ms)
{
    tapeSignalBuilderAddSilenceMs(t->b, ms);
}

/*****************************************************************************
** Blocks
******************************************************************************/

/* Shared by the standard (#10) and turbo (#11) speed blocks */
static void dataBlock(Tsx* t, UInt32 pilot, UInt32 sync1, UInt32 sync2,
                      UInt32 zero, UInt32 one, UInt32 pilotLen,
                      UInt32 lastBits, UInt32 pauseMs, UInt32 len)
{
    const UInt8* data;
    UInt32 i;

    if (len < 1 || lastBits < 1 || lastBits > 8) {
        fail(t, "Invalid TZX data block");
        return;
    }
    data = block(t, len);
    if (data == NULL) {
        return;
    }

    pulses(t, pilotLen, pilot);
    pulse(t, sync1);
    pulse(t, sync2);

    for (i = 0; i < len; i++) {
        UInt32 bits = (i == len - 1) ? lastBits : 8;
        UInt32 bit;
        for (bit = 0; bit < bits; bit++) {
            pulses(t, 2, (data[i] & (0x80 >> bit)) ? one : zero);
        }
    }

    if (pauseMs != 0) {
        pulse(t, 2000);
        silence(t, pauseMs);
    }
}

static void block10(Tsx* t)
{
    UInt32 pauseMs = u16(t);
    UInt32 len     = u16(t);
    UInt32 pilotLen;

    if (!need(t, (int)len) || len < 1) {
        fail(t, "Invalid TZX data block");
        return;
    }
    /* The flag byte selects the lead-in length: long for a header block,
    ** short for a data block. */
    pilotLen = t->buf[t->pos] < 0x80 ? STD_PILOT_HEADER : STD_PILOT_DATA;

    dataBlock(t, STD_PILOT, STD_SYNC1, STD_SYNC2, STD_ZERO, STD_ONE,
              pilotLen, 8, pauseMs, len);
}

static void block11(Tsx* t)
{
    UInt32 pilot    = u16(t);
    UInt32 sync1    = u16(t);
    UInt32 sync2    = u16(t);
    UInt32 zero     = u16(t);
    UInt32 one      = u16(t);
    UInt32 pilotLen = u16(t);
    UInt32 lastBits = u8(t);
    UInt32 pauseMs  = u16(t);
    UInt32 len      = u24(t);

    dataBlock(t, pilot, sync1, sync2, zero, one, pilotLen, lastBits, pauseMs, len);
}

static void block12(Tsx* t)
{
    UInt32 len    = u16(t);
    UInt32 count  = u16(t);

    /* An odd count would leave the polarity inverted for the next block */
    pulses(t, count & ~1u, len);
}

static void block13(Tsx* t)
{
    UInt32 count = u8(t);
    UInt32 i;

    for (i = 0; i < count; i++) {
        pulse(t, u16(t));
    }
}

/* Direct recording: the data is a stream of levels rather than edges, so a
** run of equal samples becomes a single pulse. */
static void block15(Tsx* t)
{
    UInt32 bitTstates = u16(t);
    UInt32 pauseMs    = u16(t);
    UInt32 lastBits   = u8(t);
    UInt32 len        = u24(t);
    const UInt8* data;
    UInt32 i;
    UInt64 run = 0;
    int    cur = -1;

    if (len < 1 || lastBits < 1 || lastBits > 8) {
        fail(t, "Invalid TZX block #15");
        return;
    }
    data = block(t, len);
    if (data == NULL) {
        return;
    }

    for (i = 0; i < len; i++) {
        UInt32 bits = (i == len - 1) ? lastBits : 8;
        UInt32 bit;
        for (bit = 0; bit < bits; bit++) {
            int v = (data[i] >> (7 - bit)) & 1;
            if (cur >= 0 && v != cur) {
                pulse(t, run * bitTstates);
                run = 0;
            }
            cur = v;
            run++;
        }
    }
    if (run != 0) {
        pulse(t, run * bitTstates);
    }

    silence(t, pauseMs);
}

static void block20(Tsx* t)
{
    /* A pause of zero means stop the tape, which needs no waveform */
    silence(t, u16(t));
}

static void block32(Tsx* t)
{
    UInt32 len = u16(t);

    if (len < 1) {
        fail(t, "Invalid TZX block #32");
        return;
    }
    skip(t, len);
}

static void block35(Tsx* t)
{
    skip(t, 0x10);
    skip(t, u32(t));
}

/* Records a file entry when the block holds a 16 byte MSX file header */
static void indexFileHeader(Tsx* t, const UInt8* data, UInt32 len)
{
    TapeContentType type;
    char name[8];
    int  i;

    if (len != 16) {
        return;
    }
    switch (data[0]) {
    case 0xd0: type = TAPE_BINARY; break;
    case 0xd3: type = TAPE_BASIC;  break;
    case 0xea: type = TAPE_ASCII;  break;
    default:   return;
    }
    for (i = 1; i < 10; i++) {
        if (data[i] != data[0]) {
            return;
        }
    }

    memset(name, 0, sizeof(name));
    for (i = 0; i < 6; i++) {
        UInt8 c = data[10 + i];
        name[i] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
    }
    tapeSignalBuilderAddIndex(t->b, type, name, tapeSignalBuilderTimeAsByte(t->b));
}

/* MSX Kansas City Standard block */
static void block4B(Tsx* t)
{
    UInt32 blockLen = u32(t);
    UInt32 pauseMs  = u16(t);
    UInt32 pilot    = u16(t);
    UInt32 nPilot   = u16(t);
    UInt32 bit0len  = u16(t);
    UInt32 bit1len  = u16(t);
    UInt32 bitCfg   = u8(t);
    UInt32 byteCfg  = u8(t);
    const UInt8* data;
    UInt32 zeroPulses, onePulses;
    UInt32 startBits, stopBits;
    UInt32 startVal, stopVal;
    UInt32 msbFirst;
    UInt32 len, i, n;

    if (t->failed) {
        return;
    }
    if (blockLen < 12) {
        fail(t, "Invalid TSX block #4B: length");
        return;
    }
    len  = blockLen - 12;
    data = block(t, len);
    if (data == NULL) {
        return;
    }

    /* A nibble of zero means sixteen pulses, not none */
    zeroPulses = (bitCfg >> 4)  ? (bitCfg >> 4)  : 16;
    onePulses  = (bitCfg & 0xf) ? (bitCfg & 0xf) : 16;

    startBits = (byteCfg & 0xc0) >> 6;
    startVal  = (byteCfg & 0x20) >> 5;
    stopBits  = (byteCfg & 0x18) >> 3;
    stopVal   = (byteCfg & 0x04) >> 2;
    msbFirst  =  byteCfg & 0x01;
    if (byteCfg & 0x02) {
        fail(t, "Invalid TSX block #4B: reserved byte-cfg bit set");
        return;
    }

    indexFileHeader(t, data, len);

    pulses(t, nPilot, pilot);

    for (i = 0; i < len; i++) {
        for (n = 0; n < startBits; n++) {
            pulses(t, startVal ? onePulses : zeroPulses, startVal ? bit1len : bit0len);
        }
        for (n = 0; n < 8; n++) {
            UInt32 mask = 1u << (msbFirst ? (7 - n) : n);
            if (data[i] & mask) {
                pulses(t, onePulses, bit1len);
            }
            else {
                pulses(t, zeroPulses, bit0len);
            }
        }
        for (n = 0; n < stopBits; n++) {
            pulses(t, stopVal ? onePulses : zeroPulses, stopVal ? bit1len : bit0len);
        }
    }

    silence(t, pauseMs);
}

/*****************************************************************************
** Entry points
******************************************************************************/

int tsxIsTsxImage(const UInt8* data, int size)
{
    return data != NULL && size >= 10 &&
           0 == memcmp(data, tsxSignature, sizeof(tsxSignature));
}

TapeSignalBuilder* tsxToWave(const UInt8* data, int size, char* err, int errSize)
{
    Tsx    t;
    UInt64 slots;

    if (err != NULL && errSize > 0) {
        *err = 0;
    }
    if (!tsxIsTsxImage(data, size)) {
        return NULL;
    }

    memset(&t, 0, sizeof(t));
    t.buf     = data;
    t.size    = size;
    t.err     = err;
    t.errSize = errSize;
    t.pos     = 8;

    if (u8(&t) != 1) {
        fail(&t, "Unsupported TZX major version");
        return NULL;
    }
    u8(&t);                     /* minor version, forward compatible */

    t.b = tapeSignalBuilderCreate();
    if (t.b == NULL) {
        fail(&t, "Out of memory");
        return NULL;
    }
    slots = (UInt64)size * PULSES_PER_BYTE;
    tapeSignalBuilderReserve(t.b, slots > TAPE_MAX_PULSES ? TAPE_MAX_PULSES : (UInt32)slots);

    /* Block #12 buys 65534 pulses for five bytes, so a small image can still ask
    ** for more than the builder accepts. Stop as soon as it says no. */
    while (!t.failed && !tapeSignalBuilderFailed(t.b) && t.pos < t.size) {
        UInt32 id = u8(&t);

        switch (id) {
        case 0x10: block10(&t); break;
        case 0x11: block11(&t); break;
        case 0x12: block12(&t); break;
        case 0x13: block13(&t); break;
        case 0x15: block15(&t); break;
        case 0x20: block20(&t); break;
        case 0x21: skip(&t, u8(&t)); break;     /* group start */
        case 0x22: break;                       /* group end, no payload */
        case 0x30: skip(&t, u8(&t)); break;     /* text description */
        case 0x32: block32(&t); break;
        case 0x35: block35(&t); break;
        case 0x4b: block4B(&t); break;
        case 0x5a: skip(&t, 9); break;          /* glue block */
        default: {
            char msg[64];
            sprintf(msg, "Unsupported TZX block #%02X", (unsigned)id);
            fail(&t, msg);
            break;
        }
        }
    }

    if (t.failed || tapeSignalBuilderFailed(t.b)) {
        if (!t.failed) {
            fail(&t, "Out of memory");
        }
        tapeSignalBuilderDestroy(t.b);
        return NULL;
    }

    return t.b;
}
