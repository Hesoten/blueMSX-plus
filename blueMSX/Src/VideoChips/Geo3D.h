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
#ifndef GEO3D_H
#define GEO3D_H

#include "MsxTypes.h"
#include "Geo3DDebug.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Geo3D Geo3D;

Geo3D* geo3dCreate();
void   geo3dDestroy(Geo3D* geo3d);
void   geo3dReset(Geo3D* geo3d);

void   geo3dWriteIndex(Geo3D* geo3d, UInt8 value);
void   geo3dWriteData(Geo3D* geo3d, UInt8 value);
/* ec: engine clocks since RUN; edgeEnd: when the producer ends, same scale. */
UInt8  geo3dReadStatus(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd);
UInt8  geo3dReadData(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd);
UInt8  geo3dPeekData(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd);

/* A RUN hands out its VDP commands one by one: the register numbers and
** values of the next one, in write order, and their count; 0 when none is
** left, and then geo3dFinishRun clears RUN busy. */
int    geo3dIsRunning(Geo3D* geo3d);
int    geo3dNextCommand(Geo3D* geo3d, UInt8* regs, UInt8* values);
void   geo3dFinishRun(Geo3D* geo3d);

/* 1 while CTRL selects faces: a RUN started then draws faces, not edges. */
int    geo3dFacesOn(Geo3D* geo3d);

/* Engine clocks (2 per board tick) from the RUN: when the producer, never
** stalled, has the next command ready (returns its register count, 0 for
** none), and when it reaches its end. */
int    geo3dPeekCommand(Geo3D* geo3d, UInt32* ready);
UInt32 geo3dEndClock(Geo3D* geo3d);
int    geo3dCommandsLeft(Geo3D* geo3d);

/* The whole state as bytes, freed by the caller. */
UInt8* geo3dSaveState(Geo3D* geo3d, UInt32* size);
/* layout: the device state version the bytes were saved with, which picks their format. */
void   geo3dLoadState(Geo3D* geo3d, const UInt8* data, UInt32 size, int layout);

/* The latest RUN since reset or a state load (0 for none), and whether the
** record of RUN seq is still kept. */
UInt32 geo3dDebugLastSeq(Geo3D* geo3d);
int    geo3dDebugHasRun(Geo3D* geo3d, UInt32 seq);
/* RUN seq's YPAGE, or -1 once its record is gone. */
int    geo3dDebugRunYpage(Geo3D* geo3d, UInt32 seq);
/* Fills the core's part of out without changing anything: registers (ec and
** edgeEnd as for the status), RAMs, and the RUNs in seqs, oldest first; seqs[seen]
** and later get room first, then the earlier. Leaves the device's fields alone. */
void   geo3dDebugCopy(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd,
                      const UInt32* seqs, int count, int seen, Geo3DDebugState* out);
/* The most items and commands the core keeps of its latest RUNs. */
#define GEO3D_DEBUG_CORE_ITEMS 2048
#define GEO3D_DEBUG_CORE_CMDS  4096
/* RUN seq as geo3dDebugCopy has it, its items and commands numbered from 0 in items and cmds,
** with room for itemRoom and cmdRoom; the device's fields stay 0. 0 once its record is gone. */
int    geo3dDebugCopyRun(Geo3D* geo3d, UInt32 seq, Geo3DDebugRun* run,
                         Geo3DDebugItem* items, int itemRoom, Geo3DDebugCmd* cmds, int cmdRoom);
/* Vertex v projected as a RUN would with the configuration words cfg (M00-H). */
Geo3DDebugProj geo3dDebugProject(const Int32* cfg, const Int16* v);

#ifdef __cplusplus
}
#endif

#endif
