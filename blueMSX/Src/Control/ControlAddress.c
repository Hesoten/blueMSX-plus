/*****************************************************************************
**
** MSX address parsing and resolution.
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
#include "ControlAddress.h"
#include "ControlSlots.h"
#include "ControlText.h"
#include "ControlCpu.h"
#include "ControlProtocol.h"
#include "SlotManager.h"
#include "Board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BANK_SIZE 0x2000

static void fail(char* err, int errLen, const char* text)
{
    if (err != NULL && errLen > 0) {
        strncpy(err, text, errLen - 1);
        err[errLen - 1] = 0;
    }
}

/* Case-insensitive prefix match; strncasecmp is not available on every compiler. */
static int prefixIs(const char* s, const char* prefix)
{
    while (*prefix != 0) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

/* Left in *value when readNumber refuses a number for its size. */
#define TOO_LARGE 0xFFFFFFFF

/* 0x1A30, $1A30, #1A30, 1A30h, 6704; unmarked digits are decimal. Returns the
** first unused character, or NULL when there is no number. */
static const char* readNumber(const char* s, UInt32* value)
{
    const char* start;
    const char* scan;
    UInt32 result = 0;
    int    radix  = 10;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        radix = 16;
        s += 2;
    }
    else if (s[0] == '$' || s[0] == '#') {
        radix = 16;
        s += 1;
    }

    start = s;
    scan  = s;
    while (isxdigit((unsigned char)*scan)) {
        scan++;
    }
    if (scan == start) {
        return NULL;
    }
    if (radix == 10 && (*scan == 'h' || *scan == 'H')) {
        radix = 16;
    }

    if (radix == 16) {
        while (start < scan) {
            int digit = ctrlTextHexDigit((unsigned char)*start);
            /* Refused rather than wrapped; nothing addressable is near 2^31. */
            if (result > 0x07FFFFFF) {
                *value = TOO_LARGE;
                return NULL;
            }
            result = result * 16 + (UInt32)digit;
            start++;
        }
        if (*scan == 'h' || *scan == 'H') {
            scan++;
        }
    }
    else {
        scan = start;
        while (isdigit((unsigned char)*scan)) {
            if (result > 0x07FFFFFF) {
                *value = TOO_LARGE;
                return NULL;
            }
            result = result * 10 + (UInt32)(*scan - '0');
            scan++;
        }
        if (scan == start) {
            return NULL;
        }
    }

    *value = result;
    return scan;
}

static const char* skipSpace(const char* s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/* "+0x100" or "-16" or nothing; *ok is -1 when the number is too large. */
static const char* readSigned(const char* s, Int32* delta, int* ok)
{
    UInt32 magnitude = 0;
    int negative;

    *delta = 0;
    *ok = 1;

    s = skipSpace(s);
    if (*s != '+' && *s != '-') {
        return s;
    }
    negative = *s == '-';
    s = skipSpace(s + 1);

    s = readNumber(s, &magnitude);
    if (s == NULL) {
        *ok = magnitude == TOO_LARGE ? -1 : 0;
        return NULL;
    }
    *delta = negative ? -(Int32)magnitude : (Int32)magnitude;
    return s;
}

static int isMemoryMapper(int slot, int subSlot)
{
    /* 16 kB segments: SCC-I RAM in a cartridge slot is RAM in 8 kB ones. */
    return slotDebugGetStorage(slot, subSlot, NULL, NULL, NULL) == SLOT_STORAGE_RAM &&
           slotDebugGetSegmentSize(slot, subSlot) == 0x4000;
}

/* Appends ", slotX.Y (contents)" for every memory mapper; returns how many there are. */
static int appendMemoryMappers(char* message, int size)
{
    int count = 0;
    int s;
    int ss;

    for (s = 0; s < 4; s++) {
        for (ss = 0; ss < 4; ss++) {
            int at = (int)strlen(message);

            if (!isMemoryMapper(s, ss) || at > size - 80) {
                continue;
            }
            snprintf(message + at, (size_t)(size - at), "%sslot%d.%d (%.40s)",
                     count > 0 ? ", " : "", s, ss, slotDebugGetContents(s, ss));
            count++;
        }
    }
    return count;
}

/* The slot "ram:" names, and why: the memory mapper in page 3 (0xC000-0xFFFF), else the
** memory mapper the machine was built with as its main RAM. page3Slot and page3SubSlot get
** the slot page 3 showed, which the answer rests on. */
static int ctrlAddrRamSlot(int* slot, int* subSlot, int* page3Slot, int* page3SubSlot)
{
    int s;
    int ss;
    int why = CTRL_RAM_PAGE3;

    ctrlSlotsPageSlot(0xC000, &s, &ss);
    *page3Slot    = s;
    *page3SubSlot = ss;
    if (!isMemoryMapper(s, ss)) {
        why = CTRL_RAM_MAIN;
        if (!slotDebugGetMainRam(&s, &ss) || !isMemoryMapper(s, ss)) {
            return CTRL_RAM_NONE;
        }
    }
    if (slot    != NULL) { *slot    = s;  }
    if (subSlot != NULL) { *subSlot = ss; }
    return why;
}

/* vram:0x1B00 or cpu:0xC000; returns what follows, or NULL. */
static const char* parseSpacePrefix(const char* s, CtrlAddr* addr, char* err, int errLen)
{
    UInt32 value;

    if (prefixIs(s, "vram:")) {
        s = readNumber(skipSpace(s + 5), &value);
        if (s == NULL) {
            fail(err, errLen, "expected an offset after vram:, as in vram:0x1B00");
            return NULL;
        }
        addr->space  = CTRL_SPACE_VRAM;
        addr->offset = value;
        return s;
    }
    s = readNumber(skipSpace(s + 4), &value);
    if (s == NULL) {
        fail(err, errLen, "expected an address after cpu:, as in cpu:0xC000");
        return NULL;
    }
    addr->space  = CTRL_SPACE_CPU;
    addr->offset = value;
    return s;
}

/* ram:seg12, the memory mapper ctrlAddrRamSlot picks; returns what follows, or NULL. */
static const char* parseRamForm(const char* s, CtrlAddr* addr, char* err, int errLen)
{
    UInt32 value;
    int slot;
    int subSlot;
    int page3Slot;
    int page3SubSlot;
    int why = ctrlAddrRamSlot(&slot, &subSlot, &page3Slot, &page3SubSlot);

    if (why == CTRL_RAM_NONE) {
        char message[CTRL_ADDR_ERR_MAX];

        snprintf(message, sizeof(message),
                 "ram: names nothing here: page 3 shows slot%d.%d, which is not a memory "
                 "mapper, nor is the main RAM the machine was built with. Address a memory "
                 "mapper by its slot, as in slotX.Y:seg12+0x1A30; this machine has ",
                 page3Slot, page3SubSlot);
        if (appendMemoryMappers(message, (int)sizeof(message)) == 0) {
            fail(err, errLen, "this machine has no memory mapper, so ram: names nothing; its "
                 "RAM is not banked, so use a Z80 address (0xC000) or slotX.Y+0xC000");
            return NULL;
        }
        fail(err, errLen, message);
        return NULL;
    }
    addr->space           = CTRL_SPACE_SLOT;
    addr->slot            = (UInt8)slot;
    addr->subSlot         = (UInt8)subSlot;
    addr->ramWhy          = (UInt8)why;
    addr->ramPage3Slot    = (UInt8)page3Slot;
    addr->ramPage3SubSlot = (UInt8)page3SubSlot;
    s = skipSpace(s + 4);
    if (!prefixIs(s, "seg")) {
        fail(err, errLen, "expected a segment after ram:, as in ram:seg12+0x1A30");
        return NULL;
    }
    s = readNumber(s + 3, &value);
    if (s == NULL) {
        fail(err, errLen, "expected a segment number after ram:seg");
        return NULL;
    }
    addr->segment = (Int32)value;
    return s;
}

/* slot1.0 or slot1.0:seg5; returns what follows, or NULL. */
static const char* parseSlotForm(const char* s, CtrlAddr* addr, char* err, int errLen)
{
    UInt32 slot;
    UInt32 subSlot;
    UInt32 value;

    s = readNumber(skipSpace(s + 4), &slot);
    if (s == NULL || slot > 3) {
        fail(err, errLen, "expected a slot 0..3, as in slot1.0:seg5+0x0100");
        return NULL;
    }
    subSlot = 0;
    if (*s == '.') {
        s = readNumber(s + 1, &subSlot);
        if (s == NULL || subSlot > 3) {
            fail(err, errLen, "expected a subslot 0..3 after the dot");
            return NULL;
        }
    }
    addr->space   = CTRL_SPACE_SLOT;
    addr->slot    = (UInt8)slot;
    addr->subSlot = (UInt8)subSlot;

    if (*s == ':') {
        s = skipSpace(s + 1);
        if (!prefixIs(s, "seg")) {
            fail(err, errLen, "expected seg<n> after the colon, as in slot1.0:seg5+0x0100");
            return NULL;
        }
        s = readNumber(s + 3, &value);
        if (s == NULL) {
            fail(err, errLen, "expected a segment number after seg");
            return NULL;
        }
        addr->segment = (Int32)value;
    }
    return s;
}

/* A plain Z80 address; returns what follows, or NULL. */
static const char* parseNumberForm(const char* s, CtrlAddr* addr, char* err, int errLen)
{
    UInt32 value;

    s = readNumber(s, &value);
    if (s == NULL) {
        fail(err, errLen,
             "expected 0xC000, slot1.0:seg5+0x0100, ram:seg12+0x1A30 or vram:0x1B00");
        return NULL;
    }
    addr->space  = CTRL_SPACE_CPU;
    addr->offset = value;
    return s;
}

/* The +0x100 or -16 after any form, then nothing more. */
static int parseOffsetAndEnd(const char* s, CtrlAddr* addr, char* err, int errLen)
{
    Int32 delta;
    int   ok;

    s = readSigned(s, &delta, &ok);
    if (ok <= 0) {
        fail(err, errLen, ok < 0 ? "the number after the sign is too large"
                                 : "expected a number after the sign");
        return 0;
    }
    if (delta < 0 && (UInt32)(-delta) > addr->offset) {
        fail(err, errLen, "that offset is before the start of the space");
        return 0;
    }
    addr->offset = (UInt32)((Int32)addr->offset + delta);

    s = skipSpace(s);
    if (*s != 0) {
        if (isxdigit((unsigned char)*s)) {
            fail(err, errLen,
                 "a bare number is decimal, so say 0x1A30 or 1A30h for hexadecimal");
        }
        else {
            fail(err, errLen, "there is trailing text after the address");
        }
        return 0;
    }

    if (addr->space == CTRL_SPACE_CPU && addr->offset > 0xffff) {
        fail(err, errLen, "the Z80 cannot reach past 0xFFFF");
        return 0;
    }
    return 1;
}

int ctrlAddrSubstituteRegister(char* text, int textLen, char* err, int errLen)
{
    static const char* const names[] = {
        "pc", "sp", "hl", "de", "bc", "af", "ix", "iy", NULL
    };
    char  wanted[4];
    Int32 value;
    int   nameLength = 0;
    int   i;

    while (nameLength < 3 && isalpha((unsigned char)text[nameLength])) {
        wanted[nameLength] = (char)tolower((unsigned char)text[nameLength]);
        nameLength++;
    }
    wanted[nameLength] = 0;
    if (nameLength != 2) {
        return 1;
    }
    for (i = 0; names[i] != NULL; i++) {
        if (strcmp(names[i], wanted) == 0) {
            break;
        }
    }
    if (names[i] == NULL) {
        return 1;
    }
    /* A register name alone or with an offset, not the start of a longer word. */
    if (text[nameLength] != 0 && text[nameLength] != '+' &&
        text[nameLength] != '-' && text[nameLength] != ' ')
    {
        return 1;
    }

    value = ctrlCpuRegister(wanted);

    if (value < 0) {
        return 0;
    }

    {
        char rewritten[CTRL_AT_MAX + 1];
        int  length = snprintf(rewritten, sizeof(rewritten), "0x%04X%s",
                               (unsigned)value, text + nameLength);

        /* "0x1234" is longer than "hl", so a long form may no longer fit. */
        if (length < 0 || length >= (int)sizeof(rewritten) || length >= textLen) {
            snprintf(err, (size_t)errLen, "that address is too long to read");
            return -1;
        }
        strcpy(text, rewritten);
    }
    return 1;
}

int ctrlAddrParse(const char* text, CtrlAddr* addr, char* err, int errLen)
{
    const char* s;

    memset(addr, 0, sizeof(CtrlAddr));
    addr->segment = CTRL_SEGMENT_FLAT;

    if (text == NULL) {
        fail(err, errLen, "an address is required");
        return 0;
    }
    s = skipSpace(text);

    if (prefixIs(s, "vram:") || prefixIs(s, "cpu:")) {
        s = parseSpacePrefix(s, addr, err, errLen);
    }
    else if (prefixIs(s, "ram:")) {
        s = parseRamForm(s, addr, err, errLen);
    }
    else if (isdigit((unsigned char)*s) &&
             (strchr(s, ':') != NULL) && (strchr(s, '.') != NULL) &&
             strchr(s, '.') < strchr(s, ':'))
    {
        /* 1.0:seg5+0x100 -- the short form of the slot address. */
        char rewritten[64];
        if (snprintf(rewritten, sizeof(rewritten), "slot%s", s) >= (int)sizeof(rewritten)) {
            fail(err, errLen, "that address is too long to read");
            return 0;
        }
        return ctrlAddrParse(rewritten, addr, err, errLen);
    }
    else if (prefixIs(s, "slot")) {
        s = parseSlotForm(s, addr, err, errLen);
    }
    else {
        s = parseNumberForm(s, addr, err, errLen);
    }
    return s != NULL && parseOffsetAndEnd(s, addr, err, errLen);
}

int ctrlAddrPrintSegment(char* text, int len, int slotWord, int slot, int subSlot,
                         int segment, UInt32 offset)
{
    return snprintf(text, (size_t)len, "%s%d.%d:seg%d+0x%04X", slotWord ? "slot" : "", slot,
                    subSlot, segment, (unsigned)offset);
}

void ctrlAddrPrint(const CtrlAddr* addr, char* text, int len)
{
    if (len <= 0) {
        return;
    }
    switch (addr->space) {
    case CTRL_SPACE_VRAM:
        snprintf(text, (size_t)len, "vram:0x%04X", addr->offset);
        break;
    case CTRL_SPACE_SLOT:
        if (addr->segment >= 0) {
            ctrlAddrPrintSegment(text, len, 1, addr->slot, addr->subSlot, addr->segment,
                                 addr->offset);
        }
        else {
            snprintf(text, (size_t)len, "slot%d.%d+0x%04X",
                     addr->slot, addr->subSlot, addr->offset);
        }
        break;
    case CTRL_SPACE_CPU:
        snprintf(text, (size_t)len, "cpu:0x%04X", addr->offset);
        break;
    default:
        snprintf(text, (size_t)len, "?");
        break;
    }
}

/* Folds an offset past the end of its segment into the segment number. */
static void ctrlAddrNormalize(CtrlAddr* addr)
{
    int    segmentSize;
    UInt32 carried;

    if (addr->space != CTRL_SPACE_SLOT || addr->segment < 0) {
        return;
    }
    segmentSize = slotDebugGetSegmentSize(addr->slot, addr->subSlot);
    if (segmentSize <= 0) {
        return;
    }
    /* Saturates rather than wraps negative, so a huge offset stays past the end. */
    carried = (UInt32)addr->segment + addr->offset / (UInt32)segmentSize;
    addr->segment = carried > 0x7fffffff ? 0x7fffffff : (Int32)carried;
    addr->offset %= (UInt32)segmentSize;
}

/* The 8kB bank through which the Z80 sees this segment, or -1 when paged out. */
static int findVisibleBank(const CtrlAddr* addr, int segmentSize)
{
    int banksPerSegment = segmentSize / BANK_SIZE;
    int bank;

    if (banksPerSegment < 1) {
        banksPerSegment = 1;
    }

    for (bank = 0; bank < 8; bank += banksPerSegment) {
        int slot;
        int subSlot;

        if (slotDebugGetBankSegment(addr->slot, addr->subSlot, bank) != addr->segment) {
            continue;
        }
        ctrlSlotsPageSlot((UInt16)(bank * BANK_SIZE), &slot, &subSlot);
        if (slot != (int)addr->slot || subSlot != (int)addr->subSlot) {
            continue;
        }
        return bank;
    }
    return -1;
}

/* The slot address a Z80 address shows now: a segment one, or flat where none is reported.
** Returns 0 past 0xFFFF. */
static int resolveCpu(const CtrlAddr* addr, CtrlAddr* work)
{
    int slot;
    int subSlot;
    int segmentSize;

    if (addr->offset > 0xffff) {
        return 0;
    }
    work->space   = CTRL_SPACE_SLOT;
    work->segment = ctrlSlotsPagedIn((UInt16)addr->offset, &slot, &subSlot);
    work->slot    = (UInt8)slot;
    work->subSlot = (UInt8)subSlot;

    segmentSize = slotDebugGetSegmentSize(slot, subSlot);
    if (work->segment >= 0 && segmentSize > 0) {
        work->offset = addr->offset & (UInt32)(segmentSize - 1);
    }
    else {
        work->segment = CTRL_SEGMENT_FLAT;
        work->offset  = addr->offset;
    }
    return 1;
}

/* A segment address: readable from storage, and where the Z80 sees it, if it does. */
static int resolveSegment(CtrlAddr* work, int kind, int storageWritable, CtrlAddrInfo* info)
{
    int bank;

    if (info->segmentSize <= 0) {
        return 0;
    }
    if (work->offset >= (UInt32)info->segmentSize) {
        ctrlAddrNormalize(work);
        info->canonical = *work;
    }
    /* By count, not segment * size, which wraps for a large segment number. */
    info->readable = kind != SLOT_STORAGE_NONE && work->segment < info->segmentCount;
    info->writable = info->readable && storageWritable;

    bank = findVisibleBank(work, info->segmentSize);
    if (bank >= 0) {
        info->z80Address = bank * BANK_SIZE + (Int32)work->offset;
        info->z80Page    = bank / 2;
    }
    return 1;
}

/* A flat slot publishes no layout, so it is readable only while paged in. */
static int resolveFlat(const CtrlAddr* work, CtrlAddrInfo* info)
{
    int bank = (int)(work->offset / BANK_SIZE);
    int pagedSlot;
    int pagedSubSlot;

    if (work->offset > 0xffff) {
        return 0;
    }
    ctrlSlotsPageSlot((UInt16)work->offset, &pagedSlot, &pagedSubSlot);
    if (pagedSlot == (int)work->slot && pagedSubSlot == (int)work->subSlot) {
        int state = slotDebugGetBankState(work->slot, work->subSlot, bank);
        info->z80Address = (Int32)work->offset;
        info->z80Page    = bank / 2;
        info->readable   = (state & SLOT_DEBUG_HASDATA) != 0 ||
                           (state & SLOT_DEBUG_REGISTERED) != 0;
        info->writable   = (state & SLOT_DEBUG_WRITABLE) != 0;
    }
    return 1;
}

int ctrlAddrResolve(const CtrlAddr* addr, CtrlAddrInfo* info)
{
    CtrlAddr work = *addr;
    UInt8*   storage = NULL;
    UInt32   storageSize = 0;
    int      storageWritable = 0;
    int      kind;

    memset(info, 0, sizeof(CtrlAddrInfo));
    info->z80Address   = -1;
    info->z80Page      = -1;
    info->segmentCount = -1;

    if (addr->space == CTRL_SPACE_VRAM) {
        info->canonical  = work;
        info->spaceSize  = boardGetVramSize();
        info->readable   = info->spaceSize > 0 && work.offset < info->spaceSize;
        info->writable   = info->readable;
        strcpy(info->contents, "VRAM");
        return 1;
    }
    if (addr->space == CTRL_SPACE_CPU && !resolveCpu(addr, &work)) {
        return 0;
    }
    if (work.space != CTRL_SPACE_SLOT || work.slot > 3 || work.subSlot > 3) {
        return 0;
    }

    kind = slotDebugGetStorage(work.slot, work.subSlot,
                               &storage, &storageSize, &storageWritable);
    info->canonical   = work;
    info->segmentSize = slotDebugGetSegmentSize(work.slot, work.subSlot);
    info->spaceSize   = storageSize;
    strncpy(info->contents, slotDebugGetContents(work.slot, work.subSlot),
            CTRL_CONTENTS_MAX - 1);
    if (info->segmentSize > 0 && storageSize > 0) {
        info->segmentCount = (Int32)(storageSize / (UInt32)info->segmentSize);
    }

    if (work.segment >= 0 ? !resolveSegment(&work, kind, storageWritable, info)
                          : !resolveFlat(&work, info))
    {
        return 0;
    }
    /* A CPU address names its own window, even when the segment shows in another too. */
    if (addr->space == CTRL_SPACE_CPU) {
        info->z80Address = (Int32)addr->offset;
        info->z80Page    = (Int32)(addr->offset >> 14);
    }
    return 1;
}

int ctrlAddrPastEnd(const CtrlAddrInfo* info)
{
    return info->canonical.space == CTRL_SPACE_SLOT && info->canonical.segment >= 0 &&
           info->segmentCount >= 0 && info->canonical.segment >= info->segmentCount;
}

void ctrlAddrDescribe(CtrlText* text, const CtrlAddr* addr)
{
    CtrlAddrInfo info;
    char printed[64];

    if (!ctrlAddrResolve(addr, &info)) {
        ctrlAddrPrint(addr, printed, sizeof(printed));
        ctrlTextAdd(text, "%s  (not on this machine)\n", printed);
        return;
    }

    ctrlAddrPrint(&info.canonical, printed, sizeof(printed));
    if (ctrlAddrPastEnd(&info)) {
        ctrlAddrDescribePastEnd(text, &info);
        ctrlTextAdd(text, "\n");
        return;
    }
    if (addr->space == CTRL_SPACE_VRAM && info.spaceSize > 0 && !info.readable) {
        ctrlTextAdd(text, "%s  past the end of video memory, which is %u kB on this "
                    "machine (vram:0x0000-0x%04X)\n", printed,
                    (unsigned)(info.spaceSize / 1024), (unsigned)(info.spaceSize - 1));
        return;
    }
    ctrlTextAdd(text, "%s", printed);

    if (info.z80Address >= 0) {
        ctrlTextAdd(text, "  z80 0x%04X", info.z80Address);
    }
    else if (addr->space != CTRL_SPACE_VRAM) {
        ctrlTextAdd(text, "  not paged in");
    }
    if (info.segmentSize > 0) {
        ctrlTextAdd(text, "  segment %d kB", info.segmentSize / 1024);
        if (info.segmentCount > 0) {
            ctrlTextAdd(text, " x%d", info.segmentCount);
        }
    }
    if (info.contents[0] != 0) {
        ctrlTextAdd(text, "  %s", info.contents);
    }
    ctrlTextAdd(text, "%s\n", info.writable ? "" : "  read only");
}

void ctrlAddrDescribePastEnd(CtrlText* text, const CtrlAddrInfo* info)
{
    char printed[48];

    ctrlAddrPrint(&info->canonical, printed, sizeof(printed));
    if (info->segmentCount > 0) {
        ctrlTextAdd(text, "%s is past the end: slot%d.%d has segments 0-%d", printed,
                    info->canonical.slot, info->canonical.subSlot, (int)info->segmentCount - 1);
    }
    else {
        ctrlTextAdd(text, "%s is past the end: slot%d.%d holds less than one segment", printed,
                    info->canonical.slot, info->canonical.subSlot);
    }
}
