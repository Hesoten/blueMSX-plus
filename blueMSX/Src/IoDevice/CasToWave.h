/*****************************************************************************
**
** Kansas City Standard waveform generation for CAS tape images.
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
#ifndef CAS_TO_WAVE_H
#define CAS_TO_WAVE_H

#include "MsxTypes.h"
#include "TapeSignal.h"

/* Builds a 1200 baud KCS waveform from a CAS image. The block marker is the
** one Casette.c detected for this image. Returns NULL on failure. */
TapeSignalBuilder* casToWave(const UInt8* data, int size,
                             const UInt8* marker, int markerSize);

#endif
