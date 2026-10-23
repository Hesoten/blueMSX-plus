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
#ifndef RACE_TEST_H
#define RACE_TEST_H

/* Each point sleeps once, for as long as it was armed, the next time a thread passes it. */
#define RACE_PARK_DELAY    1  /* WaitForSync, before it says the thread is parked */
#define RACE_PARK_TAIL     2  /* WaitForSync, after its wait, before the thread runs on */
#define RACE_PAUSE_CHECK   3  /* dbgPause, after it saw the machine running */
#define RACE_RESUME_NOTIFY 4  /* a resume, between the state change and its notify */
#define RACE_IN_VIEW       5  /* a view, once it is held */
#define RACE_SUSPEND_CHECK 7  /* emulatorSuspend, between its look and its switch */
#define RACE_POINTS        8

#ifdef BMSX_RACE_TEST
void raceTestArm(int point, int ms);
void raceTestPoint(int point);
void raceTestLog(const char* text);
#define RACE_TEST_POINT(point) raceTestPoint(point)
#define RACE_TEST_CHECK(holds, what) do { if (!(holds)) raceTestLog("CHECK FAILED " what); } while (0)
#else
#define RACE_TEST_POINT(point)
#define RACE_TEST_CHECK(holds, what)
#endif

#endif
