/*****************************************************************************
**
** Yamaha V9990 command engine.
** Parts follow or are adapted from openMSX's V9990 implementation.
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
#include "V9990Cmd.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>

#define OP_STOP   0x0
#define OP_LMMC   0x1
#define OP_LMMV   0x2
#define OP_LMCM   0x3
#define OP_LMMM   0x4
#define OP_CMMC   0x5
#define OP_CMMK   0x6
#define OP_CMMM   0x7
#define OP_BMXL   0x8
#define OP_BMLX   0x9
#define OP_BMLL   0xa
#define OP_LINE   0xb
#define OP_SRCH   0xc
#define OP_POINT  0xd
#define OP_PSET   0xe
#define OP_ADVANCE 0xf

#define ARG_DIY 0x08
#define ARG_DIX 0x04
#define ARG_NEQ 0x02
#define ARG_MAJ 0x01
#define LOP_TP  0x10

#define MOVE_SRC 1
#define MOVE_DST 2

/* Physical bank of VRAM1, which holds the high byte of WM, FC and BC. */
#define V9990_BANK1 0x40000

struct V9990Cmd {
    UInt8* vram;
    V9990CmdEndCallback onEnd;
    void*  ref;

    /* Parameter registers. Commands move the coordinates as they draw, so
    ** a later command that does not reload them continues from there. */
    UInt16 sx;
    UInt16 sy;
    UInt16 dx;
    UInt16 dy;
    UInt16 nx;
    UInt16 ny;
    UInt8  arg;
    UInt8  lop;
    UInt16 wm;
    UInt16 fc;
    UInt16 bc;

    UInt8  opcode;
    UInt8  status;
    UInt16 borderX;

    /* Taken from the display mode at the opcode write. */
    V9990Mode mode;
    int    bpp;
    int    width;

    /* time is how far the command has run; it only moves while it runs. */
    UInt32 time;
    int    waiting;
    int    anx;
    int    any;
    UInt16 ax;
    UInt16 err;
    UInt32 count;
    UInt32 srcAddress;
    UInt32 dstAddress;
    UInt8  data;
    UInt8  partial;
    int    bits;
    int    lastDot;
    int    lastByte;
};

/* Command timing is not emulated: every step takes no time. */
static UInt32 stepCost(V9990Cmd* cmd)
{
    return 0;
}

static UInt8 logOp(UInt8 lop, UInt8 src, UInt8 dst)
{
    UInt8 result = 0;

    if (lop & 0x01) result |= ~src & ~dst;
    if (lop & 0x02) result |= ~src &  dst;
    if (lop & 0x04) result |=  src & ~dst;
    if (lop & 0x08) result |=  src &  dst;
    return result;
}

static UInt32 mapAddress(V9990Cmd* cmd, UInt32 address)
{
    return v9990MapAddress(cmd->mode, address);
}

static UInt16 pixelMask(V9990Cmd* cmd)
{
    return (UInt16)((1 << cmd->bpp) - 1);
}

/* Logical byte address of dot (x, y), and where the dot sits in that byte.
** A 16 bpp dot is the byte pair at the address. In P1, X bit 9 selects
** layer B, which is VRAM1. */
static UInt32 pixelAddress(V9990Cmd* cmd, UInt32 x, UInt32 y, int* shift)
{
    UInt32 pitch;

    if (cmd->bpp == 16) {
        pitch  = cmd->width * 2;
        *shift = 0;
        return (((2 * x) & (pitch - 1)) + y * pitch) & V9990_VRAM_MASK;
    }

    *shift = 8 - cmd->bpp - (int)((x * cmd->bpp) & 7);
    if (cmd->mode == V9990_MODE_P1) {
        pitch = cmd->width / 2;
        return ((((x >> 1) & (pitch - 1)) + y * pitch) & 0x3ffff) | ((x & 0x200) << 9);
    }
    pitch = cmd->width * cmd->bpp / 8;
    return (((x * cmd->bpp / 8) & (pitch - 1)) + y * pitch) & V9990_VRAM_MASK;
}

static UInt16 readPixelAt(V9990Cmd* cmd, UInt32 address, int shift)
{
    if (cmd->bpp == 16) {
        return (UInt16)(cmd->vram[mapAddress(cmd, address)] |
                        (cmd->vram[mapAddress(cmd, address + 1)] << 8));
    }
    return (UInt16)((cmd->vram[mapAddress(cmd, address)] >> shift) & pixelMask(cmd));
}

static UInt16 readPixel(V9990Cmd* cmd, UInt32 x, UInt32 y)
{
    int shift;
    UInt32 address = pixelAddress(cmd, x, y, &shift);

    return readPixelAt(cmd, address, shift);
}

/* Writes the bits of value selected by bits through LOP and the write mask
** of the bank. */
static void writeByte(V9990Cmd* cmd, UInt32 phys, UInt8 value, UInt8 bits)
{
    UInt8 old  = cmd->vram[phys];
    UInt8 mask = bits & (UInt8)((phys & V9990_BANK1) ? cmd->wm >> 8 : cmd->wm);

    cmd->vram[phys] = (UInt8)((old & ~mask) | (logOp(cmd->lop, value, old) & mask));
}

static void writePixelAt(V9990Cmd* cmd, UInt32 address, int shift, UInt16 value)
{
    if ((cmd->lop & LOP_TP) && value == 0) {
        return;
    }
    if (cmd->bpp == 16) {
        writeByte(cmd, mapAddress(cmd, address),     (UInt8)value,        0xff);
        writeByte(cmd, mapAddress(cmd, address + 1), (UInt8)(value >> 8), 0xff);
        return;
    }
    writeByte(cmd, mapAddress(cmd, address), (UInt8)(value << shift),
              (UInt8)(pixelMask(cmd) << shift));
}

static void writePixel(V9990Cmd* cmd, UInt32 x, UInt32 y, UInt16 value)
{
    int shift;
    UInt32 address = pixelAddress(cmd, x, y, &shift);

    writePixelAt(cmd, address, shift, value);
}

/* The dot value a colour register (FC, BC) gives at a dot: the register
** byte of the dot's bank, then the dot's bits in that byte. */
static UInt16 colorAt(V9990Cmd* cmd, UInt32 address, int shift, UInt16 color)
{
    UInt8 byte;

    if (cmd->bpp == 16) {
        return color;
    }
    byte = (UInt8)((mapAddress(cmd, address) & V9990_BANK1) ? color >> 8 : color);
    return (UInt16)((byte >> shift) & pixelMask(cmd));
}

static void drawColor(V9990Cmd* cmd, UInt32 x, UInt32 y, UInt16 color)
{
    int shift;
    UInt32 address = pixelAddress(cmd, x, y, &shift);

    writePixelAt(cmd, address, shift, colorAt(cmd, address, shift, color));
}

/* A byte at a linear address, which always uses the bitmap mapping. TP
** works on the whole byte. */
static void writeLinear(V9990Cmd* cmd, UInt32 address, UInt8 value)
{
    if ((cmd->lop & LOP_TP) && value == 0) {
        return;
    }
    writeByte(cmd, V9990_MAP_BX(address), value, 0xff);
}

static UInt8 readLinear(V9990Cmd* cmd, UInt32 address)
{
    return cmd->vram[V9990_MAP_BX(address)];
}

static UInt32 linearAddress(UInt16 low, UInt16 high)
{
    return (low & 0xff) | ((UInt32)(high & 0x7ff) << 8);
}

/* NX is 11 bits for the rectangle commands; 0 means 2048. */
static int wrappedNx(V9990Cmd* cmd)
{
    int nx = cmd->nx & 0x7ff;

    return nx != 0 ? nx : 2048;
}

static int wrappedNy(V9990Cmd* cmd)
{
    return cmd->ny != 0 ? cmd->ny : 4096;
}

static void moveX(V9990Cmd* cmd, int which, int delta)
{
    if (which & MOVE_SRC) cmd->sx = (UInt16)((cmd->sx + delta) & 0x7ff);
    if (which & MOVE_DST) cmd->dx = (UInt16)((cmd->dx + delta) & 0x7ff);
}

static void moveY(V9990Cmd* cmd, int which, int delta)
{
    if (which & MOVE_SRC) cmd->sy = (UInt16)((cmd->sy + delta) & 0xfff);
    if (which & MOVE_DST) cmd->dy = (UInt16)((cmd->dy + delta) & 0xfff);
}

/* Moves the source and/or destination to the next dot of the rectangle.
** Returns 1 after its last dot. */
static int nextDot(V9990Cmd* cmd, int which)
{
    int stepX = (cmd->arg & ARG_DIX) ? -1 : 1;
    int stepY = (cmd->arg & ARG_DIY) ? -1 : 1;

    moveX(cmd, which, stepX);
    if (--cmd->anx > 0) {
        return 0;
    }
    moveX(cmd, which, -stepX * wrappedNx(cmd));
    moveY(cmd, which, stepY);
    if (--cmd->any == 0) {
        return 1;
    }
    cmd->anx = wrappedNx(cmd);
    return 0;
}

static void finish(V9990Cmd* cmd)
{
    cmd->opcode  = 0;
    cmd->status &= ~(V9990_CMD_CE | V9990_CMD_TR);
    cmd->onEnd(cmd->ref);
}

/* PSET and ADVANCE move the pointer by the low opcode bits. */
static void advancePointer(V9990Cmd* cmd)
{
    if (cmd->opcode & 0x01) {
        moveX(cmd, MOVE_DST, (cmd->opcode & 0x02) ? -1 : 1);
    }
    if (cmd->opcode & 0x04) {
        moveY(cmd, MOVE_DST, (cmd->opcode & 0x08) ? -1 : 1);
    }
}

/* LMMC and CMMC take one CPU byte per step. TR = 0 means the CPU has
** written a byte that is not used yet. */
static int stepLmmc(V9990Cmd* cmd)
{
    int i;

    if (cmd->status & V9990_CMD_TR) {
        return 0;
    }
    cmd->status |= V9990_CMD_TR;

    if (cmd->bpp == 16) {
        if (!cmd->bits) {
            cmd->partial = cmd->data;
            cmd->bits    = 1;
            return 1;
        }
        cmd->bits = 0;
        writePixel(cmd, cmd->dx, cmd->dy, (UInt16)(cmd->partial | (cmd->data << 8)));
        if (nextDot(cmd, MOVE_DST)) {
            finish(cmd);
        }
        return 1;
    }

    for (i = 0; i < 8; i += cmd->bpp) {
        writePixel(cmd, cmd->dx, cmd->dy, (UInt16)((cmd->data >> (8 - cmd->bpp - i)) & pixelMask(cmd)));
        if (nextDot(cmd, MOVE_DST)) {
            finish(cmd);
            break;
        }
    }
    return 1;
}

static int stepCmmc(V9990Cmd* cmd)
{
    int i;

    if (cmd->status & V9990_CMD_TR) {
        return 0;
    }
    cmd->status |= V9990_CMD_TR;

    for (i = 7; i >= 0; i--) {
        drawColor(cmd, cmd->dx, cmd->dy, ((cmd->data >> i) & 1) ? cmd->fc : cmd->bc);
        if (nextDot(cmd, MOVE_DST)) {
            finish(cmd);
            break;
        }
    }
    return 1;
}

/* LMCM offers one byte per step; TR = 1 means the CPU has not read it. */
static int stepLmcm(V9990Cmd* cmd)
{
    UInt16 value;
    int i;

    if (cmd->status & V9990_CMD_TR) {
        return 0;
    }
    cmd->status |= V9990_CMD_TR;

    if (cmd->bpp == 16 && cmd->bits) {
        cmd->data     = cmd->partial;
        cmd->bits     = 0;
        cmd->lastByte = cmd->lastDot;
        return 1;
    }
    if (cmd->lastDot) {
        /* A P#2 write took TR away: offer the byte again. */
        return 1;
    }
    if (cmd->bpp == 16) {
        value = readPixel(cmd, cmd->sx, cmd->sy);
        cmd->data    = (UInt8)value;
        cmd->partial = (UInt8)(value >> 8);
        cmd->bits    = 1;
        cmd->lastDot = nextDot(cmd, MOVE_SRC);
        return 1;
    }

    /* Dots beyond the last one in a byte read as 0. */
    cmd->data = 0;
    for (i = 0; i < 8; i += cmd->bpp) {
        cmd->data |= (UInt8)(readPixel(cmd, cmd->sx, cmd->sy) << (8 - cmd->bpp - i));
        if (nextDot(cmd, MOVE_SRC)) {
            cmd->lastDot = 1;
            break;
        }
    }
    cmd->lastByte = cmd->lastDot;
    return 1;
}

/* The dot is returned in the top bits of the byte; the bits below hold
** the dots after it in the same VRAM byte. */
static int stepPoint(V9990Cmd* cmd)
{
    UInt32 address;
    UInt16 value;
    int shift;

    if (cmd->status & V9990_CMD_TR) {
        return 0;
    }
    cmd->status |= V9990_CMD_TR;

    if (!cmd->lastDot) {
        cmd->lastDot = 1;
        address = pixelAddress(cmd, cmd->sx, cmd->sy, &shift);
        if (cmd->bpp == 16) {
            value = readPixelAt(cmd, address, shift);
            cmd->data    = (UInt8)value;
            cmd->partial = (UInt8)(value >> 8);
            cmd->bits    = 1;
        }
        else {
            cmd->data     = (UInt8)(cmd->vram[mapAddress(cmd, address)] << (8 - cmd->bpp - shift));
            cmd->lastByte = 1;
        }
    }
    else if (cmd->bits) {
        cmd->data     = cmd->partial;
        cmd->bits     = 0;
        cmd->lastByte = 1;
    }
    return 1;
}

static int stepLmmv(V9990Cmd* cmd)
{
    drawColor(cmd, cmd->dx, cmd->dy, cmd->fc);
    if (nextDot(cmd, MOVE_DST)) {
        finish(cmd);
    }
    return 1;
}

static int stepLmmm(V9990Cmd* cmd)
{
    writePixel(cmd, cmd->dx, cmd->dy, readPixel(cmd, cmd->sx, cmd->sy));
    if (nextDot(cmd, MOVE_SRC | MOVE_DST)) {
        finish(cmd);
    }
    return 1;
}

static int stepCmmm(V9990Cmd* cmd)
{
    if (cmd->bits == 0) {
        cmd->data       = readLinear(cmd, cmd->srcAddress);
        cmd->srcAddress = (cmd->srcAddress + 1) & V9990_VRAM_MASK;
        cmd->bits       = 8;
    }
    cmd->bits--;
    drawColor(cmd, cmd->dx, cmd->dy, ((cmd->data >> cmd->bits) & 1) ? cmd->fc : cmd->bc);
    if (nextDot(cmd, MOVE_DST)) {
        finish(cmd);
    }
    return 1;
}

static int stepBmxl(V9990Cmd* cmd)
{
    UInt16 value;

    if (cmd->bpp == 16) {
        value = (UInt16)(readLinear(cmd, cmd->srcAddress) | (readLinear(cmd, cmd->srcAddress + 1) << 8));
        cmd->srcAddress = (cmd->srcAddress + 2) & V9990_VRAM_MASK;
    }
    else {
        if (cmd->bits == 0) {
            cmd->data       = readLinear(cmd, cmd->srcAddress);
            cmd->srcAddress = (cmd->srcAddress + 1) & V9990_VRAM_MASK;
            cmd->bits       = 8;
        }
        cmd->bits -= cmd->bpp;
        value = (UInt16)((cmd->data >> cmd->bits) & pixelMask(cmd));
    }
    writePixel(cmd, cmd->dx, cmd->dy, value);
    if (nextDot(cmd, MOVE_DST)) {
        finish(cmd);
    }
    return 1;
}

/* Dots are packed into bytes; a partial last byte is written as well. */
static int stepBmlx(V9990Cmd* cmd)
{
    UInt16 value = readPixel(cmd, cmd->sx, cmd->sy);
    int last = nextDot(cmd, MOVE_SRC);

    if (cmd->bpp == 16) {
        writeLinear(cmd, cmd->dstAddress,     (UInt8)value);
        writeLinear(cmd, cmd->dstAddress + 1, (UInt8)(value >> 8));
        cmd->dstAddress = (cmd->dstAddress + 2) & V9990_VRAM_MASK;
    }
    else {
        cmd->data |= (UInt8)(value << (8 - cmd->bpp - cmd->bits));
        cmd->bits += cmd->bpp;
        if (cmd->bits == 8 || last) {
            writeLinear(cmd, cmd->dstAddress, cmd->data);
            cmd->dstAddress = (cmd->dstAddress + 1) & V9990_VRAM_MASK;
            cmd->data = 0;
            cmd->bits = 0;
        }
    }
    if (last) {
        finish(cmd);
    }
    return 1;
}

/* At 16 bpp BMLL moves words: the addresses and the count are in dots,
** and each word is the byte pair at the same offset in both banks. */
static int stepBmll(V9990Cmd* cmd)
{
    int delta = (cmd->arg & ARG_DIX) ? -1 : 1;
    UInt16 value;

    if (cmd->bpp == 16) {
        UInt32 src = cmd->srcAddress;
        UInt32 dst = cmd->dstAddress;

        value = (UInt16)(cmd->vram[src] | (cmd->vram[src | V9990_BANK1] << 8));
        if (!((cmd->lop & LOP_TP) && value == 0)) {
            writeByte(cmd, dst,               (UInt8)value,        0xff);
            writeByte(cmd, dst | V9990_BANK1, (UInt8)(value >> 8), 0xff);
        }
        cmd->srcAddress = (src + delta) & (V9990_BANK1 - 1);
        cmd->dstAddress = (dst + delta) & (V9990_BANK1 - 1);
    }
    else {
        writeLinear(cmd, cmd->dstAddress, readLinear(cmd, cmd->srcAddress));
        cmd->srcAddress = (cmd->srcAddress + delta) & V9990_VRAM_MASK;
        cmd->dstAddress = (cmd->dstAddress + delta) & V9990_VRAM_MASK;
    }
    if (--cmd->count == 0) {
        finish(cmd);
    }
    return 1;
}

/* MJ (NX, 12 bits) dots along the major axis plus the start dot. The line
** stops early when X leaves the image. */
static int stepLine(V9990Cmd* cmd)
{
    UInt16 mj = cmd->nx;
    UInt16 mi = cmd->ny;
    int stepX = (cmd->arg & ARG_DIX) ? -1 : 1;
    int stepY = (cmd->arg & ARG_DIY) ? -1 : 1;

    drawColor(cmd, cmd->ax, cmd->dy, cmd->fc);

    if (!(cmd->arg & ARG_MAJ)) {
        cmd->ax = (UInt16)(cmd->ax + stepX);
        if (cmd->err < mi) {
            cmd->err = (UInt16)(cmd->err + mj);
            moveY(cmd, MOVE_DST, stepY);
        }
    }
    else {
        moveY(cmd, MOVE_DST, stepY);
        if (cmd->err < mi) {
            cmd->err = (UInt16)(cmd->err + mj);
            cmd->ax  = (UInt16)(cmd->ax + stepX);
        }
    }
    cmd->err = (UInt16)(cmd->err - mi);

    if (cmd->count++ == mj || (cmd->ax & cmd->width)) {
        finish(cmd);
    }
    return 1;
}

static int stepSrch(V9990Cmd* cmd)
{
    int shift;
    UInt32 address = pixelAddress(cmd, cmd->ax, cmd->sy, &shift);
    int equal = readPixelAt(cmd, address, shift) == colorAt(cmd, address, shift, cmd->fc);

    if (equal != ((cmd->arg & ARG_NEQ) != 0)) {
        cmd->status |= V9990_CMD_BD;
        cmd->borderX = cmd->ax;
        finish(cmd);
        return 1;
    }
    cmd->ax = (UInt16)(cmd->ax + ((cmd->arg & ARG_DIX) ? -1 : 1));
    if (cmd->ax & cmd->width) {
        cmd->status &= ~V9990_CMD_BD;
        cmd->borderX = cmd->ax;
        finish(cmd);
    }
    return 1;
}

static int stepPset(V9990Cmd* cmd)
{
    if ((cmd->opcode >> 4) == OP_PSET) {
        drawColor(cmd, cmd->dx, cmd->dy, cmd->fc);
    }
    advancePointer(cmd);
    finish(cmd);
    return 1;
}

/* Runs one step of the command. Returns 0 while it waits for the CPU. */
static int step(V9990Cmd* cmd)
{
    switch (cmd->opcode >> 4) {
    case OP_LMMC:    return stepLmmc(cmd);
    case OP_LMMV:    return stepLmmv(cmd);
    case OP_LMCM:    return stepLmcm(cmd);
    case OP_LMMM:    return stepLmmm(cmd);
    case OP_CMMC:    return stepCmmc(cmd);
    case OP_CMMM:    return stepCmmm(cmd);
    case OP_BMXL:    return stepBmxl(cmd);
    case OP_BMLX:    return stepBmlx(cmd);
    case OP_BMLL:    return stepBmll(cmd);
    case OP_LINE:    return stepLine(cmd);
    case OP_SRCH:    return stepSrch(cmd);
    case OP_POINT:   return stepPoint(cmd);
    case OP_PSET:
    case OP_ADVANCE: return stepPset(cmd);
    }
    finish(cmd);
    return 1;
}

void v9990CmdSync(V9990Cmd* cmd, UInt32 time)
{
    while ((cmd->status & V9990_CMD_CE) && (Int32)(time - cmd->time) >= 0) {
        cmd->waiting = !step(cmd);
        if (cmd->waiting) {
            break;
        }
        cmd->time += stepCost(cmd);
    }
}

/* A CPU access lets a waiting command go on from now, not from when it
** started to wait. */
static void resume(V9990Cmd* cmd, UInt32 time)
{
    if (cmd->waiting) {
        cmd->time = time;
    }
}

void v9990CmdWriteRegister(V9990Cmd* cmd, int reg, UInt8 value, UInt32 time)
{
    v9990CmdSync(cmd, time);

    switch (reg) {
    case 32: cmd->sx = (UInt16)((cmd->sx & 0x700) | value);               break;
    case 33: cmd->sx = (UInt16)((cmd->sx & 0x0ff) | ((value & 0x07) << 8)); break;
    case 34: cmd->sy = (UInt16)((cmd->sy & 0xf00) | value);               break;
    case 35: cmd->sy = (UInt16)((cmd->sy & 0x0ff) | ((value & 0x0f) << 8)); break;
    case 36: cmd->dx = (UInt16)((cmd->dx & 0x700) | value);               break;
    case 37: cmd->dx = (UInt16)((cmd->dx & 0x0ff) | ((value & 0x07) << 8)); break;
    case 38: cmd->dy = (UInt16)((cmd->dy & 0xf00) | value);               break;
    case 39: cmd->dy = (UInt16)((cmd->dy & 0x0ff) | ((value & 0x0f) << 8)); break;
    case 40: cmd->nx = (UInt16)((cmd->nx & 0xf00) | value);               break;
    case 41: cmd->nx = (UInt16)((cmd->nx & 0x0ff) | ((value & 0x0f) << 8)); break;
    case 42: cmd->ny = (UInt16)((cmd->ny & 0xf00) | value);               break;
    case 43: cmd->ny = (UInt16)((cmd->ny & 0x0ff) | ((value & 0x0f) << 8)); break;
    case 44: cmd->arg = value & 0x0f;                                      break;
    case 45: cmd->lop = value & 0x1f;                                      break;
    case 46: cmd->wm = (UInt16)((cmd->wm & 0xff00) | value);               break;
    case 47: cmd->wm = (UInt16)((cmd->wm & 0x00ff) | (value << 8));        break;
    case 48: cmd->fc = (UInt16)((cmd->fc & 0xff00) | value);               break;
    case 49: cmd->fc = (UInt16)((cmd->fc & 0x00ff) | (value << 8));        break;
    case 50: cmd->bc = (UInt16)((cmd->bc & 0xff00) | value);               break;
    case 51: cmd->bc = (UInt16)((cmd->bc & 0x00ff) | (value << 8));        break;
    }
}

/* Stand-by addresses VRAM as P1. */
static void setMode(V9990Cmd* cmd, V9990Mode mode, UInt8 r6)
{
    switch (mode) {
    case V9990_MODE_P1:
        cmd->mode = V9990_MODE_P1; cmd->bpp = 4; cmd->width = 256;
        break;
    case V9990_MODE_P2:
        cmd->mode = V9990_MODE_P2; cmd->bpp = 4; cmd->width = 512;
        break;
    case V9990_MODE_STANDBY:
        cmd->mode = V9990_MODE_P1; cmd->bpp = 4; cmd->width = 256 << ((r6 >> 2) & 3);
        break;
    default:
        cmd->mode = mode; cmd->bpp = 2 << (r6 & 3); cmd->width = 256 << ((r6 >> 2) & 3);
        break;
    }
}

void v9990CmdStart(V9990Cmd* cmd, UInt8 opcode, V9990Mode mode, UInt8 r6, UInt32 time)
{
    v9990CmdSync(cmd, time);

    setMode(cmd, mode, r6);
    cmd->opcode   = opcode;
    cmd->time     = time;
    cmd->status   = (UInt8)((cmd->status & V9990_CMD_BD) | V9990_CMD_CE);
    cmd->anx      = wrappedNx(cmd);
    cmd->any      = wrappedNy(cmd);
    cmd->bits     = 0;
    cmd->lastDot  = 0;
    cmd->lastByte = 0;

    switch (opcode >> 4) {
    case OP_STOP:
    case OP_CMMK:
        /* No kanji ROM is fitted, so CMMK ends at once. */
        finish(cmd);
        break;
    case OP_LMMC:
    case OP_CMMC:
        cmd->status |= V9990_CMD_TR;
        break;
    case OP_CMMM:
    case OP_BMXL:
        cmd->srcAddress = linearAddress(cmd->sx, cmd->sy);
        break;
    case OP_BMLX:
        cmd->dstAddress = linearAddress(cmd->dx, cmd->dy);
        cmd->data = 0;
        break;
    case OP_BMLL:
        cmd->srcAddress = linearAddress(cmd->sx, cmd->sy);
        cmd->dstAddress = linearAddress(cmd->dx, cmd->dy);
        cmd->count      = linearAddress(cmd->nx, cmd->ny);
        if (cmd->count == 0) {
            cmd->count = V9990_VRAM_SIZE;
        }
        if (cmd->bpp == 16) {
            cmd->srcAddress >>= 1;
            cmd->dstAddress >>= 1;
            cmd->count      >>= 1;
            if (cmd->count == 0) {
                finish(cmd);
            }
        }
        break;
    case OP_LINE:
        cmd->ax    = cmd->dx;
        cmd->err   = (UInt16)(cmd->nx != 0 ? (cmd->nx - 1) / 2 : 0);
        cmd->count = 0;
        break;
    case OP_SRCH:
        cmd->ax = cmd->sx;
        break;
    }
}

UInt8 v9990CmdGetStatus(V9990Cmd* cmd, UInt32 time)
{
    v9990CmdSync(cmd, time);
    return cmd->status;
}

/* R#54 bits 7-3 read 0. */
UInt16 v9990CmdGetBorderX(V9990Cmd* cmd, UInt32 time)
{
    v9990CmdSync(cmd, time);
    return (UInt16)(cmd->borderX & 0x7ff);
}

void v9990CmdWriteData(V9990Cmd* cmd, UInt8 value, UInt32 time)
{
    v9990CmdSync(cmd, time);
    cmd->data    = value;
    cmd->status &= ~V9990_CMD_TR;
    resume(cmd, time);
    v9990CmdSync(cmd, time);
}

/* Without a byte ready (TR = 0) a read returns 0xFF. */
UInt8 v9990CmdReadData(V9990Cmd* cmd, UInt32 time)
{
    UInt8 value = 0xff;

    v9990CmdSync(cmd, time);
    if (cmd->status & V9990_CMD_TR) {
        value = cmd->data;
        cmd->status &= ~V9990_CMD_TR;
        resume(cmd, time);
        if (cmd->lastByte) {
            cmd->lastByte = 0;
            finish(cmd);
        }
    }
    return value;
}

static void clearState(V9990Cmd* cmd, UInt32 time)
{
    UInt8* vram = cmd->vram;
    V9990CmdEndCallback onEnd = cmd->onEnd;
    void* ref = cmd->ref;

    memset(cmd, 0, sizeof(*cmd));
    cmd->vram  = vram;
    cmd->onEnd = onEnd;
    cmd->ref   = ref;
    cmd->mode  = V9990_MODE_P1;
    cmd->bpp   = 4;
    cmd->width = 256;
    cmd->time  = time;
}

void v9990CmdReset(V9990Cmd* cmd, UInt32 time)
{
    clearState(cmd, time);
}

V9990Cmd* v9990CmdCreate(UInt8* vram, V9990CmdEndCallback onEnd, void* ref)
{
    V9990Cmd* cmd = calloc(1, sizeof(V9990Cmd));

    cmd->vram  = vram;
    cmd->onEnd = onEnd;
    cmd->ref   = ref;
    clearState(cmd, 0);
    return cmd;
}

void v9990CmdDestroy(V9990Cmd* cmd)
{
    free(cmd);
}

void v9990CmdSaveState(V9990Cmd* cmd)
{
    SaveState* state = saveStateOpenForWrite("V9990Cmd");

    saveStateSet(state, "sx",         cmd->sx);
    saveStateSet(state, "sy",         cmd->sy);
    saveStateSet(state, "dx",         cmd->dx);
    saveStateSet(state, "dy",         cmd->dy);
    saveStateSet(state, "nx",         cmd->nx);
    saveStateSet(state, "ny",         cmd->ny);
    saveStateSet(state, "arg",        cmd->arg);
    saveStateSet(state, "lop",        cmd->lop);
    saveStateSet(state, "wm",         cmd->wm);
    saveStateSet(state, "fc",         cmd->fc);
    saveStateSet(state, "bc",         cmd->bc);
    saveStateSet(state, "opcode",     cmd->opcode);
    saveStateSet(state, "status",     cmd->status);
    saveStateSet(state, "borderX",    cmd->borderX);
    saveStateSet(state, "mode",       cmd->mode);
    saveStateSet(state, "bpp",        cmd->bpp);
    saveStateSet(state, "width",      cmd->width);
    saveStateSet(state, "time",       cmd->time);
    saveStateSet(state, "waiting",    cmd->waiting);
    saveStateSet(state, "anx",        cmd->anx);
    saveStateSet(state, "any",        cmd->any);
    saveStateSet(state, "ax",         cmd->ax);
    saveStateSet(state, "err",        cmd->err);
    saveStateSet(state, "count",      cmd->count);
    saveStateSet(state, "srcAddress", cmd->srcAddress);
    saveStateSet(state, "dstAddress", cmd->dstAddress);
    saveStateSet(state, "data",       cmd->data);
    saveStateSet(state, "partial",    cmd->partial);
    saveStateSet(state, "bits",       cmd->bits);
    saveStateSet(state, "lastDot",    cmd->lastDot);
    saveStateSet(state, "lastByte",   cmd->lastByte);

    saveStateClose(state);
}

void v9990CmdLoadState(V9990Cmd* cmd, UInt32 time)
{
    SaveState* state = saveStateOpenForRead("V9990Cmd");

    clearState(cmd, time);
    if (saveStateIsEmpty(state)) {
        saveStateClose(state);
        return;
    }

    cmd->sx         = (UInt16)saveStateGet(state, "sx",         0);
    cmd->sy         = (UInt16)saveStateGet(state, "sy",         0);
    cmd->dx         = (UInt16)saveStateGet(state, "dx",         0);
    cmd->dy         = (UInt16)saveStateGet(state, "dy",         0);
    cmd->nx         = (UInt16)saveStateGet(state, "nx",         0);
    cmd->ny         = (UInt16)saveStateGet(state, "ny",         0);
    cmd->arg        = (UInt8) saveStateGet(state, "arg",        0);
    cmd->lop        = (UInt8) saveStateGet(state, "lop",        0);
    cmd->wm         = (UInt16)saveStateGet(state, "wm",         0);
    cmd->fc         = (UInt16)saveStateGet(state, "fc",         0);
    cmd->bc         = (UInt16)saveStateGet(state, "bc",         0);
    cmd->opcode     = (UInt8) saveStateGet(state, "opcode",     0);
    cmd->status     = (UInt8) saveStateGet(state, "status",     0);
    cmd->borderX    = (UInt16)saveStateGet(state, "borderX",    0);
    cmd->mode       = (V9990Mode)saveStateGet(state, "mode",    V9990_MODE_P1);
    cmd->bpp        = saveStateGet(state, "bpp",        4);
    cmd->width      = saveStateGet(state, "width",      256);
    cmd->time       = saveStateGet(state, "time",       time);
    cmd->waiting    = saveStateGet(state, "waiting",    0);
    cmd->anx        = saveStateGet(state, "anx",        0);
    cmd->any        = saveStateGet(state, "any",        0);
    cmd->ax         = (UInt16)saveStateGet(state, "ax",         0);
    cmd->err        = (UInt16)saveStateGet(state, "err",        0);
    cmd->count      = saveStateGet(state, "count",      0);
    cmd->srcAddress = saveStateGet(state, "srcAddress", 0);
    cmd->dstAddress = saveStateGet(state, "dstAddress", 0);
    cmd->data       = (UInt8) saveStateGet(state, "data",       0);
    cmd->partial    = (UInt8) saveStateGet(state, "partial",    0);
    cmd->bits       = saveStateGet(state, "bits",       0);
    cmd->lastDot    = saveStateGet(state, "lastDot",    0);
    cmd->lastByte   = saveStateGet(state, "lastByte",   0);

    saveStateClose(state);
}
