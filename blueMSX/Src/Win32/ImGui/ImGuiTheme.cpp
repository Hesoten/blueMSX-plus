/*****************************************************************************
**
** Light and dark styles for the ImGui tool windows, following Windows.
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
#include <windows.h>
#include <math.h>
#include "ImGuiTheme.h"
#include "imgui_internal.h"
#include "ImGuiHost.h"
extern "C" {
#include "../Win32Common.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

static ImU32 rgb(unsigned int c, float a = 1.0f)
{
    return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, (int)(a * 255 + 0.5f));
}

/* White or black at a percentage of opacity, as Windows 11 states them. */
static ImU32 W(float pct) { return rgb(0xffffff, pct / 100); }
static ImU32 B(float pct) { return rgb(0x000000, pct / 100); }

static ImU32 withAlpha(ImU32 c, float a)
{
    return (c & ~IM_COL32_A_MASK) | ((ImU32)(a * 255 + 0.5f) << IM_COL32_A_SHIFT);
}

/* c at opacity a over the opaque colour under. */
static ImU32 over(ImU32 c, ImU32 under, float a)
{
    int r = 0, g = 0, b = 0;
    for (int s = 0; s < 24; s += 8) {
        float v = ((c >> s) & 0xff) * a + ((under >> s) & 0xff) * (1 - a);
        int n = (int)(v + 0.5f);
        if (s == 0) r = n; else if (s == 8) g = n; else b = n;
    }
    return IM_COL32(r, g, b, 255);
}

bool imguiThemeIsDark()
{
    return win32CommonIsDarkMode() != FALSE;
}

/* AccentPalette holds 8 RGBA entries, lightest first: Windows fills with the
** second (Light2) on dark and the fifth (Dark1) on light. */
ImU32 imguiThemeReadAccent(bool dark)
{
    BYTE  pal[32];
    DWORD size = sizeof(pal);
    HKEY  key;
    ImU32 accent = dark ? rgb(0x4cc2ff) : rgb(0x0067c0);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"AccentPalette", NULL, NULL, pal, &size) == ERROR_SUCCESS && size >= 32) {
            const BYTE* e = pal + (dark ? 1 : 4) * 4;
            accent = IM_COL32(e[0], e[1], e[2], 255);
        }
        RegCloseKey(key);
    }
    return accent;
}

void imguiThemeBuildTokens(UiTokens* t, bool dark, ImU32 accent)
{
    static const unsigned int darkSeries[6]  = { 0x5ec4b6, 0x8ab4f8, 0xf7b955, 0xf28fad, 0xc3a6ff, 0xff9e7a };
    static const unsigned int lightSeries[6] = { 0x00796b, 0x1a5fb4, 0x9d5d00, 0xb4235a, 0x6b3fb8, 0xb5451b };
    t->dark   = dark;
    t->accent = accent;
    if (dark) {
        t->layer               = rgb(0x202020);
        t->cardFill            = W(5.12f);
        t->cardStroke          = W(6.98f);
        t->controlFill         = W(6.05f);
        t->controlFillHover    = W(8.37f);
        t->controlFillPressed  = W(3.26f);
        t->controlStroke       = W(6.98f);
        t->controlStrongStroke = W(54.42f);
        t->elevationEdge       = W(9.3f);
        t->elevationAtBottom   = false;
        t->subtleHover         = W(6.05f);
        t->subtlePressed       = W(4.19f);
        t->subtleSelected      = W(6.05f);
        t->text                = W(100);
        t->textSecondary       = W(78.6f);
        t->textTertiary        = W(36.28f);
        t->textOnAccent        = rgb(0x000000);
        t->accentDisabled      = W(15.81f);
        t->textOnAccentDisabled = W(53.02f);
        t->divider             = W(8.37f);
        t->focusOuter          = W(100);
        t->focusInner          = B(70);
        t->success             = rgb(0x6ccb5f);
        t->caution             = rgb(0xfce100);
        t->critical            = rgb(0xff99a4);
        t->flash               = withAlpha(accent, 0.35f);
        t->checkerA            = rgb(0x3a3a3a);
        t->checkerB            = rgb(0x2a2a2a);
        t->gridLine            = W(16);
        t->gridLineMajor       = W(35);
    }
    else {
        t->layer               = rgb(0xf3f3f3);
        t->cardFill            = W(70);
        t->cardStroke          = B(5.78f);
        t->controlFill         = W(70);
        t->controlFillHover    = rgb(0xf9f9f9, 0.5f);
        t->controlFillPressed  = rgb(0xf9f9f9, 0.3f);
        t->controlStroke       = B(5.78f);
        t->controlStrongStroke = B(44.58f);
        t->elevationEdge       = B(16.22f);
        t->elevationAtBottom   = true;
        t->subtleHover         = B(3.73f);
        t->subtlePressed       = B(2.41f);
        t->subtleSelected      = B(3.73f);
        t->text                = B(89.56f);
        t->textSecondary       = B(60.63f);
        t->textTertiary        = B(36.14f);
        t->textOnAccent        = rgb(0xffffff);
        t->accentDisabled      = B(21.69f);
        t->textOnAccentDisabled = rgb(0xffffff);
        t->divider             = B(5.88f);
        t->focusOuter          = B(89.56f);
        t->focusInner          = W(100);
        t->success             = rgb(0x0f7b0f);
        t->caution             = rgb(0x9d5d00);
        t->critical            = rgb(0xc42b1c);
        t->flash               = withAlpha(accent, 0.25f);
        t->checkerA            = rgb(0xffffff);
        t->checkerB            = rgb(0xe0e0e0);
        t->gridLine            = B(16);
        t->gridLineMajor       = B(35);
    }
    t->accentHover   = over(accent, t->layer, 0.9f);
    t->accentPressed = over(accent, t->layer, 0.8f);
    for (int i = 0; i < 6; i++) {
        t->series[i] = rgb(dark ? darkSeries[i] : lightSeries[i]);
    }
}

static void buildSizes(ImGuiStyle* s, float fontHeight)
{
    s->WindowPadding      = ImVec2(12, 12);
    s->WindowRounding     = 0;
    s->WindowBorderSize   = 0;
    s->ChildRounding      = 8;
    s->ChildBorderSize    = 1;
    s->PopupRounding      = 8;
    s->PopupBorderSize    = 0;    /* the edge comes with the shadow */
    /* Controls are 24 px high, as Windows 11 compact density. */
    s->FramePadding       = ImVec2(8, fontHeight < 24 ? (24 - fontHeight) / 2 : 0);
    s->FrameRounding      = 4;
    s->FrameBorderSize    = 1;
    s->ItemSpacing        = ImVec2(8, 6);
    s->ItemInnerSpacing   = ImVec2(6, 4);
    s->CellPadding        = ImVec2(6, 3);
    s->IndentSpacing      = 18;
    s->ScrollbarSize      = 12;
    s->ScrollbarRounding  = 8;
    s->ScrollbarPadding   = 3;
    s->GrabMinSize        = 10;
    s->GrabRounding       = 8;
    s->TabRounding        = 8;
    s->TabBorderSize      = 0;
    s->TabBarBorderSize   = 0;
    s->TabBarOverlineSize = 0;
    s->SelectableRounding = 4;
    s->MenuItemRounding   = 4;
    s->DisabledAlpha      = 0.4f;
    s->SeparatorTextBorderSize = 1;
    s->SeparatorTextPadding    = ImVec2(0, 3);
}

void imguiThemeBuildStyle(ImGuiStyle* out, const UiTokens& t, float fontHeight)
{
    ImVec4* c = out->Colors;
    *out = ImGuiStyle();
    if (t.dark) {
        ImGui::StyleColorsDark(out);
    }
    else {
        ImGui::StyleColorsLight(out);
    }
    buildSizes(out, fontHeight);

#define SET(col, u32) c[col] = ImGui::ColorConvertU32ToFloat4(u32)
    SET(ImGuiCol_Text,                 t.text);
    SET(ImGuiCol_TextDisabled,         t.textSecondary);
    SET(ImGuiCol_WindowBg,             t.layer);
    SET(ImGuiCol_ChildBg,              0);
    SET(ImGuiCol_PopupBg,              t.dark ? rgb(0x2c2c2c) : rgb(0xf9f9f9));
    SET(ImGuiCol_Border,               t.cardStroke);
    SET(ImGuiCol_BorderShadow,         0);
    SET(ImGuiCol_FrameBg,              t.controlFill);
    SET(ImGuiCol_FrameBgHovered,       t.controlFillHover);
    SET(ImGuiCol_FrameBgActive,        t.controlFillPressed);
    SET(ImGuiCol_TitleBg,              t.layer);
    SET(ImGuiCol_TitleBgActive,        t.layer);
    SET(ImGuiCol_TitleBgCollapsed,     t.layer);
    SET(ImGuiCol_MenuBarBg,            t.layer);
    SET(ImGuiCol_ScrollbarBg,          0);
    SET(ImGuiCol_ScrollbarGrab,        withAlpha(t.controlStrongStroke, t.dark ? 0.35f : 0.30f));
    SET(ImGuiCol_ScrollbarGrabHovered, t.controlStrongStroke);
    SET(ImGuiCol_ScrollbarGrabActive,  t.textSecondary);
    SET(ImGuiCol_CheckMark,            t.text);
    SET(ImGuiCol_CheckboxSelectedBg,   t.accent);
    SET(ImGuiCol_SliderGrab,           t.accent);
    SET(ImGuiCol_SliderGrabActive,     t.accentPressed);
    SET(ImGuiCol_Button,               t.controlFill);
    SET(ImGuiCol_ButtonHovered,        t.controlFillHover);
    SET(ImGuiCol_ButtonActive,         t.controlFillPressed);
    SET(ImGuiCol_Header,               t.subtleSelected);
    SET(ImGuiCol_HeaderHovered,        t.subtleHover);
    SET(ImGuiCol_HeaderActive,         t.subtlePressed);
    SET(ImGuiCol_Separator,            t.divider);
    SET(ImGuiCol_SeparatorHovered,     withAlpha(t.accent, 0.6f));
    SET(ImGuiCol_SeparatorActive,      t.accent);
    SET(ImGuiCol_ResizeGrip,           0);
    SET(ImGuiCol_ResizeGripHovered,    withAlpha(t.accent, 0.6f));
    SET(ImGuiCol_ResizeGripActive,     t.accent);
    SET(ImGuiCol_InputTextCursor,      t.text);
    SET(ImGuiCol_Tab,                  0);
    SET(ImGuiCol_TabHovered,           t.subtleHover);
    SET(ImGuiCol_TabSelected,          t.cardFill);
    SET(ImGuiCol_TabSelectedOverline,  t.accent);
    SET(ImGuiCol_TabDimmed,            0);
    SET(ImGuiCol_TabDimmedSelected,    t.cardFill);
    SET(ImGuiCol_TabDimmedSelectedOverline, t.textTertiary);
    SET(ImGuiCol_PlotLines,            t.accent);
    SET(ImGuiCol_PlotHistogram,        t.accent);
    SET(ImGuiCol_TableHeaderBg,        0);
    SET(ImGuiCol_TableBorderStrong,    t.divider);
    SET(ImGuiCol_TableBorderLight,     t.divider);
    SET(ImGuiCol_TableRowBg,           0);
    SET(ImGuiCol_TableRowBgAlt,        t.dark ? W(2.33f) : B(2.33f));
    SET(ImGuiCol_TextLink,             t.accent);
    SET(ImGuiCol_TextSelectedBg,       withAlpha(t.accent, t.dark ? 0.35f : 0.25f));
    SET(ImGuiCol_DragDropTarget,       t.accent);
    SET(ImGuiCol_NavCursor,            t.text);
#undef SET
}

const UiTokens& uiTokens()
{
    static UiTokens fallback[2];
    static bool made;
    ImGuiToolWindow* w = imguiHostCurrentWindow();
    const UiTokens* t = w ? imguiHostTokens(w) : NULL;
    if (t) {
        return *t;
    }
    if (!made) {
        made = true;
        imguiThemeBuildTokens(&fallback[0], false, rgb(0x0067c0));
        imguiThemeBuildTokens(&fallback[1], true, rgb(0x4cc2ff));
    }
    return fallback[(w ? imguiHostIsDark(w) : imguiThemeIsDark()) ? 1 : 0];
}

/* Rings from the outline outwards, fading as (1 - f)^2, each a step lower. */
static void shadowRing(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float depth, float drop, float peak)
{
    enum { CORNER = 10, LOOPS = 8, PER = 4 * (CORNER + 1) };
    static const float start[4] = { IM_PI, IM_PI * 1.5f, 0.0f, IM_PI * 0.5f };
    ImVec2 uv = dl->_Data->TexUvWhitePixel;
    r = fminf(r, fminf(b.x - a.x, b.y - a.y) * 0.5f);
    dl->PrimReserve(LOOPS * PER * 6, (LOOPS + 1) * PER);
    unsigned base = dl->_VtxCurrentIdx;
    for (int k = 0; k <= LOOPS; k++) {
        float f = (float)k / LOOPS, rr = r + depth * f, oy = drop * f;
        ImVec2 c[4] = { ImVec2(a.x + r, a.y + r + oy), ImVec2(b.x - r, a.y + r + oy),
                        ImVec2(b.x - r, b.y - r + oy), ImVec2(a.x + r, b.y - r + oy) };
        ImU32 col = IM_COL32(0, 0, 0, (int)(peak * (1 - f) * (1 - f) * 255 + 0.5f));
        for (int q = 0; q < 4; q++) {
            for (int s = 0; s <= CORNER; s++) {
                float t = start[q] + (IM_PI * 0.5f) * s / CORNER;
                dl->PrimWriteVtx(ImVec2(c[q].x + cosf(t) * rr, c[q].y + sinf(t) * rr), uv, col);
            }
        }
    }
    for (int k = 0; k < LOOPS; k++) {
        for (int i = 0; i < PER; i++) {
            unsigned i0 = base + k * PER + i, i1 = base + k * PER + (i + 1) % PER;
            dl->PrimWriteIdx((ImDrawIdx)i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + PER)); dl->PrimWriteIdx((ImDrawIdx)(i1 + PER));
            dl->PrimWriteIdx((ImDrawIdx)i0); dl->PrimWriteIdx((ImDrawIdx)(i1 + PER)); dl->PrimWriteIdx((ImDrawIdx)i1);
        }
    }
}

void imguiThemePopupShadow(float scale, float* depth, float* drop)
{
    *depth = floorf(16 * scale);
    *drop  = floorf(4 * scale);
}

void imguiThemeDrawPopupFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, bool dark, float scale)
{
    float depth, drop;
    imguiThemePopupShadow(scale, &depth, &drop);
    shadowRing(dl, a, b, rounding, depth, drop, dark ? 0.70f : 0.20f);
    dl->AddRect(a, b, dark ? IM_COL32(255, 255, 255, 31) : IM_COL32(0, 0, 0, 31), rounding, 1.0f);
}

void imguiThemeDecoratePopups(ImGuiContext* ctx, bool dark, float scale)
{
    for (ImGuiWindow* w : ctx->Windows) {
        if (!w->Active || w->Hidden) continue;
        ImGuiWindowFlags f = w->Flags;
        if (!(f & (ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_Popup))) continue;
        if (f & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoBackground)) continue;
        /* Drawn after the window's own content, but only outside it and on its edge. */
        ImDrawList* dl = w->DrawList;
        if (dl->_Splitter._Count > 1) dl->ChannelsMerge();
        ImRect vp = w->Viewport->GetMainRect();
        dl->PushClipRect(vp.Min, vp.Max, false);
        ImVec2 a = w->Pos, b(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
        imguiThemeDrawPopupFrame(dl, a, b, w->WindowRounding, dark, scale);
        dl->PopClipRect();
    }
}
