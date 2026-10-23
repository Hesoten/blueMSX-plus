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

/* Checks only the shape of a call; every judgement about the machine belongs to the emulator. */

#include "McpTools.h"
#include <stdio.h>
#include <string.h>

#include "ControlProtocol.h"

/* Use only type, properties, required, enum, description, default, minimum, maximum:
** some clients reject oneOf, $ref and format. */
#define SCHEMA(body) "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\"," \
                     "\"type\":\"object\"," body "}"

#define READ_ONLY "\"readOnlyHint\":true,\"destructiveHint\":false," \
                  "\"idempotentHint\":true,\"openWorldHint\":false"
#define CHANGES   "\"readOnlyHint\":false,\"destructiveHint\":false," \
                  "\"idempotentHint\":false,\"openWorldHint\":false"

const McpTool mcpTools[] = {
{
    "bmsx_status",
    "Where the emulated MSX is: run state, why it last stopped, which segment is "
    "paged into each Z80 page and, while it is paused, every CPU register and "
    "every VDP register with the screen mode they add up to. The VDP registers "
    "are write only to the Z80, so this is the only way to see what the program "
    "set them to -- start here when the screen is wrong. It also says whether a V9990 "
    "picture (GFX9000, Video9000) is shown.",
    SCHEMA("\"properties\":{}"),
    READ_ONLY,
    "status",
    NULL
},
{
    "bmsx_slots",
    "The slot map: what fills every slot and subslot and which segment each Z80 "
    "page is showing.",
    SCHEMA("\"properties\":{}"),
    READ_ONLY,
    "slots",
    NULL
},
{
    "bmsx_run",
    "Pause the machine, let it go again, or step. step runs count instructions (1 by "
    "default).",
    SCHEMA("\"properties\":{"
           "\"how\":{\"type\":\"string\",\"enum\":[\"pause\",\"resume\",\"step\"]},"
           "\"count\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_RUN_COUNT_DEFAULT)
           ",\"minimum\":" CTRL_STR(CTRL_RUN_COUNT_MIN) ","
           "\"maximum\":" CTRL_STR(CTRL_RUN_COUNT_MAX)
           ",\"description\":\"instructions, for step\"}},"
           "\"required\":[\"how\"]"),
    CHANGES,
    "run",
    NULL
},
{
    "bmsx_run_until",
    "Let the machine run and wait for it to stop, reporting why: do not poll "
    "bmsx_status in a loop. With "
    "pause_at_timeout, a wait that times out pauses the machine, so it can be read.",
    SCHEMA("\"properties\":{"
           "\"timeout_ms\":{\"type\":\"integer\",\"default\":"
           CTRL_STR(CTRL_RUN_UNTIL_TIMEOUT_MS_DEFAULT) ","
           "\"minimum\":" CTRL_STR(CTRL_RUN_UNTIL_TIMEOUT_MS_MIN) ",\"maximum\":"
           CTRL_STR(CTRL_RUN_UNTIL_TIMEOUT_MS_MAX) "},"
           "\"pause_at_timeout\":{\"type\":\"boolean\",\"default\":false,"
           "\"description\":\"pause the machine when the wait times out\"}}"),
    CHANGES,
    "run.until",
    NULL
},
{
    "bmsx_screen",
    "What is on the screen. In a text mode it comes back as text, which is far "
    "cheaper to read than an image: the character codes the program wrote, not the "
    "shapes drawn, with a note for cleared patterns, invisible text colour and a "
    "display that is off. In a graphics mode it is written as a PNG and the path "
    "comes back. The text is read only from a paused machine; while it runs, auto "
    "gives the PNG. While a V9990 picture (GFX9000, Video9000) is shown, auto gives the "
    "PNG and text is labelled as the VDP's.",
    SCHEMA("\"properties\":{"
           "\"as\":{\"type\":\"string\",\"enum\":[\"auto\",\"text\",\"png\"],"
           "\"default\":\"" CTRL_SCREEN_AS_DEFAULT "\"},"
           "\"path\":{\"type\":\"string\",\"description\":\"a full Windows path of a new "
           "file to write the PNG to (as=png is implied); an existing file is refused, never "
           "replaced; Full access only\"}}"),
    READ_ONLY,
    "screen",
    NULL
},
};

const int mcpToolCount = (int)(sizeof(mcpTools) / sizeof(mcpTools[0]));

const McpTool* mcpToolFind(const char* name)
{
    int i;

    for (i = 0; i < mcpToolCount; i++) {
        if (strcmp(mcpTools[i].name, name) == 0) {
            return &mcpTools[i];
        }
    }
    return NULL;
}

/* ---- the check --------------------------------------------------------- */

/* The schema's properties as JSON, or with a key the one property; 0 when it has none. */
static int schemaProperty(const McpTool* tool, const char* key, char* out, int outLen)
{
    static char properties[8192];

    if (!jsonGetObject(tool->schema, "properties", key != NULL ? properties : out,
                       key != NULL ? (int)sizeof(properties) : outLen))
    {
        return 0;
    }
    return key == NULL || jsonGetObject(properties, key, out, outLen);
}

/* The next string of a JSON array of plain strings, as in a schema, from *at on; 0 at its end. */
static int listNext(const char** at, const char** word, int* length)
{
    const char* s = strchr(*at, '"');
    const char* end = s != NULL ? strchr(s + 1, '"') : NULL;

    if (end == NULL) {
        return 0;
    }
    *word   = s + 1;
    *length = (int)(end - s - 1);
    *at     = end + 1;
    return 1;
}

/* Non-zero when such an array holds exactly name. */
static int listHas(const char* list, const char* name)
{
    const char* word;
    int length;

    while (listNext(&list, &word, &length)) {
        if ((size_t)length == strlen(name) && strncmp(word, name, (size_t)length) == 0) {
            return 1;
        }
    }
    return 0;
}

/* "x has no argument y; it takes ...", naming the arguments the schema has. */
static void refuseUnknownArgument(const McpTool* tool, const char* properties, const char* key,
                                  char* out, int outLen)
{
    char names[512];
    char name[64];
    int  n;

    names[0] = 0;
    for (n = 0; jsonKeyAt(properties, n, name, sizeof(name)); n++) {
        if (strlen(names) + strlen(name) + 3 < sizeof(names)) {
            strcat(names, n > 0 ? ", " : "");
            strcat(names, name);
        }
    }
    _snprintf(out, (size_t)outLen, "%s has no argument \"%s\"; it takes %s.",
              tool->name, key, names[0] != 0 ? names : "none");
    out[outLen - 1] = 0;
}

/* A value of the type the schema gives, and for a string one of its enum, if it has one. */
static int checkArgumentValue(const McpTool* tool, const char* arguments, const char* key,
                              const char* property, char* out, int outLen)
{
    static char value[MCP_LINE_MAX];
    char  choices[512];
    char  type[16];
    Int32 number;
    int   flag;

    type[0] = 0;
    jsonGetString(property, "type", type, sizeof(type));
    if ((strcmp(type, "integer") == 0 && !jsonGetInt(arguments, key, &number)) ||
        (strcmp(type, "string") == 0 && !jsonGetString(arguments, key, value, sizeof(value))) ||
        (strcmp(type, "boolean") == 0 && !jsonGetBool(arguments, key, &flag)))
    {
        _snprintf(out, (size_t)outLen, "%s: \"%s\" must be %s.", tool->name, key,
                  strcmp(type, "integer") == 0 ? CTRL_TEXT_WANT_INT :
                  strcmp(type, "string") == 0  ? CTRL_TEXT_WANT_STRING :
                  strcmp(type, "string") == 0  ? CTRL_TEXT_WANT_STRING : CTRL_TEXT_WANT_BOOL);
        out[outLen - 1] = 0;
        return 0;
    }
    /* An enum value nobody handles would otherwise fall through to a default. */
    if (strcmp(type, "string") == 0 &&
        jsonGetObject(property, "enum", choices, sizeof(choices)) &&
        !listHas(choices, value))
    {
        _snprintf(out, (size_t)outLen, "%s: \"%s\" must be one of %s.", tool->name, key,
                  choices);
        out[outLen - 1] = 0;
        return 0;
    }
    return 1;
}

/* Every argument the schema lists as required is given, and not null. */
static int checkRequired(const McpTool* tool, const char* arguments, char* out, int outLen)
{
    char required[512];
    char key[64];
    const char* s = required;
    const char* word;
    int length;

    if (!jsonGetObject(tool->schema, "required", required, sizeof(required))) {
        return 1;
    }
    while (listNext(&s, &word, &length)) {
        if (length < (int)sizeof(key)) {
            memcpy(key, word, (size_t)length);
            key[length] = 0;
            if (!jsonHasKey(arguments, key) || jsonIsNull(arguments, key)) {
                _snprintf(out, (size_t)outLen, "%s needs \"%s\".", tool->name, key);
                out[outLen - 1] = 0;
                return 0;
            }
        }
    }
    return 1;
}

int mcpToolCheck(const McpTool* tool, const char* arguments, char* out, int outLen)
{
    static char properties[8192];
    char key[64];
    int  i;

    if (!schemaProperty(tool, NULL, properties, sizeof(properties))) {
        strcpy(properties, "{}");
    }
    for (i = 0; jsonKeyAt(arguments, i, key, sizeof(key)); i++) {
        char property[2048];

        if (jsonIsNull(arguments, key)) {
            continue;
        }
        if (!schemaProperty(tool, key, property, sizeof(property))) {
            refuseUnknownArgument(tool, properties, key, out, outLen);
            return 0;
        }
        if (!checkArgumentValue(tool, arguments, key, property, out, outLen)) {
            return 0;
        }
    }
    return checkRequired(tool, arguments, out, outLen);
}

/* ---- the request ------------------------------------------------------- */

/* Set when an argument the check passed could not be read again for the request. */
static int copyFailed;

/* One argument as its type says: an integer unclamped, so the emulator judges its range.
** 0 when unreadable. */
static int copyValue(const char* arguments, const char* key, const char* type, JsonOut* request)
{
    static char text[MCP_LINE_MAX];
    Int64 number;
    int   flag;

    if (strcmp(type, "boolean") == 0) {
        if (!jsonGetBool(arguments, key, &flag)) {
            return 0;
        }
        jsonOutBool(request, key, flag);
        return 1;
    }
    if (strcmp(type, "string") == 0) {
        if (!jsonGetString(arguments, key, text, sizeof(text))) {
            return 0;
        }
        jsonOutString(request, key, text);
        return 1;
    }
    if (strcmp(type, "integer") == 0 && jsonGetInt64(arguments, key, &number)) {
        jsonOutInt64(request, key, number);
        return 1;
    }
    return 0;
}

void mcpToolCopy(const McpTool* tool, const char* arguments, JsonOut* request, const char* except)
{
    char key[64];
    char property[2048];
    char type[16];
    int  i;

    for (i = 0; jsonKeyAt(arguments, i, key, sizeof(key)); i++) {
        if (strcmp(key, "op") == 0 || (except != NULL && strcmp(key, except) == 0) ||
            jsonIsNull(arguments, key))
        {
            continue;
        }
        type[0] = 0;
        if (schemaProperty(tool, key, property, sizeof(property))) {
            jsonGetString(property, "type", type, sizeof(type));
        }
        if (!copyValue(arguments, key, type, request)) {
            copyFailed = 1;
        }
    }
}

int mcpToolRequest(const McpTool* tool, const char* arguments, char* line, int lineLen)
{
    JsonOut request;

    copyFailed = 0;
    jsonOutInit(&request, line, lineLen);
    jsonOutBeginObject(&request);
    if (tool->build != NULL) {
        tool->build(tool, arguments, &request);
    }
    else {
        jsonOutString(&request, "op", tool->op);
        mcpToolCopy(tool, arguments, &request, NULL);
    }
    jsonOutEndObject(&request);
    if (copyFailed) {
        return -1;
    }
    return jsonOutOverflowed(&request) ? 0 : 1;
}
