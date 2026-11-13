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
#include "ArchEvent.h"
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
    // Instance
    Int32 instEnable;
    Int32 instVolume;
    Int32 instPan;
    Int32 pending;      // registered inside a device bracket not yet ended
    Int32 location;
    Int32 slot;
    Int32 sslot;
    Int32 port;
    Int32 ordinal;
    char  name[MIXER_CHANNEL_NAME_LENGTH];
    char  id[MIXER_CHANNEL_ID_LENGTH];
    // Per-instance meter for the device window. The RMS blocks above are
    // summed by type and keep no per-channel history, so the window still
    // takes this older peak-average form.
    Int32 volIntLeft;
    Int32 volIntRight;
    Int32 volCntLeft;
    Int32 volCntRight;
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
    Int32   mixBuffer[AUDIO_STEREO_BUFFER_SIZE];
    AudioTypeInfo audioTypeInfo[MIXER_CHANNEL_TYPE_COUNT];
    /* Grows as devices register, and growing frees the old array. The UI
    ** thread reads meters and changes volumes while the emulator thread
    ** mixes, so every walk of the array holds lock (see mixerLock). */
    MixerChannel* channels;
    Int32** chBuff;
    Int32   channelCapacity;
    void*   lock;
    UInt32  channelMeterTick;
    Int32   channelCount;
    UInt32  generation;
    Int32   deviceOpen;
    Int32   deviceSlot;
    Int32   deviceSslot;
    Int32   devicePort;
    UInt32  deviceSlotRegistrations;
    MixerChannelSettingsCallback settingsCallback;
    void*   settingsRef;
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

/* Public functions that touch the channel array take the lock; the static
** helpers below them do not, so a public function never calls another
** public one while holding it. The lock is never held across the audio
** driver's write callback. */
static void mixerLock(Mixer* mixer)
{
    archSemaphoreWait(mixer->lock, -1);
}

static void mixerUnlock(Mixer* mixer)
{
    archSemaphoreSignal(mixer->lock);
}


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
        /* No instance either: recalculateChannelVolume multiplies these in. */
        channel.instEnable = 1;
        channel.instVolume = 100;
        channel.instPan    = 50;
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
    mixerLock(mixer);
    mixer->stereo = stereo;

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerRecalculateType(mixer, i);
    }
    mixerUnlock(mixer);
}

void mixerSetMasterVolume(Mixer* mixer, Int32 volume)
{
    int i;

    mixerLock(mixer);
    mixer->masterVolume = pow(10.0, (volume - 100) / 60.0) - pow(10.0, -100 / 60.0);

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerRecalculateType(mixer, i);
    }
    mixerUnlock(mixer);
}

void mixerEnableMaster(Mixer* mixer, Int32 enable)
{
    int i;

    mixerLock(mixer);
    mixer->masterEnable = enable ? 1 : 0;

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++) {
        mixerRecalculateType(mixer, i);
    }
    mixerUnlock(mixer);
}

void mixerSetChannelTypeVolume(Mixer* mixer, Int32 type, Int32 volume)
{
    mixerLock(mixer);
    mixer->audioTypeInfo[type].volume = volume;
    mixerRecalculateType(mixer, type);
    mixerUnlock(mixer);
}

void mixerSetChannelTypePan(Mixer* mixer, Int32 type, Int32 pan)
{
    mixerLock(mixer);
    mixer->audioTypeInfo[type].pan = pan;
    mixerRecalculateType(mixer, type);
    mixerUnlock(mixer);
}

void mixerEnableChannelType(Mixer* mixer, Int32 type, Int32 enable)
{
    mixerLock(mixer);
    mixer->audioTypeInfo[type].enable = enable;
    mixerRecalculateType(mixer, type);
    mixerUnlock(mixer);
}

Int32 mixerIsChannelTypeActive(Mixer* mixer, Int32 type, Int32 reset)
{
    int i;
    Int32 active = 0;

    mixerLock(mixer);
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
    mixerUnlock(mixer);

    return active;
}

///////////////////////////////////////////////////////

static DoubleT volumeCurve(Int32 volume)
{
    return pow(10.0, (volume - 100) / 60.0) - pow(10.0, -100 / 60.0);
}

static void recalculateChannelVolume(Mixer* mixer, MixerChannel* channel)
{
    /* The instance factor is normalised to 1 at volume 100, so an instance
    ** left at its defaults gives bit for bit the type's own gain. */
    DoubleT volume        = volumeCurve(channel->volume) * (volumeCurve(channel->instVolume) / volumeCurve(100));
    Int32   pan           = MAX(0, MIN(100, channel->pan + channel->instPan - 50));
    Int32   enable        = channel->enable && channel->instEnable;
    DoubleT panLeft       = pow(10.0, (MIN(100 - pan, 50) - 50) / 30.0) - pow(10.0, -50 / 30.0);
    DoubleT panRight      = pow(10.0, (MIN(pan, 50) - 50) / 30.0) - pow(10.0, -50 / 30.0);

    channel->unmutedLeft    = enable * (Int32)(1024 * mixer->masterVolume * volume * panLeft);
    channel->unmutedRight   = enable * (Int32)(1024 * mixer->masterVolume * volume * panRight);
    channel->meterGainLeft  = enable * (Int32)(1024 * volume * panLeft);
    channel->meterGainRight = enable * (Int32)(1024 * volume * panRight);

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
    channel.instEnable = 1;
    channel.instVolume = 100;
    channel.instPan    = 50;
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
    mixer->lock = archSemaphoreCreate(1);

    if (globalMixer == NULL) globalMixer = mixer;

    return mixer;
}

void mixerDestroy(Mixer* mixer)
{
    mixerStopLog(mixer);
    globalMixer = NULL;
    archSemaphoreDestroy(mixer->lock);
    free(mixer->channels);
    free(mixer->chBuff);
    free(mixer);
}

UInt32 mixerGetSampleRate(Mixer* mixer)
{
    return mixer->rate;
}

void mixerSetSampleRate(Mixer* mixer, UInt32 rate)
{
    int i;
    mixerLock(mixer);
    mixer->rate = rate;
    for(i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].rateCallback != NULL) {
            mixer->channels[i].rateCallback(mixer->channels[i].ref, rate);
        }
    }
    mixerUnlock(mixer);
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

static void resolveChannel(Mixer* mixer, MixerChannel* channel, Int32 location, Int32 slot, Int32 sslot, Int32 port)
{
    char* id = channel->id;
    int i;

    channel->location = location;
    channel->slot     = location == MIXER_LOCATION_SLOT ? slot  : 0;
    channel->sslot    = location == MIXER_LOCATION_SLOT ? sslot : 0;
    channel->port     = location == MIXER_LOCATION_BOARD ? port : -1;
    channel->ordinal  = 1;

    for (i = 0; i < mixer->channelCount; i++) {
        MixerChannel* other = mixer->channels + i;
        if (other != channel && !other->pending &&
            other->type     == channel->type     &&
            other->location == channel->location &&
            other->slot     == channel->slot     &&
            other->sslot    == channel->sslot    &&
            other->port     == channel->port     &&
            strcmp(other->name, channel->name) == 0 &&
            other->ordinal  >= channel->ordinal)
        {
            channel->ordinal = other->ordinal + 1;
        }
    }

    if (location == MIXER_LOCATION_SLOT) {
        id += sprintf(id, "slot%d-%d", channel->slot, channel->sslot);
    }
    else {
        id += sprintf(id, "board");
        if (channel->port >= 0) {
            id += sprintf(id, "-%02x", channel->port & 0xff);
        }
    }
    if (channel->name[0]) {
        id += sprintf(id, ".%s", channel->name);
    }
    if (channel->ordinal > 1) {
        sprintf(id, ".n%d", channel->ordinal);
    }

    channel->pending = 0;

    if (mixer->settingsCallback != NULL) {
        mixer->settingsCallback(mixer->settingsRef, channel->type, channel->id,
                                &channel->instEnable, &channel->instVolume, &channel->instPan);
    }
    recalculateChannelVolume(mixer, channel);
}

static void endDevice(Mixer* mixer, UInt32 slotRegistrations)
{
    Int32 location;
    int i;

    if (!mixer->deviceOpen) {
        return;
    }

    location = slotRegistrations != mixer->deviceSlotRegistrations ? MIXER_LOCATION_SLOT : MIXER_LOCATION_BOARD;

    for (i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].pending) {
            resolveChannel(mixer, mixer->channels + i, location,
                           mixer->deviceSlot, mixer->deviceSslot, mixer->devicePort);
        }
    }

    mixer->deviceOpen = 0;
    mixer->generation++;
}

void mixerBeginDevice(Mixer* mixer, int slot, int sslot, UInt32 slotRegistrations)
{
    mixerLock(mixer);
    /* An open device's count belongs to its own slot, which the caller did
    ** not pass, so its channels go to the board rather than stay unlisted. */
    endDevice(mixer, mixer->deviceSlotRegistrations);
    mixer->deviceOpen              = 1;
    mixer->deviceSlot              = slot;
    mixer->deviceSslot             = sslot;
    mixer->devicePort              = -1;
    mixer->deviceSlotRegistrations = slotRegistrations;
    mixerUnlock(mixer);
}

void mixerSetDevicePort(Mixer* mixer, int port)
{
    mixerLock(mixer);
    if (mixer->deviceOpen) {
        mixer->devicePort = port;
    }
    mixerUnlock(mixer);
}

void mixerEndDevice(Mixer* mixer, UInt32 slotRegistrations)
{
    mixerLock(mixer);
    endDevice(mixer, slotRegistrations);
    mixerUnlock(mixer);
}

void mixerSetChannelSettingsCallback(Mixer* mixer, MixerChannelSettingsCallback callback, void* ref)
{
    mixerLock(mixer);
    mixer->settingsCallback = callback;
    mixer->settingsRef      = ref;
    mixerUnlock(mixer);
}

UInt32 mixerGetChannelGeneration(Mixer* mixer)
{
    return mixer->generation;
}

int mixerGetChannelInfo(Mixer* mixer, MixerChannelInfo* info, int maxCount)
{
    int count = 0;
    int i;

    mixerLock(mixer);
    for (i = 0; i < mixer->channelCount; i++) {
        MixerChannel* channel = mixer->channels + i;
        if (channel->pending) {
            continue;
        }
        if (count < maxCount) {
            MixerChannelInfo* entry = info + count;
            entry->handle   = channel->handle;
            entry->type     = channel->type;
            entry->stereo   = channel->stereo;
            entry->location = channel->location;
            entry->slot     = channel->slot;
            entry->sslot    = channel->sslot;
            entry->port     = channel->port;
            entry->ordinal  = channel->ordinal;
            entry->enable   = channel->instEnable;
            entry->volume   = channel->instVolume;
            entry->pan      = channel->instPan;
            strcpy(entry->name, channel->name);
            strcpy(entry->id, channel->id);
        }
        count++;
    }
    mixerUnlock(mixer);

    return count;
}

static MixerChannel* findChannel(Mixer* mixer, Int32 handle)
{
    int i;
    for (i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].handle == handle) {
            return mixer->channels + i;
        }
    }
    return NULL;
}

void mixerSetChannelVolume(Mixer* mixer, Int32 handle, Int32 volume)
{
    MixerChannel* channel;
    mixerLock(mixer);
    channel = findChannel(mixer, handle);
    if (channel != NULL) {
        channel->instVolume = volume;
        recalculateChannelVolume(mixer, channel);
    }
    mixerUnlock(mixer);
}

void mixerSetChannelPan(Mixer* mixer, Int32 handle, Int32 pan)
{
    MixerChannel* channel;
    mixerLock(mixer);
    channel = findChannel(mixer, handle);
    if (channel != NULL) {
        channel->instPan = pan;
        recalculateChannelVolume(mixer, channel);
    }
    mixerUnlock(mixer);
}

void mixerEnableChannel(Mixer* mixer, Int32 handle, Int32 enable)
{
    MixerChannel* channel;
    mixerLock(mixer);
    channel = findChannel(mixer, handle);
    if (channel != NULL) {
        channel->instEnable = enable ? 1 : 0;
        recalculateChannelVolume(mixer, channel);
    }
    mixerUnlock(mixer);
}

/* The per-instance peaks fall by one step per 20 ms since last read. */
static void decayChannelMeters(Mixer* mixer)
{
    int i;
    int diff = archGetSystemUpTime(50) - mixer->channelMeterTick;

    if (diff == 0) {
        return;
    }
    for (i = 0; i < mixer->channelCount; i++) {
        MixerChannel* channel = mixer->channels + i;
        channel->volIntLeft  = MAX(0, channel->volIntLeft  - diff);
        channel->volIntRight = MAX(0, channel->volIntRight - diff);
    }
    mixer->channelMeterTick += diff;
}

Int32 mixerGetChannelVolume(Mixer* mixer, Int32 handle, int leftRight)
{
    MixerChannel* channel;
    Int32 volume = 0;
    mixerLock(mixer);
    decayChannelMeters(mixer);
    channel = findChannel(mixer, handle);
    if (channel != NULL) {
        volume = leftRight ? channel->volIntRight : channel->volIntLeft;
    }
    mixerUnlock(mixer);
    return volume;
}

Int32 mixerRegisterChannel(Mixer* mixer, Int32 audioType, Int32 stereo, MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback, void* ref)
{
    return mixerRegisterChannelNamed(mixer, audioType, stereo, callback, rateCallback, ref, NULL);
}

Int32 mixerRegisterChannelNamed(Mixer* mixer, Int32 audioType, Int32 stereo, MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback, void* ref, const char* name)
{
    MixerChannel*  channel;
    AudioTypeInfo* type    = mixer->audioTypeInfo + audioType;
    Int32          handle;

    mixerLock(mixer);

    if (mixer->channelCount == mixer->channelCapacity) {
        Int32 capacity = mixer->channelCapacity ? 2 * mixer->channelCapacity : 16;
        MixerChannel* channels = (MixerChannel*)realloc(mixer->channels, capacity * sizeof(MixerChannel));
        Int32** chBuff = (Int32**)realloc(mixer->chBuff, capacity * sizeof(Int32*));
        if (channels != NULL) {
            mixer->channels = channels;
        }
        if (chBuff != NULL) {
            mixer->chBuff = chBuff;
        }
        if (channels == NULL || chBuff == NULL) {
            mixerUnlock(mixer);
            return 0;
        }
        mixer->channelCapacity = capacity;
    }

    channel = mixer->channels + mixer->channelCount;
    memset(channel, 0, sizeof(MixerChannel));
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
    channel->instEnable     = 1;
    channel->instVolume     = 100;
    channel->instPan        = 50;
    if (name != NULL) {
        strncpy(channel->name, name, MIXER_CHANNEL_NAME_LENGTH - 1);
    }

    if (mixer->deviceOpen) {
        channel->pending = 1;
        recalculateChannelVolume(mixer, channel);
    }
    else {
        resolveChannel(mixer, channel, MIXER_LOCATION_BOARD, 0, 0, -1);
    }
    mixer->generation++;

    handle = channel->handle;
    mixerUnlock(mixer);

    return handle;
}

void mixerUnregisterChannel(Mixer* mixer, Int32 handle)
{
    int i;

    mixerLock(mixer);

    for (i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].handle == handle) {
            break;
        }
    }

    if (i < mixer->channelCount) {
        mixer->channelCount--;
        while (i < mixer->channelCount) {
            mixer->channels[i] = mixer->channels[i + 1];
            i++;
        }
        mixer->generation++;
    }

    mixerUnlock(mixer);
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
    Int32** chBuff;
    Int32* mix;
    UInt32 count;
    UInt32 frames;
    UInt32 frameNo;
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
        /* Counting a frame can close a meter block, which walks the channels. */
        frameNo = mixer->meterFrames;
        frames  = count;
        mixerLock(mixer);
        while (frames--) {
            meterCountFrame(mixer);
        }
        mixerUnlock(mixer);

        while (count--) {
            buffer[mixer->index++] = 0;
            buffer[mixer->index++] = 0;

            frameNo++;
            if (mixer->index == mixer->fragmentSize) {
                if (mixer->writeCallback != NULL) {
                    mixer->writeCallback(mixer->writeRef, buffer, mixer->fragmentSize);
                }
                mixer->meterDelivered = frameNo;
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

    /* Mix under the lock into mixBuffer, then hand the result to the audio
    ** driver without it: the driver may block, and the UI thread would wait
    ** on the lock for as long. */
    mixerLock(mixer);
    chBuff  = mixer->chBuff;
    frames  = count;
    frameNo = mixer->meterFrames;
    mix     = mixer->mixBuffer;

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
            Int32 instLeft;
            Int32 instRight;

            if (chBuff[i] == NULL) {
                continue;
            }

            if (mixer->channels[i].stereo) {
                Int32 rawLeft  = *chBuff[i]++;
                Int32 rawRight = *chBuff[i]++;
                chanLeft = mixer->channels[i].unmutedLeft * rawLeft;
                chanRight = mixer->channels[i].unmutedRight * rawRight;
                instLeft  = mixer->channels[i].meterGainLeft  * rawLeft;
                instRight = mixer->channels[i].meterGainRight * rawRight;
                mixer->channels[i].meterEnergy[0] += (UInt64)((Int64)rawLeft  * rawLeft);
                mixer->channels[i].meterEnergy[1] += (UInt64)((Int64)rawRight * rawRight);
                mixer->channels[i].meterSum[0]    += rawLeft;
                mixer->channels[i].meterSum[1]    += rawRight;
            }
            else {
                Int32 tmp = *chBuff[i]++;
                chanLeft = mixer->channels[i].unmutedLeft * tmp;
                chanRight = mixer->channels[i].unmutedRight * tmp;
                instLeft  = mixer->channels[i].meterGainLeft  * tmp;
                instRight = mixer->channels[i].meterGainRight * tmp;
                mixer->channels[i].meterEnergy[0] += (UInt64)((Int64)tmp * tmp);
                mixer->channels[i].meterSum[0]    += tmp;
            }

            mixer->channels[i].volCntLeft  += (instLeft  > 0 ? instLeft  : -instLeft)  / 2048;
            mixer->channels[i].volCntRight += (instRight > 0 ? instRight : -instRight) / 2048;

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

        *mix++ = left;
        *mix++ = right;

        meterCountFrame(mixer);
        mixer->volIndex++;
    }

    if (mixer->volIndex >= 441) {
        for (i = 0; i < mixer->channelCount; i++) {
            MixerChannel* channel = mixer->channels + i;
            Int32 newVolumeLeft  = MIN(100, channel->volCntLeft  / (Int32)mixer->volIndex / 328);
            Int32 newVolumeRight = MIN(100, channel->volCntRight / (Int32)mixer->volIndex / 328);

            channel->volIntLeft  = MAX(channel->volIntLeft,  newVolumeLeft);
            channel->volIntRight = MAX(channel->volIntRight, newVolumeRight);
            channel->volCntLeft  = 0;
            channel->volCntRight = 0;

            if (chBuff[i] && chBuff[i][0]) {
                mixer->channels[i].active++;
            }
        }
        mixer->volIndex = 0;
    }

    mixerUnlock(mixer);

    mix = mixer->mixBuffer;
    while (frames--) {
        buffer[mixer->index++] = (Int16)*mix++;
        buffer[mixer->index++] = (Int16)*mix++;

        frameNo++;
        if (mixer->index == mixer->fragmentSize) {
            if (mixer->writeCallback != NULL) {
                mixer->writeCallback(mixer->writeRef, buffer, mixer->fragmentSize);
            }
            /* meterFrames already counts the whole batch mixed above; only
            ** the frames handed over so far are delivered. */
            mixer->meterDelivered = frameNo;
            if (mixer->tapCallback != NULL) {
                mixer->tapCallback(mixer->tapRef, buffer, mixer->fragmentSize);
            }
            if (mixer->logging) {
                fwrite(buffer, 2 * mixer->fragmentSize, 1, mixer->file);
            }
            mixer->index = 0;
        }
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
