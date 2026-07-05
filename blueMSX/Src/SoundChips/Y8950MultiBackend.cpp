/*****************************************************************************
**
** Active Y8950 backend selector.
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
#include "Y8950MultiBackend.h"
#include "FmoplBackend.h"
#include "Emu8950Backend.h"
#include "OpenMsxY8950Backend.h"
extern "C" {
#include "SaveState.h"
}
#include <cstddef>
#include <cstdlib>

static int s_active = PROP_Y8950_BACKEND_FMOPL;

extern "C" const int y8950BackendDisplayOrder[] = {
    PROP_Y8950_BACKEND_EMU8950,
    PROP_Y8950_BACKEND_OPENMSX,
    PROP_Y8950_BACKEND_FMOPL,
};
extern "C" const int y8950BackendDisplayCount =
    (int)(sizeof(y8950BackendDisplayOrder) / sizeof(y8950BackendDisplayOrder[0]));

static int backendEnabledFromProperties(int idx)
{
    Properties* p = propGetGlobalProperties();
    if (p == NULL) return 1;
    switch (idx) {
    case PROP_Y8950_BACKEND_FMOPL:   return p->sound.chip.y8950BackendFmoplEnabled   ? 1 : 0;
    case PROP_Y8950_BACKEND_EMU8950: return p->sound.chip.y8950BackendEmu8950Enabled ? 1 : 0;
    case PROP_Y8950_BACKEND_OPENMSX: return p->sound.chip.y8950BackendOpenmsxEnabled ? 1 : 0;
    default: return 0;
    }
}

extern "C" int y8950BackendActiveGet(void)        { return s_active; }
extern "C" int y8950BackendIsEnabled(int idx)     { return backendEnabledFromProperties(idx); }

extern "C" void y8950BackendActiveSet(int idx)
{
    if (idx >= 0 && idx < Y8950_BACKEND_COUNT && backendEnabledFromProperties(idx)) {
        s_active = idx;
    }
}

extern "C" int y8950BackendCycle(void)
{
    int curPos = -1;
    for (int i = 0; i < y8950BackendDisplayCount; i++) {
        if (y8950BackendDisplayOrder[i] == s_active) { curPos = i; break; }
    }
    int start = (curPos < 0) ? 0 : (curPos + 1);
    for (int i = 0; i < y8950BackendDisplayCount; i++) {
        int idx = y8950BackendDisplayOrder[(start + i) % y8950BackendDisplayCount];
        if (backendEnabledFromProperties(idx)) {
            s_active = idx;
            return s_active;
        }
    }
    return s_active;
}

extern "C" const char* y8950BackendName(int idx)
{
    switch (idx) {
    case PROP_Y8950_BACKEND_FMOPL:   return "fmopl";
    case PROP_Y8950_BACKEND_EMU8950: return "emu8950";
    case PROP_Y8950_BACKEND_OPENMSX: return "openmsx";
    default: return "?";
    }
}

Y8950MultiBackend::Y8950MultiBackend(void* hostRef)
    : latchedAddr(0)
{
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) backends[i] = NULL;
    for (int i = 0; i < (int)sizeof(regCache); i++) regCache[i] = 0;

    if (backendEnabledFromProperties(PROP_Y8950_BACKEND_FMOPL)) {
        backends[PROP_Y8950_BACKEND_FMOPL] = new FmoplBackend(hostRef);
    }
    if (backendEnabledFromProperties(PROP_Y8950_BACKEND_EMU8950)) {
        backends[PROP_Y8950_BACKEND_EMU8950] = new Emu8950Backend();
    }
    if (backendEnabledFromProperties(PROP_Y8950_BACKEND_OPENMSX)) {
        backends[PROP_Y8950_BACKEND_OPENMSX] = new OpenMsxY8950Backend();
    }

    /* Snap s_active onto an enabled slot so audio dispatch never sees a
    ** NULL backend.  Initial value is taken from Properties; a stale
    ** .ini that points at a disabled backend would leave us silent. */
    Properties* p = propGetGlobalProperties();
    int desired = p ? p->sound.chip.y8950BackendActive : s_active;
    if (desired < 0 || desired >= Y8950_BACKEND_COUNT || backends[desired] == NULL) {
        desired = -1;
        for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
            if (backends[i] != NULL) { desired = i; break; }
        }
    }
    if (desired >= 0) s_active = desired;
}

Y8950MultiBackend::~Y8950MultiBackend()
{
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        delete backends[i];
        backends[i] = NULL;
    }
}

void Y8950MultiBackend::reset()
{
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->reset();
    }
}

void Y8950MultiBackend::setSampleRate(UInt32 rate)
{
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->setSampleRate(rate);
    }
}

Int32* Y8950MultiBackend::updateBuffer(UInt32 count)
{
    /* Drive every live backend so envelope / LFO state stays in lockstep
    ** with the running music and switching the active selection is
    ** glitch-free.  Only forward the active backend's samples to the
    ** Mixer. */
    Int32* result = NULL;
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (!backends[i]) continue;
        Int32* b = backends[i]->updateBuffer(count);
        if (i == s_active) result = b;
    }
    return result;
}

void Y8950MultiBackend::writeIo(int port, UInt8 value)
{
    if ((port & 1) == 0) {
        latchedAddr = value;
        for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
            if (backends[i]) backends[i]->writeIo(port, value);
        }
        return;
    }
    /* Record the bus-side value (pre-strip) so cross-backend register
    ** replay sees what the Z80 actually wrote. */
    regCache[latchedAddr] = value;
    /* Strip reg 0x04 timer-start bits for non-fmopl backends so their
    ** internal counters do not double-fire the dispatcher's boardTimer. */
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (!backends[i]) continue;
        UInt8 v = value;
        if (latchedAddr == 0x04 && i != PROP_Y8950_BACKEND_FMOPL && !(value & 0x80)) {
            v &= ~0x03;
        }
        backends[i]->writeIo(port, v);
    }
}

UInt8 Y8950MultiBackend::readIo(int port)
{
    if (backends[s_active]) return backends[s_active]->readIo(port);
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) return backends[i]->readIo(port);
    }
    return 0xff;
}

UInt8 Y8950MultiBackend::peekIo(int port)
{
    if (backends[s_active]) return backends[s_active]->peekIo(port);
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) return backends[i]->peekIo(port);
    }
    return 0xff;
}

UInt8 Y8950MultiBackend::readReg(int reg)
{
    if (backends[s_active]) return backends[s_active]->readReg(reg);
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) return backends[i]->readReg(reg);
    }
    return 0;
}

/* Reg-write broadcast keeps RAM in lockstep across backends; any
** non-NULL slot is a valid source (active preferred for hot cache). */
Y8950BackendBase* Y8950MultiBackend::pickRamSourceBackend() const
{
    if (backends[s_active]) return backends[s_active];
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) return backends[i];
    }
    return NULL;
}

void Y8950MultiBackend::saveState()
{
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->saveState();
    }

    /* Dispatcher snapshot for cross-backend replay (newly enabled
    ** backends get re-programmed even without their own chip dump). */
    {
        SaveState* s = saveStateOpenForWrite("y8950_regs");
        saveStateSetBuffer(s, "regs", regCache, sizeof(regCache));
        saveStateClose(s);
    }

    /* ADPCM RAM is dispatcher-level state, not fmopl-private. */
    Y8950BackendBase* src = pickRamSourceBackend();
    if (src) {
        UInt32 size = 0;
        const UInt8* ram = src->getAdpcmRam(&size);
        if (ram && size > 0) {
            SaveState* s = saveStateOpenForWrite("y8950_adpcm_ram");
            saveStateSet(s, "size", size);
            saveStateSetBuffer(s, "ram", (void*)ram, size);
            saveStateClose(s);
        }
    }
}

void Y8950MultiBackend::loadState()
{
    /* Backends report own-state presence via lastLoadHadOwnState();
    ** a pre-probe here would consume the SaveState per-name index. */
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->loadState();
    }

    /* Legacy saves have no y8950_regs section -- no replay possible. */
    SaveState* rc = saveStateOpenForRead("y8950_regs");
    bool haveRegs = !saveStateIsEmpty(rc);
    if (haveRegs) {
        saveStateGetBuffer(rc, "regs", regCache, sizeof(regCache));
    }
    saveStateClose(rc);

    if (haveRegs) {
        for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
            if (!backends[i] || backends[i]->lastLoadHadOwnState()) continue;
            replayRegistersTo(backends[i]);
        }
    }

    /* New saves carry RAM here; legacy saves fall back to fmopl. */
    SaveState* s = saveStateOpenForRead("y8950_adpcm_ram");
    if (!saveStateIsEmpty(s)) {
        UInt32 size = saveStateGet(s, "size", 0);
        if (size > 0) {
            UInt8* buf = (UInt8*)malloc(size);
            if (buf) {
                saveStateGetBuffer(s, "ram", buf, size);
                for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
                    if (backends[i]) backends[i]->copyAdpcmRamFrom(buf, size);
                }
                free(buf);
            }
        }
        saveStateClose(s);
    } else {
        saveStateClose(s);
        if (backends[PROP_Y8950_BACKEND_FMOPL]) {
            UInt32 size = 0;
            const UInt8* src =
                backends[PROP_Y8950_BACKEND_FMOPL]->getAdpcmRam(&size);
            if (src && size > 0) {
                for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
                    if (i == PROP_Y8950_BACKEND_FMOPL) continue;
                    if (backends[i]) backends[i]->copyAdpcmRamFrom(src, size);
                }
            }
        }
    }
}

/* Replay bus-side register history into a backend with no own dump.
** Skips dispatcher-owned timer regs (0x02-0x04) + ADPCM live state
** (0x07/0x0F).  reg 0x04 replayed separately with side bits stripped. */
void Y8950MultiBackend::replayRegistersTo(Y8950BackendBase* b)
{
    UInt32 irqBefore = boardGetInt(0x10);

    for (int r = 0; r < 256; r++) {
        if (r == 0x02 || r == 0x03 || r == 0x04 ||
            r == 0x07 || r == 0x0F) continue;
        b->writeIo(0, (UInt8)r);
        b->writeIo(1, regCache[r]);
    }
    /* 0x04 written with timer-start + IRQ-reset bits stripped: the
    ** value matters even when 0, since it programs statusmask. */
    UInt8 r04 = (UInt8)(regCache[0x04] & ~0x83);
    b->writeIo(0, 0x04);
    b->writeIo(1, r04);
    b->writeIo(0, latchedAddr);

    /* Preserve sibling-asserted IRQ across openmsx writeReg(0x04)
    ** side effects (irq.set / irq.reset via changeStatusMask). */
    UInt32 irqAfter = boardGetInt(0x10);
    if (irqBefore && !irqAfter)       boardSetInt(0x10);
    else if (!irqBefore && irqAfter)  boardClearInt(0x10);
}

const UInt8* Y8950MultiBackend::getAdpcmRam(UInt32* size_out)
{
    if (backends[s_active]) return backends[s_active]->getAdpcmRam(size_out);
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) return backends[i]->getAdpcmRam(size_out);
    }
    if (size_out) *size_out = 0;
    return NULL;
}

void Y8950MultiBackend::copyAdpcmRamFrom(const UInt8* src, UInt32 len)
{
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->copyAdpcmRamFrom(src, len);
    }
}

/* Broadcast so a runtime backend switch shows the correct timer state. */
void Y8950MultiBackend::onTimerOverflow(int timer_idx)
{
    for (int i = 0; i < Y8950_BACKEND_COUNT; i++) {
        if (backends[i]) backends[i]->onTimerOverflow(timer_idx);
    }
}
