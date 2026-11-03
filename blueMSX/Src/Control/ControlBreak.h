/*****************************************************************************
**
** Breakpoint retention across agent reconnects.
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
#ifndef CONTROL_BREAK_H
#define CONTROL_BREAK_H

#include "MsxTypes.h"

/* At a hello, before its session: owner is the number the hello gives, or 0. Returns how
** many of a client's plain breakpoints were dropped, told to their owner and to one other
** owner; above CTRL_PLAIN_LIST_MAX means more than that. */
int  ctrlBreakOpen(int owner);

/* A greeted client left without session.end: its plain breakpoints are dropped once the
** grace passes, unless a hello comes first. nowMs is a millisecond tick that may wrap. */
void ctrlBreakLeft(UInt32 nowMs);
/* Called often while no request is served; ends the grace once it has passed. */
void ctrlBreakTick(UInt32 nowMs);
/* No grace runs while stopped, so Agent Control Off ends a departed client's breakpoints now. */
void ctrlBreakDropDeparted(void);
/* For session.end: the client's plain breakpoints end now; no later hello counts them. */
void ctrlBreakDropPlain(void);

#endif
