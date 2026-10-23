/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Debugger/Debugger.c,v $
**
** $Revision: 1.19 $
**
** $Date: 2009-07-01 05:00:23 $
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

#include "Debugger.h"
#include "DebugDeviceManager.h"
#include "SlotManager.h"
#include "Emulator.h"
#include "Actions.h"
#include "Board.h"
#include "ArchEvent.h"
#include "ArchThread.h"
#include "ArchTimer.h"
#include "ArchMenu.h"
#include "RaceTest.h"
#include <stdlib.h>

struct BlueDebugger {
    DebuggerEvent onEmulatorStart;
    DebuggerEvent onEmulatorStop;
    DebuggerEvent onEmulatorPause;
    /* The listener's own, told why; NULL for a plugin, which gets onEmulatorPause. */
    DebuggerPauseEvent onEmulatorPauseFor;
    DebuggerEvent onEmulatorResume;
    DebuggerEvent onEmulatorReset;
    DebuggerTrace onDebugTrace;
    DebuggerSetBp onDebugSetBp;
    void* ref;
};

#define MAX_DEVICES 64
/* Slot 0 is the listener's, ahead of the plugins in every fan-out; plugins get the other 8. */
#define MAX_DEBUGGERS 9
#define LISTENER_SLOT 0

struct DbgSnapshot {
    int count;
    DbgDevice* dbgDevice[MAX_DEVICES];
};

static BlueDebugger* debuggerList[MAX_DEBUGGERS];
static volatile DbgState dbgState = DBG_STOPPED;
/* Bumped when the machine starts, stops, resets or is put back to another moment. */
static volatile UInt32 machineGeneration = 1;
static int debuggerVramAccessEnable = 0;

static void onDefault(void* ref) {
}

static void onDefTrace(void* ref, const char* dummy) {
}

static void onDefSetBp(void* ref, UInt16 d1, UInt16 d2, UInt16 d3) {
}

static BlueDebugger* newDebugger(DebuggerEvent onEmulatorStart,
                                 DebuggerEvent onEmulatorStop,
                                 DebuggerEvent onEmulatorPause,
                                 DebuggerEvent onEmulatorResume,
                                 DebuggerEvent onEmulatorReset,
                                 DebuggerTrace onDebugTrace,
                                 DebuggerSetBp onDebugSetBp,
                                 void* ref)
{
    BlueDebugger* debugger = malloc(sizeof(BlueDebugger));

    debugger->onEmulatorStart  = onEmulatorStart  ? onEmulatorStart  : onDefault;
    debugger->onEmulatorStop   = onEmulatorStop   ? onEmulatorStop   : onDefault;
    debugger->onEmulatorPause  = onEmulatorPause  ? onEmulatorPause  : onDefault;
    debugger->onEmulatorResume = onEmulatorResume ? onEmulatorResume : onDefault;
    debugger->onEmulatorReset  = onEmulatorReset  ? onEmulatorReset  : onDefault;
    debugger->onDebugTrace     = onDebugTrace     ? onDebugTrace     : onDefTrace;
    debugger->onDebugSetBp     = onDebugSetBp     ? onDebugSetBp     : onDefSetBp;
    debugger->onEmulatorPauseFor = NULL;
    debugger->ref = ref;
    return debugger;
}

BlueDebugger* debuggerCreate(DebuggerEvent onEmulatorStart,
                         DebuggerEvent onEmulatorStop,
                         DebuggerEvent onEmulatorPause,
                         DebuggerEvent onEmulatorResume,
                         DebuggerEvent onEmulatorReset,
                         DebuggerTrace onDebugTrace,
                         DebuggerSetBp onDebugSetBp,
                         void* ref)
{
    BlueDebugger* debugger = newDebugger(onEmulatorStart, onEmulatorStop, onEmulatorPause,
                                         onEmulatorResume, onEmulatorReset, onDebugTrace,
                                         onDebugSetBp, ref);
    int i;

    for (i = LISTENER_SLOT + 1; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] == NULL) {
            debuggerList[i] = debugger;
            break;
        }
    }

    return debugger;
}

BlueDebugger* debuggerCreateListener(DebuggerEvent onEmulatorStart, DebuggerEvent onEmulatorStop,
                                     DebuggerPauseEvent onEmulatorPause, void* ref)
{
    BlueDebugger* debugger = newDebugger(onEmulatorStart, onEmulatorStop, NULL, NULL, NULL,
                                         NULL, NULL, ref);

    debugger->onEmulatorPauseFor = onEmulatorPause;
    debuggerList[LISTENER_SLOT] = debugger;
    return debugger;
}

UInt32 dbgMachineGeneration(void)
{
    return machineGeneration;
}

void dbgNotifyMachineChanged(void)
{
    machineGeneration++;
}


void debuggerDestroy(BlueDebugger* debugger)
{
    int i;

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] == debugger) {
            debuggerList[i] = NULL;
            break;
        }
    }

    free(debugger);
}

int debuggerCheckVramAccess(void)
{
    return debuggerVramAccessEnable > 0;
}

void debuggerNotifyEmulatorStart()
{
    int i;

    dbgState = DBG_RUNNING;

    dbgNotifyMachineChanged();

    /* Before the plugins re-arm their own lists, so both meet the same new CPU. */
    dbgBreakpointsOnEmulatorStart();

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] != NULL) {
            debuggerList[i]->onEmulatorStart(debuggerList[i]->ref);
        }
    }
}

void debuggerNotifyEmulatorStop()
{
    int i;

    dbgState = DBG_STOPPED;
    dbgNotifyMachineChanged();

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] != NULL) {
            debuggerList[i]->onEmulatorStop(debuggerList[i]->ref);
        }
    }
}

void debuggerNotifyEmulatorPause()
{
    debuggerNotifyEmulatorPauseFor(DBG_STOP_USER);
}

void debuggerNotifyEmulatorPauseFor(int cause)
{
    int i;

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] == NULL) {
            continue;
        }
        /* The seq of the transition this pause notifies, made on this thread just before. */
        if (debuggerList[i]->onEmulatorPauseFor != NULL) {
            debuggerList[i]->onEmulatorPauseFor(debuggerList[i]->ref, cause, emulatorOwnStateSeq());
        }
        else {
            debuggerList[i]->onEmulatorPause(debuggerList[i]->ref);
        }
    }
}

void debuggerNotifyEmulatorResume()
{
    int i;

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] != NULL) {
            debuggerList[i]->onEmulatorResume(debuggerList[i]->ref);
        }
    }
}

void debuggerNotifyEmulatorReset()
{
    int i;

    dbgState = DBG_RUNNING;
    dbgNotifyMachineChanged();

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] != NULL) {
            debuggerList[i]->onEmulatorReset(debuggerList[i]->ref);
        }
    }
}

void debuggerTrace(const char* str)
{
    int i;

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] != NULL) {
            debuggerList[i]->onDebugTrace(debuggerList[i]->ref, str);
        }
    }
}

void debuggerSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address)
{
    int i;

    for (i = 0; i < MAX_DEBUGGERS; i++) {
        if (debuggerList[i] != NULL) {
            debuggerList[i]->onDebugSetBp(debuggerList[i]->ref, slot, page, address);
        }
    }
}

/* The access this thread holds (dbgAccessBegin), and whether it serves agents. */
static ARCH_THREAD_LOCAL int accessHeld;
static ARCH_THREAD_LOCAL int isAgentThread;

DbgSnapshot* dbgSnapshotCreate() 
{
    DbgSnapshot* dbgSnapshot;
    
    RACE_TEST_CHECK(!isAgentThread || accessHeld == DBG_ACCESS_VIEW,
                    "a snapshot on the agent's thread without a view");
    if (dbgState != DBG_PAUSED) {
        return NULL;
    }

    dbgSnapshot = malloc(sizeof(DbgSnapshot));

    debugDeviceGetSnapshot(dbgSnapshot->dbgDevice, &dbgSnapshot->count);

    return dbgSnapshot;
}

int dbgDeviceWriteMemory(DbgMemoryBlock* memoryBlock, void* data, int startAddr, int size)
{
    return debugDeviceWriteMemory(memoryBlock, data, startAddr, size);
}

int dbgDeviceWriteRegister(DbgRegisterBank* regBank, int regIndex, UInt32 value)
{
    return debugDeviceWriteRegister(regBank, regIndex, value);
}

int dbgDeviceWriteIoPort(DbgIoPorts* ioPorts, int portIndex, UInt32 value)
{
    return debugDeviceWriteIoPort(ioPorts, portIndex, value);
}

void dbgSnapshotDestroy(DbgSnapshot* dbgSnapshot)
{
    int i;

    for (i = 0; i < dbgSnapshot->count; i++) {
        DbgDevice* dbgDevice = dbgSnapshot->dbgDevice[i];
        int j;
        for (j = 0; j < MAX_DBG_COMPONENTS; j++) {
            if (dbgDevice->memoryBlock[j] != NULL) {
                free(dbgDevice->memoryBlock[j]);
            }
            if (dbgDevice->registerBank[j] != NULL) {
                free(dbgDevice->registerBank[j]);
            }
            if (dbgDevice->ioPorts[j] != NULL) {
                free(dbgDevice->ioPorts[j]);
            }
        }
        /* dbgDeviceAddCallstack mallocs this one too, and nothing else frees it. */
        if (dbgDevice->callstack != NULL) {
            free(dbgDevice->callstack);
        }

        free(dbgDevice);
    }
    free(dbgSnapshot);
}

DbgState dbgGetState()
{
    return dbgState;
}

void dbgSetRunState(DbgState state)
{
    dbgState = state;
}

int dbgSnapshotGetDeviceCount(DbgSnapshot* dbgSnapshot)
{
    return dbgSnapshot->count;
}

const DbgDevice* dbgSnapshotGetDevice(DbgSnapshot* dbgSnapshot, int index)
{
    if (index >= dbgSnapshot->count) {
        return NULL;
    }
    return dbgSnapshot->dbgDevice[index];
}

const DbgDevice* dbgSnapshotFindDeviceWithRegisters(DbgSnapshot* dbgSnapshot, int type)
{
    int count = dbgSnapshotGetDeviceCount(dbgSnapshot);
    int i;

    for (i = 0; i < count; i++) {
        const DbgDevice* device = dbgSnapshotGetDevice(dbgSnapshot, i);
        if (device != NULL && device->type == type && device->registerBankCount > 0) {
            return device;
        }
    }
    return NULL;
}

int dbgDeviceGetMemoryBlockCount(DbgDevice* dbgDevice)
{
    return dbgDevice->memoryBlockCount;
}

const DbgMemoryBlock* dbgDeviceGetMemoryBlock(DbgDevice* dbgDevice, int index)
{
    if (index >= dbgDevice->memoryBlockCount) {
        return NULL;
    }
    return dbgDevice->memoryBlock[index];
}

int dbgDeviceGetRegisterBankCount(DbgDevice* dbgDevice)
{
    return dbgDevice->registerBankCount;
}

const DbgRegisterBank* dbgDeviceGetRegisterBank(DbgDevice* dbgDevice, int index)
{
    if (index >= dbgDevice->registerBankCount) {
        return NULL;
    }
    return dbgDevice->registerBank[index];
}

int dbgDeviceGetCallstackCount(DbgDevice* dbgDevice)
{
    return dbgDevice->callstack != NULL ? 1 : 0;
}

const DbgCallstack* dbgDeviceGetCallstack(DbgDevice* dbgDevice, int index)
{
    if (index >= dbgDeviceGetCallstackCount(dbgDevice)) {
        return NULL;
    }
    return dbgDevice->callstack;
}

int dbgDeviceGetIoPortsCount(DbgDevice* dbgDevice)
{
    return dbgDevice->ioPortsCount;
}

const DbgIoPorts* dbgDeviceGetIoPorts(DbgDevice* dbgDevice, int index)
{
    if (index >= dbgDevice->ioPortsCount) {
        return NULL;
    }
    return dbgDevice->ioPorts[index];
}

void dbgRun()
{
    if (emulatorGetState() != EMU_RUNNING) {
        actionEmuTogglePause();
    }
}

void dbgStop()
{
    emulatorStop();
}

int dbgPause()
{
    RACE_TEST_POINT(RACE_PAUSE_CHECK);
    /* As the toggle it replaces: nothing pauses while a question about a folder disk is open. */
    if (actionDirGateOpen() || !emulatorPauseIfRunning()) {
        return 0;
    }
    debuggerNotifyEmulatorPause();
    archUpdateMenu(0);
    return 1;
}

/* Spins on the calling thread. Zero when it did not park in time or there was
** no machine to pause. */
int dbgPauseSync(int timeoutMs)
{
    dbgPause();

    if (emulatorGetState() != EMU_PAUSED) {
        return 0;
    }
    return emulatorWaitParked(timeoutMs);
}

/* Held for a whole access; only the agent's thread takes it, so it is free while Off. */
static void* accessLock;
/* Leaf lock around guiChanging. */
static void* changeLock;
static volatile int guiChanging;
static ARCH_THREAD_LOCAL int isGuiThread;

void debuggerCreateLocks(void)
{
    if (accessLock == NULL) {
        accessLock = archSemaphoreCreate(1);
        changeLock = archSemaphoreCreate(1);
    }
    dbgBreakpointsCreateLock();
}

void dbgMarkGuiThread(void)
{
    isGuiThread = 1;
}

void dbgMarkAgentThread(void)
{
    isAgentThread = 1;
}

int dbgChangePending(void)
{
    int pending;

    archSemaphoreWait(changeLock, -1);
    pending = guiChanging;
    archSemaphoreSignal(changeLock);
    return pending;
}

void dbgChangeBegin(void)
{
    if (accessLock == NULL) {
        return;
    }
    if (isGuiThread) {
        archSemaphoreWait(changeLock, -1);
        guiChanging = 1;
        archSemaphoreSignal(changeLock);
    }
    /* Only the agent's thread takes access, so on it there is none to wait for. */
    if (isAgentThread) {
        RACE_TEST_CHECK(accessHeld == 0, "a change made while holding access");
        return;
    }
    /* Finite: whoever holds access never waits on this thread. */
    archSemaphoreWait(accessLock, -1);
    archSemaphoreSignal(accessLock);
}

void dbgChangesDone(void)
{
    /* Only this thread sets it, so a plain look first keeps the idle loop off the lock. */
    if (guiChanging) {
        archSemaphoreWait(changeLock, -1);
        guiChanging = 0;
        archSemaphoreSignal(changeLock);
    }
}

static int timeLeft(UInt32 start, int timeoutMs)
{
    int left = timeoutMs - (int)(archGetSystemUpTime(1000) - start);

    return left > 0 ? left : 0;
}

/* With accessLock taken: whether the machine is as the kind of access needs. */
static int machineReady(int kind, UInt32 start, int timeoutMs)
{
    EmuState state = emulatorGetState();

    if (state == EMU_STOPPED) {
        return DBG_ACCESS_NO_MACHINE;
    }
    if (kind == DBG_ACCESS_HOLD) {
        return DBG_ACCESS_OK;
    }
    if (state != EMU_PAUSED) {
        return DBG_ACCESS_NOT_PAUSED;
    }
    if (!emulatorWaitParked(timeLeft(start, timeoutMs))) {
        state = emulatorGetState();
        return state == EMU_STOPPED ? DBG_ACCESS_NO_MACHINE :
               state != EMU_PAUSED  ? DBG_ACCESS_NOT_PAUSED : DBG_ACCESS_BUSY;
    }
    return DBG_ACCESS_OK;
}

int dbgAccessBegin(int kind, int timeoutMs)
{
    UInt32 start = archGetSystemUpTime(1000);
    int result;

    RACE_TEST_CHECK(accessHeld == 0, "access taken twice");
    if (accessLock == NULL || accessHeld != 0) {
        return DBG_ACCESS_BUSY;
    }
    for (;;) {
        if (!archSemaphoreWaitTimeout(accessLock, timeLeft(start, timeoutMs))) {
            return DBG_ACCESS_BUSY;
        }
        if (!dbgChangePending()) {
            break;
        }
        archSemaphoreSignal(accessLock);
        if (timeLeft(start, timeoutMs) == 0) {
            return DBG_ACCESS_CHANGING;
        }
        archThreadSleep(1);
    }
    result = machineReady(kind, start, timeoutMs);
    if (result != DBG_ACCESS_OK) {
        archSemaphoreSignal(accessLock);
        return result;
    }
    accessHeld = kind;
    /* A device read under it, such as a VDP sync, must not stop the machine on this thread. */
    debugDeviceInspectBegin();
    RACE_TEST_POINT(RACE_IN_VIEW);
    return DBG_ACCESS_OK;
}

void dbgAccessEnd(void)
{
    if (accessHeld != 0) {
        debugDeviceInspectEnd();
        accessHeld = 0;
        archSemaphoreSignal(accessLock);
    }
}

int dbgAccessHeld(void)
{
    return accessHeld;
}

void dbgStep()
{
    if (emulatorGetState() == EMU_PAUSED) {
        actionEmuStep();
    }
}

void dbgStepBack()
{
    if (emulatorGetState() == EMU_PAUSED) {
        actionEmuStepBack();
    }
}

void dbgEnableVramAccessCheck(int enable)
{
    if (enable) {
        debuggerVramAccessEnable++;
    }
    else {
        debuggerVramAccessEnable--;
    }
}    

/* No parking needed: this only fills a node in, or prepends one that is
** already complete, so a walking emulation thread always sees a valid list. */
void dbgSetWatchpoint(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 referenceValue, int size)
{
    debugDeviceSetMemoryWatchpoint(devType, address, condition, referenceValue, size);
}

/* Frees a node the emulation thread walks on every write it checks, so park it
** first. Only from EMU_RUNNING: every other state has that thread stopped or
** about to stop, and leaving one of those is someone else's call to make. */
void dbgClearWatchpoint(DbgDeviceType devType, int address)
{
    int wasRunning = emulatorGetState() == EMU_RUNNING;
    if (wasRunning) {
        emulatorSuspend();
    }
    else {
        /* A paused machine is parked, but an agent may be reading through that node. */
        dbgChangeBegin();
    }
    debugDeviceClearMemoryWatchpoint(devType, address);
    if (wasRunning) {
        emulatorResume();
    }
}
