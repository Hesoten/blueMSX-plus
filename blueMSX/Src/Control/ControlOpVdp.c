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
#include "ControlAddress.h"
#include "ControlGeo3d.h"
#include "ControlJson.h"
#include "ControlProtocol.h"
#include "ControlScreen.h"
#include "ControlStatus.h"
#include "ControlVram.h"
#include "ControlView.h"
#include "Debugger.h"
#include "DebugDeviceManager.h"
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
    VdpRasterFrame  raster[2];
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
    if (s->vramBanks > 1) {
        ctrlTextAdd(text, "banks:   %d of %d kB (R#28-R#30), bank n from vram:n*0x40000%s\n",
                    s->vramBanks, s->vramSize / 1024,
                    s->regs[21] & 0x01 ? "; R#28-R#30 stay 0 while V58 is set" : "");
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

/* The bytes a VRAM dump shows, in the program's order. */
static UInt8 dumpBytes[CTRL_MEM_READ_COUNT_MAX];

static void vdpVram(CtrlText* text, const VdpDebugState* s, UInt32 at, UInt32 count)
{
    char   layout[32];
    UInt32 i;

    ctrlVramModeShortName(layout, sizeof(layout), s->vramLayout);
    ctrlTextAdd(text, "vram:    0x%X-0x%X as the program addresses it, in the %s layout%s\n",
                (unsigned)at, (unsigned)(at + count - 1), layout,
                vdpVramInterleaved(s->vramLayout) ? ", which interleaves the chips' bytes" : "");
    for (i = 0; i < count; i++) {
        UInt32 index = (UInt32)debugDeviceVramIndex(s->vramLayout, (int)(at + i));
        int    bank  = (int)(index >> 18);
        const UInt8* from = bank == s->vramBank ? s->vram : bank == s->auxBank ? s->vramAux : NULL;

        dumpBytes[i] = from != NULL ? from[index & 0x3ffff] : 0;
    }
    ctrlTextHexDump(text, at, dumpBytes, count);
}

static void v9990Vram(CtrlText* text, const V9990DebugState* s, UInt32 at, UInt32 count)
{
    UInt32 i;

    ctrlTextAdd(text, "vram:    0x%X-0x%X as the CPU addresses it in mode %s (the first is "
                "physical byte 0x%05X)\n", (unsigned)at, (unsigned)(at + count - 1),
                ctrlStatusV9990ModeName(s->cpuMode),
                (unsigned)v9990MapAddress((V9990Mode)s->cpuMode, at));
    if (s->commandActive) {
        ctrlTextAdd(text, "as of the V9990's last access: the command it runs may have written "
                    "further\n");
    }
    for (i = 0; i < count; i++) {
        dumpBytes[i] = s->vram[v9990MapAddress((V9990Mode)s->cpuMode, at + i) & V9990_VRAM_MASK];
    }
    ctrlTextHexDump(text, at, dumpBytes, count);
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

/* Room for a raster row, and for the note that the reply was cut after it. */
#define RASTER_ROW_ROOM 512

/* The events of a frame by line and cycle, rows of one line each; ties keep the order noted.
** Returns 0 when the reply ran out of room first. */
static int rasterEvents(CtrlText* text, const VdpRasterFrame* f)
{
    static VdpRasterEvent sorted[VDP_RASTER_MAX];
    int count = f->count < VDP_RASTER_MAX ? f->count : VDP_RASTER_MAX;
    int i;
    int j;

    for (i = 0; i < count; i++) {
        VdpRasterEvent e = f->ev[i];

        for (j = i; j > 0 && (sorted[j - 1].line > e.line ||
                              (sorted[j - 1].line == e.line && sorted[j - 1].cycle > e.cycle)); j--) {
            sorted[j] = sorted[j - 1];
        }
        sorted[j] = e;
    }
    for (i = 0; i < count; i++) {
        if (text->capacity - text->length < RASTER_ROW_ROOM) {
            return 0;
        }
        if (i == 0 || sorted[i].line != sorted[i - 1].line) {
            ctrlTextAdd(text, "%sL%u", i == 0 ? "" : "\n", (unsigned)sorted[i].line);
        }
        if (sorted[i].kind == VDP_RASTER_LINEINT) {
            ctrlTextAdd(text, " @%u R#19=%02X IRQ %s", (unsigned)sorted[i].cycle,
                        sorted[i].value, sorted[i].aux ? "yes" : "no");
        }
        else {
            ctrlTextAdd(text, " @%u R#%u %02X>%02X", (unsigned)sorted[i].cycle,
                        (unsigned)sorted[i].reg, sorted[i].aux, sorted[i].value);
        }
    }
    if (count > 0) {
        ctrlTextAdd(text, "\n");
    }
    return 1;
}

/* One frame, numbered unless it came before frame 0; 0 when the reply has no room for it. */
static int rasterFrame(CtrlText* text, const VdpRasterFrame* f, UInt32 frame, int numbered,
                       const char* which, int displayOver)
{
    if (text->capacity - text->length < RASTER_ROW_ROOM * 6) {
        return 0;
    }
    ctrlTextAdd(text, "\n%s frame", which);
    if (numbered) {
        ctrlTextAdd(text, " %u", (unsigned)frame);
    }
    ctrlTextAdd(text, ": complete=%d foreign=%d ", f->complete, f->foreign);
    if (f->ended) {
        ctrlTextAdd(text, "ended");
    }
    else {
        ctrlTextAdd(text, "in progress, beam at line %d cycle %d", f->beamLine, f->beamCycle);
        if (displayOver) {
            ctrlTextAdd(text, " (display over: lines %d-%d still to run and may add events)",
                        f->beamLine, f->lastLine - 1);
        }
    }
    ctrlTextAdd(text, "; %d lines, display line 0 is frame line %d; %d events, %d dropped\n",
                f->lastLine, f->firstLine, f->count, f->dropped);
    ctrlTextAdd(text, "registers as it began:\n");
    listBytes(text, "R#", 0, f->startRegs, 32);
    return rasterEvents(text, f);
}

/* show=raster: the raster log of the machine's VDP, armed by a session's first call. */
static int replyRaster(CtrlCall* call, int chip, const char* chipName)
{
    UInt32 frame = vdpGetMachineFrame();
    int    index = vdpIndexOf(0);
    int    displayOver;
    int    whole = 1;
    int    refused;

    if (chip != CHIP_MACHINE) {
        return ctrlReplyError(call, "no_such_chip", "only the machine's VDP keeps a raster log");
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    if (index < 0) {
        return refuseNoChip(call, chipName);
    }
    describeChip(&call->text, chip, vdpDebugName(index));
    if (!call->session->rasterHeld) {
        int wasArmed = vdpRasterArm(1) > 0;

        call->session->rasterHeld = 1;
        if (!wasArmed) {
            /* A capture now marks where the log starts, so the next frame reads as whole. */
            vdpDebugCaptureRaster(captured.raster, &displayOver);
            ctrlTextAdd(&call->text, "armed the raster log now: run or step a frame (bmsx_run "
                        "how=frame), then read again\n");
            jsonOutString(&call->out, "chip", chipName);
            return ctrlReplyDone(call);
        }
        ctrlTextAdd(&call->text, "the raster log was already being kept (by the VDP Viewer or "
                    "another connection); this connection holds it too\n");
    }
    ctrlTextAdd(&call->text,
                "Lines are frame lines; a write takes effect on its line or the next. R#14-17, "
                "R#32 on and writes of the value already there are not logged. complete=0: the "
                "log was armed, the machine reset or a state loaded during the frame, so earlier "
                "writes may be missing. foreign=1: something other than the program wrote the "
                "registers during it.\n");
    vdpDebugCaptureRaster(captured.raster, &displayOver);
    if (!captured.raster[0].valid) {
        ctrlTextAdd(&call->text, "nothing logged yet: run or step a frame (bmsx_run how=frame)\n");
    }
    else {
        whole = rasterFrame(&call->text, &captured.raster[0], frame, 1, "current", displayOver);
        if (whole && captured.raster[1].valid) {
            whole = rasterFrame(&call->text, &captured.raster[1], frame - 1, frame != 0,
                                "previous", 0);
        }
    }
    if (!whole) {
        ctrlTextAdd(&call->text, CTRL_TEXT_CUT);
    }
    jsonOutString(&call->out, "chip", chipName);
    return ctrlReplyDone(call);
}

/* show=geo3d: the RUNs of the chip's geo3d, one RUN (run) or its faces or edges (item, a
** number or a range a-b). Arguments are checked before the machine has to be paused. */
static int replyGeo3d(CtrlCall* call, int chip, const char* chipName)
{
    const char* item = ctrlCallText(call, "item");
    char  err[200];
    Int64 run = 0;
    int   first = 0;
    int   last = -1;
    int   single = 0;
    int   refused;
    int   result;

    if (chip == CHIP_V9990) {
        return ctrlReplyError(call, "no_such_chip", "a V9990 has no geo3d; it is beside a V9968 "
                              "(chip=machine or chip=cartridge)");
    }
    if (ctrlCallInt64(call, "run", &run) && (run < 1 || run > 0xFFFFFFFF)) {
        return ctrlReplyError(call, "out_of_range", "\"run\" must be a RUN number, 1 or more");
    }
    if (item != NULL) {
        int used = 0;
        int ok   = 0;

        /* A number or two joined by '-', and nothing else; 9 characters cannot overflow. */
        if (strlen(item) <= 9 && item[0] >= '0' && item[0] <= '9') {
            if (sscanf(item, "%d-%d%n", &first, &last, &used) == 2 && item[used] == 0) {
                ok = 1;
            }
            else if (sscanf(item, "%d%n", &first, &used) == 1 && item[used] == 0) {
                ok     = 1;
                single = 1;
                last   = first;
            }
        }
        if (!ok || last < first || last > 1023) {
            return ctrlReplyError(call, "bad_arg", "\"item\" must be a face or edge number "
                                  "0-1023, or a range such as 0-63");
        }
    }
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    if (run == 0 && item == NULL) {
        result = ctrlGeo3dSummary(&call->text, chip == CHIP_CARTRIDGE, err, sizeof(err));
    }
    else if (single) {
        result = ctrlGeo3dItem(&call->text, chip == CHIP_CARTRIDGE, (UInt32)run, first,
                               err, sizeof(err));
    }
    else {
        result = ctrlGeo3dRun(&call->text, chip == CHIP_CARTRIDGE, (UInt32)run, first,
                              item != NULL ? last : -1, err, sizeof(err));
    }
    switch (result) {
    case CTRL_GEO3D_NONE:     return ctrlReplyError(call, "no_such_chip", err);
    case CTRL_GEO3D_NO_RUN:   return ctrlReplyError(call, "out_of_range", err);
    case CTRL_GEO3D_NOT_KEPT: return ctrlReplyError(call, "not_kept", err);
    }
    jsonOutString(&call->out, "chip", chipName);
    return ctrlReplyDone(call);
}

/* show=vram: at, a number written as in an address, and count, as bmsx_mem_read takes them. */
static int readVramRange(CtrlCall* call, UInt32* at, Int32* count)
{
    const char* given = ctrlCallText(call, "at");

    if (given == NULL) {
        return ctrlReplyError(call, "bad_arg", "show=vram needs \"at\", the address as the "
                              "program uses it, such as \"0x1B00\"");
    }
    if (!ctrlAddrReadNumber(given, at)) {
        return ctrlReplyError(call, "bad_arg", "\"at\" must be an address such as \"0x1B00\", "
                              "\"1B00h\" or \"6912\" (unmarked digits are decimal)");
    }
    *count = ctrlCallInt(call, "count", CTRL_MEM_READ_COUNT_DEFAULT);
    if (*count < CTRL_MEM_READ_COUNT_MIN || *count > CTRL_MEM_READ_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be between %d and %d",
                               CTRL_MEM_READ_COUNT_MIN, CTRL_MEM_READ_COUNT_MAX);
    }
    return 0;
}

/* An address of the range with no byte behind it: a VDP with less than 128 kB in a layout
** that puts odd addresses in a second bank. Returns the refusal, or 0. */
static int refuseUnbacked(CtrlCall* call, const VdpDebugState* s, UInt32 at, Int32 count)
{
    char   layout[32];
    UInt32 i;

    for (i = 0; i < (UInt32)count; i++) {
        if ((UInt32)debugDeviceVramIndex(s->vramLayout, (int)(at + i)) >=
            ctrlVramSpan(s->vramBanks, (UInt32)s->vramSize)) {
            ctrlVramModeShortName(layout, sizeof(layout), s->vramLayout);
            return ctrlReplyErrorf(call, "out_of_range", "0x%X has no video memory behind it: "
                                   "%s puts odd addresses in a second 64 kB bank, and this VDP "
                                   "has %d kB", (unsigned)(at + i), layout, s->vramSize / 1024);
        }
    }
    return 0;
}

static int refusePastEnd(CtrlCall* call, UInt32 at, Int32 count, UInt32 size)
{
    if (at >= size) {
        return ctrlReplyErrorf(call, "out_of_range", "this chip's video memory is %u kB, "
                               "0x0000-0x%X", (unsigned)(size / 1024), (unsigned)(size - 1));
    }
    if (at + (UInt32)count > size) {
        return ctrlReplyErrorf(call, "out_of_range", "only %u byte%s left from 0x%X in this "
                               "chip's %u kB of video memory", (unsigned)(size - at),
                               size - at == 1 ? " is" : "s are", (unsigned)at,
                               (unsigned)(size / 1024));
    }
    return 0;
}

int ctrlOpVdp(CtrlCall* call)
{
    const char* show     = ctrlCallText(call, "show");
    const char* chipName = ctrlCallText(call, "chip");
    DbgSnapshot* snapshot;
    UInt32 at = 0;
    Int32  count = 0;
    int palette;
    int vram;
    int chip;
    int refused;

    if (show == NULL) {
        show = CTRL_VDP_SHOW_DEFAULT;
    }
    if (chipName == NULL) {
        chipName = CTRL_VDP_CHIP_DEFAULT;
    }
    if (strcmp(show, "registers") != 0 && strcmp(show, "palette") != 0 &&
        strcmp(show, "geo3d") != 0 && strcmp(show, "vram") != 0 && strcmp(show, "raster") != 0) {
        return ctrlReplyError(call, "bad_show",
                              "\"show\" must be registers, palette, geo3d, vram or raster");
    }
    palette = strcmp(show, "palette") == 0;
    vram    = strcmp(show, "vram") == 0;
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
    if (strcmp(show, "geo3d") == 0) {
        return replyGeo3d(call, chip, chipName);
    }
    if (ctrlCallGiven(call, "run") || ctrlCallGiven(call, "item")) {
        return ctrlReplyError(call, "bad_arg", "run and item go with show=geo3d");
    }
    if (!vram && (ctrlCallGiven(call, "at") || ctrlCallGiven(call, "count"))) {
        return ctrlReplyError(call, "bad_arg", "at and count go with show=vram");
    }
    if (vram && (refused = readVramRange(call, &at, &count)) != 0) {
        return refused;
    }
    if (strcmp(show, "raster") == 0) {
        return replyRaster(call, chip, chipName);
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
        if (vram && (refused = refusePastEnd(call, at, count, V9990_VRAM_SIZE)) != 0) {
            return refused;
        }
        describeChip(&call->text, chip, captured.v9990.name);
        if (vram) {
            v9990Vram(&call->text, &captured.v9990, at, (UInt32)count);
        }
        else if (palette) {
            v9990Palette(&call->text, &captured.v9990);
        }
        else {
            v9990Registers(&call->text, &captured.v9990);
        }
    }
    else {
        int index = vdpIndexOf(chip == CHIP_CARTRIDGE);
        int bank  = vram && (at >> 18) < VDP_DEBUG_BANKS ? (int)(at >> 18) : 0;
        int aux   = vram && ((at + (UInt32)count - 1) >> 18) != (UInt32)bank ? bank + 1 : -1;

        if (index < 0 || !vdpDebugCaptureAnyBanks(index, &captured.vdp, bank, aux)) {
            return refuseNoChip(call, chipName);
        }
        if (vram && ((refused = refusePastEnd(call, at, count,
                                              ctrlVramSpan(captured.vdp.vramBanks, (UInt32)captured.vdp.vramSize))) != 0 ||
                     (refused = refuseUnbacked(call, &captured.vdp, at, count)) != 0)) {
            return refused;
        }
        describeChip(&call->text, chip, captured.vdp.name);
        if (vram) {
            vdpVram(&call->text, &captured.vdp, at, (UInt32)count);
        }
        else if (palette) {
            vdpPalette(&call->text, &captured.vdp);
        }
        else {
            vdpRegisters(&call->text, &captured.vdp);
        }
    }
    jsonOutString(&call->out, "chip", chipName);
    return ctrlReplyDone(call);
}
