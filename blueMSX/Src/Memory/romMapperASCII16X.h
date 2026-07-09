/*****************************************************************************
**
** ASCII16-X flashrom mapper.
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
/* 12-bit bank register extension of ASCII16, up to 64 MB flash (S29GL064 series).
** Bank register at 6000-6FFFh (bank 1) and 7000-7FFFh (bank 2), mirrored across
** 2000/A000/E000 and 3000/B000/F000.  Bank number = (addr[11:8] << 8) | data.
*/
#ifndef ROMMAPPER_ASCII16X_H
#define ROMMAPPER_ASCII16X_H

#include "MsxTypes.h"

int romMapperASCII16XCreate(const char* filename, UInt8* romData,
                            int size, int slot, int sslot, int startPage);

#endif
