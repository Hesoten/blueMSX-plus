/*****************************************************************************
**
** File path helpers for control requests.
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
#ifndef CONTROL_PATH_H
#define CONTROL_PATH_H

#include "MsxTypes.h"

/* The file name after the last \ or /. */
const char* ctrlPathLeaf(const char* path);

/* The file name without its folder and its last extension, cut to fit out. */
void ctrlPathLeafStem(const char* path, char* out, int outLen);

/* C:\..., C:/... or \\server\...; anything else opens against the emulator's folder. */
int ctrlPathIsFull(const char* path);

/* The last write time of a file, in 100 ns units; 0 when it is not there. */
Int64 ctrlFileWriteTime(const char* path);
/* The time now, in the units of ctrlFileWriteTime. */
Int64 ctrlFileTimeNow(void);

#endif
