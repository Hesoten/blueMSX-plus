/*****************************************************************************
**
** Emu2413 YM2413 (OPLL) emulator backend.
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
/* YM2413.cpp swaps this in beside the OpenYM2413 / OpenYM2413_2
** backends without any caller-side change.
*/
#ifndef EMU2413_BACKEND_H
#define EMU2413_BACKEND_H

#include "OpenMsxYM2413_2.h"

struct __OPLL;

class Emu2413Backend : public OpenYM2413Base
{
public:
    Emu2413Backend(const std::string& name, short volume, const EmuTime& time);
    virtual ~Emu2413Backend();

    virtual void reset(const EmuTime& time);
    virtual void writeReg(byte r, byte v, const EmuTime& time);
    virtual byte peekReg(byte r);

    virtual void setInternalVolume(short newVolume);
    virtual int* updateBuffer(int length);
    virtual void setSampleRate(int sampleRate, int oversampling);

    virtual void loadState();
    virtual void saveState();

private:
    struct __OPLL* opll;
    short maxVolume;
    int   buffer[AUDIO_MONO_BUFFER_SIZE];
    byte  regCache[64];
};

#endif
