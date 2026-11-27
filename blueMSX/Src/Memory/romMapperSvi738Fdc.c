/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/romMapperSvi738Fdc.c,v $
**
** $Revision: 1.10 $
**
** $Date: 2008-03-30 18:38:44 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik, Tomas Karlsson
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
#include "romMapperSvi738Fdc.h"
#include "WD2793.h"
#include "Led.h"
#include "MediaDb.h"
#include "Disk.h"
#include "SlotManager.h"
#include "DeviceManager.h"
#include "SaveState.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    int deviceHandle;
    UInt8* romData;
    WD2793* fdc;
    int slot;
    int sslot;
    int startPage;
    UInt8 drvSelect;
} RomMapperSvi738Fdc;

static void saveState(RomMapperSvi738Fdc* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperSvi738Fdc");

    saveStateSet(state, "drvSelect",  rm->drvSelect);
    
    saveStateClose(state);

    wd2793SaveState(rm->fdc);
}

static void loadState(RomMapperSvi738Fdc* rm)
{
    SaveState* state = saveStateOpenForRead("mapperSvi738Fdc");

    rm->drvSelect  = (UInt8)saveStateGet(state, "drvSelect",  0);

    saveStateClose(state);

    wd2793LoadState(rm->fdc);
}

static void destroy(RomMapperSvi738Fdc* rm)
{
    slotUnregister(rm->slot, rm->sslot, rm->startPage);
    deviceManagerUnregister(rm->deviceHandle);

    wd2793Destroy(rm->fdc);

    free(rm->romData);
    free(rm);
}

static UInt8 read(RomMapperSvi738Fdc* rm, UInt16 address) 
{
    /* Only A0-A2 and A6-A13 select a register, so 3FB8-3FBF repeats
    ** every 8 bytes over 3F80-3FBF. */
    switch (address & 0x3fc7) {
        case 0x3f80:
            return wd2793GetStatusReg(rm->fdc);
        case 0x3f81:
            return wd2793GetTrackReg(rm->fdc);
        case 0x3f82:
            return wd2793GetSectorReg(rm->fdc);
        case 0x3f83:
            return wd2793GetDataReg(rm->fdc);
        case 0x3f84:
        case 0x3f85:
        case 0x3f86:
        case 0x3f87:
            return (wd2793GetIrq(rm->fdc)?0x80:0) | (wd2793GetDataRequest(rm->fdc)?0:0x40);
    }
    return address < 0x4000 ? rm->romData[address] : 0xff;
}

static UInt8 peek(RomMapperSvi738Fdc* rm, UInt16 address) 
{
    switch (address & 0x3fc7) {
        case 0x3f80:
            return 0xff; // Get from fdc
        case 0x3f81:
            return 0xff; // Get from fdc
        case 0x3f82:
            return 0xff; // Get from fdc
        case 0x3f83:
            return 0xff; // Get from fdc
        case 0x3f84:
        case 0x3f85:
        case 0x3f86:
        case 0x3f87:
            return 0xff; // Get from fdc
    }
    return address < 0x4000 ? rm->romData[address] : 0xff;
}

static void write(RomMapperSvi738Fdc* rm, UInt16 address, UInt8 value) 
{
    switch (address & 0x3fc7) {
        case 0x3f80:
            wd2793SetCommandReg(rm->fdc, value);
            break;
        case 0x3f81:
            wd2793SetTrackReg(rm->fdc, value);
            break;
        case 0x3f82:
            wd2793SetSectorReg(rm->fdc, value);
            break;
        case 0x3f83:
            wd2793SetDataReg(rm->fdc, value);
            break;
        case 0x3f84:
        case 0x3f85:
        case 0x3f86:
        case 0x3f87:
            rm->drvSelect = value & 0x3f;
            wd2793SetSide(rm->fdc, value & 4);
            switch (value & 3) {
                case 1:
                    wd2793SetDrive(rm->fdc, 0);
                    break;
                case 2:
                    wd2793SetDrive(rm->fdc, 1);
                    break;
                default:
                    wd2793SetDrive(rm->fdc, -1);
            }
            /* Motor after the drive select so it acts on the new drive. */
            wd2793SetMotor(rm->fdc, value & 8);
            if (diskEnabled(0)) ledSetFdd1(value & 1);
            if (diskEnabled(1)) ledSetFdd2(value & 2);
            break;
    }
}       

static void reset(RomMapperSvi738Fdc* rm)
{
    wd2793Reset(rm->fdc);
    write(rm, 0x3fbc, 0);
}

int romMapperSvi738FdcCreate(const char* filename, UInt8* romData, 
                              int size, int slot, int sslot, int startPage) 
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    RomMapperSvi738Fdc* rm;
    int pages = 4;
    int i;

    if ((startPage + pages) > 8) {
        return 0;
    }

    rm = malloc(sizeof(RomMapperSvi738Fdc));

    rm->deviceHandle = deviceManagerRegister(ROM_SVI738FDC, &callbacks, rm);
    slotRegister(slot, sslot, startPage, pages, read, peek, write, destroy, rm);

    rm->romData = malloc(size);
    memcpy(rm->romData, romData, size);
    
    rm->slot  = slot;
    rm->sslot = sslot;
    rm->startPage  = startPage;

    for (i = 0; i < pages; i++) {
        slotMapPage(slot, sslot, i + startPage, NULL, 0, 0);
    }

    rm->fdc = wd2793Create(FDC_TYPE_WD2793);

    reset(rm);

    return 1;
}
