/*****************************************************************************
**
** Read access to a paused or running machine.
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
#include "ControlView.h"
#include "Debugger.h"
#include "Emulator.h"
#include <stddef.h>

int ctrlViewTake(CtrlView* view, int kind, int timeoutMs)
{
    int result = dbgAccessBegin(kind, timeoutMs);

    view->held = result == DBG_ACCESS_OK ? kind : 0;
    return result;
}

int ctrlViewTakeBest(CtrlView* view, int timeoutMs)
{
    int result = DBG_ACCESS_NOT_PAUSED;

    if (emulatorGetState() == EMU_PAUSED) {
        result = ctrlViewTake(view, DBG_ACCESS_VIEW, timeoutMs);
    }
    /* Running, or it ran on before the view was taken. */
    return result == DBG_ACCESS_NOT_PAUSED ? ctrlViewTake(view, DBG_ACCESS_HOLD, timeoutMs)
                                           : result;
}

void ctrlViewRelease(CtrlView* view)
{
    if (view->held) {
        view->held = 0;
        dbgAccessEnd();
    }
}

DbgSnapshot* ctrlViewDevices(void)
{
    return dbgAccessHeld() == DBG_ACCESS_VIEW ? dbgSnapshotCreate() : NULL;
}
