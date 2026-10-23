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
#ifndef CONTROL_CALL_H
#define CONTROL_CALL_H

#include "ControlAddressType.h"
#include "ControlJson.h"
#include "ControlSession.h"
#include "ControlText.h"
#include "ControlView.h"

#define CTRL_TEXT_NO_MACHINE "no machine is running: the emulator is stopped, or is being reset"
#define CTRL_TEXT_NOT_PAUSED "the machine is running; pause it first (bmsx_run how=pause) or " \
                             "let it run to a stop with bmsx_run_until (Full access)"
#define CTRL_TEXT_CHANGING   "the emulator window is changing the machine, or has a menu or " \
                             "dialog open; call again once it is closed"
/* Why the machine was not resumed or stepped; what was not done follows. */
#define CTRL_TEXT_FOLDER_DISK_BUSY "the emulator window has a question about a folder disk " \
                                   "open, or is changing the machine"
/* After an answer read from the layout of a running machine, as ram: is. */
#define CTRL_TEXT_RUNNING_LAYOUT "The segment paged in there was read while the machine runs, so " \
                                 "a moment later it can be another one; pause first to pin a fixed one."

/* An argument an op reads: its name, its kind and, for text, the most bytes it takes. */
typedef struct {
    const char* name;
    int         type;
    int         size;
} CtrlArg;

#define CTRL_ARG_TEXT        0
#define CTRL_ARG_INT         1   /* read as Int64 */
#define CTRL_ARG_BOOL        2

#define CTRL_OP_ARGS_MAX 8

/* An argument as the dispatcher read it; text points into its arena. */
typedef struct {
    int         given;
    const char* text;
    Int64       number;
} CtrlArgValue;

/* One request being answered: what was asked, by whom, and the reply being built. */
typedef struct {
    CtrlSession*   session;
    const char*    request;
    Int32          id;
    CtrlText       text;      /* the reply's "text" member */
    JsonOut        out;       /* the reply object, for the members before the text */
    char*          reply;
    int            replyLen;
    CtrlView       view;      /* what the dispatcher holds for the op, if anything */
    const char*    op;        /* the op's name, once it is known */
    const CtrlArg* args;      /* what the op reads, ending in { NULL }; NULL for nothing */
    CtrlArgValue   values[CTRL_OP_ARGS_MAX];
    const char*    undeclared;  /* the first key the handler read that args does not have */
    int            atUnread;    /* "at" names a register the machine cannot give now */
} CtrlCall;

/* Reads every argument the op declares, in order, and refuses the first one that is bad. */
int ctrlCallReadArgs(CtrlCall* call);

/* The arguments of the call, as the op's args read them; null counts as absent. Reading a
** key args does not have turns the reply into an internal error. */
/* The text given, or NULL when it is absent or a number. */
const char* ctrlCallText(CtrlCall* call, const char* key);
/* The number given, clamped to 32 bits as jsonGetInt does, or fallback. */
Int32 ctrlCallInt(CtrlCall* call, const char* key, Int32 fallback);
int   ctrlCallBool(CtrlCall* call, const char* key, int fallback);

/* Starts the reply object with the id and ok:true; the handler adds members to out. */
void ctrlReplyBegin(CtrlCall* call);
/* Ends the reply with the text; a reply that does not fit becomes a too_long error. */
int  ctrlReplyDone(CtrlCall* call);
/* Writes an error reply in place of any reply begun; returns its length. */
int  ctrlReplyError(CtrlCall* call, const char* code, const char* message);
/* An error with a formatted message, cut to CTRL_ERRORF_MAX. */
#define CTRL_ERRORF_MAX 4096
int  ctrlReplyErrorf(CtrlCall* call, const char* code, const char* format, ...);

/* The refusals, and the calls below that may refuse, return 0 when the request may go on,
** else the length of the refusal. */

/* The emulator is stopped, so nothing reads as the machine; suffix is added, or NULL. */
int  ctrlRefuseWithoutMachine(CtrlCall* call, const char* suffix);
/* The machine runs, or there is none: the op reads what only a view of a paused one may. */
int  ctrlRefuseUnlessViewed(CtrlCall* call);
/* For an op that holds nothing: takes a view of the paused machine, else refuses. */
int  ctrlCallTakeView(CtrlCall* call, CtrlView* view);
/* The reply to a view or hold not taken (a DBG_ACCESS_ result); 0 for DBG_ACCESS_OK. */
int  ctrlRefuseAccess(CtrlCall* call, int result);

#endif
