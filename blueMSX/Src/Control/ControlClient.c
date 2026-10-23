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
#include "ControlClient.h"
#include "ArchEvent.h"
#include <string.h>

static CtrlClientInfo clients[CTRL_CLIENT_MAX];
static int        clientUsed[CTRL_CLIENT_MAX];
static CtrlClient nextClientId = 1;
/* Written only by the thread that serves agents. */
static CtrlClient currentClient;
static volatile int blockAll;
/* A leaf lock over the clients: the window's thread revokes them too. */
static void*      clientLock;

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
    memset(clients, 0, sizeof(clients));
    memset(clientUsed, 0, sizeof(clientUsed));
    nextClientId = 1;
    currentClient = CTRL_CLIENT_NONE;
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

CtrlClient ctrlClientOpen(void)
{
    CtrlClient opened = CTRL_CLIENT_NONE;
    int i;

    lock();
    for (i = 0; i < CTRL_CLIENT_MAX && !blockAll; i++) {
        if (!clientUsed[i]) {
            memset(&clients[i], 0, sizeof(CtrlClientInfo));
            clients[i].client   = nextClientId++;
            clientUsed[i] = 1;
            opened = clients[i].client;
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

void ctrlClientBlockAll(int block)
{
    int i;

    lock();
    blockAll = block;
    for (i = 0; i < CTRL_CLIENT_MAX && block; i++) {
        if (clientUsed[i]) {
            clients[i].revoked = 1;
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
}

CtrlClient ctrlClientCurrent(void)
{
    return currentClient;
}
