/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/AY8910.c,v $
**
** $Revision: 1.35 $
**
** $Date: 2009-04-10 04:38:10 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Forked for the Y8960 cartridge, 2026 by madscient.
** The cartridge carries an SSGS: two YM2149 equivalents, each channel with
** its own volume and pan, not a pair of AY-3-8910s. It is emulated as its own
** chip rather than sharing the machine's PSG.
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
#include "Y8960Ssgs.h"
#include "Y8960Pan.h"
#include "romMapperY8960.h"
#include "SaveState.h"
#include "DebugDeviceManager.h"
#include "Language.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* = (1 << 28) * 3579545 / 32 / 44100. The SSGS gets 3.579545MHz / 2, which is
** what the machine's own PSG runs at, so the base step carries over
** unchanged. */
#define BASE_PHASE_STEP 0x28959becUL

#define Y8960_SSGS_CORES        2
#define Y8960_SSGS_CHANNELS     3
#define Y8960_SSGS_MIX_CHANNELS (Y8960_SSGS_CORES * Y8960_SSGS_CHANNELS)

/* Register map, from the `ssg` sheet of the specification (2026-10-16):
**
**   00h-0Fh  core 0, the YM2149's registers
**   10h-1Fh  core 1, the same
**   20h-22h  volume of core 0 channel A, B, C   (0 mute, bit 7 unity; Y8960Pan.h)
**   24h-26h  volume of core 1 channel A, B, C
**   28h-2Ah  pan of core 0 channel A, B, C      (1 left, 128 centre, 255 right)
**   2Ch-2Eh  pan of core 1 channel A, B, C
**
** 23h, 27h, 2Bh and 2Fh are not connected, and nothing sits at 30h and up.
** 0Eh/0Fh and 1Eh/1Fh are the cores' GPIO registers: plain storage, unless
** something is attached to core 0's (nothing is, on the cartridge). The sheet puts LEDs on the low four bits of core 1's port B;
** they are kept as the value written, nothing drives them. */
#define Y8960_SSGS_CORE_REGS    0x10
#define Y8960_SSGS_VOLUME_BASE  0x20
#define Y8960_SSGS_PAN_BASE     0x28
#define Y8960_SSGS_REG_END      0x30
#define Y8960_SSGS_GPIO_A       0x0E
#define Y8960_SSGS_GPIO_B       0x0F

/* The DIP switch on the second core's port A. Bit 1 high cuts the slot off
** from the cartridge, so whatever can read the switch at all sees it low.
** Bit 0 has no pull and no switch on the prototype; 0 stands in for it. */
#define Y8960_SSGS_DIP_SWITCH   0x00

/* The volume follows the mixer's curve (Y8960Pan.h): 0 mute, 1-127 linear,
** bit 7 unity. The sheet's default of 128 is unity. */
#define Y8960_SSGS_VOLUME_RESET 0x80

static Int16 voltTable[16];
static Int16 voltEnvTable[32];

static const UInt8 regMask[14] = {
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0x3f,
    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f
};

/* The cartridge's reset values (ssg_core.v), not the AY8910's zeros: each
** tone at its longest period, the envelope at its longest period with all
** four shape bits set, everything else 0. The volumes start at 0, so none of
** this sounds; it shows in what A2h reads back. The GPIO comes up cleared. */
static const UInt8 resetValue[Y8960_SSGS_CORE_REGS] = {
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xff, 0xff, 0x0f, 0x00, 0x00
};

typedef struct {
    UInt8  regs[Y8960_SSGS_CORE_REGS];

    UInt32 tonePhase[Y8960_SSGS_CHANNELS];
    UInt32 toneStep[Y8960_SSGS_CHANNELS];

    UInt32 noisePhase;
    UInt32 noiseStep;
    UInt32 noiseRand;
    Int16  noiseVolume;

    UInt8  envShape;
    UInt32 envStep;
    UInt32 envPhase;

    UInt8  enable;
    UInt8  ampVolume[Y8960_SSGS_CHANNELS];
} Y8960SsgCore;

struct Y8960SsgsChip {
    Mixer* mixer;
    Int32  handle;
    Int32  debugHandle;

    /* Null on the cartridge, where core 0's GPIO registers are storage only. */
    Y8960SsgsReadCb  ioPortReadCb;
    Y8960SsgsReadCb  ioPortPollCb;
    Y8960SsgsWriteCb ioPortWriteCb;
    void*            ioPortRef;

    UInt8  address;

    Y8960SsgCore core[Y8960_SSGS_CORES];

    /* Per mixing channel: core 0's A, B, C, then core 1's. */
    UInt8  volume[Y8960_SSGS_MIX_CHANNELS];
    UInt8  pan[Y8960_SSGS_MIX_CHANNELS];
    Int32  gain[Y8960_SSGS_MIX_CHANNELS][2];   /* derived from volume and pan, Y8960_GAIN_ONE units */

    /* One filter pair for the mixed output, not one per core: the two cores
    ** share the cartridge's analog stage. */
    Int32  ctrlVolume[2];
    Int32  oldSampleVolume[2];
    Int32  daVolume[2];

    Int32  buffer[AUDIO_STEREO_BUFFER_SIZE];
};

static Int32* y8960SsgsSync(void* ref, UInt32 count);

static void buildVoltTables(void)
{
    DoubleT v = 0x26a9;
    int i;

    for (i = 15; i >= 0; i--) {
        voltTable[i] = (Int16)v;
        voltEnvTable[2 * i + 0] = (Int16)v;
        voltEnvTable[2 * i + 1] = (Int16)v;
        v *= 0.70794578438413791080221494218943;
    }

    /* The SSGS's cores are YM2149 equivalents, so the envelope runs at the
    ** YM2149's 32 steps rather than the AY-3-8910's 16. */
    v = 0x26a9;
    for (i = 31; i >= 0; i--) {
        voltEnvTable[i] = (Int16)v;
        v *= 0.84139514164519509115274189380029;
    }

    /* Zero the silent level. The pan pots multiply each channel before the
    ** output is filtered, so a channel that is silent but not zero would place
    ** a direct voltage somewhere in the stereo image. */
    for (i = 0; i < 16; i++) {
        voltTable[i] -= voltTable[0];
    }
    for (i = 0; i < 32; i++) {
        voltEnvTable[i] -= voltEnvTable[0];
    }
}

/* The mixing channel a volume or pan register addresses: the core in bit 2,
** the channel in bits 1-0, with channel 3 the unconnected slot. */
static int mixChannel(UInt8 address)
{
    int slot = address & 7;

    if ((slot & 3) == 3) {
        return -1;
    }
    return (slot >> 2) * Y8960_SSGS_CHANNELS + (slot & 3);
}

static void updateGain(Y8960SsgsChip* chip, int ch)
{
    Int32 v = y8960VolumeGain(chip->volume[ch]);
    Int32 pan[2];

    y8960PanGain(chip->pan[ch], pan);
    chip->gain[ch][0] = pan[0] * v / Y8960_GAIN_ONE;
    chip->gain[ch][1] = pan[1] * v / Y8960_GAIN_ONE;
}

static void updateRegister(Y8960SsgsChip* chip, UInt8 address, UInt8 data)
{
    Y8960SsgCore* c;
    int core;
    int sub;
    UInt32 period;

    if (address >= Y8960_SSGS_REG_END) {
        return;
    }

    if (address >= Y8960_SSGS_VOLUME_BASE) {
        int ch = mixChannel(address);
        if (ch < 0) {
            return;
        }
        mixerSync(chip->mixer);
        if (address >= Y8960_SSGS_PAN_BASE) {
            chip->pan[ch] = data;
        }
        else {
            chip->volume[ch] = data;
        }
        updateGain(chip, ch);
        return;
    }

    core = address / Y8960_SSGS_CORE_REGS;
    sub  = address % Y8960_SSGS_CORE_REGS;
    c    = &chip->core[core];

    if (sub == Y8960_SSGS_GPIO_A || sub == Y8960_SSGS_GPIO_B) {
        c->regs[sub] = data;
        if (core == 0 && chip->ioPortWriteCb != NULL) {
            chip->ioPortWriteCb(chip->ioPortRef, (UInt16)(sub - Y8960_SSGS_GPIO_A), data);
        }
        return;
    }

    mixerSync(chip->mixer);

    data &= regMask[sub];
    c->regs[sub] = data;

    switch (sub) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        period = c->regs[sub & 6] | ((Int32)(c->regs[sub | 1]) << 8);
        c->toneStep[sub >> 1] = period > 0 ? BASE_PHASE_STEP / period : 1 << 31;
        break;

    case 6:
        period = data ? data : 1;
        c->noiseStep = period > 0 ? BASE_PHASE_STEP / period : 1 << 31;
        break;

    case 7:
        c->enable = data;
        break;

    case 8:
    case 9:
    case 10:
        c->ampVolume[sub - 8] = data;
        break;

    case 11:
    case 12:
        period = 16 * (c->regs[11] | ((UInt32)c->regs[12] << 8));
        c->envStep = BASE_PHASE_STEP / (period ? period : 8);
        break;

    case 13:
        if (data < 4) data = 0x09;
        if (data < 8) data = 0x0f;
        c->envShape = data;
        c->envPhase = 0;
        break;
    }
}

/* The register pointer is six bits wide, so 40h and up land on 00h-3Fh. */
void y8960SsgsWriteAddress(Y8960SsgsChip* chip, UInt8 address)
{
    chip->address = address & 0x3F;
}

void y8960SsgsWriteData(Y8960SsgsChip* chip, UInt8 data)
{
    updateRegister(chip, chip->address, data);
}

/* What a register holds, without side effects: the debugger's view. */
static UInt8 peekRegister(Y8960SsgsChip* chip, UInt8 address)
{
    if (address >= Y8960_SSGS_REG_END) {
        return 0xFF;
    }
    if (address >= Y8960_SSGS_VOLUME_BASE) {
        int ch = mixChannel(address);
        if (ch < 0) {
            return 0xFF;
        }
        return (address >= Y8960_SSGS_PAN_BASE) ? chip->pan[ch] : chip->volume[ch];
    }
    return chip->core[address / Y8960_SSGS_CORE_REGS].regs[address % Y8960_SSGS_CORE_REGS];
}

/* Registers the chip does not have read as all ones rather than as zero: the
** bus ANDs what every device drives, so ones are how a device says it is not
** answering. The registers it does have read back what was written, as the
** PSG this is built from does. This covers both cores and is for the probe
** and the debugger; what the cartridge drives is y8960SsgsReadPort(). */
UInt8 y8960SsgsReadData(Y8960SsgsChip* chip)
{
    UInt8 address = chip->address;
    int core;
    int sub;

    if (address >= Y8960_SSGS_VOLUME_BASE) {
        return peekRegister(chip, address);
    }

    core = address / Y8960_SSGS_CORE_REGS;
    sub  = address % Y8960_SSGS_CORE_REGS;

    if ((sub == Y8960_SSGS_GPIO_A || sub == Y8960_SSGS_GPIO_B) &&
        core == 0 && chip->ioPortReadCb != NULL) {
        chip->core[0].regs[sub] = chip->ioPortReadCb(chip->ioPortRef, (UInt16)(sub - Y8960_SSGS_GPIO_A));
    }

    return chip->core[core].regs[sub];
}

/* Only the second core answers, and only for its own registers: 10h-1Fh and
** its volume and pan at 24h-27h and 2Ch-2Fh. Every other pointer reads FFh,
** which leaves the bus to the machine's PSG; that PSG in turn floats for
** register numbers of 10h and up, so the two never drive the bus together.
** The second core's port A reads the DIP switch rather than what was written,
** and R7 reads its top two bits as 10. */
UInt8 y8960SsgsReadPort(Y8960SsgsChip* chip)
{
    UInt8 address = chip->address;
    int sub = address % Y8960_SSGS_CORE_REGS;

    if (address >= Y8960_SSGS_CORE_REGS && address < Y8960_SSGS_VOLUME_BASE) {
        switch (sub) {
        case 7:
            return (UInt8)(chip->core[1].regs[7] | 0x80);
        case Y8960_SSGS_GPIO_A:
            return Y8960_SSGS_DIP_SWITCH;
        default:
            return chip->core[1].regs[sub];
        }
    }

    if (address >= Y8960_SSGS_VOLUME_BASE && address < Y8960_SSGS_REG_END &&
        (address & 0x04)) {
        return peekRegister(chip, address);
    }

    return 0xFF;
}

void y8960SsgsSetIoPort(Y8960SsgsChip* chip, Y8960SsgsReadCb readCb,
                        Y8960SsgsReadCb pollCb, Y8960SsgsWriteCb writeCb, void* ref)
{
    chip->ioPortReadCb  = readCb;
    chip->ioPortPollCb  = pollCb;
    chip->ioPortWriteCb = writeCb;
    chip->ioPortRef     = ref;
}

UInt8 y8960SsgsGetLed(Y8960SsgsChip* chip)
{
    return chip->core[1].regs[Y8960_SSGS_GPIO_B] & 0x0F;
}

void y8960SsgsReset(Y8960SsgsChip* chip)
{
    int core;
    int i;

    if (chip == NULL) {
        return;
    }

    chip->address = 0;

    /* The filters hold the output's recent history. Leaving it in place lets
    ** what the chip was playing before the reset bleed out of it afterwards,
    ** and the time constant is long enough for that to last. */
    for (i = 0; i < 2; i++) {
        chip->ctrlVolume[i]      = 0;
        chip->oldSampleVolume[i] = 0;
        chip->daVolume[i]        = 0;
    }

    for (core = 0; core < Y8960_SSGS_CORES; core++) {
        Y8960SsgCore* c = &chip->core[core];

        memset(c, 0, sizeof(*c));
        c->noiseRand   = 1;
        c->noiseVolume = 1;

        for (i = 0; i < Y8960_SSGS_CORE_REGS; i++) {
            updateRegister(chip, (UInt8)(core * Y8960_SSGS_CORE_REGS + i), resetValue[i]);
        }
    }

    /* The specification's reset values: unity and centre. */
    for (i = 0; i < Y8960_SSGS_MIX_CHANNELS; i++) {
        chip->volume[i] = Y8960_SSGS_VOLUME_RESET;
        chip->pan[i]    = Y8960_PAN_CENTER;
        updateGain(chip, i);
    }
}

static void getDebugInfo(Y8960SsgsChip* chip, DbgDevice* dbgDevice)
{
    DbgRegisterBank* regBank;
    int address;

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegs(), Y8960_SSGS_REG_END);

    for (address = 0; address < Y8960_SSGS_REG_END; address++) {
        char name[8];
        sprintf(name, "R%.2x", address);
        dbgRegisterBankAddRegister(regBank, address, name, 8, peekRegister(chip, (UInt8)address));
    }
}

void y8960SsgsSaveState(Y8960SsgsChip* chip)
{
    SaveState* state = saveStateOpenForWrite("y8960ssgs");
    char tag[32];
    int core;
    int i;

    saveStateSet(state, "address", chip->address);

    for (i = 0; i < 2; i++) {
        sprintf(tag, "ctrlVolume%d", i);
        saveStateSet(state, tag, chip->ctrlVolume[i]);
        sprintf(tag, "oldSampleVolume%d", i);
        saveStateSet(state, tag, chip->oldSampleVolume[i]);
        sprintf(tag, "daVolume%d", i);
        saveStateSet(state, tag, chip->daVolume[i]);
    }

    saveStateSetBuffer(state, "volume", chip->volume, sizeof(chip->volume));
    saveStateSetBuffer(state, "pan",    chip->pan,    sizeof(chip->pan));

    for (core = 0; core < Y8960_SSGS_CORES; core++) {
        Y8960SsgCore* c = &chip->core[core];

        sprintf(tag, "c%d_regs", core);
        saveStateSetBuffer(state, tag, c->regs, sizeof(c->regs));

        for (i = 0; i < Y8960_SSGS_CHANNELS; i++) {
            sprintf(tag, "c%d_tonePhase%d", core, i);
            saveStateSet(state, tag, c->tonePhase[i]);
            sprintf(tag, "c%d_toneStep%d", core, i);
            saveStateSet(state, tag, c->toneStep[i]);
        }

        sprintf(tag, "c%d_noisePhase", core);  saveStateSet(state, tag, c->noisePhase);
        sprintf(tag, "c%d_noiseStep", core);   saveStateSet(state, tag, c->noiseStep);
        sprintf(tag, "c%d_noiseRand", core);   saveStateSet(state, tag, c->noiseRand);
        sprintf(tag, "c%d_noiseVolume", core); saveStateSet(state, tag, c->noiseVolume);
        sprintf(tag, "c%d_envShape", core);    saveStateSet(state, tag, c->envShape);
        sprintf(tag, "c%d_envStep", core);     saveStateSet(state, tag, c->envStep);
        sprintf(tag, "c%d_envPhase", core);    saveStateSet(state, tag, c->envPhase);
    }

    saveStateClose(state);
}

void y8960SsgsLoadState(Y8960SsgsChip* chip)
{
    SaveState* state = saveStateOpenForRead("y8960ssgs");
    char tag[32];
    int core;
    int i;

    chip->address = (UInt8)saveStateGet(state, "address", 0);

    for (i = 0; i < 2; i++) {
        sprintf(tag, "ctrlVolume%d", i);
        chip->ctrlVolume[i] = saveStateGet(state, tag, 0);
        sprintf(tag, "oldSampleVolume%d", i);
        chip->oldSampleVolume[i] = saveStateGet(state, tag, 0);
        sprintf(tag, "daVolume%d", i);
        chip->daVolume[i] = saveStateGet(state, tag, 0);
    }

    /* A state without these came from before the registers existed; the
    ** reset values are what such a chip was playing at. */
    memset(chip->volume, Y8960_SSGS_VOLUME_RESET, sizeof(chip->volume));
    memset(chip->pan,    Y8960_PAN_CENTER,        sizeof(chip->pan));
    saveStateGetBuffer(state, "volume", chip->volume, sizeof(chip->volume));
    saveStateGetBuffer(state, "pan",    chip->pan,    sizeof(chip->pan));

    for (core = 0; core < Y8960_SSGS_CORES; core++) {
        Y8960SsgCore* c = &chip->core[core];

        sprintf(tag, "c%d_regs", core);
        saveStateGetBuffer(state, tag, c->regs, sizeof(c->regs));

        for (i = 0; i < Y8960_SSGS_CHANNELS; i++) {
            sprintf(tag, "c%d_tonePhase%d", core, i);
            c->tonePhase[i] = saveStateGet(state, tag, 0);
            sprintf(tag, "c%d_toneStep%d", core, i);
            c->toneStep[i] = saveStateGet(state, tag, 0);
        }

        sprintf(tag, "c%d_noisePhase", core);  c->noisePhase  = saveStateGet(state, tag, 0);
        sprintf(tag, "c%d_noiseStep", core);   c->noiseStep   = saveStateGet(state, tag, 0);
        sprintf(tag, "c%d_noiseRand", core);   c->noiseRand   = saveStateGet(state, tag, 1);
        sprintf(tag, "c%d_noiseVolume", core); c->noiseVolume = (Int16)saveStateGet(state, tag, 1);
        sprintf(tag, "c%d_envShape", core);    c->envShape    = (UInt8)saveStateGet(state, tag, 0);
        sprintf(tag, "c%d_envStep", core);     c->envStep     = saveStateGet(state, tag, 0);
        sprintf(tag, "c%d_envPhase", core);    c->envPhase    = saveStateGet(state, tag, 0);

        /* Derived from regs, so they are rebuilt rather than saved. */
        c->enable = c->regs[7];
        for (i = 0; i < Y8960_SSGS_CHANNELS; i++) {
            c->ampVolume[i] = c->regs[8 + i];
        }
    }

    for (i = 0; i < Y8960_SSGS_MIX_CHANNELS; i++) {
        updateGain(chip, i);
    }

    saveStateClose(state);
}

Y8960SsgsChip* y8960SsgsCreate(Mixer* mixer, const char* name)
{
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    Y8960SsgsChip* chip = (Y8960SsgsChip*)calloc(1, sizeof(Y8960SsgsChip));

    buildVoltTables();

    chip->mixer = mixer;

    /* Six channels out of one stereo device: the pan pots place each of them,
    ** so the mixer cannot be left to do it. */
    chip->handle      = mixerRegisterChannel(mixer, MIXER_CHANNEL_Y8960, 1, y8960SsgsSync, NULL, chip);
    chip->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, name, &dbgCallbacks, chip);

    y8960SsgsReset(chip);

    return chip;
}

void y8960SsgsDestroy(Y8960SsgsChip* chip)
{
    debugDeviceUnregister(chip->debugHandle);
    mixerUnregisterChannel(chip->mixer, chip->handle);

    free(chip);
}

static Int32* y8960SsgsSync(void* ref, UInt32 count)
{
    Y8960SsgsChip* chip = (Y8960SsgsChip*)ref;
    UInt32 index;

    /* The mixer block's volume for this block, applied before the filters,
    ** which are linear, so that the sum stays within 32 bits. */
    Int32 blockGain = y8960MixerGain(Y8960_VOL_SSGS);

    for (index = 0; index < count; index++) {
        Int32 sampleVolumeL = 0;
        Int32 sampleVolumeR = 0;
        int core;

        for (core = 0; core < Y8960_SSGS_CORES; core++) {
            Y8960SsgCore* c = &chip->core[core];
            Int16 envVolume;
            int channel;

            /* Update noise generator */
            c->noisePhase += c->noiseStep;
            while (c->noisePhase >> 28) {
                c->noisePhase  -= 0x10000000;
                c->noiseVolume ^= ((c->noiseRand + 1) >> 1) & 1;
                /* Bit 1 of the mask must stay clear: the line above tracks bit 0
                ** of the next state by XOR-ing bits 0 and 1 of the current one. */
                c->noiseRand    = (c->noiseRand ^ (0x24000 * (c->noiseRand & 1))) >> 1;
            }

            /* Update envelope phase */
            c->envPhase += c->envStep;
            if ((c->envShape & 1) && (c->envPhase >> 28)) {
                c->envPhase = 0x10000000;
            }

            /* Calculate envelope volume */
            envVolume = (Int16)((c->envPhase >> 23) & 0x1f);
            if (((c->envPhase >> 27) & (c->envShape + 1) ^ (~c->envShape >> 1)) & 2) {
                envVolume ^= 0x1f;
            }

            for (channel = 0; channel < Y8960_SSGS_CHANNELS; channel++) {
                UInt32 enable = c->enable >> channel;
                UInt32 noiseEnable = ((enable >> 3) | c->noiseVolume) & 1;
                UInt32 phaseStep = (~enable & 1) * c->toneStep[channel];
                UInt32 tonePhase = c->tonePhase[channel];
                UInt32 tone = 0;
                Int32  oversample = 16;
                Int32  sampleVolume;
                const Int32* gain = chip->gain[core * Y8960_SSGS_CHANNELS + channel];

                /* Perform 16x oversampling */
                while (oversample--) {
                    tonePhase += phaseStep;
                    tone += (enable | (tonePhase >> 31)) & noiseEnable;
                }

                c->tonePhase[channel] = tonePhase;

                if (c->ampVolume[channel] & 0x10) {
                    sampleVolume = (Int16)tone * voltEnvTable[envVolume] / 16;
                }
                else {
                    sampleVolume = (Int16)tone * voltTable[c->ampVolume[channel]] / 16;
                }

                sampleVolumeL += sampleVolume * gain[0] / Y8960_GAIN_ONE;
                sampleVolumeR += sampleVolume * gain[1] / Y8960_GAIN_ONE;
            }
        }

        sampleVolumeL = sampleVolumeL * blockGain / Y8960_GAIN_ONE;
        sampleVolumeR = sampleVolumeR * blockGain / Y8960_GAIN_ONE;

        /* Perform DC offset filtering */
        chip->ctrlVolume[0] = sampleVolumeL - chip->oldSampleVolume[0] + 0x3fe7 * chip->ctrlVolume[0] / 0x4000;
        chip->oldSampleVolume[0] = sampleVolumeL;
        chip->ctrlVolume[1] = sampleVolumeR - chip->oldSampleVolume[1] + 0x3fe7 * chip->ctrlVolume[1] / 0x4000;
        chip->oldSampleVolume[1] = sampleVolumeR;

        /* Perform simple 1 pole low pass IIR filtering */
        chip->daVolume[0] += 2 * (chip->ctrlVolume[0] - chip->daVolume[0]) / 3;
        chip->daVolume[1] += 2 * (chip->ctrlVolume[1] - chip->daVolume[1]) / 3;

        chip->buffer[2 * index + 0] = 9 * chip->daVolume[0];
        chip->buffer[2 * index + 1] = 9 * chip->daVolume[1];
    }

    return chip->buffer;
}
