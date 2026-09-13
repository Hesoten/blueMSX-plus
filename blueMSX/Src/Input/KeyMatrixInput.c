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
#include "KeyMatrixInput.h"
#include "InputEvent.h"
#include "Board.h"
#include "ArchEvent.h"
#include <string.h>

#define MATRIX_ROWS     12
#define QUEUE_SIZE      16384
#define MAX_WAIT_MS     60000
#define IDLE_POLL_MS    10

typedef enum { CMD_DOWN, CMD_UP, CMD_WAIT } CommandType;

typedef struct {
    UInt8  type;
    UInt8  row;
    UInt16 value;       /* the mask, or the wait in milliseconds */
} Command;

/* The same rows MsxPPI reads, most significant bit first. The events are the
** ones the host keyboard sets too, so a key pressed here is indistinguishable
** from one pressed on the keyboard, and key bindings never come into it. */
static const int matrix[MATRIX_ROWS][8] = {
    { EC_7,       EC_6,      EC_5,       EC_4,       EC_3,      EC_2,      EC_1,      EC_0      },
    { EC_SEMICOL, EC_LBRACK, EC_AT,      EC_BKSLASH, EC_CIRCFLX,EC_NEG,    EC_9,      EC_8      },
    { EC_B,       EC_A,      EC_UNDSCRE, EC_DIV,     EC_PERIOD, EC_COMMA,  EC_RBRACK, EC_COLON  },
    { EC_J,       EC_I,      EC_H,       EC_G,       EC_F,      EC_E,      EC_D,      EC_C      },
    { EC_R,       EC_Q,      EC_P,       EC_O,       EC_N,      EC_M,      EC_L,      EC_K      },
    { EC_Z,       EC_Y,      EC_X,       EC_W,       EC_V,      EC_U,      EC_T,      EC_S      },
    { EC_F3,      EC_F2,     EC_F1,      EC_CODE,    EC_CAPS,   EC_GRAPH,  EC_CTRL,   EC_LSHIFT },
    { EC_RETURN,  EC_SELECT, EC_BKSPACE, EC_STOP,    EC_TAB,    EC_ESC,    EC_F5,     EC_F4     },
    { EC_RIGHT,   EC_DOWN,   EC_UP,      EC_LEFT,    EC_DEL,    EC_INS,    EC_CLS,    EC_SPACE  },
    { EC_NUM4,    EC_NUM3,   EC_NUM2,    EC_NUM1,    EC_NUM0,   EC_NUMDIV, EC_NUMADD, EC_NUMMUL },
    { EC_NUMPER,  EC_NUMCOM, EC_NUMSUB,  EC_NUM9,    EC_NUM8,   EC_NUM7,   EC_NUM6,   EC_NUM5   },
    { EC_NONE,    EC_NONE,   EC_NONE,    EC_NONE,    EC_TORIKE, EC_NONE,   EC_JIKKOU, EC_NONE   },
};

/* The queue is filled by whoever receives a request and emptied on the
** emulation thread, so both sides take the lock. */
static void*       lock;
static Command     queue[QUEUE_SIZE];
static int         head;
static int         count;
static int         waiting;
static UInt8       held[MATRIX_ROWS];
static BoardTimer* timer;

static void takeLock(void)
{
    if (lock != NULL) {
        archSemaphoreWait(lock, -1);
    }
}

static void dropLock(void)
{
    if (lock != NULL) {
        archSemaphoreSignal(lock);
    }
}

void keyMatrixInputInit(void)
{
    if (lock == NULL) {
        lock = archSemaphoreCreate(1);
    }
}

static int isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Decimal, or hexadecimal after 0x. Anything else, a sign included, fails. */
static const char* parseNumber(const char* p, UInt32* value)
{
    UInt32 v = 0;
    int digits = 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        for (p += 2; ; p++, digits++) {
            int d;
            if      (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
            else break;
            if (v > 0x0fffffff) return NULL;
            v = v * 16 + d;
        }
    }
    else {
        for (; *p >= '0' && *p <= '9'; p++, digits++) {
            if (v > 429496728) return NULL;
            v = v * 10 + (*p - '0');
        }
    }
    if (digits == 0 || !(*p == 0 || isSpace(*p))) {
        return NULL;
    }
    *value = v;
    return p;
}

static const char* skipSpace(const char* p)
{
    while (isSpace(*p)) p++;
    return p;
}

static int maskHasOnlyKeys(int row, UInt32 mask)
{
    int bit;

    if (mask == 0 || mask > 0xff) {
        return 0;
    }
    for (bit = 0; bit < 8; bit++) {
        if ((mask & (1 << bit)) && matrix[row][7 - bit] == EC_NONE) {
            return 0;
        }
    }
    return 1;
}

/* Fills out[] from text; returns the number of commands, or -1. */
static int parse(const char* text, Command* out, int room)
{
    const char* p = skipSpace(text);
    int n = 0;

    while (*p) {
        char op = *p++;
        UInt32 a, b;

        if (!isSpace(*p) || n == room) {
            return -1;
        }
        p = skipSpace(p);

        switch (op) {
        case 'd':
        case 'u':
            if ((p = parseNumber(p, &a)) == NULL) return -1;
            p = skipSpace(p);
            if ((p = parseNumber(p, &b)) == NULL) return -1;
            if (a >= MATRIX_ROWS || !maskHasOnlyKeys((int)a, b)) return -1;
            out[n].type  = op == 'd' ? CMD_DOWN : CMD_UP;
            out[n].row   = (UInt8)a;
            out[n].value = (UInt16)b;
            break;
        case 'w':
            if ((p = parseNumber(p, &a)) == NULL) return -1;
            if (a > MAX_WAIT_MS) return -1;
            out[n].type  = CMD_WAIT;
            out[n].row   = 0;
            out[n].value = (UInt16)a;
            break;
        default:
            return -1;
        }
        n++;
        p = skipSpace(p);
    }
    return n;
}

int keyMatrixInputSubmit(const char* text)
{
    static Command parsed[QUEUE_SIZE];
    int n;
    int i;
    int ok;

    if (text == NULL || boardGetType() != BOARD_MSX) {
        return 0;
    }

    takeLock();
    n = parse(text, parsed, QUEUE_SIZE - count);
    ok = n > 0;
    for (i = 0; ok && i < n; i++) {
        queue[(head + count) % QUEUE_SIZE] = parsed[i];
        count++;
    }
    dropLock();

    return ok;
}

int keyMatrixInputRemaining(void)
{
    int n;

    takeLock();
    n = count;
    dropLock();

    return n;
}

static void press(int row, int mask, int down)
{
    int bit;

    for (bit = 0; bit < 8; bit++) {
        if (mask & (1 << bit)) {
            int ec = matrix[row][7 - bit];
            if (down) {
                inputEventSet(ec);
            }
            else {
                inputEventUnset(ec);
            }
        }
    }
    held[row] = (UInt8)(down ? (held[row] | mask) : (held[row] & ~mask));
}

/* Carries out commands until a wait or the end of the queue, and sleeps until
** the wait is over; with nothing queued it looks again every few
** milliseconds. */
static void onTimer(void* ref, UInt32 time)
{
    UInt32 next = time + boardFrequency() / 1000 * IDLE_POLL_MS;

    takeLock();

    if (waiting) {
        head = (head + 1) % QUEUE_SIZE;
        count--;
        waiting = 0;
    }

    while (count > 0) {
        Command* c = &queue[head];

        if (c->type == CMD_WAIT) {
            waiting = 1;
            next = time + boardFrequency() / 1000 * c->value;
            break;
        }
        press(c->row, c->value, c->type == CMD_DOWN);
        head = (head + 1) % QUEUE_SIZE;
        count--;
    }

    dropLock();

    boardTimerAdd(timer, next);
}

void keyMatrixInputBoardStart(void)
{
    timer = boardTimerCreate(onTimer, NULL);
    boardTimerAdd(timer, boardSystemTime() + 1);
}

/* Whatever was still queued belonged to the run that ended. Keys pressed here
** are let go, so none stays down into the next run. */
void keyMatrixInputBoardStop(void)
{
    int row;

    if (timer != NULL) {
        boardTimerDestroy(timer);
        timer = NULL;
    }

    takeLock();
    for (row = 0; row < MATRIX_ROWS; row++) {
        if (held[row]) {
            press(row, held[row], 0);
        }
    }
    head = 0;
    count = 0;
    waiting = 0;
    dropLock();
}
