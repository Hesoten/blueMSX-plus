/*****************************************************************************
**
** Agent Control clients, the access level, and what each client did.
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
#include "ControlClient.h"
#include "ArchEvent.h"
#include "ArchTimer.h"
#include "RaceTest.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Reads closer together than this fold into one line... */
#define ACT_GAP_MS 150
/* ...but a line never spans more than this. */
#define ACT_SPAN_MS 1000

/* Static: the append path runs next to the emulation thread and must not allocate. */
static CtrlActivityEntry ring[CTRL_ACT_RING];
static UInt32 nextSeq = 1;

static CtrlClientInfo clients[CTRL_CLIENT_MAX];
static int        clientUsed[CTRL_CLIENT_MAX];
static CtrlClient nextClientId = 1;
/* Written only by the thread that serves agents. */
static CtrlClient currentClient;
/* That thread's too: the newest line the current request started, 0 for none. */
static UInt32     requestSeq;
/* The newest line ctrlActivityNoteFailed started, which a repeat of it may fold into. */
static UInt32     failedSeq;
static volatile int blockAll;
/* A leaf lock over the ring and the clients: the window's thread revokes and reads them too. */
static void*      clientLock;
static void     (*volatile listener)(void);

static void lock(void)
{
    archSemaphoreWait(clientLock, -1);
}

static void unlock(void)
{
    archSemaphoreSignal(clientLock);
}

void ctrlClientCreate(void)
{
    if (clientLock == NULL) {
        clientLock = archSemaphoreCreate(1);
    }
    memset(ring, 0, sizeof(ring));
    memset(clients, 0, sizeof(clients));
    memset(clientUsed, 0, sizeof(clientUsed));
    nextSeq = 1;
    nextClientId = 1;
    currentClient = CTRL_CLIENT_NONE;
    requestSeq = 0;
    failedSeq = 0;
}

void ctrlActivitySetListener(void (*changed)(void))
{
    listener = changed;
}

/* Copies at most size - 1 bytes, never ending inside a UTF-8 character. */
static void copyText(char* to, size_t size, const char* from)
{
    size_t length = strlen(from);

    if (length >= size) {
        length = size - 1;
        while (length > 0 && ((unsigned char)from[length] & 0xC0) == 0x80) {
            length--;
        }
    }
    memcpy(to, from, length);
    to[length] = 0;
}

static CtrlClientInfo* findClient(CtrlClient client)
{
    int i;

    for (i = 0; i < CTRL_CLIENT_MAX; i++) {
        if (clientUsed[i] && clients[i].client == client) {
            return &clients[i];
        }
    }
    return NULL;
}

static void appendPlain(CtrlClient client, int verb, const char* detail);

CtrlClient ctrlClientOpen(const char* label)
{
    CtrlClient opened = CTRL_CLIENT_NONE;
    int i;

    lock();
    for (i = 0; i < CTRL_CLIENT_MAX && !blockAll; i++) {
        if (!clientUsed[i]) {
            memset(&clients[i], 0, sizeof(CtrlClientInfo));
            clients[i].client   = nextClientId++;
            if (label != NULL) {
                copyText(clients[i].label, CTRL_CLIENT_LABEL, label);
            }
            clientUsed[i] = 1;
            opened = clients[i].client;
            appendPlain(opened, CTRL_ACT_CONNECT, clients[i].label);
            break;
        }
    }
    unlock();
    return opened;
}

void ctrlClientClose(CtrlClient client)
{
    CtrlClientInfo* info;

    lock();
    info = findClient(client);
    if (info != NULL) {
        appendPlain(client, CTRL_ACT_DISCONNECT, info->label);
        clientUsed[info - clients] = 0;
    }
    unlock();
}

int ctrlClientAllowed(CtrlClient client)
{
    CtrlClientInfo* info;
    int allowed;

    if (blockAll) {
        return 0;
    }
    if (client == CTRL_CLIENT_NONE) {
        return 1;
    }
    lock();
    info = findClient(client);
    allowed = info != NULL && !info->revoked;
    unlock();
    return allowed;
}

/* With the lock held. */
static void revoke(CtrlClientInfo* info)
{
    if (!info->revoked) {
        info->revoked = 1;
        appendPlain(info->client, CTRL_ACT_REVOKED, info->label);
    }
}

void ctrlClientBlockAll(int block)
{
    int i;

    lock();
    blockAll = block;
    for (i = 0; i < CTRL_CLIENT_MAX && block; i++) {
        if (clientUsed[i]) {
            revoke(&clients[i]);
        }
    }
    unlock();
}

int ctrlClientAllBlocked(void)
{
    return blockAll;
}

static int gateLevel = CTRL_GATE_OFF;

int ctrlGateLevel(void)
{
    return gateLevel;
}

void ctrlGateSetLevel(int level)
{
    gateLevel = level;
}

const char* ctrlGateLevelText(int level)
{
    switch (level) {
    case CTRL_GATE_READONLY: return "read only";
    case CTRL_GATE_FULL:     return "full";
    default:                 return "off";
    }
}

void ctrlClientSetCurrent(CtrlClient client)
{
    currentClient = client;
    requestSeq = 0;
}

CtrlClient ctrlClientCurrent(void)
{
    return currentClient;
}

/* Only a read folds: into a read of the same storage, through the same window, that it continues or repeats. */
static int foldsInto(const CtrlActivityEntry* tail, const CtrlActivityEntry* entry, UInt32 now)
{
    const CtrlAddr* at = &entry->addr;

    if (tail->seq == 0 || tail->client != entry->client || entry->verb != CTRL_ACT_READ ||
        tail->verb != CTRL_ACT_READ || !entry->hasAddr || !tail->hasAddr ||
        entry->outcome != CTRL_ACT_DONE || tail->outcome != CTRL_ACT_DONE)
    {
        return 0;
    }
    if (now - tail->lastMs > ACT_GAP_MS || now - tail->firstMs > ACT_SPAN_MS) {
        return 0;
    }
    if (at->space != tail->addr.space || at->slot != tail->addr.slot ||
        at->subSlot != tail->addr.subSlot || at->segment != tail->addr.segment)
    {
        return 0;
    }
    if ((entry->z80 < 0) != (tail->z80 < 0) ||
        (entry->z80 >= 0 && entry->z80 - (Int32)at->offset != tail->z80 - (Int32)tail->addr.offset))
    {
        return 0;
    }
    return at->offset <= tail->rangeEnd && entry->rangeEnd >= tail->addr.offset;
}

static void fold(CtrlActivityEntry* tail, const CtrlActivityEntry* entry, UInt32 now)
{
    tail->count++;
    tail->bytes += entry->bytes;
    tail->lastMs = now;
    if (entry->rangeEnd > tail->rangeEnd) {
        tail->rangeEnd = entry->rangeEnd;
    }
    if (entry->addr.offset < tail->addr.offset) {
        tail->addr.offset = entry->addr.offset;
        tail->z80 = entry->z80;
    }
}

static void notifyChanged(void)
{
    if (listener != NULL) {
        listener();
    }
}

/* With the lock held: entry becomes the next line, or folds into the last one. Returns 1 for a
** new line. */
static int append(const CtrlActivityEntry* entry)
{
    UInt32 now = archGetSystemUpTime(1000);
    CtrlActivityEntry* tail = nextSeq > 1 ? &ring[(nextSeq - 1) % CTRL_ACT_RING] : NULL;
    CtrlActivityEntry* slot;
    CtrlClientInfo* info;
    FILETIME wall;
    int added = tail == NULL || !foldsInto(tail, entry, now);

    if (!added) {
        fold(tail, entry, now);
    }
    else {
        slot = &ring[nextSeq % CTRL_ACT_RING];
        RACE_TEST_POINT(RACE_APPEND);
        *slot = *entry;
        GetSystemTimePreciseAsFileTime(&wall);
        slot->loggedAt = ((UInt64)wall.dwHighDateTime << 32) | wall.dwLowDateTime;
        slot->seq      = nextSeq++;
        slot->firstMs  = now;
        slot->lastMs   = now;
        slot->count    = 1;
        info = findClient(entry->client);
        if (info != NULL) {
            memcpy(slot->label, info->label, CTRL_CLIENT_LABEL);
        }
    }
    notifyChanged();
    return added;
}

/* An entry for client with nothing but a verb and a detail. */
static void startEntry(CtrlActivityEntry* entry, CtrlClient client, int verb, UInt32 bytes,
                       const char* detail, int outcome)
{
    memset(entry, 0, sizeof(CtrlActivityEntry));
    entry->client  = client;
    entry->verb    = (UInt8)verb;
    entry->outcome = (UInt8)outcome;
    entry->bytes   = bytes;
    entry->z80     = -1;
    if (detail != NULL) {
        copyText(entry->detail, CTRL_ACT_DETAIL, detail);
    }
}

/* With the lock held. */
static void appendPlain(CtrlClient client, int verb, const char* detail)
{
    CtrlActivityEntry entry;

    startEntry(&entry, client, verb, 0, detail, CTRL_ACT_DONE);
    append(&entry);
}

static void placeEntry(CtrlActivityEntry* entry, const CtrlAddr* addr, Int32 z80, UInt32 bytes)
{
    entry->addr     = *addr;
    entry->z80      = z80;
    entry->hasAddr  = 1;
    entry->rangeEnd = addr->offset + bytes;
}

/* A range that leaves its segment or its 16 kB page is never shown as one slot: it shows as the
** Z80 range, or, when nothing of it is paged in, as its start alone. */
static void placeResolved(CtrlActivityEntry* entry, const CtrlAddrInfo* where, UInt32 bytes)
{
    const CtrlAddr* canonical = &where->canonical;
    int leaves = where->segmentSize > 0
        ? canonical->offset + bytes > (UInt32)where->segmentSize
        : where->z80Address >= 0 && (UInt32)(where->z80Address & 0x3fff) + bytes > 0x4000;

    if (leaves && where->z80Address >= 0) {
        CtrlAddr cpu;

        memset(&cpu, 0, sizeof(cpu));
        cpu.space  = CTRL_SPACE_CPU;
        cpu.offset = (UInt32)where->z80Address;
        placeEntry(entry, &cpu, -1, bytes);
        return;
    }
    placeEntry(entry, canonical, where->z80Address, leaves ? 1 : bytes);
}

/* Only the thread that serves agents logs through here. */
static void store(const CtrlActivityEntry* entry)
{
    lock();
    if (append(entry)) {
        requestSeq = nextSeq - 1;
    }
    unlock();
}

void ctrlActivityNoteOn(int verb, const char* target, const char* detail, int outcome)
{
    CtrlActivityEntry entry;

    startEntry(&entry, currentClient, verb, 0, detail, outcome);
    if (target != NULL) {
        copyText(entry.target, CTRL_ACT_TARGET, target);
    }
    store(&entry);
}

void ctrlActivityNoteAmount(int verb, const char* target, UInt32 amount)
{
    CtrlActivityEntry entry;

    startEntry(&entry, currentClient, verb, 0, NULL, CTRL_ACT_DONE);
    entry.amount = amount;
    if (target != NULL) {
        copyText(entry.target, CTRL_ACT_TARGET, target);
    }
    store(&entry);
}

/* A file by its name first, then its folder. */
static void fileTarget(char* shown, size_t size, const char* path)
{
    const char* name = path;
    const char* at;

    for (at = path; *at != 0; at++) {
        if (*at == '\\' || *at == '/') {
            name = at + 1;
        }
    }
    if (name == path) {
        copyText(shown, size, path);
        return;
    }
    snprintf(shown, size, "%s  (%.*s)", name, (int)(name - 1 - path), path);
}

void ctrlActivityNoteFile(int verb, const char* path, int outcome)
{
    char shown[CTRL_ACT_TARGET];

    fileTarget(shown, sizeof(shown), path);
    ctrlActivityNoteOn(verb, shown, NULL, outcome);
}

void ctrlActivityNoteAt(int verb, const CtrlAddrInfo* where, UInt32 bytes,
                        const char* detail, int outcome)
{
    CtrlActivityEntry entry;

    startEntry(&entry, currentClient, verb, bytes, detail, outcome);
    placeResolved(&entry, where, bytes);
    store(&entry);
}

/* An entry for a request that did not happen; DENIED keeps the op name in detail. */
static void startRefusal(CtrlActivityEntry* entry, int verb, const char* opName,
                         const CtrlAddr* addr, const char* target, int outcome)
{
    startEntry(entry, currentClient, verb, 0, verb == CTRL_ACT_DENIED ? opName : NULL, outcome);
    if (addr != NULL) {
        placeEntry(entry, addr, -1, 1);
    }
    else if (target != NULL) {
        copyText(entry->target, CTRL_ACT_TARGET, target);
    }
}

void ctrlActivityNoteDenied(int verb, const char* opName, const CtrlAddr* addr,
                            const char* target, int why)
{
    CtrlActivityEntry entry;

    startRefusal(&entry, verb, opName, addr, target, why);
    store(&entry);
}

/* With the lock held: the line the current request started, while it is still held, else NULL. */
static CtrlActivityEntry* requestEntry(void)
{
    CtrlActivityEntry* logged = &ring[requestSeq % CTRL_ACT_RING];

    return requestSeq != 0 && logged->seq == requestSeq ? logged : NULL;
}

/* With the lock held: the newest line, when it is a failure entry repeats. */
static CtrlActivityEntry* repeatedFailure(const CtrlActivityEntry* entry)
{
    CtrlActivityEntry* tail = &ring[failedSeq % CTRL_ACT_RING];

    if (failedSeq == 0 || failedSeq != nextSeq - 1 || tail->seq != failedSeq) {
        return NULL;
    }
    return tail->client == entry->client && tail->verb == entry->verb &&
           strcmp(tail->detail, entry->detail) == 0 && strcmp(tail->target, entry->target) == 0 &&
           strcmp(tail->code, entry->code) == 0 ? tail : NULL;
}

/* With the lock held: a failure the request logged no line for, folded into its repeat. */
static void appendFailure(const CtrlActivityEntry* entry)
{
    CtrlActivityEntry* repeated = repeatedFailure(entry);

    if (repeated == NULL) {
        append(entry);
        failedSeq = nextSeq - 1;
        return;
    }
    repeated->count++;
    repeated->lastMs = archGetSystemUpTime(1000);
    notifyChanged();
}

void ctrlActivityNoteFailed(int verb, const char* opName, const char* target, int targetIsPath,
                            const char* code)
{
    CtrlActivityEntry  entry;
    CtrlActivityEntry* logged;
    char shown[CTRL_ACT_TARGET];

    if (target != NULL && targetIsPath) {
        fileTarget(shown, sizeof(shown), target);
        target = shown;
    }
    startRefusal(&entry, verb, opName, NULL, target, CTRL_ACT_FAILED);
    copyText(entry.code, CTRL_ACT_CODE, code);

    lock();
    logged = requestEntry();
    /* A refused, cut off or not kept line already says what went wrong. */
    if (logged != NULL && (logged->outcome == CTRL_ACT_DONE || logged->outcome == CTRL_ACT_FAILED)) {
        logged->outcome = CTRL_ACT_FAILED;
        memcpy(logged->code, entry.code, CTRL_ACT_CODE);
        notifyChanged();
    }
    else if (requestSeq == 0) {
        appendFailure(&entry);
    }
    unlock();
}

void ctrlActivityNoteWrite(int verb, const CtrlAddrInfo* where, UInt32 bytes,
                           const UInt8* before, const UInt8* after, int peek, int outcome)
{
    CtrlActivityEntry entry;

    startEntry(&entry, currentClient, verb, bytes, NULL, outcome);
    placeResolved(&entry, where, bytes);
    if (peek > CTRL_ACT_PEEK) {
        peek = CTRL_ACT_PEEK;
    }
    if (peek > 0 && before != NULL) {
        memcpy(entry.before, before, (size_t)peek);
        entry.beforeLen = (UInt8)peek;
    }
    if (peek > 0 && after != NULL) {
        memcpy(entry.after, after, (size_t)peek);
        entry.afterLen = (UInt8)peek;
    }
    store(&entry);
}

/* With the lock held. */
static UInt32 oldestSeq(void)
{
    UInt32 held = nextSeq - 1;

    return held > CTRL_ACT_RING ? held - CTRL_ACT_RING + 1 : 1;
}

int ctrlActivitySeqRange(UInt32* first, UInt32* last)
{
    int any;

    lock();
    any = nextSeq > 1;
    *first = any ? oldestSeq() : 0;
    *last  = any ? nextSeq - 1 : 0;
    unlock();
    return any;
}

int ctrlActivityEntryBySeq(UInt32 seq, CtrlActivityEntry* out)
{
    int got = 0;

    lock();
    if (seq != 0 && seq < nextSeq && seq >= oldestSeq()) {
        *out = ring[seq % CTRL_ACT_RING];
        got = out->seq == seq;
    }
    unlock();
    return got;
}

int ctrlClientList(CtrlClientInfo* out, int max)
{
    int i;
    int copied = 0;

    lock();
    for (i = 0; i < CTRL_CLIENT_MAX && copied < max; i++) {
        if (clientUsed[i]) {
            out[copied++] = clients[i];
        }
    }
    unlock();
    return copied;
}
