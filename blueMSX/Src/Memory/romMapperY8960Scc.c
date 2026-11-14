/*****************************************************************************
**
** Y8960 cartridge - the SCC block: the SCC-equivalent circuit plus the bank
** mapper, the I/O enablers and the memory mapped window.
**
** Copyright (C) 2026 madscient
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
#include "romMapperY8960.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "SlotManager.h"
#include "SaveState.h"
#include "Y8960Scc.h"
#include "AudioMixer.h"
#include "Board.h"
#include <stdlib.h>
#include <string.h>

/* 8kB x 32 banks. Banks 0-15 are ROM and 16-31 are RAM, which is what the
** firmware assumes; the RTL carries no such split because it is unfinished. */
#define Y8960_BANKS      32
#define Y8960_BANK_SIZE  0x2000
#define Y8960_ROM_BANKS  16
#define Y8960_MEM_SIZE   (Y8960_BANKS * Y8960_BANK_SIZE)

#define Y8960_REGIONS    4
#define Y8960_START_PAGE 2

/* The memory mapped window, 7FF0h-7FFFh: the offsets of what it holds, and
** the bits of the two enablers. The layout has moved with each revision of
** the specification, so these sit together rather than in the decoder. */
#define Y8960_WIN_BASE      0x1FF0
#define Y8960_WIN_SSGS      0x0     /* 7FF0h register, 7FF1h value */
#define Y8960_WIN_OPL2      0x2     /* 7FF2h address,  7FF3h data  */
#define Y8960_WIN_OPLL      0x4     /* 7FF4h address,  7FF5h data  */
#define Y8960_WIN_ENABLE1   0x6     /* 7FF6h */
#define Y8960_WIN_VOLUME    0x7     /* 7FF7h-7FFAh: SSG, OPLL, OPL2, SCC */
#define Y8960_WIN_OPM       0xB     /* 7FFBh address,  7FFCh data  */
#define Y8960_WIN_OPM_VOL   0xD     /* 7FFDh */
#define Y8960_WIN_MASTER    0xE     /* 7FFEh */
#define Y8960_WIN_ENABLE2   0xF     /* 7FFFh */
#define Y8960_ENABLE1_OPLL  0x01
#define Y8960_ENABLE2_OPL2  0x01
#define Y8960_ENABLE2_SSGS  0x02
#define Y8960_ENABLE2_OPM   0x04
#define Y8960_ENABLE2_TIMER 0x80
/* The cartridge keeps only these bits, so only these come back on a read of
** 7FFFh, and the others read as 0. */
#define Y8960_ENABLE2_READ  (Y8960_ENABLE2_OPL2 | Y8960_ENABLE2_SSGS | \
                             Y8960_ENABLE2_OPM  | Y8960_ENABLE2_TIMER)

typedef struct {
    int    deviceHandle;
    Y8960SccChip* scc;
    UInt8* memory;
    int    slot;
    int    sslot;
    int    startPage;
    /* Six bits are kept even though only five reach an address: the value
    ** 3Fh selects the SCC window instead of a bank. */
    UInt8  bankReg[Y8960_REGIONS];
    UInt8  ramMode;
    UInt8  ioEnable1;
    UInt8  ioEnable2;
} RomMapperY8960Scc;

/* The window is the only way to reach the enablers, and the cartridge is a
** single card, so the other blocks find it through here. */
static RomMapperY8960Scc* theY8960Scc = NULL;

typedef struct {
    Y8960TunnelWrite write;
    void*            ref;
} Y8960Tunnel;

static Y8960Tunnel theTunnels[Y8960_TUN_COUNT];

void y8960RegisterTunnel(Y8960TunnelBlock block, Y8960TunnelWrite write, void* ref)
{
    if (block < Y8960_TUN_COUNT) {
        theTunnels[block].write = write;
        theTunnels[block].ref   = ref;
    }
}

void y8960UnregisterTunnel(Y8960TunnelBlock block, void* ref)
{
    /* Only the block that claimed it may release it, so a device destroyed
    ** after its replacement was created cannot unhook the newcomer. */
    if (block < Y8960_TUN_COUNT && theTunnels[block].ref == ref) {
        theTunnels[block].write = NULL;
        theTunnels[block].ref   = NULL;
    }
}

static void tunnelWrite(Y8960TunnelBlock block, int port, UInt8 value)
{
    if (theTunnels[block].write != NULL) {
        theTunnels[block].write(theTunnels[block].ref, port, value);
    }
}

int y8960IoEnabled(Y8960IoBlock block)
{
    RomMapperY8960Scc* rm = theY8960Scc;

    /* The enablers live in the memory mapped window, which belongs to the
    ** SCC block. Without it there is no way to reach them, and the blocks
    ** answer rather than stay shut for good. */
    if (rm == NULL) {
        return 1;
    }

    switch (block) {
    case Y8960_IO_OPLL:  return (rm->ioEnable1 & Y8960_ENABLE1_OPLL)  != 0;
    case Y8960_IO_OPL2:  return (rm->ioEnable2 & Y8960_ENABLE2_OPL2)  != 0;
    case Y8960_IO_SSGS:  return (rm->ioEnable2 & Y8960_ENABLE2_SSGS)  != 0;
    case Y8960_IO_OPM:   return (rm->ioEnable2 & Y8960_ENABLE2_OPM)   != 0;
    case Y8960_IO_TIMER: return (rm->ioEnable2 & Y8960_ENABLE2_TIMER) != 0;
    }

    return 0;
}

/* A region shows the SCC registers instead of a bank when its own bank
** register holds 3Fh, but only two regions can: BANK2 (8000-9FFF), where the
** Konami SCC has them, in both modes, and BANK1 (6000-7FFF) in RAM mode
** alone, which is what RAM mode adds (the specification's note on 4XFBh).
** The other two never show them (decided 2026-10-16). */
static int sccVisible(RomMapperY8960Scc* rm, int region)
{
    if (rm->bankReg[region] != 0x3F) {
        return 0;
    }

    switch (region) {
    case 1:  return rm->ramMode;
    case 2:  return 1;
    default: return 0;
    }
}

/* The pan registers are x900h-x904h of the SCC window (channels A-E): 9900h
** in BANK2, 7900h in BANK1. They are decoded on the whole address and for
** writes only; the rest of x9xxh and every read go to the SCC, which decodes
** the low 8 bits and sees its own registers there, as in the cartridge's RTL
** (scc.v). The caller has already found the window open. */
static int sccPanAddress(UInt16 address)
{
    return (address & 0x1FF8) == 0x1900 && (address & 0x07) <= 4;
}

/* In RAM mode the window shows only over a ROM bank, judged on the low five
** bits of the bank number as the RTL does: #32-#47 count as ROM, and the SCC
** (#63) does not, so 7FF0-7FFF then belong to the SCC. In compatibility mode
** a bank number from 16 up cannot be written and is not RAM here, as
** madscient/openMSX_Y8960 has it. */
static int mmioVisible(RomMapperY8960Scc* rm)
{
    return !(rm->ramMode && (rm->bankReg[1] & 0x1F) >= Y8960_ROM_BANKS);
}

/* 7FFFh is the one address in the window that also answers a read. The rest
** of the window is write only, and a read there reaches the bank behind it. */
static int enable2Readable(RomMapperY8960Scc* rm, UInt16 address)
{
    return (address >> 13) == 1 && (address & 0x1FFF) == Y8960_WIN_BASE + Y8960_WIN_ENABLE2 &&
           mmioVisible(rm);
}

static int mirrorPage(int region)
{
    return region < 2 ? region + 6 : region - 2;
}

static void bankSwitch(RomMapperY8960Scc* rm, int region)
{
    int    scc      = sccVisible(rm, region);
    int    bank     = rm->bankReg[region] & 0x1F;
    UInt8* bankData = rm->memory + bank * Y8960_BANK_SIZE;
    /* Region 0 carries the RAM mode and bank registers, so its writes have
    ** to come through the callback even when a RAM bank is mapped there. */
    int    writable = !scc && rm->ramMode && region != 0 && bank >= Y8960_ROM_BANKS;
    int    readable = !scc && !(region == 1 && mmioVisible(rm));

    /* The SCC window covers only 1800-1FFF of the region, and 7FFFh is a
    ** single byte, but a mapped page is all or nothing, so the whole region
    ** goes through the callback. */
    slotMapPage(rm->slot, rm->sslot, rm->startPage + region, bankData, readable, writable);

    /* Each bank also shows four pages away: 4000-7FFF again at C000-FFFF and
    ** 8000-BFFF again at 0000-3FFF. The mirrors are for reading; writes and
    ** the SCC window stay with the page itself. Only a mapper at 4000h has
    ** them, which is where the cartridge decodes. */
    if (rm->startPage == Y8960_START_PAGE) {
        slotMapPage(rm->slot, rm->sslot, mirrorPage(region), bankData, 1, 0);
    }
}

static void bankSwitchAll(RomMapperY8960Scc* rm)
{
    int region;

    for (region = 0; region < Y8960_REGIONS; region++) {
        bankSwitch(rm, region);
    }
}

static void reset(RomMapperY8960Scc* rm)
{
    int region;

    rm->ramMode   = 0;
    rm->ioEnable1 = 0;
    rm->ioEnable2 = 0;
    for (region = 0; region < Y8960_REGIONS; region++) {
        rm->bankReg[region] = (UInt8)region;
    }

    y8960SccReset(rm->scc);
    bankSwitchAll(rm);
}

static void saveState(RomMapperY8960Scc* rm)
{
    SaveState* state = saveStateOpenForWrite("y8960Scc");

    saveStateSet(state, "ramMode", rm->ramMode);
    saveStateSet(state, "bankReg0", rm->bankReg[0]);
    saveStateSet(state, "bankReg1", rm->bankReg[1]);
    saveStateSet(state, "bankReg2", rm->bankReg[2]);
    saveStateSet(state, "bankReg3", rm->bankReg[3]);
    saveStateSet(state, "ioEnable1", rm->ioEnable1);
    saveStateSet(state, "ioEnable2", rm->ioEnable2);
    saveStateSetBuffer(state, "ram", rm->memory + Y8960_ROM_BANKS * Y8960_BANK_SIZE,
                       (Y8960_BANKS - Y8960_ROM_BANKS) * Y8960_BANK_SIZE);

    saveStateClose(state);

    y8960SccSaveState(rm->scc);
}

static void loadState(RomMapperY8960Scc* rm)
{
    SaveState* state = saveStateOpenForRead("y8960Scc");

    rm->ramMode     = (UInt8)saveStateGet(state, "ramMode", 0);
    rm->bankReg[0]  = (UInt8)saveStateGet(state, "bankReg0", 0);
    rm->bankReg[1]  = (UInt8)saveStateGet(state, "bankReg1", 1);
    rm->bankReg[2]  = (UInt8)saveStateGet(state, "bankReg2", 2);
    rm->bankReg[3]  = (UInt8)saveStateGet(state, "bankReg3", 3);
    rm->ioEnable1   = (UInt8)saveStateGet(state, "ioEnable1", 0);
    rm->ioEnable2   = (UInt8)saveStateGet(state, "ioEnable2", 0);
    saveStateGetBuffer(state, "ram", rm->memory + Y8960_ROM_BANKS * Y8960_BANK_SIZE,
                       (Y8960_BANKS - Y8960_ROM_BANKS) * Y8960_BANK_SIZE);

    saveStateClose(state);

    y8960SccLoadState(rm->scc);
    bankSwitchAll(rm);
}

static void destroy(RomMapperY8960Scc* rm)
{
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    if (rm->startPage == Y8960_START_PAGE) {
        slotUnregister(rm->slot, rm->sslot, 0);
        slotUnregister(rm->slot, rm->sslot, 6);
    }
    deviceManagerUnregister(rm->deviceHandle);
    y8960SccDestroy(rm->scc);

    free(rm->memory);
    free(rm);

    theY8960Scc = NULL;
}

static void setBank(RomMapperY8960Scc* rm, int region, UInt8 value)
{
    value &= 0x3F;

    if (rm->bankReg[region] == value) {
        return;
    }

    rm->bankReg[region] = value;
    bankSwitch(rm, region);
}

static UInt8 readMemory(RomMapperY8960Scc* rm, UInt16 address)
{
    int bank = rm->bankReg[address >> 13] & 0x1F;

    return rm->memory[bank * Y8960_BANK_SIZE + (address & 0x1FFF)];
}

static UInt8 read(RomMapperY8960Scc* rm, UInt16 address)
{
    int region = address >> 13;

    if (enable2Readable(rm, address)) {
        return rm->ioEnable2 & Y8960_ENABLE2_READ;
    }

    if ((address & 0x1800) == 0x1800 && sccVisible(rm, region)) {
        return y8960SccRead(rm->scc, (UInt8)(address & 0xFF));
    }

    return readMemory(rm, address);
}

static UInt8 peek(RomMapperY8960Scc* rm, UInt16 address)
{
    int region = address >> 13;

    if (enable2Readable(rm, address)) {
        return rm->ioEnable2 & Y8960_ENABLE2_READ;
    }

    if ((address & 0x1800) == 0x1800 && sccVisible(rm, region)) {
        return y8960SccPeek(rm->scc, (UInt8)(address & 0xFF));
    }

    return readMemory(rm, address);
}

static void write(RomMapperY8960Scc* rm, UInt16 address, UInt8 value)
{
    int region = address >> 13;
    int bank;

    /* 7FF0-7FFF: the enablers, the tunnels to the other blocks, and the
    ** mixer's volumes and master. Every write here stays in the window and
    ** never reaches the bank behind it. */
    if (region == 1 && (address & 0x1FF0) == Y8960_WIN_BASE && mmioVisible(rm)) {
        int offset = address & 0x0F;
        if (offset >= Y8960_WIN_VOLUME && offset < Y8960_WIN_VOLUME + Y8960_VOL_OPM) {
            y8960MixerWrite((Y8960Volume)(offset - Y8960_WIN_VOLUME), value);
            return;
        }
        switch (offset) {
        case Y8960_WIN_ENABLE1:
            rm->ioEnable1 = value;
            break;
        case Y8960_WIN_ENABLE2:
            rm->ioEnable2 = value;
            break;
        case Y8960_WIN_OPM_VOL:
            y8960MixerWrite(Y8960_VOL_OPM, value);
            break;
        case Y8960_WIN_MASTER:
            y8960MixerWriteMaster(value);
            break;
        case Y8960_WIN_SSGS:     tunnelWrite(Y8960_TUN_SSGS, 0, value); break;
        case Y8960_WIN_SSGS + 1: tunnelWrite(Y8960_TUN_SSGS, 1, value); break;
        case Y8960_WIN_OPL2:     tunnelWrite(Y8960_TUN_OPL2, 0, value); break;
        case Y8960_WIN_OPL2 + 1: tunnelWrite(Y8960_TUN_OPL2, 1, value); break;
        case Y8960_WIN_OPLL:     tunnelWrite(Y8960_TUN_OPLL, 0, value); break;
        case Y8960_WIN_OPLL + 1: tunnelWrite(Y8960_TUN_OPLL, 1, value); break;
        case Y8960_WIN_OPM:      tunnelWrite(Y8960_TUN_OPM,  0, value); break;
        case Y8960_WIN_OPM + 1:  tunnelWrite(Y8960_TUN_OPM,  1, value); break;
        }
        return;
    }

    if ((address & 0x1800) == 0x1800 && sccVisible(rm, region)) {
        if (sccPanAddress(address)) {
            y8960SccWritePan(rm->scc, (UInt8)(address & 0x07), value);
        }
        else {
            y8960SccWrite(rm->scc, (UInt8)(address & 0xFF), value);
        }
        return;
    }

    /* 4?FBh with ? = 8..F, the only register visible in both modes. */
    if (address >= 0x0800 && address < 0x1000 && (address & 0xFF) == 0xFB) {
        UInt8 newMode = value & 0x01;
        if (rm->ramMode != newMode) {
            rm->ramMode = newMode;
            bankSwitchAll(rm);
        }
        return;
    }

    if (rm->ramMode) {
        /* 4?FCh-4?FFh select the banks while RAM mode is on. */
        if (address >= 0x0800 && address < 0x1000 && (address & 0xFF) >= 0xFC) {
            setBank(rm, (address & 0x03), value);
            return;
        }
    }
    else {
        /* Konami SCC layout: 5000, 7000, 9000 and B000, each 800h wide. */
        if ((address & 0x1800) == 0x1000) {
            setBank(rm, region, value);
            return;
        }
    }

    /* 4000-5FFF holds the mode and bank registers, and is not written to
    ** memory even when a RAM bank shows there, as madscient/openMSX_Y8960
    ** has it. */
    bank = rm->bankReg[region] & 0x1F;
    if (rm->ramMode && region != 0 && bank >= Y8960_ROM_BANKS) {
        rm->memory[bank * Y8960_BANK_SIZE + (address & 0x1FFF)] = value;
    }
}

static int sccCreate(const char* filename, UInt8* romData, int size,
                     int slot, int sslot, int startPage, SlotEject eject)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperY8960Scc* rm = (RomMapperY8960Scc*)calloc(1, sizeof(RomMapperY8960Scc));

    rm->deviceHandle = deviceManagerRegister(ROM_Y8960, &callbacks, rm);
    slotRegister(slot, sslot, startPage, Y8960_REGIONS, read, peek, write, eject, rm);

    /* No callbacks on the mirrors: they are always mapped for reading, and a
    ** write that finds no callback is dropped, which is what they need. */
    if (startPage == Y8960_START_PAGE) {
        slotRegister(slot, sslot, 0, 2, NULL, NULL, NULL, NULL, rm);
        slotRegister(slot, sslot, 6, 2, NULL, NULL, NULL, NULL, rm);
    }

    rm->scc       = y8960SccCreate(boardGetMixer());
    rm->memory    = (UInt8*)calloc(1, Y8960_MEM_SIZE);
    rm->slot      = slot;
    rm->sslot     = sslot;
    rm->startPage = startPage;

    y8960SccSetMode(rm->scc, Y8960_SCC_REAL);

    if (romData != NULL && size > 0) {
        int romSize = size;
        if (romSize > Y8960_ROM_BANKS * Y8960_BANK_SIZE) {
            romSize = Y8960_ROM_BANKS * Y8960_BANK_SIZE;
        }
        memcpy(rm->memory, romData, romSize);
    }

    theY8960Scc = rm;

    reset(rm);

    return 1;
}

/* Ejecting the cartridge has to take the whole card, and the other blocks
** hold no slot of their own, so the card names them here. This is the slot's
** eject callback only: the device manager still gets plain destroy, because
** at shutdown it walks every block itself and doing it from here as well
** would free each of them twice. */
static void ejectCart(RomMapperY8960Scc* rm)
{
    romMapperY8960SysCtrlDestroy();
    romMapperY8960MixerDestroy();
    romMapperY8960OpmDestroy();
    romMapperY8960TimerDestroy();
    romMapperY8960SsgsDestroy();
    romMapperY8960Opl2exDestroy();
    romMapperY8960OpllexDestroy();

    destroy(rm);
}

int romMapperY8960CartCreate(const char* filename, UInt8* romData,
                             int size, int slot, int sslot)
{
    int success = sccCreate(filename, romData, size, slot, sslot,
                            Y8960_START_PAGE, ejectCart);

    success &= romMapperY8960OpllexCreate();
    success &= romMapperY8960Opl2exCreate();
    success &= romMapperY8960SsgsCreate();
    success &= romMapperY8960TimerCreate();
    success &= romMapperY8960OpmCreate();
    success &= romMapperY8960MixerCreate();
    success &= romMapperY8960SysCtrlCreate();

    return success;
}
