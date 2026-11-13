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
#include <string.h>
#include "ChipFeed.h"
#include "LiveFeed.h"
extern "C" {
#include "Emulator.h"
#include "VdpDebug.h"
#include "V9990.h"
}

struct ChipProvider {
    int         family;
    size_t      stateSize;
    int         (*count)(void);
    int         (*capture)(int index, void* dst);
    UInt32      (*idOf)(const void* st);
    const char* (*name)(int index);
    int         (*cartridge)(int index);
    int         ownPoint;   /* where a cartridge chip of the family is copied */
    size_t      shotSize;
    int         (*captureShot)(int index, void* dst, int pixels);
};

static int    vdpCapture(int i, void* d)      { return vdpDebugCapture(i, (VdpDebugState*)d); }
static int    vdpShot(int i, void* d, int p)  { return vdpDebugCaptureFrame(i, (VdpFrameShot*)d, p); }
static UInt32 vdpId(const void* s)            { return ((const VdpDebugState*)s)->id; }
static int    v9990Capture(int i, void* d)    { return v9990DebugCapture(i, (V9990DebugState*)d); }
static int    v9990Shot(int i, void* d, int)  { return v9990DebugCaptureFrame(i, (V9990FrameShot*)d); }
static UInt32 v9990Id(const void* s)          { return ((const V9990DebugState*)s)->id; }
static const char* v9990Name(int)             { return "V9990"; }
static int    v9990Cart(int)                  { return 1; }

static const ChipProvider providers[CHIP_FAMILY_COUNT] = {
    { CHIP_V99X8, sizeof(VdpDebugState),   vdpDebugCount,   vdpCapture,   vdpId,   vdpDebugName,
      vdpDebugIsCartridge, EMU_TAP_VDP_SECONDARY_SHOWN, sizeof(VdpFrameShot), vdpShot },
    { CHIP_V9990, sizeof(V9990DebugState), v9990DebugCount, v9990Capture, v9990Id, v9990Name,
      v9990Cart, EMU_TAP_V9990_SHOWN, sizeof(V9990FrameShot), v9990Shot },
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

void chipFeedRefReset(ChipFeedRef* ref)
{
    InterlockedExchange(&ref->want, 0);
    ref->shownChip     = 0;
    ref->framesWaiting = 0;
}

/* A V9990's points come at most two V9990 frames apart, less than three VDP frames; a VDP's,
** the machine's or a cartridge's, every frame unless a line count change loses its vint. */
#define FRAMES_WAITED_MAX 4

/* Only the selected chip is copied. */
int chipFeedCapture(void* dst, void* refp, int point)
{
    ChipFeedRef*  r = (ChipFeedRef*)refp;
    ChipSnapshot* snap = (ChipSnapshot*)dst;
    void*         state = (char*)dst + headSize();
    int           counts[CHIP_FAMILY_COUNT];
    bool          atChip = point == EMU_TAP_V9990_SHOWN || point == EMU_TAP_VDP_SHOWN ||
                           point == EMU_TAP_VDP_SECONDARY_SHOWN;
    int           f, i, n = 0;

    snap->shot = 0;
    for (f = 0; f < CHIP_FAMILY_COUNT; f++) {
        counts[f] = providers[f].count();
        if (counts[f] > CHIP_MAX) counts[f] = CHIP_MAX;
    }

    /* A cartridge chip coming onto the main screen is shown, as the main window switches to it,
    ** unless picked by hand; then it holds. One followed gives way to the machine's VDP when
    ** it leaves the main screen, as the main window does. */
    int  superimposed;
    int  shownVdp = vdpDebugShownIndex();
    LONG shown = 0;
    if (counts[CHIP_V9990] > 0 && v9990GetShownMode(&superimposed) != V9990_MODE_NONE) {
        shown = chipWant(CHIP_V9990, 0);
    }
    else if (shownVdp > 0 && shownVdp < counts[CHIP_V99X8] && vdpDebugIsCartridge(shownVdp)) {
        shown = chipWant(CHIP_V99X8, shownVdp);
    }
    if (shown != 0 && shown != r->shownChip && r->want != shown) {
        InterlockedExchange(&r->want, shown | CHIP_WANT_AUTO);
    }
    else if (shown == 0 && r->shownChip != 0) {
        InterlockedCompareExchange(&r->want, 0, r->shownChip | CHIP_WANT_AUTO);
    }
    r->shownChip = shown;

    LONG want = r->want & ~CHIP_WANT_AUTO;
    int  sel = -1;
    for (f = 0; f < CHIP_FAMILY_COUNT; f++) {
        for (i = 0; i < counts[f] && n < CHIP_MAX; i++, n++) {
            ChipEntry*  e = &snap->chip[n];
            const char* name = providers[f].name(i);
            e->id     = 0;
            e->family = f;
            e->index  = i;
            e->cartridge = providers[f].cartridge(i);
            strncpy(e->name, name != NULL ? name : "", sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = 0;
            if (sel < 0 && chipWant(f, i) == want) sel = n;
        }
    }
    snap->count = n;
    if (sel < 0) sel = 0;
    /* A chip is copied at its own end of display. Should that point stop coming, the VDP
    ** frame end copies it after FRAMES_WAITED_MAX frames, so the copies never stop. */
    bool cart = n > 0 && snap->chip[sel].cartridge;
    int  own  = cart ? providers[snap->chip[sel].family].ownPoint : EMU_TAP_VDP_SHOWN;
    if (atChip ? point != own : point == EMU_TAP_FRAME && ++r->framesWaiting < FRAMES_WAITED_MAX) {
        return LIVE_FEED_NOT_NOW;
    }
    r->framesWaiting = 0;
    if (n == 0) {
        snap->selected = -1;
        return 0;
    }
    ChipEntry* e = &snap->chip[sel];
    if (!providers[e->family].capture(e->index, state)) {
        snap->selected = -1;
        return 0;
    }
    e->id = providers[e->family].idOf(state);
    snap->selected = sel;
    if (r->wantFrame && providers[e->family].captureShot != NULL) {
        snap->shot = providers[e->family].captureShot(e->index, (char*)dst + shotOffset(), r->wantFrame >= 2) ? 1 : 0;
    }
    return 1;
}
