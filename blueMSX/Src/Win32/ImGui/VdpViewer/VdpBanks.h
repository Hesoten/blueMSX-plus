/*****************************************************************************
**
** The VRAM banks of a V9968 as the VDP Viewer last saw them.
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
#ifndef VDP_BANKS_H
#define VDP_BANKS_H

#include <vector>
#include "ViewPicture.h"
#include "VdpDecode.h"
extern "C" {
#include "VdpDebug.h"
}

/* Each bank's bytes from the copies the feed brings: the shown bank every copy, the
** others one at a time, so a bank not shown is up to VDP_DEBUG_BANKS copies old. */
class VdpBankStore {
public:
    static const int BANKS = VDP_DEBUG_BANKS;
    static const int BLOCKS = VDP_DEBUG_VRAM_SIZE >> 8;

    void forget();
    void note(const VdpDebugState* s);
    /* Once a drawn frame: fades the writes, and forgets them when a step starts. */
    void fade();

    bool         known(int b) const { return isKnown[b]; }
    /* Every byte the same; not known counts as not empty. */
    bool         empty(int b) const { return isKnown[b] && isEmpty[b]; }
    int          emptyCount() const;
    /* Written within the last fade time. */
    bool         writing(int b) const { return hot[b] > 0.02f; }
    /* How lately each 256 byte block was written, 1 just now. */
    const float* heat(int b) const { return blockHeat[b]; }
    const UInt8* bytes(int b) const { return isKnown[b] ? copy[b].data() : NULL; }
    /* Bumps when the bank's bytes changed. */
    unsigned     version(int b) const { return ver[b]; }
    /* R#29 pointed the sprites at it since the store started, or does now. */
    bool         spriteBank(int b) const { return (sprSeen >> b) & 1; }
    /* A copy of it was not empty since the store started; it stays so when emptied. */
    bool         used(int b) const { return (usedSeen >> b) & 1; }
    bool         allKnown() const;

private:
    std::vector<UInt8> copy[BANKS];
    bool     isKnown[BANKS] = {};
    bool     isEmpty[BANKS] = {};
    float    blockHeat[BANKS][BLOCKS] = {};
    float    hot[BANKS] = {};
    unsigned ver[BANKS] = {};
    UInt32   hitsBase[BANKS] = {};
    bool     hitsSet = false;
    bool     banked = false;      /* the last copy had banks */
    unsigned sprSeen = 0;
    unsigned usedSeen = 0;
    unsigned step = 0;
    void take(int b, const UInt8* src);
};

/* Small pictures of the banks, size px square: every page read as l and mode say, in a grid, averaged down. */
class VdpBankThumbs {
public:
    ~VdpBankThumbs();
    void forget();
    /* Draws again at most budget pictures whose bank or way of reading changed. */
    void update(const VdpDebugState* s, const VdpBankStore& store, const VdpLayout& l, int mode, int size, int budget);
    /* The bank's picture, or NULL before it is drawn. */
    const ViewPicture* picture(int b) const { return pic[b].w > 0 ? &pic[b] : NULL; }

private:
    ViewPicture    pic[VdpBankStore::BANKS];
    unsigned       drawnVer[VdpBankStore::BANKS] = {};
    unsigned       drawnKey[VdpBankStore::BANKS] = {};
    VdpDebugState* scratch = NULL;
    VdpImage       page;
};

#endif
