/*****************************************************************************
**
** CPU register access.
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
#include "ControlCpu.h"
#include "ControlView.h"
#include "R800Debug.h"
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>

/* A 16 bit CPU register by name in any case, from a snapshot the caller took, or -1. */
static Int32 ctrlCpuRegisterIn(DbgSnapshot* snapshot, const char* name);

DbgRegisterBank* ctrlCpuRegisterBankIn(DbgSnapshot* snapshot)
{
    const DbgDevice* device = dbgSnapshotFindDeviceWithRegisters(snapshot, DBGTYPE_CPU);
    return device != NULL ? device->registerBank[0] : NULL;
}

Int32 ctrlCpuPc(void)
{
    return ctrlCpuRegister("PC");
}

static int sameName(const char* have, const char* want)
{
    while (*have != 0 && *want != 0 &&
           tolower((unsigned char)*have) == tolower((unsigned char)*want))
    {
        have++;
        want++;
    }
    return *have == 0 && *want == 0;
}

static int registerIndex(const DbgRegisterBank* regs, const char* name)
{
    UInt32 i;

    if (regs == NULL) {
        return -1;
    }
    for (i = 0; i < regs->count; i++) {
        if (sameName(regs->reg[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static Int32 ctrlCpuRegisterIn(DbgSnapshot* snapshot, const char* name)
{
    const DbgRegisterBank* regs = ctrlCpuRegisterBankIn(snapshot);
    int index = registerIndex(regs, name);

    if (index < 0) {
        return -1;
    }
    return (Int32)(regs->reg[index].value & 0xffff);
}

Int32 ctrlCpuRegister(const char* name)
{
    DbgSnapshot* snapshot = ctrlViewDevices();
    Int32 value = -1;

    if (snapshot != NULL) {
        value = ctrlCpuRegisterIn(snapshot, name);
        dbgSnapshotDestroy(snapshot);
    }
    return value;
}

int ctrlCpuBreakpointAt(Int32 pc)
{
    return pc >= 0 && (dbgHasBreakpoint((UInt16)pc) ||
                       (dbgSegmentBreakpointCount() > 0 && dbgBreakpointAllows((UInt16)pc)));
}

int ctrlCpuStopsAtOnce(Int32 pc)
{
    return ctrlCpuBreakpointAt(pc) && !r800DebugWaitsInHalt();
}
