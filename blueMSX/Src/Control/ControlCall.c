/*****************************************************************************
**
** Op call arguments and replies.
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
#include "ControlCall.h"
#include "ControlMemory.h"
#include "ControlProtocol.h"
#include "Debugger.h"
#include "Emulator.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* The decoded strings of one request: together they are never longer than its line. */
static char argArena[CTRL_LINE_MAX];

/* A text argument into the arena; 0, or the refusal of it. */
static int readText(CtrlCall* call, const CtrlArg* arg, CtrlArgValue* value, int* used)
{
    char* text = argArena + *used;
    int   room = (int)sizeof(argArena) - *used;
    int   result = jsonGetText(call->request, arg->name, text, arg->size + 1 < room ? arg->size + 1 : room);

    if (result == JSON_TOO_LONG) {
        return ctrlReplyErrorf(call, "arg_too_long", "\"%s\" is too long: it takes at most %d bytes",
                               arg->name, arg->size);
    }
    if (result == JSON_WRONG_TYPE) {
        return ctrlReplyErrorf(call, "bad_arg", "\"%s\" must be %s", arg->name,
                               CTRL_TEXT_WANT_STRING);
    }
    if (result == JSON_TEXT_OK) {
        value->given = 1;
        value->text  = text;
        *used += (int)strlen(text) + 1;
    }
    return 0;
}

/* One declared argument as its kind says; 0, or the refusal of it. */
static int readArgument(CtrlCall* call, const CtrlArg* arg, CtrlArgValue* value, int* used)
{
    int kind;
    int flag = 0;

    if (arg->type == CTRL_ARG_TEXT) {
        return readText(call, arg, value, used);
    }
    kind = jsonGetKind(call->request, arg->name);
    if (kind == JSON_ABSENT) {
        return 0;
    }
    if (kind == JSON_NUMBER && arg->type == CTRL_ARG_INT) {
        value->given = jsonGetInt64(call->request, arg->name, &value->number);
        return 0;
    }
    if (kind == JSON_BOOL && arg->type == CTRL_ARG_BOOL) {
        value->given  = jsonGetBool(call->request, arg->name, &flag);
        value->number = flag;
        return 0;
    }
    return ctrlReplyErrorf(call, "bad_arg", "\"%s\" must be %s", arg->name,
                           arg->type == CTRL_ARG_INT ? CTRL_TEXT_WANT_INT : CTRL_TEXT_WANT_BOOL);
}

int ctrlCallReadArgs(CtrlCall* call)
{
    int used = 0;
    int refused;
    int i;

    for (i = 0; call->args != NULL && i < CTRL_OP_ARGS_MAX && call->args[i].name != NULL; i++) {
        if ((refused = readArgument(call, &call->args[i], &call->values[i], &used)) != 0) {
            return refused;
        }
    }
    return 0;
}

/* The value of a declared argument; NULL, with the call marked, when key is not one. */
static const CtrlArgValue* argValue(CtrlCall* call, const char* key)
{
    int i;

    for (i = 0; call->args != NULL && i < CTRL_OP_ARGS_MAX && call->args[i].name != NULL; i++) {
        if (strcmp(call->args[i].name, key) == 0) {
            return &call->values[i];
        }
    }
    if (call->undeclared == NULL) {
        call->undeclared = key;
    }
    return NULL;
}

const char* ctrlCallText(CtrlCall* call, const char* key)
{
    const CtrlArgValue* value = argValue(call, key);

    return value != NULL && value->given ? value->text : NULL;
}

Int32 ctrlCallInt(CtrlCall* call, const char* key, Int32 fallback)
{
    const CtrlArgValue* value = argValue(call, key);

    if (value == NULL || !value->given || value->text != NULL) {
        return fallback;
    }
    return value->number > 0x7FFFFFFF      ? 0x7FFFFFFF :
           value->number < -0x7FFFFFFF - 1 ? -0x7FFFFFFF - 1 : (Int32)value->number;
}

int ctrlCallInt64(CtrlCall* call, const char* key, Int64* v)
{
    const CtrlArgValue* value = argValue(call, key);

    *v = value != NULL && value->given && value->text == NULL ? value->number : 0;
    return value != NULL && value->given;
}

int ctrlCallBool(CtrlCall* call, const char* key, int fallback)
{
    const CtrlArgValue* value = argValue(call, key);

    return value != NULL && value->given && value->text == NULL ? value->number != 0 : fallback;
}

int ctrlCallGiven(CtrlCall* call, const char* key)
{
    const CtrlArgValue* value = argValue(call, key);

    return value != NULL && value->given;
}

int ctrlReplyError(CtrlCall* call, const char* code, const char* message)
{
    static char undeclared[128];
    JsonOut out;

    /* A view is refused on a stopped machine too; that is not a slow one. */
    if (strcmp(code, "busy") == 0 && emulatorGetState() == EMU_STOPPED) {
        code    = "no_machine";
        message = CTRL_TEXT_NO_MACHINE;
    }
    /* A key missing from the op's args is a bug here, whatever the op meant to answer. */
    if (call->undeclared != NULL) {
        snprintf(undeclared, sizeof(undeclared), "op %s read undeclared argument %s", call->op,
                 call->undeclared);
        code    = "internal";
        message = undeclared;
    }

    jsonOutInit(&out, call->reply, call->replyLen);
    jsonOutBeginObject(&out);
    jsonOutInt(&out, "id", call->id);
    jsonOutBool(&out, "ok", 0);
    jsonOutString(&out, "error", code);
    jsonOutString(&out, "message", message);
    jsonOutEndObject(&out);

    if (jsonOutOverflowed(&out)) {
        /* Cannot even fit the complaint, so send the shortest true thing. */
        snprintf(call->reply, (size_t)call->replyLen, "{\"ok\":false,\"error\":\"internal\"}");
    }
    return (int)strlen(call->reply);
}

void ctrlReplyBegin(CtrlCall* call)
{
    jsonOutInit(&call->out, call->reply, call->replyLen);
    jsonOutBeginObject(&call->out);
    jsonOutInt(&call->out, "id", call->id);
    jsonOutBool(&call->out, "ok", 1);
}

int ctrlReplyDone(CtrlCall* call)
{
    if (call->undeclared != NULL) {
        return ctrlReplyError(call, "internal", "");
    }
    jsonOutString(&call->out, "text", call->text.data);
    jsonOutEndObject(&call->out);

    if (jsonOutOverflowed(&call->out)) {
        return ctrlReplyError(call, "too_long",
                              "the answer did not fit in one reply; ask for less at a time");
    }
    return call->out.length;
}

int ctrlReplyTextAsError(CtrlCall* call, const char* code)
{
    return ctrlReplyError(call, code, call->text.data);
}

int ctrlReplyErrorf(CtrlCall* call, const char* code, const char* format, ...)
{
    /* Its own buffer: the arguments may point into call->text. */
    static char message[CTRL_ERRORF_MAX];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    message[sizeof(message) - 1] = 0;
    return ctrlReplyError(call, code, message);
}

/* ---- refusals ---------------------------------------------------------- */

int ctrlRefuseWithoutMachine(CtrlCall* call, const char* suffix)
{
    if (emulatorGetState() != EMU_STOPPED) {
        return 0;
    }
    return ctrlReplyErrorf(call, "no_machine", "%s%s", CTRL_TEXT_NO_MACHINE,
                           suffix != NULL ? suffix : "");
}

int ctrlRefuseUnlessViewed(CtrlCall* call)
{
    int refused;

    if (call->view.held == DBG_ACCESS_VIEW) {
        return 0;
    }
    if ((refused = ctrlRefuseWithoutMachine(call, NULL)) != 0) {
        return refused;
    }
    return ctrlReplyError(call, "not_paused", CTRL_TEXT_NOT_PAUSED);
}

void ctrlCallReleaseView(CtrlCall* call)
{
    ctrlViewRelease(&call->view);
}

int ctrlCallTakeView(CtrlCall* call, CtrlView* view)
{
    return ctrlRefuseAccess(call, ctrlViewTake(view, DBG_ACCESS_VIEW, CTRL_VIEW_TIMEOUT_MS));
}

int ctrlRefuseAccess(CtrlCall* call, int result)
{
    switch (result) {
    case DBG_ACCESS_OK:
        return 0;
    case DBG_ACCESS_NO_MACHINE:
        return ctrlReplyError(call, "no_machine", CTRL_TEXT_NO_MACHINE);
    case DBG_ACCESS_NOT_PAUSED:
        return ctrlReplyError(call, "not_paused", CTRL_TEXT_NOT_PAUSED);
    case DBG_ACCESS_CHANGING:
        return ctrlReplyError(call, "busy", CTRL_TEXT_CHANGING);
    default:
        return ctrlReplyError(call, "busy", ctrlMemErrorText(CTRL_MEM_NOVIEW));
    }
}
