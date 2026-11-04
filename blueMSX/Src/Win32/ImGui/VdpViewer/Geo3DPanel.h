/*****************************************************************************
**
** geo3d tab and cards of the VDP Viewer: drawing by a geo3d coprocessor.
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
#ifndef GEO3D_PANEL_H
#define GEO3D_PANEL_H

#include <map>
#include <vector>
#include "imgui.h"
#include "ImGuiWidgets.h"
#include "ChipView.h"
#include "ViewPicture.h"
#include "RegTables.h"
#include "VdpDecode.h"
#include "VdpRaster.h"
#include "VdpParts.h"
#include "Geo3DPerfStrip.h"
#include "SpanHold.h"
#include "Geo3DRunLog.h"

/* What the V99x8 view shows of the geo3d beside its VDP, while the snapshot has one. */
class Geo3DPanel {
public:
    Geo3DPanel();
    ~Geo3DPanel();
    static bool active(const VdpDebugState* s) { return s->geo3d.valid != 0; }
    /* The tab's icon: a cube. */
    static const ui::PixelArt* tabArt();
    void forget();
    /* The page choice and the drawing time strip, from and to the ini. */
    void loadUi(const VdpViewerProperties& p);
    void saveUi(VdpViewerProperties& p) const;
    /* Each frame the tab is not drawn: lets go of the hover. */
    void leave();

    /* Each frame, on any tab: arms the RUN log and the trace while there is a geo3d, keeps the
    ** RUNs and picks those of the page the screen shows. Lets go of a pinned face or edge once
    ** something else is pinned or its RUN is no longer kept. */
    void track(const VdpDebugState* s, const Focus& f, const VdpRasterView& raster);
    bool pinned() const { return pinSeq != 0; }
    void unpin() { pinSeq = 0; pinIndex = -1; }
    /* True once after a click pinned an item: the view then lets go of its other pins. */
    bool takeNewPin() { bool n = newPin; newPin = false; return n; }
    /* The card of what is pinned, for the pinned face or edge. */
    void selection(const VdpDebugState* s);
    /* For the Command card: the command geo3d issued that the engine runs, or NULL. */
    const char* commandNote(const VdpDebugState* s);

    /* "geo3d v2.0 (8Dh) running" after a separator, for the facts line. */
    void facts(const VdpDebugState* s, char* out, size_t size) const;
    /* One line for the folded geo3d card. */
    const char* summary(const VdpDebugState* s);
    /* The picture of the rows geo3d draws into, the frame's RUNs over it, and them from above. */
    void drawTab(const VdpDebugState* s, Focus& f, const VdpRasterView& raster);
    /* The drawing time card under the tab's card, there while the tab has a picture: its body's
    ** height, 0 while folded or not there; room is the height of the tab's card and the body. */
    float bottomHeight(const VdpDebugState* s, float room);
    bool  hasBottom() const { return bottomOn; }
    bool  bottomOpen() const { return perfStrip.open(); }
    /* After the tab's card: the card, its body paneH high. */
    void  drawBottom(float paneH) { if (bottomOn) perfStrip.bottomCard(paneH); }
    void card(const VdpDebugState* s);
    void registersCard(const VdpDebugState* s);

private:
    /* A RUN as the log or the best snapshot of it had it. */
    typedef Geo3DRunEntry RunRec;
    typedef Geo3DRunCycle Cycle;
    /* A RUN the picture shows, drawn shift rows down from its YPAGE's row. */
    struct RunRef {
        const Geo3DDebugRun*  run;
        const Geo3DDebugItem* item;
        const Geo3DDebugCmd*  cmd;
        int                   shift;
    };
    /* A face or edge of set: its RUN, its place in that RUN's items and, for a face,
    ** the DY of its command under the mouse. */
    struct Hit { int run = -1, item = -1, dy = -1; };
    void clearCache();
    void absorb(const VdpDebugState* s, int r);
    void takeLog();
    void chooseSet(const VdpDebugState* s, const VdpRasterView& raster);
    Hit  itemAt(int x, int y) const;
    bool hotItem(int r, int k) const;
    bool findInSet(UInt32 seq, int index, int* r, int* k) const;
    void followPin();
    bool sameRanges(const Geo3DDebugRun& run) const;
    static void overlay(ImDrawList* dl, const ui::CanvasXf& xf, void* ref);
    void fillFace(ImDrawList* dl, const ui::CanvasXf& xf, const RunRef& ref, const Geo3DDebugItem& it,
                  int rowLo, int rowHi, ImU32 col) const;
    bool clipToW(ImDrawList* dl, const ui::CanvasXf& xf, const Geo3DDebugRun& run) const;
    void fillQuad(ImDrawList* dl, const ui::CanvasXf& xf, const Geo3DDebugItem& it, int shift, ImU32 col) const;
    void outlineFace(ImDrawList* dl, const ui::CanvasXf& xf, const Geo3DDebugItem& it, int shift, ImU32 col, float th,
                     bool dashed = false) const;
    int  noteItems(const VdpDebugState* s, char item[][512]) const;
    void notes(char item[][512], int n, ImVec2 at, float right);
    ImVec2 legend(bool faces, bool dim);
    void selectionBody(const VdpDebugState* s);
    void pinItem(UInt32 seq, int index);
    bool findItem(UInt32 seq, int index, const RunRec** rec, int* k) const;
    bool placed(const VdpDebugState* s, const RunRec& rec);
    bool topPrepare(const VdpDebugState* s);
    void topDraw(ImDrawList* dl, ImVec2 a, ImVec2 b);
    void lightPicture(const VdpDebugState* s, const Geo3DDebugRun& run, const Geo3DDebugItem& it);
    void spanStrip(const Geo3DDebugRun& run, const Geo3DDebugItem& it, const Geo3DDebugCmd* cmd);
    void ramBar(const char* id, const char* name, int first, int count, int wptr, bool ext);
    void wordTable(const VdpDebugState* s);

    ViewPicture     pic;
    VdpImage        pages;          /* every page of the engine's mode, for the crop */
    ui::CanvasState cv;
    bool            outlines = true;
    const VdpDebugState* ovState = NULL;
    int             mode = -1;      /* the command engine's bitmap mode, or -1 */
    int             rows = 0;       /* rows of the engine's coordinate space */
    ImVec2          ovScale = ImVec2(1, 1);   /* screen px an image px across and down, as last drawn */
    /* The RUNs kept by number, in cycles by page; the latest cycle may be open. */
    Geo3DRunMap     recs;
    std::vector<Cycle> cycles;
    /* While the log has none of this geo3d, epoch and rows, the snapshots add to them. */
    bool            cacheOn = false;
    UInt32          cacheEpoch = 0, cacheSeq = 0, cacheEnd = 0;   /* the snapshot last taken in */
    UInt32          cacheLast = 0;  /* the latest RUN taken in */
    UInt32          wholeTo = 0;    /* this RUN and those before it need no capture again */
    int             cacheRows = 0;
    /* Each RUN as it starts, while the viewer shows a geo3d. */
    Geo3DRunLog*    runLog = NULL;
    bool            logFailed = false;  /* not tried again until forget */
    Geo3DRunLogView logView;
    UInt32          logTaken = 0;       /* the version of it recs hold, 0 none */
    /* What the picture shows: the page, from which row, and its cycle's RUNs. */
    std::vector<RunRef> set;
    int             setCycle = -1;
    int             pageMode = 0;        /* 0 the page the screen shows, 1 the one the latest RUN draws on */
    bool            pageShown = false;   /* the screen shows a page of the engine's mode, shownPage */
    int             shownPage = 0, picPage = 0, picStart = 0;
    /* Flat interlace weaves shownPage + 1 into shownPage, both pages one picture from picStart. */
    bool            pair = false;
    int             latestYpage = 0;
    char            shownOthers[64] = "";   /* other pages the screen shows part of */
    /* The top view's extent; fitOn false starts its far side over. */
    bool            fitOn = false;
    double          fit[4] = {};
    SpanHold        farHold;
    parts::SteadyTip notesTip;          /* the notes row's tooltip, steady as its text changes */
    ui::HeightHold  selHold;            /* the pinned face's card, as rows come and go */
    int             selHoldKey[6] = { -1 };
    bool            topAny = false;         /* something to place this frame, not all at one Z */
    bool            topFlat = false;        /* something to place, but every vertex at one Z */
    double          topCam[4] = {};         /* F, CX, W and ZNEAR of the latest RUN with items */
    /* Beside the page on the canvas, else under it, by the room and the zoom; where, in image px. */
    bool            topSide = true;
    int             topX = 0, topY = 0;
    int             topHotR = -1, topHotI = -1;   /* the item under the mouse in it, in set */
    bool            bottomOn = false;       /* the drawing time card is under the tab's card */
    /* The item under the mouse, by RUN number and face or edge number, as items move between snapshots. */
    UInt32          hotSeq = 0, hotSeqNext = 0;
    int             hotIndex = -1, hotIndexNext = -1;
    UInt32          pinSeq = 0;
    int             pinIndex = -1;
    /* The pinned item's RUN by its vertex and item RAM ranges, and which of the RUNs
    ** with those ranges it was, followed into later RUNs. */
    UInt16          pinKey[4] = {};
    int             pinFaces = 0, pinNth = 0;
    UInt32          pinEpoch = 0;       /* cacheEpoch when pinSeq was last right */
    bool            pinRenumbered = false;
    bool            newPin = false;
    /* placed() for the RUN and picture it was worked out for. */
    bool            placedPrimed = false;
    unsigned        placedVer = 0;
    UInt32          placedSeq = 0;
    bool            placedOk = false;
    char            sum[160] = "";
    char            cmdText[256] = "";
    RegFade         regFade;
    struct WordFade { Int32 prev[32]; float heat[32]; unsigned version, step; bool primed; };
    WordFade        wordFade = {};
    Geo3DPerfStrip  perfStrip;
};

#endif
