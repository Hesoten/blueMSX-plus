/*****************************************************************************
**
** Fmopl-backed Y8950 backend (original blueMSX backend).
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
#include "FmoplBackend.h"
extern "C" {
#include "SaveState.h"
extern void* cur_chip;  /* Fmopl.c global: cached active chip pointer */
}
#include <cstring>

#define FREQUENCY  3579545
#define SAMPLERATE (FREQUENCY / 72)

FmoplBackend::FmoplBackend(void* ref)
    : hostRef(ref), opl(NULL), rate(SAMPLERATE),
      off(0), s1(0), s2(0)
{
    memset(buffer, 0, sizeof(buffer));
    /* hostRef is forwarded so fmopl's y8950GetNoteOn sees the host. */
    opl = OPLCreate(OPL_TYPE_Y8950, FREQUENCY, SAMPLERATE, 256, hostRef);
    OPLSetOversampling(opl, boardGetY8950Oversampling());
    OPLResetChip(opl);
}

FmoplBackend::~FmoplBackend()
{
    if (opl) OPLDestroy(opl);
}

void FmoplBackend::reset()
{
    OPLResetChip(opl);
    off = 0;
    s1  = 0;
    s2  = 0;
}

void FmoplBackend::setSampleRate(UInt32 newRate)
{
    rate = newRate;
}

Int32* FmoplBackend::updateBuffer(UInt32 count)
{
    for (UInt32 i = 0; i < count; i++) {
        if (SAMPLERATE > rate) {
            off -= SAMPLERATE - (Int32)rate;
            s1 = s2;
            s2 = Y8950UpdateOne(opl);
            if (off < 0) {
                off += (Int32)rate;
                s1 = s2;
                s2 = Y8950UpdateOne(opl);
            }
            buffer[i] = (s1 * (off / 256) + s2 * (((Int32)SAMPLERATE - off) / 256)) / (SAMPLERATE / 256);
        }
        else {
            buffer[i] = Y8950UpdateOne(opl);
        }
    }
    return buffer;
}

void FmoplBackend::writeIo(int port, UInt8 value)
{
    OPLWrite(opl, port, value);
}

UInt8 FmoplBackend::readIo(int port)
{
    return (UInt8)OPLRead(opl, port);
}

UInt8 FmoplBackend::peekIo(int port)
{
    return (UInt8)OPLPeek(opl, port);
}

UInt8 FmoplBackend::readReg(int reg)
{
    return (UInt8)opl->regs[reg];
}

/* fmopl_host = resampler state only (chip + timer live elsewhere). */
void FmoplBackend::saveState()
{
    SaveState* state = saveStateOpenForWrite("fmopl_host");
    saveStateSet(state, "off",  off);
    saveStateSet(state, "s1",   s1);
    saveStateSet(state, "s2",   s2);
    saveStateClose(state);

    Y8950SaveState(opl);
    YM_DELTAT_ADPCM_SaveState(opl->deltat);
}

void FmoplBackend::loadState()
{
    /* Reset resampler state so a mid-session load starts clean. */
    off = 0;
    s1  = 0;
    s2  = 0;

    SaveState* state = saveStateOpenForRead("fmopl_host");
    if (!saveStateIsEmpty(state)) {
        off  = saveStateGet(state, "off",  0);
        s1   = saveStateGet(state, "s1",   0);
        s2   = saveStateGet(state, "s2",   0);
    }
    saveStateClose(state);

    /* Y8950LoadState / YM_DELTAT_ADPCM_LoadState detect empty sections
    ** internally and leave the chip at ctor state; pre-probing would
    ** desync the SaveState per-name index counter. */
    int loadedChip = Y8950LoadState(opl);
    YM_DELTAT_ADPCM_LoadState(opl->deltat);
    loadHadOwnState_ = (loadedChip != 0);

    if (loadedChip) {
        /* Recover save's oversampling from FN_TABLE[256] (=262144/osc)
        ** so chip rate matches the loaded tables -- ctor races
        ** boardSetY8950Oversampling and may have set the wrong rate. */
        if (opl->FN_TABLE[256] > 0) {
            int saveOsc = (int)(262144u / opl->FN_TABLE[256]);
            if (saveOsc < 1) saveOsc = 1;
            if (saveOsc != opl->rate / opl->baseRate) {
                OPLSetOversampling(opl, saveOsc);
            }
        }
    }

    /* Fmopl caches per-chip statics keyed by `cur_chip == OPL`. */
    cur_chip = NULL;
}

const UInt8* FmoplBackend::getAdpcmRam(UInt32* size_out)
{
    if (size_out) *size_out = (UInt32)opl->deltat->memory_size;
    return (const UInt8*)opl->deltat->memory;
}

void FmoplBackend::copyAdpcmRamFrom(const UInt8* src, UInt32 len)
{
    /* RAM lives in the dispatcher's y8950_adpcm_ram section. */
    if (!src || !opl || !opl->deltat || !opl->deltat->memory) return;
    UInt32 cap = (UInt32)opl->deltat->memory_size;
    if (cap == 0) return;
    if (len > cap) len = cap;
    memcpy(opl->deltat->memory, src, len);
}

void FmoplBackend::onTimerOverflow(int timer_idx)
{
    (void)OPLTimerOver(opl, timer_idx);
}
