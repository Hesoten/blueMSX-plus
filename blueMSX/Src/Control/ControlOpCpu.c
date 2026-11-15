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
#include "ControlDasm.h"
#include "ControlSlots.h"
#include "ControlAddressArg.h"
#include "ControlStatus.h"
#include "ControlStopLog.h"
#include "ControlSymbol.h"
#include "SlotManager.h"
#include "R800Dasm.h"
#include "R800Debug.h"
#include <ctype.h>
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

/* The register by its upper-case name, and the values as wide as the register. */
static void noteRegister(const char* name, int bits, UInt32 previous, UInt32 value)
{
    char upper[16];
    char change[32];
    int  digits = bits > 8 ? 4 : 2;
    int  i;

    for (i = 0; name[i] != 0 && i < (int)sizeof(upper) - 1; i++) {
        upper[i] = (char)toupper((unsigned char)name[i]);
    }
    upper[i] = 0;
    snprintf(change, sizeof(change), "0x%0*X -> 0x%0*X", digits, (unsigned)previous,
             digits, (unsigned)value);
    ctrlActivityNoteOn(CTRL_ACT_REG_WRITE, upper, change, CTRL_ACT_DONE);
}

int ctrlOpRegWrite(CtrlCall* call)
{
    int refused;
    const char* name = ctrlCallText(call, "reg");
    const char* valueText = ctrlCallText(call, "value");
    char  err[CTRL_ADDR_ERR_MAX];
    int   bits = 16;
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

    if ((set = ctrlCpuSetRegister(name, value, &previous, &bits, err, sizeof(err))) <= 0) {
        /* The view is held, so the registers are always there; -1 is a bug here. */
        return ctrlReplyError(call, set < 0 ? "internal" : "unwritable", err);
    }
    noteRegister(name, bits, (UInt32)previous, value);
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

/* The longest loop body the history folds, in instructions. */
#define HISTORY_FOLD_MAX 16

/* A run of the history listing: k entries from start, repeated r times back from there. */
typedef struct {
    int start;
    int k;
    int r;
} HistoryRun;

static UInt16 historyPc(const R800DebugHistory* history, UInt32 index)
{
    return history->pc[index & (PC_TRACE_SIZE - 1)];
}

/* Whether the memory an entry's length bytes ran from was switched while or after it ran:
** one that switches its own page was fetched before. */
static int historySwitchedSince(const R800DebugHistory* history, UInt32 index, int length)
{
    UInt16 pc    = historyPc(history, index);
    UInt32 age   = history->newest - index;
    int    first = pc >> 13;
    int    last  = ((pc + length - 1) & 0xffff) >> 13;

    /* By age back from the newest entry; the CPU keeps a page's age short of wrapping. */
    return history->pagesTracked &&
           (history->newest - history->changedAt[first] <= age ||
            history->newest - history->changedAt[last] <= age);
}

/* How many times the k entries before end repeat back to back, back to the first shown. */
static int historyRepeats(const R800DebugHistory* history, UInt32 base, int end, int k)
{
    int repeats = 1;
    int i;

    while (end - k * (repeats + 1) >= 0) {
        int from = end - k * (repeats + 1);

        for (i = 0; i < k; i++) {
            if (historyPc(history, base + (UInt32)(from + i)) !=
                historyPc(history, base + (UInt32)(end - k + i)))
            {
                return repeats;
            }
        }
        repeats++;
    }
    return repeats;
}

/* Splits the shown entries before end into runs, newest first: a loop that covers the most
** entries, or else one entry. Returns how many runs. */
static int historyRuns(const R800DebugHistory* history, UInt32 base, int end, HistoryRun* runs)
{
    int count = 0;

    while (end > 0) {
        int bestK = 1;
        int bestR = 1;
        int k;

        for (k = 2; k <= HISTORY_FOLD_MAX && 2 * k <= end; k++) {
            int repeats = historyRepeats(history, base, end, k);

            if (repeats >= 2 && k * repeats > bestK * bestR) {
                bestK = k;
                bestR = repeats;
            }
        }
        runs[count].start = end - bestK;
        runs[count].k     = bestK;
        runs[count].r     = bestR;
        count++;
        end -= bestK * bestR;
    }
    return count;
}

/* The last instructions the CPU ran, oldest first, each read and disassembled as memory is
** now. The caller must hold a view. */
static void describeHistory(CtrlText* text, CtrlView* view, int max)
{
    HistoryRun runs[CTRL_STACK_HISTORY_MAX];
    R800DebugHistory history;
    UInt32 recorded;
    UInt32 base;
    int    shown;
    int    runCount;
    int    uncertain = 0;
    int    folded    = 0;
    int    inside    = r800DebugInsideInstruction();
    int    newestLength;
    int    i;
    int    m;

    if (!r800DebugHistoryGet(&history)) {
        ctrlTextAdd(text, "\nhistory: this machine records no instruction history\n");
        return;
    }
    recorded = history.newest - history.oldest + 1;
    if (recorded == 0) {
        ctrlTextAdd(text, "\nhistory: nothing recorded yet (since the machine started, or "
                    "since a state saved by an older build was loaded)\n");
        return;
    }
    shown = (UInt32)max < recorded ? max : (int)recorded;
    base  = history.newest - (UInt32)(shown - 1);
    ctrlTextAdd(text, "\nhistory: last %d of %u recorded instructions, oldest first\n",
                shown, (unsigned)recorded);

    /* The newest stays out of the runs so it is always its own '>' line. */
    runCount = historyRuns(&history, base, shown - 1, runs);
    for (i = runCount - 1; i >= 0 && !ctrlTextIsFull(text); i--) {
        int oldestCopy = runs[i].start - runs[i].k * (runs[i].r - 1);

        for (m = 0; m < runs[i].k; m++) {
            CtrlTextMark row = ctrlTextMark(text);
            int length = ctrlDasmLine(text, view,
                                      historyPc(&history, base + (UInt32)(runs[i].start + m)),
                                      ' ');

            /* Not read whole: every byte an instruction can have. */
            if (historySwitchedSince(&history, base + (UInt32)(oldestCopy + m),
                                     length > 0 ? length : R800_DASM_MAX_LEN))
            {
                ctrlTextSetAt(text, row, '?');
                uncertain = 1;
            }
        }
        if (runs[i].r > 1) {
            ctrlTextAdd(text, " x%d the %d lines above\n", runs[i].r, runs[i].k);
            folded = 1;
        }
    }
    newestLength = ctrlDasmLine(text, view, historyPc(&history, history.newest), '>');
    /* Only an instruction in progress can have switched its own memory. */
    if (inside && historySwitchedSince(&history, history.newest,
                                       newestLength > 0 ? newestLength : R800_DASM_MAX_LEN))
    {
        ctrlTextAdd(text, "? the > instruction switched the memory it runs from, so it is "
                    "shown as that memory reads now, which may not be what runs\n");
    }

    if (inside) {
        ctrlTextAdd(text, "> = the instruction in progress: the machine stopped inside it\n");
    }
    else {
        ctrlTextAdd(text, "> = the instruction the CPU runs next\n");
    }
    if (folded) {
        ctrlTextAdd(text, "xN = the lines above it ran N times in a row, shown once\n");
    }
    if (uncertain) {
        ctrlTextAdd(text, "? = the memory there was switched while or after the instruction "
                    "ran, so the slot, bytes and instruction shown (read now) may not be what ran\n");
    }
    if (!history.pagesTracked) {
        ctrlTextAdd(text, "memory switches are not tracked on this machine: every line is read "
                    "through the memory paged in now, which may not be what ran\n");
    }
}

int ctrlOpStack(CtrlCall* call)
{
    int refused;
    Int32 count   = CTRL_STACK_COUNT_DEFAULT;
    Int32 history = CTRL_STACK_HISTORY_DEFAULT;

    count = ctrlCallInt(call, "count", count);
    if (count < CTRL_STACK_COUNT_MIN || count > CTRL_STACK_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be between %d and %d frames",
                               CTRL_STACK_COUNT_MIN, CTRL_STACK_COUNT_MAX);
    }
    history = ctrlCallInt(call, "history", history);
    if (history < CTRL_STACK_HISTORY_MIN || history > CTRL_STACK_HISTORY_MAX) {
        return ctrlReplyErrorf(call, "out_of_range",
                               "history must be between %d and %d instructions",
                               CTRL_STACK_HISTORY_MIN, CTRL_STACK_HISTORY_MAX);
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    describeCallstack(&call->text, (int)count);
    if (history > 0) {
        describeHistory(&call->text, &call->view, (int)history);
    }
    return ctrlReplyDone(call);
}
