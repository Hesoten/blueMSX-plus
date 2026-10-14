/*****************************************************************************
**
** Removable drives of the host inserted as hard disks.
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
#ifndef WIN32_PHYSICAL_DISK_H
#define WIN32_PHYSICAL_DISK_H

/* Lets the user pick a removable drive and inserts it in a hard disk drive. */
void physicalDiskInsert(int driveId);

/* Whether the interface behind the drive takes a physical drive. */
int physicalDiskAllowed(int driveId);

/* Releases every drive that no longer holds, or may no longer hold, its
** physical drive. */
void physicalDiskSync(void);

void physicalDiskEjectAll(void);

/* Runs the elevated side when the command line asks for it; returns nonzero
** then, and the process must end with *exitCode. */
int physicalDiskHelperCommand(const char* commandLine, int* exitCode);

#endif
