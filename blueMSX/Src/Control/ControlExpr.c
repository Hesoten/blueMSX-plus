/*****************************************************************************
**
** Breakpoint condition expressions.
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
#include "ControlExpr.h"
#include "ControlAddress.h"
#include "ControlSymbol.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char*        text;
    const char*        at;      /* the next character to read */
    DbgBreakCondition* out;
    int                depth;   /* values the code so far leaves on the stack */
    CtrlExprError*     error;
} Parser;

typedef struct {
    const char* text;
    int         op;
    int         precedence;
} BinaryOp;

/* Two-character ones first, so that "<=" is not read as "<". */
static const BinaryOp binaryOps[] = {
    { "||", DBG_COND_LOR,  1 }, { "&&", DBG_COND_LAND, 2 },
    { "==", DBG_COND_EQ,   6 }, { "!=", DBG_COND_NE,   6 },
    { "<=", DBG_COND_LE,   7 }, { ">=", DBG_COND_GE,   7 },
    { "<<", DBG_COND_SHL,  8 }, { ">>", DBG_COND_SHR,  8 },
    { "|",  DBG_COND_OR,   3 }, { "^",  DBG_COND_XOR,  4 }, { "&", DBG_COND_AND, 5 },
    { "<",  DBG_COND_LT,   7 }, { ">",  DBG_COND_GT,   7 },
    { "+",  DBG_COND_ADD,  9 }, { "-",  DBG_COND_SUB,  9 },
    { NULL }
};

static const struct {
    const char* name;
    int         reg;
} registerNames[] = {
    { "a",  DBG_REG_A },  { "f",  DBG_REG_F },  { "b",  DBG_REG_B },  { "c",  DBG_REG_C },
    { "d",  DBG_REG_D },  { "e",  DBG_REG_E },  { "h",  DBG_REG_H },  { "l",  DBG_REG_L },
    { "af", DBG_REG_AF }, { "bc", DBG_REG_BC }, { "de", DBG_REG_DE }, { "hl", DBG_REG_HL },
    { "ix", DBG_REG_IX }, { "iy", DBG_REG_IY }, { "sp", DBG_REG_SP }, { "pc", DBG_REG_PC },
    { "zf", DBG_REG_ZF }, { "cf", DBG_REG_CF },
    { NULL }
};

static int parseExpression(Parser* p, int minPrecedence);

/* Notes the first complaint, at where in the text; returns 0 for the caller to pass up. */
static int fail(Parser* p, const char* where, const char* format, ...)
{
    va_list args;

    if (p->error->message[0] != 0) {
        return 0;
    }
    p->error->pos = (int)(where - p->text);
    va_start(args, format);
    vsnprintf(p->error->message, sizeof(p->error->message), format, args);
    va_end(args);
    return 0;
}

static void skipSpace(Parser* p)
{
    while (*p->at == ' ' || *p->at == '\t') {
        p->at++;
    }
}

/* Appends one op, keeping the code within the evaluator's bounds. */
static int emit(Parser* p, const char* where, int op, Int32 value)
{
    int pushes = op == DBG_COND_CONST || op == DBG_COND_REG;
    int pops   = !pushes && !(op >= DBG_COND_BYTE && op <= DBG_COND_NEG);

    if (p->out->opCount >= DBG_COND_MAX_OPS) {
        return fail(p, where, "the condition needs more than %d steps", DBG_COND_MAX_OPS);
    }
    if (pushes && p->depth >= DBG_COND_MAX_STACK) {
        return fail(p, where, "more than %d values are pending here; nest less",
                    DBG_COND_MAX_STACK);
    }
    p->depth += pushes - pops;
    p->out->ops[p->out->opCount].op    = (UInt8)op;
    p->out->ops[p->out->opCount].value = value;
    p->out->opCount++;
    return 1;
}

static int expect(Parser* p, char closing)
{
    skipSpace(p);
    if (*p->at != closing) {
        return fail(p, p->at, "expected '%c'", closing);
    }
    p->at++;
    return 1;
}

static int isNameChar(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static int parseNumber(Parser* p)
{
    const char* where = p->at;
    UInt32 value = 0;
    const char* end = ctrlAddrScanNumber(p->at, &value);

    if (end == NULL) {
        return fail(p, where, value == CTRL_ADDR_TOO_LARGE ? "the number is too large"
                                                           : "expected a number");
    }
    if (isNameChar(*end)) {
        return fail(p, end, "unexpected '%c' after a number; a hexadecimal one takes 0x, $ or "
                    "a trailing h", *end);
    }
    p->at = end;
    return emit(p, where, DBG_COND_CONST, (Int32)value);
}

static int parseRegister(Parser* p)
{
    const char* where = p->at;
    int length = 0;
    int i;

    while (isNameChar(where[length])) {
        length++;
    }
    for (i = 0; registerNames[i].name != NULL; i++) {
        if ((int)strlen(registerNames[i].name) == length &&
            _strnicmp(registerNames[i].name, where, length) == 0)
        {
            p->at += length;
            return emit(p, where, DBG_COND_REG, registerNames[i].reg);
        }
    }
    return fail(p, where, "unknown name '%.*s': the registers are a f b c d e h l af bc de hl "
                "ix iy sp pc and the flags zf cf; a number that starts with a letter takes 0x "
                "or a leading 0", length > 32 ? 32 : length, where);
}

/* @NAME: the Z80 address its definitions share. */
static int parseLabel(Parser* p)
{
    CtrlSymbolInfo found[CTRL_SYM_BY_NAME_MAX];
    char name[CTRL_SYM_NAME_MAX];
    const char* where = p->at;
    int length = 0;
    int matches;
    int total = 0;
    int i;

    p->at++;
    while (p->at[length] != 0 && strchr(" \t()[]{}!~+-&^|<>=", p->at[length]) == NULL) {
        length++;
    }
    if (length == 0) {
        return fail(p, where, "expected a symbol name after the @");
    }
    if (length >= CTRL_SYM_NAME_MAX) {
        return fail(p, where, "a symbol name is at most %d bytes", CTRL_SYM_NAME_MAX - 1);
    }
    memcpy(name, p->at, length);
    name[length] = 0;

    matches = ctrlSymByName(name, found, CTRL_SYM_BY_NAME_MAX, 1, &total);
    if (matches == 0) {
        return fail(p, where, "no symbol called %.60s (names are case sensitive)", name);
    }
    for (i = 1; i < matches; i++) {
        if (found[i].z80Address != found[0].z80Address) {
            return fail(p, where, "%.60s is defined at more than one Z80 address; write the "
                        "address instead", name);
        }
    }
    if (total > matches) {
        return fail(p, where, "%.60s has more definitions than are looked through; write the "
                    "address instead", name);
    }
    p->at += length;
    return emit(p, where, DBG_COND_CONST, found[0].z80Address);
}

static int parsePrimary(Parser* p)
{
    const char* where;
    char c;

    skipSpace(p);
    where = p->at;
    c = *p->at;
    if (c == '(') {
        p->at++;
        return parseExpression(p, 1) && expect(p, ')');
    }
    if (c == '[' || c == '{') {
        p->at++;
        return parseExpression(p, 1) && expect(p, c == '[' ? ']' : '}') &&
               emit(p, where, c == '[' ? DBG_COND_BYTE : DBG_COND_WORD, 0);
    }
    if (c == '@') {
        return parseLabel(p);
    }
    if (isdigit((unsigned char)c) || c == '$' || c == '#') {
        return parseNumber(p);
    }
    if (isalpha((unsigned char)c) || c == '_') {
        return parseRegister(p);
    }
    if (c == 0) {
        return fail(p, where, "expected a value, but the condition ends");
    }
    return fail(p, where, "expected a value, not '%c'", c);
}

static int parseUnary(Parser* p)
{
    const char* where;
    char c;

    skipSpace(p);
    where = p->at;
    c = *p->at;
    if (c != '!' && c != '~' && c != '-' && c != '+') {
        return parsePrimary(p);
    }
    p->at++;
    if (!parseUnary(p)) {
        return 0;
    }
    return c == '+' || emit(p, where, c == '!' ? DBG_COND_NOT : c == '~' ? DBG_COND_CPL
                                                                       : DBG_COND_NEG, 0);
}

static const BinaryOp* binaryAt(const char* s)
{
    int i;

    for (i = 0; binaryOps[i].text != NULL; i++) {
        if (strncmp(s, binaryOps[i].text, strlen(binaryOps[i].text)) == 0) {
            return &binaryOps[i];
        }
    }
    return NULL;
}

/* Precedence climbing: operators of minPrecedence and above, left to right. */
static int parseExpression(Parser* p, int minPrecedence)
{
    if (!parseUnary(p)) {
        return 0;
    }
    for (;;) {
        const BinaryOp* op;
        const char* where;

        skipSpace(p);
        where = p->at;
        op = binaryAt(p->at);
        if (op == NULL || op->precedence < minPrecedence) {
            return 1;
        }
        p->at += strlen(op->text);
        if (!parseExpression(p, op->precedence + 1) || !emit(p, where, op->op, 0)) {
            return 0;
        }
    }
}

int ctrlExprCompile(const char* text, DbgBreakCondition* out, CtrlExprError* error)
{
    Parser p;

    p.text  = text;
    p.at    = text;
    p.out   = out;
    p.depth = 0;
    p.error = error;
    error->pos = 0;
    error->message[0] = 0;
    out->opCount = 0;

    /* The caret under an error counts bytes, so one character is one byte. */
    for (p.at = text; *p.at != 0; p.at++) {
        if ((unsigned char)*p.at >= 0x80) {
            return fail(&p, p.at, "only ASCII characters can be read");
        }
    }
    p.at = text;

    if (!parseExpression(&p, 1)) {
        return 0;
    }
    skipSpace(&p);
    if (*p.at != 0) {
        return fail(&p, p.at, "expected an operator, not '%c'", *p.at);
    }
    return 1;
}
