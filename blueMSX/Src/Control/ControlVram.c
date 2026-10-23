/*****************************************************************************
**
** VRAM by screen mode.
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
#include "ControlVram.h"

/* VDP mode numbers match BASIC SCREEN numbers except 13, which is SCREEN 0 WIDTH 80. */
const char* ctrlVramModeName(int mode)
{
    switch (mode) {
    case 0:  return "SCREEN 0, 40 column text";
    case 1:  return "SCREEN 1";
    case 2:  return "SCREEN 2";
    case 3:  return "SCREEN 3";
    case 4:  return "SCREEN 4";
    case 5:  return "SCREEN 5";
    case 6:  return "SCREEN 6";
    case 7:  return "SCREEN 7";
    case 8:  return "SCREEN 8";
    /* YJK with and without YAE share this number; R25 bit 3 separates them. */
    case 10: return "SCREEN 10 or 11";
    case 12: return "SCREEN 12";
    case 13: return "SCREEN 0 WIDTH 80, 80 column text";
    default: return "not a display mode";
    }
}
