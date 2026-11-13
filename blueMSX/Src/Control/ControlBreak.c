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
#include "ControlBreak.h"
#include "ControlProtocol.h"
#include "Debugger.h"

/* The owner number the last client gave in its hello, or 0. */
static int         lastOwner;

/* Plain breakpoints dropped after their client left without coming back, kept for the hello
** of droppedOwner, the owner that set them; one hello from another owner is also told. */
static int         droppedAfterLeaving;

static int         droppedOwner;

static int         droppedToldOther;

/* How long a client that left without session.end has to come back and keep its plain
** breakpoints; the help, the bp.set reply and the bmsx_break description say 5 s. */
#define LEFT_GRACE_MS 5000

/* Set when a greeted client left, until a hello comes or LEFT_GRACE_MS passes. */
static int         leftPending;

static UInt32      leftAtMs;

/* With no request being served, for a client that left without session.end and did not
** come back in time: drops the plain breakpoints it kept, and a later hello counts them. */
static void dropDeparted(void)
{
    UInt16 kept[CTRL_PLAIN_LIST_MAX + 1];
    int    count = dbgListKeptBreakpoints(kept, CTRL_PLAIN_LIST_MAX + 1);

    if (count == 0) {
        return;
    }
    dbgClearKeptBreakpoints();
    /* One owner's count at a time: an earlier owner's that was not yet told is lost. */
    if (droppedOwner != lastOwner) {
        droppedAfterLeaving = 0;
    }
    droppedOwner         = lastOwner;
    droppedToldOther     = 0;
    droppedAfterLeaving += count;
}

void ctrlBreakLeft(UInt32 nowMs)
{
    leftPending = 1;
    leftAtMs    = nowMs;
}

void ctrlBreakTick(UInt32 nowMs)
{
    if (leftPending && nowMs - leftAtMs >= LEFT_GRACE_MS) {
        leftPending = 0;
        dropDeparted();
    }
}

void ctrlBreakDropDeparted(void)
{
    leftPending = 0;
    dropDeparted();
}

void ctrlBreakDropPlain(void)
{
    dbgClearKeptBreakpoints();
}

int ctrlBreakOpen(int owner)
{
    int dropped = 0;

    leftPending = 0;
    /* A cancelled call reconnects the same MCP server, which keeps its plain breakpoints;
    ** any other client starts without them, as it never set them. */
    if (owner == 0 || owner != lastOwner) {
        dropDeparted();
    }
    /* Another owner is told once, as it may be the same conversation with a restarted server. */
    if (owner == droppedOwner) {
        dropped = droppedAfterLeaving;
        droppedAfterLeaving = 0;
    }
    else if (!droppedToldOther) {
        dropped = droppedAfterLeaving;
        droppedToldOther = 1;
    }
    lastOwner = owner;
    return dropped;
}
