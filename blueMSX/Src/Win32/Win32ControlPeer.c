/*****************************************************************************
**
** Win32 client connection socket.
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
#include "ControlPeer.h"
/* Must precede windows.h, or winsock 1 is pulled in and collides with it. */
#include <winsock2.h>
#include <windows.h>

static SOCKET clientSocket = INVALID_SOCKET;

void ctrlPeerSet(uintptr_t socketHandle)
{
    clientSocket = (SOCKET)socketHandle;
}

int ctrlPeerGone(void)
{
    fd_set readable;
    struct timeval now = { 0, 0 };
    char   byte;

    if (clientSocket == INVALID_SOCKET) {
        return 0;
    }
    FD_ZERO(&readable);
    FD_SET(clientSocket, &readable);
    if (select(0, &readable, NULL, NULL, &now) <= 0) {
        return 0;
    }
    /* Readable with nothing to read is a close or a reset; data is a pipelined request. */
    return recv(clientSocket, &byte, 1, MSG_PEEK) <= 0;
}

void ctrlPeerShutdown(void)
{
    if (clientSocket != INVALID_SOCKET) {
        shutdown(clientSocket, SD_BOTH);
    }
}
