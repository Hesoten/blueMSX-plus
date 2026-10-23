/*****************************************************************************
**
** MSX address parsing and resolution.
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
#ifndef CONTROL_ADDRESS_H
#define CONTROL_ADDRESS_H

#include "ControlAddressType.h"
#include "ControlText.h"

/* Parses 0xC000, C000h, $C000, #C000, 49152, cpu:0xC000, slot1.0:seg5+0x100, 1.0:seg5+0x100,
** slot3.0+0x4000, ram:seg12+0x1A30 or vram:0x1B00, and a register such as pc or hl+2 once
** ctrlAddrSubstituteRegister has rewritten it. Returns 0 and fills err otherwise. */
int ctrlAddrParse(const char* text, CtrlAddr* addr, char* err, int errLen);

/* Rewrites a leading register name ("pc", "hl+2") as its value. Returns 1 when it did, or
** when text names no register; 0 when a register was named and could not be read; -1 when
** the result is too long. */
int ctrlAddrSubstituteRegister(char* text, int textLen, char* err, int errLen);

/* Room for any message ctrlAddrParse writes to err. */
#define CTRL_ADDR_ERR_MAX 400

/* Formats addr as text; len should be at least 48. */
void ctrlAddrPrint(const CtrlAddr* addr, char* text, int len);
/* slotS.U:segN+0xOOOO, or with slotWord 0 the shorter S.U:segN+0xOOOO; both parse back.
** Returns what snprintf does. */
int  ctrlAddrPrintSegment(char* text, int len, int slotWord, int slot, int subSlot,
                          int segment, UInt32 offset);

/* Fills info with the canonical address and what is known about it; z80Address
** is -1 when paged out. Returns 0 when the address cannot be resolved. */
int ctrlAddrResolve(const CtrlAddr* addr, CtrlAddrInfo* info);

/* Whether a resolved address names a segment beyond the last one its slot publishes. */
int ctrlAddrPastEnd(const CtrlAddrInfo* info);

void ctrlAddrDescribe(CtrlText* text, const CtrlAddr* addr);

/* "slot3.2:seg40+0x0000 is past the end: slot3.2 has segments 0-31", for a resolved
** address that ctrlAddrPastEnd holds past the end; no newline. */
void ctrlAddrDescribePastEnd(CtrlText* text, const CtrlAddrInfo* info);

#endif
