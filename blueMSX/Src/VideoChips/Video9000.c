/*****************************************************************************
**
** Video9000 video switch with V9990 superimpose.
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
#include "Video9000.h"
#include "FrameBuffer.h"
#include "VideoManager.h"
#include "Board.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>

/* Port 0x6F bits 4-3: 0x V9990 only, 10 VDP only, 11 V9990 over the VDP. */
#define MODE_MASK        0x18
#define MODE_VDP         0x10
#define MODE_SUPERIMPOSE 0x18

struct Video9000 {
    V9990* v9990;
    UInt8  value;
    int    handle;
    FrameBufferData* composite;
    /* NULL once the frame buffer is gone. */
    FrameBufferData* vdpFrames;
    FrameBufferData* v9990Frames;
};

static int lineCount(FrameBuffer* frame)
{
    return frame->lines < FB_MAX_LINES ? frame->lines : FB_MAX_LINES;
}

static int sampleCount(FrameBuffer* frame, int y)
{
    int width = frameBufferGetLineWidth(frame, y);
    return width < FB_MAX_LINE_WIDTH ? width : FB_MAX_LINE_WIDTH;
}

static void copyFrame(FrameBuffer* dst, FrameBuffer* src)
{
    int lines = lineCount(src);
    int y;

    dst->interlace       = src->interlace;
    dst->interlaceRaster = src->interlaceRaster;
    dst->maxWidth        = src->maxWidth;
    dst->lines           = lines;
    for (y = 0; y < lines; y++) {
        dst->line[y].doubleWidth = src->line[y].doubleWidth;
        memcpy(dst->line[y].buffer, src->line[y].buffer, sampleCount(src, y) * sizeof(UInt16));
    }
}

static int sameGeometry(FrameBuffer* a, FrameBuffer* b)
{
    int y;

    if (a->lines != b->lines || a->interlace != b->interlace || a->maxWidth != b->maxWidth) {
        return 0;
    }
    for (y = 0; y < lineCount(a); y++) {
        if (a->line[y].doubleWidth != b->line[y].doubleWidth) {
            return 0;
        }
    }
    return 1;
}

/* The line of a source shown on output line y: a source with fewer lines
** than the output is stretched to its height. */
static int sourceLine(FrameBuffer* frame, int lines, int y)
{
    int n = lineCount(frame);
    int line = y * n / lines;
    return line < n ? line : n - 1;
}

/* A field type applies only to a source as tall as the output. */
static InterlaceMode interlaceOf(FrameBuffer* frame, int lines)
{
    return lineCount(frame) == lines ? frame->interlace : INTERLACE_NONE;
}

/* Each output sample shows top unless bit 15 of key, which has the geometry
** of top, is set; the narrower line is widened by repeating its samples.
** A narrower bottom frame is centred, its edge samples repeated outside. */
static void superimposeFrame(FrameBuffer* dst, FrameBuffer* top, FrameBuffer* key, FrameBuffer* bottom)
{
    int lines = lineCount(top) > lineCount(bottom) ? lineCount(top) : lineCount(bottom);
    int offset = (top->maxWidth - bottom->maxWidth) / 2;
    int y;

    dst->interlace       = interlaceOf(top, lines) != INTERLACE_NONE ?
                           interlaceOf(top, lines) : interlaceOf(bottom, lines);
    dst->interlaceRaster = top->interlaceRaster || bottom->interlaceRaster;
    dst->maxWidth        = top->maxWidth;
    dst->lines           = lines;
    for (y = 0; y < lines; y++) {
        int ty = sourceLine(top, lines, y);
        int by = sourceLine(bottom, lines, y);
        int ts = frameBufferGetLineShift(top, ty);
        int bs = frameBufferGetLineShift(bottom, by);
        int shift = ts > bs ? ts : bs;
        int tw = sampleCount(top, ty) - 1;
        int bw = sampleCount(bottom, by) - 1;
        int width = top->maxWidth << shift;
        UInt16* t = top->line[ty].buffer;
        UInt16* k = key->line[ty].buffer;
        UInt16* b = bottom->line[by].buffer;
        UInt16* d = dst->line[y].buffer;
        int x;

        if (width > FB_MAX_LINE_WIDTH) {
            width = FB_MAX_LINE_WIDTH;
        }
        dst->line[y].doubleWidth = shift;
        for (x = 0; x < width; x++) {
            int tx = x >> (shift - ts);
            int bx = (x >> (shift - bs)) - offset * (1 << bs);
            UInt16 v = t[tx < tw ? tx : tw];
            if (k[tx < tw ? tx : tw] & 0x8000) {
                v = b[bx < 0 ? 0 : bx < bw ? bx : bw];
            }
            d[x] = v & 0x7fff;
        }
    }
}

/* Composes the picture into the composite frame buffer while it is shown.
** It runs inside a frame flip, so it only draws and flips the composite. */
static void compose(Video9000* v, FrameBuffer* top, FrameBuffer* key, FrameBuffer* bottom)
{
    FrameBuffer* out;

    if (frameBufferGetActive() != v->composite) {
        return;
    }
    out = frameBufferGetDrawFrameOf(v->composite);
    if (out == NULL) {
        return;
    }
    if (bottom != NULL) {
        superimposeFrame(out, top, key, bottom);
    }
    else {
        copyFrame(out, top);
    }
    frameBufferFlipDrawFrameOf(v->composite);
}

static void onVdpFlip(Video9000* v, FrameBuffer* completed, FrameBuffer* drawn)
{
    if (completed == NULL) {
        v->vdpFrames = NULL;
        return;
    }
    if ((v->value & MODE_MASK) == MODE_VDP) {
        compose(v, completed, NULL, NULL);
    }
}

/* Not genlocked: superimpose uses the last complete VDP frame. */
static void onV9990Flip(Video9000* v, FrameBuffer* completed, FrameBuffer* drawn)
{
    FrameBuffer* bottom;

    if (completed == NULL) {
        v->v9990Frames = NULL;
        return;
    }
    switch (v->value & MODE_MASK) {
    case MODE_VDP:
        return;
    case MODE_SUPERIMPOSE:
        bottom = v->vdpFrames != NULL ? frameBufferGetLastCompletedOf(v->vdpFrames) : NULL;
        if (bottom == NULL) {
            /* Keeps the last picture until the VDP completes a frame. */
            return;
        }
        if (!v9990IsFrameKeyed(v->v9990)) {
            /* Drawn before superimpose was on: the VDP alone until the next frame. */
            compose(v, bottom, NULL, NULL);
            return;
        }
        /* A blended frame takes its keys from alternate frames, so the keys
        ** come from the frame as drawn. */
        compose(v, sameGeometry(completed, drawn) ? completed : drawn, drawn, bottom);
        return;
    }
    compose(v, completed, NULL, NULL);
}

/* The chip that the picture follows presents the screen at its rate. */
static void syncRates(Video9000* v)
{
    int mode = v->value & MODE_MASK;

    if (mode == MODE_VDP) {
        v9990SetPresenter(v->v9990, -1, 0);
        videoManagerSetPacesScreen(v->handle, 0);
        videoManagerSetRefreshRate(v->handle, 0);
        videoManagerSetAspectRate(v->handle, 0);
    }
    else {
        videoManagerSetPacesScreen(v->handle, 1);
        v9990SetPresenter(v->v9990, v->handle, mode == MODE_SUPERIMPOSE);
    }
}

Video9000* video9000Create(V9990* v9990)
{
    VideoCallbacks callbacks = { NULL, NULL, 0, 1 };
    int vdpHandle = videoManagerGetHandle(0);
    Video9000* v;

    if (vdpHandle < 0 || vdpHandle == v9990GetVideoHandle(v9990)) {
        return NULL;
    }

    v = calloc(1, sizeof(Video9000));
    v->v9990       = v9990;
    v->value       = 0x10;
    v->vdpFrames   = videoManagerGetFrameBuffer(vdpHandle);
    v->v9990Frames = videoManagerGetFrameBuffer(v9990GetVideoHandle(v9990));
    v->composite   = frameBufferDataCreate(272, 240, 1);
    frameBufferDataSetNoBlend(v->composite, 1);

    v->handle = videoManagerRegister("Video9000", v->composite, &callbacks, v);
    videoManagerSetDrawSource(v->handle, vdpHandle);
    /* While inserted, only the cartridge switches the shown source. */
    videoManagerClaim(v->handle, 1);

    frameBufferSetFlipHook(v->vdpFrames, (FrameBufferFlipHook)onVdpFlip, v);
    frameBufferSetFlipHook(v->v9990Frames, (FrameBufferFlipHook)onV9990Flip, v);
    syncRates(v);

    if (boardGetVideoAutodetect()) {
        videoManagerActivate(v->handle);
    }
    return v;
}

void video9000Destroy(Video9000* v)
{
    if (v->vdpFrames != NULL) {
        frameBufferSetFlipHook(v->vdpFrames, NULL, NULL);
    }
    if (v->v9990Frames != NULL) {
        frameBufferSetFlipHook(v->v9990Frames, NULL, NULL);
    }
    v9990SetPresenter(v->v9990, -1, 0);
    videoManagerUnregister(v->handle);
    frameBufferDataDestroy(v->composite);
    free(v);
}

void video9000Reset(Video9000* v)
{
    v->value = 0x10;
    syncRates(v);
}

void video9000Write(Video9000* v, UInt8 value)
{
    UInt8 changed = v->value ^ value;

    if (changed == 0) {
        return;
    }
    v->value = value;
    syncRates(v);
    if ((changed & MODE_MASK) && boardGetVideoAutodetect()) {
        videoManagerActivate(v->handle);
    }
}

void video9000SaveState(Video9000* v)
{
    SaveState* state = saveStateOpenForWrite("Video9000");
    saveStateSet(state, "value", v->value);
    saveStateClose(state);
}

/* The shown source is restored by the video manager, not switched here. */
void video9000LoadState(Video9000* v)
{
    SaveState* state = saveStateOpenForRead("Video9000");
    v->value = (UInt8)saveStateGet(state, "value", 0x10);
    saveStateClose(state);
    syncRates(v);
}
