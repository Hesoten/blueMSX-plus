/*****************************************************************************
**
** Breakpoint op handlers.
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
#include "ControlAddressArg.h"
#include "ControlBreak.h"
#include "ControlProtocol.h"
#include "Debugger.h"
#include "Emulator.h"
#include <stdio.h>
#include <string.h>

/* Sets or clears the breakpoint the resolved address names: a segment one, or a plain
** one at its Z80 address. Returns 0, or the refusal. */
static int applyBreak(CtrlCall* call, const CtrlAddrInfo* info, int setting)
{
    if (info->canonical.segment >= 0) {
        /* Stops only while that segment is paged in. */
        if (setting) {
            if (!dbgSetSegmentBreakpoint(info->canonical.slot, info->canonical.subSlot,
                                         info->canonical.segment,
                                         (int)info->canonical.offset))
            {
                return ctrlReplyError(call, "breakpoint_limit",
                                      "no room for another segment breakpoint");
            }
        }
        else if (!dbgClearSegmentBreakpoint(info->canonical.slot, info->canonical.subSlot,
                                            info->canonical.segment,
                                            (int)info->canonical.offset))
        {
            return ctrlReplyError(call, "not_set",
                                  "no segment breakpoint is set there, so nothing was "
                                  "cleared; listing the breakpoints shows what is set");
        }
    }
    else {
        if (info->z80Address < 0) {
            return ctrlReplyErrorf(call, "paged_out",
                                   "no segment is known for that address and the slot is not "
                                   "paged in there, so there is no address to put a breakpoint "
                                   "at%s", !setting && emulatorGetState() == EMU_RUNNING
                                   ? ". " CTRL_TEXT_RUNNING_LAYOUT : "");
        }
        if (setting) {
            dbgSetKeptBreakpoint((UInt16)info->z80Address);
        }
        else if (!dbgClearKeptBreakpoint((UInt16)info->z80Address)) {
            return ctrlReplyError(call, "not_set",
                                  dbgHasBreakpoint((UInt16)info->z80Address)
                                  ? "the plain breakpoint there is the debugger window's own, "
                                    "so nothing was cleared; it is cleared in that window"
                                  : "no plain breakpoint is set there, so nothing was "
                                    "cleared; listing the breakpoints shows what is set");
        }
    }
    return 0;
}

static void reportSegmentBreak(CtrlCall* call, const CtrlAddr* addr, const CtrlAddrInfo* info,
                               int setting)
{
    ctrlTextAdd(&call->text, "%s segment breakpoint at ", setting ? "set a" : "cleared the");
    ctrlAddrDescribe(&call->text, addr);
    if (setting && addr->space == CTRL_SPACE_CPU) {
        ctrlTextAdd(&call->text,
                    "0x%04X was pinned to seg%d of slot%d.%d, the segment paged in "
                    "there now. It fires only while seg%d is paged in, in whichever "
                    "window it is (an 8 kB mapper can show it in two), not when "
                    "another segment runs at 0x%04X.\n",
                    (unsigned)addr->offset, (int)info->canonical.segment,
                    info->canonical.slot, info->canonical.subSlot,
                    (int)info->canonical.segment, (unsigned)addr->offset);
        if (emulatorGetState() == EMU_RUNNING) {
            ctrlTextAdd(&call->text, "%s\n", CTRL_TEXT_RUNNING_LAYOUT);
        }
    }
    else if (setting) {
        ctrlTextAdd(&call->text, "It fires only while seg%d of slot%d.%d is paged in, in "
                    "whichever window it is.\n",
                    (int)info->canonical.segment,
                    info->canonical.slot, info->canonical.subSlot);
    }
    else if (addr->space == CTRL_SPACE_CPU) {
        ctrlTextAdd(&call->text, "Only the one on seg%d, the segment paged in there now, "
                    "was cleared; any on another segment at 0x%04X stays.\n",
                    (int)info->canonical.segment, (unsigned)addr->offset);
        if (emulatorGetState() == EMU_RUNNING) {
            ctrlTextAdd(&call->text, "%s\n", CTRL_TEXT_RUNNING_LAYOUT);
        }
    }
    jsonOutString(&call->out, "kind", "segment");
}

static void reportPlainBreak(CtrlCall* call, const CtrlAddr* addr, const CtrlAddrInfo* info,
                             int setting)
{
    ctrlTextAdd(&call->text, "%s plain breakpoint at ", setting ? "set a" : "cleared the");
    ctrlAddrDescribe(&call->text, addr);
    if (setting) {
        /* No reported segment does not prove the slot never switches, so do not claim it. */
        ctrlTextAdd(&call->text,
                    "slot%d.%d reports no segment at 0x%04X, so this is a plain "
                    "breakpoint: it fires whenever the CPU runs 0x%04X, whatever is "
                    "paged in there. It stays over resets, state loads and cancelled "
                    "calls, until op=clear or clear_all removes it or this MCP server "
                    "goes away (the conversation ends) or Agent Control is turned Off: "
                    "it ends at once on a normal exit or Off, and about 5 s after a "
                    "killed server or a lost connection unless the same server "
                    "reconnects.\n",
                    info->canonical.slot, info->canonical.subSlot,
                    (unsigned)info->z80Address, (unsigned)info->z80Address);
    }
    else {
        ctrlTextAdd(&call->text, "The debugger window's own breakpoint at 0x%04X, if "
                    "any, stays.\n", (unsigned)info->z80Address);
    }
    jsonOutString(&call->out, "kind", "plain");
}

static int breakOp(CtrlCall* call, int setting)
{
    int refused;
    CtrlAddr     addr;
    CtrlAddrInfo info;

    /* The next start arms the ones set here again. */
    if (!setting &&
        (refused = ctrlRefuseWithoutMachine(call, "; the breakpoints set here stay and are armed "
                                            "again when the machine starts, so clear them then, "
                                            "or use clear_all, which removes them all now")) != 0)
    {
        return refused;
    }
    if ((refused = ctrlAddrArgRead(call, CTRL_ADDR_ARG_NEEDS_MACHINE |
                                       (setting ? 0 : CTRL_ADDR_ARG_TELLS_KEPT), &addr)) != 0) {
        return refused;
    }
    if ((refused = ctrlAddrArgResolve(call, &addr, &info)) != 0) {
        return refused;
    }
    if (info.canonical.space != CTRL_SPACE_SLOT) {
        return ctrlReplyError(call, "bad_address",
                              "a breakpoint goes on code, so the address has to be in a slot");
    }
    if (setting && (refused = ctrlAddrArgRefusePastEnd(call, &addr)) != 0) {
        return refused;
    }

    refused = applyBreak(call, &info, setting);
    if (refused != 0) {
        return refused;
    }

    /* A Z80 address arrives as CPU space; say what it was pinned to. */
    if (info.canonical.segment >= 0) {
        reportSegmentBreak(call, &addr, &info, setting);
    }
    else {
        reportPlainBreak(call, &addr, &info, setting);
    }
    return ctrlReplyDone(call);
}

int ctrlOpBpSet(CtrlCall* call)
{
    return breakOp(call, 1);
}

int ctrlOpBpClear(CtrlCall* call)
{
    return breakOp(call, 0);
}

int ctrlOpBpClearAll(CtrlCall* call)
{
    /* One more than is told, to tell whether there were more. */
    UInt16 kept[CTRL_PLAIN_LIST_MAX + 1];
    int cleared = dbgSegmentBreakpointCount();
    int plainCleared = dbgListKeptBreakpoints(kept, CTRL_PLAIN_LIST_MAX + 1);

    dbgClearAllSegmentBreakpoints();
    dbgClearKeptBreakpoints();
    ctrlTextAdd(&call->text, "cleared %d segment breakpoint%s and ", cleared, cleared == 1 ? "" : "s");
    if (plainCleared > CTRL_PLAIN_LIST_MAX) {
        ctrlTextAdd(&call->text, "more than %d plain ones", CTRL_PLAIN_LIST_MAX);
    }
    else {
        ctrlTextAdd(&call->text, "%d plain one%s", plainCleared, plainCleared == 1 ? "" : "s");
    }
    ctrlTextAdd(&call->text, " set with bmsx_break. Plain breakpoints the debugger window set "
                "stay: they are its own.\n");
    jsonOutInt(&call->out, "cleared", cleared);
    jsonOutInt(&call->out, "plain_cleared",
               plainCleared > CTRL_PLAIN_LIST_MAX ? CTRL_PLAIN_LIST_MAX : plainCleared);
    return ctrlReplyDone(call);
}

int ctrlOpBpList(CtrlCall* call)
{
    /* One more than is printed, to tell whether there were more. */
    UInt16 plain[CTRL_PLAIN_LIST_MAX + 1];
    int plainCount = dbgListBreakpoints(plain, CTRL_PLAIN_LIST_MAX + 1);
    int segmentCount = 0;
    int i;

    for (i = 0; i < dbgSegmentBreakpointCapacity(); i++) {
        CtrlAddr     at;
        CtrlAddrInfo info;
        int slot, subSlot, segment, offset;
        char printed[64];

        if (!dbgGetSegmentBreakpoint(i, &slot, &subSlot, &segment, &offset)) {
            continue;
        }
        memset(&at, 0, sizeof(at));
        at.space   = CTRL_SPACE_SLOT;
        at.slot    = (UInt8)slot;
        at.subSlot = (UInt8)subSlot;
        at.segment = segment;
        at.offset  = (UInt32)offset;
        ctrlAddrPrint(&at, printed, sizeof(printed));
        if (ctrlAddrResolve(&at, &info) && info.z80Address >= 0) {
            ctrlTextAdd(&call->text, "segment  %s  (paged in at z80 0x%04X now)\n",
                        printed, (unsigned)info.z80Address);
        }
        else {
            ctrlTextAdd(&call->text, "segment  %s  (not paged in now)\n", printed);
        }
        segmentCount++;
    }
    for (i = 0; i < plainCount && i < CTRL_PLAIN_LIST_MAX; i++) {
        ctrlTextAdd(&call->text, "plain    z80 0x%04X  (whatever is paged in)\n", plain[i]);
    }
    if (plainCount > CTRL_PLAIN_LIST_MAX) {
        ctrlTextAdd(&call->text, "(more than %d plain breakpoints are set; only the first %d "
                    "are listed)\n", CTRL_PLAIN_LIST_MAX, CTRL_PLAIN_LIST_MAX);
        plainCount = CTRL_PLAIN_LIST_MAX;
    }
    if (segmentCount == 0 && plainCount == 0) {
        ctrlTextAdd(&call->text, "(none)\n");
    }
    else if (plainCount > 0) {
        ctrlTextAdd(&call->text, "plain ones include the debugger window's\n");
    }
    jsonOutInt(&call->out, "segment", segmentCount);
    jsonOutInt(&call->out, "plain", plainCount);
    return ctrlReplyDone(call);
}

/* Unlike a cancelled call, which reconnects and finds them still set. */
int ctrlOpSessionEnd(CtrlCall* call)
{
    ctrlBreakDropPlain();
    ctrlTextAdd(&call->text, "session ended: the plain breakpoints set with bp.set were cleared. "
                "Segment breakpoints and watchpoints stay until cleared.\n");
    return ctrlReplyDone(call);
}
