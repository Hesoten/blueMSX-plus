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
#include "NukedOpllBackend.h"
extern "C" {
#include "NukedOPLL/opll.h"
#include "Emu2413/emu2413.h"  /* borrowing OPLL_RateConv */
#include "../Utils/SaveState.h"
}
#include <cstring>
#include <cstdlib>

/* MSX OPLL: 3.58 MHz / 72 -> 49716 Hz frame rate; sum 18 internal
** slots per frame before OPLL_RateConv so its 16-tap sinc only has
** to handle the small 49716 -> sampleRate ratio. */
#define MSX_OPLL_FRAME_RATE  49716
#define CYCLES_PER_FRAME     18

/* AFTER must cover 18 cycle indices of slot-match window after the
** data write minus 2 for write_d_en + write_fm_data setup. */
#define WRITE_SETTLE_BETWEEN  4
#define WRITE_SETTLE_AFTER   20

NukedOpllBackend::NukedOpllBackend(const std::string& /*name*/, short volume,
                                   const EmuTime& /*time*/)
    : chipMem(NULL), convMem(NULL), maxVolume(volume), sampleRate(44100),
      f_inp((double)MSX_OPLL_FRAME_RATE), f_out(44100.0), outTime(0.0),
      cyclesInFrame(0), frameSumM(0), frameSumR(0)
{
    chipMem = std::malloc(sizeof(opll_t));
    std::memset(chipMem, 0, sizeof(opll_t));
    std::memset(regCache, 0, sizeof(regCache));
    std::memset(buffer,   0, sizeof(buffer));
    OPLL_Reset((opll_t*)chipMem, opll_type_ym2413);
    convMem = OPLL_RateConv_new(f_inp, f_out, 1);
    if (convMem) OPLL_RateConv_reset((OPLL_RateConv*)convMem);
}

NukedOpllBackend::~NukedOpllBackend()
{
    if (convMem) {
        OPLL_RateConv_delete((OPLL_RateConv*)convMem);
        convMem = NULL;
    }
    if (chipMem) {
        std::free(chipMem);
        chipMem = NULL;
    }
}

void NukedOpllBackend::reset(const EmuTime& /*time*/)
{
    OPLL_Reset((opll_t*)chipMem, opll_type_ym2413);
    std::memset(regCache, 0, sizeof(regCache));
    if (convMem) OPLL_RateConv_reset((OPLL_RateConv*)convMem);
    outTime = 0.0;
    cyclesInFrame = 0;
    frameSumM = 0;
    frameSumR = 0;
}

/* Run one chip cycle and on every 18th push the frame sample.
** Shared by writeReg + updateBuffer to keep DAC continuity. */
void NukedOpllBackend::clockAndFeed()
{
    int32_t buf[2];
    OPLL_Clock((opll_t*)chipMem, buf);
    frameSumM += buf[0];
    frameSumR += buf[1];
    if (++cyclesInFrame >= CYCLES_PER_FRAME) {
        int32_t mix = frameSumM + frameSumR;
        if (mix >  32767) mix =  32767;
        if (mix < -32768) mix = -32768;
        if (convMem) {
            OPLL_RateConv_putData((OPLL_RateConv*)convMem, 0, (int16_t)mix);
        }
        outTime += f_out;
        cyclesInFrame = 0;
        frameSumM = 0;
        frameSumR = 0;
    }
}

void NukedOpllBackend::writeReg(byte r, byte v, const EmuTime& /*time*/)
{
    if ((r & 0x3f) < 64) regCache[r & 0x3f] = v;

    opll_t* chip = (opll_t*)chipMem;
    OPLL_Write(chip, 0, r);  /* address latch */
    for (int i = 0; i < WRITE_SETTLE_BETWEEN; i++) clockAndFeed();
    OPLL_Write(chip, 1, v);  /* data         */
    for (int i = 0; i < WRITE_SETTLE_AFTER;  i++) clockAndFeed();
}

byte NukedOpllBackend::peekReg(byte r)
{
    return regCache[r & 0x3f];
}

void NukedOpllBackend::setInternalVolume(short newVolume)
{
    maxVolume = newVolume;
}

int* NukedOpllBackend::updateBuffer(int length)
{
    /* OPLL_RateConv: clockAndFeed sums 18 chip cycles into one MSX-OPLL
    ** frame sample and feeds it to the 16-tap sinc for downsampling to
    ** f_out.  Divisor 128 mirrors OpenYM2413_2's mix scale. */
    OPLL_RateConv* conv = (OPLL_RateConv*)convMem;
    int* dst = buffer;
    for (int n = 0; n < length; n++) {
        while (outTime < f_inp) {
            clockAndFeed();
        }
        outTime -= f_inp;
        int16_t out = conv ? OPLL_RateConv_getData(conv, 0) : 0;
        *(dst++) = (int)out * (int)maxVolume / 128;
    }
    return buffer;
}

void NukedOpllBackend::setSampleRate(int sr, int /*oversampling*/)
{
    sampleRate = sr > 0 ? sr : 44100;
    f_inp = (double)MSX_OPLL_FRAME_RATE;
    f_out = (double)sampleRate;
    if (convMem) OPLL_RateConv_delete((OPLL_RateConv*)convMem);
    convMem = OPLL_RateConv_new(f_inp, f_out, 1);
    if (convMem) OPLL_RateConv_reset((OPLL_RateConv*)convMem);
    outTime = 0.0;
    cyclesInFrame = 0;
    frameSumM = 0;
    frameSumR = 0;
}

/* Field-by-field. Skips patchrom (re-derived from current chipMem).
** Load uses hasChip==0 as "no new-format section" so the caller falls back
** to register replay (covers v8, pre-fix bulk v10, and disabled-chip). */
void NukedOpllBackend::saveState()
{
    SaveState* s = saveStateOpenForWrite("nukedopll");

    if (!chipMem) {
        saveStateClose(s);
        return;
    }

    opll_t* p = (opll_t*)chipMem;

    saveStateSet      (s, "hasChip",      1);
    saveStateSet      (s, "chip_type",    p->chip_type);
    saveStateSet      (s, "cycles",       p->cycles);
    saveStateSet      (s, "slot",         p->slot);

    saveStateSet      (s, "wr_data",      p->write_data);
    saveStateSet      (s, "wr_a",         p->write_a);
    saveStateSet      (s, "wr_d",         p->write_d);
    saveStateSet      (s, "wr_a_en",      p->write_a_en);
    saveStateSet      (s, "wr_d_en",      p->write_d_en);
    saveStateSet      (s, "wr_fm_a",      p->write_fm_address);
    saveStateSet      (s, "wr_fm_d",      p->write_fm_data);
    saveStateSet      (s, "wr_md_a",      p->write_mode_address);
    saveStateSet      (s, "addr",         p->address);
    saveStateSet      (s, "data",         p->data);

    saveStateSet      (s, "eg_cs",        p->eg_counter_state);
    saveStateSet      (s, "eg_csp",       p->eg_counter_state_prev);
    saveStateSet      (s, "eg_tmr",       p->eg_timer);
    saveStateSet      (s, "eg_tll",       p->eg_timer_low_lock);
    saveStateSet      (s, "eg_tcy",       p->eg_timer_carry);
    saveStateSet      (s, "eg_tsh",       p->eg_timer_shift);
    saveStateSet      (s, "eg_tsl",       p->eg_timer_shift_lock);
    saveStateSet      (s, "eg_tss",       p->eg_timer_shift_stop);
    saveStateSetBuffer(s, "eg_state",     p->eg_state, sizeof(p->eg_state));
    saveStateSetBuffer(s, "eg_level",     p->eg_level, sizeof(p->eg_level));
    saveStateSet      (s, "eg_kon",       p->eg_kon);
    saveStateSet      (s, "eg_dkn",       p->eg_dokon);
    saveStateSet      (s, "eg_off",       p->eg_off);
    saveStateSet      (s, "eg_rate",      p->eg_rate);
    saveStateSet      (s, "eg_max",       p->eg_maxrate);
    saveStateSet      (s, "eg_zer",       p->eg_zerorate);
    saveStateSet      (s, "eg_il",        p->eg_inc_lo);
    saveStateSet      (s, "eg_ih",        p->eg_inc_hi);
    saveStateSet      (s, "eg_rh",        p->eg_rate_hi);
    saveStateSet      (s, "eg_sl",        p->eg_sl);
    saveStateSet      (s, "eg_kt",        p->eg_ksltl);
    saveStateSet      (s, "eg_out",       p->eg_out);
    saveStateSet      (s, "eg_sil",       p->eg_silent);

    saveStateSet      (s, "pg_fnum",      p->pg_fnum);
    saveStateSet      (s, "pg_blk",       p->pg_block);
    saveStateSet      (s, "pg_out",       p->pg_out);
    saveStateSet      (s, "pg_inc",       p->pg_inc);
    saveStateSetBuffer(s, "pg_ph",        p->pg_phase, sizeof(p->pg_phase));
    saveStateSet      (s, "pg_phn",       p->pg_phase_next);

    saveStateSetBuffer(s, "op_fb1",       p->op_fb1, sizeof(p->op_fb1));
    saveStateSetBuffer(s, "op_fb2",       p->op_fb2, sizeof(p->op_fb2));
    saveStateSet      (s, "op_fbs",       (UInt32)(int32_t)p->op_fbsum);
    saveStateSet      (s, "op_mod",       (UInt32)(int32_t)p->op_mod);
    saveStateSet      (s, "op_neg",       p->op_neg);
    saveStateSet      (s, "op_ls",        p->op_logsin);
    saveStateSet      (s, "op_em",        p->op_exp_m);
    saveStateSet      (s, "op_es",        p->op_exp_s);

    saveStateSet      (s, "ch_out",       (UInt32)(int32_t)p->ch_out);
    saveStateSet      (s, "ch_hh",        (UInt32)(int32_t)p->ch_out_hh);
    saveStateSet      (s, "ch_tm",        (UInt32)(int32_t)p->ch_out_tm);
    saveStateSet      (s, "ch_bd",        (UInt32)(int32_t)p->ch_out_bd);
    saveStateSet      (s, "ch_sd",        (UInt32)(int32_t)p->ch_out_sd);
    saveStateSet      (s, "ch_tc",        (UInt32)(int32_t)p->ch_out_tc);

    saveStateSet      (s, "lfo_c",        p->lfo_counter);
    saveStateSet      (s, "lfo_vc",       p->lfo_vib_counter);
    saveStateSet      (s, "lfo_ac",       p->lfo_am_counter);
    saveStateSet      (s, "lfo_as",       p->lfo_am_step);
    saveStateSet      (s, "lfo_ad",       p->lfo_am_dir);
    saveStateSet      (s, "lfo_acr",      p->lfo_am_car);
    saveStateSet      (s, "lfo_ao",       p->lfo_am_out);

    saveStateSetBuffer(s, "fnum",         p->fnum,  sizeof(p->fnum));
    saveStateSetBuffer(s, "block",        p->block, sizeof(p->block));
    saveStateSetBuffer(s, "kon",          p->kon,   sizeof(p->kon));
    saveStateSetBuffer(s, "son",          p->son,   sizeof(p->son));
    saveStateSetBuffer(s, "vol",          p->vol,   sizeof(p->vol));
    saveStateSetBuffer(s, "inst",         p->inst,  sizeof(p->inst));
    saveStateSet      (s, "rhythm",       p->rhythm);
    saveStateSet      (s, "testmode",     p->testmode);
    saveStateSetBuffer(s, "patch",        &p->patch, sizeof(p->patch));

    saveStateSet      (s, "c_instr",      p->c_instr);
    saveStateSet      (s, "c_op",         p->c_op);
    saveStateSet      (s, "c_tl",         p->c_tl);
    saveStateSet      (s, "c_dc",         p->c_dc);
    saveStateSet      (s, "c_dm",         p->c_dm);
    saveStateSet      (s, "c_fb",         p->c_fb);
    saveStateSet      (s, "c_am",         p->c_am);
    saveStateSet      (s, "c_vib",        p->c_vib);
    saveStateSet      (s, "c_et",         p->c_et);
    saveStateSet      (s, "c_ksr",        p->c_ksr);
    saveStateSet      (s, "c_ksrf",       p->c_ksr_freq);
    saveStateSet      (s, "c_kslf",       p->c_ksl_freq);
    saveStateSet      (s, "c_kslb",       p->c_ksl_block);
    saveStateSet      (s, "c_multi",      p->c_multi);
    saveStateSet      (s, "c_ksl",        p->c_ksl);
    saveStateSetBuffer(s, "c_adrr",       p->c_adrr, sizeof(p->c_adrr));
    saveStateSet      (s, "c_sl",         p->c_sl);
    saveStateSet      (s, "c_fnum",       p->c_fnum);
    saveStateSet      (s, "c_block",      p->c_block);

    saveStateSet      (s, "rm_en",        (UInt32)(int32_t)p->rm_enable);
    saveStateSet      (s, "rm_noise",     p->rm_noise);
    saveStateSet      (s, "rm_sel",       p->rm_select);
    saveStateSet      (s, "rm_hh2",       p->rm_hh_bit2);
    saveStateSet      (s, "rm_hh3",       p->rm_hh_bit3);
    saveStateSet      (s, "rm_hh7",       p->rm_hh_bit7);
    saveStateSet      (s, "rm_hh8",       p->rm_hh_bit8);
    saveStateSet      (s, "rm_tc3",       p->rm_tc_bit3);
    saveStateSet      (s, "rm_tc5",       p->rm_tc_bit5);

    saveStateSet      (s, "out_m",        (UInt32)(int32_t)p->output_m);
    saveStateSet      (s, "out_r",        (UInt32)(int32_t)p->output_r);

    saveStateSetBuffer(s, "regs",         regCache, sizeof(regCache));

    /* Persist OPLL_RateConv internal state (sinc history + resample timer)
    ** *together with* the backend's frame-timing accumulators so the load
    ** doesn't desync them. Otherwise mid-frame chip/resampler phase
    ** mismatch produces a low-level noise after load. */
    if (convMem) {
        OPLL_RateConv* conv = (OPLL_RateConv*)convMem;
        int lw = OPLL_RateConv_getBufferLength();
        saveStateSet      (s, "conv_present", 1);
        saveStateSetBuffer(s, "conv_buf0",  conv->buf[0], lw * sizeof(int16_t));
        saveStateSetBuffer(s, "conv_timer", &conv->timer, sizeof(conv->timer));
        saveStateSetBuffer(s, "outTime",    &outTime,     sizeof(outTime));
        saveStateSet      (s, "cyclesInFrame", (UInt32)cyclesInFrame);
        saveStateSet      (s, "frameSumM",     (UInt32)frameSumM);
        saveStateSet      (s, "frameSumR",     (UInt32)frameSumR);
    }

    saveStateClose(s);
}

void NukedOpllBackend::loadState()
{
    SaveState* sBack = saveStateOpenForRead("nukedopll");

    if (saveStateGet(sBack, "hasChip", 0)) {
        opll_t* p = (opll_t*)chipMem;

        p->chip_type    = saveStateGet(sBack, "chip_type", p->chip_type);
        p->cycles       = saveStateGet(sBack, "cycles",    p->cycles);
        p->slot         = saveStateGet(sBack, "slot",      p->slot);

        p->write_data         = (uint8_t)saveStateGet(sBack, "wr_data", p->write_data);
        p->write_a            = (uint8_t)saveStateGet(sBack, "wr_a",    p->write_a);
        p->write_d            = (uint8_t)saveStateGet(sBack, "wr_d",    p->write_d);
        p->write_a_en         = (uint8_t)saveStateGet(sBack, "wr_a_en", p->write_a_en);
        p->write_d_en         = (uint8_t)saveStateGet(sBack, "wr_d_en", p->write_d_en);
        p->write_fm_address   = (uint8_t)saveStateGet(sBack, "wr_fm_a", p->write_fm_address);
        p->write_fm_data      = (uint8_t)saveStateGet(sBack, "wr_fm_d", p->write_fm_data);
        p->write_mode_address = (uint8_t)saveStateGet(sBack, "wr_md_a", p->write_mode_address);
        p->address      = (uint8_t)saveStateGet(sBack, "addr",  p->address);
        p->data         = (uint8_t)saveStateGet(sBack, "data",  p->data);

        p->eg_counter_state      = (uint8_t)saveStateGet(sBack, "eg_cs",  p->eg_counter_state);
        p->eg_counter_state_prev = (uint8_t)saveStateGet(sBack, "eg_csp", p->eg_counter_state_prev);
        p->eg_timer              = saveStateGet(sBack, "eg_tmr", p->eg_timer);
        p->eg_timer_low_lock     = (uint8_t)saveStateGet(sBack, "eg_tll", p->eg_timer_low_lock);
        p->eg_timer_carry        = (uint8_t)saveStateGet(sBack, "eg_tcy", p->eg_timer_carry);
        p->eg_timer_shift        = (uint8_t)saveStateGet(sBack, "eg_tsh", p->eg_timer_shift);
        p->eg_timer_shift_lock   = (uint8_t)saveStateGet(sBack, "eg_tsl", p->eg_timer_shift_lock);
        p->eg_timer_shift_stop   = (uint8_t)saveStateGet(sBack, "eg_tss", p->eg_timer_shift_stop);
        saveStateGetBuffer(sBack, "eg_state", p->eg_state, sizeof(p->eg_state));
        saveStateGetBuffer(sBack, "eg_level", p->eg_level, sizeof(p->eg_level));
        p->eg_kon       = (uint8_t)saveStateGet(sBack, "eg_kon", p->eg_kon);
        p->eg_dokon     = saveStateGet(sBack, "eg_dkn", p->eg_dokon);
        p->eg_off       = (uint8_t)saveStateGet(sBack, "eg_off",  p->eg_off);
        p->eg_rate      = (uint8_t)saveStateGet(sBack, "eg_rate", p->eg_rate);
        p->eg_maxrate   = (uint8_t)saveStateGet(sBack, "eg_max",  p->eg_maxrate);
        p->eg_zerorate  = (uint8_t)saveStateGet(sBack, "eg_zer",  p->eg_zerorate);
        p->eg_inc_lo    = (uint8_t)saveStateGet(sBack, "eg_il",   p->eg_inc_lo);
        p->eg_inc_hi    = (uint8_t)saveStateGet(sBack, "eg_ih",   p->eg_inc_hi);
        p->eg_rate_hi   = (uint8_t)saveStateGet(sBack, "eg_rh",   p->eg_rate_hi);
        p->eg_sl        = (uint16_t)saveStateGet(sBack, "eg_sl",  p->eg_sl);
        p->eg_ksltl     = (uint16_t)saveStateGet(sBack, "eg_kt",  p->eg_ksltl);
        p->eg_out       = (uint8_t)saveStateGet(sBack, "eg_out",  p->eg_out);
        p->eg_silent    = (uint8_t)saveStateGet(sBack, "eg_sil",  p->eg_silent);

        p->pg_fnum      = (uint16_t)saveStateGet(sBack, "pg_fnum", p->pg_fnum);
        p->pg_block     = (uint8_t) saveStateGet(sBack, "pg_blk",  p->pg_block);
        p->pg_out       = (uint16_t)saveStateGet(sBack, "pg_out",  p->pg_out);
        p->pg_inc       = saveStateGet(sBack, "pg_inc", p->pg_inc);
        saveStateGetBuffer(sBack, "pg_ph", p->pg_phase, sizeof(p->pg_phase));
        p->pg_phase_next = saveStateGet(sBack, "pg_phn", p->pg_phase_next);

        saveStateGetBuffer(sBack, "op_fb1", p->op_fb1, sizeof(p->op_fb1));
        saveStateGetBuffer(sBack, "op_fb2", p->op_fb2, sizeof(p->op_fb2));
        p->op_fbsum     = (int16_t)(int32_t)saveStateGet(sBack, "op_fbs", (UInt32)(int32_t)p->op_fbsum);
        p->op_mod       = (int16_t)(int32_t)saveStateGet(sBack, "op_mod", (UInt32)(int32_t)p->op_mod);
        p->op_neg       = (uint8_t)saveStateGet(sBack, "op_neg",  p->op_neg);
        p->op_logsin    = (uint16_t)saveStateGet(sBack, "op_ls",  p->op_logsin);
        p->op_exp_m     = (uint16_t)saveStateGet(sBack, "op_em",  p->op_exp_m);
        p->op_exp_s     = (uint16_t)saveStateGet(sBack, "op_es",  p->op_exp_s);

        p->ch_out       = (int16_t)(int32_t)saveStateGet(sBack, "ch_out", (UInt32)(int32_t)p->ch_out);
        p->ch_out_hh    = (int16_t)(int32_t)saveStateGet(sBack, "ch_hh",  (UInt32)(int32_t)p->ch_out_hh);
        p->ch_out_tm    = (int16_t)(int32_t)saveStateGet(sBack, "ch_tm",  (UInt32)(int32_t)p->ch_out_tm);
        p->ch_out_bd    = (int16_t)(int32_t)saveStateGet(sBack, "ch_bd",  (UInt32)(int32_t)p->ch_out_bd);
        p->ch_out_sd    = (int16_t)(int32_t)saveStateGet(sBack, "ch_sd",  (UInt32)(int32_t)p->ch_out_sd);
        p->ch_out_tc    = (int16_t)(int32_t)saveStateGet(sBack, "ch_tc",  (UInt32)(int32_t)p->ch_out_tc);

        p->lfo_counter      = (uint16_t)saveStateGet(sBack, "lfo_c",   p->lfo_counter);
        p->lfo_vib_counter  = (uint8_t) saveStateGet(sBack, "lfo_vc",  p->lfo_vib_counter);
        p->lfo_am_counter   = (uint16_t)saveStateGet(sBack, "lfo_ac",  p->lfo_am_counter);
        p->lfo_am_step      = (uint8_t) saveStateGet(sBack, "lfo_as",  p->lfo_am_step);
        p->lfo_am_dir       = (uint8_t) saveStateGet(sBack, "lfo_ad",  p->lfo_am_dir);
        p->lfo_am_car       = (uint8_t) saveStateGet(sBack, "lfo_acr", p->lfo_am_car);
        p->lfo_am_out       = (uint8_t) saveStateGet(sBack, "lfo_ao",  p->lfo_am_out);

        saveStateGetBuffer(sBack, "fnum",  p->fnum,  sizeof(p->fnum));
        saveStateGetBuffer(sBack, "block", p->block, sizeof(p->block));
        saveStateGetBuffer(sBack, "kon",   p->kon,   sizeof(p->kon));
        saveStateGetBuffer(sBack, "son",   p->son,   sizeof(p->son));
        saveStateGetBuffer(sBack, "vol",   p->vol,   sizeof(p->vol));
        saveStateGetBuffer(sBack, "inst",  p->inst,  sizeof(p->inst));
        p->rhythm       = (uint8_t)saveStateGet(sBack, "rhythm",   p->rhythm);
        p->testmode     = (uint8_t)saveStateGet(sBack, "testmode", p->testmode);
        saveStateGetBuffer(sBack, "patch", &p->patch, sizeof(p->patch));

        p->c_instr      = (uint8_t)saveStateGet(sBack, "c_instr", p->c_instr);
        p->c_op         = (uint8_t)saveStateGet(sBack, "c_op",    p->c_op);
        p->c_tl         = (uint8_t)saveStateGet(sBack, "c_tl",    p->c_tl);
        p->c_dc         = (uint8_t)saveStateGet(sBack, "c_dc",    p->c_dc);
        p->c_dm         = (uint8_t)saveStateGet(sBack, "c_dm",    p->c_dm);
        p->c_fb         = (uint8_t)saveStateGet(sBack, "c_fb",    p->c_fb);
        p->c_am         = (uint8_t)saveStateGet(sBack, "c_am",    p->c_am);
        p->c_vib        = (uint8_t)saveStateGet(sBack, "c_vib",   p->c_vib);
        p->c_et         = (uint8_t)saveStateGet(sBack, "c_et",    p->c_et);
        p->c_ksr        = (uint8_t)saveStateGet(sBack, "c_ksr",   p->c_ksr);
        p->c_ksr_freq   = (uint8_t)saveStateGet(sBack, "c_ksrf",  p->c_ksr_freq);
        p->c_ksl_freq   = (uint8_t)saveStateGet(sBack, "c_kslf",  p->c_ksl_freq);
        p->c_ksl_block  = (uint8_t)saveStateGet(sBack, "c_kslb",  p->c_ksl_block);
        p->c_multi      = (uint8_t)saveStateGet(sBack, "c_multi", p->c_multi);
        p->c_ksl        = (uint8_t)saveStateGet(sBack, "c_ksl",   p->c_ksl);
        saveStateGetBuffer(sBack, "c_adrr", p->c_adrr, sizeof(p->c_adrr));
        p->c_sl         = (uint8_t) saveStateGet(sBack, "c_sl",    p->c_sl);
        p->c_fnum       = (uint16_t)saveStateGet(sBack, "c_fnum",  p->c_fnum);
        p->c_block      = (uint16_t)saveStateGet(sBack, "c_block", p->c_block);

        p->rm_enable    = (int8_t)(int32_t)saveStateGet(sBack, "rm_en", (UInt32)(int32_t)p->rm_enable);
        p->rm_noise     = saveStateGet(sBack, "rm_noise", p->rm_noise);
        p->rm_select    = saveStateGet(sBack, "rm_sel",   p->rm_select);
        p->rm_hh_bit2   = (uint8_t)saveStateGet(sBack, "rm_hh2", p->rm_hh_bit2);
        p->rm_hh_bit3   = (uint8_t)saveStateGet(sBack, "rm_hh3", p->rm_hh_bit3);
        p->rm_hh_bit7   = (uint8_t)saveStateGet(sBack, "rm_hh7", p->rm_hh_bit7);
        p->rm_hh_bit8   = (uint8_t)saveStateGet(sBack, "rm_hh8", p->rm_hh_bit8);
        p->rm_tc_bit3   = (uint8_t)saveStateGet(sBack, "rm_tc3", p->rm_tc_bit3);
        p->rm_tc_bit5   = (uint8_t)saveStateGet(sBack, "rm_tc5", p->rm_tc_bit5);

        p->output_m     = (int16_t)(int32_t)saveStateGet(sBack, "out_m", (UInt32)(int32_t)p->output_m);
        p->output_r     = (int16_t)(int32_t)saveStateGet(sBack, "out_r", (UInt32)(int32_t)p->output_r);

        saveStateGetBuffer(sBack, "regs", regCache, sizeof(regCache));

        int convPresent = saveStateGet(sBack, "conv_present", 0);
        if (convMem) {
            OPLL_RateConv* conv = (OPLL_RateConv*)convMem;
            if (convPresent) {
                int lw = OPLL_RateConv_getBufferLength();
                saveStateGetBuffer(sBack, "conv_buf0",  conv->buf[0], lw * sizeof(int16_t));
                saveStateGetBuffer(sBack, "conv_timer", &conv->timer, sizeof(conv->timer));
            } else {
                OPLL_RateConv_reset(conv);
            }
        }
        if (convPresent) {
            saveStateGetBuffer(sBack, "outTime", &outTime, sizeof(outTime));
            cyclesInFrame = (int)saveStateGet(sBack, "cyclesInFrame", 0);
            frameSumM     = (int32_t)saveStateGet(sBack, "frameSumM",     0);
            frameSumR     = (int32_t)saveStateGet(sBack, "frameSumR",     0);
        } else {
            outTime = 0.0;
            cyclesInFrame = 0;
            frameSumM = 0;
            frameSumR = 0;
        }
        saveStateClose(sBack);
        return;
    }
    saveStateClose(sBack);

    /* Fallback: register-replay. Transient state (eg, pg, LFO) reinitialised. */
    byte src[64];
    std::memset(src, 0, sizeof(src));
    sBack = saveStateOpenForRead("nukedopll");
    saveStateGetBuffer(sBack, "regs", src, sizeof(src));
    saveStateClose(sBack);

    int allZero = 1;
    for (int i = 0; i < 64; i++) {
        if (src[i] != 0) { allZero = 0; break; }
    }
    if (allZero) {
        byte fromWrap[256];
        std::memset(fromWrap, 0, sizeof(fromWrap));
        SaveState* sWrap = saveStateOpenForRead("msxmusic");
        saveStateGetBuffer(sWrap, "regs", fromWrap, sizeof(fromWrap));
        saveStateClose(sWrap);
        std::memcpy(src, fromWrap, 64);
    }

    opll_t* chip = (opll_t*)chipMem;
    OPLL_Reset(chip, opll_type_ym2413);
    if (convMem) OPLL_RateConv_reset((OPLL_RateConv*)convMem);
    outTime = 0.0;
    for (int r = 0; r < 64; r++) {
        regCache[r] = src[r];
        OPLL_Write(chip, 0, r);
        for (int i = 0; i < WRITE_SETTLE_BETWEEN; i++) clockAndFeed();
        OPLL_Write(chip, 1, src[r]);
        for (int i = 0; i < WRITE_SETTLE_AFTER;  i++) clockAndFeed();
    }
}
