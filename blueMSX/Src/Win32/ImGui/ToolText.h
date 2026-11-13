/*****************************************************************************
**
** Tool window text in the application's language.
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
#ifndef TOOL_TEXT_H
#define TOOL_TEXT_H

#include <stddef.h>
#include "LanguageToolIds.h"

/* A row of the tool windows' string table; read every frame, so a language change shows at once. */
const char* ts(ToolStringId id);

/* snprintf that cuts only between UTF-8 characters. */
int textf(char* out, size_t size, const char* fmt, ...);

/* "text###key": shows text and keeps key as the ImGui id in every language. */
struct IdLabel {
    char s[256];
    IdLabel(const char* text, const char* key);
    operator const char*() const { return s; }
};

#endif
