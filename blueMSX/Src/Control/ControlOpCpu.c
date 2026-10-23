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
#include "ControlSlots.h"
#include "ControlAddressArg.h"
#include "ControlStatus.h"
#include "ControlStopLog.h"
#include "ControlSymbol.h"
#include "SlotManager.h"
#include <stdio.h>

/* The value a register is set to, as an address: a number, a label, or a segment address.
** Returns 0 with where set, else the refusal. */
static int regValueAddress(CtrlCall* call, const char* valueText, CtrlAddr* where)
{
    char err[CTRL_ADDR_ERR_MAX];

    /* Parsed as an address so a label is accepted; a bare number is decimal. */
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
                              "setting a register needs \"value\", for example \"0x4000\" or "
                              "\"@DRAW_SPRITES\"");
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

/* True when a CALL, an RST other than 38h (blank ROM, and IM1), or a HALT ends just before ret. */
static int callEndsAt(UInt16 ret)
{
    UInt8 op3 = slotPeek(NULL, (UInt16)(ret - 3));
    UInt8 op1 = slotPeek(NULL, (UInt16)(ret - 1));

    return op3 == 0xCD || (op3 & 0xC7) == 0xC4 || op1 == 0x76 ||
           ((op1 & 0xC7) == 0xC7 && op1 != 0xFF);
}

/* The return addresses on the stack, innermost first, with segment and nearest symbol.
** The caller must hold a view: the stack memory is read to check each one. */
static void describeCallstack(CtrlText* text, int max)
{
    UInt16 frames[64];
    UInt16 where[64];
    int    count;
    int    notFound;
    int    innerNotFound;
    int    i;

    if (max > 64) {
        max = 64;
    }
    count = ctrlCpuCallstack(frames, where, max, &notFound, &innerNotFound);
    if (count < 0) {
        ctrlTextAdd(text, "the machine has no stack to read right now\n");
        return;
    }
    if (count == 0) {
        ctrlTextAdd(text, "nothing on the stack is a return address the CPU recorded\n");
        if (notFound > 0) {
            ctrlTextAdd(text, "%d call-log entries were not found on the stack above SP "
                        "(stale, a changed return address, or a stack switch)\n", notFound);
        }
        return;
    }

    ctrlTextAdd(text, "return addresses found on the stack (the CPU's call log, checked "
                "against stack memory), innermost first:\n");
    for (i = 0; i < count; i++) {
        int   slot;
        int   subSlot;
        Int32 segment = ctrlSlotsPagedIn(frames[i], &slot, &subSlot);

        ctrlTextAdd(text, " %2d  0x%04X", i, frames[i]);
        ctrlTextAdd(text, "  %d.%d", slot, subSlot);
        if (segment >= 0) {
            ctrlTextAdd(text, ":seg%d", (int)segment);
        }

        if (ctrlSymCount() > 0) {
            CtrlSymHome home;
            CtrlSymNear near;

            home.slot    = (UInt8)slot;
            home.subSlot = (UInt8)subSlot;
            home.segment = segment >= 0 ? segment : CTRL_SYM_SEG_ANY;

            if (ctrlSymNearest(&home, frames[i], &near)) {
                ctrlTextAdd(text, "  %s", near.name);
                if (near.offset > 0) {
                    ctrlTextAdd(text, "+0x%X", near.offset);
                }
                /* The call log keeps no segment, so a switched window may show another one's code. */
                if (segment >= 0 && !callEndsAt(frames[i])) {
                    ctrlTextAdd(text, "  (no CALL, RST or HALT ends just before it in seg%d: "
                                "an interrupt, or a call made while another segment was paged "
                                "in, so the label may be wrong)", (int)segment);
                }
            }
        }
        ctrlTextAdd(text, "  (on the stack at 0x%04X)\n", where[i]);
    }
    if (notFound > 0) {
        ctrlTextAdd(text, "%d call-log entries were not found on the stack above SP and are "
                    "left out (stale, a changed return address, or a stack switch)\n",
                    notFound);
    }
    ctrlTextAdd(text,
                "the segment shown is the one paged in there now, which is not "
                "necessarily the one that was when the call was made\n");
}

int ctrlOpStack(CtrlCall* call)
{
    int refused;
    Int32 count = CTRL_STACK_COUNT_DEFAULT;

    count = ctrlCallInt(call, "count", count);
    if (count < CTRL_STACK_COUNT_MIN || count > CTRL_STACK_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be between %d and %d frames",
                               CTRL_STACK_COUNT_MIN, CTRL_STACK_COUNT_MAX);
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    describeCallstack(&call->text, (int)count);
    return ctrlReplyDone(call);
}
