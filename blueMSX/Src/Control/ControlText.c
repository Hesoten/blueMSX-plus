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
#include "ControlText.h"
#include <stdio.h>
#include <stdarg.h>

void ctrlTextInit(CtrlText* text, char* buffer, int capacity)
{
    text->data     = buffer;
    text->length   = 0;
    text->capacity = capacity;
    text->full     = 0;
    if (capacity > 0) {
        buffer[0] = 0;
    }
}

int ctrlTextIsFull(const CtrlText* text)
{
    return text->full;
}

int ctrlTextHexDigit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void ctrlTextAdd(CtrlText* text, const char* format, ...)
{
    va_list args;
    int room;
    int written;

    if (text->full) {
        return;
    }
    room = text->capacity - text->length;
    if (room <= 1) {
        text->full = 1;
        return;
    }

    va_start(args, format);
    written = vsnprintf(text->data + text->length, (size_t)room, format, args);
    va_end(args);

    if (written < 0 || written >= room) {
        text->data[text->length] = 0;
        text->full = 1;
        return;
    }
    text->length += written;
}
