/*****************************************************************************
**
** Registers and ports of the V9990.
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
#include "RegTables.h"

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

static const RegField r2[] = { { 7, 1, "WIN", "Do not advance the write address" }, { 0, 3, "A18-16", "Address bits 18-16" } };
static const RegField r5[] = { { 7, 1, "RIN", "Do not advance the read address" }, { 0, 3, "A18-16", "Address bits 18-16" } };
static const RegField r6[] = {
    { 6, 2, "DSPM", "Display mode: 0 P1, 1 P2, 2 bitmap, 3 stand-by" },
    { 4, 2, "DCKM", "Dot clock: 0 1/4, 1 1/2, 2 1/1 of the master clock" },
    { 2, 2, "XIMM", "Image width: 256, 512, 1024 or 2048 dots" },
    { 0, 2, "CLRM", "Bits per dot: 2, 4, 8 or 16" },
};
static const RegField r7[] = {
    { 6, 1, "C25M", "25 MHz master clock (B5/B6)" },
    { 5, 1, "SM1",  "Scan mode bit 1" },
    { 4, 1, "SM",   "Scan mode: 525/625 lines" },
    { 3, 1, "PAL",  "PAL (50 Hz)" },
    { 2, 1, "EO",   "Even/odd lines in alternate fields" },
    { 1, 1, "IL",   "Interlace" },
    { 0, 1, "HSCN", "High scan rate (B5/B6)" },
};
static const RegField r8[] = {
    { 7, 1, "DISP", "Display on" },
    { 6, 1, "SPD",  "Sprites and cursors off" },
    { 5, 1, "YSE",  "YS output enabled" },
    { 4, 1, "VWTE", "VRAM serial write" },
    { 3, 1, "VWM",  "VRAM write mode" },
    { 2, 1, "DMAE", "DMA enabled" },
    { 0, 2, "VSL",  "VRAM size select" },
};
static const RegField r9[] = {
    { 2, 1, "IECE", "Command end interrupt enable" },
    { 1, 1, "IEH",  "Line interrupt enable" },
    { 0, 1, "IEV",  "Frame interrupt enable" },
};
static const RegField r11[] = { { 7, 1, "IEHM", "Line interrupt on every line" }, { 0, 2, "IL9-8", "Interrupt line bits 9-8" } };
static const RegField r12[] = { { 0, 4, "IX", "Interrupt dot position (x64)" } };
static const RegField r13[] = {
    { 6, 2, "PLTM", "Colour of 8-bit dots: 0 palette, 1 BD8, 2 YJK, 3 YUV" },
    { 5, 1, "YAE",  "YJK/YUV with palette colours" },
    { 4, 1, "PLTAIH", "Palette pointer does not advance" },
    { 0, 4, "PLTO", "Palette offset (x4)" },
};
static const RegField r14[] = { { 2, 6, "PLTA", "Palette entry" }, { 0, 2, "RGB", "Component: 0 R, 1 G, 2 B" } };
static const RegField r15[] = { { 0, 6, "BDC", "Backdrop palette entry" } };
static const RegField r16[] = {
    { 4, 4, "V", "Vertical adjust (signed)" },
    { 0, 4, "H", "Horizontal adjust (signed)" },
};
static const RegField r18[] = { { 6, 2, "R512/R256", "Vertical roll at 512 or 256 lines" }, { 0, 5, "SCAY12-8", "Layer A Y scroll bits 12-8" } };
static const RegField r19[] = { { 0, 3, "SCAX2-0", "Layer A X scroll bits 2-0" } };
static const RegField r22[] = { { 6, 2, "SDB", "Layer B roll" }, { 0, 1, "SCBY8", "Layer B Y scroll bit 8" } };
static const RegField r23[] = { { 0, 3, "SCBX2-0", "Layer B X scroll bits 2-0" } };
static const RegField r24[] = { { 0, 6, "SCBX8-3", "Layer B X scroll bits 8-3" } };
static const RegField r25[] = { { 0, 4, "SGBA", "Sprite pattern base (P1/P2)" } };
static const RegField r27[] = { { 2, 2, "PRY", "Layer priority boundary Y" }, { 0, 2, "PRX", "Layer priority boundary X" } };
static const RegField r28[] = { { 0, 4, "SCOL", "Cursor colours: palette offset (x4)" } };
static const RegField r44[] = {
    { 3, 1, "DIY", "Y goes up" },
    { 2, 1, "DIX", "X goes left" },
    { 1, 1, "NEQ", "SRCH stops on another colour" },
    { 0, 1, "MAJ", "LINE: Y is the long side" },
};
static const RegField r45[] = { { 4, 1, "TP", "Transparent: colour 0 is not written" }, { 0, 4, "LO", "Logical operation" } };
static const RegField r52[] = { { 4, 4, "OP", "Command" } };

static const RegDesc regs[] = {
    { 0,  "VRAM write address 0", 0, 0 },
    { 1,  "VRAM write address 1", 0, 0 },
    { 2,  "VRAM write address 2", r2,  N(r2) },
    { 3,  "VRAM read address 0",  0, 0 },
    { 4,  "VRAM read address 1",  0, 0 },
    { 5,  "VRAM read address 2",  r5,  N(r5) },
    { 6,  "Screen mode 0",        r6,  N(r6) },
    { 7,  "Screen mode 1",        r7,  N(r7) },
    { 8,  "Control",              r8,  N(r8) },
    { 9,  "Interrupt 0",          r9,  N(r9) },
    { 10, "Interrupt 1 (line)",   0, 0 },
    { 11, "Interrupt 2",          r11, N(r11) },
    { 12, "Interrupt 3",          r12, N(r12) },
    { 13, "Palette control",      r13, N(r13) },
    { 14, "Palette pointer",      r14, N(r14) },
    { 15, "Backdrop colour",      r15, N(r15) },
    { 16, "Display adjust",       r16, N(r16) },
    { 17, "Scroll A Y low",       0, 0 },
    { 18, "Scroll A Y high",      r18, N(r18) },
    { 19, "Scroll A X low",       r19, N(r19) },
    { 20, "Scroll A X high",      0, 0 },
    { 21, "Scroll B Y low",       0, 0 },
    { 22, "Scroll B Y high",      r22, N(r22) },
    { 23, "Scroll B X low",       r23, N(r23) },
    { 24, "Scroll B X high",      r24, N(r24) },
    { 25, "Sprite pattern table", r25, N(r25) },
    { 26, "LCD control",          0, 0 },
    { 27, "Priority control",     r27, N(r27) },
    { 28, "Sprite palette",       r28, N(r28) },
    { 29, "Not used",             0, 0 },
    { 30, "Not used",             0, 0 },
    { 31, "Not used",             0, 0 },
    { 32, "SX low",               0, 0 },
    { 33, "SX high",              0, 0 },
    { 34, "SY low",               0, 0 },
    { 35, "SY high",              0, 0 },
    { 36, "DX low",               0, 0 },
    { 37, "DX high",              0, 0 },
    { 38, "DY low",               0, 0 },
    { 39, "DY high",              0, 0 },
    { 40, "NX low",               0, 0 },
    { 41, "NX high",              0, 0 },
    { 42, "NY low",               0, 0 },
    { 43, "NY high",              0, 0 },
    { 44, "Argument",             r44, N(r44) },
    { 45, "Logical operation",    r45, N(r45) },
    { 46, "Write mask low",       0, 0 },
    { 47, "Write mask high",      0, 0 },
    { 48, "Font colour FC low",   0, 0 },
    { 49, "Font colour FC high",  0, 0 },
    { 50, "Font colour BC low",   0, 0 },
    { 51, "Font colour BC high",  0, 0 },
    { 52, "Command",              r52, N(r52) },
    { 53, "Border X low",         0, 0 },
    { 54, "Border X high",        0, 0 },
    { 55, "Unknown",              0, 0 },
    { 56, "Unknown",              0, 0 },
    { 57, "Unknown",              0, 0 },
    { 58, "Unknown",              0, 0 },
    { 59, "Unknown",              0, 0 },
    { 60, "Unknown",              0, 0 },
    { 61, "Unknown",              0, 0 },
    { 62, "Unknown",              0, 0 },
    { 63, "Unknown",              0, 0 },
};

static const RegField p4[] = {
    { 7, 1, "WII", "Do not advance after a write" },
    { 6, 1, "RII", "Do not advance after a read" },
    { 0, 6, "RS",  "Register selected" },
};
static const RegField p5[] = {
    { 7, 1, "TR",  "Ready to transfer" },
    { 6, 1, "VR",  "Vertical retrace" },
    { 5, 1, "HR",  "Horizontal retrace" },
    { 4, 1, "BD",  "SRCH found the border colour" },
    { 2, 1, "MCS", "Master clock select" },
    { 1, 1, "EO",  "Second field" },
    { 0, 1, "CE",  "Command running" },
};
static const RegField p6[] = {
    { 2, 1, "CE", "Command end interrupt" },
    { 1, 1, "HI", "Line interrupt" },
    { 0, 1, "VI", "Frame interrupt" },
};
static const RegField p7[] = {
    { 1, 1, "SRS", "System reset held" },
    { 0, 1, "MCS", "Master clock select" },
};

static const RegDesc ports[] = {
    { 0,  "VRAM data",          0, 0 },
    { 1,  "Palette data",       0, 0 },
    { 2,  "Command data",       0, 0 },
    { 3,  "Register data",      0, 0 },
    { 4,  "Register select",    p4, N(p4) },
    { 5,  "Status",             p5, N(p5) },
    { 6,  "Interrupt flags",    p6, N(p6) },
    { 7,  "System control",     p7, N(p7) },
};

const RegBank regBankV9990      = { "v9990regs",  "R#", regs,  N(regs) };
const RegBank regBankV9990Ports = { "v9990ports", "P#", ports, N(ports) };
