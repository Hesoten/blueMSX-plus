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
    /* For a parsed ram: address: why it named this slot (CTRL_RAM_*), and the slot page 3
    ** showed then, so a reply says what the parse saw rather than reading page 3 again. */
    UInt8  ramWhy;
    UInt8  ramPage3Slot;
    UInt8  ramPage3SubSlot;
} CtrlAddr;

/* An address resolved against the machine as it stands now. */
#define CTRL_CONTENTS_MAX 128

typedef struct {
    CtrlAddr canonical;   /* always CTRL_SPACE_SLOT or CTRL_SPACE_VRAM     */
    Int32    z80Address;  /* -1 when the Z80 cannot reach it right now     */
    Int32    z80Page;     /* -1 likewise                                   */
    Int32    segmentSize; /* in bytes, 0 if none is reported               */
    Int32    segmentCount;/* segments in the slot, -1 if unknown           */
    UInt32   spaceSize;   /* bytes the whole space holds, 0 if unpublished */
    int      readable;    /* the bytes can be produced at all              */
    int      writable;
    char     contents[CTRL_CONTENTS_MAX];
} CtrlAddrInfo;

/* Why "ram:" names the slot it does: CtrlAddr.ramWhy. */
#define CTRL_RAM_NONE  0   /* it names nothing */
#define CTRL_RAM_PAGE3 1   /* the memory mapper in page 3 now, the RAM the program runs in */
#define CTRL_RAM_MAIN  2   /* page 3 is no memory mapper: the machine's own main RAM */

#endif
