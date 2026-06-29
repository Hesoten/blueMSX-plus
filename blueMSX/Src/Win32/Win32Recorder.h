/*****************************************************************************
**
** MP4 (H.264 / AAC) video + audio recorder.
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
/* Same lifecycle / Mixer-sink shape as the old Win32Avi.h so callers
** only need a name change. */
#ifndef WIN32_RECORDER_H
#define WIN32_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include "MsxTypes.h"
#include "AudioMixer.h"
#include "VideoRender.h"
#include "Properties.h"

/* Offline render path: replays the loaded .cap deterministically and writes
** an .mp4 (H.264 video + AAC audio) via Media Foundation IMFSinkWriter. */
void recorderStartRender(HWND hwnd, Properties* properties, Video* video);
void recorderStopRender(void);

/* Live-capture the swap-chain back buffer (post-shader, post-AR) and the
** Mixer master mix. overrideFilename=NULL auto-names under Video Capture;
** non-NULL uses the given path (Save-As). Returns 1 on success. */
int  recorderStartLive(HWND hwnd, Properties* properties, Video* video,
                       const char* overrideFilename);
void recorderStopLive(void);
int  recorderIsLiveRecording(void);

/* Call from the app's shutdown path (just before propDestroy / propSave).
** Restores syncMethod/speed snapshot if a render is still active so the
** stomped max-speed values don't leak into the INI. */
void recorderRestorePropsAtExit(void);

/* Mixer audio sink (replaces aviSound* in Win32snd.c). The Mixer write
** callback is hooked while a recording is active and PCM samples are pushed
** into the AAC encoder stream. */
typedef struct RecorderSound RecorderSound;
RecorderSound* recorderSoundCreate(HWND hwnd, Mixer* mixer,
                                   UInt32 sampleRate, UInt32 bufferSize,
                                   Int16 channels);
void recorderSoundDestroy(RecorderSound* sound);
void recorderSoundSuspend(RecorderSound* sound);
void recorderSoundResume(RecorderSound* sound);

#ifdef __cplusplus
}
#endif

#endif
