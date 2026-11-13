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
#include "MapperDebugBanks.h"
#include "SlotManager.h"
#include "DebugDeviceManager.h"
#include <stdio.h>
#include <string.h>

/* A note's own words and a file name: the save file names mappers keep fit in full. */
#define NOTE_MAX 640

void mapperBanksInit(MapperBanks* banks, const MapperBanksType* type, void* ref,
                     int slot, int sslot, int firstBank)
{
    banks->type      = type;
    banks->ref       = ref;
    banks->slot      = slot;
    banks->sslot     = sslot;
    banks->firstBank = firstBank;
    mapperBanksSync(banks);
    slotSetBankSwitch(slot, sslot, banks);
}

void mapperBanksProgramWrote(MapperBanks* banks, int reg)
{
    /* An agent's or the window's write through the bus is a debugger switch. */
    if (debugDeviceIsInspecting()) {
        return;
    }
    banks->program[reg] = banks->type->get(banks->ref, reg);
}

void mapperBanksSync(MapperBanks* banks)
{
    int reg;

    for (reg = 0; reg < banks->type->registers; reg++) {
        banks->program[reg] = banks->type->get(banks->ref, reg);
    }
}

int mapperBanksSegments(const MapperBanks* banks)
{
    int    segmentSize = slotDebugGetSegmentSize(banks->slot, banks->sslot);
    UInt32 size = 0;
    int    segments;

    if (segmentSize <= 0 ||
        slotDebugGetStorage(banks->slot, banks->sslot, NULL, &size, NULL) == SLOT_STORAGE_NONE)
    {
        return 0;
    }
    segments = (int)(size / (UInt32)segmentSize);
    return segments < banks->type->maxSegments ? segments : banks->type->maxSegments;
}

int mapperBanksHasSram(const MapperBanks* banks)
{
    /* The SRAM is selected by a value past the ROM's last segment, which 8 bits must hold. */
    return banks->type->sram && mapperBanksSegments(banks) < 256;
}

int mapperBanksRegisterAt(const MapperBanks* banks, int bank)
{
    int offset = bank - banks->firstBank;

    if (offset < 0 || offset >= banks->type->registers * banks->type->banksPerRegister) {
        return -1;
    }
    return offset / banks->type->banksPerRegister;
}

int mapperBanksSwitched(const MapperBanks* banks, int reg)
{
    return banks->type->get(banks->ref, reg) != banks->program[reg];
}

int mapperBanksSelect(MapperBanks* banks, int reg, int segment, const char** why)
{
    const MapperBanksType* type = banks->type;

    *why = type->blocked != NULL ? type->blocked(banks->ref, reg) : NULL;
    if (*why != NULL) {
        return MAPPER_SELECT_BLOCKED;
    }
    if (segment == MAPPER_BANK_SRAM) {
        if (!mapperBanksHasSram(banks)) {
            return MAPPER_SELECT_NO_SRAM;
        }
    }
    else if (segment < 0 || segment >= mapperBanksSegments(banks)) {
        return MAPPER_SELECT_PAST_END;
    }
    else if (type->neverSelected != NULL &&
             (*why = type->neverSelected(banks->ref, segment)) != NULL)
    {
        return MAPPER_SELECT_NEVER;
    }
    type->set(banks->ref, reg, segment);
    return MAPPER_SELECT_OK;
}

void mapperBanksRange(const MapperBanks* banks, int reg, char* out, int len)
{
    int first = (banks->firstBank + reg * banks->type->banksPerRegister) * 0x2000;

    snprintf(out, (size_t)len, "%04X-%04X", first, first + banks->type->banksPerRegister * 0x2000 - 1);
}

static const char* fileName(const char* path)
{
    const char* slash = strrchr(path, '\\');

    return slash != NULL ? slash + 1 : path;
}

/* A text cut short at size ends in "...", so a long file name never reads as whole. */
static const char* noteEnd(char* note, size_t size, int written)
{
    if (written < 0 || (size_t)written >= size) {
        memcpy(note + size - 4, "...", 4);
    }
    return note;
}

const char* mapperBanksFlashNote(const MapperBanks* banks, int reg, const char* path)
{
    static char note[NOTE_MAX];
    char range[16];
    int  segment = banks->type->get(banks->ref, reg);

    mapperBanksRange(banks, reg, range, sizeof(range));
    if (path == NULL) {
        snprintf(note, sizeof(note), "the program's flash writes to %s now go to seg%d, and no "
                 "file keeps them", range, segment);
        return note;
    }
    return noteEnd(note, sizeof(note), snprintf(note, sizeof(note), "the program's flash writes "
                   "to %s now go to seg%d and are kept in %s", range, segment, fileName(path)));
}

const char* mapperBanksSramNote(const MapperBanks* banks, int reg, const char* path)
{
    static char note[NOTE_MAX];
    char range[16];

    mapperBanksRange(banks, reg, range, sizeof(range));
    return noteEnd(note, sizeof(note), snprintf(note, sizeof(note), "the program's writes to %s "
                   "now go to the SRAM and are saved to %s when the cartridge is removed", range,
                   fileName(path)));
}
