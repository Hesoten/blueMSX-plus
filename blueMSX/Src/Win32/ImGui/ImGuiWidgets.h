/*****************************************************************************
**
** Small ImGui building blocks shared by the tool windows.
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
#ifndef IMGUI_WIDGETS_H
#define IMGUI_WIDGETS_H

#include <math.h>
#include "imgui.h"
#include "ImGuiHost.h"

/* Controls shaped like Windows 11 ones, for the themed tool windows. Sizes
** are in px at 96 DPI; controls are as high as ImGui::GetFrameHeight(). */
namespace ui {

/* v px at 96 DPI in screen pixels for the current window, rounded. */
float Px(float v);

/* A font of the window at em px (0: the role's usual size); pair with PopFont. */
void  PushFont(UiFont role, float em = 0);
void  PopFont();

/* A rounded panel with a title, and an optional subtitle on the right. It
** takes the width left on the line and grows with what is drawn in it. */
void BeginCard(const char* id, const char* title, const char* subtitle = NULL);
void EndCard();

/* A card without a title, its padding and least height in px at 96 DPI. */
struct CardStyle { float padX = 16, padTop = 12, padBottom = 12, minHeight = 0; };
void BeginCardEx(const char* id, const CardStyle& style);

/* A card body's height held: it grows at once but shrinks only after 1.5 s of shorter
** content, to the tallest of those. Call HoldHeight after the body, y0 its first cursor y;
** a frame without the call or a new font size starts it afresh, a new width does not. */
struct HeightHold { float latch = 0, shorter = 0, font = 0; double since = -1; int frame = -2; };
void HoldHeight(HeightHold& h, float y0);

/* ---- icons ---- */

/* Segoe Fluent Icons glyphs, as UTF-8. */
#define UI_ICON_SCREEN        "\xEE\x9F\xB4"  /* U+E7F4 TVMonitor */
#define UI_ICON_PATTERNS      "\xEF\x83\xA2"  /* U+F0E2 GridView */
#define UI_ICON_VRAM          "\xEE\xA5\x90"  /* U+E950 Processing */
#define UI_ICON_PINNED        "\xEE\xA1\x81"  /* U+E841 PinFill */
#define UI_ICON_VIEW          "\xEE\xA2\x90"  /* U+E890 View */
#define UI_ICON_EDIT          "\xEE\x9C\x8F"  /* U+E70F Edit */
#define UI_ICON_LIGHTNING     "\xEE\xA5\x85"  /* U+E945 LightningBolt */
#define UI_ICON_CHEVRON_DOWN  "\xEE\x9C\x8D"  /* U+E70D ChevronDown */
#define UI_ICON_CHEVRON_UP    "\xEE\x9C\x8E"  /* U+E70E ChevronUp */
#define UI_ICON_REMOVE        "\xEE\x9C\xB8"  /* U+E738 Remove */
#define UI_ICON_ADD           "\xEE\x9C\x90"  /* U+E710 Add */
#define UI_ICON_BORDERS       "\xEE\xA6\xA6"  /* U+E9A6 FitPage */
#define UI_ICON_PIXEL         "\xEE\x9C\xB9"  /* U+E739 Checkbox */
#define UI_ICON_BAND          "\xEE\xA3\xBD"  /* U+E8FD BulletedList */
#define UI_ICON_FIT           "\xEE\x9D\x80"  /* U+E740 FullScreen */
#define UI_ICON_GRID          "\xEE\xA0\x8A"  /* U+E80A, a 3x3 grid */
#define UI_ICON_PAGES_GRID    "\xEE\xB2\xA5"  /* U+ECA5 Tiles */
#define UI_ICON_TABLES        "\xEE\xA3\xAC"  /* U+E8EC Tag */
#define UI_ICON_BITMAP        "\xEE\xA4\x9B"  /* U+E91B Photo */
#define UI_ICON_NUMBERS       "\xEE\xA2\xAC"  /* U+E8AC Rename */

/* Whether the window has an icon font; without one icons are not drawn. */
bool  HasIcons();
/* glyph em px at 96 DPI high, its box centred on c. */
void  DrawIcon(ImDrawList* dl, ImVec2 c, const char* glyph, float em, ImU32 col);
/* fg over bg as one opaque colour, for fills that must not add up where they overlap. */
ImU32 Over(ImU32 fg, ImU32 bg);
/* c with its alpha replaced by a, 0 to 255. */
ImU32 WithAlpha(ImU32 c, int a);

/* A subtle button showing glyph, or fallback without the icon font; checked
** fills it with the accent as a toggle that is on. size in px at 96 DPI. True when clicked. */
bool IconButton(const char* id, const char* glyph, const char* fallback, const char* tip,
                bool checked = false, ImVec2 size = ImVec2(28, 24), float em = 16);
/* Its width on screen, w px at 96 DPI or wider for the fallback. */
float IconButtonWidth(const char* glyph, const char* fallback, float w = 28);

/* A tiny pixel-art icon: rows of '#' (drawn) and '.' (nothing), one string per row; smaller,
** if not NULL, a smaller drawing of the same thing for the scales whole cells miss. */
struct PixelArt { int w, h; const char* const* rows; const PixelArt* smaller; };
extern const PixelArt UI_ART_FILM;   /* frames one under the other */
/* What a pixel-art icon draws around its art: nothing, a dashed frame round it, or a checker behind it. */
enum PixelDecor { DECOR_NONE, DECOR_FRAME, DECOR_CHECKER };
/* art centred on c in whole screen pixels per art pixel: of art and its smaller drawings, the one
** drawn widest within em px at 96 DPI, art itself on a tie. */
void DrawPixelArt(ImDrawList* dl, ImVec2 c, const PixelArt& art, PixelDecor decor, ImU32 col, float em = 16);
/* IconButton with art in place of a glyph; drawn with or without the icon font, unless there is a
** fallback: it then shows that without the icon font, as IconButton does. */
bool PixelIconButton(const char* id, const PixelArt& art, PixelDecor decor, const char* tip,
                     bool checked = false, ImVec2 size = ImVec2(28, 24), const char* fallback = NULL);

/* A tool's state: a dot of tone and a word. */
void StatusDot(const char* word, int tone);

/* ---- tabs ---- */

/* art, if not NULL, is drawn in place of the glyph, also only with the icon font. */
struct TabItem { const char* glyph; const char* label; const PixelArt* art = NULL; };
/* Windows 11 tabs on one row that never wraps; the chosen one is joined to
** the card BeginTabCard draws right under it. True when the choice changed. */
bool TabView(const char* id, int* selected, const TabItem* items, int count);
/* The card under the tabs, height px on screen; 0 or less takes the space
** left less -height. What is drawn until EndTabCard goes inside it. */
void BeginTabCard(const char* id, float height = 0);
void EndTabCard();

/* ---- splitter ---- */

/* The height on screen this frame of the part under a splitter: *size (px at
** 96 DPI) or the drag, within lo..hi, hi when they cross. A drag sets *size. */
float SplitSize(const char* id, float* size, float lo, float hi);
/* The splitter at the cursor, w x h, over a part px high on screen. */
void  SplitBar(const char* id, float w, float h, float px);
/* The width on screen this frame of the part right of a splitter: *size (px at
** 96 DPI) or the drag, within lo..hi, hi when they cross. A drag sets *size. */
float SplitSizeX(const char* id, float* size, float lo, float hi);
/* The splitter at the cursor, w x h, beside a part px wide on screen. */
void  SplitBarX(const char* id, float w, float h, float px);

/* ---- command bar ---- */

/* Rows of 40 px with a divider under them; items go on the next row when
** the row is full, so a narrow window gets a second row, never squeezed items. */
void  BeginCommandBar(const char* id);
/* Puts the cursor for the next item, w by h screen px (h 0: a control's height). */
void  CommandBarItem(float w, float h = 0);
/* The row's last items, w px in all, flush right. */
void  CommandBarRight(float w);
void  CommandBarSeparator();
void  EndCommandBar();

/* A subtle toggle with an icon and a label; on, it is filled with the accent. */
float AppBarToggleWidth(const char* glyph, const char* label);
bool  AppBarToggle(const char* label, const char* glyph, bool* on, const char* tip = NULL);

/* A button that opens a menu under it: subtle, or standard (raised) for a
** value such as a zoom. Call EndDropDownButton only when it returned true. */
float DropDownButtonWidth(const char* glyph, const char* label, bool standard = false, float minW = 0);
bool  BeginDropDownButton(const char* id, const char* glyph, const char* label, bool standard = false,
                          float minW = 0, const char* tip = NULL);
void  EndDropDownButton();
/* Entries of the open menu. True when chosen; a disabled one says why on hover. */
bool  MenuRadio(const char* label, bool on, bool enabled = true, const char* disabledTip = NULL);
bool  MenuCheck(const char* label, bool* on, bool enabled = true, const char* disabledTip = NULL);
void  MenuSeparator();

/* ---- expanders, panes, status bar ---- */

/* A card that folds to its header: the title, a one-line summary and a chevron; legend, if not
** 0, is a short bar of that colour before the summary. Px at 96 DPI, but trailW is screen px kept
** free left of the chevron (ExpanderTrail). Returns *open; call EndExpander either way. */
struct ExpanderStyle { float headerH = 48, padLeft = 14, padRight = 6, bodyTop = 12, bodyBottom = 12;
                       ImU32 legend = 0; bool divider = true; float trailW = 0; };
bool BeginExpander(const char* id, const char* title, const char* summary, bool* open,
                   const ExpanderStyle& style = ExpanderStyle());
void EndExpander();
/* The last BeginExpander's trail: the rect a..b left of the chevron, trailW wide. */
void ExpanderTrail(ImVec2* a, ImVec2* b);

/* A pane of cards that folds to a narrow rail at the right; true while open
** (draw its contents then). Call EndInspectorPane either way. */
bool  BeginInspectorPane(const char* id, bool* open, ImVec2 size, const char* closeTip, const char* openTip);
void  EndInspectorPane();
/* The rail's width on screen. */
float InspectorRailWidth();

/* What the window's status bar says this frame; the last call wins. */
void  StatusText(const char* fmt, ...) IM_FMTARGS(1);
/* One line along the window's bottom edge, ellipsised; StatusBarHeight high. */
float StatusBarHeight();
void  StatusBar();

/* A heading inside a card or a page. */
void SectionTitle(const char* text);

/* Options side by side in one frame, the chosen one raised in a neutral fill; em 0 is the body size,
** 12 the small one. Items with their bit set in disabled are dimmed and say disabledTip on hover, the
** others tip; those in dim are only dimmed; ids gives ImGui ids, else the index. True when any item,
** chosen or not, is pressed. */
bool Segmented(const char* id, int* selected, const char* const* items, int count, float em = 0,
               unsigned disabled = 0, const char* disabledTip = NULL, unsigned dim = 0, const int* ids = NULL,
               const char* tip = NULL);
/* The size on screen Segmented takes for these items. */
float SegmentedWidth(const char* const* items, int count, float em = 0);
float SegmentedHeight(float em = 0);

/* A drop-down with a chevron; width in px at 96 DPI, 0 takes ImGui's item width. Call EndCombo
** only when BeginCombo returned true. */
bool BeginCombo(const char* id, const char* preview, float width = 0);
void EndCombo();
/* The width, in px at 96 DPI, at which BeginCombo shows each preview whole; never below min. */
float ComboWidth(const char* const* previews, int count, float min);
/* An entry of the open drop-down, its text dimmed with dim. True when chosen. */
bool ComboItem(const char* label, bool selected, bool dim = false);
bool Combo(const char* id, int* current, const char* const* items, int count, float width = 0);

/* size 0 fits the label. True when clicked. */
bool Button(const char* label, ImVec2 size = ImVec2(0, 0));
/* The one action a view is about. */
bool AccentButton(const char* label, ImVec2 size = ImVec2(0, 0));
/* Without a frame, as in a toolbar; `selected` keeps it lit as a toggle. */
bool SubtleButton(const char* label, bool selected = false, ImVec2 size = ImVec2(0, 0));

enum UiTone { TONE_NEUTRAL, TONE_SUCCESS, TONE_CAUTION, TONE_CRITICAL };

/* A rounded label such as LIVE or PAUSED. */
void StatusPill(const char* text, UiTone tone);

/* text on one line from pos, cut with an ellipsis at maxX; while hovered, a tooltip holds
** the whole text when cut, then more when given. col 0 is the text colour. True when it was cut. */
bool TextFit(ImDrawList* dl, ImVec2 pos, float maxX, const char* text, ImU32 col, bool hovered, const char* more = NULL);
/* TextFit as an item at the cursor, as wide as the space left; tip false: no tooltip. */
bool TextFitItem(const char* text, ImU32 col = 0, bool tip = true, const char* more = NULL);
/* A tooltip of prose for the last item, wrapped as the others are. */
void ItemTip(const char* text);
/* A tooltip for the last item, disabled or not, unwrapped. NULL or "" shows none. */
void ItemTooltip(const char* tip);

/* A two-column table of names and values, each value on one line, cut with an ellipsis.
** Call EndKeyValue only when BeginKeyValue returned true. */
bool BeginKeyValue(const char* id);
void KeyValue(const char* key, const char* fmt, ...) IM_FMTARGS(2);
/* The value in the monospaced font, for numbers and addresses. */
void KeyValueMono(const char* key, const char* fmt, ...) IM_FMTARGS(2);
/* As KeyValue and KeyValueMono, tip (NULL: none) shown on hover under the value. */
void KeyValueTip(const char* key, const char* tip, const char* fmt, ...) IM_FMTARGS(3);
void KeyValueMonoTip(const char* key, const char* tip, const char* fmt, ...) IM_FMTARGS(3);
void EndKeyValue();

/* A square of colour rgb (IM_COL32), size at 96 DPI; tip shows on hover.
** True when clicked. */
bool ColorSwatch(const char* id, ImU32 rgb, float size, bool selected = false, const char* tip = NULL);

/* Zoom N: a TV line is N screen pixels high, so a 256-dot line is 256 * N wide in every mode. */
struct CanvasZoom {
    int  zoom = 2;         /* 1 to 32 */
    bool fit  = false;     /* the largest scale the space allows instead */
};

struct CanvasState {
    CanvasZoom  own;
    CanvasZoom* shared = NULL;  /* one zoom for several canvases, else own */
    CanvasZoom&       z()       { return shared ? *shared : own; }
    const CanvasZoom& z() const { return shared ? *shared : own; }
    bool grid    = false;  /* pixel lines, drawn from 4 screen pixels up */
    bool checker = false;  /* a checkerboard behind transparent pixels */
    /* The shape of an image pixel, e.g. 1:2 for 512 dots on a TV; the view
    ** sets it before the toolbar. lineRows: image rows per TV line, 2 for half lines. */
    int  aspectX = 1, aspectY = 1, lineRows = 1;
    float gutter = 0;      /* px at 96 DPI left of the image, for marks per image line */
    bool  frameless = false;  /* no frame or tint: the well sits in a card */
    bool  panning = false;    /* a left drag is scrolling the picture */
    bool  pressEmpty = false; /* the left button went down beside the picture */
    /* Last frame's picture, for the wheel's zoom about the mouse. */
    bool   hovered = false;
    int    shownFrame = -1;
    float  shownZoom = 0, wheel = 0;  /* wheel: turns not stepped yet, e.g. from a touchpad */
    int    wheelOwned = 0;            /* the wheel axes taken from ImGui: 1 vertical, 2 sideways */
    ImVec2 shownAt = ImVec2(0, 0), shownScale = ImVec2(1, 1), shownWell = ImVec2(0, 0);
    /* A picture composed of parts, in image px, stepX image px a dot across: the checker
    ** and the grid go on them only. None: on the whole image. */
    struct Part { float x0, y0, x1, y1; int stepX; };
    Part   parts[2] = {};
    int    partCount = 0;
};

/* Where the image is on the screen, for drawing over it. */
struct CanvasXf {
    ImVec2 origin;         /* screen position of image pixel 0,0 */
    ImVec2 scale;          /* screen pixels per image pixel, per axis */
    ImVec2 clipMin, clipMax;
    ImVec2 gutterMin, gutterMax;  /* the gutter beside the image; empty without one */
    /* From 1x up, on the screen pixel where image pixel x, y starts as nearest sampling draws it. */
    ImVec2 toScreen(float x, float y) const { return ImVec2(snap(origin.x + x * scale.x, scale.x), snap(origin.y + y * scale.y, scale.y)); }
    static float snap(float v, float s) { return s >= 1 ? ceilf(v - 0.5f) : v; }
};

typedef void (*CanvasOverlayFn)(ImDrawList* dl, const CanvasXf& xf, void* ref);

struct CanvasHit {
    bool valid;            /* the mouse is over the image */
    int  x, y;             /* the image pixel under it */
    bool clicked;          /* and the left button was clicked there, released when it could pan */
    int  gutterY;          /* the image line beside the mouse in the gutter, or -1 */
    bool clickedEmpty;     /* a click on the well beside the picture or on its gutter */
};

/* The pixel shape a state draws with: its aspect, 1:1 when unset. */
void CanvasAspect(const CanvasState& state, int* ax, int* ay);
/* Screen pixels per image pixel, per axis, at zoom u. */
ImVec2 CanvasScale(const CanvasState& state, float u);

/* [-] [2x v] [+] [Fit] for a command bar's right end: what sizes the picture, the
** zoom's menu holding the steps and the option check when given (e.g. colour 0 transparent). */
float ZoomControlsWidth();
void  ZoomControls(const char* id, CanvasState& state, bool* check = NULL, const char* checkLabel = NULL);
/* [Grid], the last of the bar's left items. */
void  GridToggle(CanvasState& state);

/* The image in a scrolling well of the given size (0: the space left), sampled nearest. A picture
** larger than the well pans by a middle drag or a left drag past 4 px (a click then counts on release),
** scrolls by the wheel (Shift: sideways) and zooms by Ctrl+wheel. overlay draws over it. */
CanvasHit Canvas(const char* id, ImTextureID tex, int w, int h, CanvasState& state,
                 ImVec2 size = ImVec2(0, 0), CanvasOverlayFn overlay = NULL, void* ref = NULL);

/* tex over a..b sampled nearest, as Canvas draws its image. */
void ImageNearest(ImDrawList* dl, ImTextureID tex, ImVec2 a, ImVec2 b, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1));
/* Images added to dl between the two are sampled nearest; nothing else may come between. */
void BeginNearest(ImDrawList* dl);
void EndNearest(ImDrawList* dl);

/* For a view that lays out its own zoomed picture in a child instead of Canvas: before
** the child, the whole zoom Ctrl+wheel steps to over last frame's picture, 0 for none. */
int  CanvasWheelZoom(CanvasState& state);
/* After the child: the zoom it showed and whether the mouse was over it, so the zoom
** controls, Fit and next frame's Ctrl+wheel work as for Canvas. */
void CanvasShown(CanvasState& state, float zoom, bool hovered);

/* Canvas's grid over a..b of a picture placed as xf says: lines a dot apart, every 8th
** stronger, none below 4 screen px a dot. */
void CanvasGrid(ImDrawList* dl, const CanvasXf& xf, ImVec2 a, ImVec2 b);

/* Image pixels start..start+count of rows rowW long, filled and outlined as one shape;
** label, when given, on a chip at its top left where it has room. */
void CanvasSpan(ImDrawList* dl, const CanvasXf& xf, unsigned start, unsigned count, unsigned rowW,
                ImU32 fill, ImU32 line, const char* label = NULL);
/* The address of every 16th row in the gutter, rowBytes a row; every 64th, 256th... where
** the labels would touch. Held at the view's left edge while scrolled across. */
void CanvasAddressRuler(ImDrawList* dl, const CanvasXf& xf, int rows, unsigned rowBytes);

}

#endif
