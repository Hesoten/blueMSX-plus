/*****************************************************************************
**
** Per-connection agent sessions.
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
#include "ControlSession.h"
#include "ControlBreak.h"
#include "ControlStopLog.h"
#include "Emulator.h"
#include <string.h>

static CtrlSession sessions[CTRL_CLIENT_MAX];

CtrlSession* ctrlSessionFor(CtrlClient client)
{
    int i;
    int free = -1;

    for (i = 0; i < CTRL_CLIENT_MAX; i++) {
        if (sessions[i].client == client) {
            return &sessions[i];
        }
        if (free < 0 && sessions[i].client == CTRL_CLIENT_NONE) {
            free = i;
        }
    }
    if (free < 0) {
        return NULL;
    }
    memset(&sessions[free], 0, sizeof(CtrlSession));
    sessions[free].client = client;
    return &sessions[free];
}

int ctrlSessionOpen(CtrlClient client, int owner)
{
    CtrlSession* session = ctrlSessionFor(client);
    CtrlStop latest;
    int    dropped = ctrlBreakOpen(owner);

    if (session != NULL) {
        /* Skip stops a previous client left unread, but not the one the machine is paused at. */
        session->stopCursor = ctrlStopSeq();
        if (emulatorGetState() == EMU_PAUSED && ctrlStopLatest(&latest)) {
            session->stopCursor = latest.seq - 1;
        }
    }
    return dropped;
}

void ctrlSessionClose(CtrlClient client)
{
    int i;

    for (i = 0; i < CTRL_CLIENT_MAX; i++) {
        if (sessions[i].client == client) {
            memset(&sessions[i], 0, sizeof(CtrlSession));
            return;
        }
    }
}
