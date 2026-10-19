/*****************************************************************************
**
** V9968 cartridge with geo3d, on I/O ports 88h-8Dh and 8Fh.
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
#include "romMapperV9968Cart.h"
#include "VDP.h"
#include "Geo3DDevice.h"
#include "VideoManager.h"
#include "Emulator.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "SaveState.h"
#include "IoPort.h"
#include "Board.h"
#include "V9990.h"
#include <stdlib.h>

#define VDP_PORT   0x88
#define GEO3D_PORT 0x8d
/* Free board interrupt bits; the next two above it go to IE1 and CEIE. */
#define INT_BASE   0x2000

typedef struct {
    int deviceHandle;
    int slot;
    int sslot;
    struct VDP* vdp;
    Geo3DDevice* geo3d;
    /* Set by the first frame with R#1 BL on since reset, and kept while the
    ** program blanks the screen, as the cartridge's own monitor would. */
    int shown;
    int rate;
} RomMapperV9968Cart;

/* Read by the theme on another thread, so it never follows a pointer. */
static volatile int shownScreenMode = -1;
static volatile int present;

static void frameDone(void* ref, struct VDP* vdp, int isPal)
{
    RomMapperV9968Cart* rm = (RomMapperV9968Cart*)ref;
    int handle = vdpGetVideoHandle(vdp);
    int rate = isPal ? 50 : 60;

    if (rate != rm->rate) {
        rm->rate = rate;
        videoManagerSetRefreshRate(handle, rate);
    }
    if (!rm->shown && vdpIsDisplayOn(vdp)) {
        rm->shown = 1;
        videoManagerClaim(handle, 1);
        if (boardGetVideoAutodetect()) {
            videoManagerActivate(handle);
        }
    }
    if (videoManagerIsHandleActive(handle)) {
        shownScreenMode = vdpGetScreenMode(vdp);
        emulatorRefreshFromSource();
        emulatorShownFrameEnded();
    }
    else {
        shownScreenMode = -1;
    }
}

static void hide(RomMapperV9968Cart* rm)
{
    int handle = vdpGetVideoHandle(rm->vdp);

    rm->shown = 0;
    shownScreenMode = -1;
    videoManagerClaim(handle, 0);
    if (videoManagerIsHandleActive(handle)) {
        videoManagerActivateDefault();
    }
}

static void saveState(RomMapperV9968Cart* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperV9968Cart");

    saveStateSet(state, "shown", rm->shown);
    saveStateClose(state);

    vdpSaveStateSecondary(rm->vdp);
    geo3dDeviceSaveState(rm->geo3d);
}

/* The shown source is restored by the video manager, not switched here, and
** geo3d takes up a RUN against the command engine as loaded. */
static void loadState(RomMapperV9968Cart* rm)
{
    SaveState* state = saveStateOpenForRead("mapperV9968Cart");

    rm->shown = saveStateGet(state, "shown", 0);
    saveStateClose(state);

    vdpLoadStateSecondary(rm->vdp);
    videoManagerClaim(vdpGetVideoHandle(rm->vdp), rm->shown);

    geo3dDeviceLoadState(rm->geo3d);
}

static void reset(RomMapperV9968Cart* rm)
{
    hide(rm);
    vdpResetSecondary(rm->vdp);
    geo3dDeviceReset(rm->geo3d);
}

static void destroy(RomMapperV9968Cart* rm)
{
    hide(rm);
    slotUnregister(rm->slot, rm->sslot, 0);
    geo3dDeviceDestroy(rm->geo3d);
    vdpDestroySecondary(rm->vdp);
    deviceManagerUnregister(rm->deviceHandle);
    present = 0;

    free(rm);
}

int romMapperV9968CartCreate(int slot, int sslot)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    VdpSecondaryConfig config = {
        VDP_PORT, INT_BASE, "V9968 Cartridge", "vdpCart", "vdpCartCommandEngine", frameDone, NULL
    };
    RomMapperV9968Cart* rm;
    int i;

    /* The interrupt is an MSX /INT. 8Eh is left to a MegaRAM, as on the
    ** cartridge. */
    if (boardGetType() != BOARD_MSX) {
        return 0;
    }
    for (i = VDP_PORT; i <= GEO3D_PORT + 2; i++) {
        if (i != GEO3D_PORT + 1 && ioPortGetRef(i) != NULL) {
            return 0;
        }
    }
    /* A GFX9000 or Video9000 takes over the screen too. */
    if (v9990IsPresent()) {
        return 0;
    }

    rm = calloc(1, sizeof(RomMapperV9968Cart));

    rm->slot  = slot;
    rm->sslot = sslot;

    config.ref = rm;
    rm->vdp   = vdpCreateSecondary(&config);
    rm->geo3d = geo3dDeviceCreateSecondary(GEO3D_PORT, rm->vdp, "Geo3DDeviceCart", "geo3d (cartridge)");
    /* The machine VDP keeps drawing while this one is shown, so its status
    ** flags stay right and switching back shows a current picture. */
    videoManagerSetDrawSource(vdpGetVideoHandle(rm->vdp), videoManagerGetHandle(0));

    rm->deviceHandle = deviceManagerRegister(ROM_V9968CART, &callbacks, rm);

    /* The cartridge has no memory: its slot reads 0xFF. */
    slotRegister(slot, sslot, 0, 8, NULL, NULL, NULL, destroy, rm);
    for (i = 0; i < 8; i++) {
        slotMapPage(slot, sslot, i, NULL, 0, 0);
    }
    present = 1;

    return 1;
}

int romMapperV9968CartIsPresent(void)
{
    return present;
}

int romMapperV9968CartGetShownScreenMode(void)
{
    return shownScreenMode;
}
