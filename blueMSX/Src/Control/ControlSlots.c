/*****************************************************************************
**
** Slot and bank layout.
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
#include "ControlSlots.h"
#include "SlotManager.h"
#include "Debugger.h"
#include "MapperDebugBanks.h"
#include <stdio.h>
#include <string.h>

void ctrlSlotsBankName(char* out, int len, int segment, int inSentence)
{
    if (segment == MAPPER_BANK_SRAM) {
        snprintf(out, (size_t)len, "%s", inSentence ? "the SRAM" : "SRAM");
    }
    else {
        snprintf(out, (size_t)len, "seg%d", segment);
    }
}

/* Whether the bank register over an 8 kB bank of a slot shows other than the program's own
** selection. Only under a view: a hold may not read the mapper. */
static int bankSwitchedAt(int slot, int subSlot, int bank)
{
    const MapperBanks* banks;
    int reg;

    if (dbgAccessHeld() != DBG_ACCESS_VIEW) {
        return 0;
    }
    banks = slotDebugGetBankSwitch(slot, subSlot);
    reg = banks != NULL ? mapperBanksRegisterAt(banks, bank) : -1;
    return reg >= 0 && mapperBanksSwitched(banks, reg);
}

static void bankSwitchLine(CtrlText* text, const MapperBanks* banks, int reg, int kind)
{
    char range[16];
    char shows[16];
    char chosen[16];

    mapperBanksRange(banks, reg, range, sizeof(range));
    ctrlSlotsBankName(shows, sizeof(shows), banks->type->get(banks->ref, reg), 1);
    ctrlSlotsBankName(chosen, sizeof(chosen), banks->program[reg], 1);
    ctrlTextAdd(text, "* switched by the debugger: slot%d.%d %s shows %s, the program "
                "selected %s\n", banks->slot, banks->sslot, range, shows, chosen);
}

void ctrlSlotsDescribeSwitched(CtrlText* text, int kind)
{
    int slot;
    int subSlot;
    int reg;

    if (dbgAccessHeld() != DBG_ACCESS_VIEW) {
        return;
    }
    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            const MapperBanks* banks = slotDebugGetBankSwitch(slot, subSlot);

            for (reg = 0; banks != NULL && reg < banks->type->registers; reg++) {
                if (mapperBanksSwitched(banks, reg)) {
                    bankSwitchLine(text, banks, reg, kind);
                }
            }
        }
    }
}

/* "seg5", "-", with '*' when the debugger switched that bank. */
static void pageSegment(char* out, int len, Int32 segment, int slot, int subSlot, int bank)
{
    const char* mark = bankSwitchedAt(slot, subSlot, bank) ? "*" : "";

    if (segment >= 0) {
        snprintf(out, (size_t)len, "seg%d%s", (int)segment, mark);
    }
    else {
        snprintf(out, (size_t)len, "-%s", mark);
    }
}

static void slotMapRow(CtrlText* text, int page, int bank, int span, int showPage)
{
    int   slot;
    int   subSlot;
    Int32 segment = ctrlSlotsPagedIn((UInt16)(bank * 0x2000), &slot, &subSlot);
    int   state   = slotDebugGetBankState(slot, subSlot, bank);
    char  range[16];
    char  pageText[4];
    char  segmentText[16];

    sprintf(range, "%04X-%04X", bank * 0x2000, bank * 0x2000 + span * 0x2000 - 1);
    if (showPage) {
        sprintf(pageText, "%d", page);
    }
    else {
        pageText[0] = 0;
    }

    ctrlTextAdd(text, " %-4s %-9s  %d.%-3d ", pageText, range, slot, subSlot);

    pageSegment(segmentText, sizeof(segmentText), segment, slot, subSlot, bank);
    /* Wide enough for "seg1023*". */
    ctrlTextAdd(text, "%-8s ", segmentText);

    ctrlTextAdd(text, "%s%s  %s\n",
                (state & SLOT_DEBUG_READABLE) ? "r" : "-",
                (state & SLOT_DEBUG_WRITABLE) ? "w" : "-",
                slotDebugGetContents(slot, subSlot));
}

/* One row for a 16kB segment or a page with no segments; 8kB banks always get two. */
static void slotMapPages(CtrlText* text)
{
    int page;

    ctrlTextAdd(text, " page z80        slot  segment  r/w contents\n");

    for (page = 0; page < 4; page++) {
        int   lowBank  = page * 2;
        int   highBank = lowBank + 1;
        int   pageSlot;
        int   pageSub;
        Int32 lowSegment = ctrlSlotsPagedIn((UInt16)(lowBank * 0x2000), &pageSlot, &pageSub);
        int   whole;

        whole = slotDebugGetSegmentSize(pageSlot, pageSub) >= 0x4000 ||
                (lowSegment < 0 &&
                 slotDebugGetBankSegment(pageSlot, pageSub, highBank) < 0 &&
                 slotDebugGetBankState(pageSlot, pageSub, lowBank) ==
                 slotDebugGetBankState(pageSlot, pageSub, highBank));

        slotMapRow(text, page, lowBank, whole ? 2 : 1, 1);
        if (!whole) {
            slotMapRow(text, page, highBank, 1, 0);
        }
    }
}

/* Every banked device, and how to address its segments. */
static void slotMapBanked(CtrlText* text)
{
    int slot;
    int subSlot;
    int printedHeader = 0;

    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            UInt32 size = 0;
            int    writable = 0;
            int    segmentSize;
            int    kind;

            kind = slotDebugGetStorage(slot, subSlot, NULL, &size, &writable);
            if (kind == SLOT_STORAGE_NONE) {
                continue;
            }
            segmentSize = slotDebugGetSegmentSize(slot, subSlot);
            if (segmentSize <= 0) {
                continue;
            }

            if (!printedHeader) {
                ctrlTextAdd(text, "\nnot all paged in -- address these directly:\n");
                printedHeader = 1;
            }
            ctrlTextAdd(text, "  slot%d.%d  %u kB, segments 0-%u of %u kB, %s  as slot%d.%d:seg<n>+<offset>\n",
                        slot, subSlot,
                        (unsigned)(size / 1024),
                        (unsigned)(size / (UInt32)segmentSize - 1),
                        (unsigned)(segmentSize / 1024),
                        writable ? "writable" : "read only",
                        slot, subSlot);
        }
    }
}

static int slotIsPagedIn(int slot, int subSlot)
{
    int pagedIn = 0;
    int page;

    for (page = 0; page < 4; page++) {
        int pagedSlot;
        int pagedSubSlot;

        ctrlSlotsPageSlot((UInt16)(page << 14), &pagedSlot, &pagedSubSlot);
        pagedIn |= pagedSlot == slot && pagedSubSlot == subSlot;
    }
    return pagedIn;
}

/* Listed so that a cartridge out of view and with no segments does not look missing. */
static void slotMapOutOfView(CtrlText* text)
{
    int slot;
    int subSlot;
    int printedHeader = 0;

    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            if (slotIsPagedIn(slot, subSlot) || slotDebugGetContents(slot, subSlot)[0] == 0 ||
                slotDebugGetSegmentSize(slot, subSlot) > 0)
            {
                continue;
            }
            if (!printedHeader) {
                ctrlTextAdd(text, "\nnot paged in now, so not in the table above. These report no "
                            "segments, so they have no seg addresses: use Z80 addresses while "
                            "the slot is paged in; breakpoints there are plain.\n");
                printedHeader = 1;
            }
            ctrlTextAdd(text, "  slot%d.%d  %s\n", slot, subSlot,
                        slotDebugGetContents(slot, subSlot));
        }
    }
}

void ctrlSlotsPageSlot(UInt16 z80, int* slot, int* subSlot)
{
    int page = z80 >> 14;

    *slot    = slotDebugGetPagePrimarySlot(page);
    *subSlot = slotDebugGetPageSubSlot(page);
}

Int32 ctrlSlotsPagedIn(UInt16 z80, int* slot, int* subSlot)
{
    ctrlSlotsPageSlot(z80, slot, subSlot);
    return slotDebugGetBankSegment(*slot, *subSlot, z80 >> 13);
}

void ctrlSlotsDescribeNoSegments(CtrlText* text, int slot, int subSlot)
{
    ctrlTextAdd(text, "slot%d.%d (%s) reports no segments, so it has no seg addresses: use "
                "Z80 addresses while it is paged in; breakpoints there are plain",
                slot, subSlot, slotDebugGetContents(slot, subSlot));
}

void ctrlSlotsDescribe(CtrlText* text)
{
    slotMapPages(text);
    ctrlSlotsDescribeSwitched(text, CTRL_SLOTS_BANKS_FOOTNOTE);
    slotMapBanked(text);
    slotMapOutOfView(text);
}

void ctrlSlotsDescribePages(CtrlText* text)
{
    int page;

    ctrlTextAdd(text, "pages:  ");
    for (page = 0; page < 4; page++) {
        int   slot;
        int   subSlot;
        Int32 segment = ctrlSlotsPagedIn((UInt16)(page << 14), &slot, &subSlot);
        char  low[16];
        char  high[16];

        pageSegment(low, sizeof(low), segment, slot, subSlot, page * 2);
        if (slotDebugGetSegmentSize(slot, subSlot) == 0x2000) {
            /* 8kB banks: both halves, even when equal, so the form never changes. */
            pageSegment(high, sizeof(high), slotDebugGetBankSegment(slot, subSlot, page * 2 + 1),
                        slot, subSlot, page * 2 + 1);
            ctrlTextAdd(text, " %d:%d.%d/%s,%s", page, slot, subSlot, low, high);
        }
        else if (segment >= 0 || strchr(low, '*') != NULL) {
            ctrlTextAdd(text, " %d:%d.%d/%s", page, slot, subSlot, low);
        }
        else {
            ctrlTextAdd(text, " %d:%d.%d", page, slot, subSlot);
        }
    }
    ctrlTextAdd(text, "\n");
}
