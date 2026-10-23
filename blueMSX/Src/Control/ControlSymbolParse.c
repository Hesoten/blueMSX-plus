/*****************************************************************************
**
** Assembler symbol file parsers.
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
#include "ControlSymbolParse.h"
#include "ControlText.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
/* Routes fgets through the packet file system, as fopen is in ControlSymbol.c. */
#include "PacketFileSystem.h"

/* Bare digits are hex here, unlike in ControlAddress. Returns 0 if not a number. */
static int parseValue(const char* text, UInt32* value)
{
    UInt32 result = 0;
    int    digits = 0;

    if (text[0] == '$' || text[0] == '#') {
        text++;
    }
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
    }

    while (isxdigit((unsigned char)*text)) {
        int digit = ctrlTextHexDigit((unsigned char)*text);
        result = result * 16 + (UInt32)digit;
        digits++;
        text++;
    }
    if (digits == 0) {
        return 0;
    }
    if (*text == 'h' || *text == 'H') {
        text++;
    }
    if (*text != 0 && *text != ':') {
        return 0;
    }
    *value = result;
    return 1;
}

/* "05h:4018h" or "05:4018" or "4018h". Returns 0 when it is not an address. */
static int parseBankedAddress(const char* text, Int32* bank, UInt32* address)
{
    const char* colon = strchr(text, ':');
    UInt32 value;

    *bank = CTRL_SYM_SEG_ANY;

    if (colon != NULL) {
        char left[16];
        size_t length = (size_t)(colon - text);

        if (length == 0 || length >= sizeof(left)) {
            return 0;
        }
        memcpy(left, text, length);
        left[length] = 0;
        if (!parseValue(left, &value)) {
            return 0;
        }
        *bank = (Int32)value;
        text = colon + 1;
    }

    if (!parseValue(text, &value)) {
        return 0;
    }
    *address = value;
    return 1;
}

/* A name starts with a letter, _, ., @ or ?; a listing's line number or address does not. */
static int looksLikeName(const char* name)
{
    return isalpha((unsigned char)name[0]) || name[0] == '_' || name[0] == '.' ||
           name[0] == '@' || name[0] == '?';
}

/* "BEEF 4010h": a name can be hex digits too, so a prefix, suffix or leading digit marks the number. */
static int hasNumberMark(const char* text)
{
    size_t length = strlen(text);

    return isdigit((unsigned char)text[0]) || text[0] == '$' || text[0] == '#' ||
           (length > 1 && (text[length - 1] == 'h' || text[length - 1] == 'H'));
}

static int numberIsSecond(const char* first, const char* second)
{
    Int32  bank;
    UInt32 address;

    return !hasNumberMark(first) && hasNumberMark(second) &&
           parseBankedAddress(second, &bank, &address);
}

/* SDCC links a bank above 16 bits, as -Wl-b_SEG5=0x58000 puts SEG5 at 0x8000 of segment 5. */
static void splitSdccBank(UInt32 value, Int32* bank, UInt32* address)
{
    *bank    = value > 0xffff ? (Int32)(value >> 16) : CTRL_SYM_SEG_ANY;
    *address = value & 0xffff;
}

/* The lines of a NoICE file that are not DEF. */
static int isNoiceCommand(const char* word)
{
    static const char* const commands[] = {
        "LOAD", "FILE", "LINE", "FUNC", "SFUNC", "ENDFUNC", "CD"
    };
    int i;

    for (i = 0; i < (int)(sizeof(commands) / sizeof(commands[0])); i++) {
        if (_stricmp(word, commands[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* SDCC's linker adds area sizes (l__) and starts (s__), which are not labels. */
static int isLinkerName(const char* name)
{
    return strncmp(name, "l__", 3) == 0 || strncmp(name, "s__", 3) == 0 ||
           strncmp(name, ".__", 3) == 0;
}

static void trimEnd(char* text)
{
    size_t length = strlen(text);

    while (length > 0 && (isspace((unsigned char)text[length - 1]) ||
                          text[length - 1] == ':'))
    {
        text[--length] = 0;
    }
}

static int isAssignWord(const char* word)
{
    return _stricmp(word, "equ") == 0 || _stricmp(word, ".equ") == 0 ||
           _stricmp(word, "%equ") == 0 || _stricmp(word, ":equ") == 0 ||
           strcmp(word, "=") == 0 || strcmp(word, "==") == 0;
}

/* Copies a name, cut to what a symbol holds. */
static void takeName(char* name, const char* from)
{
    strncpy(name, from, CTRL_SYM_NAME_MAX - 1);
    name[CTRL_SYM_NAME_MAX - 1] = 0;
}

/* Returns 0 only when the table is full; a line it cannot use is counted and skipped. */
static int storeSymbol(CtrlSymSink sink, void* ref, CtrlSymParseResult* result,
                       const char* name, UInt32 address, Int32 bank, int kind)
{
    /* Skip rather than truncate: truncating would move the symbol. */
    if (address > 0xffff) {
        result->skippedTooBig++;
        return 1;
    }
    if (name[0] == 0) {
        result->skippedUnparsed++;
        return 1;
    }

    if (!sink(ref, name, address, bank, kind)) {
        return 0;
    }
    result->loaded++;
    if (bank != CTRL_SYM_SEG_ANY) {
        result->banked++;
        if (bank > result->maxBank) {
            result->maxBank = bank;
        }
    }
    if ((Int32)address < result->minAddress) {
        result->minAddress = (Int32)address;
    }
    if ((Int32)address > result->maxAddress) {
        result->maxAddress = (Int32)address;
    }
    return 1;
}

/* file|line|deffile|defline|page|value|type|data. F is a label, D a constant, Z the device. */
static int parseSldLine(char* line, char* name, UInt32* address, Int32* bank, int* kind,
                        CtrlSymParseResult* result)
{
    char* field[8];
    char* scan = line;
    int   count = 0;
    long  page;
    long  value;

    while (count < 8) {
        field[count++] = scan;
        scan = strchr(scan, '|');
        if (scan == NULL) {
            break;
        }
        *scan++ = 0;
    }
    if (count < 8) {
        return 0;
    }
    field[7][strcspn(field[7], "\r\n")] = 0;

    if (strcmp(field[6], "Z") == 0) {
        const char* size = strstr(field[7], "pages.size:");
        if (size != NULL) {
            result->pageSize = (UInt32)strtoul(size + 11, NULL, 10);
        }
        return 0;
    }
    if (strcmp(field[6], "F") != 0 && strcmp(field[6], "D") != 0) {
        return 0;
    }
    page  = strtol(field[4], NULL, 10);
    value = strtol(field[5], NULL, 10);
    if (value < 0) {
        return 0;
    }

    takeName(name, field[7]);
    *address = (UInt32)value;
    *kind    = field[6][0] == 'D' ? CTRL_SYM_EQU : CTRL_SYM_CODE;
    *bank    = CTRL_SYM_SEG_ANY;
    /* Banked only when one address window holds two pages; a flat program spreads over several. */
    if (*kind == CTRL_SYM_CODE && page >= 0 && value < 0xc000 && result->pageSize != 0) {
        int window = (int)((UInt32)value / result->pageSize);

        *bank = (Int32)page;
        if (window < 16) {
            if (result->slotPage[window] < 0) {
                result->slotPage[window] = (Int32)page;
            }
            else if (result->slotPage[window] != (Int32)page) {
                result->multiPage = 1;
            }
        }
        /* Only reported: ZX devices map flat code out of order, so this cannot decide banking. */
        if (!result->firstSeen) {
            result->firstSeen    = 1;
            result->firstPage    = (Int32)page;
            result->firstAddress = (Int32)value;
        }
        else if (!result->outOfOrder &&
                 (Int32)page - window != result->firstPage -
                                         (Int32)((UInt32)result->firstAddress / result->pageSize))
        {
            result->outOfOrder   = 1;
            result->otherPage    = (Int32)page;
            result->otherAddress = (Int32)value;
        }
    }
    return 1;
}

/* What the lines read so far say about the next one of a text symbol file. */
typedef struct {
    int section;    /* the kind its symbols are, or -1 in a section that holds none */
    int noice;
    int sdccMap;
} LineState;

static void readSection(const char* line, LineState* state, CtrlSymParseResult* result)
{
    /* [definitions] holds constants, not labels. */
    result->format = CTRL_SYM_FORMAT_WLADX;
    if (strstr(line, "definitions") != NULL) {
        state->section = CTRL_SYM_EQU;
    }
    else if (strstr(line, "labels") != NULL ||
             strstr(line, "symbols") != NULL)
    {
        state->section = CTRL_SYM_CODE;
    }
    else {
        /* Any other section holds no symbols. */
        state->section = -1;
    }
}

/* Splits at spaces into at most max words; returns how many. */
static int splitWords(char* scan, char** token, int max)
{
    int tokens = 0;

    while (tokens < max) {
        while (isspace((unsigned char)*scan)) {
            scan++;
        }
        if (*scan == 0) {
            break;
        }
        token[tokens++] = scan;
        while (*scan != 0 && !isspace((unsigned char)*scan)) {
            scan++;
        }
        if (*scan != 0) {
            *scan++ = 0;
        }
    }
    return tokens;
}

/* The symbol two or more words give, in any of the text formats; 0 when they give none. */
static int parseWords(char** token, int tokens, LineState* state, CtrlSymParseResult* result,
                      char* name, UInt32* address, Int32* bank, int* kind)
{
    if (tokens == 3 && _stricmp(token[0], "DEF") == 0 && strncmp(token[2], "0x", 2) == 0) {
        UInt32 value;
        state->noice = 1;
        if (tokens < 3 || !parseValue(token[2], &value)) {
            result->skippedUnparsed++;
            return 0;
        }
        if (isLinkerName(token[1])) {
            return 0;
        }
        takeName(name, token[1]);
        splitSdccBank(value, bank, address);
        if (result->format == CTRL_SYM_FORMAT_UNKNOWN) {
            result->format = CTRL_SYM_FORMAT_NOICE;
        }
    }
    else if (state->sdccMap) {
        UInt32 value;
        if (tokens < 2 || strlen(token[0]) != 8 || !isxdigit((unsigned char)token[0][0]) ||
            !parseValue(token[0], &value) || isLinkerName(token[1]))
        {
            return 0;
        }
        takeName(name, token[1]);
        splitSdccBank(value, bank, address);
    }
    else if (state->noice && isNoiceCommand(token[0])) {
        return 0;
    }
    else if (tokens >= 3 && isAssignWord(token[1])) {
        UInt32 value;
        if (!parseValue(token[2], &value)) {
            result->skippedUnparsed++;
            return 0;
        }
        takeName(name, token[0]);
        trimEnd(name);
        *address = value;
        *bank = CTRL_SYM_SEG_ANY;
        *kind = CTRL_SYM_DATA;
        if (result->format == CTRL_SYM_FORMAT_UNKNOWN) {
            result->format = CTRL_SYM_FORMAT_PLAIN;
        }
    }
    else if (!numberIsSecond(token[0], token[1]) &&
             parseBankedAddress(token[0], bank, address))
    {
        takeName(name, token[1]);
        trimEnd(name);
        if (result->format == CTRL_SYM_FORMAT_UNKNOWN) {
            result->format = *bank != CTRL_SYM_SEG_ANY
                                 ? CTRL_SYM_FORMAT_ASMSX : CTRL_SYM_FORMAT_PLAIN;
        }
    }
    else if (parseBankedAddress(token[1], bank, address)) {
        takeName(name, token[0]);
        trimEnd(name);
        if (result->format == CTRL_SYM_FORMAT_UNKNOWN) {
            result->format = CTRL_SYM_FORMAT_PLAIN;
        }
    }
    else {
        result->skippedUnparsed++;
        return 0;
    }

    if (!looksLikeName(name)) {
        result->skippedUnparsed += !state->sdccMap;
        return 0;
    }
    return 1;
}

/* One line of a symbol file other than SLD; 1 when it holds a symbol for the out arguments. */
static int parseTextLine(char* scan, LineState* state, CtrlSymParseResult* result,
                         char* name, UInt32* address, Int32* bank, int* kind)
{
    char* token[4];
    int   tokens;

    /* ; starts a comment, and so does a leading *. */
    {
        char* cut = strpbrk(scan, ";\r\n");
        if (cut != NULL) {
            *cut = 0;
        }
    }
    while (isspace((unsigned char)*scan)) {
        scan++;
    }
    if (*scan == 0 || *scan == '*') {
        return 0;
    }

    if (*scan == '[') {
        readSection(scan, state, result);
        return 0;
    }
    if (state->section < 0) {
        return 0;
    }

    tokens = splitWords(scan, token, 4);
    if (tokens < 2) {
        result->skippedUnparsed += !state->sdccMap;
        return 0;
    }
    return parseWords(token, tokens, state, result, name, address, bank, kind);
}

int ctrlSymParseFile(FILE* file, CtrlSymSink sink, void* ref, CtrlSymParseResult* result)
{
    char      line[1024];
    LineState state;

    /* Until a [section] header says otherwise. */
    state.section = CTRL_SYM_CODE;
    state.noice   = 0;
    state.sdccMap = 0;

    memset(result, 0, sizeof(CtrlSymParseResult));
    result->maxBank    = -1;
    result->minAddress = 0x10000;
    result->maxAddress = -1;
    result->format     = CTRL_SYM_FORMAT_UNKNOWN;
    memset(result->slotPage, 0xff, sizeof(result->slotPage));

    while (fgets(line, sizeof(line), file) != NULL) {
        char   name[CTRL_SYM_NAME_MAX];
        UInt32 address = 0;
        Int32  bank = CTRL_SYM_SEG_ANY;
        int    kind = state.section;
        int    found;

        if (strncmp(line, "|SLD.data.version|", 18) == 0) {
            result->format = CTRL_SYM_FORMAT_SLD;
            continue;
        }
        if (result->format == CTRL_SYM_FORMAT_SLD) {
            found = parseSldLine(line, name, &address, &bank, &kind, result);
        }
        else if (result->format == CTRL_SYM_FORMAT_UNKNOWN && strstr(line, "ASxxxx Linker") != NULL) {
            result->format = CTRL_SYM_FORMAT_SDCCMAP;
            state.sdccMap  = 1;
            continue;
        }
        else {
            found = parseTextLine(line, &state, result, name, &address, &bank, &kind);
        }
        if (found && !storeSymbol(sink, ref, result, name, address, bank, kind)) {
            result->full = 1;
            break;
        }
    }
    return !result->full;
}

Int32 ctrlSymPageToSegment(const CtrlSymParseResult* result, Int32 page, UInt16 address,
                           int segmentSize)
{
    UInt32 size = result->pageSize;

    if (page < 0 || size == 0) {
        return page;
    }
    if (segmentSize <= 0 || (size % (UInt32)segmentSize != 0 && (UInt32)segmentSize % size != 0)) {
        return CTRL_SYM_SEG_ANY;
    }
    return (Int32)(((UInt32)page * size + address % size) / (UInt32)segmentSize);
}
