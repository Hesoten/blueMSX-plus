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
#include <stdarg.h>
#include <stdio.h>
#include "ToolText.h"
extern "C" {
#include "Language.h"
}

const char* ts(ToolStringId id)
{
    return langToolStr(id);
}

int textf(char* out, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(out, size, fmt, args);
    va_end(args);
    if (n < 0 || size == 0 || (size_t)n < size) {
        return n;
    }
    /* Cut: drop a character whose bytes did not all fit. */
    size_t end = size - 1, lead = end;
    while (lead > 0 && ((unsigned char)out[lead - 1] & 0xc0) == 0x80) lead--;
    if (lead > 0) {
        unsigned char c = (unsigned char)out[lead - 1];
        size_t need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (end - (lead - 1) < need) out[lead - 1] = 0;
    }
    return n;
}

IdLabel::IdLabel(const char* text, const char* key)
{
    textf(s, sizeof(s), "%s###%s", text, key);
}

/* ---- format check --------------------------------------------------- */

/* A translation is printed with the English's arguments, so it must hold the
** same conversions in the same order, flags, width and length included. */
namespace {

constexpr bool isFlagOrDigit(char c)
{
    return c == '-' || c == '+' || c == ' ' || c == '#' || c == '.' || (c >= '0' && c <= '9');
}

constexpr bool isLength(char c)
{
    return c == 'h' || c == 'l' || c == 'L' || c == 'z' || c == 'j' || c == 't' || c == 'I';
}

/* The '%' of the next conversion at or after i, or -1; "%%" is text. */
constexpr int nextConversion(const char* s, int i)
{
    while (s[i]) {
        if (s[i] == '%' && s[i + 1] == '%') i += 2;
        else if (s[i] == '%') return i;
        else i++;
    }
    return -1;
}

/* Past the conversion at '%' i, or -1 for one this check does not take (e.g. '*'). */
constexpr int conversionEnd(const char* s, int i)
{
    i++;
    while (isFlagOrDigit(s[i])) i++;
    while (isLength(s[i]) || (s[i] >= '0' && s[i] <= '9')) i++;
    return s[i] && s[i] != '*' && s[i] != '%' ? i + 1 : -1;
}

constexpr bool sameConversions(const char* en, const char* ja)
{
    if (!*ja) return true;
    int a = 0, b = 0;
    for (;;) {
        a = nextConversion(en, a);
        b = nextConversion(ja, b);
        if (a < 0 || b < 0) return a == b;
        int ae = conversionEnd(en, a), be = conversionEnd(ja, b);
        if (ae < 0 || be < 0 || ae - a != be - b) return false;
        for (int k = 0; k < ae - a; k++) {
            if (en[a + k] != ja[b + k]) return false;
        }
        a = ae;
        b = be;
    }
}

#define TS(id, en, ja) static_assert(sameConversions(en, ja), "TS_" #id ": the translation's conversions differ from the English");
#include "LanguageToolStrings.h"
#undef TS

}
