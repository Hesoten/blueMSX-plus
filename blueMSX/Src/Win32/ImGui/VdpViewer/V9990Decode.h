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
#ifndef V9990_DECODE_H
#define V9990_DECODE_H

#include <stdint.h>
#include <vector>
extern "C" {
#include "V9990.h"
}

/* Layer 0 is P1's A or P2's only layer, 1 is P1's B. Addresses are CPU (logical) ones. */
struct V9990Layer {
    int    w, h;          /* the image space in dots: 512 x 512, P2 1024 x 512 */
    UInt32 names;         /* the name table: 2 bytes a cell, w / 8 cells a row */
    UInt32 patterns;      /* the pattern generator */
    int    perRow;        /* patterns a row of the generator: 32, P2 64 */
    int    rows;          /* rows of patterns the generator holds */
    int    sx, sy;        /* the scroll: the image dot at the screen's top left */
    int    rollMask;      /* image lines roll within sy & ~rollMask .. + rollMask */
    int    screenW;       /* 256, P2 512 */
};

/* False unless the chip is in P1 or P2 and the layer is one of the mode's. */
bool   v9990LayerOf(const V9990DebugState* s, int layer, V9990Layer* out);
/* The pattern number at cell col, row of the name table; *nameAddr its first byte. */
int    v9990NameAt(const V9990DebugState* s, const V9990Layer& l, int col, int row, UInt32* nameAddr);
/* The CPU address of row r (0-7) of pattern n: 4 bytes, 8 dots of 4 bits. */
UInt32 v9990PatternRowAddr(const V9990Layer& l, int n, int r);
/* The physical byte at CPU address a in the chip's mode. */
UInt8  v9990Byte(const V9990DebugState* s, UInt32 a);
/* The palette entry of dot value v (1-15) at image dot x: P2 takes the second offset at dots 2-3 and 6-7. */
int    v9990LayerColour(const V9990DebugState* s, int layer, int x, int v);
/* The layer's image space, one 4-bit dot value a byte; *highest the largest pattern number its name table uses. */
void   v9990DecodeLayer(const V9990DebugState* s, const V9990Layer& l, std::vector<uint8_t>& dots, int* highest);
/* The generator's first rows rows of patterns as a sheet perRow * 8 dots wide, one dot value a byte. */
void   v9990DecodeSheet(const V9990DebugState* s, const V9990Layer& l, int rows, std::vector<uint8_t>& dots);

/* An xRGB 1:5:5:5 colour as 0xAABBGGRR; ysClear: clear where YS is set. */
uint32_t v9990Rgba(UInt16 c, bool ysClear);
/* The 64 palette entries as colours. */
void   v9990PaletteRgba(const V9990DebugState* s, uint32_t pal[64]);
/* A layer's dot values in colours: lut[0] for dots 0-1 and 4-5, lut[1] for dots 2-3 and
** 6-7, which P2 colours apart; value 0 is the backdrop, or clear. */
void   v9990LayerLut(const V9990DebugState* s, int layer, bool clear0, uint32_t lut[2][16]);
/* n dot values from src through lut into dst. */
void   v9990LayerRow(const uint8_t* src, uint32_t* dst, int n, const uint32_t lut[2][16]);

#endif
