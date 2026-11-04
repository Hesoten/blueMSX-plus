/*****************************************************************************
**
** geo3d 3D coprocessor of the V9968 on I/O ports 9Dh/9Fh or 8Dh/8Fh.
** on the V9968 cartridge.
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
#ifndef GEO3D_DEVICE_H
#define GEO3D_DEVICE_H

#include "MsxTypes.h"
#include "Geo3DDebug.h"

/* Trace kinds of class GEO3D; a is the RUN number unless noted. RUN: BEGIN b = items,
** its CONT a = first | count << 16, b = vfirst | nvert << 16; END b = commands. ISSUE, at
** its due time: a = command index, b = RUN. WAIT, the CPU on RUN busy: END a = busy reads. */
#define GEO3D_TRACE_RUN       1
#define GEO3D_TRACE_XFORM_END 2
#define GEO3D_TRACE_ISSUE     3
#define GEO3D_TRACE_WAIT      4
/* FEED, just before each RUN BEGIN at its time: the bytes the CPU wrote to the data port
** since the RUN before, a to the model RAMs (indices 50h-53h), b to the rest. */
#define GEO3D_TRACE_FEED      5

typedef struct Geo3DDevice Geo3DDevice;
struct VDP;
struct Geo3D;

/* The one in the machine's V9968, on ports 9Dh and 9Fh. */
void geo3dDeviceCreate();

/* One beside a second V9968, at port and port + 2. Its owner resets, saves,
** loads and destroys it. */
Geo3DDevice* geo3dDeviceCreateSecondary(int port, struct VDP* vdp, const char* stateName, const char* name);
void geo3dDeviceDestroy(Geo3DDevice* dev);
void geo3dDeviceReset(Geo3DDevice* dev);
void geo3dDeviceSaveState(Geo3DDevice* dev);
void geo3dDeviceLoadState(Geo3DDevice* dev);

/* Copies the geo3d beside vdp (NULL: the machine's) and the RUNs busy in the
** VDP's frame so far (at that VDP's own frame tap, the frame just ended), else
** the latest RUN, on the terms of vdpDebugCapture. 0 and all cleared for none. */
int geo3dDebugCapture(struct VDP* vdp, Geo3DDebugState* out);

/* From a tool, any thread: it holds RUN seq and those before it of epoch, so the
** captures of that geo3d (cartridge as in its snapshot) add every RUN after seq. */
void geo3dDebugSince(int cartridge, UInt32 epoch, UInt32 seq);

/* Called on the emulation thread as a RUN of a geo3d the captures copy starts, its items
** and commands made; cartridge as in its snapshot, startTime in board ticks. Must not wait. */
typedef void (*Geo3DDebugRunSink)(int cartridge, UInt32 epoch, UInt32 seq, struct Geo3D* geo3d, UInt32 startTime);
/* From a tool: NULL stops the calls. */
void geo3dDebugSetRunSink(Geo3DDebugRunSink sink);

#endif
