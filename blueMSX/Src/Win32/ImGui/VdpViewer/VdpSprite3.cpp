/*****************************************************************************
**
** Per-line sprite selection and sprite images of a V9968 in sprite mode 3.
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
#include <string.h>
#include "VdpSprite3.h"
#include "ToolText.h"

/* Follows the renderer's mode 3 line scan. The display fetches sprites from
** VRAM in address order, never interleaved and never narrowed to 128K. */

static int rd(const VdpDebugState* s, int addr)
{
    return s->vram[addr & s->vramMask & (VDP_DEBUG_VRAM_SIZE - 1)];
}

static bool spriteMode(int m)
{
    return (m >= 1 && m <= 8) || m == 10 || m == 12;
}

bool sp3Active(const VdpDebugState* s)
{
    return s->version == VDP_V9968 && (s->regs[20] & 0x08) && spriteMode(s->screenMode);
}

int sp3LineState(const UInt8* frameRegs, int mode, const UInt8* lineRegs)
{
    const UInt8* e = frameRegs;
    const UInt8* r = lineRegs;
    if (!spriteMode(mode) || !(r[20] & 0x08) || (r[8] & 0x02) || !(r[1] & 0x40)) return SP3_LINE_OFF;
    /* SVNS takes the sprites off R#23. */
    if (r[5] != e[5] || ((r[6] ^ e[6]) & 0x7f) || ((r[11] ^ e[11]) & 7) || ((r[20] ^ e[20]) & 0x0a) ||
        (!(e[20] & 0x02) && r[23] != e[23]) ||
        ((r[25] ^ e[25]) & 0x80)) return SP3_LINE_DIFFERS;
    return SP3_LINE_SAME;
}

int sp3ScanLine(const Sp3Frame& f, int d)
{
    /* Scanned a line ahead; the R#23 offset is added in eight bits. */
    return f.svns ? (d - 1) & 0x3ff : (d - 1 + f.vscroll) & 0xff;
}

static int dyOf(const Sp3Frame& f, const Sp3Plane& p, int d)
{
    return (sp3ScanLine(f, d) - p.y) & 0x3ff;
}

int sp3SourceRow(const Sp3Frame& f, int n, int d)
{
    const Sp3Plane& p = f.p[n & 63];
    int dy = dyOf(f, p, d);
    if (dy >= p.height) return -1;
    int row = vdpDebugSpriteM3Sample(dy, p.mgy, p.sizeCode);
    return p.rvy ? p.rows - 1 - row : row;
}

int sp3PatternAddr(const Sp3Frame& f, int page, int pattern, int row)
{
    /* Added, not ORed: a tall pattern runs down the tile rows and on past its page. */
    return f.patBase + (page << 15) + ((pattern >> 4) << 11) + (row << 7) + ((pattern & 15) << 3);
}

int sp3PatternDot(const VdpDebugState* s, const Sp3Frame& f, int page, int pattern, int row, int col)
{
    int v = rd(s, sp3PatternAddr(f, page, pattern, row) + (col >> 1));
    return (col & 1) ? v & 15 : v >> 4;
}

int sp3XRuns(const Sp3Plane& p, int* x0, int* x1)
{
    int n = 0, end = p.x + p.mgx;
    if (p.mgx == 0) return 0;
    if (p.x < 256) {
        x0[n] = p.x;
        x1[n++] = end < 256 ? end : 256;
    }
    else if (end > 1024) {
        x0[n] = 0;
        x1[n++] = end - 1024 < 256 ? end - 1024 : 256;
    }
    return n;
}

void sp3Eval(const VdpDebugState* s, Sp3Frame* out, bool scanAll)
{
    Sp3Frame& f = *out;
    const UInt8* r = s->regs;
    memset(out, 0, sizeof(*out));
    f.on = sp3Active(s);
    f.lines = !vdpIsMsx1(s) && (r[9] & 0x80) ? 212 : 192;
    f.vramMask = s->vramMask;
    f.attrBase = vdpSp3AttrBase(r);
    f.aliasMask = r[5] & 3;
    f.patBase = vdpSp3PatternBase(r);
    f.shuffle = (r[25] & 0x80) != 0;
    f.svns = (r[20] & 0x02) != 0;
    f.vscroll = r[23];
    f.off = (r[8] & 0x02) ? ts(TS_SPR_OFF_SPD) : !s->screenOn ? ts(TS_SPR_OFF_BLANKED) : NULL;
    f.stopStep = SP3_PLANES;
    f.firstOverLine = f.fifthPlane = -1;

    for (int step = 0; step < SP3_PLANES; step++) {
        f.order[step] = s->sprPlaneOrder[step] & 63;
    }
    for (int n = 0; n < SP3_PLANES; n++) {
        Sp3Plane& p = f.p[n];
        int alias = vdpSp3Alias(r, n);
        int a = f.attrBase | ((alias >> 4) << 7) | ((alias & 15) << 3);
        int b1 = rd(s, a + 1), mode = rd(s, a + 3), b5 = rd(s, a + 5);
        p.attr = a & s->vramMask;
        p.alias = alias;
        p.y = rd(s, a) | (b1 & 3) << 8;
        p.sizeCode = b1 >> 6;
        p.rows = 16 << p.sizeCode;
        p.mgy = rd(s, a + 2);
        p.height = p.mgy ? p.mgy : 256;
        p.weight = 4 - (mode >> 6);
        p.rvy = (mode & 0x20) != 0;
        p.rvx = (mode & 0x10) != 0;
        p.ps = mode & 15;
        p.x = rd(s, a + 4) | (b5 & 3) << 8;
        p.page = (b5 >> 4) & 7;
        p.mgx = rd(s, a + 6);
        p.pattern = rd(s, a + 7);
        int x0[2], x1[2];
        p.xVisible = sp3XRuns(p, x0, x1) > 0;
        p.firstLine = p.lastLine = p.firstDropped = p.lastDropped = -1;
    }
    for (int step = 0; step < SP3_PLANES; step++) {
        Sp3Plane& p = f.p[f.order[step]];
        p.step = step;
        /* The whole ten bit Y ends the table, and only while the scan is in order. */
        if (f.stopStep == SP3_PLANES && p.y == 216 && !f.shuffle) f.stopStep = step;
    }

    /* Off, the renderer scans nothing: no line, no S#0. Nor below the display:
    ** VR (S#2 bit 6) is set as the last displayed line ends, and the scan skips. */
    for (int d = 0; (f.off == NULL || scanAll) && d < f.lines && d < SP3_LINES; d++) {
        int c = 0;
        for (int step = 0; step < f.stopStep; step++) {
            int n = f.order[step];
            Sp3Plane& p = f.p[n];
            if (dyOf(f, p, d) >= p.height) continue;
            f.id[d][c] = (uint8_t)n;
            if (p.firstLine < 0) p.firstLine = d;
            p.lastLine = d;
            int row = vdpDebugSpriteM3Sample(dyOf(f, p, d), p.mgy, p.sizeCode) & 127;
            if (c < SP3_LIMIT) {
                p.drawnLines++;
                f.rowSt[n][row] |= 1;
            }
            else {
                p.droppedLines++;
                f.rowSt[n][row] |= 2;
                if (p.firstDropped < 0) p.firstDropped = d;
                p.lastDropped = d;
            }
            /* Planes 32 to 63 do not fit the five bits of S#0 and report 31. */
            if (c == SP3_LIMIT && f.firstOverLine < 0) {
                f.firstOverLine = d;
                f.fifthPlane = n < 32 ? n : 31;
            }
            c++;
        }
        f.count[d] = (uint8_t)c;
        f.overLines += c > SP3_LIMIT;
    }

    for (int n = 0; n < SP3_PLANES; n++) {
        Sp3Plane& p = f.p[n];
        p.state = p.step >= f.stopStep ? SP3_ST_AFTER_STOP :
                  p.firstLine < 0 ? SP3_ST_OFF_SCREEN :
                  p.mgx == 0 ? SP3_ST_NO_WIDTH :
                  !p.xVisible ? SP3_ST_OFF_SCREEN :
                  p.droppedLines > 0 ? SP3_ST_DROPPED : SP3_ST_DRAWN;
    }
}

/* The colour code plane n has at X x of displayed line d, 0 for none. */
static int planeDot(const VdpDebugState* s, const Sp3Frame& f, int n, int x, int d)
{
    const Sp3Plane& p = f.p[n];
    int row = sp3SourceRow(f, n, d), dx = (x - p.x) & 0x3ff;
    if (row < 0 || dx >= p.mgx) return 0;
    int col = vdpDebugSpriteM3Sample(dx, p.mgx, 0);
    if (p.rvx) col = 15 - col;
    return sp3PatternDot(s, f, p.page, p.pattern, row, col);
}

int sp3PlaneAt(const VdpDebugState* s, const Sp3Frame& f, int x, int d)
{
    if (d < 0 || d >= f.lines || x < 0 || x >= 256) return -1;
    int c = f.count[d] < SP3_LIMIT ? f.count[d] : SP3_LIMIT;
    /* The first plane of the scan to put a dot there keeps it. */
    for (int k = 0; k < c; k++) {
        if (planeDot(s, f, f.id[d][k], x, d) != 0) return f.id[d][k];
    }
    return -1;
}

void sp3RenderPlacement(const VdpDebugState* s, const Sp3Frame& f, uint32_t dropRgba, uint32_t* out, int limit,
                        bool whenOff)
{
    static const int alpha[5] = { 0, 64, 128, 191, 255 };
    uint8_t weight[256];
    memset(out, 0, (size_t)256 * f.lines * sizeof(uint32_t));
    if (f.off != NULL && !whenOff) return;
    for (int d = 0; d < f.lines; d++) {
        uint32_t* dst = out + (size_t)d * 256;
        memset(weight, 0, sizeof(weight));
        for (int k = 0; k < f.count[d]; k++) {
            int n = f.id[d][k];
            const Sp3Plane& p = f.p[n];
            bool dropped = k >= limit;
            if (dropped && dropRgba == 0) break;
            int row = sp3SourceRow(f, n, d);
            for (int dx = 0; dx < p.mgx; dx++) {
                int x = (p.x + dx) & 0x3ff;
                if (x >= 256 || weight[x]) continue;
                int col = vdpDebugSpriteM3Sample(dx, p.mgx, 0);
                if (p.rvx) col = 15 - col;
                int c = sp3PatternDot(s, f, p.page, p.pattern, row, col);
                if (c == 0) continue;
                if (dropped) {
                    if (((x ^ d) & 1) == 0 && dst[x] == 0) dst[x] = dropRgba;
                    continue;
                }
                uint32_t rgba = vdpRgbToRgba(s->paletteExt[(p.ps << 4) | c]);
                dst[x] = (rgba & 0x00ffffffu) | ((uint32_t)alpha[p.weight] << 24);
                weight[x] = (uint8_t)p.weight;
            }
        }
    }
}

bool sp3RenderTile(const VdpDebugState* s, const Sp3Frame& f, int n, uint32_t* out, int pitch)
{
    const Sp3Plane& p = f.p[n & 63];
    bool any = false;
    for (int r = 0; r < p.rows; r++) {
        int row = p.rvy ? p.rows - 1 - r : r;
        for (int x = 0; x < 16; x++) {
            int c = sp3PatternDot(s, f, p.page, p.pattern, row, p.rvx ? 15 - x : x);
            out[(size_t)r * pitch + x] = c ? vdpRgbToRgba(s->paletteExt[(p.ps << 4) | c]) : 0;
            any = any || c != 0;
        }
    }
    return any;
}

void sp3RenderPage(const VdpDebugState* s, const Sp3Frame& f, int page, int ps, uint32_t* out)
{
    int base = f.patBase + (page << 15);
    for (int i = 0; i < 0x8000; i++) {
        int v = rd(s, base + i);
        for (int h = 0; h < 2; h++) {
            int c = h ? v & 15 : v >> 4;
            out[i * 2 + h] = c ? vdpRgbToRgba(s->paletteExt[((ps & 15) << 4) | c]) : 0;
        }
    }
}
