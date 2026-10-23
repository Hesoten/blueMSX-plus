/*****************************************************************************
**
** Debugger event listener for Agent Control.
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
#include "ControlDebuggerListener.h"
#include "ControlStopLog.h"
#include "ControlInstance.h"
#include "Debugger.h"
#include "Emulator.h"
#include "Properties.h"
#include <stddef.h>

/* Only at a start is it the machine that runs: a state load replaces the one asked for. */
static void onStart(void* ref)
{
    ctrlInstanceSetMachine(propGetGlobalProperties()->emulation.machineName);
}

/* The stop transition was made on this thread just before. */
static void onStop(void* ref)
{
    ctrlStopPost(CTRL_STOP_MACHINE, emulatorOwnStateSeq());
}

static void onPause(void* ref, int cause, UInt32 runGeneration)
{
    ctrlStopPost(ctrlStopReasonOf(cause), runGeneration);
}

void ctrlDebuggerListenerCreate(void)
{
    debuggerCreateListener(onStart, onStop, onPause, NULL);
}
