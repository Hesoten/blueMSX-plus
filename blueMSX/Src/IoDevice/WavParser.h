/*****************************************************************************
**
** WAV tape recording parsing.
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
#ifndef WAV_PARSER_H
#define WAV_PARSER_H

#include "MsxTypes.h"
#include "TapeSignal.h"

/* True if the image is a RIFF/WAVE file */
int wavIsWavImage(const UInt8* data, int size);

/* Slices a WAV recording into polarity edges. Returns NULL on failure and
** writes a one line reason into err. */
TapeSignalBuilder* wavToWave(const UInt8* data, int size, char* err, int errSize);

#endif
