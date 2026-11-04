/*****************************************************************************
**
** Registers of the TMS99xx, V9938 and V9958.
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

static const RegField r0[] = {
    { 6, 1, "DG",  "Digitize" },
    { 5, 1, "IE2", "Light pen interrupt enable" },
    { 4, 1, "IE1", "Line (H-blank) interrupt enable" },
    { 3, 1, "M5",  "Mode bit M5" },
    { 2, 1, "M4",  "Mode bit M4" },
    { 1, 1, "M3",  "Mode bit M3" },
    { 0, 1, "EV",  "External video" },
};
static const RegField r1[] = {
    { 7, 1, "4/16K", "TMS: 16K VRAM addressing" },
    { 6, 1, "BL",  "Display on" },
    { 5, 1, "IE0", "Frame (V-blank) interrupt enable" },
    { 4, 1, "M1",  "Mode bit M1 (text)" },
    { 3, 1, "M2",  "Mode bit M2 (multicolour)" },
    { 2, 1, "LB",  "Line blink (blueMSX+ extension)" },
    { 1, 1, "SI",  "16x16 sprites" },
    { 0, 1, "MAG", "Sprites magnified 2x" },
};
static const RegField r7[] = {
    { 4, 4, "TC", "Text colour (SCREEN 0)" },
    { 0, 4, "BD", "Backdrop colour" },
};
static const RegField r8[] = {
    { 7, 1, "MS",  "Mouse" },
    { 6, 1, "LP",  "Light pen" },
    { 5, 1, "TP",  "Colour 0 is solid, except in text and multicolour modes" },
    { 4, 1, "CB",  "Colour bus input" },
    { 3, 1, "VR",  "64K VRAM chips" },
    { 1, 1, "SPD", "Sprites disabled" },
    { 0, 1, "BW",  "Black and white" },
};
static const RegField r9[] = {
    { 7, 1, "LN", "212 lines (else 192)" },
    { 5, 1, "S1", "Simultaneous mode bit 1" },
    { 4, 1, "S0", "Simultaneous mode bit 0" },
    { 3, 1, "IL", "Interlace" },
    { 2, 1, "EO", "Alternate even/odd pages" },
    { 1, 1, "NT", "PAL (50 Hz)" },
    { 0, 1, "DC", "Dot clock input" },
};
static const RegField r12[] = {
    { 4, 4, "T2", "Blink text colour" },
    { 0, 4, "BC", "Blink background colour" },
};
static const RegField r13[] = {
    { 4, 4, "ON",  "Frames the blink colour shows (x10)" },
    { 0, 4, "OFF", "Frames the normal colour shows (x10)" },
};
static const RegField r14[] = { { 0, 3, "A16-14", "VRAM access address bits 16-14" } };
static const RegField r15[] = { { 0, 4, "S#", "Status register a read returns" } };
static const RegField r16[] = { { 0, 4, "P#", "Palette entry the next write sets" } };
static const RegField r17[] = {
    { 7, 1, "AII", "Do not advance the register number" },
    { 0, 6, "RS",  "Register written through port 3" },
};
static const RegField r18[] = {
    { 4, 4, "V", "Vertical adjust (signed, 7 up to -8 down)" },
    { 0, 4, "H", "Horizontal adjust (signed, 7 left to -8 right)" },
};
static const RegField r25[] = {
    { 6, 1, "CMD", "Commands in every screen mode" },
    { 5, 1, "VDS", "Vertical sync output on pin 8" },
    { 4, 1, "YAE", "YJK with palette colours (SCREEN 10/11)" },
    { 3, 1, "YJK", "YJK colours (SCREEN 10-12)" },
    { 2, 1, "WTE", "Wait function" },
    { 1, 1, "MSK", "Mask the 8 leftmost pixels" },
    { 0, 1, "SP2", "Two horizontal scroll pages" },
};
static const RegField r26[] = { { 0, 6, "HO", "Horizontal scroll in units of 8 pixels" } };
static const RegField r27[] = { { 0, 3, "HO", "Horizontal scroll, pixels to the left" } };
static const RegField r45[] = {
    { 6, 1, "MXC", "CPU reads/writes the expansion RAM" },
    { 5, 1, "MXD", "Destination in the expansion RAM" },
    { 4, 1, "MXS", "Source in the expansion RAM" },
    { 3, 1, "DIY", "Y goes up" },
    { 2, 1, "DIX", "X goes left" },
    { 1, 1, "EQ",  "SRCH stops on the colour (else on another)" },
    { 0, 1, "MAJ", "LINE: Y is the long side" },
};
static const RegField r46[] = {
    { 4, 4, "CM", "Command" },
    { 0, 4, "LO", "Logical operation" },
};

static const RegDesc regs[] = {
    { 0,  "Mode 0",            r0,  N(r0) },
    { 1,  "Mode 1",            r1,  N(r1) },
    { 2,  "Name table",        0, 0 },
    { 3,  "Colour table low",  0, 0 },
    { 4,  "Pattern generator", 0, 0 },
    { 5,  "Sprite attr low",   0, 0 },
    { 6,  "Sprite pattern",    0, 0 },
    { 7,  "Text / backdrop",   r7,  N(r7) },
    { 8,  "Mode 2",            r8,  N(r8) },
    { 9,  "Mode 3",            r9,  N(r9) },
    { 10, "Colour table high", 0, 0 },
    { 11, "Sprite attr high",  0, 0 },
    { 12, "Text blink colour", r12, N(r12) },
    { 13, "Blink period",      r13, N(r13) },
    { 14, "VRAM access base",  r14, N(r14) },
    { 15, "Status pointer",    r15, N(r15) },
    { 16, "Palette pointer",   r16, N(r16) },
    { 17, "Register pointer",  r17, N(r17) },
    { 18, "Display adjust",    r18, N(r18) },
    { 19, "Line interrupt",    0, 0 },
    { 20, "Colour burst 1",    0, 0 },
    { 21, "Colour burst 2",    0, 0 },
    { 22, "Colour burst 3",    0, 0 },
    { 23, "Vertical scroll",   0, 0 },
    { 25, "Mode 4",            r25, N(r25) },
    { 26, "H scroll high",     r26, N(r26) },
    { 27, "H scroll low",      r27, N(r27) },
    { 32, "SX low",  0, 0 },
    { 33, "SX high", 0, 0 },
    { 34, "SY low",  0, 0 },
    { 35, "SY high", 0, 0 },
    { 36, "DX low",  0, 0 },
    { 37, "DX high", 0, 0 },
    { 38, "DY low",  0, 0 },
    { 39, "DY high", 0, 0 },
    { 40, "NX low",  0, 0 },
    { 41, "NX high", 0, 0 },
    { 42, "NY low",  0, 0 },
    { 43, "NY high", 0, 0 },
    { 44, "Colour (CLR)", 0, 0 },
    { 45, "Argument",      r45, N(r45) },
    { 46, "Command",       r46, N(r46) },
};

static const RegField s0[] = {
    { 7, 1, "F",   "Frame interrupt pending" },
    { 6, 1, "5S",  "Fifth (ninth) sprite on a line" },
    { 5, 1, "C",   "Sprites collided" },
    { 0, 5, "5th", "Number of the fifth (ninth) sprite" },
};
static const RegField s1[] = {
    { 7, 1, "FL",  "Light pen / mouse button" },
    { 6, 1, "LPS", "Light pen switch" },
    { 1, 5, "ID",  "VDP identification" },
    { 0, 1, "FH",  "Line interrupt pending" },
};
static const RegField s2[] = {
    { 7, 1, "TR", "Ready to transfer a byte" },
    { 6, 1, "VR", "Vertical retrace" },
    { 5, 1, "HR", "Horizontal retrace" },
    { 4, 1, "BD", "SRCH found the border colour" },
    { 1, 1, "EO", "Second field" },
    { 0, 1, "CE", "Command running" },
};

static const RegDesc status[] = {
    { 0, "Status", s0, N(s0) },
    { 1, "Status", s1, N(s1) },
    { 2, "Status", s2, N(s2) },
    { 3, "Collision X low",  0, 0 },
    { 4, "Collision X high", 0, 0 },
    { 5, "Collision Y low",  0, 0 },
    { 6, "Collision Y high", 0, 0 },
    { 7, "Colour (CL)",      0, 0 },
    { 8, "Border X low",     0, 0 },
    { 9, "Border X high",    0, 0 },
};

const RegBank regBankV99x8       = { "v99x8regs", "R#", regs,   N(regs) };
const RegBank regBankV99x8Status = { "v99x8stat", "S#", status, N(status) };
