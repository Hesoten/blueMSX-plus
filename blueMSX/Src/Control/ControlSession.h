/*****************************************************************************
**
** Per-connection agent sessions.
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
#ifndef CONTROL_SESSION_H
#define CONTROL_SESSION_H

#include "ControlClient.h"

/* One per connection. */
typedef struct {
    CtrlClient client;
    /* The last stop this client was told of: status (ControlOpRun.c) moves it on; a new
    ** session sets it. */
    UInt32     stopCursor;
} CtrlSession;

/* The session of a client, made on first use; NULL when every one is taken. */
CtrlSession* ctrlSessionFor(CtrlClient client);

/* Once per connection before any request. */
void ctrlSessionOpen(CtrlClient client);
void ctrlSessionClose(CtrlClient client);

#endif
