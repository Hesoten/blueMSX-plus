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
#ifndef IMGUI_THEME_H
#define IMGUI_THEME_H

#include "imgui.h"

/* Whether Windows asks apps for a dark theme. */
bool imguiThemeIsDark();

/* The Windows 11 colours with a meaning. Translucent ones are meant to be
** drawn over `layer`, the window background. */
struct UiTokens {
    bool  dark;
    ImU32 layer;
    ImU32 cardFill;
    ImU32 cardStroke;
    ImU32 controlFill;
    ImU32 controlFillHover;
    ImU32 controlFillPressed;
    ImU32 controlStroke;
    ImU32 controlStrongStroke;
    ImU32 elevationEdge;     /* a 1 px line on the top (dark) or bottom (light) of a control */
    bool  elevationAtBottom;
    ImU32 subtleHover;
    ImU32 subtlePressed;
    ImU32 subtleSelected;
    ImU32 text;
    ImU32 textSecondary;
    ImU32 textTertiary;
    ImU32 accent;
    ImU32 accentHover;
    ImU32 accentPressed;
    ImU32 textOnAccent;
    ImU32 accentDisabled;    /* a control on but unavailable, and its text */
    ImU32 textOnAccentDisabled;
    ImU32 divider;
    ImU32 focusOuter;
    ImU32 focusInner;
    ImU32 success;
    ImU32 caution;
    ImU32 critical;
    ImU32 flash;             /* the start of a change highlight; it fades to clear */
    ImU32 checkerA;          /* the two squares behind transparent pixels */
    ImU32 checkerB;
    ImU32 gridLine;
    ImU32 gridLineMajor;
    ImU32 series[6];         /* tell data series apart */
};

/* The accent Windows uses for the theme, or the Windows 11 default. Reads
** the registry, so call it when a window opens or its theme is rebuilt. */
ImU32 imguiThemeReadAccent(bool dark);

void imguiThemeBuildTokens(UiTokens* out, bool dark, ImU32 accent);
/* The style at 96 DPI for a body font of fontHeight; the host scales it. */
void imguiThemeBuildStyle(ImGuiStyle* out, const UiTokens& tokens, float fontHeight);

/* For the window being drawn. */
const UiTokens& uiTokens();

/* A shadow and an edge round each tooltip, menu and drop-down of ctx, which ImGui
** itself draws flat; call between EndFrame and the building of the draw data. */
void imguiThemeDecoratePopups(ImGuiContext* ctx, bool dark, float scale);
/* That shadow and edge round the box a-b of dl. The shadow reaches depth past each side,
** and drop further down and less far up. */
void imguiThemeDrawPopupFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, bool dark, float scale);
void imguiThemePopupShadow(float scale, float* depth, float* drop);

#endif
