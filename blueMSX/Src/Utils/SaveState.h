/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Utils/SaveState.h,v $
**
** $Revision: 1.7 $
**
** $Date: 2009-07-18 14:10:27 $
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
#ifndef SAVE_STATE_H
#define SAVE_STATE_H
 
#include "MsxTypes.h"

typedef struct SaveState SaveState;

void saveStateCreateForRead(const char* fileName);
void saveStateCreateForWrite(const char* fileName);
void saveStateDestroy(void);

SaveState* saveStateOpenForRead(const char* fileName);
SaveState* saveStateOpenForWrite(const char* fileName);
void saveStateClose(SaveState* state);

/* Non-zero if the requested section was absent in the loaded zip; lets
** LoadState skip restore when a chip was disabled at save time. */
int saveStateIsEmpty(SaveState* state);

UInt32 saveStateGet(SaveState* state, const char* tagName, UInt32 defValue);
void saveStateSet(SaveState* state, const char* tagName, UInt32 value);

void saveStateGetBuffer(SaveState* state, const char* tagName, void* buffer, UInt32 length);
void saveStateSetBuffer(SaveState* state, const char* tagName, void* buffer, UInt32 length);

/* Non-zero if fileName is an old (blueMSX 2.8.2 era: "v 8" + pre-rename VDP
** tags) state save. Such states need the old-format load fallbacks and resume
** unreliably; the UI warns before loading one. */
int saveStateFileFormatIsOld(const char* fileName);

#endif /* SAVE_STATE_H */

