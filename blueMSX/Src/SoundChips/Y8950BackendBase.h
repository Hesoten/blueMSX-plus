/*****************************************************************************
**
** Abstract base class shared by every Y8950 backend implementation.
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
/* Abstract chip-level interface that every Y8950 backend implements. */
#ifndef Y8950_BACKEND_BASE_H
#define Y8950_BACKEND_BASE_H

#include "../Common/MsxTypes.h"

class Y8950BackendBase
{
public:
    Y8950BackendBase() : loadHadOwnState_(false) {}
    virtual ~Y8950BackendBase() {}

    virtual void   reset() = 0;
    virtual void   setSampleRate(UInt32 rate) = 0;
    virtual Int32* updateBuffer(UInt32 count) = 0;

    /* port=0 latches address, port=1 transfers data. */
    virtual void   writeIo(int port, UInt8 value) = 0;
    virtual UInt8  readIo(int port) = 0;
    virtual UInt8  peekIo(int port) = 0;

    virtual UInt8  readReg(int reg) = 0;

    virtual void   saveState() = 0;
    virtual void   loadState() = 0;

    /* True iff loadState() found this backend's own chip-dump section. */
    bool lastLoadHadOwnState() const { return loadHadOwnState_; }

    /* DELTA-T sample RAM (debugger + cross-backend load-time seeding). */
    virtual const UInt8* getAdpcmRam(UInt32* size_out) = 0;
    virtual void   copyAdpcmRamFrom(const UInt8* src, UInt32 len) = 0;

    /* Dispatcher-driven Y8950 timer fire. */
    virtual void   onTimerOverflow(int timer_idx) {}

protected:
    bool loadHadOwnState_;
};

#endif
