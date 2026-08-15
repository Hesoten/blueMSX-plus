/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/sramMapperMatsuchita.c,v $
**
** $Revision: 1.8 $
**
** $Date: 2008-03-30 18:38:44 $
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
#include "sramMapperMatsushita.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "DebugDeviceManager.h"
#include "SaveState.h"
#include "IoPort.h"
#include "sramLoader.h"
#include "Switches.h"
#include "Language.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void msxEnableCpuFreq_1_5(int enable);

/* Switched I/O device 8. Port 41h per map.grauw.nl/resources/msx_io_ports.php:
** bit 0 = speed (0 = 5.37MHz, 1 = 3.58MHz), bit 2 = turbo fitted (0 = yes, r/o),
** bit 7 = firmware switch (0 = on, r/o). Only the T9769B machines have turbo. */

typedef struct {
    int    deviceHandle;
    int    debugHandle;
    UInt8  sram[0x800];
    UInt32 address;
	UInt8  color1;
    UInt8  color2;
	UInt8  pattern;
    int    turboEnabled;
    int    hasTurbo;
} SramMapperMatsushita;

static void saveState(SramMapperMatsushita* rm)
{
    SaveState* state = saveStateOpenForWrite("mapperMatsushita");

    saveStateSet(state, "address", rm->address);
    saveStateSet(state, "color1",  rm->color1);
    saveStateSet(state, "color2",  rm->color2);
    saveStateSet(state, "pattern", rm->pattern);
    saveStateSet(state, "cpu15",   rm->turboEnabled);
    
    saveStateClose(state);
}

static void loadState(SramMapperMatsushita* rm)
{
    SaveState* state = saveStateOpenForRead("mapperMatsushita");

    rm->address =        saveStateGet(state, "address", 0);
    rm->color1  = (UInt8)saveStateGet(state, "color1",  0);
    rm->color2  = (UInt8)saveStateGet(state, "color2",  0);
    rm->pattern = (UInt8)saveStateGet(state, "pattern", 0);
    /* Key kept as "cpu15" so older save states still load */
    rm->turboEnabled = rm->hasTurbo && saveStateGet(state, "cpu15", 0);

    saveStateClose(state);

    if (rm->hasTurbo) {
        msxEnableCpuFreq_1_5(rm->turboEnabled);
    }
}

static void setTurbo(SramMapperMatsushita* rm, int enable)
{
    if (!rm->hasTurbo || rm->turboEnabled == enable) {
        return;
    }

    rm->turboEnabled = enable;
    msxEnableCpuFreq_1_5(enable);
}

static void reset(SramMapperMatsushita* rm)
{
    /* A reset drops the CPU back to 3.58MHz and clears the drawing state. */
    setTurbo(rm, 0);

    rm->address = 0;
    rm->color1  = 0;
    rm->color2  = 0;
    rm->pattern = 0;
}

static void destroy(SramMapperMatsushita* rm)
{
    sramSave(sramCreateFilename("Matsushita.SRAM"), rm->sram, 0x800, NULL, 0);

    deviceManagerUnregister(rm->deviceHandle);
    debugDeviceUnregister(rm->debugHandle);

    ioPortUnregisterSub(0x08);

    free(rm);
}

static UInt8 peek(SramMapperMatsushita* rm, UInt16 ioPort)
{
	UInt8 result;
	switch (ioPort & 0x0f) {
	case 0:
		result = ~0x08;
		break;
	case 1:
        /* bit 7: firmware switch, bit 2: turbo available, bit 0: speed mode */
        result = switchGetFront() ? 0x7f : 0xff;
        if (rm->hasTurbo) {
            result &= ~0x04;
            if (rm->turboEnabled) {
                result &= ~0x01;
            }
        }
		break;
	case 3:
		result = (((rm->pattern & 0x80) ? rm->color2 : rm->color1) << 4)
		        | ((rm->pattern & 0x40) ? rm->color2 : rm->color1);
		break;
	case 9:
		if (rm->address < 0x800) {
			result = rm->sram[rm->address];
		} else {
			result = 0xff;
		}
		break;
	default:
		result = 0xff;
	}
	return result;
}

static UInt8 read(SramMapperMatsushita* rm, UInt16 ioPort)
{
	UInt8 result = peek(rm, ioPort);

	/* Same values as peek(), plus the auto increments of a real read */
	switch (ioPort & 0x0f) {
	case 3:
		rm->pattern = (rm->pattern << 2) | (rm->pattern >> 6);
		break;
	case 9:
		rm->address = (rm->address + 1) & 0x1fff;
		break;
	}
	return result;
}

static void write(SramMapperMatsushita* rm, UInt16 ioPort, UInt8 value)
{
	switch (ioPort & 0x0f) {
    case 1:
        /* bit 0 selects the speed, the rest of the port is read only */
        setTurbo(rm, (value & 0x01) ? 0 : 1);
        break;
	case 3:
		rm->color2 = value >> 4;
		rm->color1 = value & 0x0f;
		break;
	case 4:
		rm->pattern = value;
		break;
	case 7:
		rm->address = (rm->address & 0xff00) | value;
		break;
	case 8:
		rm->address = (rm->address & 0x00ff) | ((value & 0x1f) << 8);
		break;
	case 9:
		if (rm->address < 0x800) {
			rm->sram[rm->address] = value;
		}
		rm->address = (rm->address + 1) & 0x1fff;
		break;
	}	
}

static void getDebugInfo(SramMapperMatsushita* rm, DbgDevice* dbgDevice)
{
    if (ioPortCheckSub(0x08)) {
        DbgIoPorts* ioPorts;
        int i;

        ioPorts = dbgDeviceAddIoPorts(dbgDevice, langDbgDevMatsushita(), 16);

        /* A write to 40h selects the bank and never reaches the device */
        dbgIoPortsAddPort(ioPorts, 0, 0x40, DBG_IO_READ, peek(rm, 0));

        for (i = 1; i < 16; i++) {
            dbgIoPortsAddPort(ioPorts, i, 0x40 + i, DBG_IO_READWRITE, peek(rm, i));
        }
    }
}

int sramMapperMatsushitaCreate(int hasTurbo)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    SramMapperMatsushita* rm;

    rm = malloc(sizeof(SramMapperMatsushita));

    rm->deviceHandle = deviceManagerRegister(SRAM_MATSUSHITA, &callbacks, rm);
    rm->debugHandle = debugDeviceRegister(DBGTYPE_BIOS, langDbgDevMatsushita(), &dbgCallbacks, rm);

    memset(rm->sram, 0xff, 0x800);
    rm->address      = 0;
    rm->color1       = 0;
    rm->color2       = 0;
    rm->pattern      = 0;
    rm->turboEnabled = 0;
    rm->hasTurbo     = hasTurbo;

    sramLoad(sramCreateFilename("Matsushita.SRAM"), rm->sram, 0x800, NULL, 0);

    ioPortRegisterSub(0x08, read, write, rm);

    return 1;
}

