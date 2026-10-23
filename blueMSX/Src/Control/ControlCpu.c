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
#include "SlotManager.h"
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

/* The CPU bank publishes only pairs; these halves are mapped onto them. */
static const struct { const char* name; const char* pair; int high; } regHalf[] = {
    { "A",   "AF", 1 }, { "F",   "AF", 0 },
    { "B",   "BC", 1 }, { "C",   "BC", 0 },
    { "D",   "DE", 1 }, { "E",   "DE", 0 },
    { "H",   "HL", 1 }, { "L",   "HL", 0 },
    { "IXH", "IX", 1 }, { "IXL", "IX", 0 },
    { "IYH", "IY", 1 }, { "IYL", "IY", 0 },
    { NULL, NULL, 0 }
};

/* Clock and count entries: the core accepts writes to them and does nothing. */
static int isDerived(const char* name)
{
    return sameName(name, "CLKH") || sameName(name, "CLKL") ||
           sameName(name, "CNT");
}

/* The index into regHalf of an 8 bit half's name, or -1. */
static int halfIndex(const char* name)
{
    int i;

    for (i = 0; regHalf[i].name != NULL; i++) {
        if (sameName(regHalf[i].name, name)) {
            return i;
        }
    }
    return -1;
}

/* The names the CPU bank publishes, as the refusal below lists them. */
static const char* const bankNames[] = {
    "AF", "BC", "DE", "HL", "AF1", "BC1", "DE1", "HL1", "IX", "IY", "SP", "PC",
    "I", "R", "IM", "IFF1", "IFF2", NULL
};

static void noSuchRegister(const char* name, char* err, int errLen)
{
    snprintf(err, (size_t)errLen,
             "this CPU has no register called %s. It has AF BC DE HL, the "
             "alternates AF1 BC1 DE1 HL1, IX IY SP PC I R IM IFF1 IFF2, and "
             "the halves A F B C D E H L IXH IXL IYH IYL",
             name);
}

int ctrlCpuCheckName(const char* name, char* err, int errLen)
{
    int i;

    if (isDerived(name)) {
        snprintf(err, (size_t)errLen,
                 "%s is the clock, not a register the CPU reads; it cannot be set",
                 name);
        return 0;
    }
    if (halfIndex(name) >= 0) {
        return 1;
    }
    for (i = 0; bankNames[i] != NULL; i++) {
        if (sameName(bankNames[i], name)) {
            return 1;
        }
    }
    noSuchRegister(name, err, errLen);
    return 0;
}

/* Fills err and returns 0 when value is out of range for the register. */
static int valueFits(const char* name, const char* target, int width, UInt32 value,
                     char* err, int errLen)
{
    if (value > (width == 16 ? 0xffffu : 0xffu)) {
        snprintf(err, (size_t)errLen,
                 "%s holds %d bits, so 0x%X does not fit", name, width,
                 (unsigned)value);
        return 0;
    }

    /* The core clamps IM, IFF1 and IFF2 to 2 instead of refusing. */
    if (value > 2 && sameName(target, "IM")) {
        snprintf(err, (size_t)errLen, "%s is 0, 1 or 2", name);
        return 0;
    }
    if (value > 1 && (sameName(target, "IFF1") || sameName(target, "IFF2"))) {
        snprintf(err, (size_t)errLen, "%s is one bit: 0 or 1", name);
        return 0;
    }
    return 1;
}

static int setInBank(DbgRegisterBank* regs, const char* name, int half, UInt32 value,
                     Int32* previous, char* err, int errLen)
{
    const char* target = half >= 0 ? regHalf[half].pair : name;
    int         index  = registerIndex(regs, target);
    int         width;
    UInt32      held;
    UInt32      toWrite = value;

    if (index < 0) {
        noSuchRegister(name, err, errLen);
        return 0;
    }

    width = half >= 0 ? 8 : (regs->reg[index].width > 8 ? 16 : 8);
    if (!valueFits(name, target, width, value, err, errLen)) {
        return 0;
    }

    held = regs->reg[index].value & 0xffff;
    if (half >= 0) {
        int high = regHalf[half].high;

        if (previous != NULL) {
            *previous = (Int32)(high ? held >> 8 : held & 0xff);
        }
        toWrite = high ? ((value & 0xff) << 8) | (held & 0x00ff)
                       : (held & 0xff00) | (value & 0xff);
    }
    else if (previous != NULL) {
        *previous = (Int32)held;
    }

    if (!dbgDeviceWriteRegister(regs, index, toWrite)) {
        snprintf(err, (size_t)errLen, "this CPU refused to set %s", name);
        return 0;
    }
    return 1;
}

int ctrlCpuSetRegister(const char* name, UInt32 value, Int32* previous,
                       char* err, int errLen)
{
    DbgSnapshot* snapshot;
    int          ok;

    if (!ctrlCpuCheckName(name, err, errLen)) {
        return 0;
    }

    snapshot = ctrlViewDevices();
    if (snapshot == NULL) {
        snprintf(err, (size_t)errLen, "set %s without a view of the paused machine", name);
        return -1;
    }

    ok = setInBank(ctrlCpuRegisterBankIn(snapshot), name, halfIndex(name), value,
                   previous, err, errLen);
    dbgSnapshotDestroy(snapshot);
    return ok;
}

/* The first even step from scan holding word, or 0x10000. Stops at 0xFFFE so a+1 fits. */
static UInt32 findWord(const UInt8* mem, UInt32 scan, UInt16 word)
{
    UInt32 a;

    for (a = scan; a <= 0xFFFE; a += 2) {
        if ((mem[a] | (mem[a + 1] << 8)) == word) {
            return a;
        }
    }
    return 0x10000;
}

static int callstackIn(DbgSnapshot* snapshot, UInt16* out, UInt16* where, int max,
                       int* notFound, int* innerNotFound)
{
    static UInt8 stackMem[0x10000];
    /* The CPU device that publishes the registers also keeps the call log. */
    const DbgDevice* cpu = dbgSnapshotFindDeviceWithRegisters(snapshot, DBGTYPE_CPU);
    const DbgCallstack* stack = cpu != NULL ? cpu->callstack : NULL;
    Int32  sp = ctrlCpuRegisterIn(snapshot, "SP");
    UInt32 scan;
    UInt32 a;
    int written = 0;
    Int32 i;

    *notFound = 0;
    *innerNotFound = 0;
    if (stack == NULL || stack->size == 0 || sp < 0) {
        return 0;
    }
    for (a = (UInt32)sp; a <= 0xFFFF; a++) {
        stackMem[a] = slotPeek(NULL, (UInt16)a);
    }
    /* The log keeps calls the program abandoned, so an entry counts only when its
    ** word is on the stack, each outer one above the last. */
    scan = (UInt32)sp;
    for (i = (Int32)stack->size - 1; i >= 0 && written < max; i--) {
        UInt16 frame = (UInt16)stack->callstack[i];

        a = findWord(stackMem, scan, frame);
        /* A stale entry can match high up and hide the real frames below it, so
        ** the next outer entry found lower wins. */
        if (a <= 0xFFFE && i > 0 &&
            findWord(stackMem, scan, (UInt16)stack->callstack[i - 1]) < a)
        {
            a = 0x10000;
        }
        if (a > 0xFFFE) {
            (*notFound)++;
            if (written == 0) {
                (*innerNotFound)++;
            }
            continue;
        }
        out[written] = frame;
        where[written] = (UInt16)a;
        written++;
        scan = a + 2;
    }
    return written;
}

int ctrlCpuCallstack(UInt16* out, UInt16* where, int max, int* notFound, int* innerNotFound)
{
    DbgSnapshot* snapshot = ctrlViewDevices();
    int written = -1;

    if (snapshot != NULL) {
        written = callstackIn(snapshot, out, where, max, notFound, innerNotFound);
        dbgSnapshotDestroy(snapshot);
    }
    return written;
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
