/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/IoDevice/GameReader.h,v $
**
** $Revision: 1.4 $
**
** $Date: 2008-03-30 18:38:40 $
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
#ifndef GAMER_READER_H
#define GAMER_READER_H

#include "MsxTypes.h"

typedef void* GrHandle;

#define GAMEREADER_AVAILABLE 0  /* library loaded and enough readers found  */
#define GAMEREADER_NO_DLL    1  /* MSXGr.dll absent, or not an MSXGr.dll    */
#define GAMEREADER_NO_DEVICE 2  /* library is fine, but too few readers     */

int gameReaderSupported();

/* One of the GAMEREADER_ values above. Answers whether at least wanted readers
** are attached, counting any already driving a cartridge. wanted must be at
** least 1. */
int gameReaderAvailability(int wanted);

/* The next free reader, or NULL when there is none. Readers are handed out in
** the order they were detected, which has nothing to do with cartridge slots. */
GrHandle* gameReaderCreate(void);
void gameReaderDestroy(GrHandle* grHandle);

/* 0 when the reader holds no cartridge or the transfer failed; buffer is then
** left untouched. */
int gameReaderRead(GrHandle* grHandle, UInt16 address, void* buffer, int length);
int gameReaderWrite(GrHandle* grHandle, UInt16 address, void* buffer, int length);

int gameReaderReadIo(GrHandle* grHandle, UInt16 port, UInt8* value);
int gameReaderWriteIo(GrHandle* grHandle, UInt16 port, UInt8 value);

#endif
