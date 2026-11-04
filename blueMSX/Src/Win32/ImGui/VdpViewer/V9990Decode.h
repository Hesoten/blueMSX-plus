/*****************************************************************************
**
** The layers and colours of a V9990, without ImGui.
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
#ifndef V9990_DECODE_H
#define V9990_DECODE_H

#include <stdint.h>
extern "C" {
#include "V9990.h"
}

/* Layer 0 is P1's A or P2's only layer, 1 is P1's B. */
struct V9990Layer {
    int    sx, sy;        /* the scroll: the image dot at the screen's top left */
};

/* False unless the chip is in P1 or P2 and the layer is one of the mode's. */
bool   v9990LayerOf(const V9990DebugState* s, int layer, V9990Layer* out);

/* An xRGB 1:5:5:5 colour as 0xAABBGGRR; ysClear: clear where YS is set. */
uint32_t v9990Rgba(UInt16 c, bool ysClear);

#endif
