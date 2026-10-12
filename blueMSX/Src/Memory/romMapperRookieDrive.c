/*****************************************************************************
**
** Rookie Drive NX USB storage cartridge.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
#include "romMapperRookieDrive.h"
#include "AmdFlash.h"
#include "CH376.h"
#include "Disk.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "IoPort.h"
#include "RomLoader.h"
#include "Board.h"
#include "SaveState.h"
#include <stdio.h>
#include <stdlib.h>

#define FLASH_SIZE    0x80000
#define SECTOR_SIZE   0x10000
#define BANK_MASK     (FLASH_SIZE / 0x4000 - 1)

/* Bit 7 of the bank register moves the CH376 from ports 20h/21h to 22h/23h. */
#define PORT_BASE     0x20
#define PORT_COUNT    4
#define BANK_PORT_SEL 0x80

typedef struct {
    int       deviceHandle;
    AmdFlash* flash;
    int       slot;
    int       sslot;
    int       startPage;
    int       bank;
    int       portSelect;
    CH376*    ch376;
} RomMapperRookieDrive;

/* Reads go through the callback, so the flash can answer its own commands. */
static void mapBank(RomMapperRookieDrive* rm)
{
    UInt8* bankData = amdFlashGetPage(rm->flash, rm->bank * 0x4000);

    slotMapPage(rm->slot, rm->sslot, rm->startPage,     bankData,          0, 0);
    slotMapPage(rm->slot, rm->sslot, rm->startPage + 1, bankData + 0x2000, 0, 0);
}

static void saveState(RomMapperRookieDrive* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperRookieDrive");

    saveStateSet(state, "bank",       rm->bank);
    saveStateSet(state, "portSelect", rm->portSelect);
    saveStateClose(state);

    amdFlashSaveState(rm->flash);
    ch376SaveState(rm->ch376);
}

static void loadState(RomMapperRookieDrive* rm)
{
    SaveState* state = saveStateOpenForRead("mapperRookieDrive");

    rm->bank       = saveStateGet(state, "bank", 0) & BANK_MASK;
    rm->portSelect = saveStateGet(state, "portSelect", 0) != 0;
    saveStateClose(state);

    amdFlashLoadState(rm->flash);
    ch376LoadState(rm->ch376);
    mapBank(rm);
}

static void destroy(RomMapperRookieDrive* rm)
{
    int i;

    for (i = 0; i < PORT_COUNT; i++) {
        ioPortUnregister(PORT_BASE + i, rm);
    }
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);

    ch376Destroy(rm->ch376);
    amdFlashDestroy(rm->flash);
    free(rm);
}

static void reset(RomMapperRookieDrive* rm)
{
    amdFlashReset(rm->flash);
    rm->bank       = 0;
    rm->portSelect = 0;
    mapBank(rm);
    ch376Reset(rm->ch376);
}

static UInt32 flashAddress(RomMapperRookieDrive* rm, UInt16 address)
{
    return rm->bank * 0x4000 + (address & 0x3fff);
}

static UInt8 read(RomMapperRookieDrive* rm, UInt16 address)
{
    return amdFlashRead(rm->flash, flashAddress(rm, address));
}

static UInt8 peek(RomMapperRookieDrive* rm, UInt16 address)
{
    return *amdFlashGetPage(rm->flash, flashAddress(rm, address));
}

static void write(RomMapperRookieDrive* rm, UInt16 address, UInt8 value)
{
    /* The flash sees every write, in the bank that was selected before it. */
    amdFlashWrite(rm->flash, flashAddress(rm, address), value);

    address += 0x4000;

    if (address >= 0x6000 && address < 0x7000) {
        rm->bank       = value & BANK_MASK;
        rm->portSelect = (value & BANK_PORT_SEL) != 0;
        mapBank(rm);
    }
}

static int portSelected(RomMapperRookieDrive* rm, UInt16 port)
{
    return ((port >> 1) & 1) == rm->portSelect;
}

static UInt8 readIo(RomMapperRookieDrive* rm, UInt16 port)
{
    if (!portSelected(rm, port)) {
        return 0xff;
    }
    return (port & 1) ? ch376ReadStatus(rm->ch376) : ch376ReadData(rm->ch376);
}

static void writeIo(RomMapperRookieDrive* rm, UInt16 port, UInt8 value)
{
    if (!portSelected(rm, port)) {
        return;
    }
    if (port & 1) {
        ch376WriteCommand(rm->ch376, value);
    }
    else {
        ch376WriteData(rm->ch376, value);
    }
}

/* The flash lives in SRAM/rookiedrivenx.sram. Until that file exists it is
** seeded from the firmware image, and stays unsaved if there is none. */
static AmdFlash* createFlash(void)
{
    char   sramPath[512];
    UInt8* seed = NULL;
    int    seedSize = 0;
    int    hasContents;
    FILE*  file;
    AmdFlash* flash;

    sprintf(sramPath, "%s" DIR_SEPARATOR "rookiedrivenx.sram", boardGetBaseDirectory());

    file = fopen(sramPath, "rb");
    hasContents = file != NULL;

    if (file != NULL) {
        fclose(file);
    }
    else {
        seed = romLoad("Machines" DIR_SEPARATOR "Shared Roms" DIR_SEPARATOR "RDFIRMWA.ROM", NULL, &seedSize);
        hasContents = seed != NULL && seedSize >= 0x4000 && seedSize <= FLASH_SIZE &&
                      (seedSize & 0x3fff) == 0 && seed[0] == 'A' && seed[1] == 'B';
    }
    flash = amdFlashCreate(AMD_TYPE_2, FLASH_SIZE, SECTOR_SIZE, 0, seed, hasContents ? seedSize : 0,
                           hasContents ? sramPath : NULL, 0);
    free(seed);

    return flash;
}

int romMapperRookieDriveCreate(int hdId, int slot, int sslot, int startPage)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperRookieDrive* rm = calloc(1, sizeof(RomMapperRookieDrive));
    int i;

    rm->deviceHandle = deviceManagerRegister(ROM_ROOKIEDRIVE, &callbacks, rm);
    slotRegister(slot, sslot, startPage, 2, read, peek, write, destroy, rm);

    rm->slot      = slot;
    rm->sslot     = sslot;
    rm->startPage = startPage;
    rm->flash     = createFlash();
    rm->ch376     = ch376Create(diskGetHdDriveId(hdId, 0));

    for (i = 0; i < PORT_COUNT; i++) {
        ioPortRegister(PORT_BASE + i, readIo, writeIo, rm);
    }
    reset(rm);

    return 1;
}
