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
#ifndef CONTROL_SYMBOL_H
#define CONTROL_SYMBOL_H

#include "ControlAddressType.h"
#include "ControlProtocol.h"
#include "ControlSymbolParse.h"

/* Symbols are keyed by (slot, subSlot, segment, z80Address as assembled);
** the offset into the segment is derived, never stored. */

#define CTRL_SYM_FILES_MAX 8

#define CTRL_SYM_SLOT_NONE 0xFF

typedef struct {
    const char* name;
    UInt16      z80Address;
    Int32       segment;
    UInt8       slot;
    UInt8       subSlot;
    UInt8       kind;
    UInt8       fileId;
} CtrlSymbolInfo;

/* Where the code being read lives; picks among several symbols at one address. */
typedef struct {
    UInt8 slot;
    UInt8 subSlot;
    Int32 segment;
} CtrlSymHome;

typedef struct {
    const char* name;
    UInt16      at;        /* the symbol's own address      */
    UInt16      offset;    /* how far past it the query was */
    Int32       segment;
    UInt8       slot;
    UInt8       subSlot;
} CtrlSymNear;

typedef struct {
    char   path[CTRL_PATH_MAX];
    char   name[64];
    UInt8  format;
    UInt8  slot;
    UInt8  subSlot;
    Int32  symbolCount;
    Int32  maxBank;
    UInt32 machineGeneration;
    char   boundContents[CTRL_CONTENTS_MAX];
    /* As ctrlFileWriteTime gives them: the file's own time, and the clock, at the load. */
    Int64  fileTime;
    Int64  loadedAt;
} CtrlSymFile;

/* ---- loading ----------------------------------------------------------- */

/* wantSlot/wantSubSlot of -1 means "work it out"; replace of 0 adds to what is
** loaded, a file of the same name replacing its earlier load. On failure the
** loaded table is left intact. report is human-readable. */
int ctrlSymLoad(const char* path, int wantSlot, int wantSubSlot, int replace,
                char* report, int reportLen);

void ctrlSymClearAll(void);

int  ctrlSymCount(void);
int  ctrlSymFileCount(void);
int  ctrlSymFileGet(int index, CtrlSymFile* out);
/* An SLD never bound keeps its device pages, which are not segment numbers. */
int  ctrlSymFileHasPages(const CtrlSymFile* file);

/* ---- looking up -------------------------------------------------------- */

/* The name for `at` seen from `home`, or NULL. Prefers home's segment, then the
** paged-in one, then unqualified symbols. alsoOut, if given, gets the best loser. */
const char* ctrlSymFind(const CtrlSymHome* home, UInt16 at, int isAddress,
                        const char** alsoOut, Int32* alsoSegment);

/* The name at byte `offset` of that segment, whatever address it is paged in at. */
const char* ctrlSymInSegment(UInt8 slot, UInt8 subSlot, Int32 segment,
                             UInt32 offset);

/* Whether a symbol has a segment of a slot, rather than answering at its Z80 address. */
int ctrlSymHasSegmentAddress(const CtrlSymbolInfo* symbol);

/* The slot address of a symbol that has a segment; 0 when its slot reports no segment
** size now, which leaves the offset unknown (0). */
int ctrlSymAddress(const CtrlSymbolInfo* symbol, CtrlAddr* addr);

/* The nearest symbol at or before `at`, for "NAME+offset" labels. */
int ctrlSymNearest(const CtrlSymHome* home, UInt16 at, CtrlSymNear* out);

/* How many definitions of one name sym.find lists. */
#define CTRL_SYM_BY_NAME_MAX 64

/* Every symbol of that exact name, by Z80 address, then slot, subslot and segment; with
** distinct, one of each definition several files hold. Returns how many were filled;
** totalOut gets how many there are, which may be more than max. */
int ctrlSymByName(const char* name, CtrlSymbolInfo* out, int max, int distinct,
                  int* totalOut);

/* Shell-style pattern, * and ? only, case insensitive. Paginated. */
int ctrlSymSearch(const char* pattern, int first, int max,
                  CtrlSymbolInfo* out, int* totalOut);

/* Call from the lookup path when the machine may have changed; may rewrite the table. */
void ctrlSymRevalidate(void);

#endif
