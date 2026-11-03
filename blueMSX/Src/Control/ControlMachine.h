/*****************************************************************************
**
** Machine reset for agents.
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
#ifndef CONTROL_MACHINE_H
#define CONTROL_MACHINE_H

/* What the last ctrlMachineReset did. */
#define CTRL_MACHINE_RESET_DONE         0
#define CTRL_MACHINE_RESET_NOT_RUNNING  1  /* soft: the machine was not running; nothing done  */
#define CTRL_MACHINE_RESET_FILE_MISSING 2  /* hard: the reason names it; nothing done          */
#define CTRL_MACHINE_RESET_NOT_STARTED  3  /* hard: off, and not on again; the reason says why */

/* On the window's thread, as the menu's own reset: hard switches off and on again. */
void ctrlMachineReset(int hard);

/* Read once ctrlMachineReset has returned. */
int         ctrlMachineResetOutcome(void);
const char* ctrlMachineResetReason(void);
/* What a hard reset did not start afresh (saved flash, kept folder disks), as reply lines. */
const char* ctrlMachineResetNotes(void);

/* A disk drive as the emulator's menus place it: drive A or B, or a hard disk unit. */
void ctrlMachineDriveName(int drive, char* out, int outLen);

#endif
