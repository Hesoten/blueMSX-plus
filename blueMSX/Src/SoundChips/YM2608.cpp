/*****************************************************************************
**
** YM2608 (OPNA) sound chip on top of ymfm.
** Copyright (C) 2026 madscient
**
** The chip itself is ymfm (BSD-3-Clause, Aaron Giles), kept unmodified
** under ymfm/. This file only connects it to the board and the mixer.
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
#include "YM2608.h"
#include "ymfm/ymfm_opn.h"
#include <string.h>
#include <stdio.h>
#include <vector>
extern "C" {
#include "Board.h"
#include "SaveState.h"
}

#define RHYTHM_ROM_SIZE 0x2000

/* ymfm at OPN_FIDELITY_MIN always emits clock / 48 samples per second,
** whatever prescaler the software selects (ymfm_opn.cpp, update_prescale). */
#define NATIVE_DIVIDER  48

/* The mixer divides by 4096 after a channel volume of at most 1024, and
** ymfm clamps FM to 16 bits; this keeps a full-scale OPNA at about half
** of the mixer's range. */
#define OUTPUT_GAIN     2

class Ym2608Host : public ymfm::ymfm_interface {
public:
    /* owner is declared before chip so that it is set before the chip's
    ** constructor can call back into this interface. */
    Ym2608Host(YM2608* owner) : owner(owner), chip(*this) {}

    void timerExpired(int tnum) { m_engine->engine_timer_expired(tnum); }

    virtual void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks);
    virtual void ymfm_set_busy_end(uint32_t clocks);
    virtual bool ymfm_is_busy();
    virtual void ymfm_update_irq(bool asserted);
    virtual uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address);
    virtual void ymfm_external_write(ymfm::access_class type, uint32_t address, uint8_t data);

    YM2608* owner;
    ymfm::ym2608 chip;
};

struct YM2608 {
    Mixer*      mixer;
    Int32       handle;
    UInt32      clock;
    UInt32      irqMask;
    UInt32      sampleRate;

    Ym2608Host* host;

    BoardTimer* timer[2];
    UInt32      timerActive[2];
    UInt32      timerTimeout[2];
    UInt32      timerExpiring[2];
    UInt32      busyEnd;
    UInt32      irqAsserted;

    /* Clock ticks against sampleRate * NATIVE_DIVIDER, so the ratio
    ** between native and mixer samples stays exact. */
    UInt32      resampleAcc;

    UInt8       rhythmRom[RHYTHM_ROM_SIZE];
    int         hasRhythmRom;
    UInt8*      adpcmRam;
    UInt32      adpcmRamSize;

    UInt8       address[2];
    UInt8       regs[2][256];

    Int32       buffer[AUDIO_STEREO_BUFFER_SIZE];
};

static UInt32 clocksToBoardTime(YM2608* ym2608, UInt32 clocks)
{
    return (UInt32)((UInt64)clocks * boardFrequency() / ym2608->clock);
}

void Ym2608Host::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks)
{
    YM2608* ym2608 = owner;

    if (duration_in_clocks < 0) {
        ym2608->timerActive[tnum] = 0;
        boardTimerRemove(ym2608->timer[tnum]);
        return;
    }
    /* A timer re-armed from its own expiry counts from the scheduled
    ** time, so a late callback does not stretch the period. */
    UInt32 base = ym2608->timerExpiring[tnum] ? ym2608->timerTimeout[tnum] : boardSystemTime();

    ym2608->timerActive[tnum]  = 1;
    ym2608->timerTimeout[tnum] = base + clocksToBoardTime(ym2608, duration_in_clocks);
    boardTimerAdd(ym2608->timer[tnum], ym2608->timerTimeout[tnum]);
}

void Ym2608Host::ymfm_set_busy_end(uint32_t clocks)
{
    owner->busyEnd = boardSystemTime() + clocksToBoardTime(owner, clocks);
}

bool Ym2608Host::ymfm_is_busy()
{
    return (Int32)(owner->busyEnd - boardSystemTime()) > 0;
}

void Ym2608Host::ymfm_update_irq(bool asserted)
{
    YM2608* ym2608 = owner;

    ym2608->irqAsserted = asserted ? 1 : 0;
    if (ym2608->irqMask == 0) {
        return;
    }
    if (asserted) {
        boardSetInt(ym2608->irqMask);
    }
    else {
        boardClearInt(ym2608->irqMask);
    }
}

uint8_t Ym2608Host::ymfm_external_read(ymfm::access_class type, uint32_t address)
{
    YM2608* ym2608 = owner;

    switch (type) {
    case ymfm::ACCESS_ADPCM_A:
        /* Nibbles 0 then 8 add and remove the smallest step, so the
        ** rhythm stays silent without the ROM. */
        if (!ym2608->hasRhythmRom) {
            return 0x08;
        }
        return ym2608->rhythmRom[address & (RHYTHM_ROM_SIZE - 1)];
    case ymfm::ACCESS_ADPCM_B:
        if (address >= ym2608->adpcmRamSize) {
            return 0xff;
        }
        return ym2608->adpcmRam[address];
    default:
        return 0xff;
    }
}

void Ym2608Host::ymfm_external_write(ymfm::access_class type, uint32_t address, uint8_t data)
{
    YM2608* ym2608 = owner;

    if (type == ymfm::ACCESS_ADPCM_B && address < ym2608->adpcmRamSize) {
        ym2608->adpcmRam[address] = data;
    }
}

static void onTimeout(YM2608* ym2608, int tnum)
{
    ym2608->timerActive[tnum] = 0;
    mixerSync(ym2608->mixer);
    ym2608->timerExpiring[tnum] = 1;
    ym2608->host->timerExpired(tnum);
    ym2608->timerExpiring[tnum] = 0;
}

static void onTimeoutA(void* ref, UInt32 time) { onTimeout((YM2608*)ref, 0); }
static void onTimeoutB(void* ref, UInt32 time) { onTimeout((YM2608*)ref, 1); }

static Int32* ym2608Sync(void* ref, UInt32 count)
{
    YM2608* ym2608 = (YM2608*)ref;
    UInt32 threshold = ym2608->sampleRate * NATIVE_DIVIDER;
    ymfm::ym2608::output_data out;
    UInt32 i;

    for (i = 0; i < count; i++) {
        Int32 left  = 0;
        Int32 right = 0;
        Int32 n     = 0;

        ym2608->resampleAcc += ym2608->clock;
        while (ym2608->resampleAcc >= threshold) {
            ym2608->resampleAcc -= threshold;
            ym2608->host->chip.generate(&out);
            left  += out.data[0] + out.data[2];
            right += out.data[1] + out.data[2];
            n++;
        }
        if (n > 0) {
            left  /= n;
            right /= n;
        }
        ym2608->buffer[2 * i + 0] = OUTPUT_GAIN * left;
        ym2608->buffer[2 * i + 1] = OUTPUT_GAIN * right;
    }

    return ym2608->buffer;
}

static void ym2608SetSampleRate(void* ref, UInt32 rate)
{
    YM2608* ym2608 = (YM2608*)ref;

    ym2608->sampleRate  = rate;
    ym2608->resampleAcc = 0;
}

extern "C" {

YM2608* ym2608Create(Mixer* mixer, UInt32 clock, UInt32 adpcmRamSize,
                     const UInt8* rhythmRom, int rhythmRomSize, UInt32 irqMask)
{
    YM2608* ym2608 = new YM2608;

    memset(ym2608->rhythmRom, 0, sizeof(ym2608->rhythmRom));
    ym2608->hasRhythmRom = rhythmRom != NULL && rhythmRomSize >= RHYTHM_ROM_SIZE;
    if (ym2608->hasRhythmRom) {
        memcpy(ym2608->rhythmRom, rhythmRom, RHYTHM_ROM_SIZE);
    }

    ym2608->mixer        = mixer;
    ym2608->clock        = clock;
    ym2608->irqMask      = irqMask;
    ym2608->adpcmRamSize = adpcmRamSize;
    ym2608->adpcmRam     = new UInt8[adpcmRamSize];
    memset(ym2608->adpcmRam, 0xff, adpcmRamSize);

    ym2608->timerExpiring[0] = 0;
    ym2608->timerExpiring[1] = 0;
    ym2608->timer[0] = boardTimerCreate(onTimeoutA, ym2608);
    ym2608->timer[1] = boardTimerCreate(onTimeoutB, ym2608);

    ym2608->host = new Ym2608Host(ym2608);
    ym2608->host->chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);

    ym2608->sampleRate = mixerGetSampleRate(mixer);
    ym2608->handle = mixerRegisterChannel(mixer, MIXER_CHANNEL_YAMAHA_SFG, 1,
                                          ym2608Sync, ym2608SetSampleRate, ym2608);

    ym2608Reset(ym2608);

    return ym2608;
}

void ym2608Destroy(YM2608* ym2608)
{
    mixerUnregisterChannel(ym2608->mixer, ym2608->handle);

    boardTimerDestroy(ym2608->timer[0]);
    boardTimerDestroy(ym2608->timer[1]);
    if (ym2608->irqMask != 0) {
        boardClearInt(ym2608->irqMask);
    }

    delete ym2608->host;
    delete[] ym2608->adpcmRam;
    delete ym2608;
}

void ym2608Reset(YM2608* ym2608)
{
    int i;

    for (i = 0; i < 2; i++) {
        ym2608->timerActive[i] = 0;
        boardTimerRemove(ym2608->timer[i]);
    }
    ym2608->busyEnd     = boardSystemTime();
    ym2608->irqAsserted = 0;
    ym2608->resampleAcc = 0;
    ym2608->address[0]  = 0;
    ym2608->address[1]  = 0;
    memset(ym2608->regs, 0, sizeof(ym2608->regs));

    ym2608->host->chip.reset();
}

UInt8 ym2608Read(YM2608* ym2608, int offset)
{
    mixerSync(ym2608->mixer);
    return ym2608->host->chip.read(offset & 3);
}

/* Only the lower status is free of side effects: reading the upper status
** writes flags back into the chip, and reading the upper data advances the
** ADPCM-B memory pointer. */
UInt8 ym2608Peek(YM2608* ym2608, int offset)
{
    if ((offset & 3) == 0) {
        return ym2608->host->chip.read_status();
    }
    return 0xff;
}

void ym2608Write(YM2608* ym2608, int offset, UInt8 value)
{
    int hi = (offset >> 1) & 1;

    mixerSync(ym2608->mixer);

    if (offset & 1) {
        ym2608->regs[hi][ym2608->address[hi]] = value;
    }
    else {
        ym2608->address[hi] = value;
    }
    ym2608->host->chip.write(offset & 3, value);
}

void ym2608SaveState(YM2608* ym2608)
{
    SaveState* state = saveStateOpenForWrite("ym2608");
    std::vector<uint8_t> chipState;
    ymfm::ymfm_saved_state saver(chipState, true);
    UInt32 now = boardSystemTime();

    ym2608->host->chip.save_restore(saver);

    saveStateSet(state, "timerActiveA",  ym2608->timerActive[0]);
    saveStateSet(state, "timerTimeoutA", ym2608->timerTimeout[0]);
    saveStateSet(state, "timerActiveB",  ym2608->timerActive[1]);
    saveStateSet(state, "timerTimeoutB", ym2608->timerTimeout[1]);
    saveStateSet(state, "busyLeft",      (Int32)(ym2608->busyEnd - now) > 0 ? ym2608->busyEnd - now : 0);
    saveStateSet(state, "irqAsserted",   ym2608->irqAsserted);
    saveStateSet(state, "resampleAcc",   ym2608->resampleAcc);
    saveStateSet(state, "address0",      ym2608->address[0]);
    saveStateSet(state, "address1",      ym2608->address[1]);
    saveStateSetBuffer(state, "regs", ym2608->regs, sizeof(ym2608->regs));
    saveStateSet(state, "adpcmRamSize",  ym2608->adpcmRamSize);
    saveStateSetBuffer(state, "adpcmRam", ym2608->adpcmRam, ym2608->adpcmRamSize);
    saveStateSet(state, "chipStateSize", (UInt32)chipState.size());
    saveStateSetBuffer(state, "chipState", chipState.data(), (UInt32)chipState.size());

    saveStateClose(state);
}

void ym2608LoadState(YM2608* ym2608)
{
    SaveState* state = saveStateOpenForRead("ym2608");
    std::vector<uint8_t> chipState;
    UInt32 size;
    int i;

    if (saveStateIsEmpty(state)) {
        saveStateClose(state);
        return;
    }

    ym2608->timerActive[0]  =        saveStateGet(state, "timerActiveA",  0);
    ym2608->timerTimeout[0] =        saveStateGet(state, "timerTimeoutA", 0);
    ym2608->timerActive[1]  =        saveStateGet(state, "timerActiveB",  0);
    ym2608->timerTimeout[1] =        saveStateGet(state, "timerTimeoutB", 0);
    ym2608->busyEnd         =        boardSystemTime() + saveStateGet(state, "busyLeft", 0);
    ym2608->irqAsserted     =        saveStateGet(state, "irqAsserted",   0);
    ym2608->resampleAcc     =        saveStateGet(state, "resampleAcc",   0);
    ym2608->address[0]      = (UInt8)saveStateGet(state, "address0",      0);
    ym2608->address[1]      = (UInt8)saveStateGet(state, "address1",      0);
    saveStateGetBuffer(state, "regs", ym2608->regs, sizeof(ym2608->regs));

    /* A state saved with a different RAM size keeps only what fits. */
    size = saveStateGet(state, "adpcmRamSize", 0);
    if (size > 0) {
        std::vector<uint8_t> ram(size);
        saveStateGetBuffer(state, "adpcmRam", ram.data(), size);
        memset(ym2608->adpcmRam, 0xff, ym2608->adpcmRamSize);
        memcpy(ym2608->adpcmRam, ram.data(), size < ym2608->adpcmRamSize ? size : ym2608->adpcmRamSize);
    }

    size = saveStateGet(state, "chipStateSize", 0);
    if (size > 0) {
        chipState.resize(size);
        saveStateGetBuffer(state, "chipState", chipState.data(), size);
        ymfm::ymfm_saved_state loader(chipState, false);
        ym2608->host->chip.save_restore(loader);
    }

    saveStateClose(state);

    for (i = 0; i < 2; i++) {
        boardTimerRemove(ym2608->timer[i]);
        if (ym2608->timerActive[i]) {
            boardTimerAdd(ym2608->timer[i], ym2608->timerTimeout[i]);
        }
    }
    if (ym2608->irqMask != 0) {
        if (ym2608->irqAsserted) {
            boardSetInt(ym2608->irqMask);
        }
        else {
            boardClearInt(ym2608->irqMask);
        }
    }
}

void ym2608GetDebugInfo(YM2608* ym2608, DbgDevice* dbgDevice)
{
    static const char* bankName[2] = { "YM2608 (lower)", "YM2608 (upper)" };
    DbgRegisterBank* regBank;
    char name[8];
    int hi;
    int r;

    for (hi = 0; hi < 2; hi++) {
        regBank = dbgDeviceAddRegisterBank(dbgDevice, bankName[hi], 256);
        for (r = 0; r < 256; r++) {
            sprintf(name, "R%.2x", r);
            dbgRegisterBankAddRegister(regBank, r, name, 8, ym2608->regs[hi][r]);
        }
    }

    dbgDeviceAddMemoryBlock(dbgDevice, "ADPCM RAM", 0, 0, ym2608->adpcmRamSize, ym2608->adpcmRam);
}

}
