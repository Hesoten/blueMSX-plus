/*****************************************************************************
**
** Breakpoint condition expressions.
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
#ifndef CONTROL_EXPR_H
#define CONTROL_EXPR_H

#include "Debugger.h"

/* Why a text did not compile, and the byte of it the complaint is about. */
typedef struct {
    int  pos;
    char message[160];
} CtrlExprError;

/* Compiles a C-like 32-bit expression into out's ops: registers, flags zf cf, [addr] a byte
** and {addr} a word as the CPU sees them, numbers as in an address, and @label as the
** symbol's assembled Z80 address, fixed here. Non-zero when it did; else error says why. */
int ctrlExprCompile(const char* text, DbgBreakCondition* out, CtrlExprError* error);

#endif
