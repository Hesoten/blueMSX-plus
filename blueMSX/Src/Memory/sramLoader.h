/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/sramLoader.h,v $
**
** $Revision: 1.6 $
**
** $Date: 2008-03-30 18:38:44 $
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
#ifndef SRAM_LOADER_H
#define SRAM_LOADER_H
 
#include "MsxTypes.h"

const char* sramCreateFilename(const char* romFilename);
const char* sramCreateFilenameWithSuffix(const char* romFilename, char* suffix, char* ext);
void sramLoad(const char* filename, UInt8* sram, int length, void* header, int headerLength);
void sramSave(const char* filename, UInt8* sram, int length, void* header, int headerLength);

#define SRAM_FILE_MISSING  0
#define SRAM_FILE_PRESENT  1
#define SRAM_FILE_ERROR   -1

/* Only a file that does not exist is missing; one that cannot be opened or
** examined is an error, so a locked save is never taken for an absent one. */
int sramFileStatus(const char* filename, int* size);
/* The whole file in a malloc'd buffer, or NULL. */
UInt8* sramLoadFile(const char* filename, int* length);
/* Writes a temporary file first, so a failed write leaves the old one. */
int sramSaveReplace(const char* filename, const UInt8* data, int length);

#endif
