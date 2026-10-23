/*****************************************************************************
**
** Run state op handlers.
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
#include "ControlOp.h"
#include "ControlStopLog.h"
#include "ControlStatus.h"
#include "Debugger.h"
#include "Emulator.h"
#include "Properties.h"
#include "R800Debug.h"

int ctrlOpStatus(CtrlCall* call)
{
    CtrlStop stop;
    int reason = CTRL_STOP_UNKNOWN;
    int stateAsFound = (int)emulatorGetState();

    /* The stop the machine is in now, not the oldest unread one; seen here, it is read. */
    if (ctrlStopLatest(&stop)) {
        reason = stop.reason;
        if (stop.seq > call->session->stopCursor) {
            call->session->stopCursor = stop.seq;
        }
    }
    ctrlStatusDescribe(&call->text, reason, stateAsFound);
    /* Registers and the CPU only from a paused machine. */
    if (call->view.held == DBG_ACCESS_VIEW) {
        /* A turbo R switches CPU, and CLKH/CLKL count the clock of the one running. */
        ctrlTextAdd(&call->text, "cpu:     %s\n", r800DebugIsR800() ? "R800" : "Z80");
    }
    else if (stateAsFound != EMU_STOPPED) {
        /* What a running machine may say: which one it is, and how far it has run. */
        ctrlTextAdd(&call->text, "machine: %s\n", propGetGlobalProperties()->emulation.machineName);
        ctrlTextAdd(&call->text, "frames:  %u\n", (unsigned)emulatorGetFramesRun());
        ctrlTextAdd(&call->text, "(registers and CPU omitted: the machine is running; pause "
                    "to read registers)\n");
    }
    jsonOutUInt(&call->out, "stop_seq", ctrlStopSeq());
    jsonOutUInt(&call->out, "machine_generation", dbgMachineGeneration());
    return ctrlReplyDone(call);
}
