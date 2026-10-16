/*****************************************************************************
**
** Yamaha V9990 video chip.
** Parts follow or are adapted from openMSX's V9990 implementation.
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
#include "V9990.h"
#include "V9990Render.h"
#include "V9990Cmd.h"
#include "Board.h"
#include "SaveState.h"
#include "FrameBuffer.h"
#include "VideoManager.h"
#include "Emulator.h"
#include "DebugDeviceManager.h"
#include "Language.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V9990_INT       0x1000

#define IRQ_VI          0x01
#define IRQ_HI          0x02
#define IRQ_CE          0x04

#define P_VRAM_DATA     0x00
#define P_PALETTE_DATA  0x01
#define P_COMMAND_DATA  0x02
#define P_REGISTER_DATA 0x03
#define P_REGISTER_SEL  0x04
#define P_STATUS        0x05
#define P_INT_FLAG      0x06
#define P_SYSTEM_CTRL   0x07

#define R_VRAM_WRITE    0
#define R_VRAM_READ     3
#define R_SCREEN_MODE0  6
#define R_SCREEN_MODE1  7
#define R_CONTROL       8
#define R_INT_ENABLE    9
#define R_INT_LINE_LO   10
#define R_INT_LINE_HI   11
#define R_INT_X         12
#define R_PALETTE_CTRL  13
#define R_PALETTE_PTR   14
#define R_BACKDROP      15
#define R_DISPLAY_ADJ   16
#define R_SCROLL_AY_LO  17
#define R_SCROLL_AY_HI  18
#define R_SCROLL_AX_LO  19
#define R_SCROLL_AX_HI  20
#define R_SCROLL_BY_LO  21
#define R_SCROLL_BY_HI  22
#define R_SCROLL_BX_LO  23
#define R_SCROLL_BX_HI  24
#define R_SPRITE_BASE   25
#define R_PRIORITY      27
#define R_CURSOR_PAL    28
#define R_CMD_OPCODE    52
#define R_CMD_BORDER_LO 53
#define R_CMD_BORDER_HI 54

#define REG_R  1
#define REG_W  2
#define REG_RW (REG_R | REG_W)

/* The frame buffer window is the V9938's: 8 + 256 + 8 samples of 8 UC, 240 lines. */
#define V9990_FB_WIDTH     272
#define V9990_FB_LINES     240
#define V9990_FB_WINDOW_UC (8 * V9990_FB_WIDTH)

/* Four-times-wide lines must fit a frame buffer line. */
typedef char V9990FbLineFits[(V9990_FB_WIDTH << 2) <= FB_MAX_LINE_WIDTH ? 1 : -1];

/* B5 and B6 show 640 dots over the whole window, at 10/17 dot per sample. */
#define V9990_HS_DOTS      640
#define V9990_HS_DOT_W     17
#define V9990_HS_SAMPLE_W  10

/* A command timing slot that lasts to the next frame start. */
#define COMMAND_NO_END  0x7fffffff

static const UInt8 regAccess[64] = {
    REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_RW, REG_RW,
    REG_RW, REG_RW, REG_RW, REG_RW, REG_RW, REG_W,  REG_W,  REG_RW,
    REG_RW, REG_RW, REG_RW, REG_RW, REG_RW, REG_RW, REG_RW, REG_RW,
    REG_RW, REG_RW, REG_RW, REG_RW, REG_W,  0,      0,      0,
    REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_W,
    REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_W,
    REG_W,  REG_W,  REG_W,  REG_W,  REG_W,  REG_R,  REG_R,  0,
    0,      0,      0,      0,      0,      0,      0,      0
};

static const UInt8 regWriteMask[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x87, 0xff, 0x83, 0x0f, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xdf, 0x07, 0xff, 0xff, 0xc1, 0x07,
    0x3f, 0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static const UInt8 paletteMask[4] = { 0x9f, 0x1f, 0x1f, 0x00 };

struct V9990 {
    UInt8* vram;
    UInt8  regs[64];
    UInt8  palette[256];
    UInt8  regSelect;
    UInt8  readBuffer;
    UInt8  pending;
    UInt8  mcs;
    UInt8  systemReset;

    /* Display mode, recomputed at the line start after R#6, R#7 or MCS change. */
    V9990Mode mode;
    UInt8  modeR6;
    int    modeMcs;
    int    modePending;
    UInt32 modeApplyTime;

    /* Latched at frame start. */
    UInt32 frameStartTime;
    int    secondField;
    int    latchPal;
    int    latchIl;
    int    latchSm;
    int    latchSm1;
    UInt8  latchAdjust;
    UInt8  latchControl;
    UInt8  latchScrollAYHi;
    UInt8  latchScrollBYHi;
    V9990Mode frameMode;

    /* Derived from the latches. lineZero is the first line start after the
    ** frame start; it is half a line in the second interlace field. */
    UInt32 lineTicks;
    UInt32 lineZero;
    UInt32 frameTicks;
    int    frameLines;
    int    displayStartLine;
    int    displayEndLine;
    int    windowTopLine;
    int    fbLines;

    /* The line that shows image row SCAY: the display start, or the line
    ** of an R#17 write during the display. R#21 does the same for SCBY. */
    int    scrollLineA;
    int    scrollLineB;

    /* Lines from lineZero; lines before renderLine are in the frame buffer. */
    FrameBufferData* frameBufferData;
    int    videoHandle;
    int    showOnDisplay;

    /* A source that shows this chip's picture through a compositor, or -1. */
    int    presenter;
    int    superimpose;
    /* Latched at frame start: ysMask keeps bit 15 (YS) only while the
    ** presenter superimposes with R#8 YSE; blank is the blanking colour. */
    UInt16 ysMask;
    UInt16 blank;
    /* Nonzero if the frame that the last flip completed has keys. */
    int    frameKeyed;
    int    loading;
    int    renderLine;

    V9990Cmd* cmd;

    BoardTimer* timerFrame;
    BoardTimer* timerVi;
    BoardTimer* timerHi;
    BoardTimer* timerCe;
    int    viArmed;
    UInt32 viTime;
    int    hiArmed;
    UInt32 hiTime;

    int    debugHandle;
};

/* The chip takes ports 0x60-0x6F, so there is at most one. */
static V9990* theV9990 = NULL;

UInt32 v9990MapAddress(V9990Mode mode, UInt32 address)
{
    address &= V9990_VRAM_MASK;

    switch (mode) {
    case V9990_MODE_P1:
    case V9990_MODE_STANDBY:
        return address;
    case V9990_MODE_P2:
        if (address < 0x78000) {
            return V9990_MAP_BX(address);
        }
        if (address < 0x7c000) {
            return address - 0x3c000;
        }
        return address;
    default:
        return V9990_MAP_BX(address);
    }
}

static V9990Mode calcMode(V9990* v9990)
{
    UInt8 r6 = v9990->regs[R_SCREEN_MODE0];
    UInt8 r7 = v9990->regs[R_SCREEN_MODE1];
    int dckm = (r6 >> 4) & 3;

    switch (r6 >> 6) {
    case 0: return V9990_MODE_P1;
    case 1: return V9990_MODE_P2;
    case 3: return V9990_MODE_STANDBY;
    }

    if (v9990->mcs) {
        switch (dckm) {
        case 0: return V9990_MODE_B0;
        case 1: return V9990_MODE_B2;
        case 2: return V9990_MODE_B4;
        }
        return V9990_MODE_P1;
    }
    if ((r7 & 0x01) && dckm >= 2) {
        return (r7 & 0x40) ? V9990_MODE_B6 : V9990_MODE_B5;
    }
    switch (dckm) {
    case 0: return V9990_MODE_B1;
    case 1: return V9990_MODE_B3;
    case 2: return V9990_MODE_B7;
    }
    return V9990_MODE_P1;
}

static int isOverscan(V9990Mode mode)
{
    return mode == V9990_MODE_B0 || mode == V9990_MODE_B2 || mode == V9990_MODE_B4;
}

static int isHighScan(V9990Mode mode)
{
    return mode == V9990_MODE_B5 || mode == V9990_MODE_B6;
}

static int isBitmap(V9990Mode mode)
{
    return mode != V9990_MODE_P1 && mode != V9990_MODE_P2 && mode != V9990_MODE_STANDBY;
}

/* Dot width in UC of a line drawn in mode. B5 and B6 lines in a frame
** that is not high-scan are drawn as B7. */
static int dotUc(V9990Mode mode)
{
    switch (mode) {
    case V9990_MODE_B0: return 12;
    case V9990_MODE_P1:
    case V9990_MODE_B1:
    case V9990_MODE_STANDBY: return 8;
    case V9990_MODE_B2: return 6;
    case V9990_MODE_P2:
    case V9990_MODE_B3: return 4;
    case V9990_MODE_B4: return 3;
    default: return 2;
    }
}

/* A set-adjust nibble of 0 is the nominal position; 8-15 move right or down
** by 8-1 units and 1-7 move left or up by 1-7 units. */
static int adjustOffset(int nibble)
{
    return (nibble & 8) ? 16 - nibble : -nibble;
}

static void updateIrq(V9990* v9990)
{
    if (v9990->pending & v9990->regs[R_INT_ENABLE] & 0x07) {
        boardSetInt(V9990_INT);
    }
    else {
        boardClearInt(V9990_INT);
    }
}

static void raiseIrq(V9990* v9990, UInt8 flag)
{
    v9990->pending |= flag;
    updateIrq(v9990);
}

/* B5 and B6 run at their own line counts (about 57.6 and 51.1 Hz). */
static int frameRate(V9990* v9990)
{
    if (isHighScan(v9990->frameMode)) {
        return (boardFrequency() + v9990->frameTicks / 2) / v9990->frameTicks;
    }
    return v9990->latchPal ? 50 : 60;
}

static void reportRates(V9990* v9990, int handle)
{
    if (handle >= 0) {
        videoManagerSetRefreshRate(handle, frameRate(v9990));
        videoManagerSetAspectRate(handle, isHighScan(v9990->frameMode) ? 60 : 0);
    }
}

static void calcFrameGeometry(V9990* v9990)
{
    V9990Mode mode = v9990->frameMode;
    int il = v9990->latchIl;
    int topErase;
    int topBorder;
    int displayLines;
    int lines;
    int windowTop;

    if (mode == V9990_MODE_B5) {
        v9990->lineTicks = 848;
        topErase = 25; topBorder = 0; displayLines = 400; lines = 440;
        windowTop = topErase;
        il = 0;
    }
    else if (mode == V9990_MODE_B6) {
        v9990->lineTicks = 800;
        topErase = 33; topBorder = 0; displayLines = 480; lines = 525;
        windowTop = topErase;
        il = 0;
    }
    else {
        int overscan = isOverscan(mode);
        v9990->lineTicks = v9990->latchSm ? 1365 : 1368;
        /* The window does not follow the top erase of the PAL interlace and
        ** SM1 timings, so the picture moves on screen as it does on a CRT. */
        windowTop = 15;
        if (v9990->latchPal) {
            topErase     = il ? 14 : 15;
            topBorder    = overscan ? 0 : 41;
            displayLines = overscan ? 290 : 212;
            lines        = il ? 312 : 313;
        }
        else {
            int sm1 = !il && v9990->latchSm1;
            topErase     = sm1 ? 16 : 15;
            topBorder    = overscan ? 0 : 14;
            displayLines = overscan ? 240 : 212;
            lines        = sm1 ? 263 : 262;
        }
    }

    /* An interlace field is half a line longer. The first field ends mid-line,
    ** so the second one starts with the other half of that line. */
    if (il) {
        UInt32 lateHalf = (v9990->lineTicks + 1) / 2;
        v9990->lineZero   = v9990->secondField ? lateHalf : 0;
        v9990->frameTicks = lines * v9990->lineTicks +
                            (v9990->secondField ? lateHalf : v9990->lineTicks / 2);
    }
    else {
        v9990->lineZero   = 0;
        v9990->frameTicks = lines * v9990->lineTicks;
    }

    v9990->frameLines       = lines;
    v9990->displayStartLine = topErase + topBorder + adjustOffset(v9990->latchAdjust >> 4);
    v9990->displayEndLine   = v9990->displayStartLine + displayLines;

    /* The 240 lines are centred on the nominal display, as in the V9938:
    ** 14 border lines above a 212-line display. B5 and B6 fill the window
    ** with their 400 or 480 lines, as a monitor does, at a 4:3 aspect. */
    if (isHighScan(mode)) {
        v9990->windowTopLine = windowTop;
        v9990->fbLines       = displayLines;
    }
    else {
        v9990->windowTopLine = windowTop + topBorder + (displayLines - V9990_FB_LINES) / 2;
        v9990->fbLines       = V9990_FB_LINES;
    }

    reportRates(v9990, v9990->videoHandle);
    reportRates(v9990, v9990->presenter);
}

/* Horizontal display area in UC (42.95 MHz) from the line start, using the
** display mode in effect now. window is where frame buffer sample 0 starts:
** the 2176 UC window is centred on the nominal display (B5/B6: at its start). */
static void calcHorizontalArea(V9990* v9990, int* left, int* right, int* window)
{
    V9990Mode mode = v9990->mode;
    int blank;
    int border;
    int display;
    int unit;

    if (isHighScan(v9990->frameMode)) {
        blank = 288; border = 0; display = 1280; unit = 8;
    }
    else if (isOverscan(mode)) {
        blank = v9990->latchSm ? 366 : 372; border = 0; display = 2304; unit = 12;
    }
    else {
        blank = v9990->latchSm ? 394 : 400; border = 112; display = 2048;
        unit = v9990->modeMcs ? 12 : 8;
    }

    *window = blank + border + (display - V9990_FB_WINDOW_UC) / 2;
    if (isHighScan(v9990->frameMode)) {
        *window = blank;
    }
    *left   = blank + border + adjustOffset(v9990->latchAdjust & 0x0f) * unit;
    *right  = *left + display;
}

static Int32 floorDiv(Int32 n, Int32 d)
{
    return n >= 0 ? n / d : -((-n + d - 1) / d);
}

static void syncMode(V9990* v9990, UInt32 time)
{
    if (v9990->modePending && (Int32)(time - v9990->modeApplyTime) >= 0) {
        v9990->modePending = 0;
        v9990->mode    = calcMode(v9990);
        v9990->modeR6  = v9990->regs[R_SCREEN_MODE0];
        v9990->modeMcs = v9990->mcs;
    }
}

static void scheduleModeChange(V9990* v9990, UInt32 time)
{
    Int32 t;
    Int32 line;

    if (v9990->modePending) {
        return;
    }
    t    = (Int32)(time - v9990->frameStartTime) - (Int32)v9990->lineZero;
    line = floorDiv(t, (Int32)v9990->lineTicks) + 1;

    v9990->modePending   = 1;
    v9990->modeApplyTime = v9990->frameStartTime + v9990->lineZero + line * v9990->lineTicks;
}

static UInt16 paletteColor(V9990* v9990, int index)
{
    return v9990PaletteColor(v9990->palette, index);
}

static void fillLine(UInt16* line, int count, UInt16 color)
{
    int x;

    for (x = 0; x < count; x++) {
        line[x] = color;
    }
}

static void maskLine(UInt16* line, int count, UInt16 mask)
{
    int x;

    if (mask == 0xffff) {
        return;
    }
    for (x = 0; x < count; x++) {
        line[x] &= mask;
    }
}

/* Finds the samples [first, end) of a line whose centres fall in the display,
** where a sample is one dot, and the display dot of sample first. */
static int displaySpan(int count, int ucPerSample, int window, int left, int right,
                       int* first, int* end, int* dot)
{
    *first = floorDiv(left - window - ucPerSample / 2 + ucPerSample - 1, ucPerSample);
    *end   = floorDiv(right - window - ucPerSample / 2 + ucPerSample - 1, ucPerSample);
    if (*first < 0) {
        *first = 0;
    }
    if (*end > count) {
        *end = count;
    }
    *dot = floorDiv(window + *first * ucPerSample + ucPerSample / 2 - left, ucPerSample);
    return *first < *end;
}

/* Image rows since the line that shows the scroll origin. A frame redrawn
** after a state load can start above a restart line. */
static UInt32 scrollRow(V9990* v9990, int scanLine, int restartLine)
{
    return scanLine >= restartLine ? scanLine - restartLine : scanLine - v9990->displayStartLine;
}

/* Layer A rolls at the image height unless R#18 asks for 256 or 512 lines. */
static UInt32 rollMask(V9990* v9990, UInt32 noRoll)
{
    switch (v9990->latchScrollAYHi >> 6) {
    case 0:  return noRoll;
    case 2:  return 0x01ff;
    default: return 0x00ff;
    }
}

/* Mixes two frame buffer colours by weights wa and wb (sum total), each
** channel rounded; the key bit stays only where both colours have it. */
static UInt16 blendColor(UInt16 a, int wa, UInt16 b, int wb, int total)
{
    int r = (((a >> 10) & 0x1f) * wa + ((b >> 10) & 0x1f) * wb + total / 2) / total;
    int g = (((a >> 5) & 0x1f) * wa + ((b >> 5) & 0x1f) * wb + total / 2) / total;
    int c = ((a & 0x1f) * wa + (b & 0x1f) * wb + total / 2) / total;

    return (UInt16)((a & b & 0x8000) | (r << 10) | (g << 5) | c);
}

/* Covers samples [first, end) with dots: sample x starts at origin + x * sampleW
** and dot d at d * dotW (sampleW <= dotW). A sample over two dots gets their
** area-weighted mix; dots outside [dot0, dot0 + n) keep the sample's colour. */
static void expandDots(UInt16* line, int first, int end, int origin, int sampleW,
                       int dotW, const UInt16* dots, int dot0, int n)
{
    int x;

    for (x = first; x < end; x++) {
        int u = origin + x * sampleW;
        int d = floorDiv(u, dotW);
        int f = u - d * dotW;
        UInt16 a;
        UInt16 b;

        d -= dot0;
        a = d >= 0 && d < n ? dots[d] : line[x];
        if (f + sampleW <= dotW) {
            line[x] = a;
            continue;
        }
        b = d + 1 >= 0 && d + 1 < n ? dots[d + 1] : line[x];
        line[x] = blendColor(a, dotW - f, b, f + sampleW - dotW, sampleW);
    }
}

/* Draws the two 32x32 cursors (0 on top) over display dots [dot0, dot0 + n), nonzero if
** one has dots here. X counts display dots and Y display lines (in non-overscan lines),
** both rolling at 1024x512; Y + 1 is shown, Y + 2 in interlace. */
static int drawCursors(V9990* v9990, UInt16* dots, int dot0, int n, int scanLine)
{
    UInt32 pattern[2];
    UInt16 color[2];
    UInt8  flags[2];
    int    x[2];
    int    line = scanLine - v9990->displayStartLine;
    int    shown = 0;
    int    bd16Black;
    int    c;
    int    i;

    if (v9990->regs[R_CONTROL] & 0x40) {
        return 0;
    }
    if (v9990->latchIl && (v9990->regs[R_SCREEN_MODE1] & 0x04)) {
        line = 2 * line + (v9990->secondField ? 1 : 0);
    }
    if (isOverscan(v9990->frameMode)) {
        line -= v9990->latchPal ? 41 : 14;
    }

    for (c = 0; c < 2; c++) {
        UInt32 attr = 0x7fe00 + 8 * c;
        int y = v9990->vram[V9990_MAP_BX(attr)] | ((v9990->vram[V9990_MAP_BX(attr + 2)] & 1) << 8);
        int row = (line - y - (v9990->latchIl ? 2 : 1)) & 511;

        flags[c]   = v9990->vram[V9990_MAP_BX(attr + 6)];
        x[c]       = v9990->vram[V9990_MAP_BX(attr + 4)] | ((flags[c] & 3) << 8);
        pattern[c] = 0;
        if (row >= 32 || (flags[c] & 0x10) || !(flags[c] & 0xe0)) {
            continue;
        }
        for (i = 0; i < 4; i++) {
            pattern[c] = (pattern[c] << 8) |
                         v9990->vram[V9990_MAP_BX(0x7ff00 + 0x80 * c + 4 * row + i)];
        }
        /* EOR complements the image, or the palette colour when CC != 0. */
        color[c] = paletteColor(v9990, ((v9990->regs[R_CURSOR_PAL] & 0x0f) << 2) | (flags[c] >> 6));
        if (flags[c] & 0x20) {
            color[c] ^= 0x7fff;
        }
        shown |= pattern[c] != 0;
    }
    if (!shown) {
        return 0;
    }

    /* A line with cursor dots is not keyed; a superimposed 16-bit YS dot
    ** shows black there. */
    bd16Black = v9990->ysMask == 0xffff && (v9990->modeR6 & 3) == 3;
    for (i = 0; i < n; i++) {
        dots[i] = bd16Black && (dots[i] & 0x8000) ? 0 : dots[i] & 0x7fff;
    }
    color[0] &= 0x7fff;
    color[1] &= 0x7fff;

    for (i = 0; i < n; i++) {
        for (c = 0; c < 2; c++) {
            int bit = (dot0 + i - x[c]) & 1023;

            if (bit < 32 && (pattern[c] & (0x80000000u >> bit))) {
                dots[i] = (flags[c] & 0xc0) ? color[c] : dots[i] ^ 0x7fff;
                break;
            }
        }
    }
    return 1;
}

/* Draws the bitmap image over the samples of a line that the display
** [0, displayW) covers, in the units of expandDots. */
static void drawBitmap(V9990* v9990, UInt16* line, int count, int origin, int sampleW,
                       int dotW, int displayW, int scanLine)
{
    static UInt16 dots[FB_MAX_LINE_WIDTH + 2];
    V9990Mode mode = v9990->mode;
    UInt32 scrollX = v9990->regs[R_SCROLL_AX_LO] | (v9990->regs[R_SCROLL_AX_HI] << 3);
    UInt32 scrollY = v9990->regs[R_SCROLL_AY_LO] | (v9990->latchScrollAYHi << 8);
    UInt32 mask = rollMask(v9990, 0x1fff);
    UInt32 row;
    int first = floorDiv(-origin, sampleW);
    int end   = -floorDiv(origin - displayW, sampleW);
    int dot0;
    int dot1;
    int hiRes;

    if (first < 0) {
        first = 0;
    }
    if (end > count) {
        end = count;
    }
    if (first >= end) {
        return;
    }
    dot0 = floorDiv(origin + first * sampleW, dotW);
    dot1 = floorDiv(origin + end * sampleW - 1, dotW) + 1;
    if (dot0 < 0) {
        dot0 = 0;
    }
    if (dot1 > displayW / dotW) {
        dot1 = displayW / dotW;
    }
    if (dot1 - dot0 > FB_MAX_LINE_WIDTH + 2) {
        dot1 = dot0 + FB_MAX_LINE_WIDTH + 2;
    }
    if (dot0 >= dot1) {
        return;
    }

    /* B3 at 16 bpp scrolls in 2-dot units. */
    if (mode == V9990_MODE_B3 && (v9990->modeR6 & 3) == 3) {
        scrollX &= ~1;
    }

    row = scrollRow(v9990, scanLine, v9990->scrollLineA);
    if (v9990->latchIl && (v9990->regs[R_SCREEN_MODE1] & 0x04)) {
        row = 2 * row + (v9990->secondField ? 1 : 0);
    }

    hiRes = mode == V9990_MODE_B4 || mode == V9990_MODE_B5 ||
            mode == V9990_MODE_B6 || mode == V9990_MODE_B7;
    v9990RenderBitmapLine(dots, dot1 - dot0, v9990->vram, v9990->palette,
                          v9990->modeR6, v9990->regs[R_PALETTE_CTRL], dot0 + scrollX,
                          (scrollY & ~mask & 0x1fff) + ((row + scrollY) & mask), hiRes);
    if (!drawCursors(v9990, dots, dot0, dot1 - dot0, scanLine)) {
        maskLine(dots, dot1 - dot0, v9990->ysMask);
    }
    expandDots(line, first, end, origin, sampleW, dotW, dots, dot0, dot1 - dot0);
}

/* Draws a P1 or P2 line over its display samples. Layer B always rolls at 512 lines. */
static void drawPattern(V9990* v9990, UInt16* line, int count, int ucPerSample,
                        int window, int left, int right, int scanLine)
{
    V9990PatternLine p;
    UInt32 scrollAY = v9990->regs[R_SCROLL_AY_LO] | (v9990->latchScrollAYHi << 8);
    UInt32 scrollBY = v9990->regs[R_SCROLL_BY_LO] | ((v9990->latchScrollBYHi & 1) << 8);
    UInt32 mask = rollMask(v9990, 0x1ff);
    int first;
    int end;
    int dot;

    if (!displaySpan(count, ucPerSample, window, left, right, &first, &end, &dot)) {
        return;
    }

    p.p2       = v9990->mode == V9990_MODE_P2;
    p.displayY = scanLine - v9990->displayStartLine;
    p.ax = (v9990->regs[R_SCROLL_AX_LO] | (v9990->regs[R_SCROLL_AX_HI] << 3)) & (p.p2 ? 0x3ff : 0x1ff);
    p.ay = (scrollAY & ~mask & 0x1ff) + ((scrollRow(v9990, scanLine, v9990->scrollLineA) + scrollAY) & mask);
    p.bx = (v9990->regs[R_SCROLL_BX_LO] | (v9990->regs[R_SCROLL_BX_HI] << 3)) & 0x1ff;
    p.by = (scrollRow(v9990, scanLine, v9990->scrollLineB) + scrollBY) & 0x1ff;
    p.r8  = v9990->regs[R_CONTROL];
    p.r13 = v9990->regs[R_PALETTE_CTRL];
    p.r15 = v9990->regs[R_BACKDROP];
    p.r25 = v9990->regs[R_SPRITE_BASE];
    p.r27 = v9990->regs[R_PRIORITY];

    v9990RenderPatternLine(line + first, end - first, v9990->vram, v9990->palette, &p, dot);
    maskLine(line + first, end - first, v9990->ysMask);
}

/* Draws a B5/B6 scan line: the 640 dots fill the 1088 samples, moved by
** the set-adjust in 4-dot units, with blanking where they do not reach. */
static void drawHighScanLine(V9990* v9990, UInt16* line, int count, int scanLine)
{
    int shift = adjustOffset(v9990->latchAdjust & 0x0f) * 4;
    UInt16 backdrop = paletteColor(v9990, v9990->regs[R_BACKDROP]) & v9990->ysMask;

    if (!(v9990->latchControl & 0x80)) {
        fillLine(line, count, backdrop);
        return;
    }
    fillLine(line, count, v9990->blank);
    if (scanLine < v9990->displayStartLine || scanLine >= v9990->displayEndLine) {
        return;
    }
    if (!isBitmap(v9990->mode)) {
        fillLine(line, count, backdrop);
        return;
    }
    drawBitmap(v9990, line, count, -shift * V9990_HS_DOT_W, V9990_HS_SAMPLE_W,
               V9990_HS_DOT_W, V9990_HS_DOTS * V9990_HS_DOT_W, scanLine);
}

/* Draws scan line scanLine (counted from lineZero) into frame buffer line y.
** Lines hold 272 << shift samples of 8 >> shift UC: B0 and B2 dots are 3
** samples wide, B4 dots 1.5. */
static void drawLine(V9990* v9990, FrameBuffer* frame, int y, int scanLine)
{
    V9990Mode mode = v9990->mode;
    int dotW = dotUc(mode);
    int shift;
    int count;
    int ucPerSample;
    UInt16* line = frameBufferGetLine(frame, y);
    UInt16 backdrop;
    UInt16 border;
    int left;
    int right;
    int window;
    int x;

    if (isHighScan(v9990->frameMode)) {
        shift = 2;
    }
    else if (dotW == 8) {
        shift = 0;
    }
    else if (dotW == 4 || dotW == 12) {
        shift = 1;
    }
    else {
        shift = 2;
    }
    count = V9990_FB_WIDTH << shift;
    ucPerSample = 8 >> shift;

    frameBufferSetDoubleWidth(frame, y, shift);

    if (mode == V9990_MODE_STANDBY) {
        fillLine(line, count, 0);
        return;
    }
    if (isHighScan(v9990->frameMode)) {
        drawHighScanLine(v9990, line, count, scanLine);
        return;
    }

    backdrop = paletteColor(v9990, v9990->regs[R_BACKDROP]) & v9990->ysMask;
    if (!(v9990->latchControl & 0x80)) {
        fillLine(line, count, backdrop);
        return;
    }

    /* Outside the display, overscan modes show the blanking. */
    border = isOverscan(mode) ? v9990->blank : backdrop;
    if (scanLine < v9990->displayStartLine || scanLine >= v9990->displayEndLine) {
        fillLine(line, count, border);
        return;
    }

    /* Display modes that are not drawn show the backdrop. */
    calcHorizontalArea(v9990, &left, &right, &window);
    for (x = 0; x < count; x++) {
        int center = window + x * ucPerSample + ucPerSample / 2;
        line[x] = center >= left && center < right ? backdrop : border;
    }

    if (isBitmap(mode)) {
        drawBitmap(v9990, line, count, window - left, ucPerSample, dotW, right - left, scanLine);
    }
    else {
        drawPattern(v9990, line, count, ucPerSample, window, left, right, scanLine);
    }
}

/* A line is committed where its first frame buffer sample starts. */
static UInt32 commitTicks(V9990* v9990)
{
    int left;
    int right;
    int window;

    calcHorizontalArea(v9990, &left, &right, &window);
    return window > 0 ? (UInt32)window / 2 : 0;
}

static void renderLines(V9990* v9990, int endLine)
{
    FrameBuffer* frame;

    if (endLine > v9990->frameLines) {
        endLine = v9990->frameLines;
    }
    if (v9990->renderLine >= endLine) {
        return;
    }

    frame = frameBufferGetDrawFrameOf(v9990->frameBufferData);
    for (; v9990->renderLine < endLine; v9990->renderLine++) {
        int scanLine = v9990->renderLine;
        int y = scanLine - v9990->windowTopLine;

        syncMode(v9990, v9990->frameStartTime + v9990->lineZero + scanLine * v9990->lineTicks);
        if (frame != NULL && y >= 0 && y < v9990->fbLines) {
            drawLine(v9990, frame, y, scanLine);
        }
    }
}

/* Number of lines of this frame whose commit point is at or before time. */
static int committedLines(V9990* v9990, UInt32 time)
{
    Int32 t = (Int32)(time - v9990->frameStartTime - v9990->lineZero - commitTicks(v9990));

    if (t < 0 || v9990->lineTicks == 0) {
        return 0;
    }
    return t / (Int32)v9990->lineTicks + 1;
}

/* Command timing slot at t ticks from the frame start, and where it ends:
** the display lines are timed by R#8 SPD, the rest of the frame as blanking. */
static int commandSlot(V9990* v9990, Int32 t, Int32* end)
{
    Int32 start = (Int32)(v9990->lineZero + v9990->displayStartLine * v9990->lineTicks);
    Int32 stop  = (Int32)(v9990->lineZero + v9990->displayEndLine * v9990->lineTicks);

    *end = COMMAND_NO_END;
    if (!(v9990->latchControl & 0x80) || t >= stop) {
        return V9990_CMD_SLOT_BLANK;
    }
    if (t < start) {
        *end = start;
        return V9990_CMD_SLOT_BLANK;
    }
    *end = stop;
    return (v9990->regs[R_CONTROL] & 0x40) ? V9990_CMD_SLOT_DISPLAY : V9990_CMD_SLOT_SPRITES;
}

/* Runs the command engine to time, changing the slot where the display
** lines start and end. */
static void runCommand(V9990* v9990, UInt32 time)
{
    V9990Cmd* cmd = v9990->cmd;
    Int32 limit = (Int32)(time - v9990->frameStartTime);
    Int32 end;
    Int32 t;

    t = (Int32)(v9990CmdTime(cmd, NULL) - v9990->frameStartTime);
    while (v9990CmdIsRunning(cmd)) {
        v9990CmdSetTiming(cmd, commandSlot(v9990, t, &end));
        if (end >= limit) {
            break;
        }
        v9990CmdSync(cmd, v9990->frameStartTime + end);
        t = end;
    }
    v9990CmdSync(cmd, time);
}

/* Brings the command engine, the picture and the display mode up to time.
** While a command runs, each line is drawn after the engine reaches it. */
static void sync(V9990* v9990, UInt32 time)
{
    if (v9990CmdIsActive(v9990->cmd)) {
        int end = committedLines(v9990, time);

        if (end > v9990->frameLines) {
            end = v9990->frameLines;
        }
        while (v9990->renderLine < end && v9990CmdIsRunning(v9990->cmd)) {
            UInt32 lineStart = v9990->frameStartTime + v9990->lineZero +
                               v9990->renderLine * v9990->lineTicks;

            syncMode(v9990, lineStart);
            runCommand(v9990, lineStart + commitTicks(v9990));
            renderLines(v9990, v9990->renderLine + 1);
        }
        runCommand(v9990, time);
    }
    renderLines(v9990, committedLines(v9990, time));
    syncMode(v9990, time);
}

/* Arms the CE timer at the end of the running command, as far as the slots
** of this frame tell. A frame start, R#8 or a late end arms it again. */
static void scheduleCommandEnd(V9990* v9990, UInt32 now)
{
    V9990Cmd* cmd = v9990->cmd;
    UInt32 costs[3];
    UInt32 units;
    UInt32 pos;
    UInt32 avail;
    UInt32 n;
    UInt32 target;
    Int32 end;
    Int32 t;
    int half;

    boardTimerRemove(v9990->timerCe);
    units = v9990CmdUnitsLeft(cmd, costs);
    if (units == 0) {
        return;
    }

    t   = (Int32)(v9990CmdTime(cmd, &half) - v9990->frameStartTime);
    pos = 2 * (UInt32)t + half;
    for (;;) {
        UInt32 cost = costs[commandSlot(v9990, t, &end)];

        if (cost == 0) {
            return;
        }
        avail = end == COMMAND_NO_END ? 0xffffffff : 2 * (UInt32)end - pos;
        n = avail / cost;
        if (units <= n) {
            pos += units * cost;
            break;
        }
        pos   += n * cost;
        units -= n;
        t      = end;
    }

    /* The board drops a timeout before its current time, which can be later
    ** than now; one at that time still fires in this dispatch. */
    target = v9990->frameStartTime + (pos + 1) / 2;
    if ((Int32)(target - now) <= 0) {
        target = now + 1;
    }
    if ((Int32)(target - boardSystemTime()) < 0) {
        target = boardSystemTime();
    }
    boardTimerAdd(v9990->timerCe, target);
}

static void scheduleHi(V9990* v9990, UInt32 time)
{
    Int32 t = (Int32)(time - v9990->frameStartTime);
    Int32 lineTicks = (Int32)v9990->lineTicks;
    Int32 xTicks;
    Int32 target;
    int left;
    int right;
    int window;

    boardTimerRemove(v9990->timerHi);
    v9990->hiArmed = 0;

    if (v9990->pending & IRQ_HI) {
        return;
    }

    calcHorizontalArea(v9990, &left, &right, &window);
    xTicks = (left + (v9990->regs[R_INT_X] & 0x0f) * 64 * (v9990->modeMcs ? 3 : 2)) / 2;

    if (v9990->regs[R_INT_LINE_HI] & 0x80) {
        Int32 line = floorDiv(t - (Int32)v9990->lineZero - xTicks, lineTicks) + 1;
        target = (Int32)v9990->lineZero + line * lineTicks + xTicks;
    }
    else {
        Int32 line = v9990->regs[R_INT_LINE_LO] + 256 * (v9990->regs[R_INT_LINE_HI] & 0x03);
        target = (Int32)v9990->lineZero + (v9990->displayStartLine + line) * lineTicks + xTicks;
        if (target <= t) {
            return;
        }
    }

    /* A position beyond this frame is picked up again at the next frame start. */
    if (target >= (Int32)v9990->frameTicks) {
        return;
    }

    v9990->hiArmed = 1;
    v9990->hiTime  = v9990->frameStartTime + target;
    boardTimerAdd(v9990->timerHi, v9990->hiTime);
}

/* Sets up the frame buffer frame that this frame is drawn into, from line 0. */
static void startPicture(V9990* v9990, FrameBuffer* frame)
{
    int bitmap = isBitmap(v9990->frameMode);

    v9990->renderLine = 0;
    if (frame == NULL) {
        return;
    }

    frameBufferSetLineCount(frame, v9990->fbLines);
    /* Neither an interlaced raster nor a B5/B6 frame leaves gaps between
    ** lines, so the display is told not to draw scanlines over them. */
    frame->interlaceRaster = v9990->latchIl || isHighScan(v9990->frameMode);
    /* EO selects the second field's lines only with IL, and only in Bx. */
    if (v9990->latchIl && bitmap && (v9990->regs[R_SCREEN_MODE1] & 0x04)) {
        frameBufferSetInterlace(frame, v9990->secondField ? INTERLACE_ODD : INTERLACE_EVEN);
    }
    else {
        frameBufferSetInterlace(frame, INTERLACE_NONE);
    }
}

/* While a superimposing presenter is shown, the blanking is transparent,
** and with R#8 YSE the YS colours are too. */
static void latchSuperimpose(V9990* v9990)
{
    int on = v9990->superimpose && v9990->presenter >= 0 &&
             videoManagerIsHandleActive(v9990->presenter);

    v9990->blank  = on ? 0x8000 : 0;
    v9990->ysMask = on && (v9990->latchControl & 0x20) ? 0xffff : 0x7fff;
}

static int displayLatched(V9990* v9990)
{
    return v9990->showOnDisplay && (v9990->latchControl & 0x80);
}

/* A DISP edge overrides any earlier menu choice. */
static void followDisplay(V9990* v9990, int wasOn)
{
    int on = displayLatched(v9990);

    if (on == wasOn) {
        return;
    }
    videoManagerClaim(v9990->videoHandle, on);
    if (boardGetVideoAutodetect() && !v9990->loading) {
        if (on) {
            videoManagerActivate(v9990->videoHandle);
        }
        else {
            videoManagerActivateDefault();
        }
    }
}

static void frameStart(V9990* v9990, UInt32 time)
{
    int wasOn = displayLatched(v9990);

    /* Also completes a frame that a reset cut short. */
    sync(v9990, time);
    renderLines(v9990, v9990->frameLines);

    v9990->secondField     = !v9990->secondField;
    v9990->frameMode       = v9990->mode;
    v9990->latchPal        = (v9990->regs[R_SCREEN_MODE1] & 0x08) != 0;
    v9990->latchIl         = (v9990->regs[R_SCREEN_MODE1] & 0x02) != 0;
    v9990->latchSm         = (v9990->regs[R_SCREEN_MODE1] & 0x10) != 0;
    v9990->latchSm1        = (v9990->regs[R_SCREEN_MODE1] & 0x20) != 0;
    v9990->latchAdjust     = v9990->regs[R_DISPLAY_ADJ];
    v9990->latchControl    = v9990->regs[R_CONTROL];
    v9990->latchScrollAYHi = v9990->regs[R_SCROLL_AY_HI];
    v9990->latchScrollBYHi = v9990->regs[R_SCROLL_BY_HI];
    v9990->frameKeyed      = v9990->blank != 0;
    latchSuperimpose(v9990);
    if (isHighScan(v9990->frameMode)) {
        v9990->latchPal = 0;
        v9990->latchIl  = 0;
        v9990->latchSm  = 0;
        v9990->latchSm1 = 0;
    }
    v9990->frameStartTime = time;
    calcFrameGeometry(v9990);
    v9990->scrollLineA = v9990->displayStartLine;
    v9990->scrollLineB = v9990->displayStartLine;
    startPicture(v9990, frameBufferFlipDrawFrameOf(v9990->frameBufferData));
    followDisplay(v9990, wasOn);

    boardTimerAdd(v9990->timerFrame, time + v9990->frameTicks);

    v9990->viArmed = 1;
    v9990->viTime  = v9990->lineZero + v9990->displayEndLine * v9990->lineTicks;
    if (v9990->viTime >= v9990->frameTicks) {
        /* A large vertical adjust pushes the display end past the frame. */
        v9990->viTime = v9990->frameTicks - 1;
    }
    v9990->viTime += time;
    boardTimerAdd(v9990->timerVi, v9990->viTime);

    scheduleHi(v9990, time);
    scheduleCommandEnd(v9990, time);
}

static void onFrame(V9990* v9990, UInt32 time)
{
    frameStart(v9990, time);

    /* While shown, the screen is presented at this chip's frame rate. */
    if (videoManagerIsHandleActive(v9990->videoHandle) ||
        (v9990->presenter >= 0 && videoManagerIsHandleActive(v9990->presenter)))
    {
        emulatorRefreshFromSource();
    }
}

static void onVi(V9990* v9990, UInt32 time)
{
    sync(v9990, time);
    v9990->viArmed = 0;
    raiseIrq(v9990, IRQ_VI);
}

static void onCommandEnd(void* ref)
{
    V9990* v9990 = (V9990*)ref;

    boardTimerRemove(v9990->timerCe);
    raiseIrq(v9990, IRQ_CE);
}

static void onCe(V9990* v9990, UInt32 time)
{
    sync(v9990, time);
    scheduleCommandEnd(v9990, time);
}

static void onHi(V9990* v9990, UInt32 time)
{
    sync(v9990, time);
    v9990->hiArmed = 0;
    raiseIrq(v9990, IRQ_HI);
}

/* P#5 at time, given the command engine's bits; computing it changes nothing. */
static UInt8 statusAt(V9990* v9990, UInt32 time, UInt8 status)
{
    UInt32 lineTicks = v9990->lineTicks;
    UInt32 pos = (time - v9990->frameStartTime) + lineTicks - v9990->lineZero;
    int y = (int)(pos / lineTicks) - 1;
    int x = (int)(pos % lineTicks) * 2;
    int left;
    int right;
    int window;

    calcHorizontalArea(v9990, &left, &right, &window);

    if (y < v9990->displayStartLine || y >= v9990->displayEndLine) {
        status |= 0x40;
    }
    if (x < left || x >= right) {
        status |= 0x20;
    }
    if (v9990->mcs) {
        status |= 0x04;
    }
    if (v9990->latchIl && v9990->secondField) {
        status |= 0x02;
    }
    return status;
}

static UInt8 readStatus(V9990* v9990, UInt32 time)
{
    return statusAt(v9990, time, v9990CmdGetStatus(v9990->cmd, time));
}

static UInt32 getVramAddress(V9990* v9990, int reg)
{
    return v9990->regs[reg] | (v9990->regs[reg + 1] << 8) | ((v9990->regs[reg + 2] & 0x07) << 16);
}

static void setVramAddress(V9990* v9990, int reg, UInt32 address)
{
    v9990->regs[reg]     = (UInt8)address;
    v9990->regs[reg + 1] = (UInt8)(address >> 8);
    v9990->regs[reg + 2] = (UInt8)(((address >> 16) & 0x07) | (v9990->regs[reg + 2] & 0x80));
}

static UInt8 readVramCpu(V9990* v9990, UInt32 address)
{
    return v9990->vram[v9990MapAddress(v9990->mode, address)];
}

/* What P#3 returns for reg; borderX is only used for R#53 and R#54. */
static UInt8 registerValue(V9990* v9990, int reg, UInt16 borderX)
{
    if (v9990->systemReset || !(regAccess[reg] & REG_R)) {
        return 0xff;
    }
    if (reg == R_CMD_BORDER_LO) {
        return (UInt8)borderX;
    }
    if (reg == R_CMD_BORDER_HI) {
        return (UInt8)(borderX >> 8);
    }
    return v9990->regs[reg];
}

static UInt8 readRegister(V9990* v9990, int reg, UInt32 time)
{
    UInt16 borderX = 0;

    if (!v9990->systemReset && (reg == R_CMD_BORDER_LO || reg == R_CMD_BORDER_HI)) {
        borderX = v9990CmdGetBorderX(v9990->cmd, time);
    }
    return registerValue(v9990, reg, borderX);
}

/* Writing the low byte of SCAY (SCBY) during the display makes the current
** line image row SCAY (SCBY), as the first display line is. */
static void restartScroll(V9990* v9990, int* restartLine, UInt32 time)
{
    Int32 t = (Int32)(time - v9990->frameStartTime) - (Int32)v9990->lineZero;
    int line = floorDiv(t, (Int32)v9990->lineTicks);

    if ((v9990->latchControl & 0x80) &&
        line >= v9990->displayStartLine && line < v9990->displayEndLine) {
        *restartLine = line;
    }
}

static void writeRegister(V9990* v9990, int reg, UInt8 value, UInt32 time)
{
    if (!(regAccess[reg] & REG_W)) {
        return;
    }

    if (reg >= 32) {
        v9990->regs[reg] = value;
        if (reg == R_CMD_OPCODE) {
            v9990CmdStart(v9990->cmd, value, calcMode(v9990), v9990->regs[R_SCREEN_MODE0], time);
            runCommand(v9990, time);
            scheduleCommandEnd(v9990, time);
        }
        else {
            v9990CmdWriteRegister(v9990->cmd, reg, value, time);
        }
        return;
    }

    v9990->regs[reg] = value & regWriteMask[reg];

    switch (reg) {
    case R_VRAM_READ + 2:
        v9990->readBuffer = readVramCpu(v9990, getVramAddress(v9990, R_VRAM_READ));
        break;
    case R_SCREEN_MODE0:
    case R_SCREEN_MODE1:
        scheduleModeChange(v9990, time);
        break;
    case R_CONTROL:
        scheduleCommandEnd(v9990, time);
        break;
    case R_INT_ENABLE:
        updateIrq(v9990);
        break;
    case R_SCROLL_AY_LO:
        restartScroll(v9990, &v9990->scrollLineA, time);
        break;
    case R_SCROLL_BY_LO:
        restartScroll(v9990, &v9990->scrollLineB, time);
        break;
    case R_INT_LINE_LO:
    case R_INT_LINE_HI:
    case R_INT_X:
        scheduleHi(v9990, time);
        break;
    }
}

static void stepPalettePointer(V9990* v9990)
{
    UInt8 ptr = v9990->regs[R_PALETTE_PTR];

    switch (ptr & 3) {
    case 0:
    case 1:  ptr += 1; break;
    case 2:  ptr += 2; break;
    default: ptr -= 3; break;
    }
    v9990->regs[R_PALETTE_PTR] = ptr;
}

static void writeInterruptFlags(V9990* v9990, UInt8 value, UInt32 time)
{
    v9990->pending &= ~value;
    updateIrq(v9990);
    scheduleHi(v9990, time);
}

UInt8 v9990ReadPort(V9990* v9990, UInt16 port)
{
    UInt32 time = boardSystemTime();
    UInt8 value;

    sync(v9990, time);

    switch (port & 0x0f) {
    case P_VRAM_DATA:
        value = v9990->readBuffer;
        if (!v9990->systemReset && !(v9990->regs[R_VRAM_READ + 2] & 0x80)) {
            UInt32 address = getVramAddress(v9990, R_VRAM_READ) + 1;
            setVramAddress(v9990, R_VRAM_READ, address);
            v9990->readBuffer = readVramCpu(v9990, address);
        }
        return value;

    case P_PALETTE_DATA:
        value = v9990->palette[v9990->regs[R_PALETTE_PTR]];
        if (!v9990->systemReset && !(v9990->regs[R_PALETTE_CTRL] & 0x10)) {
            stepPalettePointer(v9990);
        }
        return value;

    case P_COMMAND_DATA:
        return v9990CmdReadData(v9990->cmd, time);

    case P_REGISTER_DATA:
        value = readRegister(v9990, v9990->regSelect & 0x3f, time);
        if (!v9990->systemReset && !(v9990->regSelect & 0x40)) {
            v9990->regSelect = (UInt8)((v9990->regSelect + 1) & ~0x40);
        }
        return value;

    case P_STATUS:
        return readStatus(v9990, time);

    case P_INT_FLAG:
        return v9990->pending;
    }

    return 0xff;
}

void v9990WritePort(V9990* v9990, UInt16 port, UInt8 value)
{
    UInt32 time = boardSystemTime();
    int i;

    sync(v9990, time);

    switch (port & 0x0f) {
    case P_VRAM_DATA:
        if (!v9990->systemReset) {
            UInt32 address = getVramAddress(v9990, R_VRAM_WRITE);
            v9990->vram[v9990MapAddress(v9990->mode, address)] = value;
            if (!(v9990->regs[R_VRAM_WRITE + 2] & 0x80)) {
                setVramAddress(v9990, R_VRAM_WRITE, address + 1);
            }
            if (v9990CmdReadsVram(v9990->cmd)) {
                scheduleCommandEnd(v9990, time);
            }
        }
        break;

    case P_PALETTE_DATA:
        if (v9990->systemReset) {
            v9990->palette[v9990->regs[R_PALETTE_PTR]] = 0;
            break;
        }
        v9990->palette[v9990->regs[R_PALETTE_PTR]] = value & paletteMask[v9990->regs[R_PALETTE_PTR] & 3];
        stepPalettePointer(v9990);
        break;

    case P_COMMAND_DATA:
        v9990CmdWriteData(v9990->cmd, value, time);
        break;

    case P_REGISTER_DATA:
        writeRegister(v9990, v9990->regSelect & 0x3f, v9990->systemReset ? 0 : value, time);
        if (!(v9990->regSelect & 0x80)) {
            v9990->regSelect = (UInt8)((v9990->regSelect & 0xc0) | ((v9990->regSelect + 1) & 0x3f));
        }
        break;

    case P_REGISTER_SEL:
        v9990->regSelect = v9990->systemReset ? 0 : value;
        break;

    case P_INT_FLAG:
        writeInterruptFlags(v9990, value, time);
        break;

    case P_SYSTEM_CTRL:
        v9990->mcs = value & 0x01;
        scheduleModeChange(v9990, time);
        if (((value & 0x02) != 0) != v9990->systemReset) {
            v9990->systemReset = (value & 0x02) != 0;
            if (v9990->systemReset) {
                /* Palette and VRAM keep their contents. */
                for (i = 0; i < 64; i++) {
                    writeRegister(v9990, i, 0, time);
                }
                writeInterruptFlags(v9990, 0xff, time);
            }
        }
        break;
    }
}

static void powerOnVram(V9990* v9990)
{
    UInt32 i;

    for (i = 0; i < V9990_VRAM_SIZE; i += 1024) {
        memset(v9990->vram + i, 0x00, 512);
        memset(v9990->vram + i + 512, 0xff, 512);
    }
}

static void powerOnPalette(V9990* v9990)
{
    int i;

    for (i = 0; i < 64; i++) {
        v9990->palette[4 * i + 0] = 0x9f;
        v9990->palette[4 * i + 1] = 0x1f;
        v9990->palette[4 * i + 2] = 0x1f;
        v9990->palette[4 * i + 3] = 0x00;
    }
}

void v9990Reset(V9990* v9990)
{
    sync(v9990, boardSystemTime());

    memset(v9990->regs, 0, sizeof(v9990->regs));
    v9990->regSelect   = 0xff;
    v9990->readBuffer  = 0;
    v9990->pending     = 0;
    v9990->mcs         = 0;
    v9990->systemReset = 0;
    v9990->mode        = calcMode(v9990);
    v9990->modeR6      = 0;
    v9990->modeMcs     = 0;
    v9990->modePending = 0;
    v9990->secondField = 1;
    v9990CmdReset(v9990->cmd, boardSystemTime());
    boardTimerRemove(v9990->timerCe);

    updateIrq(v9990);
    frameStart(v9990, boardSystemTime());

    /* After a reset the display is off, so a V9990 chosen by hand gives way too. */
    if (v9990->showOnDisplay && boardGetVideoAutodetect() &&
        videoManagerIsHandleActive(v9990->videoHandle))
    {
        videoManagerActivateDefault();
    }
}

static void powerOn(V9990* v9990)
{
    powerOnVram(v9990);
    powerOnPalette(v9990);
    v9990Reset(v9990);
}

#define DBG_PALETTE_NAME "Palette"

/* What a read of P#port would return, without its side effects. The command
** engine is not synced, so its bits are as of the last access that synced it. */
static UInt8 peekPort(V9990* v9990, int port, UInt32 time)
{
    switch (port) {
    case P_VRAM_DATA:
        return v9990->readBuffer;
    case P_PALETTE_DATA:
        return v9990->palette[v9990->regs[R_PALETTE_PTR]];
    case P_COMMAND_DATA:
        return v9990CmdPeekData(v9990->cmd);
    case P_REGISTER_DATA:
        return registerValue(v9990, v9990->regSelect & 0x3f, v9990CmdPeekBorderX(v9990->cmd));
    case P_STATUS:
        return statusAt(v9990, time, v9990CmdPeekStatus(v9990->cmd));
    case P_INT_FLAG:
        return v9990->pending;
    }
    return 0xff;
}

static void getDebugInfo(V9990* v9990, DbgDevice* dbgDevice)
{
    static const UInt8 portDirection[8] = {
        DBG_IO_READWRITE, DBG_IO_READWRITE, DBG_IO_READWRITE, DBG_IO_READWRITE,
        DBG_IO_WRITE,     DBG_IO_READ,      DBG_IO_READWRITE, DBG_IO_WRITE
    };
    UInt32 time = boardSystemTime();
    UInt16 borderX = v9990CmdPeekBorderX(v9990->cmd);
    UInt8 palette[64 * 3];
    DbgRegisterBank* regBank;
    DbgIoPorts* ioPorts;
    int i;

    dbgDeviceAddMemoryBlock(dbgDevice, langDbgMemVram(), 0, 0, V9990_VRAM_SIZE, v9990->vram);

    for (i = 0; i < 64 * 3; i++) {
        palette[i] = v9990->palette[4 * (i / 3) + i % 3];
    }
    dbgDeviceAddMemoryBlock(dbgDevice, DBG_PALETTE_NAME, 0, 0, sizeof(palette), palette);

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegs(), 64);
    for (i = 0; i < 64; i++) {
        char name[8];
        UInt8 value = v9990->regs[i];

        if (i == R_CMD_BORDER_LO) value = (UInt8)borderX;
        if (i == R_CMD_BORDER_HI) value = (UInt8)(borderX >> 8);
        sprintf(name, "R%d", i);
        dbgRegisterBankAddRegister(regBank, i, name, 8, value);
    }

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, "V9990", 16);
    for (i = 0; i < 16; i++) {
        dbgIoPortsAddPort(ioPorts, i, (UInt16)(0x60 + i),
                          i < 8 ? portDirection[i] : DBG_IO_NONE, peekPort(v9990, i, time));
    }
}

static int dbgWriteMemory(V9990* v9990, char* name, void* data, int start, int size)
{
    UInt8* bytes = data;
    int i;

    if (start < 0 || size < 0) {
        return 0;
    }
    if (strcmp(name, langDbgMemVram()) == 0) {
        if (start + size > V9990_VRAM_SIZE) {
            return 0;
        }
        memcpy(v9990->vram + start, data, size);
        return 1;
    }
    if (strcmp(name, DBG_PALETTE_NAME) == 0) {
        if (start + size > 64 * 3) {
            return 0;
        }
        for (i = start; i < start + size; i++) {
            v9990->palette[4 * (i / 3) + i % 3] = bytes[i - start] & paletteMask[i % 3];
        }
        return 1;
    }
    return 0;
}

/* Goes through the normal register write. The command registers live in the
** engine and R#52 would start a command, so R#32 and above are refused. */
static int dbgWriteRegister(V9990* v9990, char* name, int regIndex, UInt32 value)
{
    UInt32 time = boardSystemTime();

    if (regIndex < 0 || regIndex >= 32 || !(regAccess[regIndex] & REG_W)) {
        return 0;
    }
    sync(v9990, time);
    writeRegister(v9990, regIndex, (UInt8)value, time);
    return 1;
}

V9990* v9990Create(void)
{
    VideoCallbacks videoCallbacks = { NULL, NULL, 1, 1 };
    DebugCallbacks debugCallbacks = { getDebugInfo, dbgWriteMemory, dbgWriteRegister, NULL };
    V9990* v9990 = calloc(1, sizeof(V9990));

    v9990->vram = malloc(V9990_VRAM_SIZE);
    v9990->cmd  = v9990CmdCreate(v9990->vram, onCommandEnd, v9990);

    v9990->timerFrame = boardTimerCreate(onFrame, v9990);
    v9990->timerVi    = boardTimerCreate(onVi, v9990);
    v9990->timerHi    = boardTimerCreate(onHi, v9990);
    v9990->timerCe    = boardTimerCreate(onCe, v9990);

    v9990->frameBufferData = frameBufferDataCreate(V9990_FB_WIDTH, V9990_FB_LINES, 1);

    /* Not registered yet: no handle for calcFrameGeometry to report the rate to. */
    v9990->videoHandle = -1;
    v9990->presenter   = -1;
    v9990->ysMask      = 0x7fff;
    powerOn(v9990);

    v9990->videoHandle = videoManagerRegister("V9990", v9990->frameBufferData, &videoCallbacks, v9990);
    videoManagerSetRefreshRate(v9990->videoHandle, frameRate(v9990));

    v9990->debugHandle = debugDeviceRegister(DBGTYPE_VIDEO, "V9990", &debugCallbacks, v9990);

    theV9990 = v9990;

    return v9990;
}

void v9990SetShowOnDisplay(V9990* v9990, int enable)
{
    v9990->showOnDisplay = enable;
    videoManagerClaim(v9990->videoHandle, displayLatched(v9990));
}

int v9990GetVideoHandle(V9990* v9990)
{
    return v9990->videoHandle;
}

V9990Mode v9990GetShownMode(int* superimposed)
{
    V9990* v9990 = theV9990;

    *superimposed = 0;
    if (v9990 == NULL) {
        return V9990_MODE_NONE;
    }
    if (v9990->presenter >= 0 && videoManagerIsHandleActive(v9990->presenter)) {
        *superimposed = v9990->superimpose;
        return v9990->frameMode;
    }
    if (videoManagerIsHandleActive(v9990->videoHandle)) {
        return v9990->frameMode;
    }
    return V9990_MODE_NONE;
}

int v9990IsFrameKeyed(V9990* v9990)
{
    return v9990->frameKeyed;
}

void v9990SetPresenter(V9990* v9990, int handle, int superimpose)
{
    v9990->presenter   = handle;
    v9990->superimpose = superimpose;
    reportRates(v9990, handle);
}

void v9990Destroy(V9990* v9990)
{
    if (theV9990 == v9990) {
        theV9990 = NULL;
    }
    debugDeviceUnregister(v9990->debugHandle);

    boardTimerDestroy(v9990->timerFrame);
    boardTimerDestroy(v9990->timerVi);
    boardTimerDestroy(v9990->timerHi);
    boardTimerDestroy(v9990->timerCe);

    boardClearInt(V9990_INT);

    videoManagerUnregister(v9990->videoHandle);
    frameBufferDataDestroy(v9990->frameBufferData);

    v9990CmdDestroy(v9990->cmd);
    free(v9990->vram);
    free(v9990);
}

void v9990SaveState(V9990* v9990)
{
    SaveState* state = saveStateOpenForWrite("V9990");

    saveStateSetBuffer(state, "regs",    v9990->regs,    sizeof(v9990->regs));
    saveStateSetBuffer(state, "palette", v9990->palette, sizeof(v9990->palette));

    saveStateSet(state, "regSelect",       v9990->regSelect);
    saveStateSet(state, "readBuffer",      v9990->readBuffer);
    saveStateSet(state, "pending",         v9990->pending);
    saveStateSet(state, "mcs",             v9990->mcs);
    saveStateSet(state, "systemReset",     v9990->systemReset);
    saveStateSet(state, "mode",            v9990->mode);
    saveStateSet(state, "modeR6",          v9990->modeR6);
    saveStateSet(state, "modeMcs",         v9990->modeMcs);
    saveStateSet(state, "modePending",     v9990->modePending);
    saveStateSet(state, "modeApplyTime",   v9990->modeApplyTime);
    saveStateSet(state, "frameStartTime",  v9990->frameStartTime);
    saveStateSet(state, "secondField",     v9990->secondField);
    saveStateSet(state, "latchPal",        v9990->latchPal);
    saveStateSet(state, "latchIl",         v9990->latchIl);
    saveStateSet(state, "latchSm",         v9990->latchSm);
    saveStateSet(state, "latchSm1",        v9990->latchSm1);
    saveStateSet(state, "latchAdjust",     v9990->latchAdjust);
    saveStateSet(state, "latchControl",    v9990->latchControl);
    saveStateSet(state, "latchScrollAYHi", v9990->latchScrollAYHi);
    saveStateSet(state, "latchScrollBYHi", v9990->latchScrollBYHi);
    saveStateSet(state, "frameMode",       v9990->frameMode);
    saveStateSet(state, "viArmed",         v9990->viArmed);
    saveStateSet(state, "viTime",          v9990->viTime);
    saveStateSet(state, "hiArmed",         v9990->hiArmed);
    saveStateSet(state, "hiTime",          v9990->hiTime);
    saveStateSet(state, "scrollLineA",     v9990->scrollLineA);
    saveStateSet(state, "scrollLineB",     v9990->scrollLineB);

    saveStateClose(state);

    state = saveStateOpenForWrite("V9990Vram");
    saveStateSetBuffer(state, "vram", v9990->vram, V9990_VRAM_SIZE);
    saveStateClose(state);

    v9990CmdSaveState(v9990->cmd);
}

void v9990LoadState(V9990* v9990)
{
    SaveState* state = saveStateOpenForRead("V9990");

    /* A state saved before the cartridge was inserted (rewind across a hot
    ** insert) has no section: start from power-on, as an insert does. */
    if (saveStateIsEmpty(state)) {
        saveStateClose(state);
        powerOn(v9990);
        return;
    }

    saveStateGetBuffer(state, "regs",    v9990->regs,    sizeof(v9990->regs));
    saveStateGetBuffer(state, "palette", v9990->palette, sizeof(v9990->palette));

    v9990->regSelect       = (UInt8)saveStateGet(state, "regSelect",       0xff);
    v9990->readBuffer      = (UInt8)saveStateGet(state, "readBuffer",      0);
    v9990->pending         = (UInt8)saveStateGet(state, "pending",         0);
    v9990->mcs             = (UInt8)saveStateGet(state, "mcs",             0);
    v9990->systemReset     = (UInt8)saveStateGet(state, "systemReset",     0);
    v9990->mode            = (V9990Mode)saveStateGet(state, "mode",        V9990_MODE_P1);
    v9990->modeR6          = (UInt8)saveStateGet(state, "modeR6",   v9990->regs[R_SCREEN_MODE0]);
    v9990->modeMcs         = saveStateGet(state, "modeMcs",         0);
    v9990->modePending     = saveStateGet(state, "modePending",     0);
    v9990->modeApplyTime   = saveStateGet(state, "modeApplyTime",   0);
    v9990->frameStartTime  = saveStateGet(state, "frameStartTime",  boardSystemTime());
    v9990->secondField     = saveStateGet(state, "secondField",     0);
    v9990->latchPal        = saveStateGet(state, "latchPal",        0);
    v9990->latchIl         = saveStateGet(state, "latchIl",         0);
    v9990->latchSm         = saveStateGet(state, "latchSm",         0);
    v9990->latchSm1        = saveStateGet(state, "latchSm1",        0);
    v9990->latchAdjust     = (UInt8)saveStateGet(state, "latchAdjust",     0);
    v9990->latchControl    = (UInt8)saveStateGet(state, "latchControl",    0);
    v9990->latchScrollAYHi = (UInt8)saveStateGet(state, "latchScrollAYHi", 0);
    v9990->latchScrollBYHi = (UInt8)saveStateGet(state, "latchScrollBYHi", 0);
    v9990->frameMode       = (V9990Mode)saveStateGet(state, "frameMode",   V9990_MODE_P1);
    v9990->viArmed         = saveStateGet(state, "viArmed",         0);
    v9990->viTime          = saveStateGet(state, "viTime",          0);
    v9990->hiArmed         = saveStateGet(state, "hiArmed",         0);
    v9990->hiTime          = saveStateGet(state, "hiTime",          0);
    v9990->scrollLineA     = saveStateGet(state, "scrollLineA",     -1);
    v9990->scrollLineB     = saveStateGet(state, "scrollLineB",     -1);

    saveStateClose(state);

    state = saveStateOpenForRead("V9990Vram");
    if (saveStateIsEmpty(state)) {
        powerOnVram(v9990);
    }
    else {
        saveStateGetBuffer(state, "vram", v9990->vram, V9990_VRAM_SIZE);
    }
    saveStateClose(state);

    v9990CmdLoadState(v9990->cmd, boardSystemTime());

    calcFrameGeometry(v9990);
    latchSuperimpose(v9990);
    if (v9990->scrollLineA < 0) {
        v9990->scrollLineA = v9990->displayStartLine;
    }
    if (v9990->scrollLineB < 0) {
        v9990->scrollLineB = v9990->displayStartLine;
    }

    /* The shown source is restored by the video manager, not switched here. */
    videoManagerClaim(v9990->videoHandle, displayLatched(v9990));

    /* The frame buffer is not saved: redraw this frame from its first line. */
    startPicture(v9990, frameBufferGetDrawFrameOf(v9990->frameBufferData));

    /* A snapshot can be taken while timers that were already due had not run.
    ** The board drops timers in the past, so their events happen here. */
    if (v9990->viArmed && (Int32)(v9990->viTime - boardSystemTime()) <= 0) {
        v9990->viArmed  = 0;
        v9990->pending |= IRQ_VI;
    }
    if (v9990->hiArmed && (Int32)(v9990->hiTime - boardSystemTime()) <= 0) {
        v9990->hiArmed  = 0;
        v9990->pending |= IRQ_HI;
    }
    if ((Int32)(v9990->frameStartTime + v9990->frameTicks - boardSystemTime()) <= 0) {
        v9990->loading = 1;
        frameStart(v9990, v9990->frameStartTime + v9990->frameTicks);
        v9990->loading = 0;
        updateIrq(v9990);
        return;
    }

    boardTimerAdd(v9990->timerFrame, v9990->frameStartTime + v9990->frameTicks);
    if (v9990->viArmed) {
        boardTimerAdd(v9990->timerVi, v9990->viTime);
    }
    else {
        boardTimerRemove(v9990->timerVi);
    }
    if (v9990->hiArmed) {
        boardTimerAdd(v9990->timerHi, v9990->hiTime);
    }
    else {
        boardTimerRemove(v9990->timerHi);
    }
    scheduleCommandEnd(v9990, boardSystemTime());

    updateIrq(v9990);
}
