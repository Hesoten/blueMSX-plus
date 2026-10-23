/*****************************************************************************
**
** Key input op handlers.
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
#include "ControlOp.h"
#include "ControlClient.h"
#include "ControlInput.h"
#include "ControlProtocol.h"
#include <stdio.h>
#include <string.h>

int ctrlOpKey(CtrlCall* call)
{
    const char* keys = ctrlCallText(call, "keys");
    const char* how = ctrlCallText(call, "how");
    char  err[240];
    Int32 hold = ctrlCallInt(call, "hold", CTRL_KEY_HOLD_DEFAULT);
    Int32 gap = ctrlCallInt(call, "gap", CTRL_KEY_GAP_DEFAULT);
    int   kind;
    int   result;

    if (keys == NULL) {
        return ctrlReplyError(call, "missing_keys",
                              "key needs \"keys\", as in \"return\" or \"ctrl+stop\"");
    }
    if (how == NULL) {
        how = CTRL_KEY_HOW_DEFAULT;
    }
    if (hold < CTRL_KEY_HOLD_MIN || hold > CTRL_KEY_HOLD_MAX ||
        gap < CTRL_KEY_GAP_MIN || gap > CTRL_KEY_GAP_MAX)
    {
        return ctrlReplyErrorf(call, "out_of_range", "hold is %d to %d frames and gap %d to %d",
                               CTRL_KEY_HOLD_MIN, CTRL_KEY_HOLD_MAX, CTRL_KEY_GAP_MIN,
                               CTRL_KEY_GAP_MAX);
    }

    kind = strcmp(how, "press") == 0   ? CTRL_INPUT_PRESS :
           strcmp(how, "release") == 0 ? CTRL_INPUT_RELEASE :
           strcmp(how, "tap") == 0     ? CTRL_INPUT_TAP : -1;
    if (kind < 0) {
        return ctrlReplyError(call, "bad_how",
                              "\"how\" must be press, release or tap");
    }

    result = ctrlInputKey(keys, kind, (int)hold, (int)gap, err, sizeof(err));
    /* An interrupted press may have reached the machine in part, so it is logged as failed too. */
    ctrlActivityNoteOn(kind == CTRL_INPUT_PRESS ? CTRL_ACT_KEY_PRESS :
                       kind == CTRL_INPUT_RELEASE ? CTRL_ACT_KEY_RELEASE : CTRL_ACT_KEY_TAP,
                       keys, NULL, result > 0 ? CTRL_ACT_DONE : CTRL_ACT_FAILED);
    if (result == CTRL_INPUT_STOPPED) {
        return ctrlReplyError(call, "input_interrupted", err);
    }
    if (result < 0) {
        char names[3000];
        char message[3400];

        ctrlInputKeyNames(names, sizeof(names));
        snprintf(message, sizeof(message), "%s The names are: %s", err, names);
        return ctrlReplyError(call, "unknown_key", message);
    }
    if (result == 0) {
        return ctrlReplyError(call, "input_failed", err);
    }
    if (kind == CTRL_INPUT_TAP) {
        ctrlTextAdd(&call->text, "tapped %s, held %d frames\n", keys, (int)hold);
    }
    else if (kind == CTRL_INPUT_PRESS) {
        ctrlTextAdd(&call->text, "%s is down and stays down until how=release\n", keys);
    }
    else {
        ctrlTextAdd(&call->text, "released %s\n", keys);
    }
    return ctrlReplyDone(call);
}

/* The typed text, quoted, as the Target. */
static void noteTyped(const char* typed, int outcome)
{
    char quoted[CTRL_ACT_TARGET];

    snprintf(quoted, sizeof(quoted), "\"%.*s\"", (int)sizeof(quoted) - 3, typed);
    ctrlActivityNoteOn(CTRL_ACT_TYPE, quoted, NULL, outcome);
}

int ctrlOpType(CtrlCall* call)
{
    const char* typed = ctrlCallText(call, "text");
    char  note[400];
    char  err[320];
    Int32 hold = ctrlCallInt(call, "hold", CTRL_TYPE_HOLD_DEFAULT);
    Int32 gap = ctrlCallInt(call, "gap", CTRL_TYPE_GAP_DEFAULT);
    int   events = 0;
    int   frames = 0;
    int   result;

    if (typed == NULL) {
        return ctrlReplyError(call, "missing_text",
                              "type needs \"text\"");
    }
    if (hold < CTRL_TYPE_HOLD_MIN || hold > CTRL_TYPE_HOLD_MAX ||
        gap < CTRL_TYPE_GAP_MIN || gap > CTRL_TYPE_GAP_MAX)
    {
        return ctrlReplyErrorf(call, "out_of_range", "hold and gap are %d to %d frames; on some "
                               "machines the BIOS reads the keyboard only every 2nd or 3rd "
                               "frame, and shorter ones lose keys", CTRL_TYPE_HOLD_MIN,
                               CTRL_TYPE_HOLD_MAX);
    }

    result = ctrlInputType(typed, (int)hold, (int)gap, &events, &frames,
                           note, sizeof(note), err, sizeof(err));
    /* One activity line for the whole string, not one per key. */
    noteTyped(typed, result > 0 ? CTRL_ACT_DONE : CTRL_ACT_FAILED);
    if (result == CTRL_INPUT_STOPPED) {
        return ctrlReplyError(call, "input_interrupted", err);
    }
    if (result == 0) {
        return ctrlReplyError(call, "input_failed", err);
    }
    ctrlTextAdd(&call->text,
                "typed %d character%s as %d key events over %d frames\n%s\n"
                "the keys went into the keyboard matrix; whether the program read "
                "them is not checked, so read the screen back.\n%s",
                (int)strlen(typed), strlen(typed) == 1 ? "" : "s", events, frames,
                typed, note);
    jsonOutInt(&call->out, "key_events", events);
    return ctrlReplyDone(call);
}
