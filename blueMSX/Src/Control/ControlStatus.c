/*****************************************************************************
**
** Machine status text for replies.
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
#include "ControlStatus.h"
#include "ControlView.h"
#include "ControlVram.h"
#include "ControlCpu.h"
#include "ControlSlots.h"
#include "ControlStopLog.h"
#include "ControlSymbol.h"
#include "Debugger.h"
#include "Emulator.h"
#include "VDP.h"
#include "V9990.h"
#include <stdio.h>

static void describeRegisters(CtrlText* text, const DbgRegisterBank* regs,
                              const char* label)
{
    UInt32 i;
    int onLine = 0;
    int printed = 0;

    if (regs == NULL) {
        return;
    }
    for (i = 0; i < regs->count; i++) {
        int digits;

        if (regs->reg[i].name[0] == 0) {
            continue;
        }
        /* Here, not before the loop: a bank of only unnamed entries prints nothing. */
        if (printed == 0) {
            ctrlTextAdd(text, "%-8s", label);
        }
        if (onLine == 10) {
            ctrlTextAdd(text, "\n        ");
            onLine = 0;
        }
        /* Not the declared width alone: the VDP's 8 bit scanline entry holds up to 312. */
        digits = regs->reg[i].width > 8 || regs->reg[i].value > 0xff ? 4 : 2;
        ctrlTextAdd(text, " %s=%0*X", regs->reg[i].name, digits,
                    (unsigned)regs->reg[i].value);
        onLine++;
        printed++;
    }
    if (printed > 0) {
        ctrlTextAdd(text, "\n");
    }
}

/* The state line: running, paused or stopped, and what stopped it. */
static void statusState(CtrlText* text, int stopReason, EmuState state)
{
    ctrlTextAdd(text, "state:   %s",
                state == EMU_RUNNING ? "running" :
                state == EMU_PAUSED  ? "paused"  :
                state == EMU_STOPPED ? "stopped" : "suspended");
    if (stopReason != CTRL_STOP_UNKNOWN) {
        /* The JSON reason keeps the short word; this line says what it covers. */
        ctrlTextAdd(text, "   stopped by: %s",
                    stopReason == CTRL_STOP_MACHINE ? "machine stopped, reset or replaced"
                                                    : ctrlStopReasonText(stopReason));
    }
    ctrlTextAdd(text, "\n");
}

/* The nearest symbol before pc, seen from the segment paged in there. */
static void statusPcSymbol(CtrlText* text, Int32 pc)
{
    CtrlSymHome home;
    CtrlSymNear near;
    int slot;
    int subSlot;

    home.segment = ctrlSlotsPagedIn((UInt16)pc, &slot, &subSlot);
    home.slot    = (UInt8)slot;
    home.subSlot = (UInt8)subSlot;

    if (ctrlSymNearest(&home, (UInt16)pc, &near)) {
        ctrlTextAdd(text, "pc:     0x%04X  ", (unsigned)pc);
        if (near.segment >= 0) {
            ctrlTextAdd(text, "seg%d:", (int)near.segment);
        }
        ctrlTextAdd(text, "%s", near.name);
        if (near.offset > 0) {
            ctrlTextAdd(text, "+0x%X", near.offset);
        }
        ctrlTextAdd(text, "\n");
    }
}

/* What the status reads under when the request holds nothing: a view of a paused machine, waited
** for when the request found it paused (it may not have parked yet), else a hold of its layout.
** Returns DBG_ACCESS_OK with a view, else why there is none. */
static int statusAccess(CtrlView* own, int stateAsFound)
{
    int result = DBG_ACCESS_NOT_PAUSED;
    int held;

    if (dbgAccessHeld() != 0) {
        return dbgAccessHeld() == DBG_ACCESS_VIEW ? DBG_ACCESS_OK : DBG_ACCESS_NOT_PAUSED;
    }
    if (emulatorGetState() == EMU_PAUSED) {
        result = ctrlViewTake(own, DBG_ACCESS_VIEW,
                              stateAsFound == EMU_PAUSED ? CTRL_VIEW_TIMEOUT_MS : 0);
    }
    if (result == DBG_ACCESS_NOT_PAUSED || result == DBG_ACCESS_BUSY) {
        held = ctrlViewTake(own, DBG_ACCESS_HOLD, 0);
        if (result == DBG_ACCESS_NOT_PAUSED) {
            result = held == DBG_ACCESS_OK ? result : held;
        }
    }
    return result;
}

/* Why no registers or pages were read, from a statusAccess result. */
static const char* statusNotRead(int access, int noMachine)
{
    return noMachine || access == DBG_ACCESS_NO_MACHINE ? "no machine is running" :
           access == DBG_ACCESS_BUSY     ? "the emulation thread did not stop in time" :
           access == DBG_ACCESS_CHANGING ? "the emulator window is changing the machine" :
                                           "not while the machine runs";
}

void ctrlStatusDescribe(CtrlText* text, int stopReason, int stateAsFound)
{
    DbgSnapshot* snapshot;
    /* No machine now is the state, whatever the request found. */
    int   noMachine = stateAsFound == EMU_STOPPED || emulatorGetState() == EMU_STOPPED;
    CtrlView own = { 0 };
    Int32 pc = -1;
    int   access;

    if (stateAsFound != CTRL_STATUS_STATE_UNSAID) {
        statusState(text, stopReason, noMachine ? EMU_STOPPED : (EmuState)stateAsFound);
    }

    access = statusAccess(&own, stateAsFound);
    snapshot = ctrlViewDevices();
    if (snapshot != NULL) {
        pc = ctrlCpuPcIn(snapshot);
        describeRegisters(text, ctrlCpuRegisterBankIn(snapshot), "regs:");
        dbgSnapshotDestroy(snapshot);
    }
    else {
        ctrlTextAdd(text, "%-8s not readable; %s\n", "regs:", statusNotRead(access, noMachine));
    }

    if (pc >= 0 && ctrlSymCount() > 0) {
        statusPcSymbol(text, pc);
    }

    /* With no machine the slot map reads 0.0 everywhere, which is not a layout. */
    if (noMachine) {
        ctrlTextAdd(text, "pages:   none, no machine is running\n");
    }
    else if (dbgAccessHeld() == 0) {
        ctrlTextAdd(text, "pages:   not read: %s\n", statusNotRead(access, 0));
    }
    else {
        ctrlSlotsDescribePages(text);
        ctrlSlotsDescribeSwitched(text, CTRL_SLOTS_BANKS_FOOTNOTE);
    }
    ctrlViewRelease(&own);
}

/* Whether a V9990 picture is on the screen; needs a view. */
static void describeV9990(CtrlText* text)
{
    static const char* modeNames[V9990_MODE_NONE] = {
        "P1", "P2", "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "STANDBY (black)"
    };
    int       superimposed;
    V9990Mode mode = v9990GetShownMode(&superimposed);

    if (mode < 0 || mode >= V9990_MODE_NONE) {
        ctrlTextAdd(text, "v9990:   its picture is not shown; the vdp lines below are the "
                    "VDP's, not the V9990's\n");
        return;
    }
    ctrlTextAdd(text, "v9990:   its picture is shown in mode %s%s; the vdp lines below are "
                "the VDP's, not the V9990's\n", modeNames[mode],
                superimposed ? ", superimposed over the VDP picture"
                             : " instead of the VDP picture");
}

void ctrlStatusDescribeVdp(CtrlText* text)
{
    DbgSnapshot* snapshot = ctrlViewDevices();
    const DbgDevice* vdp;

    if (snapshot == NULL) {
        return;
    }
    if (v9990IsPresent()) {
        describeV9990(text);
    }
    vdp = dbgSnapshotFindDeviceWithRegisters(snapshot, DBGTYPE_VIDEO);
    if (vdp != NULL) {
        int   mode = vdpDebugGetScreenMode();
        UInt8 registers[2] = { 0, 0 };
        int   raw;

        vdpDebugGetRegisters(registers, 2);
        raw = ((registers[0] & 0x0e) >> 1) | (registers[1] & 0x18);
        /* updateScreenMode names only these; it draws other mixes blank or from other tables. */
        switch (raw) {
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x04:
        case 0x05: case 0x07: case 0x08: case 0x10: case 0x12:
            ctrlTextAdd(text, "vdp:     %s, mode %d: %s\n", vdp->name, mode,
                        ctrlVramModeName(mode));
            break;
        default:
            ctrlTextAdd(text, "vdp:     %s, mode bits 0x%02X (R#0/R#1) are mixed or "
                        "undefined: the VDP draws them blank or from other tables, so "
                        "check the PNG\n", vdp->name, raw);
            break;
        }
        describeRegisters(text, vdp->registerBank[0], "");
    }
    dbgSnapshotDestroy(snapshot);
}
