/*****************************************************************************
**
** Memory op handlers.
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
#include "ControlVram.h"
#include "ControlAddressArg.h"
#include "ControlCpu.h"
#include "ControlDasm.h"
#include "ControlMemory.h"
#include "ControlProtocol.h"
#include "Emulator.h"
#include "SlotManager.h"
#include "VDP.h"
#include "DebugDeviceManager.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Room for the largest read, and a write's bytes. */
static UInt8 dataBuffer[CTRL_MEM_READ_COUNT_MAX];

/* "48 65 6C 6C 6F", "0x12,0x34", or plain "4865". -1 when it holds more than max bytes. */
static int parseHexBytes(const char* text, UInt8* out, UInt32 max, UInt32* count)
{
    UInt32 written = 0;
    int    high = -1;

    *count = 0;
    while (*text != 0) {
        int digit;

        if (*text == ' ' || *text == ',' || *text == '\t' || *text == '_') {
            text++;
            continue;
        }
        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            text += 2;
            continue;
        }
        if (!isxdigit((unsigned char)*text)) {
            return 0;
        }
        digit = ctrlTextHexDigit((unsigned char)*text);
        text++;

        if (high < 0) {
            high = digit;
            continue;
        }
        if (written >= max) {
            return -1;
        }
        out[written++] = (UInt8)((high << 4) | digit);
        high = -1;
    }

    if (high >= 0) {
        /* Odd digit count: refuse rather than guess where the nibble goes. */
        return 0;
    }
    *count = written;
    return written > 0;
}

/* Where an address shows, and which slot a Z80 address names, follow the paging; video
** memory and a segment past the end do not. */
static int answerFollowsPaging(const CtrlAddr* addr)
{
    CtrlAddrInfo info;

    return addr->space != CTRL_SPACE_VRAM && ctrlAddrResolve(addr, &info) &&
           !ctrlAddrPastEnd(&info);
}

/* In a screen mode that interleaves video memory, the line that says which byte of the chips
** a vram: address inside video memory is, as the debugger window shows them. */
static void describeVramByte(CtrlText* text, UInt32 address, int mode)
{
    char   screen[32];
    UInt32 size = 0;
    UInt32 index = (UInt32)debugDeviceVramIndex(mode, (int)address);

    if (!vdpVramInterleaved(mode)) {
        return;
    }
    vdpDebugGetVram(&size);
    if (index >= size) {
        ctrlTextAdd(text, "  ");
        ctrlVramDescribeUnbacked(text, address, mode);
        ctrlTextAdd(text, "\n");
        return;
    }
    ctrlVramModeShortName(screen, sizeof(screen), mode);
    ctrlTextAdd(text, "  video memory byte 0x%05X in the %s layout, where the debugger window's "
                "VRAM view shows it\n", (unsigned)index, screen);
}

int ctrlOpMemResolve(CtrlCall* call)
{
    int refused;
    CtrlAddr addr;
    CtrlAddrInfo info;

    if ((refused = ctrlAddrArgRead(call, CTRL_ADDR_ARG_NEEDS_MACHINE, &addr)) != 0) {
        return refused;
    }
    /* Read from the layout of a running machine, it could be another answer a moment later. */
    if (call->atUnread || (call->view.held != DBG_ACCESS_VIEW && answerFollowsPaging(&addr))) {
        return ctrlRefuseUnlessViewed(call);
    }
    ctrlAddrDescribe(&call->text, &addr);
    /* The layout is known only while the machine is stopped, and past the end there is no byte. */
    if (addr.space == CTRL_SPACE_VRAM && call->view.held == DBG_ACCESS_VIEW &&
        ctrlAddrResolve(&addr, &info) && info.readable)
    {
        describeVramByte(&call->text, addr.offset, vdpDebugGetScreenMode());
    }
    return ctrlReplyDone(call);
}

/* A CPU read goes through the device, which may answer for itself; compares with storage
** up to the end of the first window only, as the next may be another segment. Returns
** how many of the compared bytes differ; stored gets the storage address. */
static UInt32 countStorageDiffer(const CtrlAddr* addr, UInt32 count, CtrlAddrInfo* stored,
                                 UInt32* compared)
{
    UInt8* storage = NULL;
    UInt32 size = 0;
    UInt32 differ = 0;
    UInt32 at;
    UInt32 i;

    *compared = 0;
    if (addr->space != CTRL_SPACE_CPU || !ctrlAddrResolve(addr, stored) ||
        stored->canonical.segment < 0 || stored->segmentSize <= 0)
    {
        return 0;
    }
    at = (UInt32)stored->canonical.segment * (UInt32)stored->segmentSize +
         stored->canonical.offset;
    *compared = (UInt32)stored->segmentSize - stored->canonical.offset;
    if (*compared > count) {
        *compared = count;
    }
    if (slotDebugGetStorage(stored->canonical.slot, stored->canonical.subSlot,
                            &storage, &size, NULL) != SLOT_STORAGE_NONE &&
        storage != NULL && at + *compared <= size)
    {
        for (i = 0; i < *compared; i++) {
            differ += storage[at + i] != dataBuffer[i];
        }
    }
    return differ;
}

/* Paged out, nothing there, or any answer that follows the paging is none from a layout that
** runs on: without a view of the stopped machine the request is refused as not paused. */
static int refuseLayoutUnsettled(CtrlCall* call, int result, int byPaging)
{
    if (!byPaging && result != CTRL_MEM_PAGEDOUT && result != CTRL_MEM_NOTPRESENT) {
        return 0;
    }
    return ctrlRefuseUnlessViewed(call);
}

/* A read that failed, or would: why, with how many bytes are left past the end. That count
** is left out where every other byte of video memory may be missing; the screen mode that
** says so is read only under a view. */
static int refuseUnread(CtrlCall* call, int result, const CtrlAddr* addr)
{
    int    mode = call->view.held == DBG_ACCESS_VIEW ? vdpDebugGetScreenMode() : -1;
    UInt32 left;

    if (result == CTRL_MEM_PASTEND &&
        (addr->space != CTRL_SPACE_VRAM || !ctrlVramLacksBank(mode)))
    {
        left = ctrlMemRemaining(addr);
        return ctrlReplyErrorf(call, "unreadable", "%s. %u byte%s left from there",
                               ctrlMemErrorText(result), (unsigned)left,
                               left == 1 ? " is" : "s are");
    }
    return ctrlReplyError(call, result == CTRL_MEM_DENIED ? "revoked" : "unreadable",
                          ctrlMemErrorText(result));
}

static char printable(UInt8 byte)
{
    return (byte >= 0x20 && byte < 0x7f) ? (char)byte : '.';
}

/* 16 bytes to a line, hex and ASCII; runs of one repeated byte collapse to one line. */
static void describeHexDump(CtrlText* text, const CtrlAddr* addr,
                            const UInt8* data, UInt32 size)
{
    UInt32 at = 0;

    ctrlAddrDescribe(text, addr);
    ctrlTextAdd(text, "%u byte%s\n", (unsigned)size, size == 1 ? "" : "s");

    while (at < size && !ctrlTextIsFull(text)) {
        UInt32 lineBytes = size - at < 16 ? size - at : 16;
        UInt32 i;

        /* A run of one byte value three lines or longer becomes one line. */
        if (lineBytes == 16) {
            UInt32 run = 0;
            UInt8  value = data[at];

            while (at + run < size && data[at + run] == value) {
                run++;
            }
            if (run >= 48) {
                UInt32 whole = run - (run % 16);
                ctrlTextAdd(text, "%04X  all %02X  (%u bytes)\n",
                            (unsigned)(addr->offset + at), value, (unsigned)whole);
                at += whole;
                continue;
            }
        }

        ctrlTextAdd(text, "%04X ", (unsigned)(addr->offset + at));
        for (i = 0; i < 16; i++) {
            if (i == 8) {
                ctrlTextAdd(text, " ");
            }
            if (i < lineBytes) {
                ctrlTextAdd(text, " %02X", data[at + i]);
            }
            else {
                ctrlTextAdd(text, "   ");
            }
        }
        ctrlTextAdd(text, "  ");
        for (i = 0; i < lineBytes; i++) {
            ctrlTextAdd(text, "%c", printable(data[at + i]));
        }
        ctrlTextAdd(text, "\n");
        at += lineBytes;
    }
}

int ctrlOpMemRead(CtrlCall* call)
{
    int refused;
    CtrlAddr addr;
    Int32    count = CTRL_MEM_READ_COUNT_DEFAULT;
    UInt32   compared = 0;
    UInt32   differ = 0;
    CtrlAddrInfo stored;
    int      result;
    int      byPaging;

    if ((refused = ctrlAddrArgRead(call, CTRL_ADDR_ARG_NEEDS_MACHINE, &addr)) != 0) {
        return refused;
    }
    count = ctrlCallInt(call, "count", count);
    if (count < CTRL_MEM_READ_COUNT_MIN || count > CTRL_MEM_READ_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be between %d and %d",
                               CTRL_MEM_READ_COUNT_MIN, CTRL_MEM_READ_COUNT_MAX);
    }
    if ((refused = ctrlAddrArgRefusePastEnd(call, &addr)) != 0) {
        return refused;
    }
    if ((result = ctrlMemCheck(&addr, (UInt32)count, 0, &byPaging)) != CTRL_MEM_OK) {
        refused = refuseLayoutUnsettled(call, result, byPaging);
        return refused != 0 ? refused : refuseUnread(call, result, &addr);
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    result = ctrlMemRead(&call->view, &addr, dataBuffer, (UInt32)count);
    if (result == CTRL_MEM_NOVRAM) {
        return ctrlAddrArgRefuseNoVram(call, addr.offset, (UInt32)count, vdpDebugGetScreenMode());
    }
    if (result != CTRL_MEM_OK) {
        return refuseUnread(call, result, &addr);
    }
    differ = countStorageDiffer(&addr, (UInt32)count, &stored, &compared);
    describeHexDump(&call->text, &addr, dataBuffer, (UInt32)count);
    if (differ > 0) {
        char printed[48];
        ctrlAddrPrint(&stored.canonical, printed, sizeof(printed));
        ctrlTextAdd(&call->text, "%u of the first %u bytes are not what %s holds: the "
                    "cartridge answers CPU reads there itself (e.g. SCC registers or "
                    "flash status). Read %s for the stored bytes.\n",
                    (unsigned)differ, (unsigned)compared, printed, printed);
    }
    return ctrlReplyDone(call);
}

/* What was written, so a wrong parse shows: one byte in hex and decimal, else the first 16. */
static void echoWritten(char* echo, int echoLen, UInt32 count)
{
    if (count == 1) {
        snprintf(echo, (size_t)echoLen, "1 byte, 0x%02X (%u),", dataBuffer[0],
                 (unsigned)dataBuffer[0]);
    }
    else {
        UInt32 i;
        int    at = snprintf(echo, (size_t)echoLen, "%u bytes,", (unsigned)count);

        for (i = 0; i < count && i < 16; i++) {
            at += snprintf(echo + at, (size_t)echoLen - (size_t)at, " %02X", dataBuffer[i]);
        }
        snprintf(echo + at, (size_t)echoLen - (size_t)at, "%s", count > 16 ? " ...," : ",");
    }
}

/* A CPU-address write always reaches the bus (it may be a bank select), but onto
** read-only storage it must not be reported as written. */
static void reportBusWrite(CtrlCall* call, const char* echo, const CtrlAddr* addr)
{
    ctrlTextAdd(&call->text, "put %s on the bus at ", echo);
    ctrlAddrDescribe(&call->text, addr);
    ctrlTextAdd(&call->text,
                "That is not RAM this emulator writes itself, so the device in "
                "that slot decides what the bytes do: often nothing on plain ROM; "
                "on a mapper or flash cartridge, a bank selector, a register, or "
                "flash programming (a flash cartridge may keep that in its save "
                "file). Read the address back, and the slot map, to see what "
                "changed. A write that goes to the bus cannot be undone. A bank it "
                "selects shows as switched by the debugger: see bmsx_bank.\n");
}

int ctrlOpMemWrite(CtrlCall* call)
{
    int refused;
    CtrlAddr     addr;
    CtrlAddrInfo info;
    const char* hex;
    UInt32   count = 0;
    int      parsed;
    int      result;
    int      wentToStorage;
    int      byPaging;
    char     echo[80];

    if ((refused = ctrlAddrArgRead(call, CTRL_ADDR_ARG_NEEDS_MACHINE, &addr)) != 0) {
        return refused;
    }
    if ((hex = ctrlCallText(call, "data")) == NULL) {
        return ctrlReplyError(call, "missing_data",
                              "a memory write needs \"data\" as hex, for example \"3E 01 C9\"");
    }
    parsed = parseHexBytes(hex, dataBuffer, CTRL_MEM_WRITE_MAX, &count);
    if (parsed < 0) {
        return ctrlReplyErrorf(call, "arg_too_long",
                               "\"data\" holds more than %d bytes, the most one "
                               "write takes in any form. Split it into several writes",
                               CTRL_MEM_WRITE_MAX);
    }
    if (!parsed) {
        return ctrlReplyError(call, "bad_data",
                              "\"data\" must be whole hex bytes, for example \"3E 01 C9\"");
    }
    if ((refused = ctrlAddrArgRefusePastEnd(call, &addr)) != 0) {
        return refused;
    }
    if ((result = ctrlMemCheck(&addr, count, 1, &byPaging)) != CTRL_MEM_OK) {
        refused = refuseLayoutUnsettled(call, result, byPaging);
        return refused != 0 ? refused
                            : ctrlReplyError(call, "unwritable", ctrlMemErrorText(result));
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    result = ctrlMemWrite(&call->view, &addr, dataBuffer, count);
    /* Resolve while the view is held: a page switch would change the answer. */
    wentToStorage = ctrlAddrResolve(&addr, &info) && info.writable;

    if (result == CTRL_MEM_NOVRAM) {
        return ctrlAddrArgRefuseNoVram(call, addr.offset, count, vdpDebugGetScreenMode());
    }
    if (result != CTRL_MEM_OK) {
        return ctrlReplyError(call,
                              result == CTRL_MEM_DENIED ? "revoked" : "unwritable",
                              ctrlMemErrorText(result));
    }
    echoWritten(echo, sizeof(echo), count);
    if (!wentToStorage) {
        reportBusWrite(call, echo, &addr);
    }
    else {
        ctrlTextAdd(&call->text, "wrote %s to ", echo);
        ctrlAddrDescribe(&call->text, &addr);
        if (ctrlMemUndoWhyNot() != NULL) {
            ctrlTextAdd(&call->text, "no undo: %s.\n", ctrlMemUndoWhyNot());
        }
    }
    jsonOutUInt(&call->out, "written", count);
    jsonOutBool(&call->out, "undoable", ctrlMemUndoWhyNot() == NULL);
    jsonOutInt(&call->out, "undo_depth", ctrlMemUndoDepth());
    return ctrlReplyDone(call);
}

/* Nothing was put back: the reason, naming the write when undone says which. */
static int refuseNothingUndone(CtrlCall* call, int result, const CtrlAddr* undone)
{
    char where[64] = "";

    if (result != CTRL_MEM_NOUNDO && result != CTRL_MEM_STALE &&
        result != CTRL_MEM_DENIED)
    {
        ctrlAddrPrint(undone, where, sizeof(where));
    }
    if (where[0] != 0) {
        return ctrlReplyErrorf(call, "unwritable", "%s at %s",
                               ctrlMemErrorText(result), where);
    }
    return ctrlReplyError(call,
                          result == CTRL_MEM_NOUNDO ? "nothing_to_undo" :
                          result == CTRL_MEM_STALE   ? "stale_undo" :
                          result == CTRL_MEM_DENIED  ? "revoked" : "unwritable",
                          ctrlMemErrorText(result));
}

/* Where an undo put video memory back, when the screen mode now lays it out otherwise. */
static void tellUndoLayout(CtrlCall* call, const CtrlAddr* undone, int vramMode)
{
    char screen[32];

    if (undone->space != CTRL_SPACE_VRAM || vdpVramSameLayout(vramMode, vdpDebugGetScreenMode())) {
        return;
    }
    ctrlVramModeShortName(screen, sizeof(screen), vramMode);
    ctrlTextAdd(&call->text, "  (addresses as %s laid out video memory at the write)\n", screen);
}

int ctrlOpUndo(CtrlCall* call)
{
    int refused;
    CtrlAddr undone;
    UInt32   size = 0;
    int      vramMode = 0;
    Int32    count = CTRL_UNDO_COUNT_DEFAULT;
    Int32    done = 0;
    int      result = CTRL_MEM_OK;

    /* ctrlMemUndo may leave this untouched, and it is printed on failure. */
    memset(&undone, 0, sizeof(undone));

    count = ctrlCallInt(call, "count", count);
    if (count < CTRL_UNDO_COUNT_MIN || count > CTRL_UNDO_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be between %d and %d writes",
                               CTRL_UNDO_COUNT_MIN, CTRL_UNDO_COUNT_MAX);
    }
    if ((refused = ctrlRefuseWithoutMachine(call, NULL)) != 0) {
        return refused;
    }
    /* An empty journal is told without a view, as an argument would be. */
    if (ctrlMemUndoDepth() == 0) {
        return refuseNothingUndone(call, CTRL_MEM_NOUNDO, &undone);
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    while (done < count) {
        result = ctrlMemUndo(&call->view, &undone, &size, &vramMode);
        if (result != CTRL_MEM_OK) {
            break;
        }
        done++;
        ctrlTextAdd(&call->text, "put %u byte%s back at ", (unsigned)size,
                    size == 1 ? "" : "s");
        ctrlAddrDescribe(&call->text, &undone);
        tellUndoLayout(call, &undone, vramMode);
    }

    if (done == 0) {
        return refuseNothingUndone(call, result, &undone);
    }
    ctrlTextAdd(&call->text,
                "The bytes are back. Anything the program did after reading "
                "them is not.\n");
    if (result != CTRL_MEM_OK && result != CTRL_MEM_NOUNDO) {
        ctrlTextAdd(&call->text, "stopped after %d: %s\n", (int)done,
                    ctrlMemErrorText(result));
    }
    else if (done < count) {
        ctrlTextAdd(&call->text, "undid %d of the %d asked for; there were no more writes "
                    "to undo\n", (int)done, (int)count);
    }
    jsonOutInt(&call->out, "undone", done);
    jsonOutInt(&call->out, "undo_depth", ctrlMemUndoDepth());
    return ctrlReplyDone(call);
}

int ctrlOpDasm(CtrlCall* call)
{
    int refused;
    CtrlAddr addr;
    CtrlAddrInfo info;
    Int32    count = CTRL_DASM_COUNT_DEFAULT;

    if ((refused = ctrlAddrArgRead(call, CTRL_ADDR_ARG_NEEDS_MACHINE, &addr)) != 0) {
        return refused;
    }
    count = ctrlCallInt(call, "count", count);
    if (count < CTRL_DASM_COUNT_MIN || count > CTRL_DASM_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be between %d and %d instructions",
                               CTRL_DASM_COUNT_MIN, CTRL_DASM_COUNT_MAX);
    }
    if ((refused = ctrlAddrArgResolve(call, &addr, &info)) != 0) {
        return refused;
    }
    if ((refused = ctrlAddrArgRefusePastEnd(call, &addr)) != 0) {
        return refused;
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    ctrlDasmList(&call->text, &call->view, &addr, (int)count, ctrlCpuPc());
    return ctrlReplyDone(call);
}
