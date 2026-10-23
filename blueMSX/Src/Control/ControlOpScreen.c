/*****************************************************************************
**
** Screen op handler.
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
#include "ControlProtocol.h"
#include "ControlScreen.h"
#include <string.h>

/* The screen as the character codes in the name table. */
static int screenAsText(CtrlCall* call)
{
    /* Holds a whole path: a message cut inside a UTF-8 character is not valid JSON text. */
    char err[CTRL_PATH_MAX + 240];
    int  refused;
    int  ok;

    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    ok = ctrlScreenText(&call->view, &call->text, err, sizeof(err));

    if (!ok) {
        return ctrlReplyError(call, "not_text_mode", err);
    }
    return ctrlReplyDone(call);
}

/* The screen as a PNG, in wanted or, when it is empty, the emulator's own file. */
static int screenAsPng(CtrlCall* call, const char* wanted)
{
    char path[CTRL_PATH_MAX];
    char err[CTRL_PATH_MAX + 240];
    int  result;

    /* Any path the user can write to; read only gets the default file only. */
    if (wanted[0] != 0 && ctrlGateLevel() != CTRL_GATE_FULL) {
        return ctrlReplyError(call, "read_only",
                              "this emulator is set to read only, so it writes the "
                              "screenshot only to its own file; leave out path.");
    }
    result = ctrlScreenPng(wanted[0] != 0 ? wanted : NULL,
                           path, sizeof(path), err, sizeof(err));
    if (result != CTRL_SCREEN_OK) {
        return ctrlReplyError(call,
                              result == CTRL_SCREEN_FILE_EXISTS ? "file_exists"
                              : "screen_failed", err);
    }
    jsonOutString(&call->out, "path", path);
    ctrlTextAdd(&call->text, "wrote a screenshot to %s\n", path);
    /* The default file has one name per process, so the next one overwrites it. */
    if (wanted[0] == 0) {
        ctrlTextAdd(&call->text, "the next screenshot without path overwrites this file, "
                    "so copy it first%s\n",
                    ctrlGateLevel() == CTRL_GATE_FULL ? " or give path" : "");
    }
    return ctrlReplyDone(call);
}

int ctrlOpScreen(CtrlCall* call)
{
    int  refused;
    char as[12];
    const char* given = ctrlCallText(call, "as");
    const char* wanted = ctrlCallText(call, "path");

    if ((refused = ctrlRefuseWithoutMachine(call, NULL)) != 0) {
        return refused;
    }
    /* The args column holds it to the size of as. */
    strcpy(as, given != NULL ? given : CTRL_SCREEN_AS_DEFAULT);

    /* A path is where a PNG goes, so it decides auto and contradicts text. */
    if (wanted != NULL && wanted[0] == 0) {
        return ctrlReplyError(call, "bad_path", "path is empty; give the full path of a new PNG file");
    }
    if (wanted == NULL) {
        wanted = "";
    }
    if (wanted[0] != 0 && strcmp(as, "text") == 0) {
        return ctrlReplyError(call, "bad_path",
                              "path is where a PNG goes; leave it out with as=text.");
    }
    if (wanted[0] != 0 && strcmp(as, "auto") == 0) {
        strcpy(as, "png");
    }
    /* The screen mode is read only from a paused machine; a running one gets the PNG,
    ** as does a V9990 picture, which has no text to read. */
    if (strcmp(as, "auto") == 0) {
        int superimposed;

        strcpy(as, call->view.held == DBG_ACCESS_VIEW && ctrlScreenIsTextMode() &&
                   !ctrlScreenV9990Shown(&superimposed) ? "text" : "png");
    }

    if (strcmp(as, "text") == 0) {
        return screenAsText(call);
    }
    if (strcmp(as, "png") == 0) {
        return screenAsPng(call, wanted);
    }
    return ctrlReplyError(call, "bad_as",
                          "\"as\" must be auto, text or png");
}
