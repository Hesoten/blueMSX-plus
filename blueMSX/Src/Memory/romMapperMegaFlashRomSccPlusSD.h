/*****************************************************************************
**
** MegaFlashROM SCC+ SD cartridge (AmdFlash 8 MB + SCC+ + SD).
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
/* MegaFlashROM SCC+ SD cartridge: two SD/MMC card slots and an 8 MB
** expandable flash layout on top of the MegaFlashROM SCC+ mapper.
** Subslot 3 carries the MegaSD boot ROM (typically Nextor).
**
** Flash layout (matches openMSX MFRSCC+SD):
**   0x000000 - 0x0FFFFF  subslot 0  MegaFlashROM SCC+  (1 MB)
**   0x200000 - 0x2FFFFF  subslot 1  aux ROM            (1 MB)
**   0x400000 - 0x4FFFFF  subslot 2  aux ROM            (1 MB)
**   0x700000 - 0x7FFFFF  subslot 3  MegaSD / Nextor    (1 MB)
*/

#ifndef ROMMAPPERMEGAFLASHROMSCCPLUSSD_H
#define ROMMAPPERMEGAFLASHROMSCCPLUSSD_H

#include "MsxTypes.h"

/* cartNo (0 or 1) selects the HD-id range for the cart's two SD slots:
** diskGetHdDriveId(cartNo, 0) and (cartNo, 1).  Pairs with the matching
** hdType[cartNo] = HD_MFRSD entry the menu uses.
*/
int romMapperMegaFlashRomSccPlusSDCreate(int cartNo, int slot, int sslot, int startPage);

#endif
