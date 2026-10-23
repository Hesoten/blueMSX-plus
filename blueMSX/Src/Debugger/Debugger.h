/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Debugger/Debugger.h,v $
**
** $Revision: 1.23 $
**
** $Date: 2009-07-01 05:00:23 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
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
#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "MsxTypes.h"

typedef struct BlueDebugger    BlueDebugger;
typedef struct DbgSnapshot DbgSnapshot;
typedef struct DbgDevice   DbgDevice;


typedef enum { 
    DBGTYPE_UNKNOWN,
    DBGTYPE_CPU, 
    DBGTYPE_CART, 
    DBGTYPE_BIOS, 
    DBGTYPE_RAM, 
    DBGTYPE_AUDIO,
    DBGTYPE_VIDEO,
    DBGTYPE_PORT,
} DbgDeviceType;

typedef enum {
    DBGWP_ANY,
    DBGWP_EQUALS,
    DBGWP_NOT_EQUALS,
    DBGWP_LESS_THAN,
    DBGWP_GREATER_THAN
} DbgWatchpointCondition;


typedef void (*DebuggerEvent)(void*);
typedef void (*DebuggerTrace)(void*, const char*);
typedef void (*DebuggerSetBp)(void*, UInt16, UInt16, UInt16);

typedef enum { DBG_STOPPED, DBG_PAUSED, DBG_RUNNING } DbgState;

typedef struct {
    int    deviceHandle;
    char   name[32];
    int    writeProtected;
    UInt32 startAddress;
    UInt32 size;
    UInt8  memory[1];
} DbgMemoryBlock;

typedef struct {
    int    deviceHandle;
    char   name[32];
    UInt32 count;
    struct DbgRegister {
        char   name[7];
        UInt8  width;
        UInt32 value;
    } reg[1];
} DbgRegisterBank;

typedef struct {
    int    deviceHandle;
    char   name[32];
    UInt32 size;
    UInt32 callstack[1];
} DbgCallstack;

typedef struct {
    int    deviceHandle;
    char   name[32];
    UInt32 count;
    struct DbgIoPort {
        UInt16 port;
        UInt8  direction;
        UInt8  value;
    } port[1];
} DbgIoPorts;

BlueDebugger* debuggerCreate(DebuggerEvent onEmulatorStart,
                         DebuggerEvent onEmulatorStop,
                         DebuggerEvent onEmulatorPause,
                         DebuggerEvent onEmulatorResume,
                         DebuggerEvent onEmulatorReset,
                         DebuggerTrace onDebugTrace,
                         DebuggerSetBp onDebugSetBp,
                         void* ref);

void debuggerDestroy(BlueDebugger* debugger);

DbgSnapshot*     dbgSnapshotCreate();
void             dbgSnapshotDestroy(DbgSnapshot* dbgSnapshot);
DbgState         dbgGetState();
int              dbgSnapshotGetDeviceCount(DbgSnapshot* dbgSnapshot);
const DbgDevice* dbgSnapshotGetDevice(DbgSnapshot* dbgSnapshot, int deviceIndex);

int                    dbgDeviceGetMemoryBlockCount(DbgDevice* dbgDevice);
const DbgMemoryBlock*  dbgDeviceGetMemoryBlock(DbgDevice* dbgDevice, int memBlockIndex);
int                    dbgDeviceGetRegisterBankCount(DbgDevice* dbgDevice);
const DbgRegisterBank* dbgDeviceGetRegisterBank(DbgDevice* dbgDevice, int regBankIndex);
int                    dbgDeviceGetCallstackCount(DbgDevice* dbgDevice);
const DbgCallstack*    dbgDeviceGetCallstack(DbgDevice* dbgDevice, int callstackIndex);
int                    dbgDeviceGetIoPortsCount(DbgDevice* dbgDevice);
const DbgIoPorts*      dbgDeviceGetIoPorts(DbgDevice* dbgDevice, int ioPortIndex);

int dbgDeviceWriteMemory(DbgMemoryBlock* memoryBlock, void* data, int startAddr, int size);
int dbgDeviceWriteRegister(DbgRegisterBank* regBank, int regIndex, UInt32 value);
int dbgDeviceWriteIoPort(DbgIoPorts* ioPorts, int portIndex, UInt32 value);

void dbgRun();
void dbgStop();
/* Pauses a running machine only; non-zero when it did. */
int  dbgPause();

/* Pause and wait until the emulation thread has parked; non-zero once it has.
** Use this rather than dbgPause before reading memory. */
int  dbgPauseSync(int timeoutMs);

/* Once at startup, before any plugin loads. */
void debuggerCreateLocks(void);

void dbgStep();
void dbgStepBack();

void dbgSetBreakpoint(UInt16 address);
void dbgClearBreakpoint(UInt16 address);

/* Breakpoint at an offset into a mapper segment of a slot; fires only while that
** segment is paged in. Non-zero once watching; refuses when the table is full or
** the offset does not fit a segment. */
int  dbgSetSegmentBreakpoint(int slot, int subSlot, int segment, int offset);
/* Non-zero when one was set there. */
int  dbgClearSegmentBreakpoint(int slot, int subSlot, int segment, int offset);

/* Once at startup, with debuggerCreateLocks: the lock every writer of the tables takes. */
void dbgBreakpointsCreateLock(void);

/* Call on machine start, before the plugins re-arm their own lists. Plain ones are
** forgotten; segment ones are armed again on the new CPU. */
void dbgBreakpointsOnEmulatorStart();

/* Call after a cartridge goes in or out: each segment breakpoint is armed where its segment
** can be paged in now. Runs on the GUI thread for a change, and on the emulation thread as
** a machine starts; it takes the writers' lock. */
void dbgBreakpointsOnCartridgeChanged();

/* Whether a hit at this address is one the user asked for. Runs on the
** emulation thread mid instruction, so it must not block. */
int dbgBreakpointAllows(UInt16 address);

void dbgSetWatchpoint(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 referenceValue, int size);
void dbgClearWatchpoint(DbgDeviceType devType, int address);

int debuggerCheckVramAccess(void);

void dbgEnableVramAccessCheck(int enable);

// Internal structure and interface

#define MAX_DBG_COMPONENTS 16
struct DbgDevice {
    char name[64];
    DbgDeviceType type;
    int deviceHandle;
    int memoryBlockCount;
    int registerBankCount;
    int ioPortsCount;
    DbgMemoryBlock*  memoryBlock[MAX_DBG_COMPONENTS];
    DbgRegisterBank* registerBank[MAX_DBG_COMPONENTS];
    DbgIoPorts*      ioPorts[MAX_DBG_COMPONENTS];
    DbgCallstack*    callstack;
};

/* For the emulator, under its state lock: a pause or a resume sets the state here as it happens. */
void dbgSetRunState(DbgState state);

void debuggerNotifyEmulatorStart();
void debuggerNotifyEmulatorStop();
void debuggerNotifyEmulatorPause();
void debuggerNotifyEmulatorResume();
void debuggerNotifyEmulatorReset();
void debuggerTrace(const char* str);
void debuggerSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address);

int debuggerIsPresent(void);

#endif /*DEBUGGER_H*/
