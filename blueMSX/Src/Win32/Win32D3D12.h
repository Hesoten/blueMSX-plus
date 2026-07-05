/*****************************************************************************
**
** Direct3D 12 video output (windowed / fullscreen, HDR, scanlines).
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
#ifndef WIN32_D3D12_H
#define WIN32_D3D12_H

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include "VideoRender.h"
#include "Properties.h"
#include "VDP.h"
#include "Win32Keyboard.h"

void D3D12ExitFullscreenMode();
int  D3D12EnterFullscreenMode(HWND hwnd, int useVideoBackBuffer, int useSysMemBuffering);
BOOL D3D12EnterWindowedMode(HWND hwnd, int width, int height, int useVideoBackBuffer, int useSysMemBuffering);
int  D3D12UpdateWindowedMode(HWND hwnd, int width, int height, int useVideoBackBuffer, int useSysMemBuffering);
int  D3D12UpdateSurface(HWND hWnd, Video* pVideo, int syncVblank, D3DProperties* d3dProperties);
/* Wipe textures + present black so the emu hwnd doesn't flash the previous
** run when SW_NORMAL re-exposes it.  No-op if the device isn't ready. */
void D3D12ClearToBlack(HWND hwnd);

/* Synchronously bring the device + swap chain up at the current window
** size; used by callers (recorder) that need to bind to the swap chain
** outside the regular UpdateSurface loop. */
int  D3D12EnsureReady(HWND hwnd, int syncVblank);

/* 1 when the swap chain was created in an HDR colour space. */
int  D3D12IsHdrActive(void);
/* 0 SDR, 1 HDR scRGB linear, 2 HDR10 PQ. */
int  D3D12HdrMode(void);
/* 1 when at least one DXGI output reports HDR ("Use HDR" in Windows). */
int  D3D12IsSystemHdrEnabled(void);

/* Capture path for video recording: Begin allocates, CaptureFromFrame
** writes bottom-up BGRA32 (or PQ-encoded BGRA32 in HDR mode) to dst,
** End releases.  Runs off the emu thread so the modal dialog doesn't
** stall it. */
/* hdr: 0 = BGRA8; 1 = R10G10B10A2_UNORM PQ.  Caller sets up the
** SinkWriter input type to match. */
int  D3D12RecordBegin(int width, int height, int hdr);
int  D3D12RecordCaptureFromFrame(FrameBuffer* fb, Video* pVideo,
                                 D3DProperties* props,
                                 void* dstBgra32, int dstPitch);
void D3D12RecordEnd(void);

/* HDR offline-preview swap chain bound to a child HWND in the modal
** render dialog; lifetime is the dialog's. */
int  D3D12HdrPreviewBegin(HWND hwnd, int width, int height);
int  D3D12HdrPreviewBlit(void);
void D3D12HdrPreviewEnd(void);

#ifdef __cplusplus
}
#endif
#endif
