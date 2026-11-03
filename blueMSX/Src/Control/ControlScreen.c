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
#include "ControlScreen.h"
#include "ControlVram.h"
#include "ControlClient.h"
#include "ControlPath.h"
#include "VDP.h"
#include "V9990.h"
#include "FrameBuffer.h"
#include "Emulator.h"
#include "ArchNotifications.h"
#include "ControlInstance.h"
#include "ControlProtocol.h"
#include <process.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Mode 13 is TEXT2. SCREEN 2 is left out: its patterns are rarely an ASCII font. */
static int textColumns(int mode)
{
    switch (mode) {
    case 0:  return 40;
    case 1:  return 32;
    case 13: return 80;
    default: return 0;
    }
}

int ctrlScreenIsTextMode(void)
{
    return textColumns(vdpDebugGetScreenMode()) > 0;
}

int ctrlScreenV9990Shown(int* superimposed)
{
    return v9990GetShownMode(superimposed) != V9990_MODE_NONE;
}

/* Where a text screen's tables are in video memory, as the VDP registers say. */
typedef struct {
    UInt8  registers[64];
    UInt8* vram;
    UInt32 vramSize;
    int    mode;
    int    columns;
    int    rows;
    UInt32 base;
    UInt32 patterns;
    UInt32 colours;
    int    modeBits;   /* the mode bits of R#0 and R#1 */
    int    mixed;      /* they select a mixed or undefined mode */
} TextScreen;

static void copyError(char* err, int errLen, const char* text)
{
    strncpy(err, text, (size_t)errLen - 1);
    err[errLen - 1] = 0;
}

/* Reads the registers and finds the name, pattern and colour tables; 0 with err when
** the screen holds no text to read. */
static int screenTables(TextScreen* screen, char* err, int errLen)
{
    UInt8* registers = screen->registers;
    UInt32 span;

    if (screen->columns == 0) {
        snprintf(err, (size_t)errLen,
                 "%s does not hold characters in a grid, so there is "
                 "no text to read. Ask for \"png\" instead.", ctrlVramModeName(screen->mode));
        return 0;
    }
    if (vdpDebugGetRegisters(registers, 64) < 3) {
        copyError(err, errLen, "there is no VDP");
        return 0;
    }

    vdpForceSync();
    screen->vram = vdpDebugGetVram(&screen->vramSize);
    if (screen->vram == NULL) {
        copyError(err, errLen, "there is no video memory");
        return 0;
    }

    /* TEXT2 holds bits 0-1 of R#2 at 1 and aligns to 4 kB; the VDP ignores the
    ** address bits past its video memory, as VDP.c does with vramMask. */
    screen->base = (UInt32)(registers[2] & (screen->mode == 13 ? 0x7c : 0x7f)) << 10;
    screen->patterns = (UInt32)(registers[4] & 0x3f) << 11;
    screen->colours = ((UInt32)registers[10] << 14) | ((UInt32)registers[3] << 6);
    if ((screen->vramSize & (screen->vramSize - 1)) == 0) {
        screen->base &= screen->vramSize - 1;
        screen->patterns &= screen->vramSize - 1;
        screen->colours &= screen->vramSize - 1;
    }
    /* 212 lines give 26.5 rows. A TMS99x8 only takes R#0-7 (VDP.c), so R#9 bit 7 is never set. */
    screen->rows = 24;
    if (registers[9] & 0x80) {
        screen->rows = 27;
    }
    /* Past cell 1023 TEXT1 wraps to the start of its 4 kB block, as Common.h draws it. */
    span = (UInt32)(screen->columns * screen->rows);
    if (screen->mode == 0 && span > 1024) {
        span = 1024;
    }
    if (screen->base + span > screen->vramSize) {
        snprintf(err, (size_t)errLen,
                 "the name table register points at 0x%05X, past the end of %u kB "
                 "of video memory", (unsigned)screen->base, (unsigned)(screen->vramSize / 1024));
        return 0;
    }
    /* VDP.c reports mixed mode bits as mode 0 or 1 but draws them blank or from other tables. */
    screen->modeBits = ((registers[0] & 0x0e) >> 1) | (registers[1] & 0x18);
    screen->mixed    = screen->modeBits != 0x10 && screen->modeBits != 0x00 &&
                       screen->modeBits != 0x12;
    return 1;
}

/* What the screen is, and whatever keeps its text from showing. */
static void screenTextNotes(CtrlText* out, const TextScreen* screen)
{
    const UInt8* registers = screen->registers;

    ctrlTextAdd(out, "%s, %dx%d%s, name table vram:0x%04X, %d Hz; these are "
                "the character codes the program wrote, not the shapes drawn\n",
                ctrlVramModeName(screen->mode), screen->columns, screen->rows,
                screen->rows == 27 ? " (212 lines: row 27 is half shown)" : "",
                (unsigned)screen->base, vdpGetRefreshRate());
    if (!(registers[1] & 0x40)) {
        ctrlTextAdd(out, "the display is off (R#1 bit 6 is 0), so the screen is blank; "
                    "below is what the name table holds\n");
    }
    if (screen->mixed) {
        ctrlTextAdd(out, "R#0/R#1 select a mixed or undefined mode (0x%02X); the VDP may "
                    "draw nothing or use other pattern tables, so check the PNG\n",
                    screen->modeBits);
    }
    /* Colours are compared as drawn: palette, and colour 0 showing the backdrop.
    ** In TEXT2, blinking cells take their colours from R#12 instead. */
    if (screen->mode != 1 && vdpDebugColorsMatch(registers[7] >> 4, registers[7] & 0x0f)) {
        /* R#12 shows only when R#13 bits 7-4 are set,
        ** all the time when bits 3-0 are also 0 (vdpBlink). */
        int blink = screen->mode == 13 && (registers[13] >> 4) != 0 &&
                    !vdpDebugColorsMatch(registers[12] >> 4, registers[12] & 0x0f);

        ctrlTextAdd(out, "text colour %d and background colour %d are drawn the same "
                    "(R#7=0x%02X), so no text is visible%s\n", registers[7] >> 4,
                    registers[7] & 0x0f, registers[7],
                    blink ? " outside the blink area (R#12, R#13)" : "");
    }
    if (registers[23] != 0) {
        ctrlTextAdd(out, "the screen is scrolled up %d lines (R#23); the rows are in name "
                    "table order, not as shown\n", registers[23]);
    }
}

/* The name table row by row; seen marks the printable codes shown, other counts the rest. */
static void screenTextRows(CtrlText* out, const TextScreen* screen, char* seen, int* other)
{
    int row;

    for (row = 0; row < screen->rows; row++) {
        char line[96];
        int  column;

        for (column = 0; column < screen->columns && column < (int)sizeof(line) - 1; column++) {
            UInt32 cell = (UInt32)(row * screen->columns + column);
            UInt32 at = screen->mode == 0 ? ((screen->base | 0x3ff) & (~0xfffu | (0xc00 + cell)))
                                          : screen->base + cell;
            UInt8  code = screen->vram[at];

            if (code >= 0x20 && code < 0x7f) {
                line[column] = (char)code;
                seen[code] = 1;
            }
            else {
                line[column] = '.';
                other[code]++;
            }
        }
        line[column] = 0;
        ctrlTextAdd(out, "%2d|%s\n", row + 1, line);
    }
}

/* Ends a list that printed at most `most` of its `listed` entries. */
static void endList(CtrlText* out, int listed, int most)
{
    if (listed > most) {
        ctrlTextAdd(out, " and %d more", listed - most);
    }
    if (listed > 0) {
        ctrlTextAdd(out, "\n");
    }
}

static void listUnprintable(CtrlText* out, const int* other)
{
    int listed = 0;
    int i;

    for (i = 0; i < 256; i++) {
        if (other[i] == 0) {
            continue;
        }
        if (listed == 0) {
            ctrlTextAdd(out, "shown as '.':");
        }
        if (listed < 12) {
            ctrlTextAdd(out, " 0x%02X x%d", i, other[i]);
        }
        listed++;
    }
    endList(out, listed, 12);
}

/* TEXT modes draw only bits 7-2 of a pattern line. Every BIOS font draws 0x21-0x7E.
** A mixed mode takes its patterns from elsewhere, so it is not checked. */
static void listClearedPatterns(CtrlText* out, const TextScreen* screen, const char* seen)
{
    int listed = 0;
    int i;

    for (i = 0x21; i < 0x7f && !screen->mixed; i++) {
        UInt8 drawn = 0;
        int   line;

        if (!seen[i]) {
            continue;
        }
        for (line = 0; line < 8; line++) {
            drawn |= screen->vram[screen->patterns + (UInt32)(i * 8 + line)] &
                     (screen->mode == 1 ? 0xff : 0xfc);
        }
        if (drawn != 0) {
            continue;
        }
        if (listed == 0) {
            ctrlTextAdd(out, "pattern cleared, so these show no character shape:");
        }
        if (listed < 16) {
            ctrlTextAdd(out, " '%c' 0x%02X", (char)i, i);
        }
        listed++;
    }
    endList(out, listed, 16);
}

/* GRAPHIC1 takes both colours of each 8 codes from the colour table (Common.h). */
static void listInvisibleColours(CtrlText* out, const TextScreen* screen, const char* seen)
{
    int listed = 0;
    int i;

    for (i = 0x21; i < 0x7f && screen->mode == 1 && !screen->mixed &&
                   screen->colours + 0x10 <= screen->vramSize; i++)
    {
        UInt8 colour = screen->vram[screen->colours + (UInt32)(i / 8)];

        if (!seen[i] || !vdpDebugColorsMatch(colour >> 4, colour & 0x0f)) {
            continue;
        }
        if (listed == 0) {
            ctrlTextAdd(out, "their colour table entry draws foreground and background the "
                        "same, so these show no character shape:");
        }
        if (listed < 16) {
            ctrlTextAdd(out, " '%c' 0x%02X", (char)i, i);
        }
        listed++;
    }
    endList(out, listed, 16);
}

int ctrlScreenText(CtrlView* view, CtrlText* out, char* err, int errLen)
{
    TextScreen screen;
    int        other[256];
    char       seen[256];
    int        superimposed;

    /* The VDP of a running machine is not read, its mode no more than its tables. */
    if (view == NULL || view->held != DBG_ACCESS_VIEW) {
        copyError(err, errLen, "the emulation thread could not be stopped in time");
        return 0;
    }
    memset(&screen, 0, sizeof(screen));
    screen.mode    = vdpDebugGetScreenMode();
    screen.columns = textColumns(screen.mode);
    if (!screenTables(&screen, err, errLen)) {
        return 0;
    }

    memset(other, 0, sizeof(other));
    memset(seen, 0, sizeof(seen));

    if (ctrlScreenV9990Shown(&superimposed)) {
        ctrlTextAdd(out, superimposed
                    ? "a V9990 picture is shown over this VDP text; the text is the VDP's alone\n"
                    : "a V9990 picture is shown instead of this VDP text; ask for \"png\" to "
                      "see the screen\n");
    }
    screenTextNotes(out, &screen);
    screenTextRows(out, &screen, seen, other);
    listUnprintable(out, other);
    listClearedPatterns(out, &screen, seen);
    listInvisibleColours(out, &screen, seen);
    return 1;
}

/* Made before the capture, so a file already there is refused, never replaced. */
static int createNewFile(const char* path, char* err, int errLen)
{
    int handle = _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);

    if (handle >= 0) {
        _close(handle);
        return CTRL_SCREEN_OK;
    }
    if (errno == EEXIST) {
        snprintf(err, (size_t)errLen,
                 "%s already exists, and a screenshot never overwrites a file given as "
                 "path. Give a new file name, or leave out path to write the emulator's "
                 "own file, which the next screenshot overwrites", path);
        return CTRL_SCREEN_FILE_EXISTS;
    }
    snprintf(err, (size_t)errLen, "could not write %s", path);
    return CTRL_SCREEN_FAILED;
}

int ctrlScreenPng(const char* wantedPath, char* pathOut, int pathOutLen,
                  char* err, int errLen)
{
    char path[CTRL_PATH_MAX];
    int  created = 0;
    int  result;

    /* Minimized while Agent Control was Off draws nothing; the frame after it can be half old
    ** lines, and Blend frames mixes that into the next one. */
    if (emulatorGetFramesDrawnSinceMinimized() < 3) {
        int fullAccess = ctrlGateLevel() == CTRL_GATE_FULL;
        int superimposed;
        /* Only a view reads the VDP's mode; a running machine is not read. */
        int textMode   = dbgAccessHeld() == DBG_ACCESS_VIEW && ctrlScreenIsTextMode() &&
                         !ctrlScreenV9990Shown(&superimposed);
        /* Presence, not the picture shown, as this may run under a hold. */
        const char* readable = v9990IsPresent() ? " or memory)."
                                                : ", memory, or the screen as text).";

        snprintf(err, (size_t)errLen,
                 "the emulator window was minimized while Agent Control was Off, so the "
                 "screen was not drawn, and no frame has been drawn since; a PNG would show "
                 "an old picture. %s ask again%s.%s%s",
                 fullAccess ? "Let the machine run at least three frames (bmsx_run how=resume, "
                        "then how=pause) and"
                      : "Ask the person to let the machine run for a moment, then",
                 textMode ? ", or use as=text" : "",
                 fullAccess ? " This lets the program go on past where it stopped, so first read "
                        "what you need at this stop (registers" : "",
                 fullAccess ? readable : "");
        return CTRL_SCREEN_NOT_DRAWING;
    }

    if (wantedPath != NULL && wantedPath[0] != 0) {
        /* Git Bash's /c/... is not a full path, and would land elsewhere. */
        if (!ctrlPathIsFull(wantedPath)) {
            snprintf(err, (size_t)errLen,
                     "%s is not a full Windows path (C:\\... or \\\\server\\...); "
                     "it would be written to the emulator's folder. Give the full path.",
                     wantedPath);
            return CTRL_SCREEN_FAILED;
        }
        strncpy(path, wantedPath, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        result = createNewFile(path, err, errLen);
        if (result != CTRL_SCREEN_OK) {
            return result;
        }
        created = 1;
    }
    else {
        char directory[260];

        if (!ctrlInstanceAgentFolder(directory, sizeof(directory))) {
            snprintf(err, (size_t)errLen, "could not find the local application data folder");
            return CTRL_SCREEN_FAILED;
        }
        snprintf(path, sizeof(path), "%s\\screen-%d.png", directory, (int)_getpid());
    }

    /* The capture reads the view frame, which only the window's drawing moves on; take the
    ** latest finished frame, the one the check above counts, as the /hidden loop does. */
    frameBufferFlipViewFrame(0);
    if (!archScreenCaptureToFile(SC_LARGE, path)) {
        if (created) {
            remove(path);
        }
        snprintf(err, (size_t)errLen, "could not write %s", path);
        return CTRL_SCREEN_FAILED;
    }

    strncpy(pathOut, path, (size_t)pathOutLen - 1);
    pathOut[pathOutLen - 1] = 0;
    return CTRL_SCREEN_OK;
}
