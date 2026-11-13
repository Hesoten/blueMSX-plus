/*****************************************************************************
**
** Decoding of VDP snapshots into images and VRAM table addresses.
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
#include "VdpDecode.h"
#include "ToolText.h"

/* Addresses follow the renderer: table bases are masks with the low bits
** set, ANDed with the address the VDP would fetch. */

static int rd(const VdpDebugState* s, int phys)
{
    return phys >= 0 && phys < VDP_DEBUG_VRAM_SIZE ? s->vram[phys] : 0xff;
}

static uint32_t rgba(int r, int g, int b)
{
    return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

uint32_t vdpRgbToRgba(uint32_t rgb)
{
    return rgba((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

uint32_t vdpScreen8Rgba(int i)
{
    return rgba(255 * ((i >> 2) & 7) / 7, 255 * ((i >> 5) & 7) / 7,
                255 * ((i & 3) == 3 ? 7 : 2 * (i & 3)) / 7);
}

static uint32_t yjkRgba(int y, int J, int K)
{
    int j = (J & 0x1f) - (J & 0x20);
    int k = (K & 0x1f) - (K & 0x20);
    int r = 255 * (y + j) / 31;
    int g = 255 * (y + k) / 31;
    int b = 255 * ((5 * y - 2 * j - k) / 4) / 31;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return rgba(r, g, b);
}

bool vdpIsBitmapMode(int mode)
{
    return mode == 5 || mode == 6 || mode == 7 || mode == 8 || mode == 10 || mode == 12;
}

bool vdpIsPatternMode(int mode)
{
    return mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 4 || mode == 13;
}

bool vdpIsMsx1(const VdpDebugState* s)
{
    return s->version == VDP_TMS9929A || s->version == VDP_TMS99x8A || s->version == VDP_TMS9918A;
}

int vdpSpriteMode(int mode)
{
    if (mode >= 1 && mode <= 3) return 1;
    return mode >= 4 && mode <= 12 ? 2 : 0;
}

const char* vdpCmdName(int cm, bool ext)
{
    static const char* names[16] = { "STOP", "-", "-", "-", "POINT", "PSET", "SRCH", "LINE",
                                     "LMMV", "LMMM", "LMCM", "LMMC", "HMMV", "HMMM", "YMMM", "HMMC" };
    static const char* extNames[4] = { "STOP", "LFMM", "LFMC", "LRMM" };
    return ext && (cm & 15) < 4 ? extNames[cm & 15] : names[cm & 15];
}

const char* vdpModeName(int mode)
{
    switch (mode) {
    case 0:  return ts(TS_MODE_TEXT1);
    case 13: return ts(TS_MODE_TEXT2);
    case 1:  return "SCREEN 1 (GRAPHIC1)";
    case 2:  return "SCREEN 2 (GRAPHIC2)";
    case 3:  return "SCREEN 3 (MULTICOLOR)";
    case 4:  return "SCREEN 4 (GRAPHIC3)";
    case 5:  return "SCREEN 5 (GRAPHIC4)";
    case 6:  return "SCREEN 6 (GRAPHIC5)";
    case 7:  return "SCREEN 7 (GRAPHIC6)";
    case 8:  return "SCREEN 8 (GRAPHIC7)";
    case 10: return ts(TS_MODE_YJK_PALETTE);
    case 12: return "SCREEN 12 (YJK)";
    }
    return ts(TS_MODE_UNKNOWN);
}

const char* vdpVersionName(int version)
{
    switch (version) {
    case VDP_V9938:    return "V9938";
    case VDP_V9958:    return "V9958";
    case VDP_V9968:    return "V9968";
    case VDP_TMS9929A: return "TMS9929A";
    case VDP_TMS99x8A: return "TMS99x8A";
    case VDP_TMS9918A: return "TMS9918A";
    }
    return "VDP";
}

VdpLayout vdpLayoutOf(const VdpDebugState* s)
{
    VdpLayout l = { s->regs, s->screenMode, s->blinkFlag };
    return l;
}

int vdpScreenScroll(const VdpDebugState* s, const VdpLayout& l)
{
    int split = l.mode == 0 || l.mode == 13 ? l.textSplit : 0;
    return ((vdpIsMsx1(s) ? 0 : l.regs[23]) - split) & 0xff;
}

int vdpSetAdjust(int r18, bool vertical)
{
    int n = (vertical ? r18 >> 4 : r18) & 15;
    return (n & 8) ? 16 - n : -n;
}

void vdpPaletteRegRgb(const UInt16 reg[16], UInt32 out[16])
{
    for (int i = 0; i < 16; i++) {
        UInt32 r = ((reg[i] >> 4) & 7) * 255 / 7, g = ((reg[i] >> 8) & 7) * 255 / 7, b = (reg[i] & 7) * 255 / 7;
        out[i] = (r << 16) | (g << 8) | b;
    }
}

/* ---- VRAM layout ----------------------------------------------------- */

/* A V9968 in sprite mode 3 lays SCREEN 7-12 out in address order. */
static bool bitmapLinear(const VdpDebugState* s, const UInt8* r)
{
    return s->version == VDP_V9968 && (r[20] & 0x08);
}

static bool pairedMode(int mode)
{
    return mode == 7 || mode == 8 || mode == 10 || mode == 12;
}

bool vdpInterleavedIn(const VdpDebugState* s, int mode)
{
    return pairedMode(mode) && !bitmapLinear(s, s->regs);
}

/* A 256K V9968 takes one more R#2 bit, so the second 128K holds pages too. */
int vdpBitmapPage(const VdpDebugState* s, const UInt8* r, int mode)
{
    int bits = mode <= 6 ? 3 : 1;
    return (r[2] >> 5) & (s->vramA17 ? bits * 2 + 1 : bits);
}

bool vdpIsFlatInterlace(const VdpDebugState* s, const UInt8* r)
{
    return s->version == VDP_V9968 && (r[20] & 0x20);
}

unsigned vdpShownPageBits(bool flat, int page)
{
    return flat && (page & 1) ? 3u << (page - 1) : 1u << page;
}

/* ---- tables ---------------------------------------------------------- */

static int chrTabMask(const VdpDebugState* s, const UInt8* r)
{
    return ((((int)r[2] << 10) & ~((int)(r[25] & 1) << 15)) | 0x3ff) & s->vramMask;
}

static int chrGenMask(const VdpDebugState* s, const UInt8* r)
{
    return (((int)r[4] << 11) | 0x7ff) & s->vramMask;
}

static int colTabMask(const VdpDebugState* s, const UInt8* r)
{
    return (((int)r[10] << 14) | ((int)r[3] << 6) | 0x3f) & s->vramMask;
}

/* A V9968's sprite mode 3: 64 planes of 8 bytes, patterns in 32K pages from R#6. */
static bool sp3In(const VdpDebugState* s, const UInt8* r, int mode)
{
    return s->version == VDP_V9968 && (r[20] & 0x08) && vdpSpriteMode(mode) != 0;
}

int vdpSp3AttrBase(const UInt8* r)
{
    return (((int)r[11] & 7) << 15 | (int)r[5] << 7) & ~0x1ff;
}

int vdpSp3PatternBase(const UInt8* r)
{
    return ((int)r[6] << 11) & 0x3f800;
}

int vdpSp3Alias(const UInt8* r, int n)
{
    return (((n >> 4) & r[5] & 3) << 4) | (n & 15);
}

/* The pattern pages the planes before the terminator name, a tall pattern running into the next. */
static void sp3PatternSpan(const VdpDebugState* s, const UInt8* r, int* addr, int* len)
{
    int base = vdpSp3AttrBase(r), lo = 8, hi = -1;
    for (int n = 0; n < 64; n++) {
        int alias = vdpSp3Alias(r, n);
        int a = (base | ((alias >> 4) << 7) | ((alias & 15) << 3)) & s->vramMask;
        int b1 = rd(s, a + 1), page = (rd(s, a + 5) >> 4) & 7;
        if ((rd(s, a) | (b1 & 3) << 8) == 216 && !(r[25] & 0x80)) break;
        if (rd(s, a + 6) == 0) continue;
        int last = page + (((rd(s, a + 7) >> 4) * 16 + (16 << (b1 >> 6)) - 1) >> 8);
        if (page < lo) lo = page;
        if (last > hi) hi = last;
    }
    if (hi < 0) lo = hi = 0;
    *addr = (vdpSp3PatternBase(r) + (lo << 15)) & s->vramMask;
    *len = (hi - lo + 1) << 15;
}

void vdpDecodeTables(const VdpDebugState* s, const VdpLayout& l, VdpTables* t)
{
    const UInt8* r = l.regs;
    int mode = l.mode;
    int m = s->vramMask;
    int sat = (((int)r[11] << 15) | ((int)r[5] << 7)) & m;

    t->name = t->patternGen = t->color = -1;
    t->spriteAttr = t->spritePattern = t->spriteColor = -1;

    if (vdpIsBitmapMode(mode)) {
        t->name = vdpBitmapPage(s, r, mode) * (mode <= 6 ? 0x8000 : 0x10000);
    }
    else {
        /* SP2 clears A15 in every pattern mode too, as the emulated VDP's renderer reads the name table. */
        t->name = chrTabMask(s, r) & ~(mode == 13 ? 0xfff : 0x3ff);
    }
    if (mode == 13) {
        t->color = (((int)r[10] << 14) | ((int)r[3] << 6)) & m & ~0x1ff;
    }

    if (mode == 2 || mode == 4) {
        t->patternGen = ((int)r[4] << 11) & m & ~0x1fff;
        t->color      = (((int)r[10] << 14) | ((int)r[3] << 6)) & m & ~0x1fff;
    }
    else if (mode == 0 || mode == 13 || mode == 1 || mode == 3) {
        t->patternGen = ((int)r[4] << 11) & m;
        if (mode == 1) {
            t->color = (((int)r[10] << 14) | ((int)r[3] << 6)) & m;
        }
    }

    if (sp3In(s, r, mode)) {
        t->spriteAttr = vdpSp3AttrBase(r) & m;
        t->spritePattern = vdpSp3PatternBase(r) & m;
        return;
    }
    if (vdpSpriteMode(mode) == 1) {
        t->spriteAttr = sat;
    }
    else if (vdpSpriteMode(mode) == 2) {
        /* Colours sit below the attributes; R#5 A9 stays in the attribute address. */
        t->spriteAttr  = sat & 0x1fe00;
        t->spriteColor = sat & ~0x3ff;
    }
    if (vdpSpriteMode(mode) != 0) {
        t->spritePattern = ((int)r[6] << 11) & m;
    }
}

void vdpPaletteRgba(const VdpDebugState* s, const VdpLayout& l, int color0, uint32_t out[16])
{
    int mode = l.mode;
    int bg = l.regs[7] & 0x0f;
    const UInt32* src = l.rgb ? l.rgb : s->paletteRgb;
    int i;
    for (i = 0; i < 16; i++) {
        out[i] = vdpRgbToRgba(src[i]);
    }
    switch (color0) {
    case VDP_COLOR0_AUTO: {
        /* Colour 0 shows the backdrop: always in TEXT1/2 and MULTICOLOR, with TP clear in
        ** the graphic modes; SCREEN 6 keeps entry 0, and from SCREEN 8 on it never does. */
        bool tp = !vdpIsMsx1(s) && (l.regs[8] & 0x20);
        bool clear = mode == 0 || mode == 3 || mode == 13 || (mode <= 7 && mode != 6 && !tp);
        if (bg != 0 && clear) {
            out[0] = out[bg];
        }
        break;
    }
    case VDP_COLOR0_BACKDROP:
        out[0] = mode == 6 ? out[l.regs[7] & 3] : out[bg];
        break;
    case VDP_COLOR0_TRANSPARENT:
        out[0] = 0;
        break;
    }
}

/* ---- bitmap modes ---------------------------------------------------- */

int vdpBitmapWidth(int mode)
{
    return mode == 6 || mode == 7 ? 512 : 256;
}

int vdpBitmapPageCount(const VdpDebugState* s, int mode)
{
    /* Only a 256K V9968 shows pages past 128K. */
    int size = s->vramSize < 0x20000 || s->vramA17 ? s->vramSize : 0x20000;
    int n = size / (mode == 5 || mode == 6 ? 0x8000 : 0x10000);
    return n < 1 ? 1 : n;
}

bool vdpDisplayedBitmap(const VdpDebugState* s, const VdpLayout& l, VdpBitmapOptions* o)
{
    if (!vdpIsBitmapMode(l.mode)) {
        return false;
    }
    o->mode    = l.mode;
    o->page    = vdpBitmapPage(s, l.regs, l.mode);
    o->lines   = (l.regs[9] & 0x80) ? 212 : 192;
    o->vscroll = l.regs[23];
    o->color0  = VDP_COLOR0_AUTO;
    o->displayed = true;
    o->evenPair  = false;
    /* At a frame tap S#2 has flipped for the next frame already; the log keeps the one shown.
    ** Without a log S#2 is a guess. */
    o->field     = s->raster[0].valid ? s->raster[0].field : (s->status[2] >> 1) & 1;
    return true;
}

/* The physical start of one line. Shown as displayed, R#2 masks the line
** number as on the chip; a page picked by hand is read whole. */
static int lineBase(const VdpDebugState* s, const UInt8* r, const VdpBitmapOptions& o, int page, int line)
{
    bool linear = o.mode >= 7 && bitmapLinear(s, r);
    bool paired = o.mode >= 7 && !linear;
    int  shift  = linear ? 8 : 7;
    int  mask;

    if (o.displayed) {
        mask = chrTabMask(s, r);
        /* As the V9968 moves the base: interleaved, the bit above the page
        ** steps over the odd bytes into the second 128K; linear, all of it up one. */
        if (paired && s->version == VDP_V9968) mask = ((mask & 0xffff) | ((mask & 0x10000) << 1)) & s->vramMask;
        else if (linear) mask = (mask << 1) & s->vramMask;
        if (vdpIsFlatInterlace(s, r)) {
            /* The row moves up one past the field bit; the page bit becomes row bit 7's gate. */
            return (mask & ((-1 << (shift + 9)) | ((line & 0xff) << (shift + 1)))) | ((o.field & 1) << shift);
        }
        if (o.evenPair) mask &= ~(1 << (shift + 8));
    }
    else {
        int at = linear ? page << 16 : paired ? ((page & 1) << 15) | ((page & 2) << 16) : page << 15;
        mask = (at | ((1 << (shift + 8)) - 1)) & s->vramMask;
    }
    return mask & ((-1 << (shift + 8)) | ((line & 0xff) << shift));
}

/* Byte n (0-127 in SCREEN 5/6, 0-255 from 7 on) of a line. */
static int lineByte(const VdpDebugState* s, const UInt8* r, int mode, int base, int n)
{
    if (mode <= 6 || bitmapLinear(s, r)) {
        return base + n;
    }
    return base + (n >> 1) + ((n & 1) ? s->vram128 : 0);
}

/* A V9968's EPAL makes the 256 colour modes index the extended palette,
** whose first 16 entries are the 16 colour palette. */
uint32_t vdpExtRgba(const VdpDebugState* s, const uint32_t pal[16], int i)
{
    return i < 16 ? pal[i] : vdpRgbToRgba(s->paletteExt[i]);
}

static uint32_t bitmapPixel(const VdpDebugState* s, const UInt8* r, const VdpBitmapOptions& o, const uint32_t pal[16],
                            int base, int x, int* value, int* phys)
{
    int p, b;
    switch (o.mode) {
    case 5:
        p = lineByte(s, r, 5, base, x >> 1);
        b = rd(s, p);
        b = (x & 1) ? b & 0x0f : b >> 4;
        *value = b; *phys = p;
        return pal[b];
    case 6:
        p = lineByte(s, r, 6, base, x >> 2);
        b = (rd(s, p) >> (6 - 2 * (x & 3))) & 3;
        *value = b; *phys = p;
        return pal[b];
    case 7:
        p = lineByte(s, r, 7, base, x >> 1);
        b = rd(s, p);
        b = (x & 1) ? b & 0x0f : b >> 4;
        *value = b; *phys = p;
        return pal[b];
    case 8:
        p = lineByte(s, r, 8, base, x);
        b = rd(s, p);
        *value = b; *phys = p;
        if (b == 0 && o.color0 == VDP_COLOR0_TRANSPARENT) return 0;
        if (s->paletteExtCount) {
            return vdpExtRgba(s, pal, b == 0 && o.color0 == VDP_COLOR0_BACKDROP ? r[7] : b);
        }
        if (b == 0 && o.color0 == VDP_COLOR0_BACKDROP) return vdpScreen8Rgba(r[7]);
        return vdpScreen8Rgba(b);
    default: {
        int g = x & ~3;
        int t0 = rd(s, lineByte(s, r, 8, base, g));
        int t1 = rd(s, lineByte(s, r, 8, base, g + 1));
        int t2 = rd(s, lineByte(s, r, 8, base, g + 2));
        int t3 = rd(s, lineByte(s, r, 8, base, g + 3));
        int K = (t0 & 7) | ((t1 & 7) << 3);
        int J = (t2 & 7) | ((t3 & 7) << 3);
        p = lineByte(s, r, 8, base, x);
        b = rd(s, p);
        *value = b; *phys = p;
        if (o.mode == 10 && ((b >> 3) & 1)) {
            /* With EPAL a colour dot takes the whole byte as its index. */
            return s->paletteExtCount ? vdpExtRgba(s, pal, b) : pal[b >> 4];
        }
        return yjkRgba(b >> 3, J, K);
    }
    }
}

/* The layout's registers, read as another mode. */
static VdpLayout readAs(const VdpLayout& l, int mode)
{
    VdpLayout r = l;
    r.mode = mode;
    return r;
}

void vdpDecodeBitmap(const VdpDebugState* s, const VdpLayout& l, const VdpBitmapOptions& o, VdpImage& out, int* w, int* h)
{
    uint32_t pal[16];
    int pages = o.page < 0 ? vdpBitmapPageCount(s, o.mode) : 1;
    int lines = o.page < 0 ? 256 : o.lines;
    int width = vdpBitmapWidth(o.mode);
    int x, y, k;

    vdpPaletteRgba(s, readAs(l, o.mode), o.color0, pal);
    *w = width;
    *h = lines * pages;
    out.resize((size_t)width * lines * pages);
    for (k = 0; k < pages; k++) {
        int page = o.page < 0 ? k : o.page;
        for (y = 0; y < lines; y++) {
            int base = lineBase(s, l.regs, o, page, y + (o.page < 0 ? 0 : o.vscroll));
            uint32_t* row = &out[((size_t)k * lines + y) * width];
            for (x = 0; x < width; x++) {
                int v, p;
                row[x] = bitmapPixel(s, l.regs, o, pal, base, x, &v, &p);
            }
        }
    }
}

bool vdpBitmapPixel(const VdpDebugState* s, const VdpLayout& l, const VdpBitmapOptions& o, int x, int y, VdpPixelInfo* out)
{
    uint32_t pal[16];
    int lines = o.page < 0 ? 256 : o.lines;
    int page = o.page < 0 ? y / 256 : o.page;
    int line = (y % lines) + (o.page < 0 ? 0 : o.vscroll);
    int width = vdpBitmapWidth(o.mode);
    int base, n;

    if (x < 0 || x >= width || y < 0) {
        return false;
    }
    vdpPaletteRgba(s, readAs(l, o.mode), o.color0, pal);
    base = lineBase(s, l.regs, o, page, line);
    out->rgba = bitmapPixel(s, l.regs, o, pal, base, x, &out->value, &out->physical);
    n = o.mode == 5 || o.mode == 7 ? x >> 1 : o.mode == 6 ? x >> 2 : x;
    /* From the line's physical start, so R#2 masking and a small VRAM show
    ** as on the chip; from SCREEN 7 on the two 64K halves interleave unless SP3 is on. */
    out->logical = o.mode <= 6 || bitmapLinear(s, l.regs) ? base + n
                 : ((base + (n >> 1)) & s->vramA17) | (((base + (n >> 1)) & 0xffff) << 1) | (n & 1);
    return true;
}

/* ---- pattern modes --------------------------------------------------- */

int vdpNameTableWidth(int mode)
{
    return mode == 0 ? 240 : mode == 13 ? 480 : 256;
}

/* The palette and, at 16, what the TEXT2 blink colours read for 0: under AUTO the
** true entry 0, as they bypass the backdrop. */
static void charPalette(const VdpDebugState* s, const VdpLayout& l, int color0, uint32_t pal[17])
{
    vdpPaletteRgba(s, l, color0, pal);
    pal[16] = color0 == VDP_COLOR0_AUTO ? vdpRgbToRgba((l.rgb ? l.rgb : s->paletteRgb)[0]) : pal[0];
}

static void charColors(const VdpLayout& l, const uint32_t pal[17], int nameIdx,
                       int colorByte, uint32_t* fg, uint32_t* bg)
{
    if (l.mode == 0) {
        *fg = pal[l.regs[7] >> 4];
        *bg = pal[l.regs[7] & 0x0f];
    }
    else if (l.mode == 13) {
        int blink = nameIdx >= 0 && ((colorByte << (nameIdx & 7)) & 0x80) && !l.blink;
        if (blink) {
            /* A blink foreground 0 is drawn in the blink background colour. */
            int b = l.regs[12] & 0x0f, f = (l.regs[12] >> 4) ? l.regs[12] >> 4 : b;
            *fg = f ? pal[f] : pal[16];
            *bg = b ? pal[b] : pal[16];
        }
        else {
            *fg = pal[l.regs[7] >> 4];
            *bg = pal[l.regs[7] & 0x0f];
        }
    }
    else {
        *fg = pal[colorByte >> 4];
        *bg = pal[colorByte & 0x0f];
    }
}

/* One 8-pixel row of character ch, line r (0-7), third (SCREEN 2/4) or the
** screen line y for SCREEN 3. */
static void charRow(const VdpDebugState* s, const VdpLayout& l, const uint32_t pal[17], int ch, int r,
                    int third, int nameIdx, uint32_t* dst, int width)
{
    int i, mode = l.mode;
    if (mode == 3) {
        int c = rd(s, (chrGenMask(s, l.regs) & ((-1 << 11) | (r & 7))) | (ch * 8));
        for (i = 0; i < width; i++) dst[i] = pal[i < 4 ? c >> 4 : c & 0x0f];
        return;
    }
    int pat, col;
    if (mode == 2 || mode == 4) {
        int idx = (-1 << 13) | (third << 11) | (ch * 8) | (r & 7);
        pat = rd(s, chrGenMask(s, l.regs) & idx);
        col = rd(s, colTabMask(s, l.regs) & idx);
    }
    else {
        pat = rd(s, (chrGenMask(s, l.regs) & ((-1 << 11) | (r & 7))) | (ch * 8));
        col = mode == 1 ? rd(s, colTabMask(s, l.regs) & ((ch / 8) | (-1 << 6)))
            : mode == 13 && nameIdx >= 0 ? rd(s, colTabMask(s, l.regs) & ((-1 << 9) | (nameIdx / 8))) : 0;
    }
    uint32_t fg, bg;
    charColors(l, pal, nameIdx, col, &fg, &bg);
    for (i = 0; i < width; i++) dst[i] = (pat << i) & 0x80 ? fg : bg;
}

void vdpDecodePatternSheet(const VdpDebugState* s, const VdpLayout& l, int color0, VdpImage& out, int* w, int* h)
{
    uint32_t pal[17];
    int blocks = vdpSheetBlocks(s, l);
    int c, r;
    charPalette(s, l, color0, pal);
    *w = 256;
    *h = 64 * blocks;
    out.assign((size_t)256 * 64 * blocks, 0);
    for (c = 0; c < 256 * blocks; c++) {
        int third = c >> 8, ch = c & 0xff;
        int x0 = (ch & 31) * 8, y0 = (third * 8 + ch / 32) * 8;
        for (r = 0; r < 8; r++) {
            charRow(s, l, pal, ch, r, third, -1, &out[(size_t)(y0 + r) * 256 + x0], 8);
        }
    }
}

bool vdpSheetChar(const VdpDebugState* s, const VdpLayout& l, int x, int y, VdpCharInfo* out)
{
    int mode = l.mode;
    if (x < 0 || x >= 256 || y < 0 || y >= 64 * vdpSheetBlocks(s, l)) {
        return false;
    }
    int third = y / 64, ch = (y % 64) / 8 * 32 + x / 8;
    out->ch = third * 256 + ch;
    out->col = x / 8;
    out->row = y / 8;
    out->nameAddr = -1;
    if (mode == 2 || mode == 4) {
        int idx = (-1 << 13) | (third << 11) | (ch * 8);
        out->patternAddr = chrGenMask(s, l.regs) & idx;
        out->colorAddr   = colTabMask(s, l.regs) & idx;
    }
    else {
        out->patternAddr = (chrGenMask(s, l.regs) & (-1 << 11)) | (ch * 8);
        out->colorAddr   = mode == 1 ? colTabMask(s, l.regs) & ((ch / 8) | (-1 << 6)) : -1;
    }
    return true;
}

/* The name table entry for a screen character cell. */
static int nameAddr(const VdpDebugState* s, const VdpLayout& l, int col, int row)
{
    if (l.mode == 0)  return chrTabMask(s, l.regs) & ((-1 << 12) | (0xc00 + 40 * row + col));
    if (l.mode == 13) return chrTabMask(s, l.regs) & ((-1 << 12) | (80 * row + col));
    return chrTabMask(s, l.regs) & ((-1 << 10) | (32 * row + col));
}

static int firstLineOf(const VdpDebugState* s, const VdpLayout& l, int firstLine)
{
    return firstLine >= 0 ? firstLine & 0xff : vdpScreenScroll(s, l);
}

/* Name table rows the displayed lines read, from row 0: 24, 27 with 212 lines, up to 32 with R#23. */
static int nameRows(const VdpDebugState* s, const VdpLayout& l)
{
    int end = firstLineOf(s, l, -1) + (!vdpIsMsx1(s) && (l.regs[9] & 0x80) ? 212 : 192);
    return end > 256 ? 32 : (end + 7) / 8;
}

int vdpSheetBlocks(const VdpDebugState* s, const VdpLayout& l)
{
    if (l.mode != 2 && l.mode != 4) {
        return 1;
    }
    /* Block 3 reads what block (pm | cm) reads unless R#4 and R#3 keep both its address bits. */
    int pm = (chrGenMask(s, l.regs) >> 11) & 3, cm = (colTabMask(s, l.regs) >> 11) & 3;
    return nameRows(s, l) > 24 && (pm | cm) == 3 ? 4 : 3;
}

void vdpDecodeNameTable(const VdpDebugState* s, const VdpLayout& l, int lines, int firstLine, int color0, VdpImage& out, int* w, int* h)
{
    uint32_t pal[17];
    int mode = l.mode;
    int cw = mode == 0 || mode == 13 ? 6 : 8;
    int width = vdpNameTableWidth(mode);
    int cols = width / cw;
    int vscroll = firstLineOf(s, l, firstLine);
    int x, y;
    charPalette(s, l, color0, pal);
    *w = width;
    *h = lines;
    out.assign((size_t)width * lines, 0);
    for (y = 0; y < lines; y++) {
        int sy = (y + vscroll) & 0xff;
        for (x = 0; x < cols; x++) {
            int idx = mode == 13 ? 80 * (sy / 8) + x : -1;
            int ch = rd(s, nameAddr(s, l, x, sy / 8));
            uint32_t buf[8];
            if (mode == 3) {
                charRow(s, l, pal, ch, ((sy / 8) & 3) * 2 + ((sy >> 2) & 1), 0, -1, buf, 8);
            }
            else {
                charRow(s, l, pal, ch, sy & 7, (sy & 0xc0) >> 6, idx, buf, 8);
            }
            for (int i = 0; i < cw; i++) out[(size_t)y * width + x * cw + i] = buf[i];
        }
    }
}

bool vdpNameTableChar(const VdpDebugState* s, const VdpLayout& l, int x, int y, int firstLine, VdpCharInfo* out)
{
    int mode = l.mode;
    int cw = mode == 0 || mode == 13 ? 6 : 8;
    int vscroll = firstLineOf(s, l, firstLine);
    if (x < 0 || x >= vdpNameTableWidth(mode) || y < 0) {
        return false;
    }
    int sy = (y + vscroll) & 0xff;
    int col = x / cw, row = sy / 8;
    out->col = col;
    out->row = row;
    out->nameAddr = nameAddr(s, l, col, row);
    int ch = rd(s, out->nameAddr);
    /* Rows 24-31 of SCREEN 2/4 read a fourth block, 0x1800 on before the R#4 and R#3 masks. */
    int third = mode == 2 || mode == 4 ? (sy & 0xc0) >> 6 : 0;
    out->ch = third * 256 + ch;
    if (mode == 2 || mode == 4) {
        int idx = (-1 << 13) | (third << 11) | (ch * 8);
        out->patternAddr = chrGenMask(s, l.regs) & idx;
        out->colorAddr   = colTabMask(s, l.regs) & idx;
    }
    else {
        out->patternAddr = (chrGenMask(s, l.regs) & (-1 << 11)) | (ch * 8);
        out->colorAddr   = mode == 1 ? colTabMask(s, l.regs) & ((ch / 8) | (-1 << 6)) :
                           mode == 13 ? colTabMask(s, l.regs) & ((-1 << 9) | ((80 * row + col) / 8)) : -1;
    }
    return true;
}

/* ---- ranges ---------------------------------------------------------- */

int vdpTableRanges(const VdpDebugState* s, const VdpLayout& l, VdpRange out[8])
{
    VdpTables t;
    int n = 0, mode = l.mode;
    /* A fourth block on the sheet reads 0x800 past the first three in each table whose two block bits
    ** (R#4 for patterns, R#3 for colours) are both set; otherwise it mirrors bytes already covered. */
    bool four = vdpSheetBlocks(s, l) == 4;
    int patLen = four && ((chrGenMask(s, l.regs) >> 11) & 3) == 3 ? 0x2000 : 0x1800;
    int colLen = four && ((colTabMask(s, l.regs) >> 11) & 3) == 3 ? 0x2000 : 0x1800;
    int rows = nameRows(s, l);
    vdpDecodeTables(s, l, &t);
    if (t.name >= 0) {
        int len, cap;
        if (vdpIsBitmapMode(mode)) len = cap = mode <= 6 ? 0x8000 : 0x10000;
        /* TEXT1 rows past 0x400 wrap to the start of its 4K, so its range stops there. */
        else if (mode == 0) { len = 40 * rows < 0x400 ? 40 * rows : 0x400; cap = 0x400; }
        else if (mode == 13) { len = 80 * rows; cap = 80 * 32; }
        else { len = 32 * rows; cap = 0x400; }
        out[n].addr = t.name; out[n].len = len; out[n].cap = cap; out[n].kind = VDP_RANGE_NAME;
        out[n++].name = ts(vdpIsBitmapMode(mode) ? TS_RANGE_DISPLAYED_PAGE : TS_RANGE_NAME_TABLE);
    }
    if (t.patternGen >= 0) {
        out[n].addr = t.patternGen; out[n].len = mode == 2 || mode == 4 ? patLen : 0x800;
        out[n].cap = mode == 2 || mode == 4 ? 0x2000 : 0x800;
        out[n].kind = VDP_RANGE_PATTERN; out[n++].name = ts(TS_RANGE_PATTERN_GENERATOR);
    }
    if (t.color >= 0) {
        out[n].addr = t.color; out[n].len = mode == 1 ? 32 : mode == 13 ? 10 * rows : colLen;
        out[n].cap = mode == 1 ? 32 : mode == 13 ? 10 * 32 : 0x2000;
        out[n].kind = VDP_RANGE_COLOR; out[n++].name = ts(mode == 13 ? TS_RANGE_BLINK_TABLE : TS_RANGE_COLOUR_TABLE);
    }
    if (t.spriteAttr >= 0) {
        out[n].addr = t.spriteAttr; out[n].len = out[n].cap = sp3In(s, l.regs, mode) ? 0x200 : 128;
        out[n].kind = VDP_RANGE_SPRITE_ATTR; out[n++].name = ts(TS_RANGE_SPRITE_ATTRIBUTES);
    }
    if (t.spritePattern >= 0) {
        out[n].addr = t.spritePattern; out[n].len = out[n].cap = 0x800;
        if (sp3In(s, l.regs, mode)) { sp3PatternSpan(s, l.regs, &out[n].addr, &out[n].len); out[n].cap = 8 << 15; }
        out[n].kind = VDP_RANGE_SPRITE_PATTERN; out[n++].name = ts(TS_RANGE_SPRITE_PATTERNS);
    }
    if (t.spriteColor >= 0) {
        out[n].addr = t.spriteColor; out[n].len = out[n].cap = 512;
        out[n].kind = VDP_RANGE_SPRITE_COLOR; out[n++].name = ts(TS_RANGE_SPRITE_COLOURS);
    }
    return n;
}

int vdpPhysicalAddress(const VdpDebugState* s, int mode, int logical)
{
    /* From SCREEN 7 on, even addresses sit in the low 64K, odd ones at vram128;
    ** a 256K V9968 does that within each 128K half. */
    if (vdpInterleavedIn(s, mode)) {
        return (logical & s->vramA17) | (((logical & 0x1ffff) >> 1) + ((logical & 1) ? s->vram128 : 0));
    }
    return logical;
}

int vdpLogicalAddress(const VdpDebugState* s, int mode, int physical)
{
    if (vdpInterleavedIn(s, mode)) {
        int half = physical & s->vramA17, p = physical & ~s->vramA17;
        return half | (s->vram128 && p >= s->vram128 ? ((p - s->vram128) << 1) | 1 : p << 1);
    }
    return physical;
}

int vdpCharUseCount(const VdpDebugState* s, const VdpLayout& l, int lines, int firstLine, int ch)
{
    int cw = l.mode == 0 || l.mode == 13 ? 6 : 8;
    int cols = vdpNameTableWidth(l.mode) / cw;
    int vs = firstLineOf(s, l, firstLine);
    int count = 0;
    VdpCharInfo ci;
    /* Cell by cell from the first line's row; a scrolled screen shows one more. */
    for (int y = -(vs & 7); y < lines; y += 8) {
        for (int x = 0; x < cols; x++) {
            if (vdpNameTableChar(s, l, x * cw, y < 0 ? 0 : y, vs, &ci) && ci.ch == ch) count++;
        }
    }
    return count;
}
