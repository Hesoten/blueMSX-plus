/*****************************************************************************
**
** geo3d tab and cards of the VDP Viewer: drawing by a geo3d coprocessor.
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
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiTheme.h"
#include "Geo3DPanel.h"
#include "Geo3D.h"
#include "VdpCmd9968.h"
#include "VdpParts.h"
#include "ToolText.h"
extern "C" {
#include "Board.h"
#include "Emulator.h"
#include "Properties.h"
#include "Geo3DDevice.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

namespace {

/* Alphas of the picture's faces, for the legend too: a drawn face's fill, the outlines,
** and the fill of the face under the mouse or pinned. */
const int faceFillAlpha = 40, faceLineAlpha = 150, litFillAlpha = 110;
/* Faces are white in either theme, as they lie over the MSX picture; the legend shows them on black. */
const ImU32 faceColour = IM_COL32(255, 255, 255, 255), faceBackdrop = IM_COL32(0, 0, 0, 255);

/* A cube seen from above a corner. */
const char* const cubeRows[] = {
    ".....#.....",
    "...##.##...",
    ".##.....##.",
    "#.........#",
    "###.....###",
    "#..##.##..#",
    "#....#....#",
    "#....#....#",
    "#....#....#",
    ".##..#..##.",
    "...#####...",
    ".....#.....",
};
const ui::PixelArt cubeArt = { 11, 12, cubeRows, NULL };

/* A mesh of triangles, for the outlines. */
const char* const wireRows[] = {
    "###########",
    "##...##...#",
    "#.#..#.#..#",
    "#..#.#..#.#",
    "#...##...##",
    "###########",
    "##...##...#",
    "#.#..#.#..#",
    "#..#.#..#.#",
    "#...##...##",
    "###########",
};
const ui::PixelArt wireArt = { 11, 11, wireRows, NULL };

/* Readable through the data port; the configuration words and the RAM ports are write only. */
const RegField projFlags[] = {
    { 4, 4, "OFF",  "Off the screen: bit 4 left, 5 right, 6 top, 7 bottom" },
    { 1, 2, "SAT",  "Saturated" },
    { 0, 1, "NEAR", "Behind the near plane" },
};
const RegField statusBits[] = {
    { 4, 4, "OFF",   "The immediate vertex's off-screen bits (36h bits 4-7)" },
    { 2, 1, "DRAW",  "Edge or face phase, until the last command is built" },
    { 1, 1, "XFORM", "Transform phase" },
    { 0, 1, "RUN",   "RUN busy" },
};
const RegField lopBits[] = { { 0, 4, "LO", "Logical operation of the commands" } };
const RegField xctrlBits[] = {
    { 4, 4, "AW",  "Address bits of the model RAMs" },
    { 0, 1, "EXT", "Extended mode: 10-bit write pointers and indices" },
};
const RegDesc geo3dRegs[] = {
    { 0x30, "SX low (immediate)", 0, 0 },
    { 0x31, "SX high", 0, 0 },
    { 0x32, "SY low", 0, 0 },
    { 0x33, "SY high", 0, 0 },
    { 0x34, "Z low", 0, 0 },
    { 0x35, "Z high", 0, 0 },
    { 0x36, "Flags", projFlags, N(projFlags) },
    { 0x40, "VADDR low", 0, 0 },
    { 0x41, "EADDR low", 0, 0 },
    { 0x42, "NVERT low", 0, 0 },
    { 0x43, "NEDGE low", 0, 0 },
    { 0x44, "Colour", 0, 0 },
    { 0x45, "Logical operation", lopBits, N(lopBits) },
    { 0x46, "YPAGE low", 0, 0 },
    { 0x47, "YPAGE high", 0, 0 },
    { 0x48, "Status", statusBits, N(statusBits) },
    { 0x49, "ID", 0, 0 },
    { 0x4A, "SKIPPED low", 0, 0 },
    { 0x4B, "SKIPPED high", 0, 0 },
    { 0x4C, "DRAWN low", 0, 0 },
    { 0x4D, "DRAWN high", 0, 0 },
    { 0x4E, "CULLED low", 0, 0 },
    { 0x4F, "CULLED high", 0, 0 },
    { 0x54, "VFIRST low", 0, 0 },
    { 0x55, "VFIRST high", 0, 0 },
    { 0x56, "FFIRST low", 0, 0 },
    { 0x57, "FFIRST high", 0, 0 },
    { 0x58, "FADDR low", 0, 0 },
    { 0x59, "NFACE low", 0, 0 },
    { 0x65, "TADDR low", 0, 0 },
    { 0x66, "EFIRST low", 0, 0 },
    { 0x67, "EFIRST high", 0, 0 },
    { 0x68, "VADDR high", 0, 0 },
    { 0x69, "EADDR high", 0, 0 },
    { 0x6A, "NVERT high", 0, 0 },
    { 0x6B, "NEDGE high", 0, 0 },
    { 0x6C, "FADDR high", 0, 0 },
    { 0x6D, "NFACE high", 0, 0 },
    { 0x6E, "TADDR high", 0, 0 },
    { 0x6F, "XCTRL", xctrlBits, N(xctrlBits) },
};
const RegBank regBankGeo3D = { "g3dregs", "", geo3dRegs, N(geo3dRegs), true };

/* The mode of the latest RUN kept, else CTRL as last written. */
bool facesMode(const Geo3DDebugState& g)
{
    return g.runCount > 0 ? g.run[g.runCount - 1].faces != 0 : g.faces != 0;
}

const char* modeName(const Geo3DDebugState& g)
{
    bool tex = g.runCount > 0 ? g.run[g.runCount - 1].tex != 0 : g.tex != 0;
    if (!facesMode(g)) return ts(TS_G3D_MODE_EDGES);
    return ts(tex ? TS_G3D_MODE_FACES_TEX : TS_G3D_MODE_FACES);
}

long long floorDiv(long long a, long long b)
{
    long long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* For a face whose commands were cut: the dots of row y they fill, worked out from its
** projections as geo3d does, w its W; false for none. */
bool faceSpan(const Geo3DDebugItem& it, int y, int w, int* l, int* r)
{
    bool have = false;
    long long xl = 0, xr = 0;
    for (int k = 0; k < 4; k++) {
        long long xa = it.sx[k], ya = it.sy[k], xb = it.sx[(k + 1) & 3], yb = it.sy[(k + 1) & 3];
        long long c[2];
        int n = 0;
        if (ya == yb) {
            if (y == ya) {
                c[n++] = xa;
                c[n++] = xb;
            }
        }
        else if (std::min(ya, yb) <= y && y <= std::max(ya, yb)) {
            c[n++] = xa + floorDiv((y - ya) * (xb - xa), yb - ya);
        }
        for (int i = 0; i < n; i++) {
            if (!have || c[i] < xl) xl = c[i];
            if (!have || c[i] > xr) xr = c[i];
            have = true;
        }
    }
    long long cl = std::max(xl, 0LL), cr = std::min(xr, (long long)w - 1);
    if (!have || cl > cr) return false;
    *l = (int)cl;
    *r = (int)cr;
    return true;
}

/* A face's fill command: its row from the RUN's YPAGE on, DY itself, and dots x0..x1.
** LINE (R#36-46) draws NX + 1 dots; LRMM (R#32-45 first) NX dots. */
bool commandSpan(const Geo3DDebugCmd& c, int ypage, int* y, int* dy, int* x0, int* x1)
{
    const UInt8* b = c.bytes;
    int at = c.size == 19 ? 4 : 0;
    if (c.size != 19 && c.size != 11) return false;
    int nx = b[at + 4] | ((b[at + 5] & 7) << 8);
    *x0 = b[at] | ((b[at + 1] & 1) << 8);
    *dy = b[at + 2] | ((b[at + 3] & 7) << 8);
    *x1 = *x0 + (c.size == 19 ? nx - 1 : nx);
    *y = (*dy - ypage) & 0x7ff;
    return true;
}

/* An edge's LINE: its two ends, from the RUN's YPAGE on, as the engine draws them. */
bool lineEnds(const Geo3DDebugCmd& c, int ypage, int* xa, int* ya, int* xb, int* yb)
{
    const UInt8* b = c.bytes;
    if (c.size != 11) return false;
    int nx = b[4] | ((b[5] & 7) << 8), ny = b[6] | ((b[7] & 7) << 8), arg = b[9];
    int mx = (arg & 1) ? ny : nx, my = (arg & 1) ? nx : ny;
    *xa = b[0] | ((b[1] & 1) << 8);
    *ya = ((b[2] | ((b[3] & 7) << 8)) - ypage) & 0x7ff;
    *xb = *xa + ((arg & 4) ? -mx : mx);
    *yb = *ya + ((arg & 8) ? -my : my);
    return true;
}

/* The picture shows the item: a drawn face or edge, or a face turned away. */
bool onPicture(const Geo3DDebugRun& run, const Geo3DDebugItem& it)
{
    return it.outcome == GEO3D_ITEM_DRAWN || (run.faces && it.outcome == GEO3D_ITEM_BACK);
}

/* All of an item's commands are in the snapshot. */
bool complete(const Geo3DDebugItem& it)
{
    return it.cmdCount >= it.cmdTotal;
}

/* Screen px, at sc screen px a dot, from the middle of dot x, y to the segment between
** the middles of two dots. */
float segmentDistance(ImVec2 sc, int x, int y, int ax, int ay, int bx, int by)
{
    float dx = (bx - ax) * sc.x, dy = (by - ay) * sc.y, px = (x - ax) * sc.x, py = (y - ay) * sc.y;
    float len = dx * dx + dy * dy;
    float t = len > 0 ? (px * dx + py * dy) / len : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = t * dx - px, ey = t * dy - py;
    return sqrtf(ex * ex + ey * ey);
}

/* Dashes dash px on and off from a to b at any angle; only those within the clip rect,
** counted from a so they hold still as the view scrolls. */
void dashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float dash)
{
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    if (len < 1 || dash <= 0) return;
    dx /= len;
    dy /= len;
    ImVec2 c0 = dl->GetClipRectMin(), c1 = dl->GetClipRectMax();
    ImVec2 o(c0.x - th, c0.y - th), p(a.x - o.x, a.y - o.y), q(b.x - o.x, b.y - o.y);
    if (!cmdClipToBox(&p, &q, c1.x - c0.x + 2 * th, c1.y - c0.y + 2 * th)) return;
    float t0 = (p.x + o.x - a.x) * dx + (p.y + o.y - a.y) * dy, t1 = (q.x + o.x - a.x) * dx + (q.y + o.y - a.y) * dy;
    if (t0 > t1) std::swap(t0, t1);
    for (float v = floorf(std::max(t0, 0.0f) / (dash * 2)) * dash * 2; v < t1 && v < len; v += dash * 2) {
        float e = std::min(v + dash, len);
        dl->AddLine(ImVec2(a.x + dx * v, a.y + dy * v), ImVec2(a.x + dx * e, a.y + dy * e), col, th);
    }
}

/* The segment between the middles of two dots, cut to the picture, w x h dots; dashed
** dash px on and off if dash is above 0. */
void segment(ImDrawList* dl, const ui::CanvasXf& xf, int ax, int ay, int bx, int by, int w, int h, ImU32 col, float th,
             float dash = 0)
{
    ImVec2 a(ax + 0.5f, ay + 0.5f), b(bx + 0.5f, by + 0.5f);
    if (!cmdClipToBox(&a, &b, (float)w, (float)h)) return;
    ImVec2 sa(xf.origin.x + a.x * xf.scale.x, xf.origin.y + a.y * xf.scale.y);
    ImVec2 sb(xf.origin.x + b.x * xf.scale.x, xf.origin.y + b.y * xf.scale.y);
    if (dash > 0) dashedLine(dl, sa, sb, col, th, dash);
    else dl->AddLine(sa, sb, col, th);
}

/* An edge's ends: its LINE's as drawn, or the projections where the LINE was cut. */
void edgeEnds(const Geo3DDebugCmd* cmd, const Geo3DDebugRun& run, const Geo3DDebugItem& it, int* e)
{
    if (it.cmdCount > 0 && complete(it) && lineEnds(cmd[it.cmdFirst], run.ypage, &e[0], &e[1], &e[2], &e[3])) return;
    e[0] = it.sx[0];
    e[1] = it.sy[0];
    e[2] = it.sx[1];
    e[3] = it.sy[1];
}

/* Whether a drawn face's commands fill dot x of row y, and their DY. Where its commands
** were cut, worked out from the projections with W as it is now. */
bool faceCovers(const Geo3DDebugCmd* cmd, const Geo3DDebugRun& run, const Geo3DDebugItem& it, int x, int y, int* dyOut)
{
    if (y < it.ymin || y > it.ymax) return false;
    if (complete(it)) {
        for (int k = 0; k < it.cmdCount; k++) {
            int cy, dy, x0, x1;
            if (commandSpan(cmd[it.cmdFirst + k], run.ypage, &cy, &dy, &x0, &x1) && cy == y && x >= x0 && x <= x1) {
                *dyOut = dy;
                return true;
            }
        }
        return false;
    }
    int l, r;
    if (!faceSpan(it, y, run.cfg[16], &l, &r) || x < l || x > r) return false;
    *dyOut = (y + run.ypage) & 0x7ff;
    return true;
}


/* A face's light level as the RUN works it out from its normal n and the light lm. */
int lightLevel(const Int16* n, const Int32* lm)
{
    long long shade = ((long long)lm[0] * n[0] + (long long)lm[1] * n[1] + (long long)lm[2] * n[2]) >> 14;
    return shade <= 0 ? 0 : (int)std::min(6LL, (shade * 7) >> 14);
}

/* The fields of a command as the VDP registers take them: DX, DY, NX, NY and, for LRMM, SX, SY. */
void commandFields(const Geo3DDebugCmd& c, int* dx, int* dy, int* nx, int* ny, int* sx, int* sy)
{
    const UInt8* b = c.bytes;
    int at = c.size == 19 ? 4 : 0;
    *dx = b[at] | ((b[at + 1] & 1) << 8);
    *dy = b[at + 2] | ((b[at + 3] & 7) << 8);
    *nx = b[at + 4] | ((b[at + 5] & 7) << 8);
    *ny = c.size == 19 ? 0 : b[6] | ((b[7] & 7) << 8);
    *sx = c.size == 19 ? b[0] | ((b[1] & 15) << 8) : 0;
    *sy = c.size == 19 ? b[2] | ((b[3] & 31) << 8) : 0;
}

void arrow(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th)
{
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy), hl = ui::Px(6);
    if (len < 1) return;
    dx /= len;
    dy /= len;
    ImVec2 base(b.x - dx * hl, b.y - dy * hl);
    dl->AddLine(a, base, col, th);
    dl->AddTriangleFilled(b, ImVec2(base.x - dy * hl * 0.5f, base.y + dx * hl * 0.5f),
                          ImVec2(base.x + dy * hl * 0.5f, base.y - dx * hl * 0.5f), col);
}

/* Screen px from point p to the segment a-b. */
float pointSegment(ImVec2 p, ImVec2 a, ImVec2 b)
{
    float dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
    float t = len > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
    return sqrtf(ex * ex + ey * ey);
}

/* The page of the engine's mode the screen shows most, by R#2's page, not R#9 EO's or blink's side;
** pair: flat interlace weaves it, odd, with the even page before, which *page is then.
** others: the other pages shown, as "1, 2" (empty for none). False for none at all. */
bool shownEnginePage(const VdpDebugState* s, const VdpRasterView& raster, int mode, int* page, bool* pair,
                     char* others, size_t size)
{
    const int pageMax = 32;
    int bytes = mode <= 6 ? 0x8000 : 0x10000, lines[pageMax] = {}, flatLines[pageMax] = {};
    auto first = [&](int p, int pageMode) { return p * (pageMode <= 6 ? 0x8000 : 0x10000) / bytes; };
    /* Paused within a frame, lines below the beam are the frame before: a game that flipped
    ** since draws on that page now, so only this frame's lines count, else the registers. */
    bool current = false;
    for (const VdpBand& b : raster.bands) {
        current = current || (!b.prev && !b.unknown && b.mode >= 0 && b.page >= 0 && first(b.page, b.mode) < pageMax);
    }
    if (!raster.valid || !current) {
        VdpBitmapOptions o;
        if (vdpDisplayedBitmap(s, vdpLayoutOf(s), &o) && first(o.page, o.mode) < pageMax) {
            lines[first(o.page, o.mode)] = 1;
            flatLines[first(o.page, o.mode)] = vdpIsFlatInterlace(s, s->regs) ? 1 : 0;
        }
    }
    else {
        for (const VdpBand& b : raster.bands) {
            if (b.mode < 0 || b.unknown || b.prev || b.page < 0 || first(b.page, b.mode) >= pageMax) continue;
            lines[first(b.page, b.mode)] += b.last - b.first + 1;
            if (b.flat) flatLines[first(b.page, b.mode)] += b.last - b.first + 1;
        }
    }
    int best = -1;
    for (int p = 0; p < pageMax; p++) {
        if (lines[p] > 0 && (best < 0 || lines[p] > lines[best])) best = p;
    }
    *pair = best >= 0 && (best & 1) && 2 * flatLines[best] > lines[best];
    size_t at = 0;
    others[0] = 0;
    for (int p = 0; p < pageMax; p++) {
        if (lines[p] == 0 || p == best || (*pair && p == best - 1) || at >= size) continue;
        at += (size_t)textf(others + at, size - at, at ? ", %d" : "%d", p);
    }
    if (best >= 0) *page = *pair ? best & ~1 : best;
    return best >= 0;
}

/* Items and commands of the snapshot's RUN run as one RUN's, numbered from 0. */
void copyRun(const Geo3DDebugState& g, const Geo3DDebugRun& run, Geo3DDebugRun* r,
             std::vector<Geo3DDebugItem>& items, std::vector<Geo3DDebugCmd>& cmds)
{
    int i0 = std::min((int)run.itemFirst, GEO3D_DEBUG_ITEMS), i1 = std::min(i0 + (int)run.itemCount, GEO3D_DEBUG_ITEMS);
    int c0 = std::min((int)run.cmdFirst, GEO3D_DEBUG_CMDS), c1 = std::min(c0 + (int)run.cmdCount, GEO3D_DEBUG_CMDS);
    *r = run;
    items.assign(g.item + i0, g.item + i1);
    cmds.assign(g.cmd + c0, g.cmd + c1);
    for (Geo3DDebugItem& it : items) {
        bool in = it.cmdFirst >= c0 && it.cmdFirst + it.cmdCount <= c1;
        it.cmdFirst = (UInt16)(in ? it.cmdFirst - c0 : 0);
        it.cmdCount = (UInt16)(in ? it.cmdCount : 0);
    }
    r->itemFirst = 0;
    r->itemCount = (UInt16)items.size();
    r->cmdFirst = 0;
    r->cmdCount = (UInt16)cmds.size();
}

}

Geo3DPanel::Geo3DPanel()
{
    cv.shared = &shell::zoom();
    cv.frameless = true;
    regFadeReset(regFade);
}

Geo3DPanel::~Geo3DPanel()
{
    geo3dRunLogClose(runLog);
}

const ui::PixelArt* Geo3DPanel::tabArt()
{
    return &cubeArt;
}

void Geo3DPanel::forget()
{
    pic.forget();
    heatPic.forget();
    heatShown = heatOnScreen = false;
    heatHeld = 0;
    regFadeReset(regFade);
    memset(&wordFade, 0, sizeof(wordFade));
    leave();
    unpin();
    newPin = false;
    mode = -1;
    clearCache();
    geo3dRunLogClose(runLog);
    runLog = NULL;
    logFailed = false;
    logView = Geo3DRunLogView();
    perfStrip.forget();
}

void Geo3DPanel::loadUi(const VdpViewerProperties& p)
{
    pageMode = p.v99x8.g3dPage == 1 ? 1 : 0;
    perfStrip.loadUi(p);
    fillMode = p.v99x8.g3dHeat == 1 ? 1 : 0;
}

void Geo3DPanel::saveUi(VdpViewerProperties& p) const
{
    p.v99x8.g3dPage = pageMode;
    perfStrip.saveUi(p);
    p.v99x8.g3dHeat = fillMode;
}

void Geo3DPanel::clearCache()
{
    recs.clear();
    cycles.clear();
    set.clear();
    setCycle = -1;
    cacheOn = false;
    cacheLast = wholeTo = 0;
    logTaken = 0;
    fitOn = false;
    placedPrimed = false;
}

void Geo3DPanel::leave()
{
    hotSeq = hotSeqNext = 0;
    hotIndex = hotIndexNext = -1;
}

void Geo3DPanel::track(const VdpDebugState* s, const Focus& f, const VdpRasterView& raster)
{
    const RunRec* rec;
    int k;
    if (!active(s)) {
        geo3dRunLogArm(runLog, false);
        perfStrip.disarm();
        clearCache();
        unpin();
        return;
    }
    /* The log's RUNs while it has some of this geo3d, epoch and rows; else the snapshot's,
    ** after the panel's own. A new epoch or rows takes the snapshot's first, so they stay. */
    const Geo3DDebugState& g = s->geo3d;
    int m = cmdEngineMode(s), r = m < 0 ? 0 : vdpBitmapPageCount(s, m) * 256;
    if (runLog == NULL && !logFailed) {
        runLog = geo3dRunLogOpen();
        logFailed = runLog == NULL;
    }
    geo3dRunLogConfig(runLog, g.cartridge, r, pair);
    /* On any tab, so a rewind made away from it comes whole. */
    geo3dRunLogArm(runLog, true);
    perfStrip.keep(s);
    geo3dRunLogTake(runLog, &logView);
    bool logOk = geo3dRunLogArmed(runLog) && r > 0 && !logView.runs.empty() && logView.cartridge == g.cartridge &&
                 logView.epoch == g.epoch && logView.rows == r;
    if (!logOk || !cacheOn || g.epoch != cacheEpoch || r != cacheRows) absorb(s, r);
    if (logOk && logView.version != logTaken) takeLog();
    /* With the log the capture need only give the latest RUN the room first; without, it
    ** brings each RUN after wholeTo again until it is settled. */
    geo3dDebugSince(g.cartridge, g.epoch, logOk ? (g.runSeq != 0 ? g.runSeq - 1 : 0) : wholeTo);
    chooseSet(s, raster);
    /* A state load or a rewind numbers the RUNs again: the pin is found afresh by its RAM. */
    if (pinned() && pinEpoch != cacheEpoch) {
        pinEpoch = cacheEpoch;
        pinRenumbered = true;
    }
    followPin();
    if (pinned() && ((f.pinned && f.kind != FOCUS_NONE) || !findItem(pinSeq, pinIndex, &rec, &k))) unpin();
}

/* Takes in the RUNs of a new snapshot: a better copy of one kept, or the next ones, each
** going on the open cycle while it draws on the same engine page (or pair, under flat interlace). */
void Geo3DPanel::absorb(const VdpDebugState* s, int r)
{
    const Geo3DDebugState& g = s->geo3d;
    bool same = cacheOn && g.epoch == cacheEpoch && r == cacheRows;
    /* Taken before the log's latest copy: the next one is not. */
    if (same && ((g.runSeq == cacheSeq && g.windowEnd == cacheEnd) || g.runSeq < cacheLast)) return;
    /* A reset or state load numbers the RUNs again; another mode moves the pages. */
    if (!same) clearCache();
    cacheOn = true;
    cacheEpoch = g.epoch;
    cacheSeq = g.runSeq;
    cacheEnd = g.windowEnd;
    cacheRows = r;
    if (r <= 0) return;
    UInt32 oldest = 0;
    for (int q = 0; q < g.runCount; q++) {
        const Geo3DDebugRun& run = g.run[q];
        if (run.seq == 0) continue;
        if (oldest == 0) oldest = run.seq;
        bool settle = run.ended && ((run.itemCount >= run.itemTotal && run.cmdCount >= run.cmdTotal) ||
                                    run.seq == g.wholeSeq);
        if (run.seq <= cacheLast) {
            auto at = recs.find(run.seq);
            if (at == recs.end()) continue;
            const RunRec& rec = *at->second;
            /* Its items and commands are known from its start; the ring loses them later. A
            ** header that has it ended wins over one that does not, with the better copy's counts. */
            Geo3DDebugRun head = run.ended || !rec.run.ended ? run : rec.run;
            bool more = run.itemCount > rec.run.itemCount ||
                        (!rec.cmdsDropped && run.itemCount == rec.run.itemCount && run.cmdCount > rec.run.cmdCount);
            bool ends = head.ended && !rec.run.ended;
            if (!more && !ends && (rec.settled || !settle)) continue;
            std::shared_ptr<RunRec> copy = std::make_shared<RunRec>(rec);
            copy->settled = rec.settled || settle;
            if (more) {
                copyRun(g, run, &copy->run, copy->items, copy->cmds);
                if (rec.cmdsDropped) copy->dropCmds();
            }
            if (ends) {
                head.itemFirst = copy->run.itemFirst;
                head.itemCount = copy->run.itemCount;
                head.cmdFirst = copy->run.cmdFirst;
                head.cmdCount = copy->run.cmdCount;
                copy->run = head;
            }
            at->second = copy;
            continue;
        }
        /* None missed: the next one, or one the copy knows began its page. */
        bool gap = run.seq != cacheLast + 1 && run.seq != g.pageFrom;
        std::shared_ptr<RunRec> rec = std::make_shared<RunRec>();
        copyRun(g, run, &rec->run, rec->items, rec->cmds);
        rec->settled = settle;
        geo3dRunAppend(recs, cycles, rec, (run.ypage % r) / 256, gap, pair);
        cacheLast = run.seq;
    }
    geo3dRunTrim(recs, cycles, pinSeq, false);

    /* The capture hands over every RUN after wholeTo: those it no longer has, and those
    ** settled, need not come again. */
    if (oldest > wholeTo + 1) wholeTo = oldest - 1;
    while (wholeTo < cacheLast) {
        auto at = recs.find(wholeTo + 1);
        if (at != recs.end() && !at->second->settled) break;
        wholeTo++;
    }
}

/* The log's RUNs and cycles, and of the panel's own those it lacks: the start of its first
** cycle, seen before it was armed, and the cycles before or between its own while the
** retention keeps them (the last closed one of a page, the pinned one). */
void Geo3DPanel::takeLog()
{
    Geo3DRunMap next = logView.runs;
    std::vector<Cycle> nextCycles = logView.cycles;
    UInt32 logLast = next.rbegin()->first;
    bool mine = cacheOn && cacheEpoch == logView.epoch && cacheRows == logView.rows;
    size_t fi = 0;   /* the log's first cycle */
    for (const Cycle& c : cycles) {
        if (!mine) break;
        Cycle& first = nextCycles[fi];
        bool samePage = c.page == first.page || (pair && (c.page >> 1) == (first.page >> 1));
        if (first.gap && samePage && c.first < first.first && c.last + 1 >= first.first &&
            (!c.closed || c.last >= first.first)) {
            next.insert(recs.lower_bound(c.first), recs.lower_bound(first.first));
            first.first = c.first;
            first.gap = c.gap;
            continue;
        }
        bool overlaps = c.last >= logLast;
        size_t at = nextCycles.size();
        for (size_t i = 0; i < nextCycles.size(); i++) {
            overlaps = overlaps || (nextCycles[i].first <= c.last && nextCycles[i].last >= c.first);
            if (at == nextCycles.size() && nextCycles[i].first > c.last) at = i;
        }
        if (overlaps) continue;
        /* Nothing was missed between them. */
        if (c.last + 1 == first.first) first.gap = false;
        Cycle kept = c;
        kept.closed = true;
        nextCycles.insert(nextCycles.begin() + at, kept);
        if (at <= fi) fi++;
        next.insert(recs.lower_bound(c.first), recs.upper_bound(c.last));
    }
    geo3dRunTrim(next, nextCycles, pinSeq, false);
    recs.swap(next);
    cycles.swap(nextCycles);
    logTaken = logView.version;
    /* Should the log have none, the snapshots go on from here. */
    cacheOn = true;
    cacheEpoch = logView.epoch;
    cacheRows = logView.rows;
    cacheSeq = cacheEnd = 0;
    cacheLast = wholeTo = recs.empty() ? 0 : recs.rbegin()->first;
}

/* The picture's page: the screen's, else the latest RUN's; its set: the page's open
** cycle, else its last closed one. */
void Geo3DPanel::chooseSet(const VdpDebugState* s, const VdpRasterView& raster)
{
    const Geo3DDebugState& g = s->geo3d;
    set.clear();
    setCycle = -1;
    mode = cmdEngineMode(s);
    if (mode < 0) return;
    rows = vdpBitmapPageCount(s, mode) * 256;
    auto latest = recs.empty() ? recs.end() : std::prev(recs.end());
    /* The newest RUN by its number: the copied list may stop short of it. */
    const Geo3DDebugRun* newest = NULL;
    for (int i = 0; i < g.runCount && newest == NULL; i++) {
        if (g.run[i].seq == g.runSeq) newest = &g.run[i];
    }
    latestYpage = newest != NULL ? newest->ypage : g.runCount > 0 ? g.ypage :
                  latest != recs.end() ? latest->second->run.ypage : g.ypage;
    pageShown = shownEnginePage(s, raster, mode, &shownPage, &pair, shownOthers, sizeof(shownOthers));
    pair = pageShown && pair;
    shownPage = shownPage < 0 ? 0 : shownPage % (rows / 256);
    picPage = pageShown && pageMode == 0 ? shownPage : (latestYpage % rows) / 256;
    picPage = picPage < 0 ? 0 : picPage % (rows / 256);
    if (pair) picPage &= ~1;
    picStart = picPage * 256;
    /* The log runs ahead of the snapshot while running: RUNs begun after it are not in this
    ** VRAM yet, so a cycle they open is passed over and their outlines left out. */
    auto inSnapshot = [&](UInt32 seq) { return g.runSeq == 0 || seq <= g.runSeq; };
    for (int i = (int)cycles.size() - 1; i >= 0 && setCycle < 0; i--) {
        if ((cycles[i].page == picPage || (pair && (cycles[i].page >> 1) == (picPage >> 1))) && inSnapshot(cycles[i].first)) {
            setCycle = i;
        }
    }
    if (setCycle < 0) return;
    const Cycle& c = cycles[setCycle];
    for (auto at = recs.lower_bound(c.first); at != recs.end() && at->first <= c.last && inSnapshot(at->first); ++at) {
        const RunRec& rec = *at->second;
        int shift = (int)(rec.run.ypage % rows) - picStart;
        set.push_back(RunRef{ &rec.run, rec.items.data(), rec.cmds.data(), shift });
    }
}

void Geo3DPanel::pinItem(UInt32 seq, int index)
{
    if (pinSeq == seq && pinIndex == index) {
        unpin();
        return;
    }
    pinSeq = seq;
    pinIndex = index;
    pinEpoch = cacheEpoch;
    pinRenumbered = false;
    newPin = true;
    auto at = recs.find(seq);
    if (at != recs.end()) {
        const Geo3DDebugRun& r = at->second->run;
        pinKey[0] = r.vfirst; pinKey[1] = r.nvert; pinKey[2] = r.first; pinKey[3] = r.count;
        pinFaces = r.faces;
        pinNth = 0;
        for (const RunRef& ref : set) {
            if (ref.run->seq == seq) break;
            if (sameRanges(*ref.run)) pinNth++;
        }
    }
}

bool Geo3DPanel::sameRanges(const Geo3DDebugRun& run) const
{
    return run.faces == pinFaces && run.vfirst == pinKey[0] && run.nvert == pinKey[1] &&
           run.first == pinKey[2] && run.count == pinKey[3];
}

/* A pinned item whose RUN left the picture moves to the same item of the RUN that read
** the same RAM ranges, the same one of them in order, so it follows while the game runs. */
void Geo3DPanel::followPin()
{
    int r, k;
    if (!pinned() || (!pinRenumbered && findInSet(pinSeq, pinIndex, &r, &k))) return;
    UInt32 to = 0;
    int nth = 0;
    for (const RunRef& ref : set) {
        if (!sameRanges(*ref.run) || !findInSet(ref.run->seq, pinIndex, &r, &k)) continue;
        to = ref.run->seq;
        if (nth++ == pinNth) break;
    }
    if (to != 0) {
        pinSeq = to;
        pinRenumbered = false;
    }
    else if (pinRenumbered) {
        unpin();
    }
}

/* Face or edge number index of RUN seq, if it is kept: the RUN and the item's place in it. */
bool Geo3DPanel::findItem(UInt32 seq, int index, const RunRec** rec, int* k) const
{
    auto at = seq != 0 ? recs.find(seq) : recs.end();
    if (at == recs.end()) return false;
    for (int q = 0; q < (int)at->second->items.size(); q++) {
        if (at->second->items[q].index != index) continue;
        *rec = at->second.get();
        *k = q;
        return true;
    }
    return false;
}

/* The same in the picture's set: the RUN's place in it. */
bool Geo3DPanel::findInSet(UInt32 seq, int index, int* r, int* k) const
{
    for (int q = 0; seq != 0 && q < (int)set.size(); q++) {
        if (set[q].run->seq != seq) continue;
        for (int i = 0; i < set[q].run->itemCount; i++) {
            if (set[q].item[i].index != index) continue;
            *r = q;
            *k = i;
            return true;
        }
    }
    return false;
}

/* The RUN's items still project, with its words, where it put them from the RAM as it is
** now, so the model RAM the card shows is still the RUN's. Worked out once a picture. */
bool Geo3DPanel::placed(const VdpDebugState* s, const RunRec& rec)
{
    const Geo3DDebugState& g = s->geo3d;
    unsigned v = shell::pictureVersion();
    if (placedPrimed && placedVer == v && placedSeq == rec.run.seq) return placedOk;
    placedPrimed = true;
    placedVer = v;
    placedSeq = rec.run.seq;
    int nv = rec.run.faces ? 4 : 2;
    bool ok = !rec.items.empty();
    for (size_t k = 0; ok && k < rec.items.size(); k++) {
        const Geo3DDebugItem& it = rec.items[k];
        for (int j = 0; ok && j < nv; j++) {
            Geo3DDebugProj p = geo3dDebugProject(rec.run.cfg, g.vmem[it.v[j] & (GEO3D_DEBUG_ENTRIES - 1)]);
            ok = p.sx == it.sx[j] && p.sy == it.sy[j];
        }
    }
    placedOk = ok;
    return ok;
}

void Geo3DPanel::facts(const VdpDebugState* s, char* out, size_t size) const
{
    const Geo3DDebugState& g = s->geo3d;
    int id = g.regs[0x49];
    if (g.valid && size > 0) textf(out, size, ts(TS_G3D_FACTS), id >> 4, id & 15, g.port, g.running ? ts(TS_G3D_FACTS_RUN) : "");
}

const char* Geo3DPanel::summary(const VdpDebugState* s)
{
    const Geo3DDebugState& g = s->geo3d;
    int id = g.regs[0x49];
    textf(sum, sizeof(sum), ts(TS_G3D_SUM), id >> 4, id & 15, g.port, modeName(g), (unsigned)g.drawn);
    return sum;
}

bool Geo3DPanel::hotItem(int r, int k) const
{
    return hotIndex >= 0 && set[r].run->seq == hotSeq && set[r].item[k].index == hotIndex;
}

/* The topmost drawn face or edge at picture dot x, y: a later RUN over an earlier one,
** within a RUN the later in paint order; else a face turned away near it. */
Geo3DPanel::Hit Geo3DPanel::itemAt(int x, int y) const
{
    float reach = ui::Px(4);
    Hit best;
    int bestOrder = -1;
    for (int r = 0; r < (int)set.size(); r++) {
        const RunRef& ref = set[r];
        const Geo3DDebugRun& run = *ref.run;
        int ry = y - ref.shift;
        for (int k = 0; k < run.itemCount; k++) {
            int dy = -1;
            const Geo3DDebugItem& it = ref.item[k];
            if (it.outcome != GEO3D_ITEM_DRAWN) continue;
            if (run.faces) {
                if (best.run == r && it.order < bestOrder) continue;
                if (!faceCovers(ref.cmd, run, it, x, ry, &dy)) continue;
                bestOrder = it.order;
            }
            else {
                int e[4];
                edgeEnds(ref.cmd, run, it, e);
                if (segmentDistance(ovScale, x, ry, e[0], e[1], e[2], e[3]) > reach) continue;
            }
            best.run = r;
            best.item = k;
            best.dy = dy;
        }
    }
    if (best.item >= 0) return best;
    /* Else a face turned away, by its dashed outline: a later one over an earlier. */
    for (int r = 0; r < (int)set.size(); r++) {
        const RunRef& ref = set[r];
        if (!ref.run->faces) continue;
        int ry = y - ref.shift;
        for (int k = 0; k < ref.run->itemCount; k++) {
            const Geo3DDebugItem& it = ref.item[k];
            if (it.outcome != GEO3D_ITEM_BACK) continue;
            for (int j = 0; j < 4; j++) {
                int n = (j + 1) & 3;
                if (segmentDistance(ovScale, x, ry, it.sx[j], it.sy[j], it.sx[n], it.sy[n]) > reach) continue;
                best.run = r;
                best.item = k;
                break;
            }
        }
    }
    return best;
}

/* The dots a face's commands fill, over picture rows rowLo-rowHi; worked out where they were cut. */
void Geo3DPanel::fillFace(ImDrawList* dl, const ui::CanvasXf& xf, const RunRef& ref, const Geo3DDebugItem& it,
                          int rowLo, int rowHi, ImU32 col) const
{
    const Geo3DDebugRun& run = *ref.run;
    rowLo -= ref.shift;
    rowHi -= ref.shift;
    auto fill = [&](int y, int l, int r) {
        dl->AddRectFilled(xf.toScreen((float)l, (float)(y + ref.shift)), xf.toScreen((float)(r + 1), (float)(y + ref.shift + 1)), col);
    };
    if (complete(it)) {
        for (int k = 0; k < it.cmdCount; k++) {
            int y, dy, x0, x1;
            if (!commandSpan(ref.cmd[it.cmdFirst + k], run.ypage, &y, &dy, &x0, &x1) || y < rowLo || y > rowHi) continue;
            fill(y, x0, x1);
        }
        return;
    }
    int y0 = std::max((int)it.ymin, rowLo), y1 = std::min((int)it.ymax, rowHi);
    for (int y = y0; y <= y1; y++) {
        int l, r;
        if (faceSpan(it, y, run.cfg[16], &l, &r)) fill(y, l, r);
    }
}

/* Clips to the picture left of the RUN's W, as geo3d cuts what it fills; false, with
** nothing pushed, if W leaves nothing. */
bool Geo3DPanel::clipToW(ImDrawList* dl, const ui::CanvasXf& xf, const Geo3DDebugRun& run) const
{
    int w = std::min((int)run.cfg[16], pic.w);
    if (w <= 0) return false;
    dl->PushClipRect(xf.toScreen(0, 0), xf.toScreen((float)w, (float)pic.h), true);
    return true;
}

/* A face filled over the hull of the middles of its four projections, under clipToW: geo3d
** fills each row from its leftmost crossing to its rightmost, so a concave or crossed quad
** fills its hull. Nothing for a face with no area. */
void Geo3DPanel::fillQuad(ImDrawList* dl, const ui::CanvasXf& xf, const Geo3DDebugItem& it, int shift, ImU32 col) const
{
    struct P { int x, y; };
    P p[4];
    for (int k = 0; k < 4; k++) p[k] = P{ it.sx[k], it.sy[k] };
    std::sort(p, p + 4, [](const P& a, const P& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    auto cross = [](const P* o, const P* a, const P& b) {
        return (long long)(a->x - o->x) * (b.y - o->y) - (long long)(a->y - o->y) * (b.x - o->x);
    };
    /* Andrew's monotone chain, which with y down goes round clockwise on the screen, as
    ** anti-aliased filling wants: the hull's top left to right, then its bottom back. */
    const P* h[8];
    int n = 0;
    for (int k = 0; k < 4; k++) {
        while (n >= 2 && cross(h[n - 2], h[n - 1], p[k]) <= 0) n--;
        h[n++] = &p[k];
    }
    for (int k = 2, lo = n + 1; k >= 0; k--) {
        while (n >= lo && cross(h[n - 2], h[n - 1], p[k]) <= 0) n--;
        h[n++] = &p[k];
    }
    n--;
    if (n < 3) return;
    ImVec2 v[4];
    for (int k = 0; k < n; k++) {
        v[k] = ImVec2(xf.origin.x + (h[k]->x + 0.5f) * xf.scale.x, xf.origin.y + (h[k]->y + shift + 0.5f) * xf.scale.y);
    }
    dl->AddConvexPolyFilled(v, n, col);
}

void Geo3DPanel::outlineFace(ImDrawList* dl, const ui::CanvasXf& xf, const Geo3DDebugItem& it, int shift, ImU32 col, float th,
                             bool dashed) const
{
    for (int k = 0; k < 4; k++) {
        int j = (k + 1) & 3;
        segment(dl, xf, it.sx[k], it.sy[k] + shift, it.sx[j], it.sy[j] + shift, pic.w, pic.h, col, th, dashed ? ui::Px(3) : 0);
    }
}

void Geo3DPanel::overlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
{
    Geo3DPanel* self = (Geo3DPanel*)ref;
    const VdpDebugState* s = self->ovState;
    const Geo3DDebugState& g = s->geo3d;
    const std::vector<RunRef>& set = self->set;
    const UiTokens& t = uiTokens();
    float th = fmaxf(1.0f, ui::Px(1));
    int w = g.cfg[16], pw = self->pic.w, ph = self->pic.h;
    self->ovScale = xf.scale;
    dl->PushClipRect(xf.clipMin, xf.clipMax, true);
    /* The page is the canvas's first part, the top view its second: the grid goes on the page only. */
    ImVec2 pa = xf.toScreen(0, 0), pb = xf.toScreen((float)pw, (float)ph);
    if (self->pic.tex.id() != 0) ui::ImageNearest(dl, self->pic.tex.id(), pa, pb);
    if (self->cv.grid) {
        ui::CanvasGrid(dl, xf, ImVec2(fmaxf(pa.x, xf.clipMin.x), fmaxf(pa.y, xf.clipMin.y)),
                       ImVec2(fminf(pb.x, xf.clipMax.x), fminf(pb.y, xf.clipMax.y)));
    }
    dl->PushClipRect(pa, pb, true);
    /* How many times each dot was painted; over a dark ground once the
    ** picture is no longer on screen, as VRAM then holds another. */
    if (self->heatShown && self->heatPic.tex.id() != 0) {
        ImVec2 a = pa, b = pb;
        if (!self->heatOnScreen) dl->AddRectFilled(a, b, IM_COL32(20, 20, 20, 255));
        else dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 110));
        dl->AddImage(ImTextureRef(self->heatPic.tex.id()), a, b);
    }
    /* geo3d clips to W. */
    if (w > 0 && w < pw) {
        parts::dashedSegment(dl, xf.toScreen((float)w, 0), xf.toScreen((float)w, (float)ph), t.textTertiary, th, ui::Px(4));
    }
    int pinRun = -1, pin = -1;
    if (!self->findInSet(self->pinSeq, self->pinIndex, &pinRun, &pin)) pin = -1;
    else if (!onPicture(*set[pinRun].run, set[pinRun].item[pin])) pin = -1;
    auto edge = [&](const RunRef& r, const Geo3DDebugItem& it, ImU32 col, float lw) {
        int e[4];
        edgeEnds(r.cmd, *r.run, it, e);
        segment(dl, xf, e[0], e[1] + r.shift, e[2], e[3] + r.shift, pw, ph, col, lw);
    };
    /* The hovered and the pinned item filled over the rest: a drawn face where its
    ** commands fill, one turned away between its projections. */
    auto lit = [&](int r, int k, ImU32 col, int fill) {
        const RunRef& rr = set[r];
        const Geo3DDebugItem& it = rr.item[k];
        bool back = it.outcome == GEO3D_ITEM_BACK;
        if (!rr.run->faces) {
            edge(rr, it, col, th * 2);
        }
        else if (back) {
            if (self->clipToW(dl, xf, *rr.run)) {
                self->fillQuad(dl, xf, it, rr.shift, ui::WithAlpha(col, fill));
                dl->PopClipRect();
            }
            self->outlineFace(dl, xf, it, rr.shift, col, th * 2, true);
        }
        else {
            int rowLo = (int)floorf((xf.clipMin.y - xf.origin.y) / xf.scale.y);
            int rowHi = (int)ceilf((xf.clipMax.y - xf.origin.y) / xf.scale.y);
            self->fillFace(dl, xf, rr, it, rowLo, rowHi, ui::WithAlpha(col, fill));
            self->outlineFace(dl, xf, it, rr.shift, col, th * 2);
        }
    };
    if (self->outlines) {
        ImU32 line = ui::WithAlpha(faceColour, faceLineAlpha), fill = ui::WithAlpha(faceColour, faceFillAlpha);
        int hotRun = -1, hot = -1;
        for (int r = 0; r < (int)set.size(); r++) {
            for (int k = 0; k < set[r].run->itemCount; k++) {
                if (self->hotItem(r, k) && onPicture(*set[r].run, set[r].item[k]) && !(r == pinRun && k == pin)) {
                    hotRun = r;
                    hot = k;
                }
            }
        }
        auto plain = [&](int r, int k, int outcome) {
            return set[r].item[k].outcome == outcome && !(r == pinRun && k == pin) && !(r == hotRun && k == hot);
        };
        /* Drawn faces filled lightly, then the dashed outlines of those turned away, then
        ** the outlines of those drawn and the edges, each in the order recorded. */
        for (int r = 0; r < (int)set.size(); r++) {
            const RunRef& rr = set[r];
            if (!rr.run->faces || !self->clipToW(dl, xf, *rr.run)) continue;
            for (int k = 0; k < rr.run->itemCount; k++) {
                if (plain(r, k, GEO3D_ITEM_DRAWN)) self->fillQuad(dl, xf, rr.item[k], rr.shift, fill);
            }
            dl->PopClipRect();
        }
        for (int r = 0; r < (int)set.size(); r++) {
            const RunRef& rr = set[r];
            for (int k = 0; rr.run->faces && k < rr.run->itemCount; k++) {
                if (plain(r, k, GEO3D_ITEM_BACK)) self->outlineFace(dl, xf, rr.item[k], rr.shift, line, th, true);
            }
        }
        for (int r = 0; r < (int)set.size(); r++) {
            const RunRef& rr = set[r];
            for (int k = 0; k < rr.run->itemCount; k++) {
                if (!plain(r, k, GEO3D_ITEM_DRAWN)) continue;
                if (rr.run->faces) self->outlineFace(dl, xf, rr.item[k], rr.shift, line, th);
                else edge(rr, rr.item[k], line, th);
            }
        }
        if (hot >= 0) lit(hotRun, hot, pin >= 0 ? t.accentHover : t.accent, pin >= 0 ? 70 : litFillAlpha);
    }
    /* The faces of the RUN pinned on the drawing time strip, for the picture on screen. */
    UInt32 perfRun = self->pageMode == 0 ? self->perfStrip.litRun() : 0;
    for (int r = 0; perfRun != 0 && r < (int)set.size(); r++) {
        if (set[r].run->seq != perfRun) continue;
        for (int k = 0; k < set[r].run->itemCount; k++) {
            if (set[r].item[k].outcome == GEO3D_ITEM_DRAWN && onPicture(*set[r].run, set[r].item[k])) lit(r, k, t.accent, 50);
        }
    }
    if (pin >= 0) lit(pinRun, pin, t.accent, litFillAlpha);
    dl->PopClipRect();
    float tx = (float)self->topX, ty = (float)self->topY;
    self->topDraw(dl, xf.toScreen(tx, ty), xf.toScreen(tx + pw, ty + ph));
    dl->PopClipRect();
}

/* The notes that apply, for the legend's row. They are about the set, which changes only
** when the picture does; the RUN in progress is shown only paused. */
int Geo3DPanel::noteItems(const VdpDebugState* s, char item[][512]) const
{
    const Geo3DDebugState& g = s->geo3d;
    int  n = 0;
    bool running = emulatorGetState() == EMU_RUNNING || emulatorGetState() == EMU_SUSPENDED;
    if (g.runSeq == 0) {
        textf(item[n++], sizeof(item[0]), "%s", ts(TS_G3D_NOTE_NO_RUN));
    }
    int sm = s->screenMode;
    /* FG4 over an interleaved SCREEN 7/8: the engine's SCREEN 5 addresses go through the
    ** interleave, which this picture, read in address order, does not follow. */
    if (mode == 5 && (sm == 7 || sm == 8 || sm == 10 || sm == 12) && vdpInterleavedIn(s, sm)) {
        textf(item[n++], sizeof(item[0]), ts(TS_G3D_NOTE_FG4), vdpModeName(sm));
    }
    if (!pageShown && pageMode == 0) {
        textf(item[n++], sizeof(item[0]), "%s", ts(TS_G3D_NOTE_NO_SHOWN_PAGE));
    }
    if (setCycle >= 0 && cycles[setCycle].gap && !set.empty()) {
        textf(item[n++], sizeof(item[0]), ts(TS_G3D_NOTE_SET_GAP), (unsigned)set[0].run->seq);
    }
    unsigned total = 0;
    int kept = 0, limit = GEO3D_DEBUG_ITEMS;
    for (const RunRef& r : set) {
        total += r.run->itemTotal;
        kept += r.run->itemCount;
        /* Only a copy from the run log holds more than the snapshot's room. */
        if (r.run->itemCount < r.run->itemTotal && r.run->itemCount >= GEO3D_DEBUG_CORE_ITEMS) limit = GEO3D_DEBUG_CORE_ITEMS;
    }
    if ((unsigned)kept < total) {
        textf(item[n++], sizeof(item[0]), ts(TS_G3D_NOTE_SET_CUT), limit, total - (unsigned)kept);
    }
    /* Only when it draws into the page shown here: a step that stops at a flip leaves the
    ** next picture in progress on the other page. */
    int runPage = rows > 0 ? (latestYpage % rows) / 256 : -1;
    bool runHere = runPage == picPage || (pair && (runPage >> 1) == (picPage >> 1));
    if (!running && g.running && runHere) {
        textf(item[n++], sizeof(item[0]), ts(TS_G3D_NOTE_RUNNING), g.cmdsIssued, g.cmdsTotal);
    }
    if (fillMode == 1 && n < 6) {
        if (!heatShown) textf(item[n++], sizeof(item[0]), "%s", ts(TS_G3D_HEAT_NOTE_NONE));
        else if (!heatOnScreen && !running) textf(item[n++], sizeof(item[0]), "%s", ts(TS_G3D_HEAT_NOTE_OFF_SCREEN));
    }
    return n;
}

/* The notes on the legend's row from at to right: the first, a count of the rest, all in a tooltip. */
void Geo3DPanel::notes(char item[][512], int n, ImVec2 at, float right)
{
    if (n <= 0 || right <= at.x) return;
    char line[640];
    size_t len = (size_t)textf(line, sizeof(line), "%s", item[0]);
    if (n > 1 && len < sizeof(line)) textf(line + len, sizeof(line) - len, ts(TS_SPR_TAB_MORE_NOTES), n - 1);
    float lh = ImGui::GetTextLineHeight();
    bool hov = ImGui::IsMouseHoveringRect(at, ImVec2(right, at.y + lh));
    ui::TextFit(ImGui::GetWindowDrawList(), at, right, line, uiTokens().textSecondary, hov && n <= 1);
    if (n > 1 && hov) {
        char all[2048] = "";
        size_t al = 0;
        for (int k = 0; k < n && al < sizeof(all); k++) al += (size_t)textf(all + al, sizeof(all) - al, "%s%s", k ? "\n" : "", item[k]);
        parts::steadyTooltip(notesTip, ImGui::GetID("##g3dnotes"), all);
    }
}

/* One row, always there and one line high, so the picture never moves: how the picture
** draws faces by what the RUN did with them, dimmed with the outlines off; empty for edges.
** Returns where the row has room left, for the notes. */
ImVec2 Geo3DPanel::legend(bool faces, bool dim)
{
    UiTokens t = uiTokens();
    if (dim) {
        for (ImU32* c : { &t.text, &t.textSecondary, &t.accent }) *c = ui::WithAlpha(*c, (int)((*c >> IM_COL32_A_SHIFT) & 0xFF) / 2);
    }
    if (fillMode == 1) return heatLegend();
    float lh = ImGui::GetTextLineHeight(), w = std::max(1.0f, floorf(ImGui::GetContentRegionAvail().x));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, lh));
    if (!faces) return p;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = fmaxf(1.0f, ui::Px(1)), sw = floorf(lh * 1.4f), inset = floorf(lh * 0.2f), x = p.x;
    const char* text[3] = { ts(TS_G3D_LEGEND_DRAWN), ts(TS_G3D_LEGEND_BACK), ts(TS_G3D_LEGEND_HOT) };
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + lh), true);
    for (int k = 0; k < 3; k++) {
        ImVec2 a(x + th, p.y + inset), b(x + sw - th, p.y + lh - inset);
        ImVec2 ia(a.x + 2 * th, a.y + 2 * th), ib(b.x - 2 * th, b.y - 2 * th);
        if (k < 2) dl->AddRectFilled(a, b, faceBackdrop);
        if (k == 0) {
            dl->AddRectFilled(ia, ib, ui::WithAlpha(faceColour, faceFillAlpha));
            dl->AddRect(ia, ib, ui::WithAlpha(faceColour, faceLineAlpha), 0, th);
        }
        else if (k == 1) {
            parts::dashedRect(dl, ia, ib, ui::WithAlpha(faceColour, faceLineAlpha), th);
        }
        else {
            dl->AddRectFilled(a, b, ui::WithAlpha(t.accent, litFillAlpha));
            dl->AddRect(a, b, t.accent, 0, th * 2);
        }
        x += sw + ui::Px(4);
        dl->AddText(ImVec2(x, p.y), t.text, text[k]);
        x += ImGui::CalcTextSize(text[k]).x + ui::Px(14);
    }
    dl->PopClipRect();
    return ImVec2(x, p.y);
}

float Geo3DPanel::bottomHeight(const VdpDebugState* s, float room)
{
    /* As drawTab: no picture while the command engine is in no bitmap mode. */
    bottomOn = active(s) && mode >= 0;
    return bottomOn ? perfStrip.bottomHeight(room) : 0;
}

void Geo3DPanel::drawTab(const VdpDebugState* s, Focus& f, const VdpRasterView& raster)
{
    const Geo3DDebugState& g = s->geo3d;
    hotSeq = hotSeqNext;
    hotIndex = hotIndexNext;
    hotSeqNext = 0;
    hotIndexNext = -1;
    perfStrip.update();
    const char* pageNames[] = { ts(TS_G3D_PAGE_SHOWN), ts(TS_G3D_PAGE_DRAWN) };
    ui::BeginCommandBar("bar");
    ui::CommandBarItem(ui::SegmentedWidth(pageNames, 2), ui::SegmentedHeight());
    int pick = pageMode;
    /* The top view's extent starts again for the other page's RUNs. */
    if (ui::Segmented("g3dpage", &pick, pageNames, 2, 0, 0, NULL, 0, NULL, ts(TS_G3D_PAGE_TIP)) && pick != pageMode) {
        pageMode = pick;
        fitOn = false;
    }
    parts::barArtToggle("##g3dout", ui::DECOR_NONE, ts(TS_G3D_OUTLINES_TIP), &outlines, wireArt);
    bool heat = fillMode == 1;
    parts::barToggle("##g3dfill", "\xEE\xA0\x9E" /* U+E81E MapLayers */, ts(TS_G3D_FILL_HEAT), ts(TS_G3D_FILL_TIP), &heat);
    fillMode = heat ? 1 : 0;
    ui::GridToggle(cv);
    ui::CommandBarRight(ui::ZoomControlsWidth());
    ui::ZoomControls("zoom", cv);
    ui::EndCommandBar();
    if (mode < 0) {
        parts::emptyView(ts(TS_G3D_NOT_BITMAP));
        return;
    }

    /* The engine's Y wraps at its rows; the picture starts at its page, as high as the screen
    ** and as far down as a RUN's YPAGE within the page moves it. Not H: a game may change it
    ** for each RUN. */
    int down = 0, lines = (s->regs[9] & 0x80) ? 212 : 192;
    for (const RunRef& r : set) down = std::max(down, r.shift);
    int h = std::min(lines + down, rows);
    /* Woven, the screen is twice its display lines high; no more, as the rest is not shown. */
    if (pair) h = std::min(2 * lines, rows);
    VdpLayout l = vdpLayoutOf(s);
    VdpBitmapOptions o = { mode, -1, 256, 0, VDP_COLOR0_AUTO, false, false };
    char key[64];
    snprintf(key, sizeof(key), "g%d/%d/%d/%d", mode, picStart, h, rows);
    bool decoded = pic.needsDecode(key);
    if (decoded) {
        int w, all;
        vdpDecodeBitmap(s, l, o, pages, &w, &all);
        pic.w = w;
        pic.h = h;
        pic.img.resize((size_t)w * h);
        for (int i = 0; i < h; i++) {
            memcpy(&pic.img[(size_t)i * w], &pages[(size_t)((picStart + i) % all) * w], (size_t)w * sizeof(uint32_t));
        }
    }
    pic.upload(decoded);
    /* The engine's space as the picture is decoded, for replaying the writes; a byte holds 2 dots
    ** in SCREEN 5 and 7, 4 in 6, 1 in 8. */
    perfStrip.setSpace(pic.w, rows, mode == 6 ? 4 : mode == 5 || mode == 7 ? 2 : 1);
    buildHeat(h, s->screenMode, s->frameStartTime);
    bool faces = set.empty() ? facesMode(g) : false;
    for (const RunRef& r : set) faces = faces || r.run->faces;
    char item[6][512];
    int noteCount = noteItems(s, item);
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImVec2 rest = legend(faces, !outlines);
    notes(item, noteCount, rest, right);

    /* Woven, the picture has twice the rows: 512 wide modes come out square, 256 wide ones stretch across. */
    bool wide = mode == 6 || mode == 7;
    cv.aspectX = pair && !wide ? 2 : 1;
    cv.aspectY = wide && !pair ? 2 : 1;
    cv.lineRows = 1;
    cv.gutter = 0;
    cv.checker = false;
    ovState = s;
    topPrepare(s);
    /* The top view as large as the page, beside it when that fits the well at this zoom (Fit:
    ** when it shows larger), else under it; going back beside needs 24 px to spare. */
    ImVec2 avail = ImGui::GetContentRegionAvail(), k = ui::CanvasScale(cv, 1);
    float wellH = floorf(std::max(avail.y, ImGui::GetFrameHeight()));
    int pw = pic.w, ph = pic.h, gap = 8, gapX = (int)ceilf(8 * k.y / k.x);
    float sb = ImGui::GetStyle().ScrollbarSize, roomW = avail.x - sb, roomH = wellH - sb;
    float wS = (float)(2 * pw + gapX), hS = (float)ph, wT = (float)pw, hT = (float)(2 * ph + gap);
    if (pw > 0 && ph > 0) {
        if (cv.z().fit) topSide = fminf(roomW / (wS * k.x), roomH / (hS * k.y)) >= fminf(roomW / (wT * k.x), roomH / (hT * k.y));
        else topSide = wS * (float)cv.z().zoom * k.x <= roomW - (topSide ? 0 : ui::Px(24));
    }
    topX = topSide ? pw + gapX : 0;
    topY = topSide ? 0 : ph + gap;
    cv.partCount = 1;
    cv.parts[0] = ui::CanvasState::Part{ 0, 0, (float)pw, (float)ph, 1 };
    topHotR = topHotI = -1;
    ui::CanvasHit hit = ui::Canvas("g3d", 0, topSide ? (int)wS : pw, topSide ? ph : (int)hT, cv, ImVec2(0, wellH), overlay, this);
    if (topHotI >= 0) {
        const Geo3DDebugRun& run = *set[topHotR].run;
        const Geo3DDebugItem& it = set[topHotR].item[topHotI];
        char name[64];
        textf(name, sizeof(name), ts(run.faces ? TS_G3D_SEL_FACE : TS_G3D_SEL_EDGE), it.index);
        ImGui::SetTooltip(ts(it.outcome == GEO3D_ITEM_NEAR ? TS_G3D_TOP_TIP_NEAR : TS_G3D_TOP_TIP), name, (unsigned)run.seq);
        /* Lights it on the picture too. */
        hotSeqNext = run.seq;
        hotIndexNext = it.index;
        if (hit.clicked) pinItem(run.seq, it.index);
    }
    if (hit.clickedEmpty) shell::releaseSelection();
    /* Only the page part is VRAM: the gap and the top view have no dot. */
    VdpPixelInfo pi;
    if (!hit.valid || hit.x >= pw || hit.y >= ph) return;
    if (!vdpBitmapPixel(s, l, o, hit.x, (picStart + hit.y) % rows, &pi)) return;
    Hit it = outlines ? itemAt(hit.x, hit.y) : Hit();
    if (it.item >= 0) {
        const Geo3DDebugRun& run = *set[it.run].run;
        const Geo3DDebugItem& d = set[it.run].item[it.item];
        hotSeqNext = run.seq;
        hotIndexNext = d.index;
        if (hit.clicked) pinItem(run.seq, d.index);
    }
    char st[384];
    if (it.item >= 0) {
        const Geo3DDebugRun& run = *set[it.run].run;
        const Geo3DDebugItem& d = set[it.run].item[it.item];
        if (d.outcome == GEO3D_ITEM_BACK) textf(st, sizeof(st), ts(TS_G3D_STATUS_BACK), hit.x, hit.y, d.index, pi.logical);
        else if (run.faces) textf(st, sizeof(st), ts(TS_G3D_STATUS_FACE), hit.x, hit.y, d.index, it.dy, pi.logical);
        else textf(st, sizeof(st), ts(TS_G3D_STATUS_EDGE), hit.x, hit.y, d.index, d.v[0], d.v[1], pi.logical);
    }
    else {
        textf(st, sizeof(st), ts(TS_G3D_STATUS_PIXEL), hit.x, hit.y, pi.logical);
    }
    /* Overdraw: how often the dot was painted, after what the line already says of it. */
    const Geo3DPerfStrip::Overdraw* od = heatShown ? perfStrip.overdraw() : NULL;
    size_t len = strlen(st);
    if (od != NULL && od->number == heatNumber && hit.x < od->w && len < sizeof(st)) {
        size_t at = (size_t)((heatBase + hit.y) % od->rows) * od->w + hit.x;
        int n = od->count[at];
        UInt32 last = od->last[at];
        if (n == 0) textf(st + len, sizeof(st) - len, "%s", ts(TS_G3D_HEAT_STATUS_NONE));
        else if (last >= 2) textf(st + len, sizeof(st) - len, ts(n == 1 ? TS_G3D_HEAT_STATUS_RUN_ONE : TS_G3D_HEAT_STATUS_RUN), n,
                                  od->runs[last - 2].run);
        else textf(st + len, sizeof(st) - len, ts(last == 1 ? (n == 1 ? TS_G3D_HEAT_STATUS_GEO_ONE : TS_G3D_HEAT_STATUS_GEO) :
                                                      (n == 1 ? TS_G3D_HEAT_STATUS_Z80_ONE : TS_G3D_HEAT_STATUS_Z80)), n);
    }
    ui::StatusText("%s", st);
    /* The dot lights up on the VRAM map. */
    if (!f.pinned) {
        f.space = FOCUS_PHYS;
        f.addr = (UInt32)pi.physical;
        f.len = 1;
        f.kind = FOCUS_VRAM;
        f.a = f.b = 0;
    }
}

/* One model RAM as a bar of its entries: those the next RUN reads filled, a mark over
** FIRST and a line at the write pointer; without ext, the 256 it can write shaded. */
void Geo3DPanel::ramBar(const char* id, const char* name, int first, int count, int wptr, bool ext)
{
    const int entries = GEO3D_DEBUG_ENTRIES;
    const UiTokens& t = uiTokens();
    char line[192];
    textf(line, sizeof(line), ts(TS_G3D_RAM_LINE), name, count, first, wptr);
    ui::TextFitItem(line);
    float w = floorf(ImGui::GetContentRegionAvail().x), mark = ui::Px(6), h = ui::Px(10), th = fmaxf(1.0f, ui::Px(1));
    if (w < 1) return;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, mark + h + ui::Px(2)));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(p0.x, p0.y + mark), b(p0.x + w, p0.y + mark + h);
    auto at = [&](int e) { return floorf(a.x + w * e / entries); };
    int mask = entries - 1, f0 = first & mask, n = count < entries ? count : entries;
    dl->AddRectFilled(a, b, t.controlFill);
    if (!ext) {
        int band = wptr & 0x300;
        dl->AddRectFilled(ImVec2(at(band), a.y), ImVec2(at(band + 256), b.y), ui::WithAlpha(t.caution, 60));
    }
    if (n > 0) {
        int end = f0 + n;
        dl->AddRectFilled(ImVec2(at(f0), a.y), ImVec2(at(std::min(end, entries)), b.y), t.accent);
        if (end > entries) dl->AddRectFilled(ImVec2(at(0), a.y), ImVec2(at(end - entries), b.y), t.accent);
    }
    dl->AddRect(a, b, t.controlStroke);
    float cx = at(f0), hw = floorf(mark * 0.6f);
    dl->AddTriangleFilled(ImVec2(cx - hw, p0.y), ImVec2(cx + hw, p0.y), ImVec2(cx, p0.y + mark), t.text);
    float wx = at(wptr & mask);
    dl->AddLine(ImVec2(wx, a.y - ui::Px(1)), ImVec2(wx, b.y + ui::Px(2)), t.caution, th * 2);
    if (hovered) {
        int e = (int)((ImGui::GetIO().MousePos.x - a.x) * entries / w);
        e = e < 0 ? 0 : e > mask ? mask : e;
        bool used = n > 0 && ((e - f0) & mask) < n;
        char tip[512];
        int k = textf(tip, sizeof(tip), ts(used ? TS_G3D_RAM_TIP_USED : TS_G3D_RAM_TIP_FREE), name, e);
        if (k > 0 && k < (int)sizeof(tip)) textf(tip + k, sizeof(tip) - k, "\n%s", ts(ext ? TS_G3D_RAM_LEGEND_EXT : TS_G3D_RAM_LEGEND));
        ui::ItemTooltip(tip);
    }
}

void Geo3DPanel::card(const VdpDebugState* s)
{
    const Geo3DDebugState& g = s->geo3d;
    int id = g.regs[0x49];
    bool faces = facesMode(g);
    if (ui::BeginKeyValue("g3d")) {
        ui::KeyValue(ts(TS_G3D_KV_VERSION), "v%d.%d (ID %02Xh)", id >> 4, id & 15, id);
        ui::KeyValue(ts(TS_G3D_KV_PORTS), ts(g.cartridge ? TS_G3D_PORTS_CART : TS_G3D_PORTS_MACHINE), g.port, g.port + 2);
        if (g.running) ui::KeyValue("RUN", ts(TS_G3D_RUN_BUSY), g.cmdsIssued, g.cmdsTotal);
        else ui::KeyValue("RUN", "%s", ts(TS_G3D_RUN_IDLE));
        if (g.runSeq == 0) ui::KeyValue(ts(TS_G3D_KV_FRAME), "%s", ts(TS_G3D_FRAME_NEVER));
        else if (g.fallback || g.frameFirst >= g.runCount) ui::KeyValue(ts(TS_G3D_KV_FRAME), ts(TS_G3D_FRAME_NONE), (unsigned)g.runSeq);
        else ui::KeyValue(ts(TS_G3D_KV_FRAME), ts(TS_G3D_FRAME_RUNS), g.runCount - g.frameFirst + g.runsLost,
                          (unsigned)(g.run[g.frameFirst].seq - g.runsLost), (unsigned)g.run[g.runCount - 1].seq);
        ui::KeyValue(ts(TS_KV_MODE), "%s", modeName(g));
        /* The latest RUN's counters, named for what each counts in its mode. */
        ui::KeyValue(ts(TS_G3D_CNT_DRAWN), "%u", (unsigned)g.drawn);
        ui::KeyValue(ts(faces ? TS_G3D_CNT_BACK : TS_G3D_CNT_OFFSCREEN), "%u", (unsigned)g.culled);
        ui::KeyValue(ts(faces ? TS_G3D_CNT_NEAR : TS_G3D_CNT_NEAR_UNCUT), "%u", (unsigned)g.skipped);
        ui::EndKeyValue();
    }
    bool ext = g.ext != 0;
    char head[160];
    textf(head, sizeof(head), ts(ext ? TS_G3D_RAM_HEAD_EXT : TS_G3D_RAM_HEAD), GEO3D_DEBUG_ENTRIES);
    ui::SectionTitle(head);
    ramBar("##g3dv", ts(TS_G3D_RAM_VERTICES), g.vfirst, g.nvert, g.vaddr, ext);
    ramBar("##g3de", ts(TS_G3D_RAM_EDGES), g.efirst, g.nedge, g.eaddr, ext);
    ramBar("##g3df", ts(TS_G3D_RAM_FACES), g.ffirst, g.nface, g.faddr, ext);
}

/* The picture's RUNs from above in camera space, x across and z up, with the latest RUN's
** view edges through x = 0 and W and ZNEAR across: here the extent and that view, before
** the canvas. False with nothing to place or all of it at one Z. */
bool Geo3DPanel::topPrepare(const VdpDebugState* s)
{
    const Geo3DDebugState& g = s->geo3d;
    int shown = 0, last = -1, n = (int)set.size();
    auto shows = [](const Geo3DDebugItem& it) { return it.outcome == GEO3D_ITEM_DRAWN || it.outcome == GEO3D_ITEM_NEAR; };
    for (int r = 0; r < n; r++) {
        if (set[r].run->itemCount > 0) last = r;
    }
    const Int32* lc = last >= 0 ? set[last].run->cfg : g.cfg;
    double f = lc[12], cx = lc[13], w = lc[16], znear = lc[15];
    double x0 = 0, x1 = 0, z0 = std::min(0.0, znear), z1 = std::max(1.0, znear);
    int zlo = INT_MAX, zhi = INT_MIN;
    /* The fit leaves out vertices behind the camera; their lines are cut at the edge. */
    for (int r = 0; r < n; r++) {
        const Geo3DDebugRun& run = *set[r].run;
        for (int k = 0; k < run.itemCount; k++) {
            const Geo3DDebugItem& it = set[r].item[k];
            if (!shows(it)) continue;
            for (int j = 0; j < (run.faces ? 4 : 2); j++) {
                double x = it.cx[j], z = it.cz[j];
                zlo = std::min(zlo, it.cz[j]);
                zhi = std::max(zhi, it.cz[j]);
                if (z <= 0) continue;
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                z0 = std::min(z0, z);
                z1 = std::max(z1, z);
            }
            shown++;
        }
    }
    /* Every vertex at one Z, as when the program projects them itself, is a line from above:
    ** a note instead, and the fit is left as it was. */
    topFlat = shown > 0 && zlo == zhi;
    topAny = shown > 0 && !topFlat;
    if (!topAny) {
        farHold.touch(true);
        return false;
    }
    topCam[0] = f;
    topCam[1] = cx;
    topCam[2] = w;
    topCam[3] = znear;
    /* The far side follows what is shown: at once further, nearer only once all has stayed
    ** within 80% of it for 1.5 s, to 10% past the furthest. A new page or a hidden tab starts over. */
    bool running = emulatorGetState() == EMU_RUNNING || emulatorGetState() == EMU_SUSPENDED;
    if (!fitOn || farHold.stale()) farHold.reset(z1);
    z1 = std::max(z1, farHold.step(z1, running, running, 0, 0.8, 1.1));
    /* The view's edges reach the far side. */
    if (f > 0) {
        x0 = std::min(x0, (0 - cx) * z1 / f);
        x1 = std::max(x1, (w - cx) * z1 / f);
    }
    fitOn = true;
    fit[0] = x0;
    fit[1] = x1;
    fit[2] = z0;
    fit[3] = z1;
    return true;
}

/* The top view in a..b on the canvas, inside Canvas's child: its own scale, the same across
** and up, under its title; the item under the mouse is left in topHotR, topHotI. */
void Geo3DPanel::topDraw(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    const UiTokens& t = uiTokens();
    auto shows = [](const Geo3DDebugItem& it) { return it.outcome == GEO3D_ITEM_DRAWN || it.outcome == GEO3D_ITEM_NEAR; };
    float pad = ui::Px(8), th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
    dl->PushClipRect(a, b, true);
    ImVec2 ca = dl->GetClipRectMin(), cb = dl->GetClipRectMax(), mouse = ImGui::GetIO().MousePos;
    bool hovered = ImGui::IsWindowHovered() && !cv.panning && mouse.x >= ca.x && mouse.x < cb.x && mouse.y >= ca.y && mouse.y < cb.y;
    dl->AddRectFilled(a, b, t.controlFill, ui::Px(4));
    ui::TextFit(dl, ImVec2(a.x + pad, a.y + pad / 2), b.x - pad, ts(TS_G3D_TOP), t.textSecondary,
                hovered && mouse.y < a.y + pad / 2 + lh);
    ImVec2 ia(a.x + pad, a.y + pad + lh), ib(b.x - pad, b.y - pad);
    if (!topAny) {
        const char* none = ts(topFlat ? TS_G3D_TOP_FLAT : TS_G3D_TOP_NONE);
        float nw = ImGui::CalcTextSize(none).x;
        float x = std::max(ia.x, floorf((ia.x + ib.x - nw) / 2));
        ui::TextFit(dl, ImVec2(x, floorf((ia.y + ib.y - lh) / 2)), ib.x, none, t.textSecondary, false);
    }
    if (!topAny || ib.x - ia.x < 1 || ib.y - ia.y < 1) {
        dl->PopClipRect();
        dl->AddRect(a, b, t.controlStroke, ui::Px(4));
        return;
    }
    int n = (int)set.size();
    double f = topCam[0], cx = topCam[1], w = topCam[2], znear = topCam[3];
    double x0 = fit[0], x1 = fit[1], z0 = fit[2], z1 = fit[3];
    double aw = ib.x - ia.x, ah = ib.y - ia.y;
    double spanX = std::max(x1 - x0, 1.0), spanZ = std::max(z1 - z0, 1.0);
    double k = std::min(aw / spanX, ah / spanZ);
    double ox = ia.x + aw * 0.5 - (x0 + x1) * 0.5 * k, oy = ia.y + ah * 0.5 + (z0 + z1) * 0.5 * k;
    auto at = [&](double x, double z) { return ImVec2((float)(ox + x * k), (float)(oy - z * k)); };

    /* The topmost item under the mouse: a later one over an earlier. */
    int hotR = -1, hotI = -1;
    for (int r = 0; hovered && r < n; r++) {
        const Geo3DDebugRun& run = *set[r].run;
        for (int q = 0; q < run.itemCount; q++) {
            int nv = run.faces ? 4 : 2;
            const Geo3DDebugItem& it = set[r].item[q];
            if (!shows(it)) continue;
            ImVec2 v[4];
            for (int j = 0; j < nv; j++) {
                v[j] = at(it.cx[j], it.cz[j]);
            }
            for (int j = 0; j < (nv == 4 ? 4 : 1); j++) {
                if (pointSegment(mouse, v[j], v[(j + 1) % nv]) > ui::Px(4)) continue;
                hotR = r;
                hotI = q;
                break;
            }
        }
    }

    ImVec2 eye = at(0, 0);
    /* The view, light blue: through x = 0 and x = W out to the far end of the fit. */
    if (f > 0) {
        ImU32 view = t.dark ? IM_COL32(138, 180, 248, 255) : IM_COL32(30, 136, 229, 255);
        ImVec2 l = at((0 - cx) * z1 / f, z1), r = at((w - cx) * z1 / f, z1);
        dl->AddTriangleFilled(eye, l, r, ui::WithAlpha(view, 26));
        dl->AddTriangle(eye, l, r, view, th);
    }
    float zy = at(0, znear).y;
    /* One brown in both themes, as the heading names it by its colour. */
    dl->AddLine(ImVec2(a.x, zy), ImVec2(b.x, zy), IM_COL32(176, 112, 32, 255), th * 2);
    dl->AddCircleFilled(eye, ui::Px(3), t.text);
    int pinR = -1, pinI = -1;
    findInSet(pinSeq, pinIndex, &pinR, &pinI);
    for (int pass = 0; pass < 2; pass++) {
        for (int r = 0; r < n; r++) {
            const Geo3DDebugRun& run = *set[r].run;
            for (int q = 0; q < run.itemCount; q++) {
                int nv = run.faces ? 4 : 2;
                const Geo3DDebugItem& it = set[r].item[q];
                bool pin = r == pinR && q == pinI, hot = (r == hotR && q == hotI) || (hotItem(r, q) && hotI < 0);
                /* A face turned away only while the picture's mouse or pin is on it, faintly. */
                bool back = run.faces && it.outcome == GEO3D_ITEM_BACK && (pin || hot);
                if ((!shows(it) && !back) || (pass == 1) != (pin || hot)) continue;
                ImU32 col = pin ? t.accent : hot ? t.accentHover : ui::WithAlpha(t.text, it.outcome == GEO3D_ITEM_NEAR ? 110 : 150);
                if (back) col = ui::WithAlpha(col, 140);
                float lw = (pin || hot) && !back ? th * 2 : th;
                ImVec2 v[4];
                for (int j = 0; j < nv; j++) {
                    v[j] = at(it.cx[j], it.cz[j]);
                }
                for (int j = 0; j < (nv == 4 ? 4 : 1); j++) {
                    if (it.outcome == GEO3D_ITEM_NEAR || back) dashedLine(dl, v[j], v[(j + 1) % nv], col, lw, ui::Px(3));
                    else dl->AddLine(v[j], v[(j + 1) % nv], col, lw);
                }
            }
        }
    }
    dl->PopClipRect();
    dl->AddRect(a, b, t.controlStroke, ui::Px(4));
    topHotR = hotR;
    topHotI = hotI;
}

/* A face's normal and the light, both in model space, at the angle between them; for a
** drawn face, the level of light it got as a bar of the 6 above 0. */
void Geo3DPanel::lightPicture(const VdpDebugState* s, const Geo3DDebugRun& run, const Geo3DDebugItem& it)
{
    const Geo3DDebugState& g = s->geo3d;
    const UiTokens& t = uiTokens();
    const Geo3DDebugFace& fc = g.fmem[it.index & (GEO3D_DEBUG_ENTRIES - 1)];
    if (ui::BeginKeyValue("g3dlight")) {
        ui::KeyValueMono(ts(TS_G3D_NORMAL), "%d, %d, %d", fc.n[0], fc.n[1], fc.n[2]);
        ui::KeyValueMono(ts(TS_G3D_KV_LIGHT_MODEL), "%d, %d, %d", (int)run.lm[0], (int)run.lm[1], (int)run.lm[2]);
        ui::EndKeyValue();
    }
    double n[3] = { (double)fc.n[0], (double)fc.n[1], (double)fc.n[2] };
    double l[3] = { (double)run.lm[0], (double)run.lm[1], (double)run.lm[2] };
    double nn = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), ll = sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    float pw = ui::Px(120), ph = ui::Px(72), th = fmaxf(1.0f, ui::Px(1));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(pw, ph));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + pw, p0.y + ph), t.controlFill, ui::Px(4));
    dl->AddRect(p0, ImVec2(p0.x + pw, p0.y + ph), t.controlStroke, ui::Px(4));
    /* From the middle, so a light from behind the face stays in the box. */
    ImVec2 o(floorf(p0.x + pw * 0.5f), floorf(p0.y + ph * 0.5f));
    float len = ph * 0.5f - ui::Px(6);
    dl->PushClipRect(p0, ImVec2(p0.x + pw, p0.y + ph), true);
    /* The face edge on, under both. */
    dl->AddLine(ImVec2(o.x - pw * 0.35f, o.y), ImVec2(o.x + pw * 0.35f, o.y), t.textSecondary, th * 2);
    ui::PushFont(UI_FONT_BODY, 12);
    if (nn > 0 && ll > 0) {
        double c = (n[0] * l[0] + n[1] * l[1] + n[2] * l[2]) / (nn * ll);
        double a = acos(c < -1 ? -1 : c > 1 ? 1 : c);
        ImVec2 tipN(o.x, o.y - len), tipL((float)(o.x + sin(a) * len), (float)(o.y - cos(a) * len));
        arrow(dl, o, tipN, t.accent, th * 2);
        arrow(dl, o, tipL, t.caution, th * 2);
        float fh = ImGui::GetFontSize();
        dl->AddText(ImVec2(tipN.x - ImGui::CalcTextSize(ts(TS_G3D_NORMAL)).x - ui::Px(4), tipN.y), t.accent, ts(TS_G3D_NORMAL));
        dl->AddText(ImVec2(std::min(tipL.x + ui::Px(4), p0.x + pw - ImGui::CalcTextSize(ts(TS_G3D_LIGHT)).x - ui::Px(2)),
                           std::max(p0.y, std::min(tipL.y, p0.y + ph - fh))), t.caution, ts(TS_G3D_LIGHT));
    }
    ui::PopFont();
    dl->PopClipRect();
    if (it.outcome != GEO3D_ITEM_DRAWN) return;
    /* The level beside: 6 cells, as many lit. Textured, it picks the texture's rows. */
    ImGui::SameLine();
    ImGui::BeginGroup();
    char line[128];
    if (it.textured) textf(line, sizeof(line), ts(TS_G3D_LEVEL_TEX), it.lvl, (run.texy + it.lvl * run.tstride) & 0x1fff);
    else textf(line, sizeof(line), ts(TS_G3D_LEVEL), it.lvl, it.col);
    ui::TextFitItem(line);
    ImVec2 c0 = ImGui::GetCursorScreenPos();
    float cw = ui::Px(14), ch = ui::Px(10), gap = ui::Px(2);
    for (int k = 0; k < 6; k++) {
        ImVec2 a(c0.x + k * (cw + gap), c0.y), e(a.x + cw, a.y + ch);
        dl->AddRectFilled(a, e, k < it.lvl ? t.accent : t.controlFill, ui::Px(2));
        dl->AddRect(a, e, t.controlStroke, ui::Px(2));
    }
    ImGui::Dummy(ImVec2(6 * (cw + gap), ch));
    ImGui::EndGroup();
}

/* The rows and spans a face's commands fill, across W; worked out where they were cut. */
void Geo3DPanel::spanStrip(const Geo3DDebugRun& run, const Geo3DDebugItem& it, const Geo3DDebugCmd* cmd)
{
    const UiTokens& t = uiTokens();
    int w = run.cfg[16] > 0 ? run.cfg[16] : 256, rows = it.ymax - it.ymin + 1;
    if (rows <= 0) return;
    float aw = floorf(ImGui::GetContentRegionAvail().x), ah = ui::Px(40);
    if (aw < 1) return;
    ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + aw, p0.y + ah);
    ImGui::InvisibleButton("##g3dspans", ImVec2(aw, ah));
    ui::ItemTooltip(ts(TS_G3D_SPANS_TIP));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, t.controlFill, ui::Px(2));
    float sx = aw / w, sy = ah / rows;
    auto span = [&](int y, int l, int r) {
        l = std::max(l, 0);
        r = std::min(r, w - 1);
        if (l > r || y < it.ymin || y > it.ymax) return;
        float ya = p0.y + (y - it.ymin) * sy;
        dl->AddRectFilled(ImVec2(p0.x + l * sx, ya), ImVec2(p0.x + (r + 1) * sx, ya + fmaxf(sy, 1.0f)), t.accent);
    };
    if (complete(it)) {
        for (int k = 0; k < it.cmdCount; k++) {
            int y, dy, l, r;
            if (commandSpan(cmd[it.cmdFirst + k], run.ypage, &y, &dy, &l, &r)) span(y, l, r);
        }
    }
    else {
        for (int y = it.ymin; y <= it.ymax; y++) {
            int l, r;
            if (faceSpan(it, y, w, &l, &r)) span(y, l, r);
        }
    }
    dl->AddRect(p0, p1, t.controlStroke, ui::Px(2));
}

/* The pinned face or edge: a header that lets go of it, then what its RUN made of it. */
void Geo3DPanel::selection(const VdpDebugState* s)
{
    /* Held while one face or edge stays pinned; not by pinSeq, which follows the RUNs. */
    int key[6] = { pinIndex, pinFaces, pinKey[0], pinKey[1], pinKey[2], pinKey[3] };
    float y0 = ImGui::GetCursorPosY();
    if (memcmp(key, selHoldKey, sizeof(key)) != 0) {
        selHold = ui::HeightHold();
        memcpy(selHoldKey, key, sizeof(key));
    }
    selectionBody(s);
    ui::HoldHeight(selHold, y0);
}

void Geo3DPanel::selectionBody(const VdpDebugState* s)
{
    const RunRec* rec;
    int k;
    if (!findItem(pinSeq, pinIndex, &rec, &k)) return;
    const Geo3DDebugState& g = s->geo3d;
    const Geo3DDebugRun& run = rec->run;
    const Geo3DDebugItem& it = rec->items[k];
    const UiTokens& t = uiTokens();
    const int mask = GEO3D_DEBUG_ENTRIES - 1;
    bool faces = run.faces != 0;
    char head[64], caption[160];
    textf(head, sizeof(head), ts(faces ? TS_G3D_SEL_FACE : TS_G3D_SEL_EDGE), it.index);
    int cl = textf(caption, sizeof(caption), ts(TS_G3D_SEL_CAPTION), (unsigned)run.seq, run.ypage);
    if (it.outcome == GEO3D_ITEM_BACK && cl > 0 && cl < (int)sizeof(caption)) {
        textf(caption + cl, sizeof(caption) - cl, " \xC2\xB7 %s", ts(TS_G3D_LEGEND_BACK));
    }

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float h = ui::Px(24), right = p0.x + ImGui::GetContentRegionAvail().x, x = p0.x, cy = floorf(p0.y + h / 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ui::HasIcons()) {
        ui::DrawPixelArt(dl, ImVec2(x + ui::Px(8), cy), cubeArt, ui::DECOR_NONE, t.text);
        x += ui::Px(24);
    }
    ui::PushFont(UI_FONT_STRONG);
    ImVec2 hs = ImGui::CalcTextSize(head);
    bool overHead = ImGui::ItemAdd(ImRect(ImVec2(x, p0.y), ImVec2(right - ui::Px(36), p0.y + h)), 0) &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    ui::TextFit(dl, ImVec2(x, floorf(cy - hs.y / 2)), right - ui::Px(36), head, t.text, overHead);
    ui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(right - ui::Px(28), p0.y));
    bool release = ui::IconButton("##g3dpin", UI_ICON_PINNED, "\xC3\x97", ts(TS_PIN_RELEASE_TIP), false, ImVec2(28, 24), 14);
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(right - p0.x, h));
    ui::PushFont(UI_FONT_BODY, 12);
    ui::TextFitItem(caption, t.textSecondary);
    ui::PopFont();
    if (release) {
        unpin();
        return;
    }

    /* The RAM may have moved on since the RUN: its vertices, the face's or edge's entry,
    ** and for a drawn face the normal its level was worked out from. */
    int nv = faces ? 4 : 2;
    bool same = placed(s, *rec);
    for (int j = 0; j < nv; j++) {
        same = same && (faces ? g.fmem[it.index & mask].i[j] : g.emem[it.index & mask][j]) == it.v[j];
    }
    if (faces && it.outcome == GEO3D_ITEM_DRAWN) same = same && lightLevel(g.fmem[it.index & mask].n, run.lm) == it.lvl;
    if (!same) ui::TextFitItem(ts(TS_G3D_SEL_STALE_SHORT), t.caution, true, ts(TS_G3D_SEL_STALE));
    if (faces && it.outcome == GEO3D_ITEM_DRAWN && ui::BeginKeyValue("g3dsel")) {
        ui::KeyValue(ts(TS_G3D_KV_ORDER), ts(TS_G3D_ORDER_VALUE), it.order + 1, (int)run.drawn);
        ui::EndKeyValue();
    }
    /* Narrow: vertex k and its number, then x,y,z and sx,sy without padding. A vertex the
    ** RUN could not project has no sx,sy. */
    if (ImGui::BeginTable("g3dverts", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings |
                                         ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn(ts(TS_G3D_COL_VERTEX));
        ImGui::TableSetupColumn(ts(TS_G3D_COL_MODEL));
        ImGui::TableSetupColumn(ts(TS_G3D_COL_SCREEN));
        ImGui::TableHeadersRow();
        for (int j = 0; j < nv; j++) {
            const Int16* m = g.vmem[it.v[j] & mask];
            bool lost = it.outcome == GEO3D_ITEM_NEAR && (geo3dDebugProject(run.cfg, m).flags & 7) != 0;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d: %d", j, it.v[j]);
            ImGui::TableNextColumn();
            ImGui::Text("%d,%d,%d", m[0], m[1], m[2]);
            ImGui::TableNextColumn();
            if (lost) ImGui::TextUnformatted("\xE2\x80\x94");
            else ImGui::Text("%d,%d", it.sx[j], it.sy[j]);
        }
        ImGui::EndTable();
    }
    if (faces) {
        ui::SectionTitle(ts(TS_G3D_SEL_LIGHT));
        lightPicture(s, run, it);
    }
    if (it.textured) {
        /* The four UVs in their 256 x 256 square, in vertex order. */
        const UInt8* uv = g.tmem[it.index & mask];
        ui::SectionTitle(ts(TS_G3D_SEL_UV));
        float sz = ui::Px(72), th = fmaxf(1.0f, ui::Px(1));
        ImVec2 a = ImGui::GetCursorScreenPos(), e(a.x + sz, a.y + sz);
        ImGui::Dummy(ImVec2(sz, sz));
        dl->AddRectFilled(a, e, t.controlFill);
        dl->AddRect(a, e, t.controlStroke);
        ImVec2 q[4];
        for (int j = 0; j < 4; j++) q[j] = ImVec2(a.x + (uv[2 * j] + 0.5f) * sz / 256, a.y + (uv[2 * j + 1] + 0.5f) * sz / 256);
        for (int j = 0; j < 4; j++) dl->AddLine(q[j], q[(j + 1) & 3], t.accent, th * 2);
        dl->AddCircleFilled(q[0], ui::Px(3), t.accent);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ui::PushFont(UI_FONT_MONO);
        for (int j = 0; j < 4; j++) ImGui::Text("%d: u %3d  v %3d", j, uv[2 * j], uv[2 * j + 1]);
        ui::PopFont();
        ImGui::EndGroup();
    }
    ui::SectionTitle(ts(TS_G3D_SEL_CMDS));
    if (ui::BeginKeyValue("g3dcmds")) {
        if (it.cmdCount < it.cmdTotal) ui::KeyValue(ts(TS_G3D_KV_CMD_COUNT), ts(TS_G3D_CMDS_CUT), it.cmdTotal, it.cmdCount);
        else ui::KeyValue(ts(TS_G3D_KV_CMD_COUNT), "%d", it.cmdTotal);
        if (it.cmdCount > 0 && complete(it)) {
            const Geo3DDebugCmd& c = rec->cmds[it.cmdFirst];
            int dx, dy, nx, ny, sx, sy;
            commandFields(c, &dx, &dy, &nx, &ny, &sx, &sy);
            char k1[64], k2[64];
            textf(k1, sizeof(k1), ts(TS_G3D_KV_FIRST), "DX, DY, NX");
            textf(k2, sizeof(k2), ts(TS_G3D_KV_FIRST), "SX, SY");
            if (c.size == 19) {
                ui::KeyValueMono(k1, "%d, %d, %d", dx, dy, nx);
                ui::KeyValueMono(k2, "%d, %d", sx, sy);
            }
            else if (faces) ui::KeyValueMono(k1, "%d, %d, %d", dx, dy, nx);
            else ui::KeyValueMono("DX, DY, NX, NY", "%d, %d, %d, %d", dx, dy, nx, ny);
        }
        if (faces) {
            if (it.ymin <= it.ymax) ui::KeyValue(ts(TS_G3D_KV_ROWS), "%d-%d", it.ymin, it.ymax);
            else ui::KeyValue(ts(TS_G3D_KV_ROWS), "%s", ts(TS_G3D_ROWS_NONE));
        }
        ui::EndKeyValue();
    }
    if (faces && it.ymin <= it.ymax) spanStrip(run, it, rec->cmds.data());
}

/* While geo3d waits for the engine to finish the command it issued last: which one and
** what for. A Z80 command in between is told apart only where its CMD or CLR differs. */
const char* Geo3DPanel::commandNote(const VdpDebugState* s)
{
    const Geo3DDebugState& g = s->geo3d;
    if (!g.valid || !g.running || g.devPhase != GEO3D_PHASE_WAIT_CE || g.cmdsIssued <= 0 || !(s->status[2] & 1)) return NULL;
    for (int r = 0; r < g.runCount; r++) {
        const Geo3DDebugRun& run = g.run[r];
        if (run.seq != g.runSeq) continue;
        int k = g.cmdsIssued - 1, lo = (int)run.cmdTotal - run.cmdCount;
        if (k < lo || k >= (int)run.cmdTotal) return NULL;
        int c = run.cmdFirst + k - lo;
        const Geo3DDebugCmd& cmd = g.cmd[c];
        if (cmd.size != 11 && cmd.size != 19) return NULL;
        int clr = cmd.size == 19 ? cmd.bytes[12] : cmd.bytes[8];
        if ((s->cmdRegs[14] >> 4) != (cmd.bytes[cmd.size - 1] >> 4) || s->cmdRegs[12] != clr) return NULL;
        for (int q = 0; q < run.itemCount; q++) {
            const Geo3DDebugItem& it = g.item[run.itemFirst + q];
            if (c < it.cmdFirst || c >= it.cmdFirst + it.cmdCount) continue;
            textf(cmdText, sizeof(cmdText), ts(cmd.size == 19 ? TS_G3D_CMD_TEX : run.faces ? TS_G3D_CMD_FILL : TS_G3D_CMD_EDGE),
                  it.index, (unsigned)run.seq, k + 1, (int)run.cmdTotal);
            return cmdText;
        }
        return NULL;
    }
    return NULL;
}

/* The configuration words and the other write-only words, by the index of their low byte. */
void Geo3DPanel::wordTable(const VdpDebugState* s)
{
    static const char* const cfgNames[18] = { "M00", "M01", "M02", "M10", "M11", "M12", "M20", "M21", "M22",
                                              "TX", "TY", "TZ", "F", "CX", "CY", "ZNEAR", "W", "H" };
    struct Word { int index; const char* name; Int32 value; };
    const Geo3DDebugState& g = s->geo3d;
    const UiTokens& t = uiTokens();
    Word words[32];
    int n = 0;
    for (int i = 0; i < 18; i++) words[n++] = Word{ 2 * i, cfgNames[i], g.cfg[i] };
    for (int i = 0; i < 3; i++) words[n++] = Word{ 0x24 + 2 * i, i == 0 ? "VX" : i == 1 ? "VY" : "VZ", g.ivtx[i] };
    for (int i = 0; i < 3; i++) words[n++] = Word{ 0x5a + 2 * i, i == 0 ? "LX" : i == 1 ? "LY" : "LZ", g.light[i] };
    words[n++] = Word{ 0x60, "TEXX", g.texx };
    words[n++] = Word{ 0x62, "TEXY", g.texy };
    words[n++] = Word{ 0x64, "TSTRIDE", g.tstride };

    /* Rows that changed with the last content fade from the accent, as the register rows do. */
    WordFade& fd = wordFade;
    unsigned v = shell::contentVersion();
    float dt = shell::toolDecayDt() / 0.8f;
    if (fd.step != shell::stepVersion()) {
        fd.step = shell::stepVersion();
        memset(fd.heat, 0, sizeof(fd.heat));
    }
    for (int k = 0; k < n; k++) fd.heat[k] = fd.heat[k] > dt ? fd.heat[k] - dt : 0;
    if (!fd.primed || fd.version != v) {
        for (int k = 0; k < n; k++) {
            if (fd.primed && fd.prev[k] != words[k].value) fd.heat[k] = 1;
            fd.prev[k] = words[k].value;
        }
        fd.version = v;
        fd.primed = true;
    }
    if (!ImGui::BeginTable("g3dwords", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings |
                                          ImGuiTableFlags_NoHostExtendX)) {
        return;
    }
    ui::PushFont(UI_FONT_MONO);
    for (int k = 0; k < n; k++) {
        ImGui::TableNextRow();
        if (fd.heat[k] > 0) {
            ImU32 a = (ImU32)(fd.heat[k] * (t.dark ? 90 : 64));
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ui::WithAlpha(t.accent, (int)a));
        }
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::Text("%02Xh", words[k].index);
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(words[k].name);
        ImGui::TableNextColumn();
        ImGui::Text("%6d", (int)words[k].value);
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::Text("%04X", (unsigned)words[k].value & 0xffff);
        ImGui::PopStyleColor();
    }
    ui::PopFont();
    ImGui::EndTable();
}

void Geo3DPanel::registersCard(const VdpDebugState* s)
{
    ui::SectionTitle(ts(TS_G3D_WORDS));
    wordTable(s);
    ui::SectionTitle(ts(TS_G3D_READ_REGS));
    drawRegBank(regBankGeo3D, s->geo3d.regs, regFade);
}

namespace {

/* Painted 1, 2, 3 and 4 or more times. */
const ImU32 heatColours[4] = { IM_COL32(255, 226, 110, 140), IM_COL32(255, 168, 60, 175), IM_COL32(236, 96, 40, 205),
                               IM_COL32(196, 24, 40, 235) };

}

/* The heat image over the rows the picture shows: of the picture on screen, from its own
** row; of one no longer there, from the row of the page it showed on. */
void Geo3DPanel::buildHeat(int h, int screenMode, UInt32 frameStart)
{
    const Geo3DPerfStrip::Overdraw* od = fillMode == 1 ? perfStrip.overdraw() : NULL;
    heatShown = false;
    bool running = emulatorGetState() == EMU_RUNNING || emulatorGetState() == EMU_SUSPENDED;
    /* The trace and the snapshot drift a few frames apart while running: the heat seen over
    ** this page in the last ten frames or so stands in rather than the dark ground. */
    auto holdLast = [&]() {
        if (fillMode != 1 || !running || !perfStrip.following() || pageMode != 0 || heatHeld == 0 || heatPic.h != h ||
            heatPic.w != pic.w || heatHeldBase != picStart || (UInt32)(frameStart - heatHeldAt) > boardFrequency() / 6) {
            return false;
        }
        heatBase = heatHeldBase;
        heatNumber = heatHeld;
        heatOnScreen = true;
        heatShown = true;
        return true;
    };
    if (od == NULL || od->w != pic.w || od->rows != rows || rows <= 0) {
        holdLast();
        return;
    }
    /* The trace counts pages as the screen does, 32 kB up to SCREEN 6 and 64 kB from 7, in
    ** rows of the engine's mode: 128 bytes in its SCREEN 5 and 6, 256 in 7 and 8. */
    int rowsPerPage = (screenMode <= 6 ? 0x8000 : 0x10000) / (mode == 5 || mode == 6 ? 128 : 256);
    int odPage = pair && od->page >= 0 ? od->page & ~1 : od->page;
    heatBase = odPage >= 0 ? (odPage * rowsPerPage) % rows : picStart;
    /* A picture is settled by the capture where its page first shows, so it goes over the
    ** page whenever it is the one on screen in this snapshot. */
    bool onScreen = running ? perfStrip.onScreenAt(od->number, frameStart) : perfStrip.isLatest(od->number);
    /* The trace runs ahead of the snapshot: keep the heat of the picture still on screen. */
    if (running && !onScreen && perfStrip.following() && heatHeld != 0 && heatPic.h == h && heatPic.w == pic.w && heatHeldBase == picStart &&
        perfStrip.onScreenAt(heatHeld, frameStart)) {
        heatBase = heatHeldBase;
        heatNumber = heatHeld;
        heatOnScreen = pageMode == 0;
        heatShown = true;
        heatHeldAt = frameStart;
        return;
    }
    heatOnScreen = pageMode == 0 && onScreen && heatBase == picStart;
    if (!heatOnScreen && holdLast()) return;
    heatHeldAt = frameStart;
    heatHeld = heatOnScreen ? od->number : 0;
    heatHeldBase = heatBase;
    heatNumber = od->number;
    char key[64];
    snprintf(key, sizeof(key), "h%u/%d/%d/%d/%d", od->number, heatBase, h, od->w, rows);
    bool decoded = heatPic.needsDecodeAt(0, key);
    if (decoded) {
        heatPic.w = od->w;
        heatPic.h = h;
        heatPic.img.assign((size_t)od->w * h, 0);
        for (int y = 0; y < h; y++) {
            const UInt8* c = &od->count[(size_t)((heatBase + y) % rows) * od->w];
            uint32_t* d = &heatPic.img[(size_t)y * od->w];
            for (int x = 0; x < od->w; x++) d[x] = c[x] ? heatColours[std::min((int)c[x], 4) - 1] : 0;
        }
    }
    heatPic.upload(decoded);
    heatShown = true;
}

/* The legend row in overdraw: a swatch for each count; what the counts leave out is in a
** tooltip on them. */
ImVec2 Geo3DPanel::heatLegend()
{
    const UiTokens& t = uiTokens();
    float lh = ImGui::GetTextLineHeight(), w = std::max(1.0f, floorf(ImGui::GetContentRegionAvail().x));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, lh));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float sw = floorf(lh * 0.7f), in = floorf((lh - sw) / 2), x = p.x;
    char text[64];
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + lh), true);
    for (int k = 0; k < 4; k++) {
        dl->AddRectFilled(ImVec2(x, p.y + in), ImVec2(x + sw, p.y + in + sw), ui::Over(heatColours[k], IM_COL32(20, 20, 20, 255)));
        x += sw + ui::Px(4);
        if (k == 0) textf(text, sizeof(text), "%s", ts(TS_G3D_HEAT_ONCE));
        else if (k < 3) textf(text, sizeof(text), ts(TS_G3D_HEAT_TIMES), k + 1);
        else textf(text, sizeof(text), "%s", ts(TS_G3D_HEAT_MORE));
        dl->AddText(ImVec2(x, p.y), t.text, text);
        x += ImGui::CalcTextSize(text).x + ui::Px(14);
    }
    dl->PopClipRect();
    /* What the counts leave out, on hover over the swatches. */
    if (ImGui::IsMouseHoveringRect(p, ImVec2(x, p.y + lh))) ui::ItemTip(ts(TS_G3D_HEAT_LEG_NOTE));
    return ImVec2(x, p.y);
}
