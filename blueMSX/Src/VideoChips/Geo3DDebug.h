/*****************************************************************************
**
** Snapshot of a geo3d coprocessor and its latest RUNs for the tool windows.
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
#ifndef GEO3D_DEBUG_H
#define GEO3D_DEBUG_H

#include "MsxTypes.h"

#define GEO3D_DEBUG_ENTRIES 1024   /* entries in each model RAM */
#define GEO3D_DEBUG_RUNS    32
#define GEO3D_DEBUG_ITEMS   1024
#define GEO3D_DEBUG_CMDS    3072

/* The device's phase while it waits for the VDP to finish a command. */
#define GEO3D_PHASE_WAIT_CE 1

/* What a RUN did with a face or edge. NEAR: a vertex behind the near plane or
** saturated (counted SKIPPED). OFFSCREEN: an edge wholly off one side; BACK: a
** face turned away (both CULLED). UNCUT: an edge that misses the screen (SKIPPED). */
enum { GEO3D_ITEM_DRAWN, GEO3D_ITEM_NEAR, GEO3D_ITEM_BACK, GEO3D_ITEM_OFFSCREEN, GEO3D_ITEM_UNCUT };

typedef struct {
    Int16  sx, sy, z;
    UInt8  flags;           /* bit 0 near, 1-2 saturated, 4-7 off left/right/top/bottom */
    UInt8  pad;
} Geo3DDebugProj;

typedef struct {
    UInt16 i[4];
    Int16  n[3];
    UInt8  base;
    UInt8  pad;
} Geo3DDebugFace;

/* A VDP command in the order geo3d writes it: 11 bytes for R#36-46 (LINE),
** 19 for R#32-45, R#47-50, R#46 (LRMM). */
typedef struct {
    UInt8  size;
    UInt8  bytes[19];
} Geo3DDebugCmd;

/* A face or edge as the RUN saw it; the Z80 may have rewritten the RAMs since. */
typedef struct {
    UInt16 index;           /* face or edge number */
    UInt8  outcome;         /* GEO3D_ITEM_* */
    UInt8  textured;
    UInt8  col;
    UInt8  lvl;             /* light level of a face, 0-6 */
    UInt16 order;           /* paint order of a drawn face */
    UInt16 v[4];            /* vertex numbers; an edge has two */
    Int16  sx[4];           /* their projections at the RUN */
    Int16  sy[4];
    Int32  cx[4];           /* their camera x and z at the RUN */
    Int32  cz[4];
    Int16  ymin, ymax;      /* rows drawn; ymin > ymax for none */
    UInt16 cmdFirst;        /* in cmd[] */
    UInt16 cmdCount;        /* of them in cmd[] */
    UInt16 cmdTotal;        /* the RUN made; more than cmdCount when cut */
} Geo3DDebugItem;

typedef struct {
    UInt32 seq;             /* RUNs since reset or a state load, from 1 */
    UInt32 startTime;       /* board ticks */
    UInt32 endTime;         /* board ticks, when RUN busy cleared; valid if ended */
    int    ended;
    int    faces;
    int    tex;
    UInt16 vfirst, nvert;
    UInt16 first, count;    /* EFIRST, NEDGE or FFIRST, NFACE */
    UInt16 ypage;
    UInt8  color, lop;
    UInt16 skipped, drawn, culled;
    Int32  lm[3];           /* light in model space (faces) */
    Int32  cfg[18];         /* configuration words at RUN */
    UInt16 texx, texy;
    UInt8  tstride;
    UInt32 xfEndClock;      /* engine clocks (2 per board tick) from RUN */
    UInt32 endClock;
    UInt16 itemFirst, itemCount;   /* in item[] */
    UInt16 cmdFirst, cmdCount;     /* in cmd[] */
    UInt32 itemTotal;       /* the RUN had; more than itemCount when cut */
    UInt32 cmdTotal;
} Geo3DDebugRun;

typedef struct {
    int    valid;           /* 0: no geo3d beside this VDP */
    int    cartridge;
    int    port;            /* index and status; port + 2 is the data */
    int    ext;             /* XCTRL bit 0 */
    int    entries;
    Int32  cfg[18];         /* M00-M22, TX, TY, TZ, F, CX, CY, ZNEAR, W, H */
    Int32  ivtx[3];
    Geo3DDebugProj imm;
    UInt16 vaddr, eaddr, faddr, taddr;
    UInt16 nvert, nedge, nface;
    UInt16 vfirst, efirst, ffirst;
    UInt16 ypage, texx, texy;
    UInt8  color, lop, tstride;
    UInt8  widx, rptr;      /* write index, read pointer */
    UInt8  status;
    Int32  light[3];
    int    running;
    int    faces, tex;      /* CTRL bits 1, 2 as last written */
    UInt16 skipped, drawn, culled;
    UInt32 runSeq;          /* the latest RUN; 0 for none */
    int    cmdsTotal;       /* of the RUN in progress */
    int    cmdsIssued;
    int    devPhase;        /* GEO3D_PHASE_WAIT_CE: an issued command is the VDP's to finish */
    UInt8  regs[0x70];      /* as the data port reads them; 0xff where it cannot */
    UInt32 epoch;           /* changes whenever the RUN numbers start again */
    /* run[]: the RUNs after the tool's geo3dDebugSince and those busy in
    ** [windowStart, windowEnd] (board ticks), oldest first, up to the latest;
    ** run[frameFirst..] were busy; fallback: none was, frameFirst = runCount. */
    UInt32 windowStart, windowEnd;
    int    frameFirst;
    int    fallback;
    int    runsLost;        /* nonzero: at least this many busy in the window are not in run[] */
    UInt32 wholeSeq;        /* the RUN copied first, with all the room; 0 for none */
    UInt32 pageFrom;        /* a RUN known to be the first on its engine page since the last turn; 0 none */
    int    runCount;
    int    itemCount;
    int    cmdCount;
    Geo3DDebugRun  run[GEO3D_DEBUG_RUNS];
    Int16          vmem[GEO3D_DEBUG_ENTRIES][3];
    UInt16         emem[GEO3D_DEBUG_ENTRIES][2];
    Geo3DDebugFace fmem[GEO3D_DEBUG_ENTRIES];
    UInt8          tmem[GEO3D_DEBUG_ENTRIES][8];
    Geo3DDebugProj pmem[GEO3D_DEBUG_ENTRIES];
    Geo3DDebugItem item[GEO3D_DEBUG_ITEMS];
    Geo3DDebugCmd  cmd[GEO3D_DEBUG_CMDS];
} Geo3DDebugState;

#endif
