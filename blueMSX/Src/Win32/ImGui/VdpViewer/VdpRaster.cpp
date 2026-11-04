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
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "VdpRaster.h"
#include "RegTables.h"

/* Whole lines only, by the latch rules of the emulated VDP's sync and renderers; a split part way
** into a line is marked, not rebuilt. t is the cycle less (leftBorder - 20), where the emulated VDP
** starts laying the line out. */

static int leftBorderOf(int mode, const UInt8* r)
{
    /* As the emulated VDP's onScrModeChange: the border, less the R#18 H adjust, plus 4 for YJK. */
    int lb = mode == 0 || mode == 13 ? 102 + 92 : 102 + 56;
    lb += vdpSetAdjust(r[18], false);
    if (r[25] & 0x08) lb += 4;
    return lb;
}

/* The mode R#0, R#1 and R#25 select, as the emulated VDP's updateScreenMode; fallback otherwise. */
static int modeOf(const UInt8* r, int fallback)
{
    static const signed char modes[32] = {
        1, 2, 4, 5, 6, 7, -1, 8, 3, -1, -1, -1, -1, -1, -1, -1,
        0, -1, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };
    int m = modes[((r[0] & 0x0e) >> 1) | (r[1] & 0x18)];
    if (m < 0) return fallback;
    if ((m == 7 || m == 8) && (r[25] & 0x08)) m = (r[25] & 0x10) ? 10 : 12;
    return m;
}

/* The mode the emulated VDP compares for a text split: as modeOf, TEXT1 mixed with M2 or M3 as 0, others as 1. */
static int splitModeOf(const UInt8* r)
{
    int m = modeOf(r, -1);
    if (m >= 0) return m;
    int key = ((r[0] & 0x0e) >> 1) | (r[1] & 0x18);
    return key == 0x11 || key == 0x18 || key == 0x19 ? 0 : 1;
}

static bool chunkMode(int mode)
{
    return mode == 5 || mode == 7 || mode == 8 || mode == 10 || mode == 12;
}

/* The register table's name for the registers a write mark names; "" for the others. */
const char* vdpRegisterName(int reg)
{
    switch (reg) {
    case 0: case 1: case 2: case 7: case 8: case 9: case 13:
    case 18: case 19: case 23: case 25: case 26: case 27: {
        const RegDesc* d = regBankFind(regBankV99x8, reg);
        return d != NULL ? d->name : "";
    }
    }
    return "";
}

struct Replay {
    std::vector<VdpRasterMark> marks;   /* by line, then cycle */
    std::vector<int>           byFrom;  /* REG marks by fromLine, for one walk down the lines */
    UInt8 start[64];
    int   field, blinkFlag;
};

static void replay(const VdpDebugState* s, const VdpRasterFrame& f, int lines, bool prev, Replay* out, UInt8 end[64])
{
    UInt8 r[64], eff[64];
    memcpy(r, f.startRegs, sizeof(r));
    memcpy(eff, f.startRegs, sizeof(eff));
    memcpy(out->start, f.startRegs, sizeof(out->start));
    out->field = f.field;
    out->blinkFlag = f.blinkFlag;
    out->marks.clear();
    out->byFrom.clear();
    for (int i = 0; i < f.count && i < VDP_RASTER_MAX; i++) {
        const VdpRasterEvent& e = f.ev[i];
        VdpRasterMark m = { e.line, e.cycle, e.kind, e.reg, e.value, e.aux, e.line, -1, prev };
        out->marks.push_back(m);
    }
    /* Line interrupts are noted a little late, so order by time first. */
    std::stable_sort(out->marks.begin(), out->marks.end(), [](const VdpRasterMark& a, const VdpRasterMark& b) {
        return a.line != b.line ? a.line < b.line : a.cycle < b.cycle;
    });
    std::vector<int> pending;   /* REG marks not yet in eff, by index */
    for (size_t i = 0; i < out->marks.size(); i++) {
        VdpRasterMark& m = out->marks[i];
        /* eff: the registers the VDP lays line m.line out with so far. */
        for (size_t k = 0; k < pending.size();) {
            const VdpRasterMark& p = out->marks[pending[k]];
            if (p.fromLine <= m.line) {
                eff[p.reg & 63] = (UInt8)p.value;
                pending.erase(pending.begin() + k);
            }
            else k++;
        }
        if (m.kind != VDP_RASTER_REG) continue;
        int mode = modeOf(eff, s->screenMode);
        int t = m.cycle - (leftBorderOf(mode, eff) - 20);
        int chunk = (t + 32) / 32 - 1;
        int change = m.aux ^ m.value;
        if (m.reg == 0 || m.reg == 1 || m.reg == 18 || (m.reg == 25 && (change & ~0x02))) {
            /* Mode, adjust and R#25 changes wait for the next line (scheduleScrModeChange). */
            m.fromLine = m.line + 1;
        }
        else if (m.reg == 25 || ((m.reg == 2 || m.reg == 23) && chunkMode(mode))) {
            /* Read again every 8 dots (MSK at the first); the first 32 cycles draw only the border. */
            m.fromLine = chunk <= 0 ? m.line : m.line + 1;
            bool shown = m.line >= f.firstLine && m.line < f.firstLine + lines;
            if (m.reg != 25 && shown && chunk >= 1 && chunk <= 31) m.splitDot = chunk * 8;
        }
        else {
            m.fromLine = t < 0 ? m.line : m.line + 1;
        }
        pending.push_back((int)i);
        out->byFrom.push_back((int)i);
        r[m.reg & 63] = (UInt8)m.value;
    }
    std::stable_sort(out->byFrom.begin(), out->byFrom.end(), [out](int a, int b) {
        return out->marks[a].fromLine < out->marks[b].fromLine;
    });
    if (end) memcpy(end, r, 64);
}

/* Registers line by line down the frame, one walk per replay, with the text split a mode change sets.
** A mode write on a frame's last line takes effect at the next frame's line 0, unseen as a split. */
struct Walker {
    const Replay* rp;
    UInt8  r[64];
    size_t at;
    int    firstLine, mode, split;
    void   init(const Replay* p, int first)
    {
        rp = p;
        memcpy(r, p->start, 64);
        at = 0;
        firstLine = first;
        mode = splitModeOf(r);
        split = 0;
    }
    void   to(int L)
    {
        while (at < rp->byFrom.size() && rp->marks[rp->byFrom[at]].fromLine <= L) {
            int from = rp->marks[rp->byFrom[at]].fromLine;
            while (at < rp->byFrom.size() && rp->marks[rp->byFrom[at]].fromLine == from) {
                const VdpRasterMark& m = rp->marks[rp->byFrom[at++]];
                r[m.reg & 63] = (UInt8)m.value;
            }
            int now = splitModeOf(r);
            if (now != mode) split = (from - firstLine) & ~7;
            mode = now;
        }
    }
};

static VdpBand bandOf(const VdpDebugState* s, const Replay& rp, const UInt8* r, int d, bool prev, int split)
{
    VdpBand b;
    int mode = modeOf(r, -1);
    b.mode = mode;
    b.first = b.last = d;
    b.page = -1;
    b.evenSide = false;
    b.flat = false;
    b.field = rp.field & 1;
    if (vdpIsBitmapMode(mode)) {
        b.page = vdpBitmapPage(s, r, mode);
        b.flat = vdpIsFlatInterlace(s, r);
        /* The emulated VDP clears the page's low bit (A15, A16 under SP3) for EO on fields with
        ** S#2 bit 1 clear, and in blink's off phase; FIL ignores both. */
        b.evenSide = !b.flat && (b.page & 1) && (((r[9] & 0x04) && !(rp.field & 1)) || (r[13] != 0 && !rp.blinkFlag));
    }
    memcpy(b.regs, r, sizeof(b.regs));
    b.prev = prev;
    b.unknown = false;
    b.blink = rp.blinkFlag;
    b.textSplit = split;
    return b;
}

bool vdpRasterSameKey(const VdpBand& a, const VdpBand& b)
{
    /* Table bases, scroll and adjust in full; R#1 sprite size and magnification, R#8 TP. */
    static const int whole[] = { 2, 3, 4, 5, 6, 10, 11, 18, 23, 25, 26, 27 };
    for (int reg : whole) {
        if (a.regs[reg] != b.regs[reg]) return false;
    }
    if ((a.regs[1] ^ b.regs[1]) & 0x03) return false;
    if ((a.regs[8] ^ b.regs[8]) & 0x20) return false;
    if (a.flat != b.flat || (a.flat && a.field != b.field)) return false;
    return a.mode == b.mode && a.page == b.page && a.evenSide == b.evenSide && a.textSplit == b.textSplit && a.prev == b.prev &&
           a.unknown == b.unknown;
}

void vdpRasterBuild(const VdpDebugState* s, int lines, VdpRasterView* v)
{
    const VdpRasterFrame& f0 = s->raster[0];
    const VdpRasterFrame& f1 = s->raster[1];
    Replay r0, r1;
    UInt8 end[64];

    v->valid = f0.valid != 0;
    v->complete = f0.complete != 0;
    v->ended = f0.ended != 0;
    v->foreign = f0.foreign != 0;
    v->dropped = f0.dropped;
    v->firstLine = f0.firstLine;
    v->lines = lines;
    v->beamLine = f0.valid && !f0.ended ? f0.beamLine : -1;
    v->beamCycle = f0.beamCycle;
    v->leftBorder = leftBorderOf(s->screenMode, s->regs);
    v->changes = 0;
    v->endSplit = 0;
    v->endMatches = true;
    v->prevUsed = v->prevComplete = v->prevForeign = false;
    v->marks.clear();
    v->bands.clear();
    v->bandOfLine.assign(lines > 0 ? lines : 0, 0);
    v->spritesOff.clear();
    if (!v->valid) {
        memcpy(r0.start, s->regs, 64);
        r0.field = (s->status[2] >> 1) & 1;
        r0.blinkFlag = s->blinkFlag;
        VdpBand b = bandOf(s, r0, s->regs, 0, false, 0);
        b.last = lines - 1;
        v->bands.push_back(b);
        return;
    }

    replay(s, f0, lines, false, &r0, end);
    /* R#14-17 are not logged, so they cannot be checked. */
    for (int i = 0; i < 32; i++) {
        if ((i < 14 || i > 17) && end[i] != s->regs[i]) v->endMatches = false;
    }
    bool paused = v->beamLine >= 0;
    v->prevUsed = paused && f1.valid;
    if (v->prevUsed) {
        replay(s, f1, lines, true, &r1, NULL);
        v->prevComplete = f1.complete != 0;
        v->prevForeign = f1.foreign != 0;
    }

    /* A line at the beam is the old one until the VDP has latched it. */
    auto belowBeam = [&](int L) {
        return paused && (L > v->beamLine || (L == v->beamLine && v->beamCycle < v->leftBorder - 20));
    };
    for (const VdpRasterMark& m : r0.marks) {
        if (m.kind == VDP_RASTER_REG) v->changes++;
        v->marks.push_back(m);
    }
    if (v->prevUsed) {
        for (const VdpRasterMark& m : r1.marks) {
            if (belowBeam(m.kind == VDP_RASTER_REG ? m.fromLine : m.line)) v->marks.push_back(m);
        }
    }

    Walker w0, w1;
    w0.init(&r0, f0.firstLine);
    if (v->prevUsed) w1.init(&r1, f1.firstLine);
    v->spritesOff.assign(lines > 0 ? lines : 0, 0);
    for (int d = 0; d < lines; d++) {
        int L = f0.firstLine + d;
        bool below = belowBeam(L);
        bool fromPrev = below && v->prevUsed;
        Walker& w = fromPrev ? w1 : w0;
        int wl = (fromPrev ? f1.firstLine : f0.firstLine) + d;
        /* Sprites of a line are built on the line above; a blank line, or one in a mode
        ** without sprites, shows none. */
        w.to(wl - 1);
        bool off = !(w.r[1] & 0x40) || (!vdpIsMsx1(s) && (w.r[8] & 0x02));
        w.to(wl);
        v->spritesOff[d] = off || !(w.r[1] & 0x40) || vdpSpriteMode(modeOf(w.r, -1)) == 0;
        VdpBand b = bandOf(s, fromPrev ? r1 : r0, w.r, d, fromPrev, w.split);
        b.unknown = below && !fromPrev;
        if (!v->bands.empty() && vdpRasterSameKey(v->bands.back(), b)) v->bands.back().last = d;
        else v->bands.push_back(b);
        v->bandOfLine[d] = (short)(v->bands.size() - 1);
    }
    /* The split the last displayed line was drawn with: a mode write after it takes effect at the
    ** next frame's line 0, where the split starts again at 0. */
    w0.to(f0.firstLine + lines - 1);
    v->endSplit = w0.split;
}

int vdpRasterBandAt(const VdpRasterView& v, int d)
{
    return d >= 0 && d < (int)v.bandOfLine.size() ? v.bandOfLine[d] : -1;
}

void vdpRasterBandRows(const VdpDebugState* s, const VdpBand& b, int page, std::vector<int>& runs)
{
    /* R#2 bits 0-4 gate line bits 3-7 of a bitmap page, as the VDP's address mask. */
    int mask = b.page >= 0 && vdpIsBitmapMode(b.mode) ? ((b.regs[2] & 0x1f) << 3) | 7 : 0xff;
    int vs = vdpScreenScroll(s, vdpLayoutOf(b));
    runs.clear();
    if (b.flat) {
        /* FIL: they gate row bits 2-6, the page's low bit row bit 7, which picks the page of the pair. */
        mask = ((b.regs[2] & 0x1f) << 2) | 3 | ((b.page & 1) << 7);
        for (int d = b.first; d <= b.last; d++) {
            int row = ((d + vs) & 0xff) & mask;
            if (((b.page & ~1) | (row >> 7)) != page) continue;
            row = ((row & 0x7f) << 1) | b.field;
            if (!runs.empty() && runs.back() == row) runs.back() = row + 1;
            else { runs.push_back(row); runs.push_back(row + 1); }
        }
        return;
    }
    for (int d = b.first; d <= b.last; d++) {
        int row = ((d + vs) & 0xff) & mask;
        if (!runs.empty() && runs.back() == row) runs.back() = row + 1;
        else { runs.push_back(row); runs.push_back(row + 1); }
    }
}

/* FIL weaves the odd page's rows with the even one's: the same columns on both. */
static int flatPair(const VdpBand& b, bool sp2, int seg[12], int n)
{
    for (int k = 0, m = n; b.flat && !sp2 && (b.page & 1) && k < m; k++) {
        seg[n * 3] = seg[k * 3] - 1;
        seg[n * 3 + 1] = seg[k * 3 + 1];
        seg[n * 3 + 2] = seg[k * 3 + 2];
        n++;
    }
    return n;
}

int vdpRasterBandCols(const VdpBand& b, int seg[12])
{
    if (b.page < 0 || !vdpIsBitmapMode(b.mode)) return 0;
    const UInt8* r = b.regs;
    bool sp2 = (r[25] & 1) && (r[2] & 0x20);
    /* SP2 joins the even page and the odd one into a 512-dot line. On the even side
    ** of EO or blink the VDP reads other pages; that is not followed. */
    int wrap = sp2 ? 512 : 256;
    int h = (((r[26] & 0x3f) << 3) - (r[27] & 7)) & (wrap - 1);
    if (!sp2 && h == 0) {
        seg[0] = b.evenSide ? b.page & ~1 : b.page;
        seg[1] = 0;
        seg[2] = 256;
        return flatPair(b, sp2, seg, 1);
    }
    /* MSK blanks the first 8 dots; without it SCREEN 5 and 7 blank the first h & 7. */
    int lead = (r[25] & 2) ? 8 : (b.mode == 5 || b.mode == 7) ? h & 7 : 0;
    int pos = (h + lead) % wrap, left = 256 - lead, n = 0;
    while (left > 0) {
        int x = pos & 255, run = left < 256 - x ? left : 256 - x;
        seg[n * 3] = sp2 ? (b.page & ~1) | (pos >> 8) : b.evenSide ? b.page & ~1 : b.page;
        seg[n * 3 + 1] = x;
        seg[n * 3 + 2] = x + run;
        n++;
        pos = (pos + run) % wrap;
        left -= run;
    }
    return flatPair(b, sp2, seg, n);
}

VdpLayout vdpLayoutOf(const VdpBand& b)
{
    VdpLayout l = { b.regs, b.mode, b.blink, NULL, b.textSplit };
    return l;
}

bool vdpBandBitmap(const VdpDebugState* s, const VdpBand& b, VdpBitmapOptions* o)
{
    if (!vdpDisplayedBitmap(s, vdpLayoutOf(b), o)) return false;
    o->lines = 256;   /* y is a display line, wrapped only by R#23 as on the chip */
    o->evenPair = b.evenSide;
    o->field = b.field;
    return true;
}

int vdpModeFamily(int mode)
{
    return vdpIsBitmapMode(mode) ? 1 : vdpIsPatternMode(mode) ? 0 : -1;
}

VdpBandKey vdpBandKeyOf(const VdpBand& b)
{
    VdpBandKey k = { vdpModeFamily(b.mode), b.first, b.last };
    return k;
}

static int overlap(int a0, int a1, int b0, int b1)
{
    int n = (a1 < b1 ? a1 : b1) - (a0 > b0 ? a0 : b0) + 1;
    return n > 0 ? n : 0;
}

int vdpRasterFindBand(const VdpRasterView& v, const VdpBandKey& k)
{
    int best = -1, most = 0;
    for (int i = 0; i < (int)v.bands.size(); i++) {
        const VdpBand& b = v.bands[i];
        int n = vdpModeFamily(b.mode) == k.family ? overlap(b.first, b.last, k.first, k.last) : 0;
        if (n > most) { most = n; best = i; }
    }
    return best;
}

bool vdpGroupHasBand(const VdpBandGroup& g, const VdpBand& b)
{
    /* Bitmap bands of one mode share their pages; pattern bands need the same tables too. */
    static const int tables[] = { 2, 3, 4, 6, 10 };
    if (g.mode != b.mode) return false;
    if (vdpIsBitmapMode(b.mode)) return true;
    for (int reg : tables) {
        if (g.regs[reg] != b.regs[reg]) return false;
    }
    return ((g.regs[1] ^ b.regs[1]) & 0x03) == 0;
}

/* Adds lines first to last to the group's runs and its heading. */
static void addLines(VdpBandGroup& g, int first, int last)
{
    if (g.runs > 0 && g.runLast[g.runs - 1] == first - 1) g.runLast[g.runs - 1] = last;
    else if (g.runs < 8) { g.runFirst[g.runs] = first; g.runLast[g.runs] = last; g.runs++; }
    g.lineCount += last - first + 1;
    size_t at = 0;
    g.lines[0] = 0;
    for (int i = 0; i < g.runs && at < sizeof(g.lines); i++) {
        const char* sep = i ? ", " : "";
        if (g.runFirst[i] == g.runLast[i]) at += (size_t)snprintf(g.lines + at, sizeof(g.lines) - at, "%s%d", sep, g.runFirst[i]);
        else at += (size_t)snprintf(g.lines + at, sizeof(g.lines) - at, "%s%d-%d", sep, g.runFirst[i], g.runLast[i]);
    }
}

void vdpRasterGroups(const VdpRasterView& v, std::vector<VdpBandGroup>& out)
{
    out.clear();
    if (!v.valid) return;
    for (const VdpBand& b : v.bands) {
        if (b.mode < 0) continue;
        size_t k = 0;
        while (k < out.size() && !vdpGroupHasBand(out[k], b)) k++;
        if (k == out.size()) {
            VdpBandGroup g;
            memset(&g, 0, sizeof(g));
            g.mode = b.mode;
            memcpy(g.regs, b.regs, sizeof(g.regs));
            g.blink = b.blink;
            g.textSplit = b.textSplit;
            out.push_back(g);
        }
        VdpBandGroup& g = out[k];
        int seg[12];
        for (int n = vdpRasterBandCols(b, seg), q = 0; q < n; q++) g.shownPages |= 1u << seg[q * 3];
        addLines(g, b.first, b.last);
    }
    size_t kept = 0;
    for (const VdpBandGroup& g : out) {
        if (g.lineCount > 2) kept++;
    }
    if (kept > 0 && kept < out.size()) {
        out.erase(std::remove_if(out.begin(), out.end(), [](const VdpBandGroup& g) { return g.lineCount <= 2; }), out.end());
    }
}

VdpLayout vdpLayoutOf(const VdpBandGroup& g)
{
    VdpLayout l = { g.regs, g.mode, g.blink, NULL, g.textSplit };
    return l;
}

VdpBandKey vdpGroupKeyOf(const VdpBandGroup& g)
{
    VdpBandKey k = { vdpModeFamily(g.mode), g.runs ? g.runFirst[0] : 0, g.runs ? g.runLast[g.runs - 1] : 0 };
    return k;
}

int vdpFindGroup(const std::vector<VdpBandGroup>& groups, const VdpBandKey& k)
{
    int best = -1, most = 0;
    for (int i = 0; i < (int)groups.size(); i++) {
        const VdpBandGroup& g = groups[i];
        int n = 0;
        for (int r = 0; vdpModeFamily(g.mode) == k.family && r < g.runs; r++) n += overlap(g.runFirst[r], g.runLast[r], k.first, k.last);
        if (n > most) { most = n; best = i; }
    }
    return best;
}
