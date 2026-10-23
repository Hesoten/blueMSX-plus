/*****************************************************************************
**
** Memory and VRAM access by slot and segment.
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
#ifndef CONTROL_MEMORY_H
#define CONTROL_MEMORY_H

#include "ControlAddressType.h"
#include "ControlView.h"

#define CTRL_MEM_OK          0
#define CTRL_MEM_BADADDR     1  /* no such place on this machine           */
#define CTRL_MEM_PASTEND     2  /* starts inside, runs past the end        */
#define CTRL_MEM_NOTPRESENT  3  /* real address, nothing answering there   */
#define CTRL_MEM_PAGEDOUT    4  /* only reachable while it is paged in     */
#define CTRL_MEM_READONLY    5
#define CTRL_MEM_NOVIEW      6  /* no view of the machine stopped is held  */
#define CTRL_MEM_TOOBIG      7
#define CTRL_MEM_DENIED      8  /* cut off at the emulator by the user     */
#define CTRL_MEM_NOVRAM      9  /* no video memory there in this screen mode */

/* What a read (or a write) of size bytes at addr would refuse, found from the layout alone,
** so a request can be refused on its arguments before it needs the machine paused. byPaging
** is set when that answer follows what is paged in now. */
int ctrlMemCheck(const CtrlAddr* addr, UInt32 size, int isWrite, int* byPaging);

/* Reads size bytes at addr, across segments if needed. On failure dst is untouched. */
int ctrlMemRead(CtrlView* view, const CtrlAddr* addr, UInt8* dst, UInt32 size);

/* A segment address may run into the next segment; a CPU address goes to the
** bus unclamped. */
int ctrlMemWrite(CtrlView* view, const CtrlAddr* addr, const UInt8* src, UInt32 size);

/* Why a result code came back, in words that can be shown to whoever asked. */
const char* ctrlMemErrorText(int result);

/* Bytes left from addr to the end of the space it sits in. */
UInt32 ctrlMemRemaining(const CtrlAddr* addr);

#endif
