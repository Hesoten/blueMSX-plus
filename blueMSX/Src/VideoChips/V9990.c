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
#include "Board.h"
#include "SaveState.h"
#include "FrameBuffer.h"
#include "VideoManager.h"
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
#define R_SCROLL_AY_HI  18
#define R_SCROLL_BY_HI  22
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
    UInt8  cmdData;
    UInt8  pending;
    UInt8  mcs;
    UInt8  systemReset;

    /* Display mode, recomputed at the line start after R#6, R#7 or MCS change. */
    V9990Mode mode;
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

    /* Lines from lineZero; lines before renderLine are in the frame buffer. */
    FrameBufferData* frameBufferData;
    int    videoHandle;
    int    renderLine;

    BoardTimer* timerFrame;
    BoardTimer* timerVi;
    BoardTimer* timerHi;
    int    viArmed;
    UInt32 viTime;
    int    hiArmed;
    UInt32 hiTime;
};

static UInt32 mapBx(UInt32 address)
{
    return ((address & 1) << 18) | ((address & 0x7fffe) >> 1);
}

UInt32 v9990MapAddress(V9990Mode mode, UInt32 address)
{
    address &= V9990_VRAM_MASK;

    switch (mode) {
    case V9990_MODE_P1:
    case V9990_MODE_STANDBY:
        return address;
    case V9990_MODE_P2:
        if (address < 0x78000) {
            return mapBx(address);
        }
        if (address < 0x7c000) {
            return address - 0x3c000;
        }
        return address;
    default:
        return mapBx(address);
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
    ** 14 border lines above a 212-line display. */
    v9990->windowTopLine = windowTop + topBorder + (displayLines - V9990_FB_LINES) / 2;
}

/* Horizontal display area in UC (42.95 MHz) from the line start, using the
** display mode in effect now. window is where frame buffer sample 0 starts:
** the 2176 UC window is centred on the nominal display. */
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
    const UInt8* entry = v9990->palette + 4 * (index & 0x3f);
    int r = entry[0] & 0x1f;
    int g = entry[1] & 0x1f;
    int b = entry[2] & 0x1f;

    return (UInt16)videoGetColor((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

static void fillLine(UInt16* line, int count, UInt16 color)
{
    int x;

    for (x = 0; x < count; x++) {
        line[x] = color;
    }
}

/* Draws scan line scanLine (counted from lineZero) into frame buffer line y. */
static void drawLine(V9990* v9990, FrameBuffer* frame, int y, int scanLine)
{
    V9990Mode mode = v9990->mode;
    int wide = mode == V9990_MODE_P2 || mode == V9990_MODE_B3;
    int count = V9990_FB_WIDTH << wide;
    int ucPerSample = 8 >> wide;
    UInt16* line = frameBufferGetLine(frame, y);
    UInt16 backdrop;
    UInt16 border;
    int left;
    int right;
    int window;
    int x;

    frameBufferSetDoubleWidth(frame, y, wide);

    if (mode == V9990_MODE_STANDBY) {
        fillLine(line, count, 0);
        return;
    }

    backdrop = paletteColor(v9990, v9990->regs[R_BACKDROP]);
    if (!(v9990->latchControl & 0x80)) {
        fillLine(line, count, backdrop);
        return;
    }

    /* Outside the display, overscan and high-scan modes show the blanking. */
    border = isOverscan(mode) || isHighScan(v9990->frameMode) ? 0 : backdrop;
    if (scanLine < v9990->displayStartLine || scanLine >= v9990->displayEndLine) {
        fillLine(line, count, border);
        return;
    }

    /* No display mode is drawn yet: the display area shows the backdrop. */
    calcHorizontalArea(v9990, &left, &right, &window);
    for (x = 0; x < count; x++) {
        int center = window + x * ucPerSample + ucPerSample / 2;
        line[x] = center >= left && center < right ? backdrop : border;
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
        if (frame != NULL && y >= 0 && y < V9990_FB_LINES) {
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

/* Brings the picture and the display mode up to time. Every port access and
** timer calls this first, so a line is drawn with the state at its commit. */
static void sync(V9990* v9990, UInt32 time)
{
    renderLines(v9990, committedLines(v9990, time));
    syncMode(v9990, time);
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
    V9990Mode mode = v9990->frameMode;
    int bitmap = mode != V9990_MODE_P1 && mode != V9990_MODE_P2 && mode != V9990_MODE_STANDBY;

    v9990->renderLine = 0;
    if (frame == NULL) {
        return;
    }

    frameBufferSetLineCount(frame, V9990_FB_LINES);
    frame->interlaceRaster = v9990->latchIl;
    /* EO selects the second field's lines only with IL, and only in Bx. */
    if (v9990->latchIl && bitmap && (v9990->regs[R_SCREEN_MODE1] & 0x04)) {
        frameBufferSetInterlace(frame, v9990->secondField ? INTERLACE_ODD : INTERLACE_EVEN);
    }
    else {
        frameBufferSetInterlace(frame, INTERLACE_NONE);
    }
}

static void frameStart(V9990* v9990, UInt32 time)
{
    /* Also completes a frame that a reset cut short. */
    renderLines(v9990, v9990->frameLines);
    syncMode(v9990, time);

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
    if (isHighScan(v9990->frameMode)) {
        v9990->latchPal = 0;
        v9990->latchIl  = 0;
        v9990->latchSm  = 0;
        v9990->latchSm1 = 0;
    }
    v9990->frameStartTime = time;
    calcFrameGeometry(v9990);
    startPicture(v9990, frameBufferFlipDrawFrameOf(v9990->frameBufferData));

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
}

static void onFrame(V9990* v9990, UInt32 time)
{
    frameStart(v9990, time);
}

static void onVi(V9990* v9990, UInt32 time)
{
    sync(v9990, time);
    v9990->viArmed = 0;
    raiseIrq(v9990, IRQ_VI);
}

static void onHi(V9990* v9990, UInt32 time)
{
    sync(v9990, time);
    v9990->hiArmed = 0;
    raiseIrq(v9990, IRQ_HI);
}

static UInt8 readStatus(V9990* v9990, UInt32 time)
{
    UInt32 lineTicks = v9990->lineTicks;
    UInt32 pos = (time - v9990->frameStartTime) + lineTicks - v9990->lineZero;
    int y = (int)(pos / lineTicks) - 1;
    int x = (int)(pos % lineTicks) * 2;
    int left;
    int right;
    int window;
    UInt8 status = 0;

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

static UInt8 readRegister(V9990* v9990, int reg)
{
    if (v9990->systemReset || !(regAccess[reg] & REG_R)) {
        return 0xff;
    }
    /* Border X of a search command; without a command engine it stays 0. */
    if (reg == R_CMD_BORDER_LO || reg == R_CMD_BORDER_HI) {
        return 0x00;
    }
    return v9990->regs[reg];
}

static void writeRegister(V9990* v9990, int reg, UInt8 value, UInt32 time)
{
    if (!(regAccess[reg] & REG_W)) {
        return;
    }

    if (reg >= 32) {
        v9990->regs[reg] = value;
        /* No command engine yet: every opcode, STOP included, ends at once
        ** without CE in P#5 ever reading 1, and raises the CE flag in P#6. */
        if (reg == R_CMD_OPCODE) {
            raiseIrq(v9990, IRQ_CE);
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
    case R_INT_ENABLE:
        updateIrq(v9990);
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
        /* No transfer is ever ready (TR = 0) without a command engine. */
        return 0xff;

    case P_REGISTER_DATA:
        value = readRegister(v9990, v9990->regSelect & 0x3f);
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
        v9990->cmdData = value;
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
    v9990->cmdData     = 0;
    v9990->pending     = 0;
    v9990->mcs         = 0;
    v9990->systemReset = 0;
    v9990->mode        = calcMode(v9990);
    v9990->modeMcs     = 0;
    v9990->modePending = 0;
    v9990->secondField = 1;

    updateIrq(v9990);
    frameStart(v9990, boardSystemTime());
}

static void powerOn(V9990* v9990)
{
    powerOnVram(v9990);
    powerOnPalette(v9990);
    v9990Reset(v9990);
}

V9990* v9990Create(void)
{
    VideoCallbacks videoCallbacks = { NULL, NULL };
    V9990* v9990 = calloc(1, sizeof(V9990));

    v9990->vram = malloc(V9990_VRAM_SIZE);

    v9990->timerFrame = boardTimerCreate(onFrame, v9990);
    v9990->timerVi    = boardTimerCreate(onVi, v9990);
    v9990->timerHi    = boardTimerCreate(onHi, v9990);

    v9990->frameBufferData = frameBufferDataCreate(V9990_FB_WIDTH, V9990_FB_LINES, 1);

    powerOn(v9990);

    v9990->videoHandle = videoManagerRegister("V9990", v9990->frameBufferData, &videoCallbacks, v9990);

    return v9990;
}

void v9990Destroy(V9990* v9990)
{
    boardTimerDestroy(v9990->timerFrame);
    boardTimerDestroy(v9990->timerVi);
    boardTimerDestroy(v9990->timerHi);

    boardClearInt(V9990_INT);

    videoManagerUnregister(v9990->videoHandle);
    frameBufferDataDestroy(v9990->frameBufferData);

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
    saveStateSet(state, "cmdData",         v9990->cmdData);
    saveStateSet(state, "pending",         v9990->pending);
    saveStateSet(state, "mcs",             v9990->mcs);
    saveStateSet(state, "systemReset",     v9990->systemReset);
    saveStateSet(state, "mode",            v9990->mode);
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

    saveStateClose(state);

    state = saveStateOpenForWrite("V9990Vram");
    saveStateSetBuffer(state, "vram", v9990->vram, V9990_VRAM_SIZE);
    saveStateClose(state);
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
    v9990->cmdData         = (UInt8)saveStateGet(state, "cmdData",         0);
    v9990->pending         = (UInt8)saveStateGet(state, "pending",         0);
    v9990->mcs             = (UInt8)saveStateGet(state, "mcs",             0);
    v9990->systemReset     = (UInt8)saveStateGet(state, "systemReset",     0);
    v9990->mode            = (V9990Mode)saveStateGet(state, "mode",        V9990_MODE_P1);
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

    saveStateClose(state);

    state = saveStateOpenForRead("V9990Vram");
    if (saveStateIsEmpty(state)) {
        powerOnVram(v9990);
    }
    else {
        saveStateGetBuffer(state, "vram", v9990->vram, V9990_VRAM_SIZE);
    }
    saveStateClose(state);

    calcFrameGeometry(v9990);

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
        frameStart(v9990, v9990->frameStartTime + v9990->frameTicks);
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

    updateIrq(v9990);
}
