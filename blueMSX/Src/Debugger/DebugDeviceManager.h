/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Debugger/DebugDeviceManager.h,v $
**
** $Revision: 1.11 $
**
** $Date: 2008-03-31 19:42:19 $
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
#ifndef DEBUG_DEVICE_MANAGER_H
#define DEBUG_DEVICE_MANAGER_H

#include "MsxTypes.h"
#include "Debugger.h"

typedef struct {
    void (*getDebugInfo)(void* ref, DbgDevice* dbgDevice);
    int (*writeMemory)(void* ref, char* name, void* data, int start, int size);
    int (*writeRegister)(void* ref, char* name, int reg, UInt32 value);
    int (*writeIoPort)(void* ref, char* name, UInt16 port, UInt32 value);
} DebugCallbacks;

typedef UInt8 (*WatchpointReadMemCallback)(void*, int);


void debugDeviceManagerReset();

int debugDeviceRegister(DbgDeviceType type, const char* name, DebugCallbacks* callbacks, void* ref);
void debugDeviceUnregister(int handle);

DbgMemoryBlock* dbgDeviceAddMemoryBlock(DbgDevice* dbgDevice,
                                        const char* name,
                                        int   writeProtected,
                                        UInt32 startAddress,
                                        UInt32 size,
                                        UInt8* memory);

DbgCallstack* dbgDeviceAddCallstack(DbgDevice* dbgDevice,
                                    const char* name,
                                    UInt16* callstack, 
                                    int size);

DbgRegisterBank* dbgDeviceAddRegisterBank(DbgDevice* dbgDevice,
                                          const char* name,
                                          UInt32 registerCount);
void dbgRegisterBankAddRegister(DbgRegisterBank* regBank,
                                int index,
                                const char* name,
                                UInt8 width,
                                UInt32 value);

typedef enum {
    DBG_IO_NONE      = 0,
    DBG_IO_READ      = 1,
    DBG_IO_WRITE     = 2,
    DBG_IO_READWRITE = 3
} DbgIoPortDirection;

DbgIoPorts* dbgDeviceAddIoPorts(DbgDevice* dbgDevice,
                                const char* name,
                                UInt32 ioPortsCount);
void dbgIoPortsAddPort(DbgIoPorts* ioPorts,
                       int index,
                       UInt16 port,
                       DbgIoPortDirection direction,
                       UInt8 value);

void debugDeviceGetSnapshot(DbgDevice** dbgDeviceList, int* count);

/* True while one of the calls below is running a device callback. They come
** from the UI thread, which cannot survive being parked, so a device must not
** stop the board from inside one. */
int debugDeviceIsInspecting(void);
/* What a match of a watchpoint found: none, one set at the device's own address, or a VRAM
** one set at the address the program uses. */
#define DBG_WATCH_NONE     0
#define DBG_WATCH_PHYSICAL 1
#define DBG_WATCH_PROGRAM  2

/* The watchpoint hit a WATCH stop is for: noted on the emulation thread before it stops,
** and read back as that stop is told. A VIDEO hit has the screen mode at the write. */
void debugDeviceNoteWatchHit(int devType, UInt32 address, int screenMode, int kind);
void debugDeviceLastWatchHit(int* devType, UInt32* address, int* screenMode, int* kind);

/* The same for a whole access by another thread, so a VDP sync under it keeps a hit pending. */
void debugDeviceInspectBegin(void);
void debugDeviceInspectEnd(void);

/* A VRAM address as the program uses it in that screen mode, to the index of its byte in the
** chips, and back. Only below 0x20000 do the interleaving modes number the bytes otherwise. */
int debugDeviceVramIndex(int screenMode, int address);
int debugDeviceVramAddress(int screenMode, int index);

int debugDeviceWriteMemory(DbgMemoryBlock* memoryBlock, void* data, int startAddr, int size);
int debugDeviceWriteRegister(DbgRegisterBank* regBank, int regIndex, UInt32 value);
int debugDeviceWriteIoPort(DbgIoPorts* ioPorts, int portIndex, UInt32 value);

void debugDeviceSetMemoryWatchpoint(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 refValue, int size);
void debugDeviceClearMemoryWatchpoint(DbgDeviceType devType, int address);

/* The same with byProgram: non-zero for a VIDEO watchpoint at the address the program uses,
** matched in the screen mode of each write. It is another watchpoint than one at that index.
** Has says whether one starts exactly there, which is what a clear matches. */
void debugDeviceSetMemoryWatchpointEx(DbgDeviceType devType, int address, DbgWatchpointCondition condition, UInt32 refValue, int size, int byProgram);
void debugDeviceClearMemoryWatchpointEx(DbgDeviceType devType, int address, int byProgram);
int  debugDeviceHasMemoryWatchpointEx(DbgDeviceType devType, int address, int byProgram);
void tryWatchpoint(DbgDeviceType devType, int address, UInt8 value, void* ref, WatchpointReadMemCallback callback);

/* Reports a match without stopping, for callers that have to finish what they
** are doing before the emulator may be parked; a DBG_WATCH_ value. */
int checkWatchpoint(DbgDeviceType devType, int address, UInt8 value, void* ref, WatchpointReadMemCallback callback);

/* How many watchpoints of that type exist. Exposed as data because the VDP
** command engine has to test it on every VRAM write it makes. Sized like the
** list it mirrors, so the same devType always indexes both. */
extern int debugWatchpointCount[];

/* For a caller holding a hit it could not report at the time: the DBG_WATCH_ kind of a watchpoint
** that still covers that address in that screen mode, kind itself while one of that kind does. */
int watchpointCovers(DbgDeviceType devType, int address, int screenMode, int kind);

#endif /*DEBUG_DEVICE_MANAGER_H*/
