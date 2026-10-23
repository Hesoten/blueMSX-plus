/*****************************************************************************
**
** Fixed-size text buffer for replies.
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
#ifndef CONTROL_TEXT_H
#define CONTROL_TEXT_H

/* Fixed-size text buffer: an append that does not fit is dropped whole and sets full. */
typedef struct {
    char* data;
    int   length;
    int   capacity;
    int   full;
} CtrlText;

void ctrlTextInit(CtrlText* text, char* buffer, int capacity);
void ctrlTextAdd(CtrlText* text, const char* format, ...);

/* The value of a hex digit, 0-15, or -1 when c is none. */
int  ctrlTextHexDigit(int c);

#endif
