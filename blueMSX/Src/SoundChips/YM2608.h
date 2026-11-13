/*****************************************************************************
**
** YM2608 (OPNA) sound chip on top of ymfm.
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
#ifndef YM2608_H
#define YM2608_H

#ifdef __cplusplus
extern "C" {
#endif

#include "MsxTypes.h"
#include "AudioMixer.h"
#include "DebugDeviceManager.h"

typedef struct YM2608 YM2608;

/* rhythmRom may be NULL: the rhythm part is then silent. The chip keeps its
** own copy of the ROM, so the caller may free it after the call. */
YM2608* ym2608Create(Mixer* mixer, UInt32 clock, UInt32 adpcmRamSize,
                     const UInt8* rhythmRom, int rhythmRomSize, UInt32 irqMask);
void ym2608Destroy(YM2608* ym2608);
void ym2608Reset(YM2608* ym2608);

/* offset is the chip's A1:A0 (0: address/status, 1: data,
** 2: address/status of the upper half, 3: data of the upper half) */
UInt8 ym2608Read(YM2608* ym2608, int offset);
UInt8 ym2608Peek(YM2608* ym2608, int offset);
void  ym2608Write(YM2608* ym2608, int offset, UInt8 value);

void ym2608SaveState(YM2608* ym2608);
void ym2608LoadState(YM2608* ym2608);
void ym2608GetDebugInfo(YM2608* ym2608, DbgDevice* dbgDevice);

#ifdef __cplusplus
}
#endif

#endif
