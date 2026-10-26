/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/AudioMixer.c,v $
**
** $Revision: 1.18 $
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
#include "AudioMixer.h"
#include "Board.h"
#include "ArchTimer.h"
#include "ArchMidi.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* After stdio.h: pkg_fopen overrides fopen for UTF-8 paths. */
#include "PacketFileSystem.h"

#define BITSPERSAMPLE     16

#define str2ul(s) ((UInt32)s[0]<<0|(UInt32)s[1]<<8|(UInt32)s[2]<<16|(UInt32)s[3]<<24)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define METER_BLOCK     128     /* frames per meter block (~3 ms) */
#define METER_RING      512     /* blocks in flight, a power of two (~1.5 s) */
#define METER_HEARD     64      /* blocks the UI keeps (~190 ms) */
#define METER_WINDOW    2048    /* RMS window, centred on "now" (~46 ms) */
#define METER_HOLD_MS   200     /* a taken block stops counting after this; */
                                /* longer than the main window's 100 ms poll */
#define METER_MIDI_MS   100     /* MIDI has no samples: light it per note-on */
#define METER_FLOOR     0.001   /* -60 dBFS and below reads as silence */
#define METER_CURVE     0.45    /* scale curve: amplitude^0.45 */

/* The meter ring is written by the emulator thread and read by the UI;
** x86/x64 keep stores and loads in order, so only the compiler needs
** fencing there. */
#if defined(_MSC_VER)
#include <intrin.h>
#define METER_BARRIER() _ReadWriteBarrier()
#elif defined(__GNUC__)
#define METER_BARRIER() __sync_synchronize()
#else
#define METER_BARRIER()
#endif


static int mixerCPUFrequency;
static int mixerConnector;
static int mixerCPUFrequencyFixed;

void mixerSetBoardFrequency(int CPUFrequency)
{
    if (mixerCPUFrequencyFixed != 0) {
    	mixerCPUFrequency = mixerCPUFrequencyFixed;
    }
    else {
    	mixerCPUFrequency = CPUFrequency;
    }
}

void mixerSetBoardFrequencyFixed(int CPUFrequency)
{
	mixerCPUFrequencyFixed = CPUFrequency;
    mixerCPUFrequency = mixerCPUFrequencyFixed;
}

typedef struct {
    UInt32 riff;
    UInt32 fileSize;
    UInt32 wave;
    struct {
        UInt32 fmt;
        UInt32 chunkSize;
        UInt16 formatType;
        UInt16 channels;
        UInt32 samplesPerSec;
        UInt32 avgBytesPerSec;
        UInt16 blockAlign;
        UInt16 bitsPerSample;
    } wavHeader;
    UInt32 data;
    UInt32 dataSize;
} WavHeader;

typedef struct {
    Int32 volume;
    Int32 pan;
    Int32 enable;
} AudioTypeInfo;

typedef struct {
    Int32 handle;
    MixerUpdateCallback updateCallback;
    MixerSetSampleRateCallback rateCallback;
    void* ref;
    MixerAudioType type;
    // User config
    Int32 volume;
    Int32 pan;
    Int32 enable;
    Int32 stereo;
    // Internal config
    Int32 volumeLeft;
    Int32 volumeRight;
    // The same before the master mute, so the meters still move while muted
    Int32 unmutedLeft;
    Int32 unmutedRight;
    // Level meter: gain without the master volume, raw energy in this block
    Int32 meterGainLeft;
    Int32 meterGainRight;
    UInt64 meterEnergy[2];
    Int64  meterSum[2];     /* to take out a constant offset */
    DoubleT meterDc[2];     /* that offset, followed slowly */
    Int32  meterDcSet[2];   /* blocks seen: the first may be partial */
    UInt32 active;
} MixerChannel;

/* One meter block as the emulator thread produced it: mean square in
** output units (32767 = 0 dBFS). Channel levels leave out the master. */
typedef struct {
    UInt32 endFrame;
    float  master[2];
    float  type[MIXER_CHANNEL_TYPE_COUNT][2];
} MeterBlock;

/* A block the UI has taken from the ring, and when it did. */
typedef struct {
    MeterBlock block;
    UInt32     takenAt;
} MeterHeard;

struct Mixer
{ 
    MixerWriteCallback writeCallback;
    void*  writeRef;
    /* Optional read-only tap that sees the same fragment buffer the audio
    ** driver receives, invoked right after writeCallback. Used by the live
    ** recorder so it can encode the master mix without displacing the
    ** playback driver. Null when no tap is registered. */
    MixerWriteCallback tapCallback;
    void*  tapRef;
    Int32  fragmentSize;
    UInt32 refTime;
    UInt32 refFrag;
    UInt32 index;
    UInt32 volIndex;
    Int16   buffer[AUDIO_STEREO_BUFFER_SIZE];
    AudioTypeInfo audioTypeInfo[MIXER_CHANNEL_TYPE_COUNT];
    MixerChannel channels[MAX_CHANNELS];
    Int32   channelCount;
    Int32   handleCount;
    Int32   logging;
    Int32   stereo;
    UInt32  rate;
    DoubleT  masterVolume;
    Int32   masterEnable;
    FILE*   file;
    int     enable;

    /* Level meter, emulator thread side */
    UInt32  meterFrames;
    UInt32  meterBlockFill;
    UInt64  meterEnergy[2];
    Int64   meterSum[2];
    DoubleT meterDc[2];
    Int32   meterDcSet[2];
    MeterBlock meterRing[METER_RING];
    volatile UInt32 meterSeq;
    volatile UInt32 meterDelivered;

    /* Level meter, UI thread side */
    MixerDelayCallback delayCallback;
    void*   delayRef;
    UInt32  meterReadSeq;
    UInt32  meterTick;
    UInt32  meterHeardFrame;
    MeterHeard meterHeard[METER_HEARD];
    UInt32  meterHeardPos;
    UInt32  meterMidiAt;
    int     meterMidiLit;
};


static void recalculateChannelVolume(Mixer* mixer, MixerChannel* channel);


///////////////////////////////////////////////////////

static void mixerRecalculateType(Mixer* mixer, int audioType) 
{
    AudioTypeInfo* type    = mixer->audioTypeInfo + audioType;
    int i;

    if (audioType == MIXER_CHANNEL_MIDI) {
        /* MIDI goes to an external synth and has no channel of its own. */
        MixerChannel channel;
        memset(&channel, 0, sizeof(channel));
        channel.enable = type->enable;
        channel.volume = type->volume;
        channel.pan    = type->pan;
        recalculateChannelVolume(mixer, &channel);
        archMidiUpdateVolume(channel.volumeLeft, channel.volumeRight);
    }

    for (i = 0; i < mixer->channelCount; i++) {
        MixerChannel* channel = mixer->channels + i;
        if (channel->type == audioType) {
            channel->enable         = type->enable;
            channel->volume         = type->volume;
            channel->pan            = type->pan;
            recalculateChannelVolume(mixer, channel);
        }
    }
}

Int16 *mixerGetBuffer(Mixer* mixer, UInt32 *samplesOut)
{
	*samplesOut = mixer->index;
	mixer->index = 0;
	return mixer->buffer;
}

void mixerSetStereo(Mixer* mixer, Int32 stereo)
{
    int i;

    /* Output is always 2ch; toggle just retunes per-channel volume. */
    mixer->stereo = stereo;

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerRecalculateType(mixer, i);
    }
}

void mixerSetMasterVolume(Mixer* mixer, Int32 volume)
{
    int i;

    mixer->masterVolume = pow(10.0, (volume - 100) / 60.0) - pow(10.0, -100 / 60.0);
    
    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerRecalculateType(mixer, i);
    }
}

void mixerEnableMaster(Mixer* mixer, Int32 enable)
{
    int i;

    mixer->masterEnable = enable ? 1 : 0;

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerRecalculateType(mixer, i);
    }
}

void mixerSetChannelTypeVolume(Mixer* mixer, Int32 type, Int32 volume)
{
    mixer->audioTypeInfo[type].volume = volume;
    mixerRecalculateType(mixer, type);
}

void mixerSetChannelTypePan(Mixer* mixer, Int32 type, Int32 pan)
{
    mixer->audioTypeInfo[type].pan = pan;
    mixerRecalculateType(mixer, type);
}

void mixerEnableChannelType(Mixer* mixer, Int32 type, Int32 enable) 
{
    mixer->audioTypeInfo[type].enable = enable;
    mixerRecalculateType(mixer, type);
}

Int32 mixerIsChannelTypeActive(Mixer* mixer, Int32 type, Int32 reset)
{
    int i;
    Int32 active = 0;

    for (i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].type == type) {
            if (mixer->channels[i].active) {
                active = 1;
            }
            if (reset) {
                mixer->channels[i].active = 0;
            }
        }
    }

    return active;
}

///////////////////////////////////////////////////////

static void recalculateChannelVolume(Mixer* mixer, MixerChannel* channel)
{
    DoubleT volume        = pow(10.0, (channel->volume - 100) / 60.0) - pow(10.0, -100 / 60.0);
    DoubleT panLeft       = pow(10.0, (MIN(100 - channel->pan, 50) - 50) / 30.0) - pow(10.0, -50 / 30.0);
    DoubleT panRight      = pow(10.0, (MIN(channel->pan, 50) - 50) / 30.0) - pow(10.0, -50 / 30.0);

    channel->unmutedLeft    = channel->enable * (Int32)(1024 * mixer->masterVolume * volume * panLeft);
    channel->unmutedRight   = channel->enable * (Int32)(1024 * mixer->masterVolume * volume * panRight);
    channel->meterGainLeft  = channel->enable * (Int32)(1024 * volume * panLeft);
    channel->meterGainRight = channel->enable * (Int32)(1024 * volume * panRight);

    if (!mixer->stereo) {
        Int32 tmp = (channel->unmutedLeft + channel->unmutedRight) / 2;
        channel->unmutedLeft  = tmp;
        channel->unmutedRight = tmp;
        tmp = (channel->meterGainLeft + channel->meterGainRight) / 2;
        channel->meterGainLeft  = tmp;
        channel->meterGainRight = tmp;
    }
    channel->volumeLeft  = mixer->masterEnable * channel->unmutedLeft;
    channel->volumeRight = mixer->masterEnable * channel->unmutedRight;
}

///////////////////////////////////////////////////////
// Level meter

/* Mean square of a block around the signal's slowly followed average: a
** chip whose silence sits at a constant offset (the Nuked OPLL's DAC does)
** reads as silent, as it sounds. ~0.2 s keeps bass notes whole. */
static DoubleT acPower(UInt64 energy, Int64 sum, DoubleT* dc, Int32* seen)
{
    DoubleT mean = (DoubleT)sum / METER_BLOCK;
    DoubleT power;

    /* A chip registered mid-block has a partial first block, which would
    ** understate the offset; the first whole block sets it. */
    if (*seen < 2) {
        *dc = mean;
        if (++*seen == 1) {
            return 0;
        }
    }
    else {
        *dc += (mean - *dc) / 64;
    }
    power = (DoubleT)energy / METER_BLOCK - 2 * *dc * mean + *dc * *dc;
    return power > 0 ? power : 0;
}

/* Emulator thread: close the current block and publish it. */
static void meterPushBlock(Mixer* mixer)
{
    UInt32 seq = mixer->meterSeq;
    MeterBlock* block = mixer->meterRing + (seq & (METER_RING - 1));
    int i;

    memset(block->type, 0, sizeof(block->type));
    /* No chip runs while the turboR PCM mutes the mixer: show silence and
    ** take the offsets afresh once it is back, dropping the block across. */
    if (!mixer->enable) {
        for (i = 0; i < mixer->channelCount; i++) {
            MixerChannel* channel = mixer->channels + i;
            channel->meterDcSet[0]  = 0;
            channel->meterDcSet[1]  = 0;
            channel->meterEnergy[0] = 0;
            channel->meterEnergy[1] = 0;
            channel->meterSum[0]    = 0;
            channel->meterSum[1]    = 0;
        }
        mixer->meterDcSet[0] = 0;
        mixer->meterDcSet[1] = 0;
    }
    for (i = 0; mixer->enable && i < mixer->channelCount; i++) {
        MixerChannel* channel = mixer->channels + i;
        DoubleT powerLeft  = acPower(channel->meterEnergy[0], channel->meterSum[0], channel->meterDc + 0, channel->meterDcSet + 0);
        DoubleT powerRight = channel->stereo ?
                             acPower(channel->meterEnergy[1], channel->meterSum[1], channel->meterDc + 1, channel->meterDcSet + 1) :
                             powerLeft;
        DoubleT gainLeft  = channel->meterGainLeft  / 4096.0;
        DoubleT gainRight = channel->meterGainRight / 4096.0;

        /* Chips of one type add up like the mixer adds them: in power. */
        block->type[channel->type][0] += (float)(powerLeft * gainLeft * gainLeft);
        block->type[channel->type][1] += (float)(powerRight * gainRight * gainRight);
        channel->meterEnergy[0] = 0;
        channel->meterEnergy[1] = 0;
        channel->meterSum[0]    = 0;
        channel->meterSum[1]    = 0;
    }
    block->master[0] = !mixer->enable ? 0 :
                       (float)acPower(mixer->meterEnergy[0], mixer->meterSum[0], mixer->meterDc + 0, mixer->meterDcSet + 0);
    block->master[1] = !mixer->enable ? 0 :
                       (float)acPower(mixer->meterEnergy[1], mixer->meterSum[1], mixer->meterDc + 1, mixer->meterDcSet + 1);
    block->endFrame  = mixer->meterFrames;
    mixer->meterEnergy[0] = 0;
    mixer->meterEnergy[1] = 0;
    mixer->meterSum[0]    = 0;
    mixer->meterSum[1]    = 0;
    mixer->meterBlockFill = 0;

    METER_BARRIER();
    mixer->meterSeq = seq + 1;
}

/* Emulator thread: count one mixed frame. The delivered count moves only
** after the driver has queued a fragment, so the UI can err only late. */
static void meterCountFrame(Mixer* mixer)
{
    mixer->meterFrames++;
    if (++mixer->meterBlockFill == METER_BLOCK) {
        meterPushBlock(mixer);
    }
}

/* UI thread: take the blocks up to half a window past the frame playing
** now, so the RMS window sits centred on it and adds no lag. */
static void meterAdvance(Mixer* mixer)
{
    UInt32 now = archGetSystemUpTime(1000);
    UInt32 delivered;
    UInt32 delay;
    UInt32 until;
    UInt32 seq;

    if (now == mixer->meterTick) {
        return;
    }
    mixer->meterTick = now;

    /* Delivered first: a fragment queued between the two reads then shows
    ** in the delay only, which errs late rather than early. */
    delivered = mixer->meterDelivered;
    METER_BARRIER();
    delay = mixer->delayCallback != NULL ? mixer->delayCallback(mixer->delayRef) : 0;
    mixer->meterHeardFrame = delivered - delay;
    until = mixer->meterHeardFrame + METER_WINDOW / 2;

    seq = mixer->meterSeq;
    METER_BARRIER();
    if (seq - mixer->meterReadSeq > METER_RING / 2) {
        mixer->meterReadSeq = seq - METER_RING / 2;
    }

    while (mixer->meterReadSeq != seq) {
        UInt32 s = mixer->meterReadSeq;
        MeterBlock block = mixer->meterRing[s & (METER_RING - 1)];

        METER_BARRIER();
        if (mixer->meterSeq - s >= METER_RING) {
            /* Overwritten while copying: the writer lapped us. */
            mixer->meterReadSeq = mixer->meterSeq - METER_RING / 2;
            seq = mixer->meterSeq;
            continue;
        }
        if ((Int32)(block.endFrame - until) > 0) {
            break;
        }
        mixer->meterHeard[mixer->meterHeardPos & (METER_HEARD - 1)].block   = block;
        mixer->meterHeard[mixer->meterHeardPos & (METER_HEARD - 1)].takenAt = now;
        mixer->meterHeardPos++;
        mixer->meterReadSeq = s + 1;
    }

    if (archMidiGetNoteOn()) {
        mixer->meterMidiAt  = now;
        mixer->meterMidiLit = 1;
    }
}

/* RMS over the window centred on the frame playing now, in output units.
** Blocks stop counting a while after they were taken, so a paused or
** stopped emulator reads as silence. type < 0 asks for the master. */
static DoubleT meterLevel(Mixer* mixer, Int32 type, int side)
{
    UInt32 from = mixer->meterHeardFrame - METER_WINDOW / 2;
    DoubleT energy = 0;
    int count = 0;
    int i;

    for (i = 0; i < METER_HEARD; i++) {
        MeterHeard* heard = mixer->meterHeard + i;

        if (mixer->meterTick - heard->takenAt >= METER_HOLD_MS ||
            (Int32)(heard->block.endFrame - from) <= 0) {
            continue;
        }
        energy += type < 0 ? heard->block.master[side] : heard->block.type[type][side];
        count++;
    }
    return count > 0 ? sqrt(energy / count) : 0;
}

/* Map an RMS level to the 0..100 a theme meter takes. A power curve keeps
** 0 dBFS at the top; 0.45 was set by ear so -12..-20 dBFS sits mid-scale. */
static Int32 meterPercent(DoubleT level)
{
    DoubleT amplitude = level / 32767.0;
    Int32 percent;

    if (amplitude <= METER_FLOOR) {
        return 0;
    }
    percent = (Int32)ceil(100.0 * pow(amplitude, METER_CURVE));
    return MAX(1, MIN(100, percent));
}

/* MIDI goes to an external synth, so light it from its own fader. */
static DoubleT meterMidiLevel(Mixer* mixer, int side)
{
    AudioTypeInfo* type = mixer->audioTypeInfo + MIXER_CHANNEL_MIDI;
    MixerChannel channel;

    if (!mixer->meterMidiLit || mixer->meterTick - mixer->meterMidiAt >= METER_MIDI_MS) {
        mixer->meterMidiLit = 0;
        return 0;
    }
    memset(&channel, 0, sizeof(channel));
    channel.enable = type->enable;
    channel.volume = type->volume;
    channel.pan    = type->pan;
    recalculateChannelVolume(mixer, &channel);
    return 32767.0 * (side ? channel.meterGainRight : channel.meterGainLeft) / 1024.0;
}

void mixerSetDelayCallback(Mixer* mixer, MixerDelayCallback callback, void* ref)
{
    mixer->delayCallback = callback;
    mixer->delayRef      = ref;
}

Int32 mixerMeterGetTypePercent(Mixer* mixer, Int32 type, int leftRight)
{
    meterAdvance(mixer);
    if (type == MIXER_CHANNEL_MIDI) {
        return meterPercent(meterMidiLevel(mixer, leftRight ? 1 : 0));
    }
    return meterPercent(meterLevel(mixer, type, leftRight ? 1 : 0));
}

Int32 mixerMeterGetMasterPercent(Mixer* mixer, int leftRight)
{
    meterAdvance(mixer);
    return meterPercent(meterLevel(mixer, -1, leftRight ? 1 : 0));
}

static Mixer* globalMixer = NULL;

Mixer* mixerGetGlobalMixer()
{
    return globalMixer;
}

Mixer* mixerCreate()
{
    Mixer* mixer = (Mixer*)calloc(1, sizeof(Mixer));

    mixer->fragmentSize = 512;
    mixer->enable = 1;
    mixer->rate = AUDIO_SAMPLERATE;

    if (globalMixer == NULL) globalMixer = mixer;

    return mixer;
}

void mixerDestroy(Mixer* mixer)
{
    mixerStopLog(mixer);
    globalMixer = NULL;
    free(mixer);
}

UInt32 mixerGetSampleRate(Mixer* mixer)
{
    return mixer->rate;
}

void mixerSetSampleRate(Mixer* mixer, UInt32 rate)
{
    int i;
    mixer->rate = rate;
    for(i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].rateCallback != NULL) {
            mixer->channels[i].rateCallback(mixer->channels[i].ref, rate);
        }
    }
}

void mixerSetWriteCallback(Mixer* mixer, MixerWriteCallback callback, void* ref, int fragmentSize)
{
    mixer->fragmentSize = fragmentSize;
    mixer->writeCallback = callback;
    mixer->writeRef = ref;

    if (mixer->fragmentSize <= 0) {
        mixer->fragmentSize = 512;
    }
}

void mixerSetTapCallback(Mixer* mixer, MixerWriteCallback callback, void* ref)
{
    mixer->tapCallback = callback;
    mixer->tapRef = ref;
}

Int32 mixerRegisterChannel(Mixer* mixer, Int32 audioType, Int32 stereo, MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback, void* ref)
{
    MixerChannel*  channel = mixer->channels + mixer->channelCount;
    AudioTypeInfo* type    = mixer->audioTypeInfo + audioType;

    if (mixer->channelCount == MAX_CHANNELS - 1) {
        return 0;
    }

    mixer->channelCount++;

    channel->updateCallback = callback;
    channel->rateCallback   = rateCallback;
    channel->ref            = ref;
    channel->type           = audioType;
    channel->stereo         = stereo;
    channel->enable         = type->enable;
    channel->volume         = type->volume;
    channel->pan            = type->pan;
    channel->handle         = ++mixer->handleCount;
    channel->meterEnergy[0] = 0;
    channel->meterEnergy[1] = 0;
    channel->meterSum[0]    = 0;
    channel->meterSum[1]    = 0;
    channel->meterDcSet[0]  = 0;
    channel->meterDcSet[1]  = 0;

    recalculateChannelVolume(mixer, channel);

    return channel->handle;
}

void mixerUnregisterChannel(Mixer* mixer, Int32 handle) 
{
    int i;

    if (mixer->channelCount == 0) {
        return;
    }

    for (i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].handle == handle) {
            break;
        }
    }

    if (i == mixer->channelCount) {
        return;
    }

    mixer->channelCount--;
    while (i < mixer->channelCount) {
        mixer->channels[i] = mixer->channels[i + 1];
        i++;
    }
}

void mixerReset(Mixer* mixer)
{
    mixer->refTime = boardSystemTime();
    mixer->index = 0;
}

void mixerSync(Mixer* mixer)
{
    UInt32 systemTime = boardSystemTime();
    Int16* buffer   = mixer->buffer;
    Int32* chBuff[MAX_CHANNELS];
    UInt32 count;
    UInt64 elapsed;
    int i;

    elapsed        = mixer->rate * (UInt64)(systemTime - mixer->refTime) + mixer->refFrag;
    mixer->refTime = systemTime;
    mixer->refFrag = (UInt32)(elapsed % (mixerCPUFrequency * (boardFrequency() / 3579545)));
    count          = (UInt32)(elapsed / (mixerCPUFrequency * (boardFrequency() / 3579545)));

    if (count == 0 || count > AUDIO_MONO_BUFFER_SIZE) {
        return;
    }

    /* Output is always interleaved stereo; mono is dual-mono via
    ** recalculateChannelVolume, so no driver tear-down on toggle. */
    if (!mixer->enable) {
        while (count--) {
            buffer[mixer->index++] = 0;
            buffer[mixer->index++] = 0;

            meterCountFrame(mixer);
            if (mixer->index == mixer->fragmentSize) {
                if (mixer->writeCallback != NULL) {
                    mixer->writeCallback(mixer->writeRef, buffer, mixer->fragmentSize);
                }
                mixer->meterDelivered = mixer->meterFrames;
                if (mixer->tapCallback != NULL) {
                    mixer->tapCallback(mixer->tapRef, buffer, mixer->fragmentSize);
                }
                if (mixer->logging) {
                    fwrite(buffer, 2 * mixer->fragmentSize, 1, mixer->file);
                }
                mixer->index = 0;
            }
        }
        return;
    }
    
    for (i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].updateCallback != NULL) {
            chBuff[i] = mixer->channels[i].updateCallback(mixer->channels[i].ref, count);
        }
        else {
            chBuff[i] = NULL;
        }
    }

    while (count--) {
        Int32 left = 0;
        Int32 right = 0;

        for (i = 0; i < mixer->channelCount; i++) {
            Int32 chanLeft;
            Int32 chanRight;

            if (chBuff[i] == NULL) {
                continue;
            }

            if (mixer->channels[i].stereo) {
                Int32 rawLeft  = *chBuff[i]++;
                Int32 rawRight = *chBuff[i]++;
                chanLeft = mixer->channels[i].unmutedLeft * rawLeft;
                chanRight = mixer->channels[i].unmutedRight * rawRight;
                mixer->channels[i].meterEnergy[0] += (UInt64)((Int64)rawLeft  * rawLeft);
                mixer->channels[i].meterEnergy[1] += (UInt64)((Int64)rawRight * rawRight);
                mixer->channels[i].meterSum[0]    += rawLeft;
                mixer->channels[i].meterSum[1]    += rawRight;
            }
            else {
                Int32 tmp = *chBuff[i]++;
                chanLeft = mixer->channels[i].unmutedLeft * tmp;
                chanRight = mixer->channels[i].unmutedRight * tmp;
                mixer->channels[i].meterEnergy[0] += (UInt64)((Int64)tmp * tmp);
                mixer->channels[i].meterSum[0]    += tmp;
            }

            left  += chanLeft;
            right += chanRight;
        }

        left  /= 4096;
        right /= 4096;

        mixer->meterEnergy[0] += (UInt64)((Int64)left  * left);
        mixer->meterEnergy[1] += (UInt64)((Int64)right * right);
        mixer->meterSum[0]    += left;
        mixer->meterSum[1]    += right;

        if (!mixer->masterEnable) {
            left  = 0;
            right = 0;
        }
        if (left  >  32767) { left  = 32767; }
        if (left  < -32767) { left  = -32767; }
        if (right >  32767) { right = 32767; }
        if (right < -32767) { right = -32767; }

        buffer[mixer->index++] = (Int16)left;
        buffer[mixer->index++] = (Int16)right;

        meterCountFrame(mixer);
        if (mixer->index == mixer->fragmentSize) {
            if (mixer->writeCallback != NULL) {
                mixer->writeCallback(mixer->writeRef, buffer, mixer->fragmentSize);
            }
            mixer->meterDelivered = mixer->meterFrames;
            if (mixer->tapCallback != NULL) {
                mixer->tapCallback(mixer->tapRef, buffer, mixer->fragmentSize);
            }
            if (mixer->logging) {
                fwrite(buffer, 2 * mixer->fragmentSize, 1, mixer->file);
            }
            mixer->index = 0;
        }

        mixer->volIndex++;
    }

    if (mixer->volIndex >= 441) {
        for (i = 0; i < mixer->channelCount; i++) {
            if (chBuff[i] && chBuff[i][0]) {
                mixer->channels[i].active++;
            }
        }
        mixer->volIndex = 0;
    }
}

void mixerStartLog(Mixer* mixer, char* fileName) 
{
    if (mixer->logging == 1) {
        mixerStopLog(mixer);
    }
    mixer->file = fopen(fileName, "wb");
    if (mixer->file != NULL) {
        fseek(mixer->file, sizeof(WavHeader), SEEK_SET);
        mixer->logging = 1;
    }
}

int mixerIsLogging(Mixer* mixer) {
    return mixer->logging;
}

void mixerStopLog(Mixer* mixer) 
{
    WavHeader header;
    int fileSize;

    if (mixer->logging == 0) {
        return;
    }

    mixer->logging = 0;
    
    fileSize = ftell(mixer->file);
    
    header.riff                     = str2ul("RIFF");
    header.fileSize                 = fileSize - 8;
    header.wave                     = str2ul("WAVE");
    header.wavHeader.fmt            = str2ul("fmt ");
    header.wavHeader.chunkSize      = 16;
    header.wavHeader.formatType     = 1;
    /* Mix output is always interleaved stereo (dual-mono in mono mode),
    ** so the log file is always 2-channel regardless of mixer->stereo. */
    header.wavHeader.channels       = 2;
    header.wavHeader.samplesPerSec  = mixer->rate;
    header.wavHeader.avgBytesPerSec = 2 * mixer->rate * BITSPERSAMPLE / 8;
    header.wavHeader.blockAlign     = 2 * BITSPERSAMPLE / 8;
    header.wavHeader.bitsPerSample  = BITSPERSAMPLE;
    header.data                     = str2ul("data");
    header.dataSize                 = fileSize - sizeof(WavHeader);

    fseek(mixer->file, 0, SEEK_SET);
    fwrite(&header, 1, sizeof(WavHeader), mixer->file);
    fclose(mixer->file);
}

void mixerSetEnable(Mixer* mixer, int enable)
{
    mixer->enable = enable;
//    printf("AUDIO: %s\n", enable?"enabled":"disabled");
}
