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
/*****************************************************************************
**
** Forked for the Y8960 cartridge, 2026 by madscient.
**
** The cartridge's OPL2EX is a YM3812 with ADPCM-B bolted on, and its register
** map is the Y8950's, so this is the Y8950 port with three changes: the four
** waveforms a YM3812 has and a Y8950 does not, the MSX-AUDIO parts the
** cartridge does not carry (keyboard connector, 13 bit DAC, periphery), and a
** sample RAM that lives outside the circuit, where the block owns it.
**
** It is a fork rather than an extension because the machine's own MSX-AUDIO
** keeps using the unmodified port; both end up in one binary. The namespace
** is what keeps them apart.
**
******************************************************************************
*/
/* Latest openMSX Y8950 ADPCM port for blueMSX.
**
** Adapted from openMSX commit ~2024:
**   src/sound/Y8950Adpcm.hh + Y8950Adpcm.cc
**
** Schedulable / TrackedRam / Clock are stubbed in OpenMsxY8950Latest.h. */

#ifndef Y8960_OPL2_ADPCM_HH
#define Y8960_OPL2_ADPCM_HH

#include "Y8960Opl2Core.h"

namespace y8960opl2 {

class Y8950Adpcm final : public Schedulable
{
public:
    Y8950Adpcm(Y8950& y8950, const DeviceConfig& config,
               const std::string& name, SampleRam& sampleRam);
	
    void clearRam();
    void reset(EmuTime time);
    bool isMuted() const;
    void writeReg(uint8_t rg, uint8_t data, EmuTime time);
    uint8_t readReg(uint8_t rg, EmuTime time);
    uint8_t peekReg(uint8_t rg, EmuTime time) const;
    int  calcSample();
    void sync(EmuTime time);
    void resetStatus();
    
    /* The sample RAM is not saved here; it belongs to the block. */
    void blueMsxSaveStateImpl(::SaveState* state);
    void blueMsxLoadStateImpl(::SaveState* state);

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
    SampleRam& ram;

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

} // namespace y8960opl2

#endif 
