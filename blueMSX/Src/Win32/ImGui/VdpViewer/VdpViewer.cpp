/*****************************************************************************
**
** The VDP Viewer: live screens, VRAM and registers of the video chips.
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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiWindow.h"
#include "ImGuiTheme.h"
#include "ImGuiWidgets.h"
#include "LiveFeed.h"
#include "ChipFeed.h"
#include "ChipView.h"
#include "VdpViewer.h"
#include "ToolText.h"
extern "C" {
#include "Emulator.h"
#include "Language.h"
#include "VdpDebug.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

#define BLOCK_SHIFT  8
#define BLOCK_COUNT  (VDP_DEBUG_VRAM_SIZE >> BLOCK_SHIFT)
/* Seconds a written VRAM block takes to cool down on the map. */
#define HEAT_SECONDS 1.5f

/* ---- snapshot ---------------------------------------------------------- */

static LiveFeed*   feed;
static ChipFeedRef feedRef;      /* process lifetime: see chipFeedRefInit */
static ChipView*   chipViews[CHIP_FAMILY_COUNT];

static unsigned char* last;          /* the content the views draw */
static size_t         lastCap;
static UInt32         lastId;
static int            lastFamily = -1;
static UInt32         lastSeq;
static unsigned       contentVer = 1;
static unsigned       pictureVer = 1;   /* only VRAM, registers and palette */
static unsigned       rasterVer = 1;    /* only the raster logs' heads */
static bool           rasterOn;         /* this window armed the raster log */
static int            splitOn = -1;     /* the family whose sprite split this window armed, or -1 */
static bool           tablesOn;         /* this window armed the sprite tables */
static float          heat[BLOCK_COUNT];

static Focus focus, prevFocus;
static int   hoverSeriesNow = -1, hoverBlockNow = -1;
static int   mapRows = 1;                /* lane rows: grows with the bands, so the map does not jump */
static bool  mapOpen = true;             /* the map's card, for the session */
static bool  paneOpen = true;            /* the details pane, for the session */
static float paneWidth = 358;            /* its width, px at 96 DPI, for the session */
static char  viewKey[CHIP_FAMILY_COUNT][16] = { "screen" };
static char  wantViewKey[16];
static const void* shotNow;          /* the shown snapshot's picture, or NULL */
static unsigned    shotVer = 1;
static UInt32      shotSeq;
static int         frameWanted;      /* a view showed a picture this frame: 2, or 1 needing no samples */
static int         splitWanted = -1; /* the family of a view that showed Output's sprites split off this frame */
static int         viewFamily = -1;  /* the family of the view being drawn */
static bool        tablesWanted;     /* a V99x8 view was drawn this frame */
static bool        releaseWanted;    /* a view asked to let go of the selection */
static ui::CanvasZoom viewerZoom;    /* kept when the window closes, as the views keep theirs */
static shell::PagesState pagesState;

namespace shell {
unsigned     contentVersion() { return contentVer; }
unsigned     pictureVersion() { return pictureVer; }
unsigned     rasterVersion()  { return rasterVer; }
const Focus& shownFocus()     { return focus.pinned ? focus : prevFocus; }
int          hoverSeries()    { return hoverSeriesNow; }
int          hoverBlock()     { return hoverBlockNow; }
void         requestView(const char* key) { snprintf(wantViewKey, sizeof(wantViewKey), "%s", key); }
const void*  frameShot(unsigned* version) { *version = shotVer; return shotNow; }
void         wantFrame(bool pixels) { if (frameWanted < (pixels ? 2 : 1)) frameWanted = pixels ? 2 : 1; }
void         wantSpriteSplit() { splitWanted = viewFamily; }
void         wantSpriteTables() { tablesWanted = true; }
void         releaseSelection() { releaseWanted = true; }
ui::CanvasZoom& zoom()        { return viewerZoom; }
PagesState&  pages()          { return pagesState; }
}

/* Arms (on) or disarms the sprite split of a chip family; -1 is none. */
static void armSplit(int family, int on)
{
    if (family == CHIP_V99X8) vdpSprSplitArm(on);
}

static bool cardOpen[CHIP_FAMILY_COUNT][16];   /* the folding cards opened */
static ui::HeightHold cardHold[CHIP_FAMILY_COUNT][16];

static ChipView* viewOf(int family)
{
    if (family < 0 || family >= CHIP_FAMILY_COUNT) return NULL;
    if (chipViews[family] == NULL) {
        chipViews[family] = createV99x8View();
    }
    return chipViews[family];
}

static const UInt8* vramOf(int family, const void* st, size_t* size)
{
    *size = VDP_DEBUG_VRAM_SIZE;
    return ((const VdpDebugState*)st)->vram;
}

/* Fields that change with the beam on their own, e.g. P#5 VR/HR. Pictures
** do not depend on them, so they are not decoded again for them. */
struct Span { size_t off, len; };

static bool differOutside(const void* a, const void* b, size_t size, const Span* skip, int n)
{
    size_t at = 0;
    for (int i = 0; i <= n; i++) {
        size_t end = i < n ? skip[i].off : size;
        if (end > at && memcmp((const char*)a + at, (const char*)b + at, end - at) != 0) return true;
        if (i < n) at = skip[i].off + skip[i].len;
    }
    return false;
}

static bool pictureFieldsDiffer(int family, const void* a, const void* b)
{
    static const Span skip[] = {
        { offsetof(VdpDebugState, status), sizeof(((VdpDebugState*)0)->status) },
        { offsetof(VdpDebugState, raster), sizeof(((VdpDebugState*)0)->raster) },
        { offsetof(VdpDebugState, vram), VDP_DEBUG_VRAM_SIZE },
    };
    return differOutside(a, b, sizeof(VdpDebugState), skip, 3);
}

/* The raster logs changed; entries past count are stale and not compared. */
static bool rasterDiffers(int family, const void* a, const void* b)
{
    if (family != CHIP_V99X8) return false;
    for (int i = 0; i < 2; i++) {
        const VdpRasterFrame& x = ((const VdpDebugState*)a)->raster[i];
        const VdpRasterFrame& y = ((const VdpDebugState*)b)->raster[i];
        if (memcmp(&x, &y, offsetof(VdpRasterFrame, ev)) != 0) return true;
        int n = x.count < VDP_RASTER_MAX ? x.count : VDP_RASTER_MAX;
        if (n > 0 && memcmp(x.ev, y.ev, (size_t)n * sizeof(VdpRasterEvent)) != 0) return true;
    }
    return false;
}

static bool stateDiffers(int family, const void* a, const void* b, size_t size)
{
    static const Span skip[] = { { offsetof(VdpDebugState, raster), sizeof(((VdpDebugState*)0)->raster) } };
    return differOutside(a, b, size, skip, 1) || rasterDiffers(family, a, b);
}

/* Bumps contentVer only when the content changed, and marks the VRAM blocks
** written since the previous distinct snapshot. */
static void noteSnapshot(const ChipEntry& e, const void* st, UInt32 seq)
{
    size_t size = chipFeedStateSize(e.family);
    if (lastCap < size) {
        free(last);
        last = (unsigned char*)malloc(size);
        lastCap = last ? size : 0;
        lastFamily = -1;
        if (last == NULL) return;
    }
    if (e.id != lastId || e.family != lastFamily) {
        if (lastFamily >= 0 && chipViews[lastFamily]) chipViews[lastFamily]->forget();
        if (chipViews[e.family]) chipViews[e.family]->forget();
        for (auto& row : cardHold) for (ui::HeightHold& h : row) h = ui::HeightHold();
        memcpy(last, st, size);
        lastId = e.id;
        lastFamily = e.family;
        lastSeq = seq;
        memset(heat, 0, sizeof(heat));
        mapRows = 1;
        memset(&focus, 0, sizeof(focus));
        memset(&prevFocus, 0, sizeof(prevFocus));
        hoverSeriesNow = hoverBlockNow = -1;
        focus.chipId = prevFocus.chipId = e.id;
        contentVer++;
        pictureVer++;
        rasterVer++;
        return;
    }
    if (seq == lastSeq) return;
    lastSeq = seq;
    if (!stateDiffers(e.family, last, st, size)) return;
    size_t vsize;
    const UInt8* a = vramOf(e.family, last, &vsize);
    const UInt8* b = vramOf(e.family, st, &vsize);
    bool vramChanged = false;
    for (size_t k = 0; k < vsize >> BLOCK_SHIFT && k < BLOCK_COUNT; k++) {
        if (memcmp(a + (k << BLOCK_SHIFT), b + (k << BLOCK_SHIFT), 1 << BLOCK_SHIFT) != 0) {
            heat[k] = 1;
            vramChanged = true;
        }
    }
    if (vramChanged || pictureFieldsDiffer(e.family, last, st)) pictureVer++;
    if (rasterDiffers(e.family, last, st)) rasterVer++;
    memcpy(last, st, size);
    contentVer++;
}

static int capture(void* dst, void* ref)
{
    return chipFeedCapture(dst, ref);
}

/* ---- VRAM map -------------------------------------------------------- */

static ImU32 alphaOf(ImU32 c, float a)
{
    int v = (int)(((c >> IM_COL32_A_SHIFT) & 0xff) * a);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)(v < 0 ? 0 : v > 255 ? 255 : v) << IM_COL32_A_SHIFT);
}

static bool focusOverlaps(ChipView* v, const void* st, const Focus& f, UInt32 a, UInt32 len)
{
    if (f.kind == FOCUS_NONE || f.len == 0) return false;
    UInt32 p = v->toPhysical(st, f.space, f.addr);
    return p + (f.space == FOCUS_PHYS ? f.len : 1) > a && p < a + len;
}

/* A lane's label: name and lines, the lines alone, or the name, whichever fits. */
static const char* laneLabel(const VramLane& l, float room, char* out, size_t size)
{
    if (l.where) {
        snprintf(out, size, "%s \xC2\xB7 %s", l.name, l.where);
        if (ImGui::CalcTextSize(out).x <= room) return out;
        snprintf(out, size, "%s", l.where);
        if (ImGui::CalcTextSize(out).x <= room) return out;
    }
    snprintf(out, size, "%s", l.name);
    return ImGui::CalcTextSize(out).x <= room ? out : NULL;
}

/* The map's lanes and their rows, worked out before the main row so that
** the card under it is known to be as high as it will be drawn. */
struct MapPlan {
    VramLane lanes[64];
    int      n, row[64];
    bool     perBand;
    UInt32   size;
};

static const MapPlan* planNow;   /* this frame's, while the views draw */

/* "    Name - lines" for each lane of m holding physical address a; size > 0. */
static void lanesAt(const MapPlan& m, UInt32 a, char* out, size_t size)
{
    size_t at = 0;
    out[0] = 0;
    for (int i = 0; i < m.n && at + 1 < size; i++) {
        const VramLane& l = m.lanes[i];
        if (a < l.addr || a >= l.addr + l.len) continue;
        int k = l.where ? textf(out + at, size - at, "    %s \xC2\xB7 %s", l.name, l.where) : textf(out + at, size - at, "    %s", l.name);
        at = k < 0 ? size : at + (size_t)k;
    }
}

namespace shell {
const VramLane* mapLanes(int* n)
{
    *n = planNow ? planNow->n : 0;
    return planNow ? planNow->lanes : NULL;
}

void laneNames(UInt32 a, char* out, size_t size)
{
    if (size == 0) return;
    out[0] = 0;
    if (planNow) lanesAt(*planNow, a, out, size);
}
}

/* The card's header row, all of it when folded, and the body's margins; logical px. */
static const float MAP_HEADER = 32, MAP_BODY_TOP = 4, MAP_BODY_BOTTOM = 8;

static float mapHeaderH() { return ui::Px(MAP_HEADER); }
static float mapLaneH()   { return ui::Px(16); }
static float mapGap()     { return ui::Px(4); }
static float mapActH()    { return ui::Px(3); }

static float mapTickH()
{
    ui::PushFont(UI_FONT_MONO, 12);
    float h = ImGui::GetTextLineHeight();
    ui::PopFont();
    return h;
}

/* The lane rows, the activity strip and the ticks. */
static float mapBodyH()
{
    return mapLaneH() * mapRows + mapGap() * mapRows + mapActH() + mapGap() + mapTickH();
}

static float mapCardH(bool open)
{
    return open ? mapHeaderH() + ui::Px(MAP_BODY_TOP) + mapBodyH() + ui::Px(MAP_BODY_BOTTOM) : mapHeaderH();
}

static void planVramMap(ChipView* v, const void* st, float w, MapPlan& m)
{
    VramLane* lanes = m.lanes;
    int* row = m.row;
    int n = m.n = v->vramLanes(st, lanes, 64);
    UInt32 size = m.size = v->vramSize(st);

    /* Lanes that overlap go to the next row while bands split the frame. */
    int needed = 1;
    bool perBand = false;
    for (int i = 0; i < n; i++) {
        float x0 = (float)lanes[i].addr / size * w, x1 = fmaxf((float)(lanes[i].addr + lanes[i].len) / size * w, x0 + 2);
        int r = 0;
        for (; r < 3; r++) {
            bool fits = true;
            for (int j = 0; j < i && fits; j++) {
                float y0 = (float)lanes[j].addr / size * w, y1 = fmaxf((float)(lanes[j].addr + lanes[j].len) / size * w, y0 + 2);
                if (row[j] == r && lanes[j].len > 0 && x0 < y1 && y0 < x1) fits = false;
            }
            if (fits) break;
        }
        row[i] = r < 3 ? r : 2;
        if (row[i] + 1 > needed) needed = row[i] + 1;
        perBand |= lanes[i].band != 0;
    }
    /* One layout keeps its lanes on one row; the rows shrink only once the bands are unknown. */
    if (!perBand) {
        for (int i = 0; i < n; i++) row[i] = 0;
        needed = 1;
    }
    if (needed > mapRows || !v->bandsKnown(st)) mapRows = needed;
    m.perBand = perBand;
}

/* Drawn after the views, which read its hover a frame late; it shows this frame's focus. */
static void drawVramMap(ChipView* v, const void* st, const MapPlan& m)
{
    const UiTokens& t = uiTokens();
    const VramLane* lanes = m.lanes;
    const int* row = m.row;
    int n = m.n;
    UInt32 size = m.size;
    bool perBand = m.perBand;
    float w = ImGui::GetContentRegionAvail().x;
    float laneH = mapLaneH(), actH = mapActH(), gap = mapGap(), tickH = mapTickH(), pitch = laneH + gap;
    float lanesH = pitch * mapRows - gap, barH = lanesH + gap + actH;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Focus& shown = focus;

    ImGui::InvisibleButton("##vrammap", ImVec2(w, barH + gap + tickH));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    float mx = ImGui::GetIO().MousePos.x, my = ImGui::GetIO().MousePos.y;
    UInt32 hoverAddr = 0;
    int hoverLane = -1;
    hoverSeriesNow = hoverBlockNow = -1;
    if (hovered && w > 0) {
        float fx = (mx - p.x) / w;
        fx = fx < 0 ? 0 : fx > 0.9999f ? 0.9999f : fx;
        hoverAddr = (UInt32)(fx * size);
        hoverBlockNow = (int)(hoverAddr >> BLOCK_SHIFT);
        int hoverRow = !perBand ? 0 : my < p.y + lanesH ? (int)((my - p.y) / pitch) : -1;
        for (int i = 0; i < n; i++) {
            if (hoverAddr < lanes[i].addr || hoverAddr >= lanes[i].addr + lanes[i].len) continue;
            if (hoverLane < 0 || row[i] == hoverRow) hoverLane = i;
        }
        if (hoverLane >= 0) hoverSeriesNow = lanes[hoverLane].series;
    }

    float r = ui::Px(3);
    for (int i = 0; i < n; i++) {
        if (lanes[i].len == 0) continue;
        float x0 = floorf(p.x + (float)lanes[i].addr / size * w);
        float x1 = floorf(p.x + (float)(lanes[i].addr + lanes[i].len) / size * w);
        float ly = p.y + pitch * row[i];
        if (x1 < x0 + 2) x1 = x0 + 2;
        /* The hovered lane, and lanes of its kind that serve the same bands. */
        bool lit = hoverLane == i || (hoverLane >= 0 && hoverSeriesNow == lanes[i].series && lanes[hoverLane].band == lanes[i].band) ||
                   focusOverlaps(v, st, shown, lanes[i].addr, lanes[i].len);
        ImU32 c = t.series[lanes[i].series % 6];
        dl->AddRectFilled(ImVec2(x0, ly), ImVec2(x1, ly + laneH), alphaOf(c, lit ? 0.60f : 0.30f), x1 - x0 > r * 2 ? r : 0);
        ui::PushFont(UI_FONT_BODY, 12);
        char label[192];
        const char* text = laneLabel(lanes[i], x1 - x0 - ui::Px(12), label, sizeof(label));
        if (text) {
            ImVec2 ts = ImGui::CalcTextSize(text);
            dl->AddText(ImVec2(x0 + ui::Px(6), floorf(ly + (laneH - ts.y) / 2)), t.text, text);
        }
        ui::PopFont();
    }

    /* Activity: blocks written lately, brighter the more recent. */
    float ay = p.y + lanesH + gap;
    dl->AddRectFilled(ImVec2(p.x, ay), ImVec2(p.x + w, ay + actH), t.controlFill, ui::Px(2));
    int blocks = (int)(size >> BLOCK_SHIFT);
    float bw = w / (blocks > 0 ? blocks : 1);
    for (int k = 0; k < blocks && k < BLOCK_COUNT; k++) {
        if (heat[k] <= 0.02f) continue;
        float x0 = floorf(p.x + k * bw);
        dl->AddRectFilled(ImVec2(x0, ay), ImVec2(fmaxf(x0 + 1, floorf(p.x + (k + 1) * bw)), ay + actH),
                          alphaOf(t.accent, heat[k]));
    }

    /* The focused address. */
    if (shown.kind != FOCUS_NONE && shown.len > 0) {
        UInt32 a = v->toPhysical(st, shown.space, shown.addr);
        if (a < size) {
            float x = floorf(p.x + (float)a / size * w);
            float th = shown.pinned ? ui::Px(2) : fmaxf(1.0f, ui::Px(1));
            dl->AddRectFilled(ImVec2(x, p.y - ui::Px(2)), ImVec2(x + th, ay + actH + ui::Px(2)), t.text);
        }
    }
    if (hovered) {
        dl->AddLine(ImVec2(floorf(mx), p.y), ImVec2(floorf(mx), ay + actH), alphaOf(t.text, 0.6f));
    }

    /* Ticks every eighth. */
    float ty = p.y + barH + gap;
    ui::PushFont(UI_FONT_MONO, 12);
    /* Every other one when they would touch. */
    int step = w / 8 < ImGui::CalcTextSize("00000").x * 2 ? 2 : 1;
    for (int i = 0; i <= 8; i += step) {
        char lab[16];
        UInt32 a = (UInt32)((UInt64)size * i / 8);
        float x = floorf(p.x + w * i / 8);
        snprintf(lab, sizeof(lab), "%05X", a);
        ImVec2 ts = ImGui::CalcTextSize(lab);
        float tx = i == 0 ? x : i == 8 ? x - ts.x : x - ts.x / 2;
        dl->AddText(ImVec2(tx, ty), t.textSecondary, lab);
    }
    ui::PopFont();

    /* What is under the mouse goes to the status bar; the map is drawn last, so it wins. */
    if (hovered) {
        char text[1024];
        size_t at = (size_t)textf(text, sizeof(text), ts(TS_MAP_PHYSICAL), hoverAddr);
        if (at + 1 < sizeof(text)) {
            lanesAt(m, hoverAddr, text + at, sizeof(text) - at);
            at += strlen(text + at);
        }
        if (at + 1 < sizeof(text) && hoverBlockNow >= 0 && hoverBlockNow < BLOCK_COUNT && heat[hoverBlockNow] > 0.02f) {
            textf(text + at, sizeof(text) - at, "    %s", ts(TS_MAP_WRITTEN));
        }
        ui::StatusText("%s", text);
        if (clicked) {
            Focus f = { focus.chipId, FOCUS_PHYS, hoverAddr & ~255u, 256, FOCUS_VRAM, 0, 0, true };
            focus = f;
            shell::requestView("vram");
        }
    }
}

/* ---- Pages: the card under the Screen, folding as the VRAM map does ---- */

namespace shell {
float pagesStepScale(int step)
{
    return step == 2 ? 1.5f : step == 1 ? 1.0f : 0.5f;
}

float pagesChromeH(bool open)
{
    return ui::Px(32) + (open ? ui::Px(4) + ui::Px(8) : 0);
}

float pagesHeight(float room)
{
    if (!pagesState.open) return 0;
    return ui::SplitSize("##split", &pagesState.height, ui::Px(80), room - ui::Px(160));
}

bool pagesHeader(float paneH, bool* sizeChanged)
{
    *sizeChanged = false;
    /* The splitter in the 8 px under the card (EndTabCard left ItemSpacing below it), then the card. */
    ImVec2 p = ImGui::GetCursorScreenPos();
    float sp = ImGui::GetStyle().ItemSpacing.y;
    if (paneH > 0) {
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y - sp));
        ui::SplitBar("##split", ImGui::GetContentRegionAvail().x, ui::Px(8), paneH);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ui::Px(8) - sp));
    const char* sizes[] = { ts(TS_STRIP_SMALL), ts(TS_STRIP_MEDIUM), ts(TS_STRIP_LARGE) };
    ui::ExpanderStyle es;
    es.headerH = 32;
    es.padLeft = 12;
    es.bodyTop = 4;
    es.bodyBottom = 8;
    es.divider = false;
    es.trailW = ui::SegmentedWidth(sizes, 3);
    bool open = ui::BeginExpander("pages", ts(TS_STRIP), NULL, &pagesState.open, es);
    /* Opened by a click this frame: the room is reserved from the next frame on. */
    if (!open || paneH <= 0) {
        ui::EndExpander();
        return false;
    }
    ImVec2 body = ImGui::GetCursorScreenPos(), ta, tb;
    ui::ExpanderTrail(&ta, &tb);
    ui::PushFont(UI_FONT_STRONG);
    float titleW = ImGui::CalcTextSize(ts(TS_STRIP)).x;
    ui::PopFont();
    /* The size, in the header while it fits beside the title. */
    if (ta.x >= p.x + ui::Px(12) + titleW + ui::Px(16)) {
        int was = pagesState.size;
        ImGui::SetCursorScreenPos(ImVec2(ta.x, floorf((ta.y + tb.y - ui::SegmentedHeight()) / 2)));
        *sizeChanged = ui::Segmented("size", &pagesState.size, sizes, 3) && pagesState.size != was;
    }
    ImGui::SetCursorScreenPos(body);
    return true;
}

void pagesBeginScroll(float paneH, float cw, float ch, PagesScroll& sc)
{
    /* Across when wider than the pane and down when taller; ImGui is told the same, not left to last frame's bars. */
    float w = ImGui::GetContentRegionAvail().x, sb = ImGui::GetStyle().ScrollbarSize;
    bool overX = cw > w, overY = ch > paneH - (overX ? sb : 0);
    overX = overX || (overY && cw > w - sb);
    float toX = overX ? sc.to : 0;
    if (toX >= 0 || !overY) ImGui::SetNextWindowScroll(ImVec2(toX, overY ? -1.0f : 0));
    sc.to = -1;
    ImGui::SetNextWindowContentSize(ImVec2(overX ? cw : fminf(cw, w - (overY ? sb : 0)), overY ? ch : fminf(ch, paneH - (overX ? sb : 0))));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags scroll = (overX ? ImGuiWindowFlags_AlwaysHorizontalScrollbar : 0) | (overY ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0);
    ImGui::BeginChild("pane", ImVec2(w, paneH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | (scroll ? scroll : ImGuiWindowFlags_NoScrollbar));
    ImGui::PopStyleVar();
}

/* Over a row that scrolls only across, the wheel scrolls it across, taken from ImGui a frame
** ahead as the pictures take theirs; Shift+wheel by ImGui's step across, a plain one by its step down. */
void pagesEnd(PagesScroll& sc)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindow* cw = ImGui::GetCurrentWindow();
    bool hovered = ImGui::IsWindowHovered();
    if (sc.wheelOwned && hovered && !io.KeyCtrl && io.MouseWheel != 0) {
        float step = ImTrunc(ImMin((io.KeyShift ? 2 : 5) * cw->FontRefSize, cw->InnerRect.GetWidth() * 0.67f));
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * step);
    }
    sc.x = ImGui::GetScrollX();
    sc.wheelOwned = hovered && !io.KeyCtrl && ImGui::GetScrollMaxX() > 0 && ImGui::GetScrollMaxY() <= 0;
    ImGuiID id = ImGui::GetID("##wheel");
    if (sc.wheelOwned) ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, id);
    else if (ImGui::GetKeyOwner(ImGuiKey_MouseWheelY) == id) ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGuiKeyOwner_NoOwner);
    ImGui::EndChild();
    ui::EndExpander();
}
}

/* ---- window ------------------------------------------------------------ */


static void commandBar(const ChipSnapshot* snap, const LiveFeedInfo& info, ChipView* v, const void* st)
{
    EmuState es = emulatorGetState();
    {
        /* A step over the body text, on a row as high as the controls. */
        float h = ImGui::GetFrameHeight();
        ui::PushFont(UI_FONT_STRONG, 16);
        const char* name = snap && snap->count == 1 ? snap->chip[0].name : "VDP";
        ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(name);
        ImGui::Dummy(ImVec2(ts.x, h));
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, floorf(p.y + (h - ts.y) / 2)), uiTokens().text, name);
        ui::PopFont();
    }
    ImGui::SameLine(0, ui::Px(12));
    if (es == EMU_STOPPED)               ui::StatusDot(ts(TS_STATE_STOPPED), ui::TONE_NEUTRAL);
    else if (snap != NULL && !info.valid) ui::StatusDot(ts(TS_STATE_NO_VDP), ui::TONE_CAUTION);
    else if (snap == NULL)                ui::StatusDot(ts(TS_STATE_WAITING), ui::TONE_NEUTRAL);
    else if (es == EMU_RUNNING)           ui::StatusDot(ts(TS_STATE_LIVE), ui::TONE_SUCCESS);
    else                                  ui::StatusDot(ts(TS_STATE_PAUSED), ui::TONE_CAUTION);
    if (v && st) {
        char text[256];
        v->facts(st, text, sizeof(text));
        ImGui::SameLine(0, ui::Px(12));
        ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(text);
        float right = p.x + ImGui::GetContentRegionAvail().x, y = floorf(p.y + (ImGui::GetFrameHeight() - ts.y) / 2);
        ImGui::Dummy(ImVec2(fmaxf(1.0f, right - p.x), ImGui::GetFrameHeight()));
        ui::TextFit(ImGui::GetWindowDrawList(), ImVec2(p.x, y), right, text, uiTokens().textSecondary,
                    ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip));
    }
}

static void drawCards(ChipView* v, int family, const void* st)
{
    CardDesc cards[16];
    int n = v->cards(cards, 16, st);
    for (int i = 0; i < n; i++) {
        if (!cards[i].shown) continue;
        if (cards[i].expander) {
            if (ui::BeginExpander(cards[i].key, cards[i].title, cards[i].summary, &cardOpen[family][i])) {
                float y0 = ImGui::GetCursorPosY();
                v->drawCard(i, st, focus);
                if (!cards[i].noHold) ui::HoldHeight(cardHold[family][i], y0);
            }
            ui::EndExpander();
            continue;
        }
        /* A card without a title draws its own header. */
        if (cards[i].title) ui::BeginCard(cards[i].key, cards[i].title);
        else ui::BeginCardEx(cards[i].key, ui::CardStyle{ 14, 12, 12, 0 });
        float y0 = ImGui::GetCursorPosY();
        v->drawCard(i, st, focus);
        if (!cards[i].noHold) ui::HoldHeight(cardHold[family][i], y0);
        ui::EndCard();
    }
}

static int selectView(ChipView* v, int family, const void* st, ViewDesc* views, int* count)
{
    *count = v->views(views, 8, st);
    if (wantViewKey[0]) {
        snprintf(viewKey[family], sizeof(viewKey[family]), "%s", wantViewKey);
        wantViewKey[0] = 0;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(views[i].key, viewKey[family]) == 0) return i;
    }
    return 0;
}

static void viewArea(ChipView* v, int family, const void* st)
{
    ViewDesc views[8];
    ui::TabItem tabs[8];
    int n, sel = selectView(v, family, st, views, &n);
    for (int i = 0; i < n; i++) tabs[i] = ui::TabItem{ views[i].glyph, views[i].label, views[i].art };
    if (ui::TabView("views", &sel, tabs, n)) {
        snprintf(viewKey[family], sizeof(viewKey[family]), "%s", views[sel].key);
    }
    v->drawView(sel, st, focus);
}

static void draw(ImGuiToolWindow*, void*)
{
    const UiTokens& t = uiTokens();
    LiveFeedInfo info;
    const ChipSnapshot* snap = (const ChipSnapshot*)liveFeedLatest(feed, &info);
    bool ok = snap != NULL && info.valid && snap->selected >= 0;
    const ChipEntry* e = ok ? &snap->chip[snap->selected] : NULL;
    ChipView* v = ok ? viewOf(e->family) : NULL;

    viewFamily = ok ? e->family : -1;
    if (ok) noteSnapshot(*e, chipFeedState(snap), info.seq);
    const void* st = ok && last ? last : NULL;
    /* Copied only while a view shows it; asked for again from what this frame shows. */
    InterlockedExchange(&feedRef.wantFrame, frameWanted);
    frameWanted = 0;
    if (splitWanted != splitOn) {
        armSplit(splitOn, 0);
        armSplit(splitWanted, 1);
        splitOn = splitWanted;
    }
    splitWanted = -1;
    /* Not running, no frame is drawn: kept armed, as arming again would drop the tables of the frame shown. */
    if (tablesOn && emulatorGetState() != EMU_RUNNING) tablesWanted = true;
    if (tablesWanted != tablesOn) {
        vdpSprTableArm(tablesWanted ? 1 : 0);
        tablesOn = tablesWanted;
    }
    tablesWanted = false;
    shotNow = ok ? chipFeedShot(snap) : NULL;
    if (shotNow != NULL && info.seq != shotSeq) {
        shotSeq = info.seq;
        shotVer++;
    }

    float dt = ImGui::GetIO().DeltaTime / HEAT_SECONDS;
    for (int k = 0; k < BLOCK_COUNT; k++) heat[k] = heat[k] > dt ? heat[k] - dt : 0;

    prevFocus = focus;
    if (!focus.pinned) {
        UInt32 id = focus.chipId;
        memset(&focus, 0, sizeof(focus));
        focus.chipId = id;
    }
    /* Esc, or a click beside a picture last frame, lets go of the pin and of a band picked on the Splits card. */
    bool release = releaseWanted;
    releaseWanted = false;
    bool key = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId);
    if (release || key) {
        if (focus.pinned) {
            focus.pinned = false;
            focus.kind = FOCUS_NONE;
        }
        if (v) v->escape(key);
    }

    commandBar(snap, info, v, st);
    if (v == NULL || st == NULL) {
        hoverSeriesNow = hoverBlockNow = -1;  /* no map this frame: its hover must not light the next chip's lanes */
        ImGui::Dummy(ImVec2(0, ui::Px(24)));
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::TextUnformatted(ts(snap == NULL && emulatorGetState() == EMU_STOPPED ? TS_EMU_STOPPED :
                                  snap == NULL ? TS_EMU_WAITING : TS_NO_VDP_TO_SHOW));
        ImGui::PopStyleColor();
        return;
    }

    /* Top to bottom: the main row, the VRAM map, and the status bar along the window's edge. */
    ImVec2 avail = ImGui::GetContentRegionAvail(), top = ImGui::GetCursorScreenPos();
    MapPlan plan;
    planVramMap(v, st, avail.x - ui::Px(24), plan);
    planNow = &plan;
    float mapH = mapCardH(mapOpen);
    float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - ui::StatusBarHeight() - ui::Px(8) - mapH - ui::Px(8);
    avail.y = fmaxf(bottom - top.y, ui::Px(120));
    /* The details pane at the right, or its rail when folded; the view takes the rest. */
    /* Its edge drags it from 330 wide to where the view keeps 360. */
    float paneW = ui::Px(358), gap = ui::Px(12);
    bool side = avail.x >= paneW + gap + ui::Px(360);
    if (side) {
        if (paneOpen) paneW = ui::SplitSizeX("##panesplit", &paneWidth, ui::Px(330), avail.x - gap - ui::Px(360));
        float w = paneOpen ? paneW : ui::InspectorRailWidth();
        ImGui::BeginChild("left", ImVec2(avail.x - w - gap, avail.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        viewArea(v, e->family, st);
        ImGui::EndChild();
        if (paneOpen) {
            ImGui::SameLine(0, 0);
            ui::SplitBarX("##panesplit", gap, avail.y, paneW);
            ImGui::SameLine(0, 0);
        }
        else {
            ImGui::SameLine(0, gap);
        }
        if (ui::BeginInspectorPane("inspector", &paneOpen, ImVec2(paneW, avail.y), ts(TS_PANE_CLOSE), ts(TS_PANE_OPEN))) {
            drawCards(v, e->family, st);
        }
        ui::EndInspectorPane();
    }
    else {
        /* Too narrow for a column: the cards go under the view, which keeps room for its splitter. */
        ImGui::BeginChild("stack", ImVec2(0, avail.y));
        ImGui::BeginChild("left", ImVec2(0, fmaxf(ui::Px(480), avail.y * 0.6f)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        viewArea(v, e->family, st);
        ImGui::EndChild();
        drawCards(v, e->family, st);
        ImGui::EndChild();
    }

    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + avail.y + ui::Px(8)));
    ui::ExpanderStyle es;
    es.headerH = MAP_HEADER;
    es.padLeft = 12;
    es.bodyTop = MAP_BODY_TOP;
    es.bodyBottom = MAP_BODY_BOTTOM;
    es.legend = t.accent;
    es.divider = false;
    if (ui::BeginExpander("map", ts(TS_MAP_TITLE), ts(TS_MAP_WRITES), &mapOpen, es)) drawVramMap(v, st, plan);
    else hoverSeriesNow = hoverBlockNow = -1;
    ui::EndExpander();
    planNow = NULL;
    ui::StatusBar();
}

/* A window destroyed inside a modal loop reports closing late, maybe after
** the next one opened: only the newest window's report counts. */
static uintptr_t openCount;

static void closed(void* ref)
{
    if ((uintptr_t)ref != openCount) {
        return;
    }
    liveFeedClose(feed);
    feed = NULL;
    if (rasterOn) vdpRasterArm(0);
    rasterOn = false;
    armSplit(splitOn, 0);
    splitOn = -1;
    splitWanted = -1;
    if (tablesOn) vdpSprTableArm(0);
    tablesOn = false;
    tablesWanted = false;
    releaseWanted = false;
    for (int f = 0; f < CHIP_FAMILY_COUNT; f++) {
        if (chipViews[f]) chipViews[f]->forget();
    }
    for (auto& row : cardHold) for (ui::HeightHold& h : row) h = ui::HeightHold();
    free(last);
    last = NULL;
    lastCap = 0;
    lastFamily = -1;
    lastId = 0;
    lastSeq = 0;
    shotNow = NULL;
    shotSeq = 0;
    frameWanted = 0;
    InterlockedExchange(&feedRef.wantFrame, 0);
    memset(&focus, 0, sizeof(focus));
}

static bool openFeed()
{
    if (!chipFeedRefInit(&feedRef)) {
        return false;
    }
    feed = liveFeedOpen(chipFeedSize(), capture, &feedRef);
    if (feed == NULL) {
        return false;
    }
    /* Logged while the window is open, whatever it shows. */
    vdpRasterArm(1);
    rasterOn = true;
    return true;
}

extern "C" void vdpViewerShow(void)
{
    ImGuiToolDesc desc = {};
    if (!imguiToolIsOpen("vdp")) {
        openCount++;
        if (feed == NULL && !openFeed()) {
            return;
        }
    }
    desc.id     = "vdp";
    desc.title  = langMenuToolsVdpViewer();
    desc.width  = 960;
    desc.height = 720;
    desc.draw   = draw;
    desc.closed = closed;
    desc.ref    = (void*)openCount;
    if (imguiToolOpen(&desc) == NULL && !imguiToolIsOpen("vdp")) {
        liveFeedClose(feed);
        feed = NULL;
        if (rasterOn) vdpRasterArm(0);
        rasterOn = false;
    }
}
