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

typedef struct Geo3DDevice Geo3DDevice;
struct VDP;

/* The one in the machine's V9968, on ports 9Dh and 9Fh. */
void geo3dDeviceCreate();

/* One beside a second V9968, at port and port + 2. Its owner resets, saves,
** loads and destroys it. */
Geo3DDevice* geo3dDeviceCreateSecondary(int port, struct VDP* vdp, const char* stateName, const char* name);
void geo3dDeviceDestroy(Geo3DDevice* dev);
void geo3dDeviceReset(Geo3DDevice* dev);
void geo3dDeviceSaveState(Geo3DDevice* dev);
void geo3dDeviceLoadState(Geo3DDevice* dev);

#endif
