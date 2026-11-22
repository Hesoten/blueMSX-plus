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
** free. base holds each logId's value as of tail, cur as of the newest entry. */
static Entry* ring;
static UInt64 head;
static UInt64 tail;
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
}

void rewindInputReset(void)
{
    head = 0;
    tail = 0;
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

    if (ring == NULL || (curValid[logId] && cur[logId] == value)) {
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

void rewindInputLoaded(UInt64 time)
{
    UInt64 i;

    if (ring == NULL) {
        return;
    }
    memcpy(cur, base, sizeof(cur));
    memcpy(curValid, baseValid, sizeof(curValid));
    for (i = tail; i < head && ring[i & LOG_MASK].time <= time; i++) {
        apply(&ring[i & LOG_MASK], cur, curValid);
    }
    head = i;
}

void rewindInputTrim(UInt64 oldestSnapshot)
{
    if (ring == NULL) {
        return;
    }
    while (tail < head && ring[tail & LOG_MASK].time <= oldestSnapshot) {
        apply(&ring[tail & LOG_MASK], base, baseValid);
        tail++;
    }
}

UInt64 rewindInputCoveredFrom(void)
{
    return coveredFrom;
}
