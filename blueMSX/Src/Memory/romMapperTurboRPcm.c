/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/romMapperTurboRPcm.c,v $
**
** $Revision: 1.13 $
**
** $Date: 2009-07-03 21:27:14 $
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
#include "romMapperTurboRPcm.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "DebugDeviceManager.h"
#include "SaveState.h"
#include "Board.h"
#include "DAC.h"
#include "IoPort.h"
#include "Language.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Turbo-R PCM: 8-bit DAC, one sample per 1368 board cycles
** (= 228 Z80, ~15700 Hz, matches openMSX Clock<3579545, 228>). */
#define PCM_PERIOD_CYCLES 1368

typedef struct {
    DAC*        dac;
    int         deviceHandle;
    int         debugHandle;
    UInt8       sample;       /* Latched DValue (most recent port 0xA4 write) */
    UInt8       status;
    UInt8       time;
    UInt32      refTime;
    UInt32      refFrag;      /* board cycles into current period [0, PCM_PERIOD_CYCLES) */
    Mixer*      mixer;
    BoardTimer* flushTimer;   /* One-shot, scheduled by BUFF=1 writes */
    int         flushPending;
    UInt32      flushTime;    /* boardSystemTime at which flushTimer will fire */
} RomMapperTurboRPcm;

static void saveState(RomMapperTurboRPcm* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperTurboRPcm");

    saveStateSet(state, "sample",  rm->sample);
    saveStateSet(state, "status",  rm->status);
    saveStateSet(state, "time",    rm->time);
    saveStateSet(state, "refTime", rm->refTime);
    saveStateSet(state, "refFrag", rm->refFrag);
    
    saveStateClose(state);
}

static void loadState(RomMapperTurboRPcm* rm)
{
    SaveState* state = saveStateOpenForRead("mapperTurboRPcm");

    rm->sample  = (UInt8)saveStateGet(state, "sample",  0);
    rm->status  = (UInt8)saveStateGet(state, "status",  0);
    rm->time    = (UInt8)saveStateGet(state, "time",    0);
    rm->refTime =        saveStateGet(state, "refTime", 0);
    rm->refFrag =        saveStateGet(state, "refFrag", 0);

    mixerSetEnable(rm->mixer, rm->status & 2);

    saveStateClose(state);
}

static void destroy(RomMapperTurboRPcm* rm)
{
    deviceManagerUnregister(rm->deviceHandle);
    debugDeviceUnregister(rm->debugHandle);
    boardTimerDestroy(rm->flushTimer);
    dacDestroy(rm->dac);

    ioPortUnregister(0xa4);
    ioPortUnregister(0xa5);

    free(rm);
}

static UInt8 getTimerCounter(RomMapperTurboRPcm* rm)
{
    UInt32 systemTime = boardSystemTime();
    UInt32 delta      = systemTime - rm->refTime;
    UInt32 total      = delta + rm->refFrag;

    rm->refTime  = systemTime;
    rm->refFrag  = total % PCM_PERIOD_CYCLES;
    rm->time    += (UInt8)(total / PCM_PERIOD_CYCLES);

    return rm->time & 0x03;
}

/* Latched-mode flush: arm a one-shot timer at the next sample boundary
** so the DAC picks up the latest rm->sample once per period (BUFF=1). */
static void onFlushTimer(RomMapperTurboRPcm* rm, UInt32 time)
{
    rm->flushPending = 0;
    if (rm->status & 0x02) {
        /* Use the scheduled boundary cycle as the BlipBuffer timestamp;
        ** boardSystemTime() can lag by CPU-yield jitter. */
        dacWriteAt(rm->dac, DAC_CH_MONO, rm->sample, time);
    }
}

static void scheduleFlush(RomMapperTurboRPcm* rm)
{
    UInt32 remaining;
    if (rm->flushPending) return;
    /* The 1368-cycle clock is free-running; writes latch HOLD without
    ** disturbing the clock phase (openMSX Turbor.cc TurborPCM model). */
    remaining = PCM_PERIOD_CYCLES - rm->refFrag;
    rm->flushTime = boardSystemTime() + remaining;
    boardTimerAdd(rm->flushTimer, rm->flushTime);
    rm->flushPending = 1;
}

static UInt8 read(RomMapperTurboRPcm* rm, UInt16 ioPort)
{
    switch (ioPort & 0x01) {
	case 0: 
		return getTimerCounter(rm);
	case 1:
		return (~rm->sample & 0x80) | rm->status;
    }
    return 0xff;
}

static UInt8 peek(RomMapperTurboRPcm* rm, UInt16 ioPort)
{
    switch (ioPort & 0x01) {
	case 0: 
        return rm->time & 0x03;
	case 1:
		return (~rm->sample & 0x80) | rm->status;
    }
    return 0xff;
}

static void write(RomMapperTurboRPcm* rm, UInt16 ioPort, UInt8 value)
{
	switch (ioPort & 0x01) {
	case 0:
        /* Latch the sample.  BUFF=0 pushes to the DAC immediately;
        ** BUFF=1 defers to the next absolute clock edge.  Write resets
        ** the counter VALUE but not the clock phase. */
        getTimerCounter(rm);
        rm->time = 0;
        /* If a flush was scheduled for now-or-earlier, fire it manually
        ** with the old sample before overwriting.  Back-to-back writes
        ** with an already-due flush drop the earlier sample (openMSX
        ** TurborPCM model). */
        if (rm->flushPending &&
            (Int32)(rm->flushTime - boardSystemTime()) <= 0) {
            boardTimerRemove(rm->flushTimer);
            rm->flushPending = 0;
            if (rm->status & 0x02) {
                dacWriteAt(rm->dac, DAC_CH_MONO, rm->sample, rm->flushTime);
            }
        }
        rm->sample = value;
        if (rm->status & 0x02) {                /* not muted */
            if (!(rm->status & 0x01)) {         /* BUFF=0: immediate */
                dacWrite(rm->dac, DAC_CH_MONO, rm->sample);
            } else {                            /* BUFF=1: latched */
                scheduleFlush(rm);
            }
        }
        break;

    case 1:
        {
            /* Port 0xA5 control: BUFF 1->0 flushes the held sample now. */
            UInt8 newStatus = value & 0x1f;
            UInt8 change    = rm->status ^ newStatus;
            rm->status = newStatus;
            if ((change & 0x01) && !(rm->status & 0x01)) {
                dacWrite(rm->dac, DAC_CH_MONO, rm->sample);
            }
            mixerSetEnable(rm->mixer, rm->status & 2);
        }
        break;
    }
}

static void reset(RomMapperTurboRPcm* rm)
{
    rm->refTime = boardSystemTime();
    rm->refFrag = 0;
    rm->time    = 0;
    /* Clear status/DValue on reset (openMSX MSXTurboRPCM::reset). */
    rm->status  = 0;
    rm->sample  = 0x80;
    mixerSetEnable(rm->mixer, 0);
    if (rm->flushPending) {
        boardTimerRemove(rm->flushTimer);
        rm->flushPending = 0;
    }
}

static void getDebugInfo(RomMapperTurboRPcm* rm, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts;

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, langDbgDevPcm(), 2);
    dbgIoPortsAddPort(ioPorts, 0, 0xa4, DBG_IO_READWRITE, peek(rm, 0xa4));
    dbgIoPortsAddPort(ioPorts, 1, 0xa5, DBG_IO_READWRITE, peek(rm, 0xa5));
}

int romMapperTurboRPcmCreate() 
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    RomMapperTurboRPcm* rm = malloc(sizeof(RomMapperTurboRPcm));

    rm->deviceHandle = deviceManagerRegister(ROM_TURBORPCM, &callbacks, rm);
    rm->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, langDbgDevPcm(), &dbgCallbacks, rm);

    rm->mixer  = boardGetMixer();

    rm->dac    = dacCreate(rm->mixer, DAC_MONO);
	rm->status = 0;
    rm->time   = 0;
    rm->sample = 0x80;                   /* mid-level silence */
    rm->refTime = boardSystemTime();
    rm->refFrag = 0;
    rm->flushTimer   = boardTimerCreate(onFlushTimer, rm);
    rm->flushPending = 0;
    rm->flushTime    = 0;

    ioPortRegister(0xa4, read, write, rm);
    ioPortRegister(0xa5, read, write, rm);

    return 1;
}

