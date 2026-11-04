/*****************************************************************************
**
** Per-line sprite selection and sprite images of TMS99xx, V9938 and V9958.
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
#include "VdpSprite.h"
#include "ToolText.h"

/* Follows the renderer's sprite line builder; every address outside a recorded
** table goes through vdpPhysicalAddress, so SCREEN 7-12 read the interleaved halves. */

/* While vdpEvalSpritesFrom runs: the bytes the VDP read of a recorded table,
** logical [from[i], to[i]) at bytes + (from[i] - base). */
static const VdpSprTable* recorded;
static int recFrom[2], recTo[2];

static int rdl(const VdpDebugState* s, int mode, int logical)
{
    for (int i = 0; recorded && i < 2; i++) {
        int k = logical - recorded->base;
        if (logical >= recFrom[i] && logical < recTo[i] && k >= 0 && k < (int)sizeof(recorded->bytes)) return recorded->bytes[k];
    }
    int p = vdpPhysicalAddress(s, mode, logical);
    return p >= 0 && p < VDP_DEBUG_VRAM_SIZE ? s->vram[p] : 0xff;
}

/* R#11/R#5 with the low bits set, as the VDP ANDs fetch addresses with it. */
static int sprTabBase(const VdpDebugState* s, const UInt8* r)
{
    return (((int)r[11] << 15) | ((int)r[5] << 7) | 0x7f) & s->vramMask;
}

/* The attributes: in sprite mode 2 at A9 and up of the base, kept inside its 1 KB block. */
static int sprAttrBase(int sprMode, int tab)
{
    return sprMode == 1 ? tab & ~0x7f : (tab & ~0x3ff) | (tab & 0x200);
}

static int patternAddr(const VdpDebugState* s, const UInt8* r, int mode, int pattern)
{
    /* Sprite mode 2 keeps A11-A16 only, as the renderer does with 192K. */
    int base = ((int)r[6] << 11) & s->vramMask & ~0x7ff;
    if (vdpSpriteMode(mode) == 2) base &= 0x1f800;
    return base + (pattern & 0xff) * 8;
}

/* R#1's size and magnification, and why the VDP draws no sprites or NULL: only SPD and
** a blank screen (R#1 BL); S#2 VR is set because the snapshot is in VBlank. */
static const char* sprLook(const VdpDebugState* s, const UInt8* r, int* size, int* mag)
{
    *size = (r[1] & 2) ? 16 : 8;
    *mag  = (r[1] & 1) ? 2 : 1;
    return !(r[1] & 0x40) ? ts(TS_SPR_OFF_BLANKED) : !vdpIsMsx1(s) && (r[8] & 2) ? ts(TS_SPR_OFF_SPD) : NULL;
}

bool vdpSpriteUseKey(const VdpDebugState* s, const VdpSprUse& u, int* attr, int* patBase, int* mode, int* sizeKey)
{
    UInt8 r[32] = {};
    r[1] = u.r1;
    r[5] = u.r5;
    r[6] = u.r6;
    r[8] = u.r8;
    r[11] = u.r11;
    int m = vdpSpriteMode(u.screenMode), size, mag;
    if (m == 0 || sprLook(s, r, &size, &mag) != NULL) return false;
    *attr = sprAttrBase(m, sprTabBase(s, r));
    *patBase = patternAddr(s, r, u.screenMode, 0);
    *mode = m;
    *sizeKey = size * 2 + mag;
    return true;
}

int vdpSpritePatternAddr(const VdpDebugState* s, const VdpSpriteFrame& f, int pattern)
{
    return patternAddr(s, f.regs, f.screenMode, pattern);
}

int vdpSpriteColorAddr(const VdpDebugState* s, const VdpSpriteFrame& f, int n, int row)
{
    /* ANDed, so A7-A9 of R#5 mirror rows as on the chip. */
    return sprTabBase(s, f.regs) & ((-1 << 10) | (n * 16 + row));
}

VdpLayout vdpLayoutOf(const VdpSpriteFrame& f)
{
    VdpLayout l = { f.regs, f.screenMode, 0 };
    return l;
}

int vdpSpriteRow(const VdpSpriteFrame& f, int n, int scrollLine)
{
    if (n < 0 || n >= 32 || f.mode == 0) return -1;
    int row = ((scrollLine - 1 - f.spr[n].y) & 0xff) / f.mag;
    return row < f.size ? row : -1;
}

int vdpSpriteState(const VdpSpriteFrame& f, int scrollLine, int k)
{
    return vdpSpriteStateAt(f, scrollLine, k, f.limit);
}

int vdpSpriteStateAt(const VdpSpriteFrame& f, int scrollLine, int k, int limit)
{
    const VdpSpriteLine& ln = f.line[scrollLine & 0xff];
    if (k >= limit) return VDP_SPRITE_DROPPED;
    if (f.mode != 2) return VDP_SPRITE_DRAWN;
    /* CC sprites show only after one without CC on the same line. */
    for (int j = 0; j <= k && j < ln.count; j++) {
        int n = ln.id[j], row = vdpSpriteRow(f, n, scrollLine);
        if (row >= 0 && !(f.spr[n].color[row] & 0x40)) return VDP_SPRITE_DRAWN;
    }
    return VDP_SPRITE_CC_HIDDEN;
}

int vdpSpriteDisplayLine(const VdpSpriteFrame& f, int scrollLine)
{
    int d = (scrollLine - f.vscroll) & 0xff;
    return d < f.lines ? d : -1;
}

/* The 16 dots of the pattern row at a, the right half 16 bytes on in 16x16 sprites. */
static int rowBits(const VdpDebugState* s, int mode, int size, int a)
{
    return rdl(s, mode, a) << 8 | (size == 16 ? rdl(s, mode, a + 16) : 0);
}

int vdpSpriteRowBits(const VdpDebugState* s, const VdpSpriteFrame& f, int n, int row)
{
    return rowBits(s, f.screenMode, f.size, f.spr[n].patternAddr + row);
}

int vdpSpritePatternRowBits(const VdpDebugState* s, const VdpSpriteFrame& f, int pattern, int row)
{
    return rowBits(s, f.screenMode, f.size, patternAddr(s, f.regs, f.screenMode, pattern) + row);
}

int vdpSpriteSpan(const VdpSpriteFrame& f, int n, int row, int* w)
{
    if (w != NULL) *w = f.size * f.mag;
    return f.spr[n].x - ((f.spr[n].color[row] & 0x80) ? 32 : 0);
}

bool vdpSpriteClear(const VdpDebugState* s, const VdpSpriteFrame& f, int code)
{
    return (code & 15) == 0 && (vdpIsMsx1(s) || !(f.regs[8] & 0x20));
}

int vdpSpriteAt(const VdpSpriteFrame& f, int scrollLine, int dot)
{
    const VdpSpriteLine& ln = f.line[scrollLine & 0xff];
    for (int k = 0; k < ln.count; k++) {
        int n = ln.id[k], row = vdpSpriteRow(f, n, scrollLine), w;
        if (row < 0) continue;
        int x0 = vdpSpriteSpan(f, n, row, &w);
        if (dot >= x0 && dot < x0 + w) return n;
    }
    return -1;
}

uint32_t vdpSpriteRgbaFrom(const VdpDebugState* s, const UInt32* rgb, const VdpSpriteFrame& f, int code)
{
    /* SCREEN 8 sprites use their own fixed colours, R G B in sevenths. */
    static const unsigned char screen8[16][3] = {
        { 0, 0, 0 }, { 0, 0, 2 }, { 3, 0, 0 }, { 3, 0, 2 }, { 0, 3, 0 }, { 0, 3, 2 }, { 3, 3, 0 }, { 3, 3, 2 },
        { 7, 4, 2 }, { 0, 0, 7 }, { 7, 0, 0 }, { 7, 0, 7 }, { 0, 7, 0 }, { 0, 7, 7 }, { 7, 7, 0 }, { 7, 7, 7 },
    };
    code &= 15;
    if (f.screenMode == 8) {
        const unsigned char* c = screen8[code];
        return vdpRgbToRgba(((uint32_t)(c[0] * 255 / 7) << 16) | ((uint32_t)(c[1] * 255 / 7) << 8) | (uint32_t)(c[2] * 255 / 7));
    }
    if (f.screenMode == 6) code = (code >> 2) & 3;
    return vdpRgbToRgba((rgb ? rgb : s->paletteRgb)[code]);
}

void vdpRenderSpriteTile(const VdpDebugState* s, const VdpSpriteFrame& f, int n, const UInt32* const rowRgb[16], uint32_t* dst, int pitch)
{
    for (int r = 0; r < f.size; r++) {
        int c = f.spr[n].color[r], bits = vdpSpriteRowBits(s, f, n, r);
        uint32_t col = vdpSpriteClear(s, f, c) ? 0 : vdpSpriteRgbaFrom(s, rowRgb != NULL ? rowRgb[r] : NULL, f, c);
        for (int i = 0; i < f.size; i++) dst[(size_t)r * pitch + i] = (bits & (0x8000 >> i)) ? col : 0;
    }
}

void vdpRenderSpriteLines(const VdpDebugState* s, const VdpSpriteFrame& f, uint32_t* out, int width, int first, int last,
                          const UInt32* const* lineRgb, uint8_t* dropped)
{
    int wide = width / 256;
    int code[256], group[256];
    for (int d = first; d <= last; d++) {
        uint32_t* dst = &out[(size_t)d * width];
        memset(dst, 0, (size_t)width * sizeof(uint32_t));
        if (dropped != NULL) memset(dropped + (size_t)d * 256, 0, 256);
        if (f.mode == 0 || f.off || d < 0 || d >= f.lines) continue;
        int L = (d + f.vscroll) & 0xff, k, i;
        const VdpSpriteLine& ln = f.line[L];
        int drawn = ln.count < f.limit ? ln.count : f.limit, lead = -1;
        for (i = 0; i < 256; i++) code[i] = -1;
        for (k = 0; k < drawn; k++) {
            int n = ln.id[k], row = vdpSpriteRow(f, n, L);
            if (row < 0) continue;
            const VdpSprite& sp = f.spr[n];
            int c = sp.color[row];
            /* A CC sprite joins the sprite without CC before it; with none it is not shown. */
            if (f.mode == 2 && (c & 0x40)) {
                if (lead < 0) continue;
            }
            else {
                lead = k;
            }
            if (vdpSpriteClear(s, f, c)) continue;
            int w, bits = vdpSpriteRowBits(s, f, n, row), x0 = vdpSpriteSpan(f, n, row, &w);
            for (i = 0; i < w; i++) {
                int x = x0 + i;
                if (!(bits & (0x8000 >> (i / f.mag))) || x < 0 || x >= 256) continue;
                if (code[x] < 0) {
                    code[x] = c & 15;
                    group[x] = lead;
                }
                else if (f.mode == 2 && group[x] == lead) {
                    code[x] |= c & 15;
                }
            }
        }
        /* Dots a sprite past the limit would show where no drawn sprite does. */
        for (k = 0, lead = -1; dropped != NULL && k < ln.count; k++) {
            int n = ln.id[k], row = vdpSpriteRow(f, n, L);
            if (row < 0) continue;
            const VdpSprite& sp = f.spr[n];
            int c = sp.color[row];
            if (f.mode != 2 || !(c & 0x40)) lead = k;
            if (k < drawn || (f.mode == 2 && (c & 0x40) && lead < 0) || vdpSpriteClear(s, f, c)) continue;
            int w, bits = vdpSpriteRowBits(s, f, n, row), x0 = vdpSpriteSpan(f, n, row, &w);
            for (i = 0; i < w; i++) {
                int x = x0 + i;
                if ((bits & (0x8000 >> (i / f.mag))) && x >= 0 && x < 256 && code[x] < 0) dropped[(size_t)d * 256 + x] = 1;
            }
        }
        const UInt32* rgb = lineRgb != NULL && lineRgb[d] != NULL ? lineRgb[d] : s->paletteRgb;
        for (i = 0; i < 256; i++) {
            if (code[i] < 0) continue;
            if (wide == 1) {
                dst[i] = vdpSpriteRgbaFrom(s, rgb, f, code[i]);
            }
            else {
                /* SCREEN 6 shows bits 3-2 on the left dot and 1-0 on the right one. */
                dst[i * 2] = vdpSpriteRgbaFrom(s, rgb, f, code[i]);
                dst[i * 2 + 1] = f.screenMode == 6 ? vdpRgbToRgba(rgb[code[i] & 3]) : dst[i * 2];
            }
        }
    }
}

/* Counts collidable dots per position of one displayed line; true when the
** sprite at position k of the line takes part. */
static bool collidable(const VdpDebugState* s, const VdpSpriteFrame& f, int L, int k, int* row)
{
    int n = f.line[L].id[k];
    *row = vdpSpriteRow(f, n, L);
    if (*row < 0 || vdpSpriteState(f, L, k) != VDP_SPRITE_DRAWN) return false;
    int c = f.spr[n].color[*row];
    bool clear = vdpSpriteClear(s, f, c);
    if (f.mode == 1) {
        /* A transparent colour 0 still collides on the TMS99xx, not on the V99x8. */
        return !clear || vdpIsMsx1(s);
    }
    return !clear && !(c & 0x60);
}

int vdpSpriteLineCollision(const VdpDebugState* s, const VdpSpriteFrame& f, int d, int* xOut, uint32_t* colliders)
{
    int L = (d + f.vscroll) & 0xff;
    const VdpSpriteLine& ln = f.line[L];
    int drawn = ln.count < f.limit ? ln.count : f.limit;
    unsigned char cov[256];
    int row, k, i;
    *colliders = 0;
    if (f.mode == 0 || f.off != NULL || d < 0 || d >= f.lines) return 0;
    memset(cov, 0, sizeof(cov));
    for (k = 0; k < drawn; k++) {
        if (!collidable(s, f, L, k, &row)) continue;
        int w, bits = vdpSpriteRowBits(s, f, ln.id[k], row);
        int x0 = vdpSpriteSpan(f, ln.id[k], row, &w);
        for (i = 0; i < w; i++) {
            int x = x0 + i;
            if ((bits & (0x8000 >> (i / f.mag))) && x >= 0 && x < 256 && cov[x] < 255) cov[x]++;
        }
    }
    int first = -1;
    for (i = 0; i < 256 && first < 0; i++) {
        if (cov[i] >= 2) first = i;
    }
    if (first < 0) return 0;
    *xOut = first;
    for (k = 0; k < drawn; k++) {
        if (!collidable(s, f, L, k, &row)) continue;
        int w, bits = vdpSpriteRowBits(s, f, ln.id[k], row);
        int x0 = vdpSpriteSpan(f, ln.id[k], row, &w);
        for (i = 0; i < w; i++) {
            int x = x0 + i;
            if ((bits & (0x8000 >> (i / f.mag))) && x >= 0 && x < 256 && cov[x] >= 2) {
                *colliders |= 1u << ln.id[k];
                break;
            }
        }
    }
    return 1;
}

static void lineCollision(const VdpDebugState* s, VdpSpriteFrame* f, int d)
{
    uint32_t colliders;
    int x;
    if (!vdpSpriteLineCollision(s, *f, d, &x, &colliders)) return;
    if (f->collLine < 0) {
        f->collLine = d;
        f->collX = x;
    }
    for (int n = 0; n < 32; n++) {
        if ((colliders >> n) & 1) f->spr[n].collides = true;
    }
}

void vdpEvalSprites(const VdpDebugState* s, const VdpLayout& l, int lines, VdpSpriteFrame* f)
{
    const UInt8* r = l.regs;
    int n, L, d, k;
    memset(f, 0, sizeof(*f));
    f->collX = f->collLine = f->firstDropLine = f->firstDropped = -1;
    f->screenMode = l.mode;
    memcpy(f->regs, l.regs, sizeof(f->regs));
    f->mode = vdpSpriteMode(l.mode);
    f->stopIndex = 32;
    if (f->mode == 0) return;

    f->off     = sprLook(s, r, &f->size, &f->mag);
    f->limit   = f->mode == 1 ? 4 : 8;
    f->stopY   = f->mode == 1 ? 208 : 216;
    f->lines   = lines;
    f->vscroll = vdpIsMsx1(s) ? 0 : r[23];

    int attr = sprAttrBase(f->mode, sprTabBase(s, r));
    for (n = 0; n < 32; n++) {
        VdpSprite& sp = f->spr[n];
        int a = attr + n * 4;
        sp.attrAddr = a;
        sp.y = rdl(s, l.mode, a);
        sp.x = rdl(s, l.mode, a + 1);
        sp.pattern = rdl(s, l.mode, a + 2) & (f->size == 16 ? 0xfc : 0xff);
        sp.patternAddr = vdpSpritePatternAddr(s, *f, sp.pattern);
        if (f->mode == 1) {
            sp.colorAddr = a + 3;
            memset(sp.color, rdl(s, l.mode, a + 3), sizeof(sp.color));
        }
        else {
            sp.colorAddr = vdpSpriteColorAddr(s, *f, n, 0);
            for (k = 0; k < 16; k++) sp.color[k] = (uint8_t)rdl(s, l.mode, vdpSpriteColorAddr(s, *f, n, k));
        }
        sp.firstDropped = sp.lastDropped = -1;
        if (f->stopIndex == 32 && sp.y == f->stopY) f->stopIndex = n;
        sp.afterStop = n >= f->stopIndex;
    }

    for (L = 0; L < 256; L++) {
        VdpSpriteLine& ln = f->line[L];
        for (n = 0; n < f->stopIndex; n++) {
            if (vdpSpriteRow(*f, n, L) >= 0) ln.id[ln.count++] = (uint8_t)n;
        }
    }

    for (d = 0; d < lines; d++) {
        L = (d + f->vscroll) & 0xff;
        const VdpSpriteLine& ln = f->line[L];
        for (k = 0; k < ln.count; k++) {
            VdpSprite& sp = f->spr[ln.id[k]];
            int st = vdpSpriteState(*f, L, k);
            if (st == VDP_SPRITE_DRAWN) sp.drawnLines++;
            else if (st == VDP_SPRITE_CC_HIDDEN) sp.hiddenLines++;
            else {
                sp.droppedLines++;
                if (sp.firstDropped < 0) sp.firstDropped = d;
                sp.lastDropped = d;
            }
        }
        if (ln.count > f->limit && f->off == NULL) {
            if (f->firstDropLine < 0) {
                f->firstDropLine = d;
                f->firstDropped = ln.id[f->limit];
            }
            f->overLines++;
        }
        if (f->off == NULL) lineCollision(s, f, d);
    }
}

void vdpEvalSpritesFrom(const VdpDebugState* s, const VdpSprTable& t, VdpSpriteFrame* out)
{
    VdpLayout l = { t.regs, t.screenMode, 0 };
    int mode = vdpSpriteMode(t.screenMode), tab = sprTabBase(s, t.regs);
    int attr = sprAttrBase(mode, tab);
    /* Sprite mode 1 reads the attributes alone, mode 2 the colours before them too. */
    recFrom[0] = attr;
    recTo[0] = attr + 128;
    recFrom[1] = recTo[1] = 0;
    if (mode == 2) {
        recFrom[1] = t.base;
        recTo[1] = t.base + 0x200;
    }
    recorded = mode != 0 && (tab & ~0x3ff) == t.base ? &t : NULL;
    vdpEvalSprites(s, l, !vdpIsMsx1(s) && (t.regs[9] & 0x80) ? 212 : 192, out);
    recorded = NULL;
}

void vdpDecodeSpriteSheetUsers(const VdpDebugState* s, const VdpSpriteFrame& f, const int owner[256], const UInt32* const* ownerRgb,
                               uint32_t grey, VdpImage& out, int* w, int* h)
{
    *w = 256;
    *h = 64;
    out.assign((size_t)256 * 64, 0);
    for (int p = 0; p < 256; p += f.size == 16 ? 4 : 1) {
        int cx, cy, n = owner[p];
        uint32_t* dst;
        vdpSpriteSheetCell(f.size, p, &cx, &cy);
        dst = &out[(size_t)cy * 256 + cx];
        if (n >= 0 && n < 32) {
            vdpRenderSpriteTile(s, f, n, ownerRgb != NULL ? ownerRgb + n * 16 : NULL, dst, 256);
            continue;
        }
        int a = patternAddr(s, f.regs, f.screenMode, p);
        for (int row = 0; row < f.size; row++) {
            int bits = rowBits(s, f.screenMode, f.size, a + row);
            for (int i = 0; i < f.size; i++) dst[(size_t)row * 256 + i] = (bits & (0x8000 >> i)) ? grey : 0;
        }
    }
}

int vdpSpriteSheetPattern(int size, int x, int y)
{
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return -1;
    if (size == 16) return ((y / 16) * 16 + x / 16) * 4;
    return (y / 8) * 32 + x / 8;
}

void vdpSpriteSheetCell(int size, int pattern, int* x, int* y)
{
    if (size == 16) {
        int k = (pattern & 0xff) >> 2;
        *x = (k & 15) * 16;
        *y = (k >> 4) * 16;
    }
    else {
        *x = (pattern & 31) * 8;
        *y = ((pattern & 0xff) >> 5) * 8;
    }
}
