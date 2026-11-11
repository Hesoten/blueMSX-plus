/*****************************************************************************
**
** Log of geo3d RUNs as each starts, grouped by the screen update drawn.
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
#ifndef GEO3D_RUN_LOG_H
#define GEO3D_RUN_LOG_H

#include <map>
#include <memory>
#include <vector>
#include "MsxTypes.h"
#include "Geo3DDebug.h"

/* A RUN, its items and commands numbered from 0. */
struct Geo3DRunEntry {
    Geo3DDebugRun               run;
    std::vector<Geo3DDebugItem> items;
    std::vector<Geo3DDebugCmd>  cmds;
    bool                        cmdsDropped = false;   /* its commands let go for the cap */
    bool                        settled = false;       /* no capture need bring it again: ended and as whole as one gives it, or logged */
    /* Lets go of the commands; the items stay, cut, as they need them only for the card. */
    void dropCmds()
    {
        std::vector<Geo3DDebugCmd>().swap(cmds);
        for (Geo3DDebugItem& it : items) it.cmdFirst = it.cmdCount = 0;
        run.cmdFirst = run.cmdCount = 0;
        cmdsDropped = true;
    }
};
/* Shared and never changed once made: a change makes a new one. */
typedef std::shared_ptr<const Geo3DRunEntry> Geo3DRunPtr;
typedef std::map<UInt32, Geo3DRunPtr> Geo3DRunMap;

/* RUNs one after another on one engine page: the picture they draw. */
struct Geo3DRunCycle {
    int    page = -1;
    UInt32 first = 0, last = 0;
    bool   closed = false;
    bool   gap = false;         /* RUNs before first were not seen or not kept */
};

/* Puts e on the open cycle while it draws on the same engine page (or pair, under flat
** interlace) and none was missed before it; else closes that cycle and opens one. */
void geo3dRunAppend(Geo3DRunMap& runs, std::vector<Geo3DRunCycle>& cycles, const Geo3DRunPtr& e,
                    int page, bool gap, bool pair);
/* The caps, on every cycle or on the latest only, then the cycles kept: the open one, the
** last closed one of each page, and the one with RUN pinSeq (0 for none). */
void geo3dRunTrim(Geo3DRunMap& runs, std::vector<Geo3DRunCycle>& cycles, UInt32 pinSeq, bool latestOnly);

/* What the log holds: RUNs of one geo3d and epoch, folded for an engine space of rows. */
struct Geo3DRunLogView {
    UInt32 version = 0;         /* changes with each publish; 0 before the first */
    int    cartridge = 0;
    UInt32 epoch = 0;
    int    rows = 0;
    Geo3DRunMap                runs;
    std::vector<Geo3DRunCycle> cycles;
};

struct Geo3DRunLog;

/* UI thread. One log at a time: open returns NULL when one is open or there is no room.
** Close waits for the emulation thread to let go of it. */
Geo3DRunLog* geo3dRunLogOpen();
void         geo3dRunLogClose(Geo3DRunLog* l);
/* Takes in each RUN as it starts while armed; disarmed, it lets go of what it holds. */
void         geo3dRunLogArm(Geo3DRunLog* l, bool on);
bool         geo3dRunLogArmed(const Geo3DRunLog* l);
/* The geo3d to follow (cartridge as in its snapshot), its engine's rows (0 for no bitmap
** mode) and whether a page pair is one picture; a new geo3d or rows starts the fold again. */
void         geo3dRunLogConfig(Geo3DRunLog* l, int cartridge, int rows, bool pair);
/* Copies what it holds into out when it changed since out->version; true then. */
bool         geo3dRunLogTake(Geo3DRunLog* l, Geo3DRunLogView* out);

#endif
