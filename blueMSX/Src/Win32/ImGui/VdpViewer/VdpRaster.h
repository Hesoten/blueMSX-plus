/*****************************************************************************
**
** Line bands of a V9938 or V9958 frame, replayed from its raster log.
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
#ifndef VDP_RASTER_H
#define VDP_RASTER_H

#include <vector>
#include "VdpDecode.h"

/* A logged event placed on the frame. Lines are the log's frame lines;
** display line d is frame line firstLine + d. */
struct VdpRasterMark {
    int  line, cycle;
    int  kind;          /* VDP_RASTER_REG or VDP_RASTER_LINEINT */
    int  reg, value;
    int  aux;           /* REG: the old value; LINEINT: 1 if IE1 let it raise the IRQ */
    int  fromLine;      /* REG: the first line drawn with the new value */
    int  splitDot;      /* >= 0: SCREEN 5-12 R#2/R#23 change part way into `line`, from this
                        ** dot of 256 (twice it in 512-dot modes) */
    bool prev;          /* from the previous frame, shown below the beam while paused */
};

/* Display lines that share the registers the picture is laid out from. */
struct VdpBand {
    int  first, last;   /* display lines, inclusive */
    int  mode;          /* the screenMode R#0, R#1 and R#25 select, or -1 for a combination with none */
    int  page;          /* bitmap modes: the page R#2 selects; else -1 */
    bool evenSide;      /* this frame showed page & ~1 instead (R#9 EO or R#13 blink) */
    UInt8 regs[32];     /* R#0-31 as the band's first line is laid out */
    bool prev;          /* from the previous frame, below the beam while paused */
    bool unknown;       /* below the beam while paused, with no previous frame logged */
    int  blink;         /* the frame's TEXT2 blink phase, as the snapshot's blinkFlag */
    int  textSplit;     /* TEXT1/2: display lines down to the last mode change above, in 8s */
};

struct VdpRasterView {
    bool valid;         /* the machine's VDP has a log for this frame */
    bool complete, ended, foreign;
    bool prevUsed, prevComplete, prevForeign;
    bool endMatches;    /* replaying the log ends at the snapshot's registers */
    int  dropped;
    int  firstLine, lines, beamLine, beamCycle;   /* beamLine: frame line, or -1 once ended */
    int  leftBorder;    /* VDP cycles at the end of the frame, as the emulated VDP works it out */
    int  changes;       /* register changes in this frame */
    int  endSplit;      /* the textSplit the snapshot's registers are drawn with */
    std::vector<VdpRasterMark> marks;
    std::vector<VdpBand>       bands;
    std::vector<short>         bandOfLine;   /* per display line */
};

/* lines is 192 or 212. Without a log, valid is false and one band holds
** the snapshot's registers. */
void vdpRasterBuild(const VdpDebugState* s, int lines, VdpRasterView* out);
/* The band showing display line d, or -1. */
int  vdpRasterBandAt(const VdpRasterView& v, int d);
/* Same mode, table bases, sprite size, TP, scroll and page; a band is found
** again after a rebuild by this. */
bool vdpRasterSameKey(const VdpBand& a, const VdpBand& b);
/* The 256-line rows a band reads, R#2 line masks of the bitmap modes applied;
** runs of [from, to). vdpRasterBandCols gives the pages they are on. */
void vdpRasterBandRows(const VdpDebugState* s, const VdpBand& b, std::vector<int>& runs);
/* The pages and columns of those rows a bitmap band shows, in 256ths of a line: seg holds
** (page, from, to) twice at most, split where the R#26/R#27 window wraps, with SP2 between
** the even page and the odd one. Returns the count, 0 outside the bitmap modes. */
int  vdpRasterBandCols(const VdpBand& b, int seg[6]);
const char* vdpRegisterName(int reg);

/* The layout a band's lines are drawn with. */
VdpLayout vdpLayoutOf(const VdpBand& b);
/* What the band shows of a bitmap page, as the VDP lays its lines out; false
** when the band is not in a bitmap mode. */
bool vdpBandBitmap(const VdpDebugState* s, const VdpBand& b, VdpBitmapOptions* out);

/* 1 bitmap, 0 pattern modes, -1 none: what a pin or a choice keeps across frames. */
int  vdpModeFamily(int mode);
/* A band or group found again in later frames: by its mode family and the
** lines it covers, not by scroll, page or adjust registers. */
struct VdpBandKey { int family, first, last; };
VdpBandKey vdpBandKeyOf(const VdpBand& b);
/* The band of the key's family that shares the most lines with it, or -1. */
int  vdpRasterFindBand(const VdpRasterView& v, const VdpBandKey& k);

/* Bands drawn in one mode from the same tables (bitmap bands: the same
** mode), for headings and lists; the pictures are decoded per band. */
struct VdpBandGroup {
    int      mode;
    UInt8    regs[32];     /* of its first band */
    int      blink;
    int      textSplit;    /* of its first band */
    char     lines[64];    /* "0-30, 113-211" */
    unsigned shownPages;   /* bit p: a band of the group shows page p, as vdpRasterBandCols gives them */
    int      runFirst[8], runLast[8], runs, lineCount;
};
/* Groups in the order their first lines come; groups of one or two lines,
** such as the line a mode change takes effect on, are left out when larger
** ones exist. Empty without a log. */
void vdpRasterGroups(const VdpRasterView& v, std::vector<VdpBandGroup>& out);
bool vdpGroupHasBand(const VdpBandGroup& g, const VdpBand& b);
VdpLayout vdpLayoutOf(const VdpBandGroup& g);
VdpBandKey vdpGroupKeyOf(const VdpBandGroup& g);
/* The group of the key's family that shares the most lines with it, or -1. */
int  vdpFindGroup(const std::vector<VdpBandGroup>& groups, const VdpBandKey& k);

#endif
