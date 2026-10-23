/*****************************************************************************
**
** Address argument of an op call.
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
#ifndef CONTROL_ADDRESS_ARG_H
#define CONTROL_ADDRESS_ARG_H

#include "ControlCall.h"

/* Reads "at", a register name too, into addr, and tells in the text how it was
** read; refuses one missing, unreadable, or in a slot with contents but no segments. A
** register a running machine cannot give marks atUnread, which the refusals below refuse. */
#define CTRL_ADDR_ARG_NEEDS_MACHINE 1   /* refused with no machine */
int  ctrlAddrArgRead(CtrlCall* call, int flags, CtrlAddr* addr);
/* The address is not on this machine, or atUnread; info is filled otherwise. */
int  ctrlAddrArgResolve(CtrlCall* call, const CtrlAddr* addr, CtrlAddrInfo* info);
/* A segment past the slot's last one, where there is nothing to read or write; or atUnread. */
int  ctrlAddrArgRefusePastEnd(CtrlCall* call, const CtrlAddr* addr);
/* A vram: range with a byte that has no video memory behind it in that screen mode. */
int  ctrlAddrArgRefuseNoVram(CtrlCall* call, UInt32 address, UInt32 size, int mode);

/* Says in the text how a typed address was read: which mapper ram: took. */
void ctrlAddrArgTellHowRead(CtrlCall* call, const char* typed, const CtrlAddr* addr);

#endif
