/*****************************************************************************
**
** Interface between the ImGui tool window host and the C++ tool code.
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
#ifndef IMGUI_HOST_H
#define IMGUI_HOST_H

#ifndef __cplusplus
#error "ImGuiHost.h is for C++ tool code; C code uses ImGuiWindow.h"
#endif

#include "ImGuiWindow.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ImFont;
struct UiTokens;

/* Text roles of the themed windows. */
enum UiFont {
    UI_FONT_BODY,     /* Segoe UI */
    UI_FONT_STRONG,   /* Segoe UI Semibold */
    UI_FONT_MONO,     /* Cascadia Mono, or Consolas */
    UI_FONT_COUNT
};

/* The device every tool window draws with; NULL while none is open. */
ID3D11Device*        imguiHostDevice();
ID3D11DeviceContext* imguiHostContext();
/* Changes whenever the device is created anew, e.g. after a device loss. */
unsigned             imguiHostDeviceGeneration();

/* The window whose ImGui context is current, or NULL. */
ImGuiToolWindow*     imguiHostCurrentWindow();
/* Whether the window is drawn with a dark style. */
bool                 imguiHostIsDark(ImGuiToolWindow* w);
/* The colours of a themed window; NULL for one in ImGui's own dark style. */
const UiTokens*      imguiHostTokens(ImGuiToolWindow* w);
/* A window in ImGui's own style has one font for every role. */
ImFont*              imguiHostFont(ImGuiToolWindow* w, UiFont role);
/* The size to give PushFont for text whose em is `em` px at 96 DPI. */
float                imguiHostFontSize(ImGuiToolWindow* w, UiFont role, float em);

#endif
