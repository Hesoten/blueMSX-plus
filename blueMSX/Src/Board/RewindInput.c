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
#include "RewindInput.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    UInt64 time;
    UInt8  logId;
    UInt8  value;
} Entry;

#define LOG_BITS 18
#define LOG_SIZE ((UInt64)1 << LOG_BITS)
#define LOG_MASK (LOG_SIZE - 1)

/* Entries are numbered from the start; tail is the oldest kept, head the next
** free. base holds each logId's value as of tail; cur as of the newest entry,
** or while replaying as of cursor, the next entry the machine has not met. */
static Entry* ring;
static UInt64 head;
static UInt64 tail;
static UInt64 cursor;
/* Replaying until the machine is back at the time the rewind started from. */
static int    replaying;
static UInt64 frontier;
static UInt64 coveredFrom;
static UInt8  base[256];
static UInt8  baseValid[256];
static UInt8  cur[256];
static UInt8  curValid[256];

void rewindInputCreate(void)
{
    if (ring == NULL) {
        ring = malloc((size_t)LOG_SIZE * sizeof(Entry));
    }
    rewindInputReset();
}

void rewindInputDestroy(void)
{
    free(ring);
    ring = NULL;
    rewindInputReset();
}

void rewindInputReset(void)
{
    head = 0;
    tail = 0;
    cursor = 0;
    replaying = 0;
    coveredFrom = 0;
    memset(baseValid, 0, sizeof(baseValid));
    memset(curValid, 0, sizeof(curValid));
}

static void apply(const Entry* e, UInt8* values, UInt8* valid)
{
    values[e->logId] = e->value;
    valid[e->logId] = 1;
}

UInt8 rewindInputFilter(UInt64 time, UInt8 logId, UInt8 value)
{
    Entry* e;

    if (ring == NULL) {
        return value;
    }
    if (replaying) {
        while (cursor < head && ring[cursor & LOG_MASK].time < time) {
            apply(&ring[cursor++ & LOG_MASK], cur, curValid);
        }
        /* Reads at one moment keep their order: each takes its own entry of that moment. */
        if (cursor < head && ring[cursor & LOG_MASK].time == time &&
            ring[cursor & LOG_MASK].logId == logId) {
            apply(&ring[cursor++ & LOG_MASK], cur, curValid);
        }
        if (time < frontier) {
            return curValid[logId] ? cur[logId] : value;
        }
        while (cursor < head) {
            apply(&ring[cursor++ & LOG_MASK], cur, curValid);
        }
        replaying = 0;
    }
    if (curValid[logId] && cur[logId] == value) {
        return value;
    }
    if (head - tail == LOG_SIZE) {
        e = &ring[tail & LOG_MASK];
        apply(e, base, baseValid);
        coveredFrom = e->time;
        tail++;
    }
    e = &ring[head++ & LOG_MASK];
    e->time  = time;
    e->logId = logId;
    e->value = value;
    apply(e, cur, curValid);
    return value;
}

void rewindInputLoaded(UInt64 from, UInt64 time)
{
    if (ring == NULL) {
        return;
    }
    memcpy(cur, base, sizeof(cur));
    memcpy(curValid, baseValid, sizeof(curValid));
    for (cursor = tail; cursor < head && ring[cursor & LOG_MASK].time <= time; cursor++) {
        apply(&ring[cursor & LOG_MASK], cur, curValid);
    }
    /* A rewind made during the replay of an earlier one goes on to the same end. */
    if (!replaying || from >= frontier) {
        frontier = from;
    }
    replaying = 1;
}

int rewindInputReplaying(UInt64 now)
{
    return ring != NULL && replaying && now < frontier;
}

UInt64 rewindInputTakeOver(UInt64 now)
{
    UInt64 dropped;

    if (ring == NULL || !replaying) {
        return 0;
    }
    /* Past the end with no read since: nothing recorded lies ahead. */
    if (now >= frontier) {
        while (cursor < head) {
            apply(&ring[cursor++ & LOG_MASK], cur, curValid);
        }
        replaying = 0;
        return 0;
    }
    /* What is left was read after now; the reads that follow are new input. */
    dropped = frontier - now;
    head = cursor;
    replaying = 0;
    return dropped;
}

void rewindInputTrim(UInt64 oldestSnapshot)
{
    if (ring == NULL) {
        return;
    }
    while (tail < head && ring[tail & LOG_MASK].time <= oldestSnapshot) {
        if (replaying && cursor == tail) {
            apply(&ring[cursor++ & LOG_MASK], cur, curValid);
        }
        apply(&ring[tail & LOG_MASK], base, baseValid);
        tail++;
    }
}

UInt64 rewindInputCoveredFrom(void)
{
    return coveredFrom;
}
