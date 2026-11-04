/*****************************************************************************
**
** UI parts of the VDP Viewer shared by every chip family.
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
#include <float.h>
#include <math.h>
#include <stdio.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiTheme.h"
#include "VdpParts.h"

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace parts {

void barToggle(const char* id, const char* glyph, const char* label, const char* tip, bool* on)
{
    ui::CommandBarItem(ui::IconButtonWidth(glyph, label), ui::Px(24));
    if (ui::IconButton(id, glyph, label, tip, *on)) *on = !*on;
}

void barArtToggle(const char* id, ui::PixelDecor decor, const char* tip, bool* on, const ui::PixelArt& art)
{
    ui::CommandBarItem(ui::Px(28), ui::Px(24));
    if (ui::PixelIconButton(id, art, decor, tip, *on)) *on = !*on;
}

void partChip(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text)
{
    const UiTokens& t = uiTokens();
    ui::PushFont(UI_FONT_BODY, 11);
    ImVec2 ts = ImGui::CalcTextSize(text);
    if (b.x - a.x >= ts.x + ui::Px(8) && b.y - a.y >= ts.y + ui::Px(2)) {
        ImU32 back = ui::WithAlpha(t.layer, 200);
        dl->AddRectFilled(a, ImVec2(a.x + ts.x + ui::Px(8), a.y + ts.y + ui::Px(2)), back);
        dl->AddText(ImVec2(a.x + ui::Px(4), a.y + ui::Px(1)), t.text, text);
    }
    ui::PopFont();
}

void hatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col)
{
    float step = fmaxf(4.0f, ui::Px(5)), h = b.y - a.y;
    dl->PushClipRect(a, b, true);
    /* Only the lines that cross the clip rect, the line at x reaching x + b.y - y at row y. */
    ImVec2 c0 = dl->GetClipRectMin(), c1 = dl->GetClipRectMax();
    float x = a.x - h, lo = c0.x - (b.y - c0.y);
    if (lo > x) x += floorf((lo - x) / step) * step;
    for (; c0.y < c1.y && x + b.y - c1.y < c1.x; x += step) {
        dl->AddLine(ImVec2(x + b.y - c1.y, c1.y), ImVec2(x + b.y - c0.y, c0.y), col);
    }
    dl->PopClipRect();
}

void dashedSegment(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float dash)
{
    bool across = a.y == b.y;
    ImVec2 c0 = dl->GetClipRectMin(), c1 = dl->GetClipRectMax();
    float at = across ? a.y : a.x, v = across ? a.x : a.y, end = across ? b.x : b.y;
    float lo = (across ? c0.x : c0.y) - th, hi = (across ? c1.x : c1.y) + th;
    if (at < (across ? c0.y : c0.x) - th || at > (across ? c1.y : c1.x) + th) return;
    if (lo > v) v += floorf((lo - v) / (dash * 2)) * dash * 2;
    for (; v < end && v < hi; v += dash * 2) {
        float e = fminf(v + dash, end);
        dl->AddLine(across ? ImVec2(v, at) : ImVec2(at, v), across ? ImVec2(e, at) : ImVec2(at, e), col, th);
    }
}

void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th)
{
    float dash = ui::Px(4);
    dashedSegment(dl, a, ImVec2(b.x, a.y), col, th, dash);
    dashedSegment(dl, ImVec2(a.x, b.y), b, col, th, dash);
    dashedSegment(dl, a, ImVec2(a.x, b.y), col, th, dash);
    dashedSegment(dl, ImVec2(b.x, a.y), b, col, th, dash);
}

void drawCheckerRect(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    const UiTokens& t = uiTokens();
    float c = fmaxf(2.0f, ui::Px(6));
    /* Only the squares inside the clip rect. */
    ImVec2 ca = dl->GetClipRectMin(), cb = dl->GetClipRectMax();
    ImVec2 va(fmaxf(a.x, ca.x), fmaxf(a.y, ca.y)), vb(fminf(b.x, cb.x), fminf(b.y, cb.y));
    if (vb.x <= va.x || vb.y <= va.y) return;
    dl->AddRectFilled(va, vb, t.checkerA);
    dl->PushClipRect(va, vb, true);
    int r0 = (int)floorf((va.y - a.y) / c), c0 = (int)floorf((va.x - a.x) / c);
    for (int r = r0; a.y + r * c < vb.y; r++) {
        for (int i = c0 - ((c0 ^ r) & 1); a.x + i * c < vb.x; i += 2) {
            float x = a.x + i * c, y = a.y + r * c;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + c, y + c), t.checkerB);
        }
    }
    dl->PopClipRect();
}

void fitCell(const char* text, const char* tip)
{
    ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(text);
    /* As wide as the text, so a fitted column keeps its width; cut where the clip rect ends. */
    ImGui::Dummy(ts);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float maxX = fminf(p.x + ts.x, dl->GetClipRectMax().x);
    ui::TextFit(dl, p, maxX, text, 0, ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip), tip);
}

bool steadyTipBegin(SteadyTip& t, ImGuiID id)
{
    int frame = ImGui::GetFrameCount();
    double now = ImGui::GetTime();
    bool item = id != t.id || t.frame != frame - 1, fresh = item || now - t.at >= 0.5;
    if (item) t.w = 0;
    if (fresh) t.at = now;
    t.id = id;
    t.frame = frame;
    ImGui::SetNextWindowSizeConstraints(ImVec2(t.w, 0), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None);
    return fresh;
}

void steadyTipEnd(SteadyTip& t)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    t.w = fmaxf(t.w, fmaxf(w->DC.CursorMaxPos.x, w->DC.IdealMaxPos.x) - w->DC.CursorStartPos.x + w->WindowPadding.x * 2);
    ImGui::EndTooltip();
}

void steadyTooltip(SteadyTip& t, ImGuiID id, const char* text)
{
    if (steadyTipBegin(t, id)) snprintf(t.text, sizeof(t.text), "%s", text);
    ImGui::TextUnformatted(t.text);
    steadyTipEnd(t);
}

float monoWidth(const char* const* texts, int n)
{
    ui::PushFont(UI_FONT_MONO);
    float w = 0;
    for (int i = 0; i < n; i++) w = fmaxf(w, ImGui::CalcTextSize(texts[i]).x);
    ui::PopFont();
    return w;
}

void note(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, uiTokens().textSecondary);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void emptyView(const char* text)
{
    ImGui::Dummy(ImVec2(0, ui::Px(16)));
    ImGui::Indent(ui::Px(16));
    note(text);
    ImGui::Unindent(ui::Px(16));
}

float sprPatPitch(float tile)
{
    return ImGui::GetTextLineHeight() + tile + ui::Px(2) + ui::Px(7) + ui::Px(6);
}

void canvasGrid(const ui::CanvasState& cv, ImDrawList* dl, ImVec2 a, ImVec2 b, float scaleX, float scaleY)
{
    if (!cv.grid) return;
    ui::CanvasXf xf = {};
    xf.origin = a;
    xf.scale = ImVec2(scaleX, scaleY);
    ui::CanvasGrid(dl, xf, a, b);
}

}
