/*****************************************************************************
**
** Y8960 cartridge - the OPLLEX chip: a YM2413 with four preset ROMs.
**
** Copyright (C) 2026 madscient
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
#ifndef Y8960_OPLLEX_H
#define Y8960_OPLLEX_H

#include "MsxTypes.h"
#include "AudioMixer.h"

typedef struct Y8960OpllexChip Y8960OpllexChip;

/* name distinguishes the two circuits in the debugger. */
Y8960OpllexChip* y8960OpllexCreate(Mixer* mixer, const char* name);
void y8960OpllexDestroy(Y8960OpllexChip* chip);

void y8960OpllexReset(Y8960OpllexChip* chip);

void y8960OpllexWriteAddress(Y8960OpllexChip* chip, UInt8 address);
void y8960OpllexWriteData(Y8960OpllexChip* chip, UInt8 data);

void y8960OpllexSaveState(Y8960OpllexChip* chip);
void y8960OpllexLoadState(Y8960OpllexChip* chip);

#endif
