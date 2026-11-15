/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Z80/R800Debug.h,v $
**
** $Revision: 1.4 $
**
** $Date: 2008-03-30 18:38:48 $
**
** Author: Daniel Vik
**
** Description: Debugger support for an R800 object
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
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
#ifndef R800_DEBUG_H
#define R800_DEBUG_H
 
#include "MsxTypes.h"
#include "R800.h"

typedef struct R800Debug R800Debug;

void r800DebugCreate(R800* r800);
void r800DebugDestroy();

/* Whether the running CPU is in R800 mode. Only while a machine exists. */
int r800DebugIsR800(void);

/* Whether the CPU waits in a HALT, where a breakpoint fires only on arriving. */
int r800DebugWaitsInHalt(void);

/* Whether the CPU's /INT or /NMI line is held low. Only while a machine exists. */
int r800DebugIntLow(void);
int r800DebugNmiLow(void);

/* Whether the CPU is parked in a breakpoint stop (a step over or out ends in one too),
** between two instructions with the breakpoint at PC already tested. */
int r800DebugStoppedBeforeInstruction(void);

/* The PCs the CPU ran, for a reader while the CPU is stopped. Entry i is
** pc[i & (PC_TRACE_SIZE - 1)], from oldest to newest; newest is the one running or next.
** changedAt[p] is the entry during which 8kB page p last showed other memory. */
typedef struct {
    const UInt16* pc;
    UInt32        oldest;
    UInt32        newest;
    int           pagesTracked;     /* 0: changedAt is not kept on this machine */
    UInt32        changedAt[8];
} R800DebugHistory;

/* Non-zero with out filled while a machine exists and records the PC trace. */
int r800DebugHistoryGet(R800DebugHistory* out);

/* Keeps changedAt while on; for a machine whose memory goes through the slot manager.
** Off during a state load, whose mapping comes with the changedAt saved with it. */
void r800DebugTrackPages(int on);

/* Whether the CPU is parked inside an instruction (at a watchpoint or a VRAM access check
** it made) rather than between two, as at a breakpoint or a device stop in a timer. */
int r800DebugInsideInstruction(void);

#endif
