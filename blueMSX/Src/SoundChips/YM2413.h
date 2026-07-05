/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/YM2413.h,v $
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
#ifndef YM2413_H
#define YM2413_H

#ifdef __cplusplus
extern "C" {
#endif

#include "MsxTypes.h"
#include "AudioMixer.h"
#include "DebugDeviceManager.h"

/* Type definitions */
typedef struct YM_2413 YM_2413;

/* Constructor and destructor */
YM_2413* ym2413Create(Mixer* mixer);
void ym2413Destroy(YM_2413* ym2413);
void ym2413WriteAddress(YM_2413* ym2413, UInt8 address);
void ym2413WriteData(YM_2413* ym2413, UInt8 data);
void ym2413Reset(YM_2413* ref);
void ym2413SaveState(YM_2413* ref);
void ym2413LoadState(YM_2413* ref);
void ym2413GetDebugInfo(YM_2413* ym2413, DbgDevice* dbgDevice);

/* Process-wide active YM2413 backend selector shared by every chip
** instance (FM-PAC, FM-PAK, MSX-MUSIC built-in all switch in unison).
** See Ym2413MultiBackend.h. */
int         ym2413BackendActiveGet(void);
void        ym2413BackendActiveSet(int idx);
int         ym2413BackendIsEnabled(int idx);
int         ym2413BackendCycle(void);
const char* ym2413BackendName(int idx);

extern const int ym2413BackendDisplayOrder[];
extern const int ym2413BackendDisplayCount;

/* Hot-apply analog post-filter cutoffs (LPF / HPF, 0 = bypass). */
void        ym2413AnalogFilterSet(int lpfHz, int hpfHz);

#ifdef __cplusplus
}
#endif

#endif

