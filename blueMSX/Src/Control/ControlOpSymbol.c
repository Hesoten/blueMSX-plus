/*****************************************************************************
**
** Symbol file op handlers.
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
#include "ControlClient.h"
#include "ControlSymbol.h"
#include "ControlProtocol.h"
#include "Emulator.h"
#include <stdio.h>
#include <string.h>

int ctrlOpSymLoad(CtrlCall* call)
{
    const char* path = ctrlCallText(call, "path");
    const char* slotText = ctrlCallText(call, "slot");
    int   replace = ctrlCallBool(call, "replace", 0);
    int   wantSlot = -1;
    int   wantSubSlot = -1;

    /* Binding reads the slot map, and a file loaded unbound is never bound later. */
    if (emulatorGetState() == EMU_STOPPED) {
        return ctrlReplyError(call, "no_machine",
                              "no machine is running, so there are no slots to bind "
                              "symbols to; start the machine, then load the file");
    }
    if (path == NULL) {
        return ctrlReplyError(call, "missing_path",
                              "loading symbols needs the path of a symbol file");
    }
    if (slotText != NULL) {
        CtrlAddr slotAddr;
        char     err[160];
        char     qualified[48];

        snprintf(qualified, sizeof(qualified), "slot%s", slotText);
        if (!ctrlAddrParse(qualified, &slotAddr, err, sizeof(err))) {
            return ctrlReplyError(call, "bad_slot",
                                  "\"slot\" wants a slot, as in \"1.0\"");
        }
        wantSlot    = slotAddr.slot;
        wantSubSlot = slotAddr.subSlot;
    }
    if (!ctrlSymLoad(path, wantSlot, wantSubSlot, replace,
                     call->text.data, call->text.capacity))
    {
        ctrlActivityNoteFile(CTRL_ACT_SYM_LOAD, path, CTRL_ACT_FAILED);
        return ctrlReplyTextAsError(call, "sym_load_failed");
    }
    ctrlActivityNoteFile(CTRL_ACT_SYM_LOAD, path, CTRL_ACT_DONE);
    jsonOutInt(&call->out, "symbols", ctrlSymCount());
    return ctrlReplyDone(call);
}

static void describeSymbol(CtrlText* text, const CtrlSymbolInfo* symbol)
{
    CtrlAddr     addr;
    CtrlAddrInfo info;
    char         printed[64];
    CtrlSymFile  from;
    int          files = 0;
    int          i;

    /* With several files loaded, say which one, as the same name may come from two. */
    for (i = 0; i < ctrlSymFileCount(); i++) {
        files += ctrlSymFileGet(i, &from) && from.path[0] != 0;
    }
    if (files > 1 && ctrlSymFileGet(symbol->fileId, &from)) {
        ctrlTextAdd(text, "%s  (from %s)\n", symbol->name, from.name);
    }
    else {
        ctrlTextAdd(text, "%s\n", symbol->name);
    }

    /* A bank number with no slot: the file is not bound, so where it is is unknown. */
    if (symbol->slot == CTRL_SYM_SLOT_NONE && symbol->segment >= 0) {
        CtrlSymFile file;
        int page = ctrlSymFileGet(symbol->fileId, &file) && ctrlSymFileHasPages(&file);

        ctrlTextAdd(text, "  0x%04X  %s %d, file not bound to a slot: no address "
                    "until it is\n", symbol->z80Address, page ? "page" : "bank",
                    (int)symbol->segment);
        return;
    }
    if (!ctrlSymHasSegmentAddress(symbol)) {
        ctrlTextAdd(text, "  0x%04X  %s  ",
                    symbol->z80Address,
                    symbol->kind == CTRL_SYM_EQU ? "constant" :
                    symbol->kind == CTRL_SYM_DATA ? "label or constant" : "code");
        if (symbol->slot == CTRL_SYM_SLOT_NONE || symbol->kind == CTRL_SYM_EQU) {
            ctrlTextAdd(text, "unqualified\n");
        }
        else {
            ctrlTextAdd(text, "any segment of slot%d.%d, while it is paged in there\n",
                        symbol->slot, symbol->subSlot);
        }
        return;
    }

    /* With no segment size, the address would be taken for the offset. */
    if (!ctrlSymAddress(symbol, &addr)) {
        ctrlTextAdd(text, "  slot%d.%d seg%d  %s   assembled for 0x%04X; %s, so its "
                    "offset in the segment is not known\n",
                    symbol->slot, symbol->subSlot, (int)symbol->segment,
                    symbol->kind == CTRL_SYM_DATA ? "label or constant" : "code",
                    symbol->z80Address,
                    emulatorGetState() == EMU_STOPPED ? "no machine is running"
                                                      : "the slot has no segments now");
        return;
    }

    ctrlAddrPrint(&addr, printed, sizeof(printed));
    ctrlTextAdd(text, "  %-28s %s", printed,
                symbol->kind == CTRL_SYM_DATA ? "label or constant" : "code");

    if (ctrlAddrResolve(&addr, &info) && info.z80Address >= 0) {
        ctrlTextAdd(text, "   z80 0x%04X right now\n", info.z80Address);
    }
    else {
        ctrlTextAdd(text, "   assembled for 0x%04X; not paged in\n",
                    symbol->z80Address);
    }
}

int ctrlOpSymFind(CtrlCall* call)
{
    CtrlSymbolInfo found[CTRL_SYM_BY_NAME_MAX];
    const char* name = ctrlCallText(call, "name");
    int  filled;
    int  total;
    int  i;

    if (name == NULL) {
        return ctrlReplyError(call, "missing_name",
                              "finding a symbol needs a \"name\"");
    }
    filled = ctrlSymByName(name, found, CTRL_SYM_BY_NAME_MAX, 0, &total);
    if (filled == 0) {
        ctrlTextAdd(&call->text, "no symbol called %s (names are case sensitive). Search "
                    "takes a pattern with * and ?, ignoring case.\n", name);
        return ctrlReplyDone(call);
    }
    for (i = 0; i < filled; i++) {
        describeSymbol(&call->text, &found[i]);
    }
    if (total > filled) {
        ctrlTextAdd(&call->text, "  ... and %d more with this name; a symbol search pages through "
                    "them all\n", total - filled);
    }
    jsonOutInt(&call->out, "matches", total);
    return ctrlReplyDone(call);
}

int ctrlOpSymSearch(CtrlCall* call)
{
    CtrlSymbolInfo found[CTRL_SYM_SEARCH_COUNT_MAX];
    const char* pattern = ctrlCallText(call, "pattern");
    Int32 first = ctrlCallInt(call, "first", CTRL_SYM_SEARCH_FIRST_DEFAULT);
    Int32 count = ctrlCallInt(call, "count", CTRL_SYM_SEARCH_COUNT_DEFAULT);
    int   total = 0;
    int   filled;
    int   i;

    if (pattern == NULL) {
        return ctrlReplyError(call, "missing_pattern",
                              "a symbol search needs a \"pattern\"; * and ? work");
    }
    if (first < CTRL_SYM_SEARCH_FIRST_MIN || count < CTRL_SYM_SEARCH_COUNT_MIN ||
        count > CTRL_SYM_SEARCH_COUNT_MAX)
    {
        return ctrlReplyErrorf(call, "out_of_range", "first must not be negative and count must "
                               "be %d to %d", CTRL_SYM_SEARCH_COUNT_MIN, CTRL_SYM_SEARCH_COUNT_MAX);
    }
    filled = ctrlSymSearch(pattern, (int)first, (int)count, found, &total);
    ctrlTextAdd(&call->text, "%d of %d symbols match %s\n", total,
                ctrlSymCount(), pattern);
    for (i = 0; i < filled; i++) {
        describeSymbol(&call->text, &found[i]);
    }
    if (total > first + filled) {
        ctrlTextAdd(&call->text, "  ... %d more, ask again from %d\n",
                    total - (int)first - filled, (int)first + filled);
    }
    jsonOutInt(&call->out, "matches", total);
    return ctrlReplyDone(call);
}

int ctrlOpSymStatus(CtrlCall* call)
{
    int count = ctrlSymFileCount();
    int i;

    /* So a file whose cartridge was swapped out is shown as unbound. */
    ctrlSymRevalidate();

    if (ctrlSymCount() == 0) {
        ctrlTextAdd(&call->text, "no symbols loaded; load a symbol file by its full path\n");
    }
    for (i = 0; i < count; i++) {
        CtrlSymFile file;

        if (!ctrlSymFileGet(i, &file) || file.path[0] == 0) {
            continue;
        }
        ctrlTextAdd(&call->text, "%s  %d symbols", file.name, file.symbolCount);
        if (file.slot != CTRL_SYM_SLOT_NONE) {
            ctrlTextAdd(&call->text, "  slot%d.%d, segment qualified",
                        file.slot, file.subSlot);
        }
        else if (file.boundContents[0] != 0) {
            ctrlTextAdd(&call->text, "  was bound to %s until that slot's contents changed%s; "
                        "load it again", file.boundContents,
                        file.maxBank >= 0 ? ", so its banked symbols are not used" : "");
        }
        else if (file.maxBank >= 0) {
            ctrlTextAdd(&call->text, "  not bound to a slot, so its banked symbols are not "
                        "used; loading it again says why, and which slots could hold it");
        }
        else {
            ctrlTextAdd(&call->text, "  unqualified");
        }
        if (file.maxBank >= 0) {
            ctrlTextAdd(&call->text, "  %s 0x00-0x%02X",
                        ctrlSymFileHasPages(&file) ? "pages" : "banks",
                        (unsigned)file.maxBank);
        }
        ctrlTextAdd(&call->text, "\n  %s\n", file.path);
    }
    jsonOutInt(&call->out, "symbols", ctrlSymCount());
    return ctrlReplyDone(call);
}

int ctrlOpSymClear(CtrlCall* call)
{
    ctrlSymClearAll();
    ctrlTextAdd(&call->text, "all symbols cleared\n");
    return ctrlReplyDone(call);
}
