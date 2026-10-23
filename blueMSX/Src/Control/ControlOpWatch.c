/*****************************************************************************
**
** Watchpoint op handlers.
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
#include "ControlWatch.h"
#include "ControlProtocol.h"
#include "Debugger.h"
#include "DebugDeviceManager.h"
#include "Emulator.h"
#include "VDP.h"
#include "V9990.h"
#include <stdio.h>
#include <string.h>

int ctrlOpWpList(CtrlCall* call)
{
    int listed = ctrlWatchDescribeList(&call->text);

    if (listed == 0) {
        ctrlTextAdd(&call->text, "(none)\n");
    }
    ctrlTextAdd(&call->text, "Only watches set over this socket are listed, as they were set; "
                "the debugger window's are not, and a change it makes to one is not "
                "shown.\n");
    jsonOutInt(&call->out, "watches", listed);
    return ctrlReplyDone(call);
}

/* What a watch request names: the engine's device and address, and how the list prints it.
** A VRAM watch is at the address the program uses, read in the screen mode vramMode, or
** -1 when set on a running machine. */
typedef struct {
    CtrlAddr      addr;
    CtrlAddrInfo  info;     /* for an address; a port has none */
    DbgDeviceType devType;
    int           where;
    int           byProgram;
    int           vramMode;
    char          printed[64];
} WatchTarget;

/* The condition, value and size a set asks for. */
typedef struct {
    int   condition;
    char  conditionName[16];
    Int64 value;
    Int32 size;
} WatchArgs;

/* The address in "at" as the CPU or the VDP reaches it. */
static int watchAddress(CtrlCall* call, int setting, WatchTarget* target)
{
    CtrlAddrInfo* info = &target->info;
    const char* typed = ctrlCallText(call, "at");
    char err[CTRL_ADDR_ERR_MAX];
    int  refused;

    /* With no machine, only a plain or vram: address means what it did;
    ** the others read the fake slot map. */
    if (!setting && emulatorGetState() == EMU_STOPPED && typed != NULL &&
        (!ctrlAddrParse(typed, &target->addr, err, sizeof(err)) ||
         (target->addr.space != CTRL_SPACE_CPU && target->addr.space != CTRL_SPACE_VRAM)))
    {
        return ctrlReplyError(call, "no_machine",
                              CTRL_TEXT_NO_MACHINE "; a watch can still be cleared by the "
                              "address that watch list shows, such as 0xC000 or "
                              "vram:0x1800");
    }
    /* Clearing needs no machine: a watch set while one ran stays set after it stops. */
    if ((refused = ctrlAddrArgRead(call, setting ? CTRL_ADDR_ARG_NEEDS_MACHINE : 0,
                                       &target->addr)) != 0) {
        return refused;
    }
    if ((refused = ctrlAddrArgResolve(call, &target->addr, info)) != 0) {
        return refused;
    }
    /* Otherwise a segment that does not exist would read as one not paged in. */
    if ((refused = ctrlAddrArgRefusePastEnd(call, &target->addr)) != 0) {
        return refused;
    }

    if (target->addr.space == CTRL_SPACE_VRAM) {
        if (!info->readable) {
            return ctrlReplyErrorf(call, "out_of_range", "video memory is %u kB on this machine",
                                   (unsigned)(info->spaceSize / 1024));
        }
        target->devType   = DBGTYPE_VIDEO;
        target->where     = (int)target->addr.offset;
        target->byProgram = 1;
        /* One read, under a view: a running VDP can change mode, so then none is kept. */
        target->vramMode  = call->view.held == DBG_ACCESS_VIEW ? vdpDebugGetScreenMode() : -1;
    }
    else {
        /* Paged out of a running machine is no answer: a moment later it can be paged in. */
        if (info->z80Address < 0 && setting && (refused = ctrlRefuseUnlessViewed(call)) != 0) {
            return refused;
        }
        if (info->z80Address < 0) {
            return ctrlReplyErrorf(call, "paged_out",
                                   "a watchpoint watches an address the CPU can "
                                   "reach, and that segment is not paged in. Set "
                                   "a segment breakpoint on it instead, or wait "
                                   "until it is paged in.%s", emulatorGetState() == EMU_RUNNING
                                   ? " " CTRL_TEXT_RUNNING_LAYOUT : "");
        }
        /* Use the Z80 address, not a device offset: device watch blocks
        ** index storage, and would fire on a different byte. */
        target->devType = DBGTYPE_CPU;
        target->where   = info->z80Address;
    }
    ctrlAddrPrint(&info->canonical, target->printed, sizeof(target->printed));
    if (target->devType == DBGTYPE_CPU && emulatorGetState() == EMU_STOPPED) {
        snprintf(target->printed, sizeof(target->printed), "0x%04X", target->where);
    }
    else if (target->devType == DBGTYPE_CPU) {
        /* The Z80 address is what is watched; the slot address is what is there now. */
        char canonical[sizeof(target->printed)];
        memcpy(canonical, target->printed, sizeof(target->printed));
        snprintf(target->printed, sizeof(target->printed), "0x%04X (now %s)", target->where,
                 canonical);
    }
    return 0;
}

/* What is watched: an I/O port in "port", else the address in "at". */
static int watchTarget(CtrlCall* call, int setting, WatchTarget* target)
{
    Int32 port = ctrlCallInt(call, "port", -1);

    memset(target, 0, sizeof(WatchTarget));
    if (!ctrlCallGiven(call, "port")) {
        return watchAddress(call, setting, target);
    }
    /* Picking one of the two would watch something the caller did not mean. */
    if (ctrlCallGiven(call, "at")) {
        return ctrlReplyError(call, "bad_address",
                              "give \"at\" or \"port\", not both");
    }
    if (port < CTRL_WP_PORT_MIN || port > CTRL_WP_PORT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "an I/O port is %d to %d",
                               CTRL_WP_PORT_MIN, CTRL_WP_PORT_MAX);
    }
    target->devType = DBGTYPE_PORT;
    target->where   = (int)port;
    snprintf(target->printed, sizeof(target->printed), "I/O port 0x%02X", (unsigned)port);
    return 0;
}

/* A condition compares at most 4 bytes, and only a value the watched bytes can hold. */
static int checkWatchCondition(CtrlCall* call, const WatchTarget* target, const WatchArgs* args)
{
    Int32 size = args->size;

    if (size > 4) {
        return ctrlReplyError(call, "out_of_range",
                              "a condition compares at most 4 bytes; watch "
                              "fewer bytes, or use condition any");
    }
    if (target->devType == DBGTYPE_PORT && size > 1) {
        return ctrlReplyError(call, "out_of_range",
                              "a port has no neighbouring bytes to compare, so "
                              "a condition on a port needs size 1");
    }
    /* The engine compares the first byte highest, so ordering is wrong for words. */
    if (size > 1 && (args->condition == DBGWP_LESS_THAN ||
                     args->condition == DBGWP_GREATER_THAN))
    {
        return ctrlReplyError(call, "bad_condition",
                              "lt and gt only work on 1 byte here; for a "
                              "longer value use eq or ne");
    }
    /* 4 bytes hold -0x80000000 to 0xFFFFFFFF; fewer, no sign. */
    if (args->value < -0x80000000LL || args->value > 0xFFFFFFFFLL ||
        (size < 4 && (args->value < 0 || args->value >= (1LL << (8 * size)))))
    {
        return ctrlReplyErrorf(call, "out_of_range", "%lld does not fit in %d byte%s, so "
                               "the watch could never fire", (long long)args->value, (int)size,
                               size == 1 ? "" : "s");
    }
    return 0;
}

/* Reads condition, value and size for a set; refuses what the engine could not watch. */
static int checkWatchArgs(CtrlCall* call, const WatchTarget* target, WatchArgs* args)
{
    const char* name = args->conditionName;
    const char* condition = ctrlCallText(call, "condition");

    /* The args column holds it to the size of conditionName. */
    strcpy(args->conditionName, condition != NULL ? condition : CTRL_WP_CONDITION_DEFAULT);
    if (strcmp(name, "any") == 0)      { args->condition = DBGWP_ANY; }
    else if (strcmp(name, "eq") == 0)  { args->condition = DBGWP_EQUALS; }
    else if (strcmp(name, "ne") == 0)  { args->condition = DBGWP_NOT_EQUALS; }
    else if (strcmp(name, "lt") == 0)  { args->condition = DBGWP_LESS_THAN; }
    else if (strcmp(name, "gt") == 0)  { args->condition = DBGWP_GREATER_THAN; }
    else {
        return ctrlReplyError(call, "bad_condition",
                              "\"condition\" must be any, eq, ne, lt or gt");
    }
    /* Left out, it would watch for 0. */
    if (args->condition != DBGWP_ANY && !ctrlCallGiven(call, "value")) {
        return ctrlReplyErrorf(call, "missing_value", "condition %s compares against \"value\"; "
                               "give one, or use condition any", name);
    }
    ctrlCallInt64(call, "value", &args->value);
    args->size  = ctrlCallInt(call, "size", CTRL_WP_SIZE_DEFAULT);
    if (args->size < CTRL_WP_SIZE_MIN || args->size > CTRL_WP_SIZE_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "size must be between %d and %d bytes",
                               CTRL_WP_SIZE_MIN, CTRL_WP_SIZE_MAX);
    }
    if (target->devType == DBGTYPE_VIDEO &&
        target->addr.offset + (UInt32)args->size > target->info.spaceSize)
    {
        return ctrlReplyErrorf(call, "out_of_range", "the watched range runs past the end "
                               "of video memory, which is %u kB on this machine",
                               (unsigned)(target->info.spaceSize / 1024));
    }
    if (target->devType == DBGTYPE_VIDEO && target->vramMode >= 0) {
        int refused = ctrlAddrArgRefuseNoVram(call, target->addr.offset, (UInt32)args->size,
                                            target->vramMode);
        if (refused != 0) {
            return refused;
        }
    }
    if (target->devType == DBGTYPE_CPU && target->where + (int)args->size > 0x10000) {
        return ctrlReplyError(call, "out_of_range",
                              "the watched range runs past 0xFFFF");
    }
    return args->condition != DBGWP_ANY ? checkWatchCondition(call, target, args) : 0;
}

static void reportWatchSet(CtrlCall* call, const WatchTarget* target, const WatchArgs* args)
{
    Int32 size = args->size;

    ctrlTextAdd(&call->text, "watching %d byte%s at %s\n", (int)size,
                size == 1 ? "" : "s", target->printed);
    if (target->devType == DBGTYPE_PORT) {
        ctrlTextAdd(&call->text, "stops on an OUT to it; IN does not stop it\n");
    }
    else if (target->devType == DBGTYPE_VIDEO) {
        ctrlTextAdd(&call->text, "stops when video memory there is written; reads do not "
                    "stop it\n");
        /* Presence alone: a set may hold the machine rather than view it. */
        if (v9990IsPresent()) {
            ctrlTextAdd(&call->text, "this is the VDP's video memory; writes to the V9990's "
                        "video memory do not stop it\n");
        }
    }
    else {
        ctrlTextAdd(&call->text, "stops when the CPU writes there, whatever segment is "
                    "paged in; reads do not stop it\n");
    }
    if (args->condition != DBGWP_ANY && size > 1) {
        ctrlTextAdd(&call->text, "only on a write that leaves the %d byte little endian "
                    "value %s %lld (0x%X). It is checked after each byte is "
                    "written, so it also sees a store that is half done.\n",
                    (int)size, args->conditionName, (long long)args->value,
                    (unsigned)(UInt32)args->value);
    }
    else if (args->condition != DBGWP_ANY) {
        ctrlTextAdd(&call->text, "only on a write that leaves the value %s %lld (0x%X)\n",
                    args->conditionName, (long long)args->value, (unsigned)(UInt32)args->value);
    }
}

static int setWatch(CtrlCall* call, const WatchTarget* target, const WatchArgs* args)
{
    /* The Z80 stores the low byte first; the engine reads the first byte highest. */
    UInt32 reference = 0;
    int    n;

    /* Refused rather than set unlisted, so the list stays complete. */
    if (!ctrlWatchRemember(target->devType, target->where, (int)args->size,
                           args->condition == DBGWP_ANY ? "" : args->conditionName, args->value,
                           target->printed, target->vramMode))
    {
        return ctrlReplyErrorf(call, "watch_limit", "%d watches are set over this socket "
                               "already; clear one first", CTRL_WATCH_LIST_MAX);
    }
    for (n = 0; n < (int)args->size && n < 4; n++) {
        reference = (reference << 8) | (((UInt32)args->value >> (8 * n)) & 0xff);
    }
    dbgSetWatchpointEx(target->devType, target->where, (DbgWatchpointCondition)args->condition,
                       args->condition == DBGWP_ANY ? (UInt32)args->value : reference,
                       (int)args->size, target->byProgram);
    reportWatchSet(call, target, args);
    return ctrlReplyDone(call);
}

static int clearWatch(CtrlCall* call, const WatchTarget* target)
{
    if (!debugDeviceHasMemoryWatchpointEx(target->devType, target->where, target->byProgram)) {
        /* Listed but gone: the debugger window shares a CPU or port node and can remove it. */
        if (ctrlWatchForget(target->devType, target->where)) {
            return ctrlReplyErrorf(call, "not_set", "no watch starts at %s any more; it was "
                                   "removed elsewhere, perhaps in the debugger window, so "
                                   "nothing was cleared", target->printed);
        }
        return ctrlReplyErrorf(call, "not_set", "no watch starts at %s, so nothing was "
                               "cleared; listing the watches shows what is set",
                               target->printed);
    }
    /* The clear suspends a running machine, which no request may do while it holds a view. */
    ctrlCallReleaseView(call);
    dbgClearWatchpointEx(target->devType, target->where, target->byProgram);
    ctrlWatchForget(target->devType, target->where);
    ctrlTextAdd(&call->text, "stopped watching %s\n", target->printed);
    return ctrlReplyDone(call);
}

static int watchOp(CtrlCall* call, int setting)
{
    WatchTarget target;
    WatchArgs   args;
    int         refused;

    if ((refused = watchTarget(call, setting, &target)) != 0) {
        return refused;
    }
    if (!setting) {
        return clearWatch(call, &target);
    }
    if ((refused = checkWatchArgs(call, &target, &args)) != 0) {
        return refused;
    }
    return setWatch(call, &target, &args);
}

int ctrlOpWpSet(CtrlCall* call)
{
    return watchOp(call, 1);
}

int ctrlOpWpClear(CtrlCall* call)
{
    return watchOp(call, 0);
}
