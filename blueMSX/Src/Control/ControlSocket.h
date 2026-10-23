/*****************************************************************************
**
** Platform-neutral control socket server interface.
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
#ifndef CONTROL_SOCKET_H
#define CONTROL_SOCKET_H

/* Refused while the thread a stop could not join still runs. */
int  ctrlSocketStart(void);
/* Ends the serving thread, waiting for a request in progress; it is killed only when
** exiting, otherwise left to end by itself and start refused until it has. */
void ctrlSocketStop(int exiting);
/* From the window's loop on each pass: notices when a thread Off could not join has ended. */
void ctrlSocketPoll(void);

int  ctrlSocketIsListening(void);
int  ctrlSocketPort(void);

#endif
