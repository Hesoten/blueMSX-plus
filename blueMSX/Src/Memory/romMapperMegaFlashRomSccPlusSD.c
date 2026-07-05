/*****************************************************************************
**
** MegaFlashROM SCC+ SD cartridge (AmdFlash 8 MB + SCC+ + SD).
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
/* 8 MB flash split across four subslots:
**   Subslot 0:  recovery firmware (flat 16 KB at flash[0..0x3FFF])
**   Subslot 1:  1 MB MegaFlashROM SCC+ (Konami-SCC / Konami / 64K /
**               ASCII-8 / ASCII-16); hosts configReg / mapperReg /
**               offsetReg I/O.
**   Subslot 2:  MegaRAM, 512 KB memory mapper
**   Subslot 3:  MegaSD with two SD card slots (ASCII-8 mapper)
*/

#include "romMapperMegaFlashRomSccPlusSD.h"
#include "AmdFlash.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "IoPort.h"
#include "sramLoader.h"
#include "RomLoader.h"
#include "AY8910.h"
#include "SCC.h"
#include "Board.h"
#include "SaveState.h"
#include "SdCard.h"
#include "Disk.h"
#include "ramMapperIo.h"
#include "../Media/Sha1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ------------------- Constants ------------------- */

#define FLASH_SIZE            0x800000    /* 8 MB */
#define MEGARAM_SIZE          0x080000    /* 512 KB MegaRAM */
#define MEGARAM_MASK          (MEGARAM_SIZE / 0x4000 - 1)   /* 16 KB-page mask */

/* Flash base offsets per subslot. */
#define BASE_SUBSLOT1         0x010000    /* MFR SCC+ main flash */
#define BASE_SUBSLOT3         0x700000    /* MegaSD / Nextor */

/* Factory dump SHA-1 (matches openMSX's <rom><sha1> tag for mfrsd.rom). */
#define MFRSD_FACTORY_SHA1    "1621f623b834dc57cb2983f30b36bcc3ac56cafd"

/* SCC enable values matching the existing blueMSX SCC backend. */
typedef enum {
    EN_NONE = 0,
    EN_SCC,
    EN_SCCPLUS
} SCCEnable;


typedef struct {
    int deviceHandle;
    int debugHandle;

    int slot;
    int sslot;
    int startPage;

    AmdFlash* flash;

    SCC*    scc;
    AY8910* ay8910;
    UInt8   psgLatch;

    SdCard* sdCard[2];
    int     sdDiskId[2];
    int     selectedCard;

    /* Cartridge-internal registers.  The subslot register at MSX
    ** address 0xFFFF is handled by blueMSX's slotManager once we mark
    ** the parent slot as subslotted, so we don't store it here. */
    UInt8  configReg;
    UInt8  mapperReg;
    UInt16 offsetReg;        /* 10 bits */
    UInt8  sccMode;
    UInt8  sccBanks[4];
    UInt16 bankRegsSubSlot1[4];
    UInt8  bankRegsSubSlot3[4];
    UInt8  memMapperRegs[4];
    int    memMapperIoHandle;

    UInt8* megaRam;
} RomMapperMfrSccSd;


/* ------------------- Config-register predicates ------------------- */

/* configReg (#7FFC) bit map (mirrored by openMSX):
**   bit 0 (0x01): flash ROM write enable
**   bit 1 (0x02): flash boot-block protect (VPP_WD pin) -- WP# only,
**                 NOT a bank-reg lock
**   bit 2 (0x04): subslot expander disabled (set => slot is flat, not
**                 expanded into the 4 internal subslots)
**   bit 3 (0x08): PSG also mapped to ports 0xA0..0xA3
**   bit 4 (0x10): DSK mode (remaps banks 0 and 1)
**   bit 5 (0x20): memory mapper (MegaRAM) disabled
**   bit 7 (0x80): config register itself locked (further #7FFC writes
**                 ignored). NOTE: bit 2 was incorrectly used here in
**                 earlier code, but bits 2 and 7 are independent --
**                 disabling the subslot expander does NOT lock the
**                 config reg, and flashers / OPFXSD firmware flip bit 2
**                 routinely while still needing to write the rest of
**                 configReg.
**
** mapperReg (#7FFF) bit map:
**   bit 0 (0x01): Konami / Konami-SCC mapper bank-register limits
**   bit 1 (0x02): mapper and offset registers disabled (bank-reg lock)
**   bit 2 (0x04): mapperReg itself locked
**   bit 3 (0x08): Konami #4000-#5FFF bank-register write disabled
**   bits 5..7 (0xE0): mapper mode select
**     0x00 = Konami-SCC
**     0x20 = Konami
**     0x40 / 0x60 = 64 KB linear
**     0x80 / 0xA0 = ASCII-8
**     0xC0 / 0xE0 = ASCII-16
*/

static int isFlashRomWriteEnabled(RomMapperMfrSccSd* rm)        { return  (rm->configReg & 0x01) != 0; }
static int isConfigRegDisabled(RomMapperMfrSccSd* rm)           { return  (rm->configReg & 0x80) != 0; }
static int isMapperRegisterDisabled(RomMapperMfrSccSd* rm)      { return  (rm->mapperReg & 0x04) != 0; }
static int areBankRegsAndOffsetRegsDisabled(RomMapperMfrSccSd* rm){return (rm->mapperReg & 0x02) != 0; }
static int isSlotExpanderEnabled(RomMapperMfrSccSd* rm)         { return !(rm->configReg & 0x04); }
static int isMemoryMapperEnabled(RomMapperMfrSccSd* rm)         { return !(rm->configReg & 0x20); }
static int isPsgAlsoMappedToNormalPorts(RomMapperMfrSccSd* rm)  { return  (rm->configReg & 0x08) != 0; }
static int isDskModeEnabled(RomMapperMfrSccSd* rm)              { return  (rm->configReg & 0x10) != 0; }
static int isKonamiSCCmapperConfigured(RomMapperMfrSccSd* rm)   { return  (rm->mapperReg & 0xE0) == 0x00; }
static int isKonamiMapperConfigured(RomMapperMfrSccSd* rm)      { return  (rm->mapperReg & 0xE0) == 0x20; }
static int is64KmapperConfigured(RomMapperMfrSccSd* rm)         { return  (rm->mapperReg & 0xC0) == 0x40; }
static int isAscii8Configured(RomMapperMfrSccSd* rm)            { return  (rm->mapperReg & 0xC0) == 0x80; }
static int isAscii16Configured(RomMapperMfrSccSd* rm)           { return  (rm->mapperReg & 0xC0) == 0xC0; }
static int areKonamiMapperLimitsEnabled(RomMapperMfrSccSd* rm)  { return  (rm->mapperReg & 0x01) != 0; }
static int isWritingKonamiBankRegisterDisabled(RomMapperMfrSccSd* rm){return(rm->mapperReg & 0x08) != 0; }


static void writeToFlash(RomMapperMfrSccSd* rm, UInt32 flashAddr, UInt8 value)
{
    if (isFlashRomWriteEnabled(rm)) {
        amdFlashWrite(rm->flash, flashAddr, value);
    }
}


/* ------------------- SCC enable detection (subslot 1) ------------------- */

static SCCEnable getSCCEnable(RomMapperMfrSccSd* rm)
{
    if ((rm->sccMode & 0x20) && (rm->sccBanks[3] & 0x80)) return EN_SCCPLUS;
    if (!(rm->sccMode & 0x20) && ((rm->sccBanks[2] & 0x3F) == 0x3F)) return EN_SCC;
    return EN_NONE;
}


/* ------------------- Flash address for subslot 1 ------------------- */

static UInt32 getFlashAddrSubSlot1(RomMapperMfrSccSd* rm, UInt16 addr)
{
    int    is64K = is64KmapperConfigured(rm);
    /* 64K linear mode: 16KB pages spanning the full 0x0000-0xFFFF.
    ** All other modes: 8KB pages, valid only in 0x4000-0xBFFF; the
    ** out-of-range guard below returns 0xFFFFFFFFu so callers map
    ** unmapped reads to 0xFF (matches openMSX's unsigned(-1) sentinel). */
    int    page = is64K ? (addr >> 14) : ((addr >> 13) - 2);
    UInt32 size = is64K ? 0x4000 : 0x2000;
    UInt32 bank;
    UInt32 tmp;

    if (page < 0 || page >= 4) return 0xFFFFFFFFu;

    bank = rm->bankRegsSubSlot1[page];

    /* DSK mode (configReg bit 4) substitutes specific bank numbers so the
    ** OPF-XSD disk ROM at flash[0x7F4000-0x7F7FFF] is aliased to MSX
    ** pages 1-2 (0x4000-0x7FFF -> flash 0x4000-0x7FFF after the 8MB
    ** wrap). The substitution only fires when the bank register still
    ** has its reset value -- once the guest writes a different bank,
    ** offsetReg applies as usual. Nextor and other firmware rely on
    ** this aliasing to find the disk ROM. */
    if (isDskModeEnabled(rm) && page == 0 && bank == 0) {
        bank = 0x3FA;
    } else if (isDskModeEnabled(rm) && page == 1 && bank == 1) {
        bank = 0x3FB;
    } else {
        bank += rm->offsetReg;          /* in bank units, NOT bytes */
    }

    tmp = (bank * size) + (addr & (size - 1));
    return (tmp + BASE_SUBSLOT1) & (FLASH_SIZE - 1);
}


/* ------------------- Subslot 0 (flat 16 KB) ------------------- */

static UInt8 readMemSubSlot0(RomMapperMfrSccSd* rm, UInt16 addr)
{
    return amdFlashRead(rm->flash, addr & 0x3FFF);
}

static void writeMemSubSlot0(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 value)
{
    writeToFlash(rm, addr & 0x3FFF, value);
}


/* ------------------- Subslot 1 (MFR SCC+ main flash) ------------------- */

static UInt8 readMemSubSlot1(RomMapperMfrSccSd* rm, UInt16 addr)
{
    UInt32 flashAddr;

    if (isKonamiSCCmapperConfigured(rm)) {
        SCCEnable en = getSCCEnable(rm);
        if (((en == EN_SCC)     && addr >= 0x9800 && addr < 0xA000) ||
            ((en == EN_SCCPLUS) && addr >= 0xB800 && addr < 0xC000)) {
            return sccRead(rm->scc, (UInt8)(addr & 0xFF));
        }
    }

    flashAddr = getFlashAddrSubSlot1(rm, addr);
    return (flashAddr != 0xFFFFFFFFu) ? amdFlashRead(rm->flash, flashAddr) : 0xFF;
}

static UInt8 peekMemSubSlot1(RomMapperMfrSccSd* rm, UInt16 addr)
{
    UInt32 flashAddr;

    if (isKonamiSCCmapperConfigured(rm)) {
        SCCEnable en = getSCCEnable(rm);
        if (((en == EN_SCC)     && addr >= 0x9800 && addr < 0xA000) ||
            ((en == EN_SCCPLUS) && addr >= 0xB800 && addr < 0xC000)) {
            return sccPeek(rm->scc, (UInt8)(addr & 0xFF));
        }
    }

    flashAddr = getFlashAddrSubSlot1(rm, addr);
    return (flashAddr != 0xFFFFFFFFu) ? amdFlashRead(rm->flash, flashAddr) : 0xFF;
}

static void writeMemSubSlot1(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 value)
{
    UInt32 flashAddr = getFlashAddrSubSlot1(rm, addr);
    int page8;

    /* Configuration / mapper / offset register writes (all gated). */
    if (!isConfigRegDisabled(rm) && addr == 0x7FFC) {
        UInt8 old = rm->configReg;
        rm->configReg = value;
        /* configReg bit 2 toggles the cart's internal subslot expander.
        ** Mirror this into the slotManager so BIOS slot scans see the
        ** correct expansion state on the next probe -- a launcher that
        ** disables the expander and soft-resets relies on BIOS observing
        ** the slot as "flat" (= SLTTBL bit 7 cleared) to take its OK
        ** path. Without this sync, BIOS keeps reporting the slot as
        ** expanded and the launcher loops on re-disabling the expander
        ** + soft-reset forever. */
        if ((old ^ value) & 0x04) {
            slotSetSubslotted(rm->slot, isSlotExpanderEnabled(rm));
        }
    }
    if (!isMapperRegisterDisabled(rm) && addr == 0x7FFF) {
        rm->mapperReg = value;
    }
    if (!areBankRegsAndOffsetRegsDisabled(rm) && addr == 0x7FFD) {
        rm->offsetReg = (rm->offsetReg & 0x300) | value;
    }
    if (!areBankRegsAndOffsetRegsDisabled(rm) && addr == 0x7FFE) {
        rm->offsetReg = (rm->offsetReg & 0xFF) | ((UInt16)(value & 0x03) << 8);
    }

    /* SCC chip mode + SCC register writes (Konami-SCC mode only). */
    if (isKonamiSCCmapperConfigured(rm)) {
        SCCEnable en;
        int isRamSeg2, isRamSeg3;

        if ((addr & 0xFFFE) == 0xBFFE) {
            rm->sccMode = value;
            sccSetMode(rm->scc, (value & 0x20) ? SCC_PLUS : SCC_COMPATIBLE);
        }

        en        = getSCCEnable(rm);
        isRamSeg2 = ((rm->sccMode & 0x24) == 0x24) || ((rm->sccMode & 0x10) == 0x10);
        isRamSeg3 = ((rm->sccMode & 0x10) == 0x10);
        if (((en == EN_SCC)     && !isRamSeg2 && addr >= 0x9800 && addr < 0xA000) ||
            ((en == EN_SCCPLUS) && !isRamSeg3 && addr >= 0xB800 && addr < 0xC000)) {
            sccWrite(rm->scc, (UInt8)(addr & 0xFF), value);
            /* When SCC regs are selected, flash is masked off.  Drop the
            ** write before it can bump the AmdFlash command state machine. */
            return;
        }
    }

    page8 = (addr >> 13) - 2;
    if (page8 >= 0 && page8 < 4 && !areBankRegsAndOffsetRegsDisabled(rm)) {
        switch (rm->mapperReg & 0xE0) {
        case 0x00: { /* Konami-SCC */
            UInt8 mask;
            if ((addr & 0x1800) != 0x1000) break;
            rm->sccBanks[page8] = value;
            mask = areKonamiMapperLimitsEnabled(rm) ? 0x3F : 0xFF;
            rm->bankRegsSubSlot1[page8] = value & mask;
            break;
        }
        case 0x20: { /* Konami */
            UInt8 mask;
            if (isWritingKonamiBankRegisterDisabled(rm) && addr < 0x6000) break;
            if (addr < 0x5000 || (addr >= 0x5800 && addr < 0x6000)) break;
            mask = areKonamiMapperLimitsEnabled(rm) ? 0x1F : 0xFF;
            rm->bankRegsSubSlot1[page8] = value & mask;
            break;
        }
        case 0x40: case 0x60: /* 64 KB linear */
            rm->bankRegsSubSlot1[page8] = value;
            break;
        case 0x80: case 0xA0: /* ASCII-8 */
            if (addr >= 0x6000 && addr < 0x8000) {
                int b = (addr >> 11) & 0x03;
                rm->bankRegsSubSlot1[b] = value;
            }
            break;
        case 0xC0: case 0xE0: { /* ASCII-16 (9-bit bank reg) */
            UInt16 m = (1 << 9) - 1;
            if (addr >= 0x6000 && addr < 0x6800) {
                rm->bankRegsSubSlot1[0] = (2 * value + 0) & m;
                rm->bankRegsSubSlot1[1] = (2 * value + 1) & m;
            }
            if (addr >= 0x7000 && addr < 0x7800) {
                rm->bankRegsSubSlot1[2] = (2 * value + 0) & m;
                rm->bankRegsSubSlot1[3] = (2 * value + 1) & m;
            }
            break;
        }
        }
    }

    if (flashAddr != 0xFFFFFFFFu) {
        writeToFlash(rm, flashAddr, value);
    }
}


/* ------------------- Subslot 2 (MegaRAM) ------------------- */

static UInt32 calcMemMapperAddress(RomMapperMfrSccSd* rm, UInt16 addr)
{
    UInt8 bank = rm->memMapperRegs[addr >> 14];
    return ((UInt32)(bank & MEGARAM_MASK) << 14) | (addr & 0x3FFF);
}

static UInt8 readMemSubSlot2(RomMapperMfrSccSd* rm, UInt16 addr)
{
    return rm->megaRam[calcMemMapperAddress(rm, addr)];
}

static void writeMemSubSlot2(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 value)
{
    rm->megaRam[calcMemMapperAddress(rm, addr)] = value;
}

/* MSX memory mapper I/O broadcast (ports 0xFC-0xFF).  blueMSX dispatches
** writes to every device registered via ramMapperIoAdd; we keep a copy
** in memMapperRegs masked to the legal range for our 512 KB MegaRAM. */
static void writeMemMapperIo(void* ref, UInt16 page, UInt8 value)
{
    RomMapperMfrSccSd* rm = (RomMapperMfrSccSd*)ref;
    rm->memMapperRegs[page & 3] = value & MEGARAM_MASK;
}


/* ------------------- Subslot 3 (MegaSD) ------------------- */

static UInt32 getFlashAddrSubSlot3(RomMapperMfrSccSd* rm, UInt16 addr)
{
    int page8 = (addr >> 13) - 2;
    if (page8 < 0 || page8 >= 4) return 0xFFFFFFFFu;
    return ((UInt32)(rm->bankRegsSubSlot3[page8] & 0x7F) * 0x2000) +
           (addr & 0x1FFF) + BASE_SUBSLOT3;
}

static UInt8 readMemSubSlot3(RomMapperMfrSccSd* rm, UInt16 addr)
{
    UInt32 flashAddr;

    if (((rm->bankRegsSubSlot3[0] & 0xC0) == 0x40) &&
        addr >= 0x4000 && addr < 0x6000) {
        SdCard* card = rm->sdCard[rm->selectedCard];
        return (card == NULL) ? 0xFF
                              : sdCardTransferCs(card, 0xFF, (addr & 0x1000) != 0);
    }

    if (addr >= 0x4000 && addr < 0xC000) {
        flashAddr = getFlashAddrSubSlot3(rm, addr);
        if (flashAddr != 0xFFFFFFFFu) {
            return amdFlashRead(rm->flash, flashAddr);
        }
    }
    return 0xFF;
}

static void writeMemSubSlot3(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 value)
{
    if (((rm->bankRegsSubSlot3[0] & 0xC0) == 0x40) &&
        addr >= 0x4000 && addr < 0x6000) {
        SdCard* card = rm->sdCard[rm->selectedCard];
        if (addr >= 0x5800) {
            rm->selectedCard = value & 1;
        } else if (card != NULL) {
            (void)sdCardTransferCs(card, value, (addr & 0x1000) != 0);
        }
        return;
    }

    /* Flash write first (address before bank-reg update). */
    if (addr >= 0x4000 && addr < 0xC000) {
        UInt32 flashAddr = getFlashAddrSubSlot3(rm, addr);
        if (flashAddr != 0xFFFFFFFFu) {
            writeToFlash(rm, flashAddr, value);
        }
    }

    /* ASCII-8 bank-select. */
    if (addr >= 0x6000 && addr < 0x8000) {
        int b = (addr >> 11) & 0x03;
        rm->bankRegsSubSlot3[b] = value;
    }
}


/* ------------------- slotManager-facing thunks ------------------- */
/* The cart is registered at startPage=0 / numPages=8, i.e. full
** 0x0000-0xFFFF.  With startPage=0 the slotManager subtracts nothing,
** so the handler's addr parameter already is the MSX address.  The
** full-64K coverage matches openMSX's <mem base="0x0000" size="0x10000"/>:
** MegaRAM and the recovery 16K mirror need to respond at MSX pages 0
** and 3 too, not just 0x4000-0xBFFF where the cart firmware boots. */

/* When configReg bit 2 (subslot expander) is set, openMSX's getSubSlot()
** collapses all 4 subslot windows to subslot 1 (main flash) regardless
** of the subslotReg.  OPFXSD's /U auto-launch path relies on this:
** firmware disables the expander, configures mapperReg/offsetReg, then
** the launched ROM sees the main flash flat across the full 64KB.
** blueMSX's slotManager doesn't natively model dynamic expander toggle,
** so dispatch from each subslot's thunk: expander-disabled => subslot 1. */

static UInt8 thunkRead0(RomMapperMfrSccSd* rm, UInt16 addr)  { return isSlotExpanderEnabled(rm) ? readMemSubSlot0(rm, addr) : readMemSubSlot1(rm, addr); }
static UInt8 thunkPeek0(RomMapperMfrSccSd* rm, UInt16 addr)  { return isSlotExpanderEnabled(rm) ? amdFlashRead(rm->flash, addr & 0x3FFF) : peekMemSubSlot1(rm, addr); }
static void  thunkWrite0(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 v) { if (isSlotExpanderEnabled(rm)) writeMemSubSlot0(rm, addr, v); else writeMemSubSlot1(rm, addr, v); }

static UInt8 thunkRead1(RomMapperMfrSccSd* rm, UInt16 addr)  { return readMemSubSlot1(rm, addr); }
static UInt8 thunkPeek1(RomMapperMfrSccSd* rm, UInt16 addr)  { return peekMemSubSlot1(rm, addr); }
static void  thunkWrite1(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 v) { writeMemSubSlot1(rm, addr, v); }

static UInt8 thunkRead2(RomMapperMfrSccSd* rm, UInt16 addr)  { return !isSlotExpanderEnabled(rm) ? readMemSubSlot1(rm, addr) : (isMemoryMapperEnabled(rm) ? readMemSubSlot2(rm, addr) : 0xFF); }
static UInt8 thunkPeek2(RomMapperMfrSccSd* rm, UInt16 addr)  { return !isSlotExpanderEnabled(rm) ? peekMemSubSlot1(rm, addr) : (isMemoryMapperEnabled(rm) ? readMemSubSlot2(rm, addr) : 0xFF); }
static void  thunkWrite2(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 v) { if (!isSlotExpanderEnabled(rm)) writeMemSubSlot1(rm, addr, v); else if (isMemoryMapperEnabled(rm)) writeMemSubSlot2(rm, addr, v); }

static UInt8 thunkRead3(RomMapperMfrSccSd* rm, UInt16 addr)  { return isSlotExpanderEnabled(rm) ? readMemSubSlot3(rm, addr) : readMemSubSlot1(rm, addr); }
static UInt8 thunkPeek3(RomMapperMfrSccSd* rm, UInt16 addr)  { return isSlotExpanderEnabled(rm) ? readMemSubSlot3(rm, addr) : peekMemSubSlot1(rm, addr); }
static void  thunkWrite3(RomMapperMfrSccSd* rm, UInt16 addr, UInt8 v) { if (isSlotExpanderEnabled(rm)) writeMemSubSlot3(rm, addr, v); else writeMemSubSlot1(rm, addr, v); }

/* ------------------- SaveState ------------------- */

static void saveState(RomMapperMfrSccSd* rm)
{
    SaveState* state = saveStateOpenForWrite("mfrSccSdMapper");

    saveStateSet(state, "configReg",    rm->configReg);
    saveStateSet(state, "mapperReg",    rm->mapperReg);
    saveStateSet(state, "offsetReg",    rm->offsetReg);
    saveStateSet(state, "sccMode",      rm->sccMode);
    saveStateSet(state, "selectedCard", rm->selectedCard);
    saveStateSet(state, "psgLatch",     rm->psgLatch);
    saveStateSetBuffer(state, "sccBanks",       rm->sccBanks,        sizeof(rm->sccBanks));
    saveStateSetBuffer(state, "bankRegsSlot1",  rm->bankRegsSubSlot1, sizeof(rm->bankRegsSubSlot1));
    saveStateSetBuffer(state, "bankRegsSlot3",  rm->bankRegsSubSlot3, sizeof(rm->bankRegsSubSlot3));
    saveStateSetBuffer(state, "memMapperRegs",  rm->memMapperRegs,    sizeof(rm->memMapperRegs));
    saveStateSetBuffer(state, "megaRam",        rm->megaRam,          MEGARAM_SIZE);

    saveStateClose(state);

    sccSaveState(rm->scc);
    /* Use a cart-unique chunk tag: the cart's second PSG would otherwise
    ** collide with the main MSX PSG (both saved/loaded under "ay8910"),
    ** and the second writer wins -- mixing the two chips' register banks
    ** and garbling audio after restore. */
    if (rm->ay8910) ay8910SaveStateWithTag(rm->ay8910, "mfrSccSdPsg");
    amdFlashSaveState(rm->flash);
    if (rm->sdCard[0]) sdCardSaveState(rm->sdCard[0], "mfrSccSd0");
    if (rm->sdCard[1]) sdCardSaveState(rm->sdCard[1], "mfrSccSd1");
}

static void loadState(RomMapperMfrSccSd* rm)
{
    SaveState* state = saveStateOpenForRead("mfrSccSdMapper");

    rm->configReg     = (UInt8)saveStateGet(state, "configReg",    3);
    rm->mapperReg     = (UInt8)saveStateGet(state, "mapperReg",    0);
    rm->offsetReg     = (UInt16)saveStateGet(state, "offsetReg",   0);
    rm->sccMode       = (UInt8)saveStateGet(state, "sccMode",      0);
    rm->selectedCard  = saveStateGet(state, "selectedCard",         0);
    rm->psgLatch      = (UInt8)saveStateGet(state, "psgLatch",     0);
    saveStateGetBuffer(state, "sccBanks",       rm->sccBanks,        sizeof(rm->sccBanks));
    saveStateGetBuffer(state, "bankRegsSlot1",  rm->bankRegsSubSlot1, sizeof(rm->bankRegsSubSlot1));
    saveStateGetBuffer(state, "bankRegsSlot3",  rm->bankRegsSubSlot3, sizeof(rm->bankRegsSubSlot3));
    saveStateGetBuffer(state, "memMapperRegs",  rm->memMapperRegs,    sizeof(rm->memMapperRegs));
    saveStateGetBuffer(state, "megaRam",        rm->megaRam,          MEGARAM_SIZE);

    saveStateClose(state);

    sccLoadState(rm->scc);
    if (rm->ay8910) ay8910LoadStateWithTag(rm->ay8910, "mfrSccSdPsg");
    amdFlashLoadState(rm->flash);
    if (rm->sdCard[0]) sdCardLoadState(rm->sdCard[0], "mfrSccSd0");
    if (rm->sdCard[1]) sdCardLoadState(rm->sdCard[1], "mfrSccSd1");

    /* Sync the slotManager's subslot-expander state with the loaded
    ** configReg so BIOS slot probes after state load see the same
    ** expansion the cart had when the state was saved. */
    slotSetSubslotted(rm->slot, isSlotExpanderEnabled(rm));
}


/* ------------------- Lifecycle ------------------- */

static void destroy(RomMapperMfrSccSd* rm)
{
    amdFlashDestroy(rm->flash);
    /* Unregister all four subslot handlers, then return the parent
    ** slot to its un-expanded state so subsequent cart inserts behave
    ** like a vanilla MSX. */
    slotUnregister(rm->slot, 0, rm->startPage);
    slotUnregister(rm->slot, 1, rm->startPage);
    slotUnregister(rm->slot, 2, rm->startPage);
    slotUnregister(rm->slot, 3, rm->startPage);
    slotSetSubslotted(rm->slot, 0);
    deviceManagerUnregister(rm->deviceHandle);
    debugDeviceUnregister(rm->debugHandle);
    if (rm->ay8910) ay8910Destroy(rm->ay8910);
    sccDestroy(rm->scc);
    if (rm->sdCard[0]) sdCardDestroy(rm->sdCard[0]);
    if (rm->sdCard[1]) sdCardDestroy(rm->sdCard[1]);

    ioPortUnregister(0x10);
    ioPortUnregister(0x11);
    ioPortUnregister(0x12);

    ramMapperIoRemove(rm->memMapperIoHandle);

    free(rm->megaRam);
    free(rm);
}

static void reset(RomMapperMfrSccSd* rm)
{
    int i;

    amdFlashReset(rm->flash);
    sccReset(rm->scc);
    if (rm->ay8910) ay8910Reset(rm->ay8910);
    if (rm->sdCard[0]) sdCardReset(rm->sdCard[0]);
    if (rm->sdCard[1]) sdCardReset(rm->sdCard[1]);

    rm->mapperReg    = 0;
    rm->offsetReg    = 0;
    rm->configReg    = 3;       /* matches openMSX: FLASH_WE + VPP_WD set */
    rm->sccMode      = 0;
    rm->selectedCard = 0;
    rm->psgLatch     = 0;

    /* Re-assert subslot-expander state: configReg=3 has bit 2 clear, so
    ** the slot is expanded. Restores slotManager's view in case the
    ** previous firmware disabled it before the hardware reset. */
    slotSetSubslotted(rm->slot, 1);

    /* openMSX uses ranges::iota to fill bankRegsSubSlot1 and sccBanks
    ** with 0,1,2,3 (linear) so a Konami-SCC mapper sees the first 32KB
    ** of subslot 1's flash mapped contiguously at boot.  Subslot 3
    ** uses the explicit pattern bank[1]=1 / others=0 so the same 8KB
    ** is reflected at three pages until the firmware bank-switches. */
    for (i = 0; i < 4; i++) {
        rm->bankRegsSubSlot1[i] = (UInt16)i;
        rm->sccBanks[i]         = (UInt8)i;
        rm->memMapperRegs[i]    = (UInt8)(3 - i);
        rm->bankRegsSubSlot3[i] = (i == 1) ? 1 : 0;
    }
}


/* ------------------- PSG I/O ------------------- */

/* Cart's native PSG: address latch at #10, data write at #11, data
** read at #12.  Real hardware leaves the PSG write-only -- reading #12
** returns 0xFF (relied on by VGMPlay's "scan for MFRSD signature" cart
** detection, which works around the unreadable second PSG). */
static UInt8 ioRead(RomMapperMfrSccSd* rm, UInt16 port)
{
    (void)rm; (void)port;
    return 0xFF;
}

static void ioWrite(RomMapperMfrSccSd* rm, UInt16 port, UInt8 value)
{
    if ((port & 3) == 0) ay8910WriteAddress(rm->ay8910, port, value);
    if ((port & 3) == 1) ay8910WriteData(rm->ay8910, port, value);
}


/* ------------------- Debug glue ------------------- */

static void getDebugInfo(RomMapperMfrSccSd* rm, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts = dbgDeviceAddIoPorts(dbgDevice, "MFR SCC+ SD PSG", 3);
    dbgIoPortsAddPort(ioPorts, 0, 0x10, DBG_IO_WRITE, 0xff);
    dbgIoPortsAddPort(ioPorts, 1, 0x11, DBG_IO_WRITE, 0xff);
    dbgIoPortsAddPort(ioPorts, 2, 0x12, DBG_IO_READ,  0xff);
}


/* ------------------- Construction ------------------- */

int romMapperMegaFlashRomSccPlusSDCreate(int cartNo, int slot, int sslot, int startPage)
{
    DeviceCallbacks  callbacks    = { destroy, reset, saveState, loadState };
    DebugCallbacks   dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    RomMapperMfrSccSd* rm;
    int hdId0, hdId1;
    char sramPath[512];

    rm = calloc(1, sizeof(RomMapperMfrSccSd));

    rm->deviceHandle = deviceManagerRegister(ROM_MEGAFLSHSCCPLUS_SD, &callbacks, rm);
    rm->debugHandle  = debugDeviceRegister(DBGTYPE_AUDIO, "MFR SCC+ SD", &dbgCallbacks, rm);

    /* Real MFR SCC+ SD carries its own slot expander chip, so the cart
    ** has to mark the parent slot subslotted and present four separate
    ** subslots to blueMSX's slotManager (which then handles the 0xFFFF
    ** subslot register internally and dispatches reads/writes to the
    ** matching (slot, current_subslot) handler).  The sslot argument
    ** passed in is intentionally ignored: regardless of where the user
    ** "inserted" the cart, the cart occupies subslots 0..3 of the
    ** primary slot. */
    /* Cart is a slot expander -- it owns all 4 subslots of its primary
    ** slot and covers the full 0x0000-0xFFFF MSX address space.  Both
    ** the sslot and startPage args from the caller are intentionally
    ** ignored: regardless of where the menu "inserts" the cart, the
    ** real layout is fixed by the hardware. */
    (void)sslot;
    (void)startPage;
    slotSetSubslotted(slot, 1);
    slotRegister(slot, 0, 0, 8, thunkRead0, thunkPeek0, thunkWrite0, destroy, rm);
    slotRegister(slot, 1, 0, 8, thunkRead1, thunkPeek1, thunkWrite1, NULL,    rm);
    slotRegister(slot, 2, 0, 8, thunkRead2, thunkPeek2, thunkWrite2, NULL,    rm);
    slotRegister(slot, 3, 0, 8, thunkRead3, thunkPeek3, thunkWrite3, NULL,    rm);

    rm->slot      = slot;
    rm->sslot     = 0;
    rm->startPage = 0;

    rm->scc = sccCreate(boardGetMixer());
    sccSetMode(rm->scc, SCC_REAL);
    rm->ay8910 = ay8910Create(boardGetMixer(), AY8910_MSX, PSGTYPE_AY8910, 0, NULL);

    rm->megaRam = calloc(1, MEGARAM_SIZE);

    /* Persistent 8 MB flash: backing file is SRAM/megaflashromsccplussd.sram
    ** (lowercase filename mirrors openMSX so the dump is shareable).  When
    ** the .sram is missing, accept Machines/Shared Roms/mfrsd.rom as the
    ** factory seed iff its SHA-1 matches.  If neither is present the flash
    ** boots blank and the sram filename is withheld so the next session
    ** can retry the seed search instead of persisting our blank state. */
    {
        UInt8*      seedData = NULL;
        int         seedSize = 0;
        int         sramExists = 0;
        const char* persistName;
        FILE*       fchk;
        int         seedFileSize = 0;
        UInt8*      buf;

        sprintf(sramPath, "%s" DIR_SEPARATOR "megaflashromsccplussd.sram",
                boardGetBaseDirectory());

        fchk = fopen(sramPath, "rb");
        if (fchk != NULL) {
            sramExists = 1;
            fclose(fchk);
        }

        if (!sramExists) {
            /* mfrsd.rom isn't a full 8 MB blob -- trailing sectors are
            ** absent, so the SHA-1 covers the file's actual size (same as
            ** openMSX).  amdFlashCreate clamps to FLASH_SIZE and pads the
            ** rest with 0xFF. */
            buf = romLoad("Machines" DIR_SEPARATOR "Shared Roms"
                          DIR_SEPARATOR "mfrsd.rom", NULL, &seedFileSize);
            if (buf != NULL) {
                if (seedFileSize > 0) {
                    char hex[41];
                    calcSha1Hex(buf, (unsigned)seedFileSize, hex);
                    if (strcmp(hex, MFRSD_FACTORY_SHA1) == 0) {
                        seedData = buf;
                        seedSize = seedFileSize;
                        buf = NULL;
                    }
                }
                free(buf);
            }
        }

        persistName = (sramExists || seedData != NULL) ? sramPath : NULL;
        rm->flash = amdFlashCreate(AMD_TYPE_2, FLASH_SIZE, 0x10000, 0,
                                   seedData, seedSize, persistName, 0);
        free(seedData);
    }

    hdId0 = diskGetHdDriveId(cartNo, 0);
    hdId1 = diskGetHdDriveId(cartNo, 1);
    rm->sdDiskId[0] = hdId0;
    rm->sdDiskId[1] = hdId1;
    /* The HD disk-id slots are shared across cart types: if the user
    ** previously had MEGA-SCSI or another HDD-class cart in this slot,
    ** its image is still attached at these ids and the SD card layer
    ** would pick it up as a phantom SD card.  Detach explicitly so the
    ** user has to re-attach an SD image via the menu. */
    diskChange(hdId0, NULL, NULL);
    diskChange(hdId1, NULL, NULL);
    rm->sdCard[0] = sdCardCreate(hdId0);
    rm->sdCard[1] = sdCardCreate(hdId1);

    ioPortRegister(0x10, NULL, ioWrite, rm);
    ioPortRegister(0x11, NULL, ioWrite, rm);
    ioPortRegister(0x12, ioRead, NULL,  rm);

    /* Hook into the broadcast 0xFC-0xFF mapper I/O bus so writes update
    ** memMapperRegs[] -- without this the Subslot 2 MegaRAM bank regs
    ** never move from their reset defaults and the OS sees only the
    ** first 64 KB. */
    rm->memMapperIoHandle = ramMapperIoAdd(MEGARAM_SIZE, writeMemMapperIo, rm);

    reset(rm);

    return 1;
}
