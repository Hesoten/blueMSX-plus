/*****************************************************************************
**
** Y8960 cartridge - the OPM block's sound chip, an equivalent of the YM2151.
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Forked from blueMSX's ym2151.c for the Y8960 cartridge, 2026 by madscient.
** The cartridge carries IKAOPM, an equivalent circuit, rather than the
** YM2151 of a Yamaha SFG unit, so it runs on its own copy of the core
** (Y8960OpmCore.c). What the cartridge adds: the mixer block's volume, and an
** interrupt line of its own on /INT with no vector, the cartridge having
** none of the SFG's IM2 support.
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
#include "Y8960Opm.h"
#include "Y8960OpmCore.h"
#include "Y8960Pan.h"
#include "romMapperY8960.h"
#include "Board.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>

/* 3.579545MHz, the clock the cartridge's OPM block is given. */
#define FREQUENCY        3579545
#define SAMPLERATE       (FREQUENCY / 64)
/* One timer A step is 64 master clocks, one sample. */
#define TIMER_FREQUENCY  (boardFrequency() / SAMPLERATE)

/* The factor ym2151.c applies, so the block lands at the level of an SFG
** rather than at one nobody has measured. The other Y8960 blocks are set
** against it in the mixer block's gain (romMapperY8960Mixer.c). */
#define Y8960_OPM_VOLUME 11

struct Y8960OpmChip {
    Mixer* mixer;
    Int32  handle;
    UInt32 rate;

    Y8960OpmCore* opm;
    BoardTimer* timer1;
    BoardTimer* timer2;
    UInt32 timerValue1;
    UInt32 timerValue2;
    UInt32 timeout1;
    UInt32 timeout2;
    UInt32 timerRunning1;
    UInt32 timerRunning2;
    UInt8  latch;
    int    irqState;
    /* Linear resampling from the chip's own rate, as ym2151.c does it. */
    Int32  off;
    Int32  s1l;
    Int32  s2l;
    Int32  s1r;
    Int32  s2r;
    Int32  buffer[AUDIO_STEREO_BUFFER_SIZE];
};

/* The core calls these by name. */
void y8960OpmTimerStart(void* ref, int timer, int start);

void y8960OpmTimerSet(void* ref, int timer, int count)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;

    if (timer == 0) {
        chip->timerValue1 = count;
    }
    else {
        chip->timerValue2 = count;
    }
}

void y8960OpmSetIrq(void* ref, int timer)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;

    if (chip->irqState == 0) {
        boardSetInt(BOARD_INT_Y8960_OPM);
    }
    chip->irqState |= timer;
}

void y8960OpmClearIrq(void* ref, int timer)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;

    chip->irqState &= ~timer;
    if (chip->irqState == 0) {
        boardClearInt(BOARD_INT_Y8960_OPM);
    }
}

void y8960OpmWritePortCallback(void* ref, UInt32 port, UInt8 value)
{
}

static void onTimeout1(void* ref, UInt32 time)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;

    chip->timerRunning1 = 0;
    Y8960OpmCoreTimerCallback(chip->opm, 0);
    y8960OpmTimerStart(chip, 0, 1);
}

static void onTimeout2(void* ref, UInt32 time)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;

    chip->timerRunning2 = 0;
    Y8960OpmCoreTimerCallback(chip->opm, 1);
    y8960OpmTimerStart(chip, 1, 1);
}

void y8960OpmTimerStart(void* ref, int timer, int start)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;

    if (timer == 0) {
        if (start != 0) {
            if (!chip->timerRunning1) {
                UInt32 systemTime = boardSystemTime();
                UInt32 adjust = systemTime % TIMER_FREQUENCY;
                chip->timeout1 = systemTime + TIMER_FREQUENCY * chip->timerValue1 - adjust;
                boardTimerAdd(chip->timer1, chip->timeout1);
                chip->timerRunning1 = 1;
            }
        }
        else {
            if (chip->timerRunning1) {
                boardTimerRemove(chip->timer1);
                chip->timerRunning1 = 0;
            }
        }
    }
    else {
        if (start != 0) {
            if (!chip->timerRunning2) {
                UInt32 systemTime = boardSystemTime();
                UInt32 adjust = systemTime % (16 * TIMER_FREQUENCY);
                chip->timeout2 = systemTime + TIMER_FREQUENCY * chip->timerValue2 - adjust;
                boardTimerAdd(chip->timer2, chip->timeout2);
                chip->timerRunning2 = 1;
            }
        }
        else {
            if (chip->timerRunning2) {
                boardTimerRemove(chip->timer2);
                chip->timerRunning2 = 0;
            }
        }
    }
}

/* Bit 7 is the hardware's busy flag, set while its write queue drains. Here
** a write takes effect at once, so it reads 0 throughout. */
UInt8 y8960OpmReadStatus(Y8960OpmChip* chip)
{
    return (UInt8)Y8960OpmCoreReadStatus(chip->opm);
}

void y8960OpmWriteAddress(Y8960OpmChip* chip, UInt8 address)
{
    chip->latch = address;
}

void y8960OpmWriteData(Y8960OpmChip* chip, UInt8 data)
{
    mixerSync(chip->mixer);
    Y8960OpmCoreWriteReg(chip->opm, chip->latch, data);
}

static Int32* y8960OpmSync(void* ref, UInt32 count)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;
    Int64 gain = (Int64)y8960MixerGain(Y8960_VOL_OPM) * Y8960_OPM_VOLUME;
    UInt32 i;

    for (i = 0; i < count; i++) {
        Int16 sl, sr;
        Int32 l, r;
        chip->off -= SAMPLERATE - chip->rate;
        chip->s1l = chip->s2l;
        chip->s1r = chip->s2r;
        Y8960OpmCoreUpdateOne(chip->opm, &sl, &sr, 1);
        chip->s2l = sl;
        chip->s2r = sr;
        if (chip->off < 0) {
            chip->off += chip->rate;
            chip->s1l = chip->s2l;
            chip->s1r = chip->s2r;
            Y8960OpmCoreUpdateOne(chip->opm, &sl, &sr, 1);
            chip->s2l = sl;
            chip->s2r = sr;
        }
        l = (chip->s1l * (chip->off / 256) + chip->s2l * ((SAMPLERATE - chip->off) / 256)) / (SAMPLERATE / 256);
        r = (chip->s1r * (chip->off / 256) + chip->s2r * ((SAMPLERATE - chip->off) / 256)) / (SAMPLERATE / 256);
        chip->buffer[2 * i + 0] = (Int32)(l * gain / Y8960_GAIN_ONE);
        chip->buffer[2 * i + 1] = (Int32)(r * gain / Y8960_GAIN_ONE);
    }

    return chip->buffer;
}

static void y8960OpmSetSampleRate(void* ref, UInt32 rate)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)ref;

    chip->rate = rate;
}

void y8960OpmSaveState(Y8960OpmChip* chip)
{
    SaveState* state = saveStateOpenForWrite("y8960opm");

    saveStateSet(state, "irqState",      chip->irqState);
    saveStateSet(state, "latch",         chip->latch);
    saveStateSet(state, "timerValue1",   chip->timerValue1);
    saveStateSet(state, "timerRunning1", chip->timerRunning1);
    saveStateSet(state, "timeout1",      chip->timeout1);
    saveStateSet(state, "timerValue2",   chip->timerValue2);
    saveStateSet(state, "timerRunning2", chip->timerRunning2);
    saveStateSet(state, "timeout2",      chip->timeout2);

    saveStateClose(state);

    Y8960OpmCoreSaveState(chip->opm);
}

void y8960OpmLoadState(Y8960OpmChip* chip)
{
    SaveState* state = saveStateOpenForRead("y8960opm");

    chip->irqState      =        saveStateGet(state, "irqState",      0);
    chip->latch         = (UInt8)saveStateGet(state, "latch",         0);
    chip->timerValue1   =        saveStateGet(state, "timerValue1",   0);
    chip->timerRunning1 =        saveStateGet(state, "timerRunning1", 0);
    chip->timeout1      =        saveStateGet(state, "timeout1",      0);
    chip->timerValue2   =        saveStateGet(state, "timerValue2",   0);
    chip->timerRunning2 =        saveStateGet(state, "timerRunning2", 0);
    chip->timeout2      =        saveStateGet(state, "timeout2",      0);

    saveStateClose(state);

    Y8960OpmCoreLoadState(chip->opm);

    if (chip->timerRunning1) {
        boardTimerAdd(chip->timer1, chip->timeout1);
    }
    if (chip->timerRunning2) {
        boardTimerAdd(chip->timer2, chip->timeout2);
    }
}

void y8960OpmReset(Y8960OpmChip* chip)
{
    chip->timerRunning1 = 0;
    chip->timerRunning2 = 0;
    y8960OpmTimerSet(chip, 0, 1024);
    y8960OpmTimerSet(chip, 1, 16 * 256);
    boardTimerRemove(chip->timer1);
    boardTimerRemove(chip->timer2);
    Y8960OpmCoreReset(chip->opm);
    chip->off = 0;
    chip->s1l = 0;
    chip->s2l = 0;
    chip->s1r = 0;
    chip->s2r = 0;
    chip->latch = 0;
    chip->irqState = 0;
    boardClearInt(BOARD_INT_Y8960_OPM);
}

Y8960OpmChip* y8960OpmCreate(Mixer* mixer)
{
    Y8960OpmChip* chip = (Y8960OpmChip*)calloc(1, sizeof(Y8960OpmChip));

    chip->mixer = mixer;

    chip->timer1 = boardTimerCreate(onTimeout1, chip);
    chip->timer2 = boardTimerCreate(onTimeout2, chip);

    chip->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_Y8960, 1,
                                        y8960OpmSync, y8960OpmSetSampleRate, chip);

    chip->opm  = Y8960OpmCoreCreate(chip, FREQUENCY, SAMPLERATE);
    chip->rate = mixerGetSampleRate(mixer);

    y8960OpmReset(chip);

    return chip;
}

void y8960OpmDestroy(Y8960OpmChip* chip)
{
    boardClearInt(BOARD_INT_Y8960_OPM);
    mixerUnregisterChannel(chip->mixer, chip->handle);
    boardTimerDestroy(chip->timer1);
    boardTimerDestroy(chip->timer2);
    Y8960OpmCoreDestroy(chip->opm);

    free(chip);
}
