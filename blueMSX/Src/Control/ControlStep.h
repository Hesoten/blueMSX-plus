/*****************************************************************************
**
** Step over for call and rst instructions.
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
#ifndef CONTROL_STEP_H
#define CONTROL_STEP_H

#include "ControlView.h"
#include "MsxTypes.h"

/* For a call or rst: 0, with in stopAt where it may return (two after an rst whose BIOS
** routine skips inline bytes). Else 1: a plain step does it, once the view is let go. */
int  ctrlStepOverStops(CtrlView* view, UInt16* stopAt, int* stops);

/* A temporary breakpoint at each stop that has none yet; hadBreakpoint says which had. */
void ctrlStepSetStops(const UInt16* stopAt, int stops, int* hadBreakpoint);
/* Clears the temporary ones ctrlStepSetStops set. */
void ctrlStepClearStops(const UInt16* stopAt, int stops, const int* hadBreakpoint);

#endif
