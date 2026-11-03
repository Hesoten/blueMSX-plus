/*****************************************************************************
**
** Disassembly listing with symbol labels.
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
#include "ControlDasm.h"
#include "ControlAddress.h"
#include "ControlMemory.h"
#include "ControlSymbol.h"
#include "R800Dasm.h"
#include <stdio.h>
#include <string.h>

/* home is where the listed code lives; it picks between symbols sharing an address. */
typedef struct {
    CtrlSymHome home;
    UInt16      origin;     /* the address the row was disassembled at */
    UInt32      offset;     /* the row's offset in its segment */
    int         segmentSize;
    int         pagedIn;    /* origin is a true Z80 address */
    char        target[32];
} DasmSymbols;

static const char* dasmSymbol(void* ref, UInt16 at, int isAddress)
{
    DasmSymbols* context = (DasmSymbols*)ref;

    /* A relative target is exact as a segment offset, in whichever window the segment runs. */
    if (isAddress == 2 && context->home.segment >= 0 && context->segmentSize > 0) {
        Int32 delta  = (Int16)(UInt16)(at - context->origin);
        Int32 target = (Int32)context->offset + delta;

        if (target >= 0 && target < context->segmentSize) {
            const char* name = ctrlSymInSegment(context->home.slot, context->home.subSlot,
                                                context->home.segment, (UInt32)target);
            if (name != NULL) {
                return name;
            }
            if (!context->pagedIn) {
                ctrlAddrPrintSegment(context->target, sizeof(context->target), 0,
                                     context->home.slot, context->home.subSlot,
                                     (int)context->home.segment, (UInt32)target);
                return context->target;
            }
        }
        else if (!context->pagedIn) {
            /* Out of the segment, so only the distance is known; $ is not an address to paste. */
            snprintf(context->target, sizeof(context->target), "$%+d", (int)delta);
            return context->target;
        }
    }
    return ctrlSymFind(&context->home, at, isAddress, NULL, NULL);
}

/* Must stay a form ctrlAddrParse accepts: listing lines are pasted back as addresses. */
static void printShort(const CtrlAddr* addr, char* out, int outLen)
{
    if (addr->space == CTRL_SPACE_SLOT && addr->segment >= 0) {
        ctrlAddrPrintSegment(out, outLen, 0, addr->slot, addr->subSlot, addr->segment,
                             addr->offset);
    }
    else {
        ctrlAddrPrint(addr, out, outLen);
    }
}

/* Where the listing starts, the symbol files loaded, and the column heads. */
static void dasmHeader(CtrlText* text, const CtrlAddr* at, const CtrlAddrInfo* info)
{
    ctrlAddrDescribe(text, at);

    if (ctrlSymCount() > 0) {
        int fileIndex;
        int listed = 0;

        ctrlSymRevalidate();
        ctrlTextAdd(text, "symbols: %d from", ctrlSymCount());
        for (fileIndex = 0; fileIndex < ctrlSymFileCount(); fileIndex++) {
            CtrlSymFile file;
            if (!ctrlSymFileGet(fileIndex, &file) || file.path[0] == 0) {
                continue;
            }
            ctrlTextAdd(text, "%s %s", listed++ > 0 ? "," : "", file.name);
            if (file.slot != CTRL_SYM_SLOT_NONE) {
                ctrlTextAdd(text, " (slot%d.%d)", file.slot, file.subSlot);
            }
            else if (file.maxBank >= 0) {
                ctrlTextAdd(text, " (not bound: its banked labels are not used)");
            }
            else {
                ctrlTextAdd(text, " (no slot)");
            }
        }
        ctrlTextAdd(text, "\n");
    }
    else {
        ctrlTextAdd(text, "no symbols loaded; load a symbol file by its full path\n");
    }
    if (info->canonical.space == CTRL_SPACE_SLOT && info->canonical.segment >= 0 &&
        info->z80Address < 0)
    {
        ctrlTextAdd(text, "this segment is not paged in, so no z80 address is "
                          "true for it right now\n");
    }
    ctrlTextAdd(text, "\n address          z80     bytes        instruction\n");
}

/* One listing row for the instruction at cursor; its length, or 0 when the bytes in
** window end before it does (the row then shows those bytes only). */
static int dasmRow(CtrlText* text, const CtrlAddr* cursor, int segmentSize, Int32 z80,
                   const UInt8* window, int readable, DasmSymbols* context, Int32 pc)
{
    char   printed[48];
    char   mnemonic[R800_DASM_TEXT_MAX];
    const char* own;
    const char* also = NULL;
    Int32  alsoSegment = 0;
    int    length;
    int    byte;

    printShort(cursor, printed, sizeof(printed));
    context->origin      = (UInt16)(z80 >= 0 ? z80 : (Int32)cursor->offset);
    context->offset      = cursor->offset;
    context->segmentSize = segmentSize;
    context->pagedIn     = z80 >= 0;
    length = r800DasmBuf(window, readable, context->origin,
                         dasmSymbol, context, mnemonic, sizeof(mnemonic));

    ctrlTextAdd(text, "%c%-16s ", pc >= 0 && z80 == pc ? '>' : ' ', printed);
    if (z80 >= 0) {
        ctrlTextAdd(text, "0x%04X  ", z80);
    }
    else {
        ctrlTextAdd(text, "-       ");
    }

    if (length == 0) {
        for (byte = 0; byte < readable; byte++) {
            ctrlTextAdd(text, "%02x ", window[byte]);
        }
        ctrlTextAdd(text, "\n");
        return 0;
    }

    for (byte = 0; byte < length; byte++) {
        ctrlTextAdd(text, "%02x ", window[byte]);
    }
    for (; byte < R800_DASM_MAX_LEN; byte++) {
        ctrlTextAdd(text, "   ");
    }

    ctrlTextAdd(text, " %s", mnemonic);

    /* By segment and offset: a segment's labels hold wherever it is paged in now. */
    if (cursor->segment >= 0) {
        own = ctrlSymInSegment(cursor->slot, cursor->subSlot, cursor->segment,
                               cursor->offset);
        /* Unbanked labels hold in every segment, though only where it is paged in now. */
        if (own == NULL && z80 >= 0) {
            own = ctrlSymFind(&context->home, (UInt16)z80, 0, &also, &alsoSegment);
        }
    }
    else {
        own = z80 >= 0
                  ? ctrlSymFind(&context->home, (UInt16)z80, 0, &also, &alsoSegment)
                  : NULL;
    }
    if (own != NULL) {
        ctrlTextAdd(text, "  ; %s", own);
        if (also != NULL && alsoSegment >= 0) {
            ctrlTextAdd(text, "  (also seg%d:%s)", (int)alsoSegment, also);
        }
        else if (also != NULL) {
            ctrlTextAdd(text, "  (also %s)", also);
        }
    }
    ctrlTextAdd(text, "\n");
    return length;
}

/* A segment in two windows is listed through the one pc is in: the Z80 address of the
** segment's start there, or -1. */
static Int32 dasmWindowBase(const CtrlAddr* cursor, Int32 pc)
{
    CtrlAddr     pcAddr;
    CtrlAddrInfo pcInfo;

    if (cursor->segment < 0 || pc < 0) {
        return -1;
    }
    memset(&pcAddr, 0, sizeof(pcAddr));
    pcAddr.space  = CTRL_SPACE_CPU;
    pcAddr.offset = (UInt32)pc;
    if (ctrlAddrResolve(&pcAddr, &pcInfo) &&
        pcInfo.canonical.slot == cursor->slot &&
        pcInfo.canonical.subSlot == cursor->subSlot &&
        pcInfo.canonical.segment == cursor->segment)
    {
        return pc - (Int32)pcInfo.canonical.offset;
    }
    return -1;
}

/* The bytes at cursor up to the end of its segment, at most an instruction's worth. */
static int dasmReadable(const CtrlAddr* cursor, const CtrlAddrInfo* here)
{
    UInt32 remaining = ctrlMemRemaining(cursor);

    /* Clamp to the segment here; ctrlMemRemaining deliberately runs on into the
    ** next one, which the Z80 would not see after the end of a bank. */
    if (cursor->segment >= 0 && here->segmentSize > 0) {
        UInt32 toSegmentEnd = (UInt32)here->segmentSize - cursor->offset;
        if (toSegmentEnd < remaining) {
            remaining = toSegmentEnd;
        }
    }
    return (int)(remaining < R800_DASM_MAX_LEN ? remaining : R800_DASM_MAX_LEN);
}

/* After an instruction the segment's end cuts short: where to read on, if anywhere. */
static void dasmSegmentEndNote(CtrlText* text, const CtrlAddr* cursor, const CtrlAddrInfo* info)
{
    ctrlTextAdd(text,
                " -- the instruction here needs more bytes than this "
                "segment has left.\n"
                "    What follows is decided by the mapper, not by the "
                "segment number.\n");
    if (cursor->segment >= 0 && info->segmentCount >= 0 &&
        cursor->segment + 1 >= info->segmentCount)
    {
        ctrlTextAdd(text, "    seg%d is the last segment of slot%d.%d.\n",
                    cursor->segment, cursor->slot, cursor->subSlot);
    }
    else if (cursor->segment >= 0) {
        char next[48];

        ctrlAddrPrintSegment(next, sizeof(next), 0, cursor->slot, cursor->subSlot,
                             cursor->segment + 1, 0);
        ctrlTextAdd(text, "    To read on, disassemble from %s\n", next);
    }
}

/* Stops at a segment boundary, as the next segment has its own labels; 1 when it did. */
static int dasmSegmentBoundary(CtrlText* text, const CtrlAddr* cursor, const CtrlAddrInfo* info,
                               const DasmSymbols* context)
{
    char next[48];

    if (cursor->space != CTRL_SPACE_SLOT || cursor->segment < 0 ||
        cursor->segment == context->home.segment)
    {
        return 0;
    }
    if (info->segmentCount >= 0 && cursor->segment >= info->segmentCount) {
        ctrlTextAdd(text, " -- end of seg%d, the last segment of slot%d.%d\n",
                    (int)context->home.segment, cursor->slot, cursor->subSlot);
        return 1;
    }
    ctrlAddrPrintSegment(next, sizeof(next), 0, cursor->slot, cursor->subSlot,
                         (int)cursor->segment, 0);
    ctrlTextAdd(text, " -- end of seg%d. The next segment has its own labels; ask again at %s\n",
                (int)context->home.segment, next);
    return 1;
}

/* A listing by slot address stays in its segment, which has labels of its own. */
static void dasmSlotListing(CtrlText* text, CtrlView* view, CtrlAddr cursor,
                            const CtrlAddrInfo* info, int count, Int32 pc,
                            DasmSymbols* context)
{
    Int32 windowBase = dasmWindowBase(&cursor, pc);
    int   i;

    for (i = 0; i < count && !ctrlTextIsFull(text); i++) {
        CtrlAddrInfo here;
        UInt8  window[R800_DASM_MAX_LEN];
        char   printed[48];
        int    readable;
        int    length;
        Int32  z80;

        if (!ctrlAddrResolve(&cursor, &here)) {
            break;
        }
        /* Valid because the listing stops at the end of the segment. */
        z80 = windowBase >= 0 ? windowBase + (Int32)cursor.offset : here.z80Address;
        printShort(&cursor, printed, sizeof(printed));

        readable = dasmReadable(&cursor, &here);
        if (readable <= 0) {
            ctrlTextAdd(text, " -- nothing readable at %s\n", printed);
            break;
        }
        if (ctrlMemRead(view, &cursor, window, (UInt32)readable) != CTRL_MEM_OK) {
            ctrlTextAdd(text, " -- %s could not be read\n", printed);
            break;
        }

        length = dasmRow(text, &cursor, here.segmentSize, z80, window, readable, context, pc);
        if (length == 0) {
            dasmSegmentEndNote(text, &cursor, info);
            break;
        }
        if (!ctrlAddrAdvance(&cursor, (UInt32)length) ||
            dasmSegmentBoundary(text, &cursor, info, context))
        {
            break;
        }
    }
}

/* Bytes as the Z80 sees them: each 8 kB bank through whatever it shows now. */
static int readAsZ80(CtrlView* view, UInt32 z80, UInt8* out, int size)
{
    while (size > 0) {
        CtrlAddr at;
        int      chunk = 0x2000 - (int)(z80 & 0x1fff);

        if (chunk > size) {
            chunk = size;
        }
        memset(&at, 0, sizeof(at));
        at.space  = CTRL_SPACE_CPU;
        at.offset = z80;
        if (ctrlMemRead(view, &at, out, (UInt32)chunk) != CTRL_MEM_OK) {
            return 0;
        }
        out  += chunk;
        z80  += (UInt32)chunk;
        size -= chunk;
    }
    return 1;
}

/* A listing by Z80 address reads on through whatever the Z80 sees at the next one. */
static void dasmZ80Listing(CtrlText* text, CtrlView* view, UInt32 z80, int count, Int32 pc,
                           DasmSymbols* context)
{
    int i;

    for (i = 0; i < count && !ctrlTextIsFull(text); i++) {
        CtrlAddr     at;
        CtrlAddrInfo here;
        UInt8        window[R800_DASM_MAX_LEN];
        char         printed[48];
        int          readable;
        int          length;

        if (z80 > 0xffff) {
            ctrlTextAdd(text, " -- end of the Z80 address space\n");
            break;
        }
        memset(&at, 0, sizeof(at));
        at.space  = CTRL_SPACE_CPU;
        at.offset = z80;
        if (!ctrlAddrResolve(&at, &here)) {
            break;
        }
        printShort(&here.canonical, printed, sizeof(printed));

        /* Another slot or segment from here on has labels of its own. */
        if (here.canonical.slot != context->home.slot ||
            here.canonical.subSlot != context->home.subSlot ||
            here.canonical.segment != context->home.segment)
        {
            ctrlTextAdd(text, " -- from 0x%04X on, the Z80 sees %s\n", (unsigned)z80, printed);
            context->home.slot    = here.canonical.slot;
            context->home.subSlot = here.canonical.subSlot;
            context->home.segment = here.canonical.segment;
        }

        readable = 0x10000 - z80 < R800_DASM_MAX_LEN ? (int)(0x10000 - z80) : R800_DASM_MAX_LEN;
        if (!readAsZ80(view, z80, window, readable)) {
            ctrlTextAdd(text, " -- %s could not be read\n", printed);
            break;
        }

        length = dasmRow(text, &here.canonical, here.segmentSize, (Int32)z80, window, readable,
                         context, pc);
        if (length == 0) {
            ctrlTextAdd(text, " -- the instruction at 0x%04X runs past 0xFFFF\n", (unsigned)z80);
            break;
        }
        z80 += (UInt32)length;
    }
}

void ctrlDasmList(CtrlText* text, CtrlView* view, const CtrlAddr* at, int count, Int32 pc)
{
    CtrlAddrInfo info;
    DasmSymbols  context;

    if (!ctrlAddrResolve(at, &info)) {
        ctrlTextAdd(text, "there is no such address on this machine\n");
        return;
    }

    dasmHeader(text, at, &info);

    context.home.slot    = info.canonical.slot;
    context.home.subSlot = info.canonical.subSlot;
    context.home.segment = info.canonical.segment;

    if (at->space == CTRL_SPACE_CPU) {
        dasmZ80Listing(text, view, at->offset, count, pc, &context);
    }
    else {
        dasmSlotListing(text, view, info.canonical, &info, count, pc, &context);
    }
}
