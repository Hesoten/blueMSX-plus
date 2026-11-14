/*****************************************************************************
**
** Y8960 cartridge - the MSX-TIMER block: four counters with an interrupt.
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
#include "SaveState.h"
#include "Board.h"
#include "IoPort.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* 3579545 * 12, the cartridge's system clock. The timer's design sheet gives
** 85.9MHz, but the cartridge clocks the timer at 42.95MHz, which its author
** confirmed as intended. Every counter divides this by 2^(10 + 2 * reso),
** giving eight steps from 23.839156us to 390580.739545us. */
#define Y8960_TIMER_FREQ    42954540u

#define Y8960_TIMER_COUNT   4

typedef struct RomMapperY8960Timer RomMapperY8960Timer;

typedef struct {
    RomMapperY8960Timer* owner;
    BoardTimer* boardTimer;
    int    index;
    /* Counted before the divider, so changing the resolution does not throw
    ** away where the counter stands. */
    UInt32 counter;
    UInt32 refTime;
    UInt32 refFrag;
    UInt8  repeat;
    UInt8  reso;
    UInt8  intrEnable;
    UInt8  count;
    UInt8  countEnable;
    UInt8  countEnd;
    UInt8  irqState;
} Y8960TimerCore;

struct RomMapperY8960Timer {
    int deviceHandle;
    Y8960TimerCore core[Y8960_TIMER_COUNT];
    UInt8 registerLatch;
    UInt8 counterSelect;
};

static RomMapperY8960Timer* theTimer = NULL;

static int resoShift(const Y8960TimerCore* t)
{
    return 10 + 2 * (t->reso & 0x07);
}

static UInt32 divCount(const Y8960TimerCore* t, UInt32 value)
{
    return value >> resoShift(t);
}

/* 64 bit on the way out: with terminal value 255 and resolution 7 this is
** 256 shifted left 24, which does not fit in 32 bits. Truncating it to zero
** would drop the sync point and leave the counter standing still. */
static UInt64 mulCount(const Y8960TimerCore* t, UInt32 value)
{
    return (UInt64)value << resoShift(t);
}

static UInt32 modCount(const Y8960TimerCore* t, UInt32 value)
{
    return value & ((1u << resoShift(t)) - 1);
}

static int endReached(const Y8960TimerCore* t, UInt32 value)
{
    return (divCount(t, value) & 0xFF) >= t->count &&
           modCount(t, value) == modCount(t, 0xFFFFFFFFu);
}

static UInt64 nextPoint(const Y8960TimerCore* t, UInt32 value)
{
    UInt32 counter = divCount(t, value) & 0xFF;

    return mulCount(t, (counter >= t->count) ? (counter + 1) : ((UInt32)t->count + 1));
}

static void timerUpdateIrq(RomMapperY8960Timer* rm)
{
    int i;
    int any = 0;

    for (i = 0; i < Y8960_TIMER_COUNT; i++) {
        rm->core[i].irqState = (UInt8)(rm->core[i].countEnd && rm->core[i].intrEnable);
        any |= rm->core[i].irqState;
    }

    if (any) {
        boardSetInt(BOARD_INT_Y8960_TIMER);
    }
    else {
        boardClearInt(BOARD_INT_Y8960_TIMER);
    }
}

static void timerUpdate(Y8960TimerCore* t)
{
    UInt32 now = boardSystemTime();
    UInt64 elapsed = (UInt64)Y8960_TIMER_FREQ * (UInt32)(now - t->refTime) + t->refFrag;
    UInt64 ticks = elapsed / boardFrequency();

    /* Time advances even while the counter is stopped; otherwise enabling it
    ** would pour in everything that passed meanwhile. */
    t->refFrag = (UInt32)(elapsed % boardFrequency());
    t->refTime = now;

    while (ticks > 0 && t->countEnable) {
        UInt64 step = nextPoint(t, t->counter) - (UInt64)t->counter;
        UInt64 value;

        if (step > ticks) {
            step = ticks;
        }

        value = (UInt64)t->counter + step;

        if (endReached(t, (UInt32)(value - 1))) {
            if (t->repeat) {
                value = 0;
            }
            else {
                value--;
                t->countEnable = 0;
            }
            t->countEnd = 1;
        }

        t->counter = (UInt32)value;
        ticks -= step;
    }
}

static void timerSchedule(Y8960TimerCore* t)
{
    UInt64 period;
    UInt64 sysTicks;
    UInt64 needed;

    if (!t->countEnable) {
        return;
    }

    period = (!t->repeat && endReached(t, t->counter))
             ? 0
             : nextPoint(t, t->counter) - (UInt64)t->counter;

    if (period == 0) {
        return;
    }

    /* Pre-divider ticks back into board time, rounded up so the wake-up never
    ** lands before the carry it is waiting for. */
    needed = period * boardFrequency();
    if (needed > t->refFrag) {
        needed -= t->refFrag;
    }
    else {
        needed = 0;
    }
    sysTicks = (needed + Y8960_TIMER_FREQ - 1) / Y8960_TIMER_FREQ;
    if (sysTicks == 0) {
        sysTicks = 1;
    }
    /* boardTimerAdd takes a wake-up about 2^31 board clocks ahead or further
    ** for one already past and drops it, and the longest count (255 at
    ** resolution 7) comes to exactly 2^31. A long wait is walked a second at a time;
    ** onTimer counts what has passed and schedules the rest. */
    if (sysTicks > boardFrequency()) {
        sysTicks = boardFrequency();
    }

    boardTimerAdd(t->boardTimer, t->refTime + (UInt32)sysTicks);
}

static void onTimer(void* ref, UInt32 time)
{
    Y8960TimerCore* t = (Y8960TimerCore*)ref;

    timerUpdate(t);
    timerUpdateIrq(t->owner);
    timerSchedule(t);
}

static void coreWriteReg(Y8960TimerCore* t, UInt8 rg, UInt8 value)
{
    timerUpdate(t);

    switch (rg & 0x03) {
    case 0x00:
        t->repeat     = value & 0x01;
        t->reso       = (value >> 4) & 0x07;
        t->intrEnable = (value >> 7) & 0x01;
        break;
    case 0x01:
        t->count = value;
        break;
    case 0x02:
        t->countEnable = value & 0x01;
        if (value & 0x02) {
            t->counter = 0;
        }
        break;
    }

    timerSchedule(t);
    timerUpdateIrq(t->owner);
}

static UInt8 coreReadReg(Y8960TimerCore* t, UInt8 rg)
{
    timerUpdate(t);

    switch (rg & 0x03) {
    case 0x00:
        return (UInt8)((t->intrEnable << 7) | (t->reso << 4) | t->repeat);
    case 0x01:
        return t->count;
    case 0x02:
        return t->countEnable;
    }

    /* Register 3 does not exist; the hardware reads it as 0. */
    return 0x00;
}

static UInt8 coreReadCounter(Y8960TimerCore* t)
{
    UInt8 result;

    timerUpdate(t);
    result = (UInt8)(divCount(t, t->counter) & 0xFF);
    timerSchedule(t);
    timerUpdateIrq(t->owner);

    return result;
}

static void coreResetIrqFlag(Y8960TimerCore* t)
{
    timerUpdate(t);
    t->countEnd = 0;
}

static UInt8 read(RomMapperY8960Timer* rm, UInt16 port)
{
    int i;

    /* The enabler only gates the address decoder; the counters and the
    ** interrupt keep running behind it. */
    if (!y8960IoEnabled(Y8960_IO_TIMER)) {
        return 0xFF;
    }

    switch (port & 0x03) {
    case 0x00:
        return rm->registerLatch;
    case 0x01:
        return coreReadReg(&rm->core[(rm->registerLatch >> 2) & 0x03], rm->registerLatch);
    case 0x02:
        {
            UInt8 result = 0;
            for (i = 0; i < Y8960_TIMER_COUNT; i++) {
                timerUpdate(&rm->core[i]);
                if (rm->core[i].countEnd) {
                    result |= (UInt8)(1 << i);
                }
            }
            return result;
        }
    case 0x03:
        if (rm->counterSelect >= Y8960_TIMER_COUNT) {
            return 0xFF;
        }
        return coreReadCounter(&rm->core[rm->counterSelect]);
    }

    return 0xFF;
}

static void write(RomMapperY8960Timer* rm, UInt16 port, UInt8 value)
{
    int i;

    if (!y8960IoEnabled(Y8960_IO_TIMER)) {
        return;
    }

    switch (port & 0x03) {
    case 0x00:
        rm->registerLatch = value;
        break;
    case 0x01:
        coreWriteReg(&rm->core[(rm->registerLatch >> 2) & 0x03], rm->registerLatch, value);
        break;
    case 0x02:
        for (i = 0; i < Y8960_TIMER_COUNT; i++) {
            if (value & (1 << i)) {
                coreResetIrqFlag(&rm->core[i]);
            }
        }
        timerUpdateIrq(rm);
        break;
    case 0x03:
        /* A value other than 0-3 selects no counter, and B3h then reads FFh. */
        rm->counterSelect = (value & 0xFC) ? Y8960_TIMER_COUNT : value;
        break;
    }
}

static void reset(RomMapperY8960Timer* rm)
{
    int i;

    for (i = 0; i < Y8960_TIMER_COUNT; i++) {
        Y8960TimerCore* t = &rm->core[i];

        t->counter     = 0;
        t->refTime     = boardSystemTime();
        t->refFrag     = 0;
        t->repeat      = 0;
        t->reso        = 0;
        t->intrEnable  = 0;
        t->count       = 0;
        t->countEnable = 0;
        t->countEnd    = 0;
        t->irqState    = 0;
    }

    rm->registerLatch = 0;
    rm->counterSelect = 0;

    boardClearInt(BOARD_INT_Y8960_TIMER);
}

static void saveState(RomMapperY8960Timer* rm)
{
    SaveState* state = saveStateOpenForWrite("y8960Timer");
    char name[32];
    int i;

    saveStateSet(state, "registerLatch", rm->registerLatch);
    saveStateSet(state, "counterSelect", rm->counterSelect);

    for (i = 0; i < Y8960_TIMER_COUNT; i++) {
        sprintf(name, "counter%d",     i); saveStateSet(state, name, rm->core[i].counter);
        sprintf(name, "refTime%d",     i); saveStateSet(state, name, rm->core[i].refTime);
        sprintf(name, "refFrag%d",     i); saveStateSet(state, name, rm->core[i].refFrag);
        sprintf(name, "repeat%d",      i); saveStateSet(state, name, rm->core[i].repeat);
        sprintf(name, "reso%d",        i); saveStateSet(state, name, rm->core[i].reso);
        sprintf(name, "intrEnable%d",  i); saveStateSet(state, name, rm->core[i].intrEnable);
        sprintf(name, "count%d",       i); saveStateSet(state, name, rm->core[i].count);
        sprintf(name, "countEnable%d", i); saveStateSet(state, name, rm->core[i].countEnable);
        sprintf(name, "countEnd%d",    i); saveStateSet(state, name, rm->core[i].countEnd);
    }

    saveStateClose(state);
}

static void loadState(RomMapperY8960Timer* rm)
{
    SaveState* state = saveStateOpenForRead("y8960Timer");
    char name[32];
    int i;

    rm->registerLatch = (UInt8)saveStateGet(state, "registerLatch", 0);
    rm->counterSelect = (UInt8)saveStateGet(state, "counterSelect", 0);

    for (i = 0; i < Y8960_TIMER_COUNT; i++) {
        Y8960TimerCore* t = &rm->core[i];

        sprintf(name, "counter%d",     i); t->counter     = saveStateGet(state, name, 0);
        sprintf(name, "refTime%d",     i); t->refTime     = saveStateGet(state, name, boardSystemTime());
        sprintf(name, "refFrag%d",     i); t->refFrag     = saveStateGet(state, name, 0);
        sprintf(name, "repeat%d",      i); t->repeat      = (UInt8)saveStateGet(state, name, 0);
        sprintf(name, "reso%d",        i); t->reso        = (UInt8)saveStateGet(state, name, 0);
        sprintf(name, "intrEnable%d",  i); t->intrEnable  = (UInt8)saveStateGet(state, name, 0);
        sprintf(name, "count%d",       i); t->count       = (UInt8)saveStateGet(state, name, 0);
        sprintf(name, "countEnable%d", i); t->countEnable = (UInt8)saveStateGet(state, name, 0);
        sprintf(name, "countEnd%d",    i); t->countEnd    = (UInt8)saveStateGet(state, name, 0);

        timerSchedule(t);
    }

    saveStateClose(state);

    timerUpdateIrq(rm);
}

static void destroy(RomMapperY8960Timer* rm)
{
    int i;

    ioPortUnregister(0xB0, rm);
    ioPortUnregister(0xB1, rm);
    ioPortUnregister(0xB2, rm);
    ioPortUnregister(0xB3, rm);

    for (i = 0; i < Y8960_TIMER_COUNT; i++) {
        boardTimerDestroy(rm->core[i].boardTimer);
    }

    boardClearInt(BOARD_INT_Y8960_TIMER);
    deviceManagerUnregister(rm->deviceHandle);

    free(rm);

    theTimer = NULL;
}

void romMapperY8960TimerDestroy(void)
{
    if (theTimer != NULL) {
        destroy(theTimer);
    }
}

int romMapperY8960TimerCreate(void)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperY8960Timer* rm = (RomMapperY8960Timer*)calloc(1, sizeof(RomMapperY8960Timer));
    int i;

    rm->deviceHandle = deviceManagerRegister(ROM_Y8960, &callbacks, rm);

    for (i = 0; i < Y8960_TIMER_COUNT; i++) {
        rm->core[i].owner      = rm;
        rm->core[i].index      = i;
        rm->core[i].boardTimer = boardTimerCreate(onTimer, &rm->core[i]);
    }

    ioPortRegister(0xB0, read, write, rm);
    ioPortRegister(0xB1, read, write, rm);
    ioPortRegister(0xB2, read, write, rm);
    ioPortRegister(0xB3, read, write, rm);

    theTimer = rm;

    reset(rm);

    return 1;
}
