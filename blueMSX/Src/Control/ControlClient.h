/*****************************************************************************
**
** Agent Control clients and the access level.
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

/* One accepted connection, numbered by the emulator; a client that reconnects is a new one with the same owner. */
typedef int CtrlClient;

#define CTRL_CLIENT_NONE  0    /* no client */

#define CTRL_CLIENT_MAX   8

typedef struct {
    CtrlClient client;
    int        revoked;
} CtrlClientInfo;

void ctrlClientCreate(void);

/* ---- who may act -------------------------------------------------------- */

CtrlClient ctrlClientOpen(void);
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

/* Client the current work belongs to; set around each request by whatever services it. */
void       ctrlClientSetCurrent(CtrlClient client);
CtrlClient ctrlClientCurrent(void);

#endif
