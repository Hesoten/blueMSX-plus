/*****************************************************************************
**
** Slot and bank layout.
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
#ifndef CONTROL_SLOTS_H
#define CONTROL_SLOTS_H

#include "ControlText.h"
#include "MsxTypes.h"

/* One row per Z80 page, then every banked device and how to address its segments,
** then the slots with contents but no segments that are not paged in. */
void ctrlSlotsDescribe(CtrlText* text);

/* "seg5", or for MAPPER_BANK_SRAM "the SRAM" in a sentence and "SRAM" in a column. */
void ctrlSlotsBankName(char* out, int len, int segment, int inSentence);

/* One line for each bank register that shows other than the program's own selection: the
** footnote to the '*' of the slots table and the status, or what a snapshot keeps. Nothing
** without a view. */
#define CTRL_SLOTS_BANKS_FOOTNOTE 0
#define CTRL_SLOTS_BANKS_SNAPSHOT 1
void ctrlSlotsDescribeSwitched(CtrlText* text, int kind);

/* Each Z80 page's slot and segment. */
void ctrlSlotsDescribePages(CtrlText* text);

/* The slot and subslot paged in at a Z80 address now, and the segment its 8 kB bank
** shows there, or -1 when the slot reports none. */
Int32 ctrlSlotsPagedIn(UInt16 z80, int* slot, int* subSlot);
/* The same slot and subslot alone. */
void  ctrlSlotsPageSlot(UInt16 z80, int* slot, int* subSlot);

/* Says that a slot with contents reports no segments and what to use instead; no newline. */
void ctrlSlotsDescribeNoSegments(CtrlText* text, int slot, int subSlot);

#endif
