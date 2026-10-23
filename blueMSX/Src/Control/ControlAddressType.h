/*****************************************************************************
**
** MSX address types.
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
#ifndef CONTROL_ADDRESS_TYPE_H
#define CONTROL_ADDRESS_TYPE_H

#include "MsxTypes.h"

/* CPU: what the Z80 sees now, 0..0xFFFF. SLOT: a slot's storage, paged in or
** not; with CTRL_SEGMENT_FLAT the offset is from the bottom of a slot that
** reports no segments. VRAM: video memory, which is in no slot. */

#define CTRL_SPACE_CPU  1
#define CTRL_SPACE_SLOT 2
#define CTRL_SPACE_VRAM 3

#define CTRL_SEGMENT_FLAT (-1)

typedef struct {
    UInt8  space;
    UInt8  slot;
    UInt8  subSlot;
    Int32  segment;
    UInt32 offset;
} CtrlAddr;

#endif
