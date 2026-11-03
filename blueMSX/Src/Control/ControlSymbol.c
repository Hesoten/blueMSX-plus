/*****************************************************************************
**
** Symbol table by slot segment.
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
#include "ControlSymbol.h"
#include "Properties.h"
#include "Debugger.h"
#include "ControlSlots.h"
#include "ControlPath.h"
#include "ControlSymbolParse.h"
#include "SlotManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
/* Routes fopen through pkg_fopen, which opens UTF-8 paths with _wfopen. */
#include "PacketFileSystem.h"

#define SYM_MAX       32768
#define NAMES_MAX     (1536 * 1024)
/* How far back a "nearest symbol" may reach. */
#define SYM_BACK_MAX  256

typedef struct {
    UInt32 name;        /* offset into the name pool */
    UInt16 z80Address;
    Int32  segment;
    UInt8  slot;
    UInt8  subSlot;
    UInt8  kind;
    UInt8  fileId;
} Symbol;

typedef struct {
    Symbol* symbol;
    char*   names;
    Int32*  first;      /* 0x10001 entries: first symbol at each address */
    Int32*  byName;
    int     count;
    UInt32  nameUsed;
} Table;

static Table       table;
static CtrlSymFile files[CTRL_SYM_FILES_MAX];
static int         fileCount;
static UInt32      validatedGeneration;

static const char* nameOf(int index)
{
    return table.names + table.symbol[index].name;
}

/* ---- allocation -------------------------------------------------------- */

static int tableEnsure(void)
{
    if (table.symbol != NULL) {
        return 1;
    }
    table.symbol = malloc(sizeof(Symbol) * SYM_MAX);
    table.names  = malloc(NAMES_MAX);
    table.first  = malloc(sizeof(Int32) * 0x10001);
    table.byName = malloc(sizeof(Int32) * SYM_MAX);

    if (table.symbol == NULL || table.names == NULL ||
        table.first == NULL || table.byName == NULL)
    {
        free(table.symbol); free(table.names);
        free(table.first);  free(table.byName);
        memset(&table, 0, sizeof(table));
        return 0;
    }
    table.count    = 0;
    table.nameUsed = 0;
    return 1;
}

void ctrlSymClearAll(void)
{
    free(table.symbol); free(table.names);
    free(table.first);  free(table.byName);
    memset(&table, 0, sizeof(table));
    memset(files, 0, sizeof(files));
    fileCount = 0;
}

int ctrlSymCount(void)
{
    return table.count;
}

int ctrlSymFileCount(void)
{
    return fileCount;
}

int ctrlSymFileHasPages(const CtrlSymFile* file)
{
    return file->format == CTRL_SYM_FORMAT_SLD && file->boundContents[0] == 0;
}

int ctrlSymHasSegmentAddress(const CtrlSymbolInfo* symbol)
{
    return symbol->slot != CTRL_SYM_SLOT_NONE && symbol->segment != CTRL_SYM_SEG_ANY;
}

int ctrlSymAddress(const CtrlSymbolInfo* symbol, CtrlAddr* addr)
{
    int segmentSize = slotDebugGetSegmentSize(symbol->slot, symbol->subSlot);

    memset(addr, 0, sizeof(CtrlAddr));
    addr->space   = CTRL_SPACE_SLOT;
    addr->slot    = symbol->slot;
    addr->subSlot = symbol->subSlot;
    addr->segment = symbol->segment;
    if (segmentSize <= 0) {
        return 0;
    }
    addr->offset = (UInt32)(symbol->z80Address % (UInt16)segmentSize);
    return 1;
}

int ctrlSymFileGet(int index, CtrlSymFile* out)
{
    if (index < 0 || index >= fileCount) {
        return 0;
    }
    *out = files[index];
    return 1;
}

/* ---- indexing ---------------------------------------------------------- */

static int compareByAddress(const void* a, const void* b)
{
    const Symbol* x = (const Symbol*)a;
    const Symbol* y = (const Symbol*)b;

    if (x->z80Address != y->z80Address) {
        return x->z80Address < y->z80Address ? -1 : 1;
    }
    if (x->slot != y->slot) {
        return x->slot < y->slot ? -1 : 1;
    }
    if (x->subSlot != y->subSlot) {
        return x->subSlot < y->subSlot ? -1 : 1;
    }
    if (x->segment != y->segment) {
        return x->segment < y->segment ? -1 : 1;
    }
    return 0;
}

static int compareByNameIndex(const void* a, const void* b)
{
    Int32 x = *(const Int32*)a;
    Int32 y = *(const Int32*)b;
    int   order = strcmp(nameOf(x), nameOf(y));

    /* qsort is not stable: one name keeps the address table's order. */
    if (order != 0) {
        return order;
    }
    return x < y ? -1 : x > y ? 1 : 0;
}

static void reindex(void)
{
    int i;
    int at = 0;

    if (table.count > 0) {
        qsort(table.symbol, (size_t)table.count, sizeof(Symbol), compareByAddress);
    }

    for (i = 0; i <= 0x10000; i++) {
        while (at < table.count && table.symbol[at].z80Address < i) {
            at++;
        }
        table.first[i] = at;
    }

    for (i = 0; i < table.count; i++) {
        table.byName[i] = i;
    }
    if (table.count > 0) {
        qsort(table.byName, (size_t)table.count, sizeof(Int32), compareByNameIndex);
    }
}

static int addSymbol(Table* into, const char* name, UInt32 address,
                     Int32 bank, int kind, int fileId)
{
    Symbol* symbol;
    size_t  length = strlen(name);

    if (into->count >= SYM_MAX || length == 0 || length >= CTRL_SYM_NAME_MAX) {
        return 0;
    }
    if (into->nameUsed + length + 1 >= NAMES_MAX) {
        return 0;
    }
    symbol = &into->symbol[into->count];

    symbol->name = into->nameUsed;
    memcpy(into->names + into->nameUsed, name, length + 1);
    into->nameUsed += (UInt32)length + 1;

    symbol->z80Address = (UInt16)address;
    symbol->segment    = bank;
    symbol->slot       = CTRL_SYM_SLOT_NONE;
    symbol->subSlot    = 0;
    symbol->kind       = (UInt8)kind;
    symbol->fileId     = (UInt8)fileId;

    into->count++;
    return 1;
}

/* The table a file is read into, and the file its symbols belong to. */
typedef struct {
    Table* into;
    int    fileId;
} ShadowSink;

static int addToShadow(void* ref, const char* name, UInt32 address, Int32 bank, int kind)
{
    ShadowSink* sink = (ShadowSink*)ref;

    return addSymbol(sink->into, name, address, bank, kind, sink->fileId);
}

/* One page throughout is a flat program, and pages without their size cannot become segments. */
static void flattenSinglePageSld(Table* into, CtrlSymParseResult* result)
{
    int i;

    if (result->format == CTRL_SYM_FORMAT_SLD && (!result->multiPage || result->pageSize == 0)) {
        for (i = 0; i < into->count; i++) {
            into->symbol[i].segment = CTRL_SYM_SEG_ANY;
        }
        result->banked  = 0;
        result->maxBank = -1;
    }
}

/* ---- binding to a slot ------------------------------------------------- */

static int slotSegmentCount(int slot, int subSlot)
{
    UInt32 size = 0;
    int    segmentSize;

    if (slotDebugGetStorage(slot, subSlot, NULL, &size, NULL) == SLOT_STORAGE_NONE) {
        return 0;
    }
    segmentSize = slotDebugGetSegmentSize(slot, subSlot);
    if (segmentSize <= 0 || size == 0) {
        return 0;
    }
    return (int)(size / (UInt32)segmentSize);
}

static int fitsSlot(const CtrlSymParseResult* result, int slot, int subSlot, int segments)
{
    Int32 last = ctrlSymPageToSegment(result, result->maxBank, (UInt16)(result->pageSize - 1),
                               slotDebugGetSegmentSize(slot, subSlot));

    return segments > 0 && last != CTRL_SYM_SEG_ANY && last < segments;
}

/* Whether a slot could hold the file's banks; page 3 is always RAM, so a file living
** entirely there is not a ROM's. */
static int mayHold(const CtrlSymParseResult* result, int slot, int subSlot)
{
    if (!fitsSlot(result, slot, subSlot, slotSegmentCount(slot, subSlot))) {
        return 0;
    }
    return result->minAddress < 0xc000 ||
           slotDebugGetStorage(slot, subSlot, NULL, NULL, NULL) != SLOT_STORAGE_ROM;
}

/* The first symbol whose small page sits where its segment never shows it, or NULL; such a
** file would be labelled at the wrong offset, since offsets come from the address. */
static const Symbol* firstMisplacedPageSymbol(int fileId, const CtrlSymParseResult* result, int segmentSize)
{
    UInt32 pages;
    int    i;

    if (result->pageSize == 0 || segmentSize <= 0 || result->pageSize >= (UInt32)segmentSize ||
        (UInt32)segmentSize % result->pageSize != 0)
    {
        return NULL;
    }
    pages = (UInt32)segmentSize / result->pageSize;
    for (i = 0; i < table.count; i++) {
        const Symbol* s = &table.symbol[i];

        if (s->fileId == fileId && s->segment >= 0 &&
            ((UInt32)s->z80Address % (UInt32)segmentSize) / result->pageSize !=
                (UInt32)s->segment % pages)
        {
            return s;
        }
    }
    return NULL;
}

/* Why a slot whose ROM has the file's name cannot take it; 1 when that leaves the file
** unbound, 0 when another slot may still be offered. */
static int explainNameMismatch(const CtrlSymParseResult* result, int slot, int subSlot,
                               const char* contents, CtrlText* report)
{
    /* Say which of mayHold's two tests failed. */
    int ramOnly = result->minAddress >= 0xc000 &&
                  slotDebugGetStorage(slot, subSlot, NULL, NULL, NULL) == SLOT_STORAGE_ROM;

    if (ramOnly) {
        ctrlTextAdd(report, "  %s in slot%d.%d has the same name, but every "
                    "symbol is at 0xC000 or above, which is RAM, "
                    "not this ROM\n", contents, slot, subSlot);
        return 0;
    }
    if (slotDebugGetSegmentSize(slot, subSlot) <= 0) {
        ctrlTextAdd(report, "  not bound to a slot: ");
        ctrlSlotsDescribeNoSegments(report, slot, subSlot);
        ctrlTextAdd(report, "\n");
        return 1;
    }
    ctrlTextAdd(report, "  not bound to a slot: %s in slot%d.%d has the "
                "same name but only %d segments, too few for "
                "this file\n", contents,
                slot, subSlot, slotSegmentCount(slot, subSlot));
    return 1;
}

/* Binds to the slot whose ROM has the file's name: 1 when bound, -1 when such a ROM
** cannot take it, 0 when no ROM has the name. */
static int bindByFileName(CtrlSymFile* info, const CtrlSymParseResult* result, CtrlText* report)
{
    int  slot;
    int  subSlot;
    char stem[64];
    int  named   = 0;
    CtrlTextMark namedAt = ctrlTextMark(report);

    ctrlPathLeafStem(info->path, stem, sizeof(stem));
    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            const char* contents = slotDebugGetContents(slot, subSlot);
            char romStem[64];

            if (contents[0] == 0) {
                continue;
            }
            ctrlPathLeafStem(contents, romStem, sizeof(romStem));
            if (_stricmp(romStem, stem) != 0) {
                continue;
            }
            /* A matching name with too few segments would bind to segments that do not exist. */
            if (!mayHold(result, slot, subSlot)) {
                named |= explainNameMismatch(result, slot, subSlot, contents, report);
                continue;
            }
            info->slot    = (UInt8)slot;
            info->subSlot = (UInt8)subSlot;
            /* It binds after all, so the "not bound" lines for earlier same-named slots go. */
            ctrlTextCut(report, namedAt);
            ctrlTextAdd(report, "  bound to slot%d.%d, matched on the file "
                        "name against %s\n",
                        slot, subSlot, contents);
            return 1;
        }
    }
    return named ? -1 : 0;
}

/* Whether a slot can hold the file with each page where it was assembled. */
static int canTake(int fileId, const CtrlSymParseResult* result, int slot, int subSlot)
{
    return mayHold(result, slot, subSlot) &&
           firstMisplacedPageSymbol(fileId, result, slotDebugGetSegmentSize(slot, subSlot)) == NULL;
}

/* Says why the file is left unbound, listing the slots it could be pinned to. */
static void reportCandidateSlots(int fileId, const CtrlSymParseResult* result, CtrlText* report)
{
    int slot;
    int subSlot;
    int candidates = 0;
    int misplaced  = 0;

    /* Never by segment size alone: a RAM mapper or another cartridge often fits too. */
    for (slot = 0; slot < 4; slot++) {
        for (subSlot = 0; subSlot < 4; subSlot++) {
            if (!mayHold(result, slot, subSlot)) {
                continue;
            }
            if (firstMisplacedPageSymbol(fileId, result, slotDebugGetSegmentSize(slot, subSlot)) != NULL) {
                misplaced++;
            }
            else {
                candidates++;
            }
        }
    }

    if (candidates == 0 && misplaced > 0) {
        ctrlTextAdd(report, "  not bound to a slot: the slots with enough segments have "
                    "larger segments that never show its %u kB pages where they "
                    "were assembled, so check the cartridge's mapper type "
                    "(bmsx_slots)\n", (unsigned)(result->pageSize / 1024));
        return;
    }
    if (candidates == 0) {
        ctrlTextAdd(report, "  not bound to a slot: no slot has enough segments "
                    "for %s 0x%02X\n", result->pageSize != 0 ? "page" : "bank",
                    (unsigned)result->maxBank);
        return;
    }
    ctrlTextAdd(report, "  not bound to a slot. To pin it, load it again with %s as "
                "the slot:\n", candidates == 1 ? "this" : "one of these");
    for (slot = 0; slot < 4 && report->length < report->capacity - 120; slot++) {
        for (subSlot = 0; subSlot < 4 && report->length < report->capacity - 120; subSlot++) {
            if (!canTake(fileId, result, slot, subSlot)) {
                continue;
            }
            ctrlTextAdd(report, "    %d.%d   %s, %d segments\n",
                        slot, subSlot,
                        slotDebugGetContents(slot, subSlot), slotSegmentCount(slot, subSlot));
        }
    }
}

/* Binds by file name or by the slot asked for; otherwise the report lists the slots to pin it to. */
static void bindFile(CtrlSymFile* info, const CtrlSymParseResult* result, int fileId,
                     int wantSlot, int wantSubSlot, CtrlText* report)
{
    if (wantSlot >= 0) {
        info->slot    = (UInt8)wantSlot;
        info->subSlot = (UInt8)(wantSubSlot < 0 ? 0 : wantSubSlot);
        ctrlTextAdd(report, "  bound to slot%d.%d, as asked\n",
                    info->slot, info->subSlot);
        return;
    }

    info->slot    = CTRL_SYM_SLOT_NONE;
    info->subSlot = 0;
    /* Left unbound so its symbols answer in every slot. */
    if (result->banked == 0) {
        ctrlTextAdd(report, "  no bank numbers in the file, so these answer for "
                    "any segment\n");
        return;
    }
    /* Bound by its name, or named after a ROM that cannot take it: another slot would mislabel it. */
    if (bindByFileName(info, result, report) != 0) {
        return;
    }
    reportCandidateSlots(fileId, result, report);
}

/* Unbinds a ROM-bound file's symbols at 0xC000 and up, which are RAM. Returns how many. */
static int unbindPage3Symbols(int fileId, UInt8 slot, UInt8 subSlot)
{
    int unbound = 0;
    int i;

    if (slot == CTRL_SYM_SLOT_NONE) {
        return 0;
    }
    if (slotDebugGetStorage(slot, subSlot, NULL, NULL, NULL) != SLOT_STORAGE_ROM) {
        return 0;
    }

    for (i = 0; i < table.count; i++) {
        if (table.symbol[i].fileId != fileId) {
            continue;
        }
        if (table.symbol[i].z80Address < 0xc000) {
            continue;
        }
        table.symbol[i].slot    = CTRL_SYM_SLOT_NONE;
        table.symbol[i].subSlot = 0;
        table.symbol[i].segment = CTRL_SYM_SEG_ANY;
        unbound++;
    }
    return unbound;
}

/* Removes one file's symbols, and their names too so reloads cannot fill the pool. */
static void dropFile(int fileId)
{
    char*  names;
    UInt32 used = 0;
    int    out = 0;
    int    i;

    if (table.symbol == NULL) {
        return;
    }
    names = malloc(NAMES_MAX);
    for (i = 0; i < table.count; i++) {
        Symbol kept = table.symbol[i];

        if (kept.fileId == fileId) {
            continue;
        }
        if (names != NULL) {
            size_t length = strlen(table.names + kept.name) + 1;
            memcpy(names + used, table.names + kept.name, length);
            kept.name = used;
            used += (UInt32)length;
        }
        table.symbol[out++] = kept;
    }
    table.count = out;
    /* Without memory for a new pool the old names stay, which only wastes room. */
    if (names != NULL) {
        free(table.names);
        table.names    = names;
        table.nameUsed = used;
    }
}

/* One symbol file load in progress: what was read, where it goes, and what it replaces. */
typedef struct {
    CtrlSymFile        info;
    CtrlSymParseResult result;
    int  fileId;
    int  reload;
    int  earlierBanked;
    char earlierPath[CTRL_PATH_MAX];
    int  unloaded;
    int  full;
} Load;

static void freeShadow(Table* shadow)
{
    free(shadow->symbol);
    free(shadow->names);
}

/* The file slot a load goes in: its earlier load's, a free one, or the next; 0 when all
** are taken. */
static int pickFileSlotId(const char* path, int replace, Load* load, CtrlText* out)
{
    char stem[64];
    int  i;

    /* The same name is a reload of the same program, whatever the folder or extension. */
    ctrlPathLeafStem(path, stem, sizeof(stem));
    load->fileId = -1;
    if (replace) {
        load->fileId = 0;
        return 1;
    }
    for (i = 0; i < fileCount && load->fileId < 0; i++) {
        if (files[i].path[0] != 0 && _stricmp(files[i].name, stem) == 0) {
            load->fileId = i;
            load->reload = 1;
            strcpy(load->earlierPath, files[i].path);
            load->earlierBanked = files[i].maxBank >= 0;
        }
    }
    for (i = 0; i < fileCount && load->fileId < 0; i++) {
        if (files[i].path[0] == 0) {
            load->fileId = i;
        }
    }
    if (load->fileId < 0) {
        if (fileCount >= CTRL_SYM_FILES_MAX) {
            ctrlTextAdd(out, "%d symbol files are loaded (the limit); clear them or load "
                        "with replace_all", fileCount);
            return 0;
        }
        load->fileId = fileCount;
    }
    return 1;
}

/* A listing or source still yields a few lines that look like symbols, as "org 4000h". */
static int looksLikeSymbolFile(const CtrlSymParseResult* result)
{
    return result->loaded > 0 && result->skippedUnparsed <= result->loaded;
}

static void reportNotSymbolFile(const char* path, const Load* load, CtrlText* out)
{
    const CtrlSymParseResult* result = &load->result;

    if (result->skippedUnparsed > result->loaded) {
        char counted[96];

        if (result->loaded == 0) {
            _snprintf(counted, sizeof(counted), "none of its lines looked like symbol lines");
        }
        else {
            _snprintf(counted, sizeof(counted),
                      "%d lines did not look like symbol lines and only %d did",
                      result->skippedUnparsed, result->loaded);
        }
        counted[sizeof(counted) - 1] = 0;
        ctrlTextAdd(out, "%s: %s, so nothing was loaded%s; it does not look like a symbol "
                    "file (an assembler listing or source file is the usual cause). Load "
                    "the symbol file the assembler wrote: sjasmplus .sld, asMSX or WLA-DX "
                    ".sym, SDCC .noi or .map, or a file of \"NAME equ 4018h\" or \"4018h "
                    "NAME\" lines; comments start with ;.", path, counted,
                    table.count > 0 ? " and the symbols already loaded stay loaded" : "");
    }
    else {
        ctrlTextAdd(out, "%s: no symbols recognised. Supported: sjasmplus .sld, "
                    "asMSX .sym, WLA-DX .sym, SDCC .noi and .map, and "
                    "\"NAME equ 4018h\" or \"4018h NAME\" lines.",
                    load->info.name);
    }
}

/* Reads the file into shadow, a table of its own, so a failed load leaves the live one
** intact; 0, with shadow freed, when it cannot be read or is no symbol file. */
static int readSymbolFile(const char* path, Load* load, Table* shadow, CtrlText* out)
{
    FILE*    file = fopen(path, "r");
    ShadowSink sink;

    if (file == NULL) {
        ctrlTextAdd(out, "cannot open %s: %s", path, strerror(errno));
        return 0;
    }
    memset(shadow, 0, sizeof(Table));
    shadow->symbol = malloc(sizeof(Symbol) * SYM_MAX);
    shadow->names  = malloc(NAMES_MAX);
    if (shadow->symbol == NULL || shadow->names == NULL) {
        freeShadow(shadow);
        fclose(file);
        ctrlTextAdd(out, "no memory to read the file");
        return 0;
    }

    strncpy(load->info.path, path, sizeof(load->info.path) - 1);
    ctrlPathLeafStem(path, load->info.name, sizeof(load->info.name));
    load->info.fileTime = ctrlFileWriteTime(path);
    load->info.loadedAt = ctrlFileTimeNow();

    /* It returns 0 when the table filled up; what it read before that is kept. */
    sink.into   = shadow;
    sink.fileId = load->fileId;
    load->full = !ctrlSymParseFile(file, addToShadow, &sink, &load->result);
    /* Also for a file cut short, so it is still judged flat or banked. */
    flattenSinglePageSld(shadow, &load->result);
    fclose(file);
    if (!looksLikeSymbolFile(&load->result)) {
        freeShadow(shadow);
        reportNotSymbolFile(path, load, out);
        return 0;
    }

    load->info.format      = (UInt8)load->result.format;
    load->info.symbolCount = load->result.loaded;
    load->info.maxBank     = load->result.maxBank;
    return 1;
}

/* Refuses a slot asked for that cannot take the file; checked before anything is unloaded,
** so a refusal leaves the table as it was. */
static int checkWantedSlot(const CtrlSymParseResult* result, int wantSlot, int wantSubSlot,
                           CtrlText* out)
{
    int sub = wantSubSlot < 0 ? 0 : wantSubSlot;
    int segments = slotSegmentCount(wantSlot, sub);

    /* Not "load it without slot": that would offer some other slot for this ROM's file. */
    if (result->banked > 0 && slotDebugGetSegmentSize(wantSlot, sub) <= 0 &&
        slotDebugGetContents(wantSlot, sub)[0] != 0)
    {
        ctrlSlotsDescribeNoSegments(out, wantSlot, sub);
        ctrlTextAdd(out, ". A file with %s numbers cannot be bound to it.",
                    result->pageSize != 0 ? "page" : "bank");
        return 0;
    }
    if (result->banked > 0 && !fitsSlot(result, wantSlot, sub, segments)) {
        ctrlTextAdd(out, "slot%d.%d (%s) has %d segments, which cannot hold %s 0x%02X; "
                    "a file with bank numbers goes on a megaROM or memory mapper "
                    "slot. Load it without slot to see which fit.",
                    wantSlot, sub, slotDebugGetContents(wantSlot, sub), segments,
                    result->pageSize != 0 ? "page" : "bank", (unsigned)result->maxBank);
        return 0;
    }
    if (slotDebugGetContents(wantSlot, sub)[0] == 0 &&
        slotDebugGetStorage(wantSlot, sub, NULL, NULL, NULL) == SLOT_STORAGE_NONE)
    {
        ctrlTextAdd(out, "nothing is in slot%d.%d", wantSlot, sub);
        return 0;
    }
    return 1;
}

/* Unloads what the file replaces, then appends the shadow to the live table under the
** file's id, as much as fits, and frees the shadow. */
static void commitShadow(Load* load, Table* shadow, int replace)
{
    int i;

    if (replace) {
        for (i = 0; i < fileCount; i++) {
            load->unloaded += files[i].path[0] != 0;
        }
        memset(files, 0, sizeof(files));
        fileCount = 0;
        table.count    = 0;
        table.nameUsed = 0;
    }
    else if (load->reload) {
        /* Before the append: the per-file loops below select by fileId. */
        dropFile(load->fileId);
    }

    for (i = 0; i < shadow->count; i++) {
        const char* name = shadow->names + shadow->symbol[i].name;
        if (!addSymbol(&table, name, shadow->symbol[i].z80Address,
                       shadow->symbol[i].segment, shadow->symbol[i].kind, load->fileId))
        {
            break;
        }
    }
    load->info.symbolCount = i;
    freeShadow(shadow);
}

static void reportLoaded(const Load* load, CtrlText* out)
{
    const CtrlSymFile*        info   = &load->info;
    const CtrlSymParseResult* result = &load->result;

    ctrlTextAdd(out, "%s  %s\n  %d symbols, addresses 0x%04X-0x%04X",
                info->name,
                info->format == CTRL_SYM_FORMAT_ASMSX ? "asMSX, with bank numbers" :
                info->format == CTRL_SYM_FORMAT_WLADX ? "WLA-DX" :
                info->format == CTRL_SYM_FORMAT_NOICE ? "SDCC NoICE" :
                info->format == CTRL_SYM_FORMAT_SDCCMAP ? "SDCC linker map" :
                info->format == CTRL_SYM_FORMAT_SLD ? "sjasmplus SLD" : "plain",
                info->symbolCount,
                (unsigned)result->minAddress, (unsigned)result->maxAddress);
    if (result->banked > 0) {
        ctrlTextAdd(out, ", %s 0x00-0x%02X", result->pageSize != 0 ? "pages" : "banks",
                    (unsigned)result->maxBank);
    }
    ctrlTextAdd(out, "\n");
    if (load->earlierBanked && result->banked == 0) {
        ctrlTextAdd(out, "  this replaced %s, which had bank numbers, with a file that has "
                    "none: its labels now name whatever segment is paged in. If %s was "
                    "the right file, load it again\n", load->earlierPath, load->earlierPath);
    }
}

/* Refuses the slot rather than label a page at the wrong offset; the binding's report
** from bindAt on is replaced by why. */
static void refuseMisplacedPages(Load* load, CtrlTextMark bindAt, CtrlText* out)
{
    CtrlSymFile*              info   = &load->info;
    const CtrlSymParseResult* result = &load->result;
    int           segmentSize;
    const Symbol* s;
    UInt32        pages;

    if (result->pageSize == 0 || info->slot == CTRL_SYM_SLOT_NONE) {
        return;
    }
    segmentSize = slotDebugGetSegmentSize(info->slot, info->subSlot);
    s           = firstMisplacedPageSymbol(load->fileId, result, segmentSize);
    if (s == NULL) {
        return;
    }
    pages = (UInt32)segmentSize / result->pageSize;

    ctrlTextCut(out, bindAt);
    ctrlTextAdd(out, "  not bound to slot%d.%d: page %d is assembled for "
                "0x%04X, but with %d kB segments it starts 0x%04X into "
                "seg%d, so it never runs at that address; the file was "
                "built for %u kB banks, so check the cartridge's "
                "mapper type (bmsx_slots)\n",
                info->slot, info->subSlot, (int)s->segment,
                (unsigned)s->z80Address, segmentSize / 1024,
                (unsigned)(((UInt32)s->segment % pages) * result->pageSize),
                (int)((UInt32)s->segment / pages),
                (unsigned)(result->pageSize / 1024));
    info->slot    = CTRL_SYM_SLOT_NONE;
    info->subSlot = 0;
}

/* An SLD bound to a slot has its device pages turned into that slot's segments. Unbound
** pages stay page numbers with no slot, so they label nothing until bound. */
static void pagesToSegments(Load* load, CtrlText* out)
{
    CtrlSymFile*              info   = &load->info;
    const CtrlSymParseResult* result = &load->result;
    int segmentSize;
    int i;

    if (result->pageSize == 0 || info->slot == CTRL_SYM_SLOT_NONE) {
        return;
    }
    segmentSize = slotDebugGetSegmentSize(info->slot, info->subSlot);
    for (i = 0; i < table.count; i++) {
        if (table.symbol[i].fileId == load->fileId) {
            table.symbol[i].segment = ctrlSymPageToSegment(result, table.symbol[i].segment,
                                                           table.symbol[i].z80Address, segmentSize);
        }
    }
    info->maxBank = ctrlSymPageToSegment(result, result->maxBank,
                                         (UInt16)(result->pageSize - 1), segmentSize);
    /* A RAM mapper is filled at run time, so there is no ROM image to hold the pages. */
    if (result->banked > 0 && segmentSize > 0 && (UInt32)segmentSize != result->pageSize &&
        slotDebugGetStorage(info->slot, info->subSlot, NULL, NULL, NULL) == SLOT_STORAGE_RAM)
    {
        ctrlTextAdd(out, "  %u kB pages on %d kB %s segments: page n is "
                    "read as seg n%c%u\n",
                    (unsigned)(result->pageSize / 1024), segmentSize / 1024,
                    segmentSize == 0x4000 ? "memory mapper" : "RAM",
                    (UInt32)segmentSize > result->pageSize ? '/' : '*',
                    (UInt32)segmentSize > result->pageSize
                        ? (unsigned)((UInt32)segmentSize / result->pageSize)
                        : (unsigned)(result->pageSize / (UInt32)segmentSize));
    }
    else if (result->banked > 0 && segmentSize > 0 && (UInt32)segmentSize != result->pageSize) {
        ctrlTextAdd(out, "  %u kB pages on %d kB segments: read on the assumption that "
                    "the ROM image holds the pages in order\n",
                    (unsigned)(result->pageSize / 1024), segmentSize / 1024);
    }
}

/* Bank numbers on a RAM mapper hold only if the program picks the same segments.
** A memory mapper has 16 kB segments; SCC-I RAM has 8 kB ones and no MSX-DOS 2. */
static void tellRamMapperBanks(const Load* load, CtrlText* out)
{
    const CtrlSymFile* info = &load->info;

    if (load->result.banked > 0 && info->slot != CTRL_SYM_SLOT_NONE &&
        slotDebugGetStorage(info->slot, info->subSlot, NULL, NULL, NULL) == SLOT_STORAGE_RAM &&
        slotDebugGetSegmentSize(info->slot, info->subSlot) == 0x4000)
    {
        ctrlTextAdd(out, "  slot%d.%d is a memory mapper: a program that gets its segments "
                    "from MSX-DOS 2 at run time is usually given other ones, and then "
                    "these labels name the wrong segment\n", info->slot, info->subSlot);
    }
}

/* Stamps the slot on every symbol of the file, unbanked ones included. */
static void stampFileSlot(int fileId, UInt8 slot, UInt8 subSlot)
{
    int i;

    for (i = 0; i < table.count; i++) {
        if (table.symbol[i].fileId == fileId) {
            table.symbol[i].slot    = slot;
            table.symbol[i].subSlot = subSlot;
        }
    }
}

/* What the load left out or put aside, and what it replaced. */
static void reportSkipped(const Load* load, int page3Unbound, CtrlText* out)
{
    const CtrlSymParseResult* result = &load->result;

    if (page3Unbound > 0) {
        ctrlTextAdd(out, "  %d symbols at 0xC000 and above left unqualified: page 3 "
                    "is RAM on every MSX, whichever bank the file named\n",
                    page3Unbound);
    }
    if (result->skippedTooBig > 0) {
        ctrlTextAdd(out, "  %d lines skipped: the value does not fit in 16 bits\n",
                    result->skippedTooBig);
    }
    if (result->skippedUnparsed > 0) {
        ctrlTextAdd(out, "  %d lines skipped: not recognised\n",
                    result->skippedUnparsed);
    }
    if (result->format == CTRL_SYM_FORMAT_SLD && result->outOfOrder && !result->multiPage) {
        ctrlTextAdd(out, "  pages do not follow the windows in order (page %d at 0x%04X, "
                    "page %d at 0x%04X), but no window holds two pages, so it is read "
                    "as a flat program: its labels answer for whatever is paged in. If "
                    "this is a mapper ROM, the file does not say which segment is which\n",
                    (int)result->firstPage, (unsigned)result->firstAddress,
                    (int)result->otherPage, (unsigned)result->otherAddress);
    }
    if (load->full) {
        ctrlTextAdd(out, "  stopped at %d symbols: the symbol table is full; labels after "
                    "that point are missing and NAME+offset may name an earlier one\n",
                    result->loaded);
    }
    if (load->info.symbolCount < result->loaded) {
        ctrlTextAdd(out, "  only %d of %d fitted beside the other files: the symbol table "
                    "is full\n", load->info.symbolCount, result->loaded);
    }
    if (load->reload && !(load->earlierBanked && result->banked == 0)) {
        ctrlTextAdd(out, "  replaced the earlier load of %s\n", load->earlierPath);
    }
    if (load->unloaded > 0) {
        ctrlTextAdd(out, "  unloaded %d earlier file%s\n", load->unloaded,
                    load->unloaded == 1 ? "" : "s");
    }
}

/* The other files still loaded, and a clash between two that both have no slot. */
static void reportOtherFiles(const Load* load, CtrlText* out)
{
    const CtrlSymFile* info = &load->info;
    int others = 0;
    int i;

    for (i = 0; i < fileCount && out->length < out->capacity - 80; i++) {
        if (i == load->fileId || files[i].path[0] == 0) {
            continue;
        }
        ctrlTextAdd(out, "%s%s",
                    others == 0 ? "  also loaded: " : ", ", files[i].name);
        others++;
    }
    if (others > 0) {
        ctrlTextAdd(out, " (replace_all unloads them first)\n");
    }
    /* Two slot-less files both answer in every slot without a file of its own. */
    for (i = 0; i < fileCount && info->slot == CTRL_SYM_SLOT_NONE && info->maxBank < 0; i++) {
        if (i != load->fileId && files[i].path[0] != 0 && files[i].slot == CTRL_SYM_SLOT_NONE &&
            files[i].maxBank < 0)
        {
            ctrlTextAdd(out, "  %s and %s both have no slot; where both name one address "
                        "only one name is shown. Bind one with slot (BIOS symbols "
                        "to 0.0)\n", info->name, files[i].name);
            break;
        }
    }
}

static void registerFile(Load* load)
{
    CtrlSymFile* info = &load->info;

    info->machineGeneration = dbgMachineGeneration();
    if (info->slot != CTRL_SYM_SLOT_NONE) {
        strncpy(info->boundContents, slotDebugGetContents(info->slot, info->subSlot),
                CTRL_CONTENTS_MAX - 1);
    }

    files[load->fileId] = *info;
    if (load->fileId >= fileCount) {
        fileCount = load->fileId + 1;
    }
    validatedGeneration = dbgMachineGeneration();

    reindex();
}

int ctrlSymLoad(const char* path, int wantSlot, int wantSubSlot, int replace,
                char* report, int reportLen)
{
    Load     load;
    Table    shadow;
    CtrlText out;
    CtrlTextMark bindAt;

    ctrlTextInit(&out, report, reportLen);
    memset(&load, 0, sizeof(load));

    if (!ctrlPathIsFull(path)) {
        ctrlTextAdd(&out, "%s is not a full Windows path (C:\\... or \\\\server\\...); it would "
                    "be read from the emulator's folder. Give the full path.", path);
        return 0;
    }
    if (!tableEnsure()) {
        ctrlTextAdd(&out, "no memory for a symbol table");
        return 0;
    }
    if (!pickFileSlotId(path, replace, &load, &out) ||
        !readSymbolFile(path, &load, &shadow, &out))
    {
        return 0;
    }
    if (wantSlot >= 0 && !checkWantedSlot(&load.result, wantSlot, wantSubSlot, &out)) {
        freeShadow(&shadow);
        return 0;
    }
    commitShadow(&load, &shadow, replace);
    reportLoaded(&load, &out);

    bindAt = ctrlTextMark(&out);
    bindFile(&load.info, &load.result, load.fileId, wantSlot, wantSubSlot, &out);
    refuseMisplacedPages(&load, bindAt, &out);
    pagesToSegments(&load, &out);
    tellRamMapperBanks(&load, &out);
    stampFileSlot(load.fileId, load.info.slot, load.info.subSlot);
    reportSkipped(&load, unbindPage3Symbols(load.fileId, load.info.slot, load.info.subSlot),
                  &out);
    reportOtherFiles(&load, &out);
    registerFile(&load);
    return 1;
}

/* ---- revalidation ------------------------------------------------------ */

void ctrlSymRevalidate(void)
{
    int fileId;
    int changed = 0;

    if (table.symbol == NULL || validatedGeneration == dbgMachineGeneration()) {
        return;
    }
    validatedGeneration = dbgMachineGeneration();

    for (fileId = 0; fileId < fileCount; fileId++) {
        CtrlSymFile* info = &files[fileId];
        int i;

        if (info->slot == CTRL_SYM_SLOT_NONE || info->path[0] == 0) {
            continue;
        }
        /* Compare contents, not generation, so symbols survive a reset. */
        if (strcmp(info->boundContents,
                   slotDebugGetContents(info->slot, info->subSlot)) == 0)
        {
            continue;
        }

        /* Banked symbols keep their segment, so with no slot they match nothing. */
        for (i = 0; i < table.count; i++) {
            if (table.symbol[i].fileId == fileId) {
                table.symbol[i].slot    = CTRL_SYM_SLOT_NONE;
                table.symbol[i].subSlot = 0;
            }
        }
        info->slot = CTRL_SYM_SLOT_NONE;
        changed = 1;
    }
    if (changed) {
        reindex();
    }
}

/* ---- lookup ------------------------------------------------------------ */

static int slotHasFile(int slot, int subSlot)
{
    int i;

    for (i = 0; i < fileCount && slot >= 0; i++) {
        if (files[i].path[0] != 0 && files[i].slot == (UInt8)slot &&
            files[i].subSlot == (UInt8)subSlot)
        {
            return 1;
        }
    }
    return 0;
}

/* 1 is best. 0 means no. */
static int rungOf(const Symbol* symbol, const CtrlSymHome* home, UInt16 at)
{
    int liveSlot;
    int liveSubSlot;
    Int32 liveSegment;

    if (symbol->kind == CTRL_SYM_EQU) {
        /* A constant never labels an address. */
        return 0;
    }

    if (home != NULL && home->segment != CTRL_SYM_SEG_ANY &&
        symbol->segment == home->segment &&
        symbol->slot == home->slot && symbol->subSlot == home->subSlot)
    {
        return 1;
    }

    liveSegment = ctrlSlotsPagedIn(at, &liveSlot, &liveSubSlot);
    if (symbol->slot == (UInt8)liveSlot &&
        symbol->subSlot == (UInt8)liveSubSlot &&
        symbol->segment == liveSegment && liveSegment >= 0)
    {
        return 2;
    }

    /* An unbanked label of a bound file holds only in its own slot. */
    if (symbol->segment == CTRL_SYM_SEG_ANY) {
        if ((symbol->slot == (UInt8)liveSlot &&
             symbol->subSlot == (UInt8)liveSubSlot) ||
            (home != NULL && symbol->slot == home->slot &&
             symbol->subSlot == home->subSlot))
        {
            return 3;
        }
        /* Not where the slot has its own file, as BIOS names on a cartridge; page 3 is shared. */
        if (symbol->slot == CTRL_SYM_SLOT_NONE) {
            if (at < 0xc000 && files[symbol->fileId].slot == CTRL_SYM_SLOT_NONE &&
                slotHasFile(liveSlot, liveSubSlot))
            {
                return 0;
            }
            return 4;
        }
    }
    return 0;
}

static int bestInBucket(const CtrlSymHome* home, UInt16 at, int isAddress,
                        int* alsoIndex)
{
    int from = table.first[at];
    int to   = table.first[at + 1];
    int best = -1;
    int bestRung = 99;
    int second = -1;
    int i;

    if (alsoIndex != NULL) {
        *alsoIndex = -1;
    }

    for (i = from; i < to; i++) {
        int rung = rungOf(&table.symbol[i], home, at);

        if (rung == 0) {
            continue;
        }
        /* An address operand prefers a code label over data at the same place. */
        if (isAddress && table.symbol[i].kind == CTRL_SYM_DATA && to - from > 1) {
            rung += 10;
        }
        if (rung < bestRung ||
            (rung == bestRung && best >= 0 &&
             table.symbol[i].fileId >= table.symbol[best].fileId))
        {
            second   = best;
            best     = i;
            bestRung = rung;
        }
        else if (second < 0) {
            second = i;
        }
    }

    if (alsoIndex != NULL) {
        *alsoIndex = second;
    }
    return best;
}

/* Another bound segment's label at the same address, as two banks' entries at 0x8000. */
static int rivalSegment(int best, UInt16 at)
{
    const Symbol* mine = &table.symbol[best];
    int i;

    for (i = table.first[at]; i < table.first[at + 1]; i++) {
        const Symbol* other = &table.symbol[i];

        if (i == best || other->kind == CTRL_SYM_EQU || other->segment < 0 ||
            other->slot == CTRL_SYM_SLOT_NONE)
        {
            continue;
        }
        if (other->slot != mine->slot || other->subSlot != mine->subSlot ||
            other->segment != mine->segment)
        {
            return 1;
        }
    }
    return 0;
}

const char* ctrlSymFind(const CtrlSymHome* home, UInt16 at, int isAddress,
                        const char** alsoOut, Int32* alsoSegment)
{
    int also = -1;
    int best;

    if (alsoOut != NULL) {
        *alsoOut = NULL;
    }
    if (table.symbol == NULL || table.count == 0) {
        return NULL;
    }
    ctrlSymRevalidate();

    best = bestInBucket(home, at, isAddress, &also);
    if (best < 0) {
        return NULL;
    }
    /* What is paged in now does not say where a listed call lands if another segment has it. */
    if (rungOf(&table.symbol[best], home, at) == 2 && rivalSegment(best, at)) {
        return NULL;
    }
    if (also >= 0 && alsoOut != NULL) {
        *alsoOut = nameOf(also);
        if (alsoSegment != NULL) {
            *alsoSegment = table.symbol[also].segment;
        }
    }
    return nameOf(best);
}

const char* ctrlSymInSegment(UInt8 slot, UInt8 subSlot, Int32 segment,
                             UInt32 offset)
{
    int segmentSize;
    UInt32 window;

    if (table.symbol == NULL || table.count == 0 || segment < 0) {
        return NULL;
    }
    ctrlSymRevalidate();

    segmentSize = slotDebugGetSegmentSize(slot, subSlot);
    if (segmentSize <= 0 || offset >= (UInt32)segmentSize) {
        return NULL;
    }

    for (window = 0; window * (UInt32)segmentSize < 0x10000; window++) {
        UInt32 candidate = offset + window * (UInt32)segmentSize;
        int from;
        int to;
        int i;

        if (candidate > 0xffff) {
            break;
        }
        from = table.first[candidate];
        to   = table.first[candidate + 1];

        for (i = from; i < to; i++) {
            if (table.symbol[i].kind == CTRL_SYM_EQU) {
                continue;
            }
            if (table.symbol[i].segment == segment &&
                table.symbol[i].slot == slot &&
                table.symbol[i].subSlot == subSlot)
            {
                return nameOf(i);
            }
        }
    }
    return NULL;
}

/* The nearest symbol at or before `at` whose kind is one of `wantKind`, or -1.
** `backOut` receives how far back it was found. */
static int nearestOfKind(const CtrlSymHome* home, UInt16 at, int wantKind,
                         int* backOut)
{
    int back;

    for (back = 0; back <= SYM_BACK_MAX; back++) {
        int address = (int)at - back;
        int best;

        if (address < 0) {
            return -1;
        }
        if (table.first[address] == table.first[address + 1]) {
            continue;
        }
        best = bestInBucket(home, (UInt16)address, 0, NULL);
        if (best < 0 || table.symbol[best].kind != wantKind) {
            continue;
        }
        *backOut = back;
        return best;
    }
    return -1;
}

int ctrlSymNearest(const CtrlSymHome* home, UInt16 at, CtrlSymNear* out)
{
    int back = 0;
    int best;

    memset(out, 0, sizeof(CtrlSymNear));
    if (table.symbol == NULL || table.count == 0) {
        return 0;
    }
    ctrlSymRevalidate();

    /* Any code label in range beats a nearer data label. */
    best = nearestOfKind(home, at, CTRL_SYM_CODE, &back);
    if (best < 0) {
        best = nearestOfKind(home, at, CTRL_SYM_DATA, &back);
    }
    if (best < 0) {
        return 0;
    }

    out->name    = nameOf(best);
    out->at      = table.symbol[best].z80Address;
    out->offset  = (UInt16)back;
    out->segment = table.symbol[best].segment;
    out->slot    = table.symbol[best].slot;
    out->subSlot = table.symbol[best].subSlot;
    return 1;
}

static void fill(CtrlSymbolInfo* out, int index)
{
    out->name       = nameOf(index);
    out->z80Address = table.symbol[index].z80Address;
    out->segment    = table.symbol[index].segment;
    out->slot       = table.symbol[index].slot;
    out->subSlot    = table.symbol[index].subSlot;
    out->kind       = table.symbol[index].kind;
    out->fileId     = table.symbol[index].fileId;
}

/* Same address, slot, subslot and segment: the address order puts such symbols side by side. */
static int sameDefinition(Int32 a, Int32 b)
{
    const Symbol* x = &table.symbol[a];
    const Symbol* y = &table.symbol[b];

    return x->z80Address == y->z80Address && x->slot == y->slot &&
           x->subSlot == y->subSlot && x->segment == y->segment;
}

int ctrlSymByName(const char* name, CtrlSymbolInfo* out, int max, int distinct,
                  int* totalOut)
{
    int low = 0;
    int high = table.count - 1;
    int found = 0;
    int i;

    *totalOut = 0;
    if (table.symbol == NULL || table.count == 0) {
        return 0;
    }
    ctrlSymRevalidate();

    while (low <= high) {
        int middle = (low + high) / 2;
        int order = strcmp(nameOf(table.byName[middle]), name);

        if (order < 0) {
            low = middle + 1;
        }
        else if (order > 0) {
            high = middle - 1;
        }
        else {
            /* Walk back to the first of several with this name. */
            while (middle > 0 &&
                   strcmp(nameOf(table.byName[middle - 1]), name) == 0)
            {
                middle--;
            }
            for (i = middle; i < table.count; i++) {
                if (strcmp(nameOf(table.byName[i]), name) != 0) {
                    break;
                }
                if (distinct && i > middle && sameDefinition(table.byName[i - 1], table.byName[i])) {
                    continue;
                }
                if (found < max) {
                    fill(&out[found++], table.byName[i]);
                }
                (*totalOut)++;
            }
            return found;
        }
    }
    return 0;
}

/* * and ? only, case insensitive. */
static int matches(const char* text, const char* pattern)
{
    while (*pattern != 0) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == 0) {
                return 1;
            }
            while (*text != 0) {
                if (matches(text, pattern)) {
                    return 1;
                }
                text++;
            }
            return 0;
        }
        if (*text == 0) {
            return 0;
        }
        if (*pattern != '?' &&
            tolower((unsigned char)*pattern) != tolower((unsigned char)*text))
        {
            return 0;
        }
        pattern++;
        text++;
    }
    return *text == 0;
}

int ctrlSymSearch(const char* pattern, int first, int max,
                  CtrlSymbolInfo* out, int* totalOut)
{
    int seen = 0;
    int filled = 0;
    int i;

    if (totalOut != NULL) {
        *totalOut = 0;
    }
    if (table.symbol == NULL || table.count == 0) {
        return 0;
    }
    ctrlSymRevalidate();

    for (i = 0; i < table.count; i++) {
        if (!matches(nameOf(table.byName[i]), pattern)) {
            continue;
        }
        if (totalOut != NULL) {
            (*totalOut)++;
        }
        if (seen++ < first || filled >= max) {
            continue;
        }
        fill(&out[filled++], table.byName[i]);
    }
    return filled;
}

/* Symbol files changed since they were loaded, or older than a ROM the build rewrote. */
void ctrlSymDescribeStale(CtrlText* text)
{
    int i;

    for (i = 0; i < ctrlSymFileCount(); i++) {
        CtrlSymFile file;
        const char* leaf;
        Int64 now;
        int   cart;

        if (!ctrlSymFileGet(i, &file) || file.path[0] == 0) {
            continue;
        }
        leaf = ctrlPathLeaf(file.path);
        now  = ctrlFileWriteTime(file.path);
        if (now != file.fileTime) {
            ctrlTextAdd(text, "symbols: %s has %s since it was loaded, so the labels "
                        "are from the earlier build. Load it again (bmsx_symbols "
                        "op=load with the same path replaces it).\n",
                        leaf, now == 0 ? "gone from disk" : "changed on disk");
            continue;
        }
        for (cart = 0; cart < PROP_MAX_CARTS; cart++) {
            const char* rom = propGetGlobalProperties()->media.carts[cart].fileName;

            if (rom[0] != 0 && ctrlFileWriteTime(rom) > file.loadedAt) {
                const char* romLeaf = ctrlPathLeaf(rom);

                ctrlTextAdd(text, "symbols: %s was written after %s was loaded, and "
                            "%s was not. If your build writes both, it did not write "
                            "%s this time, and the labels may not match the code.\n",
                            romLeaf, leaf, leaf, leaf);
                break;
            }
        }
    }
}
