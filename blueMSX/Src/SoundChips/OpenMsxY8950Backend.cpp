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
#include "OpenMsxY8950Backend.h"
#include "OpenMsxY8950Latest.h"

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "Board.h"
#include "../Utils/SaveState.h"
}

#define FREQUENCY  3579545
#define SAMPLERATE (FREQUENCY / 72)

/* Per-instance state: openMSX chip + scratch sample buffer hidden away
** so the header does not have to pull in the full openMSX namespace. */
struct OpenMsxY8950InstState {
    openmsx::MSXMotherBoard mb;
    openmsx::DeviceConfig   cfg;
    openmsx::MSXAudio       audio;
    openmsx::Y8950          chip;
    std::vector<int32_t>    chipSamples;

    OpenMsxY8950InstState(unsigned sampleRam, openmsx::EmuTime time)
        : cfg(mb), chip("Y8950", cfg, sampleRam, time, audio)
    {}
};

OpenMsxY8950Backend::OpenMsxY8950Backend()
    : inst(NULL), mixerRate(SAMPLERATE), off(0), o1(0), o2(0), latchedAddr(0)
{
    memset(buffer, 0, sizeof(buffer));
    memset(regCache, 0, sizeof(regCache));
    inst = new OpenMsxY8950InstState(256 * 1024, (openmsx::EmuTime)boardSystemTime());
    /* Keep upstream's setVolume convention: 32767 * 9/10 ~= Fmopl's
    ** legacy headroom.  The wrapper applies an additional << 1 below
    ** to match Fmopl/emu8950 RMS. */
    inst->chip.setVolume(32767 * 9 / 10);
}

OpenMsxY8950Backend::~OpenMsxY8950Backend()
{
    delete inst;
}

void OpenMsxY8950Backend::reset()
{
    inst->chip.reset((openmsx::EmuTime)boardSystemTime());
    off = 0;
    o1  = 0;
    o2  = 0;
}

void OpenMsxY8950Backend::setSampleRate(UInt32 rate)
{
    mixerRate = rate;
    /* The port runs at chip rate (clock/72 = 49716 Hz); we resample to
    ** mixer rate here in updateBuffer. */
}

Int32* OpenMsxY8950Backend::updateBuffer(UInt32 count)
{
    /* Pre-simulate the linear-resampler off-counter loop so we can ask
    ** the chip for the exact sample count it must advance.  Over-
    ** fetching would turn voices into high-pitched buzz. */
    Int32 simOff = off;
    int chipNeed;
    if (SAMPLERATE > mixerRate) {
        chipNeed = 0;
        for (UInt32 i = 0; i < count; i++) {
            simOff -= SAMPLERATE - (Int32)mixerRate;
            chipNeed++;
            if (simOff < 0) {
                simOff += (Int32)mixerRate;
                chipNeed++;
            }
        }
    }
    else {
        chipNeed = (int)count;
    }

    if ((int)inst->chipSamples.size() < chipNeed) inst->chipSamples.resize((size_t)chipNeed);
    inst->chip.generateMono(inst->chipSamples.data(), (unsigned)chipNeed);
    int32_t* chipBuf = inst->chipSamples.data();
    int chipPos = 0;

    for (UInt32 i = 0; i < count; i++) {
        if (SAMPLERATE > mixerRate) {
            off -= SAMPLERATE - (Int32)mixerRate;
            o1 = o2;
            o2 = chipBuf[chipPos++] << 1;
            if (off < 0) {
                off += (Int32)mixerRate;
                o1 = o2;
                o2 = chipBuf[chipPos++] << 1;
            }
            buffer[i] = (o1 * (off / 256) + o2 * (((Int32)SAMPLERATE - off) / 256)) / (SAMPLERATE / 256);
        }
        else {
            buffer[i] = chipBuf[chipPos++] << 1;
        }
    }
    return buffer;
}

void OpenMsxY8950Backend::writeIo(int port, UInt8 value)
{
    if ((port & 1) == 0) {
        latchedAddr = value;
    }
    else {
        regCache[latchedAddr] = value;
        inst->chip.writeReg(latchedAddr, value, (openmsx::EmuTime)boardSystemTime());
    }
}

UInt8 OpenMsxY8950Backend::readIo(int port)
{
    if ((port & 1) == 0) {
        /* Build status from regCache mask so dispatcher-driven T1/T2
        ** fires are visible even when chip's own statusMask is 0. */
        UInt8 raw     = (UInt8)inst->chip.peekRawStatus();
        UInt8 maskInv = (UInt8)(~regCache[0x04] & 0x78);
        UInt8 vis     = (UInt8)(raw & maskInv);
        return (UInt8)((vis ? (vis | 0x80) : 0) | 0x06);
    }
    return regCache[latchedAddr];
}

UInt8 OpenMsxY8950Backend::peekIo(int port)
{
    return readIo(port);
}

UInt8 OpenMsxY8950Backend::readReg(int reg)
{
    return regCache[reg & 0xff];
}

void OpenMsxY8950Backend::saveState()
{
    SaveState* s = saveStateOpenForWrite("openmsxY8950");
    saveStateSet      (s, "hasChip",    1);
    saveStateSet      (s, "latchedAddr", latchedAddr);
    saveStateSetBuffer(s, "regCache",   regCache, sizeof(regCache));
    inst->chip.blueMsxSaveStateImpl(s);
    saveStateClose(s);
}

void OpenMsxY8950Backend::loadState()
{
    SaveState* s = saveStateOpenForRead("openmsxY8950");
    if (!saveStateGet(s, "hasChip", 0)) {
        saveStateClose(s);
        reset();
        loadHadOwnState_ = false;
        return;
    }
    loadHadOwnState_ = true;
    latchedAddr = (UInt8)saveStateGet(s, "latchedAddr", 0);
    saveStateGetBuffer(s, "regCache", regCache, sizeof(regCache));
    inst->chip.blueMsxLoadStateImpl(s);
    saveStateClose(s);
}

const UInt8* OpenMsxY8950Backend::getAdpcmRam(UInt32* size_out)
{
    size_t live = inst->chip.blueMsxAdpcmRamSize();
    UInt32 n = (UInt32)live;
    if (n > sizeof(adpcmRamCache)) n = sizeof(adpcmRamCache);
    inst->chip.blueMsxAdpcmCopyRamTo(adpcmRamCache, n);
    if (size_out) *size_out = n;
    return adpcmRamCache;
}

void OpenMsxY8950Backend::copyAdpcmRamFrom(const UInt8* src, UInt32 len)
{
    inst->chip.blueMsxAdpcmCopyRamFrom(src, (size_t)len);
}

void OpenMsxY8950Backend::onTimerOverflow(int timer_idx)
{
    uint8_t bit = (timer_idx == 0) ? 0x40 : 0x20;
    inst->chip.setStatus(bit);
    if (!(regCache[0x04] & bit)) {
        boardSetInt(0x10);
    }
}
