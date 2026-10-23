/*****************************************************************************
**
** Test-only race delays and thread checks.
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
#include "RaceTest.h"

#ifdef BMSX_RACE_TEST
#include <windows.h>
#include <stdio.h>

static volatile LONG armed[RACE_POINTS];

/* Lines go to %TEMP%\bmsx-race-<pid>.log with the tick count, as the race plugin writes its own. */
void raceTestLog(const char* text)
{
    static CRITICAL_SECTION lock;
    static LONG ready;
    char path[MAX_PATH];
    char dir[MAX_PATH];
    FILE* file;

    if (InterlockedCompareExchange(&ready, 1, 0) == 0) {
        InitializeCriticalSection(&lock);
        InterlockedExchange(&ready, 2);
    }
    while (ready != 2) {
        Sleep(0);
    }
    EnterCriticalSection(&lock);
    GetTempPathA(sizeof(dir), dir);
    _snprintf(path, sizeof(path), "%sbmsx-race-%lu.log", dir, GetCurrentProcessId());
    file = fopen(path, "a");
    if (file != NULL) {
        fprintf(file, "%llu %lu %s\n", (unsigned long long)GetTickCount64(), GetCurrentThreadId(),
                text);
        fclose(file);
    }
    LeaveCriticalSection(&lock);
}

void raceTestArm(int point, int ms)
{
    char text[64];

    if (point > 0 && point < RACE_POINTS) {
        InterlockedExchange(&armed[point], ms);
        _snprintf(text, sizeof(text), "armed %d %d", point, ms);
        raceTestLog(text);
    }
}

void raceTestPoint(int point)
{
    LONG ms = armed[point] != 0 ? InterlockedExchange(&armed[point], 0) : 0;
    char text[64];

    if (ms > 0) {
        _snprintf(text, sizeof(text), "point %d sleeps %ld", point, ms);
        raceTestLog(text);
        Sleep(ms);
        _snprintf(text, sizeof(text), "point %d woke", point);
        raceTestLog(text);
    }
}
#endif
