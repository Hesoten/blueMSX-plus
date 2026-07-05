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
/* Holds the per-slot OpenYM2413Base pointers (OpenYM2413 [dead-coded],
** OpenYM2413_2, EMU2413, Nuked-OPLL).  Only the backends marked enabled
** in Properties->sound.chip.ym2413Backend*Enabled are actually
** instantiated -- disabled slots stay NULL.  Of the enabled set, the
** active one (Properties->sound.chip.ym2413BackendActive) feeds the
** mixer; all other live backends are kept in lockstep on every register
** write so the user can flip the active selection glitch-free. */
#ifndef YM2413_MULTI_BACKEND_H
#define YM2413_MULTI_BACKEND_H

#include "OpenMsxYM2413_2.h"
extern "C" {
#include "Properties.h"
}

#define YM2413_BACKEND_COUNT PROP_YM2413_BACKEND_COUNT

class Ym2413MultiBackend : public OpenYM2413Base
{
public:
    Ym2413MultiBackend(short volume);
    virtual ~Ym2413MultiBackend();

    virtual void reset(const EmuTime& time);
    virtual void writeReg(byte r, byte v, const EmuTime& time);
    virtual byte peekReg(byte r);

    virtual void setInternalVolume(short newVolume);
    virtual int* updateBuffer(int length);
    virtual void setSampleRate(int sampleRate, int oversampling);

    virtual void loadState();
    virtual void saveState();

private:
    OpenYM2413Base* backends[YM2413_BACKEND_COUNT];
};

extern "C" {
/* Process-wide active-backend selector and the matching enabled mask.
** ym2413BackendIsEnabled(idx) returns 1 when backend idx is currently
** instantiated in every Ym2413MultiBackend.  ym2413BackendCycle walks
** ym2413BackendDisplayOrder and skips disabled slots so the cycle lands
** on the next enabled entry shown in the UI. */
int         ym2413BackendActiveGet(void);
void        ym2413BackendActiveSet(int idx);
int         ym2413BackendIsEnabled(int idx);
int         ym2413BackendCycle(void);
const char* ym2413BackendName(int idx);

/* User-visible order, shared by cycle / dropdown.  openmsx (initial) is
** dead-coded and never appears here. */
extern const int ym2413BackendDisplayOrder[];
extern const int ym2413BackendDisplayCount;
}

#endif
