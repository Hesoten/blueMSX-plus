/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Board/MSX.c,v $
**
** $Revision: 1.71 $
**
** $Date: 2008-04-18 04:09:54 $
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MSX.h"

#include "R800.h"
#include "R800Dasm.h"
#include "R800SaveState.h"
#include "R800Debug.h"

#include "SaveState.h"
#include "MsxPPI.h"
#include "Board.h"
#include "RTC.h"
#include "MsxPsg.h"
#include "VDP.h"
#include "Casette.h"
#include "Disk.h"
#include "MegaromCartridge.h"
#include "IoPort.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "ramMapperIo.h"
#include "CoinDevice.h"
#include "romMapperDRAM.h"

void PatchZ80(void* ref, CpuRegs* cpuRegs);

// Hardware
static MsxPsg*         msxPsg;
static R800*           r800;
static RTC*            rtc;
static UInt8*          msxRam;
static UInt32          msxRamSize;
static UInt32          msxRamStart;
static UInt32          z80Frequency;
static UInt8           msxWaitClass[2][4][4][8];   /* [DRAM mode][slot][sslot][page] */
static int             msxDramHandle = -1;

static void msxSetDramWaits(void* ref, int dramMode)
{
    int slot;
    int sslot;
    int page;

    for (slot = 0; slot < 4; slot++) {
        for (sslot = 0; sslot < 4; sslot++) {
            for (page = 0; page < 8; page++) {
                slotSetWaitClass(slot, sslot, page, msxWaitClass[dramMode ? 1 : 0][slot][sslot][page]);
            }
        }
    }
}

/* The S1990 slows R800 accesses to the cartridge slots and to internal ROM,
 * but not to DRAM, including the ROMs it copies to DRAM in DRAM mode. */
static void msxCreateWaits(Machine* machine)
{
    int i;
    int slot;
    int sslot;
    int page;

    for (slot = 0; slot < 4; slot++) {
        int ext = slot != 0 && (slot == machine->cart[0].slot || slot == machine->cart[1].slot);
        for (sslot = 0; sslot < 4; sslot++) {
            for (page = 0; page < 8; page++) {
                msxWaitClass[0][slot][sslot][page] = ext ? R800_WAIT_EXT : R800_WAIT_ROM;
                msxWaitClass[1][slot][sslot][page] = ext ? R800_WAIT_EXT : R800_WAIT_ROM;
            }
        }
    }

    for (i = 0; i < machine->slotInfoCount; i++) {
        SlotInfo* si = &machine->slotInfo[i];
        int ram = si->romType == RAM_MAPPER || si->romType == RAM_NORMAL;
        /* romMapperDRAM copies only the 0-0 and 3-1 ROMs below 8000h. */
        int dram = si->romType == ROM_DRAM &&
                   ((si->slot == 0 && si->subslot == 0) || (si->slot == 3 && si->subslot == 1));

        if (si->slot != 0 && (si->slot == machine->cart[0].slot || si->slot == machine->cart[1].slot)) {
            continue;
        }
        for (page = si->startPage; page < si->startPage + si->pageCount && page < 8; page++) {
            if (ram) {
                msxWaitClass[0][si->slot][si->subslot][page] = R800_WAIT_NONE;
            }
            if (ram || (dram && page < 4)) {
                msxWaitClass[1][si->slot][si->subslot][page] = R800_WAIT_NONE;
            }
        }
    }

    msxSetDramWaits(NULL, 0);
    msxDramHandle = panasonicDramRegister(msxSetDramWaits, NULL);
    r800SetPageWaits(r800, slotGetPageWaits());
}

void msxSetCpu(int mode)
{
    switch (mode) {
    default:
    case 0:
        r800SetMode(r800, CPU_Z80);
        break;
    case 1:
        r800SetMode(r800, CPU_R800);
        break;
    }
}

void msxEnableCpuFreq_1_5(int enable) {
    if (enable) {
        r800SetFrequency(r800, CPU_Z80, 3 * z80Frequency / 2);
    }
    else {
        r800SetFrequency(r800, CPU_Z80, z80Frequency);
    }
}

static void reset()
{
    UInt32 systemTime = boardSystemTime();

    slotManagerReset();

    if (r800 != NULL) {
        r800Reset(r800, systemTime);
    }
    
    deviceManagerReset();
}

static void destroy() {
    if (msxDramHandle >= 0) {
        panasonicDramUnregister(msxDramHandle);
        msxDramHandle = -1;
    }

    rtcDestroy(rtc);

    boardRemoveExternalDevices();

    slotManagerDestroy();

    r800DebugDestroy();
    
	ioPortUnregister(0x2e, NULL);

    deviceManagerDestroy();

    r800Destroy(r800);
}

int getPC(){return r800->regs.PC.W;}

static int getRefreshRate()
{
    return vdpGetRefreshRate();
}

static UInt32 getTimeTrace(int offset) {
    return r800GetTimeTrace(r800, offset);
}

static UInt8* getRamPage(int page) {
    int start;

    if (msxRam == NULL) {
        return NULL;
    }

    start = page * 0x2000 - (int)msxRamStart;
    if (page < 0) {
        start += msxRamSize;
    }

    if (start < 0 || start >= (int)msxRamSize) {
        return NULL;
    }

	return msxRam + start;
}
    
static void saveState()
{   
    SaveState* state = saveStateOpenForWrite("msx");

    saveStateSet(state, "z80Frequency",    z80Frequency);
    
    saveStateClose(state);

    r800SaveState(r800);
    deviceManagerSaveState();
    slotSaveState();
    rtcSaveState(rtc);
}

static void loadState()
{
    SaveState* state = saveStateOpenForRead("msx");

    z80Frequency = saveStateGet(state, "z80Frequency", 0);

    saveStateClose(state);

    r800LoadState(r800);
    boardInit(&r800->systemTime);

    deviceManagerLoadState();
    slotLoadState();
    rtcLoadState(rtc);
}

static UInt8 testPort(void* dummy, UInt16 ioPort)
{
    return 0x27;
}

int msxCreate(Machine* machine, 
              VdpSyncMode vdpSyncMode,
              BoardInfo* boardInfo)
{
    char cmosName[512];
    int success;
    int i;

    UInt32 cpuFlags = CPU_ENABLE_M1;

    if (machine->board.type == BOARD_MSX_T9769B ||
        machine->board.type == BOARD_MSX_T9769C)
    {
        cpuFlags |= CPU_VDP_IO_DELAY;
    }

    r800 = r800Create(cpuFlags, slotRead, slotWrite, ioPortRead, ioPortWrite, PatchZ80, boardTimerCheckTimeout, NULL, NULL, NULL, NULL, NULL, NULL);

    boardInfo->cartridgeCount   = machine->board.type == BOARD_MSX_FORTE_II ? 0 : 2;
    boardInfo->diskdriveCount   = machine->board.type == BOARD_MSX_FORTE_II ? 0 : 2;
    boardInfo->casetteCount     = machine->board.type == BOARD_MSX_FORTE_II ? 0 : 1;
    boardInfo->cpuRef           = r800;

    boardInfo->destroy          = destroy;
    boardInfo->softReset        = reset;
    boardInfo->loadState        = loadState;
    boardInfo->saveState        = saveState;
    boardInfo->getRefreshRate   = getRefreshRate;
    boardInfo->getRamPage       = getRamPage;

    boardInfo->run              = r800Execute;
    boardInfo->stop             = r800StopExecution;
    boardInfo->setInt           = r800SetInt;
    boardInfo->clearInt         = r800ClearInt;
    boardInfo->setCpuTimeout    = r800SetTimeoutAt;
    boardInfo->setBreakpoint    = r800SetBreakpoint;
    boardInfo->clearBreakpoint  = r800ClearBreakpoint;
    boardInfo->setDataBus       = r800SetDataBus;
    
    boardInfo->getTimeTrace     = getTimeTrace;

    deviceManagerCreate();
    boardInit(&r800->systemTime);

    ioPortReset();
    ramMapperIoCreate();

    r800Reset(r800, 0);
    mixerReset(boardGetMixer());

    msxPPICreate(machine->board.type == BOARD_MSX_FORTE_II);
    slotManagerCreate();

    r800DebugCreate(r800);
    
	ioPortRegister(0x2e, testPort, NULL, NULL);

    sprintf(cmosName, "%s" DIR_SEPARATOR "%s.cmos", boardGetBaseDirectory(), machine->name);
    rtc = rtcCreate(machine->cmos.enable, machine->cmos.batteryBacked ? cmosName : 0);

    msxRam = NULL;

    vdpCreate(VDP_MSX, machine->video.vdpVersion, vdpSyncMode, machine->video.vramSize / 0x4000);

    for (i = 0; i < 4; i++) {
        slotSetSubslotted(i, machine->slot[i].subslotted);
    }

    for (i = 0; i < 2; i++) {
        cartridgeSetSlotInfo(i, machine->cart[i].slot, machine->cart[i].subslot);
    }

    success = machineInitialize(machine, &msxRam, &msxRamSize, &msxRamStart);

    if (machine->cpu.hasR800) {
        msxCreateWaits(machine);
    }

    msxPsg = msxPsgCreate(machine->board.type == BOARD_MSX || 
                          machine->board.type == BOARD_MSX_FORTE_II 
                          ? PSGTYPE_AY8910 : PSGTYPE_YM2149,
                          machine->audio.psgstereo,
                          machine->audio.psgpan,
                          machine->board.type == BOARD_MSX_FORTE_II ? 1 : 2);

    if (machine->board.type == BOARD_MSX_FORTE_II) {
        CoinDevice* coinDevice = coinDeviceCreate(msxPsg);
        msxPsgRegisterCassetteRead(msxPsg, coinDeviceRead, coinDevice);
    }

    for (i = 0; i < 8; i++) {
        slotMapRamPage(0, 0, i);
    }

    if (success) {
        success = boardInsertExternalDevices();
    }

    z80Frequency = machine->cpu.freqZ80;

    diskEnable(0, machine->fdc.count > 0);
    diskEnable(1, machine->fdc.count > 1);

    r800SetFrequency(r800, CPU_Z80,  machine->cpu.freqZ80);
    r800SetFrequency(r800, CPU_R800, machine->cpu.freqR800);

    if (!success) {
        destroy();
    }

    return success;
}
