/*****************************************************************************
**
** GFX9000 V9990 graphics cartridge.
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
#ifndef ROMMAPPER_GFX9000_H
#define ROMMAPPER_GFX9000_H

#include "MsxTypes.h"

/* Returns 0 without inserting anything on a machine that is not an MSX or
** when another device already answers on one of the ports 0x60-0x6F. */
int romMapperGfx9000Create(int slot, int sslot);

#endif
