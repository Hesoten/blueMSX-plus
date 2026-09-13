/*****************************************************************************
**
** Yamanooto flash + SCC + PSG cartridge.
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
/* Reference: https://genami.shop/blogs/news/programming-the-yamanooto
** Not implemented: FPGA SD storage path. */
#include "romMapperYamanooto.h"
#include "AmdFlash.h"
#include "AY8910.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "IoPort.h"
#include "sramLoader.h"
#include "SCC.h"
#include "Board.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>

#define YAMANOOTO_FLASH_SIZE     0x800000    /* 8 MB */
#define YAMANOOTO_FLASH_SECTOR   0x10000     /* 64 KB */

/* ENAR (7FFF) bits.  Names taken from yamaflash's YAMA.Z8A. */
#define REGEN     0x01     /* enable subsequent CFGR/OFFR writes */
#define SDEN      0x02     /* SDCON routing on 0x7FFE (SD controller access) */
#define MSTEN     0x04     /* MOFFR routing on 0x7FFE (master offset register) */
#define WREN      0x10     /* flash write mode (routes writes to flash) */

/* SDCON (0x7FFE when SDEN=1) status bits.  Stub: RDY is always signalled
** so YAMDET succeeds; card-present and CS bits stay 0 (no SD emulated). */
#define SDCON_CS  0x10
#define SDCON_CD  0x40
#define SDCON_RDY 0x80

/* CFGR (7FFD) bits */
#define MDIS      0x01     /* mapper disable */
#define ECHO      0x02     /* PSG mirror on 0xA0-0xA1 */
#define ROMDIS    0x04     /* ROM disable */
#define K4        0x08     /* K4 mapper mode (else K5) */
#define SUBOFF    0x30     /* offset low bits (combined with offsetReg) */
#define FPGA_EN   0x40     /* FPGA_REG enable */
#define FPGA_WAIT 0x80     /* FPGA ready (always signalled ready) */

/* SPI PROM (Adesto AT45DB041) JEDEC 0x9F Read ID response, MSB-first.
** Index 0 = pre-cmd idle; index 1 = byte clocked in during the 0x9F cmd
** transfer (junk); indices 2+ = MFR (0x1F Adesto), DEV1 (0x23 AT45DB041),
** DEV2, DEV3. */
static const UInt8 FPGA_ID[6] = { 0xFF, 0x00, 0x1F, 0x23, 0x00, 0x00 };

typedef struct {
    int       deviceHandle;
    AmdFlash* flash;
    SCC*      scc;
    AY8910*   ay8910;
    int       slot;
    int       sslot;
    int       startPage;
    UInt32    romMask;             /* (FLASH_SIZE / 0x2000) - 1 = 0x3FF */

    UInt16    bankReg[4];          /* 10-bit effective bank number */
    UInt8     rawBank[4];          /* value written by software */
    UInt8     configReg;
    UInt8     offsetReg;
    UInt8     masterOffsetReg;     /* MOFFR - accessed at 0x7FFE when MSTEN=1 */
    UInt8     enableReg;
    UInt8     sccMode;
    UInt8     psgLatch;
    UInt8     fpgaFsm;
} RomMapperYamanooto;

/* SCC / SCC+ visibility is gated by BOTH the sccMode bit and the RAW bank
** register (offset-adjusted bankReg would incorrectly show SCC+ after OFFR
** changes).  SCC+ range excludes 0xBFFE-0xBFFF (mode register). */
static int isSCCAccess(const RomMapperYamanooto* rm, UInt16 msxAddr)
{
    /* Konami-4 mapper mode has no SCC on real hardware. */
    if (rm->configReg & K4) {
        return 0;
    }
    if (rm->sccMode & 0x20) {
        /* SCC+ mode: 0xB800..0xBFFD, gated by rawBank[3] bit 7 */
        return (rm->rawBank[3] & 0x80) &&
               (msxAddr >= 0xB800) && (msxAddr < 0xBFFE);
    }
    /* SCC compat mode: 0x9800..0x9FFF, gated by rawBank[2] & 0x3F == 0x3F */
    return ((rm->rawBank[2] & 0x3F) == 0x3F) &&
           (msxAddr >= 0x9800) && (msxAddr < 0xA000);
}

static void mapBank(RomMapperYamanooto* rm, int page)
{
    UInt32 bankNum = rm->bankReg[page] & rm->romMask;
    UInt8* bankData = amdFlashGetPage(rm->flash, bankNum * 0x2000);
    /* readEnable=0: reads route through the read callback so SCC ranges
    ** and flash-ident responses are intercepted before hitting the ROM. */
    slotMapPage(rm->slot, rm->sslot, rm->startPage + page, bankData, 0, 0);
}

static void updateSccChipMode(RomMapperYamanooto* rm)
{
    sccSetMode(rm->scc, (rm->sccMode & 0x20) ? SCC_PLUS : SCC_REAL);
}

static void applyBankWrite(RomMapperYamanooto* rm, int page, UInt8 value)
{
    UInt16 effective;
    UInt32 offset;

    /* offsetReg supplies bits 2..9 (32KB units), CFGR SUBOFF supplies bits 0..1. */
    offset = ((UInt32)rm->offsetReg << 2) | ((rm->configReg & SUBOFF) >> 4);

    rm->rawBank[page] = value;
    effective = (UInt16)((value + offset) & 0x3FF);

    if (rm->bankReg[page] != effective) {
        rm->bankReg[page] = effective;
        mapBank(rm, page);
    }
}

static UInt8 ioRead(RomMapperYamanooto* rm, UInt16 ioPort);
static void ioWrite(RomMapperYamanooto* rm, UInt16 ioPort, UInt8 value);

static void writeConfigReg(RomMapperYamanooto* rm, UInt8 value)
{
    UInt8 changed = (UInt8)(rm->configReg ^ value);
    rm->configReg = value;
    if ((changed & FPGA_EN) && !(value & FPGA_EN)) {
        /* CS deassert: reset SPI shift-register state so the next Read ID
        ** command starts from a clean idle position. */
        rm->fpgaFsm = 0;
    }
    if (changed & ECHO) {
        /* Mirror MSX PSG writes on 0xA0-0xA1 when ECHO enabled. */
        if (value & ECHO) {
            ioPortRegister(0xA0, NULL, ioWrite, rm);
            ioPortRegister(0xA1, NULL, ioWrite, rm);
        } else {
            ioPortUnregister(0xA0);
            ioPortUnregister(0xA1);
        }
    }
    if (changed & (MDIS | ROMDIS | K4)) {
        /* Mapper/ROM mode changed - remap all pages to reflect current state. */
        int i;
        for (i = 0; i < 4; i++) {
            mapBank(rm, i);
        }
    }
}

static void destroy(RomMapperYamanooto* rm)
{
    amdFlashDestroy(rm->flash);
    sccDestroy(rm->scc);
    if (rm->ay8910) {
        ay8910Destroy(rm->ay8910);
    }
    ioPortUnregister(0x10);
    ioPortUnregister(0x11);
    ioPortUnregister(0x12);
    if (rm->configReg & ECHO) {
        ioPortUnregister(0xA0);
        ioPortUnregister(0xA1);
    }
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);
    free(rm);
}

static void reset(RomMapperYamanooto* rm)
{
    int i;
    rm->enableReg = 0;
    rm->offsetReg = 0;
    rm->masterOffsetReg = 0;
    rm->psgLatch = 0;
    rm->fpgaFsm = 0;
    rm->sccMode = 0;
    writeConfigReg(rm, 0);
    for (i = 0; i < 4; i++) {
        rm->rawBank[i] = (UInt8)i;
        rm->bankReg[i] = (UInt16)i;
    }
    updateSccChipMode(rm);
    amdFlashReset(rm->flash);
    sccReset(rm->scc);
    if (rm->ay8910) {
        ay8910Reset(rm->ay8910);
    }
    for (i = 0; i < 4; i++) {
        mapBank(rm, i);
    }
}

static void saveState(RomMapperYamanooto* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperYamanooto");
    char tag[16];
    int i;

    for (i = 0; i < 4; i++) {
        sprintf(tag, "bankReg%d", i);
        saveStateSet(state, tag, rm->bankReg[i]);
        sprintf(tag, "rawBank%d", i);
        saveStateSet(state, tag, rm->rawBank[i]);
    }
    saveStateSet(state, "configReg", rm->configReg);
    saveStateSet(state, "offsetReg", rm->offsetReg);
    saveStateSet(state, "masterOffsetReg", rm->masterOffsetReg);
    saveStateSet(state, "enableReg", rm->enableReg);
    saveStateSet(state, "sccMode",   rm->sccMode);
    saveStateSet(state, "psgLatch",  rm->psgLatch);
    saveStateSet(state, "fpgaFsm",   rm->fpgaFsm);
    saveStateClose(state);

    sccSaveState(rm->scc);
    if (rm->ay8910) {
        ay8910SaveState(rm->ay8910);
    }
    amdFlashSaveState(rm->flash);
}

static void loadState(RomMapperYamanooto* rm)
{
    SaveState* state = saveStateOpenForRead("mapperYamanooto");
    char tag[16];
    int i;

    for (i = 0; i < 4; i++) {
        sprintf(tag, "bankReg%d", i);
        rm->bankReg[i] = (UInt16)saveStateGet(state, tag, i);
        sprintf(tag, "rawBank%d", i);
        rm->rawBank[i] = (UInt8)saveStateGet(state, tag, i);
    }
    rm->configReg = (UInt8)saveStateGet(state, "configReg", 0);
    rm->offsetReg = (UInt8)saveStateGet(state, "offsetReg", 0);
    rm->masterOffsetReg = (UInt8)saveStateGet(state, "masterOffsetReg", 0);
    rm->enableReg = (UInt8)saveStateGet(state, "enableReg", 0);
    rm->sccMode   = (UInt8)saveStateGet(state, "sccMode",   0);
    rm->psgLatch  = (UInt8)saveStateGet(state, "psgLatch",  0);
    rm->fpgaFsm   = (UInt8)saveStateGet(state, "fpgaFsm",   0);
    saveStateClose(state);

    sccLoadState(rm->scc);
    if (rm->ay8910) {
        ay8910LoadState(rm->ay8910);
    }
    amdFlashLoadState(rm->flash);

    updateSccChipMode(rm);
    for (i = 0; i < 4; i++) {
        mapBank(rm, i);
    }
}

static UInt32 msxAddrToFlashAddr(RomMapperYamanooto* rm, UInt16 msxAddr)
{
    int page = (msxAddr - 0x4000) >> 13;
    if (page < 0 || page >= 4) {
        return 0;
    }
    return (UInt32)(rm->bankReg[page] & rm->romMask) * 0x2000 + (msxAddr & 0x1FFF);
}

/* Register readback for 0x7FFC-0x7FFF.  Gated by REGEN: when REGEN=0 the
** caller falls through to the flash-mapped read.  Matches openMSX
** Yamanooto.cc plus a SDCON stub for yamaflash's YAMDET (RDY always set). */
static UInt8 readRegister(const RomMapperYamanooto* rm, UInt16 msxAddr)
{
    switch (msxAddr) {
    case 0x7FFF:
        return rm->enableReg;
    case 0x7FFE:
        if (rm->enableReg & SDEN)  return SDCON_RDY;
        if (rm->enableReg & MSTEN) return rm->masterOffsetReg;
        return rm->offsetReg;
    case 0x7FFD:
        return (UInt8)(rm->configReg | FPGA_WAIT);
    case 0x7FFC:
        if (!(rm->configReg & FPGA_EN)) return 0xFF;
        return (rm->fpgaFsm < sizeof(FPGA_ID)) ? FPGA_ID[rm->fpgaFsm] : 0x00;
    default:
        return 0xFF;
    }
}

static UInt8 read(RomMapperYamanooto* rm, UInt16 address)
{
    UInt16 msxAddr = address + 0x4000;

    if (msxAddr >= 0x7FFC && msxAddr <= 0x7FFF && (rm->enableReg & REGEN)) {
        return readRegister(rm, msxAddr);
    }

    /* SCC / SCC+ read range: 0x9800-0x9FFF (SCC compat) or 0xB800-0xBFFD
    ** (SCC+), gated by sccMode and the raw bank register. */
    if (isSCCAccess(rm, msxAddr)) {
        return sccRead(rm->scc, (UInt8)(msxAddr & 0xFF));
    }

    if (rm->configReg & ROMDIS) {
        return 0xFF;
    }
    return amdFlashRead(rm->flash, msxAddrToFlashAddr(rm, msxAddr));
}

static UInt8 peek(RomMapperYamanooto* rm, UInt16 address)
{
    UInt16 msxAddr = address + 0x4000;

    if (msxAddr >= 0x7FFC && msxAddr <= 0x7FFF && (rm->enableReg & REGEN)) {
        return readRegister(rm, msxAddr);
    }
    if (isSCCAccess(rm, msxAddr)) {
        return sccPeek(rm->scc, (UInt8)(msxAddr & 0xFF));
    }
    if (rm->configReg & ROMDIS) {
        return 0xFF;
    }
    {
        UInt8* p = amdFlashGetPage(rm->flash, msxAddrToFlashAddr(rm, msxAddr));
        return *p;
    }
}

static void write(RomMapperYamanooto* rm, UInt16 address, UInt8 value)
{
    UInt16 msxAddr = address + 0x4000;

    /* Register accesses at 7FFC-7FFF.  ENAR (7FFF) is always writable; the
    ** others require REGEN in the current enableReg. */
    if (msxAddr == 0x7FFF) {
        rm->enableReg = value;
        return;
    }
    if (msxAddr == 0x7FFE) {
        if (rm->enableReg & REGEN) {
            if (rm->enableReg & SDEN) {
                /* SDCON write - stub, discarded (no SD emulated). */
            } else if (rm->enableReg & MSTEN) {
                rm->masterOffsetReg = value;
            } else {
                rm->offsetReg = value;
            }
        }
        return;
    }
    if (msxAddr == 0x7FFD) {
        if (rm->enableReg & REGEN) {
            writeConfigReg(rm, value);
        }
        return;
    }
    if (msxAddr == 0x7FFC) {
        if (rm->configReg & FPGA_EN) {
            /* Emulate SPI PROM Read ID (0x9F): on cmd write, arm the FSM;
            ** each subsequent byte write clocks the next response byte
            ** into the shift register (read at 0x7FFC returns it). */
            if (rm->fpgaFsm == 0) {
                if (value == 0x9F) rm->fpgaFsm = 1;
            } else if (rm->fpgaFsm < sizeof(FPGA_ID) - 1) {
                rm->fpgaFsm++;
            } else {
                rm->fpgaFsm = 0;
            }
        }
        return;
    }

    /* WREN gates the write mode: WREN=1 routes writes to flash programming;
    ** WREN=0 routes to normal K5/K4 bank switching and SCC access. */
    if (rm->enableReg & WREN) {
        if (!(rm->configReg & ROMDIS)) {
            amdFlashWrite(rm->flash, msxAddrToFlashAddr(rm, msxAddr), value);
        }
        return;
    }

    if (rm->configReg & K4) {
        /* K4 mode: writes to 6000/8000/A000 in respective pages set bank. */
        if ((rm->configReg & MDIS) == 0 && msxAddr >= 0x6000) {
            int page = (msxAddr - 0x4000) >> 13;
            if (page >= 1 && page < 4) {
                applyBankWrite(rm, page, value & 0xFF);
            }
        }
        return;
    }

    /* K5 mode: SCC access, bank switch, and mode register. */
    if (isSCCAccess(rm, msxAddr)) {
        sccWrite(rm->scc, (UInt8)(msxAddr & 0xFF), value);
    }
    if ((msxAddr & 0x1800) == 0x1000 && (rm->configReg & MDIS) == 0) {
        /* Bank switch at 5000/7000/9000/B000. */
        int page = (msxAddr - 0x4000) >> 13;
        if (page >= 0 && page < 4) {
            applyBankWrite(rm, page, value & 0xFF);
        }
    }
    if ((msxAddr & 0xFFFE) == 0xBFFE) {
        /* SCC mode register (0xBFFE mirrored to 0xBFFF). */
        rm->sccMode = value;
        updateSccChipMode(rm);
    }
}

static UInt8 ioRead(RomMapperYamanooto* rm, UInt16 ioPort)
{
    (void)ioPort;
    return ay8910ReadData(rm->ay8910, 0x12);
}

static void ioWrite(RomMapperYamanooto* rm, UInt16 ioPort, UInt8 value)
{
    /* Ports 0x10 / 0x11 (secondary PSG) and 0xA0 / 0xA1 (ECHO mirror of MSX
    ** PSG) share semantics: even = address latch, odd = data write. */
    if (ioPort & 1) {
        ay8910WriteData(rm->ay8910, ioPort, value);
    } else {
        rm->psgLatch = value & 0x0F;
        ay8910WriteAddress(rm->ay8910, ioPort, value);
    }
}

int romMapperYamanootoCreate(const char* filename, UInt8* romData,
                             int size, int slot, int sslot, int startPage)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperYamanooto* rm;
    int i;

    if (size < 0) {
        return 0;
    }
    if (size > YAMANOOTO_FLASH_SIZE) {
        size = YAMANOOTO_FLASH_SIZE;
    }

    rm = calloc(1, sizeof(RomMapperYamanooto));
    rm->deviceHandle = deviceManagerRegister(ROM_YAMANOOTO, &callbacks, rm);
    slotRegister(slot, sslot, startPage, 4, read, peek, write, destroy, rm);

    rm->flash = amdFlashCreate(AMD_TYPE_2, YAMANOOTO_FLASH_SIZE, YAMANOOTO_FLASH_SECTOR,
                               0, romData, size,
                               sramCreateFilenameWithSuffix(filename, "", ".sram"), 1);

    rm->romMask = YAMANOOTO_FLASH_SIZE / 0x2000 - 1;   /* 0x3FF */
    rm->slot  = slot;
    rm->sslot = sslot;
    rm->startPage = startPage;

    rm->scc = sccCreate(boardGetMixer());
    sccSetMode(rm->scc, SCC_REAL);

    rm->ay8910 = ay8910Create(boardGetMixer(), AY8910_NONE, PSGTYPE_AY8910, 0, NULL);

    for (i = 0; i < 4; i++) {
        rm->rawBank[i] = (UInt8)i;
        rm->bankReg[i] = (UInt16)i;
    }
    rm->configReg = 0;
    rm->offsetReg = 0;
    rm->masterOffsetReg = 0;
    rm->enableReg = 0;
    rm->sccMode = 0;
    rm->psgLatch = 0;
    rm->fpgaFsm = 0;

    updateSccChipMode(rm);
    for (i = 0; i < 4; i++) {
        mapBank(rm, i);
    }

    ioPortRegister(0x10, NULL, ioWrite, rm);
    ioPortRegister(0x11, NULL, ioWrite, rm);
    ioPortRegister(0x12, ioRead, NULL, rm);

    return 1;
}
