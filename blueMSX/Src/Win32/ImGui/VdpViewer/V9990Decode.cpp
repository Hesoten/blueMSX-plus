/*****************************************************************************
**
** Decoding of V9990 name tables, patterns and palettes.
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
#include "V9990Decode.h"
extern "C" {
#include "V9990Render.h"
}

/* The layers read through the renderer's own fetch, so the pictures are its dots. */

bool v9990LayerOf(const V9990DebugState* s, int layer, V9990Layer* out)
{
    bool p2 = s->mode == V9990_MODE_P2;
    if ((s->mode != V9990_MODE_P1 && !p2) || layer < 0 || layer > (p2 ? 0 : 1)) return false;
    const UInt8* r = s->regs;
    V9990Layer l = {};
    l.w = p2 ? 1024 : 512;
    l.h = 512;
    l.perRow = p2 ? 64 : 32;
    l.screenW = p2 ? 512 : 256;
    if (layer == 0) {
        int roll = s->scrollAYHi >> 6;
        l.names = 0x7c000;
        l.patterns = 0;
        /* A holds 00000-3FDFF in P1; P2's generator runs up to the sprite area at 78000. */
        l.rows = p2 ? 0x78000 / 2048 : 0x3fe00 / 1024;
        l.sx = (int)((r[19] | ((UInt32)r[20] << 3)) & (p2 ? 0x3ff : 0x1ff));
        l.sy = (int)((r[17] | ((UInt32)s->scrollAYHi << 8)) & 0x1ff);
        l.rollMask = roll == 0 || roll == 2 ? 0x1ff : 0xff;
    }
    else {
        /* B always rolls at 512 lines. */
        l.names = 0x7e000;
        l.patterns = 0x40000;
        l.rows = (0x7c000 - 0x40000) / 1024;
        l.sx = (int)((r[23] | ((UInt32)r[24] << 3)) & 0x1ff);
        l.sy = (int)((r[21] | ((UInt32)(s->scrollBYHi & 1) << 8)) & 0x1ff);
        l.rollMask = 0x1ff;
    }
    *out = l;
    return true;
}

UInt8 v9990Byte(const V9990DebugState* s, UInt32 a)
{
    return s->vram[v9990MapAddress((V9990Mode)s->mode, a)];
}

int v9990NameAt(const V9990DebugState* s, const V9990Layer& l, int col, int row, UInt32* nameAddr)
{
    UInt32 a = l.names + (UInt32)(row * (l.w / 8) + col) * 2;
    if (nameAddr) *nameAddr = a;
    return (v9990Byte(s, a) | (v9990Byte(s, a + 1) << 8)) & 0x3fff;
}

UInt32 v9990PatternRowAddr(const V9990Layer& l, int n, int r)
{
    int pitch = l.perRow * 4;
    return (l.patterns + (UInt32)(n / l.perRow) * pitch * 8 + (UInt32)(n % l.perRow) * 4 + (UInt32)r * pitch) & V9990_VRAM_MASK;
}

int v9990LayerColour(const V9990DebugState* s, int layer, int x, int v)
{
    int r13 = s->regs[13];
    if (s->mode == V9990_MODE_P2) return (((x & 2) ? (r13 & 0x0c) << 2 : (r13 & 0x03) << 4) + v) & 63;
    return ((layer == 0 ? (r13 & 0x03) << 4 : (r13 & 0x0c) << 2) + v) & 63;
}

void v9990DecodeLayer(const V9990DebugState* s, const V9990Layer& l, std::vector<uint8_t>& dots, int* highest)
{
    dots.assign((size_t)l.w * l.h, 0);
    for (int y = 0; y < l.h; y++) {
        uint8_t* row = &dots[(size_t)y * l.w];
        if (l.w == 1024) v9990FetchP2Layer(row, l.w, s->vram, 0, (UInt32)y);
        else v9990FetchP1Layer(row, l.w, s->vram, l.names, l.patterns, 0, (UInt32)y);
    }
    int hi = 0;
    for (int row = 0; row < l.h / 8; row++) {
        for (int col = 0; col < l.w / 8; col++) {
            int n = v9990NameAt(s, l, col, row, NULL);
            hi = n > hi ? n : hi;
        }
    }
    *highest = hi;
}

void v9990DecodeSheet(const V9990DebugState* s, const V9990Layer& l, int rows, std::vector<uint8_t>& dots)
{
    int w = l.perRow * 8, h = rows * 8, pitch = l.perRow * 4;
    dots.assign((size_t)w * h, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            UInt8 b = v9990Byte(s, (l.patterns + (UInt32)y * pitch + (UInt32)x / 2) & V9990_VRAM_MASK);
            dots[(size_t)y * w + x] = b >> 4;
            dots[(size_t)y * w + x + 1] = b & 15;
        }
    }
}

uint32_t v9990Rgba(UInt16 c, bool ysClear)
{
    uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    uint32_t a = ysClear && (c & 0x8000) ? 0 : 0xff;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

void v9990PaletteRgba(const V9990DebugState* s, uint32_t pal[64])
{
    for (int i = 0; i < 64; i++) pal[i] = v9990Rgba(v9990PaletteColor(s->palette, i), false);
}

void v9990LayerLut(const V9990DebugState* s, int layer, bool clear0, uint32_t lut[2][16])
{
    uint32_t pal[64];
    v9990PaletteRgba(s, pal);
    for (int k = 0; k < 2; k++) {
        lut[k][0] = clear0 ? 0 : pal[s->regs[15] & 63];
        for (int v = 1; v < 16; v++) lut[k][v] = pal[v9990LayerColour(s, layer, k * 2, v)];
    }
}

void v9990LayerRow(const uint8_t* src, uint32_t* dst, int n, const uint32_t lut[2][16])
{
    for (int x = 0; x < n; x++) dst[x] = lut[(x >> 1) & 1][src[x] & 15];
}
