/*****************************************************************************
**
** Yamanooto flash + SCC + PSG cartridge.
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
/* 8 MB flash cartridge with FPGA-generated SCC / SCC+ and secondary PSG.
** Konami-SCC (K5) / Konami-4 (K4) mapper via CFGR K4 bit; config regs
** CFGR / OFFR / ENAR at 7FFDh-7FFFh gated by ENAR REGEN. */
#ifndef ROMMAPPER_YAMANOOTO_H
#define ROMMAPPER_YAMANOOTO_H

#include "MsxTypes.h"

int romMapperYamanootoCreate(const char* filename, UInt8* romData,
                             int size, int slot, int sslot, int startPage);

#endif
