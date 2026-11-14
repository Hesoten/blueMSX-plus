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
** The cartridge carries an SCC-equivalent circuit (IKASCC in the hardware),
** not a Konami SCC, so it is emulated as its own chip rather than sharing
** the machine's. Divergence in behaviour is expected as the hardware is
** finished; keeping them separate is what makes that possible.
** IKASCC was built from the SCC die, so the real chip's quirks hold for it
** too: behaviour fixes to SCC.c are ported here unless the hardware is
** found to differ.
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
#include "Board.h"
#include "SaveState.h"
#include "DebugDeviceManager.h"
#include "Language.h"
#include <stdlib.h>
#include <string.h>


//#define BASE_PHASE_STEP 0x28959becUL  /* = (1 << 28) * 3579545 / 32 / 44100 */
#define BASE_PHASE_STEP 0xA2566FBUL  /* = (1 << 28) * 3579545 / 32 / (44100 * 4) */

#define ROTATE_OFF 32
#define ROTATE_ON  28

#define OFFSETOF(s, a) ((int)(&((s*)0)->a))

static Int32* sccSync(Y8960SccChip* scc, UInt32 count);


struct Y8960SccChip
{
    Mixer* mixer;
    Int32  handle;
    Int32  debugHandle;
    
    Y8960SccMode mode;
    UInt8 deformReg;
    Int8 curWave[5];
    Int8 wave[5][32];
    UInt32 period[5];
    UInt32 phase[5];
    UInt32 phaseStep[5];
    int  volume[5];
    int  nextVolume[5];
    UInt8 enable;
    UInt16 bus;
    int rotate[5];
    int readOnly[5];
    Int32 oldSample[5];
    UInt32 effPeriod[5];
    UInt32 deformRef;
    UInt32 deformTicks;

    UInt8 pan[5];
    Int32 panGain[5][2];   /* from pan, Y8960_GAIN_ONE units */

    Int32 in[95];
    Int32 inR[95];         /* filter4's history for the right side; in[] is the left */
    Int32 inHp[3];
    Int32 outHp[3];

    Int32  buffer[AUDIO_STEREO_BUFFER_SIZE];
};

#define SCC_TICK (boardFrequency() / 3579545)

static UInt32 sccEffectivePeriod(UInt32 period, UInt8 deformReg)
{
    if (deformReg & 2) {
        return period & 0xff;
    }
    if (deformReg & 1) {
        return period >> 8;
    }
    return period;
}

static UInt32 sccPhaseStep(UInt32 effPeriod)
{
    return effPeriod > 8 ? BASE_PHASE_STEP / (1 + effPeriod) : 0;
}

/* Chip clocks since the rotation counter last restarted. deformTicks is
** folded forward on emulation-thread accesses so the 32-bit board time
** difference never has to span more than one wrap. */
static UInt32 sccDeformTicks(Y8960SccChip* scc)
{
    return scc->deformTicks + (boardSystemTime() - scc->deformRef) / SCC_TICK;
}

static void sccFoldDeformTicks(Y8960SccChip* scc)
{
    UInt32 ticks = (boardSystemTime() - scc->deformRef) / SCC_TICK;

    scc->deformTicks += ticks;
    scc->deformRef   += ticks * SCC_TICK;
}

static void sccRestartDeformTicks(Y8960SccChip* scc)
{
    scc->deformTicks = 0;
    scc->deformRef   = boardSystemTime();
}

static void sccSetPan(Y8960SccChip* scc, int channel, UInt8 value)
{
    scc->pan[channel] = value;
    y8960PanGain(value, scc->panGain[channel]);
}

void y8960SccLoadState(Y8960SccChip* scc)
{
    SaveState* state = saveStateOpenForRead("y8960scc");
    char tag[32];
    UInt32 curWave;
    int i;

    scc->mode      =         saveStateGet(state, "mode", Y8960_SCC_COMPATIBLE);
    scc->deformReg = (UInt8) saveStateGet(state, "deformReg", 0);
    
    for (i = 0; i < 5; i++) {
        int j;
        for (j = 0; j < 32; j++) {
            sprintf(tag, "wave%d%d", i, j);
            scc->wave[i][j] = (UInt8)saveStateGet(state, tag, 0);
        }

        sprintf(tag, "period%d", i);
        scc->period[i] = saveStateGet(state, tag, 0);
        
        sprintf(tag, "phase%d", i);
        scc->phase[i] = saveStateGet(state, tag, 0);
        
        sprintf(tag, "volume%d", i);
        scc->volume[i] = saveStateGet(state, tag, 0);
        
        sprintf(tag, "nextVolume%d", i);
        scc->nextVolume[i] = saveStateGet(state, tag, 0);
        
        sprintf(tag, "rotate%d", i);
        scc->rotate[i] = saveStateGet(state, tag, 0);
        
        sprintf(tag, "readOnly%d", i);
        scc->readOnly[i] = saveStateGet(state, tag, 0);
        
        sprintf(tag, "oldSample%d", i);
        scc->oldSample[i] = saveStateGet(state, tag, 0);

        sprintf(tag, "effPeriod%d", i);
        scc->effPeriod[i] = saveStateGet(state, tag, sccEffectivePeriod(scc->period[i], scc->deformReg));
        scc->phaseStep[i] = sccPhaseStep(scc->effPeriod[i]);

        curWave = scc->oldSample[i] >= 0 && scc->oldSample[i] < 32 ? (UInt8)scc->wave[i][scc->oldSample[i]] : 0;
        sprintf(tag, "curWave%d", i);
        scc->curWave[i] = (Int8)saveStateGet(state, tag, curWave);

        sprintf(tag, "pan%d", i);
        sccSetPan(scc, i, (UInt8)saveStateGet(state, tag, Y8960_PAN_CENTER));
    }

    scc->enable = (UInt8)saveStateGet(state, "enable", 0xff);

    scc->deformRef   = saveStateGet(state, "deformRef", boardSystemTime());
    scc->deformTicks = saveStateGet(state, "deformTicks", 0);

    saveStateClose(state);
}

void y8960SccSaveState(Y8960SccChip* scc)
{
    SaveState* state = saveStateOpenForWrite("y8960scc");
    char tag[32];
    int i;

    saveStateSet(state, "mode", scc->mode);
    saveStateSet(state, "deformReg", scc->deformReg);
    
    for (i = 0; i < 5; i++) {
        int j;
        for (j = 0; j < 32; j++) {
            sprintf(tag, "wave%d%d", i, j);
            saveStateSet(state, tag, scc->wave[i][j]);
        }

        sprintf(tag, "period%d", i);
        saveStateSet(state, tag, scc->period[i]);
        
        sprintf(tag, "phase%d", i);
        saveStateSet(state, tag, scc->phase[i]);
        
        sprintf(tag, "step%d", i);
        saveStateSet(state, tag, scc->phaseStep[i]);
        
        sprintf(tag, "volume%d", i);
        saveStateSet(state, tag, scc->volume[i]);
        
        sprintf(tag, "nextVolume%d", i);
        saveStateSet(state, tag, scc->nextVolume[i]);
        
        sprintf(tag, "rotate%d", i);
        saveStateSet(state, tag, scc->rotate[i]);
        
        sprintf(tag, "readOnly%d", i);
        saveStateSet(state, tag, scc->readOnly[i]);
        
        sprintf(tag, "oldSample%d", i);
        saveStateSet(state, tag, scc->oldSample[i]);

        sprintf(tag, "effPeriod%d", i);
        saveStateSet(state, tag, scc->effPeriod[i]);

        sprintf(tag, "curWave%d", i);
        saveStateSet(state, tag, (UInt8)scc->curWave[i]);

        sprintf(tag, "pan%d", i);
        saveStateSet(state, tag, scc->pan[i]);
    }

    saveStateSet(state, "enable", scc->enable);

    saveStateSet(state, "deformRef",   scc->deformRef);
    saveStateSet(state, "deformTicks", scc->deformTicks);

    saveStateClose(state);
}

static UInt8 sccGetWave(Y8960SccChip* scc, UInt8 channel, UInt8 address)
{
    if (scc->rotate[channel] == ROTATE_OFF) {
        return scc->wave[channel][address & 0x1f];
    } 
    else {
        UInt8 periodCh = channel;
        UInt32 shift;

         if ((scc->deformReg & 0xc0) == 0x80) {
             if (channel == 4) {
                 periodCh = 3;
             }
         }
         else if (channel == 3 && scc->mode != Y8960_SCC_PLUS) {
             periodCh = 4;
         }

         shift = sccDeformTicks(scc) / (scc->effPeriod[periodCh] + 1);

        return scc->wave[channel][(address + shift) & 0x1f];
    }
}

static void sccUpdateWave(Y8960SccChip* scc, UInt8 channel, UInt8 address, UInt8 value)
{
    if (!scc->readOnly[channel]) {
        UInt8 pos = address & 0x1f;

        scc->bus = value;

        scc->wave[channel][pos] = value;
        if ((scc->mode != Y8960_SCC_PLUS) && (channel == 3)) {
            scc->wave[4][pos] = scc->wave[3][pos];
        }
    }
}

static void sccUpdateFreqAndVol(Y8960SccChip* scc, UInt8 address, UInt8 value)
{
    address &= 0x0f;
    /* Drop the FDC/HDD boost on writes that make any voice audible
    ** (non-zero volume or channel-enable bit). Setup writes (frequency
    ** 0x00-0x09, zero-volume mutes) stay free so loaders touching SCC
    ** during transfer keep the boost. */
    boardCheckSccBoostKill(address, value);
    if (address < 0x0a) {
        UInt8 channel = address / 2;
        UInt32 period;

        mixerSync(scc->mixer);

        if (address & 1) {
            scc->period[channel] = ((value & 0xf) << 8) | (scc->period[channel] & 0xff);
        } 
        else {
            scc->period[channel] = (scc->period[channel] & 0xf00) | (value & 0xff);
        }
        if (scc->deformReg & 0x20) {
            scc->phase[channel] = 0;
            sccRestartDeformTicks(scc);
        }
        period = sccEffectivePeriod(scc->period[channel], scc->deformReg);
        scc->effPeriod[channel] = period;
        
        scc->phaseStep[channel] = sccPhaseStep(period);
        
        scc->volume[channel] = scc->nextVolume[channel];
        scc->phase[channel] &= 0x1f << 23;
        scc->oldSample[channel] = 0xff;
    } 
    else if (address < 0x0f) {
        scc->nextVolume[address - 0x0a] = value & 0x0f;
    } 
    else {
        scc->enable = value;
    }
}

static void sccUpdateDeformation(Y8960SccChip* scc, UInt8 value)
{
    int channel;

    if (value == scc->deformReg) {
        return;
    }

    mixerSync(scc->mixer);

    scc->deformReg = value;
    
    sccRestartDeformTicks(scc);

    if (scc->mode != Y8960_SCC_REAL) {
        value &= ~0x80;
    }

    switch (value & 0xc0) {
        case 0x00:
            for (channel = 0; channel < 5; channel++) {
                scc->rotate[channel]   = ROTATE_OFF;
                scc->readOnly[channel] = 0;
            }
            break;
        case 0x40:
            for (channel = 0; channel < 5; channel++) {
                scc->rotate[channel]   = ROTATE_ON;
                scc->readOnly[channel] = 1;
            }
            break;
        case 0x80:
            for (channel = 0; channel < 3; channel++) {
                scc->rotate[channel]   = ROTATE_OFF;
                scc->readOnly[channel] = 0;
            }
            for (channel = 3; channel < 5; channel++) {
                scc->rotate[channel]   = ROTATE_ON;
                scc->readOnly[channel] = 1;
            }
            break;
        case 0xC0:
            for (channel = 0; channel < 3; channel++) {
                scc->rotate[channel]   = ROTATE_ON;
                scc->readOnly[channel] = 1;
            }
            for (channel = 3; channel < 5; channel++) {
                scc->rotate[channel]   = ROTATE_OFF;
                scc->readOnly[channel] = 1;
            }
            break;
    }
}

void y8960SccReset(Y8960SccChip* scc) {
    int channel;

    if (scc->mode != Y8960_SCC_REAL) {
        y8960SccSetMode(scc, Y8960_SCC_COMPATIBLE);
    }

    for (channel = 0; channel < 5; channel++) {
        scc->rotate[channel]     = ROTATE_OFF;
        scc->readOnly[channel]   = 0;
        sccSetPan(scc, channel, Y8960_PAN_CENTER);
    }

    scc->deformReg = 0;
    scc->enable      = 0;
}

void y8960SccSetMode(Y8960SccChip* scc, Y8960SccMode newMode)
{
    scc->mode = newMode;
}

static void getDebugInfo(Y8960SccChip* scc, DbgDevice* dbgDevice)
{
    static UInt8 ram[0x100];
    int i;

    for (i = 0; i < 0x100; i++) {
        y8960SccPeek(scc, i);
    }

    dbgDeviceAddMemoryBlock(dbgDevice, langDbgMemScc(), 1, 0, 0x100, ram);
}

Y8960SccChip* y8960SccCreate(Mixer* mixer)
{
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    Y8960SccChip* scc = (Y8960SccChip*)calloc(1, sizeof(Y8960SccChip));
    int channel;

    scc->mixer = mixer;

    /* Chip power-on contents: waveforms FFh, full volume, channels off. */
    memset(scc->wave, 0xff, sizeof(scc->wave));
    for (channel = 0; channel < 5; channel++) {
        scc->volume[channel]     = 15;
        scc->nextVolume[channel] = 15;
        scc->oldSample[channel]  = 0xff;
    }
    scc->bus = 0xFFFF;

//    scc->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, langDbgDevScc(), &dbgCallbacks, scc);

    scc->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_Y8960, 1, sccSync, NULL, scc);

    sccRestartDeformTicks(scc);
    y8960SccReset(scc);

    return scc;
}

void y8960SccDestroy(Y8960SccChip* scc)
{
//    debugDeviceUnregister(scc->debugHandle);
    mixerUnregisterChannel(scc->mixer, scc->handle);
    free(scc);
}

UInt8 y8960SccRead(Y8960SccChip* scc, UInt8 address)
{
    sccFoldDeformTicks(scc);

    switch (scc->mode) {

    case Y8960_SCC_REAL:
        if (address < 0x80) {
            return sccGetWave(scc, address >> 5, address);
        } 
        
        if (address < 0xe0) {
            return 0xff;
        }

        sccUpdateDeformation(scc, 0xff);

        return 0xff;

    case Y8960_SCC_COMPATIBLE:
        if (address < 0x80) {
            return sccGetWave(scc, address >> 5, address);
        } 
        
        if (address < 0xa0) {
            return 0xff;
        }
        
        if (address < 0xc0) {
            return sccGetWave(scc, 4, address);
        } 

        if (address < 0xe0) {
            sccUpdateDeformation(scc, 0xff);
            return 0xff;
        }
 
        return 0xff;

    case Y8960_SCC_PLUS:
        if (address < 0xa0) {
            return sccGetWave(scc, address >> 5, address);
        } 
        
        if (address < 0xc0) {
            return 0xff;
        } 
        
        if (address < 0xe0) {
            sccUpdateDeformation(scc, 0xff);
            return 0xff;
        }

        return 0xff;
    }

    return 0xff;
}

UInt8 y8960SccPeek(Y8960SccChip* scc, UInt8 address)
{
    switch (scc->mode) {

    case Y8960_SCC_REAL:
        if (address < 0x80) {
            return sccGetWave(scc, address >> 5, address);
        } 
        
        if (address < 0xe0) {
            return 0xff;
        }

        return 0xff;

    case Y8960_SCC_COMPATIBLE:
        if (address < 0x80) {
            return sccGetWave(scc, address >> 5, address);
        } 
        
        if (address < 0xa0) {
            return 0xff;
        }
        
        if (address < 0xc0) {
            return sccGetWave(scc, 4, address);
        } 

        return 0xff;

    case Y8960_SCC_PLUS:
        if (address < 0xa0) {
            return sccGetWave(scc, address >> 5, address);
        } 
        
        if (address < 0xc0) {
            return 0xff;
        } 
        
        if (address < 0xe0) {
            return 0xff;
        }

        return 0xff;
    }

    return 0xff;
}

void y8960SccWrite(Y8960SccChip* scc, UInt8 address, UInt8 value)
{
    mixerSync(scc->mixer);
    sccFoldDeformTicks(scc);

    switch (scc->mode) {
    case Y8960_SCC_REAL:
        if (address < 0x80) {
            sccUpdateWave(scc, address >> 5, address, value);
            return;
        } 
        
        if (address < 0xa0) {
            sccUpdateFreqAndVol(scc, address, value);
            return;
        } 
        
        if (address < 0xe0) {
            return;
        }

        sccUpdateDeformation(scc, value);
        return;

    case Y8960_SCC_COMPATIBLE:
        if (address < 0x80) {
            sccUpdateWave(scc, address >> 5, address, value);
            return;
        } 
        
        if (address < 0xa0) {
            sccUpdateFreqAndVol(scc, address, value);
            return;
        } 
        
        if (address < 0xc0) {
            return;
        } 
        
        if (address < 0xe0) {
            sccUpdateDeformation(scc, value);
            return;
        } 

        return;

    case Y8960_SCC_PLUS:
        if (address < 0xa0) {
            sccUpdateWave(scc, address >> 5, address, value);
            return;
        } 
        
        if (address < 0xc0) {
            sccUpdateFreqAndVol(scc, address, value);
            return;
        } 
        
        if (address < 0xe0) {
            sccUpdateDeformation(scc, value);
            return;
        }

        return;
    }
}

void y8960SccWritePan(Y8960SccChip* scc, UInt8 channel, UInt8 value)
{
    if (channel >= 5) {
        return;
    }
    mixerSync(scc->mixer);
    sccSetPan(scc, channel, value);
}

void y8960SccGetDebugInfo(Y8960SccChip* scc, DbgDevice* dbgDevice)
{
}

static Int32 filter(Y8960SccChip* scc, Int32 input) {
    scc->in[4] = scc->in[3];
    scc->in[3] = scc->in[2];
    scc->in[2] = scc->in[1];
    scc->in[1] = scc->in[0];
    scc->in[0] = input;

    scc->inHp[2] = scc->inHp[1];
    scc->inHp[1] = scc->inHp[0];
    scc->inHp[0] = (1 * (scc->in[0] + scc->in[4]) + 12 * (scc->in[1] + scc->in[3]) + 45 * scc->in[2]) / 100;

    scc->outHp[2] = scc->outHp[1];
    scc->outHp[1] = scc->outHp[0];
    scc->outHp[0] =(997 * scc->inHp[0] - 1994 * scc->inHp[1] + 997 * scc->inHp[2] + 1994 * scc->outHp[1] - 994 * scc->outHp[2]) / 1000;

    return scc->outHp[0];
}

// Filter type: Low pass
// Passband: 0.0 - 750.0 Hz
// Order: 94
// Transition band: 250.0 Hz
// Stopband attenuation: 50.0 dB
//
static Int32 filter4(Int32* in, Int32 in1, Int32 in2, Int32 in3, Int32 in4)
{
    int i;
    DoubleT res;

    for (i = 0; i < 91; ++i) {
        in[i] = in[i + 4];
    }
    in[91] = in1;
    in[92] = in2;
    in[93] = in3;
    in[94] = in4;

    res =  2.8536195E-4 * (in[ 0] + in[94]) +
           9.052306E-5  * (in[ 1] + in[93]) +
          -2.6902245E-4 * (in[ 2] + in[92]) +
          -6.375284E-4  * (in[ 3] + in[91]) +
          -7.87536E-4   * (in[ 4] + in[90]) +
          -5.3910224E-4 * (in[ 5] + in[89]) +
           1.1107049E-4 * (in[ 6] + in[88]) +
           9.2801993E-4 * (in[ 7] + in[87]) +
           0.0015018889 * (in[ 8] + in[86]) +
           0.0014338732 * (in[ 9] + in[85]) +
           5.688559E-4  * (in[10] + in[84]) +
          -8.479743E-4  * (in[11] + in[83]) +
          -0.0021999443 * (in[12] + in[82]) +
          -0.0027432537 * (in[13] + in[81]) +
          -0.0019824558 * (in[14] + in[80]) +
           2.018935E-9  * (in[15] + in[79]) +
           0.0024515253 * (in[16] + in[78]) +
           0.00419754   * (in[17] + in[77]) +
           0.0041703423 * (in[18] + in[76]) +
           0.0019952168 * (in[19] + in[75]) +
          -0.0016656333 * (in[20] + in[74]) +
          -0.005242034  * (in[21] + in[73]) +
          -0.0068841926 * (in[22] + in[72]) +
          -0.005360789  * (in[23] + in[71]) +
          -8.1365916E-4 * (in[24] + in[70]) +
           0.0050464263 * (in[25] + in[69]) +
           0.00950725   * (in[26] + in[68]) +
           0.010038091  * (in[27] + in[67]) +
           0.005602208  * (in[28] + in[66]) +
          -0.00253724   * (in[29] + in[65]) +
          -0.011011368  * (in[30] + in[64]) +
          -0.015622435  * (in[31] + in[63]) +
          -0.013267951  * (in[32] + in[62]) +
          -0.0036876823 * (in[33] + in[61]) +
           0.009843254  * (in[34] + in[60]) +
           0.021394625  * (in[35] + in[59]) +
           0.02469893   * (in[36] + in[58]) +
           0.01608393   * (in[37] + in[57]) +
          -0.0032088074 * (in[38] + in[56]) +
          -0.026453404  * (in[39] + in[55]) +
          -0.043139543  * (in[40] + in[54]) +
          -0.042553578  * (in[41] + in[53]) +
          -0.018007802  * (in[42] + in[52]) +
           0.029919287  * (in[43] + in[51]) +
           0.09252273   * (in[44] + in[50]) +
           0.15504532   * (in[45] + in[49]) +
           0.20112106   * (in[46] + in[48]) +
           0.2180678    *  in[47];

    return (Int32)res;
}

static Int32* sccSync(Y8960SccChip* scc, UInt32 count)
{
    Int32* buffer  = scc->buffer;
    Int32  channel;
    UInt32 index;
    /* The mixer block's volume for this block. */
    Int32  blockGain = y8960MixerGain(Y8960_VOL_SCC);

    for (index = 0; index < count; index++) {
        Int32 masterVolume[4] = {0, 0, 0, 0};
        Int32 masterVolumeR[4] = {0, 0, 0, 0};
        int i;
        for (i = 0; i < 4; i++) {
            for (channel = 0; channel < 5; channel++) {
                Int32 phase;
                Int32 sample;

                phase = scc->phase[channel] + scc->phaseStep[channel];
                phase &= 0xfffffff;
                scc->phase[channel] = phase;

                sample = (phase >> 23) & 0x1f;

                if (sample != scc->oldSample[channel]) {
                    scc->volume[channel] = scc->nextVolume[channel];

#if 0
                    if ((sample == 15 || sample == 16) && scc->bus != 0xFFFF) {
                        scc->curWave[channel] = (UInt8)scc->bus;
                    }
                    else {
                        scc->curWave[channel] = scc->wave[channel][sample];
                    }
#else
                    scc->curWave[channel] = scc->wave[channel][sample];
#endif

                    scc->oldSample[channel] = sample;   
                }

                if (!((scc->enable >> channel) & 1)) {
                    scc->curWave[channel] = 0;
                }

                /* The chip drops the low 4 bits of each product before the sum;
                ** 400 = 25 * 16 keeps the output level the mixer is balanced for. */
                {
                    Int32 out = 400 * ((scc->curWave[channel] * scc->volume[channel]) >> 4);
                    masterVolume[i]  += out * scc->panGain[channel][0] / Y8960_GAIN_ONE;
                    masterVolumeR[i] += out * scc->panGain[channel][1] / Y8960_GAIN_ONE;
                }
            }
        }
        buffer[2 * index + 0] = (Int32)((Int64)filter4(scc->in, masterVolume[0], masterVolume[1], masterVolume[2], masterVolume[3])
                                        * blockGain / Y8960_GAIN_ONE);
        buffer[2 * index + 1] = (Int32)((Int64)filter4(scc->inR, masterVolumeR[0], masterVolumeR[1], masterVolumeR[2], masterVolumeR[3])
                                        * blockGain / Y8960_GAIN_ONE);
        scc->bus = 0xFFFF;

//        buffer[index] = (masterVolume[0] + masterVolume[1] + masterVolume[2] + masterVolume[3]);
    }

    return scc->buffer;
}

