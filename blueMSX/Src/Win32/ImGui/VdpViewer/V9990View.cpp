/*****************************************************************************
**
** The VDP Viewer's views and cards of a V9990.
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
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiTheme.h"
#include "ImGuiWidgets.h"
#include "ChipView.h"
#include "ViewPicture.h"
#include "RegTables.h"
#include "VdpParts.h"
#include "V9990Decode.h"
#include "V9990Sprite.h"
#include "ToolText.h"
extern "C" {
#include "V9990.h"
#include "V9990Render.h"
#include "Properties.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace {

using namespace parts;

enum { C_OVERVIEW, C_TABLES, C_SPRITES, C_PALETTE, C_REGISTERS, C_COUNT };

bool isBitmap(int mode)
{
    return mode >= V9990_MODE_B0 && mode <= V9990_MODE_B7;
}

/* B4-B7 read the dual palette, as the chip does. */
bool isHiRes(int mode)
{
    return mode >= V9990_MODE_B4 && mode <= V9990_MODE_B7;
}

const char* modeName(int mode)
{
    static const char* names[] = { "P1", "P2", "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7" };
    if (mode == V9990_MODE_STANDBY) return ts(TS_V9_STANDBY);
    if (mode == V9990_MODE_NONE) return ts(TS_V9_NONE);
    return mode >= 0 && mode < V9990_MODE_STANDBY ? names[mode] : "?";
}

int screenWidth(int mode)
{
    switch (mode) {
    case V9990_MODE_B0: return 192;
    case V9990_MODE_B1: return 256;
    case V9990_MODE_B2: return 384;
    case V9990_MODE_B3: return 512;
    case V9990_MODE_B4: return 768;
    case V9990_MODE_B5:
    case V9990_MODE_B6: return 640;
    case V9990_MODE_B7: return 1024;
    case V9990_MODE_P1: return 256;
    case V9990_MODE_P2: return 512;
    }
    return 0;
}

/* Both fields woven into one picture: EO counts only with IL, and only in Bx, as the renderer has it. */
bool fieldsWoven(const V9990DebugState* s)
{
    return s->interlace && (s->regs[7] & 0x04) && isBitmap(s->mode);
}

int screenLines(const V9990DebugState* s)
{
    int lines;
    switch (s->mode) {
    case V9990_MODE_B0: case V9990_MODE_B2: case V9990_MODE_B4: lines = s->isPal ? 290 : 240; break;
    case V9990_MODE_B5: lines = 400; break;
    case V9990_MODE_B6: lines = 480; break;
    default: lines = 212; break;
    }
    if (fieldsWoven(s)) lines *= 2;
    return lines;
}

/* A dot's shape as on a TV, B1's being square; B5 and B6 are 31 kHz modes. */
void dotAspect(const V9990DebugState* s, int* ax, int* ay)
{
    int x = 1, y = 1;
    switch (s->mode) {
    case V9990_MODE_B0: x = 3; y = 2; break;
    case V9990_MODE_B2: x = 3; y = 4; break;
    case V9990_MODE_B3: case V9990_MODE_P2: y = 2; break;
    case V9990_MODE_B4: x = 3; y = 8; break;
    case V9990_MODE_B7: y = 4; break;
    }
    /* Even and odd fields woven together: lines half as tall. */
    if (fieldsWoven(s)) {
        x *= 2;
        if (y % 2 == 0) { x /= 2; y /= 2; }
    }
    *ax = x;
    *ay = y;
}

/* Image rows per TV line: 2 for woven fields and for B5 and B6's 31 kHz lines. */
int lineRows(const V9990DebugState* s)
{
    bool half = fieldsWoven(s) || s->mode == V9990_MODE_B5 || s->mode == V9990_MODE_B6;
    return half ? 2 : 1;
}

int imageWidth(const V9990DebugState* s)
{
    return 256 << ((s->modeR6 >> 2) & 3);
}

int bitsPerDot(const V9990DebugState* s)
{
    static const int bpp[4] = { 2, 4, 8, 16 };
    return bpp[s->modeR6 & 3];
}

int imageRows(const V9990DebugState* s)
{
    return (int)(0x80000LL * 8 / ((long long)imageWidth(s) * bitsPerDot(s)));
}

const char* colourType(const V9990DebugState* s)
{
    switch (s->modeR6 & 3) {
    case 0: return ts(TS_V9_BP2);
    case 1: return ts(TS_V9_BP4);
    case 2:
        switch (s->regs[13] >> 6) {
        case 0: return ts(TS_V9_BP6);
        case 1: return ts(TS_V9_BD8);
        case 2: return "YJK";
        }
        return "YUV";
    }
    return ts(TS_V9_BD16);
}

class V9990View : public ChipView {
public:
    V9990View()
    {
        vramCv.shared = bitmapCv.shared = outCv.shared = patCv.shared = sprCv.shared = &shell::zoom();
        vramCv.frameless = bitmapCv.frameless = outCv.frameless = patCv.frameless = true;
        regFadeReset(regFade);
        regFadeReset(portFade);
    }

    void facts(const void* st, char* out, size_t size) override
    {
        const V9990DebugState* s = (const V9990DebugState*)st;
        int at = textf(out, size, ts(TS_V9_FACTS_MODE), modeName(s->mode));
        if (isBitmap(s->mode) && at >= 0 && (size_t)at < size) {
            at += textf(out + at, size - at, " \xC2\xB7 %d\xC3\x97%d", screenWidth(s->mode), screenLines(s));
        }
        if (at >= 0 && (size_t)at < size) at += textf(out + at, size - at, " \xC2\xB7 %s", s->isPal ? "PAL 50 Hz" : "NTSC 60 Hz");
        if (!s->displayOn && at >= 0 && (size_t)at < size) textf(out + at, size - at, " \xC2\xB7 %s", ts(TS_FACTS_BLANKED));
    }

    int views(ViewDesc* out, int max, const void* st) const override
    {
        const V9990DebugState* s = (const V9990DebugState*)st;
        /* The bitmap modes' cursors take the sprites' tab, so a title switching modes keeps it. */
        bool cursors = s != NULL && isBitmap(s->mode);
        int n = 0;
        if (n < max) out[n++] = ViewDesc{ "screen", ts(TS_VIEW_SCREEN), UI_ICON_SCREEN };
        if (n < max) out[n++] = ViewDesc{ "patterns", ts(TS_VIEW_PATTERNS), UI_ICON_PATTERNS };
        if (n < max) out[n++] = ViewDesc{ "bitmap", ts(TS_VIEW_BITMAP), UI_ICON_BITMAP };
        if (n < max) out[n++] = ViewDesc{ "sprites", ts(cursors ? TS_V9_VIEW_CURSORS : TS_VIEW_SPRITES), NULL,
                                               cursors ? &ui::UI_ART_CURSOR : &ui::UI_ART_SHIP };
        if (n < max) out[n++] = ViewDesc{ "vram", "VRAM", UI_ICON_VRAM };
        return n;
    }

    void drawView(int v, const void* st, Focus& f) override
    {
        const V9990DebugState* s = (const V9990DebugState*)st;
        ViewDesc d[8];
        int n = views(d, 8, st);
        /* A pattern or sprite pinned in P1 or P2 means nothing in a bitmap mode, and back. */
        int family = isBitmap(s->mode) ? 1 : 0;
        if (family != lastFamily && f.pinned && (f.kind == FOCUS_CHAR || f.kind == FOCUS_SPRITE)) {
            f.pinned = false;
            f.kind = FOCUS_NONE;
        }
        lastFamily = family;
        if (v >= 0 && v < n && strcmp(d[v].key, "screen") == 0) {
            /* The pages go under the Screen's card as high as the splitter says; the card takes the rest.
            ** Their IDs are their own, as in drawPages, so the pane's scroll is not the V99x8's. */
            ImGui::PushID("v9990");
            float paneH = fmaxf(0.0f, shell::pagesHeight(ImGui::GetContentRegionAvail().y - ui::Px(8) - shell::pagesChromeH(true)));
            ImGui::PopID();
            ui::BeginTabCard("card", -(ui::Px(8) + shell::pagesChromeH(shell::pages().open) + paneH));
            drawOutput(s, f);
            ui::EndTabCard();
            drawPages(s, f, paneH);
            return;
        }
        ui::BeginTabCard("card");
        if (v >= 0 && v < n && strcmp(d[v].key, "bitmap") == 0) drawBitmap(s, f);
        else if (v >= 0 && v < n && strcmp(d[v].key, "patterns") == 0) drawPatterns(s, f);
        else if (v >= 0 && v < n && strcmp(d[v].key, "sprites") == 0) drawSprites(s, f);
        else if (v >= 0 && v < n) drawVram(s, f);
        ui::EndTabCard();
    }

    int cards(CardDesc* out, int max, const void* st) const override
    {
        const V9990DebugState* s = (const V9990DebugState*)st;
        const CardDesc all[C_COUNT] = {
            { "ov", ts(TS_CARD_OVERVIEW), true, true }, { "tables", ts(TS_CARD_TABLES), true, true },
            { "sprites", ts(TS_CARD_SPRITES), true, true },
            { "pal", ts(TS_CARD_PALETTE), true, true }, { "regs", ts(TS_CARD_REGISTERS), true, true },
        };
        int n = 0;
        for (int i = 0; i < C_COUNT && n < max; i++) {
            out[n] = all[i];
            if (i == C_TABLES || i == C_SPRITES) out[n].shown = s->mode == V9990_MODE_P1 || s->mode == V9990_MODE_P2;
            n++;
        }
        return n;
    }

    void drawCard(int c, const void* st, Focus&) override
    {
        const V9990DebugState* s = (const V9990DebugState*)st;
        switch (c) {
        case C_OVERVIEW:  cardOverview(s); break;
        case C_TABLES:    cardTables(s); break;
        case C_SPRITES:   cardSprites(s); break;
        case C_PALETTE:   cardPalette(s); break;
        case C_REGISTERS: cardRegisters(s); break;
        }
    }

    /* A table of P1 or P2 at CPU addresses; series picks its colour on the map. */
    struct TableRow { int series; const char* name; UInt32 addr, len; };

    /* The tables the pattern modes read; none in the bitmap modes. */
    int tableRows(const V9990DebugState* s, TableRow* out) const
    {
        int n = 0;
        if (s->mode == V9990_MODE_P1) {
            textf(tableNames[0], sizeof(tableNames[0]), ts(TS_V9_TBL_PATTERNS), "A");
            textf(tableNames[1], sizeof(tableNames[1]), ts(TS_V9_TBL_PATTERNS), "B");
            textf(tableNames[2], sizeof(tableNames[2]), ts(TS_V9_TBL_NAMES), "A");
            textf(tableNames[3], sizeof(tableNames[3]), ts(TS_V9_TBL_NAMES), "B");
            out[n++] = TableRow{ 0, tableNames[0], 0x00000, 0x3fe00 };
            out[n++] = TableRow{ 1, tableNames[1], 0x40000, 0x3c000 };
            out[n++] = TableRow{ 2, tableNames[2], 0x7c000, 0x2000 };
            out[n++] = TableRow{ 3, tableNames[3], 0x7e000, 0x2000 };
            out[n++] = TableRow{ 4, ts(TS_RANGE_SPRITE_PATTERNS), (UInt32)(s->regs[25] & 0x0e) << 14, 0x8000 };
            out[n++] = TableRow{ 5, ts(TS_RANGE_SPRITE_ATTRIBUTES), 0x3fe00, 0x200 };
        }
        else if (s->mode == V9990_MODE_P2) {
            out[n++] = TableRow{ 0, ts(TS_RANGE_PATTERN_GENERATOR), 0x00000, 0x78000 };
            out[n++] = TableRow{ 2, ts(TS_RANGE_NAME_TABLE), 0x7c000, 0x4000 };
            out[n++] = TableRow{ 4, ts(TS_RANGE_SPRITE_PATTERNS), (UInt32)(s->regs[25] & 0x0f) << 15, 0x8000 };
            out[n++] = TableRow{ 5, ts(TS_RANGE_SPRITE_ATTRIBUTES), 0x7be00, 0x200 };
        }
        return n;
    }

    int vramLanes(const void* st, VramLane* out, int max) const override
    {
        const V9990DebugState* s = (const V9990DebugState*)st;
        if (!isBitmap(s->mode)) {
            /* P2's interleaved tables are a lane in each half; its attributes sit at 3FE00. */
            TableRow r[8];
            int n = tableRows(s, r), k = 0;
            for (int i = 0; i < n && k + 2 <= max; i++) {
                /* The sprite patterns are read interleaved even at 78000 (R#25 = 0F). */
                bool bx = s->mode == V9990_MODE_P2 && (r[i].addr < 0x78000 || r[i].series == 4);
                UInt32 a = bx ? r[i].addr >> 1 : v9990MapAddress((V9990Mode)s->mode, r[i].addr);
                out[k++] = VramLane{ a, bx ? r[i].len / 2 : r[i].len, r[i].series, r[i].name };
                if (bx) out[k++] = VramLane{ 0x40000 + a, r[i].len / 2, r[i].series, r[i].name };
            }
            return k;
        }
        if (max < 2) return 0;
        /* The rows the screen shows, overlapping runs merged; bitmap bytes
        ** interleave over the two halves of VRAM. */
        unsigned ver;
        const V9990FrameShot* o = (const V9990FrameShot*)shell::frameShot(&ver);
        std::vector<ShownRun> runs;
        shownRuns(s, o != NULL && o->valid ? o : NULL, runs);
        std::sort(runs.begin(), runs.end(), [](const ShownRun& a, const ShownRun& b) { return a.y0 < b.y0; });
        UInt32 rowBytes = (UInt32)(imageWidth(s) * bitsPerDot(s) / 8);
        int k = 0;
        for (size_t i = 0; i < runs.size() && k + 2 <= max;) {
            int y0 = runs[i].y0, y1 = runs[i].y1;
            /* The last pair of lanes takes in the rest when they run out. */
            for (i++; i < runs.size() && (runs[i].y0 <= y1 || k + 4 > max); i++) y1 = runs[i].y1 > y1 ? runs[i].y1 : y1;
            UInt32 a = (UInt32)y0 * rowBytes, len = (UInt32)(y1 - y0) * rowBytes;
            out[k++] = VramLane{ a >> 1, len / 2, 0, ts(TS_V9_DISPLAYED_AREA) };
            out[k++] = VramLane{ 0x40000 + (a >> 1), len / 2, 0, ts(TS_V9_DISPLAYED_AREA) };
        }
        return k;
    }

    UInt32 vramSize(const void*) const override
    {
        return V9990_VRAM_SIZE;
    }

    UInt32 toPhysical(const void* st, int space, UInt32 a) const override
    {
        const V9990DebugState* s = (const V9990DebugState*)st;
        return space == FOCUS_LOGICAL ? v9990MapAddress((V9990Mode)s->mode, a) : a;
    }

    void escape(bool key) override
    {
        /* Only the key lets go of the line clicked on the placement. */
        if (key) sprLineFixed = -1;
    }

    void forget() override
    {
        outPic.forget();
        vramPic.forget();
        bitmapPic.forget();
        stripPic.forget();
        layerPic[0].forget();
        layerPic[1].forget();
        patPic.forget();
        patDotsKey[0] = 0;
        patCol = patRow = -1;
        sprPlacePic.forget();
        sprAtlasPic.forget();
        curPic.forget();
        sprPatPic.forget();
        sprVer = sprBitsVer = ~0u;
        sprHot = sprHotNext = sprPatHot = sprPatHotNext = sprLine = sprLineNext = sprLineFixed = -1;
        outHot = outHotNext = -1;
        outLayer = SL_SHOWN;
        outRowNone.clear();
        outSprShot = outSprPicture = ~0u;
        regFadeReset(regFade);
        regFadeReset(portFade);
    }

    void loadUi(const VdpViewerProperties& p) override
    {
        sprShown = p.v9990.sprShown != 0;
        sprOnly = p.v9990.sprOnly != 0;
        outCv.grid = p.v9990.outGrid != 0;
        ysClear = p.v9990.ysClear != 0;
        bitmapCv.grid = p.v9990.bitmapGrid != 0;
        vramOrder = p.v9990.vramOrder == 1 ? 1 : 0;
        vramTint = p.v9990.vramTint != 0;
        vramCv.grid = p.v9990.vramGrid != 0;
        patLayer = p.v9990.patLayer == 1 ? 1 : 0;
        patNumbers = p.v9990.patNumbers != 0;
        patCv.grid = p.v9990.patGrid != 0;
        transparent0 = p.v9990.transparent0 != 0;
        sprDropped = p.v9990.sprDropped != 0;
        sprBoxes = p.v9990.sprBoxes != 0;
        sprCv.grid = p.v9990.sprGrid != 0;
        curShown = p.v9990.curShown != 0;
        curOnly = p.v9990.curOnly != 0;
        curBoxes = p.v9990.curBoxes != 0;
    }

    void saveUi(VdpViewerProperties& p) const override
    {
        p.v9990.sprShown = sprShown;
        p.v9990.sprOnly = sprOnly;
        p.v9990.outGrid = outCv.grid;
        p.v9990.ysClear = ysClear;
        p.v9990.bitmapGrid = bitmapCv.grid;
        p.v9990.vramOrder = vramOrder;
        p.v9990.vramTint = vramTint;
        p.v9990.vramGrid = vramCv.grid;
        p.v9990.patLayer = patLayer;
        p.v9990.patNumbers = patNumbers;
        p.v9990.patGrid = patCv.grid;
        p.v9990.transparent0 = transparent0;
        p.v9990.sprDropped = sprDropped;
        p.v9990.sprBoxes = sprBoxes;
        p.v9990.sprGrid = sprCv.grid;
        p.v9990.curShown = curShown;
        p.v9990.curOnly = curOnly;
        p.v9990.curBoxes = curBoxes;
    }

private:
    ViewPicture          vramPic, bitmapPic, stripPic, outPic, layerPic[2];
    std::vector<uint8_t> layerDots;          /* the Pages' layer being decoded, apart from the Patterns tab's */
    shell::PagesScroll   pagesScroll;
    float                pagesTw = 0;        /* the pieces' width last drawn, for a new size to keep the left one */
    ui::CanvasState      vramCv, bitmapCv, outCv;
    const V9990FrameShot* outShot = NULL;    /* for the overlay and the strip's shown area; NULL when none came */
    bool                 outDim = false;
    enum { SL_SHOWN, SL_HIDDEN, SL_ONLY };
    bool                 sprShown = true;    /* Output's sprites shown, or hidden */
    bool                 sprOnly = false;    /* ...alone */
    bool                 curShown = true, curOnly = false, curBoxes = false;  /* the same for the cursors, and their boxes */
    int                  outLayer = SL_SHOWN;  /* what outPic was decoded with */
    std::vector<uint8_t> outRowNone;         /* per row: not split, so hidden or alone shows it wrong */
    const V9990DebugState* outState = NULL;  /* for the overlay */
    bool                 outSprites = false, outCursors = false;  /* the overlay boxes the sprites, or the cursors */
    int                  outPin = -1, outHot = -1, outHotNext = -1;  /* the pinned sprite, and the one under the mouse */
    V9990SpriteFrame     outSpr = {};        /* the sprites of the frame Output shows */
    unsigned             outSprShot = ~0u, outSprPicture = ~0u;
    bool                 ysClear = false;
    int                  vramOrder = 0;      /* the VRAM tab's bytes: 0 in CPU order, 1 physical */
    bool                 vramTint = true;    /* the bytes tinted by the VRAM map's lanes */
    RegFade              regFade, portFade;
    std::vector<UInt16>  line;
    const V9990DebugState* ovState = NULL;
    int                  invMode = -1;
    UInt32               invPhys = 0, invLogical = 0;
    mutable char         tableNames[4][48];

    /* The Patterns tab: the name table at nx, ny and the sheet at sx, sy of one picture w x h. */
    struct PatParts { int nx, ny, nw, nh, sx, sy, sw, sh, w, h; };
    ViewPicture          patPic;
    ui::CanvasState      patCv;
    V9990Layer           patL = {};
    PatParts             pat = {};
    std::vector<uint8_t> patLayerDots, patSheetDots;
    unsigned             patDotsVer = ~0u;
    char                 patDotsKey[32] = "";
    int                  patLayer = 0;        /* P1: 0 layer A, 1 layer B */
    int                  lastFamily = -1;     /* 1 in a bitmap mode, 0 in the others */
    int                  patShown = 0;        /* the layer drawn: patLayer, 0 in P2 */
    std::vector<int>     patCount;            /* cells showing each pattern number */
    int                  patHighest = 0, patRows = 8;
    int                  patCol = -1, patRow = -1;   /* the cell the focus came from, or -1 */
    int                  patUses = 0;         /* cells showing the focused pattern */
    bool                 patNumbers = false, patSide = true;
    bool                 transparent0 = false;

    /* The Sprites tab. Hovers are noted while drawing and shown from the next frame. */
    V9990SpriteFrame     spr = {};
    unsigned             sprVer = ~0u, sprBitsVer = ~0u;
    int                  sprMode = -1;
    uint16_t             rowBits[V9_SPRITES][16] = {};
    ViewPicture          sprPlacePic, sprAtlasPic, sprPatPic;
    ViewPicture          curPic;                 /* the Cursors tab's screen */
    std::vector<UInt32>  curLines, curLinesDrawn;  /* bitmapLines for it, now and as drawn */
    ui::CanvasState      sprCv;
    bool                 sprDropped = true, sprBoxes = false;
    int                  sprHot = -1, sprHotNext = -1;        /* the sprite under the mouse */
    int                  sprPatHot = -1, sprPatHotNext = -1;  /* the pattern under the mouse */
    int                  sprLine = -1, sprLineNext = -1;      /* the placement's line under the mouse */
    int                  sprLineFixed = -1;                    /* a line clicked on the placement */
    SteadyTip            sprTip, sprNotesTip;
    char                 lineText[256] = "";

    static void pin(Focus& f, const Focus& hit, bool clicked)
    {
        if (clicked) {
            bool same = f.pinned && f.kind == hit.kind && f.a == hit.a && f.b == hit.b;
            f = hit;
            f.pinned = !same;
            if (same) f.kind = FOCUS_NONE;
        }
        else if (!f.pinned) {
            f = hit;
            f.pinned = false;
        }
    }

    void renderRow(const V9990DebugState* s, uint32_t* dst, int count, UInt32 x, UInt32 y, bool hiRes)
    {
        if ((int)line.size() < count) line.resize(count);
        v9990RenderBitmapLine(line.data(), count, s->vram, s->palette, (UInt8)s->modeR6, s->regs[13], x, y, hiRes);
        for (int i = 0; i < count; i++) dst[i] = v9990Rgba(line[i], ysClear);
    }

    void scrollOf(const V9990DebugState* s, UInt32* sx, UInt32* sy, UInt32* mask) const
    {
        *sx = s->regs[19] | ((UInt32)s->regs[20] << 3);
        /* B3 at 16 bpp scrolls in 2-dot units, as the renderer does. */
        if (s->mode == V9990_MODE_B3 && (s->modeR6 & 3) == 3) *sx &= ~1u;
        *sy = s->regs[17] | ((UInt32)s->scrollAYHi << 8);
        int roll = s->scrollAYHi >> 6;
        *mask = roll == 0 ? 0x1fff : roll == 2 ? 0x1ff : 0xff;
    }

    UInt32 imageY(UInt32 sy, UInt32 mask, int row) const
    {
        return (sy & ~mask & 0x1fff) + ((row + sy) & mask);
    }

    /* Image rows [y0, y1) shown from column x. */
    struct ShownRun { int y0, y1, x; };
    std::vector<ShownRun> bitmapRuns;    /* the Bitmap tab's shown area, for its overlay */
    std::vector<ShownRun> patRuns;       /* the Patterns tab's screen frame on the name table */
    bool                  patRunsTop = true;   /* its first run starts at the screen's top line */

    /* The runs of image rows the screen shows: those the V9990 recorded drawing
    ** the rows of shot o from, else one screen from the scroll registers. */
    void shownRuns(const V9990DebugState* s, const V9990FrameShot* o, std::vector<ShownRun>& out) const
    {
        int iw = imageWidth(s), rows = imageRows(s), last = -2;
        out.clear();
        /* Rows recorded in another mode are in another image geometry. */
        if (o != NULL && o->frameMode != s->mode) o = NULL;
        /* Woven fields draw every other image row. */
        int step = o != NULL && o->interlace ? 2 : 1;
        auto add = [&](int r, int y, int x) {
            int y1 = y + step < rows ? y + step : rows;
            if (last == r - 1 && x == out.back().x && y == out.back().y1) out.back().y1 = y1;
            else out.push_back(ShownRun{ y, y1, x });
            last = r;
        };
        for (int r = 0; o != NULL && r < o->rows; r++) {
            if (o->rowY[r] != 0xffff) add(r, o->rowY[r] % rows, o->rowX[r] % iw);
        }
        if (out.empty()) {
            UInt32 sx, sy, mask;
            scrollOf(s, &sx, &sy, &mask);
            step = 1;
            for (int r = 0; r < screenLines(s); r++) add(r, (int)(imageY(sy, mask, r) % (UInt32)rows), (int)(sx % (UInt32)iw));
        }
    }

    /* The runs of layer l's image lines the screen shows, as shownRuns does for the bitmap.
    ** True when the first run starts at the screen's top line. */
    bool layerRuns(const V9990DebugState* s, const V9990FrameShot* o, const V9990Layer& l, int layer, std::vector<ShownRun>& out) const
    {
        int last = -2, top = -1;
        out.clear();
        if (o != NULL && o->frameMode != s->mode) o = NULL;
        auto add = [&](int r, int y, int x) {
            if (last == r - 1 && x == out.back().x && y == out.back().y1) out.back().y1 = y + 1;
            else out.push_back(ShownRun{ y, y + 1, x });
            last = r;
        };
        for (int r = 0; o != NULL && r < o->rows; r++) {
            if (o->layerY[layer][r] == 0xffff) continue;
            if (top < 0) top = r;
            add(r, o->layerY[layer][r] % l.h, o->layerX[layer][r] % l.w);
        }
        if (!out.empty()) return top == o->displayStartLine - o->windowTopLine;
        int base = l.sy & ~l.rollMask & 0x1ff;
        for (int r = 0; r < 212; r++) add(r, base + ((l.sy + r) & l.rollMask), l.sx);
        return true;
    }

    /* What the screen shows of image rows [r0, r1): each run's rows c0..c1, in two runs of
    ** columns d0..d1 where it wraps past the image's width iw. */
    template <class Area>
    static void forShownAreas(const std::vector<ShownRun>& runs, int r0, int r1, int iw, int sw, Area area)
    {
        for (const ShownRun& run : runs) {
            int c0 = run.y0 > r0 ? run.y0 : r0, c1 = run.y1 < r1 ? run.y1 : r1;
            if (c0 >= c1) continue;
            for (int m = 0; m < 2; m++) {
                int d0 = m == 0 ? run.x : 0, d1 = m == 0 ? run.x + sw : run.x + sw - iw;
                if (d1 > iw) d1 = iw;
                if (d0 >= d1) continue;
                area(c0, c1, d0, d1);
            }
        }
    }

    /* ---- Output: the picture the V9990 drew ---- */

    /* The widest row's shift: narrower rows are widened to it. */
    static int outMaxShift(const V9990FrameShot* o)
    {
        int m = 0;
        for (int y = 0; y < o->rows; y++) m = o->shift[y] > m ? o->shift[y] : m;
        return m;
    }

    /* What Output does with the sprites, or in the bitmap modes the cursors: shown, hidden or alone. */
    int sprLayer(bool cursors) const
    {
        bool only = cursors ? curOnly : sprOnly, shown = cursors ? curShown : sprShown;
        return only ? SL_ONLY : shown ? SL_SHOWN : SL_HIDDEN;
    }

    /* Sample i of row y as layer shows it; false where it shows nothing (sprites only). */
    static bool outSample(const V9990FrameShot* o, int y, int i, int layer, UInt16* c)
    {
        bool split = o->sprSplit && o->splitRow[y];
        if (layer == SL_ONLY && (!split || o->hit[y][i] == 0xff)) return false;
        *c = layer == SL_HIDDEN && split ? o->bg[y][i] : o->pix[y][i];
        return true;
    }

    /* Rows the split did not record show as drawn when hidden and empty when alone, marked in outRowNone. */
    void decodeOutput(const V9990FrameShot* o, int layer)
    {
        int maxShift = outMaxShift(o), w = 0;
        for (int y = 0; y < o->rows; y++) {
            int rw = o->width[y] << (maxShift - o->shift[y]);
            w = rw > w ? rw : w;
        }
        w = w < V9990_SHOT_WIDTH ? w : V9990_SHOT_WIDTH;
        outPic.w = w;
        outPic.h = o->rows;
        outPic.img.assign((size_t)w * o->rows, 0);
        outRowNone.assign(o->rows, 0);
        for (int y = 0; y < o->rows; y++) {
            int k = maxShift - o->shift[y], n = o->width[y] < V9990_SHOT_WIDTH ? o->width[y] : V9990_SHOT_WIDTH;
            uint32_t* dst = &outPic.img[(size_t)y * w];
            outRowNone[y] = layer != SL_SHOWN && !(o->sprSplit && o->splitRow[y]);
            for (int x = 0; x < w; x++) {
                int i = x >> k;
                UInt16 c;
                if (i < n && outSample(o, y, i, layer, &c)) dst[x] = v9990Rgba(c, ysClear);
            }
        }
    }

    /* The image x on Output where display dot d starts in row y, or where it would. */
    static float outDotX(const V9990FrameShot* o, int y, int d, int maxShift)
    {
        float x = ceilf((float)(d * o->dotUc[y] - o->dotOrg[y]) / o->sampleUc[y]);
        return x * (float)(1 << (maxShift - o->shift[y]));
    }

    /* Row y if it has display dots, else the first that has, or -1. */
    static int outDotRow(const V9990FrameShot* o, int y)
    {
        if (y >= 0 && y < o->rows && o->dotUc[y] != 0) return y;
        for (int r = 0; r < o->rows; r++) {
            if (o->dotUc[r] != 0) return r;
        }
        return -1;
    }

    /* A screen rectangle on Output for display dots [d0, d1) of rows [r0, r1), false without display dots. */
    static bool outRect(const V9990FrameShot* o, const ui::CanvasXf& xf, int d0, int d1, int r0, int r1, ImVec2* a, ImVec2* b)
    {
        int g = outDotRow(o, r0), m = outMaxShift(o);
        if (g < 0) return false;
        *a = xf.toScreen(outDotX(o, g, d0, m), (float)r0);
        *b = xf.toScreen(outDotX(o, g, d1, m), (float)r1);
        return true;
    }

    static void outputOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V9990View* self = (V9990View*)ref;
        const V9990FrameShot* o = self->outShot;
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1)), w = (float)self->outPic.w;
        ImVec2 top = xf.toScreen(0, 0), end = xf.toScreen(w, (float)self->outPic.h);
        dl->PushClipRect(xf.clipMin, xf.clipMax, true);
        if (self->outDim) dl->AddRectFilled(top, end, ui::WithAlpha(t.layer, 150));
        if (o != NULL) {
            for (int y = 0, n = (int)self->outRowNone.size(); y < n && y < self->outPic.h;) {
                if (!self->outRowNone[y]) { y++; continue; }
                int y1 = y;
                while (y1 < n && self->outRowNone[y1]) y1++;
                hatch(dl, xf.toScreen(0, (float)y), xf.toScreen(w, (float)y1), ui::WithAlpha(t.caution, 110));
                y = y1;
            }
            if (self->outSprites) self->outputSpriteBoxes(dl, xf, o);
            if (self->outCursors && self->curBoxes) self->outputCursorBoxes(dl, xf, o);
            /* Paused within a frame: from the row the V9990 has drawn to on, the frame before. */
            if (!o->ended && o->drawnRow < self->outPic.h) {
                ImVec2 a = xf.toScreen(0, (float)o->drawnRow);
                hatch(dl, a, end, ui::WithAlpha(t.text, o->restMissing ? 90 : 50));
                dl->AddLine(a, ImVec2(end.x, a.y), t.caution, th * 2);
            }
        }
        dl->PopClipRect();
    }

    /* The sprite boxes, as on the Sprites tab, and the pinned and hovered sprites' boxes. */
    void outputSpriteBoxes(ImDrawList* dl, const ui::CanvasXf& xf, const V9990FrameShot* o) const
    {
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1));
        int row0 = o->displayStartLine - o->windowTopLine;
        if (outDotRow(o, row0) < 0) return;
        auto rect = [&](const V9990Sprite& sp, ImVec2* a, ImVec2* b) {
            outRect(o, xf, sp.x, sp.x + 16, row0 + sp.line0, row0 + sp.line1 + 1, a, b);
        };
        if (sprBoxes) spriteBoxes(outSpr, dl, rect);
        int lit[2] = { outPin, outHot };
        for (int i = 0; i < 2; i++) {
            const V9990Sprite* sp = lit[i] >= 0 && lit[i] < V9_SPRITES ? &outSpr.spr[lit[i]] : NULL;
            if (sp == NULL || sp->line0 < 0) continue;
            ImVec2 a, b;
            rect(*sp, &a, &b);
            float e = th * (i == 0 ? 2 : 1);
            dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), i == 0 ? t.accent : ui::WithAlpha(t.accent, 200), 0, e);
        }
    }

    /* The two cursors' boxes, as on the Cursors tab: solid when shown, dashed when not. */
    void outputCursorBoxes(ImDrawList* dl, const ui::CanvasXf& xf, const V9990FrameShot* o) const
    {
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1));
        int row0 = o->displayStartLine - o->windowTopLine;
        /* Woven fields hold every other line of the cursors. */
        int half = o->interlace != 0 ? 1 : 0;
        for (int c = 1; c >= 0; c--) {
            Cursor k = cursorOf(outState, c, o->sprAttrValid == 2 ? o->sprAttr : NULL);
            int top = (k.top - (half ? o->secondField : 0)) >> half;
            for (int wx = 0; wx < 2; wx++) {
                for (int wy = 0; wy < 2; wy++) {
                    int r0 = row0 + top - wy * (512 >> half), x0 = k.x - wx * 1024;
                    ImVec2 a, b;
                    if (r0 + (32 >> half) <= 0 || r0 >= o->rows) continue;
                    if (!outRect(o, xf, x0, x0 + 32, r0, r0 + (32 >> half), &a, &b)) return;
                    ImU32 col = c == 0 ? t.accent : t.text;
                    if (k.shown) dl->AddRect(a, b, col, 0, th * 2);
                    else dashedRect(dl, a, b, col, th);
                    char num[4];
                    snprintf(num, sizeof(num), "%d", c);
                    partChip(dl, ImVec2(a.x + th, a.y + th), ImVec2(a.x + ui::Px(40), a.y + ui::Px(40)), num);
                }
            }
        }
    }

    /* [Show sprites] [Show sprites only] [Sprite boxes], or the same for the cursors. Show sprites
    ** only shows them, so Show sprites is on and unavailable while it is. */
    void spriteToggles(bool cursors)
    {
        const ui::PixelArt& art = cursors ? ui::UI_ART_CURSOR : ui::UI_ART_SHIP;
        bool& shownSet = cursors ? curShown : sprShown;
        bool& only = cursors ? curOnly : sprOnly;
        bool shown = shownSet || only;
        ImGui::BeginDisabled(only);
        barArtToggle("##sprshow", ui::DECOR_NONE,
                     ts(cursors ? (only ? TS_V9_CUR_SHOW_ONLY_TIP : TS_V9_CUR_SHOW_TIP) : (only ? TS_SPR_SHOW_ONLY_TIP : TS_SPR_SHOW_TIP)),
                     &shown, art);
        ImGui::EndDisabled();
        if (!only) shownSet = shown;
        barArtToggle("##spronly", ui::DECOR_CHECKER, ts(cursors ? TS_V9_CUR_LAYER_ONLY : TS_SPR_LAYER_ONLY), &only, art);
        barArtToggle("##sprboxes", ui::DECOR_FRAME, ts(cursors ? TS_V9_CUR_BOXES_TIP : TS_V9_SPR_BOXES_TIP), cursors ? &curBoxes : &sprBoxes, art);
    }

    void drawOutput(const V9990DebugState* s, Focus& f)
    {
        unsigned ver;
        const V9990FrameShot* o = (const V9990FrameShot*)shell::frameShot(&ver);
        if (o != NULL && !o->valid) o = NULL;
        shell::wantFrame();
        /* Split while shown, so a pause, the toggles and the hover find the frame recorded. */
        bool pattern = s->mode == V9990_MODE_P1 || s->mode == V9990_MODE_P2, cursors = isBitmap(s->mode);
        int layer = pattern || cursors ? sprLayer(cursors) : SL_SHOWN;
        if (pattern || cursors) shell::wantSpriteSplit();
        if (pattern) ensureSprites(s);
        outHot = outHotNext;
        outHotNext = -1;
        if (o != NULL) {
            /* A sample is a dot of 8 >> shift UC; 240 rows are a TV field, more are 31 kHz lines. */
            int maxShift = outMaxShift(o);
            /* Rows of half a TV line: two to a line, each sample twice as wide as tall. */
            bool half = o->rows > 240;
            outCv.aspectX = half && maxShift == 0 ? 2 : 1;
            outCv.aspectY = half && maxShift > 0 ? 1 << (maxShift - 1) : 1 << maxShift;
            outCv.lineRows = half ? 2 : 1;
        }
        ui::BeginCommandBar("bar");
        if (pattern || cursors) spriteToggles(cursors);
        ui::GridToggle(outCv);
        zoomGroup(outCv, true);
        ui::EndCommandBar();
        outCv.checker = outCv.checker || layer == SL_ONLY;

        /* Without a new picture the last one stays, dimmed. */
        if (o != NULL) {
            char key[32];
            snprintf(key, sizeof(key), "o%d/%d", ysClear, layer);
            bool decoded = outPic.needsDecodeAt(ver, key);
            if (decoded) {
                decodeOutput(o, layer);
                outLayer = layer;
            }
            outPic.upload(decoded);
        }
        outShot = o;
        outDim = o == NULL || o->stale;
        outState = s;
        /* Sprites read at capture time fit the picture only in the mode it was drawn in. */
        outSprites = pattern && o != NULL && o->frameMode == s->mode;
        outCursors = cursors && o != NULL && o->frameMode == s->mode;
        outPin = outSprites ? sprPinned(f) : -1;
        /* Laid out from the table the frame was drawn with, where the split recorded it. */
        if (outSprites && (outSprShot != ver || outSprPicture != shell::pictureVersion())) {
            outSprShot = ver;
            outSprPicture = shell::pictureVersion();
            v9990EvalSprites(s, &outSpr, o->sprAttrValid == 1 ? o->sprAttr : NULL);
        }
        if (!outSprites) outHot = -1;

        ui::CanvasHit hit = ui::Canvas("output", outPic.tex.id(), outPic.w, outPic.h, outCv, ImVec2(0, 0), outputOverlay, this);
        if (hit.clickedEmpty) shell::releaseSelection();
        /* The status line says when the copy is missing, paused or stale. */
        if (o == NULL) {
            ui::StatusText("%s", ts(TS_OUT_NONE));
        }
        else {
            const char* paused = o->ended ? "" : ts(o->restMissing ? TS_OUT_PAUSED_UNKNOWN : TS_OUT_PAUSED);
            if (paused[0] || o->stale) ui::StatusText("%s%s", paused, o->stale ? ts(TS_V9_OUT_STALE) : "");
            /* Without this the picture of sprites only is simply empty. */
            if (layer == SL_ONLY && (s->regs[8] & 0x40)) ui::StatusText("%s", ts(cursors ? TS_V9_CUR_NOTE_SPD : TS_V9_SPR_NOTE_SPD));
        }
        if (hit.valid && o != NULL && hit.y < o->rows) outputHover(s, f, o, hit);
    }

    /* A sprite under the mouse is pointed at as on the Sprites tab; elsewhere, the sample as shown. */
    void outputHover(const V9990DebugState* s, Focus& f, const V9990FrameShot* o, const ui::CanvasHit& hit)
    {
        int y = hit.y, i = hit.x >> (outMaxShift(o) - o->shift[y]), n = -1;
        if (outSprites && outLayer != SL_HIDDEN && o->sprSplit && o->splitRow[y] && i < o->width[y] && o->hit[y][i] < V9_SPRITES) n = o->hit[y][i];
        if (n >= 0) {
            sprPoint(s, f, n, hit.clicked, &outSpr);
            outHotNext = n;
            return;
        }
        UInt16 c;
        if (i >= o->width[y] || !outSample(o, y, i, outLayer, &c)) return;
        uint32_t rgb = v9990Rgba(c, false);
        ui::StatusText(ts(TS_V9_OUT_HOVER), y, o->windowTopLine + y, i, (int)o->width[y], rgb & 0xff, (rgb >> 8) & 0xff,
                       (rgb >> 16) & 0xff, (c & 0x8000) ? ts(TS_V9_OUT_YS) : "");
    }

    /* The zoom at the bar's right end; with ys, YS dots transparent in its menu. */
    void zoomGroup(ui::CanvasState& cv, bool ys)
    {
        ui::CommandBarRight(ui::ZoomControlsWidth());
        ui::ZoomControls("zoom", cv, ys ? &ysClear : NULL, ts(TS_YS_TRANSPARENT));
        cv.checker = ys && ysClear;
    }

    /* ---- Pieces: the image space cut into screen-high pieces ---- */

    static const int STRIP_PIECES = 4;

    /* The first piece shown, so that the one holding the top of the screen is among them. */
    int stripFirstPiece(int pieces, int shownPiece) const
    {
        int first = shownPiece < STRIP_PIECES ? 0 : shownPiece - (STRIP_PIECES - 1);
        return first + STRIP_PIECES > pieces ? (pieces > STRIP_PIECES ? pieces - STRIP_PIECES : 0) : first;
    }

    /* Under the Screen's card: the pieces of a bitmap, the layers of P1 and P2, else a note. */
    void drawPages(const V9990DebugState* s, Focus& f, float paneH)
    {
        bool sizeChanged;
        ImGui::PushID("v9990");
        if (!shell::pagesHeader(paneH, &sizeChanged)) {
            ImGui::PopID();
            return;
        }
        bool bitmap = isBitmap(s->mode), pattern = s->mode == V9990_MODE_P1 || s->mode == V9990_MODE_P2;
        float tw = 0, th = 0, cw = 0, ch = 0, gap = ui::Px(8);
        if (bitmap) stripLayout(s, &tw, &th, &cw, &ch);
        else if (pattern) layerLayout(s, &tw, &th, &cw, &ch);
        /* A new size keeps the piece at the left there. */
        if (sizeChanged && pagesTw > 0 && tw > 0) pagesScroll.to = floorf(pagesScroll.x * (tw + gap) / (pagesTw + gap));
        pagesTw = tw;
        shell::pagesBeginScroll(paneH, cw, ch, pagesScroll);
        char status[512] = "";
        if (bitmap) drawStrip(s, f, tw, th, status, sizeof(status));
        else if (pattern) drawLayers(s, tw, th);
        else note(ts(TS_STRIP_EMPTY));
        shell::pagesEnd(pagesScroll);
        ImGui::PopID();
        if (status[0]) ui::StatusText("%s", status);
    }

    /* A caption's height: a line of the body font, which the strip's text uses. */
    static float stripLabelH()
    {
        ui::PushFont(UI_FONT_BODY);
        float h = ImGui::GetTextLineHeight() + ui::Px(2);
        ui::PopFont();
        return h;
    }

    /* Pieces at the size's step: the screen's width as wide as a V99x8 page, a wider image
    ** wider, up to twice that, and as tall as on a TV; *cw x *ch the row with its "+N". */
    void stripLayout(const V9990DebugState* s, float* tw, float* th, float* cw, float* ch) const
    {
        int pieceH = screenLines(s), pieces = (imageRows(s) + pieceH - 1) / pieceH, ax, ay;
        dotAspect(s, &ax, &ay);
        float unit = 256 * shell::pagesStepScale(shell::pages().size);
        float tvW = (float)imageWidth(s) * ax, tvH = (float)pieceH * ay;
        *tw = floorf(fminf(unit * imageWidth(s) / screenWidth(s->mode), unit * 2));
        *th = floorf(*tw * tvH / tvW);
        std::vector<ShownRun> runs;
        shownRuns(s, outShot, runs);
        int first = stripFirstPiece(pieces, runs[0].y0 / pieceH), n = pieces - first < STRIP_PIECES ? pieces - first : STRIP_PIECES;
        float gap = ui::Px(8), more = ui::Px(40);
        *cw = (first > 0 ? more : 0) + n * (*tw + gap) - gap + (first + n < pieces ? gap + more : 0);
        *ch = ui::Px(4) + *th + stripLabelH();
    }

    /* P1's two layers or P2's one, each whole and as tall as the screen is when its width is
    ** as wide as a V99x8 page. */
    void layerLayout(const V9990DebugState* s, float* tw, float* th, float* cw, float* ch) const
    {
        int ax, ay, layers = s->mode == V9990_MODE_P1 ? 2 : 1;
        V9990Layer l;
        if (!v9990LayerOf(s, 0, &l)) l.w = l.h = 512;
        dotAspect(s, &ax, &ay);
        *th = floorf(256 * shell::pagesStepScale(shell::pages().size) * 212.0f * ay / (screenWidth(s->mode) * ax));
        *tw = floorf(*th * l.w * ax / ((float)l.h * ay));
        *cw = layers * (*tw + ui::Px(8)) - ui::Px(8);
        *ch = ui::Px(4) + *th + stripLabelH();
    }

    void drawStrip(const V9990DebugState* s, Focus& f, float tw, float th, char* status, size_t size)
    {
        const UiTokens& t = uiTokens();
        int iw = imageWidth(s), rows = imageRows(s), pieceH = screenLines(s), bpp = bitsPerDot(s), sw = screenWidth(s->mode);
        int pieces = (rows + pieceH - 1) / pieceH;
        std::vector<ShownRun> runs;
        shownRuns(s, outShot, runs);
        int first = stripFirstPiece(pieces, runs[0].y0 / pieceH), n = pieces - first < STRIP_PIECES ? pieces - first : STRIP_PIECES;
        bool hiRes = isHiRes(s->mode);
        char key[64];
        snprintf(key, sizeof(key), "%d/%d/%d/%d/%d/%d", first, s->mode, s->modeR6, s->regs[13], ysClear, pieceH);
        bool decoded = stripPic.needsDecodeVram(key);
        if (decoded) {
            int h = n * pieceH;
            stripPic.w = iw;
            stripPic.h = h;
            stripPic.img.assign((size_t)iw * h, 0xff000000u);
            for (int y = 0; y < h && first * pieceH + y < rows; y++) {
                renderRow(s, &stripPic.img[(size_t)y * iw], iw, 0, (UInt32)(first * pieceH + y), hiRes);
            }
        }
        stripPic.upload(decoded);

        float gap = ui::Px(8), labelH = stripLabelH(), lw = fmaxf(1.0f, ui::Px(1)), more = pieces > n ? ui::Px(40) : 0;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float y = origin.y + ui::Px(4), x0 = origin.x + (first > 0 ? more : 0);
        status[0] = 0;
        ui::PushFont(UI_FONT_BODY);
        if (first > 0) {
            char text[16];
            snprintf(text, sizeof(text), "+%d", first);
            ImVec2 ts = ImGui::CalcTextSize(text);
            dl->AddText(ImVec2(x0 - gap - ts.x, y + th / 2 - ts.y / 2), t.textSecondary, text);
        }
        for (int i = 0; i < n; i++) {
            int p = first + i, r0 = p * pieceH, r1 = r0 + pieceH < rows ? r0 + pieceH : rows;
            ImVec2 a(x0 + i * (tw + gap), y), b(a.x + tw, a.y + th);
            if (ysClear) {
                dl->AddRectFilled(a, b, t.checkerA);
            }
            if (stripPic.tex.id()) {
                dl->AddImage(ImTextureRef(stripPic.tex.id()), a, b, ImVec2(0, (float)(i * pieceH) / stripPic.h),
                             ImVec2(1, (float)((i + 1) * pieceH) / stripPic.h));
            }
            bool shown = false;
            forShownAreas(runs, r0, r1, iw, sw, [&](int c0, int c1, int d0, int d1) {
                shown = true;
                float y0 = floorf(a.y + th * (c0 - r0) / pieceH), y1 = floorf(a.y + th * (c1 - r0) / pieceH);
                float xa = floorf(a.x + tw * d0 / iw), xb = floorf(a.x + tw * d1 / iw);
                dl->AddRect(ImVec2(xa - lw, y0 - lw), ImVec2(xb + lw, y1 + lw), t.textSecondary, 0, lw);
            });
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##piece", ImVec2(tw, th + labelH));
            ImGui::PopID();
            bool hovered = ImGui::IsItemHovered();
            if (hovered) dl->AddRect(ImVec2(a.x - lw, a.y - lw), ImVec2(b.x + lw, b.y + lw), t.text, 0, lw);
            char label[64];
            textf(label, sizeof(label), ts(TS_V9_STRIP_ROWS), r0, r1 - 1);
            float lx = a.x, cy = floorf(b.y + ui::Px(2) + ImGui::GetTextLineHeight() / 2), dot = ui::Px(3);
            /* Cut at the piece's width so a long caption does not run into the next one. */
            dl->PushClipRect(ImVec2(a.x, b.y), ImVec2(b.x, b.y + labelH), true);
            if (shown) {
                dl->AddCircleFilled(ImVec2(lx + dot, cy), dot, t.textSecondary);
                lx += dot * 2 + ui::Px(4);
            }
            dl->AddText(ImVec2(lx, b.y + ui::Px(2)), t.textSecondary, label);
            dl->PopClipRect();
            if (hovered) {
                UInt32 from = (UInt32)(((UInt64)r0 * iw) * bpp / 8), to = (UInt32)(((UInt64)r1 * iw) * bpp / 8) - 1;
                char extra[256], click[128];
                textf(click, sizeof(click), ts(TS_STRIP_CLICK_VIEW), ts(TS_VIEW_BITMAP));
                textf(extra, sizeof(extra), "%s%s", shown ? ts(TS_STRIP_SHOWN) : "", click);
                textf(status, size, ts(TS_STRIP_RANGE), label, from, to, extra);
                Focus hit = { f.chipId, FOCUS_LOGICAL, from, to - from + 1, FOCUS_VRAM, 0, r0, false };
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    f = hit;
                    f.pinned = true;
                    shell::requestView("bitmap");
                }
                else if (!f.pinned) {
                    f = hit;
                }
            }
        }
        if (first + n < pieces) {
            char text[16];
            snprintf(text, sizeof(text), "+%d", pieces - first - n);
            dl->AddText(ImVec2(x0 + n * (tw + gap), y + th / 2 - ImGui::GetTextLineHeight() / 2), t.textSecondary, text);
        }
        ui::PopFont();
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(1, th + labelH + ui::Px(4)));
    }

    /* P1 has layers A and B, P2 one: each layer's whole image space, framing what the screen shows. */
    void drawLayers(const V9990DebugState* s, float tw, float th)
    {
        const UiTokens& t = uiTokens();
        int layers = s->mode == V9990_MODE_P1 ? 2 : 1;
        float gap = ui::Px(8), lw = fmaxf(1.0f, ui::Px(1));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float labelH = stripLabelH();
        ui::PushFont(UI_FONT_BODY);
        for (int i = 0; i < layers; i++) {
            ImVec2 a(origin.x + i * (tw + gap), origin.y + ui::Px(4)), b(a.x + tw, a.y + th);
            V9990Layer l;
            if (!v9990LayerOf(s, i, &l)) continue;
            /* Layer A's colour 0 lets layer B through, so it is clear; not the Patterns tab's choice. */
            bool clear0 = layers == 2 && i == 0;
            char key[32];
            snprintf(key, sizeof(key), "%d/%d/%d/%d", s->mode, i, s->regs[13], s->regs[15]);
            bool decoded = layerPic[i].needsDecodeVram(key);
            if (decoded) {
                int highest;
                uint32_t lut[2][16];
                v9990DecodeLayer(s, l, layerDots, &highest);
                v9990LayerLut(s, i, clear0, lut);
                layerPic[i].w = l.w;
                layerPic[i].h = l.h;
                layerPic[i].img.resize((size_t)l.w * l.h);
                for (int y = 0; y < l.h; y++) v9990LayerRow(&layerDots[(size_t)y * l.w], &layerPic[i].img[(size_t)y * l.w], l.w, lut);
            }
            layerPic[i].upload(decoded);
            if (clear0) dl->AddRectFilled(a, b, t.checkerA);
            if (layerPic[i].tex.id()) dl->AddImage(ImTextureRef(layerPic[i].tex.id()), a, b);
            std::vector<ShownRun> runs;
            layerRuns(s, outShot, l, i, runs);
            forShownAreas(runs, 0, l.h, l.w, l.screenW, [&](int y0, int y1, int x0, int x1) {
                float xa = floorf(a.x + tw * x0 / l.w), xb = floorf(a.x + tw * x1 / l.w);
                float ya = floorf(a.y + th * y0 / l.h), yb = floorf(a.y + th * y1 / l.h);
                dl->AddRect(ImVec2(xa - lw, ya - lw), ImVec2(xb + lw, yb + lw), t.textSecondary, 0, lw);
            });
            char label[64];
            textf(label, sizeof(label), ts(TS_V9_LAYER), i == 0 ? "A" : "B");
            dl->AddText(ImVec2(a.x, b.y + ui::Px(2)), t.textSecondary, label);
        }
        ui::PopFont();
        ImGui::Dummy(ImVec2(layers * (tw + gap), ui::Px(4) + th + labelH));
    }

    /* The CPU address of physical byte p in the mode. */
    UInt32 toLogical(int mode, UInt32 p)
    {
        if (isBitmap(mode)) return ((p & 0x3ffff) << 1) | (p >> 18);
        if (mode != V9990_MODE_P2) return p;
        if (invMode != mode || invPhys != p) {
            invMode = mode;
            invPhys = p;
            invLogical = p;
            for (UInt32 a = 0; a < V9990_VRAM_SIZE; a++) {
                if (v9990MapAddress(V9990_MODE_P2, a) == p) { invLogical = a; break; }
            }
        }
        return invLogical;
    }

    /* Outlines units start..start+count of rows rowW long as one shape, so rows thinner than
    ** a pixel do not fill it in; one row sits just outside so a dot shows when zoomed out. */
    static void outlineSpan(ImDrawList* dl, const ui::CanvasXf& xf, UInt64 start, UInt64 count, UInt32 rowW)
    {
        float th = fmaxf(1.0f, ui::Px(1));
        UInt64 end = start + (count ? count : 1), r0 = start / rowW, r1 = (end - 1) / rowW;
        float c0 = (float)(start % rowW), c1 = (float)((end - 1) % rowW + 1), w = (float)rowW, y0 = (float)r0, y1 = (float)r1;
        if (r0 == r1) {
            ImVec2 p = xf.toScreen(c0, y0), q = xf.toScreen(c1, y0 + 1);
            dl->AddRect(ImVec2(p.x - th * 2, p.y - th * 2), ImVec2(q.x + th * 2, q.y + th * 2), uiTokens().accent, 0, th * 2);
            return;
        }
        ImVec2 corners[8] = { xf.toScreen(c0, y0), xf.toScreen(w, y0), xf.toScreen(w, y1), xf.toScreen(c1, y1),
                              xf.toScreen(c1, y1 + 1), xf.toScreen(0, y1 + 1), xf.toScreen(0, y0 + 1), xf.toScreen(c0, y0 + 1) };
        ImVec2 pts[8];
        int n = 0;
        for (int i = 0; i < 8; i++) {
            if (n > 0 && corners[i].x == pts[n - 1].x && corners[i].y == pts[n - 1].y) continue;
            pts[n++] = corners[i];
        }
        dl->AddPolyline(pts, n, uiTokens().accent, th * 2, ImDrawFlags_Closed);
    }

    /* The VRAM map's lanes over the bytes; in CPU order a bitmap lane's two halves are one run. */
    void tintLanes(ImDrawList* dl, const ui::CanvasXf& xf) const
    {
        const UiTokens& t = uiTokens();
        int n;
        const VramLane* lanes = shell::mapLanes(&n);
        for (int i = 0; i < n; i++) {
            UInt32 a = lanes[i].addr, len = lanes[i].len;
            bool seen = false;
            if (vramOrder == 0 && isBitmap(ovState->mode)) {
                a = (a & 0x3ffff) << 1;
                len *= 2;
                for (int j = 0; j < i && !seen; j++) seen = ((lanes[j].addr & 0x3ffff) << 1) == a && lanes[j].len * 2 == len;
            }
            if (seen) continue;
            ImU32 col = t.series[lanes[i].series % 6];
            char lab[192];
            if (lanes[i].where) textf(lab, sizeof(lab), "%s \xC2\xB7 %s", lanes[i].name, lanes[i].where);
            else textf(lab, sizeof(lab), "%s", lanes[i].name);
            ImU32 fill = ui::WithAlpha(col, t.dark ? 70 : 90);
            ui::CanvasSpan(dl, xf, a, len, 512, fill, col, lab);
        }
    }

    static void vramOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V9990View* self = (V9990View*)ref;
        const V9990DebugState* s = self->ovState;
        if (s != NULL && self->vramTint) self->tintLanes(dl, xf);
        ui::CanvasAddressRuler(dl, xf, self->vramPic.h, 512);
        const Focus& f = shell::shownFocus();
        if (s == NULL || f.kind == FOCUS_NONE || f.len == 0) return;
        UInt32 phys = self->toPhysical(s, f.space, f.addr);
        UInt32 physLen = f.space == FOCUS_PHYS ? f.len : 1;
        if (self->vramOrder == 1) {
            outlineSpan(dl, xf, phys, physLen, 512);
            return;
        }
        UInt32 logical, logicalLen;
        self->focusLogical(s, f, &logical, &logicalLen);
        outlineSpan(dl, xf, logical, logicalLen, 512);
    }

    /* The focus as CPU addresses: a physical run is every other CPU byte in the bitmap modes. */
    void focusLogical(const V9990DebugState* s, const Focus& f, UInt32* logical, UInt32* len)
    {
        UInt32 phys = toPhysical(s, f.space, f.addr), physLen = f.space == FOCUS_PHYS ? f.len : 1;
        *logical = f.space == FOCUS_LOGICAL ? f.addr : toLogical(s->mode, phys);
        *len = f.space == FOCUS_LOGICAL ? f.len : isBitmap(s->mode) && physLen > 1 ? physLen * 2 : physLen;
    }

    static void bitmapOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V9990View* self = (V9990View*)ref;
        const V9990DebugState* s = self->ovState;
        if (s == NULL) return;
        float lw = fmaxf(1.0f, ui::Px(1));
        forShownAreas(self->bitmapRuns, 0, imageRows(s), imageWidth(s), screenWidth(s->mode), [&](int c0, int c1, int d0, int d1) {
            ImVec2 p = xf.toScreen((float)d0, (float)c0), q = xf.toScreen((float)d1, (float)c1);
            dl->AddRect(ImVec2(floorf(p.x) - lw, floorf(p.y) - lw), ImVec2(floorf(q.x) + lw, floorf(q.y) + lw), uiTokens().textSecondary, 0, lw);
        });
        const Focus& f = shell::shownFocus();
        if (f.kind == FOCUS_NONE || f.len == 0) return;
        UInt32 logical, logicalLen;
        self->focusLogical(s, f, &logical, &logicalLen);
        int bpp = bitsPerDot(s);
        outlineSpan(dl, xf, (UInt64)logical * 8 / bpp, (UInt64)logicalLen * 8 / bpp, (UInt32)imageWidth(s));
    }

    /* The image space of the current mode, its dots read from VRAM. */
    void drawBitmap(const V9990DebugState* s, Focus& f)
    {
        if (!isBitmap(s->mode)) {
            emptyView(ts(TS_V9_BITMAP_NONE));
            return;
        }
        dotAspect(s, &bitmapCv.aspectX, &bitmapCv.aspectY);
        bitmapCv.lineRows = lineRows(s);
        ui::BeginCommandBar("bar");
        ui::GridToggle(bitmapCv);
        /* The image decodes YS too, so its option is here as well as on the Screen. */
        zoomGroup(bitmapCv, true);
        ui::EndCommandBar();
        int iw = imageWidth(s), rows = imageRows(s), bpp = bitsPerDot(s);
        ovState = s;
        /* The shown area as on the Pages; the Screen tab's copy is not kept up to date here. */
        unsigned ver;
        const V9990FrameShot* o = (const V9990FrameShot*)shell::frameShot(&ver);
        shell::wantFrame();
        shownRuns(s, o != NULL && o->valid ? o : NULL, bitmapRuns);
        char key[64];
        snprintf(key, sizeof(key), "%d/%d/%d/%d", s->mode, s->modeR6, s->regs[13], ysClear);
        bool decoded = bitmapPic.needsDecodeVram(key);
        if (decoded) {
            bitmapPic.w = iw;
            bitmapPic.h = rows;
            bitmapPic.img.resize((size_t)iw * rows);
            bool hiRes = isHiRes(s->mode);
            for (int y = 0; y < rows; y++) renderRow(s, &bitmapPic.img[(size_t)y * iw], iw, 0, (UInt32)y, hiRes);
        }
        bitmapPic.upload(decoded);
        ui::CanvasHit hit = ui::Canvas("bitmap", bitmapPic.tex.id(), bitmapPic.w, bitmapPic.h, bitmapCv, ImVec2(0, 0), bitmapOverlay, this);
        if (hit.clickedEmpty) shell::releaseSelection();
        if (hit.valid) {
            UInt32 logical = (UInt32)(((UInt64)hit.y * iw + hit.x) * bpp / 8) & V9990_VRAM_MASK;
            UInt32 phys = v9990MapAddress((V9990Mode)s->mode, logical);
            ui::StatusText(ts(TS_V9_STATUS_IMAGE), hit.x, hit.y, logical, phys, s->vram[phys]);
            Focus fh = { f.chipId, FOCUS_LOGICAL, logical, 1, FOCUS_VRAM, hit.x, hit.y, false };
            pin(f, fh, hit.clicked);
        }
    }

    /* ---- Patterns: a layer's name table and pattern generator ---- */

    /* Colour 0 of layer A lets layer B through where A is in front; else it is the backdrop unless asked to be clear. */
    bool patClear0(const V9990DebugState* s) const
    {
        return transparent0 || (s->mode == V9990_MODE_P1 && patShown == 0);
    }

    /* The layer's dots and its generator's used rows, read again when VRAM or the layer changed. */
    void patDecodeDots(const V9990DebugState* s)
    {
        char key[32];
        snprintf(key, sizeof(key), "%d/%d", s->mode, patShown);
        if (patDotsVer == shell::vramVersion() && strcmp(patDotsKey, key) == 0) return;
        patDotsVer = shell::vramVersion();
        snprintf(patDotsKey, sizeof(patDotsKey), "%s", key);
        v9990DecodeLayer(s, patL, patLayerDots, &patHighest);
        /* The generator up to the row of the highest number used, at least 8 rows. */
        int rows = patHighest / patL.perRow + 1;
        patRows = rows < 8 ? 8 : rows > patL.rows ? patL.rows : rows;
        v9990DecodeSheet(s, patL, patRows, patSheetDots);
        patCount.assign(0x4000, 0);
        for (int row = 0; row < patL.h / 8; row++) {
            for (int col = 0; col < patL.w / 8; col++) patCount[v9990NameAt(s, patL, col, row, NULL)]++;
        }
    }

    /* The parts in one picture: side by side when that fits the well at this zoom (Fit: when it
    ** shows larger), else one under the other; going back side by side needs 24 px to spare. */
    void patCompose(const V9990DebugState* s)
    {
        patDecodeDots(s);
        PatParts P = {};
        P.nw = patL.w;
        P.nh = patL.h;
        P.sw = patL.perRow * 8;
        P.sh = patRows * 8;
        int gap = 8;
        ImVec2 avail = ImGui::GetContentRegionAvail(), k = ui::CanvasScale(patCv, 1);
        float wellW = avail.x - ImGui::GetStyle().ScrollbarSize, wellH = avail.y - ImGui::GetStyle().ScrollbarSize;
        float wS = (float)(P.nw + gap + P.sw), hS = (float)(P.nh > P.sh ? P.nh : P.sh);
        float wT = (float)(P.nw > P.sw ? P.nw : P.sw), hT = (float)(P.nh + gap + P.sh);
        if (patCv.z().fit) {
            patSide = fminf(wellW / (wS * k.x), wellH / (hS * k.y)) >= fminf(wellW / (wT * k.x), wellH / (hT * k.y));
        }
        else {
            float u = (float)patCv.z().zoom;
            patSide = wS * u * k.x <= wellW - (patSide ? 0 : ui::Px(24));
        }
        P.sx = patSide ? P.nw + gap : 0;
        P.sy = patSide ? 0 : P.nh + gap;
        P.w = patSide ? (int)wS : (int)wT;
        P.h = patSide ? (int)hS : (int)hT;
        bool clear0 = patClear0(s);
        char key[64];
        snprintf(key, sizeof(key), "%s/%d/%dx%d/%d/%d/%d", patDotsKey, (int)patSide, P.w, P.h, (int)clear0,
                 s->regs[13], s->regs[15]);
        bool composed = patPic.needsDecodeVram(key);
        if (composed) {
            uint32_t lut[2][16];
            v9990LayerLut(s, patShown, clear0, lut);
            patPic.w = P.w;
            patPic.h = P.h;
            patPic.img.assign((size_t)P.w * P.h, 0);
            for (int y = 0; y < P.nh; y++) {
                v9990LayerRow(&patLayerDots[(size_t)y * patL.w], &patPic.img[(size_t)(P.ny + y) * P.w + P.nx], P.nw, lut);
            }
            for (int y = 0; y < P.sh; y++) {
                v9990LayerRow(&patSheetDots[(size_t)y * P.sw], &patPic.img[(size_t)(P.sy + y) * P.w + P.sx], P.sw, lut);
            }
        }
        patPic.upload(composed);
        pat = P;
        patCv.partCount = 2;
        patCv.parts[0] = { (float)P.nx, (float)P.ny, (float)(P.nx + P.nw), (float)(P.ny + P.nh), 1 };
        patCv.parts[1] = { (float)P.sx, (float)P.sy, (float)(P.sx + P.sw), (float)(P.sy + P.sh), 1 };
    }

    /* The pattern a focus names on this layer's sheet, or -1. */
    int patFocusPattern(const Focus& f) const
    {
        if (f.kind != FOCUS_CHAR || f.space != FOCUS_LOGICAL || f.a < 0 || f.b < 0) return -1;
        int n = (f.b / 8) * patL.perRow + f.a / 8;
        return n < patRows * patL.perRow && f.addr == v9990PatternRowAddr(patL, n, 0) ? n : -1;
    }

    /* How many cells of the name table show pattern n. */
    int patUsers(int n) const
    {
        return n >= 0 && n < (int)patCount.size() ? patCount[n] : 0;
    }

    /* The screen's frame on the name table, with the scroll the screen's top line used on its corner. */
    void patScreenFrame(ImDrawList* dl, const ui::CanvasXf& xf) const
    {
        const UiTokens& t = uiTokens();
        const PatParts& P = pat;
        float th = fmaxf(1.0f, ui::Px(1));
        bool corner = patRunsTop;
        forShownAreas(patRuns, 0, patL.h, patL.w, patL.screenW, [&](int y0, int y1, int x0, int x1) {
            ImVec2 a = xf.toScreen((float)(P.nx + x0), (float)(P.ny + y0));
            ImVec2 b = xf.toScreen((float)(P.nx + x1), (float)(P.ny + y1));
            dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), ui::WithAlpha(t.layer, 200), 0, th * 3);
            dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t.text, 0, th);
            /* At the frame's bottom left, clear of the name table's chip at the top; under a band too short for it. */
            if (corner) {
                char text[96];
                textf(text, sizeof(text), ts(TS_V9_PAT_CHIP_SCREEN), patRuns[0].x, patRuns[0].y0);
                ui::PushFont(UI_FONT_BODY, 11);
                float ch = ImGui::GetTextLineHeight() + ui::Px(2);
                ui::PopFont();
                float clear = xf.toScreen(0, (float)P.ny).y + ch + th;
                float y = b.y - ch - th >= fmaxf(a.y + th, clear) ? b.y - ch - th : fmaxf(b.y + th, clear);
                partChip(dl, ImVec2(a.x + th, y), ImVec2(b.x, y + ch + th), text);
                corner = false;
            }
        });
    }

    /* Each cell's pattern number in hex, on the cells in view wide enough for it. */
    void patNumbersDraw(ImDrawList* dl, const ui::CanvasXf& xf) const
    {
        const UiTokens& t = uiTokens();
        const PatParts& P = pat;
        const V9990DebugState* s = ovState;
        ui::PushFont(UI_FONT_MONO, 11);
        ImVec2 two = ImGui::CalcTextSize("FF");
        float cell = 8 * xf.scale.x;
        auto label = [&](ImVec2 a, int v) {
            char num[8];
            snprintf(num, sizeof(num), v < 0x100 ? "%02X" : "%X", v);
            float w = v < 0x100 ? two.x : ImGui::CalcTextSize(num).x;
            if (cell < fmaxf(ui::Px(18), w + ui::Px(2)) || 8 * xf.scale.y < two.y) return;
            dl->AddRectFilled(a, ImVec2(a.x + w + ui::Px(2), a.y + two.y), ui::WithAlpha(t.layer, 170));
            dl->AddText(ImVec2(a.x + ui::Px(1), a.y), t.text, num);
        };
        if (cell < ui::Px(18)) {
            ui::PopFont();
            return;
        }
        /* Only the cells that reach the view. */
        auto span = [&](int x0, int y0, int cols, int rows, int* c0, int* c1, int* r0, int* r1) {
            *c0 = (int)floorf((xf.clipMin.x - xf.origin.x) / cell - x0 / 8.0f);
            *c1 = (int)ceilf((xf.clipMax.x - xf.origin.x) / cell - x0 / 8.0f);
            *r0 = (int)floorf((xf.clipMin.y - xf.origin.y) / (8 * xf.scale.y) - y0 / 8.0f);
            *r1 = (int)ceilf((xf.clipMax.y - xf.origin.y) / (8 * xf.scale.y) - y0 / 8.0f);
            *c0 = *c0 < 0 ? 0 : *c0;
            *r0 = *r0 < 0 ? 0 : *r0;
            *c1 = *c1 > cols - 1 ? cols - 1 : *c1;
            *r1 = *r1 > rows - 1 ? rows - 1 : *r1;
        };
        int c0, c1, r0, r1;
        span(P.nx, P.ny, P.nw / 8, P.nh / 8, &c0, &c1, &r0, &r1);
        for (int r = r0; r <= r1; r++) {
            for (int c = c0; c <= c1; c++) label(xf.toScreen((float)(P.nx + c * 8), (float)(P.ny + r * 8)), v9990NameAt(s, patL, c, r, NULL));
        }
        span(P.sx, P.sy, patL.perRow, patRows, &c0, &c1, &r0, &r1);
        for (int r = r0; r <= r1; r++) {
            for (int c = c0; c <= c1; c++) label(xf.toScreen((float)(P.sx + c * 8), (float)(P.sy + r * 8)), r * patL.perRow + c);
        }
        ui::PopFont();
    }

    /* The focused pattern on both parts: its sheet cell and the name cells showing it, these
    ** only while they are few; the screen's frame, the numbers and the parts' chips. */
    static void patOverlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref)
    {
        V9990View* self = (V9990View*)ref;
        const UiTokens& t = uiTokens();
        const PatParts& P = self->pat;
        const V9990DebugState* s = self->ovState;
        const V9990Layer& l = self->patL;
        float th = fmaxf(1.0f, ui::Px(1));
        dl->PushClipRect(xf.clipMin, xf.clipMax, true);
        self->patScreenFrame(dl, xf);
        int n = self->patFocusPattern(shell::shownFocus());
        if (n >= 0) {
            ImVec2 a = xf.toScreen((float)(P.sx + (n % l.perRow) * 8), (float)(P.sy + (n / l.perRow) * 8));
            ImVec2 b = xf.toScreen((float)(P.sx + (n % l.perRow) * 8 + 8), (float)(P.sy + (n / l.perRow) * 8 + 8));
            dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t.accent, 0, th * 2);
        }
        if (n >= 0) {
            int cols = l.w / 8, rows = l.h / 8;
            bool many = self->patUses * 4 > cols * rows;
            for (int r = 0; r < rows; r++) {
                ImVec2 ya = xf.toScreen(0, (float)(P.ny + r * 8)), yb = xf.toScreen(0, (float)(P.ny + r * 8 + 8));
                if (yb.y < xf.clipMin.y || ya.y > xf.clipMax.y) continue;
                for (int c = 0; c < cols; c++) {
                    bool hot = c == self->patCol && r == self->patRow;
                    if (many && !hot) continue;
                    ImVec2 a = xf.toScreen((float)(P.nx + c * 8), (float)(P.ny + r * 8)), b =xf.toScreen((float)(P.nx + c * 8 + 8), (float)(P.ny + r * 8 + 8));
                    if (b.x < xf.clipMin.x || a.x > xf.clipMax.x) continue;
                    if (v9990NameAt(s, l, c, r, NULL) != n) continue;
                    float w = hot ? th * 2 : th;
                    dl->AddRect(ImVec2(a.x - w / 2, a.y - w / 2), ImVec2(b.x + w / 2, b.y + w / 2), hot ? t.accent : ui::WithAlpha(t.accent, 160), 0, w);
                }
            }
        }
        if (self->patNumbers) self->patNumbersDraw(dl, xf);
        char chip[64];
        const char* name = s->mode == V9990_MODE_P2 ? "" : self->patShown == 0 ? "A " : "B ";
        textf(chip, sizeof(chip), ts(TS_V9_PAT_CHIP_NAMES), name, l.names);
        partChip(dl, xf.toScreen((float)P.nx, (float)P.ny), xf.toScreen((float)(P.nx + P.nw), (float)(P.ny + P.nh)), chip);
        textf(chip, sizeof(chip), ts(TS_V9_PAT_CHIP_PATTERNS), name, l.patterns);
        partChip(dl, xf.toScreen((float)P.sx, (float)P.sy), xf.toScreen((float)(P.sx + P.sw), (float)(P.sy + P.sh)), chip);
        dl->PopClipRect();
    }

    /* The palette entries a layer's dots take, as "16-31", or two runs in P2. */
    void patPaletteText(const V9990DebugState* s, char* out, size_t size) const
    {
        int a = v9990LayerColour(s, patShown, 0, 0), b = v9990LayerColour(s, patShown, 2, 0);
        if (a == b) textf(out, size, "%d-%d", a, a + 15);
        else textf(out, size, "%d-%d, %d-%d", a, a + 15, b, b + 15);
    }

    void drawPatterns(const V9990DebugState* s, Focus& f)
    {
        /* P2's one layer is shown without forgetting the layer picked for P1. */
        patShown = s->mode == V9990_MODE_P1 ? patLayer : 0;
        if (!v9990LayerOf(s, patShown, &patL)) {
            emptyView(ts(TS_V9_PATTERNS_NONE));
            return;
        }
        patCv.aspectX = patCv.aspectY = 1;
        patCv.lineRows = 1;
        ui::BeginCommandBar("bar");
        if (s->mode == V9990_MODE_P1) {
            char a[32], b[32];
            textf(a, sizeof(a), ts(TS_V9_LAYER), "A");
            textf(b, sizeof(b), ts(TS_V9_LAYER), "B");
            const char* names[2] = { a, b };
            ui::CommandBarItem(ui::SegmentedWidth(names, 2), ui::SegmentedHeight());
            int pick = patLayer;
            if (ui::Segmented("layer", &pick, names, 2, 0, 0, NULL, 0, NULL, ts(TS_V9_LAYER_PICK_TIP)) && pick != patLayer) {
                patLayer = patShown = pick;
                patCol = patRow = -1;
                v9990LayerOf(s, patLayer, &patL);
            }
            ui::CommandBarSeparator();
        }
        barToggle("##patnum", UI_ICON_NUMBERS, ts(TS_PAT_NUMBERS), ts(TS_V9_PAT_NUMBERS_TIP), &patNumbers);
        ui::GridToggle(patCv);
        ui::CommandBarRight(ui::ZoomControlsWidth());
        ui::ZoomControls("zoom", patCv, &transparent0, ts(TS_COLOUR0_TRANSPARENT));
        ui::EndCommandBar();
        ovState = s;
        unsigned ver;
        const V9990FrameShot* o = (const V9990FrameShot*)shell::frameShot(&ver);
        shell::wantFrame();
        patRunsTop = layerRuns(s, o != NULL && o->valid ? o : NULL, patL, patShown, patRuns);
        patCompose(s);
        patCv.checker = patClear0(s);
        int fn = patFocusPattern(shell::shownFocus());
        patUses = fn >= 0 ? patUsers(fn) : 0;
        const PatParts& P = pat;
        ui::CanvasHit h = ui::Canvas("sheet", patPic.tex.id(), patPic.w, patPic.h, patCv, ImVec2(0, 0), patOverlay, this);
        bool onPart = false;
        char status[512] = "";
        int n = -1;
        if (h.valid && h.x >= P.nx && h.x < P.nx + P.nw && h.y >= P.ny && h.y < P.ny + P.nh) {
            int col = (h.x - P.nx) / 8, row = (h.y - P.ny) / 8;
            UInt32 nameAddr;
            n = v9990NameAt(s, patL, col, row, &nameAddr);
            onPart = true;
            char pal[48];
            patPaletteText(s, pal, sizeof(pal));
            textf(status, sizeof(status), ts(TS_V9_PAT_STATUS_CELL), col, row, nameAddr, n, n, v9990PatternRowAddr(patL, n, 0), pal, patUsers(n));
        }
        else if (h.valid && h.x >= P.sx && h.x < P.sx + P.sw && h.y >= P.sy && h.y < P.sy + P.sh) {
            n = ((h.y - P.sy) / 8) * patL.perRow + (h.x - P.sx) / 8;
            onPart = true;
            textf(status, sizeof(status), ts(TS_V9_PAT_STATUS_SHEET), n, n, v9990PatternRowAddr(patL, n, 0), patUsers(n));
        }
        if (n >= 0) {
            size_t at = strlen(status);
            if (n >= patL.rows * patL.perRow && at < sizeof(status)) textf(status + at, sizeof(status) - at, ts(TS_V9_PAT_PAST), patL.rows * patL.perRow - 1);
            /* Past the generator a pattern has no cell on the sheet: it is only named. */
            if (n < patRows * patL.perRow) {
                Focus hit = { f.chipId, FOCUS_LOGICAL, v9990PatternRowAddr(patL, n, 0), 4, FOCUS_CHAR, (n % patL.perRow) * 8, (n / patL.perRow) * 8, false };
                pin(f, hit, h.clicked);
            }
            else if (h.clicked) {
                shell::releaseSelection();
            }
            bool cell = h.x >= P.nx && h.x < P.nx + P.nw && h.y >= P.ny && h.y < P.ny + P.nh;
            if (!f.pinned || h.clicked) {
                patCol = cell ? (h.x - P.nx) / 8 : -1;
                patRow = cell ? (h.y - P.ny) / 8 : -1;
            }
        }
        /* The gap between the parts and the room beside them are as the well. */
        if (h.clickedEmpty || (h.clicked && !onPart)) shell::releaseSelection();
        if (status[0]) ui::StatusText("%s", status);
    }

    /* ---- Sprites: P1 and P2's 125 sprites ---- */

    enum { SP_PLACE, SP_ATTRS, SP_MAG, SP_PATS, SP_COUNT };
    /* The parts at a zoom: where each goes in the child, its size and screen px a dot. */
    struct SprLayout { ImVec2 at[SP_COUNT], size[SP_COUNT]; float scale[SP_COUNT]; float width, height; int patCols; };

    /* The sprites laid out again when VRAM, the registers or the chip changed. */
    void ensureSprites(const V9990DebugState* s)
    {
        if (sprVer == shell::pictureVersion() && sprMode == s->mode) return;
        sprVer = shell::pictureVersion();
        sprMode = s->mode;
        v9990EvalSprites(s, &spr);
    }

    /* Sprite n of the pinned focus, or -1. */
    int sprPinned(const Focus& f) const
    {
        if (!f.pinned || f.kind != FOCUS_SPRITE || f.space != FOCUS_LOGICAL || f.a < 0 || f.a >= V9_SPRITES || !spr.valid) return -1;
        return f.addr == spr.spr[f.a].attrAddr ? f.a : -1;
    }

    /* Hovered or clicked sprite n: the status line, and a click pins its attributes. from, unless
    ** NULL, is the layout the sprite was found in (Output's, from the table the frame drew). */
    void sprPoint(const V9990DebugState* s, Focus& f, int n, bool clicked, const V9990SpriteFrame* from = NULL)
    {
        const V9990Sprite& sp = (from != NULL ? *from : spr).spr[n];
        Focus hit = { f.chipId, FOCUS_LOGICAL, sp.attrAddr, 4, FOCUS_SPRITE, n, sp.line0, false };
        pin(f, hit, clicked);
        sprHotNext = n;
        ui::StatusText(ts(TS_V9_SPR_STATUS), n, sp.y, sp.x, sp.pattern, sp.sc, sp.sc * 16, sp.sc * 16 + 15,
                       sp.pr0 ? "    PR0" : sp.pr1 ? "    PR1" : "", sp.attrAddr);
    }

    /* The tiles of the attribute table, 16 a row, and the sheet of patterns, 16 a row, as pictures. */
    void sprRenderTiles(const V9990DebugState* s)
    {
        if (!sprAtlasPic.needsDecode("t")) {
            sprAtlasPic.upload(false);
            sprPatPic.upload(false);
            return;
        }
        uint32_t pal[64];
        v9990PaletteRgba(s, pal);
        sprAtlasPic.w = 256;
        sprAtlasPic.h = 128;
        sprPatPic.w = 256;
        sprPatPic.h = 256;
        v9990RenderSpriteSheets(s, spr, pal, sprAtlasPic.img, sprPatPic.img);
        sprAtlasPic.upload(true);
        sprPatPic.upload(true);
    }

    /* The sprites alone as drawn, with the dropped ones' dots stippled red when asked. */
    void sprRenderPlace(const V9990DebugState* s)
    {
        const UiTokens& t = uiTokens();
        char key[32];
        snprintf(key, sizeof(key), "p%d/%08X", (int)sprDropped, (unsigned)t.critical);
        if (!sprPlacePic.needsDecode(key)) {
            sprPlacePic.upload(false);
            return;
        }
        uint32_t pal[64];
        v9990PaletteRgba(s, pal);
        sprPlacePic.w = spr.width;
        sprPlacePic.h = V9_SPRITE_LINES;
        uint32_t stipple = (t.critical & 0x00ffffffu) | 0xc8000000u;
        v9990RenderSpritePlane(spr, pal, sprDropped ? stipple : 0, sprPlacePic.img);
        sprPlacePic.upload(true);
    }

    /* A part's title in the strong font, then its subtitle. */
    static void sprTitle(ImDrawList* dl, ImVec2 at, const char* title, const char* sub, float maxX)
    {
        const UiTokens& t = uiTokens();
        ui::PushFont(UI_FONT_STRONG);
        dl->AddText(at, t.text, title);
        float tw = ImGui::CalcTextSize(title).x;
        ui::PopFont();
        if (sub && sub[0]) ui::TextFit(dl, ImVec2(at.x + tw + ui::Px(8), at.y), maxX, sub, t.textSecondary, false);
    }

    /* The band left of the placement: places taken on each line, PR0 ones darker; a tick at 16. */
    static float sprBandW()
    {
        return ui::Px(24) + ui::Px(6);
    }

    /* The sprite drawn at dot x of line d, else a dropped one with a dot there (the stipple, while shown),
    ** else one in range there with no dot, else -1. */
    int sprAt(int d, int x) const
    {
        const V9990SpriteLine& ln = spr.line[d];
        int any = -1, dropped = -1;
        for (int k = 0; k < ln.count; k++) {
            const V9990Sprite& sp = spr.spr[ln.id[k]];
            if (sp.pr0 || x < sp.x || x >= sp.x + 16) continue;
            int r = (d - sp.y - 1) & 0xff, j = x - sp.x;
            bool dot = (rowBits[ln.id[k]][r] & (0x8000 >> j)) != 0;
            if (k >= ln.places) {
                if (dot && sprDropped && dropped < 0) dropped = ln.id[k];
                continue;
            }
            if (any < 0) any = ln.id[k];
            if (sp.row[r] == V9_ROW_SHOWN && dot) return ln.id[k];
        }
        return dropped >= 0 ? dropped : any;
    }

    /* Each sprite's rows as masks of their set dots, for hit tests. */
    void sprRowBits(const V9990DebugState* s)
    {
        uint8_t dots[16];
        for (int n = 0; n < V9_SPRITES; n++) {
            for (int r = 0; r < 16; r++) rowBits[n][r] = v9990SpritePatternRow(s, spr, spr.spr[n].pattern, r, dots);
        }
    }

    void sprDrawPlace(const V9990DebugState* s, Focus& f, ImVec2 at, float p, float partW)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        sprTitle(dl, at, ts(TS_SPR_TAB_PLACE), ts(TS_V9_SPR_PLACE_SUB), at.x + partW);
        float y0 = floorf(at.y + lh + ui::Px(6)), bandW = ui::Px(24);
        /* P2's 512 dots fill the width of P1's 256, the lines as high: dots half as wide as tall. */
        float q = p * spr.width / 256;
        ImVec2 img(at.x + sprBandW(), y0), end(img.x + spr.width * p, y0 + V9_SPRITE_LINES * q);
        sprRenderPlace(s);
        drawCheckerRect(dl, img, end);
        if (sprPlacePic.tex.id()) ui::ImageNearest(dl, sprPlacePic.tex.id(), img, end);
        canvasGrid(sprCv, dl, img, end, p, q);
        for (int d = 0; d < V9_SPRITE_LINES; d++) {
            const V9990SpriteLine& ln = spr.line[d];
            float ya = y0 + d * q, yb = fmaxf(y0 + (d + 1) * q, ya + 1);
            float lenAll = ln.places * bandW / V9_SPRITE_PLACES, lenPr0 = ln.pr0 * bandW / V9_SPRITE_PLACES;
            if (ln.places > 0) dl->AddRectFilled(ImVec2(at.x, ya), ImVec2(at.x + lenAll, yb), t.textSecondary);
            if (ln.pr0 > 0) dl->AddRectFilled(ImVec2(at.x + lenAll - lenPr0, ya), ImVec2(at.x + lenAll, yb), t.textTertiary);
            bool over = false;
            for (int k = ln.places; k < ln.count; k++) over = over || !spr.spr[ln.id[k]].pr0;
            if (over) dl->AddRectFilled(ImVec2(at.x + bandW + ui::Px(1), ya), ImVec2(at.x + bandW + ui::Px(4), yb), t.critical);
        }
        dl->AddLine(ImVec2(floorf(at.x + bandW), y0), ImVec2(floorf(at.x + bandW), end.y), t.textTertiary, th);

        ImGui::SetCursorScreenPos(ImVec2(at.x, y0));
        ImGui::InvisibleButton("##place", ImVec2(end.x - at.x, V9_SPRITE_LINES * q));
        bool hovered = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        ImVec2 m = ImGui::GetIO().MousePos;
        int d = (int)floorf((m.y - y0) / q), dot = (int)floorf((m.x - img.x) / p);
        if (hovered && d >= 0 && d < V9_SPRITE_LINES) {
            sprLineNext = d;
            int hit = m.x >= img.x && dot >= 0 && dot < spr.width ? sprAt(d, dot) : -1;
            if (hit >= 0) sprPoint(s, f, hit, clicked);
            else ui::StatusText(ts(TS_GUTTER_LINE), d, sprLineText(d));
            if (clicked && hit < 0) {
                shell::releaseSelection();
                sprLineFixed = sprLineFixed == d ? -1 : d;
            }
        }
        if (sprLineFixed >= 0) {
            float fy = floorf(y0 + (sprLineFixed + 0.5f) * q);
            dl->AddLine(ImVec2(at.x, fy), ImVec2(img.x, fy), t.accent, th * 2);
        }
        dl->PushClipRect(img, end, true);
        if (sprBoxes) sprDrawBoxes(dl, img, p, q);
        if (sprLine >= 0) dl->AddLine(ImVec2(img.x, y0 + (sprLine + 0.5f) * q), ImVec2(end.x, y0 + (sprLine + 0.5f) * q), ui::WithAlpha(t.accent, 160), th);
        int lit[2] = { sprPinned(f), sprHot };
        for (int i = 0; i < 2; i++) {
            const V9990Sprite* sp = lit[i] >= 0 ? &spr.spr[lit[i]] : NULL;
            if (sp == NULL || sp->line0 < 0) continue;
            float e = th * (i == 0 ? 2 : 1);
            ImVec2 a(img.x + sp->x * p, y0 + sp->line0 * q), b(img.x + (sp->x + 16) * p, y0 + (sp->line1 + 1) * q);
            dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), i == 0 ? t.accent : ui::WithAlpha(t.accent, 200), 0, e);
        }
        sprPatBoxes(dl, img, p, q);
        dl->PopClipRect();
    }

    void sprDrawBoxes(ImDrawList* dl, ImVec2 img, float p, float q) const
    {
        spriteBoxes(spr, dl, [&](const V9990Sprite& sp, ImVec2* a, ImVec2* b) {
            *a = ImVec2(img.x + sp.x * p, img.y + sp.line0 * q);
            *b = ImVec2(img.x + (sp.x + 16) * p, img.y + (sp.line1 + 1) * q);
        });
    }

    /* Every sprite of fr on the screen boxed where rect puts it: solid when it takes a place and
    ** draws, dashed for PR0; where several share a corner, one box and their count. */
    template <class Rect>
    void spriteBoxes(const V9990SpriteFrame& fr, ImDrawList* dl, Rect rect) const
    {
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1));
        bool done[V9_SPRITES] = {};
        for (int n = 0; n < V9_SPRITES; n++) {
            const V9990Sprite& sp = fr.spr[n];
            if (done[n] || sp.line0 < 0 || sp.x >= fr.width) continue;
            int same = 0;
            for (int k = n; k < V9_SPRITES; k++) {
                const V9990Sprite& o = fr.spr[k];
                if (o.x == sp.x && o.y == sp.y && o.pr0 == sp.pr0 && o.line0 >= 0) { done[k] = true; same++; }
            }
            ImVec2 a, b;
            rect(sp, &a, &b);
            ImU32 col = sp.pr0 ? t.textSecondary : sp.state == V9_SPR_DROPPED ? t.critical : t.text;
            if (sp.pr0) dashedRect(dl, a, b, col, th);
            else dl->AddRect(a, b, col, 0, th);
            if (same > 1) {
                char text[16];
                snprintf(text, sizeof(text), "\xC3\x97%d", same);
                partChip(dl, ImVec2(b.x + th, a.y), ImVec2(b.x + ui::Px(80), b.y + ui::Px(20)), text);
            }
        }
    }

    /* The users of the hovered pattern boxed on the placement. */
    void sprPatBoxes(ImDrawList* dl, ImVec2 img, float p, float q) const
    {
        if (sprPatHot < 0) return;
        const UiTokens& t = uiTokens();
        float th = fmaxf(1.0f, ui::Px(1));
        for (int n = 0; n < V9_SPRITES; n++) {
            const V9990Sprite& sp = spr.spr[n];
            if (sp.pattern != sprPatHot || sp.line0 < 0) continue;
            ImVec2 a(img.x + sp.x * p, img.y + sp.line0 * q), b(img.x + (sp.x + 16) * p, img.y + (sp.line1 + 1) * q);
            dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), ui::WithAlpha(t.accent, 200), 0, th);
        }
    }

    /* Line d's sprites in a phrase, for the status line. */
    const char* sprLineText(int d)
    {
        const V9990SpriteLine& ln = spr.line[d];
        int dropped = 0;
        for (int k = ln.places; k < ln.count; k++) dropped += !spr.spr[ln.id[k]].pr0;
        if (ln.count == 0) return ts(TS_LINE_NO_SPRITES);
        int at = textf(lineText, sizeof(lineText), ts(TS_V9_SPR_LINE_STATUS), ln.count, ln.places, ln.pr0);
        if (dropped > 0 && at > 0 && (size_t)at < sizeof(lineText)) textf(lineText + at, sizeof(lineText) - at, ts(TS_V9_SPR_LINE_DROPPED), dropped);
        return lineText;
    }

    /* What a sprite's tile says on hover: its state, PR1's hidden dots and its palette. */
    void sprStateText(int n, char* out, size_t size) const
    {
        const V9990Sprite& sp = spr.spr[n];
        int at = 0;
        switch (sp.state) {
        case V9_SPR_OFF_Y:   at = textf(out, size, "%s", ts(TS_V9_SPR_TIP_OFF_Y)); break;
        case V9_SPR_PR0:
            if (sp.place0 >= 0) at = textf(out, size, ts(TS_V9_SPR_TIP_PR0), sp.place0, sp.place1);
            else at = textf(out, size, "%s", ts(TS_V9_SPR_TIP_PR0_PAST));
            break;
        case V9_SPR_DROPPED: at = textf(out, size, ts(TS_V9_SPR_TIP_DROPPED), sp.dropLine, sp.line0, sp.line1); break;
        case V9_SPR_OFF_X:   at = textf(out, size, ts(TS_V9_SPR_TIP_OFF_X), sp.x, sp.line0, sp.line1); break;
        default:             at = textf(out, size, ts(TS_V9_SPR_TIP_SHOWN), sp.line0, sp.line1); break;
        }
        int front = 0, under = 0;
        for (int r = 0; r < 16; r++) {
            for (int j = 0; j < 16; j++) {
                front += (sp.frontMask[r] >> j) & 1;
                under += (sp.spriteMask[r] >> j) & 1;
            }
        }
        if (sp.pr1 && at > 0 && (size_t)at < size) at += textf(out + at, size - at, ts(TS_V9_SPR_TIP_PR1), front, sp.dots);
        if (under > 0 && at > 0 && (size_t)at < size) at += textf(out + at, size - at, ts(TS_V9_SPR_TIP_UNDER), under);
        if (at > 0 && (size_t)at < size) textf(out + at, size - at, ts(TS_V9_SPR_TIP_PALETTE), sp.sc * 16, sp.sc * 16 + 15, sp.sc);
    }

    /* A tile of sprite n at a..b, its state drawn over it. */
    void sprTile(ImDrawList* dl, int n, ImVec2 a, ImVec2 b, float sc, bool words) const
    {
        const UiTokens& t = uiTokens();
        const V9990Sprite& sp = spr.spr[n];
        float th = fmaxf(1.0f, ui::Px(1));
        drawCheckerRect(dl, a, b);
        if (sprAtlasPic.tex.id()) {
            ImVec2 uv0((float)(n % 16) / 16, (float)(n / 16) / 8), uv1((float)(n % 16 + 1) / 16, (float)(n / 16 + 1) / 8);
            ui::ImageNearest(dl, sprAtlasPic.tex.id(), a, b, uv0, uv1);
        }
        canvasGrid(sprCv, dl, a, b, sc, sc);
        for (int r = 0; r < 16; r++) {
            ImVec2 ra(a.x, a.y + r * sc), rb(b.x, a.y + (r + 1) * sc);
            if (sp.row[r] == V9_ROW_DROPPED) {
                dl->AddRectFilled(ra, rb, ui::WithAlpha(t.critical, 70));
                hatch(dl, ra, rb, ui::WithAlpha(t.critical, 200));
            }
            else if (sp.row[r] == V9_ROW_OFF || sp.row[r] == V9_ROW_PR0 || sp.row[r] == V9_ROW_PR0_PAST) {
                /* A PR0 row that takes a place darker than one past the places or off screen. */
                dl->AddRectFilled(ra, rb, ui::WithAlpha(t.layer, sp.row[r] == V9_ROW_PR0 ? 170 : 120));
            }
        }
        if (sp.pr0) dashedRect(dl, a, b, t.textTertiary, th);
        bool hidden = false;
        for (int r = 0; r < 16 && !hidden; r++) hidden = sp.frontMask[r] != 0;
        if (hidden) dl->AddRectFilled(a, ImVec2(a.x + ui::Px(2), b.y), t.textSecondary);
        ui::PushFont(UI_FONT_BODY, 11);
        float tagH = ImGui::GetTextLineHeight();
        dl->PushClipRect(a, b, true);
        if (words && b.y - a.y >= tagH * 2) {
            if (sp.pr0) dl->AddText(ImVec2(a.x + ui::Px(2), b.y - tagH), t.textSecondary, "PR0");
            else if (sp.pr1) dl->AddText(ImVec2(a.x + ui::Px(3), b.y - tagH), t.textSecondary, "PR1");
            const char* arrow = sp.state == V9_SPR_OFF_Y ? "\xE2\x86\x93" : sp.state == V9_SPR_OFF_X ? "\xE2\x86\x92" : NULL;
            if (arrow) dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(arrow).x - ui::Px(1), a.y), t.textSecondary, arrow);
        }
        else if (sp.state == V9_SPR_OFF_Y || sp.state == V9_SPR_OFF_X) {
            dl->AddRectFilled(ImVec2(b.x - ui::Px(2), a.y), b, t.textTertiary);
        }
        dl->PopClipRect();
        ui::PopFont();
    }

    /* The 125 attributes as tiles, 16 a row; under them the chosen line's sprites. */
    void sprDrawAttrs(const V9990DebugState* s, Focus& f, ImVec2 at, float sc, float partW)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight(), tile = 16 * sc, cellW = tile + ui::Px(4);
        sprTitle(dl, at, ts(TS_SPR_TAB_ATTRS), NULL, at.x + partW);
        int pinN = sprPinned(f);
        float y0 = floorf(at.y + lh + ui::Px(6)), rowH = floorf(lh + tile + ui::Px(6));
        bool words = tile >= ImGui::CalcTextSize("PR0").x + ui::Px(6);
        for (int n = 0; n < V9_SPRITES; n++) {
            float x = at.x + (n % 16) * cellW, y = y0 + (n / 16) * rowH;
            ImVec2 a(x, y + lh), b(x + tile, y + lh + tile);
            bool lit = n == sprHot || n == pinN;
            char num[8];
            snprintf(num, sizeof(num), "%d", n);
            dl->AddText(ImVec2(x, y), lit ? t.accent : t.textSecondary, num);
            sprTile(dl, n, a, b, sc, words);
            int w = n == pinN ? 2 : n == sprHot || (sprPatHot >= 0 && spr.spr[n].pattern == sprPatHot) ? 1 : 0;
            if (w) dl->AddRect(ImVec2(a.x - th * w, a.y - th * w), ImVec2(b.x + th * w, b.y + th * w), t.accent, 0, th * w);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(n);
            ImGui::InvisibleButton("##tile", ImVec2(tile, tile));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                char text[512];
                sprStateText(n, text, sizeof(text));
                steadyTooltip(sprTip, ImGui::GetItemID(), text);
                sprPoint(s, f, n, ImGui::IsItemClicked(ImGuiMouseButton_Left));
            }
        }
        sprDrawLineRow(s, f, ImVec2(at.x, floorf(y0 + 8 * rowH + ui::Px(10))), sc, partW);
    }

    /* The line under the mouse, else the one fixed by a click, else the pinned sprite's first
    ** dropped or first line, else the busiest line over the limit, else the busiest; *why says which. */
    int sprLinePick(const Focus& f, int* why) const
    {
        *why = 0;
        if (sprLine >= 0) { *why = 1; return sprLine; }
        if (sprLineFixed >= 0) { *why = 2; return sprLineFixed; }
        int pinN = sprPinned(f);
        if (pinN >= 0 && spr.spr[pinN].dropLine >= 0) { *why = 3; return spr.spr[pinN].dropLine; }
        if (pinN >= 0 && spr.spr[pinN].line0 >= 0) { *why = 4; return spr.spr[pinN].line0; }
        int best = -1, bestOver = -1;
        for (int d = 0; d < V9_SPRITE_LINES; d++) {
            const V9990SpriteLine& ln = spr.line[d];
            bool over = false;
            for (int k = ln.places; k < ln.count; k++) over = over || !spr.spr[ln.id[k]].pr0;
            if (over && (bestOver < 0 || ln.count > spr.line[bestOver].count)) bestOver = d;
            if (ln.count > 0 && (best < 0 || ln.count > spr.line[best].count)) best = d;
        }
        if (bestOver >= 0) { *why = 5; return bestOver; }
        *why = best >= 0 ? 6 : 0;
        return best;
    }

    /* The chosen line's sprites in range in the order the chip checks them; the fence after the 16 places. */
    void sprDrawLineRow(const V9990DebugState* s, Focus& f, ImVec2 at, float sc, float partW)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        int why, line = sprLinePick(f, &why);
        if (line < 0) return;
        const V9990SpriteLine& ln = spr.line[line];
        static const ToolStringId whyText[] = { TS_SPR_TAB_WHY_HOVER, TS_SPR_TAB_WHY_FIXED, TS_SPR_TAB_WHY_PIN_DROP,
                                                TS_SPR_TAB_WHY_PIN, TS_SPR_TAB_WHY_OVER, TS_SPR_TAB_WHY_BUSIEST };
        char cap[256];
        int at2 = textf(cap, sizeof(cap), ts(TS_SPR_TAB_LINE), line, (int)ln.count);
        if (why > 0 && at2 > 0 && at2 < (int)sizeof(cap)) textf(cap + at2, sizeof(cap) - at2, "%s", ts(whyText[why - 1]));
        bool overCap = ImGui::ItemAdd(ImRect(at, ImVec2(at.x + partW, at.y + lh)), 0) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
        ui::TextFit(dl, at, at.x + partW, cap, t.textSecondary, overCap);
        /* Tiles as large as fit the 16 places and the fence in the part, never below 1 px a dot. */
        float fence = floorf(ui::Px(14)), tl = fmaxf(1.0f, floorf(sc * 0.75f));
        while (tl > 1 && V9_SPRITE_PLACES * (16 * tl + ui::Px(4)) + fence > partW) tl -= 1;
        float tile = 16 * tl, cellW = tile + ui::Px(4);
        bool over = false;
        for (int k = ln.places; k < ln.count; k++) over = over || !spr.spr[ln.id[k]].pr0;
        float y = floorf(at.y + lh + ui::Px(4) + lh);
        dl->PushClipRect(at, ImVec2(at.x + partW, y + tile + lh + ui::Px(2)), true);
        float right = at.x + partW;
        auto tileX = [&](int k) { return at.x + k * cellW + (k >= ln.places ? fence : 0); };
        int shown = ln.count;
        while (shown > 0 && tileX(shown - 1) + tile > right) shown--;
        char more[16] = "";
        if (shown < ln.count) {
            for (;;) {
                snprintf(more, sizeof(more), "+%d", ln.count - shown);
                if (shown == 0 || tileX(shown - 1) + cellW + ImGui::CalcTextSize(more).x <= right) break;
                shown--;
            }
        }
        for (int k = 0; k < shown; k++) {
            int n = ln.id[k], row = (line - spr.spr[n].y - 1) & 0xff;
            bool past = k >= ln.places && !spr.spr[n].pr0;
            float x = tileX(k);
            ImVec2 a(x, y), b(x + tile, y + tile);
            sprTile(dl, n, a, b, tl, false);
            dl->AddLine(ImVec2(a.x, a.y + (row + 0.5f) * tl), ImVec2(b.x, a.y + (row + 0.5f) * tl), t.accent, th);
            if (n == sprHot || n == sprPinned(f)) dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t.accent, 0, th);
            if (past) {
                dl->AddLine(a, b, t.critical, th * 1.5f);
                dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), t.critical, th * 1.5f);
            }
            char num[16];
            snprintf(num, sizeof(num), "#%d", n);
            ui::PushFont(UI_FONT_BODY, 11);
            dl->PushClipRect(ImVec2(x, b.y), ImVec2(x + cellW, b.y + lh + ui::Px(2)), true);
            dl->AddText(ImVec2(x, b.y + ui::Px(1)), past ? t.critical : spr.spr[n].pr0 ? t.textTertiary : t.textSecondary, num);
            dl->PopClipRect();
            ui::PopFont();
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(1000 + k);
            ImGui::InvisibleButton("##ltile", ImVec2(tile, tile));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                sprPoint(s, f, n, ImGui::IsItemClicked(ImGuiMouseButton_Left));
                ui::StatusText(ts(TS_GUTTER_LINE), line, sprLineText(line));
            }
        }
        if (more[0]) {
            float mx = shown > 0 ? tileX(shown - 1) + cellW : at.x;
            dl->AddText(ImVec2(mx, floorf(y + (tile - lh) / 2)), over ? t.critical : t.textSecondary, more);
        }
        if (ln.count > ln.places && ln.places < shown) {
            float fx = at.x + ln.places * cellW + fence / 2 - ui::Px(2);
            dl->AddLine(ImVec2(fx, y - lh), ImVec2(fx, y + tile), t.critical, th * 2);
            char lim[64];
            textf(lim, sizeof(lim), ts(TS_SPR_TAB_FENCE), V9_SPRITE_PLACES);
            dl->AddText(ImVec2(fx + ui::Px(3), y - lh - ui::Px(2)), t.critical, lim);
        }
        dl->PopClipRect();
    }

    /* Why sprite n is or is not drawn, in a sentence or two. */
    void sprWhy(int n, char* out, size_t size) const
    {
        const V9990Sprite& sp = spr.spr[n];
        int at;
        switch (sp.state) {
        case V9_SPR_OFF_Y: at = textf(out, size, "%s", ts(TS_SPR_WHY_OFF_SCREEN)); break;
        case V9_SPR_PR0:
            if (sp.place0 >= 0) at = textf(out, size, ts(TS_V9_SPR_WHY_PR0), sp.place0, sp.place1);
            else at = textf(out, size, "%s", ts(TS_V9_SPR_WHY_PR0_PAST));
            break;
        case V9_SPR_OFF_X: at = textf(out, size, ts(TS_V9_SPR_WHY_OFF_X), sp.x, sp.line0, sp.line1); break;
        case V9_SPR_DROPPED: {
            const V9990SpriteLine& ln = spr.line[sp.dropLine];
            at = textf(out, size, ts(TS_V9_SPR_WHY_DROPPED), sp.dropLine, (int)ln.places, (int)ln.pr0);
            break;
        }
        default: at = textf(out, size, "%s", sp.dots > 0 ? ts(TS_SPR_WHY_DRAWN) : ts(TS_SPR_WHY_EMPTY)); break;
        }
        int front = 0, under = 0;
        for (int r = 0; r < 16; r++) {
            for (int j = 0; j < 16; j++) {
                front += (sp.frontMask[r] >> j) & 1;
                under += (sp.spriteMask[r] >> j) & 1;
            }
        }
        if (sp.pr1 && sp.dots > 0 && at > 0 && (size_t)at < size) at += textf(out + at, size - at, ts(TS_V9_SPR_WHY_FRONT), front, sp.dots);
        if (under > 0 && at > 0 && (size_t)at < size) at += textf(out + at, size - at, ts(TS_V9_SPR_WHY_UNDER), under);
        if (spr.off && at > 0 && (size_t)at < size) textf(out + at, size - at, "%s", ts(TS_V9_SPR_WHY_SPD));
    }

    /* The magnifier's sixteen colours of block sc, a dot under those row bits uses. */
    void sprColourBlock(const V9990DebugState* s, ImVec2 at, int sc, unsigned used) const
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        uint32_t pal[64];
        v9990PaletteRgba(s, pal);
        float sw = ui::Px(12);
        for (int i = 0; i < 16; i++) {
            ImVec2 a(at.x + i * (sw + ui::Px(1)), at.y), b(a.x + sw, a.y + sw);
            if (i == 0) drawCheckerRect(dl, a, b);
            else dl->AddRectFilled(a, b, pal[sc * 16 + i]);
            if ((used >> i) & 1) dl->AddCircleFilled(ImVec2((a.x + b.x) / 2, b.y + ui::Px(4)), ui::Px(2), t.text);
        }
    }

    /* The hovered sprite or pattern, else the pinned sprite: its dots, each row's state and why. */
    void sprDrawMag(const V9990DebugState* s, const Focus& f, ImVec2 at, float m, float partW)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
        int n = sprHot >= 0 ? sprHot : sprPatHot >= 0 ? -1 : sprPinned(f), p = n >= 0 ? spr.spr[n].pattern : sprPatHot;
        if (p < 0) {
            ImGui::SetCursorScreenPos(at);
            ImGui::PushTextWrapPos(at.x + partW);
            note(ts(TS_SPR_TAB_MAG_HINT));
            ImGui::PopTextWrapPos();
            return;
        }
        char head[256];
        if (n < 0) textf(head, sizeof(head), ts(TS_V9_SPR_PAT_MAG), p, p, v9990SpritePatternAddr(spr, p, 0));
        else if (spr.spr[n].line0 >= 0) textf(head, sizeof(head), ts(TS_SPR_TAB_MAG), n, p, spr.spr[n].x, spr.spr[n].line0, spr.spr[n].line1);
        else textf(head, sizeof(head), ts(TS_SPR_TAB_MAG_OFF), n, p, spr.spr[n].x);
        ui::PushFont(UI_FONT_STRONG);
        dl->AddText(at, t.text, head);
        ui::PopFont();
        uint32_t pal[64];
        v9990PaletteRgba(s, pal);
        int sc = n >= 0 ? spr.spr[n].sc : 0;
        for (int k = 0; n < 0 && k < V9_SPRITES; k++) {
            if (spr.spr[k].pattern == p && spr.spr[k].state != V9_SPR_OFF_Y) { sc = spr.spr[k].sc; break; }
        }
        float y0 = floorf(at.y + lh + ui::Px(6)), gx = floorf(at.x + ui::Px(30));
        ImVec2 ga(gx, y0), gb(gx + 16 * m, y0 + 16 * m);
        unsigned used = 0;
        int prev = -2;
        uint8_t dots[16];
        for (int r = 0; r < 16; r++) {
            v9990SpritePatternRow(s, spr, p, r, dots);
            float ry = y0 + r * m;
            for (int j = 0; j < 16; j++) {
                ImVec2 a(gx + j * m, ry), b(a.x + m, ry + m);
                used |= 1u << dots[j];
                if (dots[j]) dl->AddRectFilled(a, b, pal[sc * 16 + dots[j]]);
                else dl->AddRectFilled(a, b, ((j + r) & 1) ? t.checkerA : t.checkerB);
                if (n >= 0 && (((spr.spr[n].frontMask[r] | spr.spr[n].spriteMask[r]) << j) & 0x8000)) hatch(dl, a, b, ui::WithAlpha(t.textSecondary, 220));
            }
            if (n < 0) continue;
            int st = spr.spr[n].row[r], d = spr.spr[n].rowLine[r];
            if (st == V9_ROW_DROPPED) hatch(dl, ImVec2(gx, ry), ImVec2(gb.x, ry + m), ui::WithAlpha(t.critical, 200));
            else if (st == V9_ROW_OFF || st == V9_ROW_PR0 || st == V9_ROW_PR0_PAST) dl->AddRectFilled(ImVec2(gx, ry), ImVec2(gb.x, ry + m), ui::WithAlpha(t.layer, st == V9_ROW_PR0 ? 170 : 120));
            if (st != prev && d >= 0) {
                char num[16];
                snprintf(num, sizeof(num), "%d", d);
                dl->AddText(ImVec2(at.x, ry + (m - lh) / 2), st == V9_ROW_DROPPED ? t.critical : t.textSecondary, num);
            }
            prev = st;
        }
        canvasGrid(sprCv, dl, ga, gb, m, m);
        dl->AddRect(ga, gb, t.controlStrongStroke, 0, th);
        float by = gb.y + ui::Px(8);
        char palText[64];
        textf(palText, sizeof(palText), ts(TS_V9_SPR_PALETTE), sc * 16, sc * 16 + 15, sc);
        dl->AddText(ImVec2(gx, by), t.textSecondary, palText);
        sprColourBlock(s, ImVec2(gx, by + lh + ui::Px(2)), sc, used & ~1u);
        if (n < 0) return;
        char why[512];
        sprWhy(n, why, sizeof(why));
        ImGui::SetCursorScreenPos(ImVec2(at.x, by + lh + ui::Px(24)));
        ImGui::PushTextWrapPos(at.x + partW);
        ImGui::TextUnformatted(why);
        ImGui::PopTextWrapPos();
    }

    /* The 256 patterns at R#25, in the colours of the sprites using them; grey ones unused now. */
    void sprDrawPats(ImVec2 at, float sc, float partW, int cols)
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight(), tile = 16 * sc;
        char sub[256];
        textf(sub, sizeof(sub), ts(TS_V9_SPR_PATS_SUB), 256, spr.patBase);
        sprTitle(dl, at, ts(TS_SPR_TAB_PATS), sub, at.x + partW);
        unsigned scs[256] = {};
        int users[256] = {}, pr0Users[256] = {};
        for (int n = 0; n < V9_SPRITES; n++) {
            const V9990Sprite& sp = spr.spr[n];
            if (sp.state == V9_SPR_OFF_Y) continue;
            users[sp.pattern]++;
            if (sp.pr0) pr0Users[sp.pattern]++;
            else scs[sp.pattern] |= 1u << sp.sc;
        }
        uint32_t pal[64];
        v9990PaletteRgba(ovState, pal);
        float y0 = floorf(at.y + lh * 2 + ui::Px(6)), pitch = sprPatPitch(tile);
        ui::PushFont(UI_FONT_BODY, 11);
        float tagLh = ImGui::GetTextLineHeight();
        ui::PopFont();
        for (int p = 0; p < 256; p++) {
            float x = at.x + (p % cols) * (tile + ui::Px(4)), y = y0 + (p / cols) * pitch;
            ImVec2 a(x, y + lh), b(x + tile, y + lh + tile);
            char num[8];
            snprintf(num, sizeof(num), "%d", p);
            dl->AddText(ImVec2(x, y), p == sprPatHot ? t.accent : t.textSecondary, num);
            drawCheckerRect(dl, a, b);
            if (sprPatPic.tex.id()) {
                ImVec2 uv0((float)(p % 16) / 16, (float)(p / 16) / 16), uv1((float)(p % 16 + 1) / 16, (float)(p / 16 + 1) / 16);
                ui::ImageNearest(dl, sprPatPic.tex.id(), a, b, uv0, uv1);
            }
            canvasGrid(sprCv, dl, a, b, sc, sc);
            if (p == sprPatHot || (sprHot >= 0 && spr.spr[sprHot].pattern == p)) dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t.accent, 0, th);
            float cx = a.x, cy = b.y + ui::Px(2);
            for (int k = 0; k < 4; k++) {
                if (!((scs[p] >> k) & 1)) continue;
                dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + ui::Px(7), cy + ui::Px(5)), pal[k * 16 + 15]);
                dl->AddRect(ImVec2(cx, cy), ImVec2(cx + ui::Px(7), cy + ui::Px(5)), t.controlStroke, 0, th);
                cx += ui::Px(9);
            }
            if (pr0Users[p] > 0 && pr0Users[p] == users[p] && tile >= ui::Px(24)) {
                char tag[16];
                snprintf(tag, sizeof(tag), "PR0\xC3\x97%d", pr0Users[p]);
                ui::PushFont(UI_FONT_BODY, 11);
                dl->PushClipRect(ImVec2(a.x, cy - ui::Px(2)), ImVec2(a.x + tile + ui::Px(4), cy + tagLh), true);
                dl->AddText(ImVec2(cx, cy - ui::Px(3)), t.textTertiary, tag);
                dl->PopClipRect();
                ui::PopFont();
            }
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(2000 + p);
            ImGui::InvisibleButton("##pat", ImVec2(tile, tile));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                sprPatHotNext = p;
                char text[256];
                int k = textf(text, sizeof(text), ts(TS_V9_SPR_PAT_MAG), p, p, v9990SpritePatternAddr(spr, p, 0));
                if (k > 0 && (size_t)k < sizeof(text)) {
                    if (users[p] == 0) textf(text + k, sizeof(text) - k, "    %s", ts(TS_V9_SPR_PAT_UNUSED));
                    else textf(text + k, sizeof(text) - k, ts(TS_V9_SPR_PAT_USERS), users[p], pr0Users[p]);
                }
                ui::StatusText("%s", text);
            }
        }
    }

    /* Pattern cells a row in rowW px: 16, else fewer by halves. */
    static int sprPatCols(float tile, float rowW)
    {
        int cols = 16;
        while (cols > 1 && cols * (tile + ui::Px(4)) - ui::Px(4) > rowW) cols /= 2;
        return cols;
    }

    /* A part's size at scale sc, its title included; the patterns wrap in rowW. */
    ImVec2 sprPartSize(int part, float sc, float rowW) const
    {
        float lh = ImGui::GetTextLineHeight(), title = lh + ui::Px(6);
        if (part == SP_PLACE) return ImVec2(sprBandW() + spr.width * sc, title + V9_SPRITE_LINES * sc * spr.width / 256);
        if (part == SP_ATTRS) {
            float tile = 16 * sc, tl = fmaxf(1.0f, floorf(sc * 0.75f));
            return ImVec2(16 * (tile + ui::Px(4)) - ui::Px(4), title + 8 * floorf(lh + tile + ui::Px(6)) + ui::Px(10) + lh * 3 + ui::Px(4) + 16 * tl);
        }
        if (part == SP_MAG) return ImVec2(ui::Px(30) + 16 * sc + ui::Px(150), title + 16 * sc + ui::Px(8) + lh * 5 + ui::Px(24));
        float tile = 16 * sc;
        int cols = sprPatCols(tile, rowW);
        return ImVec2(cols * (tile + ui::Px(4)) - ui::Px(4), lh * 2 + ui::Px(6) + ((256 + cols - 1) / cols) * sprPatPitch(tile));
    }

    /* The parts at zoom N: placement 256 * N px wide, attributes and patterns at N px a dot, the
    ** magnifier at 8 N; all but the patterns on one row when they fit W, else in fewer a row. */
    SprLayout sprLayoutAt(float W, int N) const
    {
        SprLayout L = {};
        L.scale[SP_PLACE] = (float)N * 256 / spr.width;
        L.scale[SP_ATTRS] = (float)N;
        L.scale[SP_MAG] = fmaxf(6.0f, 8.0f * N);
        L.scale[SP_PATS] = (float)N;
        float gap = ui::Px(12);
        for (int p = 0; p < SP_COUNT; p++) L.size[p] = sprPartSize(p, L.scale[p], W);
        std::vector<std::vector<int>> rows;
        float beside = W - L.size[SP_MAG].x - gap;
        if (L.size[SP_PLACE].x + L.size[SP_ATTRS].x + L.size[SP_MAG].x + gap * 2 <= W) rows = { { SP_PLACE, SP_ATTRS, SP_MAG }, { SP_PATS } };
        else if (L.size[SP_PLACE].x + L.size[SP_ATTRS].x + gap <= W && sprPatCols(16.0f * N, beside) >= 8) rows = { { SP_PLACE, SP_ATTRS }, { SP_MAG, SP_PATS } };
        else if (L.size[SP_PLACE].x + L.size[SP_ATTRS].x + gap <= W) rows = { { SP_PLACE, SP_ATTRS }, { SP_MAG }, { SP_PATS } };
        else rows = { { SP_PLACE }, { SP_ATTRS }, { SP_MAG }, { SP_PATS } };
        float patW = rows.size() == 2 && rows[1].size() == 2 ? beside : W;
        L.size[SP_PATS] = sprPartSize(SP_PATS, L.scale[SP_PATS], patW);
        L.patCols = sprPatCols(16.0f * N, patW);
        float y = 0;
        for (const std::vector<int>& r : rows) {
            float x = 0, h = 0;
            for (int p : r) {
                L.at[p] = ImVec2(x, y);
                x += L.size[p].x + gap;
                h = fmaxf(h, L.size[p].y);
            }
            y += h + gap;
            L.width = fmaxf(L.width, x - gap);
        }
        L.height = y - gap;
        return L;
    }

    /* Mode, size, the limit and the tables, then the notes on one line, the rest on hover. */
    void sprHeader(const V9990DebugState* s)
    {
        const UiTokens& t = uiTokens();
        char head[256];
        textf(head, sizeof(head), ts(TS_V9_SPR_HEAD), modeName(s->mode), V9_SPRITE_PLACES, spr.patBase, spr.attrBase);
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::TextUnformatted(head);
        ImGui::PopStyleColor();
        char notes[3][256];
        int n = 0;
        if (spr.off) textf(notes[n++], sizeof(notes[0]), "%s", ts(TS_V9_SPR_NOTE_SPD));
        if (spr.pr0Full0 >= 0) textf(notes[n++], sizeof(notes[0]), ts(TS_V9_SPR_NOTE_PR0_FULL), spr.pr0Full0, spr.pr0Full1);
        char line[384] = "", all[1024] = "";
        int k = n > 0 ? textf(line, sizeof(line), "%s", notes[0]) : 0;
        if (n > 1 && k > 0) textf(line + k, sizeof(line) - k, ts(TS_SPR_TAB_MORE_NOTES), n - 1);
        size_t at = 0;
        for (int i = 0; i < n && at < sizeof(all); i++) at += (size_t)textf(all + at, sizeof(all) - at, "%s%s", i ? "\n" : "", notes[i]);
        ImGui::PushStyleColor(ImGuiCol_Text, n > 0 ? t.caution : t.textSecondary);
        ImGui::TextUnformatted(line);
        ImGui::PopStyleColor();
        if (n > 1 && ImGui::IsItemHovered()) steadyTooltip(sprNotesTip, ImGui::GetID("##notes"), all);
    }

    void drawSprites(const V9990DebugState* s, Focus& f)
    {
        sprHot = sprHotNext;
        sprHotNext = -1;
        sprPatHot = sprPatHotNext;
        sprPatHotNext = -1;
        sprLine = sprLineNext;
        sprLineNext = -1;
        if (isBitmap(s->mode)) {
            drawCursors(s, f);
            return;
        }
        if (s->mode != V9990_MODE_P1 && s->mode != V9990_MODE_P2) {
            emptyView(ts(TS_V9_SPRITES_NONE));
            return;
        }
        ovState = s;
        ensureSprites(s);
        if (sprBitsVer != sprVer) {
            sprBitsVer = sprVer;
            sprRowBits(s);
        }
        if (sprHot >= V9_SPRITES) sprHot = -1;
        ui::BeginCommandBar("bar");
        barToggle("##sprdrop", UI_ICON_DROPPED, ts(TS_SPR_DROPPED), ts(TS_SPR_DROPPED_TIP), &sprDropped);
        barArtToggle("##sprboxes", ui::DECOR_FRAME, ts(TS_V9_SPR_BOXES_TIP), &sprBoxes);
        ui::GridToggle(sprCv);
        ui::CommandBarRight(ui::ZoomControlsWidth());
        ui::ZoomControls("zoom", sprCv);
        ui::EndCommandBar();
        sprHeader(s);
        sprRenderTiles(s);

        ImVec2 avail = ImGui::GetContentRegionAvail(), childAt = ImGui::GetCursorScreenPos();
        float W = avail.x - ImGui::GetStyle().ScrollbarSize - ui::Px(8);
        ui::CanvasZoom& zs = sprCv.z();
        zs.zoom = zs.zoom < 1 ? 1 : zs.zoom > 32 ? 32 : zs.zoom;
        int z = ui::CanvasWheelZoom(sprCv);
        if (z > 0) {
            zs.zoom = z;
            zs.fit = false;
        }
        int N = zs.zoom;
        if (zs.fit) {
            /* Fit: the largest zoom whose parts fit across, and whose first row fits down. */
            N = 1;
            for (int k = 16; k > 1; k--) {
                SprLayout L = sprLayoutAt(W, k);
                if (L.width <= W && L.at[SP_PATS].y <= avail.y) { N = k; break; }
            }
        }
        SprLayout L = sprLayoutAt(W, N);
        float bar = ImGui::GetStyle().ScrollbarSize;
        bool over = L.height + ui::Px(8) > avail.y, overX = false;
        for (int i = 0; i < 2; i++) {
            overX = L.width > avail.x - (over ? bar : 0);
            over = L.height + ui::Px(8) > avail.y - (overX ? bar : 0);
        }
        ImVec2 content(fmaxf(W, L.width), L.height + ui::Px(8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGuiWindowFlags bars = (over ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_NoScrollbar) |
                                (overX ? ImGuiWindowFlags_AlwaysHorizontalScrollbar : 0);
        ImGui::BeginChild("spr", avail, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | bars);
        ImGui::PopStyleVar();
        (void)childAt;
        ImVec2 o = ImGui::GetCursorScreenPos();
        o.y += ui::Px(4);
        auto partAt = [&](int p) { return ImVec2(floorf(o.x + L.at[p].x), floorf(o.y + L.at[p].y)); };
        sprDrawPlace(s, f, partAt(SP_PLACE), L.scale[SP_PLACE], L.size[SP_PLACE].x);
        sprDrawAttrs(s, f, partAt(SP_ATTRS), L.scale[SP_ATTRS], L.size[SP_ATTRS].x);
        sprDrawMag(s, f, partAt(SP_MAG), L.scale[SP_MAG], L.size[SP_MAG].x);
        sprDrawPats(partAt(SP_PATS), L.scale[SP_PATS], L.size[SP_PATS].x, L.patCols);
        ImGui::SetCursorScreenPos(ImVec2(o.x, o.y - ui::Px(4)));
        ImGui::Dummy(content);
        bool hovered = ImGui::IsWindowHovered();
        ImGui::EndChild();
        ui::CanvasShown(sprCv, (float)N, hovered);
    }

    /* ---- Cursors: the bitmap modes' two 32 x 32 cursors ---- */

    /* Cursor c as the chip reads it: attributes at 7FE00 + 8c, pattern at 7FF00 + 80h c. */
    struct Cursor { int x, y, cc, top; bool eor, pr0, shown; UInt32 bits[32]; };

    /* attr, unless NULL, holds the 16 attribute bytes to read instead of VRAM's. */
    Cursor cursorOf(const V9990DebugState* s, int c, const UInt8* attr = NULL) const
    {
        Cursor k = {};
        UInt32 a = 0x7fe00 + 8 * c;
        auto at = [&](int i) { return attr != NULL ? attr[8 * c + i] : s->vram[V9990_MAP_BX(a + i)]; };
        UInt8 flags = at(6);
        k.y = at(0) | ((at(2) & 1) << 8);
        k.x = at(4) | ((flags & 3) << 8);
        k.cc = flags >> 6;
        k.eor = (flags & 0x20) != 0;
        k.pr0 = (flags & 0x10) != 0;
        k.shown = !k.pr0 && (flags & 0xe0) != 0 && !(s->regs[8] & 0x40);
        /* Its first row is Y + 1 (Y + 2 in interlace) lines down; overscan modes count from 14 (PAL 41) lines lower. */
        int fm = s->frameMode;
        bool overscan = fm == V9990_MODE_B0 || fm == V9990_MODE_B2 || fm == V9990_MODE_B4;
        k.top = (k.y + (s->interlace ? 2 : 1) + (overscan ? (s->isPal ? 41 : 14) : 0)) & 511;
        for (int r = 0; r < 32; r++) {
            UInt32 p = 0x7ff00 + 0x80 * c + 4 * r, v = 0;
            for (int i = 0; i < 4; i++) v = (v << 8) | s->vram[V9990_MAP_BX(p + i)];
            k.bits[r] = v;
        }
        return k;
    }

    /* The image column and row each line of the screen reads, woven lines in IL+EO: those
    ** shot o recorded drawing it from, else the scroll registers'. */
    void bitmapLines(const V9990DebugState* s, const V9990FrameShot* o, std::vector<UInt32>& xy) const
    {
        int lines = screenLines(s);
        bool woven = fieldsWoven(s);
        UInt32 sx, sy, mask;
        scrollOf(s, &sx, &sy, &mask);
        /* Paused mid-frame in IL+EO, the rows past the beam may be the other field's. */
        if (o != NULL && (!o->valid || o->frameMode != s->mode || (woven && !o->ended && o->drawnRow > 0 && o->drawnRow < o->rows))) o = NULL;
        int top = o != NULL ? o->displayStartLine - o->windowTopLine : 0, f = o != NULL && o->secondField ? 1 : 0;
        xy.resize((size_t)2 * lines);
        for (int d = 0; d < lines; d++) {
            int r = top + (woven ? d >> 1 : d);
            UInt32 x = sx, y = imageY(sy, mask, d);
            if (o != NULL && r >= 0 && r < o->rows && o->rowY[r] != 0xffff) {
                x = o->rowX[r];
                y = o->rowY[r];
                /* The other field's line is the next or previous image row within the roll. */
                if (woven && (d & 1) != f) y = (y & ~mask) | ((y + (d & 1) - f) & mask);
            }
            xy[(size_t)2 * d] = x;
            xy[(size_t)2 * d + 1] = y;
        }
    }

    /* The screen as displayed, each line from where it was drawn, without the cursors. */
    void curRenderScreen(const V9990DebugState* s, const V9990FrameShot* o)
    {
        int sw = screenWidth(s->mode), lines = screenLines(s);
        bitmapLines(s, o, curLines);
        char key[48];
        snprintf(key, sizeof(key), "%d/%d/%d/%d/%d", s->mode, s->modeR6, s->regs[13], lines, (int)ysClear);
        bool decoded = curPic.needsDecodeVram(key) || curLines != curLinesDrawn;
        if (decoded) {
            int rows = imageRows(s);
            curLinesDrawn = curLines;
            curPic.w = sw;
            curPic.h = lines;
            curPic.img.assign((size_t)sw * lines, 0xff000000u);
            for (int d = 0; d < lines; d++) {
                renderRow(s, &curPic.img[(size_t)d * sw], sw, curLines[(size_t)2 * d], curLines[(size_t)2 * d + 1] % (UInt32)rows, isHiRes(s->mode));
            }
        }
        curPic.upload(decoded);
    }

    /* Cursor k's 32 x 32 dots at a, m px a dot: its colour, or the picture inverted (EOR) as a half black, half white dot. */
    void curDrawPattern(const V9990DebugState* s, const Cursor& k, ImVec2 a, float m) const
    {
        const UiTokens& t = uiTokens();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int entry = ((s->regs[28] & 0x0f) << 2) | k.cc;
        UInt16 c = v9990PaletteColor(s->palette, entry);
        if (k.eor) c ^= 0x7fff;
        ImU32 col = v9990Rgba(c, false);
        for (int r = 0; r < 32; r++) {
            for (int b = 0; b < 32; b++) {
                ImVec2 p(a.x + b * m, a.y + r * m), q(p.x + m, p.y + m);
                if (!((k.bits[r] << b) & 0x80000000u)) {
                    dl->AddRectFilled(p, q, ((r + b) & 1) ? t.checkerA : t.checkerB);
                }
                else if (k.cc == 0 && k.eor) {
                    dl->AddRectFilled(p, q, IM_COL32(0, 0, 0, 255));
                    dl->AddTriangleFilled(ImVec2(q.x, p.y), q, ImVec2(p.x, q.y), IM_COL32(255, 255, 255, 255));
                }
                else {
                    dl->AddRectFilled(p, q, col);
                }
            }
        }
        canvasGrid(sprCv, dl, a, ImVec2(a.x + 32 * m, a.y + 32 * m), m, m);
        dl->AddRect(a, ImVec2(a.x + 32 * m, a.y + 32 * m), t.controlStrongStroke, 0, fmaxf(1.0f, ui::Px(1)));
    }

    void drawCursors(const V9990DebugState* s, Focus& f)
    {
        const UiTokens& t = uiTokens();
        (void)f;
        ui::BeginCommandBar("bar");
        ui::GridToggle(sprCv);
        ui::CommandBarRight(ui::ZoomControlsWidth());
        ui::ZoomControls("zoom", sprCv);
        ui::EndCommandBar();
        unsigned ver;
        const V9990FrameShot* shot = (const V9990FrameShot*)shell::frameShot(&ver);
        shell::wantFrame();
        char head[256];
        textf(head, sizeof(head), ts(TS_V9_CUR_HEAD), modeName(s->mode), s->regs[28] & 0x0f);
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::TextUnformatted(head);
        ImGui::PopStyleColor();
        /* An empty line without the note, so the canvas stays put when SPD flips. */
        ImGui::PushStyleColor(ImGuiCol_Text, t.caution);
        ImGui::TextUnformatted((s->regs[8] & 0x40) ? ts(TS_V9_CUR_NOTE_SPD) : "");
        ImGui::PopStyleColor();

        Cursor k[2] = { cursorOf(s, 0), cursorOf(s, 1) };
        int sw = screenWidth(s->mode), lines = screenLines(s), ax, ay;
        dotAspect(s, &ax, &ay);
        ui::CanvasZoom& zs = sprCv.z();
        zs.zoom = zs.zoom < 1 ? 1 : zs.zoom > 32 ? 32 : zs.zoom;
        int z = ui::CanvasWheelZoom(sprCv);
        if (z > 0) {
            zs.zoom = z;
            zs.fit = false;
        }
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float lh = ImGui::GetTextLineHeight(), gap = ui::Px(12);
        auto sizeAt = [&](int N, float* px, float* py, float* m, float* w, float* h) {
            *px = (float)N * 256 / sw;
            *py = *px * ay / ax;
            *m = fmaxf(3.0f, 4.0f * N);
            float cardW = 32 * *m + ui::Px(8);
            *w = sw * *px + gap + 2 * cardW + gap;
            *h = lh + ui::Px(6) + fmaxf(lines * *py, 32 * *m + lh * 6);
        };
        int N = zs.zoom;
        float px, py, m, w, h;
        if (zs.fit) {
            N = 1;
            for (int n = 16; n > 1; n--) {
                sizeAt(n, &px, &py, &m, &w, &h);
                if (w <= avail.x - ImGui::GetStyle().ScrollbarSize && h <= avail.y) { N = n; break; }
            }
        }
        sizeAt(N, &px, &py, &m, &w, &h);
        ImGui::BeginChild("cur", avail, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetCursorScreenPos();
        o = ImVec2(floorf(o.x), floorf(o.y + ui::Px(4)));
        float th = fmaxf(1.0f, ui::Px(1));
        sprTitle(dl, o, ts(TS_SPR_TAB_PLACE), ts(TS_V9_CUR_PLACE_SUB), o.x + sw * px);
        ImVec2 img(o.x, floorf(o.y + lh + ui::Px(6))), end(img.x + sw * px, img.y + lines * py);
        curRenderScreen(s, shot);
        if (curPic.tex.id()) ui::ImageNearest(dl, curPic.tex.id(), img, end);
        canvasGrid(sprCv, dl, img, end, px, py);
        dl->PushClipRect(img, end, true);
        /* Cursor 1 first, as cursor 0 is on top; each in up to four pieces where it rolls at 1024 x 512. */
        for (int c = 1; c >= 0; c--) {
            for (int wx = 0; wx < 2; wx++) {
                for (int wy = 0; wy < 2; wy++) {
                    ImVec2 a(img.x + (k[c].x - wx * 1024) * px, img.y + (k[c].top - wy * 512) * py);
                    ImVec2 b(a.x + 32 * px, a.y + 32 * py);
                    if (b.x <= img.x || a.x >= end.x || b.y <= img.y || a.y >= end.y) continue;
                    ImU32 col = c == 0 ? t.accent : t.text;
                    if (k[c].shown) dl->AddRect(a, b, col, 0, th * 2);
                    else dashedRect(dl, a, b, col, th);
                    char num[4];
                    snprintf(num, sizeof(num), "%d", c);
                    partChip(dl, ImVec2(a.x + th, a.y + th), ImVec2(a.x + ui::Px(40), a.y + ui::Px(40)), num);
                }
            }
        }
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(img);
        ImGui::InvisibleButton("##curplace", ImVec2(fmaxf(1.0f, end.x - img.x), fmaxf(1.0f, end.y - img.y)));
        bool placeHovered = ImGui::IsItemHovered();
        if (placeHovered) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            ui::StatusText(ts(TS_V9_CUR_STATUS_DOT), (int)floorf((mp.x - img.x) / px), (int)floorf((mp.y - img.y) / py));
        }
        /* The two cursors side by side: the dots, then what the chip does with them. */
        for (int c = 0; c < 2; c++) {
            const Cursor& q = k[c];
            ImVec2 at(floorf(end.x + gap + c * (32 * m + ui::Px(8) + gap)), o.y);
            char text[256];
            textf(text, sizeof(text), ts(TS_V9_CUR_TITLE), c);
            sprTitle(dl, at, text, NULL, at.x + 32 * m);
            ImVec2 pa(at.x, floorf(at.y + lh + ui::Px(6)));
            curDrawPattern(s, q, pa, m);
            ImGui::SetCursorScreenPos(ImVec2(at.x, pa.y + 32 * m + ui::Px(6)));
            /* A group, so that each line starts under the dots rather than at the child's left. */
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(at.x + 32 * m + ui::Px(8));
            textf(text, sizeof(text), ts(TS_V9_CUR_POS), q.x, q.y, q.top, (q.top + 31) & 511);
            ImGui::TextUnformatted(text);
            textf(text, sizeof(text), ts(TS_V9_CUR_FLAGS), q.cc, (int)q.eor, (int)q.pr0, 0x7fe00 + 8 * c);
            note(text);
            int entry = ((s->regs[28] & 0x0f) << 2) | q.cc;
            if (!q.shown && (s->regs[8] & 0x40)) textf(text, sizeof(text), "%s", ts(TS_V9_CUR_HIDDEN_SPD));
            else if (q.pr0) textf(text, sizeof(text), "%s", ts(TS_V9_CUR_HIDDEN_PR0));
            else if (!q.shown) textf(text, sizeof(text), "%s", ts(TS_V9_CUR_HIDDEN_CC));
            else if (q.cc == 0) textf(text, sizeof(text), "%s%s", ts(c == 0 ? TS_V9_CUR_ORDER0 : TS_V9_CUR_ORDER1), ts(TS_V9_CUR_EOR));
            else if (q.eor) textf(text, sizeof(text), ts(c == 0 ? TS_V9_CUR_ORDER0_EOR_CC : TS_V9_CUR_ORDER1_EOR_CC), entry);
            else textf(text, sizeof(text), ts(c == 0 ? TS_V9_CUR_ORDER0_CC : TS_V9_CUR_ORDER1_CC), entry);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
        }
        ImGui::SetCursorScreenPos(o);
        ImGui::Dummy(ImVec2(w, h + ui::Px(8)));
        bool hovered = ImGui::IsWindowHovered();
        ImGui::EndChild();
        ui::CanvasShown(sprCv, (float)N, hovered);
    }

    void cardSprites(const V9990DebugState* s)
    {
        if (s->mode != V9990_MODE_P1 && s->mode != V9990_MODE_P2) {
            ui::TextFitItem(ts(TS_V9_SPRITES_NONE), uiTokens().textSecondary);
            return;
        }
        ensureSprites(s);
        if (!ui::BeginKeyValue("spr")) return;
        ui::KeyValue(ts(TS_KV_MODE), "%s \xC2\xB7 16x16", modeName(s->mode));
        ui::KeyValue(ts(TS_V9_KV_SPRITES), "%s", ts(spr.off ? TS_V9_SPRITES_OFF : TS_V9_SPRITES_ON));
        ui::KeyValue(ts(TS_KV_SHOWN), "%d", spr.shown);
        ui::KeyValue(ts(TS_V9_KV_PR0), "%d", spr.pr0);
        ui::KeyValue(ts(TS_KV_DROPPED), "%d", spr.dropped);
        ui::KeyValue(ts(TS_SPR_OFF_SCREEN), ts(TS_V9_KV_OFF_VALUE), spr.offY + spr.offX, spr.offY, spr.offX);
        ui::KeyValue(ts(TS_KV_OVER_LIMIT), ts(TS_KV_OVER_LIMIT_VALUE), spr.overLines, V9_SPRITE_PLACES);
        if (spr.firstDropped >= 0) ui::KeyValue(ts(TS_KV_FIRST_DROPPED), ts(TS_KV_FIRST_DROPPED_VALUE), spr.firstDropped, spr.firstDropLine);
        else ui::KeyValue(ts(TS_KV_FIRST_DROPPED), "%s", ts(TS_KV_NONE));
        ui::KeyValueMono(ts(TS_KV_ATTRIBUTES), "%05X", spr.attrBase);
        ui::KeyValueMono(ts(TS_KV_PATTERNS), "%05X (R#25 = %02X)", spr.patBase, s->regs[25]);
        ui::EndKeyValue();
    }

    /* The bytes, 512 a row in CPU or physical order, tinted by the VRAM map's lanes. */
    void drawVram(const V9990DebugState* s, Focus& f)
    {
        vramCv.aspectX = vramCv.aspectY = 1;
        vramCv.lineRows = 1;
        vramCv.gutter = 44;   /* the address ruler */
        const char* orderNames[] = { ts(TS_V9_LOGICAL_BYTES), ts(TS_V9_PHYSICAL_BYTES) };
        ui::BeginCommandBar("bar");
        ui::CommandBarItem(ui::SegmentedWidth(orderNames, IM_ARRAYSIZE(orderNames)), ui::SegmentedHeight());
        ui::Segmented("vorder", &vramOrder, orderNames, IM_ARRAYSIZE(orderNames));
        ui::CommandBarItem(ui::IconButtonWidth(UI_ICON_TABLES, ts(TS_CARD_TABLES)), ui::Px(24));
        if (ui::IconButton("##tint", UI_ICON_TABLES, ts(TS_CARD_TABLES), ts(TS_VRAM_TABLES_TIP), vramTint)) vramTint = !vramTint;
        ui::GridToggle(vramCv);
        zoomGroup(vramCv, false);
        ui::EndCommandBar();
        ovState = s;
        char key[64];
        snprintf(key, sizeof(key), "%d/%d", vramOrder, s->mode);
        bool decoded = vramPic.needsDecodeVram(key);
        if (decoded) {
            vramPic.w = 512;
            vramPic.h = V9990_VRAM_SIZE / 512;
            vramPic.img.resize(V9990_VRAM_SIZE);
            for (UInt32 i = 0; i < V9990_VRAM_SIZE; i++) {
                uint32_t v = s->vram[vramOrder == 0 ? v9990MapAddress((V9990Mode)s->mode, i) : i];
                vramPic.img[i] = 0xff000000u | (v << 16) | (v << 8) | v;
            }
        }
        vramPic.upload(decoded);
        ui::CanvasHit hit = ui::Canvas("vram", vramPic.tex.id(), vramPic.w, vramPic.h, vramCv, ImVec2(0, 0), vramOverlay, this);
        if (hit.clickedEmpty) shell::releaseSelection();
        if (hit.valid) {
            UInt32 a = (UInt32)(hit.y * 512 + hit.x);
            UInt32 phys = vramOrder == 0 ? v9990MapAddress((V9990Mode)s->mode, a) : a;
            char text[256], in[768];
            if (vramOrder == 0) textf(text, sizeof(text), ts(TS_V9_STATUS_LOGICAL), a, phys, s->vram[phys]);
            else textf(text, sizeof(text), ts(TS_V9_STATUS_PHYSICAL), a, s->vram[a]);
            shell::laneNames(phys, in, sizeof(in));
            ui::StatusText("%s%s", text, in);
            Focus fh = { f.chipId, vramOrder == 1 ? FOCUS_PHYS : FOCUS_LOGICAL, vramOrder == 1 ? phys : a, 1,
                         FOCUS_VRAM, hit.x, hit.y, false };
            pin(f, fh, hit.clicked);
        }
    }

    void cardOverview(const V9990DebugState* s)
    {
        UInt32 sx, sy, mask;
        scrollOf(s, &sx, &sy, &mask);
        if (!ui::BeginKeyValue("ov")) return;
        ui::KeyValue(ts(TS_KV_CHIP), "%s", s->name);
        ui::KeyValue(ts(TS_KV_MODE), "%s%s", modeName(s->mode), s->frameMode != s->mode ? ts(TS_V9_FRAME_OTHER_MODE) : "");
        if (isBitmap(s->mode)) ui::KeyValue(ts(TS_KV_SCREEN), "%d x %d", screenWidth(s->mode), screenLines(s));
        ui::KeyValue(ts(TS_KV_IMAGE_SPACE), "%d x %d", imageWidth(s), imageRows(s));
        ui::KeyValue(ts(TS_KV_COLOUR), "%s%s", colourType(s), (s->modeR6 & 3) == 2 && (s->regs[13] & 0x20) ? " + YAE" : "");
        ui::KeyValue(ts(TS_KV_INTERLACE), "%s", ts(s->interlace ? (fieldsWoven(s) ? TS_INTERLACE_EO : TS_ON) : TS_OFF));
        ui::KeyValue(ts(TS_KV_DISPLAY), "%s", ts(s->displayOn ? TS_ON : TS_OFF));
        if (s->mode == V9990_MODE_P1 || s->mode == V9990_MODE_P2) {
            V9990Layer a, b;
            v9990LayerOf(s, 0, &a);
            ui::KeyValueMono(ts(TS_KV_SCROLL_A), "%d, %d", a.sx, a.sy);
            if (v9990LayerOf(s, 1, &b)) ui::KeyValueMono(ts(TS_KV_SCROLL_B), "%d, %d", b.sx, b.sy);
            int pa = (s->regs[13] & 0x03) << 4, pb = (s->regs[13] & 0x0c) << 2;
            ui::KeyValue(ts(TS_KV_PALETTE_OFFSET), ts(s->mode == V9990_MODE_P1 ? TS_V9_PALETTE_OFFSET_AB : TS_V9_PALETTE_OFFSET_P2),
                         pa, pa + 15, pb, pb + 15);
            /* B comes in front from dot PRX * 64, and on every dot from line PRY * 64. */
            int prx = s->regs[27] & 0x03, pry = (s->regs[27] >> 2) & 0x03;
            if (s->mode == V9990_MODE_P1 && prx == 0 && pry == 0) ui::KeyValue(ts(TS_KV_FRONT), "%s", ts(TS_V9_FRONT_A));
            else if (s->mode == V9990_MODE_P1 && pry == 0) ui::KeyValue(ts(TS_KV_FRONT), ts(TS_V9_FRONT_SPLIT_X), prx * 64);
            else if (s->mode == V9990_MODE_P1 && prx == 0) ui::KeyValue(ts(TS_KV_FRONT), ts(TS_V9_FRONT_SPLIT_Y), pry * 64);
            else if (s->mode == V9990_MODE_P1) ui::KeyValue(ts(TS_KV_FRONT), ts(TS_V9_FRONT_SPLIT), prx * 64, pry * 64);
        }
        else {
            ui::KeyValueMono(ts(TS_KV_SCROLL_A), "%u, %u", sx, sy & 0x1fff);
        }
        ui::KeyValue(ts(TS_KV_BACKDROP), "%d", s->regs[15] & 63);
        ui::KeyValue(ts(TS_KV_MASTER_CLOCK), "%s", s->mcs ? "MCS 1" : "MCS 0");
        ui::EndKeyValue();
    }

    /* The tables of P1 or P2 at their CPU addresses, in the VRAM map's colours. */
    void cardTables(const V9990DebugState* s)
    {
        const UiTokens& t = uiTokens();
        TableRow r[8];
        int n = tableRows(s, r);
        if (n == 0) {
            ui::TextFitItem(ts(TS_NO_TABLES), uiTokens().textSecondary);
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
        int hover = shell::hoverSeries();
        const char* addrTip = ts(TS_V9_TBL_ADDR_TIP);
        for (int i = 0; i < n; i++) {
            ImGui::TableNextRow();
            if (hover == r[i].series) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, t.subtleSelected);
            ImGui::TableNextColumn();
            float sz = ui::Px(10);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float dy = floorf((ImGui::GetTextLineHeight() - sz) / 2);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + dy), ImVec2(p.x + sz, p.y + dy + sz), t.series[r[i].series % 6], ui::Px(2));
            ImGui::Dummy(ImVec2(sz, ImGui::GetTextLineHeight()));
            ImGui::SameLine(0, ui::Px(8));
            /* P2 lays some tables out unlike their CPU addresses: how, on hover. */
            const char* rowTip = NULL;
            if (s->mode == V9990_MODE_P2) {
                if (r[i].series == 5) rowTip = ts(TS_V9_TBL_ATTR_TIP_P2);
                else if (r[i].addr < 0x78000 || r[i].series == 4) rowTip = ts(TS_V9_TBL_INTERLEAVED_TIP);
            }
            ui::TextFitItem(r[i].name, 0, true, rowTip);
            ImGui::TableNextColumn();
            char tip[256];
            textf(tip, sizeof(tip), "%s%s%s", addrTip, rowTip != NULL ? "\n" : "", rowTip != NULL ? rowTip : "");
            ui::PushFont(UI_FONT_MONO);
            fitCell(range[i], tip);
            ui::PopFont();
        }
        ImGui::EndTable();
    }

    void cardPalette(const V9990DebugState* s)
    {
        const UiTokens& t = uiTokens();
        /* The first entry of each range the renderer reads by R#13: layers A and B (P2's even and
        ** odd dots), BP2/BP4's offset and in B4-B7 the one 32 on, YAE's; none in the other bitmaps. */
        int r13 = s->regs[13], cm = s->modeR6 & 3, at[2] = { -1, -1 };
        bool hi = isHiRes(s->mode);
        if (s->mode == V9990_MODE_P1 || s->mode == V9990_MODE_P2) {
            at[0] = (r13 & 0x03) << 4;
            at[1] = (r13 & 0x0c) << 2;
        }
        else if (isBitmap(s->mode) && cm < 2) {
            at[0] = (r13 & (cm == 0 ? (hi ? 0x07 : 0x0f) : (hi ? 0x04 : 0x0c))) << 2;
            if (hi) at[1] = at[0] + 32;
        }
        else if (isBitmap(s->mode) && cm == 2 && (r13 >> 6) >= 2 && (r13 & 0x20)) {
            at[0] = (r13 & 0x0c) << 2;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::Px(2), ui::Px(4)));
        for (int i = 0; i < 64; i++) {
            char id[16], tip[256];
            UInt16 c = v9990PaletteColor(s->palette, i);
            uint32_t rgba = v9990Rgba(c, false);
            bool ys = (c & 0x8000) != 0;
            if (i % 16 == 0) {
                ui::PushFont(UI_FONT_MONO);
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
                ImGui::Text("%2d", i);
                ImGui::PopStyleColor();
                ui::PopFont();
                ImGui::SameLine(0, ui::Px(6));
            }
            else {
                ImGui::SameLine();
            }
            snprintf(id, sizeof(id), "##p%d", i);
            bool offset = i == at[0] || i == at[1];
            int tl = textf(tip, sizeof(tip), ts(TS_V9_PAL_TIP), i, i / 16, s->palette[i * 4] & 31, s->palette[i * 4 + 1] & 31,
                           s->palette[i * 4 + 2] & 31, ys ? ts(TS_V9_PAL_YS) : "");
            if (offset && tl >= 0 && (size_t)tl < sizeof(tip)) textf(tip + tl, sizeof(tip) - tl, "%s", ts(TS_V9_PAL_TIP_OFFSET));
            ui::ColorSwatch(id, (ImU32)rgba, 14, offset, tip);
            if (ys) {
                ImVec2 q = ImGui::GetItemRectMax(), p = ImGui::GetItemRectMin();
                float m = floorf((q.x - p.x) / 2);
                ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(q.x - m, p.y), q.x > p.x ? ImVec2(q.x, p.y) : q, ImVec2(q.x, p.y + m), t.text);
            }
        }
        ImGui::PopStyleVar();
    }

    void cardRegisters(const V9990DebugState* s)
    {
        /* The registers as they are now; pictures read R#13 as the last displayed line drew it. */
        UInt8 regs[64];
        memcpy(regs, s->regs, sizeof(regs));
        regs[13] = s->liveR13;
        drawRegBank(regBankV9990, regs, regFade);
        ImGui::Spacing();
        ui::SectionTitle(ts(TS_SECTION_PORTS));
        drawRegBank(regBankV9990Ports, s->ports, portFade);
    }
};

}

ChipView* createV9990View()
{
    return new V9990View();
}
