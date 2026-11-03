/*****************************************************************************
**
** Address argument of an op call.
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
#include "ControlAddressArg.h"
#include "ControlAddress.h"
#include "ControlSlots.h"
#include "ControlVram.h"
#include "ControlProtocol.h"
#include "Emulator.h"
#include "SlotManager.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int ctrlAddrArgResolve(CtrlCall* call, const CtrlAddr* addr, CtrlAddrInfo* info)
{
    if (call->atUnread) {
        return ctrlRefuseUnlessViewed(call);
    }
    if (ctrlAddrResolve(addr, info)) {
        return 0;
    }
    return ctrlReplyError(call, "bad_address", "there is no such address on this machine");
}

/* Bare digits are decimal here but hex in symbol files, so a long bare number is echoed. */
static void tellBareDecimal(CtrlText* out, const char* typed)
{
    const char* scan;

    for (scan = typed; *scan != 0; scan++) {
        const char* end = scan;
        if (!isdigit((unsigned char)*scan) ||
            (scan > typed && (isalnum((unsigned char)scan[-1]) || strchr("_.$#", scan[-1]) != NULL)))
        {
            continue;
        }
        while (isdigit((unsigned char)*end)) {
            end++;
        }
        if (end - scan >= 4 && !isxdigit((unsigned char)*end) &&
            (*end == 0 || strchr("hHxX", *end) == NULL))
        {
            /* Video memory holds at most 192 kB, and every other space 64 kB. */
            unsigned long most = _strnicmp(typed, "vram:", 5) == 0 ? 0x2FFFF : 0xFFFF;

            ctrlTextAdd(out, "read %.*s as decimal (0x%04X)",
                        (int)(end - scan), scan, (unsigned)strtoul(scan, NULL, 10));
            if (strtoul(scan, NULL, 16) <= most) {
                ctrlTextAdd(out, "; write 0x%.*s for hexadecimal", (int)(end - scan), scan);
            }
            ctrlTextAdd(out, "\n");
            return;
        }
        scan = end - 1;
    }
}

/* Which mapper ram: took, and why, as the parse saw page 3: reading it again could disagree. */
static void tellRamChoice(CtrlText* out, const CtrlAddr* addr)
{
    if (addr->ramWhy == CTRL_RAM_PAGE3) {
        ctrlTextAdd(out, "ram: is slot%d.%d, the memory mapper in page 3 now", addr->slot,
                    addr->subSlot);
    }
    else if (addr->ramWhy == CTRL_RAM_MAIN) {
        ctrlTextAdd(out, "ram: is slot%d.%d, the machine's main RAM, as page 3 shows slot%d.%d, "
                    "which is no memory mapper", addr->slot, addr->subSlot,
                    addr->ramPage3Slot, addr->ramPage3SubSlot);
    }
    else {
        return;
    }
    if (emulatorGetState() == EMU_RUNNING) {
        ctrlTextAdd(out, "; page 3 was read while the machine runs; BIOS slot routines switch "
                    "it for a moment, so pause for a fixed answer");
    }
    ctrlTextAdd(out, "\n");
}

void ctrlAddrArgTellHowRead(CtrlCall* call, const char* typed, const CtrlAddr* addr)
{
    tellBareDecimal(&call->text, typed);
    tellRamChoice(&call->text, addr);
    if (addr->symbolByPaging && emulatorGetState() == EMU_RUNNING) {
        ctrlTextAdd(&call->text, "Of the definitions of that name, the one paged in was taken. "
                    "%s\n", CTRL_TEXT_RUNNING_LAYOUT);
    }
}

int ctrlAddrArgRead(CtrlCall* call, int flags, CtrlAddr* addr)
{
    char text[CTRL_AT_MAX + 1];
    char err[CTRL_ADDR_ERR_MAX];
    const char* typed;
    int  substituted;
    int  refused;

    if ((flags & CTRL_ADDR_ARG_NEEDS_MACHINE) && (refused = ctrlRefuseWithoutMachine(call, NULL)) != 0) {
        return refused;
    }
    if ((typed = ctrlCallText(call, "at")) == NULL) {
        return ctrlReplyError(call, "missing_at",
                              "this request needs an address in \"at\", for example "
                              "\"0xC000\", \"pc\", \"@DRAW_SPRITES\", "
                              "\"ram:seg12+0x1A30\" or \"slot1.0:seg5+0x100\"");
    }
    strcpy(text, typed);
    substituted = ctrlAddrSubstituteRegister(text, sizeof(text), err, sizeof(err));
    if (substituted < 0) {
        return ctrlReplyError(call, "bad_address", err);
    }
    /* The op checks its other arguments first, then refuses as it refuses any read. */
    if (substituted == 0) {
        memset(addr, 0, sizeof(*addr));
        addr->space  = CTRL_SPACE_CPU;
        call->atUnread = 1;
        return 0;
    }
    if (!ctrlAddrParse(text, addr, err, sizeof(err))) {
        return ctrlReplyError(call, "bad_address", err);
    }
    /* seg<n> on a slot that reports no segments would otherwise read as no address at all. */
    if (addr->space == CTRL_SPACE_SLOT && addr->segment >= 0 &&
        emulatorGetState() != EMU_STOPPED &&
        slotDebugGetSegmentSize(addr->slot, addr->subSlot) <= 0 &&
        slotDebugGetContents(addr->slot, addr->subSlot)[0] != 0)
    {
        CtrlText why;

        ctrlTextInit(&why, err, sizeof(err));
        ctrlSlotsDescribeNoSegments(&why, addr->slot, addr->subSlot);
        /* Segment breakpoints stay across a restart, even one under a mapper with no segments. */
        if (flags & CTRL_ADDR_ARG_TELLS_KEPT) {
            ctrlTextAdd(&why, "; a segment breakpoint kept from an earlier start is removed "
                        "with clear_all, which removes every segment breakpoint");
        }
        return ctrlReplyError(call, "bad_address", err);
    }
    ctrlAddrArgTellHowRead(call, text, addr);
    return 0;
}

int ctrlAddrArgRefuseNoVram(CtrlCall* call, UInt32 address, UInt32 size, int mode)
{
    Int32    unbacked = ctrlVramUnbacked(address, size, mode);
    char     message[160];
    CtrlText why;

    if (unbacked < 0) {
        return 0;
    }
    ctrlTextInit(&why, message, sizeof(message));
    ctrlVramDescribeUnbacked(&why, (UInt32)unbacked, mode);
    return ctrlReplyError(call, "out_of_range", message);
}

int ctrlAddrArgRefusePastEnd(CtrlCall* call, const CtrlAddr* addr)
{
    CtrlAddrInfo info;
    char         message[128];
    CtrlText     why;

    if (call->atUnread) {
        return ctrlRefuseUnlessViewed(call);
    }
    if (!ctrlAddrResolve(addr, &info) || !ctrlAddrPastEnd(&info)) {
        return 0;
    }
    ctrlTextInit(&why, message, sizeof(message));
    ctrlAddrDescribePastEnd(&why, &info);
    return ctrlReplyError(call, "out_of_range", message);
}
