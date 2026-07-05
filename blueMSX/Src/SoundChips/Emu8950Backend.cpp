/*****************************************************************************
**
** emu8950-backed Y8950 backend.
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
#include "Emu8950Backend.h"
extern "C" {
#include "Emu8950/emu8950.h"
#include "Emu8950/emuadpcm.h"
#include "../Utils/SaveState.h"
#include "Board.h"
}
#include <cstdio>
#include <cstring>

#define FREQUENCY  3579545
#define SAMPLERATE (FREQUENCY / 72)

Emu8950Backend::Emu8950Backend()
    : opl(NULL), mixerRate(SAMPLERATE), off(0), e1(0), e2(0)
{
    memset(buffer, 0, sizeof(buffer));
    opl = (struct __OPL*)OPL_new((uint32_t)FREQUENCY, (uint32_t)SAMPLERATE);
    OPL_setChipType((OPL*)opl, 0);  /* 0 = Y8950 */
    OPL_reset((OPL*)opl);
}

Emu8950Backend::~Emu8950Backend()
{
    if (opl) OPL_delete((OPL*)opl);
}

void Emu8950Backend::reset()
{
    OPL_reset((OPL*)opl);
    off = 0;
    e1  = 0;
    e2  = 0;
}

void Emu8950Backend::setSampleRate(UInt32 rate)
{
    mixerRate = rate;
    /* Keep emu8950 at chip rate so its 16-tap sinc rate converter is
    ** disabled.  updateBuffer here handles the chip-rate -> mixer-rate
    ** interpolation. */
    OPL_setRate((OPL*)opl, (uint32_t)SAMPLERATE);
}

Int32* Emu8950Backend::updateBuffer(UInt32 count)
{
    for (UInt32 i = 0; i < count; i++) {
        if (SAMPLERATE > mixerRate) {
            off -= SAMPLERATE - (Int32)mixerRate;
            e1 = e2;
            e2 = (Int32)OPL_calc((OPL*)opl) * 32;
            if (off < 0) {
                off += (Int32)mixerRate;
                e1 = e2;
                e2 = (Int32)OPL_calc((OPL*)opl) * 32;
            }
            buffer[i] = (e1 * (off / 256) + e2 * (((Int32)SAMPLERATE - off) / 256)) / (SAMPLERATE / 256);
        }
        else {
            buffer[i] = (Int32)OPL_calc((OPL*)opl) * 32;
        }
    }
    return buffer;
}

void Emu8950Backend::writeIo(int port, UInt8 value)
{
    OPL_writeIO((OPL*)opl, (uint32_t)port, value);
}

UInt8 Emu8950Backend::readIo(int port)
{
    /* emu8950 exposes status via OPL_status (port 0) and register data
    ** via OPL_readIO (port 1).  Port 0 reads must consult OPL_status to
    ** include the ADPCM EOS / BUF-RDY bits. */
    if ((port & 1) == 0) return (UInt8)OPL_status((OPL*)opl);
    return (UInt8)OPL_readIO((OPL*)opl);
}

UInt8 Emu8950Backend::peekIo(int port)
{
    return readIo(port);
}

UInt8 Emu8950Backend::readReg(int reg)
{
    return ((OPL*)opl)->reg[reg];
}

const UInt8* Emu8950Backend::getAdpcmRam(UInt32* size_out)
{
    OPL* p = (OPL*)opl;
    if (!p->adpcm || !p->adpcm->memory[0]) {
        if (size_out) *size_out = 0;
        return NULL;
    }
    if (size_out) *size_out = 256 * 1024;
    return (const UInt8*)p->adpcm->memory[0];
}

void Emu8950Backend::copyAdpcmRamFrom(const UInt8* src, UInt32 len)
{
    OPL* p = (OPL*)opl;
    if (!p->adpcm || !p->adpcm->memory[0] || !src) return;
    UInt32 ramSize = 256 * 1024;
    if (len > ramSize) len = ramSize;
    memcpy(p->adpcm->memory[0], src, len);
}

/* Dispatcher fired; set status + raise boardSetInt if not masked. */
void Emu8950Backend::onTimerOverflow(int timer_idx)
{
    OPL* p = (OPL*)opl;
    uint8_t bit = (timer_idx == 0) ? 0x40 : 0x20;
    p->status |= bit;
    if (!(p->reg[0x04] & bit)) {
        boardSetInt(0x10);
    }
}

/* Field-by-field. Skips pointers; OPL_relinkAfterRestore rebuilds them.
** Load uses hasChip==0 as "no new-format section" so the caller falls
** back to register replay (covers v8, pre-fix bulk v10, disabled-chip). */
void Emu8950Backend::saveState()
{
    OPL* p = (OPL*)opl;
    SaveState* s = saveStateOpenForWrite("emu8950");
    char t[24];
    int i;

    if (!p) {
        saveStateClose(s);
        return;
    }

    saveStateSet      (s, "hasChip",         1);
    saveStateSet      (s, "clk",             p->clk);
    saveStateSet      (s, "rate",            p->rate);
    saveStateSet      (s, "chip_type",       p->chip_type);
    saveStateSet      (s, "adr",             p->adr);
    saveStateSet      (s, "csm_mode",        p->csm_mode);
    saveStateSet      (s, "csm_key_count",   p->csm_key_count);
    saveStateSet      (s, "notesel",         p->notesel);
    saveStateSet      (s, "inp_step",        p->inp_step);
    saveStateSet      (s, "out_step",        p->out_step);
    saveStateSet      (s, "out_time",        p->out_time);
    saveStateSetBuffer(s, "reg",             p->reg, sizeof(p->reg));
    saveStateSet      (s, "test_flag",       p->test_flag);
    saveStateSet      (s, "slot_key_status", p->slot_key_status);
    saveStateSet      (s, "rhythm_mode",     p->rhythm_mode);
    saveStateSet      (s, "eg_counter",      p->eg_counter);
    saveStateSet      (s, "pm_phase",        p->pm_phase);
    saveStateSet      (s, "pm_dphase",       p->pm_dphase);
    saveStateSet      (s, "am_phase",        (UInt32)p->am_phase);
    saveStateSet      (s, "am_dphase",       (UInt32)p->am_dphase);
    saveStateSet      (s, "lfo_am",          p->lfo_am);
    saveStateSet      (s, "noise",           p->noise);
    saveStateSet      (s, "short_noise",     p->short_noise);

    for (i = 0; i < 18; i++) {
        const OPL_SLOT* sl = &p->slot[i];
        sprintf(t, "s%02d_number",  i); saveStateSet      (s, t, sl->number);
        sprintf(t, "s%02d_type",    i); saveStateSet      (s, t, sl->type);
        sprintf(t, "s%02d_patch",   i); saveStateSetBuffer(s, t, (void*)&sl->__patch, sizeof(OPL_PATCH));
        sprintf(t, "s%02d_out0",    i); saveStateSet      (s, t, (UInt32)sl->output[0]);
        sprintf(t, "s%02d_out1",    i); saveStateSet      (s, t, (UInt32)sl->output[1]);
        sprintf(t, "s%02d_pgph",    i); saveStateSet      (s, t, sl->pg_phase);
        sprintf(t, "s%02d_pgout",   i); saveStateSet      (s, t, sl->pg_out);
        sprintf(t, "s%02d_pgkeep",  i); saveStateSet      (s, t, sl->pg_keep);
        sprintf(t, "s%02d_blkfnum", i); saveStateSet      (s, t, sl->blk_fnum);
        sprintf(t, "s%02d_fnum",    i); saveStateSet      (s, t, sl->fnum);
        sprintf(t, "s%02d_blk",     i); saveStateSet      (s, t, sl->blk);
        sprintf(t, "s%02d_egst",    i); saveStateSet      (s, t, sl->eg_state);
        sprintf(t, "s%02d_tll",     i); saveStateSet      (s, t, sl->tll);
        sprintf(t, "s%02d_rks",     i); saveStateSet      (s, t, sl->rks);
        sprintf(t, "s%02d_egrh",    i); saveStateSet      (s, t, sl->eg_rate_h);
        sprintf(t, "s%02d_egrl",    i); saveStateSet      (s, t, sl->eg_rate_l);
        sprintf(t, "s%02d_egsh",    i); saveStateSet      (s, t, sl->eg_shift);
        sprintf(t, "s%02d_egout",   i); saveStateSet      (s, t, (UInt32)(int32_t)sl->eg_out);
        sprintf(t, "s%02d_upd",     i); saveStateSet      (s, t, sl->update_requests);
    }

    saveStateSetBuffer(s, "ch_alg",   p->ch_alg,   sizeof(p->ch_alg));
    saveStateSetBuffer(s, "pan",      p->pan,      sizeof(p->pan));
    saveStateSetBuffer(s, "pan_fine", p->pan_fine, sizeof(p->pan_fine));
    saveStateSet      (s, "mask",     p->mask);
    saveStateSet      (s, "am_mode",  p->am_mode);
    saveStateSet      (s, "pm_mode",  p->pm_mode);
    saveStateSetBuffer(s, "ch_out",   p->ch_out,   sizeof(p->ch_out));
    saveStateSetBuffer(s, "mix_out",  p->mix_out,  sizeof(p->mix_out));
    saveStateSet      (s, "t1cnt",    p->timer1_counter);
    saveStateSet      (s, "t2cnt",    p->timer2_counter);
    saveStateSet      (s, "status",   p->status);

    if (p->adpcm) {
        OPL_ADPCM* a = p->adpcm;
        saveStateSet      (s, "a_clk",    a->clk);
        saveStateSetBuffer(s, "a_reg",    a->reg, sizeof(a->reg));
        saveStateSet      (s, "a_status", a->status);
        saveStateSet      (s, "a_start",  a->start_addr);
        saveStateSet      (s, "a_stop",   a->stop_addr);
        saveStateSet      (s, "a_play",   a->play_addr);
        saveStateSet      (s, "a_delta",  a->delta_addr);
        saveStateSet      (s, "a_dn",     a->delta_n);
        saveStateSet      (s, "a_pamask", a->play_addr_mask);
        saveStateSet      (s, "a_pstart", a->play_start);
        saveStateSet      (s, "a_out0",   (UInt32)a->output[0]);
        saveStateSet      (s, "a_out1",   (UInt32)a->output[1]);
        saveStateSet      (s, "a_diff",   a->diff);
    }

    if (p->conv) {
        int lw = OPL_RateConv_getBufferLength();
        saveStateSet      (s, "conv_present", 1);
        saveStateSetBuffer(s, "conv_buf0",  p->conv->buf[0], lw * sizeof(int16_t));
        saveStateSetBuffer(s, "conv_buf1",  p->conv->buf[1], lw * sizeof(int16_t));
        saveStateSetBuffer(s, "conv_timer", &p->conv->timer, sizeof(p->conv->timer));
    }

    saveStateClose(s);
}

void Emu8950Backend::loadState()
{
    OPL* p = (OPL*)opl;
    SaveState* s = saveStateOpenForRead("emu8950");
    char t[24];
    int i;

    if (!saveStateGet(s, "hasChip", 0)) {
        saveStateClose(s);
        OPL_reset(p);
        loadHadOwnState_ = false;
        return;
    }
    loadHadOwnState_ = true;

    p->clk             = saveStateGet(s, "clk",             p->clk);
    p->rate            = saveStateGet(s, "rate",            p->rate);
    p->chip_type       = (uint8_t) saveStateGet(s, "chip_type",       p->chip_type);
    p->adr             = saveStateGet(s, "adr",             p->adr);
    p->csm_mode        = (uint8_t) saveStateGet(s, "csm_mode",        p->csm_mode);
    p->csm_key_count   = (uint8_t) saveStateGet(s, "csm_key_count",   p->csm_key_count);
    p->notesel         = (uint8_t) saveStateGet(s, "notesel",         p->notesel);
    p->inp_step        = saveStateGet(s, "inp_step",        p->inp_step);
    p->out_step        = saveStateGet(s, "out_step",        p->out_step);
    p->out_time        = saveStateGet(s, "out_time",        p->out_time);
    saveStateGetBuffer(s, "reg",                           p->reg, sizeof(p->reg));
    p->test_flag       = (uint8_t) saveStateGet(s, "test_flag",       p->test_flag);
    p->slot_key_status = saveStateGet(s, "slot_key_status", p->slot_key_status);
    p->rhythm_mode     = (uint8_t) saveStateGet(s, "rhythm_mode",     p->rhythm_mode);
    p->eg_counter      = saveStateGet(s, "eg_counter",      p->eg_counter);
    p->pm_phase        = saveStateGet(s, "pm_phase",        p->pm_phase);
    p->pm_dphase       = saveStateGet(s, "pm_dphase",       p->pm_dphase);
    p->am_phase        = (int32_t) saveStateGet(s, "am_phase",        (UInt32)p->am_phase);
    p->am_dphase       = (int32_t) saveStateGet(s, "am_dphase",       (UInt32)p->am_dphase);
    p->lfo_am          = (uint8_t) saveStateGet(s, "lfo_am",          p->lfo_am);
    p->noise           = saveStateGet(s, "noise",           p->noise);
    p->short_noise     = (uint8_t) saveStateGet(s, "short_noise",     p->short_noise);

    for (i = 0; i < 18; i++) {
        OPL_SLOT* sl = &p->slot[i];
        sprintf(t, "s%02d_number",  i); sl->number          = (uint8_t) saveStateGet(s, t, sl->number);
        sprintf(t, "s%02d_type",    i); sl->type            = (uint8_t) saveStateGet(s, t, sl->type);
        sprintf(t, "s%02d_patch",   i); saveStateGetBuffer(s, t, &sl->__patch, sizeof(OPL_PATCH));
        sprintf(t, "s%02d_out0",    i); sl->output[0]       = (int32_t) saveStateGet(s, t, (UInt32)sl->output[0]);
        sprintf(t, "s%02d_out1",    i); sl->output[1]       = (int32_t) saveStateGet(s, t, (UInt32)sl->output[1]);
        sprintf(t, "s%02d_pgph",    i); sl->pg_phase        = saveStateGet(s, t, sl->pg_phase);
        sprintf(t, "s%02d_pgout",   i); sl->pg_out          = saveStateGet(s, t, sl->pg_out);
        sprintf(t, "s%02d_pgkeep",  i); sl->pg_keep         = (uint8_t) saveStateGet(s, t, sl->pg_keep);
        sprintf(t, "s%02d_blkfnum", i); sl->blk_fnum        = (uint16_t)saveStateGet(s, t, sl->blk_fnum);
        sprintf(t, "s%02d_fnum",    i); sl->fnum            = (uint16_t)saveStateGet(s, t, sl->fnum);
        sprintf(t, "s%02d_blk",     i); sl->blk             = (uint8_t) saveStateGet(s, t, sl->blk);
        sprintf(t, "s%02d_egst",    i); sl->eg_state        = (uint8_t) saveStateGet(s, t, sl->eg_state);
        sprintf(t, "s%02d_tll",     i); sl->tll             = (uint16_t)saveStateGet(s, t, sl->tll);
        sprintf(t, "s%02d_rks",     i); sl->rks             = (uint8_t) saveStateGet(s, t, sl->rks);
        sprintf(t, "s%02d_egrh",    i); sl->eg_rate_h       = (uint8_t) saveStateGet(s, t, sl->eg_rate_h);
        sprintf(t, "s%02d_egrl",    i); sl->eg_rate_l       = (uint8_t) saveStateGet(s, t, sl->eg_rate_l);
        sprintf(t, "s%02d_egsh",    i); sl->eg_shift        = saveStateGet(s, t, sl->eg_shift);
        sprintf(t, "s%02d_egout",   i); sl->eg_out          = (int16_t)(int32_t)saveStateGet(s, t, (UInt32)(int32_t)sl->eg_out);
        sprintf(t, "s%02d_upd",     i); sl->update_requests = saveStateGet(s, t, sl->update_requests);
    }

    saveStateGetBuffer(s, "ch_alg",   p->ch_alg,   sizeof(p->ch_alg));
    saveStateGetBuffer(s, "pan",      p->pan,      sizeof(p->pan));
    saveStateGetBuffer(s, "pan_fine", p->pan_fine, sizeof(p->pan_fine));
    p->mask            = saveStateGet(s, "mask",     p->mask);
    p->am_mode         = (uint8_t)saveStateGet(s, "am_mode",  p->am_mode);
    p->pm_mode         = (uint8_t)saveStateGet(s, "pm_mode",  p->pm_mode);
    saveStateGetBuffer(s, "ch_out",   p->ch_out,   sizeof(p->ch_out));
    saveStateGetBuffer(s, "mix_out",  p->mix_out,  sizeof(p->mix_out));
    p->timer1_counter  = saveStateGet(s, "t1cnt",    p->timer1_counter);
    p->timer2_counter  = saveStateGet(s, "t2cnt",    p->timer2_counter);
    p->status          = (uint8_t)saveStateGet(s, "status",   p->status);

    if (p->conv) {
        if (saveStateGet(s, "conv_present", 0)) {
            int lw = OPL_RateConv_getBufferLength();
            saveStateGetBuffer(s, "conv_buf0",  p->conv->buf[0], lw * sizeof(int16_t));
            saveStateGetBuffer(s, "conv_buf1",  p->conv->buf[1], lw * sizeof(int16_t));
            saveStateGetBuffer(s, "conv_timer", &p->conv->timer, sizeof(p->conv->timer));
        } else {
            OPL_RateConv_reset(p->conv);
        }
    }
    OPL_relinkAfterRestore(p);

    if (p->adpcm) {
        OPL_ADPCM* a = p->adpcm;
        a->clk            = saveStateGet(s, "a_clk",    a->clk);
        saveStateGetBuffer(s, "a_reg",    a->reg, sizeof(a->reg));
        a->status         = (uint8_t)saveStateGet(s, "a_status", a->status);
        a->start_addr     = saveStateGet(s, "a_start",  a->start_addr);
        a->stop_addr      = saveStateGet(s, "a_stop",   a->stop_addr);
        a->play_addr      = saveStateGet(s, "a_play",   a->play_addr);
        a->delta_addr     = saveStateGet(s, "a_delta",  a->delta_addr);
        a->delta_n        = saveStateGet(s, "a_dn",     a->delta_n);
        a->play_addr_mask = saveStateGet(s, "a_pamask", a->play_addr_mask);
        a->play_start     = (uint8_t)saveStateGet(s, "a_pstart", a->play_start);
        a->output[0]      = (int32_t)saveStateGet(s, "a_out0",   (UInt32)a->output[0]);
        a->output[1]      = (int32_t)saveStateGet(s, "a_out1",   (UInt32)a->output[1]);
        a->diff           = saveStateGet(s, "a_diff",   a->diff);

        a->wave = (a->reg[0x08] & 0x01) ? a->memory[1] : a->memory[0];
    }

    saveStateClose(s);
}
