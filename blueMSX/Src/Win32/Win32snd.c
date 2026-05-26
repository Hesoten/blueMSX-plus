/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32snd.c,v $
**
** $Revision: 1.5 $
**
** $Date: 2008-03-30 18:38:48 $
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
#include "Win32Sound.h"
#include "Win32directXSound.h"
#include "Win32WasapiSound.h"
#include "Win32Avi.h"

#include "ArchSound.h"

/* Sound backends: None / DirectX / WASAPI.  Ini values containing
** "wmm" fall back to WASAPI via stringToEnum() returning -1. */

static DxSound* dxSound = NULL;
static WasapiSound* wasapiSound = NULL;
static AviSound* aviSound = NULL;

static HWND        cfgHwnd   = NULL;
static SoundDriver cfgDriver = SOUND_DRV_NONE;

void soundDriverConfig(HWND hwnd, SoundDriver driver)
{
    cfgHwnd   = hwnd;
    cfgDriver = driver;
}

void archSoundCreate(Mixer* mixer, UInt32 sampleRate, UInt32 bufferSize, Int16 channels)
{
    archSoundDestroy();
 
    switch (cfgDriver) {
    case SOUND_DRV_NONE:
        break;
    case SOUND_DRV_DIRECTX:
        dxSound = dxSoundCreate(cfgHwnd, mixer, sampleRate, bufferSize, channels);
        break;
    case SOUND_DRV_WASAPI:
        wasapiSound = wasapiSoundCreate(cfgHwnd, mixer, sampleRate, bufferSize, channels);
        if (!wasapiSound) {
            /* Fall back to DirectSound if WASAPI initialisation fails. */
            dxSound = dxSoundCreate(cfgHwnd, mixer, sampleRate, bufferSize, channels);
        }
        break;
    case SOUND_DRV_AVI:
        aviSound = aviSoundCreate(cfgHwnd, mixer, sampleRate, bufferSize, channels);
        break;
    }
}

void archSoundDestroy(void) 
{
    if (dxSound) {
        dxSoundDestroy(dxSound);
        dxSound = NULL;
    }
    if (wasapiSound) {
        wasapiSoundDestroy(wasapiSound);
        wasapiSound = NULL;
    }
    if (aviSound) {
        aviSoundDestroy(aviSound);
        aviSound = NULL;
    }
}

void archSoundResume(void) 
{
    if (dxSound) {
        dxSoundResume(dxSound);
    }
    if (wasapiSound) {
        wasapiSoundResume(wasapiSound);
    }
    if (aviSound) {
        aviSoundResume(aviSound);
    }
}


void archSoundSuspend(void)
{
    if (dxSound) {
        dxSoundSuspend(dxSound);
    }
    if (wasapiSound) {
        wasapiSoundSuspend(wasapiSound);
    }
    if (aviSound) {
        aviSoundSuspend(aviSound);
    }
}
