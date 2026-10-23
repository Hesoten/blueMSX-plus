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
#ifndef CONTROL_REQUEST_H
#define CONTROL_REQUEST_H

#include "ControlClient.h"

/* client is from ctrlHelloAnswer. reply gets one JSON line without the newline;
** returns its length. */
int ctrlRequestHandle(CtrlClient client, const char* request,
                      char* reply, int replyLen);

/* The reply to a line longer than a request may be, of which line holds the start: nothing
** of it is read but its id. Returns the reply's length. */
int ctrlRequestRefuseLine(const char* line, char* reply, int replyLen);

#endif
