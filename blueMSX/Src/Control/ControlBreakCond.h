/*****************************************************************************
**
** Breakpoint conditions: the breakpoints an agent sets, with their conditions' texts.
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
#ifndef CONTROL_BREAK_COND_H
#define CONTROL_BREAK_COND_H

#include "MsxTypes.h"
#include "ControlJson.h"
#include "ControlStopLog.h"
#include "ControlText.h"
#include "Debugger.h"

/* A plain breakpoint at a Z80 address (segment -1), or one at an offset into a segment. */
typedef struct {
    int    slot;
    int    subSlot;
    int    segment;
    UInt32 at;
} CtrlBreakCondAt;

/* Sets it, replacing its condition with cond (NULL for none) and zeroing its counts; when,
** the text cond came from or NULL, is kept for listing and stops. 0 when there is no room. */
int  ctrlBreakCondSet(const CtrlBreakCondAt* at, const DbgBreakCondition* cond, const char* when);
/* Non-zero when one set here was there. */
int  ctrlBreakCondClear(const CtrlBreakCondAt* at);
/* Segment ones, and plain ones set here. */
void ctrlBreakCondClearAll(void);
/* Only the plain ones set here. */
void ctrlBreakCondClearPlain(void);
/* Whether a breakpoint the debugger window or a plugin set is there too, which stops every
** time; for a plain one also such a segment one paged in at that address. */
int  ctrlBreakCondOthersStop(const CtrlBreakCondAt* at);

/* For a listed breakpoint with a condition or skip: its when, hits, matched and skip. */
void ctrlBreakCondDescribeCounts(CtrlText* text, const CtrlBreakCondAt* at,
                                 const DbgBreakCounts* counts);
/* For a stop where a breakpoint with a condition or skip stopped, also one a step ended at:
** a line, and hits and matched in out. */
void ctrlBreakCondDescribeStop(CtrlText* text, JsonOut* out, const CtrlStop* stop);

#endif
