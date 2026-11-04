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
#include "VdpRaster.h"
#include "VdpParts.h"
#include "ToolText.h"
extern "C" {
#include "Emulator.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace {

using namespace parts;


enum { C_SELECTION, C_SPLITS, C_DISPLAY, C_TABLES, C_STATUS, C_COMMAND, C_PALETTE, C_REGISTERS, C_COUNT };

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

bool interleaved(int mode)
{
    return mode == 7 || mode == 8 || mode == 10 || mode == 12;
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

const char* cmdName(int cm)
{
    static const char* names[16] = { "STOP", "-", "-", "-", "POINT", "PSET", "SRCH", "LINE",
                                     "LMMV", "LMMM", "LMCM", "LMMC", "HMMV", "HMMM", "YMMM", "HMMC" };
    return names[cm & 15];
}

const char* logOpName(int lo)
{
    static const char* names[16] = { "IMP", "AND", "OR", "EOR", "NOT", "-", "-", "-",
                                     "TIMP", "TAND", "TOR", "TEOR", "TNOT", "-", "-", "-" };
    return names[lo & 15];
}

class V99x8View : public ChipView {
public:
    V99x8View()
    {
        outCv.shared = vramCv.shared = &shell::zoom();
        vramCv.frameless = outCv.frameless = true;
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
        (void)st;
        int n = 0;
        if (n < max) out[n++] = ViewDesc{ "screen", ts(TS_VIEW_SCREEN), UI_ICON_SCREEN };
        if (n < max) out[n++] = ViewDesc{ "vram", "VRAM", UI_ICON_VRAM };
        return n;
    }

    void drawView(int v, const void* st, Focus& f) override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        ViewDesc d[8];
        int n = views(d, 8, st);
        /* A pin on a tile means nothing once the screen is a bitmap, and back; a band's pin follows its line's band. */
        int family = vdpIsBitmapMode(s->screenMode) ? 1 : 0;
        ensureRaster(s);
        if (family != lastFamily && f.pinned && f.kind != FOCUS_VRAM && !bandFocus(f)) {
            f.pinned = false;
            f.kind = FOCUS_NONE;
        }
        refreshBandPin(s, f);
        /* Also while the card is folded away. */
        notePin(f);
        if (!(f.pinned && f.kind != FOCUS_NONE) && selBand < 0) selLatch = 0;
        lastFamily = family;
        ensureLinePalettes(s);
        /* On every tab: the tables and palettes each line used; Output asks for the samples too. */
        shell::wantSpriteTables();
        shell::wantFrame(false);
        if (v < 0 || v >= n) return;
        ui::BeginTabCard("card");
        if (strcmp(d[v].key, "screen") == 0) drawOutput(s, f);
        else drawVram(s, f);
        ui::EndTabCard();
    }

    int cards(CardDesc* out, int max, const void* st) const override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        const CardDesc all[C_COUNT] = {
            { "sel", NULL, true }, { "splits", NULL, true }, { "disp", ts(TS_CARD_DISPLAY), true },
            { "tables", ts(TS_CARD_TABLES), true }, { "status", ts(TS_CARD_STATUS), true },
            { "cmd", ts(TS_CARD_COMMAND), true }, { "pal", ts(TS_CARD_PALETTE), true }, { "regs", ts(TS_CARD_REGISTERS), true },
        };
        int n = 0;
        for (int i = 0; i < C_COUNT && n < max; i++) {
            out[n] = all[i];
            if (i == C_COMMAND) out[n].shown = s->cmdRegCount > 0;
            /* The pinned thing and the bands stay open; the frame's facts fold. */
            out[n].expander = i != C_SELECTION && i != C_SPLITS;
            out[n].summary = i == C_DISPLAY || i == C_TABLES ? cardSummary(s, i) : NULL;
            out[n].noHold = i == C_SELECTION;
            n++;
        }
        return n;
    }

    /* One line for a folded card. */
    const char* cardSummary(const VdpDebugState* s, int c) const
    {
        char* out = summaries[c == C_DISPLAY ? 0 : 1];
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
        return out;
    }

    void drawCard(int c, const void* st, Focus& f) override
    {
        const VdpDebugState* s = (const VdpDebugState*)st;
        ensureLinePalettes(s);
        switch (c) {
        case C_SELECTION: cardSelection(s, f); break;
        case C_DISPLAY:   cardDisplay(s); break;
        case C_SPLITS:    splitsBody(s); break;
        case C_TABLES:    cardTables(s); break;
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
            if (interleaved(l.mode)) {
                /* Even bytes in the low 64K, odd ones from vram128. */
                UInt32 a = (UInt32)(r[i].addr & 0x1ffff) >> 1, len = (UInt32)r[i].len / 2;
                if (k < max) out[k++] = VramLane{ a, len, r[i].kind, r[i].name, NULL, band + 1, (UInt32)r[i].cap / 2 };
                if (k < max) out[k++] = VramLane{ a + (UInt32)s->vram128, len, r[i].kind, r[i].name, NULL, band + 1, (UInt32)r[i].cap / 2 };
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

    void escape() override
    {
        selectBand(-1);
    }

    bool bandsKnown(const void* st) const override
    {
        const_cast<V99x8View*>(this)->ensureRaster((const VdpDebugState*)st);
        return raster.valid;
    }

    void forget() override
    {
        outPic.forget();
        selSheetPic.forget();
        vramPic.forget();
        regFadeReset(regFade);
        regFadeReset(statFade);
        lastFamily = -1;
        rasterVer = ~0u;
        rasterBuilt = false;
        linePalShot = linePalRaster = ~0u;
        selBand = -1;
        selWanted = false;
        selLatch = 0;
        palHeldAt = -1;
    }

private:
    ViewPicture     vramPic;
    ViewPicture     selSheetPic;          /* the Selection's, for a band's or a cell's pin: never uploaded */
    ui::CanvasState vramCv;
    int             lastFamily = -1;      /* 1 bitmap, 0 pattern mode */
    bool            vramTint = true;      /* the bytes tinted by the VRAM map's lanes */
    RegFade         regFade, statFade;
    VdpRasterView   raster;
    unsigned        rasterVer = ~0u;
    bool            rasterBuilt = false;
    int             selBand = -1;         /* the band picked on the Splits card */
    ui::HeightHold  pillHold, bandHold;   /* the Splits card's pill row and band rows keep their room a while */
    VdpBandKey      selKey;               /* found again by its family and lines after a rebuild */
    struct PinSeen { int kind, a, b, grid; };
    PinSeen         pinSeen = { -1, 0, 0, 0 };  /* a band's pin, and the grid it was last read on */
    bool            selWanted = false;    /* selKey stays picked while no band matches it */
    mutable char    summaries[2][256];    /* the folded Display and Tables cards' lines */
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

    /* For the overlays, which run inside Canvas. */
    const VdpDebugState* ovState = NULL;
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

    /* The pattern sheet of a layout into pic; true when decoded again. */
    bool decodeSheet(const VdpDebugState* s, const VdpLayout& l, ViewPicture& pic)
    {
        char key[64];
        const UInt8* r = l.regs;
        snprintf(key, sizeof(key), "%d/%02X%02X%02X%02X%02X%02X/%d/%d/%08X", l.mode, r[3], r[4], r[7], r[8], r[10], r[12], l.blink,
                 vdpSheetBlocks(s, l), palHash(l.rgb));
        bool decoded = pic.needsDecode(key);
        if (decoded) {
            vdpDecodePatternSheet(s, l, VDP_COLOR0_AUTO, pic.img, &pic.w, &pic.h);
        }
        return decoded;
    }

    /* Display lines of a layout: 212 with R#9 LN. */
    static int layoutLines(const VdpDebugState* s, const UInt8* r)
    {
        return !vdpIsMsx1(s) && (r[9] & 0x80) ? 212 : 192;
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
        memcpy(rasterRegs, s->regs, 32);
        vdpRasterBuild(s, lines, &raster);
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
        bool ok = o != NULL && o->valid && o->linePalValid && !o->stale && !vdpIsMsx1(s) && raster.valid &&
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

    void selectBand(int i)
    {
        selBand = i;
        cardBand = i >= 0;
        selWanted = i >= 0 && i < (int)raster.bands.size();
        if (selWanted) selKey = vdpBandKeyOf(raster.bands[i]);
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
            /* VRAM views share a and b as picture spots, so a VRAM pin also needs the same bytes. */
            bool same = f.pinned && f.kind == hit.kind && f.a == hit.a && f.b == hit.b;
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
        return -1;
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

    /* The zoom at the bar's right end. */
    void zoomGroup(ui::CanvasState& cv)
    {
        ui::CommandBarRight(ui::ZoomControlsWidth());
        ui::ZoomControls("zoom", cv);
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

    void decodeOutput(const VdpDebugState* s, const VdpFrameShot* o)
    {
        if (outBorders) {
            outPic.w = VDP_SHOT_WIDTH;
            outPic.h = o->rows;
            outPic.img.assign((size_t)outPic.w * outPic.h, 0);
            for (int y = 0; y < o->rows; y++) outRow(o, y, &outPic.img[(size_t)y * outPic.w]);
            return;
        }
        std::vector<uint32_t> row(VDP_SHOT_WIDTH);
        outPic.w = 512;
        outPic.h = raster.lines;
        outPic.img.assign((size_t)outPic.w * outPic.h, 0);
        for (int d = 0; d < raster.lines; d++) {
            int y = d + outFirstLine(s, o) - o->displayOffset, x0 = outOrigin(s, o, d);
            if (y < 0 || y >= o->rows) continue;
            outRow(o, y, row.data());
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
        std::vector<int> runs;
        const Focus& f = shell::shownFocus();
        self->pinRows(f, runs);
        for (size_t k = 0; k + 1 < runs.size(); k += 2) {
            ImVec2 a = xf.toScreen(0, (float)runs[k]), b = xf.toScreen(w, (float)runs[k + 1]);
            dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t.accent, 0, th * 2);
        }
        dl->PopClipRect();
        if (xf.gutterMax.x > xf.gutterMin.x) dl->AddRectFilled(xf.gutterMin, xf.gutterMax, t.controlFillPressed);
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
        ensureRaster(s);
        outCv.aspectX = 1;
        outCv.aspectY = 2;   /* 544 samples a row: 512-dot units */
        outCv.gutter = rasterMarks() ? 16.0f : 0.0f;
        outCv.checker = false;

        ui::BeginCommandBar("bar");
        barToggle("##borders", UI_ICON_BORDERS, ts(TS_OUT_BORDERS), ts(TS_OUT_BORDERS_TIP), &outBorders);
        ui::GridToggle(outCv);
        if (o != NULL && o->interlace != 0) {
            /* Paused within a frame, the field is that of the rows drawn so far. */
            const char* name = ts(o->interlace == 1 ? TS_OUT_FIELD_ODD : TS_OUT_FIELD_EVEN);
            bool mixed = !o->ended && (o->drawnRow > 0 || o->drawnSamples > 0);
            char field[128];
            textf(field, sizeof(field), mixed ? ts(TS_OUT_FIELD_ABOVE) : "%s", name);
            barText(field);
        }
        zoomGroup(outCv);
        ui::EndCommandBar();

        /* Without a new picture the last one stays, dimmed; one copied for another tab has no samples. */
        if (o != NULL && o->pixels) {
            char key[64];
            /* The crop follows the bands' modes, which come with the raster log. */
            snprintf(key, sizeof(key), "o%d/%d/%d/%u/%d", outBorders, raster.lines, s->screenMode, outBorders ? 0u : shell::rasterVersion(),
                     outBorders ? 0 : outFirstLine(s, o));
            bool decoded = outPic.needsDecodeAt(ver, key);
            if (decoded) decodeOutput(s, o);
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

        char mt[1024];
        bool onMarks = h.gutterY >= 0 && rasterMarks() && ImGui::GetIO().MousePos.x >= ovMarkX;
        if (onMarks && marksAtRow(h.gutterY, mt, sizeof(mt))) {
            ui::StatusText("%s", mt);
        }
        else if (h.valid && o != NULL && outPic.w > 0) {
            outputHover(s, o, f, h);
        }
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

    /* ---- VRAM ---- */

    /* Where a physical span lands on the VRAM picture: the one mapping
    ** the focus and the map's hover share. */
    bool vramRect(UInt32 phys, UInt32 len, float* x0, float* y0, float* x1, float* y1) const
    {
        *x0 = (float)(phys & 255); *y0 = (float)(phys >> 8);
        *x1 = len >= 256 ? 256.0f : *x0 + (float)len; *y1 = *y0 + 1;
        if (len >= 256) *x0 = 0;
        return true;
    }

    /* The block hovered on the map and the focus, over the VRAM picture. */
    void focusMarks(ImDrawList* dl, const ui::CanvasXf& xf) const
    {
        const UiTokens& t = uiTokens();
        const Focus& f = shell::shownFocus();
        float th = fmaxf(1.0f, ui::Px(1)), x0, y0, x1, y1;
        int block = shell::hoverBlock();
        if (block >= 0 && vramRect((UInt32)block << 8, 256, &x0, &y0, &x1, &y1)) {
            ImVec2 a = xf.toScreen(x0, y0), b = xf.toScreen(x1, y1);
            dl->AddRectFilled(a, b, ui::WithAlpha(t.accent, 70));
        }
        if (f.kind == FOCUS_NONE || f.len == 0) return;
        UInt32 phys = toPhysical(ovState, f.space, f.addr);
        if (!vramRect(phys, f.space == FOCUS_PHYS ? f.len : 1, &x0, &y0, &x1, &y1)) return;
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
        self->focusMarks(dl, xf);
    }

    /* The bytes themselves, 256 a row by physical address, tinted by the VRAM map's lanes. */
    void drawVram(const VdpDebugState* s, Focus& f)
    {
        vramCv.aspectX = vramCv.aspectY = 1;
        vramCv.gutter = 44;   /* the address ruler */
        ui::BeginCommandBar("bar");
        barToggle("##tint", UI_ICON_TABLES, ts(TS_CARD_TABLES), ts(TS_VRAM_TABLES_TIP), &vramTint);
        ui::GridToggle(vramCv);
        zoomGroup(vramCv);
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

    /* The pin as clicked, not as worked out again each frame: a VRAM byte is its
    ** address. A new pin takes the card from a band picked before. */
    void notePin(const Focus& f)
    {
        bool pinned = f.pinned && f.kind != FOCUS_NONE, fixedAddr = f.kind == FOCUS_VRAM;
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

    /* The header names what is pinned, with its kind's icon and a pin that lets
    ** go of it; under it a caption says where it is: line, band and mode. */
    void selectionHeader(const VdpDebugState* s, Focus& f, bool band)
    {
        const UiTokens& t = uiTokens();
        char head[160] = "", caption[512] = "";
        const char* glyph = UI_ICON_PIXEL;
        int bi = bandOfFocus(f);
        bool multi = raster.valid && splitBands() > 1;
        if (band) {
            const VdpBand& b = raster.bands[selBand];
            glyph = UI_ICON_BAND;
            textf(head, sizeof(head), ts(TS_SEL_HEAD_BAND), b.first, b.last, shortModeName(b.mode));
            bandText(s, selBand, caption, sizeof(caption));
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
            int lines = screenLines(s), d = bandFocus(f) ? f.b : (f.b - vdpScreenScroll(s, L)) & 0xff;
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
            else if (bi >= 0 || d < lines) textf(caption, sizeof(caption), ts(TS_SEL_CAPTION_LINE), d, mode);
            else textf(caption, sizeof(caption), ts(TS_SEL_CAPTION_SCROLL), f.b, mode);
        }

        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ui::Px(24), right = p.x + ImGui::GetContentRegionAvail().x, x = p.x, cy = floorf(p.y + h / 2);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ui::HasIcons()) {
            ui::DrawIcon(dl, ImVec2(x + ui::Px(8), cy), glyph, 16, t.text);
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
        int firstLine = 0;    /* a tile's b is a scroll line */
        int bi = bandOfFocus(f);
        /* A band's pin kept while its line has no band of its kind to read: the caption says so. */
        if (bandFocus(f) && bi < 0) return;
        if (bi >= 0) {
            L = withPal(vdpLayoutOf(raster.bands[bi]), palOfBand(bi));
            firstLine = -1;
        }
        int mode = L.mode;

        if (f.kind == FOCUS_TILE) {
            VdpCharInfo ci;
            bool ok = vdpNameTableChar(s, L, f.a, f.b, firstLine, &ci);
            if (!ok || !vdpIsPatternMode(mode)) return;
            ViewPicture& sheet = selSheetPic;
            decodeSheet(s, L, selSheetPic);
            int sx, sy;
            sheetCell(ci.ch, &sx, &sy);
            /* Rows 24-31 of SCREEN 2/4 may use a fourth block the sheet does not hold. */
            bool direct = sy >= sheet.h;
            uint32_t pal[16];
            vdpPaletteRgba(s, L, VDP_COLOR0_AUTO, pal);
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
                ui::KeyValueMono(ts(TS_KV_CELL), "%d, %d", ci.col, ci.row);
                ui::KeyValueMono(ts(TS_KV_NAME), "%05X", ci.nameAddr);
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
            ui::KeyValue(ts(TS_KV_INTERLACE), "%s", ts((s->regs[9] & 0x08) ? ((s->regs[9] & 0x04) ? TS_INTERLACE_EO_PAGES : TS_ON) : TS_OFF));
            ui::KeyValueMono(ts(TS_KV_VSCROLL), "%d", s->regs[23]);
            int adj = s->regs[18];
            /* As SET ADJUST moves the picture, with R#18's nibble beside it. */
            ui::KeyValueMono(ts(TS_KV_ADJUST), "H %d (%X)  V %d (%X)", vdpSetAdjust(adj, false), adj & 15,
                             vdpSetAdjust(adj, true), (adj >> 4) & 15);
            if (s->version == VDP_V9958) {
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
        ImGui::Text("%s  %s", cmdName(cmr >> 4), logOpName(cmr & 15));
        if (!ui::BeginKeyValue("cmd")) return;
        ui::KeyValueMono("SX, SY", "%d, %d", c[0] | ((c[1] & 1) << 8), c[2] | ((c[3] & 3) << 8));
        ui::KeyValueMono("DX, DY", "%d, %d", c[4] | ((c[5] & 1) << 8), c[6] | ((c[7] & 3) << 8));
        ui::KeyValueMono("NX, NY", "%d, %d", c[8] | ((c[9] & 1) << 8), c[10] | ((c[11] & 3) << 8));
        ui::KeyValueMono("CLR", "%02X", c[12]);
        ui::KeyValueMono("ARG", "%02X%s%s%s", c[13], (c[13] & 8) ? " DIY" : "", (c[13] & 4) ? " DIX" : "",
                         (c[13] & 1) ? " MAJ" : "");
        ui::EndKeyValue();
    }

    void cardPalette(const VdpDebugState* s)
    {
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
        memcpy(vals + 32, s->cmdRegs, 16);
        drawRegBank(regBankV99x8, vals, regFade, regShown, s);
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
