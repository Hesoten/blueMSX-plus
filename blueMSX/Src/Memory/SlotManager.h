/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/SlotManager.h,v $
**
** $Revision: 1.7 $
**
** $Date: 2008-03-30 18:38:42 $
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
#ifndef SLOT_MANAGER_H
#define SLOT_MANAGER_H

#include "MsxTypes.h"


typedef UInt8 (*SlotRead)(void*, UInt16);
typedef void  (*SlotWrite)(void*, UInt16, UInt8);
typedef void  (*SlotEject)(void*);


void slotManagerCreate();
void slotManagerDestroy();

void slotManagerReset();

void slotLoadState();
void slotSaveState();

void slotWrite(void* ref, UInt16 address, UInt8 value);
UInt8 slotRead(void* ref, UInt16 address);
UInt8 slotPeek(void* ref, UInt16 address);

void slotRegister(int slot, int sslot, int startpage, int pages,
                  SlotRead readCb, SlotRead peekCb, SlotWrite writeCb, SlotEject ejectCb, void* ref);
void slotUnregister(int slot, int sslot, int startpage);

void slotRemove(int slot, int sslot);

void slotRegisterWrite0(SlotWrite writeCb, void* ref);
void slotUnregisterWrite0();

void slotSetRamSlot(int slot, int psl);
int slotGetRamSlot(int page);
void slotMapRamPage(int slot, int sslot, int page);

/* Memory wait class of an 8kB page of a slot, in the R800's terms. The
 * array holds the classes of the 8 pages the CPU currently sees. */
void slotSetWaitClass(int slot, int sslot, int page, UInt8 waitCls);
const UInt8* slotGetPageWaits();

void slotMapPage(int slot, int sslot, int page, UInt8* pageData, 
                 int readEnable, int writeEnable);
void slotUnmapPage(int slot, int sslot, int page);
void slotUpdatePage(int slot, int sslot, int page, UInt8* pageData, 
                    int readEnable, int writeEnable);

void slotSetSubslotted(int slot, int subslotted);

/* A "page" is a Z80 16kB page (0..3), a "bank" one of the eight 8kB banks of a
** slot (0..7). */

/* Bits returned by slotDebugGetBankState. */
#define SLOT_DEBUG_REGISTERED 0x01  /* a device claims this bank            */
#define SLOT_DEBUG_READABLE   0x02  /* direct page reads are enabled        */
#define SLOT_DEBUG_WRITABLE   0x04  /* direct page writes are enabled       */
#define SLOT_DEBUG_HASDATA    0x08  /* real memory is mapped in, not filler */

int slotDebugGetPagePrimarySlot(int page);
int slotDebugGetPageSubSlot(int page);
int slotDebugGetSubslotted(int primarySlot);
int slotDebugGetSubslotSelect(int primarySlot);
int slotDebugGetBankState(int primarySlot, int subSlot, int bank);

/* What a slot holds: a ROM file name, a mapper name, a RAM size. Names accumulate
** until the slot is cleared. slotDebugGetContents never returns NULL. */
void slotAddContents(int slot, int sslot, const char* description);
void slotClearContents(int slot, int sslot);
const char* slotDebugGetContents(int slot, int sslot);

/* Segment a mapper has in an 8kB bank, reported by the mapper as it switches.
** -1 when no segment is reported there. */
void slotSetBankSegment(int slot, int sslot, int bank, int segment);
int slotDebugGetBankSegment(int slot, int sslot, int bank);

/* Segment size in bytes (0x2000 or 0x4000); 0 when no segments are reported. */
void slotSetSegmentSize(int slot, int sslot, int segmentSize);
int slotDebugGetSegmentSize(int slot, int sslot);

/* The device's whole flat storage, so paged out segments can be read without
** switching. The pointer is live: the device clears it when it goes away. */
#define SLOT_STORAGE_NONE 0
#define SLOT_STORAGE_ROM  1
#define SLOT_STORAGE_RAM  2

void slotSetStorage(int slot, int sslot, UInt8* data, UInt32 size,
                    int writable, int kind);
/* As slotSetStorage, for unswitched storage whose first byte is at slot offset base. */
void slotSetStorageAt(int slot, int sslot, UInt8* data, UInt32 size,
                      int writable, int kind, UInt32 base);
/* Any out argument may be NULL. Returns SLOT_STORAGE_NONE and touches nothing
** when the slot publishes no storage. */
int slotDebugGetStorage(int slot, int sslot, UInt8** data, UInt32* size,
                        int* writable);
/* The slot offset of the storage's first byte; 0 for switched storage or none. */
UInt32 slotDebugGetStorageBase(int slot, int sslot);

/* The memory mapper the machine was built with as its main RAM, marked by the mapper
** itself; a cartridge's mapper is never marked. slotDebugGetMainRam is 0 with no mark. */
void slotSetMainRam(int slot, int sslot);
int slotDebugGetMainRam(int* slot, int* sslot);

#endif
