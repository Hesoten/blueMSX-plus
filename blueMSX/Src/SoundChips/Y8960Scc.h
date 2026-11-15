/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/SCC.h,v $
**
** $Revision: 1.8 $
**
** $Date: 2008-03-30 18:38:45 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Forked for the Y8960 cartridge, 2026 by madscient.
** The cartridge carries an SCC-equivalent circuit (IKASCC in the hardware),
** run on the machine's SCC core with a pan per channel and its own volume.
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
#ifndef Y8960_SCC_H
#define Y8960_SCC_H

#include "MsxTypes.h"
#include "AudioMixer.h"
#include "SCC.h"

/* Type definitions */
typedef struct Y8960SccChip Y8960SccChip;

/* Constructor and destructor */
Y8960SccChip* y8960SccCreate(Mixer* mixer);
void y8960SccDestroy(Y8960SccChip* scc);
void y8960SccReset(Y8960SccChip* scc);
void y8960SccSetMode(Y8960SccChip* scc, SccMode newMode);

/* Register read/write methods */
UInt8 y8960SccRead(Y8960SccChip* scc, UInt8 address);
UInt8 y8960SccPeek(Y8960SccChip* scc, UInt8 address);
void y8960SccWrite(Y8960SccChip* scc, UInt8 address, UInt8 value);

/* The per-channel pan (channels 0-4 are A-E; others are ignored). Its
** registers lie outside the 256 bytes the SCC decodes, so the mapper takes
** them and hands them in here. The law is y8960PanGain(). */
void y8960SccWritePan(Y8960SccChip* scc, UInt8 channel, UInt8 value);

void y8960SccLoadState(Y8960SccChip* scc);
void y8960SccSaveState(Y8960SccChip* scc);

#endif

