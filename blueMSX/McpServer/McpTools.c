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

#define AT_PROPERTY \
    "\"at\":{\"type\":\"string\",\"description\":" \
    "\"Where. One of: 0xC000 (what the Z80 sees now), slot1.0:seg5+0x100 (a " \
    "segment of a slot, whether or not it is paged in), ram:seg12+0x1A30 (the " \
    "memory mapper in page 3), vram:0x1B00 (as the program addresses video memory " \
    "through the VDP). Addresses printed by these tools can be " \
    "sent straight back.\"}"

#define READ_ONLY "\"readOnlyHint\":true,\"destructiveHint\":false," \
                  "\"idempotentHint\":true,\"openWorldHint\":false"
#define CHANGES   "\"readOnlyHint\":false,\"destructiveHint\":false," \
                  "\"idempotentHint\":false,\"openWorldHint\":false"
#define DESTROYS  "\"readOnlyHint\":false,\"destructiveHint\":true," \
                  "\"idempotentHint\":true,\"openWorldHint\":false"
/* A second undo reverts a different write. */
#define DESTROYS_NOT_IDEMPOTENT "\"readOnlyHint\":false,\"destructiveHint\":true," \
                                "\"idempotentHint\":false,\"openWorldHint\":false"

static void buildByOp(const McpTool* tool, const char* arguments, JsonOut* request);
static void buildSymbols(const McpTool* tool, const char* arguments, JsonOut* request);

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
    "The slot map: what fills every slot and subslot, which segment each Z80 "
    "page is showing, and what else is in the machine that is not paged in "
    "right now but can still be read.",
    SCHEMA("\"properties\":{}"),
    READ_ONLY,
    "slots",
    NULL
},
{
    "bmsx_mem_read",
    "Read memory as a hex dump. Works on segments that are not currently "
    "paged in, which is where most of a mapper program lives.",
    SCHEMA("\"properties\":{" AT_PROPERTY ","
           "\"count\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_MEM_READ_COUNT_DEFAULT) ","
           "\"minimum\":" CTRL_STR(CTRL_MEM_READ_COUNT_MIN) ",\"maximum\":"
           CTRL_STR(CTRL_MEM_READ_COUNT_MAX) "}},\"required\":[\"at\"]"),
    READ_ONLY,
    "mem.read",
    NULL
},
{
    "bmsx_mem_resolve",
    "What an address turns out to be right now: which slot and segment, "
    "whether the Z80 can currently see it and at what address, and what the "
    "slot holds.",
    SCHEMA("\"properties\":{" AT_PROPERTY "},\"required\":[\"at\"]"),
    READ_ONLY,
    "mem.resolve",
    NULL
},
{
    "bmsx_run",
    "Pause the machine, let it go again, or step. step runs count instructions (1 by "
    "default); step_over runs a call to completion instead of going into it; step_out "
    "runs until the current subroutine returns. Those two wait 2 seconds: if the call has "
    "not returned by then the reply says timeout and the machine is still running, so "
    "wait with bmsx_run_until or pause it.",
    SCHEMA("\"properties\":{"
           "\"how\":{\"type\":\"string\",\"enum\":[\"pause\",\"resume\",\"step\","
           "\"step_over\",\"step_out\"]},"
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
    "bmsx_stack",
    "The return addresses on the stack: the CPU's call log, confirmed against "
    "stack memory above SP, innermost first, each with where it sits and the slot and "
    "segment paged in there. A return address on its own "
    "means little on a bank switched program, where 0x4140 is in thirty-two "
    "places at once.",
    SCHEMA("\"properties\":{"
           "\"count\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_STACK_COUNT_DEFAULT)
           ",\"minimum\":" CTRL_STR(CTRL_STACK_COUNT_MIN) ","
           "\"maximum\":" CTRL_STR(CTRL_STACK_COUNT_MAX) "}}"),
    READ_ONLY,
    "stack",
    NULL
},
{
    "bmsx_run_until",
    "Let the machine run and wait for it to stop, reporting why. This is how "
    "to wait for a breakpoint: do not poll bmsx_status in a loop. With "
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
    "bmsx_break",
    "Set or clear a breakpoint. Give a segment address and the breakpoint "
    "only fires when that segment is the one paged in, so code in a bank that "
    "is not currently visible can still be broken on. A Z80 address in a mapper "
    "slot is pinned to the segment paged in there when "
    "it is set and fires only while that segment is paged in; the reply names it. "
    "Where no segment is known, as in the BIOS or under a mapper that does not "
    "report its segments, it is a plain breakpoint. "
    "Breakpoints set here stay over resets, state loads and cancelled calls. "
    "clear_all removes them all, leaving only plain ones the debugger window set. "
    "Plain ones set here end when this MCP server goes away (the conversation "
    "ends) or Agent Control is turned Off: at once on a normal exit or Off, about "
    "5 s after a killed server or a lost connection unless the same server "
    "reconnects; a cancelled call reconnects at once and keeps them. "
    "Segment ones stay until cleared. list shows both "
    "kinds that are set, the debugger window's plain ones too.",
    SCHEMA("\"properties\":{"
           "\"op\":{\"type\":\"string\",\"enum\":[\"set\",\"clear\",\"clear_all\",\"list\"],"
           "\"default\":\"set\"},"
           AT_PROPERTY "}"),
    CHANGES,
    "bp.",
    buildByOp
},
{
    "bmsx_hunt",
    "Find a variable with no symbol for it, by how its value changes. Start a "
    "hunt, let the machine change the thing you can see (a score, a life "
    "count), then refine by how it changed. Repeat until a few candidates are "
    "left. Unlike the usual version of this, it searches all of physical "
    "mapper RAM and video RAM, not only the 64kB the Z80 can see. A hunt belongs to "
    "the connection: a cancelled call reconnects without it, so start it again.",
    SCHEMA("\"properties\":{"
           "\"op\":{\"type\":\"string\",\"enum\":[\"start\",\"refine\",\"list\",\"end\"]},"
           "\"width\":{\"type\":\"string\",\"enum\":[\"u8\",\"u16\",\"u32\",\"s8\",\"s16\",\"s32\"],"
           "\"default\":\"" CTRL_HUNT_START_WIDTH_DEFAULT
           "\",\"description\":\"for start. Use a signed width for "
           "anything that can count down past zero.\"},"
           "\"compare\":{\"type\":\"string\",\"enum\":[\"changed\",\"unchanged\","
           "\"increased\",\"decreased\",\"increased_by\",\"decreased_by\",\"eq\",\"ne\","
           "\"lt\",\"le\",\"gt\",\"ge\",\"in_range\"],\"description\":\"for refine\"},"
           "\"value\":{\"type\":\"integer\"},"
           "\"value2\":{\"type\":\"integer\",\"description\":\"upper bound for in_range\"},"
           "\"first\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_HUNT_LIST_FIRST_DEFAULT)
           ",\"minimum\":" CTRL_STR(CTRL_HUNT_LIST_FIRST_MIN) "},"
           "\"count\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_HUNT_LIST_COUNT_DEFAULT)
           ",\"minimum\":" CTRL_STR(CTRL_HUNT_LIST_COUNT_MIN) ",\"maximum\":"
           CTRL_STR(CTRL_HUNT_LIST_COUNT_MAX) "}},"
           "\"required\":[\"op\"]"),
    READ_ONLY,
    "hunt.",
    buildByOp
},
{
    "bmsx_watch",
    "Stop the machine when a byte is written (reads do not count, and an "
    "unchanged value still counts), or when an I/O port gets an OUT. Works on "
    "a Z80 address whatever segment is paged in, on video memory -- which is how "
    "to find out what drew over your sprite -- and on ports. A condition is "
    "checked after each byte is written, and a value of more than 1 byte is "
    "little endian. Then bmsx_run_until reports the stop and bmsx_status says "
    "where the code was. list shows the watches agents set, not the debugger window's.",
    SCHEMA("\"properties\":{"
           "\"op\":{\"type\":\"string\",\"enum\":[\"set\",\"clear\",\"list\"],\"default\":\"set\"},"
           AT_PROPERTY ","
           "\"port\":{\"type\":\"integer\",\"minimum\":" CTRL_STR(CTRL_WP_PORT_MIN) ",\"maximum\":"
           CTRL_STR(CTRL_WP_PORT_MAX) ","
           "\"description\":\"an I/O port instead of an address\"},"
           "\"condition\":{\"type\":\"string\",\"enum\":[\"any\",\"eq\",\"ne\",\"lt\",\"gt\"],"
           "\"default\":\"" CTRL_WP_CONDITION_DEFAULT "\"},"
           "\"value\":{\"type\":\"integer\",\"description\":\"what to compare against\"},"
           "\"size\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_WP_SIZE_DEFAULT)
           ",\"minimum\":" CTRL_STR(CTRL_WP_SIZE_MIN) ",\"maximum\":" CTRL_STR(CTRL_WP_SIZE_MAX)
           "}}"),
    CHANGES,
    "wp.",
    buildByOp
},
{
    "bmsx_symbols",
    "Load a symbol file, or look one up. Symbols are bound to the segment they "
    "were assembled for, so a megaROM whose thirty-two banks all run at 0x4000 "
    "gets the right labels for each one instead of whichever file was read last. "
    "Reads bank numbers from sjasmplus .sld, asMSX .sym, WLA-DX .sym and SDCC "
    ".noi or .map, and plain NAME equ VALUE files without them.",
    SCHEMA("\"properties\":{"
           "\"op\":{\"type\":\"string\","
           "\"enum\":[\"load\",\"find\",\"search\",\"status\",\"clear\"]},"
           "\"path\":{\"type\":\"string\",\"description\":\"for load: the full path of "
           "the file\"},"
           "\"slot\":{\"type\":\"string\",\"description\":\"for load, as in \\\"1.0\\\", "
           "when the file's banks belong to a slot it could not work out\"},"
           "\"replace_all\":{\"type\":\"boolean\",\"default\":false,\"description\":"
           "\"for load: unload every other symbol file first. Without it the other "
           "files stay, and a file of the same name replaces its earlier load\"},"
           "\"name\":{\"type\":\"string\",\"description\":\"for find\"},"
           "\"pattern\":{\"type\":\"string\",\"description\":\"for search; * and ? work\"},"
           "\"first\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_SYM_SEARCH_FIRST_DEFAULT)
           ",\"minimum\":" CTRL_STR(CTRL_SYM_SEARCH_FIRST_MIN) "},"
           "\"count\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_SYM_SEARCH_COUNT_DEFAULT)
           ",\"minimum\":" CTRL_STR(CTRL_SYM_SEARCH_COUNT_MIN) ",\"maximum\":"
           CTRL_STR(CTRL_SYM_SEARCH_COUNT_MAX) "}},"
           "\"required\":[\"op\"]"),
    READ_ONLY,
    "sym.",
    buildSymbols
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
{
    "bmsx_mem_write",
    "Write bytes into the machine. This changes the program that is running; "
    "it is how a candidate found by bmsx_hunt is confirmed. Says whether the "
    "write can be taken back with bmsx_undo, and if not, why not.",
    SCHEMA("\"properties\":{" AT_PROPERTY ","
           "\"data\":{\"type\":\"string\",\"description\":"
           "\"hex bytes, for example \\\"3E 01 C9\\\"\"}},"
           "\"required\":[\"at\",\"data\"]"),
    DESTROYS,
    "mem.write",
    NULL
},
{
    "bmsx_reg_write",
    "Set a CPU register. This is how to carry on from a mistake instead of "
    "starting the program again: move the PC past the call that faults, put "
    "back a value a routine clobbered, take the other branch. The reply says "
    "what the register held before, so the change can be put back by hand.",
    SCHEMA("\"properties\":{"
           "\"reg\":{\"type\":\"string\",\"description\":"
           "\"AF BC DE HL, the alternates AF1 BC1 DE1 HL1, IX IY SP PC I R IM "
           "IFF1 IFF2, or a half: A F B C D E H L IXH IXL IYH IYL\"},"
           "\"value\":{\"type\":\"string\",\"description\":"
           "\"0x4000, 4000h or a decimal number. A "
           "bare number is decimal.\"}},"
           "\"required\":[\"reg\",\"value\"]"),
    DESTROYS,
    "reg.write",
    NULL
},
{
    "bmsx_undo",
    "Put back what bmsx_mem_write overwrote, most recent first. Writing is "
    "allowed, so there has to be a way back: what goes wrong is usually not "
    "the bytes but the place, and by then the old bytes exist nowhere else. "
    "Restores the bytes only -- whatever the program did after reading them "
    "has already happened. A write through the Z80's bus into ROM cannot be "
    "taken back, and bmsx_mem_write says so at the time.",
    SCHEMA("\"properties\":{"
           "\"count\":{\"type\":\"integer\",\"default\":" CTRL_STR(CTRL_UNDO_COUNT_DEFAULT) ","
           "\"minimum\":" CTRL_STR(CTRL_UNDO_COUNT_MIN) ",\"maximum\":"
           CTRL_STR(CTRL_UNDO_COUNT_MAX) ",\"description\":"
           "\"how many writes to walk back\"}}"),
    DESTROYS_NOT_IDEMPOTENT,
    "undo",
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

/* "op" as tool->op and then the "op" argument, or the schema's default for it, the one place
** that default is written. 0 when there is neither. */
static int writeOp(const McpTool* tool, const char* arguments, JsonOut* request)
{
    char property[2048];
    char word[32];
    char op[64];

    if (!jsonGetString(arguments, "op", word, sizeof(word)) &&
        !(schemaProperty(tool, "op", property, sizeof(property)) &&
          jsonGetString(property, "default", word, sizeof(word))))
    {
        copyFailed = 1;
        return 0;
    }
    _snprintf(op, sizeof(op), "%s%s", tool->op, word);
    op[sizeof(op) - 1] = 0;
    jsonOutString(request, "op", op);
    return 1;
}

static void buildByOp(const McpTool* tool, const char* arguments, JsonOut* request)
{
    if (writeOp(tool, arguments, request)) {
        mcpToolCopy(tool, arguments, request, NULL);
    }
}

/* As buildByOp, with replace_all sent as the emulator's "replace". */
static void buildSymbols(const McpTool* tool, const char* arguments, JsonOut* request)
{
    int replaceAll;

    if (!writeOp(tool, arguments, request)) {
        return;
    }
    mcpToolCopy(tool, arguments, request, "replace_all");
    if (jsonGetBool(arguments, "replace_all", &replaceAll)) {
        jsonOutBool(request, "replace", replaceAll);
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
