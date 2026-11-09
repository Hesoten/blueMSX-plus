/*****************************************************************************
**
** Snapshots of the video chip shown in the VDP Viewer.
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
#ifndef CHIP_FEED_H
#define CHIP_FEED_H

#include <stddef.h>
#include <windows.h>
#include "MsxTypes.h"

enum ChipFamily { CHIP_V99X8, CHIP_V9990, CHIP_FAMILY_COUNT };

#define CHIP_MAX 8

struct ChipEntry {
    UInt32 id;          /* the selected entry's chip instance; 0 for the others */
    int    family;      /* ChipFamily */
    int    index;       /* within the family */
    char   name[48];
};

/* The head of every snapshot; the selected chip's state follows at
** chipFeedState(). */
struct ChipSnapshot {
    int       count;
    ChipEntry chip[CHIP_MAX];
    int       selected;  /* into chip[], or -1 */
    int       shot;      /* 1: chipFeedShot() holds a picture capture */
};

/* Shared by the UI thread and the capture; only want and wantFrame are
** written by the UI once the feed is open. */
struct ChipFeedRef {
    /* The chip to show, as chipWant() gives it, with CHIP_WANT_AUTO when the capture followed it
    ** onto the main screen; 0 for none. None or a missing one: the first. */
    volatile LONG want;
    volatile LONG wantFrame; /* nonzero: also copy the picture the chip drew; 1 without its samples */
    bool   shownV9990;       /* a V9990 was on the main screen at the last capture; the capture's own */
    int    framesWaiting;    /* VDP frames passed up for the chip's own point since it last came */
};

/* A chip by family and index: it stays the same chip across a reset or a state load. */
inline LONG chipWant(int family, int index)
{
    return family * CHIP_MAX + index + 1;
}
#define CHIP_WANT_AUTO 0x10000

size_t      chipFeedSize();
/* The state of the selected chip, of the family chip[selected].family. */
const void* chipFeedState(const ChipSnapshot* snap);
size_t      chipFeedStateSize(int family);
/* The selected chip's picture (VdpFrameShot or V9990FrameShot), or NULL
** when wantFrame was 0 or there is no picture (valid 0) to show. */
const void* chipFeedShot(const ChipSnapshot* snap);
/* For liveFeedOpen; runs on the emulator thread. A V9990 is copied at its
** last displayed line, a V99x8 at its end of active display; either also while paused. */
int         chipFeedCapture(void* dst, void* ref, int point);

/* Forgets the chip asked for, so the next capture takes a V9990 on the main screen,
** else the first chip. UI thread, before the feed opens. */
void        chipFeedRefReset(ChipFeedRef* ref);

#endif
