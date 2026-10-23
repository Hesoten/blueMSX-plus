/*****************************************************************************
**
** Control request op table and dispatch.
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
#include "ControlRequest.h"
#include "ControlJson.h"
#include "ControlOp.h"
#include "ControlProtocol.h"
#include "ControlSession.h"
#include "Debugger.h"
#include <string.h>

#define TEXT_MAX (CTRL_REPLY_MAX / 2)

static char textBuffer[TEXT_MAX];

/* Every op in the order help lists them. CHANGES ones are refused under Read Only. */
#define CTRL_OP_LOOKS   0
#define CTRL_OP_CHANGES 1

/* HELD ones run holding a view of a paused machine, else a hold; SELF ones take their own. */
#define CTRL_OP_HELD    0
#define CTRL_OP_SELF    1

typedef struct {
    const char*   name;
    int           effect;
    int           viewOwner;
    CtrlOpHandler handler;
    const CtrlArg* args;    /* what it reads, ending in { NULL }; NULL for nothing */
    const char*   help;     /* its rows in the help, each "  " + name first */
} CtrlOp;

static int ctrlOpHelp(CtrlCall* call);

/* What each op reads, in the order its help row names them. */
static const CtrlArg memReadArgs[] = {
    { "at", CTRL_ARG_TEXT, CTRL_AT_MAX }, { "count", CTRL_ARG_INT, 0 }, { NULL }
};
static const CtrlArg memWriteArgs[] = {
    { "at", CTRL_ARG_TEXT, CTRL_AT_MAX }, { "data", CTRL_ARG_TEXT, CTRL_LINE_MAX - 1 }, { NULL }
};
static const CtrlArg memResolveArgs[] = {
    { "at", CTRL_ARG_TEXT, CTRL_AT_MAX }, { NULL }
};
static const CtrlArg regWriteArgs[] = {
    { "reg", CTRL_ARG_TEXT, 15 }, { "value", CTRL_ARG_TEXT, 95 }, { NULL }
};
static const CtrlArg undoArgs[] = {
    { "count", CTRL_ARG_INT, 0 }, { NULL }
};
static const CtrlArg runArgs[] = {
    { "how", CTRL_ARG_TEXT, 15 }, { "count", CTRL_ARG_INT, 0 }, { NULL }
};
static const CtrlArg runUntilArgs[] = {
    { "timeout_ms", CTRL_ARG_INT, 0 }, { "pause_at_timeout", CTRL_ARG_BOOL, 0 }, { NULL }
};
static const CtrlArg bpSetArgs[] = {
    { "at", CTRL_ARG_TEXT, CTRL_AT_MAX }, { NULL }
};
static const CtrlArg bpClearArgs[] = {
    { "at", CTRL_ARG_TEXT, CTRL_AT_MAX }, { NULL }
};
static const CtrlArg screenArgs[] = {
    { "as", CTRL_ARG_TEXT, 11 }, { "path", CTRL_ARG_TEXT, CTRL_PATH_MAX - 1 }, { NULL }
};

/* An op's args in its row; an array longer than CTRL_OP_ARGS_MAX and its { NULL } fails to compile. */
#define OP_ARGS(a) ((a) + 0 * sizeof(char[sizeof(a) / sizeof((a)[0]) <= CTRL_OP_ARGS_MAX + 1 ? 1 : -1]))

static const CtrlOp opTable[] = {
    { "help",              CTRL_OP_LOOKS,   CTRL_OP_SELF, ctrlOpHelp, NULL,
      "  help\n" },
    { "status",            CTRL_OP_LOOKS,   CTRL_OP_HELD, ctrlOpStatus, NULL,
      "  status\n" },
    { "slots",             CTRL_OP_LOOKS,   CTRL_OP_HELD, ctrlOpSlots, NULL,
      "  slots\n" },
    { "mem.read",          CTRL_OP_LOOKS,   CTRL_OP_HELD, ctrlOpMemRead, OP_ARGS(memReadArgs),
      "  mem.read     at, count\n" },
    { "mem.write",         CTRL_OP_CHANGES, CTRL_OP_HELD, ctrlOpMemWrite, OP_ARGS(memWriteArgs),
      "  mem.write    at, data (hex)\n" },
    { "mem.resolve",       CTRL_OP_LOOKS,   CTRL_OP_HELD, ctrlOpMemResolve, OP_ARGS(memResolveArgs),
      "  mem.resolve  at\n" },
    { "reg.write",         CTRL_OP_CHANGES, CTRL_OP_HELD, ctrlOpRegWrite, OP_ARGS(regWriteArgs),
      "  reg.write    reg, value\n" },
    { "undo",              CTRL_OP_CHANGES, CTRL_OP_HELD, ctrlOpUndo, OP_ARGS(undoArgs),
      "  undo         count        (puts back what mem.write overwrote)\n" },
    { "run",               CTRL_OP_CHANGES, CTRL_OP_SELF, ctrlOpRun, OP_ARGS(runArgs),
      "  run          how = pause|resume|step, count\n" },
    { "run.until",         CTRL_OP_CHANGES, CTRL_OP_SELF, ctrlOpRunUntil, OP_ARGS(runUntilArgs),
      "  run.until    timeout_ms, pause_at_timeout\n" },
    { "bp.set",            CTRL_OP_CHANGES, CTRL_OP_HELD, ctrlOpBpSet, OP_ARGS(bpSetArgs),
      "  bp.set       at            (fires only in the segment named, or for a Z80\n"
      "                             address in a mapper slot the one paged in now)\n" },
    { "bp.clear",          CTRL_OP_CHANGES, CTRL_OP_HELD, ctrlOpBpClear, OP_ARGS(bpClearArgs),
      "  bp.clear     at\n" },
    { "bp.clear_all",      CTRL_OP_CHANGES, CTRL_OP_HELD, ctrlOpBpClearAll, NULL,
      "  bp.clear_all               (segment ones, and plain ones set here)\n" },
    { "bp.list",           CTRL_OP_LOOKS,   CTRL_OP_HELD, ctrlOpBpList, NULL,
      "  bp.list\n" },
    { "screen",            CTRL_OP_LOOKS,   CTRL_OP_HELD, ctrlOpScreen, OP_ARGS(screenArgs),
      "  screen       as = auto|text|png, path\n" },
};

static const CtrlOp* findOp(const char* name)
{
    int i;

    for (i = 0; i < (int)(sizeof(opTable) / sizeof(opTable[0])); i++) {
        if (strcmp(opTable[i].name, name) == 0) {
            return &opTable[i];
        }
    }
    return NULL;
}

static int ctrlOpHelp(CtrlCall* call)
{
    int i;

    ctrlTextAdd(&call->text, "ops:\n");
    for (i = 0; i < (int)(sizeof(opTable) / sizeof(opTable[0])); i++) {
        ctrlTextAdd(&call->text, "%s", opTable[i].help);
    }
    ctrlTextAdd(&call->text,
        "\n"
        "addresses are one of:\n"
        "  0xC000                what the Z80 can see right now\n"
        "  slot1.0:seg5+0x0100   a segment of a slot, paged in or not\n"
        "  ram:seg12+0x1A30      the memory mapper in page 3\n"
        "  vram:0x1B00           as the program addresses video memory through the VDP\n");
    return ctrlReplyDone(call);
}

/* The op runs with what the machine allows held; with no machine, it says so itself. */
static int runHolding(CtrlCall* call, const CtrlOp* row)
{
    int result = ctrlViewTakeBest(&call->view, CTRL_VIEW_TIMEOUT_MS);
    int length;

    if (result == DBG_ACCESS_BUSY || result == DBG_ACCESS_CHANGING) {
        return ctrlRefuseAccess(call, result);
    }
    length = row->handler(call);
    ctrlViewRelease(&call->view);
    return length;
}

/* What read only refuses: an op that changes the machine, but not a pause, which changes
** nothing in it and is what lets read only read it. */
static int refusedReadOnly(const CtrlOp* row, const char* request)
{
    char how[16];

    if (row == NULL || row->effect != CTRL_OP_CHANGES) {
        return 0;
    }
    return !(strcmp(row->name, "run") == 0 &&
             jsonGetString(request, "how", how, sizeof(how)) && strcmp(how, "pause") == 0);
}

int ctrlRequestRefuseLine(const char* line, char* reply, int replyLen)
{
    CtrlCall call;

    memset(&call, 0, sizeof(call));
    call.reply    = reply;
    call.replyLen = replyLen;
    jsonGetInt(line, "id", &call.id);
    return ctrlReplyErrorf(&call, "line_too_long", CTRL_TEXT_LINE_TOO_LONG, CTRL_LINE_MAX - 1);
}

int ctrlRequestHandle(CtrlClient client, const char* request,
                      char* reply, int replyLen)
{
    char          op[40];
    CtrlCall      call;
    const CtrlOp* row;
    int           opRead;
    int           refused;

    memset(&call, 0, sizeof(call));
    call.request  = request;
    call.reply    = reply;
    call.replyLen = replyLen;
    jsonGetInt(request, "id", &call.id);

    /* An op name too long for any op is left empty, which none is called: unknown. */
    opRead = jsonGetText(request, "op", op, sizeof(op));
    if (opRead != JSON_TEXT_OK && opRead != JSON_TOO_LONG) {
        return ctrlReplyError(&call, "missing_op",
                              "every request needs an \"op\"; send {\"op\":\"help\"} for the list");
    }
    row = findOp(op);

    call.session = ctrlSessionFor(client);
    if (call.session == NULL) {
        return ctrlReplyError(&call, "too_many_clients",
                              "this emulator is already serving as many clients as it will");
    }

    if (!ctrlClientAllowed(client)) {
        return ctrlReplyError(&call, "revoked",
                              "access to this emulator was cut off at the emulator. "
                              "Ask the person at the keyboard to turn it back on: "
                              "Tools, Agent Control, Read Only or Full.");
    }

    /* Read only denies anything that changes the machine or its breakpoints. */
    if (ctrlGateLevel() != CTRL_GATE_FULL && refusedReadOnly(row, request)) {
        return ctrlReplyError(&call, "read_only",
                              "this emulator is set to read only. To change it, ask the "
                              "person at the keyboard for Tools, Agent Control, Full.");
    }

    ctrlClientSetCurrent(client);
    ctrlTextInit(&call.text, textBuffer, TEXT_MAX);
    ctrlReplyBegin(&call);

    if (row == NULL) {
        return ctrlReplyError(&call, "unknown_op",
                              "no such op; send {\"op\":\"help\"} for the list");
    }
    call.op   = row->name;
    call.args = row->args;
    if ((refused = ctrlCallReadArgs(&call)) != 0) {
        return refused;
    }
    if (row->viewOwner == CTRL_OP_SELF) {
        return row->handler(&call);
    }
    return runHolding(&call, row);
}
