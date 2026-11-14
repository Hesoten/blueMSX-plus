/*****************************************************************************
**
** Debug device op handler.
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
#include "ControlStatus.h"
#include "ControlView.h"
#include "Debugger.h"
#include "DebugDeviceManager.h"
#include <stddef.h>

/* Room kept for CTRL_TEXT_CUT after each part of the reply. */
#define CUT_NOTE_ROOM 64

static const char* kindName(DbgDeviceType type)
{
    switch (type) {
    case DBGTYPE_CPU:   return "cpu";
    case DBGTYPE_CART:  return "cart";
    case DBGTYPE_BIOS:  return "bios";
    case DBGTYPE_RAM:   return "ram";
    case DBGTYPE_AUDIO: return "audio";
    case DBGTYPE_VIDEO: return "video";
    case DBGTYPE_PORT:  return "port";
    default:            return "unknown";
    }
}

/* Keeps what came after mark if room for the cut note is left; else drops it, adds the
** note and returns 0. */
static int keepOrCut(CtrlText* text, CtrlTextMark mark)
{
    if (!ctrlTextIsFull(text) && text->capacity - text->length >= CUT_NOTE_ROOM) {
        return 1;
    }
    ctrlTextCut(text, mark);
    ctrlTextAdd(text, CTRL_TEXT_CUT);
    return 0;
}

static void listMemoryBlocks(CtrlText* text, const DbgDevice* device)
{
    int i;

    for (i = 0; i < device->memoryBlockCount; i++) {
        ctrlTextAdd(text, "%s%s %u bytes", i == 0 ? "" : ", ", device->memoryBlock[i]->name,
                    (unsigned)device->memoryBlock[i]->size);
    }
}

/* One line per device: what it has, not its values. */
static void listDevices(CtrlText* text, DbgSnapshot* snapshot)
{
    int count = dbgSnapshotGetDeviceCount(snapshot);
    int i;
    int j;

    for (i = 0; i < count; i++) {
        const DbgDevice* device = dbgSnapshotGetDevice(snapshot, i);
        CtrlTextMark     mark   = ctrlTextMark(text);
        int              ports  = 0;

        ctrlTextAdd(text, "%3d  %-7s %s", i, kindName(device->type), device->name);
        if (device->registerBankCount > 0) {
            ctrlTextAdd(text, "; registers:");
            for (j = 0; j < device->registerBankCount; j++) {
                ctrlTextAdd(text, "%s %s (%u)", j == 0 ? "" : ",", device->registerBank[j]->name,
                            (unsigned)device->registerBank[j]->count);
            }
        }
        for (j = 0; j < device->ioPortsCount; j++) {
            ports += (int)device->ioPorts[j]->count;
        }
        if (device->ioPortsCount > 0) {
            ctrlTextAdd(text, "; I/O port views: %d (%d ports)", device->ioPortsCount, ports);
        }
        if (device->memoryBlockCount > 0) {
            ctrlTextAdd(text, "; memory: ");
            listMemoryBlocks(text, device);
        }
        ctrlTextAdd(text, "\n");
        if (!keepOrCut(text, mark)) {
            return;
        }
    }
}

static void listPorts(CtrlText* text, const DbgIoPorts* ports)
{
    UInt32 i;

    ctrlTextAdd(text, "ports:   %s, %u\n", ports->name, (unsigned)ports->count);
    for (i = 0; i < ports->count; i++) {
        int dir = ports->port[i].direction;

        ctrlTextAdd(text, "  0x%02X  %-2s  ", (unsigned)ports->port[i].port,
                    dir == DBG_IO_READWRITE ? "rw" : dir == DBG_IO_READ ? "r" :
                    dir == DBG_IO_WRITE ? "w" : "-");
        /* The debug info has no value for a port the CPU cannot read. */
        if (dir & DBG_IO_READ) {
            ctrlTextAdd(text, "%02X\n", ports->port[i].value);
        }
        else {
            ctrlTextAdd(text, "--\n");
        }
    }
}

/* device=N: every register and port of one device with its value. */
static void describeDevice(CtrlText* text, const DbgDevice* device, int index)
{
    CtrlTextMark mark = ctrlTextMark(text);
    int i;

    ctrlTextAdd(text, "device:  %d, %s, %s\n", index, kindName(device->type), device->name);
    if (device->type == DBGTYPE_AUDIO) {
        ctrlTextAdd(text, "status bits are as of the chip's last update\n");
    }
    for (i = 0; i < device->registerBankCount; i++) {
        if (!keepOrCut(text, mark)) {
            return;
        }
        mark = ctrlTextMark(text);
        ctrlTextAdd(text, "regs:    %s, %u\n", device->registerBank[i]->name,
                    (unsigned)device->registerBank[i]->count);
        ctrlStatusListRegisters(text, device->registerBank[i], "");
    }
    for (i = 0; i < device->ioPortsCount; i++) {
        if (!keepOrCut(text, mark)) {
            return;
        }
        mark = ctrlTextMark(text);
        listPorts(text, device->ioPorts[i]);
    }
    if (!keepOrCut(text, mark)) {
        return;
    }
    mark = ctrlTextMark(text);
    ctrlTextAdd(text, "memory:  ");
    if (device->memoryBlockCount == 0) {
        ctrlTextAdd(text, "none");
    }
    listMemoryBlocks(text, device);
    ctrlTextAdd(text, "\n");
    keepOrCut(text, mark);
}

int ctrlOpDevices(CtrlCall* call)
{
    DbgSnapshot* snapshot;
    UInt32 generation;
    Int64  index = 0;
    int    given = ctrlCallInt64(call, "device", &index);
    int    count;
    int    refused;

    if (given && index < 0) {
        return ctrlReplyErrorf(call, "out_of_range", "device must be 0 or more");
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    generation = dbgMachineGeneration();
    snapshot = ctrlViewDevices();
    if (snapshot == NULL) {
        return ctrlRefuseAccess(call, DBG_ACCESS_NOT_PAUSED);
    }
    count = dbgSnapshotGetDeviceCount(snapshot);
    if (given && (index < 0 || index >= count)) {
        dbgSnapshotDestroy(snapshot);
        return ctrlReplyErrorf(call, "out_of_range", "device must be between 0 and %d: this "
                               "machine has %d devices (generation %u)", count - 1, count,
                               (unsigned)generation);
    }
    ctrlTextAdd(&call->text, "generation %u: an index holds only while it stays %u; a reset, a "
                "cartridge change or a state load changes it\n", (unsigned)generation,
                (unsigned)generation);
    if (given) {
        describeDevice(&call->text, dbgSnapshotGetDevice(snapshot, (int)index), (int)index);
    }
    else {
        ctrlTextAdd(&call->text, "%d devices:\n", count);
        listDevices(&call->text, snapshot);
    }
    dbgSnapshotDestroy(snapshot);
    jsonOutUInt(&call->out, "machine_generation", generation);
    if (given) {
        jsonOutInt(&call->out, "device", (Int32)index);
    }
    return ctrlReplyDone(call);
}
