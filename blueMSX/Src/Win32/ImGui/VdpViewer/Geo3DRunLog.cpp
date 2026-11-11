/*****************************************************************************
**
** Log of geo3d RUNs as each starts, grouped by the screen update drawn.
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
#define NOMINMAX
#include <windows.h>
#include <utility>
#include "Geo3DRunLog.h"
#include "LiveFeed.h"
extern "C" {
#include "Emulator.h"
#include "Geo3D.h"
#include "Geo3DDevice.h"
}

namespace {

/* What a cycle keeps of its newest RUNs. */
const int maxRuns = 64, maxItems = 8192, maxCmds = 16384;

}

void geo3dRunAppend(Geo3DRunMap& runs, std::vector<Geo3DRunCycle>& cycles, const Geo3DRunPtr& e,
                    int page, bool gap, bool pair)
{
    UInt32 seq = e->run.seq;
    runs[seq] = e;
    if (!cycles.empty() && !cycles.back().closed) {
        /* Under flat interlace the pair's two pages are one picture. */
        bool same = cycles.back().page == page || (pair && (page >> 1) == (cycles.back().page >> 1));
        if (!gap && same) cycles.back().last = seq;
        else cycles.back().closed = true;
    }
    if (cycles.empty() || cycles.back().closed) {
        Geo3DRunCycle c;
        c.page = page;
        c.first = c.last = seq;
        c.gap = gap;
        cycles.push_back(c);
    }
}

void geo3dRunTrim(Geo3DRunMap& runs, std::vector<Geo3DRunCycle>& cycles, UInt32 pinSeq, bool latestOnly)
{
    /* A cycle keeps its newest RUNs up to the caps (a page drawn on while shown grows);
    ** past the commands' cap its older RUNs keep their items only, as the picture needs no more. */
    for (size_t i = latestOnly && !cycles.empty() ? cycles.size() - 1 : 0; i < cycles.size(); i++) {
        Geo3DRunCycle& c = cycles[i];
        int n = 0, items = 0, cmds = 0;
        UInt32 cut = 0;
        for (auto at = runs.upper_bound(c.last); at != runs.begin();) {
            --at;
            if (at->first < c.first) break;
            n++;
            items += (int)at->second->items.size();
            if (n > maxRuns || items > maxItems) {
                cut = at->first;
                break;
            }
            cmds += (int)at->second->cmds.size();
            if (cmds > maxCmds && !at->second->cmdsDropped) {
                std::shared_ptr<Geo3DRunEntry> d = std::make_shared<Geo3DRunEntry>(*at->second);
                d->dropCmds();
                at->second = d;
            }
        }
        if (cut == 0) continue;
        runs.erase(runs.lower_bound(c.first), runs.upper_bound(cut));
        c.first = cut + 1;
        c.gap = true;
    }
    /* Kept: the open cycle, the last closed one of each page, and the pinned one. */
    unsigned pagesSeen = 0;
    for (int i = (int)cycles.size() - 1; i >= 0; i--) {
        Geo3DRunCycle& c = cycles[i];
        bool keep = !c.closed || (pinSeq >= c.first && pinSeq <= c.last);
        if (c.closed && c.page < 32 && !((pagesSeen >> c.page) & 1)) {
            pagesSeen |= 1u << c.page;
            keep = true;
        }
        if (keep) continue;
        runs.erase(runs.lower_bound(c.first), runs.upper_bound(c.last));
        cycles.erase(cycles.begin() + i);
    }
}

/* The emulation thread's: what it folded so far, and the copy it publishes from. */
struct Geo3DRunFold {
    Geo3DRunMap                runs;
    std::vector<Geo3DRunCycle> cycles;
    Geo3DRunLogView            stage;
};

struct Geo3DRunLog {
    SRWLOCK          pubLock;
    Geo3DRunLogView* pub;
    bool             armed;         /* UI thread */
    /* From the UI thread; gen changes with each arming. */
    volatile LONG    on, gen, cfgCartridge, cfgRows, cfgPair;
    /* The emulation thread's; busy while it changes or copies fold, publishing while pub. */
    Geo3DRunFold*    fold;
    volatile LONG    busy, publishing;
    LONG             seenGen;
    int              cartridge;
    UInt32           epoch;
    int              rows;
    bool             pair;
    UInt32           lastSeq;
    bool             gapNext;
    bool             dirty;
    UInt32           version;
    std::vector<Geo3DDebugItem> itemRoom;
    std::vector<Geo3DDebugCmd>  cmdRoom;
};

/* The open log the sink folds into; the sink never waits on the lock, the UI takes it
** only to open and close. */
static SRWLOCK      lifeLock = SRWLOCK_INIT;
static Geo3DRunLog* lifeLog;

/* Forgets the fold; the RUN after it starts a cycle with a gap. */
static void clearFold(Geo3DRunLog* l)
{
    if (l->fold != NULL) {
        l->fold->runs.clear();
        l->fold->cycles.clear();
    }
    l->lastSeq = 0;
    l->gapNext = true;
    l->dirty = true;
}

static void fold(Geo3DRunLog* l, int cartridge, UInt32 epoch, UInt32 seq, Geo3D* g, UInt32 startTime)
{
    if (cartridge != (int)l->cfgCartridge) {
        return;
    }
    if (l->fold == NULL) {
        l->fold = new Geo3DRunFold();
    }
    Geo3DRunFold& f = *l->fold;
    LONG gen = l->gen;
    int rows = (int)l->cfgRows;
    bool pair = l->cfgPair != 0;
    /* A reset or state load numbers the RUNs again; another mode or pairing moves the pages. */
    if (gen != l->seenGen || cartridge != l->cartridge || epoch != l->epoch || rows != l->rows ||
        pair != l->pair || (l->lastSeq != 0 && seq <= l->lastSeq)) {
        clearFold(l);
        l->seenGen = gen;
        l->cartridge = cartridge;
        l->epoch = epoch;
        l->rows = rows;
        l->pair = pair;
    }
    Geo3DDebugRun h;
    if (rows <= 0 || !geo3dDebugCopyRun(g, seq, &h, l->itemRoom.data(), (int)l->itemRoom.size(),
                                        l->cmdRoom.data(), (int)l->cmdRoom.size())) {
        l->gapNext = true;
        return;
    }
    h.startTime = startTime;
    std::shared_ptr<Geo3DRunEntry> e = std::make_shared<Geo3DRunEntry>();
    e->run = h;
    e->items.assign(l->itemRoom.begin(), l->itemRoom.begin() + h.itemCount);
    e->cmds.assign(l->cmdRoom.begin(), l->cmdRoom.begin() + h.cmdCount);
    e->settled = true;
    bool gap = l->gapNext || seq != l->lastSeq + 1;
    l->gapNext = false;
    l->lastSeq = seq;
    geo3dRunAppend(f.runs, f.cycles, e, (h.ypage % rows) / 256, gap, pair);
    geo3dRunTrim(f.runs, f.cycles, 0, true);
    l->dirty = true;
}

static void sink(int cartridge, UInt32 epoch, UInt32 seq, Geo3D* g, UInt32 startTime)
{
    if (!TryAcquireSRWLockShared(&lifeLock)) {
        return;
    }
    Geo3DRunLog* l = lifeLog;
    if (l != NULL && l->on) {
        l->busy = 1;
        try {
            fold(l, cartridge, epoch, seq, g, startTime);
        }
        catch (...) {
            clearFold(l);
        }
        l->busy = 0;
    }
    ReleaseSRWLockShared(&lifeLock);
}

/* The fold as it is now into pub; when the UI holds it, at the next tap. */
static void publish(Geo3DRunLog* l)
{
    Geo3DRunLogView& v = l->fold->stage;
    v.cartridge = l->cartridge;
    v.epoch = l->epoch;
    v.rows = l->rows;
    v.runs = l->fold->runs;
    v.cycles = l->fold->cycles;
    v.version = ++l->version;
    if (v.version == 0) v.version = ++l->version;
    /* Swaps that cannot throw, so the lock is always let go. */
    if (l->pub != NULL && TryAcquireSRWLockExclusive(&l->pubLock)) {
        Geo3DRunLogView& pub = *l->pub;
        l->publishing = 1;
        std::swap(pub.version, v.version);
        std::swap(pub.cartridge, v.cartridge);
        std::swap(pub.epoch, v.epoch);
        std::swap(pub.rows, v.rows);
        pub.runs.swap(v.runs);
        pub.cycles.swap(v.cycles);
        l->publishing = 0;
        ReleaseSRWLockExclusive(&l->pubLock);
        l->dirty = false;
        v.runs.clear();
        v.cycles.clear();
    }
}

/* The emulator thread is gone, maybe killed while it changed the fold or pub: those are
** let go of without being walked, and an empty pub replaces a broken one. */
static void stopped(Geo3DRunLog* l)
{
    InitializeSRWLock(&l->pubLock);
    InitializeSRWLock(&lifeLock);
    if (!l->busy) {
        delete l->fold;
    }
    l->fold = NULL;
    l->busy = 0;
    if (l->publishing) {
        l->publishing = 0;
        l->pub = NULL;
        try {
            l->pub = new Geo3DRunLogView();
            l->pub->version = ++l->version;
        }
        catch (...) {
        }
    }
    l->lastSeq = 0;
    l->gapNext = true;
    l->dirty = false;
}

static void drain(void* ref, int point)
{
    Geo3DRunLog* l = (Geo3DRunLog*)ref;
    if (point == EMU_TAP_STOPPED) {
        stopped(l);
        return;
    }
    /* Mid-replay it only folds: what the tab shows changes at the stop. */
    if ((point != EMU_TAP_FRAME && point != EMU_TAP_IDLE) || l->fold == NULL) {
        return;
    }
    l->busy = 1;
    try {
        if (!l->on && !l->fold->runs.empty()) {
            clearFold(l);
        }
        if (l->dirty) {
            publish(l);
        }
    }
    catch (...) {
        l->fold->stage.runs.clear();
        l->fold->stage.cycles.clear();
    }
    l->busy = 0;
}

static void freeLog(Geo3DRunLog* l)
{
    if (l != NULL) {
        delete l->fold;
        delete l->pub;
        delete l;
    }
}

Geo3DRunLog* geo3dRunLogOpen()
{
    Geo3DRunLog* l = NULL;
    try {
        l = new Geo3DRunLog();
        l->pub = new Geo3DRunLogView();
        l->itemRoom.resize(GEO3D_DEBUG_CORE_ITEMS);
        l->cmdRoom.resize(GEO3D_DEBUG_CORE_CMDS);
    }
    catch (...) {
        freeLog(l);
        return NULL;
    }
    InitializeSRWLock(&l->pubLock);
    l->gapNext = true;
    AcquireSRWLockExclusive(&lifeLock);
    bool taken = lifeLog != NULL;
    if (!taken) lifeLog = l;
    ReleaseSRWLockExclusive(&lifeLock);
    if (taken || !liveFeedAddDrain(drain, l)) {
        if (!taken) {
            AcquireSRWLockExclusive(&lifeLock);
            lifeLog = NULL;
            ReleaseSRWLockExclusive(&lifeLock);
        }
        freeLog(l);
        return NULL;
    }
    return l;
}

void geo3dRunLogClose(Geo3DRunLog* l)
{
    if (l == NULL) {
        return;
    }
    liveFeedRemoveDrain(drain, l);
    geo3dRunLogArm(l, false);
    AcquireSRWLockExclusive(&lifeLock);
    lifeLog = NULL;
    ReleaseSRWLockExclusive(&lifeLock);
    freeLog(l);
}

void geo3dRunLogArm(Geo3DRunLog* l, bool on)
{
    if (l == NULL || l->armed == on) {
        return;
    }
    l->armed = on;
    if (on) InterlockedIncrement(&l->gen);
    InterlockedExchange(&l->on, on ? 1 : 0);
    geo3dDebugSetRunSink(on ? sink : NULL);
}

bool geo3dRunLogArmed(const Geo3DRunLog* l)
{
    return l != NULL && l->armed;
}

void geo3dRunLogConfig(Geo3DRunLog* l, int cartridge, int rows, bool pair)
{
    if (l == NULL) {
        return;
    }
    if (l->cfgCartridge != cartridge) InterlockedExchange(&l->cfgCartridge, cartridge);
    if (l->cfgRows != rows) InterlockedExchange(&l->cfgRows, rows);
    if (l->cfgPair != (pair ? 1 : 0)) InterlockedExchange(&l->cfgPair, pair ? 1 : 0);
}

bool geo3dRunLogTake(Geo3DRunLog* l, Geo3DRunLogView* out)
{
    bool taken = false;
    if (l == NULL || !TryAcquireSRWLockShared(&l->pubLock)) {
        return false;
    }
    const Geo3DRunLogView* pub = l->pub;
    if (pub != NULL && pub->version != out->version) {
        try {
            out->cartridge = pub->cartridge;
            out->epoch = pub->epoch;
            out->rows = pub->rows;
            out->runs = pub->runs;
            out->cycles = pub->cycles;
            out->version = pub->version;
            taken = true;
        }
        catch (...) {
            out->version = 0;
        }
    }
    ReleaseSRWLockShared(&l->pubLock);
    return taken;
}
