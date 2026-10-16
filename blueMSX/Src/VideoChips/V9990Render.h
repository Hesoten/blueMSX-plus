/*****************************************************************************
**
** Yamaha V9990 line renderer.
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
#ifndef V9990_RENDER_H
#define V9990_RENDER_H

#include "MsxTypes.h"

/* Frame buffer colour of palette entry index (6 bits), with bit 15 set if
** the entry has YS. Bit 15 marks a pixel that superimpose makes transparent. */
UInt16 v9990PaletteColor(const UInt8* palette, int index);

/* Draws count pixels of a bitmap image line, starting at image coordinates
** (x, y), in the colour type selected by R#6 and R#13; hiRes selects the
** dual palette of B4-B7. */
void v9990RenderBitmapLine(UInt16* dst, int count, const UInt8* vram, const UInt8* palette,
                           UInt8 r6, UInt8 r13, UInt32 x, UInt32 y, int hiRes);

/* A P1 or P2 display line, with the registers as they stand at its commit. */
typedef struct {
    int    p2;
    int    displayY;    /* display line, for sprites and the R#27 boundary */
    UInt32 ax, ay;      /* image dot and line of layer A at display dot 0 */
    UInt32 bx, by;      /* the same for layer B (P1 only) */
    UInt8  r8, r13, r15, r25, r27;
} V9990PatternLine;

/* Draws count dots of a pattern-mode display line, from display dot dot. */
void v9990RenderPatternLine(UInt16* dst, int count, const UInt8* vram, const UInt8* palette,
                            const V9990PatternLine* line, int dot);

#endif
