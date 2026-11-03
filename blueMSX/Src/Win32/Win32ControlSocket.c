/*****************************************************************************
**
** Win32 loopback control socket server.
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
#include "ControlSocket.h"
/* Must precede windows.h, or winsock 1 is pulled in and collides with it. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "ControlRequest.h"
#include "ControlHello.h"
#include "ControlPeer.h"
#include "ControlInstance.h"
#include "ControlSession.h"
#include "ControlBreak.h"
#include "ControlClient.h"
#include "ControlProtocol.h"
#include "ArchThread.h"
#include "Debugger.h"
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define LISTEN_BACKLOG 4
/* A client that sends no hello in this time is closed, so it cannot hold off the grace for long. */
#define HELLO_TIMEOUT_MS 2000

/* How long a stop waits for a request in progress, pumping only sent messages: a plugin
** callback on the serving thread may be waiting for the window. */
#define JOIN_MS 5000

static SOCKET listenSocket = INVALID_SOCKET;
static void*  thread;
/* A serving thread Off could not join; it ends its own session when its request is done. */
static void*  orphan;
static int    running;
static int    boundPort;
static int    winsockUp;
static void (*statusListener)(void);

int ctrlSocketIsListening(void)
{
    return running && listenSocket != INVALID_SOCKET;
}

int ctrlSocketPort(void)
{
    return boundPort;
}

static int sendAll(SOCKET socketHandle, const char* data, int length)
{
    while (length > 0) {
        int sent = send(socketHandle, data, length, 0);
        if (sent <= 0) {
            return 0;
        }
        data += sent;
        length -= sent;
    }
    return 1;
}

static int sendLine(SOCKET socketHandle, const char* line)
{
    return sendAll(socketHandle, line, (int)strlen(line)) &&
           sendAll(socketHandle, "\n", 1);
}

/* One client at a time: the request handler's reply buffers are shared. */
static void serveClient(SOCKET socketHandle)
{
    static char line[CTRL_LINE_MAX];
    static char reply[CTRL_REPLY_MAX];
    int    length = 0;
    int    overlong = 0;   /* the line being read has lost a byte */
    CtrlClient client = CTRL_CLIENT_NONE;
    DWORD  waitMs = HELLO_TIMEOUT_MS;

    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&waitMs, sizeof(waitMs));
    for (;;) {
        char   chunk[4096];
        int    received = recv(socketHandle, chunk, sizeof(chunk), 0);
        int    i;

        if (received <= 0) {
            break;
        }

        for (i = 0; i < received; i++) {
            if (chunk[i] != '\n') {
                if (length < CTRL_LINE_MAX - 1) {
                    line[length++] = chunk[i];
                }
                else {
                    overlong = 1;
                }
                continue;
            }
            line[length] = 0;
            length = 0;

            /* Never the start of a request run as if it were all of it; before hello, it ends. */
            if (overlong) {
                overlong = 0;
                ctrlRequestRefuseLine(line, reply, sizeof(reply));
                if (!sendLine(socketHandle, reply) || client == CTRL_CLIENT_NONE) {
                    goto finished;
                }
                continue;
            }

            if (client == CTRL_CLIENT_NONE) {
                char  hello[256];
                DWORD noTimeout = 0;

                client = ctrlHelloAnswer(line, ctrlInstanceToken(), hello, sizeof(hello));
                if (client == CTRL_CLIENT_NONE) {
                    sendLine(socketHandle, hello);
                    return;
                }
                /* A greeted client may wait as long as it likes between requests. */
                setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&noTimeout,
                           sizeof(noTimeout));
                if (!sendLine(socketHandle, hello)) {
                    goto finished;
                }
                continue;
            }

            ctrlRequestHandle(client, line, reply, sizeof(reply));
            if (!sendLine(socketHandle, reply)) {
                goto finished;
            }
        }
    }

finished:
    if (client != CTRL_CLIENT_NONE) {
        ctrlSessionClose(client);
        ctrlClientClose(client);
        /* Its plain breakpoints end unless it comes back in time; a killed MCP server sends no
        ** session.end, which would have cleared them already. */
        ctrlBreakLeft(GetTickCount());
    }
}

static void acceptLoop(void)
{
    dbgMarkAgentThread();
    while (running) {
        struct sockaddr_in from;
        int    fromLength = sizeof(from);
        struct timeval slice = { 0, 250000 };
        fd_set waiting;
        SOCKET accepted;
        int    ready;

        ctrlBreakTick(GetTickCount());
        /* In slices, so that the grace above ends on time while no client connects. */
        FD_ZERO(&waiting);
        FD_SET(listenSocket, &waiting);
        ready = select(0, &waiting, NULL, NULL, &slice);
        if (ready == 0) {
            continue;
        }
        accepted = ready > 0 ? accept(listenSocket, (struct sockaddr*)&from, &fromLength)
                             : INVALID_SOCKET;

        if (accepted == INVALID_SOCKET) {
            if (!running) {
                break;
            }
            archThreadSleep(20);
            continue;
        }

        /* Redundant with the loopback bind; kept as a defence in depth. */
        if (from.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
            closesocket(accepted);
            continue;
        }

        ctrlPeerSet((uintptr_t)accepted);
        serveClient(accepted);
        ctrlPeerSet((uintptr_t)INVALID_SOCKET);
        closesocket(accepted);
    }
}

void ctrlSocketPoll(void)
{
    if (orphan != NULL && WaitForSingleObject((HANDLE)orphan, 0) == WAIT_OBJECT_0) {
        CloseHandle((HANDLE)orphan);
        orphan = NULL;
        /* Off would have dropped its client's plain breakpoints; that is done now. */
        ctrlBreakDropDeparted();
        if (statusListener != NULL) {
            statusListener();
        }
    }
}

void ctrlSocketSetStatusListener(void (*changed)(void))
{
    statusListener = changed;
}

/* Non-zero while the thread a stop could not join is still ending. */
static int ctrlSocketIsClosing(void)
{
    ctrlSocketPoll();
    return orphan != NULL;
}

int ctrlSocketOrphanPending(void)
{
    return orphan != NULL;
}

int ctrlSocketStart(void)
{
    struct sockaddr_in address;
    int    addressLength = sizeof(address);
    WSADATA wsaData;

    if (running) {
        return 1;
    }
    if (ctrlGateLevel() == CTRL_GATE_OFF || ctrlSocketIsClosing()) {
        return 0;
    }

    if (!winsockUp) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return 0;
        }
        winsockUp = 1;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        return 0;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;

    if (bind(listenSocket, (struct sockaddr*)&address, sizeof(address)) != 0 ||
        listen(listenSocket, LISTEN_BACKLOG) != 0 ||
        getsockname(listenSocket, (struct sockaddr*)&address, &addressLength) != 0)
    {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return 0;
    }
    boundPort = ntohs(address.sin_port);

    ctrlInstancePublish(boundPort);

    running = 1;
    thread = archThreadCreate(acceptLoop, THREAD_PRIO_NORMAL);
    if (thread == NULL) {
        running = 0;
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        ctrlInstanceRemove();
        return 0;
    }
    return 1;
}

/* Called on the main thread, which the serving thread may be sending a window
** message to (a pause updates the menu). Only sent messages are delivered here,
** so nothing the user does is handled in the middle of the stop. Non-zero once it ended. */
static int joinThread(void* joined, int timeoutMs)
{
    DWORD started = GetTickCount();
    HANDLE handle = (HANDLE)joined;

    for (;;) {
        DWORD waited = GetTickCount() - started;
        DWORD woken;
        MSG   message;

        if (waited >= (DWORD)timeoutMs) {
            return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
        }
        woken = MsgWaitForMultipleObjects(1, &handle, FALSE, (DWORD)timeoutMs - waited,
                                          QS_SENDMESSAGE);
        if (woken == WAIT_OBJECT_0) {
            return 1;
        }
        if (woken != WAIT_OBJECT_0 + 1) {
            return 0;
        }
        PeekMessage(&message, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
}

/* At exit only: a thread that did not end in time is killed, and nothing it may have held
** a lock in (session, activity, stop log, access) is touched after. */
static void endAtExit(void* joined)
{
    if (!joinThread(joined, JOIN_MS)) {
        TerminateThread((HANDLE)joined, 0);
    }
    CloseHandle((HANDLE)joined);
}

void ctrlSocketStop(int exiting)
{
    if (exiting && orphan != NULL) {
        endAtExit(orphan);
        orphan = NULL;
    }
    if (!running) {
        ctrlInstanceRemove();
        return;
    }
    running = 0;
    /* First, so a client cut off below does not find this emulator again. */
    ctrlInstanceRemove();
    /* Ends any wait a request is in, so the thread can be joined rather than killed. */
    ctrlClientBlockAll(1);

    /* Shutting the client down ends its recv; closing the listener fails the select. */
    ctrlPeerShutdown();
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
    boundPort = 0;
    if (exiting) {
        endAtExit(thread);
        thread = NULL;
        return;
    }
    /* Never killed here: it may be inside a plugin callback or hold the access lock. */
    if (!joinThread(thread, JOIN_MS)) {
        orphan = thread;
        thread = NULL;
        return;
    }
    CloseHandle((HANDLE)thread);
    thread = NULL;
    ctrlBreakDropDeparted();
}
