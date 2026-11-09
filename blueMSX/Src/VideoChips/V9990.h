/*****************************************************************************
**
** Yamaha V9990 video chip.
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
#ifndef V9990_H
#define V9990_H

#include "MsxTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V9990_VRAM_SIZE 0x80000
#define V9990_VRAM_MASK (V9990_VRAM_SIZE - 1)

/* Physical offset of a bitmap-mode byte address: the banks interleave byte by byte. */
#define V9990_MAP_BX(a) ((((a) & 1) << 18) | (((a) & V9990_VRAM_MASK) >> 1))

typedef enum {
    V9990_MODE_P1,
    V9990_MODE_P2,
    V9990_MODE_B0,
    V9990_MODE_B1,
    V9990_MODE_B2,
    V9990_MODE_B3,
    V9990_MODE_B4,
    V9990_MODE_B5,
    V9990_MODE_B6,
    V9990_MODE_B7,
    V9990_MODE_STANDBY,
    V9990_MODE_NONE
} V9990Mode;

typedef struct V9990 V9990;

/* Creates the chip in its power-on state with its timers running. */
V9990* v9990Create(void);
void   v9990Destroy(V9990* v9990);
/* While enabled, the chip claims the screen whenever its frame-latched DISP
** is set, and a DISP edge switches the shown source if autodetect is on. */
void   v9990SetShowOnDisplay(V9990* v9990, int enable);
/* The video manager handle of the chip's picture. */
int    v9990GetVideoHandle(V9990* v9990);
/* While source handle (none if -1) is shown, the chip presents the screen
** and gives it its refresh rate and aspect; superimpose marks the YS
** colours and the blanking with frame buffer bit 15. */
void   v9990SetPresenter(V9990* v9990, int handle, int superimpose);
/* The display mode of the V9990 picture on the screen, or V9990_MODE_NONE
** if it is not shown; superimposed is set if the VDP picture is under it. */
V9990Mode v9990GetShownMode(int* superimposed);
/* Nonzero if the machine has a V9990, shown or not. */
int    v9990IsPresent(void);
/* Nonzero if the frame that the last frame flip completed was drawn with
** the keys of a superimposing presenter. */
int    v9990IsFrameKeyed(V9990* v9990);
void   v9990Reset(V9990* v9990);
void   v9990SaveState(V9990* v9990);
void   v9990LoadState(V9990* v9990);

/* port is the low nibble of the I/O address (P#0-P#F). */
UInt8  v9990ReadPort(V9990* v9990, UInt16 port);
void   v9990WritePort(V9990* v9990, UInt16 port, UInt8 value);

/* Turns a logical VRAM address, as the CPU or a command sees it in the
** given display mode, into an index into the physical 512 KB array. */
UInt32 v9990MapAddress(V9990Mode mode, UInt32 address);

typedef struct {
    int    valid;           /* 0: no V9990 at this index */
    UInt32 id;              /* debug handle + 1; differs for every chip created */
    char   name[32];
    int    mode;            /* V9990Mode the lines are drawn in now */
    int    frameMode;       /* V9990Mode latched at frame start */
    int    modeR6;          /* R#6 as the lines are drawn now */
    int    mcs;             /* P#7 MCS */
    int    isPal;           /* latched at frame start, like the four below */
    int    interlace;
    int    secondField;
    int    displayOn;
    UInt8  scrollAYHi;      /* R#18, which latches at frame start */
    UInt8  scrollBYHi;      /* R#22, which latches at frame start too */
    UInt8  regs[64];        /* R#53/R#54 hold the command border X */
    UInt8  liveR13;         /* R#13 now; regs[13] and palette are as the last displayed line drew them, when known */
    UInt8  ports[16];       /* what P#0-P#F reads would return */
    UInt8  palette[256];    /* 64 x R (bit 7 YS), G, B, unused */
    UInt8  vram[V9990_VRAM_SIZE];  /* physical order */
} V9990DebugState;

int v9990DebugCount(void);

/* Copies the chip without syncing it or touching any latch, so call it only
** on the emulator thread; the EMU_TAP_V9990_SHOWN tap gets the frame as shown.
** Returns 0 and clears valid when there is none. */
int v9990DebugCapture(int index, V9990DebugState* out);

#define V9990_SHOT_ROWS  480
#define V9990_SHOT_WIDTH 1152

/* The picture the V9990 drew into its own frame buffer, before compositing.
** Row y is scan line windowTopLine + y and holds width[y] ARGB1555 samples
** of 8 >> shift[y] UC (bit 15: key while superimposed); the rest is stale. */
typedef struct {
    int    valid;            /* 0: no V9990, or no frame copied yet */
    int    stale;            /* no copy yet (paused: rows from the last frame buffer frame), or copying had stopped */
    int    ended;            /* 1: all rows are of one frame, the newest completed or the one shown; 0: paused */
    int    drawnRow;         /* rows before it are the frame in progress (paused) or shown */
    int    restMissing;      /* paused, no copy and no frame buffer frame: rows from drawnRow on hold 0x8000 */
    UInt32 frameStart;       /* the frame copied; paused: the frame in progress */
    UInt32 seq;              /* frames the chip had completed when the copy was made, counting the one shown */
    int    behind;           /* frames completed since the copy; 0: it is the newest */
    int    rows;             /* 240; 400 or 480 in B5/B6 */
    int    frameMode;        /* V9990Mode latched for the frame */
    int    interlace;        /* InterlaceMode: 0 none, 1 odd, 2 even field */
    int    interlaceRaster;  /* IL or B5/B6: no gaps between lines */
    int    secondField;
    int    frameLines;       /* scan lines in the frame */
    int    windowTopLine;    /* scan line of row 0, counted from the frame's first line start */
    int    displayStartLine; /* display lines are [displayStartLine, displayEndLine) */
    int    displayEndLine;
    UInt8  shift[V9990_SHOT_ROWS];
    UInt16 width[V9990_SHOT_ROWS];   /* 272 or 288 (overscan) << shift */
    UInt16 rowY[V9990_SHOT_ROWS];    /* the bitmap row drawn, 0xffff when not drawn from the bitmap */
    UInt16 rowX[V9990_SHOT_ROWS];    /* the bitmap column the scroll put at the display's left */
    UInt16 layerY[2][V9990_SHOT_ROWS]; /* [0] P1's A or P2's layer, [1] P1's B: the image line drawn, 0xffff when not drawn from it */
    UInt16 layerX[2][V9990_SHOT_ROWS]; /* its image dot at the display's left */
    UInt16 pix[V9990_SHOT_ROWS][V9990_SHOT_WIDTH];
    /* Sample x of a row shows display dot floor((dotOrg + x * sampleUc) / dotUc); dotUc 0: no display dot. */
    Int16  dotOrg[V9990_SHOT_ROWS];
    UInt8  dotUc[V9990_SHOT_ROWS];
    UInt8  sampleUc[V9990_SHOT_ROWS];
    /* While the sprite split is armed, rows with splitRow 1 also hold the row drawn without
    ** sprites and cursors (bg) and per sample the sprite (0-124) or 0x80 + cursor shown there (hit, 0xff none). */
    int    sprSplit;
    UInt8  splitRow[V9990_SHOT_ROWS];
    UInt16 bg[V9990_SHOT_ROWS][V9990_SHOT_WIDTH];
    UInt8  hit[V9990_SHOT_ROWS][V9990_SHOT_WIDTH];
    /* While armed, as the first display line split read them: 1, the P1/P2 sprite attribute
    ** table (physical 3FE00); 2, the bitmap modes' 16 cursor attribute bytes from 7FE00; 0, none. */
    int    sprAttrValid;
    UInt8  sprAttr[512];
} V9990FrameShot;

/* Copies the picture of the chip on the same terms. Asking also has the chip
** copy each frame it completes from now on for a while, so the first answers
** after a pause in asking are stale or not valid. */
int v9990DebugCaptureFrame(int index, V9990FrameShot* out);

/* Starts (on) or stops recording the sprite split of the V9990's frames; calls
** nest. UI thread only. Costs a copy of each line's dots while it lasts. */
void v9990SprSplitArm(int on);

#ifdef __cplusplus
}
#endif

#endif
