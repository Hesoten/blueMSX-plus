/*****************************************************************************
**
** Y8960 cartridge - the OPL2EX block: a YM3812 + ADPCM-B circuit with the
** cartridge's sample RAM.
**
** Copyright (C) 2026 madscient
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
#include "Y8960Opl2ex.h"
#include "Y8960Opl2Core.h"
#include "Y8960Pan.h"

extern "C" {
#include "Board.h"
#include "romMapperY8960.h"
#include "SaveState.h"
#include "DebugDeviceManager.h"
#include "Language.h"
}

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define FREQUENCY  3579545
#define SAMPLERATE (FREQUENCY / 72)

/* 256kB, the whole of the cartridge's sample RAM. */
#define Y8960_OPL2EX_SAMPLE_RAM (256 * 1024)

/* The same << 1 the machine's own Y8950 port applies, so the block lands where
** the chip it stands in for would. The balance against the other Y8960 blocks
** is set on top of it, in the mixer block's gain (romMapperY8960Mixer.c). */
#define Y8960_OPL2EX_GAIN_SHIFT 1

struct Y8960Opl2ex {
    Mixer*                          mixer;
    y8960opl2::MSXMotherBoard       mb;
    y8960opl2::DeviceConfig         cfg;
    y8960opl2::SampleRam            ram;
    y8960opl2::Y8950                chip;
    std::vector<int32_t>            chipSamples;
    Int32                           handle;
    Int32                           debugHandle;
    UInt32                          mixerRate;
    Int32                           off, o1[2], o2[2];
    UInt8                           latchedAddr;
    UInt8                           regCache[256];
    Int32                           buffer[AUDIO_STEREO_BUFFER_SIZE];

    Y8960Opl2ex(Mixer* mixer_, const std::string& name, y8960opl2::EmuTime time)
        : mixer(mixer_), cfg(mb), ram(Y8960_OPL2EX_SAMPLE_RAM)
        , chip(name, cfg, ram, BOARD_INT_Y8960_OPL2EX, time)
        , handle(0), debugHandle(0), mixerRate(SAMPLERATE)
        , off(0), o1{0, 0}, o2{0, 0}, latchedAddr(0)
    {
        memset(regCache, 0, sizeof(regCache));
        memset(buffer,   0, sizeof(buffer));
    }
};

/* The chip runs at clock/72 whatever the mixer wants, so the extra samples are
** resampled here. Asking the chip for more than the loop consumes turns voices
** into buzz, so the count is worked out before the chip is advanced. Stereo,
** because the pan registers place each channel. */
static Int32 scaled(int32_t sample, Int64 gain)
{
    return (Int32)(((Int64)sample << Y8960_OPL2EX_GAIN_SHIFT) * gain / Y8960_GAIN_ONE);
}

static Int32* sync(void* ref, UInt32 count)
{
    Y8960Opl2ex& c = *(Y8960Opl2ex*)ref;
    Int32 simOff = c.off;
    int chipNeed;
    /* The mixer's OPL2 volume covers the ADPCM too, which the hardware
    ** mixes into the OPL2 first. */
    Int64 gain = y8960MixerGain(Y8960_VOL_OPL2);

    if (SAMPLERATE > c.mixerRate) {
        chipNeed = 0;
        for (UInt32 i = 0; i < count; i++) {
            simOff -= SAMPLERATE - (Int32)c.mixerRate;
            chipNeed++;
            if (simOff < 0) {
                simOff += (Int32)c.mixerRate;
                chipNeed++;
            }
        }
    }
    else {
        chipNeed = (int)count;
    }

    if ((int)c.chipSamples.size() < 2 * chipNeed) c.chipSamples.resize((size_t)(2 * chipNeed));
    c.chip.generateStereo(c.chipSamples.data(), (unsigned)chipNeed);

    int32_t* chipBuf = c.chipSamples.data();
    int chipPos = 0;

    for (UInt32 i = 0; i < count; i++) {
        if (SAMPLERATE > c.mixerRate) {
            c.off -= SAMPLERATE - (Int32)c.mixerRate;
            for (int side = 0; side < 2; side++) {
                c.o1[side] = c.o2[side];
                c.o2[side] = scaled(chipBuf[2 * chipPos + side], gain);
            }
            chipPos++;
            if (c.off < 0) {
                c.off += (Int32)c.mixerRate;
                for (int side = 0; side < 2; side++) {
                    c.o1[side] = c.o2[side];
                    c.o2[side] = scaled(chipBuf[2 * chipPos + side], gain);
                }
                chipPos++;
            }
            for (int side = 0; side < 2; side++) {
                c.buffer[2 * i + side] = (c.o1[side] * (c.off / 256)
                                       +  c.o2[side] * (((Int32)SAMPLERATE - c.off) / 256)) / (SAMPLERATE / 256);
            }
        }
        else {
            c.buffer[2 * i + 0] = scaled(chipBuf[2 * chipPos + 0], gain);
            c.buffer[2 * i + 1] = scaled(chipBuf[2 * chipPos + 1], gain);
            chipPos++;
        }
    }

    return c.buffer;
}

static void setRate(void* ref, UInt32 rate)
{
    Y8960Opl2ex& c = *(Y8960Opl2ex*)ref;
    c.mixerRate = rate ? rate : SAMPLERATE;
}

static void getDebugInfo(void* ref, DbgDevice* dbgDevice)
{
    Y8960Opl2ex& c = *(Y8960Opl2ex*)ref;
    DbgRegisterBank* regBank;
    int r;

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegs(), 256);

    for (r = 0; r < 256; r++) {
        char name[8];
        sprintf(name, "R%.2x", r);
        dbgRegisterBankAddRegister(regBank, r, name, 8, c.regCache[r]);
    }
}

extern "C" {

void y8960Opl2exReset(Y8960Opl2ex* c)
{
    c->chip.reset((y8960opl2::EmuTime)boardSystemTime());
    c->off = 0;
    c->o1[0] = c->o1[1] = 0;
    c->o2[0] = c->o2[1] = 0;
    c->latchedAddr = 0;
    memset(c->regCache, 0, sizeof(c->regCache));
}

void y8960Opl2exWrite(Y8960Opl2ex* c, int port, UInt8 value)
{
    mixerSync(c->mixer);

    if ((port & 1) == 0) {
        c->latchedAddr = value;
    }
    else {
        c->regCache[c->latchedAddr] = value;
        c->chip.writeReg(c->latchedAddr, value, (y8960opl2::EmuTime)boardSystemTime());
    }
}

/* Unlike a Y8950, the data port reads back only the ADPCM data register
** (0Fh); every other register reads FFh. The debugger still shows the written
** values, from regCache. */
UInt8 y8960Opl2exRead(Y8960Opl2ex* c, int port)
{
    /* A read brings the ADPCM up to date, and that can end a sample. What
    ** the mixer has not been given by then would come out silent. */
    mixerSync(c->mixer);

    if ((port & 1) == 0) {
        return c->chip.readStatus((y8960opl2::EmuTime)boardSystemTime());
    }
    if (c->latchedAddr != 0x0F) {
        return 0xFF;
    }
    return c->chip.readReg(c->latchedAddr, (y8960opl2::EmuTime)boardSystemTime());
}

UInt8 y8960Opl2exPeek(Y8960Opl2ex* c, int port)
{
    if ((port & 1) == 0) {
        return c->chip.peekStatus((y8960opl2::EmuTime)boardSystemTime());
    }
    if (c->latchedAddr != 0x0F) {
        return 0xFF;
    }
    return c->chip.peekReg(c->latchedAddr, (y8960opl2::EmuTime)boardSystemTime());
}

/* One section for the circuit and one for the sample RAM. */
void y8960Opl2exSaveState(Y8960Opl2ex* c)
{
    {
        SaveState* s = saveStateOpenForWrite("y8960opl2ex");
        saveStateSet      (s, "latchedAddr", c->latchedAddr);
        saveStateSetBuffer(s, "regCache",    c->regCache, sizeof(c->regCache));
        c->chip.blueMsxSaveStateImpl(s);
        saveStateClose(s);
    }
    {
        SaveState* s = saveStateOpenForWrite("y8960opl2ram");
        saveStateSetBuffer(s, "ram", c->ram.raw(), c->ram.size());
        saveStateClose(s);
    }
}

void y8960Opl2exLoadState(Y8960Opl2ex* c)
{
    {
        SaveState* s = saveStateOpenForRead("y8960opl2ex");
        c->latchedAddr = (UInt8)saveStateGet(s, "latchedAddr", 0);
        saveStateGetBuffer(s, "regCache", c->regCache, sizeof(c->regCache));
        c->chip.blueMsxLoadStateImpl(s);
        saveStateClose(s);
    }
    {
        SaveState* s = saveStateOpenForRead("y8960opl2ram");
        saveStateGetBuffer(s, "ram", c->ram.raw(), c->ram.size());
        saveStateClose(s);
    }
}

Y8960Opl2ex* y8960Opl2exCreate(Mixer* mixer)
{
    static const char* const name = "Y8960 OPL2EX";
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    Y8960Opl2ex* c = new Y8960Opl2ex(mixer, name, (y8960opl2::EmuTime)boardSystemTime());

    c->mixerRate   = mixerGetSampleRate(mixer);
    c->handle      = mixerRegisterChannel(mixer, MIXER_CHANNEL_Y8960, 1, sync, setRate, c);
    c->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, name, &dbgCallbacks, c);

    y8960Opl2exReset(c);

    return c;
}

void y8960Opl2exDestroy(Y8960Opl2ex* c)
{
    debugDeviceUnregister(c->debugHandle);
    mixerUnregisterChannel(c->mixer, c->handle);

    delete c;
}

} // extern "C"
