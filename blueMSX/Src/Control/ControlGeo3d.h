/*****************************************************************************
**
** Text listings of a geo3d's RUNs, faces and edges.
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
#ifndef CONTROL_GEO3D_H
#define CONTROL_GEO3D_H

#include "ControlText.h"
#include "MsxTypes.h"

/* What a geo3d call came to: the text was written, or why not (err says more). */
#define CTRL_GEO3D_OK        0
#define CTRL_GEO3D_NONE      1   /* that chip has no geo3d */
#define CTRL_GEO3D_NO_RUN    2   /* no such RUN, or no such face or edge in it */
#define CTRL_GEO3D_NOT_KEPT  3   /* the RUN is older than the ones the device keeps */

/* How many faces or edges a RUN listing shows when no range is asked for. */
#define CTRL_GEO3D_ITEMS_SHOWN 64

/* All need a held view. cartridge picks the cartridge's geo3d, else the machine's. */

/* The device, every RUN it keeps, and the configuration words that changed since the latest. */
int ctrlGeo3dSummary(CtrlText* text, int cartridge, char* err, int errLen);

/* RUN seq (0: the latest) and its faces or edges numbered first to last, at most
** CTRL_GEO3D_ITEMS_SHOWN when last is -1. */
int ctrlGeo3dRun(CtrlText* text, int cartridge, UInt32 seq, int first, int last,
                 char* err, int errLen);

/* Face or edge number index of RUN seq (0: the latest): vertices, rows and VDP commands. */
int ctrlGeo3dItem(CtrlText* text, int cartridge, UInt32 seq, int index, char* err, int errLen);

#endif
