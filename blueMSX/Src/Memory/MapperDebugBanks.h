/*****************************************************************************
**
** ROM mapper bank switching for the debugger.
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
#ifndef MAPPER_DEBUG_BANKS_H
#define MAPPER_DEBUG_BANKS_H

#include "MsxTypes.h"

/* A switch is a real selection, as if the program had written the bank register: its next
** write to that register replaces it. program[] keeps what the program itself last selected,
** so a bank the debugger switched can be told apart. */

#define MAPPER_BANKS_MAX 6

/* get(): the SRAM shows instead of a ROM segment. */
#define MAPPER_BANK_SRAM (-2)

/* What mapperBanksSelect did. */
#define MAPPER_SELECT_OK       0
#define MAPPER_SELECT_PAST_END 1
#define MAPPER_SELECT_BLOCKED  2
#define MAPPER_SELECT_NO_SRAM  3
#define MAPPER_SELECT_NEVER    4   /* in the ROM, but no bank value the program writes selects it */

typedef struct {
    int registers;          /* 2..MAPPER_BANKS_MAX */
    int banksPerRegister;   /* 8 kB banks one register switches: 1 or 2 */
    int maxSegments;        /* the most its register can select: 256 for an 8 bit one */
    int sram;               /* MAPPER_BANK_SRAM can be selected, when the ROM leaves room */
    int  (*get)(void* ref, int reg);                    /* latched segment, or MAPPER_BANK_SRAM */
    void (*set)(void* ref, int reg, int segment);       /* latch and map it; no other state changes */
    const char* (*blocked)(void* ref, int reg);         /* why it cannot be switched now, else NULL */
    const char* (*note)(void* ref, int reg);            /* what else answers in the range, else NULL */
    const char* (*neverSelected)(void* ref, int segment); /* why no program write selects it */
} MapperBanksType;

typedef struct MapperBanks {
    const MapperBanksType* type;
    void* ref;
    int   slot;
    int   sslot;
    int   firstBank;                 /* the 8 kB bank of the slot register 0 switches */
    int   program[MAPPER_BANKS_MAX]; /* get() after the program's last write to each register */
} MapperBanks;

/* After the slot's storage and segment size are set: publishes banks to the slot and syncs. */
void mapperBanksInit(MapperBanks* banks, const MapperBanksType* type, void* ref,
                     int slot, int sslot, int firstBank);
/* Every program write to reg, equal or not; a debugger's write through the bus is not one. */
void mapperBanksProgramWrote(MapperBanks* banks, int reg);
/* At create, reset and loadState: what each register holds is the program's own. */
void mapperBanksSync(MapperBanks* banks);
/* Switches reg to segment, or MAPPER_BANK_SRAM; a MAPPER_SELECT_ value, with the blocked text. */
int  mapperBanksSelect(MapperBanks* banks, int reg, int segment, const char** why);

/* Segments a register can select: the slot's storage, up to the register's reach. */
int  mapperBanksSegments(const MapperBanks* banks);
int  mapperBanksHasSram(const MapperBanks* banks);
/* The register that switches an 8 kB bank of the slot, or -1. */
int  mapperBanksRegisterAt(const MapperBanks* banks, int bank);
/* Whether reg shows other than what the program selected. */
int  mapperBanksSwitched(const MapperBanks* banks, int reg);

/* The Z80 range a register switches, as "8000-9FFF". */
void mapperBanksRange(const MapperBanks* banks, int reg, char* out, int len);
/* note() texts: where the program's writes in reg's window now go, in the file at path; a
** flash path is NULL when no file keeps them. A file name too long for the text ends in "...". */
const char* mapperBanksFlashNote(const MapperBanks* banks, int reg, const char* path);
const char* mapperBanksSramNote(const MapperBanks* banks, int reg, const char* path);

#endif
