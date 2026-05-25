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
/* openMSX Y8950 ADPCM port; deps stubbed in OpenMsxY8950Latest.h. */

#include "OpenMsxY8950LatestAdpcm.h"
#include "OpenMsxY8950Latest.h"

extern "C" {
#include "../Utils/SaveState.h"
}

#include <algorithm>
#include <array>
#include <cassert>

namespace openmsx {

static constexpr int R07_RESET       = 0x01;
static constexpr int R07_SP_OFF      = 0x08;
static constexpr int R07_REPEAT      = 0x10;
static constexpr int R07_MEMORY_DATA = 0x20;
static constexpr int R07_REC         = 0x40;
static constexpr int R07_START       = 0x80;
static constexpr int R07_MODE        = 0xE0;

static constexpr int R08_ROM         = 0x01;
static constexpr int R08_64K         = 0x02;
static constexpr int R08_DA_AD       = 0x04;
static constexpr int R08_SAMPL       = 0x08;
static constexpr int R08_NOTE_SET    = 0x40;
static constexpr int R08_CSM         = 0x80;

static constexpr int DIFF_MAX     = 0x6000;
static constexpr int DIFF_MIN     = 0x7F;
static constexpr int DIFF_DEFAULT = 0x7F;

static constexpr int STEP_BITS = 16;
static constexpr int STEP_MASK = (1 << STEP_BITS) - 1;


Y8950Adpcm::Y8950Adpcm(Y8950& y8950_, const DeviceConfig& config,
                       const std::string& name, unsigned sampleRam)
    : Schedulable(config.getScheduler())
    , y8950(y8950_)
    , ram(config, name + " RAM", "Y8950 sample RAM", sampleRam)
    , clock(config.getMotherBoard().getCurrentTime())
    , emu{}
    , aud{}
    , startAddr(0), stopAddr(7), addrMask((1 << 18) - 1)
    , volumeWStep(0), readDelay(0), delta(0)
    , reg7(0), reg15(0), romBank(false)
{
    clearRam();
}

void Y8950Adpcm::clearRam()
{
    ram.clear(0xFF);
}

void Y8950Adpcm::reset(EmuTime time)
{
    removeSyncPoint();

    clock.reset(time);

    startAddr = 0;
    stopAddr = 7;
    delta = 0;
    addrMask = (1 << 18) - 1;
    reg7 = 0;
    reg15 = 0;
    readDelay = 0;
    romBank = false;
    writeReg(0x12, 255, time);

    restart(emu);
    restart(aud);

    y8950.setStatus(Y8950::STATUS_BUF_RDY);
}

bool Y8950Adpcm::isPlaying() const
{
    return (reg7 & 0xC0) == 0x80;
}

bool Y8950Adpcm::isMuted() const
{
    return !isPlaying() || (reg7 & R07_SP_OFF);
}

void Y8950Adpcm::restart(PlayData& pd) const
{
    pd.memPtr = startAddr;
    pd.nowStep = (1 << STEP_BITS) - delta;
    pd.out = 0;
    pd.output = 0;
    pd.diff = DIFF_DEFAULT;
    pd.nextLeveling = 0;
    pd.sampleStep = 0;
    pd.adpcm_data = 0;
}

void Y8950Adpcm::sync(EmuTime time)
{
    if (isPlaying()) {
        unsigned ticks = clock.getTicksTill(time);
        for (unsigned i = 0; isPlaying() && (i < ticks); ++i) {
            (void)calcSample(true);
        }
    }
    clock.advance(time);
}

void Y8950Adpcm::schedule()
{
    /* In the openMSX integration this would set a sync-point so the
    ** scheduler fires executeUntil() at end-of-sample.  Our Schedulable
    ** stub has no scheduler, so EOS is detected lazily during sync()
    ** instead.  Accuracy is good enough for the audio thread; cycle-
    ** exact IRQ timing is not preserved. */
}

void Y8950Adpcm::executeUntil(EmuTime time)
{
    sync(time);
    if (isPlaying() && (reg7 & R07_REPEAT)) {
        schedule();
    }
}

void Y8950Adpcm::writeReg(uint8_t rg, uint8_t data, EmuTime time)
{
    sync(time);
    switch (rg) {
    case 0x07:
        reg7 = data;
        if (reg7 & R07_START) {
            y8950.setStatus(Y8950::STATUS_PCM_BSY);
        } else {
            y8950.resetStatus(Y8950::STATUS_PCM_BSY);
        }
        if (reg7 & R07_RESET) {
            reg7 = 0;
        }
        if (reg7 & R07_START) {
            restart(emu);
            restart(aud);
        }
        if (reg7 & R07_MEMORY_DATA) {
            emu.memPtr = startAddr;
            aud.memPtr = startAddr;
            readDelay = 2;
            if ((reg7 & 0xA0) == 0x20) {
                y8950.setStatus(Y8950::STATUS_BUF_RDY);
            }
        } else {
            emu.memPtr = 0;
            aud.memPtr = 0;
        }
        removeSyncPoint();
        if (isPlaying()) schedule();
        break;

    case 0x08:
        romBank = data & R08_ROM;
        addrMask = data & R08_64K ? (1 << 16) - 1 : (1 << 18) - 1;
        break;

    case 0x09: startAddr = (startAddr & 0x7F807) | (data << 3);  break;
    case 0x0A: startAddr = (startAddr & 0x007FF) | (data << 11); break;

    case 0x0B:
        stopAddr = (stopAddr & 0x7F807) | (data << 3);
        if (isPlaying()) { removeSyncPoint(); schedule(); }
        break;
    case 0x0C:
        stopAddr = (stopAddr & 0x007FF) | (data << 11);
        if (isPlaying()) { removeSyncPoint(); schedule(); }
        break;

    case 0x0F: writeData(data); break;

    case 0x10:
        delta = (delta & 0xFF00) | data;
        volumeWStep = (volume * delta) >> STEP_BITS;
        if (isPlaying()) { removeSyncPoint(); schedule(); }
        break;
    case 0x11:
        delta = (delta & 0x00FF) | (data << 8);
        volumeWStep = (volume * delta) >> STEP_BITS;
        if (isPlaying()) { removeSyncPoint(); schedule(); }
        break;

    case 0x12:
        volume = data;
        volumeWStep = (volume * delta) >> STEP_BITS;
        break;

    case 0x0D: case 0x0E:
    case 0x15: case 0x16: case 0x17:
    case 0x1A:
        break;
    }
}

void Y8950Adpcm::writeData(uint8_t data)
{
    reg15 = data;
    if ((reg7 & R07_MODE) == 0x60) {
        if (readDelay) {
            emu.memPtr = startAddr;
            readDelay = 0;
        }
        if (emu.memPtr <= stopAddr) {
            writeMemory(emu.memPtr, data);
            emu.memPtr += 2;

            y8950.resetStatus(Y8950::STATUS_BUF_RDY);
            y8950.setStatus(Y8950::STATUS_BUF_RDY);

            if (emu.memPtr > stopAddr) {
                y8950.setStatus(Y8950::STATUS_EOS);
                emu.memPtr = startAddr;
            }
        }
    } else if ((reg7 & R07_MODE) == 0x80) {
        y8950.resetStatus(Y8950::STATUS_BUF_RDY);
    }
}

uint8_t Y8950Adpcm::readReg(uint8_t rg, EmuTime time)
{
    sync(time);
    return (rg == 0x0F) ? readData() : peekReg(rg);
}

uint8_t Y8950Adpcm::peekReg(uint8_t rg, EmuTime time) const
{
    const_cast<Y8950Adpcm*>(this)->sync(time);
    return peekReg(rg);
}

uint8_t Y8950Adpcm::peekReg(uint8_t rg) const
{
    switch (rg) {
    case 0x0F: return peekData();
    case 0x13: return uint8_t((emu.output >>  8) & 0xFF);
    case 0x14: return uint8_t((emu.output >> 16) & 0xFF);
    default:   return 255;
    }
}

void Y8950Adpcm::resetStatus()
{
    if (((reg7 & R07_MODE & ~R07_REC) == R07_MEMORY_DATA) ||
        ((reg7 & R07_MODE) == 0)) {
        y8950.setStatus(Y8950::STATUS_BUF_RDY);
    }
}

uint8_t Y8950Adpcm::readData()
{
    if ((reg7 & R07_MODE) == R07_MEMORY_DATA) {
        if (readDelay) {
            emu.memPtr = startAddr;
        }
    }
    uint8_t result = peekData();
    if ((reg7 & R07_MODE) == R07_MEMORY_DATA) {
        if (readDelay) {
            --readDelay;
            y8950.setStatus(Y8950::STATUS_BUF_RDY);
        } else if (emu.memPtr > stopAddr) {
            y8950.setStatus(Y8950::STATUS_EOS);
        } else {
            emu.memPtr += 2;
            y8950.resetStatus(Y8950::STATUS_BUF_RDY);
            y8950.setStatus(Y8950::STATUS_BUF_RDY);
        }
    }
    return result;
}

uint8_t Y8950Adpcm::peekData() const
{
    if ((reg7 & R07_MODE) == R07_MEMORY_DATA) {
        if (readDelay) {
            return reg15;
        } else if (emu.memPtr > stopAddr) {
            return 0;
        } else {
            return readMemory(emu.memPtr);
        }
    } else {
        return 0;
    }
}

void Y8950Adpcm::writeMemory(unsigned memPtr, uint8_t value)
{
    unsigned addr = (memPtr / 2) & addrMask;
    if ((addr < ram.size()) && !romBank) {
        ram.write(addr, value);
    }
}

uint8_t Y8950Adpcm::readMemory(unsigned memPtr) const
{
    unsigned addr = (memPtr / 2) & addrMask;
    if (romBank || (addr >= ram.size())) {
        return 0;
    } else {
        return ram[addr];
    }
}

int Y8950Adpcm::calcSample()
{
    if (!isPlaying()) return 0;
    int output = calcSample(false);
    return (reg7 & R07_SP_OFF) ? 0 : output;
}
	
int Y8950Adpcm::calcSample(bool doEmu)
{
    static constexpr std::array<int, 16> F1 = {
         1,   3,   5,   7,   9,  11,  13,  15,
        -1,  -3,  -5,  -7,  -9, -11, -13, -15
    };
    static constexpr std::array<int, 16> F2 = {
        57,  57,  57,  57,  77, 102, 128, 153,
        57,  57,  57,  57,  77, 102, 128, 153
    };

    PlayData& pd = doEmu ? emu : aud;
    pd.nowStep += delta;
    if (pd.nowStep & ~STEP_MASK) {
        pd.nowStep &= STEP_MASK;
        uint8_t val;
        if (!(pd.memPtr & 1)) {
            if (reg7 & R07_MEMORY_DATA) {
                pd.adpcm_data = readMemory(pd.memPtr);
            } else {
                pd.adpcm_data = reg15;
                if (doEmu) y8950.setStatus(Y8950::STATUS_BUF_RDY);
            }
            val = uint8_t(pd.adpcm_data >> 4);
        } else {
            val = uint8_t(pd.adpcm_data & 0x0F);
        }
        int prevOut = pd.out;
        pd.out = Math::clipToInt16(pd.out + (pd.diff * F1[val]) / 8);
        pd.diff = std::clamp((pd.diff * F2[val]) / 64, DIFF_MIN, DIFF_MAX);
		
        int prevLeveling = pd.nextLeveling;
        pd.nextLeveling = (prevOut + pd.out) / 2;
        int deltaLeveling = pd.nextLeveling - prevLeveling;
        pd.sampleStep = deltaLeveling * volumeWStep;
        int tmp = deltaLeveling * ((volume * int(pd.nowStep)) >> STEP_BITS);
        pd.output = prevLeveling * volume + tmp;

        ++pd.memPtr;
        if ((reg7 & R07_MEMORY_DATA) && (pd.memPtr > stopAddr)) {
            if (doEmu) y8950.setStatus(Y8950::STATUS_EOS);
            if (reg7 & R07_REPEAT) {
                restart(pd);
            } else if (doEmu) {
                removeSyncPoint();
                reg7 = 0;
            }
        }
    } else {
        pd.output += pd.sampleStep;
    }
    return pd.output >> 12;
}

void Y8950Adpcm::blueMsxSaveStateImpl(SaveState* s)
{
    /* RAM persists in dispatcher's y8950_adpcm_ram section, not here. */
    saveStateSet(s, "adpcm_startAddr",   (UInt32)startAddr);
    saveStateSet(s, "adpcm_stopAddr",    (UInt32)stopAddr);
    saveStateSet(s, "adpcm_addrMask",    (UInt32)addrMask);
    saveStateSet(s, "adpcm_volume",      (UInt32)volume);
    saveStateSet(s, "adpcm_volumeWStep", (UInt32)volumeWStep);
    saveStateSet(s, "adpcm_readDelay",   (UInt32)readDelay);
    saveStateSet(s, "adpcm_delta",       (UInt32)delta);
    saveStateSet(s, "adpcm_reg7",        (UInt32)reg7);
    saveStateSet(s, "adpcm_reg15",       (UInt32)reg15);
    saveStateSet(s, "adpcm_romBank",     (UInt32)romBank);

    saveStateSet(s, "adpcm_emu_memPtr",       (UInt32)emu.memPtr);
    saveStateSet(s, "adpcm_emu_nowStep",      (UInt32)emu.nowStep);
    saveStateSet(s, "adpcm_emu_out",          (UInt32)emu.out);
    saveStateSet(s, "adpcm_emu_output",       (UInt32)emu.output);
    saveStateSet(s, "adpcm_emu_diff",         (UInt32)emu.diff);
    saveStateSet(s, "adpcm_emu_nextLeveling", (UInt32)emu.nextLeveling);
    saveStateSet(s, "adpcm_emu_sampleStep",   (UInt32)emu.sampleStep);
    saveStateSet(s, "adpcm_emu_adpcm_data",   (UInt32)emu.adpcm_data);
}

void Y8950Adpcm::blueMsxLoadStateImpl(SaveState* s)
{
    startAddr   = saveStateGet(s, "adpcm_startAddr",   startAddr);
    stopAddr    = saveStateGet(s, "adpcm_stopAddr",    stopAddr);
    addrMask    = saveStateGet(s, "adpcm_addrMask",    addrMask);
    volume      = (int)saveStateGet(s, "adpcm_volume",      volume);
    volumeWStep = (int)saveStateGet(s, "adpcm_volumeWStep", volumeWStep);
    readDelay   = (int)saveStateGet(s, "adpcm_readDelay",   readDelay);
    delta       = (int)saveStateGet(s, "adpcm_delta",       delta);
    reg7        = (uint8_t)saveStateGet(s, "adpcm_reg7",  reg7);
    reg15       = (uint8_t)saveStateGet(s, "adpcm_reg15", reg15);
    romBank     = (bool)saveStateGet(s, "adpcm_romBank",  romBank);

    emu.memPtr       = saveStateGet(s, "adpcm_emu_memPtr",       emu.memPtr);
    emu.nowStep      = saveStateGet(s, "adpcm_emu_nowStep",      emu.nowStep);
    emu.out          = (int)saveStateGet(s, "adpcm_emu_out",          emu.out);
    emu.output       = (int)saveStateGet(s, "adpcm_emu_output",       emu.output);
    emu.diff         = (int)saveStateGet(s, "adpcm_emu_diff",         emu.diff);
    emu.nextLeveling = (int)saveStateGet(s, "adpcm_emu_nextLeveling", emu.nextLeveling);
    emu.sampleStep   = (int)saveStateGet(s, "adpcm_emu_sampleStep",   emu.sampleStep);
    emu.adpcm_data   = (uint8_t)saveStateGet(s, "adpcm_emu_adpcm_data", emu.adpcm_data);

    /* Audio-side PlayData is a 1:1 mirror of the emu side at load. */
    aud = emu;
}

void Y8950Adpcm::blueMsxCopyRamTo(uint8_t* dst, unsigned len) const
{
    unsigned n = ram.size();
    if (len < n) n = len;
    for (unsigned i = 0; i < n; i++) dst[i] = ram[i];
}

unsigned Y8950Adpcm::blueMsxRamSize() const
{
    return ram.size();
}

void Y8950Adpcm::blueMsxCopyRamFrom(const uint8_t* src, unsigned len)
{
    unsigned n = ram.size();
    if (len < n) n = len;
    for (unsigned i = 0; i < n; i++) ram.write(i, src[i]);
}

} // namespace openmsx
