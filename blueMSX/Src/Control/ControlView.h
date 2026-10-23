/*****************************************************************************
**
** Read access to a paused or running machine.
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
#ifndef CONTROL_VIEW_H
#define CONTROL_VIEW_H

#include "MsxTypes.h"
#include "Debugger.h"

/* What may be read: a view (DBG_ACCESS_VIEW) of a paused machine reads the CPU, memory and
** devices, a hold (DBG_ACCESS_HOLD) of any running one only its layout and the last frame.
** Holding either, nothing may wait on the window's thread: its changes wait for it. */

/* How long one waits for the emulation thread to park, or for the window to finish a change. */
#define CTRL_VIEW_TIMEOUT_MS 250

typedef struct {
    int held;   /* DBG_ACCESS_VIEW or DBG_ACCESS_HOLD, 0 when none is held */
} CtrlView;

/* DBG_ACCESS_OK with the view or hold taken, else why not; a view refuses a running machine. */
int  ctrlViewTake(CtrlView* view, int kind, int timeoutMs);
/* A view of a paused machine, else a hold. */
int  ctrlViewTakeBest(CtrlView* view, int timeoutMs);
void ctrlViewRelease(CtrlView* view);

/* A snapshot of the devices, only while this thread holds a view; NULL otherwise. */
DbgSnapshot* ctrlViewDevices(void);

#endif
