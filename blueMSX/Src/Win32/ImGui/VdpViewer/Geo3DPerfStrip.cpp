/*****************************************************************************
**
** Drawing time strip of the geo3d tab, one bar per screen update.
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
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "imgui.h"
#include "ImGuiTheme.h"
#include "ImGuiWidgets.h"
#include "Geo3DPerfStrip.h"
#include "ChipView.h"
#include "VdpParts.h"
#include "VdpDecode.h"
#include "ToolText.h"
extern "C" {
#include "Board.h"
#include "Emulator.h"
#include "DebugTrace.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace {

const UInt32 notWhole = G3P_CUT | G3P_DROPPED | G3P_NO_FLIP;
const int baseMax = 32;

/* The parts bottom up, in the order of the bar and the legend; a TAIL part is its CPU part lighter. */
ImU32 partColour(int k)
{
    const UiTokens& t = uiTokens();
    switch (k) {
    case G3P_GEO:   return t.series[0];
    case G3P_VDP:   return t.series[1];
    case G3P_RUN:   return t.series[2];
    case G3P_IRQ:   return t.series[4];
    case G3P_HALT:  return t.series[5];
    case G3P_OTHER: return t.series[3];
    default:        return ui::Over(ui::WithAlpha(partColour(k - G3P_IRQ_TAIL + G3P_IRQ), 0x50), ui::Over(t.cardFill, t.layer));
    }
}

const char* partName(int k)
{
    static const ToolStringId ids[G3P_PARTS] = { TS_G3D_PERF_PART_GEO, TS_G3D_PERF_PART_VDP, TS_G3D_PERF_PART_RUN,
                                                 TS_G3D_PERF_PART_IRQ, TS_G3D_PERF_PART_HALT, TS_G3D_PERF_PART_OTHER,
                                                 TS_G3D_PERF_PART_TAIL, TS_G3D_PERF_PART_TAIL, TS_G3D_PERF_PART_TAIL };
    return ts(ids[k]);
}

/* The legend's entries: the six parts. */
const int legendEntries = G3P_OTHER + 1;

const char* legendLabel(int e)
{
    static const ToolStringId ids[legendEntries] = { TS_G3D_PERF_LEG_GEO, TS_G3D_PERF_LEG_VDP, TS_G3D_PERF_LEG_RUN,
                                                     TS_G3D_PERF_LEG_IRQ, TS_G3D_PERF_LEG_HALT, TS_G3D_PERF_LEG_OTHER };
    return ts(ids[e]);
}

/* The seen bits that show entry e. */
unsigned legendBits(int e)
{
    return (1u << e) | (e >= G3P_IRQ ? 1u << G3P_TAIL_OF(e) : 0);
}

/* Entry e's swatch from a to b, its right half the lighter tint when split. */
void legendSwatch(ImDrawList* dl, ImVec2 a, ImVec2 b, int e, bool split)
{
    float mid = split ? floorf((a.x + b.x) / 2) : b.x;
    dl->AddRectFilled(a, ImVec2(mid, b.y), partColour(e));
    if (split) dl->AddRectFilled(ImVec2(mid, a.y), b, partColour(G3P_TAIL_OF(e)));
}

/* A part in frames, so the parts add up to the bar's whole frames. */
double partFrames(const Geo3DPerfPicture& p, int k)
{
    UInt64 total = 0;
    for (int i = 0; i < G3P_PARTS; i++) total += p.part[i];
    return total ? (double)p.frames * p.part[k] / (double)total : 0;
}

/* What the tooltip lists and the why compares: the six parts up to the last VDP or RUN end,
** then the time from there to the flip (the TAIL parts together). */
const int rankCount = G3P_OTHER + 2;

UInt32 rankTicks(const Geo3DPerfPicture& p, int k)
{
    return k <= G3P_OTHER ? p.part[k] : p.tail;
}

int rankPart(int k)
{
    return k <= G3P_OTHER ? k : G3P_OTHER_TAIL;
}

double frameRate(UInt32 period)
{
    return period ? (double)boardFrequency() / period : 0;
}

double ticksMs(double ticks)
{
    return ticks * 1000.0 / boardFrequency();
}

/* ticks as microseconds, or milliseconds from one. */
void ticksText(char* out, size_t size, double ticks)
{
    double us = ticks * 1e6 / boardFrequency();
    if (us >= 1000) textf(out, size, ts(TS_G3D_PERF_MS), us / 1000);
    else textf(out, size, ts(TS_G3D_PERF_US), us);
}

}

Geo3DPerfStrip::Geo3DPerfStrip()
{
}

Geo3DPerfStrip::~Geo3DPerfStrip()
{
    forget();
}

void Geo3DPerfStrip::loadUi(const VdpViewerProperties& p)
{
    shown = p.v99x8.g3dPerf != 0;
    heightPx = (float)std::min(std::max(p.v99x8.g3dPerfH, 150), 2000);
    whyWidth = (float)std::min(std::max(p.v99x8.g3dPerfWhyW, 280), 2000);
}

void Geo3DPerfStrip::saveUi(VdpViewerProperties& p) const
{
    p.v99x8.g3dPerf = shown ? 1 : 0;
    p.v99x8.g3dPerfH = (int)(heightPx + 0.5f);
    p.v99x8.g3dPerfWhyW = (int)(whyWidth + 0.5f);
}

void Geo3DPerfStrip::forget()
{
    geo3dPerfClose(perf);
    perf = NULL;
    delete view;
    view = NULL;
    delete detail;
    detail = NULL;
    derivedFor = 0;
    derivedState = -1;
    laneFor = 0;
    hold = SpanHold();
    pinObj = pinRun = 0;
    srcId = 0;
    follow = true;
    selNumber = 0;
    openFailed = false;
    legendSeen = 0;
    legendLast = 0;
}

void Geo3DPerfStrip::disarm()
{
    geo3dPerfArm(perf, false);
}

void Geo3DPerfStrip::keep(const VdpDebugState* s)
{
    UInt64 now = GetTickCount64();
    if (!shown) {
        geo3dPerfArm(perf, false);
        return;
    }
    if (perf == NULL) {
        if (openFailed) return;
        perf = geo3dPerfOpen();
        openFailed = perf == NULL;
        if (perf == NULL) return;
        view = new Geo3DPerfView();
        detail = new Geo3DPerfDetail();
        srcId = 0;
    }
    geo3dPerfArm(perf, true);
    /* The machine's VDP is "VDP"; a cartridge's goes by its own name, with its geo3d "<name> geo3d". */
    if (s->id != srcId || now - srcAt >= 1000) {
        DebugTraceSource src;
        char name[sizeof(src.name)] = "VDP", geo[sizeof(src.name)];
        UInt16 cmd = DEBUG_TRACE_NO_SOURCE, reg = DEBUG_TRACE_NO_SOURCE, g3 = DEBUG_TRACE_NO_SOURCE, cpu = DEBUG_TRACE_NO_SOURCE;
        if (s->secondary) {
            name[0] = 0;
            for (UInt16 i = 1; i < DEBUG_TRACE_SOURCES && debugTraceSourceGet(i, &src); i++) {
                if (src.live && src.classId == DEBUG_TRACE_VDPREG && strcmp(src.name, "VDP") != 0) {
                    strcpy(name, src.name);
                    break;
                }
            }
        }
        /* Cut as the producer's name is, at the source name's size. */
        snprintf(geo, sizeof(geo), "%s geo3d", name);
        for (UInt16 i = 1; name[0] && i < DEBUG_TRACE_SOURCES && debugTraceSourceGet(i, &src); i++) {
            if (!src.live) continue;
            if (src.classId == DEBUG_TRACE_VDPCMD && strcmp(src.name, name) == 0) cmd = i;
            if (src.classId == DEBUG_TRACE_VDPREG && strcmp(src.name, name) == 0) reg = i;
            if (src.classId == DEBUG_TRACE_GEO3D && strcmp(src.name, geo) == 0) g3 = i;
            if (src.classId == DEBUG_TRACE_CPU && strcmp(src.name, "CPU") == 0) cpu = i;
        }
        geo3dPerfSources(perf, cmd, reg, g3, cpu, s->secondary != 0);
        srcId = s->id;
        srcAt = now;
    }
}

void Geo3DPerfStrip::update()
{
    if (perf == NULL || !shown) return;
    if (geo3dPerfTake(perf, view)) {
        /* The pictures start again when none is left of those taken before (a new source or a reset). */
        if (view->count == 0 || view->pic[0].number > legendLast) {
            legendSeen = 0;
            hold = SpanHold();
            pinObj = pinRun = 0;
        }
        for (int i = 0; i < view->count; i++) {
            const Geo3DPerfPicture& p = view->pic[i];
            for (int k = 0; k < G3P_PARTS; k++) {
                if (p.part[k] > 0) legendSeen |= 1u << k;
            }
        }
        if (view->count > 0) legendLast = view->pic[view->count - 1].number;
    }
    if (view->count > 0 && (follow || geo3dPerfIndexOf(view, selNumber) < 0)) {
        follow = true;
        selNumber = view->pic[view->count - 1].number;
    }
    /* Following, the fold hands each latest picture's detail over as it settles. */
    geo3dPerfWantDetail(perf, follow ? G3P_DETAIL_LATEST : selNumber);
    geo3dPerfTakeDetail(perf, detail);
    /* Taken across a rewind, the two disagree on the times until both are new. */
    if (detail->gen != view->gen) detail->state = G3P_DETAIL_WAIT;
}

bool Geo3DPerfStrip::effectiveFps(double* fps, double* ms) const
{
    if (view == NULL || !shown) return false;
    double sum = 0, rate = 0, sumMs = 0;
    int n = 0;
    for (int i = view->count - 1; i >= 0 && n < baseMax; i--) {
        const Geo3DPerfPicture& p = view->pic[i];
        if (p.flags & notWhole) continue;
        if (n == 0) rate = frameRate(p.period);
        sum += p.frames;
        sumMs += ticksMs((double)(p.t1 - p.t0));
        n++;
    }
    if (n == 0 || rate <= 0) return false;
    *fps = rate * n / sum;
    *ms = sumMs / n;
    return true;
}

int Geo3DPerfStrip::selectedIndex() const
{
    if (view == NULL || view->count == 0) return -1;
    return geo3dPerfIndexOf(view, selNumber);
}

float Geo3DPerfStrip::bottomHeight(float room)
{
    /* The bars want 150 px; the tab's card keeps its bar, lines and some picture first. */
    return shell::bottomCardHeight(shown, &heightPx, ui::Px(150), ui::Px(320), room);
}

void Geo3DPerfStrip::bottomCard(float paneH)
{
    /* Folded, it does not measure; the summary says so. */
    const char* summary = !shown ? ts(TS_G3D_PERF_FOLDED) : NULL;
    if (!shell::bottomCardHeader("g3dperf", ts(TS_G3D_PERF), summary, &shown, paneH, 0)) return;
    draw(paneH);
    ui::EndExpander();
}

void Geo3DPerfStrip::draw(float h)
{
    /* An item even so: the caller moved the cursor into the card's body. */
    if (view == NULL) {
        ImGui::Dummy(ImVec2(0, 0));
        return;
    }
    h = floorf(h);
    float wAll = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.y;
    /* Wide: the bars over the lanes, the breakdown beside them; narrower, all three under one
    ** another and scrolled; narrower still, no lanes. */
    bool wide = wAll >= ui::Px(720), lanesOn = wAll >= ui::Px(420);
    float lanesH = lanesOn ? lanesHeight() : 0;
    if (wide && h < lanesH + ui::Px(110) + sp) {
        lanesOn = false;
        lanesH = 0;
    }
    /* The bars keep one height, as the lanes and the breakdown do; a short pane squeezes them. */
    float barsFixed = ui::Px(210);
    float barsH = wide ? std::min(barsFixed, h - (lanesOn ? lanesH + sp : 0)) : barsFixed;
    ImGuiWindowFlags still = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##g3dperfstrip", ImVec2(0, h), ImGuiChildFlags_None, wide ? still : ImGuiWindowFlags_NoBackground);
    if (wide) {
        /* Its left edge drags the breakdown from 280 wide to where the bars keep 360. */
        float gap = ImGui::GetStyle().ItemSpacing.x;
        float whyW = floorf(ui::SplitSizeX("##g3dperfsplit", &whyWidth, ui::Px(280), wAll - gap - ui::Px(360)));
        ImGui::BeginChild("##g3dperfleft", ImVec2(wAll - whyW - gap, h), ImGuiChildFlags_None, still);
        barsCard(std::max(barsH, ui::Px(110)));
        if (lanesOn) lanesCard();
        ImGui::EndChild();
        ImGui::SameLine(0, 0);
        ui::SplitBarX("##g3dperfsplit", gap, h, whyW);
        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##g3dperfwhy", ImVec2(0, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        runsCard();
        ImGui::EndChild();
    }
    else {
        barsCard(barsH);
        if (lanesOn) lanesCard();
        runsCard();
    }
    ImGui::EndChild();
}

/* The card of the bars, h high: the chart takes what its legend row leaves. */
void Geo3DPerfStrip::barsCard(float h)
{
    char sub[160] = "";
    double fps, ms;
    if (view->count == 0) textf(sub, sizeof(sub), "%s", ts(TS_G3D_PERF_SUB_WAIT));
    else if (view->perFrame && view->period == 0) textf(sub, sizeof(sub), "%s", ts(TS_G3D_PERF_SUB_FRAMES_NOMS));
    else if (view->perFrame) textf(sub, sizeof(sub), ts(TS_G3D_PERF_SUB_FRAMES), ticksMs(view->period));
    else if (effectiveFps(&fps, &ms)) textf(sub, sizeof(sub), ts(TS_G3D_PERF_SUB), fps, ms);
    float top = ImGui::GetCursorScreenPos().y;
    ui::BeginCard("g3dperfcard", ts(TS_G3D_PERF_CARD), sub[0] ? sub : NULL);
    float sp = ImGui::GetStyle().ItemSpacing.y;
    float legendH = ImGui::GetFrameHeight();
    float chartH = floorf(top + h - ImGui::GetCursorScreenPos().y - ui::Px(12) - legendH - 2 * sp - ui::Px(2));
    chart(std::max(chartH, ui::Px(24)));
    legend(legendH);
    ui::EndCard();
}

void Geo3DPerfStrip::chart(float h)
{
    const UiTokens& t = uiTokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetCursorScreenPos();
    float w = std::max(1.0f, floorf(ImGui::GetContentRegionAvail().x));
    ImGui::InvisibleButton("##g3dbars", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImVec2 m = ImGui::GetIO().MousePos;
    float lh = ImGui::GetTextLineHeight();
    if (view->count == 0) {
        const char* text = ts(TS_G3D_PERF_EMPTY);
        dl->PushClipRect(a, ImVec2(a.x + w, a.y + h), true);
        dl->AddText(ImVec2(a.x + ui::Px(8), a.y + floorf((h - lh) / 2)), t.textSecondary, text);
        dl->PopClipRect();
        return;
    }
    UInt32 period = view->period ? view->period : view->pic[view->count - 1].period;
    double rate = frameRate(period);
    char label[32], num[16];
    float gutter = ImGui::CalcTextSize("60.0 fps").x + ui::Px(8);
    float x0 = a.x + gutter, x1 = a.x + w, yTop = a.y + lh / 2, yBase = a.y + h - ui::Px(1);
    float bw = std::max(2.0f, ui::Px(5)), gap = std::max(1.0f, ui::Px(1)), gapBig = ui::Px(5);

    /* Newest at the right; a gap before a picture whose predecessor was not seen. */
    float xs[G3P_PICTURES];
    int first = view->count;
    float x = x1;
    for (int i = view->count - 1; i >= 0; i--) {
        x -= bw;
        if (x < x0) break;
        xs[i] = x;
        first = i;
        x -= gap;
        if (view->pic[i].flags & G3P_GAP) x -= gapBig;
    }
    UInt32 most = 0;
    for (int i = first; i < view->count; i++) most = std::max(most, view->pic[i].frames);
    float ym = (float)std::max(4.5, std::min((double)most, 30.0) + 0.5);
    float perFrame = (yBase - yTop) / ym;
    int hot = -1;
    if (hovered) {
        for (int i = first; i < view->count; i++) {
            if (m.x >= xs[i] - gap && m.x < xs[i] + bw + gap) hot = i;
        }
    }

    dl->PushClipRect(a, ImVec2(a.x + w, a.y + h), true);
    /* Dashed targets in frames a picture, named by the rate they give; only those a line apart. */
    static const int targets[] = { 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30 };
    float targetY[sizeof(targets) / sizeof(targets[0])];
    int targetCount = 0;
    float labelY = 1e9f;
    for (int n : targets) {
        float y = floorf(yBase - n * perFrame) + 0.5f;
        if (y < yTop || labelY - y < lh) continue;
        targetY[targetCount++] = y;
        labelY = y;
        if (rate > 0) {
            double f = rate / n;
            snprintf(num, sizeof(num), fabs(f - floor(f + 0.5)) < 0.05 ? "%.0f" : "%.1f", f);
            textf(label, sizeof(label), ts(TS_G3D_PERF_FPS), num);
            dl->AddText(ImVec2(a.x, y - lh / 2), t.textSecondary, label);
        }
    }
    for (int k = 0; k < targetCount; k++) {
        parts::dashedSegment(dl, ImVec2(x0, targetY[k]), ImVec2(x1, targetY[k]), t.textTertiary, 1.0f, ui::Px(4));
    }
    dl->AddLine(ImVec2(x0, yBase), ImVec2(x1, yBase), t.divider);
    int sel = selectedIndex();
    for (int i = first; i < view->count; i++) {
        const Geo3DPerfPicture& p = view->pic[i];
        float bx = xs[i];
        if (i == hot) dl->AddRectFilled(ImVec2(bx - gap, yTop), ImVec2(bx + bw + gap, yBase), t.subtleHover);
        float y = yBase;
        for (int k = 0; k < G3P_PARTS; k++) {
            float ph = (float)partFrames(p, k) * perFrame;
            if (ph <= 0) continue;
            float yt = std::max(yTop, y - ph);
            if (yt < y) dl->AddRectFilled(ImVec2(bx, floorf(yt)), ImVec2(bx + bw, floorf(y)), partColour(k));
            y = yt;
        }
        /* A target a bar passes cuts a notch through it, so tall rows keep the lines. */
        for (int k = 0; k < targetCount; k++) {
            if (targetY[k] > y) dl->AddLine(ImVec2(bx, targetY[k]), ImVec2(bx + bw, targetY[k]), t.cardFill);
        }
        if (p.flags & (G3P_CUT | G3P_DROPPED)) {
            parts::hatch(dl, ImVec2(bx, floorf(y)), ImVec2(bx + bw, yBase), ui::WithAlpha(t.text, 0x90));
        }
        if ((float)p.frames > ym) {
            /* Taller than the chart: a notch at the top says so. */
            dl->AddTriangleFilled(ImVec2(bx, yTop), ImVec2(bx + bw, yTop), ImVec2(bx + bw / 2, yTop - lh / 3), t.text);
        }
        if (i == sel) {
            float o = std::max(1.0f, ui::Px(1));
            dl->AddRect(ImVec2(bx - o, floorf(y) - o), ImVec2(bx + bw + o, yBase + o), t.accent, 0, ui::Px(2));
        }
    }
    dl->PopClipRect();
    if (hot >= 0) {
        tooltip(hot);
        if (clicked) {
            follow = false;
            selNumber = view->pic[hot].number;
            pinObj = pinRun = 0;
        }
    }
}

void Geo3DPerfStrip::picLabel(UInt32 number, char* out, size_t size) const
{
    int i = view != NULL ? geo3dPerfIndexOf(view, number) : -1;
    const Geo3DPerfPicture* p = i >= 0 ? &view->pic[i] : NULL;
    if (p == NULL || p->frameLo == 0 || p->frames == 0) textf(out, size, "%s", ts(TS_G3D_PERF_PIC));
    else if (p->frames == 1) textf(out, size, ts(TS_G3D_PERF_PIC_ONE), p->frameLo);
    else textf(out, size, ts(TS_G3D_PERF_PIC_RANGE), p->frameLo, p->frameLo + p->frames - 1);
}

void Geo3DPerfStrip::tooltip(int i)
{
    const UiTokens& t = uiTokens();
    const Geo3DPerfPicture& p = view->pic[i];
    char text[256], dur[48];
    double ms = ticksMs((double)(p.t1 - p.t0));
    ImGui::BeginTooltip();
    char label[96];
    picLabel(p.number, label, sizeof(label));
    if (p.page >= 0) textf(text, sizeof(text), ts(TS_G3D_PERF_TIP_HEAD), label, ms, p.frames, p.page);
    else textf(text, sizeof(text), ts(TS_G3D_PERF_TIP_HEAD_NOPAGE), label, ms, p.frames);
    ImGui::TextUnformatted(text);
    float lh = ImGui::GetTextLineHeight();
    for (int k = rankCount - 1; k >= 0; k--) {
        ImVec2 c = ImGui::GetCursorScreenPos();
        float sw = floorf(lh * 0.7f), in = floorf((lh - sw) / 2);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x, c.y + in), ImVec2(c.x + sw, c.y + in + sw), partColour(rankPart(k)));
        ImGui::Dummy(ImVec2(sw, lh));
        ImGui::SameLine();
        ticksText(dur, sizeof(dur), rankTicks(p, k));
        textf(text, sizeof(text), ts(TS_G3D_PERF_TIP_PART), partName(rankPart(k)), dur);
        ImGui::TextUnformatted(text);
    }
    textf(text, sizeof(text), ts(TS_G3D_PERF_BYTES), p.vramWritten, p.vramRead, p.feedModel + p.feedOther, p.feedModel);
    ImGui::TextUnformatted(text);
    textf(text, sizeof(text), ts(TS_G3D_PERF_TIP_CMDS), p.cmdsGeo, p.cmdsZ80, p.runs);
    ImGui::TextUnformatted(text);
    ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    if (p.flags & G3P_CUT) ImGui::TextWrapped("%s", ts(TS_G3D_PERF_TIP_CUT));
    if (p.flags & G3P_DROPPED) ImGui::TextWrapped("%s", ts(TS_G3D_PERF_TIP_DROPPED));
    if (p.flags & G3P_NO_FLIP) ImGui::TextWrapped("%s", ts(TS_G3D_PERF_TIP_NO_FLIP));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

/* One row, rowH high, as the tab's other legend: a swatch per part a kept picture had, split
** where its lighter tint came too. Entries past the edge go under "...". */
void Geo3DPerfStrip::legend(float rowH)
{
    const UiTokens& t = uiTokens();
    float lh = ImGui::GetTextLineHeight(), w = std::max(1.0f, floorf(ImGui::GetContentRegionAvail().x));
    const char* latest = ts(TS_G3D_PERF_LATEST);
    float bw = follow ? 0 : ImGui::CalcTextSize(latest).x + ImGui::GetStyle().FramePadding.x * 2;
    float sw = floorf(lh * 0.7f), in = floorf((lh - sw) / 2), pad = ui::Px(4), space = ui::Px(14);
    ImVec2 at = ImGui::GetCursorScreenPos();
    float y = at.y + floorf((rowH - lh) / 2), end = at.x + w - (bw > 0 ? bw + ui::Px(8) : 0);
    int list[legendEntries], n = 0;
    for (int e = 0; e < legendEntries; e++) {
        if (legendSeen & legendBits(e)) list[n++] = e;
    }
    auto split = [&](int e) { return e >= G3P_IRQ && e <= G3P_OTHER && ((legendSeen >> G3P_TAIL_OF(e)) & 1); };
    float ex[legendEntries], right[legendEntries], x = at.x;
    for (int q = 0; q < n; q++) {
        ex[q] = x;
        right[q] = x + sw + pad + ImGui::CalcTextSize(legendLabel(list[q])).x;
        x = right[q] + space;
    }
    /* Short of room, as many as leave room for the "..." after them. */
    const char* more = "...";
    int fit = n;
    if (n > 0 && right[n - 1] > end) {
        float moreW = ImGui::CalcTextSize(more).x;
        fit = 0;
        while (fit < n && right[fit] + space + moreW <= end) fit++;
    }
    /* The row's item comes after the button, so the card grows by an item and not by a moved cursor. */
    if (bw > 0) {
        ImGui::SetCursorScreenPos(ImVec2(at.x + w - bw, at.y));
        if (ui::SubtleButton(latest, false, ImVec2(bw, rowH))) {
            follow = true;
            pinObj = pinRun = 0;
        }
        ImGui::SetCursorScreenPos(at);
    }
    ImGui::Dummy(ImVec2(w, rowH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hovering = ImGui::IsWindowHovered();
    int hot = -1;
    dl->PushClipRect(ImVec2(at.x, y), ImVec2(std::max(end, at.x), y + lh), true);
    for (int q = 0; q < fit; q++) {
        legendSwatch(dl, ImVec2(ex[q], y + in), ImVec2(ex[q] + sw, y + in + sw), list[q], split(list[q]));
        dl->AddText(ImVec2(ex[q] + sw + pad, y), t.textSecondary, legendLabel(list[q]));
        if (hovering && ImGui::IsMouseHoveringRect(ImVec2(ex[q], y), ImVec2(right[q], y + lh))) hot = list[q];
    }
    bool moreHot = false;
    if (fit < n && (fit > 0 || end >= at.x + ImGui::CalcTextSize(more).x)) {
        float mx = fit > 0 ? right[fit - 1] + space : at.x;
        dl->AddText(ImVec2(mx, y), t.textSecondary, more);
        moreHot = hovering && ImGui::IsMouseHoveringRect(ImVec2(mx, y), ImVec2(mx + ImGui::CalcTextSize(more).x, y + lh));
    }
    dl->PopClipRect();
    if (hot >= 0) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::TextUnformatted(hot == G3P_IRQ ? ts(TS_G3D_PERF_PART_IRQ_TIP) : hot == G3P_HALT ? ts(TS_G3D_PERF_PART_HALT_TIP) :
                               hot == G3P_OTHER ? ts(TS_G3D_PERF_PART_OTHER_TIP) : partName(hot));
        if (split(hot)) ImGui::TextUnformatted(ts(TS_G3D_PERF_LEG_TAIL_TIP));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (moreHot) {
        ImGui::BeginTooltip();
        for (int q = fit; q < n; q++) {
            ImVec2 c = ImGui::GetCursorScreenPos();
            legendSwatch(ImGui::GetWindowDrawList(), ImVec2(c.x, c.y + in), ImVec2(c.x + sw, c.y + in + sw), list[q], split(list[q]));
            ImGui::Dummy(ImVec2(sw, lh));
            ImGui::SameLine();
            ImGui::TextUnformatted(legendLabel(list[q]));
        }
        ImGui::EndTooltip();
    }
}

/* What made the selected picture slower than the whole ones before it: the part most above
** their average, once it is longer than their average by a quarter of a frame. tip: why it
** was not compared, when it was not. */
void Geo3DPerfStrip::whyText(int i, char* text, size_t size, char* tip, size_t tipSize) const
{
    text[0] = tip[0] = 0;
    if (i >= 0) {
        const Geo3DPerfPicture& p = view->pic[i];
        double mean[rankCount] = {}, meanMs = 0, ms = ticksMs((double)(p.t1 - p.t0));
        int n = 0;
        for (int j = i - 1; j >= 0 && n < baseMax; j--) {
            const Geo3DPerfPicture& b = view->pic[j];
            if (b.flags & notWhole) continue;
            for (int k = 0; k < rankCount; k++) mean[k] += ticksMs(rankTicks(b, k));
            meanMs += ticksMs((double)(b.t1 - b.t0));
            n++;
        }
        /* Not compared: the time alone, the reason in the tooltip. */
        if (p.flags & (G3P_CUT | G3P_DROPPED | G3P_NO_FLIP) || n == 0) {
            textf(text, size, ts(TS_G3D_PERF_MS1), ms);
            textf(tip, tipSize, "%s", ts(p.flags & (G3P_CUT | G3P_DROPPED) ? TS_G3D_PERF_WHY_CUT :
                                          p.flags & G3P_NO_FLIP ? TS_G3D_PERF_WHY_NO_FLIP : TS_G3D_PERF_WHY_NOBASE));
        }
        else {
            int best = 0;
            double most = -1e9;
            for (int k = 0; k < rankCount; k++) {
                double d = ticksMs(rankTicks(p, k)) - mean[k] / n;
                if (d > most) {
                    most = d;
                    best = k;
                }
            }
            if (ms <= meanMs / n + 0.25 * ticksMs(p.period)) {
                textf(text, size, ts(TS_G3D_PERF_WHY_AVG), ms, meanMs / n);
            }
            else {
                textf(text, size, ts(TS_G3D_PERF_WHY_SLOW), ms, meanMs / n, partName(rankPart(best)), most);
            }
        }
    }
}

namespace {

const int laneRows = 3;
const UInt8 cmLrmm = 3;

float laneRowH()
{
    return floorf(std::max(ImGui::GetTextLineHeight() + ui::Px(6), ui::Px(20)));
}

/* A command's colour; LRMM, which reads a texture, darker. */
ImU32 spanColour(const Geo3DPerfSpan& sp)
{
    ImU32 c = partColour(sp.kind == G3P_SPAN_GEO ? G3P_GEO : sp.kind == G3P_SPAN_VDP ? G3P_VDP :
                         sp.kind == G3P_SPAN_IRQ ? G3P_IRQ : sp.kind == G3P_SPAN_HALT ? G3P_HALT : G3P_RUN);
    return sp.cm == cmLrmm && sp.kind != G3P_SPAN_RUN ? ui::Over(IM_COL32(0, 0, 0, 100), c) : c;
}

}

UInt32 Geo3DPerfStrip::litRun() const
{
    return view != NULL && shown ? pinRun : 0;
}

/* The picture the lanes and the breakdown show: the selected one, or while following the
** one before it until the new one's detail comes, so nothing flickers. */
int Geo3DPerfStrip::shownIndex() const
{
    int i = selectedIndex();
    if (i < 0 || detail == NULL || detail->number == selNumber || !follow || detail->state != G3P_DETAIL_OK) return i;
    int j = geo3dPerfIndexOf(view, detail->number);
    return j >= 0 ? j : i;
}

/* What the selected picture's detail gives: when the devices were busy, and the RUNs by VDP time. */
void Geo3DPerfStrip::derive()
{
    int i = shownIndex();
    pinRun = 0;
    if (i < 0 || detail == NULL || detail->number != view->pic[i].number) {
        busy.clear();
        runRows.clear();
        derivedFor = 0;
        return;
    }
    if (derivedFor != detail->number || derivedState != detail->state) deriveFrom(view->pic[i]);
    /* The pin follows its object into each picture: the RUN of it with the most time. */
    for (const RunRow& r : runRows) {
        if (r.obj != 0 && r.obj == pinObj) {
            pinRun = r.run;
            break;
        }
    }
}

const char* Geo3DPerfStrip::detailNote(int i) const
{
    if (i < 0 || detail == NULL) return " ";
    bool here = detail->number == view->pic[i].number;
    if (here && detail->state == G3P_DETAIL_OK) return NULL;
    /* Stopped, nothing hands it over any more. */
    if ((here && detail->state == G3P_DETAIL_GONE) || emulatorGetState() == EMU_STOPPED) return ts(TS_G3D_PERF_DETAIL_GONE);
    return ts(TS_G3D_PERF_DETAIL_WAIT);
}

void Geo3DPerfStrip::deriveFrom(const Geo3DPerfPicture& p)
{
    derivedFor = detail->number;
    derivedState = detail->state;
    busy.clear();
    runRows.clear();
    if (detail->state != G3P_DETAIL_OK) return;
    std::vector<std::pair<UInt64, UInt64>> b;
    for (int k = 0; k < detail->count; k++) {
        const Geo3DPerfSpan& sp = detail->span[k];
        if (sp.kind <= G3P_SPAN_RUN) {
            UInt64 s = std::max(sp.s, p.t0), e = std::min(sp.e, p.t1);
            if (e > s) b.push_back(std::make_pair(s, e));
        }
        /* Time within the picture, as the bar's part counts it; commands and dots of those begun in it. */
        if (sp.kind == G3P_SPAN_GEO && sp.run != 0 && sp.e > p.t0 && sp.s < p.t1) {
            RunRow* r = NULL;
            for (RunRow& x : runRows) if (x.run == sp.run) r = &x;
            if (r == NULL) {
                runRows.push_back(RunRow());
                r = &runRows.back();
                memset(r, 0, sizeof(*r));
                r->run = sp.run;
                r->obj = sp.obj;
            }
            r->fill += std::min(sp.e, p.t1) - std::max(sp.s, p.t0);
            if (sp.s >= p.t0) {
                r->cmds++;
                r->dots += sp.dots;
                if (sp.cm == cmLrmm) r->lrmm += sp.dots;
            }
        }
    }
    std::sort(b.begin(), b.end());
    for (const auto& x : b) {
        if (!busy.empty() && x.first <= busy.back()) busy.back() = std::max(busy.back(), x.second);
        else {
            busy.push_back(x.first);
            busy.push_back(x.second);
        }
    }
    std::sort(runRows.begin(), runRows.end(), [](const RunRow& x, const RunRow& y) { return x.fill > y.fill; });
}

float Geo3DPerfStrip::lanesHeight() const
{
    float sp = ImGui::GetStyle().ItemSpacing.y, lh = ImGui::GetTextLineHeight();
    return floorf(ui::Px(24) + lh * 1.25f + sp * 2 + laneRows * laneRowH() + lh + ui::Px(4));
}

void Geo3DPerfStrip::lanesCard()
{
    char title[160], label[96];
    int i = shownIndex();
    picLabel(i >= 0 ? view->pic[i].number : selNumber, label, sizeof(label));
    textf(title, sizeof(title), ts(TS_G3D_PERF_LANES), label);
    ui::BeginCard("g3dlanes", title, ts(TS_G3D_PERF_LANES_SUB));
    lanes(laneRows * laneRowH() + ImGui::GetTextLineHeight() + ui::Px(4));
    ui::EndCard();
}

double Geo3DPerfStrip::laneSpan(const Geo3DPerfPicture& p, float widthPx)
{
    double len = (double)(p.t1 - p.t0);
    if (!follow) return len;
    bool running = emulatorGetState() == EMU_RUNNING || emulatorGetState() == EMU_SUSPENDED;
    double period = p.period ? p.period : view->period ? view->period : boardFrequency() / 100.0;
    if (!(period > 0)) return len;
    /* Up to 2% over k periods counts as k, its flip landing a little late: the span is
    ** k periods, or the picture itself when it runs that little past them. */
    double fit = std::max(len, std::max(1.0, ceil(len / (period * 1.02))) * period);
    return std::max(len, hold.step(fit, running && (p.flags & notWhole) == 0, running, std::max(widthPx, 1.0f)));
}

/* The selected picture over time: what the CPU did, geo3d's RUNs and the VDP's commands,
** a thin line at each screen refresh and an axis in ms. */
void Geo3DPerfStrip::lanes(float h)
{
    const UiTokens& t = uiTokens();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetCursorScreenPos();
    float w = std::max(1.0f, floorf(ImGui::GetContentRegionAvail().x)), lh = ImGui::GetTextLineHeight();
    ImGui::InvisibleButton("##g3dlanes", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    int i = shownIndex();
    derive();
    const char* note = detailNote(i);
    if (i >= 0 && note == NULL && view->pic[i].t1 <= view->pic[i].t0) note = " ";
    if (note) {
        dl->AddText(ImVec2(a.x, a.y + floorf((h - lh) / 2)), t.textSecondary, note);
        /* The card was drawn, so the hold is not stale next frame; only its stamp moves. */
        hold.touch();
        return;
    }
    /* The wheel is the lanes' only while they show something to zoom. */
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const Geo3DPerfPicture& p = view->pic[i];
    float gw = ImGui::CalcTextSize("geo3d").x + ui::Px(10), x0 = a.x + gw, x1 = a.x + w, rh = laneRowH();
    double full = laneSpan(p, x1 - x0);
    if (laneFor != p.number || laneHi <= laneLo) {
        laneFor = p.number;
        laneAuto = true;
    }
    /* The held span eases between pictures too, so an unzoomed window follows it every frame. */
    if (laneAuto) {
        laneLo = 0;
        laneHi = full;
    }
    double span = laneHi - laneLo;
    /* Wheel zooms about the mouse, a drag moves, a double click shows the whole picture. */
    if (hovered && io.MouseWheel != 0 && x1 > x0) {
        double at = laneLo + (io.MousePos.x - x0) / (x1 - x0) * span;
        double next = std::min(std::max(span * pow(0.8, io.MouseWheel), std::min(full, 200.0)), full);
        laneLo = at - (at - laneLo) * next / span;
        laneHi = laneLo + next;
        laneAuto = false;
    }
    if (ImGui::IsItemActive() && io.MouseDelta.x != 0 && x1 > x0) {
        double d = -io.MouseDelta.x / (x1 - x0) * span;
        laneLo += d;
        laneHi += d;
        laneAuto = false;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        laneLo = 0;
        laneHi = full;
        laneAuto = true;
    }
    if (laneLo < 0) { laneHi -= laneLo; laneLo = 0; }
    if (laneHi > full) { laneLo = std::max(0.0, laneLo - (laneHi - full)); laneHi = full; }
    span = laneHi - laneLo;
    if (hovered) ui::StatusText("%s", ts(TS_G3D_PERF_LANES_HINT));
    auto toX = [&](UInt64 tk) {
        return (float)(x0 + ((double)(Int64)(tk - p.t0) - laneLo) / span * (x1 - x0));
    };
    float inset = ui::Px(2);
    auto row = [&](int r, float* y0, float* y1) {
        *y0 = a.y + r * rh + inset;
        *y1 = a.y + (r + 1) * rh - inset;
    };
    const char* rowNames[laneRows] = { "CPU", "geo3d", "VDP" };
    /* Past the picture's end, when the held span is longer, hatched faintly: not idle time. */
    float xEnd = std::min(std::max(toX(p.t1), x0), x1);
    for (int r = 0; r < laneRows; r++) {
        float y0, y1;
        row(r, &y0, &y1);
        dl->AddText(ImVec2(a.x, floorf((y0 + y1 - lh) / 2)), t.textSecondary, rowNames[r]);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(xEnd, y1), ui::WithAlpha(t.text, 0x0C));
        if (xEnd < x1) parts::hatch(dl, ImVec2(xEnd, y0), ImVec2(x1, y1), ui::WithAlpha(t.text, 0x18));
    }
    dl->PushClipRect(ImVec2(x0, a.y), ImVec2(x1, a.y + h), true);
    auto bar = [&](int r, UInt64 s, UInt64 e, ImU32 col) {
        float y0, y1, xa = toX(s), xb = toX(e);
        row(r, &y0, &y1);
        if (xb < x0 || xa > x1) return;
        if (xb - xa < 1) xb = xa + 1;
        dl->AddRectFilled(ImVec2(floorf(xa), y0), ImVec2(floorf(xb), y1), col);
    };
    /* The CPU by what it did, OTHER under its halts and those under its handlers, each lighter
    ** after the last VDP or RUN end as in the bar; polls in light grey. */
    UInt64 last = p.t0 + p.busyEnd;
    auto cpuBar = [&](UInt64 s, UInt64 e, int part) {
        s = std::max(s, p.t0);
        e = std::min(e, p.t1);
        if (s < last && s < e) bar(0, s, std::min(e, last), partColour(part));
        if (e > last && s < e) bar(0, std::max(s, last), e, partColour(G3P_TAIL_OF(part)));
    };
    cpuBar(p.t0, p.t1, G3P_OTHER);
    for (int k = 0; k < detail->count; k++) {
        const Geo3DPerfSpan& sp = detail->span[k];
        if (sp.kind != G3P_SPAN_HALT || sp.s >= p.t1) continue;
        cpuBar(sp.s, sp.e, G3P_HALT);
    }
    for (int k = 0; k < detail->count; k++) {
        const Geo3DPerfSpan& sp = detail->span[k];
        if (sp.kind == G3P_SPAN_IRQ) cpuBar(sp.s, sp.e, G3P_IRQ);
        else if (sp.s >= p.t1) continue;
        else if (sp.kind == G3P_SPAN_RUN) bar(1, sp.s, std::min(sp.e, p.t1), spanColour(sp));
        else if (sp.kind <= G3P_SPAN_VDP) bar(2, sp.s, std::min(sp.e, p.t1), spanColour(sp));
    }
    for (int k = 0; k < detail->count; k++) {
        const Geo3DPerfSpan& sp = detail->span[k];
        if ((sp.kind != G3P_SPAN_WAIT_CE && sp.kind != G3P_SPAN_WAIT_RUN) || sp.s >= p.t1) continue;
        UInt64 e = std::min(sp.e, p.t1);
        float y0, y1, xa = toX(sp.s), xb = std::max(toX(e), toX(sp.s) + 1);
        row(0, &y0, &y1);
        dl->AddRectFilled(ImVec2(xa, y0), ImVec2(xb, y1), ui::Over(ui::WithAlpha(t.text, 0x20), ui::Over(t.cardFill, t.layer)));
    }
    /* Thin lines at the screen refreshes; under the rows, ms from the picture's start in steps
    ** of 1, 2 or 5 times a power of ten, some 70 px apart at the zoom. */
    for (UInt64 tk = p.t0 + p.vsync0; p.period != 0 && tk <= p.t1; tk += p.period) {
        float x = floorf(toX(tk)) + 0.5f;
        if (x < x0 - 1 || x > x1 + 1) continue;
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, a.y + laneRows * rh), t.textTertiary);
    }
    if (x1 <= x0) {
        dl->PopClipRect();
        return;
    }
    double msLo = ticksMs(laneLo), msHi = ticksMs(laneHi), pxPerMs = (x1 - x0) / std::max(msHi - msLo, 1e-9);
    /* The step is kept while its spacing stays within 0.6 to 2.2 times the aim, so it does not flip
    ** back and forth as the span eases; once out, the 1-2-5 step nearest the aim is taken. */
    double aim = ui::Px(70), gap = tickStep * pxPerMs;
    if (!(tickStep > 0) || gap < aim * 0.6 || gap > aim * 2.2) {
        double want = aim / pxPerMs, base = pow(10.0, floor(log10(want))), best = 0;
        for (double m : { 1.0, 2.0, 5.0, 10.0 })
            if (best == 0 || fabs(log(base * m / want)) < fabs(log(best / want))) best = base * m;
        tickStep = best;
    }
    double step = tickStep;
    int decimals = step >= 1 ? 0 : (int)ceil(-log10(step) - 1e-9);
    float yAxis = a.y + laneRows * rh, nameEnd = -1e9f;
    char num[24], name[48];
    for (double n = ceil(msLo / step - 1e-9); n * step <= msHi + 1e-9; n++) {
        float x = floorf((float)(x0 + (n * step - msLo) * pxPerMs)) + 0.5f;
        dl->AddLine(ImVec2(x, yAxis), ImVec2(x, yAxis + ui::Px(3)), t.textSecondary);
        /* ceil of a hair below 0 is -0, which would print as "-0". */
        double v = fabs(n * step) < step * 1e-6 ? 0.0 : n * step;
        snprintf(num, sizeof(num), "%.*f", decimals, v);
        textf(name, sizeof(name), ts(TS_G3D_PERF_AXIS_MS), num);
        if (x + ui::Px(3) > nameEnd) {
            dl->AddText(ImVec2(x + ui::Px(3), yAxis + ui::Px(2)), t.textSecondary, name);
            nameEnd = x + ui::Px(3) + ImGui::CalcTextSize(name).x + ui::Px(8);
        }
    }
    dl->PopClipRect();
    /* The narrowest span under the mouse, so a short command is found over a long one. */
    if (hovered && io.MousePos.x >= x0) {
        int r = (int)((io.MousePos.y - a.y) / rh), hit = -1;
        float best = 1e9f;
        for (int k = 0; r >= 0 && r < laneRows && k < detail->count; k++) {
            const Geo3DPerfSpan& sp = detail->span[k];
            int sr = sp.kind == G3P_SPAN_RUN ? 1 : sp.kind <= G3P_SPAN_VDP ? 2 : 0;
            if (sp.s >= p.t1) continue;
            float xa = toX(sp.s), xb = std::max(toX(std::min(sp.e, p.t1)), xa + 1);
            if (sr == r && io.MousePos.x >= xa - 1 && io.MousePos.x <= xb + 1 && xb - xa < best) {
                best = xb - xa;
                hit = k;
            }
        }
        if (hit >= 0) {
            const Geo3DPerfSpan& sp = detail->span[hit];
            float y0, y1;
            row(r, &y0, &y1);
            float xa = toX(sp.s), xb = std::max(toX(std::min(sp.e, p.t1)), xa + 1);
            dl->AddRect(ImVec2(floorf(xa) - 1, y0 - 1), ImVec2(floorf(xb) + 1, y1 + 1), t.text, 0, ui::Px(1));
            spanTip(sp);
        }
    }
}

void Geo3DPerfStrip::spanTip(const Geo3DPerfSpan& sp)
{
    char text[256], dur[48];
    ticksText(dur, sizeof(dur), (double)(sp.e - sp.s));
    ImGui::BeginTooltip();
    if (sp.kind == G3P_SPAN_GEO && sp.run != 0) textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_CMD_RUN), vdpCmdName(sp.cm, true), sp.run, sp.arg + 1);
    else if (sp.kind == G3P_SPAN_GEO) textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_CMD_GEO), vdpCmdName(sp.cm, true));
    else if (sp.kind == G3P_SPAN_VDP) textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_CMD_Z80), vdpCmdName(sp.cm, true));
    else if (sp.kind == G3P_SPAN_RUN && sp.cut) textf(text, sizeof(text), ts(TS_G3D_PERF_RUN_ROW), sp.run);
    else if (sp.kind == G3P_SPAN_RUN) textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_RUN), sp.run, sp.arg);
    else if (sp.kind == G3P_SPAN_IRQ) textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_IRQ), sp.arg & 0xFFFF);
    else if (sp.kind == G3P_SPAN_HALT) textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_HALT), sp.arg & 0xFFFF);
    else if (sp.cut) textf(text, sizeof(text), "%s", ts(sp.kind == G3P_SPAN_WAIT_CE ? TS_G3D_PERF_SPAN_WAIT_CE_OPEN : TS_G3D_PERF_SPAN_WAIT_RUN_OPEN));
    else if (sp.kind == G3P_SPAN_WAIT_CE) textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_WAIT_CE), sp.arg);
    else textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_WAIT_RUN), sp.arg);
    ImGui::TextUnformatted(text);
    if (sp.kind <= G3P_SPAN_VDP && !sp.cut) {
        textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_SIZE), (unsigned)sp.nx, (unsigned)sp.ny, (unsigned)sp.dx, (unsigned)sp.dy, sp.dots);
        ImGui::TextUnformatted(text);
    }
    textf(text, sizeof(text), ts(TS_G3D_PERF_SPAN_TIME), dur);
    ImGui::TextUnformatted(text);
    ImVec4 grey = ImGui::ColorConvertU32ToFloat4(uiTokens().textSecondary);
    if (sp.aborted) ImGui::TextColored(grey, "%s", ts(TS_G3D_PERF_SPAN_ABORTED));
    if (sp.cut) ImGui::TextColored(grey, "%s", ts(TS_G3D_PERF_SPAN_CUT));
    else if (sp.kind == G3P_SPAN_WAIT_CE || sp.kind == G3P_SPAN_WAIT_RUN) ImGui::TextColored(grey, "%s", ts(TS_G3D_PERF_SPAN_WAIT_TIP));
    if (sp.kind == G3P_SPAN_IRQ || sp.kind == G3P_SPAN_HALT) {
        ImGui::PushStyleColor(ImGuiCol_Text, uiTokens().textSecondary);
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::TextWrapped("%s", ts(sp.kind == G3P_SPAN_IRQ ? TS_G3D_PERF_PART_IRQ_TIP : TS_G3D_PERF_PART_HALT_TIP));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndTooltip();
}

/* The why of the selected picture, and its RUNs by the VDP time their commands took, each
** against an outline of the same RUN of the pictures before it on average. */
void Geo3DPerfStrip::runsCard()
{
    const UiTokens& t = uiTokens();
    char title[160], label[96], text[512];
    int i = shownIndex();
    picLabel(i >= 0 ? view->pic[i].number : selNumber, label, sizeof(label));
    textf(title, sizeof(title), ts(TS_G3D_PERF_WHY_CARD), label);
    ui::BeginCard("g3dwhy", title);
    /* The lines above the RUN bars come and go by picture; held, the bars hold still. */
    float y0 = ImGui::GetCursorPosY();
    char tip[512];
    whyText(i, text, sizeof(text), tip, sizeof(tip));
    ui::TextFitItem(text[0] ? text : " ", 0, true, tip[0] ? tip : NULL);
    if (i >= 0) {
        const Geo3DPerfPicture& b = view->pic[i];
        textf(text, sizeof(text), ts(TS_G3D_PERF_BYTES_ROW), b.vramWritten, b.vramRead, b.feedModel + b.feedOther);
        textf(tip, sizeof(tip), ts(TS_G3D_PERF_BYTES), b.vramWritten, b.vramRead, b.feedModel + b.feedOther, b.feedModel);
        ui::TextFitItem(text, t.textSecondary, true, tip);
    }
    else ui::TextFitItem(" ", 0, false);
    derive();
    if (const char* note = detailNote(i)) {
        ui::TextFitItem(note, t.textSecondary);
        ui::HoldHeight(textHold, y0);
        ui::HoldHeight(bodyHold, y0);
        ui::EndCard();
        return;
    }
    const Geo3DPerfPicture& p = view->pic[i];
    if (runRows.empty()) {
        ui::TextFitItem(ts(TS_G3D_PERF_RUNS_NONE), t.textSecondary);
        ui::HoldHeight(textHold, y0);
        ui::HoldHeight(bodyHold, y0);
        ui::EndCard();
        return;
    }
    ui::HoldHeight(textHold, y0);
    /* One line either way, so the bars below hold still: a cut list says so in the heading. */
    if (p.flags & G3P_DETAIL_CUT) {
        int hl = textf(text, sizeof(text), "%s \xC2\xB7 ", ts(TS_G3D_PERF_RUNS_HEAD));
        if (hl > 0 && hl < (int)sizeof(text)) textf(text + hl, sizeof(text) - hl, ts(TS_G3D_PERF_DETAIL_CUT), G3P_DETAIL_MAX);
        ui::TextFitItem(text, t.caution, true, ts(TS_G3D_PERF_RUNS_HEAD_TIP));
    }
    else ui::TextFitItem(ts(TS_G3D_PERF_RUNS_HEAD), t.textSecondary, true, ts(TS_G3D_PERF_RUNS_HEAD_TIP));
    /* A RUN of the same object, on average over the whole pictures before that drew it. */
    int rows = std::min((int)runRows.size(), 6);
    double ghost[6] = {}, most = 1;
    for (int r = 0; r < rows; r++) {
        double fill = 0, runs = 0;
        int n = 0;
        for (int j = i - 1; j >= 0 && n < baseMax && runRows[r].obj != 0; j--) {
            const Geo3DPerfPicture& b = view->pic[j];
            if (b.flags & notWhole) continue;
            for (UInt32 k = 0; k < b.objCount; k++) {
                if (b.objs[k].obj != runRows[r].obj || b.objs[k].runs == 0) continue;
                fill += b.objs[k].fill;
                runs += b.objs[k].runs;
                n++;
            }
        }
        ghost[r] = runs > 0 ? fill / runs : 0;
        most = std::max(most, std::max(ghost[r], (double)runRows[r].fill));
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lh = ImGui::GetTextLineHeight(), w = std::max(1.0f, floorf(ImGui::GetContentRegionAvail().x));
    char rowLabel[64];
    textf(rowLabel, sizeof(rowLabel), ts(TS_G3D_PERF_RUN_ROW), 99999u);
    float labelW = ImGui::CalcTextSize(rowLabel).x;
    float barH = floorf(lh * 0.8f), rowH = lh * 2 + ui::Px(8);
    for (int r = 0; r < rows; r++) {
        const RunRow& rr = runRows[r];
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID((int)rr.run);
        ImGui::InvisibleButton("##run", ImVec2(w, rowH));
        bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) pinObj = pinObj == rr.obj ? 0 : rr.obj;
        ImGui::PopID();
        bool pinned = rr.obj != 0 && pinObj == rr.obj;
        if (hov || pinned) dl->AddRectFilled(at, ImVec2(at.x + w, at.y + rowH), hov ? t.subtleHover : t.subtleSelected, ui::Px(4));
        if (pinned) dl->AddRect(at, ImVec2(at.x + w, at.y + rowH), t.accent, ui::Px(4), ui::Px(1));
        char label[48], dur[48];
        textf(label, sizeof(label), ts(TS_G3D_PERF_RUN_ROW), rr.run);
        dl->AddText(ImVec2(at.x + ui::Px(4), at.y + ui::Px(4)), t.text, label);
        float bx0 = at.x + ui::Px(4) + labelW, bx1 = at.x + w - ui::Px(4), by0 = at.y + ui::Px(4) + floorf((lh - barH) / 2);
        float scale = (bx1 - bx0) / (float)most;
        if (bx1 > bx0) {
            dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx0 + std::max(1.0f, rr.fill * scale), by0 + barH), partColour(G3P_GEO));
            if (rr.lrmm > 0 && rr.dots > 0) {
                /* The share of its dots LRMM drew, darker at the bar's start. */
                float lw = rr.fill * scale * rr.lrmm / rr.dots;
                dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx0 + lw, by0 + barH), ui::Over(IM_COL32(0, 0, 0, 100), partColour(G3P_GEO)));
            }
            if (ghost[r] > 0) {
                float gx = bx0 + (float)(ghost[r] * scale);
                dl->AddRect(ImVec2(bx0 - 1, by0 - 1), ImVec2(gx + 1, by0 + barH + 1), t.text, 0, ui::Px(1));
            }
        }
        ticksText(dur, sizeof(dur), (double)rr.fill);
        if (rr.lrmm > 0) textf(text, sizeof(text), ts(TS_G3D_PERF_RUN_VALUE_LRMM), dur, rr.dots, rr.cmds, rr.lrmm);
        else textf(text, sizeof(text), ts(TS_G3D_PERF_RUN_VALUE), dur, rr.dots, rr.cmds);
        ui::TextFit(dl, ImVec2(at.x + ui::Px(4), at.y + ui::Px(4) + lh), bx1, text, t.textSecondary, hov);
        if (hov) {
            char g[48];
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
            if (rr.obj != 0 && (rr.obj >> 16) != 0xFFFF) {
                textf(text, sizeof(text), ts(TS_G3D_PERF_RUN_OBJ), rr.obj & 0xFFFF, rr.obj >> 16);
                ImGui::TextWrapped("%s", text);
            }
            if (ghost[r] > 0) {
                ticksText(g, sizeof(g), ghost[r]);
                textf(text, sizeof(text), ts(TS_G3D_PERF_RUN_GHOST_TIP), g);
                ImGui::TextWrapped("%s", text);
            }
            ImGui::TextWrapped("%s", ts(TS_G3D_PERF_RUN_PIN_TIP));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ui::HoldHeight(bodyHold, y0);
    ui::EndCard();
}
