/*****************************************************************************
**
** Screen text and screenshots.
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
#ifndef CONTROL_SCREEN_H
#define CONTROL_SCREEN_H

#include "ControlText.h"
#include "ControlView.h"

int ctrlScreenIsTextMode(void);

/* Needs a view. Nonzero while a V9990 picture is on the screen; superimposed is set
** if the VDP picture shows under it. */
int ctrlScreenV9990Shown(int* superimposed);

/* Needs a held view. Fails with err in a graphics mode rather than returning pixels. */
int ctrlScreenText(CtrlView* view, CtrlText* out, char* err, int errLen);

#define CTRL_SCREEN_OK           1
#define CTRL_SCREEN_FAILED       0
#define CTRL_SCREEN_NOT_DRAWING -1  /* minimized under Off, and no frame drawn since */
#define CTRL_SCREEN_FILE_EXISTS -2  /* a path given names a file that is there */

/* Writes the last finished frame as a PNG; pathOut gets where it went. No view needed.
** A path given must name a new file; without one, the emulator's own file is replaced. */
int ctrlScreenPng(const char* wantedPath, char* pathOut, int pathOutLen,
                  char* err, int errLen);

#endif
