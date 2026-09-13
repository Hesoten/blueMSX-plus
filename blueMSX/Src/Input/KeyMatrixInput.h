/*****************************************************************************
**
** Key presses fed to the MSX keyboard matrix from outside the emulator.
** Copyright (C) 2026 madscient
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
#ifndef KEY_MATRIX_INPUT_H
#define KEY_MATRIX_INPUT_H

/* A request is ASCII text of commands separated by white space:
**
**   d <row> <mask>   press the keys of <mask> in matrix row <row>
**   u <row> <mask>   release them
**   w <ms>           wait <ms> milliseconds of emulated time
**
** Numbers are decimal, or hexadecimal with a 0x prefix. Rows are 0-11 and a
** mask may only name bits that have a key. Which character a key gives is up
** to the MSX, so nothing here knows about keyboard layouts.
*/

void keyMatrixInputInit(void);

/* Queues a whole request, or nothing: returns 0 when any part is malformed,
** out of range, or the running board has no MSX keyboard matrix. */
int keyMatrixInputSubmit(const char* text);

/* Commands queued and not yet carried out; a wait counts until it ends. */
int keyMatrixInputRemaining(void);

/* Called by the board around a run, from the emulation thread. */
void keyMatrixInputBoardStart(void);
void keyMatrixInputBoardStop(void);

#endif
