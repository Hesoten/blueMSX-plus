/*****************************************************************************
**
** Snapshot of a V99x8/TMS VDP for the tool windows.
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
#ifndef VDP_DEBUG_H
#define VDP_DEBUG_H

#include "MsxTypes.h"

#define VDP_DEBUG_VRAM_SIZE (256 * 1024)
#define VDP_RASTER_MAX      4096

/* Times are VDP cycles: line = (t - frameStart) / 1368, cycle = the rest. A line interrupt
** is stamped with its own time but noted a little later, so a write a few cycles after it
** may be listed before it. A write timed just past the frame's end is put on its last cycle. */
enum { VDP_RASTER_REG = 0, VDP_RASTER_LINEINT = 1 };

/* REG: R#reg went from aux to value (R#14-17, R#32 up and rewrites of the
** same value are not logged). LINEINT: the line interrupt time (right edge
** of the line) with reg 19, value R#19, aux 1 if IE1 let it raise the IRQ. */
typedef struct {
    UInt16 line;
    UInt16 cycle;
    UInt8  kind;
    UInt8  reg;
    UInt8  value;
    UInt8  aux;
} VdpRasterEvent;

typedef struct {
    int    valid;           /* 0: not armed, no VDP, or no such frame known */
    int    complete;        /* 0: armed, reset or a state loaded in it; writes may be missing */
    int    ended;           /* 1: the frame is over, or its active display is (VDP_SHOWN); 0: beamLine/beamCycle is where it is */
    int    foreign;         /* registers were written from another thread, e.g. the debugger */
    UInt32 frameStart;
    int    lastLine;        /* lines in the frame, 262 or 313 */
    int    firstLine;       /* line of display line 0 */
    int    displayOffset;   /* line of framebuffer row 0 */
    int    field;           /* S#2 bit 1 during the frame */
    int    blinkFlag;       /* at the frame's first event; R#13 and line blink change it within */
    int    beamLine;        /* lastLine and 0 once ended */
    int    beamCycle;
    UInt8  startRegs[64];   /* R#0-31 as the frame began; ev turns it into the end, bar R#14-17 */
    int    count;           /* entries of ev that are filled, in the order noted */
    int    dropped;         /* events lost after ev filled up */
    VdpRasterEvent ev[VDP_RASTER_MAX];
} VdpRasterFrame;

typedef struct {
    int    valid;           /* 0: no VDP at this index */
    UInt32 id;              /* debug handle + 1; differs for every VDP created */
    char   name[32];
    int    version;         /* VdpVersion */
    int    vramSize;        /* bytes the machine has */
    int    vramMask;        /* what the renderer masks table addresses with */
    int    vram128;         /* where the renderer reads odd bytes in SCREEN 7-12 */
    int    screenMode;      /* 0-8, 10 (YAE), 12 (YJK), 13 (TEXT2) */
    int    isPal;
    int    screenOn;
    int    blinkFlag;
    int    regCount;        /* R#0 onwards */
    int    statusCount;     /* S#0 onwards */
    int    cmdRegCount;     /* R#32 onwards */
    UInt8  regs[64];
    UInt8  status[16];      /* S#7 is CL; S#8/S#9 the border X */
    UInt8  cmdRegs[16];
    UInt16 paletteReg[16];  /* G in bits 8-10, R in 4-6, B in 0-2 */
    UInt32 paletteRgb[16];  /* 0x00RRGGBB; the fixed colours on a TMS VDP */
    /* [0]: the frame that just ended at a frame tap, or the one in progress
    ** while paused; [1]: the one before it, if known. Entries of ev past
    ** count are stale, so compare snapshots outside raster. */
    VdpRasterFrame raster[2];
    UInt8  vram[VDP_DEBUG_VRAM_SIZE];  /* physical order; zero past what the VDP reaches */
} VdpDebugState;

#define VDP_SHOT_ROWS  240
#define VDP_SHOT_WIDTH 544  /* (8 + 256 + 8) << 1; wider rows would be cut */
#define VDP_SPR_TABS   64   /* sprite tables a shot holds */

/* The sprite tables and registers a row's sprites were laid out from, as the VDP
** read them: base is the logical address of bytes[0], the 1 KB block holding the
** attributes (and in sprite mode 2 the colours); bytes it does not read are 0. */
typedef struct {
    UInt8 regs[32];
    int   screenMode;
    int   base;
    UInt8 bytes[0x400];
} VdpSprTable;

#define VDP_SPR_USES 16  /* sprite table uses a shot holds */

/* A sprite table as its key: the register bits that place, size or hide it
** (r1 & 0x43, r8 & 0x02, the others whole), and the seq of the frame that last used it. */
typedef struct {
    UInt8  screenMode, r1, r5, r6, r8, r11;
    UInt32 seq;
} VdpSprUse;

/* The picture the VDP drew, in frame buffer rows: row y is scan line
** y + displayOffset and holds maxWidth << shift[y] ARGB1555 samples (bit 15:
** key colour); past that, stale. Mixed externally or with none, as mixed. */
typedef struct {
    int    valid;           /* 0: no VDP, or no frame drawn yet */
    int    pixels;          /* 0: copied without the samples; pix is stale, shift holds */
    int    stale;           /* the VDP did not draw the newest frame: not the source drawn, minimised, or the frame count changed */
    int    ended;           /* 1: all rows are the frame that just ended (at the VDP_SHOWN tap, up to drawnRow); 0: paused within a frame */
    int    drawnRow;        /* paused or at VDP_SHOWN: rows before it are the frame in progress, the rest the frame before */
    int    drawnSamples;    /* paused or at VDP_SHOWN: samples of row drawnRow from the frame in progress */
    int    restMissing;     /* the frame before is unknown: rows from drawnRow on hold 0x8000 */
    UInt32 frameStart;      /* the frame of the rows (paused: of the rows before drawnRow), as in raster[0] */
    UInt32 seq;             /* frame buffer flip that completed the frame before drawnRow (or of all rows) */
    int    rows;
    int    maxWidth;        /* 272: 8 + 256 + 8 */
    int    interlace;       /* InterlaceMode of the frame: 0 none, 1 odd, 2 even field */
    int    interlaceRaster; /* R#9 IL */
    int    displayOffset;   /* scan line of row 0: 27 PAL, 0 NTSC */
    int    firstLine;       /* scan line of display line 0 */
    int    hAdjust;         /* the left border is (8 + hAdjust (+ hAdjustSc0 in text modes)) << shift samples, as at the frame's end */
    int    hAdjustSc0;
    UInt8  shift[VDP_SHOT_ROWS];
    UInt16 pix[VDP_SHOT_ROWS][VDP_SHOT_WIDTH];
    /* While the sprite split is armed: each row drawn again without sprites
    ** as its line ended (sprBg), the samples its sprites could cover
    ** (sprMask, bit x & 7 of byte x >> 3) and how far the row holds. */
    int    sprSplit;        /* 1: the fields below are filled */
    UInt8  sprRow[VDP_SHOT_ROWS];   /* VDP_SPR_ROW_* */
    UInt8  sprMask[VDP_SHOT_ROWS][VDP_SHOT_WIDTH / 8];
    UInt16 sprBg[VDP_SHOT_ROWS][VDP_SHOT_WIDTH];
    /* While the sprite tables are armed: the table each row's sprites came
    ** from (sprTabRow, 0xff none) and how sure that is (sprTabState). */
    int    sprTabCount;
    int    sprTabLost;  /* rows whose table did not fit in the frame's tables */
    UInt8  sprTabRow[VDP_SHOT_ROWS];
    UInt8  sprTabState[VDP_SHOT_ROWS];  /* VDP_SPR_ROW_*: APPROX when they changed while the line was drawn */
    VdpSprTable sprTab[VDP_SPR_TABS];
    /* While the sprite tables are armed: the tables displayed lines used since
    ** sprUseSince (a seq), each with the seq of the last frame that used it. */
    int    sprUseCount;     /* 0: not recorded */
    UInt32 sprUseSince;
    VdpSprUse sprUse[VDP_SPR_USES];
    /* While the sprite tables are armed: each row's palette as its line ended, as
    ** paletteReg; linePalState VDP_SPR_ROW_*, APPROX when it changed while drawn. */
    int    linePalValid;
    UInt8  linePalState[VDP_SHOT_ROWS];
    UInt16 linePal[VDP_SHOT_ROWS][16];
} VdpFrameShot;

/* NONE: not recorded for this picture. EXACT: pix differs from sprBg
** only by sprites. APPROX: the line changed while drawn; sprMask bounds it. */
enum { VDP_SPR_ROW_NONE = 0, VDP_SPR_ROW_EXACT = 1, VDP_SPR_ROW_APPROX = 2 };

#ifdef __cplusplus
extern "C" {
#endif

int vdpDebugCount(void);

/* The name vdpDebugCapture gives VDP number index, or NULL when there is none. */
const char* vdpDebugName(int index);

/* Copies VDP number index without syncing it or touching any latch, so call
** it only on the emulator thread at a frame boundary or while it waits.
** Returns 0 and clears valid when there is no such VDP. */
int vdpDebugCapture(int index, VdpDebugState* out);

/* Copies the picture of VDP number index, on the same terms; with pixels 0
** all but the samples. Returns 0 and clears valid when there is none. */
int vdpDebugCaptureFrame(int index, VdpFrameShot* out, int pixels);

/* Starts (on) or stops logging raster events of the machine's VDP; calls
** nest. UI thread only. Each new start forgets what was logged before. */
void vdpRasterArm(int on);

/* Nonzero while armed: the generation of the current start. */
extern volatile int vdpRasterArmed;

/* Starts (on) or stops recording the sprite split of the machine's VDP;
** calls nest. UI thread only. Costs a second render of each line. */
void vdpSprSplitArm(int on);

/* Starts (on) or stops recording, per line of the machine's VDP, the sprite
** tables its sprites were laid out from and its palette; calls nest. UI
** thread only. */
void vdpSprTableArm(int on);

/* Nonzero while either is armed: the split's generation in the low 16 bits,
** the tables' in the high 16. */
extern volatile int vdpSprSplitArmed;

/* Called by the VDP after it stores a register, and when a line interrupt
** time passes. Do not call them from elsewhere. */
struct VDP;
void vdpRasterNoteReg(struct VDP* vdp, int reg, UInt8 old, UInt8 value);
void vdpRasterNoteLineInt(struct VDP* vdp);

#ifdef __cplusplus
}
#endif

#endif
