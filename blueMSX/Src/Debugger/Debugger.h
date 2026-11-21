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

/* What a watchpoint stops on; only CPU and PORT ones can be READ. */
#define DBG_WATCH_ON_WRITE 0
#define DBG_WATCH_ON_READ  1


typedef void (*DebuggerEvent)(void*);
/* A pause told with its cause (a DBG_STOP_ value) and the run generation it began. */
typedef void (*DebuggerPauseEvent)(void*, int cause, UInt32 runGeneration);
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

/* The one listener inside the emulator, told before every plugin; created before they load. */
BlueDebugger* debuggerCreateListener(DebuggerEvent onEmulatorStart, DebuggerEvent onEmulatorStop,
                                     DebuggerPauseEvent onEmulatorPause, void* ref);

/* Bumped when the machine is started, stopped, reset, or a savestate loaded. */
UInt32 dbgMachineGeneration(void);
void   dbgNotifyMachineChanged(void);

DbgSnapshot*     dbgSnapshotCreate();
void             dbgSnapshotDestroy(DbgSnapshot* dbgSnapshot);
DbgState         dbgGetState();
int              dbgSnapshotGetDeviceCount(DbgSnapshot* dbgSnapshot);
const DbgDevice* dbgSnapshotGetDevice(DbgSnapshot* dbgSnapshot, int deviceIndex);
const DbgDevice* dbgSnapshotFindDeviceWithRegisters(DbgSnapshot* dbgSnapshot, int type);

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

/* Resumes a paused machine only, as an agent asks: it never starts a stopped one, and is
** refused (busy) while the window holds the machine for a question or a change. */
#define DBG_RESUME_OK         0
#define DBG_RESUME_NO_MACHINE 1
#define DBG_RESUME_NOT_PAUSED 2
#define DBG_RESUME_BUSY       3
int  dbgResume(void);

/* Pause and wait until the emulation thread has parked; non-zero once it has.
** Use this rather than dbgPause before reading memory. */
int  dbgPauseSync(int timeoutMs);

/* Access to the machine from an agent's thread, which reads it only while it holds one:
** a view of a paused, parked machine reads the CPU, memory and devices; a hold, of any
** machine that is not stopped, only its layout and the last frame. Changers wait for it. */
#define DBG_ACCESS_HOLD 1
#define DBG_ACCESS_VIEW 2

#define DBG_ACCESS_OK         0
#define DBG_ACCESS_NO_MACHINE 1
#define DBG_ACCESS_NOT_PAUSED 2  /* a view of a machine that runs */
#define DBG_ACCESS_BUSY       3  /* no park, or no free lock, in time; or one is held already */
#define DBG_ACCESS_CHANGING   4  /* the window's thread is changing the machine, or has a menu open */

/* Once at startup, before any plugin loads. */
void debuggerCreateLocks(void);
/* Which thread is the window's and which serves agents; each calls its own once. */
void dbgMarkGuiThread(void);
void dbgMarkAgentThread(void);

/* Waits up to timeoutMs; DBG_ACCESS_OK once this thread holds it. Never nested: a second
** take while one is held is refused as busy. */
int  dbgAccessBegin(int kind, int timeoutMs);
void dbgAccessEnd(void);
/* The kind this thread holds, or 0. */
int  dbgAccessHeld(void);

/* For whatever changes the machine: waits for an access in progress to end. On the window's
** thread it also turns new ones away until dbgChangesDone, called where its loop is idle. */
void dbgChangeBegin(void);
void dbgChangesDone(void);
/* Non-zero while the window's thread has a change under way. */
int  dbgChangePending(void);
/* Steps a paused machine one instruction; a DBG_RESUME_ result. On an agent's thread it is
** refused (busy) while the window has a change under way, as dbgResume is. */
int  dbgStep(void);
/* Runs to the end of the shown frame (from a pause, one frame), or with change on to the
** first frame whose picture differs; a DBG_RESUME_ result, busy while the window or another
** step, step back or hold has the machine. Never ends or toggles a step the user began. */
int  dbgFrameStep(int change);
void dbgStepBack();

/* A breakpoint condition as postfix code: each op pushes, or pops its operands and pushes
** the result, in 32-bit integers. Ends with one value; no ops is always true. */
#define DBG_COND_MAX_OPS   64
#define DBG_COND_MAX_STACK 16
/* How many kept plain breakpoints can have a condition or skip at once. */
#define DBG_COND_PLAIN_MAX 64

typedef enum {
    DBG_COND_CONST,     /* value                                  */
    DBG_COND_REG,       /* value is a DBG_REG_                    */
    DBG_COND_BYTE,      /* pops an address, pushes the byte there */
    DBG_COND_WORD,      /* the same, a little endian word         */
    DBG_COND_NOT, DBG_COND_CPL, DBG_COND_NEG,
    DBG_COND_ADD, DBG_COND_SUB, DBG_COND_SHL, DBG_COND_SHR,
    DBG_COND_AND, DBG_COND_XOR, DBG_COND_OR,
    DBG_COND_EQ, DBG_COND_NE, DBG_COND_LT, DBG_COND_LE, DBG_COND_GT, DBG_COND_GE,
    DBG_COND_LAND, DBG_COND_LOR
} DbgCondOpCode;

typedef enum {
    DBG_REG_A, DBG_REG_F, DBG_REG_B, DBG_REG_C, DBG_REG_D, DBG_REG_E, DBG_REG_H, DBG_REG_L,
    DBG_REG_AF, DBG_REG_BC, DBG_REG_DE, DBG_REG_HL, DBG_REG_IX, DBG_REG_IY, DBG_REG_SP,
    DBG_REG_PC, DBG_REG_ZF, DBG_REG_CF
} DbgCondRegister;

typedef struct {
    UInt8 op;           /* a DbgCondOpCode */
    Int32 value;
} DbgCondOp;

/* Stops once the condition has held more than skip times. */
typedef struct {
    int       opCount;
    DbgCondOp ops[DBG_COND_MAX_OPS];
    UInt32    skip;
} DbgBreakCondition;

/* How often a breakpoint with a condition or skip was reached and found its condition true. */
typedef struct {
    UInt32 hits;
    UInt32 matched;
    UInt32 skip;
    UInt32 stamp;       /* which set of the condition these count */
} DbgBreakCounts;

/* The breakpoint stop last decided: counted is non-zero when one with a condition or skip
** stopped (segment -1 for a plain one at address), byUser when not only a step's stop did. */
typedef struct {
    int    counted;
    int    byUser;
    UInt16 address;
    int    slot;
    int    subSlot;
    int    segment;
    int    offset;
    UInt32 hits;
    UInt32 matched;
    UInt32 stamp;       /* as in DbgBreakCounts, to tell a later set apart */
} DbgBreakHit;

void dbgSetBreakpoint(UInt16 address);
/* A plain breakpoint that stays when the machine starts again and is armed on the
** new CPU, as segment ones are. For the control socket; dbgClearKeptBreakpoint removes it.
** cond NULL stops every time; 0 when there is no room for another with a condition. */
int  dbgSetKeptBreakpoint(UInt16 address, const DbgBreakCondition* cond);
/* Non-zero when a kept one was set there. The CPU keeps an address dbgSetBreakpoint or a
** segment one also set, as in dbgClearKeptBreakpoints, which drops all kept ones. */
int  dbgClearKeptBreakpoint(UInt16 address);
void dbgClearKeptBreakpoints(void);
/* Whether a plain breakpoint, from a plugin or the control socket, is set here. */
int  dbgHasBreakpoint(UInt16 address);
/* Clears only what dbgSetBreakpoint set; a kept one at the same address stays. */
void dbgClearBreakpoint(UInt16 address);

/* Breakpoint at an offset into a mapper segment of a slot; fires only while that
** segment is paged in. Non-zero once watching; refuses when the table is full or
** the offset does not fit a segment. */
int  dbgSetSegmentBreakpoint(int slot, int subSlot, int segment, int offset);
/* The same for the control socket, with its condition replaced (NULL for none) and its
** counts zeroed. One dbgSetSegmentBreakpoint also set there still stops every time. */
int  dbgSetSegmentBreakpointIf(int slot, int subSlot, int segment, int offset,
                               const DbgBreakCondition* cond);
/* Whether dbgSetSegmentBreakpoint set that one, which stops every time. */
int  dbgSegmentBreakpointAlwaysStops(int slot, int subSlot, int segment, int offset);
/* Whether a plain one dbgSetBreakpoint set, or such a segment one paged in, is at address. */
int  dbgOtherBreakpointAt(UInt16 address);
/* The segment table index of that one, or -1. */
int  dbgFindSegmentBreakpoint(int slot, int subSlot, int segment, int offset);
/* Non-zero when one was set there. */
int  dbgClearSegmentBreakpoint(int slot, int subSlot, int segment, int offset);
void dbgClearAllSegmentBreakpoints();
int  dbgSegmentBreakpointCount();

/* For listing: one segment table entry (0 when unused), and the plain addresses up to max. */
int  dbgGetSegmentBreakpoint(int index, int* slot, int* subSlot, int* segment, int* offset);
int  dbgSegmentBreakpointCapacity(void);
int  dbgListBreakpoints(UInt16* out, int max);
int  dbgListKeptBreakpoints(UInt16* out, int max);
/* The counts of a kept plain one or a segment table entry; 0 when it has no condition or skip. */
int  dbgGetKeptBreakpointCounts(UInt16 address, DbgBreakCounts* out);
int  dbgGetSegmentBreakpointCounts(int index, DbgBreakCounts* out);

/* A stop for a step over or out, apart from the user's breakpoints: always stops. */
void dbgSetStepStop(UInt16 address);
void dbgClearStepStop(UInt16 address);

/* Once at startup, with debuggerCreateLocks: the lock every writer of the tables takes. */
void dbgBreakpointsCreateLock(void);

/* Call on machine start, before the plugins re-arm their own lists. Plain ones are
** forgotten unless kept; kept and segment ones are armed again on the new CPU. */
void dbgBreakpointsOnEmulatorStart();

/* Call after a cartridge goes in or out: each segment breakpoint is armed where its segment
** can be paged in now. Runs on the GUI thread for a change, and on the emulation thread as
** a machine starts; it takes the writers' lock, as an agent's request does. */
void dbgBreakpointsOnCartridgeChanged();

/* Whether a hit at this address stops. With count, on the emulation thread mid instruction
** (so it must not block), it counts the hit and notes it for dbgBreakpointLastHit; without,
** it predicts the next hit and changes nothing. */
int  dbgBreakpointDecide(UInt16 address, int count);
void dbgBreakpointLastHit(DbgBreakHit* out);

void dbgSetWatchpoint(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 referenceValue, int size);
void dbgClearWatchpoint(DbgDeviceType devType, int address);
/* The same for a VIDEO watchpoint at the address the program uses when byProgram is non-zero,
** and for one stopping on access, a DBG_WATCH_ON_ value. */
void dbgSetWatchpointEx(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 referenceValue, int size, int byProgram, int access);
void dbgClearWatchpointEx(DbgDeviceType devType, int address, int byProgram, int access);

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
/* Why the machine stopped, as a pause tells it on. */
#define DBG_STOP_NONE        0
#define DBG_STOP_USER        1  /* a pause was asked for                     */
#define DBG_STOP_STEP        2  /* a single step finished                    */
#define DBG_STOP_BREAKPOINT  3
#define DBG_STOP_WATCH       4  /* debugDeviceLastWatchHit says which        */
#define DBG_STOP_VRAM_TIMING 5  /* the VDP was accessed faster than it takes */
#define DBG_STOP_STEP_BACK   6  /* the debugger's step back to the previous instruction ended */
#define DBG_STOP_FRAME       7  /* paused itself on a shown frame: a frame step or step back ended, or a hold from a pause was let go */

void debuggerNotifyEmulatorPause();
void debuggerNotifyEmulatorPauseFor(int cause);
void debuggerNotifyEmulatorResume();
void debuggerNotifyEmulatorReset();
void debuggerTrace(const char* str);
void debuggerSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address);

int debuggerIsPresent(void);

#endif /*DEBUGGER_H*/
