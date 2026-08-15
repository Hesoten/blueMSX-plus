/*****************************************************************************
**
** Kansas City Standard waveform generation for CAS tape images.
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
#include "CasToWave.h"
#include <stdlib.h>
#include <string.h>

/* Pulse widths are counted in units of one 2400 Hz half cycle so that both
** bit cells stay on a single exact accumulator. */
#define HALF_CYCLES_PER_SEC  4800

/* The MSX BIOS writes 16000 cycles of 2400 Hz for a long header and 4000 for
** a short one, which is 4 half cycles per "1" bit cell. */
#define LONG_HEADER_PULSES   32000  /* 16000 cycles, about 6.7 s */
#define SHORT_HEADER_PULSES   8000  /*  4000 cycles, about 1.7 s */
#define LONG_GAP_MS           2000
#define SHORT_GAP_MS          1000

/* Worst case pulses per byte: 11 bit cells of 4 pulses each */
#define MAX_PULSES_PER_BYTE  44

typedef struct {
    TapeSignalBuilder* b;
    UInt32             acc;
    const UInt8*       marker;
    int                markerSize;
} CasEnc;

/* Emit one polarity flip lasting the given number of 2400 Hz half cycles */
static void emitUnits(CasEnc* e, UInt32 units)
{
    e->acc += TAPE_TSTATE_FREQ * units;
    tapeSignalBuilderAddPulse(e->b, e->acc / HALF_CYCLES_PER_SEC);
    e->acc %= HALF_CYCLES_PER_SEC;
}

static void emitBit(CasEnc* e, int bit)
{
    if (bit) {
        /* "1" is two cycles of 2400 Hz */
        emitUnits(e, 1);
        emitUnits(e, 1);
        emitUnits(e, 1);
        emitUnits(e, 1);
    }
    else {
        /* "0" is one cycle of 1200 Hz */
        emitUnits(e, 2);
        emitUnits(e, 2);
    }
}

/* One start bit (0), 8 data bits LSB first, two stop bits (1) */
static void emitByte(CasEnc* e, UInt8 value)
{
    int i;

    emitBit(e, 0);
    for (i = 0; i < 8; i++) {
        emitBit(e, (value >> i) & 1);
    }
    emitBit(e, 1);
    emitBit(e, 1);
}

static void emitHeaderTone(CasEnc* e, UInt32 pulses)
{
    while (pulses-- > 0) {
        emitUnits(e, 1);
    }
}

/* Offset of the next block marker at or after from, or size if there is none.
** Scanned byte by byte: only well formed fMSX-DOS images are 8 byte aligned,
** and a tape saved in the emulator is not padded at all. */
static int findMarker(const CasEnc* e, const UInt8* data, int size, int from)
{
    int offset;

    for (offset = from; offset + e->markerSize <= size; offset++) {
        if (0 == memcmp(data + offset, e->marker, e->markerSize)) {
            return offset;
        }
    }
    return size;
}

static int countMarkers(const CasEnc* e, const UInt8* data, int size)
{
    int count  = 0;
    int offset = 0;

    while ((offset = findMarker(e, data, size, offset)) < size) {
        count++;
        offset += e->markerSize;
    }
    return count;
}

/* A file header block starts with ten copies of the type byte */
static int fileHeaderType(const UInt8* data, int size, int offset, TapeContentType* type)
{
    UInt8 id;
    int   i;

    if (offset + 10 > size) {
        return 0;
    }
    id = data[offset];
    if (id != 0xd0 && id != 0xd3 && id != 0xea) {
        return 0;
    }
    for (i = 1; i < 10; i++) {
        if (data[offset + i] != id) {
            return 0;
        }
    }

    switch (id) {
    case 0xd0: *type = TAPE_BINARY; break;
    case 0xd3: *type = TAPE_BASIC;  break;
    default:   *type = TAPE_ASCII;  break;
    }
    return 1;
}

TapeSignalBuilder* casToWave(const UInt8* data, int size,
                             const UInt8* marker, int markerSize)
{
    CasEnc e;
    int    offset;
    int    firstBlock = 1;
    UInt64 slots;

    if (data == NULL || size <= 0 || marker == NULL || markerSize <= 0) {
        return NULL;
    }

    e.b          = NULL;
    e.acc        = 0;
    e.marker     = marker;
    e.markerSize = markerSize;

    /* The pulse array is indexed by UInt32, so refuse an image that cannot fit */
    slots = (UInt64)size * MAX_PULSES_PER_BYTE
          + (UInt64)(countMarkers(&e, data, size) + 1) * LONG_HEADER_PULSES;
    if (slots > TAPE_MAX_PULSES) {
        return NULL;
    }

    e.b = tapeSignalBuilderCreate();
    if (e.b == NULL) {
        return NULL;
    }
    tapeSignalBuilderReserve(e.b, (UInt32)slots);

    offset = findMarker(&e, data, size, 0);

    if (offset >= size) {
        /* No block structure, so play the whole image as one data block */
        tapeSignalBuilderMarkBytePos(e.b, 0);
        tapeSignalBuilderAddSilenceMs(e.b, LONG_GAP_MS);
        emitHeaderTone(&e, LONG_HEADER_PULSES);
        for (offset = 0; offset < size; offset++) {
            emitByte(&e, data[offset]);
        }
    }

    while (offset < size) {
        int             dataStart = offset + markerSize;
        int             next      = findMarker(&e, data, size, dataStart);
        TapeContentType type;
        int             isHeader  = fileHeaderType(data, size, dataStart, &type);

        /* Mark before the lead-in so that seeking to this block replays its
        ** header tone; the BIOS needs it to sync. */
        tapeSignalBuilderMarkBytePos(e.b, (UInt32)offset);

        /* Every block is preceded by a gap: the BIOS stops the motor between
        ** the file header and the data, so a real tape has one there too. */
        if (isHeader || firstBlock) {
            tapeSignalBuilderAddSilenceMs(e.b, LONG_GAP_MS);
            emitHeaderTone(&e, LONG_HEADER_PULSES);
        }
        else {
            tapeSignalBuilderAddSilenceMs(e.b, SHORT_GAP_MS);
            emitHeaderTone(&e, SHORT_HEADER_PULSES);
        }

        if (isHeader) {
            char name[8];
            int  i;
            memset(name, 0, sizeof(name));
            for (i = 0; i < 6 && dataStart + 10 + i < size; i++) {
                UInt8 c = data[dataStart + 10 + i];
                name[i] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
            }
            tapeSignalBuilderAddIndex(e.b, type, name, (UInt32)offset);
        }

        for (; dataStart < next; dataStart++) {
            emitByte(&e, data[dataStart]);
        }

        firstBlock = 0;
        offset     = next;
    }

    tapeSignalBuilderAddSilenceMs(e.b, SHORT_GAP_MS);

    /* End mark, so seeking to the end of the image lands past the last block
    ** instead of replaying it. */
    tapeSignalBuilderMarkBytePos(e.b, (UInt32)size);

    if (tapeSignalBuilderFailed(e.b)) {
        tapeSignalBuilderDestroy(e.b);
        return NULL;
    }
    return e.b;
}
