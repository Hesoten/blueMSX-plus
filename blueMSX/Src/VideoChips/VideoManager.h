/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/VideoChips/VideoManager.h,v $
**
** $Revision: 1.10 $
**
** $Date: 2008-03-30 18:38:47 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
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
#ifndef VIDEO_MANAGER_H
#define VIDEO_MANAGER_H

#include "MsxTypes.h"
#include "FrameBuffer.h"

typedef struct {
    void (*enable)(void*);
    void (*disable)(void*);
    /* Nonzero if the source presents the screen itself while it is shown. */
    int  pacesScreen;
    /* Nonzero if other sources cannot activate themselves while it is shown. */
    int  holdsScreen;
} VideoCallbacks;

typedef enum {
    VIDEO_INTERNAL = 1,
    VIDEO_MIX      = 2,
    VIDEO_EXTERNAL = 4,
    VIDEO_NONE     = 8,
    VIDEO_MASK_ALL = VIDEO_INTERNAL | VIDEO_MIX | VIDEO_EXTERNAL | VIDEO_NONE
} VideoMode;

int videoManagerGetCount();
int videoManagerGetActive();
void videoManagerSetActive(int index);
/* Shows the source unless another source holds a claim or the screen. */
void videoManagerActivate(int handle);
/* While claimed, videoManagerActivate ignores every other source. */
void videoManagerClaim(int handle, int claim);
/* Shows the machine's own video chip, which registers first. */
void videoManagerActivateDefault();
void videoManagerSetMode(int handle, VideoMode videoMode, VideoMode modeMask);
int videoManagerIsActive(int index);
char* videoManagerGetName(int index);
int videoManagerIsHandleActive(int handle);
void videoManagerSetRefreshRate(int handle, int rate);
int videoManagerGetActiveRefreshRate();
void videoManagerSetAspectRate(int handle, int rate);
int videoManagerGetActiveAspectRate();
int videoManagerActivePacesScreen();
void videoManagerSetPacesScreen(int handle, int pacesScreen);
/* While source handle is shown, source drawHandle (none if -1) stays
** enabled and the VDP drawing functions draw into its frame buffer. */
void videoManagerSetDrawSource(int handle, int drawHandle);
/* The handle of the source at index, or -1. */
int videoManagerGetHandle(int index);
FrameBufferData* videoManagerGetFrameBuffer(int handle);

void videoManagerReset();

void videoManagerLoadState();
void videoManagerApplyLoadedState();
void videoManagerSaveState();

int videoManagerRegister(const char* name, FrameBufferData* frameBuffer, 
                        VideoCallbacks* callbacks, void* ref);
void videoManagerUnregister(int handle);

#endif
