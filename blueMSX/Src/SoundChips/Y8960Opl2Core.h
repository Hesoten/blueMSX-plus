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
/*****************************************************************************
**
** Forked for the Y8960 cartridge, 2026 by madscient.
**
** The cartridge's OPL2EX is a YM3812 with ADPCM-B bolted on, and its register
** map is the Y8950's, so this is the Y8950 port with four changes: the four
** waveforms a YM3812 has and a Y8950 does not, the MSX-AUDIO parts the
** cartridge does not carry (keyboard connector, 13 bit DAC, periphery), a
** sample RAM that lives outside the circuit, where the block owns it, and a
** pan per channel (F6h-FEh) and for the ADPCM (FFh).
**
** It is a fork rather than an extension because the machine's own MSX-AUDIO
** keeps using the unmodified port; both end up in one binary. The namespace
** is what keeps them apart.
**
******************************************************************************
*/
/* openMSX Y8950 port; openMSX-internal deps stubbed inline below. */

#ifndef Y8960_OPL2_CORE_HH
#define Y8960_OPL2_CORE_HH

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include "Board.h"
#include "AudioMixer.h"
}

/* Forward-declare blueMSX's SaveState in the global namespace so the
** member-function signatures below resolve to ::SaveState rather than
** openmsx::SaveState (which would otherwise be an unrelated type). */
struct SaveState;

namespace y8960opl2 {

/* ----- Forward declarations needed by stubs ---------------------- */
class MSXMotherBoard;
class Scheduler;
class DeviceConfig;
class EmuTimerCallback;

/* ----- Minimal stubs for openMSX-internal types ----------------- */

using EmuTime = unsigned long;

namespace Math {
    static constexpr double pi = 3.14159265358979323846;
    inline int clipToInt16(int x) {
        return (x > 32767) ? 32767 : (x < -32768) ? -32768 : x;
    }
}

template<typename T, typename U>
constexpr T narrow_cast(U u) { return static_cast<T>(u); }

template<typename T, typename U>
constexpr T narrow(U u) { return static_cast<T>(u); }

class xrange_view {
public:
    constexpr xrange_view(int hi)         : lo(0),  hi(hi) {}
    constexpr xrange_view(int lo, int hi) : lo(lo), hi(hi) {}
    struct iterator {
        int v;
        constexpr int operator*() const { return v; }
        constexpr iterator& operator++() { ++v; return *this; }
        constexpr bool operator!=(const iterator& o) const { return v != o.v; }
    };
    constexpr iterator begin() const { return {lo}; }
    constexpr iterator end()   const { return {hi}; }
private:
    int lo, hi;
};
constexpr xrange_view xrange(int hi)         { return xrange_view(hi); }
constexpr xrange_view xrange(int lo, int hi) { return xrange_view(lo, hi); }

template<int FRACTION_BITS_>
class FixedPoint {
public:
    static constexpr int FRACTION_BITS = FRACTION_BITS_;

    constexpr FixedPoint() : value(0) {}
    constexpr explicit FixedPoint(int v) : value(v << FRACTION_BITS) {}

    constexpr int  toInt()       const { return value >> FRACTION_BITS; }
    constexpr int  getRawValue() const { return value; }
    constexpr void setRawValue(int v) { value = v; }

    constexpr FixedPoint& operator+=(FixedPoint o) { value += o.value; return *this; }
    constexpr FixedPoint  operator+ (FixedPoint o) const { FixedPoint r; r.value = value + o.value; return r; }
    constexpr FixedPoint  operator>>(int n)        const { FixedPoint r; r.value = value >> n;     return r; }

    constexpr bool operator==(FixedPoint o) const { return value == o.value; }
    constexpr bool operator!=(FixedPoint o) const { return value != o.value; }
    constexpr bool operator< (FixedPoint o) const { return value <  o.value; }
    constexpr bool operator<=(FixedPoint o) const { return value <= o.value; }
    constexpr bool operator> (FixedPoint o) const { return value >  o.value; }
    constexpr bool operator>=(FixedPoint o) const { return value >= o.value; }

private:
    int value;
};

/* The mask is given by the owner rather than fixed at MSX-AUDIO's bit: the
** cartridge's block and the machine's own MSX-AUDIO can both be present, and
** one clearing the other's pending bit would lose an interrupt. */
class IRQHelper {
public:
    explicit IRQHelper(UInt32 mask_) : mask(mask_) {}
    /* As openMSX's IntHelper: a request still up goes with the chip. Ejecting
    ** the cartridge destroys it, and nothing else would lower the line. */
    ~IRQHelper()  { reset(); }
    void reset()  { boardClearInt(mask); }
    void set()    { boardSetInt  (mask); }
private:
    UInt32 mask;
};

class EmuTimerCallback {
public:
    virtual ~EmuTimerCallback() = default;
    virtual void callback(uint8_t flag) = 0;
};

/* Two timers wired through blueMSX's boardTimer system.  The factories
** match upstream's signature so Y8950.cc compiles unchanged.  Internally
** we ignore the Scheduler argument (we do scheduling via boardTimer). */
class EmuTimer {
public:
    static std::unique_ptr<EmuTimer> createOPL3_1(Scheduler&, EmuTimerCallback& cb) {
        return std::unique_ptr<EmuTimer>(new EmuTimer(cb, 0x40 /*MASK_T1*/, 80));
    }
    static std::unique_ptr<EmuTimer> createOPL3_2(Scheduler&, EmuTimerCallback& cb) {
        return std::unique_ptr<EmuTimer>(new EmuTimer(cb, 0x20 /*MASK_T2*/, 320));
    }
    ~EmuTimer() {
        if (timer) boardTimerDestroy(timer);
    }
    void setValue(uint8_t value) {
        ticksReload = 256 - value;
        if (running) reschedule();
    }
    void setStart(bool start, EmuTime /*time*/) {
        if (start && !running) { running = true;  reschedule(); }
        else if (!start && running) { running = false; if (timer) boardTimerRemove(timer); }
    }
private:
    EmuTimer(EmuTimerCallback& cb_, uint8_t flag_, int us_)
        : cb(cb_), flag(flag_), usPerTick(us_) {
        timer = boardTimerCreate(&EmuTimer::onFire, this);
    }
    void reschedule() {
        if (!timer || ticksReload <= 0) return;
        UInt64 cycles = (UInt64)boardFrequency() * usPerTick * ticksReload / 1000000ULL;
        boardTimerAdd(timer, boardSystemTime() + (UInt32)cycles);
    }
    static void onFire(void* ref, UInt32 /*time*/) {
        EmuTimer* self = (EmuTimer*)ref;
        self->cb.callback(self->flag);
        if (self->running) self->reschedule();
    }
    EmuTimerCallback& cb;
    uint8_t flag;
    int usPerTick;
    int ticksReload = 0;
    bool running = false;
    BoardTimer* timer = nullptr;
};

/* The cartridge's sample RAM, behind the ADPCM. It lives outside the circuit
** so that the block owns it and saves it as one piece; the circuit addresses
** it from zero and cannot see past its end. */
class SampleRam {
public:
    explicit SampleRam(unsigned size_) : data(size_, 0xFF) {}

    uint8_t read(unsigned addr) const  { return data[addr]; }
    void    write(unsigned addr, uint8_t v) { data[addr] = v; }
    void    clear(uint8_t v) { std::fill(data.begin(), data.end(), v); }

    unsigned       size() const { return (unsigned)data.size(); }
    const uint8_t* raw()  const { return data.data(); }
    uint8_t*       raw()        { return data.data(); }

private:
    std::vector<uint8_t> data;
};

/* Board time is a 32 bit count of master clock cycles that wraps every 200 s,
** so elapsed time is taken modulo 2^32 instead of by comparing two times.
** The clock stays on a tick boundary: the part of a tick that has gone by at
** one sync is still there at the next, however close together they come. */
template<int FREQ, int DIV>
class Clock {
public:
    explicit Clock(EmuTime now) : lastTick((UInt32)now) {}
    EmuTime  getTime() const { return (EmuTime)lastTick; }
    void     reset(EmuTime now) { lastTick = (UInt32)now; }
    void     advance(EmuTime now) { lastTick += getTicksTill(now) * PERIOD; }
    unsigned getTicksTill(EmuTime now) const {
        return (unsigned)(((UInt32)now - lastTick) / PERIOD);
    }
    Clock& operator+=(unsigned ticks) {
        lastTick += (UInt32)ticks * PERIOD;
        return *this;
    }
private:
    /* Master clock cycles per tick. */
    static constexpr UInt32 PERIOD = (UInt32)((UInt64)boardFrequency() * DIV / FREQ);
    static_assert((UInt64)boardFrequency() * DIV % FREQ == 0,
                  "a tick must be a whole number of master clock cycles");
    UInt32 lastTick;
};

class Scheduler {};

/* A sync point is a board timer. */
class Schedulable {
public:
    Schedulable() : timer(boardTimerCreate(&Schedulable::onFire, this)) {}
    explicit Schedulable(Scheduler&) : Schedulable() {}
    Schedulable(const Schedulable&) = delete;
    Schedulable& operator=(const Schedulable&) = delete;
    virtual ~Schedulable() { boardTimerDestroy(timer); }
    void removeSyncPoint() { boardTimerRemove(timer); }
    void setSyncPoint(EmuTime time) { boardTimerAdd(timer, (UInt32)time); }
    EmuTime getCurrentTime() const { return (EmuTime)boardSystemTime(); }
    virtual void executeUntil(EmuTime) {}
private:
    static void onFire(void* ref, UInt32 /*time*/) {
        Schedulable* self = (Schedulable*)ref;
        /* A register write reaches the chip after the wrapper has brought the
        ** mixer up to date. A sync point changes the chip's state without
        ** passing the wrapper, so the mixer is brought up to date here. */
        mixerSync(boardGetMixer());
        /* The current time, not the one the timer was set for: a register
        ** write in the instruction that crossed it may have synced past it. */
        self->executeUntil(self->getCurrentTime());
    }
    BoardTimer* timer;
};

class MSXMotherBoard {
public:
    EmuTime    getCurrentTime()        { return (EmuTime)boardSystemTime(); }
    Scheduler& getScheduler()          { return scheduler; }
    int        getPluggingController() { return 0; }
private:
    Scheduler scheduler;
};

class DeviceConfig {
public:
    explicit DeviceConfig(MSXMotherBoard& mb) : mb(mb) {}
    MSXMotherBoard& getMotherBoard() const { return mb; }
    Scheduler&      getScheduler()   const { return mb.getScheduler(); }
private:
    MSXMotherBoard& mb;
};

class SimpleDebuggable {
public:
    SimpleDebuggable(MSXMotherBoard&, const std::string&, const std::string&, unsigned) {}
};

/* ResampledSoundDevice: minimal base providing name, volume, and no-op
** updateStream/registerSound/unregisterSound.  Resampling is done in the
** wrapper, which pulls chip-rate samples from generateMono(). */
class ResampledSoundDevice {
public:
    ResampledSoundDevice(MSXMotherBoard& /*mb*/, const std::string& n,
                         const std::string& /*desc*/, unsigned /*numChannels*/,
                         unsigned /*inputRate*/, bool /*stereo*/)
        : name(n) {}
    virtual ~ResampledSoundDevice() = default;

    void setVolume(short v)        { maxVolume = v; }
    short getVolume() const         { return maxVolume; }
    const std::string& getName() const { return name; }

    void updateStream(EmuTime) {}
    void registerSound(const DeviceConfig&) {}
    void unregisterSound() {}

    virtual void  generateChannels(std::span<float*> bufs, unsigned num) = 0;
    virtual float getAmplificationFactorImpl() const = 0;

protected:
    std::string name;
    short       maxVolume = 32767 * 9 / 10;
};

/* ----- Forward declaration -------------------------------------- */
class Y8950Adpcm;

/* ----- The chip itself (verbatim from openMSX, types adapted) --- */
class Y8950 final : private ResampledSoundDevice, private EmuTimerCallback
{
public:
    static constexpr int CLOCK_FREQ     = 3579545;
    static constexpr int CLOCK_FREQ_DIV = 72;

    static constexpr int R04_ST1          = 0x01;
    static constexpr int R04_ST2          = 0x02;
    static constexpr int R04_MASK_BUF_RDY = 0x08;
    static constexpr int R04_MASK_EOS     = 0x10;
    static constexpr int R04_MASK_T2      = 0x20;
    static constexpr int R04_MASK_T1      = 0x40;
    static constexpr int R04_IRQ_RESET    = 0x80;

    static constexpr int STATUS_PCM_BSY = 0x01;
    static constexpr int STATUS_EOS     = R04_MASK_EOS;
    static constexpr int STATUS_BUF_RDY = R04_MASK_BUF_RDY;
    static constexpr int STATUS_T2      = R04_MASK_T2;
    static constexpr int STATUS_T1      = R04_MASK_T1;

    /* irqMask is the bit of the board's pending mask this circuit raises. */
    Y8950(const std::string& name, const DeviceConfig& config,
          SampleRam& sampleRam, UInt32 irqMask, EmuTime time);
    ~Y8950();

    void setEnabled(bool enabled, EmuTime time);
    void clearRam();
    void reset(EmuTime time);
    void writeReg(uint8_t rg, uint8_t data, EmuTime time);
    uint8_t readReg(uint8_t rg, EmuTime time);
    uint8_t peekReg(uint8_t rg, EmuTime time) const;
    uint8_t readStatus(EmuTime time) const;
    uint8_t peekStatus(EmuTime time) const;

    void setStatus(uint8_t flags);
    void resetStatus(uint8_t flags);
    uint8_t peekRawStatus() const;

    /* Produce `num` chip-rate signed-32-bit mono samples into `out`, the
    ** pans ignored: the probe's view of the chip. */
    void generateMono(int32_t* out, unsigned num);

    /* Produce `num` chip-rate stereo samples, interleaved, each channel placed
    ** by its pan register (F6h-FEh, FFh for the ADPCM). */
    void generateStereo(int32_t* out, unsigned num);

    using ResampledSoundDevice::setVolume;
    using ResampledSoundDevice::getName;

    /* The sample RAM is shared between the circuits, so the device that owns
    ** it saves it once rather than each circuit saving its own view. */
    void blueMsxSaveStateImpl(::SaveState* state);
    bool blueMsxLoadStateImpl(::SaveState* state);

private:
    float getAmplificationFactorImpl() const override;
    void  generateChannels(std::span<float*> bufs, unsigned num) override;

    void keyOn_BD();
    void keyOn_SD();
    void keyOn_TOM();
    void keyOn_HH();
    void keyOn_CYM();
    void keyOff_BD();
    void keyOff_SD();
    void keyOff_TOM();
    void keyOff_HH();
    void keyOff_CYM();
    void setRythmMode(int data);
    void update_key_status();

    /* Recomputes every slot's waveform from E0h-F5h and the gate in 01h b5.
    ** Called whenever either moves, and after a state load, so the slots
    ** carry no waveform of their own to keep in step. */
    void updateWaveSelect();

    bool checkMuteHelper();

    void changeStatusMask(uint8_t newMask);

    void callback(uint8_t flag) override;

public:
    static constexpr int EG_BITS    = 9;
    static constexpr int EG_DP_BITS = 23;
    using EnvPhaseIndex = FixedPoint<EG_DP_BITS - EG_BITS>;
    enum class EnvelopeState : uint8_t { ATTACK, DECAY, SUSTAIN, RELEASE, FINISH };

private:
    enum KeyPart : uint8_t { KEY_MAIN = 1, KEY_RHYTHM = 2 };

    class Patch {
    public:
        Patch();
        void reset();

        void setKeyScaleRate(bool value) { KR = value ? 9 : 11; }
        void setFeedbackShift(uint8_t value) { FB = value ? 8 - value : 0; }

        bool AM, PM, EG;
        uint8_t KR;
        uint8_t ML;
        uint8_t KL;
        uint8_t TL;
        uint8_t FB;
        uint8_t AR;
        uint8_t DR;
        uint8_t SL;
        uint8_t RR;
    };

    class Slot {
    public:
        Slot();
        void reset();

        bool isActive() const;
        void slotOn (KeyPart part);
        void slotOff(KeyPart part);

        unsigned calc_phase(int lfo_pm);
        unsigned calc_envelope(int lfo_am);
        int calc_slot_car(int lfo_pm, int lfo_am, int fm);
        int calc_slot_mod(int lfo_pm, int lfo_am);
        int calc_slot_tom(int lfo_pm, int lfo_am);
        int calc_slot_rhythm(int lfo_am, unsigned pgout);

        void updateAll(unsigned freq);
        void updatePG(unsigned freq);
        void updateTLL(unsigned freq);
        void updateRKS(unsigned freq);
        void updateEG();

        int feedback;
        int output;

        unsigned phase;
        unsigned dPhase;

        std::span<const EnvPhaseIndex, 16> dPhaseARTableRks;
        std::span<const EnvPhaseIndex, 16> dPhaseDRTableRks;
        int tll;
        EnvelopeState eg_mode;
        EnvPhaseIndex eg_phase;
        EnvPhaseIndex eg_dPhase;

        Patch patch;
        uint8_t key;

        /* 0-3, already resolved against the gate in 01h b5, so calc_slot_*
        ** needs no access to the chip. Derived from reg[], never saved. */
        uint8_t wave;
    };

    class Channel {
    public:
        Channel();
        void reset();
        void setFreq(unsigned freq);
        void keyOn (KeyPart part);
        void keyOff(KeyPart part);

        std::array<Slot, 2> slot;
        unsigned freq;
        bool alg;
    };

    MSXMotherBoard& motherBoard;
    std::unique_ptr<Y8950Adpcm> adpcm;

    const std::unique_ptr<EmuTimer> timer1;
    const std::unique_ptr<EmuTimer> timer2;
    IRQHelper irq;
    
    std::array<uint8_t, 0x100> reg;
    std::array<Channel, 9>     ch;

    /* Derived from reg[F6h..FFh]; the last is the ADPCM's. 4.12 fixed point. */
    std::array<std::array<int32_t, 2>, 10> panGain;
    void updatePanGains();
    
    unsigned pm_phase;
    unsigned am_phase;

    unsigned noisePoly;

    uint8_t status;
    uint8_t statusMask;
    bool    rythm_mode;
    bool    am_mode;
    bool    pm_mode;
    bool    enabled;
};

} // namespace y8960opl2

#endif
