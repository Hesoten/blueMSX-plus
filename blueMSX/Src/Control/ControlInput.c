/*****************************************************************************
**
** Agent keyboard and joystick input.
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
#include "ControlInput.h"
#include "ControlClient.h"
#include "ControlView.h"
#include "ControlMemory.h"
#include "ControlAddress.h"
#include "ControlPeer.h"
#include "KeyMatrixInput.h"
#include "InputEvent.h"
#include "JoystickPort.h"
#include "Led.h"
#include "SlotManager.h"
#include "Emulator.h"
#include "Properties.h"
#include "VDP.h"
#include "ArchThread.h"
#include "ArchTimer.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define EVENTS_MAX 4096

static void fail(char* err, int errLen, const char* text)
{
    if (err != NULL && errLen > 0) {
        strncpy(err, text, (size_t)errLen - 1);
        err[errLen - 1] = 0;
    }
}

/* Non-letter characters on the Japanese layout; EC_ codes are named after its keys. */
typedef struct {
    char  character;
    UInt8 code;
    UInt8 shifted;
} KeyChar;

static const KeyChar japaneseChars[] = {
    { ' ',  EC_SPACE,   0 }, { '\n', EC_RETURN,  0 }, { '\r', EC_RETURN,  0 },
    { '\t', EC_TAB,     0 },

    { '0',  EC_0,       0 }, { '1',  EC_1,       0 }, { '2',  EC_2,       0 },
    { '3',  EC_3,       0 }, { '4',  EC_4,       0 }, { '5',  EC_5,       0 },
    { '6',  EC_6,       0 }, { '7',  EC_7,       0 }, { '8',  EC_8,       0 },
    { '9',  EC_9,       0 },

    { ')',  EC_9,       1 }, { '(',  EC_8,       1 }, { '\'', EC_7,       1 },
    { '&',  EC_6,       1 }, { '%',  EC_5,       1 }, { '$',  EC_4,       1 },
    { '#',  EC_3,       1 }, { '"',  EC_2,       1 }, { '!',  EC_1,       1 },

    { '-',  EC_NEG,     0 }, { '=',  EC_NEG,     1 },
    { '^',  EC_CIRCFLX, 0 }, { '~',  EC_CIRCFLX, 1 },
    { '\\', EC_BKSLASH, 0 }, { '|',  EC_BKSLASH, 1 },
    { '@',  EC_AT,      0 }, { '`',  EC_AT,      1 },
    { '[',  EC_LBRACK,  0 }, { '{',  EC_LBRACK,  1 },
    { ']',  EC_RBRACK,  0 }, { '}',  EC_RBRACK,  1 },
    { ';',  EC_SEMICOL, 0 }, { '+',  EC_SEMICOL, 1 },
    { ':',  EC_COLON,   0 }, { '*',  EC_COLON,   1 },
    { ',',  EC_COMMA,   0 }, { '<',  EC_COMMA,   1 },
    { '.',  EC_PERIOD,  0 }, { '>',  EC_PERIOD,  1 },
    { '/',  EC_DIV,     0 }, { '?',  EC_DIV,     1 },
    { '_',  EC_UNDSCRE, 1 },
    { 0,    0,          0 }
};

/* The international layout; the underscore position is a dead key there. */
static const KeyChar internationalChars[] = {
    { ' ',  EC_SPACE,   0 }, { '\n', EC_RETURN,  0 }, { '\r', EC_RETURN,  0 },
    { '\t', EC_TAB,     0 },

    { '0',  EC_0,       0 }, { '1',  EC_1,       0 }, { '2',  EC_2,       0 },
    { '3',  EC_3,       0 }, { '4',  EC_4,       0 }, { '5',  EC_5,       0 },
    { '6',  EC_6,       0 }, { '7',  EC_7,       0 }, { '8',  EC_8,       0 },
    { '9',  EC_9,       0 },

    { ')',  EC_0,       1 }, { '!',  EC_1,       1 }, { '@',  EC_2,       1 },
    { '#',  EC_3,       1 }, { '$',  EC_4,       1 }, { '%',  EC_5,       1 },
    { '^',  EC_6,       1 }, { '&',  EC_7,       1 }, { '*',  EC_8,       1 },
    { '(',  EC_9,       1 },

    { '-',  EC_NEG,     0 }, { '_',  EC_NEG,     1 },
    { '=',  EC_CIRCFLX, 0 }, { '+',  EC_CIRCFLX, 1 },
    { '\\', EC_BKSLASH, 0 }, { '|',  EC_BKSLASH, 1 },
    { '[',  EC_AT,      0 }, { '{',  EC_AT,      1 },
    { ']',  EC_LBRACK,  0 }, { '}',  EC_LBRACK,  1 },
    { ';',  EC_SEMICOL, 0 }, { ':',  EC_SEMICOL, 1 },
    { '\'', EC_COLON,   0 }, { '"',  EC_COLON,   1 },
    { '`',  EC_RBRACK,  0 }, { '~',  EC_RBRACK,  1 },
    { ',',  EC_COMMA,   0 }, { '<',  EC_COMMA,   1 },
    { '.',  EC_PERIOD,  0 }, { '>',  EC_PERIOD,  1 },
    { '/',  EC_DIV,     0 }, { '?',  EC_DIV,     1 },
    { 0,    0,          0 }
};

/* The BIOS's own decode tables: international lists all 48 keys plain then
** shifted (0x01 is the dead key, 0x00 or 0xFF), Japanese only the symbol keys. */
static const char internationalTable[] =
    "0123456789-=\\[];'`,./" "\x01" "abcdefghijklmnopqrstuvwxyz"
    ")!@#$%^&*(_+|{}:\"~<>?" "\x01" "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char japaneseTable[] = "-^\\@[;:],./" "\xff" "=~|`{+*}<>?_";

/* Spelled out: EC_ letter codes follow the key matrix, so EC_A + n is not the nth letter. */
static const UInt8 letterCode[26] = {
    EC_A, EC_B, EC_C, EC_D, EC_E, EC_F, EC_G, EC_H, EC_I,
    EC_J, EC_K, EC_L, EC_M, EC_N, EC_O, EC_P, EC_Q, EC_R,
    EC_S, EC_T, EC_U, EC_V, EC_W, EC_X, EC_Y, EC_Z
};

static int charToKey(const KeyChar* table, char character, UInt8* code, int* shifted)
{
    int i;

    if (character >= 'a' && character <= 'z') {
        *code = letterCode[character - 'a'];
        *shifted = 0;
        return 1;
    }
    if (character >= 'A' && character <= 'Z') {
        *code = letterCode[character - 'A'];
        *shifted = 1;
        return 1;
    }
    for (i = 0; table[i].character != 0; i++) {
        if (table[i].character == character) {
            *code    = table[i].code;
            *shifted = table[i].shifted;
            return 1;
        }
    }
    return 0;
}

static int biosHasTable(const UInt8* bios, int size, const char* table, int length)
{
    int at;
    int i;

    for (at = 0; at + length <= size; at++) {
        for (i = 0; i < length; i++) {
            UInt8 want = (UInt8)table[i];

            if (want == 0x01 ? bios[at + i] != 0x00 && bios[at + i] != 0xff
                             : bios[at + i] != want) {
                break;
            }
        }
        if (i == length) {
            return 1;
        }
    }
    return 0;
}

/* blueMSX knows no layout, so the BIOS in slot0.0 decides by its decode table;
** the byte at 0x2C alone is wrong for C-BIOS, Brazilian and Russian machines. */
static const KeyChar* machineLayout(int* cbios, char* err, int errLen)
{
    static UInt8 bios[0x4000];
    const UInt8* low;
    const UInt8* high;
    CtrlAddr addr;
    CtrlView view;
    int      read = CTRL_MEM_OK;

    addr.space   = CTRL_SPACE_SLOT;
    addr.slot    = 0;
    addr.subSlot = 0;
    addr.segment = CTRL_SEGMENT_FLAT;
    addr.offset  = 0;
    /* A hold: the BIOS banks do not change while the machine runs. */
    if (ctrlViewTake(&view, DBG_ACCESS_HOLD, CTRL_VIEW_TIMEOUT_MS) != DBG_ACCESS_OK) {
        fail(err, errLen, "the emulator window was changing the machine, so the keyboard "
             "layout could not be read from the BIOS; call again");
        return NULL;
    }
    /* The banks are read where the ROM maps them, so RAM in page 0 does not matter. */
    low  = slotDebugGetBankData(0, 0, 0);
    high = slotDebugGetBankData(0, 0, 1);
    if (low != NULL && high != NULL) {
        memcpy(bios, low, 0x2000);
        memcpy(bios + 0x2000, high, 0x2000);
    }
    else {
        read = ctrlMemPeek(&view, &addr, bios, sizeof(bios));
    }
    ctrlViewRelease(&view);
    /* A hold reads no memory: the bus of a running machine is not read. */
    if (read == CTRL_MEM_NOVIEW) {
        fail(err, errLen, "the BIOS in slot0.0 of this machine has no ROM banks to read while "
             "it runs, and typing needs it running, so the keyboard layout is unknown; press "
             "the keys by name instead, which are positions on the keyboard.");
        return NULL;
    }
    if (read != CTRL_MEM_OK) {
        snprintf(err, (size_t)errLen, "could not read the BIOS in slot0.0 to find the "
                 "keyboard layout: %s", ctrlMemErrorText(read));
        return NULL;
    }

    *cbios = biosHasTable(bios, sizeof(bios), "C-BIOS", 6);
    if (biosHasTable(bios, sizeof(bios), internationalTable,
                     (int)sizeof(internationalTable) - 1)) {
        return internationalChars;
    }
    if ((bios[0x2c] & 0x0f) == 0 &&
        biosHasTable(bios, sizeof(bios), japaneseTable, (int)sizeof(japaneseTable) - 1)) {
        return japaneseChars;
    }
    fail(err, errLen, "this machine's BIOS uses a keyboard layout that typing text "
         "has no table for (only Japanese and international are known); press the "
         "keys by name instead, which are positions on the keyboard.");
    return NULL;
}

/* The key queue drains on board time, which stops while the machine is paused. */
static int requireRunning(char* err, int errLen)
{
    if (emulatorGetState() == EMU_RUNNING) {
        return 1;
    }
    fail(err, errLen,
         "the machine is not running, and keys are only scanned while it is. "
         "Resume the machine first.");
    return 0;
}

static int refreshRate(void)
{
    int rate = vdpGetRefreshRate();

    return rate > 0 ? rate : 60;
}

/* Wall time that frames of emulated time take at the emulation speed setting. */
static int wallMs(int frames)
{
    int hz = emulatorLogFrequencyToHz(propGetGlobalProperties()->emulation.speed);

    return (int)((double)frames * 1000.0 / refreshRate() * 3579545.0 / (hz > 0 ? hz : 3579545));
}

/* Must not hold a view while waiting: the queue drains only while the machine runs.
** CTRL_INPUT_STOPPED once the machine pauses (*dropped when it stopped, which empties
** the queue) or takes no event while running (*idleMs); a suspend is waited out. */
static int waitDrained(int longestWaitFrames, int* remaining, int* idleMs, int* dropped,
                       char* err, int errLen)
{
    int    limit = wallMs(longestWaitFrames) + 2000;
    int    last = keyMatrixInputRemaining();
    UInt32 since = archGetSystemUpTime(1000);

    *idleMs = 0;
    *dropped = 0;
    while ((*remaining = keyMatrixInputRemaining()) > 0) {
        EmuState state = emulatorGetState();
        int      idle;

        if (ctrlClientAllBlocked()) {
            fail(err, errLen, "agent control was turned off at the emulator before "
                 "the keys were all delivered; the rest may still be typed");
            return 0;
        }
        /* The next client is served only once this returns, so typing must not keep it waiting. */
        if (ctrlPeerGone()) {
            keyMatrixInputCancel();
            fail(err, errLen, "the connection was closed before the keys were all "
                 "delivered; the rest were dropped, and keys this call still holds "
                 "are let go when the current hold ends");
            return 0;
        }
        /* Only the trailing wait left means every key went down and up. */
        if (state != EMU_RUNNING && state != EMU_SUSPENDED) {
            *dropped = state == EMU_STOPPED;
            return *remaining == 1 ? 1 : CTRL_INPUT_STOPPED;
        }
        /* Only running time counts; a menu or a dragged window suspends the machine. */
        if (*remaining != last || state == EMU_SUSPENDED) {
            last = *remaining;
            since = archGetSystemUpTime(1000);
        }
        idle = (int)(archGetSystemUpTime(1000) - since);
        if (idle > limit) {
            *idleMs = idle;
            return *remaining == 1 ? 1 : CTRL_INPUT_STOPPED;
        }
        archThreadSleep(2);
    }
    return 1;
}

/* The trailing wait of an earlier request may still run; give it an emulated second. */
static int queueFree(char* err, int errLen)
{
    char   message[200];
    UInt32 start = archGetSystemUpTime(1000);
    int    left;

    while ((left = keyMatrixInputRemaining()) > 0 && emulatorGetState() == EMU_RUNNING &&
           (int)(archGetSystemUpTime(1000) - start) < wallMs(refreshRate()) + 200) {
        archThreadSleep(2);
    }
    if (left == 0) {
        return 1;
    }
    snprintf(message, sizeof(message),
             "%d key events or waits from an earlier request are still queued; let "
             "the machine run until they are done, then send this again.", left);
    fail(err, errLen, message);
    return 0;
}

/* Names up to 4 held keys of one source, the agent's (1) or the emulator's (0). */
static int heldNames(int byAgent, char* names, int namesLen)
{
    int length = 0;
    int count = 0;
    int code;

    names[0] = 0;
    for (code = 1; code < EC_KEYCOUNT; code++) {
        const char* name = inputEventCodeToString(code);

        if (!inputEventIsKeyboard(code) ||
            !(byAgent ? eventInjected[code] : eventMap[code] && !eventInjected[code])) {
            continue;
        }
        if (count < 4) {
            length += snprintf(names + length, (size_t)(namesLen - length), "%s%s",
                               count > 0 ? " " : "",
                               name != NULL && name[0] != 0 ? name : "a key");
        }
        count++;
    }
    if (count > 4) {
        snprintf(names + length, (size_t)(namesLen - length), " and %d more", count - 4);
    }
    return count;
}

/* A key still down (shift, ctrl, graph...) changes what every typed key gives. */
static int noKeyHeld(char* err, int errLen)
{
    char agent[80];
    char emulator[80];
    int  byAgent = heldNames(1, agent, sizeof(agent));
    int  byEmulator = heldNames(0, emulator, sizeof(emulator));

    if (byAgent == 0 && byEmulator == 0) {
        return 1;
    }
    snprintf(err, (size_t)errLen,
             "held keys would change what is typed:%s%s%s%s%s%s",
             byAgent ? " " : "", agent,
             byAgent ? " held through agent control (let go with how=release)" : "",
             byAgent && byEmulator ? ";" : "", byEmulator ? " " : "", emulator);
    if (byEmulator) {
        int used = (int)strlen(err);
        snprintf(err + used, (size_t)(errLen - used),
                 " held at the emulator (keyboard, on-screen key or game pad)");
    }
    return 0;
}

void ctrlInputKeyNames(char* out, int outLen)
{
    int length = 0;
    int code;

    out[0] = 0;
    for (code = 1; code < EC_KEYCOUNT; code++) {
        const char* name = inputEventCodeToString(code);
        int written;

        if (name == NULL || name[0] == 0) {
            continue;
        }
        written = snprintf(out + length, (size_t)(outLen - length), "%s%s",
                           length > 0 ? " " : "", name);
        if (written < 0 || written >= outLen - length) {
            out[length] = 0;
            return;
        }
        length += written;
    }
}

/* Only a joystick device reads a joystick key; anything else in the port ignores it. */
static int joystickConnected(int code, const char* name, char* err, int errLen)
{
    int port;
    JoystickPortType type;
    char message[220];

    if (!inputEventIsJoystick1(code) && !inputEventIsJoystick2(code)) {
        return 1;
    }
    port = inputEventIsJoystick1(code) ? 0 : 1;
    type = joystickPortGetType(port);
    if (type == JOYSTICK_PORT_JOYSTICK || type == JOYSTICK_PORT_COLECOJOYSTICK ||
        type == JOYSTICK_PORT_SUPERACTION) {
        return 1;
    }
    snprintf(message, sizeof(message),
             "joystick port %d has %s%s in it, so %.40s reaches nothing. Ask "
             "the person at the keyboard to connect a joystick to port %d.",
             port + 1, type == JOYSTICK_PORT_NONE ? "" : "the ",
             type == JOYSTICK_PORT_NONE ? "nothing" :
             joystickPortGetDescription(type, 0), name, port + 1);
    fail(err, errLen, message);
    return 0;
}

/* The key codes of names joined with +: 1, else 0 (or -1 for a name that is no key) with
** err filled. */
static int keyCodes(const char* keys, UInt8* codes, int* codeCount, char* err, int errLen)
{
    char  work[128];
    char* token;
    char* next;

    strncpy(work, keys, sizeof(work) - 1);
    work[sizeof(work) - 1] = 0;

    *codeCount = 0;
    for (token = work; token != NULL; token = next) {
        int code;

        next = strchr(token, '+');
        if (next != NULL) {
            *next++ = 0;
        }
        if (*token == 0) {
            fail(err, errLen, "an empty key name; join names with one + between "
                 "them, as in ctrl+stop");
            return 0;
        }
        if (*codeCount == 8) {
            fail(err, errLen, "at most 8 keys can be pressed at once");
            return 0;
        }
        code = inputEventStringToCode(token);
        if (code <= 0) {
            char message[160];
            snprintf(message, sizeof(message), "no key called \"%.40s\".", token);
            fail(err, errLen, message);
            return -1;
        }
        if (!joystickConnected(code, token, err, errLen)) {
            return 0;
        }
        codes[(*codeCount)++] = (UInt8)code;
    }
    return 1;
}

/* Down in the order named, up in reverse, so a leading modifier wraps the key, then a
** trailing wait so the program has had a scan to see the keys. Returns how many events. */
static int keyEvents(KeyMatrixEvent* events, const UInt8* codes, int codeCount, int how,
                     int holdFrames, int gapFrames)
{
    int at = 0;
    int i;

    if (how == CTRL_INPUT_PRESS || how == CTRL_INPUT_TAP) {
        for (i = 0; i < codeCount; i++) {
            events[at].kind = KM_DOWN;
            events[at++].code = codes[i];
        }
    }
    if (how == CTRL_INPUT_TAP) {
        events[at].kind = KM_WAIT_FRAMES;
        events[at++].code = (UInt16)holdFrames;
    }
    if (how == CTRL_INPUT_RELEASE || how == CTRL_INPUT_TAP) {
        for (i = codeCount - 1; i >= 0; i--) {
            events[at].kind = KM_UP;
            events[at++].code = codes[i];
        }
    }
    events[at].kind = KM_WAIT_FRAMES;
    events[at++].code = (UInt16)(gapFrames > 0 ? gapFrames : 1);
    return at;
}

/* Why the keys were not all delivered, from what waitDrained found. */
static void explainKeysStopped(const char* keys, int how, int codeCount, int remaining,
                               int idleMs, int dropped, char* err, int errLen)
{
    char message[240];

    if (dropped) {
        snprintf(message, sizeof(message),
                 "the machine was stopped before %.40s was done; stopping drops "
                 "the queued keys and lets go of held ones, so send it again once "
                 "a machine runs.", keys);
    }
    else if (idleMs > 0) {
        snprintf(message, sizeof(message),
                 "the machine has run but took no key event for %d ms; %d key "
                 "events or waits stay queued and are sent when it does.",
                 idleMs, remaining);
    }
    else if (how == CTRL_INPUT_TAP && remaining <= codeCount + 2) {
        snprintf(message, sizeof(message),
                 "the machine stopped (paused or at a breakpoint) with %.40s held "
                 "down; it is let go when the machine runs again, so do not send "
                 "it again.", keys);
    }
    else {
        snprintf(message, sizeof(message),
                 "the machine stopped (paused or at a breakpoint) before %.40s %s; "
                 "that happens when it runs again, so do not send it again.",
                 keys, how == CTRL_INPUT_RELEASE ? "was let go" : "went down");
    }
    fail(err, errLen, message);
}

int ctrlInputKey(const char* keys, int how, int holdFrames, int gapFrames,
                 char* err, int errLen)
{
    KeyMatrixEvent events[EVENTS_MAX];
    UInt8 codes[8];
    int   codeCount;
    int   at;
    int   result;
    int   remaining;
    int   idleMs;
    int   dropped;

    if (keys == NULL || keys[0] == 0) {
        fail(err, errLen, "no key named");
        return 0;
    }
    if ((result = keyCodes(keys, codes, &codeCount, err, errLen)) <= 0) {
        return result;
    }
    if (!requireRunning(err, errLen) || !queueFree(err, errLen)) {
        return 0;
    }

    at = keyEvents(events, codes, codeCount, how, holdFrames, gapFrames);
    if (!keyMatrixInputSubmitEvents(events, at)) {
        fail(err, errLen, "the key queue is full, or this machine has no keyboard");
        return 0;
    }
    result = waitDrained(holdFrames > gapFrames ? holdFrames : gapFrames,
                         &remaining, &idleMs, &dropped, err, errLen);
    if (result == CTRL_INPUT_STOPPED) {
        explainKeysStopped(keys, how, codeCount, remaining, idleMs, dropped, err, errLen);
    }
    return result;
}

/* The code point of the UTF-8 sequence at text, or -1 when it is not valid UTF-8. */
static long utf8CodePoint(const unsigned char* text)
{
    int  length = text[0] >= 0xf0 ? 4 : text[0] >= 0xe0 ? 3 : 2;
    long point = text[0] & (0x3f >> (length - 1));
    int  i;

    if (text[0] < 0xc2 || text[0] > 0xf4) {
        return -1;
    }
    for (i = 1; i < length; i++) {
        if ((text[i] & 0xc0) != 0x80) {
            return -1;
        }
        point = (point << 6) | (text[i] & 0x3f);
    }
    return point;
}

static int checkTypeable(const char* text, char* err, int errLen)
{
    char message[240];
    int  i;

    for (i = 0; text[i] != 0; i++) {
        unsigned char byte = (unsigned char)text[i];
        long point;

        if (byte >= 0x20 && byte < 0x7f) {
            continue;
        }
        if (byte == '\n' || byte == '\r' || byte == '\t') {
            continue;
        }
        point = byte >= 0x80 ? utf8CodePoint((const unsigned char*)text + i) : 0;
        if (point > 0) {
            snprintf(message, sizeof(message),
                     "U+%04lX (at byte %d of the text) has no key here; only ASCII is "
                     "typed. To put other characters on screen, write them to video "
                     "memory.", point, i);
        }
        else if (byte >= 0x80) {
            snprintf(message, sizeof(message),
                     "byte 0x%02X at %d is not ASCII and not valid UTF-8; only ASCII "
                     "is typed.", byte, i);
        }
        else {
            snprintf(message, sizeof(message),
                     "character 0x%02X (a control character) has no key; only \\n, "
                     "\\r and \\t are typed.", byte);
        }
        fail(err, errLen, message);
        return 0;
    }
    return 1;
}

/* The key events a text becomes. */
typedef struct {
    KeyMatrixEvent events[EVENTS_MAX];
    int count;
    int frames;
    int letters;
} TypedKeys;

static void addEvent(TypedKeys* keys, UInt8 kind, UInt16 code)
{
    keys->events[keys->count].kind   = kind;
    keys->events[keys->count++].code = code;
}

/* One character: shift if needed, the key down, held, up, then at least one frame up, or
** a repeated character reads as one long hold. */
static void addCharacter(TypedKeys* keys, UInt8 code, int shifted, int holdFrames,
                         int gapFrames)
{
    int gap = gapFrames > 0 ? gapFrames : 1;

    if (shifted) {
        addEvent(keys, KM_DOWN, EC_LSHIFT);
    }
    addEvent(keys, KM_DOWN, code);
    addEvent(keys, KM_WAIT_FRAMES, (UInt16)holdFrames);
    addEvent(keys, KM_UP, code);
    if (shifted) {
        addEvent(keys, KM_UP, EC_LSHIFT);
    }
    addEvent(keys, KM_WAIT_FRAMES, (UInt16)gap);
    keys->frames += holdFrames + gap;
}

/* The events for every character of text; 0 with err filled for one that cannot be typed. */
static int typedKeys(TypedKeys* keys, const char* text, const KeyChar* layout, int cbios,
                     int holdFrames, int gapFrames, char* err, int errLen)
{
    int i;

    for (i = 0; text[i] != 0; i++) {
        UInt8 code;
        int   shifted;
        char  message[200];

        if (keys->count + 6 >= EVENTS_MAX) {
            fail(err, errLen, "that is more text than one request may type");
            return 0;
        }
        if (isalpha((unsigned char)text[i])) {
            keys->letters++;
        }
        if (!charToKey(layout, text[i], &code, &shifted)) {
            snprintf(message, sizeof(message),
                     "no key gives '%c' on this machine's keyboard", text[i]);
            fail(err, errLen, message);
            return 0;
        }
        /* Measured on C-BIOS 0.23: it decodes row 1 bits 5-7 and row 2 bits 0-1 as F1-F5. */
        if (cbios && (code == EC_AT || code == EC_LBRACK || code == EC_SEMICOL ||
                      code == EC_COLON || code == EC_RBRACK)) {
            snprintf(message, sizeof(message),
                     "C-BIOS reads the keys for [ ] ; ' and ` (and { } : \" ~) as "
                     "function keys, so '%c' cannot be typed on it; write it to "
                     "video memory instead.", text[i]);
            fail(err, errLen, message);
            return 0;
        }
        addCharacter(keys, code, shifted, holdFrames, gapFrames);
    }
    return 1;
}

/* One request has to finish inside a client's tool call timeout, at this speed. */
static int typingFitsOneCall(int frames, int holdFrames, int gapFrames, char* err, int errLen)
{
    char message[200];

    if (wallMs(frames) <= 25000) {
        return 1;
    }
    snprintf(message, sizeof(message),
             "that is %d s of typing at hold %d and gap %d; one request types "
             "about %d characters at these settings, so split the text.",
             (wallMs(frames) + 999) / 1000, holdFrames, gapFrames,
             (int)(25000.0 / wallMs(holdFrames + (gapFrames > 0 ? gapFrames : 1))));
    fail(err, errLen, message);
    return 0;
}

/* A program may drive these lights itself, so this is only a hint. */
static void tellLockLights(int letters, char* note, int noteLen)
{
    if (letters > 0 && ledGetCapslock()) {
        snprintf(note, (size_t)noteLen,
                 "note: the caps lock light is on, so letters may have come out in "
                 "the other case; read the screen back, and tap \"caps\" to turn it "
                 "off if that is what it shows.\n");
    }
    /* The same light is the hangul lock on Korean and the Arabic lock on Arabic machines. */
    if (ledGetKana()) {
        int used = (int)strlen(note);
        snprintf(note + used, (size_t)(noteLen - used),
                 "note: the kana (or hangul or Arabic) lock light is on, so keys may "
                 "have given other characters; read the screen back, and tap \"code\" "
                 "to turn it off if that is what it shows.\n");
    }
}

/* How far typing got before the machine stopped taking keys, from what waitDrained found. */
static void explainTypeStopped(const char* text, const KeyChar* layout, int delivered,
                               int idleMs, int dropped, char* err, int errLen)
{
    char message[240];
    int  total = (int)strlen(text);
    int  done = 0;
    int  sent = 0;
    int  held = 0;
    int  i;

    /* A character counts once its keys are up again: 4 events, 6 when shifted. */
    for (i = 0; text[i] != 0; i++) {
        UInt8 code;
        int   shifted = 0;

        charToKey(layout, text[i], &code, &shifted);
        if (sent + (shifted ? 6 : 4) - 1 > delivered) {
            held = delivered - sent >= (shifted ? 2 : 1);
            break;
        }
        sent += shifted ? 6 : 4;
        done++;
    }
    if (dropped) {
        snprintf(message, sizeof(message),
                 "the machine was stopped after %d of %d characters; stopping "
                 "drops the queued keys, so the rest were not typed. Send them "
                 "again once a machine runs.", done, total);
    }
    else if (idleMs > 0) {
        snprintf(message, sizeof(message),
                 "the machine has run but took no key event for %d ms; %d of %d "
                 "characters went in%s, and the rest stay queued and are typed "
                 "when it does, so do not resend them.", idleMs, done, total,
                 held ? " and the next one's key is down" : "");
    }
    else {
        snprintf(message, sizeof(message),
                 "the machine stopped (paused or at a breakpoint) after %d of %d "
                 "characters%s; the rest stay queued and are typed when it runs "
                 "again, so do not resend them.", done, total,
                 held ? ", the next one's key down (it may have been read)" : "");
    }
    fail(err, errLen, message);
}

int ctrlInputType(const char* text, int holdFrames, int gapFrames,
                  int* keyEventsOut, int* framesOut, char* note, int noteLen,
                  char* err, int errLen)
{
    TypedKeys keys;
    const KeyChar* layout;
    int cbios = 0;
    int result;
    int remaining;
    int idleMs;
    int dropped;

    note[0] = 0;
    if (text == NULL || text[0] == 0) {
        fail(err, errLen, "nothing to type");
        return 0;
    }
    if (!checkTypeable(text, err, errLen) || !requireRunning(err, errLen) ||
        !queueFree(err, errLen) || !noKeyHeld(err, errLen)) {
        return 0;
    }
    layout = machineLayout(&cbios, err, errLen);
    if (layout == NULL) {
        return 0;
    }
    keys.count = keys.frames = keys.letters = 0;
    if (!typedKeys(&keys, text, layout, cbios, holdFrames, gapFrames, err, errLen) ||
        !typingFitsOneCall(keys.frames, holdFrames, gapFrames, err, errLen)) {
        return 0;
    }
    tellLockLights(keys.letters, note, noteLen);

    if (!keyMatrixInputSubmitEvents(keys.events, keys.count)) {
        fail(err, errLen, "the key queue is full, or this machine has no keyboard");
        return 0;
    }
    if (keyEventsOut != NULL) {
        *keyEventsOut = keys.count;
    }
    if (framesOut != NULL) {
        *framesOut = keys.frames;
    }
    result = waitDrained(holdFrames > gapFrames ? holdFrames : gapFrames,
                         &remaining, &idleMs, &dropped, err, errLen);
    if (result == CTRL_INPUT_STOPPED) {
        explainTypeStopped(text, layout, keys.count - remaining, idleMs, dropped, err, errLen);
    }
    return result;
}
