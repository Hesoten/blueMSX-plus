/*****************************************************************************
**
** Agent watchpoints.
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
#include "ControlWatch.h"
#include "ControlVram.h"
#include "Debugger.h"
#include "DebugDeviceManager.h"
#include "VDP.h"
#include <string.h>

typedef struct {
    int    inUse;
    int    devType;
    int    where;
    int    size;
    char   condition[4];
    Int64  value;
    char   printed[64];
    int    vramMode;   /* the screen mode a VRAM watch was set in, -1 when not read */
} KeptWatch;

/* What this socket set, for listing: the engine's own list is freed from other threads
** without a lock this one could take. Only a clear reads that list, once, to tell a watch
** the debugger window removed; that window removing one at the same moment can race it. */
static KeptWatch setWatches[CTRL_WATCH_LIST_MAX];

int ctrlWatchRemember(int devType, int where, int size, const char* condition,
                      Int64 value, const char* printed, int vramMode)
{
    KeptWatch* slot = NULL;
    int i;

    for (i = 0; i < CTRL_WATCH_LIST_MAX; i++) {
        if (setWatches[i].inUse && setWatches[i].devType == devType &&
            setWatches[i].where == where)
        {
            slot = &setWatches[i];
            break;
        }
        if (slot == NULL && !setWatches[i].inUse) {
            slot = &setWatches[i];
        }
    }
    if (slot == NULL) {
        return 0;
    }
    slot->inUse    = 1;
    slot->devType  = devType;
    slot->where    = where;
    slot->size     = size;
    slot->value    = value;
    slot->vramMode = vramMode;
    strncpy(slot->condition, condition, sizeof(slot->condition) - 1);
    slot->condition[sizeof(slot->condition) - 1] = 0;
    strncpy(slot->printed, printed, sizeof(slot->printed) - 1);
    slot->printed[sizeof(slot->printed) - 1] = 0;
    return 1;
}

int ctrlWatchForget(int devType, int where)
{
    int found = 0;
    int i;

    for (i = 0; i < CTRL_WATCH_LIST_MAX; i++) {
        if (setWatches[i].inUse && setWatches[i].devType == devType &&
            setWatches[i].where == where)
        {
            setWatches[i].inUse = 0;
            found = 1;
        }
    }
    return found;
}

int ctrlWatchDescribeList(CtrlText* text)
{
    int listed = 0;
    int i;

    for (i = 0; i < CTRL_WATCH_LIST_MAX; i++) {
        if (!setWatches[i].inUse) {
            continue;
        }
        ctrlTextAdd(text, "watch  %s  %d byte%s", setWatches[i].printed,
                    setWatches[i].size, setWatches[i].size == 1 ? "" : "s");
        if (setWatches[i].condition[0] != 0) {
            ctrlTextAdd(text, "  %s %lld", setWatches[i].condition,
                        (long long)setWatches[i].value);
        }
        ctrlTextAdd(text, "\n");
        listed++;
    }
    return listed;
}

/* For a stop at a VRAM watch set over this socket, the screen mode the first one covering
** the address hit was set in; else -1. */
static int watchModeAtStop(const CtrlStop* stop)
{
    UInt32 address;
    int i;

    if (stop->reason != CTRL_STOP_WATCH || stop->watchType != DBGTYPE_VIDEO ||
        stop->watchKind != DBG_WATCH_PROGRAM)
    {
        return -1;
    }
    address = (UInt32)debugDeviceVramAddress(stop->watchMode, (int)stop->watchAddress);
    for (i = 0; i < CTRL_WATCH_LIST_MAX; i++) {
        const KeptWatch* watch = &setWatches[i];
        if (watch->inUse && watch->devType == DBGTYPE_VIDEO && address >= (UInt32)watch->where &&
            address < (UInt32)(watch->where + watch->size))
        {
            return watch->vramMode;
        }
    }
    return -1;
}

/* A VRAM hit at the program's address in the mode of the write, with the chips' byte in an
** interleaving mode, and a word when the agent's watch was set in the other layout. */
static void describeVramWatchStop(CtrlText* text, const CtrlStop* stop, int setMode)
{
    UInt32 address = (UInt32)debugDeviceVramAddress(stop->watchMode, (int)stop->watchAddress);
    char   hit[32];
    char   set[32];

    ctrlTextAdd(text, "watch:   a write to vram:0x%04X; PC is past the instruction "
                "that wrote it, or anywhere if a VDP command did\n", (unsigned)address);
    ctrlVramModeShortName(hit, sizeof(hit), stop->watchMode);
    if (vdpVramInterleaved(stop->watchMode)) {
        ctrlTextAdd(text, "         %s interleaves video memory: that is byte 0x%05X in the "
                    "debugger window's VRAM view\n", hit, (unsigned)stop->watchAddress);
    }
    if (stop->watchKind != DBG_WATCH_PROGRAM) {
        return;
    }
    if (setMode >= 0 && !vdpVramSameLayout(setMode, stop->watchMode)) {
        ctrlVramModeShortName(set, sizeof(set), setMode);
        ctrlTextAdd(text, "         this watch was set while video memory had the %s layout; in "
                    "%s vram:0x%04X is another byte of video memory than it was then\n",
                    set, hit, (unsigned)address);
    }
}

void ctrlWatchDescribeStop(CtrlText* text, const CtrlStop* stop)
{
    if (stop->reason != CTRL_STOP_WATCH) {
        return;
    }
    if (stop->watchType == DBGTYPE_VIDEO) {
        describeVramWatchStop(text, stop, watchModeAtStop(stop));
    }
    else if (stop->watchType == DBGTYPE_PORT) {
        ctrlTextAdd(text, "watch:   an OUT to I/O port 0x%02X; PC is already past the "
                    "instruction\n", (unsigned)stop->watchAddress);
    }
    else {
        ctrlTextAdd(text, "watch:   a write to 0x%04X; the CPU stopped during the "
                    "write, so PC is already past the instruction\n",
                    (unsigned)stop->watchAddress);
    }
}
