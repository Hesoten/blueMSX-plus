/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/VideoChips/VDP.c,v $
**
** $Revision: 1.98 $
**
** $Date: 2009-07-01 05:00:23 $
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
#include "VDP.h"
#include "V9938.h"
#include "Board.h"
#include "Board.h"
#include "IoPort.h"
#include "SaveState.h"
#include "DeviceManager.h"
#include "DebugDeviceManager.h"
#include "FrameBuffer.h"
#include "ArchVideoIn.h"
#include "Language.h"
#include "VdpDebug.h"
#include "Emulator.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


//#define ENABLE_VRAM_DECAY
// Global configuration (not device specific)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static int spritesEnable = 1;
static int noSpriteLimits = 0;
static int displayEnable = 1;
static int refreshRate   = 0;
static int canFlipFrameBuffer = 0;

/* The setting; spritesEnable drops to 0 while the sprite split redraws a line. */
static int spritesSetting = 1;

void vdpSetSpritesEnable(int enable) {
    spritesSetting = enable ? 1 : 0;
    spritesEnable = spritesSetting;
}

int vdpGetSpritesEnable() {
    return spritesSetting;
}

void vdpSetNoSpriteLimits(int enable) {
    noSpriteLimits = enable ? 1 : 0;
}

int vdpGetNoSpritesLimit() {
    return noSpriteLimits;
}


void vdpSetDisplayEnable(int enable) {
    displayEnable = enable ? 1 : 0;
}

int vdpGetDisplayEnable() {
    return displayEnable;
}

int vdpGetRefreshRate() 
{
    return refreshRate;
}


// Video DA management

typedef struct {
    int handle;
    VideoMode videoModeMask;
    VdpDaCallbacks callbacks;
    void* ref;
} VdpDaDevice;

static void  daStart(void* ref, int oddPage) {}
static void  daEnd(void* ref) {}
static UInt8 daRead(void* ref, int screenMode, int x, int y, Pixel* palette, int count) { return 0x00; }

static VdpDaDevice vdpDaDevice = {
    0,
    VIDEO_INTERNAL,
    { daStart, daEnd, daRead },
    NULL
};

int vdpRegisterDaConverter(VdpDaCallbacks* callbacks, void* ref, VideoMode videoModeMask)
{
    vdpDaDevice.videoModeMask     = videoModeMask;
    vdpDaDevice.callbacks.daStart = callbacks->daStart ? callbacks->daStart : daStart;
    vdpDaDevice.callbacks.daEnd   = callbacks->daEnd   ? callbacks->daEnd   : daEnd;
    vdpDaDevice.callbacks.daRead  = callbacks->daRead  ? callbacks->daRead  : daRead;
    vdpDaDevice.ref               = ref;
    vdpDaDevice.handle++;

    return vdpDaDevice.handle;
}

void vdpUnregisterDaConverter(int vdpDaHandle)
{
    if (vdpDaHandle == vdpDaDevice.handle) {
        vdpDaDevice.videoModeMask       = VIDEO_INTERNAL;
        vdpDaDevice.callbacks.daStart   = daStart;
        vdpDaDevice.callbacks.daEnd     = daEnd;
        vdpDaDevice.callbacks.daRead    = daRead;
    }
}



// VDP emulation

#define HPERIOD      1368

#define INT_IE0     0x01
#define INT_IE1     0x02

#define VRAM_SIZE (256 * 1024)
/* What a state holds of every VDP but the V9968, as before the V9968. */
#define VRAM_STATE_SIZE (192 * 1024)

static int vramAddr;
/* A17 sits outside the odd/even rotation and picks a 128kB half. */
#define VDP_ILV(vdp, a) ((vdp)->vramA17 ? (((a) & 0x20000) | VDP_VRAM_INDEX((a) & 0x1ffff)) : VDP_VRAM_INDEX(a))
#define MAP_VRAM(vdp, addr) ((vdp)->vramPtr + ((vramAddr = addr, VDP_VRAM_INTERLEAVED((vdp)->screenMode) ? VDP_ILV(vdp, vramAddr) : vramAddr) & (vdp)->vramAccMask))
#define MAP_VRAMINDEX(vdp, addr) (((vramAddr = addr, VDP_VRAM_INTERLEAVED((vdp)->screenMode) ? VDP_ILV(vdp, vramAddr) : vramAddr)))

#define vdpIsSpritesBig(regs)        (regs[1]  & 0x01)
#define vdpIsSprites16x16(regs)      (regs[1]  & 0x02)
#define vdpIsSpritesOff(regs)        (regs[8]  & 0x02)
#define vdpIsColor0Solid(regs)       (regs[8]  & 0x20)
#define vdpIsMsx1Vdp(vdp)            ((vdp)->vdpVersion == VDP_TMS9918A || (vdp)->vdpVersion == VDP_TMS99x8A || (vdp)->vdpVersion == VDP_TMS9929A)
// The VDPs with 64 registers, a command engine and the extended port set.
#define vdpIsV99x8(vdp)              ((vdp)->vdpVersion == VDP_V9938 || (vdp)->vdpVersion == VDP_V9958 || (vdp)->vdpVersion == VDP_V9968)
#define vdpIsV9968(vdp)              ((vdp)->vdpVersion == VDP_V9968)
// R#21 bit0 V58: set means behave as a V9958, clear means as a V9968.
#define vdpIsV9968Native(vdp)        (vdpIsV9968(vdp) && !((vdp)->vdpRegs[21] & 0x01))
// R#20 bit4 EPAL: 256 entries of 5 bit RGB, written three bytes at a time.
#define vdpIsExtPalette(vdp)         (vdpIsV9968(vdp) && ((vdp)->vdpRegs[20] & 0x10))
#define vdpIsVideoPal(vdp)          (((vdp)->vdpRegs[9]  & (vdp)->palMask & 0x02) | (vdp)->palValue)
#define vdpIsOddPage(vdp)           (((~(vdp)->vdpStatus[2] & 0x02) << 7) & (((vdp)->vdpRegs[9]  & 0x04) << 6))
// V9938 blink page alternation: while the blink OFF phase is active the odd
// display page selected in R#2 is ANDed down to its even pair, like interlace.
#define vdpBlinkEvenPage(vdp)       (((vdp)->vdpRegs[13] != 0 && !(vdp)->blinkFlag) ? 0x100 : 0)
#define vdpIsInterlaceOn(regs)       (regs[9]  & 0x08)
#define vdpIsScanLines212(regs)      (regs[9]  & 0x80)
#define vdpIsEdgeMasked(regs)        (regs[25] & 0x02)
#define vdpIsModeYJK(regs)           (regs[25] & 0x08)
#define vdpIsModeYAE(regs)           (regs[25] & 0x10)
#define vdpVScroll(vdp)              ((vdp)->vdpRegs[23])
#define vdpHScroll(vdp)       ((((int)((vdp)->vdpRegs[26]&0x3F)<<3)-(int)((vdp)->vdpRegs[27]&0x07))&~(~(int)vdpHScroll512(vdp)<<8))
#define vdpHScroll512(vdp)    ((vdp)->vdpRegs[25]&((vdp)->vdpRegs[2]>>5)&0x1)


static const UInt8 registerValueMaskMSX1[8] = {
	0x03, 0xfb, 0x0f, 0xff, 0x07, 0x7f, 0x07, 0xff
};

static const UInt8 registerValueMaskMSX2[64] = {
	0x7e, 0x7f, 0x7f, 0xff, 0x3f, 0xff, 0x3f, 0xff,   /* R#1 bit 2 (line-blink) writable */
	0xfb, 0xbf, 0x07, 0x03, 0xff, 0xff, 0x07, 0x0f,
	0x0f, 0xbf, 0xff, 0xff, 0x3f, 0x3f, 0x3f, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static const UInt8 registerValueMaskMSX2p[64] = {
	0x7e, 0x7f, 0x7f, 0xff, 0x3f, 0xff, 0x3f, 0xff,   /* R#1 bit 2 (line-blink) writable */
	0xfb, 0xbf, 0x07, 0x03, 0xff, 0xff, 0x07, 0x0f,
	0x0f, 0xbf, 0xff, 0xff, 0x3f, 0x3f, 0x3f, 0xff,
    0x00, 0x7f, 0x3f, 0x07, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* The V9958 mask, widened for the 8 bit R#16 index and the R#20 mode bits. */
static const UInt8 registerValueMaskV9968[64] = {
	0x7e, 0x7f, 0x7f, 0xff, 0x3f, 0xff, 0x3f, 0xff,
	0xfb, 0xbf, 0x07, 0x03, 0xff, 0xff, 0x0f, 0x0f,
	0xff, 0xbf, 0xff, 0xff, 0xff, 0x3f, 0x3f, 0xff,
    0x00, 0x7f, 0x3f, 0x07, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static struct {
    int r;
    int g;
    int b;
} msx1Palette[16] = {
#if 0
    { 0, 0, 0 }, 
    { 0, 0, 0 }, 
    { 0, 241, 20 }, 
    { 68, 249, 86 }, 
    { 85, 79, 255 }, 
    { 128, 111, 255 }, 
    { 250, 80, 51 }, 
    { 12, 255, 255 }, 
    { 255, 81, 52 }, 
    { 255, 115, 86 }, 
    { 226, 210, 4 }, 
    { 242, 217, 71 }, 
    { 4, 212, 19 }, 
    { 231, 80, 229 }, 
    { 208, 208, 208 }, 
    { 255, 255, 255 }
#elif 0
    { 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00 }, 
    { 0x21, 0xc8, 0x42 }, 
    { 0x5e, 0xdc, 0x78 }, 
    { 0x54, 0x55, 0xed }, 
    { 0x7d, 0x76, 0xfc }, 
    { 0xd4, 0x52, 0x4d },
    { 0x42, 0xeb, 0xf5 },
    { 0xfc, 0x55, 0x54 }, 
    { 0xff, 0x79, 0x78 }, 
    { 0xd4, 0xc1, 0x54 }, 
    { 0xe6, 0xce, 0x80 }, 
    { 0x21, 0xb0, 0x3b }, 
    { 0xc9, 0x5b, 0xba }, 
    { 0xcc, 0xcc, 0xcc }, 
    { 0xff, 0xff, 0xff }
#else
	// TMS9929A
	{   0,   0,   0 },
	{   0,   0,   0 },
	{  62, 184,  73 },
	{ 116, 208, 125 },
	{  89,  85, 224 },
	{ 128, 118, 241 },
	{ 185,  94,  81 },
	{ 101, 219, 239 },
	{ 219, 101,  89 },
	{ 255, 137, 125 },
	{ 204, 195,  94 },
	{ 222, 208, 135 },
	{  58, 162,  65 },
	{ 183, 102, 181 },
	{ 204, 204, 204 },
	{ 255, 255, 255 }
#endif
};

static struct {
    int r;
    int g;
    int b;
} msx1Palette_9918a[16] = {
	// TMS9918A
	{   0,   0,   0 },
	{   0,   0,   0 },
	{  79, 176,  69 },
	{ 129, 202, 119 },
	{  95,  81, 237 },
	{ 129, 116, 255 },
	{ 173, 101,  77 },
	{ 103, 195, 228 },
	{ 204, 110,  80 },
	{ 240, 146, 116 },
	{ 193, 202,  81 },
	{ 209, 215, 129 },
	{  72, 156,  59 },
	{ 176, 104, 190 },
	{ 204, 204, 204 },
	{ 255, 255, 255 }
};

static struct {
    int r;
    int g;
    int b;
} msx2Palette[16] = {
    { 0x00, 0x00, 0x00 }, 
    { 0x00, 0x00, 0x00 }, 
    { 0x24, 0xda, 0x24 }, 
    { 0x68, 0xff, 0x68 }, 
    { 0x24, 0x24, 0xff }, 
    { 0x48, 0x68, 0xff },
    { 0xb6, 0x24, 0x24 },
    { 0x48, 0xda, 0xff },
    { 0xff, 0x24, 0x24 }, 
    { 0xff, 0x68, 0x68 },
    { 0xda, 0xda, 0x24 }, 
    { 0xda, 0xda, 0x91 }, 
    { 0x24, 0x91, 0x24 }, 
    { 0xda, 0x48, 0xb6 }, 
    { 0xb6, 0xb6, 0xb6 }, 
    { 0xff, 0xff, 0xff }
};

static UInt16 defaultPaletteRegs[16] = {
    0x0000, 0x0000, 0x0611, 0x0733, 0x0117, 0x0327, 0x0151, 0x0627,
    0x0171, 0x0373, 0x0661, 0x0664, 0x0411, 0x0265, 0x0555, 0x0777
};

typedef struct VDP VDP;

static VDP* theVdp = NULL;
static void sprSplitLineDone(VDP*, int);
static void sprPaletteDisplayStart(VDP*);

static void RefreshLineTx80(VDP*, int, int, int);
static void RefreshLine0(VDP*, int, int, int);
static void RefreshLine0Plus(VDP*, int, int, int);
static void RefreshLine1(VDP*, int, int, int);
static void RefreshLine2(VDP*, int, int, int);
static void RefreshLine3(VDP*, int, int, int);
static void RefreshLine4(VDP*, int, int, int);
static void RefreshLine5(VDP*, int, int, int);
static void RefreshLine6(VDP*, int, int, int);
static void RefreshLine7(VDP*, int, int, int);
static void RefreshLine8(VDP*, int, int, int);
static void RefreshLine10(VDP*, int, int, int);
static void RefreshLine12(VDP*, int, int, int);

static void sync(VDP*, UInt32);

struct VDP {
    VdpCmdState* cmdEngine;
    void (*RefreshLine)(VDP*, int, int, int);
    int    vdpConnector;
    int    vdpVersion;

    const UInt8* registerValueMask;
    UInt8  registerMask;

    /* P#4 bit7. While set, writes to R#20 and R#21 are discarded. */
    int    extRegsLocked;

    /* Which byte of a palette entry comes next: two per entry, three with EPAL.
    ** The first shares the write latch with the register ports, as it always has. */
    int    palKey;
    UInt8  paletteLatch;

    UInt8  palMask;
    UInt8  palValue;
    int    scr0splitLine;
    int    vramPage;
    int    sprGenBase;
    int    sprTabBase;
    int    colTabBase;
    int    chrTabBase;
    int    chrGenBase;
    UInt8  FGColor;
    UInt8  BGColor;
    UInt8  XFGColor;
    UInt8  XBGColor;
    int    blinkFlag;
    int    blinkCnt;
    int    blinkLineBase;
    int    drawArea;
    /* drawArea as the command engine sees it. Fetching starts a fetch window
    ** before the first display line, so this goes up earlier. */
    int    cmdDrawArea;
    UInt16 paletteReg[16];
    int    vramSize;
    int    vramPages;
    int    vram128;
    int    vram256;
    int    vramA17;
    int    vram192;
    int    vram16;
    int    vramEnable;
    int    vramMask;
    int    lineOffset;
    /* Set while the current line is being laid out again after a mid-line
    ** horizontal scroll change, so the replay repeats no side effects. */
    int    reScroll;
    int    firstLine;
    int    lastLine;
    int    displayOffest;
    int    leftBorder;
    UInt32 displayArea;

    int    curLine;

    int    screenOn;
    int    VAdjust;
    int    HAdjust;
    int    hAdjustSc0;

    int screenMode;
    UInt8  vdpRegs[64];
    UInt8  vdpStatus[16];

    int    vdpKey;
    UInt8  vdpData;
    UInt8  vdpDataLatch;
    UInt16 vramAddress;

    UInt32 frameStartTime;

    BoardTimer* timerDisplay;
    BoardTimer* timerDrawAreaStart;
    BoardTimer* timerVStart;
    BoardTimer* timerScrModeChange;
    BoardTimer* timerHint;
    BoardTimer* timerVint;
    BoardTimer* timerTmsVint;
    BoardTimer* timerDrawAreaEnd;

    UInt32 timeScrMode;
    UInt32 timeHint;
    UInt32 timeVint;
    UInt32 timeTmsVint;
    UInt32 timeDrawAreaStart;
    UInt32 timeVStart;
    UInt32 timeDisplay;
    UInt32 timeDrawAreaEnd;

    int timeScrModeEn;
    int timeHintEn;
    int timeVintEn;
    int timeDrawAreaStartEn;
    int timeVStartEn;
    int timeDisplayEn;
    int timeDrawAreaEndEn;

    UInt32 screenOffTime;
    
    Pixel paletteFixed[256];
    Pixel  paletteExt[256];
    UInt16 paletteExtReg[256];
    Pixel paletteSprite8[16];
    Pixel  palette0;
    Pixel palette[16];
    Pixel yjkColor[32][64][64];

    UInt8* vramPtr;
    int    vramAccMask;
    int vramOffsets[2];
    int vramMasks[4];
    UInt8  vram[VRAM_SIZE];
    
    int deviceHandle;
    int debugHandle;
    int videoHandle;
    int videoEnabled;
    /* flippedEarly: this frame was flipped at the end of active display, and onDisplay
    ** has not run yet; lastFlipEarly: the latest flip was such a one. */
    int flippedEarly;
    int lastFlipEarly;
    UInt32 machineFrame;   /* machine VDP frames begun since power-on; saved with the state */

    FrameBufferData* frameBuffer;
};

#include "SpriteLine.h"
#include "Common.h"


static void digitize(VDP* vdp);
static void updateOutputMode(VDP* vdp);

/* R#21 bit0 clear is what opens the second 128kB to the CPU port and the
** command engine. The display tables still cap at 17 bits. */
static void vdpUpdateVramMode(VDP* vdp)
{
    int wide = vdp->vram256 && vdpIsV9968Native(vdp);

    vdp->vramA17     = wide ? 0x20000 : 0;
    vdp->vramAccMask = wide ? vdp->vramMask
                            : vdp->vramMasks[((vdp->vdpRegs[8] & 0x08) >> 2) | ((vdp->vdpRegs[0x2d] >> 6) & 1)];
    if (wide) {
        vdp->vramPtr    = vdp->vram;
        vdp->vramEnable = 1;
    }
    vdpCmdSetVram256(vdp->cmdEngine, wide);
}


#include "SpriteLine.h"

// Minimum safe distance between vram access
//
// ScrMode VBLANK    BlankBit  Display
//         (S2&0x40) (R1&0x40) 
// 0       2us       2us      3.1us    (R1&0x10)
// 1       2us       2us      7.95us
// 2       2us       2us      7.95us   (R0&0x02)
// 3       2us       2us      3.5us    (R1&0x08)
static void checkVramAccessTimeTms(VDP* vdp)
{
    static UInt32 oldTime = 0xffff0000;

    if (debuggerCheckVramAccess()) {
        UInt32 minTime;

        if (vdp->vdpStatus[2] & 0x40) {          // VBLANK
            minTime = 43;                        // 2us
        }
        else {
            if ((vdp->vdpRegs[1] & 0x40) == 0) { // BLK (screen disabled)
                minTime = 43;                    // 2us
            }
            else if (vdp->vdpRegs[0] & 0x02) {   // Screen 2 (incl mixed modes)
                minTime = 171;                   // 7.95us
            }
            else if (vdp->vdpRegs[1] & 0x08) {   // Screen 3
                minTime = 76;                    // 3.5us
            }
            else if (vdp->vdpRegs[1] & 0x10) {   // Screen 0
                minTime = 67;                    // 3.1us
            }
            else {                               // Screen 1
                minTime = 171;                   // 7.95us
            }
        }

        if (boardSystemTime() - oldTime < minTime) {
            boardOnBreakpoint(DBG_STOP_VRAM_TIMING);
        }
        oldTime = boardSystemTime();
    }
}


static void vdpBlink(VDP* vdp)
{
    if(vdp->blinkCnt) {
        vdp->blinkCnt--;
    }
    else {
        int onTime  = (vdp->vdpRegs[13] & 0x0f) * 10;
        int offTime = (vdp->vdpRegs[13] >> 4) * 10;
        vdp->blinkFlag = !vdp->blinkFlag;
        if (onTime == 0 || offTime == 0) {
            // A zero duration pins the blink in the other phase (R#13 = 0
            // pins the ON phase, i.e. normal colors / the page set in R#2).
            vdp->blinkFlag = offTime == 0;
        }
        vdp->blinkCnt = vdp->blinkFlag ? onTime : offTime;
        if (vdp->blinkFlag) {
            vdp->XFGColor = vdp->FGColor;
            vdp->XBGColor = vdp->BGColor;
        }
        else {
            vdp->XFGColor = vdp->vdpRegs[12] >> 4;
            vdp->XBGColor = vdp->vdpRegs[12] & 0x0f;
        }
    }
}

/* R#1 bit 2: derive the blink phase for one scan line from a per-frame base
** plus the line number, so the page alternates band-by-band and the bands
** drift by (lines-per-frame mod period) each frame. */
static void vdpLineBlink(VDP* vdp)
{
    int r13    = vdp->vdpRegs[13];
    int onTime = (r13 & 0x0f) * 10;
    int period = onTime + (r13 >> 4) * 10;
    if (period > 0) {
        int ph = (vdp->blinkLineBase + vdp->curLine) % period;
        vdp->blinkFlag = ph < onTime;
        if (vdp->blinkFlag) {
            vdp->XFGColor = vdp->FGColor;
            vdp->XBGColor = vdp->BGColor;
        }
        else {
            vdp->XFGColor = vdp->vdpRegs[12] >> 4;
            vdp->XBGColor = vdp->vdpRegs[12] & 0x0f;
        }
    }
}

static void scheduleScrModeChange(VDP* vdp)
{
    vdp->timeScrMode = vdp->frameStartTime + HPERIOD * (1 + (boardSystemTime() - vdp->frameStartTime) / HPERIOD);
    vdp->timeScrModeEn = 1;
    boardTimerAdd(vdp->timerScrModeChange, vdp->timeScrMode);
}

static void scheduleHint(VDP* vdp)
{
    vdp->timeHint = vdp->frameStartTime + 
        (vdp->firstLine + ((vdp->vdpRegs[19] - vdp->vdpRegs[23]) & 0xff)) * HPERIOD + 
        vdp->leftBorder + vdp->displayArea;
    vdp->timeHintEn = 1;
    boardTimerAdd(vdp->timerHint, vdp->timeHint + 20);
}

static void scheduleVint(VDP* vdp)
{
    vdp->timeVint = vdp->frameStartTime + 
                    (vdp->firstLine + ((vdp->vdpRegs[9] & 0x80) ? 212 : 192)) * HPERIOD + 
                    vdp->leftBorder - 10;
    vdp->timeVintEn = 1;
    boardTimerAdd(vdp->timerVint, vdp->timeVint);
    
    vdp->timeDrawAreaEnd = vdp->timeVint - (vdp->leftBorder - 10);
    vdp->timeDrawAreaEndEn = 1;
    boardTimerAdd(vdp->timerDrawAreaEnd, vdp->timeDrawAreaEnd);
#if 0
    if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
        vdp->timeTmsVint = vdp->timeVint + 1176;
        boardTimerAdd(vdp->timerTmsVint, vdp->timeTmsVint);
    }
#endif
}

static void scheduleDrawAreaStart(VDP* vdp)
{
    vdp->timeDrawAreaStart = vdp->frameStartTime + ((vdp->drawArea ? 3 + 13 : vdp->firstLine) - 1) * HPERIOD + vdp->leftBorder + vdp->displayArea + 13;
    vdp->timeDrawAreaStartEn = 1;
    boardTimerAdd(vdp->timerDrawAreaStart, vdp->timeDrawAreaStart);

    vdp->timeVStart = vdp->frameStartTime + (vdp->firstLine - 1) * HPERIOD + vdp->leftBorder - 10;
    vdp->timeVStartEn = 1;
    boardTimerAdd(vdp->timerVStart, vdp->timeVStart);
}

static void onHint(VDP* vdp, UInt32 time)
{
    sync(vdp, time);

    vdp->timeHintEn = 0;
    if (vdpRasterArmed) vdpRasterNoteLineInt(vdp);

    if (vdp->vdpRegs[0] & 0x10) {
        boardSetInt(INT_IE1);
    }
}

int framecounter = 0;

/* Set while the tap runs at theVdp's end of active display. */
static int vdpAtShown;

static int shotDrawnSamples(VDP* vdp, FrameBuffer* draw, int y);

/* Completes the frame at the end of active display and flips it. The rows still to come
** are the last frame's, unless that was flipped early too: then the draw frame already
** holds that frame's own bottom rows. */
static void showEarly(void* ref)
{
    VDP* vdp = (VDP*)ref;
    FrameBuffer* draw = frameBufferGetDrawFrameOf(vdp->frameBuffer);
    FrameBuffer* last;
    UInt32 seq, flips;

    last = frameBufferGetLastDrawnOf(vdp->frameBuffer, &seq, &flips);
    /* A replay draws every frame over one draw frame, which holds the last one's rows. */
    if (!vdp->lastFlipEarly && !emulatorRewindReplaying() && draw != NULL && last != NULL && last != draw) {
        int row  = MAX(vdp->curLine - vdp->displayOffest, 0);
        int rows = MIN(draw->lines, last->lines);
        int y;
        for (y = row; y < rows; y++) {
            int shift = last->line[y].doubleWidth;
            int width = last->maxWidth << shift;
            int from  = y == row ? shotDrawnSamples(vdp, draw, y) : 0;
            if (from > 0 && draw->line[y].doubleWidth != shift) {
                continue;
            }
            if (width > from) {
                memcpy(draw->line[y].buffer + from, last->line[y].buffer + from, (width - from) * sizeof(UInt16));
            }
            draw->line[y].doubleWidth = shift;
        }
    }
    vdp->flippedEarly  = 1;
    vdp->lastFlipEarly = 1;
    frameBufferFlipDrawFrame();
}

static void onVint(VDP* vdp, UInt32 time)
{
    sync(vdp, time);

    vdp->timeVintEn = 0;

    framecounter++;

//    vdp->lineOffset = -1;
    vdp->vdpStatus[0] |= 0x80;
    vdp->vdpStatus[2] |= 0x40;
//    if (vdp->vdpVersion != VDP_TMS9929A && vdp->vdpVersion != VDP_TMS99x8A && vdp->vdpVersion != VDP_TMS9918A) {
        if (vdp->vdpRegs[1] & 0x20) {
            boardSetInt(INT_IE0);
        }
//    }
//    vdp->drawArea = 0;
    vdp->cmdDrawArea = 0;
    vdpSetTimingMode(vdp->cmdEngine, vdp->vdpRegs[8] & 2);

    /* After an early flip, a second vint in the frame (LN changed) neither taps nor flips. */
    if (vdp == theVdp && !vdp->flippedEarly && emulatorIsEmuThread()) {
        vdpAtShown = 1;
        emulatorTap(EMU_TAP_VDP_SHOWN);
        vdpAtShown = 0;
        emulatorShownDisplayEnded(vdp->videoEnabled && canFlipFrameBuffer >= 2,
                                  !videoManagerActivePacesScreen() && frameBufferGetActiveDraw() == vdp->frameBuffer,
                                  showEarly, vdp);
    }
}

static void onDrawAreaEnd(VDP* vdp, UInt32 time)
{
    sync(vdp, time);

    vdp->timeDrawAreaEndEn = 0;
    vdp->drawArea = 0;
    vdp->cmdDrawArea = 0;
}

static void onTmsVint(VDP* vdp, UInt32 time)
{
    if (vdp->vdpRegs[1] & 0x20) {
        boardSetInt(INT_IE0);
    }
}

static void onVStart(VDP* vdp, UInt32 time)
{
    sync(vdp, time);

    vdp->timeVStartEn = 0;
//    vdp->lineOffset = -1;
    vdp->vdpStatus[2] &= ~0x40;

    vdp->cmdDrawArea = 1;
    vdpSetTimingMode(vdp->cmdEngine, ((vdp->vdpRegs[1] >> 6) & vdp->cmdDrawArea) | (vdp->vdpRegs[8] & 2));
}

static void onDrawAreaStart(VDP* vdp, UInt32 time)
{
    sync(vdp, time);
    
    vdp->timeDrawAreaStartEn = 0;

    vdp->drawArea = 1;
    vdp->cmdDrawArea = 1;
    vdp->vdpStatus[2] &= ~0x40;
    vdpSetTimingMode(vdp->cmdEngine, ((vdp->vdpRegs[1] >> 6) & vdp->cmdDrawArea) | (vdp->vdpRegs[8] & 2));
}

static UInt32 frameStartTime;
static int    timeDisplay;

int getScreenCompletePercent() 
{
    return 100 * (boardSystemTime() - frameStartTime) / timeDisplay;
}

static void onDisplay(VDP* vdp, UInt32 time)
{
    int isPal = vdpIsVideoPal(vdp); 
    int early;
    
    sync(vdp, time);

    vdp->timeDisplayEn = 0;
    early = vdp->flippedEarly;
    vdp->flippedEarly  = 0;
    vdp->lastFlipEarly = early;
    /* A soft reset runs this on the UI thread; that is no frame begun. */
    if (vdp == theVdp && emulatorIsEmuThread()) {
        vdp->machineFrame++;
    }

    boardCheckVdpBoostKill((UInt32)vdp->vdpRegs[23]              |
                           ((UInt32)vdp->vdpRegs[26] <<  8)      |
                           ((UInt32)vdp->vdpRegs[27] << 16));

    if (vdp->videoEnabled) {
        FrameBuffer* frameBuffer;
        if (early) {
            frameBuffer = frameBufferGetDrawFrame();
        }
        else if (emulatorRewindReplaying()) {
            /* Shown only at its end of active display; a frame begun after the load is whole. */
            frameBuffer = frameBufferGetDrawFrame();
            canFlipFrameBuffer = 2;
        }
        else if (canFlipFrameBuffer >= 2) {
            frameBuffer = frameBufferFlipDrawFrame();
        }
        else {
            frameBuffer = frameBufferGetDrawFrame();
            canFlipFrameBuffer++;
        }
        frameBufferSetLineCount(frameBuffer, 240);
        {
            // Reg 9 IL,!EO (mode 1) = single page on interlaced raster: treat
            // as NONE + interlaceRaster=1 (no phantom field, no scanlines);
            // only IL+EO+vram128 (mode 3) is true ODD/EVEN alternation.
            int il = vdpIsInterlaceOn(vdp->vdpRegs) ? 1 : 0;
            int eo = (vdp->vdpRegs[9] & 0x04) ? 1 : 0;
            frameBuffer->interlaceRaster = il;
            if (il && eo && vdp->vram128) {
                frameBufferSetInterlace(frameBuffer,
                    (vdp->vdpStatus[2] & 0x02) ? INTERLACE_EVEN : INTERLACE_ODD);
            }
            else {
                frameBufferSetInterlace(frameBuffer, INTERLACE_NONE);
            }
        }
    }

    refreshRate = isPal ? 50 : 60; // Update global refresh rate

    vdp->scr0splitLine = 0;
    vdp->curLine = 0;
    /* Line 0 is blanked whatever the last frame did, and rescheduling can drop
    ** the timer that would have cleared this, so clear it here. */
    vdp->cmdDrawArea = 0;
    vdp->VAdjust = (-((Int8)(vdp->vdpRegs[18]) >> 4));

    vdp->lastLine = isPal ? 313 : 262;
    vdp->displayOffest = isPal ? 27 : 0;
    vdp->firstLine = vdp->displayOffest + (vdpIsScanLines212(vdp->vdpRegs) ? 14 : 24) + vdp->VAdjust;
    if (!(vdp->vdpRegs[0] & 0x10)) {
        boardClearInt(INT_IE1);
    }
    vdp->vdpStatus[2] ^= 0x02;
    RefreshScreen(vdp->screenMode, early,
                  vdp == theVdp && vdp->videoEnabled && canFlipFrameBuffer >= 2 &&
                  frameBufferGetActiveDraw() == vdp->frameBuffer);

    /* Per-frame (VSYNC) blink clock. In line-blink mode the phase is derived
    ** per scan line instead; advance its base by one frame of lines so the
    ** bands drift by (lines mod period) each frame. */
    {
        int r13    = vdp->vdpRegs[13];
        int period = ((r13 & 0x0f) + (r13 >> 4)) * 10;
        if ((vdp->vdpRegs[1] & 0x04) && period > 0) {
            vdp->blinkLineBase = (vdp->blinkLineBase + vdp->lastLine) % period;
        }
        else {
            vdpBlink(vdp);
        }
    }

    vdp->frameStartTime = vdp->timeDisplay;
    vdp->timeDisplay += HPERIOD * vdp->lastLine;
    vdp->timeDisplayEn = 1;
    boardTimerAdd(vdp->timerDisplay, vdp->timeDisplay);

    scheduleDrawAreaStart(vdp);
    scheduleHint(vdp);
    scheduleVint(vdp);

    frameStartTime = vdp->frameStartTime;
    timeDisplay    = HPERIOD * vdp->lastLine;

    if (vdp->vdpRegs[0] & 0x40) {
        if (vdp->screenMode >= 5 && vdp->screenMode <= 12) {
            digitize(vdp);
        }
    }
}

static void simulateVramDecay(VDP* vdp) 
{
    int time = (boardSystemTime() - vdp->screenOffTime) / 1350000;
    int i;
    if (time >= 24) {
        for (i = 0x0000; i < 0x3000; i += 2) {
            vdp->vramPtr[i]     = 0x55;
            vdp->vramPtr[i + 1] = 0xaa;
        }
    }
    else if (time >= 8) {
        static UInt32 rnd = 0xfffffff3;
        static UInt8 v[32] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
        UInt8* vPtr = v + time - 8;
        time -= 8;
        for (i = 0x0000; i < 0x3000; i += 2) {
            int j = 8;
            UInt8 val = 0;
            while (j--) {
                val <<= 1;
                val |= vPtr[(rnd *= 17) >> 28];
            }
            vdp->vramPtr[i] |= val & 0x55;
            vdp->vramPtr[i] &= ~(val & 0xaa);
            vdp->vramPtr[i + 1] |= val & 0xaa;
            vdp->vramPtr[i + 1] &= ~(val & 0x55);
        }
    }
}

static int updateScreenMode(VDP* vdp) {
    int screenMode;
    switch (((vdp->vdpRegs[0] & 0x0e) >> 1) | (vdp->vdpRegs[1] & 0x18)) {
    case 0x10: screenMode = 0; break;
    case 0x00: screenMode = 1; break;
    case 0x01: screenMode = 2; break;
    case 0x08: screenMode = 3; break;
    case 0x02: screenMode = 4; break;
    case 0x03: screenMode = 5; break;
    case 0x04: screenMode = 6; break;
    case 0x05: screenMode = 7; break;
    case 0x07: screenMode = 8; break;
    case 0x12: screenMode = 13; break;
    case 0x11:  // Screen 0 + 2
        screenMode = 16;
        break;
    case 0x18: // Screen 0 + 3
    case 0x19: // Screen 0 + 2 + 3
        screenMode = 32;
        break;
    default: // Unknown screen mode
        screenMode = 64;
        break;
    }

    switch (screenMode) {
    case 0: vdp->RefreshLine = RefreshLine0; break;
    case 1: vdp->RefreshLine = RefreshLine1; break;
    case 2: vdp->RefreshLine = RefreshLine2; break;
    case 3: vdp->RefreshLine = RefreshLine3; break;
    case 4: vdp->RefreshLine = RefreshLine4; break;
    case 5: vdp->RefreshLine = RefreshLine5; break;
    case 6: vdp->RefreshLine = RefreshLine6; break;
    case 7: 
        if(!vdpIsModeYJK(vdp->vdpRegs)) {
            vdp->RefreshLine = RefreshLine7; 
            break;
        }
    case 8: 
        if(screenMode == 8 && !vdpIsModeYJK(vdp->vdpRegs)) {
            vdp->RefreshLine = RefreshLine8; 
        }
        else if (vdpIsModeYAE(vdp->vdpRegs)) {
            vdp->RefreshLine = RefreshLine10; 
            screenMode = 10;
        }
        else {
            vdp->RefreshLine = RefreshLine12; 
            screenMode = 12;
        }
        break;
    case 16:
        screenMode = 0;
        if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
            vdp->RefreshLine = RefreshLine0Plus; 
        }
        else {
            vdp->RefreshLine = RefreshLineBlank; 
        }
        break;
    case 32:
        screenMode = 0;
        if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
            vdp->RefreshLine = RefreshLine0Mix; 
        }
        else {
            vdp->RefreshLine = RefreshLineBlank; 
        }
        break;
    case 13:
        vdp->RefreshLine = RefreshLineTx80; break;
        screenMode = 13;
        break;
    default:
        screenMode = 1;
        vdp->RefreshLine = RefreshLineBlank; 
        break;
    }

    return screenMode;
}

/* YJK screens (10/11/12) follow their base bitmap layout in the command
** engine. R#0 bit 2 (M4) selects it: 0 = G6 (4bit/512), 1 = G7 (8bit/256);
** drive a G6-based YJK screen as SCREEN 7, not SCREEN 8. */
static int cmdEngineScreenMode(VDP* vdp)
{
    int mode = vdp->screenMode & 0x0f;
    if ((mode == 10 || mode == 12) && !(vdp->vdpRegs[0] & 0x04)) {
        mode = 7;
    }
    return mode;
}

static void onScrModeChange(VDP* vdp, UInt32 time)
{
    int scanLine = (boardSystemTime() - vdp->frameStartTime) / HPERIOD;
    int screenMode = vdp->screenMode;
    sync(vdp, time);
    
    vdp->timeScrModeEn = 0;
    
    vdp->screenMode = updateScreenMode(vdp);
    
    vdp->chrTabBase = ((((int)vdp->vdpRegs[2] << 10) & ~((int)(vdp->vdpRegs[25] & 1) << 15)) | ~(-1 << 10)) & vdp->vramMask;
    vdp->chrGenBase = (((int)vdp->vdpRegs[4] << 11) | ~(-1 << 11)) & vdp->vramMask;
    vdp->colTabBase = (((int)vdp->vdpRegs[10] << 14) | ((int)vdp->vdpRegs[3] << 6) | ~(-1 << 6)) & vdp->vramMask;

    vdp->sprTabBase = (((int)vdp->vdpRegs[11] << 15) | ((int)vdp->vdpRegs[5] << 7) | ~(-1 << 7)) & vdp->vramMask;
    vdp->sprGenBase = (((int)vdp->vdpRegs[6] << 11) | ~(-1 << 11)) & vdp->vramMask;

#ifdef ENABLE_VRAM_DECAY
    if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
        if ((vdp->screenOn ^ vdp->vdpRegs[1]) & 0x40) {
            if ((~vdp->vdpRegs[1] & 0x80) && !vdp->screenOn) {
               simulateVramDecay(vdp);
            }
            vdp->screenOffTime = boardSystemTime();
        }
    }
#endif
    vdp->screenOn = vdp->vdpRegs[1] & 0x40;
    
    vdpSetScreenMode(vdp->cmdEngine, cmdEngineScreenMode(vdp), vdp->vdpRegs[25] & 0x40);

    if (screenMode != vdp->screenMode) {
        vdp->scr0splitLine = (scanLine - vdp->firstLine) & ~7;
        /* Colour 0 handling depends on the mode, so refresh it on mode
        ** change too, not only on register writes. */
        updateOutputMode(vdp);
    }

    if (vdp->screenMode == 0 || vdp->screenMode == 13) {
        vdp->displayArea = 960;
        vdp->leftBorder = 102 + 92;
    }
    else {
        vdp->displayArea = 1024;
        vdp->leftBorder = 102 + 56;
    }

    vdp->HAdjust = (-((Int8)(vdp->vdpRegs[18] << 4) >> 4));
	if (vdp->vdpRegs[25] & 0x08) {
		vdp->HAdjust += 4;
	}
    vdp->leftBorder += vdp->HAdjust;
}

/* R#26/R#27 written part way through a scan line. The renderers read the
** scroll once, when the line starts, so lay the line out again with the new
** value and put back the pixels that were already on screen. */
static void rescrollLine(VDP* vdp)
{
    Pixel  saved[FB_MAX_LINE_WIDTH];
    Pixel* start;
    int drawn;

    if (vdp->lineOffset < 0 || vdp->lineOffset > 32) {
        return;
    }
    if (vdp->curLine < vdp->displayOffest || vdp->curLine >= vdp->displayOffest + SCREEN_HEIGHT) {
        return;
    }
    if (!vdp->videoEnabled || !displayEnable || frameBufferGetDrawFrame() == NULL) {
        return;
    }

    start = RefreshLineStart(vdp->curLine);
    drawn = RefreshLineDrawn(vdp);
    if (start == NULL || drawn < 0 || drawn > FB_MAX_LINE_WIDTH) {
        return;
    }
    if (drawn > 0) {
        memcpy(saved, start, drawn * sizeof(Pixel));
    }

    vdp->reScroll = 1;
    vdp->RefreshLine(vdp, vdp->curLine, -1, vdp->lineOffset);
    vdp->reScroll = 0;

    if (drawn > 0) {
        memcpy(start, saved, drawn * sizeof(Pixel));
    }
}

static void vdpUpdateRegisters(VDP* vdp, UInt8 reg, UInt8 value)
{ 
    UInt8 change;

    reg   &= vdp->registerMask;
    value &= vdp->registerValueMask[reg];

    if (vdpIsV9968(vdp) && vdp->extRegsLocked && (reg == 20 || reg == 21)) {
        return;
    }

    sync(vdp, boardSystemTime());

//    if (reg == 0 || reg == 1 || reg == 5 || reg == 6 || reg == 8 || reg == 9 || reg == 11 || reg == 23)
//        printf("W %.2x: 0x%.2x\t%d\n", reg, value, boardSystemTime());

#if 0
    if (reg == 0 || reg == 8 || reg == 9)
        printf("W %.2x: 0x%.2x\n", reg, value);
#endif
//    printf("W %.2x: 0x%.2x\n", reg, value);

    change = vdp->vdpRegs[reg] ^ value;
    vdp->vdpRegs[reg] = value;
    if (vdpRasterArmed) vdpRasterNoteReg(vdp, reg, value ^ change, value);

    if (reg >= 0x20) {
        if (reg == 0x2d && (change & 0x40)) {
            if (!vdpIsV9968Native(vdp)) {
                vdp->vramPtr    = vdp->vram + vdp->vramOffsets[(value >> 6) & 1];
                vdp->vramEnable = vdp->vram192 || !((value >> 6) & 1);
            }
            vdpUpdateVramMode(vdp);
        }
        vdpCmdWrite(vdp->cmdEngine, reg - 0x20, value, boardSystemTime());
        return;
    }

    switch (reg) {
    case 0: 
        if (!(value & 0x10)) {
            boardClearInt(INT_IE1);
        }

        if (change & 0x0e) {
            scheduleScrModeChange(vdp);
        }
        
        if (change & 0x40) {
             updateOutputMode(vdp);
        }

        break;

    case 1: 
        if (vdp->vdpStatus[0] & 0x80) {
			if (value & 0x20) {
				boardSetInt(INT_IE0);
			}
			else {
				boardClearInt(INT_IE0);
			}
        }

        if (change & 0x58) {
            scheduleScrModeChange(vdp);
        }
        
        vdpSetTimingMode(vdp->cmdEngine, ((value >> 6) & vdp->cmdDrawArea) | (vdp->vdpRegs[8] & 2));
        break;

    case 2: 
        vdp->chrTabBase = ((((int)vdp->vdpRegs[2] << 10) & ~((int)(vdp->vdpRegs[25] & 1) << 15)) | ~(-1 << 10)) & vdp->vramMask;
        break;

    case 3: 
        vdp->colTabBase = (((int)vdp->vdpRegs[10] << 14) | ((int)value << 6) | ~(-1 << 6)) & vdp->vramMask;
        break;

    case 4: 
        vdp->chrGenBase = (((int)value << 11) | ~(-1 << 11)) & vdp->vramMask;
        break;

    case 5: 
        vdp->sprTabBase = ((vdp->vdpRegs[11] << 15) | (value << 7) | ~(-1 << 7)) & ((vdp->vramPages << 14) - 1);
        break;

    case 6: 
    	vdp->sprGenBase = ((value << 11) | ~(-1 << 11)) & ((vdp->vramPages << 14) - 1);
        break;

    case 7: 
        vdp->FGColor = value >> 4;
        vdp->BGColor = value & 0x0F;
        updateOutputMode(vdp);
        break;

    case 8:
        vdpUpdateVramMode(vdp);
        vdpSetTimingMode(vdp->cmdEngine, ((vdp->vdpRegs[1] >> 6) & vdp->cmdDrawArea) | (value & 2));
        if (change & 0xb0) {
            updateOutputMode(vdp);
        }
        break;

    case 9:
        value = (value & vdp->palMask) | vdp->palValue;
        if (change & 0x80) {
            scheduleVint(vdp);
        }
        if (change & 0x30) {
            updateOutputMode(vdp);
        }
        break;

    case 10: 
        vdp->colTabBase = (((int)value << 14) | ((int)vdp->vdpRegs[3] << 6) | ~(-1 << 6)) & vdp->vramMask;
        break;

    case 11: 
//        if (change & 0x03) {
//            vdp->vdpStatus[0] &= ~0x40;
//        }
        vdp->sprTabBase = ((value << 15) | (vdp->vdpRegs[5] << 7) | ~(-1 << 7)) & ((vdp->vramPages << 14) - 1);
        break;

    case 13:
        {
            /* Reload the blink counter on R#13 write. High nibble = ON period
            ** (alternate R#12 colors), low = OFF (normal R#7); restart at the ON
            ** phase unless it is zero, which pins the normal colors. */
            int onTime  = (value >> 4)   * 10;   /* high nibble: alternate/ON  */
            int offTime = (value & 0x0f) * 10;   /* low  nibble: normal/OFF    */
            vdp->blinkLineBase = 0;
            if (onTime && offTime) {
                vdp->blinkFlag = 0;              /* start showing R#12 colors  */
                vdp->blinkCnt  = onTime;
            }
            else {
                vdp->blinkFlag = onTime == 0;
                vdp->blinkCnt  = 0;
            }
            if (vdp->blinkFlag) {
                vdp->XFGColor = vdp->FGColor;
                vdp->XBGColor = vdp->BGColor;
            }
            else {
                vdp->XFGColor = vdp->vdpRegs[12] >> 4;
                vdp->XBGColor = vdp->vdpRegs[12] & 0x0f;
            }
        }
        break;

    case 14:
        if (!vdpIsV9968Native(vdp)) {
            vdp->vdpRegs[14] &= 0x07;
        }
        value = vdp->vdpRegs[14] & (vdp->vramPages - 1);
        vdp->vramPage = (int)value << 14; 
        if (vdp->vram16) {
            vdp->vramEnable = value == 0;
        }
        break;

    case 16:
        vdp->palKey = 0;
        break;

    case 18:
        if (change) {
            scheduleScrModeChange(vdp);
        }
        break;

    case 19:
        /* Writing R#19 only sets the compare line; it must not acknowledge a
        ** pending line interrupt (only reading S#1 does). */
        if (change) {
            scheduleHint(vdp);
        }
        break;

    case 23:
        if (change) {
            scheduleHint(vdp);
            spriteLineInvalidate(vdp, (boardSystemTime() - vdp->frameStartTime) / HPERIOD);
        }
        if (!(vdp->vdpRegs[0] & 0x10)) {
            boardClearInt(INT_IE1);
        }
        break;

    case 21:
        /* S#1 bits 5-1 are the id, bit0 is FH and must survive. */
        if ((change & 0x01) && vdpIsV9968(vdp)) {
            vdp->vdpStatus[1] = (vdp->vdpStatus[1] & ~0x3e) | (vdpIsV9968Native(vdp) ? 0x06 : 0x04);
            vdpUpdateVramMode(vdp);
        }
        break;

    case 25:
        if (change) {
            scheduleScrModeChange(vdp);
        }
        break;

    case 26:
    case 27:
        if (change) {
            rescrollLine(vdp);
        }
        break;

    default:
        break;
    }
}

static UInt8 peek(VDP* vdp, UInt16 ioPort) 
{
    if (vdpIsV99x8(vdp)) {
        vdpCmdExecute(vdp->cmdEngine, boardSystemTime());
    }

	return vdp->vdpData;
}

static UInt8 readNoTimingCheck(VDP* vdp, UInt16 ioPort) 
{
    UInt8 value;

    if (vdpIsV99x8(vdp)) {
        vdpCmdExecute(vdp->cmdEngine, boardSystemTime());
        vdpCmdStealAccessSlot(vdp->cmdEngine);
    }

	value = vdp->vdpData;
    vdp->vdpData = vdp->vramEnable ? *MAP_VRAM(vdp, (vdp->vdpRegs[14] << 14) | vdp->vramAddress) : 0xff;
	vdp->vramAddress = (vdp->vramAddress + 1) & 0x3fff;
    if (vdp->vramAddress == 0 && vdp->screenMode > 3) {
        vdp->vdpRegs[14] = (vdp->vdpRegs[14] + 1) & ((vdp->vramPages - 1) & (vdpIsV9968Native(vdp) ? 0x0f : 0x07));
    }
	vdp->vdpKey = 0;

    return value;
}

static UInt8 read(VDP* vdp, UInt16 ioPort) 
{
    if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
        checkVramAccessTimeTms(vdp);
    }

    return readNoTimingCheck(vdp, ioPort);
}

static UInt8 peekStatus(VDP* vdp, UInt16 ioPort)
{
    UInt8 vdpStatus;

    sync(vdp, boardSystemTime());
    if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
        return vdp->vdpStatus[0];
    }
    vdpStatus = vdp->vdpStatus[vdp->vdpRegs[15]];

    switch(vdp->vdpRegs[15]) {
    case 1: 
        if (vdp->vdpRegs[0] & 0x10) {
            if (boardGetInt(INT_IE1)) {
                vdpStatus |=  0x01;
            }
        }
        else {
            if (boardSystemTime() - vdp->timeHint < HPERIOD - vdp->displayArea) {
                vdpStatus |= 0x01;
            }
        }
        break;

    case 2:
        {
            UInt32 frameTime = boardSystemTime() - vdp->frameStartTime;
            vdpStatus |= 0x40 | 0x20 | vdpGetStatus(vdp->cmdEngine);
            if (vdp->drawArea || (frameTime - ((vdp->firstLine - 1) * HPERIOD + vdp->leftBorder - 10) < 4 * HPERIOD)) {
                vdpStatus &= ~0x40;
            }
            if (frameTime % HPERIOD - vdp->leftBorder - 30 < (UInt32)vdp->displayArea + 30) {
                vdpStatus &= ~0x20;
            }
        }
        break;

    case 4:
        vdpStatus |= 0xfe;
        break;

    case 6:
        vdpStatus |= 0xfc;
        break;

    case 7: 
        vdpStatus = vdpGetColor(vdp->cmdEngine);
        break;

    case 8: 
        vdpStatus = (UInt8)vdpGetBorderX(vdp->cmdEngine);
        break;

    case 9: 
        vdpStatus = (UInt8)(vdpGetBorderX(vdp->cmdEngine) >> 8);
        break;
    }

    return vdpStatus;
}

static UInt8 readStatus(VDP* vdp, UInt16 ioPort)
{
    UInt8 vdpStatus;

    sync(vdp, boardSystemTime());

    vdp->vdpKey = 0;

    if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
        vdpStatus = vdp->vdpStatus[0];
        vdp->vdpStatus[0] &= 0x1f;
        boardClearInt(INT_IE0);
        return vdpStatus;
    }
    
    vdpStatus = vdp->vdpStatus[vdp->vdpRegs[15]];
   
    switch(vdp->vdpRegs[15]) {
    case 0: 
//        printf("I %.2x: 0x%.2x\t%d\n", vdp->vdpRegs[15], vdpStatus, boardSystemTime());
        vdp->vdpStatus[0] &= 0x1f;
        boardClearInt(INT_IE0);
        break;

    case 1: 
        if (vdp->vdpRegs[0] & 0x10) {
            if (boardGetInt(INT_IE1)) {
                boardClearInt(INT_IE1);
                vdpStatus |=  0x01;
            }
        }
        else {
            if (boardSystemTime() - vdp->timeHint < HPERIOD - vdp->displayArea) {
                vdpStatus |= 0x01;
            }
        }
        break;

    case 2:
        {
            UInt32 frameTime = boardSystemTime() - vdp->frameStartTime;
            vdpStatus |= 0x40 | 0x20 | vdpGetStatus(vdp->cmdEngine);
            if (vdp->drawArea || (frameTime - ((vdp->firstLine - 1) * HPERIOD + vdp->leftBorder - 10) < 4 * HPERIOD)) {
                vdpStatus &= ~0x40;
            }
            if (frameTime % HPERIOD - vdp->leftBorder - 30 < (UInt32)vdp->displayArea + 30) {
                vdpStatus &= ~0x20;
            }
        }
        break;

    case 4:
        vdpStatus |= 0xfe;
        break;

    case 5:
        // Reading S#5 resets the latched sprite collision coordinates.
        vdp->vdpStatus[3] = 0;
        vdp->vdpStatus[4] = 0;
        vdp->vdpStatus[5] = 0;
        vdp->vdpStatus[6] = 0;
        break;

    case 6:
        vdpStatus |= 0xfc;
        break;

    case 7: 
        vdpStatus = vdpGetColor(vdp->cmdEngine);
        break;

    case 8: 
        vdpStatus = (UInt8)vdpGetBorderX(vdp->cmdEngine);
        break;

    case 9: 
        vdpStatus = (UInt8)(vdpGetBorderX(vdp->cmdEngine) >> 8);
        break;
    }

    return vdpStatus;
}

static UInt8 peekVram(VDP* vdp, int address) {
    return vdp->vram[address];
}
                  
static void write(VDP* vdp, UInt16 ioPort, UInt8 value)
{
    sync(vdp, boardSystemTime());

    if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
        checkVramAccessTimeTms(vdp);
    }
    if (vdpIsV99x8(vdp)) {
        vdpCmdStealAccessSlot(vdp->cmdEngine);
    }

    if (vdp->vramEnable) {
        int index = MAP_VRAMINDEX(vdp, (vdp->vdpRegs[14] << 14) | vdp->vramAddress);
        if (!(index & ~vdp->vramAccMask)) {
            vdp->vram[index] = value;

            tryWatchpoint(DBGTYPE_VIDEO, index, value, vdp, peekVram);
//        printf("W(0x%.4x): %.2x\n", (vdp->vdpRegs[14] << 14) | vdp->vramAddress, value);
//            *MAP_VRAM(vdp, (vdp->vdpRegs[14] << 14) | vdp->vramAddress) = value;
        }
    }
	vdp->vdpData = value;
	vdp->vdpKey = 0;
    vdp->vramAddress = (vdp->vramAddress + 1) & 0x3fff;
    if (vdp->vramAddress == 0 && vdp->screenMode > 3) {
        vdp->vdpRegs[14] = (vdp->vdpRegs[14] + 1) & ((vdp->vramPages - 1) & (vdpIsV9968Native(vdp) ? 0x0f : 0x07));
    }
    if (!vdp->videoEnabled && boardGetVideoAutodetect() && videoManagerGetCount() > 1) {
        videoManagerActivate(vdp->videoHandle);
    }
}

static void writeLatch(VDP* vdp, UInt16 ioPort, UInt8 value)
{
	switch (vdp->vdpVersion) {
		
		/* MSX1 VDP: address always updates */
        case VDP_TMS9929A: case VDP_TMS99x8A:
        case VDP_TMS9918A:
			if (vdp->vdpKey) {
				vdp->vramAddress = ((UInt16)value << 8 | (vdp->vramAddress & 0xff)) & 0x3fff;
				if (!(value & 0x40)) {
					if (value & 0x80) vdpUpdateRegisters(vdp, value, vdp->vdpDataLatch);
					else readNoTimingCheck(vdp, ioPort);
				}
				vdp->vdpKey = 0;
			}
			else {
				vdp->vramAddress = (vdp->vramAddress & 0x3f00) | value;
				vdp->vdpDataLatch = value;
				vdp->vdpKey = 1;
			}
			
			break;
		
		default:
			if (vdp->vdpKey) {
				if (value & 0x80) {
					if (!(value & 0x40)) vdpUpdateRegisters(vdp, value, vdp->vdpDataLatch);
				} 
				else {
					vdp->vramAddress = ((UInt16)value << 8 | vdp->vdpDataLatch) & 0x3fff;
					if (!(value & 0x40)) readNoTimingCheck(vdp, ioPort);
				}
				vdp->vdpKey = 0;
			}
			else {
				vdp->vdpDataLatch = value;
				vdp->vdpKey = 1;
			}
			
			break;
	}
}

static void initPalette(VDP* vdp)
{
    int i;
    int y;
    int J;
    int K;

    for (y = 0; y < 32; y++) {
        for (J = 0; J < 64; J++) {
            for (K = 0; K < 64; K++) {
		        int j = (J & 0x1f) - (J & 0x20);
		        int k = (K & 0x1f) - (K & 0x20);
			    int r = 255 * (y + j) / 31;
			    int g = 255 * (y + k) / 31;
			    int b = 255 * ((5 * y - 2 * j - k) / 4) / 31;

                r = MIN(255, MAX(0, r));
                g = MIN(255, MAX(0, g));
                b = MIN(255, MAX(0, b));
                vdp->yjkColor[y][J][K] = videoGetColor(r, g, b);
            }
        }
    }

    for (i = 0; i < 256; i++) {
        vdp->paletteFixed[i] = videoGetColor(255 * ((i >> 2) & 7) / 7, 
                                           255 * ((i >> 5) & 7) / 7, 
                                           255 * ((i & 3) == 3 ? 7 : 2 * (i & 3)) / 7);
    }

    vdp->paletteSprite8[0]  = videoGetColor(0 * 255 / 7, 0 * 255 / 7, 0 * 255 / 7);
    vdp->paletteSprite8[1]  = videoGetColor(0 * 255 / 7, 0 * 255 / 7, 2 * 255 / 7);
    vdp->paletteSprite8[2]  = videoGetColor(3 * 255 / 7, 0 * 255 / 7, 0 * 255 / 7);
    vdp->paletteSprite8[3]  = videoGetColor(3 * 255 / 7, 0 * 255 / 7, 2 * 255 / 7);
    vdp->paletteSprite8[4]  = videoGetColor(0 * 255 / 7, 3 * 255 / 7, 0 * 255 / 7);
    vdp->paletteSprite8[5]  = videoGetColor(0 * 255 / 7, 3 * 255 / 7, 2 * 255 / 7);
    vdp->paletteSprite8[6]  = videoGetColor(3 * 255 / 7, 3 * 255 / 7, 0 * 255 / 7);
    vdp->paletteSprite8[7]  = videoGetColor(3 * 255 / 7, 3 * 255 / 7, 2 * 255 / 7);
    vdp->paletteSprite8[8]  = videoGetColor(7 * 255 / 7, 4 * 255 / 7, 2 * 255 / 7);
    vdp->paletteSprite8[9]  = videoGetColor(0 * 255 / 7, 0 * 255 / 7, 7 * 255 / 7);
    vdp->paletteSprite8[10] = videoGetColor(7 * 255 / 7, 0 * 255 / 7, 0 * 255 / 7);
    vdp->paletteSprite8[11] = videoGetColor(7 * 255 / 7, 0 * 255 / 7, 7 * 255 / 7);
    vdp->paletteSprite8[12] = videoGetColor(0 * 255 / 7, 7 * 255 / 7, 0 * 255 / 7);
    vdp->paletteSprite8[13] = videoGetColor(0 * 255 / 7, 7 * 255 / 7, 7 * 255 / 7);
    vdp->paletteSprite8[14] = videoGetColor(7 * 255 / 7, 7 * 255 / 7, 0 * 255 / 7);
    vdp->paletteSprite8[15] = videoGetColor(7 * 255 / 7, 7 * 255 / 7, 7 * 255 / 7);
}

static void digitize(VDP* vdp)
{
    UInt8 colorMask = vdp->vdpRegs[7];
    int yDelta = 14 + vdp->VAdjust;
    int x, y;

    vdpDaDevice.callbacks.daStart(vdpDaDevice.ref, vdpIsOddPage(vdp));

#define videoDaGet(sm, x, y, pal, cnt) vdpDaDevice.callbacks.daRead(vdpDaDevice.ref, sm, x, y, pal, cnt)

    for (y = 0; y < 212; y++) {
        UInt8* charTable = vdp->vram + (vdp->chrTabBase & (~vdpIsOddPage(vdp) << 7) & ((-1 << 15) | ((y + vdpVScroll(vdp)) << 7)));

        switch (vdp->screenMode) {
        case 5:
            for (x = 0; x < 128; x++) {
                charTable[x] = ((videoDaGet(vdp->screenMode, 4 * x + 0, y + yDelta, vdp->palette, 16) & colorMask) << 4) |
                               ((videoDaGet(vdp->screenMode, 4 * x + 2, y + yDelta, vdp->palette, 16) & colorMask) << 0);
            }
            break;
        case 6:
            for (x = 0; x < 128; x++) {
                charTable[x] = ((((videoDaGet(vdp->screenMode, 4 * x + 0, y + yDelta, vdp->palette, 4) << 2) | 
                                  (videoDaGet(vdp->screenMode, 4 * x + 1, y + yDelta, vdp->palette, 4) << 0)) & colorMask) << 4) |
                               ((((videoDaGet(vdp->screenMode, 4 * x + 2, y + yDelta, vdp->palette, 4) << 2) | 
                                  (videoDaGet(vdp->screenMode, 4 * x + 3, y + yDelta, vdp->palette, 4) << 0)) & colorMask) << 0);
            }
            break;
        case 7:
            for (x = 0; x < 128; x++) {
                charTable[x] =                ((videoDaGet(vdp->screenMode, 4 * x + 0, y + yDelta, vdp->palette, 16) & colorMask) << 4) |
                                              ((videoDaGet(vdp->screenMode, 4 * x + 1, y + yDelta, vdp->palette, 16) & colorMask) << 0);
                charTable[x + vdp->vram128] = ((videoDaGet(vdp->screenMode, 4 * x + 2, y + yDelta, vdp->palette, 16) & colorMask) << 4) |
                                              ((videoDaGet(vdp->screenMode, 4 * x + 3, y + yDelta, vdp->palette, 16) & colorMask) << 0);
            }
            break;
        case 8:
        case 10:
        case 11:
        case 12:
            for (x = 0; x < 128; x++) {
                charTable[x]                = videoDaGet(vdp->screenMode, 4 * x + 0, y + yDelta, NULL, 0) & colorMask;
                charTable[x + vdp->vram128] = videoDaGet(vdp->screenMode, 4 * x + 2, y + yDelta, NULL, 0) & colorMask;
            }
        }
    }
    
    vdpDaDevice.callbacks.daEnd(vdpDaDevice.ref);
}

static void updateOutputMode(VDP* vdp)
{
    int mode = (vdp->vdpRegs[9] >> 4) & 3;
    /* Text 1/2 and Multicolor ignore TP: colour 0 is always transparent
    ** there (measured on a V9958). */
    int textOrMulti  = vdp->screenMode == 0 || vdp->screenMode == 3 || vdp->screenMode == 13;
    int transparency = textOrMulti ||
                       ((vdp->screenMode < 8 || vdp->screenMode > 12) && (vdp->vdpRegs[8] & 0x20) == 0);

    if (mode == 2 || 
        (!(vdp->vdpRegs[8] & 0x80) && (vdp->vdpRegs[8] & 0x10)) || (vdp->vdpRegs[0] & 0x40)) 
    {
        if (vdp->screenMode >=5 && vdp->screenMode <= 12) {
            videoManagerSetMode(vdp->videoHandle, VIDEO_EXTERNAL, vdpDaDevice.videoModeMask);
        }
        else {
            videoManagerSetMode(vdp->videoHandle, VIDEO_INTERNAL, vdpDaDevice.videoModeMask);
        }
    }
    else if (mode == 1 && transparency) {
        vdp->palette[0] = videoGetTransparentColor();
        videoManagerSetMode(vdp->videoHandle, VIDEO_MIX, vdpDaDevice.videoModeMask);
    }
    else {
        if (vdp->BGColor == 0 || !transparency || vdp->screenMode == 6) {
            /* SCREEN 6 (G5): R#7 is two 2-bit fields, not a 16-colour index,
            ** so keep the true colour 0 rather than palette[R#7 & 0x0F]. */
            vdp->palette[0] = vdp->palette0;
        }
        else {
            vdp->palette[0] = vdp->palette[vdp->BGColor];
        }
        videoManagerSetMode(vdp->videoHandle, VIDEO_INTERNAL, vdpDaDevice.videoModeMask);
    }
}

static void updatePalette(VDP* vdp, int palEntry, int r, int g, int b)
{
    Pixel color = videoGetColor(r, g, b);
    if (palEntry == 0) {
        vdp->palette0 = color;
        updateOutputMode(vdp);
    }
    else {
        vdp->palette[palEntry] = color;
        if (palEntry == vdp->BGColor) {
            updateOutputMode(vdp);
        }
    }
    vdp->paletteExt[palEntry & 0x0f] = color;
}

/* r, g and b are 5 bit. Entries 0-15 are the ones the 16 colour modes read. */
static void updateExtPalette(VDP* vdp, int palEntry, int r, int g, int b)
{
    palEntry &= 0xff;

    if (palEntry < 16) {
        vdp->paletteReg[palEntry] = ((g >> 2) << 8) | ((r >> 2) << 4) | (b >> 2);
        updatePalette(vdp, palEntry, 255 * r / 31, 255 * g / 31, 255 * b / 31);
    }
    else {
        vdp->paletteExt[palEntry] = videoGetColor(255 * r / 31, 255 * g / 31, 255 * b / 31);
    }
    vdp->paletteExtReg[palEntry] = (UInt16)((r << 10) | (g << 5) | b);
}

/* A 9 bit write also sets the 5 bit copy EPAL reads, as at reset. */
static void notePaletteReg(VDP* vdp, int palEntry)
{
    int reg = vdp->paletteReg[palEntry];
    int r = (reg >> 4) & 7, g = (reg >> 8) & 7, b = reg & 7;

    if (vdpIsV9968(vdp)) {
        vdp->paletteExtReg[palEntry] = (UInt16)((((r << 2) | (r >> 1)) << 10) |
                                                (((g << 2) | (g >> 1)) <<  5) | ((b << 2) | (b >> 1)));
    }
}

static void writePaletteLatch(VDP* vdp, UInt16 ioPort, UInt8 value)
{
    int palEntry = vdp->vdpRegs[16];

    if (vdpIsExtPalette(vdp)) {
        value &= 0x1f;
        if (vdp->palKey == 0) {
            vdp->vdpDataLatch = value;
            vdp->palKey = 1;
            return;
        }
        if (vdp->palKey == 1) {
            vdp->paletteLatch = value;
            vdp->palKey = 2;
            return;
        }
        sync(vdp, boardSystemTime());
        updateExtPalette(vdp, palEntry, vdp->vdpDataLatch, vdp->paletteLatch, value);
        vdp->vdpRegs[16] = (palEntry + 1) & 0xff;
        vdp->palKey = 0;
        return;
    }

    if (vdp->palKey) {
        palEntry &= 0x0f;
        sync(vdp, boardSystemTime());
        vdp->paletteReg[palEntry] = 256 * (value & 0x07) | (vdp->vdpDataLatch & 0x77);
        notePaletteReg(vdp, palEntry);
        updatePalette(vdp, palEntry, (vdp->vdpDataLatch & 0x70) * 255 / 112,
                                     (value & 0x07) * 255 / 7,
                                     (vdp->vdpDataLatch & 0x07) * 255 / 7);
        vdp->vdpRegs[16] = (palEntry + 1) & 0x0f;
        vdp->palKey = 0;
	}
    else {
		vdp->vdpDataLatch = value;
        vdp->palKey = 1;
	}
}

static void writeRegister(VDP* vdp, UInt16 ioPort, UInt8 value)
{
    int reg;

	vdp->vdpDataLatch = value;
    reg = vdp->vdpRegs[17];
    if((reg & 0x3f) != 17) {
        vdpUpdateRegisters(vdp, reg, value);
    }
    if(~reg & 0x80) {
        vdp->vdpRegs[17] = (reg + 1) & 0x3f;
    }
}

/* P#4: bit0 F, bit1 FH, bit7 the R#20/R#21 lock. Bits 0-1 are
** write-one-to-clear, bit7 is a plain load, and reading clears nothing. */
static UInt8 readInterruptStatus(VDP* vdp, UInt16 ioPort)
{
    UInt8 status;

    sync(vdp, boardSystemTime());

    status = vdp->extRegsLocked ? 0x80 : 0x00;
    if (vdp->vdpStatus[0] & 0x80) {
        status |= 0x01;
    }
    if (vdp->vdpRegs[0] & 0x10) {
        if (boardGetInt(INT_IE1)) {
            status |= 0x02;
        }
    }
    else if (boardSystemTime() - vdp->timeHint < HPERIOD - vdp->displayArea) {
        status |= 0x02;
    }

    return status;
}

static void writeInterruptStatus(VDP* vdp, UInt16 ioPort, UInt8 value)
{
    sync(vdp, boardSystemTime());

    if (value & 0x01) {
        vdp->vdpStatus[0] &= ~0x80;
        boardClearInt(INT_IE0);
    }
    if (value & 0x02) {
        boardClearInt(INT_IE1);
    }

    vdp->extRegsLocked = (value >> 7) & 1;
}

void vdpForceSync()
{
    if (theVdp != NULL) {
        sync(theVdp, boardSystemTime());
    }
}

UInt8* vdpDebugGetVram(UInt32* size)
{
    if (theVdp == NULL) {
        if (size != NULL) {
            *size = 0;
        }
        return NULL;
    }
    if (size != NULL) {
        *size = (UInt32)theVdp->vramSize;
    }
    return theVdp->vram;
}

int vdpDebugGetRegisters(UInt8* registers, int max)
{
    int count = (int)sizeof(theVdp->vdpRegs);

    if (theVdp == NULL || registers == NULL || max <= 0) {
        return 0;
    }
    if (count > max) {
        count = max;
    }
    memcpy(registers, theVdp->vdpRegs, (size_t)count);
    return count;
}

int vdpVramInterleaved(int mode)
{
    return VDP_VRAM_INTERLEAVED(mode);
}

int vdpVramSameLayout(int mode, int other)
{
    return VDP_VRAM_INTERLEAVED(mode) == VDP_VRAM_INTERLEAVED(other);
}

int vdpDebugGetScreenMode(void)
{
    return theVdp != NULL ? theVdp->screenMode : -1;
}

int vdpDebugColorsMatch(int first, int second)
{
    return theVdp != NULL && theVdp->palette[first & 0x0f] == theVdp->palette[second & 0x0f];
}

UInt32 vdpGetMachineFrame(void)
{
    return theVdp != NULL ? theVdp->machineFrame : 0;
}

static void sync(VDP* vdp, UInt32 systemTime)
{
    int frameTime = systemTime - vdp->frameStartTime;
    int scanLine = frameTime / HPERIOD;
    int lineTime = frameTime % HPERIOD - (vdp->leftBorder - 20);
    int curLineOffset;

    if (vdpIsV99x8(vdp)) {
        vdpCmdExecute(vdp->cmdEngine, boardSystemTime());
    }

    if (!vdp->videoEnabled || !displayEnable || frameBufferGetDrawFrame() == NULL) {
        return;
    }

    if (vdp->curLine < scanLine) {
        if (vdp->lineOffset <= 32) {
            if (vdp->curLine >= vdp->displayOffest && vdp->curLine < vdp->displayOffest + SCREEN_HEIGHT) {
                if (vdp->vdpRegs[1] & 0x04) vdpLineBlink(vdp);
                vdp->RefreshLine(vdp, vdp->curLine, vdp->lineOffset, 33);
            }
        }
        if (vdpSprSplitArmed && vdp->lineOffset <= 32) sprSplitLineDone(vdp, vdp->lineOffset);
        vdp->lineOffset = -1;
        vdp->curLine++;
        while (vdp->curLine < scanLine) {
            if (vdp->curLine >= vdp->displayOffest && vdp->curLine < vdp->displayOffest + SCREEN_HEIGHT) {
                if (vdp->vdpRegs[1] & 0x04) vdpLineBlink(vdp);
                vdp->RefreshLine(vdp, vdp->curLine, -1, 33);
            }
            if (vdpSprSplitArmed) sprSplitLineDone(vdp, -1);
            vdp->curLine++;
        }
    }

    if (vdp->lineOffset > 32 || lineTime < -1) {
        return;
    }

    curLineOffset = (lineTime + 32) / 32 - 1;
    if (curLineOffset > 33) {
        curLineOffset = 33;
    }

    if (vdp->lineOffset < curLineOffset) {
        if (vdp->curLine >= vdp->displayOffest && vdp->curLine < vdp->displayOffest + SCREEN_HEIGHT) {
            if (vdp->vdpRegs[1] & 0x04) vdpLineBlink(vdp);
            vdp->RefreshLine(vdp, vdp->curLine, vdp->lineOffset, curLineOffset);
        }
        if (vdpSprSplitArmed && curLineOffset == 33) sprSplitLineDone(vdp, vdp->lineOffset);
        if (vdpSprSplitArmed && vdp->lineOffset <= 0 && curLineOffset > 0) sprPaletteDisplayStart(vdp);
        vdp->lineOffset = curLineOffset;
    }
}
#if 0
static void saveState(VDP* vdp)
{
    SaveState* state = saveStateOpenForWrite("vdp");
    char tag[32];
    int index;

    saveStateSet(state, "timeScrMode",         vdp->timeScrMode);
    saveStateSet(state, "timeScrModeEn",       vdp->timeScrModeEn);
    saveStateSet(state, "timeHint",            vdp->timeHint);
    saveStateSet(state, "timeHintEn",          vdp->timeHintEn);
    saveStateSet(state, "timeVint",            vdp->timeVint);
    saveStateSet(state, "timeVintEn",          vdp->timeVintEn);
    saveStateSet(state, "timeDrawAreaStart",   vdp->timeDrawAreaStart);
    saveStateSet(state, "timeDrawAreaStartEn", vdp->timeDrawAreaStartEn);
    saveStateSet(state, "timeVStart",          vdp->timeVStart);
    saveStateSet(state, "timeVStartEn",        vdp->timeVStartEn);
    saveStateSet(state, "timeDisplay",         vdp->timeDisplay);
    saveStateSet(state, "timeDisplayEn",       vdp->timeDisplayEn);
    saveStateSet(state, "timeDrawAreaEnd",         vdp->timeDrawAreaEnd);
    saveStateSet(state, "timeDrawAreaEndEn",   vdp->timeDrawAreaEndEn);
    
//    saveStateSet(state, "timeTmsVint",         vdp->timeTmsVint);

    saveStateSet(state, "frameStartTime",    vdp->frameStartTime);

    saveStateSet(state, "vdpKey",          vdp->vdpKey);
    saveStateSet(state, "vramAddress",     vdp->vramAddress);
    saveStateSet(state, "vdpData",         vdp->vdpData);
    saveStateSet(state, "vdpDataLatch",    vdp->vdpDataLatch);
    saveStateSet(state, "xfgColor",        vdp->XFGColor);
    saveStateSet(state, "xbgColor",        vdp->XBGColor);
    saveStateSet(state, "firstLine",       vdp->firstLine);
    saveStateSet(state, "blinkFlag",       vdp->blinkFlag);
    saveStateSet(state, "blinkCnt",        vdp->blinkCnt);
    saveStateSet(state, "drawArea",        vdp->drawArea);
    saveStateSet(state, "vramPages",       vdp->vramPages);
    saveStateSet(state, "vdpConnector",    vdp->vdpConnector);
    saveStateSet(state, "vdpVersion",      vdp->vdpVersion);
    saveStateSet(state, "leftBorder",      vdp->leftBorder);
    saveStateSet(state, "hRefresh",        vdp->displayArea);

    saveStateSetBuffer(state, "regs", vdp->vdpRegs, sizeof(vdp->vdpRegs));
    saveStateSetBuffer(state, "status", vdp->vdpStatus, sizeof(vdp->vdpStatus));

    saveStateSet(state, "paletteTmp0",        vdp->palette0);
    
    for (index = 0; index < sizeof(vdp->palette) / sizeof(vdp->palette[0]); index++) {
        sprintf(tag, "vdp->palette%d", index);
        saveStateSet(state, tag, vdp->palette[index]);
    }
    
    saveStateSetBuffer(state, "vram", vdp->vram, 0x4000 * vdp->vramPages);

    saveStateClose(state);

    vdpCmdSaveState(vdp->cmdEngine);
}

static void loadState(VDP* vdp)
{
    SaveState* state = saveStateOpenForRead("vdp");
    UInt32 systemTime = boardSystemTime() + 100;
    char tag[32];
    int index;

    vdp->timeScrMode         =      saveStateGet(state, "timeScrMode",         systemTime);
    vdp->timeScrModeEn       =      saveStateGet(state, "timeScrModeEn",       0);
    vdp->timeHint            =      saveStateGet(state, "timeHint",            systemTime);
    vdp->timeHintEn          =      saveStateGet(state, "timeHintEn",          0);
    vdp->timeVint            =      saveStateGet(state, "timeVint",            systemTime);
    vdp->timeVintEn          =      saveStateGet(state, "timeVintEn",          0);
    vdp->timeDrawAreaStart   =      saveStateGet(state, "timeDrawAreaStart",   systemTime);
    vdp->timeDrawAreaStartEn =      saveStateGet(state, "timeDrawAreaStartEn", 0);
    vdp->timeVStart          =      saveStateGet(state, "timeVStart",          systemTime);
    vdp->timeVStartEn        =      saveStateGet(state, "timeVStartEn",        0);
    vdp->timeDisplay         =      saveStateGet(state, "timeDisplay",         systemTime);
    vdp->timeDisplayEn       =      saveStateGet(state, "timeDisplayEn",       0);
    vdp->timeDrawAreaEnd     =      saveStateGet(state, "timeDrawAreaEnd",     systemTime);
    vdp->timeDrawAreaEndEn   =      saveStateGet(state, "timeDrawAreaEndEn",   0);
    
    vdp->frameStartTime      =      saveStateGet(state, "frameStartTime",      systemTime);
//    vdp->timeTmsVint       =      saveStateGet(state, "timeTmsVint",       systemTime);

    vdp->vdpKey         =         saveStateGet(state, "vdpKey",          0);
    vdp->vramAddress    = (UInt16)saveStateGet(state, "vramAddress",     0);
    vdp->vdpData        = (UInt8) saveStateGet(state, "vdpData",         0);
    vdp->vdpDataLatch   = (UInt8) saveStateGet(state, "vdpDataLatch",    0);
    vdp->XFGColor       = (UInt8) saveStateGet(state, "xfgColor",        0);
    vdp->XBGColor       = (UInt8) saveStateGet(state, "xbgColor",        0);
    vdp->firstLine      =         saveStateGet(state, "firstLine",       1);
    vdp->blinkFlag      =         saveStateGet(state, "blinkFlag",       0);
    vdp->blinkCnt       =         saveStateGet(state, "blinkCnt",        0);
    vdp->drawArea       =         saveStateGet(state, "drawArea",        0);
    vdp->vramPages      =         saveStateGet(state, "vramPages",       8);
    vdp->vdpConnector   =         saveStateGet(state, "vdpConnector",    VDP_MSX);
    vdp->vdpVersion     =         saveStateGet(state, "vdpVersion",      0);
    vdp->leftBorder     =         saveStateGet(state, "leftBorder",      200);
    vdp->displayArea       =         saveStateGet(state, "hRefresh",        1024);

    vdp->palette0 = saveStateGet(state, "paletteTmp0",        0);

    vdp->screenOffTime = boardSystemTime();

    saveStateGetBuffer(state, "regs", vdp->vdpRegs, sizeof(vdp->vdpRegs));
    saveStateGetBuffer(state, "status", vdp->vdpStatus, sizeof(vdp->vdpStatus));

    for (index = 0; index < sizeof(vdp->palette) / sizeof(vdp->palette[0]); index++) {
        sprintf(tag, "vdp->palette%d", index);
        vdp->palette[index] = (UInt16)saveStateGet(state, tag, 0);
    }

    saveStateGetBuffer(state, "vram", vdp->vram, 0x4000 * vdp->vramPages);

    saveStateClose(state);

    vdp->vramMask        = (vdp->vramPages << 14) - 1;
    vdp->vram128         = vdp->vramPages >= 8 ? 0x10000 : 0;
    vdp->vramPage        = ((int)vdp->vdpRegs[14] << 14) & (vdp->vramPages - 1);
    vdp->lastLine        = vdpIsVideoPal(vdp) ? 313 : 262;    
    vdp->displayOffest   = vdpIsVideoPal(vdp) ? 27 : 0;  
    vdp->FGColor         = vdp->vdpRegs[7] >> 4;
    vdp->BGColor         = vdp->vdpRegs[7] & 0x0F;
    vdp->lineOffset      = -1;

    switch (vdp->vdpVersion) {
    case VDP_TMS9929A:
    case VDP_TMS99x8A:
    case VDP_TMS9918A:
        vdp->registerValueMask = registerValueMaskMSX1;
        vdp->registerMask      = 0x07;
        break;
    case VDP_V9938:
        vdp->registerValueMask = registerValueMaskMSX2;
        vdp->registerMask      = 0x3f;
        break;
    case VDP_V9958:
        vdp->registerValueMask = registerValueMaskMSX2p;
        vdp->registerMask      = 0x3f;
        break;
    }
    
    vdp->vramPtr        = vdp->vram + vdp->vramOffsets[(vdp->vdpRegs[0x2d] >> 6) & 1];
    vdp->vramAccMask    = vdp->vramAccMask  = vdp->vramMasks[((vdp->vdpRegs[8] & 0x08) >> 2) | (((vdp->vdpRegs[0x2d] >> 6) & 1))];
    vdp->vramEnable     = vdp->vram192 || !((vdp->vdpRegs[0x2d] >> 6) & 1);
    vdp->vramEnable    &= !vdp->vram16 || vdp->vdpRegs[14] == 0;

    vdpCmdLoadState(vdp->cmdEngine);

    if (vdp->timeScrModeEn) {
        boardTimerAdd(vdp->timerScrModeChange, vdp->timeScrMode);
    }
    if (vdp->timeHintEn) {
        boardTimerAdd(vdp->timerHint, vdp->timeHint + 20);
    }
    if (vdp->timeVintEn) {
        boardTimerAdd(vdp->timerVint, vdp->timeVint);
    }
    if (vdp->timeDrawAreaStartEn) {
        boardTimerAdd(vdp->timerDrawAreaStart, vdp->timeDrawAreaStart);
    }
    if (vdp->timeVStartEn) {
        boardTimerAdd(vdp->timerVStart, vdp->timeVStart);
    }
    if (vdp->timeDisplayEn) {
        boardTimerAdd(vdp->timerDisplay, vdp->timeDisplay);
    }

//    if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A) {
//        boardTimerAdd(vdp->timerTmsVint, vdp->timeTmsVint);
//    }

    onScrModeChange(vdp, boardSystemTime());

    canFlipFrameBuffer = 0;
}

#else


static void saveState(VDP* vdp)
{
    SaveState* state = saveStateOpenForWrite("vdp");
    char tag[32];
    int i;

    saveStateSet(state, "scr0splitLine",         vdp->scr0splitLine);

    saveStateSet(state, "sprGenBase",         vdp->sprGenBase);
    saveStateSet(state, "sprTabBase",         vdp->sprTabBase);
    saveStateSet(state, "colTabBase",         vdp->colTabBase);
    saveStateSet(state, "chrTabBase",         vdp->chrTabBase);
    saveStateSet(state, "chrGenBase",         vdp->chrGenBase);
    
    saveStateSet(state, "FGColor",         vdp->FGColor);
    saveStateSet(state, "BGColor",         vdp->BGColor);
    saveStateSet(state, "XFGColor",         vdp->XFGColor);
    saveStateSet(state, "XBGColor",         vdp->XBGColor);
    
    saveStateSet(state, "blinkFlag",         vdp->blinkFlag);
    saveStateSet(state, "blinkCnt",         vdp->blinkCnt);
    
    saveStateSet(state, "drawArea",         vdp->drawArea);
    
    for (i = 0; i < sizeof(vdp->paletteReg) / sizeof(vdp->paletteReg[0]); i++) {
        sprintf(tag, "paletteRegNo%d", i);
        saveStateSet(state, tag,         vdp->paletteReg[i]);
    }

    saveStateSet(state, "vramEnable",         vdp->vramEnable);
    
    saveStateSet(state, "lineOffset",         vdp->lineOffset);
    saveStateSet(state, "firstLine",         vdp->firstLine);
    saveStateSet(state, "lastLine",         vdp->lastLine);
    saveStateSet(state, "displayOffest",         vdp->displayOffest);
    saveStateSet(state, "leftBorder",         vdp->leftBorder);
    saveStateSet(state, "displayArea",         vdp->displayArea);
    
    saveStateSet(state, "curLine",         vdp->curLine);
    
    saveStateSet(state, "screenOn",         vdp->screenOn);

    saveStateSet(state, "VAdjust",         vdp->VAdjust);
    saveStateSet(state, "HAdjust",         vdp->HAdjust);
    saveStateSet(state, "hAdjustSc0",         vdp->hAdjustSc0);
    
    saveStateSet(state, "screenMode",         vdp->screenMode);
    
    saveStateSetBuffer(state, "regs", vdp->vdpRegs, sizeof(vdp->vdpRegs));
    saveStateSetBuffer(state, "vdpStatus", vdp->vdpStatus, sizeof(vdp->vdpStatus));
    
    saveStateSet(state, "vdpKey",         vdp->vdpKey);
    saveStateSet(state, "vdpData",         vdp->vdpData);
    saveStateSet(state, "vdpDataLatch",         vdp->vdpDataLatch);
    saveStateSet(state, "vramAddress",         vdp->vramAddress);

    saveStateSet(state, "frameStartTime",         vdp->frameStartTime);
    saveStateSet(state, "machineFrame",           vdp->machineFrame);

    saveStateSet(state, "timeScrMode",         vdp->timeScrMode);
    saveStateSet(state, "timeScrModeEn",       vdp->timeScrModeEn);
    saveStateSet(state, "timeHint",            vdp->timeHint);
    saveStateSet(state, "timeHintEn",          vdp->timeHintEn);
    saveStateSet(state, "timeVint",            vdp->timeVint);
    saveStateSet(state, "timeVintEn",          vdp->timeVintEn);
    saveStateSet(state, "timeDrawAreaStart",   vdp->timeDrawAreaStart);
    saveStateSet(state, "timeDrawAreaStartEn", vdp->timeDrawAreaStartEn);
    saveStateSet(state, "timeVStart",          vdp->timeVStart);
    saveStateSet(state, "timeVStartEn",        vdp->timeVStartEn);
    saveStateSet(state, "timeDisplay",         vdp->timeDisplay);
    saveStateSet(state, "timeDisplayEn",       vdp->timeDisplayEn);
    saveStateSet(state, "timeDrawAreaEnd",         vdp->timeDrawAreaEnd);
    saveStateSet(state, "timeDrawAreaEndEn",   vdp->timeDrawAreaEndEn);
    
    saveStateSet(state, "screenOffTime",         vdp->screenOffTime);

    saveStateSet(state, "palette0",         vdp->palette0);

    for (i = 0; i < sizeof(vdp->palette) / sizeof(vdp->palette[0]); i++) {
        sprintf(tag, "paletteNo%d", i);
        saveStateSet(state, tag,         vdp->palette[i]);
    }

    saveStateSet(state, "vramAccMask",         vdp->vramAccMask);
    saveStateSet(state, "extRegsLocked",       vdp->extRegsLocked);
    saveStateSet(state, "palKey",              vdp->palKey);
    saveStateSet(state, "paletteLatch",        vdp->paletteLatch);
    saveStateSetBuffer(state, "paletteExtReg", vdp->paletteExtReg, sizeof(vdp->paletteExtReg));

    saveStateSetBuffer(state, "vram", vdp->vram, vdpIsV9968(vdp) ? VRAM_SIZE : VRAM_STATE_SIZE);

    saveStateClose(state);

    vdpCmdSaveState(vdp->cmdEngine);
}

static void loadState(VDP* vdp)
{
    SaveState* state = saveStateOpenForRead("vdp");
    UInt32 systemTime = boardSystemTime() + 100;
    char tag[32];
    int i;
    /* Old (blueMSX 2.8.2 era) state: apply the pre-rename tag fallbacks and
    ** rebuild the register-derived fields it does not serialize. New states
    ** take the original path unchanged. */
    int isOldFormat = boardStateLoadIsOldFormat();

    vdp->scr0splitLine = saveStateGet(state, "scr0splitLine",         0);

    vdp->sprGenBase = saveStateGet(state, "sprGenBase",         0);
    vdp->sprTabBase = saveStateGet(state, "sprTabBase",         0);
    vdp->colTabBase = saveStateGet(state, "colTabBase",         0);
    vdp->chrTabBase = saveStateGet(state, "chrTabBase",         0);
    vdp->chrGenBase = saveStateGet(state, "chrGenBase",         0);
    
    vdp->FGColor = saveStateGet(state, "FGColor",         0);
    vdp->BGColor = saveStateGet(state, "BGColor",         0);
    vdp->XFGColor = saveStateGet(state, "XFGColor",         0);
    vdp->XBGColor = saveStateGet(state, "XBGColor",         0);
    
    vdp->blinkFlag = saveStateGet(state, "blinkFlag",         0);
    vdp->blinkCnt = saveStateGet(state, "blinkCnt",         0);
    
    vdp->drawArea = saveStateGet(state, "drawArea",         0);
    /* Derived rather than stored: it only differs from drawArea for the fetch
    ** window before the first display line, and the next frame resets it. */
    vdp->cmdDrawArea = vdp->drawArea;
    
    for (i = 0; i < sizeof(vdp->paletteReg) / sizeof(vdp->paletteReg[0]); i++) {
        sprintf(tag, "paletteRegNo%d", i);
        vdp->paletteReg[i] = saveStateGet(state, tag,         0);
    }

    vdp->vramEnable = saveStateGet(state, "vramEnable",         0);
    
    vdp->lineOffset = saveStateGet(state, "lineOffset",         0);
    vdp->firstLine = saveStateGet(state, "firstLine",         0);
    vdp->lastLine = saveStateGet(state, "lastLine",         0);
    vdp->displayOffest = saveStateGet(state, "displayOffest",         0);
    vdp->leftBorder = saveStateGet(state, "leftBorder",         0);
    vdp->displayArea = saveStateGet(state, "displayArea",         0);
    
    vdp->curLine = saveStateGet(state, "curLine",         0);
    
    vdp->screenOn = saveStateGet(state, "screenOn",         0);

    vdp->VAdjust = saveStateGet(state, "VAdjust",         0);
    vdp->HAdjust = saveStateGet(state, "HAdjust",         0);
    vdp->hAdjustSc0 = saveStateGet(state, "hAdjustSc0",         0);
    
    vdp->screenMode = saveStateGet(state, "screenMode",         0);
    
    saveStateGetBuffer(state, "regs", vdp->vdpRegs, sizeof(vdp->vdpRegs));
    saveStateGetBuffer(state, "vdpStatus", vdp->vdpStatus, sizeof(vdp->vdpStatus));
    
    vdp->vdpKey = saveStateGet(state, "vdpKey",         0);
    vdp->vdpData = saveStateGet(state, "vdpData",         0);
    vdp->vdpDataLatch = saveStateGet(state, "vdpDataLatch",         0);
    vdp->vramAddress = saveStateGet(state, "vramAddress",         0);

    vdp->frameStartTime = saveStateGet(state, "frameStartTime",         0);
    vdp->machineFrame   = saveStateGet(state, "machineFrame",           0);

    vdp->timeScrMode = saveStateGet(state, "timeScrMode",         0);
    vdp->timeScrModeEn = saveStateGet(state, "timeScrModeEn",       0);
    vdp->timeHint = saveStateGet(state, "timeHint",            0);
    vdp->timeHintEn = saveStateGet(state, "timeHintEn",          0);
    vdp->timeVint = saveStateGet(state, "timeVint",            0);
    vdp->timeVintEn = saveStateGet(state, "timeVintEn",          0);
    vdp->timeDrawAreaStart = saveStateGet(state, "timeDrawAreaStart",   0);
    vdp->timeDrawAreaStartEn = saveStateGet(state, "timeDrawAreaStartEn", 0);
    vdp->timeVStart = saveStateGet(state, "timeVStart",          0);
    vdp->timeVStartEn = saveStateGet(state, "timeVStartEn",        0);
    vdp->timeDisplay = saveStateGet(state, "timeDisplay",         0);
    vdp->timeDisplayEn = saveStateGet(state, "timeDisplayEn",       0);
    vdp->timeDrawAreaEnd = saveStateGet(state, "timeDrawAreaEnd",         0);
    vdp->timeDrawAreaEndEn = saveStateGet(state, "timeDrawAreaEndEn",   0);
    
    vdp->screenOffTime = saveStateGet(state, "screenOffTime",         0);

    vdp->palette0 = saveStateGet(state, "palette0",         0);

    for (i = 0; i < sizeof(vdp->palette) / sizeof(vdp->palette[0]); i++) {
        sprintf(tag, "paletteNo%d", i);
        vdp->palette[i] = saveStateGet(state, tag,         0);
    }

    vdp->vramAccMask = saveStateGet(state, "vramAccMask",         0);
    vdp->extRegsLocked = saveStateGet(state, "extRegsLocked",     0);
    vdp->palKey        = saveStateGet(state, "palKey",            0);
    vdp->paletteLatch  = (UInt8)saveStateGet(state, "paletteLatch", 0);
    saveStateGetBuffer(state, "paletteExtReg", vdp->paletteExtReg, sizeof(vdp->paletteExtReg));
    /* The first sixteen entries are the sixteen colour palette seen a second
    ** time, so they come back from there rather than from the five bit copy.
    ** Entry zero keeps its own colour, the one transparency substitutes for. */
    vdp->paletteExt[0] = vdp->palette0;
    for (i = 1; i < 16; i++) {
        vdp->paletteExt[i] = vdp->palette[i];
    }
    for (i = 16; i < 256; i++) {
        vdp->paletteExt[i] = videoGetColor(255 * ((vdp->paletteExtReg[i] >> 10) & 0x1f) / 31,
                                           255 * ((vdp->paletteExtReg[i] >>  5) & 0x1f) / 31,
                                           255 * ( vdp->paletteExtReg[i]        & 0x1f) / 31);
    }

    if (isOldFormat) {
        /* Old (2.8.2) tag-name overrides:
        **   status / paletteTmp0 / vdp->palette%d / xfgColor / xbgColor */
        saveStateGetBuffer(state, "status", vdp->vdpStatus, sizeof(vdp->vdpStatus));
        vdp->palette0 = saveStateGet(state, "paletteTmp0", vdp->palette0);
        vdp->XFGColor = saveStateGet(state, "xfgColor", vdp->XFGColor);
        vdp->XBGColor = saveStateGet(state, "xbgColor", vdp->XBGColor);
        for (i = 0; i < sizeof(vdp->palette) / sizeof(vdp->palette[0]); i++) {
            sprintf(tag, "vdp->palette%d", i);
            vdp->palette[i] = saveStateGet(state, tag, vdp->palette[i]);
        }
    }

    saveStateGetBuffer(state, "vram", vdp->vram, vdpIsV9968(vdp) ? VRAM_SIZE : VRAM_STATE_SIZE);

    saveStateClose(state);

    vdpCmdLoadState(vdp->cmdEngine);

    vdp->vramPtr = vdp->vram + vdp->vramOffsets[(vdp->vdpRegs[0x2d] >> 6) & 1];
    vdpUpdateVramMode(vdp);

    canFlipFrameBuffer = 0;
    vdp->flippedEarly  = 0;
    vdp->lastFlipEarly = 0;

    if (isOldFormat) {
        /* Reconstruct derived state from vdpRegs; old states only
        ** serialize raw registers and the renderer would otherwise read
        ** its tables from VRAM offset 0. */
        vdp->screenMode = updateScreenMode(vdp);

        vdp->chrTabBase = ((((int)vdp->vdpRegs[2] << 10) & ~((int)(vdp->vdpRegs[25] & 1) << 15)) | ~(-1 << 10)) & vdp->vramMask;
        vdp->chrGenBase = (((int)vdp->vdpRegs[4] << 11) | ~(-1 << 11)) & vdp->vramMask;
        vdp->colTabBase = (((int)vdp->vdpRegs[10] << 14) | ((int)vdp->vdpRegs[3] << 6) | ~(-1 << 6)) & vdp->vramMask;
        vdp->sprTabBase = (((int)vdp->vdpRegs[11] << 15) | ((int)vdp->vdpRegs[5] << 7) | ~(-1 << 7)) & vdp->vramMask;
        vdp->sprGenBase = (((int)vdp->vdpRegs[6] << 11) | ~(-1 << 11)) & vdp->vramMask;
        vdpUpdateVramMode(vdp);

        vdp->screenOn   = vdp->vdpRegs[1] & 0x40;
        vdp->vramEnable = vdp->vram192 || !((vdp->vdpRegs[0x2d] >> 6) & 1);
        vdpSetScreenMode(vdp->cmdEngine, cmdEngineScreenMode(vdp), vdp->vdpRegs[25] & 0x40);
        if (vdp->screenMode == 0 || vdp->screenMode == 13) {
            vdp->displayArea = 960;
            vdp->leftBorder  = 102 + 92;
        }
        else {
            vdp->displayArea = 1024;
            vdp->leftBorder  = 102 + 56;
        }
        vdp->HAdjust = (-((Int8)(vdp->vdpRegs[18] << 4) >> 4));
        if (vdp->vdpRegs[25] & 0x08) {
            vdp->HAdjust += 4;
        }
        vdp->leftBorder += vdp->HAdjust;

        /* FGColor/BGColor from reg7 and palette0 from the resolved color-0, so
        ** the next reg7/reg8 write does not repaint color 0 / the backdrop
        ** with a wrong (gray) palette0. */
        vdp->FGColor = vdp->vdpRegs[7] >> 4;
        vdp->BGColor = vdp->vdpRegs[7] & 0x0F;
        vdp->palette0 = vdp->palette[0];
        updateOutputMode(vdp);
    }
    else {
        updateScreenMode(vdp);
        /* The output mode is not in the state: rebuild it and colour 0. */
        updateOutputMode(vdp);
    }

    if (vdp->timeScrModeEn) {
        boardTimerAdd(vdp->timerScrModeChange, vdp->timeScrMode);
    }
    if (vdp->timeHintEn) {
        boardTimerAdd(vdp->timerHint, vdp->timeHint + 20);
    }
    if (vdp->timeVintEn) {
        boardTimerAdd(vdp->timerVint, vdp->timeVint);
    }
    if (vdp->timeDrawAreaStartEn) {
        boardTimerAdd(vdp->timerDrawAreaStart, vdp->timeDrawAreaStart);
    }
    if (vdp->timeVStartEn) {
        boardTimerAdd(vdp->timerVStart, vdp->timeVStart);
    }
    if (vdp->timeDisplayEn) {
        boardTimerAdd(vdp->timerDisplay, vdp->timeDisplay);
    }
    if (vdp->timeDrawAreaEndEn) {
        boardTimerAdd(vdp->timerDrawAreaEnd, vdp->timeDrawAreaEnd);
    }

}

#endif

/* Order of the debug register bank: registers, command registers, palette,
** status, then the three extras. dbgWriteRegister decodes with these too. */
static void dbgRegisterLayout(VDP* vdp, int* regCount, int* cmdRegCount,
                              int* paletteCount, int* statusRegCount)
{
    if (vdpIsV99x8(vdp)) {
        *regCount       = vdp->vdpVersion == VDP_V9938 ? 24 : 32;
        *cmdRegCount    = 15;
        *paletteCount   = 16;
        *statusRegCount = 9;
    }
    else {
        *regCount       = 8;
        *cmdRegCount    = 0;
        *paletteCount   = 0;
        *statusRegCount = 1;
    }
}

/* Raster log of theVdp: two frames in static buffers, filled on the emulator
** thread only. A note from a new frame reuses the older buffer. */
volatile int vdpRasterArmed;
static int    rasterArmCount;
static int    rasterGeneration;
static VdpRasterFrame rasterBuf[2];
static int    rasterCur;
static int    rasterSeenGen;
static UInt32 rasterSeenOwner;
static UInt32 rasterArmEnd;     /* frames from here on are logged whole */
static int    rasterSettled;    /* every frame from now on is logged whole */
static UInt8  rasterRegs[32];   /* the registers as the log has them */
static UInt32 rasterObsStart;   /* the newest frame seen, and where it ends */
static UInt32 rasterObsEnd;
static int    rasterPrevValid;  /* the frame seen before it ended where it began */
static UInt32 rasterPrevStart;
static UInt32 rasterPrevEnd;
/* Frames whose registers another thread wrote; kept across rearming. */
static volatile UInt32 rasterForeignStart[2];
static volatile int    rasterForeignSet[2];
static int    rasterForeignNext;
#define RASTER_HEAD (sizeof(VdpRasterFrame) - sizeof(rasterBuf[0].ev))

void vdpRasterArm(int on)
{
    if (on) {
        if (rasterArmCount++ == 0) {
            /* Wrapped by hand: a generation is never 0, which means not armed. */
            rasterGeneration = rasterGeneration >= 0x7fffffff ? 1 : rasterGeneration + 1;
            vdpRasterArmed = rasterGeneration;
        }
    }
    else if (rasterArmCount > 0 && --rasterArmCount == 0) {
        vdpRasterArmed = 0;
    }
}

static int rasterIsForeign(UInt32 frameStart)
{
    return (rasterForeignSet[0] && rasterForeignStart[0] == frameStart) ||
           (rasterForeignSet[1] && rasterForeignStart[1] == frameStart);
}

/* The registers now, R#reg still at old, as the log keeps them. */
static void rasterCurrentRegs(VDP* vdp, int reg, UInt8 old, UInt8* regs)
{
    memcpy(regs, vdp->vdpRegs, 32);
    if (reg >= 0 && reg < 32) {
        regs[reg] = old;
    }
    memset(regs + 14, 0, 4);
}

/* Something changed the registers or the frame timing outside the log, such
** as a reset or a loaded state: only frames after this one are whole. */
static void rasterBreak(VDP* vdp)
{
    rasterBuf[0].complete = 0;
    rasterBuf[1].complete = 0;
    rasterArmEnd  = vdp->timeDisplay;
    rasterSettled = 0;
}

/* Emulator thread. Forgets the buffers when armed anew or the VDP changed. */
static int rasterObserve(VDP* vdp, int reg, UInt8 old)
{
    int    gen   = vdpRasterArmed;
    UInt32 owner = (UInt32)vdp->debugHandle + 1;
    UInt8  regs[32];

    if (gen == 0 || vdp != theVdp) {
        return 0;
    }
    rasterCurrentRegs(vdp, reg, old, regs);
    if (gen != rasterSeenGen || owner != rasterSeenOwner) {
        rasterBuf[0].valid = 0;
        rasterBuf[1].valid = 0;
        rasterSeenGen   = gen;
        rasterSeenOwner = owner;
        rasterPrevValid = 0;
        rasterObsStart  = vdp->frameStartTime;
        rasterObsEnd    = vdp->timeDisplay;
        memcpy(rasterRegs, regs, sizeof(rasterRegs));
        rasterBreak(vdp);
        return 1;
    }
    if (vdp->frameStartTime != rasterObsStart) {
        rasterPrevValid = rasterObsEnd == vdp->frameStartTime;
        rasterPrevStart = rasterObsStart;
        rasterPrevEnd   = rasterObsEnd;
        if ((Int32)(vdp->frameStartTime - rasterObsEnd) < 0) {
            rasterBreak(vdp);
        }
        rasterObsStart = vdp->frameStartTime;
        rasterObsEnd   = vdp->timeDisplay;
    }
    if (memcmp(rasterRegs, regs, sizeof(rasterRegs)) != 0) {
        memcpy(rasterRegs, regs, sizeof(rasterRegs));
        rasterBreak(vdp);
    }
    if (!rasterSettled && (Int32)(vdp->frameStartTime - rasterArmEnd) > 0) {
        rasterSettled = 1;
    }
    return 1;
}

static int rasterIsComplete(UInt32 frameStart)
{
    return rasterSettled || (Int32)(frameStart - rasterArmEnd) >= 0;
}

static void rasterAdd(VDP* vdp, UInt32 time, int kind, int reg, UInt8 old, UInt8 value)
{
    VdpRasterFrame* f = &rasterBuf[rasterCur];
    VdpRasterEvent* e;
    Int32 t;

    if (kind == VDP_RASTER_REG) {
        rasterRegs[reg] = value;
    }
    if (!f->valid || f->frameStart != vdp->frameStartTime) {
        rasterCur ^= 1;
        f = &rasterBuf[rasterCur];
        f->valid         = 1;
        f->complete      = rasterIsComplete(vdp->frameStartTime);
        f->ended         = 0;
        f->foreign       = 0;
        f->frameStart    = vdp->frameStartTime;
        f->lastLine      = vdp->lastLine;
        f->firstLine     = vdp->firstLine;
        f->displayOffset = vdp->displayOffest;
        f->field         = (vdp->vdpStatus[2] >> 1) & 1;
        f->blinkFlag     = vdp->blinkFlag;
        f->beamLine      = 0;
        f->beamCycle     = 0;
        memcpy(f->startRegs, vdp->vdpRegs, sizeof(f->startRegs));
        if (kind == VDP_RASTER_REG) {
            f->startRegs[reg] = old;
        }
        f->count   = 0;
        f->dropped = 0;
    }
    if (f->count >= VDP_RASTER_MAX) {
        f->dropped++;
        return;
    }
    e = &f->ev[f->count++];
    t = (Int32)(time - vdp->frameStartTime);
    if (t < 0) {
        t = 0;
    }
    if (f->lastLine > 0 && t / HPERIOD >= f->lastLine) {
        t = f->lastLine * HPERIOD - 1;
    }
    e->line  = (UInt16)MIN(t / HPERIOD, 0xffff);
    e->cycle = (UInt16)(t % HPERIOD);
    e->kind  = (UInt8)kind;
    e->reg   = (UInt8)reg;
    e->value = value;
    e->aux   = old;
}

void vdpRasterNoteReg(VDP* vdp, int reg, UInt8 old, UInt8 value)
{
    if (reg >= 32 || (reg >= 14 && reg <= 17) || old == value || vdp != theVdp) {
        return;
    }
    if (!emulatorIsEmuThread()) {
        if (!rasterIsForeign(vdp->frameStartTime)) {
            rasterForeignSet[rasterForeignNext]   = 0;
            rasterForeignStart[rasterForeignNext] = vdp->frameStartTime;
            rasterForeignSet[rasterForeignNext]   = 1;
            rasterForeignNext ^= 1;
        }
        return;
    }
    if (rasterObserve(vdp, reg, old)) {
        rasterAdd(vdp, boardSystemTime(), VDP_RASTER_REG, reg, old, value);
    }
}

void vdpRasterNoteLineInt(VDP* vdp)
{
    if (vdp == theVdp && emulatorIsEmuThread() && rasterObserve(vdp, -1, 0)) {
        rasterAdd(vdp, vdp->timeHint, VDP_RASTER_LINEINT, 19,
                  (UInt8)((vdp->vdpRegs[0] >> 4) & 1), vdp->vdpRegs[19]);
    }
}

/* Copies the logged frame starting at frameStart; 0 when there is none. */
static int rasterCopyFrame(VdpRasterFrame* out, UInt32 frameStart)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (rasterBuf[i].valid && rasterBuf[i].frameStart == frameStart) {
            memcpy(out, &rasterBuf[i], RASTER_HEAD);
            memcpy(out->ev, rasterBuf[i].ev, rasterBuf[i].count * sizeof(VdpRasterEvent));
            out->foreign |= rasterIsForeign(frameStart);
            return 1;
        }
    }
    return 0;
}

/* Sets the layout of a frame of the given length from the VDP's current one. */
static void rasterSetLength(VdpRasterFrame* f, VDP* vdp, int lines)
{
    f->lastLine      = lines;
    f->displayOffset = lines >= 313 ? 27 : 0;
    f->firstLine     = vdp->firstLine - vdp->displayOffest + f->displayOffset;
}

/* At a frame tap the frame that just ended goes to [0]; while paused the one in progress
** does, with the beam, but as ended at the end of active display or after the early flip
** there. [1] is the frame before [0]. */
static void rasterCapture(VDP* vdp, VdpDebugState* out)
{
    VdpRasterFrame* r = out->raster;
    UInt32 now = boardSystemTime();
    UInt32 start;
    int    ended;
    int    moved;
    int    lines;
    int    i;

    memset(&r[0], 0, RASTER_HEAD);
    memset(&r[1], 0, RASTER_HEAD);
    if (!rasterObserve(vdp, -1, 0)) {
        return;
    }
    moved = (Int32)(now - vdp->timeDisplay) >= 0;
    ended = moved || vdpAtShown || vdp->flippedEarly;
    lines = moved ? (int)((vdp->timeDisplay - vdp->frameStartTime) / HPERIOD) : vdp->lastLine;
    if (lines <= 0) {
        return;
    }

    /* Nothing logged in it: the registers have not changed since it began.
    ** The layout fields have already moved on to the next frame at a tap. */
    if (!rasterCopyFrame(&r[0], vdp->frameStartTime)) {
        r[0].valid      = 1;
        r[0].complete   = rasterIsComplete(vdp->frameStartTime);
        r[0].foreign    = rasterIsForeign(vdp->frameStartTime);
        r[0].frameStart = vdp->frameStartTime;
        r[0].field      = ((vdp->vdpStatus[2] >> 1) & 1) ^ (moved ? 1 : 0);
        r[0].blinkFlag  = vdp->blinkFlag;
        rasterSetLength(&r[0], vdp, lines);
        memcpy(r[0].startRegs, vdp->vdpRegs, sizeof(r[0].startRegs));
    }
    r[0].ended     = ended;
    r[0].beamLine  = ended ? r[0].lastLine : (int)((now - vdp->frameStartTime) / HPERIOD);
    r[0].beamCycle = ended ? 0 : (int)((now - vdp->frameStartTime) % HPERIOD);

    /* The frame before is known only if logged, or seen ending where [0]
    ** began; with nothing logged it kept the registers [0] began with. */
    for (i = 0; i < 2; i++) {
        start = rasterBuf[i].frameStart;
        if (rasterBuf[i].valid && start != r[0].frameStart &&
            start + HPERIOD * rasterBuf[i].lastLine == r[0].frameStart) {
            rasterCopyFrame(&r[1], start);
            break;
        }
    }
    if (i == 2 && rasterPrevValid && rasterObsStart == r[0].frameStart &&
        rasterPrevEnd == r[0].frameStart && rasterPrevEnd != rasterPrevStart) {
        memcpy(&r[1], &r[0], RASTER_HEAD);
        r[1].complete   = rasterIsComplete(rasterPrevStart);
        r[1].foreign    = rasterIsForeign(rasterPrevStart);
        r[1].frameStart = rasterPrevStart;
        r[1].field     ^= 1;
        r[1].count      = 0;
        r[1].dropped    = 0;
        rasterSetLength(&r[1], vdp, (int)((rasterPrevEnd - rasterPrevStart) / HPERIOD));
    }
    if (r[1].valid) {
        r[1].ended     = 1;
        r[1].beamLine  = r[1].lastLine;
        r[1].beamCycle = 0;
    }
}

int vdpDebugCount(void)
{
    return theVdp != NULL ? 1 : 0;
}

const char* vdpDebugName(int index)
{
    VDP* vdp = index == 0 ? theVdp : NULL;
    return vdp != NULL ? VdpNames[vdp->vdpVersion] : NULL;
}

int vdpDebugCapture(int index, VdpDebugState* out)
{
    VDP* vdp = index == 0 ? theVdp : NULL;
    int regCount, cmdRegCount, paletteCount, statusRegCount;
    int vramBytes;
    int i;

    if (vdp == NULL) {
        out->valid = 0;
        return 0;
    }
    dbgRegisterLayout(vdp, &regCount, &cmdRegCount, &paletteCount, &statusRegCount);

    out->valid       = 1;
    out->id          = (UInt32)vdp->debugHandle + 1;
    strncpy(out->name, VdpNames[vdp->vdpVersion], sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = 0;
    out->version     = vdp->vdpVersion;
    out->vramSize    = vdp->vramSize;
    out->vramMask    = vdp->vramMask;
    out->vram128     = vdp->vram128;
    out->screenMode  = vdp->screenMode;
    out->isPal       = vdpIsVideoPal(vdp) != 0;
    out->screenOn    = vdp->screenOn;
    out->blinkFlag   = vdp->blinkFlag;
    out->regCount    = regCount;
    out->statusCount = statusRegCount > 1 ? 10 : 1;
    out->cmdRegCount = cmdRegCount;

    memcpy(out->regs, vdp->vdpRegs, sizeof(out->regs));
    memcpy(out->status, vdp->vdpStatus, sizeof(out->status));
    memset(out->cmdRegs, 0, sizeof(out->cmdRegs));
    if (statusRegCount > 1 && vdp->cmdEngine != NULL) {
        /* Stored flags plus the command engine's, from fields only:
        ** peekStatus syncs and vdpGetColor clears TR. */
        out->status[2] |= vdpGetStatus(vdp->cmdEngine);
        out->status[7]  = vdpCmdPeek(vdp->cmdEngine, 0x0c, 0);
        out->status[8]  = (UInt8)vdpGetBorderX(vdp->cmdEngine);
        out->status[9]  = (UInt8)(vdpGetBorderX(vdp->cmdEngine) >> 8);
        for (i = 0; i < cmdRegCount && i < (int)sizeof(out->cmdRegs); i++) {
            out->cmdRegs[i] = vdpCmdPeek(vdp->cmdEngine, (UInt8)i, 0);
        }
    }

    memcpy(out->paletteReg, vdp->paletteReg, sizeof(out->paletteReg));
    for (i = 0; i < 16; i++) {
        int r, g, b;
        if (vdp->vdpVersion == VDP_TMS9918A) {
            r = msx1Palette_9918a[i].r; g = msx1Palette_9918a[i].g; b = msx1Palette_9918a[i].b;
        }
        else if (vdpIsMsx1Vdp(vdp)) {
            r = msx1Palette[i].r; g = msx1Palette[i].g; b = msx1Palette[i].b;
        }
        else {
            r = ((vdp->paletteReg[i] >> 4) & 7) * 255 / 7;
            g = ((vdp->paletteReg[i] >> 8) & 7) * 255 / 7;
            b = (vdp->paletteReg[i] & 7) * 255 / 7;
        }
        out->paletteRgb[i] = ((UInt32)r << 16) | ((UInt32)g << 8) | (UInt32)b;
    }

    /* A 16K VDP is created with 64K the renderer and engine can reach, so
    ** copy up to vramMask too; nothing past that is ever written. */
    vramBytes = MAX(vdp->vramSize, vdp->vramMask + 1);
    vramBytes = MIN(vramBytes, (int)MIN(sizeof(out->vram), sizeof(vdp->vram)));
    memcpy(out->vram, vdp->vram, vramBytes);
    memset(out->vram + vramBytes, 0, sizeof(out->vram) - vramBytes);
    rasterCapture(vdp, out);
    return 1;
}

/* The VDP's frame buffer rows fit; a wider one would be cut. */
typedef char VdpShotFits[((8 + 256 + 8) << 1) <= VDP_SHOT_WIDTH ? 1 : -1];

/* Copies samples [from, to) of row y of src, up to the row's width. */
static void shotCopy(VdpFrameShot* out, int y, FrameBuffer* src, int from, int to)
{
    int shift = src->line[y].doubleWidth;
    int width = MIN(src->maxWidth << shift, VDP_SHOT_WIDTH);

    out->shift[y] = (UInt8)shift;
    to = MIN(to, width);
    if (to > from && out->pixels) {
        memcpy(out->pix[y] + from, src->line[y].buffer + from, (to - from) * sizeof(UInt16));
    }
}

static void shotKey(VdpFrameShot* out, int y, int shift)
{
    int width = MIN(out->maxWidth << shift, VDP_SHOT_WIDTH);
    int x;

    out->shift[y] = (UInt8)shift;
    for (x = 0; out->pixels && x < width; x++) {
        out->pix[y][x] = 0x8000;
    }
}

/* Samples of row y, the row of curLine, that the VDP has drawn this frame. */
static int shotDrawnSamples(VDP* vdp, FrameBuffer* draw, int y)
{
    const UInt16* line = draw->line[y].buffer;
    const UInt16* start;

    if (vdp->lineOffset < 0) {
        return 0;
    }
    if (vdp->lineOffset > 32) {
        return VDP_SHOT_WIDTH;
    }
    start = (const UInt16*)RefreshLineStart(vdp->curLine);
    if (start == NULL || start < line || start > line + VDP_SHOT_WIDTH) {
        return 0;
    }
    return MIN((int)(start - line) + RefreshLineDrawn(vdp), VDP_SHOT_WIDTH);
}

/* Sprite split and sprite tables of theVdp: per frame of its frame buffer, each row
** drawn again without sprites as its line ended, and the tables its sprites were
** laid out from. Emulator thread only. */
volatile int vdpSprSplitArmed;
static int      sprSplitArmCount, sprTabArmCount;
static unsigned sprSplitGeneration, sprTabGeneration;

#define SPR_SPLIT_GEN ((int)((unsigned)vdpSprSplitArmed & 0xffff))
#define SPR_TAB_GEN   ((int)(((unsigned)vdpSprSplitArmed >> 16) & 0xffff))
/* The renderers' sprite break column: SCREEN 5 and up lay a line's sprites out
** here; sprite mode 1 and SCREEN 4 at the line's right border. */
#define SPR_EVAL_COLUMN 24
/* Sprite tables a frame of the frame buffer keeps; rows past them get none and
** are marked SPR_TAB_LOST. */
#define SPR_TAB_PER_FRAME 32
#define SPR_TAB_LOST      0xfe

typedef struct {
    int    gen;             /* SPR_SPLIT_GEN when recorded; 0: never */
    UInt32 owner;
    UInt32 frameStart;
    UInt8  state;           /* VDP_SPR_ROW_* */
    UInt8  tabIndex;        /* into the plane's tables; 0xff none, SPR_TAB_LOST */
    UInt8  tabState;
    int    tabGen;          /* SPR_TAB_GEN when recorded; 0: never */
    UInt32 tabOwner;
    UInt32 tabFrame;
    UInt8  palState;
    int    palGen;          /* SPR_TAB_GEN when the palette was recorded; 0: never */
    UInt32 palOwner;
    UInt32 palFrame;
} SprSplitRow;

typedef struct {
    FrameBuffer* key;
    UInt32       used;
    SprSplitRow  row[VDP_SHOT_ROWS];
    UInt8        mask[VDP_SHOT_ROWS][VDP_SHOT_WIDTH / 8];
    UInt16       bg[VDP_SHOT_ROWS][VDP_SHOT_WIDTH];
    UInt32       tabFrame;  /* the frame the tables are of */
    int          tabCount;
    VdpSprTable  tab[SPR_TAB_PER_FRAME];
    UInt16       pal[VDP_SHOT_ROWS][16];
} SprSplitPlane;

/* The frame buffer's four frames and its two blend frames. */
#define SPR_SPLIT_PLANES 6
static SprSplitPlane sprPlanes[SPR_SPLIT_PLANES];
static UInt32        sprPlaneClock;

/* The tables displayed lines used, for the generation and VDP that recorded
** them since sprUseSince, and the last one noted in the frame being drawn. */
static VdpSprUse sprUses[VDP_SPR_USES];
static int       sprUseCount, sprUseGen;
static UInt32    sprUseOwner, sprUseSince;
static VdpSprUse sprUseLast;
static int       sprUseLastSet;

static int sprUseSame(const VdpSprUse* a, const VdpSprUse* b)
{
    return a->screenMode == b->screenMode && a->r1 == b->r1 && a->r5 == b->r5 && a->r6 == b->r6 &&
           a->r8 == b->r8 && a->r11 == b->r11;
}

/* Table u is used in the frame being drawn: stamped with the seq the frame
** will have once it completes, in place of the least recent when full. */
static void sprUseNote(VDP* vdp, const VdpSprUse* u)
{
    UInt32 owner = (UInt32)vdp->debugHandle + 1;
    UInt32 seq, flips;
    int i, k;

    frameBufferGetLastDrawnOf(vdp->frameBuffer, &seq, &flips);
    seq = flips + 1;
    if (sprUseGen != SPR_TAB_GEN || sprUseOwner != owner) {
        sprUseGen   = SPR_TAB_GEN;
        sprUseOwner = owner;
        sprUseSince = seq;
        sprUseCount = 0;
    }
    for (i = 0; i < sprUseCount && !sprUseSame(&sprUses[i], u); i++) {
    }
    if (i == sprUseCount && i < VDP_SPR_USES) {
        sprUseCount++;
    }
    else if (i == sprUseCount) {
        for (i = 0, k = 1; k < sprUseCount; k++) {
            if ((Int32)(sprUses[k].seq - sprUses[i].seq) < 0) {
                i = k;
            }
        }
        /* Every table used since then is still held. */
        if ((Int32)(sprUses[i].seq + 1 - sprUseSince) > 0) {
            sprUseSince = sprUses[i].seq + 1;
        }
    }
    sprUses[i] = *u;
    sprUses[i].seq = seq;
}

/* Both generations in the hooks' one word, published in one store. */
static void sprArmPublish(void)
{
    vdpSprSplitArmed = (int)((sprSplitArmCount > 0 ? sprSplitGeneration : 0u) | (sprTabArmCount > 0 ? sprTabGeneration << 16 : 0u));
}

static void sprArm(int on, int* count, unsigned* generation)
{
    if (on) {
        if ((*count)++ == 0 && ++*generation > 0xffff) {
            *generation = 1;
        }
    }
    else if (*count > 0) {
        --*count;
    }
    sprArmPublish();
}

void vdpSprSplitArm(int on)
{
    sprArm(on, &sprSplitArmCount, &sprSplitGeneration);
}

void vdpSprTableArm(int on)
{
    sprArm(on, &sprTabArmCount, &sprTabGeneration);
}

static SprSplitPlane* sprPlaneOf(FrameBuffer* fb)
{
    int i;

    for (i = 0; fb != NULL && i < SPR_SPLIT_PLANES; i++) {
        if (sprPlanes[i].key == fb) {
            return &sprPlanes[i];
        }
    }
    return NULL;
}

/* len bytes of sprite table from logical address a, read as the sprite line builder
** reads them in sprite mode sprMode: the only place the tables are read from VRAM. */
static void sprTabRead(VDP* vdp, int sprMode, int a, UInt8* dst, int len)
{
    int i;

    if (sprMode == 1) {
        memcpy(dst, vdp->vram + a, len);
    }
    else if (vdp->screenMode < 7 || vdp->screenMode > 12) {
        memcpy(dst, vdp->vramPtr + (a & vdp->vramAccMask), len);
    }
    else {
        for (i = 0; i < len; i++) {
            dst[i] = *MAP_VRAM(vdp, a + i);
        }
    }
}

/* The tables as they are now into t; false when the mode draws no sprites. */
static int sprTabGather(VDP* vdp, VdpSprTable* t)
{
    int mode = vdp->screenMode;
    int sprMode = mode >= 1 && mode <= 3 ? 1 : mode >= 4 && mode <= 12 ? 2 : 0;
    int attr;

    /* An unknown mode is kept as 1 but draws nothing. */
    if (sprMode == 0 || vdp->RefreshLine == RefreshLineBlank) {
        return 0;
    }
    t->base = vdp->sprTabBase & ~0x3ff;
    /* Built from the block, so a wider base cannot point the attributes out of it. */
    attr = sprMode == 1 ? vdp->sprTabBase & ~0x7f : t->base | (vdp->sprTabBase & 0x200);
    if (attr - t->base < 0 || attr - t->base > (int)sizeof(t->bytes) - 128) {
        return 0;
    }
    memcpy(t->regs, vdp->vdpRegs, sizeof(t->regs));
    t->screenMode = mode;
    memset(t->bytes, 0, sizeof(t->bytes));
    if (sprMode == 2) {
        sprTabRead(vdp, 2, t->base, t->bytes, 0x200);
    }
    sprTabRead(vdp, sprMode, attr, t->bytes + (attr - t->base), 128);
    return 1;
}

/* The same layout: the bytes and the registers sprites are laid out with. */
static int sprTabSame(const VdpSprTable* a, const VdpSprTable* b)
{
    static const int regs[] = { 0, 1, 5, 6, 8, 9, 11, 23 };
    int attr = (a->regs[5] << 7) & (a->screenMode <= 3 ? 0x380 : 0x200);
    int i;

    /* The attributes first: they are what a game rewrites while lines are drawn. */
    if (memcmp(a->bytes + attr, b->bytes + attr, 128) != 0) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); i++) {
        if (a->regs[regs[i]] != b->regs[regs[i]]) {
            return 0;
        }
    }
    return a->screenMode == b->screenMode && a->base == b->base && memcmp(a->bytes, b->bytes, sizeof(a->bytes)) == 0;
}

/* Row r's sprites are laid out while line r - 1 is drawn, from the tables as they
** are at its end when no access came after the layout. One sync() call has no
** access in it, so the lines it ends share one read. */
static void sprRecordTable(VDP* vdp, SprSplitPlane* plane, int r, int from)
{
    static VdpSprTable now;
    static SprSplitPlane* nowPlane;
    static UInt32 nowTime, nowFrame;
    static int nowIndex = -1;
    SprSplitRow* row = &plane->row[r];
    SprSplitRow* prev = r > 0 ? &plane->row[r - 1] : NULL;
    UInt32 time = boardSystemTime();
    int gen = SPR_TAB_GEN;
    int i;

    /* A plane just taken for another frame buffer has tabCount -1. */
    if (plane->tabFrame != vdp->frameStartTime || plane->tabCount < 0) {
        plane->tabFrame = vdp->frameStartTime;
        plane->tabCount = 0;
        nowPlane = NULL;
        sprUseLastSet = 0;
    }
    if (nowPlane != plane || nowTime != time || nowFrame != vdp->frameStartTime) {
        nowPlane = plane;
        nowTime = time;
        nowFrame = vdp->frameStartTime;
        nowIndex = -1;
        if (sprTabGather(vdp, &now)) {
            /* Tables in a plane are unique, so a match with the previous row's table is the match. */
            if (prev != NULL && prev->tabIndex < plane->tabCount && sprTabSame(&plane->tab[prev->tabIndex], &now)) {
                i = prev->tabIndex;
            }
            else {
                for (i = 0; i < plane->tabCount && !sprTabSame(&plane->tab[i], &now); i++) {
                }
            }
            if (i == plane->tabCount && i < SPR_TAB_PER_FRAME) {
                plane->tab[plane->tabCount++] = now;
            }
            nowIndex = i < plane->tabCount ? i : SPR_TAB_LOST;
        }
    }
    row->tabGen   = gen;
    row->tabOwner = (UInt32)vdp->debugHandle + 1;
    row->tabFrame = vdp->frameStartTime;
    row->tabIndex = nowIndex < 0 ? 0xff : (UInt8)nowIndex;
    row->tabState = VDP_SPR_ROW_NONE;
    /* A displayed line's table, lost or not, unless the last one noted this frame. */
    if (nowIndex >= 0 && vdp->curLine + 1 - vdp->firstLine >= 0 &&
        vdp->curLine + 1 - vdp->firstLine < (vdpIsScanLines212(vdp->vdpRegs) ? 212 : 192)) {
        VdpSprUse u;
        u.screenMode = (UInt8)now.screenMode;
        u.r1  = now.regs[1] & 0x43;
        u.r5  = now.regs[5];
        u.r6  = now.regs[6];
        u.r8  = now.regs[8] & 0x02;
        u.r11 = now.regs[11];
        u.seq = 0;
        if (!sprUseLastSet || !sprUseSame(&u, &sprUseLast) || sprUseGen != gen || sprUseOwner != row->tabOwner) {
            sprUseNote(vdp, &u);
            sprUseLast = u;
            sprUseLastSet = 1;
        }
    }
    if (nowIndex < 0 || nowIndex == SPR_TAB_LOST) {
        return;
    }
    /* Exact when laid out after the line's last access: at its right border (sprite
    ** mode 1, SCREEN 4) or inside its last piece; or when the tables stayed. */
    row->tabState = VDP_SPR_ROW_APPROX;
    if (from < 0 || now.screenMode <= 4 || from < SPR_EVAL_COLUMN ||
        (prev != NULL && prev->tabGen == gen && prev->tabFrame == row->tabFrame && prev->tabIndex == row->tabIndex)) {
        row->tabState = VDP_SPR_ROW_EXACT;
    }
}

/* The palette as the line being drawn showed its first display dot. */
static UInt16 sprPalStart[16];
static UInt32 sprPalStartFrame;
static int    sprPalStartLine = -1;

static void sprPaletteDisplayStart(VDP* vdp)
{
    memcpy(sprPalStart, vdp->paletteReg, sizeof(sprPalStart));
    sprPalStartFrame = vdp->frameStartTime;
    sprPalStartLine  = vdp->curLine;
}

/* Row y's palette as its line ended. Exact when every display dot was drawn in
** the last piece or the palette is the one the first display dot was drawn with. */
static void sprRecordPalette(VDP* vdp, SprSplitPlane* plane, int y, int from)
{
    SprSplitRow* row = &plane->row[y];

    memcpy(plane->pal[y], vdp->paletteReg, sizeof(plane->pal[y]));
    row->palGen   = SPR_TAB_GEN;
    row->palOwner = (UInt32)vdp->debugHandle + 1;
    row->palFrame = vdp->frameStartTime;
    row->palState = VDP_SPR_ROW_APPROX;
    if (from <= 0 || (sprPalStartLine == vdp->curLine && sprPalStartFrame == vdp->frameStartTime &&
                      memcmp(sprPalStart, vdp->paletteReg, sizeof(sprPalStart)) == 0)) {
        row->palState = VDP_SPR_ROW_EXACT;
    }
}

/* Line curLine has ended; its last piece was drawn from lineOffset from. Draws
** it again without sprites and without evaluating any, as rescrollLine does,
** keeps that, and puts the line back as it was. */
static void sprSplitLineDone(VDP* vdp, int from)
{
    UInt16 saved[FB_MAX_LINE_WIDTH];
    int y = vdp->curLine - vdp->displayOffest;
    FrameBuffer* fb;
    SprSplitPlane* plane;
    SprSplitRow* row;
    UInt16* line;
    UInt16* start;
    UInt8* sprites;
    int savedShift, savedScanline, savedReScroll;
    int shift, width, state, noSprites, x, i;

    /* Line -1 lays out row 0's sprites. */
    if (vdp != theVdp || y < -1 || y >= SCREEN_HEIGHT || y >= VDP_SHOT_ROWS) {
        return;
    }
    fb = frameBufferGetDrawFrame();
    if (fb == NULL || frameBufferGetActiveDraw() != vdp->frameBuffer) {
        return;
    }
    plane = sprPlaneOf(fb);
    /* spritesEnable is global: the UI thread's sync must not hide sprites, so
    ** a row it finishes is left unrecorded. */
    if (!emulatorIsEmuThread()) {
        if (plane != NULL && y >= 0) {
            plane->row[y].gen = 0;
            plane->row[y].palGen = 0;
        }
        if (plane != NULL && y + 1 < VDP_SHOT_ROWS) {
            plane->row[y + 1].tabGen = 0;
        }
        return;
    }
    if (plane == NULL) {
        plane = &sprPlanes[0];
        for (i = 1; i < SPR_SPLIT_PLANES; i++) {
            if (sprPlanes[i].used < plane->used) {
                plane = &sprPlanes[i];
            }
        }
        plane->key = fb;
        plane->tabCount = -1;
        memset(plane->row, 0, sizeof(plane->row));
    }
    plane->used = ++sprPlaneClock;
    if (SPR_TAB_GEN != 0 && y + 1 < VDP_SHOT_ROWS) {
        sprRecordTable(vdp, plane, y + 1, from);
    }
    if (SPR_TAB_GEN != 0 && y >= 0) {
        sprRecordPalette(vdp, plane, y, from);
    }
    if (y < 0 || SPR_SPLIT_GEN == 0) {
        return;
    }

    line = fb->line[y].buffer;
    memcpy(saved, line, sizeof(saved));
    savedShift    = fb->line[y].doubleWidth;
    savedScanline = frameBufferGetScanline();
    savedReScroll = vdp->reScroll;

    /* reScroll and X2 = 32 skip every sprite evaluation, so no status changes. */
    spritesEnable = 0;
    vdp->reScroll = 1;
    vdp->RefreshLine(vdp, vdp->curLine, -1, 32);
    vdp->reScroll = savedReScroll;
    spritesEnable = spritesSetting;

    shift = fb->line[y].doubleWidth;
    width = MIN(fb->maxWidth << shift, VDP_SHOT_WIDTH);
    memcpy(plane->bg[y], line, sizeof(plane->bg[y]));
    start = (UInt16*)RefreshLineStart(vdp->curLine);

    memcpy(line, saved, sizeof(saved));
    fb->line[y].doubleWidth = savedShift;
    frameBufferSetScanline(savedScanline);

    memset(plane->mask[y], 0, sizeof(plane->mask[y]));
    /* Text and blank lines evaluate no sprites; the line buffer is older. */
    sprites = getSpritesLine(vdp, vdp->curLine);
    noSprites = vdp->RefreshLine == RefreshLineBlank || vdp->RefreshLine == RefreshLine0 || vdp->RefreshLine == RefreshLine0Plus ||
                vdp->RefreshLine == RefreshLine0Mix || vdp->RefreshLine == RefreshLineTx80;
    if (noSprites) {
        start = NULL;
    }
    /* No sprite shows on a border, and the redraw stops before the right one:
    ** there, and on a whole line of a mode without sprites, bg is the line as drawn. */
    if (start != NULL && start >= line && start < line + VDP_SHOT_WIDTH) {
        int x0 = (int)(start - line);
        int x1 = MIN(x0 + (256 << shift), VDP_SHOT_WIDTH);
        for (i = 0; i < 256; i++) {
            for (x = x0 + (i << shift); sprites[i] && x < x0 + ((i + 1) << shift) && x < VDP_SHOT_WIDTH; x++) {
                plane->mask[y][x >> 3] |= 1 << (x & 7);
            }
        }
        if (shift == savedShift) {
            memcpy(plane->bg[y], saved, x0 * sizeof(UInt16));
            memcpy(plane->bg[y] + x1, saved + x1, (VDP_SHOT_WIDTH - x1) * sizeof(UInt16));
        }
    }
    else if (noSprites && shift == savedShift) {
        memcpy(plane->bg[y], saved, sizeof(plane->bg[y]));
    }

    /* Drawn in one piece, the line had the registers and VRAM it has now. */
    state = VDP_SPR_ROW_EXACT;
    if (shift != savedShift) {
        state = VDP_SPR_ROW_NONE;
    }
    else if (from >= 0) {
        for (x = 0; x < width; x++) {
            if (saved[x] != plane->bg[y][x] && !(plane->mask[y][x >> 3] & (1 << (x & 7)))) {
                state = VDP_SPR_ROW_APPROX;
                break;
            }
        }
    }
    row = &plane->row[y];
    row->gen        = SPR_SPLIT_GEN;
    row->owner      = (UInt32)vdp->debugHandle + 1;
    row->frameStart = vdp->frameStartTime;
    row->state      = (UInt8)state;
}

/* Row y of the shot's sprite split, from frame fb when recorded there while
** armed now; a row of the frame in progress must be of this frame. */
static void shotSprRow(VdpFrameShot* out, VDP* vdp, int y, FrameBuffer* fb, int inProgress)
{
    /* The plane's table each shot table came from, for this capture. */
    static const VdpSprTable* from[VDP_SPR_TABS];
    SprSplitPlane* plane = sprPlaneOf(fb);
    SprSplitRow* row = plane != NULL ? &plane->row[y] : NULL;
    int tabOk = row != NULL && SPR_TAB_GEN != 0 && row->tabGen == SPR_TAB_GEN && row->tabOwner == (UInt32)vdp->debugHandle + 1 &&
                row->tabFrame == plane->tabFrame && (!inProgress || row->tabFrame == vdp->frameStartTime);
    int i;

    out->sprTabRow[y] = 0xff;
    out->sprTabState[y] = VDP_SPR_ROW_NONE;
    if (tabOk && row->tabIndex == SPR_TAB_LOST) {
        out->sprTabLost++;
    }
    if (tabOk && row->tabIndex < plane->tabCount) {
        const VdpSprTable* t = &plane->tab[row->tabIndex];
        for (i = 0; i < out->sprTabCount && from[i] != t; i++) {
        }
        if (i == out->sprTabCount && i < VDP_SPR_TABS) {
            from[i] = t;
            out->sprTab[out->sprTabCount++] = *t;
        }
        if (i < out->sprTabCount) {
            out->sprTabRow[y] = (UInt8)i;
            out->sprTabState[y] = row->tabState;
        }
    }

    out->linePalState[y] = VDP_SPR_ROW_NONE;
    if (row != NULL && SPR_TAB_GEN != 0 && row->palGen == SPR_TAB_GEN && row->palOwner == (UInt32)vdp->debugHandle + 1 &&
        row->palFrame == plane->tabFrame && (!inProgress || row->palFrame == vdp->frameStartTime)) {
        out->linePalState[y] = row->palState;
        memcpy(out->linePal[y], plane->pal[y], sizeof(out->linePal[y]));
    }

    out->sprRow[y] = VDP_SPR_ROW_NONE;
    if (row == NULL || SPR_SPLIT_GEN == 0 || row->gen != SPR_SPLIT_GEN || row->owner != (UInt32)vdp->debugHandle + 1 ||
        (inProgress && row->frameStart != vdp->frameStartTime)) {
        return;
    }
    out->sprRow[y] = row->state;
    memcpy(out->sprMask[y], plane->mask[y], sizeof(out->sprMask[y]));
    memcpy(out->sprBg[y], plane->bg[y], sizeof(out->sprBg[y]));
}

/* Ended at a frame tap (flipped just before), at the end of active display (the bottom border
** from the frame before) and after an early flip (all rows the flipped frame). Else paused:
** the rows the VDP has drawn so far are the frame in progress, the others the one before. */
int vdpDebugCaptureFrame(int index, VdpFrameShot* out, int pixels)
{
    VDP* vdp = index == 0 ? theVdp : NULL;
    FrameBuffer* last;
    FrameBuffer* draw;
    FrameBuffer* head;
    UInt32 flips;
    int drawing;
    int y;

    out->valid = 0;
    out->pixels = pixels;
    if (vdp == NULL) {
        return 0;
    }
    last = frameBufferGetLastDrawnOf(vdp->frameBuffer, &out->seq, &flips);
    draw = frameBufferGetDrawFrameOf(vdp->frameBuffer);
    drawing = vdp->videoEnabled && displayEnable && frameBufferGetActiveDraw() == vdp->frameBuffer;

    out->stale        = !drawing || out->seq != flips;
    /* Ended at a frame end only once it has run; rasterCapture goes by the
    ** time alone, so a pause right at the end can differ there. */
    out->ended        = vdpAtShown || vdp->flippedEarly ||
                        ((Int32)(boardSystemTime() - vdp->timeDisplay) >= 0 &&
                         vdp->curLine == 0 && vdp->lineOffset < 0);
    out->drawnRow     = 0;
    out->drawnSamples = 0;
    out->frameStart   = vdp->frameStartTime;
    out->hAdjust      = vdp->HAdjust;
    out->hAdjustSc0   = vdp->hAdjustSc0;
    if (vdpAtShown) {
        /* The frame the next flip completes. */
        out->seq = flips + 1;
    }
    if (out->ended && !vdpAtShown && !vdp->flippedEarly) {
        /* The layout fields have moved on to the next frame already. */
        int lines = (int)((vdp->timeDisplay - vdp->frameStartTime) / HPERIOD);
        out->displayOffset = lines >= 313 ? 27 : 0;
        out->firstLine     = vdp->firstLine - vdp->displayOffest + out->displayOffset;
    }
    else {
        out->displayOffset = vdp->displayOffest;
        out->firstLine     = vdp->firstLine;
        if (!vdp->flippedEarly && drawing && draw != NULL && vdp->curLine >= vdp->displayOffest) {
            out->drawnRow = MIN(vdp->curLine - vdp->displayOffest, VDP_SHOT_ROWS);
            if (out->drawnRow < VDP_SHOT_ROWS) {
                out->drawnSamples = shotDrawnSamples(vdp, draw, out->drawnRow);
            }
        }
    }

    head = out->drawnRow > 0 || out->drawnSamples > 0 ? draw : last;
    if (head == NULL) {
        return 0;
    }
    out->restMissing     = last == NULL;
    out->rows            = MIN(head->lines, VDP_SHOT_ROWS);
    out->maxWidth        = head->maxWidth;
    out->interlace       = head->interlace;
    out->interlaceRaster = head->interlaceRaster;

    for (y = 0; y < out->rows; y++) {
        if (y < out->drawnRow) {
            shotCopy(out, y, draw, 0, VDP_SHOT_WIDTH);
        }
        else if (y == out->drawnRow && out->drawnSamples > 0) {
            int shift = draw->line[y].doubleWidth;
            if (last != NULL && last->line[y].doubleWidth == shift) {
                shotCopy(out, y, last, out->drawnSamples, VDP_SHOT_WIDTH);
            }
            else {
                shotKey(out, y, shift);
            }
            shotCopy(out, y, draw, 0, out->drawnSamples);
        }
        else if (last != NULL) {
            shotCopy(out, y, last, 0, VDP_SHOT_WIDTH);
        }
        else {
            shotKey(out, y, 0);
        }
    }
    out->sprSplit = SPR_SPLIT_GEN != 0;
    out->sprTabCount = 0;
    out->sprTabLost = 0;
    out->sprUseCount = 0;
    out->sprUseSince = 0;
    if (SPR_TAB_GEN != 0 && sprUseGen == SPR_TAB_GEN && sprUseOwner == (UInt32)vdp->debugHandle + 1) {
        out->sprUseCount = sprUseCount;
        out->sprUseSince = sprUseSince;
        memcpy(out->sprUse, sprUses, sizeof(sprUses));
    }
    out->linePalValid = SPR_TAB_GEN != 0;
    for (y = 0; vdpSprSplitArmed && y < out->rows; y++) {
        if (y < out->drawnRow || (y == out->drawnRow && out->drawnSamples >= VDP_SHOT_WIDTH)) {
            shotSprRow(out, vdp, y, draw, 1);
        }
        else if (y == out->drawnRow && out->drawnSamples > 0) {
            out->sprRow[y] = VDP_SPR_ROW_NONE;
            out->sprTabRow[y] = 0xff;
            out->linePalState[y] = VDP_SPR_ROW_NONE;
        }
        else {
            shotSprRow(out, vdp, y, last, 0);
        }
    }
    out->valid = 1;
    return 1;
}

static void getDebugInfo(VDP* vdp, DbgDevice* dbgDevice)
{
    DbgRegisterBank* regBank;
    DbgIoPorts* ioPorts;
    int cmdRegCount;
    int regCount;
    int i;
    int extraRegCount = 3;
    int statusRegCount;
    int paletteCount;
    int scanLine;
    int lineTime;
    int frameTime;
    int regOffset = 0;
    char* vdpVersionName;

    switch (vdp->vdpVersion) {
    case VDP_TMS9929A:
        vdpVersionName = "TMS9929A";
        break;
    case VDP_TMS99x8A:
        vdpVersionName = "TMS99x8A";
        break;
    case VDP_TMS9918A:
        vdpVersionName = "TMS9918A";
        break;
    case VDP_V9938:
        vdpVersionName = "V9938";
        break;
    case VDP_V9958:
        vdpVersionName = "V9958";
        break;
    case VDP_V9968:
        vdpVersionName = "V9968";
        break;
    default:
        vdpVersionName = "VDP";
        break;
    }

    sync(vdp, boardSystemTime());

    dbgDeviceAddMemoryBlock(dbgDevice, langDbgMemVram(), 0, 0, vdp->vramSize, vdp->vram);

    dbgRegisterLayout(vdp, &regCount, &cmdRegCount, &paletteCount, &statusRegCount);

    regBank = dbgDeviceAddRegisterBank(dbgDevice, langDbgRegs(), 
                                       regCount + 
                                       cmdRegCount + 
                                       paletteCount +
                                       statusRegCount + 
                                       extraRegCount);

    for (i = 0; i < regCount; i++) {
        char reg[4];
        sprintf(reg, "R%d", i);
        dbgRegisterBankAddRegister(regBank, regOffset++, reg,  8, vdp->vdpRegs[i]);
    }

    for (i = 0; i < cmdRegCount; i++) {
        char reg[4];
        sprintf(reg, "R%d", 32 + i);
        dbgRegisterBankAddRegister(regBank, regOffset++, reg, 8, vdpCmdPeek(vdp->cmdEngine, i, boardSystemTime()));
    }

    for (i = 0; i < paletteCount; i++) {
        char reg[4];
        sprintf(reg, "P%d", i);
        dbgRegisterBankAddRegister(regBank, regOffset++, reg, 16, vdp->paletteReg[i]);
    }

    for (i = 0; i < statusRegCount; i++) {
        char reg[4];
        sprintf(reg, "S%d", i);
        dbgRegisterBankAddRegister(regBank, regOffset++, reg, 8, vdp->vdpStatus[i]);
    }

    frameTime = boardSystemTime() - vdp->frameStartTime;
    scanLine = frameTime / HPERIOD;
    lineTime = frameTime % HPERIOD;
    if (scanLine < 0) scanLine += vdpIsVideoPal(vdp) ? 313 : 262;
    dbgRegisterBankAddRegister(regBank, regOffset++, "VRMP", 16, vdp->vramAddress);
    dbgRegisterBankAddRegister(regBank, regOffset++, "SCAN", 8, scanLine);
    dbgRegisterBankAddRegister(regBank, regOffset++, "LNTM", 16, lineTime);

    // Add IO Ports
    switch (vdp->vdpConnector) {
    case VDP_MSX:
        if (vdpIsV99x8(vdp)) {
            ioPorts = dbgDeviceAddIoPorts(dbgDevice, vdpVersionName, vdpIsV9968(vdp) ? 5 : 4);
            dbgIoPortsAddPort(ioPorts, 0, 0x98, DBG_IO_READWRITE,  peek(vdp, 0x98));
            dbgIoPortsAddPort(ioPorts, 1, 0x99, DBG_IO_READWRITE,  peekStatus(vdp, 0x99));
            dbgIoPortsAddPort(ioPorts, 2, 0x9a, DBG_IO_WRITE, 0);
            dbgIoPortsAddPort(ioPorts, 3, 0x9b, DBG_IO_WRITE, 0);
            if (vdpIsV9968(vdp)) {
                dbgIoPortsAddPort(ioPorts, 4, 0x9c, DBG_IO_READWRITE, readInterruptStatus(vdp, 0x9c));
            }
        }
        else {
            ioPorts = dbgDeviceAddIoPorts(dbgDevice, vdpVersionName, 2);
            dbgIoPortsAddPort(ioPorts, 0, 0x98, DBG_IO_READWRITE,  peek(vdp, 0x98));
            dbgIoPortsAddPort(ioPorts, 1, 0x99, DBG_IO_READWRITE,  peekStatus(vdp, 0x99));
        }
        break;

    case VDP_SVI:
        ioPorts = dbgDeviceAddIoPorts(dbgDevice, vdpVersionName, 4);
        dbgIoPortsAddPort(ioPorts, 0, 0x80, DBG_IO_WRITE, 0);
        dbgIoPortsAddPort(ioPorts, 1, 0x81, DBG_IO_WRITE, 0);
        dbgIoPortsAddPort(ioPorts, 2, 0x84, DBG_IO_READ,  peek(vdp, 0x84));
        dbgIoPortsAddPort(ioPorts, 3, 0x85, DBG_IO_READ,  peekStatus(vdp, 0x85));
        break;

    case VDP_COLECO:
        ioPorts = dbgDeviceAddIoPorts(dbgDevice, vdpVersionName, 32);
        for (i = 0; i < 32; i += 2) {
            dbgIoPortsAddPort(ioPorts, 0,     i + 0xa0, DBG_IO_READWRITE, peek(vdp, i + 0xa0));
            dbgIoPortsAddPort(ioPorts, i + 1, i + 0xa1, DBG_IO_READWRITE, peekStatus(vdp, i + 0xa1));
        }
        break;

    case VDP_SG1000:
        ioPorts = dbgDeviceAddIoPorts(dbgDevice, vdpVersionName, 64);
		for (i=0; i<64; i+=2) {
			dbgIoPortsAddPort(ioPorts, 0, i+0x80, DBG_IO_READWRITE,  peek(vdp, i+0x80));
			dbgIoPortsAddPort(ioPorts, 1, i+0x81, DBG_IO_READWRITE,  peekStatus(vdp, i+0x81));
		}
        break;
    }
}

static int dbgWriteMemory(VDP* vdp, char* name, void* data, int start, int size)
{
    if (strcmp(name, "VRAM") || start + size > vdp->vramSize) {
        return 0;
    }

    memcpy(vdp->vram + start, data, size);

    return 1;
}

static int dbgWriteRegister(VDP* vdp, char* name, int regIndex, UInt32 value)
{
    int regCount;
    int cmdRegCount;
    int paletteCount;
    int statusRegCount;

    dbgRegisterLayout(vdp, &regCount, &cmdRegCount, &paletteCount, &statusRegCount);

    if (regIndex < 0) {
        return 0;
    }

    if (regIndex < regCount) {
        vdpUpdateRegisters(vdp, (UInt8)regIndex, (UInt8)value);
        return 1;
    }

    regIndex -= regCount;

    if (regIndex < cmdRegCount) {
        return 0;
    }

    regIndex -= cmdRegCount;

    if (regIndex < paletteCount) {
        value &= 0x0777;
        vdp->paletteReg[regIndex] = (UInt16)value;
        notePaletteReg(vdp, regIndex);

        /* Green lives in the high byte, the layout writePaletteLatch builds. */
        updatePalette(vdp, regIndex, (value & 0x0070) * 255 / 112,
                                     ((value >> 8) & 0x07) * 255 / 7,
                                     (value & 0x0007) * 255 / 7);
        return 1;
    }

    regIndex -= paletteCount;

    /* The status registers sit here and are read only. Without the skip S0
    ** would land on the VRMP case below and corrupt the VRAM address. */
    if (regIndex < statusRegCount) {
        return 0;
    }

    regIndex -= statusRegCount;

    if (regIndex == 0) { // VRMP
        vdp->vramAddress = (UInt16)value & 0x3fff;
        return 1;
    }

    return 0;
}

static void reset(VDP* vdp)
{
    int i;

    RefreshLineReset();

    vdp->frameStartTime  = boardSystemTime();
    vdp->timeDisplay     = boardSystemTime();
    vdp->scr0splitLine   = 0;
    vdp->vramPage        = 0;
    vdp->sprGenBase      = 0;
    vdp->sprTabBase      = 0;
    vdp->colTabBase      = 0;
    vdp->chrTabBase      = 0;
    vdp->chrGenBase      = 0;
    vdp->palKey          = 0;
    vdp->vdpKey          = 0;
	vdp->vramAddress     = 0;
	vdp->vdpData         = 0;
	vdp->vdpDataLatch    = 0;
    vdp->FGColor         = 0;
    vdp->BGColor         = 0;
    vdp->XFGColor        = 0;
    vdp->XBGColor        = 0;
    vdp->blinkFlag       = 0;
    vdp->blinkCnt        = 0;
    vdp->blinkLineBase   = 0;
    vdp->drawArea        = 0;
    vdp->cmdDrawArea     = 0;
    vdp->lastLine        = 0;
    vdp->displayOffest   = 0;
    vdp->screenOn        = 0;
    vdp->VAdjust         = 0;
    vdp->HAdjust         = 0;
    vdp->displayArea        = 0;

    vdp->leftBorder      = 200;
    vdp->screenMode      = 1;
    vdp->lineOffset      = 256;
    vdp->firstLine       = 1;
    vdp->lastLine        = -1;
    vdp->screenOffTime = boardSystemTime();
    
    vdp->timeScrModeEn       = 0;
    vdp->timeHintEn          = 0;
    vdp->timeVintEn          = 0;
    vdp->timeDrawAreaStartEn = 0;
    vdp->timeVStartEn        = 0;
    vdp->timeDisplayEn       = 0;

    memset(vdp->vdpStatus, 0, sizeof(vdp->vdpStatus));
    memset(vdp->vdpRegs, 0, sizeof(vdp->vdpRegs));

    vdp->extRegsLocked = 0;

    vdp->vdpStatus[0] = 0x9f;
    vdp->vdpStatus[1] = (vdp->vdpVersion == VDP_V9958 || vdpIsV9968(vdp)) ? 0x04 : 0;
    vdp->vdpStatus[2] = 0x6c;
        
    vdp->vdpRegs[1]  = 0x10;
    vdp->vdpRegs[2]  = 0xff;
    vdp->vdpRegs[3]  = 0xff;
    vdp->vdpRegs[4]  = 0xff;
    vdp->vdpRegs[5]  = 0xff;
    vdp->vdpRegs[8]  = 0x08;
    vdp->vdpRegs[9]  = (0x02 & vdp->palMask) | vdp->palValue;
    vdp->vdpRegs[21] = 0x3b;
    vdp->vdpRegs[22] = 0x05;

    if (vdp->vdpVersion == VDP_TMS9918A) {
        for (i = 0; i < 16; i++) {
            updatePalette(vdp, i, msx1Palette_9918a[i].r, msx1Palette_9918a[i].g, msx1Palette_9918a[i].b);
        }
    }
    else if (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A) {
        for (i = 0; i < 16; i++) {
            updatePalette(vdp, i, msx1Palette[i].r, msx1Palette[i].g, msx1Palette[i].b);
        }
    }
    else {
        for (i = 0; i < 16; i++) {
            updatePalette(vdp, i, msx2Palette[i].r, msx2Palette[i].g, msx2Palette[i].b);
        }
    }

    /* The 16 boot colours are repeated across all 16 sets. The five bit copy
    ** has to be laid down too: it is what a save state carries, and three bits
    ** reach five as v2 v1 v0 v2 v1. */
    for (i = 0; i < 256; i++) {
        int reg = defaultPaletteRegs[i & 0x0f];
        int r   = (reg >> 4) & 7;
        int g   = (reg >> 8) & 7;
        int b   =  reg       & 7;

        vdp->paletteExt[i]    = vdp->palette[i & 0x0f];
        vdp->paletteExtReg[i] = (UInt16)((((r << 2) | (r >> 1)) << 10) |
                                         (((g << 2) | (g >> 1)) <<  5) |
                                          ((b << 2) | (b >> 1)));
    }

    /* The engine keeps its own copy of R#45, so a stale one would decide the
    ** addressing of the next command the guest never asked for. */
    vdpCmdWrite(vdp->cmdEngine, 0x0d, 0, boardSystemTime());
    vdpUpdateVramMode(vdp);

    memcpy(vdp->paletteReg, defaultPaletteRegs, sizeof(vdp->paletteReg));

    onScrModeChange(vdp, boardSystemTime());

    onDisplay(vdp, boardSystemTime());
}

static void destroy(VDP* vdp) 
{
    int i;

    theVdp = NULL;

    debugDeviceUnregister(vdp->debugHandle);
    deviceManagerUnregister(vdp->deviceHandle);
    videoManagerUnregister(vdp->videoHandle);

    switch (vdp->vdpConnector) {
    case VDP_MSX:
        ioPortUnregister(0x98, vdp);
        ioPortUnregister(0x99, vdp);
        ioPortUnregister(0x9a, vdp);
        ioPortUnregister(0x9b, vdp);
        if (vdpIsV9968(vdp)) {
            ioPortUnregister(0x9c, vdp);
        }
        break;

    case VDP_SVI:
        ioPortUnregister(0x80, vdp);
        ioPortUnregister(0x81, vdp);
        ioPortUnregister(0x84, vdp);
        ioPortUnregister(0x85, vdp);
        break;

    case VDP_COLECO:
        for (i = 0xa0; i < 0xc0; i++) {
            ioPortUnregister(i, vdp);
        }
        break;

    case VDP_SG1000:
        for (i = 0x80; i < 0xc0; i++) {
            ioPortUnregister(i, vdp);
        }
        break;
    }

    boardTimerDestroy(vdp->timerDisplay);
    boardTimerDestroy(vdp->timerDrawAreaStart);
    boardTimerDestroy(vdp->timerVStart);
    boardTimerDestroy(vdp->timerScrModeChange);
    boardTimerDestroy(vdp->timerHint);
    boardTimerDestroy(vdp->timerVint);
    boardTimerDestroy(vdp->timerTmsVint);
    boardTimerDestroy(vdp->timerDrawAreaEnd);

    vdpCmdDestroy(vdp->cmdEngine);

    frameBufferDataDestroy(vdp->frameBuffer);

    free(vdp);
}

static void videoEnable(VDP* vdp)
{
    vdp->videoEnabled = 1;
}

static void videoDisable(VDP* vdp)
{
    vdp->videoEnabled = 0;
}

void vdpCreate(VdpConnector connector, VdpVersion version, VdpSyncMode sync, int vramPages)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, dbgWriteMemory, dbgWriteRegister, NULL };
    VideoCallbacks videoCallbacks = { videoEnable, videoDisable };
    char* vdpVersionString;
    int vramSize;
    int i;

    VDP* vdp = (VDP*)calloc(1, sizeof(VDP));

    theVdp = vdp;

    initPalette(vdp);

    vdp->deviceHandle = deviceManagerRegister(ROM_V9958, &callbacks, vdp);

    vdp->timerDisplay       = boardTimerCreate(onDisplay, vdp);
    vdp->timerDrawAreaStart = boardTimerCreate(onDrawAreaStart, vdp);
    vdp->timerVStart        = boardTimerCreate(onVStart, vdp);
    vdp->timerScrModeChange = boardTimerCreate(onScrModeChange, vdp);
    vdp->timerHint          = boardTimerCreate(onHint, vdp);
    vdp->timerVint          = boardTimerCreate(onVint, vdp);
    vdp->timerDrawAreaEnd   = boardTimerCreate(onDrawAreaEnd, vdp);
    vdp->timerTmsVint       = boardTimerCreate(onTmsVint, vdp);

    vdp->RefreshLine = RefreshLine0; 

    vdp->vramSize       = vramPages << 14;

    vdp->vram192        = vramPages == 12;
    vdp->vram16         = vramPages == 1;
    
    if (vramPages < 4) {
        vramPages = 4;
    }

    vramSize            = vramPages << 14;
    vdp->vramOffsets[0] = 0;
    vdp->vramOffsets[1] = vramSize > 0x20000 ? 0x20000 : 0;
    vdp->vramMasks[0]   = vramSize > 0x8000  ? 0x7fff  : vramSize - 1;
    vdp->vramMasks[1]   = vramSize > 0x8000  ? 0x7fff  : vramSize - 1;
    vdp->vramMasks[2]   = vramSize > 0x20000 ? 0x1ffff : vramSize - 1;
    vdp->vramMasks[3]   = vramSize > 0x20000 ? 0xffff  : vramSize - 1;
    if (version == VDP_V9968) {
        vdp->vramOffsets[1] = 0;
        vdp->vramMasks[2]   = vramSize > 0x20000 ? 0x1ffff : vramSize - 1;
        vdp->vramMasks[3]   = vdp->vramMasks[2];
    }
    vdp->vramPtr        = vdp->vram + vdp->vramOffsets[0];
    vdp->vramAccMask    = vdp->vramMasks[2];
    vdp->vramEnable     = 1;

    if (vramPages > (version == VDP_V9968 ? 16 : 8)) {
        vramPages = version == VDP_V9968 ? 16 : 8;
    }

    vdp->vramPages     = vramPages;
    vdp->vram128       = vramPages >= 8 ? 0x10000 : 0;
    vdp->vram256       = vramPages > 8;
    vdp->vramMask      = (vramPages << 14) - 1;
    vdp->vdpVersion    = version;
    vdp->vdpConnector  = connector;

    vdp->frameBuffer   = frameBufferDataCreate(8 + 256 + 8, 240, 1);
    vdp->videoHandle   = videoManagerRegister(VdpNames[version], vdp->frameBuffer, &videoCallbacks, vdp);

    if (sync == VDP_SYNC_AUTO) {
        vdp->palMask  = ~0;
        vdp->palValue = 0;
    }
    else if (sync == VDP_SYNC_50HZ) {
        vdp->palMask  = ~0x02;
        vdp->palValue = 0x02;
    }
    else if (sync == VDP_SYNC_60HZ) {
        vdp->palMask  = ~0x02;
        vdp->palValue = 0x00;
    }

    memset(vdp->vram, 0, VRAM_SIZE);
    vdp->cmdEngine = vdpCmdCreate(vramSize, vdp->vram, boardSystemTime());
    vdpCmdSetExpansionWindow(vdp->cmdEngine, version != VDP_V9968);

    reset(vdp);

    switch (vdp->vdpVersion) {
    case VDP_TMS9929A:
        vdp->registerValueMask = registerValueMaskMSX1;
        vdp->registerMask      = 0x07;
        vdpVersionString       = langDbgDevTms9929A();
        vdp->hAdjustSc0        = -2; // 6
        break;
    case VDP_TMS99x8A:
        vdp->registerValueMask = registerValueMaskMSX1;
        vdp->registerMask      = 0x07;
        vdp->vdpRegs[9]          &= ~0x02;
        vdpVersionString       = langDbgDevTms99x8A();
        vdp->hAdjustSc0        = -2; // 6
        break;
    case VDP_TMS9918A:
        vdp->registerValueMask = registerValueMaskMSX1;
        vdp->registerMask      = 0x07;
        vdp->vdpRegs[9]          &= ~0x02;
        vdpVersionString       = langDbgDevTms9918A();
        vdp->hAdjustSc0        = -2; // 6
        break;
    case VDP_V9938:
        vdp->registerValueMask = registerValueMaskMSX2;
        vdp->registerMask      = 0x3f;
        vdpVersionString       = langDbgDevV9938();
        vdp->hAdjustSc0        = 1; // 9
        break;
    case VDP_V9958:
        vdp->registerValueMask = registerValueMaskMSX2p;
        vdp->registerMask      = 0x3f;
        vdpVersionString       = langDbgDevV9958();
        vdp->hAdjustSc0        = 1; // 9
        break;
    case VDP_V9968:
        vdp->registerValueMask = registerValueMaskV9968;
        vdp->registerMask      = 0x3f;
        vdpVersionString       = langDbgDevV9968();
        vdp->hAdjustSc0        = 1; // 9
        break;
    }
    
    vdp->debugHandle = debugDeviceRegister(DBGTYPE_VIDEO, vdpVersionString, &dbgCallbacks, vdp);

    switch (vdp->vdpConnector) {
    case VDP_MSX:
        ioPortRegister(0x98, read,       write,      vdp);
        ioPortRegister(0x99, readStatus, writeLatch, vdp);
        if (vdpIsV99x8(vdp)) {
            ioPortRegister(0x9a, NULL, writePaletteLatch, vdp);
            ioPortRegister(0x9b, NULL, writeRegister,     vdp);
        }
        if (vdpIsV9968(vdp)) {
            ioPortRegister(0x9c, readInterruptStatus, writeInterruptStatus, vdp);
        }
        break;

    case VDP_SVI:
        ioPortRegister(0x80, NULL,       write,      vdp); // vdp->vdpRegs vdp->vram Write
        ioPortRegister(0x81, NULL,       writeLatch, vdp); // vdp->vdpRegs Address Latch
        ioPortRegister(0x84, read,       NULL,       vdp); // vdp->vdpRegs vdp->vram Read
        ioPortRegister(0x85, readStatus, NULL,       vdp); // vdp->vdpRegs Status Read
        break;

    case VDP_COLECO:
        for (i = 0xa0; i < 0xc0; i += 2) {
            ioPortRegister(i,     read,       write,      vdp);
            ioPortRegister(i + 1, readStatus, writeLatch, vdp);
        }
        break;
        
    case VDP_SG1000:
        ioPortRegister(0xbe, read,       write,      vdp);
        ioPortRegister(0xbf, readStatus, writeLatch, vdp);
        break;
    }
}


