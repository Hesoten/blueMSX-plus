/*****************************************************************************
**
** Slot op handlers.
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
#include "ControlAddressArg.h"
#include "ControlStopLog.h"
#include "ControlClient.h"
#include "ControlCpu.h"
#include "ControlSlots.h"
#include "ControlStatus.h"
#include "MapperDebugBanks.h"
#include "SlotManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEGMENT_TEXT_MAX 16
#define RANGE_TEXT_MAX   16

/* "slot1.0 a8.rom (ASCII8)", as the replies name a cartridge. */
static void slotName(char* out, int len, const MapperBanks* banks)
{
    snprintf(out, (size_t)len, "slot%d.%d %s", banks->slot, banks->sslot,
             slotDebugGetContents(banks->slot, banks->sslot));
}

static const char* blockedText(const MapperBanks* banks, int reg)
{
    return banks->type->blocked != NULL ? banks->type->blocked(banks->ref, reg) : NULL;
}

static void listSlot(CtrlText* text, const MapperBanks* banks)
{
    int reg;

    ctrlTextAdd(text, "slot%d.%d  %s  segments 0-%d of %d kB%s\n", banks->slot, banks->sslot,
                slotDebugGetContents(banks->slot, banks->sslot), mapperBanksSegments(banks) - 1,
                slotDebugGetSegmentSize(banks->slot, banks->sslot) / 1024,
                mapperBanksHasSram(banks) ? ", and its SRAM" : "");
    for (reg = 0; reg < banks->type->registers; reg++) {
        const char* blocked = blockedText(banks, reg);
        char range[RANGE_TEXT_MAX];
        char shows[SEGMENT_TEXT_MAX];
        char program[SEGMENT_TEXT_MAX];

        mapperBanksRange(banks, reg, range, sizeof(range));
        ctrlSlotsBankName(shows, sizeof(shows), banks->type->get(banks->ref, reg), 0);
        ctrlSlotsBankName(program, sizeof(program), banks->program[reg], 1);
        /* A bank switched and then blocked by the program still says it was switched. */
        if (mapperBanksSwitched(banks, reg)) {
            ctrlTextAdd(text, "  %s  %-7s  switched by the debugger; the program selected %s%s%s\n",
                        range, shows, program, blocked != NULL ? "; cannot be switched: " : "",
                        blocked != NULL ? blocked : "");
        }
        else if (blocked != NULL) {
            ctrlTextAdd(text, "  %s  %-7s  cannot be switched: %s\n", range, shows, blocked);
        }
        else {
            ctrlTextAdd(text, "  %s  %s\n", range, shows);
        }
    }
}

/* The slots that report segments but whose banks the debugger cannot switch. */
static void listUnswitchable(CtrlText* text)
{
    int slot;
    int subSlot;

    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            if (slotDebugGetSegmentSize(slot, subSlot) > 0 &&
                slotDebugGetBankSwitch(slot, subSlot) == NULL)
            {
                ctrlTextAdd(text, "slot%d.%d (%s) reports its segments, but the debugger cannot "
                            "switch its banks\n", slot, subSlot, slotDebugGetContents(slot, subSlot));
            }
        }
    }
}

int ctrlOpBankList(CtrlCall* call)
{
    int refused;
    int listed = 0;
    int slot;
    int subSlot;

    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            const MapperBanks* banks = slotDebugGetBankSwitch(slot, subSlot);

            if (banks != NULL) {
                listSlot(&call->text, banks);
                listed++;
            }
        }
    }
    if (listed == 0) {
        ctrlTextAdd(&call->text, "no cartridge in this machine has banks the debugger can switch\n");
    }
    listUnswitchable(&call->text);
    return ctrlReplyDone(call);
}

/* "at" as a Z80 address, and "segment" as a number or "sram"; a negative number gives -1,
** refused as out of range once the slot is known. */
static int refuseBadArguments(CtrlCall* call, CtrlAddr* addr, int* segment)
{
    const char* word;
    int         refused;

    if (!ctrlCallGiven(call, "at")) {
        return ctrlReplyError(call, "missing_at", "give the Z80 address of the bank to switch, "
                              "such as 0x8000");
    }
    if ((refused = ctrlAddrArgRead(call, CTRL_ADDR_ARG_NEEDS_MACHINE, addr)) != 0) {
        return refused;
    }
    if (addr->space != CTRL_SPACE_CPU) {
        return ctrlReplyError(call, "bad_at", "give the Z80 address of the bank, such as 0x8000; to "
                              "read a segment without switching, use slot1.0:seg5+0x0000 in "
                              "bmsx_mem_read or bmsx_dasm");
    }
    word = ctrlCallText(call, "segment");
    if (word == NULL && ctrlCallGiven(call, "segment")) {
        Int32 number = ctrlCallInt(call, "segment", 0);
        *segment = number >= 0 ? (int)number : -1;
        return 0;
    }
    if (word != NULL && _stricmp(word, "sram") == 0) {
        *segment = MAPPER_BANK_SRAM;
        return 0;
    }
    return ctrlReplyError(call, word != NULL ? "bad_segment" : "missing_segment",
                          "give a segment number or \"sram\"");
}

/* The switchable slots, for a refusal: "slot1.0 (a8.rom), ..." or "none". */
static void switchableSlots(char* out, int len)
{
    int at = 0;
    int slot;
    int subSlot;

    snprintf(out, (size_t)len, "none");
    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            if (slotDebugGetBankSwitch(slot, subSlot) != NULL && at < len - 1) {
                at += snprintf(out + at, (size_t)(len - at), "%sslot%d.%d (%.40s)",
                               at > 0 ? ", " : "", slot, subSlot, slotDebugGetContents(slot, subSlot));
            }
        }
    }
}

/* The register that switches the bank at a Z80 address, in the slot paged in there now. */
static int refuseNoRegister(CtrlCall* call, UInt16 z80, MapperBanks** banks, int* reg)
{
    char others[400];
    int  slot;
    int  subSlot;

    ctrlSlotsPageSlot(z80, &slot, &subSlot);
    *banks = slotDebugGetBankSwitch(slot, subSlot);
    *reg = *banks != NULL ? mapperBanksRegisterAt(*banks, z80 >> 13) : -1;
    if (*reg >= 0) {
        return 0;
    }
    switchableSlots(others, sizeof(others));
    return ctrlReplyErrorf(call, "no_bank_switch", "0x%04X shows slot%d.%d (%s), %s; the switchable "
                           "ones: %s", (unsigned)z80, slot, subSlot, slotDebugGetContents(slot, subSlot),
                           *banks != NULL ? "which has no bank register there"
                                          : "whose banks the debugger cannot switch", others);
}

static int refuseNotSelected(CtrlCall* call, const MapperBanks* banks, int reg, int result,
                             const char* why, int segment)
{
    char name[CTRL_CONTENTS_MAX + 16];
    char range[RANGE_TEXT_MAX];

    slotName(name, sizeof(name), banks);
    mapperBanksRange(banks, reg, range, sizeof(range));
    if (result == MAPPER_SELECT_PAST_END && segment < 0) {
        return ctrlReplyErrorf(call, "out_of_range", "a segment number is 0 or more: slot%d.%d "
                               "has segments 0-%d", banks->slot, banks->sslot,
                               mapperBanksSegments(banks) - 1);
    }
    if (result == MAPPER_SELECT_PAST_END) {
        return ctrlReplyErrorf(call, "out_of_range", "seg%d is past the end: slot%d.%d has "
                               "segments 0-%d", segment, banks->slot, banks->sslot,
                               mapperBanksSegments(banks) - 1);
    }
    if (result == MAPPER_SELECT_NEVER) {
        return ctrlReplyErrorf(call, "out_of_range", "seg%d is in the ROM, but the program never "
                               "selects it: %s", segment, why);
    }
    if (result == MAPPER_SELECT_NO_SRAM) {
        return ctrlReplyErrorf(call, "no_sram", "%s has no SRAM to page in", name);
    }
    return ctrlReplyErrorf(call, "not_switchable", "%s: %s cannot be switched: %s", name, range,
                           why);
}

/* What shows now against what showed and what the program chose, and the way back. */
static void describeSwitch(CtrlText* text, const MapperBanks* banks, int reg, int previous)
{
    int  segment = banks->type->get(banks->ref, reg);
    int  program = banks->program[reg];
    char name[CTRL_CONTENTS_MAX + 16];
    char range[RANGE_TEXT_MAX];
    char shows[SEGMENT_TEXT_MAX];
    char showed[SEGMENT_TEXT_MAX];
    char chosen[SEGMENT_TEXT_MAX];

    slotName(name, sizeof(name), banks);
    mapperBanksRange(banks, reg, range, sizeof(range));
    ctrlSlotsBankName(shows, sizeof(shows), segment, 1);
    ctrlSlotsBankName(showed, sizeof(showed), previous, 1);
    ctrlSlotsBankName(chosen, sizeof(chosen), program, 1);
    if (segment == program) {
        ctrlTextAdd(text, previous == program ? "%s: %s already shows %s, the program's own selection\n"
                                              : "%s: %s shows %s again, the program's own selection\n",
                    name, range, shows);
        return;
    }
    ctrlTextAdd(text, "%s: %s shows %s now; it showed %s", name, range, shows, showed);
    if (previous == program) {
        ctrlTextAdd(text, ", which the program selected\n");
    }
    else {
        ctrlTextAdd(text, ", switched by the debugger; the program selected %s\n", chosen);
    }
    if (program == MAPPER_BANK_SRAM) {
        snprintf(chosen, sizeof(chosen), "\"sram\"");
    }
    else {
        snprintf(chosen, sizeof(chosen), "%d", program);
    }
    ctrlTextAdd(text, "the program switches this bank again with its next write to the bank "
                "register; switch back with segment=%s before running on if it should not run "
                "in %s\n", chosen, shows);
}

/* When the CPU runs in the bank just switched, its next instruction comes from there. */
static void tellPc(CtrlText* text, const MapperBanks* banks, int reg)
{
    Int32 pc = ctrlCpuPc();
    char  shows[SEGMENT_TEXT_MAX];
    int   slot;
    int   subSlot;

    if (pc < 0) {
        return;
    }
    ctrlSlotsPageSlot((UInt16)pc, &slot, &subSlot);
    if (slot != banks->slot || subSlot != banks->sslot ||
        mapperBanksRegisterAt(banks, (int)pc >> 13) != reg)
    {
        return;
    }
    ctrlSlotsBankName(shows, sizeof(shows), banks->type->get(banks->ref, reg), 1);
    ctrlTextAdd(text, "the PC (0x%04X) is in this bank: the next instruction comes from %s\n",
                (unsigned)pc, shows);
}

static void outSegment(JsonOut* out, const char* key, int segment)
{
    if (segment == MAPPER_BANK_SRAM) {
        jsonOutString(out, key, "sram");
    }
    else {
        jsonOutInt(out, key, segment);
    }
}

static void noteSwitch(const MapperBanks* banks, int reg, int previous)
{
    char range[RANGE_TEXT_MAX];
    char target[CTRL_ACT_TARGET];
    char showed[SEGMENT_TEXT_MAX];
    char shows[SEGMENT_TEXT_MAX];
    char detail[CTRL_ACT_DETAIL];
    int  slot;
    int  subSlot;

    mapperBanksRange(banks, reg, range, sizeof(range));
    /* "Z80" only when the Z80 sees this slot there now. */
    ctrlSlotsPageSlot((UInt16)strtoul(range, NULL, 16), &slot, &subSlot);
    snprintf(target, sizeof(target),
             slot == banks->slot && subSlot == banks->sslot ? "slot%d.%d (Z80 0x%.4s-0x%.4s)"
                                                            : "slot%d.%d 0x%.4s-0x%.4s",
             banks->slot, banks->sslot, range, range + 5);
    ctrlSlotsBankName(showed, sizeof(showed), previous, 0);
    ctrlSlotsBankName(shows, sizeof(shows), banks->type->get(banks->ref, reg), 0);
    snprintf(detail, sizeof(detail), "%s -> %s", showed, shows);
    ctrlActivityNoteOn(CTRL_ACT_BANK_SWITCH, target, detail, CTRL_ACT_DONE);
}

int ctrlOpBankSet(CtrlCall* call)
{
    CtrlAddr     addr;
    MapperBanks* banks;
    const char*  why;
    const char*  note;
    int refused;
    int segment;
    int reg;
    int previous;
    int result;

    if ((refused = refuseBadArguments(call, &addr, &segment)) != 0 ||
        (refused = ctrlRefuseUnlessViewed(call)) != 0 ||
        (refused = refuseNoRegister(call, (UInt16)addr.offset, &banks, &reg)) != 0)
    {
        return refused;
    }
    previous = banks->type->get(banks->ref, reg);
    result = mapperBanksSelect(banks, reg, segment, &why);
    if (result != MAPPER_SELECT_OK) {
        return refuseNotSelected(call, banks, reg, result, why, segment);
    }
    noteSwitch(banks, reg, previous);
    describeSwitch(&call->text, banks, reg, previous);
    note = banks->type->note != NULL ? banks->type->note(banks->ref, reg) : NULL;
    if (note != NULL) {
        ctrlTextAdd(&call->text, "%s\n", note);
    }
    tellPc(&call->text, banks, reg);
    ctrlStatusDescribe(&call->text, CTRL_STOP_UNKNOWN, CTRL_STATUS_STATE_UNSAID);
    outSegment(&call->out, "segment", banks->type->get(banks->ref, reg));
    outSegment(&call->out, "previous", previous);
    outSegment(&call->out, "program", banks->program[reg]);
    return ctrlReplyDone(call);
}

int ctrlOpSlots(CtrlCall* call)
{
    int refused;

    if ((refused = ctrlRefuseWithoutMachine(call, NULL)) != 0) {
        return refused;
    }
    ctrlSlotsDescribe(&call->text);
    return ctrlReplyDone(call);
}
