/*****************************************************************************
**
** Video chip op handler.
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
#include "ControlOp.h"
#include "ControlJson.h"
#include "ControlProtocol.h"
#include "ControlScreen.h"
#include "ControlStatus.h"
#include "ControlVram.h"
#include "ControlView.h"
#include "Debugger.h"
#include "VDP.h"
#include "VdpDebug.h"
#include "V9990.h"
#include <stdio.h>
#include <string.h>

enum { CHIP_MACHINE, CHIP_CARTRIDGE, CHIP_V9990 };

/* Too big for the stack; ops run one at a time on the agent thread, under its view. */
static union {
    VdpDebugState   vdp;
    V9990DebugState v9990;
} captured;

/* The debug index of the machine's VDP (cartridge 0) or a cartridge's (1), or -1. */
static int vdpIndexOf(int cartridge)
{
    int i;

    for (i = 0; i < vdpDebugCount(); i++) {
        if ((vdpDebugIsCartridge(i) != 0) == cartridge) {
            return i;
        }
    }
    return -1;
}

/* count bytes as rows of 8 labelled prefix<first>-<last>. */
static void listBytes(CtrlText* text, const char* prefix, int first, const UInt8* bytes, int count)
{
    int i;
    int j;

    for (i = 0; i < count; i += 8) {
        int  last = i + 8 < count ? i + 7 : count - 1;
        char label[16];

        snprintf(label, sizeof(label), "%s%d-%d", prefix, first + i, first + last);
        ctrlTextAdd(text, "%-9s", label);
        for (j = i; j <= last; j++) {
            ctrlTextAdd(text, " %02X", bytes[j]);
        }
        ctrlTextAdd(text, "\n");
    }
}

/* Which chip this is, and which picture the screen shows. */
static void describeChip(CtrlText* text, int chip, const char* name)
{
    int superimposed = 0;
    int v9990Shown   = ctrlScreenV9990Shown(&superimposed);
    int cartShown    = ctrlScreenV9968CartShown();

    switch (chip) {
    case CHIP_V9990:
        ctrlTextAdd(text, "chip:    v9990, %s; its picture is %s\n", name,
                    !v9990Shown ? "not shown" : superimposed ? "shown over the VDP picture"
                                                             : "shown instead of the VDP picture");
        return;
    case CHIP_CARTRIDGE:
        ctrlTextAdd(text, "chip:    cartridge, %s; its picture is %s\n", name,
                    cartShown ? "shown instead of the VDP picture" : "not shown");
        return;
    default:
        ctrlTextAdd(text, "chip:    machine, %s%s\n", name,
                    v9990Shown && superimposed ? "; the V9990 picture is shown over its own (chip=v9990)" :
                    v9990Shown ? "; the screen shows the V9990 picture instead (chip=v9990)" :
                    cartShown  ? "; the screen shows the cartridge picture instead (chip=cartridge)" : "");
        return;
    }
}

static int isTmsVdp(const VdpDebugState* s)
{
    return s->version == VDP_TMS9929A || s->version == VDP_TMS99x8A || s->version == VDP_TMS9918A;
}

static void vdpRegisters(CtrlText* text, const VdpDebugState* s)
{
    int raw = ctrlStatusUndefinedModeBits(s->regs);

    if (raw < 0) {
        ctrlTextAdd(text, "mode:    %d, %s; display %s, %s\n", s->screenMode,
                    ctrlVramModeName(s->screenMode), s->screenOn ? "on" : "off",
                    s->isPal ? "50 Hz" : "60 Hz");
    }
    else {
        ctrlTextAdd(text, "mode:    bits 0x%02X (R#0/R#1) are mixed or undefined, drawn blank "
                    "or from other tables; display %s, %s\n", raw, s->screenOn ? "on" : "off",
                    s->isPal ? "50 Hz" : "60 Hz");
    }
    ctrlTextAdd(text, "vram:    %d kB, %s\n", s->vramSize / 1024,
                s->bitmapLinear ? "SCREEN 7-12 in address order (SP3)" :
                !s->interleaved ? "in address order" :
                s->vramA17      ? "interleaved, each 128 kB half on its own" :
                                  "interleaved, two 64 kB banks");
    listBytes(text, "R#", 0, s->regs, s->regCount);
    listBytes(text, "S#", 0, s->status, s->statusCount);
    /* The engine's own copy: its counters move while a command runs. */
    listBytes(text, "R#", 32, s->cmdRegs, s->cmdRegCount);
}

static void vdpPalette(CtrlText* text, const VdpDebugState* s)
{
    int i;

    if (isTmsVdp(s)) {
        ctrlTextAdd(text, "fixed colours: this VDP has no palette registers\n");
        for (i = 0; i < 16; i++) {
            ctrlTextAdd(text, "  %2d  #%06X\n", i, (unsigned)s->paletteRgb[i]);
        }
        return;
    }
    ctrlTextAdd(text, "palette: P#n as GRB, 3 bits each, and the colour drawn\n");
    for (i = 0; i < 16; i++) {
        ctrlTextAdd(text, "  %2d  GRB=%03X  #%06X\n", i, (unsigned)s->paletteReg[i],
                    (unsigned)s->paletteRgb[i]);
    }
    if (s->version != VDP_V9968) {
        return;
    }
    ctrlTextAdd(text, "epal:    %s; 256 colours as #RRGGBB, 8 per row\n",
                s->paletteExtCount > 0 ? "on, SCREEN 8 dots and sprites, SCREEN 10-11 YAE dots, "
                                         "the SCREEN 8 and 10-12 backdrop, SP3 sprites and the "
                                         "palette set a mode 2 sprite picks in attribute byte 3 "
                                         "(bits 0-3, 16 colours from set*16) read these"
                                       : "off, only SP3 sprites read these");
    for (i = 0; i < 256; i += 8) {
        int j;

        ctrlTextAdd(text, "  %3d ", i);
        for (j = i; j < i + 8; j++) {
            ctrlTextAdd(text, " #%06X", (unsigned)s->paletteExt[j]);
        }
        ctrlTextAdd(text, "\n");
    }
}

static void v9990Registers(CtrlText* text, const V9990DebugState* s)
{
    ctrlTextAdd(text, "as of the V9990's last access: a command it runs may have gone further\n");
    ctrlTextAdd(text, "mode:    %s (%s at the frame start), R#6=%02X, MCS=%d; display %s, %s%s\n",
                ctrlStatusV9990ModeName(s->mode), ctrlStatusV9990ModeName(s->frameMode),
                s->modeR6, s->mcs, s->displayOn ? "on" : "off", s->isPal ? "50 Hz" : "60 Hz",
                s->interlace ? ", interlaced" : "");
    listBytes(text, "R#", 0, s->regs, 64);
    if (s->liveR13 != s->regs[13]) {
        ctrlTextAdd(text, "R#13 is %02X now; the row above has it as the last displayed line "
                    "drew it\n", s->liveR13);
    }
    listBytes(text, "P#", 0, s->ports, 16);
}

static void v9990Palette(CtrlText* text, const V9990DebugState* s)
{
    int i;

    ctrlTextAdd(text, "palette: 64 entries as R, G, B, 5 bits each, YS where set; as the "
                "last displayed line drew them when that is known, as of the V9990's last access\n");
    for (i = 0; i < 64; i += 4) {
        int j;

        ctrlTextAdd(text, "  %2d ", i);
        for (j = i; j < i + 4; j++) {
            const UInt8* p = s->palette + 4 * j;

            ctrlTextAdd(text, "  R%02d G%02d B%02d%s", p[0] & 0x1f, p[1] & 0x1f, p[2] & 0x1f,
                        p[0] & 0x80 ? " YS" : "   ");
        }
        ctrlTextAdd(text, "\n");
    }
}

/* The chips the machine has, as a refusal lists them. */
static int refuseNoChip(CtrlCall* call, const char* wanted)
{
    char chips[96];
    int  machine = vdpIndexOf(0);

    snprintf(chips, sizeof(chips), "machine (%s)%s%s",
             machine >= 0 ? vdpDebugName(machine) : "none",
             vdpIndexOf(1) >= 0 ? ", cartridge (V9968)" : "",
             v9990DebugCount() > 0 ? ", v9990" : "");
    return ctrlReplyErrorf(call, "no_such_chip", "this machine has no %s chip; its video chips: %s",
                           wanted, chips);
}

int ctrlOpVdp(CtrlCall* call)
{
    const char* show     = ctrlCallText(call, "show");
    const char* chipName = ctrlCallText(call, "chip");
    DbgSnapshot* snapshot;
    int palette;
    int chip;
    int refused;

    if (show == NULL) {
        show = CTRL_VDP_SHOW_DEFAULT;
    }
    if (chipName == NULL) {
        chipName = CTRL_VDP_CHIP_DEFAULT;
    }
    if (strcmp(show, "registers") != 0 && strcmp(show, "palette") != 0) {
        return ctrlReplyError(call, "bad_show", "\"show\" must be registers or palette");
    }
    palette = strcmp(show, "palette") == 0;
    if (strcmp(chipName, "machine") == 0) {
        chip = CHIP_MACHINE;
    }
    else if (strcmp(chipName, "cartridge") == 0) {
        chip = CHIP_CARTRIDGE;
    }
    else if (strcmp(chipName, "v9990") == 0) {
        chip = CHIP_V9990;
    }
    else {
        return ctrlReplyError(call, "bad_chip", "\"chip\" must be machine, cartridge or v9990");
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    /* The device snapshot syncs each VDP, as status does, so the command engine's counters
    ** show how far a running command got. The V9990 is not synced by it. */
    snapshot = ctrlViewDevices();
    if (snapshot != NULL) {
        dbgSnapshotDestroy(snapshot);
    }

    if (chip == CHIP_V9990) {
        if (v9990DebugCount() <= 0 || !v9990DebugCapture(0, &captured.v9990)) {
            return refuseNoChip(call, chipName);
        }
        describeChip(&call->text, chip, captured.v9990.name);
        if (palette) {
            v9990Palette(&call->text, &captured.v9990);
        }
        else {
            v9990Registers(&call->text, &captured.v9990);
        }
    }
    else {
        int index = vdpIndexOf(chip == CHIP_CARTRIDGE);

        if (index < 0 || !vdpDebugCapture(index, &captured.vdp)) {
            return refuseNoChip(call, chipName);
        }
        describeChip(&call->text, chip, captured.vdp.name);
        if (palette) {
            vdpPalette(&call->text, &captured.vdp);
        }
        else {
            vdpRegisters(&call->text, &captured.vdp);
        }
    }
    jsonOutString(&call->out, "chip", chipName);
    return ctrlReplyDone(call);
}
