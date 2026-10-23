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
#ifndef CONTROL_CLIENT_H
#define CONTROL_CLIENT_H

#include "ControlAddressType.h"

/* Logged inside each access; none may be suppressed or grouped, save the emulator's own ctrlMemPeek reads. */

#define CTRL_ACT_READ        1
#define CTRL_ACT_WRITE       2
#define CTRL_ACT_RUN         3
#define CTRL_ACT_PAUSE       4
#define CTRL_ACT_STEP        5
#define CTRL_ACT_RESET       6
#define CTRL_ACT_BREAK_SET   7
#define CTRL_ACT_BREAK_CLEAR 8
#define CTRL_ACT_WATCH_SET   9
#define CTRL_ACT_WATCH_CLEAR 10
#define CTRL_ACT_HUNT_START  11
#define CTRL_ACT_HUNT_REFINE 12
#define CTRL_ACT_HUNT_LIST   13
#define CTRL_ACT_HUNT_END    14
#define CTRL_ACT_CONNECT     15
#define CTRL_ACT_DISCONNECT  16
#define CTRL_ACT_DENIED      17
#define CTRL_ACT_REVOKED     18
#define CTRL_ACT_KEY_TAP     19
#define CTRL_ACT_SYM_LOAD    20
#define CTRL_ACT_UNDO        21
#define CTRL_ACT_SNAPSHOT    22
#define CTRL_ACT_REG_WRITE   23
#define CTRL_ACT_BANK_SWITCH 24
#define CTRL_ACT_SCREEN_SAVE 25
#define CTRL_ACT_TYPE        26
#define CTRL_ACT_RESET_SOFT  27
#define CTRL_ACT_KEY_PRESS   28
#define CTRL_ACT_KEY_RELEASE 29
#define CTRL_ACT_SNAPSHOT_RESTORE 30
#define CTRL_ACT_STEP_OVER   31
#define CTRL_ACT_STEP_OUT    32
#define CTRL_ACT_RUN_UNTIL_STOP 33
#define CTRL_ACT_BREAK_CLEAR_ALL 34
#define CTRL_ACT_SCREEN_TEXT 35
#define CTRL_ACT_VERB_COUNT  36

/* How an entry ended; DONE unless it says otherwise. */
#define CTRL_ACT_DONE         0
#define CTRL_ACT_FAILED       1   /* asked for, but it did not happen                 */
#define CTRL_ACT_NOT_KEPT     2   /* a write the memory did not keep                  */
#define CTRL_ACT_READ_ONLY    3   /* DENIED: the access level does not allow it       */
#define CTRL_ACT_CUT_OFF      4   /* DENIED: the connection was cut off               */
#define CTRL_ACT_TIMED_OUT    5   /* a pause made because a wait ran out              */
#define CTRL_ACT_NOT_KEPT_ROM 6   /* NOT_KEPT on plain ROM, which stayed as it was    */

/* One accepted connection, numbered by the emulator; a client that reconnects is a new one with the same owner. */
typedef int CtrlClient;

#define CTRL_CLIENT_NONE  0    /* no client */

#define CTRL_CLIENT_MAX   8
#define CTRL_CLIENT_LABEL 32
#define CTRL_ACT_TARGET   64
#define CTRL_ACT_DETAIL   40
#define CTRL_ACT_PEEK     8
#define CTRL_ACT_CODE     24
/* Entries held; older ones are dropped. */
#define CTRL_ACT_RING     4096

typedef struct {
    UInt32 seq;          /* only goes up; a gap means entries were dropped */
    UInt32 firstMs;      /* host uptime, for folding only                  */
    UInt32 lastMs;
    UInt64 loggedAt;     /* wall clock when logged, as a UTC FILETIME      */
    UInt32 count;        /* accesses folded into this line                 */
    UInt32 bytes;
    UInt32 amount;       /* instructions stepped, or search candidates left   */
    Int32  client;
    UInt8  verb;
    UInt8  outcome;      /* CTRL_ACT_DONE and the others above             */
    UInt8  hasAddr;
    UInt8  beforeLen;
    UInt8  afterLen;
    CtrlAddr addr;       /* canonical when it could be resolved, else as given */
    Int32  z80;          /* the Z80 address addr was reached at, -1 if none */
    UInt32 rangeEnd;     /* offset one past the furthest byte touched      */
    UInt8  before[CTRL_ACT_PEEK];
    UInt8  after[CTRL_ACT_PEEK];
    char   label[CTRL_CLIENT_LABEL]; /* the client's name when it was logged */
    char   target[CTRL_ACT_TARGET];  /* what it was done to, when no address */
    char   detail[CTRL_ACT_DETAIL];
    char   code[CTRL_ACT_CODE];  /* the error code a FAILED entry was answered with, if any */
} CtrlActivityEntry;

typedef struct {
    CtrlClient client;
    char       label[CTRL_CLIENT_LABEL];
    int        revoked;
} CtrlClientInfo;

void ctrlClientCreate(void);

/* Called after every new or folded entry, on whichever thread logged it, with the log locked. */
void ctrlActivitySetListener(void (*changed)(void));

/* ---- who may act -------------------------------------------------------- */

/* label is the client's own name for itself, from its hello; display only. */
CtrlClient ctrlClientOpen(const char* label);
void       ctrlClientClose(CtrlClient client);

/* Asked before a request is served, and by the accesses that read or change the machine. */
int  ctrlClientAllowed(CtrlClient client);

void ctrlClientBlockAll(int block);
int  ctrlClientAllBlocked(void);

/* How much of the machine a connected client may touch; enforced where a request is answered
** (ControlRequest.c and the ops that check it), never in a bridge. */
#define CTRL_GATE_OFF      0  /* not listening at all                       */
#define CTRL_GATE_READONLY 1  /* may look; may not write, run or break      */
#define CTRL_GATE_FULL     2

int  ctrlGateLevel(void);
void ctrlGateSetLevel(int level);
const char* ctrlGateLevelText(int level);

/* Client the current work belongs to; set around each request by whatever services it, which
** starts a new request for ctrlActivityNoteFailed. */
void       ctrlClientSetCurrent(CtrlClient client);
CtrlClient ctrlClientCurrent(void);

/* ---- what --------------------------------------------------------------- */

/* target names what was acted on when it is no address: a register, a bank, keys. */
void ctrlActivityNoteOn(int verb, const char* target, const char* detail, int outcome);

/* amount is the instructions a step ran, or the candidates a search has left. */
void ctrlActivityNoteAmount(int verb, const char* target, UInt32 amount);

/* A file the agent named, shown by its name first, then its folder. */
void ctrlActivityNoteFile(int verb, const char* path, int outcome);

/* where is the address resolved while the view was held; only a read folds, into the read before it. */
void ctrlActivityNoteAt(int verb, const CtrlAddrInfo* where, UInt32 bytes,
                        const char* detail, int outcome);

/* verb is what was refused, or CTRL_ACT_DENIED with its op name in opName; addr or target is
** the place as given, or NULL. why is CTRL_ACT_READ_ONLY or CTRL_ACT_CUT_OFF. */
void ctrlActivityNoteDenied(int verb, const char* opName, const CtrlAddr* addr,
                            const char* target, int why);

/* A request answered with the error code: the entry it logged, if DONE or FAILED, is marked
** FAILED; with none, one is logged as ctrlActivityNoteDenied would, path shown as a file. A
** repeat of the newest such entry folds into it. */
void ctrlActivityNoteFailed(int verb, const char* opName, const char* target, int targetIsPath,
                            const char* code);

/* verb is CTRL_ACT_WRITE or CTRL_ACT_UNDO. Keeps the first peek bytes (at most CTRL_ACT_PEEK)
** before, and after as read back, so they show what the memory holds. */
void ctrlActivityNoteWrite(int verb, const CtrlAddrInfo* where, UInt32 bytes,
                           const UInt8* before, const UInt8* after, int peek, int outcome);

/* ---- reading it back ---------------------------------------------------- */

/* The oldest and newest seq still held; returns 0 when nothing is. */
int  ctrlActivitySeqRange(UInt32* first, UInt32* last);
/* Returns 0 when seq has been dropped or is not logged yet. */
int  ctrlActivityEntryBySeq(UInt32 seq, CtrlActivityEntry* out);

/* The connected clients, at most max; returns how many were copied. */
int  ctrlClientList(CtrlClientInfo* out, int max);

#endif
