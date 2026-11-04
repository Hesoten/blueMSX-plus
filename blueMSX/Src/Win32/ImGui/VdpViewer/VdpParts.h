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
#ifndef VDP_PARTS_H
#define VDP_PARTS_H

#include "imgui.h"
#include "ImGuiWidgets.h"

#define UI_ICON_DROPPED "\xEE\x9E\xBA"  /* U+E7BA Warning */

namespace parts {

/* An icon toggle of the command bar; label is shown without the icon font. */
void barToggle(const char* id, const char* glyph, const char* label, const char* tip, bool* on);
/* A command bar toggle drawn as the sprite ship with decor. */
void barArtToggle(const char* id, ui::PixelDecor decor, const char* tip, bool* on);

/* A chip naming a part at a, where the part has room for it. */
void partChip(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text);

/* Diagonal lines over a..b, for parts the chip drops or does not draw. */
void hatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col);
/* Dashes dash px on and off from a to b, across or down: only those that reach the
** clip rect, so a zoomed picture costs what is seen. */
void dashedSegment(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float dash);
void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th);
/* The theme's checker over a..b, its squares counted from a so the pattern does not move. */
void drawCheckerRect(ImDrawList* dl, ImVec2 a, ImVec2 b);

/* text in a table cell, as wide as itself and cut where the cell is clipped; on hover the
** whole text when cut, then tip (NULL: none). */
void fitCell(const char* text, const char* tip = NULL);

/* A tooltip held still while the mouse rests on one item: new content on a new item, else at most
** twice a second, and never narrower than it has been on that item. */
struct SteadyTip { ImGuiID id = 0; int frame = -2; double at = 0; float w = 0; char text[2048] = ""; };
/* Begins t's tooltip for item id; true when it takes new content now. */
bool steadyTipBegin(SteadyTip& t, ImGuiID id);
/* Ends t's tooltip, noting its width as the auto-fit counts it. */
void steadyTipEnd(SteadyTip& t);
void steadyTooltip(SteadyTip& t, ImGuiID id, const char* text);
/* The widest of n texts in the monospace font, as a fixed table column's width: without it
** the column fits the cells of the frame before, which on its first frame are none. */
float monoWidth(const char* const* texts, int n);

/* A secondary line that wraps at the card's edge. */
void note(const char* text);
/* A view with nothing to show: one muted sentence where the picture goes. */
void emptyView(const char* text);
/* A row of pattern cells: the number, the tile, then a band of chips under it. */
float sprPatPitch(float tile);
/* cv's Grid over a picture at a..b, scaleX and scaleY screen px a dot. */
void canvasGrid(const ui::CanvasState& cv, ImDrawList* dl, ImVec2 a, ImVec2 b, float scaleX, float scaleY);

}

#endif
