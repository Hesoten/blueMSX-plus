/*****************************************************************************
**
** Ported from openMSX's latest Y8950 ADPCM; the upstream source
** is kept under OpenMsxY8950Latest/ as the reference.
** The openMSX file traces back to emu8950.c by Mitsutaka Okazaki
** (2001) via openMSX's heavy rewrite.
** Upstream openMSX is distributed under the GNU GPL v2 or later.
**
** Copyright (C) 2026 Hesoten (blueMSX port)
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
/* Latest openMSX Y8950 ADPCM port for blueMSX.
**
** Adapted from openMSX commit ~2024:
**   src/sound/Y8950Adpcm.hh + Y8950Adpcm.cc
**
** Schedulable / TrackedRam / Clock are stubbed in OpenMsxY8950Latest.h. */

#ifndef OPENMSX_Y8950_LATEST_ADPCM_HH
#define OPENMSX_Y8950_LATEST_ADPCM_HH

#include "OpenMsxY8950Latest.h"

namespace openmsx {

class Y8950Adpcm final : public Schedulable
{
public:
    Y8950Adpcm(Y8950& y8950, const DeviceConfig& config,
               const std::string& name, unsigned sampleRam);
	
    void clearRam();
    void reset(EmuTime time);
    bool isMuted() const;
    void writeReg(uint8_t rg, uint8_t data, EmuTime time);
    uint8_t readReg(uint8_t rg, EmuTime time);
    uint8_t peekReg(uint8_t rg, EmuTime time) const;
    int  calcSample();
    void sync(EmuTime time);
    void resetStatus();
    
    /* blueMSX addition: see OpenMsxY8950Latest.h for the savestate scheme. */
    void blueMsxSaveStateImpl(::SaveState* state);
    void blueMsxLoadStateImpl(::SaveState* state);
    void blueMsxCopyRamFrom(const uint8_t* src, unsigned len);
    void blueMsxCopyRamTo(uint8_t* dst, unsigned len) const;
    unsigned blueMsxRamSize() const;

private:
    struct PlayData {
        unsigned memPtr;
        unsigned nowStep;
        int out;
        int output;
        int diff;
        int nextLeveling;
        int sampleStep;
        uint8_t adpcm_data;
    };

    void executeUntil(EmuTime time) override;

    void schedule();
    void restart(PlayData& pd) const;

    bool isPlaying() const;
    void writeData(uint8_t data);
    uint8_t peekReg(uint8_t rg) const;
    uint8_t readData();
    uint8_t peekData() const;
    void writeMemory(unsigned memPtr, uint8_t value);
    uint8_t readMemory(unsigned memPtr) const;
    int  calcSample(bool doEmu);

private:
    Y8950& y8950;
    TrackedRam ram;

    static constexpr int CLOCK_FREQ     = 3579545;
    static constexpr int CLOCK_FREQ_DIV = 72;
    Clock<CLOCK_FREQ, CLOCK_FREQ_DIV> clock;

    PlayData emu;
    PlayData aud;

    unsigned startAddr;
    unsigned stopAddr;
    unsigned addrMask;
    int volume = 0;
    int volumeWStep;
    int readDelay;
    int delta;
    uint8_t reg7;
    uint8_t reg15;
    bool romBank;
};

} // namespace openmsx

#endif 
