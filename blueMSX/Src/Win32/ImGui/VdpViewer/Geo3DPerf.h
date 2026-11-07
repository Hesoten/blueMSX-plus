/*****************************************************************************
**
** Drawing time per screen update of geo3d games, from the event trace.
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
#ifndef GEO3D_PERF_H
#define GEO3D_PERF_H

#include "MsxTypes.h"

/* A picture runs from one page flip to the next. Each tick of it goes to the first of these
** that was busy, else to OTHER; the CPU's three are split at the last end of the VDP's and
** geo3d's, the TAIL ones after it. */
enum { G3P_GEO, G3P_VDP, G3P_RUN, G3P_IRQ, G3P_HALT, G3P_OTHER,
       G3P_IRQ_TAIL, G3P_HALT_TAIL, G3P_OTHER_TAIL, G3P_PARTS };
#define G3P_TAIL_OF(k) ((k) + G3P_IRQ_TAIL - G3P_IRQ)

/* G3P_CUT: its start or end was not seen, or a command or RUN ran across its flip, or was
** still open when one without a flip settled. */
#define G3P_CUT      0x01
#define G3P_DROPPED  0x02   /* trace records were lost while it was open */
#define G3P_NO_FLIP  0x04   /* no flip: a frame, or the 2 s before they were taken one by one */
#define G3P_GAP      0x08   /* what came just before it was not seen */
#define G3P_DETAIL_CUT 0x10 /* more spans than its detail holds */

#define G3P_PICTURES   512
#define G3P_RUN_SLOTS  16     /* objects of a picture whose VDP time is kept */
#define G3P_DETAIL_MAX 16384  /* spans of one picture's detail */

/* What a span is: the VDP running a command geo3d issued or one the CPU did, geo3d
** running a RUN, the CPU polling CE or polling geo3d's busy, in an interrupt handler
** (arg the address left) or halted (arg the HALT's address). */
enum { G3P_SPAN_GEO, G3P_SPAN_VDP, G3P_SPAN_RUN, G3P_SPAN_WAIT_CE, G3P_SPAN_WAIT_RUN,
       G3P_SPAN_IRQ, G3P_SPAN_HALT };

struct Geo3DPerfSpan {
    UInt64 s, e;            /* board ticks */
    UInt32 run;             /* a RUN's number, or that of the RUN that issued a command; 0 none */
    UInt32 obj;             /* that RUN's object: its vertex list, first | count << 16; 0 unknown */
    UInt32 arg;             /* a command's index in its RUN; commands a RUN issued; polls of a wait */
    UInt32 dots;            /* dots a command wrote */
    UInt16 nx, ny, dx, dy;
    UInt8  kind;
    UInt8  cm;
    UInt8  aborted;
    UInt8  cut;             /* ran across its picture's flip, or was open when it settled: it ends there */
    UInt8  lo;              /* a command's logic operation, ARG, geo3d's one row fill flag and CL */
    UInt8  cmdArg;
    UInt8  fill;
    UInt8  cl;
};

/* An object the RUNs of a picture drew, and the VDP time their commands took within it. */
struct Geo3DPerfObj {
    UInt32 obj, fill, runs;
};

struct Geo3DPerfPicture {
    UInt64 t0, t1;          /* board ticks: the R#2 writes that showed the page before it and its own */
    UInt32 number;          /* from 1 since the fold began */
    UInt32 frames;          /* whole frames from t0 to t1 */
    UInt32 frameLo;         /* the machine frame number of the first of them, as the deck counts; 0 unknown */
    UInt64 firstVint;       /* board ticks: the display end (vint) of the first of them; 0 unknown */
    UInt64 shownVint;       /* board ticks: the display end of the first frame showing its page; 0 no flip */
    UInt32 period;          /* board ticks a frame, as measured then */
    Int32  page;            /* the page shown from t1, or -1 */
    UInt32 flags;
    UInt32 part[G3P_PARTS]; /* board ticks; together t1 - t0 */
    UInt32 tail;            /* the TAIL parts together: from the last VDP or RUN end to t1 */
    UInt32 busyEnd;         /* board ticks from t0 to that end, where the TAIL parts begin */
    UInt32 vsync0;          /* board ticks from t0 to the first frame display end (vint) at or after it */
    UInt32 vramWritten, vramRead;    /* CPU bytes through the VDP's data port, by frame midpoints in it */
    UInt32 feedModel, feedOther;     /* CPU bytes to geo3d's data port, by the RUNs begun in it */
    UInt32 cmdsGeo, cmdsZ80, runs;   /* begun within it */
    UInt32 dotsGeo, dotsZ80;
    UInt32 objCount;
    Geo3DPerfObj objs[G3P_RUN_SLOTS];
    UInt32 detailFirst, detailCount; /* the fold's own */
};

struct Geo3DPerfView {
    UInt32 version;         /* changes with each picture settled, and on a reset */
    UInt32 gen;             /* as Geo3DPerfDetail's: a detail of another gen has other times */
    int    count;           /* pictures in pic, oldest first */
    UInt32 period;          /* board ticks a frame, 0 before two frames were seen */
    int    perFrame;        /* no flip for 2 s: a picture for each frame */
    /* Board ticks: the latest frame display end (vint) read. A picture whose flip showed by then
    ** is in pic already. Take updates it even when the pictures did not change. */
    UInt64 frameT;
    Geo3DPerfPicture pic[G3P_PICTURES];
};

/* The index of picture number in v, or -1; numbers only grow, with gaps after a rewind. */
int geo3dPerfIndexOf(const Geo3DPerfView* v, UInt32 number);

struct Geo3DPerf;

/* UI thread. Open reads the trace from now on, on the emulator thread; close
** waits for that to end, lets go of the trace and frees. */
Geo3DPerf* geo3dPerfOpen();
void       geo3dPerfClose(Geo3DPerf* p);
/* Arms the trace classes it reads, or lets go of them; the pictures stay. */
void       geo3dPerfArm(Geo3DPerf* p, bool on);
bool       geo3dPerfArmed(const Geo3DPerf* p);
/* The VDP's two sources, its geo3d's and the CPU's, and whether that VDP is a cartridge's;
** a change starts the fold again. */
void       geo3dPerfSources(Geo3DPerf* p, UInt16 vdpCmd, UInt16 vdpReg, UInt16 geo3d, UInt16 cpu, bool cartridge);
/* Copies the pictures into out when they changed since out->version; true then. */
bool       geo3dPerfTake(Geo3DPerf* p, Geo3DPerfView* out);

enum { G3P_DETAIL_WAIT, G3P_DETAIL_OK, G3P_DETAIL_GONE };

/* The spans of one picture: its own and those reaching into it, in no order. */
struct Geo3DPerfDetail {
    UInt32 number;
    UInt32 gen;             /* changes when a rewind moves the times of the pictures kept */
    int    state;
    int    count;
    Geo3DPerfSpan span[G3P_DETAIL_MAX];
};

/* Asks for picture number's detail, 0 for none, G3P_DETAIL_LATEST for each latest one as it
** settles; the fold keeps it only for the latest pictures. Take copies it into out once it
** came or went; true then. */
#define G3P_DETAIL_LATEST 0xFFFFFFFFu
void       geo3dPerfWantDetail(Geo3DPerf* p, UInt32 number);
bool       geo3dPerfTakeDetail(Geo3DPerf* p, Geo3DPerfDetail* out);

#endif
