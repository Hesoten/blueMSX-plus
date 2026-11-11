/*****************************************************************************
**
** Sprites tab and sprite cards of the VDP Viewer in V9968 sprite mode 3.
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
#ifndef VDP_SPRITE3_VIEW_H
#define VDP_SPRITE3_VIEW_H

#include <memory>
#include "imgui.h"
#include "ImGuiWidgets.h"
#include "ChipView.h"
#include "ViewPicture.h"
#include "VdpParts.h"
#include "VdpSprite3.h"
#include "VdpRaster.h"

/* What the V99x8 view shows of the sprites while SP3 is in use. It keeps its own
** pin, a plane number, apart from the view's Focus: the card of what is pinned
** shows it while no other pin is set. */
class Sp3Panel {
public:
    Sp3Panel();
    static bool active(const VdpDebugState* s) { return sp3Active(s); }
    /* Each frame: lets go of the pin once SP3 is off or something else is pinned,
    ** and of a mode 1/2 sprite pin; while kept, lights its attributes as f's hover. */
    void track(const VdpDebugState* s, Focus& f);
    bool pinned() const { return pin >= 0; }
    void unpin() { pin = -1; }
    void forget();
    /* Esc: lets go of the line a click fixed for the line row. */
    void escape();

    void drawTab(const VdpDebugState* s, Focus& f);
    void card(const VdpDebugState* s, Focus& f);
    void selection(const VdpDebugState* s, Focus& f);
    void summary(const VdpDebugState* s, char* out, size_t size);

    /* The Screen tab's boxes and per-line counts. Before its picture: screenBegin, then
    ** screenLine for each displayed line with its image row, left edge (in image dots)
    ** and band, NULL where the picture is not of the frame end. */
    void screenBegin(const VdpDebugState* s, bool boxes, bool counts);
    void screenLine(int d, int row, int org, const VdpBand* b);
    void screenOverlay(ImDrawList* dl, const ui::CanvasXf& xf, float marksW) const;
    /* The plane or the line under the mouse on the status line; false for neither. */
    bool screenHover(const VdpDebugState* s, Focus& f, const ui::CanvasHit& h);
    /* The page strip's sprite layer: 256 dots by lines, drawn even where the frame ends with
    ** sprites off (*offAtEnd says why, else NULL); false when the frame is not lines high. */
    bool layer(const VdpDebugState* s, int lines, bool noLimit, uint32_t* out, const char** offAtEnd);
    /* Why the frame ends with no SP3 sprite on screen, or NULL. */
    const char* offReason(const VdpDebugState* s) { return frame(s, true).off; }

private:
    enum { PART_PLACE, PART_ATTRS, PART_MAG, PART_PATS, PART_COUNT };
    /* The tab's parts at a zoom, in the child's space: as the mode 1/2 tab lays them out. */
    struct Layout { ImVec2 at[PART_COUNT], size[PART_COUNT]; float scale[PART_COUNT], width, height, fitH; int tier; };

    const Sp3Frame& frame(const VdpDebugState* s, bool scanAll = false);
    void pinPlane(int n, Focus& f);
    void choosePages();
    int  sheetY(int i) const;
    int  sheetHit(int x, int y, int* row) const;
    int  boxAt(int x, int d, int only = -1) const;
    int  screenLineOfRow(int y) const;
    void screenBox(ImDrawList* dl, const ui::CanvasXf& xf, int n, bool me) const;
    void stateText(int n, char* out, size_t size, bool lead = true) const;
    void selectionBody(const VdpDebugState* s, Focus& f);
    void spriteStatus(int n, bool droppedHere) const;
    void drawBoxes(ImDrawList* dl, const ui::CanvasXf& xf) const;
    void drawPatternBoxes(ImDrawList* dl, const ui::CanvasXf& xf, int page) const;
    void drawGutter(ImDrawList* dl, const ui::CanvasXf& xf) const;

    void ensureAtlas(const VdpDebugState* s);
    void atlasImage(ImDrawList* dl, int n, ImVec2 a, float px) const;
    void swatchRow(ImDrawList* dl, const VdpDebugState* s, int ps, ImVec2 a) const;
    int  attrRows(int r) const;
    int  tallest() const;
    void header(const VdpDebugState* s);
    ImVec2 partSize(int part, int N) const;
    Layout layoutAt(float W, int N) const;
    int  fitZoom(float W, float H);
    void drawPlace(const VdpDebugState* s, Focus& f, ImVec2 at, int N);
    void drawAttrs(Focus& f, ImVec2 at, int N, float partW);
    void tileMarks(ImDrawList* dl, int n, ImVec2 a, ImVec2 b, float px) const;
    int  lineRowPick(int* why) const;
    void drawLineRow(Focus& f, ImVec2 at, int N, float partW);
    void drawMag(const VdpDebugState* s, ImVec2 at, int N, float partW);
    void drawPats(const VdpDebugState* s, Focus& f, ImVec2 at, int N, float partW);

    std::unique_ptr<Sp3Frame> fr;
    unsigned ver = ~0u, built = 0;
    UInt8 regs[64], order[SP3_PLANES];
    int  mode = -1, screenOn = -1;
    const char* lang = NULL;
    bool frScanAll = false;     /* the frame was scanned for the Screen tab */

    ViewPicture placePic, sheetPic, atlasPic;
    bool atlasAny[SP3_PLANES];    /* the atlas's tile of each plane has a dot set */
    ui::CanvasState cv;
    bool dropped = true, boxes = true;
    int  pin = -1, hot = -1, hotNext = -1;
    int  pages[16], pageCount = 0, sheetPs = 0;
    int  placeLine = -1, lineFixed = -1, rowPin = -1;   /* the line row's: hovered, clicked, pin it last saw */
    int  fitN = 0;
    float fitKey[8];
    parts::SteadyTip notesTip;
    ui::HeightHold selHold;          /* the pinned plane's card, as rows come and go */
    int            selHoldPin = -1;
    Layout lastL = {};             /* last frame's, for Ctrl+wheel about the mouse */
    ImVec2 lastO = ImVec2(0, 0);

    bool scrBoxes = false, scrCounts = false;
    int  scrLimit = SP3_LIMIT;   /* scrLimit: the emulator's, 64 when told to draw them all */
    int  scrRow[SP3_LINES], scrOrg[SP3_LINES];
    uint8_t  scrState[SP3_LINES];                 /* Sp3LineState, SP3_LINE_OFF without a row */
    uint64_t scrDrop[SP3_LINES];                  /* bit n: plane n is past scrLimit on the line */
};

#endif
