/*****************************************************************************
**
** Win32 control socket work handoff to the window thread.
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
#include "ControlWindowThread.h"
#include <windows.h>
#include "ControlPeer.h"
#include "ControlClient.h"
#include "Win32Common.h"

/* Work handed to the window's thread. Only work not yet taken may be given up on,
** and a message left over runs only work posted since, never the abandoned one. */
#define WORK_IDLE      0
#define WORK_PREPARING 1
#define WORK_POSTED    2
#define WORK_RUNNING   3
#define WORK_FINISHED  4
#define WORK_ABANDONED 5

static volatile LONG workState;
static volatile LONG workResult;
static void        (*postedWork)(int arg);
static int           postedArg;

int ctrlWindowThreadRun(void (*work)(int arg), int arg, int timeoutMs)
{
    DWORD started = GetTickCount();

    /* One that finished after its caller gave up waiting leaves the channel free. */
    InterlockedCompareExchange(&workState, WORK_IDLE, WORK_FINISHED);
    if (InterlockedCompareExchange(&workState, WORK_PREPARING, WORK_IDLE) != WORK_IDLE &&
        InterlockedCompareExchange(&workState, WORK_PREPARING, WORK_ABANDONED) != WORK_ABANDONED)
    {
        return CTRL_WINDOW_THREAD_BUSY;
    }
    postedWork = work;
    postedArg  = arg;
    InterlockedExchange(&workState, WORK_POSTED);
    if (getMainHwnd() == NULL ||
        !PostMessage(getMainHwnd(), WM_CTRLGUICOMMAND, 0, 0))
    {
        InterlockedExchange(&workState, WORK_IDLE);
        return CTRL_WINDOW_THREAD_NOT_TAKEN;
    }

    while (GetTickCount() - started < (DWORD)timeoutMs &&
           !ctrlClientAllBlocked() && !ctrlPeerGone())
    {
        if (InterlockedCompareExchange(&workState, WORK_IDLE, WORK_FINISHED) == WORK_FINISHED) {
            return (int)workResult;
        }
        Sleep(50);
    }
    if (InterlockedCompareExchange(&workState, WORK_ABANDONED, WORK_POSTED) == WORK_POSTED) {
        return CTRL_WINDOW_THREAD_NOT_TAKEN;
    }
    if (InterlockedCompareExchange(&workState, WORK_IDLE, WORK_FINISHED) == WORK_FINISHED) {
        return (int)workResult;
    }
    return CTRL_WINDOW_THREAD_UNFINISHED;
}

void ctrlWindowThreadRunPosted(int ready)
{
    if (InterlockedCompareExchange(&workState, WORK_RUNNING, WORK_POSTED) != WORK_POSTED) {
        InterlockedCompareExchange(&workState, WORK_IDLE, WORK_ABANDONED);
        return;
    }
    if (ready) {
        postedWork(postedArg);
    }
    InterlockedExchange(&workResult, ready ? CTRL_WINDOW_THREAD_DONE : CTRL_WINDOW_THREAD_DIALOG);
    InterlockedExchange(&workState, WORK_FINISHED);
}
