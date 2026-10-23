/*****************************************************************************
**
** CPU op handlers.
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
#include "ControlAddress.h"
#include "ControlCpu.h"
#include "ControlAddressArg.h"
#include "ControlStatus.h"
#include "ControlStopLog.h"
#include <stdio.h>

/* The value a register is set to, as an address: a number or a segment address.
** Returns 0 with where set, else the refusal. */
static int regValueAddress(CtrlCall* call, const char* valueText, CtrlAddr* where)
{
    char err[CTRL_ADDR_ERR_MAX];

    /* Parsed as an address so a segment address is accepted; a bare number is decimal. */
    if (!ctrlAddrParse(valueText, where, err, sizeof(err))) {
        return ctrlReplyError(call, "bad_value", err);
    }
    ctrlAddrArgTellHowRead(call, valueText, where);
    if (where->space == CTRL_SPACE_VRAM) {
        return ctrlReplyError(call, "bad_value",
                              "a CPU register cannot hold a video memory address; "
                              "the Z80 reaches VRAM through a port, not an address");
    }
    return 0;
}

/* The 16 bit value of where, read under the view: a segment address needs to be paged in.
** Returns 0 with value set, else the refusal. */
static int regValue(CtrlCall* call, const CtrlAddr* where, UInt32* value)
{
    CtrlAddrInfo info;

    if (where->space != CTRL_SPACE_SLOT) {
        *value = where->offset;
        return 0;
    }
    if (!ctrlAddrResolve(where, &info) || info.z80Address < 0) {
        return ctrlReplyError(call, "not_paged_in",
                              "that segment is not paged in, so there is no "
                              "16 bit address a register could hold for it. "
                              "Page it in, or give the number you want");
    }
    *value = (UInt32)info.z80Address;
    return 0;
}

int ctrlOpRegWrite(CtrlCall* call)
{
    int refused;
    const char* name = ctrlCallText(call, "reg");
    const char* valueText = ctrlCallText(call, "value");
    char  err[CTRL_ADDR_ERR_MAX];
    Int32 previous = -1;
    CtrlAddr where;
    UInt32 value;
    int    set;

    if (name == NULL) {
        return ctrlReplyError(call, "missing_reg",
                              "setting a register needs \"reg\", for example \"pc\", \"hl\" "
                              "or \"a\"");
    }
    if (valueText == NULL) {
        return ctrlReplyError(call, "missing_value",
                              "setting a register needs \"value\", for example \"0x4000\"");
    }
    if (valueText[0] == '-') {
        return ctrlReplyError(call, "bad_value",
                              "a register holds no sign; for -1 give 0xFF for an 8 bit "
                              "register or 0xFFFF for a 16 bit one");
    }
    if ((refused = ctrlRefuseWithoutMachine(call, NULL)) != 0 ||
        (refused = regValueAddress(call, valueText, &where)) != 0)
    {
        return refused;
    }
    if (!ctrlCpuCheckName(name, err, sizeof(err))) {
        return ctrlReplyError(call, "unwritable", err);
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0 ||
        (refused = regValue(call, &where, &value)) != 0)
    {
        return refused;
    }

    if ((set = ctrlCpuSetRegister(name, value, &previous, err, sizeof(err))) <= 0) {
        /* The view is held, so the registers are always there; -1 is a bug here. */
        return ctrlReplyError(call, set < 0 ? "internal" : "unwritable", err);
    }
    ctrlTextAdd(&call->text, "%s was 0x%X, is now 0x%X\n", name,
                (unsigned)previous, (unsigned)value);
    /* No run state: this request saw no stop, so any state could be stale. */
    ctrlStatusDescribe(&call->text, CTRL_STOP_UNKNOWN, CTRL_STATUS_STATE_UNSAID);
    jsonOutInt(&call->out, "previous", previous);
    return ctrlReplyDone(call);
}
