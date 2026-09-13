/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Memory/IoPort.c,v $
**
** $Revision: 1.9 $
**
** $Date: 2008-05-25 14:22:39 $
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
#include "IoPort.h"
#include "Board.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Room for the overlaps real hardware produces - a cartridge sound chip
** doubling a built-in one - with slack. Claims past this are dropped. */
#define IO_PORT_MAX_CLAIMS 4

typedef struct IoPortClaim {
    IoPortRead  read;
    IoPortWrite write;
    void*       ref;
} IoPortClaim;

typedef struct IoPortInfo {
    IoPortClaim claim[IO_PORT_MAX_CLAIMS];
    int         count;
} IoPortInfo;

static IoPortInfo  ioTable[256];
static IoPortClaim ioSubTable[256];
static IoPortClaim ioUnused[2];
static int currentSubport;

void ioPortReset()
{
    memset(ioTable, 0, sizeof(ioTable));
    memset(ioSubTable, 0, sizeof(ioSubTable));

    currentSubport = 0;
}

void* ioPortGetRef(int port)
{
	return ioTable[port].count > 0 ? ioTable[port].claim[0].ref : NULL;
}

void ioPortRegister(int port, IoPortRead read, IoPortWrite write, void* ref)
{
    IoPortInfo* info = &ioTable[port];
    int i;

    for (i = 0; i < info->count; i++) {
        if (info->claim[i].ref == ref) {
            info->claim[i].read  = read;
            info->claim[i].write = write;
            return;
        }
    }

    if (info->count == IO_PORT_MAX_CLAIMS) {
        return;
    }

    info->claim[info->count].read  = read;
    info->claim[info->count].write = write;
    info->claim[info->count].ref   = ref;
    info->count++;
}


void ioPortUnregister(int port, void* ref)
{
    IoPortInfo* info = &ioTable[port];
    int i;

    for (i = 0; i < info->count; i++) {
        if (info->claim[i].ref == ref) {
            info->count--;
            /* Writes go out in claim order, so close the gap rather than
            ** swapping the last entry into it. */
            memmove(&info->claim[i], &info->claim[i + 1],
                    (size_t)(info->count - i) * sizeof(IoPortClaim));
            return;
        }
    }
}

void ioPortRegisterUnused(int idx, IoPortRead read, IoPortWrite write, void* ref)
{
    ioUnused[idx].read  = read;
    ioUnused[idx].write = write;
    ioUnused[idx].ref   = ref;
}

void ioPortUnregisterUnused(int idx)
{
    ioUnused[idx].read  = NULL;
    ioUnused[idx].write = NULL;
    ioUnused[idx].ref   = NULL;
}

void ioPortRegisterSub(int subport, IoPortRead read, IoPortWrite write, void* ref)
{
    ioSubTable[subport].read  = read;
    ioSubTable[subport].write = write;
    ioSubTable[subport].ref   = ref;
}


void ioPortUnregisterSub(int subport)
{
    ioSubTable[subport].read  = NULL;
    ioSubTable[subport].write = NULL;
    ioSubTable[subport].ref   = NULL;
}

int ioPortCheckSub(int subport)
{
    return currentSubport == subport;
}

/* A handler may claim or release ports while it runs - an I/O enabler does
** exactly that - so walk a copy instead of the live table. */
static int ioPortTakeClaims(int port, IoPortClaim* out)
{
    int count = ioTable[port].count;

    memcpy(out, ioTable[port].claim, (size_t)count * sizeof(IoPortClaim));

    return count;
}

UInt8 ioPortRead(void* ref, UInt16 port)
{
    IoPortClaim claim[IO_PORT_MAX_CLAIMS];
    UInt8 value = 0xff;
    int driven = 0;
    int count;
    int i;

    port &= 0xff;

    if (boardGetType() == BOARD_MSX && port >= 0x40 && port < 0x50) {
        if (ioSubTable[currentSubport].read == NULL) {
            return 0xff;
        }

        return ioSubTable[currentSubport].read(ioSubTable[currentSubport].ref, port);
    }

    count = ioPortTakeClaims(port, claim);

    for (i = 0; i < count; i++) {
        if (claim[i].read != NULL) {
            /* Two cards answering at once pull the bus down together. */
            value &= claim[i].read(claim[i].ref, port);
            driven = 1;
        }
    }

    if (driven) {
        return value;
    }

    if (ioUnused[0].read != NULL) {
        return ioUnused[0].read(ioUnused[0].ref, port);
    }
    if (ioUnused[1].read != NULL) {
        return ioUnused[1].read(ioUnused[1].ref, port);
    }

    return 0xff;
}

void  ioPortWrite(void* ref, UInt16 port, UInt8 value)
{
    IoPortClaim claim[IO_PORT_MAX_CLAIMS];
    int taken = 0;
    int count;
    int i;

    boardCheckFdcBoostKill(port, value);

    port &= 0xff;

    if (boardGetType() == BOARD_MSX && port >= 0x40 && port < 0x50) {
        if (port == 0x40) {
            currentSubport = value;
            return;
        }
        
        if (ioSubTable[currentSubport].write != NULL) {
            ioSubTable[currentSubport].write(ioSubTable[currentSubport].ref, port, value);
        }
        return;
    }

    count = ioPortTakeClaims(port, claim);

    for (i = 0; i < count; i++) {
        if (claim[i].write != NULL) {
            claim[i].write(claim[i].ref, port, value);
            taken = 1;
        }
    }

    if (taken) {
        return;
    }

    if (ioUnused[0].write != NULL) {
        ioUnused[0].write(ioUnused[0].ref, port, value);
    }
    else if (ioUnused[1].write != NULL) {
        ioUnused[1].write(ioUnused[1].ref, port, value);
    }
}
