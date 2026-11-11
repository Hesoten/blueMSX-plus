/*****************************************************************************
**
** V9968 command registers on the VDP Viewer's Command card and Bitmap tab.
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
#ifndef VDP_CMD_9968_H
#define VDP_CMD_9968_H

#include "imgui.h"
#include "ImGuiWidgets.h"
#include "VdpDecode.h"

/* The Bitmap tab's page grid, in canvas pixels: page p at column p % cols. */
struct CmdPageMap {
    int pages, cols, tileW, tileH, gap;
};

/* The Command card's SX, SY / DX, DY / NX, NY lines; on a native V9968 at their
** own widths, signed in LRMM, then VX, VY and the LRMM read window. */
void cmdKeyValues(const VdpDebugState* s);

/* On the Bitmap tab read as readMode: where the LRMM last started reads, its read
** window and where it writes, while the command engine works in that mode. */
void cmdLrmmOverlay(ImDrawList* dl, const ui::CanvasXf& xf, const VdpDebugState* s, int readMode,
                    const CmdPageMap& g);

/* The bitmap mode the command engine works in at the frame's end, or -1. */
int cmdEngineMode(const VdpDebugState* s);

/* Cuts a..b down to the box 0..w by 0..h; false when none of it is inside. */
bool cmdClipToBox(ImVec2* a, ImVec2* b, float w, float h);

#endif
