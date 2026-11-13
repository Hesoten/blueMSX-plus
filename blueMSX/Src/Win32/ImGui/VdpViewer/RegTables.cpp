/*****************************************************************************
**
** Register tables of the video chips and the raw table that shows them.
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
#include "imgui.h"
#include "ImGuiTheme.h"
#include "ImGuiWidgets.h"
#include "ChipView.h"
#include "RegTables.h"
#include "ToolText.h"

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

/* Seconds a changed row takes to fade back. */
#define FADE_SECONDS 0.8f

void regFadeReset(RegFade& f)
{
    memset(&f, 0, sizeof(f));
}

const RegDesc* regBankFind(const RegBank& bank, int index)
{
    for (int i = 0; i < bank.count; i++) {
        if (bank.regs[i].index == index) return &bank.regs[i];
    }
    return NULL;
}

static void noteValues(const RegBank& bank, const UInt8* values, RegFade& f)
{
    unsigned v = shell::contentVersion();
    float dt = shell::toolDecayDt() / FADE_SECONDS;
    if (f.step != shell::stepVersion()) {
        f.step = shell::stepVersion();
        memset(f.heat, 0, sizeof(f.heat));
    }
    for (int i = 0; i < bank.count; i++) {
        int k = bank.regs[i].index & 0xff;
        f.heat[k] = f.heat[k] > dt ? f.heat[k] - dt : 0;
    }
    if (f.primed && f.version == v) {
        return;
    }
    for (int i = 0; i < bank.count; i++) {
        int k = bank.regs[i].index & 0xff;
        UInt8 val = values[bank.regs[i].index];
        if (f.primed && f.prev[k] != val) f.heat[k] = 1;
        f.prev[k] = val;
    }
    f.version = v;
    f.primed = true;
}

static int fieldValue(const RegField& fl, int value)
{
    return (value >> fl.shift) & ((1 << fl.width) - 1);
}

/* "BL IE0 M2" for set flags and "TC=F" for wider fields. */
static void summary(const RegDesc& d, int value, char* out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < d.count && n + 1 < size; i++) {
        const RegField& fl = d.fields[i];
        int v = fieldValue(fl, value);
        int w;
        if (fl.width == 1) {
            if (!v) continue;
            w = snprintf(out + n, size - n, "%s%s", n ? " " : "", fl.name);
        }
        else {
            if (!v) continue;
            w = snprintf(out + n, size - n, "%s%s=%X", n ? " " : "", fl.name, v);
        }
        if (w < 0) break;
        n += (size_t)w;
        if (n >= size) { n = size - 1; break; }
    }
}

static void tip(const RegBank& bank, const RegDesc& d, int value)
{
    const UiTokens& t = uiTokens();
    ImGui::BeginTooltip();
    ui::PushFont(UI_FONT_STRONG);
    if (bank.hex) ImGui::Text("%s%02Xh  %s", bank.prefix, d.index, d.name);
    else ImGui::Text("%s%d  %s", bank.prefix, d.index, d.name);
    ui::PopFont();
    /* The row's summary, which a narrow pane cuts. */
    char sum[96];
    summary(d, value, sum, sizeof(sum));
    if (sum[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::TextUnformatted(sum);
        ImGui::PopStyleColor();
    }
    ui::PushFont(UI_FONT_MONO);
    ImGui::Text("%02X  %d%d%d%d %d%d%d%d", value, (value >> 7) & 1, (value >> 6) & 1, (value >> 5) & 1,
                (value >> 4) & 1, (value >> 3) & 1, (value >> 2) & 1, (value >> 1) & 1, value & 1);
    ui::PopFont();
    if (d.count > 0 && ImGui::BeginTable("bits", 3, ImGuiTableFlags_SizingFixedFit)) {
        for (int i = 0; i < d.count; i++) {
            const RegField& fl = d.fields[i];
            int v = fieldValue(fl, value);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
            if (fl.width == 1) ImGui::Text(ts(TS_REG_BIT), fl.shift);
            else ImGui::Text(ts(TS_REG_BITS), fl.shift + fl.width - 1, fl.shift);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, fl.width == 1 && !v ? t.textTertiary : t.text);
            if (fl.width == 1) ImGui::Text("%s %d", fl.name, v);
            else ImGui::Text("%s %X", fl.name, v);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fl.tip ? fl.tip : "");
        }
        ImGui::EndTable();
    }
    else if (d.count == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        ImGui::Text(ts(TS_REG_VALUE), value);
        ImGui::PopStyleColor();
    }
    ImGui::EndTooltip();
}

void drawRegBank(const RegBank& bank, const UInt8* values, RegFade& fade,
                 bool (*shown)(int index, const void* ref), const void* ref)
{
    const UiTokens& t = uiTokens();
    noteValues(bank, values, fade);
    if (!ImGui::BeginTable(bank.id, 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings |
                                       ImGuiTableFlags_NoHostExtendX)) {
        return;
    }
    ImGui::TableSetupColumn("reg", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("hex", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    bool tableHovered = ImGui::TableGetHoveredColumn() >= 0;
    for (int i = 0; i < bank.count; i++) {
        const RegDesc& d = bank.regs[i];
        if (shown && !shown(d.index, ref)) continue;
        int value = values[d.index];
        float heat = fade.heat[d.index & 0xff];
        char sum[96];
        ImGui::TableNextRow();
        if (heat > 0) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ui::WithAlpha(t.accent, (int)(heat * (t.dark ? 90 : 64))));
        }
        ImGui::TableNextColumn();
        float y0 = ImGui::GetCursorScreenPos().y;
        float bodyH = ImGui::GetTextLineHeight();
        ui::PushFont(UI_FONT_MONO);
        /* Centred on the body text's line, as KeyValueMono does. */
        float dy = floorf((bodyH - ImGui::GetTextLineHeight()) / 2);
        if (dy > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dy);
        ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
        if (bank.hex) ImGui::Text("%s%02Xh", bank.prefix, d.index);
        else ImGui::Text("%s%d", bank.prefix, d.index);
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        if (dy > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dy);
        ImGui::Text("%02X", value);
        ui::PopFont();
        ImGui::TableNextColumn();
        summary(d, value, sum, sizeof(sum));
        {
            /* Name then summary on one line, each cut with an ellipsis; the row's tooltip has both. */
            ImVec2 p = ImGui::GetCursorScreenPos();
            float right = p.x + fmaxf(1.0f, ImGui::GetContentRegionAvail().x);
            ImGui::Dummy(ImVec2(right - p.x, ImGui::GetTextLineHeight()));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ui::TextFit(dl, p, right, d.name, t.text, false);
            float x = p.x + ImGui::CalcTextSize(d.name).x + ImGui::GetStyle().ItemSpacing.x;
            if (sum[0] && x < right) ui::TextFit(dl, ImVec2(x, p.y), right, sum, t.textSecondary, false);
        }
        float my = ImGui::GetIO().MousePos.y;
        if (tableHovered && my >= y0 && my < ImGui::GetItemRectMax().y + ImGui::GetStyle().CellPadding.y * 2) {
            tip(bank, d, value);
        }
    }
    ImGui::EndTable();
}
