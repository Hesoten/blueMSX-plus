/*****************************************************************************
**
** README reference lookup for the MCP server.
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
#include "McpReference.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* README.md, compiled in as a resource, is the reference; a topic picks the
** "## " section whose title contains it, looking in the agent's part first, and
** no match lists the titles. */
void mcpReferenceSection(const char* topic, char* out, int outLen)
{
    HRSRC   found = FindResourceA(NULL, "README", (LPCSTR)RT_RCDATA);
    HGLOBAL loaded = found != NULL ? LoadResource(NULL, found) : NULL;
    const char* text = loaded != NULL ? (const char*)LockResource(loaded) : NULL;
    int   size = found != NULL ? (int)SizeofResource(NULL, found) : 0;
    int   written = 0;
    int   pass;

    out[0] = 0;
    if (text == NULL || size <= 0) {
        _snprintf(out, (size_t)outLen, "the reference is missing from this build");
        out[outLen - 1] = 0;
        return;
    }

    /* Pass -1 copies the matching section of the agent's part, pass 0 of the whole
    ** file, and pass 1, when nothing matched, lists the titles. */
    for (pass = topic[0] != 0 ? -1 : 1; pass < 2 && written == 0; pass++) {
        int inside = 0;
        int at = 0;

        while (pass == -1 && at < size &&
               (size - at < 15 || strncmp(text + at, "# For the agent", 15) != 0))
        {
            at++;
        }
        if (pass == 1) {
            written = _snprintf(out, (size_t)outLen, "%s%s",
                                topic[0] != 0 ? "no section title contains that word; " : "",
                                "sections:\n");
        }
        while (at < size) {
            int end = at;
            int isTitle;

            while (end < size && text[end] != '\n') {
                end++;
            }
            isTitle = end - at > 3 && strncmp(text + at, "## ", 3) == 0;
            /* A "# " part heading ends the section before it. */
            if (pass <= 0 && inside && strncmp(text + at, "# ", 2) == 0) {
                break;
            }
            if (pass <= 0 && isTitle) {
                char title[128];
                int  length = end - at - 3 < (int)sizeof(title) - 1 ? end - at - 3
                                                                    : (int)sizeof(title) - 1;
                memcpy(title, text + at + 3, (size_t)length);
                title[length] = 0;
                inside = strstr(_strlwr(title), topic) != NULL && written == 0;
                if (!inside && written > 0) {
                    break;
                }
            }
            if ((pass <= 0 && inside) || (pass == 1 && isTitle)) {
                int i;
                for (i = pass == 1 ? at + 3 : at; i < end && written < outLen - 2; i++) {
                    if (text[i] != '\r') {
                        out[written++] = text[i];
                    }
                }
                if (written < outLen - 1) {
                    out[written++] = '\n';
                }
            }
            at = end + 1;
        }
        out[written] = 0;
    }
}
