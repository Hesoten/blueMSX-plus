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
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <vector>
extern "C" {
#include "Board.h"
#include "SaveState.h"
}

#define RHYTHM_ROM_SIZE 0x2000

/* The mixer divides by 4096 after a channel volume of at most 1024, and
** ymfm clamps FM to 16 bits; this keeps a full-scale OPNA at about half
** of the mixer's range. */
#define OUTPUT_GAIN     2

/* The windowed sinc of the rate conversion: how many zero crossings it
** reaches to each side, counted at the lower of the two rates, and into how
** many steps a mixer sample period is divided to tell where a mixer sample
** falls between two chip samples. Coefficients carry 14 bits, so that a
** 16-bit sample times the sum of their magnitudes stays within 32 bits. */
#define SINC_ZERO_CROSSINGS 12
#define SINC_STEPS          1024
#define SINC_COEF_BITS      14

/* ymfm's saved state carries no mark of its layout, so the state says which
** one it holds. Layout 1, which a state without the mark has, keeps the
** ADPCM-B channel in nine values of which seven mean something else now. */
#define CHIP_STATE_LAYOUT   2

/* ym2608::generate() brings FM+ADPCM and the SSG to one common rate by
** repeating and averaging samples. Its protected members let the two parts
** be clocked apart, each at the rate the chip produces it at, without
** changing ymfm. */
class Ym2608Chip : public ymfm::ym2608 {
public:
    Ym2608Chip(ymfm::ymfm_interface& intf) : ymfm::ym2608(intf) {}

    void clockFm(Int16* sample)
    {
        clock_fm_and_adpcm();
        sample[0] = (Int16)m_last_fm.data[0];
        sample[1] = (Int16)m_last_fm.data[1];
    }

    /* The three voices mixed into one as generate() mixes them, which
    ** also keeps their sum within 16 bits. */
    Int16 clockSsg()
    {
        ymfm::ssg_engine::output_data out;

        m_ssg.clock();
        m_ssg.output(out);
        return (Int16)((out.data[0] + out.data[1] + out.data[2]) * 2 / 3);
    }

    UInt32 prescale() const { return m_fm.clock_prescale(); }

    /* Puts the ADPCM-B channel back to where a reset leaves it: stopped,
    ** output at zero, no transfer under way. The registers keep their
    ** values; the engine can only be reset as a whole, so they are saved
    ** around it. */
    void restartAdpcmB()
    {
        std::vector<uint8_t> regs;
        ymfm::ymfm_saved_state saver(regs, true);
        ymfm::ymfm_saved_state loader(regs, false);

        m_adpcm_b.regs().save_restore(saver);
        m_adpcm_b.reset();
        m_adpcm_b.regs().save_restore(loader);
    }
};

/* Brings one stream from the rate the chip produces it at to the mixer
** rate: a windowed sinc, evaluated where each mixer sample falls between
** two chip samples. */
class RateConverter {
public:
    RateConverter(int channels) : channels(channels), ratio(0), taps(0), phases(0), pos(0), quiet(0) {}

    /* ratio is chip samples per mixer sample. What the converter held is
    ** dropped: it was taken at the rate before. */
    void configure(double newRatio)
    {
        if (newRatio != ratio) {
            ratio = newRatio;
            buildKernel();
        }
        history.assign(channels * 2 * taps, 0);
        pos   = 0;
        quiet = taps;
    }

    void put(const Int16* sample)
    {
        Int16 any = 0;
        int c;

        for (c = 0; c < channels; c++) {
            Int16* h = &history[c * 2 * taps];
            h[pos] = h[pos + taps] = sample[c];
            any |= sample[c];
        }
        if (++pos == taps) {
            pos = 0;
        }
        if (any != 0) {
            quiet = 0;
        }
        else if (quiet < taps) {
            quiet++;
        }
    }

    /* acc out of period tells how far the mixer sample lies past the
    ** newest chip sample. */
    void get(UInt32 acc, UInt32 period, Int32* sample) const
    {
        const Int16* coef = &kernel[(UInt32)((UInt64)acc * phases / period) * taps];
        int c;
        int k;

        for (c = 0; c < channels; c++) {
            const Int16* h = &history[c * 2 * taps + pos];
            Int32 sum = 0;

            if (quiet < taps) {
                for (k = 0; k < taps; k++) {
                    sum += h[k] * coef[k];
                }
            }
            sample[c] = (sum + (1 << (SINC_COEF_BITS - 1))) >> SINC_COEF_BITS;
        }
    }

    int getTaps() const { return taps; }

    /* The last taps samples of each channel, oldest first. */
    void save(std::vector<Int16>& samples) const
    {
        int c;

        samples.clear();
        for (c = 0; c < channels; c++) {
            const Int16* h = &history[c * 2 * taps + pos];
            samples.insert(samples.end(), h, h + taps);
        }
    }

    void load(const std::vector<Int16>& samples)
    {
        int c;
        int k;

        for (c = 0; c < channels; c++) {
            Int16* h = &history[c * 2 * taps];
            for (k = 0; k < taps; k++) {
                h[k] = h[k + taps] = samples[c * taps + k];
            }
        }
        pos   = 0;
        quiet = 0;
    }

private:
    void buildKernel()
    {
        const double PI = 3.14159265358979323846;
        double stretch = ratio > 1.0 ? ratio : 1.0;
        int half = (int)ceil(SINC_ZERO_CROSSINGS * stretch);
        std::vector<double> row;
        int p;
        int k;

        taps   = 2 * half;
        phases = (int)ceil(SINC_STEPS / stretch);
        row.resize(taps);
        kernel.resize(phases * taps);

        for (p = 0; p < phases; p++) {
            double total = 0;
            Int32 sum = 0;
            int peak = 0;

            for (k = 0; k < taps; k++) {
                /* chip samples from the mixer sample to tap k */
                double d = k - (half - 1) - (double)p / phases;
                double x = d / stretch;
                double window = 0.42 + 0.5 * cos(PI * d / half) + 0.08 * cos(2 * PI * d / half);

                row[k] = (x == 0 ? 1.0 : sin(PI * x) / (PI * x)) * window;
                total += row[k];
                if (row[k] > row[peak]) {
                    peak = k;
                }
            }
            /* Every row sums to exactly one, so that a constant level
            ** comes out constant wherever the mixer sample falls. */
            for (k = 0; k < taps; k++) {
                Int16 c = (Int16)floor(row[k] / total * (1 << SINC_COEF_BITS) + 0.5);
                kernel[p * taps + k] = c;
                sum += c;
            }
            kernel[p * taps + peak] += (Int16)((1 << SINC_COEF_BITS) - sum);
        }
    }

    int    channels;
    double ratio;
    int    taps;

    /* One row of taps coefficients for each place a mixer sample can take
    ** between two chip samples. */
    int    phases;
    std::vector<Int16> kernel;

    /* Every sample is stored twice, taps apart, so that the last taps of
    ** them always lie in a row starting at pos. */
    std::vector<Int16> history;
    int    pos;

    /* Zero samples in a row, up to taps: with nothing but zeros to weigh,
    ** the arithmetic is skipped. Only that is skipped; the chip is clocked
    ** all the same, because the ADPCM-B status advances with it. */
    int    quiet;
};

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
    Ym2608Chip chip;
};

struct YM2608 {
    YM2608() : fm(2), ssg(1) {}

    Mixer*      mixer;
    Int32       fmHandle;
    Int32       ssgHandle;
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

    /* The two streams of the chip: FM with rhythm and ADPCM in stereo, and
    ** the SSG. Each has its own number of clocks per sample, which follows
    ** the prescaler, and its clock ticks against sampleRate times that
    ** number, so the ratio between chip and mixer samples stays exact.
    ** Each is a mixer channel of its own, with its own level: the chip
    ** leaves mixing the two to the board. */
    UInt32      prescale;
    UInt32      fmClocks;
    UInt32      ssgClocks;
    UInt32      fmAcc;
    UInt32      ssgAcc;
    RateConverter fm;
    RateConverter ssg;

    UInt8       rhythmRom[RHYTHM_ROM_SIZE];
    int         hasRhythmRom;
    UInt8*      adpcmRam;
    UInt32      adpcmRamSize;

    UInt8       address[2];
    UInt8       regs[2][256];

    Int32       fmBuffer[AUDIO_STEREO_BUFFER_SIZE];
    Int32       ssgBuffer[AUDIO_MONO_BUFFER_SIZE];
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

/* The chip produces an FM sample every 24 * prescaler clocks and an SSG
** sample every 32, 16 or 8 clocks at prescaler 6, 3 or 2 (ymfm_opn.h,
** "A note about prescaling and sample rates"). Both streams start over. */
static void setRates(YM2608* ym2608)
{
    UInt32 prescale = ym2608->host->chip.prescale();

    ym2608->prescale  = prescale;
    ym2608->fmClocks  = 24 * prescale;
    ym2608->ssgClocks = prescale == 6 ? 32 : prescale == 3 ? 16 : 8;
    ym2608->fmAcc     = 0;
    ym2608->ssgAcc    = 0;
    ym2608->fm.configure((double)ym2608->clock / ((double)ym2608->fmClocks * ym2608->sampleRate));
    ym2608->ssg.configure((double)ym2608->clock / ((double)ym2608->ssgClocks * ym2608->sampleRate));
}

static Int32* ym2608FmSync(void* ref, UInt32 count)
{
    YM2608* ym2608 = (YM2608*)ref;
    Ym2608Chip& chip = ym2608->host->chip;
    UInt32 period = ym2608->sampleRate * ym2608->fmClocks;
    UInt32 i;

    for (i = 0; i < count; i++) {
        Int16 sample[2];
        Int32 fm[2];

        ym2608->fmAcc += ym2608->clock;
        while (ym2608->fmAcc >= period) {
            ym2608->fmAcc -= period;
            chip.clockFm(sample);
            ym2608->fm.put(sample);
        }
        ym2608->fm.get(ym2608->fmAcc, period, fm);

        ym2608->fmBuffer[2 * i + 0] = OUTPUT_GAIN * fm[0];
        ym2608->fmBuffer[2 * i + 1] = OUTPUT_GAIN * fm[1];
    }

    return ym2608->fmBuffer;
}

static Int32* ym2608SsgSync(void* ref, UInt32 count)
{
    YM2608* ym2608 = (YM2608*)ref;
    Ym2608Chip& chip = ym2608->host->chip;
    UInt32 period = ym2608->sampleRate * ym2608->ssgClocks;
    UInt32 i;

    for (i = 0; i < count; i++) {
        Int16 sample;
        Int32 ssg;

        ym2608->ssgAcc += ym2608->clock;
        while (ym2608->ssgAcc >= period) {
            ym2608->ssgAcc -= period;
            sample = chip.clockSsg();
            ym2608->ssg.put(&sample);
        }
        ym2608->ssg.get(ym2608->ssgAcc, period, &ssg);

        ym2608->ssgBuffer[i] = OUTPUT_GAIN * ssg;
    }

    return ym2608->ssgBuffer;
}

static void ym2608SetSampleRate(void* ref, UInt32 rate)
{
    YM2608* ym2608 = (YM2608*)ref;

    ym2608->sampleRate = rate;
    setRates(ym2608);
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

    ym2608->sampleRate = mixerGetSampleRate(mixer);
    setRates(ym2608);
    /* The rate callback of the FM channel sets the rates of both streams. */
    ym2608->fmHandle  = mixerRegisterChannel(mixer, MIXER_CHANNEL_OPNA_FM, 1,
                                             ym2608FmSync, ym2608SetSampleRate, ym2608);
    ym2608->ssgHandle = mixerRegisterChannel(mixer, MIXER_CHANNEL_OPNA_SSG, 0,
                                             ym2608SsgSync, NULL, ym2608);

    ym2608Reset(ym2608);

    return ym2608;
}

void ym2608Destroy(YM2608* ym2608)
{
    mixerUnregisterChannel(ym2608->mixer, ym2608->fmHandle);
    mixerUnregisterChannel(ym2608->mixer, ym2608->ssgHandle);

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
    ym2608->address[0]  = 0;
    ym2608->address[1]  = 0;
    memset(ym2608->regs, 0, sizeof(ym2608->regs));

    ym2608->host->chip.reset();
    setRates(ym2608);
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

    /* Selecting address 2Dh-2Fh changes the prescaler. */
    if (ym2608->host->chip.prescale() != ym2608->prescale) {
        setRates(ym2608);
    }
}

void ym2608SaveState(YM2608* ym2608)
{
    SaveState* state = saveStateOpenForWrite("ym2608");
    std::vector<uint8_t> chipState;
    std::vector<Int16> fmHistory;
    std::vector<Int16> ssgHistory;
    ymfm::ymfm_saved_state saver(chipState, true);
    UInt32 now = boardSystemTime();

    ym2608->host->chip.save_restore(saver);
    ym2608->fm.save(fmHistory);
    ym2608->ssg.save(ssgHistory);

    saveStateSet(state, "timerActiveA",  ym2608->timerActive[0]);
    saveStateSet(state, "timerTimeoutA", ym2608->timerTimeout[0]);
    saveStateSet(state, "timerActiveB",  ym2608->timerActive[1]);
    saveStateSet(state, "timerTimeoutB", ym2608->timerTimeout[1]);
    saveStateSet(state, "busyLeft",      (Int32)(ym2608->busyEnd - now) > 0 ? ym2608->busyEnd - now : 0);
    saveStateSet(state, "irqAsserted",   ym2608->irqAsserted);
    saveStateSet(state, "mixerRate",     ym2608->sampleRate);
    saveStateSet(state, "fmAcc",         ym2608->fmAcc);
    saveStateSet(state, "ssgAcc",        ym2608->ssgAcc);
    saveStateSet(state, "fmHistorySize", (UInt32)(fmHistory.size() * sizeof(Int16)));
    saveStateSetBuffer(state, "fmHistory", fmHistory.data(), (UInt32)(fmHistory.size() * sizeof(Int16)));
    saveStateSet(state, "ssgHistorySize", (UInt32)(ssgHistory.size() * sizeof(Int16)));
    saveStateSetBuffer(state, "ssgHistory", ssgHistory.data(), (UInt32)(ssgHistory.size() * sizeof(Int16)));
    saveStateSet(state, "address0",      ym2608->address[0]);
    saveStateSet(state, "address1",      ym2608->address[1]);
    saveStateSetBuffer(state, "regs", ym2608->regs, sizeof(ym2608->regs));
    saveStateSet(state, "adpcmRamSize",  ym2608->adpcmRamSize);
    saveStateSetBuffer(state, "adpcmRam", ym2608->adpcmRam, ym2608->adpcmRamSize);
    saveStateSet(state, "chipStateLayout", CHIP_STATE_LAYOUT);
    saveStateSet(state, "chipStateSize", (UInt32)chipState.size());
    saveStateSetBuffer(state, "chipState", chipState.data(), (UInt32)chipState.size());

    saveStateClose(state);
}

void ym2608LoadState(YM2608* ym2608)
{
    SaveState* state = saveStateOpenForRead("ym2608");
    std::vector<uint8_t> chipState;
    std::vector<Int16> fmHistory;
    std::vector<Int16> ssgHistory;
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

        /* The ADPCM-B channel is the one part whose values changed their
        ** meaning between the layouts, at the same size. Read as they are
        ** they leave its output and its address somewhere else, so the
        ** channel starts over; ADPCM-B that was playing is silent after
        ** the load, and everything else goes on. */
        if (saveStateGet(state, "chipStateLayout", 1) != CHIP_STATE_LAYOUT) {
            ym2608->host->chip.restartAdpcmB();
        }
    }

    /* The prescaler comes with the chip's state. The streams go on where
    ** they were only if they were taken at these very rates; a state from
    ** another mixer rate, or one that does not hold them, starts them over. */
    setRates(ym2608);
    fmHistory.resize(2 * ym2608->fm.getTaps());
    ssgHistory.resize(ym2608->ssg.getTaps());
    if (saveStateGet(state, "mixerRate", 0) == ym2608->sampleRate &&
        saveStateGet(state, "fmHistorySize", 0) == fmHistory.size() * sizeof(Int16) &&
        saveStateGet(state, "ssgHistorySize", 0) == ssgHistory.size() * sizeof(Int16)) {
        ym2608->fmAcc  = saveStateGet(state, "fmAcc",  0);
        ym2608->ssgAcc = saveStateGet(state, "ssgAcc", 0);
        saveStateGetBuffer(state, "fmHistory", fmHistory.data(), (UInt32)(fmHistory.size() * sizeof(Int16)));
        saveStateGetBuffer(state, "ssgHistory", ssgHistory.data(), (UInt32)(ssgHistory.size() * sizeof(Int16)));
        ym2608->fm.load(fmHistory);
        ym2608->ssg.load(ssgHistory);
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
