/*****************************************************************************
**
** Small ImGui building blocks shared by the tool windows.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
#define IMGUI_DEFINE_MATH_OPERATORS
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiWidgets.h"
#include "ImGuiTheme.h"
#include "ToolText.h"

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace ui {

float Px(float v)
{
    return floorf(v * ImGui::GetStyle().FontScaleDpi + 0.5f);
}

void PushFont(UiFont role, float em)
{
    ImGuiToolWindow* w = imguiHostCurrentWindow();
    if (em <= 0) {
        em = role == UI_FONT_MONO ? 13.0f : 14.0f;
    }
    ImGui::PushFont(imguiHostFont(w, role), w ? imguiHostFontSize(w, role, em) : 0.0f);
}

void PopFont()
{
    ImGui::PopFont();
}

/* Token colours through the style alpha, so BeginDisabled dims them. */
static ImU32 C(ImU32 c)
{
    return ImGui::GetColorU32(c);
}

static bool visible(ImU32 c)
{
    return (c & IM_COL32_A_MASK) != 0;
}

/* The line Windows 11 puts on one edge of a raised control. */
static void drawElevationEdge(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, bool atBottom, float r)
{
    float y = atBottom ? b.y - 0.5f : a.y + 0.5f;
    dl->AddLine(ImVec2(a.x + r, y), ImVec2(b.x - r, y), C(col));
}

/* Text up to a "##" suffix, as ImGui labels have. */
static const char* labelEnd(const char* label)
{
    return ImGui::FindRenderedTextEnd(label);
}

/* ---- card ------------------------------------------------------------- */

/* Contents go to channel 1 first, so the card's fill can be put under them
** once their height is known. */
struct CardFrame {
    ImDrawListSplitter split;
    ImDrawList*        dl;
    ImVec2             start;
    float              width;
    float              workMaxX, contentMaxX;
    float              padBottom, minHeight;
};
static CardFrame cards[4];
static int       cardDepth;

/* Cards nested deeper than this draw without a frame. */
static int cardOverflow;
static int cardFrameCount = -1;

/* The card's frame, or NULL when nested too deep to have one. */
static CardFrame* beginCardFrame(const char* id, float padX, float padTop, float padBottom, float minHeight)
{
    /* A frame that ended inside a card must not leak into the next one. */
    if (cardFrameCount != ImGui::GetFrameCount()) {
        cardFrameCount = ImGui::GetFrameCount();
        cardDepth = cardOverflow = 0;
    }
    if (cardDepth >= IM_ARRAYSIZE(cards)) {
        cardOverflow++;
        ImGui::PushID(id);
        ImGui::BeginGroup();
        return NULL;
    }
    CardFrame& c = cards[cardDepth++];
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    c.dl    = ImGui::GetWindowDrawList();
    c.start = ImGui::GetCursorScreenPos();
    c.width = ImGui::GetContentRegionAvail().x;
    c.padBottom = padBottom;
    c.minHeight = minHeight;
    c.split.Split(c.dl, 2);
    c.split.SetCurrentChannel(c.dl, 1);
    /* What is drawn inside sees the card's inner width as the space left. */
    float right   = c.start.x + c.width - padX;
    c.workMaxX    = win->WorkRect.Max.x;
    c.contentMaxX = win->ContentRegionRect.Max.x;
    win->WorkRect.Max.x = win->ContentRegionRect.Max.x = right;
    ImGui::SetCursorScreenPos(ImVec2(c.start.x + padX, c.start.y + padTop));
    ImGui::BeginGroup();
    return &c;
}

void BeginCardEx(const char* id, const CardStyle& style)
{
    beginCardFrame(id, Px(style.padX), Px(style.padTop), Px(style.padBottom), Px(style.minHeight));
}

void BeginCard(const char* id, const char* title, const char* subtitle)
{
    if (beginCardFrame(id, Px(16), Px(12), Px(12), 0) == NULL) {
        if (title) SectionTitle(title);
        return;
    }
    float right = ImGui::GetCurrentWindow()->WorkRect.Max.x;
    if (title) {
        PushFont(UI_FONT_STRONG);
        ImGui::TextUnformatted(title);
        PopFont();
        if (subtitle) {
            ImGui::SameLine();
            float x = right - ImGui::CalcTextSize(subtitle).x;
            if (x > ImGui::GetCursorScreenPos().x) {
                ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetCursorScreenPos().y));
            }
            ImGui::PushStyleColor(ImGuiCol_Text, uiTokens().textSecondary);
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
        }
    }
}

void HoldHeight(HeightHold& h, float y0)
{
    float used = ImGui::GetCursorPosY() - y0;
    float font = ImGui::GetFontSize();
    double now = ImGui::GetTime();
    int frame = ImGui::GetFrameCount();
    if (h.frame != frame - 1 || font != h.font || used >= h.latch) {
        h.font = font;
        h.latch = used;
        h.shorter = 0;
        h.since = -1;
    }
    else {
        h.shorter = fmaxf(h.shorter, used);
        if (h.since < 0) h.since = now;
        if (now - h.since >= 1.5) {
            h.latch = h.shorter;
            h.shorter = 0;
            h.since = -1;
        }
    }
    h.frame = frame;
    float pad = h.latch - used - ImGui::GetStyle().ItemSpacing.y;
    if (pad > 0) ImGui::Dummy(ImVec2(0, pad));
}

void EndCard()
{
    if (cardOverflow > 0) {
        cardOverflow--;
        ImGui::EndGroup();
        ImGui::PopID();
        return;
    }
    if (cardDepth <= 0) {
        return;
    }
    CardFrame& c = cards[--cardDepth];
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const UiTokens& t = uiTokens();
    ImGui::EndGroup();
    float bottom = fmaxf(ImGui::GetItemRectMax().y + c.padBottom, c.start.y + c.minHeight);
    win->WorkRect.Max.x = c.workMaxX;
    win->ContentRegionRect.Max.x = c.contentMaxX;

    ImVec2 a = c.start, b(c.start.x + c.width, bottom);
    c.split.SetCurrentChannel(c.dl, 0);
    c.dl->AddRectFilled(a, b, C(t.cardFill), Px(8));
    c.dl->AddRect(a, b, C(t.cardStroke), Px(8));
    c.split.Merge(c.dl);
    ImGui::SetCursorScreenPos(a);
    /* Cards stand 8 px apart. */
    float gap = Px(8) - ImGui::GetStyle().ItemSpacing.y;
    ImGui::Dummy(ImVec2(c.width, b.y - a.y + (gap > 0 ? gap : 0)));
    ImGui::PopID();
}

void SectionTitle(const char* text)
{
    PushFont(UI_FONT_STRONG);
    ImGui::TextUnformatted(text);
    PopFont();
}

/* ---- choices ---------------------------------------------------------- */

/* The frame is 1 px, then 2 px of padding; items are 2 px apart. */
float SegmentedWidth(const char* const* items, int count, float em)
{
    if (em > 0) PushFont(UI_FONT_BODY, em);
    float w = Px(2) * 2 + 2 + Px(2) * (count > 0 ? count - 1 : 0);
    for (int i = 0; i < count; i++) {
        w += ImGui::CalcTextSize(items[i], labelEnd(items[i])).x + Px(10) * 2;
    }
    if (em > 0) PopFont();
    return w;
}

float SegmentedHeight(float em)
{
    return Px(em > 0 && em < 14 ? 24.0f : 28.0f);
}

bool Segmented(const char* id, int* selected, const char* const* items, int count, float em, unsigned disabled,
               const char* disabledTip, unsigned dim, const int* ids, const char* tip)
{
    const UiTokens& t = uiTokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float h = SegmentedHeight(em), w = SegmentedWidth(items, count, em), pad = Px(2) + 1, gap = Px(2), padX = Px(10);
    bool changed = false;
    ImVec2 o = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), C(t.controlFill), Px(6));
    dl->AddRect(o, ImVec2(o.x + w, o.y + h), C(t.controlStroke), Px(6));

    if (em > 0) PushFont(UI_FONT_BODY, em);
    ImGui::PushID(id);
    float x = o.x + pad;
    for (int i = 0; i < count; i++) {
        const char* end = labelEnd(items[i]);
        ImVec2 ts = ImGui::CalcTextSize(items[i], end);
        bool off = (disabled >> i) & 1;
        ImVec2 a(x, o.y + pad), b(x + ts.x + padX * 2, o.y + h - pad);
        ImGui::SetCursorScreenPos(a);
        if (off) ImGui::BeginDisabled();
        ImGui::PushID(ids != NULL ? ids[i] : i);
        bool pressed = ImGui::InvisibleButton("##seg", b - a, ImGuiButtonFlags_EnableNav);
        ImGui::PopID();
        bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        bool on = *selected == i;
        if (pressed) {
            *selected = i;
            changed = on = true;
        }
        if (on) {
            dl->AddRectFilled(a, b, C(t.controlFillHover), Px(4));
            drawElevationEdge(dl, a, b, t.elevationEdge, t.elevationAtBottom, Px(4));
        }
        else if (hovered || held) {
            dl->AddRectFilled(a, b, held ? C(t.subtlePressed) : C(t.subtleHover), Px(4));
        }
        dl->AddText(ImVec2(a.x + padX, floorf((a.y + b.y - ts.y) / 2)), C(on || hovered ? t.text : (dim >> i) & 1 ? t.textTertiary : t.textSecondary), items[i], end);
        ImGui::RenderNavCursor(ImRect(a, b), ImGui::GetItemID());
        ItemTooltip(off ? disabledTip : tip);
        if (off) ImGui::EndDisabled();
        x = b.x + gap;
    }
    ImGui::PopID();
    if (em > 0) PopFont();
    ImGui::SetCursorScreenPos(o);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

/* ---- drop-down -------------------------------------------------------- */

static void drawChevron(ImDrawList* dl, ImVec2 c, ImU32 col)
{
    float w = Px(4), h = Px(2);
    ImVec2 pts[3] = { ImVec2(c.x - w, c.y - h), ImVec2(c.x, c.y + h), ImVec2(c.x + w, c.y - h) };
    dl->AddPolyline(pts, 3, C(col), fmaxf(1.0f, Px(1)));
}

bool BeginCombo(const char* id, const char* preview, float width)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) {
        return false;
    }
    const UiTokens& t = uiTokens();
    if (width > 0) {
        ImGui::SetNextItemWidth(Px(width));
    }
    ImVec2 a = win->DC.CursorPos;
    ImVec2 b(a.x + ImGui::CalcItemWidth(), a.y + ImGui::GetFrameHeight());
    /* ImGui draws the frame; the preview and chevron go on it here, in the
    ** window's own draw list, as an open combo makes the popup current. */
    bool open = ImGui::BeginCombo(id, "", ImGuiComboFlags_NoArrowButton);
    ImDrawList* dl = win->DrawList;
    float padX = ImGui::GetStyle().FramePadding.x, chevronW = Px(28);
    drawElevationEdge(dl, a, b, t.elevationEdge, t.elevationAtBottom, ImGui::GetStyle().FrameRounding);
    if (preview && *preview) {
        const char* pend = labelEnd(preview);
        ImVec2 ts = ImGui::CalcTextSize(preview, pend);
        dl->PushClipRect(ImVec2(a.x + padX, a.y), ImVec2(b.x - chevronW, b.y), true);
        dl->AddText(ImVec2(a.x + padX, floorf(a.y + (b.y - a.y - ts.y) / 2)), C(t.text), preview, pend);
        dl->PopClipRect();
    }
    drawChevron(dl, ImVec2(floorf(b.x - chevronW / 2), floorf((a.y + b.y) / 2)), t.textSecondary);
    if (open) {
        /* Entries sit close; EndCombo takes this off. */
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, Px(2)));
    }
    return open;
}

void EndCombo()
{
    ImGui::PopStyleVar();
    ImGui::EndCombo();
}

float ComboWidth(const char* const* previews, int count, float min)
{
    /* BeginCombo's text inset and chevron, and a little air. */
    float pad = ImGui::GetStyle().FramePadding.x + Px(28) + Px(4), w = 0;
    for (int i = 0; i < count; i++) {
        w = fmaxf(w, ImGui::CalcTextSize(previews[i], labelEnd(previews[i])).x + pad);
    }
    float scale = ImGui::GetStyle().FontScaleDpi;
    return fmaxf(min, ceilf(w / (scale > 0 ? scale : 1)));
}

bool ComboItem(const char* label, bool selected, bool dim)
{
    const UiTokens& t = uiTokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* end = labelEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight() + Px(4);
    if (w < ts.x + Px(24)) {
        w = ts.x + Px(24);
    }
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImVec2 q = ImGui::GetItemRectMax();
    if (selected && ImGui::IsWindowAppearing()) {
        ImGui::SetItemDefaultFocus();
        ImGui::SetScrollHereY();
    }
    ImU32 fill = held ? t.subtlePressed : hovered ? t.subtleHover : selected ? t.subtleSelected : 0;
    if (visible(fill)) {
        dl->AddRectFilled(p, q, C(fill), Px(4));
    }
    if (selected) {
        float pw = Px(3), ph = Px(16), cy = floorf((p.y + q.y) / 2);
        dl->AddRectFilled(ImVec2(p.x, cy - ph / 2), ImVec2(p.x + pw, cy + ph / 2), C(t.accent), pw / 2);
    }
    dl->AddText(ImVec2(p.x + Px(12), floorf(p.y + (h - ts.y) / 2)), C(dim && !hovered ? t.textTertiary : t.text), label, end);
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    if (pressed) {
        ImGui::CloseCurrentPopup();
    }
    return pressed;
}

bool Combo(const char* id, int* current, const char* const* items, int count, float width)
{
    bool changed = false;
    const char* preview = *current >= 0 && *current < count ? items[*current] : "";
    if (BeginCombo(id, preview, width)) {
        for (int i = 0; i < count; i++) {
            ImGui::PushID(i);
            if (ComboItem(items[i], i == *current) && i != *current) {
                *current = i;
                changed = true;
            }
            ImGui::PopID();
        }
        EndCombo();
    }
    return changed;
}

/* ---- buttons ---------------------------------------------------------- */

enum ButtonKind { BUTTON_STANDARD, BUTTON_ACCENT, BUTTON_SUBTLE };

static bool buttonEx(const char* label, ImVec2 size, ButtonKind kind, bool selected)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) {
        return false;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const UiTokens& t = uiTokens();
    ImGuiID id = win->GetID(label);
    ImVec2 ls = ImGui::CalcTextSize(label, NULL, true);
    ImVec2 p = win->DC.CursorPos;
    ImVec2 sz = ImGui::CalcItemSize(size, ls.x + style.FramePadding.x * 2, ImGui::GetFrameHeight());
    ImRect bb(p, p + sz);
    ImGui::ItemSize(sz, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* dl = win->DrawList;
    float r = style.FrameRounding;
    ImU32 fill, text = t.text;
    bool accent = kind == BUTTON_ACCENT || (kind == BUTTON_SUBTLE && selected);
    if (accent) {
        fill = held ? t.accentPressed : hovered ? t.accentHover : t.accent;
        text = t.textOnAccent;
    }
    else if (kind == BUTTON_SUBTLE) {
        fill = held ? t.subtlePressed : hovered ? t.subtleHover : 0;
        text = held ? t.textSecondary : t.text;
    }
    else {
        fill = held ? t.controlFillPressed : hovered ? t.controlFillHover : t.controlFill;
        text = held ? t.textSecondary : t.text;
    }
    if (visible(fill)) {
        dl->AddRectFilled(bb.Min, bb.Max, C(fill), r);
    }
    if (kind == BUTTON_STANDARD) {
        dl->AddRect(bb.Min, bb.Max, C(t.controlStroke), r);
        drawElevationEdge(dl, bb.Min, bb.Max, t.elevationEdge, t.elevationAtBottom, r);
    }
    else if (kind == BUTTON_ACCENT) {
        drawElevationEdge(dl, bb.Min, bb.Max, IM_COL32(0, 0, 0, 36), true, r);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &ls,
                             ImVec2(0.5f, 0.5f), &bb);
    ImGui::PopStyleColor();
    ImGui::RenderNavCursor(bb, id);
    return pressed;
}

bool Button(const char* label, ImVec2 size)
{
    return buttonEx(label, size, BUTTON_STANDARD, false);
}

bool AccentButton(const char* label, ImVec2 size)
{
    return buttonEx(label, size, BUTTON_ACCENT, false);
}

bool SubtleButton(const char* label, bool selected, ImVec2 size)
{
    return buttonEx(label, size, BUTTON_SUBTLE, selected);
}

/* ---- labels ----------------------------------------------------------- */

void StatusPill(const char* text, UiTone tone)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) {
        return;
    }
    const UiTokens& t = uiTokens();
    ImU32 col = tone == TONE_SUCCESS ? t.success : tone == TONE_CAUTION ? t.caution :
                tone == TONE_CRITICAL ? t.critical : t.textSecondary;
    float frameH = ImGui::GetFrameHeight(), h = Px(20), padX = Px(8);
    PushFont(UI_FONT_STRONG, 12);
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = win->DC.CursorPos;
    ImVec2 sz(ts.x + padX * 2, frameH);
    ImGui::ItemSize(sz, ImGui::GetStyle().FramePadding.y);
    if (ImGui::ItemAdd(ImRect(p, p + sz), 0)) {
        ImDrawList* dl = win->DrawList;
        ImVec2 a(p.x, floorf(p.y + (frameH - h) / 2)), b(p.x + sz.x, a.y + h);
        ImU32 fill = tone == TONE_NEUTRAL ? t.subtleSelected :
                     (col & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, t.dark ? 46 : 31);
        dl->AddRectFilled(a, b, C(fill), h / 2);
        dl->AddText(ImVec2(a.x + padX, floorf(a.y + (h - ts.y) / 2)), C(col), text);
    }
    PopFont();
}

/* The whole text when cut, then more; wrapped as other long tooltips are. With more, in
** the body font, as more is prose even under a monospaced value. */
static void fitTooltip(const char* text, bool cut, const char* more)
{
    bool hasMore = more != NULL && *more;
    if ((!cut && !hasMore) || !ImGui::BeginTooltip()) {
        return;
    }
    if (hasMore) PushFont(UI_FONT_BODY);
    ImGui::PushTextWrapPos(Px(480));
    if (cut) ImGui::TextUnformatted(text);
    if (hasMore) ImGui::TextUnformatted(more);
    ImGui::PopTextWrapPos();
    if (hasMore) PopFont();
    ImGui::EndTooltip();
}

void ItemTip(const char* text)
{
    if (text != NULL && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        fitTooltip(text, true, NULL);
    }
}

bool TextFit(ImDrawList* dl, ImVec2 pos, float maxX, const char* text, ImU32 col, bool hovered, const char* more)
{
    ImVec2 ts = ImGui::CalcTextSize(text);
    bool cut = pos.x + ts.x > maxX;
    if (!cut) {
        dl->AddText(pos, col == 0 ? ImGui::GetColorU32(ImGuiCol_Text) : C(col), text);
    }
    else {
        /* RenderTextEllipsis applies the style alpha itself, so the colour goes in raw. */
        if (col != 0) ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::RenderTextEllipsis(dl, pos, ImVec2(maxX, pos.y + ts.y), maxX, text, NULL, &ts);
        if (col != 0) ImGui::PopStyleColor();
    }
    if (hovered) fitTooltip(text, cut, more);
    return cut;
}

bool TextFitItem(const char* text, ImU32 col, bool tip, const char* more)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) {
        return false;
    }
    ImVec2 p = win->DC.CursorPos;
    ImVec2 sz(fmaxf(1.0f, ImGui::GetContentRegionAvail().x), ImGui::GetTextLineHeight());
    ImGui::ItemSize(sz);
    if (!ImGui::ItemAdd(ImRect(p, p + sz), 0)) {
        return false;
    }
    bool hovered = tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    return TextFit(win->DrawList, p, p.x + sz.x, text, col, hovered, more);
}

bool BeginKeyValue(const char* id)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        return false;
    }
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

static void keyCell(const char* key)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, uiTokens().textSecondary);
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
}

/* The value cell: one line, cut with an ellipsis; text past 511 bytes is dropped. */
static void trimUtf8(char* s, size_t size);

static void valueCell(const char* key, const char* tip, bool mono, const char* fmt, va_list args)
{
    char text[512];
    vsnprintf(text, sizeof(text), fmt, args);
    trimUtf8(text, sizeof(text));
    keyCell(key);
    if (!mono) {
        TextFitItem(text, 0, true, tip);
        return;
    }
    float bodyH = ImGui::GetTextLineHeight();
    PushFont(UI_FONT_MONO);
    /* Centred on the body text's line, so rows keep one height. */
    float dy = floorf((bodyH - ImGui::GetTextLineHeight()) / 2);
    if (dy > 0) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dy);
    }
    TextFitItem(text, 0, true, tip);
    PopFont();
}

void KeyValue(const char* key, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    valueCell(key, NULL, false, fmt, args);
    va_end(args);
}

void KeyValueMono(const char* key, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    valueCell(key, NULL, true, fmt, args);
    va_end(args);
}

void KeyValueTip(const char* key, const char* tip, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    valueCell(key, tip, false, fmt, args);
    va_end(args);
}

void KeyValueMonoTip(const char* key, const char* tip, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    valueCell(key, tip, true, fmt, args);
    va_end(args);
}

void EndKeyValue()
{
    ImGui::EndTable();
}

bool ColorSwatch(const char* id, ImU32 rgb, float size, bool selected, const char* tip)
{
    const UiTokens& t = uiTokens();
    float s = Px(size);
    float r = Px(size >= 20 ? 4.0f : 2.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(s, s), ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 q(p.x + s, p.y + s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, C(rgb), r);
    if (selected) {
        /* Inside the square, so close neighbours do not cover it. */
        float w = Px(2);
        dl->AddRect(ImVec2(p.x + w / 2, p.y + w / 2), ImVec2(q.x - w / 2, q.y - w / 2), C(t.text), r, w);
        dl->AddRect(ImVec2(p.x + w, p.y + w), ImVec2(q.x - w, q.y - w), C(t.layer), r > w ? r - w : 0);
    }
    else {
        dl->AddRect(p, q, hovered ? C(t.textSecondary) : C(t.gridLine), r);
    }
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    ItemTooltip(tip);
    return clicked;
}

/* ---- icons ------------------------------------------------------------ */

bool HasIcons()
{
    return imguiHostFont(imguiHostCurrentWindow(), UI_FONT_ICON) != NULL;
}

void DrawIcon(ImDrawList* dl, ImVec2 c, const char* glyph, float em, ImU32 col)
{
    if (glyph == NULL || !HasIcons()) {
        return;
    }
    /* The icon fonts' line is their em, with no descent: the box is the glyph's cell. */
    PushFont(UI_FONT_ICON, em);
    float size = ImGui::GetFontSize();
    dl->AddText(ImGui::GetFont(), size, ImVec2(floorf(c.x - size / 2), floorf(c.y - size / 2)), C(col), glyph);
    PopFont();
}

ImU32 WithAlpha(ImU32 c, int a)
{
    return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

ImU32 Over(ImU32 fg, ImU32 bg)
{
    float a = ((fg >> IM_COL32_A_SHIFT) & 0xff) / 255.0f;
    static const int shifts[3] = { IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT };
    ImU32 out = IM_COL32_A_MASK;
    for (int s : shifts) {
        float f = (float)((fg >> s) & 0xff), b = (float)((bg >> s) & 0xff);
        out |= (ImU32)(b + (f - b) * a + 0.5f) << s;
    }
    return out;
}

/* A subtle control's fill: none at rest, neutral while on. */
static ImU32 subtleFill(const UiTokens& t, bool hovered, bool held, bool on)
{
    if (held) return t.subtlePressed;
    if (on) return hovered ? t.controlFillHover : t.controlFill;
    return hovered ? t.subtleHover : 0;
}

/* A toggle as Windows 11 draws one: off subtle, on in the accent. On but disabled it
** keeps colours of its own, drawn at full alpha (*opaque) so it still reads as on. */
static void toggleColours(const UiTokens& t, bool hovered, bool held, bool on, ImU32* fill, ImU32* glyph, bool* opaque)
{
    *opaque = on && (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    if (*opaque) {
        *fill = t.accentDisabled;
        *glyph = t.textOnAccentDisabled;
    }
    else if (on) {
        *fill = held ? t.accentPressed : hovered ? t.accentHover : t.accent;
        *glyph = held ? (t.textOnAccent & ~IM_COL32_A_MASK) | ((ImU32)(t.dark ? 128 : 179) << IM_COL32_A_SHIFT) : t.textOnAccent;
    }
    else {
        *fill = subtleFill(t, hovered, held, false);
        *glyph = held ? t.textSecondary : t.text;
    }
}

void ItemTooltip(const char* tip)
{
    if (tip && *tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tip);
    }
}

float IconButtonWidth(const char* glyph, const char* fallback, float w)
{
    bool icon = glyph && HasIcons();
    return !icon && fallback ? fmaxf(Px(w), ImGui::CalcTextSize(fallback).x + Px(12)) : Px(w);
}

bool IconButton(const char* id, const char* glyph, const char* fallback, const char* tip, bool checked, ImVec2 size, float em)
{
    const UiTokens& t = uiTokens();
    bool icon = glyph && HasIcons();
    ImVec2 sz(IconButtonWidth(glyph, fallback, size.x), Px(size.y));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, sz, ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImVec2 q = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill, col;
    bool opaque;
    toggleColours(t, hovered, held, checked, &fill, &col, &opaque);
    if (opaque) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    if (visible(fill)) {
        dl->AddRectFilled(p, q, C(fill), Px(4));
    }
    ImVec2 c(floorf((p.x + q.x) / 2), floorf((p.y + q.y) / 2));
    if (icon) {
        DrawIcon(dl, c, glyph, em, col);
    }
    else if (fallback) {
        ImVec2 ts = ImGui::CalcTextSize(fallback);
        dl->AddText(ImVec2(floorf(c.x - ts.x / 2), floorf(c.y - ts.y / 2)), C(col), fallback);
    }
    if (opaque) ImGui::PopStyleVar();
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    ItemTooltip(tip);
    return pressed;
}

static bool artOn(const PixelArt& a, int x, int y)
{
    return x >= 0 && y >= 0 && x < a.w && y < a.h && a.rows[y][x] == '#';
}

static ImU32 scaleAlpha(ImU32 c, float k)
{
    ImU32 a = (ImU32)(((c >> IM_COL32_A_SHIFT) & 0xff) * k + 0.5f);
    return (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

/* One horizontal run of cells from x0 to x1 (exclusive) on row y of a grid at o. */
static void cellRun(ImDrawList* dl, ImVec2 o, int cell, int x0, int x1, int y, ImU32 col)
{
    dl->AddRectFilled(ImVec2(o.x + x0 * cell, o.y + y * cell), ImVec2(o.x + x1 * cell, o.y + (y + 1) * cell), col);
}

/* Dashes t px thick along n px from a, solid at both ends: about Px(3) on, Px(2) off. */
static void dashedSide(ImDrawList* dl, ImVec2 a, int n, bool across, int t, ImU32 col)
{
    int d = (int)fmaxf(2.0f, Px(3)), gap = (int)fmaxf(1.0f, Px(2));
    if (n <= d) d = n;
    int k = (int)floorf((n - d) / (float)(d + gap) + 0.5f);
    for (int i = 0; i <= k; i++) {
        int s = k > 0 ? (int)floorf(i * (n - d) / (float)k + 0.5f) : 0;
        ImVec2 p = across ? ImVec2(a.x + s, a.y) : ImVec2(a.x, a.y + s);
        dl->AddRectFilled(p, across ? ImVec2(p.x + d, p.y + t) : ImVec2(p.x + t, p.y + d), col);
    }
}

void DrawPixelArt(ImDrawList* dl, ImVec2 c, const PixelArt& pick, PixelDecor decor, ImU32 col, float em)
{
    /* One drawing and cell for every decor, so the art is the same size with or without a frame. */
    const PixelArt* best = NULL;
    int cell = 1, lim = (int)Px(em);
    /* The widest drawing within Px(em) at a whole cell, the larger on a tie: the 16x11 at 100, 125 and
    ** 200 %, the smaller one wherever it is wider (cell 2 at 150 and 175 %, cell 3 at 250 %). */
    for (const PixelArt* p = &pick; p != NULL; p = p->smaller) {
        int k = lim / p->w < lim / p->h ? lim / p->w : lim / p->h;
        if (k >= 1 && (best == NULL || p->w * k > best->w * cell)) { best = p; cell = k; }
    }
    if (best == NULL) {
        for (best = &pick; best->smaller != NULL; best = best->smaller) {}
        cell = 1;
    }
    const PixelArt& art = *best;
    const int m = 2, gw = art.w + 2 * m, gh = art.h + 2 * m;
    ImVec2 o(floorf(c.x - art.w * cell * 0.5f), floorf(c.y - art.h * cell * 0.5f));
    ImVec2 g(o.x - m * cell, o.y - m * cell);
    if (decor == DECOR_CHECKER && gw <= 40 && gh <= 40) {
        /* Cells outside the art, reached from the edge; the checker keeps one cell off it. */
        bool outside[40][40] = {};
        int stack[1600], top = 0;
        outside[0][0] = true;
        stack[top++] = 0;
        while (top > 0) {
            int v = stack[--top], x = v % gw, y = v / gw;
            static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
            for (int i = 0; i < 4; i++) {
                int nx = x + dx[i], ny = y + dy[i];
                if (nx < 0 || ny < 0 || nx >= gw || ny >= gh || outside[ny][nx] || artOn(art, nx - m, ny - m)) continue;
                outside[ny][nx] = true;
                stack[top++] = ny * gw + nx;
            }
        }
        /* Squares as coarse and faint as still read as a checker, so the art stays clear. */
        int b = (int)fmaxf(1.0f, floorf(Px(4) / cell + 0.5f));
        ImU32 dim = C(scaleAlpha(col, uiTokens().dark ? 0.2f : 0.22f));
        for (int y = 0; y < gh; y++) {
            int run = -1;
            for (int x = 0; x <= gw; x++) {
                bool on = x < gw && ((x / b + y / b) & 1) == 0;
                for (int j = -1; on && j <= 1; j++)
                    for (int i = -1; on && i <= 1; i++) {
                        int nx = x + i, ny = y + j;
                        if (nx >= 0 && ny >= 0 && nx < gw && ny < gh && !outside[ny][nx]) on = false;
                    }
                if (on && run < 0) run = x;
                if (!on && run >= 0) { cellRun(dl, g, cell, run, x, y, dim); run = -1; }
            }
        }
    }
    ImU32 body = C(col);
    for (int y = 0; y < art.h; y++) {
        int run = -1;
        for (int x = 0; x <= art.w; x++) {
            bool on = artOn(art, x, y);
            if (on && run < 0) run = x;
            if (!on && run >= 0) { cellRun(dl, o, cell, run, x, y, body); run = -1; }
        }
    }
    if (decor == DECOR_FRAME) {
        /* A thin frame one cell off the art, the sides between the top and bottom ones. */
        ImU32 fc = C(col);
        int t = ImGui::GetStyle().FontScaleDpi < 1.75f ? 1 : 2, w = art.w * cell + 2 * (cell + t), h = art.h * cell + 2 * (cell + t);
        ImVec2 f(o.x - cell - t, o.y - cell - t);
        dashedSide(dl, f, w, true, t, fc);
        dashedSide(dl, ImVec2(f.x, f.y + h - t), w, true, t, fc);
        dashedSide(dl, ImVec2(f.x, f.y + t), h - 2 * t, false, t, fc);
        dashedSide(dl, ImVec2(f.x + w - t, f.y + t), h - 2 * t, false, t, fc);
    }
}

bool PixelIconButton(const char* id, const PixelArt& art, PixelDecor decor, const char* tip, bool checked, ImVec2 size, const char* fallback)
{
    const UiTokens& t = uiTokens();
    bool icon = fallback == NULL || HasIcons();
    ImVec2 sz(IconButtonWidth(fallback ? "" : NULL, fallback, size.x), Px(size.y));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, sz, ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImVec2 q = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill, col;
    bool opaque;
    toggleColours(t, hovered, held, checked, &fill, &col, &opaque);
    if (opaque) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    if (visible(fill)) {
        dl->AddRectFilled(p, q, C(fill), Px(4));
    }
    ImVec2 c(floorf((p.x + q.x) / 2), floorf((p.y + q.y) / 2));
    if (icon) {
        DrawPixelArt(dl, c, art, decor, col);
    }
    else {
        ImVec2 ts = ImGui::CalcTextSize(fallback);
        dl->AddText(ImVec2(floorf(c.x - ts.x / 2), floorf(c.y - ts.y / 2)), C(col), fallback);
    }
    if (opaque) ImGui::PopStyleVar();
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    ItemTooltip(tip);
    return pressed;
}

void StatusDot(const char* word, int tone)
{
    const UiTokens& t = uiTokens();
    ImU32 col = tone == TONE_SUCCESS ? t.success : tone == TONE_CAUTION ? t.caution :
                tone == TONE_CRITICAL ? t.critical : t.textSecondary;
    float h = ImGui::GetFrameHeight(), d = Px(8), gap = Px(6);
    ImVec2 ts = ImGui::CalcTextSize(word);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(d + gap + ts.x, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = floorf(p.y + h / 2);
    dl->AddCircleFilled(ImVec2(p.x + d / 2, cy), d / 2, C(col));
    dl->AddText(ImVec2(p.x + d + gap, floorf(cy - ts.y / 2)), C(t.text), word);
}

/* ---- tabs ------------------------------------------------------------- */

/* The chosen tab of the last TabView, for the card joined to it. */
static struct { float x0, x1, cardTop; int frame; } tabSel = { 0, 0, 0, -1 };

bool TabView(const char* id, int* selected, const TabItem* items, int count)
{
    const UiTokens& t = uiTokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    /* 2 pt under the 36 px row of the mockup. */
    const float trim = 2 * 96.0f / 72;
    float rowH = Px(36 - trim), selH = Px(37 - trim), h = Px(32 - trim), padX = Px(16), gap = Px(8), icon = Px(16), r = Px(8);
    ImU32 fill = Over(t.cardFill, t.layer), stroke = Over(t.cardStroke, t.layer);
    bool icons = HasIcons(), changed = false;
    int sel0 = *selected;
    tabSel.frame = -1;

    ImGui::PushID(id);
    dl->PushClipRect(o, ImVec2(o.x + avail, o.y + selH), true);
    float x = o.x;
    for (int i = 0; i < count; i++) {
        bool on = sel0 == i;
        /* A thin line between two tabs that are not chosen. */
        if (i > 0 && !on && sel0 != i - 1) {
            dl->AddRectFilled(ImVec2(x, floorf(o.y + h / 2) - Px(8)), ImVec2(x + 1, floorf(o.y + h / 2) + Px(8)), C(t.divider));
            x += 1;
        }
        bool glyph = icons && (items[i].glyph || items[i].art);
        /* Measured in the chosen tab's weight, so choosing one moves nothing. */
        PushFont(UI_FONT_STRONG);
        float labelW = ImGui::CalcTextSize(items[i].label).x;
        PopFont();
        float w = fmaxf(Px(104), padX * 2 + (glyph ? icon + gap : 0) + labelW), th = on ? selH : h;
        ImGui::SetCursorScreenPos(ImVec2(x, o.y));
        ImGui::PushID(i);
        bool pressed = ImGui::InvisibleButton("##tab", ImVec2(w, th), ImGuiButtonFlags_EnableNav);
        ImGui::PopID();
        bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        if (pressed && !on) {
            *selected = i;
            changed = true;
        }
        ImVec2 a(x, o.y), b(x + w, o.y + th);
        if (on) {
            dl->AddRectFilled(a, b, C(fill), r, ImDrawFlags_RoundTop);
            float l = a.x + 0.5f, rr = b.x - 0.5f, tp = a.y + 0.5f;
            dl->PathLineTo(ImVec2(l, b.y));
            dl->PathArcTo(ImVec2(l + r, tp + r), r, IM_PI, IM_PI * 1.5f);
            dl->PathArcTo(ImVec2(rr - r, tp + r), r, IM_PI * 1.5f, IM_PI * 2);
            dl->PathLineTo(ImVec2(rr, b.y));
            dl->PathStroke(C(stroke), 1.0f);
            tabSel.x0 = a.x;
            tabSel.x1 = b.x;
            tabSel.frame = ImGui::GetFrameCount();
        }
        else if (hovered || held) {
            dl->AddRectFilled(a, b, C(held ? t.subtlePressed : t.subtleHover), Px(6));
        }
        float cy = floorf((a.y + b.y) / 2), tx = a.x + padX;
        ImU32 col = on || hovered ? t.text : t.textSecondary;
        if (glyph) {
            if (items[i].art) DrawPixelArt(dl, ImVec2(tx + icon / 2, cy), *items[i].art, DECOR_NONE, col);
            else DrawIcon(dl, ImVec2(tx + icon / 2, cy), items[i].glyph, 16, col);
            tx += icon + gap;
        }
        PushFont(on ? UI_FONT_STRONG : UI_FONT_BODY);
        ImVec2 ts = ImGui::CalcTextSize(items[i].label);
        dl->AddText(ImVec2(tx, floorf(cy - ts.y / 2)), C(col), items[i].label);
        PopFont();
        ImGui::RenderNavCursor(ImRect(a, b), ImGui::GetItemID());
        x += w;
    }
    dl->PopClipRect();
    ImGui::PopID();
    tabSel.cardTop = o.y + rowH;
    ImGui::SetCursorScreenPos(o);
    ImGui::Dummy(ImVec2(avail, rowH));
    return changed;
}

void BeginTabCard(const char* id, float height)
{
    const UiTokens& t = uiTokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool joined = tabSel.frame == ImGui::GetFrameCount();
    ImVec2 a = ImGui::GetCursorScreenPos();
    if (joined) {
        a.y = tabSel.cardTop;
        ImGui::SetCursorScreenPos(a);
    }
    ImVec2 av = ImGui::GetContentRegionAvail();
    float h = height > 0 ? height : fmaxf(av.y + height, Px(80));
    ImVec2 b(a.x + av.x, a.y + h);
    ImU32 fill = Over(t.cardFill, t.layer), stroke = Over(t.cardStroke, t.layer);
    float r = Px(8);
    bool square = joined && tabSel.x0 <= a.x + 0.5f;
    dl->AddRectFilled(a, b, C(fill), r, square ? ImDrawFlags_RoundAll & ~ImDrawFlags_RoundTopLeft : ImDrawFlags_RoundAll);
    /* The outline leaves the chosen tab's foot open, so the two read as one. */
    float l = a.x + 0.5f, rr = b.x - 0.5f, tp = a.y + 0.5f, bt = b.y - 0.5f, rtl = square ? 0 : r;
    float g0 = joined ? fmaxf(tabSel.x0 + 0.5f, l + rtl) : l + rtl, g1 = joined ? fmaxf(tabSel.x1 - 0.5f, g0) : g0;
    dl->PathLineTo(ImVec2(g1, tp));
    dl->PathArcTo(ImVec2(rr - r, tp + r), r, IM_PI * 1.5f, IM_PI * 2);
    dl->PathArcTo(ImVec2(rr - r, bt - r), r, 0, IM_PI * 0.5f);
    dl->PathArcTo(ImVec2(l + r, bt - r), r, IM_PI * 0.5f, IM_PI);
    if (square) dl->PathLineTo(ImVec2(l, tp));
    else dl->PathArcTo(ImVec2(l + r, tp + r), r, IM_PI, IM_PI * 1.5f);
    dl->PathLineTo(ImVec2(g0, tp));
    dl->PathStroke(C(stroke), 1.0f);

    ImGui::SetCursorScreenPos(ImVec2(a.x + 1, a.y + 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(id, ImVec2(fmaxf(av.x - 2, 1.0f), fmaxf(h - 2, 1.0f)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
}

void EndTabCard()
{
    ImGui::EndChild();
}

/* ---- splitter --------------------------------------------------------- */

/* The drag going on; ImGui has one active item at a time. startAt is the mouse
** coordinate along the drag's axis when it began. */
static struct { ImGuiID id; float startPx, startAt; bool moved; } split;

/* The part grows as the mouse moves towards lower coordinates on its axis. */
static float splitSize(const char* id, float* size, float lo, float hi, bool x)
{
    /* A click that does not move keeps *size, which the window may be clamping. */
    float m = x ? ImGui::GetIO().MousePos.x : ImGui::GetIO().MousePos.y;
    bool active = split.id != 0 && ImGui::GetActiveID() == split.id && split.id == ImGui::GetID(id);
    split.moved |= active && m != split.startAt;
    bool drag = active && split.moved;
    float px = drag ? split.startPx + split.startAt - m : Px(*size);
    px = floorf(fminf(fmaxf(px, lo), hi));
    if (drag) *size = px / ImGui::GetStyle().FontScaleDpi;
    return px;
}

static void splitBar(const char* id, float w, float h, float px, bool x)
{
    const UiTokens& t = uiTokens();
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (ImGui::IsItemActivated()) {
        split.id = ImGui::GetItemID();
        split.startPx = px;
        split.startAt = x ? ImGui::GetIO().MousePos.x : ImGui::GetIO().MousePos.y;
        split.moved = false;
    }
    if (hovered || active) ImGui::SetMouseCursor(x ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    /* A divider at rest, stronger under the mouse, the accent while dragged. */
    float th = hovered || active ? Px(2) : 1, inset = Px(8);
    ImU32 col = C(active ? t.accent : hovered ? t.controlStrongStroke : t.divider);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (x) {
        float l = floorf(a.x + (w - th) / 2);
        dl->AddRectFilled(ImVec2(l, a.y + inset), ImVec2(l + th, a.y + h - inset), col);
    }
    else {
        float y = floorf(a.y + (h - th) / 2);
        dl->AddRectFilled(ImVec2(a.x + inset, y), ImVec2(a.x + w - inset, y + th), col);
    }
}

float SplitSize(const char* id, float* size, float lo, float hi)
{
    return splitSize(id, size, lo, hi, false);
}

void SplitBar(const char* id, float w, float h, float px)
{
    splitBar(id, w, h, px, false);
}

float SplitSizeX(const char* id, float* size, float lo, float hi)
{
    return splitSize(id, size, lo, hi, true);
}

void SplitBarX(const char* id, float w, float h, float px)
{
    splitBar(id, w, h, px, true);
}

/* ---- command bar ------------------------------------------------------ */

static struct { ImVec2 o; float w, left, right, x, rowTop; int rows; } bar;

void BeginCommandBar(const char* id)
{
    ImGui::PushID(id);
    bar.o = ImGui::GetCursorScreenPos();
    bar.w = ImGui::GetContentRegionAvail().x;
    bar.left = bar.x = bar.o.x + Px(8);
    bar.right = bar.o.x + bar.w - Px(8);
    bar.rowTop = bar.o.y;
    bar.rows = 1;
}

static bool barWraps(float w)
{
    if (bar.x <= bar.left || bar.x + w <= bar.right) {
        return false;
    }
    bar.rows++;
    bar.rowTop += Px(40);
    bar.x = bar.left;
    return true;
}

void CommandBarItem(float w, float h)
{
    if (h <= 0) {
        h = ImGui::GetFrameHeight();
    }
    barWraps(w);
    ImGui::SetCursorScreenPos(ImVec2(bar.x, floorf(bar.rowTop + (Px(40) - h) / 2)));
    bar.x += w + Px(4);
}

void CommandBarRight(float w)
{
    barWraps(w);
    bar.x = fmaxf(bar.x, bar.right - w);
}

void CommandBarSeparator()
{
    float w = Px(9);
    /* None at the start of a row. */
    if (bar.x <= bar.left || barWraps(w)) {
        return;
    }
    CommandBarItem(w, Px(20));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float x = floorf(p.x + Px(4));
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, p.y), ImVec2(x + 1, p.y + Px(20)), C(uiTokens().divider));
}

void EndCommandBar()
{
    float bottom = bar.o.y + bar.rows * Px(40);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bar.o.x, bottom), ImVec2(bar.o.x + bar.w, bottom + 1),
                                              C(uiTokens().divider));
    ImGui::SetCursorScreenPos(bar.o);
    ImGui::Dummy(ImVec2(bar.w, bottom + 1 - bar.o.y));
    ImGui::SetCursorScreenPos(ImVec2(bar.o.x, bottom + 1));
    ImGui::PopID();
}

float AppBarToggleWidth(const char* glyph, const char* label)
{
    bool icon = glyph && HasIcons();
    return Px(8) + (icon ? Px(16) + Px(6) : 0) + ImGui::CalcTextSize(label, labelEnd(label)).x + Px(8);
}

bool AppBarToggle(const char* label, const char* glyph, bool* on, const char* tip)
{
    const UiTokens& t = uiTokens();
    bool icon = glyph && HasIcons();
    float w = AppBarToggleWidth(glyph, label), h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    if (pressed) {
        *on = !*on;
    }
    ImVec2 q = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill, col;
    bool opaque;
    toggleColours(t, hovered, held, *on, &fill, &col, &opaque);
    if (opaque) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    if (visible(fill)) {
        dl->AddRectFilled(p, q, C(fill), Px(4));
    }
    float x = p.x + Px(8), cy = floorf(p.y + h / 2);
    if (icon) {
        DrawIcon(dl, ImVec2(x + Px(8), cy), glyph, 16, col);
        x += Px(22);
    }
    const char* end = labelEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(x, floorf(cy - ts.y / 2)), C(col), label, end);
    if (opaque) ImGui::PopStyleVar();
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    ItemTooltip(tip);
    return pressed;
}

/* ---- menus ------------------------------------------------------------ */

float DropDownButtonWidth(const char* glyph, const char* label, bool standard, float minW)
{
    bool icon = glyph && HasIcons();
    float w = Px(8) + (icon ? Px(16) + Px(6) : 0) + ImGui::CalcTextSize(label, labelEnd(label)).x +
              (standard ? Px(8) : Px(6)) + Px(10) + Px(8);
    return fmaxf(w, Px(minW));
}

bool BeginDropDownButton(const char* id, const char* glyph, const char* label, bool standard, float minW, const char* tip)
{
    const UiTokens& t = uiTokens();
    bool icon = glyph && HasIcons();
    float w = DropDownButtonWidth(glyph, label, standard, minW), h = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    ImGuiID popup = ImGui::GetID("##menu");
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##button", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    /* A click on the button while its menu is open only closes the menu. */
    if (pressed && !st->GetBool(popup, false)) {
        ImGui::OpenPopupEx(popup);
    }
    bool open = ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None);
    ImVec2 q = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = ImGui::GetStyle().FrameRounding;
    if (standard) {
        dl->AddRectFilled(p, q, C(held ? t.controlFillPressed : hovered ? t.controlFillHover : t.controlFill), r);
        dl->AddRect(p, q, C(t.controlStroke), r);
        drawElevationEdge(dl, p, q, t.elevationEdge, t.elevationAtBottom, r);
    }
    else {
        ImU32 fill = held || open ? t.subtlePressed : hovered ? t.subtleHover : 0;
        if (visible(fill)) dl->AddRectFilled(p, q, C(fill), r);
    }
    float x = p.x + Px(8), cy = floorf(p.y + h / 2);
    if (icon) {
        DrawIcon(dl, ImVec2(x + Px(8), cy), glyph, 16, t.text);
        x += Px(22);
    }
    const char* end = labelEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(x, floorf(cy - ts.y / 2)), C(t.text), label, end);
    drawChevron(dl, ImVec2(floorf(q.x - Px(8) - Px(5)), cy), t.text);
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    if (!open) {
        ItemTooltip(tip);
    }
    bool shown = false;
    if (open) {
        ImGui::SetNextWindowPos(ImVec2(p.x, q.y + Px(4)));
        ImGui::SetNextWindowSizeConstraints(ImVec2(Px(200), 0), ImVec2(FLT_MAX, FLT_MAX));
        /* Entries touch; their spacing stays pushed until EndDropDownButton. */
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(4), Px(4)));
        shown = ImGui::BeginPopupEx(popup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar();
        if (!shown) {
            ImGui::PopStyleVar();
        }
    }
    st->SetBool(popup, shown);
    if (!shown) {
        ImGui::PopID();
    }
    return shown;
}

void EndDropDownButton()
{
    ImGui::EndPopup();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

enum MenuMark { MARK_NONE, MARK_RADIO, MARK_CHECK };

static bool menuItem(const char* label, MenuMark mark, bool enabled, const char* disabledTip)
{
    const UiTokens& t = uiTokens();
    const char* end = labelEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    float h = Px(30), w = fmaxf(ImGui::GetContentRegionAvail().x, Px(28) + ts.x + Px(10));
    if (!enabled) ImGui::BeginDisabled();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImVec2 q = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill = held ? t.subtlePressed : hovered ? t.subtleHover : 0;
    if (visible(fill)) {
        dl->AddRectFilled(p, q, C(fill), Px(4));
    }
    float cy = floorf((p.y + q.y) / 2);
    if (mark == MARK_RADIO) {
        dl->AddCircleFilled(ImVec2(p.x + Px(13), cy), Px(3), C(t.text));
    }
    else if (mark == MARK_CHECK && HasIcons()) {
        DrawIcon(dl, ImVec2(p.x + Px(14), cy), "\xEE\x9C\xBE", 12, t.text);   /* U+E73E CheckMark */
    }
    else if (mark == MARK_CHECK) {
        float k = ImGui::GetStyle().FontScaleDpi;
        ImVec2 o(p.x + Px(8), cy - Px(6));
        ImVec2 pts[3] = { o + ImVec2(2 * k, 6.5f * k), o + ImVec2(5 * k, 9 * k), o + ImVec2(10 * k, 3 * k) };
        dl->AddPolyline(pts, 3, C(t.text), fmaxf(1.0f, 1.3f * k));
    }
    dl->AddText(ImVec2(p.x + Px(28), floorf(cy - ts.y / 2)), C(t.text), label, end);
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    if (!enabled) {
        ItemTooltip(disabledTip);
        ImGui::EndDisabled();
    }
    if (pressed) {
        ImGui::CloseCurrentPopup();
    }
    return pressed && enabled;
}

bool MenuRadio(const char* label, bool on, bool enabled, const char* disabledTip)
{
    return menuItem(label, on ? MARK_RADIO : MARK_NONE, enabled, disabledTip);
}

bool MenuCheck(const char* label, bool* on, bool enabled, const char* disabledTip)
{
    bool pressed = menuItem(label, *on ? MARK_CHECK : MARK_NONE, enabled, disabledTip);
    if (pressed) {
        *on = !*on;
    }
    return pressed;
}

void MenuSeparator()
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(w, Px(9)));
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + Px(4)), ImVec2(p.x + w, p.y + Px(4) + 1), C(uiTokens().divider));
}

/* ---- expander --------------------------------------------------------- */

static void drawChevronUp(ImDrawList* dl, ImVec2 c, ImU32 col)
{
    float w = Px(4), h = Px(2);
    ImVec2 pts[3] = { ImVec2(c.x - w, c.y + h), ImVec2(c.x, c.y - h), ImVec2(c.x + w, c.y + h) };
    dl->AddPolyline(pts, 3, C(col), fmaxf(1.0f, Px(1)));
}

static ImVec2 expanderTrail[2];

bool BeginExpander(const char* id, const char* title, const char* summary, bool* open, const ExpanderStyle& es)
{
    const UiTokens& t = uiTokens();
    CardFrame* c = beginCardFrame(id, Px(es.padLeft), 0, Px(es.bodyBottom), 0);
    if (c == NULL) {
        SectionTitle(title);
        return *open;
    }
    float hh = Px(es.headerH), w = c->width;
    ImVec2 s = c->start;
    ImGui::SetCursorScreenPos(s);
    /* Controls in the trail are submitted after the header and take the mouse from it. */
    ImGuiButtonFlags flags = ImGuiButtonFlags_EnableNav | (es.trailW > 0 ? ImGuiButtonFlags_AllowOverlap : 0);
    bool pressed = ImGui::InvisibleButton("##head", ImVec2(w, hh), flags);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    if (pressed) {
        *open = !*open;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = floorf(s.y + hh / 2), x = s.x + Px(es.padLeft);
    PushFont(UI_FONT_STRONG);
    ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(x, floorf(cy - ts.y / 2)), C(t.text), title);
    PopFont();
    x += ts.x + Px(8);
    float bw = Px(32), bh = fminf(Px(32), hh - Px(4));
    ImVec2 ba(s.x + w - Px(es.padRight) - bw, floorf(cy - bh / 2)), bb(ba.x + bw, ba.y + bh);
    float trailX = ba.x - (es.trailW > 0 ? Px(8) + es.trailW : 0);
    expanderTrail[0] = ImVec2(trailX, s.y);
    expanderTrail[1] = ImVec2(es.trailW > 0 ? ba.x - Px(8) : trailX, s.y + hh);
    if (es.legend) {
        dl->AddRectFilled(ImVec2(x, cy - 1), ImVec2(x + Px(12), cy - 1 + Px(3)), C(es.legend));
        x += Px(16);
    }
    if (summary && *summary) {
        PushFont(UI_FONT_BODY, 12);
        ImVec2 ss = ImGui::CalcTextSize(summary);
        TextFit(dl, ImVec2(x, floorf(cy - ss.y / 2)), trailX - Px(8), summary, t.textSecondary,
                ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip));
        PopFont();
    }
    if (hovered || held) {
        dl->AddRectFilled(ba, bb, C(held ? t.subtlePressed : t.subtleHover), Px(4));
    }
    ImVec2 cc(floorf((ba.x + bb.x) / 2), floorf((ba.y + bb.y) / 2));
    if (HasIcons()) DrawIcon(dl, cc, *open ? UI_ICON_CHEVRON_UP : UI_ICON_CHEVRON_DOWN, 12, t.text);
    else if (*open) drawChevronUp(dl, cc, t.text);
    else drawChevron(dl, cc, t.text);
    ImGui::RenderNavCursor(ImRect(s, ImVec2(s.x + w, s.y + hh)), ImGui::GetItemID());
    if (*open) {
        if (es.divider) dl->AddRectFilled(ImVec2(s.x + 1, s.y + hh), ImVec2(s.x + w - 1, s.y + hh + 1), C(t.divider));
        ImGui::SetCursorScreenPos(ImVec2(s.x + Px(es.padLeft), s.y + hh + (es.divider ? 1 : 0) + Px(es.bodyTop)));
    }
    else {
        c->padBottom = 0;
    }
    return *open;
}

void EndExpander()
{
    EndCard();
}

void ExpanderTrail(ImVec2* a, ImVec2* b)
{
    *a = expanderTrail[0];
    *b = expanderTrail[1];
}

/* ---- inspector pane --------------------------------------------------- */

static struct { ImVec2 p, size; bool body; } pane;

float InspectorRailWidth()
{
    return Px(40);
}

/* >> or <<, as two chevrons in a 16 px box. */
static bool paneButton(const char* id, bool right, const char* tip)
{
    const UiTokens& t = uiTokens();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(Px(32), Px(28)), ImGuiButtonFlags_EnableNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImVec2 q = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill = subtleFill(t, hovered, held, false);
    if (visible(fill)) {
        dl->AddRectFilled(p, q, C(fill), Px(4));
    }
    float k = ImGui::GetStyle().FontScaleDpi;
    ImVec2 o(floorf((p.x + q.x) / 2 - 8 * k), floorf((p.y + q.y) / 2 - 8 * k));
    static const float lefts[2] = { 4.0f, 8.5f };
    for (float x0 : lefts) {
        float a = right ? x0 : x0 + 4, b = right ? x0 + 4 : x0;
        ImVec2 pts[3] = { o + ImVec2(a * k, 4 * k), o + ImVec2(b * k, 8 * k), o + ImVec2(a * k, 12 * k) };
        dl->AddPolyline(pts, 3, C(t.text), fmaxf(1.0f, 1.2f * k));
    }
    ImGui::RenderNavCursor(ImRect(p, q), ImGui::GetItemID());
    ItemTooltip(tip);
    return pressed;
}

bool BeginInspectorPane(const char* id, bool* open, ImVec2 size, const char* closeTip, const char* openTip)
{
    const UiTokens& t = uiTokens();
    bool wasOpen = *open;
    pane.p = ImGui::GetCursorScreenPos();
    pane.size = ImVec2(wasOpen ? size.x : InspectorRailWidth(), size.y);
    pane.body = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b = pane.p + pane.size;
    /* A layer a shade off the window, as Windows 11 puts panes on. */
    ImU32 fill = t.dark ? IM_COL32(255, 255, 255, 8) : IM_COL32(255, 255, 255, 128);
    dl->AddRectFilled(pane.p, b, C(fill), Px(8));
    dl->AddRect(pane.p, b, C(t.cardStroke), Px(8));
    ImGui::PushID(id);
    if (!wasOpen) {
        ImGui::SetCursorScreenPos(ImVec2(pane.p.x + floorf((pane.size.x - Px(32)) / 2), pane.p.y + Px(4)));
        if (paneButton("##open", false, openTip)) *open = true;
        return false;
    }
    ImGui::SetCursorScreenPos(ImVec2(b.x - Px(8) - Px(32), pane.p.y + Px(4)));
    if (paneButton("##close", true, closeTip)) *open = false;
    float top = Px(4) + Px(28) + Px(8);
    ImGui::SetCursorScreenPos(ImVec2(pane.p.x + Px(8), pane.p.y + top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##body", ImVec2(fmaxf(pane.size.x - Px(16), 1.0f), fmaxf(pane.size.y - top - Px(8), 1.0f)));
    ImGui::PopStyleVar();
    pane.body = true;
    return true;
}

void EndInspectorPane()
{
    if (pane.body) {
        ImGui::EndChild();
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(pane.p);
    ImGui::Dummy(pane.size);
}

/* ---- status bar ------------------------------------------------------- */

static char statusLine[1024];
static int  statusFrame = -1;

static void statusFresh()
{
    if (statusFrame != ImGui::GetFrameCount()) {
        statusFrame = ImGui::GetFrameCount();
        statusLine[0] = 0;
    }
}

/* Cut s, filled to size by vsnprintf, back to a whole UTF-8 character. */
static void trimUtf8(char* s, size_t size)
{
    size_t n = strlen(s);
    if (n + 1 < size) return;
    size_t lead = n;
    while (lead > 0 && ((unsigned char)s[lead - 1] & 0xc0) == 0x80) lead--;
    if (lead > 0) {
        unsigned char c = (unsigned char)s[lead - 1];
        size_t need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (n - (lead - 1) < need) s[lead - 1] = 0;
    }
}

void StatusText(const char* fmt, ...)
{
    va_list args;
    statusFresh();
    va_start(args, fmt);
    vsnprintf(statusLine, sizeof(statusLine), fmt, args);
    va_end(args);
    trimUtf8(statusLine, sizeof(statusLine));
}

float StatusBarHeight()
{
    return Px(24);
}

void StatusBar()
{
    const UiTokens& t = uiTokens();
    statusFresh();
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImRect r = win->Rect();
    float h = StatusBarHeight(), padX = Px(16);
    ImVec2 a(r.Min.x, r.Max.y - h), b(r.Max.x, r.Max.y);
    ImDrawList* dl = win->DrawList;
    /* Along the window's edges, past the padding. */
    dl->PushClipRect(a, b, false);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + 1), C(t.divider));
    float textRight = b.x - padX;
    PushFont(UI_FONT_BODY, 12);
    ImVec2 ts = ImGui::CalcTextSize(statusLine);
    float y = floorf(a.y + 1 + (h - 1 - ts.y) / 2);
    ImGui::PushStyleColor(ImGuiCol_Text, t.textSecondary);
    ImGui::RenderTextEllipsis(dl, ImVec2(a.x + padX, y), ImVec2(textRight, y + ts.y), textRight, statusLine, NULL, &ts);
    ImGui::PopStyleColor();
    PopFont();
    dl->PopClipRect();
}

/* ---- canvas ----------------------------------------------------------- */

#define CANVAS_ZOOM_MAX 32

void CanvasAspect(const CanvasState& s, int* ax, int* ay)
{
    bool ok = s.aspectX >= 1 && s.aspectY >= 1;
    *ax = ok ? s.aspectX : 1;
    *ay = ok ? s.aspectY : 1;
}

ImVec2 CanvasScale(const CanvasState& s, float u)
{
    int ax, ay;
    CanvasAspect(s, &ax, &ay);
    float ky = u / (s.lineRows > 1 ? s.lineRows : 1);
    return ImVec2(ky * ax / ay, ky);
}

static const int zoomSteps[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
#define ZOOM_WHOLE_MAX 16  /* - and + step by 1 up to here, then by the steps above */

/* "4x" with U+00D7 MULTIPLICATION SIGN. */
static const char* zoomLabel(int zoom, char* out, size_t size)
{
    snprintf(out, size, "%d\xC3\x97", zoom);
    return out;
}

/* A scale such as Fit shows, "2.4x"; a whole one prints as "3x". */
static const char* scaleLabel(float scale, char* out, size_t size)
{
    char v[16];
    snprintf(v, sizeof(v), "%.1f", scale);
    size_t n = strlen(v);
    if (n > 2 && strcmp(v + n - 2, ".0") == 0) v[n - 2] = 0;
    snprintf(out, size, "%s\xC3\x97", v);
    return out;
}

/* The zoom button keeps one width, whatever it shows. */
static float zoomButtonWidth()
{
    char z[16];
    return fmaxf(DropDownButtonWidth(NULL, zoomLabel(CANVAS_ZOOM_MAX, z, sizeof(z)), true, 64),
                 DropDownButtonWidth(NULL, scaleLabel(CANVAS_ZOOM_MAX - 0.5f, z, sizeof(z)), true, 64));
}

/* The canvas toggles Fit and Grid: glyph, label without the icon font. */
static const char* toggleGlyph(int i)
{
    return i == 0 ? UI_ICON_FIT : UI_ICON_GRID;
}

static const char* toggleFallback(int i)
{
    return i == 0 ? ::ts(TS_FIT) : ::ts(TS_CANVAS_GRID);
}

/* Four items, each but the last followed by Px(4). */
float ZoomControlsWidth()
{
    return Px(28) * 2 + zoomButtonWidth() + IconButtonWidth(toggleGlyph(0), toggleFallback(0)) + Px(4) * 3;
}

void GridToggle(CanvasState& s)
{
    CommandBarItem(IconButtonWidth(toggleGlyph(1), toggleFallback(1)), Px(24));
    if (IconButton("##grid", toggleGlyph(1), toggleFallback(1), ::ts(TS_CANVAS_GRID_TIP), s.grid)) {
        s.grid = !s.grid;
    }
}

/* The scale last frame's picture was shown at, or 0 when it was not shown then. */
static float lastScale(const CanvasState& s)
{
    return s.shownZoom > 0 && s.shownFrame == ImGui::GetFrameCount() - 1 ? s.shownZoom : 0;
}

/* Fit turned off: the menu's step at or below the scale shown, 1x below it. */
static int zoomBelow(const CanvasState& s)
{
    float cur = lastScale(s);
    if (cur <= 0) return s.z().zoom;
    int z = zoomSteps[0];
    for (int step : zoomSteps) {
        if (step <= cur + 0.001f) z = step;
    }
    return z;
}

/* The whole zoom one step in or out from what the picture shows, Fit too; 0 for none. */
static int zoomStepFrom(const CanvasState& s, int dir)
{
    float cur = s.z().fit && s.shownZoom > 0 ? s.shownZoom : (float)s.z().zoom;
    if ((dir > 0 && cur >= ZOOM_WHOLE_MAX - 0.001f) || (dir < 0 && cur > ZOOM_WHOLE_MAX + 0.001f)) {
        int z = 0;
        for (int step : zoomSteps) {
            if (dir > 0 && step > cur + 0.001f) return step;
            if (dir < 0 && step < cur - 0.001f) z = step;
        }
        return z;
    }
    int z = dir > 0 ? (int)floorf(cur + 0.001f) + 1 : (int)ceilf(cur - 0.001f) - 1;
    return z >= 1 && z <= CANVAS_ZOOM_MAX ? z : 0;
}

void ZoomControls(const char* id, CanvasState& s, bool* check, const char* checkLabel)
{
    char z[16];
    ImGui::PushID(id);
    CommandBarItem(Px(28), Px(24));
    /* U+2212 MINUS SIGN: as wide as the plus, unlike a hyphen. */
    CanvasZoom& c = s.z();
    if (IconButton("##out", UI_ICON_REMOVE, "\xe2\x88\x92", ::ts(TS_ZOOM_OUT), false, ImVec2(28, 24), 14) && zoomStepFrom(s, -1)) {
        c.zoom = zoomStepFrom(s, -1);
        c.fit = false;
    }
    float w = zoomButtonWidth();
    CommandBarItem(w);
    /* While Fit is on: the scale it shows, and no step marked. */
    const char* label = c.fit && lastScale(s) > 0 ? scaleLabel(lastScale(s), z, sizeof(z)) : zoomLabel(c.zoom, z, sizeof(z));
    if (BeginDropDownButton("zoom", NULL, label, true, w / ImGui::GetStyle().FontScaleDpi, c.fit ? ::ts(TS_ZOOM_FIT_ON_TIP) : NULL)) {
        for (int step : zoomSteps) {
            if (MenuRadio(zoomLabel(step, z, sizeof(z)), !c.fit && c.zoom == step)) {
                c.zoom = step;
                c.fit = false;
            }
        }
        if (check && checkLabel) {
            MenuSeparator();
            MenuCheck(checkLabel, check);
        }
        EndDropDownButton();
    }
    CommandBarItem(Px(28), Px(24));
    if (IconButton("##in", UI_ICON_ADD, "+", ::ts(TS_ZOOM_IN), false, ImVec2(28, 24), 14) && zoomStepFrom(s, 1)) {
        c.zoom = zoomStepFrom(s, 1);
        c.fit = false;
    }
    CommandBarItem(IconButtonWidth(toggleGlyph(0), toggleFallback(0)), Px(24));
    if (IconButton("##fit", toggleGlyph(0), toggleFallback(0), ::ts(TS_FIT_TIP), c.fit)) {
        if (c.fit) c.zoom = zoomBelow(s);
        c.fit = !c.fit;
    }
    ImGui::PopID();
}

/* Fills a..b; squares are counted from o so they stay put when scrolled. */
static void drawChecker(ImDrawList* dl, ImVec2 o, ImVec2 a, ImVec2 b, float cell)
{
    const UiTokens& t = uiTokens();
    int i0 = (int)floorf((a.x - o.x) / cell);
    int j0 = (int)floorf((a.y - o.y) / cell);
    dl->AddRectFilled(a, b, C(t.checkerA));
    for (int j = j0; o.y + j * cell < b.y; j++) {
        for (int i = i0; o.x + i * cell < b.x; i++) {
            if ((i + j) & 1) {
                ImVec2 q0(fmaxf(o.x + i * cell, a.x), fmaxf(o.y + j * cell, a.y));
                ImVec2 q1(fminf(o.x + (i + 1) * cell, b.x), fminf(o.y + (j + 1) * cell, b.y));
                dl->AddRectFilled(q0, q1, C(t.checkerB));
            }
        }
    }
}

void BeginNearest(ImDrawList* dl)
{
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest) {
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, NULL);
    }
}

void EndNearest(ImDrawList* dl)
{
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_ResetRenderState) {
        dl->AddCallback(pio.DrawCallback_ResetRenderState, NULL);
    }
}

void ImageNearest(ImDrawList* dl, ImTextureID tex, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1)
{
    BeginNearest(dl);
    dl->AddImage(ImTextureRef(tex), a, b, uv0, uv1);
    EndNearest(dl);
}

/* Lines a dot apart over va..vb, from image px ox, oy, a dot sx image px across. */
static void drawGrid(ImDrawList* dl, const CanvasXf& xf, ImVec2 va, ImVec2 vb, float ox = 0, float oy = 0, int sx = 1)
{
    const UiTokens& t = uiTokens();
    float zx = xf.scale.x * sx, zy = xf.scale.y;
    float bx = xf.origin.x + ox * xf.scale.x, by = xf.origin.y + oy * xf.scale.y;
    int x0 = (int)((va.x - bx) / zx), x1 = (int)ceilf((vb.x - bx) / zx);
    int y0 = (int)((va.y - by) / zy), y1 = (int)ceilf((vb.y - by) / zy);
    for (int x = x0; x <= x1; x++) {
        float fx = xf.toScreen(ox + (float)(x * sx), 0).x;
        dl->AddLine(ImVec2(fx, va.y), ImVec2(fx, vb.y), (x & 7) ? C(t.gridLine) : C(t.gridLineMajor));
    }
    for (int y = y0; y <= y1; y++) {
        float fy = xf.toScreen(0, oy + (float)y).y;
        dl->AddLine(ImVec2(va.x, fy), ImVec2(vb.x, fy), (y & 7) ? C(t.gridLine) : C(t.gridLineMajor));
    }
}

void CanvasGrid(ImDrawList* dl, const CanvasXf& xf, ImVec2 a, ImVec2 b)
{
    if (fminf(xf.scale.x, xf.scale.y) >= 4 && a.x < b.x && a.y < b.y) drawGrid(dl, xf, a, b);
}

CanvasHit Canvas(const char* id, ImTextureID tex, int w, int h, CanvasState& state,
                 ImVec2 size, CanvasOverlayFn overlay, void* ref)
{
    const UiTokens& t = uiTokens();
    CanvasHit hit = { false, 0, 0, false, -1, false };
    if (size.y <= 0) {
        size.y = fmaxf(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight());
    }
    CanvasZoom& zs = state.z();
    if (zs.zoom < 1) zs.zoom = 1;
    if (zs.zoom > CANVAS_ZOOM_MAX) zs.zoom = CANVAS_ZOOM_MAX;
    ImVec2 k = CanvasScale(state, 1);
    /* Ctrl+wheel over last frame's picture steps its zoom as - and + do,
    ** keeping the image pixel under the mouse there; the window's zoom stays. */
    ImGuiIO& io = ImGui::GetIO();
    bool over = state.hovered && state.shownFrame == ImGui::GetFrameCount() - 1 && w > 0 && h > 0;
    state.wheel = over && io.KeyCtrl ? state.wheel + io.MouseWheel : 0;
    int z = fabsf(state.wheel) >= 1 ? zoomStepFrom(state, state.wheel > 0 ? 1 : -1) : 0;
    if (fabsf(state.wheel) >= 1) state.wheel -= state.wheel > 0 ? 1 : -1;
    if (z > 0) {
        ImVec2 m = io.MousePos, at = ImGui::GetCursorScreenPos(), well = state.shownWell;
        float gw = state.gutter > 0 ? Px(state.gutter) : 0, iw = floorf(w * (float)z * k.x), ih = floorf(h * (float)z * k.y);
        float px = ImClamp((m.x - state.shownAt.x) / state.shownScale.x, 0.0f, (float)w);
        float py = ImClamp((m.y - state.shownAt.y) / state.shownScale.y, 0.0f, (float)h);
        ImGui::SetNextWindowContentSize(ImVec2(iw + gw, ih));
        ImGui::SetNextWindowScroll(ImVec2(iw + gw > well.x ? fmaxf(0, gw + px * iw / w - (m.x - at.x)) : 0,
                                          ih > well.y ? fmaxf(0, py * ih / h - (m.y - at.y)) : 0));
        zs.zoom = z;
        zs.fit = false;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, state.frameless ? 0 : t.dark ? IM_COL32(0, 0, 0, 51) : IM_COL32(0, 0, 0, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding);
    ImGui::BeginChild(id, size, state.frameless ? ImGuiChildFlags_None : ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (w > 0 && h > 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float gw = state.gutter > 0 ? Px(state.gutter) : 0;
        float u = (float)zs.zoom;
        if (zs.fit) {
            /* The whole space, between zoom steps too. */
            float f = fminf((avail.x - gw) / ((float)w * k.x), avail.y / ((float)h * k.y));
            u = fmaxf(fminf(f, CANVAS_ZOOM_MAX), 0.01f);
        }
        float iw = fmaxf(1.0f, floorf(w * u * k.x)), ih = fmaxf(1.0f, floorf(h * u * k.y));
        /* What was drawn, so overlays and the hit test agree with it. */
        ImVec2 scale(iw / w, ih / h);
        /* Centred while smaller than the well; whole screen pixels, or
        ** nearest sampling splits image pixels unevenly. */
        ImVec2 cur = ImGui::GetCursorScreenPos();
        ImVec2 p0(floorf(cur.x + gw + fmaxf(0, (avail.x - gw - iw) / 2)), floorf(cur.y + fmaxf(0, (avail.y - ih) / 2)));
        ImVec2 p1(p0.x + iw, p0.y + ih);
        ImVec2 cmin = dl->GetClipRectMin(), cmax = dl->GetClipRectMax();
        ImVec2 va(fmaxf(p0.x, cmin.x), fmaxf(p0.y, cmin.y));
        ImVec2 vb(fminf(p1.x, cmax.x), fminf(p1.y, cmax.y));
        bool shown = va.x < vb.x && va.y < vb.y;
        CanvasXf xf;
        xf.origin  = p0;
        xf.scale   = scale;
        xf.clipMin = va;
        xf.clipMax = vb;
        xf.gutterMin = ImVec2(p0.x - gw, p0.y);
        xf.gutterMax = ImVec2(gw > 0 ? p0.x - Px(2) : p0.x, p1.y);

        /* The parts' screen rects within the view; the whole image without parts. */
        int np = state.partCount > 0 ? ImMin(state.partCount, 2) : 1;
        ImVec2 pa[2] = { va, va }, pb[2] = { vb, vb }, po[2] = { p0, p0 };
        for (int i = 0; i < np && state.partCount > 0; i++) {
            const CanvasState::Part& q = state.parts[i];
            po[i] = xf.toScreen(q.x0, q.y0);
            ImVec2 e = xf.toScreen(q.x1, q.y1);
            pa[i] = ImVec2(fmaxf(po[i].x, va.x), fmaxf(po[i].y, va.y));
            pb[i] = ImVec2(fminf(e.x, vb.x), fminf(e.y, vb.y));
        }
        for (int i = 0; shown && state.checker && i < np; i++) {
            if (pa[i].x < pb[i].x && pa[i].y < pb[i].y) drawChecker(dl, po[i], pa[i], pb[i], fmaxf(2.0f, Px(8)));
        }
        if (shown && tex) {
            ImageNearest(dl, tex, p0, p1);
        }
        for (int i = 0; shown && state.grid && i < np; i++) {
            const CanvasState::Part* q = state.partCount > 0 ? &state.parts[i] : NULL;
            int sx = q && q->stepX > 1 ? q->stepX : 1;
            if (fminf(scale.x * sx, scale.y) < 4 || pa[i].x >= pb[i].x || pa[i].y >= pb[i].y) continue;
            drawGrid(dl, xf, pa[i], pb[i], q ? q->x0 : 0, q ? q->y0 : 0, sx);
        }
        if (shown && overlay) {
            overlay(dl, xf, ref);
        }

        ImGui::SetCursorScreenPos(ImVec2(p0.x - gw, p0.y));
        bool released = ImGui::InvisibleButton("##canvas", ImVec2(iw + gw, ih));
        /* Held on the picture or on the well around it (ImGui's move id for the child). */
        bool held = ImGui::IsItemActive() || ImGui::GetActiveID() == ImGui::GetCurrentWindow()->MoveId;
        bool overflows = ImGui::GetScrollMaxX() > 0 || ImGui::GetScrollMaxY() > 0;
        bool start = held && overflows && !state.panning && ImGui::IsMouseDragging(ImGuiMouseButton_Left, Px(4));
        if (start) {
            state.panning = true;
        }
        if (state.panning && held) {
            /* The first step takes the whole drag so far, so the picture stays under the mouse. */
            ImVec2 d = start ? ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0) : ImGui::GetIO().MouseDelta;
            ImGui::SetScrollX(ImGui::GetScrollX() - d.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - d.y);
        }
        if (ImGui::IsItemHovered() && !state.panning) {
            ImVec2 m = ImGui::GetIO().MousePos;
            /* The image pixel the screen pixel under the mouse samples. */
            int x = (int)floorf((m.x + 0.5f - p0.x) / scale.x);
            int y = (int)floorf((m.y + 0.5f - p0.y) / scale.y);
            if (m.x < p0.x && y >= 0 && y < h) {
                hit.gutterY = y;
            }
            else if (x >= 0 && x < w && y >= 0 && y < h) {
                hit.valid   = true;
                hit.x       = x;
                hit.y       = y;
                /* Where a drag could pan, a click is a release that did not pan. */
                hit.clicked = overflows ? released : ImGui::IsItemClicked(ImGuiMouseButton_Left);
            }
        }
        bool middle = ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0);
        if (middle) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ImGui::SetScrollX(ImGui::GetScrollX() - d.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - d.y);
        }
        if (state.panning || (middle && overflows)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        /* The wheel taken from ImGui last frame scrolls by ImGui's steps: down, or across
        ** with Shift or a sideways swipe. */
        if (over && !io.KeyCtrl) {
            ImGuiWindow* cw = ImGui::GetCurrentWindow();
            float stepY = ImTrunc(ImMin(5 * cw->FontRefSize, cw->InnerRect.GetHeight() * 0.67f));
            float stepX = ImTrunc(ImMin(2 * cw->FontRefSize, cw->InnerRect.GetWidth() * 0.67f));
            float wx = (state.wheelOwned & 2) ? io.MouseWheelH : 0, wy = 0;
            if (state.wheelOwned & 1) (io.KeyShift ? wx : wy) += io.MouseWheel;
            if (wx != 0) ImGui::SetScrollX(ImGui::GetScrollX() - wx * stepX);
            if (wy != 0) ImGui::SetScrollY(ImGui::GetScrollY() - wy * stepY);
        }
        /* A click on the well beside the picture, its gutter too, not on a scrollbar, that did not pan. */
        ImVec2 mp = io.MousePos;
        bool beside = (mp.x < p0.x || mp.x >= p1.x || mp.y < p0.y || mp.y >= p1.y) &&
                      ImGui::GetCurrentWindow()->InnerRect.Contains(mp) && ImGui::IsWindowHovered();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) state.pressEmpty = beside;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            hit.clickedEmpty = state.pressEmpty && beside && !state.panning && io.MouseDragMaxDistanceSqr[0] < Px(4) * Px(4);
            state.pressEmpty = false;
        }
        if (!held) state.panning = false;
        state.shownZoom = u;
        state.shownAt = p0;
        state.shownScale = scale;
        state.shownWell = avail;
    }
    /* Next frame's wheel is the picture's on an axis it can scroll, and with Ctrl:
    ** no window scrolls with it then; Ctrl never zooms the window over a picture. */
    state.hovered = w > 0 && h > 0 && ImGui::IsWindowHovered();
    state.shownFrame = ImGui::GetFrameCount();
    bool canX = ImGui::GetScrollMaxX() > 0, canY = ImGui::GetScrollMaxY() > 0;
    bool ownY = state.hovered && (io.KeyCtrl || (io.KeyShift ? canX : canY)), ownX = state.hovered && canX;
    state.wheelOwned = (ownY ? 1 : 0) | (ownX ? 2 : 0);
    if (state.hovered) imguiHostTakeWheel();
    const ImGuiKey axes[2] = { ImGuiKey_MouseWheelY, ImGuiKey_MouseWheelX };
    for (int i = 0; i < 2; i++) {
        if (i == 0 ? ownY : ownX) ImGui::SetKeyOwner(axes[i], ImGui::GetID("##wheel"));
        else if (ImGui::GetKeyOwner(axes[i]) == ImGui::GetID("##wheel")) ImGui::SetKeyOwner(axes[i], ImGuiKeyOwner_NoOwner);
    }
    ImGui::EndChild();
    return hit;
}

int CanvasWheelZoom(CanvasState& state)
{
    ImGuiIO& io = ImGui::GetIO();
    bool over = state.hovered && state.shownFrame == ImGui::GetFrameCount() - 1;
    state.wheel = over && io.KeyCtrl ? state.wheel + io.MouseWheel : 0;
    int z = fabsf(state.wheel) >= 1 ? zoomStepFrom(state, state.wheel > 0 ? 1 : -1) : 0;
    if (fabsf(state.wheel) >= 1) state.wheel -= state.wheel > 0 ? 1 : -1;
    return z;
}

void CanvasShown(CanvasState& state, float zoom, bool hovered)
{
    state.shownZoom = zoom;
    state.shownFrame = ImGui::GetFrameCount();
    state.hovered = hovered;
    /* Ctrl+wheel never zooms the window over the picture. */
    if (hovered) imguiHostTakeWheel();
}

void CanvasSpan(ImDrawList* dl, const CanvasXf& xf, unsigned start, unsigned count, unsigned rowW,
                ImU32 fill, ImU32 line, const char* label)
{
    if (count == 0 || rowW == 0) return;
    unsigned end = start + count, r0 = start / rowW, r1 = (end - 1) / rowW;
    float c0 = (float)(start % rowW), c1 = (float)((end - 1) % rowW + 1), w = (float)rowW, y0 = (float)r0, y1 = (float)r1;
    /* The first row's part, the whole rows, the last row's part. */
    if (r0 == r1) dl->AddRectFilled(xf.toScreen(c0, y0), xf.toScreen(c1, y0 + 1), fill);
    else {
        dl->AddRectFilled(xf.toScreen(c0, y0), xf.toScreen(w, y0 + 1), fill);
        if (r1 > r0 + 1) dl->AddRectFilled(xf.toScreen(0, y0 + 1), xf.toScreen(w, y1), fill);
        dl->AddRectFilled(xf.toScreen(0, y1), xf.toScreen(c1, y1 + 1), fill);
    }
    ImVec2 pts[8];
    int n = 0;
    if (r0 == r1) {
        pts[n++] = xf.toScreen(c0, y0); pts[n++] = xf.toScreen(c1, y0); pts[n++] = xf.toScreen(c1, y0 + 1); pts[n++] = xf.toScreen(c0, y0 + 1);
    }
    else {
        pts[n++] = xf.toScreen(c0, y0); pts[n++] = xf.toScreen(w, y0); pts[n++] = xf.toScreen(w, y1); pts[n++] = xf.toScreen(c1, y1);
        pts[n++] = xf.toScreen(c1, y1 + 1); pts[n++] = xf.toScreen(0, y1 + 1); pts[n++] = xf.toScreen(0, y0 + 1); pts[n++] = xf.toScreen(c0, y0 + 1);
    }
    for (int i = 0; i < n; i++) pts[i] = ImVec2(pts[i].x + 0.5f, pts[i].y + 0.5f);
    dl->AddPolyline(pts, n, line, fmaxf(1.0f, Px(1)), ImDrawFlags_Closed);
    if (label == NULL || label[0] == 0) return;
    /* On the whole rows when the first row holds only its end. */
    bool below = r1 > r0 && c0 > 0 && (r1 > r0 + 1 || c1 > c0);
    ImVec2 a = below ? xf.toScreen(0, y0 + 1) : xf.toScreen(c0, y0), b = xf.toScreen(w, y1 + 1);
    const UiTokens& t = uiTokens();
    PushFont(UI_FONT_BODY, 11);
    ImVec2 ts = ImGui::CalcTextSize(label);
    if (b.y - a.y >= ts.y + Px(2) && b.x - a.x >= ts.x + Px(8)) {
        dl->AddRectFilled(a, ImVec2(a.x + ts.x + Px(8), a.y + ts.y + Px(2)), WithAlpha(t.layer, 200));
        dl->AddText(ImVec2(a.x + Px(4), a.y + Px(1)), t.text, label);
    }
    PopFont();
}

void CanvasAddressRuler(ImDrawList* dl, const CanvasXf& xf, int rows, unsigned rowBytes)
{
    if (xf.gutterMax.x <= xf.gutterMin.x || rows <= 0) return;
    const UiTokens& t = uiTokens();
    PushFont(UI_FONT_MONO, 11);
    float lh = ImGui::GetTextLineHeight();
    int step = 16;
    while (step < rows && xf.scale.y * step < lh + Px(4)) step *= 4;
    /* From the first label that reaches the clip rect down. */
    float top = dl->GetClipRectMin().y, bottom = dl->GetClipRectMax().y;
    /* Scrolled across, the ruler stays at the view's left edge, on a backing over the bytes. */
    float dx = fmaxf(0, dl->GetClipRectMin().x - xf.gutterMin.x), gx = xf.gutterMax.x + dx;
    if (dx > 0) {
        dl->AddRectFilled(ImVec2(xf.gutterMin.x + dx, fmaxf(top, xf.gutterMin.y)), ImVec2(gx + Px(2), fminf(bottom, xf.gutterMax.y)),
                          WithAlpha(t.layer, 230));
    }
    int r = (int)floorf((top - lh - xf.origin.y) / xf.scale.y);
    r = r < 0 ? 0 : r - r % step;
    for (; r < rows; r += step) {
        float y = xf.toScreen(0, (float)r).y;
        if (y > bottom) break;
        char lab[16];
        snprintf(lab, sizeof(lab), "%05X", (unsigned)r * rowBytes);
        ImVec2 ts = ImGui::CalcTextSize(lab);
        dl->AddLine(ImVec2(gx - Px(3), y), ImVec2(gx, y), t.textTertiary);
        dl->AddText(ImVec2(gx - ts.x - Px(5), y), t.textSecondary, lab);
    }
    PopFont();
}

}
