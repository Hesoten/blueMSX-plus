/*****************************************************************************
**
** Breakpoint conditions: the breakpoints an agent sets, with their conditions' texts.
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
#include "ControlBreakCond.h"
#include "ControlAddress.h"
#include "ControlProtocol.h"
#include <string.h>

/* Room for a when for each segment breakpoint and each plain one with a record. */
#define WHEN_TEXTS_MAX 128

/* The text each condition was compiled from, which only the agent's side keeps. */
typedef struct {
    int             used;
    CtrlBreakCondAt at;
    char            when[CTRL_BREAK_WHEN_MAX + 1];
} WhenText;

static WhenText whenTexts[WHEN_TEXTS_MAX];

static int sameAt(const CtrlBreakCondAt* a, const CtrlBreakCondAt* b)
{
    return a->segment == b->segment && a->at == b->at &&
           (a->segment < 0 || (a->slot == b->slot && a->subSlot == b->subSlot));
}

static WhenText* findWhen(const CtrlBreakCondAt* at)
{
    int i;

    for (i = 0; i < WHEN_TEXTS_MAX; i++) {
        if (whenTexts[i].used && sameAt(&whenTexts[i].at, at)) {
            return &whenTexts[i];
        }
    }
    return NULL;
}

/* The counts of the breakpoint's record, if it still has one; a plugin may have cleared a
** segment one. */
static int countsOf(const CtrlBreakCondAt* at, DbgBreakCounts* counts)
{
    if (at->segment < 0) {
        return dbgGetKeptBreakpointCounts((UInt16)at->at, counts);
    }
    return dbgGetSegmentBreakpointCounts(dbgFindSegmentBreakpoint(at->slot, at->subSlot,
                                                                  at->segment, (int)at->at),
                                         counts);
}

static void keepWhen(const CtrlBreakCondAt* at, const char* when)
{
    WhenText* kept = findWhen(at);
    DbgBreakCounts counts;
    int i;

    if (when == NULL) {
        if (kept != NULL) {
            kept->used = 0;
        }
        return;
    }
    for (i = 0; i < WHEN_TEXTS_MAX && kept == NULL; i++) {
        if (!whenTexts[i].used) {
            kept = &whenTexts[i];
        }
    }
    /* Full only of ones left behind by breakpoints cleared elsewhere. */
    for (i = 0; i < WHEN_TEXTS_MAX && kept == NULL; i++) {
        if (!countsOf(&whenTexts[i].at, &counts)) {
            kept = &whenTexts[i];
        }
    }
    if (kept != NULL) {
        kept->used = 1;
        kept->at   = *at;
        strncpy(kept->when, when, CTRL_BREAK_WHEN_MAX);
        kept->when[CTRL_BREAK_WHEN_MAX] = 0;
    }
}

void ctrlBreakCondClearPlain(void)
{
    int i;

    dbgClearKeptBreakpoints();
    for (i = 0; i < WHEN_TEXTS_MAX; i++) {
        if (whenTexts[i].at.segment < 0) {
            whenTexts[i].used = 0;
        }
    }
}

int ctrlBreakCondSet(const CtrlBreakCondAt* at, const DbgBreakCondition* cond, const char* when)
{
    int set;

    if (at->segment >= 0) {
        set = dbgSetSegmentBreakpointIf(at->slot, at->subSlot, at->segment, (int)at->at, cond);
    }
    else {
        set = dbgSetKeptBreakpoint((UInt16)at->at, cond);
    }
    if (set) {
        keepWhen(at, when);
    }
    return set;
}

int ctrlBreakCondClear(const CtrlBreakCondAt* at)
{
    keepWhen(at, NULL);
    if (at->segment >= 0) {
        return dbgClearSegmentBreakpoint(at->slot, at->subSlot, at->segment, (int)at->at);
    }
    return dbgClearKeptBreakpoint((UInt16)at->at);
}

int ctrlBreakCondOthersStop(const CtrlBreakCondAt* at)
{
    if (at->segment < 0) {
        return dbgOtherBreakpointAt((UInt16)at->at);
    }
    return dbgSegmentBreakpointAlwaysStops(at->slot, at->subSlot, at->segment, (int)at->at);
}

void ctrlBreakCondClearAll(void)
{
    dbgClearAllSegmentBreakpoints();
    dbgClearKeptBreakpoints();
    memset(whenTexts, 0, sizeof(whenTexts));
}

static void describeAt(CtrlText* text, const CtrlBreakCondAt* at)
{
    char printed[64];

    if (at->segment < 0) {
        ctrlTextAdd(text, "0x%04X", (unsigned)at->at);
        return;
    }
    ctrlAddrPrintSegment(printed, sizeof(printed), 1, at->slot, at->subSlot, at->segment, at->at);
    ctrlTextAdd(text, "%s", printed);
}

void ctrlBreakCondDescribeCounts(CtrlText* text, const CtrlBreakCondAt* at,
                                 const DbgBreakCounts* counts)
{
    const WhenText* kept = findWhen(at);

    if (kept != NULL) {
        ctrlTextAdd(text, "  when %s", kept->when);
    }
    ctrlTextAdd(text, "  hits %u  matched %u  skip %u", (unsigned)counts->hits,
                (unsigned)counts->matched, (unsigned)counts->skip);
}

void ctrlBreakCondDescribeStop(CtrlText* text, JsonOut* out, const CtrlStop* stop)
{
    const DbgBreakHit* hit = &stop->breakHit;
    const WhenText* kept;
    CtrlBreakCondAt at;
    DbgBreakCounts counts;

    if ((stop->reason != CTRL_STOP_BREAKPOINT && stop->reason != CTRL_STOP_STEP) ||
        !hit->counted)
    {
        return;
    }
    at.slot    = hit->slot;
    at.subSlot = hit->subSlot;
    at.segment = hit->segment;
    at.at      = hit->segment < 0 ? hit->address : (UInt32)hit->offset;
    /* Its when only while the condition is the one the stop counted, not a later set's. */
    kept = countsOf(&at, &counts) && counts.stamp == hit->stamp ? findWhen(&at) : NULL;

    ctrlTextAdd(text, "%sbreakpoint at ", stop->reason == CTRL_STOP_STEP ? "the step ended at the "
                                                                         : "");
    describeAt(text, &at);
    if (kept != NULL) {
        ctrlTextAdd(text, " when %s", kept->when);
    }
    ctrlTextAdd(text, ": reached %u time%s, condition true %u time%s\n", (unsigned)hit->hits,
                hit->hits == 1 ? "" : "s", (unsigned)hit->matched, hit->matched == 1 ? "" : "s");
    jsonOutUInt(out, "hits", hit->hits);
    jsonOutUInt(out, "matched", hit->matched);
}
