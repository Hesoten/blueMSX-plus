/*****************************************************************************
**
** MCP server main loop.
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

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ControlJson.h"
#include "ControlProtocol.h"
#include "McpInstance.h"
#include "McpLink.h"
#include "McpReference.h"
#include "McpTools.h"

static char inLine[MCP_LINE_MAX];
static char outLine[MCP_LINE_MAX];

/* ---- MCP --------------------------------------------------------------- */

static void emit(const char* line)
{
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void replyError(const char* id, int code, const char* message)
{
    _snprintf(outLine, MCP_LINE_MAX,
              "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
              id, code, message);
    emit(outLine);
}

/* A failed tool reports inside the result with isError, not as a protocol error. */
static void replyToolText(const char* id, const char* escapedText, int isError)
{
    _snprintf(outLine, MCP_LINE_MAX,
              "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":"
              "[{\"type\":\"text\",\"text\":\"%s\"}],\"isError\":%s}}",
              id, escapedText, isError ? "true" : "false");
    emit(outLine);
}

/* Set when escapeInto had to cut text short; renderReply clears and checks it. */
static int escapeCut;

static void escapeInto(const char* text, char* out, int outLen)
{
    if (!jsonEscape(text, out, outLen)) {
        escapeCut = 1;
    }
}

static void listTools(const char* id)
{
    int written;
    int i;

    written = _snprintf(outLine, MCP_LINE_MAX,
                        "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":[", id);
    for (i = 0; i < mcpToolCount; i++) {
        char description[2048];

        escapeInto(mcpTools[i].description, description, sizeof(description));
        written += _snprintf(outLine + written, MCP_LINE_MAX - written,
                             "%s{\"name\":\"%s\",\"description\":\"%s\","
                             "\"inputSchema\":%s,\"annotations\":{%s}}",
                             i > 0 ? "," : "", mcpTools[i].name, description,
                             mcpTools[i].schema, mcpTools[i].annotations);
    }
    _snprintf(outLine + written, MCP_LINE_MAX - written, "]}}");
    emit(outLine);
}

/* params.arguments as an object, {} when absent or null; 0 when it is something else,
** which is answered here. */
static int readArguments(const char* id, const char* params, char* arguments, int argumentsLen)
{
    /* jsonGetObject also copies an array, which has no named arguments. */
    if (!jsonGetObject(params, "arguments", arguments, argumentsLen) ||
        arguments[0] != '{')
    {
        if (jsonHasKey(params, "arguments") && !jsonIsNull(params, "arguments")) {
            replyError(id, -32602, "params.arguments must be a JSON object");
            return 0;
        }
        strcpy(arguments, "{}");
    }
    return 1;
}

/* bmsx_reference is answered here, from the guide compiled into this server. */
static void callReference(const char* id, const char* arguments, char* text, int textLen,
                          char* escaped, int escapedLen)
{
    char   topic[64];
    size_t length;

    if (!jsonGetString(arguments, "topic", topic, sizeof(topic))) {
        topic[0] = 0;
    }
    /* A copied section starts with its "## " title; a miss retries without a trailing s. */
    length = strlen(_strlwr(topic));
    mcpReferenceSection(topic, text, textLen);
    if (strncmp(text, "## ", 3) != 0 && length > 3 && topic[length - 1] == 's') {
        topic[length - 1] = 0;
        mcpReferenceSection(topic, text, textLen);
    }
    escapeInto(text, escaped, escapedLen);
    replyToolText(id, escaped, 0);
}

/* The emulator's reply in mcpLinkReply() as the tool's result, after the notes. */
static void renderReply(const char* id, const char* note, char* text, int textLen,
                        char* escaped, int escapedLen)
{
    int ok = 0;

    jsonGetBool(mcpLinkReply(), "ok", &ok);
    if (!ok) {
        char code[48];
        static char message[8192];

        jsonGetString(mcpLinkReply(), "error", code, sizeof(code));
        if (!jsonGetString(mcpLinkReply(), "message", message, sizeof(message))) {
            strcpy(message, "(the emulator's message could not be read)");
        }
        _snprintf(text, (size_t)textLen, "%s%s\n%s", note, code, message);
        escapeInto(text, escaped, escapedLen);
        replyToolText(id, escaped, 1);
        mcpLinkNotesShown();
        return;
    }

    if (!jsonGetString(mcpLinkReply(), "text", text, textLen) &&
        jsonHasKey(mcpLinkReply(), "text"))
    {
        replyToolText(id, "blueMSX+ answered, but its reply text was too long or badly escaped.", 1);
        return;
    }
    escapeCut = 0;
    escapeInto(note, escaped, escapedLen);
    escapeInto(text, escaped + strlen(escaped), escapedLen - (int)strlen(escaped));
    if (escapeCut) {
        replyToolText(id, "blueMSX+ answered, but its reply was too long to pass on.", 1);
        return;
    }
    replyToolText(id, escaped, 0);
    /* Cleared only once shown, so a reply that could not carry it leaves it for the next. */
    mcpLinkNotesShown();
}

/* The request for a checked call in controlLine; 0 when it could not be built, which is
** answered here. */
static int prepareRequest(const char* id, const McpTool* tool, const char* arguments,
                          char* controlLine, int controlLineLen, char* text, int textLen,
                          char* escaped, int escapedLen)
{
    int built = mcpToolRequest(tool, arguments, controlLine, controlLineLen);

    if (built > 0) {
        return 1;
    }
    if (built == 0) {
        _snprintf(text, (size_t)textLen, "line_too_long\n" CTRL_TEXT_LINE_TOO_LONG,
                  CTRL_LINE_MAX - 1);
    }
    else {
        _snprintf(text, (size_t)textLen, "internal\n%s: an argument the check passed could not be "
                  "read again", tool->name);
    }
    text[textLen - 1] = 0;
    escapeInto(text, escaped, escapedLen);
    replyToolText(id, escaped, 1);
    return 0;
}

static void callTool(const char* id, const char* request)
{
    static char arguments[MCP_LINE_MAX];
    static char controlLine[CTRL_LINE_MAX];
    static char problem[4096];
    static char text[MCP_LINE_MAX / 2];
    static char escaped[MCP_LINE_MAX / 2];
    static char params[MCP_LINE_MAX];
    char   name[64];
    char   note[800];
    const McpTool* tool;
    int    asked;

    if (!jsonGetObject(request, "params", params, sizeof(params)) ||
        !jsonGetString(params, "name", name, sizeof(name)))
    {
        replyError(id, -32602, "a tools/call needs params.name");
        return;
    }
    if (!readArguments(id, params, arguments, sizeof(arguments))) {
        return;
    }
    tool = mcpToolFind(name);
    if (tool == NULL) {
        replyError(id, -32602, "no tool by that name; tools/list has them");
        return;
    }
    if (!mcpToolCheck(tool, arguments, text, sizeof(text))) {
        escapeInto(text, escaped, sizeof(escaped));
        replyToolText(id, escaped, 1);
        return;
    }
    if (tool->op == NULL && tool->build == NULL) {
        callReference(id, arguments, text, sizeof(text), escaped, sizeof(escaped));
        return;
    }
    if (!prepareRequest(id, tool, arguments, controlLine, sizeof(controlLine), text, sizeof(text),
                        escaped, sizeof(escaped)))
    {
        return;
    }

    asked = mcpLinkAsk(controlLine, id, problem, sizeof(problem));
    /* A cancelled call gets no reply, and a closed client cannot read one. */
    if (asked == MCP_LINK_CANCELLED || asked == MCP_LINK_CLIENT_GONE) {
        return;
    }
    if (asked != MCP_LINK_OK) {
        /* The link leaves problem JSON-escaped. */
        replyToolText(id, problem, 1);
        return;
    }
    mcpLinkNotes(note, sizeof(note));
    renderReply(id, note, text, sizeof(text), escaped, sizeof(escaped));
}

static void handle(const char* request)
{
    char id[6 * MCP_ID_TEXT_MAX + 16];
    char method[64];

    mcpIdAsJson(request, "id", id, sizeof(id));
    if (!jsonGetString(request, "method", method, sizeof(method))) {
        return;
    }

    if (strcmp(method, "initialize") == 0) {
        _snprintf(outLine, MCP_LINE_MAX,
                  "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{"
                  "\"protocolVersion\":\"2025-06-18\","
                  "\"capabilities\":{\"tools\":{}},"
                  "\"serverInfo\":{\"name\":\"bluemsx\",\"version\":\"1.0\"},"
                  "\"instructions\":\"Debugging an MSX program running in blueMSX+. "
                  "Memory, registers and the text screen are read only while the machine "
                  "is paused: pause it (bmsx_run how=pause) or let it run to a stop "
                  "with bmsx_run_until (Full access) first. "
                  "Addresses these tools print can be sent straight back to them; do "
                  "not work out slot or segment arithmetic yourself. To wait for a "
                  "breakpoint use bmsx_run_until rather than polling bmsx_status. To "
                  "find a variable you have no symbol for, use bmsx_hunt: start, let "
                  "the machine change the value, refine by how it changed, repeat. "
                  "Address forms, symbols per segment and more recipes: bmsx_reference.\"}}",
                  id);
        emit(outLine);
        return;
    }
    if (strcmp(method, "notifications/initialized") == 0 ||
        strncmp(method, "notifications/", 14) == 0)
    {
        return;
    }
    if (strcmp(method, "ping") == 0) {
        _snprintf(outLine, MCP_LINE_MAX,
                  "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}", id);
        emit(outLine);
        return;
    }
    if (strcmp(method, "tools/list") == 0) {
        listTools(id);
        return;
    }
    if (strcmp(method, "tools/call") == 0) {
        callTool(id, request);
        return;
    }

    replyError(id, -32601, "no such method");
}

/* --instance and --help; returns -1 to go on serving, else the exit code. */
static int readCommandLine(int argc, char** argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--instance") == 0 && i + 1 < argc) {
            const char* pin = argv[++i];
            size_t length;

            /* Spaces around it are a typing slip; nothing left would mean unpinned, so it is refused. */
            while (*pin == ' ' || *pin == '\t') {
                pin++;
            }
            length = strlen(pin);
            while (length > 0 && (pin[length - 1] == ' ' || pin[length - 1] == '\t')) {
                length--;
            }
            if (length > 0) {
                mcpInstancePin(pin, length);
            }
            else {
                fprintf(stderr, "--instance needs a pid or a machine name\n");
                return 2;
            }
        }
        else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "blueMSX+ MCP server. Speaks MCP on stdin and stdout, and\n"
                    "connects to a running blueMSX+ that has agent control turned on.\n"
                    "\n"
                    "  --instance <pid or machine name>   pick one of several emulators\n"
                    "\n"
                    "Register it with an MCP client by pointing the client at this\n"
                    "executable. It does not need to be running before the emulator.\n");
            return 0;
        }
    }
    return -1;
}

/* Answers each line on stdin until it closes. */
static void serve(void)
{
    while (fgets(inLine, MCP_LINE_MAX, stdin) != NULL) {
        char*  start = inLine;
        size_t length = strlen(inLine);

        while (length > 0 && (inLine[length - 1] == '\n' || inLine[length - 1] == '\r')) {
            inLine[--length] = 0;
        }
        /* Skip a UTF-8 byte order mark some writers put before the first line. */
        if ((unsigned char)start[0] == 0xEF &&
            (unsigned char)start[1] == 0xBB &&
            (unsigned char)start[2] == 0xBF)
        {
            start += 3;
        }
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        if (*start == 0) {
            continue;
        }
        handle(start);
    }
}

int main(int argc, char** argv)
{
    WSADATA wsaData;
    int exitCode = readCommandLine(argc, argv);

    if (exitCode >= 0) {
        return exitCode;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "could not start winsock\n");
        return 1;
    }

    mcpLinkSetOwner();

    /* Stdout carries only protocol messages; everything else goes to stderr. */
    setvbuf(stdout, NULL, _IOFBF, 65536);
    /* Unbuffered: the CRT keeps at most the next line's first byte, which watchClient restores. */
    setvbuf(stdin, NULL, _IONBF, 0);

    serve();

    mcpLinkEnd();
    WSACleanup();
    return 0;
}
