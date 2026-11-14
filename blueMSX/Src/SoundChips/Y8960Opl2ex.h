/*****************************************************************************
**
** Y8960 cartridge - the OPL2EX block: a YM3812 + ADPCM-B circuit with the
** cartridge's sample RAM.
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
#ifndef Y8960_OPL2EX_H
#define Y8960_OPL2EX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "MsxTypes.h"
#include "AudioMixer.h"

typedef struct Y8960Opl2ex Y8960Opl2ex;

Y8960Opl2ex* y8960Opl2exCreate(Mixer* mixer);
void y8960Opl2exDestroy(Y8960Opl2ex* chip);

void y8960Opl2exReset(Y8960Opl2ex* chip);

/* port 0 latches the register number, port 1 transfers data. */
void  y8960Opl2exWrite(Y8960Opl2ex* chip, int port, UInt8 value);
UInt8 y8960Opl2exRead (Y8960Opl2ex* chip, int port);
UInt8 y8960Opl2exPeek (Y8960Opl2ex* chip, int port);

void y8960Opl2exSaveState(Y8960Opl2ex* chip);
void y8960Opl2exLoadState(Y8960Opl2ex* chip);

#ifdef __cplusplus
}
#endif

#endif
