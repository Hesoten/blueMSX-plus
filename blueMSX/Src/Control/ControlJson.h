/*****************************************************************************
**
** Minimal JSON reader and writer for control socket messages.
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
#ifndef CONTROL_JSON_H
#define CONTROL_JSON_H

#include "MsxTypes.h"

/* A request is one flat object of scalars per line; the reader refuses anything else. */

/* ---- reading ---------------------------------------------------------- */

/* What jsonGetText and jsonGetKind find under a key; null reads as absent. */
typedef enum {
    JSON_ABSENT,
    JSON_TEXT_OK,
    JSON_WRONG_TYPE,  /* not a string, or one badly escaped */
    JSON_TOO_LONG,
    JSON_NUMBER,      /* a whole number, quoted or not, as jsonGetInt reads one */
    JSON_STRING,
    JSON_BOOL,
    JSON_OTHER
} JsonResult;

/* A string decoded into out (\u becomes UTF-8; \u0000 and a lone surrogate are refused):
** JSON_TEXT_OK, JSON_ABSENT, JSON_WRONG_TYPE or JSON_TOO_LONG. Else out is empty. */
int jsonGetText(const char* json, const char* key, char* out, int outLen);
/* JSON_ABSENT, JSON_NUMBER, JSON_STRING, JSON_BOOL or JSON_OTHER. */
int jsonGetKind(const char* json, const char* key);

/* Non-zero when jsonGetText reads the string whole; whenever it returns 0, out is empty. */
int jsonGetString(const char* json, const char* key, char* out, int outLen);
int jsonGetInt(const char* json, const char* key, Int32* out);
/* Unclamped, for a caller that must tell -1 from 0xFFFFFFFF; out of range reads as _I64_MAX or _I64_MIN. */
int jsonGetInt64(const char* json, const char* key, Int64* out);
int jsonGetBool(const char* json, const char* key, int* out);
int jsonHasKey(const char* json, const char* key);

/* Copies out the text of a nested object so the flat readers above can be used on it. */
int jsonGetObject(const char* json, const char* key, char* out, int outLen);

/* The name of the index-th top-level key, as written; zero past the last one. */
int jsonKeyAt(const char* json, int index, char* key, int keyLen);

/* Non-zero when key is present and holds null. */
int jsonIsNull(const char* json, const char* key);

/* ---- writing ---------------------------------------------------------- */

typedef struct {
    char*  data;
    int    length;
    int    capacity;
    int    overflowed;
    int    inObject;
    int    needComma;
} JsonOut;

void jsonOutInit(JsonOut* out, char* buffer, int capacity);
void jsonOutBeginObject(JsonOut* out);
void jsonOutEndObject(JsonOut* out);
void jsonOutString(JsonOut* out, const char* key, const char* value);
void jsonOutInt(JsonOut* out, const char* key, Int32 value);
void jsonOutUInt(JsonOut* out, const char* key, UInt32 value);
void jsonOutInt64(JsonOut* out, const char* key, Int64 value);
void jsonOutBool(JsonOut* out, const char* key, int value);

/* Escapes text as the inside of a JSON string, whole characters only, stopping 8 bytes
** short of outLen; 0 when it had to stop before the end of text. */
int  jsonEscape(const char* text, char* out, int outLen);

/* Non-zero when the buffer ran out; the caller must send an error, not the truncated reply. */
int  jsonOutOverflowed(const JsonOut* out);

#endif
