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
#include "Emu2413Backend.h"
extern "C" {
#include "Emu2413/emu2413.h"
#include "../Utils/SaveState.h"
}
#include <cstring>

#define MSX_OPLL_CLOCK 3579545

Emu2413Backend::Emu2413Backend(const std::string& /*name*/, short volume,
                               const EmuTime& /*time*/)
    : opll(NULL), maxVolume(volume)
{
    memset(regCache, 0, sizeof(regCache));
    memset(buffer,   0, sizeof(buffer));
    /* 44100 is a placeholder; Mixer calls setSampleRate right after
    ** construction.  Avoids OPLL_new divide-by-zero if anyone races it. */
    opll = OPLL_new(MSX_OPLL_CLOCK, 44100);
    OPLL_setChipType(opll, 0);  /* 0 = OPLL_2413_TONE */
    OPLL_reset(opll);
}

Emu2413Backend::~Emu2413Backend()
{
    if (opll) {
        OPLL_delete(opll);
        opll = NULL;
    }
}

void Emu2413Backend::reset(const EmuTime& /*time*/)
{
    OPLL_reset(opll);
    memset(regCache, 0, sizeof(regCache));
}

void Emu2413Backend::writeReg(byte r, byte v, const EmuTime& /*time*/)
{
    if ((r & 0x3f) < 64) {
        regCache[r & 0x3f] = v;
    }
    OPLL_writeReg(opll, r, v);
}

byte Emu2413Backend::peekReg(byte r)
{
    return regCache[r & 0x3f];
}

void Emu2413Backend::setInternalVolume(short newVolume)
{
    maxVolume = newVolume;
}

/* RMS-match EMU2413 (cleaner dynamics) with OpenYM2413_2 so backend
** switches don't rebalance the mixer. */
#define EMU2413_OUTPUT_RMS_MATCH_DIV 1024

int* Emu2413Backend::updateBuffer(int length)
{
    int* dst = buffer;
    for (int i = 0; i < length; i++) {
        int s = (int)OPLL_calc(opll);
        *(dst++) = s * (int)maxVolume / EMU2413_OUTPUT_RMS_MATCH_DIV;
    }
    return buffer;
}

void Emu2413Backend::setSampleRate(int sampleRate, int oversampling)
{
    if (sampleRate <= 0) sampleRate = 44100;
    OPLL_setRate(opll, (uint32_t)sampleRate);
    /* OPLL_setQuality(1) enables EMU2413's high-quality (interpolated)
    ** rendering path.  Map any oversampling > 1 onto that. */
    OPLL_setQuality(opll, oversampling > 1 ? 1 : 0);
}

/* Field-by-field. Skips pointers; OPLL_relinkAfterRestore rebuilds them.
** Load uses hasChip==0 as "no new-format section" so the caller falls back
** to register replay (covers v8, pre-fix bulk v10, and disabled-chip).
**
** Save takes an atomic-ish snapshot first because the chip is read by
** the audio thread; field-by-field reads spread over many saveStateSet
** calls + reallocs would otherwise capture inconsistent state. */
void Emu2413Backend::saveState()
{
    SaveState* s = saveStateOpenForWrite("emu2413");
    char t[24];

    if (!opll) {
        saveStateClose(s);
        return;
    }

    OPLL snap;
    memcpy(&snap, opll, sizeof(OPLL));
    OPLL* p = &snap;

    saveStateSet      (s, "hasChip",         1);
    saveStateSet      (s, "clk",             p->clk);
    saveStateSet      (s, "rate",            p->rate);
    saveStateSet      (s, "chip_type",       p->chip_type);
    saveStateSet      (s, "adr",             p->adr);
    saveStateSetBuffer(s, "inp_step",        &p->inp_step, sizeof(p->inp_step));
    saveStateSetBuffer(s, "out_step",        &p->out_step, sizeof(p->out_step));
    saveStateSetBuffer(s, "out_time",        &p->out_time, sizeof(p->out_time));
    saveStateSetBuffer(s, "reg",             p->reg, sizeof(p->reg));
    saveStateSet      (s, "test_flag",       p->test_flag);
    saveStateSet      (s, "slot_key_status", p->slot_key_status);
    saveStateSet      (s, "rhythm_mode",     p->rhythm_mode);
    saveStateSet      (s, "eg_counter",      p->eg_counter);
    saveStateSet      (s, "pm_phase",        p->pm_phase);
    saveStateSet      (s, "am_phase",        (UInt32)p->am_phase);
    saveStateSet      (s, "lfo_am",          p->lfo_am);
    saveStateSet      (s, "noise",           p->noise);
    saveStateSet      (s, "short_noise",     p->short_noise);
    saveStateSetBuffer(s, "patch_number",    p->patch_number, sizeof(p->patch_number));

    for (int i = 0; i < 18; i++) {
        const OPLL_SLOT* sl = &p->slot[i];
        sprintf(t, "s%02d_number",  i); saveStateSet      (s, t, sl->number);
        sprintf(t, "s%02d_type",    i); saveStateSet      (s, t, sl->type);
        sprintf(t, "s%02d_out0",    i); saveStateSet      (s, t, (UInt32)sl->output[0]);
        sprintf(t, "s%02d_out1",    i); saveStateSet      (s, t, (UInt32)sl->output[1]);
        sprintf(t, "s%02d_pgph",    i); saveStateSet      (s, t, sl->pg_phase);
        sprintf(t, "s%02d_pgout",   i); saveStateSet      (s, t, sl->pg_out);
        sprintf(t, "s%02d_pgkeep",  i); saveStateSet      (s, t, sl->pg_keep);
        sprintf(t, "s%02d_blkfnum", i); saveStateSet      (s, t, sl->blk_fnum);
        sprintf(t, "s%02d_fnum",    i); saveStateSet      (s, t, sl->fnum);
        sprintf(t, "s%02d_blk",     i); saveStateSet      (s, t, sl->blk);
        sprintf(t, "s%02d_egst",    i); saveStateSet      (s, t, sl->eg_state);
        sprintf(t, "s%02d_vol",     i); saveStateSet      (s, t, (UInt32)sl->volume);
        sprintf(t, "s%02d_key",     i); saveStateSet      (s, t, sl->key_flag);
        sprintf(t, "s%02d_sus",     i); saveStateSet      (s, t, sl->sus_flag);
        sprintf(t, "s%02d_tll",     i); saveStateSet      (s, t, sl->tll);
        sprintf(t, "s%02d_rks",     i); saveStateSet      (s, t, sl->rks);
        sprintf(t, "s%02d_egrh",    i); saveStateSet      (s, t, sl->eg_rate_h);
        sprintf(t, "s%02d_egrl",    i); saveStateSet      (s, t, sl->eg_rate_l);
        sprintf(t, "s%02d_egsh",    i); saveStateSet      (s, t, sl->eg_shift);
        sprintf(t, "s%02d_egout",   i); saveStateSet      (s, t, sl->eg_out);
        sprintf(t, "s%02d_upd",     i); saveStateSet      (s, t, sl->update_requests);
    }

    saveStateSetBuffer(s, "patch",    p->patch,    sizeof(p->patch));
    saveStateSetBuffer(s, "pan",      p->pan,      sizeof(p->pan));
    saveStateSetBuffer(s, "pan_fine", p->pan_fine, sizeof(p->pan_fine));
    saveStateSet      (s, "mask",     p->mask);
    saveStateSetBuffer(s, "ch_out",   p->ch_out,   sizeof(p->ch_out));
    saveStateSetBuffer(s, "mix_out",  p->mix_out,  sizeof(p->mix_out));

    saveStateSetBuffer(s, "regs",     regCache,    sizeof(regCache));

    /* Save OPLL_RateConv internal state (sinc history + resample timer);
    ** otherwise OPLL_RateConv_reset on load produces a persistent high-freq
    ** artefact from a zero-history sinc convolution. */
    if (opll->conv) {
        int lw = OPLL_RateConv_getBufferLength();
        saveStateSet      (s, "conv_present", 1);
        saveStateSetBuffer(s, "conv_buf0",  opll->conv->buf[0], lw * sizeof(int16_t));
        saveStateSetBuffer(s, "conv_buf1",  opll->conv->buf[1], lw * sizeof(int16_t));
        saveStateSetBuffer(s, "conv_timer", &opll->conv->timer, sizeof(opll->conv->timer));
    }

    saveStateClose(s);
}

void Emu2413Backend::loadState()
{
    SaveState* sBack = saveStateOpenForRead("emu2413");
    char t[24];

    if (saveStateGet(sBack, "hasChip", 0)) {
        OPLL* p = opll;
        p->clk             = saveStateGet(sBack, "clk",             p->clk);
        p->rate            = saveStateGet(sBack, "rate",            p->rate);
        p->chip_type       = (uint8_t)saveStateGet(sBack, "chip_type",       p->chip_type);
        p->adr             = saveStateGet(sBack, "adr",             p->adr);
        saveStateGetBuffer(sBack, "inp_step", &p->inp_step, sizeof(p->inp_step));
        saveStateGetBuffer(sBack, "out_step", &p->out_step, sizeof(p->out_step));
        saveStateGetBuffer(sBack, "out_time", &p->out_time, sizeof(p->out_time));
        saveStateGetBuffer(sBack, "reg",      p->reg, sizeof(p->reg));
        p->test_flag       = (uint8_t)saveStateGet(sBack, "test_flag",       p->test_flag);
        p->slot_key_status = saveStateGet(sBack, "slot_key_status", p->slot_key_status);
        p->rhythm_mode     = (uint8_t)saveStateGet(sBack, "rhythm_mode",     p->rhythm_mode);
        p->eg_counter      = saveStateGet(sBack, "eg_counter",      p->eg_counter);
        p->pm_phase        = saveStateGet(sBack, "pm_phase",        p->pm_phase);
        p->am_phase        = (int32_t)saveStateGet(sBack, "am_phase", (UInt32)p->am_phase);
        p->lfo_am          = (uint8_t)saveStateGet(sBack, "lfo_am",          p->lfo_am);
        p->noise           = saveStateGet(sBack, "noise",           p->noise);
        p->short_noise     = (uint8_t)saveStateGet(sBack, "short_noise",     p->short_noise);
        saveStateGetBuffer(sBack, "patch_number", p->patch_number, sizeof(p->patch_number));

        for (int i = 0; i < 18; i++) {
            OPLL_SLOT* sl = &p->slot[i];
            sprintf(t, "s%02d_number",  i); sl->number          = (uint8_t) saveStateGet(sBack, t, sl->number);
            sprintf(t, "s%02d_type",    i); sl->type            = (uint8_t) saveStateGet(sBack, t, sl->type);
            sprintf(t, "s%02d_out0",    i); sl->output[0]       = (int32_t) saveStateGet(sBack, t, (UInt32)sl->output[0]);
            sprintf(t, "s%02d_out1",    i); sl->output[1]       = (int32_t) saveStateGet(sBack, t, (UInt32)sl->output[1]);
            sprintf(t, "s%02d_pgph",    i); sl->pg_phase        = saveStateGet(sBack, t, sl->pg_phase);
            sprintf(t, "s%02d_pgout",   i); sl->pg_out          = saveStateGet(sBack, t, sl->pg_out);
            sprintf(t, "s%02d_pgkeep",  i); sl->pg_keep         = (uint8_t) saveStateGet(sBack, t, sl->pg_keep);
            sprintf(t, "s%02d_blkfnum", i); sl->blk_fnum        = (uint16_t)saveStateGet(sBack, t, sl->blk_fnum);
            sprintf(t, "s%02d_fnum",    i); sl->fnum            = (uint16_t)saveStateGet(sBack, t, sl->fnum);
            sprintf(t, "s%02d_blk",     i); sl->blk             = (uint8_t) saveStateGet(sBack, t, sl->blk);
            sprintf(t, "s%02d_egst",    i); sl->eg_state        = (uint8_t) saveStateGet(sBack, t, sl->eg_state);
            sprintf(t, "s%02d_vol",     i); sl->volume          = (int32_t) saveStateGet(sBack, t, (UInt32)sl->volume);
            sprintf(t, "s%02d_key",     i); sl->key_flag        = (uint8_t) saveStateGet(sBack, t, sl->key_flag);
            sprintf(t, "s%02d_sus",     i); sl->sus_flag        = (uint8_t) saveStateGet(sBack, t, sl->sus_flag);
            sprintf(t, "s%02d_tll",     i); sl->tll             = (uint16_t)saveStateGet(sBack, t, sl->tll);
            sprintf(t, "s%02d_rks",     i); sl->rks             = (uint8_t) saveStateGet(sBack, t, sl->rks);
            sprintf(t, "s%02d_egrh",    i); sl->eg_rate_h       = (uint8_t) saveStateGet(sBack, t, sl->eg_rate_h);
            sprintf(t, "s%02d_egrl",    i); sl->eg_rate_l       = (uint8_t) saveStateGet(sBack, t, sl->eg_rate_l);
            sprintf(t, "s%02d_egsh",    i); sl->eg_shift        = saveStateGet(sBack, t, sl->eg_shift);
            sprintf(t, "s%02d_egout",   i); sl->eg_out          = saveStateGet(sBack, t, sl->eg_out);
            sprintf(t, "s%02d_upd",     i); sl->update_requests = saveStateGet(sBack, t, sl->update_requests);
        }

        saveStateGetBuffer(sBack, "patch",    p->patch,    sizeof(p->patch));
        saveStateGetBuffer(sBack, "pan",      p->pan,      sizeof(p->pan));
        saveStateGetBuffer(sBack, "pan_fine", p->pan_fine, sizeof(p->pan_fine));
        p->mask = saveStateGet(sBack, "mask", p->mask);
        saveStateGetBuffer(sBack, "ch_out",   p->ch_out,   sizeof(p->ch_out));
        saveStateGetBuffer(sBack, "mix_out",  p->mix_out,  sizeof(p->mix_out));

        saveStateGetBuffer(sBack, "regs",     regCache,    sizeof(regCache));

        if (p->conv) {
            if (saveStateGet(sBack, "conv_present", 0)) {
                int lw = OPLL_RateConv_getBufferLength();
                saveStateGetBuffer(sBack, "conv_buf0",  p->conv->buf[0], lw * sizeof(int16_t));
                saveStateGetBuffer(sBack, "conv_buf1",  p->conv->buf[1], lw * sizeof(int16_t));
                saveStateGetBuffer(sBack, "conv_timer", &p->conv->timer, sizeof(p->conv->timer));
            } else {
                OPLL_RateConv_reset(p->conv);
            }
        }
        OPLL_relinkAfterRestore(p);
        saveStateClose(sBack);
        return;
    }
    saveStateClose(sBack);

    /* Fallback: register-replay. Prefer emu2413/regs over msxmusic/regs;
    ** transient state (eg phase, pg phase, LFO) is reinitialised. */
    byte fromBackend[64];
    byte fromWrapper[256];
    memset(fromBackend, 0, sizeof(fromBackend));
    memset(fromWrapper, 0, sizeof(fromWrapper));

    sBack = saveStateOpenForRead("emu2413");
    saveStateGetBuffer(sBack, "regs", fromBackend, sizeof(fromBackend));
    saveStateClose(sBack);

    SaveState* sWrap = saveStateOpenForRead("msxmusic");
    saveStateGetBuffer(sWrap, "regs", fromWrapper, sizeof(fromWrapper));
    saveStateClose(sWrap);

    int allZeroBackend = 1;
    for (int i = 0; i < 64; i++) {
        if (fromBackend[i] != 0) { allZeroBackend = 0; break; }
    }
    const byte* src = allZeroBackend ? fromWrapper : fromBackend;

    OPLL_reset(opll);
    for (int r = 0; r < 64; r++) {
        regCache[r] = src[r];
        OPLL_writeReg(opll, r, src[r]);
    }
}
