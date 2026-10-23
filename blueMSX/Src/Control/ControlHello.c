/*****************************************************************************
**
** Agent connection handshake.
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
#include "ControlHello.h"
#include "ControlJson.h"
#include "ControlSession.h"
#include <stdio.h>
#include <string.h>

/* A refused hello: the reply is copied as it is, never used as a format. */
static CtrlClient helloRefused(char* reply, int replyLen, const char* text)
{
    strncpy(reply, text, (size_t)replyLen - 1);
    reply[replyLen - 1] = 0;
    return CTRL_CLIENT_NONE;
}

CtrlClient ctrlHelloAnswer(const char* line, const char* token, char* reply, int replyLen)
{
    char       given[64];
    char       label[CTRL_CLIENT_LABEL];
    Int32      owner = 0;
    CtrlClient client;
    int        dropped;

    if (strstr(line, "\"op\"") == NULL || strstr(line, "hello") == NULL) {
        return helloRefused(reply, replyLen,
                            "{\"ok\":false,\"error\":\"expected_hello\","
                            "\"message\":\"the first line must be "
                            "{\\\"op\\\":\\\"hello\\\",\\\"token\\\":\\\"...\\\"}\"}");
    }
    if (!jsonGetString(line, "token", given, sizeof(given)) || strcmp(given, token) != 0) {
        return helloRefused(reply, replyLen,
                            "{\"ok\":false,\"error\":\"bad_token\","
                            "\"message\":\"the token is in the instance file under "
                            "%LOCALAPPDATA%\\\\blueMSXplus\\\\agent\"}");
    }
    label[0] = 0;
    jsonGetString(line, "client", label, sizeof(label));
    jsonGetInt(line, "owner", &owner);
    client = ctrlClientOpen(label[0] != 0 ? label : "unnamed client");
    if (client == CTRL_CLIENT_NONE) {
        return helloRefused(reply, replyLen,
                            "{\"ok\":false,\"error\":\"blocked\",\"message\":"
                            "\"this emulator is not accepting connections\"}");
    }
    dropped = ctrlSessionOpen(client, (int)owner);
    snprintf(reply, (size_t)replyLen,
             "{\"ok\":true,\"protocol\":1,\"level\":\"%s\",\"dropped\":%d,"
             "\"message\":\"send {\\\"op\\\":\\\"help\\\"}\"}",
             ctrlGateLevelText(ctrlGateLevel()), dropped);
    return client;
}
