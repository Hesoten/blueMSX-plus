/*****************************************************************************
**
** Interface between the VDP Viewer shell and each chip family's view.
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
#ifndef CHIP_VIEW_H
#define CHIP_VIEW_H

#include "MsxTypes.h"

namespace ui { struct CanvasZoom; struct PixelArt; }
struct VdpViewerProperties;

/* key e.g. "screen", "patterns", "vram"; art, if not NULL, is drawn in place of the glyph. */
struct ViewDesc { const char* key; const char* label; const char* glyph; const ui::PixelArt* art = NULL; };
/* No title: the card draws its own header. An expander folds to its title and
** summary, and is folded until opened. noHold: the card keeps its own height. */
struct CardDesc { const char* key; const char* title; bool shown; bool expander; const char* summary; bool noHold = false; };
/* A table or area of VRAM, in physical addresses; series picks its colour, cap the most len gets
** in its mode. where names the lines that read it when bands differ; band is 1 + the first of
** them, or 0 when the frame has one layout. */
struct VramLane { UInt32 addr, len; int series; const char* name; const char* where; int band; UInt32 cap; };

enum FocusSpace { FOCUS_PHYS, FOCUS_LOGICAL };
/* On a V99x8, a tile or pixel in FOCUS_PHYS is picked by its line's band:
** a is the dot in that band's mode and b the display line, not a scroll line. */
enum FocusKind {
    FOCUS_NONE,
    FOCUS_VRAM,    /* a block picked on the VRAM map */
    FOCUS_TILE,    /* a name table cell: a = x, b = scroll line (0-255, past R#23) */
    FOCUS_CHAR,    /* a character of the pattern sheet: a = sheet x, b = sheet y */
    FOCUS_PIXEL,   /* a bitmap pixel: a = x, b = scroll line on a V99x8 */
    FOCUS_SPRITE   /* a = sprite number, b = scroll line; addr its attributes */
};

/* What the user points at or pinned; addr/len in `space` mark VRAM. */
struct Focus {
    UInt32 chipId;
    int    space;
    UInt32 addr, len;
    int    kind, a, b;
    bool   pinned;
};

/* Views and cards of one chip family. st is that family's snapshot, the
** same until the shell sees new content. */
class ChipView {
public:
    virtual ~ChipView() {}
    /* One line of facts for the command bar, e.g. SCREEN 5, 212 lines and NTSC 60 Hz, joined by middle dots. */
    virtual void   facts(const void* st, char* out, size_t size) = 0;
    virtual int    views(ViewDesc* out, int max, const void* st) const = 0;
    /* Under the tabs: the view's card (ui::BeginTabCard), and anything under it. */
    virtual void   drawView(int v, const void* st, Focus& f) = 0;
    virtual int    cards(CardDesc* out, int max, const void* st) const = 0;
    virtual void   drawCard(int c, const void* st, Focus& f) = 0;
    virtual int    vramLanes(const void* st, VramLane* out, int max) const = 0;
    virtual UInt32 vramSize(const void* st) const = 0;
    virtual UInt32 toPhysical(const void* st, int space, UInt32 a) const = 0;
    /* True while the chip knows how the frame's lines split into bands. */
    virtual bool   bandsKnown(const void* st) const { (void)st; return false; }
    /* Esc (key true) or a click beside a picture (false): let go of what the view keeps
    ** picked besides the focus; some of it only on the key. */
    virtual void   escape(bool key) { (void)key; }
    /* The chip changed: drop pictures and anything kept per chip. */
    virtual void   forget() = 0;
    /* The view's own toggles from the settings, once as it is made, and back to them. */
    virtual void   loadUi(const VdpViewerProperties& p) = 0;
    virtual void   saveUi(VdpViewerProperties& p) const = 0;
};

ChipView* createV99x8View();
ChipView* createV9990View();

/* Services of the shell for the views, valid while it draws. */
namespace shell {
/* Bumps whenever the shown snapshot's content changed. */
unsigned    contentVersion();
/* Bumps only when VRAM, registers or palette changed, not status alone. */
unsigned    pictureVersion();
/* Bumps when the VDP's raster logs changed. */
unsigned    rasterVersion();
/* Bumps only when VRAM or the V9990's palette changed; a picture keyed on it
** puts the registers it reads in its key. */
unsigned    vramVersion();
/* Focus as it stood at the end of the previous frame, for highlights drawn
** before this frame's hover is known. */
const Focus& shownFocus();
/* The map lane under the mouse, or -1. */
int         hoverSeries();
/* Physical VRAM block (256 bytes) under the map's mouse, or -1. */
int         hoverBlock();
/* Asks the shell to show view key ("vram", ...) next frame. */
void        requestView(const char* key);
/* The picture the shown chip drew (VdpFrameShot or V9990FrameShot), or NULL;
** *version bumps with each new one. Asked for while wantFrame is called. */
const void* frameShot(unsigned* version);
/* Called each frame a picture is on show, so that the next ones are copied;
** pixels false when only what goes with the samples is read (V99x8 only). */
void        wantFrame(bool pixels = true);
/* Called each frame Output shows its sprites (the V9990's cursors in its bitmap modes) split off. */
void        wantSpriteSplit();
/* Called each frame a V99x8 view is drawn, so the tables and palettes each line used are recorded. */
void        wantSpriteTables();
/* Lets go of the pin and of a picked band next frame, as Esc does. */
void        releaseSelection();
/* The pictures' zoom, one for every tab of both chips, for the session. */
ui::CanvasZoom& zoom();
/* The Pages card under the Screen, one for both chips: open, its height in px at
** 96 DPI, and the thumbnails' size, 0 small to 2 large. */
struct PagesState { bool open = true; float height = 200; int size = 0; };
PagesState& pages();
/* A view's scroll across its pages, and where a new size puts it. */
struct PagesScroll { float x = 0, to = -1; bool wheelOwned = false; };
/* Screen px, not px at 96 DPI, per dot of a 256-dot page at each size: 0.5, 1 and 1.5. */
float       pagesStepScale(int step);
/* The card's height besides its pages: the header, and the body's padding while open. */
float       pagesChromeH(bool open);
/* The pages' height from the splitter, 0 while folded; room is the height of the
** Screen's card and the pages. The pages keep 80 px, the card 160 first. */
float       pagesHeight(float room);
/* Under the Screen's card: the splitter, the header and its size switch. True while
** the pages are drawn: lay them out, then pagesBeginScroll, draw them, pagesEnd. */
bool        pagesHeader(float paneH, bool* sizeChanged);
/* The pages' pane, cw x ch of content, scrolling across or down when it is larger. */
void        pagesBeginScroll(float paneH, float cw, float ch, PagesScroll& sc);
void        pagesEnd(PagesScroll& sc);
/* The VRAM map's lanes this frame, *n of them; none outside the views' drawing. */
const VramLane* mapLanes(int* n);
/* "    Name - lines" at out for each of those lanes holding physical address a, as the map's hover says. */
void        laneNames(UInt32 a, char* out, size_t size);
}

#endif
