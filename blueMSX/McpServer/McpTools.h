/*****************************************************************************
**
** MCP tool table.
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
#ifndef MCP_TOOLS_H
#define MCP_TOOLS_H

#include "ControlJson.h"

/* The longest line the server reads from its client, and so the longest argument. */
#define MCP_LINE_MAX (1024 * 1024)

typedef struct McpTool McpTool;

/* Writes the members of the request a call sends, into an object begun and not yet ended. */
typedef void (*McpToolBuild)(const McpTool* tool, const char* arguments, JsonOut* request);

struct McpTool {
    const char*  name;
    const char*  description;
    const char*  schema;
    const char*  annotations;
    /* The op sent, whole; for buildByOp and buildSymbols its start, which the "op" argument
    ** ends. */
    const char*  op;
    McpToolBuild build;          /* NULL: "op" is op, and every argument is copied */
};

extern const McpTool mcpTools[];
extern const int     mcpToolCount;

const McpTool* mcpToolFind(const char* name);

/* Refuses an argument the tool does not have, a required one left out, and a value of the
** wrong kind, with the reason in out; 1 when the call may go on. Null is absent. */
int  mcpToolCheck(const McpTool* tool, const char* arguments, char* out, int outLen);

/* Copies each argument, in its order, as its schema's type says; skips "op", except and
** nulls. */
void mcpToolCopy(const McpTool* tool, const char* arguments, JsonOut* request, const char* except);

/* The request line for a checked call: 1; 0 when it is longer than a request line may be;
** -1 when an argument could not be read again after mcpToolCheck passed it. */
int  mcpToolRequest(const McpTool* tool, const char* arguments, char* line, int lineLen);

#endif
