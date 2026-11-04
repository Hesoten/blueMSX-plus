/*****************************************************************************
**
** The VDP Viewer's views and cards of a TMS99xx, V9938 or V9958.
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
#include <math.h>
#include <stddef.h>
#include <algorithm>
#include <memory>
#include <stdio.h>
#include <string.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiTheme.h"
#include "ImGuiWidgets.h"
#include "ChipView.h"
#include "ViewPicture.h"
#include "RegTables.h"
#include "VdpDecode.h"
#include "VdpSprite.h"
#include "VdpSprite3View.h"
#include "VdpCmd9968.h"
#include "VdpRaster.h"
#include "VdpParts.h"
#include "ToolText.h"
extern "C" {
#include "Emulator.h"
#include "Properties.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace {

using namespace parts;

enum { C_SELECTION, C_SPLITS, C_DISPLAY, C_TABLES, C_SPRITES, C_STATUS, C_COMMAND, C_PALETTE, C_REGISTERS, C_COUNT };

/* The Bitmap view's "Read as" choices. */
const int bitmapModes[] = { 5, 6, 7, 8, 10, 12 };
const char* const bitmapModeNames[] = { "SC5", "SC6", "SC7", "SC8", "SC10/11", "SC12" };
#define BITMAP_MODES 6

ImU32 rgbaToCol(uint32_t c)
{
    return (ImU32)c;   /* both are 0xAABBGGRR */
}

/* "SCREEN 5", for labels that name a mode beside other facts. */
const char* shortModeName(int mode)
{
    static char text[16];
    if (mode < 0) return ts(TS_BAND_MODE_UNKNOWN);
    if (mode == 0) return "SCREEN 0 (40)";
    if (mode == 13) return "SCREEN 0 (80)";
    if (mode == 10) return "SCREEN 10/11";
    snprintf(text, sizeof(text), "SCREEN %d", mode);
    return text;
}

int screenLines(const VdpDebugState* s)
{
    return !vdpIsMsx1(s) && (s->regs[9] & 0x80) ? 212 : 192;
}

int cellWidth(int mode)
{
    return mode == 0 || mode == 13 ? 6 : 8;
}

bool interleaved(const VdpDebugState* s, int mode)
{
    return vdpInterleavedIn(s, mode);
}

/* A pixel's shape as on a TV, 256 dots wide being square: the 512- and
** 480-dot modes have dots half as wide as tall. */
void dotAspect(int mode, int* ax, int* ay)
{
    *ax = 1;
    *ay = mode == 6 || mode == 7 || mode == 13 ? 2 : 1;
}

/* Sheet position of character ch (SCREEN 2/4 add 256 per block). */
void sheetCell(int ch, int* x, int* y)
{
    int third = ch >> 8, c = ch & 0xff;
    *x = (c & 31) * 8;
    *y = (third * 8 + c / 32) * 8;
}

const char* cmdName(const VdpDebugState* s, int cm)
{
    static const char* names[16] = { "STOP", "-", "-", "-", "POINT", "PSET", "SRCH", "LINE",
                                     "LMMV", "LMMM", "LMCM", "LMMC", "HMMV", "HMMM", "YMMM", "HMMC" };
    static const char* ext[4] = { "STOP", "LFMM", "LFMC", "LRMM" };
    /* A V9968 with R#21 bit 0 V58 clear takes codes 1-3 as its own commands. */
    bool v9968 = s->version == VDP_V9968 && !(s->regs[21] & 1);
    return v9968 && (cm & 15) < 4 ? ext[cm & 15] : names[cm & 15];
}

const char* logOpName(int lo)
{
    static const char* names[16] = { "IMP", "AND", "OR", "EOR", "NOT", "-", "-", "-",
                                     "TIMP", "TAND", "TOR", "TEOR", "TNOT", "-", "-", "-" };
    return names[lo & 15];
}

/* The pages of a bitmap mode in a grid, a gap between them. */
struct PageGrid {
    int cols = 1, pages = 1, tileW = 256, tileH = 256, gap = 0;

    int  width() const  { return cols * tileW + (cols - 1) * gap; }
    int  height() const { int rows = (pages + cols - 1) / cols; return rows * tileH + (rows - 1) * gap; }
    void origin(int page, int* x, int* y) const
    {
        *x = (page % cols) * (tileW + gap);
        *y = (page / cols) * (tileH + gap);
    }
    bool at(int x, int y, int* page, int* px, int* py) const
    {
        if (x < 0 || y < 0) return false;
        int c = x / (tileW + gap), r = y / (tileH + gap);
        *px = x - c * (tileW + gap);
        *py = y - r * (tileH + gap);
        *page = r * cols + c;
        return c < cols && *px < tileW && *py < tileH && *page < pages;
    }
};

class V99x8View : public ChipView {
public:
    ~V99x8View()
    {
        delete bandFrame;
        delete sprPinKeep;
        delete sprSelKeep;
    }

    V99x8View()
    {
        outCv.shared = sheetCv.shared = vramCv.shared = bitmapCv.shared = sprCv.shared = &shell::zoom();
        sheetCv.frameless = vramCv.frameless = bitmapCv.frameless = outCv.frameless = true;
        regFadeReset(regFade);
        regFadeReset(statFade);
    }

    /* The modes of the bands in order, e.g. "SCREEN 5 -> SCREEN 4 (2 bands)", then lines and video. */
    void facts(const void* st, char* out, size_t size) override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        size_t at = 0;
        auto add = [&](const char* fmt, const char* a, int n) {
            if (at < size) at += (size_t)textf(out + at, size - at, fmt, a, n);
        };
        ensureRaster(s);
        int drawn = 0, last = -2;
        out[0] = 0;
        for (size_t i = 0; i < raster.bands.size(); i++) {
            const VdpBand& b = raster.bands[i];
            /* A split in the sprite registers only is not counted: a game flips its sprite table at any line. */
            if (!bandDrawn(b) || (i > 0 && spriteOnlyAcrossBeam(raster.bands[i - 1], b))) continue;
            drawn++;
            if (b.mode != last) add(at ? " \xE2\x86\x92 %s" : "%s", shortModeName(b.mode), 0);
            last = b.mode;
        }
        if (drawn == 0) add("%s", shortModeName(s->screenMode), 0);
        else if (drawn > 1 && at < size) at += (size_t)textf(out + at, size - at, ts(TS_FACTS_BANDS), drawn);
        char lines[64];
        textf(lines, sizeof(lines), ts(TS_LINES_N), screenLines(s));
        add(" \xC2\xB7 %s", lines, 0);
        add(" \xC2\xB7 %s", s->isPal ? "PAL 50 Hz" : "NTSC 60 Hz", 0);
        if (!s->screenOn) add(" \xC2\xB7 %s", ts(TS_FACTS_BLANKED), 0);
    }

    int views(ViewDesc* out, int max, const void* st) const override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        int n = 0;
        /* A V9938/58 frame can mix modes, so these views stay; they say when no line uses them. */
        bool fixed = !vdpIsMsx1(s);
        if (n < max) out[n++] = ViewDesc{ "screen", ts(TS_VIEW_SCREEN), UI_ICON_SCREEN };
        if (n < max && (fixed || vdpIsPatternMode(s->screenMode))) out[n++] = ViewDesc{ "patterns", ts(TS_VIEW_PATTERNS), UI_ICON_PATTERNS };
        /* An MSX1 VDP has no bitmap mode to read its VRAM as. */
        if (n < max && fixed) out[n++] = ViewDesc{ "bitmap", ts(TS_VIEW_BITMAP), UI_ICON_BITMAP };
        if (n < max && (fixed || vdpSpriteMode(s->screenMode))) out[n++] = ViewDesc{ "sprites", ts(TS_VIEW_SPRITES), NULL, &ui::UI_ART_SHIP };
        if (n < max) out[n++] = ViewDesc{ "vram", "VRAM", UI_ICON_VRAM };
        return n;
    }

    void drawView(int v, const void* st, Focus& f) override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        ViewDesc d[8];
        int n = views(d, 8, st);
        /* A pin on a tile means nothing once the screen is a bitmap, and back; a band's pin follows its line's
        ** band, and a character's the group it was read from, letting go once the frame has no such group. */
        int family = vdpIsBitmapMode(s->screenMode) ? 1 : 0;
        ensureRaster(s);
        bool byGroup = f.kind == FOCUS_CHAR && charPin.set && f.a == charPin.a && f.b == charPin.b && raster.valid;
        if (f.pinned && (byGroup ? vdpFindGroup(groups, charPin.key) < 0 :
                         family != lastFamily && f.kind != FOCUS_VRAM && f.kind != FOCUS_SPRITE && !bandFocus(f))) {
            f.pinned = false;
            f.kind = FOCUS_NONE;
        }
        sp3.track(s, f);
        refreshBandPin(s, f);
        /* Also while the card is folded away. */
        notePin(f);
        if (!(f.pinned && f.kind != FOCUS_NONE) && selBand < 0) selLatch = 0;
        ensureSprites(s);
        if (f.kind == FOCUS_SPRITE && sprites.mode == 0) {
            f.pinned = false;
            f.kind = FOCUS_NONE;
        }
        lastFamily = family;
        ensureLinePalettes(s);
        /* A hover is found again in a rebuilt scene only while the Sprites tab draws it. */
        if (v < 0 || v >= n || strcmp(d[v].key, "sprites") != 0) {
            sprHotNext.n = -1;
            sprPatHotNext.p = -1;
        }
        ensureScene(s);
        sprKeepPin(f);
        stripHold();
        noteBitmapMode(s);
        /* On every tab: the tables and palettes each line used; Output asks for the samples too. */
        shell::wantSpriteTables();
        shell::wantFrame(false);
        if (v < 0 || v >= n) return;
        if (strcmp(d[v].key, "screen") == 0) {
            /* The pages go under the Screen's card as high as the splitter
            ** says, whatever they hold; the card takes the rest. */
            float paneH = fmaxf(0.0f, bottomHeight(s, ImGui::GetContentRegionAvail().y - ui::Px(8) - shell::pagesChromeH(true)));
            ui::BeginTabCard("card", -(ui::Px(8) + shell::pagesChromeH(shell::pages().open) + paneH));
            drawOutput(s, f);
            ui::EndTabCard();
            drawBottom(s, f, paneH);
            return;
        }
        ui::BeginTabCard("card");
        if (strcmp(d[v].key, "patterns") == 0) drawPatterns(s, f);
        else if (strcmp(d[v].key, "bitmap") == 0) drawBitmap(s, f);
        else if (strcmp(d[v].key, "sprites") == 0) drawSprites(s, f);
        else drawVram(s, f);
        ui::EndTabCard();
    }

    int cards(CardDesc* out, int max, const void* st) const override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        const CardDesc all[C_COUNT] = {
            { "sel", NULL, true }, { "splits", NULL, true }, { "disp", ts(TS_CARD_DISPLAY), true },
            { "tables", ts(TS_CARD_TABLES), true }, { "sprites", ts(TS_CARD_SPRITES), true }, { "status", ts(TS_CARD_STATUS), true },
            { "cmd", ts(TS_CARD_COMMAND), true }, { "pal", ts(TS_CARD_PALETTE), true }, { "regs", ts(TS_CARD_REGISTERS), true },
        };
        int n = 0;
        for (int i = 0; i < C_COUNT && n < max; i++) {
            out[n] = all[i];
            if (i == C_COMMAND) out[n].shown = s->cmdRegCount > 0;
            if (i == C_SPRITES) out[n].shown = !vdpIsMsx1(s) || vdpSpriteMode(s->screenMode) != 0;
            /* The pinned thing and the bands stay open; the frame's facts fold. */
            out[n].expander = i != C_SELECTION && i != C_SPLITS;
            out[n].summary = i == C_DISPLAY || i == C_TABLES || i == C_SPRITES ? cardSummary(s, i) : NULL;
            out[n].noHold = i == C_SELECTION;
            n++;
        }
        return n;
    }

    /* One line for a folded card. */
    const char* cardSummary(const VdpDebugState* s, int c) const
    {
        char* out = summaries[c == C_DISPLAY ? 0 : c == C_TABLES ? 1 : 2];
        size_t size = sizeof(summaries[0]);
        out[0] = 0;
        if (c == C_DISPLAY) {
            int adj = s->regs[18];
            if (vdpIsMsx1(s)) textf(out, size, ts(TS_SUM_DISPLAY_MSX1), s->regs[7] & 15);
            else textf(out, size, ts(TS_SUM_DISPLAY), vdpSetAdjust(adj, false), adj & 15, vdpSetAdjust(adj, true), (adj >> 4) & 15,
                       s->regs[7] & 15, ts((s->regs[9] & 0x08) ? TS_ON : TS_OFF));
        }
        else if (c == C_TABLES) {
            VdpRange r[8];
            int n = vdpTableRanges(s, vdpLayoutOf(s), r);
            size_t at = 0;
            for (int i = 0; i < n && at < size; i++) {
                at += (size_t)textf(out + at, size - at, "%s%s %05X", at ? " \xC2\xB7 " : "", r[i].name, r[i].addr);
            }
        }
        else if (Sp3Panel::active(s)) {
            sp3.summary(s, out, size);
        }
        else {
            const_cast<V99x8View*>(this)->ensureScene(s);
            SprView v = shownTable();
            const VdpSpriteFrame& fr = v.fr != NULL ? *v.fr : sprites;
            int shownCount = 0;
            for (int i = 0; i < fr.stopIndex && i < 32; i++) {
                shownCount += v.fr != NULL ? v.agg[i].drawn > 0 : fr.spr[i].drawnLines > 0;
            }
            const char* off = sprOffWhy(fr);
            if (fr.mode == 0) textf(out, size, "%s", ts(TS_SPR_CARD_NONE));
            else if (off != NULL) textf(out, size, ts(TS_SUM_SPRITES_OFF), fr.mode, fr.size == 16 ? "16\xC3\x97" "16" : "8\xC3\x97" "8", off);
            else textf(out, size, ts(TS_SUM_SPRITES), fr.mode, fr.size == 16 ? "16\xC3\x97" "16" : "8\xC3\x97" "8", shownCount);
        }
        return out;
    }

    void drawCard(int c, const void* st, Focus& f) override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        ensureLinePalettes(s);
        ensureScene(s);
        switch (c) {
        case C_SELECTION: cardSelection(s, f); break;
        case C_DISPLAY:   cardDisplay(s); break;
        case C_SPLITS:    splitsBody(s); break;
        case C_TABLES:    cardTables(s); break;
        case C_SPRITES:
            if (Sp3Panel::active(s)) sp3.card(s, f);
            else cardSprites(s);
            break;
        case C_STATUS:    cardStatus(s); break;
        case C_COMMAND:   cardCommand(s); break;
        case C_PALETTE:   cardPalette(s); break;
        case C_REGISTERS: cardRegisters(s); break;
        }
    }

    /* The tables a layout reads, as lanes of physical addresses. */
    static int laneRanges(const VdpDebugState* s, const VdpLayout& l, VramLane* out, int max, int band)
    {
        VdpRange r[8];
        int n = vdpTableRanges(s, l, r), k = 0;
        for (int i = 0; i < n; i++) {
            if (interleaved(s, l.mode)) {
                /* Even bytes in the low 64K, odd ones from vram128, within each 128K on a 256K V9968. */
                UInt32 len = (UInt32)r[i].len / 2, cap = (UInt32)r[i].cap / 2;
                UInt32 a = (UInt32)vdpPhysicalAddress(s, l.mode, r[i].addr & ~1);
                UInt32 b = (UInt32)vdpPhysicalAddress(s, l.mode, r[i].addr | 1);
                if (k < max) out[k++] = VramLane{ a, len, r[i].kind, r[i].name, NULL, band + 1, cap };
                if (k < max) out[k++] = VramLane{ b, len, r[i].kind, r[i].name, NULL, band + 1, cap };
            }
            else if (k < max) {
                out[k++] = VramLane{ (UInt32)r[i].addr, (UInt32)r[i].len, r[i].kind, r[i].name, NULL, band + 1, (UInt32)r[i].cap };
            }
        }
        return k;
    }

    /* "SC4", for lane labels. */
    static void scName(int mode, char* out, size_t size)
    {
        if (mode == 0) snprintf(out, size, "SC0/40");
        else if (mode == 13) snprintf(out, size, "SC0/80");
        else if (mode == 10) snprintf(out, size, "SC10/11");
        else snprintf(out, size, "SC%d", mode);
    }

    /* Adds band b's modes and lines to what lane i says it serves. */
    void laneServe(int i, const VdpBand& b) const
    {
        LaneServe& w = laneServes[i];
        w.modes |= 1u << (b.mode & 15);
        if (w.runs > 0 && w.runLast[w.runs - 1] >= b.first - 1) {
            if (b.last > w.runLast[w.runs - 1]) w.runLast[w.runs - 1] = b.last;
        }
        else if (w.runs < 8) {
            w.runFirst[w.runs] = b.first;
            w.runLast[w.runs] = b.last;
            w.runs++;
        }
    }

    void laneWhereText(int i) const
    {
        const LaneServe& w = laneServes[i];
        char modes[48] = "", lines[64] = "";
        size_t a = 0, b = 0;
        for (int m = 0; m < 16 && a < sizeof(modes); m++) {
            if (!((w.modes >> m) & 1)) continue;
            char sc[16];
            scName(m, sc, sizeof(sc));
            a += (size_t)snprintf(modes + a, sizeof(modes) - a, "%s%s", a ? ", " : "", sc);
        }
        for (int r = 0; r < w.runs && b < sizeof(lines); r++) {
            const char* sep = r ? ", " : "";
            if (w.runFirst[r] == w.runLast[r]) b += (size_t)snprintf(lines + b, sizeof(lines) - b, "%s%d", sep, w.runFirst[r]);
            else b += (size_t)snprintf(lines + b, sizeof(lines) - b, "%s%d-%d", sep, w.runFirst[r], w.runLast[r]);
        }
        textf(laneWheres[i], sizeof(laneWheres[i]), ts(TS_LANE_WHERE), modes, lines);
    }

    int vramLanes(const void* st, VramLane* out, int max) const override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        UInt32 size = vramSize(st);
        int k = 0;
        /* The map is drawn before the views: bring the bands up to date with st. */
        const_cast<V99x8View*>(this)->ensureRaster(s);
        if (max > MAX_LANES) max = MAX_LANES;
        if (!raster.valid || raster.bands.size() <= 1) {
            /* One band drew the frame; the frame's end may hold the next one's tables. */
            bool one = raster.valid && raster.bands.size() == 1 && bandDrawn(raster.bands[0]);
            k = laneRanges(s, one ? vdpLayoutOf(raster.bands[0]) : vdpLayoutOf(s), out, max, -1);
        }
        else {
            /* Each band's tables; a table two bands read is one lane naming both, spanning the longer read
            ** where one band reads more of it (same name and cap: a 32-byte SCREEN 1 colour table stays apart). */
            for (int bi = 0; bi < (int)raster.bands.size(); bi++) {
                const VdpBand& b = raster.bands[bi];
                if (!bandDrawn(b)) continue;
                VramLane mine[16];
                int n = laneRanges(s, vdpLayoutOf(b), mine, 16, bi);
                for (int i = 0; i < n; i++) {
                    int j = 0;
                    while (j < k && !(out[j].addr == mine[i].addr && out[j].series == mine[i].series &&
                                      out[j].name == mine[i].name && out[j].cap == mine[i].cap)) j++;
                    if (j < k && out[j].len < mine[i].len) out[j].len = mine[i].len;
                    if (j == k) {
                        if (k == max) continue;
                        out[k] = mine[i];
                        laneServes[k] = LaneServe();
                        k++;
                    }
                    laneServe(j, b);
                }
            }
            for (int i = 0; i < k; i++) {
                laneWhereText(i);
                out[i].where = laneWheres[i];
            }
        }
        for (int i = 0; i < k; i++) {
            if (out[i].addr >= size) out[i].len = 0;
            else if (out[i].addr + out[i].len > size) out[i].len = size - out[i].addr;
        }
        return k;
    }

    UInt32 vramSize(const void* st) const override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        int size = s->vramSize < 0x4000 ? 0x4000 : s->vramSize;
        return (UInt32)(size > VDP_DEBUG_VRAM_SIZE ? VDP_DEBUG_VRAM_SIZE : size);
    }

    UInt32 toPhysical(const void* st, int space, UInt32 a) const override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        return space == FOCUS_LOGICAL ? (UInt32)vdpPhysicalAddress(s, s->screenMode, (int)a) : a;
    }

    void escape(bool key) override
    {
        selectBand(-1);
        sp3.unpin();
        /* A click beside the picture fixes the line row's line; only the key lets go of it. */
        if (key) {
            sprLineFixed = sprLineHeld = -1;
            sp3.escape();
        }
    }

    bool bandsKnown(const void* st) const override
    {
        const_cast<V99x8View*>(this)->ensureRaster((const VdpDebugState*)st);
        return raster.valid;
    }

    void forget() override
    {
        outPic.forget();
        sheetPic.forget();
        namePic.forget();
        patPic.forget();
        patCol = patRow = -1;
        selSheetPic.forget();
        vramPic.forget();
        bitmapPic.forget();
        stripForget();
        regFadeReset(regFade);
        regFadeReset(statFade);
        readAs = -1;
        lastBitmapMode = 5;
        bitmapSeen = false;
        readGroupSet = false;
        patGroupSet = false;
        charPin.set = false;
        lastFamily = -1;
        spriteVer = ~0u;
        rasterVer = ~0u;
        rasterBuilt = false;
        linePalShot = linePalRaster = ~0u;
        memset(sceneKey, 0xff, sizeof(sceneKey));
        sprPinLine = -1;
        sprSelReset();
        lineRowReset();
        sp3.forget();
        sprRowSeq = 0;
        sprPinKeepAttr = -1;
        sprPinKeepVer = ~0u;
        sprPlacePic.forget();
        sprAtlasPic.forget();
        sprLinePic.forget();
        sprPlaceHash = sprAtlasHash = sprLineHash = 0;
        sprFitN = 0;
        sprPrevPat.clear();
        sprPatPic.forget();
        sprPatHash = 0;
        sprPatRingLen = sprPatRingPos = 0;
        sprPatRingSeq = ~0u;
        sprPatDrawnLen = sprPatDrawnPos = 0;
        sprPatHot.p = sprPatHotNext.p = -1;
        sprRingLen = sprRingPos = 0;
        sprRingSeq = ~0u;
        memset(sprOverSum, 0, sizeof(sprOverSum));
        sprNoTables = 0;
        selBand = -1;
        selWanted = false;
        selLatch = 0;
        palHeldAt = -1;
    }

    void loadUi(const VdpViewerProperties& p) override
    {
        outBorders = p.v99x8.outBorders != 0;
        sprShown = p.v99x8.sprShown != 0;
        sprOnly = p.v99x8.sprOnly != 0;
        boxes = p.v99x8.boxes != 0;
        outCv.grid = p.v99x8.outGrid != 0;
        patNumbers = p.v99x8.patNumbers != 0;
        sheetCv.grid = p.v99x8.patGrid != 0;
        transparent0 = p.v99x8.transparent0 != 0;
        layout = p.v99x8.layout == 1 ? 1 : 0;
        bitmapCv.grid = p.v99x8.bitmapGrid != 0;
        sprDropped = p.v99x8.sprDropped != 0;
        sprCv.grid = p.v99x8.sprGrid != 0;
        vramTint = p.v99x8.vramTint != 0;
        vramCv.grid = p.v99x8.vramGrid != 0;
    }

    void saveUi(VdpViewerProperties& p) const override
    {
        p.v99x8.outBorders = outBorders;
        p.v99x8.sprShown = sprShown;
        p.v99x8.sprOnly = sprOnly;
        p.v99x8.boxes = boxes;
        p.v99x8.outGrid = outCv.grid;
        p.v99x8.patNumbers = patNumbers;
        p.v99x8.patGrid = sheetCv.grid;
        p.v99x8.transparent0 = transparent0;
        p.v99x8.layout = layout;
        p.v99x8.bitmapGrid = bitmapCv.grid;
        p.v99x8.sprDropped = sprDropped;
        p.v99x8.sprGrid = sprCv.grid;
        p.v99x8.vramTint = vramTint;
        p.v99x8.vramGrid = vramCv.grid;
    }

private:
    ViewPicture     sheetPic, vramPic, bitmapPic;
    ViewPicture     selSheetPic;          /* the Selection's, for a band's or a cell's pin: never uploaded */
    ui::CanvasState sheetCv, vramCv, bitmapCv;
    /* The Patterns tab: the screen the name table builds beside the pattern generator, one
    ** picture; parts in image px, sdx image px a sheet dot across (2 beside T80's names). */
    struct PatParts { int nx, ny, nw, nh, sx, sy, sw, sh, sdx, w, h; };
    bool            patSide = true;
    bool            patNumbers = false;   /* character numbers on the cells */
    bool            patGroupSet = false;  /* Patterns reads the group patKey finds */
    int             patScrollPart = 0, patScrollFrames = 0;  /* a part scrolled to for a few frames: 0 names, 1 sheet */
    VdpBandKey      patKey = {};
    int             patPickOrder = -1;    /* the held group picked, by its first-seen order, for the list */
    VdpBand         patPickBand = {};     /* the pick's grouping key (mode and table registers) */
    PatParts        pat = {};
    ViewPicture     namePic, patPic;
    int             patCol = -1, patRow = -1;  /* the name cell last hovered or pinned */
    Focus           patCellFocus = {};         /* the focus that cell gave */
    /* The character pinned on the Patterns tab, a and b as its focus, and the group it was read from. */
    struct CharPin { bool set; int a, b; VdpBandKey key; };
    CharPin         charPin = {};
    bool            transparent0 = false;
    int             readAs = -1;          /* index into bitmapModes; -1 follows the mode */
    bool            readGroupSet = false; /* Read as follows the group readKey finds */
    VdpBandKey      readKey = {};
    int             lastBitmapMode = 5;   /* the last bitmap mode a frame showed, for Auto in pattern modes */
    bool            bitmapSeen = false;   /* ...and whether one did */
    int             ovReadGroup = -1;     /* the group the Bitmap view reads, or -1: the frame's end */
    int             lastFamily = -1;      /* 1 bitmap, 0 pattern mode */
    enum { SL_SHOWN, SL_HIDDEN, SL_ONLY };
    bool            sprShown = true;      /* Output's sprites shown, or hidden */
    bool            sprOnly = false;      /* Output's sprites alone */
    ViewPicture     spriteLayer;          /* the display lines' sprites, band by band */
    int             layout = 0;           /* 0 tiles, 1 a column */
    bool            vramTint = true;      /* the bytes tinted by the VRAM map's lanes */
    bool            boxes = false;        /* sprite boxes on the Screen and the Sprites tab */
    RegFade         regFade, statFade;
    PageGrid        grid;
    VdpImage        column;               /* every page one under the other, for the tiles */
    VdpSpriteFrame  sprites;              /* from the group Auto reads, or the frame's end without a log */
    unsigned        spriteVer = ~0u;
    int             spriteLines = 0;
    const char*     spriteText = NULL;
    int             spriteGroup = -1;
    UInt8           spriteRegs[32];
    int             spriteMode = -1;
    std::vector<VdpBandGroup> groups;     /* of the bands, rebuilt with them */
    VdpRasterView   raster;
    unsigned        rasterVer = ~0u;
    bool            rasterBuilt = false;
    int             selBand = -1;         /* the band picked on the Splits card */
    ui::HeightHold  pillHold, bandHold;   /* the Splits card's pill row and band rows keep their room a while */
    VdpBandKey      selKey;               /* found again by its family and lines after a rebuild */
    struct PinSeen { int kind, a, b, grid; };
    PinSeen         pinSeen = { -1, 0, 0, 0 };  /* a band's pin, and the grid it was last read on */
    bool            selWanted = false;    /* selKey stays picked while no band matches it */
    mutable char    summaries[3][256];    /* the folded Display, Tables and Sprites cards' lines */
    mutable Sp3Panel sp3;                 /* the Sprites tab and cards while a V9968 is in SP3 */
    float           selLatch = 0;         /* the inspected card's height so far for... */
    float           selLatchKey[6] = {};  /* ...this pin (kind, a, b, address, space) or band, at this width */
    char            palHeldRuns[160] = "";  /* the Palette card's last note while running... */
    bool            palHeldAll = false;
    double          palHeldAt = -1;         /* ...and when it was seen, or -1 */
    float           cardPin[5] = {};      /* the pin last seen, checked every frame */
    bool            cardBand = false;     /* the card shows the band picked after the pin */
    UInt8           rasterRegs[32];       /* the registers the bands were checked against */
    mutable float   ovMarkX = 0;          /* left edge of the gutter's raster marks */
    /* The bands each map lane serves, and its label, kept for the shell to read. */
    static const int MAX_LANES = 64;
    struct LaneServe { unsigned modes = 0; int runFirst[8], runLast[8], runs = 0; };
    mutable LaneServe laneServes[MAX_LANES];
    mutable char      laneWheres[MAX_LANES][96];
    static const int STRIP_GROUPS = 3;
    static const int STRIP_BRIEF = 2;     /* a slot in no more pictures gives way first */
    float           stripTw = 0;          /* the pages' thumbnail width */
    shell::PagesScroll pagesScroll;
    bool            stripMixed = false;
    int             stripGroupCount = 0;
    int             stripOrder[STRIP_GROUPS] = {};  /* the slots shown, in the order they were first seen */
    UInt32          stripHoldSeq = 0;               /* the picture the slots were last held for */
    unsigned        stripHoldGen = ~0u, rasterGen = 0;
    int             stripOrderNext = 0;
    int             stripVramPage = -1;   /* the Bitmap view scrolls to it for a few frames */
    int             stripVramFrames = 0;

    /* For the overlays, which run inside Canvas. */
    const VdpDebugState* ovState = NULL;
    int                  ovReadMode = -1;
    VdpLayout            ovLayout = {};          /* the Patterns view's; rgb valid until the next ensureLinePalettes */
    int                  ovRows = 0, ovWidth = 256;  /* the Output picture's */
    int                  ovSplitWidth = 256;  /* dots a split is counted in */

    /* Output: the picture the VDP drew. */
    ViewPicture          outPic;
    ui::CanvasState      outCv;
    bool                 outBorders = true;
    bool                 outPicBorders = true;  /* what outPic was decoded with */
    const VdpFrameShot*  outShot = NULL;    /* for the overlay; NULL when none came */
    int                  outLine0 = 0;      /* image row of display line 0 */
    bool                 outDim = false;
    int                  outLayer = SL_SHOWN;      /* what outPic was decoded with */
    std::vector<uint8_t> outRowApprox;     /* per image row: the sprite split is not exact */
    /* sprPinAttr is the attribute address of a sprite pinned where its table is known,
    ** which keeps it in that table; sprPinLine the display line it was pinned on. */
    bool                 sprPinned = false;
    int                  sprPinAttr = -1;
    int                  sprPinLine = -1;
    int                  outHoverAttr = -1;  /* the sprite under the mouse last frame */
    bool                 outBoxes = false;   /* boxes on, in a frame with sprites */
    bool                 outCounts = false;  /* the per-line counts beside it: sprites shown, in such a frame */

    /* The palettes the display lines were drawn with, from the shot, when it is of the
    ** frame the bands are; ver changes only when they do. */
    struct LinePalettes {
        bool                 valid = false;
        std::vector<UInt32>  rgb;       /* 16 colours (0x00RRGGBB) per distinct palette */
        std::vector<int16_t> lineIdx;   /* per display line: a palette of rgb, or -1 */
        std::vector<uint8_t> lineState; /* per display line: VDP_SPR_ROW_* */
        int                  drawnLines = 0;  /* lines of the frame the bands are of; paused, those above the beam */
        unsigned             ver = 1;
    };
    LinePalettes         linePal;
    unsigned             linePalShot = ~0u, linePalRaster = ~0u;
    int                  linePalLines = -1;

    /* The sprites of each display line, one source for Output's boxes, the cards and
    ** the Sprites tab: from the table the line used when recorded, else (FALLBACK) from
    ** its band's registers and VRAM at the frame's end. Frames count with the VDP's limit. */
    enum { SS_NONE = VDP_SPR_ROW_NONE, SS_EXACT = VDP_SPR_ROW_EXACT, SS_APPROX = VDP_SPR_ROW_APPROX, SS_FALLBACK, SS_LOST };
    /* The lines one table lays out, known by its attributes, patterns, sprite mode and size. */
    struct SprSlot {
        int  attr, patBase, mode, sizeKey;
        int  rep;                       /* the frame laying out most of its lines, the earliest on a tie */
        int  lines, first;
        int  runFirst[4], runLast[4], runs;
        bool versions;                  /* more than one frame: its bytes changed within the frame */
    };
    /* A table as a slot knows it: by these four, not by its attribute address alone. */
    struct SprKey { int attr, patBase, mode, sizeKey; };
    /* A sprite on the lines of its slot, with the VDP's limit. */
    struct SprAgg { int drawn, dropped, hidden, firstDropped, lastDropped; bool collides; };
    struct SprScene {
        bool recorded = false;
        int  lines = 0, drawLimit = 0, lost = 0, approx = 0;   /* drawLimit: the emulator's, 0 the VDP's */
        std::vector<VdpSpriteFrame> frames;
        std::vector<int16_t> lineFrame;  /* per display line: into frames, or -1 */
        std::vector<uint8_t> lineState;  /* per display line: SS_* */
        std::vector<uint8_t> lineOff;    /* per display line: the VDP shows no sprite there (SPD, blanked) */
        std::vector<SprSlot> slots;      /* by first line */
        std::vector<int8_t>  lineSlot;
        std::vector<SprAgg>  agg;        /* slots x 32 */
        int  collX = -1, collLine = -1, firstDropLine = -1, firstDropped = -1, overLines = 0;
        unsigned ver = 0;
    };
    SprScene             scene;
    unsigned             sceneKey[7] = { ~0u };
    /* The table the Sprites tab shows, picked or the first by line, held while a double buffer swaps
    ** tables: its frame, sprites, row states (sprRowState) and lines as last laid out, and the
    ** picture (emulated frame count) it was last used in. */
    enum { SPR_AWAY_SHOWN = 10, SPR_SEEN_KEEP = 120, SPR_KEPT = -2 };
    bool                 sprSelSet = false, sprSelKept = false;
    SprKey               sprSel = {};
    VdpSpriteFrame*      sprSelKeep = NULL;
    SprSlot              sprSelSlot = {};    /* its runs, for labels; rep is not used */
    SprAgg               sprSelAgg[32] = {};
    int8_t               sprSelRow[32][16] = {};
    std::vector<uint8_t> sprSelLines;
    UInt32               sprSelSeenSeq = 0;
    /* The scene's picture count. */
    UInt32               sprSceneSeq = 0;
    /* The tables displayed lines used up to the scene's picture, as recorded with it: those that
    ** show sprites by key, with the picture each was last used in. */
    struct SprUse { SprKey key; UInt32 seq; };
    SprUse               sprUse[VDP_SPR_USES] = {};
    int                  sprUseN = 0;
    UInt32               sprUseSince = 0;
    bool                 sprUseValid = false;
    /* A table as last laid out: as the shown table's copy above, so any listed table can be shown. */
    struct SprKeep { VdpSpriteFrame fr; SprAgg agg[32]; int8_t row[32][16]; std::vector<uint8_t> lines; };
    /* The tables of the last pictures for the selector, by key: the picture each was last laid out in. */
    struct SprSeen { SprKey key; UInt32 seq; SprSlot slot; std::unique_ptr<SprKeep> keep; };
    std::vector<SprSeen> sprSeen;
    /* The selector's buttons: tables of sprSeen, each kept in its place until it drops out. */
    SprKey               sprRow[4] = {};
    int                  sprRowN = 0;
    /* The pinned table as last laid out, shown while it is away (a double buffer). */
    VdpSpriteFrame*      sprPinKeep = NULL;
    int                  sprPinKeepAttr = -1;
    UInt32               sprPinSeenSeq = 0;  /* the picture it was last used in */
    SprAgg               sprPinKeepAgg = {};
    unsigned             sprPinKeepVer = ~0u;

    /* The Sprites tab. */
    bool                 sprDropped = true;  /* dropped sprites in red stipple */
    /* The tab's parts: where each is, at what scale (screen px per sprite dot), in which tier. */
    enum { SPT_PLACE, SPT_ATTRS, SPT_MAG, SPT_PATS, SPT_COUNT };
    struct SprLayout {
        float  scale[SPT_COUNT];
        ImVec2 at[SPT_COUNT], size[SPT_COUNT];
        float  width, height;
        float  fitH;              /* down to the patterns' first row */
        int    tier, patCols;
    };
    ui::CanvasState      sprCv;              /* the shared zoom; the tab lays out its own parts */
    SprLayout            sprLastL = {};      /* last frame's layout and content origin, for Ctrl+wheel */
    ImVec2               sprLastO = ImVec2(0, 0);
    float                sprFitKey[7] = {};  /* what Fit's zoom sprFitN was found for */
    int                  sprFitN = 0;
    ViewPicture          sprPlacePic;
    unsigned             sprPlaceHash = 0;
    std::vector<uint8_t> sprDropMask;
    /* ver: the scene's when hovered; frame SPR_KEPT: the shown table's kept one; sel: of the shown table. */
    struct SprHot { int slot, n, d, frame; unsigned ver; bool sel; };
    SprHot               sprHot = { -1, -1, -1, -1 }, sprHotNext = { -1, -1, -1, -1 };  /* drawn one frame late */
    int                  sprSlotSel = -1;    /* the shown table's slot in this picture, or -1 while away */
    /* The shown table: this picture's slot, else its kept frame; fr NULL until it is first laid out. */
    struct SprView { int slot; const VdpSpriteFrame* fr; const SprAgg* agg; };
    SprView              sprView = {};       /* this frame's */
    /* The line row's line: this frame's placement hover, the line the mouse last rested on,
    ** and the automatic pick, held for a second. */
    enum { ROW_NONE, ROW_HOVER, ROW_FIXED, ROW_HELD, ROW_PIN_DROP, ROW_PIN, ROW_OVER, ROW_BUSIEST };
    int                  sprLineFixed = -1;  /* a click on the placement fixes a line until Esc or a click on it */
    int                  sprPlaceLine = -1, sprLineHeld = -1, sprLineAuto = -1, sprDwellLine = -1;
    double               sprLineAutoAt = 0, sprDwellAt = 0, sprLineAutoGone = -1;
    bool                 sprLineAutoOver = false;
    int                  sprRowPin = -1;     /* the pinned sprite's attribute address last frame */
    UInt32               sprRowSeq = 0;      /* the picture count last frame; it goes back on a new machine or a loaded state */
    int                  sprNoTables = 0;    /* pictures in a row without tables */
    unsigned             sprNoTablesVer = ~0u;
    /* Over the limit per display line in the last pictures, a bit per line. */
    static const int     SPR_RING = 60;
    uint8_t              sprOverRing[SPR_RING][30] = {};
    uint8_t              sprOverSum[240] = {};
    int                  sprRingLen = 0, sprRingPos = 0, sprRingLines = 0;
    UInt32               sprRingSeq = 0, sprRingSeqFirst = 0, sprRingChip = 0;
    ViewPicture          sprAtlasPic, sprLinePic;  /* the 32 attributes, the hovered line's sprites */
    unsigned             sprAtlasHash = 0, sprLineHash = 0;
    /* Pattern numbers of the last picture of each table, by attribute address. */
    struct SprPrevPat { int attr; int pat[32]; };
    std::vector<SprPrevPat> sprPrevPat;
    /* Patterns the selected table used in the last pictures, a bit per pattern. */
    ViewPicture          sprPatPic;
    unsigned             sprPatHash = 0;
    uint32_t             sprPatRing[SPR_RING][8] = {};
    int                  sprPatRingLen = 0, sprPatRingPos = 0, sprPatRingBase = -1;
    UInt32               sprPatRingSeq = 0;
    /* Per pattern, a bit per colour code of the sprites drawn with it, in the last SPR_AWAY_SHOWN pictures. */
    uint16_t             sprPatDrawn[SPR_AWAY_SHOWN][256] = {};
    int                  sprPatDrawnLen = 0, sprPatDrawnPos = 0;
    /* A pattern by its table's pattern base, number, size and sprite mode: it holds through table swaps.
    ** Hovered (drawn one frame late); a pinned one is the Focus's, its table's part kept here. */
    struct SprPatKey { int base, p, size, mode; };
    SprPatKey            sprPatHot = { -1, -1, 0, 0 }, sprPatHotNext = { -1, -1, 0, 0 };
    SprPatKey            sprPatPin = { -1, -1, 0, 0 };
    uint32_t             sprPatChanged = 0;  /* sprites of the selected table whose pattern changed */
    unsigned             sprPatVer = ~0u;
    int                  sprPatSlotAttr = -1;

    int color0() const
    {
        return transparent0 ? VDP_COLOR0_TRANSPARENT : VDP_COLOR0_AUTO;
    }

    /* The pattern sheet of a layout into pic; true when decoded again. */
    bool decodeSheet(const VdpDebugState* s, const VdpLayout& l, ViewPicture& pic)
    {
        char key[64];
        const UInt8* r = l.regs;
        snprintf(key, sizeof(key), "%d/%d/%02X%02X%02X%02X%02X%02X/%d/%d/%08X", l.mode, color0(), r[3], r[4], r[7], r[8], r[10], r[12], l.blink,
                 vdpSheetBlocks(s, l), palHash(l.rgb));
        bool decoded = pic.needsDecode(key);
        if (decoded) {
            vdpDecodePatternSheet(s, l, color0(), pic.img, &pic.w, &pic.h);
        }
        return decoded;
    }

    void ensureSheet(const VdpDebugState* s, const VdpLayout& l)
    {
        decodeSheet(s, l, sheetPic);
    }

    /* Display lines of a layout: 212 with R#9 LN. */
    static int layoutLines(const VdpDebugState* s, const UInt8* r)
    {
        return !vdpIsMsx1(s) && (r[9] & 0x80) ? 212 : 192;
    }

    static bool viewFits(bool spr, int mode)
    {
        return spr ? vdpSpriteMode(mode) != 0 : vdpIsPatternMode(mode);
    }

    /* The group Auto reads for the Patterns (spr false) or Sprites view, -1 when no line is in
    ** such a mode; *count such groups. Of two or more, Patterns takes a pinned character's,
    ** else the band in context's; then the group with the most lines. */
    int autoGroup(bool spr, int* count) const
    {
        int best = -1, n = 0;
        for (int i = 0; i < (int)groups.size(); i++) {
            if (!viewFits(spr, groups[i].mode)) continue;
            n++;
            if (best < 0 || groups[i].lineCount > groups[best].lineCount) best = i;
        }
        *count = n;
        if (n < 2 || spr) return best;
        const Focus& f = shell::shownFocus();
        int k = f.pinned && f.kind == FOCUS_CHAR && charPin.set && f.a == charPin.a && f.b == charPin.b ? vdpFindGroup(groups, charPin.key) : -1;
        if (k < 0) k = groupOfBand(ctxBand());
        return k >= 0 && viewFits(spr, groups[k].mode) ? k : best;
    }

    /* The registers the Patterns or Sprites view reads: the group Auto or the Patterns tab picks,
    ** the frame's end without a log; *group the group, -1 when the frame's end has its only one
    ** (*read is the group read even then). False when no line of the frame is in such a mode. */
    bool viewLayout(const VdpDebugState* s, bool spr, VdpLayout* l, int* group, int* read = NULL)
    {
        ensureRaster(s);
        *group = -1;
        if (read) *read = -1;
        /* A group picked on the Patterns tab, while the frame still has it. */
        int k = !spr && patGroupSet && raster.valid ? vdpFindGroup(groups, patKey) : -1;
        if (k >= 0 && vdpIsPatternMode(groups[k].mode)) {
            /* Its own slot's tables: a twin on the same lines (another R#2) may be the one in this picture. */
            int ps = pickedSlot();
            const VdpBandGroup& rg = ps >= 0 ? stripGroups[ps] : groups[k];
            *group = k;
            if (read) *read = k;
            *l = withPal(vdpLayoutOf(rg), palOfGroup(rg));
            return true;
        }
        if (groups.empty()) {
            if (!viewFits(spr, s->screenMode)) return false;
            *l = frameEndLayout(s);
            return true;
        }
        int n, g = autoGroup(spr, &n);
        if (g < 0) return false;
        /* The frame's end may be the next frame's tables, but names a lone group as before. */
        *group = n == 1 && viewFits(spr, s->screenMode) ? -1 : g;
        if (read) *read = g;
        *l = withPal(vdpLayoutOf(groups[g]), palOfGroup(groups[g]));
        return true;
    }

    /* "Lines 34-191, SCREEN 4" for a view that reads a group, not the frame's end. */
    const char* groupLabel(int k, char* out, size_t size) const
    {
        out[0] = 0;
        if (k >= 0 && k < (int)groups.size()) textf(out, size, ts(TS_STRIP_GROUP), groups[k].lines, shortModeName(groups[k].mode));
        return out;
    }

    /* "0-111, 180-211" for the lines a slot lays out. */
    static const char* slotLines(const SprSlot& sl, char* out, size_t size)
    {
        size_t at = 0;
        out[0] = 0;
        for (int r = 0; r < sl.runs && at < size; r++) {
            const char* sep = r ? ", " : "";
            if (sl.runFirst[r] == sl.runLast[r]) at += (size_t)snprintf(out + at, size - at, "%s%d", sep, sl.runFirst[r]);
            else at += (size_t)snprintf(out + at, size - at, "%s%d-%d", sep, sl.runFirst[r], sl.runLast[r]);
        }
        return out;
    }

    /* "Lines 0-111, SCREEN 5" for a slot. */
    static const char* slotLabel(const SprSlot& sl, int mode, char* out, size_t size)
    {
        char lines[64];
        textf(out, size, ts(TS_STRIP_GROUP), slotLines(sl, lines, sizeof(lines)), shortModeName(mode));
        return out;
    }

    /* A focus on a logical address read in mode; physical when the frame's end maps it otherwise. */
    void focusAddr(const VdpDebugState* s, int mode, int logical, UInt32 len, Focus* f) const
    {
        if (mode == s->screenMode || interleaved(s, mode) == interleaved(s, s->screenMode)) {
            f->space = FOCUS_LOGICAL;
            f->addr = (UInt32)logical;
            f->len = len;
            return;
        }
        f->space = FOCUS_PHYS;
        f->addr = (UInt32)vdpPhysicalAddress(s, mode, logical);
        f->len = interleaved(s, mode) && len > 1 ? len / 2 : len;
    }

    void ensureSprites(const VdpDebugState* s)
    {
        VdpLayout l;
        int g;
        if (!viewLayout(s, true, &l, &g)) l = vdpLayoutOf(s);
        int lines = layoutLines(s, l.regs);
        /* The frame keeps why sprites are off as text: evaluated again in another language. */
        const char* text = ts(TS_SPR_OFF_BLANKED);
        if (spriteVer == shell::pictureVersion() && spriteLines == lines && spriteText == text && spriteGroup == g &&
            spriteMode == l.mode && memcmp(spriteRegs, l.regs, sizeof(spriteRegs)) == 0) return;
        spriteVer = shell::pictureVersion();
        spriteLines = lines;
        spriteText = text;
        spriteGroup = g;
        spriteMode = l.mode;
        memcpy(spriteRegs, l.regs, sizeof(spriteRegs));
        vdpEvalSprites(s, l, lines, &sprites);
    }

    void ensureRaster(const VdpDebugState* s)
    {
        /* VRAM alone does not move the bands; registers and the logs do. */
        int lines = screenLines(s);
        if (rasterVer == shell::rasterVersion() && memcmp(rasterRegs, s->regs, 32) == 0 && raster.lines == lines && rasterBuilt) {
            return;
        }
        rasterVer = shell::rasterVersion();
        rasterBuilt = true;
        rasterGen++;
        memcpy(rasterRegs, s->regs, 32);
        vdpRasterBuild(s, lines, &raster);
        vdpRasterGroups(raster, groups);
        selBand = selWanted ? vdpRasterFindBand(raster, selKey) : -1;
    }

    /* The palette each display line was drawn with, from the shot of the frame the bands are of. */
    void ensureLinePalettes(const VdpDebugState* s)
    {
        unsigned ver;
        const VdpFrameShot* o = (const VdpFrameShot*)shell::frameShot(&ver);
        ensureRaster(s);
        if (ver == linePalShot && shell::rasterVersion() == linePalRaster && raster.lines == linePalLines) return;
        linePalShot = ver;
        linePalRaster = shell::rasterVersion();
        linePalLines = raster.lines;
        LinePalettes n;
        /* An extended palette keeps entries 0-15 outside the 3-bit registers recorded here. */
        bool ok = o != NULL && o->valid && o->linePalValid && !o->stale && !vdpIsMsx1(s) && s->paletteExtCount == 0 && raster.valid &&
                  o->frameStart == s->raster[0].frameStart;
        if (ok) {
            int y0 = outFirstLine(s, o) - o->displayOffset;
            n.lineIdx.assign(raster.lines, -1);
            n.lineState.assign(raster.lines, VDP_SPR_ROW_NONE);
            n.drawnLines = o->ended ? raster.lines : o->drawnRow - y0;
            for (int d = 0, k = -1; d < raster.lines; d++) {
                int y = d + y0;
                if (y < 0 || y >= o->rows || o->linePalState[y] == VDP_SPR_ROW_NONE) continue;
                UInt32 rgb[16];
                vdpPaletteRegRgb(o->linePal[y], rgb);
                int count = (int)n.rgb.size() / 16;
                if (k < 0 || memcmp(&n.rgb[(size_t)k * 16], rgb, sizeof(rgb)) != 0) {
                    for (k = 0; k < count && memcmp(&n.rgb[(size_t)k * 16], rgb, sizeof(rgb)) != 0; k++) {
                    }
                    if (k == count) n.rgb.insert(n.rgb.end(), rgb, rgb + 16);
                }
                n.lineIdx[d] = (int16_t)k;
                n.lineState[d] = o->linePalState[y];
            }
            n.valid = !n.rgb.empty();
        }
        if (n.valid == linePal.valid && n.rgb == linePal.rgb && n.lineIdx == linePal.lineIdx && n.lineState == linePal.lineState &&
            n.drawnLines == linePal.drawnLines) {
            return;
        }
        n.ver = linePal.ver + 1;
        linePal = n;
    }

    /* The colours display line d was drawn with; NULL: the snapshot's. */
    const UInt32* lineRgb(int d) const
    {
        if (!linePal.valid || d < 0 || d >= (int)linePal.lineIdx.size() || linePal.lineIdx[d] < 0) return NULL;
        return &linePal.rgb[(size_t)linePal.lineIdx[d] * 16];
    }

    /* The palette most lines of the runs were drawn with, counting exact lines (the others
    ** only when none is), the earliest on a tie; mode >= 0 counts lines of bands in it only.
    ** NULL: none known, so the snapshot's. Valid until the next ensureLinePalettes. */
    const UInt32* palOfRuns(const int* first, const int* last, int runs, int mode = -1) const
    {
        if (!linePal.valid) return NULL;
        int count = (int)linePal.rgb.size() / 16, best = -1;
        std::vector<int> votes((size_t)count * 2, 0), at((size_t)count * 2, 0x7fffffff);
        for (int r = 0; r < runs; r++) {
            for (int d = first[r] > 0 ? first[r] : 0; d <= last[r] && d < linePal.drawnLines && d < (int)linePal.lineIdx.size(); d++) {
                int k = linePal.lineIdx[d], b = d < (int)raster.bandOfLine.size() ? raster.bandOfLine[d] : -1;
                if (k < 0 || (mode >= 0 && (b < 0 || raster.bands[b].mode != mode))) continue;
                int j = k * 2 + (linePal.lineState[d] == VDP_SPR_ROW_EXACT ? 0 : 1);
                votes[j]++;
                if (d < at[j]) at[j] = d;
            }
        }
        for (int pass = 0; pass < 2 && best < 0; pass++) {
            for (int k = 0; k < count; k++) {
                int j = k * 2 + pass, bj = best * 2 + pass;
                if (votes[j] > 0 && (best < 0 || votes[j] > votes[bj] || (votes[j] == votes[bj] && at[j] < at[bj]))) best = k;
            }
        }
        return best >= 0 ? &linePal.rgb[(size_t)best * 16] : NULL;
    }

    const UInt32* palOfGroup(const VdpBandGroup& g) const
    {
        return palOfRuns(g.runFirst, g.runLast, g.runs);
    }

    const UInt32* palOfBand(int bi) const
    {
        return bi >= 0 && bi < (int)raster.bands.size() ? palOfRuns(&raster.bands[bi].first, &raster.bands[bi].last, 1) : NULL;
    }

    /* The frame's end registers describe the lines in its mode: their palette, else all lines'. */
    const UInt32* palFrameEnd(const VdpDebugState* s) const
    {
        int first = 0, last = raster.lines - 1;
        const UInt32* p = palOfRuns(&first, &last, 1, s->screenMode);
        return p != NULL ? p : palOfRuns(&first, &last, 1);
    }

    static VdpLayout withPal(VdpLayout l, const UInt32* rgb)
    {
        l.rgb = rgb;
        return l;
    }

    /* The frame's end registers with the palette of the lines they describe. */
    VdpLayout frameEndLayout(const VdpDebugState* s) const
    {
        VdpLayout l = withPal(vdpLayoutOf(s), palFrameEnd(s));
        if (raster.valid) l.textSplit = raster.endSplit;
        return l;
    }

    /* For picture keys: 0 for the snapshot's palette. */
    static unsigned palHash(const UInt32* rgb)
    {
        return rgb != NULL ? fnvWords(rgb, 16) : 0u;
    }

    /* The colours sprite n's pattern row r is drawn with: of the display line it is on,
    ** else of the sprite's first displayed line; NULL: the snapshot's. */
    const UInt32* spriteRowRgb(const VdpSpriteFrame& fr, int n, int r) const
    {
        if (!linePal.valid || fr.mode == 0 || fr.screenMode == 8) return NULL;
        const VdpSprite& sp = fr.spr[n];
        int d = vdpSpriteDisplayLine(fr, (sp.y + 1 + r * fr.mag) & 0xff);
        for (int i = 0; d < 0 && i < fr.size * fr.mag; i++) d = vdpSpriteDisplayLine(fr, (sp.y + 1 + i) & 0xff);
        return lineRgb(d);
    }

    /* ---- The sprite scene ---- */

    void ensureScene(const VdpDebugState* s)
    {
        unsigned ver;
        const VdpFrameShot* o = (const VdpFrameShot*)shell::frameShot(&ver);
        if (o != NULL && !o->valid) o = NULL;
        ensureRaster(s);
        ensureSprites(s);
        if (o != NULL) {
            if (o->seq < sprRowSeq) {
                lineRowReset();
                sprSelReset();
                sprPinKeepAttr = -1;
            }
            sprRowSeq = o->seq;
        }
        unsigned key[7] = { ver, shell::pictureVersion(), shell::rasterVersion(), (unsigned)(uintptr_t)ts(TS_SPR_OFF_BLANKED),
                            o != NULL ? 1u + (unsigned)o->sprTabCount + (o->stale ? 0x100u : 0u) : 0u,
                            (unsigned)vdpGetNoSpritesLimit() | ((unsigned)vdpGetSpritesEnable() << 1), (unsigned)raster.lines };
        if (memcmp(key, sceneKey, sizeof(key)) == 0) return;
        memcpy(sceneKey, key, sizeof(key));
        if (o != NULL) sprSceneSeq = o->seq;
        sprUseValid = o != NULL && o->sprUseCount > 0;
        sprUseSince = sprUseValid ? o->sprUseSince : 0;
        sprUseN = 0;
        for (int i = 0; sprUseValid && i < o->sprUseCount && i < VDP_SPR_USES; i++) {
            SprUse& u = sprUse[sprUseN];
            if (!vdpSpriteUseKey(s, o->sprUse[i], &u.key.attr, &u.key.patBase, &u.key.mode, &u.key.sizeKey)) continue;
            u.seq = o->sprUse[i].seq;
            sprUseN++;
        }
        int hotAttr = sprHotAttr();
        SprScene& sc = scene;
        int lines = raster.lines;
        sc.ver++;
        if (lines != sc.lines) lineRowReset();
        /* A stale shot holds the last drawn frame's tables while VRAM moves on. */
        sc.recorded = o != NULL && o->sprTabCount > 0 && !o->stale;
        sc.lines = lines;
        sc.drawLimit = vdpGetNoSpritesLimit() ? 32 : 0;
        sc.lost = sc.recorded ? o->sprTabLost : 0;
        sc.frames.clear();
        sc.lineFrame.assign(lines, -1);
        sc.lineState.assign(lines, SS_FALLBACK);
        if (sc.recorded) {
            sc.frames.resize(o->sprTabCount);
            for (int i = 0; i < o->sprTabCount; i++) vdpEvalSpritesFrom(s, o->sprTab[i], &sc.frames[i]);
            int y0 = outFirstLine(s, o) - o->displayOffset, first = -1, last = -1;
            for (int d = 0; d < lines; d++) {
                int y = d + y0;
                if (y < 0 || y >= o->rows || o->sprTabRow[y] >= o->sprTabCount) continue;
                sc.lineFrame[d] = o->sprTabRow[y];
                sc.lineState[d] = o->sprTabState[y];
                if (first < 0) first = d;
                last = d;
            }
            /* Arming came within the frame: the lines before or after it are not missing, only unknown. */
            for (int d = first; d >= 0 && d < lines; d++) {
                if (sc.lineFrame[d] < 0 && (d < last || sc.lost > 0)) sc.lineState[d] = sc.lost > 0 ? SS_LOST : SS_NONE;
            }
        }
        sceneFallback(s);
        sc.approx = 0;
        sc.lineOff.assign(lines, 0);
        for (int d = 0; d < lines; d++) {
            const VdpSpriteFrame* fr = sc.lineFrame[d] >= 0 ? &sc.frames[sc.lineFrame[d]] : NULL;
            sc.lineOff[d] = fr != NULL && (fr->off != NULL || fr->mode == 0 ||
                                           (sc.lineState[d] == SS_FALLBACK && d < (int)raster.spritesOff.size() && raster.spritesOff[d]));
            /* Lines that show no sprites do not count, but the first after sprites stopped may still have drawn them. */
            bool prevShows = d > 0 && sc.lineFrame[d - 1] >= 0 && !sc.lineOff[d - 1] && sc.lineState[d - 1] != SS_NONE && sc.lineState[d - 1] != SS_LOST;
            sc.approx += sc.lineState[d] == SS_APPROX && (!sc.lineOff[d] || prevShows);
        }
        sceneSlots(s);
        sprKeepSel();
        sprHotFind(hotAttr);
    }

    /* Frames for the lines no table was recorded for: the band's registers, VRAM at the frame's end. */
    void sceneFallback(const VdpDebugState* s)
    {
        SprScene& sc = scene;
        std::vector<int> keys;
        for (int d = 0; d < sc.lines; d++) {
            if (sc.lineState[d] != SS_FALLBACK) continue;
            if (!raster.valid) {
                if (sprites.mode == 0) continue;
                if (keys.empty()) {
                    sc.frames.push_back(sprites);
                    keys.push_back(-1);
                }
                sc.lineFrame[d] = (int16_t)(sc.frames.size() - 1);
                continue;
            }
            int kb = spriteBandOf(d), key[8];
            const VdpBand& b = raster.bands[kb >= 0 ? kb : 0];
            if (b.mode < 0 || vdpSpriteMode(b.mode) == 0) continue;
            spriteKey(b, key);
            size_t i = 0;
            while (i < keys.size() / 8 && memcmp(&keys[i * 8], key, sizeof(key)) != 0) i++;
            if (i == keys.size() / 8) {
                VdpLayout l = { b.regs, b.mode, b.blink };
                sc.frames.emplace_back();
                vdpEvalSprites(s, l, sc.lines, &sc.frames.back());
                keys.insert(keys.end(), key, key + 8);
                sc.lineFrame[d] = (int16_t)(sc.frames.size() - 1);
                continue;
            }
            /* The fallback frames follow the recorded ones in frames. */
            sc.lineFrame[d] = (int16_t)(sc.frames.size() - keys.size() / 8 + i);
        }
    }

    /* Slots, and each sprite's lines, drops and collisions on its slot's lines. */
    void sceneSlots(const VdpDebugState* s)
    {
        SprScene& sc = scene;
        sc.slots.clear();
        sc.lineSlot.assign(sc.lines, -1);
        sc.collX = sc.collLine = sc.firstDropLine = sc.firstDropped = -1;
        sc.overLines = 0;
        std::vector<int> perFrame;
        for (int d = 0; d < sc.lines; d++) {
            int fi = sc.lineFrame[d];
            if (fi < 0 || sc.lineOff[d] || sc.lineState[d] == SS_NONE || sc.lineState[d] == SS_LOST) continue;
            const VdpSpriteFrame& fr = sc.frames[fi];
            SprSlot id = {};
            id.attr = fr.spr[0].attrAddr;
            id.patBase = vdpSpritePatternAddr(s, fr, 0);
            id.mode = fr.mode;
            id.sizeKey = fr.size * 2 + fr.mag;
            size_t k = 0;
            while (k < sc.slots.size() && !(sc.slots[k].attr == id.attr && sc.slots[k].patBase == id.patBase &&
                                            sc.slots[k].mode == id.mode && sc.slots[k].sizeKey == id.sizeKey)) k++;
            if (k == sc.slots.size() && k < 64) {
                id.first = d;
                id.rep = fi;
                sc.slots.push_back(id);
            }
            if (k == sc.slots.size()) continue;
            SprSlot& sl = sc.slots[k];
            sc.lineSlot[d] = (int8_t)k;
            sl.lines++;
            if (sl.runs > 0 && sl.runLast[sl.runs - 1] == d - 1) sl.runLast[sl.runs - 1] = d;
            else if (sl.runs < 4) { sl.runFirst[sl.runs] = sl.runLast[sl.runs] = d; sl.runs++; }
            else sl.runLast[3] = d;
            if (fi != sl.rep) sl.versions = true;
        }
        /* The representative: the frame laying out most of the slot's lines. */
        perFrame.assign(sc.frames.size() * (sc.slots.size() + 1), 0);
        for (int d = 0; d < sc.lines; d++) {
            if (sc.lineSlot[d] >= 0) perFrame[(size_t)sc.lineSlot[d] * sc.frames.size() + sc.lineFrame[d]]++;
        }
        for (size_t k = 0; k < sc.slots.size(); k++) {
            int best = sc.slots[k].rep;
            for (size_t i = 0; i < sc.frames.size(); i++) {
                if (perFrame[k * sc.frames.size() + i] > perFrame[k * sc.frames.size() + best]) best = (int)i;
            }
            sc.slots[k].rep = best;
        }
        sc.agg.assign(sc.slots.size() * 32, SprAgg{ 0, 0, 0, -1, -1, false });
        for (int d = 0; d < sc.lines; d++) {
            int k = sc.lineSlot[d];
            if (k < 0) continue;
            const VdpSpriteFrame& fr = sc.frames[sc.lineFrame[d]];
            int L = (d + fr.vscroll) & 0xff;
            const VdpSpriteLine& ln = fr.line[L];
            for (int j = 0; j < ln.count; j++) {
                SprAgg& a = sc.agg[(size_t)k * 32 + ln.id[j]];
                int st = vdpSpriteState(fr, L, j);
                if (st == VDP_SPRITE_DRAWN) a.drawn++;
                else if (st == VDP_SPRITE_CC_HIDDEN) a.hidden++;
                else {
                    a.dropped++;
                    if (a.firstDropped < 0) a.firstDropped = d;
                    a.lastDropped = d;
                }
            }
            if (ln.count > fr.limit) {
                sc.overLines++;
                if (sc.firstDropLine < 0) {
                    sc.firstDropLine = d;
                    sc.firstDropped = ln.id[fr.limit];
                }
            }
            int x;
            uint32_t colliders;
            if (!vdpSpriteLineCollision(s, fr, d, &x, &colliders)) continue;
            if (sc.collLine < 0) {
                sc.collLine = d;
                sc.collX = x;
            }
            for (int n = 0; n < 32; n++) {
                if ((colliders >> n) & 1) sc.agg[(size_t)k * 32 + n].collides = true;
            }
        }
    }

    /* The frame of display line d, or NULL. */
    const VdpSpriteFrame* sceneFrame(int d) const
    {
        if (d < 0 || d >= (int)scene.lineFrame.size() || scene.lineFrame[d] < 0) return NULL;
        return &scene.frames[scene.lineFrame[d]];
    }

    /* The hovered sprite's table, while its slot and frame index still hold, or -1. */
    int sprHotAttr() const
    {
        const SprHot& h = sprHotNext;
        if (h.ver != scene.ver || h.n < 0) return -1;
        if (h.frame == SPR_KEPT) return sprSel.attr;
        if (h.frame < 0 || h.frame >= (int)scene.frames.size()) return -1;
        return scene.frames[h.frame].spr[0].attrAddr;
    }

    /* The hover of the scene before in this one, as the picture changes while the mouse rests: the
    ** same sprite of its table, on its line where that table lays it out, the shown table's kept frame
    ** while it is away; none when the table is gone. */
    void sprHotFind(int attr)
    {
        SprHot& h = sprHotNext;
        if (attr < 0) return;
        int sel = sprSelSet ? slotOfKey(sprSel) : -1;
        h.ver = scene.ver;
        h.slot = h.sel ? (sprSelSet && sprSel.attr == attr ? sel : -1) : slotOfAttr(attr, h.d);
        if (h.slot >= 0) {
            bool onLine = h.d >= 0 && h.d < (int)scene.lineSlot.size() && scene.lineSlot[h.d] == h.slot;
            h.frame = onLine ? scene.lineFrame[h.d] : scene.slots[h.slot].rep;
            h.sel = h.slot == sel;
        }
        else if (h.sel && sprSelKept && sprSel.attr == attr) h.frame = SPR_KEPT;
        else {
            h.n = h.frame = -1;
            h.sel = false;
        }
    }

    /* The slot whose table holds attribute address attr, laying out line d if it can, or -1. */
    int slotOfAttr(int attr, int d) const
    {
        if (attr < 0) return -1;
        if (d >= 0 && d < (int)scene.lineSlot.size() && scene.lineSlot[d] >= 0 && scene.slots[scene.lineSlot[d]].attr == (attr & ~0x7f)) {
            return scene.lineSlot[d];
        }
        for (size_t k = 0; k < scene.slots.size(); k++) {
            if (scene.slots[k].attr == (attr & ~0x7f)) return (int)k;
        }
        return -1;
    }

    static SprKey slotKey(const SprSlot& sl) { return SprKey{ sl.attr, sl.patBase, sl.mode, sl.sizeKey }; }
    static bool sameKey(const SprKey& a, const SprKey& b)
    {
        return a.attr == b.attr && a.patBase == b.patBase && a.mode == b.mode && a.sizeKey == b.sizeKey;
    }
    static bool keyBefore(const SprKey& a, const SprKey& b)
    {
        if (a.attr != b.attr) return a.attr < b.attr;
        if (a.patBase != b.patBase) return a.patBase < b.patBase;
        return a.mode != b.mode ? a.mode < b.mode : a.sizeKey < b.sizeKey;
    }
    /* The key of the table frame fr lays out, as sceneSlots makes it. */
    static SprKey frameKey(const VdpDebugState* s, const VdpSpriteFrame& fr)
    {
        return SprKey{ fr.spr[0].attrAddr, vdpSpritePatternAddr(s, fr, 0), fr.mode, fr.size * 2 + fr.mag };
    }

    /* The slot of table k in this picture, or -1. */
    int slotOfKey(const SprKey& k) const
    {
        for (size_t i = 0; i < scene.slots.size(); i++) {
            if (sameKey(slotKey(scene.slots[i]), k)) return (int)i;
        }
        return -1;
    }

    /* The first slot by line at k's address in k's mode, or -1. */
    int slotAtAddr(const SprKey& k) const
    {
        for (size_t i = 0; i < scene.slots.size(); i++) {
            if (scene.slots[i].attr == k.attr && scene.slots[i].mode == k.mode) return (int)i;
        }
        return -1;
    }

    int sprSeenOf(const SprKey& k) const
    {
        for (size_t i = 0; i < sprSeen.size(); i++) {
            if (sameKey(sprSeen[i].key, k)) return (int)i;
        }
        return -1;
    }

    /* seq raised to the last picture the record has table key (key NULL: any table at attribute
    ** address attr) used in; never past the scene's, as a frame in progress is stamped ahead. */
    UInt32 sprRaiseSeq(const SprKey* key, int attr, UInt32 seq) const
    {
        for (int i = 0; i < sprUseN; i++) {
            if (key != NULL ? !sameKey(sprUse[i].key, *key) : sprUse[i].key.attr != attr) continue;
            UInt32 used = (Int32)(sprUse[i].seq - sprSceneSeq) > 0 ? sprSceneSeq : sprUse[i].seq;
            if ((Int32)(used - seq) > 0) seq = used;
        }
        return seq;
    }

    /* Pictures since table i of sprSeen was last used; when a record holds no use of it and it
    ** is not in this picture, at least those since the record began. */
    UInt32 sprSeenAge(int i) const
    {
        UInt32 age = sprSceneSeq - sprSeen[i].seq;
        if (!sprUseValid || age == 0) return age;
        for (int k = 0; k < sprUseN; k++) {
            if (sameKey(sprUse[k].key, sprSeen[i].key)) return age;
        }
        Int32 before = (Int32)(sprSceneSeq - sprUseSince);
        return before >= 0 && (UInt32)before + 1 > age ? (UInt32)before + 1 : age;
    }

    /* The shown table's slot in this picture, or -1 while it is away or none is chosen. */
    int selectedSlot(const Focus&) const
    {
        return sprSelSet ? slotOfKey(sprSel) : -1;
    }

    SprView shownTable() const
    {
        SprView v = { -1, NULL, NULL };
        if (!sprSelSet) return v;
        v.slot = slotOfKey(sprSel);
        if (v.slot >= 0) {
            v.fr = &scene.frames[scene.slots[v.slot].rep];
            v.agg = &scene.agg[(size_t)v.slot * 32];
        }
        else {
            v.fr = sprSelKept ? sprSelKeep : NULL;
            v.agg = sprSelKept ? sprSelAgg : NULL;
        }
        return v;
    }

    /* Why the VDP shows no sprites in this picture, or NULL when a line shows some; fr's reason
    ** without a scene. The Sprites card, its summary and the tab's head all say this. */
    const char* sprOffWhy(const VdpSpriteFrame& fr) const
    {
        if (!scene.slots.empty()) return NULL;
        for (const VdpSpriteFrame& sf : scene.frames) {
            if (sf.off != NULL) return sf.off;
        }
        return !scene.frames.empty() ? ts(TS_SPR_OFF_BLANKED) : fr.off;
    }

    /* Whether the shown table lays out display line d, in this picture or when kept. */
    bool shownLays(int d) const
    {
        if (d < 0) return false;
        if (sprSlotSel >= 0) return d < (int)scene.lineSlot.size() && scene.lineSlot[d] == sprSlotSel;
        return sprSelKept && d < (int)sprSelLines.size() && sprSelLines[d];
    }

    /* What line d does to pattern row r of sprite n of fr, slot fr's: VdpSpriteState, -1 out of
    ** range, -3 off screen, -4 another table's line; from the kept states for the kept frame. */
    int sprRowState(const VdpSpriteFrame& fr, int slot, int n, int r) const
    {
        if (slot < 0 && &fr == sprSelKeep && sprSelKept) return sprSelRow[n][r];
        int d = sprRowLine(fr, n, r);
        if (d < 0) return -3;
        if (slot < 0 || d >= (int)scene.lineSlot.size() || scene.lineSlot[d] != slot) return -4;
        return sprStateOn(d, n);
    }

    /* Slot k as last laid out. */
    void sprKeepSlot(int k, SprKeep& kp) const
    {
        const VdpSpriteFrame& fr = scene.frames[scene.slots[k].rep];
        for (int n = 0; n < 32; n++) {
            for (int r = 0; r < 16; r++) kp.row[n][r] = (int8_t)(r < fr.size ? sprRowState(fr, k, n, r) : -3);
        }
        kp.fr = fr;
        memcpy(kp.agg, &scene.agg[(size_t)k * 32], sizeof(kp.agg));
        kp.lines.assign(scene.lines, 0);
        for (int d = 0; d < scene.lines; d++) kp.lines[d] = scene.lineSlot[d] == k;
    }

    /* A table's last layout as the shown table's kept frame, sprites, row states and lines. */
    void sprUseKeep(const SprKeep& kp, const SprSlot& sl)
    {
        if (sprSelKeep == NULL) sprSelKeep = new VdpSpriteFrame;
        *sprSelKeep = kp.fr;
        sprSelSlot = sl;
        memcpy(sprSelAgg, kp.agg, sizeof(sprSelAgg));
        memcpy(sprSelRow, kp.row, sizeof(sprSelRow));
        sprSelLines = kp.lines;
        sprSelKept = true;
    }

    /* Shows and holds table key from now on, counted as seen now; the line row starts afresh. */
    void sprChoose(const SprKey& key)
    {
        sprSelSeenSeq = sprSceneSeq;
        if (sprSelSet && sameKey(key, sprSel)) return;
        int k = slotOfKey(key);
        sprSel = key;
        sprSelSet = true;
        sprSelKept = false;
        int i = sprSeenOf(sprSel);
        if (i >= 0 && sprSeen[i].keep) {
            sprUseKeep(*sprSeen[i].keep, sprSeen[i].slot);
        }
        else if (k >= 0) {
            std::unique_ptr<SprKeep> kp(new SprKeep);
            sprKeepSlot(k, *kp);
            sprUseKeep(*kp, scene.slots[k]);
        }
        lineRowReset();
        sprRowHoldSel();
    }

    /* Puts the shown table in the selector's row, in place of the one unseen longest. */
    void sprRowHoldSel()
    {
        if (!sprSelSet || sprSeenOf(sprSel) < 0) return;
        for (int r = 0; r < sprRowN; r++) {
            if (sameKey(sprRow[r], sprSel)) return;
        }
        if (sprRowN < 4) {
            sprRow[sprRowN++] = sprSel;
            return;
        }
        int old = 0;
        UInt32 oldAge = 0;
        for (int r = 0; r < sprRowN; r++) {
            int i = sprSeenOf(sprRow[r]);
            UInt32 age = i >= 0 ? sprSeenAge(i) : ~0u;
            if (r == 0 || age > oldAge) { old = r; oldAge = age; }
        }
        sprRow[old] = sprSel;
    }

    /* The selector's row: tables that dropped out leave, empty places take the latest seen, and
    ** the shown table is in it; nothing else moves. */
    void sprRowUpdate()
    {
        int n = 0;
        for (int r = 0; r < sprRowN; r++) {
            if (sprSeenOf(sprRow[r]) >= 0) sprRow[n++] = sprRow[r];
        }
        sprRowN = n;
        std::vector<int> latest;
        for (size_t i = 0; i < sprSeen.size(); i++) {
            bool in = false;
            for (int r = 0; r < sprRowN && !in; r++) in = sameKey(sprRow[r], sprSeen[i].key);
            if (!in) latest.push_back((int)i);
        }
        std::stable_sort(latest.begin(), latest.end(), [this](int a, int b) { return sprSeenAge(a) < sprSeenAge(b); });
        for (size_t i = 0; i < latest.size() && sprRowN < 4; i++) sprRow[sprRowN++] = sprSeen[latest[i]].key;
        sprRowHoldSel();
    }

    /* The tables of this picture, each kept as laid out, and the shown table held: while it is
    ** away its last layout is shown, until it has been unused SPR_AWAY_SHOWN frames (by the record
    ** when there is one, which sees between pictures); then, as when none is held, the first by line. */
    void sprKeepSel()
    {
        for (size_t k = 0; k < scene.slots.size(); k++) {
            const SprSlot& sl = scene.slots[k];
            int i = sprSeenOf(slotKey(sl));
            if (i < 0) {
                i = (int)sprSeen.size();
                sprSeen.emplace_back();
                sprSeen[i].key = slotKey(sl);
                sprSeen[i].keep.reset(new SprKeep);
            }
            sprSeen[i].seq = sprSceneSeq;
            sprSeen[i].slot = sl;
            sprKeepSlot((int)k, *sprSeen[i].keep);
        }
        for (SprSeen& e : sprSeen) e.seq = sprRaiseSeq(&e.key, 0, e.seq);
        if (sprSelSet) sprSelSeenSeq = sprRaiseSeq(&sprSel, 0, sprSelSeenSeq);
        int k = sprSelSet ? slotOfKey(sprSel) : -1;
        /* Gone a while, its address laid out in its mode: the game changed only R#6 or R#1, so the
        ** table there is followed. */
        if (k < 0 && sprSelSet && sprSceneSeq - sprSelSeenSeq >= (UInt32)SPR_AWAY_SHOWN) k = slotAtAddr(sprSel);
        if (k >= 0) {
            sprSel = slotKey(scene.slots[k]);
            int i = sprSeenOf(sprSel);
            sprUseKeep(*sprSeen[i].keep, sprSeen[i].slot);
            sprSelSeenSeq = sprSceneSeq;
        }
        else if (sprSelSet && sprSceneSeq - sprSelSeenSeq > (UInt32)SPR_AWAY_SHOWN) {
            sprSelSet = sprSelKept = false;
            lineRowReset();
        }
        if (!sprSelSet && !scene.slots.empty()) sprChoose(slotKey(scene.slots[0]));
        /* Tables unseen a while drop out, the oldest first past 64; never the chosen one. */
        for (;;) {
            int old = -1;
            for (size_t i = 0; i < sprSeen.size(); i++) {
                bool chosen = sprSelSet && sameKey(sprSeen[i].key, sprSel);
                UInt32 age = sprSeenAge((int)i);
                if (!chosen && (age > (UInt32)SPR_SEEN_KEEP || sprSeen.size() > 64) && (old < 0 || age > sprSeenAge(old))) old = (int)i;
            }
            if (old < 0) break;
            sprSeen.erase(sprSeen.begin() + old);
        }
        std::sort(sprSeen.begin(), sprSeen.end(), [](const SprSeen& a, const SprSeen& b) { return keyBefore(a.key, b.key); });
        sprRowUpdate();
    }

    /* No table chosen or seen: a new machine, chip or loaded state. */
    void sprSelReset()
    {
        sprSelSet = sprSelKept = false;
        sprSeen.clear();
        sprRowN = 0;
    }

    /* The frame a pinned sprite is read from: the one laying it out on its line, else on
    ** any line of its slot, else the slot's representative; NULL without a scene. */
    const VdpSpriteFrame* pinFrame(const Focus& f, int* slot) const
    {
        *slot = slotOfAttr(pinnedSpriteAttr(f), sprPinLine);
        /* Away for a few pictures (a double buffer): as last laid out, not another table's sprite. */
        if (*slot < 0 && sprPinKeep != NULL && sprPinKeepAttr >= 0 && sprPinKeepAttr == pinnedSpriteAttr(f)) return sprPinKeep;
        if (*slot < 0) *slot = selectedSlot(f);
        if (*slot < 0) return NULL;
        if (sprPinLine >= 0 && sprPinLine < (int)scene.lineSlot.size() && scene.lineSlot[sprPinLine] == *slot) return sceneFrame(sprPinLine);
        return &scene.frames[scene.slots[*slot].rep];
    }

    /* Keeps the pinned table's frame while a line lays it out and the picture a table at its
    ** address was last used in, by the record when there is one; forgets both on unpinning. */
    void sprKeepPin(const Focus& f)
    {
        int attr = pinnedSpriteAttr(f);
        if (attr < 0) {
            sprPinKeepAttr = -1;
            sprPinKeepVer = ~0u;
            return;
        }
        if (sprPinKeepVer == scene.ver) return;
        sprPinKeepVer = scene.ver;
        int slot = slotOfAttr(attr, sprPinLine);
        if (slot >= 0) {
            if (sprPinKeep == NULL) sprPinKeep = new VdpSpriteFrame;
            int d = sprPinLine >= 0 && sprPinLine < (int)scene.lineSlot.size() && scene.lineSlot[sprPinLine] == slot ? sprPinLine : scene.slots[slot].first;
            *sprPinKeep = *sceneFrame(d);
            sprPinKeepAttr = attr;
            sprPinKeepAgg = scene.agg[(size_t)slot * 32 + ((attr & 0x7f) >> 2)];
            sprPinSeenSeq = sprSceneSeq;
        }
        else if (sprPinKeepAttr == attr) {
            sprPinSeenSeq = sprRaiseSeq(NULL, attr & ~0x7f, sprPinSeenSeq);
        }
        else {
            sprPinKeepAttr = -1;
        }
    }

    /* The limit the emulator draws with: the VDP's unless told to draw past it. */
    int drawLimitOf(const VdpSpriteFrame& fr) const
    {
        return scene.drawLimit ? scene.drawLimit : fr.limit;
    }

    /* The band the views take as context: the one picked on the Splits card,
    ** else the band of a line pinned on the Output, or -1. */
    int ctxBand() const
    {
        if (selBand >= 0 && selBand < (int)raster.bands.size()) return selBand;
        const Focus& f = shell::shownFocus();
        return f.pinned ? bandOfFocus(f) : -1;
    }

    /* A pixel or tile picked on a picture drawn band by band: addr is
    ** physical, a the dot in its band's mode and b the display line. */
    static bool bandFocus(const Focus& f)
    {
        return (f.kind == FOCUS_PIXEL || f.kind == FOCUS_TILE) && f.space == FOCUS_PHYS;
    }

    /* The band a band's pin is on when that band still draws its kind, or -1. */
    int bandOfFocus(const Focus& f) const
    {
        int bi = bandFocus(f) ? vdpRasterBandAt(raster, f.b) : -1;
        if (bi < 0) return -1;
        const VdpBand& b = raster.bands[bi];
        bool fits = f.kind == FOCUS_PIXEL ? vdpIsBitmapMode(b.mode) : vdpIsPatternMode(b.mode);
        return fits && bandDrawn(b) ? bi : -1;
    }

    /* Dots and cell width a band's pin counts x in: x means another place once they change. */
    static int pinGrid(const VdpBand& b)
    {
        return vdpIsBitmapMode(b.mode) ? vdpBitmapWidth(b.mode) : vdpNameTableWidth(b.mode) * 16 + cellWidth(b.mode);
    }

    /* The pin's address again from its band. It goes when its line has left the
    ** display or its band (the frame before's, below the beam) shows another kind or
    ** grid; it stays while the line has no band or one in no known mode. */
    void refreshBandPin(const VdpDebugState* s, Focus& f)
    {
        if (!f.pinned || !bandFocus(f)) {
            pinSeen.kind = -1;  /* a pin made again on the same spot starts afresh */
            return;
        }
        ensureRaster(s);
        int bi = bandOfFocus(f), at = raster.valid ? vdpRasterBandAt(raster, f.b) : -1;
        bool fresh = pinSeen.kind != f.kind || pinSeen.a != f.a || pinSeen.b != f.b;
        bool gone = raster.valid && f.b >= raster.lines;
        if (bi < 0 && at >= 0 && bandDrawn(raster.bands[at])) gone = true;
        if (bi >= 0 && !fresh && pinGrid(raster.bands[bi]) != pinSeen.grid) gone = true;
        if (gone) {
            f.pinned = false;
            f.kind = FOCUS_NONE;
            return;
        }
        if (bi < 0) return;
        const VdpBand& b = raster.bands[bi];
        pinSeen = PinSeen{ f.kind, f.a, f.b, pinGrid(b) };
        VdpLayout l = vdpLayoutOf(b);
        if (f.kind == FOCUS_PIXEL) {
            VdpBitmapOptions o;
            VdpPixelInfo px;
            vdpBandBitmap(s, b, &o);
            if (vdpBitmapPixel(s, l, o, f.a, f.b, &px)) f.addr = (UInt32)px.physical;
        }
        else {
            VdpCharInfo ci;
            if (vdpNameTableChar(s, l, f.a, f.b, -1, &ci)) f.addr = (UInt32)ci.patternAddr;
        }
    }

    /* The group holding a band, or -1. */
    int groupOfBand(int bi) const
    {
        for (int g = 0; bi >= 0 && g < (int)groups.size(); g++) {
            if (vdpGroupHasBand(groups[g], raster.bands[bi])) return g;
        }
        return -1;
    }

    void selectBand(int i)
    {
        selBand = i;
        cardBand = i >= 0;
        if (i >= 0) sp3.unpin();
        selWanted = i >= 0 && i < (int)raster.bands.size();
        if (selWanted) selKey = vdpBandKeyOf(raster.bands[i]);
    }

    /* Whether pieces from vdpRasterBandCols fall on page p. */
    static bool showsPage(const int* seg, int n, int p)
    {
        for (int k = 0; k < n; k++) {
            if (seg[k * 3] == p) return true;
        }
        return false;
    }

    /* Frames a band's pieces on the Bitmap picture: pages and columns from seg, rows by page. */
    void bandFrames(ImDrawList* dl, const ui::CanvasXf& xf, const VdpDebugState* s, const VdpBand& b, const int* seg, int n,
                    int bandIndex, float th)
    {
        const UiTokens& t = uiTokens();
        for (int k = 0; k < n; k++) {
            int p = seg[k * 3];
            if (p < 0 || p >= grid.pages) continue;
            int ox, oy;
            grid.origin(p, &ox, &oy);
            std::vector<int> runs;
            vdpRasterBandRows(s, b, p, runs);
            float x0 = ox + (float)grid.tileW * seg[k * 3 + 1] / 256, x1 = ox + (float)grid.tileW * seg[k * 3 + 2] / 256;
            for (size_t q = 0; q + 1 < runs.size(); q += 2) {
                ImVec2 a = xf.toScreen(x0, (float)(oy + runs[q])), c = xf.toScreen(x1, (float)(oy + runs[q + 1]));
                if (bandIndex == selBand) dl->AddRectFilled(a, c, ui::WithAlpha(t.accent, 50));
                /* Inset by the line width, but never inside out for a piece a few dots wide. */
                float ix = fminf(th, (c.x - a.x) / 2), iy = fminf(th, (c.y - a.y) / 2);
                dl->AddRect(ImVec2(a.x + ix, a.y + iy), ImVec2(c.x - ix, c.y - iy), bandColour(bandIndex), 0, th);
            }
        }
    }

    bool rasterMarks() const
    {
        return raster.valid && !raster.marks.empty();
    }

    /* The Output image row a display line is drawn on, or -1. With the borders,
    ** lines above and below the display have rows too. */
    int rowOfDisplayLine(int d) const
    {
        int y = d + outLine0;
        if (!outPicBorders && (d < 0 || d >= raster.lines)) return -1;
        return y >= 0 && y < ovRows ? y : -1;
    }


    ImU32 bandColour(int i) const
    {
        const UiTokens& t = uiTokens();
        return i == selBand ? t.accent : t.series[(i + 1) % 6];
    }

    /* Display line d as "line 115 (display 101)". */
    void lineName(int d, char* out, size_t size) const
    {
        textf(out, size, ts(TS_LINE_NAME), raster.firstLine + d, d);
    }

    /* Dots a split on m's line is counted in, by the mode of that line's band; -1 without one. */
    int splitWidth(const VdpRasterMark& m) const
    {
        int d = m.line - raster.firstLine, b = d >= 0 && d < (int)raster.bandOfLine.size() ? raster.bandOfLine[d] : -1;
        if (b < 0 || b >= (int)raster.bands.size() || raster.bands[b].mode < 0 || raster.bands[b].unknown) return -1;
        int mode = raster.bands[b].mode;
        return vdpIsBitmapMode(mode) ? vdpBitmapWidth(mode) : 256;
    }

    void markText(const VdpRasterMark& m, char* out, size_t size) const
    {
        char from[256];
        if (m.kind == VDP_RASTER_LINEINT) {
            textf(out, size, ts(TS_MARK_LINEINT), m.line, m.line - raster.firstLine, m.value,
                  ts(m.aux ? TS_MARK_IE1_ON : TS_MARK_IE1_MASKED), m.prev ? ts(TS_MARK_PREV_FRAME) : "");
            return;
        }
        lineName(m.fromLine - raster.firstLine, from, sizeof(from));
        int at = textf(out, size, ts(TS_MARK_WRITE), m.line, m.line - raster.firstLine, m.cycle, m.reg, vdpRegisterName(m.reg),
                       m.aux, m.value, from);
        if (m.splitDot >= 0 && at > 0 && (size_t)at < size) {
            int w = splitWidth(m);
            if (w < 0) w = ovSplitWidth;
            at += textf(out + at, size - at, ts(TS_MARK_SPLIT), m.splitDot * w / 256, w, m.line - raster.firstLine);
        }
        if (m.prev && at > 0 && (size_t)at < size) textf(out + at, size - at, "%s", ts(TS_MARK_PREV_FRAME));
    }

    /* Every mark on the gutter row nearest y (within one), joined for the status line. */
    bool marksAtRow(int y, char* out, size_t size) const
    {
        int bestRow = -1, bestDist = 2;
        for (const VdpRasterMark& m : raster.marks) {
            int r = rowOfDisplayLine(m.line - raster.firstLine);
            int dist = r < 0 ? 99 : r > y ? r - y : y - r;
            if (dist < bestDist) { bestRow = r; bestDist = dist; }
        }
        if (bestRow < 0) return false;
        size_t at = 0;
        out[0] = 0;
        for (const VdpRasterMark& m : raster.marks) {
            if (rowOfDisplayLine(m.line - raster.firstLine) != bestRow || at + 4 >= size) continue;
            if (at) at += (size_t)textf(out + at, size - at, "  |  ");
            markText(m, out + at, size - at);
            at = strlen(out);
        }
        return true;
    }

    void drawRaster(ImDrawList* dl, const ui::CanvasXf& xf) const
    {
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1)), w = (float)ovWidth;
        if (!raster.valid) return;
        bool split = raster.bands.size() > 1;
        dl->PushClipRect(xf.clipMin, xf.clipMax, true);
        for (size_t i = 0; split && i < raster.bands.size(); i++) {
            const VdpBand& b = raster.bands[i];
            ImU32 col = bandColour((int)i);
            int y0 = rowOfDisplayLine(b.first), y1 = rowOfDisplayLine(b.last);
            if (y0 < 0 || y1 < 0) continue;
            ImVec2 a = xf.toScreen(0, (float)y0), c = xf.toScreen(w, (float)(y1 + 1));
            if ((int)i == selBand) dl->AddRectFilled(a, c, ui::WithAlpha(t.accent, 50));
            /* Dashed where only the sprites' registers change: the background goes on. */
            if (i > 0 && spriteOnlySplit(raster.bands[i - 1], b)) dashedLine(dl, a, c.x, ui::WithAlpha(col, 160), th);
            else if (i > 0) dl->AddLine(ImVec2(a.x, a.y), ImVec2(c.x, a.y), col, th);
        }
        dl->PopClipRect();

        /* Marks at the gutter's right edge: ticks for writes, triangles for line interrupts. */
        if (xf.gutterMax.x <= xf.gutterMin.x) return;
        float x1 = xf.gutterMax.x, x0 = x1 - ui::Px(10);
        ovMarkX = x1 - ui::Px(12);
        for (const VdpRasterMark& m : raster.marks) {
            int y = rowOfDisplayLine(m.line - raster.firstLine);
            if (y < 0) continue;
            float cy = floorf((xf.toScreen(0, (float)y).y + xf.toScreen(0, (float)(y + 1)).y) / 2);
            if (m.kind == VDP_RASTER_LINEINT) {
                float h = ui::Px(4);
                ImVec2 p1(x0, cy - h), p2(x1, cy), p3(x0, cy + h);
                if (m.aux) dl->AddTriangleFilled(p1, p2, p3, t.text);
                else dl->AddTriangle(p1, p2, p3, t.text, th);
            }
            else {
                ImU32 col = m.reg == 23 ? t.accent : m.reg == 2 ? t.series[1] : t.textSecondary;
                if (m.prev) col = ui::WithAlpha(col, 120);
                dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, th * 2);
            }
        }
    }

    static void pin(Focus& f, const Focus& hit, bool clicked)
    {
        if (clicked) {
            /* A sprite is the same pin whichever line or view it was clicked on; VRAM views
            ** share a and b as picture spots, so a VRAM pin also needs the same bytes. */
            bool same = f.pinned && f.kind == hit.kind && f.a == hit.a && (hit.kind == FOCUS_SPRITE ? f.addr == hit.addr : f.b == hit.b);
            if (same && hit.kind == FOCUS_VRAM) same = f.addr == hit.addr && f.space == hit.space;
            f = hit;
            f.pinned = !same;
            if (same) f.kind = FOCUS_NONE;
        }
        else if (!f.pinned) {
            f = hit;
            f.pinned = false;
        }
    }

    /* The character a focus points at, or -1. Tiles keep scroll lines. */
    int focusChar(const VdpDebugState* s, const VdpLayout& l, const Focus& f) const
    {
        VdpCharInfo ci;
        if (bandFocus(f)) {
            int bi = bandOfFocus(f);
            if (f.kind == FOCUS_TILE && bi >= 0 && vdpNameTableChar(s, vdpLayoutOf(raster.bands[bi]), f.a, f.b, -1, &ci)) return ci.ch;
            return -1;
        }
        if (f.kind == FOCUS_TILE && vdpNameTableChar(s, l, f.a, f.b, 0, &ci)) return ci.ch;
        if (f.kind == FOCUS_CHAR && vdpSheetChar(s, l, f.a, f.b, &ci)) return ci.ch;
        return -1;
    }

    /* ---- Under the Screen: the Pages card, folding as the VRAM map does ---- */

    /* The pages' height, from the splitter before the card takes the rest; room is
    ** the height of the card and the pages. */
    float bottomHeight(const VdpDebugState* s, float room)
    {
        ensureRaster(s);
        if (!shell::pages().open) return 0;
        stripGroupsFor(s);
        stripItemsFor(s, false);
        return shell::pagesHeight(room);
    }

    void drawBottom(const VdpDebugState* s, Focus& f, float paneH)
    {
        bool sizeChanged;
        if (!shell::pagesHeader(paneH, &sizeChanged)) return;
        int size = shell::pages().size;
        float gap = ui::Px(8), cw = 0;
        if (sizeChanged && !stripItems.empty()) {
            /* A new size keeps the thumbnail at the left there; the pane keeps the splitter's height. */
            float oldTw = stripTw;
            stripLayout(&stripTw, &cw, stripRows, size);
            if (oldTw > 0) pagesScroll.to = floorf(pagesScroll.x * (stripTw + gap) / (oldTw + gap));
        }
        /* One row of the size's thumbnails; drawStrip starts 4 px down and ends without the last gap. */
        float ch = stripLayout(&stripTw, &cw, stripRows, size);
        ch = ch > 0 ? ch + ui::Px(4) - gap : 0;
        shell::pagesBeginScroll(paneH, cw, ch, pagesScroll);
        char status[512] = "";
        if (!stripItems.empty()) {
            stripItemsFor(s, true);
            drawStrip(s, f, stripTw, stripRows, status, sizeof(status));
        }
        else {
            note(ts(TS_STRIP_EMPTY));
        }
        shell::pagesEnd(pagesScroll);
        if (status[0]) ui::StatusText("%s", status);
    }

    /* A sprite's pin follows its attribute address at the frame's end; one made where
    ** its table is known keeps the address of that table. */
    void refreshPin(const VdpDebugState* s, Focus& f)
    {
        bool sprite = f.pinned && f.kind == FOCUS_SPRITE && f.a >= 0 && f.a < 32;
        if (!sprite || ((sprPinAttr & 0x7f) >> 2) != f.a) sprPinned = false;
        if (sprite && !sprPinned && sprites.mode != 0) {
            focusAddr(s, sprites.screenMode, sprites.spr[f.a].attrAddr, 4, &f);
        }
    }

    /* The attribute address of the pinned sprite as outputBoxes matches it, or -1. */
    int pinnedSpriteAttr(const Focus& f) const
    {
        if (!f.pinned || f.kind != FOCUS_SPRITE || f.a < 0 || f.a >= 32) return -1;
        return sprPinned ? sprPinAttr : sprites.mode != 0 ? sprites.spr[f.a].attrAddr : -1;
    }

    /* What Output does with its sprites: shown, hidden or alone. */
    int sprLayer() const
    {
        return sprOnly ? SL_ONLY : sprShown ? SL_SHOWN : SL_HIDDEN;
    }

    /* [Show sprites] [Show sprites only] [Sprite boxes]; none in a frame without sprites. Show
    ** sprites only shows them, so Show sprites is on and unavailable while it is. */
    void spriteToggles(const VdpDebugState* s)
    {
        int w, ax, ay;
        if (!spriteLayerShape(s, &w, &ax, &ay)) return;
        bool shown = sprShown || sprOnly;
        ImGui::BeginDisabled(sprOnly);
        barArtToggle("##sprshow", ui::DECOR_NONE, ts(sprOnly ? TS_SPR_SHOW_ONLY_TIP : TS_SPR_SHOW_TIP), &shown);
        ImGui::EndDisabled();
        if (!sprOnly) sprShown = shown;
        barArtToggle("##spronly", ui::DECOR_CHECKER, ts(TS_SPR_LAYER_ONLY), &sprOnly);
        barArtToggle("##boxes", ui::DECOR_FRAME, ts(TS_BOXES_TIP), &boxes);
    }

    /* Secondary text on the command bar. */
    static void barText(const char* text)
    {
        ui::CommandBarItem(ImGui::CalcTextSize(text).x);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, uiTokens().textSecondary);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    /* The zoom at the bar's right end; with colour0, Colour 0 transparent in its menu. */
    void zoomGroup(ui::CanvasState& cv, bool colour0)
    {
        ui::CommandBarRight(ui::ZoomControlsWidth());
        ui::ZoomControls("zoom", cv, colour0 ? &transparent0 : NULL, ts(TS_COLOUR0_TRANSPARENT));
        if (colour0) cv.checker = transparent0;
    }

    /* ---- Output: the picture the VDP drew ---- */

    static bool textMode(int mode)
    {
        return mode == 0 || mode == 13;
    }

    /* Whether display line d is laid out as a text mode, by its band. */
    bool textLine(const VdpDebugState* s, int d) const
    {
        int b = raster.lines > 0 ? vdpRasterBandAt(raster, d < 0 ? 0 : d >= raster.lines ? raster.lines - 1 : d) : -1;
        int mode = b >= 0 && raster.bands[b].mode >= 0 ? raster.bands[b].mode : s->screenMode;
        return textMode(mode);
    }

    /* The frame line of display line 0: the register log's, as the bands count
    ** lines, when the picture is of the logged frame; else the picture's own. */
    int outFirstLine(const VdpDebugState* s, const VdpFrameShot* o) const
    {
        bool same = raster.valid && !o->stale && o->frameStart == s->raster[0].frameStart;
        return same ? raster.firstLine : o->firstLine;
    }

    /* The first display sample of display line d, in 544ths of a row (shift-0
    ** rows are widened to 544 samples too); R#18 and R#25 by the line's band. */
    int outOrigin(const VdpDebugState* s, const VdpFrameShot* o, int d) const
    {
        int adj = o->hAdjust;
        int b = raster.valid && raster.lines > 0 ? vdpRasterBandAt(raster, d < 0 ? 0 : d >= raster.lines ? raster.lines - 1 : d) : -1;
        if (b >= 0) {
            /* As the emulated VDP's onScrModeChange works HAdjust out. */
            const UInt8* r = raster.bands[b].regs;
            adj = vdpSetAdjust(r[18], false) + ((r[25] & 0x08) ? 4 : 0);
        }
        return (8 + adj + (textLine(s, d) ? o->hAdjustSc0 : 0)) * 2;
    }

    static uint32_t argb1555ToRgba(UInt16 c)
    {
        uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        return 0xff000000u | (b << 16) | (g << 8) | r;
    }

    /* Row y of the shot at 544 samples; bit 15, the key colour, is not shown. */
    static void outRow(const VdpFrameShot* o, int y, uint32_t* dst)
    {
        int shift = o->shift[y] ? 1 : 0;
        int n = o->maxWidth << shift;
        if (n > VDP_SHOT_WIDTH) n = VDP_SHOT_WIDTH;
        for (int x = 0; x < VDP_SHOT_WIDTH; x++) {
            int i = shift ? x : x >> 1;
            dst[x] = i < n ? argb1555ToRgba(o->pix[y][i]) : 0;
        }
    }

    /* Row y as shown, hidden (drawn again without sprites as the line ended)
    ** or sprites only (clear where the sprites changed nothing); false when
    ** that is not exact. */
    static bool outRowLayer(const VdpFrameShot* o, int y, int layer, uint32_t* dst)
    {
        outRow(o, y, dst);
        if (layer == SL_SHOWN) return true;
        int st = o->sprSplit ? o->sprRow[y] : VDP_SPR_ROW_NONE;
        if (st == VDP_SPR_ROW_NONE) {
            if (layer == SL_ONLY) memset(dst, 0, VDP_SHOT_WIDTH * sizeof(uint32_t));
            return false;
        }
        int shift = o->shift[y] ? 1 : 0;
        int n = o->maxWidth << shift;
        if (n > VDP_SHOT_WIDTH) n = VDP_SHOT_WIDTH;
        for (int x = 0; x < VDP_SHOT_WIDTH; x++) {
            int i = shift ? x : x >> 1;
            if (i >= n) continue;
            UInt16 bg = o->sprBg[y][i];
            bool cand = st == VDP_SPR_ROW_EXACT || ((o->sprMask[y][i >> 3] >> (i & 7)) & 1);
            if (layer == SL_HIDDEN) dst[x] = argb1555ToRgba(cand ? bg : o->pix[y][i]);
            else if (o->pix[y][i] == bg || !cand) dst[x] = 0;
        }
        return st == VDP_SPR_ROW_EXACT;
    }

    void decodeOutput(const VdpDebugState* s, const VdpFrameShot* o, int layer)
    {
        if (outBorders) {
            outPic.w = VDP_SHOT_WIDTH;
            outPic.h = o->rows;
            outPic.img.assign((size_t)outPic.w * outPic.h, 0);
            outRowApprox.assign(outPic.h, 0);
            for (int y = 0; y < o->rows; y++) outRowApprox[y] = !outRowLayer(o, y, layer, &outPic.img[(size_t)y * outPic.w]);
            return;
        }
        std::vector<uint32_t> row(VDP_SHOT_WIDTH);
        outPic.w = 512;
        outPic.h = raster.lines;
        outPic.img.assign((size_t)outPic.w * outPic.h, 0);
        outRowApprox.assign(outPic.h, 0);
        for (int d = 0; d < raster.lines; d++) {
            int y = d + outFirstLine(s, o) - o->displayOffset, x0 = outOrigin(s, o, d);
            if (y < 0 || y >= o->rows) continue;
            outRowApprox[d] = !outRowLayer(o, y, layer, row.data());
            for (int x = 0; x < 512; x++) {
                int i = x0 + x;
                outPic.img[(size_t)d * 512 + x] = i >= 0 && i < VDP_SHOT_WIDTH ? row[i] : 0;
            }
        }
    }

    /* Image rows of a band's pin, its line or the eight lines of its tile's cell
    ** within the band, as runs of [from, to). */
    void pinRows(const Focus& f, std::vector<int>& runs) const
    {
        runs.clear();
        int bi = f.pinned && bandFocus(f) ? bandOfFocus(f) : -1;
        if (bi < 0) return;
        const VdpBand& b = raster.bands[bi];
        int top = f.kind == FOCUS_TILE ? f.b - ((f.b + b.regs[23]) & 7) : f.b, end = f.kind == FOCUS_TILE ? top + 8 : top + 1;
        for (int d = top > b.first ? top : b.first; d < end && d <= b.last; d++) {
            int y = rowOfDisplayLine(d);
            if (y < 0) continue;
            if (!runs.empty() && runs.back() == y) runs.back() = y + 1;
            else { runs.push_back(y); runs.push_back(y + 1); }
        }
    }

    /* The sprites of display line d as the VDP laid them out: the frame of the table
    ** recorded for its row, *state how sure that is; NULL when none was recorded. */
    const VdpSpriteFrame* rowFrame(const VdpDebugState*, const VdpFrameShot* o, int d, int* state) const
    {
        if (o == NULL || d < 0 || d >= (int)scene.lineState.size()) return NULL;
        int st = scene.lineState[d];
        const VdpSpriteFrame* fr = sceneFrame(d);
        if ((st != SS_EXACT && st != SS_APPROX) || fr == NULL || fr->mode == 0) return NULL;
        *state = st;
        return fr;
    }

    /* Display line d at image row y, or -1 for a border row. */
    int displayLineOfRow(int y) const
    {
        int d = outPicBorders ? y - outLine0 : y;
        return d >= 0 && d < raster.lines ? d : -1;
    }

    /* Image rows [y0, y1) of a box drawn alike: style 0 solid, 1 the rows draw no dot,
    ** 2 the tables changed as drawn. */
    struct BoxSeg { int y0, y1, state, style; };
    /* A sprite's box over image rows [y0, y1): x0 and w in image dots, org the display's
    ** left edge; no label when it goes on from a box of the same sprite right above. */
    struct BoxRun { int attr, n, org, x0, w, y0, y1; bool live, label; std::vector<BoxSeg> seg; };

    /* Each sprite where the VDP drew it, row by row from the table that row was laid out from, or only
    ** the one at attribute address only. Style 1: colour 0 or a blank pattern row; none where SPD or a
    ** blank screen hides sprites. tab: the Sprites tab's placement, one row a line, at the VDP's own limit. */
    void boxRuns(int only, std::vector<BoxRun>& done, bool tab = false) const
    {
        const VdpDebugState* s = ovState;
        const VdpFrameShot* o = outShot;
        done.clear();
        if (!tab && (o == NULL || o->sprTabCount == 0)) return;
        typedef BoxRun Run;
        std::vector<Run> open;
        int lines = tab ? scene.lines : raster.lines;
        for (int d = 0; d <= lines; d++) {
            for (Run& r : open) r.live = false;
            int rowState = 0, y = d < lines ? (tab ? d : rowOfDisplayLine(d)) : -1;
            const VdpSpriteFrame* fr = y >= 0 ? (tab ? sceneShows(d) ? sceneFrame(d) : NULL : rowFrame(s, o, d, &rowState)) : NULL;
            /* On the tab a line read from another frame (a fallback) is as unsure as an approximate one. */
            if (tab && fr != NULL) rowState = scene.lineState[d] == SS_EXACT ? SS_EXACT : VDP_SPR_ROW_APPROX;
            /* SPD or a blank screen on this row: the VDP draws no sprite. */
            if (fr != NULL && fr->off == NULL) {
                int L = (d + fr->vscroll) & 0xff, org = outPicBorders && !tab ? outOrigin(s, o, d) : 0;
                const VdpSpriteLine& ln = fr->line[L];
                for (int k = 0; k < ln.count; k++) {
                    int n = ln.id[k], row = vdpSpriteRow(*fr, n, L);
                    const VdpSprite& sp = fr->spr[n];
                    if (row < 0 || (only >= 0 && sp.attrAddr != only)) continue;
                    int c = sp.color[row], span, x0 = org + 2 * vdpSpriteSpan(*fr, n, row, &span), w = 2 * span;
                    bool clear = vdpSpriteClear(s, *fr, c) || vdpSpriteRowBits(s, *fr, n, row) == 0;
                    BoxSeg g = { y, y + 1, vdpSpriteStateAt(*fr, L, k, tab ? fr->limit : drawLimitOf(*fr)), rowState == VDP_SPR_ROW_APPROX ? 2 : clear ? 1 : 0 };
                    size_t i = 0;
                    while (i < open.size() && !(open[i].attr == sp.attrAddr && open[i].org == org && open[i].x0 == x0 && open[i].w == w &&
                                                open[i].y1 == y)) i++;
                    if (i == open.size()) {
                        Run fresh = { sp.attrAddr, n, org, x0, w, y, y, true, true };
                        open.push_back(fresh);
                    }
                    Run& r = open[i];
                    r.y1 = y + 1;
                    r.live = true;
                    if (!r.seg.empty() && r.seg.back().state == g.state && r.seg.back().style == g.style) r.seg.back().y1 = y + 1;
                    else r.seg.push_back(g);
                }
            }
            for (size_t i = 0; i < open.size();) {
                if (open[i].live) { i++; continue; }
                /* Sprites show only over the display, not on the borders. */
                Run r = open[i];
                int x0 = r.x0 > r.org ? r.x0 : r.org, x1 = r.x0 + r.w < r.org + 512 ? r.x0 + r.w : r.org + 512;
                r.x0 = x0;
                r.w = x1 - x0;
                if (r.w > 0) done.push_back(r);
                open.erase(open.begin() + i);
            }
        }
        /* One number a sprite: a box that moved sideways under another of it goes without. */
        for (BoxRun& r : done) {
            for (const BoxRun& q : done) r.label = r.label && !(&q != &r && q.attr == r.attr && q.y1 == r.y0);
        }
    }

    /* One side of a box from a to b, across or down; dashed as dashedRect draws them. */
    static void boxEdge(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, bool dashed)
    {
        if (!dashed) {
            dl->AddLine(a, b, col, th);
            return;
        }
        dashedSegment(dl, a, b, col, th, ui::Px(4));
    }

    /* The boxes of boxRuns, after the picture. */
    void outputBoxes(ImDrawList* dl, const ui::CanvasXf& xf, int only) const
    {
        const Focus& f = shell::shownFocus();
        std::vector<BoxRun> done;
        boxRuns(only, done);
        drawBoxRuns(dl, xf, done, f.pinned ? pinnedSpriteAttr(f) : outHoverAttr);
    }

    /* Boxes in the accent for the sprite at attribute address focused; a dark halo keeps them seen on any colour. */
    void drawBoxRuns(ImDrawList* dl, const ui::CanvasXf& xf, const std::vector<BoxRun>& done, int focused) const
    {
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1));
        dl->PushClipRect(xf.clipMin, xf.clipMax, true);
        for (const BoxRun& r : done) {
            ImVec2 a = xf.toScreen((float)r.x0, (float)r.y0), b = xf.toScreen((float)(r.x0 + r.w), (float)r.y1);
            bool me = r.attr == focused;
            float lw = me ? th * 2 : th;
            ImU32 col = 0;
            dl->AddRect(a, b, ui::WithAlpha(t.layer, 200), 0, lw + th * 2);
            /* One outline; each part draws its sides, the first the top and the last the bottom. */
            for (size_t i = 0; i < r.seg.size(); i++) {
                const BoxSeg& g = r.seg[i];
                ImU32 c = me ? t.accent : g.state == VDP_SPRITE_DROPPED ? t.critical : g.style == 2 ? t.caution :
                          g.state == VDP_SPRITE_CC_HIDDEN || g.style == 1 ? t.textTertiary : ui::WithAlpha(t.text, 190);
                ImVec2 p = xf.toScreen((float)r.x0, (float)g.y0), q = xf.toScreen((float)(r.x0 + r.w), (float)g.y1);
                bool dashed = g.style != 0 && !me;
                if (i == 0) col = c;
                if (g.state == VDP_SPRITE_DROPPED) hatch(dl, p, q, ui::WithAlpha(t.critical, 150));
                if (r.seg.size() == 1 && dashed) dashedRect(dl, p, q, c, lw);
                else if (r.seg.size() == 1) dl->AddRect(p, q, c, 0, lw);
                else {
                    boxEdge(dl, p, ImVec2(p.x, q.y), c, lw, dashed);
                    boxEdge(dl, ImVec2(q.x, p.y), q, c, lw, dashed);
                    if (i == 0) boxEdge(dl, p, ImVec2(q.x, p.y), c, lw, dashed);
                    if (i + 1 == r.seg.size()) boxEdge(dl, ImVec2(p.x, q.y), q, c, lw, dashed);
                }
            }
            if (!r.label) continue;
            char num[8];
            snprintf(num, sizeof(num), "%d", r.n);
            ImVec2 ts = ImGui::CalcTextSize(num);
            if (b.x - a.x >= ts.x + ui::Px(4) && b.y - a.y >= ts.y) {
                dl->AddRectFilled(ImVec2(a.x + th, a.y + th), ImVec2(a.x + th + ts.x + ui::Px(2), a.y + th + ts.y), ui::WithAlpha(t.layer, 170));
                dl->AddText(ImVec2(a.x + th + ui::Px(1), a.y + th), col, num);
            }
        }
        dl->PopClipRect();
    }

    /* The gutter: sprites in range per display line, the limit as a tick. */
    void outputCountGutter(ImDrawList* dl, const ui::CanvasXf& xf) const
    {
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1)), gx0 = xf.gutterMin.x;
        float gw = xf.gutterMax.x - xf.gutterMin.x - (rasterMarks() ? ui::Px(12) : 0);
        int limit = vdpSpriteMode(ovState->screenMode) == 1 ? 4 : 8;
        float per = gw / (limit * 2);
        for (int d = 0; d < raster.lines; d++) {
            int st, y = rowOfDisplayLine(d);
            const VdpSpriteFrame* fr = y >= 0 ? rowFrame(ovState, outShot, d, &st) : NULL;
            if (fr == NULL || fr->off != NULL) continue;
            int c = fr->line[(d + fr->vscroll) & 0xff].count;
            if (c == 0) continue;
            ImVec2 a = xf.toScreen(0, (float)y), b = xf.toScreen(0, (float)(y + 1));
            float len = (float)(c < limit * 2 ? c : limit * 2) * per;
            /* Red past the VDP's limit, at the tick, also while the emulator draws them all. */
            dl->AddRectFilled(ImVec2(gx0, a.y), ImVec2(gx0 + len, fmaxf(b.y, a.y + 1)), c > fr->limit ? t.critical : t.textSecondary);
        }
        float tx = floorf(gx0 + per * limit);
        dl->AddLine(ImVec2(tx, xf.gutterMin.y), ImVec2(tx, xf.gutterMax.y), t.textTertiary, th);
    }

    /* The sprite drawn on top at image dot x of image row y, its frame and scroll line; -1 for none. */
    int outputSpriteAt(int x, int y, const VdpSpriteFrame** frame, int* scrollLine) const
    {
        int d = displayLineOfRow(y), st;
        const VdpSpriteFrame* fr = d >= 0 ? rowFrame(ovState, outShot, d, &st) : NULL;
        if (fr == NULL || fr->off != NULL) return -1;
        int L = (d + fr->vscroll) & 0xff, dot = (x - (outPicBorders ? outOrigin(ovState, outShot, d) : 0)) >> 1;
        if (dot < 0 || dot >= 256) return -1;
        int n = vdpSpriteAt(*fr, L, dot);
        if (n >= 0) {
            *frame = fr;
            *scrollLine = L;
        }
        return n;
    }

    /* E.g. "sprites 0, 1, 2, 3, 4, 5 - 4 and 5 dropped". */
    /* drawLimit: what the emulator draws, past the VDP's limit when told to. */
    static void lineSummary(const VdpSpriteFrame& fr, int scrollLine, int drawLimit, char* out, size_t size)
    {
        const VdpSpriteLine& ln = fr.line[scrollLine & 0xff];
        char all[160], dropped[160];
        size_t a = 0, d = 0;
        all[0] = dropped[0] = 0;
        for (int k = 0; k < ln.count && a < sizeof(all); k++) {
            a += (size_t)snprintf(all + a, sizeof(all) - a, "%s%d", k ? ", " : "", ln.id[k]);
        }
        for (int k = fr.limit; k < ln.count && d < sizeof(dropped); k++) {
            const char* sep = k == fr.limit ? "" : k == ln.count - 1 ? ts(TS_LIST_LAST) : ", ";
            d += (size_t)textf(dropped + d, sizeof(dropped) - d, "%s%d", sep, ln.id[k]);
        }
        if (ln.count == 0) textf(out, size, "%s", ts(TS_LINE_NO_SPRITES));
        else if (ln.count > fr.limit && drawLimit > fr.limit) textf(out, size, ts(TS_LINE_SPRITES_PAST), all, dropped);
        else if (ln.count > fr.limit) textf(out, size, ts(TS_LINE_SPRITES_DROPPED), all, dropped);
        else textf(out, size, ts(TS_LINE_SPRITES), all);
    }

    static void outputOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V99x8View* self = (V99x8View*)ref;
        const VdpFrameShot* o = self->outShot;
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1)), w = (float)self->outPic.w;
        ImVec2 top = xf.toScreen(0, 0), end = xf.toScreen(w, (float)self->outPic.h);

        dl->PushClipRect(xf.clipMin, xf.clipMax, true);
        if (self->outDim) dl->AddRectFilled(top, end, ui::WithAlpha(t.layer, 150));
        /* Paused within a frame: from the row the VDP has drawn to on, the frame before. */
        if (o != NULL && !o->ended) {
            int y = o->drawnRow + (o->drawnSamples > 0 ? 1 : 0);
            if (!self->outPicBorders) y -= self->outFirstLine(self->ovState, o) - o->displayOffset;
            if (y < 0) y = 0;
            if (y < self->outPic.h) {
                ImVec2 a = xf.toScreen(0, (float)y);
                hatch(dl, a, end, ui::WithAlpha(t.text, o->restMissing ? 90 : 50));
                dl->AddLine(a, ImVec2(end.x, a.y), t.caution, th * 2);
            }
        }
        /* Rows the sprite split has only roughly, or not at all. */
        const std::vector<uint8_t>& rough = self->outRowApprox;
        for (int y = 0, n = self->outLayer != SL_SHOWN ? (int)rough.size() : 0; y < n;) {
            int e = y;
            while (e < n && rough[e]) e++;
            if (e > y) hatch(dl, xf.toScreen(0, (float)y), xf.toScreen(w, (float)e), ui::WithAlpha(t.caution, 110));
            y = e > y ? e : y + 1;
        }
        std::vector<int> runs;
        const Focus& f = shell::shownFocus();
        self->pinRows(f, runs);
        for (size_t k = 0; k + 1 < runs.size(); k += 2) {
            ImVec2 a = xf.toScreen(0, (float)runs[k]), b = xf.toScreen(w, (float)runs[k + 1]);
            dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t.accent, 0, th * 2);
        }
        dl->PopClipRect();
        /* After the picture, so a box shows over any sprite; a pinned sprite alone without them. */
        if (self->outBoxes) self->outputBoxes(dl, xf, -1);
        else if (self->pinnedSpriteAttr(f) >= 0) self->outputBoxes(dl, xf, self->pinnedSpriteAttr(f));
        if (xf.gutterMax.x > xf.gutterMin.x) dl->AddRectFilled(xf.gutterMin, xf.gutterMax, t.controlFillPressed);
        if (self->outCounts && xf.gutterMax.x > xf.gutterMin.x) self->outputCountGutter(dl, xf);
        if (Sp3Panel::active(self->ovState)) self->sp3.screenOverlay(dl, xf, self->rasterMarks() ? ui::Px(12) : 0);
        self->drawRaster(dl, xf);
    }

    /* R#26/R#27 move a band's picture sideways: x then counts dots from the border, not VRAM columns. */
    static bool scrollsSideways(const VdpBand& b)
    {
        return (b.regs[26] & 0x3f) != 0 || (b.regs[27] & 7) != 0;
    }

    /* The status line for a sample of the drawn picture: the dot and line, and
    ** the VRAM it came from by the line's band, when the picture and the
    ** register log are of the same frame. */
    void outputHover(const VdpDebugState* s, const VdpFrameShot* o, Focus& f, const ui::CanvasHit& h)
    {
        int row = outPicBorders ? h.y : h.y + outFirstLine(s, o) - o->displayOffset;
        int line = row + o->displayOffset;
        int d = line - outFirstLine(s, o);
        int origin = outOrigin(s, o, d), x544 = outPicBorders ? h.x : h.x + origin;
        uint32_t c = outPic.img[(size_t)h.y * outPic.w + h.x];
        int shift = row >= 0 && row < o->rows && o->shift[row] ? 1 : 0;
        int dot = (x544 - origin) >> (1 - shift), dots = 256 << shift;
        if (textLine(s, d)) {
            dot -= 8 << shift;
            dots = 240 << shift;
        }
        if (d < 0 || d >= raster.lines || x544 < origin || dot < 0 || dot >= dots) {
            ui::StatusText(ts(TS_OUT_HOVER_BORDER), line, c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
            return;
        }
        int bi = vdpRasterBandAt(raster, d);
        const VdpBand* b = bi >= 0 ? &raster.bands[bi] : NULL;
        char head[256], where[160] = "", link[256] = "";
        textf(head, sizeof(head), ts(TS_OUT_HOVER), dot, b && scrollsSideways(*b) ? " \xE2\x89\x88" : "", d, line, c & 0xff,
              (c >> 8) & 0xff, (c >> 16) & 0xff);
        if (b == NULL) {
            ui::StatusText("%s", head);
            return;
        }
        if (raster.bands.size() > 1) textf(where, sizeof(where), ts(TS_STATUS_AT_BAND), b->first, b->last, shortModeName(b->mode));
        /* Paused: rows from drawnRow on are the frame before, but above the beam the log has this frame's registers. */
        int beamRow = raster.beamLine >= 0 ? raster.beamLine - o->displayOffset : o->rows;
        const char* why = NULL;
        if (o->stale) why = ts(TS_OUT_LINK_STALE);
        else if (raster.valid && o->frameStart != s->raster[0].frameStart) why = ts(TS_OUT_LINK_FRAME);
        else if (!o->ended && row >= o->drawnRow && row < beamRow) why = ts(TS_OUT_LINK_BEAM);
        else if (!bandDrawn(*b)) why = ts(TS_OUT_LINK_UNKNOWN);
        if (why) {
            ui::StatusText("%s%s%s", head, where, why);
            return;
        }
        VdpLayout l = vdpLayoutOf(*b);
        if (vdpIsBitmapMode(b->mode)) {
            VdpBitmapOptions bo;
            VdpPixelInfo px;
            vdpBandBitmap(s, *b, &bo);
            bo.color0 = color0();
            int x = dot * vdpBitmapWidth(b->mode) / dots;
            if (vdpBitmapPixel(s, l, bo, x, d, &px)) {
                Focus hit = { f.chipId, FOCUS_PHYS, (UInt32)px.physical, 1, FOCUS_PIXEL, x, d, false };
                pin(f, hit, h.clicked);
                textf(link, sizeof(link), ts(TS_OUT_LINK_PIXEL), px.logical, px.physical, px.value, px.value);
            }
        }
        else {
            VdpCharInfo ci;
            int x = dot * vdpNameTableWidth(b->mode) / dots;
            if (vdpNameTableChar(s, l, x, d, -1, &ci)) {
                Focus hit = { f.chipId, FOCUS_PHYS, (UInt32)ci.patternAddr, 8, FOCUS_TILE, x, d, false };
                pin(f, hit, h.clicked);
                textf(link, sizeof(link), ts(TS_OUT_LINK_TILE), ci.nameAddr, ci.ch & 0xff, ci.ch & 0xff, ci.patternAddr);
            }
        }
        ui::StatusText("%s%s%s", head, link, where);
    }

    void drawOutput(const VdpDebugState* s, Focus& f)
    {
        unsigned ver;
        const VdpFrameShot* o = (const VdpFrameShot*)shell::frameShot(&ver);
        if (o != NULL && !o->valid) o = NULL;
        shell::wantFrame();
        /* A mode without sprites has no sprite toggles: the choice waits for one. */
        int lw, lax, lay;
        bool hasSprites = spriteLayerShape(s, &lw, &lax, &lay);
        int layer = hasSprites ? sprLayer() : SL_SHOWN;
        if (layer != SL_SHOWN) shell::wantSpriteSplit();
        /* SP3 sprites have boxes and counts of their own, from the frame end. */
        bool sp3On = Sp3Panel::active(s), sp3Counts = sp3On && hasSprites && layer != SL_HIDDEN;
        outBoxes = boxes && hasSprites && !sp3On;
        outCounts = hasSprites && layer != SL_HIDDEN && !sp3On;
        ensureRaster(s);
        if (vdpSpriteMode(s->screenMode)) ensureSprites(s);
        outCv.aspectX = 1;
        outCv.aspectY = 2;   /* 544 samples a row: 512-dot units */
        outCv.gutter = outCounts || sp3Counts ? 40.0f : rasterMarks() ? 16.0f : 0.0f;
        outCv.checker = layer == SL_ONLY;

        ui::BeginCommandBar("bar");
        barToggle("##borders", UI_ICON_BORDERS, ts(TS_OUT_BORDERS), ts(TS_OUT_BORDERS_TIP), &outBorders);
        spriteToggles(s);
        ui::GridToggle(outCv);
        if (o != NULL && o->interlace != 0) {
            /* Paused within a frame, the field is that of the rows drawn so far. */
            const char* name = ts(o->interlace == 1 ? TS_OUT_FIELD_ODD : TS_OUT_FIELD_EVEN);
            bool mixed = !o->ended && (o->drawnRow > 0 || o->drawnSamples > 0);
            char field[128];
            textf(field, sizeof(field), mixed ? ts(TS_OUT_FIELD_ABOVE) : "%s", name);
            barText(field);
        }
        zoomGroup(outCv, false);
        ui::EndCommandBar();

        /* Without a new picture the last one stays, dimmed; one copied for another tab has no samples. */
        if (o != NULL && o->pixels) {
            char key[64];
            /* The crop follows the bands' modes, which come with the raster log. */
            snprintf(key, sizeof(key), "o%d/%d/%d/%u/%d/%d", outBorders, raster.lines, s->screenMode, outBorders ? 0u : shell::rasterVersion(),
                     outBorders ? 0 : outFirstLine(s, o), layer);
            bool decoded = outPic.needsDecodeAt(ver, key);
            if (decoded) {
                decodeOutput(s, o, layer);
                outLayer = layer;
            }
            outPic.upload(decoded);
            outPicBorders = outBorders;
            outLine0 = outBorders ? outFirstLine(s, o) - o->displayOffset : 0;
        }
        outShot = o;
        outDim = o == NULL || o->stale;
        ovState = s;
        ovRows = outPic.h;
        ovWidth = outPic.w > 0 ? outPic.w : VDP_SHOT_WIDTH;
        int ax, ay;
        dotAspect(s->screenMode, &ax, &ay);
        ovSplitWidth = ay == 2 ? 512 : 256;
        if (sp3On) sp3Lines(s, o, sp3Counts);

        ui::CanvasHit h = ui::Canvas("output", outPic.tex.id(), outPic.w, outPic.h, outCv, ImVec2(0, 0), outputOverlay, this);
        if (h.clickedEmpty) shell::releaseSelection();
        /* The status line says when the frame is missing, paused or stale. */
        if (o == NULL) {
            ui::StatusText("%s", ts(TS_OUT_NONE));
        }
        else {
            const char* paused = o->ended ? "" : ts(o->restMissing ? TS_OUT_PAUSED_UNKNOWN : TS_OUT_PAUSED);
            if (paused[0] || o->stale) ui::StatusText("%s%s", paused, o->stale ? ts(TS_OUT_STALE) : "");
        }
        refreshPin(s, f);
        /* The notes name what is on: the boxes, the per-line counts or both. */
        bool boxed = outBoxes || pinnedSpriteAttr(f) >= 0, boxing = boxed || outCounts;
        if (boxing && (o == NULL || o->sprTabCount == 0))
            ui::StatusText("%s", ts(!outCounts ? TS_OUT_BOXES_WAIT : boxed ? TS_OUT_BOXES_COUNTS_WAIT : TS_OUT_COUNTS_WAIT));
        else if (boxing && o->sprTabLost > 0)
            ui::StatusText("%s", ts(!outCounts ? TS_OUT_BOXES_LOST : boxed ? TS_OUT_BOXES_COUNTS_LOST : TS_OUT_COUNTS_LOST));

        /* The marks own the gutter's right edge; the sprite counts the rest. */
        char mt[1024];
        bool onMarks = h.gutterY >= 0 && rasterMarks() && ImGui::GetIO().MousePos.x >= ovMarkX;
        int d = h.gutterY >= 0 ? displayLineOfRow(h.gutterY) : -1, st;
        const VdpSpriteFrame* gf = d >= 0 && outCounts ? rowFrame(s, o, d, &st) : NULL;
        outHoverAttr = -1;
        if (onMarks && marksAtRow(h.gutterY, mt, sizeof(mt))) {
            ui::StatusText("%s", mt);
        }
        else if (gf != NULL) {
            lineSummary(*gf, d + gf->vscroll, drawLimitOf(*gf), mt, sizeof(mt));
            ui::StatusText(ts(TS_GUTTER_LINE), d, mt);
        }
        else if (!(sp3On && sp3.screenHover(s, f, h)) && h.valid && o != NULL && outPic.w > 0 && !(outBoxes && outputSpriteHover(s, f, h))) {
            outputHover(s, o, f, h);
        }
    }

    /* Each display line's image row, left edge and band for the SP3 boxes; no band where
    ** the picture is not of the snapshot's frame, or is the frame before below the beam. */
    void sp3Lines(const VdpDebugState* s, const VdpFrameShot* o, bool counts)
    {
        sp3.screenBegin(s, boxes, counts);
        bool same = o != NULL && !o->stale && (!raster.valid || o->frameStart == s->raster[0].frameStart);
        for (int d = 0; same && d < raster.lines; d++) {
            int bi = vdpRasterBandAt(raster, d), shot = d + outFirstLine(s, o) - o->displayOffset;
            bool drawn = bi >= 0 && (o->ended || shot < o->drawnRow);
            sp3.screenLine(d, rowOfDisplayLine(d), outPicBorders ? outOrigin(s, o, d) : 0, drawn ? &raster.bands[bi] : NULL);
        }
    }

    /* A sprite's box under the mouse: its attributes on the status line, a click pins it. */
    bool outputSpriteHover(const VdpDebugState* s, Focus& f, const ui::CanvasHit& h)
    {
        const VdpSpriteFrame* fr;
        int L, n = outputSpriteAt(h.x, h.y, &fr, &L);
        if (n < 0) return false;
        const VdpSprite& sp = fr->spr[n];
        Focus hit = { f.chipId, FOCUS_LOGICAL, (UInt32)sp.attrAddr, 4, FOCUS_SPRITE, n, L, false };
        focusAddr(s, fr->screenMode, sp.attrAddr, 4, &hit);
        pin(f, hit, h.clicked);
        if (h.clicked) {
            sprPinned = f.pinned;
            sprPinAttr = sp.attrAddr;
            sprPinLine = displayLineOfRow(h.y);
            if (f.pinned) sprChoose(frameKey(s, *fr));
        }
        outHoverAttr = sp.attrAddr;
        int k = 0;
        while (k < fr->line[L].count && fr->line[L].id[k] != n) k++;
        int st = vdpSpriteStateAt(*fr, L, k, drawLimitOf(*fr));
        ui::StatusText(ts(TS_STATUS_SPRITE), n, sp.y, sp.x, sp.pattern, vdpSpriteRow(*fr, n, L), sp.attrAddr,
                       st == VDP_SPRITE_DROPPED ? ts(TS_STATUS_SPRITE_DROPPED) : st == VDP_SPRITE_CC_HIDDEN ? ts(TS_STATUS_SPRITE_CC_HIDDEN) : "");
        return true;
    }

    /* ---- Bands ---- */

    static bool bandDrawn(const VdpBand& b)
    {
        return b.mode >= 0 && !b.unknown;
    }

    /* b only goes on from a below the beam: from the frame before, or with no frame before logged. */
    static bool sameAcrossBeam(const VdpBand& a, const VdpBand& b)
    {
        VdpBand c = b;
        c.prev = a.prev;
        c.unknown = a.unknown;
        return vdpRasterSameKey(a, c);
    }

    /* As sameAcrossBeam, but b may also differ from a in the sprite registers. */
    static bool spriteOnlyAcrossBeam(const VdpBand& a, const VdpBand& b)
    {
        VdpBand c = b;
        c.prev = a.prev;
        c.unknown = a.unknown;
        return spriteOnlySplit(a, c);
    }

    /* The bands the frame is split into, a band going on across the beam counted once. */
    int splitBands() const
    {
        int n = 0;
        for (size_t i = 0; i < raster.bands.size(); i++) {
            if (i == 0 || !sameAcrossBeam(raster.bands[i - 1], raster.bands[i])) n++;
        }
        return n;
    }

    /* ---- Page strip ---- */

    enum { SI_PAGE, SI_NAME, SI_PATGEN, SI_SPRPAT, SI_SPRITES };

    /* One thumbnail: rows v0..v1 of a picture, w x h dots shaped ax:ay. */
    struct StripItem {
        int          kind, group, page;
        ViewPicture* pic;
        int          v0, v1;
        int          w, h, ax, ay;
        int          addr, len;   /* logical; len 0 for the sprite layer */
        unsigned     also;        /* bit k: slot k shows the same table, drawn here once */
    };

    /* What one of a group's pictures was decoded from, VRAM aside. */
    struct StripKey {
        int    mode, color0, blink, blocks;
        UInt8  regs[32];         /* only those the picture reads, the rest 0 */
        UInt32 palette[16];
        UInt32 extHash;          /* EPAL's 256 colours, hashed, while a bitmap reads them */
    };
    enum { SP_FIRST, SP_PATTERNS, SP_SPRITES, SP_COUNT };  /* a group's pictures: pages or names first */

    /* A group's thumbnails, decoded with the registers of its first band. A slot holds its group while a
    ** game flips it away: pictures since it was in one, first-seen order, the lines it covered in each of
    ** the last pictures and their union as its heading, and the sprite table its first line last used. */
    struct StripGroup : VdpBandGroup {
        StripKey       decoded[SP_COUNT], key[SP_COUNT];
        bool           hasDecoded[SP_COUNT] = {};
        ViewPicture    pages, name, sheet, sprSheet;
        bool           used = false, sprKeySet = false;
        int            age = 0, order = 0, ringLen = 0, ringPos = 0;
        int            seen = 0;              /* the pictures it was in, counted up to STRIP_BRIEF + 1 */
        uint32_t       lineRing[SPR_AWAY_SHOWN][8] = {}, heldMask[8] = {};
        char           heldLines[64] = "";
        SprKey         sprKey = {};
    };

    struct StripRow { int first, count; float h; };

    /* A slot that left; its group back within SPR_SEEN_KEEP frames takes its old place, held for good. */
    struct StripGone { VdpBandGroup g; UInt32 seq; int order; bool used = false; };

    StripGroup             stripGroups[STRIP_GROUPS];
    StripGone              stripGone[4];
    VdpSpriteFrame*        bandFrame = NULL;
    std::vector<StripItem> stripItems;
    std::vector<StripRow>  stripRows;
    std::vector<int>       stripRuns;
    VdpImage               stripPage;
    std::vector<UInt8>     stripPrev;              /* the VRAM the pictures were decoded from */
    bool                   stripPrevValid = false;
    unsigned               stripDecodedVer = ~0u, stripDecodedRaster = ~0u, stripDecodedPal = ~0u;
    int                    stripDecodedC0 = -1;

    void stripForget()
    {
        for (StripGroup& g : stripGroups) {
            g.pages.forget();
            g.name.forget();
            g.sheet.forget();
            g.sprSheet.forget();
            memset(g.hasDecoded, 0, sizeof(g.hasDecoded));
            g.used = false;
        }
        for (StripGone& e : stripGone) e.used = false;
        spriteLayer.forget();
        stripHoldGen = ~0u;
        stripPrevValid = false;
        stripDecodedVer = stripDecodedRaster = ~0u;
    }

    /* The first band of found group g, by the key that grouped it. */
    const VdpBand* firstBandOf(const VdpBandGroup& g) const
    {
        for (const VdpBand& b : raster.bands) {
            if (b.mode >= 0 && vdpGroupHasBand(g, b)) return &b;
        }
        return NULL;
    }

    /* Notes a slot as it goes, in place of the oldest note. */
    void stripRememberGone(const StripGroup& g, UInt32 seq)
    {
        StripGone* e = &stripGone[0];
        for (StripGone& x : stripGone) {
            if (!x.used || (e->used && x.seq < e->seq)) e = &x;
        }
        *e = StripGone{ g, seq, g.order, true };
    }

    /* The strip's slots held through a game's table flips, once a picture: a group found again by its
    ** grouping key keeps its slot; one in none of the last SPR_AWAY_SHOWN pictures leaves, its place kept
    ** SPR_SEEN_KEEP frames in case it comes back. Aged once a picture, folded or unseen too. */
    void stripHold()
    {
        unsigned ver;
        const VdpFrameShot* o = (const VdpFrameShot*)shell::frameShot(&ver);
        UInt32 seq = o != NULL && o->valid ? o->seq : stripHoldSeq;
        bool back = (Int32)(seq - stripHoldSeq) < 0;
        if (seq == stripHoldSeq && rasterGen == stripHoldGen) return;
        /* After a jump past SPR_SEEN_KEEP frames (a state loaded ahead) a slot not in this picture leaves. */
        int step = back || seq == stripHoldSeq ? 0 : seq - stripHoldSeq > (UInt32)SPR_SEEN_KEEP ? SPR_AWAY_SHOWN + 1 : 1;
        stripHoldSeq = seq;
        stripHoldGen = rasterGen;
        for (StripGone& e : stripGone) {
            if (back || seq - e.seq > (UInt32)SPR_SEEN_KEEP) e.used = false;
        }
        for (StripGroup& g : stripGroups) {
            if (back || !raster.valid) g.used = false;
            if (!g.used) continue;
            g.age += step;
            for (int i = 0; i < step && i < SPR_AWAY_SHOWN; i++) g.ringPos = (g.ringPos + 1) % SPR_AWAY_SHOWN;
            for (int i = 0; i < step && i < SPR_AWAY_SHOWN; i++) memset(g.lineRing[(g.ringPos - i + SPR_AWAY_SHOWN) % SPR_AWAY_SHOWN], 0, sizeof(g.lineRing[0]));
            g.ringLen = g.ringLen + step < SPR_AWAY_SHOWN ? g.ringLen + step : SPR_AWAY_SHOWN;
            memset(g.lineRing[g.ringPos], 0, sizeof(g.lineRing[0]));
        }
        if (!raster.valid) return;
        /* Every found group to its slot first; then the new ones to a free slot, else to the one gone
        ** longest, one seen briefly first. */
        int nf = (int)groups.size() < 8 ? (int)groups.size() : 8, at[8];
        bool taken[STRIP_GROUPS] = {};
        for (int i = 0; i < nf; i++) {
            const VdpBand* b = firstBandOf(groups[i]);
            at[i] = -1;
            for (int k = 0; k < STRIP_GROUPS && at[i] < 0 && b != NULL; k++) {
                if (stripGroups[k].used && !taken[k] && vdpGroupHasBand(stripGroups[k], *b)) at[i] = k;
            }
            if (at[i] >= 0) taken[at[i]] = true;
            if (at[i] >= 0 && step > 0 && stripGroups[at[i]].seen <= STRIP_BRIEF) stripGroups[at[i]].seen++;
        }
        /* In none of the last SPR_AWAY_SHOWN pictures nor in this one: it leaves, remembered in case it comes back. */
        for (int k = 0; k < STRIP_GROUPS; k++) {
            StripGroup& g = stripGroups[k];
            if (!g.used || taken[k] || g.age <= SPR_AWAY_SHOWN) continue;
            stripRememberGone(g, seq);
            g.used = false;
            memset(g.hasDecoded, 0, sizeof(g.hasDecoded));
        }
        for (int i = 0; i < nf; i++) {
            if (at[i] >= 0) continue;
            int k = -1;
            for (int j = 0; j < STRIP_GROUPS && k < 0; j++) {
                if (!stripGroups[j].used) k = j;
            }
            /* Never one found in this picture: groups past the slots are left out. */
            for (int j = 0; j < STRIP_GROUPS && (k < 0 || stripGroups[k].used); j++) {
                const StripGroup& g = stripGroups[j];
                if (taken[j] || g.age <= 0) continue;
                bool brief = g.seen <= STRIP_BRIEF, kBrief = k >= 0 && stripGroups[k].seen <= STRIP_BRIEF;
                if (k < 0 || (brief && !kBrief) || (brief == kBrief && g.age > stripGroups[k].age)) k = j;
            }
            if (k < 0) continue;
            StripGroup& g = stripGroups[k];
            if (g.used) stripRememberGone(g, seq);
            g.used = true;
            memset(g.hasDecoded, 0, sizeof(g.hasDecoded));
            g.sprKeySet = false;
            g.order = stripOrderNext++;
            g.seen = 1;
            const VdpBand* b = firstBandOf(groups[i]);
            for (StripGone& e : stripGone) {
                if (!e.used || b == NULL || !vdpGroupHasBand(e.g, *b)) continue;
                g.order = e.order;
                g.seen = STRIP_BRIEF + 1;
                e.used = false;
                break;
            }
            g.ringLen = 1;
            g.ringPos = 0;
            memset(g.lineRing, 0, sizeof(g.lineRing));
            memset(g.heldMask, 0, sizeof(g.heldMask));
            g.heldLines[0] = 0;
            at[i] = k;
            taken[k] = true;
        }
        for (int i = 0; i < nf; i++) {
            if (at[i] < 0) continue;
            StripGroup& g = stripGroups[at[i]];
            static_cast<VdpBandGroup&>(g) = groups[i];
            g.age = 0;
            for (int r = 0; r < g.runs; r++) {
                for (int d = g.runFirst[r]; d <= g.runLast[r] && d < 256; d++) g.lineRing[g.ringPos][d >> 5] |= 1u << (d & 31);
            }
            int d = g.runs > 0 ? g.runFirst[0] : -1;
            if (d >= 0 && d < (int)scene.lineSlot.size() && scene.lineSlot[d] >= 0) {
                g.sprKey = slotKey(scene.slots[scene.lineSlot[d]]);
                g.sprKeySet = true;
            }
        }
        /* The heading: the lines of the last pictures, written again only when they change. */
        for (StripGroup& g : stripGroups) {
            uint32_t m[8] = {};
            for (int i = 0; g.used && i < g.ringLen; i++) {
                for (int w = 0; w < 8; w++) m[w] |= g.lineRing[(g.ringPos - i + SPR_AWAY_SHOWN) % SPR_AWAY_SHOWN][w];
            }
            /* Gone all those pictures: the lines it last covered stay. */
            static const uint32_t none[8] = {};
            if (!g.used || memcmp(m, none, sizeof(m)) == 0 || memcmp(m, g.heldMask, sizeof(m)) == 0) continue;
            memcpy(g.heldMask, m, sizeof(m));
            size_t at2 = 0;
            g.heldLines[0] = 0;
            for (int d = 0; d < 256 && at2 < sizeof(g.heldLines); d++) {
                if (!((m[d >> 5] >> (d & 31)) & 1) || (d > 0 && ((m[(d - 1) >> 5] >> ((d - 1) & 31)) & 1))) continue;
                int e = d;
                while (e + 1 < 256 && ((m[(e + 1) >> 5] >> ((e + 1) & 31)) & 1)) e++;
                const char* sep = at2 ? ", " : "";
                if (e == d) at2 += (size_t)snprintf(g.heldLines + at2, sizeof(g.heldLines) - at2, "%s%d", sep, d);
                else at2 += (size_t)snprintf(g.heldLines + at2, sizeof(g.heldLines) - at2, "%s%d-%d", sep, d, e);
            }
        }
    }

    /* The slots to show, in first-seen order; without a log, one from the frame's end. */
    void stripGroupsFor(const VdpDebugState* s)
    {
        stripGroupCount = 0;
        if (!raster.valid) {
            stripOrder[stripGroupCount] = 0;
            StripGroup& g = stripGroups[stripGroupCount++];
            VdpBitmapOptions o = {};
            memset(static_cast<VdpBandGroup*>(&g), 0, sizeof(VdpBandGroup));
            g.mode = s->screenMode;
            memcpy(g.regs, s->regs, sizeof(g.regs));
            g.blink = s->blinkFlag;
            g.lineCount = screenLines(s);
            g.age = 0;
            snprintf(g.lines, sizeof(g.lines), "0-%d", g.lineCount - 1);
            g.shownPages = vdpDisplayedBitmap(s, vdpLayoutOf(s), &o) ? vdpShownPageBits(vdpIsFlatInterlace(s, s->regs), o.page) : 0;
            snprintf(g.heldLines, sizeof(g.heldLines), "%s", g.lines);
            stripMixed = false;
            return;
        }
        stripGroupCount = heldSlots(stripOrder);
        stripMixed = stripGroupCount >= 2;
    }

    /* The Patterns tab's pick: its grouping key, kept to find its slot again after the slot is dropped. */
    void notePick(const VdpBandGroup& g, int order)
    {
        patPickOrder = order;
        memset(&patPickBand, 0, sizeof(patPickBand));
        patPickBand.mode = g.mode;
        memcpy(patPickBand.regs, g.regs, sizeof(patPickBand.regs));
    }

    /* The held slot of the pick's key, or -1; the list then marks that slot's order. */
    int pickedSlot()
    {
        for (int k = 0; k < STRIP_GROUPS; k++) {
            if (!stripGroups[k].used || !vdpGroupHasBand(stripGroups[k], patPickBand)) continue;
            patPickOrder = stripGroups[k].order;
            return k;
        }
        return -1;
    }

    /* The held slots in first-seen order into out; how many. */
    int heldSlots(int* out) const
    {
        int count = 0;
        for (int k = 0; k < STRIP_GROUPS; k++) {
            if (!stripGroups[k].used) continue;
            int i = count++;
            while (i > 0 && stripGroups[out[i - 1]].order > stripGroups[k].order) {
                out[i] = out[i - 1];
                i--;
            }
            out[i] = k;
        }
        return count;
    }

    bool bytesChanged(const VdpDebugState* s, int phys, int len) const
    {
        if (phys < 0 || phys >= VDP_DEBUG_VRAM_SIZE || len <= 0) return false;
        if (phys + len > VDP_DEBUG_VRAM_SIZE) len = VDP_DEBUG_VRAM_SIZE - phys;
        return memcmp(&stripPrev[phys], &s->vram[phys], (size_t)len) != 0;
    }

    /* A logical range differs from the VRAM the pictures were decoded from. */
    bool rangeChanged(const VdpDebugState* s, int mode, int addr, int len) const
    {
        if (!stripPrevValid) return true;
        if (interleaved(s, mode)) {
            int half = (len + 1) / 2;
            return bytesChanged(s, vdpPhysicalAddress(s, mode, addr & ~1), half) ||
                   bytesChanged(s, vdpPhysicalAddress(s, mode, addr | 1), half);
        }
        return bytesChanged(s, vdpPhysicalAddress(s, mode, addr), len);
    }

    /* What picture p of a group reads besides VRAM: pages, the registers they always did; names, their
    ** tables, colours, scroll and height; patterns, their tables and colours; sprite patterns, theirs and the size. */
    void stripKeyOf(const VdpDebugState* s, const VdpLayout& l, int p, StripKey* k) const
    {
        static const int pages[] = { 2, 3, 4, 6, 7, 10, 12, 13 }, names[] = { 2, 3, 4, 7, 10, 12, 13, 25 };
        static const int patterns[] = { 3, 4, 7, 10 };
        bool bitmap = vdpIsBitmapMode(l.mode);
        memset(k, 0, sizeof(*k));
        k->mode = l.mode;
        if (!bitmap && p == SP_SPRITES) {
            k->regs[6] = l.regs[6];
            k->regs[1] = l.regs[1] & 0x02;
            return;
        }
        k->color0 = color0();
        if (bitmap) {
            for (int reg : pages) k->regs[reg] = l.regs[reg];
            k->regs[1] = l.regs[1] & 0x03;
            k->regs[20] = l.regs[20] & 0x18;   /* a V9968's SP3 lays the pages out in order; EPAL */
            if (l.mode == 8 || l.mode == 10) {
                k->extHash = fnvWords(s->paletteExt, (size_t)s->paletteExtCount);
            }
        }
        else if (p == SP_FIRST) {
            for (int reg : names) k->regs[reg] = l.regs[reg];
            k->regs[23] = (UInt8)vdpScreenScroll(s, l);
            k->regs[9] = l.regs[9] & 0x80;
            k->blink = l.blink;
        }
        else {
            for (int reg : patterns) k->regs[reg] = l.regs[reg];
            k->blocks = vdpSheetBlocks(s, l);
        }
        k->regs[8] = l.regs[8] & 0x20;
        memcpy(k->palette, l.rgb ? l.rgb : s->paletteRgb, sizeof(k->palette));
    }

    /* Whether a thumbnail of this kind is on the strip already from a slot whose picture p reads the
    ** same table with the same registers, whatever palette its lines had; if so, notes slot k there. */
    bool stripShownEarlier(int kind, int p, const StripKey& key, int k)
    {
        bool any = false;
        for (StripItem& e : stripItems) {
            if (e.kind != kind || e.group >= STRIP_GROUPS || memcmp(&stripGroups[e.group].key[p], &key, offsetof(StripKey, palette)) != 0) continue;
            e.also |= 1u << k;
            any = true;
        }
        return any;
    }

    /* The thumbnails, a table that slots share drawn once under the first; when shown, the pictures
    ** not decoded yet, or whose VRAM or registers changed, are decoded again. */
    void stripItemsFor(const VdpDebugState* s, bool decode)
    {
        int c0 = color0();
        bool shown = decode;
        bool fresh = decode && (stripDecodedVer != shell::pictureVersion() || stripDecodedRaster != shell::rasterVersion() ||
                                stripDecodedC0 != c0 || stripDecodedPal != linePal.ver);
        stripItems.clear();
        for (int i = 0; i < stripGroupCount; i++) {
            int k = stripOrder[i];
            StripGroup& g = stripGroups[k];
            VdpLayout gl = withPal(vdpLayoutOf(g), palOfGroup(g));
            int mode = g.mode, ax, ay;
            dotAspect(mode, &ax, &ay);
            for (int p = 0; p < SP_COUNT; p++) stripKeyOf(s, gl, p, &g.key[p]);
            /* A picture this mode has none of goes stale while unused: decoded again when it comes back. */
            int has = vdpIsBitmapMode(mode) ? 1 : vdpSpriteMode(mode) ? SP_COUNT : SP_SPRITES;
            for (int p = has; p < SP_COUNT; p++) g.hasDecoded[p] = false;
            if (vdpIsBitmapMode(mode)) {
                /* One slot a bitmap mode, so its pages are never on the strip twice. */
                int pages = vdpBitmapPageCount(s, mode), w = vdpBitmapWidth(mode);
                int bytes = mode == 5 || mode == 6 ? 0x8000 : 0x10000;
                if (shown && (fresh || !g.hasDecoded[SP_FIRST])) {
                    bool any = false;
                    bool full = !g.hasDecoded[SP_FIRST] || memcmp(&g.key[SP_FIRST], &g.decoded[SP_FIRST], sizeof(StripKey)) != 0;
                    if (g.pages.w != w || g.pages.h != pages * 256) {
                        g.pages.w = w;
                        g.pages.h = pages * 256;
                        g.pages.img.assign((size_t)w * pages * 256, 0);
                        full = true;
                    }
                    for (int p = 0; p < pages; p++) {
                        if (!full && !rangeChanged(s, mode, p * bytes, bytes)) continue;
                        VdpBitmapOptions o = { mode, p, 256, 0, c0, false, false };
                        int pw, ph;
                        vdpDecodeBitmap(s, gl, o, stripPage, &pw, &ph);
                        memcpy(&g.pages.img[(size_t)p * 256 * w], stripPage.data(), (size_t)w * 256 * sizeof(uint32_t));
                        any = true;
                    }
                    g.pages.upload(any);
                    g.decoded[SP_FIRST] = g.key[SP_FIRST];
                    g.hasDecoded[SP_FIRST] = true;
                }
                for (int p = 0; p < pages; p++) {
                    stripItems.push_back(StripItem{ SI_PAGE, k, p, &g.pages, p * 256, p * 256 + 256, w, 256, ax, ay, p * bytes, bytes });
                }
                continue;
            }
            static const int kinds[SP_COUNT] = { SI_NAME, SI_PATGEN, SI_SPRPAT };
            int parts = vdpSpriteMode(mode) ? SP_COUNT : SP_SPRITES;
            bool dup[SP_COUNT] = {}, dec[SP_COUNT] = {}, changed[SP_COUNT] = {};
            for (int p = 0; p < parts; p++) {
                dup[p] = stripShownEarlier(kinds[p], p, g.key[p], k);
                if (dup[p]) g.hasDecoded[p] = false;
                dec[p] = !dup[p] && shown && (fresh || !g.hasDecoded[p]);
                changed[p] = !g.hasDecoded[p] || memcmp(&g.key[p], &g.decoded[p], sizeof(StripKey)) != 0;
            }
            VdpRange r[8];
            int n = vdpTableRanges(s, gl, r), nameAt = 0, nameLen = 0, patAt = 0, patLen = 0, sprAt = 0, sprLen = 0;
            for (int i = 0; i < n; i++) {
                if (r[i].kind == VDP_RANGE_NAME) { nameAt = r[i].addr; nameLen = r[i].len; }
                if (r[i].kind == VDP_RANGE_PATTERN) { patAt = r[i].addr; patLen = r[i].len; }
                if (r[i].kind == VDP_RANGE_SPRITE_PATTERN) { sprAt = r[i].addr; sprLen = r[i].len; }
                if (r[i].kind == VDP_RANGE_SPRITE_ATTR || r[i].kind == VDP_RANGE_SPRITE_COLOR) continue;
                /* Names read the name, pattern and colour tables; patterns the last two; sprite patterns their own. */
                bool spr = r[i].kind == VDP_RANGE_SPRITE_PATTERN, nm = r[i].kind == VDP_RANGE_NAME;
                bool ask = spr ? dec[SP_SPRITES] && !changed[SP_SPRITES]
                               : (dec[SP_FIRST] && !changed[SP_FIRST]) || (!nm && dec[SP_PATTERNS] && !changed[SP_PATTERNS]);
                if (!ask || !rangeChanged(s, mode, r[i].addr, r[i].len)) continue;
                changed[spr ? SP_SPRITES : SP_FIRST] = true;
                if (!spr && !nm) changed[SP_PATTERNS] = true;
            }
            int lines = layoutLines(s, g.regs), w = vdpNameTableWidth(mode), size = (g.regs[1] & 2) ? 16 : 8;
            if (dec[SP_FIRST] && changed[SP_FIRST]) vdpDecodeNameTable(s, gl, lines, -1, c0, g.name.img, &g.name.w, &g.name.h);
            if (dec[SP_PATTERNS] && changed[SP_PATTERNS]) vdpDecodePatternSheet(s, gl, c0, g.sheet.img, &g.sheet.w, &g.sheet.h);
            if (dec[SP_SPRITES] && changed[SP_SPRITES]) vdpDecodeSpriteSheet(s, gl, size, g.sprSheet.img, &g.sprSheet.w, &g.sprSheet.h);
            ViewPicture* pics[SP_COUNT] = { &g.name, &g.sheet, &g.sprSheet };
            for (int p = 0; p < parts; p++) {
                if (!dec[p]) continue;
                pics[p]->upload(changed[p]);
                g.decoded[p] = g.key[p];
                g.hasDecoded[p] = true;
            }
            int sheetH = 64 * vdpSheetBlocks(s, gl);
            if (!dup[SP_FIRST]) stripItems.push_back(StripItem{ SI_NAME, k, -1, &g.name, 0, lines, w, lines, ax, ay, nameAt, nameLen });
            if (!dup[SP_PATTERNS]) stripItems.push_back(StripItem{ SI_PATGEN, k, -1, &g.sheet, 0, sheetH, 256, sheetH, 1, 1, patAt, patLen });
            if (parts == SP_COUNT && !dup[SP_SPRITES]) {
                stripItems.push_back(StripItem{ SI_SPRPAT, k, -1, &g.sprSheet, 0, 64, 256, 64, 1, 1, sprAt, sprLen });
            }
        }
        int layerW = 0, ax = 1, ay = 1;
        if (spriteLayerShape(s, &layerW, &ax, &ay)) {
            int lines = screenLines(s);
            /* Keyed by itself: the tables each line used come with every picture. */
            if (shown) ensureSpriteLayer(s);
            stripItems.push_back(StripItem{ SI_SPRITES, STRIP_GROUPS, -1, &spriteLayer, 0, lines, layerW, lines, ax, ay, 0, 0 });
        }
        if (fresh) {
            stripDecodedVer = shell::pictureVersion();
            stripDecodedRaster = shell::rasterVersion();
            stripDecodedC0 = c0;
            stripDecodedPal = linePal.ver;
            stripPrev.assign(s->vram, s->vram + VDP_DEBUG_VRAM_SIZE);
            stripPrevValid = true;
        }
    }

    /* Whether any band has sprites, and the layer's width: 512 when a band is in SCREEN 6 or 7. */
    bool spriteLayerShape(const VdpDebugState* s, int* w, int* ax, int* ay) const
    {
        bool any = false;
        int wide = 0;
        auto note = [&](int mode) {
            if (vdpSpriteMode(mode) == 0) return;
            any = true;
            if (mode == 6 || mode == 7) wide = mode;
        };
        /* SP3 places its sprites in 256 dots whatever the mode. */
        if (Sp3Panel::active(s)) {
            *w = 256;
            dotAspect(5, ax, ay);
            return true;
        }
        if (raster.valid) {
            for (const VdpBand& b : raster.bands) note(b.mode);
        }
        else {
            note(s->screenMode);
        }
        *w = wide ? 512 : 256;
        dotAspect(wide ? wide : 5, ax, ay);
        return any;
    }

    /* The band whose registers lay out the sprites of display line d: the VDP builds them a line ahead. */
    int spriteBandOf(int d) const
    {
        return vdpRasterBandAt(raster, d > 0 ? d - 1 : 0);
    }

    /* The registers sprites are laid out from: mode, size, tables, TP and R#23. Blanking
    ** and SPD go line by line (spritesOff). */
    static void spriteKey(const VdpBand& b, int out[8])
    {
        out[0] = b.mode;
        out[1] = b.regs[1] & 0x03;
        out[2] = b.regs[5];
        out[3] = b.regs[6];
        out[4] = b.regs[8] & 0x20;
        out[5] = b.regs[11];
        out[6] = b.regs[23];
        out[7] = 0;
    }

    /* Bands that lay sprites out from different registers. */
    bool spriteBandsDiffer() const
    {
        int a[8], b[8];
        for (size_t i = 1; raster.valid && i < raster.bands.size(); i++) {
            spriteKey(raster.bands[0], a);
            spriteKey(raster.bands[i], b);
            if (memcmp(a, b, sizeof(a)) != 0) return true;
        }
        return false;
    }

    /* The sprite layer of the display lines, for the strip; false
    ** when no band has sprites. Drawn again whenever VRAM, the registers, the
    ** bands or the options change, so it keeps no copy of VRAM to compare. */
    bool ensureSpriteLayer(const VdpDebugState* s)
    {
        int w, ax, ay;
        if (!spriteLayerShape(s, &w, &ax, &ay)) return false;
        ensureRaster(s);
        bool sp3On = Sp3Panel::active(s);
        if (vdpSpriteMode(s->screenMode)) ensureSprites(s);
        char key[96];
        ensureScene(s);
        /* SP3 reads VRAM at the frame end, which a write while paused changes. */
        snprintf(key, sizeof(key), "L%u/%d/%d/%d/%d/%u/%u/%u", shell::rasterVersion(), w, screenLines(s), vdpGetSpritesEnable(),
                 vdpGetNoSpritesLimit(), linePal.ver, scene.recorded ? scene.ver : 0u, sp3On ? shell::pictureVersion() : 0u);
        bool render = spriteLayer.needsDecode(key);
        if (render) renderSpriteBands(s, spriteLayer);
        spriteLayer.upload(render);
        return true;
    }

    /* The sprite layer, each display line laid out with the registers of the band the VDP built it in. */
    void renderSpriteBands(const VdpDebugState* s, ViewPicture& pic)
    {
        int lines = screenLines(s), w, ax, ay;
        spriteLayerShape(s, &w, &ax, &ay);
        if (pic.w != w || pic.h != lines) pic.img.assign((size_t)w * lines, 0);
        pic.w = w;
        pic.h = lines;
        if (bandFrame == NULL) bandFrame = new VdpSpriteFrame;
        if (!vdpGetSpritesEnable() || lines == 0) {
            std::fill(pic.img.begin(), pic.img.end(), 0u);
            return;
        }
        int limit = vdpGetNoSpritesLimit() ? 32 : 0;
        if (Sp3Panel::active(s)) {
            /* SP3 lays them out from its own tables as the Screen tab does; a line whose own
            ** band has SP3 off is cleared, so one that had sprites shows if the frame ends off. */
            const char* offAtEnd = NULL;
            if (!sp3.layer(s, lines, limit != 0, pic.img.data(), &offAtEnd)) {
                std::fill(pic.img.begin(), pic.img.end(), 0u);
                return;
            }
            for (int d = 0; d < lines; d++) {
                int bi = raster.valid ? vdpRasterBandAt(raster, d) : -1;
                const VdpBand* b = bi >= 0 ? &raster.bands[bi] : NULL;
                bool off = raster.valid ? b == NULL || b->prev || b->unknown ||
                                              sp3LineState(s->regs, b->mode, b->regs) == SP3_LINE_OFF
                                        : offAtEnd != NULL;
                if (off) memset(&pic.img[(size_t)d * w], 0, (size_t)w * sizeof(uint32_t));
            }
            return;
        }
        std::vector<const UInt32*> rgb((size_t)lines);
        for (int d = 0; d < lines; d++) rgb[d] = lineRgb(d);
        if (scene.recorded && scene.lines == lines) {
            /* Each line from the table it was laid out from, as Output's boxes. */
            for (int d = 0; d < lines;) {
                int fi = scene.lineFrame[d], e = d;
                bool shown = fi >= 0 && !scene.lineOff[d] && scene.lineState[d] != SS_NONE && scene.lineState[d] != SS_LOST;
                while (e + 1 < lines && scene.lineFrame[e + 1] == fi && scene.lineOff[e + 1] == scene.lineOff[d] &&
                       scene.lineState[e + 1] == scene.lineState[d]) e++;
                if (!shown) {
                    memset(&pic.img[(size_t)d * w], 0, (size_t)(e - d + 1) * w * sizeof(uint32_t));
                }
                else {
                    *bandFrame = scene.frames[fi];
                    if (limit) bandFrame->limit = limit;
                    vdpRenderSpriteLines(s, *bandFrame, pic.img.data(), w, d, e, rgb.data());
                }
                d = e + 1;
            }
            return;
        }
        if (!raster.valid) {
            *bandFrame = sprites;
            if (limit) bandFrame->limit = limit;
            vdpRenderSpriteLines(s, *bandFrame, pic.img.data(), w, 0, lines - 1, rgb.data());
            return;
        }
        int prev[8] = { -2 }, key[8];
        for (int d = 0; d < lines;) {
            int kb = spriteBandOf(d), e = d;
            while (e + 1 < lines && spriteBandOf(e + 1) == kb) e++;
            const VdpBand& b = raster.bands[kb >= 0 ? kb : 0];
            if (b.mode < 0 || vdpSpriteMode(b.mode) == 0) {
                memset(&pic.img[(size_t)d * w], 0, (size_t)(e - d + 1) * w * sizeof(uint32_t));
                d = e + 1;
                continue;
            }
            spriteKey(b, key);
            if (memcmp(key, prev, sizeof(key)) != 0) {
                /* Shown here; the lines the VDP shows none on are cleared below. */
                UInt8 regs[32];
                memcpy(regs, b.regs, sizeof(regs));
                regs[1] |= 0x40;
                regs[8] &= ~0x02;
                VdpLayout l = { regs, b.mode, b.blink };
                vdpEvalSprites(s, l, lines, bandFrame);
                if (limit) bandFrame->limit = limit;
                memcpy(prev, key, sizeof(key));
            }
            vdpRenderSpriteLines(s, *bandFrame, pic.img.data(), w, d, e, rgb.data());
            for (int k = d; k <= e && k < (int)raster.spritesOff.size(); k++) {
                if (raster.spritesOff[k]) memset(&pic.img[(size_t)k * w], 0, (size_t)w * sizeof(uint32_t));
            }
            d = e + 1;
        }
    }

    /* The strip's text line in the body font, which its captions and headings use. */
    static float stripLineH()
    {
        ui::PushFont(UI_FONT_BODY);
        float h = ImGui::GetTextLineHeight();
        ui::PopFont();
        return h;
    }

    /* Where thumbnail j of a row starts. */
    static float stripItemX(int j, float tw)
    {
        return j * (tw + ui::Px(8));
    }

    /* One row of thumbnails tw wide, a 256-dot page at the step's size, and as tall whatever they
    ** show, under a heading row; returns its height with a gap below, and its width in *width. */
    float stripLayout(float* tw, float* width, std::vector<StripRow>& rows, int step) const
    {
        float gap = ui::Px(8), lineH = stripLineH(), labelH = lineH + ui::Px(2), groupH = lineH + ui::Px(4);
        int n = (int)stripItems.size();
        rows.clear();
        *tw = floorf(256 * shell::pagesStepScale(step));
        *width = n > 0 ? stripItemX(n - 1, *tw) + *tw : 0;
        if (n == 0) return 0;
        rows.push_back(StripRow{ 0, n, *tw });
        return groupH + *tw + labelH + gap;
    }

    /* Bands a and b differ only in what lays out sprites: R#5, R#6, R#11 and R#1's size bits. */
    static bool spriteOnlySplit(const VdpBand& a, const VdpBand& b)
    {
        VdpBand c = b;
        c.regs[5] = a.regs[5];
        c.regs[6] = a.regs[6];
        c.regs[11] = a.regs[11];
        c.regs[1] = (UInt8)((b.regs[1] & ~0x03) | (a.regs[1] & 0x03));
        return vdpRasterSameKey(a, c);
    }

    /* A line from a to x across, 3 px on and 3 px off. */
    static void dashedLine(ImDrawList* dl, ImVec2 a, float x1, ImU32 col, float th)
    {
        dashedSegment(dl, a, ImVec2(x1, a.y), col, th, ui::Px(3));
    }

    /* The thumbnail's name; the short one when the full one does not fit in w. */
    const char* itemName(const StripItem& it, float w, char* out, size_t size) const
    {
        const char* full = NULL;
        const char* brief = NULL;
        switch (it.kind) {
        case SI_PAGE:   textf(out, size, ts(TS_PAGE_LABEL), it.page); break;
        case SI_NAME:   full = ts(TS_RANGE_NAME_TABLE); brief = ts(TS_STRIP_SHORT_NAMES); break;
        case SI_PATGEN: full = ts(TS_VIEW_PATTERNS); brief = ts(TS_STRIP_SHORT_PATTERNS); break;
        case SI_SPRPAT: full = ts(TS_RANGE_SPRITE_PATTERNS); brief = ts(TS_STRIP_SHORT_SPRPAT); break;
        default:        full = ts(TS_VIEW_SPRITES); brief = ts(TS_STRIP_SHORT_SPRITES); break;
        }
        if (full) textf(out, size, "%s", full);
        if (ImGui::CalcTextSize(out).x <= w) return out;
        if (it.kind == SI_PAGE) textf(out, size, "%d", it.page);
        else textf(out, size, "%s", brief);
        return out;
    }

    /* A group's heading: its lines of the last pictures, mode and name table, shorter when it does not fit in w. */
    void groupHeading(const VdpDebugState* s, int k, float w, char* out, size_t size) const
    {
        if (k >= STRIP_GROUPS) {
            textf(out, size, "%s", ts(TS_STRIP_WHOLE_FRAME));
            return;
        }
        const StripGroup& g = stripGroups[k];
        /* A held pattern slot's R#2 is fixed by its grouping, so its name table does not change; without a log the regs are live. */
        int name = -1;
        if (raster.valid && vdpIsPatternMode(g.mode)) {
            VdpTables tb;
            vdpDecodeTables(s, vdpLayoutOf(g), &tb);
            name = tb.name;
        }
        char nameAt[48] = "", hex[16] = "";
        if (name >= 0) {
            textf(nameAt, sizeof(nameAt), ts(TS_STRIP_NAME_AT), name);
            snprintf(hex, sizeof(hex), " \xC2\xB7 %05X", name);
        }
        int at = textf(out, size, ts(TS_STRIP_GROUP), g.heldLines, shortModeName(g.mode));
        if (at > 0 && (size_t)at < size) textf(out + at, size - at, "%s", nameAt);
        if (ImGui::CalcTextSize(out).x <= w) return;
        snprintf(out, size, "%s \xC2\xB7 %s%s", g.heldLines, shortModeName(g.mode), nameAt);
        if (ImGui::CalcTextSize(out).x <= w || g.mode < 0) return;
        int sc = g.mode == 13 ? 0 : g.mode == 10 ? 10 : g.mode;
        snprintf(out, size, "%s \xC2\xB7 SC%d%s", g.heldLines, sc, hex);
        if (ImGui::CalcTextSize(out).x <= w || name < 0) return;
        snprintf(out, size, "SC%d%s", sc, hex);
    }

    /* The slot in a picture last of those that show a thumbnail: its own, or one sharing the table. */
    int stripFreshest(const StripItem& it) const
    {
        int k = it.group;
        for (int j = 0; j < STRIP_GROUPS && k < STRIP_GROUPS; j++) {
            if (((it.also >> j) & 1) && stripGroups[j].age < stripGroups[k].age) k = j;
        }
        return k;
    }

    /* Draws the strip at the cursor; status gets the hovered thumbnail's line. */
    void drawStrip(const VdpDebugState* s, Focus& f, float tw, const std::vector<StripRow>& rows, char* status, size_t size)
    {
        const UiTokens& t = uiTokens();
        float gap = ui::Px(8), lineH = stripLineH(), labelH = lineH + ui::Px(2), th = fmaxf(1.0f, ui::Px(1));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float y = origin.y + ui::Px(4);
        bool perBand = spriteBandsDiffer();
        status[0] = 0;
        ui::PushFont(UI_FONT_BODY);
        float clipL = dl->GetClipRectMin().x, clipR = dl->GetClipRectMax().x;
        for (const StripRow& r : rows) {
            /* Each group's lines and mode over its thumbnails, kept at the left edge while it scrolls by. */
            for (int j = r.first; j < r.first + r.count; j++) {
                int k = stripItems[j].group, end = j;
                if (j > r.first && stripItems[j - 1].group == k) continue;
                while (end + 1 < r.first + r.count && stripItems[end + 1].group == k) end++;
                float x0 = origin.x + stripItemX(j - r.first, tw), x1 = origin.x + stripItemX(end - r.first, tw) + tw;
                if (x1 <= clipL || x0 >= clipR) continue;
                x0 = fmaxf(x0, floorf(clipL));
                char head[256];
                groupHeading(s, k, x1 - x0, head, sizeof(head));
                dl->AddLine(ImVec2(x0, y + lineH), ImVec2(x1, y + lineH), t.divider, th);
                dl->PushClipRect(ImVec2(x0, y), ImVec2(x1, y + lineH + ui::Px(2)), true);
                dl->AddText(ImVec2(x0, y), t.textSecondary, head);
                dl->PopClipRect();
            }
            y += lineH + ui::Px(4);
            for (int j = r.first; j < r.first + r.count; j++) {
                const StripItem& it = stripItems[j];
                float x = origin.x + stripItemX(j - r.first, tw);
                if (x + tw <= clipL || x >= clipR) continue;
                const StripGroup* g = it.group < STRIP_GROUPS ? &stripGroups[it.group] : NULL;
                float tvW = (float)it.w * it.ax, tvH = (float)it.h * it.ay, sc = fminf(tw / tvW, tw / tvH);
                float iw = floorf(tvW * sc), ih = floorf(tvH * sc);
                ImVec2 a(floorf(x + (tw - iw) / 2), y), b(a.x + iw, a.y + ih);
                ViewPicture* pic = it.pic;
                if (it.kind == SI_SPRITES || transparent0) drawCheckerRect(dl, a, b);
                if (pic->tex.id() && pic->h > 0) {
                    dl->AddImage(ImTextureRef(pic->tex.id()), a, b, ImVec2(0, (float)it.v0 / pic->h), ImVec2(1, (float)it.v1 / pic->h));
                }
                if (it.kind == SI_PAGE) {
                    /* Where 192 or 212 displayed lines end, and the rows and columns each band reads here. */
                    float ly = floorf(a.y + ih * layoutLines(s, g->regs) / 256);
                    dl->AddLine(ImVec2(a.x, ly), ImVec2(b.x, ly), ui::WithAlpha(t.textTertiary, 140), th);
                    for (size_t i = 0; (stripMixed || raster.bands.size() > 1) && i < raster.bands.size(); i++) {
                        const VdpBand& bd = raster.bands[i];
                        int seg[12], n = vdpRasterBandCols(bd, seg);
                        if (!showsPage(seg, n, it.page) || !vdpGroupHasBand(*g, bd)) continue;
                        vdpRasterBandRows(s, bd, it.page, stripRuns);
                        for (int k = 0; k < n; k++) {
                            if (seg[k * 3] != it.page) continue;
                            float x0 = floorf(a.x + iw * seg[k * 3 + 1] / 256), x1 = floorf(a.x + iw * seg[k * 3 + 2] / 256);
                            for (size_t q = 0; q + 1 < stripRuns.size(); q += 2) {
                                float y0 = floorf(a.y + ih * stripRuns[q] / 256), y1 = floorf(a.y + ih * stripRuns[q + 1] / 256);
                                dl->AddRect(ImVec2(x0, y0), ImVec2(fmaxf(x1, x0 + th), fmaxf(y1, y0 + th)), bandColour((int)i), 0, th);
                            }
                        }
                    }
                }
                if (it.kind == SI_PATGEN && g && (g->mode == 2 || g->mode == 4)) {
                    int blocks = it.v1 / 64;
                    for (int block = 1; block < blocks; block++) {
                        float ly = floorf(a.y + ih * block / blocks);
                        dl->AddLine(ImVec2(a.x, ly), ImVec2(b.x, ly), ui::WithAlpha(t.textTertiary, 140), th);
                    }
                }
                /* An odd page shown with EO or blink alternates with its even pair. */
                bool shown = it.kind == SI_PAGE && ((g->shownPages >> it.page) & 1) != 0;
                bool alternates = g && ((g->regs[9] & 0x04) || g->regs[13] != 0);
                bool pair = it.kind == SI_PAGE && alternates && !(it.page & 1) && ((g->shownPages >> (it.page | 1)) & 1) != 0;
                ImVec2 oa(a.x - th, a.y - th), ob(b.x + th, b.y + th);
                if (shown) dl->AddRect(oa, ob, t.textSecondary, 0, th);
                if (pair && !shown) dashedRect(dl, oa, ob, t.textSecondary, th);

                ImGui::SetCursorScreenPos(ImVec2(x, y));
                ImGui::PushID(j);
                ImGui::InvisibleButton("##thumb", ImVec2(tw, r.h + labelH));
                ImGui::PopID();
                bool hovered = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                if (hovered) dl->AddRect(oa, ob, t.text, 0, th);

                /* The label: a dot per band showing the page; an eye after the shown page's name. */
                float lx = a.x, ly = y + r.h + ui::Px(2), dot = ui::Px(3);
                float cy = floorf(ly + ImGui::GetTextLineHeight() / 2);
                float eye = shown && ui::HasIcons() ? ui::Px(18) : 0;
                char bandLines[64] = "";
                size_t bl = 0;
                for (size_t i = 0; it.kind == SI_PAGE && !stripMixed && raster.bands.size() > 1 && i < raster.bands.size(); i++) {
                    const VdpBand& bd = raster.bands[i];
                    int seg[12], n = vdpRasterBandCols(bd, seg);
                    if (!showsPage(seg, n, it.page)) continue;
                    dl->AddCircleFilled(ImVec2(lx + dot, cy), dot, bandColour((int)i));
                    lx += dot * 2 + ui::Px(3);
                    if (bl < sizeof(bandLines)) {
                        bl += (size_t)(bd.first == bd.last ? snprintf(bandLines + bl, sizeof(bandLines) - bl, "%s%d", bl ? ", " : "", bd.first)
                                                           : snprintf(bandLines + bl, sizeof(bandLines) - bl, "%s%d-%d", bl ? ", " : "", bd.first, bd.last));
                    }
                }
                if (bandLines[0]) lx += ui::Px(1);
                char name[96], full[96];
                itemName(it, x + tw - lx - eye, name, sizeof(name));
                itemName(it, 1e6f, full, sizeof(full));
                dl->PushClipRect(ImVec2(x, ly), ImVec2(x + tw, ly + labelH), true);
                dl->AddText(ImVec2(lx, ly), shown ? t.text : t.textSecondary, name);
                if (eye > 0) ui::DrawIcon(dl, ImVec2(lx + ImGui::CalcTextSize(name).x + ui::Px(4) + ui::Px(7), cy), UI_ICON_VIEW, 14, t.text);
                dl->PopClipRect();

                if (hovered) stripHover(s, f, it, full, shown, pair, bandLines, perBand, clicked, status, size);
            }
            y += r.h + labelH + gap;
        }
        ui::PopFont();
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(1, y - origin.y - gap));
    }

    void stripHover(const VdpDebugState* s, Focus& f, const StripItem& it, const char* name, bool shown, bool pair,
                    const char* bandLines, bool perBand, bool clicked, char* status, size_t size)
    {
        if (it.kind == SI_SPRITES) {
            if (!vdpGetSpritesEnable()) textf(status, size, "%s", ts(TS_STRIP_SPRITES_OPTION_OFF));
            else if (const char* off = !raster.valid ? (Sp3Panel::active(s) ? sp3.offReason(s) : sprites.off) : NULL) {
                textf(status, size, ts(TS_SPR_NONE_SHOWN), off);
            }
            else textf(status, size, ts(TS_STRIP_SPRITES), screenLines(s), perBand && !Sp3Panel::active(s) ? ts(TS_STRIP_SPRITES_BANDS) : "",
                       vdpGetNoSpritesLimit() ? ts(TS_STRIP_SPRITES_NO_LIMIT) : "");
            if (clicked) shell::requestView("sprites");
            return;
        }
        char extra[256] = "";
        size_t at = 0;
        const StripGroup& fg = stripGroups[stripFreshest(it)];
        for (int j = 0; j < STRIP_GROUPS && at < sizeof(extra); j++) {
            if ((it.also >> j) & 1) at += (size_t)textf(extra + at, sizeof(extra) - at, ts(TS_STRIP_SHARED), stripGroups[j].heldLines);
        }
        if (shown && at < sizeof(extra)) at += (size_t)textf(extra + at, sizeof(extra) - at, "%s", ts(TS_STRIP_SHOWN));
        if (pair && at < sizeof(extra)) at += (size_t)textf(extra + at, sizeof(extra) - at, "%s", ts(TS_STRIP_PAIR));
        if (bandLines[0] && at < sizeof(extra)) at += (size_t)textf(extra + at, sizeof(extra) - at, ts(TS_STRIP_ON_LINES), bandLines);
        /* Each goes to the tab that draws it: pages to Bitmap, names and patterns to Patterns
        ** (reading the thumbnail's group), sprite patterns to Sprites. */
        const char* view = it.kind == SI_SPRPAT ? ts(TS_VIEW_SPRITES) : it.kind == SI_PAGE ? ts(TS_VIEW_BITMAP) : ts(TS_VIEW_PATTERNS);
        if (at < sizeof(extra)) textf(extra + at, sizeof(extra) - at, ts(TS_STRIP_CLICK_VIEW), view);
        textf(status, size, ts(TS_STRIP_RANGE), name, it.addr, it.addr + it.len - 1, extra);

        /* The map lights the lanes the hovered table or page overlaps. */
        int mode = stripGroups[it.group].mode;
        UInt32 phys = (UInt32)vdpPhysicalAddress(s, mode, it.addr);
        Focus hit = { f.chipId, FOCUS_PHYS, phys, (UInt32)(interleaved(s, mode) ? it.len / 2 : it.len), FOCUS_VRAM, 0, 0, false };
        if (clicked && (it.kind == SI_NAME || it.kind == SI_PATGEN)) {
            /* A shared table opens its heading's slot, not whichever twin this picture has. */
            const StripGroup& pg = stripGroups[it.group];
            patGroupSet = stripMixed;
            if (stripMixed) patKey = vdpGroupKeyOf(pg);
            notePick(pg, pg.order);
            patScrollPart = it.kind == SI_NAME ? 0 : 1;
            patScrollFrames = 3;
            shell::requestView("patterns");
        }
        else if (clicked && it.kind == SI_SPRPAT) {
            /* The table its slot's first line used when that slot was last in a picture. */
            if (fg.sprKeySet) sprChoose(fg.sprKey);
            shell::requestView("sprites");
        }
        else if (clicked) {
            f = hit;
            f.len = 256;
            f.pinned = true;
            stripVramPage = it.page;
            stripVramFrames = 3;
            shell::requestView("bitmap");
        }
        else if (!f.pinned) {
            f = hit;
        }
    }

    /* ---- Patterns ---- */

    /* Whether two characters draw from the same bytes: the same pattern, and colours in SCREEN 2 and 4. */
    static bool sameChar(const VdpCharInfo& a, const VdpCharInfo& b, int mode)
    {
        return a.patternAddr == b.patternAddr && (!(mode == 2 || mode == 4) || a.colorAddr == b.colorAddr);
    }

    /* The sheet character read from the same bytes as ci, or -1. */
    static int sheetCharLike(const VdpDebugState* s, const VdpLayout& l, const VdpCharInfo& ci)
    {
        int blocks = vdpSheetBlocks(s, l);
        for (int c = 0; c < 256 * blocks; c++) {
            int x, y;
            VdpCharInfo sc;
            sheetCell(c, &x, &y);
            if (vdpSheetChar(s, l, x, y, &sc) && sameChar(sc, ci, l.mode)) return c;
        }
        return -1;
    }

    /* The focused character on both parts: its sheet cells (every block that mirrors it) and the
    ** name cells showing it, these only while they are few; the parts' chips. */
    static void patOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V99x8View* self = (V99x8View*)ref;
        const UiTokens& t = uiTokens();
        const PatParts& P = self->pat;
        const VdpDebugState* s = self->ovState;
        const VdpLayout& l = self->ovLayout;
        int mode = l.mode, blocks = vdpSheetBlocks(s, l);
        float th = fmaxf(1.0f, ui::Px(1));
        dl->PushClipRect(xf.clipMin, xf.clipMax, true);
        const Focus& sf = shell::shownFocus(), &cf = self->patCellFocus;
        int ch = self->focusChar(s, l, sf);
        VdpCharInfo fc;
        int fx, fy;
        if (ch >= 0 && ch < 256 * blocks) sheetCell(ch, &fx, &fy);
        if (ch >= 0 && ch < 256 * blocks && vdpSheetChar(s, l, fx, fy, &fc)) {
            for (int c = 0; c < 256 * blocks; c++) {
                int x, y;
                VdpCharInfo ci;
                sheetCell(c, &x, &y);
                ImVec2 a = xf.toScreen((float)(P.sx + x * P.sdx), (float)(P.sy + y)), b = xf.toScreen((float)(P.sx + (x + 8) * P.sdx), (float)(P.sy + y + 8));
                if (b.x < xf.clipMin.x || a.x > xf.clipMax.x || b.y < xf.clipMin.y || a.y > xf.clipMax.y) continue;
                if (c != ch && !(vdpSheetChar(s, l, x, y, &ci) && sameChar(ci, fc, mode))) continue;
                float w = c == ch ? th * 2 : th;
                dl->AddRect(ImVec2(a.x - w / 2, a.y - w / 2), ImVec2(b.x + w / 2, b.y + w / 2), c == ch ? t.accent : ui::WithAlpha(t.accent, 160), 0, w);
            }
            /* The cell thick only while the focus is still the one it gave, not a pin made elsewhere. */
            bool own = sf.kind == cf.kind && sf.addr == cf.addr && sf.a == cf.a && sf.b == cf.b;
            self->patNameMarks(dl, xf, fc, th, own);
        }
        if (self->patNumbers) self->patNumbersDraw(dl, xf);
        VdpTables tb;
        vdpDecodeTables(s, l, &tb);
        char chip[64];
        textf(chip, sizeof(chip), ts(TS_PAT_CHIP_NAMES), tb.name);
        partChip(dl, xf.toScreen((float)P.nx, (float)P.ny), xf.toScreen((float)(P.nx + P.nw), (float)(P.ny + P.nh)), chip);
        textf(chip, sizeof(chip), ts(TS_PAT_CHIP_PATTERNS), tb.patternGen);
        partChip(dl, xf.toScreen((float)P.sx, (float)P.sy), xf.toScreen((float)(P.sx + P.sw), (float)(P.sy + P.sh)), chip);
        dl->PopClipRect();
    }

    /* The name cells that show character fc, only those in view; none but the hovered cell when
    ** more than a quarter of the cells show it (then it marks nothing). own: the focus is the one
    ** the cell at patCol, patRow gave, which is drawn thick. */
    void patNameMarks(ImDrawList* dl, const ui::CanvasXf& xf, const VdpCharInfo& fc, float th, bool own) const
    {
        const UiTokens& t = uiTokens();
        const PatParts& P = pat;
        const VdpDebugState* s = ovState;
        const VdpLayout& l = ovLayout;
        int cw = l.mode == 0 || l.mode == 13 ? 6 : 8, cols = P.nw / cw, vs = vdpScreenScroll(s, l);
        int many = patMatches(fc) * 4 > cols * ((P.nh + 7) / 8);
        for (int r = 0; r < 32; r++) {
            /* Row r's lines on the screen: from (8r - scroll) mod 256, in one or two pieces. */
            int y0 = (r * 8 - vs) & 0xff;
            for (int piece = 0; piece < 2; piece++) {
                int a0 = piece == 0 ? y0 : 0, a1 = piece == 0 ? (y0 + 8 < 256 ? y0 + 8 : 256) : y0 + 8 - 256;
                if (a1 > P.nh) a1 = P.nh;
                if (a0 >= a1) continue;
                ImVec2 ya = xf.toScreen(0, (float)(P.ny + a0)), yb = xf.toScreen(0, (float)(P.ny + a1));
                if (yb.y < xf.clipMin.y || ya.y > xf.clipMax.y) continue;
                for (int c = 0; c < cols; c++) {
                    ImVec2 a = xf.toScreen((float)(P.nx + c * cw), (float)(P.ny + a0)), b = xf.toScreen((float)(P.nx + (c + 1) * cw), (float)(P.ny + a1));
                    if (b.x < xf.clipMin.x || a.x > xf.clipMax.x) continue;
                    bool hot = own && c == patCol && r == patRow;
                    if (many && !hot) continue;
                    VdpCharInfo ci;
                    if (!vdpNameTableChar(s, l, c * cw, a0, -1, &ci) || !sameChar(ci, fc, l.mode)) continue;
                    float w = hot ? th * 2 : th;
                    dl->AddRect(ImVec2(a.x - w / 2, a.y - w / 2), ImVec2(b.x + w / 2, b.y + w / 2), hot ? t.accent : ui::WithAlpha(t.accent, 160), 0, w);
                }
            }
        }
    }

    /* Each cell's character number in hex, on the cells in view that are 18 px or wider. */
    void patNumbersDraw(ImDrawList* dl, const ui::CanvasXf& xf) const
    {
        const UiTokens& t = uiTokens();
        const PatParts& P = pat;
        const VdpLayout& l = ovLayout;
        int cw = l.mode == 0 || l.mode == 13 ? 6 : 8, vs = vdpScreenScroll(ovState, l);
        ui::PushFont(UI_FONT_MONO, 11);
        ImVec2 ts = ImGui::CalcTextSize("FF");
        auto label = [&](ImVec2 a, int v) {
            char num[4];
            snprintf(num, sizeof(num), "%02X", v & 0xff);
            dl->AddRectFilled(a, ImVec2(a.x + ts.x + ui::Px(2), a.y + ts.y), ui::WithAlpha(t.layer, 170));
            dl->AddText(ImVec2(a.x + ui::Px(1), a.y), t.text, num);
        };
        /* The cells across from x0 a cell w image px wide that reach the view, as c0..c1. */
        auto across = [&](int x0, float w, int n, int* c0, int* c1) {
            *c0 = (int)floorf((xf.clipMin.x - xf.origin.x) / (w * xf.scale.x) - x0 / w);
            *c1 = (int)ceilf((xf.clipMax.x - xf.origin.x) / (w * xf.scale.x) - x0 / w);
            if (*c0 < 0) *c0 = 0;
            if (*c1 > n - 1) *c1 = n - 1;
        };
        if (cw * xf.scale.x >= ui::Px(18)) {
            int c0, c1;
            across(P.nx, (float)cw, P.nw / cw, &c0, &c1);
            for (int r = 0; r < 32; r++) {
                int y0 = (r * 8 - vs) & 0xff, h = y0 + 8 <= 256 ? 8 : 256 - y0;
                if (y0 >= P.nh || h * xf.scale.y < ts.y) continue;
                ImVec2 ya = xf.toScreen(0, (float)(P.ny + y0));
                if (ya.y + ts.y < xf.clipMin.y || ya.y > xf.clipMax.y) continue;
                for (int c = c0; c <= c1; c++) {
                    VdpCharInfo ci;
                    if (vdpNameTableChar(ovState, l, c * cw, y0, -1, &ci)) label(xf.toScreen((float)(P.nx + c * cw), (float)(P.ny + y0)), ci.ch);
                }
            }
        }
        if (8 * P.sdx * xf.scale.x >= ui::Px(18)) {
            int c0, c1;
            across(P.sx, 8.0f * P.sdx, 32, &c0, &c1);
            for (int r = 0; r < P.sh / 8; r++) {
                ImVec2 ya = xf.toScreen(0, (float)(P.sy + r * 8));
                if (ya.y + ts.y < xf.clipMin.y || ya.y > xf.clipMax.y) continue;
                for (int c = c0; c <= c1; c++) label(xf.toScreen((float)(P.sx + c * 8 * P.sdx), (float)(P.sy + r * 8)), (r % 8) * 32 + c);
            }
        }
        ui::PopFont();
    }

    /* How many name cells on the screen show character fc. */
    int patMatches(const VdpCharInfo& fc) const
    {
        const VdpDebugState* s = ovState;
        const VdpLayout& l = ovLayout;
        int cw = l.mode == 0 || l.mode == 13 ? 6 : 8, n = 0;
        for (int y = 0; y < pat.nh; y += 8) {
            for (int x = 0; x + cw <= pat.nw; x += cw) {
                VdpCharInfo ci;
                if (vdpNameTableChar(s, l, x, y, -1, &ci) && sameChar(ci, fc, l.mode)) n++;
            }
        }
        return n;
    }

    /* The group a view reads, on the command bar, unless the frame's end is in the mode of its only one. */
    void groupTag(int k)
    {
        char label[128];
        if (k < 0 || !groupLabel(k, label, sizeof(label))[0]) return;
        barText(label);
    }

    /* The name table's screen into namePic; true when decoded again. */
    bool decodeNames(const VdpDebugState* s, const VdpLayout& l)
    {
        char key[64];
        const UInt8* r = l.regs;
        int lines = layoutLines(s, r);
        snprintf(key, sizeof(key), "%d/%d/%d/%02X%02X%02X%02X%02X%02X%02X%02X%d/%d/%08X", l.mode, color0(), lines, r[2], r[3], r[4], r[7],
                 r[10], r[12], r[13], vdpScreenScroll(s, l), r[25] & 1, l.blink, palHash(l.rgb));
        bool decoded = namePic.needsDecode(key);
        if (decoded) vdpDecodeNameTable(s, l, lines, -1, color0(), namePic.img, &namePic.w, &namePic.h);
        return decoded;
    }

    /* The parts in one picture: side by side when that fits the well at this zoom (Fit: when it
    ** shows larger), else one under the other; going back side by side needs 24 px to spare. */
    void patCompose(const VdpDebugState* s, const VdpLayout& l)
    {
        bool nd = decodeNames(s, l);
        bool sd = decodeSheet(s, l, sheetPic);
        PatParts P = {};
        P.sdx = l.mode == 13 ? 2 : 1;
        P.nw = namePic.w;
        P.nh = namePic.h;
        P.sw = sheetPic.w * P.sdx;
        P.sh = sheetPic.h;
        int gx = 8 * P.sdx, gy = 8;
        ImVec2 avail = ImGui::GetContentRegionAvail(), k = ui::CanvasScale(sheetCv, 1);
        float wellW = avail.x - ImGui::GetStyle().ScrollbarSize, wellH = avail.y - ImGui::GetStyle().ScrollbarSize;
        float wS = (float)(P.nw + gx + P.sw), hS = (float)(P.nh > P.sh ? P.nh : P.sh);
        float wT = (float)(P.nw > P.sw ? P.nw : P.sw), hT = (float)(P.nh + gy + P.sh);
        if (sheetCv.z().fit) {
            patSide = fminf(wellW / (wS * k.x), wellH / (hS * k.y)) >= fminf(wellW / (wT * k.x), wellH / (hT * k.y));
        }
        else {
            float u = (float)sheetCv.z().zoom;
            patSide = wS * u * k.x <= wellW - (patSide ? 0 : ui::Px(24));
        }
        P.sx = patSide ? P.nw + gx : 0;
        P.sy = patSide ? 0 : P.nh + gy;
        P.w = patSide ? (int)wS : (int)wT;
        P.h = patSide ? (int)hS : (int)hT;
        char key[64];
        unsigned hk = 2166136261u;
        for (const char* c : { namePic.key, sheetPic.key }) {
            for (; *c; c++) hk = (hk ^ (unsigned char)*c) * 16777619u;
        }
        snprintf(key, sizeof(key), "%08X/%d/%d/%dx%d", hk, (int)patSide, P.sdx, P.w, P.h);
        bool composed = patPic.needsDecode(key) || nd || sd;
        if (composed) {
            patPic.w = P.w;
            patPic.h = P.h;
            patPic.img.assign((size_t)P.w * P.h, 0);
            for (int y = 0; y < P.nh; y++) {
                memcpy(&patPic.img[(size_t)(P.ny + y) * P.w + P.nx], &namePic.img[(size_t)y * namePic.w], (size_t)P.nw * 4);
            }
            for (int y = 0; y < P.sh; y++) {
                for (int x = 0; x < P.sw; x++) patPic.img[(size_t)(P.sy + y) * P.w + P.sx + x] = sheetPic.img[(size_t)y * sheetPic.w + x / P.sdx];
            }
        }
        patPic.upload(composed);
        pat = P;
        /* The checker and grid on the parts only; the sheet's grid a sheet dot apart. */
        sheetCv.partCount = 2;
        sheetCv.parts[0] = { (float)P.nx, (float)P.ny, (float)(P.nx + P.nw), (float)(P.ny + P.nh), 1 };
        sheetCv.parts[1] = { (float)P.sx, (float)P.sy, (float)(P.sx + P.sw), (float)(P.sy + P.sh), P.sdx };
    }

    /* Read as, first on the bar, with two or more groups in pattern modes: Auto (as autoGroup picks)
    ** or one; else the group read, as a tag. True when it drew the chooser.
    ** The groups: the Pages strip's held ones, so one flipped away stays listed, then this picture's others. */
    bool patGroupChooser(const VdpDebugState* s, int g)
    {
        const VdpBandGroup* pg[8];
        const char* lines[8];
        int order[8], held[STRIP_GROUPS], nh = raster.valid ? heldSlots(held) : 0, npg = 0;
        for (int i = 0; i < nh && npg < 8; i++) {
            const StripGroup& h = stripGroups[held[i]];
            if (!vdpIsPatternMode(h.mode)) continue;
            pg[npg] = &h;
            lines[npg] = h.heldLines;
            order[npg++] = h.order;
        }
        for (int i = 0; raster.valid && i < (int)groups.size() && npg < 8; i++) {
            const VdpBand* b = firstBandOf(groups[i]);
            bool isHeld = false;
            for (int j = 0; j < nh && b != NULL; j++) isHeld = isHeld || vdpGroupHasBand(stripGroups[held[j]], *b);
            if (!vdpIsPatternMode(groups[i].mode) || isHeld) continue;
            pg[npg] = &groups[i];
            lines[npg] = groups[i].lines;
            order[npg++] = -1;
        }
        if (npg < 2) {
            groupTag(g);
            return false;
        }
        int n = 0, autoG = raster.valid ? autoGroup(false, &n) : -1;
        int autoMode = autoG >= 0 ? groups[autoG].mode : s->screenMode;
        if (autoG >= 0 && n == 1 && vdpIsPatternMode(s->screenMode)) autoG = -1;
        bool fixed = patGroupSet && g >= 0;
        char preview[256], groupText[8][160];
        if (autoG >= 0) textf(preview, sizeof(preview), ts(TS_READ_AS_GROUP_AUTO), groups[autoG].lines, shortModeName(groups[autoG].mode));
        else textf(preview, sizeof(preview), ts(TS_READ_AS_AUTO), shortModeName(autoMode));
        const char* items[9] = { preview };
        /* The one picked stays the one shown while it is held; else the one read now. */
        int chosen = -1;
        for (int k = 0; fixed && k < npg && chosen < 0; k++) {
            if (order[k] >= 0 && order[k] == patPickOrder) chosen = k;
        }
        const VdpBand* gb = fixed && chosen < 0 ? firstBandOf(groups[g]) : NULL;
        for (int k = 0; k < npg; k++) {
            int at = textf(groupText[k], sizeof(groupText[k]), ts(TS_STRIP_GROUP), lines[k], shortModeName(pg[k]->mode));
            /* A held slot's name table, as its heading says: twins on the same lines read apart. */
            if (order[k] >= 0 && at > 0 && (size_t)at < sizeof(groupText[k])) {
                VdpTables tb;
                vdpDecodeTables(s, vdpLayoutOf(*pg[k]), &tb);
                textf(groupText[k] + at, sizeof(groupText[k]) - at, ts(TS_STRIP_NAME_AT), tb.name);
            }
            items[1 + k] = groupText[k];
            if (chosen < 0 && gb != NULL && vdpGroupHasBand(*pg[k], *gb)) chosen = k;
        }
        float comboW = ui::ComboWidth(items, 1 + npg, 120);
        barText(ts(TS_READ_AS));
        ui::CommandBarItem(ui::Px(comboW));
        if (ui::BeginCombo("##patgroup", chosen >= 0 ? groupText[chosen] : preview, comboW)) {
            if (ui::ComboItem(IdLabel(preview, "auto"), chosen < 0)) patGroupSet = false;
            for (int k = 0; k < npg; k++) {
                ImGui::PushID(100 + k);
                if (ui::ComboItem(IdLabel(groupText[k], "group"), chosen == k)) {
                    patGroupSet = true;
                    patKey = vdpGroupKeyOf(*pg[k]);
                    notePick(*pg[k], order[k]);
                }
                ImGui::PopID();
            }
            ui::EndCombo();
        }
        if (fixed) {
            ui::CommandBarItem(ui::Px(14) + ImGui::CalcTextSize(ts(TS_STATE_FIXED)).x);
            ui::StatusDot(ts(TS_STATE_FIXED), ui::TONE_CAUTION);
            IdLabel follow(ts(TS_FOLLOW_MODE), "follow");
            ui::CommandBarItem(ImGui::CalcTextSize(follow, NULL, true).x + ImGui::GetStyle().FramePadding.x * 2);
            if (ui::SubtleButton(follow)) patGroupSet = false;
        }
        return true;
    }

    /* A character pinned by a click keeps the group it was read from (read, or -1 for the frame's end). */
    void noteCharPin(const Focus& f, bool clicked, int read)
    {
        if (!clicked || !f.pinned || f.kind != FOCUS_CHAR) return;
        charPin.set = read >= 0 && read < (int)groups.size();
        charPin.a = f.a;
        charPin.b = f.b;
        if (charPin.set) charPin.key = vdpGroupKeyOf(groups[read]);
    }

    void drawPatterns(const VdpDebugState* s, Focus& f)
    {
        VdpLayout l;
        int g, read;
        if (!viewLayout(s, false, &l, &g, &read)) {
            emptyView(ts(TS_PATTERNS_NONE));
            return;
        }
        int mode = l.mode, blocks = vdpSheetBlocks(s, l);
        /* T80's 480 dots a line are half as wide as tall, as on a TV. */
        sheetCv.aspectX = 1;
        sheetCv.aspectY = mode == 13 ? 2 : 1;
        ui::BeginCommandBar("bar");
        if (patGroupChooser(s, g)) ui::CommandBarSeparator();
        barToggle("##patnum", UI_ICON_NUMBERS, ts(TS_PAT_NUMBERS), ts(TS_PAT_NUMBERS_TIP), &patNumbers);
        ui::GridToggle(sheetCv);
        zoomGroup(sheetCv, true);
        ui::EndCommandBar();
        ovState = s;
        ovLayout = l;
        patCompose(s, l);
        const PatParts& P = pat;
        if (patScrollFrames > 0) {
            /* A thumbnail clicked on the Pages card: its part scrolled to until the well knows its size. */
            if (!sheetCv.z().fit) {
                ImVec2 sc = ui::CanvasScale(sheetCv, (float)sheetCv.z().zoom);
                float ox = patScrollPart == 0 ? (float)P.nx : (float)P.sx, oy = patScrollPart == 0 ? (float)P.ny : (float)P.sy;
                ImGui::SetNextWindowScroll(ImVec2(floorf(ox * sc.x), floorf(oy * sc.y)));
            }
            patScrollFrames--;
        }
        ui::CanvasHit h = ui::Canvas("sheet", patPic.tex.id(), patPic.w, patPic.h, sheetCv, ImVec2(0, 0), patOverlay, this);
        bool onPart = false;
        char status[512] = "";
        VdpCharInfo ci;
        if (h.valid && h.x >= P.nx && h.x < P.nx + P.nw && h.y >= P.ny && h.y < P.ny + P.nh &&
            vdpNameTableChar(s, l, h.x - P.nx, h.y - P.ny, -1, &ci)) {
            /* A cell: its character, as on the sheet. Rows 24-31 reading a block the sheet leaves out
            ** stand for the sheet character read from the same bytes (R#4 and R#3 mirror it). */
            onPart = true;
            int ch = ci.ch < 256 * blocks ? ci.ch : sheetCharLike(s, l, ci);
            if (ch >= 0) {
                int cx, cy;
                sheetCell(ch, &cx, &cy);
                Focus hit = { f.chipId, FOCUS_LOGICAL, (UInt32)ci.patternAddr, 8, FOCUS_CHAR, cx, cy, false };
                focusAddr(s, mode, ci.patternAddr, 8, &hit);
                pin(f, hit, h.clicked);
                noteCharPin(f, h.clicked, read);
            }
            else if (h.clicked) {
                shell::releaseSelection();
            }
            if (!f.pinned || h.clicked) { patCol = ci.col; patRow = ci.row; patCellFocus = f; }
            textf(status, sizeof(status), ts(TS_PAT_STATUS_TILE), ci.col, ci.row, ci.nameAddr, ci.ch & 0xff, ci.ch & 0xff, ci.patternAddr);
        }
        else if (h.valid && h.x >= P.sx && h.x < P.sx + P.sw && h.y >= P.sy && h.y < P.sy + P.sh &&
                 vdpSheetChar(s, l, (h.x - P.sx) / P.sdx, h.y - P.sy, &ci)) {
            onPart = true;
            Focus hit = { f.chipId, FOCUS_LOGICAL, (UInt32)ci.patternAddr, 8, FOCUS_CHAR, (h.x - P.sx) / P.sdx, h.y - P.sy, false };
            focusAddr(s, mode, ci.patternAddr, 8, &hit);
            pin(f, hit, h.clicked);
            noteCharPin(f, h.clicked, read);
            if (!f.pinned || h.clicked) patCol = patRow = -1;
            int third = ci.ch >> 8;
            size_t at = (size_t)textf(status, sizeof(status), ts(TS_STATUS_CHAR), ci.ch & 0xff, ci.ch & 0xff,
                                      mode == 2 || mode == 4 ? ts(third == 0 ? TS_THIRD_TOP : third == 1 ? TS_THIRD_MIDDLE : third == 2 ? TS_THIRD_BOTTOM : TS_THIRD_ROWS_24) : "",
                                      ci.patternAddr, ci.colorAddr >= 0 ? ts(TS_STATUS_CHAR_COLOUR) : "");
            if (at < sizeof(status)) textf(status + at, sizeof(status) - at, ts(TS_PAT_MATCHES), patMatches(ci));
        }
        /* The gap between the parts and the room beside them are as the well. */
        if (h.clickedEmpty || (h.clicked && !onPart)) shell::releaseSelection();
        if (status[0]) ui::StatusText("%s", status);
    }

    /* ---- The Sprites tab ---- */

    /* Whether display line d draws sprites from a frame the tab can show. */
    bool sceneShows(int d) const
    {
        if (d < 0 || d >= (int)scene.lineFrame.size() || scene.lineFrame[d] < 0 || scene.lineOff[d]) return false;
        return scene.lineState[d] != SS_NONE && scene.lineState[d] != SS_LOST;
    }

    /* Any displayed line in SCREEN 6 or 7: the placement is 512 dots wide. */
    bool sceneWide() const
    {
        for (const VdpSpriteFrame& fr : scene.frames) {
            if (fr.screenMode == 6 || fr.screenMode == 7) return true;
        }
        return false;
    }

    /* Dots across a sprite of the shown table, unmagnified: from its key, so it holds while the table is away. */
    int sprTileDots() const
    {
        return sprSelSet && sprSel.sizeKey < 32 ? 8 : 16;
    }

    /* The line row's tile scale for an attribute tile scale: whole, so every dot is as wide. */
    static float sprLineScale(float t)
    {
        return fmaxf(floorf(ImGui::GetStyle().FontScaleDpi + 0.5f), floorf(t * 0.75f));
    }

    /* Pattern cells a row in rowW px, tile px across: a power of two up to the sheet's row, so rows start on round numbers. */
    int sprPatCols(float tile, float rowW) const
    {
        int cols = sprTileDots() == 16 ? 16 : 32;
        while (cols > 1 && cols * (tile + ui::Px(4)) - ui::Px(4) > rowW) cols /= 2;
        return cols;
    }

    /* The patterns' two heading lines. */
    static float sprPatTitleH()
    {
        return ImGui::GetTextLineHeight() * 2 + ui::Px(6);
    }

    /* A part's size at a scale, its title included; the patterns wrap in rowW. */
    ImVec2 sprPartSize(int part, float sc, float rowW) const
    {
        float title = ImGui::GetTextLineHeight() + ui::Px(6);
        float lh = ImGui::GetTextLineHeight();
        if (part == SPT_PLACE) return ImVec2(sprBandsW() + 256 * sc, title + scene.lines * sc);
        if (part == SPT_ATTRS) {
            float tile = sprTileDots() * sc, tileL = sprTileDots() * sprLineScale(sc);
            float grid = 4 * (lh + tile + ui::Px(6)), lineRow = ui::Px(10) + lh + ui::Px(4) + lh + tileL + lh;
            return ImVec2(8 * (tile + ui::Px(4)), title + grid + lineRow);
        }
        if (part == SPT_MAG) {
            float grid = sprTileDots() * sc;
            return ImVec2(sprMagLabelW() + grid + ui::Px(8) + ui::Px(14) + ui::Px(44), title + grid + ui::Px(8) + lh * 3);
        }
        if (part == SPT_PATS) {
            float tile = sprTileDots() * sc;
            int cols = sprPatCols(tile, rowW), count = sprTileDots() == 16 ? 64 : 256, rows = (count + cols - 1) / cols;
            return ImVec2(cols * (tile + ui::Px(4)) - ui::Px(4), sprPatTitleH() + rows * sprPatPitch(tile));
        }
        return ImVec2(0, 0);
    }

    /* The heat and count bands left of the placement. */
    static float sprBandsW()
    {
        return ui::Px(6) + ui::Px(2) + ui::Px(24) + ui::Px(4);
    }

    /* The parts at zoom N: placement N px a dot, attributes as wide, patterns as large as the attributes
    ** (wrapped in the width left), the magnifier half as wide; in the widest tier that fits W (all but the
    ** patterns on one row; placement and attributes on one; each under the other, wider than W if need be). */
    SprLayout sprLayoutAt(float W, int N) const
    {
        bool has[SPT_COUNT];
        float sc[SPT_COUNT];
        int size = sprTileDots();
        for (int p = 0; p < SPT_COUNT; p++) has[p] = p == SPT_PLACE || sprSelSet;
        sc[SPT_PLACE] = (float)N;
        sc[SPT_ATTRS] = 32.0f * N / size;
        sc[SPT_MAG]   = fmaxf(6.0f, 128.0f * N / size);
        sc[SPT_PATS]  = sc[SPT_ATTRS];
        float gap = ui::Px(12);
        auto w = [&](int p) { return has[p] ? sprPartSize(p, sc[p], W).x : 0.0f; };
        auto row = [&](int a, int b, int c) {
            float x = 0;
            int n = 0;
            for (int p : { a, b, c }) {
                if (p >= 0 && has[p]) { x += w(p); n++; }
            }
            return x + gap * (n > 1 ? n - 1 : 0);
        };
        SprLayout L = {};
        if (row(SPT_PLACE, SPT_ATTRS, SPT_MAG) <= W) L.tier = 1;
        else if (row(SPT_PLACE, SPT_ATTRS, -1) <= W && w(SPT_MAG) <= W) L.tier = 2;
        else L.tier = 3;
        for (int p = 0; p < SPT_COUNT; p++) {
            L.scale[p] = has[p] ? sc[p] : 0;
            L.size[p] = has[p] ? sprPartSize(p, L.scale[p], W) : ImVec2(0, 0);
        }
        /* Rows of parts, left to right; the patterns beside the magnifier when half a sheet row fits there. */
        std::vector<std::vector<int>> rows;
        float tile = size * sc[SPT_PATS], patW = W, beside = W - L.size[SPT_MAG].x - gap;
        if (L.tier == 1) rows = { { SPT_PLACE, SPT_ATTRS, SPT_MAG }, { SPT_PATS } };
        else if (L.tier == 2 && sprPatCols(tile, beside) >= (size == 16 ? 8 : 16)) {
            rows = { { SPT_PLACE, SPT_ATTRS }, { SPT_MAG, SPT_PATS } };
            patW = beside;
        }
        else if (L.tier == 2) rows = { { SPT_PLACE, SPT_ATTRS }, { SPT_MAG }, { SPT_PATS } };
        else rows = { { SPT_PLACE }, { SPT_ATTRS }, { SPT_MAG }, { SPT_PATS } };
        if (has[SPT_PATS]) {
            L.size[SPT_PATS] = sprPartSize(SPT_PATS, sc[SPT_PATS], patW);
            L.patCols = sprPatCols(tile, patW);
        }
        float y = 0;
        for (const std::vector<int>& r : rows) {
            float x = 0, h = 0;
            for (int p : r) {
                if (!has[p]) continue;
                L.at[p] = ImVec2(x, y);
                x += L.size[p].x + gap;
                h = fmaxf(h, L.size[p].y);
            }
            if (h > 0) {
                y += h + gap;
                L.width = fmaxf(L.width, x - gap);
            }
        }
        L.height = y > 0 ? y - gap : 0;
        /* What Fit fits: every other part and the patterns' first row. */
        for (int p = 0; p < SPT_COUNT; p++) {
            if (has[p]) L.fitH = fmaxf(L.fitH, p == SPT_PATS ? L.at[p].y + sprPatTitleH() + sprPatPitch(tile) : L.at[p].y + L.size[p].y);
        }
        return L;
    }

    /* Fit: the largest whole zoom whose layout fits W, and H down to the patterns' first row; else the
    ** largest that fits W; looked for again only when what the layouts depend on changed. */
    int sprFitZoom(float W, float H)
    {
        float key[7] = { W, H, (float)scene.lines, (float)sprTileDots(), sprSelSet ? 1.0f : 0.0f, ImGui::GetTextLineHeight(), ui::Px(100) };
        if (sprFitN > 0 && memcmp(key, sprFitKey, sizeof(key)) == 0) return sprFitN;
        memcpy(sprFitKey, key, sizeof(key));
        int n = 0;
        for (int pass = 0; pass < 2 && n == 0; pass++) {
            for (int N = 32; N > 1 && n == 0; N--) {
                SprLayout L = sprLayoutAt(W, N);
                if (L.width <= W && (pass == 1 || L.fitH + ui::Px(8) <= H)) n = N;
            }
        }
        sprFitN = n > 0 ? n : 1;
        return sprFitN;
    }

    /* Pictures over the limit per line: a picture is counted once, when it is new and whole. */
    void sprNoteRing(const VdpDebugState* s)
    {
        unsigned ver;
        const VdpFrameShot* o = (const VdpFrameShot*)shell::frameShot(&ver);
        if (o == NULL || !o->valid || !o->ended || o->stale || o->seq == sprRingSeq) return;
        if (s->id != sprRingChip || sprRingLines != scene.lines || (Int32)(o->seq - sprRingSeq) < 0 || o->seq - sprRingSeq > (UInt32)SPR_RING) {
            sprRingLen = sprRingPos = 0;
            memset(sprOverSum, 0, sizeof(sprOverSum));
        }
        sprRingChip = s->id;
        sprRingLines = scene.lines;
        sprRingSeq = o->seq;
        uint8_t bits[30] = {};
        for (int d = 0; d < scene.lines && d < 240; d++) {
            const VdpSpriteFrame* fr = sceneShows(d) ? sceneFrame(d) : NULL;
            if (fr != NULL && fr->line[(d + fr->vscroll) & 0xff].count > fr->limit) bits[d >> 3] |= (uint8_t)(1 << (d & 7));
        }
        if (sprRingLen == SPR_RING) {
            for (int d = 0; d < 240; d++) sprOverSum[d] -= (sprOverRing[sprRingPos][d >> 3] >> (d & 7)) & 1;
        }
        else {
            sprRingLen++;
        }
        if (sprRingLen == 1) sprRingSeqFirst = o->seq;
        memcpy(sprOverRing[sprRingPos], bits, sizeof(bits));
        for (int d = 0; d < 240; d++) sprOverSum[d] += (bits[d >> 3] >> (d & 7)) & 1;
        sprRingPos = (sprRingPos + 1) % SPR_RING;
        sprRingSeqFirst = sprRingLen == SPR_RING ? o->seq - SPR_RING + 1 : sprRingSeqFirst;
    }

    /* The placement picture: each line's sprites from its frame, dropped dots stippled, lines
    ** the shown table does not lay out faint. Uploaded only when its bytes change. */
    void sprRenderPlace(const VdpDebugState* s)
    {
        const UiTokens& t = uiTokens();
        int lines = scene.lines, w = sceneWide() ? 512 : 256;
        char key[64];
        snprintf(key, sizeof(key), "s%u/%u/%05X/%d/%d/%08X/%d", scene.ver, linePal.ver, sprSel.attr, sprSlotSel, (int)sprDropped, (unsigned)t.critical, w);
        if (!sprPlacePic.needsDecode(key)) {
            sprPlacePic.upload(false);
            return;
        }
        sprPlacePic.w = w;
        sprPlacePic.h = lines;
        sprPlacePic.img.assign((size_t)w * lines, 0);
        sprDropMask.assign((size_t)256 * lines, 0);
        std::vector<const UInt32*> rgb((size_t)lines);
        for (int d = 0; d < lines; d++) rgb[d] = lineRgb(d);
        for (int d = 0; d < lines;) {
            int e = d;
            while (e + 1 < lines && scene.lineFrame[e + 1] == scene.lineFrame[d] && sceneShows(e + 1) == sceneShows(d)) e++;
            if (sceneShows(d)) {
                vdpRenderSpriteLines(s, scene.frames[scene.lineFrame[d]], sprPlacePic.img.data(), w, d, e, rgb.data(), sprDropMask.data());
            }
            d = e + 1;
        }
        uint32_t stipple = (t.critical & 0x00ffffffu) | 0xc8000000u;
        for (int d = 0; d < lines; d++) {
            uint32_t* row = &sprPlacePic.img[(size_t)d * w];
            bool faint = sprSelSet && !shownLays(d) && (sprSlotSel < 0 || scene.slots.size() > 1);
            for (int x = 0; x < w; x++) {
                int dot = w == 512 ? x >> 1 : x;
                if (row[x] == 0 && sprDropped && sprDropMask[(size_t)d * 256 + dot] && ((dot + d) & 1) == 0) row[x] = stipple;
                if (faint && row[x] != 0) row[x] = (row[x] & 0x00ffffffu) | ((uint32_t)((row[x] >> 24) * 45 / 100) << 24);
            }
        }
        sprPlacePic.uploadChanged(&sprPlaceHash);
    }

    /* The boxes of sprite n of table attr on the placement, run by run. */
    void sprBox(ImDrawList* dl, ImVec2 img, float p, int attr, int n, ImU32 col, float th) const
    {
        int runX = -1, runY = -1, runW = 0;
        for (int d = 0; d <= scene.lines; d++) {
            int x0 = -1, w = 0;
            const VdpSpriteFrame* fr = d < scene.lines && sceneShows(d) ? sceneFrame(d) : NULL;
            if (fr != NULL && n >= 0 && n < 32 && fr->spr[n].attrAddr == attr) {
                int L = (d + fr->vscroll) & 0xff, row = vdpSpriteRow(*fr, n, L);
                if (row >= 0) x0 = vdpSpriteSpan(*fr, n, row, &w);
            }
            if (runY >= 0 && (x0 != runX || w != runW)) {
                ImVec2 a(img.x + fmaxf(0.0f, (float)runX) * p, img.y + runY * p), b(img.x + fminf(256.0f, (float)(runX + runW)) * p, img.y + d * p);
                if (b.x > a.x) dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), col, 0, th);
                runY = -1;
            }
            if (x0 != -1 || w > 0) {
                if (runY < 0) { runY = d; runX = x0; runW = w; }
            }
        }
    }

    /* The boxes of the hovered or pinned pattern's users, on each line from the table that line shows,
    ** so both buffers of a double buffer light the same object. */
    void sprPatBoxes(const VdpDebugState* s, const Focus& f, ImDrawList* dl, ImVec2 img, float p) const
    {
        const UiTokens& t = uiTokens();
        SprPatKey k;
        bool pin = pinnedPattern(f, &k);
        if (sprPatHot.p < 0 && !pin) return;
        float th = fmaxf(1.0f, ui::Px(1));
        /* Per frame of the scene: the pinned and the hovered pattern, when they are of its table. */
        std::vector<int> pp(scene.frames.size(), -1), hp(scene.frames.size(), -1);
        for (size_t i = 0; i < scene.frames.size(); i++) {
            if (pin && patOf(s, k, scene.frames[i])) pp[i] = k.p;
            if (patOf(s, sprPatHot, scene.frames[i])) hp[i] = sprPatHot.p;
        }
        for (int n = 0; n < 32; n++) {
            int runX = -1, runY = -1, runW = 0, runM = 0;
            for (int d = 0; d <= scene.lines; d++) {
                int x0 = -1, w = 0, mk = 0;
                const VdpSpriteFrame* fr = d < scene.lines && sceneShows(d) ? sceneFrame(d) : NULL;
                int fi = fr != NULL ? scene.lineFrame[d] : -1, pat = fr != NULL && n < fr->stopIndex ? fr->spr[n].pattern & 0xff : -1;
                if (pat >= 0 && (mk = pat == pp[fi] ? 2 : pat == hp[fi] ? 1 : 0) > 0) {
                    int row = vdpSpriteRow(*fr, n, (d + fr->vscroll) & 0xff);
                    if (row >= 0) x0 = vdpSpriteSpan(*fr, n, row, &w);
                }
                if (w == 0) mk = 0;
                if (runY >= 0 && (x0 != runX || w != runW || mk != runM)) {
                    ImVec2 a(img.x + fmaxf(0.0f, (float)runX) * p, img.y + runY * p), b(img.x + fminf(256.0f, (float)(runX + runW)) * p, img.y + d * p);
                    float e = th * runM;
                    if (b.x > a.x) dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), runM == 2 ? t.accent : ui::WithAlpha(t.accent, 200), 0, e);
                    runY = -1;
                }
                if (w > 0 && runY < 0) { runY = d; runX = x0; runW = w; runM = mk; }
            }
        }
    }

    /* The heat and count bands and the placement, at a; hover and pins. */
    void sprDrawPlace(const VdpDebugState* s, Focus& f, ImVec2 at, float p)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        ui::PushFont(UI_FONT_STRONG);
        dl->AddText(at, t.text, ts(TS_SPR_TAB_PLACE));
        float tw = ImGui::CalcTextSize(ts(TS_SPR_TAB_PLACE)).x;
        ui::PopFont();
        dl->AddText(ImVec2(at.x + tw + ui::Px(8), at.y), t.textSecondary, ts(TS_SPR_TAB_PLACE_SUB));
        float y0 = floorf(at.y + lh + ui::Px(6)), heatX = at.x, countX = heatX + ui::Px(6) + ui::Px(2), countW = ui::Px(24);
        ImVec2 img(at.x + sprBandsW(), y0);
        int lines = scene.lines;
        sprRenderPlace(s);
        ImVec2 end(img.x + 256 * p, y0 + lines * p);
        drawCheckerRect(dl, img, end);
        if (sprPlacePic.tex.id()) ui::ImageNearest(dl, sprPlacePic.tex.id(), img, end);
        canvasGrid(sprCv, dl, img, end, p, p);
        /* Heat: pictures over the limit; count: sprites on the line now, the tick at the limit. */
        for (int d = 0; d < lines; d++) {
            float ya = y0 + d * p, yb = fmaxf(y0 + (d + 1) * p, ya + 1);
            if (d < 240 && sprRingLen > 0 && sprOverSum[d] > 0) {
                dl->AddRectFilled(ImVec2(heatX, ya), ImVec2(heatX + ui::Px(6), yb), ui::WithAlpha(t.critical, 60 + 195 * sprOverSum[d] / sprRingLen));
            }
            const VdpSpriteFrame* fr = sceneFrame(d);
            int st = d < (int)scene.lineState.size() ? scene.lineState[d] : SS_NONE;
            if (sceneShows(d)) {
                int c = fr->line[(d + fr->vscroll) & 0xff].count;
                float len = (float)(c < fr->limit * 2 ? c : fr->limit * 2) * countW / (fr->limit * 2);
                if (c > 0) dl->AddRectFilled(ImVec2(countX, ya), ImVec2(countX + len, yb), c > fr->limit ? t.critical : t.textSecondary);
            }
            /* How the line's table is known, at the band's right edge. */
            ImU32 mark = st == SS_APPROX ? t.caution : st == SS_FALLBACK ? ui::WithAlpha(t.textTertiary, 140) : st == SS_LOST || st == SS_NONE ?
                         ui::WithAlpha(t.caution, 120) : (d < (int)scene.lineOff.size() && scene.lineOff[d]) ? ui::WithAlpha(t.textTertiary, 90) : 0;
            if (mark && (fr != NULL || st != SS_FALLBACK)) dl->AddRectFilled(ImVec2(countX + countW - ui::Px(2), ya), ImVec2(countX + countW, yb), mark);
            if (linePal.valid && d < (int)linePal.lineState.size() && linePal.lineState[d] == VDP_SPR_ROW_APPROX) {
                dl->AddRectFilled(ImVec2(countX + countW + ui::Px(1), ya), ImVec2(countX + countW + ui::Px(3), yb), t.caution);
            }
        }
        float tick = floorf(countX + countW / 2);
        dl->AddLine(ImVec2(tick, y0), ImVec2(tick, y0 + lines * p), t.textTertiary, th);

        /* Hover: a sprite on the picture, a line on the bands. */
        ImGui::SetCursorScreenPos(ImVec2(at.x, y0));
        ImGui::InvisibleButton("##place", ImVec2(end.x - at.x, lines * p));
        bool hovered = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        ImVec2 m = ImGui::GetIO().MousePos;
        int d = (int)floorf((m.y - y0) / p), dot = (int)floorf((m.x - img.x) / p);
        bool onBands = m.x < img.x;
        int hit = -1;
        if (hovered && d >= 0 && d < lines) {
            const VdpSpriteFrame* fr = sceneShows(d) ? sceneFrame(d) : NULL;
            int L = fr != NULL ? (d + fr->vscroll) & 0xff : 0;
            hit = fr != NULL && !onBands && dot >= 0 && dot < 256 ? vdpSpriteAt(*fr, L, dot) : -1;
            sprHotNext.d = d;
            sprPlaceLine = d;
            if (hit >= 0) sprSpriteHover(s, f, *fr, hit, L, d, clicked);
            else sprLineStatus(d);
            if (clicked && hit < 0) {
                shell::releaseSelection();
                if (sprLineFixed == d) {
                    /* Let go: no rest on this line holds it again until the mouse moves to another. */
                    sprLineFixed = sprLineHeld = -1;
                    sprDwellLine = d;
                    sprDwellAt = DBL_MAX;
                }
                else {
                    sprLineFixed = sprLineHeld = d;
                }
            }
        }
        /* The fixed line across the bands. */
        if (sprLineFixed >= 0 && sprLineFixed < lines) {
            float fy = floorf(y0 + (sprLineFixed + 0.5f) * p);
            dl->AddLine(ImVec2(at.x, fy), ImVec2(img.x, fy), t.accent, th * 2);
        }
        /* Every sprite's box with Boxes on, as on the Screen tab, in 256-dot units so the edges stay on whole pixels. */
        if (boxes) {
            ovState = s;
            std::vector<BoxRun> runs;
            boxRuns(-1, runs, true);
            for (BoxRun& r : runs) { r.x0 /= 2; r.w /= 2; }
            ui::CanvasXf xf;
            xf.origin = img;
            xf.scale = ImVec2(p, p);
            xf.clipMin = img;
            xf.clipMax = end;
            xf.gutterMin = xf.gutterMax = img;
            drawBoxRuns(dl, xf, runs, -1);
        }
        /* The hovered line, the hovered and pinned sprites, the first collision. */
        dl->PushClipRect(img, end, true);
        if (sprHot.d >= 0 && sprHot.d < lines) {
            dl->AddLine(ImVec2(img.x, y0 + (sprHot.d + 0.5f) * p), ImVec2(end.x, y0 + (sprHot.d + 0.5f) * p), ui::WithAlpha(t.accent, 160), th);
        }
        int pinAttr = pinnedSpriteAttr(f);
        if (pinAttr >= 0) sprBox(dl, img, p, pinAttr, f.a, t.accent, th * 2);
        if (sprHot.n >= 0 && sprHot.frame >= 0 && sprHot.frame < (int)scene.frames.size()) {
            sprBox(dl, img, p, scene.frames[sprHot.frame].spr[sprHot.n].attrAddr, sprHot.n, ui::WithAlpha(t.accent, 200), th);
        }
        sprPatBoxes(s, f, dl, img, p);
        dl->PopClipRect();
        if (scene.collLine >= 0) {
            ImVec2 c(img.x + (scene.collX + 0.5f) * p, y0 + (scene.collLine + 0.5f) * p);
            dl->AddCircle(c, ui::Px(4), t.caution, 0, th * 1.5f);
            if (hovered && fabsf(m.x - c.x) <= ui::Px(5) && fabsf(m.y - c.y) <= ui::Px(5)) {
                ImGui::SetTooltip(ts(TS_KV_FIRST_COLLISION_VALUE), scene.collX, scene.collLine);
            }
        }
    }

    /* The status line for display line d of the tab: its sprites, and how often it went over the limit. */
    void sprLineStatus(int d)
    {
        const VdpSpriteFrame* fr = sceneShows(d) ? sceneFrame(d) : NULL;
        char text[512] = "", heat[160] = "";
        if (fr != NULL) lineSummary(*fr, d + fr->vscroll, fr->limit, text, sizeof(text));
        else if (d < (int)scene.lineState.size() && scene.lineState[d] == SS_FALLBACK) textf(text, sizeof(text), "%s", ts(TS_SPR_TAB_FALLBACK_LINE));
        else textf(text, sizeof(text), "%s", ts(TS_LINE_NO_SPRITES));
        if (d < 240 && sprRingLen > 0) {
            textf(heat, sizeof(heat), ts(TS_SPR_HEAT), sprOverSum[d], sprRingLen, (int)(sprRingSeq - sprRingSeqFirst + 1));
        }
        size_t at = strlen(text);
        if (heat[0] && at < sizeof(text)) at += (size_t)textf(text + at, sizeof(text) - at, " \xC2\xB7 %s", heat);
        if (linePal.valid && d < (int)linePal.lineState.size() && linePal.lineState[d] == VDP_SPR_ROW_APPROX && at < sizeof(text)) {
            textf(text + at, sizeof(text) - at, " \xC2\xB7 %s", ts(TS_SPR_TAB_PAL_APPROX));
        }
        ui::StatusText(ts(TS_GUTTER_LINE), d, text);
    }

    /* A sprite under the mouse on the tab: Output's status line; a click pins it in its table. */
    void sprSpriteHover(const VdpDebugState* s, Focus& f, const VdpSpriteFrame& fr, int n, int L, int d, bool clicked)
    {
        const VdpSprite& sp = fr.spr[n];
        Focus hit = { f.chipId, FOCUS_LOGICAL, (UInt32)sp.attrAddr, 4, FOCUS_SPRITE, n, L, false };
        focusAddr(s, fr.screenMode, sp.attrAddr, 4, &hit);
        pin(f, hit, clicked);
        if (clicked) {
            sprPinned = f.pinned;
            sprPinAttr = sp.attrAddr;
            sprPinLine = d;
            if (f.pinned) sprChoose(frameKey(s, fr));
        }
        sprHotNext.slot = d >= 0 && d < (int)scene.lineSlot.size() ? scene.lineSlot[d] : -1;
        sprHotNext.sel = sprHotNext.slot >= 0 && sprHotNext.slot == sprSlotSel;
        sprHotNext.n = n;
        sprHotNext.frame = (int)(&fr - scene.frames.data());
        int k = 0;
        while (k < fr.line[L].count && fr.line[L].id[k] != n) k++;
        int st = vdpSpriteState(fr, L, k);
        ui::StatusText(ts(TS_STATUS_SPRITE), n, sp.y, sp.x, sp.pattern, vdpSpriteRow(fr, n, L), sp.attrAddr,
                       st == VDP_SPRITE_DROPPED ? ts(TS_STATUS_SPRITE_DROPPED) : st == VDP_SPRITE_CC_HIDDEN ? ts(TS_STATUS_SPRITE_CC_HIDDEN) : "");
    }

    /* Table i of sprSeen by attribute address, the same address told apart by a number. */
    const char* sprTableName(int i, char* out, size_t size) const
    {
        int same = 0, nth = 0;
        for (int j = 0; j < (int)sprSeen.size(); j++) {
            if (sprSeen[j].key.attr != sprSeen[i].key.attr) continue;
            same++;
            if (j <= i) nth++;
        }
        if (same > 1) textf(out, size, "%05X \xC2\xB7 %d", sprSeen[i].key.attr, nth);
        else textf(out, size, "%05X", sprSeen[i].key.attr);
        return out;
    }

    /* The table selector first on the bar: the row's tables in key order, those away a while
    ** dimmed, then "+N" for the other tables seen. False when no table has been seen. */
    bool sprSelector()
    {
        int idx[4], n = 0;
        for (int r = 0; r < sprRowN; r++) {
            int i = sprSeenOf(sprRow[r]);
            if (i >= 0) idx[n++] = i;
        }
        if (n == 0) return false;
        std::sort(idx, idx + n);
        char labels[4][24], lines[64], tip[1024];
        const char* names[4];
        int ids[4];
        size_t at = 0;
        unsigned away = 0;
        int sel = -1;
        tip[0] = 0;
        for (int k = 0; k < n; k++) {
            const SprSeen& e = sprSeen[idx[k]];
            names[k] = sprTableName(idx[k], labels[k], sizeof(labels[k]));
            /* By key, so a click lands on its table while the row changes under the mouse. */
            ids[k] = (int)ImHashData(&e.key, sizeof(e.key));
            if (sprSelSet && sameKey(e.key, sprSel)) sel = k;
            bool old = sprSeenAge(idx[k]) > (UInt32)SPR_AWAY_SHOWN;
            if (old) away |= 1u << k;
            slotLines(e.slot, lines, sizeof(lines));
            if (at > 0 && at < sizeof(tip)) at += (size_t)textf(tip + at, sizeof(tip) - at, "\n");
            if (at >= sizeof(tip)) continue;
            if (old) at += (size_t)textf(tip + at, sizeof(tip) - at, ts(TS_SPR_TAB_TABLE_AWAY_TIP), SPR_AWAY_SHOWN, lines, e.key.attr, e.key.patBase);
            else at += (size_t)textf(tip + at, sizeof(tip) - at, ts(TS_SPR_TAB_TABLE_TIP), lines, e.key.attr, e.key.patBase);
        }
        int pick = sel;
        float w = ui::SegmentedWidth(names, n);
        ui::CommandBarItem(w, ui::SegmentedHeight());
        if (ui::Segmented("tables", &pick, names, n, 0, 0, NULL, away, ids, tip) && pick >= 0 && pick < n) sprChoose(sprSeen[idx[pick]].key);
        int more = (int)sprSeen.size() - n;
        if (more > 0) {
            char label[16], name[24];
            textf(label, sizeof(label), "+%d", more);
            ui::CommandBarItem(ui::DropDownButtonWidth(NULL, label, true));
            if (ui::BeginDropDownButton("more", NULL, label, true)) {
                int chosen = -1;
                for (int i = 0, k = 0; i < (int)sprSeen.size(); i++) {
                    if (k < n && idx[k] == i) { k++; continue; }
                    if (ui::MenuRadio(sprTableName(i, name, sizeof(name)), sprSelSet && sameKey(sprSeen[i].key, sprSel))) chosen = i;
                }
                ui::EndDropDownButton();
                if (chosen >= 0) sprChoose(sprSeen[chosen].key);
            }
        }
        return true;
    }

    SteadyTip sprNotesTip, sprTileTip, sprPatHeld;

    /* Mode, size, limit and patterns of the selected table, then the notes on one line. */
    void sprHeader(const VdpDebugState* s)
    {
        const UiTokens& t = uiTokens();
        const VdpSpriteFrame* fr = sprView.fr;
        char head[256] = "";
        if (scene.slots.empty()) {
            /* No line shows sprites: say why, as the strip does. */
            const char* off = sprOffWhy(sprites);
            textf(head, sizeof(head), ts(TS_SPR_NONE_SHOWN), off != NULL ? off : ts(TS_SPR_OFF_BLANKED));
        }
        else if (fr != NULL) {
            int at = textf(head, sizeof(head), ts(TS_SPR_TAB_HEAD), fr->mode, fr->size == 16 ? "16\xC3\x97" "16" : "8\xC3\x97" "8", fr->limit,
                           sprSel.patBase);
            if (at > 0 && fr->mag == 2) at += textf(head + at, sizeof(head) - at, "%s", ts(TS_SPR_MAGNIFIED));
            if (at > 0 && !vdpIsMsx1(s) && (fr->regs[8] & 0x20)) at += textf(head + at, sizeof(head) - at, "%s", ts(TS_SPR_TAB_TP));
            if (at > 0 && sceneWide()) textf(head + at, sizeof(head) - at, "%s", ts(TS_SPR_TAB_WIDE));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::TextUnformatted(head[0] ? head : " ");
        ImGui::PopStyleColor();
        /* One line, kept even when empty so nothing below moves; the rest of the notes on hover. */
        unsigned ver;
        const VdpFrameShot* o = (const VdpFrameShot*)shell::frameShot(&ver);
        if (ver != sprNoTablesVer) {
            sprNoTablesVer = ver;
            sprNoTables = scene.recorded ? 0 : sprNoTables + 1;
        }
        char notes[6][384];
        int n = 0;
        if (!scene.slots.empty() && sprSelSet && !sprSelKept && sprView.slot < 0) textf(notes[n++], sizeof(notes[0]), ts(TS_SPR_TAB_NOT_KEPT), sprSel.attr);
        if (!scene.recorded && sprNoTables >= 2) textf(notes[n++], sizeof(notes[0]), "%s", ts(o != NULL && o->valid && !o->ended ? TS_SPR_TAB_NOW : TS_SPR_TAB_VBLANK));
        if (scene.approx > 0) textf(notes[n++], sizeof(notes[0]), ts(TS_SPR_TAB_APPROX), scene.approx);
        if (scene.lost > 0) textf(notes[n++], sizeof(notes[0]), ts(TS_SPR_TAB_LOST), scene.lost);
        if (vdpGetNoSpritesLimit()) textf(notes[n++], sizeof(notes[0]), "%s", ts(TS_SPR_TAB_NO_LIMIT));
        if (!vdpGetSpritesEnable()) textf(notes[n++], sizeof(notes[0]), "%s", ts(TS_SPR_NOTE_HIDDEN));
        char line[512] = "", all[2048] = "";
        size_t at = 0;
        if (n > 0) {
            int k = textf(line, sizeof(line), "%s", notes[0]);
            if (n > 1 && k > 0) textf(line + k, sizeof(line) - k, ts(TS_SPR_TAB_MORE_NOTES), n - 1);
        }
        for (int i = 0; i < n && at < sizeof(all); i++) at += (size_t)textf(all + at, sizeof(all) - at, "%s%s", i ? "\n" : "", notes[i]);
        ImGui::PushStyleColor(ImGuiCol_Text, t.caution);
        ImGui::TextUnformatted(line[0] ? line : " ");
        ImGui::PopStyleColor();
        if (n > 1 && ImGui::IsItemHovered()) steadyTooltip(sprNotesTip, ImHashData(&sprSel.attr, sizeof(sprSel.attr), ImGui::GetID("##notes")), all);
    }

    /* The colours of sprite n's rows in frame fr, each from the line it is drawn on (P1). */
    void sprRowRgb(const VdpSpriteFrame& fr, int n, const UInt32* out[16]) const
    {
        for (int r = 0; r < 16; r++) out[r] = r < fr.size ? spriteRowRgb(fr, n, r) : NULL;
    }

    /* Sprites of the selected table whose pattern number changed since the last picture of the same table. */
    void sprNotePatterns()
    {
        if (sprView.fr == NULL || (sprPatVer == scene.ver && sprPatSlotAttr == sprSel.attr)) return;
        sprPatVer = scene.ver;
        sprPatSlotAttr = sprSel.attr;
        const VdpSpriteFrame& fr = *sprView.fr;
        size_t i = 0;
        while (i < sprPrevPat.size() && sprPrevPat[i].attr != sprPatSlotAttr) i++;
        sprPatChanged = 0;
        if (i == sprPrevPat.size()) {
            if (sprPrevPat.size() >= 32) sprPrevPat.erase(sprPrevPat.begin());
            sprPrevPat.push_back(SprPrevPat{ sprPatSlotAttr, {} });
            i = sprPrevPat.size() - 1;
            for (int n = 0; n < 32; n++) sprPrevPat[i].pat[n] = fr.spr[n].pattern;
        }
        for (int n = 0; n < 32; n++) {
            if (sprPrevPat[i].pat[n] != fr.spr[n].pattern && n < fr.stopIndex) sprPatChanged |= 1u << n;
            sprPrevPat[i].pat[n] = fr.spr[n].pattern;
        }
    }

    /* Tiles into pic, n of them side by side, each its rows coloured by its lines. */
    void sprRenderTiles(const VdpDebugState* s, const VdpSpriteFrame& fr, const int* ids, int count, int cols, ViewPicture& pic, unsigned* hash)
    {
        int size = fr.size, rows = (count + cols - 1) / cols;
        pic.w = cols * size;
        pic.h = (rows > 0 ? rows : 1) * size;
        pic.img.assign((size_t)pic.w * pic.h, 0);
        for (int k = 0; k < count; k++) {
            const UInt32* rgb[16];
            sprRowRgb(fr, ids[k], rgb);
            vdpRenderSpriteTile(s, fr, ids[k], rgb, &pic.img[(size_t)(k / cols) * size * pic.w + (k % cols) * size], pic.w);
        }
        pic.uploadChanged(hash);
    }

    /* The first display line pattern row r of sprite n of frame fr is drawn on, or -1. */
    static int sprRowLine(const VdpSpriteFrame& fr, int n, int r)
    {
        return vdpSpriteDisplayLine(fr, (fr.spr[n].y + 1 + r * fr.mag) & 0xff);
    }

    /* What line d does to sprite n: VdpSpriteState, or -1 when the sprite is not in range there. */
    int sprStateOn(int d, int n) const
    {
        const VdpSpriteFrame* fr = sceneShows(d) ? sceneFrame(d) : NULL;
        if (fr == NULL) return -1;
        int L = (d + fr->vscroll) & 0xff, k = 0;
        const VdpSpriteLine& ln = fr->line[L];
        while (k < ln.count && ln.id[k] != n) k++;
        return k < ln.count ? vdpSpriteState(*fr, L, k) : -1;
    }

    /* Pins sprite n of table fr as clicked on the tab; d its line, -1 when it has none. */
    void sprPinTile(const VdpDebugState* s, Focus& f, const VdpSpriteFrame& fr, int n, int d, bool clicked)
    {
        const VdpSprite& sp = fr.spr[n];
        Focus hit = { f.chipId, FOCUS_LOGICAL, (UInt32)sp.attrAddr, 4, FOCUS_SPRITE, n, (sp.y + 1) & 0xff, false };
        focusAddr(s, fr.screenMode, sp.attrAddr, 4, &hit);
        pin(f, hit, clicked);
        if (clicked) {
            sprPinned = f.pinned;
            sprPinAttr = sp.attrAddr;
            sprPinLine = d;
            if (f.pinned) sprChoose(frameKey(s, fr));
        }
    }

    /* The 32 attributes of the selected table as pictures, what happens to each row drawn
    ** over them; under them the hovered line's sprites in the order the VDP checks them. */
    void sprDrawAttrs(const VdpDebugState* s, Focus& f, ImVec2 at, float t, float partW)
    {
        const UiTokens& t_ = uiTokens();
        if (sprView.fr == NULL) return;
        const VdpSpriteFrame& fr = *sprView.fr;
        bool versions = sprSlotSel >= 0 ? scene.slots[sprSlotSel].versions : sprSelSlot.versions;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight(), tile = fr.size * t, cellW = tile + ui::Px(4);
        ui::PushFont(UI_FONT_STRONG);
        dl->AddText(at, t_.text, ts(TS_SPR_TAB_ATTRS));
        ui::PopFont();
        int ids[32];
        for (int n = 0; n < 32; n++) ids[n] = n;
        sprNotePatterns();
        sprRenderTiles(s, fr, ids, 32, 8, sprAtlasPic, &sprAtlasHash);
        int pinAttr = pinnedSpriteAttr(f), pinN = pinAttr >= 0 ? f.a : -1;
        bool pinHere = pinAttr >= 0 && (pinAttr & ~0x7f) == sprSel.attr;
        float y0 = floorf(at.y + lh + ui::Px(6));
        for (int n = 0; n < 32; n++) {
            float x = at.x + (n % 8) * cellW, y = y0 + floorf((n / 8) * (lh + tile + ui::Px(6)));
            ImVec2 a(x, y + lh), b(x + tile, y + lh + tile);
            const VdpSprite& sp = fr.spr[n];
            const SprAgg& ag = sprView.agg[n];
            bool hot = sprHot.n == n && sprHot.sel, pinned = pinHere && pinN == n;
            char num[24];
            snprintf(num, sizeof(num), "%d", n);
            dl->AddText(ImVec2(x, y), hot || pinned ? t_.accent : n > fr.stopIndex ? t_.textTertiary : t_.textSecondary, num);
            if ((sprPatChanged >> n) & 1) dl->AddCircleFilled(ImVec2(x + ImGui::CalcTextSize(num).x + ui::Px(5), y + lh / 2), ui::Px(2), t_.accent);
            if (n > fr.stopIndex) {
                /* Past the terminator the VDP reads nothing: a small grey tag. */
                float q = tile * 0.3f;
                dl->AddRect(ImVec2(a.x + q, a.y + q), ImVec2(b.x - q, b.y - q), t_.textTertiary, 0, th);
                continue;
            }
            drawCheckerRect(dl, a, b);
            if (sprAtlasPic.tex.id() && n < fr.stopIndex) {
                ImVec2 uv0((float)(n % 8) / 8, (float)(n / 8) / 4), uv1((float)(n % 8 + 1) / 8, (float)(n / 8 + 1) / 4);
                ui::ImageNearest(dl, sprAtlasPic.tex.id(), a, b, uv0, uv1);
            }
            if (n < fr.stopIndex) canvasGrid(sprCv, dl, a, b, t, t);
            if (n == fr.stopIndex) {
                /* The terminator: a red bar, "End" and its Y. */
                dl->AddRectFilled(ImVec2(b.x - ui::Px(3), a.y), b, t_.critical);
                char yv[16];
                snprintf(yv, sizeof(yv), "Y=%d", fr.stopY);
                dl->PushClipRect(a, b, true);
                dl->AddText(ImVec2(a.x + ui::Px(2), a.y), t_.critical, ts(TS_SPR_TAB_END));
                dl->AddText(ImVec2(a.x + ui::Px(2), a.y + lh), t_.textTertiary, yv);
                dl->PopClipRect();
            }
            else {
                /* Each pattern row by the line it is drawn on. */
                bool any = false, ec = false, cc = false, offScreen = true;
                for (int r = 0; r < fr.size; r++) {
                    int c = sp.color[r], st = sprRowState(fr, sprSlotSel, n, r);
                    any = any || (vdpSpriteRowBits(s, fr, n, r) != 0 && !vdpSpriteClear(s, fr, c));
                    ec = ec || (c & 0x80);
                    cc = cc || (fr.mode == 2 && (c & 0x40));
                    ImVec2 ra(a.x, a.y + r * t), rb(b.x, a.y + (r + 1) * t);
                    if (st != -3) offScreen = false;
                    if (st == -3 || st == -4) {
                        dl->AddRectFilled(ra, rb, ui::WithAlpha(t_.layer, st == -3 ? 150 : 120));
                        continue;
                    }
                    if (st == VDP_SPRITE_DROPPED) {
                        dl->AddRectFilled(ra, rb, ui::WithAlpha(t_.critical, 70));
                        hatch(dl, ra, rb, ui::WithAlpha(t_.critical, 200));
                    }
                    else if (st == VDP_SPRITE_CC_HIDDEN) {
                        dl->AddRectFilled(ra, rb, ui::WithAlpha(t_.layer, 170));
                    }
                }
                if (!any) {
                    dashedRect(dl, a, b, t_.textTertiary, th);
                    dl->PushClipRect(a, b, true);
                    dl->AddText(ImVec2(a.x + ui::Px(2), a.y + (tile - lh) / 2), t_.textTertiary, ts(TS_SPR_TAB_COLOUR0));
                    dl->PopClipRect();
                }
                /* Tags in words on a roomy tile; on a small one a bar each: EC left, CC and off screen below. */
                ui::PushFont(UI_FONT_BODY, 11);
                float tagH = ImGui::GetTextLineHeight();
                bool words = tile >= ImGui::CalcTextSize("\xE2\x86\x90" "32 CC").x + ui::Px(4) && tile >= tagH * 2;
                dl->PushClipRect(a, b, true);
                if (words) {
                    if (offScreen) dl->AddText(ImVec2(b.x - ImGui::CalcTextSize("\xE2\x86\x93").x - ui::Px(1), a.y), t_.textSecondary, "\xE2\x86\x93");
                    if (ec) dl->AddText(ImVec2(a.x + ui::Px(1), b.y - tagH), t_.caution, "\xE2\x86\x90" "32");
                    if (cc) dl->AddText(ImVec2(b.x - ImGui::CalcTextSize("CC").x - ui::Px(1), b.y - tagH), t_.textSecondary, "CC");
                }
                else {
                    if (ec) dl->AddRectFilled(a, ImVec2(a.x + ui::Px(2), b.y), t_.caution);
                    if (cc) dl->AddRectFilled(ImVec2(a.x, b.y - ui::Px(2)), ImVec2(b.x, b.y), t_.textSecondary);
                    if (offScreen) dl->AddRectFilled(ImVec2(b.x - ui::Px(2), a.y), b, t_.textTertiary);
                }
                dl->PopClipRect();
                ui::PopFont();
                if (ag.collides) dl->AddCircleFilled(ImVec2(b.x - ui::Px(3), a.y + ui::Px(3)), ui::Px(2.5f), t_.caution);
            }
            /* The sprite pinned or hovered, else a user of the pattern pinned (thick) or hovered. */
            int lit = pinned ? 2 : hot ? 1 : sprPatMark(s, f, fr, n);
            if (lit) dl->AddRect(ImVec2(a.x - th * lit, a.y - th * lit), ImVec2(b.x + th * lit, b.y + th * lit), t_.accent, 0, th * lit);

            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(n);
            ImGui::InvisibleButton("##tile", ImVec2(tile, tile));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                char text[512];
                int at2 = 0;
                spriteStateText(fr, &ag, n, text, sizeof(text));
                at2 = (int)strlen(text);
                if (((sprPatChanged >> n) & 1) && at2 < (int)sizeof(text)) at2 += textf(text + at2, sizeof(text) - at2, "%s", ts(TS_SPR_TAB_CHANGED));
                if (versions && at2 < (int)sizeof(text)) textf(text + at2, sizeof(text) - at2, "%s", ts(TS_SPR_TAB_VERSIONS));
                steadyTooltip(sprTileTip, ImHashData(&sprSel.attr, sizeof(sprSel.attr), ImGui::GetItemID()), text);
                int d0 = -1;
                for (int r = 0; r < fr.size * fr.mag && d0 < 0; r++) {
                    int d = vdpSpriteDisplayLine(fr, (sp.y + 1 + r) & 0xff);
                    if (shownLays(d)) d0 = d;
                }
                sprHotNext.slot = sprSlotSel;
                sprHotNext.n = n;
                sprHotNext.frame = sprSlotSel >= 0 ? scene.slots[sprSlotSel].rep : SPR_KEPT;
                sprHotNext.sel = true;
                sprPinTile(s, f, fr, n, d0, ImGui::IsItemClicked(ImGuiMouseButton_Left));
                ui::StatusText(ts(TS_STATUS_SPRITE), n, sp.y, sp.x, sp.pattern, 0, sp.attrAddr, "");
            }
        }
        sprDrawLineRow(s, f, ImVec2(at.x, floorf(y0 + 4 * (lh + tile + ui::Px(6)) + ui::Px(10))), sprLineScale(t), partW);
    }

    /* The line row forgets the lines it held: a table picked, the chip, machine, state or line count changed. */
    void lineRowReset()
    {
        sprLineFixed = sprLineHeld = sprLineAuto = sprDwellLine = -1;
        sprLineAutoAt = sprDwellAt = 0;
        sprLineAutoGone = -1;
    }

    /* The shown table's frame for display line d: this picture's, else the kept one; NULL when it does not lay d out. */
    const VdpSpriteFrame* rowFrame(int d) const
    {
        if (d < 0 || d >= scene.lines || !shownLays(d)) return NULL;
        return sprSlotSel >= 0 ? (sceneShows(d) ? sceneFrame(d) : NULL) : sprSelKeep;
    }

    /* How many sprites display line d has in range, -1 when it shows none of the shown table's;
    ** over, whether that is more than the limit. */
    int lineRowCount(int d, bool* over = NULL) const
    {
        const VdpSpriteFrame* lf = rowFrame(d);
        int c = lf != NULL ? lf->line[(d + lf->vscroll) & 0xff].count : -1;
        if (over) *over = lf != NULL && c > lf->limit;
        return c;
    }

    /* The line row's line and why: the placement line under the mouse, else the one a click fixed,
    ** else the one it last rested on for a quarter second, else the pinned sprite's, else the
    ** selected table's busiest line over the limit or else at all, picked again at most once a second. */
    int lineRowPick(const Focus& f, int* why)
    {
        double now = ImGui::GetTime();
        int pinAttr = pinnedSpriteAttr(f);
        if (pinAttr != sprRowPin && pinAttr >= 0) sprLineFixed = sprLineHeld = -1;  /* a new pin shows its own line */
        sprRowPin = pinAttr;
        if (sprPlaceLine >= 0) {
            if (sprPlaceLine != sprDwellLine) { sprDwellLine = sprPlaceLine; sprDwellAt = now; }
            else if (now - sprDwellAt >= 0.25) sprLineHeld = sprPlaceLine;
            *why = ROW_HOVER;
            return sprPlaceLine;
        }
        sprDwellLine = -1;
        if (sprLineFixed >= 0 && sceneShows(sprLineFixed)) { *why = ROW_FIXED; return sprLineFixed; }
        if (sprLineHeld >= 0 && sceneShows(sprLineHeld)) { *why = ROW_HELD; return sprLineHeld; }
        /* The pinned sprite's line, from the kept sprites while its table is the shown one away. */
        int slot = slotOfAttr(pinAttr, sprPinLine);
        const SprAgg* pag = slot >= 0 ? &scene.agg[(size_t)slot * 32 + f.a] : NULL;
        if (pag == NULL && pinAttr >= 0 && sprSlotSel < 0 && sprSelKept && (pinAttr & ~0x7f) == sprSel.attr) pag = &sprSelAgg[f.a];
        if (pag != NULL) {
            *why = pag->firstDropped >= 0 ? ROW_PIN_DROP : ROW_PIN;
            return pag->firstDropped >= 0 ? pag->firstDropped : sprPinLine;
        }
        *why = sprLineAutoOver ? ROW_OVER : ROW_BUSIEST;
        if (sprView.fr == NULL) return -1;
        /* The shown table away from the line for a quarter second: picked again at once. */
        if (sprLineAuto >= 0 && sprLineAuto < scene.lines && now - sprLineAutoAt < 1.0) {
            if (lineRowCount(sprLineAuto) >= 0) sprLineAutoGone = -1;
            else if (sprLineAutoGone < 0) sprLineAutoGone = now;
            if (sprLineAutoGone < 0 || now - sprLineAutoGone < 0.25) return sprLineAuto;
        }
        sprLineAutoGone = -1;
        /* Ranked by (over the limit, count). */
        int line = -1, most = -1;
        bool mostOver = false, over;
        for (int d = 0; d < scene.lines; d++) {
            int c = lineRowCount(d, &over);
            if ((over && !mostOver) || (over == mostOver && c > most)) { most = c; mostOver = over; line = d; }
        }
        if (most >= 0 && lineRowCount(sprLineAuto, &over) == most && over == mostOver) line = sprLineAuto;  /* a tie keeps the line */
        sprLineAuto = line;
        sprLineAutoOver = mostOver;
        *why = mostOver ? ROW_OVER : ROW_BUSIEST;
        sprLineAutoAt = now;
        return line;
    }

    /* The sprites in range of the line lineRowPick gives, in the order the VDP checks them,
    ** with why in the caption. A fence at the limit. */
    void sprDrawLineRow(const VdpDebugState* s, Focus& f, ImVec2 at, float t, float partW)
    {
        const UiTokens& t_ = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        int why = ROW_NONE, line = lineRowPick(f, &why);
        /* The shown table's sprites where it lays the line out, also while away; else the picture's. */
        const VdpSpriteFrame* fr = rowFrame(line);
        bool other = fr == NULL;
        if (fr == NULL) fr = sceneShows(line) ? sceneFrame(line) : NULL;
        if (fr == NULL) return;
        int L = (line + fr->vscroll) & 0xff, count = fr->line[L].count;
        static const ToolStringId whyText[] = { TS_SPR_TAB_WHY_HOVER, TS_SPR_TAB_WHY_FIXED, TS_SPR_TAB_WHY_HELD, TS_SPR_TAB_WHY_PIN_DROP,
                                                TS_SPR_TAB_WHY_PIN, TS_SPR_TAB_WHY_OVER, TS_SPR_TAB_WHY_BUSIEST };
        char cap[256];
        int at2 = textf(cap, sizeof(cap), ts(TS_SPR_TAB_LINE), line, count);
        if (why > ROW_NONE && at2 > 0 && at2 < (int)sizeof(cap)) at2 += textf(cap + at2, sizeof(cap) - at2, "%s", ts(whyText[why - 1]));
        if (other && at2 > 0 && at2 < (int)sizeof(cap)) textf(cap + at2, sizeof(cap) - at2, ts(TS_SPR_TAB_LINE_TABLE), fr->spr[0].attrAddr);
        bool overCap = ImGui::ItemAdd(ImRect(at, ImVec2(at.x + partW, at.y + lh)), 0) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        ui::TextFit(dl, at, at.x + partW, cap, t_.textSecondary, overCap);
        int ids[32];
        for (int k = 0; k < count; k++) ids[k] = fr->line[L].id[k];
        if (count > 0) sprRenderTiles(s, *fr, ids, count, count, sprLinePic, &sprLineHash);
        float tile = fr->size * t, cellW = ceilf(fmaxf(tile, ImGui::CalcTextSize("#31").x)) + ui::Px(4), fence = floorf(ui::Px(14));
        float y = floorf(at.y + lh + ui::Px(4) + lh);
        dl->PushClipRect(ImVec2(at.x, at.y), ImVec2(at.x + partW, y + tile + lh + ui::Px(2)), true);
        /* Only whole tiles, each hoverable; the rest are counted as "+N" at the end. */
        float right = at.x + partW;
        auto tileX = [&](int k) { return at.x + k * cellW + (k >= fr->limit ? fence : 0); };
        int shown = count;
        while (shown > 0 && tileX(shown - 1) + tile > right) shown--;
        char more[16] = "";
        if (shown < count) {
            for (;;) {
                snprintf(more, sizeof(more), "+%d", count - shown);
                if (shown == 0 || tileX(shown - 1) + cellW + ImGui::CalcTextSize(more).x <= right) break;
                shown--;
            }
        }
        for (int k = 0; k < shown; k++) {
            int n = ids[k], row = vdpSpriteRow(*fr, n, L);
            float x = tileX(k);
            ImVec2 a(x, y), b(x + tile, y + tile);
            drawCheckerRect(dl, a, b);
            if (sprLinePic.tex.id()) ui::ImageNearest(dl, sprLinePic.tex.id(), a, b, ImVec2((float)k / count, 0), ImVec2((float)(k + 1) / count, 1));
            canvasGrid(sprCv, dl, a, b, t, t);
            if (row >= 0) dl->AddLine(ImVec2(a.x, a.y + (row + 0.5f) * t), ImVec2(b.x, a.y + (row + 0.5f) * t), t_.accent, th);
            if (sprPatMark(s, f, *fr, n)) dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t_.accent, 0, th);
            if (k >= fr->limit) {
                dl->AddLine(a, b, t_.critical, th * 1.5f);
                dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), t_.critical, th * 1.5f);
            }
            char num[16];
            snprintf(num, sizeof(num), "#%d", n);
            dl->AddText(ImVec2(x, b.y + ui::Px(1)), k >= fr->limit ? t_.critical : t_.textSecondary, num);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(100 + k);
            ImGui::InvisibleButton("##ltile", ImVec2(tile, tile));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                sprHotNext.slot = other ? scene.lineSlot[line] : sprSlotSel;
                sprHotNext.n = n;
                sprHotNext.d = line;
                sprHotNext.frame = fr == sprSelKeep ? SPR_KEPT : scene.lineFrame[line];
                sprHotNext.sel = !other;
                sprPinTile(s, f, *fr, n, line, ImGui::IsItemClicked(ImGuiMouseButton_Left));
                lineSummary(*fr, L, fr->limit, cap, sizeof(cap));
                ui::StatusText(ts(TS_GUTTER_LINE), line, cap);
            }
        }
        if (more[0]) {
            float mx = shown > 0 ? tileX(shown - 1) + cellW : at.x;
            dl->AddText(ImVec2(mx, floorf(y + (tile - lh) / 2)), count > fr->limit ? t_.critical : t_.textSecondary, more);
        }
        if (count > fr->limit && fr->limit < shown) {
            /* The fence: the VDP stops drawing here. */
            float fx = at.x + fr->limit * cellW + fence / 2 - ui::Px(2);
            dl->AddLine(ImVec2(fx, y - lh), ImVec2(fx, y + tile), t_.critical, th * 2);
            char lim[64];
            textf(lim, sizeof(lim), ts(TS_SPR_TAB_FENCE), fr->limit);
            dl->AddText(ImVec2(fx + ui::Px(3), y - lh - ui::Px(2)), t_.critical, lim);
        }
        dl->PopClipRect();
    }

    /* The line labels' column left of the magnified sprite. */
    static float sprMagLabelW()
    {
        return ui::Px(30);
    }

    /* Why sprite n of fr is or is not drawn, in one sentence; slot is fr's, -1 when it is away. */
    void sprWhy(const VdpDebugState* s, const VdpSpriteFrame& fr, int n, int slot, char* out, size_t size) const
    {
        const VdpSprite& sp = fr.spr[n];
        int onScreen = 0, own = 0, dropFrom = -1, dropTo = -1, dropD0 = -1, dropD1 = -1, cc = 0, any = 0, bits = 0;
        for (int r = 0; r < fr.size; r++) {
            int d = sprRowLine(fr, n, r), b = vdpSpriteRowBits(s, fr, n, r), st = sprRowState(fr, slot, n, r);
            bits |= b;
            any |= b != 0 && !vdpSpriteClear(s, fr, sp.color[r]);
            if (st == -3) continue;
            onScreen++;
            if (st == -4) continue;
            own++;
            if (st == VDP_SPRITE_DROPPED) {
                if (dropFrom < 0) { dropFrom = r; dropD0 = d; }
                dropTo = r;
                dropD1 = d;
            }
            if (st == VDP_SPRITE_CC_HIDDEN) cc++;
        }
        int at;
        if (n == fr.stopIndex) at = textf(out, size, ts(TS_SPR_WHY_TERMINATOR), fr.stopY);
        else if (sp.afterStop) at = textf(out, size, "%s", ts(TS_SPR_WHY_AFTER));
        else if (fr.off) at = textf(out, size, ts(TS_SPR_WHY_OFF), fr.off);
        else if (onScreen == 0) at = textf(out, size, "%s", ts(TS_SPR_WHY_OFF_SCREEN));
        else if (own == 0) at = textf(out, size, "%s", ts(TS_SPR_WHY_OTHER_TABLE));
        else if (dropFrom >= 0) at = textf(out, size, ts(TS_SPR_WHY_DROPPED), dropFrom, dropTo, dropD0, dropD1, fr.limit);
        else if (cc > 0) at = textf(out, size, "%s", ts(TS_SPR_WHY_CC));
        else if (bits == 0) at = textf(out, size, "%s", ts(TS_SPR_WHY_EMPTY));
        else if (!any) at = textf(out, size, "%s", ts(TS_SPR_WHY_COLOUR0));
        else at = textf(out, size, "%s", ts(TS_SPR_WHY_DRAWN));
        bool coll = slot >= 0 ? scene.agg[(size_t)slot * 32 + n].collides : &fr == sprSelKeep && sprSelKept && sprSelAgg[n].collides;
        if (coll && at > 0 && (size_t)at < size) textf(out + at, size - at, "%s", ts(TS_SPR_WHY_COLLIDES));
    }

    /* The hovered sprite or pattern, else the pinned one; a sprite row by row: the line at each change
    ** of state, its dots, each row's colour and flags, and why it is or is not drawn. */
    void sprDrawMag(const VdpDebugState* s, const Focus& f, ImVec2 at, float m, float partW)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        const VdpSpriteFrame* fr = NULL;
        int n = -1, slot = -1, pinAway = 0;
        SprPatKey pk;
        if (sprHot.n >= 0 && sprHot.frame >= 0 && sprHot.frame < (int)scene.frames.size()) {
            fr = &scene.frames[sprHot.frame];
            n = sprHot.n;
            slot = sprHot.slot;
        }
        else if (sprHot.n >= 0 && sprHot.frame == SPR_KEPT && sprSelKept) {
            fr = sprSelKeep;
            n = sprHot.n;
        }
        else if (sprPatHot.p >= 0) {
            sprDrawMagPattern(s, sprPatHot, at, m, partW);
            return;
        }
        else if (pinnedPattern(f, &pk)) {
            sprDrawMagPattern(s, pk, at, m, partW);
            return;
        }
        else if (pinnedSpriteAttr(f) >= 0 && f.a >= 0 && f.a < 32) {
            n = f.a;
            slot = slotOfAttr(pinnedSpriteAttr(f), sprPinLine);
            /* Away: the shown table's kept rows when it is the pin's, so the swaps do not show. */
            if (slot >= 0) fr = pinFrame(f, &slot);
            else if (sprSelKept && (pinnedSpriteAttr(f) & ~0x7f) == sprSel.attr) { fr = sprSelKeep; pinAway = (int)(sprSceneSeq - sprSelSeenSeq); }
            else if (sprPinKeep != NULL && sprPinKeepAttr == pinnedSpriteAttr(f)) { fr = sprPinKeep; pinAway = (int)(sprSceneSeq - sprPinSeenSeq); }
        }
        if (fr == NULL || n < 0) {
            ImGui::SetCursorScreenPos(at);
            ImGui::PushTextWrapPos(at.x + partW);
            note(ts(TS_SPR_TAB_MAG_HINT));
            ImGui::PopTextWrapPos();
            return;
        }
        const VdpSprite& sp = fr->spr[n];
        int d0 = -1, d1 = -1;
        for (int r = 0; r < fr->size * fr->mag; r++) {
            int d = vdpSpriteDisplayLine(*fr, (sp.y + 1 + r) & 0xff);
            if (d < 0) continue;
            if (d0 < 0) d0 = d;
            d1 = d;
        }
        char head[256];
        int hl = d0 >= 0 ? textf(head, sizeof(head), ts(TS_SPR_TAB_MAG), n, sp.pattern, sp.x, d0, d1) : textf(head, sizeof(head), ts(TS_SPR_TAB_MAG_OFF), n, sp.pattern, sp.x);
        if (slot >= 0 && slot != sprSlotSel && hl > 0) hl += textf(head + hl, sizeof(head) - hl, ts(TS_SPR_TAB_LINE_TABLE), fr->spr[0].attrAddr);
        ui::PushFont(UI_FONT_STRONG);
        dl->AddText(at, t.text, head);
        ui::PopFont();
        if (slot < 0 && pinAway > SPR_AWAY_SHOWN) {
            char old[128];
            textf(old, sizeof(old), ts(TS_SPR_PIN_OLD), SPR_AWAY_SHOWN);
            dl->AddText(ImVec2(at.x, at.y + lh), t.caution, old);
        }
        float y0 = floorf(at.y + lh + ui::Px(6)), gx = floorf(at.x + sprMagLabelW()), size = (float)fr->size;
        ImVec2 ga(gx, y0), gb(gx + size * m, y0 + size * m);
        int prevSt = -2;
        for (int r = 0; r < fr->size; r++) {
            int d = sprRowLine(*fr, n, r), c = sp.color[r], bits = vdpSpriteRowBits(s, *fr, n, r);
            int st = sprRowState(*fr, slot, n, r);
            float ry = y0 + r * m;
            ImU32 col = rgbaToCol(vdpSpriteRgbaFrom(s, spriteRowRgb(*fr, n, r), *fr, c));
            bool clear = vdpSpriteClear(s, *fr, c);
            for (int x = 0; x < fr->size; x++) {
                ImVec2 a(gx + x * m, ry), b(a.x + m, ry + m);
                if ((bits & (0x8000 >> x)) && !clear) dl->AddRectFilled(a, b, col);
                else dl->AddRectFilled(a, b, ((x + r) & 1) ? t.checkerA : t.checkerB);
            }
            if (st == VDP_SPRITE_DROPPED) hatch(dl, ImVec2(gx, ry), ImVec2(gb.x, ry + m), ui::WithAlpha(t.critical, 200));
            else if (st == VDP_SPRITE_CC_HIDDEN || st == -3 || st == -4) dl->AddRectFilled(ImVec2(gx, ry), ImVec2(gb.x, ry + m), ui::WithAlpha(t.layer, 150));
            /* The line where the state changes, at the left. */
            if (st != prevSt && d >= 0) {
                char num[16];
                snprintf(num, sizeof(num), "%d", d);
                dl->AddText(ImVec2(at.x, ry + (m - lh) / 2), st == VDP_SPRITE_DROPPED ? t.critical : t.textSecondary, num);
            }
            prevSt = st;
            /* The row's colour and its flags at the right. */
            float sx = gb.x + ui::Px(8);
            swatchRect(dl, ImVec2(sx, ry + 1), ImVec2(sx + ui::Px(14), ry + m - 1), s, *fr, c, spriteRowRgb(*fr, n, r));
            char tag[24] = "";
            size_t tl = 0;
            if (c & 0x80) tl += (size_t)snprintf(tag + tl, sizeof(tag) - tl, "\xE2\x86\x90" "32 ");
            if (fr->mode == 2 && (c & 0x40)) tl += (size_t)snprintf(tag + tl, sizeof(tag) - tl, "CC ");
            if (st == VDP_SPRITE_DROPPED && tl < sizeof(tag)) snprintf(tag + tl, sizeof(tag) - tl, "\xC3\x97");
            if (tag[0] && m >= lh * 0.8f) dl->AddText(ImVec2(sx + ui::Px(18), ry + (m - lh) / 2), st == VDP_SPRITE_DROPPED ? t.critical : t.textSecondary, tag);
        }
        canvasGrid(sprCv, dl, ga, gb, m, m);
        dl->AddRect(ga, gb, t.controlStrongStroke, 0, th);
        char why[512];
        sprWhy(s, *fr, n, slot, why, sizeof(why));
        ImGui::SetCursorScreenPos(ImVec2(at.x, gb.y + ui::Px(8)));
        ImGui::PushTextWrapPos(at.x + partW);
        ImGui::TextUnformatted(why);
        ImGui::PopTextWrapPos();
    }

    /* Pattern k in the magnifier: its dots in its owner's colours, and a token per user with its colours
    ** (hollow when that colour was not drawn lately); only a note when the shown table reads other patterns. */
    void sprDrawMagPattern(const VdpDebugState* s, const SprPatKey& k, ImVec2 at, float m, float partW)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        const VdpSpriteFrame* fr = sprView.fr;
        bool here = fr != NULL && patOf(s, k, *fr);
        SprPatUse u;
        if (here) sprPatUse(s, *fr, u);
        int users = here ? u.users[k.p] : 0;
        char lab[16], head[256], who[64];
        if (k.size == 16) snprintf(lab, sizeof(lab), "%d-%d", k.p, k.p + 3);
        else snprintf(lab, sizeof(lab), "%d", k.p);
        int hl = textf(head, sizeof(head), ts(TS_SPR_PAT_MAG), lab, k.p, k.base + k.p * 8);
        if (here && hl > 0 && hl < (int)sizeof(head)) {
            if (users == 1) textf(who, sizeof(who), "%s", ts(TS_SPR_PAT_ONE_SPRITE));
            else textf(who, sizeof(who), ts(TS_SPR_PAT_SPRITES), users);
            textf(head + hl, sizeof(head) - hl, " \xC2\xB7 %s", who);
        }
        ui::PushFont(UI_FONT_STRONG);
        dl->AddText(at, t.text, head);
        ui::PopFont();
        if (!here) {
            char why[256];
            textf(why, sizeof(why), ts(TS_SPR_PAT_OTHER_BASE), sprSel.patBase);
            dl->PushClipRect(at, ImVec2(at.x + partW, at.y + lh * 2), true);
            dl->AddText(ImVec2(at.x, at.y + lh), t.caution, why);
            dl->PopClipRect();
            return;
        }
        int o = u.owner[k.p];
        float y0 = floorf(at.y + lh + ui::Px(6)), gx = floorf(at.x + sprMagLabelW()), size = (float)k.size;
        ImVec2 ga(gx, y0), gb(gx + size * m, y0 + size * m);
        ImU32 grey = ui::WithAlpha(t.textTertiary, 0xa0);
        for (int r = 0; r < k.size; r++) {
            int bits = vdpSpritePatternRowBits(s, *fr, k.p, r), c = o >= 0 ? fr->spr[o].color[r] : 0;
            ImU32 col = o >= 0 ? rgbaToCol(vdpSpriteRgbaFrom(s, spriteRowRgb(*fr, o, r), *fr, c)) : grey;
            bool clear = o >= 0 && vdpSpriteClear(s, *fr, c);
            for (int x = 0; x < k.size; x++) {
                ImVec2 a(gx + x * m, y0 + r * m), b(a.x + m, a.y + m);
                dl->AddRectFilled(a, b, ((x + r) & 1) ? t.checkerA : t.checkerB);
                if ((bits & (0x8000 >> x)) && !clear) dl->AddRectFilled(a, b, col);
            }
        }
        canvasGrid(sprCv, dl, ga, gb, m, m);
        dl->AddRect(ga, gb, t.controlStrongStroke, 0, th);
        /* The users, wrapped in three lines: a strip of each one's row colours and its number. */
        unsigned drawn = sprPatDrawnCodes(k.p);
        float sw = floorf(ui::Px(5)), x = at.x, y = gb.y + ui::Px(8);
        int line = 0, left = users;
        dl->PushClipRect(ImVec2(at.x, y), ImVec2(at.x + partW, y + lh * 3), true);
        if (users == 0) dl->AddText(ImVec2(x, y), t.caution, ts(TS_SPR_PAT_UNUSED_NOW));
        for (int n = 0; n < fr->stopIndex && line < 3; n++) {
            if ((fr->spr[n].pattern & 0xff) != k.p) continue;
            char num[8], more[16];
            snprintf(num, sizeof(num), "#%d", n);
            snprintf(more, sizeof(more), "+%d", left);
            float w = sw + ui::Px(3) + ImGui::CalcTextSize(num).x + ui::Px(10);
            /* The last line keeps room for "+n" while more follow. */
            float room = at.x + partW - (line == 2 && left > 1 ? ImGui::CalcTextSize(more).x : 0);
            if (x + w - ui::Px(10) > room && (x > at.x || line == 2)) {
                if (++line == 3) break;
                x = at.x;
                y += lh;
            }
            bool filled = ((drawn >> u.code[n]) & 1) != 0;
            for (int r = 0; r < fr->size; r++) {
                int c = fr->spr[n].color[r];
                ImVec2 a(x, floorf(y + lh * r / fr->size)), b(x + sw, floorf(y + lh * (r + 1) / fr->size));
                if (!filled || vdpSpriteClear(s, *fr, c)) dl->AddRectFilled(a, b, t.checkerA);
                else dl->AddRectFilled(a, b, rgbaToCol(vdpSpriteRgbaFrom(s, spriteRowRgb(*fr, n, r), *fr, c)));
            }
            if (!filled) {
                int c = u.code[n];
                ImU32 col = vdpSpriteClear(s, *fr, c) ? t.textTertiary : rgbaToCol(vdpSpriteRgbaFrom(s, spriteRowRgb(*fr, n, u.codeRow[n]), *fr, c));
                dl->AddRect(ImVec2(x, y), ImVec2(x + sw, y + lh), col, 0, th);
            }
            dl->AddText(ImVec2(x + sw + ui::Px(3), y), t.textSecondary, num);
            x += w;
            left--;
        }
        if (left > 0) {
            char more[16];
            snprintf(more, sizeof(more), "+%d", left);
            dl->AddText(ImVec2(fminf(x, at.x + partW - ImGui::CalcTextSize(more).x), y), t.textSecondary, more);
        }
        dl->PopClipRect();
    }

    /* The patterns the selected table's sprites used in recent pictures; reset when its pattern base moves. */
    void sprNotePatternRing(const VdpDebugState* s)
    {
        unsigned ver;
        const VdpFrameShot* o = (const VdpFrameShot*)shell::frameShot(&ver);
        if (sprView.fr == NULL || o == NULL || !o->valid || !o->ended || o->stale || o->seq == sprPatRingSeq) return;
        const VdpSpriteFrame& fr = *sprView.fr;
        bool reset = sprSel.patBase != sprPatRingBase || (Int32)(o->seq - sprPatRingSeq) < 0;
        if (reset || o->seq - sprPatRingSeq > (UInt32)SPR_RING) sprPatRingLen = sprPatRingPos = 0;
        if (reset || o->seq - sprPatRingSeq > (UInt32)SPR_AWAY_SHOWN) sprPatDrawnLen = sprPatDrawnPos = 0;
        sprPatRingBase = sprSel.patBase;
        sprPatRingSeq = o->seq;
        uint32_t* bits = sprPatRing[sprPatRingPos];
        uint16_t* drawn = sprPatDrawn[sprPatDrawnPos];
        memset(bits, 0, sizeof(sprPatRing[0]));
        memset(drawn, 0, sizeof(sprPatDrawn[0]));
        for (int n = 0; n < fr.stopIndex; n++) {
            int p = fr.spr[n].pattern & 0xff, row;
            if (sprView.agg[n].drawn + sprView.agg[n].dropped > 0) bits[p >> 5] |= 1u << (p & 31);
            if (sprView.agg[n].drawn > 0) drawn[p] |= (uint16_t)(1u << sprUserCode(s, fr, n, &row));
        }
        sprPatRingPos = (sprPatRingPos + 1) % SPR_RING;
        if (sprPatRingLen < SPR_RING) sprPatRingLen++;
        sprPatDrawnPos = (sprPatDrawnPos + 1) % SPR_AWAY_SHOWN;
        if (sprPatDrawnLen < SPR_AWAY_SHOWN) sprPatDrawnLen++;
    }

    /* The colour a sprite shows most, its row in *row: mode 1 its one colour; mode 2 the code covering
    ** the most dots of its rows, the upper row's on a tie, row 0's without dots. */
    static int sprUserCode(const VdpDebugState* s, const VdpSpriteFrame& fr, int n, int* row)
    {
        const VdpSprite& sp = fr.spr[n];
        int dots[16] = {}, first[16], best = sp.color[0] & 15;
        *row = 0;
        if (fr.mode != 2) return best;
        for (int c = 0; c < 16; c++) first[c] = -1;
        for (int r = 0; r < fr.size; r++) {
            int c = sp.color[r] & 15;
            for (unsigned b = (unsigned)vdpSpriteRowBits(s, fr, n, r) & 0xffff; b; b &= b - 1) dots[c]++;
            if (first[c] < 0) first[c] = r;
        }
        for (int r = 0; r < fr.size; r++) {
            int c = sp.color[r] & 15;
            if (dots[c] > dots[best]) best = c;
        }
        *row = first[best];
        return best;
    }

    /* The pinned pattern: a Focus a pattern click made (b -1, a its number), with its table as clicked. */
    bool pinnedPattern(const Focus& f, SprPatKey* k) const
    {
        if (!f.pinned || f.kind != FOCUS_VRAM || f.b != -1 || f.a < 0 || f.a > 255 || sprPatPin.base < 0) return false;
        *k = sprPatPin;
        k->p = f.a;
        return true;
    }

    /* Whether k is a pattern of table fr. */
    static bool patOf(const VdpDebugState* s, const SprPatKey& k, const VdpSpriteFrame& fr)
    {
        return k.p >= 0 && fr.mode == k.mode && fr.size == k.size && vdpSpritePatternAddr(s, fr, 0) == k.base;
    }

    /* Sprite n of table fr as a user: 2 of the pinned pattern, 1 of the hovered one, else 0. */
    int sprPatMark(const VdpDebugState* s, const Focus& f, const VdpSpriteFrame& fr, int n) const
    {
        if (n < 0 || n >= fr.stopIndex) return 0;
        int p = fr.spr[n].pattern & 0xff;
        SprPatKey k;
        if (pinnedPattern(f, &k) && k.p == p && patOf(s, k, fr)) return 2;
        return sprPatHot.p == p && patOf(s, sprPatHot, fr) ? 1 : 0;
    }

    /* The users of each pattern in a table: how many, their codes, and the one whose colours draw it. */
    struct SprPatUse { int owner[256], users[256], code[32], codeRow[32]; uint16_t codes[256]; };

    /* Owner: of the users with a dot that shows, those of the colour most of them carry, the lower code
    ** on a tie, and of these the lowest number; the first user when none shows a dot. */
    void sprPatUse(const VdpDebugState* s, const VdpSpriteFrame& fr, SprPatUse& u) const
    {
        uint8_t votes[256][16];
        memset(votes, 0, sizeof(votes));
        memset(u.users, 0, sizeof(u.users));
        memset(u.codes, 0, sizeof(u.codes));
        for (int p = 0; p < 256; p++) u.owner[p] = -1;
        uint32_t shows = 0;
        for (int n = 0; n < fr.stopIndex; n++) {
            int p = fr.spr[n].pattern & 0xff;
            u.code[n] = sprUserCode(s, fr, n, &u.codeRow[n]);
            u.codes[p] |= (uint16_t)(1u << u.code[n]);
            if (u.users[p]++ == 0) u.owner[p] = n;
            for (int r = 0; r < fr.size && !((shows >> n) & 1); r++) {
                if (vdpSpriteRowBits(s, fr, n, r) != 0 && !vdpSpriteClear(s, fr, fr.spr[n].color[r])) shows |= 1u << n;
            }
            if ((shows >> n) & 1) votes[p][u.code[n]]++;
        }
        for (int p = 0; p < 256; p++) {
            int best = -1;
            for (int c = 0; c < 16; c++) {
                if (votes[p][c] > (best < 0 ? 0 : votes[p][best])) best = c;
            }
            if (best < 0) continue;
            for (int n = 0; n < fr.stopIndex; n++) {
                if ((fr.spr[n].pattern & 0xff) == p && u.code[n] == best && ((shows >> n) & 1)) { u.owner[p] = n; break; }
            }
        }
    }

    /* The colour codes drawn with pattern p in the last pictures. */
    unsigned sprPatDrawnCodes(int p) const
    {
        unsigned m = 0;
        for (int i = 0; i < sprPatDrawnLen; i++) m |= sprPatDrawn[i][p & 0xff];
        return m;
    }

    /* A chip of colour code c as row row of sprite n shows it: filled, else an outline over the checker. */
    void sprChip(ImDrawList* dl, ImVec2 a, ImVec2 b, const VdpDebugState* s, const VdpSpriteFrame& fr, int c, int n, int row, bool filled) const
    {
        const UInt32* rgb = spriteRowRgb(fr, n, row);
        chipRect(dl, a, b, filled, vdpSpriteClear(s, fr, c), rgbaToCol(vdpSpriteRgbaFrom(s, rgb, fr, c)));
    }

    /* A colour chip: filled in col, or a checker when the colour is clear; hollow, edged in col (grey when clear). */
    static void chipRect(ImDrawList* dl, ImVec2 a, ImVec2 b, bool filled, bool clear, ImU32 col)
    {
        const UiTokens& t = uiTokens();
        if (filled && !clear) {
            dl->AddRectFilled(a, b, col);
            return;
        }
        dl->AddRectFilled(a, b, t.checkerA);
        if (filled) {
            ImVec2 m((a.x + b.x) / 2, (a.y + b.y) / 2);
            dl->AddRectFilled(a, m, t.checkerB);
            dl->AddRectFilled(m, b, t.checkerB);
            return;
        }
        dl->AddRect(a, b, clear ? t.textTertiary : col, 0, fmaxf(1.0f, floorf(ui::Px(1))));
    }

    /* Under pattern p's tile, w px from a: a chip per colour code of its users, by code, hollow when
    ** none of that colour was drawn in the last pictures; "+n" for those without room. */
    void sprPatBand(ImDrawList* dl, const VdpDebugState* s, const VdpSpriteFrame& fr, int p, ImVec2 a, float w, unsigned codes,
                    const int* code, const int* codeRow) const
    {
        const UiTokens& t = uiTokens();
        int nc = 0, shown;
        for (unsigned m = codes; m; m &= m - 1) nc++;
        float h = floorf(ui::Px(7)), g = fmaxf(1.0f, floorf(ui::Px(1))), cw = h;
        while (nc * (cw + g) - g > w && cw > floorf(ui::Px(3))) cw -= 1;
        ui::PushFont(UI_FONT_BODY, 11);
        char more[16] = "";
        for (shown = nc; shown > 0 && shown * (cw + g) - g > w; shown--) {}
        while (shown < nc) {
            snprintf(more, sizeof(more), "+%d", nc - shown);
            if (shown == 0 || shown * (cw + g) + ImGui::CalcTextSize(more).x <= w) break;
            shown--;
        }
        unsigned drawn = sprPatDrawnCodes(p);
        for (int c = 0, i = 0; c < 16 && i < shown; c++) {
            if (!((codes >> c) & 1)) continue;
            int n = 0;
            while (n < fr.stopIndex && ((fr.spr[n].pattern & 0xff) != p || code[n] != c)) n++;
            if (n == fr.stopIndex) continue;
            ImVec2 ca(a.x + i * (cw + g), a.y);
            sprChip(dl, ca, ImVec2(ca.x + cw, ca.y + h), s, fr, c, n, codeRow[n], ((drawn >> c) & 1) != 0);
            i++;
        }
        if (more[0]) dl->AddText(ImVec2(a.x + shown * (cw + g), floorf(a.y + (h - ImGui::GetTextLineHeight()) / 2)), t.textSecondary, more);
        ui::PopFont();
    }

    /* What pattern p's tooltip shows, kept between its refreshes. */
    struct SprPatShown {
        struct Row { char line[160]; bool filled, clear; ImU32 col; };
        char head[256] = "", more[160] = "", others[160] = "";
        bool unused = false;
        int  rows = 0;
        Row  row[8];
    };
    SprPatShown sprPatShown;

    /* Pattern p's users in a tooltip: a chip and the lines of each, at most 8; held still as a SteadyTip. */
    void sprPatTip(const VdpDebugState* s, const VdpSpriteFrame& fr, int p, int users, unsigned codes, const int* code, const int* codeRow, int others)
    {
        SprPatShown& m = sprPatShown;
        int base = vdpSpritePatternAddr(s, fr, 0);
        int key[4] = { base, p, fr.size, fr.mode };
        if (steadyTipBegin(sprPatHeld, ImHashData(key, sizeof(key), (ImGuiID)sprSel.attr))) {
            char lab[16], a1[64], a2[64];
            int nc = 0, listed = 0;
            for (unsigned c = codes; c; c &= c - 1) nc++;
            if (fr.size == 16) snprintf(lab, sizeof(lab), "%d-%d", p, p + 3);
            else snprintf(lab, sizeof(lab), "%d", p);
            int at = textf(m.head, sizeof(m.head), ts(TS_SPR_PAT_TIP_NAME), lab);
            if (users > 0 && at > 0 && at < (int)sizeof(m.head)) {
                if (users == 1) textf(a1, sizeof(a1), "%s", ts(TS_SPR_PAT_ONE_SPRITE));
                else textf(a1, sizeof(a1), ts(TS_SPR_PAT_SPRITES), users);
                if (nc == 1) textf(a2, sizeof(a2), "%s", ts(TS_SPR_PAT_ONE_COLOUR));
                else textf(a2, sizeof(a2), ts(TS_SPR_PAT_COLOURS), nc);
                textf(m.head + at, sizeof(m.head) - at, ts(TS_SPR_PAT_TIP_COUNTS), a1, a2);
            }
            m.unused = users == 0;
            m.rows = 0;
            unsigned drawn = sprPatDrawnCodes(p);
            for (int n = 0; n < fr.stopIndex; n++) {
                if ((fr.spr[n].pattern & 0xff) != p) continue;
                if (listed++ == 8) break;
                const VdpSprite& sp = fr.spr[n];
                SprPatShown::Row& row = m.row[m.rows++];
                int d0 = -1, d1 = -1;
                bool cc = false;
                for (int r = 0; r < fr.size * fr.mag; r++) {
                    int d = vdpSpriteDisplayLine(fr, (sp.y + 1 + r) & 0xff);
                    if (d < 0) continue;
                    if (d0 < 0) d0 = d;
                    d1 = d;
                }
                for (int r = 0; r < fr.size; r++) cc = cc || (fr.mode == 2 && (sp.color[r] & 0x40));
                int k;
                /* Whether it is drawn is left to the chip, held as the band's: per picture it blinks with multiplexing. */
                if (d0 < 0) k = textf(row.line, sizeof(row.line), ts(TS_SPR_PAT_USER_OFF), n);
                else k = textf(row.line, sizeof(row.line), ts(TS_SPR_PAT_USER_LINES), n, d0, d1);
                if (cc && k > 0 && k < (int)sizeof(row.line)) textf(row.line + k, sizeof(row.line) - k, " \xC2\xB7 CC");
                /* The chip's colour as of now, so a held row never reads a later picture's sprite n. */
                row.filled = ((drawn >> code[n]) & 1) != 0;
                row.clear = vdpSpriteClear(s, fr, code[n]);
                row.col = rgbaToCol(vdpSpriteRgbaFrom(s, spriteRowRgb(fr, n, codeRow[n]), fr, code[n]));
            }
            m.more[0] = m.others[0] = 0;
            if (listed > 8) textf(m.more, sizeof(m.more), ts(TS_SPR_PAT_MORE), users - 8);
            if (others > 0) textf(m.others, sizeof(m.others), ts(TS_SPR_TAB_OTHER_USERS), others);
        }
        ImGui::TextUnformatted(m.head);
        if (m.unused) {
            ImGui::PushStyleColor(ImGuiCol_Text, uiTokens().textSecondary);
            ImGui::TextUnformatted(ts(TS_SPR_PAT_UNUSED_NOW));
            ImGui::PopStyleColor();
        }
        float lh = ImGui::GetTextLineHeight(), sw = floorf(ui::Px(10));
        for (int i = 0; i < m.rows; i++) {
            ImVec2 c = ImGui::GetCursorScreenPos();
            float cy = floorf(c.y + (lh - sw) / 2);
            chipRect(ImGui::GetWindowDrawList(), ImVec2(c.x, cy), ImVec2(c.x + sw, cy + sw), m.row[i].filled, m.row[i].clear, m.row[i].col);
            ImGui::Dummy(ImVec2(sw, lh));
            ImGui::SameLine();
            ImGui::TextUnformatted(m.row[i].line);
        }
        if (m.more[0]) ImGui::TextUnformatted(m.more);
        if (m.others[0]) ImGui::TextUnformatted(m.others);
        steadyTipEnd(sprPatHeld);
    }

    /* The selected table's patterns, each as large as an attribute tile (q px a dot), cols a row, in the
    ** colours most of its users show, all its users' colours under it; only the rows in view are drawn. */
    void sprDrawPats(const VdpDebugState* s, Focus& f, ImVec2 at, float q, float partW, int cols)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        if (sprView.fr == NULL) return;
        const VdpSpriteFrame& fr = *sprView.fr;
        ui::PushFont(UI_FONT_STRONG);
        dl->AddText(at, t.text, ts(TS_SPR_TAB_PATS));
        ui::PopFont();
        dl->PushClipRect(at, ImVec2(at.x + partW, at.y + lh * 2 + ui::Px(6)), true);
        dl->AddText(ImVec2(at.x, at.y + lh + ui::Px(2)), t.textSecondary, ts(TS_SPR_TAB_PATS_SUB));
        dl->PopClipRect();
        /* Users of the selected table and the one owning the colours. */
        SprPatUse u;
        sprPatUse(s, fr, u);
        const int* owner = u.owner;
        const int* users = u.users;
        const int* code = u.code;
        const int* codeRow = u.codeRow;
        const uint16_t* codes = u.codes;
        const UInt32* rgb[32 * 16];
        for (int n = 0; n < fr.stopIndex; n++) sprRowRgb(fr, n, &rgb[n * 16]);
        sprNotePatternRing(s);
        vdpDecodeSpriteSheetUsers(s, fr, owner, rgb, (t.textTertiary & 0x00ffffffu) | 0xa0000000u, sprPatPic.img, &sprPatPic.w, &sprPatPic.h);
        sprPatPic.uploadChanged(&sprPatHash);
        int step = fr.size == 16 ? 4 : 1, count = 256 / step, rows = (count + cols - 1) / cols;
        float y0 = floorf(at.y + sprPatTitleH()), tile = fr.size * q, cellW = tile + ui::Px(4), pitch = sprPatPitch(tile);
        uint32_t recent[8] = {};
        for (int i = 0; i < sprPatRingLen; i++) {
            for (int w = 0; w < 8; w++) recent[w] |= sprPatRing[i][w];
        }
        int focusPat = -1;
        if (sprHot.n >= 0 && sprHot.sel) focusPat = fr.spr[sprHot.n].pattern;
        else if (pinnedSpriteAttr(f) >= 0 && (pinnedSpriteAttr(f) & ~0x7f) == sprSel.attr) focusPat = fr.spr[f.a].pattern;
        SprPatKey pk;
        bool pinHere = pinnedPattern(f, &pk) && patOf(s, pk, fr), hotHere = patOf(s, sprPatHot, fr);
        /* The rows in view; the checkers, then every tile from the one sheet sampled nearest, then marks over them. */
        int rowFirst = (int)floorf((dl->GetClipRectMin().y - y0) / pitch), rowLast = (int)floorf((dl->GetClipRectMax().y - y0) / pitch);
        rowFirst = rowFirst < 0 ? 0 : rowFirst;
        rowLast = rowLast >= rows ? rows - 1 : rowLast;
        int kFirst = rowFirst * cols, kEnd = (rowLast + 1) * cols < count ? (rowLast + 1) * cols : count;
        auto tileAt = [&](int k) { return ImVec2(at.x + (k % cols) * cellW, floorf(y0 + (k / cols) * pitch + lh)); };
        for (int k = kFirst; k < kEnd; k++) {
            ImVec2 a = tileAt(k);
            drawCheckerRect(dl, a, ImVec2(a.x + tile, a.y + tile));
        }
        if (sprPatPic.tex.id() && kEnd > kFirst) {
            ui::BeginNearest(dl);
            for (int k = kFirst; k < kEnd; k++) {
                int cx, cy;
                vdpSpriteSheetCell(fr.size, k * step, &cx, &cy);
                ImVec2 a = tileAt(k);
                dl->AddImage(ImTextureRef(sprPatPic.tex.id()), a, ImVec2(a.x + tile, a.y + tile), ImVec2(cx / 256.0f, cy / 64.0f),
                             ImVec2((cx + fr.size) / 256.0f, (cy + fr.size) / 64.0f));
            }
            ui::EndNearest(dl);
        }
        for (int k = kFirst; k < kEnd; k++) {
            int p = k * step;
            ImVec2 ca = tileAt(k), cb(ca.x + tile, ca.y + tile);
            canvasGrid(sprCv, dl, ca, cb, q, q);
            /* The number, and how many sprites use it at the right when that fits. */
            char num[16], x[16] = "";
            snprintf(num, sizeof(num), "%d", p);
            if (users[p] > 1) snprintf(x, sizeof(x), "\xC3\x97%d", users[p]);
            dl->AddText(ImVec2(ca.x, ca.y - lh), users[p] > 0 ? t.textSecondary : t.textTertiary, num);
            float xw = ImGui::CalcTextSize(x).x;
            if (x[0] && ImGui::CalcTextSize(num).x + ui::Px(4) + xw <= tile) dl->AddText(ImVec2(cb.x - xw, ca.y - lh), t.textSecondary, x);
            ImVec2 band(ca.x, cb.y + ui::Px(2));
            if (users[p] == 0 && ((recent[p >> 5] >> (p & 31)) & 1)) dl->AddCircleFilled(ImVec2(band.x + ui::Px(2), band.y + ui::Px(3.5f)), ui::Px(2), t.textSecondary);
            if (users[p] > 0) sprPatBand(dl, s, fr, p, band, tile, codes[p], code, codeRow);
            if (p == (focusPat & ~(step - 1))) dl->AddRect(ImVec2(ca.x - th, ca.y - th), ImVec2(cb.x + th, cb.y + th), t.accent, 0, th * 2);
            int lit = pinHere && pk.p == p ? 2 : hotHere && sprPatHot.p == p ? 1 : 0;
            if (lit) dl->AddRect(ImVec2(ca.x - th * lit, ca.y - th * lit), ImVec2(cb.x + th * lit, cb.y + th * lit), t.accent, 0, th * lit);
        }
        /* One item over the cells; the cell from the mouse, gaps counted with the cell. */
        ImGui::SetCursorScreenPos(ImVec2(at.x, y0));
        ImGui::InvisibleButton("##pats", ImVec2(cols * cellW - ui::Px(4), rows * pitch));
        if (!ImGui::IsItemHovered()) return;
        ImVec2 m = ImGui::GetIO().MousePos;
        int hc = (int)floorf((m.x - at.x) / cellW), hk = (int)floorf((m.y - y0) / pitch) * cols + hc;
        if (hc < 0 || hc >= cols || hk < 0 || hk >= count) return;
        int p = hk * step;
        int addr = vdpSpritePatternAddr(s, fr, p);
        /* b = -1 marks a sprite pattern, a its number, so pin() tells two patterns apart. */
        Focus hit = { f.chipId, FOCUS_LOGICAL, (UInt32)addr, (UInt32)(fr.size == 16 ? 32 : 8), FOCUS_VRAM, p, -1, false };
        focusAddr(s, fr.screenMode, addr, fr.size == 16 ? 32 : 8, &hit);
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        pin(f, hit, clicked);
        SprPatKey key = { vdpSpritePatternAddr(s, fr, 0), p, fr.size, fr.mode };
        sprPatHotNext = key;
        if (clicked && f.pinned) sprPatPin = key;
        char list[160] = "", text[384];
        size_t at2 = 0;
        for (int n = 0; n < fr.stopIndex && at2 < sizeof(list); n++) {
            if ((fr.spr[n].pattern & 0xff) == p) at2 += (size_t)snprintf(list + at2, sizeof(list) - at2, "%s%d", at2 ? ", " : "", n);
        }
        int others = 0;
        for (size_t k = 0; k < scene.slots.size(); k++) {
            if ((int)k == sprSlotSel || scene.slots[k].patBase != sprSel.patBase) continue;
            const VdpSpriteFrame& o = scene.frames[scene.slots[k].rep];
            for (int n = 0; n < o.stopIndex; n++) others += (o.spr[n].pattern & 0xff) == p;
        }
        char users2[200] = "", more[64] = "";
        if (list[0]) textf(users2, sizeof(users2), "%s%s", ts(TS_SPR_SHEET_USED_BY), list);
        if (others) textf(more, sizeof(more), ts(TS_SPR_TAB_OTHER_USERS), others);
        textf(text, sizeof(text), ts(TS_SPR_SHEET_PATTERN), p, p, addr, list[0] ? users2 : ts(TS_SPR_SHEET_UNUSED), more);
        ui::StatusText("%s", text);
        sprPatTip(s, fr, p, users[p], codes[p], code, codeRow, others);
    }

    void drawSprites(const VdpDebugState* s, Focus& f)
    {
        /* Its slot and frame index only hold in the scene it was hovered in; its display line in any. */
        sprHot = sprHotNext.ver == scene.ver ? sprHotNext : SprHot{ -1, -1, sprHotNext.d, -1, 0, false };
        sprHotNext = SprHot{ -1, -1, -1, -1, scene.ver, false };
        sprPatHot = sprPatHotNext;
        sprPatHotNext.p = -1;
        sprPlaceLine = -1;
        if (Sp3Panel::active(s)) {
            sp3.drawTab(s, f);
            return;
        }
        bool any = false;
        for (int fi : scene.lineFrame) any = any || fi >= 0;
        if (!any) {
            emptyView(ts(TS_SPRITES_NONE));
            return;
        }
        refreshPin(s, f);
        sprNoteRing(s);
        sprSlotSel = selectedSlot(f);
        ui::BeginCommandBar("bar");
        if (sprSelector()) ui::CommandBarSeparator();
        sprSlotSel = selectedSlot(f);
        sprView = shownTable();
        barToggle("##sprdrop", UI_ICON_DROPPED, ts(TS_SPR_DROPPED), ts(TS_SPR_DROPPED_TIP), &sprDropped);
        /* The Screen tab's Boxes: one setting, the two tabs are never seen together. */
        barArtToggle("##sprboxes", ui::DECOR_FRAME, ts(TS_BOXES_TIP), &boxes);
        ui::GridToggle(sprCv);
        zoomGroup(sprCv, false);
        ui::EndCommandBar();
        sprHeader(s);
        /* No line shows sprites: only the reason, under the same bar. */
        if (scene.slots.empty()) return;

        /* The vertical scrollbar's room is always kept; the layout, not last frame's scrollbars, says
        ** whether they show, the sideways one first as it takes height. */
        ImVec2 avail = ImGui::GetContentRegionAvail(), childAt = ImGui::GetCursorScreenPos();
        float W = avail.x - ImGui::GetStyle().ScrollbarSize - ui::Px(8);
        ui::CanvasZoom& zs = sprCv.z();
        zs.zoom = zs.zoom < 1 ? 1 : zs.zoom > 32 ? 32 : zs.zoom;
        /* Ctrl+wheel keeps the dot under the mouse where it is, or the part's corner in view when it moves. */
        int z = ui::CanvasWheelZoom(sprCv), anchor = -1, cell = -1;
        ImVec2 m = ImGui::GetIO().MousePos, dot(0, 0);
        if (z > 0) {
            for (int p = 0; p < SPT_COUNT && anchor < 0; p++) {
                ImVec2 a(sprLastO.x + sprLastL.at[p].x, sprLastO.y + sprLastL.at[p].y);
                if (sprLastL.scale[p] > 0 && m.x >= a.x && m.y >= a.y && m.x < a.x + sprLastL.size[p].x && m.y < a.y + sprLastL.size[p].y) {
                    anchor = p;
                    dot = ImVec2((m.x - a.x) / sprLastL.scale[p], (m.y - a.y) / sprLastL.scale[p]);
                }
            }
            /* The patterns reflow with the zoom: the dot under the mouse is kept by its cell. */
            if (anchor == SPT_PATS && sprLastL.patCols > 0) {
                float sc = sprLastL.scale[SPT_PATS], tile = sprTileDots() * sc, pitch = sprPatPitch(tile);
                float x = m.x - floorf(sprLastO.x + sprLastL.at[SPT_PATS].x);
                float y = m.y - floorf(sprLastO.y + ui::Px(4) + sprLastL.at[SPT_PATS].y) - sprPatTitleH();
                int c = (int)floorf(x / (tile + ui::Px(4))), r = (int)floorf(y / pitch);
                if (y >= 0 && c >= 0 && c < sprLastL.patCols) {
                    cell = r * sprLastL.patCols + c;
                    dot = ImVec2((x - c * (tile + ui::Px(4))) / sc, (y - r * pitch - ImGui::GetTextLineHeight()) / sc);
                }
            }
            zs.zoom = z;
            zs.fit = false;
        }
        int N = zs.fit ? sprFitZoom(W, avail.y) : zs.zoom;
        SprLayout L = sprLayoutAt(W, N);
        /* A bar only for what does not fit the child; each bar takes room from the other way. */
        float bar = ImGui::GetStyle().ScrollbarSize;
        bool over = L.height + ui::Px(8) > avail.y, overX = false;
        for (int i = 0; i < 2; i++) {
            overX = L.width > avail.x - (over ? bar : 0);
            over = L.height + ui::Px(8) > avail.y - (overX ? bar : 0);
        }
        ImVec2 content(fmaxf(W, L.width), L.height + ui::Px(8));
        ImVec2 scroll(overX ? -1.0f : 0.0f, over ? -1.0f : 0.0f);
        if (anchor >= 0) {
            ImVec2 a(L.at[anchor].x, L.at[anchor].y + ui::Px(4));
            bool moved = L.tier != sprLastL.tier && cell < 0;
            if (cell >= 0 && L.patCols > 0) {
                float tile = sprTileDots() * L.scale[SPT_PATS];
                a.x += (cell % L.patCols) * (tile + ui::Px(4));
                a.y += sprPatTitleH() + (cell / L.patCols) * sprPatPitch(tile) + ImGui::GetTextLineHeight();
            }
            ImVec2 tgt = moved ? a : ImVec2(a.x + dot.x * L.scale[anchor], a.y + dot.y * L.scale[anchor]);
            ImVec2 off = moved ? ImVec2(0, 0) : ImVec2(m.x - childAt.x, m.y - childAt.y);
            if (overX) scroll.x = fmaxf(0, tgt.x - off.x);
            if (over) scroll.y = fmaxf(0, tgt.y - off.y);
            ImGui::SetNextWindowContentSize(content);
        }
        if (scroll.x >= 0 || scroll.y >= 0) ImGui::SetNextWindowScroll(scroll);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGuiWindowFlags bars = (over ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_NoScrollbar) |
                                (overX ? ImGuiWindowFlags_AlwaysHorizontalScrollbar : 0);
        ImGui::BeginChild("spr", avail, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | bars);
        ImGui::PopStyleVar();
        ImVec2 o = ImGui::GetCursorScreenPos();
        o.y += ui::Px(4);
        sprLastL = L;
        sprLastO = ImVec2(o.x, o.y - ui::Px(4));
        /* Each part from a whole pixel, so its dots come out even. */
        auto partAt = [&](int p) { return ImVec2(floorf(o.x + L.at[p].x), floorf(o.y + L.at[p].y)); };
        sprDrawPlace(s, f, partAt(SPT_PLACE), L.scale[SPT_PLACE]);
        if (L.size[SPT_ATTRS].x > 0) {
            sprDrawAttrs(s, f, partAt(SPT_ATTRS), L.scale[SPT_ATTRS], L.size[SPT_ATTRS].x);
        }
        if (L.size[SPT_MAG].x > 0) {
            sprDrawMag(s, f, partAt(SPT_MAG), L.scale[SPT_MAG], L.size[SPT_MAG].x);
        }
        if (L.size[SPT_PATS].x > 0) {
            sprDrawPats(s, f, partAt(SPT_PATS), L.scale[SPT_PATS], L.size[SPT_PATS].x, L.patCols);
        }
        ImGui::SetCursorScreenPos(ImVec2(o.x, o.y - ui::Px(4)));
        ImGui::Dummy(content);
        bool hovered = ImGui::IsWindowHovered();
        ImGui::EndChild();
        ui::CanvasShown(sprCv, (float)N, hovered);
    }

    /* ---- Sprites ---- */

    static void swatchRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const VdpDebugState* s, const VdpSpriteFrame& f, int code, const UInt32* rgb)
    {
        const UiTokens& t = uiTokens();
        if (!vdpSpriteClear(s, f, code)) {
            dl->AddRectFilled(a, b, rgbaToCol(vdpSpriteRgbaFrom(s, rgb, f, code)));
            return;
        }
        ImVec2 m((a.x + b.x) / 2, (a.y + b.y) / 2);
        dl->AddRectFilled(a, b, t.checkerA);
        dl->AddRectFilled(a, m, t.checkerB);
        dl->AddRectFilled(m, b, t.checkerB);
    }

    /* A sprite's flags and state in words, for its tooltip: counted on the lines of
    ** its slot (ag), or over the whole frame without one. */
    static void spriteStateText(const VdpSpriteFrame& fr, const SprAgg* ag, int n, char* out, size_t size)
    {
        const VdpSprite& sp = fr.spr[n];
        SprAgg own = { sp.drawnLines, sp.droppedLines, sp.hiddenLines, sp.firstDropped, sp.lastDropped, sp.collides };
        if (ag != NULL) own = *ag;
        int any = 0, at = 0;
        for (int r = 0; r < fr.size; r++) any |= sp.color[r];
        if (n == fr.stopIndex) at = textf(out, size, ts(TS_SPR_TERMINATOR), fr.stopY);
        else if (sp.afterStop) at = textf(out, size, "%s", ts(TS_SPR_AFTER_TERMINATOR));
        else if (own.dropped) at = textf(out, size, ts(TS_SPR_DROPPED_LINES), own.firstDropped, own.lastDropped, own.collides ? ts(TS_SPR_COLLIDES) : "");
        else if (own.drawn + own.hidden == 0) at = textf(out, size, "%s", ts(TS_SPR_OFF_SCREEN));
        else at = textf(out, size, "%s%s", ts(own.hidden && !own.drawn ? TS_SPR_HIDDEN_CC : TS_SPR_SHOWN), own.collides ? ts(TS_SPR_COLLIDES) : "");
        if (at >= 0 && (size_t)at < size && n < fr.stopIndex && (any & 0xe0)) {
            textf(out + at, size - at, "\n%s%s%s", (any & 0x80) ? "EC " : "", fr.mode == 2 && (any & 0x40) ? "CC " : "",
                  fr.mode == 2 && (any & 0x20) ? "IC" : "");
        }
    }

    /* ---- VRAM ---- */

    /* Back to a CPU address in the mode the VRAM is read as. */
    static UInt32 readAsAddress(const VdpDebugState* s, int rm, UInt32 phys)
    {
        return (UInt32)vdpLogicalAddress(s, rm, (int)phys);
    }

    /* The pages, read as rm, holding page p of mode: from SCREEN 7 on a page is split
    ** between the two 64K halves, so read as SCREEN 5/6 it is two pages. */
    static unsigned pagesHolding(const VdpDebugState* s, int mode, int p, int rm)
    {
        int bytes = mode == 5 || mode == 6 ? 0x8000 : 0x10000, rmBytes = rm == 5 || rm == 6 ? 0x8000 : 0x10000;
        unsigned m = 0;
        for (int odd = 0; odd < 2; odd++) {
            UInt32 page = readAsAddress(s, rm, (UInt32)vdpPhysicalAddress(s, mode, p * bytes + odd)) / (UInt32)rmBytes;
            if (page < 32) m |= 1u << page;
        }
        return m;
    }

    /* Where a physical span lands on the VRAM picture read as rm: the one
    ** mapping the focus, the map's hover and the page outlines share. */
    bool vramRect(const VdpDebugState* s, int rm, UInt32 phys, UInt32 len, float* x0, float* y0, float* x1, float* y1) const
    {
        if (rm < 0) {
            *x0 = (float)(phys & 255); *y0 = (float)(phys >> 8);
            *x1 = len >= 256 ? 256.0f : *x0 + (float)len; *y1 = *y0 + 1;
            if (len >= 256) *x0 = 0;
            return true;
        }
        UInt32 logical = readAsAddress(s, rm, phys);
        int pageBytes = rm == 5 || rm == 6 ? 0x8000 : 0x10000;
        int bpl = rm == 5 || rm == 6 ? 128 : 256;
        int ppb = rm == 5 || rm == 7 ? 2 : rm == 6 ? 4 : 1;
        int page = (int)logical / pageBytes, within = (int)logical % pageBytes, ox, oy;
        if (page >= grid.pages) return false;
        grid.origin(page, &ox, &oy);
        *y0 = (float)(oy + within / bpl); *y1 = *y0 + 1;
        *x0 = (float)(ox + (within % bpl) * ppb); *x1 = *x0 + ppb;
        if (len >= 256) {
            /* 256 bytes are two lines in SCREEN 5/6; interleaved, every other byte of two lines. */
            int lines = (int)(interleaved(s, rm) ? len * 2 : len) / bpl;
            *x0 = (float)ox; *x1 = (float)(ox + grid.tileW);
            *y1 = *y0 + (float)(lines > 1 ? lines : 1);
        }
        return true;
    }

    /* The block hovered on the map and the focus, over the picture read as rm. */
    void focusMarks(ImDrawList* dl, const ui::CanvasXf& xf, int rm) const
    {
        const UiTokens& t = uiTokens();
        const Focus& f = shell::shownFocus();
        float th = fmaxf(1.0f, ui::Px(1)), x0, y0, x1, y1;
        int block = shell::hoverBlock();
        if (block >= 0 && vramRect(ovState, rm, (UInt32)block << 8, 256, &x0, &y0, &x1, &y1)) {
            ImVec2 a = xf.toScreen(x0, y0), b = xf.toScreen(x1, y1);
            dl->AddRectFilled(a, b, ui::WithAlpha(t.accent, 70));
        }
        if (f.kind == FOCUS_NONE || f.len == 0) return;
        UInt32 phys = toPhysical(ovState, f.space, f.addr);
        if (!vramRect(ovState, rm, phys, f.space == FOCUS_PHYS ? f.len : 1, &x0, &y0, &x1, &y1)) return;
        ImVec2 a = xf.toScreen(x0, y0), b = xf.toScreen(x1, y1);
        dl->AddRect(ImVec2(a.x - th * 2, a.y - th * 2), ImVec2(b.x + th * 2, b.y + th * 2), t.accent, 0, th * 2);
    }

    static void vramOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V99x8View* self = (V99x8View*)ref;
        const UiTokens& t = uiTokens();
        if (self->vramTint) {
            /* The VRAM map's lanes, of every band: what the frame's lines read, tinted and named. */
            int n;
            const VramLane* lanes = shell::mapLanes(&n);
            for (int i = 0; i < n; i++) {
                ImU32 col = t.series[lanes[i].series % 6];
                char lab[192];
                if (lanes[i].where) textf(lab, sizeof(lab), "%s \xC2\xB7 %s", lanes[i].name, lanes[i].where);
                else textf(lab, sizeof(lab), "%s", lanes[i].name);
                ui::CanvasSpan(dl, xf, lanes[i].addr, lanes[i].len, 256, ui::WithAlpha(col, t.dark ? 70 : 90), col, lab);
            }
        }
        ui::CanvasAddressRuler(dl, xf, self->vramPic.h, 256);
        self->focusMarks(dl, xf, -1);
    }

    static void bitmapOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V99x8View* self = (V99x8View*)ref;
        const VdpDebugState* s = self->ovState;
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1));
        int rm = self->ovReadMode;

        const VdpBandGroup* rg = self->ovReadGroup >= 0 ? &self->groups[self->ovReadGroup] : NULL;
        if (rm >= 0 && rg) {
            /* Page labels, and the pages the group's bands show with the pages they alternate with. */
            int pageBytes = rm == 5 || rm == 6 ? 0x8000 : 0x10000;
            int gBytes = rg->mode == 5 || rg->mode == 6 ? 0x8000 : 0x10000;
            bool alternates = vdpIsBitmapMode(rg->mode) && ((rg->regs[9] & 0x04) || rg->regs[13] != 0);
            unsigned shown = 0, pair = 0;
            for (int p = 0; p < 32 && vdpIsBitmapMode(rg->mode); p++) {
                if (!((rg->shownPages >> p) & 1)) continue;
                shown |= pagesHolding(s, rg->mode, p, rm);
                if (alternates) pair |= pagesHolding(s, rg->mode, p & ~1, rm);
            }
            pair &= ~shown;
            for (int p = 0; p < self->grid.pages; p++) {
                int ox, oy;
                self->grid.origin(p, &ox, &oy);
                ImVec2 a = xf.toScreen((float)ox, (float)oy), b = xf.toScreen((float)(ox + self->grid.tileW), (float)(oy + self->grid.tileH));
                bool isShown = ((shown >> p) & 1) != 0, isPair = ((pair >> p) & 1) != 0;
                if (isShown || isPair) {
                    dl->AddRect(ImVec2(a.x - th * 2, a.y - th * 2), ImVec2(b.x + th * 2, b.y + th * 2),
                                isShown ? t.accent : ui::WithAlpha(t.accent, 140), 0, th * 2);
                }
                char lab[256];
                textf(lab, sizeof(lab), ts(isShown ? (gBytes == pageBytes ? TS_PAGE_LABEL_SHOWN : TS_PAGE_LABEL_HOLDS_SHOWN) :
                                           isPair ? TS_PAGE_LABEL_PAIR : TS_PAGE_LABEL), p);
                ImVec2 ts = ImGui::CalcTextSize(lab);
                dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + ts.x + ui::Px(8), a.y + ts.y + ui::Px(4)), ui::WithAlpha(t.layer, 200));
                dl->AddText(ImVec2(a.x + ui::Px(4), a.y + ui::Px(2)), t.text, lab);
            }
            /* The rows and columns of its pages the group's bands show. */
            for (size_t i = 0; gBytes == pageBytes && i < self->raster.bands.size(); i++) {
                const VdpBand& b = self->raster.bands[i];
                int seg[12], n = vdpRasterBandCols(b, seg);
                if (n == 0 || !vdpGroupHasBand(*rg, b)) continue;
                self->bandFrames(dl, xf, s, b, seg, n, (int)i, th);
            }
        }
        else if (rm >= 0) {
            /* Page labels, and the displayed page with the page it alternates with. */
            const VdpBandGroup* one = self->soleBitmapGroup();
            const UInt8* regs = one ? one->regs : s->regs;
            int mode = one ? one->mode : s->screenMode;
            VdpTables tb;
            vdpDecodeTables(s, one ? vdpLayoutOf(*one) : vdpLayoutOf(s), &tb);
            int pageBytes = rm == 5 || rm == 6 ? 0x8000 : 0x10000;
            int liveBytes = mode == 5 || mode == 6 ? 0x8000 : 0x10000;
            int livePage = vdpIsBitmapMode(mode) ? tb.name / liveBytes : -1;
            bool flat = livePage >= 0 && liveBytes == pageBytes && vdpIsFlatInterlace(s, regs);
            bool alternates = livePage >= 0 && !flat && ((regs[9] & 0x04) || regs[13] != 0) && (livePage & 1);
            unsigned shown = flat ? vdpShownPageBits(flat, livePage) : livePage >= 0 ? pagesHolding(s, mode, livePage, rm) : 0;
            unsigned pair = alternates ? pagesHolding(s, mode, livePage & ~1, rm) & ~shown : 0;
            for (int p = 0; p < self->grid.pages; p++) {
                int ox, oy;
                self->grid.origin(p, &ox, &oy);
                ImVec2 a = xf.toScreen((float)ox, (float)oy), b = xf.toScreen((float)(ox + self->grid.tileW), (float)(oy + self->grid.tileH));
                bool isShown = ((shown >> p) & 1) != 0, isPair = ((pair >> p) & 1) != 0;
                if (isShown || isPair) {
                    dl->AddRect(ImVec2(a.x - th * 2, a.y - th * 2), ImVec2(b.x + th * 2, b.y + th * 2),
                                isShown ? t.accent : ui::WithAlpha(t.accent, 140), 0, th * 2);
                }
                char lab[256];
                textf(lab, sizeof(lab), ts(isShown ? (liveBytes == pageBytes ? TS_PAGE_LABEL_SHOWN : TS_PAGE_LABEL_HOLDS_SHOWN) :
                                           isPair ? TS_PAGE_LABEL_PAIR : TS_PAGE_LABEL), p);
                ImVec2 ts = ImGui::CalcTextSize(lab);
                dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + ts.x + ui::Px(8), a.y + ts.y + ui::Px(4)), ui::WithAlpha(t.layer, 200));
                dl->AddText(ImVec2(a.x + ui::Px(4), a.y + ui::Px(2)), t.text, lab);
            }
        }
        const VdpBandGroup* one = self->soleBitmapGroup();
        int liveMode = one ? one->mode : s->screenMode;
        int liveBytes2 = liveMode == 5 || liveMode == 6 ? 0x8000 : 0x10000;
        int rmBytes = rm == 5 || rm == 6 ? 0x8000 : 0x10000;
        if (rm >= 0 && !rg && vdpIsBitmapMode(liveMode) && liveBytes2 == rmBytes && self->raster.valid) {
            /* The rows and columns of its pages each band shows. */
            for (size_t i = 0; i < self->raster.bands.size(); i++) {
                const VdpBand& b = self->raster.bands[i];
                int seg[12], n = vdpRasterBandCols(b, seg);
                if (n == 0) continue;
                self->bandFrames(dl, xf, s, b, seg, n, (int)i, th);
            }
        }
        if (rm >= 0) cmdLrmmOverlay(dl, xf, s, rm, CmdPageMap{ self->grid.pages, self->grid.cols, self->grid.tileW, self->grid.tileH, self->grid.gap });
        self->focusMarks(dl, xf, rm);
    }

    /* Columns of pages that give the largest picture in the space left. */
    int bestColumns(int pages, int tileW, int gap, int ax, int ay) const
    {
        ImVec2 av = ImGui::GetContentRegionAvail();
        float availY = av.y;
        int best = 1;
        float bestU = 0;
        for (int c = 1; c <= pages; c++) {
            int rows = (pages + c - 1) / c;
            float w = (float)(c * tileW + (c - 1) * gap), h = (float)(rows * 256 + (rows - 1) * gap);
            float u = fminf(av.x / (w * ax), availY / (h * ay));
            if (u > bestU * 1.001f) {
                bestU = u;
                best = c;
            }
        }
        return best;
    }

    /* The one group of a frame not split, when it is in a bitmap mode: the registers
    ** the frame was drawn with, where the frame's end may hold the next one's. */
    const VdpBandGroup* soleBitmapGroup() const
    {
        return raster.valid && groups.size() == 1 && vdpIsBitmapMode(groups[0].mode) ? &groups[0] : NULL;
    }

    /* The bitmap group of a split frame Auto reads: the band in context's, else the one
    ** with the most lines; -1 when no band is in a bitmap mode. */
    int bitmapAutoGroup() const
    {
        int g = groupOfBand(ctxBand());
        if (g >= 0 && vdpIsBitmapMode(groups[g].mode)) return g;
        g = -1;
        for (int i = 0; i < (int)groups.size(); i++) {
            if (vdpIsBitmapMode(groups[i].mode) && (g < 0 || groups[i].lineCount > groups[g].lineCount)) g = i;
        }
        return g;
    }

    /* The bitmap mode the frame shows, whatever the tab: Bitmap's Auto in a frame without one. */
    void noteBitmapMode(const VdpDebugState* s)
    {
        if (vdpIsMsx1(s)) return;
        ensureRaster(s);
        int g = raster.valid && groups.size() >= 2 ? bitmapAutoGroup() : -1;
        int mode = g >= 0 ? groups[g].mode : raster.valid && groups.size() >= 2 ? -1 : s->screenMode;
        if (vdpIsBitmapMode(mode)) {
            lastBitmapMode = mode;
            bitmapSeen = true;
        }
    }

    /* VRAM read as the pages of a bitmap mode, whatever mode the VDP is in. */
    void drawBitmap(const VdpDebugState* s, Focus& f)
    {
        ensureRaster(s);
        /* A split frame: Auto reads a bitmap group, as bitmapAutoGroup picks. */
        bool split = raster.valid && groups.size() >= 2;
        int autoG = split ? bitmapAutoGroup() : -1, chosenG = -1;
        int bitmapGroups[8], ng = 0;
        for (int g = 0; split && g < (int)groups.size() && ng < 8; g++) {
            if (vdpIsBitmapMode(groups[g].mode)) bitmapGroups[ng++] = g;
        }
        if (split && readGroupSet && readAs < 0) {
            chosenG = vdpFindGroup(groups, readKey);
            if (chosenG >= 0 && !vdpIsBitmapMode(groups[chosenG].mode)) chosenG = -1;
        }
        int refG = chosenG >= 0 ? chosenG : autoG;
        int refMode = refG >= 0 ? groups[refG].mode : s->screenMode;
        /* No line in a bitmap mode: the last one shown, SCREEN 5 before any. */
        bool none = !vdpIsBitmapMode(refMode);
        int autoMode = none ? lastBitmapMode : refMode, autoSel = 0;
        for (int i = 0; i < BITMAP_MODES; i++) {
            if (bitmapModes[i] == autoMode) autoSel = i;
        }
        int sel = readAs < 0 ? autoSel : readAs;
        int rm = bitmapModes[sel];
        dotAspect(rm, &bitmapCv.aspectX, &bitmapCv.aspectY);

        const char* const* names = bitmapModeNames;
        ui::BeginCommandBar("bar");
        char preview[256], groupText[8][160];
        if (autoG >= 0) textf(preview, sizeof(preview), ts(TS_READ_AS_GROUP_AUTO), groups[autoG].lines, shortModeName(groups[autoG].mode));
        else textf(preview, sizeof(preview), ts(TS_READ_AS_AUTO), names[autoSel]);
        const char* previews[16] = { preview };
        int chosenItem = -1;
        for (int k = 0; k < ng; k++) {
            const VdpBandGroup& g = groups[bitmapGroups[k]];
            textf(groupText[k], sizeof(groupText[k]), ts(TS_STRIP_GROUP), g.lines, shortModeName(g.mode));
            previews[1 + k] = groupText[k];
            if (bitmapGroups[k] == chosenG) chosenItem = k;
        }
        for (int i = 0; i < BITMAP_MODES; i++) previews[1 + ng + i] = names[i];
        float comboW = ui::ComboWidth(previews, 1 + ng + BITMAP_MODES, 120);
        bool fixed = readAs >= 0 || readGroupSet;
        barText(ts(TS_READ_AS));
        ui::CommandBarItem(ui::Px(comboW));
        const char* shown = readAs >= 0 ? names[sel] : chosenItem >= 0 ? groupText[chosenItem] : preview;
        if (ui::BeginCombo("##readas", shown, comboW)) {
            if (ui::ComboItem(IdLabel(preview, "auto"), !fixed)) { readAs = -1; readGroupSet = false; }
            for (int k = 0; k < ng; k++) {
                ImGui::PushID(100 + k);
                if (ui::ComboItem(IdLabel(groupText[k], "group"), readAs < 0 && chosenItem == k)) {
                    readAs = -1;
                    readGroupSet = true;
                    readKey = vdpGroupKeyOf(groups[bitmapGroups[k]]);
                }
                ImGui::PopID();
            }
            if (ng > 0) ImGui::Separator();
            for (int i = 0; i < BITMAP_MODES; i++) {
                ImGui::PushID(i);
                if (ui::ComboItem(IdLabel(names[i], "mode"), readAs == i)) { readAs = i; readGroupSet = false; }
                ImGui::PopID();
            }
            ui::EndCombo();
        }
        if (fixed) {
            ui::CommandBarItem(ui::Px(14) + ImGui::CalcTextSize(ts(TS_STATE_FIXED)).x);
            ui::StatusDot(ts(TS_STATE_FIXED), ui::TONE_CAUTION);
            IdLabel follow(ts(TS_FOLLOW_MODE), "follow");
            ui::CommandBarItem(ImGui::CalcTextSize(follow, NULL, true).x + ImGui::GetStyle().FramePadding.x * 2);
            if (ui::SubtleButton(follow)) { readAs = -1; readGroupSet = false; }
        }
        /* Tiles | Column: one is always on. */
        ui::CommandBarSeparator();
        ui::CommandBarItem(ui::IconButtonWidth(UI_ICON_PAGES_GRID, ts(TS_LAYOUT_TILES)), ui::Px(24));
        if (ui::IconButton("##pgrid", UI_ICON_PAGES_GRID, ts(TS_LAYOUT_TILES), ts(TS_LAYOUT_TILES_TIP), layout == 0)) layout = 0;
        ui::CommandBarItem(ui::IconButtonWidth("", ts(TS_LAYOUT_COLUMN)), ui::Px(24));
        if (ui::PixelIconButton("##pcol", ui::UI_ART_FILM, ui::DECOR_NONE, ts(TS_LAYOUT_COLUMN_TIP), layout == 1, ImVec2(28, 24), ts(TS_LAYOUT_COLUMN))) layout = 1;
        ui::GridToggle(bitmapCv);
        zoomGroup(bitmapCv, false);
        ui::EndCommandBar();
        ovReadGroup = refG;
        bitmapCv.checker = false;
        ovState = s;
        ovReadMode = rm;

        UInt32 size = vramSize(s);
        /* Colour 0 as the emulator draws it, from the registers of the group read. */
        const VdpBandGroup* vg = refG >= 0 ? &groups[refG] : soleBitmapGroup();
        VdpLayout vl = vg ? withPal(vdpLayoutOf(*vg), palOfGroup(*vg)) : frameEndLayout(s);
        VdpBitmapOptions o = { rm, -1, 256, 0, VDP_COLOR0_AUTO, false, false };
        grid.pages = vdpBitmapPageCount(s, rm);
        grid.tileW = vdpBitmapWidth(rm);
        grid.tileH = 256;
        grid.gap   = layout == 0 ? 8 : 0;
        int ax, ay;
        ui::CanvasAspect(bitmapCv, &ax, &ay);
        grid.cols  = layout == 0 ? bestColumns(grid.pages, grid.tileW, 8, ax, ay) : 1;
        char key[64];
        snprintf(key, sizeof(key), "v%d/%u/%d/%d/%d/%02X%02X/%08X", rm, size, grid.cols, grid.gap, refG, vl.regs[7], vl.regs[8] & 0x20,
                 palHash(vl.rgb));
        bool decoded = bitmapPic.needsDecode(key);
        if (decoded) {
            if (grid.cols == 1 && grid.gap == 0) {
                vdpDecodeBitmap(s, vl, o, bitmapPic.img, &bitmapPic.w, &bitmapPic.h);
            }
            else {
                int cw, ch;
                vdpDecodeBitmap(s, vl, o, column, &cw, &ch);
                bitmapPic.w = grid.width();
                bitmapPic.h = grid.height();
                bitmapPic.img.assign((size_t)bitmapPic.w * bitmapPic.h, 0);
                for (int p = 0; p < grid.pages; p++) {
                    int ox, oy;
                    grid.origin(p, &ox, &oy);
                    for (int y = 0; y < 256; y++) {
                        memcpy(&bitmapPic.img[(size_t)(oy + y) * bitmapPic.w + ox], &column[(size_t)(p * 256 + y) * cw],
                               (size_t)cw * sizeof(uint32_t));
                    }
                }
            }
        }
        bitmapPic.upload(decoded);
        if (stripVramFrames > 0) {
            /* A page clicked on the strip: scrolled to for a few frames, until the well knows its size. */
            int ox, oy;
            if (stripVramPage >= 0 && stripVramPage < grid.pages && !bitmapCv.z().fit) {
                grid.origin(stripVramPage, &ox, &oy);
                ImVec2 sc = ui::CanvasScale(bitmapCv, (float)bitmapCv.z().zoom);
                ImGui::SetNextWindowScroll(ImVec2(floorf(ox * sc.x), floorf(oy * sc.y)));
            }
            if (--stripVramFrames == 0) stripVramPage = -1;
        }
        ui::CanvasHit h = ui::Canvas("bitmap", bitmapPic.tex.id(), bitmapPic.w, bitmapPic.h, bitmapCv, ImVec2(0, 0), bitmapOverlay, this);
        if (h.clickedEmpty) shell::releaseSelection();
        int page, px, py;
        if (h.valid && grid.at(h.x, h.y, &page, &px, &py)) {
            VdpPixelInfo pi;
            if (vdpBitmapPixel(s, vl, o, px, page * 256 + py, &pi)) {
                Focus hit = { f.chipId, FOCUS_PHYS, (UInt32)pi.physical, 1, FOCUS_VRAM, h.x, h.y, false };
                pin(f, hit, h.clicked);
                ui::StatusText(ts(TS_VRAM_PAGE_PIXEL), page, px, py, pi.logical, pi.physical, pi.value, pi.value);
            }
        }
        else if (readAs >= 0 && rm != refMode) {
            ui::StatusText(ts(TS_VRAM_READ_BY_HAND), names[readAs], shortModeName(refMode));
        }
        else if (none && !fixed) {
            ui::StatusText(ts(bitmapSeen ? TS_BITMAP_AUTO_LAST : TS_BITMAP_AUTO_NONE), shortModeName(refMode), names[sel]);
        }
    }

    /* The bytes themselves, 256 a row by physical address, tinted by the VRAM map's lanes. */
    void drawVram(const VdpDebugState* s, Focus& f)
    {
        vramCv.aspectX = vramCv.aspectY = 1;
        vramCv.gutter = 44;   /* the address ruler */
        ui::BeginCommandBar("bar");
        barToggle("##tint", UI_ICON_TABLES, ts(TS_CARD_TABLES), ts(TS_VRAM_TABLES_TIP), &vramTint);
        ui::GridToggle(vramCv);
        zoomGroup(vramCv, false);
        ui::EndCommandBar();
        vramCv.checker = false;
        ovState = s;

        UInt32 size = vramSize(s);
        char key[32];
        snprintf(key, sizeof(key), "b%u", size);
        bool decoded = vramPic.needsDecode(key);
        if (decoded) {
            vramPic.w = 256;
            vramPic.h = (int)(size / 256);
            vramPic.img.resize((size_t)vramPic.w * vramPic.h);
            for (UInt32 i = 0; i < size; i++) {
                uint32_t v = s->vram[i];
                vramPic.img[i] = 0xff000000u | (v << 16) | (v << 8) | v;
            }
        }
        vramPic.upload(decoded);
        ui::CanvasHit h = ui::Canvas("vram", vramPic.tex.id(), vramPic.w, vramPic.h, vramCv, ImVec2(0, 0), vramOverlay, this);
        if (h.clickedEmpty) shell::releaseSelection();
        if (h.valid) {
            UInt32 a = (UInt32)(h.y * 256 + h.x);
            Focus hit = { f.chipId, FOCUS_PHYS, a, 1, FOCUS_VRAM, h.x, h.y, false };
            pin(f, hit, h.clicked);
            /* What reads the byte, as the map's hover says. */
            char in[768], text[256];
            shell::laneNames(a, in, sizeof(in));
            textf(text, sizeof(text), ts(TS_VRAM_PHYS_VALUE), a, s->vram[a], s->vram[a]);
            ui::StatusText("%s%s", text, in);
        }
    }

    /* ---- cards ---- */

    void spriteSelection(const VdpDebugState* s, const Focus& f)
    {
        const UiTokens& t = uiTokens();
        ensureSprites(s);
        /* The table that laid the pinned sprite out, counted on the lines that table laid out. */
        int slot = -1, n = f.a;
        const VdpSpriteFrame* pf = n >= 0 && n < 32 ? pinFrame(f, &slot) : NULL;
        const VdpSpriteFrame& fr = pf != NULL ? *pf : sprites;
        if (n < 0 || n >= 32 || fr.mode == 0) return;
        const VdpSprite& sp = fr.spr[n];
        SprAgg own = { sp.drawnLines, sp.droppedLines, sp.hiddenLines, sp.firstDropped, sp.lastDropped, sp.collides };
        if (pf != NULL && slot >= 0) own = scene.agg[(size_t)slot * 32 + n];
        else if (pf != NULL && pf == sprPinKeep) own = sprPinKeepAgg;
        float cell = ui::Px(fr.size == 16 ? 6.0f : 10.0f);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int r = 0; r < fr.size; r++) {
            int bits = vdpSpriteRowBits(s, fr, n, r);
            ImU32 col = rgbaToCol(vdpSpriteRgbaFrom(s, spriteRowRgb(fr, n, r), fr, sp.color[r]));
            for (int x = 0; x < fr.size; x++) {
                ImVec2 a(p.x + x * cell, p.y + r * cell), b(a.x + cell, a.y + cell);
                if ((bits & (0x8000 >> x)) && !vdpSpriteClear(s, fr, sp.color[r])) dl->AddRectFilled(a, b, col);
                else dl->AddRectFilled(a, b, ((x + r) & 1) ? t.checkerA : t.checkerB);
            }
        }
        dl->AddRect(p, ImVec2(p.x + fr.size * cell, p.y + fr.size * cell), t.controlStrongStroke);
        ImGui::Dummy(ImVec2(fr.size * cell, fr.size * cell));
        ImGui::SameLine(0, ui::Px(12));
        ImGui::BeginGroup();
        if (ui::BeginKeyValue("sprkv")) {
            ui::KeyValue(ts(TS_KV_SPRITE), "%d%s", n, n == fr.stopIndex ? ts(TS_KV_SPRITE_TERMINATOR) :
                                                     sp.afterStop ? ts(TS_KV_SPRITE_AFTER) : "");
            ui::KeyValueMono("Y, X", "%d, %d", sp.y, sp.x);
            ui::KeyValueMono(ts(TS_KV_PATTERN), ts(TS_KV_PATTERN_AT), sp.pattern, sp.patternAddr);
            ui::KeyValueMono(ts(TS_KV_ATTRIBUTES), "%05X", sp.attrAddr);
            if (fr.mode == 2) ui::KeyValueMono(ts(TS_KV_COLOURS), "%05X", sp.colorAddr);
            ui::KeyValue(ts(TS_KV_SHOWN), ts(TS_LINES_N), own.drawn);
            if (own.dropped) ui::KeyValue(ts(TS_KV_DROPPED), ts(TS_KV_LINES_RANGE), own.firstDropped, own.lastDropped);
            if (own.hidden) ui::KeyValue(ts(TS_KV_HIDDEN), ts(TS_KV_LINES_CC), own.hidden);
            ui::KeyValue(ts(TS_KV_COLLIDES), "%s", ts(own.collides ? TS_YES : TS_NO));
            /* The pinned line: the sprite's row on it, its place among those in range, its state. */
            int row = vdpSpriteRow(fr, n, f.b), d = vdpSpriteDisplayLine(fr, f.b);
            if (row >= 0 && !sp.afterStop) {
                const VdpSpriteLine& ln = fr.line[f.b & 0xff];
                int k = 0;
                while (k < ln.count && ln.id[k] != n) k++;
                int st = vdpSpriteState(fr, f.b, k);
                ui::KeyValue(ts(d >= 0 ? TS_KV_LINE : TS_KV_SCROLL_LINE), ts(TS_SEL_LINE_ROW), d >= 0 ? d : f.b, row, k + 1, ln.count,
                             ts(st == VDP_SPRITE_DROPPED ? TS_STATE_DROPPED : st == VDP_SPRITE_CC_HIDDEN ? TS_STATE_HIDDEN_CC : TS_STATE_DRAWN));
            }
            ui::EndKeyValue();
        }
        ImGui::EndGroup();
        char line[96];
        if (fr.mode == 2) {
            /* One line per pattern row: colour address, byte and its flags. */
            ui::PushFont(UI_FONT_MONO);
            for (int r = 0; r < fr.size; r++) {
                int c = sp.color[r];
                textf(line, sizeof(line), "%2d %05X %02X  %2d %s%s%s", r, vdpSpriteColorAddr(s, fr, n, r), c, c & 15,
                      (c & 0x80) ? "EC " : "", (c & 0x40) ? "CC " : "", (c & 0x20) ? "IC" : "");
                ui::TextFitItem(line);
            }
            ui::PopFont();
        }
        else {
            ui::PushFont(UI_FONT_MONO);
            textf(line, sizeof(line), ts(TS_COLOUR_ADDR_EC), sp.colorAddr, sp.color[0], (sp.color[0] & 0x80) ? "  EC" : "");
            ui::TextFitItem(line);
            ui::PopFont();
        }
    }

    /* The pin as clicked, not as worked out again each frame: a VRAM byte or a pattern
    ** is its address. A new pin takes the card from a band picked before. */
    void notePin(const Focus& f)
    {
        bool pinned = f.pinned && f.kind != FOCUS_NONE, fixedAddr = f.kind == FOCUS_VRAM || f.kind == FOCUS_CHAR;
        float pin[5] = { (float)(pinned ? f.kind : -1), (float)f.a, (float)f.b, (float)(fixedAddr ? f.addr : 0),
                         (float)(fixedAddr ? f.space : bandFocus(f)) };
        if (memcmp(pin, cardPin, sizeof(pin)) == 0) return;
        if (pinned) cardBand = false;
        memcpy(cardPin, pin, sizeof(pin));
    }

    /* The card of what is pinned, or of the band picked on the Splits card; never
    ** of what is hovered. It keeps its height while the same thing stays pinned. */
    void cardSelection(const VdpDebugState* s, Focus& f)
    {
        bool pinned = f.pinned && f.kind != FOCUS_NONE;
        notePin(f);
        /* Picking a band or pinning anything else lets go of the SP3 pin. */
        if (sp3.pinned() && !pinned && Sp3Panel::active(s)) {
            sp3.selection(s, f);
            return;
        }
        const float* pin = cardPin;
        bool band = selBand >= 0 && selBand < (int)raster.bands.size() && (!pinned || cardBand);
        pinned = pinned && !band;
        if (!pinned && !band) {
            selLatch = 0;
            ui::TextFitItem(ts(TS_SEL_NONE), uiTokens().textSecondary);
            return;
        }
        float w = ImGui::GetContentRegionAvail().x, y0 = ImGui::GetCursorPosY();
        float key[6] = { pinned ? pin[0] : -1, pinned ? pin[1] : (float)selKey.family, pinned ? pin[2] : (float)selKey.first,
                         pinned ? pin[3] : (float)selKey.last, pinned ? pin[4] : 0, w };
        if (memcmp(key, selLatchKey, sizeof(key)) != 0) {
            selLatch = 0;
            memcpy(selLatchKey, key, sizeof(key));
        }
        selectionHeader(s, f, band);
        if (band) tablesList(s, vdpLayoutOf(raster.bands[selBand]), false);
        else selectionBody(s, f);
        float used = ImGui::GetCursorPosY() - y0, pad = selLatch - used - ImGui::GetStyle().ItemSpacing.y;
        if (used > selLatch) selLatch = used;
        else if (pad > 0) ImGui::Dummy(ImVec2(0, pad));
    }

    /* "Lines 60-75 - band 32-211 - SCREEN 4": where sprite n is shown. */
    void spriteCaption(const Focus& f, char* out, size_t size) const
    {
        int slot = -1, n = f.a;
        const VdpSpriteFrame* pf = n >= 0 && n < 32 ? pinFrame(f, &slot) : NULL;
        const VdpSpriteFrame& fr = pf != NULL ? *pf : sprites;
        int d0 = -1, d1 = -1, h = fr.size * fr.mag;
        for (int r = 0; n >= 0 && n < 32 && r < h; r++) {
            int d = vdpSpriteDisplayLine(fr, (fr.spr[n].y + 1 + r) & 0xff);
            /* With tables per line, only the lines its own table laid out. */
            if (d < 0 || (pf != NULL && slot >= 0 && (d >= (int)scene.lineSlot.size() || scene.lineSlot[d] != slot))) continue;
            if (d0 < 0 || d < d0) d0 = d;
            if (d > d1) d1 = d;
        }
        int bi = d0 >= 0 && raster.valid && splitBands() > 1 ? vdpRasterBandAt(raster, d0) : -1;
        if (d0 < 0) textf(out, size, "%s \xC2\xB7 %s", ts(TS_SPR_OFF_SCREEN), shortModeName(fr.screenMode));
        else if (bi >= 0) textf(out, size, ts(TS_SEL_CAPTION_SPRITE_BAND), d0, d1, raster.bands[bi].first, raster.bands[bi].last,
                                shortModeName(raster.bands[bi].mode));
        else textf(out, size, ts(TS_SEL_CAPTION_SPRITE), d0, d1, shortModeName(fr.screenMode));
    }

    /* The header names what is pinned, with its kind's icon and a pin that lets
    ** go of it; under it a caption says where it is: line, band and mode. */
    void selectionHeader(const VdpDebugState* s, Focus& f, bool band)
    {
        const UiTokens& t = uiTokens();
        char head[160] = "", caption[512] = "";
        const char* glyph = UI_ICON_PIXEL;
        const ui::PixelArt* art = NULL;
        int bi = bandOfFocus(f);
        bool multi = raster.valid && splitBands() > 1;
        if (band) {
            const VdpBand& b = raster.bands[selBand];
            glyph = UI_ICON_BAND;
            textf(head, sizeof(head), ts(TS_SEL_HEAD_BAND), b.first, b.last, shortModeName(b.mode));
            bandText(s, selBand, caption, sizeof(caption));
        }
        else if (f.kind == FOCUS_SPRITE) {
            art = &ui::UI_ART_SHIP;
            ensureSprites(s);
            textf(head, sizeof(head), ts(TS_SEL_HEAD_SPRITE), f.a);
            spriteCaption(f, caption, sizeof(caption));
        }
        else if (f.kind == FOCUS_VRAM) {
            glyph = UI_ICON_VRAM;
            UInt32 phys = toPhysical(s, f.space, f.addr);
            textf(head, sizeof(head), ts(TS_SEL_HEAD_VRAM), phys);
            VramLane lanes[64];
            int n = vramLanes(s, lanes, 64), at = -1;
            /* Of the tables holding it, the narrowest: a fourth pattern block runs over the name table,
            ** and a bitmap page holds the sprite tables. */
            for (int i = 0; i < n; i++) {
                if (phys < lanes[i].addr || phys >= lanes[i].addr + lanes[i].len) continue;
                if (at < 0 || lanes[i].len < lanes[at].len) at = i;
            }
            if (at >= 0 && lanes[at].where) textf(caption, sizeof(caption), "%s \xC2\xB7 %s", lanes[at].name, lanes[at].where);
            else if (at >= 0) textf(caption, sizeof(caption), "%s", lanes[at].name);
        }
        else {
            /* A pixel or a tile, read by its line's band or with the frame's end. */
            VdpLayout L = frameEndLayout(s);
            int group = -1, lines = screenLines(s), d = bandFocus(f) ? f.b : (f.b - vdpScreenScroll(s, L)) & 0xff;
            if (f.kind == FOCUS_CHAR) viewLayout(s, false, &L, &group);
            if (f.kind == FOCUS_PIXEL) {
                textf(head, sizeof(head), ts(TS_SEL_HEAD_PIXEL), f.a, d);
            }
            else {
                int ch = focusChar(s, L, f);
                glyph = UI_ICON_PATTERNS;
                if (ch >= 0) textf(head, sizeof(head), ts(TS_SEL_HEAD_TILE), ch & 0xff, ch & 0xff);
                else textf(head, sizeof(head), "%s", ts(TS_KV_CHARACTER));
            }
            const char* mode = shortModeName(bi >= 0 ? raster.bands[bi].mode : L.mode);
            if (bandFocus(f) && bi < 0) textf(caption, sizeof(caption), ts(TS_SEL_BAND_GONE), f.b);
            else if (bi >= 0 && multi) textf(caption, sizeof(caption), ts(TS_SEL_BAND_HEAD), f.b, raster.bands[bi].first, raster.bands[bi].last, mode);
            else if (f.kind == FOCUS_CHAR && group >= 0) groupLabel(group, caption, sizeof(caption));
            else if (f.kind == FOCUS_CHAR) textf(caption, sizeof(caption), "%s", mode);
            else if (bi >= 0 || d < lines) textf(caption, sizeof(caption), ts(TS_SEL_CAPTION_LINE), d, mode);
            else textf(caption, sizeof(caption), ts(TS_SEL_CAPTION_SCROLL), f.b, mode);
        }

        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ui::Px(24), right = p.x + ImGui::GetContentRegionAvail().x, x = p.x, cy = floorf(p.y + h / 2);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ui::HasIcons()) {
            if (art) ui::DrawPixelArt(dl, ImVec2(x + ui::Px(8), cy), *art, ui::DECOR_NONE, t.text);
            else ui::DrawIcon(dl, ImVec2(x + ui::Px(8), cy), glyph, 16, t.text);
            x += ui::Px(24);
        }
        ui::PushFont(UI_FONT_STRONG);
        ImVec2 hs = ImGui::CalcTextSize(head);
        bool overHead = ImGui::ItemAdd(ImRect(ImVec2(x, p.y), ImVec2(right - ui::Px(36), p.y + h)), 0) &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        ui::TextFit(dl, ImVec2(x, floorf(cy - hs.y / 2)), right - ui::Px(36), head, t.text, overHead);
        ui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(right - ui::Px(28), p.y));
        /* A button that lets go, not a toggle: no accent fill. */
        if (ui::IconButton("##pin", UI_ICON_PINNED, "\xC3\x97", ts(TS_PIN_RELEASE_TIP), false, ImVec2(28, 24), 14)) {
            if (band) {
                selectBand(-1);
            }
            else {
                f.pinned = false;
                f.kind = FOCUS_NONE;
            }
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(right - p.x, h));
        if (caption[0]) {
            ui::PushFont(UI_FONT_BODY, 12);
            ui::TextFitItem(caption, uiTokens().textSecondary);
            ui::PopFont();
        }
    }

    void selectionBody(const VdpDebugState* s, Focus& f)
    {
        const UiTokens& t = uiTokens();
        VdpLayout L = frameEndLayout(s);
        int group = -1, firstLine = 0;    /* a tile's b is a scroll line */
        /* A character comes from the Patterns view, which may read a group of bands. */
        if (f.kind == FOCUS_CHAR && !viewLayout(s, false, &L, &group)) return;
        int bi = bandOfFocus(f);
        /* A band's pin kept while its line has no band of its kind to read: the caption says so. */
        if (bandFocus(f) && bi < 0) return;
        if (bi >= 0) {
            L = withPal(vdpLayoutOf(raster.bands[bi]), palOfBand(bi));
            firstLine = -1;
        }
        int mode = L.mode;

        if (f.kind == FOCUS_SPRITE) {
            spriteSelection(s, f);
            return;
        }
        if (f.kind == FOCUS_TILE || f.kind == FOCUS_CHAR) {
            VdpCharInfo ci;
            bool ok = f.kind == FOCUS_TILE ? vdpNameTableChar(s, L, f.a, f.b, firstLine, &ci) : vdpSheetChar(s, L, f.a, f.b, &ci);
            if (!ok || !vdpIsPatternMode(mode)) return;
            /* A band's or the frame end's registers get a sheet of their own, so the Patterns view keeps its. */
            bool own = bi >= 0 || f.kind == FOCUS_TILE;
            ViewPicture& sheet = own ? selSheetPic : sheetPic;
            if (own) decodeSheet(s, L, selSheetPic);
            else ensureSheet(s, L);
            int sx, sy;
            sheetCell(ci.ch, &sx, &sy);
            /* Rows 24-31 of SCREEN 2/4 may use a fourth block the sheet does not hold. */
            bool direct = sy >= sheet.h;
            uint32_t pal[16];
            vdpPaletteRgba(s, L, color0(), pal);
            float cell = ui::Px(10);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            int cw = cellWidth(mode);
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < cw; x++) {
                    size_t i = (size_t)(sy + y) * sheet.w + sx + x;
                    uint32_t c = i < sheet.img.size() ? sheet.img[i] : 0;
                    if (direct) {
                        int pa = ci.patternAddr + y, ca = ci.colorAddr + y;
                        int pv = pa < VDP_DEBUG_VRAM_SIZE ? s->vram[pa] : 0, cv = ca >= 0 && ca < VDP_DEBUG_VRAM_SIZE ? s->vram[ca] : 0;
                        c = (pv << x) & 0x80 ? pal[cv >> 4] : pal[cv & 15];
                    }
                    ImVec2 a(p.x + x * cell, p.y + y * cell);
                    ImVec2 b(a.x + cell, a.y + cell);
                    if ((c >> 24) == 0) dl->AddRectFilled(a, b, ((x + y) & 1) ? t.checkerA : t.checkerB);
                    else dl->AddRectFilled(a, b, rgbaToCol(c));
                }
            }
            dl->AddRect(p, ImVec2(p.x + cw * cell, p.y + 8 * cell), t.controlStrongStroke);
            ImGui::Dummy(ImVec2(cw * cell, 8 * cell));
            ImGui::SameLine(0, ui::Px(12));
            ImGui::BeginGroup();
            if (ui::BeginKeyValue("selkv")) {
                ui::KeyValueMono(ts(TS_KV_CHARACTER), "%d (&H%02X)", ci.ch & 0xff, ci.ch & 0xff);
                if (mode == 2 || mode == 4) ui::KeyValue(ts(TS_KV_THIRD), "%d%s", ci.ch >> 8, ci.ch >> 8 == 3 ? ts(TS_KV_THIRD_ROWS) : "");
                if (f.kind == FOCUS_TILE) {
                    ui::KeyValueMono(ts(TS_KV_CELL), "%d, %d", ci.col, ci.row);
                    ui::KeyValueMono(ts(TS_KV_NAME), "%05X", ci.nameAddr);
                }
                ui::KeyValue(ts(TS_KV_USED), ts(TS_KV_TIMES), vdpCharUseCount(s, L, layoutLines(s, L.regs), -1, ci.ch));
                /* Text modes colour every cell from R#7, and from R#12 too while TEXT2 blinks. */
                if (mode == 0 || mode == 13) {
                    ui::KeyValue(ts(TS_KV_TEXT_COLOURS), ts(TS_TEXT_COLOURS_VALUE), 7, L.regs[7] >> 4, L.regs[7] & 15);
                    if (mode == 13 && L.regs[13] != 0) ui::KeyValue("", ts(TS_TEXT_COLOURS_VALUE), 12, L.regs[12] >> 4, L.regs[12] & 15);
                }
                ui::EndKeyValue();
            }
            ImGui::EndGroup();

            /* One line per pattern row: address, byte, bits, colour byte. */
            ui::PushFont(UI_FONT_MONO);
            bool rowColours = mode == 2 || mode == 4;
            char line[64];
            for (int r = 0; r < 8; r++) {
                int pa = ci.patternAddr + r;
                int pv = pa < VDP_DEBUG_VRAM_SIZE ? s->vram[pa] : 0;
                char bits[9];
                for (int b = 0; b < 8; b++) bits[b] = (pv << b) & 0x80 ? '#' : '.';
                bits[8] = 0;
                if (rowColours) {
                    int ca = ci.colorAddr + r;
                    int cv = ca < VDP_DEBUG_VRAM_SIZE ? s->vram[ca] : 0;
                    textf(line, sizeof(line), "%05X %02X %s  %05X %02X", pa, pv, bits, ca, cv);
                }
                else {
                    textf(line, sizeof(line), "%05X %02X %s", pa, pv, bits);
                }
                ui::TextFitItem(line);
            }
            if (mode == 1 && ci.colorAddr >= 0) {
                textf(line, sizeof(line), ts(TS_COLOUR_ADDR), ci.colorAddr, s->vram[ci.colorAddr]);
                ui::TextFitItem(line);
            }
            ui::PopFont();
            return;
        }
        if (f.kind == FOCUS_PIXEL) {
            VdpBitmapOptions o;
            VdpPixelInfo px;
            /* A band's pin: b is a display line. */
            if (bi < 0) return;
            vdpBandBitmap(s, raster.bands[bi], &o);
            o.color0 = color0();
            if (!vdpBitmapPixel(s, L, o, f.a, f.b, &px)) return;
            ui::ColorSwatch("##px", rgbaToCol(px.rgba | 0xff000000u), 40);
            ImGui::SameLine(0, ui::Px(12));
            if (ui::BeginKeyValue("pxkv")) {
                ui::KeyValueMono(ts(TS_KV_PIXEL), "%d, %d", f.a, f.b);
                ui::KeyValueMono(ts(TS_KV_SCROLL_LINE), "%d", (f.b + L.regs[23]) & 0xff);
                ui::KeyValueMono("VRAM", "%05X", px.logical);
                ui::KeyValueMono(ts(TS_KV_PHYSICAL), "%05X", px.physical);
                ui::KeyValueMono(ts(TS_KV_VALUE), "%d (&H%02X)", px.value, px.value);
                ui::EndKeyValue();
            }
            return;
        }
        /* A VRAM address or block. */
        UInt32 phys = toPhysical(s, f.space, f.addr);
        UInt32 start = f.len >= 256 ? phys & ~255u : phys & ~15u;
        if (ui::BeginKeyValue("vkv")) {
            ui::KeyValueMono(ts(TS_KV_PHYSICAL), "%05X", phys);
            if (f.len >= 256) ui::KeyValueMono(ts(TS_KV_BLOCK), "%05X-%05X", start, start + 255);
            ui::EndKeyValue();
        }
        ui::PushFont(UI_FONT_MONO);
        for (int row = 0; row < 4; row++) {
            UInt32 a = start + row * 8;
            if (a + 8 > (UInt32)VDP_DEBUG_VRAM_SIZE) break;
            const UInt8* v = &s->vram[a];
            char line[64];
            textf(line, sizeof(line), "%05X  %02X %02X %02X %02X %02X %02X %02X %02X", a, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            ui::TextFitItem(line);
        }
        ui::PopFont();
    }

    /* The bands of this frame are drawn in more than one screen mode. */
    bool mixedModes() const
    {
        for (const VdpBand& b : raster.bands) {
            if (b.mode != raster.bands[0].mode) return true;
        }
        return false;
    }

    void bandText(const VdpDebugState* s, int i, char* out, size_t size) const
    {
        const VdpBand& b = raster.bands[i];
        const VdpBand* p = i > 0 ? &raster.bands[i - 1] : NULL;
        const UInt8* r = b.regs;
        size_t at = 0;
        auto add = [&](const char* fmt, int v, int w) {
            if (at < size) at += (size_t)textf(out + at, size - at, "%s", at ? ts(TS_SEP_ITEM) : "");
            if (at < size) at += (size_t)textf(out + at, size - at, fmt, v, w);
        };
        bool modeChanged = p && b.mode != p->mode;
        if (modeChanged || (!p && mixedModes())) {
            if (at < size) at += (size_t)textf(out + at, size - at, "%s", shortModeName(b.mode));
        }
        if (modeChanged || (b.page >= 0 ? !p || b.page != p->page || b.evenSide != p->evenSide : !p || r[2] != p->regs[2])) {
            if (b.page >= 0 && b.evenSide) add(ts(TS_BAND_PAGE_EVEN), b.page, 0);
            else if (b.page >= 0) add(ts(TS_BAND_PAGE), b.page, 0);
            else if (b.mode >= 0) {
                VdpTables tb;
                vdpDecodeTables(s, vdpLayoutOf(b), &tb);
                add(ts(TS_BAND_NAME_TABLE), tb.name, 0);
            }
        }
        if (!p || r[23] != p->regs[23]) add(ts(TS_BAND_VSCROLL), r[23], 0);
        if (p && r[18] != p->regs[18]) add(ts(TS_BAND_ADJUST), r[18], 0);
        if (p && r[25] != p->regs[25]) add("R#25 &H%02X", r[25], 0);
        if (p && (r[26] != p->regs[26] || r[27] != p->regs[27])) add(ts(TS_BAND_HSCROLL), ((r[26] & 0x3f) << 3) - (r[27] & 7), 0);
        /* The other registers the band splits on, by number. */
        static const int tables[] = { 1, 3, 4, 5, 6, 8, 10, 11 };
        for (int reg : tables) {
            int mask = reg == 1 ? 0x03 : reg == 8 ? 0x20 : 0xff;
            if (p && !modeChanged && ((r[reg] ^ p->regs[reg]) & mask)) add("R#%d &H%02X", reg, r[reg]);
        }
        /* Writes that split a line of this band part way along it, with the dot they take effect from. */
        for (const VdpRasterMark& m : raster.marks) {
            int d = m.line - raster.firstLine;
            if (m.splitDot < 0 || m.prev || d < 0 || d >= (int)raster.bandOfLine.size() || raster.bandOfLine[d] != i) continue;
            int width = splitWidth(m);
            if (width < 0) width = vdpIsBitmapMode(s->screenMode) ? vdpBitmapWidth(s->screenMode) : 256;
            if (at < size) at += (size_t)textf(out + at, size - at, "%s", at ? ts(TS_SEP_ITEM) : "");
            if (at < size) at += (size_t)textf(out + at, size - at, ts(TS_SPLITS_DOT), m.reg, d, m.splitDot * width / 256, width);
        }
        if (b.prev && at < size) textf(out + at, size - at, "%s%s", at ? ts(TS_SEP_CLAUSE) : "", ts(TS_BAND_PREV));
        if (b.unknown && at < size) textf(out + at, size - at, "%s%s", at ? ts(TS_SEP_CLAUSE) : "", ts(TS_BAND_UNKNOWN));
    }

    /* What band i's row does not show: R#7, R#12 or R#13 written part way down it, and TEXT2 blink. */
    void bandNotes(int i, char* out, size_t size) const
    {
        const VdpBand& b = raster.bands[i];
        bool later = false, blink = b.mode == 13 && b.regs[13] != 0;
        for (const VdpRasterMark& m : raster.marks) {
            int d = m.fromLine - raster.firstLine;
            if (m.kind == VDP_RASTER_REG && m.prev == b.prev && (m.reg == 7 || m.reg == 12 || m.reg == 13) && m.value != m.aux &&
                d > b.first && d <= b.last) later = true;
        }
        textf(out, size, "%s%s%s", later ? ts(TS_SPLITS_LATER_REGS) : "", later && blink ? "\n" : "", blink ? ts(TS_SPLITS_BLINK) : "");
    }

    /* A small icon and a count in the Splits header; tip on hover. */
    static void headerCount(const char* id, const char* glyph, int count, const char* tip)
    {
        const UiTokens& t = uiTokens();
        char num[16];
        snprintf(num, sizeof(num), "%d", count);
        ui::PushFont(UI_FONT_BODY, 12);
        ImVec2 ns = ImGui::CalcTextSize(num), p = ImGui::GetCursorScreenPos();
        float icon = ui::HasIcons() ? ui::Px(15) : 0, h = ImGui::GetFrameHeight(), cy = floorf(p.y + h / 2);
        ImGui::InvisibleButton(id, ImVec2(icon + ns.x, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (icon > 0) ui::DrawIcon(dl, ImVec2(p.x + ui::Px(6), cy), glyph, 12, t.textSecondary);
        dl->AddText(ImVec2(p.x + icon, floorf(cy - ns.y / 2)), t.textSecondary, num);
        ui::PopFont();
        ui::ItemTooltip(tip);
    }

    void splitsBody(const VdpDebugState* s)
    {
        const UiTokens& t = uiTokens();
        ensureRaster(s);
        const VdpRasterView& r = raster;
        int ints = 0, ie = 0, intLine = -1;
        for (const VdpRasterMark& m : r.marks) {
            if (m.kind == VDP_RASTER_LINEINT && !m.prev) {
                if (intLine < 0) intLine = m.line - r.firstLine;
                ints++;
                ie |= m.aux;
            }
        }
        /* The header: the title, register changes and line interrupts. */
        char intsTip[256];
        if (ints) textf(intsTip, sizeof(intsTip), ts(TS_SPLITS_INTS_TIP), ints, intLine, ts(ie ? TS_ON : TS_OFF));
        else textf(intsTip, sizeof(intsTip), "%s", ts(TS_SPLITS_NO_INTS_TIP));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ui::PushFont(UI_FONT_STRONG);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ts(TS_CARD_SPLITS));
        ui::PopFont();
        if (r.valid) {
            ImGui::SameLine(0, ui::Px(8));
            headerCount("##changes", UI_ICON_EDIT, r.changes, ts(TS_SPLITS_CHANGES_TIP));
            ImGui::SameLine(0, ui::Px(10));
            headerCount("##ints", UI_ICON_LIGHTNING, ints, intsTip);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));
        if (!r.valid) {
            ui::TextFitItem("\xE2\x80\x94", uiTokens().textSecondary);
            return;
        }
        /* The pill row and the band rows come and go between frames; holding their room keeps the rows below in place. */
        float y0 = ImGui::GetCursorPosY();
        bool any = false;
        auto pill = [&](const char* text, ui::UiTone tone, const char* tip) {
            if (any) ImGui::SameLine(0, ui::Px(4));
            ui::StatusPill(text, tone);
            ui::ItemTip(tip);
            any = true;
        };
        if (r.dropped) pill(ts(TS_PILL_LOG_FULL), ui::TONE_CRITICAL, NULL);
        else if (!r.endMatches) pill(ts(TS_PILL_MISMATCH), ui::TONE_CRITICAL, ts(TS_SPLITS_MISMATCH));
        if (!r.complete) pill(ts(TS_PILL_INCOMPLETE), ui::TONE_CAUTION, NULL);
        if (r.foreign) pill(ts(TS_PILL_DEBUGGER_EDITS), ui::TONE_CAUTION, NULL);
        if (!r.ended) pill(ts(TS_PILL_NOT_ENDED), ui::TONE_NEUTRAL, NULL);
        if (r.prevUsed && !r.prevComplete) pill(ts(TS_PILL_PREV_INCOMPLETE), ui::TONE_CAUTION, NULL);
        if (r.prevUsed && r.prevForeign && !r.foreign) pill(ts(TS_PILL_DEBUGGER_EDITS), ui::TONE_CAUTION, NULL);
        ui::HoldHeight(pillHold, y0);

        char text[512];
        float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetTextLineHeight() + ui::Px(6);
        /* At most SPLITS_ROWS band rows, the picked band always among them; the rest behind a row that lists them. */
        const int SPLITS_ROWS = 8;
        int n = (int)r.bands.size(), rows = n < SPLITS_ROWS ? n : SPLITS_ROWS;
        int last = n > SPLITS_ROWS && selBand >= SPLITS_ROWS && selBand < n ? selBand : rows - 1;
        y0 = ImGui::GetCursorPosY();
        for (int k = 0; k < rows; k++) {
            int i = k == rows - 1 ? last : k;
            const VdpBand& b = r.bands[i];
            char desc[512];
            bandText(s, i, desc, sizeof(desc));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##band", ImVec2(w, h))) {
                /* A band the card does not show goes on it; the one it shows is let go. */
                bool onCard = selBand == i && (cardBand || !shell::shownFocus().pinned);
                selectBand(onCard ? -1 : i);
            }
            bool hovered = ImGui::IsItemHovered(), tip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
            ImGui::PopID();
            char notes[512] = "";
            if (tip) bandNotes(i, notes, sizeof(notes));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 q(p.x + w, p.y + h);
            if (i == selBand || hovered) dl->AddRectFilled(p, q, i == selBand ? t.subtleSelected : t.subtleHover, ui::Px(4));
            float sz = ui::Px(8), ty = floorf(p.y + (h - ImGui::GetTextLineHeight()) / 2);
            dl->AddRectFilled(ImVec2(p.x + ui::Px(4), floorf(p.y + (h - sz) / 2)), ImVec2(p.x + ui::Px(4) + sz, floorf(p.y + (h + sz) / 2)),
                              bandColour(i), ui::Px(2));
            char range[24];
            if (b.first == b.last) snprintf(range, sizeof(range), "%d", b.first);
            else snprintf(range, sizeof(range), "%d-%d", b.first, b.last);
            ui::PushFont(UI_FONT_MONO);
            dl->AddText(ImVec2(p.x + ui::Px(18), ty + ui::Px(1)), t.text, range);
            ui::PopFont();
            ui::TextFit(dl, ImVec2(p.x + ui::Px(80), ty), q.x - ui::Px(4), desc, b.prev ? t.textSecondary : t.text, tip, notes);
        }
        if (n > rows) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##morebands", ImVec2(w, h))) ImGui::OpenPopup("##morebandlist");
            bool hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), t.subtleHover, ui::Px(4));
            textf(text, sizeof(text), ts(TS_SPLITS_MORE), n - rows);
            dl->AddText(ImVec2(p.x + ui::Px(18), floorf(p.y + (h - ImGui::GetTextLineHeight()) / 2)), t.textSecondary, text);
        }
        /* Submitted every frame, so the list closes rather than lingers once the bands fit again. */
        float lw = fmaxf(w, ui::Px(320));
        ImGui::SetNextWindowSizeConstraints(ImVec2(lw, 0), ImVec2(lw, ui::Px(320)));
        if (ImGui::BeginPopup("##morebandlist")) {
            if (n <= rows) ImGui::CloseCurrentPopup();
            float iw = ImGui::GetContentRegionAvail().x;
            for (int i = rows - 1; i < n && n > rows; i++) {
                if (i == last) continue;
                const VdpBand& b = r.bands[i];
                char desc[512];
                bandText(s, i, desc, sizeof(desc));
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::PushID(i);
                if (ImGui::Selectable("##band", false, 0, ImVec2(iw, h))) selectBand(i);
                bool tip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
                ImGui::PopID();
                char notes[512] = "";
                if (tip) bandNotes(i, notes, sizeof(notes));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float sz = ui::Px(8), ty = floorf(p.y + (h - ImGui::GetTextLineHeight()) / 2);
                dl->AddRectFilled(ImVec2(p.x + ui::Px(4), floorf(p.y + (h - sz) / 2)), ImVec2(p.x + ui::Px(4) + sz, floorf(p.y + (h + sz) / 2)),
                                  bandColour(i), ui::Px(2));
                char range[24];
                if (b.first == b.last) snprintf(range, sizeof(range), "%d", b.first);
                else snprintf(range, sizeof(range), "%d-%d", b.first, b.last);
                ui::PushFont(UI_FONT_MONO);
                dl->AddText(ImVec2(p.x + ui::Px(18), ty + ui::Px(1)), t.text, range);
                ui::PopFont();
                ui::TextFit(dl, ImVec2(p.x + ui::Px(80), ty), p.x + iw - ui::Px(4), desc, b.prev ? t.textSecondary : t.text, tip, notes);
            }
            ImGui::EndPopup();
        }
        ui::HoldHeight(bandHold, y0);
        if (!r.ended) ui::TextFitItem(ts(r.prevUsed ? TS_SPLITS_PAUSED_PREV : TS_SPLITS_PAUSED_UNKNOWN), uiTokens().textSecondary);
    }

    void cardDisplay(const VdpDebugState* s)
    {
        bool msx1 = vdpIsMsx1(s);
        if (!ui::BeginKeyValue("disp")) return;
        ui::KeyValue(ts(TS_KV_CHIP), "%s", vdpVersionName(s->version));
        ui::KeyValue(ts(TS_KV_MODE), "%s", vdpModeName(s->screenMode));
        ui::KeyValue("VRAM", "%d KB", s->vramSize / 1024);
        ui::KeyValue(ts(TS_KV_DISPLAY), "%s", ts(s->screenOn ? TS_ON : TS_DISPLAY_OFF_BLANKED));
        if (!msx1) {
            if (vdpIsFlatInterlace(s, s->regs)) ui::KeyValue(ts(TS_KV_INTERLACE), "%s", ts(vdpIsBitmapMode(s->screenMode) ? TS_INTERLACE_FLAT : TS_INTERLACE_FLAT_RASTER));
            else ui::KeyValue(ts(TS_KV_INTERLACE), "%s", ts((s->regs[9] & 0x08) ? ((s->regs[9] & 0x04) ? TS_INTERLACE_EO_PAGES : TS_ON) : TS_OFF));
            ui::KeyValueMono(ts(TS_KV_VSCROLL), "%d", s->regs[23]);
            int adj = s->regs[18];
            /* As SET ADJUST moves the picture, with R#18's nibble beside it. */
            ui::KeyValueMono(ts(TS_KV_ADJUST), "H %d (%X)  V %d (%X)", vdpSetAdjust(adj, false), adj & 15,
                             vdpSetAdjust(adj, true), (adj >> 4) & 15);
            if (s->version == VDP_V9958 || s->version == VDP_V9968) {
                ui::KeyValueMono(ts(TS_KV_HSCROLL), "%d", ((s->regs[26] & 0x3f) << 3) - (s->regs[27] & 7));
            }
            ui::KeyValue(ts(TS_KV_BACKDROP), "%d", s->regs[7] & 15);
        }
        ui::EndKeyValue();
    }

    void cardTables(const VdpDebugState* s)
    {
        tablesList(s, vdpLayoutOf(s), true);
    }

    /* The tables a layout reads, with their addresses; lit lets the map's hover light them. */
    void tablesList(const VdpDebugState* s, const VdpLayout& l, bool lit)
    {
        const UiTokens& t = uiTokens();
        VdpRange r[8];
        int n = vdpTableRanges(s, l, r);
        if (n == 0) {
            ui::TextFitItem(ts(TS_NO_TABLES), t.textSecondary);
            return;
        }
        char range[8][24];
        const char* ranges[8];
        for (int i = 0; i < n; i++) {
            snprintf(range[i], sizeof(range[i]), "%05X-%05X", r[i].addr, r[i].addr + r[i].len - 1);
            ranges[i] = range[i];
        }
        if (!ImGui::BeginTable("tbl", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("addr", ImGuiTableColumnFlags_WidthFixed, monoWidth(ranges, n));
        int hover = lit ? shell::hoverSeries() : -1;
        for (int i = 0; i < n; i++) {
            ImGui::TableNextRow();
            if (hover == r[i].kind) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, t.subtleSelected);
            ImGui::TableNextColumn();
            float sz = ui::Px(10);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float dy = floorf((ImGui::GetTextLineHeight() - sz) / 2);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + dy), ImVec2(p.x + sz, p.y + dy + sz),
                                                      t.series[r[i].kind % 6], ui::Px(2));
            ImGui::Dummy(ImVec2(sz, ImGui::GetTextLineHeight()));
            ImGui::SameLine(0, ui::Px(8));
            ui::TextFitItem(r[i].name);
            ImGui::TableNextColumn();
            ui::PushFont(UI_FONT_MONO);
            fitCell(range[i]);
            ui::PopFont();
        }
        ImGui::EndTable();
    }

    void cardSprites(const VdpDebugState* s)
    {
        VdpTables tb;
        ensureSprites(s);
        /* The table shown is the Sprites tab's; the counts are of every line, each by its own table. */
        SprView v = shownTable();
        int slot = v.fr != NULL ? 0 : -1;
        const VdpSpriteFrame& fr = v.fr != NULL ? *v.fr : sprites;
        if (fr.mode == 0) {
            if (ui::BeginKeyValue("spr")) {
                ui::KeyValue(ts(TS_KV_SPRITE_MODE), "%s", ts(TS_KV_NONE));
                ui::EndKeyValue();
            }
            return;
        }
        int overLines = slot >= 0 ? scene.overLines : fr.overLines, dropLine = slot >= 0 ? scene.firstDropLine : fr.firstDropLine;
        int dropped = slot >= 0 ? scene.firstDropped : fr.firstDropped, collX = slot >= 0 ? scene.collX : fr.collX;
        int collLine = slot >= 0 ? scene.collLine : fr.collLine;
        vdpDecodeTables(s, vdpLayoutOf(fr), &tb);
        char tag[128];
        /* A shown table is always named, so the line does not come and go with the pictures. */
        if (v.fr != NULL) ui::TextFitItem(slotLabel(v.slot >= 0 ? scene.slots[v.slot] : sprSelSlot, fr.screenMode, tag, sizeof(tag)), uiTokens().textSecondary);
        else if (spriteGroup >= 0) ui::TextFitItem(groupLabel(spriteGroup, tag, sizeof(tag)), uiTokens().textSecondary);
        if (ui::BeginKeyValue("spr")) {
            /* What the emulator's video options change is said on the row it changes, the why on hover. */
            bool hidden = !vdpGetSpritesEnable(), noLimit = vdpGetNoSpritesLimit() != 0;
            const char* frameEnd = !scene.recorded && spriteGroup < 0 && groups.empty() ? ts(TS_SPR_NOTE_FRAME_END) : NULL;
            char value[192];
            ui::KeyValue(ts(TS_KV_SPRITE_MODE), "%d", fr.mode);
            ui::KeyValue(ts(TS_KV_SIZE), "%s%s", fr.size == 16 ? "16x16" : "8x8", fr.mag == 2 ? ts(TS_KV_SIZE_MAGNIFIED) : "");
            const char* off = sprOffWhy(fr);
            if (off != NULL) textf(value, sizeof(value), ts(TS_KV_SHOWN_NO), off);
            else textf(value, sizeof(value), "%s", ts(TS_YES));
            ui::KeyValueTip(ts(TS_KV_SHOWN), hidden ? ts(TS_SPR_NOTE_HIDDEN) : NULL, "%s%s", value, hidden ? ts(TS_SPR_HIDDEN_SUFFIX) : "");
            ui::KeyValue(ts(TS_KV_ACTIVE), ts(TS_KV_ACTIVE_VALUE), fr.stopIndex, fr.stopY);
            /* These rows are drawn on every picture, so rows and columns stay put as sprites drop in and out. */
            textf(value, sizeof(value), ts(TS_KV_OVER_LIMIT_VALUE), overLines, fr.limit);
            ui::KeyValueTip(ts(TS_KV_OVER_LIMIT), noLimit ? ts(TS_SPR_NOTE_NO_LIMIT) : NULL, "%s%s", value, noLimit ? ts(TS_SPR_NO_LIMIT_SUFFIX) : "");
            if (dropped >= 0) {
                ui::KeyValue(ts(TS_KV_FIRST_DROPPED), ts(TS_KV_FIRST_DROPPED_VALUE), dropped, dropLine);
                ui::KeyValueMonoTip(ts(TS_KV_S0_WOULD_READ), frameEnd, "&H%02X", 0x40 | dropped);
            }
            else {
                ui::KeyValue(ts(TS_KV_FIRST_DROPPED), "%s", ts(TS_KV_NONE));
                ui::KeyValueMonoTip(ts(TS_KV_S0_WOULD_READ), frameEnd, "\xE2\x80\x94");
            }
            if (collLine >= 0) ui::KeyValue(ts(TS_KV_FIRST_COLLISION), ts(TS_KV_FIRST_COLLISION_VALUE), collX, collLine);
            else ui::KeyValue(ts(TS_KV_FIRST_COLLISION), "%s", ts(TS_KV_NONE));
            if (tb.spriteAttr >= 0)    ui::KeyValueMono(ts(TS_KV_ATTRIBUTES), "%05X", tb.spriteAttr);
            if (tb.spriteColor >= 0)   ui::KeyValueMono(ts(TS_KV_COLOURS), "%05X", tb.spriteColor);
            if (tb.spritePattern >= 0) ui::KeyValueMono(ts(TS_KV_PATTERNS), "%05X", tb.spritePattern);
            ui::EndKeyValue();
        }
    }

    void cardStatus(const VdpDebugState* s)
    {
        const UInt8* st = s->status;
        bool msx1 = vdpIsMsx1(s);
        if (ui::BeginKeyValue("stat")) {
            ui::KeyValue(ts(TS_KV_FRAME_INT), "%s%s", ts((s->regs[1] & 0x20) ? TS_ENABLED : TS_OFF),
                         (st[0] & 0x80) ? ts(TS_PENDING_SUFFIX) : "");
            if (!msx1) {
                ui::KeyValue(ts(TS_KV_LINE_INT), ts(TS_LINE_INT_VALUE), ts((s->regs[0] & 0x10) ? TS_ENABLED : TS_OFF),
                             s->regs[19], (st[1] & 1) ? ts(TS_PENDING_SUFFIX) : "");
            }
            ui::KeyValue(ts(TS_KV_5TH_SPRITE), ts((st[0] & 0x40) ? TS_FIFTH_YES : TS_FIFTH_NO), st[0] & 0x1f);
            if (msx1) {
                ui::KeyValue(ts(TS_KV_COLLISION), "%s", ts((st[0] & 0x20) ? TS_YES : TS_NO));
            }
            else {
                ui::KeyValue(ts(TS_KV_COLLISION), ts((st[0] & 0x20) ? TS_COLLISION_YES_AT : TS_COLLISION_NO_LAST),
                             st[3] | ((st[4] & 1) << 8), st[5] | ((st[6] & 3) << 8));
            }
            ui::EndKeyValue();
        }
    }

    void cardCommand(const VdpDebugState* s)
    {
        const UInt8* c = s->cmdRegs;
        int cmr = c[14];
        bool ce = (s->status[2] & 1) != 0;
        ui::StatusPill(ts(ce ? TS_PILL_RUNNING : TS_PILL_IDLE), ce ? ui::TONE_CAUTION : ui::TONE_NEUTRAL);
        if (ce) ui::ItemTip(ts(TS_CMD_NOTE));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s  %s", cmdName(s, cmr >> 4), logOpName(cmr & 15));
        if (!ui::BeginKeyValue("cmd")) return;
        cmdKeyValues(s);
        ui::KeyValueMono("CLR", "%02X", c[12]);
        ui::KeyValueMono("ARG", "%02X%s%s%s", c[13], (c[13] & 8) ? " DIY" : "", (c[13] & 4) ? " DIX" : "",
                         (c[13] & 1) ? " MAJ" : "");
        ui::EndKeyValue();
    }

    /* A V9968's EPAL: 256 colours of 5 bit R G B, 16 to a row; R#7 is a whole byte. */
    void extPalette(const VdpDebugState* s)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::Px(2), ui::Px(2)));
        for (int i = 0; i < 256; i++) {
            char id[16], tip[256];
            int v = s->paletteExtReg[i];
            uint32_t rgb = s->paletteExt[i];
            snprintf(id, sizeof(id), "##epal%d", i);
            int tl = textf(tip, sizeof(tip), ts(TS_PAL_TIP_EPAL), i, (v >> 10) & 31, (v >> 5) & 31, v & 31, rgb);
            if (s->regs[7] == i && tl >= 0 && (size_t)tl < sizeof(tip)) textf(tip + tl, sizeof(tip) - tl, "%s", ts(TS_PAL_TIP_BORDER));
            if (i % 16) ImGui::SameLine();
            ui::ColorSwatch(id, IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255), 15,
                            s->regs[7] == i, tip);
        }
        ImGui::PopStyleVar();
    }

    void cardPalette(const VdpDebugState* s)
    {
        if (s->paletteExtCount == 256) {
            extPalette(s);
            return;
        }
        bool msx1 = vdpIsMsx1(s);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::Px(6), ui::Px(6)));
        for (int i = 0; i < 16; i++) {
            char id[16], tip[256];
            int v = s->paletteReg[i], tl;
            uint32_t rgb = s->paletteRgb[i];
            bool border = (s->regs[7] & 15) == i;
            snprintf(id, sizeof(id), "##pal%d", i);
            if (msx1) tl = textf(tip, sizeof(tip), ts(TS_PAL_TIP_MSX1), i, rgb);
            else tl = textf(tip, sizeof(tip), ts(TS_PAL_TIP), i, (v >> 4) & 7, (v >> 8) & 7, v & 7, rgb);
            if (border && tl >= 0 && (size_t)tl < sizeof(tip)) textf(tip + tl, sizeof(tip) - tl, "%s", ts(TS_PAL_TIP_BORDER));
            if (i % 8) ImGui::SameLine();
            ui::ColorSwatch(id, IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255), 28, border, tip);
        }
        ImGui::PopStyleVar();
        /* The lines whose palette is not the one above. */
        char runs[160] = "", text[384];
        size_t at = 0;
        int known = 0, other = 0;
        for (int d = 0; !msx1 && d < (int)linePal.lineIdx.size(); d++) {
            const UInt32* rgb = lineRgb(d);
            bool differs = rgb != NULL && memcmp(rgb, s->paletteRgb, sizeof(s->paletteRgb)) != 0;
            known += rgb != NULL;
            other += differs;
            if (!differs || (d > 0 && lineRgb(d - 1) && memcmp(lineRgb(d - 1), s->paletteRgb, sizeof(s->paletteRgb)) != 0)) continue;
            int e = d;
            while (e + 1 < (int)linePal.lineIdx.size() && lineRgb(e + 1) && memcmp(lineRgb(e + 1), s->paletteRgb, sizeof(s->paletteRgb)) != 0) e++;
            if (at < sizeof(runs)) at += (size_t)snprintf(runs + at, sizeof(runs) - at, e > d ? "%s%d-%d" : "%s%d", at ? ", " : "", d, e);
        }
        /* One line names them, the sentence behind it on hover. Running, the last one stays 1.5 s
        ** as a recent fact, so it does not blink; paused or stopped, only the picture shown is described. */
        bool all = other > 0 && other == known, recent = false;
        const char* lines = runs;
        if (emulatorGetState() == EMU_RUNNING) {
            double now = ImGui::GetTime();
            if (other > 0) {
                snprintf(palHeldRuns, sizeof(palHeldRuns), "%s", runs);
                palHeldAll = all;
                palHeldAt = now;
            }
            if (palHeldAt < 0 || now - palHeldAt >= 1.5) return;
            recent = true;
            all = palHeldAll;
            lines = palHeldRuns;
        }
        else {
            palHeldAt = -1;
            if (other == 0) return;
        }
        char tip[384];
        if (all) {
            textf(text, sizeof(text), "%s", ts(TS_PAL_OTHER_ALL));
            textf(tip, sizeof(tip), "%s", ts(recent ? TS_PAL_NOTE_ALL_LINES_RECENT : TS_PAL_NOTE_ALL_LINES));
        }
        else {
            textf(text, sizeof(text), ts(TS_PAL_OTHER_LINES), lines);
            textf(tip, sizeof(tip), ts(recent ? TS_PAL_NOTE_LINES_RECENT : TS_PAL_NOTE_LINES), lines);
        }
        ui::TextFitItem(text, uiTokens().textSecondary, true, tip);
    }

    static bool regShown(int index, const void* ref)
    {
        const VdpDebugState* s = (const VdpDebugState*)ref;
        if (index >= 32) return s->cmdRegCount > 0 && index < 32 + s->cmdRegCount;
        return index < s->regCount && index != 24;
    }

    static bool statusShown(int index, const void* ref)
    {
        return index < ((const VdpDebugState*)ref)->statusCount;
    }

    void cardRegisters(const VdpDebugState* s)
    {
        UInt8 vals[64];
        memcpy(vals, s->regs, 32);
        memset(vals + 32, 0, 32);
        memcpy(vals + 32, s->cmdRegs, 32);
        drawRegBank(s->version == VDP_V9968 ? regBankV9968() : regBankV99x8, vals, regFade, regShown, s);
        ImGui::Spacing();
        ui::SectionTitle(ts(TS_SECTION_STATUS));
        drawRegBank(regBankV99x8Status, s->status, statFade, statusShown, s);
    }
};

}

ChipView* createV99x8View()
{
    return new V99x8View();
}
