/*****************************************************************************
**
** Ported from openMSX's latest Y8950 (MSX-Audio); the upstream
** sources are kept under OpenMsxY8950Latest/ as the reference.
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
/* openMSX Y8950 port; openMSX deps stubbed in OpenMsxY8950Latest.h. */

#include "OpenMsxY8950Latest.h"
#include "OpenMsxY8950LatestAdpcm.h"

extern "C" {
#include "../Utils/SaveState.h"
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace openmsx {

static constexpr unsigned EG_MUTE = 1u << Y8950::EG_BITS;
static const Y8950::EnvPhaseIndex EG_DP_MAX = Y8950::EnvPhaseIndex(int(EG_MUTE));

static constexpr unsigned MOD = 0;
static constexpr unsigned CAR = 1;

static constexpr double EG_STEP = 0.1875;
static constexpr double SL_STEP = 3.0;
static constexpr double TL_STEP = 0.75;
static constexpr double DB_STEP = 0.1875;

static constexpr unsigned SL_PER_EG = 16;
static constexpr unsigned TL_PER_EG =  4;
static constexpr unsigned EG_PER_DB =  1;

static constexpr double PM_SPEED  = 6.4;
static constexpr double PM_DEPTH  = 13.75 / 2;
static constexpr double PM_DEPTH2 = 13.75;

static constexpr int SL_BITS = 4;
static constexpr int SL_MUTE = 1 << SL_BITS;
static constexpr int PG_BITS = 10;
static constexpr int PG_WIDTH = 1 << PG_BITS;
static constexpr int PG_MASK = PG_WIDTH - 1;
static constexpr int DP_BITS = 19;
static constexpr int DP_BASE_BITS = DP_BITS - PG_BITS;

static constexpr int DB_BITS = 9;
static constexpr int DB_MUTE = 1 << DB_BITS;
static constexpr int PM_AMP_BITS = 8;
static constexpr int PM_AMP = 1 << PM_AMP_BITS;

static constexpr int DB2LIN_AMP_BITS = 11;
static constexpr int SLOT_AMP_BITS = DB2LIN_AMP_BITS;

static constexpr int PM_PG_BITS = 8;
static constexpr int PM_PG_WIDTH = 1 << PM_PG_BITS;
static constexpr int PM_DP_BITS = 16;
static constexpr int PM_DP_WIDTH = 1 << PM_DP_BITS;
static constexpr int AM_PG_BITS = 8;
static constexpr int AM_PG_WIDTH = 1 << AM_PG_BITS;
static constexpr int AM_DP_BITS = 16;

static constexpr unsigned PM_DPHASE = unsigned(
    PM_SPEED * PM_DP_WIDTH / (Y8950::CLOCK_FREQ / double(Y8950::CLOCK_FREQ_DIV)));

static constexpr unsigned LFO_AM_TAB_ELEMENTS = 210;
static constexpr std::array<int8_t, LFO_AM_TAB_ELEMENTS> lfo_am_table = {
    0,0,0,0,0,0,0,
    1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5, 6,6,6,6, 7,7,7,7,
    8,8,8,8, 9,9,9,9, 10,10,10,10, 11,11,11,11, 12,12,12,12, 13,13,13,13,
    14,14,14,14, 15,15,15,15, 16,16,16,16, 17,17,17,17, 18,18,18,18,
    19,19,19,19, 20,20,20,20, 21,21,21,21, 22,22,22,22, 23,23,23,23,
    24,24,24,24, 25,25,25,25, 26,26,26,
    25,25,25,25, 24,24,24,24, 23,23,23,23, 22,22,22,22, 21,21,21,21,
    20,20,20,20, 19,19,19,19, 18,18,18,18, 17,17,17,17, 16,16,16,16,
    15,15,15,15, 14,14,14,14, 13,13,13,13, 12,12,12,12, 11,11,11,11,
    10,10,10,10, 9,9,9,9, 8,8,8,8, 7,7,7,7, 6,6,6,6, 5,5,5,5,
    4,4,4,4, 3,3,3,3, 2,2,2,2, 1,1,1,1
};

static constexpr unsigned DB_POS(int x) {
    int result = int(x / DB_STEP);
    return unsigned(result);
}
static constexpr unsigned DB_NEG(int x) {
    return 2 * DB_MUTE + DB_POS(x);
}

/* Tables that need runtime math (std::log/pow/sin/exp2/log10) cannot be
** constexpr until C++26 -- materialise them once at static-init time. */
static const std::array<unsigned, EG_MUTE> adjustAR = []{
    std::array<unsigned, EG_MUTE> r{};
    r[0] = EG_MUTE;
    double log_eg_mute = std::log(double(EG_MUTE));
    for (int i = 1; i < int(EG_MUTE); ++i) {
        r[i] = unsigned((EG_MUTE - 1 - EG_MUTE * std::log(double(i)) / log_eg_mute) / 2);
    }
    return r;
}();
static const std::array<unsigned, EG_MUTE + 1> adjustRA = []{
    std::array<unsigned, EG_MUTE + 1> r{};
    r[0] = EG_MUTE;
    for (int i = 1; i < int(EG_MUTE); ++i) {
        r[i] = unsigned(std::pow(double(EG_MUTE), (double(EG_MUTE) - 1 - 2 * i) / EG_MUTE));
    }
    r[EG_MUTE] = 0;
    return r;
}();

static const std::array<int, (2 * DB_MUTE) * 2> dB2LinTab = []{
    std::array<int, (2 * DB_MUTE) * 2> r{};
    for (int i = 0; i < DB_MUTE; ++i) {
        r[i] = int(double((1 << DB2LIN_AMP_BITS) - 1) *
                   std::pow(10.0, -double(i) * DB_STEP / 20.0));
    }
    for (int i = DB_MUTE; i < 2 * DB_MUTE; ++i) {
        r[i] = 0;
    }
    for (int i = 0; i < 2 * DB_MUTE; ++i) {
        r[i + 2 * DB_MUTE] = -r[i];
    }
    return r;
}();

static const std::array<unsigned, PG_WIDTH> sinTable = []{
    auto lin2db = [](double d) {
        if (d < 1e-4) return DB_MUTE - 1;
        int tmp = -int(20.0 * std::log10(d) / DB_STEP);
        return std::min(tmp, DB_MUTE - 1);
    };
    std::array<unsigned, PG_WIDTH> r{};
    for (int i = 0; i < PG_WIDTH / 4; ++i) {
        r[i] = unsigned(lin2db(std::sin(2.0 * Math::pi * i / PG_WIDTH)));
    }
    for (int i = 0; i < PG_WIDTH / 4; ++i) {
        r[PG_WIDTH / 2 - 1 - i] = r[i];
    }
    for (int i = 0; i < PG_WIDTH / 2; ++i) {
        r[PG_WIDTH / 2 + i] = 2 * DB_MUTE + r[i];
    }
    return r;
}();

static const std::array<std::array<int, PM_PG_WIDTH>, 2> pmTable = []{
    std::array<std::array<int, PM_PG_WIDTH>, 2> r{};
    for (int i = 0; i < PM_PG_WIDTH; ++i) {
        double s = std::sin(2.0 * Math::pi * i / PM_PG_WIDTH) / 1200;
        r[0][i] = int(PM_AMP * std::exp2(PM_DEPTH  * s));
        r[1][i] = int(PM_AMP * std::exp2(PM_DEPTH2 * s));
    }
    return r;
}();

static constexpr auto tllTable = []{
    constexpr std::array<int, 16> klTable = {
        0, 24, 32, 37, 40, 43, 45, 47, 48, 50, 51, 52, 53, 54, 55, 56
    };
    constexpr std::array<int, 4> shift = { 31, 1, 2, 0 };
    std::array<std::array<int, 4>, 16 * 8> r{};
    for (int freq = 0; freq < 16 * 8; ++freq) {
        int fnum  = freq % 16;
        int block = freq / 16;
        int tmp = 4 * klTable[fnum] - 32 * (7 - block);
        for (int KL = 0; KL < 4; ++KL) {
            r[freq][KL] = (tmp <= 0) ? 0 : (tmp >> shift[KL]);
        }
    }
    return r;
}();

static constexpr auto dPhaseArTable = []{
    std::array<std::array<Y8950::EnvPhaseIndex, 16>, 16> r{};
    for (int Rks = 0; Rks < 16; ++Rks) {
        r[Rks][0] = Y8950::EnvPhaseIndex(0);
        for (int AR = 1; AR < 15; ++AR) {
            int RM = (AR + (Rks >> 2) > 15) ? 15 : (AR + (Rks >> 2));
            int RL = Rks & 3;
            r[Rks][AR] = Y8950::EnvPhaseIndex(12 * (RL + 4)) >> (15 - RM);
        }
        r[Rks][15] = Y8950::EnvPhaseIndex(int(EG_MUTE));
    }
    return r;
}();

static constexpr auto dPhaseDrTable = []{
    std::array<std::array<Y8950::EnvPhaseIndex, 16>, 16> r{};
    for (int Rks = 0; Rks < 16; ++Rks) {
        r[Rks][0] = Y8950::EnvPhaseIndex(0);
        for (int DR = 1; DR < 16; ++DR) {
            int RM = (DR + (Rks >> 2) > 15) ? 15 : (DR + (Rks >> 2));
            int RL = Rks & 3;
            r[Rks][DR] = Y8950::EnvPhaseIndex(RL + 4) >> (15 - RM);
        }
    }
    return r;
}();

/* class Y8950::Patch ----------------------------------------------- */

Y8950::Patch::Patch() { reset(); }

void Y8950::Patch::reset()
{
    AM = false; PM = false; EG = false;
    ML = 0; KL = 0; TL = 0;
    AR = 0; DR = 0; SL = 0; RR = 0;
    setKeyScaleRate(false);
    setFeedbackShift(0);
}

/* class Y8950::Slot ------------------------------------------------ */

Y8950::Slot::Slot()
    : dPhaseARTableRks(dPhaseArTable[0])
    , dPhaseDRTableRks(dPhaseDrTable[0])
{
}

void Y8950::Slot::reset()
{
    phase = 0;
    output = 0;
    feedback = 0;
    eg_mode = EnvelopeState::FINISH;
    eg_phase = EG_DP_MAX;
    key = 0;
    patch.reset();
    updateAll(0);
}

void Y8950::Slot::updatePG(unsigned freq)
{
    static constexpr std::array<int, 16> mlTable = {
          1, 1*2,  2*2,  3*2,  4*2,  5*2,  6*2,  7*2,
        8*2, 9*2, 10*2, 10*2, 12*2, 12*2, 15*2, 15*2
    };
    unsigned fnum  = freq % 1024;
    unsigned block = freq / 1024;
    dPhase = ((fnum * mlTable[patch.ML]) << block) >> (21 - DP_BITS);
}

void Y8950::Slot::updateTLL(unsigned freq)
{
    tll = tllTable[freq >> 6][patch.KL] + int(patch.TL * TL_PER_EG);
}

void Y8950::Slot::updateRKS(unsigned freq)
{
    unsigned rks = freq >> patch.KR;
    dPhaseARTableRks = dPhaseArTable[rks];
    dPhaseDRTableRks = dPhaseDrTable[rks];
}

void Y8950::Slot::updateEG()
{
    switch (eg_mode) {
    case EnvelopeState::ATTACK:  eg_dPhase = dPhaseARTableRks[patch.AR]; break;
    case EnvelopeState::DECAY:   eg_dPhase = dPhaseDRTableRks[patch.DR]; break;
    case EnvelopeState::SUSTAIN:
    case EnvelopeState::RELEASE: eg_dPhase = dPhaseDRTableRks[patch.RR]; break;
    case EnvelopeState::FINISH:  eg_dPhase = Y8950::EnvPhaseIndex(0);    break;
    }
}

void Y8950::Slot::updateAll(unsigned freq)
{
    updatePG(freq);
    updateTLL(freq);
    updateRKS(freq);
    updateEG();
}

bool Y8950::Slot::isActive() const
{
    return eg_mode != EnvelopeState::FINISH;
}

void Y8950::Slot::slotOn(KeyPart part)
{
    if (!key) {
        eg_mode = EnvelopeState::ATTACK;
        phase = 0;
        eg_phase = Y8950::EnvPhaseIndex(int(adjustRA[eg_phase.toInt()]));
    }
    key |= part;
}

void Y8950::Slot::slotOff(KeyPart part)
{
    if (key) {
        key &= ~part;
        if (!key) {
            if (eg_mode == EnvelopeState::ATTACK) {
                eg_phase = Y8950::EnvPhaseIndex(int(adjustAR[eg_phase.toInt()]));
            }
            eg_mode = EnvelopeState::RELEASE;
        }
    }
}

/* class Y8950::Channel --------------------------------------------- */

Y8950::Channel::Channel() { reset(); }

void Y8950::Channel::reset()
{
    setFreq(0);
    slot[MOD].reset();
    slot[CAR].reset();
    alg = false;
}

void Y8950::Channel::setFreq(unsigned freq_) { freq = freq_; }

void Y8950::Channel::keyOn(KeyPart part)
{
    slot[MOD].slotOn(part);
    slot[CAR].slotOn(part);
}

void Y8950::Channel::keyOff(KeyPart part)
{
    slot[MOD].slotOff(part);
    slot[CAR].slotOff(part);
}

static constexpr auto INPUT_RATE = unsigned(
    (Y8950::CLOCK_FREQ + Y8950::CLOCK_FREQ_DIV / 2) / Y8950::CLOCK_FREQ_DIV);

/* class Y8950 ------------------------------------------------------ */

Y8950::Y8950(const std::string& name_, const DeviceConfig& config,
             unsigned sampleRam, EmuTime time, MSXAudio& audio)
    : ResampledSoundDevice(config.getMotherBoard(), name_, "MSX-AUDIO", 9 + 5 + 1, INPUT_RATE, false)
    , motherBoard(config.getMotherBoard())
    , periphery(audio.createPeriphery(getName()))
    , adpcm(std::make_unique<Y8950Adpcm>(*this, config, name_, sampleRam))
    , connector(motherBoard.getPluggingController())
    , dac13(name_ + " DAC", "MSX-AUDIO 13-bit DAC", config)
    , timer1(EmuTimer::createOPL3_1(motherBoard.getScheduler(), *this))
    , timer2(EmuTimer::createOPL3_2(motherBoard.getScheduler(), *this))
    , irq(motherBoard, getName() + ".IRQ")
    , pm_phase(0), am_phase(0)
    , noise_seed(0xffff)
    , noiseA_phase(0), noiseB_phase(0)
    , noiseA_dPhase(0), noiseB_dPhase(0)
    , status(0), statusMask(0)
    , rythm_mode(false), am_mode(false), pm_mode(false)
    , enabled(true)
{
    reset(time);
    registerSound(config);
}

Y8950::~Y8950()
{
    unregisterSound();
}

void Y8950::clearRam()
{
    adpcm->clearRam();
}

void Y8950::reset(EmuTime time)
{
    for (auto& c : ch) c.reset();

    rythm_mode = false;
    am_mode = false;
    pm_mode = false;
    pm_phase = 0;
    am_phase = 0;
    noise_seed = 0xffff;
    noiseA_phase = 0;
    noiseB_phase = 0;
    noiseA_dPhase = 0;
    noiseB_dPhase = 0;

    updateStream(time);
    std::fill(reg.begin(), reg.end(), uint8_t(0));

    reg[0x04] = 0x18;
    reg[0x19] = 0x0F;
    status = 0x00;
    statusMask = 0;
    irq.reset();
	
    adpcm->reset(time);
}

void Y8950::keyOn_BD()  { ch[6].keyOn(KEY_RHYTHM); }
void Y8950::keyOn_HH()  { ch[7].slot[MOD].slotOn(KEY_RHYTHM); }
void Y8950::keyOn_SD()  { ch[7].slot[CAR].slotOn(KEY_RHYTHM); }
void Y8950::keyOn_TOM() { ch[8].slot[MOD].slotOn(KEY_RHYTHM); }
void Y8950::keyOn_CYM() { ch[8].slot[CAR].slotOn(KEY_RHYTHM); }

void Y8950::keyOff_BD() { ch[6].keyOff(KEY_RHYTHM); }
void Y8950::keyOff_HH() { ch[7].slot[MOD].slotOff(KEY_RHYTHM); }
void Y8950::keyOff_SD() { ch[7].slot[CAR].slotOff(KEY_RHYTHM); }
void Y8950::keyOff_TOM(){ ch[8].slot[MOD].slotOff(KEY_RHYTHM); }
void Y8950::keyOff_CYM(){ ch[8].slot[CAR].slotOff(KEY_RHYTHM); }

void Y8950::setRythmMode(int data)
{
    bool newMode = (data & 32) != 0;
    if (rythm_mode != newMode) {
        rythm_mode = newMode;
        if (!rythm_mode) {
            keyOff_BD();
            keyOff_HH();
            keyOff_SD();
            keyOff_TOM();
            keyOff_CYM();
        }
    }
}

void Y8950::update_key_status()
{
    for (int i = 0; i < (int)ch.size(); ++i) {
        auto& c = ch[i];
        uint8_t main = (reg[0xb0 + i] & 0x20) ? KEY_MAIN : 0;
        c.slot[MOD].key = main;
        c.slot[CAR].key = main;
    }
    if (rythm_mode) {
        ch[6].slot[MOD].key |= uint8_t((reg[0xbd] & 0x10) ? KEY_RHYTHM : 0);
        ch[6].slot[CAR].key |= uint8_t((reg[0xbd] & 0x10) ? KEY_RHYTHM : 0);
        ch[7].slot[MOD].key |= uint8_t((reg[0xbd] & 0x01) ? KEY_RHYTHM : 0);
        ch[7].slot[CAR].key |= uint8_t((reg[0xbd] & 0x08) ? KEY_RHYTHM : 0);
        ch[8].slot[MOD].key |= uint8_t((reg[0xbd] & 0x04) ? KEY_RHYTHM : 0);
        ch[8].slot[CAR].key |= uint8_t((reg[0xbd] & 0x02) ? KEY_RHYTHM : 0);
    }
}

/* Generate wave data ----------------------------------------------- */

static constexpr int wave2_8pi(int e)
{
    int shift = SLOT_AMP_BITS - PG_BITS - 2;
    return (shift > 0) ? (e >> shift) : (e << -shift);
}

unsigned Y8950::Slot::calc_phase(int lfo_pm)
{
    if (patch.PM) {
        phase += (dPhase * lfo_pm) >> PM_AMP_BITS;
    } else {
        phase += dPhase;
    }
    return phase >> DP_BASE_BITS;
}

static constexpr Y8950::EnvPhaseIndex S2E(int x) {
    return Y8950::EnvPhaseIndex(int(x / EG_STEP));
}
static constexpr std::array<Y8950::EnvPhaseIndex, 16> SL = {
    S2E( 0), S2E( 3), S2E( 6), S2E( 9), S2E(12), S2E(15), S2E(18), S2E(21),
    S2E(24), S2E(27), S2E(30), S2E(33), S2E(36), S2E(39), S2E(42), S2E(93)
};
	
unsigned Y8950::Slot::calc_envelope(int lfo_am)
{
    unsigned egOut = 0;
    switch (eg_mode) {
    case EnvelopeState::ATTACK:
        eg_phase += eg_dPhase;
        if (eg_phase >= EG_DP_MAX) {
            egOut = 0;
            eg_phase = Y8950::EnvPhaseIndex(0);
            eg_mode = EnvelopeState::DECAY;
            updateEG();
        } else {
            egOut = adjustAR[eg_phase.toInt()];
        }
        break;
    case EnvelopeState::DECAY:
        eg_phase += eg_dPhase;
        if (eg_phase >= SL[patch.SL]) {
            eg_phase = SL[patch.SL];
            eg_mode = EnvelopeState::SUSTAIN;
            updateEG();
        }
        egOut = unsigned(eg_phase.toInt());
        break;
    case EnvelopeState::SUSTAIN:
        if (!patch.EG) {
            eg_phase += eg_dPhase;
        }
        egOut = unsigned(eg_phase.toInt());
        if (egOut >= EG_MUTE) {
            eg_phase = EG_DP_MAX;
            eg_mode = EnvelopeState::FINISH;
            egOut = EG_MUTE - 1;
        }
        break;
    case EnvelopeState::RELEASE:
        eg_phase += eg_dPhase;
        egOut = unsigned(eg_phase.toInt());
        if (egOut >= EG_MUTE) {
            eg_phase = EG_DP_MAX;
            eg_mode = EnvelopeState::FINISH;
            egOut = EG_MUTE - 1;
        }
        break;
    case EnvelopeState::FINISH:
        egOut = EG_MUTE - 1;
        break;
    }

    egOut = ((egOut + unsigned(tll)) * EG_PER_DB);
    if (patch.AM) {
        egOut += unsigned(lfo_am);
    }
    if (egOut >= unsigned(DB_MUTE - 1)) egOut = unsigned(DB_MUTE - 1);
    return egOut;
}

int Y8950::Slot::calc_slot_car(int lfo_pm, int lfo_am, int fm)
{
    unsigned egOut = calc_envelope(lfo_am);
    int pgout = int(calc_phase(lfo_pm)) + wave2_8pi(fm);
    return dB2LinTab[sinTable[unsigned(pgout) & PG_MASK] + egOut];
}

int Y8950::Slot::calc_slot_mod(int lfo_pm, int lfo_am)
{
    unsigned egOut = calc_envelope(lfo_am);
    unsigned pgout = calc_phase(lfo_pm);

    if (patch.FB != 0) {
        pgout += wave2_8pi(feedback) >> patch.FB;
    }
    int newOutput = dB2LinTab[sinTable[pgout & PG_MASK] + egOut];
    feedback = (output + newOutput) >> 1;
    output = newOutput;
    return feedback;
}

int Y8950::Slot::calc_slot_tom(int lfo_pm, int lfo_am)
{
    unsigned egOut = calc_envelope(lfo_am);
    unsigned pgout = calc_phase(lfo_pm);
    return dB2LinTab[sinTable[pgout & PG_MASK] + egOut];
}

int Y8950::Slot::calc_slot_snare(int lfo_pm, int lfo_am, int whiteNoise)
{
    unsigned egOut = calc_envelope(lfo_am);
    unsigned pgout = calc_phase(lfo_pm);
    unsigned tmp = (pgout & (1 << (PG_BITS - 1))) ? 0 : 2 * DB_MUTE;
    return (dB2LinTab[tmp + egOut] + dB2LinTab[egOut + unsigned(whiteNoise)]) >> 1;
}

int Y8950::Slot::calc_slot_cym(int lfo_am, int a, int b)
{
    unsigned egOut = calc_envelope(lfo_am);
    return (dB2LinTab[egOut + unsigned(a)] + dB2LinTab[egOut + unsigned(b)]) >> 1;
}

int Y8950::Slot::calc_slot_hat(int lfo_am, int a, int b, int whiteNoise)
{
    unsigned egOut = calc_envelope(lfo_am);
    return (dB2LinTab[egOut + unsigned(whiteNoise)] +
            dB2LinTab[egOut + unsigned(a)] +
            dB2LinTab[egOut + unsigned(b)]) >> 2;
}

float Y8950::getAmplificationFactorImpl() const
{
    return 1.0f / (1 << DB2LIN_AMP_BITS);
}

void Y8950::setEnabled(bool enabled_, EmuTime time)
{
    updateStream(time);
    enabled = enabled_;
}

bool Y8950::checkMuteHelper()
{
    if (!enabled) return true;

    for (int i = 0; i < 6; ++i) {
        if (ch[i].slot[CAR].isActive()) return false;
    }
    if (!rythm_mode) {
        for (int i = 6; i < 9; ++i) {
            if (ch[i].slot[CAR].isActive()) return false;
        }
    } else {
        if (ch[6].slot[CAR].isActive()) return false;
        if (ch[7].slot[MOD].isActive()) return false;
        if (ch[7].slot[CAR].isActive()) return false;
        if (ch[8].slot[MOD].isActive()) return false;
        if (ch[8].slot[CAR].isActive()) return false;
    }
    return adpcm->isMuted();
}

void Y8950::generateChannels(std::span<float*> bufs, unsigned num)
{
    if (checkMuteHelper()) {
        for (auto& p : bufs) p = nullptr;
        return;
    }

    for (unsigned sample = 0; sample < num; ++sample) {
        ++am_phase;
        if (am_phase == (LFO_AM_TAB_ELEMENTS * 64)) am_phase = 0;
        int tmp = int(lfo_am_table[am_phase / 64]);
        int lfo_am = am_mode ? tmp : tmp / 4;

        pm_phase = (pm_phase + PM_DPHASE) & (PM_DP_WIDTH - 1);
        int lfo_pm = pmTable[pm_mode ? 1 : 0][pm_phase >> (PM_DP_BITS - PM_PG_BITS)];

        if (noise_seed & 1) {
            noise_seed ^= 0x24000;
        }
        noise_seed >>= 1;
        int whiteNoise = noise_seed & 1 ? int(DB_POS(6)) : int(DB_NEG(6));

        noiseA_phase += noiseA_dPhase;
        noiseA_phase &= (0x40 << 11) - 1;
        if ((noiseA_phase >> 11) == 0x3f) noiseA_phase = 0;
        int noiseA = noiseA_phase & (0x03 << 11) ? int(DB_POS(6)) : int(DB_NEG(6));

        noiseB_phase += noiseB_dPhase;
        noiseB_phase &= (0x10 << 11) - 1;
        int noiseB = noiseB_phase & (0x0A << 11) ? int(DB_POS(6)) : int(DB_NEG(6));

        int upper = rythm_mode ? 6 : 9;
        for (int i = 0; i < upper; ++i) {
            if (ch[i].slot[CAR].isActive()) {
                bufs[i][sample] += float(ch[i].alg
                    ? ch[i].slot[CAR].calc_slot_car(lfo_pm, lfo_am, 0) +
                           ch[i].slot[MOD].calc_slot_mod(lfo_pm, lfo_am)
                    : ch[i].slot[CAR].calc_slot_car(lfo_pm, lfo_am,
                           ch[i].slot[MOD].calc_slot_mod(lfo_pm, lfo_am)));
            }
        }
        if (rythm_mode) {
            (void)ch[7].slot[MOD].calc_phase(lfo_pm);
            (void)ch[8].slot[CAR].calc_phase(lfo_pm);

            bufs[ 9][sample] += (ch[6].slot[CAR].isActive())
                ? float(2 * ch[6].slot[CAR].calc_slot_car(lfo_pm, lfo_am,
                        ch[6].slot[MOD].calc_slot_mod(lfo_pm, lfo_am)))
                : 0.0f;
            bufs[10][sample] += (ch[7].slot[CAR].isActive())
                ? float(2 * ch[7].slot[CAR].calc_slot_snare(lfo_pm, lfo_am, whiteNoise))
                : 0.0f;
            bufs[11][sample] += (ch[8].slot[CAR].isActive())
                ? float(2 * ch[8].slot[CAR].calc_slot_cym(lfo_am, noiseA, noiseB))
                : 0.0f;
            bufs[12][sample] += (ch[7].slot[MOD].isActive())
                ? float(2 * ch[7].slot[MOD].calc_slot_hat(lfo_am, noiseA, noiseB, whiteNoise))
                : 0.0f;
            bufs[13][sample] += (ch[8].slot[MOD].isActive())
                ? float(2 * ch[8].slot[MOD].calc_slot_tom(lfo_pm, lfo_am))
                : 0.0f;
        }
        bufs[14][sample] += float(adpcm->calcSample());
    }
}

/* Wrapper-facing entry point: pull `num` chip-rate samples and mix to
** signed 32-bit mono. */
void Y8950::generateMono(int32_t* out, unsigned num)
{
    constexpr unsigned NCH = 9 + 5 + 1;
    /* Stack-friendly small allocations: mixer chunk is bounded by the
    ** caller (typically <= ~512 chip-rate samples per call). */
    std::vector<float> storage(NCH * num, 0.0f);
    std::array<float*, NCH> bufs{};
    for (unsigned i = 0; i < NCH; ++i) bufs[i] = &storage[i * num];

    generateChannels(std::span<float*>(bufs.data(), NCH), num);

    float amp = getAmplificationFactorImpl() * float(maxVolume);
    for (unsigned s = 0; s < num; ++s) {
        float sum = 0.0f;
        for (unsigned i = 0; i < NCH; ++i) {
            if (bufs[i]) sum += bufs[i][s];
        }
        out[s] = int32_t(sum * amp);
    }
}

/* I/O Ctrl --------------------------------------------------------- */

void Y8950::writeReg(uint8_t rg, uint8_t data, EmuTime time)
{
    static constexpr std::array<int, 32> sTbl = {
         0,  2,  4,  1,  3,  5, -1, -1,
         6,  8, 10,  7,  9, 11, -1, -1,
        12, 14, 16, 13, 15, 17, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1
    };

    updateStream(time);

    switch (rg & 0xe0) {
    case 0x00: {
        switch (rg) {
        case 0x01: reg[rg] = data; break;
        case 0x02: timer1->setValue(data); reg[rg] = data; break;
        case 0x03: timer2->setValue(data); reg[rg] = data; break;
        case 0x04:
            if (data & Y8950::R04_IRQ_RESET) {
                resetStatus(0x78);
            } else {
                changeStatusMask((~data) & 0x78);
                timer1->setStart((data & Y8950::R04_ST1) != 0, time);
                timer2->setStart((data & Y8950::R04_ST2) != 0, time);
                reg[rg] = data;
            }
            adpcm->resetStatus();
            break;
        case 0x06:
            connector.write(data, time);
            reg[rg] = data;
            break;
        case 0x07:
            periphery.setSPOFF((data & 8) != 0, time);
            reg[rg] = data;
            adpcm->writeReg(rg, data, time);
            break;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C:
        case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11:
        case 0x12: case 0x1A:
            reg[rg] = data;
            adpcm->writeReg(rg, data, time);
            break;
        case 0x15:
            reg[rg] = data;
            if (reg[0x08] & 0x04) {
                int tmp = static_cast<signed char>(reg[0x15]) * 256 + reg[0x16];
                tmp = (tmp * 4) >> (7 - reg[0x17]);
                dac13.writeDAC(Math::clipToInt16(tmp), time);
            }
            break;
        case 0x16: reg[rg] = data & 0xC0; break;
        case 0x17: reg[rg] = data & 0x07; break;
        case 0x18:
            reg[rg] = data;
            periphery.write(reg[0x18], reg[0x19], time);
            break;
        case 0x19:
            reg[rg] = data;
            periphery.write(reg[0x18], reg[0x19], time);
            break;
        }
        break;
    }
    case 0x20: {
        int s = sTbl[rg & 0x1f];
        if (s >= 0) {
            auto& chan = ch[s / 2];
            auto& slot = chan.slot[s & 1];
            slot.patch.AM = (data >> 7) &  1;
            slot.patch.PM = (data >> 6) &  1;
            slot.patch.EG = (data >> 5) &  1;
            slot.patch.setKeyScaleRate((data & 0x10) != 0);
            slot.patch.ML = (data >> 0) & 15;
            slot.updateAll(chan.freq);
        }
        reg[rg] = data;
        break;
    }
    case 0x40: {
        int s = sTbl[rg & 0x1f];
        if (s >= 0) {
            auto& chan = ch[s / 2];
            auto& slot = chan.slot[s & 1];
            slot.patch.KL = (data >> 6) &  3;
            slot.patch.TL = (data >> 0) & 63;
            slot.updateAll(chan.freq);
        }
        reg[rg] = data;
        break;
    }
    case 0x60: {
        int s = sTbl[rg & 0x1f];
        if (s >= 0) {
            auto& slot = ch[s / 2].slot[s & 1];
            slot.patch.AR = (data >> 4) & 15;
            slot.patch.DR = (data >> 0) & 15;
            slot.updateEG();
        }
        reg[rg] = data;
        break;
    }
    case 0x80: {
        int s = sTbl[rg & 0x1f];
        if (s >= 0) {
            auto& slot = ch[s / 2].slot[s & 1];
            slot.patch.SL = (data >> 4) & 15;
            slot.patch.RR = (data >> 0) & 15;
            slot.updateEG();
        }
        reg[rg] = data;
        break;
    }
    case 0xa0: {
        if (rg == 0xbd) {
            am_mode = (data & 0x80) != 0;
            pm_mode = (data & 0x40) != 0;
            setRythmMode(data);
            if (rythm_mode) {
                if (data & 0x10) keyOn_BD();  else keyOff_BD();
                if (data & 0x08) keyOn_SD();  else keyOff_SD();
                if (data & 0x04) keyOn_TOM(); else keyOff_TOM();
                if (data & 0x02) keyOn_CYM(); else keyOff_CYM();
                if (data & 0x01) keyOn_HH();  else keyOff_HH();
            }
            ch[6].slot[MOD].updateAll(ch[6].freq);
            ch[6].slot[CAR].updateAll(ch[6].freq);
            ch[7].slot[MOD].updateAll(ch[7].freq);
            ch[7].slot[CAR].updateAll(ch[7].freq);
            ch[8].slot[MOD].updateAll(ch[8].freq);
            ch[8].slot[CAR].updateAll(ch[8].freq);
            reg[rg] = data;
            break;
        }
        unsigned c = rg & 0x0f;
        if (c > 8) break;
        unsigned freq;
        if (!(rg & 0x10)) {
            freq = data | ((reg[rg + 0x10] & 0x1F) << 8);
        } else {
            if (data & 0x20) ch[c].keyOn (KEY_MAIN);
            else             ch[c].keyOff(KEY_MAIN);
            freq = reg[rg - 0x10] | ((data & 0x1F) << 8);
        }
        ch[c].setFreq(freq);
        unsigned fNum  = freq % 1024;
        unsigned block = freq / 1024;
        switch (c) {
        case 7: noiseA_dPhase = fNum << block; break;
        case 8: noiseB_dPhase = fNum << block; break;
        }
        ch[c].slot[CAR].updateAll(freq);
        ch[c].slot[MOD].updateAll(freq);
        reg[rg] = data;
        break;
    }
    case 0xc0: {
        if (rg > 0xc8) break;
        int c = rg - 0xC0;
        ch[c].slot[MOD].patch.setFeedbackShift((data >> 1) & 7);
        ch[c].alg = data & 1;
        reg[rg] = data;
    }
    }
}

uint8_t Y8950::readReg(uint8_t rg, EmuTime time)
{
    updateStream(time);
    switch (rg) {
    case 0x0F: case 0x13: case 0x14: case 0x1A:
        return adpcm->readReg(rg, time);
    default:
        return peekReg(rg, time);
    }
}

uint8_t Y8950::peekReg(uint8_t rg, EmuTime time) const
{
    switch (rg) {
    case 0x05: return connector.peek(time);
    case 0x0F: case 0x13: case 0x14: case 0x1A:
        return adpcm->peekReg(rg, time);
    case 0x19: {
        uint8_t input  = const_cast<Y8950Periphery&>(periphery).read(time);
        uint8_t output = reg[0x19];
        uint8_t enable = reg[0x18];
        return (output & enable) | (input & ~enable) | 0xF0;
    }
    default:
        return reg[rg];
    }
}

uint8_t Y8950::readStatus(EmuTime time) const  { return peekStatus(time); }

uint8_t Y8950::peekStatus(EmuTime time) const
{
    const_cast<Y8950Adpcm&>(*adpcm).sync(time);
    return (status & (0x87 | statusMask)) | 0x06;
}

void Y8950::callback(uint8_t flag) { setStatus(flag); }

void Y8950::setStatus(uint8_t flags)
{
    status |= flags;
    if (status & statusMask) {
        status |= 0x80;
        irq.set();
    }
}

void Y8950::resetStatus(uint8_t flags)
{
    status &= ~flags;
    if (!(status & statusMask)) {
        status &= 0x7f;
        irq.reset();
    }
}

uint8_t Y8950::peekRawStatus() const { return status; }

void Y8950::changeStatusMask(uint8_t newMask)
{
    statusMask = newMask;
    status &= 0x87 | statusMask;
    if (status & statusMask) { status |= 0x80; irq.set(); }
    else                     { status &= 0x7f; irq.reset(); }
}

/* blueMSX-specific savestate path.  See header comment for scheme. */
#define BMSAVE_INT(s, name, v) ::saveStateSet((s), (name), (UInt32)(v))
#define BMSAVE_BUF(s, name, p, n) ::saveStateSetBuffer((s), (name), (p), (UInt32)(n))
#define BMLOAD_INT(s, name, def) ::saveStateGet((s), (name), (UInt32)(def))
#define BMLOAD_BUF(s, name, p, n) ::saveStateGetBuffer((s), (name), (p), (UInt32)(n))

#define BM_PATCH_SAVE(prefix, p) do { \
    char tag[64]; \
    snprintf(tag, sizeof tag, "%s_AM", prefix); BMSAVE_INT(s, tag, (p).AM); \
    snprintf(tag, sizeof tag, "%s_PM", prefix); BMSAVE_INT(s, tag, (p).PM); \
    snprintf(tag, sizeof tag, "%s_EG", prefix); BMSAVE_INT(s, tag, (p).EG); \
    snprintf(tag, sizeof tag, "%s_KR", prefix); BMSAVE_INT(s, tag, (p).KR); \
    snprintf(tag, sizeof tag, "%s_ML", prefix); BMSAVE_INT(s, tag, (p).ML); \
    snprintf(tag, sizeof tag, "%s_KL", prefix); BMSAVE_INT(s, tag, (p).KL); \
    snprintf(tag, sizeof tag, "%s_TL", prefix); BMSAVE_INT(s, tag, (p).TL); \
    snprintf(tag, sizeof tag, "%s_FB", prefix); BMSAVE_INT(s, tag, (p).FB); \
    snprintf(tag, sizeof tag, "%s_AR", prefix); BMSAVE_INT(s, tag, (p).AR); \
    snprintf(tag, sizeof tag, "%s_DR", prefix); BMSAVE_INT(s, tag, (p).DR); \
    snprintf(tag, sizeof tag, "%s_SL", prefix); BMSAVE_INT(s, tag, (p).SL); \
    snprintf(tag, sizeof tag, "%s_RR", prefix); BMSAVE_INT(s, tag, (p).RR); \
} while (0)

#define BM_PATCH_LOAD(prefix, p) do { \
    char tag[64]; \
    snprintf(tag, sizeof tag, "%s_AM", prefix); (p).AM = (bool)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_PM", prefix); (p).PM = (bool)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_EG", prefix); (p).EG = (bool)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_KR", prefix); (p).KR = (uint8_t)BMLOAD_INT(s, tag, 11); \
    snprintf(tag, sizeof tag, "%s_ML", prefix); (p).ML = (uint8_t)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_KL", prefix); (p).KL = (uint8_t)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_TL", prefix); (p).TL = (uint8_t)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_FB", prefix); (p).FB = (uint8_t)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_AR", prefix); (p).AR = (uint8_t)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_DR", prefix); (p).DR = (uint8_t)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_SL", prefix); (p).SL = (uint8_t)BMLOAD_INT(s, tag, 0); \
    snprintf(tag, sizeof tag, "%s_RR", prefix); (p).RR = (uint8_t)BMLOAD_INT(s, tag, 0); \
} while (0)

void Y8950::blueMsxSaveStateImpl(SaveState* s)
{
    BMSAVE_INT(s, "hasChip", 1);
    BMSAVE_BUF(s, "reg", reg.data(), reg.size());
    BMSAVE_INT(s, "pm_phase",     pm_phase);
    BMSAVE_INT(s, "am_phase",     am_phase);
    BMSAVE_INT(s, "noise_seed",   noise_seed);
    BMSAVE_INT(s, "noiseA_phase", noiseA_phase);
    BMSAVE_INT(s, "noiseB_phase", noiseB_phase);
    BMSAVE_INT(s, "noiseA_dPhase", noiseA_dPhase);
    BMSAVE_INT(s, "noiseB_dPhase", noiseB_dPhase);
    BMSAVE_INT(s, "status",       status);
    BMSAVE_INT(s, "statusMask",   statusMask);
    BMSAVE_INT(s, "rythm_mode",   rythm_mode);
    BMSAVE_INT(s, "am_mode",      am_mode);
    BMSAVE_INT(s, "pm_mode",      pm_mode);
    BMSAVE_INT(s, "enabled",      enabled);

    char base[16], tag[64];
    for (int c = 0; c < 9; c++) {
        snprintf(base, sizeof base, "ch%d", c);
        snprintf(tag, sizeof tag, "%s_freq", base); BMSAVE_INT(s, tag, ch[c].freq);
        snprintf(tag, sizeof tag, "%s_alg",  base); BMSAVE_INT(s, tag, ch[c].alg);
        for (int sl = 0; sl < 2; sl++) {
            const Slot& slot = ch[c].slot[sl];
            char sp[32];
            snprintf(sp, sizeof sp, "%s_s%d", base, sl);
            snprintf(tag, sizeof tag, "%s_fb",     sp); BMSAVE_INT(s, tag, slot.feedback);
            snprintf(tag, sizeof tag, "%s_out",    sp); BMSAVE_INT(s, tag, slot.output);
            snprintf(tag, sizeof tag, "%s_phase",  sp); BMSAVE_INT(s, tag, slot.phase);
            snprintf(tag, sizeof tag, "%s_egph",   sp); BMSAVE_INT(s, tag, slot.eg_phase.getRawValue());
            snprintf(tag, sizeof tag, "%s_egmd",   sp); BMSAVE_INT(s, tag, (uint8_t)slot.eg_mode);
            snprintf(tag, sizeof tag, "%s_key",    sp); BMSAVE_INT(s, tag, slot.key);
            char pp[40];
            snprintf(pp, sizeof pp, "%s_p", sp);
            BM_PATCH_SAVE(pp, slot.patch);
        }
    }

    if (adpcm) adpcm->blueMsxSaveStateImpl(s);
}

bool Y8950::blueMsxLoadStateImpl(SaveState* s)
{
    if (!BMLOAD_INT(s, "hasChip", 0)) return false;

    BMLOAD_BUF(s, "reg", reg.data(), reg.size());
    pm_phase     =        BMLOAD_INT(s, "pm_phase",      pm_phase);
    am_phase     =        BMLOAD_INT(s, "am_phase",      am_phase);
    noise_seed   = (int)  BMLOAD_INT(s, "noise_seed",    noise_seed);
    noiseA_phase =        BMLOAD_INT(s, "noiseA_phase",  noiseA_phase);
    noiseB_phase =        BMLOAD_INT(s, "noiseB_phase",  noiseB_phase);
    noiseA_dPhase =       BMLOAD_INT(s, "noiseA_dPhase", noiseA_dPhase);
    noiseB_dPhase =       BMLOAD_INT(s, "noiseB_dPhase", noiseB_dPhase);
    status       = (uint8_t)BMLOAD_INT(s, "status",       status);
    statusMask   = (uint8_t)BMLOAD_INT(s, "statusMask",   statusMask);
    rythm_mode   = (bool) BMLOAD_INT(s, "rythm_mode",    rythm_mode);
    am_mode      = (bool) BMLOAD_INT(s, "am_mode",       am_mode);
    pm_mode      = (bool) BMLOAD_INT(s, "pm_mode",       pm_mode);
    enabled      = (bool) BMLOAD_INT(s, "enabled",       enabled);

    char base[16], tag[64];
    for (int c = 0; c < 9; c++) {
        snprintf(base, sizeof base, "ch%d", c);
        snprintf(tag, sizeof tag, "%s_freq", base); ch[c].freq = BMLOAD_INT(s, tag, 0);
        snprintf(tag, sizeof tag, "%s_alg",  base); ch[c].alg  = (bool)BMLOAD_INT(s, tag, 0);
        for (int sl = 0; sl < 2; sl++) {
            Slot& slot = ch[c].slot[sl];
            char sp[32];
            snprintf(sp, sizeof sp, "%s_s%d", base, sl);
            snprintf(tag, sizeof tag, "%s_fb",     sp); slot.feedback = (int)BMLOAD_INT(s, tag, 0);
            snprintf(tag, sizeof tag, "%s_out",    sp); slot.output   = (int)BMLOAD_INT(s, tag, 0);
            snprintf(tag, sizeof tag, "%s_phase",  sp); slot.phase    = BMLOAD_INT(s, tag, 0);
            snprintf(tag, sizeof tag, "%s_egph",   sp);
            { int raw = (int)BMLOAD_INT(s, tag, 0); slot.eg_phase.setRawValue(raw); }
            snprintf(tag, sizeof tag, "%s_egmd",   sp); slot.eg_mode  = (EnvelopeState)BMLOAD_INT(s, tag, (UInt32)EnvelopeState::FINISH);
            snprintf(tag, sizeof tag, "%s_key",    sp); slot.key      = (uint8_t)BMLOAD_INT(s, tag, 0);
            char pp[40];
            snprintf(pp, sizeof pp, "%s_p", sp);
            BM_PATCH_LOAD(pp, slot.patch);
        }
        /* Re-derive dPhase / tll / RKS / EG-rate tables from freq + patch. */
        ch[c].slot[0].updateAll(ch[c].freq);
        ch[c].slot[1].updateAll(ch[c].freq);
    }

    if (adpcm) adpcm->blueMsxLoadStateImpl(s);

    /* Mirror the keyboard-/rhythm-key bits into Slot.key (which is the
    ** OR of KEY_MAIN / KEY_RHYTHM bits).  update_key_status reads from
    ** reg[]/rythm_mode and overwrites slot.key bits accordingly. */
    update_key_status();

    return true;
}

void Y8950::blueMsxAdpcmCopyRamFrom(const uint8_t* src, size_t len)
{
    if (adpcm) adpcm->blueMsxCopyRamFrom(src, (unsigned)len);
}

void Y8950::blueMsxAdpcmCopyRamTo(uint8_t* dst, size_t len) const
{
    if (adpcm) adpcm->blueMsxCopyRamTo(dst, (unsigned)len);
}

size_t Y8950::blueMsxAdpcmRamSize() const
{
    return adpcm ? adpcm->blueMsxRamSize() : 0;
}

} // namespace openmsx
