/*****************************************************************************
**
** Agent watchpoints.
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
#ifndef CONTROL_WATCH_H
#define CONTROL_WATCH_H

#include "ControlStopLog.h"
#include "ControlText.h"

/* The most places this socket's watch list keeps; a watch at one more is refused. */
#define CTRL_WATCH_LIST_MAX 64

/* Zero when the list is full. */
int ctrlWatchRemember(int devType, int where, int size, const char* condition,
                      Int64 value, const char* printed, int vramMode);

/* Non-zero when it was listed. */
int ctrlWatchForget(int devType, int where);
/* One line for each watch set over this socket, as it was set; returns how many. */
int ctrlWatchDescribeList(CtrlText* text);

/* After the status lines, for a stop a watchpoint caused; for a VRAM hit, also when the watch
** it fell in was set while video memory had another layout. */
void ctrlWatchDescribeStop(CtrlText* text, const CtrlStop* stop);

#endif
