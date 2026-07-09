/*****************************************************************************
**
** NEO-8 megarom mapper.
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
/* 16-bit segment register megarom (MSXgl NEO spec v1.2).
** Six 8 KB regions mapped at 0x0000-0xBFFF (Page 0-2, Page 3 unmapped).
** Register writes at 5000/5800/6000/6800/7000/7800h (mirrored every 0x0800);
** even address = LSB, odd address = MSB (6 usable bits, top 2 reserved).
*/
#ifndef ROMMAPPER_NEO8_H
#define ROMMAPPER_NEO8_H

#include "MsxTypes.h"

int romMapperNeo8Create(const char* filename, UInt8* romData,
                        int size, int slot, int sslot, int startPage);

#endif
