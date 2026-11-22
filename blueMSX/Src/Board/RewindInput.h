/*****************************************************************************
**
** The input read by the machine over the rewind window, kept by board time.
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
#ifndef REWIND_INPUT_H
#define REWIND_INPUT_H

#include "MsxTypes.h"

/* Each value an input read gives is kept when it changes, with the 64-bit
** board time of the read, so the machine run again from a snapshot reads
** what it read the first time. Times are passed in by the board. */

/* Kept only while the board keeps rewind snapshots. */
void rewindInputCreate(void);
void rewindInputDestroy(void);

/* Forgets everything, for when no snapshot on the ring can be restored. */
void rewindInputReset(void);

/* What the read at time gives the machine: value itself while recording, the
** value read the first time while the machine runs again after a rewind. */
UInt8 rewindInputFilter(UInt64 time, UInt8 logId, UInt8 value);

/* After a snapshot taken at time has been loaded in place over the machine,
** which had run until from. What follows is replayed until it is back there. */
void rewindInputLoaded(UInt64 from, UInt64 time);

/* Non-zero while what runs at now is replayed. */
int rewindInputReplaying(UInt64 now);

/* Ends a replay at now and drops what was read after it, so the machine takes
** new input from here; returns how much board time was dropped, 0 if none. */
UInt64 rewindInputTakeOver(UInt64 now);

/* Folds what was read up to the oldest snapshot still kept into one record. */
void rewindInputTrim(UInt64 oldestSnapshot);

/* A snapshot taken before this time can no longer be run again with its
** input: the log ran full and dropped what was read after it. */
UInt64 rewindInputCoveredFrom(void);

#endif
