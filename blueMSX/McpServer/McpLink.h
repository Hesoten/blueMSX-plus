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
#ifndef MCP_LINK_H
#define MCP_LINK_H

#include <stddef.h>

/* A string id at least this long is answered with a null id. */
#define MCP_ID_TEXT_MAX 128

/* Echoes the id at key back as the same JSON type, number or string, it arrived as;
** the link reads the id of a cancel the same way. */
void mcpIdAsJson(const char* request, const char* key, char* out, int outLen);

/* Picks the owner number this server gives in each hello, as Windows reuses a pid. */
void mcpLinkSetOwner(void);

#define MCP_LINK_OK          0
#define MCP_LINK_FAILED      1   /* problem says why, JSON-escaped */
#define MCP_LINK_CANCELLED   2   /* the client cancelled the call id */
#define MCP_LINK_CLIENT_GONE 3   /* the client closed stdin */

/* Sends one request line for the call id and waits for its reply, attaching first and
** once more if the emulator was restarted; watches the client for a cancel meanwhile. */
int mcpLinkAsk(const char* line, const char* id, char* problem, int problemLen);
/* The reply line mcpLinkAsk read. */
const char* mcpLinkReply(void);

/* What a reply also has to say: another emulator process than before, and plain
** breakpoints a hello said were dropped. Shown, they are cleared with mcpLinkNotesShown. */
void mcpLinkNotes(char* note, int noteLen);
void mcpLinkNotesShown(void);

/* At a normal exit: ends the session, so the emulator drops this server's breakpoints. */
void mcpLinkEnd(void);

#endif
