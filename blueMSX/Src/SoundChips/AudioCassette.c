/*****************************************************************************
**
** Cassette tape playback as a mixer channel.
** Copyright (C) 2026 Hesoten
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
#include "AudioCassette.h"
#include "TapeSignal.h"
#include <stdlib.h>
#include <string.h>

static Int32* audioCassetteSync(void* ref, UInt32 count);

struct AudioCassette
{
    Mixer* mixer;
    Int32  handle;
    Int32  buffer[AUDIO_MONO_BUFFER_SIZE];
};

AudioCassette* audioCassetteCreate(Mixer* mixer)
{
    AudioCassette* cassette = (AudioCassette*)calloc(1, sizeof(AudioCassette));

    cassette->mixer  = mixer;
    cassette->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_CASSETTE, 0,
                                            audioCassetteSync, NULL, cassette);
    return cassette;
}

void audioCassetteDestroy(AudioCassette* cassette)
{
    mixerUnregisterChannel(cassette->mixer, cassette->handle);
    free(cassette);
}

/* The waveform is the tape itself, so nothing is buffered here. A NULL
** result tells the mixer this channel is silent for this block. */
static Int32* audioCassetteSync(void* ref, UInt32 count)
{
    AudioCassette* cassette = (AudioCassette*)ref;

    if (count > AUDIO_MONO_BUFFER_SIZE) {
        count = AUDIO_MONO_BUFFER_SIZE;
    }
    return tapeSignalRenderAudio(cassette->buffer, count);
}
