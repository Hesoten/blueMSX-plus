/*****************************************************************************
**
** CPU register access.
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
#ifndef CONTROL_CPU_H
#define CONTROL_CPU_H

#include "Debugger.h"

/* The CPU's register bank in a snapshot the caller took, or NULL. */
DbgRegisterBank* ctrlCpuRegisterBankIn(DbgSnapshot* snapshot);

/* The program counter from a snapshot of its own, so also -1 without a view. */
Int32 ctrlCpuPc(void);

/* A 16 bit CPU register by name in any case, from a snapshot of its own, so -1 without a
** view. */
Int32 ctrlCpuRegister(const char* name);

/* A breakpoint at pc: a plain one, or a segment one for the segment paged in there now. */
int ctrlCpuBreakpointAt(Int32 pc);

/* The CPU tests a breakpoint before the instruction at it runs, but not again in a HALT
** it already waits in, so a run from pc stops there at once. */
int ctrlCpuStopsAtOnce(Int32 pc);

#endif
