/*****************************************************************************
**
** Per-line sprite selection and sprite images of TMS99xx, V9938 and V9958.
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
#ifndef VDP_SPRITE_H
#define VDP_SPRITE_H

#include "VdpDecode.h"

/* What happens to a sprite on a line it is in range of. */
enum VdpSpriteState {
    VDP_SPRITE_DRAWN,
    VDP_SPRITE_DROPPED,    /* past the 4 (mode 1) or 8 (mode 2) a line allows */
    VDP_SPRITE_CC_HIDDEN   /* CC set and no sprite without CC before it on the line */
};

struct VdpSprite {
    int     y, x, pattern;   /* attribute bytes; 16x16 sprites clear the pattern's low 2 bits */
    int     attrAddr;        /* logical (CPU) addresses */
    int     colorAddr;       /* row 0's colour in sprite mode 2; the 4th attribute byte in mode 1 */
    int     patternAddr;
    uint8_t color[16];       /* per pattern row: EC 0x80, CC 0x40, IC 0x20, colour in bits 0-3 */
    int     drawnLines, droppedLines, hiddenLines;   /* over the displayed lines */
    int     firstDropped, lastDropped;               /* displayed lines, or -1 */
    bool    afterStop;       /* at or past the terminator, so the VDP never looks at it */
    bool    collides;        /* covers a dot another collidable sprite covers */
};

struct VdpSpriteLine {
    int     count;           /* sprites in range, those past the limit too */
    uint8_t id[32];          /* in the order the VDP checks them */
};

struct VdpSpriteFrame {
    int  mode;               /* vdpSpriteMode: 0 none, 1 or 2 */
    int  screenMode;         /* of the layout evaluated */
    UInt8 regs[32];          /* the layout's registers, which the other calls read */
    int  size, mag, limit, stopY;
    int  lines, vscroll;     /* displayed line d is scroll line (d + vscroll) & 255 */
    int  stopIndex;          /* the sprite holding the terminator, or 32 */
    const char* off;         /* why the VDP draws none (R#8 SPD, blanked), or NULL */
    int  overLines;          /* displayed lines with more sprites than the limit; 0 when off */
    int  firstDropLine;      /* the first of them, or -1 */
    int  firstDropped;       /* the sprite S#0 names on that line, or -1 */
    int  collX, collLine;    /* the first collision: dot 0-255 and displayed line, or -1 */
    VdpSprite     spr[32];
    VdpSpriteLine line[256]; /* by scroll line, the 256 lines R#23 scrolls through */
};

/* Evaluates every scroll line from the snapshot's VRAM and the layout's
** registers, as the VDP would; lines is 192 or 212. */
void vdpEvalSprites(const VdpDebugState* s, const VdpLayout& l, int lines, VdpSpriteFrame* out);
/* The same from a table recorded as a line was drawn: its attributes and colours
** as the VDP read them, the patterns from the snapshot. */
void vdpEvalSpritesFrom(const VdpDebugState* s, const VdpSprTable& t, VdpSpriteFrame* out);
/* The key of the table a recorded use lays out, as a frame of it has: its attributes and pattern
** base, sprite mode and size * 2 + mag; false when it shows no sprites (mode 0, BL off, SPD). */
bool vdpSpriteUseKey(const VdpDebugState* s, const VdpSprUse& u, int* attr, int* patBase, int* mode, int* sizeKey);
/* The registers and mode the frame was evaluated with. */
VdpLayout vdpLayoutOf(const VdpSpriteFrame& f);
/* The pattern row sprite n shows on a scroll line, or -1 when out of range. */
int  vdpSpriteRow(const VdpSpriteFrame& f, int n, int scrollLine);
/* The state of the k-th sprite in range of a scroll line (VdpSpriteState). */
int  vdpSpriteState(const VdpSpriteFrame& f, int scrollLine, int k);
/* The same with limit sprites a line, e.g. 32 when the emulator draws past the VDP's. */
int  vdpSpriteStateAt(const VdpSpriteFrame& f, int scrollLine, int k, int limit);
/* The first collision on displayed line d: true with its dot in *x and the sprites
** taking part as bits of *colliders; the frame is not changed. */
int  vdpSpriteLineCollision(const VdpDebugState* s, const VdpSpriteFrame& f, int d, int* x, uint32_t* colliders);
/* The displayed line a scroll line is shown on, or -1. */
int  vdpSpriteDisplayLine(const VdpSpriteFrame& f, int scrollLine);
/* The 16 dots of a pattern row from the left, as bits 15 to 0. */
int  vdpSpriteRowBits(const VdpDebugState* s, const VdpSpriteFrame& f, int n, int row);
/* The same for a pattern number of f's table, whether or not a sprite uses it. */
int  vdpSpritePatternRowBits(const VdpDebugState* s, const VdpSpriteFrame& f, int pattern, int row);
/* The dot sprite n's pattern row starts at, EC shifting it 32 left; *w, if given, its width in dots. */
int  vdpSpriteSpan(const VdpSpriteFrame& f, int n, int row, int* w);
/* True when colour code is transparent: 0, unless R#8 TP on a V99x8. */
bool vdpSpriteClear(const VdpDebugState* s, const VdpSpriteFrame& f, int code);
/* The sprite drawn on top at a dot of a scroll line, those past the limit too; -1 for none. */
int  vdpSpriteAt(const VdpSpriteFrame& f, int scrollLine, int dot);
/* A sprite colour code as drawn with the 16 colours rgb (0x00RRGGBB) of its line, NULL:
** the snapshot's. SCREEN 8 has fixed colours, SCREEN 6 the even dot's. */
uint32_t vdpSpriteRgbaFrom(const VdpDebugState* s, const UInt32* rgb, const VdpSpriteFrame& f, int code);
/* Sprite n alone, unmagnified, f.size dots square at dst (pitch dots a row): each
** row in its colour from rowRgb[row] (NULL entries or rowRgb NULL: the snapshot's),
** colour 0 clear unless R#8 TP. */
void vdpRenderSpriteTile(const VdpDebugState* s, const VdpSpriteFrame& f, int n, const UInt32* const rowRgb[16], uint32_t* dst, int pitch);
/* Displayed lines first to last into rows width dots long (256, or 512 with two dots a sprite dot),
** row d at out + d * width; lineRgb[d], if given and not NULL, holds line d's colours. dropped, if
** given, gets 256 bytes a line: 1 where a sprite past the limit has a dot no drawn one covers. */
void vdpRenderSpriteLines(const VdpDebugState* s, const VdpSpriteFrame& f, uint32_t* out, int width, int first, int last,
                          const UInt32* const* lineRgb = NULL, uint8_t* dropped = NULL);

/* f's sprite pattern table as 256 x 64 (8x8 patterns in rows of 32, 16x16 in rows of 16): pattern p
** drawn as sprite owner[p] draws it (rows coloured by ownerRgb[owner * 16 + row], as
** vdpRenderSpriteTile), or in grey with no owner. */
void vdpDecodeSpriteSheetUsers(const VdpDebugState* s, const VdpSpriteFrame& f, const int owner[256], const UInt32* const* ownerRgb,
                               uint32_t grey, VdpImage& out, int* w, int* h);
/* The pattern number at a sheet pixel, or -1. */
int  vdpSpriteSheetPattern(int size, int x, int y);
void vdpSpriteSheetCell(int size, int pattern, int* x, int* y);
int  vdpSpritePatternAddr(const VdpDebugState* s, const VdpSpriteFrame& f, int pattern);
/* The logical address of sprite n's colour for a pattern row in sprite mode 2. */
int  vdpSpriteColorAddr(const VdpDebugState* s, const VdpSpriteFrame& f, int n, int row);

#endif
