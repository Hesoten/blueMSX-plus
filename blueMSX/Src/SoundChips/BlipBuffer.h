/*****************************************************************************
**
** Click-free audio mixer buffer.
** Ported from openMSX's BlipBuffer.{hh,cc}; the openMSX file is itself
** heavily based on Shay Green's Blip_Buffer 0.4.0
** (http://www.slack.net/~ant/).
** Upstream openMSX is distributed under the GNU GPL v2 or later;
** Blip_Buffer is distributed under the GNU LGPL.
**
** Copyright (C) 2026 Hesoten (blueMSX port)
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
/* BlipBuffer -- band-limited step buffering, C-callable interface. */
#ifndef BLIP_BUFFER_H
#define BLIP_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fractional bits of time precision passed to blipBufferAddDelta.  Match
** the openMSX BLIP_PHASE_BITS so the kernel table is identical. */
#define BLIP_PHASE_BITS  10
#define BLIP_PHASE_UNIT  (1 << BLIP_PHASE_BITS)   /* = 1024 */

typedef struct BlipBuffer BlipBuffer;

BlipBuffer* blipBufferCreate(void);
void        blipBufferDestroy(BlipBuffer* bb);

/* Push a step transition.
**   time:  fixed-point output-sample offset since the last
**          blipBufferReadSamples() call.  Integer part is the sample index,
**          low BLIP_PHASE_BITS are the sub-sample phase.  Must satisfy
**          (time >> BLIP_PHASE_BITS) + 16 < 16384 (buffer size).
**   delta: amplitude change at that instant (new - old). */
void blipBufferAddDelta(BlipBuffer* bb, unsigned time, float delta);

/* Pull `samples` output values into `out`, stored at out[0], out[pitch],
** out[2*pitch], ... .  Returns 1 if any sample is non-silent, 0 if fully
** quiescent (caller can short-circuit). */
int  blipBufferReadSamples(BlipBuffer* bb, float* out, int samples, int pitch);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BLIP_BUFFER_H */
