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
#ifndef CONTROL_SYMBOL_PARSE_H
#define CONTROL_SYMBOL_PARSE_H

#include "MsxTypes.h"
#include <stdio.h>

#define CTRL_SYM_NAME_MAX  192   /* room for @Module.Sub.label names        */

/* The file gave no bank; distinct from CTRL_SEGMENT_FLAT, and resolved differently. */
#define CTRL_SYM_SEG_ANY   (-2)

#define CTRL_SYM_CODE 0
#define CTRL_SYM_DATA 1
#define CTRL_SYM_EQU  2   /* [definitions] constant; NAME equ loads as DATA */

#define CTRL_SYM_FORMAT_UNKNOWN 0
#define CTRL_SYM_FORMAT_ASMSX   1   /* NNh:AAAAh NAME / AAAAh NAME             */
#define CTRL_SYM_FORMAT_WLADX   2   /* [labels] then NN:AAAA NAME              */
#define CTRL_SYM_FORMAT_PLAIN   3   /* NAME: equ VALUEh, and its relatives     */
#define CTRL_SYM_FORMAT_NOICE   4   /* DEF NAME 0xBBAAAA (SDCC); bank above 16 bits */
#define CTRL_SYM_FORMAT_SDCCMAP 5   /* BBBBAAAA NAME MODULE (SDCC linker map)  */
#define CTRL_SYM_FORMAT_SLD     6   /* sjasmplus --sld, labels by device page   */

typedef struct {
    int loaded;
    int skippedTooBig;
    int skippedUnparsed;
    Int32 maxBank;
    Int32 minAddress;
    Int32 maxAddress;
    int banked;
    int format;
    UInt32 pageSize;    /* SLD: banks are device pages of this size, not segments */
    Int32 slotPage[16]; /* SLD: the page first seen in each address window */
    int multiPage;
    int outOfOrder;     /* SLD: two labelled pages do not follow their windows in order */
    int firstSeen;      /* SLD: the first labelled page and address are set */
    Int32 firstPage;
    Int32 firstAddress;
    Int32 otherPage;    /* SLD: the first one out of order with it */
    Int32 otherAddress;
    int full;          /* the table filled up before the end of the file */
} CtrlSymParseResult;

/* Takes one symbol as the file gives it; 0 when there is no room for it, which ends the read. */
typedef int (*CtrlSymSink)(void* ref, const char* name, UInt32 address, Int32 bank, int kind);

/* Reads every symbol line of an open file into sink; 0 when the sink filled up, with what
** came before kept. An SLD keeps its device pages as banks: ctrlSymPageToSegment maps them. */
int ctrlSymParseFile(FILE* file, CtrlSymSink sink, void* ref, CtrlSymParseResult* result);

/* An SLD page becomes a segment on the assumption that the ROM image holds the pages in order. */
Int32 ctrlSymPageToSegment(const CtrlSymParseResult* result, Int32 page, UInt16 address,
                           int segmentSize);

#endif
