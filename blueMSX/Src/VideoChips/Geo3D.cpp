/*****************************************************************************
**
** geo3d 3D coprocessor: C interface to Geo3DCore.
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
#include "Geo3D.h"
#include "Geo3DCore.h"
#include <cstdlib>
#include <cstring>

struct Geo3D {
    Geo3DCore core;
};

extern "C" Geo3D* geo3dCreate()
{
    return new Geo3D;
}

extern "C" void geo3dDestroy(Geo3D* geo3d)
{
    delete geo3d;
}

extern "C" void geo3dReset(Geo3D* geo3d)
{
    geo3d->core.reset();
}

extern "C" void geo3dWriteIndex(Geo3D* geo3d, UInt8 value)
{
    geo3d->core.writeIndex(value);
}

extern "C" void geo3dWriteData(Geo3D* geo3d, UInt8 value)
{
    geo3d->core.writeData(value);
}

extern "C" UInt8 geo3dReadStatus(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd)
{
    return geo3d->core.readStatus(ec, edgeEnd);
}

extern "C" UInt8 geo3dReadData(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd)
{
    return geo3d->core.readData(ec, edgeEnd);
}

extern "C" UInt8 geo3dPeekData(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd)
{
    return geo3d->core.peekData(ec, edgeEnd);
}

extern "C" int geo3dIsRunning(Geo3D* geo3d)
{
    return geo3d->core.isRunning() ? 1 : 0;
}

extern "C" int geo3dNextCommand(Geo3D* geo3d, UInt8* regs, UInt8* values)
{
    if (!geo3d->core.hasCommand()) {
        return 0;
    }
    const Geo3DCore::Command& c = geo3d->core.nextCommand();
    for (unsigned k = 0; k < c.size; k++) {
        regs[k]   = (UInt8)c.regNum(k);
        values[k] = c.bytes[k];
    }
    return c.size;
}

extern "C" void geo3dFinishRun(Geo3D* geo3d)
{
    geo3d->core.finishRun();
}

extern "C" int geo3dPeekCommand(Geo3D* geo3d, UInt32* ready)
{
    if (!geo3d->core.hasCommand()) {
        return 0;
    }
    const Geo3DCore::Command& c = geo3d->core.peekCommand();
    *ready = c.ready;
    return c.size;
}

extern "C" UInt32 geo3dEndClock(Geo3D* geo3d)
{
    return geo3d->core.endClock();
}

extern "C" int geo3dCommandsLeft(Geo3D* geo3d)
{
    return (int)geo3d->core.commandsLeft();
}

/* No exception may cross into the C callers: a state that cannot be made is
** saved empty, and one that cannot be read leaves the core reset. */
extern "C" UInt8* geo3dSaveState(Geo3D* geo3d, UInt32* size)
{
    UInt8* data = NULL;

    *size = 0;
    try {
        std::vector<uint8_t> state = geo3d->core.saveState();
        data = (UInt8*)malloc(state.size());
        if (data != NULL) {
            memcpy(data, state.data(), state.size());
            *size = (UInt32)state.size();
        }
    }
    catch (...) {
    }
    return data;
}

extern "C" void geo3dLoadState(Geo3D* geo3d, const UInt8* data, UInt32 size, int layout)
{
    geo3d->core.reset();
    try {
        geo3d->core.loadState(std::vector<uint8_t>(data, data + size), layout);
    }
    catch (...) {
        geo3d->core.reset();
    }
}
