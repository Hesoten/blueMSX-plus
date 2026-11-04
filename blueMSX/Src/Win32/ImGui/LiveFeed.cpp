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
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "LiveFeed.h"
extern "C" {
#include "Emulator.h"
}

/* Three buffers per feed: the emulator thread fills back and swaps it with
** middle; the UI swaps middle with front, which only it reads. A snapshot
** is taken only after the UI asked for one, so at most one per drawn frame. */
struct Slot {
    void*        data;
    LiveFeedInfo info;
};

struct LiveFeed {
    size_t            size;
    LiveFeedCaptureFn capture;
    void*             ref;
    Slot              slot[3];
    int               back, middle, front;
    bool              wanted;   /* the UI asked for a snapshot */
    bool              fresh;    /* middle is newer than front */
    UInt32            seq;
    LiveFeed*         next;
};

/* Guards the list and every feed's back/middle/wanted/fresh. The emulator thread
** only tries it, to keep time; the UI thread waits, as a capture always ends.
** emulatorStop re-initialises it after killing the emulator thread. */
static SRWLOCK       lock = SRWLOCK_INIT;
static LiveFeed*     feeds;
static volatile LONG anyWanted;

static UInt64 hostTimeUs()
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&now);
    return (UInt64)(now.QuadPart / freq.QuadPart * 1000000 +
                    now.QuadPart % freq.QuadPart * 1000000 / freq.QuadPart);
}

static void tap(EmuTapPoint point)
{
    LiveFeed* f;
    if (point == EMU_TAP_STOPPED) {
        /* The emulator thread is gone, maybe killed holding the lock or mid-swap. */
        InitializeSRWLock(&lock);
        for (f = feeds; f; f = f->next) {
            f->back   = (f->front + 1) % 3;
            f->middle = (f->front + 2) % 3;
            f->fresh  = false;
            f->wanted = false;
            /* Nothing to show until the next run captures; seq keeps counting. */
            for (int i = 0; i < 3; i++) memset(&f->slot[i].info, 0, sizeof(f->slot[i].info));
        }
        anyWanted = 0;
        return;
    }
    if (!anyWanted || !emulatorIsEmuThread()) {
        return;
    }
    if (!TryAcquireSRWLockExclusive(&lock)) {
        return;
    }
    UInt64 now = hostTimeUs();
    for (f = feeds; f; f = f->next) {
        if (f->wanted) {
            Slot* s = &f->slot[f->back];
            int t;
            s->info.valid      = f->capture(s->data, f->ref) != 0;
            s->info.point      = point;
            s->info.hostTimeUs = now;
            s->info.seq        = ++f->seq;
            t = f->back; f->back = f->middle; f->middle = t;
            f->fresh  = true;
            f->wanted = false;
        }
    }
    anyWanted = 0;
    ReleaseSRWLockExclusive(&lock);
}

/* For opening and closing, which wait for a capture to end; waitMs < 0 waits
** without a limit. */
static bool lockUi(int waitMs)
{
    ULONGLONG start = GetTickCount64();
    while (!TryAcquireSRWLockExclusive(&lock)) {
        if (waitMs >= 0 && GetTickCount64() - start >= (ULONGLONG)waitMs) {
            return false;
        }
        Sleep(1);
    }
    return true;
}

static void freeFeed(LiveFeed* f)
{
    int i;
    for (i = 0; i < 3; i++) {
        free(f->slot[i].data);
    }
    free(f);
}

extern "C" LiveFeed* liveFeedOpen(size_t size, LiveFeedCaptureFn capture, void* ref)
{
    LiveFeed* f;
    int i;
    if (size == 0 || capture == NULL) {
        return NULL;
    }
    f = (LiveFeed*)calloc(1, sizeof(LiveFeed));
    if (f == NULL) {
        return NULL;
    }
    for (i = 0; i < 3; i++) {
        f->slot[i].data = calloc(1, size);
        if (f->slot[i].data == NULL) {
            freeFeed(f);
            return NULL;
        }
    }
    f->size    = size;
    f->capture = capture;
    f->ref     = ref;
    f->back    = 0;
    f->middle  = 1;
    f->front   = 2;
    if (!lockUi(50)) {
        freeFeed(f);
        return NULL;
    }
    f->next = feeds;
    feeds = f;
    ReleaseSRWLockExclusive(&lock);
    emulatorSetTap(tap);
    return f;
}

extern "C" void liveFeedClose(LiveFeed* feed)
{
    LiveFeed** p;
    if (feed == NULL) {
        return;
    }
    /* Waits without a limit: giving up would leave the tap capturing into freed memory. */
    (void)lockUi(-1);
    for (p = &feeds; *p; p = &(*p)->next) {
        if (*p == feed) {
            *p = feed->next;
            break;
        }
    }
    if (feeds == NULL) {
        emulatorSetTap(NULL);
    }
    ReleaseSRWLockExclusive(&lock);
    freeFeed(feed);
}

extern "C" const void* liveFeedLatest(LiveFeed* feed, LiveFeedInfo* info)
{
    Slot* s;
    if (feed == NULL) {
        if (info) memset(info, 0, sizeof(*info));
        return NULL;
    }
    /* Busy means a capture is running: keep the last one this time. */
    if (TryAcquireSRWLockExclusive(&lock)) {
        if (feed->fresh) {
            int t = feed->middle; feed->middle = feed->front; feed->front = t;
            feed->fresh = false;
        }
        feed->wanted = true;
        anyWanted = 1;
        ReleaseSRWLockExclusive(&lock);
    }
    s = &feed->slot[feed->front];
    if (info) {
        *info = s->info;
    }
    return s->info.seq != 0 ? s->data : NULL;
}
