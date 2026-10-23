/*****************************************************************************
**
** Value search in RAM and VRAM.
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
#ifndef CONTROL_HUNT_H
#define CONTROL_HUNT_H

#include "ControlAddressType.h"
#include "ControlView.h"

#define CTRL_HUNT_U8  0
#define CTRL_HUNT_U16 1
#define CTRL_HUNT_U32 2
#define CTRL_HUNT_S8  3
#define CTRL_HUNT_S16 4
#define CTRL_HUNT_S32 5

/* Against the previous pass. */
#define CTRL_HUNT_CHANGED      0
#define CTRL_HUNT_UNCHANGED    1
#define CTRL_HUNT_INCREASED    2
#define CTRL_HUNT_DECREASED    3
#define CTRL_HUNT_INCREASED_BY 4
#define CTRL_HUNT_DECREASED_BY 5
/* Against a number. */
#define CTRL_HUNT_EQ           6
#define CTRL_HUNT_NE           7
#define CTRL_HUNT_LT           8
#define CTRL_HUNT_LE           9
#define CTRL_HUNT_GT          10
#define CTRL_HUNT_GE          11
#define CTRL_HUNT_IN_RANGE    12

typedef struct CtrlHunt CtrlHunt;

typedef struct {
    CtrlAddr addr;
    UInt32   prevValue;
    UInt32   curValue;
    Int32    z80Address;   /* -1 when that segment is paged out right now */
} CtrlHuntCandidate;

#define CTRL_HUNT_MAX 4

/* Takes a held view. width is a CTRL_HUNT_* type (size and signedness); stride 0 tests every byte offset.
** NULL when denied, without a held view, on a bad width, when nothing is
** scannable, when CTRL_HUNT_MAX hunts exist, or when allocation failed. */
CtrlHunt* ctrlHuntStart(CtrlView* view, int width, int stride);

/* Samples memory and drops every candidate that fails compare; only the sample needs the view.
** Returns 0 when the machine has changed, there is no held view, or the caller is denied. */
int ctrlHuntRefine(CtrlHunt* hunt, CtrlView* view, int compare,
                   Int32 operand, Int32 operand2, UInt32* remaining);

UInt32 ctrlHuntCandidateCount(const CtrlHunt* hunt);

/* The CTRL_HUNT_* width the hunt was started with. */
int ctrlHuntWidth(const CtrlHunt* hunt);

/* Fills out with up to max candidates from the first'th survivor; returns how many. */
int ctrlHuntList(const CtrlHunt* hunt, UInt32 first, UInt32 max,
                 CtrlHuntCandidate* out);

/* 0 once a reset, cartridge change or savestate load has happened since the hunt began. */
int ctrlHuntIsValid(const CtrlHunt* hunt);

/* Frees the hunt without an activity entry: only the agent's own hunt.end logs one. */
void ctrlHuntAbandon(CtrlHunt* hunt);

/* Bytes the hunt scans. */
UInt32 ctrlHuntScannableSize(const CtrlHunt* hunt);

/* The screen mode at the start, whose layout of video memory the hunt reads and names. */
int ctrlHuntVramMode(const CtrlHunt* hunt);

/* Non-zero when video memory was left out: that mode interleaves more than the VDP has. */
int ctrlHuntVramLeftOut(const CtrlHunt* hunt);

const char* ctrlHuntCompareText(int compare);

#endif
