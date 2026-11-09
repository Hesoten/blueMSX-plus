/*****************************************************************************
**
** Per-line sprite selection and sprite images of a V9990 in P1 and P2.
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
#ifndef V9990_SPRITE_H
#define V9990_SPRITE_H

#include <stdint.h>
#include <vector>
extern "C" {
#include "V9990.h"
}

#define V9_SPRITES        125
#define V9_SPRITE_PLACES  16    /* a line's places; a PR0 sprite takes one too */
#define V9_SPRITE_LINES   212

/* A sprite as a whole: the first that holds of these. */
enum V9990SpriteState {
    V9_SPR_OFF_Y,      /* no row of it is on a displayed line */
    V9_SPR_PR0,        /* PR0 set: not drawn, but takes a place on its lines */
    V9_SPR_DROPPED,    /* some row is past the 16 places of its line */
    V9_SPR_OFF_X,      /* drawn past the right edge: takes places, shows nothing */
    V9_SPR_SHOWN
};

/* What a line does to one row of a sprite. */
/* PR0_PAST: a PR0 row past the places, which takes none. */
enum V9990SpriteRow { V9_ROW_OFF = -1, V9_ROW_SHOWN, V9_ROW_PR0, V9_ROW_DROPPED, V9_ROW_PR0_PAST };

struct V9990Sprite {
    int      y, x, pattern, sc;  /* attribute bytes; x is -15..1008, as drawn */
    bool     pr0, pr1;
    UInt32   attrAddr;           /* CPU address of its 4 bytes */
    int      state;              /* V9990SpriteState */
    int      line0, line1;       /* its first and last displayed line, -1 when none */
    int8_t   row[16];            /* V9990SpriteRow of pattern row r */
    int16_t  rowLine[16];        /* the line row r is on, -1 off screen */
    uint16_t frontMask[16];      /* dots of a drawn row under a front layer dot (PR1) */
    uint16_t spriteMask[16];     /* dots of a drawn row under a sprite before it */
    int      dots;               /* dots set on its drawn rows, on the screen */
    int      dropLine;           /* the first line it is dropped on, -1 when never */
    int      place0, place1;     /* PR0: the first and last line it takes a place on, -1 when none */
};

/* A line's sprites in range, in the order the chip checks them; the first `places` of them took a place. */
struct V9990SpriteLine {
    uint8_t count, places, pr0;  /* pr0: PR0 sprites among the places */
    uint8_t id[V9_SPRITES];
};

struct V9990SpriteFrame {
    bool     valid;               /* P1 or P2 */
    bool     p2, off;             /* off: R#8 SPD set, the chip draws no sprite */
    int      width;               /* 256, P2 512 */
    UInt32   attrBase, patBase;   /* CPU addresses of the tables */
    V9990Sprite     spr[V9_SPRITES];
    V9990SpriteLine line[V9_SPRITE_LINES];
    int      shown, pr0, dropped, offY, offX;     /* sprites in each state */
    int      overLines;           /* lines with sprites past their places */
    int      firstDropped, firstDropLine;          /* -1 when none */
    int      pr0Full0, pr0Full1;  /* the first run of lines at the limit with PR0 among the places, -1 when none */
    /* The sprites alone, a PR1 dot also where a layer hides it, w x 212: 1 + palette entry, 0 for none;
    ** dropMask: 1 where a dropped row would have drawn a dot. */
    std::vector<uint8_t> layer, dropMask;
};

/* Lays out the sprites from the snapshot's VRAM and registers, as the renderer does;
** attr, unless NULL, is the 512-byte attribute table to read instead. */
void   v9990EvalSprites(const V9990DebugState* s, V9990SpriteFrame* out, const UInt8* attr = NULL);
/* The CPU address of row r (0-15) of sprite pattern p: 8 bytes, 16 dots of 4 bits. */
UInt32 v9990SpritePatternAddr(const V9990SpriteFrame& f, int p, int r);
/* The 16 dot values of row r of sprite pattern p, as a mask of those set. */
uint16_t v9990SpritePatternRow(const V9990DebugState* s, const V9990SpriteFrame& f, int p, int r, uint8_t dots[16]);
/* The attribute table's sprites as 16x16 tiles, 16 a row, into atlas (256 x 128), and the 256
** patterns, 16 a row, into sheet (256 x 256): a pattern in its first user's colours, else grey. */
void   v9990RenderSpriteSheets(const V9990DebugState* s, const V9990SpriteFrame& f, const uint32_t pal[64],
                               std::vector<uint32_t>& atlas, std::vector<uint32_t>& sheet);
/* The sprites alone as drawn, f.width x 212; dots of dropped rows on alternate dots in
** stipple, unless it is 0. */
void   v9990RenderSpritePlane(const V9990SpriteFrame& f, const uint32_t pal[64], uint32_t stipple, std::vector<uint32_t>& out);

#endif
