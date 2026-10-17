/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/VideoChips/FrameBuffer.c,v $
**
** $Revision: 1.32 $
**
** $Date: 2009-07-18 14:35:59 $
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
#include "FrameBuffer.h"
#include "ArchEvent.h"
#include "ArchVideoIn.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>


//int xxxx = 0;

#ifdef WII
#define MAX_FRAMES_PER_FRAMEBUFFER 3
#else
#define MAX_FRAMES_PER_FRAMEBUFFER 4
#endif

struct FrameBufferData {
    int viewFrame;
    int drawFrame;
    int currentAge;
#ifndef WII
    int currentBlendFrame;
#endif
    FrameBuffer frame[MAX_FRAMES_PER_FRAMEBUFFER];
#ifndef WII
    FrameBuffer blendFrame[2];
#endif
    int noBlend;
    FrameBufferFlipHook flipHook;
    void* flipRef;
};

#ifdef WII
#define FB_THREAD_LOCAL
#elif defined(_MSC_VER)
#define FB_THREAD_LOCAL __declspec(thread)
#else
#define FB_THREAD_LOCAL __thread
#endif

static int curScanline = 0;
static void* semaphore = NULL;
/* Per thread: the UI thread and the recorder on the emu thread both render. */
static FB_THREAD_LOCAL FrameBuffer* deintBuffer = NULL;
static FB_THREAD_LOCAL FrameBuffer* narrowBuffer = NULL;
static FB_THREAD_LOCAL FrameBuffer* fitBuffer = NULL;

/* Samples in line y, at most what a line buffer holds. */
static int lineWidth(FrameBuffer* frameBuffer, int y)
{
    int width = frameBuffer->maxWidth << frameBuffer->line[y].doubleWidth;
    return width < FB_MAX_LINE_WIDTH ? width : FB_MAX_LINE_WIDTH;
}

/* Halves a four-times-wide line in place; a mixed sample keeps the key bit
** only where both samples have it. */
static void narrowLine(FrameBuffer* frameBuffer, int y)
{
    LineBuffer* line = &frameBuffer->line[y];
    int width = lineWidth(frameBuffer, y) / 2;
    int x;

    for (x = 0; x < width; x++) {
        UInt16 a = line->buffer[2 * x];
        UInt16 b = line->buffer[2 * x + 1];
        line->buffer[x] = (UInt16)((a & b & 0x8000) | (((a & b) + (((a ^ b) & 0x7bde) >> 1)) & 0x7fff));
    }
    line->doubleWidth = 1;
}
#ifndef WII
static int confBlendFrames = 0;
#endif

static FrameBuffer* mixFrame(FrameBuffer* d, FrameBuffer* a, FrameBuffer* b, int pct);
static FrameBuffer* mixFrameInterlace(FrameBuffer* d, FrameBuffer* a, FrameBuffer* b, int pct);
static void frameBufferSuperimpose(FrameBuffer* a);
static void frameBufferExternal(FrameBuffer* a);
static void frameBufferBlack(FrameBuffer* a);
extern int getScreenCompletePercent();

static void waitSem() {
    if (semaphore == NULL) {
        semaphore = archSemaphoreCreate(1);
    }
    archSemaphoreWait(semaphore, -1);
}

static void signalSem() {
    archSemaphoreSignal(semaphore);
}



static FrameBufferData* currentBuffer = NULL;
/* The buffer the VDP draws into: currentBuffer unless a source is shown
** while the VDP keeps drawing into its own buffer. */
static FrameBufferData* drawBuffer = NULL;
static FrameBufferMixMode mixMode = MIXMODE_INTERNAL;
static FrameBufferMixMode mixMask = MIXMODE_INTERNAL;
static int frameBufferCount = MAX_FRAMES_PER_FRAMEBUFFER;


static FrameBuffer* frameBufferFlipViewFrame1(int mixFrames)
{
    if (currentBuffer == NULL) {
        return NULL;
    }
    currentBuffer->frame->age = ++currentBuffer->currentAge;
    return currentBuffer->frame;
}

static FrameBuffer* frameBufferFlipViewFrame2(int mixFrames)
{
    int index;

    if (currentBuffer == NULL) {
        return NULL;
    }
    waitSem();
    index = currentBuffer->viewFrame == 1 ? 0 : 1;
    if (currentBuffer->frame[index].age > currentBuffer->frame[currentBuffer->viewFrame].age) {
        currentBuffer->viewFrame = index;
    }
    signalSem();
    return currentBuffer->frame + currentBuffer->viewFrame;
}

static FrameBuffer* frameBufferFlipViewFrame3(int mixFrames)
{
    int index;

    if (currentBuffer == NULL) {
        return NULL;
    }
    waitSem();
    switch (currentBuffer->viewFrame) {
    case 0: index = currentBuffer->drawFrame == 1 ? 2 : 1; break;
    case 1: index = currentBuffer->drawFrame == 2 ? 0 : 2; break;
    case 2: index = currentBuffer->drawFrame == 0 ? 1 : 0; break;
    }
    if (currentBuffer->frame[index].age > currentBuffer->frame[currentBuffer->viewFrame].age) {
        currentBuffer->viewFrame = index;
    }
    signalSem();
    return currentBuffer->frame + currentBuffer->viewFrame;
}

static FrameBuffer* frameBufferFlipViewFrame4(int mixFrames)
{
    int i;

    if (currentBuffer == NULL) {
        return NULL;
    }
    waitSem();
    for (i = 0; i < 4; i++) {
        if (i == currentBuffer->drawFrame) continue;
        if (currentBuffer->frame[i].age > currentBuffer->frame[currentBuffer->viewFrame].age) {
            currentBuffer->viewFrame = i;
        }
    }

    if (mixFrames) {
        int secondFrame = 0;
        int viewAge = 0;
        for (i = 0; i < 4; i++) {
            if (i == currentBuffer->drawFrame) continue;
            if (i == currentBuffer->viewFrame) continue;
            if (currentBuffer->frame[i].age > viewAge) {
                viewAge = currentBuffer->frame[i].age;
                secondFrame = i;
            }
        }
        signalSem();

        return mixFrame(NULL, currentBuffer->frame + currentBuffer->viewFrame, currentBuffer->frame + secondFrame, getScreenCompletePercent());
    }

    signalSem();
    return currentBuffer->frame + currentBuffer->viewFrame;
}


// Frame indices are only kept in range for the active buffer.
static void resetStaleIndices(FrameBufferData* fb)
{
    if (fb->viewFrame >= frameBufferCount || fb->drawFrame >= frameBufferCount) {
        int i;
        fb->viewFrame = 0;
        fb->drawFrame = frameBufferCount > 1 ? 1 : 0;
        for (i = 0; i < MAX_FRAMES_PER_FRAMEBUFFER; i++) {
            fb->frame[i].age = 0;
        }
    }
}

static FrameBuffer* frameBufferFlipDrawFrame1(FrameBufferData* fb)
{
    if (fb == NULL) {
        return NULL;
    }
    return fb->frame;
}

static FrameBuffer* frameBufferFlipDrawFrame2(FrameBufferData* fb)
{
    FrameBuffer* frame;

    if (fb == NULL) {
        return NULL;
    }
    waitSem();
    fb->drawFrame = fb->viewFrame;
    frame = fb->frame + fb->drawFrame;
    frame->age = ++fb->currentAge;
    signalSem();
    return frame;
}

static FrameBuffer* frameBufferFlipDrawFrame3(FrameBufferData* fb)
{
    FrameBuffer* frame;

    if (fb == NULL) {
        return NULL;
    }
    waitSem();
    switch (fb->drawFrame) {
    case 0: fb->drawFrame = fb->viewFrame == 1 ? 2 : 1; break;
    case 1: fb->drawFrame = fb->viewFrame == 2 ? 0 : 2; break;
    case 2: fb->drawFrame = fb->viewFrame == 0 ? 1 : 0; break;
    }
    frame = fb->frame + fb->drawFrame;
    frame->age = ++fb->currentAge;
    signalSem();
    return frame;
}


static FrameBuffer* frameBufferFlipDrawFrame4(FrameBufferData* fb)
{
    FrameBuffer* frame;
    int drawFrame = fb->drawFrame;
    int drawAge = 0x7fffffff;
    int i;

    if (fb == NULL) {
        return NULL;
    }

    waitSem();

    for (i = 0; i < 4; i++) {
        if (i == fb->viewFrame) continue;
        if (fb->frame[i].age < drawAge) {
            drawAge = fb->frame[i].age;
            fb->drawFrame = i;
        }
    }

    signalSem();
    frame = fb->frame + fb->drawFrame;
    frame->age = ++fb->currentAge;
    return frame;
}



FrameBuffer* frameBufferGetViewFrame()
{
    return currentBuffer ? currentBuffer->frame + currentBuffer->viewFrame : NULL;
}

void frameBufferSetScanline(int scanline)
{
    curScanline = scanline;
}

int frameBufferGetScanline()
{
    return curScanline;
}

FrameBuffer* frameBufferGetDrawFrameOf(FrameBufferData* fb)
{
    FrameBuffer* frameBuffer;

    if (fb == NULL) {
        return NULL;
    }
#ifdef WII

    frameBuffer = fb->frame + fb->drawFrame;
#else
    if (confBlendFrames && !fb->noBlend) {
        frameBuffer = fb->blendFrame + fb->currentBlendFrame;
    }
    else {
        frameBuffer = fb->frame + fb->drawFrame;
    }
#endif

    return frameBuffer;
}

FrameBuffer* frameBufferGetDrawFrame()
{
    return frameBufferGetDrawFrameOf(drawBuffer);
}

void frameBufferSetFrameCount(int frameCount)
{
    waitSem();
    frameBufferCount = frameCount;
    if (currentBuffer != NULL) {
        int i;
        currentBuffer->viewFrame = 0;
        currentBuffer->drawFrame = 0;

        for (i = 0; i < MAX_FRAMES_PER_FRAMEBUFFER; i++) {
            currentBuffer->frame[i].age = 0;
        }
    }
    if (drawBuffer != NULL && drawBuffer != currentBuffer) {
        int i;
        drawBuffer->viewFrame = 0;
        drawBuffer->drawFrame = 0;

        for (i = 0; i < MAX_FRAMES_PER_FRAMEBUFFER; i++) {
            drawBuffer->frame[i].age = 0;
        }
    }
    signalSem();
}

FrameBuffer* frameBufferFlipViewFrame(int mixFrames)
{
    FrameBuffer* frameBuffer;

    if (currentBuffer == NULL) {
        return NULL;
    }

    switch (frameBufferCount) {
    case 2:
        frameBuffer = frameBufferFlipViewFrame2(mixFrames);
        break;
    case 3:
        frameBuffer = frameBufferFlipViewFrame3(mixFrames);
        break;
    case 4:
        frameBuffer = frameBufferFlipViewFrame4(mixFrames);
        break;
    default:
        frameBuffer = frameBufferFlipViewFrame1(mixFrames);
        break;
    }
    return frameBuffer;
}

FrameBuffer* frameBufferFlipDrawFrameOf(FrameBufferData* fb)
{
    FrameBuffer* frameBuffer;
    FrameBuffer* completed;
    FrameBuffer* drawn;
    int drawFrame;
    int blend;

#ifdef WII
    archThreadSleep(0); // wait one frame
#endif
    if (fb == NULL) {
        return NULL;
    }
    drawFrame = fb->drawFrame;
    if (fb != currentBuffer) {
        waitSem();
        resetStaleIndices(fb);
        signalSem();
    }
#ifdef WII
    blend = 0;
#else
    blend = confBlendFrames && !fb->noBlend;
    if (blend) {
        FrameBuffer* a = &fb->blendFrame[0];
        FrameBuffer* b = &fb->blendFrame[1];
        /* Frames of different widths do not line up: the newest is shown alone. */
        if (a->maxWidth != b->maxWidth) {
            a = b = fb->blendFrame + fb->currentBlendFrame;
        }
        mixFrame(fb->frame + fb->drawFrame, a, b, 50);
    }
#endif
    if (fb == drawBuffer) {
        curScanline = 0;

        if (mixMode == MIXMODE_EXTERNAL) {
            frameBufferExternal(fb->frame + fb->drawFrame);
        }
        else if (mixMode == MIXMODE_BOTH) {
            frameBufferSuperimpose(fb->frame + fb->drawFrame);
        }
        else if (mixMode == MIXMODE_NONE) {
            frameBufferBlack(fb->frame + fb->drawFrame);
        }
    }

//    ++xxxx;
    //printf("%d\n", xxxx);
//    confBlendFrames = xxxx < 2100 || (xxxx >= 7900 && xxxx <= 9400);

    completed = fb->frame + fb->drawFrame;
    drawn = completed;
#ifndef WII
    if (blend) {
        drawn = fb->blendFrame + fb->currentBlendFrame;
    }
#endif
    switch (frameBufferCount) {
    case 2:
        frameBuffer = frameBufferFlipDrawFrame2(fb);
        break;
    case 3:
        frameBuffer = frameBufferFlipDrawFrame3(fb);
        break;
    case 4:
        frameBuffer = frameBufferFlipDrawFrame4(fb);
        break;
    default:
        frameBuffer = frameBufferFlipDrawFrame1(fb);
        break;
    }
#ifndef WII
    if (blend) {
        fb->currentBlendFrame ^= 1;
        frameBuffer = fb->blendFrame + fb->currentBlendFrame;
    }
#endif
    /* A buffer neither shown nor drawing gets its indices reset here after a
    ** frame count change: the frame completed is then not the one drawn. */
    if (fb->flipHook != NULL && completed == fb->frame + drawFrame) {
        fb->flipHook(fb->flipRef, completed, drawn);
    }
    return frameBuffer;
}

FrameBuffer* frameBufferFlipDrawFrame()
{
    return frameBufferFlipDrawFrameOf(drawBuffer);
}

/* The newest frame drawn to its end, or NULL before there is one. With one
** or two frames that is the frame being drawn, or the last blended one. */
FrameBuffer* frameBufferGetLastCompletedOf(FrameBufferData* fb)
{
    int best = -1;
    int i;

    if (fb == NULL) {
        return NULL;
    }
    if (frameBufferCount <= 2) {
        return fb->frame + fb->drawFrame;
    }
    for (i = 0; i < frameBufferCount && i < MAX_FRAMES_PER_FRAMEBUFFER; i++) {
        if (i != fb->drawFrame && fb->frame[i].age > 0 &&
            (best < 0 || fb->frame[i].age > fb->frame[best].age))
        {
            best = i;
        }
    }
    return best < 0 ? NULL : fb->frame + best;
}

void frameBufferSetFlipHook(FrameBufferData* fb, FrameBufferFlipHook hook, void* ref)
{
    assert(hook == NULL || fb->flipHook == NULL);
    fb->flipHook = hook;
    fb->flipRef  = ref;
}

void frameBufferDataSetNoBlend(FrameBufferData* fb, int noBlend)
{
    fb->noBlend = noBlend;
}

void frameBufferSetBlendFrames(int blendFrames)
{
#ifdef WII
    (void)blendFrames;
#else
    confBlendFrames = blendFrames;
#endif
}

FrameBufferData* frameBufferDataCreate(int maxWidth, int maxHeight, int defaultHorizZoom)
{
    int i;
    FrameBufferData* frameData = calloc(1, sizeof(FrameBufferData));
    frameData->drawFrame = frameBufferCount > 1 ? 1 : 0;

    for (i = 0; i < MAX_FRAMES_PER_FRAMEBUFFER; i++) {
        int j;

        frameData->frame[i].maxWidth = maxWidth;
        frameData->frame[i].lines = maxHeight;
        for (j = 0; j < FB_MAX_LINES; j++) {
            frameData->frame[i].line[j].doubleWidth = defaultHorizZoom - 1;
        }
    }
#ifndef WII
    for (i = 0; i < 2; i++) {
        int j;

        frameData->blendFrame[i].maxWidth = maxWidth;
        frameData->blendFrame[i].lines = maxHeight;
        for (j = 0; j < FB_MAX_LINES; j++) {
            frameData->blendFrame[i].line[j].doubleWidth = defaultHorizZoom - 1;
        }
    }
#endif
    return frameData;
}

void frameBufferDataDestroy(FrameBufferData* frameData)
{
    if (frameData == NULL) {
        return;
    }
    if (frameData->flipHook != NULL) {
        frameData->flipHook(frameData->flipRef, NULL, NULL);
    }
    waitSem();
    if (frameData == currentBuffer) {
        currentBuffer = NULL;
    }
    if (frameData == drawBuffer) {
        drawBuffer = currentBuffer;
    }
    signalSem();
    free(frameData);
}


void frameBufferSetActive(FrameBufferData* frameData)
{
    frameBufferSetActiveDraw(frameData, frameData);
}

void frameBufferSetActiveDraw(FrameBufferData* frameData, FrameBufferData* drawData)
{
    waitSem();
    if (frameData != NULL) {
        resetStaleIndices(frameData);
    }
    if (drawData != NULL) {
        resetStaleIndices(drawData);
    }
    currentBuffer = frameData;
    drawBuffer    = drawData;
    if (frameData == NULL) {
        mixMode = MIXMODE_INTERNAL;
    }
    signalSem();
}

void frameBufferSetMixMode(FrameBufferMixMode mode,  FrameBufferMixMode mask)
{
    mixMode = mode;
    mixMask = mask;
}

FrameBufferData* frameBufferGetActive()
{
    return currentBuffer;
}

FrameBufferData* frameBufferGetActiveDraw()
{
    return drawBuffer;
}

FrameBuffer* frameBufferGetWhiteNoiseFrame()
{
    static FrameBuffer* frameBuffer = NULL;
    UInt16 colors[32];
    static UInt32 r = 13;
    int y;

    for (y = 0; y < 32; y++) {
        colors[y] = videoGetColor(y << 3, y << 3, y << 3);
    }

    if (frameBuffer == NULL) {
        frameBuffer = calloc(1, sizeof(FrameBuffer));
        frameBuffer->lines = 240;
        frameBuffer->maxWidth = 320;
    }

    for (y = 0; y < 240; y++) {
        int x;
        UInt16* buffer = frameBuffer->line[y].buffer;
        frameBuffer->line[y].doubleWidth = 0;
        for (x = 0; x < 320; x++) {
            buffer[x] = colors[r >> 27];
            r *= 7;
        }
    }

    return frameBuffer;
}

/* Frees the calling thread's deinterlace and narrowing buffers. */
void frameBufferClearDeinterlace()
{
    if (deintBuffer != NULL) {
        void* buf = deintBuffer;
        deintBuffer = NULL;
        free(buf);
    }
    if (narrowBuffer != NULL) {
        void* buf = narrowBuffer;
        narrowBuffer = NULL;
        free(buf);
    }
    if (fitBuffer != NULL) {
        void* buf = fitBuffer;
        fitBuffer = NULL;
        free(buf);
    }
}

/* Returns frameBuffer with its four-times-wide lines halved (each ARGB1555
** channel the floor average of a pixel pair), or frameBuffer itself if it
** has none. For renderers that handle at most twice the logical width. */
FrameBuffer* frameBufferNarrow(FrameBuffer* frameBuffer)
{
    int y;
    int x;

    for (y = 0; y < frameBuffer->lines; y++) {
        if (frameBuffer->line[y].doubleWidth > 1) {
            break;
        }
    }
    if (y == frameBuffer->lines) {
        return frameBuffer;
    }
    if (narrowBuffer == NULL) {
        narrowBuffer = calloc(1, sizeof(FrameBuffer));
        if (narrowBuffer == NULL) {
            return frameBuffer;
        }
    }

    narrowBuffer->age             = frameBuffer->age;
    narrowBuffer->interlace       = frameBuffer->interlace;
    narrowBuffer->interlaceRaster = frameBuffer->interlaceRaster;
    narrowBuffer->maxWidth        = frameBuffer->maxWidth;
    narrowBuffer->lines           = frameBuffer->lines;

    for (y = 0; y < frameBuffer->lines; y++) {
        LineBuffer* src = &frameBuffer->line[y];
        LineBuffer* dst = &narrowBuffer->line[y];
        int width = lineWidth(frameBuffer, y) / 2;

        if (src->doubleWidth <= 1) {
            dst->doubleWidth = src->doubleWidth;
            memcpy(dst->buffer, src->buffer, lineWidth(frameBuffer, y) * sizeof(UInt16));
            continue;
        }
        dst->doubleWidth = 1;
        for (x = 0; x < width; x++) {
            UInt16 a = src->buffer[2 * x];
            UInt16 b = src->buffer[2 * x + 1];
            dst->buffer[x] = (UInt16)(((a & b) + (((a ^ b) & 0x7bde) >> 1)) & 0x7fff);
        }
    }
    return narrowBuffer;
}

/* Returns a progressive frame of between 240 and 480 lines stretched to 480
** lines by repeating lines, or frameBuffer itself if it has another count. */
FrameBuffer* frameBufferFitLines(FrameBuffer* frameBuffer)
{
    int y;

    if (frameBuffer->lines <= FB_MAX_LINES / 2 || frameBuffer->lines >= FB_MAX_LINES ||
        frameBuffer->interlace != INTERLACE_NONE) {
        return frameBuffer;
    }
    if (fitBuffer == NULL) {
        fitBuffer = calloc(1, sizeof(FrameBuffer));
        if (fitBuffer == NULL) {
            return frameBuffer;
        }
    }

    fitBuffer->age             = frameBuffer->age;
    fitBuffer->interlace       = INTERLACE_NONE;
    fitBuffer->interlaceRaster = frameBuffer->interlaceRaster;
    fitBuffer->maxWidth        = frameBuffer->maxWidth;
    fitBuffer->lines           = FB_MAX_LINES;

    for (y = 0; y < FB_MAX_LINES; y++) {
        int src = y * frameBuffer->lines / FB_MAX_LINES;
        fitBuffer->line[y].doubleWidth = frameBuffer->line[src].doubleWidth;
        memcpy(fitBuffer->line[y].buffer, frameBuffer->line[src].buffer,
               lineWidth(frameBuffer, src) * sizeof(UInt16));
    }
    return fitBuffer;
}

FrameBuffer* frameBufferDeinterlace(FrameBuffer* frameBuffer)
{
    int y;

    if (deintBuffer == NULL) {
        deintBuffer = calloc(1, sizeof(FrameBuffer));
    }
    deintBuffer->lines = frameBuffer->lines < FB_MAX_LINES / 2 ? 2 * frameBuffer->lines : FB_MAX_LINES;
    deintBuffer->maxWidth = frameBuffer->maxWidth;

    for (y = frameBuffer->interlace == INTERLACE_ODD ? 1 : 0; y < FB_MAX_LINES; y += 2) {
        deintBuffer->line[y] = frameBuffer->line[y / 2];
    }
    return deintBuffer;
}


#define M1 0x3E07C1F
#define M2 0x3E0F81F

static FrameBuffer* mixFrame(FrameBuffer* d, FrameBuffer* a, FrameBuffer* b, int pct)
{
    static FrameBuffer* dst = NULL;
    int p = 0x20 * pct / 100;
    int n = 0x20 - p;
    int x;
    int y;

    if (d == NULL) {
        if (dst == NULL) {
            dst = (FrameBuffer*)calloc(1, sizeof(FrameBuffer));
        }
        d = dst;
    }

    /* Frames of different widths do not line up: a is shown alone. */
    if (a->maxWidth != b->maxWidth) {
        b = a;
    }

    if (a->interlace != INTERLACE_NONE) {
        return mixFrameInterlace(d, a, b, pct);
    }

    if (p == 0x20) {
        return a;
    }
    if (n == 0x20) {
        return b;
    }

    d->lines           = a->lines;
    d->interlace       = a->interlace;
    d->interlaceRaster = a->interlaceRaster;
    d->maxWidth        = a->maxWidth;

    for (y = 0; y < a->lines; y++) {
        int width = lineWidth(a, y) / 2;
        UInt32* ap = (UInt32*)a->line[y].buffer;
        UInt32* bp = (UInt32*)b->line[y].buffer;
        UInt32* dp = (UInt32*)d->line[y].buffer;

        d->line[y].doubleWidth = a->line[y].doubleWidth;
        for (x = 0; x < width; x ++) {
#ifdef WII
            UInt32 av = ((ap[x] >> 1) & 0xffe0ffe0) | (ap[x] & 0x001f001f);
            UInt32 bv = ((bp[x] >> 1) & 0xffe0ffe0) | (bp[x] & 0x001f001f);
            UInt32 dd = ((((av & M1) * p + (bv & M1) * n) >> 5) & M1) |
                    ((((((av >> 5) & M2) * p + ((bv >> 5) & M2) * n) >> 5) & M2) << 5);
            dp[x] = ((dd << 1) & 0xffc0ffc0) | (dd & 0x001f001f);
#else
            UInt32 av = ap[x];
            UInt32 bv = bp[x];
            dp[x] = ((((av & M1) * p + (bv & M1) * n) >> 5) & M1) |
                    ((((((av >> 5) & M2) * p + ((bv >> 5) & M2) * n) >> 5) & M2) << 5) |
                    (av & 0x80008000);
#endif
        }
    }

    return d;
}


static FrameBuffer* mixFrameInterlace(FrameBuffer* d, FrameBuffer* a, FrameBuffer* b, int pct)
{
    static FrameBuffer* dst = NULL;
    int p = 0x20 * pct / 100;
    int n = 0x20 - p;
    int x;
    int y;

    if (d == NULL) {
        if (dst == NULL) {
            dst = (FrameBuffer*)calloc(1, sizeof(FrameBuffer));
        }
        d = dst;
    }

    d->lines           = a->lines * 2;
    d->interlace       = INTERLACE_NONE;
    d->interlaceRaster = a->interlaceRaster;
    d->maxWidth        = a->maxWidth;

    if (p == 0x20) { p = 0x1f; n = 1; }
    if (n == 0x20) { n = 0x1f; p = 1; }

    if (a->interlace == INTERLACE_ODD) {
        FrameBuffer* t;
        int t2;

        t = a;
        a = b;
        b = t;

        t2 = n;
        n = p;
        p = t2;
    }

    for (y = 0; y < a->lines * 2; y++) {
        int     width = lineWidth(a, y / 2) / 2;
        UInt32* ap;
        UInt32* bp;
        UInt32* dp;

        if (y & 1) {
            ap = (UInt32*)a->line[y / 2].buffer;
            bp = (UInt32*)b->line[y / 2].buffer;
        }
        else {
            ap = (UInt32*)a->line[y / 2].buffer;
            if (y == 0) {
                bp = (UInt32*)b->line[a->lines - 1].buffer;
            }
            else {
                bp = (UInt32*)b->line[y / 2 - 1].buffer;
            }
        }
        dp = (UInt32*)d->line[y].buffer;
        d->line[y].doubleWidth = a->line[y / 2].doubleWidth;

        for (x = 0; x < width; x++) {
#ifdef WII
            UInt32 av = ((ap[x] >> 1) & 0xffe0ffe0) | (ap[x] & 0x001f001f);
            UInt32 bv = ((bp[x] >> 1) & 0xffe0ffe0) | (bp[x] & 0x001f001f);
            UInt32 dd = ((((av & M1) * p + (bv & M1) * n) >> 5) & M1) |
                    ((((((av >> 5) & M2) * p + ((bv >> 5) & M2) * n) >> 5) & M2) << 5);
            dp[x] = ((dd << 1) & 0xffc0ffc0) | (dd & 0x001f001f);
#else
            UInt32 av = ap[x];
            UInt32 bv = bp[x];
            dp[x] = ((((av & M1) * p + (bv & M1) * n) >> 5) & M1) |
                    ((((((av >> 5) & M2) * p + ((bv >> 5) & M2) * n) >> 5) & M2) << 5) |
                    (av & 0x80008000);
#endif
        }
    }


    return d;
}

static UInt16* getBlackImage()
{
    static UInt16* blackImage = NULL;

    if (blackImage == NULL) {
        blackImage = calloc(sizeof(UInt16), FB_MAX_LINE_WIDTH * FB_MAX_LINES);
    }
    return blackImage;
}

static void frameBufferBlack(FrameBuffer* a)
{
    UInt16* pImage = getBlackImage();
    int y;

    for (y = 0; y < a->lines; y++) {
        memcpy(a->line[y].buffer, pImage, a->maxWidth * sizeof(UInt16));
        a->line[y].doubleWidth = 0;
    }
}

static void frameBufferExternal(FrameBuffer* a)
{
    int y;
    UInt16* pImage = NULL;
    int scaleHeight = 0;
    int scaleWidth = 0;
    int imageHeight = a->lines;
    int imageWidth  = a->maxWidth;

    if (a->maxWidth <= 320) {
        scaleWidth = 1;
        imageWidth *= 2;
    }

    if (a->lines <= FB_MAX_LINES / 4) {
        scaleHeight = 1;
        imageHeight *= 2;
    }
    else if (a->lines <= FB_MAX_LINES / 2) {
        if (a->interlace == INTERLACE_NONE) {
            scaleHeight = 1;
        }
        imageHeight *= 2;
    }

    if (mixMode & mixMask) {
        pImage = archVideoInBufferGet(imageWidth, imageHeight);
    }
    if (pImage == NULL) {
        pImage = getBlackImage();
    }

    if (scaleHeight) {
        a->lines *= 2;

        for (y = 0; y < a->lines; y++) {
            memcpy(a->line[y].buffer, pImage + y * imageWidth, imageWidth * sizeof(UInt16));
            if (scaleWidth) {
                a->line[y].doubleWidth = 1;
            }
        }
    }
}

static void frameBufferSuperimpose(FrameBuffer* a)
{
    int x, y;
    UInt16* pImage = NULL;
    int scaleHeight = 0;
    int scaleWidth = 0;
    int imageHeight = a->lines;
    int imageWidth  = a->maxWidth;

    if (a->maxWidth <= 320) {
        scaleWidth = 1;
        imageWidth *= 2;
    }

    if (a->lines <= FB_MAX_LINES / 4) {
        scaleHeight = 1;
        imageHeight *= 2;
    }
    else if (a->lines <= FB_MAX_LINES / 2) {
        if (a->interlace == INTERLACE_NONE) {
            scaleHeight = 1;
        }
        imageHeight *= 2;
    }

    if (mixMode & mixMask) {
        pImage = archVideoInBufferGet(imageWidth, imageHeight);
    }
    if (pImage == NULL) {
        pImage = getBlackImage();
    }

    if (scaleHeight) {
        for (y = a->lines - 1; y >= 0; y--) {
            UInt16* pSrc = a->line[y].buffer;
            UInt16* pDst1 = a->line[2*y+0].buffer;
            UInt16* pDst2 = a->line[2*y+1].buffer;
            UInt16* pImg1 = pImage+(2*y+0) * imageWidth;
            UInt16* pImg2 = pImage+(2*y+1) * imageWidth;

            /* The mixing below handles lines of up to twice the width. */
            if (a->line[y].doubleWidth > 1) {
                narrowLine(a, y);
            }

            if (scaleWidth && a->line[y].doubleWidth) {
                for (x = imageWidth - 1; x >= 0; x--) {
                    UInt16 val = pSrc[x];
                    if (val & BKMODE_TRANSPARENT) {
                        pDst1[x] = pImg1[x];
                        pDst2[x] = pImg2[x];
                    }
                    else {
                        pDst1[x] = val;
                        pDst2[x] = val;
                    }
                }
            }
            else {
                for (x = imageWidth - 1; x >= 0; x--) {
                    UInt16 val = pSrc[x / 2];
                    if (val & BKMODE_TRANSPARENT) {
                        pDst1[x] = pImg1[x];
                        pDst2[x] = pImg2[x];
                        x--;
                        pDst1[x] = pImg1[x];
                        pDst2[x] = pImg2[x];
                    }
                    else {
                        pDst1[x] = val;
                        pDst2[x] = val;
                        x--;
                        pDst1[x] = val;
                        pDst2[x] = val;
                    }
                }
            }

            if (scaleWidth) {
                a->line[2*y+0].doubleWidth = 1;
                a->line[2*y+1].doubleWidth = 1;
            }
        }

        a->lines *= 2;
    }
    else {
        if (a->interlace == INTERLACE_ODD) {
            pImage += imageWidth;
        }

        for (y = a->lines - 1; y >= 0; y--) {
            UInt16* pSrc = a->line[y].buffer;
            UInt16* pDst = a->line[y].buffer;
            UInt16* pImg = pImage + y * imageWidth;

            /* The mixing below handles lines of up to twice the width. */
            if (a->line[y].doubleWidth > 1) {
                narrowLine(a, y);
            }

            if (scaleWidth && a->line[y].doubleWidth) {
                for (x = imageWidth - 1; x >= 0; x--) {
                    UInt16 val = pSrc[x];
                    if (val & BKMODE_TRANSPARENT) {
                        pDst[x] = pImg[x];
                    }
                    else {
                        pDst[x] = val;
                    }
                }
            }
            else {
                for (x = imageWidth - 1; x >= 0; x--) {
                    UInt16 val = pSrc[x / 2];
                    if (val & BKMODE_TRANSPARENT) {
                        pDst[x] = pImg[x];
                        x--;
                        pDst[x] = pImg[x];
                    }
                    else {
                        pDst[x] = val;
                        x--;
                        pDst[x] = val;
                    }
                }
            }
            if (scaleWidth) {
                a->line[y].doubleWidth = 1;
            }
        }
    }
}
