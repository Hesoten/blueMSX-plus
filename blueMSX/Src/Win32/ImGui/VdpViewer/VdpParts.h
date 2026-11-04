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

namespace parts {

/* An icon toggle of the command bar; label is shown without the icon font. */
void barToggle(const char* id, const char* glyph, const char* label, const char* tip, bool* on);

/* Diagonal lines over a..b, for parts the chip does not draw. */
void hatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col);
/* Dashes dash px on and off from a to b, across or down: only those that reach the
** clip rect, so a zoomed picture costs what is seen. */
void dashedSegment(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float dash);


/* text in a table cell, as wide as itself and cut where the cell is clipped; on hover the
** whole text when cut, then tip (NULL: none). */
void fitCell(const char* text, const char* tip = NULL);

/* The widest of n texts in the monospace font, as a fixed table column's width: without it
** the column fits the cells of the frame before, which on its first frame are none. */
float monoWidth(const char* const* texts, int n);
/* A secondary line that wraps at the card's edge. */
void note(const char* text);

}

#endif
