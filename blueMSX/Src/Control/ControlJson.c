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
#include "ControlJson.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char* skipWs(const char* s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return s;
}

/* Past the closing quote of a string literal, or NULL if unterminated. */
static const char* skipString(const char* s)
{
    if (*s != '"') {
        return NULL;
    }
    s++;
    while (*s != 0 && *s != '"') {
        if (*s == '\\' && s[1] != 0) {
            s++;
        }
        s++;
    }
    return *s == '"' ? s + 1 : NULL;
}

/* Past a whole value of any kind, or NULL if malformed. */
static const char* skipValue(const char* s)
{
    int depth;

    s = skipWs(s);
    if (*s == '"') {
        return skipString(s);
    }
    if (*s == '{' || *s == '[') {
        depth = 0;
        while (*s != 0) {
            if (*s == '"') {
                s = skipString(s);
                if (s == NULL) {
                    return NULL;
                }
                continue;
            }
            if (*s == '{' || *s == '[') {
                depth++;
            }
            else if (*s == '}' || *s == ']') {
                depth--;
                if (depth == 0) {
                    return s + 1;
                }
            }
            s++;
        }
        return NULL;
    }
    while (*s != 0 && *s != ',' && *s != '}' && *s != ']' &&
           *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
    {
        s++;
    }
    return s;
}

/* The value belonging to key, at the top level of the object only. */
static const char* findValue(const char* json, const char* key)
{
    const char* s;
    size_t keyLen = strlen(key);

    if (json == NULL) {
        return NULL;
    }
    s = skipWs(json);
    if (*s != '{') {
        return NULL;
    }
    s++;

    for (;;) {
        const char* name;
        size_t nameLen;

        s = skipWs(s);
        if (*s == '}' || *s == 0) {
            return NULL;
        }
        if (*s != '"') {
            return NULL;
        }
        name = s + 1;
        s = skipString(s);
        if (s == NULL) {
            return NULL;
        }
        nameLen = (size_t)((s - 1) - name);

        s = skipWs(s);
        if (*s != ':') {
            return NULL;
        }
        s = skipWs(s + 1);

        if (nameLen == keyLen && strncmp(name, key, keyLen) == 0) {
            return s;
        }

        s = skipValue(s);
        if (s == NULL) {
            return NULL;
        }
        s = skipWs(s);
        if (*s == ',') {
            s++;
            continue;
        }
        return NULL;
    }
}

int jsonHasKey(const char* json, const char* key)
{
    return findValue(json, key) != NULL;
}

int jsonGetObject(const char* json, const char* key, char* out, int outLen)
{
    const char* s = findValue(json, key);
    const char* end;
    int length;

    if (s == NULL || (*s != '{' && *s != '[')) {
        return 0;
    }
    end = skipValue(s);
    if (end == NULL) {
        return 0;
    }
    length = (int)(end - s);
    if (length >= outLen) {
        return 0;
    }
    memcpy(out, s, (size_t)length);
    out[length] = 0;
    return 1;
}

int jsonKeyAt(const char* json, int index, char* key, int keyLen)
{
    const char* s;
    int at = 0;

    if (json == NULL || keyLen <= 0) {
        return 0;
    }
    s = skipWs(json);
    if (*s != '{') {
        return 0;
    }
    s++;

    for (;;) {
        const char* name;

        s = skipWs(s);
        if (*s != '"') {
            return 0;
        }
        name = s + 1;
        s = skipString(s);
        if (s == NULL) {
            return 0;
        }
        if (at == index) {
            size_t nameLen = (size_t)((s - 1) - name);
            if (nameLen > (size_t)keyLen - 1) {
                nameLen = (size_t)keyLen - 1;
            }
            memcpy(key, name, nameLen);
            key[nameLen] = 0;
            return 1;
        }
        s = skipWs(s);
        if (*s != ':') {
            return 0;
        }
        s = skipValue(s + 1);
        if (s == NULL) {
            return 0;
        }
        s = skipWs(s);
        if (*s != ',') {
            return 0;
        }
        s++;
        at++;
    }
}

int jsonIsNull(const char* json, const char* key)
{
    const char* s = findValue(json, key);

    return s != NULL && strncmp(s, "null", 4) == 0;
}

/* Its own, so the MCP server needs no other Control file. */
static int hexDigit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Four hex digits of a \u escape, or -1. */
static int hex4(const char* s)
{
    int value = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int digit = hexDigit((unsigned char)s[i]);
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

/* The character of a \u escape at s (just past the u), a surrogate pair as one; moves s to
** its last digit. 0 for none: a bad digit, \u0000 or a lone surrogate. */
static unsigned int unicodeEscape(const char** s)
{
    int value = hex4(*s);
    int low;

    if (value < 0 || value == 0 || (value >= 0xDC00 && value <= 0xDFFF)) {
        return 0;
    }
    *s += 3;
    if (value < 0xD800 || value > 0xDBFF) {
        return (unsigned int)value;
    }
    if ((*s)[1] != '\\' || (*s)[2] != 'u' || (low = hex4(*s + 3)) < 0xDC00 || low > 0xDFFF) {
        return 0;
    }
    *s += 6;
    return 0x10000 + (((unsigned int)value - 0xD800) << 10) + ((unsigned int)low - 0xDC00);
}

/* value as UTF-8 into bytes; returns how many. */
static int utf8Encode(unsigned int value, unsigned char* bytes)
{
    if (value < 0x80) {
        bytes[0] = (unsigned char)value;
        return 1;
    }
    if (value < 0x800) {
        bytes[0] = (unsigned char)(0xC0 | (value >> 6));
        bytes[1] = (unsigned char)(0x80 | (value & 0x3F));
        return 2;
    }
    if (value < 0x10000) {
        bytes[0] = (unsigned char)(0xE0 | (value >> 12));
        bytes[1] = (unsigned char)(0x80 | ((value >> 6) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | (value & 0x3F));
        return 3;
    }
    bytes[0] = (unsigned char)(0xF0 | (value >> 18));
    bytes[1] = (unsigned char)(0x80 | ((value >> 12) & 0x3F));
    bytes[2] = (unsigned char)(0x80 | ((value >> 6) & 0x3F));
    bytes[3] = (unsigned char)(0x80 | (value & 0x3F));
    return 4;
}

/* The escape at s (just past the backslash) into bytes, moving s to its last character;
** returns how many bytes, 0 when it is no valid escape. */
static int decodeEscape(const char** s, unsigned char* bytes)
{
    static const char from[] = "ntrbf\"\\/";
    static const char to[]   = "\n\t\r\b\f\"\\/";
    const char* at = **s != 0 ? strchr(from, **s) : NULL;
    unsigned int value;

    if (at != NULL) {
        bytes[0] = (unsigned char)to[at - from];
        return 1;
    }
    if (**s != 'u') {
        return 0;
    }
    (*s)++;
    value = unicodeEscape(s);
    return value != 0 ? utf8Encode(value, bytes) : 0;
}

int jsonGetText(const char* json, const char* key, char* out, int outLen)
{
    const char* s = findValue(json, key);
    int written = 0;

    if (outLen <= 0) {
        return JSON_TOO_LONG;
    }
    out[0] = 0;
    if (s == NULL || strncmp(s, "null", 4) == 0) {
        return JSON_ABSENT;
    }
    if (*s != '"') {
        return JSON_WRONG_TYPE;
    }
    for (s++; *s != 0 && *s != '"'; s++) {
        unsigned char bytes[4];
        int count = 1;
        int i;

        bytes[0] = (unsigned char)*s;
        if (*s == '\\') {
            s++;
            if ((count = decodeEscape(&s, bytes)) == 0) {
                out[0] = 0;
                return JSON_WRONG_TYPE;
            }
        }
        if (written + count > outLen - 1) {
            out[0] = 0;
            return JSON_TOO_LONG;
        }
        for (i = 0; i < count; i++) {
            out[written++] = (char)bytes[i];
        }
    }
    if (*s != '"') {
        out[0] = 0;
        return JSON_WRONG_TYPE;
    }
    out[written] = 0;
    return JSON_TEXT_OK;
}

int jsonGetString(const char* json, const char* key, char* out, int outLen)
{
    return jsonGetText(json, key, out, outLen) == JSON_TEXT_OK;
}

/* In 64 bits, so the callers decide what is too large; out of range reads as _I64_MAX or _I64_MIN. */
static int getNumber(const char* json, const char* key, Int64* out)
{
    const char* s = findValue(json, key);
    const char* end;
    Int64 value;

    if (s == NULL) {
        return 0;
    }
    /* A quoted number may be hex, e.g. "0xC000". */
    if (*s == '"') {
        char text[32];
        const char* digits;
        if (jsonGetText(json, key, text, sizeof(text)) != JSON_TEXT_OK) {
            return 0;
        }
        /* Decimal as in addresses ("016" is 16); only 0x marks hex.
        ** Leading spaces or a + are not numbers. */
        digits = text[0] == '-' ? text + 1 : text;
        if (!isdigit((unsigned char)digits[0])) {
            return 0;
        }
        value = _strtoi64(text, (char**)&end,
                          digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X') ? 16 : 10);
        if (end == text || *end != 0) {
            return 0;
        }
        *out = value;
        return 1;
    }

    value = _strtoi64(s, (char**)&end, 10);
    if (end == s) {
        return 0;
    }
    /* 16.0 is 16; 16.5, 1e1 and 16abc are not whole numbers, so they are refused. */
    if (*end == '.') {
        do {
            end++;
        } while (*end == '0');
    }
    if (*end != 0 && *end != ',' && *end != '}' && *end != ']' &&
        *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
    {
        return 0;
    }
    *out = value;
    return 1;
}

int jsonGetInt(const char* json, const char* key, Int32* out)
{
    Int64 value;

    if (!getNumber(json, key, &value)) {
        return 0;
    }
    /* Out of range is clamped. */
    *out = value > 0x7FFFFFFF      ? 0x7FFFFFFF :
           value < -0x7FFFFFFF - 1 ? -0x7FFFFFFF - 1 : (Int32)value;
    return 1;
}

int jsonGetInt64(const char* json, const char* key, Int64* out)
{
    return getNumber(json, key, out);
}

int jsonGetKind(const char* json, const char* key)
{
    const char* s = findValue(json, key);
    Int64 number;

    if (s == NULL || strncmp(s, "null", 4) == 0) {
        return JSON_ABSENT;
    }
    if (getNumber(json, key, &number)) {
        return JSON_NUMBER;
    }
    if (*s == '"') {
        return JSON_STRING;
    }
    if (strncmp(s, "true", 4) == 0 || strncmp(s, "false", 5) == 0) {
        return JSON_BOOL;
    }
    return JSON_OTHER;
}

int jsonGetBool(const char* json, const char* key, int* out)
{
    const char* s = findValue(json, key);

    if (s == NULL) {
        return 0;
    }
    if (strncmp(s, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (strncmp(s, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

/* ---- writing ---------------------------------------------------------- */

void jsonOutInit(JsonOut* out, char* buffer, int capacity)
{
    out->data       = buffer;
    out->length     = 0;
    out->capacity   = capacity;
    out->overflowed = 0;
    out->inObject   = 0;
    out->needComma  = 0;
    if (capacity > 0) {
        buffer[0] = 0;
    }
}

static void put(JsonOut* out, const char* text, int length)
{
    if (out->overflowed) {
        return;
    }
    if (out->length + length >= out->capacity) {
        out->overflowed = 1;
        return;
    }
    memcpy(out->data + out->length, text, (size_t)length);
    out->length += length;
    out->data[out->length] = 0;
}

static void putc1(JsonOut* out, char c)
{
    put(out, &c, 1);
}

/* Bytes in the valid UTF-8 character text starts with, 1 for ASCII; 0 when it is not one.
** Strings are written with each such byte as U+FFFD, so a reply is always valid UTF-8. */
static int jsonUtf8Length(const char* text)
{
    const unsigned char* s = (const unsigned char*)text;
    int length;
    int i;

    if (s[0] < 0x80) {
        return 1;
    }
    if (s[0] >= 0xC2 && s[0] <= 0xDF) {
        length = 2;
    }
    else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        length = 3;
    }
    else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        length = 4;
    }
    else {
        return 0;
    }
    /* Overlong forms, UTF-16 surrogates and values past U+10FFFF are not characters. */
    if ((s[0] == 0xE0 && s[1] < 0xA0) || (s[0] == 0xED && s[1] > 0x9F) ||
        (s[0] == 0xF0 && s[1] < 0x90) || (s[0] == 0xF4 && s[1] > 0x8F))
    {
        return 0;
    }
    /* Stops at the NUL, which is not a continuation byte, so a cut sequence is refused. */
    for (i = 1; i < length; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    return length;
}

/* The next character of text escaped into buf, which takes 6 bytes; returns how many it
** wrote and moves text past it. A byte that starts no valid UTF-8 character is U+FFFD. */
static int escapeOne(const char** text, char* buf)
{
    unsigned char c = (unsigned char)*(*text)++;
    int length;

    switch (c) {
    case '"':  buf[0] = '\\'; buf[1] = '"';  return 2;
    case '\\': buf[0] = '\\'; buf[1] = '\\'; return 2;
    case '\n': buf[0] = '\\'; buf[1] = 'n';  return 2;
    case '\r': buf[0] = '\\'; buf[1] = 'r';  return 2;
    case '\t': buf[0] = '\\'; buf[1] = 't';  return 2;
    default:
        break;
    }
    if (c < 0x20) {
        return sprintf(buf, "\\u%04x", c);
    }
    if (c < 0x80) {
        buf[0] = (char)c;
        return 1;
    }
    /* A reply must be valid UTF-8 even when a message cut a character short. */
    length = jsonUtf8Length(*text - 1);
    if (length > 0) {
        memcpy(buf, *text - 1, (size_t)length);
        *text += length - 1;
        return length;
    }
    memcpy(buf, "\xEF\xBF\xBD", 3);
    return 3;
}

static void putEscaped(JsonOut* out, const char* text)
{
    putc1(out, '"');
    while (text != NULL && *text != 0) {
        char buf[8];
        int  length = escapeOne(&text, buf);

        put(out, buf, length);
    }
    putc1(out, '"');
}

int jsonEscape(const char* text, char* out, int outLen)
{
    int written = 0;

    while (*text != 0 && written < outLen - 8) {
        written += escapeOne(&text, out + written);
    }
    out[written] = 0;
    return *text == 0;
}

static void beginMember(JsonOut* out, const char* key)
{
    if (out->needComma) {
        putc1(out, ',');
    }
    putEscaped(out, key);
    putc1(out, ':');
    out->needComma = 1;
}

void jsonOutBeginObject(JsonOut* out)
{
    putc1(out, '{');
    out->inObject = 1;
    out->needComma = 0;
}

void jsonOutEndObject(JsonOut* out)
{
    putc1(out, '}');
    out->inObject = 0;
}

void jsonOutString(JsonOut* out, const char* key, const char* value)
{
    beginMember(out, key);
    putEscaped(out, value);
}

void jsonOutInt(JsonOut* out, const char* key, Int32 value)
{
    char text[16];

    beginMember(out, key);
    sprintf(text, "%d", (int)value);
    put(out, text, (int)strlen(text));
}

void jsonOutUInt(JsonOut* out, const char* key, UInt32 value)
{
    char text[16];

    beginMember(out, key);
    sprintf(text, "%u", (unsigned)value);
    put(out, text, (int)strlen(text));
}

void jsonOutInt64(JsonOut* out, const char* key, Int64 value)
{
    char text[24];

    beginMember(out, key);
    sprintf(text, "%I64d", value);
    put(out, text, (int)strlen(text));
}

void jsonOutBool(JsonOut* out, const char* key, int value)
{
    beginMember(out, key);
    if (value) {
        put(out, "true", 4);
    }
    else {
        put(out, "false", 5);
    }
}

int jsonOutOverflowed(const JsonOut* out)
{
    return out->overflowed;
}
