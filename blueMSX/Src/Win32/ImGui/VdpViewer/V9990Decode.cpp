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
#include "V9990Decode.h"

bool v9990LayerOf(const V9990DebugState* s, int layer, V9990Layer* out)
{
    bool p2 = s->mode == V9990_MODE_P2;
    if ((s->mode != V9990_MODE_P1 && !p2) || layer < 0 || layer > (p2 ? 0 : 1)) return false;
    const UInt8* r = s->regs;
    V9990Layer l = {};
    if (layer == 0) {
        l.sx = (int)((r[19] | ((UInt32)r[20] << 3)) & (p2 ? 0x3ff : 0x1ff));
        l.sy = (int)((r[17] | ((UInt32)s->scrollAYHi << 8)) & 0x1ff);
    }
    else {
        l.sx = (int)((r[23] | ((UInt32)r[24] << 3)) & 0x1ff);
        l.sy = (int)((r[21] | ((UInt32)(r[22] & 1) << 8)) & 0x1ff);
    }
    *out = l;
    return true;
}

uint32_t v9990Rgba(UInt16 c, bool ysClear)
{
    uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    uint32_t a = ysClear && (c & 0x8000) ? 0 : 0xff;
    return (a << 24) | (b << 16) | (g << 8) | r;
}
