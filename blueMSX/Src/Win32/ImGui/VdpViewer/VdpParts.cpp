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
#include <math.h>
#include "imgui.h"
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


void fitCell(const char* text, const char* tip)
{
    ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(text);
    /* As wide as the text, so a fitted column keeps its width; cut where the clip rect ends. */
    ImGui::Dummy(ts);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float maxX = fminf(p.x + ts.x, dl->GetClipRectMax().x);
    ui::TextFit(dl, p, maxX, text, 0, ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip), tip);
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

}
