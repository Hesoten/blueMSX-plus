/*****************************************************************************
**
** Step over for call and rst instructions.
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
#include "ControlStep.h"
#include "ControlCpu.h"
#include "ControlMemory.h"
#include "Board.h"
#include "Debugger.h"
#include "R800Dasm.h"
#include <string.h>

/* Bytes the MSX BIOS routine of an rst reads after it and returns past: SYNCHR (RST 08h)
** one character, CALLF (RST 30h) a slot and an address. Other code in page 0 may not, and
** a Forte II board runs its game ROM there, not a BIOS. */
static int inlineBytesAfterRst(UInt8 opcode)
{
    if (boardGetType() != BOARD_MSX) {
        return 0;
    }
    return opcode == 0xCF ? 1 : opcode == 0xF7 ? 3 : 0;
}

/* For a call or rst: 0, with in stopAt where it may return (two after an rst whose BIOS
** routine skips inline bytes). Else 1: a plain step does it, once the view is let go. */
int ctrlStepOverStops(CtrlView* view, UInt16* stopAt, int* stops)
{
    UInt8    window[R800_DASM_MAX_LEN];
    char     mnemonic[R800_DASM_TEXT_MAX];
    CtrlAddr at;
    Int32    pc = ctrlCpuPc();
    int      length;
    int      i;

    if (pc < 0) {
        return 1;
    }

    memset(&at, 0, sizeof(at));
    at.space  = CTRL_SPACE_CPU;
    at.offset = (UInt32)pc;
    for (i = 0; i < R800_DASM_MAX_LEN; i++) {
        CtrlAddr byteAt = at;
        byteAt.offset = (UInt32)((pc + i) & 0xffff);
        if (ctrlMemPeek(view, &byteAt, &window[i], 1) != CTRL_MEM_OK) {
            return 1;
        }
    }

    length = r800DasmBuf(window, R800_DASM_MAX_LEN, (UInt16)pc, NULL, NULL,
                         mnemonic, sizeof(mnemonic));
    /* Only call and rst leave a return address; jumps are plain steps. */
    if (length <= 0 || (strncmp(mnemonic, "call", 4) != 0 && strncmp(mnemonic, "rst", 3) != 0)) {
        return 1;
    }

    stopAt[0] = (UInt16)((pc + length) & 0xffff);
    *stops = 1;
    if (inlineBytesAfterRst(window[0]) > 0) {
        stopAt[1] = (UInt16)((pc + length + inlineBytesAfterRst(window[0])) & 0xffff);
        *stops = 2;
    }
    return 0;
}

/* A temporary breakpoint at each stop that has none yet; hadBreakpoint says which had. */
void ctrlStepSetStops(const UInt16* stopAt, int stops, int* hadBreakpoint)
{
    int i;

    for (i = 0; i < stops; i++) {
        hadBreakpoint[i] = dbgHasBreakpoint(stopAt[i]);
        if (!hadBreakpoint[i]) {
            dbgSetKeptBreakpoint(stopAt[i]);
        }
    }
}

void ctrlStepClearStops(const UInt16* stopAt, int stops, const int* hadBreakpoint)
{
    int i;

    for (i = 0; i < stops; i++) {
        if (!hadBreakpoint[i]) {
            dbgClearKeptBreakpoint(stopAt[i]);
        }
    }
}
