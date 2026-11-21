/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Z80/R800Debug.c,v $
**
** $Revision: 1.9 $
**
** $Date: 2008-04-18 04:09:54 $
**
** Author: Daniel Vik
**
** Description: Debugger support for an R800 object
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
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
#include "R800Debug.h"
#include "SlotManager.h"
#include "DebugDeviceManager.h"
#include "DebugTrace.h"
#include "Language.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "Board.h"
#include "Emulator.h"


extern void debuggerTrace(const char* str);
extern void archTrap(UInt8 value);

struct R800Debug {
    int debugHandle;
    R800* r800;
    R800ReadCb readMemory;      /* the CPU's own reads, which a read watch wraps */
    R800ReadCb readIoPort;
    R800WriteCb writeIoPort;    /* the CPU's own OUT, which the io trace wraps */
    const UInt8* memBits;       /* the READ watch bits, which the memory wrapper tests */
    UInt16 ioSource;
};

static R800Debug* dbg;
/* Set while a breakpoint stop parks here, before the instruction at PC runs. */
static int stoppedBeforeInstruction;
static int pagesTracked;


static void getDebugInfo(R800Debug* dbg, DbgDevice* dbgDevice)
{
    static UInt8 mappedRAM[0x10000];
    DbgRegisterBank* regBank;
    int freqAdjust;
    int i;

    for (i = 0; i < 0x10000; i++) {
        mappedRAM[i] = slotPeek(NULL, i);
    }

    dbgDeviceAddMemoryBlock(dbgDevice, langDbgMemVisible(), 0, 0, 0x10000, mappedRAM);

#ifdef ENABLE_CALLSTACK
    if (dbg->r800->callstackSize > 255) {
        static UInt16 callstack[0x100];
        int beginning = dbg->r800->callstackSize & 0xff;
        int reminder = 256 - beginning;
        memcpy(callstack, dbg->r800->callstack + beginning, reminder * sizeof(UInt16));
        memcpy(callstack + reminder, dbg->r800->callstack, beginning * sizeof(UInt16));
        dbgDeviceAddCallstack(dbgDevice, langDbgCallstack(), callstack, 256);
    }
    else {
        dbgDeviceAddCallstack(dbgDevice, langDbgCallstack(), dbg->r800->callstack, dbg->r800->callstackSize);
    }
#endif

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegsCpu(), 20);

    dbgRegisterBankAddRegister(regBank,  0, "AF",  16, dbg->r800->regs.AF.W);
    dbgRegisterBankAddRegister(regBank,  1, "BC",  16, dbg->r800->regs.BC.W);
    dbgRegisterBankAddRegister(regBank,  2, "DE",  16, dbg->r800->regs.DE.W);
    dbgRegisterBankAddRegister(regBank,  3, "HL",  16, dbg->r800->regs.HL.W);
    dbgRegisterBankAddRegister(regBank,  4, "AF1", 16, dbg->r800->regs.AF1.W);
    dbgRegisterBankAddRegister(regBank,  5, "BC1", 16, dbg->r800->regs.BC1.W);
    dbgRegisterBankAddRegister(regBank,  6, "DE1", 16, dbg->r800->regs.DE1.W);
    dbgRegisterBankAddRegister(regBank,  7, "HL1", 16, dbg->r800->regs.HL1.W);
    dbgRegisterBankAddRegister(regBank,  8, "IX",  16, dbg->r800->regs.IX.W);
    dbgRegisterBankAddRegister(regBank,  9, "IY",  16, dbg->r800->regs.IY.W);
    dbgRegisterBankAddRegister(regBank, 10, "SP",  16, dbg->r800->regs.SP.W);
    dbgRegisterBankAddRegister(regBank, 11, "PC",  16, dbg->r800->regs.PC.W);
    dbgRegisterBankAddRegister(regBank, 12, "I",   8,  dbg->r800->regs.I);
    dbgRegisterBankAddRegister(regBank, 13, "R",   8,  dbg->r800->regs.R);
    dbgRegisterBankAddRegister(regBank, 14, "IM",  8,  dbg->r800->regs.im);
    dbgRegisterBankAddRegister(regBank, 15, "IFF1",8,  dbg->r800->regs.iff1);
    dbgRegisterBankAddRegister(regBank, 16, "IFF2",8,  dbg->r800->regs.iff2);
    
    switch (dbg->r800->cpuMode) {
    default:
    case CPU_Z80:
        freqAdjust = R800_MASTER_FREQUENCY / (dbg->r800->frequencyZ80 - 1);
        break;
    case CPU_R800:
        freqAdjust = R800_MASTER_FREQUENCY / (dbg->r800->frequencyR800 - 1);
        break;
    }

    dbgRegisterBankAddRegister(regBank, 17, "CLKH",16,  (UInt16)(dbg->r800->systemTime / freqAdjust / 0x10000));
    dbgRegisterBankAddRegister(regBank, 18, "CLKL",16,  (UInt16)(dbg->r800->systemTime / freqAdjust));
    dbgRegisterBankAddRegister(regBank, 19, "CNT",16,  (UInt16)dbg->r800->instCnt);
}


static int dbgWriteMemory(R800Debug* dbg, char* name, void* data, int start, int size)
{
    UInt8* dataBuffer = data;
    int i;
    int rv = 1;

    if (strcmp(name, langDbgMemVisible()) || start + size > 0x10000) {
        return 0;
    }

    for (i = 0; i < size; i++) {
        slotWrite(NULL, start + i, dataBuffer[i]);
        rv &= dataBuffer[i] == slotPeek(NULL, start + i);
    }

    return rv;
}

static int dbgWriteRegister(R800Debug* dbg, char* name, int regIndex, UInt32 value)
{
    switch (regIndex) {
    case  0: dbg->r800->regs.AF.W = (UInt16)value; break;
    case  1: dbg->r800->regs.BC.W = (UInt16)value; break;
    case  2: dbg->r800->regs.DE.W = (UInt16)value; break;
    case  3: dbg->r800->regs.HL.W = (UInt16)value; break;
    case  4: dbg->r800->regs.AF1.W = (UInt16)value; break;
    case  5: dbg->r800->regs.BC1.W = (UInt16)value; break;
    case  6: dbg->r800->regs.DE1.W = (UInt16)value; break;
    case  7: dbg->r800->regs.HL1.W = (UInt16)value; break;
    case  8: dbg->r800->regs.IX.W = (UInt16)value; break;
    case  9: dbg->r800->regs.IY.W = (UInt16)value; break;
    case 10: dbg->r800->regs.SP.W = (UInt16)value; break;
    /* A new PC leaves the HALT, so a breakpoint at the new PC can fire. */
    case 11:
        dbg->r800->regs.PC.W = (UInt16)value;
        dbg->r800->regs.halt = 0;
#if TIME_TRACE_SIZE > 0
        /* The flag is stored only at stops between instructions. */
        if (!dbg->r800->pcTraceParked) {
            dbg->r800->pcTraceFresh = 0;
        }
#endif
        r800TraceSetPc(dbg->r800);
        break;
    case 12: dbg->r800->regs.I = (UInt8)value; break;
    case 13: dbg->r800->regs.R = (UInt8)value; break;
    case 14: dbg->r800->regs.im = value > 2 ? 2 : (UInt8)value; break;
    case 15: dbg->r800->regs.iff1 = value > 2 ? 2 : (UInt8)value; break; 
    case 16: dbg->r800->regs.iff2 = value > 2 ? 2 : (UInt8)value; break; 
    }

    return 1;
}


/* The callbacks get the CPU's ref, which is NULL, so this reads the one CPU directly. */
static int cpuWaitsInHalt(void)
{
    return dbg != NULL && dbg->r800->regs.halt;
}

static void breakpointCb(R800Debug* dbg, UInt16 pc)
{
    /* A HALT runs again every cycle until an interrupt; break only on arriving at it. */
    if (cpuWaitsInHalt()) {
        return;
    }
    /* A step checks only the address it starts from, and stepping from a breakpoint runs it. */
    if (emulatorIsSingleStepping()) {
        return;
    }

    /* Segment breakpoints arm every address the segment could land at; a condition may hold off. */
    if (!dbgBreakpointDecide(pc, 1)) {
        return;
    }

    stoppedBeforeInstruction = 1;
    boardOnBreakpoint(DBG_STOP_BREAKPOINT);
    stoppedBeforeInstruction = 0;
}
  
UInt8 readMem(void* ref, int address) {
    if (address < 0x10000) {
        return slotPeek(NULL, (UInt16)address);
    }
    return 0xff;
}

static void watchpointMemCb(R800Debug* dbg, UInt16 address, UInt8 value) 
{
    tryWatchpoint(DBGTYPE_CPU, address, value, dbg, readMem); 
}

static void watchpointIoCb(R800Debug* dbg, UInt16 port, UInt8 value) 
{
    /* Only the low byte selects the port; the high one is whatever the CPU put
    ** on the bus (A for out (n),a, B for out (c),r). */
    tryWatchpoint(DBGTYPE_PORT, port & 0xff, value, dbg, NULL);
}

/* The start of the instruction running, which only r800Execute keeps in lastPC (the
** ExecuteUntil and ExecuteInstruction loops do not, and only the unused r800ExecuteTrace
** calls them); -1 if untraced. */
static int readerPc(void)
{
#if TIME_TRACE_SIZE > 0
    return dbg->r800->lastPC;
#else
    return -1;
#endif
}

/* Installed as the CPU's data read while a read watch is armed; fetches do not come here. */
static UInt8 readMemoryWatched(void* ref, UInt16 address)
{
    UInt8 value = dbg->readMemory(ref, address);

    if (dbg->memBits[address >> 3] >> (address & 7) & 1) {
        tryReadWatchpoint(DBGTYPE_CPU, address, value, ref, readMem, readerPc());
    }
    return value;
}

static UInt8 readIoPortWatched(void* ref, UInt16 port)
{
    UInt8 value = dbg->readIoPort(ref, port);

    tryReadWatchpoint(DBGTYPE_PORT, port & 0xff, value, ref, NULL, readerPc());
    return value;
}

/* A plain pointer store: what each wrapper reads is set at create, and the CPU takes one
** read or the other, so a running CPU may pick the change up at any access. */
static void armReadWatch(int memory, int ports)
{
    dbg->r800->readMemory = memory ? readMemoryWatched : dbg->readMemory;
    dbg->r800->readIoPort = ports ? readIoPortWatched : dbg->readIoPort;
}

#define IO_TRACE_OUT 1

static const DebugTraceField ioTraceOutFields[] = {
    { "port",  DEBUG_TRACE_WORD_A, 0,  8,  DEBUG_TRACE_HEX, 0, NULL, 0 },
    { "value", DEBUG_TRACE_WORD_A, 8,  8,  DEBUG_TRACE_HEX, 0, NULL, 0 },
    { "hi",    DEBUG_TRACE_WORD_A, 16, 8,  DEBUG_TRACE_HEX, 0, NULL, 0 },
    { "pc",    DEBUG_TRACE_WORD_B, 0,  16, DEBUG_TRACE_HEX, 0, NULL, 0 },
};

static const DebugTraceKindDesc ioTraceKinds[] = {
    { IO_TRACE_OUT, "out", ioTraceOutFields, 4 },
};

static const DebugTraceClassDesc ioTraceClass = { "io", ioTraceKinds, 1 };

/* Installed as the CPU's OUT while class io is armed. The record goes first, so it comes
** before what the device records at the same tick. */
static void writeIoPortTraced(void* ref, UInt16 port, UInt8 value)
{
    if (debugTracePorts[(port & 0xff) >> 3] >> (port & 7) & 1) {
        debugTraceEmit(dbg->ioSource, IO_TRACE_OUT, dbg->r800->systemTime, 0,
                       (port & 0xff) | (UInt32)value << 8 | (UInt32)(port & 0xff00) << 8,
                       (UInt32)readerPc());
    }
    dbg->writeIoPort(ref, port, value);
}

/* Under the trace's lock: a plain pointer store, as armReadWatch. */
static void armIoTrace(UInt32 armed)
{
    dbg->r800->writeIoPort = armed & DEBUG_TRACE_BIT(DEBUG_TRACE_IO) ? writeIoPortTraced
                                                                     : dbg->writeIoPort;
}

static void debugCb(R800Debug* dbg, int command, const char* data) 
{
    int slot, page, addr, rv;
    switch (command) {
    case ASDBG_TRACE:
        debuggerTrace(data);
        break;
    case ASDBG_SETBP:
        rv = sscanf(data, "%x %x %x", &slot, &page, &addr);
        if (rv == 3) {
            debuggerSetBreakpoint((UInt16)slot, (UInt16)page, (UInt16)addr);
        }
        break;
    }
}

void trapCb(R800* r800, UInt8 value)
{
    archTrap(value);
}

void r800DebugCreate(R800* r800)
{
    DebugCallbacks dbgCallbacks = { getDebugInfo, dbgWriteMemory, dbgWriteRegister, NULL };
    R800Debug* created = (R800Debug*)calloc(1, sizeof(R800Debug));

    created->r800        = r800;
    created->readMemory  = r800->readMemory;
    created->readIoPort  = r800->readIoPort;
    created->writeIoPort = r800->writeIoPort;
    created->memBits     = debugDeviceCpuReadBits();
    dbg = created;
    dbg->debugHandle = debugDeviceRegister(DBGTYPE_CPU, langDbgDevZ80(), &dbgCallbacks, dbg);

    r800->debugCb           = debugCb;
    r800->breakpointCb      = breakpointCb;
    r800->trapCb            = trapCb;
    r800->watchpointMemCb   = watchpointMemCb;
    r800->watchpointIoCb    = watchpointIoCb;
    /* Watchpoints outlive a machine, so the new CPU takes over the read ones. */
    debugDeviceSetReadWatchCb(armReadWatch);
    r800SetTraceSource(r800, debugTraceSourceRegister(DEBUG_TRACE_CPU, r800TraceClass(), "CPU"));
    dbg->ioSource = debugTraceSourceRegister(DEBUG_TRACE_IO, &ioTraceClass, "CPU");
    debugTraceSetArmCb(armIoTrace);
}

void r800DebugDestroy()
{
    slotSetPageMappedCb(NULL);
    debugDeviceSetReadWatchCb(NULL);
    debugTraceSetArmCb(NULL);
    pagesTracked = 0;
    debugDeviceUnregister(dbg->debugHandle);
    /* The wrappers read through dbg, which goes now. */
    dbg->r800->readMemory  = dbg->readMemory;
    dbg->r800->readIoPort  = dbg->readIoPort;
    dbg->r800->writeIoPort = dbg->writeIoPort;
    debugTraceSourceUnregister(dbg->ioSource);
    debugTraceSourceUnregister(dbg->r800->traceSource);
    free(dbg);
    dbg = NULL;
}

int r800DebugIsR800(void)
{
    return dbg != NULL && r800GetMode(dbg->r800) == CPU_R800;
}

int r800DebugWaitsInHalt(void)
{
    return cpuWaitsInHalt();
}

const CpuRegs* r800DebugRegisters(void)
{
    return dbg != NULL ? &dbg->r800->regs : NULL;
}

int r800DebugIntLow(void)
{
    return dbg != NULL && r800IntIsLow(dbg->r800);
}

int r800DebugNmiLow(void)
{
    return dbg != NULL && r800NmiIsLow(dbg->r800);
}

int r800DebugStoppedBeforeInstruction(void)
{
    return stoppedBeforeInstruction;
}

int r800DebugHistoryGet(R800DebugHistory* out)
{
#if TIME_TRACE_SIZE > 0
    if (dbg == NULL) {
        return 0;
    }
    out->pc     = dbg->r800->pcTraceBuffer;
    out->newest = dbg->r800->timeTraceIndex;
    out->oldest = dbg->r800->pcTraceFirst;
    if (out->newest - out->oldest + 1 > PC_TRACE_SIZE) {
        out->oldest = out->newest - (PC_TRACE_SIZE - 1);
    }
    out->pagesTracked = pagesTracked;
    memcpy(out->changedAt, dbg->r800->pageChangedAt, sizeof(out->changedAt));
    return 1;
#else
    return 0;
#endif
}

static void pageMapped(int page)
{
#if TIME_TRACE_SIZE > 0
    if (dbg != NULL && page >= 0 && page < 8) {
        dbg->r800->pageChangedAt[page] = dbg->r800->timeTraceIndex;
    }
#endif
}

void r800DebugTrackPages(int on)
{
    slotSetPageMappedCb(on ? pageMapped : NULL);
    pagesTracked = on;
}

int r800DebugInsideInstruction(void)
{
#if TIME_TRACE_SIZE > 0
    /* A device stop inside timerCb, such as a VDP command's, is between instructions too. */
    return dbg != NULL && boardIsParkedInBreakpointStop() && !dbg->r800->pcTraceParked;
#else
    return boardIsParkedInBreakpointStop() && !stoppedBeforeInstruction;
#endif
}
