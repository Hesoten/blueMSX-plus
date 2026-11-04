/*****************************************************************************
**
** Per-line sprite selection and sprite images of a V9968 in sprite mode 3.
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
#ifndef VDP_SPRITE3_H
#define VDP_SPRITE3_H

#include "VdpDecode.h"

#define SP3_PLANES 64
#define SP3_LIMIT  16
#define SP3_LINES  256

/* What became of a plane in the frame, first match wins. */
enum Sp3State {
    SP3_ST_AFTER_STOP,   /* the terminator itself or a plane the scan reaches after it */
    SP3_ST_OFF_SCREEN,   /* on no displayed line, or no dot falls in X 0-255 */
    SP3_ST_NO_WIDTH,     /* MGX 0: takes a place on its lines but draws nothing */
    SP3_ST_DROPPED,      /* past the 16 a line on at least one line */
    SP3_ST_DRAWN
};

/* One plane's eight attribute bytes, read as the renderer reads them. */
struct Sp3Plane {
    int  attr;                 /* physical address of byte 0 */
    int  alias;                /* the plane whose bytes it reads: itself unless R#5 bits 0-1 fold it */
    int  step;                 /* where the scan of the frame shown reaches it, 0-63 */
    int  y, x;                 /* 10 bit */
    int  sizeCode, rows;       /* rows = 16 << sizeCode pattern rows, 16 dots wide */
    int  mgx, mgy, height;     /* height: MGY, 256 for 0 */
    int  weight;               /* 4 - TP: 4 opaque to 1 (25%) */
    bool rvx, rvy;
    int  ps, page, pattern;
    bool xVisible;             /* some dot lands on X 0-255 */
    int  drawnLines, droppedLines;   /* displayed lines in range within or past the limit */
    int  firstLine, lastLine;        /* displayed lines in range before the terminator, or -1 */
    int  firstDropped, lastDropped;  /* displayed lines it is past the 16 on, or -1 */
    int  state;                /* Sp3State */
};

struct Sp3Frame {
    bool on;                   /* a V9968 with SP3 in a sprite mode */
    const char* off;           /* why no sprite is drawn (R#8 SPD, blanked), or NULL */
    int  lines;                /* displayed lines, 192 or 212 */
    int  vramMask;
    int  attrBase;             /* the 512 bytes of the attribute table */
    int  aliasMask;            /* R#5 bits 0-1: planes 16-63 fold onto 0-15 where clear */
    int  patBase;              /* R#6 << 11 */
    bool shuffle;              /* R#25 SPS: the scan strides and no terminator ends it */
    bool svns;                 /* R#20 SVNS: R#23 does not move the sprites */
    int  vscroll;
    int  stopStep;             /* the step of the Y = 216 terminator, or 64 */
    int  overLines, firstOverLine, fifthPlane;   /* fifthPlane as S#0 reports it, or -1 */
    Sp3Plane p[SP3_PLANES];    /* by plane number */
    uint8_t order[SP3_PLANES]; /* plane at each scan step */
    uint8_t count[SP3_LINES];  /* planes in range on each displayed line, before the terminator */
    uint8_t id[SP3_LINES][SP3_PLANES];   /* those planes, in scan order */
    /* By the row a line samples before RVY: bit 0 drawn on some line, bit 1 dropped on some. */
    uint8_t rowSt[SP3_PLANES][128];
};

/* How a displayed line's own registers show the frame end's planes. */
enum Sp3LineState {
    SP3_LINE_OFF,        /* no SP3 sprite there: another mode, SP3 clear, R#8 SPD or blanked */
    SP3_LINE_DIFFERS,    /* its sprite tables, R#20 SVNS, R#23 or R#25 SPS are not the frame end's */
    SP3_LINE_SAME
};

/* SP3 is in use: a V9968, R#20 SP3, in a mode with sprites. */
bool sp3Active(const VdpDebugState* s);
/* A line laid out in mode by lineRegs, against the frame end's frameRegs. */
int  sp3LineState(const UInt8* frameRegs, int mode, const UInt8* lineRegs);
/* Evaluates every displayed line from the snapshot's registers and VRAM; scanAll scans
** them even when the frame end has the sprites off (off still says why). */
void sp3Eval(const VdpDebugState* s, Sp3Frame* out, bool scanAll = false);
/* The scan line the renderer looks at for displayed line d. */
int  sp3ScanLine(const Sp3Frame& f, int d);
/* The pattern row plane n shows on displayed line d, or -1 when out of range. */
int  sp3SourceRow(const Sp3Frame& f, int n, int d);
/* The colour code (0-15) at a pattern row and column of a page and pattern. */
int  sp3PatternDot(const VdpDebugState* s, const Sp3Frame& f, int page, int pattern, int row, int col);
/* The plane whose dot shows at X x of displayed line d, or -1. */
int  sp3PlaneAt(const VdpDebugState* s, const Sp3Frame& f, int x, int d);
/* The displayed lines alone, 256 dots wide: RGBA with alpha by TP over clear; with
** dropRgba not 0, dots of the planes past limit no drawn one covers are stippled with it.
** whenOff draws them even if the frame ends with sprites off (for lines that had them). */
void sp3RenderPlacement(const VdpDebugState* s, const Sp3Frame& f, uint32_t dropRgba, uint32_t* out,
                        int limit = SP3_LIMIT, bool whenOff = false);
/* The 32 KB at patBase + page * 32 KB as 256 x 256 dots of 4 bits, in palette set ps, 0 clear. */
void sp3RenderPage(const VdpDebugState* s, const Sp3Frame& f, int page, int ps, uint32_t* out);
/* Plane n's pattern as 16 x rows dots at out, pitch dots a row: in its palette set,
** RVX and RVY applied, MGX and MGY not, opaque but for colour 0; true when a dot is set. */
bool sp3RenderTile(const VdpDebugState* s, const Sp3Frame& f, int n, uint32_t* out, int pitch);
/* The VRAM address of a pattern row's first byte, before the VRAM mask. */
int  sp3PatternAddr(const Sp3Frame& f, int page, int pattern, int row);
/* The run of X 0-255 a plane's MGX dots land on, as [x0, x1); returns 0 or 1. */
int  sp3XRuns(const Sp3Plane& p, int* x0, int* x1);

#endif
