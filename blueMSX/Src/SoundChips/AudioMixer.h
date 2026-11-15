/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/SoundChips/AudioMixer.h,v $
**
** $Revision: 1.14 $
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
#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include "MsxTypes.h"

/* Type definitions */
typedef struct Mixer Mixer;

#define AUDIO_MONO_BUFFER_SIZE    10000
#define AUDIO_STEREO_BUFFER_SIZE  (2 * AUDIO_MONO_BUFFER_SIZE)

#define AUDIO_SAMPLERATE       44100

typedef enum { 
    MIXER_CHANNEL_PSG = 0,
    MIXER_CHANNEL_SCC,
    MIXER_CHANNEL_MSXMUSIC,
    MIXER_CHANNEL_MSXAUDIO,
    MIXER_CHANNEL_MOONSOUND,
    MIXER_CHANNEL_YAMAHA_SFG,
    MIXER_CHANNEL_KEYBOARD,
    MIXER_CHANNEL_PCM,
    MIXER_CHANNEL_IO,
    MIXER_CHANNEL_MIDI,
    MIXER_CHANNEL_CASSETTE,
    /* The two outputs of a YM2608 (OPNA). A board mixes them outside the
    ** chip, so the balance between them is not the chip's: each has its
    ** own level here. */
    MIXER_CHANNEL_OPNA_FM,
    MIXER_CHANNEL_OPNA_SSG,
    /* Every Y8960 block shares this one type, so the cartridge can be
    ** balanced against the machine with a single control. */
    MIXER_CHANNEL_Y8960,
    MIXER_CHANNEL_TYPE_COUNT
} MixerAudioType;

typedef enum {
    MIXER_CHANNEL_LEFT = 0,
    MIXER_CHANNEL_RIGHT
} MixerChannelPan;

#define MAX_CHANNELS 32

typedef Int32* (*MixerUpdateCallback)(void*, UInt32);
typedef void (*MixerSetSampleRateCallback)(void*, UInt32);
typedef Int32 (*MixerWriteCallback)(void*, Int16*, UInt32);
typedef UInt32 (*MixerDelayCallback)(void*);

/* Constructor and destructor */
Mixer* mixerCreate();
void mixerDestroy(Mixer* mixer);

Mixer* mixerGetGlobalMixer();

void mixerSetMasterVolume(Mixer* mixer, Int32 volume);
void mixerEnableMaster(Mixer* mixer, Int32 enable);
void mixerSetStereo(Mixer* mixer, Int32 stereo);
UInt32 mixerGetSampleRate(Mixer* mixer);
void mixerSetSampleRate(Mixer* mixer, UInt32 rate);

void mixerSetChannelTypeVolume(Mixer* mixer, Int32 channelType, Int32 volume);
void mixerSetChannelTypePan(Mixer* mixer, Int32 channelType, Int32 pan);
void mixerEnableChannelType(Mixer* mixer, Int32 channelType, Int32 enable);
Int32 mixerIsChannelTypeActive(Mixer* mixer, Int32 channelType, Int32 reset);

/* Write callback registration for audio drivers */
void mixerSetWriteCallback(Mixer* mixer, MixerWriteCallback callback, void*, int);

/* Optional read-only tap that fires right after the audio driver's write
** callback for every fragment. Used by the live recorder so it can encode
** the master mix without displacing the playback driver. The tap reuses
** the same fragment cadence the playback driver established via
** mixerSetWriteCallback. Pass callback=NULL to remove the tap. */
void mixerSetTapCallback(Mixer* mixer, MixerWriteCallback callback, void* ref);

/* Frames handed to the driver that have not been heard yet. The meters
** subtract it to show what plays now. UI thread; NULL means none. */
void mixerSetDelayCallback(Mixer* mixer, MixerDelayCallback callback, void* ref);

/* Level meters for the UI thread: 0..100 on a power-curve scale, the RMS
** of ~46 ms around what plays now, measured before the master mute.
** Channel types leave out the master volume. */
Int32 mixerMeterGetTypePercent(Mixer* mixer, Int32 channelType, int leftRight);
Int32 mixerMeterGetMasterPercent(Mixer* mixer, int leftRight);
/* The same for one chip, by the handle mixerGetInstances reports. */
Int32 mixerMeterGetInstancePercent(Mixer* mixer, Int32 handle, int leftRight);
/* UI thread: measure each chip and voice only while enabled, as these meters
** cost every chip time and memory; they read 0 while disabled. */
void mixerEnableDetailMeters(Mixer* mixer, Int32 enable);

/* File logging methods */
void mixerStartLog(Mixer* mixer, char* fileName);
int  mixerIsLogging(Mixer* mixer);
void mixerStopLog(Mixer* mixer);

/* Internal interface methods */
void mixerReset(Mixer* mixer);
void mixerSync(Mixer* mixer);

/* Returns 0 when every channel is taken; 0 is never a handle, and unregistering it does nothing. */
Int32 mixerRegisterChannel(Mixer* mixer, Int32 audioType, Int32 stereo, 
                           MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback,
                           void*param);
void mixerSetEnable(Mixer* mixer, int enable);
void mixerUnregisterChannel(Mixer* mixer, Int32 handle);

/* Slot of the device whose chips are about to register, so instances of
** one chip type can be told apart. slot < 0: a device without a slot.
** Chips registered until the next call with a slot form one device. */
void mixerSetCreateSlot(Mixer* mixer, int slot, int sslot);
/* Name of the chip about to register, for a device with several chips;
** NULL or mixerSetCreateSlot drops it. */
void mixerSetCreateName(Mixer* mixer, const char* name);

typedef struct {
    Int32 handle;
    Int32 type;
    Int32 stereo;
    Int32 slot;     /* -1: built in or on I/O ports only */
    Int32 sslot;
    Int32 index;    /* 1 for the first of its type at that slot, then 2... */
    char  name[16]; /* "" unless mixerSetCreateName gave one */
    Int32 device;   /* equal for the chips of one device; 0 without a slot */
} MixerInstanceInfo;

/* UI thread: copy up to max registered instances into out and return how
** many there are; *generation changes whenever the list changes. */
int mixerGetInstances(Mixer* mixer, MixerInstanceInfo* out, int max, UInt32* generation);

/* UI thread: detailed mixer settings of one instance, kept across machine
** changes. volume 0..100 on the type mixer's curve, 100 = unchanged; mute
** wins over solo, and while any solo is on only soloed instances play. */
int  mixerSetInstance(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index,
                      Int32 volume, Int32 mute, Int32 solo);   /* 0: table full */
/* UI thread, with the emulator thread gone: replace the lock that guards
** the channel layout, in case that thread was killed while holding it. */
void  mixerResetLayoutLock(Mixer* mixer);

/* Once a start or a cartridge change has put the chips in place: a chip that
** did not come back keeps its own volume and mute, not its voices' or solos. */
void mixerForgetRemovedInstances(Mixer* mixer);

/* Voices of one chip, for per-voice gain and metering. The mixer owns the
** table and lends it to the chip, so the UI can write gains without the
** chip's lifetime; the chip must drop it when it unregisters. */
#define MIXER_MAX_VOICES 48

typedef struct MixerVoices {
    Int32  count;
    Int32  live;                        /* the chip feeds these voices now */
    /* Bit n: voice n is silent in the chip's current mode, such as FM 7-9
    ** in rhythm mode. Written whole by the chip; voices past 31 never are. */
    volatile UInt32 idle;
    float  scale;                       /* voice sample to chip output units */
    Int32  active;                      /* any gain is not unity: apply it */
    Int32  metering;                    /* the levels are wanted: sum them */
    Int32  gain[MIXER_MAX_VOICES];      /* 4096 = unity */
    float  gainF[MIXER_MAX_VOICES];
    /* While metering, the chip adds each voice's sample after the gain,
    ** once per sample; the mixer takes them every few milliseconds. */
    Int64  sum[MIXER_MAX_VOICES];
    UInt64 sumSq[MIXER_MAX_VOICES];
    UInt32 samples;
} MixerVoices;

/* Chip, once per update: whether gains or levels need the voices apart. */
#define mixerVoicesWanted(v) ((v) != NULL && ((v)->active || (v)->metering))

static __inline void mixerMeterVoice(MixerVoices* voices, int voice, Int64 sample)
{
    voices->sum[voice]   += sample;
    voices->sumSq[voice] += (UInt64)(sample * sample);
}

/* A float voice sample with its gain while active, metered times amp while metering. */
static __inline float mixerVoiceSampleF(MixerVoices* voices, int voice, float v,
                                        int active, int metered, float amp)
{
    if (active) {
        v *= voices->gainF[voice];
    }
    if (metered) {
        mixerMeterVoice(voices, voice, (Int64)(v * amp));
    }
    return v;
}

/* Chip, right after registering: borrow a voice table, live and with the
** given scale; a chip with several engines may change both later. Returns
** NULL when none is left; then the chip runs as before. */
MixerVoices* mixerAttachVoices(Mixer* mixer, Int32 handle, Int32 count,
                               const char* const* names, float scale);

/* What part of a chip a voice belongs to, for a UI that groups them. */
enum {
    MIXER_VOICE_TONE,
    MIXER_VOICE_WAVETABLE,
    MIXER_VOICE_FM,
    MIXER_VOICE_RHYTHM,
    MIXER_VOICE_WAVE,
    MIXER_VOICE_ADPCM
};

/* Chip, after mixerAttachVoices: one MIXER_VOICE_* per voice, kept by
** reference like the names. */
void mixerSetVoiceGroups(Mixer* mixer, Int32 handle, const UInt8* groups);

typedef struct {
    char  name[16];
    Int32 live;     /* 0: the chip's current engine has no per-voice output */
    Int32 idle;     /* 1: unused in the chip's current mode */
    Int32 group;    /* MIXER_VOICE_*, -1 when the chip gave none */
} MixerVoiceInfo;

/* UI thread: the voice names of the chip with that handle; returns the
** count, 0 for a chip without voices. */
int  mixerGetVoices(Mixer* mixer, Int32 handle, MixerVoiceInfo* out, int max);
/* UI thread: like mixerSetInstance for one voice. A soloed voice also
** lets its own chip play while other chips are silenced. */
int  mixerSetVoice(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index,
                   Int32 voice, Int32 volume, Int32 mute, Int32 solo);
Int32 mixerMeterGetVoicePercent(Mixer* mixer, Int32 handle, Int32 voice);
/* UI thread: what mixerSetInstance and mixerSetVoice stored for that
** instance, 100 and off when nothing is; voice < 0 reads the chip itself. */
void mixerGetInstanceSetting(Mixer* mixer, Int32 type, Int32 slot, Int32 sslot, Int32 index,
                             Int32 voice, Int32* volume, Int32* mute, Int32* solo);

void mixerSetBoardFrequency(int CPUFrequency);
void mixerSetBoardFrequencyFixed(int CPUFrequency);

#endif

