/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/romMapperCasette.c,v $
**
** $Revision: 1.9 $
**
** $Date: 2008-03-31 19:42:22 $
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
#include "romMapperCasette.h"
#include "MediaDb.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "SaveState.h"
#include "Board.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static UInt16 patchAddress[] = { 0x00e1, 0x00e4, 0x00e7, 0x00ea, 0x00ed, 0x00f0, 0x00f3, 0 };
static UInt16 patchAddressSVI[] = {0x006C,0x006F,0x0072,0x0075,0x0078,0x210A,0x21A9,0}; // 0x0069

typedef struct {
    int deviceHandle;
    UInt8* romData;
    int slot;
    int sslot;
    int startPage;
    UInt8 origBytes[8][3];
    int patched;
} RomMapperCasette;

/* The MSX cassette BIOS entries are patched in place. Keeping the original
** bytes lets the trap stand down for signal level images so the real BIOS
** routines run and decode the waveform instead. SVI is never toggled. */
static RomMapperCasette* casPatchMapper = NULL;
static int patchEnabled = 1;

static void destroy(RomMapperCasette* rm)
{
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);

    if (casPatchMapper == rm) {
        casPatchMapper = NULL;
    }

    free(rm->romData);
    free(rm);
}

static void applyPatchEnable(RomMapperCasette* rm, int enable)
{
    int i;

    if (rm == NULL || rm->patched == enable) {
        return;
    }
    rm->patched = enable;

    for (i = 0; patchAddress[i]; i++) {
        UInt8* ptr = rm->romData + patchAddress[i];
        if (enable) {
            ptr[0] = 0xed;
            ptr[1] = 0xfe;
            ptr[2] = 0xc9;
        }
        else {
            ptr[0] = rm->origBytes[i][0];
            ptr[1] = rm->origBytes[i][1];
            ptr[2] = rm->origBytes[i][2];
        }
    }
}

void romMapperCasetteSetPatchEnable(int enable)
{
    patchEnabled = enable ? 1 : 0;
    applyPatchEnable(casPatchMapper, patchEnabled);
}

int romMapperCasetteCreate(const char* filename, UInt8* romData, 
                        int size, int slot, int sslot, int startPage) 
{
    DeviceCallbacks callbacks = { destroy, NULL, NULL, NULL };
    RomMapperCasette* rm;
    int pages = size / 0x2000;
    int i;

    if (pages == 0 || (startPage + pages) > 8) {
        return 0;
    }

    rm = malloc(sizeof(RomMapperCasette));

    rm->deviceHandle = deviceManagerRegister(ROM_CASPATCH, &callbacks, rm);
    slotRegister(slot, sslot, startPage, pages, NULL, NULL, NULL, destroy, rm);

    rm->romData = malloc(size);
    memcpy(rm->romData, romData, size);
    
    rm->slot  = slot;
    rm->sslot = sslot;
    rm->startPage  = startPage;

    if (boardGetType() == BOARD_SVI) {
        // Patch the SVI-328 BIOS and BASIC for cassette handling
        for (i = 0; patchAddressSVI[i]; i++) {
            UInt8* ptr = rm->romData + patchAddressSVI[i];
            ptr[0] = 0xed;
            ptr[1] = 0xfe;
            ptr[2] = 0xc9;
        }
        rm->romData[0x2073]=0x01;   // Skip delay loop after save
        rm->romData[0x20D0]=0x10;   // Write $55 only $10 times, instead
        rm->romData[0x20D1]=0x00;   //   of $190
        rm->romData[0x20E3]=0x00;   // Cancel instruction
        rm->romData[0x20E4]=0x00;
        rm->romData[0x20E5]=0x00;
        rm->romData[0x20E6]=0xED;
        rm->romData[0x20E7]=0xFE;
    }
    else {
        // Patch the casette rom
        for (i = 0; patchAddress[i]; i++) {
            UInt8* ptr = rm->romData + patchAddress[i];
            rm->origBytes[i][0] = ptr[0];
            rm->origBytes[i][1] = ptr[1];
            rm->origBytes[i][2] = ptr[2];
        }
        rm->patched    = 0;
        casPatchMapper = rm;
        applyPatchEnable(rm, patchEnabled);
    }

    for (i = 0; i < pages; i++) {
        slotMapPage(slot, sslot, i + startPage, rm->romData + 0x2000 * i, 1, 0);
    }

    return 1;
}
