/*****************************************************************************
**
** Memory and VRAM access by slot and segment.
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
#include "ControlMemory.h"
#include "ControlAddress.h"
#include "Debugger.h"
#include "ControlVram.h"
#include "ControlClient.h"
#include "DebugDeviceManager.h"
#include "SlotManager.h"
#include "VDP.h"
#include <string.h>

#define CTRL_MEM_MAX 0x10000

/* For a check made before the machine is stopped, when the screen mode is not known yet. */
#define ANY_MODE (-1)

const char* ctrlMemErrorText(int result)
{
    switch (result) {
    case CTRL_MEM_OK:         return "ok";
    case CTRL_MEM_BADADDR:    return "there is no such address on this machine";
    case CTRL_MEM_PASTEND:    return "the range runs past the end of the memory it starts in; use fewer bytes";
    case CTRL_MEM_NOTPRESENT: return "that address is real but nothing answers there";
    case CTRL_MEM_PAGEDOUT:   return "that slot does not publish its storage, so it can only be read while it is paged in";
    case CTRL_MEM_READONLY:   return "that memory is read only";
    case CTRL_MEM_NOVIEW:     return "the emulation thread could not be stopped in time";
    case CTRL_MEM_TOOBIG:     return "that is more than one request may read";
    case CTRL_MEM_DENIED:     return "access to this emulator was cut off by the user";
    case CTRL_MEM_NOVRAM:     return "there is no video memory behind that address in this screen mode";
    default:                  return "unknown";
    }
}

/* Offset of a slot address in the device's published storage, or -1 outside it. */
static Int32 storageOffset(const CtrlAddr* addr, UInt32 storageSize, int segmentSize)
{
    UInt32 offset;

    if (addr->segment < 0 || segmentSize <= 0) {
        return -1;
    }
    /* By count first: segment * size wraps for a large segment number. */
    if ((UInt32)addr->segment >= storageSize / (UInt32)segmentSize) {
        return -1;
    }
    offset = (UInt32)addr->segment * (UInt32)segmentSize + addr->offset;
    if (offset >= storageSize) {
        return -1;
    }
    return (Int32)offset;
}

UInt32 ctrlMemRemaining(const CtrlAddr* addr)
{
    CtrlAddrInfo info;

    switch (addr->space) {
    case CTRL_SPACE_CPU:
        return addr->offset <= 0xffff ? 0x10000 - addr->offset : 0;

    case CTRL_SPACE_VRAM:
    case CTRL_SPACE_SLOT:
        if (!ctrlAddrResolve(addr, &info)) {
            return 0;
        }
        if (addr->space == CTRL_SPACE_VRAM) {
            return info.spaceSize > 0 && addr->offset < info.spaceSize
                       ? info.spaceSize - addr->offset : 0;
        }
        if (info.canonical.segment < 0) {
            /* Unswitched slots are read through the Z80 window; test before spaceSize. */
            return info.z80Address >= 0 ? 0x10000 - (UInt32)info.z80Address : 0;
        }
        if (info.spaceSize == 0) {
            return 0;
        }
        {
            Int32 at = storageOffset(&info.canonical, info.spaceSize, info.segmentSize);
            /* To the end of the storage, not the segment. */
            return at < 0 ? 0 : info.spaceSize - (UInt32)at;
        }

    default:
        return 0;
    }
}

/* Copies between bytes and the Z80 bus from z80 on: what the CPU would read, or a write
** the device in the slot then handles. */
static void busCopy(UInt32 z80, UInt8* bytes, UInt32 size, int isWrite)
{
    UInt32 i;

    for (i = 0; i < size; i++) {
        if (isWrite) {
            slotWrite(NULL, (UInt16)(z80 + i), bytes[i]);
        }
        else {
            bytes[i] = slotPeek(NULL, (UInt16)(z80 + i));
        }
    }
}

/* Video memory from the vram: address at on, byte by byte as that screen mode lays it out. */
static void vramCopy(UInt8* vram, UInt32 at, UInt8* bytes, UInt32 size, int isWrite, int vramMode)
{
    UInt32 i;

    for (i = 0; i < size; i++) {
        UInt8* byte = vram + debugDeviceVramIndex(vramMode, (int)(at + i));
        if (isWrite) {
            *byte = bytes[i];
        }
        else {
            bytes[i] = *byte;
        }
    }
}

static void storageCopy(UInt8* storage, UInt32 at, UInt8* bytes, UInt32 size, int isWrite)
{
    if (isWrite) {
        memcpy(storage + at, bytes, size);
    }
    else {
        memcpy(bytes, storage + at, size);
    }
}

/* Where a transfer goes: the Z80 bus from z80 on, or storage from at on. */
typedef struct {
    UInt8* storage;   /* NULL for the bus */
    UInt32 at;
    int    byPaging;  /* the answer follows what is paged in now */
} Place;

/* Where size bytes at addr are, from the layout alone. A write to read only is refused before
** its range; a slot paged out with no storage says nothing answers a read, paged out a write.
** Video memory is checked in the screen mode vramMode, unless that is ANY_MODE. */
static int locate(const CtrlAddr* addr, UInt32 size, int isWrite, int vramMode, Place* place)
{
    CtrlAddrInfo info;
    UInt32 storageSize = 0;
    int    writable = 0;
    Int32  at;

    place->storage = NULL;
    place->byPaging = 0;
    if (size > CTRL_MEM_MAX) {
        return CTRL_MEM_TOOBIG;
    }

    if (addr->space == CTRL_SPACE_CPU) {
        place->at = addr->offset;
        return addr->offset + size > 0x10000 ? CTRL_MEM_PASTEND : CTRL_MEM_OK;
    }

    if (addr->space == CTRL_SPACE_VRAM) {
        place->storage = vdpDebugGetVram(&storageSize);
        place->at = addr->offset;
        if (place->storage == NULL) {
            return CTRL_MEM_NOTPRESENT;
        }
        /* Before the end: a range that runs past it there has bytes with no memory first. */
        if (vramMode != ANY_MODE && addr->offset < storageSize && ctrlVramLacksBank(vramMode) &&
            ctrlVramUnbacked(addr->offset, size, vramMode) >= 0)
        {
            return CTRL_MEM_NOVRAM;
        }
        if (addr->offset + size > storageSize) {
            return addr->offset < storageSize ? CTRL_MEM_PASTEND : CTRL_MEM_BADADDR;
        }
        return CTRL_MEM_OK;
    }

    if (!ctrlAddrResolve(addr, &info)) {
        return CTRL_MEM_BADADDR;
    }

    if (info.canonical.segment < 0) {
        place->byPaging = 1;
        if (info.z80Address < 0) {
            return CTRL_MEM_PAGEDOUT;
        }
        if (isWrite && !info.writable) {
            return CTRL_MEM_READONLY;
        }
        place->at = (UInt32)info.z80Address;
        return place->at + size > 0x10000 ? CTRL_MEM_PASTEND : CTRL_MEM_OK;
    }

    if (slotDebugGetStorage(info.canonical.slot, info.canonical.subSlot,
                            &place->storage, &storageSize, &writable) == SLOT_STORAGE_NONE)
    {
        place->storage = NULL;
        return isWrite || info.z80Address >= 0 ? CTRL_MEM_PAGEDOUT : CTRL_MEM_NOTPRESENT;
    }
    if (isWrite && !writable) {
        return CTRL_MEM_READONLY;
    }

    at = storageOffset(&info.canonical, storageSize, info.segmentSize);
    if (at < 0) {
        return CTRL_MEM_BADADDR;
    }
    place->at = (UInt32)at;
    return (UInt32)at + size > storageSize ? CTRL_MEM_PASTEND : CTRL_MEM_OK;
}

int ctrlMemCheck(const CtrlAddr* addr, UInt32 size, int isWrite, int* byPaging)
{
    Place place;
    int   result = size == 0 ? CTRL_MEM_OK : locate(addr, size, isWrite, ANY_MODE, &place);

    *byPaging = size != 0 && place.byPaging;
    return result;
}

/* Reads size bytes at addr into bytes, or writes them there, holding a view: a hold of a
** running machine reads none of its memory. A vram: address is laid out as in vramMode. */
static int transfer(CtrlView* view, const CtrlAddr* addr, UInt8* bytes, UInt32 size, int isWrite,
                    int vramMode)
{
    Place place;
    int   result;

    if (view == NULL || view->held != DBG_ACCESS_VIEW) {
        return CTRL_MEM_NOVIEW;
    }
    if (size == 0) {
        return CTRL_MEM_OK;
    }
    /* The VDP's command engine first writes what it is behind with. */
    if (addr->space == CTRL_SPACE_VRAM) {
        vdpForceSync();
    }
    result = locate(addr, size, isWrite, vramMode, &place);
    if (result != CTRL_MEM_OK) {
        return result;
    }
    if (place.storage == NULL) {
        busCopy(place.at, bytes, size, isWrite);
    }
    else if (addr->space == CTRL_SPACE_VRAM && vdpVramInterleaved(vramMode)) {
        vramCopy(place.storage, place.at, bytes, size, isWrite, vramMode);
    }
    else {
        storageCopy(place.storage, place.at, bytes, size, isWrite);
    }
    return CTRL_MEM_OK;
}

int ctrlMemRead(CtrlView* view, const CtrlAddr* addr, UInt8* dst, UInt32 size)
{
    if (!ctrlClientAllowed(ctrlClientCurrent())) {
        return CTRL_MEM_DENIED;
    }
    return transfer(view, addr, dst, size, 0, vdpDebugGetScreenMode());
}

int ctrlMemWrite(CtrlView* view, const CtrlAddr* addr, const UInt8* src, UInt32 size)
{
    if (!ctrlClientAllowed(ctrlClientCurrent())) {
        return CTRL_MEM_DENIED;
    }

    /* transfer changes bytes only when it reads, so the caller's stay as they are. */
    return transfer(view, addr, (UInt8*)src, size, 1, vdpDebugGetScreenMode());
}
