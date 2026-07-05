/*****************************************************************************
**
** Non-modal pop-up toast for capture / save notifications.
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
/* Non-modal fade-out notification helper: shows a short status blurb
** anchored to the emu window without blocking hotkey input. */
#ifndef WIN32_TOAST_H
#define WIN32_TOAST_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Show a text message in the non-activating topmost toast (fades out
** after ~4s, dismisses on click).
** Calling again replaces the message and restarts the timer.
**   ownerHwnd : top-level window that owns the toast for z-order /
**               lifetime. Required (NULL = no-op).
**   anchorHwnd: window whose screen rect is used to position the toast
**               (bottom-left). Pass the emu render hwnd to land the
**               toast over the active video output rather than the
**               surrounding theme/chrome. NULL = use ownerHwnd's rect. */
void toastShowMessage(HWND ownerHwnd, HWND anchorHwnd, const char* msg);

/* Convenience: format the localized "Saved: <path>" message and show it. */
void toastShowSaved(HWND ownerHwnd, HWND anchorHwnd, const char* savedPath);

/* Hide the toast (if any) immediately. Call before opening another modal
** dialog so the toast's WS_EX_TOPMOST overlay and 50ms timer do not race
** the dialog's modal message pump. Idempotent. */
void toastHide(void);

/* Tear down the cached toast window (call from app shutdown). */
void toastDestroy(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WIN32_TOAST_H */
