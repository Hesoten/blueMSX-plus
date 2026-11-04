/*****************************************************************************
**
** Emulator state snapshots for tool windows, taken on the emulator thread.
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
#ifndef LIVE_FEED_H
#define LIVE_FEED_H

#include <stddef.h>
#include "MsxTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LiveFeed LiveFeed;

/* Runs on the emulator thread at the EmuTapPoint point, also inside a V9990
** access: only reads device fields, never syncs. Fills dst (the size given to
** liveFeedOpen); 0: nothing to show; LIVE_FEED_NOT_NOW: ask again next point. */
typedef int (*LiveFeedCaptureFn)(void* dst, void* ref, int point);
#define LIVE_FEED_NOT_NOW (-1)

typedef struct {
    UInt32 seq;         /* bumps per snapshot; 0 before the first one */
    int    valid;       /* what the capture returned */
    int    point;       /* the EmuTapPoint it was taken at */
    UInt64 hostTimeUs;  /* when it was taken */
} LiveFeedInfo;

/* UI thread only. Open returns NULL on failure. */
LiveFeed*   liveFeedOpen(size_t size, LiveFeedCaptureFn capture, void* ref);
void        liveFeedClose(LiveFeed* feed);
/* The newest snapshot, valid until the next call, and a request for the
** next one: call once per drawn frame. NULL before the first snapshot. */
const void* liveFeedLatest(LiveFeed* feed, LiveFeedInfo* info);

#ifdef __cplusplus
}
#endif

#endif
