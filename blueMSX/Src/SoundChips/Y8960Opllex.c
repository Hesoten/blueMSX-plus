/*****************************************************************************
**
** Y8960 cartridge - the OPLLEX chip: a YM2413 with four preset ROMs.
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
#include "Y8960Opllex.h"
#include "Y8960OpllCore.h"
#include "Y8960Pan.h"
#include "romMapperY8960.h"
#include "SaveState.h"
#include "DebugDeviceManager.h"
#include "Language.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* 3.579545MHz, the same clock the MSX-MUSIC it replaces gets. */
#define Y8960_OPLLEX_CLOCK 3579545

/* The same factor Emu2413Backend applies, so the block lands at the level of
** the machine's own MSX-MUSIC rather than at one nobody has measured. That
** figure was set by RMS-matching emu2413 against OpenYM2413_2. The balance
** against the other Y8960 blocks is set on top of it, in the mixer block's
** gain (romMapperY8960Mixer.c). */
#define Y8960_OPLLEX_VOLUME (32767 * 9 / 10)
#define Y8960_OPLLEX_VOLUME_DIV 1024

/* No analog post-filter. The machine's MSX-MUSIC runs one with user-settable
** cutoffs, but those belong to that device's settings and the cartridge's
** analog stage is not documented. */

struct Y8960OpllexChip {
    Mixer*        mixer;
    Int32         handle;
    Int32         debugHandle;

    Y8960OPLL*    opll;
    UInt8         address;
    Int32         buffer[AUDIO_STEREO_BUFFER_SIZE];
};

/* Stereo, because the pan registers place each channel; the mixer's own pan
** could only move the block as a whole. */
static Int32* y8960OpllexSync(void* ref, UInt32 count)
{
    Y8960OpllexChip* chip = (Y8960OpllexChip*)ref;
    Int64 gain = (Int64)y8960MixerGain(Y8960_VOL_OPLL) * Y8960_OPLLEX_VOLUME;
    UInt32 i;

    for (i = 0; i < count; i++) {
        int32_t out[2];
        Y8960OPLL_calcStereo(chip->opll, out);
        chip->buffer[2 * i + 0] = (Int32)(out[0] * gain / (Y8960_OPLLEX_VOLUME_DIV * Y8960_GAIN_ONE));
        chip->buffer[2 * i + 1] = (Int32)(out[1] * gain / (Y8960_OPLLEX_VOLUME_DIV * Y8960_GAIN_ONE));
    }

    return chip->buffer;
}

static void y8960OpllexSetSampleRate(void* ref, UInt32 rate)
{
    Y8960OpllexChip* chip = (Y8960OpllexChip*)ref;

    Y8960OPLL_setRate(chip->opll, rate);
}

void y8960OpllexReset(Y8960OpllexChip* chip)
{
    Y8960OPLL_reset(chip->opll);
    chip->address = 0;
}

void y8960OpllexWriteAddress(Y8960OpllexChip* chip, UInt8 address)
{
    chip->address = address;
}

void y8960OpllexWriteData(Y8960OpllexChip* chip, UInt8 data)
{
    mixerSync(chip->mixer);

    Y8960OPLL_writeReg(chip->opll, chip->address, data);
}

/* The chip is saved field by field, as Emu2413Backend saves the machine's
** MSX-MUSIC, and its pointers are rebuilt on load, so sounding notes carry on
** where they were. The ROM voices are not saved: they come from the build
** that loads the state. Neither is the sample rate: the chip keeps the
** mixer's rate of the moment, and the resampler's history and timing are
** restored only when the state was saved at that same rate. */
void y8960OpllexSaveState(Y8960OpllexChip* chip)
{
    SaveState* state = saveStateOpenForWrite("y8960opllex");
    Y8960OPLL snap;
    const Y8960OPLL* p = &snap;
    char t[24];
    int i;

    memcpy(&snap, chip->opll, sizeof(snap));

    saveStateSet      (state, "address", chip->address);
    saveStateSetBuffer(state, "regs",    snap.reg, sizeof(snap.reg));

    saveStateSet      (state, "hasChip",         1);
    saveStateSet      (state, "rate",            p->rate);
    saveStateSet      (state, "out_time",        p->out_time);
    saveStateSet      (state, "adr",             p->adr);
    saveStateSet      (state, "test_flag",       p->test_flag);
    saveStateSet      (state, "slot_key_status", p->slot_key_status);
    saveStateSet      (state, "rhythm_mode",     p->rhythm_mode);
    saveStateSet      (state, "eg_counter",      p->eg_counter);
    saveStateSet      (state, "pm_phase",        p->pm_phase);
    saveStateSet      (state, "am_phase",        (UInt32)p->am_phase);
    saveStateSet      (state, "lfo_am",          p->lfo_am);
    saveStateSet      (state, "noise",           p->noise);
    saveStateSet      (state, "short_noise",     p->short_noise);
    saveStateSetBuffer(state, "patch_number",    snap.patch_number, sizeof(snap.patch_number));
    saveStateSetBuffer(state, "patch_bank",      snap.patch_bank,   sizeof(snap.patch_bank));
    saveStateSetBuffer(state, "user_patch",      snap.user_patch,   sizeof(snap.user_patch));

    for (i = 0; i < 18; i++) {
        const Y8960OPLL_SLOT* sl = &p->slot[i];
        sprintf(t, "s%02d_number",  i); saveStateSet(state, t, sl->number);
        sprintf(t, "s%02d_type",    i); saveStateSet(state, t, sl->type);
        sprintf(t, "s%02d_out0",    i); saveStateSet(state, t, (UInt32)sl->output[0]);
        sprintf(t, "s%02d_out1",    i); saveStateSet(state, t, (UInt32)sl->output[1]);
        sprintf(t, "s%02d_pgph",    i); saveStateSet(state, t, sl->pg_phase);
        sprintf(t, "s%02d_pgout",   i); saveStateSet(state, t, sl->pg_out);
        sprintf(t, "s%02d_pgkeep",  i); saveStateSet(state, t, sl->pg_keep);
        sprintf(t, "s%02d_blkfnum", i); saveStateSet(state, t, sl->blk_fnum);
        sprintf(t, "s%02d_fnum",    i); saveStateSet(state, t, sl->fnum);
        sprintf(t, "s%02d_blk",     i); saveStateSet(state, t, sl->blk);
        sprintf(t, "s%02d_egst",    i); saveStateSet(state, t, sl->eg_state);
        sprintf(t, "s%02d_vol",     i); saveStateSet(state, t, (UInt32)sl->volume);
        sprintf(t, "s%02d_key",     i); saveStateSet(state, t, sl->key_flag);
        sprintf(t, "s%02d_sus",     i); saveStateSet(state, t, sl->sus_flag);
        sprintf(t, "s%02d_tll",     i); saveStateSet(state, t, sl->tll);
        sprintf(t, "s%02d_rks",     i); saveStateSet(state, t, sl->rks);
        sprintf(t, "s%02d_egrh",    i); saveStateSet(state, t, sl->eg_rate_h);
        sprintf(t, "s%02d_egrl",    i); saveStateSet(state, t, sl->eg_rate_l);
        sprintf(t, "s%02d_egsh",    i); saveStateSet(state, t, sl->eg_shift);
        sprintf(t, "s%02d_egout",   i); saveStateSet(state, t, sl->eg_out);
        sprintf(t, "s%02d_upd",     i); saveStateSet(state, t, sl->update_requests);
    }

    saveStateSetBuffer(state, "pan",      snap.pan,      sizeof(snap.pan));
    saveStateSetBuffer(state, "pan_fine", snap.pan_fine, sizeof(snap.pan_fine));
    saveStateSet      (state, "mask",     p->mask);
    saveStateSetBuffer(state, "ch_out",   snap.ch_out,   sizeof(snap.ch_out));
    saveStateSetBuffer(state, "mix_out",  snap.mix_out,  sizeof(snap.mix_out));

    /* The resampler's buffers are not in the snapshot (it holds pointers),
    ** so they are read from the chip itself. */
    if (chip->opll->conv && chip->opll->conv->ch == 2) {
        const Y8960OPLL_RateConv* conv = chip->opll->conv;
        int lw = Y8960OPLL_RateConv_getBufferLength();
        saveStateSet      (state, "conv_present", 1);
        saveStateSet      (state, "conv_timer",   conv->timer);
        saveStateSet      (state, "conv_pos0",    (UInt32)conv->pos[0]);
        saveStateSet      (state, "conv_pos1",    (UInt32)conv->pos[1]);
        saveStateSetBuffer(state, "conv_buf0",    conv->buf[0], 2 * lw * sizeof(int16_t));
        saveStateSetBuffer(state, "conv_buf1",    conv->buf[1], 2 * lw * sizeof(int16_t));
    }

    saveStateClose(state);
}

/* States from before the chip was saved field by field hold the registers
** only. They are replayed in ascending order, which is what software would
** have written: a channel's f-number low byte (10h-18h) is already in place
** when the block and key-on bits (20h-28h) arrive, and the bank registers
** re-point the voice afterwards. Notes restart from their attack. 19h-1Fh,
** 29h-2Fh and 39h-3Fh are skipped: the core folds them onto the channel
** registers nine below, so replaying the zeros they hold would clear those. */
static void replayRegisters(Y8960OpllexChip* chip, const UInt8* regs)
{
    int i;

    Y8960OPLL_reset(chip->opll);
    for (i = 0; i < Y8960OPLL_REG_COUNT; i++) {
        if (0x10 <= i && i < 0x40 && (i & 0x0F) >= 9) continue;
        Y8960OPLL_writeReg(chip->opll, i, regs[i]);
    }
}

void y8960OpllexLoadState(Y8960OpllexChip* chip)
{
    SaveState* state = saveStateOpenForRead("y8960opllex");
    Y8960OPLL* p = chip->opll;
    UInt8 regs[Y8960OPLL_REG_COUNT];
    char t[24];
    int i;

    memset(regs, 0, sizeof(regs));
    /* A state from before the pan registers existed is shorter than this
    ** buffer and leaves them untouched; the centre is what it was playing at. */
    memset(regs + Y8960OPLL_PAN_REG_BASE, Y8960_PAN_CENTER, 9);

    chip->address = (UInt8)saveStateGet(state, "address", 0);
    saveStateGetBuffer(state, "regs", regs, sizeof(regs));

    if (!saveStateGet(state, "hasChip", 0)) {
        saveStateClose(state);
        replayRegisters(chip, regs);
        return;
    }

    /* From a clean chip: the ROM voices reloaded, the resampler emptied. */
    Y8960OPLL_reset(p);

    memcpy(p->reg, regs, sizeof(p->reg));
    p->adr             =          saveStateGet(state, "adr",             p->adr);
    p->test_flag       = (uint8_t)saveStateGet(state, "test_flag",       p->test_flag);
    p->slot_key_status =          saveStateGet(state, "slot_key_status", p->slot_key_status);
    p->rhythm_mode     = (uint8_t)saveStateGet(state, "rhythm_mode",     p->rhythm_mode);
    p->eg_counter      =          saveStateGet(state, "eg_counter",      p->eg_counter);
    p->pm_phase        =          saveStateGet(state, "pm_phase",        p->pm_phase);
    p->am_phase        = (int32_t)saveStateGet(state, "am_phase",        (UInt32)p->am_phase);
    p->lfo_am          = (uint8_t)saveStateGet(state, "lfo_am",          p->lfo_am);
    p->noise           =          saveStateGet(state, "noise",           p->noise);
    p->short_noise     = (uint8_t)saveStateGet(state, "short_noise",     p->short_noise);
    saveStateGetBuffer(state, "patch_number", p->patch_number, sizeof(p->patch_number));
    saveStateGetBuffer(state, "patch_bank",   p->patch_bank,   sizeof(p->patch_bank));
    saveStateGetBuffer(state, "user_patch",   p->user_patch,   sizeof(p->user_patch));

    for (i = 0; i < 18; i++) {
        Y8960OPLL_SLOT* sl = &p->slot[i];
        sprintf(t, "s%02d_number",  i); sl->number          = (uint8_t) saveStateGet(state, t, sl->number);
        sprintf(t, "s%02d_type",    i); sl->type            = (uint8_t) saveStateGet(state, t, sl->type);
        sprintf(t, "s%02d_out0",    i); sl->output[0]       = (int32_t) saveStateGet(state, t, (UInt32)sl->output[0]);
        sprintf(t, "s%02d_out1",    i); sl->output[1]       = (int32_t) saveStateGet(state, t, (UInt32)sl->output[1]);
        sprintf(t, "s%02d_pgph",    i); sl->pg_phase        =           saveStateGet(state, t, sl->pg_phase);
        sprintf(t, "s%02d_pgout",   i); sl->pg_out          =           saveStateGet(state, t, sl->pg_out);
        sprintf(t, "s%02d_pgkeep",  i); sl->pg_keep         = (uint8_t) saveStateGet(state, t, sl->pg_keep);
        sprintf(t, "s%02d_blkfnum", i); sl->blk_fnum        = (uint16_t)saveStateGet(state, t, sl->blk_fnum);
        sprintf(t, "s%02d_fnum",    i); sl->fnum            = (uint16_t)saveStateGet(state, t, sl->fnum);
        sprintf(t, "s%02d_blk",     i); sl->blk             = (uint8_t) saveStateGet(state, t, sl->blk);
        sprintf(t, "s%02d_egst",    i); sl->eg_state        = (uint8_t) saveStateGet(state, t, sl->eg_state);
        sprintf(t, "s%02d_vol",     i); sl->volume          = (int32_t) saveStateGet(state, t, (UInt32)sl->volume);
        sprintf(t, "s%02d_key",     i); sl->key_flag        = (uint8_t) saveStateGet(state, t, sl->key_flag);
        sprintf(t, "s%02d_sus",     i); sl->sus_flag        = (uint8_t) saveStateGet(state, t, sl->sus_flag);
        sprintf(t, "s%02d_tll",     i); sl->tll             = (uint16_t)saveStateGet(state, t, sl->tll);
        sprintf(t, "s%02d_rks",     i); sl->rks             = (uint8_t) saveStateGet(state, t, sl->rks);
        sprintf(t, "s%02d_egrh",    i); sl->eg_rate_h       = (uint8_t) saveStateGet(state, t, sl->eg_rate_h);
        sprintf(t, "s%02d_egrl",    i); sl->eg_rate_l       = (uint8_t) saveStateGet(state, t, sl->eg_rate_l);
        sprintf(t, "s%02d_egsh",    i); sl->eg_shift        =           saveStateGet(state, t, sl->eg_shift);
        sprintf(t, "s%02d_egout",   i); sl->eg_out          =           saveStateGet(state, t, sl->eg_out);
        sprintf(t, "s%02d_upd",     i); sl->update_requests =           saveStateGet(state, t, sl->update_requests);
    }

    saveStateGetBuffer(state, "pan",      p->pan,      sizeof(p->pan));
    saveStateGetBuffer(state, "pan_fine", p->pan_fine, sizeof(p->pan_fine));
    p->mask = saveStateGet(state, "mask", p->mask);
    saveStateGetBuffer(state, "ch_out",   p->ch_out,   sizeof(p->ch_out));
    saveStateGetBuffer(state, "mix_out",  p->mix_out,  sizeof(p->mix_out));

    if (saveStateGet(state, "rate", 0) == p->rate &&
        saveStateGet(state, "conv_present", 0) && p->conv && p->conv->ch == 2) {
        Y8960OPLL_RateConv* conv = p->conv;
        int lw = Y8960OPLL_RateConv_getBufferLength();
        p->out_time = saveStateGet(state, "out_time", p->out_time);
        conv->timer = saveStateGet(state, "conv_timer", conv->timer);
        conv->pos[0] = (int)(saveStateGet(state, "conv_pos0", 0) % (UInt32)lw);
        conv->pos[1] = (int)(saveStateGet(state, "conv_pos1", 0) % (UInt32)lw);
        saveStateGetBuffer(state, "conv_buf0", conv->buf[0], 2 * lw * sizeof(int16_t));
        saveStateGetBuffer(state, "conv_buf1", conv->buf[1], 2 * lw * sizeof(int16_t));
    }

    saveStateClose(state);

    Y8960OPLL_relinkAfterRestore(p);
}

static void getDebugInfo(Y8960OpllexChip* chip, DbgDevice* dbgDevice)
{
    /* 00h-07h user voice, 0Eh rhythm, 10h-18h / 20h-28h / 30h-38h per channel,
    ** 40h-48h bank, 50h-58h pan. The gaps are not registers. */
    static const UInt8 regsAvail[] = {
        1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1
    };
    DbgRegisterBank* regBank;
    int r;
    int count = 0;

    for (r = 0; r < (int)sizeof(regsAvail); r++) {
        count += regsAvail[r];
    }

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegs(), count);

    count = 0;
    for (r = 0; r < (int)sizeof(regsAvail); r++) {
        if (regsAvail[r]) {
            char name[8];
            sprintf(name, "R%.2x", r);
            dbgRegisterBankAddRegister(regBank, count++, name, 8, chip->opll->reg[r]);
        }
    }
}

Y8960OpllexChip* y8960OpllexCreate(Mixer* mixer, const char* name)
{
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    Y8960OpllexChip* chip = (Y8960OpllexChip*)calloc(1, sizeof(Y8960OpllexChip));

    chip->mixer = mixer;
    chip->opll  = Y8960OPLL_new(Y8960_OPLLEX_CLOCK, mixerGetSampleRate(mixer));

    chip->handle      = mixerRegisterChannel(mixer, MIXER_CHANNEL_Y8960, 1,
                                             y8960OpllexSync, y8960OpllexSetSampleRate, chip);
    chip->debugHandle = debugDeviceRegister(DBGTYPE_AUDIO, name, &dbgCallbacks, chip);

    y8960OpllexReset(chip);

    return chip;
}

void y8960OpllexDestroy(Y8960OpllexChip* chip)
{
    debugDeviceUnregister(chip->debugHandle);
    mixerUnregisterChannel(chip->mixer, chip->handle);

    Y8960OPLL_delete(chip->opll);

    free(chip);
}
