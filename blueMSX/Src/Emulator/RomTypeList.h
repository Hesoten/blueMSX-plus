/*****************************************************************************
**
** Tables of the rom types: the mappers and the built-in cartridges.
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
#ifndef ROM_TYPE_LIST_H
#define ROM_TYPE_LIST_H

#include "MediaDb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The group a rom type is listed under. A type can be in a different group in
** each table: ASCII16-X is a mapper for an image and a flash cartridge when it
** is inserted. */
typedef enum {
    ROMCAT_NONE = 0,
    ROMCAT_PLAIN,       /* Plain roms with no mapper */
    ROMCAT_COMMON,      /* Common mappers */
    ROMCAT_ONEGAME,     /* Mappers unique to one game or program */
    ROMCAT_MULTIGAME,   /* Multi game cartridges */
    ROMCAT_FLASH,       /* Flash cartridges */
    ROMCAT_MEMORY,      /* Memory expansions */
    ROMCAT_SOUND,       /* Sound cartridges */
    ROMCAT_STORAGE,     /* Disk and storage interfaces */
    ROMCAT_NETWORK,     /* Network interfaces */
    ROMCAT_KANJI,       /* Kanji and dictionary roms */
    ROMCAT_OTHER,       /* Other hardware */
    ROMCAT_NONMSX,      /* Non-MSX systems */
    ROMCAT_COUNT
} RomTypeCategory;

/* The heading for a group, or NULL past the last group. Group 0 is not listed. */
const char* romTypeListGroupName(int category);
const char* romTypeListCartGroupName(int category);

/* The mapper at index in display order, or ROM_UNKNOWN past the end. */
RomType         romTypeListMapperAt(int index);
RomTypeCategory romTypeListMapperCategory(RomType romType);

/* The cartridge at index, or ROM_UNKNOWN past the end. A hidden one is not
** listed, but its name is still accepted. */
RomType         romTypeListCartAt(int index);
RomTypeCategory romTypeListCartCategory(RomType romType);
int             romTypeListCartIsHiddenAt(int index);

/* The name a cartridge is stored under, and the lookup back from that name. */
const char* romTypeListCartName(RomType romType);
RomType     romTypeListCartFromName(const char* cartName);

#ifdef __cplusplus
}
#endif

#endif /* ROM_TYPE_LIST_H */
