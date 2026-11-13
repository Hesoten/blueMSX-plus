/*****************************************************************************
**
** Agent keyboard and joystick input.
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
#ifndef CONTROL_INPUT_H
#define CONTROL_INPUT_H

#include "MsxTypes.h"

/* Both refuse a paused machine or a queue still busy with earlier keys, take hold
** and gap times in frames, and return once the keys have been delivered. They return
** CTRL_INPUT_STOPPED when the machine paused first, the rest staying queued, or stopped. */

#define CTRL_INPUT_PRESS   0
#define CTRL_INPUT_RELEASE 1
#define CTRL_INPUT_TAP     2

#define CTRL_INPUT_STOPPED (-2)

/* keys: inputEventCodeToString names joined by '+', e.g. "ctrl+stop". how is CTRL_INPUT_*.
** Returns -1 when a name is not a key, and 0 when the machine cannot take the keys. */
int ctrlInputKey(const char* keys, int how, int holdFrames, int gapFrames,
                 char* err, int errLen);

/* Every name ctrlInputKey accepts, separated by spaces. */
void ctrlInputKeyNames(char* out, int outLen);

/* Types characters on the layout the BIOS decodes; \n is return, \t tab. A character
** with no key fails the call. note gets a warning when a lock light is on. */
int ctrlInputType(const char* text, int holdFrames, int gapFrames,
                  int* keyEventsOut, int* framesOut, char* note, int noteLen,
                  char* err, int errLen);

#endif
