/*****************************************************************************
**
** Drawing time strip of the geo3d tab, one bar per screen update.
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
#ifndef GEO3D_PERF_STRIP_H
#define GEO3D_PERF_STRIP_H

#include <vector>
#include "MsxTypes.h"
#include "Geo3DPerf.h"
#include "SpanHold.h"
extern "C" {
#include "Properties.h"
#include "VdpDebug.h"
}

class Geo3DPerfStrip {
public:
    Geo3DPerfStrip();
    ~Geo3DPerfStrip();
    void loadUi(const VdpViewerProperties& p);
    void saveUi(VdpViewerProperties& p) const;
    /* Each frame while s's VDP has a geo3d, on any tab: arms the trace and follows that VDP. */
    void keep(const VdpDebugState* s);
    /* Each frame the tab is drawn: takes the pictures. */
    void update();
    /* Lets go of the trace; the pictures stay. */
    void disarm();
    /* Lets go of the trace and the pictures. */
    void forget();
    /* The rate over the latest whole pictures, and the ms each was on screen on average; false with none. */
    bool effectiveFps(double* fps, double* ms) const;
    /* The card under the tab's card: the strip's height from its splitter, 0 while folded;
    ** room is the height of the tab's card and the strip. */
    float bottomHeight(float room);
    bool  open() const { return shown; }
    /* The card's splitter and header at the cursor, and the strip paneH high in it while open. */
    void  bottomCard(float paneH);
    /* The RUN of the pinned object in the picture the cards show, whose faces the picture lights; else 0. */
    UInt32 litRun() const;

    /* The command engine's space, w dots by rows rows at ppb dots a byte, that the writes are replayed in. */
    void setSpace(int w, int rows, int ppb) { spaceW = w; spaceRows = rows; spacePpb = ppb > 0 ? ppb : 1; }
    /* The writes of the picture the cards show replayed in start order: for each dot how many
    ** wrote it and who wrote it last. Clears by the Z80 and fills in colour 0 with a
    ** transparent logic op are not counted. NULL without that picture's detail. */
    struct Overdraw {
        /* A RUN's dots written, and of them those a later write covered, its own included. */
        struct Run { UInt32 run, written, hidden; };
        UInt32 number = 0;
        int    w = 0, rows = 0, ppb = 1;
        Int32  page = -1;           /* the page the picture showed on */
        std::vector<UInt8>  count;
        std::vector<UInt32> last;   /* 0 the Z80, 1 geo3d with its RUN not known, else 2 + a place in runs */
        std::vector<Run>    runs;
    };
    const Overdraw* overdraw();
    /* Whether picture number is the latest settled one. */
    bool isLatest(UInt32 number) const { return view != NULL && view->count > 0 && view->pic[view->count - 1].number == number; }
    /* Whether picture number is the one on screen at the end of the frame that began at frameStart. */
    bool onScreenAt(UInt32 number, UInt32 frameStart) const;
    bool following() const { return follow; }
    /* Picture number named by its machine frames, as the deck counts them, else just "screen update". */
    void picLabel(UInt32 number, char* out, size_t size) const;

private:
    int  selectedIndex() const;
    UInt64 backVint(int i) const;
    void draw(float h);
    void barsCard(float h);
    void chart(float h);
    void legend(float rowH);
    void whyText(int i, char* out, size_t size, char* tip, size_t tipSize) const;
    int  shownIndex() const;
    void tooltip(int i);
    float lanesHeight() const;
    void lanesCard();
    void lanes(float h);
    /* The span the lanes show for p: the held span while following, else p's own. */
    double laneSpan(const Geo3DPerfPicture& p, float widthPx);
    void spanTip(const Geo3DPerfSpan& sp);
    void runsCard();
    void derive();
    void deriveFrom(const Geo3DPerfPicture& p);
    void replay();
    /* Why the detail of picture i is not shown, or NULL when it is. */
    const char* detailNote(int i) const;

    /* A RUN of the selected picture: the VDP time and dots of the commands it issued. */
    struct RunRow { UInt32 run, obj; UInt64 fill; UInt32 cmds, dots, lrmm; };

    Geo3DPerf*     perf = NULL;
    Geo3DPerfView* view = NULL;
    bool           shown = true;        /* the card is open */
    float          heightPx = 320;      /* the card's body, px at 96 DPI */
    float          whyWidth = 330;      /* the breakdown beside the bars, px at 96 DPI */
    bool           openFailed = false;  /* no room for another drain: not tried again until forget */
    UInt32         srcId = 0;           /* the VDP the sources were found for */
    UInt64         srcAt = 0;
    bool           follow = true;       /* the selection is the latest picture */
    UInt32         selNumber = 0;
    Geo3DPerfDetail* detail = NULL;     /* the selected picture's spans, once they came */
    UInt32         derivedFor = 0;      /* the detail busy and runRows were worked out from */
    int            derivedState = -1;
    std::vector<UInt64> busy;           /* VDP or geo3d busy, merged: start, end, start, end... */
    std::vector<RunRow> runRows;
    UInt32         laneFor = 0;         /* the picture the lanes' window is for */
    double         laneLo = 0, laneHi = 0;   /* ticks from its start */
    bool           laneAuto = true;     /* not zoomed or moved: the window follows the span */
    ui::HeightHold textHold;            /* the breakdown card's lines above its RUN bars */
    ui::HeightHold bodyHold;            /* the whole card, as the RUN rows come and go */
    double         tickStep = 0;        /* the axis step in ms last chosen, 0 none */
    /* The lanes' held span while following, so the axis does not jump from picture to picture. */
    SpanHold       hold;
    Overdraw       od;
    int            spaceW = 0, spaceRows = 0, spacePpb = 1;
    UInt32         pinObj = 0;          /* the object pinned, followed from picture to picture */
    UInt32         pinRun = 0;          /* its RUN in the picture the cards show */
    /* The parts any kept picture had; kept until the pictures start again. */
    unsigned       legendSeen = 0;
    UInt32         legendLast = 0;      /* the latest picture they were taken from */
};

#endif
