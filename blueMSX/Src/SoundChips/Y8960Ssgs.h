/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/AY8910.h,v $
**
** $Revision: 1.13 $
**
** $Date: 2008-03-30 18:38:44 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Forked for the Y8960 cartridge, 2026 by madscient.
** The cartridge carries an SSGS: two YM2149 equivalents, each channel with
** its own volume and pan, not a pair of AY-3-8910s. It is emulated as its own
** chip rather than sharing the machine's PSG.
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
#ifndef Y8960_SSGS_H
#define Y8960_SSGS_H

#include "MsxTypes.h"
#include "AudioMixer.h"

typedef struct Y8960SsgsChip Y8960SsgsChip;

/* Same shape as the PSG's, so whatever wires the machine's joystick ports,
** cassette and kana LED can drive this chip without knowing which it is. */
typedef UInt8 (*Y8960SsgsReadCb)(void* ref, UInt16 port);
typedef void  (*Y8960SsgsWriteCb)(void* ref, UInt16 port, UInt8 data);

Y8960SsgsChip* y8960SsgsCreate(Mixer* mixer, const char* name);
void y8960SsgsDestroy(Y8960SsgsChip* chip);

void y8960SsgsReset(Y8960SsgsChip* chip);

/* The address covers both cores, 00h-0Fh for the first and 10h-1Fh for the
** second, and the volume and pan registers at 20h-2Fh. */
void y8960SsgsWriteAddress(Y8960SsgsChip* chip, UInt8 address);
void y8960SsgsWriteData(Y8960SsgsChip* chip, UInt8 data);

/* Every register as written, both cores: the probe's and the debugger's view. */
UInt8 y8960SsgsReadData(Y8960SsgsChip* chip);

/* What the cartridge drives onto A2h, which it shares with the machine's PSG:
** the second core's registers, and FFh, not answering, for everything else. */
UInt8 y8960SsgsReadPort(Y8960SsgsChip* chip);

/* Attaches a GPIO to the first core's 0Eh and 0Fh. The cartridge has none,
** and those two registers are then plain storage. Kept as the AY8910 has it,
** in case a Y8960 ever stands in for a machine's PSG again. */
void y8960SsgsSetIoPort(Y8960SsgsChip* chip, Y8960SsgsReadCb readCb,
                        Y8960SsgsReadCb pollCb, Y8960SsgsWriteCb writeCb, void* ref);

/* The four LED bits on the second core's port B (1Fh). Nothing drives them;
** they are kept so the debugger can show what software asked for. */
UInt8 y8960SsgsGetLed(Y8960SsgsChip* chip);

void y8960SsgsSaveState(Y8960SsgsChip* chip);
void y8960SsgsLoadState(Y8960SsgsChip* chip);

#endif
