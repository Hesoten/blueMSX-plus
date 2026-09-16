/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/IoDevice/MsxPPI.c,v $
**
** $Revision: 1.20 $
**
** $Date: 2008-09-09 04:40:32 $
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
#include "MsxPPI.h"
#include "MediaDb.h"
#include "DeviceManager.h"
#include "DebugDeviceManager.h"
#include "SlotManager.h"
#include "IoPort.h"
#include "I8255.h"
#include "Board.h"
#include "SaveState.h"
#include "KeyClick.h"
#include "ArchInput.h"
#include "Switches.h"
#include "Led.h"
#include "InputEvent.h"
#include "Language.h"
#include "Properties.h"
#include "AudioCassette.h"
#include "TapeSignal.h"
#include <stdlib.h>


static UInt8 getKeyState(int row);


typedef struct {
    int    deviceHandle;
    int    debugHandle;
    I8255* i8255;

    AudioKeyClick* keyClick;
    AudioCassette* cassette;

    UInt8 row;
    Int32 regA;
    Int32 regCHi;
} MsxPPI;

static void destroy(MsxPPI* ppi)
{
    ioPortUnregister(0xa8, ppi->i8255);
    ioPortUnregister(0xa9, ppi->i8255);
    ioPortUnregister(0xaa, ppi->i8255);
    ioPortUnregister(0xab, ppi->i8255);

    audioKeyClickDestroy(ppi->keyClick);
    audioCassetteDestroy(ppi->cassette);
    deviceManagerUnregister(ppi->deviceHandle);
    debugDeviceUnregister(ppi->debugHandle);

    i8255Destroy(ppi->i8255);

    free(ppi);
}

static void reset(MsxPPI* ppi) 
{
    ppi->row       = 0;
    ppi->regA   = -1;
    ppi->regCHi = -1;

    /* Force a known motor state: i8255Reset writes port C right after, and
    ** writeCHi only acts on a change. */
    tapeSignalSetMotor(0);
    tapeSignalReset();

    i8255Reset(ppi->i8255);
}

static void loadState(MsxPPI* ppi)
{
    SaveState* state = saveStateOpenForRead("MsxPPI");

    ppi->row    = (UInt8)saveStateGet(state, "row", 0);
    ppi->regA   =        saveStateGet(state, "regA", -1);
    ppi->regCHi =        saveStateGet(state, "regCHi", -1);

    saveStateClose(state);

    /* writeCHi only reacts to changes, so restore the motor explicitly */
    tapeSignalSetMotor(ppi->regCHi >= 0 && !(ppi->regCHi & 0x01));

    i8255LoadState(ppi->i8255);
}

static void saveState(MsxPPI* ppi)
{
    SaveState* state = saveStateOpenForWrite("MsxPPI");
    
    saveStateSet(state, "row", ppi->row);
    saveStateSet(state, "regA", ppi->regA);
    saveStateSet(state, "regCHi", ppi->regCHi);

    saveStateClose(state);

    i8255SaveState(ppi->i8255);
}

static void writeA(MsxPPI* ppi, UInt8 value)
{
    if (value != ppi->regA) {
        int i;

        ppi->regA = value;

        for (i = 0; i < 4; i++) {
            slotSetRamSlot(i, value & 3);
            value >>= 2;
        }
    }
}

static void writeCLo(MsxPPI* ppi, UInt8 value)
{
    ppi->row = value;
}

static void writeCHi(MsxPPI* ppi, UInt8 value)
{
    if (value != ppi->regCHi) {
        ppi->regCHi = value;

        audioKeyClick(ppi->keyClick, value & 0x08);
        ledSetCapslock(!(value & 0x04));
        /* Port C bit 4 is CASON, active low: 0 = motor on */
        tapeSignalSetMotor(!(value & 0x01));
        /* Port C bit 5 is the tape output the deck records */
        tapeSignalWriteBit(value & 0x02);
    }
}

static UInt8 peekB(MsxPPI* ppi)
{
    UInt8 value = getKeyState(ppi->row);

    if (ppi->row == 8) {
        int renshaSpeed = switchGetRensha();
        if (renshaSpeed) {
            UInt8 renshaOn = (UInt8)((UInt64)renshaSpeed * boardSystemTime() / boardFrequency());
            value |= (renshaOn & 1);
        }
    }

    return value;
}

static UInt8 readB(MsxPPI* ppi)
{
    UInt8 value = boardCaptureUInt8(ppi->row, getKeyState(ppi->row));

    if (ppi->row == 8) {
        int renshaSpeed = switchGetRensha();
        if (renshaSpeed) {
            UInt8 renshaOn = (UInt8)((UInt64)renshaSpeed * boardSystemTime() / boardFrequency());
            ledSetRensha(renshaSpeed > 14 ? 1 : renshaOn & 2);
            value |= (renshaOn & 1);
        }
        else {
            ledSetRensha(0);
        }
    }

    return value;
}

static void getDebugInfo(MsxPPI* ppi, DbgDevice* dbgDevice)
{
    DbgIoPorts* ioPorts;

    ioPorts = dbgDeviceAddIoPorts(dbgDevice, langDbgDevPpi(), 4);
    dbgIoPortsAddPort(ioPorts, 0, 0xa8, DBG_IO_READWRITE, i8255Peek(ppi->i8255, 0xa8));
    dbgIoPortsAddPort(ioPorts, 1, 0xa9, DBG_IO_READWRITE, i8255Peek(ppi->i8255, 0xa9));
    dbgIoPortsAddPort(ioPorts, 2, 0xaa, DBG_IO_READWRITE, i8255Peek(ppi->i8255, 0xaa));
    dbgIoPortsAddPort(ioPorts, 3, 0xab, DBG_IO_READWRITE, i8255Peek(ppi->i8255, 0xab));
}

void msxPPICreate(int ignoreKeyboard)
{
    DeviceCallbacks callbacks = { destroy, reset, saveState, loadState };
    DebugCallbacks dbgCallbacks = { getDebugInfo, NULL, NULL, NULL };
    MsxPPI* ppi = malloc(sizeof(MsxPPI));

    ppi->deviceHandle = deviceManagerRegister(RAM_MAPPER, &callbacks, ppi);
    ppi->debugHandle = debugDeviceRegister(DBGTYPE_BIOS, langDbgDevPpi(), &dbgCallbacks, ppi);

    if (ignoreKeyboard) {
        ppi->i8255 = i8255Create(NULL,  NULL,  writeA,
                                 NULL,  NULL,  NULL,
                                 NULL,  NULL,  writeCLo,
                                 NULL,  NULL,  writeCHi,
                                 ppi);
    }
    else {
        ppi->i8255 = i8255Create(NULL,  NULL,  writeA,
                                 peekB, readB, NULL,
                                 NULL,  NULL,  writeCLo,
                                 NULL,  NULL,  writeCHi,
                                 ppi);
    }
    ppi->keyClick = audioKeyClickCreate(boardGetMixer());
    ppi->cassette = audioCassetteCreate(boardGetMixer());

    ioPortRegister(0xa8, i8255Read, i8255Write, ppi->i8255);
    ioPortRegister(0xa9, i8255Read, i8255Write, ppi->i8255);
    ioPortRegister(0xaa, i8255Read, i8255Write, ppi->i8255);
    ioPortRegister(0xab, i8255Read, i8255Write, ppi->i8255);

    reset(ppi);
}



static UInt8 getKeyState(int row)
{
	/* An empty position is EC_NONE, which is 0, so the test folds away and
	** event 0 is never read. It can be set, and would look like a key. */
	#define MSX_KEY_BIT(k,n) ((k) ? (inputEventGetState(k)<<(n)) : 0)
	#define MSX_KEY_ALT(r)   ((r)==6 ? inputEventGetState(EC_RSHIFT) : 0)
	#define MSX_KEY_ROW(r,k7,k6,k5,k4,k3,k2,k1,k0) \
		~(MSX_KEY_BIT(k7,7)|MSX_KEY_BIT(k6,6)|MSX_KEY_BIT(k5,5)|MSX_KEY_BIT(k4,4)| \
		  MSX_KEY_BIT(k3,3)|MSX_KEY_BIT(k2,2)|MSX_KEY_BIT(k1,1)|MSX_KEY_BIT(k0,0)|MSX_KEY_ALT(r))

    Properties* pProperties = propGetGlobalProperties();
    if (!pProperties->keyboard.enableKeyboardQuirk) {
	    switch (row) {
	#define X(r,k7,k6,k5,k4,k3,k2,k1,k0) case r: return MSX_KEY_ROW(r,k7,k6,k5,k4,k3,k2,k1,k0);
		    MSX_KEY_MATRIX(X)
	#undef X
		    default: break;
	    }
	    return 0xff;
    }
    else {
        /*
	    Same, but including MSX keyboard matrix quirk, eg. pressing X+Z+J results in X+Z+H+J.
	    Slower than the above, since it needs data of all rows
	    */
	#define X(r,k7,k6,k5,k4,k3,k2,k1,k0) MSX_KEY_ROW(r,k7,k6,k5,k4,k3,k2,k1,k0),
	    UInt8 keyrow[12]={MSX_KEY_MATRIX(X)};
	#undef X
	    int i=11;
	
	    if (row>11) return 0xff;
	
	    while (i--) {
		
		    if (keyrow[i]==0xff) continue;
		
		    if (i==6) {
			    /* modifier keys */
			    int k=keyrow[6]&0x15; keyrow[6]|=0x15;
			    if (keyrow[6]!=0xff) {
				    int j=12;
				    while (j--) {
					    if ((keyrow[6]|keyrow[j])!=0xff) keyrow[6]=keyrow[j]=keyrow[6]&keyrow[j];
				    }
			    }
			    keyrow[6]=k|(keyrow[6]&0xea);
		    }
		
		    else {
			    int j=12;
			    while (j--) {
				    if ((keyrow[i]|keyrow[j])!=0xff) keyrow[i]=keyrow[j]=keyrow[i]&keyrow[j];
			    }
		    }
	    }
	
	    return keyrow[row];
    }
}

#undef MSX_KEY_ROW
#undef MSX_KEY_ALT
#undef MSX_KEY_BIT
