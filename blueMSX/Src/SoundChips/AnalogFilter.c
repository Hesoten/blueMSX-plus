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
#include "AnalogFilter.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void analogFilterInit(AnalogFilter* f, UInt32 sampleRate, int lpfHz, int hpfHz)
{
    if (sampleRate == 0) {
        sampleRate = 44100;
    }

    /* LPF active only below Nyquist; HPF must stay strictly above 0 Hz.    */
    if (lpfHz > 0 && (UInt32)(2 * lpfHz) < sampleRate) {
        f->lpfA = (float)(1.0 - exp(-2.0 * M_PI * (double)lpfHz / (double)sampleRate));
        f->lpfActive = 1;
    } else {
        f->lpfA = 0.0f;
        f->lpfActive = 0;
    }

    if (hpfHz > 0 && (UInt32)(2 * hpfHz) < sampleRate) {
        f->hpfR = (float)exp(-2.0 * M_PI * (double)hpfHz / (double)sampleRate);
        f->hpfActive = 1;
    } else {
        f->hpfR = 1.0f;
        f->hpfActive = 0;
    }

    analogFilterReset(f);
}

void analogFilterReset(AnalogFilter* f)
{
    f->lpfPrevY = 0.0f;
    f->hpfPrevX = 0.0f;
    f->hpfPrevY = 0.0f;
}

void analogFilterProcess(AnalogFilter* f, Int32* buf, UInt32 count)
{
    UInt32 i;

    if (!f->lpfActive && !f->hpfActive) {
        return;
    }

    if (f->lpfActive && f->hpfActive) {
        const float a = f->lpfA;
        const float r = f->hpfR;
        float lpY = f->lpfPrevY;
        float hpX = f->hpfPrevX;
        float hpY = f->hpfPrevY;
        for (i = 0; i < count; i++) {
            float x = (float)buf[i];
            lpY += a * (x - lpY);
            hpY = (lpY - hpX) + r * hpY;
            hpX = lpY;
            buf[i] = (Int32)hpY;
        }
        f->lpfPrevY = lpY;
        f->hpfPrevX = hpX;
        f->hpfPrevY = hpY;
    }
    else if (f->lpfActive) {
        const float a = f->lpfA;
        float lpY = f->lpfPrevY;
        for (i = 0; i < count; i++) {
            float x = (float)buf[i];
            lpY += a * (x - lpY);
            buf[i] = (Int32)lpY;
        }
        f->lpfPrevY = lpY;
    }
    else {
        const float r = f->hpfR;
        float hpX = f->hpfPrevX;
        float hpY = f->hpfPrevY;
        for (i = 0; i < count; i++) {
            float x = (float)buf[i];
            hpY = (x - hpX) + r * hpY;
            hpX = x;
            buf[i] = (Int32)hpY;
        }
        f->hpfPrevX = hpX;
        f->hpfPrevY = hpY;
    }
}
