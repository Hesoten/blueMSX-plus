/*****************************************************************************
**
** The VRAM banks of a V9968 as the VDP Viewer last saw them.
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
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "VdpBanks.h"
#include "VdpDecode.h"

void VdpBankStore::forget()
{
    for (int b = 0; b < BANKS; b++) {
        std::vector<UInt8>().swap(copy[b]);
        isKnown[b] = isEmpty[b] = false;
        hot[b] = 0;
    }
    memset(blockHeat, 0, sizeof(blockHeat));
    hitsSet = false;
    sprSeen = 0;
    bgSeen = 0;
    usedSeen = 0;
    banked = false;
}

void VdpBankStore::take(int b, const UInt8* src)
{
    const size_t n = VDP_DEBUG_VRAM_SIZE;
    std::vector<UInt8>& c = copy[b];
    bool changed = !isKnown[b];
    if (isKnown[b]) {
        for (int k = 0; k < BLOCKS; k++) {
            if (memcmp(&c[(size_t)k << 8], src + ((size_t)k << 8), 256) == 0) continue;
            blockHeat[b][k] = 1;
            hot[b] = 1;
            changed = true;
        }
    }
    if (!changed) return;
    c.resize(n);
    memcpy(c.data(), src, n);
    isEmpty[b] = memcmp(src, src + 1, n - 1) == 0;
    if (!isEmpty[b]) usedSeen |= 1u << b;
    isKnown[b] = true;
    ver[b]++;
}

void VdpBankStore::note(const VdpDebugState* s)
{
    if (s->banks <= 1 || s->banks > BANKS) {
        banked = false;
        return;
    }
    if (!banked) {
        for (int b = 0; b < BANKS; b++) isKnown[b] = false;
        memset(blockHeat, 0, sizeof(blockHeat));
        memset(hot, 0, sizeof(hot));
        hitsSet = false;
        sprSeen = 0;
        bgSeen = 0;
        usedSeen = 0;
        banked = true;
    }
    if (!hitsSet) {
        memcpy(hitsBase, s->sprBankHits, sizeof(hitsBase));
        hitsSet = true;
    }
    sprSeen |= 1u << (s->regs[29] >> 4);
    bgSeen |= 1u << (s->regs[29] & 0x0f);
    for (int b = 0; b < BANKS; b++) {
        if (s->sprBankHits[b] != hitsBase[b]) sprSeen |= 1u << b;
    }
    take(s->vramBank, s->vram);
    if (s->auxBank >= 0 && s->auxBank < BANKS) take(s->auxBank, s->vramAux);
}

void VdpBankStore::fade()
{
    if (step != shell::stepVersion()) {
        step = shell::stepVersion();
        memset(blockHeat, 0, sizeof(blockHeat));
        memset(hot, 0, sizeof(hot));
    }
    float dt = shell::heatFade();
    if (dt <= 0) return;
    for (int b = 0; b < BANKS; b++) {
        if (hot[b] <= 0) continue;
        hot[b] = hot[b] > dt ? hot[b] - dt : 0;
        for (float& h : blockHeat[b]) h = h > dt ? h - dt : 0;
    }
}

bool VdpBankStore::allKnown() const
{
    for (int b = 0; b < BANKS; b++) {
        if (!isKnown[b]) return false;
    }
    return true;
}

int VdpBankStore::emptyCount() const
{
    int n = 0;
    for (int b = 0; b < BANKS; b++) n += empty(b);
    return n;
}

VdpBankThumbs::~VdpBankThumbs()
{
    free(scratch);
}

void VdpBankThumbs::forget()
{
    for (int b = 0; b < VdpBankStore::BANKS; b++) {
        pic[b].forget();
        pic[b].w = pic[b].h = 0;
    }
}

/* The registers a page decode reads: mode, colour 0, TP, lines, the V9968's layout, YJK. */
static unsigned regsKey(const UInt8* r)
{
    static const UInt8 used[32] = { 1,1,0,0,0,0,0,1, 1,1,0,0,0,0,0,0, 0,0,0,0,1,0,0,0, 0,1,0,0,0,0,0,0 };
    uint32_t w[32];
    for (int i = 0; i < 32; i++) w[i] = used[i] ? r[i] : 0;
    return fnvWords(w, 32);
}

void VdpBankThumbs::update(const VdpDebugState* s, const VdpBankStore& store, const VdpLayout& l, int mode, int size, int budget)
{
    if (!vdpIsBitmapMode(mode) || size < 1) return;
    unsigned key = fnvWords(l.rgb != NULL ? l.rgb : s->paletteRgb, 16) ^ regsKey(l.regs) ^ (unsigned)(mode * 0x9e3779b9u) ^
                   (unsigned)(size << 8) ^ (unsigned)((s->interleaved << 1) | (s->bitmapLinear << 2) | (s->vramA17 << 3)) ^
                   fnvWords(s->paletteExt, (size_t)s->paletteExtCount) ^ (unsigned)(s->paletteExtCount << 20);
    bool copied = false;
    for (int b = 0; b < VdpBankStore::BANKS && budget > 0; b++) {
        if (!store.known(b) || (pic[b].w > 0 && drawnVer[b] == store.version(b) && drawnKey[b] == key)) continue;
        budget--;
        if (scratch == NULL && (scratch = (VdpDebugState*)malloc(sizeof(VdpDebugState))) == NULL) return;
        if (!copied) {
            memcpy(scratch, s, offsetof(VdpDebugState, vram));
            scratch->auxBank = -1;
            copied = true;
        }
        memcpy(scratch->vram, store.bytes(b), VDP_DEBUG_VRAM_SIZE);
        VdpBitmapOptions o = { mode, -1, 256, 0, VDP_COLOR0_AUTO, false, false };
        int w, h;
        vdpDecodeBitmap(scratch, l, o, page, &w, &h);
        /* The pages one under the other, 256 lines each; in square cells a pixel apart, the grid centred. */
        int pages = h / 256, cols = 1;
        while (cols * cols < pages) cols++;
        int rows = (pages + cols - 1) / cols, cell = (size - (cols - 1)) / cols;
        if (cell < 1) cell = 1;
        int ox = (size - (cols * cell + cols - 1)) / 2, oy = (size - (rows * cell + rows - 1)) / 2;
        ViewPicture& p = pic[b];
        p.w = p.h = size;
        p.img.assign((size_t)size * size, 0xff000000u);
        /* Each picture pixel the mean of the page's dots it covers. */
        for (int pg = 0; pg < pages; pg++) {
            int cx = ox + (pg % cols) * (cell + 1), cy = oy + (pg / cols) * (cell + 1);
            for (int j = 0; j < cell; j++) {
                int y0 = pg * 256 + j * 256 / cell, y1 = pg * 256 + (j + 1) * 256 / cell;
                if (y1 <= y0) y1 = y0 + 1;
                for (int i = 0; i < cell; i++) {
                    int x0 = i * w / cell, x1 = (i + 1) * w / cell;
                    if (x1 <= x0) x1 = x0 + 1;
                    unsigned r = 0, g = 0, bl = 0, n = 0;
                    for (int y = y0; y < y1; y++) {
                        const uint32_t* row = &page[(size_t)y * w];
                        for (int x = x0; x < x1; x++, n++) {
                            r += row[x] & 0xff;
                            g += (row[x] >> 8) & 0xff;
                            bl += (row[x] >> 16) & 0xff;
                        }
                    }
                    p.img[(size_t)(cy + j) * size + cx + i] = 0xff000000u | ((bl / n) << 16) | ((g / n) << 8) | (r / n);
                }
            }
        }
        p.upload(true);
        drawnVer[b] = store.version(b);
        drawnKey[b] = key;
    }
}
