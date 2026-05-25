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
/* Holds per-slot backends; only the Properties-active one drives the
** mixer, but all enabled backends are kept in lockstep via reg-write
** broadcast so switching is glitch-free. */
#ifndef Y8950_MULTI_BACKEND_H
#define Y8950_MULTI_BACKEND_H

#include "Y8950BackendBase.h"
extern "C" {
#include "Properties.h"
}

#define Y8950_BACKEND_COUNT PROP_Y8950_BACKEND_COUNT

class Y8950MultiBackend : public Y8950BackendBase
{
public:
    Y8950MultiBackend(void* hostRef);
    virtual ~Y8950MultiBackend();

    virtual void   reset();
    virtual void   setSampleRate(UInt32 rate);
    virtual Int32* updateBuffer(UInt32 count);

    virtual void   writeIo(int port, UInt8 value);
    virtual UInt8  readIo(int port);
    virtual UInt8  peekIo(int port);
    virtual UInt8  readReg(int reg);

    virtual void   saveState();
    virtual void   loadState();

    virtual const UInt8* getAdpcmRam(UInt32* size_out);
    virtual void   copyAdpcmRamFrom(const UInt8* src, UInt32 len);

    virtual void   onTimerOverflow(int timer_idx);

private:
    Y8950BackendBase* pickRamSourceBackend() const;
    void              replayRegistersTo(Y8950BackendBase* b);

    Y8950BackendBase* backends[Y8950_BACKEND_COUNT];
    UInt8          latchedAddr;
    UInt8          regCache[256];  /* last bus-side value per address */
};

#ifdef __cplusplus
extern "C" {
#endif

int         y8950BackendActiveGet(void);
void        y8950BackendActiveSet(int idx);
int         y8950BackendIsEnabled(int idx);
int         y8950BackendCycle(void);
const char* y8950BackendName(int idx);

extern const int y8950BackendDisplayOrder[];
extern const int y8950BackendDisplayCount;

#ifdef __cplusplus
}
#endif

#endif
