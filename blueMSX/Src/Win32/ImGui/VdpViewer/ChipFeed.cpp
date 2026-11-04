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
#include <stdlib.h>
#include <string.h>
#include "ChipFeed.h"
extern "C" {
#include "VdpDebug.h"
}

struct ChipProvider {
    int         family;
    size_t      stateSize;
    int         (*count)(void);
    int         (*capture)(int index, void* dst);
    UInt32      (*idOf)(const void* st);
    const char* (*nameOf)(const void* st);
    size_t      shotSize;
    int         (*captureShot)(int index, void* dst, int pixels);
};

static int    vdpCapture(int i, void* d)      { return vdpDebugCapture(i, (VdpDebugState*)d); }
static int    vdpShot(int i, void* d, int p)  { return vdpDebugCaptureFrame(i, (VdpFrameShot*)d, p); }
static UInt32 vdpId(const void* s)            { return ((const VdpDebugState*)s)->id; }
static const char* vdpName(const void* s)     { return ((const VdpDebugState*)s)->name; }

static const ChipProvider providers[CHIP_FAMILY_COUNT] = {
    { CHIP_V99X8, sizeof(VdpDebugState),   vdpDebugCount,   vdpCapture,   vdpId,   vdpName,
      sizeof(VdpFrameShot), vdpShot },
};

static size_t headSize()
{
    return (sizeof(ChipSnapshot) + 63) & ~(size_t)63;
}

static size_t maxStateSize()
{
    size_t m = 0;
    for (int f = 0; f < CHIP_FAMILY_COUNT; f++) {
        if (providers[f].stateSize > m) m = providers[f].stateSize;
    }
    return m;
}

static size_t shotOffset()
{
    return headSize() + ((maxStateSize() + 63) & ~(size_t)63);
}

size_t chipFeedSize()
{
    size_t m = 0;
    for (int f = 0; f < CHIP_FAMILY_COUNT; f++) {
        if (providers[f].shotSize > m) m = providers[f].shotSize;
    }
    return shotOffset() + m;
}

const void* chipFeedShot(const ChipSnapshot* snap)
{
    return snap->shot ? (const char*)snap + shotOffset() : NULL;
}

size_t chipFeedStateSize(int family)
{
    return family >= 0 && family < CHIP_FAMILY_COUNT ? providers[family].stateSize : 0;
}

const void* chipFeedState(const ChipSnapshot* snap)
{
    return (const char*)snap + headSize();
}

bool chipFeedRefInit(ChipFeedRef* ref)
{
    if (ref->scratch == NULL) {
        ref->scratch = malloc(maxStateSize());
    }
    return ref->scratch != NULL;
}

static void remember(ChipFeedRef* r, int f, int i, const void* st)
{
    r->knownId[f][i] = providers[f].idOf(st);
    strncpy(r->knownName[f][i], providers[f].nameOf(st), sizeof(r->knownName[f][i]) - 1);
    r->knownName[f][i][sizeof(r->knownName[f][i]) - 1] = 0;
}

/* Only the selected chip is copied each time; the others are copied to the
** scratch now and then to learn their ids, and when their count changes. */
int chipFeedCapture(void* dst, void* refp)
{
    ChipFeedRef*  r = (ChipFeedRef*)refp;
    ChipSnapshot* snap = (ChipSnapshot*)dst;
    void*         state = (char*)dst + headSize();
    int           counts[CHIP_FAMILY_COUNT];
    bool          changed = false;
    bool          refresh;
    int           f, i, n = 0;

    snap->shot = 0;
    for (f = 0; f < CHIP_FAMILY_COUNT; f++) {
        counts[f] = providers[f].count();
        if (counts[f] > CHIP_MAX) counts[f] = CHIP_MAX;
        if (counts[f] != r->knownCount[f]) changed = true;
    }
    refresh = changed || r->refreshIn <= 1;
    if (refresh) {
        r->refreshIn = 60;
        for (f = 0; f < CHIP_FAMILY_COUNT; f++) {
            r->knownCount[f] = counts[f];
            for (i = 0; i < counts[f]; i++) {
                if (providers[f].capture(i, r->scratch)) {
                    remember(r, f, i, r->scratch);
                }
            }
        }
    }

    int  sel = 0;
    for (f = 0; f < CHIP_FAMILY_COUNT; f++) {
        for (i = 0; i < counts[f] && n < CHIP_MAX; i++, n++) {
            ChipEntry* e = &snap->chip[n];
            e->id     = r->knownId[f][i];
            e->family = f;
            e->index  = i;
            strncpy(e->name, r->knownName[f][i], sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = 0;
        }
    }
    snap->count = n;
    if (!refresh) r->refreshIn--;
    if (n == 0) {
        snap->selected = -1;
        return 0;
    }
    ChipEntry* e = &snap->chip[sel];
    if (!providers[e->family].capture(e->index, state)) {
        snap->selected = -1;
        return 0;
    }
    remember(r, e->family, e->index, state);
    e->id = r->knownId[e->family][e->index];
    snap->selected = sel;
    if (r->wantFrame && providers[e->family].captureShot != NULL) {
        snap->shot = providers[e->family].captureShot(e->index, (char*)dst + shotOffset(), r->wantFrame >= 2) ? 1 : 0;
    }
    return 1;
}
