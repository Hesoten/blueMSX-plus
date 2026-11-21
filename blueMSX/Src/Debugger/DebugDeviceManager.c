/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Debugger/DebugDeviceManager.c,v $
**
** $Revision: 1.14 $
**
** $Date: 2008-03-30 18:38:39 $
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
#include "DebugDeviceManager.h"
#include "Board.h"
#include "ArchThread.h"
#include "ArchEvent.h"
#include "VDP.h"
#include <stdlib.h>
#include <string.h>

#define MAX_DEVICES 64

typedef struct {
    int handle;
    DebugCallbacks callbacks;
    void* ref;
    char  name[32];
    DbgDeviceType type;
} DebugDeviceInfo;

typedef struct {
    DebugDeviceInfo di[MAX_DEVICES];
    int count;
    int lastHandle;
} DebugDeviceManager;

static DebugDeviceManager devManager;

void debugDeviceManagerReset() 
{
    devManager.count = 0;
//    devManager.lastHandle = 0;
}

int debugDeviceRegister(DbgDeviceType type, const char* name, DebugCallbacks* callbacks, void* ref)
{
    /* 0 is the first device's handle, so a full table must not return it. */
    if (devManager.count >= MAX_DEVICES) {
        return -1;
    }

    devManager.di[devManager.count].handle    = ++devManager.lastHandle;
    devManager.di[devManager.count].callbacks = *callbacks;
    devManager.di[devManager.count].ref       = ref;
    devManager.di[devManager.count].type      = type;

    strcpy(devManager.di[devManager.count].name, name);

    devManager.count++;

    return devManager.lastHandle - 1;
}

void debugDeviceUnregister(int handle)
{
    int i;

    if (devManager.count == 0) {
        return;
    }

    for (i = 0; i < devManager.count; i++) {
        if (devManager.di[i].handle == handle + 1) {
            break;
        }
    }

    if (i == devManager.count) {
        return;
    }

    devManager.count--;
    while (i < devManager.count) {
        devManager.di[i] = devManager.di[i + 1];
        i++;
    }
}

/* Counts the device reads and writes this thread is inside: a snapshot, a device write, or an
** agent's whole access. No device may stop the board from one, as this thread cannot park
** itself; per thread, so the emulation loop running on is never the one silenced. */
static ARCH_THREAD_LOCAL int inspecting = 0;

int debugDeviceIsInspecting(void)
{
    return inspecting != 0;
}

static int    lastWatchType;
static UInt32 lastWatchAddress;
static int    lastWatchMode;
static int    lastWatchKind;
static int    lastWatchAccess;
static int    lastWatchPc;

static void noteWatchHit(int devType, UInt32 address, int layout, int kind, int access, int pc)
{
    lastWatchType    = devType;
    lastWatchAddress = address;
    lastWatchMode    = layout;
    lastWatchKind    = kind;
    lastWatchAccess  = access;
    lastWatchPc      = pc;
}

void debugDeviceNoteWatchHit(int devType, UInt32 address, int layout, int kind)
{
    noteWatchHit(devType, address, layout, kind, DBG_WATCH_ON_WRITE, -1);
}

void debugDeviceLastWatchHit(int* devType, UInt32* address, int* layout, int* kind, int* access,
                             int* pc)
{
    *devType    = lastWatchType;
    *address    = lastWatchAddress;
    *layout     = lastWatchMode;
    *kind       = lastWatchKind;
    *access     = lastWatchAccess;
    *pc         = lastWatchPc;
}

int debugDeviceVramIndex(int layout, int address)
{
    int a17 = layout >= 0 && (layout & VDP_LAYOUT_A17) ? 0x20000 : 0;
    int low = address & 0x3ffff;

    if (address < 0 || !vdpVramInterleaved(layout) || low >= 0x20000 + a17) {
        return address;
    }
    return (address & ~0x3ffff) | VDP_VRAM_INDEX_A17(a17, low);
}

int debugDeviceVramAddress(int layout, int index)
{
    int a17 = layout >= 0 && (layout & VDP_LAYOUT_A17) ? 0x20000 : 0;
    int low = index & 0x3ffff;

    if (index < 0 || !vdpVramInterleaved(layout) || low >= 0x20000 + a17) {
        return index;
    }
    return (index & ~0x3ffff) | VDP_VRAM_ADDRESS_A17(a17, low);
}

void debugDeviceInspectBegin(void)
{
    inspecting++;
}

void debugDeviceInspectEnd(void)
{
    inspecting--;
}

void debugDeviceGetSnapshot(DbgDevice** dbgDeviceList, int* count)
{
    int index = 0;
    int i;

    inspecting++;
    for (i = 0; i < devManager.count; i++) {
        if (devManager.di[i].handle != 0) {
            dbgDeviceList[index] = calloc(1, sizeof(DbgDevice));
            strcpy(dbgDeviceList[index]->name, devManager.di[i].name);
            dbgDeviceList[index]->type = devManager.di[i].type;
            dbgDeviceList[index]->deviceHandle = devManager.di[i].handle;
            if (devManager.di[i].callbacks.getDebugInfo != NULL) {
                devManager.di[i].callbacks.getDebugInfo(devManager.di[i].ref, dbgDeviceList[index]);
            }
            index++;
        }
    }
    inspecting--;

    *count = index;
}

int debugDeviceWriteMemory(DbgMemoryBlock* memoryBlock, void* data, int startAddr, int size)
{
    int rv = 0;
    int i;

    inspecting++;
    for (i = 0; i < devManager.count; i++) {
        if (devManager.di[i].handle == memoryBlock->deviceHandle) {
            if (devManager.di[i].callbacks.writeMemory != NULL) {
                rv = devManager.di[i].callbacks.writeMemory(devManager.di[i].ref, memoryBlock->name, data, startAddr, size);
                break;
            }
        }
    }
    inspecting--;
    return rv;
}

int debugDeviceWriteRegister(DbgRegisterBank* regBank, int regIndex, UInt32 value)
{
    int rv = 0;
    int i;

    inspecting++;
    for (i = 0; i < devManager.count; i++) {
        if (devManager.di[i].handle == regBank->deviceHandle) {
            if (devManager.di[i].callbacks.writeRegister != NULL) {
                rv = devManager.di[i].callbacks.writeRegister(devManager.di[i].ref, regBank->name, regIndex, value);
                break;
            }
        }
    }
    inspecting--;
    return rv;
}

int debugDeviceWriteIoPort(DbgIoPorts* ioPorts, int portIndex, UInt32 value)
{
    int rv = 0;
    int i;

    inspecting++;
    for (i = 0; i < devManager.count; i++) {
        if (devManager.di[i].handle == ioPorts->deviceHandle) {
            if (devManager.di[i].callbacks.writeIoPort != NULL) {
                rv = devManager.di[i].callbacks.writeIoPort(devManager.di[i].ref, ioPorts->name, portIndex, value);
                break;
            }
        }
    }
    inspecting--;
    return rv;
}

DbgDevice* dbgDeviceCreate(int handle)
{
    DbgDevice* device = calloc(1, sizeof(DbgDevice));

    strcpy(device->name, devManager.di[handle].name);
    device->deviceHandle = devManager.di[handle].handle;

    return device;
}

DbgMemoryBlock* dbgDeviceAddMemoryBlock(DbgDevice* dbgDevice,
                                        const char* name,
                                        int   writeProtected,
                                        UInt32 startAddress,
                                        UInt32 size,
                                        UInt8* memory)
{
    DbgMemoryBlock* mem;
    int i;
    for (i = 0; i < MAX_DBG_COMPONENTS; i++) {
        if (dbgDevice->memoryBlock[i] == NULL) {
            break;
        }
    }

    if (i == MAX_DBG_COMPONENTS) {
        return NULL;
    }

    mem = malloc(sizeof(DbgMemoryBlock) + size);
    strcpy(mem->name, name);
    mem->writeProtected = writeProtected;
    mem->startAddress = startAddress;
    mem->size = size;
    mem->deviceHandle = dbgDevice->deviceHandle;
    memcpy(mem->memory, memory, size);

    dbgDevice->memoryBlock[i] = mem;
    dbgDevice->memoryBlockCount = i + 1;

    return mem;
}

DbgCallstack* dbgDeviceAddCallstack(DbgDevice* dbgDevice,
                                    const char* name,
                                    UInt16* callstack, 
                                    int size)
{
    DbgCallstack* stack;
    int i;

    if (dbgDevice->callstack != NULL) {
        return NULL;
    }
    stack = malloc(sizeof(DbgCallstack) + sizeof(UInt32) * size);
    for (i = 0; i < size; i++) {
        stack->callstack[i] = callstack[i];
    }
    stack->size = size;
    stack->deviceHandle = dbgDevice->deviceHandle;
    strcpy(stack->name, name);

    dbgDevice->callstack = stack;
    return stack;
}


DbgRegisterBank* dbgDeviceAddRegisterBank(DbgDevice* dbgDevice,
                                          const char* name,
                                          UInt32 registerCount)
{
    DbgRegisterBank* regBank;
    int i;
    for (i = 0; i < MAX_DBG_COMPONENTS; i++) {
        if (dbgDevice->registerBank[i] == NULL) {
            break;
        }
    }

    if (i == MAX_DBG_COMPONENTS) {
        return NULL;
    }

    regBank = calloc(1, sizeof(DbgRegisterBank) + registerCount * sizeof(struct DbgRegister));
    strcpy(regBank->name, name);
    regBank->count = registerCount;
    regBank->deviceHandle = dbgDevice->deviceHandle;

    dbgDevice->registerBank[i] = regBank;
    dbgDevice->registerBankCount = i + 1;

    return regBank;
}

void dbgRegisterBankAddRegister(DbgRegisterBank* regBank,
                                int index,
                                const char* name,
                                UInt8 width,
                                UInt32 value)
{
    strcpy(regBank->reg[index].name, name);
    regBank->reg[index].width = width;
    regBank->reg[index].value = value;
}

DbgIoPorts* dbgDeviceAddIoPorts(DbgDevice* dbgDevice,
                                const char* name,
                                UInt32 ioPortsCount)
{
    DbgIoPorts* ioPorts;
    int i;
    for (i = 0; i < MAX_DBG_COMPONENTS; i++) {
        if (dbgDevice->ioPorts[i] == NULL) {
            break;
        }
    }

    if (i == MAX_DBG_COMPONENTS) {
        return NULL;
    }

    ioPorts = calloc(1, sizeof(DbgIoPorts) + ioPortsCount * sizeof(struct DbgIoPort));
    strcpy(ioPorts->name, name);
    ioPorts->count = ioPortsCount;
    ioPorts->deviceHandle = dbgDevice->deviceHandle;

    dbgDevice->ioPorts[i] = ioPorts;
    dbgDevice->ioPortsCount = i + 1;

    return ioPorts;
}

void dbgIoPortsAddPort(DbgIoPorts* ioPorts,
                       int index,
                       UInt16 port,
                       DbgIoPortDirection direction,
                       UInt8 value)
{
    if (index >= 0 && (UInt32)index < ioPorts->count) {
        ioPorts->port[index].port       = port;  
        ioPorts->port[index].direction  = direction;    
        ioPorts->port[index].value      = value;
    }
}

typedef struct Watchpoint {
    struct Watchpoint* next;
    int address;
    DbgWatchpointCondition condition;
    UInt32 refValue;
    int size;
    int byProgram;   /* a VIDEO one at the address the program uses, not the index */
    int access;      /* DBG_WATCH_ON_ */
} Watchpoint;

Watchpoint* watchpoints[MAX_DEVICES];
int debugWatchpointCount[MAX_DEVICES];

/* A bit for each CPU address a READ watchpoint covers, tested on every read the CPU makes. */
static UInt8 cpuReadBits[0x10000 / 8];
static DebugDeviceReadWatchCb readWatchCb;
/* Innermost lock around the arming callback, a set's lookup and link, and a clear's unlink,
** free and rebuild; nothing waits under it. Never taken on a hit. */
static void* readWatchLock;

void debugDeviceCreateLock(void)
{
    if (readWatchLock == NULL) {
        readWatchLock = archSemaphoreCreate(1);
    }
}

static void readWatchLockTake(void)
{
    if (readWatchLock != NULL) {
        archSemaphoreWait(readWatchLock, -1);
    }
}

static void readWatchLockGive(void)
{
    if (readWatchLock != NULL) {
        archSemaphoreSignal(readWatchLock);
    }
}

static int isWatchpointAt(const Watchpoint* watchpoint, int address, int byProgram, int access)
{
    return watchpoint->address == address && watchpoint->byProgram == byProgram &&
           watchpoint->access == access;
}

static int anyReadWatchpoint(DbgDeviceType devType)
{
    const Watchpoint* watchpoint;

    for (watchpoint = watchpoints[devType]; watchpoint != NULL; watchpoint = watchpoint->next) {
        if (watchpoint->access == DBG_WATCH_ON_READ) {
            return 1;
        }
    }
    return 0;
}

static void markReadBits(UInt8* bits, const Watchpoint* watchpoint)
{
    int address;

    for (address = watchpoint->address;
         address < watchpoint->address + watchpoint->size && address < 0x10000; address++) {
        bits[address >> 3] |= (UInt8)(1 << (address & 7));
    }
}

/* Under readWatchLock. */
static void armReadWatchesLocked(void)
{
    if (readWatchCb != NULL) {
        readWatchCb(anyReadWatchpoint(DBGTYPE_CPU), anyReadWatchpoint(DBGTYPE_PORT));
    }
}

const UInt8* debugDeviceCpuReadBits(void)
{
    return cpuReadBits;
}

void debugDeviceSetReadWatchCb(DebugDeviceReadWatchCb cb)
{
    readWatchLockTake();
    readWatchCb = cb;
    armReadWatchesLocked();
    readWatchLockGive();
}

void debugDeviceSetMemoryWatchpoint(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 refValue, int size)
{
    debugDeviceSetMemoryWatchpointEx(devType, address, condition, refValue, size, 0);
}

void debugDeviceClearMemoryWatchpoint(DbgDeviceType devType, int address)
{
    debugDeviceClearMemoryWatchpointEx(devType, address, 0);
}

void debugDeviceSetMemoryWatchpointEx(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 refValue, int size, int byProgram)
{
    debugDeviceSetMemoryWatchpointOn(devType, address, condition, refValue, size, byProgram,
                                     DBG_WATCH_ON_WRITE);
}

void debugDeviceClearMemoryWatchpointEx(DbgDeviceType devType, int address, int byProgram)
{
    debugDeviceClearMemoryWatchpointOn(devType, address, byProgram, DBG_WATCH_ON_WRITE);
}

int debugDeviceHasMemoryWatchpointEx(DbgDeviceType devType, int address, int byProgram)
{
    return debugDeviceHasMemoryWatchpointOn(devType, address, byProgram, DBG_WATCH_ON_WRITE);
}

void debugDeviceSetMemoryWatchpointOn(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 refValue, int size, int byProgram, int access)
{
    Watchpoint* watchpoint;
    int isNew = 0;

    readWatchLockTake();
    watchpoint = watchpoints[devType];
    while (watchpoint != NULL) {
        if (isWatchpointAt(watchpoint, address, byProgram, access)) {
            break;
        }
        watchpoint = watchpoint->next;
    }
    if (watchpoint == NULL) {
        watchpoint = (Watchpoint*)calloc(1, sizeof(Watchpoint));
        watchpoint->byProgram = byProgram;
        watchpoint->access = access;
        isNew = 1;
    }

    watchpoint->address = address;
    watchpoint->condition = condition;
    watchpoint->refValue = refValue;
    watchpoint->size = size;

    if (isNew) {
        /* Linked in only once it is filled, so the emulation thread walking
        ** the list never has to see a half built node. */
        watchpoint->next = watchpoints[devType];
        watchpoints[devType] = watchpoint;
        debugWatchpointCount[devType]++;
    }
    if (access == DBG_WATCH_ON_READ) {
        /* Bits a smaller size left go at the next clear; a stray one only costs a list walk. */
        if (devType == DBGTYPE_CPU) {
            markReadBits(cpuReadBits, watchpoint);
        }
        armReadWatchesLocked();
    }
    readWatchLockGive();
}

void debugDeviceClearMemoryWatchpointOn(DbgDeviceType devType, int address, int byProgram, int access)
{
    Watchpoint* watchpoint;
    Watchpoint* prevWatchpoint = NULL;

    /* The arming walks the list on another thread; the CPU's own walk is parked by the caller. */
    readWatchLockTake();
    watchpoint = watchpoints[devType];
    while (watchpoint != NULL) {
        if (isWatchpointAt(watchpoint, address, byProgram, access)) {
            if (prevWatchpoint == NULL) {
                watchpoints[devType] = watchpoint->next;
            }
            else {
                prevWatchpoint->next = watchpoint->next;
            }
            free(watchpoint);
            debugWatchpointCount[devType]--;
            break;
        }
        prevWatchpoint = watchpoint;
        watchpoint = watchpoint->next;
    }
    /* Rebuilt aside and copied over, so a bit another watch still needs is never clear. */
    if (access == DBG_WATCH_ON_READ && devType == DBGTYPE_CPU) {
        UInt8 bits[sizeof(cpuReadBits)];

        memset(bits, 0, sizeof(bits));
        for (watchpoint = watchpoints[devType]; watchpoint != NULL; watchpoint = watchpoint->next) {
            if (watchpoint->access == DBG_WATCH_ON_READ) {
                markReadBits(bits, watchpoint);
            }
        }
        memcpy(cpuReadBits, bits, sizeof(cpuReadBits));
    }
    if (access == DBG_WATCH_ON_READ) {
        armReadWatchesLocked();
    }
    readWatchLockGive();
}

int debugDeviceHasMemoryWatchpointOn(DbgDeviceType devType, int address, int byProgram, int access)
{
    Watchpoint* watchpoint = watchpoints[devType];
    while (watchpoint != NULL) {
        if (isWatchpointAt(watchpoint, address, byProgram, access)) {
            return 1;
        }
        watchpoint = watchpoint->next;
    }
    return 0;
}

/* The VRAM layout an access is being made in, read only when a watchpoint needs it. */
#define MODE_OF_ACCESS (-2)

/* Whether the watchpoint covers the byte accessed at index in layout. One set by program
** address compares the program's address of that byte, which *program keeps once found (-1 before). */
static int nodeCovers(const Watchpoint* watchpoint, int index, int layout, int* program)
{
    int at = index;

    if (watchpoint->byProgram) {
        if (*program < 0) {
            if (layout == MODE_OF_ACCESS) {
                layout = vdpDebugGetVramLayout();
            }
            *program = debugDeviceVramAddress(layout, index);
        }
        at = *program;
    }
    return at >= watchpoint->address && at < watchpoint->address + watchpoint->size;
}

/* The watched bytes, first byte highest, read through callback by index. */
static UInt32 watchedValue(const Watchpoint* watchpoint, int index, UInt8 value, void* ref, WatchpointReadMemCallback callback)
{
    UInt32 checkValue = 0;
    int i;

    if (watchpoint->size == 1) {
        return value;
    }
    for (i = 0; i < watchpoint->size; i++) {
        int byteIndex = watchpoint->address + i;
        if (watchpoint->byProgram) {
            byteIndex = debugDeviceVramIndex(vdpDebugGetVramLayout(), byteIndex);
        }
        checkValue <<= 8;
        /* A read takes the byte the CPU got, which a peek of a device may not give again. */
        if (byteIndex == index && (callback == NULL || watchpoint->access == DBG_WATCH_ON_READ)) {
            checkValue |= value;
        }
        else if (callback) {
            checkValue |= callback(ref, byteIndex);
        }
    }
    return checkValue;
}

static int matchWatchpoint(DbgDeviceType devType, int access, int address, UInt8 value, void* ref, WatchpointReadMemCallback callback) {
    Watchpoint* watchpoint = watchpoints[devType];
    int program = -1;
    while (watchpoint != NULL) {
        if (watchpoint->access == access && nodeCovers(watchpoint, address, MODE_OF_ACCESS, &program)) {
            UInt32 checkValue = watchedValue(watchpoint, address, value, ref, callback);
            int breakpointHit = 0;
            switch (watchpoint->condition) {
            case DBGWP_ANY:
                breakpointHit = 1;
                break;
            case DBGWP_EQUALS:
                breakpointHit = checkValue == watchpoint->refValue;
                break;
            case DBGWP_NOT_EQUALS:
                breakpointHit = checkValue != watchpoint->refValue;
                break;
            case DBGWP_GREATER_THAN:
                breakpointHit = checkValue > watchpoint->refValue;
                break;
            case DBGWP_LESS_THAN:
                breakpointHit = checkValue < watchpoint->refValue;
                break;
            }
            if (breakpointHit) {
                return watchpoint->byProgram ? DBG_WATCH_PROGRAM : DBG_WATCH_PHYSICAL;
            }
        }
        watchpoint = watchpoint->next;
    }
    return DBG_WATCH_NONE;
}

int checkWatchpoint(DbgDeviceType devType, int address, UInt8 value, void* ref, WatchpointReadMemCallback callback) {
    return matchWatchpoint(devType, DBG_WATCH_ON_WRITE, address, value, ref, callback);
}

int watchpointCovers(DbgDeviceType devType, int address, int layout, int kind)
{
    Watchpoint* watchpoint = watchpoints[devType];
    int program = -1;
    int found = DBG_WATCH_NONE;
    while (watchpoint != NULL) {
        if (watchpoint->access == DBG_WATCH_ON_WRITE &&
            nodeCovers(watchpoint, address, layout, &program)) {
            found = watchpoint->byProgram ? DBG_WATCH_PROGRAM : DBG_WATCH_PHYSICAL;
            if (found == kind) {
                return found;
            }
        }
        watchpoint = watchpoint->next;
    }
    return found;
}

static void tryAccess(DbgDeviceType devType, int access, int address, UInt8 value, void* ref, WatchpointReadMemCallback callback, int pc) {
    /* Tested second: this runs on every CPU write, every read of a watched byte and, while a
    ** port read watch is armed, every IN; a match is rare. No device routes a debugger access
    ** back through here today, but one that did would hang the UI thread. */
    int kind = matchWatchpoint(devType, access, address, value, ref, callback);

    if (kind != DBG_WATCH_NONE && !debugDeviceIsInspecting()) {
        noteWatchHit(devType, (UInt32)address,
                     devType == DBGTYPE_VIDEO ? vdpDebugGetVramLayout() : -1, kind, access, pc);
        boardOnBreakpoint(DBG_STOP_WATCH);
    }
}

void tryWatchpoint(DbgDeviceType devType, int address, UInt8 value, void* ref, WatchpointReadMemCallback callback) {
    tryAccess(devType, DBG_WATCH_ON_WRITE, address, value, ref, callback, -1);
}

void tryReadWatchpoint(DbgDeviceType devType, int address, UInt8 value, void* ref, WatchpointReadMemCallback callback, int pc) {
    tryAccess(devType, DBG_WATCH_ON_READ, address, value, ref, callback, pc);
}
