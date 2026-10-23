/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/SlotManager.c,v $
**
** $Revision: 1.8 $
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
#include "SlotManager.h"
#include "SaveState.h"
#include "Led.h"
#include "IoPort.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    UInt8* pageData;
    int    readEnable;
    int    writeEnable;
} RamSlotState;

typedef struct {
    int   subslotted;
    UInt8 state;
    UInt8 substate;
    UInt8 sslReg;
} PrimarySlotState;


typedef struct {
    UInt16        startpage;
    UInt16        pages;
    UInt8*        pageData;
    int           writeEnable;
    int           readEnable;
    SlotRead      read;
    SlotRead      peek;
    SlotWrite     write;
    SlotEject     eject;
    void*         ref;
} Slot;

#define SLOT_CONTENTS_MAX 128

static RamSlotState     ramslot[8];
static PrimarySlotState pslot[4];
typedef struct {
    UInt8* data;
    UInt32 size;
    int    writable;
    int    kind;
    UInt32 base;
} SlotStorage;

static char             slotContents[4][4][SLOT_CONTENTS_MAX];
static int              slotBankSegment[4][4][8];
static int              slotSegmentSize[4][4];
static SlotStorage      slotStorage[4][4];
static int              mainRamSlot = -1;
static int              mainRamSubSlot;
static Slot             slotTable[4][4][8];
static Slot             slotAddr0;
static UInt8            emptyRAM[0x2000];
static Int32            initialized;
static UInt8            waitClass[4][4][8];
static UInt8            pageWait[8];

void slotMapRamPage(int slot, int sslot, int page)
{
    ramslot[page].readEnable  = slotTable[slot][sslot][page].readEnable;
    ramslot[page].writeEnable = slotTable[slot][sslot][page].writeEnable;
    ramslot[page].pageData    = slotTable[slot][sslot][page].pageData;
    pageWait[page]            = waitClass[slot][sslot][page];
}

void slotSetWaitClass(int slot, int sslot, int page, UInt8 waitCls)
{
    waitClass[slot][sslot][page] = waitCls;

    if (pslot[page >> 1].state == slot &&
        (pslot[slot].subslotted ? pslot[page >> 1].substate : 0) == sslot)
    {
        pageWait[page] = waitCls;
    }
}

const UInt8* slotGetPageWaits()
{
    return pageWait;
}

void slotSetRamSlot(int slot, int psl)
{
    int ssl;

    pslot[slot].state    = psl;
    pslot[slot].substate = (pslot[psl].sslReg >> (slot * 2)) & 3;

    ssl = pslot[psl].subslotted ? pslot[slot].substate : 0;
    
    slotMapRamPage(psl, ssl, 2 * slot);
    slotMapRamPage(psl, ssl, 2 * slot + 1);
}

int slotGetRamSlot(int page)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (pslot[i].state == page) {
            return i;
        }
    }
    return 0;
}

/* pslot[] is indexed two ways: state and substate by Z80 page, subslotted and
** sslReg by primary slot. */

int slotDebugGetPagePrimarySlot(int page)
{
    if (!initialized || page < 0 || page > 3) {
        return 0;
    }
    return pslot[page].state & 3;
}

int slotDebugGetPageSubSlot(int page)
{
    int primarySlot;

    if (!initialized || page < 0 || page > 3) {
        return 0;
    }
    primarySlot = pslot[page].state & 3;

    /* An unexpanded slot has no subslot; its substate is stale. */
    if (!pslot[primarySlot].subslotted) {
        return 0;
    }
    return pslot[page].substate & 3;
}

int slotDebugGetSubslotted(int primarySlot)
{
    if (!initialized || primarySlot < 0 || primarySlot > 3) {
        return 0;
    }
    return pslot[primarySlot].subslotted ? 1 : 0;
}

/* The value last written to $FFFF; reading $FFFF gives its complement. */
int slotDebugGetSubslotSelect(int primarySlot)
{
    if (!initialized || primarySlot < 0 || primarySlot > 3) {
        return 0;
    }
    return pslot[primarySlot].sslReg;
}

void slotAddContents(int slot, int sslot, const char* description)
{
    char* text;
    int used;
    int room;

    if (slot < 0 || slot > 3 || sslot < 0 || sslot > 3 ||
        description == NULL || *description == 0)
    {
        return;
    }

    text = slotContents[slot][sslot];
    used = (int)strlen(text);
    room = SLOT_CONTENTS_MAX - 1 - used;

    /* Several things can share a slot, so append rather than replace. */
    if (used > 0) {
        if (room < 4) {
            return;
        }
        strcat(text, " + ");
        used += 3;
        room -= 3;
    }

    strncat(text, description, room);
    text[SLOT_CONTENTS_MAX - 1] = 0;
}

void slotClearContents(int slot, int sslot)
{
    if (slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return;
    }
    slotContents[slot][sslot][0] = 0;
}

const char* slotDebugGetContents(int slot, int sslot)
{
    if (slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return "";
    }
    return slotContents[slot][sslot];
}

void slotSetBankSegment(int slot, int sslot, int bank, int segment)
{
    if (slot < 0 || slot > 3 || sslot < 0 || sslot > 3 || bank < 0 || bank > 7) {
        return;
    }
    slotBankSegment[slot][sslot][bank] = segment;
}

int slotDebugGetBankSegment(int slot, int sslot, int bank)
{
    if (!initialized ||
        slot < 0 || slot > 3 || sslot < 0 || sslot > 3 || bank < 0 || bank > 7)
    {
        return -1;
    }
    return slotBankSegment[slot][sslot][bank];
}

void slotSetSegmentSize(int slot, int sslot, int segmentSize)
{
    if (slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return;
    }
    slotSegmentSize[slot][sslot] = segmentSize;
}

int slotDebugGetSegmentSize(int slot, int sslot)
{
    if (!initialized || slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return 0;
    }
    return slotSegmentSize[slot][sslot];
}

void slotSetStorage(int slot, int sslot, UInt8* data, UInt32 size,
                    int writable, int kind)
{
    slotSetStorageAt(slot, sslot, data, size, writable, kind, 0);
}

void slotSetStorageAt(int slot, int sslot, UInt8* data, UInt32 size,
                      int writable, int kind, UInt32 base)
{
    SlotStorage* storage;

    if (slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return;
    }
    storage = &slotStorage[slot][sslot];

    if (data == NULL || size == 0) {
        storage->data     = NULL;
        storage->size     = 0;
        storage->writable = 0;
        storage->kind     = SLOT_STORAGE_NONE;
        storage->base     = 0;
        return;
    }
    storage->data     = data;
    storage->size     = size;
    storage->writable = writable;
    storage->kind     = kind;
    storage->base     = base;
}

UInt32 slotDebugGetStorageBase(int slot, int sslot)
{
    if (!initialized || slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return 0;
    }
    return slotStorage[slot][sslot].base;
}

void slotSetMainRam(int slot, int sslot)
{
    if (slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return;
    }
    mainRamSlot    = slot;
    mainRamSubSlot = sslot;
}

int slotDebugGetMainRam(int* slot, int* sslot)
{
    if (!initialized || mainRamSlot < 0) {
        return 0;
    }
    *slot  = mainRamSlot;
    *sslot = mainRamSubSlot;
    return 1;
}

int slotDebugGetStorage(int slot, int sslot, UInt8** data, UInt32* size,
                        int* writable)
{
    const SlotStorage* storage;

    if (!initialized || slot < 0 || slot > 3 || sslot < 0 || sslot > 3) {
        return SLOT_STORAGE_NONE;
    }
    storage = &slotStorage[slot][sslot];
    if (storage->kind == SLOT_STORAGE_NONE) {
        return SLOT_STORAGE_NONE;
    }

    if (data     != NULL) { *data     = storage->data;     }
    if (size     != NULL) { *size     = storage->size;     }
    if (writable != NULL) { *writable = storage->writable; }

    return storage->kind;
}

int slotDebugGetBankState(int primarySlot, int subSlot, int bank)
{
    const Slot* slotInfo;
    int state = 0;

    if (!initialized ||
        primarySlot < 0 || primarySlot > 3 ||
        subSlot     < 0 || subSlot     > 3 ||
        bank        < 0 || bank        > 7)
    {
        return 0;
    }

    slotInfo = &slotTable[primarySlot][subSlot][bank];

    if (slotInfo->read != NULL || slotInfo->write != NULL || slotInfo->peek != NULL) {
        state |= SLOT_DEBUG_REGISTERED;
    }
    if (slotInfo->readEnable) {
        state |= SLOT_DEBUG_READABLE;
    }
    if (slotInfo->writeEnable) {
        state |= SLOT_DEBUG_WRITABLE;
    }
    /* An unmapped bank points at the shared emptyRAM filler, not real memory. */
    if (slotInfo->pageData != NULL && slotInfo->pageData != emptyRAM) {
        state |= SLOT_DEBUG_HASDATA;
    }

    return state;
}

void slotMapPage(int slot, int sslot, int page, UInt8* pageData,
                 int readEnable, int writeEnable)
{
    if (!initialized) {
        return;
    }

    slotTable[slot][sslot][page].readEnable  = readEnable;
    slotTable[slot][sslot][page].writeEnable = writeEnable;

    if (pageData != NULL) {
        slotTable[slot][sslot][page].pageData = pageData;
    }
#if 0
    if (pslot[page >> 1].state == slot && (!pslot[slot].subslotted || sslot == 2 || pslot[page >> 1].substate == sslot)) {
        slotMapRamPage(slot, sslot, page);
    }
#else
    if (pslot[page >> 1].state == slot && 
        (!pslot[slot].subslotted || pslot[page >> 1].substate == sslot)) 
    {
        slotMapRamPage(slot, sslot, page);
    }
#endif
}

void slotUpdatePage(int slot, int sslot, int page, UInt8* pageData, 
                    int readEnable, int writeEnable) 
{
    if (!initialized) {
        return;
    }

    slotMapPage(slot, sslot, page, pageData, readEnable, writeEnable);
    return;
    slotTable[slot][sslot][page].readEnable  = readEnable;
    slotTable[slot][sslot][page].writeEnable = writeEnable;

    if (pageData != NULL) {
        slotTable[slot][sslot][page].pageData = pageData;
    }
}

void slotUnmapPage(int slot, int sslot, int page)
{
    if (!initialized) {
        return;
    }

    slotTable[slot][sslot][page].readEnable  = 0;
    slotTable[slot][sslot][page].writeEnable = 1;
    slotTable[slot][sslot][page].pageData = emptyRAM;

    if (pslot[page >> 1].state == slot && 
        (!pslot[slot].subslotted || pslot[page >> 1].substate == sslot))  
    {
        slotMapRamPage(slot, sslot, page);
    }
}

void slotRegisterWrite0(SlotWrite writeCb, void* ref) 
{
    if (!initialized) {
        return;
    }

    slotAddr0.read  = NULL;
    slotAddr0.write = writeCb;
    slotAddr0.eject = NULL;
    slotAddr0.ref   = ref;
}

void slotUnregisterWrite0() {
    if (!initialized) {
        return;
    }

    memset(&slotAddr0, 0, sizeof(Slot));
}

void slotRegister(int slot, int sslot, int startpage, int pages,
                  SlotRead readCb, SlotRead peekCb, SlotWrite writeCb, SlotEject ejectCb, void* ref)
{
    Slot* slotInfo;
    
    if (!initialized) {
        return;
    }

    slotInfo = &slotTable[slot][sslot][startpage];

    slotInfo->pages = pages;

    while (pages--) {
        slotInfo->startpage = startpage;
        slotInfo->read  = readCb;
        slotInfo->peek  = peekCb;
        slotInfo->write = writeCb;
        slotInfo->eject = ejectCb;
        slotInfo->ref   = ref;
        slotInfo++;
    }
}


/* Switched storage belongs to the mapper of the whole subslot, unswitched storage to the
** device whose pages hold its first byte. */
static int ownsStorage(int slot, int sslot, int startpage, int pages)
{
    const SlotStorage* storage = &slotStorage[slot][sslot];
    int firstPage = (int)(storage->base / 0x2000);

    if (storage->kind == SLOT_STORAGE_NONE || slotSegmentSize[slot][sslot] > 0) {
        return 1;
    }
    return firstPage >= startpage && firstPage < startpage + pages;
}

void slotUnregister(int slot, int sslot, int startpage)
{
    Slot* slotInfo;
    int pages;
    int owner;

    if (!initialized) {
        return;
    }

    slotInfo = &slotTable[slot][sslot][startpage];
    pages = slotInfo->pages;
    owner = ownsStorage(slot, sslot, startpage, pages);

    while (pages--) {
        memset(slotInfo, 0, sizeof(Slot));
        /* A stale segment would have a breakpoint watching the wrong bank. */
        slotSetBankSegment(slot, sslot, startpage + pages, -1);
        slotUnmapPage(slot, sslot, startpage + pages);
        slotInfo++;
    }

    /* Storage another device in this subslot owns stays when this one goes. */
    if (!owner) {
        return;
    }

    /* Left behind, the next plain ROM in this slot would look like a mapper. */
    slotSetSegmentSize(slot, sslot, 0);

    /* The storage is freed with the device, so drop the pointer now. */
    slotSetStorage(slot, sslot, NULL, 0, 0, SLOT_STORAGE_NONE);
}

void slotRemove(int slot, int sslot)
{
    int page;

    if (!initialized) {
        return;
    }

    for (page = 0; page < 8; page++) {
        Slot* slotInfo = &slotTable[slot][sslot][page];
        if (slotInfo->eject != NULL) {
            slotInfo->eject(slotInfo->ref);
        }
    }
}

void slotSetSubslotted(int slot, int subslotted)
{
    if (!initialized) {
        return;
    }

    pslot[slot].subslotted = subslotted;
}

void slotManagerReset() 
{
    int page;

    if (!initialized) {
        return;
    }

    for (page = 0; page < 4; page++) {
        pslot[page].state = 0;
        pslot[page].substate = 0;

        slotMapRamPage(0, 0, 2 * page);
        slotMapRamPage(0, 0, 2 * page + 1);
    }
}

void slotManagerCreate()
{
    int slot;
    int sslot;
    int page;

    memset(emptyRAM, 0xff, 0x2000);
    memset(ramslot, 0, sizeof(ramslot));
    memset(pslot, 0, sizeof(pslot));
    memset(slotContents, 0, sizeof(slotContents));
    memset(slotSegmentSize, 0, sizeof(slotSegmentSize));
    memset(slotStorage, 0, sizeof(slotStorage));
    memset(slotTable, 0, sizeof(slotTable));
    mainRamSlot = -1;

    /* A bank that never reports has no known segment. */
    for (slot = 0; slot < 4; slot++) {
        for (sslot = 0; sslot < 4; sslot++) {
            for (page = 0; page < 8; page++) {
                slotBankSegment[slot][sslot][page] = -1;
            }
        }
    }
    memset(&slotAddr0, 0, sizeof(slotAddr0));
    memset(waitClass, 0, sizeof(waitClass));
    memset(pageWait, 0, sizeof(pageWait));

    for (slot = 0; slot < 4; slot++) {
        for (sslot = 0; sslot < 4; sslot++) {
            for (page = 0; page < 8; page++) {
                slotUnmapPage(slot, sslot, page);
            }
        }
    }

    initialized = 1;
}

void slotManagerDestroy() 
{
    initialized = 0;
}

UInt8 slotPeek(void* ref, UInt16 address)
{
    Slot* slotInfo;
    int psl;
    int ssl;

    if (!initialized) {
        return 0xff;
    }

    if (address == 0xffff) {
        UInt8 sslReg = pslot[3].state;
        if (pslot[sslReg].subslotted) {
            return ~pslot[sslReg].sslReg;
        }
    }

    if (ramslot[address >> 13].readEnable) {
        return ramslot[address >> 13].pageData[address & 0x1fff];
    }

    psl = pslot[address >> 14].state;
    ssl = pslot[psl].subslotted ? pslot[address >> 14].substate : 0;

    slotInfo = &slotTable[psl][ssl][address >> 13];

    if (slotInfo->peek != NULL) {
        address -= slotInfo->startpage << 13;
        return slotInfo->peek(slotInfo->ref, address);
    }

    return 0xff;
}

UInt8 slotRead(void* ref, UInt16 address)
{
    Slot* slotInfo;
    int psl;
    int ssl;

    if (!initialized) {
        return 0xff;
    }

    if (address == 0xffff) {
        UInt8 sslReg = pslot[3].state;
        if (pslot[sslReg].subslotted) {
            return ~pslot[sslReg].sslReg;
        }
    }

    if (ramslot[address >> 13].readEnable) {
        return ramslot[address >> 13].pageData[address & 0x1fff];
    }

    psl = pslot[address >> 14].state;
    ssl = pslot[psl].subslotted ? pslot[address >> 14].substate : 0;

    slotInfo = &slotTable[psl][ssl][address >> 13];

    if (slotInfo->read != NULL) {
        address -= slotInfo->startpage << 13;
        return slotInfo->read(slotInfo->ref, address);
    }

    return 0xff;
}

void slotWrite(void* ref, UInt16 address, UInt8 value)
{
    Slot* slotInfo;
    int psl;
    int ssl;
    int page;

    if (!initialized) {
        return;
    }

    if (address == 0xffff) {
        UInt8 pslReg = pslot[3].state;

        if (pslot[pslReg].subslotted) {
//            printf("SW: %d %d %d %d\n", (value>>0)&3, (value>>2)&3, (value>>4)&3, (value>>6)&3);
            pslot[pslReg].sslReg = value;

            for (page = 0; page < 4; page++) {
                if(pslot[page].state == pslReg) {
                    pslot[page].substate = value & 3;
                    slotMapRamPage(pslReg, value & 3, 2 * page);
                    slotMapRamPage(pslReg, value & 3, 2 * page + 1);
                }
                value >>= 2;
            }

            return;
        }
    }

    if (address == 0) {
        if (slotAddr0.write != NULL) {
            slotAddr0.write(slotAddr0.ref, address, value);
            return;
        }
    }

    if (ramslot[address >> 13].writeEnable) {
        ramslot[address >> 13].pageData[address & 0x1FFF] = value;
        return;
    }

    psl = pslot[address >> 14].state;
    ssl = pslot[psl].subslotted ? pslot[address >> 14].substate : 0;

    slotInfo = &slotTable[psl][ssl][address >> 13];

    if (slotInfo->write != NULL) {
        address -= slotInfo->startpage << 13;
        slotInfo->write(slotInfo->ref, address, value);
    }
}

void slotSaveState()
{
    SaveState* state;
    char tag[32];
    int i;

    if (!initialized) {
        return;
    }

    state = saveStateOpenForWrite("slotManager");

    for (i = 0; i < 4; i++) {
        sprintf(tag, "subslotted%d", i);
        saveStateSet(state, tag, pslot[i].subslotted);
        
        sprintf(tag, "state%d", i);
        saveStateSet(state, tag, pslot[i].state);
        
        sprintf(tag, "substate%d", i);
        saveStateSet(state, tag, pslot[i].substate);
        
        sprintf(tag, "sslReg%d", i);
        saveStateSet(state, tag, pslot[i].sslReg);
    }

    saveStateClose(state);
}

void slotLoadState()
{
    SaveState* state;
    char tag[32];
    int page;
    int i;

    if (!initialized) {
        return;
    }

    state = saveStateOpenForRead("slotManager");

    for (i = 0; i < 4; i++) {
        sprintf(tag, "subslotted%d", i);
        pslot[i].subslotted = saveStateGet(state, tag, 0);
        
        sprintf(tag, "state%d", i);
        pslot[i].state = (UInt8)saveStateGet(state, tag, 0);
        
        sprintf(tag, "substate%d", i);
        pslot[i].substate = (UInt8)saveStateGet(state, tag, 0);
        
        sprintf(tag, "sslReg%d", i);
        pslot[i].sslReg = (UInt8)saveStateGet(state, tag, 0);
    }

    saveStateClose(state);

    for (page = 0; page < 4; page++) {
        int psl = pslot[page].state;
        int ssl = pslot[psl].subslotted ? pslot[page].substate : 0;

        slotMapRamPage(psl, ssl, 2 * page);
        slotMapRamPage(psl, ssl, 2 * page + 1);
    }
}
