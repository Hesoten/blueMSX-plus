/*****************************************************************************
**
** Analogue low-pass / high-pass filter for sound chip output.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
/* Optional 1-pole IIR low-pass (treble roll-off) and high-pass (DC
** blocking) stages for sound-chip output.  Either stage is bypassed
** when its cutoff is 0 Hz. */
#ifndef ANALOG_FILTER_H
#define ANALOG_FILTER_H

#include "MsxTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnalogFilter {
    float lpfPrevY;
    float hpfPrevX;
    float hpfPrevY;
    float lpfA;       /* 1 - exp(-2*pi*fc/fs)  */
    float hpfR;       /* exp(-2*pi*fc/fs)      */
    int   lpfActive;
    int   hpfActive;
} AnalogFilter;

/* lpfHz / hpfHz: 0 disables that stage.                                    */
/* When sampleRate is 0, defaults to 44100 to avoid divide-by-zero.         */
void analogFilterInit(AnalogFilter* f, UInt32 sampleRate, int lpfHz, int hpfHz);

/* Clears delay-line state. Call on chip reset.                             */
void analogFilterReset(AnalogFilter* f);

/* Filters count Int32 mono samples in place. No-op when both stages are    */
/* bypassed.                                                                */
void analogFilterProcess(AnalogFilter* f, Int32* buf, UInt32 count);

#ifdef __cplusplus
}
#endif

#endif /* ANALOG_FILTER_H */
