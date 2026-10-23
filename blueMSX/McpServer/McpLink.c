/*****************************************************************************
**
** MCP server connection to blueMSX+.
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
#define _CRT_RAND_S
#include "McpLink.h"
#include "McpInstance.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ControlJson.h"

#pragma comment(lib, "ws2_32.lib")

#define REPLY_MAX (1024 * 1024)
#define HELLO_TIMEOUT_MS 5000

static SOCKET emulator = INVALID_SOCKET;
static char   attachedMachine[128];
static int    attachedPid;
/* So the first reply from a restarted emulator can say that nothing carried over. */
static int    lastServedPid;
static int    previousPid;

static char socketLine[REPLY_MAX];

void mcpIdAsJson(const char* request, const char* key, char* out, int outLen)
{
    char text[MCP_ID_TEXT_MAX];
    char safe[6 * MCP_ID_TEXT_MAX + 8];
    Int32 number;

    if (jsonGetString(request, key, text, sizeof(text))) {
        jsonEscape(text, safe, sizeof(safe));
        _snprintf(out, (size_t)outLen, "\"%s\"", safe);
        return;
    }
    if (jsonGetInt(request, key, &number)) {
        _snprintf(out, (size_t)outLen, "%d", (int)number);
        return;
    }
    strcpy(out, "null");
}

static void detach(void)
{
    if (emulator != INVALID_SOCKET) {
        closesocket(emulator);
        emulator = INVALID_SOCKET;
    }
    attachedPid = 0;
}

static int sendAll(const char* data, int length)
{
    while (length > 0) {
        int sent = send(emulator, data, length, 0);
        if (sent <= 0) {
            return 0;
        }
        data += sent;
        length -= sent;
    }
    return 1;
}

static int readLine(char* out, int outLen)
{
    int length = 0;

    for (;;) {
        char byte;
        int  got = recv(emulator, &byte, 1, 0);

        if (got <= 0) {
            return 0;
        }
        if (byte == '\n') {
            out[length] = 0;
            return 1;
        }
        if (length < outLen - 1) {
            out[length++] = byte;
        }
    }
}

static int connectInstance(const McpInstance* instance, char* problem, int problemLen)
{
    struct sockaddr_in address;

    emulator = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (emulator == INVALID_SOCKET) {
        _snprintf(problem, (size_t)problemLen, "could not make a socket");
        return 0;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = htons((u_short)instance->port);

    if (connect(emulator, (struct sockaddr*)&address, sizeof(address)) != 0) {
        detach();
        _snprintf(problem, (size_t)problemLen,
                  "blueMSX+ (pid %d) is running but did not answer on port %d",
                  instance->pid, instance->port);
        return 0;
    }
    return 1;
}

/* Sends the hello with the token and reads its answer. */
static int sayHello(const McpInstance* instance, char* problem, int problemLen)
{
    char  hello[160];
    DWORD timeoutMs;
    int   ok = 0;

    _snprintf(hello, sizeof(hello),
              "{\"op\":\"hello\",\"token\":\"%s\"}\n",
              instance->token);
    if (!sendAll(hello, (int)strlen(hello))) {
        detach();
        _snprintf(problem, (size_t)problemLen, "the handshake with blueMSX+ failed");
        return 0;
    }
    /* It serves one client at a time; only the hello may time out, as run.until waits for minutes. */
    timeoutMs = HELLO_TIMEOUT_MS;
    setsockopt(emulator, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
    if (!readLine(socketLine, REPLY_MAX)) {
        detach();
        _snprintf(problem, (size_t)problemLen,
                  "blueMSX+ (pid %d) did not answer within 5 s. It serves one client at "
                  "a time, so another MCP session (possibly an earlier one that is still "
                  "open) or tool is probably connected to it.\\nfix: close that session, "
                  "or start another emulator for this one.", instance->pid);
        return 0;
    }
    timeoutMs = 0;
    setsockopt(emulator, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
    jsonGetBool(socketLine, "ok", &ok);
    if (!ok) {
        char message[256];
        char safe[512];
        if (!jsonGetString(socketLine, "message", message, sizeof(message))) {
            strcpy(message, "(the reason could not be read)");
        }
        jsonEscape(message, safe, sizeof(safe));
        detach();
        _snprintf(problem, (size_t)problemLen, "blueMSX+ refused: %s", safe);
        return 0;
    }
    return 1;
}

/* Called per request, not at startup: the server must keep running while no emulator is up. */
static int attach(char* problem, int problemLen)
{
    McpInstance list[MCP_INSTANCE_MAX];
    int    picked[MCP_INSTANCE_MAX];
    char   pinText[400];
    int    stored = 0;
    int    live;
    int    count;
    const McpInstance* instance;

    if (emulator != INVALID_SOCKET) {
        return 1;
    }

    count = mcpInstancePick(list, picked, &live, &stored);
    jsonEscape(mcpInstancePinned(), pinText, sizeof(pinText));
    if (count == 0) {
        mcpInstanceExplainNone(list, picked, live, stored, pinText, problem, problemLen);
        return 0;
    }
    if (count > 1) {
        mcpInstanceExplainMany(list, picked, count, live, stored, pinText, problem, problemLen);
        return 0;
    }
    instance = &list[picked[0]];
    strcpy(attachedMachine, instance->machine);
    if (!connectInstance(instance, problem, problemLen) ||
        !sayHello(instance, problem, problemLen))
    {
        return 0;
    }

    attachedPid = instance->pid;
    if (lastServedPid != 0 && instance->pid != lastServedPid) {
        previousPid = lastServedPid;
    }
    lastServedPid = instance->pid;
    return 1;
}

/* The id of the call waiting in ask, and why it stopped waiting early. */
static const char* waitingId;
static int    cancelled;
static int    clientGone;
static int    watchingClient = 1;

/* Looks at what the client sent meanwhile without taking it, so queued lines keep their order. */
static int watchClient(void)
{
    static char peek[65536];
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD  got = 0;
    char*  line;
    char*  newline;

    if (!watchingClient || GetFileType(input) != FILE_TYPE_PIPE) {
        return 0;
    }
    if (!PeekNamedPipe(input, peek + 1, sizeof(peek) - 2, &got, NULL, NULL)) {
        DWORD error = GetLastError();

        if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
            clientGone = 1;
            return 1;
        }
        /* Not a pipe that can be peeked, e.g. a socket given as stdin. */
        watchingClient = 0;
        return 0;
    }
    peek[got + 1] = 0;
    /* The CRT reads stdin 2 bytes at a time, so it may hold the next line's { already. */
    line = peek + 1;
    if (got > 0 && peek[1] != '{') {
        peek[0] = '{';
        line = peek;
    }
    for (; (newline = strchr(line, '\n')) != NULL; line = newline + 1) {
        char method[64];
        char params[1024];
        char id[6 * MCP_ID_TEXT_MAX + 16];

        *newline = 0;
        if (jsonGetString(line, "method", method, sizeof(method)) &&
            strcmp(method, "notifications/cancelled") == 0 &&
            jsonGetObject(line, "params", params, sizeof(params)))
        {
            mcpIdAsJson(params, "requestId", id, sizeof(id));
            if (strcmp(id, waitingId) == 0) {
                cancelled = 1;
                return 1;
            }
        }
    }
    return 0;
}

/* Watches the client meanwhile: a cancel or a closed stdin ends the wait and the connection. */
static int waitForReply(void)
{
    for (;;) {
        fd_set readable;
        struct timeval slice = { 0, 200000 };

        FD_ZERO(&readable);
        FD_SET(emulator, &readable);
        if (select(0, &readable, NULL, NULL, &slice) != 0) {
            return readLine(socketLine, REPLY_MAX);
        }
        if (watchClient()) {
            return 0;
        }
    }
}

/* Sends one line and reads the reply into socketLine, reattaching once if the emulator was restarted. */
static int ask(const char* line, char* problem, int problemLen)
{
    int attempt;

    /* A newline would split this into two requests and shift every later reply by one. */
    if (strchr(line, '\n') != NULL || strchr(line, '\r') != NULL) {
        _snprintf(problem, (size_t)problemLen,
                  "an argument contains a line break, which a request cannot carry");
        return 0;
    }

    for (attempt = 0; attempt < 2; attempt++) {
        if (!attach(problem, problemLen)) {
            return 0;
        }
        if (sendAll(line, (int)strlen(line)) && sendAll("\n", 1) && waitForReply()) {
            return 1;
        }
        /* Closing the connection is what ends the emulator's side of an abandoned call. */
        detach();
        if (cancelled || clientGone) {
            return 0;
        }
    }
    _snprintf(problem, (size_t)problemLen, "blueMSX+ stopped answering");
    return 0;
}

void mcpLinkNotes(char* note, int noteLen)
{
    note[0] = 0;
    if (previousPid != 0) {
        _snprintf(note, (size_t)noteLen,
                  "Note: this is a different blueMSX+ process (pid %d, %s) from the one "
                  "this session used before (pid %d). Nothing set up in the earlier one "
                  "carries over.\n", attachedPid, mcpInstanceMachineShown(attachedMachine),
                  previousPid);
        note[noteLen - 1] = 0;
    }
}

void mcpLinkNotesShown(void)
{
    previousPid        = 0;
}

int mcpLinkAsk(const char* line, const char* id, char* problem, int problemLen)
{
    waitingId = id;
    cancelled = 0;
    if (ask(line, problem, problemLen)) {
        return MCP_LINK_OK;
    }
    return cancelled ? MCP_LINK_CANCELLED : clientGone ? MCP_LINK_CLIENT_GONE : MCP_LINK_FAILED;
}

const char* mcpLinkReply(void)
{
    return socketLine;
}

void mcpLinkEnd(void)
{
    detach();
}
