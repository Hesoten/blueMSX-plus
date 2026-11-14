/*****************************************************************************
**
** Y8960 cartridge - the pan and volume laws its registers share.
**
** Copyright (C) 2026 madscient
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
#ifndef Y8960_PAN_H
#define Y8960_PAN_H

#include "MsxTypes.h"
#include <math.h>

/* Gains are 4.12 fixed point: 4096 is unity. */
#define Y8960_GAIN_ONE   4096
#define Y8960_PAN_CENTER 128

/* The pan law of the Y8960's per-channel pan registers (OPLL 50h-58h, OPL2
** F6h-FFh, SSGS 28h-2Eh, SCC 9900h-9904h): 1 is hard left, 128 the centre,
** 255 hard right, and 0 counts as the centre.
**
** The specification gives only those anchors. Between them this is power
** panning, L = cos(t), R = sin(t) with t running from 0 to pi/2, so the
** centre sits at 1/sqrt(2) on both sides, not normalised (decided
** 2026-10-16). The value is worked out for the left half and mirrored, so
** that v and 256-v are exact images of each other rather than cos and sin a
** rounding apart, and the centre is one figure for both sides. */
static void y8960PanGain(UInt8 pan, Int32 gain[2])
{
    const double halfPi = 3.14159265358979323846 / 2.0;
    Int32 l, r;

    if (pan == 0 || pan == Y8960_PAN_CENTER) {
        l = r = (Int32)floor(sqrt(0.5) * Y8960_GAIN_ONE + 0.5);
    }
    else {
        int v = pan < Y8960_PAN_CENTER ? pan : 256 - pan;
        double t = (double)(v - 1) / 254.0 * halfPi;
        l = (Int32)floor(cos(t) * Y8960_GAIN_ONE + 0.5);
        r = (Int32)floor(sin(t) * Y8960_GAIN_ONE + 0.5);
        if (pan > Y8960_PAN_CENTER) {
            Int32 swap = l; l = r; r = swap;
        }
    }
    gain[0] = l;
    gain[1] = r;
}

/* The gain of a volume register (the mixer's 7FF7h-7FFAh and 7FFEh, the
** SSGS's 20h-26h): 0 is mute, 1-127 is value / 128, and bit 7 set is unity,
** so the top of the range joins the maximum without a step. The sheet's
** "0: OFF, ~128: MAX"; the curve below 128 was decided 2026-10-17. */
static Int32 y8960VolumeGain(UInt8 volume)
{
    if (volume & 0x80) {
        return Y8960_GAIN_ONE;
    }
    return (Int32)volume * Y8960_GAIN_ONE / 128;
}

#endif
