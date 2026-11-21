/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/RomLoader.h,v $
**
** $Revision: 1.5 $
**
** $Date: 2008-03-30 18:38:42 $
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
#ifndef ROM_LOADER_H
#define ROM_LOADER_H
 
#include "MsxTypes.h"
#include "MediaDb.h"

UInt8* romLoad(const char *fileName, const char *fileInZipFile, int* size);

/* Reads fileName from Shared Roms in the machines directory, else from the old
** folder by the exe. A missing file is reported for device unless it is ROM_UNKNOWN. */
UInt8* romLoadShared(RomType device, const char* fileName, int* size);
/* Reports a Shared Roms file that was read but that device cannot use. */
void romRejectShared(RomType device, const char* fileName);

#endif
