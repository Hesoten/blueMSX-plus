/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32ThemeClassic.c,v $
**
** $Revision: 1.17 $
**
** $Date: 2008-05-06 14:56:24 $
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
#include "Theme.h"
#include "Actions.h"
#include "Win32ThemeClassic.h"
#include "ArchBitmap.h"
#include "ArchMenu.h"
#include "ArchText.h"
#include "Win32TextUtf8.h"
#include "Resource.h"
#include "FileHistory.h"
#include "Properties.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NO_DEFAULT_THEME

ThemeCollection* themeClassicCreate() 
{
    return NULL;
}

ThemeCollection* themeClassicCreateDark()
{
    return NULL;
}

void themeClassicTitlebarUpdate(HWND wnd)
{
	return;
}

void themeClassicRebuild(ThemeCollection* tc)
{
    return;
}

#else

/* Resolve the _DARK resource ID paired with a Classic light bitmap.
   Returns 0 when no _DARK pair exists. */
static int classicDarkIdFor(int lightId)
{
    switch (lightId) {
    case IDB_CLASSIC_BG:        return IDB_CLASSIC_BG_DARK;
    case IDB_CLASSIC_FONT:      return IDB_CLASSIC_FONT_DARK;
    case IDB_CLASSIC_DISKA:     return IDB_CLASSIC_DISKA_DARK;
    case IDB_CLASSIC_DISKB:     return IDB_CLASSIC_DISKB_DARK;
    case IDB_CLASSIC_CAS:       return IDB_CLASSIC_CAS_DARK;
    case IDB_CLASSIC_CAPS:      return IDB_CLASSIC_CAPS_DARK;
    case IDB_CLASSIC_KANA:      return IDB_CLASSIC_KANA_DARK;
    case IDB_CLASSIC_FS:        return IDB_CLASSIC_FS_DARK;
    case IDB_CLASSIC_AS:        return IDB_CLASSIC_AS_DARK;
    case IDB_CLASSIC_RESET:     return IDB_CLASSIC_RESET_DARK;
    case IDB_CLASSIC_PLAY:      return IDB_CLASSIC_PLAY_DARK;
    case IDB_CLASSIC_PAUSE:     return IDB_CLASSIC_PAUSE_DARK;
    case IDB_CLASSIC_STOP:      return IDB_CLASSIC_STOP_DARK;
    case IDB_CLASSIC_CART1:     return IDB_CLASSIC_CART1_DARK;
    case IDB_CLASSIC_CART2:     return IDB_CLASSIC_CART2_DARK;
    case IDB_CLASSIC_BTDISKA:   return IDB_CLASSIC_BTDISKA_DARK;
    case IDB_CLASSIC_BTDISKB:   return IDB_CLASSIC_BTDISKB_DARK;
    case IDB_CLASSIC_BTCAS:     return IDB_CLASSIC_BTCAS_DARK;
    case IDB_CLASSIC_BTSIZE:    return IDB_CLASSIC_BTSIZE_DARK;
    case IDB_CLASSIC_BTOPTIONS: return IDB_CLASSIC_BTOPTIONS_DARK;
    case IDB_CLASSIC_BTHELP:    return IDB_CLASSIC_BTHELP_DARK;
    case IDB_CLASSIC_FONT2:     return IDB_CLASSIC_FONT2_DARK;
    case IDB_CLASSIC_BGSMALL:   return IDB_CLASSIC_BGSMALL_DARK;
    case IDB_CLASSIC_FPS:       return IDB_CLASSIC_FPS_DARK;
    case IDB_CLASSIC_FREQ:      return IDB_CLASSIC_FREQ_DARK;
    default:                    return 0;
    }
}

/* Set during theme construction (single-threaded) so loadClassicResBitmap
   knows which resource set to load. */
static int s_classicBuildDark = 0;

/* Menu strip palette for themePageCreate; dark values match the
   per-pixel transform applied to the classic_dark bitmaps. */
static unsigned long classicMenuBgColor(void)
{
    return s_classicBuildDark ? archRGB(45, 46, 46)
                              : archRGB(219, 221, 224);
}
static unsigned long classicMenuFocusColor(void)
{
    return s_classicBuildDark ? archRGB(64, 64, 110)
                              : archRGB(128, 128, 255);
}
static unsigned long classicMenuTextColor(void)
{
    return s_classicBuildDark ? archRGB(220, 220, 224)
                              : archRGB(0, 0, 0);
}

/* Load a Classic bitmap, preferring the _DARK variant during a dark
   build.  Missing _DARK entries fall back to the light artwork so the
   dark set can be filled in incrementally. */
static ArchBitmap* loadClassicResBitmap(int id)
{
    if (s_classicBuildDark) {
        int darkId = classicDarkIdFor(id);
        if (darkId != 0) {
            ArchBitmap* darkBm = archBitmapCreateFromId(darkId);
            if (darkBm != NULL) return darkBm;
        }
    }
    return archBitmapCreateFromId(id);
}

/* Design-y of the divider band's top row in the Classic background
   bitmap; anchoring here avoids odd-zoom rounding wobble. */
#define CLASSIC_BG_DIVIDER_Y 19

static ThemePage* themeCreateSmall()
{
    /* vy = vertical offset to push the whole theme down so the
       bitmap's divider top (design-y CLASSIC_BG_DIVIDER_Y) lands at
       screen-y = menuH (just under the menu strip). */
    const int vy = archMenuStripHeight() - CLASSIC_BG_DIVIDER_Y;
    ThemePage* theme = themePageCreate("small",
                                   328,                 // Width
                                   318 + vy,            // Height (extended for shifted content)
                                   4,                   // Emu pos x
                                   51 + vy,             // Emu pos y
                                   320,                 // Emu width
                               240,                 // Emu height
                               0, 
                               0, 
                               328,                 // Menu width (= page width, no right gap)
                               classicMenuBgColor(),
                               classicMenuFocusColor(),
                               classicMenuTextColor(),
                               0,
                               0,
                               NULL);

    /* Background drawn at (0, vy) so the bitmap divider (design-y
       CLASSIC_BG_DIVIDER_Y) sits right below the menu strip. */
    themePageAddImage(theme, activeImageCreate(0, vy, 1, loadClassicResBitmap(IDB_CLASSIC_BGSMALL), 1),    THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    themePageAddText(theme, activeTextCreate(283, 278 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT2), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_VERSION, THEME_TRIGGER_NONE);

    themePageAddImage(theme, activeImageCreate(204, 300 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_DISKA), 2),  THEME_TRIGGER_IMG_DISKA, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(222, 300 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_DISKB), 2),  THEME_TRIGGER_IMG_DISKB, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(240, 300 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_CAS), 2),    THEME_TRIGGER_IMG_CAS, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(269, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_CAPS), 2),   THEME_TRIGGER_IMG_CAPS, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(284, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_KANA), 2),   THEME_TRIGGER_IMG_KANA, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(299, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_FS), 2),     THEME_TRIGGER_IMG_FS, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(314, 302 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_AS), 2),     THEME_TRIGGER_IMG_AS, THEME_TRIGGER_NONE);

    themePageAddImage(theme, activeImageCreate(68, 298 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_FPS), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(177, 298 + vy, 2, loadClassicResBitmap(IDB_CLASSIC_FREQ), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);

    themePageAddText(theme, activeTextCreate(143, 299 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT), 0, 256, 5, 0, 0, 1),  THEME_TRIGGER_TEXT_FREQ, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(55,  299 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT), 0, 256, 2, 0, 0, 1),  THEME_TRIGGER_TEXT_FPS, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(96, 299 + vy, 256, loadClassicResBitmap(IDB_CLASSIC_FONT), 0, 256, 5, 0, 0, 0),   THEME_TRIGGER_TEXT_CPU, THEME_TRIGGER_NONE);

    themePageAddDualButton(theme, activeDualButtonCreate(8, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_RESET),
                                              (ButtonEvent)actionEmuResetHard, 0, 0, 
                                              (ButtonEvent)actionMenuReset, 8, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(52, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_CART1),
                                              (ButtonEvent)actionCartInsert1, 0, 0, 
                                              (ButtonEvent)actionMenuCart1, 52, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(90, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_CART2),
                                              (ButtonEvent)actionCartInsert2, 0, 0, 
                                              (ButtonEvent)actionMenuCart2, 90, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(128, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTDISKA),
                                              (ButtonEvent)actionDiskInsertA, 0, 0, 
                                              (ButtonEvent)actionMenuDiskA, 128, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(166, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTDISKB),
                                              (ButtonEvent)actionDiskInsertB, 0, 0, 
                                              (ButtonEvent)actionMenuDiskB, 166, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(204, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTCAS),
                                              (ButtonEvent)actionCasInsert, 0, 0, 
                                              (ButtonEvent)actionMenuCassette, 204, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(251, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTSIZE),
                                              (ButtonEvent)actionWindowSize2x, 0, 0, 
                                              (ButtonEvent)actionMenuZoom, 251, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(289, 22 + vy, 5, 0, loadClassicResBitmap(IDB_CLASSIC_BTOPTIONS),
                                              (ButtonEvent)actionPropShowEmulation, 0, 0, 
                                              (ButtonEvent)actionMenuOptions, 289, 47 + vy, 0), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    return theme;
}


/* NN-stretch the X2 bitmap to (z/2)x; frameCount keeps per-frame width
   integer so odd zooms don't garble sprite-sheet glyphs. */
static ArchBitmap* loadClassicBitmap(int z, int idX2, int frameCount)
{
    ArchBitmap* base = loadClassicResBitmap(idX2);
    ArchBitmap* scaled;
    int origW, origH, dstW, dstH;

    if (z == 2 || base == NULL) {
        return base;
    }
    origW = archBitmapGetWidth(base);
    origH = archBitmapGetHeight(base);

    if (frameCount > 1 && origW % frameCount == 0) {
        int frameW    = origW / frameCount;
        int dstFrameW = (frameW * z + 1) / 2;   /* round-half-up, matches SC() */
        dstW          = dstFrameW * frameCount;
    } else {
        dstW = (origW * z + 1) / 2;
    }
    dstH = (origH * z + 1) / 2;

    scaled = archBitmapCreateScaledCopy(base, dstW, dstH);
    if (scaled == NULL) {
        return base;  /* keep the unscaled bitmap rather than returning NULL */
    }
    archBitmapDestroy(base);
    return scaled;
}

/* Data-driven layout for x2..x8 from x2 base coords scaled round-half-up.
   topShift compensates for the bitmap's scaled top empty band so the
   button row lands flush against the actual Win32 menu strip height. */
static ThemePage* themeCreateZoom(int z)
{
    ThemePage* theme;
    const int menuH = archMenuStripHeight();

    /* SC: scale x2 base coord by zoom factor, round half up. */
    #define SC(v) (((v) * (z) + 1) / 2)
    /* topShift puts the scaled divider top at screen-y = menuH so the
       full divider band shows just below the menu strip; anchoring on
       SC(19) avoids a 1-px wobble at odd zooms. */
    const int topShift = SC(CLASSIC_BG_DIVIDER_Y) - menuH;
    /* SCY: like SC but for Y, with the topShift compensation applied. */
    #define SCY(v) (SC(v) - topShift)
    /* BMP: X2 bitmap NN-stretched to (z/2)x; `frames` aligns per-frame
       width with activeImage's expectation. */
    #define BMP(name, frames) loadClassicBitmap((z), IDB_CLASSIC_##name, (frames))

    theme = themePageCreate("zoom",
                            SC(648),               // Width
                            SC(558) - topShift,    // Height (top empty band cropped)
                            SC(4),                 // Emu pos x
                            SCY(51),               // Emu pos y
                            320 * z,               // Emu width
                            240 * z,               // Emu height
                            0,                     // Menu pos x
                            0,                     // Menu pos y
                            SC(648),               // Menu width (= page width, no right gap)
                            classicMenuBgColor(),
                            classicMenuFocusColor(),
                            classicMenuTextColor(),
                            0,
                            0,
                            NULL);

    /* Background -- drawn at y = -topShift so its scaled top empty band is
       clipped against the client top edge. */
    themePageAddImage(theme, activeImageCreate(0, -topShift, 1, BMP(BG, 1), 1),
                      THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    /* Frame count = 256 = one cell per glyph so non-integer zooms don't
       scramble the version / build / status / FPS text. */
    themePageAddText(theme, activeTextCreate(SC(538), SCY(515), 256, BMP(FONT2, 256), 0, 256, 5, 0, 0, 0),
                     THEME_TRIGGER_TEXT_VERSION, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(606), SCY(515), 256, BMP(FONT2, 256), 0, 256, 5, 0, 0, 0),
                     THEME_TRIGGER_TEXT_BUILDNUMBER, THEME_TRIGGER_NONE);

    /* Status row icons. */
    themePageAddImage(theme, activeImageCreate(SC(439), SCY(540), 2, BMP(DISKA, 2), 2), THEME_TRIGGER_IMG_DISKA, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(457), SCY(540), 2, BMP(DISKB, 2), 2), THEME_TRIGGER_IMG_DISKB, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(475), SCY(540), 2, BMP(CAS,   2), 2), THEME_TRIGGER_IMG_CAS,   THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(530), SCY(542), 2, BMP(CAPS,  2), 2), THEME_TRIGGER_IMG_CAPS,  THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(571), SCY(542), 2, BMP(KANA,  2), 2), THEME_TRIGGER_IMG_KANA,  THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(602), SCY(542), 2, BMP(FS,    2), 2), THEME_TRIGGER_IMG_FS,    THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(633), SCY(542), 2, BMP(AS,    2), 2), THEME_TRIGGER_IMG_AS,    THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(238), SCY(538), 2, BMP(FPS,   2), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);
    themePageAddImage(theme, activeImageCreate(SC(342), SCY(538), 2, BMP(FREQ,  2), 2), THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);

    /* Status row text. */
    themePageAddText(theme, activeTextCreate(SC(370), SCY(539), 256, BMP(FONT, 256), 0, 256, 9, 0, 0, 0), THEME_TRIGGER_TEXT_SCREEN, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(310), SCY(539), 256, BMP(FONT, 256), 0, 256, 5, 0, 0, 1), THEME_TRIGGER_TEXT_FREQ,   THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(225), SCY(539), 256, BMP(FONT, 256), 0, 256, 2, 0, 0, 1), THEME_TRIGGER_TEXT_FPS,    THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(SC(268), SCY(539), 256, BMP(FONT, 256), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_CPU,    THEME_TRIGGER_NONE);

    /* Top button row -- buttons at fixed client y=22 (= SCY(22)) regardless
       of zoom; popup-menu offset y=47 base scaled and shifted likewise. */
    themePageAddDualButton(theme, activeDualButtonCreate(SC(8), SCY(22), 5, 0, BMP(RESET, 5),
                                          (ButtonEvent)actionEmuResetHard, 0, 0, 
                                          (ButtonEvent)actionMenuReset, SC(8), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(52), SCY(22), 4, 0, BMP(PLAY, 4),
                                          (ButtonEvent)actionEmuTogglePause, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_IMG_NOT_RUNNING, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(78), SCY(22), 4, 0, BMP(PAUSE, 4),
                                          (ButtonEvent)actionEmuTogglePause, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_IMG_RUNNING, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(104), SCY(22), 4, 0, BMP(STOP, 4),
                                          (ButtonEvent)actionEmuStop, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_IMG_NOT_STOPPED, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(136), SCY(22), 5, 0, BMP(CART1, 5),
                                          (ButtonEvent)actionCartInsert1, 0, 0, 
                                          (ButtonEvent)actionMenuCart1, SC(136), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(174), SCY(22), 5, 0, BMP(CART2, 5),
                                          (ButtonEvent)actionCartInsert2, 0, 0, 
                                          (ButtonEvent)actionMenuCart2, SC(174), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(212), SCY(22), 5, 0, BMP(BTDISKA, 5),
                                          (ButtonEvent)actionDiskInsertA, 0, 0, 
                                          (ButtonEvent)actionMenuDiskA, SC(212), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(250), SCY(22), 5, 0, BMP(BTDISKB, 5),
                                          (ButtonEvent)actionDiskInsertB, 0, 0, 
                                          (ButtonEvent)actionMenuDiskB, SC(250), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(288), SCY(22), 5, 0, BMP(BTCAS, 5),
                                          (ButtonEvent)actionCasInsert, 0, 0, 
                                          (ButtonEvent)actionMenuCassette, SC(288), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(335), SCY(22), 5, 0, BMP(BTSIZE, 5),
                                          (ButtonEvent)actionWindowSize1x, 0, 0, 
                                          (ButtonEvent)actionMenuZoom, SC(335), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddDualButton(theme, activeDualButtonCreate(SC(373), SCY(22), 5, 0, BMP(BTOPTIONS, 5),
                                          (ButtonEvent)actionPropShowEmulation, 0, 0, 
                                          (ButtonEvent)actionMenuOptions, SC(373), SCY(47), 0),
                           THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);
    themePageAddButton(theme, activeButtonCreate(SC(420), SCY(22), 4, 0, BMP(BTHELP, 4),
                                          (ButtonEvent)actionHelpShowHelp, 0, 0),
                       THEME_TRIGGER_NONE, THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    #undef SC
    #undef SCY
    #undef BMP
    return theme;
}

static ThemePage* themeCreateFullscreen() 
{
    ThemePage* theme = themePageCreate("fullscreen",
                               640,                 // Width
                               480,                 // Hidth
                               0,                   // Emu pos x
                               0,                   // Emu pos y
                               640,                 // Emu width
                               480,                 // Emu height
                               0, 
                               0, 
                               640,
                               classicMenuBgColor(),
                               classicMenuFocusColor(),
                               classicMenuTextColor(),
                               0,
                               0,
                               NULL);

    themePageAddImage(theme, activeImageCreate(-4, -51, 1, loadClassicResBitmap(IDB_CLASSIC_BG), 1), THEME_TRIGGER_NONE, THEME_TRIGGER_NONE);

    themePageAddText(theme, activeTextCreate(534, 463, 256, loadClassicResBitmap(IDB_CLASSIC_FONT2), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_VERSION, THEME_TRIGGER_NONE);
    themePageAddText(theme, activeTextCreate(602, 463, 256, loadClassicResBitmap(IDB_CLASSIC_FONT2), 0, 256, 5, 0, 0, 0), THEME_TRIGGER_TEXT_BUILDNUMBER, THEME_TRIGGER_NONE);

    return theme;
}

static void themeClassicPopulate(ThemeCollection* tc)
{
    int z;

    /* x1 has a distinct compact button layout (Play/Pause/Stop/Help omitted)
       and is generated by its own dedicated function. */
    tc->zoom[1] = themeCreate("little");
    themeAddPage(tc->zoom[1], themeCreateSmall());

    /* x2..x8 share the same layout, generated from a single x2 base spec
       scaled by the zoom factor. */
    for (z = 2; z < THEME_ZOOM_COUNT; z++) {
        tc->zoom[z] = themeCreate("zoom");
        themeAddPage(tc->zoom[z], themeCreateZoom(z));
    }

    tc->fullscreen = themeCreate("fullscreen");
    themeAddPage(tc->fullscreen, themeCreateFullscreen());
}

ThemeCollection* themeClassicCreate() 
{
    ThemeCollection* themeCollection = themeCollectionCreate();
    strcpy(themeCollection->name, "Classic");
    s_classicBuildDark = 0;
    themeClassicPopulate(themeCollection);
    return themeCollection;
}

ThemeCollection* themeClassicCreateDark()
{
    ThemeCollection* themeCollection = themeCollectionCreate();
    strcpy(themeCollection->name, "Classic Dark");
    s_classicBuildDark = 1;
    themeClassicPopulate(themeCollection);
    s_classicBuildDark = 0;
    return themeCollection;
}

/* Called from WM_DPICHANGED: re-runs theme creation so the embedded
   archMenuStripHeight() lookups pick up the new DPI. */
void themeClassicRebuild(ThemeCollection* tc)
{
    int z;
    int prevDark;
    if (tc == NULL) return;
    
    for (z = 1; z < THEME_ZOOM_COUNT; z++) {
        if (tc->zoom[z]) {
            themeDestroy(tc->zoom[z]);
            tc->zoom[z] = NULL;
        }
    }
    if (tc->fullscreen) {
        themeDestroy(tc->fullscreen);
        tc->fullscreen = NULL;
    }

    prevDark = s_classicBuildDark;
    s_classicBuildDark = (strcmp(tc->name, "Classic Dark") == 0) ? 1 : 0;
    themeClassicPopulate(tc);
    s_classicBuildDark = prevDark;
}

void themeClassicTitlebarUpdate(HWND wnd)
{
	char title[1024]={0};
	char title_old[1024]={0};
	char baseName[128];
	Properties* pProperties = propGetGlobalProperties();
	
	/* GetWindowTextU returns 0 for an empty window title; that's fine -- we
	** still want to write the new title in that case. The machineName check
	** keeps us out before a machine has been initialised. */
	GetWindowTextU(wnd, title_old, 1024);
	if (!strlen(pProperties->emulation.machineName)) return;
	
	sprintf(title,"  blueMSX - %s",pProperties->emulation.machineName);
	if (createSaveFileBaseName(baseName, pProperties, 0)) {
		strcat(title," - ");
		strcat(title,baseName);
	}
	
	if (strcmp(title,title_old)) SetWindowTextU(wnd,title);
}

#endif
