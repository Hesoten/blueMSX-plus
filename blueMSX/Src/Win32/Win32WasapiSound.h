/*****************************************************************************
**
** WASAPI audio output backend.
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
#ifndef WIN32_WASAPI_SOUND_H
#define WIN32_WASAPI_SOUND_H

#include <windows.h>
#include "MsxTypes.h"
#include "AudioMixer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WasapiSound WasapiSound;

WasapiSound* wasapiSoundCreate(HWND hwnd, Mixer* mixer, UInt32 sampleRate, UInt32 bufferSizeMs, Int16 channels);
void wasapiSoundDestroy(WasapiSound* ws);
void wasapiSoundSuspend(WasapiSound* ws);
void wasapiSoundResume(WasapiSound* ws);

/* Actual endpoint buffer size in ms (rounded), 0 if WASAPI not active. */
UInt32 wasapiSoundGetActualBufferMs(void);

#ifdef __cplusplus
}
#endif

#endif // WIN32_WASAPI_SOUND_H
