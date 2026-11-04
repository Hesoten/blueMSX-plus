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
#define NOMINMAX
#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include "Geo3DPerf.h"
#include "LiveFeed.h"
extern "C" {
#include "Emulator.h"
#include "Board.h"
#include "DebugTrace.h"
#include "V9938.h"
#include "VdpDebug.h"
#include "Geo3DDevice.h"
#include "R800.h"
}

#define READ_MAX   4096
#define TALLY_MAX  16384
#define SPANS_MAX  65536
#define QUEUE_MAX  64
#define NO_PAGE    0xFFFFFFFFu
#define CUT_FRAMES 120
#define LINKS      8192
#define DETAIL_RING 65536
/* The most frames a reverse play step is asked to replay for the picture on screen. */
#define LEAD_FRAMES 60

namespace {

/* The spans that take parts, in the order they take them; the waits take none. */
enum { CAT_GEO = G3P_SPAN_GEO, CAT_VDP = G3P_SPAN_VDP, CAT_RUN = G3P_SPAN_RUN, CAT_IRQ, CAT_HALT, CAT_NONE };

typedef Geo3DPerfSpan Span;

UInt32 catOf(UInt32 kind)
{
    return kind <= G3P_SPAN_RUN ? kind : kind == G3P_SPAN_IRQ ? CAT_IRQ : kind == G3P_SPAN_HALT ? CAT_HALT : CAT_NONE;
}

/* Bytes the CPU moved, counted at time t: a frame's through the VDP, or a RUN's to geo3d. */
struct Tally {
    UInt64 t;
    UInt32 a, b;
    bool   feed;
};

/* What issued the VDP command a link was given to. */
struct Link {
    UInt32 link, run, index;
};

/* A picture whose end is known, waiting for what began before it to end. */
struct Pending {
    UInt64 t0, t1;
    UInt64 vsync;           /* the first frame display end (vint) at or after t0 */
    UInt32 frames;
    UInt32 frameLo;
    UInt64 shownVint;
    Int32  page;
    UInt32 flags;
    bool   atSwitch;        /* closed at a known R#2 switch: settled at once, what crossed it cut */
};

struct Clip {
    UInt64 s, e;
    UInt32 cat;
};

}

/* The fold is the emulator thread's, and the UI thread's only once it is gone (STOPPED);
** the UI reads pub under pubLock and writes only the cfg fields. */
struct Geo3DPerf {
    SRWLOCK         pubLock;
    Geo3DPerfView   pub;
    Geo3DPerfView   own;
    bool            armed;
    volatile LONG   cfgVersion;
    volatile LONG   cfgVdp;     /* VDPCMD source | VDPREG source << 16 */
    volatile LONG   cfgGeo;
    volatile LONG   cfgCpu;
    volatile LONG   cfgCart;
    LONG            cfgSeen;

    UInt16 srcCmd, srcReg, srcGeo, srcCpu;
    bool   cart;                /* the VDP read is a cartridge's */
    bool   haveSources;
    UInt32 lastSeq;
    DebugTraceRecord buf[READ_MAX];

    Span*  spans;
    int    spanCount;
    Clip*  clips;
    Link   links[LINKS];
    /* The detail of the latest pictures: a ring that later ones write over. */
    Span*  ring;
    UInt32 ringHead;
    volatile LONG detWant;
    UInt32 detNumber;           /* the detail last handed over, as pubDet has it */
    UInt32 detGen;              /* bumped by a rewind, which moves the times */
    int    detState;
    Geo3DPerfDetail* pubDet;
    Pending queue[QUEUE_MAX];
    int    queueHead, queueCount;

    /* Open state, carried across reads. */
    bool   cmdOpen, cmdOpenGeo;
    UInt64 cmdOpenT;
    UInt8  cmdOpenCm;
    UInt32 cmdOpenRun, cmdOpenIndex;
    UInt32 runOpenObj;
    Link   runObjs[256];        /* RUN number to object, as link to RUN: link = RUN, run = object */
    bool   ceWaitOpen, runWaitOpen;
    UInt64 ceWaitT, runWaitT;
    bool   runOpen;
    UInt64 runOpenT;
    UInt32 runOpenSeq;
    bool   irqOpen, haltOpen;
    UInt64 irqT, haltT;
    UInt32 irqPc, haltPc;
    Tally  tallies[TALLY_MAX];
    int    tallyCount;
    int    contSpan;            /* the span the next CMD CONT belongs to, or -1 */
    UInt32 contCm;

    bool   haveFrame;
    UInt64 prevT, lastT;
    UInt64 prevPrevT;           /* the display end (vint) before prevT */
    struct PageWrite { UInt64 t; UInt32 now, was; } pageWrites[16];
    int    pageWriteNext;
    UInt32 prevPage;
    bool   open;
    UInt64 openT, openVsync;
    UInt32 openFrames, openFlags;
    UInt32 openFrameLo;
    UInt32 nextFlags;           /* for the next picture to open */
    int    perFrame;
    UInt32 period;
    UInt32 number;
    /* A rewind's break until its resume: when, and the epoch it began. */
    bool   rwBreak;
    UInt64 rwBreakT;
    UInt32 rwBreakEpoch;
    /* The picture kept whole across a rewind's snapshot: the time its flip comes again; 0 none. */
    UInt64 rwKeepT1;
    bool   rwNoPeriod;          /* the first frame after a rewind is no measure of the period */
};

static UInt32 clampTicks(UInt64 v)
{
    return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (UInt32)v;
}

static void publish(Geo3DPerf* p)
{
    bool pics = p->pub.version != p->own.version;
    if (!pics && p->pub.frameT == p->lastT) {
        return;
    }
    if (TryAcquireSRWLockExclusive(&p->pubLock)) {
        size_t head = offsetof(Geo3DPerfView, pic);
        if (pics) memcpy(&p->pub, &p->own, head + (size_t)p->own.count * sizeof(Geo3DPerfPicture));
        p->pub.frameT = p->lastT;
        ReleaseSRWLockExclusive(&p->pubLock);
    }
}

static void bump(Geo3DPerf* p)
{
    p->own.version++;
    if (p->own.version == 0) p->own.version = 1;
}

/* Forgets what is open; the pictures settled stay. */
static void clearOpen(Geo3DPerf* p)
{
    p->spanCount  = 0;
    p->queueHead  = 0;
    p->queueCount = 0;
    p->cmdOpen    = false;
    p->runOpen    = false;
    p->ceWaitOpen = false;
    p->runWaitOpen = false;
    p->irqOpen    = false;
    p->haltOpen   = false;
    p->tallyCount = 0;
    memset(p->links, 0, sizeof(p->links));
    memset(p->runObjs, 0, sizeof(p->runObjs));
    memset(p->pageWrites, 0, sizeof(p->pageWrites));
    p->contSpan   = -1;
    p->haveFrame  = false;
    p->open       = false;
    p->openFrames = 0;
    p->openFlags  = 0;
    p->nextFlags  = G3P_GAP;
    if (p->perFrame) {
        p->perFrame = 0;
        p->own.perFrame = 0;
        bump(p);
    }
}

/* A new span of kind from s to e, cleared otherwise; NULL when there is no room. */
static Span* addSpan(Geo3DPerf* p, UInt64 s, UInt64 e, UInt32 kind)
{
    if (p->spanCount >= SPANS_MAX) {
        /* Too many to hold: what is open is missing some of them. */
        p->openFlags |= G3P_DROPPED;
        for (int i = 0; i < p->queueCount; i++) p->queue[(p->queueHead + i) % QUEUE_MAX].flags |= G3P_DROPPED;
        return NULL;
    }
    Span& sp = p->spans[p->spanCount++];
    memset(&sp, 0, sizeof(sp));
    sp.s = s;
    sp.e = e > s ? e : s;
    sp.kind = (UInt8)kind;
    return &sp;
}

/* Union length within [lo, hi) of the clips of category up to maxCat, sorted by start; last
** gets the latest end, 0 for none. */
static UInt64 unionOf(const Clip* c, int n, UInt32 maxCat, UInt64 lo, UInt64 hi, UInt64* last)
{
    UInt64 total = 0, s = 0, e = 0;
    bool any = false;
    for (int i = 0; i < n; i++) {
        UInt64 cs = std::max(c[i].s, lo), ce = std::min(c[i].e, hi);
        if (c[i].cat > maxCat || ce <= cs) continue;
        if (!any || cs > e) {
            if (any) total += e - s;
            s = cs;
            e = ce;
            any = true;
        }
        else if (ce > e) {
            e = ce;
        }
    }
    if (any) total += e - s;
    if (last) *last = any ? e : 0;
    return total;
}

/* The parts of [lo, hi) by the clips: each category's ticks the ones before it left, the rest to
** OTHER; at is the first of the three CPU parts to fill, VDP and RUN parts going to theirs. */
static void partsIn(const Clip* c, int n, UInt64 lo, UInt64 hi, UInt32* part, int at)
{
    UInt64 before = 0;
    for (UInt32 k = CAT_GEO; k <= CAT_HALT && hi > lo; k++) {
        UInt64 u = unionOf(c, n, k, lo, hi, NULL);
        int to = k < CAT_IRQ ? (int)k : at + (int)(k - CAT_IRQ);
        part[to] += clampTicks(u - before);
        before = u;
    }
    if (hi > lo) part[at + 2] += clampTicks(hi - lo - before);
}

/* Nothing that ended by t1 counts for a later picture. */
static void forgetSpans(Geo3DPerf* p, UInt64 t1)
{
    int k = 0;
    for (int i = 0; i < p->spanCount; i++) {
        if (p->spans[i].e > t1) {
            if (i == p->contSpan) p->contSpan = k;
            p->spans[k++] = p->spans[i];
        }
        else if (i == p->contSpan) {
            p->contSpan = -1;
        }
    }
    p->spanCount = k;
}

static void settleOne(Geo3DPerf* p, const Pending& q)
{
    /* The first picture after a rewind: the copy kept whole stands for it if its flip came
    ** again, else it goes. */
    if (p->rwKeepT1 != 0) {
        bool same = q.t1 == p->rwKeepT1;
        p->rwKeepT1 = 0;
        if (same) {
            int kept = 0;
            for (int i = 0; i < p->tallyCount; i++) {
                if (p->tallies[i].t >= q.t1) p->tallies[kept++] = p->tallies[i];
            }
            p->tallyCount = kept;
            forgetSpans(p, q.t1);
            return;
        }
        if (p->own.count > 0) {
            p->own.count--;
            bump(p);
        }
    }
    Geo3DPerfPicture pic;
    int n = 0;
    memset(&pic, 0, sizeof(pic));
    pic.t0     = q.t0;
    pic.t1     = q.t1;
    pic.frames = q.frames;
    pic.frameLo = q.frameLo;
    pic.shownVint = q.shownVint;
    pic.period = p->period;
    pic.page   = q.page;
    pic.flags  = q.flags;
    pic.number = ++p->number;
    pic.vsync0 = clampTicks(q.vsync > q.t0 ? q.vsync - q.t0 : 0);
    for (int i = 0; i < p->spanCount; i++) {
        const Span& sp = p->spans[i];
        if (sp.s >= q.t0 && sp.s < q.t1) {
            if (sp.kind == CAT_GEO) { pic.cmdsGeo++; pic.dotsGeo += sp.dots; }
            else if (sp.kind == CAT_VDP) { pic.cmdsZ80++; pic.dotsZ80 += sp.dots; }
            else if (sp.kind == CAT_RUN) pic.runs++;
        }
        UInt64 s = std::max(sp.s, q.t0), e = std::min(sp.e, q.t1);
        if (e > s && catOf(sp.kind) != CAT_NONE) {
            p->clips[n].s = s;
            p->clips[n].e = e;
            p->clips[n].cat = catOf(sp.kind);
            n++;
        }
    }
    /* The bytes counted within it; those before its end count for no later one. */
    int kept = 0;
    for (int i = 0; i < p->tallyCount; i++) {
        const Tally& y = p->tallies[i];
        if (y.t >= q.t0 && y.t < q.t1) {
            if (y.feed) { pic.feedModel += y.a; pic.feedOther += y.b; }
            else { pic.vramWritten += y.a; pic.vramRead += y.b; }
        }
        if (y.t >= q.t1) p->tallies[kept++] = y;
    }
    p->tallyCount = kept;
    /* The VDP time within it of each object its RUNs drew, and its detail into the ring. */
    bool flip = q.atSwitch;
    pic.detailFirst = p->ringHead;
    for (int i = 0; i < p->spanCount; i++) {
        const Span& sp = p->spans[i];
        if (sp.e < q.t0 || sp.s >= q.t1) continue;
        /* A command or RUN running at the switch is cut there; only a command into the page
        ** that came on showed half done, the rest drew elsewhere. */
        Span cp = sp;
        if (flip && sp.e > q.t1 && sp.kind <= G3P_SPAN_RUN) {
            cp.e = q.t1;
            cp.cut = 1;
            if (sp.kind != G3P_SPAN_RUN && (Int32)(sp.dy >> 8) == q.page) pic.flags |= G3P_CUT;
        }
        if ((sp.kind == CAT_GEO || (sp.kind == CAT_RUN && sp.s >= q.t0)) && sp.obj != 0) {
            UInt32 k = 0;
            while (k < pic.objCount && pic.objs[k].obj != sp.obj) k++;
            if (k == pic.objCount && k < G3P_RUN_SLOTS) {
                pic.objs[k].obj = sp.obj;
                pic.objCount++;
            }
            if (k < pic.objCount && sp.kind == CAT_RUN) pic.objs[k].runs++;
            if (k < pic.objCount && sp.kind == CAT_GEO) {
                pic.objs[k].fill += clampTicks(std::min(sp.e, q.t1) - std::max(sp.s, q.t0));
            }
        }
        if (pic.detailCount == G3P_DETAIL_MAX) {
            pic.flags |= G3P_DETAIL_CUT;
            continue;
        }
        p->ring[p->ringHead++ % DETAIL_RING] = cp;
        pic.detailCount++;
    }
    std::sort(p->clips, p->clips + n, [](const Clip& a, const Clip& b) { return a.s < b.s; });
    /* The CPU's parts after the VDP and geo3d last were busy are the TAIL ones; none busy, none. */
    UInt64 last = 0;
    if (unionOf(p->clips, n, CAT_RUN, q.t0, q.t1, &last) == 0) last = q.t1;
    partsIn(p->clips, n, q.t0, last, pic.part, G3P_IRQ);
    partsIn(p->clips, n, last, q.t1, pic.part, G3P_IRQ_TAIL);
    pic.tail = pic.part[G3P_IRQ_TAIL] + pic.part[G3P_HALT_TAIL] + pic.part[G3P_OTHER_TAIL];
    pic.busyEnd = clampTicks(last - q.t0);

    Geo3DPerfView& v = p->own;
    if (v.count == G3P_PICTURES) {
        memmove(&v.pic[0], &v.pic[1], (G3P_PICTURES - 1) * sizeof(Geo3DPerfPicture));
        v.count--;
    }
    v.pic[v.count++] = pic;
    v.period = p->period;
    bump(p);
    forgetSpans(p, q.t1);
}

static UInt32 objOf(const Geo3DPerf* p, UInt32 run)
{
    const Link& l = p->runObjs[run % 256];
    return run != 0 && l.link == run ? l.run : 0;
}

/* Settles the oldest picture waiting, counting a command or RUN still open as busy to its end. */
static void settleFront(Geo3DPerf* p)
{
    Pending q = p->queue[p->queueHead];
    if ((p->cmdOpen && p->cmdOpenT < q.t1) || (p->runOpen && p->runOpenT < q.t1)) {
        q.flags |= G3P_CUT;
        if (p->cmdOpen && p->cmdOpenT < q.t1) {
            Span* sp = addSpan(p, p->cmdOpenT, q.t1, p->cmdOpenGeo ? CAT_GEO : CAT_VDP);
            if (sp) {
                sp->cut = 1;
                sp->cm = p->cmdOpenCm;
                sp->run = p->cmdOpenRun;
                sp->arg = p->cmdOpenIndex;
                sp->obj = objOf(p, sp->run);
            }
        }
        if (p->runOpen && p->runOpenT < q.t1) {
            Span* sp = addSpan(p, p->runOpenT, q.t1, CAT_RUN);
            if (sp) {
                sp->cut = 1;
                sp->run = p->runOpenSeq;
                sp->obj = p->runOpenObj;
            }
        }
    }
    /* A wait across the end shows up to it; the rest comes with its end record. */
    Span* w = p->ceWaitOpen && p->ceWaitT < q.t1 ? addSpan(p, p->ceWaitT, q.t1, G3P_SPAN_WAIT_CE) : NULL;
    if (w) w->cut = 1;
    w = p->runWaitOpen && p->runWaitT < q.t1 ? addSpan(p, p->runWaitT, q.t1, G3P_SPAN_WAIT_RUN) : NULL;
    if (w) w->cut = 1;
    w = p->irqOpen && p->irqT < q.t1 ? addSpan(p, p->irqT, q.t1, G3P_SPAN_IRQ) : NULL;
    if (w) { w->cut = 1; w->arg = p->irqPc; }
    w = p->haltOpen && p->haltT < q.t1 ? addSpan(p, p->haltT, q.t1, G3P_SPAN_HALT) : NULL;
    if (w) { w->cut = 1; w->arg = p->haltPc; }
    settleOne(p, q);
    p->queueHead = (p->queueHead + 1) % QUEUE_MAX;
    p->queueCount--;
}

/* Settles a picture ended by a flip at once, and those before it, cutting at the end what is
** still open; one without waits until its commands and RUN begun before its end have ended.
** force settles them all. */
static void settle(Geo3DPerf* p, bool force)
{
    int now = 0;
    for (int i = 0; i < p->queueCount; i++) {
        if (force || p->queue[(p->queueHead + i) % QUEUE_MAX].atSwitch) now = i + 1;
    }
    for (; p->queueCount > 0; now--) {
        Pending q = p->queue[p->queueHead];
        bool waitCmd = p->cmdOpen && p->cmdOpenT < q.t1;
        bool waitRun = p->runOpen && p->runOpenT < q.t1;
        if (now <= 0) {
            /* Also waits for the CONT of the last command read, which has its dots. */
            bool late = p->period != 0 && p->lastT > q.t1 + (UInt64)CUT_FRAMES * p->period;
            bool cont = p->contSpan >= 0 && p->spans[p->contSpan].s < q.t1 && p->contCm != 0xFF;
            if ((waitCmd || waitRun || cont) && !late) break;
        }
        settleFront(p);
    }
}

/* shown is the display end of the frame its flip showed in, 0 when it ends without one. */
static void closePicture(Geo3DPerf* p, UInt64 t0, UInt64 t1, UInt32 frames, UInt32 page, UInt32 flags, UInt64 shown,
                         bool atSwitch = false)
{
    if (frames == 0 || t1 <= t0) {
        return;
    }
    if (p->queueCount == QUEUE_MAX) {
        p->queue[p->queueHead].flags |= G3P_CUT;
        settleFront(p);
    }
    Pending& q = p->queue[(p->queueHead + p->queueCount++) % QUEUE_MAX];
    q.t0 = t0;
    q.t1 = t1;
    q.vsync = p->openVsync;
    q.frames = frames;
    q.frameLo = p->openFrameLo;
    q.shownVint = shown;
    q.page = page == NO_PAGE ? -1 : (Int32)page;
    q.flags = flags;
    q.atSwitch = atSwitch;
}

/* vsync is the first frame display end (vint) at or after t, link the machine frame number
** that ended there (0 unknown); frames 0 counts from the frame after it. */
static void openPicture(Geo3DPerf* p, UInt64 t, UInt64 vsync, UInt32 frames, UInt32 link)
{
    p->open = true;
    p->openT = t;
    p->openVsync = vsync;
    p->openFrames = frames;
    p->openFrameLo = link != 0 ? link + (frames == 0 ? 1 : 0) : 0;
    p->openFlags = p->nextFlags;
    p->nextFlags = 0;
}

/* Whether an R#2 write from was to now turned the screen to page; every bitmap mode
** counts its own pages from bit 5. */
static bool r2Turns(UInt32 now, UInt32 was, UInt32 page)
{
    return ((now >> 5) & 7) == page && ((was >> 5) & 7) != page;
}

/* When the page the frame that ended at time showed came on: the earliest R#2 write that
** turned to it since the frame before, so the work right after a flip goes with the next
** picture; else the frame before's display end. A split's writes back to it come later. */
static UInt64 flipTime(const Geo3DPerf* p, UInt64 time, UInt32 page, bool* known)
{
    UInt64 best = 0;
    for (int i = 0; i < 16; i++) {
        const Geo3DPerf::PageWrite& w = p->pageWrites[i];
        if (w.t > p->prevPrevT && w.t <= time && w.t > p->openT && r2Turns(w.now, w.was, page) &&
            (best == 0 || w.t < best)) {
            best = w.t;
        }
    }
    *known = best != 0;
    return best != 0 ? best : p->prevT;
}

/* time is the end of a frame's active display; that frame showed page most, and was machine
** frame link. */
static void onFrame(Geo3DPerf* p, UInt64 time, UInt32 page, UInt32 link)
{
    p->lastT = time;
    if (!p->haveFrame) {
        /* The picture on screen began before this was seen. */
        p->haveFrame = true;
        p->prevT = time;
        p->prevPage = page;
        p->nextFlags |= G3P_CUT;
        openPicture(p, time, time, 0, link);
        return;
    }
    UInt64 d = time - p->prevT;
    if (d > boardFrequency() / 120 && d < boardFrequency() / 20 && !p->rwNoPeriod) {
        p->period = (UInt32)d;
        p->own.period = p->period;
    }
    p->rwNoPeriod = false;
    if (p->prevPage == NO_PAGE) {
        p->prevPage = page;
    }
    bool flip = page != NO_PAGE && page != p->prevPage;
    bool known = false;
    UInt64 cut = flip ? flipTime(p, time, page, &known) : 0;
    if (flip) {
        /* The new page showed in the frame just ended: the picture drawn ends at the flip. */
        closePicture(p, p->openT, cut, p->openFrames, page, p->openFlags, time, known);
        if (p->perFrame) {
            p->perFrame = 0;
            p->own.perFrame = 0;
            bump(p);
        }
        /* The flip falls in the frame just ended or the one before; frames count from the
        ** one just ended, the first to show the new page, as the one before went to the
        ** picture closed. */
        openPicture(p, cut, cut <= p->prevT ? p->prevT : time, 1, link);
        p->prevPage = page;
    }
    else if (p->perFrame) {
        closePicture(p, p->openT, time, 1, p->prevPage, p->openFlags | G3P_NO_FLIP, 0);
        openPicture(p, time, time, 0, link);
    }
    else {
        p->openFrames++;
        UInt32 limit = p->period ? (UInt32)((2ull * boardFrequency() + p->period - 1) / p->period) : 0;
        if (limit > 0 && p->openFrames >= limit) {
            closePicture(p, p->openT, time, p->openFrames, p->prevPage, p->openFlags | G3P_NO_FLIP | G3P_CUT, 0);
            openPicture(p, time, time, 0, link);
            p->perFrame = 1;
            p->own.perFrame = 1;
            bump(p);
        }
    }
    p->prevPrevT = p->prevT;
    p->prevT = time;
}

/* A rewind loaded a state of time oldT, now newT, and the trace goes on from there: what came
** after oldT goes, what is kept moves by newT - oldT. A picture across oldT stays, kept whole
** while the replay brings its flip again; numbers are not given twice. */
static void rewindTo(Geo3DPerf* p, UInt64 oldT, UInt64 newT)
{
    UInt64 delta = newT - oldT;
    p->lastT = newT;
    while (p->queueCount > 0 && p->queue[p->queueHead].t1 <= oldT) {
        settleFront(p);
    }
    p->queueHead = 0;
    p->queueCount = 0;

    Geo3DPerfView& v = p->own;
    int n = v.count;
    while (n > 0 && v.pic[n - 1].t1 > oldT) n--;
    bool across = n < v.count && v.pic[n].t0 <= oldT;
    if (across) n++;
    v.count = n;
    for (int i = 0; i < n; i++) {
        v.pic[i].t0 += delta;
        v.pic[i].t1 += delta;
        if (v.pic[i].shownVint != 0) v.pic[i].shownVint += delta;
    }
    if (n > 0) {
        UInt32 from = p->ringHead - v.pic[0].detailFirst > DETAIL_RING ? p->ringHead - DETAIL_RING : v.pic[0].detailFirst;
        for (UInt32 i = from; i != p->ringHead; i++) {
            p->ring[i % DETAIL_RING].s += delta;
            p->ring[i % DETAIL_RING].e += delta;
        }
    }
    p->rwKeepT1 = across ? v.pic[n - 1].t1 : 0;
    p->detGen++;
    p->detNumber = 0;
    v.gen = p->detGen;
    bump(p);

    /* What was open at the break and had begun by oldT was open then too. */
    bool cmd = p->cmdOpen && p->cmdOpenT <= oldT, run = p->runOpen && p->runOpenT <= oldT;
    bool ceWait = p->ceWaitOpen && p->ceWaitT <= oldT, runWait = p->runWaitOpen && p->runWaitT <= oldT;
    bool irq = p->irqOpen && p->irqT <= oldT, halt = p->haltOpen && p->haltT <= oldT;
    p->cmdOpen = cmd;
    p->runOpen = run;
    p->ceWaitOpen = ceWait;
    p->runWaitOpen = runWait;
    p->irqOpen = irq;
    p->haltOpen = halt;
    p->cmdOpenT += delta;
    p->runOpenT += delta;
    p->ceWaitT += delta;
    p->runWaitT += delta;
    p->irqT += delta;
    p->haltT += delta;
    /* The spans and counts before oldT stay with the open picture; what ended after it but began
    ** before was open then. Commands come again with the replay, as they are told at their end. */
    int keep = 0;
    for (int i = 0; i < p->spanCount; i++) {
        Span sp = p->spans[i];
        if (sp.e <= oldT) {
            sp.s += delta;
            sp.e += delta;
            p->spans[keep++] = sp;
            continue;
        }
        if (sp.s > oldT) continue;
        UInt64 t = sp.s + delta;
        switch (sp.kind) {
        case G3P_SPAN_RUN:      p->runOpen = true; p->runOpenT = t; p->runOpenSeq = sp.run; p->runOpenObj = sp.obj; break;
        case G3P_SPAN_WAIT_CE:  p->ceWaitOpen = true; p->ceWaitT = t; break;
        case G3P_SPAN_WAIT_RUN: p->runWaitOpen = true; p->runWaitT = t; break;
        case G3P_SPAN_IRQ:      p->irqOpen = true; p->irqT = t; p->irqPc = sp.arg; break;
        case G3P_SPAN_HALT:     p->haltOpen = true; p->haltT = t; p->haltPc = sp.arg; break;
        }
    }
    p->spanCount = keep;
    p->contSpan = -1;
    p->contCm = 0xFF;
    keep = 0;
    for (int i = 0; i < p->tallyCount; i++) {
        if (p->tallies[i].t > oldT) continue;
        p->tallies[keep] = p->tallies[i];
        p->tallies[keep++].t += delta;
    }
    p->tallyCount = keep;
    for (int i = 0; i < 16; i++) {
        Geo3DPerf::PageWrite& w = p->pageWrites[i];
        if (w.t > oldT) memset(&w, 0, sizeof(w));
        else if (w.t != 0) w.t += delta;
    }

    /* The open picture goes on after the page shown at oldT, so its flip is seen: the one kept
    ** whole, else the one from the last flip kept. Its frames are the vints by oldT since its start. */
    UInt32 openFlags = p->open && p->openT <= oldT ? p->openFlags : G3P_CUT;
    p->haveFrame = false;
    p->open = false;
    p->openFrames = 0;
    p->openFlags = 0;
    p->nextFlags = G3P_GAP;
    int perFrame = n > 0 && (v.pic[n - 1].flags & G3P_NO_FLIP) ? 1 : 0;
    if (perFrame != p->perFrame) {
        p->perFrame = perFrame;
        p->own.perFrame = perFrame;
        bump(p);
    }
    if (n == 0 || (across && n < 2)) {
        return;
    }
    const Geo3DPerfPicture& k = v.pic[n - 1];
    const Geo3DPerfPicture& before = across ? v.pic[n - 2] : k;
    UInt32 period = k.period != 0 ? k.period : p->period;
    if (before.page < 0 || period == 0) {
        return;
    }
    UInt64 start = across ? k.t0 : k.t1, vint = k.t0 + k.vsync0;
    if (vint < start) vint += (start - vint + period - 1) / period * period;
    p->haveFrame = true;
    p->prevT = p->prevPrevT = p->lastT = newT;
    p->rwNoPeriod = true;
    p->prevPage = (UInt32)before.page;
    p->open = true;
    p->openT = start;
    p->openVsync = vint;
    /* A picture opened at a display end counts from the frame after it, as openPicture does. */
    UInt64 first = vint == start ? vint + period : vint;
    p->openFrames = newT >= first ? (UInt32)((newT - first) / period) + 1 : 0;
    /* The one after k starts at the frame after k's last. */
    p->openFrameLo = k.frameLo == 0 ? 0 : across ? k.frameLo : k.frameLo + k.frames;
    p->openFlags = across ? 0 : openFlags;
    p->nextFlags = 0;
}

static void onDiscontinuity(Geo3DPerf* p, const DebugTraceRecord& r)
{
    /* A rewind's break leaves what is open as it is; its resume tells where it went. */
    if (r.a == DEBUG_TRACE_REWIND && r.link == 0) {
        p->rwBreak = true;
        p->rwBreakT = r.time;
        p->rwBreakEpoch = r.b;
        return;
    }
    if (r.a == DEBUG_TRACE_REWIND && p->rwBreak && r.b == p->rwBreakEpoch + 1) {
        UInt32 back = (UInt32)p->rwBreakT - (UInt32)r.time;
        p->rwBreak = false;
        if ((Int32)back >= 0) {
            rewindTo(p, p->rwBreakT - back, r.time);
            return;
        }
    }
    p->rwBreak = false;
    p->rwKeepT1 = 0;
    if (p->open && p->haveFrame) {
        closePicture(p, p->openT, p->prevT, p->openFrames, p->prevPage, p->openFlags | G3P_CUT, 0);
    }
    p->contSpan = -1;
    settle(p, true);
    clearOpen(p);
}

static UInt32 dotsOf(UInt32 cm, UInt32 size)
{
    UInt32 nx = size & 0xFFFF, ny = size >> 16;
    switch (cm) {
    case 0x0: case 0x4: case 0x6: return 0;
    case 0x5: return 1;
    case 0x7: return nx + 1;
    default:  return nx * ny;
    }
}

static void markDropped(Geo3DPerf* p);

static void addTally(Geo3DPerf* p, UInt64 t, UInt32 a, UInt32 b, bool feed)
{
    if (p->tallyCount == TALLY_MAX) {
        markDropped(p);
        return;
    }
    Tally& y = p->tallies[p->tallyCount++];
    y.t = t;
    y.a = a;
    y.b = b;
    y.feed = feed;
}

/* The CPU's interrupt handlers and halts; an END without its BEGIN, from before the trace, is let go. */
static void foldCpu(Geo3DPerf* p, const DebugTraceRecord& r, UInt16 kind, UInt16 phase)
{
    bool irq = kind == CPU_TRACE_IRQ;
    if (kind != CPU_TRACE_IRQ && kind != CPU_TRACE_HALT) return;
    bool& open = irq ? p->irqOpen : p->haltOpen;
    UInt64& t = irq ? p->irqT : p->haltT;
    UInt32& pc = irq ? p->irqPc : p->haltPc;
    if (phase == DEBUG_TRACE_BEGIN) {
        open = true;
        t = r.time;
        pc = r.a;
    }
    else if (phase == DEBUG_TRACE_END && open) {
        Span* sp = addSpan(p, t, r.time, irq ? G3P_SPAN_IRQ : G3P_SPAN_HALT);
        if (sp) sp->arg = pc;
        open = false;
    }
}

static void fold(Geo3DPerf* p, const DebugTraceRecord& r)
{
    UInt16 kind = r.kind & DEBUG_TRACE_KIND_MASK;
    UInt16 phase = r.kind & DEBUG_TRACE_PHASE_MASK;
    if (r.source == 0) {
        if (kind == DEBUG_TRACE_KIND_DISCONTINUITY) onDiscontinuity(p, r);
        return;
    }
    if (r.source == p->srcCmd && kind == VDPCMD_TRACE_CMD) {
        if (phase == DEBUG_TRACE_CONT) {
            if (p->contSpan >= 0) {
                Span& sp = p->spans[p->contSpan];
                sp.nx = (UInt16)r.a;
                sp.ny = (UInt16)(r.a >> 16);
                sp.dx = (UInt16)r.b;
                sp.dy = (UInt16)(r.b >> 16);
                /* An aborted command's time counts, its dots do not. */
                sp.dots = sp.aborted ? 0 : dotsOf(sp.cm, r.a);
            }
            p->contSpan = -1;
            p->contCm = 0xFF;
            return;
        }
        p->contSpan = -1;
        p->contCm = 0xFF;
        if (phase == DEBUG_TRACE_BEGIN) {
            const Link& l = p->links[r.link % LINKS];
            bool known = r.link != 0 && l.link == r.link;
            p->cmdOpen = true;
            p->cmdOpenGeo = r.link != 0;
            p->cmdOpenT = r.time;
            p->cmdOpenCm = (UInt8)(r.a & 0xF);
            p->cmdOpenRun = known ? l.run : 0;
            p->cmdOpenIndex = known ? l.index : 0;
        }
        else if (phase == DEBUG_TRACE_INSTANT) {
            p->cmdOpen = false;
            Span* sp = addSpan(p, r.time, r.time + (r.b + 7) / 8, r.link != 0 ? CAT_GEO : CAT_VDP);
            if (sp) {
                const Link& l = p->links[r.link % LINKS];
                sp->cm = (UInt8)(r.a & 0xF);
                sp->aborted = (r.a & 0x200) != 0;
                if (r.link != 0 && l.link == r.link) {
                    sp->run = l.run;
                    sp->arg = l.index;
                    sp->obj = objOf(p, l.run);
                }
                p->contSpan = (int)(sp - p->spans);
                p->contCm = sp->cm;
            }
        }
        return;
    }
    if (r.source == p->srcCmd && kind == VDPCMD_TRACE_WAIT) {
        if (phase == DEBUG_TRACE_BEGIN) {
            p->ceWaitOpen = true;
            p->ceWaitT = r.time;
        }
        else if (phase == DEBUG_TRACE_END && p->ceWaitOpen) {
            Span* sp = addSpan(p, p->ceWaitT, r.time, G3P_SPAN_WAIT_CE);
            if (sp) {
                sp->arg = r.a;
                sp->cm = (UInt8)(r.b & 0xF);
            }
            p->ceWaitOpen = false;
        }
        return;
    }
    if (r.source == p->srcReg && kind == VDPREG_TRACE_PAGE) {
        Geo3DPerf::PageWrite& w = p->pageWrites[p->pageWriteNext++ % 16];
        w.t = r.time;
        w.now = r.a;
        w.was = r.b;
        return;
    }
    if (r.source == p->srcReg && kind == VDPREG_TRACE_FRAME && phase == DEBUG_TRACE_INSTANT) {
        onFrame(p, r.time, r.a, r.link);
        return;
    }
    if (r.source == p->srcReg && kind == VDPREG_TRACE_CPUVRAM) {
        /* The frame ending now began at prevT: its bytes go where most of it lies. */
        if (p->haveFrame && r.time > p->prevT) addTally(p, p->prevT + (r.time - p->prevT) / 2, r.a, r.b, false);
        return;
    }
    if (r.source == p->srcCpu) {
        foldCpu(p, r, kind, phase);
        return;
    }
    if (r.source != p->srcGeo) {
        return;
    }
    if (kind == GEO3D_TRACE_FEED) {
        addTally(p, r.time, r.a, r.b, true);
        return;
    }
    if (kind == GEO3D_TRACE_ISSUE && r.link != 0) {
        Link& l = p->links[r.link % LINKS];
        l.link = r.link;
        l.run = r.b;
        l.index = r.a;
    }
    else if (kind == GEO3D_TRACE_RUN && phase == DEBUG_TRACE_BEGIN) {
        p->runOpen = true;
        p->runOpenT = r.time;
        p->runOpenSeq = r.a;
        p->runOpenObj = 0;
    }
    else if (kind == GEO3D_TRACE_RUN && phase == DEBUG_TRACE_CONT && p->runOpen) {
        /* The vertex list it reads names the object it draws. */
        Link& l = p->runObjs[p->runOpenSeq % 256];
        p->runOpenObj = r.b != 0 ? r.b : 0xFFFF0000u;
        l.link = p->runOpenSeq;
        l.run = p->runOpenObj;
    }
    else if (kind == GEO3D_TRACE_RUN && phase == DEBUG_TRACE_END && p->runOpen && r.a == p->runOpenSeq) {
        Span* sp = addSpan(p, p->runOpenT, r.time, CAT_RUN);
        if (sp) {
            sp->run = r.a;
            sp->arg = r.b;
            sp->obj = p->runOpenObj;
        }
        p->runOpen = false;
    }
    else if (kind == GEO3D_TRACE_WAIT && phase == DEBUG_TRACE_BEGIN) {
        p->runWaitOpen = true;
        p->runWaitT = r.time;
    }
    else if (kind == GEO3D_TRACE_WAIT && phase == DEBUG_TRACE_END && p->runWaitOpen) {
        Span* sp = addSpan(p, p->runWaitT, r.time, G3P_SPAN_WAIT_RUN);
        if (sp) {
            sp->arg = r.a;
            sp->run = r.b;
        }
        p->runWaitOpen = false;
    }
}

static void markDropped(Geo3DPerf* p)
{
    p->openFlags |= G3P_DROPPED;
    for (int i = 0; i < p->queueCount; i++) p->queue[(p->queueHead + i) % QUEUE_MAX].flags |= G3P_DROPPED;
}

static void readAll(Geo3DPerf* p)
{
    for (int loop = 0; loop < 16; loop++) {
        UInt32 first, dropped;
        int n = debugTraceRead(p->lastSeq, p->buf, READ_MAX, &first, &dropped);
        if (dropped != 0 && p->haveSources) markDropped(p);
        p->lastSeq = first + (UInt32)n - 1;
        if (p->haveSources) {
            for (int i = 0; i < n; i++) fold(p, p->buf[i]);
        }
        if (n < READ_MAX) break;
    }
}

/* Hands over the detail the UI asks for, once its picture has settled; numbers are never
** used twice, so a picture's detail never changes once handed over. */
static void handDetail(Geo3DPerf* p)
{
    UInt32 want = (UInt32)p->detWant;
    const Geo3DPerfView& v = p->own;
    if (want == G3P_DETAIL_LATEST) want = v.count > 0 ? v.pic[v.count - 1].number : 0;
    int state = G3P_DETAIL_WAIT;
    const Geo3DPerfPicture* pic = NULL;
    if (want == 0 || (want == p->detNumber && p->detState != G3P_DETAIL_WAIT)) {
        return;
    }
    int i = geo3dPerfIndexOf(&v, want);
    if (i >= 0) {
        pic = &v.pic[i];
        state = p->ringHead - pic->detailFirst <= DETAIL_RING ? G3P_DETAIL_OK : G3P_DETAIL_GONE;
    }
    else if (v.count == 0 || want <= v.pic[v.count - 1].number) {
        state = G3P_DETAIL_GONE;
    }
    if (state == G3P_DETAIL_WAIT || !TryAcquireSRWLockExclusive(&p->pubLock)) {
        return;
    }
    Geo3DPerfDetail* d = p->pubDet;
    d->number = want;
    d->gen = p->detGen;
    d->state = state;
    d->count = 0;
    if (state == G3P_DETAIL_OK) {
        for (UInt32 i = 0; i < pic->detailCount; i++) d->span[i] = p->ring[(pic->detailFirst + i) % DETAIL_RING];
        d->count = (int)pic->detailCount;
    }
    ReleaseSRWLockExclusive(&p->pubLock);
    p->detNumber = want;
    p->detState = state;
}

int geo3dPerfIndexOf(const Geo3DPerfView* v, UInt32 number)
{
    int lo = 0, hi = v->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (v->pic[mid].number == number) return mid;
        if (v->pic[mid].number < number) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static void drain(void* ref, int point)
{
    Geo3DPerf* p = (Geo3DPerf*)ref;
    if (point == EMU_TAP_STOPPED) {
        /* The emulator thread is gone, maybe killed holding pubLock; its last records are read here. */
        InitializeSRWLock(&p->pubLock);
        readAll(p);
        p->contSpan = -1;
        settle(p, true);
        clearOpen(p);
        publish(p);
        handDetail(p);
        return;
    }
    LONG cfg = p->cfgVersion;
    if (cfg != p->cfgSeen) {
        MemoryBarrier();
        p->cfgSeen = cfg;
        p->srcCmd = (UInt16)(p->cfgVdp & 0xFFFF);
        p->srcReg = (UInt16)((UInt32)p->cfgVdp >> 16);
        p->srcGeo = (UInt16)p->cfgGeo;
        p->srcCpu = (UInt16)p->cfgCpu;
        p->cart = p->cfgCart != 0;
        p->haveSources = true;
        p->rwBreak = false;
        p->rwKeepT1 = 0;
        clearOpen(p);
        p->own.count = 0;
        bump(p);
    }
    /* A shown tap comes right after its VDP's FRAME record, before that frame is copied: a
    ** picture its flip ended is handed over for the copy. Only the VDP read has its tap. */
    if (point == (p->cart ? EMU_TAP_VDP_SHOWN : EMU_TAP_VDP_SECONDARY_SHOWN)) {
        return;
    }
    readAll(p);
    settle(p, false);
    /* Mid-replay it only keeps up with the trace: what the tab shows changes at the stop. */
    if (point == EMU_TAP_REPLAY) {
        return;
    }
    publish(p);
    handDetail(p);
}

static void freePerf(Geo3DPerf* p)
{
    free(p->spans);
    free(p->clips);
    free(p->ring);
    free(p->pubDet);
    free(p);
}

Geo3DPerf* geo3dPerfOpen()
{
    Geo3DPerf* p = (Geo3DPerf*)calloc(1, sizeof(Geo3DPerf));
    if (p == NULL) {
        return NULL;
    }
    p->spans = (Span*)malloc(SPANS_MAX * sizeof(Span));
    p->clips = (Clip*)malloc((SPANS_MAX + 2) * sizeof(Clip));
    p->ring = (Span*)malloc(DETAIL_RING * sizeof(Span));
    p->pubDet = (Geo3DPerfDetail*)calloc(1, sizeof(Geo3DPerfDetail));
    if (p->spans == NULL || p->clips == NULL || p->ring == NULL || p->pubDet == NULL) {
        freePerf(p);
        return NULL;
    }
    InitializeSRWLock(&p->pubLock);
    p->own.version = 1;
    p->contSpan = -1;
    p->contCm = 0xFF;
    p->nextFlags = G3P_GAP;
    p->lastSeq = debugTraceSeq();
    if (!liveFeedAddDrain(drain, p)) {
        freePerf(p);
        return NULL;
    }
    return p;
}

void geo3dPerfClose(Geo3DPerf* p)
{
    if (p == NULL) {
        return;
    }
    liveFeedRemoveDrain(drain, p);
    geo3dPerfArm(p, false);
    freePerf(p);
}

/* The armed fold reverse play asks, on the emulation thread; the lock only keeps it from
** being freed meanwhile, and that thread never waits on it. */
static SRWLOCK    leadLock = SRWLOCK_INIT;
static Geo3DPerf* leadPerf;

/* A reverse play step replays from before the picture on screen at stopAt began, so its
** RUNs and commands come again for the geo3d tab: the latest flip by then, kept or pending. */
static UInt32 rewindLead(UInt32 stopAt)
{
    UInt32 lead = stopAt;
    if (!TryAcquireSRWLockShared(&leadLock)) {
        return lead;
    }
    const Geo3DPerf* p = leadPerf;
    if (p != NULL && p->period != 0) {
        bool   found = false;
        UInt32 t0 = 0, t1 = 0;
        auto consider = [&](UInt64 s, UInt64 e) {
            if ((Int32)((UInt32)e - stopAt) <= 0 && (!found || (Int32)((UInt32)e - t1) > 0)) {
                found = true;
                t0 = (UInt32)s;
                t1 = (UInt32)e;
            }
        };
        for (int i = p->own.count - 1; i >= 0; i--) {
            const Geo3DPerfPicture& q = p->own.pic[i];
            if ((Int32)((UInt32)q.t1 - stopAt) <= 0) {
                consider(q.t0, q.t1);
                break;
            }
        }
        for (int i = 0; i < p->queueCount; i++) {
            const Pending& q = p->queue[(p->queueHead + i) % QUEUE_MAX];
            consider(q.t0, q.t1);
        }
        /* Not past the second the step allows: a picture begun longer ago would not come
        ** whole anyway, and the replay would only be slower. */
        UInt32 back = stopAt - (t0 - p->period);
        if (found && (Int32)back > 0 && back <= std::min((UInt32)(LEAD_FRAMES * p->period), (UInt32)boardFrequency())) {
            lead = t0 - p->period;
        }
    }
    ReleaseSRWLockShared(&leadLock);
    return lead;
}

void geo3dPerfArm(Geo3DPerf* p, bool on)
{
    if (p == NULL || p->armed == on) {
        return;
    }
    p->armed = on;
    debugTraceArm(DEBUG_TRACE_BIT(DEBUG_TRACE_GEO3D) | DEBUG_TRACE_BIT(DEBUG_TRACE_VDPCMD) |
                  DEBUG_TRACE_BIT(DEBUG_TRACE_VDPREG) | DEBUG_TRACE_BIT(DEBUG_TRACE_CPU), on ? 1 : 0);
    AcquireSRWLockExclusive(&leadLock);
    leadPerf = on ? p : NULL;
    ReleaseSRWLockExclusive(&leadLock);
    emulatorSetRewindLead(on ? rewindLead : NULL);
}

bool geo3dPerfArmed(const Geo3DPerf* p)
{
    return p != NULL && p->armed;
}

void geo3dPerfSources(Geo3DPerf* p, UInt16 vdpCmd, UInt16 vdpReg, UInt16 geo3d, UInt16 cpu, bool cartridge)
{
    LONG vdp = (LONG)((UInt32)vdpCmd | ((UInt32)vdpReg << 16));
    if (p == NULL || (p->cfgVersion != 0 && p->cfgVdp == vdp && p->cfgGeo == (LONG)geo3d && p->cfgCpu == (LONG)cpu &&
                      p->cfgCart == (cartridge ? 1 : 0))) {
        return;
    }
    p->cfgVdp = vdp;
    p->cfgGeo = geo3d;
    p->cfgCpu = cpu;
    p->cfgCart = cartridge ? 1 : 0;
    MemoryBarrier();
    InterlockedIncrement(&p->cfgVersion);
}

bool geo3dPerfTake(Geo3DPerf* p, Geo3DPerfView* out)
{
    bool taken = false;
    if (p == NULL || !TryAcquireSRWLockShared(&p->pubLock)) {
        return false;
    }
    if (p->pub.version != out->version) {
        size_t head = offsetof(Geo3DPerfView, pic);
        memcpy(out, &p->pub, head + (size_t)p->pub.count * sizeof(Geo3DPerfPicture));
        taken = true;
    }
    out->frameT = p->pub.frameT;
    ReleaseSRWLockShared(&p->pubLock);
    return taken;
}

void geo3dPerfWantDetail(Geo3DPerf* p, UInt32 number)
{
    if (p != NULL) InterlockedExchange(&p->detWant, (LONG)number);
}

bool geo3dPerfTakeDetail(Geo3DPerf* p, Geo3DPerfDetail* out)
{
    bool taken = false;
    if (p == NULL || !TryAcquireSRWLockShared(&p->pubLock)) {
        return false;
    }
    const Geo3DPerfDetail* d = p->pubDet;
    UInt32 want = (UInt32)p->detWant;
    if ((d->number == want || want == G3P_DETAIL_LATEST) && d->number != 0 &&
        (d->number != out->number || d->gen != out->gen || d->state != out->state)) {
        size_t head = offsetof(Geo3DPerfDetail, span);
        memcpy(out, d, head + (size_t)d->count * sizeof(Geo3DPerfSpan));
        taken = true;
    }
    ReleaseSRWLockShared(&p->pubLock);
    return taken;
}
