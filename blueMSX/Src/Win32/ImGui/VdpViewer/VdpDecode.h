/*****************************************************************************
**
** Decoding of VDP snapshots into images and VRAM table addresses.
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
#ifndef VDP_DECODE_H
#define VDP_DECODE_H

#include <stdint.h>
#include <vector>
extern "C" {
#include "VDP.h"
#include "VdpDebug.h"
}

/* Pixels are 0xAABBGGRR, like ImGuiDynamicTexture wants them. */
typedef std::vector<uint32_t> VdpImage;

enum VdpColor0 {
    VDP_COLOR0_AUTO,         /* as the VDP shows it in the mode */
    VDP_COLOR0_PALETTE,      /* always palette entry 0 */
    VDP_COLOR0_BACKDROP,     /* always the R#7 backdrop colour */
    VDP_COLOR0_TRANSPARENT   /* alpha 0 */
};

/* Pattern modes use the screenMode numbers of the snapshot: 0 (TEXT1),
** 1, 2, 3, 4 and 13 (TEXT2). Bitmap modes: 5, 6, 7, 8, 10 (YAE), 12 (YJK). */
bool vdpIsBitmapMode(int mode);
bool vdpIsPatternMode(int mode);
bool vdpIsMsx1(const VdpDebugState* s);
const char* vdpModeName(int mode);
/* 1 in SCREEN 1-3, 2 in SCREEN 4-12, 0 where the mode has no sprites. */
int  vdpSpriteMode(int mode);
const char* vdpVersionName(int version);

/* The registers a picture is laid out from: the snapshot's at the end of
** the frame, or those of a band of display lines. */
struct VdpLayout {
    const UInt8* regs;   /* R#0-31 */
    int          mode;   /* screenMode numbering */
    int          blink;  /* TEXT2 blink phase, as the snapshot's blinkFlag */
    const UInt32* rgb;   /* the 16 colours (0x00RRGGBB) the lines were drawn with; NULL: the snapshot's */
    int          textSplit;  /* TEXT1/2: display lines down to the last mode change above, in 8s; 0 unless known */
};
VdpLayout vdpLayoutOf(const VdpDebugState* s);
/* The scroll line display line 0 reads: R#23 (none on MSX1), less TEXT1/2's text split. */
int vdpScreenScroll(const VdpDebugState* s, const VdpLayout& l);
/* R#18's H (vertical false) or V adjust as SET ADJUST gives it: how far right or down
** the picture moves, the negated signed nibble, as the emulated VDP applies it. */
int vdpSetAdjust(int r18, bool vertical);
/* Palette registers (G 8-10, R 4-6, B 0-2) as 0x00RRGGBB, as the snapshot's paletteRgb. */
void vdpPaletteRegRgb(const UInt16 reg[16], UInt32 out[16]);

struct VdpTables {
    int name;            /* -1 where the mode has no such table */
    int patternGen;
    int color;
    int spriteAttr;
    int spritePattern;
    int spriteColor;
};
/* Base addresses from the registers, low bits cleared; bitmap modes give the
** displayed page as a logical address. */
void vdpDecodeTables(const VdpDebugState* s, const VdpLayout& l, VdpTables* out);

/* The 16 colours as drawn in the layout's mode, colour 0 per the option. */
void vdpPaletteRgba(const VdpDebugState* s, const VdpLayout& l, int color0, uint32_t out[16]);
/* SCREEN 8's fixed colours. */
uint32_t vdpScreen8Rgba(int value);
uint32_t vdpRgbToRgba(uint32_t rgb);

struct VdpBitmapOptions {
    int mode;      /* 5, 6, 7, 8, 10 or 12 */
    int page;      /* -1: every page, one under the other, 256 lines each */
    int lines;     /* 192, 212 or 256 */
    int vscroll;   /* R#23; 0 when not shown as displayed */
    int color0;    /* VdpColor0 */
    bool displayed; /* page and line masks from R#2, as the VDP shows it */
    bool evenPair;  /* displayed: the even page an odd one alternates with (R#9 EO, R#13 blink) */
};

int  vdpBitmapWidth(int mode);
int  vdpBitmapPageCount(const VdpDebugState* s, int mode);
/* What the layout shows; false when it is not in a bitmap mode. */
bool vdpDisplayedBitmap(const VdpDebugState* s, const VdpLayout& l, VdpBitmapOptions* out);
/* o.mode is the mode the VRAM is read as; l gives the registers. */
void vdpDecodeBitmap(const VdpDebugState* s, const VdpLayout& l, const VdpBitmapOptions& o, VdpImage& out, int* w, int* h);

struct VdpPixelInfo {
    int      logical;    /* VRAM address as the CPU sees it */
    int      physical;   /* index into the snapshot's vram */
    int      value;      /* the colour code of this pixel */
    uint32_t rgba;
};
bool vdpBitmapPixel(const VdpDebugState* s, const VdpLayout& l, const VdpBitmapOptions& o, int x, int y, VdpPixelInfo* out);

/* Blocks of 256 characters on the sheet: 1, or 3 in SCREEN 2 and 4 (one per third of the
** screen), 4 when rows 24-31 are displayed and read a block the other three do not. */
int  vdpSheetBlocks(const VdpDebugState* s, const VdpLayout& l);
/* Every character of the pattern generator in rows of 32, a block of 8 rows each. */
void vdpDecodePatternSheet(const VdpDebugState* s, const VdpLayout& l, int color0, VdpImage& out, int* w, int* h);
/* The screen rebuilt from the name table, without sprites, from scroll line
** firstLine (-1: R#23, as displayed); 256 lines show rows 24-31 too. */
void vdpDecodeNameTable(const VdpDebugState* s, const VdpLayout& l, int lines, int firstLine, int color0, VdpImage& out, int* w, int* h);

struct VdpCharInfo {
    int ch;              /* character number; SCREEN 2/4 add 256 per block */
    int patternAddr;
    int colorAddr;       /* -1 when colours come from registers */
    int nameAddr;        /* -1 on the sheet */
    int col, row;
};
bool vdpSheetChar(const VdpDebugState* s, const VdpLayout& l, int x, int y, VdpCharInfo* out);
bool vdpNameTableChar(const VdpDebugState* s, const VdpLayout& l, int x, int y, int firstLine, VdpCharInfo* out);
int  vdpNameTableWidth(int mode);

/* Kinds of VdpRange; they pick the series colour on the VRAM map. */
enum VdpRangeKind {
    VDP_RANGE_NAME,            /* the name table, or the displayed page */
    VDP_RANGE_PATTERN,
    VDP_RANGE_COLOR,
    VDP_RANGE_SPRITE_ATTR,
    VDP_RANGE_SPRITE_PATTERN,
    VDP_RANGE_SPRITE_COLOR
};
struct VdpRange {
    int         addr, len;     /* logical (CPU) addresses */
    int         kind;          /* VdpRangeKind */
    const char* name;
    int         cap;           /* the most len gets in this mode (more lines shown, a fourth block) */
};
/* The tables the mode reads, as address ranges; returns how many. */
int vdpTableRanges(const VdpDebugState* s, const VdpLayout& l, VdpRange out[8]);
/* The index into the snapshot's vram for a CPU address in the mode. */
int vdpPhysicalAddress(const VdpDebugState* s, int mode, int logical);
/* Name table cells of lines from firstLine that show character ch (from VdpCharInfo). */
int vdpCharUseCount(const VdpDebugState* s, const VdpLayout& l, int lines, int firstLine, int ch);

#endif
