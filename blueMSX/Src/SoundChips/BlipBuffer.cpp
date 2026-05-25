/*****************************************************************************
**
** Click-free audio mixer buffer.
** Ported from openMSX's BlipBuffer.{hh,cc}; the openMSX file is itself
** heavily based on Shay Green's Blip_Buffer 0.4.0
** (http://www.slack.net/~ant/).
** Upstream openMSX is distributed under the GNU GPL v2 or later;
** Blip_Buffer is distributed under the GNU LGPL.
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
/* Differences vs the upstream openMSX version:
**   - C++ standard-library only (no FixedPoint / xrange / narrow / span /
**     ranges helpers from the openMSX tree).
**   - Impulse table is built at first-use runtime instead of constexpr, so
**     we don't depend on constexpr sin/cos from C++26.
**   - C-callable API: opaque BlipBuffer*, factory + addDelta + readSamples
**     functions.  Pitched read replaces the openMSX <PITCH> template.  */
#include "BlipBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr int BLIP_IMPULSE_WIDTH = 16;
constexpr int BLIP_RES           = 1 << BLIP_PHASE_BITS;        /* 1024 */
constexpr std::size_t BUFFER_SIZE = 1u << 14;                   /* 16384 */
constexpr std::size_t BUFFER_MASK = BUFFER_SIZE - 1;

constexpr float BASS_FACTOR = 511.0f / 512.0f;

using ImpulseRow   = std::array<float, BLIP_IMPULSE_WIDTH>;
using ImpulseTable = std::array<ImpulseRow, BLIP_RES>;

/* The sinc-window kernel.  Computed once on the first BlipBuffer
** construction (cheap: ~16 KB of float, runs in microseconds). */
const ImpulseTable& getImpulses()
{
    static const ImpulseTable table = []() {
        constexpr int HALF_SIZE = BLIP_RES / 2 * (BLIP_IMPULSE_WIDTH - 1);
        constexpr int F_SIZE    = BLIP_RES + HALF_SIZE + BLIP_RES;
        std::array<double, F_SIZE> fImpulse{};
        double* out = fImpulse.data() + BLIP_RES;
        double* end = fImpulse.data() + BLIP_RES + HALF_SIZE;

        constexpr double pi = 3.14159265358979323846;
        const double overSample  = (4.5 / (BLIP_IMPULSE_WIDTH - 1)) + 0.85;
        const double toAngle     = pi / (2.0 * overSample * BLIP_RES);
        const double toFraction  = pi / (2.0 * (HALF_SIZE - 1));

        /* sinc * hamming window */
        for (int i = 0; i < HALF_SIZE; ++i) {
            double angle = ((i - HALF_SIZE) * 2 + 1) * toAngle;
            out[i] = std::sin(angle) / angle;
            out[i] *= 0.54 - 0.46 * std::cos((2 * i + 1) * toFraction);
        }
        /* mirror just past centre for the integration step below */
        for (int i = 0; i < BLIP_RES; ++i) {
            end[i] = out[HALF_SIZE - 1 - i];
        }
        /* rescale so the integrated impulse has unit area */
        double total = 0.0;
        for (int i = 0; i < HALF_SIZE; ++i) total += out[i];
        const double rescale = 1.0 / (2.0 * total);

        /* integrate, first-difference, rescale, store in flat array */
        constexpr int IMP_SIZE = BLIP_RES * (BLIP_IMPULSE_WIDTH / 2) + 1;
        std::array<float, IMP_SIZE> imp{};
        double sum = 0.0;
        double next = 0.0;
        for (int i = 0; i < IMP_SIZE; ++i) {
            imp[i] = static_cast<float>((next - sum) * rescale);
            sum  += fImpulse[i];
            next += fImpulse[i + BLIP_RES];
        }
        /* reshuffle for cache friendliness (matches openMSX layout) */
        ImpulseTable result{};
        for (int phase = 0; phase < BLIP_RES; ++phase) {
            const float* impFwd = &imp[BLIP_RES - phase];
            const float* impRev = &imp[phase];
            float* p = result[phase].data();
            for (int i = 0; i < BLIP_IMPULSE_WIDTH / 2; ++i) {
                *p++ = impFwd[BLIP_RES * i];
            }
            for (int i = BLIP_IMPULSE_WIDTH / 2 - 1; i >= 0; --i) {
                *p++ = impRev[BLIP_RES * i];
            }
        }
        return result;
    }();
    return table;
}

inline bool isSilent(float x)
{
    /* Mirrors openMSX: switch to fast-silent path once |accum| < 1/32768.
    ** Input range we expect is roughly [-32768*256 .. +32767*256] (DAC
    ** scale), so it takes a moment before this triggers -- still well
    ** under a second of BASS_FACTOR decay. */
    return std::abs(x) < (1.0f / 32768.0f);
}

} /* namespace */

/* Opaque struct.  Allocated via blipBufferCreate/Destroy below; the C
** caller never sees its members. */
struct BlipBuffer {
    std::array<float, BUFFER_SIZE> buffer;
    std::size_t offset;
    std::ptrdiff_t availSamp;
    float accum;
};

extern "C" BlipBuffer* blipBufferCreate(void)
{
    /* Touch the static impulse table once so first-use cost is paid here
    ** rather than on a sample-write path. */
    (void)getImpulses();

    BlipBuffer* bb = new (std::nothrow) BlipBuffer{};
    if (bb == nullptr) return nullptr;
    bb->buffer.fill(0.0f);
    bb->offset    = 0;
    bb->availSamp = 0;
    bb->accum     = 0.0f;
    return bb;
}

extern "C" void blipBufferDestroy(BlipBuffer* bb)
{
    delete bb;
}

extern "C" void blipBufferAddDelta(BlipBuffer* bb, unsigned time, float delta)
{
    if (bb == nullptr) return;
    if (delta == 0.0f) return;

    const unsigned intTime  = time >> BLIP_PHASE_BITS;
    const unsigned phase    = time & (BLIP_PHASE_UNIT - 1);
    const unsigned afterImp = intTime + BLIP_IMPULSE_WIDTH;

    /* If the caller never syncs long enough for the buffer to drain this
    ** shouldn't fire in practice; bail out instead of trashing memory. */
    if (afterImp >= BUFFER_SIZE) return;

    if (static_cast<std::ptrdiff_t>(afterImp) > bb->availSamp) {
        bb->availSamp = static_cast<std::ptrdiff_t>(afterImp);
    }

    const auto& impulses = getImpulses();
    const float* __restrict imp = impulses[phase].data();
    const std::size_t ofst = static_cast<std::size_t>(intTime) + bb->offset;

    if ((ofst + BLIP_IMPULSE_WIDTH) <= BUFFER_SIZE) {
        float* __restrict result = &bb->buffer[ofst];
        for (int i = 0; i < BLIP_IMPULSE_WIDTH; ++i) {
            result[i] += imp[i] * delta;
        }
    } else {
        for (int i = 0; i < BLIP_IMPULSE_WIDTH; ++i) {
            bb->buffer[(ofst + i) & BUFFER_MASK] += imp[i] * delta;
        }
    }
}

namespace {

/* Consume `samples` entries starting at bb->offset, draining the integrated
** accumulator (the delta stream stored in `bb->buffer`) into `out` with the
** given pitch. */
void readChunk(BlipBuffer* bb, float* __restrict out, std::size_t samples, int pitch)
{
    float acc = bb->accum;
    std::size_t ofst = bb->offset;
    for (std::size_t i = 0; i < samples; ++i) {
        out[i * pitch] = acc;
        acc *= BASS_FACTOR;
        acc += bb->buffer[ofst];
        bb->buffer[ofst] = 0.0f;
        ++ofst;
    }
    bb->accum  = acc;
    bb->offset = ofst & BUFFER_MASK;
}

} /* namespace */

extern "C" int blipBufferReadSamples(BlipBuffer* bb, float* out, int samples, int pitch)
{
    if (bb == nullptr || samples <= 0 || pitch <= 0) return 0;

    if (bb->availSamp <= 0) {
        /* Buffer is all zeros; decay the bass-filter tail toward 0 and let
        ** the caller short-circuit once it goes silent. */
        if (isSilent(bb->accum)) {
            return 0;
        }
        float acc = bb->accum;
        for (int i = 0; i < samples; ++i) {
            out[i * pitch] = acc;
            acc *= BASS_FACTOR;
        }
        bb->accum = acc;
        return 1;
    }

    bb->availSamp -= samples;

    std::size_t t1 = std::min<std::size_t>(static_cast<std::size_t>(samples),
                                           BUFFER_SIZE - bb->offset);
    readChunk(bb, out, t1, pitch);
    if (t1 < static_cast<std::size_t>(samples)) {
        std::size_t t2 = static_cast<std::size_t>(samples) - t1;
        readChunk(bb, out + static_cast<std::ptrdiff_t>(t1) * pitch, t2, pitch);
    }
    return 1;
}
