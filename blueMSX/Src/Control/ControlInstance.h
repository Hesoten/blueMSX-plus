/*****************************************************************************
**
** Instance file for tool discovery.
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
#ifndef CONTROL_INSTANCE_H
#define CONTROL_INSTANCE_H

/* Port and token are published in %LOCALAPPDATA%\blueMSXplus\agent\instance-<pid>.json. */

/* When the socket starts listening on port: a new token, files of dead emulators swept, and
** this one's written. */
void ctrlInstancePublish(int port);
/* When it stops listening: the file is removed, and no later machine change writes it. */
void ctrlInstanceRemove(void);

const char* ctrlInstanceToken(void);

/* Separate from start: the name is known only once a machine starts.
** NULL keeps the name and rewrites the file if the level changed. */
void ctrlInstanceSetMachine(const char* machineName);

/* The folder that holds the instance file. */
int  ctrlInstanceAgentFolder(char* out, int outLen);

#endif
