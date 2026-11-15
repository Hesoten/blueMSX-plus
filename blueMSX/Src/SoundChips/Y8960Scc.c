/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/SCC.c,v $
**
** $Revision: 1.27 $
**
** $Date: 2008-03-30 18:38:45 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Forked for the Y8960 cartridge, 2026 by madscient.
** The cartridge carries an SCC-equivalent circuit (IKASCC in the hardware).
** IKASCC was built from the SCC die, so it runs on the machine's SCC core
** (SCC.c).
** What the cartridge adds on top of the chip: the mixer block's volume, and
** a pan per channel, which makes this a stereo device. The pan is applied to
** each channel before the sum, so the output filter runs once per side.
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
#include "Y8960Scc.h"
#include "Y8960Pan.h"
#include "romMapperY8960.h"
#include "SaveState.h"
#include <stdio.h>
#include <stdlib.h>

struct Y8960SccChip
{
    Mixer* mixer;
    Int32  handle;
    SCC*   scc;
    MixerVoices* voices;
    UInt8  pan[5];
};

static void setPan(Y8960SccChip* chip, int channel, UInt8 value)
{
    Int32 gain[2];

    chip->pan[channel] = value;
    y8960PanGain(value, gain);
    sccSetPanGain(chip->scc, channel, gain[0], gain[1]);
}

static Int32* y8960SccSync(void* ref, UInt32 count)
{
    Y8960SccChip* chip = (Y8960SccChip*)ref;
    /* The mixer block's volume for this block. */
    Int32  blockGain = y8960MixerGain(Y8960_VOL_SCC);
    Int32* buffer;
    UInt32 i;

    if (chip->voices != NULL) {
        chip->voices->scale = (float)blockGain / Y8960_GAIN_ONE;
    }
    buffer = sccSyncStereo(chip->scc, count);

    for (i = 0; i < 2 * count; i++) {
        buffer[i] = (Int32)((Int64)buffer[i] * blockGain / Y8960_GAIN_ONE);
    }

    return buffer;
}

Y8960SccChip* y8960SccCreate(Mixer* mixer)
{
    Y8960SccChip* chip = (Y8960SccChip*)calloc(1, sizeof(Y8960SccChip));

    chip->mixer  = mixer;
    chip->scc    = sccCreateStereo(mixer);
    chip->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_Y8960, 1, y8960SccSync, NULL, chip);
    chip->voices = sccAttachVoices(chip->scc, chip->handle, 1.0f);

    y8960SccReset(chip);

    return chip;
}

void y8960SccDestroy(Y8960SccChip* chip)
{
    mixerUnregisterChannel(chip->mixer, chip->handle);
    sccDestroy(chip->scc);
    free(chip);
}

void y8960SccReset(Y8960SccChip* chip)
{
    int channel;

    sccReset(chip->scc);
    for (channel = 0; channel < 5; channel++) {
        setPan(chip, channel, Y8960_PAN_CENTER);
    }
}

void y8960SccSetMode(Y8960SccChip* chip, SccMode newMode)
{
    sccSetMode(chip->scc, newMode);
}

UInt8 y8960SccRead(Y8960SccChip* chip, UInt8 address)
{
    return sccRead(chip->scc, address);
}

UInt8 y8960SccPeek(Y8960SccChip* chip, UInt8 address)
{
    return sccPeek(chip->scc, address);
}

void y8960SccWrite(Y8960SccChip* chip, UInt8 address, UInt8 value)
{
    sccWrite(chip->scc, address, value);
}

void y8960SccWritePan(Y8960SccChip* chip, UInt8 channel, UInt8 value)
{
    if (channel >= 5) {
        return;
    }
    mixerSync(chip->mixer);
    setPan(chip, channel, value);
}

/* The pans share the core's section, where states have always kept them. */
void y8960SccLoadState(Y8960SccChip* chip)
{
    SaveState* state = saveStateOpenForRead("y8960scc");
    char tag[32];
    int i;

    sccLoadStateFrom(chip->scc, state);
    for (i = 0; i < 5; i++) {
        sprintf(tag, "pan%d", i);
        setPan(chip, i, (UInt8)saveStateGet(state, tag, Y8960_PAN_CENTER));
    }

    saveStateClose(state);
}

void y8960SccSaveState(Y8960SccChip* chip)
{
    SaveState* state = saveStateOpenForWrite("y8960scc");
    char tag[32];
    int i;

    sccSaveStateTo(chip->scc, state);
    for (i = 0; i < 5; i++) {
        sprintf(tag, "pan%d", i);
        saveStateSet(state, tag, chip->pan[i]);
    }

    saveStateClose(state);
}
