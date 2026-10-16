/*****************************************************************************
**
** Yamaha V9990 pattern-mode and sprite renderer.
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

#define P1_WIDTH 256
#define P2_WIDTH 512

#define P1_NAME_A    0x7c000
#define P1_NAME_B    0x7e000
#define P1_PATTERN_B 0x40000
#define P2_NAME      0x7c000

/* Physical address of the sprite attribute table in both modes. */
#define SPRITE_TABLE 0x3fe00
#define SPRITE_COUNT 125
#define SPRITES_PER_LINE 16

/* What a dot shows, for sprite priority. */
#define PLANE_REAR   0
#define PLANE_FRONT  1
#define PLANE_SPRITE 2

static UInt32 patternNumber(const UInt8* vram, UInt32 nameAddress)
{
    return (vram[nameAddress] | (vram[nameAddress + 1] << 8)) & 0x3fff;
}

/* Reads count 4-bit dots of a P1 layer, from image dot x of image line y. */
static void fetchP1Layer(UInt8* out, int count, const UInt8* vram,
                         UInt32 nameTable, UInt32 patternTable, UInt32 x, UInt32 y)
{
    UInt32 nameRow = nameTable + (y >> 3) * 128;
    UInt32 lineOffset = (y & 7) * 128;
    int i = 0;

    while (i < count) {
        UInt32 n = patternNumber(vram, nameRow + ((x >> 3) & 63) * 2);
        UInt32 address = patternTable + (n >> 5) * 1024 + (n & 31) * 4 + lineOffset;
        int sub;

        for (sub = x & 7; sub < 8 && i < count; sub++, i++, x++) {
            UInt8 data = vram[(address + (sub >> 1)) & V9990_VRAM_MASK];
            out[i] = (sub & 1) ? data & 0x0f : data >> 4;
        }
    }
}

/* P2 has one layer twice as wide; its generator is interleaved over both banks. */
static void fetchP2Layer(UInt8* out, int count, const UInt8* vram, UInt32 x, UInt32 y)
{
    UInt32 nameRow = P2_NAME + (y >> 3) * 256;
    UInt32 lineOffset = (y & 7) * 256;
    int i = 0;

    while (i < count) {
        UInt32 n = patternNumber(vram, nameRow + ((x >> 3) & 127) * 2);
        UInt32 address = (n >> 6) * 2048 + (n & 63) * 4 + lineOffset;
        int sub;

        for (sub = x & 7; sub < 8 && i < count; sub++, i++, x++) {
            UInt8 data = vram[V9990_MAP_BX(address + (sub >> 1))];
            out[i] = (sub & 1) ? data & 0x0f : data >> 4;
        }
    }
}

static void drawSprites(UInt8* index, UInt8* plane, int width, const UInt8* vram,
                        const V9990PatternLine* line)
{
    int visible[SPRITES_PER_LINE];
    int limit = SPRITES_PER_LINE;
    int found = 0;
    int i;

    /* A sprite hidden by PR0 still takes one of the 16 places on its lines. */
    for (i = 0; i < SPRITE_COUNT && found < limit; i++) {
        const UInt8* attr = vram + SPRITE_TABLE + 4 * i;
        if (((line->displayY - attr[0] - 1) & 0xff) < 16) {
            if (attr[3] & 0x10) {
                limit--;
            }
            else {
                visible[found++] = i;
            }
        }
    }

    for (i = 0; i < found; i++) {
        const UInt8* attr = vram + SPRITE_TABLE + 4 * visible[i];
        int row = (line->displayY - attr[0] - 1) & 0xff;
        int x = attr[2] | ((attr[3] & 0x03) << 8);
        int level = (attr[3] & 0x20) ? PLANE_FRONT : PLANE_SPRITE;
        int offset = (attr[3] >> 2) & 0x30;
        UInt8 no = attr[1];
        UInt32 address;
        int j;

        if (x > 1024 - 16) {
            x -= 1024;
        }
        if (line->p2) {
            address = ((line->r25 & 0x0f) << 15) + 256 * (((no & 0xe0) >> 1) + row) + 8 * (no & 0x1f);
        }
        else {
            address = ((line->r25 & 0x0e) << 14) + 128 * ((no & 0xf0) + row) + 8 * (no & 0x0f);
        }

        /* A dot of a sprite, even one behind the front layer, hides the
        ** sprites after it. */
        for (j = 0; j < 16; j++) {
            int xx = x + j;
            UInt32 a = (address + (j >> 1)) & V9990_VRAM_MASK;
            UInt8 data;
            int p;

            if (xx < 0 || xx >= width) {
                continue;
            }
            data = vram[line->p2 ? V9990_MAP_BX(a) : a];
            p = (j & 1) ? data & 0x0f : data >> 4;
            if (p != 0) {
                if (plane[xx] < level) {
                    index[xx] = (UInt8)(offset + p);
                }
                plane[xx] = PLANE_SPRITE;
            }
        }
    }
}

static void composeP1(UInt8* index, UInt8* plane, const UInt8* vram, const V9990PatternLine* line)
{
    UInt8 a[P1_WIDTH];
    UInt8 b[P1_WIDTH];
    int offsetA = (line->r13 & 0x03) << 4;
    int offsetB = (line->r13 & 0x0c) << 2;
    int backdrop = line->r15 & 0x3f;
    int prioX = (line->r27 & 0x03) ? (line->r27 & 0x03) << 6 : P1_WIDTH;
    int prioY = (line->r27 & 0x0c) ? (line->r27 & 0x0c) << 4 : P1_WIDTH;
    int x;

    fetchP1Layer(a, P1_WIDTH, vram, P1_NAME_A, 0, line->ax, line->ay);
    fetchP1Layer(b, P1_WIDTH, vram, P1_NAME_B, P1_PATTERN_B, line->bx, line->by);

    /* Layer B is in front from dot PRX*64 and from line PRY*64 on. */
    if (line->displayY >= prioY) {
        prioX = 0;
    }
    for (x = 0; x < P1_WIDTH; x++) {
        int frontB = x >= prioX;
        int front = frontB ? b[x] : a[x];
        int rear  = frontB ? a[x] : b[x];

        if (front != 0) {
            index[x] = (UInt8)((frontB ? offsetB : offsetA) + front);
            plane[x] = PLANE_FRONT;
        }
        else {
            index[x] = (UInt8)(rear != 0 ? (frontB ? offsetA : offsetB) + rear : backdrop);
            plane[x] = PLANE_REAR;
        }
    }
}

static void composeP2(UInt8* index, UInt8* plane, const UInt8* vram, const V9990PatternLine* line)
{
    UInt8 a[P2_WIDTH];
    int offset0 = (line->r13 & 0x03) << 4;
    int offset1 = (line->r13 & 0x0c) << 2;
    int backdrop = line->r15 & 0x3f;
    int x;

    fetchP2Layer(a, P2_WIDTH, vram, line->ax, line->ay);

    /* Dots 2-3 and 6-7 of each pattern take the second palette offset. */
    for (x = 0; x < P2_WIDTH; x++) {
        if (a[x] != 0) {
            index[x] = (UInt8)((((line->ax + x) & 2) ? offset1 : offset0) + a[x]);
            plane[x] = PLANE_FRONT;
        }
        else {
            index[x] = (UInt8)backdrop;
            plane[x] = PLANE_REAR;
        }
    }
}

void v9990RenderPatternLine(UInt16* dst, int count, const UInt8* vram, const UInt8* palette,
                            const V9990PatternLine* line, int dot)
{
    UInt8 index[P2_WIDTH];
    UInt8 plane[P2_WIDTH];
    UInt16 pal[64];
    int width = line->p2 ? P2_WIDTH : P1_WIDTH;
    int i;

    if (dot < 0) {
        dst -= dot;
        count += dot;
        dot = 0;
    }
    if (count > width - dot) {
        count = width - dot;
    }
    if (count <= 0) {
        return;
    }

    if (line->p2) {
        composeP2(index, plane, vram, line);
    }
    else {
        composeP1(index, plane, vram, line);
    }
    if (!(line->r8 & 0x40)) {
        drawSprites(index, plane, width, vram, line);
    }

    for (i = 0; i < 64; i++) {
        pal[i] = v9990PaletteColor(palette, i);
    }
    for (i = 0; i < count; i++) {
        dst[i] = pal[index[dot + i]];
    }
}
