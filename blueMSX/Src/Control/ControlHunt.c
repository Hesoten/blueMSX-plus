/*****************************************************************************
**
** Value search in RAM and VRAM.
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
#include "ControlHunt.h"
#include "ControlAddress.h"
#include "Debugger.h"
#include "ControlVram.h"
#include "ControlClient.h"
#include "ControlMemory.h"
#include "SlotManager.h"
#include "VDP.h"
#include <stdlib.h>
#include <string.h>

#define HUNT_SPACES_MAX 17   /* sixteen slot and subslot pairs, plus VRAM */

typedef struct {
    UInt8  space;      /* CTRL_SPACE_SLOT or CTRL_SPACE_VRAM */
    UInt8  slot;
    UInt8  subSlot;
    UInt8* live;
    UInt32 size;
    UInt32 bufOffset;
    Int32  segmentSize;
    UInt32 base;       /* slot offset of live[0] in an unswitched slot */
} HuntSpace;

struct CtrlHunt {
    UInt32    machineGeneration;
    int       width;
    int       valueBytes;
    int       isSigned;
    int       stride;
    HuntSpace space[HUNT_SPACES_MAX];
    int       spaceCount;
    UInt32    totalSize;
    UInt8*    prev;
    UInt8*    cur;
    UInt8*    mask;
    UInt32    alive;
    int       vramMode;
    int       vramLeftOut;
};

static int liveHunts;

const char* ctrlHuntCompareText(int compare)
{
    switch (compare) {
    case CTRL_HUNT_CHANGED:      return "changed";
    case CTRL_HUNT_UNCHANGED:    return "unchanged";
    case CTRL_HUNT_INCREASED:    return "increased";
    case CTRL_HUNT_DECREASED:    return "decreased";
    case CTRL_HUNT_INCREASED_BY: return "increased by";
    case CTRL_HUNT_DECREASED_BY: return "decreased by";
    case CTRL_HUNT_EQ:           return "equal to";
    case CTRL_HUNT_NE:           return "not equal to";
    case CTRL_HUNT_LT:           return "less than";
    case CTRL_HUNT_LE:           return "at most";
    case CTRL_HUNT_GT:           return "greater than";
    case CTRL_HUNT_GE:           return "at least";
    case CTRL_HUNT_IN_RANGE:     return "within";
    default:                     return "?";
    }
}

static int widthBytes(int width)
{
    switch (width) {
    case CTRL_HUNT_U8:
    case CTRL_HUNT_S8:  return 1;
    case CTRL_HUNT_U16:
    case CTRL_HUNT_S16: return 2;
    case CTRL_HUNT_U32:
    case CTRL_HUNT_S32: return 4;
    default:            return 0;
    }
}

static int widthIsSigned(int width)
{
    return width == CTRL_HUNT_S8 || width == CTRL_HUNT_S16 || width == CTRL_HUNT_S32;
}

/* Writable slot RAM and VRAM only; ROM cannot change and would double the buffers. VRAM is
** left out when vramMode interleaves banks the VDP does not have. */
static int collectSpaces(HuntSpace* out, int max, UInt32* totalSize, int vramMode, int* vramLeftOut)
{
    int    count = 0;
    UInt32 total = 0;
    int    slot;
    int    subSlot;
    UInt8* vram;
    UInt32 vramSize = 0;

    for (slot = 0; slot < 4 && count < max; slot++) {
        for (subSlot = 0; subSlot < 4 && count < max; subSlot++) {
            UInt8* data = NULL;
            UInt32 size = 0;
            int    writable = 0;

            if (slotDebugGetStorage(slot, subSlot, &data, &size, &writable) != SLOT_STORAGE_RAM) {
                continue;
            }
            if (data == NULL || size == 0 || !writable) {
                continue;
            }
            out[count].space       = CTRL_SPACE_SLOT;
            out[count].slot        = (UInt8)slot;
            out[count].subSlot     = (UInt8)subSlot;
            out[count].live        = data;
            out[count].size        = size;
            out[count].bufOffset   = total;
            out[count].segmentSize = slotDebugGetSegmentSize(slot, subSlot);
            out[count].base        = slotDebugGetStorageBase(slot, subSlot);
            total += size;
            count++;
        }
    }

    vram = vdpDebugGetVram(&vramSize);
    *vramLeftOut = ctrlVramLacksBank(vramMode);
    if (vram != NULL && vramSize > 0 && count < max && !*vramLeftOut) {
        out[count].space       = CTRL_SPACE_VRAM;
        out[count].slot        = 0;
        out[count].subSlot     = 0;
        out[count].live        = vram;
        out[count].size        = vramSize;
        out[count].bufOffset   = total;
        out[count].segmentSize = 0;
        out[count].base        = 0;
        total += vramSize;
        count++;
    }

    *totalSize = total;
    return count;
}

UInt32 ctrlHuntScannableSize(const CtrlHunt* hunt)
{
    return hunt != NULL ? hunt->totalSize : 0;
}

int ctrlHuntVramMode(const CtrlHunt* hunt)
{
    return hunt != NULL ? hunt->vramMode : -1;
}

int ctrlHuntVramLeftOut(const CtrlHunt* hunt)
{
    return hunt != NULL && hunt->vramLeftOut;
}

static int bitGet(const UInt8* mask, UInt32 index)
{
    return (mask[index >> 3] >> (index & 7)) & 1;
}

static void bitClear(UInt8* mask, UInt32 index)
{
    mask[index >> 3] &= (UInt8)~(1u << (index & 7));
}

static void bitSet(UInt8* mask, UInt32 index)
{
    mask[index >> 3] |= (UInt8)(1u << (index & 7));
}

static UInt32 readValue(const UInt8* buffer, UInt32 at, int bytes)
{
    switch (bytes) {
    case 1:  return buffer[at];
    case 2:  return (UInt32)buffer[at] | ((UInt32)buffer[at + 1] << 8);
    default: return (UInt32)buffer[at] | ((UInt32)buffer[at + 1] << 8) |
                    ((UInt32)buffer[at + 2] << 16) | ((UInt32)buffer[at + 3] << 24);
    }
}

static Int32 asSigned(UInt32 value, int bytes)
{
    switch (bytes) {
    case 1:  return (Int32)(Int8)value;
    case 2:  return (Int32)(Int16)value;
    default: return (Int32)value;
    }
}

/* Adjacent segments are rarely adjacent in Z80 space, so a value across a boundary is not one. */
static int straddlesSegment(const HuntSpace* space, UInt32 offsetInSpace, int bytes)
{
    UInt32 segmentSize;

    if (space->segmentSize <= 0 || bytes <= 1) {
        return 0;
    }
    segmentSize = (UInt32)space->segmentSize;
    return (offsetInSpace % segmentSize) + (UInt32)bytes > segmentSize;
}

/* Video memory in program order for the hunt's screen mode, so a value reads in the bytes the
** program addresses; in an interleaving mode the two 64 kB banks alternate below 0x20000. */
static void sampleVram(const CtrlHunt* hunt, const HuntSpace* space, UInt8* into)
{
    UInt32 k;

    /* The VDP writes lazily; sync so the copied VRAM is current. */
    vdpForceSync();
    if (!vdpVramInterleaved(hunt->vramMode)) {
        memcpy(into, space->live, space->size);
        return;
    }
    for (k = 0; k < 0x10000; k++) {
        into[2 * k]     = space->live[k];
        into[2 * k + 1] = space->live[0x10000 + k];
    }
    memcpy(into + 0x20000, space->live + 0x20000, space->size - 0x20000);
}

static void sample(CtrlHunt* hunt, UInt8* into)
{
    int i;

    for (i = 0; i < hunt->spaceCount; i++) {
        if (hunt->space[i].space == CTRL_SPACE_VRAM) {
            sampleVram(hunt, &hunt->space[i], into + hunt->space[i].bufOffset);
        }
        else {
            memcpy(into + hunt->space[i].bufOffset, hunt->space[i].live, hunt->space[i].size);
        }
    }
}

CtrlHunt* ctrlHuntStart(CtrlView* view, int width, int stride)
{
    CtrlHunt* hunt;
    UInt32 maskBytes;
    int    bytes = widthBytes(width);
    int    i;

    if (view == NULL || view->held != DBG_ACCESS_VIEW || bytes == 0) {
        return NULL;
    }
    if (liveHunts >= CTRL_HUNT_MAX) {
        return NULL;
    }
    if (!ctrlClientAllowed(ctrlClientCurrent())) {
        return NULL;
    }

    hunt = calloc(1, sizeof(CtrlHunt));
    if (hunt == NULL) {
        return NULL;
    }

    hunt->width      = width;
    hunt->valueBytes = bytes;
    hunt->isSigned   = widthIsSigned(width);
    /* Every byte offset by default: Z80 programs keep words at odd addresses. */
    hunt->stride     = stride > 0 ? stride : 1;
    hunt->vramMode   = vdpDebugGetScreenMode();
    hunt->spaceCount = collectSpaces(hunt->space, HUNT_SPACES_MAX, &hunt->totalSize,
                                     hunt->vramMode, &hunt->vramLeftOut);

    if (hunt->spaceCount == 0 || hunt->totalSize == 0) {
        free(hunt);
        return NULL;
    }

    maskBytes  = (hunt->totalSize + 7) / 8;
    hunt->prev = malloc(hunt->totalSize);
    hunt->cur  = malloc(hunt->totalSize);
    hunt->mask = calloc(1, maskBytes);

    if (hunt->prev == NULL || hunt->cur == NULL || hunt->mask == NULL) {
        free(hunt->prev);
        free(hunt->cur);
        free(hunt->mask);
        free(hunt);
        return NULL;
    }

    for (i = 0; i < hunt->spaceCount; i++) {
        const HuntSpace* space = &hunt->space[i];
        UInt32 offsetInSpace;

        for (offsetInSpace = 0;
             offsetInSpace + (UInt32)bytes <= space->size;
             offsetInSpace += (UInt32)hunt->stride)
        {
            if (straddlesSegment(space, offsetInSpace, bytes)) {
                continue;
            }
            bitSet(hunt->mask, space->bufOffset + offsetInSpace);
            hunt->alive++;
        }
    }

    sample(hunt, hunt->prev);
    memcpy(hunt->cur, hunt->prev, hunt->totalSize);

    hunt->machineGeneration = dbgMachineGeneration();
    liveHunts++;

    return hunt;
}

int ctrlHuntIsValid(const CtrlHunt* hunt)
{
    return hunt != NULL && hunt->machineGeneration == dbgMachineGeneration();
}

void ctrlHuntAbandon(CtrlHunt* hunt)
{
    if (hunt == NULL) {
        return;
    }
    free(hunt->prev);
    free(hunt->cur);
    free(hunt->mask);
    free(hunt);
    if (liveHunts > 0) {
        liveHunts--;
    }
}

static int passes(int compare, Int32 operand, Int32 operand2,
                  UInt32 before, UInt32 after, int bytes, int isSigned)
{
    if (isSigned) {
        Int32 a = asSigned(before, bytes);
        Int32 b = asSigned(after, bytes);

        switch (compare) {
        case CTRL_HUNT_CHANGED:      return b != a;
        case CTRL_HUNT_UNCHANGED:    return b == a;
        case CTRL_HUNT_INCREASED:    return b > a;
        case CTRL_HUNT_DECREASED:    return b < a;
        case CTRL_HUNT_INCREASED_BY: return b - a == operand;
        case CTRL_HUNT_DECREASED_BY: return a - b == operand;
        case CTRL_HUNT_EQ:           return b == operand;
        case CTRL_HUNT_NE:           return b != operand;
        case CTRL_HUNT_LT:           return b < operand;
        case CTRL_HUNT_LE:           return b <= operand;
        case CTRL_HUNT_GT:           return b > operand;
        case CTRL_HUNT_GE:           return b >= operand;
        case CTRL_HUNT_IN_RANGE:     return b >= operand && b <= operand2;
        default:                     return 0;
        }
    }
    else {
        switch (compare) {
        case CTRL_HUNT_CHANGED:      return after != before;
        case CTRL_HUNT_UNCHANGED:    return after == before;
        case CTRL_HUNT_INCREASED:    return after > before;
        case CTRL_HUNT_DECREASED:    return after < before;
        case CTRL_HUNT_INCREASED_BY: return operand >= 0 &&
                                            after == before + (UInt32)operand;
        case CTRL_HUNT_DECREASED_BY: return operand >= 0 &&
                                            before == after + (UInt32)operand;
        case CTRL_HUNT_EQ:           return after == (UInt32)operand;
        case CTRL_HUNT_NE:           return after != (UInt32)operand;
        case CTRL_HUNT_LT:           return after < (UInt32)operand;
        case CTRL_HUNT_LE:           return after <= (UInt32)operand;
        case CTRL_HUNT_GT:           return after > (UInt32)operand;
        case CTRL_HUNT_GE:           return after >= (UInt32)operand;
        case CTRL_HUNT_IN_RANGE:     return after >= (UInt32)operand &&
                                            after <= (UInt32)operand2;
        default:                     return 0;
        }
    }
}

int ctrlHuntRefine(CtrlHunt* hunt, CtrlView* view, int compare,
                   Int32 operand, Int32 operand2, UInt32* remaining)
{
    int i;

    if (hunt == NULL || view == NULL || view->held != DBG_ACCESS_VIEW) {
        return 0;
    }
    if (!ctrlHuntIsValid(hunt)) {
        return 0;
    }
    if (!ctrlClientAllowed(ctrlClientCurrent())) {
        return 0;
    }

    /* Swap, not copy: prev must keep the last reading while cur is resampled. */
    {
        UInt8* reusable = hunt->prev;
        hunt->prev = hunt->cur;
        hunt->cur  = reusable;
    }

    /* Must run with the machine parked, or the sample is not one consistent instant. */
    sample(hunt, hunt->cur);

    for (i = 0; i < hunt->spaceCount; i++) {
        const HuntSpace* space = &hunt->space[i];
        UInt32 offsetInSpace;

        for (offsetInSpace = 0;
             offsetInSpace + (UInt32)hunt->valueBytes <= space->size;
             offsetInSpace += (UInt32)hunt->stride)
        {
            UInt32 index = space->bufOffset + offsetInSpace;
            UInt32 before;
            UInt32 after;

            if (!bitGet(hunt->mask, index)) {
                continue;
            }
            before = readValue(hunt->prev, index, hunt->valueBytes);
            after  = readValue(hunt->cur, index, hunt->valueBytes);

            if (!passes(compare, operand, operand2, before, after,
                        hunt->valueBytes, hunt->isSigned))
            {
                bitClear(hunt->mask, index);
                hunt->alive--;
            }
        }
    }

    if (remaining != NULL) {
        *remaining = hunt->alive;
    }
    return 1;
}

UInt32 ctrlHuntCandidateCount(const CtrlHunt* hunt)
{
    return hunt != NULL ? hunt->alive : 0;
}

int ctrlHuntWidth(const CtrlHunt* hunt)
{
    return hunt != NULL ? hunt->width : CTRL_HUNT_U8;
}

int ctrlHuntList(const CtrlHunt* hunt, UInt32 first, UInt32 max,
                 CtrlHuntCandidate* out)
{
    UInt32 seen = 0;
    UInt32 filled = 0;
    int    i;

    if (hunt == NULL || max == 0) {
        return 0;
    }

    for (i = 0; i < hunt->spaceCount && filled < max; i++) {
        const HuntSpace* space = &hunt->space[i];
        UInt32 offsetInSpace;

        for (offsetInSpace = 0;
             offsetInSpace + (UInt32)hunt->valueBytes <= space->size && filled < max;
             offsetInSpace += (UInt32)hunt->stride)
        {
            UInt32 index = space->bufOffset + offsetInSpace;
            CtrlHuntCandidate* candidate;
            CtrlAddrInfo info;

            if (!bitGet(hunt->mask, index)) {
                continue;
            }
            if (seen++ < first) {
                continue;
            }

            candidate = &out[filled++];
            memset(candidate, 0, sizeof(CtrlHuntCandidate));

            candidate->addr.space = space->space;
            if (space->space == CTRL_SPACE_SLOT) {
                candidate->addr.slot    = space->slot;
                candidate->addr.subSlot = space->subSlot;
                if (space->segmentSize > 0) {
                    candidate->addr.segment = (Int32)(offsetInSpace / (UInt32)space->segmentSize);
                    candidate->addr.offset  = offsetInSpace % (UInt32)space->segmentSize;
                }
                else {
                    candidate->addr.segment = CTRL_SEGMENT_FLAT;
                    candidate->addr.offset  = space->base + offsetInSpace;
                }
            }
            else {
                candidate->addr.segment = CTRL_SEGMENT_FLAT;
                candidate->addr.offset  = offsetInSpace;
            }

            candidate->prevValue = readValue(hunt->prev, index, hunt->valueBytes);
            candidate->curValue  = readValue(hunt->cur, index, hunt->valueBytes);

            candidate->z80Address = -1;
            if (ctrlAddrResolve(&candidate->addr, &info)) {
                candidate->z80Address = info.z80Address;
            }
        }
    }

    return (int)filled;
}
