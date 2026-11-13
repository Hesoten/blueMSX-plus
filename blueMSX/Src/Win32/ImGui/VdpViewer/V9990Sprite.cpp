/*****************************************************************************
**
** Per-line sprite selection and sprite images of a V9990 in P1 and P2.
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
#include "V9990Sprite.h"
#include "V9990Decode.h"
extern "C" {
#include "V9990Render.h"
}

/* Follows the renderer's line by line choice: the 125 attributes in order,
** a sprite in range taking a place even with PR0, none checked once the 16 places are taken. */

#define ATTR_PHYS 0x3fe00
#define PLANE_REAR   0
#define PLANE_FRONT  1
#define PLANE_SPRITE 2

UInt32 v9990SpritePatternAddr(const V9990SpriteFrame& f, int p, int r)
{
    if (f.p2) return (f.patBase + 256 * (((p & 0xe0) >> 1) + r) + 8 * (p & 0x1f)) & V9990_VRAM_MASK;
    return (f.patBase + 128 * ((p & 0xf0) + r) + 8 * (p & 0x0f)) & V9990_VRAM_MASK;
}

uint16_t v9990SpritePatternRow(const V9990DebugState* s, const V9990SpriteFrame& f, int p, int r, uint8_t dots[16])
{
    UInt32 a = v9990SpritePatternAddr(f, p, r);
    uint16_t set = 0;
    for (int j = 0; j < 16; j += 2) {
        /* P2 reads its sprite patterns interleaved at every address, as the renderer does. */
        UInt32 c = (a + j / 2) & V9990_VRAM_MASK;
        UInt8 b = s->vram[f.p2 ? V9990_MAP_BX(c) : c];
        dots[j] = b >> 4;
        dots[j + 1] = b & 15;
        if (dots[j]) set |= 0x8000 >> j;
        if (dots[j + 1]) set |= 0x8000 >> (j + 1);
    }
    return set;
}

/* Which layer dot is in front on line d, as the renderer composes P1 and P2: nonzero where it is. */
static void frontPlane(const V9990DebugState* s, int d, int width, uint8_t* plane)
{
    uint8_t a[512], b[256];
    V9990Layer la, lb;
    v9990LayerOf(s, 0, &la);
    int ay = (la.sy & ~la.rollMask & 0x1ff) + ((d + la.sy) & la.rollMask);
    if (width == 512) {
        v9990FetchP2Layer(a, 512, s->vram, (UInt32)la.sx, (UInt32)ay);
        for (int x = 0; x < 512; x++) plane[x] = a[x] ? PLANE_FRONT : PLANE_REAR;
        return;
    }
    v9990LayerOf(s, 1, &lb);
    v9990FetchP1Layer(a, 256, s->vram, la.names, la.patterns, (UInt32)la.sx, (UInt32)ay);
    v9990FetchP1Layer(b, 256, s->vram, lb.names, lb.patterns, (UInt32)lb.sx, (UInt32)((d + lb.sy) & 0x1ff));
    int r27 = s->regs[27];
    int prioX = (r27 & 0x03) ? (r27 & 0x03) << 6 : 256, prioY = (r27 & 0x0c) ? (r27 & 0x0c) << 4 : 256;
    if (d >= prioY) prioX = 0;
    for (int x = 0; x < 256; x++) plane[x] = (x >= prioX ? b[x] : a[x]) ? PLANE_FRONT : PLANE_REAR;
}

int v9990SpriteLines(const V9990Sprite& sp, int* last)
{
    int d0 = -1, d1 = -1;
    for (int r = 0; r < 16; r++) {
        int d = sp.rowLine[r];
        if (d >= 0 && (d0 < 0 || d < d0)) d0 = d;
        if (d > d1) d1 = d;
    }
    if (last != NULL) *last = d1;
    return d0;
}

void v9990EvalSprites(const V9990DebugState* s, V9990SpriteFrame* out, const UInt8* attr, bool keepWhenOff)
{
    V9990SpriteFrame& f = *out;
    f.valid = s->mode == V9990_MODE_P1 || s->mode == V9990_MODE_P2;
    f.layer.clear();
    f.dropMask.clear();
    if (!f.valid) return;
    f.p2 = s->mode == V9990_MODE_P2;
    f.off = (s->regs[8] & 0x40) != 0;
    f.width = f.p2 ? 512 : 256;
    f.attrBase = f.p2 ? 0x7be00 : ATTR_PHYS;
    f.patBase = f.p2 ? (UInt32)(s->regs[25] & 0x0f) << 15 : (UInt32)(s->regs[25] & 0x0e) << 14;
    f.shown = f.pr0 = f.dropped = f.offY = f.offX = f.overLines = 0;
    f.firstDropped = f.firstDropLine = f.pr0Full0 = f.pr0Full1 = -1;
    f.layer.assign((size_t)f.width * V9_SPRITE_LINES, 0);
    f.dropMask.assign((size_t)f.width * V9_SPRITE_LINES, 0);
    for (int i = 0; i < V9_SPRITES; i++) {
        const UInt8* a = (attr != NULL ? attr : s->vram + ATTR_PHYS) + 4 * i;
        V9990Sprite& sp = f.spr[i];
        memset(&sp, 0, sizeof(sp));
        sp.y = a[0];
        sp.pattern = a[1];
        sp.x = a[2] | ((a[3] & 0x03) << 8);
        if (sp.x > 1024 - 16) sp.x -= 1024;
        sp.sc = a[3] >> 6;
        sp.pr0 = (a[3] & 0x10) != 0;
        sp.pr1 = (a[3] & 0x20) != 0;
        sp.attrAddr = f.attrBase + 4 * i;
        sp.line0 = sp.line1 = sp.dropLine = sp.place0 = sp.place1 = -1;
        for (int r = 0; r < 16; r++) {
            int d = (sp.y + 1 + r) & 0xff;
            sp.rowLine[r] = (int16_t)(d < V9_SPRITE_LINES ? d : -1);
            sp.row[r] = V9_ROW_OFF;
        }
    }
    uint8_t plane[512], dots[16];
    for (int d = 0; d < V9_SPRITE_LINES; d++) {
        V9990SpriteLine& ln = f.line[d];
        int limit = V9_SPRITE_PLACES, found = 0, visible[V9_SPRITE_PLACES];
        bool stopped = false;
        ln.count = ln.places = ln.pr0 = 0;
        if (f.off && !keepWhenOff) continue;
        for (int i = 0; i < V9_SPRITES; i++) {
            V9990Sprite& sp = f.spr[i];
            int r = (d - sp.y - 1) & 0xff;
            if (r >= 16) continue;
            ln.id[ln.count++] = (uint8_t)i;
            if (sp.line0 < 0) sp.line0 = d;
            sp.line1 = d;
            if (stopped) {
                sp.row[r] = sp.pr0 ? V9_ROW_PR0_PAST : V9_ROW_DROPPED;
                if (!sp.pr0 && sp.dropLine < 0) sp.dropLine = d;
                continue;
            }
            ln.places++;
            if (sp.pr0) {
                sp.row[r] = V9_ROW_PR0;
                if (sp.place0 < 0) sp.place0 = d;
                sp.place1 = d;
                ln.pr0++;
                limit--;
            }
            else {
                sp.row[r] = V9_ROW_SHOWN;
                visible[found++] = i;
            }
            stopped = found >= limit;
        }
        if (ln.places == V9_SPRITE_PLACES && ln.pr0 > 0) {
            if (f.pr0Full0 < 0) f.pr0Full0 = f.pr0Full1 = d;
            else if (f.pr0Full1 == d - 1) f.pr0Full1 = d;
        }
        bool over = false;
        for (int k = ln.places; k < ln.count; k++) over = over || !f.spr[ln.id[k]].pr0;
        f.overLines += over;
        /* The dots: an earlier sprite's dot hides a later one's; PR1 goes behind the front layer. */
        frontPlane(s, d, f.width, plane);
        uint8_t* lay = &f.layer[(size_t)d * f.width];
        for (int k = 0; k < found; k++) {
            V9990Sprite& sp = f.spr[visible[k]];
            int r = (d - sp.y - 1) & 0xff;
            v9990SpritePatternRow(s, f, sp.pattern, r, dots);
            for (int j = 0; j < 16; j++) {
                int xx = sp.x + j;
                if (dots[j] == 0 || xx < 0 || xx >= f.width) continue;
                sp.dots++;
                if (plane[xx] == PLANE_SPRITE) sp.spriteMask[r] |= 0x8000 >> j;
                else if (sp.pr1 && plane[xx] == PLANE_FRONT) sp.frontMask[r] |= 0x8000 >> j;
                if (plane[xx] != PLANE_SPRITE) lay[xx] = (uint8_t)(1 + sp.sc * 16 + dots[j]);
                plane[xx] = PLANE_SPRITE;
            }
        }
        for (int k = ln.places; k < ln.count; k++) {
            V9990Sprite& sp = f.spr[ln.id[k]];
            if (sp.pr0) continue;
            v9990SpritePatternRow(s, f, sp.pattern, (d - sp.y - 1) & 0xff, dots);
            for (int j = 0; j < 16; j++) {
                int xx = sp.x + j;
                if (dots[j] != 0 && xx >= 0 && xx < f.width) f.dropMask[(size_t)d * f.width + xx] = 1;
            }
        }
    }
    for (int i = 0; i < V9_SPRITES; i++) {
        V9990Sprite& sp = f.spr[i];
        if (f.off && !keepWhenOff) {
            sp.state = v9990SpriteLines(sp) >= 0 ? V9_SPR_SPD : V9_SPR_OFF_Y;
            f.offY += sp.state == V9_SPR_OFF_Y;
            continue;
        }
        if (sp.line0 < 0) sp.state = V9_SPR_OFF_Y;
        else if (sp.pr0) sp.state = V9_SPR_PR0;
        else if (sp.dropLine >= 0) sp.state = V9_SPR_DROPPED;
        else if (sp.x >= f.width) sp.state = V9_SPR_OFF_X;
        else sp.state = V9_SPR_SHOWN;
        switch (sp.state) {
        case V9_SPR_OFF_Y:   f.offY++; break;
        case V9_SPR_PR0:     f.pr0++; break;
        case V9_SPR_DROPPED: f.dropped++; break;
        case V9_SPR_OFF_X:   f.offX++; break;
        default:             f.shown++; break;
        }
        if (sp.dropLine >= 0 && (f.firstDropLine < 0 || sp.dropLine < f.firstDropLine)) {
            f.firstDropLine = sp.dropLine;
            f.firstDropped = i;
        }
    }
}

void v9990RenderSpriteSheets(const V9990DebugState* s, const V9990SpriteFrame& f, const uint32_t pal[64],
                             std::vector<uint32_t>& atlas, std::vector<uint32_t>& sheet)
{
    /* A pattern takes the colours of its first user; unused ones are grey. */
    int sc[256];
    for (int p = 0; p < 256; p++) sc[p] = -1;
    for (int n = 0; n < V9_SPRITES; n++) {
        const V9990Sprite& sp = f.spr[n];
        if (sp.state != V9_SPR_OFF_Y && sc[sp.pattern] < 0) sc[sp.pattern] = sp.sc;
    }
    atlas.assign((size_t)256 * 128, 0);
    sheet.assign((size_t)256 * 256, 0);
    uint8_t dots[16];
    for (int p = 0; p < 256; p++) {
        for (int r = 0; r < 16; r++) {
            v9990SpritePatternRow(s, f, p, r, dots);
            uint32_t* row = &sheet[(size_t)((p / 16) * 16 + r) * 256 + (p % 16) * 16];
            for (int j = 0; j < 16; j++) {
                if (dots[j] == 0) continue;
                uint32_t g = (uint32_t)(70 + dots[j] * 8);
                row[j] = sc[p] >= 0 ? pal[sc[p] * 16 + dots[j]] : 0xff000000u | (g << 16) | (g << 8) | g;
            }
        }
    }
    for (int n = 0; n < V9_SPRITES; n++) {
        const V9990Sprite& sp = f.spr[n];
        for (int r = 0; r < 16; r++) {
            v9990SpritePatternRow(s, f, sp.pattern, r, dots);
            uint32_t* row = &atlas[(size_t)((n / 16) * 16 + r) * 256 + (n % 16) * 16];
            for (int j = 0; j < 16; j++) if (dots[j]) row[j] = pal[sp.sc * 16 + dots[j]];
        }
    }
}

void v9990RenderSpritePlane(const V9990SpriteFrame& f, const uint32_t pal[64], uint32_t stipple, std::vector<uint32_t>& out)
{
    int w = f.width;
    out.assign((size_t)w * V9_SPRITE_LINES, 0);
    for (int d = 0; d < V9_SPRITE_LINES; d++) {
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)d * w + x;
            if (f.layer[i]) out[i] = pal[(f.layer[i] - 1) & 63];
            else if (stipple != 0 && f.dropMask[i] && ((x + d) & 1) == 0) out[i] = stipple;
        }
    }
}
