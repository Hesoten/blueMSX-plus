/*****************************************************************************
**
** VRAM by screen mode.
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
#ifndef CONTROL_VRAM_H
#define CONTROL_VRAM_H

#include "ControlText.h"
#include "MsxTypes.h"

/* The BASIC SCREEN a VDP mode number shows, as "SCREEN 0 WIDTH 80, 80 column text" for 13. */
const char* ctrlVramModeName(int mode);

/* "SCREEN 8": how a message names a screen mode, and so the layout of video memory in it. */
void ctrlVramModeShortName(char* out, int len, int mode);

/* The first of size vram: addresses from address on with no video memory behind it in
** that screen mode, or -1 when every one has. */
Int32 ctrlVramUnbacked(UInt32 address, UInt32 size, int vramMode);

/* Non-zero when that screen mode interleaves video memory with a second 64 kB bank this
** VDP does not have, so every other vram: address in it has no memory behind it. A mode
** of -1, one not read, counts as one that interleaves. */
int ctrlVramLacksBank(int vramMode);

/* "vram:0x8001 has no video memory behind it: ...", for an address ctrlVramUnbacked found
** in that screen mode; no newline. */
void ctrlVramDescribeUnbacked(CtrlText* text, UInt32 address, int mode);

#endif
