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

/* The instance list can change on two threads at once (a printer type
** change runs on the UI thread unsuspended), so bump its generation
** atomically and never leave it stuck odd. */
#if defined(_MSC_VER)
#define LAYOUT_BUMP(gen) _InterlockedIncrement((volatile long*)&(gen))
#elif defined(__GNUC__)
#define LAYOUT_BUMP(gen) __sync_fetch_and_add(&(gen), 1)
#else
#define LAYOUT_BUMP(gen) ((gen)++)
#endif
#define LAYOUT_TRIES 1000


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
    // Where the chip sits, for the detailed mixer; index tells apart
    // chips of one type at one slot and stays fixed once registered
    Int32 slot;
    Int32 sslot;
    Int32 index;
    Int32 voices;       // slot in the voice pool, -1 without voices
    char  name[16];
    Int32 device;
} MixerChannel;

/* Detailed mixer settings of one chip instance. They live apart from the
** channels, which get compacted and are rebuilt on every machine start. */
typedef struct {
    Int32   used;
    Int32   type;
    Int32   slot;
    Int32   sslot;
    Int32   index;
    DoubleT gain;       /* 1.0 leaves the chip as the type mixer sets it */
    Int32   mute;
    Int32   solo;
    DoubleT voiceGain[MIXER_MAX_VOICES];
    UInt64  voiceMute;  /* one bit per voice */
    UInt64  voiceSolo;
    /* The voice table the voice settings were made for; NULL if not known */
    Int32   voiceCount;
    const char* const* voiceNames;
} InstanceSetting;

#define INSTANCE_SETTINGS 32

/* Per-voice levels of every chip over at least VOICE_FRAME mixed frames,
** as mean squares in output units. The UI averages the frames around what
** plays now. */
typedef struct {
    UInt32 startFrame;
    UInt32 endFrame;
    Int32  owner[MAX_CHANNELS];         /* handle of the chip in each pool slot */
    float  level[MAX_CHANNELS][MIXER_MAX_VOICES];
} VoiceFrame;

#define VOICE_RING  64
#define VOICE_FRAME 512     /* ~12 ms; mixerSync can run for a handful of frames */

/* One meter block as the emulator thread produced it: mean square in
** output units (32767 = 0 dBFS). Channel levels leave out the master. */
typedef struct {
    UInt32 endFrame;
    float  master[2];
    float  type[MIXER_CHANNEL_TYPE_COUNT][2];
} MeterBlock;

/* The same block per chip, keyed by handle. */
typedef struct {
    Int32  instances;
    Int32  handle[MAX_CHANNELS];
    float  instance[MAX_CHANNELS][2];
} MeterInstances;

/* A block the UI has taken from the ring, and when it did. */
typedef struct {
    MeterBlock block;
    UInt32     takenAt;
} MeterHeard;

/* The meters per chip and voice, which exist only while enabled. Indexes
** follow the meter ring and the heard blocks. */
typedef struct {
    MeterInstances  ring[METER_RING];
    MeterInstances  heard[METER_HEARD];
    VoiceFrame      voiceRing[VOICE_RING];
    volatile UInt32 voiceSeq;
} DetailMeters;

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

    /* Instances for the detailed mixer. The list changes at machine start,
    ** on a cartridge swap and on a printer type change; the generation is
    ** odd while it changes so readers retry (see LAYOUT_BUMP). */
    Int32   createSlot;
    Int32   createSslot;
    char    createName[16];
    Int32   createDevice;
    Int32   deviceCount;
    volatile UInt32 layoutGen;
    /* Held while channels or voice tables come and go, and by the UI calls
    ** that would read a freed voice table or race on the settings. */
    void*   layoutLock;
    InstanceSetting instances[INSTANCE_SETTINGS];
    Int32   soloCount;

    /* Voice tables lent to chips; owner 0 means free */
    MixerVoices voicePool[MAX_CHANNELS];
    Int32   voiceOwner[MAX_CHANNELS];
    const char* const* voiceNames[MAX_CHANNELS];
    const UInt8* voiceGroups[MAX_CHANNELS];
    UInt32  voiceFrameStart;            /* where the frame being summed began */

    /* Set and freed by the UI thread under the layout lock, which the
    ** emulator thread takes to write into it. */
    DetailMeters* detail;
    volatile Int32 detailFresh;         /* the chips' sums predate it */

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
    UInt32  meterLastTaken;     /* when a block last reached the speakers */
    UInt32  meterMidiAt;
    int     meterMidiLit;
};


static void recalculateChannelVolume(Mixer* mixer, MixerChannel* channel);
static void recalculateInstances(Mixer* mixer);
static void releaseVoices(Mixer* mixer, int pool);
static void layoutLock(Mixer* mixer);
static void layoutUnlock(Mixer* mixer);


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

static InstanceSetting* findInstance(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index)
{
    int i;

    for (i = 0; i < INSTANCE_SETTINGS; i++) {
        InstanceSetting* s = mixer->instances + i;
        if (s->used && s->type == type && s->slot == slot &&
            s->sslot == sslot && s->index == index) {
            return s;
        }
    }
    return NULL;
}

/* The chip now plugged in under an instance key, or NULL. */
static MixerChannel* findChannel(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index)
{
    int i;

    for (i = 0; i < mixer->channelCount; i++) {
        MixerChannel* channel = mixer->channels + i;
        if (channel->type == type && channel->slot == slot &&
            channel->sslot == sslot && channel->index == index) {
            return channel;
        }
    }
    return NULL;
}

/* Detailed mixer gain of a channel. Mute wins over solo; while anything is
** soloed, only soloed instances play. A channel without an index (MIDI's
** stand-in) is never affected. */
static DoubleT instanceGain(Mixer* mixer, MixerChannel* channel)
{
    InstanceSetting* s;

    if (channel->index < 1) {
        return 1.0;
    }
    s = findInstance(mixer, channel->type, channel->slot, channel->sslot, channel->index);
    if (s != NULL && s->mute) {
        return 0.0;
    }
    if (mixer->soloCount > 0 && (s == NULL || (!s->solo && !s->voiceSolo))) {
        return 0.0;
    }
    return s != NULL ? s->gain : 1.0;
}

/* Gain of one voice on top of its chip's: a voice solo silences the other
** voices of that chip unless the whole chip is soloed. */
static DoubleT voiceGain(Mixer* mixer, InstanceSetting* s, int voice)
{
    UInt64 bit = (UInt64)1 << voice;

    if (s == NULL) {
        return 1.0;
    }
    if (s->voiceMute & bit) {
        return 0.0;
    }
    if (mixer->soloCount > 0 && !s->solo && s->voiceSolo && !(s->voiceSolo & bit)) {
        return 0.0;
    }
    return s->voiceGain[voice];
}

static int instanceIsDefault(InstanceSetting* s)
{
    int i;

    if (s->gain != 1.0 || s->mute || s->solo || s->voiceMute || s->voiceSolo) {
        return 0;
    }
    for (i = 0; i < MIXER_MAX_VOICES; i++) {
        if (s->voiceGain[i] != 1.0) {
            return 0;
        }
    }
    return 1;
}

/* What a removed chip leaves for when it comes back: its own volume and mute
** (gain, mute) stay, its voice settings and solos go. */
static void forgetRemovedInstance(InstanceSetting* s)
{
    int v;

    s->solo      = 0;
    s->voiceSolo = 0;
    s->voiceMute = 0;
    for (v = 0; v < MIXER_MAX_VOICES; v++) {
        s->voiceGain[v] = 1.0;
    }
}

/* Write the effective voice gains of a chip into the table it reads. */
static void recalculateVoices(Mixer* mixer, MixerChannel* channel)
{
    MixerVoices* voices;
    InstanceSetting* s;
    Int32 active = 0;
    int i;

    if (channel->voices < 0) {
        return;
    }
    voices = mixer->voicePool + channel->voices;
    s = findInstance(mixer, channel->type, channel->slot, channel->sslot, channel->index);
    for (i = 0; i < voices->count; i++) {
        DoubleT gain = voiceGain(mixer, s, i);
        voices->gainF[i] = (float)gain;
        voices->gain[i]  = (Int32)(gain * 4096 + 0.5);
        active |= voices->gain[i] != 4096;
    }
    voices->active = active;
}

/* A fresh setting for an instance, or NULL when the table is full. */
static InstanceSetting* addInstance(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index)
{
    InstanceSetting* s = findInstance(mixer, type, slot, sslot, index);
    int i;

    for (i = 0; s == NULL && i < INSTANCE_SETTINGS; i++) {
        if (!mixer->instances[i].used) {
            MixerChannel* channel;
            int v;
            s = mixer->instances + i;
            s->type      = type;
            s->slot      = slot;
            s->sslot     = sslot;
            s->index     = index;
            s->gain      = 1.0;
            s->mute      = 0;
            s->solo      = 0;
            s->voiceMute = 0;
            s->voiceSolo = 0;
            for (v = 0; v < MIXER_MAX_VOICES; v++) {
                s->voiceGain[v] = 1.0;
            }
            channel = findChannel(mixer, type, slot, sslot, index);
            s->voiceCount = channel != NULL && channel->voices >= 0 ? mixer->voicePool[channel->voices].count : 0;
            s->voiceNames = channel != NULL && channel->voices >= 0 ? mixer->voiceNames[channel->voices] : NULL;
            s->used      = 1;
        }
    }
    return s;
}

/* The type mixer's curve, scaled so that 100 is exactly unchanged. */
static DoubleT detailGain(Int32 volume)
{
    DoubleT bottom = pow(10.0, -100 / 60.0);

    volume = MAX(0, MIN(100, volume));
    return volume == 100 ? 1.0 : (pow(10.0, (volume - 100) / 60.0) - bottom) / (1.0 - bottom);
}

/* The volume detailGain was given back from its gain. */
static Int32 detailVolume(DoubleT gain)
{
    DoubleT bottom = pow(10.0, -100 / 60.0);

    if (gain >= 1.0) {
        return 100;
    }
    return MAX(0, (Int32)floor(100 + 60 * log10(gain * (1.0 - bottom) + bottom) + 0.5));
}

static void recalculateChannelVolume(Mixer* mixer, MixerChannel* channel)
{
    DoubleT volume        = (pow(10.0, (channel->volume - 100) / 60.0) - pow(10.0, -100 / 60.0)) *
                            instanceGain(mixer, channel);
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

/* Emulator thread: the meters per chip and voice with the layout lock
** held, or NULL without it when they are off. */
static DetailMeters* lockDetail(Mixer* mixer)
{
    DetailMeters* detail;

    if (mixer->detail == NULL) {
        return NULL;
    }
    layoutLock(mixer);
    detail = mixer->detail;
    if (detail == NULL) {
        layoutUnlock(mixer);
    }
    return detail;
}

static void unlockDetail(Mixer* mixer, DetailMeters* detail)
{
    if (detail != NULL) {
        layoutUnlock(mixer);
    }
}

/* Emulator thread: close the current block and publish it. */
static void meterPushBlock(Mixer* mixer)
{
    UInt32 seq = mixer->meterSeq;
    MeterBlock* block = mixer->meterRing + (seq & (METER_RING - 1));
    DetailMeters* detail = lockDetail(mixer);
    MeterInstances* inst = detail != NULL ? detail->ring + (seq & (METER_RING - 1)) : NULL;
    int i;

    memset(block->type, 0, sizeof(block->type));
    /* The turboR PCM mute silences the machine: show silence and take the
    ** offsets afresh once it is back, dropping the block across. */
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
        float   left      = (float)(powerLeft * gainLeft * gainLeft);
        float   right     = (float)(powerRight * gainRight * gainRight);

        /* Chips of one type add up like the mixer adds them: in power. */
        block->type[channel->type][0] += left;
        block->type[channel->type][1] += right;
        if (inst != NULL) {
            inst->instance[i][0] = left;
            inst->instance[i][1] = right;
            inst->handle[i]      = channel->handle;
        }
        channel->meterEnergy[0] = 0;
        channel->meterEnergy[1] = 0;
        channel->meterSum[0]    = 0;
        channel->meterSum[1]    = 0;
    }
    if (inst != NULL) {
        inst->instances = i;
    }
    unlockDetail(mixer, detail);
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

/* Emulator thread: drop what the chips summed for their voices. */
static void clearVoiceSums(Mixer* mixer)
{
    int i;

    for (i = 0; i < mixer->channelCount; i++) {
        int pool = mixer->channels[i].voices;
        if (pool >= 0) {
            MixerVoices* voices = mixer->voicePool + pool;
            memset(voices->sum, 0, voices->count * sizeof(voices->sum[0]));
            memset(voices->sumSq, 0, voices->count * sizeof(voices->sumSq[0]));
            voices->samples = 0;
        }
    }
}

/* Emulator thread, after each update: once VOICE_FRAME frames went by,
** publish what the chips summed for their voices. The variance is used,
** so steady DC reads as silence. */
static void meterPushVoices(Mixer* mixer)
{
    DetailMeters* detail;
    UInt32 seq;
    VoiceFrame* frame;
    int i;
    int v;

    if (mixer->detail == NULL) {
        return;
    }
    /* The chips run on through the turboR PCM mute; their voices read as
    ** silent then, like the chips' own meters. */
    if (mixer->detailFresh || !mixer->enable) {
        clearVoiceSums(mixer);
        if (mixer->detailFresh) {
            mixer->detailFresh     = 0;
            mixer->voiceFrameStart = mixer->meterFrames;
        }
    }
    if (mixer->meterFrames - mixer->voiceFrameStart < VOICE_FRAME) {
        return;
    }
    detail = lockDetail(mixer);
    if (detail == NULL) {
        return;
    }

    seq   = detail->voiceSeq;
    frame = detail->voiceRing + (seq & (VOICE_RING - 1));
    memset(frame->owner, 0, sizeof(frame->owner));
    for (i = 0; i < mixer->channelCount; i++) {
        MixerChannel* channel = mixer->channels + i;
        int pool = channel->voices;
        MixerVoices* voices;
        DoubleT samples;
        DoubleT gain;
        DoubleT scale;

        if (pool < 0) {
            continue;
        }
        voices  = mixer->voicePool + pool;
        samples = voices->samples;
        scale   = voices->scale;
        gain    = ((DoubleT)channel->meterGainLeft * channel->meterGainLeft +
                   (DoubleT)channel->meterGainRight * channel->meterGainRight) / 2 / (4096.0 * 4096.0);
        frame->owner[pool] = channel->handle;
        for (v = 0; v < voices->count; v++) {
            DoubleT level = 0;
            if (samples > 0) {
                DoubleT mean = voices->sum[v] / samples;
                level = voices->sumSq[v] / samples - mean * mean;
            }
            frame->level[pool][v] = (float)(MAX(level, 0) * scale * scale * gain);
        }
    }
    clearVoiceSums(mixer);
    frame->startFrame      = mixer->voiceFrameStart;
    frame->endFrame        = mixer->meterFrames;
    mixer->voiceFrameStart = mixer->meterFrames;

    METER_BARRIER();
    detail->voiceSeq = seq + 1;
    unlockDetail(mixer, detail);
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
        MeterInstances inst;

        if (mixer->detail != NULL) {
            inst = mixer->detail->ring[s & (METER_RING - 1)];
        }
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
        if (mixer->detail != NULL) {
            mixer->detail->heard[mixer->meterHeardPos & (METER_HEARD - 1)] = inst;
        }
        mixer->meterHeardPos++;
        mixer->meterLastTaken = now;
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

/* Same as meterLevel for one chip; a block without the chip counts as
** silence. */
static DoubleT meterInstanceLevel(Mixer* mixer, Int32 handle, int side)
{
    UInt32 from = mixer->meterHeardFrame - METER_WINDOW / 2;
    DoubleT energy = 0;
    int count = 0;
    int i;
    int j;

    if (mixer->detail == NULL) {
        return 0;
    }
    for (i = 0; i < METER_HEARD; i++) {
        MeterHeard* heard = mixer->meterHeard + i;
        const MeterInstances* inst = mixer->detail->heard + i;

        if (mixer->meterTick - heard->takenAt >= METER_HOLD_MS ||
            (Int32)(heard->block.endFrame - from) <= 0) {
            continue;
        }
        for (j = 0; j < inst->instances; j++) {
            if (inst->handle[j] == handle) {
                energy += inst->instance[j][side];
                break;
            }
        }
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

Int32 mixerMeterGetInstancePercent(Mixer* mixer, Int32 handle, int leftRight)
{
    meterAdvance(mixer);
    return meterPercent(meterInstanceLevel(mixer, handle, leftRight ? 1 : 0));
}

/* Average of the voice frames overlapping the window around the frame
** playing now, weighted by how much of each falls inside the window. */
Int32 mixerMeterGetVoicePercent(Mixer* mixer, Int32 handle, Int32 voice)
{
    DetailMeters* detail = mixer->detail;
    UInt32 from;
    UInt32 to;
    UInt32 seq;
    UInt32 k;
    DoubleT energy = 0;
    DoubleT weight = 0;
    int j;

    meterAdvance(mixer);
    if (detail == NULL || voice < 0 || voice >= MIXER_MAX_VOICES ||
        mixer->meterTick - mixer->meterLastTaken >= METER_HOLD_MS) {
        return 0;
    }
    from = mixer->meterHeardFrame - METER_WINDOW / 2;
    to   = mixer->meterHeardFrame + METER_WINDOW / 2;
    seq  = detail->voiceSeq;
    METER_BARRIER();

    for (k = 1; k <= VOICE_RING / 2 && k <= seq; k++) {
        const VoiceFrame* frame = detail->voiceRing + ((seq - k) & (VOICE_RING - 1));
        UInt32 start = frame->startFrame;
        UInt32 end   = frame->endFrame;
        UInt32 inStart;
        UInt32 inEnd;

        if ((Int32)(end - from) <= 0) {
            break;
        }
        if ((Int32)(start - to) >= 0) {
            continue;
        }
        inStart = (Int32)(start - from) > 0 ? start : from;
        inEnd   = (Int32)(end - to) < 0 ? end : to;
        for (j = 0; j < MAX_CHANNELS; j++) {
            if (frame->owner[j] == handle) {
                energy += frame->level[j][voice] * (DoubleT)(inEnd - inStart);
                weight += inEnd - inStart;
                break;
            }
        }
    }
    METER_BARRIER();
    if (detail->voiceSeq - seq >= VOICE_RING / 2 || weight <= 0) {
        return 0;
    }
    return meterPercent(sqrt(energy / weight));
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
    mixer->createSlot  = -1;
    mixer->createSslot = -1;
    mixer->layoutLock  = archSemaphoreCreate(1);

    if (globalMixer == NULL) globalMixer = mixer;

    return mixer;
}

void mixerDestroy(Mixer* mixer)
{
    mixerStopLog(mixer);
    globalMixer = NULL;
    archSemaphoreDestroy(mixer->layoutLock);
    free(mixer->detail);
    free(mixer);
}

void mixerEnableDetailMeters(Mixer* mixer, Int32 enable)
{
    DetailMeters* detail = NULL;
    DetailMeters* old;
    int i;

    if ((mixer->detail != NULL) == (enable != 0)) {
        return;
    }
    if (enable) {
        detail = (DetailMeters*)calloc(1, sizeof(DetailMeters));
        if (detail == NULL) {
            return;
        }
    }
    layoutLock(mixer);
    old = mixer->detail;
    mixer->detail = detail;
    mixer->detailFresh = detail != NULL;
    for (i = 0; i < MAX_CHANNELS; i++) {
        mixer->voicePool[i].metering = detail != NULL;
    }
    layoutUnlock(mixer);
    free(old);
}

void mixerResetLayoutLock(Mixer* mixer)
{
    archSemaphoreDestroy(mixer->layoutLock);
    mixer->layoutLock = archSemaphoreCreate(1);
}

static void layoutLock(Mixer* mixer)
{
    archSemaphoreWait(mixer->layoutLock, -1);
}

static void layoutUnlock(Mixer* mixer)
{
    archSemaphoreSignal(mixer->layoutLock);
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

static Int32 registerChannel(Mixer* mixer, Int32 audioType, Int32 stereo, MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback, void* ref)
{
    MixerChannel*  channel = mixer->channels + mixer->channelCount;
    AudioTypeInfo* type    = mixer->audioTypeInfo + audioType;
    int i;

    if (mixer->channelCount >= MAX_CHANNELS) {
        return 0;
    }

    LAYOUT_BUMP(mixer->layoutGen);
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
    channel->slot           = mixer->createSlot;
    channel->sslot          = mixer->createSslot;
    channel->device         = mixer->createDevice;
    channel->voices         = -1;
    memcpy(channel->name, mixer->createName, sizeof(channel->name));
    /* The lowest index not taken, so a re-plugged chip gets back the one
    ** it would get on the next machine start. */
    channel->index          = 1;
    for (i = 0; i < mixer->channelCount - 1; i++) {
        MixerChannel* other = mixer->channels + i;
        if (other->type == audioType && other->slot == channel->slot &&
            other->sslot == channel->sslot && other->index == channel->index) {
            channel->index++;
            i = -1;
        }
    }

    recalculateInstances(mixer);

    LAYOUT_BUMP(mixer->layoutGen);

    return channel->handle;
}

static void unregisterChannel(Mixer* mixer, Int32 handle)
{
    MixerChannel removed;
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

    removed = mixer->channels[i];
    LAYOUT_BUMP(mixer->layoutGen);
    mixer->channelCount--;
    while (i < mixer->channelCount) {
        mixer->channels[i] = mixer->channels[i + 1];
        i++;
    }
    LAYOUT_BUMP(mixer->layoutGen);

    if (removed.voices >= 0) {
        releaseVoices(mixer, removed.voices);
    }
    recalculateInstances(mixer);
}

/* Free a voice table and the levels summed for it. */
static void releaseVoices(Mixer* mixer, int pool)
{
    mixer->voiceOwner[pool]  = 0;
    mixer->voiceGroups[pool] = NULL;
    memset(mixer->voicePool + pool, 0, sizeof(MixerVoices));
    mixer->voicePool[pool].metering = mixer->detail != NULL;
}

static MixerVoices* attachVoices(Mixer* mixer, Int32 handle, Int32 count,
                                 const char* const* names, float scale)
{
    MixerChannel* channel = NULL;
    InstanceSetting* s;
    int i;
    int pool;

    for (i = 0; i < mixer->channelCount; i++) {
        if (mixer->channels[i].handle == handle) {
            channel = mixer->channels + i;
        }
    }
    if (channel == NULL || handle == 0 || count <= 0 || count > MIXER_MAX_VOICES) {
        return NULL;
    }
    if (channel->voices >= 0) {
        return mixer->voicePool + channel->voices;
    }
    for (pool = 0; pool < MAX_CHANNELS && mixer->voiceOwner[pool] != 0; pool++) {
    }
    if (pool == MAX_CHANNELS) {
        return NULL;
    }

    releaseVoices(mixer, pool);
    mixer->voicePool[pool].count = count;
    mixer->voiceNames[pool]      = names;
    mixer->voicePool[pool].scale = scale;
    mixer->voicePool[pool].live  = 1;
    mixer->voiceOwner[pool]      = handle;
    channel->voices              = pool;
    /* Another chip under the same key (a machine switch swaps the PSG for an
    ** SN76489) starts with fresh voices, as if the old one had been removed. */
    s = findInstance(mixer, channel->type, channel->slot, channel->sslot, channel->index);
    if (s != NULL && s->voiceNames != NULL && (s->voiceCount != count || s->voiceNames != names)) {
        forgetRemovedInstance(s);
        if (instanceIsDefault(s)) {
            s->used = 0;
        }
    }
    if (s != NULL) {
        s->voiceCount = count;
        s->voiceNames = names;
    }
    recalculateInstances(mixer);
    return mixer->voicePool + pool;
}

static int getVoices(Mixer* mixer, Int32 handle, MixerVoiceInfo* out, int max)
{
    int pool;
    UInt32 idle;
    int count;
    int i;

    for (pool = 0; pool < MAX_CHANNELS && mixer->voiceOwner[pool] != handle; pool++) {
    }
    if (handle == 0 || pool == MAX_CHANNELS) {
        return 0;
    }
    count = mixer->voicePool[pool].count;
    idle  = mixer->voicePool[pool].idle;
    for (i = 0; i < count && i < max; i++) {
        const char* name = mixer->voiceNames[pool] != NULL ? mixer->voiceNames[pool][i] : NULL;
        strncpy(out[i].name, name != NULL ? name : "", sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = 0;
        out[i].live = mixer->voicePool[pool].live;
        out[i].idle = i < 32 && ((idle >> i) & 1);
        out[i].group = mixer->voiceGroups[pool] != NULL ? mixer->voiceGroups[pool][i] : -1;
    }
    return count;
}

/* The emulator thread changes the channels while the detailed mixer reads
** them from the UI thread; these take the layout lock around it. */
Int32 mixerRegisterChannel(Mixer* mixer, Int32 audioType, Int32 stereo, MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback, void* ref)
{
    Int32 handle;
    layoutLock(mixer);
    handle = registerChannel(mixer, audioType, stereo, callback, rateCallback, ref);
    layoutUnlock(mixer);
    return handle;
}

void mixerUnregisterChannel(Mixer* mixer, Int32 handle)
{
    layoutLock(mixer);
    unregisterChannel(mixer, handle);
    layoutUnlock(mixer);
}

MixerVoices* mixerAttachVoices(Mixer* mixer, Int32 handle, Int32 count,
                               const char* const* names, float scale)
{
    MixerVoices* voices;
    layoutLock(mixer);
    voices = attachVoices(mixer, handle, count, names, scale);
    layoutUnlock(mixer);
    return voices;
}

void mixerSetVoiceGroups(Mixer* mixer, Int32 handle, const UInt8* groups)
{
    int pool;

    layoutLock(mixer);
    for (pool = 0; pool < MAX_CHANNELS; pool++) {
        if (handle != 0 && mixer->voiceOwner[pool] == handle) {
            mixer->voiceGroups[pool] = groups;
        }
    }
    layoutUnlock(mixer);
}

int mixerGetVoices(Mixer* mixer, Int32 handle, MixerVoiceInfo* out, int max)
{
    int count;
    layoutLock(mixer);
    count = getVoices(mixer, handle, out, max);
    layoutUnlock(mixer);
    return count;
}

void mixerSetCreateSlot(Mixer* mixer, int slot, int sslot)
{
    mixer->createSlot   = slot < 0 ? -1 : slot;
    mixer->createSslot  = slot < 0 ? -1 : sslot;
    mixer->createDevice = slot < 0 ? 0 : ++mixer->deviceCount;
    mixer->createName[0] = 0;
}

void mixerSetCreateName(Mixer* mixer, const char* name)
{
    strncpy(mixer->createName, name != NULL ? name : "", sizeof(mixer->createName) - 1);
    mixer->createName[sizeof(mixer->createName) - 1] = 0;
}

int mixerGetInstances(Mixer* mixer, MixerInstanceInfo* out, int max, UInt32* generation)
{
    MixerChannel channels[MAX_CHANNELS];
    UInt32 gen;
    int tries = 0;
    int count;
    int i;

    /* Copy the list between two equal, even generations; give up retrying
    ** after a while so a stalled writer cannot hang the UI. */
    do {
        gen = mixer->layoutGen;
        METER_BARRIER();
        count = MIN(mixer->channelCount, MAX_CHANNELS);
        memcpy(channels, mixer->channels, count * sizeof(MixerChannel));
        METER_BARRIER();
    } while (((gen & 1) || gen != mixer->layoutGen) && ++tries < LAYOUT_TRIES);

    for (i = 0; i < count && i < max; i++) {
        out[i].handle = channels[i].handle;
        out[i].type   = channels[i].type;
        out[i].stereo = channels[i].stereo;
        out[i].slot   = channels[i].slot;
        out[i].sslot  = channels[i].sslot;
        out[i].index  = channels[i].index;
        out[i].device = channels[i].device;
        memcpy(out[i].name, channels[i].name, sizeof(out[i].name));
    }
    if (generation != NULL) {
        *generation = gen;
    }
    return count;
}

/* Only solos of chips plugged in now count, so a chip gone by any path
** cannot leave the others silent. Run it whenever a chip comes or goes. */
static void recalculateInstances(Mixer* mixer)
{
    int i;

    mixer->soloCount = 0;
    for (i = 0; i < INSTANCE_SETTINGS; i++) {
        InstanceSetting* s = mixer->instances + i;
        if (s->used && (s->solo || s->voiceSolo) &&
            findChannel(mixer, s->type, s->slot, s->sslot, s->index) != NULL) {
            mixer->soloCount++;
        }
    }
    for (i = 0; i < mixer->channelCount; i++) {
        recalculateChannelVolume(mixer, mixer->channels + i);
        recalculateVoices(mixer, mixer->channels + i);
    }
}

int mixerSetInstance(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index,
                     Int32 volume, Int32 mute, Int32 solo)
{
    InstanceSetting* s;

    layoutLock(mixer);
    s = addInstance(mixer, type, slot, sslot, index);
    if (s == NULL) {
        layoutUnlock(mixer);
        return 0;
    }
    s->gain = detailGain(volume);
    s->mute = mute ? 1 : 0;
    s->solo = solo ? 1 : 0;
    if (instanceIsDefault(s)) {
        s->used = 0;
    }
    recalculateInstances(mixer);
    layoutUnlock(mixer);
    return 1;
}

void mixerGetInstanceSetting(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index,
                             Int32 voice, Int32* volume, Int32* mute, Int32* solo)
{
    InstanceSetting* s = findInstance(mixer, type, slot, sslot, index);
    UInt64 bit = voice >= 0 && voice < MIXER_MAX_VOICES ? (UInt64)1 << voice : 0;

    *volume = 100;
    *mute   = 0;
    *solo   = 0;
    if (s == NULL || (voice >= 0 && bit == 0)) {
        return;
    }
    if (voice < 0) {
        *volume = detailVolume(s->gain);
        *mute   = s->mute;
        *solo   = s->solo;
    }
    else {
        *volume = detailVolume(s->voiceGain[voice]);
        *mute   = (s->voiceMute & bit) != 0;
        *solo   = (s->voiceSolo & bit) != 0;
    }
}

int mixerSetVoice(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index,
                  Int32 voice, Int32 volume, Int32 mute, Int32 solo)
{
    InstanceSetting* s;
    UInt64 bit;

    if (voice < 0 || voice >= MIXER_MAX_VOICES) {
        return 0;
    }
    layoutLock(mixer);
    s = addInstance(mixer, type, slot, sslot, index);
    if (s == NULL) {
        layoutUnlock(mixer);
        return 0;
    }
    bit = (UInt64)1 << voice;
    s->voiceGain[voice] = detailGain(volume);
    s->voiceMute = mute ? s->voiceMute | bit : s->voiceMute & ~bit;
    s->voiceSolo = solo ? s->voiceSolo | bit : s->voiceSolo & ~bit;
    if (instanceIsDefault(s)) {
        s->used = 0;
    }
    recalculateInstances(mixer);
    layoutUnlock(mixer);
    return 1;
}

void mixerForgetRemovedInstances(Mixer* mixer)
{
    int i;

    layoutLock(mixer);
    for (i = 0; i < INSTANCE_SETTINGS; i++) {
        InstanceSetting* s = mixer->instances + i;
        if (s->used && findChannel(mixer, s->type, s->slot, s->sslot, s->index) == NULL) {
            forgetRemovedInstance(s);
            if (instanceIsDefault(s)) {
                s->used = 0;
            }
        }
    }
    recalculateInstances(mixer);
    layoutUnlock(mixer);
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

        /* The turboR PCM mute silences the output; the chips run on. */
        if (!mixer->masterEnable || !mixer->enable) {
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

    meterPushVoices(mixer);
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
