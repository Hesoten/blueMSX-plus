/*****************************************************************************
**
** Y8960 cartridge - the OPLLEX chip: a YM2413 with four preset ROMs.
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
#include "Y8960Opllex.h"
#include "Y8960OpllCore.h"
#include "Y8960Pan.h"
#include "romMapperY8960.h"
#include "SaveState.h"
#include "DebugDeviceManager.h"
#include "Language.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* 3.579545MHz, the same clock the MSX-MUSIC it replaces gets. */
#define Y8960_OPLLEX_CLOCK 3579545

/* The same factor Emu2413Backend applies, so the block lands at the level of
** the machine's own MSX-MUSIC rather than at one nobody has measured. That
** figure was set by RMS-matching emu2413 against OpenYM2413_2. The balance
** against the other Y8960 blocks is set on top of it, in the mixer block's
** gain (romMapperY8960Mixer.c). */
#define Y8960_OPLLEX_VOLUME (32767 * 9 / 10)
#define Y8960_OPLLEX_VOLUME_DIV 1024

/* No analog post-filter. The machine's MSX-MUSIC runs one with user-settable
** cutoffs, but those belong to that device's settings and the cartridge's
** analog stage is not documented. */

struct Y8960OpllexChip {
    Mixer*        mixer;
    Int32         handle;
    Int32         debugHandle;

    Y8960OPLL*    opll;
    UInt8         address;
    Int32         buffer[AUDIO_STEREO_BUFFER_SIZE];
};

/* Stereo, because the pan registers place each channel; the mixer's own pan
** could only move the block as a whole. */
static Int32* y8960OpllexSync(void* ref, UInt32 count)
{
    Y8960OpllexChip* chip = (Y8960OpllexChip*)ref;
    Int64 gain = (Int64)y8960MixerGain(Y8960_VOL_OPLL) * Y8960_OPLLEX_VOLUME;
    UInt32 i;

    for (i = 0; i < count; i++) {
        int32_t out[2];
        Y8960OPLL_calcStereo(chip->opll, out);
        chip->buffer[2 * i + 0] = (Int32)(out[0] * gain / (Y8960_OPLLEX_VOLUME_DIV * Y8960_GAIN_ONE));
        chip->buffer[2 * i + 1] = (Int32)(out[1] * gain / (Y8960_OPLLEX_VOLUME_DIV * Y8960_GAIN_ONE));
    }

    return chip->buffer;
}

static void y8960OpllexSetSampleRate(void* ref, UInt32 rate)
{
    Y8960OpllexChip* chip = (Y8960OpllexChip*)ref;

    Y8960OPLL_setRate(chip->opll, rate);
}

void y8960OpllexReset(Y8960OpllexChip* chip)
{
    Y8960OPLL_reset(chip->opll);
    chip->address = 0;
}

void y8960OpllexWriteAddress(Y8960OpllexChip* chip, UInt8 address)
{
    chip->address = address;
}

void y8960OpllexWriteData(Y8960OpllexChip* chip, UInt8 data)
{
    mixerSync(chip->mixer);

    Y8960OPLL_writeReg(chip->opll, chip->address, data);
}

/* The registers are the whole of the chip's programmed state, so the state is
** saved as registers and replayed on load. What that loses is where the
** envelopes and phases had got to, which costs a click at the load point. */
void y8960OpllexSaveState(Y8960OpllexChip* chip)
{
    SaveState* state = saveStateOpenForWrite("y8960opllex");

    saveStateSet      (state, "address", chip->address);
    saveStateSetBuffer(state, "regs",    chip->opll->reg, sizeof(chip->opll->reg));

    saveStateClose(state);
}

void y8960OpllexLoadState(Y8960OpllexChip* chip)
{
    SaveState* state = saveStateOpenForRead("y8960opllex");
    UInt8 regs[Y8960OPLL_REG_COUNT];
    int i;

    memset(regs, 0, sizeof(regs));
    /* A state from before the pan registers existed is shorter than this
    ** buffer and leaves them untouched; the centre is what it was playing at. */
    memset(regs + Y8960OPLL_PAN_REG_BASE, Y8960_PAN_CENTER, 9);

    chip->address = (UInt8)saveStateGet(state, "address", 0);
    saveStateGetBuffer(state, "regs", regs, sizeof(regs));

    saveStateClose(state);

    Y8960OPLL_reset(chip->opll);

    /* Ascending order is what software would have written: a channel's f-number
    ** low byte (10h-18h) is already in place when the block and key-on bits
    ** (20h-28h) arrive, and the bank registers re-point the voice afterwards. */
    for (i = 0; i < Y8960OPLL_REG_COUNT; i++) {
        Y8960OPLL_writeReg(chip->opll, i, regs[i]);
    }
}

static void getDebugInfo(Y8960OpllexChip* chip, DbgDevice* dbgDevice)
{
    /* 00h-07h user voice, 0Eh rhythm, 10h-18h / 20h-28h / 30h-38h per channel,
    ** 40h-48h bank, 50h-58h pan. The gaps are not registers. */
    static const UInt8 regsAvail[] = {
        1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1
    };
    DbgRegisterBank* regBank;
    int r;
    int count = 0;

    for (r = 0; r < (int)sizeof(regsAvail); r++) {
        count += regsAvail[r];
    }

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegs(), count);

    count = 0;
    for (r = 0; r < (int)sizeof(regsAvail); r++) {
        if (regsAvail[r]) {
            char name[8];
            sprintf(name, "R%.2x", r);
            dbgRegisterBankAddRegister(regBank, count++, name, 8, chip->opll->reg[r]);
        }
    }
}

Y8960OpllexChip* y8960OpllexCreate(Mixer* mixer, const char* name)
{
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    Y8960OpllexChip* chip = (Y8960OpllexChip*)calloc(1, sizeof(Y8960OpllexChip));

    chip->mixer = mixer;
    chip->opll  = Y8960OPLL_new(Y8960_OPLLEX_CLOCK, mixerGetSampleRate(mixer));

    chip->handle      = mixerRegisterChannel(mixer, MIXER_CHANNEL_Y8960, 1,
                                             y8960OpllexSync, y8960OpllexSetSampleRate, chip);
    chip->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, name, &dbgCallbacks, chip);

    y8960OpllexReset(chip);

    return chip;
}

void y8960OpllexDestroy(Y8960OpllexChip* chip)
{
    debugDeviceUnregister(chip->debugHandle);
    mixerUnregisterChannel(chip->mixer, chip->handle);

    Y8960OPLL_delete(chip->opll);

    free(chip);
}
