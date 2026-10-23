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

/* Non-zero when name is a register, or a half of one, that can be set; else 0 with err. */
int ctrlCpuCheckName(const char* name, char* err, int errLen);

/* Sets a CPU register by its printed name or an 8 bit half (A, H, IXL...).
** The caller must hold a view. *previous, if not NULL, gets the old value.
** Returns 0 with err filled on refusal, -1 when no view gave the registers. */
int ctrlCpuSetRegister(const char* name, UInt32 value, Int32* previous,
                       char* err, int errLen);

/* Return addresses from the CPU's call log still on the stack, innermost first, with where
** on the stack each is. notFound counts log entries left out, innerNotFound those inside
** the first one kept. Returns how many were written, or -1 without a view. */
int ctrlCpuCallstack(UInt16* out, UInt16* where, int max, int* notFound, int* innerNotFound);

/* A breakpoint at pc: a plain one, or a segment one for the segment paged in there now. */
int ctrlCpuBreakpointAt(Int32 pc);

/* The CPU tests a breakpoint before the instruction at it runs, but not again in a HALT
** it already waits in, so a run from pc stops there at once. */
int ctrlCpuStopsAtOnce(Int32 pc);

#endif
