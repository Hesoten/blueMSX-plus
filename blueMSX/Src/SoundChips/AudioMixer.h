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
    MIXER_CHANNEL_TYPE_COUNT
} MixerAudioType;

typedef enum {
    MIXER_CHANNEL_LEFT = 0,
    MIXER_CHANNEL_RIGHT
} MixerChannelPan;

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

/* File logging methods */
void mixerStartLog(Mixer* mixer, char* fileName);
int  mixerIsLogging(Mixer* mixer);
void mixerStopLog(Mixer* mixer);

/* Internal interface methods */
void mixerReset(Mixer* mixer);
void mixerSync(Mixer* mixer);

Int32 mixerRegisterChannel(Mixer* mixer, Int32 audioType, Int32 stereo,
                           MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback,
                           void*param);
/* As mixerRegisterChannel, for a device that registers several channels of
** one type and wants them told apart by name rather than by order. The name
** becomes part of the channel's id and of the keys its settings are saved
** under, so keep it short and stable: lowercase letters and digits, not
** "n" followed by a digit (that is the order suffix), and not beginning with
** "enable", "pan" or "volume" (the settings reader matches keys by prefix). */
Int32 mixerRegisterChannelNamed(Mixer* mixer, Int32 audioType, Int32 stereo,
                                MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback,
                                void* param, const char* name);
void mixerSetEnable(Mixer* mixer, int enable);
void mixerUnregisterChannel(Mixer* mixer, Int32 handle);

/* Instances
**
** A channel belongs to the device instance that registered it, and each
** instance can be set on its own on top of its type: the volumes multiply,
** the instance pan shifts the type pan (50 leaves it), and both must be
** enabled. An instance at volume 100, pan 50, enabled, plays as before.
**
** Whoever creates a device that may sit in a slot brackets the creation
** with mixerBeginDevice and mixerEndDevice, passing the slot manager's
** registration count for the device's slot both times. Channels registered
** in between belong to that slot if the count moved (the device mapped
** memory), and to the board if it did not (the device only uses I/O
** ports). Beginning a device while another is open puts the open one's
** channels on the board. Channels registered outside a bracket belong to
** the board.
**
** A channel's id names its instance: "slot1-0" or "board", then "-c0" if the
** device gave its first I/O port (board only), then ".<name>" if it
** registered by name, then ".n2", ".n3"... for the second and later channels
** of one type with the same place and name. */
void mixerBeginDevice(Mixer* mixer, int slot, int sslot, UInt32 slotRegistrations);
/* For a device on I/O ports only, whose instances differ by port. */
void mixerSetDevicePort(Mixer* mixer, int port);
void mixerEndDevice(Mixer* mixer, UInt32 slotRegistrations);

typedef enum {
    MIXER_LOCATION_BOARD = 0,
    MIXER_LOCATION_SLOT
} MixerLocation;

#define MIXER_CHANNEL_NAME_LENGTH 16
#define MIXER_CHANNEL_ID_LENGTH   48

typedef struct {
    Int32 handle;
    Int32 type;
    Int32 stereo;
    Int32 location;     /* MixerLocation */
    Int32 slot;         /* MIXER_LOCATION_SLOT only */
    Int32 sslot;
    Int32 port;         /* -1 if the device gave none */
    Int32 ordinal;      /* 1 for the first channel of its type at its place */
    char  name[MIXER_CHANNEL_NAME_LENGTH];
    char  id[MIXER_CHANNEL_ID_LENGTH];
    Int32 enable;       /* the instance's own settings */
    Int32 volume;
    Int32 pan;
} MixerChannelInfo;

/* Fills up to maxCount entries in registration order and returns the number
** of channels, which may be more than maxCount. Channels still inside an
** open device bracket are left out. */
int    mixerGetChannelInfo(Mixer* mixer, MixerChannelInfo* info, int maxCount);
/* Changes whenever a channel comes or goes, so a view can tell when to
** rebuild its list. */
UInt32 mixerGetChannelGeneration(Mixer* mixer);

void  mixerSetChannelVolume(Mixer* mixer, Int32 handle, Int32 volume);
void  mixerSetChannelPan(Mixer* mixer, Int32 handle, Int32 pan);
void  mixerEnableChannel(Mixer* mixer, Int32 handle, Int32 enable);
/* One channel's level meter, read like the type meters above: the same
** scale and window, before the master volume and mute. The only playing
** channel of its type reads as the type. UI thread. */
Int32 mixerMeterGetChannelPercent(Mixer* mixer, Int32 handle, int leftRight);

/* Asked for an instance's saved settings when its id is first known. The
** values come in at their defaults (enabled, 100, 50) and are kept as the
** callback leaves them. Called with the mixer's lock held, so it must not
** call back into the mixer. */
typedef void (*MixerChannelSettingsCallback)(void* ref, Int32 audioType, const char* id,
                                             Int32* enable, Int32* volume, Int32* pan);
void mixerSetChannelSettingsCallback(Mixer* mixer, MixerChannelSettingsCallback callback, void* ref);

void mixerSetBoardFrequency(int CPUFrequency);
void mixerSetBoardFrequencyFixed(int CPUFrequency);

#endif

