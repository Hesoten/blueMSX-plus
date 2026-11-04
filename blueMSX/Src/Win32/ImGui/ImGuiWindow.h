/*****************************************************************************
**
** Top-level tool windows drawn with Dear ImGui on Direct3D 11.
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
#ifndef IMGUI_WINDOW_H
#define IMGUI_WINDOW_H

/* Plain C on purpose: including imgui.h here would let a file built
** without IMGUI_USER_CONFIG see a different ImGui. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ImGuiToolWindow ImGuiToolWindow;
struct ToolWindowPlacement;

/* Called once per frame, inside an ImGui window that fills the client area. */
typedef void (*ImGuiToolDrawFn)(ImGuiToolWindow* w, void* ref);
/* Called once the window is gone; ref is no longer used after it returns. */
typedef void (*ImGuiToolClosedFn)(void* ref);

enum {
    /* Keep ImGui's own dark style whatever the Windows theme is. */
    IMGUI_TOOL_ALWAYS_DARK = 1 << 0
};

typedef struct {
    const char*       id;      /* one window per id */
    const char*       title;   /* UTF-8 */
    int               width;   /* client size at 96 DPI */
    int               height;
    int               flags;   /* IMGUI_TOOL_* */
    ImGuiToolDrawFn   draw;
    ImGuiToolClosedFn closed;  /* may be NULL */
    void*             ref;
    /* Where the window opens and its zoom, written back as they change; NULL
    ** opens it over the main window at width x height. */
    struct ToolWindowPlacement* placement;
} ImGuiToolDesc;

/* Opens a window, or brings the one with the same id to the front.
** Returns NULL on failure. */
ImGuiToolWindow* imguiToolOpen(const ImGuiToolDesc* desc);
/* Safe inside the window's own draw: it closes when the frame ends. */
void  imguiToolClose(ImGuiToolWindow* w);
void  imguiToolCloseAll(void);
int   imguiToolIsOpen(const char* id);
/* DPI scale times the user's zoom. */
float imguiToolScale(ImGuiToolWindow* w);

#ifdef __cplusplus
}
#endif

#endif
