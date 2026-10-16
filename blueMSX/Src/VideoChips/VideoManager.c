/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/VideoChips/VideoManager.c,v $
**
** $Revision: 1.14 $
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
#include "VideoManager.h"
#include "ArchNotifications.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_DEVICES 64

typedef struct {
    int handle;
    VideoCallbacks callbacks;
    FrameBufferData* frameBufer;
    void* ref;
    VideoMode videoMode;
    VideoMode videoMask;
    /* Handle + 1 of the source that keeps drawing while this one is shown, or 0. */
    int   drawHandle;
    int   refreshRate;
    int   aspectRate;
    char  name[32];
} VideoInfo;

typedef struct {
    VideoInfo di[MAX_DEVICES];
    int count;
    int lastHandle;
} VideoManager;

static VideoManager videoManager;
static int loadedActiveIndex = -2;

/* Handle + 1 of the source that keeps other sources from activating
** themselves, or 0. */
static int claimHandle = 0;

/* Copies of the shown source's values, so that the timer thread can read them
** without touching a source that may be ejected meanwhile. */
static volatile int activeRefreshRate = 0;
static volatile int activeAspectRate = 0;
static volatile int activePacesScreen = 0;

void videoManagerReset() 
{
    videoManager.count = 0;
    videoManager.lastHandle = 0;
    loadedActiveIndex = -2;
    claimHandle = 0;
    activeRefreshRate = 0;
    activeAspectRate = 0;
    activePacesScreen = 0;
}

int videoManagerGetCount() 
{
    return videoManager.count;
}

int videoManagerIsActive(int index) 
{
    if (index >= videoManager.count) {
        return 0;
    }

    return videoManager.di[index].frameBufer == frameBufferGetActive();
}

int videoManagerGetActive()
{
    int index;
    for (index = 0; index < videoManager.count; index++) {
        if (videoManagerIsActive(index)) {
            return index;
        }
    }
    return -1;
}

static int videoManagerIndexOf(int handle)
{
    int index;
    for (index = 0; index < videoManager.count; index++) {
        if (videoManager.di[index].handle == handle + 1) {
            return index;
        }
    }
    return -1;
}

/* The source that draws while source index is shown: its draw source if
** that is registered, else itself. */
static int videoManagerDrawIndexOf(int index)
{
    int drawIndex;

    if (index < 0 || videoManager.di[index].drawHandle == 0) {
        return index;
    }
    drawIndex = videoManagerIndexOf(videoManager.di[index].drawHandle - 1);
    return drawIndex >= 0 ? drawIndex : index;
}

/* Nonzero while the source is enabled: shown or drawing for the shown one. */
static int videoManagerIsEnabled(int index)
{
    FrameBufferData* frameBuffer = videoManager.di[index].frameBufer;
    return frameBuffer != NULL &&
           (frameBuffer == frameBufferGetActive() || frameBuffer == frameBufferGetActiveDraw());
}

void videoManagerSetMode(int handle, VideoMode videoMode, VideoMode modeMask)
{
    int index = videoManagerIndexOf(handle);
    if (index < 0) {
        return;
    }
    videoManager.di[index].videoMode = videoMode;
    videoManager.di[index].videoMask = modeMask;
    if (index == videoManagerDrawIndexOf(videoManagerGetActive())) {
        frameBufferSetMixMode(videoManager.di[index].videoMode, videoManager.di[index].videoMask);
    }
}

static void updateActiveCache()
{
    int index = videoManagerGetActive();
    activeRefreshRate = index < 0 ? 0 : videoManager.di[index].refreshRate;
    activeAspectRate  = index < 0 ? 0 : videoManager.di[index].aspectRate;
    activePacesScreen = index < 0 ? 0 : videoManager.di[index].callbacks.pacesScreen;
}

void videoManagerSetActive(int index)
{
    int drawIndex;
    int i;

    if (videoManager.count <= 0) {
        frameBufferSetActive(NULL);
        updateActiveCache();
        return;
    }

    if (index >= videoManager.count) {
        return;
    }

    drawIndex = videoManagerDrawIndexOf(index);
    for (i = 0; i < videoManager.count; i++) {
        if (videoManagerIsEnabled(i) && i != index && i != drawIndex &&
            videoManager.di[i].callbacks.disable != NULL)
        {
            videoManager.di[i].callbacks.disable(videoManager.di[i].ref);
        }
    }

    if (index < 0) {
        frameBufferSetActive(NULL);
    }
    else {
        frameBufferSetActiveDraw(videoManager.di[index].frameBufer,
                                 videoManager.di[drawIndex].frameBufer);
        frameBufferSetMixMode(videoManager.di[drawIndex].videoMode, videoManager.di[drawIndex].videoMask);
        if (videoManager.di[index].callbacks.enable != NULL) {
            videoManager.di[index].callbacks.enable(videoManager.di[index].ref);
        }
        if (drawIndex != index && videoManager.di[drawIndex].callbacks.enable != NULL) {
            videoManager.di[drawIndex].callbacks.enable(videoManager.di[drawIndex].ref);
        }
    }
    updateActiveCache();
}

void videoManagerActivate(int handle)
{
    int index = videoManagerIndexOf(handle);
    int active = videoManagerGetActive();
    if (claimHandle != 0 && claimHandle != handle + 1) {
        return;
    }
    /* The claimant switches even from a source picked by hand. */
    if (claimHandle != handle + 1 && active >= 0 && active != index &&
        videoManager.di[active].callbacks.holdsScreen)
    {
        return;
    }
    if (index >= 0) {
        videoManagerSetActive(index);
    }
}

void videoManagerClaim(int handle, int claim)
{
    if (claim) {
        claimHandle = handle + 1;
    }
    else if (claimHandle == handle + 1) {
        claimHandle = 0;
    }
}

void videoManagerActivateDefault()
{
    videoManagerSetActive(0);
}

char* videoManagerGetName(int index)
{
    if (index >= videoManager.count) {
        return index == 0 ? "Default" : NULL;
    }
    return videoManager.di[index].name;
}

int videoManagerIsHandleActive(int handle)
{
    int index = videoManagerIndexOf(handle);
    return index >= 0 && videoManagerIsActive(index);
}

/* rate is frames per second, or 0 to leave the rate to the VDP. */
void videoManagerSetRefreshRate(int handle, int rate)
{
    int index = videoManagerIndexOf(handle);
    if (index < 0) {
        return;
    }
    videoManager.di[index].refreshRate = rate;
    if (videoManagerIsActive(index)) {
        activeRefreshRate = rate;
    }
}

int videoManagerGetActiveRefreshRate()
{
    return activeRefreshRate;
}

/* rate is the TV rate whose aspect the picture has, or 0 for its refresh rate. */
void videoManagerSetAspectRate(int handle, int rate)
{
    int index = videoManagerIndexOf(handle);
    if (index < 0) {
        return;
    }
    videoManager.di[index].aspectRate = rate;
    if (videoManagerIsActive(index)) {
        activeAspectRate = rate;
    }
}

int videoManagerGetActiveAspectRate()
{
    int rate = activeAspectRate;
    return rate != 0 ? rate : activeRefreshRate;
}

int videoManagerActivePacesScreen()
{
    return activePacesScreen;
}

void videoManagerSetPacesScreen(int handle, int pacesScreen)
{
    int index = videoManagerIndexOf(handle);
    if (index < 0) {
        return;
    }
    videoManager.di[index].callbacks.pacesScreen = pacesScreen;
    if (videoManagerIsActive(index)) {
        activePacesScreen = pacesScreen;
    }
}

void videoManagerSetDrawSource(int handle, int drawHandle)
{
    int index = videoManagerIndexOf(handle);
    if (index < 0) {
        return;
    }
    videoManager.di[index].drawHandle = drawHandle < 0 ? 0 : drawHandle + 1;
    if (videoManagerIsActive(index)) {
        videoManagerSetActive(index);
    }
}

int videoManagerGetHandle(int index)
{
    if (index < 0 || index >= videoManager.count) {
        return -1;
    }
    return videoManager.di[index].handle - 1;
}

FrameBufferData* videoManagerGetFrameBuffer(int handle)
{
    int index = videoManagerIndexOf(handle);
    return index < 0 ? NULL : videoManager.di[index].frameBufer;
}

int videoManagerRegister(const char* name, FrameBufferData* frameBuffer, 
                         VideoCallbacks* callbacks, void* ref)
{
    if (videoManager.count >= MAX_DEVICES) {
        return 0;
    }

    videoManager.di[videoManager.count].handle      = ++videoManager.lastHandle;
    videoManager.di[videoManager.count].frameBufer  = frameBuffer;
    videoManager.di[videoManager.count].callbacks   = *callbacks;
    videoManager.di[videoManager.count].ref         = ref;
    videoManager.di[videoManager.count].videoMode   = VIDEO_INTERNAL;
    videoManager.di[videoManager.count].videoMask   = VIDEO_INTERNAL;
    videoManager.di[videoManager.count].drawHandle  = 0;
    videoManager.di[videoManager.count].refreshRate = 0;
    videoManager.di[videoManager.count].aspectRate = 0;

    strcpy(videoManager.di[videoManager.count].name, name);

    videoManager.count++;

    if (videoManager.count == 1) {
        videoManagerSetActive(0);
    }
    
    archVideoOutputChange();

    return videoManager.lastHandle - 1;
}

void videoManagerUnregister(int handle)
{
    int isActive;
    int isEnabled;
    int i = videoManagerIndexOf(handle);

    if (i < 0) {
        return;
    }

    isActive = videoManagerIsActive(i);
    isEnabled = videoManagerIsEnabled(i);
    videoManagerClaim(handle, 0);

    videoManager.count--;
    while (i < videoManager.count) {
        videoManager.di[i] = videoManager.di[i + 1];
        i++;
    }

    if (isActive || videoManager.count == 0) {
        videoManagerSetActive(0);
    }
    else if (isEnabled) {
        /* It drew for the shown source, which now draws for itself. */
        videoManagerSetActive(videoManagerGetActive());
    }

    if (videoManager.count == 0) {
        frameBufferClearDeinterlace();
    }

    archVideoOutputChange();
}


// Runs before the machine's video chips exist; the index is applied
// once they have registered, in videoManagerApplyLoadedState().
void videoManagerLoadState()
{
    SaveState* state = saveStateOpenForRead("VideoManager");
    loadedActiveIndex = saveStateGet(state, "ActiveFrameBuffer",  0);
    saveStateClose(state);
}

void videoManagerApplyLoadedState()
{
    if (loadedActiveIndex != -2) {
        videoManagerSetActive(loadedActiveIndex);
        loadedActiveIndex = -2;
    }
}

void videoManagerSaveState()
{
    SaveState* state = saveStateOpenForWrite("VideoManager");
    saveStateSet(state, "ActiveFrameBuffer",  videoManagerGetActive());
    saveStateClose(state);
}
