/*****************************************************************************
**
** Value search op handlers.
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
#include "ControlClient.h"
#include "ControlSymbol.h"
#include "ControlHunt.h"
#include "ControlMemory.h"
#include "ControlProtocol.h"
#include "VDP.h"
#include <stdio.h>
#include <string.h>

/* No hunt to go on with, or one about a machine since reset, replaced or put back, which
** ends it: its candidates would be printed against memory that means something else now. */
static int refuseWithoutHunt(CtrlCall* call)
{
    if (call->session->hunt == NULL) {
        return ctrlReplyError(call, "no_hunt",
                              "start a hunt first. A hunt belongs to the connection that "
                              "started it, and a cancelled call reconnects without it");
    }
    if (!ctrlHuntIsValid(call->session->hunt)) {
        ctrlHuntAbandon(call->session->hunt);
        call->session->hunt = NULL;
        return ctrlReplyError(call, "stale_hunt",
                              "the machine was reset, replaced or put back to an earlier "
                              "moment, so the candidates are about memory that no longer means "
                              "the same thing. Start again.");
    }
    return 0;
}

static int huntCompareFromName(const char* name, int* compare)
{
    static const struct { const char* name; int compare; } table[] = {
        { "changed",      CTRL_HUNT_CHANGED      },
        { "unchanged",    CTRL_HUNT_UNCHANGED    },
        { "increased",    CTRL_HUNT_INCREASED    },
        { "decreased",    CTRL_HUNT_DECREASED    },
        { "increased_by", CTRL_HUNT_INCREASED_BY },
        { "decreased_by", CTRL_HUNT_DECREASED_BY },
        { "eq",           CTRL_HUNT_EQ           },
        { "ne",           CTRL_HUNT_NE           },
        { "lt",           CTRL_HUNT_LT           },
        { "le",           CTRL_HUNT_LE           },
        { "gt",           CTRL_HUNT_GT           },
        { "ge",           CTRL_HUNT_GE           },
        { "in_range",     CTRL_HUNT_IN_RANGE     }
    };
    int i;

    for (i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++) {
        if (strcmp(name, table[i].name) == 0) {
            *compare = table[i].compare;
            return 1;
        }
    }
    return 0;
}

static int huntWidthFromName(const char* name, int* width)
{
    static const struct { const char* name; int width; } table[] = {
        { "u8",  CTRL_HUNT_U8  }, { "u16", CTRL_HUNT_U16 },
        { "u32", CTRL_HUNT_U32 }, { "s8",  CTRL_HUNT_S8  },
        { "s16", CTRL_HUNT_S16 }, { "s32", CTRL_HUNT_S32 }
    };
    int i;

    for (i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++) {
        if (strcmp(name, table[i].name) == 0) {
            *width = table[i].width;
            return 1;
        }
    }
    return 0;
}

/* A value the width cannot hold is never what the hunt reads there, so it is refused. */
static int refuseOutsideWidth(CtrlCall* call, int compare, Int64 number, Int64 number2)
{
    static const struct { const char* text; Int64 low; Int64 high; } range[] = {
        { "a u8 hunt holds 0 to 255",                    0,               0xFF       },
        { "a u16 hunt holds 0 to 65535",                 0,               0xFFFF     },
        { "a u32 hunt holds 0 to 0xFFFFFFFF",            0,               0xFFFFFFFF },
        { "an s8 hunt holds -128 to 127",                -0x80,           0x7F       },
        { "an s16 hunt holds -32768 to 32767",           -0x8000,         0x7FFF     },
        { "an s32 hunt holds -0x80000000 to 0x7FFFFFFF", -0x7FFFFFFF - 1, 0x7FFFFFFF }
    };
    int         w;
    const char* key = NULL;

    if (compare < CTRL_HUNT_EQ) {
        return 0;
    }
    w = ctrlHuntWidth(call->session->hunt);
    if (number < range[w].low || number > range[w].high) {
        key = "value";
    }
    else if (compare == CTRL_HUNT_IN_RANGE &&
             (number2 < range[w].low || number2 > range[w].high))
    {
        key = "value2";
    }
    if (key == NULL) {
        return 0;
    }
    return ctrlReplyErrorf(call, "bad_value", "\"%s\" is outside what the hunt holds: %s",
                           key, range[w].text);
}

static int huntSigned(int width, UInt32 value)
{
    return width == CTRL_HUNT_S8  ? (Int8)value :
           width == CTRL_HUNT_S16 ? (Int16)value : (Int32)value;
}

/* Says when the hunt left video memory out, or when the VDP now lays it out otherwise than
** the vram: addresses of the hunt number it. */
static void describeHuntLayout(CtrlText* text, const CtrlHunt* hunt)
{
    char   began[32];
    char   now[32];
    int    mode = ctrlHuntVramMode(hunt);
    UInt32 size = 0;

    ctrlVramModeShortName(began, sizeof(began), mode);
    if (ctrlHuntVramLeftOut(hunt)) {
        vdpDebugGetVram(&size);
        ctrlTextAdd(text, "video memory is left out: %s needs 128 kB and this VDP has %u kB\n",
                    began, (unsigned)(size / 1024));
        return;
    }
    if (vdpVramSameLayout(mode, vdpDebugGetScreenMode())) {
        return;
    }
    ctrlVramModeShortName(now, sizeof(now), vdpDebugGetScreenMode());
    ctrlTextAdd(text, "the hunt began in the %s layout of video memory and the VDP is now in %s, "
                "which lays it out differently; the vram: addresses below are as %s numbers "
                "them\n", began, now, began);
}

static void describeHuntList(CtrlText* text, const CtrlHunt* hunt,
                             UInt32 first, UInt32 max)
{
    CtrlHuntCandidate found[64];
    UInt32 total = ctrlHuntCandidateCount(hunt);
    int    width = ctrlHuntWidth(hunt);
    int    filled;
    int    i;

    if (max > 64) {
        max = 64;
    }
    ctrlTextAdd(text, "candidates: %u\n", (unsigned)total);

    filled = ctrlHuntList(hunt, first, max, found);
    for (i = 0; i < filled; i++) {
        char printed[64];

        ctrlAddrPrint(&found[i].addr, printed, sizeof(printed));
        if (width >= CTRL_HUNT_S8) {
            /* A signed hunt prints its values signed, as its compares read them. */
            ctrlTextAdd(text, "  %-28s %d -> %d",
                        printed,
                        huntSigned(width, found[i].prevValue),
                        huntSigned(width, found[i].curValue));
        }
        else {
            ctrlTextAdd(text, "  %-28s %u -> %u",
                        printed,
                        (unsigned)found[i].prevValue,
                        (unsigned)found[i].curValue);
        }
        if (found[i].addr.space == CTRL_SPACE_SLOT &&
            found[i].addr.segment >= 0)
        {
            const char* symbol = ctrlSymInSegment(found[i].addr.slot,
                                                  found[i].addr.subSlot,
                                                  found[i].addr.segment,
                                                  found[i].addr.offset);
            if (symbol != NULL) {
                ctrlTextAdd(text, "  %s", symbol);
            }
        }
        if (found[i].z80Address >= 0) {
            ctrlTextAdd(text, "   z80 0x%04X\n", found[i].z80Address);
        }
        /* Video memory has no Z80 address to be paged in at. */
        else if (found[i].addr.space == CTRL_SPACE_VRAM) {
            ctrlTextAdd(text, "\n");
        }
        else {
            ctrlTextAdd(text, "   not paged in\n");
        }
    }

    if (total > first + (UInt32)filled) {
        ctrlTextAdd(text, "  ... %u more, ask again from %u\n",
                    (unsigned)(total - first - (UInt32)filled),
                    (unsigned)(first + (UInt32)filled));
    }
}

int ctrlOpHuntStart(CtrlCall* call)
{
    int refused;
    const char* widthName = ctrlCallText(call, "width");
    Int32 stride = ctrlCallInt(call, "stride", 0);
    int   width;
    CtrlHunt* started;

    if (widthName == NULL) {
        widthName = CTRL_HUNT_START_WIDTH_DEFAULT;
    }
    if (!huntWidthFromName(widthName, &width)) {
        return ctrlReplyError(call, "bad_width",
                              "\"width\" must be u8, u16, u32, s8, s16 or s32");
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }

    /* The old hunt is replaced only by one that started: a refused start keeps its candidates. */
    started = ctrlHuntStart(&call->view, width, stride);
    if (started == NULL) {
        if (!ctrlClientAllowed(ctrlClientCurrent())) {
            return ctrlReplyError(call, "revoked", ctrlMemErrorText(CTRL_MEM_DENIED));
        }
        return ctrlReplyError(call, "no_hunt",
                              "there is no writable memory to hunt through on this machine");
    }
    ctrlHuntAbandon(call->session->hunt);
    call->session->hunt = started;
    ctrlTextAdd(&call->text,
                "hunting %u kB, %u candidates.\n"
                "Now let the machine change the value you are looking for, then\n"
                "refine with compare = increased, decreased, unchanged, changed\n"
                "or eq with a value. Repeat until a handful are left.\n",
                (unsigned)(ctrlHuntScannableSize(call->session->hunt) / 1024),
                (unsigned)ctrlHuntCandidateCount(call->session->hunt));
    describeHuntLayout(&call->text, call->session->hunt);
    if (width != CTRL_HUNT_U8 && width != CTRL_HUNT_S8 && stride <= 0) {
        ctrlTextAdd(&call->text,
                    "Every byte offset is a candidate, so they overlap: a %d byte "
                    "value at X also shows at nearby offsets until an exact value "
                    "(eq) rules those out.\n",
                    width == CTRL_HUNT_U16 || width == CTRL_HUNT_S16 ? 2 : 4);
    }
    jsonOutUInt(&call->out, "candidates", ctrlHuntCandidateCount(call->session->hunt));
    return ctrlReplyDone(call);
}

int ctrlOpHuntRefine(CtrlCall* call)
{
    int refused;
    const char* compareName = ctrlCallText(call, "compare");
    Int32 value = 0;
    Int32 value2 = 0;
    Int64 number = 0;
    Int64 number2 = 0;
    UInt32 remaining = 0;
    int   compare;

    if ((refused = refuseWithoutHunt(call)) != 0) {
        return refused;
    }
    if (compareName == NULL || !huntCompareFromName(compareName, &compare)) {
        return ctrlReplyError(call, "bad_compare",
                              "\"compare\" must be one of changed, unchanged, increased, "
                              "decreased, increased_by, decreased_by, eq, ne, lt, le, gt, "
                              "ge, in_range");
    }
    /* Left out, either would compare against 0. */
    if (!ctrlCallInt64(call, "value", &number) && compare != CTRL_HUNT_CHANGED &&
        compare != CTRL_HUNT_UNCHANGED && compare != CTRL_HUNT_INCREASED &&
        compare != CTRL_HUNT_DECREASED)
    {
        return ctrlReplyErrorf(call, "missing_value", "compare %s needs \"value\"", compareName);
    }
    if (!ctrlCallInt64(call, "value2", &number2) && compare == CTRL_HUNT_IN_RANGE) {
        return ctrlReplyError(call, "missing_value2", "in_range needs \"value2\", the upper bound");
    }
    if (number < -0x7FFFFFFF - 1 || number > 0xFFFFFFFF ||
        number2 < -0x7FFFFFFF - 1 || number2 > 0xFFFFFFFF)
    {
        return ctrlReplyError(call, "bad_value",
                              "\"value\" and \"value2\" must be whole numbers from "
                              "-0x80000000 to 0xFFFFFFFF");
    }
    if ((refused = refuseOutsideWidth(call, compare, number, number2)) != 0) {
        return refused;
    }
    /* Only the 32 bits go on; the compare reads them at the hunt's width and sign. */
    value = (Int32)(UInt32)number;
    value2 = (Int32)(UInt32)number2;

    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    if (!ctrlHuntRefine(call->session->hunt, &call->view, compare, value, value2, &remaining)) {
        if (!ctrlClientAllowed(ctrlClientCurrent())) {
            return ctrlReplyError(call, "revoked", ctrlMemErrorText(CTRL_MEM_DENIED));
        }
        return ctrlReplyError(call, "stale_hunt",
                              "the hunt no longer describes the machine that is running");
    }

    ctrlTextAdd(&call->text, "%s: %u candidates left\n",
                ctrlHuntCompareText(compare), (unsigned)remaining);
    describeHuntLayout(&call->text, call->session->hunt);
    if (remaining > 0 && remaining <= 20) {
        describeHuntList(&call->text, call->session->hunt, 0, 20);
    }
    if (remaining == 0) {
        ctrlTextAdd(&call->text,
                    "Nothing matched. If the machine did not run between this "
                    "refine and the last one, nothing can have changed -- let it "
                    "run, or make the value change, and start a new "
                    "hunt.\n");
    }
    jsonOutUInt(&call->out, "candidates", remaining);
    return ctrlReplyDone(call);
}

int ctrlOpHuntList(CtrlCall* call)
{
    int refused;
    Int32 first = CTRL_HUNT_LIST_FIRST_DEFAULT;
    Int32 count = CTRL_HUNT_LIST_COUNT_DEFAULT;

    if ((refused = refuseWithoutHunt(call)) != 0) {
        return refused;
    }
    first = ctrlCallInt(call, "first", first);
    count = ctrlCallInt(call, "count", count);
    if (first < CTRL_HUNT_LIST_FIRST_MIN || count < CTRL_HUNT_LIST_COUNT_MIN ||
        count > CTRL_HUNT_LIST_COUNT_MAX)
    {
        return ctrlReplyErrorf(call, "out_of_range", "first must not be negative and count must "
                               "be %d to %d", CTRL_HUNT_LIST_COUNT_MIN, CTRL_HUNT_LIST_COUNT_MAX);
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    describeHuntLayout(&call->text, call->session->hunt);
    describeHuntList(&call->text, call->session->hunt, (UInt32)first, (UInt32)count);
    jsonOutUInt(&call->out, "candidates", ctrlHuntCandidateCount(call->session->hunt));
    return ctrlReplyDone(call);
}

int ctrlOpHuntEnd(CtrlCall* call)
{
    if (call->session->hunt != NULL) {
        ctrlHuntAbandon(call->session->hunt);
        call->session->hunt = NULL;
    }
    ctrlTextAdd(&call->text, "hunt abandoned\n");
    return ctrlReplyDone(call);
}
