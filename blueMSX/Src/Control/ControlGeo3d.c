/*****************************************************************************
**
** Text listings of a geo3d's RUNs, faces and edges.
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
#include "ControlGeo3d.h"
#include "Geo3DDevice.h"
#include "Geo3D.h"
#include "Board.h"
#include <stdio.h>
#include <string.h>

/* Too big for the stack; ops run one at a time on the agent thread, under its view. */
static Geo3DDebugState state;
static Geo3DDebugRun   oneRun;
static Geo3DDebugItem  runItems[GEO3D_DEBUG_ITEMS];
static Geo3DDebugCmd   runCmds[GEO3D_DEBUG_CORE_CMDS];

static const char* const outcomeNames[] = { "DRAWN", "NEAR", "BACK", "OFFSCREEN", "UNCUT" };
typedef char OutcomeNamesFit[sizeof(outcomeNames) / sizeof(outcomeNames[0]) == GEO3D_ITEM_UNCUT + 1 ? 1 : -1];

static const char* const cfgNames[18] = {
    "M00", "M01", "M02", "M10", "M11", "M12", "M20", "M21", "M22",
    "TX", "TY", "TZ", "F", "CX", "CY", "ZNEAR", "W", "H"
};
#define CFG_ZNEAR 15

static const char* outcomeName(int outcome)
{
    return outcome >= 0 && outcome <= GEO3D_ITEM_UNCUT ? outcomeNames[outcome] : "?";
}

/* Board ticks as microseconds. */
static unsigned ticksUs(UInt32 ticks)
{
    return (unsigned)((UInt64)ticks * 1000000 / boardFrequency());
}

/* The engine counts 2 clocks to a board tick. */
static unsigned clocksUs(UInt32 clocks)
{
    return ticksUs(clocks / 2);
}

static int capture(int cartridge, char* err, int errLen)
{
    if (!geo3dDebugCaptureAll(cartridge, &state)) {
        snprintf(err, (size_t)errLen, "the %s has no geo3d%s", cartridge ? "cartridge" : "machine",
                 geo3dDebugCaptureAll(!cartridge, &state) ?
                     (cartridge ? "; the machine's V9968 has one (chip=machine)"
                                : "; the V9968 cartridge has one (chip=cartridge)") : "");
        return CTRL_GEO3D_NONE;
    }
    return CTRL_GEO3D_OK;
}

static void describeTime(CtrlText* text, const Geo3DDebugRun* r, UInt32 now)
{
    if (!r->ended) {
        ctrlTextAdd(text, "running for %u us", ticksUs(now - r->startTime));
        return;
    }
    ctrlTextAdd(text, "busy %u us, transform %u us", ticksUs(r->endTime - r->startTime),
                clocksUs(r->xfEndClock));
}

static void listCfg(CtrlText* text, const Int32* cfg, const Int32* than)
{
    int i;
    int listed = 0;

    for (i = 0; i < 18; i++) {
        if (than == NULL || cfg[i] != than[i]) {
            ctrlTextAdd(text, "%s%s=%d", listed++ ? " " : "", cfgNames[i], (int)cfg[i]);
        }
    }
    if (listed == 0) {
        ctrlTextAdd(text, "none");
    }
}

int ctrlGeo3dSummary(CtrlText* text, int cartridge, char* err, int errLen)
{
    int result = capture(cartridge, err, errLen);
    int i;

    if (result != CTRL_GEO3D_OK) {
        return result;
    }
    ctrlTextAdd(text, "geo3d:   %s, ports %02Xh/%02Xh; RUNs numbered since reset or a state load, "
                "epoch %u\n", cartridge ? "cartridge" : "machine", state.port, state.port + 2,
                (unsigned)state.epoch);
    if (state.runCount == 0) {
        ctrlTextAdd(text, "runs:    no RUN kept\n");
        return CTRL_GEO3D_OK;
    }
    ctrlTextAdd(text, "runs:    latest %u; kept %u-%u (the device keeps the last %d)\n",
                (unsigned)state.runSeq, (unsigned)state.run[0].seq,
                (unsigned)state.run[state.runCount - 1].seq, GEO3D_DEBUG_RUNS);
    if (state.running) {
        ctrlTextAdd(text, "state:   RUN %u running: %d of %d commands issued%s\n",
                    (unsigned)state.runSeq, state.cmdsIssued, state.cmdsTotal,
                    state.devPhase == GEO3D_PHASE_WAIT_CE ?
                        "; waiting for the VDP to finish one (the device's own view, up to 16 "
                        "ticks behind the VDP)" : "");
    }
    else {
        ctrlTextAdd(text, "state:   idle\n");
    }
    if (state.fallback || state.frameFirst >= state.runCount) {
        ctrlTextAdd(text, "frame:   no RUN busy in the VDP frame so far");
    }
    else {
        ctrlTextAdd(text, "frame:   RUNs %u-%u busy in the VDP frame so far",
                    (unsigned)state.run[state.frameFirst].seq,
                    (unsigned)state.run[state.runCount - 1].seq);
    }
    ctrlTextAdd(text, state.runsLost ? "; %d more no longer kept\n" : "\n", state.runsLost);

    ctrlTextAdd(text, "RUN    kind        items  drawn skipped culled   cmds  ypage  time\n");
    for (i = 0; i < state.runCount; i++) {
        const Geo3DDebugRun* r = &state.run[i];

        ctrlTextAdd(text, "%5u  %-5s %-4s  %5u  %5u %7u %6u %6u  %5u  ", (unsigned)r->seq,
                    r->faces ? "faces" : "edges", r->tex ? "tex" : "", (unsigned)r->count,
                    (unsigned)r->drawn, (unsigned)r->skipped, (unsigned)r->culled,
                    (unsigned)r->cmdTotal, (unsigned)r->ypage);
        describeTime(text, r, state.windowEnd);
        ctrlTextAdd(text, "\n");
    }
    ctrlTextAdd(text, "config:  now, where it differs from the latest RUN's: ");
    listCfg(text, state.cfg, state.run[state.runCount - 1].cfg);
    ctrlTextAdd(text, "\n");
    return CTRL_GEO3D_OK;
}

/* Faces and edges are numbered from FFIRST or EFIRST on, wrapping at 1024. */
static int inRun(int index)
{
    return ((index - (int)oneRun.first) & 1023) < (int)oneRun.count;
}

static int lastInRun(void)
{
    return (int)((oneRun.first + oneRun.count - 1) & 1023);
}

/* "faces 240-479", "edges 1000-1023 and 0-75" or "no faces". */
static void describeNumbers(char* out, int len)
{
    const char* kind = oneRun.faces ? "faces" : "edges";

    if (oneRun.count == 0) {
        snprintf(out, (size_t)len, "no %s", kind);
    }
    else if (lastInRun() >= (int)oneRun.first) {
        snprintf(out, (size_t)len, "%s %u-%d", kind, (unsigned)oneRun.first, lastInRun());
    }
    else {
        snprintf(out, (size_t)len, "%s %u-1023 and 0-%d", kind, (unsigned)oneRun.first, lastInRun());
    }
}

/* RUN seq copied whole into oneRun, runItems and runCmds; seq 0 is the latest. */
static int captureRun(int cartridge, UInt32* seq, char* err, int errLen)
{
    int result = capture(cartridge, err, errLen);

    if (result != CTRL_GEO3D_OK) {
        return result;
    }
    if (*seq == 0) {
        *seq = state.runSeq;
    }
    if (*seq == 0 || *seq > state.runSeq) {
        snprintf(err, (size_t)errLen, state.runSeq == 0 ? "there has been no RUN since reset or a "
                 "state load" : "there is no RUN %u yet; the latest is %u",
                 (unsigned)*seq, (unsigned)state.runSeq);
        return CTRL_GEO3D_NO_RUN;
    }
    if (!geo3dDebugCaptureRun(cartridge, *seq, &oneRun, runItems, GEO3D_DEBUG_ITEMS,
                              runCmds, GEO3D_DEBUG_CORE_CMDS)) {
        snprintf(err, (size_t)errLen, "RUN %u is no longer kept; kept: %u-%u (the device keeps "
                 "the last %d)", (unsigned)*seq,
                 (unsigned)(state.runCount > 0 ? state.run[0].seq : 0),
                 (unsigned)state.runSeq, GEO3D_DEBUG_RUNS);
        return CTRL_GEO3D_NOT_KEPT;
    }
    return CTRL_GEO3D_OK;
}

static void describeRunHead(CtrlText* text, UInt32 seq)
{
    const Geo3DDebugRun* r = &oneRun;

    ctrlTextAdd(text, "RUN %u (epoch %u): %s%s; %s %u, %s %u; VFIRST %u, NVERT %u; YPAGE %u, "
                "COLOR %02X, LOP %02X\n", (unsigned)seq, (unsigned)state.epoch,
                r->faces ? "faces" : "edges", r->tex ? ", textured" : "",
                r->faces ? "FFIRST" : "EFIRST", (unsigned)r->first,
                r->faces ? "NFACE" : "NEDGE", (unsigned)r->count, (unsigned)r->vfirst,
                (unsigned)r->nvert, (unsigned)r->ypage, r->color, r->lop);
    ctrlTextAdd(text, "time:    ");
    describeTime(text, r, state.windowEnd);
    ctrlTextAdd(text, "\ncounts:  drawn %u, skipped %u (NEAR, UNCUT), culled %u (BACK, OFFSCREEN); "
                "%u commands\n", (unsigned)r->drawn, (unsigned)r->skipped, (unsigned)r->culled,
                (unsigned)r->cmdTotal);
    ctrlTextAdd(text, "config:  ");
    listCfg(text, r->cfg, NULL);
    ctrlTextAdd(text, "\n");
    if (r->faces) {
        ctrlTextAdd(text, "light:   %d %d %d (model space)\n", (int)r->lm[0], (int)r->lm[1],
                    (int)r->lm[2]);
    }
    if (r->tex) {
        ctrlTextAdd(text, "texture: TEXX %u, TEXY %u, TSTRIDE %u\n", (unsigned)r->texx,
                    (unsigned)r->texy, (unsigned)r->tstride);
    }
}

int ctrlGeo3dRun(CtrlText* text, int cartridge, UInt32 seq, int first, int last,
                 char* err, int errLen)
{
    int result = captureRun(cartridge, &seq, err, errLen);
    int corners = oneRun.faces ? 4 : 2;
    int shown = 0;
    int i;

    if (result != CTRL_GEO3D_OK) {
        return result;
    }
    describeRunHead(text, seq);
    if (oneRun.itemCount < oneRun.itemTotal) {
        ctrlTextAdd(text, "kept:    %u of the RUN's %u faces or edges\n",
                    (unsigned)oneRun.itemCount, (unsigned)oneRun.itemTotal);
    }
    ctrlTextAdd(text, " %s  outcome    lvl order  vertex(sx,sy)%s  rows      cmds\n",
                oneRun.faces ? "face" : "edge", oneRun.faces ? " x4" : " x2");
    for (i = 0; i < oneRun.itemCount; i++) {
        const Geo3DDebugItem* it = &runItems[i];
        int c;

        if (it->index < first || (last >= 0 && it->index > last)) {
            continue;
        }
        if (last < 0 && shown == CTRL_GEO3D_ITEMS_SHOWN) {
            int end = lastInRun() >= (int)it->index ? lastInRun() : 1023;

            ctrlTextAdd(text, "(more: ask for item=%u-%d)\n", (unsigned)it->index, end);
            break;
        }
        shown++;
        ctrlTextAdd(text, "%5u  %-9s  ", (unsigned)it->index, outcomeName(it->outcome));
        if (oneRun.faces && it->outcome == GEO3D_ITEM_DRAWN) {
            ctrlTextAdd(text, "%3u %5u ", (unsigned)it->lvl, (unsigned)it->order);
        }
        else {
            ctrlTextAdd(text, "  -     - ");
        }
        for (c = 0; c < corners; c++) {
            ctrlTextAdd(text, " %u(%d,%d)", (unsigned)it->v[c], it->sx[c], it->sy[c]);
        }
        if (it->ymin <= it->ymax) {
            ctrlTextAdd(text, "  %d-%d", it->ymin, it->ymax);
        }
        else {
            ctrlTextAdd(text, "  none");
        }
        ctrlTextAdd(text, "  %u\n", (unsigned)it->cmdTotal);
    }
    if (shown == 0) {
        char numbers[48];

        describeNumbers(numbers, sizeof(numbers));
        ctrlTextAdd(text, "(none in that range; the RUN has %s)\n", numbers);
    }
    return CTRL_GEO3D_OK;
}

int ctrlGeo3dItem(CtrlText* text, int cartridge, UInt32 seq, int index, char* err, int errLen)
{
    int result = captureRun(cartridge, &seq, err, errLen);
    const Geo3DDebugItem* it = NULL;
    int corners = oneRun.faces ? 4 : 2;
    int i;

    if (result != CTRL_GEO3D_OK) {
        return result;
    }
    for (i = 0; i < oneRun.itemCount && it == NULL; i++) {
        if (runItems[i].index == index) {
            it = &runItems[i];
        }
    }
    if (it == NULL) {
        char numbers[48];

        describeNumbers(numbers, sizeof(numbers));
        snprintf(err, (size_t)errLen, inRun(index) ? "RUN %u has %s, but %d was not kept" :
                 "RUN %u has %s, not %d", (unsigned)seq, numbers, index);
        return CTRL_GEO3D_NO_RUN;
    }
    ctrlTextAdd(text, "RUN %u (epoch %u), %s %d: %s%s", (unsigned)seq, (unsigned)state.epoch,
                oneRun.faces ? "face" : "edge", index, outcomeName(it->outcome),
                it->textured ? ", textured" : "");
    if (it->outcome == GEO3D_ITEM_DRAWN) {
        ctrlTextAdd(text, "; COL %02X", it->col);
    }
    if (oneRun.faces && it->outcome == GEO3D_ITEM_DRAWN) {
        ctrlTextAdd(text, "; light level %u, painted %u", (unsigned)it->lvl, (unsigned)it->order);
    }
    ctrlTextAdd(text, "\nvertex     sx     sy    camera x    camera z  (ZNEAR %d)\n",
                (int)oneRun.cfg[CFG_ZNEAR]);
    for (i = 0; i < corners; i++) {
        ctrlTextAdd(text, "%6u %6d %6d  %10d  %10d\n", (unsigned)it->v[i], it->sx[i], it->sy[i],
                    (int)it->cx[i], (int)it->cz[i]);
    }
    if (it->ymin <= it->ymax) {
        ctrlTextAdd(text, "rows:    %d-%d\n", it->ymin, it->ymax);
    }
    else {
        ctrlTextAdd(text, "rows:    none\n");
    }
    ctrlTextAdd(text, "commands: %u kept of %u%s\n", (unsigned)it->cmdCount, (unsigned)it->cmdTotal,
                it->cmdCount < it->cmdTotal ? " (the latest are kept)" : "");
    for (i = 0; i < it->cmdCount; i++) {
        const Geo3DDebugCmd* c = &runCmds[it->cmdFirst + i];
        int b;

        ctrlTextAdd(text, "  %s", c->size == 11 ? "LINE R#36-46:" :
                                  c->size == 19 ? "LRMM R#32-45,47-50,46:" : "bytes:");
        for (b = 0; b < c->size && b < (int)sizeof(c->bytes); b++) {
            ctrlTextAdd(text, " %02X", c->bytes[b]);
        }
        ctrlTextAdd(text, "\n");
    }
    ctrlTextAdd(text, "as at the RUN: the model RAMs may have changed since\n");
    return CTRL_GEO3D_OK;
}
