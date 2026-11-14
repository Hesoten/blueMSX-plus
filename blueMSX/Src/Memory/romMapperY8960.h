/*****************************************************************************
**
** Y8960 cartridge - the Y8960 block: declarations shared by the device blocks.
**
** Copyright (C) 2026 madscient
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
#ifndef ROM_MAPPER_Y8960_H
#define ROM_MAPPER_Y8960_H

#include "MsxTypes.h"

/* The I/O enablers sit in the memory mapped window, which belongs to the SCC
** block. Every other block asks whether its own ports are open before
** answering, the way Panasonic's FM-PAC does.
**
** Reset leaves all of them closed. The cartridge has no direct I/O port for
** the enablers, so software has to reach them through the window.
**
** The specification of 2026-10-16 struck the second OPLL and OPL2 circuits
** and the DCSG out, and may yet move what remains; the window offsets and
** the enabler bits are gathered at the top of romMapperY8960Scc.c. */
typedef enum {
    Y8960_IO_OPLL,      /* 7CH-7DH, enabler 1 bit 0 */
    Y8960_IO_OPL2,      /* C0H-C1H, enabler 2 bit 0 */
    Y8960_IO_SSGS,      /* A0H-A2H, enabler 2 bit 1 */
    Y8960_IO_OPM,       /* 7EH-7FH, enabler 2 bit 2 */
    Y8960_IO_TIMER     /* B0H-B3H, enabler 2 bit 7 */
} Y8960IoBlock;

/* True when the SCC block is absent, which leaves the enablers unreachable. */
int y8960IoEnabled(Y8960IoBlock block);

/* The window also tunnels writes straight into the sound blocks. Unlike the
** direct I/O ports these are NOT gated by the enablers: the enablers
** themselves are only reachable through this window, so gating the tunnels
** would leave no way to open anything. */
typedef enum {
    Y8960_TUN_SSGS,     /* 7FF0h register, 7FF1h value */
    Y8960_TUN_OPL2,     /* 7FF2h address,  7FF3h data  */
    Y8960_TUN_OPLL,     /* 7FF4h address,  7FF5h data  */
    Y8960_TUN_OPM,      /* 7FFBh address,  7FFCh data  */
    Y8960_TUN_COUNT
} Y8960TunnelBlock;

/* port is 0 for the address or register number and 1 for the data. */
typedef void (*Y8960TunnelWrite)(void* ref, int port, UInt8 value);

/* A block registers itself when it is created and clears the entry when it
** is destroyed. Writes to an unclaimed tunnel go nowhere. */
void y8960RegisterTunnel(Y8960TunnelBlock block, Y8960TunnelWrite write, void* ref);
void y8960UnregisterTunnel(Y8960TunnelBlock block, void* ref);

/* The MSX-MIXER block's volumes, one per sound block, written through the
** window at 7FF7h-7FFAh and 7FFDh, and a master volume at 7FFEh over all
** five. The
** ADPCM has no volume of its own here: it is mixed into the OPL2 and follows
** the OPL2's. The SCC block hands the writes over; the sound blocks ask for
** their gain while rendering. The gain also carries the block's level
** against the others, which holds without the mixer block too. */
typedef enum {
    Y8960_VOL_SSGS,
    Y8960_VOL_OPLL,
    Y8960_VOL_OPL2,
    Y8960_VOL_SCC,
    Y8960_VOL_OPM,
    Y8960_VOL_COUNT
} Y8960Volume;

void  y8960MixerWrite(Y8960Volume channel, UInt8 value);
void  y8960MixerWriteMaster(UInt8 value);
/* The channel's volume times the master times the block's level, 4.12 fixed
** point, Y8960_GAIN_ONE for unity (Y8960Pan.h). Above unity for every block
** but the OPM. */
Int32 y8960MixerGain(Y8960Volume channel);

/* The blocks are separate devices, which the whole card below creates
** together.
**
** The Destroy entry points exist for the whole-card cartridge below. These
** blocks hold no slot, so ejecting the cartridge cannot reach them the way
** it reaches a mapper; the card has to take them down by name. They are not
** for shutdown: there the device manager destroys every block itself, and
** calling them as well would free each block twice. */
int  romMapperY8960OpllexCreate(void);
void romMapperY8960OpllexDestroy(void);
int  romMapperY8960Opl2exCreate(void);
void romMapperY8960Opl2exDestroy(void);
int  romMapperY8960SsgsCreate(void);
void romMapperY8960SsgsDestroy(void);
int  romMapperY8960TimerCreate(void);
void romMapperY8960TimerDestroy(void);
int  romMapperY8960MixerCreate(void);
void romMapperY8960MixerDestroy(void);
int  romMapperY8960OpmCreate(void);
void romMapperY8960OpmDestroy(void);
/* Answers on the switched I/O (40h-4Fh), which no enabler gates. */
int  romMapperY8960SysCtrlCreate(void);
void romMapperY8960SysCtrlDestroy(void);

/* The whole card: the SCC block plus the seven other blocks, as a cartridge.
** romData may be NULL, which is the card with no firmware in it. */
int  romMapperY8960CartCreate(const char* filename, UInt8* romData,
                              int size, int slot, int sslot);

#endif
