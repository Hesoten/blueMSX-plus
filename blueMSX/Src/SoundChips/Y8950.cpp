/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/Y8950.c,v $
**
** $Revision: 1.21 $
**
** $Date: 2008-03-31 19:42:23 $
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
extern "C" {
#include "Y8950.h"
#include "Board.h"
#include "SaveState.h"
#include "IoPort.h"
#include "MediaDb.h"
#include "MidiIO.h"
#include "DeviceManager.h"
#include "Language.h"
#include "Properties.h"
}
#include "Y8950MultiBackend.h"
#include <stdlib.h>
#include <string.h>

#define FREQUENCY        3579545
#define SAMPLERATE       (FREQUENCY / 72)
#define TIMER_FREQUENCY  (4 * boardFrequency() / SAMPLERATE)

/* Dispatcher owns boardTimer + IRQ wiring; backends are chip wrappers.
** reg 0x04 bits 0/1 stripped on broadcast to non-fmopl backends. */
struct Y8950 {
    Mixer*         mixer;
    Int32          handle;
    UInt32         rate;

    Y8950BackendBase* backend;
    MidiIO*        ykIo;
    UInt8          address;

    BoardTimer*    timer1;
    BoardTimer*    timer2;
    UInt32         timerValue1, timerValue2;
    UInt32         timeout1,    timeout2;
    UInt32         timerRunning1, timerRunning2;
    UInt32         timerEnabled1, timerEnabled2;
};

extern "C" {

/* Fmopl globals saved in msxaudio1 for pre-refactor save-state parity.
** Typed as Int32 (signed 32bit) to match Fmopl.c's INT32 declarations
** without dragging Fmopl.h into the dispatcher. */
extern Int32 outd;
extern Int32 ams;
extern Int32 vib;
extern Int32 feedback2;

static void y8950ScheduleTimer(Y8950* y8950, int timer);
static void y8950CancelTimer  (Y8950* y8950, int timer);

static void onTimeout1(void* ptr, UInt32 /*time*/)
{
    Y8950* y8950 = (Y8950*)ptr;
    y8950->timerRunning1 = 0;
    if (y8950->backend) y8950->backend->onTimerOverflow(0);
    if (y8950->timerEnabled1) y8950ScheduleTimer(y8950, 0);
}

static void onTimeout2(void* ptr, UInt32 /*time*/)
{
    Y8950* y8950 = (Y8950*)ptr;
    y8950->timerRunning2 = 0;
    if (y8950->backend) y8950->backend->onTimerOverflow(1);
    if (y8950->timerEnabled2) y8950ScheduleTimer(y8950, 1);
}

static void y8950ScheduleTimer(Y8950* y8950, int timer)
{
    UInt32 systemTime = boardSystemTime();
    if (timer == 0) {
        if (!y8950->timerRunning1) {
            UInt32 adjust = systemTime % TIMER_FREQUENCY;
            y8950->timeout1 = systemTime + TIMER_FREQUENCY * y8950->timerValue1 - adjust;
            boardTimerAdd(y8950->timer1, y8950->timeout1);
            y8950->timerRunning1 = 1;
        }
    }
    else {
        if (!y8950->timerRunning2) {
            UInt32 adjust = systemTime % (4 * TIMER_FREQUENCY);
            y8950->timeout2 = systemTime + TIMER_FREQUENCY * y8950->timerValue2 - adjust;
            boardTimerAdd(y8950->timer2, y8950->timeout2);
            y8950->timerRunning2 = 1;
        }
    }
}

static void y8950CancelTimer(Y8950* y8950, int timer)
{
    if (timer == 0) {
        if (y8950->timerRunning1) {
            boardTimerRemove(y8950->timer1);
            y8950->timerRunning1 = 0;
        }
    }
    else {
        if (y8950->timerRunning2) {
            boardTimerRemove(y8950->timer2);
            y8950->timerRunning2 = 0;
        }
    }
}

/* Fmopl's OPLWriteReg invokes these; dispatcher parses reg writes
** directly, so they are no-ops kept only to satisfy the linker. */
void y8950TimerSet(void* /*ref*/, int /*timer*/, int /*count*/)   {}
void y8950TimerStart(void* /*ref*/, int /*timer*/, int /*start*/) {}

#define Y8950_KEY_START 36

int y8950GetNoteOn(void* ref, int kbdLatch)
{
    Y8950* y8950 = (Y8950*)ref;
    UInt8 val = 0xff;
    int row;

    for (row = 0; row < 8; row++) {
        if ((1 << row) & kbdLatch) {
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 0) ? ~0x01 : 0xff;
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 1) ? ~0x02 : 0xff;
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 2) ? ~0x04 : 0xff;
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 3) ? ~0x08 : 0xff;
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 4) ? ~0x10 : 0xff;
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 5) ? ~0x20 : 0xff;
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 6) ? ~0x40 : 0xff;
            val &= ykIoGetKeyState(y8950->ykIo, Y8950_KEY_START + row * 8 + 7) ? ~0x80 : 0xff;
        }
    }

    return val;
}

UInt8 y8950Peek(Y8950* y8950, UInt16 ioPort)
{
    if (y8950 != NULL && y8950->backend) {
        return y8950->backend->peekIo(ioPort & 1);
    }
    return  0xff;
}

UInt8 y8950Read(Y8950* y8950, UInt16 ioPort)
{
    if ((ioPort & 1) == 1 && y8950->address == 0x14) {
        mixerSync(y8950->mixer);
    }
    return y8950->backend->readIo(ioPort & 1);
}

void y8950Write(Y8950* y8950, UInt16 ioPort, UInt8 value)
{
    if ((ioPort & 1) == 0) {
        y8950->address = value;
    }
    else {
        mixerSync(y8950->mixer);
        switch (y8950->address) {
        case 0x02:
            y8950->timerValue1 = 1 * (256 - value);
            break;
        case 0x03:
            y8950->timerValue2 = 4 * (256 - value);
            break;
        case 0x04:
            if (value & 0x80) {
                /* IRQ flag clear: enable / mask bits unaffected. */
                boardClearInt(0x10);
            }
            else {
                if (value & 0x01) {
                    if (!y8950->timerEnabled1) {
                        y8950->timerEnabled1 = 1;
                        y8950ScheduleTimer(y8950, 0);
                    }
                }
                else {
                    y8950->timerEnabled1 = 0;
                    y8950CancelTimer(y8950, 0);
                }
                if (value & 0x02) {
                    if (!y8950->timerEnabled2) {
                        y8950->timerEnabled2 = 1;
                        y8950ScheduleTimer(y8950, 1);
                    }
                }
                else {
                    y8950->timerEnabled2 = 0;
                    y8950CancelTimer(y8950, 1);
                }
            }
            break;
        }
    }
    y8950->backend->writeIo(ioPort & 1, value);
}

static char* regText(int d)
{
    static char text[5];
    sprintf(text, "R%.2x", d);
    return text;
}

static char regsAvailAY8950[] = {
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,0,0,0,0,0, // 0x00
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0, // 0x20
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0, // 0x40
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0, // 0x60
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0, // 0x80
    1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,1,0,0, // 0xa0
    1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 0xc0
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1  // 0xe0
};

void y8950GetDebugInfo(Y8950* y8950, DbgDevice* dbgDevice)
{
    DbgRegisterBank* regBank;
    int c = 1;
    int r;

    for (r = 0; r < (int)sizeof(regsAvailAY8950); r++) {
        c += regsAvailAY8950[r];
    }

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegsAy8950(), c);

    c = 0;
    dbgRegisterBankAddRegister(regBank, c++, "SR", 8, y8950->backend->readIo(0));

    for (r = 0; r < (int)sizeof(regsAvailAY8950); r++) {
        if (regsAvailAY8950[r]) {
            dbgRegisterBankAddRegister(regBank, c++, regText(r), 8, y8950->backend->readReg(r));
        }
    }

    UInt32 adpcmSize = 0;
    const UInt8* adpcmMem = y8950->backend->getAdpcmRam(&adpcmSize);
    dbgDeviceAddMemoryBlock(dbgDevice, langDbgMemAy8950(), 0, 0, adpcmSize, (UInt8*)adpcmMem);
}

static Int32* y8950Sync(void* ref, UInt32 count)
{
    Y8950* y8950 = (Y8950*)ref;
    Int32* buf = y8950->backend->updateBuffer(count);
    return buf;
}


/* msxaudio1 = pre-multi-backend layout (address + timer + fmopl
** globals).  Load also reads fmopl_host as the abstract-refactor
** interim fallback. */
void y8950SaveState(Y8950* y8950)
{
    SaveState* state = saveStateOpenForWrite("msxaudio1");
    saveStateSet(state, "address",        y8950->address);
    saveStateSet(state, "timerValue1",    y8950->timerValue1);
    saveStateSet(state, "timerValue2",    y8950->timerValue2);
    saveStateSet(state, "timerEnabled1",  y8950->timerEnabled1);
    saveStateSet(state, "timerEnabled2",  y8950->timerEnabled2);
    saveStateSet(state, "timerRunning1",  y8950->timerRunning1);
    saveStateSet(state, "timerRunning2",  y8950->timerRunning2);
    saveStateSet(state, "timeout1",       y8950->timeout1);
    saveStateSet(state, "timeout2",       y8950->timeout2);
    saveStateSet(state, "outd",           outd);
    saveStateSet(state, "ams",            ams);
    saveStateSet(state, "vib",            vib);
    saveStateSet(state, "feedback2",      feedback2);
    saveStateClose(state);

    if (y8950->backend) y8950->backend->saveState();
}

void y8950LoadState(Y8950* y8950)
{
    SaveState* state = saveStateOpenForRead("msxaudio1");
    if (saveStateIsEmpty(state)) {
        saveStateClose(state);
        return;
    }
    /* Cancel any boardTimer scheduled by mid-session activity before we
    ** apply the saved schedule, otherwise the timer would fire twice
    ** per period and music would play at roughly double speed. */
    y8950CancelTimer(y8950, 0);
    y8950CancelTimer(y8950, 1);
    y8950->timerEnabled1 = 0;
    y8950->timerEnabled2 = 0;
    boardClearInt(0x10);

    y8950->address       = (UInt8)saveStateGet(state, "address",       0);
    y8950->timerValue1   =        saveStateGet(state, "timerValue1",   0);
    y8950->timerValue2   =        saveStateGet(state, "timerValue2",   0);
    y8950->timerEnabled1 =        saveStateGet(state, "timerEnabled1", 0);
    y8950->timerEnabled2 =        saveStateGet(state, "timerEnabled2", 0);
    y8950->timerRunning1 =        saveStateGet(state, "timerRunning1", 0);
    y8950->timerRunning2 =        saveStateGet(state, "timerRunning2", 0);
    y8950->timeout1      =        saveStateGet(state, "timeout1",      0);
    y8950->timeout2      =        saveStateGet(state, "timeout2",      0);
    outd      = saveStateGet(state, "outd",      0);
    ams       = saveStateGet(state, "ams",       0);
    vib       = saveStateGet(state, "vib",       0);
    feedback2 = saveStateGet(state, "feedback2", 0);
    saveStateClose(state);

    /* Abstract-refactor fallback: timer + fmopl globals were in
    ** fmopl_host then.  Override with anything present there. */
    state = saveStateOpenForRead("fmopl_host");
    y8950->timerValue1   = saveStateGet(state, "timerValue1",   y8950->timerValue1);
    y8950->timerValue2   = saveStateGet(state, "timerValue2",   y8950->timerValue2);
    y8950->timerRunning1 = saveStateGet(state, "timerRunning1", y8950->timerRunning1);
    y8950->timerRunning2 = saveStateGet(state, "timerRunning2", y8950->timerRunning2);
    y8950->timeout1      = saveStateGet(state, "timeout1",      y8950->timeout1);
    y8950->timeout2      = saveStateGet(state, "timeout2",      y8950->timeout2);
    outd      = saveStateGet(state, "outd",      outd);
    ams       = saveStateGet(state, "ams",       ams);
    vib       = saveStateGet(state, "vib",       vib);
    feedback2 = saveStateGet(state, "feedback2", feedback2);
    saveStateClose(state);

    if (y8950->backend) y8950->backend->loadState();

    /* Re-propagate the mixer rate so backends recover correct
    ** resampling cadence even if a legacy save path left their cached
    ** rate at the chip-rate ctor default. */
    if (y8950->backend) y8950->backend->setSampleRate(y8950->rate);

    /* Legacy saves predate timerEnabled; infer from timerRunning so
    ** the dispatcher reschedules after the first fire. */
    if (y8950->timerRunning1 && !y8950->timerEnabled1) y8950->timerEnabled1 = 1;
    if (y8950->timerRunning2 && !y8950->timerEnabled2) y8950->timerEnabled2 = 1;

    if (y8950->timerRunning1) boardTimerAdd(y8950->timer1, y8950->timeout1);
    if (y8950->timerRunning2) boardTimerAdd(y8950->timer2, y8950->timeout2);
}

void y8950Destroy(Y8950* y8950)
{
    mixerUnregisterChannel(y8950->mixer, y8950->handle);

    if (y8950->timer1) boardTimerDestroy(y8950->timer1);
    if (y8950->timer2) boardTimerDestroy(y8950->timer2);

    delete y8950->backend;
    y8950->backend = NULL;

    if (y8950->ykIo != NULL) {
        ykIoDestroy(y8950->ykIo);
    }

    free(y8950);
}

void y8950Reset(Y8950* y8950)
{
    y8950->timerEnabled1 = 0;
    y8950->timerEnabled2 = 0;
    y8950CancelTimer(y8950, 0);
    y8950CancelTimer(y8950, 1);
    boardClearInt(0x10);
    if (y8950->backend) y8950->backend->reset();
}

void y8950SetSampleRate(void* ref, UInt32 rate)
{
    Y8950* y8950 = (Y8950*)ref;
    y8950->rate = rate;
    if (y8950->backend) y8950->backend->setSampleRate(rate);
}

Y8950* y8950Create(Mixer* mixer)
{
    Y8950* y8950 = (Y8950*)calloc(1, sizeof(Y8950));

    y8950->mixer = mixer;
    y8950->ykIo  = ykIoCreate();
    y8950->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_MSXAUDIO, 0, y8950Sync, y8950SetSampleRate, y8950);

    y8950->timer1 = boardTimerCreate(onTimeout1, y8950);
    y8950->timer2 = boardTimerCreate(onTimeout2, y8950);

    y8950->backend = new Y8950MultiBackend(y8950);
    y8950->rate    = mixerGetSampleRate(mixer);
    y8950->backend->setSampleRate(y8950->rate);

    /* Active backend comes from Properties; clamp to fmopl if the
    ** chosen backend was opted out of. */
    {
        Properties* props = propGetGlobalProperties();
        int desired = props ? props->sound.chip.y8950BackendActive : PROP_Y8950_BACKEND_FMOPL;
        if (!y8950BackendIsEnabled(desired)) desired = PROP_Y8950_BACKEND_FMOPL;
        y8950BackendActiveSet(desired);
    }

    return y8950;
}

}  /* extern "C" */
