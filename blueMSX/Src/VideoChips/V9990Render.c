/*****************************************************************************
**
** Yamaha V9990 line renderer.
** Parts follow or are adapted from openMSX's V9990 implementation.
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
#include "V9990Render.h"
#include "V9990.h"
#include "FrameBuffer.h"

/* The DAC is linear: 5-bit levels widen to 8 bits by bit replication. */
#define COLOR5(r, g, b) ((UInt16)videoGetColor(((r) << 3) | ((r) >> 2), \
                                               ((g) << 3) | ((g) >> 2), \
                                               ((b) << 3) | ((b) >> 2)))

UInt16 v9990PaletteColor(const UInt8* palette, int index)
{
    const UInt8* entry = palette + 4 * (index & 0x3f);
    int r = entry[0] & 0x1f;
    int g = entry[1] & 0x1f;
    int b = entry[2] & 0x1f;

    return COLOR5(r, g, b);
}

/* Bitmap modes interleave the two VRAM banks byte by byte. */
static UInt8 readBx(const UInt8* vram, UInt32 address)
{
    return vram[V9990_MAP_BX(address)];
}

/* palOdd colours the odd image dots; it is pal except in the dual-palette
** high-resolution modes. */
static void drawBp2(UInt16* dst, int count, const UInt8* vram, const UInt16* pal,
                    const UInt16* palOdd, UInt32 pos)
{
    UInt32 address = pos >> 2;
    int shift = 6 - 2 * (int)(pos & 3);
    UInt8 data = readBx(vram, address);
    int i;

    for (i = 0; i < count; i++) {
        dst[i] = (((pos + i) & 1) ? palOdd : pal)[(data >> shift) & 3];
        shift -= 2;
        if (shift < 0) {
            shift = 6;
            data = readBx(vram, ++address);
        }
    }
}

static void drawBp4(UInt16* dst, int count, const UInt8* vram, const UInt16* pal,
                    const UInt16* palOdd, UInt32 pos)
{
    UInt32 address = pos >> 1;
    int i = 0;

    if ((pos & 1) && count > 0) {
        dst[i++] = palOdd[readBx(vram, address++) & 0x0f];
    }
    for (; i + 1 < count; i += 2) {
        UInt8 data = readBx(vram, address++);
        dst[i]     = pal[data >> 4];
        dst[i + 1] = palOdd[data & 0x0f];
    }
    if (i < count) {
        dst[i] = pal[readBx(vram, address) >> 4];
    }
}

static void drawBp6(UInt16* dst, int count, const UInt8* vram, const UInt16* pal, UInt32 pos)
{
    int i;

    for (i = 0; i < count; i++) {
        dst[i] = pal[readBx(vram, pos + i) & 0x3f];
    }
}

/* G3 R3 B2, widened to 5 bits as the DAC wiring does it. */
static void drawBd8(UInt16* dst, int count, const UInt8* vram, UInt32 pos)
{
    static const UInt8 level3[8] = { 0, 4, 9, 13, 18, 22, 27, 31 };
    static const UInt8 level2[4] = { 0, 11, 21, 31 };
    static UInt16 table[256];
    static int tableReady = 0;
    int i;

    if (!tableReady) {
        for (i = 0; i < 256; i++) {
            table[i] = COLOR5(level3[(i >> 2) & 7], level3[i >> 5], level2[i & 3]);
        }
        tableReady = 1;
    }
    for (i = 0; i < count; i++) {
        dst[i] = table[readBx(vram, pos + i)];
    }
}

/* G5 R5 B5; bit 15 (YS) only keys a superimposed picture. The low byte of
** a pixel is in bank 0 and the high byte in bank 1. */
static void drawBd16(UInt16* dst, int count, const UInt8* vram, UInt32 pos)
{
    int i;

    for (i = 0; i < count; i++) {
        UInt32 p = (pos + i) << 1;
        int value = vram[V9990_MAP_BX(p)] | (vram[V9990_MAP_BX(p + 1)] << 8);
        int g = (value >> 10) & 0x1f;
        int r = (value >> 5) & 0x1f;
        int b = value & 0x1f;
        dst[i] = COLOR5(r, g, b);
    }
}

int v9990RenderBitmapLine(UInt16* dst, int count, const UInt8* vram, const UInt8* palette,
                          UInt8 r6, UInt8 r13, UInt32 x, UInt32 y, int hiRes)
{
    UInt32 pos = y * (256u << ((r6 >> 2) & 3)) + x;
    UInt16 pal[64];
    int offset;
    int odd;
    int i;

    if (count <= 0) {
        return 1;
    }

    /* In high resolution, PLTO5 is replaced by the dot's image x parity. */
    switch (r6 & 3) {
    case 0:
        offset = (r13 & (hiRes ? 0x07 : 0x0f)) << 2;
        odd = hiRes ? 32 : 0;
        for (i = 0; i < 4; i++) {
            pal[i]     = v9990PaletteColor(palette, offset + i);
            pal[i + 4] = v9990PaletteColor(palette, offset + odd + i);
        }
        drawBp2(dst, count, vram, pal, pal + 4, pos);
        return 1;

    case 1:
        offset = (r13 & (hiRes ? 0x04 : 0x0c)) << 2;
        odd = hiRes ? 32 : 0;
        for (i = 0; i < 16; i++) {
            pal[i]      = v9990PaletteColor(palette, offset + i);
            pal[i + 16] = v9990PaletteColor(palette, offset + odd + i);
        }
        drawBp4(dst, count, vram, pal, pal + 16, pos);
        return 1;

    case 2:
        switch (r13 >> 6) {
        case 0:
            for (i = 0; i < 64; i++) {
                pal[i] = v9990PaletteColor(palette, i);
            }
            drawBp6(dst, count, vram, pal, pos);
            return 1;
        case 1:
            drawBd8(dst, count, vram, pos);
            return 1;
        }
        return 0;

    default:
        drawBd16(dst, count, vram, pos);
        return 1;
    }
}
