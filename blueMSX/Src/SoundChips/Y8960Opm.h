/*****************************************************************************
**
** Y8960 cartridge - the OPM block's sound chip, an equivalent of the YM2151.
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Forked from blueMSX's ym2151.h for the Y8960 cartridge, 2026 by madscient.
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
#ifndef Y8960_OPM_H
#define Y8960_OPM_H

#include "MsxTypes.h"
#include "AudioMixer.h"

typedef struct Y8960OpmChip Y8960OpmChip;

Y8960OpmChip* y8960OpmCreate(Mixer* mixer);
void y8960OpmDestroy(Y8960OpmChip* chip);

void y8960OpmReset(Y8960OpmChip* chip);

void  y8960OpmWriteAddress(Y8960OpmChip* chip, UInt8 address);
void  y8960OpmWriteData(Y8960OpmChip* chip, UInt8 data);
UInt8 y8960OpmReadStatus(Y8960OpmChip* chip);

void y8960OpmSaveState(Y8960OpmChip* chip);
void y8960OpmLoadState(Y8960OpmChip* chip);

#endif
