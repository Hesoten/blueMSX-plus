/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/IoDevice/Casette.h,v $
**
** $Revision: 1.11 $
**
** $Date: 2008-09-09 04:40:32 $
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
#ifndef CASSETTE_H
#define CASSETTE_H

#include "MsxTypes.h"

typedef enum { TAPE_ASCII = 0, TAPE_BINARY, TAPE_BASIC, TAPE_CUSTOM } TapeContentType;

/* Keep TAPE_TSX last: Actions.c matches the save formats by their numbers */
typedef enum { TAPE_UNKNOWN = 0, TAPE_FMSXDOS, TAPE_FMSX98AT, TAPE_SVICAS, TAPE_WAV, TAPE_TSX } TapeFormat;

typedef struct {
    int             pos;
    TapeContentType type;
    char            fileName[8];
} TapeContent;

void   tapeSetDirectory(char* baseDir, char* prefix);
int    tapeInsert(char *name, const char *fileInZipFile);
int    tapeIsInserted();
int    tapeSave(char *name, TapeFormat format);
void tapeLoadState();
void tapeSaveState();
void tapeRewindNextInsert(void);
UInt32 tapeGetLength();
UInt32 tapeGetCurrentPos();
void   tapeSetCurrentPos(int pos);
TapeContent* tapeGetContent(int* count);
TapeFormat   tapeGetFormat();
void tapeSetReadOnly(int readOnly);

/* TSX and WAV carry no byte stream, so the BIOS trap cannot serve them */
int tapeIsSignalOnly(void);

UInt8 tapeWrite(UInt8 value);
UInt8 tapeRead(UInt8* value);
UInt8 tapeReadHeader();
UInt8 tapeWriteHeader();

#endif
