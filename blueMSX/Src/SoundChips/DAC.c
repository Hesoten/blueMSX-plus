/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/DAC.c,v $
**
** $Revision: 1.9 $
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
/* BlipBuffer-based band-limited step synthesis + 1-pole IIR analog
** reconstruction filter (~7 kHz cutoff). DAC_OUTPUT_SCALE = 5.4
** preserves the legacy 6 * 9 / 10 gain. */
#include "DAC.h"
#include "Board.h"
#include "BlipBuffer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DAC_OUTPUT_SCALE  5.4f   /* matches the legacy 6 * 9 / 10 gain stage */

/* Soft 1-pole IIR low-pass applied after BlipBuffer to take the edge off
** near-Nyquist content that BlipBuffer alone preserves. */
#define DAC_ANALOG_FILTER_CUTOFF_HZ  7000.0f

struct DAC
{
    Mixer*      mixer;
    Int32       handle;
    DacMode     mode;
    Int32       enabled;

    BlipBuffer* blip[2];
    Int32       currentValue[2];   /* last value written, in sampleVolume units */
    UInt32      lastSyncTime;      /* boardSystemTime() at last sync callback */

    float       analogState[2];     /* 1-pole IIR carry */
    float       analogAlpha;        /* recomputed when mixer sample rate changes */
    UInt32      analogAlphaRate;    /* rate that analogAlpha was computed for */

    float       floatBuf[AUDIO_STEREO_BUFFER_SIZE];
    Int32       outBuf[AUDIO_STEREO_BUFFER_SIZE];
    Int32       defaultBuffer[AUDIO_STEREO_BUFFER_SIZE];
};

static Int32* dacSyncMono(void* ref, UInt32 count);
static Int32* dacSyncStereo(void* ref, UInt32 count);

/* y[n] = y[n-1] + alpha * (x[n] - y[n-1])
** alpha = 1 - exp(-2*pi*fc/fs) for cutoff fc at sample rate fs. */
static void ensureAnalogAlpha(DAC* dac)
{
    UInt32 rate = mixerGetSampleRate(dac->mixer);
    if (rate == 0) rate = AUDIO_SAMPLERATE;
    if (rate == dac->analogAlphaRate) return;
    dac->analogAlphaRate = rate;
    dac->analogAlpha = 1.0f -
        (float)exp(-2.0 * 3.14159265358979323846 *
                   (double)DAC_ANALOG_FILTER_CUTOFF_HZ / (double)rate);
}

void dacReset(DAC* dac) {
    dac->currentValue[0] = 0;
    dac->currentValue[1] = 0;
    dac->lastSyncTime    = boardSystemTime();
    dac->analogState[0]  = 0.0f;
    dac->analogState[1]  = 0.0f;
    dac->analogAlphaRate = 0;        /* force re-compute on first sync */
    /* BlipBuffer state intentionally not cleared: any in-flight events
    ** decay naturally via BASS_FACTOR, so resetting wouldn't free anything
    ** and would risk losing audio mid-playback. */
}

DAC* dacCreate(Mixer* mixer, DacMode mode)
{
    DAC* dac = (DAC*)calloc(1, sizeof(DAC));
    if (dac == NULL) return NULL;

    dac->mixer = mixer;
    dac->mode  = mode;

    dac->blip[0] = blipBufferCreate();
    if (mode == DAC_STEREO) {
        dac->blip[1] = blipBufferCreate();
    }

    dacReset(dac);

    if (mode == DAC_MONO) {
        dac->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_PCM, 0, dacSyncMono, NULL, dac);
    }
    else {
        dac->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_PCM, 1, dacSyncStereo, NULL, dac);
    }
    return dac;
}

void dacDestroy(DAC* dac)
{
    mixerUnregisterChannel(dac->mixer, dac->handle);
    if (dac->blip[0]) blipBufferDestroy(dac->blip[0]);
    if (dac->blip[1]) blipBufferDestroy(dac->blip[1]);
    free(dac);
}

/* Internal common path: enqueue a BlipBuffer delta for `value` at board
** cycle `time`. dacWrite passes time=boardSystemTime(); dacWriteAt passes
** an explicit timer cycle. */
static void dacWriteAtInternal(DAC* dac, DacChannel channel, UInt8 value, UInt32 time)
{
    Int32  newValue;
    Int32  delta;
    Int32  elapsedSigned;
    UInt32 elapsed;
    UInt32 rate;
    UInt64 num;
    unsigned timeFp;

    if (channel != DAC_CH_LEFT && channel != DAC_CH_RIGHT) return;
    if (dac->blip[channel] == NULL) return;

    newValue = ((Int32)value - 0x80) * 256;
    delta    = newValue - dac->currentValue[channel];
    if (delta == 0) return;

    /* Clamp negative offsets to zero -- another callback in the same
    ** dispatch may have advanced lastSyncTime past `time`. */
    elapsedSigned = (Int32)(time - dac->lastSyncTime);
    elapsed       = elapsedSigned < 0 ? 0u : (UInt32)elapsedSigned;
    rate          = mixerGetSampleRate(dac->mixer);

    /* time_fp = elapsed * rate * BLIP_PHASE_UNIT / boardFreq, with the
    ** integer part being the sample index inside the next sync window and
    ** the BLIP_PHASE_BITS low bits being the sub-sample phase. */
    num    = (UInt64)elapsed * (UInt64)rate * (UInt64)BLIP_PHASE_UNIT;
    timeFp = (unsigned)(num / (UInt64)boardFrequency());

    blipBufferAddDelta(dac->blip[channel], timeFp, (float)delta);

    dac->currentValue[channel] = newValue;
    dac->enabled = 1;
}

void dacWrite(DAC* dac, DacChannel channel, UInt8 value)
{
    dacWriteAtInternal(dac, channel, value, boardSystemTime());
}

void dacWriteAt(DAC* dac, DacChannel channel, UInt8 value, UInt32 time)
{
    dacWriteAtInternal(dac, channel, value, time);
}

static Int32* dacSyncMono(void* ref, UInt32 count)
{
    DAC* dac = (DAC*)ref;
    int  active;
    UInt32 i;

    if (!dac->enabled || count == 0 || count > AUDIO_MONO_BUFFER_SIZE) {
        dac->lastSyncTime = boardSystemTime();
        return dac->defaultBuffer;
    }

    active = blipBufferReadSamples(dac->blip[0], dac->floatBuf, (int)count, 1);
    if (!active) {
        dac->enabled = 0;
        dac->lastSyncTime = boardSystemTime();
        return dac->defaultBuffer;
    }

    ensureAnalogAlpha(dac);
    {
        float y = dac->analogState[0];
        float a = dac->analogAlpha;
        for (i = 0; i < count; i++) {
            y += a * (dac->floatBuf[i] - y);
            dac->outBuf[i] = (Int32)(y * DAC_OUTPUT_SCALE);
        }
        dac->analogState[0] = y;
    }
    dac->lastSyncTime = boardSystemTime();
    return dac->outBuf;
}

static Int32* dacSyncStereo(void* ref, UInt32 count)
{
    DAC* dac = (DAC*)ref;
    int  activeL;
    int  activeR;
    UInt32 i;

    if (!dac->enabled || count == 0 || count > AUDIO_MONO_BUFFER_SIZE) {
        dac->lastSyncTime = boardSystemTime();
        return dac->defaultBuffer;
    }

    /* Read into the same buffer with pitch=2 so left and right are
    ** interleaved.  Each readSamples advances its BlipBuffer's offset
    ** independently. */
    activeL = blipBufferReadSamples(dac->blip[0], dac->floatBuf,     (int)count, 2);
    activeR = blipBufferReadSamples(dac->blip[1], dac->floatBuf + 1, (int)count, 2);
    if (!activeL && !activeR) {
        dac->enabled = 0;
        dac->lastSyncTime = boardSystemTime();
        return dac->defaultBuffer;
    }
    /* If one channel is silent and the other isn't, the silent channel's
    ** float positions are left untouched (readSamples returns 0 without
    ** writing); zero them explicitly to avoid leaking stale buffer data. */
    if (!activeL) {
        for (i = 0; i < count; i++) dac->floatBuf[2 * i] = 0.0f;
    }
    if (!activeR) {
        for (i = 0; i < count; i++) dac->floatBuf[2 * i + 1] = 0.0f;
    }

    ensureAnalogAlpha(dac);
    {
        float yL = dac->analogState[0];
        float yR = dac->analogState[1];
        float a  = dac->analogAlpha;
        for (i = 0; i < count; i++) {
            yL += a * (dac->floatBuf[2 * i]     - yL);
            yR += a * (dac->floatBuf[2 * i + 1] - yR);
            dac->outBuf[2 * i]     = (Int32)(yL * DAC_OUTPUT_SCALE);
            dac->outBuf[2 * i + 1] = (Int32)(yR * DAC_OUTPUT_SCALE);
        }
        dac->analogState[0] = yL;
        dac->analogState[1] = yR;
    }
    dac->lastSyncTime = boardSystemTime();
    return dac->outBuf;
}
