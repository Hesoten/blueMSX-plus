/*****************************************************************************
**
** Emulator instance selection for the MCP server.
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
#ifndef MCP_INSTANCE_H
#define MCP_INSTANCE_H

#include <stddef.h>

typedef struct {
    int  pid;
    int  port;
    char token[64];
    char machine[128];
} McpInstance;

#define MCP_INSTANCE_MAX 32

/* The --instance the server is pinned to, length bytes of pin; empty for none. */
void mcpInstancePin(const char* pin, size_t length);
const char* mcpInstancePinned(void);

/* The live instances into list, and the ones this server may attach to into picked (both
** MCP_INSTANCE_MAX long), only onlyPid's when it is not 0; returns how many were picked. */
int mcpInstancePick(int onlyPid, McpInstance* list, int* picked, int* live, int* stored);

/* Why no instance was picked, or several, and what to do: into problem, JSON-escaped.
** pinText is the pin, JSON-escaped; ExplainNone reuses picked as scratch. */
void mcpInstanceExplainNone(const McpInstance* list, int* picked, int live, int stored,
                            const char* pinText, char* problem, int problemLen);
void mcpInstanceExplainMany(const McpInstance* list, const int* picked, int count, int live,
                            int stored, const char* pinText, char* problem, int problemLen);

/* An emulator names its machine in the instance file only once one has started. */
const char* mcpInstanceMachineShown(const char* machine);

#endif
