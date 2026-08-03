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
#ifndef AUDIO_CASSETTE_H
#define AUDIO_CASSETTE_H

#include "MsxTypes.h"
#include "AudioMixer.h"

typedef struct AudioCassette AudioCassette;

AudioCassette* audioCassetteCreate(Mixer* mixer);
void audioCassetteDestroy(AudioCassette* cassette);

#endif
