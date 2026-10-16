/*****************************************************************************
**
** Yamaha V9990 command engine.
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
#ifndef V9990CMD_H
#define V9990CMD_H

#include "V9990.h"

/* P#5 bits owned by the command engine. */
#define V9990_CMD_TR 0x80
#define V9990_CMD_BD 0x10
#define V9990_CMD_CE 0x01

typedef struct V9990Cmd V9990Cmd;

/* Called when a command ends, STOP included. */
typedef void (*V9990CmdEndCallback)(void* ref);

V9990Cmd* v9990CmdCreate(UInt8* vram, V9990CmdEndCallback onEnd, void* ref);
void      v9990CmdDestroy(V9990Cmd* cmd);
void      v9990CmdReset(V9990Cmd* cmd, UInt32 time);

/* Runs the command in progress up to time. Every access that can observe
** the engine or VRAM calls this first. */
void      v9990CmdSync(V9990Cmd* cmd, UInt32 time);

/* Timing slots: the display shown with cursors or sprites, shown without
** them, and blanking or display off. The engine is synced before a change. */
#define V9990_CMD_SLOT_SPRITES 0
#define V9990_CMD_SLOT_DISPLAY 1
#define V9990_CMD_SLOT_BLANK   2

void      v9990CmdSetTiming(V9990Cmd* cmd, int slot);

/* Active: CE is set. Running: active and not waiting for the CPU. */
int       v9990CmdIsActive(V9990Cmd* cmd);
int       v9990CmdIsRunning(V9990Cmd* cmd);

/* The running command ends at a time that depends on what VRAM holds. */
int       v9990CmdReadsVram(V9990Cmd* cmd);

/* How far the engine has run; half (may be NULL) is 1 for one more UC. */
UInt32    v9990CmdTime(V9990Cmd* cmd, int* half);

/* Units the running command still takes and the UC per unit in each slot;
** 0 when it takes no time. SRCH holds while VRAM does not change. */
UInt32    v9990CmdUnitsLeft(V9990Cmd* cmd, UInt32 costs[3]);

/* reg is R#32-R#51. */
void      v9990CmdWriteRegister(V9990Cmd* cmd, int reg, UInt8 value, UInt32 time);

/* An R#52 write. mode and r6 are the display mode and R#6 at the write;
** they fix the colour depth, the image width and the VRAM mapping. */
void      v9990CmdStart(V9990Cmd* cmd, UInt8 opcode, V9990Mode mode, UInt8 r6, UInt32 time);

UInt8     v9990CmdGetStatus(V9990Cmd* cmd, UInt32 time);
UInt16    v9990CmdGetBorderX(V9990Cmd* cmd, UInt32 time);
void      v9990CmdWriteData(V9990Cmd* cmd, UInt8 value, UInt32 time);
UInt8     v9990CmdReadData(V9990Cmd* cmd, UInt32 time);

/* The engine as of its last sync, for the debugger: these change nothing.
** PeekData is the byte P#2 would return. */
UInt8     v9990CmdPeekStatus(V9990Cmd* cmd);
UInt16    v9990CmdPeekBorderX(V9990Cmd* cmd);
UInt8     v9990CmdPeekData(V9990Cmd* cmd);

void      v9990CmdSaveState(V9990Cmd* cmd);
void      v9990CmdLoadState(V9990Cmd* cmd, UInt32 time);

#endif
