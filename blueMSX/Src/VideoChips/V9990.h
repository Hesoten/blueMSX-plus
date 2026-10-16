/*****************************************************************************
**
** Yamaha V9990 video chip.
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
#ifndef V9990_H
#define V9990_H

#include "MsxTypes.h"

#define V9990_VRAM_SIZE 0x80000
#define V9990_VRAM_MASK (V9990_VRAM_SIZE - 1)

typedef enum {
    V9990_MODE_P1,
    V9990_MODE_P2,
    V9990_MODE_B0,
    V9990_MODE_B1,
    V9990_MODE_B2,
    V9990_MODE_B3,
    V9990_MODE_B4,
    V9990_MODE_B5,
    V9990_MODE_B6,
    V9990_MODE_B7,
    V9990_MODE_STANDBY
} V9990Mode;

typedef struct V9990 V9990;

/* Creates the chip in its power-on state with its timers running. */
V9990* v9990Create(void);
void   v9990Destroy(V9990* v9990);
void   v9990Reset(V9990* v9990);
void   v9990SaveState(V9990* v9990);
void   v9990LoadState(V9990* v9990);

/* port is the low nibble of the I/O address (P#0-P#F). */
UInt8  v9990ReadPort(V9990* v9990, UInt16 port);
void   v9990WritePort(V9990* v9990, UInt16 port, UInt8 value);

/* Turns a logical VRAM address, as the CPU or a command sees it in the
** given display mode, into an index into the physical 512 KB array. */
UInt32 v9990MapAddress(V9990Mode mode, UInt32 address);

#endif
