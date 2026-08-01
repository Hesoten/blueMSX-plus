/*****************************************************************************
**
** TZX 1.2x / TSX tape image parsing.
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
#ifndef TSX_PARSER_H
#define TSX_PARSER_H

#include "MsxTypes.h"
#include "TapeSignal.h"

/* True if the image starts with the ZXTape! signature */
int tsxIsTsxImage(const UInt8* data, int size);

/* Builds a waveform from a TZX/TSX image. Returns NULL on failure and writes
** a one line reason into err. */
TapeSignalBuilder* tsxToWave(const UInt8* data, int size, char* err, int errSize);

#endif
