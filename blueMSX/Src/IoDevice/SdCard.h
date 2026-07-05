/*****************************************************************************
**
** SD / MMC card emulator in SPI mode.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
/* SD/MMC card emulator in SPI mode.
**
** Clean-room implementation from the SD Physical Layer Simplified
** Specification (public, SD Association).  Targets the SDHC v2.0
** command subset MSX Nextor (and similar drivers behind the
** MegaFlashROM SCC+ SD cartridge family) actually issue: CMD0/8/9/10/
** 12/13/16/17/24/55/58 + ACMD41/51.  Block-addressed, 512 byte sectors.
**
** Backing store is a regular blueMSX Disk* (raw sector image),
** identified by diskId, so the cartridge layer can reuse the existing
** Disk dialog / file dialog plumbing to swap images at runtime.
*/

#ifndef SDCARD_H
#define SDCARD_H

#include "MsxTypes.h"

typedef struct SdCard SdCard;

/* diskId: index into the global Disk table (see Disk.h).  The card is
** "inserted" iff diskPresent(diskId) is true; sdCardTransfer returns
** 0xFF (idle bus) for every byte when the slot is empty so the host
** driver sees a clean no-card condition. */
SdCard* sdCardCreate(int diskId);
void    sdCardDestroy(SdCard* sd);

/* Power-on / hardware reset.  Same effect as toggling CS with the card
** un-initialised (forces CMD0 sequence). */
void    sdCardReset(SdCard* sd);

/* Chip select line.  csActiveLow == 0 means CS is asserted (card
** listening); csActiveLow == 1 deselects.  Spec requires deselect to
** complete the current command before the next CS-low frame; we treat
** CS rising edge as a barrier that flushes the response/data pipeline. */
void    sdCardSetCs(SdCard* sd, int csActiveLow);

/* One SPI byte exchange.  mosi is what the host clocks out, return is
** what the card returns on MISO during the same 8 clocks.  Idle bus
** value is 0xFF. */
UInt8   sdCardTransfer(SdCard* sd, UInt8 mosi);

/* Convenience for cartridges that map CS state onto an address bit:
** combines sdCardSetCs(csHigh ? 0 : 1) + sdCardTransfer in one call.
** csHigh == 1 means CS deasserted (card not selected). */
UInt8   sdCardTransferCs(SdCard* sd, UInt8 mosi, int csHigh);

/* SaveState tags are scoped by the caller (cart instance) so multiple
** cards in different slots don't collide. */
void    sdCardSaveState(SdCard* sd, const char* tagPrefix);
void    sdCardLoadState(SdCard* sd, const char* tagPrefix);

#endif
