/*****************************************************************************
**
** Nuked-OPLL YM2413 (OPLL) emulator backend.
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
#ifndef NUKED_OPLL_BACKEND_H
#define NUKED_OPLL_BACKEND_H

#include "OpenMsxYM2413_2.h"

class NukedOpllBackend : public OpenYM2413Base
{
public:
    NukedOpllBackend(const std::string& name, short volume, const EmuTime& time);
    virtual ~NukedOpllBackend();

    virtual void reset(const EmuTime& time);
    virtual void writeReg(byte r, byte v, const EmuTime& time);
    virtual byte peekReg(byte r);

    virtual void setInternalVolume(short newVolume);
    virtual int* updateBuffer(int length);
    virtual void setSampleRate(int sampleRate, int oversampling);

    virtual void loadState();
    virtual void saveState();

private:
    /* Clock the chip once, mix its DAC sample, and feed RateConv every
    ** 18 cycles.  Shared by writeReg and updateBuffer. */
    void clockAndFeed();

    void*   chipMem;       /* opll_t* */
    void*   convMem;       /* OPLL_RateConv* borrowed from EMU2413 */
    short   maxVolume;
    int     sampleRate;
    double  f_inp;         /* chip frame rate (49716 Hz) */
    double  f_out;         /* output sample rate */
    double  outTime;       /* RateConv timing accumulator */
    int     cyclesInFrame; /* 0..17, advances each clockAndFeed */
    int32_t frameSumM;
    int32_t frameSumR;
    int     buffer[AUDIO_MONO_BUFFER_SIZE];
    byte    regCache[64];
};

#endif
