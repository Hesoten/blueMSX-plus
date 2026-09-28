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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Geo3D Geo3D;

Geo3D* geo3dCreate();
void   geo3dDestroy(Geo3D* geo3d);
void   geo3dReset(Geo3D* geo3d);

void   geo3dWriteIndex(Geo3D* geo3d, UInt8 value);
void   geo3dWriteData(Geo3D* geo3d, UInt8 value);
UInt8  geo3dReadStatus(Geo3D* geo3d);
UInt8  geo3dReadData(Geo3D* geo3d);
UInt8  geo3dPeekData(Geo3D* geo3d);

/* A RUN hands out its VDP commands one by one: the register numbers and
** values of the next one, in write order, and their count; 0 when none is
** left, and then geo3dFinishRun clears RUN busy. */
int    geo3dIsRunning(Geo3D* geo3d);
int    geo3dNextCommand(Geo3D* geo3d, UInt8* regs, UInt8* values);
void   geo3dFinishRun(Geo3D* geo3d);

/* The whole state as bytes, freed by the caller. */
UInt8* geo3dSaveState(Geo3D* geo3d, UInt32* size);
void   geo3dLoadState(Geo3D* geo3d, const UInt8* data, UInt32 size);

#ifdef __cplusplus
}
#endif

#endif
