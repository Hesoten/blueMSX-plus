/*****************************************************************************
**
** Video9000 video switch with V9990 superimpose.
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
#ifndef VIDEO9000_H
#define VIDEO9000_H

#include "MsxTypes.h"
#include "V9990.h"

typedef struct Video9000 Video9000;

/* Adds the "Video9000" video source, which shows the machine's VDP (the
** first video source), the V9990 or both as port 0x6F selects. */
Video9000* video9000Create(V9990* v9990);
void video9000Destroy(Video9000* video9000);
void video9000Reset(Video9000* video9000);
void video9000Write(Video9000* video9000, UInt8 value);
void video9000SaveState(Video9000* video9000);
void video9000LoadState(Video9000* video9000);

#endif
