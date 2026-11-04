/*****************************************************************************
**
** Register tables of the video chips and the raw table that shows them.
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
#ifndef REG_TABLES_H
#define REG_TABLES_H

#include "MsxTypes.h"

/* A bit field of a register; width 1 is a flag. */
struct RegField {
    int         shift, width;
    const char* name;
    const char* tip;
};

struct RegDesc {
    int             index;
    const char*     name;
    const RegField* fields;
    int             count;
};

struct RegBank {
    const char*    id;
    const char*    prefix;     /* "R#", "S#", "P#" */
    const RegDesc* regs;
    int            count;
    bool           hex;        /* indices as 30h, else as R#30 */
};

/* What a bank looked like at the last content, for the change highlight. */
struct RegFade {
    UInt8    prev[256];
    float    heat[256];
    unsigned version;
    unsigned step;     /* the step whose changes are lit */
    bool     primed;
};

void regFadeReset(RegFade& f);

/* One row per register whose index passes `shown` (NULL: all); values are
** indexed by register number. Rows that changed with the last content
** fade from the accent; the tip lists the fields. */
void drawRegBank(const RegBank& bank, const UInt8* values, RegFade& fade,
                 bool (*shown)(int index, const void* ref) = 0, const void* ref = 0);

/* The description of register index, or NULL. */
const RegDesc* regBankFind(const RegBank& bank, int index);

extern const RegBank regBankV99x8;
extern const RegBank regBankV99x8Status;
/* regBankV99x8 with a V9968's R#20, R#21, R#25 and R#45 and its R#47-58. */
const RegBank& regBankV9968();
extern const RegBank regBankV9990;
extern const RegBank regBankV9990Ports;

#endif
