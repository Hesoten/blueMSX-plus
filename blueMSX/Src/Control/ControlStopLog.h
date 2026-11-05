/*****************************************************************************
**
** Machine stop log.
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
#ifndef CONTROL_STOP_LOG_H
#define CONTROL_STOP_LOG_H

#include "MsxTypes.h"

#define CTRL_STOP_UNKNOWN    0
#define CTRL_STOP_USER       1  /* somebody pressed pause                  */
#define CTRL_STOP_STEP       2  /* a single step finished                  */
#define CTRL_STOP_BREAKPOINT 3  /* a breakpoint fired                      */
#define CTRL_STOP_MACHINE    4  /* the machine stopped or was replaced     */
#define CTRL_STOP_WATCH      6  /* a watchpoint fired; watchType says where */
#define CTRL_STOP_VRAM_TIMING 7 /* the VDP was accessed faster than it can be */
#define CTRL_STOP_STEP_BACK  8  /* a step back reached its moment          */
#define CTRL_STOP_FRAME      9  /* a frame step reached a shown frame's end */

typedef struct {
    UInt32 seq;
    UInt8  reason;
    UInt32 runGeneration;
    UInt8  watchType;   /* a DbgDeviceType, for CTRL_STOP_WATCH only */
    UInt32 watchAddress;
    Int8   watchMode;   /* the screen mode at a VIDEO hit, whose layout watchAddress indexes */
    UInt8  watchKind;   /* the DBG_WATCH_ kind of watchpoint that matched */
} CtrlStop;

void ctrlStopLogCreate(void);

/* The reason a pause's cause (a DBG_STOP_ value) is logged as. */
int ctrlStopReasonOf(int cause);

/* Logs a stop of the run generation it ended; the watch fields for a watch only. */
void ctrlStopPost(int reason, UInt32 runGeneration, int watchType, UInt32 watchAddress,
                  int watchMode, int watchKind);

/* The highest sequence number written so far. */
UInt32 ctrlStopSeq(void);

/* The first stop numbered above afterSeq. Non-zero when there was one. */
int ctrlStopGet(UInt32 afterSeq, CtrlStop* out);

/* The same, but waits up to timeoutMs for one; takes no lock while it waits.
** Gives up early, returning 0, once agent control is turned off. */
int ctrlStopWait(UInt32 afterSeq, int timeoutMs, CtrlStop* out);

/* The newest stop, only while the machine is still in it. Non-zero when there is one. */
int ctrlStopLatest(CtrlStop* out);

/* Rewrites the reason of a logged stop, while the ring still holds it. Call only while paused. */
void ctrlStopRelabel(UInt32 seq, int reason);

/* How many stops have been overwritten in the ring, read or not. */
UInt32 ctrlStopDropped(void);

const char* ctrlStopReasonText(int reason);

#endif
