/*****************************************************************************
**
** Flash-ROM SCC cartridge (Developer Edition).
** Copyright (C) 2026 Hesoten
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
/* Reference: https://github.com/popolonfr/Flash-ROM-SCC-Cartridge
**
** Developer Edition feature set (superset of the other editions):
**  - 16 MB flash (M29W128 type, 128 KB sectors, x8 wiring)
**  - Konami SCC mapper: 8 KB pages at 4000h/6000h/8000h/A000h, bank
**    registers at 5000h/7000h/9000h/B000h (mirrored through x7FFh)
**  - 14-bit Offset register at 3800h (L) / 3801h (H), added to all bank
**    registers, in 8 KB units
**  - Operating-Mode register at 3804h: bit 1 selects 16-bit bank registers
**    (even address = low byte, odd address = high byte)
**  - SCC sound (segment 3Fh on page 9000h) with an 8-bit DAC in the
**    repurposed SCC test-register area (98FBh control, 98FCh data) plus a
**    Konami-compatible DAC window at 4000h-4FFFh
** Not emulated: the capacitive touch button (boot-blocker). */

#include "romMapperFlashRomScc.h"
#include "AmdFlash.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "sramLoader.h"
#include "SCC.h"
#include "DAC.h"
#include "Board.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>

#define FLASHROMSCC_FLASH_SIZE   0x1000000   /* 16 MB */
#define FLASHROMSCC_FLASH_SECTOR 0x20000     /* 128 KB */

/* Operating-Mode register (3804h) bits */
#define OPMODE_16BIT  0x02     /* M: 16-bit bank registers */

/* Knm_DAC_Ctrl (98FBh) bits */
#define KNM_DAC_EN    0x10     /* D: Konami DAC window at 4000h-4FFFh */

typedef struct {
    int       deviceHandle;
    AmdFlash* flash;
    SCC*      scc;
    DAC*      dac;
    int       slot;
    int       sslot;
    int       startPage;           /* first registered slot page (2000h) */
    UInt32    romMask;             /* (FLASH_SIZE / 0x2000) - 1 = 0x7FF */

    UInt16    bankReg[4];          /* raw 14-bit bank registers */
    UInt16    offsetReg;           /* 14-bit offset, added to all banks */
    UInt8     modeReg;             /* Operating-Mode register (3804h) */
    UInt8     dacCtrl;             /* Knm_DAC_Ctrl register (98FBh) */
} RomMapperFlashRomScc;

/* SCC register window at 9800h-9FFFh, visible per the Konami SCC rule: low
** 6 bits of the raw page-2 bank register equal 3Fh.  The offset-adjusted
** value is not used. */
static int isSCCAccess(const RomMapperFlashRomScc* rm, UInt16 msxAddr)
{
    return (rm->bankReg[2] & 0x3F) == 0x3F &&
           msxAddr >= 0x9800 && msxAddr < 0xA000;
}

static UInt32 effectiveBank(const RomMapperFlashRomScc* rm, int page)
{
    return (UInt32)(rm->bankReg[page] + rm->offsetReg) & rm->romMask;
}

static void mapBank(RomMapperFlashRomScc* rm, int page)
{
    UInt8* bankData = amdFlashGetPage(rm->flash, effectiveBank(rm, page) * 0x2000);
    /* readEnable=0: reads route through the read callback so SCC ranges
    ** and flash-ident responses are intercepted before hitting the ROM. */
    slotMapPage(rm->slot, rm->sslot, rm->startPage + 1 + page, bankData, 0, 0);
}

static void mapAllBanks(RomMapperFlashRomScc* rm)
{
    int i;
    for (i = 0; i < 4; i++) {
        mapBank(rm, i);
    }
}

static UInt32 msxAddrToFlashAddr(RomMapperFlashRomScc* rm, UInt16 msxAddr)
{
    int page = (msxAddr - 0x4000) >> 13;
    if (page < 0 || page >= 4) {
        return 0;
    }
    return effectiveBank(rm, page) * 0x2000 + (msxAddr & 0x1FFF);
}

static void destroy(RomMapperFlashRomScc* rm)
{
    amdFlashDestroy(rm->flash);
    sccDestroy(rm->scc);
    dacDestroy(rm->dac);
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);
    free(rm);
}

static void reset(RomMapperFlashRomScc* rm)
{
    int i;
    for (i = 0; i < 4; i++) {
        rm->bankReg[i] = (UInt16)i;
    }
    rm->offsetReg = 0;
    rm->modeReg   = 0;
    rm->dacCtrl   = 0;
    amdFlashReset(rm->flash);
    sccReset(rm->scc);
    dacReset(rm->dac);
    mapAllBanks(rm);
}

static void saveState(RomMapperFlashRomScc* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperFlashRomScc");
    char tag[16];
    int i;

    for (i = 0; i < 4; i++) {
        sprintf(tag, "bankReg%d", i);
        saveStateSet(state, tag, rm->bankReg[i]);
    }
    saveStateSet(state, "offsetReg", rm->offsetReg);
    saveStateSet(state, "modeReg",   rm->modeReg);
    saveStateSet(state, "dacCtrl",   rm->dacCtrl);
    saveStateClose(state);

    sccSaveState(rm->scc);
    amdFlashSaveState(rm->flash);
}

static void loadState(RomMapperFlashRomScc* rm)
{
    SaveState* state = saveStateOpenForRead("mapperFlashRomScc");
    char tag[16];
    int i;

    for (i = 0; i < 4; i++) {
        sprintf(tag, "bankReg%d", i);
        rm->bankReg[i] = (UInt16)saveStateGet(state, tag, i);
    }
    rm->offsetReg = (UInt16)saveStateGet(state, "offsetReg", 0);
    rm->modeReg   = (UInt8)saveStateGet(state, "modeReg",   0);
    rm->dacCtrl   = (UInt8)saveStateGet(state, "dacCtrl",   0);
    saveStateClose(state);

    sccLoadState(rm->scc);
    amdFlashLoadState(rm->flash);

    mapAllBanks(rm);
}

static UInt8 read(RomMapperFlashRomScc* rm, UInt16 address)
{
    UInt16 msxAddr = address + (UInt16)(rm->startPage << 13);

    if (msxAddr < 0x4000) {
        /* Control registers at 3800h-3FFFh are write-only. */
        return 0xFF;
    }

    if (isSCCAccess(rm, msxAddr)) {
        return sccRead(rm->scc, (UInt8)(msxAddr & 0xFF));
    }

    return amdFlashRead(rm->flash, msxAddrToFlashAddr(rm, msxAddr));
}

static UInt8 peek(RomMapperFlashRomScc* rm, UInt16 address)
{
    UInt16 msxAddr = address + (UInt16)(rm->startPage << 13);

    if (msxAddr < 0x4000) {
        return 0xFF;
    }
    if (isSCCAccess(rm, msxAddr)) {
        return sccPeek(rm->scc, (UInt8)(msxAddr & 0xFF));
    }
    /* amdFlashGetPage has no side effects on the flash command state. */
    return *amdFlashGetPage(rm->flash, msxAddrToFlashAddr(rm, msxAddr));
}

static void write(RomMapperFlashRomScc* rm, UInt16 address, UInt8 value)
{
    UInt16 msxAddr = address + (UInt16)(rm->startPage << 13);

    if (msxAddr < 0x4000) {
        if (msxAddr >= 0x3800) {
            /* Write-only control registers: A2 selects the Operating-Mode
            ** register (3804h), otherwise A0 picks the low/high byte of
            ** the 14-bit Offset register (3800h/3801h, mirrored). */
            if (msxAddr & 0x04) {
                rm->modeReg = value & 0x03;
            }
            else {
                UInt16 newOffset;
                if (msxAddr & 0x01) {
                    newOffset = (UInt16)((rm->offsetReg & 0x00FF) | ((value & 0x3F) << 8));
                }
                else {
                    newOffset = (UInt16)((rm->offsetReg & 0x3F00) | value);
                }
                if (rm->offsetReg != newOffset) {
                    rm->offsetReg = newOffset;
                    /* The offset is applied immediately to all bank registers. */
                    mapAllBanks(rm);
                }
            }
        }
        return;
    }

    /* SCC sound registers plus the repurposed test-register area (98FBh
    ** Knm_DAC_Ctrl, 98FCh DAC_Data) when segment 3Fh is on page 2. */
    if (isSCCAccess(rm, msxAddr)) {
        UInt8 reg = (UInt8)(msxAddr & 0xFF);
        if (reg == 0xFB) {
            rm->dacCtrl = value;
        }
        else if (reg == 0xFC) {
            dacWrite(rm->dac, DAC_CH_MONO, value);
        }
        else {
            sccWrite(rm->scc, reg, value);
        }
    }

    /* Konami DAC window at 4000h-4FFFh: while enabled those writes go to
    ** the DAC instead of the flash command decoder.  All other writes
    ** reach the flash through the current bank mapping, as on hardware. */
    if ((rm->dacCtrl & KNM_DAC_EN) && msxAddr < 0x5000) {
        dacWrite(rm->dac, DAC_CH_MONO, value);
    }
    else {
        amdFlashWrite(rm->flash, msxAddrToFlashAddr(rm, msxAddr), value);
    }

    /* Bank registers at 5000h/7000h/9000h/B000h, mirrored through x7FFh. */
    if ((msxAddr & 0x1800) == 0x1000) {
        int page = (msxAddr - 0x4000) >> 13;
        UInt16 newBank;

        if (rm->modeReg & OPMODE_16BIT) {
            /* 16-bit mode: A0 selects the low or high register byte. */
            if (msxAddr & 0x01) {
                newBank = (UInt16)((rm->bankReg[page] & 0x00FF) | ((value & 0x3F) << 8));
            }
            else {
                newBank = (UInt16)((rm->bankReg[page] & 0x3F00) | value);
            }
        }
        else {
            /* 8-bit mode: Konami SCC compatible. */
            newBank = value;
        }

        if (rm->bankReg[page] != newBank) {
            rm->bankReg[page] = newBank;
            mapBank(rm, page);
        }
    }
}

int romMapperFlashRomSccCreate(const char* filename, UInt8* romData,
                               int size, int slot, int sslot, int startPage)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperFlashRomScc* rm;

    /* The address decode (control registers at 3800h-3FFFh, cartridge
    ** window at 4000h-BFFFh) assumes the standard cartridge mapping. */
    if (size < 0 || startPage != 2) {
        return 0;
    }
    if (size > FLASHROMSCC_FLASH_SIZE) {
        size = FLASHROMSCC_FLASH_SIZE;
    }

    rm = calloc(1, sizeof(RomMapperFlashRomScc));
    rm->deviceHandle = deviceManagerRegister(ROM_FLASHROMSCC, &callbacks, rm);

    /* Register one extra page below 4000h so the write-only control
    ** registers at 3800h-3FFFh are reachable. */
    rm->startPage = startPage - 1;
    slotRegister(slot, sslot, rm->startPage, 5, read, peek, write, destroy, rm);

    rm->flash = amdFlashCreate(AMD_TYPE_3, FLASHROMSCC_FLASH_SIZE, FLASHROMSCC_FLASH_SECTOR,
                               0, romData, size,
                               sramCreateFilenameWithSuffix(filename, "", ".sram"), 1);

    rm->romMask = FLASHROMSCC_FLASH_SIZE / 0x2000 - 1;   /* 0x7FF */
    rm->slot  = slot;
    rm->sslot = sslot;

    rm->scc = sccCreate(boardGetMixer());
    sccSetMode(rm->scc, SCC_REAL);
    rm->dac = dacCreate(boardGetMixer(), DAC_MONO);

    /* Control-register page (2000h-3FFFh): no data mapped, writes must
    ** reach the write callback. */
    slotMapPage(rm->slot, rm->sslot, rm->startPage, NULL, 0, 0);
    reset(rm);

    return 1;
}
