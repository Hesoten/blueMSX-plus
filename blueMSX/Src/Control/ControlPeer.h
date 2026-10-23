/*****************************************************************************
**
** Client connection state.
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
#ifndef CONTROL_PEER_H
#define CONTROL_PEER_H

#include <stdint.h>

/* The serving thread's connection, a socket handle as uintptr_t, from accept to close;
** the invalid handle between. */
void ctrlPeerSet(uintptr_t socketHandle);

/* On the serving thread during a long request: 1 once the client has closed or reset the connection. */
int  ctrlPeerGone(void);

/* Ends the connection's reads and writes, so its serving thread can be joined. */
void ctrlPeerShutdown(void);

#endif
