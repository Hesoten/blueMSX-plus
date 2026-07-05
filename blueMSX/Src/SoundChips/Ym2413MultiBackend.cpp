/*****************************************************************************
**
** Active YM2413 backend selector.
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
#include "Ym2413MultiBackend.h"
#ifdef YM2413_BUILD_OPENMSX_INITIAL
#include "OpenMsxYM2413.h"
#endif
#include "Emu2413Backend.h"
#include "NukedOpllBackend.h"
extern "C" {
#include "Properties.h"
}
#include <cstring>

static int g_ym2413Active = PROP_YM2413_BACKEND_OPENMSX_2;

static const char* const kBackendDisplay[YM2413_BACKEND_COUNT] = {
    "openmsx",
    "openmsx_2",
    "emu2413",
    "nuked",
};

extern "C" const int ym2413BackendDisplayOrder[] = {
    PROP_YM2413_BACKEND_EMU2413,
    PROP_YM2413_BACKEND_NUKED,
    PROP_YM2413_BACKEND_OPENMSX_2,
};
extern "C" const int ym2413BackendDisplayCount =
    (int)(sizeof(ym2413BackendDisplayOrder) / sizeof(ym2413BackendDisplayOrder[0]));

/* Per-backend Enabled flag lookup.  Treats a NULL Properties (early
** startup / Mixer test path) as "all enabled". */
static int backendEnabledFromProperties(int idx)
{
    Properties* p = propGetGlobalProperties();
    if (p == NULL) {
        /* Defaults: emu2413 / openmsx_2 / nuked on; openmsx (initial) is
        ** dead-coded so it never reports enabled at runtime. */
        if (idx == PROP_YM2413_BACKEND_OPENMSX) return 0;
        return 1;
    }
    switch (idx) {
    case PROP_YM2413_BACKEND_OPENMSX:
#ifdef YM2413_BUILD_OPENMSX_INITIAL
        return p->sound.chip.ym2413BackendOpenmsxEnabled ? 1 : 0;
#else
        return 0;
#endif
    case PROP_YM2413_BACKEND_OPENMSX_2: return p->sound.chip.ym2413BackendOpenmsx2Enabled ? 1 : 0;
    case PROP_YM2413_BACKEND_EMU2413:   return p->sound.chip.ym2413BackendEmu2413Enabled  ? 1 : 0;
    case PROP_YM2413_BACKEND_NUKED:     return p->sound.chip.ym2413BackendNukedEnabled    ? 1 : 0;
    default: return 0;
    }
}

extern "C" int ym2413BackendActiveGet(void)
{
    return g_ym2413Active;
}

extern "C" void ym2413BackendActiveSet(int idx)
{
    if (idx >= 0 && idx < YM2413_BACKEND_COUNT && backendEnabledFromProperties(idx)) {
        g_ym2413Active = idx;
    }
}

extern "C" int ym2413BackendIsEnabled(int idx)
{
    return backendEnabledFromProperties(idx);
}

extern "C" int ym2413BackendCycle(void)
{
    /* Walk forward through ym2413BackendDisplayOrder, skipping any
    ** backends the user has disabled. */
    int curPos = -1;
    for (int i = 0; i < ym2413BackendDisplayCount; i++) {
        if (ym2413BackendDisplayOrder[i] == g_ym2413Active) { curPos = i; break; }
    }
    int start = (curPos < 0) ? 0 : (curPos + 1);
    for (int i = 0; i < ym2413BackendDisplayCount; i++) {
        int idx = ym2413BackendDisplayOrder[(start + i) % ym2413BackendDisplayCount];
        if (backendEnabledFromProperties(idx)) {
            g_ym2413Active = idx;
            return g_ym2413Active;
        }
    }
    return g_ym2413Active;
}

extern "C" const char* ym2413BackendName(int idx)
{
    if (idx < 0 || idx >= YM2413_BACKEND_COUNT) return "?";
    return kBackendDisplay[idx];
}

Ym2413MultiBackend::Ym2413MultiBackend(short volume)
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) backends[i] = NULL;

#ifdef YM2413_BUILD_OPENMSX_INITIAL
    if (backendEnabledFromProperties(PROP_YM2413_BACKEND_OPENMSX)) {
        backends[PROP_YM2413_BACKEND_OPENMSX] = new OpenYM2413("ym2413", volume, 0);
    }
#endif
    if (backendEnabledFromProperties(PROP_YM2413_BACKEND_OPENMSX_2)) {
        backends[PROP_YM2413_BACKEND_OPENMSX_2] = new OpenYM2413_2("ym2413", volume, 0);
    }
    if (backendEnabledFromProperties(PROP_YM2413_BACKEND_EMU2413)) {
        backends[PROP_YM2413_BACKEND_EMU2413] = new Emu2413Backend("ym2413", volume, 0);
    }
    if (backendEnabledFromProperties(PROP_YM2413_BACKEND_NUKED)) {
        backends[PROP_YM2413_BACKEND_NUKED] = new NukedOpllBackend("ym2413", volume, 0);
    }

    /* Snap g_ym2413Active onto an enabled slot so updateBuffer never
    ** dereferences NULL (a stale ini could point at a disabled slot). */
    Properties* p = propGetGlobalProperties();
    int desired = p ? p->sound.chip.ym2413BackendActive : g_ym2413Active;
    if (desired < 0 || desired >= YM2413_BACKEND_COUNT || backends[desired] == NULL) {
        desired = -1;
        for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
            if (backends[i] != NULL) { desired = i; break; }
        }
    }
    if (desired >= 0) g_ym2413Active = desired;
}

Ym2413MultiBackend::~Ym2413MultiBackend()
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        delete backends[i];
        backends[i] = NULL;
    }
}

void Ym2413MultiBackend::reset(const EmuTime& time)
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->reset(time);
    }
}

void Ym2413MultiBackend::writeReg(byte r, byte v, const EmuTime& time)
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->writeReg(r, v, time);
    }
}

byte Ym2413MultiBackend::peekReg(byte r)
{
    if (backends[g_ym2413Active]) return backends[g_ym2413Active]->peekReg(r);
    /* Fallback: pick any live backend so the debugger can still read regs. */
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (backends[i]) return backends[i]->peekReg(r);
    }
    return 0;
}

void Ym2413MultiBackend::setInternalVolume(short newVolume)
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->setVolume(newVolume);
    }
}

int* Ym2413MultiBackend::updateBuffer(int length)
{
    /* Run every live backend so envelope / LFO state stays in lockstep
    ** with the running music and switching the active selection is
    ** glitch-free.  Only forward the active backend's samples to the
    ** Mixer.  An empty updateBuffer return propagates as silence rather
    ** than a sibling's output, matching standalone behaviour. */
    int* result = NULL;
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (!backends[i]) continue;
        int* b = backends[i]->updateBuffer(length);
        if (i == g_ym2413Active) result = b;
    }
    return result;
}

void Ym2413MultiBackend::setSampleRate(int sampleRate, int oversampling)
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->setSampleRate(sampleRate, oversampling);
    }
}

void Ym2413MultiBackend::loadState()
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->loadState();
    }
}

void Ym2413MultiBackend::saveState()
{
    for (int i = 0; i < YM2413_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->saveState();
    }
}
