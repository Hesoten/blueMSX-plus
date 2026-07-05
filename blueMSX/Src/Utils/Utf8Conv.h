/*****************************************************************************
**
** UTF-8 / UTF-16 / ACP string conversion helpers.
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
/* Header-only; pulls only <windows.h>.  Non-Windows targets see an
** empty file. */
#ifndef UTILS_UTF8_CONV_H
#define UTILS_UTF8_CONV_H

#ifdef _WIN32

#include <windows.h>
#include <string.h>

/* UTF-8 -> wchar_t. dstCap counts wchar_t units. */
static __inline int Utf8ToWide(const char* src, wchar_t* dst, int dstCap)
{
    return MultiByteToWideChar(CP_UTF8, 0, src ? src : "", -1, dst, dstCap);
}

/* UTF-8 -> wchar_t with explicit byte length (no NUL appended in dst on -1). */
static __inline int Utf8ToWideN(const char* src, int srcBytes, wchar_t* dst, int dstCap)
{
    return MultiByteToWideChar(CP_UTF8, 0, src ? src : "", srcBytes, dst, dstCap);
}

/* wchar_t -> UTF-8. dstCap counts bytes. */
static __inline int WideToUtf8(const wchar_t* src, char* dst, int dstCap)
{
    return WideCharToMultiByte(CP_UTF8, 0, src ? src : L"", -1, dst, dstCap, NULL, NULL);
}

/* wchar_t -> ACP (for filenames passed to legacy ANSI fopen / FindFirstFileA;
** non-ACP code points are lost, matching the pre-/utf-8 behavior). */
static __inline int WideToAcp(const wchar_t* src, char* dst, int dstCap)
{
    return WideCharToMultiByte(CP_ACP, 0, src ? src : L"", -1, dst, dstCap, NULL, NULL);
}

/* Path bytes -> wchar_t. Tries UTF-8 first; falls back to ACP for legacy
** ACP-encoded paths in saved INI / history (no migration step needed). */
static __inline int PathToWide(const char* path, wchar_t* dst, int dstCap)
{
    int n;
    if (!path) { dst[0] = 0; return 0; }
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, dst, dstCap);
    if (n == 0) {
        n = MultiByteToWideChar(CP_ACP, 0, path, -1, dst, dstCap);
    }
    return n;
}

/* Pass-through convert. Valid UTF-8 stays unchanged; otherwise bytes are
** decoded as ACP (legacy zip entries, ACP-encoded INI values, etc.) and
** re-encoded as UTF-8 so display APIs / SetWindowTextU produce correct text. */
static __inline void AnyToUtf8(const char* src, char* dst, int dstCap)
{
    wchar_t wbuf[1024];
    int n;
    if (!src) { if (dstCap > 0) dst[0] = 0; return; }
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, wbuf,
                            (int)(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n > 0) {
        int srcLen = (int)strlen(src);
        if (dstCap > srcLen) {
            memcpy(dst, src, srcLen + 1);
        } else if (dstCap > 0) {
            memcpy(dst, src, dstCap - 1);
            dst[dstCap - 1] = 0;
        }
        return;
    }
    if (MultiByteToWideChar(CP_ACP, 0, src, -1, wbuf,
                            (int)(sizeof(wbuf) / sizeof(wbuf[0]))) > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, dst, dstCap, NULL, NULL);
    } else if (dstCap > 0) {
        dst[0] = 0;
    }
}

#endif /* _WIN32 */

#endif /* UTILS_UTF8_CONV_H */
