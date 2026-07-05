/*****************************************************************************
**
** openMSX-derived Y8950 (MSX-Audio + ADPCM) emulator backend.
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
#ifndef OPENMSX_Y8950_BACKEND_H
#define OPENMSX_Y8950_BACKEND_H

#include "Y8950BackendBase.h"
extern "C" {
#include "AudioMixer.h"
}

struct OpenMsxY8950InstState;

class OpenMsxY8950Backend : public Y8950BackendBase
{
public:
    OpenMsxY8950Backend();
    virtual ~OpenMsxY8950Backend();

    virtual void   reset();
    virtual void   setSampleRate(UInt32 rate);
    virtual Int32* updateBuffer(UInt32 count);

    virtual void   writeIo(int port, UInt8 value);
    virtual UInt8  readIo(int port);
    virtual UInt8  peekIo(int port);
    virtual UInt8  readReg(int reg);

    virtual void   saveState();
    virtual void   loadState();

    virtual const UInt8* getAdpcmRam(UInt32* size_out);
    virtual void   copyAdpcmRamFrom(const UInt8* src, UInt32 len);

    virtual void   onTimerOverflow(int timer_idx);

private:
    OpenMsxY8950InstState* inst;
    UInt32 mixerRate;
    Int32  off, o1, o2;
    Int32  buffer[AUDIO_MONO_BUFFER_SIZE];
    UInt8  latchedAddr;
    UInt8  regCache[256];
    UInt8  adpcmRamCache[256 * 1024];  /* lazy snapshot for getAdpcmRam */
};

#endif
