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

enum ChipFamily { CHIP_V99X8, CHIP_FAMILY_COUNT };

#define CHIP_MAX 8

struct ChipEntry {
    UInt32 id;          /* 0 until the chip has been seen once */
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

/* Shared by the UI thread and the capture; only wantFrame is written by the
** UI once the feed is open. */
struct ChipFeedRef {
    volatile LONG wantFrame; /* nonzero: also copy the picture the chip drew; 1 without its samples */
    void*  scratch;         /* a state of every family fits; for names and ids */
    UInt32 knownId[CHIP_FAMILY_COUNT][CHIP_MAX];
    char   knownName[CHIP_FAMILY_COUNT][CHIP_MAX][32];
    int    knownCount[CHIP_FAMILY_COUNT];
    int    refreshIn;
};

size_t      chipFeedSize();
/* The state of the selected chip, of the family chip[selected].family. */
const void* chipFeedState(const ChipSnapshot* snap);
size_t      chipFeedStateSize(int family);
/* The selected chip's picture (a VdpFrameShot), or NULL
** when wantFrame was 0 or there is no picture (valid 0) to show. */
const void* chipFeedShot(const ChipSnapshot* snap);
/* For liveFeedOpen; runs on the emulator thread. */
int         chipFeedCapture(void* dst, void* ref);

/* Allocates the scratch once and keeps it for the process. */
bool        chipFeedRefInit(ChipFeedRef* ref);

#endif
