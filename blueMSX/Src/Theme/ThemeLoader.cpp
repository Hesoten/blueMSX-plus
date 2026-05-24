/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Theme/ThemeLoader.cpp,v $
**
** $Revision: 1.67 $
**
** $Date: 2008-04-03 02:31:52 $
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
#define USE_ARCH_GLOB

#include "tinyxml.h"
#include "ThemeLoader.h"
extern "C" {
#include "InputEvent.h"
#include "StrcmpNoCase.h"
#include "AppConfig.h"
#include "ArchBitmap.h"
#include "ArchMenu.h"
#include "ArchText.h"
#include "ArchFile.h"
#ifdef USE_ARCH_GLOB
#include "ArchGlob.h"
    

// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"
#endif
}

#define MAX_CLIP_POINTS 512

struct ThemeDefaultInfo {
    char* mode;
    int width;
    int height;
} themeDefaultInfo[] = {
    { "small",           320, 240 },
    { "normal",          640, 480 },
    { "triple",          960, 720 },
    { "quad",           1280, 960 },
    { "fullscreen",      640, 480 }
};

static char rootPath[512];

/* Scale used when re-parsing "normal" mode to synthesise zoom[N]. */
static double g_themeScale = 1.0;

/* XML's 22 px menu band is fixed-pixel: below-menu elements shift by
   topShift to sit flush against the runtime menu strip. */
#define DESIGN_MENU_H 22
/* Reveal area below the menu band so theme-author dividers placed just
   under the strip stay visible. */
#define EXTERNAL_THEME_TOP_GAP 6
static int g_topShift         = 0;
static int g_designMenuY      = -1;  /* sentinel = no menu band on this page */
static int g_designMenuBottom = -1;  /* = g_designMenuY + DESIGN_MENU_H */

static int themeScaledCoord(int v)
{
    if (g_themeScale == 1.0) return v;
    return (int)(v * g_themeScale + 0.5);
}

/* Zoom-scale; subtract topShift below the menu band so content sits
   flush against the runtime strip. */
static int themeScaledY(int v)
{
    int scaled = themeScaledCoord(v);
    if (g_designMenuBottom >= 0 && v >= g_designMenuBottom) {
        scaled -= g_topShift;
    }
    return scaled;
}

const char* fullPath(const char* filename)
{
    static char path[512];

    if (filename[1] == ':') {
        return filename;
    }

    strcpy(path, rootPath);

    if (path[strlen(path)-1] != '/' && path[strlen(path)-1] != '\\') {
        strcat(path, "/");
    }

    strcat(path, filename);

    return path;
}

enum ThemeInfo { THEME_SMALL = 0, THEME_NORMAL = 1, THEME_TRIPLE = 2, THEME_QUAD = 3, THEME_FULLSCREEN = 4 };

static ButtonEvent getAction(TiXmlElement* el, const char* actionTag, 
                             const char* arg1Tag, const char* arg2Tag, LONG_PTR* arg1, LONG_PTR* arg2,
                             ThemeCollection* themeCollection, Theme* theme, int dx, int dy)
{
    ButtonEvent buttonEvent = 0;

    *arg1 = 1;
    *arg2 = 1;

    {
        int iArg1 = 1, iArg2 = 1;
        el->QueryIntAttribute(arg1Tag, &iArg1);
        el->QueryIntAttribute(arg2Tag, &iArg2);
        *arg1 = iArg1;
        *arg2 = iArg2;
    }

    const char* action = el->Attribute(actionTag);
    if (action == NULL) {
        return NULL;
    }

    if (0 == strcmp(action, "switch-togglefdctiming"))  return (ButtonEvent)actionToggleFdcTiming;
    if (0 == strcmp(action, "switch-nospritelimits"))  return (ButtonEvent)actionToggleNoSpriteLimits;
    
    if (0 == strcmp(action, "switch-videohstretch"))    return (ButtonEvent)actionToggleHorizontalStretch;
    if (0 == strcmp(action, "switch-videovstretch"))    return (ButtonEvent)actionToggleVerticalStretch;
    if (0 == strcmp(action, "switch-videoscanlines"))   return (ButtonEvent)actionToggleScanlinesEnable;
    if (0 == strcmp(action, "switch-videodeinterlace")) return (ButtonEvent)actionToggleDeinterlaceEnable;
    if (0 == strcmp(action, "switch-videoblendframes")) return (ButtonEvent)actionToggleBlendFrameEnable;
    if (0 == strcmp(action, "switch-videorfmodulation"))return (ButtonEvent)actionToggleRfModulatorEnable;

    if (0 == strcmp(action, "switch-audioswitch"))      return (ButtonEvent)actionToggleMsxAudioSwitch;
    if (0 == strcmp(action, "switch-frontswitch"))      return (ButtonEvent)actionToggleFrontSwitch;
    if (0 == strcmp(action, "switch-pauseswitch"))      return (ButtonEvent)actionTogglePauseSwitch;
    
    /* TrackPopupMenu wants client-rendered (post-zoom) coords. */
#define MENU_XY()  do { *arg1 = themeScaledCoord((int)*arg1 + dx); \
                        *arg2 = themeScaledY   ((int)*arg2 + dy); } while (0)
    if (0 == strcmp(action, "menu-specialcart1"))       { MENU_XY(); return (ButtonEvent)actionMenuSpecialCart1; }
    if (0 == strcmp(action, "menu-specialcart2"))       { MENU_XY(); return (ButtonEvent)actionMenuSpecialCart2; }
    if (0 == strcmp(action, "menu-reset"))              { MENU_XY(); return (ButtonEvent)actionMenuReset; }
    if (0 == strcmp(action, "menu-run"))                { MENU_XY(); return (ButtonEvent)actionMenuRun; }
    if (0 == strcmp(action, "menu-file"))               { MENU_XY(); return (ButtonEvent)actionMenuFile; }
    if (0 == strcmp(action, "menu-cart1"))              { MENU_XY(); return (ButtonEvent)actionMenuCart1; }
    if (0 == strcmp(action, "menu-cart2"))              { MENU_XY(); return (ButtonEvent)actionMenuCart2; }
    if (0 == strcmp(action, "menu-harddisk"))           { MENU_XY(); return (ButtonEvent)actionMenuHarddisk; }
    if (0 == strcmp(action, "menu-diska"))              { MENU_XY(); return (ButtonEvent)actionMenuDiskA; }
    if (0 == strcmp(action, "menu-diskb"))              { MENU_XY(); return (ButtonEvent)actionMenuDiskB; }
    if (0 == strcmp(action, "menu-cassette"))           { MENU_XY(); return (ButtonEvent)actionMenuCassette; }
    if (0 == strcmp(action, "menu-printer"))            { MENU_XY(); return (ButtonEvent)actionMenuPrinter; }
    if (0 == strcmp(action, "menu-joyport1"))           { MENU_XY(); return (ButtonEvent)actionMenuJoyPort1; }
    if (0 == strcmp(action, "menu-joyport2"))           { MENU_XY(); return (ButtonEvent)actionMenuJoyPort2; }
    if (0 == strcmp(action, "menu-windowsize"))         { MENU_XY(); return (ButtonEvent)actionMenuZoom; }
    if (0 == strcmp(action, "menu-options"))            { MENU_XY(); return (ButtonEvent)actionMenuOptions; }
    if (0 == strcmp(action, "menu-help"))               { MENU_XY(); return (ButtonEvent)actionMenuHelp; }
    if (0 == strcmp(action, "menu-tools"))              { MENU_XY(); return (ButtonEvent)actionMenuTools; }
#undef MENU_XY
    
    if (0 == strcmp(action, "dlg-emulation"))           return (ButtonEvent)actionPropShowEmulation;
    if (0 == strcmp(action, "dlg-controls"))            return (ButtonEvent)actionPropShowEmulation;
    if (0 == strcmp(action, "dlg-video"))               return (ButtonEvent)actionPropShowVideo;
    if (0 == strcmp(action, "dlg-audio"))               return (ButtonEvent)actionPropShowAudio;
    if (0 == strcmp(action, "dlg-effects"))             return (ButtonEvent)actionPropShowEffects;
    if (0 == strcmp(action, "dlg-settings"))            return (ButtonEvent)actionPropShowSettings;
    if (0 == strcmp(action, "dlg-apearance"))           return (ButtonEvent)actionPropShowApearance;
    if (0 == strcmp(action, "dlg-language"))            return (ButtonEvent)actionOptionsShowLanguage;
    if (0 == strcmp(action, "dlg-machineeditor"))       return (ButtonEvent)actionToolsShowMachineEditor;
    if (0 == strcmp(action, "dlg-shortcuteditor"))      return (ButtonEvent)actionToolsShowShorcutEditor;
    if (0 == strcmp(action, "dlg-keyboardeditor"))      return (ButtonEvent)actionToolsShowKeyboardEditor;
    if (0 == strcmp(action, "dlg-mixer"))               return (ButtonEvent)actionToolsShowMixer;
    if (0 == strcmp(action, "dlg-help"))                return (ButtonEvent)actionHelpShowHelp;
    if (0 == strcmp(action, "dlg-about"))               return (ButtonEvent)actionHelpShowAbout;
    
    if (0 == strcmp(action, "state-load"))              return (ButtonEvent)actionLoadState;
    if (0 == strcmp(action, "state-save"))              return (ButtonEvent)actionSaveState;
    if (0 == strcmp(action, "state-quickload"))         return (ButtonEvent)actionQuickLoadState;
    if (0 == strcmp(action, "state-quicksave"))         return (ButtonEvent)actionQuickSaveState;

    if (0 == strcmp(action, "cart1-insert"))            return (ButtonEvent)actionCartInsert1;
    if (0 == strcmp(action, "cart1-remove"))            return (ButtonEvent)actionCartRemove1;
    if (0 == strcmp(action, "cart1-toggleautoreset"))   return (ButtonEvent)actionToggleCartAutoReset;
    if (0 == strcmp(action, "cart2-insert"))            return (ButtonEvent)actionCartInsert2;
    if (0 == strcmp(action, "cart2-remove"))            return (ButtonEvent)actionCartRemove2;
    
    if (0 == strcmp(action, "diska-insert"))            return (ButtonEvent)actionDiskInsertA;
    if (0 == strcmp(action, "diska-insertdir"))         return (ButtonEvent)actionDiskDirInsertA;
    if (0 == strcmp(action, "diska-remove"))            return (ButtonEvent)actionDiskRemoveA;
    if (0 == strcmp(action, "diska-quickchange"))       return (ButtonEvent)actionDiskQuickChange;
    if (0 == strcmp(action, "diska-toggleautoreset"))   return (ButtonEvent)actionToggleDiskAutoReset;
    if (0 == strcmp(action, "diskb-insert"))            return (ButtonEvent)actionDiskInsertB;
    if (0 == strcmp(action, "diskb-insertdir"))         return (ButtonEvent)actionDiskDirInsertB;
    if (0 == strcmp(action, "diskb-remove"))            return (ButtonEvent)actionDiskRemoveB;
    
    if (0 == strcmp(action, "cas-insert"))              return (ButtonEvent)actionCasInsert;
    if (0 == strcmp(action, "cas-remove"))              return (ButtonEvent)actionCasRemove;
    if (0 == strcmp(action, "cas-rewind"))              return (ButtonEvent)actionCasRewind;
    if (0 == strcmp(action, "cas-setposition"))         return (ButtonEvent)actionCasSetPosition;
    if (0 == strcmp(action, "cas-togglereadonly"))      return (ButtonEvent)actionCasToggleReadonly;
    if (0 == strcmp(action, "cas-toggleautorewind"))    return (ButtonEvent)actionToggleCasAutoRewind;
    if (0 == strcmp(action, "cas-save"))                return (ButtonEvent)actionCasSave;
    
    if (0 == strcmp(action, "window-small"))            return (ButtonEvent)actionWindowSize1x;
    if (0 == strcmp(action, "window-normal"))           return (ButtonEvent)actionWindowSize2x;
    if (0 == strcmp(action, "window-minimized"))        return (ButtonEvent)actionWindowSizeMinimized;
    if (0 == strcmp(action, "window-fullscreen"))       return (ButtonEvent)actionWindowSizeFullscreen;
    if (0 == strcmp(action, "window-togglefullscreen")) return (ButtonEvent)actionFullscreenToggle;
    
    if (0 == strcmp(action, "screenshot-normal"))       return (ButtonEvent)actionScreenCapture;
    if (0 == strcmp(action, "screenshot-small"))        return (ButtonEvent)actionScreenCaptureUnfilteredSmall;
    if (0 == strcmp(action, "screenshot-large"))        return (ButtonEvent)actionScreenCaptureUnfilteredLarge;
    
    if (0 == strcmp(action, "emu-quit"))                return (ButtonEvent)actionQuit;
    if (0 == strcmp(action, "emu-resetsoft"))           return (ButtonEvent)actionEmuResetSoft;
    if (0 == strcmp(action, "emu-resethard"))           return (ButtonEvent)actionEmuResetHard;
    if (0 == strcmp(action, "emu-resetclean"))          return (ButtonEvent)actionEmuResetClean;
    if (0 == strcmp(action, "emu-togglepause"))         return (ButtonEvent)actionEmuTogglePause;
    if (0 == strcmp(action, "emu-stop"))                return (ButtonEvent)actionEmuStop;
    if (0 == strcmp(action, "emu-togglemousecapture"))  return (ButtonEvent)actionToggleMouseCapture;
    
    if (0 == strcmp(action, "emu-setmaxspeed"))         return (ButtonEvent)actionMaxSpeedSet;
    if (0 == strcmp(action, "emu-releasemaxspeed"))     return (ButtonEvent)actionMaxSpeedRelease;
    if (0 == strcmp(action, "emu-togglemaxspeed"))      return (ButtonEvent)actionMaxSpeedToggle;
    if (0 == strcmp(action, "emu-normalspeed"))         return (ButtonEvent)actionEmuSpeedNormal;
    if (0 == strcmp(action, "emu-increasespeed"))       return (ButtonEvent)actionEmuSpeedDecrease;
    if (0 == strcmp(action, "emu-decreasespeed"))       return (ButtonEvent)actionEmuSpeedDecrease;
    
    if (0 == strcmp(action, "audio-togglerecord"))      return (ButtonEvent)actionToggleWaveCapture;
    if (0 == strcmp(action, "audio-increasevolume"))    return (ButtonEvent)actionVolumeIncrease;
    if (0 == strcmp(action, "audio-decreasevolume"))    return (ButtonEvent)actionVolumeDecrease;
    if (0 == strcmp(action, "audio-togglestereo"))      return (ButtonEvent)actionVolumeToggleStereo;
    if (0 == strcmp(action, "audio-togglemute"))        return (ButtonEvent)actionMuteToggleMaster;
    
    if (0 == strcmp(action, "video-captureload"))       return (ButtonEvent)actionVideoCaptureLoad;
    if (0 == strcmp(action, "video-captureplay"))       return (ButtonEvent)actionVideoCapturePlay;
    if (0 == strcmp(action, "video-capturerec"))        return (ButtonEvent)actionVideoCaptureRec;
    if (0 == strcmp(action, "video-capturestop"))       return (ButtonEvent)actionVideoCaptureStop;
    if (0 == strcmp(action, "video-capturestop"))       return (ButtonEvent)actionVideoCaptureSave;

    if (0 == strcmp(action, "audio-togglemutemaster"))    return (ButtonEvent)actionMuteToggleMaster;
    if (0 == strcmp(action, "audio-togglemutepsg"))       return (ButtonEvent)actionMuteTogglePsg;
    if (0 == strcmp(action, "audio-togglemutepcm"))       return (ButtonEvent)actionMuteTogglePcm;
    if (0 == strcmp(action, "audio-togglemuteio"))        return (ButtonEvent)actionMuteToggleIo;
    if (0 == strcmp(action, "audio-togglemutescc"))       return (ButtonEvent)actionMuteToggleScc;
    if (0 == strcmp(action, "audio-togglemutekeyboard"))  return (ButtonEvent)actionMuteToggleKeyboard;
    if (0 == strcmp(action, "audio-togglemutemsxmusic"))  return (ButtonEvent)actionMuteToggleMsxMusic;
    if (0 == strcmp(action, "audio-togglemutemsxaudio"))  return (ButtonEvent)actionMuteToggleMsxAudio;
    if (0 == strcmp(action, "audio-togglemutemoonsound")) return (ButtonEvent)actionMuteToggleMoonsound;
    if (0 == strcmp(action, "audio-togglemutesfg"))       return (ButtonEvent)actionMuteToggleYamahaSfg;
    if (0 == strcmp(action, "audio-togglemutemidi"))      return (ButtonEvent)actionMuteToggleMidi;
    
    if (0 == strcmp(action, "printer-forceformfeed"))   return (ButtonEvent)actionPrinterForceFormFeed;

    if (0 == strcmp(action, "video-gamma"))        return (ButtonEvent)actionVideoSetGamma;
    if (0 == strcmp(action, "video-brightness"))   return (ButtonEvent)actionVideoSetBrightness;
    if (0 == strcmp(action, "video-contrast"))     return (ButtonEvent)actionVideoSetContrast;
    if (0 == strcmp(action, "video-saturation"))   return (ButtonEvent)actionVideoSetSaturation;
    if (0 == strcmp(action, "video-scanlines"))    return (ButtonEvent)actionVideoSetScanlines;
    if (0 == strcmp(action, "video-rfmodulation")) return (ButtonEvent)actionVideoSetRfModulation;
    if (0 == strcmp(action, "video-colormode"))    return (ButtonEvent)actionVideoSetColorMode;
    if (0 == strcmp(action, "video-filter"))       return (ButtonEvent)actionVideoSetFilter;
    if (0 == strcmp(action, "video-enable-mon1"))  return (ButtonEvent)actionVideoEnableMon1;
    if (0 == strcmp(action, "video-enable-mon2"))  return (ButtonEvent)actionVideoEnableMon2;
    if (0 == strcmp(action, "video-enable-mon3"))  return (ButtonEvent)actionVideoEnableMon3;
    
    if (0 == strcmp(action, "level-master"))      return (ButtonEvent)actionVolumeSetMaster;
    if (0 == strcmp(action, "level-psg"))         return (ButtonEvent)actionVolumeSetPsg;
    if (0 == strcmp(action, "level-pcm"))         return (ButtonEvent)actionVolumeSetPcm;
    if (0 == strcmp(action, "level-io"))          return (ButtonEvent)actionVolumeSetIo;
    if (0 == strcmp(action, "level-io"))          return (ButtonEvent)actionVolumeSetIo;
    if (0 == strcmp(action, "level-scc"))         return (ButtonEvent)actionVolumeSetScc;
    if (0 == strcmp(action, "level-keyboard"))    return (ButtonEvent)actionVolumeSetKeyboard;
    if (0 == strcmp(action, "level-msxmusic"))    return (ButtonEvent)actionVolumeSetMsxMusic;
    if (0 == strcmp(action, "level-msxaudio"))    return (ButtonEvent)actionVolumeSetMsxAudio;
    if (0 == strcmp(action, "level-moonsound"))   return (ButtonEvent)actionVolumeSetMoonsound;
    if (0 == strcmp(action, "level-sfg"))         return (ButtonEvent)actionVolumeSetYamahaSfg;
    if (0 == strcmp(action, "level-midi"))        return (ButtonEvent)actionVolumeSetMidi;
    if (0 == strcmp(action, "pan-psg"))           return (ButtonEvent)actionPanSetPsg;
    if (0 == strcmp(action, "pan-pcm"))           return (ButtonEvent)actionPanSetPcm;
    if (0 == strcmp(action, "pan-io"))            return (ButtonEvent)actionPanSetIo;
    if (0 == strcmp(action, "pan-scc"))           return (ButtonEvent)actionPanSetScc;
    if (0 == strcmp(action, "pan-keyboard"))      return (ButtonEvent)actionPanSetKeyboard;
    if (0 == strcmp(action, "pan-msxmusic"))      return (ButtonEvent)actionPanSetMsxMusic;
    if (0 == strcmp(action, "pan-msxaudio"))      return (ButtonEvent)actionPanSetMsxAudio;
    if (0 == strcmp(action, "pan-moonsound"))     return (ButtonEvent)actionPanSetMoonsound;
    if (0 == strcmp(action, "pan-sfg"))           return (ButtonEvent)actionPanSetYamahaSfg;
    if (0 == strcmp(action, "pan-midi"))          return (ButtonEvent)actionPanSetMidi;

    if (0 == strcmp(action, "slider-rensha"))       return (ButtonEvent)actionRenshaSetLevel;

    if (0 == strcmp(action, "slider-emuspeed"))     return (ButtonEvent)actionEmuSpeedSet;
    
    if (0 == strcmp(action, "cart1-setautoreset"))   return (ButtonEvent)actionSetCartAutoReset;
    if (0 == strcmp(action, "diska-setautoreset"))   return (ButtonEvent)actionSetDiskAutoResetA;
    if (0 == strcmp(action, "cas-setautorewind"))    return (ButtonEvent)actionSetCasAutoRewind;
    if (0 == strcmp(action, "sprite-setenable"))     return (ButtonEvent)actionSetSpriteEnable;
    if (0 == strcmp(action, "switch-setfdctiming"))  return (ButtonEvent)actionSetFdcTiming;
    if (0 == strcmp(action, "switch-setnospritelimits"))  return (ButtonEvent)actionSetNoSpriteLimits;
    if (0 == strcmp(action, "switch-togglemsxkeyboardquirk")) return (ButtonEvent)actionToggleMsxKeyboardQuirk;
    if (0 == strcmp(action, "switch-setmsxaudio"))   return (ButtonEvent)actionSetMsxAudioSwitch;
    if (0 == strcmp(action, "switch-setfront"))      return (ButtonEvent)actionSetFrontSwitch;
    if (0 == strcmp(action, "switch-setpause"))      return (ButtonEvent)actionSetPauseSwitch;
    if (0 == strcmp(action, "audio-setrecord"))      return (ButtonEvent)actionSetWaveCapture;
    if (0 == strcmp(action, "mouse-setcapture"))     return (ButtonEvent)actionSetMouseCapture;
    if (0 == strcmp(action, "window-setfullscreen")) return (ButtonEvent)actionSetFullscreen;
    if (0 == strcmp(action, "cas-setreadonly"))      return (ButtonEvent)actionSetCasReadonly;
    if (0 == strcmp(action, "audio-setmute"))        return (ButtonEvent)actionSetVolumeMute;
    if (0 == strcmp(action, "audio-setstereo"))      return (ButtonEvent)actionSetVolumeStereo;

    if (0 == strcmp(action, "theme-maximize"))       return (ButtonEvent)actionMaximizeWindow;
    if (0 == strcmp(action, "theme-minimize"))       return (ButtonEvent)actionMinimizeWindow;
    if (0 == strcmp(action, "theme-close"))          return (ButtonEvent)actionCloseWindow;

    if (0 == strcmp(action, "theme-setpage"))        buttonEvent = (ButtonEvent)themeSetPageFromHash;
    if (0 == strcmp(action, "theme-openwindow"))     buttonEvent = (ButtonEvent)themeCollectionOpenWindow;


    if (buttonEvent != NULL) {
        const char* argaStr = el->Attribute(arg1Tag);
        if (argaStr != NULL) {
            const char* argaStr = el->Attribute(arg1Tag);
            *arg1 = 0;
            *arg2 = themeGetNameHash(argaStr);

            if (buttonEvent == (ButtonEvent)themeSetPageFromHash) {
                *arg1 = (LONG_PTR)theme;
            }
            if (buttonEvent == (ButtonEvent)themeCollectionOpenWindow) {
                *arg1 = (LONG_PTR)themeCollection;
            }
        }
    }

	return buttonEvent;
}

static int getKeyCode(TiXmlElement* el, char* triggerName)
{
    const char* keycode = el->Attribute(triggerName);
    if (keycode == NULL) {
        return -1;
    }
    
    return inputEventStringToCode(keycode);
}


static int getIndexedTrigger(TiXmlElement* el, char* triggerName, int idx)
{
    const char* trigger = el->Attribute(triggerName);
    if (trigger == NULL) {
        return THEME_TRIGGER_NONE;
    }

    int inverted = (strlen(trigger) > 4 && 0 == memcmp(trigger, "not ", 4));

    const char* s = inverted ? trigger + 4 : trigger;
    int         t = inverted ? THEME_TRIGGER_NOT : 0;
    
    if (0 == strcmp(s, "key-pressed"))    return t | (THEME_TRIGGER_FIRST_KEY_PRESSED + idx);
    if (0 == strcmp(s, "key-configured")) return t | (THEME_TRIGGER_FIRST_KEY_CONFIG + idx);

    return -1;
}

static int getTrigger(TiXmlElement* el, char* triggerName)
{
    const char* trigger = el->Attribute(triggerName);
    if (trigger == NULL) {
        return THEME_TRIGGER_NONE;
    }

    int inverted = (strlen(trigger) > 4 && 0 == memcmp(trigger, "not ", 4));

    const char* s = inverted ? trigger + 4 : trigger;
    int         t = inverted ? THEME_TRIGGER_NOT : 0;
    
    if (0 == strcmp(s, "emu-stopped"))              return t | THEME_TRIGGER_IMG_STOPPED;
    if (0 == strcmp(s, "emu-paused"))               return t | THEME_TRIGGER_IMG_PAUSED;
    if (0 == strcmp(s, "emu-running"))              return t | THEME_TRIGGER_IMG_RUNNING;
    
    if (0 == strcmp(s, "led-diska"))                return t | THEME_TRIGGER_IMG_DISKA;
    if (0 == strcmp(s, "led-diskb"))                return t | THEME_TRIGGER_IMG_DISKB;
    if (0 == strcmp(s, "led-cassette"))             return t | THEME_TRIGGER_IMG_CAS;
    if (0 == strcmp(s, "led-hd"))                   return t | THEME_TRIGGER_IMG_HD;
    if (0 == strcmp(s, "led-audioswitch"))          return t | THEME_TRIGGER_IMG_AS;
    if (0 == strcmp(s, "led-frontswitch"))          return t | THEME_TRIGGER_IMG_FS;
    if (0 == strcmp(s, "led-pauseswitch"))          return t | THEME_TRIGGER_IMG_PS;
    if (0 == strcmp(s, "led-caps"))                 return t | THEME_TRIGGER_IMG_CAPS;
    if (0 == strcmp(s, "led-kana"))                 return t | THEME_TRIGGER_IMG_KANA;
    if (0 == strcmp(s, "led-turbor"))               return t | THEME_TRIGGER_IMG_TURBOR;
    if (0 == strcmp(s, "led-pause"))                return t | THEME_TRIGGER_IMG_PAUSE;
    
    if (0 == strcmp(s, "enable-fdctiming"))         return t | THEME_TRIGGER_IMG_FDCTIMING;
    if (0 == strcmp(s, "enable-nospritelimits"))    return t | THEME_TRIGGER_IMG_NOSPRITELIMITS;

    if (0 == strcmp(s, "enable-keyboard"))          return t | THEME_TRIGGER_IMG_KBD;
    if (0 == strcmp(s, "enable-moonsound"))         return t | THEME_TRIGGER_IMG_MOON;
    if (0 == strcmp(s, "enable-sfg"))               return t | THEME_TRIGGER_IMG_SFG;
    if (0 == strcmp(s, "enable-msxaudio"))          return t | THEME_TRIGGER_IMG_MSXA;
    if (0 == strcmp(s, "enable-msxmusic"))          return t | THEME_TRIGGER_IMG_MSXM;
    if (0 == strcmp(s, "enable-psg"))               return t | THEME_TRIGGER_IMG_PSG;
    if (0 == strcmp(s, "enable-scc"))               return t | THEME_TRIGGER_IMG_SCC;
    if (0 == strcmp(s, "enable-pcm"))               return t | THEME_TRIGGER_IMG_PCM;
    if (0 == strcmp(s, "enable-io"))                return t | THEME_TRIGGER_IMG_IO;
    if (0 == strcmp(s, "enable-midi"))              return t | THEME_TRIGGER_IMG_MIDI;
    if (0 == strcmp(s, "enable-master"))            return t | THEME_TRIGGER_IMG_MASTER;
    if (0 == strcmp(s, "enable-stereo"))            return t | THEME_TRIGGER_IMG_STEREO;
    
    if (0 == strcmp(s, "volume-keyboard-left"))     return t | THEME_TRIGGER_IMG_L_KBD;
    if (0 == strcmp(s, "volume-keyboard-right"))    return t | THEME_TRIGGER_IMG_R_KBD;
    if (0 == strcmp(s, "volume-moonsound-left"))    return t | THEME_TRIGGER_IMG_L_MOON;
    if (0 == strcmp(s, "volume-moonsound-right"))   return t | THEME_TRIGGER_IMG_R_MOON;
    if (0 == strcmp(s, "volume-sfg-left"))          return t | THEME_TRIGGER_IMG_L_SFG;
    if (0 == strcmp(s, "volume-sfg-right"))         return t | THEME_TRIGGER_IMG_R_SFG;
    if (0 == strcmp(s, "volume-msxaudio-left"))     return t | THEME_TRIGGER_IMG_L_MSXA;
    if (0 == strcmp(s, "volume-msxaudio-right"))    return t | THEME_TRIGGER_IMG_R_MSXA;
    if (0 == strcmp(s, "volume-msxmusic-left"))     return t | THEME_TRIGGER_IMG_L_MSXM;
    if (0 == strcmp(s, "volume-msxmusic-right"))    return t | THEME_TRIGGER_IMG_R_MSXM;
    if (0 == strcmp(s, "volume-psg-left"))          return t | THEME_TRIGGER_IMG_L_PSG;
    if (0 == strcmp(s, "volume-psg-right"))         return t | THEME_TRIGGER_IMG_R_PSG;
    if (0 == strcmp(s, "volume-scc-left"))          return t | THEME_TRIGGER_IMG_L_SCC;
    if (0 == strcmp(s, "volume-scc-right"))         return t | THEME_TRIGGER_IMG_R_SCC;
    if (0 == strcmp(s, "volume-pcm-left"))          return t | THEME_TRIGGER_IMG_L_PCM;
    if (0 == strcmp(s, "volume-pcm-right"))         return t | THEME_TRIGGER_IMG_R_PCM;
    if (0 == strcmp(s, "volume-io-left"))           return t | THEME_TRIGGER_IMG_L_IO;
    if (0 == strcmp(s, "volume-io-right"))          return t | THEME_TRIGGER_IMG_R_IO;
    if (0 == strcmp(s, "volume-midi-left"))         return t | THEME_TRIGGER_IMG_L_MIDI;
    if (0 == strcmp(s, "volume-midi-right"))        return t | THEME_TRIGGER_IMG_R_MIDI;
    if (0 == strcmp(s, "volume-master-left"))       return t | THEME_TRIGGER_IMG_L_MASTER;
    if (0 == strcmp(s, "volume-master-right"))      return t | THEME_TRIGGER_IMG_R_MASTER;

    if (0 == strcmp(s, "slider-rensha"))            return t | THEME_TRIGGER_RENSHA;
    if (0 == strcmp(s, "led-rensha"))               return t | THEME_TRIGGER_RENSHALED;
    
    if (0 == strcmp(s, "slider-emuspeed"))          return t | THEME_TRIGGER_EMUSPEED;
    \
    if (0 == strcmp(s, "video-gamma"))          return t | THEME_TRIGGER_VIDEO_GAMMA;
    if (0 == strcmp(s, "video-brightness"))     return t | THEME_TRIGGER_VIDEO_BRIGHTNESS;
    if (0 == strcmp(s, "video-contrast"))       return t | THEME_TRIGGER_VIDEO_CONTRAST;
    if (0 == strcmp(s, "video-saturation"))     return t | THEME_TRIGGER_VIDEO_SATURATION;
    if (0 == strcmp(s, "video-scanlines"))      return t | THEME_TRIGGER_VIDEO_SCANLINES;
    if (0 == strcmp(s, "video-rfmodulation"))   return t | THEME_TRIGGER_VIDEO_RFMODULATION;
    if (0 == strcmp(s, "video-colormode"))      return t | THEME_TRIGGER_VIDEO_COLORMODE;
    if (0 == strcmp(s, "video-filter"))         return t | THEME_TRIGGER_VIDEO_FILTER;
    if (0 == strcmp(s, "video-present-mon1"))    return t | THEME_TRIGGER_VIDEO_PRESENT_MON1;
    if (0 == strcmp(s, "video-present-mon2"))    return t | THEME_TRIGGER_VIDEO_PRESENT_MON2;
    if (0 == strcmp(s, "video-present-mon3"))    return t | THEME_TRIGGER_VIDEO_PRESENT_MON3;
    if (0 == strcmp(s, "video-enable-mon1"))    return t | THEME_TRIGGER_VIDEO_ENABLE_MON1;
    if (0 == strcmp(s, "video-enable-mon2"))    return t | THEME_TRIGGER_VIDEO_ENABLE_MON2;
    if (0 == strcmp(s, "video-enable-mon3"))    return t | THEME_TRIGGER_VIDEO_ENABLE_MON3;

    if (0 == strcmp(s, "video-enable-mon-amber")) return t | THEME_TRIGGER_VIDEO_ENABLE_MON_AMBER;
    if (0 == strcmp(s, "video-enable-mon-green")) return t | THEME_TRIGGER_VIDEO_ENABLE_MON_GREEN;
    if (0 == strcmp(s, "video-enable-mon-white")) return t | THEME_TRIGGER_VIDEO_ENABLE_MON_WHITE;
    if (0 == strcmp(s, "video-enable-mon-color")) return t | THEME_TRIGGER_VIDEO_ENABLE_MON_COLOR;
    
    if (0 == strcmp(s, "video-enable-mon-hq2x"))        return t | THEME_TRIGGER_VIDEO_ENABLE_MON_HQ2X;
    if (0 == strcmp(s, "video-enable-mon-scale2x"))     return t | THEME_TRIGGER_VIDEO_ENABLE_MON_SCALE2X;
    if (0 == strcmp(s, "video-enable-mon-compnoise"))   return t | THEME_TRIGGER_VIDEO_ENABLE_MON_COMPNOISE;
    if (0 == strcmp(s, "video-enable-mon-comp"))        return t | THEME_TRIGGER_VIDEO_ENABLE_MON_COMP;
    if (0 == strcmp(s, "video-enable-mon-ycnoise"))     return t | THEME_TRIGGER_VIDEO_ENABLE_MON_YCNOISE;
    if (0 == strcmp(s, "video-enable-mon-yc"))          return t | THEME_TRIGGER_VIDEO_ENABLE_MON_YC;
    if (0 == strcmp(s, "video-enable-mon-monitor"))     return t | THEME_TRIGGER_VIDEO_ENABLE_MON_MONITOR;
    if (0 == strcmp(s, "video-enable-mon-none"))        return t | THEME_TRIGGER_VIDEO_ENABLE_MON_NONE;

    if (0 == strcmp(s, "level-master"))            return t | THEME_TRIGGER_LEVEL_MASTER;
    if (0 == strcmp(s, "level-psg"))               return t | THEME_TRIGGER_LEVEL_PSG;
    if (0 == strcmp(s, "level-pcm"))               return t | THEME_TRIGGER_LEVEL_PCM;
    if (0 == strcmp(s, "level-io"))                return t | THEME_TRIGGER_LEVEL_IO;
    if (0 == strcmp(s, "level-scc"))               return t | THEME_TRIGGER_LEVEL_SCC;
    if (0 == strcmp(s, "level-keyboard"))          return t | THEME_TRIGGER_LEVEL_KEYBOARD;
    if (0 == strcmp(s, "level-msxmusic"))          return t | THEME_TRIGGER_LEVEL_MSXMUSIC;
    if (0 == strcmp(s, "level-msxaudio"))          return t | THEME_TRIGGER_LEVEL_MSXAUDIO;
    if (0 == strcmp(s, "level-moonsound"))         return t | THEME_TRIGGER_LEVEL_MOONSOUND;
    if (0 == strcmp(s, "level-sfg"))               return t | THEME_TRIGGER_LEVEL_SFG;
    if (0 == strcmp(s, "level-midi"))              return t | THEME_TRIGGER_LEVEL_MIDI;
    if (0 == strcmp(s, "pan-psg"))                 return t | THEME_TRIGGER_PAN_PSG;
    if (0 == strcmp(s, "pan-pcm"))                 return t | THEME_TRIGGER_PAN_PCM;
    if (0 == strcmp(s, "pan-io"))                  return t | THEME_TRIGGER_PAN_IO;
    if (0 == strcmp(s, "pan-scc"))                 return t | THEME_TRIGGER_PAN_SCC;
    if (0 == strcmp(s, "pan-keyboard"))            return t | THEME_TRIGGER_PAN_KEYBOARD;
    if (0 == strcmp(s, "pan-midi"))                return t | THEME_TRIGGER_PAN_MIDI;
    if (0 == strcmp(s, "pan-msxmusic"))            return t | THEME_TRIGGER_PAN_MSXMUSIC;
    if (0 == strcmp(s, "pan-msxaudio"))            return t | THEME_TRIGGER_PAN_MSXAUDIO;
    if (0 == strcmp(s, "pan-moonsound"))           return t | THEME_TRIGGER_PAN_MOONSOUND;
    if (0 == strcmp(s, "pan-sfg"))                 return t | THEME_TRIGGER_PAN_SFG;

    if (0 == strcmp(s, "using-moonsound"))          return t | THEME_TRIGGER_IMG_M_MOON;
    if (0 == strcmp(s, "using-sfg"))                return t | THEME_TRIGGER_IMG_M_SFG;
    if (0 == strcmp(s, "using-msxmusic"))           return t | THEME_TRIGGER_IMG_M_MSXM;
    if (0 == strcmp(s, "using-msxaudio"))           return t | THEME_TRIGGER_IMG_M_MSXA;
    if (0 == strcmp(s, "using-scc"))                return t | THEME_TRIGGER_IMG_M_SCC;
    if (0 == strcmp(s, "using-rom"))                return t | THEME_TRIGGER_IMG_M_ROM;
    if (0 == strcmp(s, "using-megaram"))            return t | THEME_TRIGGER_IMG_M_MEGARAM;
    if (0 == strcmp(s, "using-megarom"))            return t | THEME_TRIGGER_IMG_M_MEGAROM;
    if (0 == strcmp(s, "using-fmpac"))              return t | THEME_TRIGGER_IMG_M_FMPAC;

    if (0 == strcmp(s, "status-record"))            return t | THEME_TRIGGER_IMG_REC;
    if (0 == strcmp(s, "status-diskreset"))         return t | THEME_TRIGGER_IMG_DISK_RI;
    if (0 == strcmp(s, "status-cartreset"))         return t | THEME_TRIGGER_IMG_CART_RI;
    if (0 == strcmp(s, "status-casreadonly"))       return t | THEME_TRIGGER_IMG_CAS_RO;
    if (0 == strcmp(s, "status-scanlines"))         return t | THEME_TRIGGER_VIDEO_SCANLINES_EN;
    if (0 == strcmp(s, "status-deinterlace"))       return t | THEME_TRIGGER_VIDEO_DEINTERLACE_EN;
    if (0 == strcmp(s, "status-blendframes"))       return t | THEME_TRIGGER_VIDEO_BLENDFRAMES_EN;
    if (0 == strcmp(s, "status-rfmodulation"))      return t | THEME_TRIGGER_VIDEO_RFMODULATION_EN;
    if (0 == strcmp(s, "status-hstretch"))          return t | THEME_TRIGGER_VIDEO_HSTRETCH_EN;
    if (0 == strcmp(s, "status-vstretch"))          return t | THEME_TRIGGER_VIDEO_VSTRETCH_EN;
    if (0 == strcmp(s, "video-captureoff"))         return t | THEME_TRIGGER_VIDEO_CAPTURE_NONE;
    if (0 == strcmp(s, "video-captureplay"))        return t | THEME_TRIGGER_VIDEO_CAPTURE_PLAY;
    if (0 == strcmp(s, "video-capturerec"))         return t | THEME_TRIGGER_VIDEO_CAPTURE_REC;

    if (0 == strcmp(s, "keyboard-enable"))       return t | THEME_TRIGGER_KEYBOARD_ENABLE;
    if (0 == strcmp(s, "port1-enable"))          return t | THEME_TRIGGER_JOY1_ENABLE;
    if (0 == strcmp(s, "port1-none"))            return t | THEME_TRIGGER_JOY1_NONE;
    if (0 == strcmp(s, "port1-joystick"))        return t | THEME_TRIGGER_JOY1_JOYSTICK;
    if (0 == strcmp(s, "port1-mouse"))           return t | THEME_TRIGGER_JOY1_MOUSE;
    if (0 == strcmp(s, "port1-arkanoidpad"))     return t | THEME_TRIGGER_JOY1_ARK_PAD;
    if (0 == strcmp(s, "port1-tetris2dongle"))   return t | THEME_TRIGGER_JOY1_TETRIS;
    if (0 == strcmp(s, "port1-magickeydongle"))  return t | THEME_TRIGGER_JOY1_MAGICKEY;
    if (0 == strcmp(s, "port1-gunstick"))        return t | THEME_TRIGGER_JOY1_GUNSTICK;
    if (0 == strcmp(s, "port1-asciilaser"))      return t | THEME_TRIGGER_JOY1_ASCIILASER;
    if (0 == strcmp(s, "port1-colecojoystick"))  return t | THEME_TRIGGER_JOY1_COLECOJOY;
    if (0 == strcmp(s, "port1-superaction"))     return t | THEME_TRIGGER_JOY1_SUPERACTION;
    if (0 == strcmp(s, "port1-steeringwheel"))   return t | THEME_TRIGGER_JOY1_STEERINGWHEEL;
    if (0 == strcmp(s, "port2-enable"))          return t | THEME_TRIGGER_JOY2_ENABLE;
    if (0 == strcmp(s, "port2-none"))            return t | THEME_TRIGGER_JOY2_NONE;
    if (0 == strcmp(s, "port2-joystick"))        return t | THEME_TRIGGER_JOY2_JOYSTICK;
    if (0 == strcmp(s, "port2-mouse"))           return t | THEME_TRIGGER_JOY2_MOUSE;
    if (0 == strcmp(s, "port2-arkanoidpad"))     return t | THEME_TRIGGER_JOY2_ARK_PAD;
    if (0 == strcmp(s, "port2-tetris2dongle"))   return t | THEME_TRIGGER_JOY2_TETRIS;
    if (0 == strcmp(s, "port2-magickeydongle"))  return t | THEME_TRIGGER_JOY2_MAGICKEY;
    if (0 == strcmp(s, "port2-gunstick"))        return t | THEME_TRIGGER_JOY2_GUNSTICK;
    if (0 == strcmp(s, "port2-asciilaser"))      return t | THEME_TRIGGER_JOY2_ASCIILASER;
    if (0 == strcmp(s, "port2-colecojoystick"))  return t | THEME_TRIGGER_JOY2_COLECOJOY;
    if (0 == strcmp(s, "port2-superaction"))     return t | THEME_TRIGGER_JOY2_SUPERACTION;
    if (0 == strcmp(s, "port2-steeringwheel"))   return t | THEME_TRIGGER_JOY2_STEERINGWHEEL;

    if (0 == strcmp(s, "text-scanlinespct"))        return t | THEME_TRIGGER_TEXT_SCANLINESPCT;
    if (0 == strcmp(s, "text-videogamma"))          return t | THEME_TRIGGER_TEXT_VIDEOGAMMA;
    if (0 == strcmp(s, "text-videobrightness"))     return t | THEME_TRIGGER_TEXT_VIDEOBRIGHTNESS;
    if (0 == strcmp(s, "text-videocontrast"))       return t | THEME_TRIGGER_TEXT_VIDEOCONTRAST;
    if (0 == strcmp(s, "text-videosaturation"))     return t | THEME_TRIGGER_TEXT_VIDEOSATURATION;
    if (0 == strcmp(s, "text-videomonname1"))       return t | THEME_TRIGGER_TEXT_VIDEOMONNAME1;
    if (0 == strcmp(s, "text-videomonname2"))       return t | THEME_TRIGGER_TEXT_VIDEOMONNAME2;
    if (0 == strcmp(s, "text-videomonname3"))       return t | THEME_TRIGGER_TEXT_VIDEOMONNAME3;
    if (0 == strcmp(s, "text-emufrequency"))        return t | THEME_TRIGGER_TEXT_FREQ;
    if (0 == strcmp(s, "text-cpuusage"))            return t | THEME_TRIGGER_TEXT_CPU;
    
    if (0 == strcmp(s, "text-perftimer0"))          return t | THEME_TRIGGER_TEXT_PERFTIMER0;
    if (0 == strcmp(s, "text-perftimer1"))          return t | THEME_TRIGGER_TEXT_PERFTIMER1;
    if (0 == strcmp(s, "text-perftimer2"))          return t | THEME_TRIGGER_TEXT_PERFTIMER2;
    if (0 == strcmp(s, "text-perftimer3"))          return t | THEME_TRIGGER_TEXT_PERFTIMER3;
    if (0 == strcmp(s, "text-perftimer4"))          return t | THEME_TRIGGER_TEXT_PERFTIMER4;

    if (0 == strcmp(s, "text-framespersecond"))     return t | THEME_TRIGGER_TEXT_FPS;
    if (0 == strcmp(s, "text-ramsize"))             return t | THEME_TRIGGER_TEXT_RAM;
    if (0 == strcmp(s, "text-vramsize"))            return t | THEME_TRIGGER_TEXT_VRAM;
    if (0 == strcmp(s, "text-screentype"))          return t | THEME_TRIGGER_TEXT_SCREEN;
    if (0 == strcmp(s, "text-screentypeshort"))     return t | THEME_TRIGGER_TEXT_SCREENSHORT;
    if (0 == strcmp(s, "text-rommapper1"))          return t | THEME_TRIGGER_TEXT_ROMMAPPER1;
    if (0 == strcmp(s, "text-rommapper2"))          return t | THEME_TRIGGER_TEXT_ROMMAPPER2;
    if (0 == strcmp(s, "text-rommapper1short"))     return t | THEME_TRIGGER_TEXT_ROMMAPPER1SHORT;
    if (0 == strcmp(s, "text-rommapper2short"))     return t | THEME_TRIGGER_TEXT_ROMMAPPER2SHORT;
    if (0 == strcmp(s, "text-machinename"))         return t | THEME_TRIGGER_TEXT_MACHINENAME;
    if (0 == strcmp(s, "text-runningname"))         return t | THEME_TRIGGER_TEXT_RUNNAME;
    if (0 == strcmp(s, "text-version"))             return t | THEME_TRIGGER_TEXT_VERSION;
    if (0 == strcmp(s, "text-buildnumber"))         return t | THEME_TRIGGER_TEXT_BUILDNUMBER;
    if (0 == strcmp(s, "text-buildandversion"))     return t | THEME_TRIGGER_TEXT_BUILDANDVER;
    if (0 == strcmp(s, "text-selectedkey"))         return t | THEME_TRIGGER_TEXT_SELECTEDKEY;
    if (0 == strcmp(s, "text-mappedkey"))           return t | THEME_TRIGGER_TEXT_MAPPEDKEY;
    if (0 == strcmp(s, "text-joyport1"))            return t | THEME_TRIGGER_TEXT_JOYPORT1;
    if (0 == strcmp(s, "text-joyport2"))            return t | THEME_TRIGGER_TEXT_JOYPORT2;
    
    if (0 == strcmp(s, "lang-kbd-selkey"))          return t | THEME_TRIGGER_LANG_KBD_SELKEY;
    if (0 == strcmp(s, "lang-kbd-mappedto"))        return t | THEME_TRIGGER_LANG_KBD_MAPPEDTO;
    if (0 == strcmp(s, "lang-kbd-mapscheme"))       return t | THEME_TRIGGER_LANG_KBD_MAPSCHEME;

    return -1;
}

static ArchBitmap* loadBitmap(TiXmlElement* el, int* x, int* y, int* columns)
{
    *x = 0;
    *y = 0;
    *columns = 999;

    el->QueryIntAttribute("srcColumns", columns);
    el->QueryIntAttribute("x", x);
    el->QueryIntAttribute("y", y);

    const char* src = el->Attribute("src");
    if (src == NULL) {
        return NULL;
    }
    
    /* Per-frame scaling deferred to applyThemeScale (frame count is
       caller-specific; naive round on raw bitmap smears sprite frames). */
    return archBitmapCreateFromFile(fullPath(src));
}

/* Pre-stretch to g_themeScale.  Sprite frames stay NN; single-frame uses
   HALFTONE at non-integer zoom.  Returns input on failure. */
static ArchBitmap* applyThemeScale(ArchBitmap* bm, int frameCount)
{
    if (bm == NULL || g_themeScale == 1.0) {
        return bm;
    }
    int origW = archBitmapGetWidth(bm);
    int origH = archBitmapGetHeight(bm);
    int dstW;
    bool useSprite = (frameCount > 1 && origW % frameCount == 0);
    if (useSprite) {
        int frameW    = origW / frameCount;
        int dstFrameW = themeScaledCoord(frameW);
        dstW          = dstFrameW * frameCount;
    } else {
        dstW = themeScaledCoord(origW);
    }
    int dstH = themeScaledCoord(origH);

    /* x3/x5/x7 are half-integer scales (N/2.0 with odd N). */
    bool integerZoom = (g_themeScale == (double)(int)g_themeScale);

    ArchBitmap* scaled;
    if (!useSprite && !integerZoom) {
        scaled = archBitmapCreateScaledCopySmooth(bm, dstW, dstH);
    } else {
        scaled = archBitmapCreateScaledCopy(bm, dstW, dstH);
    }
    if (scaled == NULL) {
        return bm;
    }
    archBitmapDestroy(bm);
    return scaled;
}

/* Explicit srcColumns wins over caller's known count (999 = unset). */
static int spriteFrameCount(int columns, int count)
{
    if (columns > 1 && columns < 999) return columns;
    if (count > 1) return count;
    return 1;
}

/* Split a background spanning the menu band into top/menu/below sub-
   bitmaps; the menu portion is stretched to the runtime strip height so
   no black strip leaks beside the Win32 menu.  Returns 1 on split
   (consumes *bm), 0 otherwise. */
static int splitImageAtMenuBand(ArchBitmap** bm, ArchBitmap* designBm,
                                int absDesignY, int designH,
                                ArchBitmap** outTopBm,   int* outTopDstY,
                                ArchBitmap** outMenuBm,  int* outMenuDstY,
                                ArchBitmap** outBelowBm, int* outBelowDstY)
{
    *outTopBm   = NULL; *outTopDstY   = 0;
    *outMenuBm  = NULL; *outMenuDstY  = 0;
    *outBelowBm = NULL; *outBelowDstY = 0;

    if (g_designMenuBottom < 0)                           return 0;
    if (absDesignY + designH <= g_designMenuY)            return 0; /* entirely above */
    if (absDesignY >= g_designMenuBottom)                 return 0; /* entirely below */

    int topRowsDesign = g_designMenuY - absDesignY;
    int botRowsDesign = g_designMenuBottom - absDesignY;
    if (topRowsDesign < 0)        topRowsDesign = 0;
    if (botRowsDesign > designH)  botRowsDesign = designH;
    int menuRowsDesign = botRowsDesign - topRowsDesign;

    ArchBitmap* src     = *bm;
    int scaledW         = archBitmapGetWidth(src);
    int scaledH         = archBitmapGetHeight(src);
    int topRowsScaled   = themeScaledCoord(topRowsDesign);
    int botRowsScaled   = themeScaledCoord(botRowsDesign);
    if (botRowsScaled > scaledH)  botRowsScaled = scaledH;
    if (topRowsScaled > scaledH)  topRowsScaled = scaledH;

    int menuHRuntime = archMenuStripHeight();
    if (menuHRuntime <= 0) menuHRuntime = DESIGN_MENU_H;

    /* Top portion (above-menu rows). */
    if (topRowsScaled > 0) {
        ArchBitmap* topBm = archBitmapCreate(scaledW, topRowsScaled);
        archBitmapCopy(topBm, 0, 0, src, 0, 0, scaledW, topRowsScaled);
        *outTopBm   = topBm;
        *outTopDstY = themeScaledCoord(absDesignY);  /* unshifted */
    }

    /* Stretch in a single NN step from the design bitmap -- double-interp
       via applyThemeScale smears 1-px dividers at x3/x5/x7. */
    if (botRowsScaled > topRowsScaled && menuRowsDesign > 0) {
        int menuRowsSrcH = botRowsScaled - topRowsScaled;
        int menuDstHRuntime = menuHRuntime + EXTERNAL_THEME_TOP_GAP;
        int menuPortionDstH = (menuRowsDesign * menuDstHRuntime + DESIGN_MENU_H/2) / DESIGN_MENU_H;
        if (menuPortionDstH < 1) menuPortionDstH = 1;

        ArchBitmap* menuBm;
        if (designBm != NULL) {
            int designW = archBitmapGetWidth(designBm);
            int topRowsFromDesign = (g_designMenuY > absDesignY) ? (g_designMenuY - absDesignY) : 0;
            ArchBitmap* menuDesignSrc = archBitmapCreate(designW, menuRowsDesign);
            archBitmapCopy(menuDesignSrc, 0, 0, designBm, 0, topRowsFromDesign,
                           designW, menuRowsDesign);
            menuBm = archBitmapCreateScaledCopy(menuDesignSrc, scaledW, menuPortionDstH);
            archBitmapDestroy(menuDesignSrc);
        } else {
            ArchBitmap* menuSrc = archBitmapCreate(scaledW, menuRowsSrcH);
            archBitmapCopy(menuSrc, 0, 0, src, 0, topRowsScaled, scaledW, menuRowsSrcH);
            if (menuPortionDstH != menuRowsSrcH) {
                menuBm = archBitmapCreateScaledCopy(menuSrc, scaledW, menuPortionDstH);
                archBitmapDestroy(menuSrc);
            } else {
                menuBm = menuSrc;
            }
        }
        *outMenuBm   = menuBm;
        /* Menu portion starts at the band top (or image top if inside). */
        *outMenuDstY = themeScaledCoord(absDesignY + topRowsDesign);
    }

    /* Below portion (below-menu rows). */
    if (botRowsScaled < scaledH) {
        int belowH = scaledH - botRowsScaled;
        ArchBitmap* belowBm = archBitmapCreate(scaledW, belowH);
        archBitmapCopy(belowBm, 0, 0, src, 0, botRowsScaled, scaledW, belowH);
        *outBelowBm = belowBm;
        /* Design start >= g_designMenuBottom, so themeScaledY applies topShift. */
        *outBelowDstY = themeScaledY(absDesignY + botRowsDesign);
    }

    archBitmapDestroy(src);
    *bm = NULL;
    return 1;
}


static void addImage(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                     TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int designH = archBitmapGetHeight(bitmap);
    int absDesignX = x + dx;
    int absDesignY = y + dy;

    /* Clone the design bitmap before applyThemeScale destroys it, so the
       menu-band split can do single-step NN at odd zooms. */
    ArchBitmap* designClone = NULL;
    if (g_designMenuBottom >= 0
        && absDesignY + designH > g_designMenuY
        && absDesignY < g_designMenuBottom) {
        int designW = archBitmapGetWidth(bitmap);
        designClone = archBitmapCreate(designW, designH);
        if (designClone != NULL) {
            archBitmapCopy(designClone, 0, 0, bitmap, 0, 0, designW, designH);
        }
    }

    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, 1));

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    ArchBitmap *topBm, *menuBm, *belowBm;
    int topDstY, menuDstY, belowDstY;
    int dstX = themeScaledCoord(absDesignX);
    if (splitImageAtMenuBand(&bitmap, designClone, absDesignY, designH,
                             &topBm,   &topDstY,
                             &menuBm,  &menuDstY,
                             &belowBm, &belowDstY)) {
        if (topBm != NULL) {
            themePageAddImage(themePage, activeImageCreate(dstX, topDstY, cols, topBm, 1),
                              THEME_TRIGGER_NONE, visible);
        }
        if (menuBm != NULL) {
            themePageAddImage(themePage, activeImageCreate(dstX, menuDstY, cols, menuBm, 1),
                              THEME_TRIGGER_NONE, visible);
        }
        if (belowBm != NULL) {
            themePageAddImage(themePage, activeImageCreate(dstX, belowDstY, cols, belowBm, 1),
                              THEME_TRIGGER_NONE, visible);
        }
    } else {
        int dstY = themeScaledY(absDesignY);
        themePageAddImage(themePage, activeImageCreate(dstX, dstY, cols, bitmap, 1),
                          THEME_TRIGGER_NONE, visible);
    }
    if (designClone != NULL) {
        archBitmapDestroy(designClone);
    }
}

static void addGrabImage(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                     TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int activenotify = 0;
    el->QueryIntAttribute("activenotify", &activenotify);
    int count = activenotify ? 2 : 1;
    int designH = archBitmapGetHeight(bitmap);
    int absDesignX = x + dx;
    int absDesignY = y + dy;

    /* Same single-step menu-portion path as addImage. */
    ArchBitmap* designClone = NULL;
    if (g_designMenuBottom >= 0
        && absDesignY + designH > g_designMenuY
        && absDesignY < g_designMenuBottom) {
        int designW = archBitmapGetWidth(bitmap);
        designClone = archBitmapCreate(designW, designH);
        if (designClone != NULL) {
            archBitmapCopy(designClone, 0, 0, bitmap, 0, 0, designW, designH);
        }
    }

    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, count));

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    ArchBitmap *topBm, *menuBm, *belowBm;
    int topDstY, menuDstY, belowDstY;
    int dstX = themeScaledCoord(absDesignX);
    if (splitImageAtMenuBand(&bitmap, designClone, absDesignY, designH,
                             &topBm,   &topDstY,
                             &menuBm,  &menuDstY,
                             &belowBm, &belowDstY)) {
        if (topBm != NULL) {
            themePageAddGrabImage(themePage, activeGrabImageCreate(dstX, topDstY, cols, topBm, count),
                                  THEME_TRIGGER_NONE, visible);
        }
        if (menuBm != NULL) {
            /* runtime menu strip overlays this band; grab would be useless. */
            themePageAddImage(themePage, activeImageCreate(dstX, menuDstY, cols, menuBm, 1),
                              THEME_TRIGGER_NONE, visible);
        }
        if (belowBm != NULL) {
            themePageAddGrabImage(themePage, activeGrabImageCreate(dstX, belowDstY, cols, belowBm, count),
                                  THEME_TRIGGER_NONE, visible);
        }
    } else {
        int dstY = themeScaledY(absDesignY);
        themePageAddGrabImage(themePage, activeGrabImageCreate(dstX, dstY, cols, bitmap, count),
                              THEME_TRIGGER_NONE, visible);
    }
    if (designClone != NULL) {
        archBitmapDestroy(designClone);
    }
}

static void addLed(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                   TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, 2));  /* 2 LED frames (on/off) */
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    ThemeTrigger trigger = (ThemeTrigger)getTrigger(el, "trigger");
    if (trigger == -1) {
        return;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    themePageAddImage(themePage, activeImageCreate(x, y, cols, bitmap, 2), trigger, visible);
}

static void addMeter(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                     TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int max = 1;
    el->QueryIntAttribute("max", &max);
    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, max));
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    ThemeTrigger trigger = (ThemeTrigger)getTrigger(el, "trigger");
    if (trigger == -1) {
        return;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    themePageAddMeter(themePage, activeMeterCreate(x, y, cols, bitmap, max), trigger, visible);
}

static void addSlider(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                      TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int max = 1;
    el->QueryIntAttribute("max", &max);
    /* Align to `max` frames so per-frame width stays integer at non-
       integer zoom (sub-pixel accumulation otherwise smears the dial). */
    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, max));
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    ThemeTrigger trigger = (ThemeTrigger)getTrigger(el, "trigger");
    if (trigger == -1) {
        return;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    LONG_PTR arga, argb;
    SliderEvent action = (SliderEvent)getAction(el, "action", "arga", "argb", &arga, &argb, themeCollection, theme, dx, dy);

    AsDirection direction = AS_BOTH;
    const char* align = el->Attribute("direction");
    if (align != 0 && strcmp(align, "vertical") == 0) {
        direction = AS_VERTICAL;
    }
    if (align != 0 && strcmp(align, "horizontal") == 0) {
        direction = AS_HORIZONTAL;
    }
    
    int sensitivity = 1;
    el->QueryIntAttribute("sensitivity", &sensitivity);

    const char* mode = el->Attribute("mode");
    if (mode != 0 && strcmp(mode, "inverted") == 0) {
        sensitivity *= -1;
    }

    /* Sensitivity is in design pixels; activeSliderMouseMove sees the raw
       mouse delta in rendered pixels.  Scale to keep drag feel zoom-stable. */
    {
        int sign = sensitivity < 0 ? -1 : 1;
        int absScaled = themeScaledCoord(abs(sensitivity));
        if (absScaled < 1) absScaled = 1;
        sensitivity = sign * absScaled;
    }

    LONG_PTR relArga, relArgb;
    ButtonEvent release = getAction(el, "release", "relarga", "relargb", &relArga, &relArgb, themeCollection, theme, dx, dy);


    themePageAddSlider(themePage, activeSliderCreate(x, y, cols, bitmap, action, max, direction, sensitivity, release, relArga, relArgb), trigger, visible);
}

static void addButton(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                      TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int activeNotify = 0;
    el->QueryIntAttribute("activenotify", &activeNotify);
    activeNotify = activeNotify ? 1 : 0;
    /* 4 frames (normal/hover/pressed/disabled), x2 with activenotify. */
    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, activeNotify ? 8 : 4));
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    ThemeTrigger trigger = (ThemeTrigger)getTrigger(el, "trigger");
    if (trigger == -1) {
        return;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    LONG_PTR arga, argb;
    ButtonEvent action = getAction(el, "action", "arga", "argb", &arga, &argb, themeCollection, theme, dx, dy);

    themePageAddButton(themePage, activeButtonCreate(x, y, cols, activeNotify, bitmap, action, arga, argb), trigger, visible, THEME_TRIGGER_NONE);
}

static void addToggleButton(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                            TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int tbActiveNotify = 0;
    el->QueryIntAttribute("activenotify", &tbActiveNotify);
    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, tbActiveNotify ? 8 : 4));
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    ThemeTrigger trigger = (ThemeTrigger)getTrigger(el, "trigger");
    if (trigger == -1) {
        return;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    LONG_PTR arga, argb;
    ButtonEvent action = getAction(el, "action", "arga", "argb", &arga, &argb, themeCollection, theme, dx, dy);

    int activeNotify = 0;
    el->QueryIntAttribute("activenotify", &activeNotify);
    activeNotify = activeNotify ? 1 : 0;

    themePageAddToggleButton(themePage, activeToggleButtonCreate(x, y, cols, activeNotify, bitmap, action, arga, argb), 
                         trigger, visible, THEME_TRIGGER_NONE);
}

static void addDualButton(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                          TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int activeNotify = 0;
    el->QueryIntAttribute("activenotify", &activeNotify);
    activeNotify = activeNotify ? 1 : 0;
    /* 5 frames (x2 with activenotify); activeDualButtonCreate cols=5. */
    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, activeNotify ? 8 : 5));
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    ThemeTrigger trigger = (ThemeTrigger)getTrigger(el, "trigger");
    if (trigger == -1) {
        return;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    LONG_PTR arg1x, arg1y;
    ButtonEvent action1 = getAction(el, "action1", "arg1x", "arg1y", &arg1x, &arg1y, themeCollection, theme, dx, dy);
    
    LONG_PTR arg2x, arg2y;
    ButtonEvent action2 = getAction(el, "action2", "arg2x", "arg2y", &arg2x, &arg2y, themeCollection, theme, dx, dy);

    int vertical = 0;
    const char* align = el->Attribute("direction");
    if (align != 0 && strcmp(align, "vertical") == 0) {
        vertical = 1;
    }

    themePageAddDualButton(themePage, activeDualButtonCreate(x, y, cols, activeNotify, bitmap, action1, arg1x, arg1y,
                                                      action2, arg2x, arg2y, vertical), 
                       trigger, visible, THEME_TRIGGER_NONE);
}

static void addKeyButton(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, TiXmlElement* el, ArchBitmap* srcBitmap, 
                         int srcX, int srcY, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int xDesign = -1, yDesign = -1, widthDesign = -1, heightDesign = -1;
    el->QueryIntAttribute("x",      &xDesign);
    el->QueryIntAttribute("y",      &yDesign);
    el->QueryIntAttribute("width",  &widthDesign);
    el->QueryIntAttribute("height", &heightDesign);
    
    if (xDesign < 0 || yDesign < 0 || widthDesign < 0 || heightDesign < 0) {
        return;
    }

    int keycode = getKeyCode(el, "code");
    if (keycode < 0) {
        return;
    }

    /* srcBitmap is pre-stretched in addKeyboard; crop/offset use same scale. */
    int x      = themeScaledCoord(xDesign);
    int y      = themeScaledCoord(yDesign);
    int width  = themeScaledCoord(widthDesign);
    int height = themeScaledCoord(heightDesign);

    ArchBitmap* bitmap = archBitmapCreate(6 * width, height);
    int srcWidth = archBitmapGetWidth(srcBitmap) / 6;

    for (int i = 0; i < 6; i++) {
        archBitmapCopy(bitmap, i * width, 0, srcBitmap, x + i * srcWidth, y, width, height);
    }
    
    ThemeTrigger trigger = (ThemeTrigger)(THEME_TRIGGER_FIRST_KEY_CONFIG + keycode);
    ThemeTrigger pressed = (ThemeTrigger)(THEME_TRIGGER_FIRST_KEY_PRESSED + keycode);
    ButtonEvent action   = (ButtonEvent)actionKeyPress;

    themePageAddToggleButton(themePage, activeToggleButtonCreate(srcX + x, srcY + y, 999, 0, bitmap, action, keycode, -1), 
                         trigger, visible, pressed);
}

static void addKeyboard(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                        TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    /* 6 horizontal frames (one per state); pre-stretch for per-key crops. */
    bitmap = applyThemeScale(bitmap, 6);
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    int srcWidth  = archBitmapGetWidth(bitmap) / 6;
    int srcHeight = archBitmapGetHeight(bitmap);
    ArchBitmap* bgBitmap = archBitmapCreate(srcWidth, srcHeight);
    archBitmapCopy(bgBitmap, 0, 0, bitmap, 0, 0, srcWidth, srcHeight);
    themePageAddImage(themePage, activeImageCreate(x, y, cols, bgBitmap, 1), THEME_TRIGGER_NONE, visible);

    TiXmlElement* keyEl;
    for (keyEl = el->FirstChildElement(); keyEl != NULL; keyEl = keyEl->NextSiblingElement()) {
        if (strcmp(keyEl->Value(), "key") == 0) {
            addKeyButton(themeCollection, theme, themePage, keyEl, bitmap, x, y, visible);
        }
    }

    archBitmapDestroy(bitmap);
}


static void addObject(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                      TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x = 0;
    int y = 0;
    int width = -1;
    int height = -1;

    el->QueryIntAttribute("x", &x);
    el->QueryIntAttribute("y", &y);
    el->QueryIntAttribute("width", &width);
    el->QueryIntAttribute("height", &height);

    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);
    if (width  > 0) width  = themeScaledCoord(width);
    if (height > 0) height = themeScaledCoord(height);


    const char* id = el->Attribute("id");
    if (id == NULL) {
        return;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    LONG_PTR arg1 = 0;
    if (0 == strcmp(id, "dropdown-themepages")) arg1 = (LONG_PTR)theme;

    themePageAddObject(themePage, activeObjectCreate(x, y, width, height, id, arg1, 0), visible);
}

static void addText(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                    TiXmlElement* el, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x, y, cols;
    ArchBitmap* bitmap = loadBitmap(el, &x, &y, &cols);
    if (bitmap == NULL) {
        return;
    }
    int startChar = 0;
    el->QueryIntAttribute("startchar", &startChar);

    int charCount = 256;
    el->QueryIntAttribute("charcount", &charCount);

    /* charCount-glyph sprite sheet; integer-align per-glyph stretch. */
    bitmap = applyThemeScale(bitmap, spriteFrameCount(cols, charCount));
    x = themeScaledCoord(x + dx);
    y = themeScaledY(y + dy);

    ThemeTrigger trigger = (ThemeTrigger)getTrigger(el, "trigger");
    if (trigger == -1) {
        return;
    }

    int width = 10;
    el->QueryIntAttribute("width", &width);
    /* width = character count (memset buffer length), NOT pixels.  Pixel
       width already scales via the pre-stretched glyph cells. */
    
    int type = 0;
    const char* typeStr = el->Attribute("type");
    if (typeStr != NULL && 0 == strcmp(typeStr, "native")) {
        type = 1;
    }

    int color = 0xffffff;
    const char* colStr = el->Attribute("fgcolor");
    if (colStr != NULL) {
        sscanf(colStr, "%x", &color);
        color = archRGB((color>>16), ((color>>8)&0xff), (color&0xff));
    }    

    int rightAlign = 0;
    const char* align = el->Attribute("align");
    if (align != 0 && strcmp(align, "right") == 0) {
        rightAlign = 1;
    }

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(el, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }
    
    themePageAddText(themePage, activeTextCreate(x, y, cols, bitmap, startChar, charCount, width, type, color, rightAlign), 
                     trigger, visible);
}

static void addBlock(ThemeCollection* themeCollection, Theme* theme, ThemePage* themePage, 
                     TiXmlElement* root, int dx, int dy, 
                     ThemeTrigger visible = THEME_TRIGGER_NONE)
{
    int x = 0;
    root->QueryIntAttribute("x", &x);
    int y = 0;
    root->QueryIntAttribute("y", &y);
    dx += x;
    dy += y;

    if (visible == THEME_TRIGGER_NONE) {
        visible = (ThemeTrigger)getTrigger(root, "visible");
        if (visible == -1) {
            visible = THEME_TRIGGER_NONE;
        }
    }

    for (TiXmlElement* el = root->FirstChildElement(); el != NULL; el = el->NextSiblingElement()) {
        if (strcmp(el->Value(), "block") == 0) {
            addBlock(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "image") == 0) {
            addImage(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "dragimage") == 0) {
            addGrabImage(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "led") == 0) {
            addLed(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "text") == 0) {
            addText(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "button") == 0) {
            addButton(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "dualbutton") == 0) {
            addDualButton(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "togglebutton") == 0) {
            addToggleButton(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "keyboard") == 0) {
            addKeyboard(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "meter") == 0) {
            addMeter(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "slider") == 0) {
            addSlider(themeCollection, theme, themePage, el, dx, dy, visible);
        }
        if (strcmp(el->Value(), "object") == 0) {
            addObject(themeCollection, theme, themePage, el, dx, dy, visible);
        }
    }
}

static ThemePage* loadThemePage(ThemeCollection* themeCollection, Theme* theme, 
                                TiXmlElement* root, const char* name, 
                                int width, int height, int frame, 
                                int emuWidth, int emuHeight, int fullscreen) 
{
    ThemePage* themePage = NULL;

    /* Reset top-band shift; recomputed below for pages with <menu>. */
    g_topShift         = 0;
    g_designMenuY      = -1;
    g_designMenuBottom = -1;

    TiXmlElement* infoEl;
    ClipPoint clipPointList[MAX_CLIP_POINTS];  
    int clipPointCount = 0;      
    int emuX           = 0;
    int emuY           = 0;
    int menuX          = 0;
    int menuY          = -100;
    int menuYxml       = -100;  /* raw XML menu y for topShift calc */
    /* Span most of the page so localized menu text fits when <menu width=>
       is omitted (the historical 357 default truncated CJK locales). */
    int menuWidth      = width > 0 ? width - 8 : 800;
    int menuColor      = archRGB(219, 221, 224);
    int menuFocusColor = archRGB(128, 128, 255);
    int menuTextColor  = archRGB(0, 0, 0);
    int color;
    int noFrame        = frame ? 0 : 1;

    // First, get info about emu window and menu
    for (infoEl = root->FirstChildElement(); infoEl != NULL; infoEl = infoEl->NextSiblingElement()) {
        if (strcmp(infoEl->Value(), "emuwindow") == 0) {
            infoEl->QueryIntAttribute("x", &emuX);
            infoEl->QueryIntAttribute("y", &emuY);
        }
        
        if (strcmp(infoEl->Value(), "menu") == 0) {
            if (!fullscreen) {
                infoEl->QueryIntAttribute("x", &menuX);
                infoEl->QueryIntAttribute("y", &menuY);
                infoEl->QueryIntAttribute("width", &menuWidth);
                menuYxml = menuY;
            }
            else {
                menuX = 0;
                menuY = 0;
                menuWidth = emuWidth;
            }
            const char* colStr = infoEl->Attribute("bgcolor");
            if (colStr != NULL) {
                sscanf(colStr, "%x", &color);
                menuColor = archRGB((color>>16), ((color>>8)&0xff), (color&0xff));
            }
            
            colStr = infoEl->Attribute("fgcolor");
            if (colStr != NULL) {
                sscanf(colStr, "%x", &color);
                menuTextColor = archRGB((color>>16), ((color>>8)&0xff), (color&0xff));
            }
            
            colStr = infoEl->Attribute("focuscolor");
            if (colStr != NULL) {
                sscanf(colStr, "%x", &color);
                menuFocusColor = archRGB((color>>16), ((color>>8)&0xff), (color&0xff));
            }
        }

        if (strcmp(infoEl->Value(), "clipregion") == 0) {
            clipPointCount = 0;
            TiXmlElement* ptEl;
            for (ptEl = infoEl->FirstChildElement(); ptEl != NULL; ptEl = ptEl->NextSiblingElement()) {
                if (strcmp(ptEl->Value(), "point") == 0) {
                    int x = -1;
                    int y = -1;
                    ptEl->QueryIntAttribute("x", &x);
                    ptEl->QueryIntAttribute("y", &y);
                    if (x >= 0 && y >= 0 && clipPointCount < MAX_CLIP_POINTS) {
                        ClipPoint clipPoint = { x, y };
                        clipPointList[clipPointCount++] = clipPoint;
                    }
                }
            }
        }
    }

    /* Set menu band globals: themeScaledY shifts only below-band content;
       above-band decoration / title bar stays at natural SC(y). */
    if (!fullscreen && menuYxml >= 0) {
        int menuHRuntime  = archMenuStripHeight();
        if (menuHRuntime <= 0) menuHRuntime = DESIGN_MENU_H;
        g_designMenuY      = menuYxml;
        g_designMenuBottom = menuYxml + DESIGN_MENU_H;
        g_topShift         = themeScaledCoord(DESIGN_MENU_H) - menuHRuntime - EXTERNAL_THEME_TOP_GAP;
    }

    // Scale page-level coords after reading all XML values
    emuX      = themeScaledCoord(emuX);
    emuY      = themeScaledY(emuY);
    if (!fullscreen) {
        menuX     = themeScaledCoord(menuX);
        /* Preserve XML menu y so above-band decoration stays visible
           (Classic with menuYxml=0 lands at y=0 as before). */
        menuY     = themeScaledCoord(menuY);
        menuWidth = themeScaledCoord(menuWidth);
    }

    /* Apply topShift to clipregion points (page-relative shape). */
    for (int i = 0; i < clipPointCount; i++) {
        clipPointList[i].x = themeScaledCoord(clipPointList[i].x);
        clipPointList[i].y = themeScaledY(clipPointList[i].y);
    }

    themePage = themePageCreate(name,
                        themeScaledCoord(width),
                        themeScaledCoord(height) - g_topShift,
                        emuX,
                        emuY,
                        emuWidth,
                        emuHeight,
                        menuX, 
                        menuY, 
                        menuWidth,
                        menuColor,
                        menuFocusColor,
                        menuTextColor,
                        noFrame,
                        clipPointCount,
                        clipPointList);

    addBlock(themeCollection, theme, themePage, root, 0, 0);

    /* Reset for the next page so popup windows / fullscreen don't inherit. */
    g_topShift         = 0;
    g_designMenuY      = -1;
    g_designMenuBottom = -1;

    return themePage;
}

static int loadThemeWindows(ThemeCollection* themeCollection, TiXmlElement* root)
{
    TiXmlElement* modeEl;
    int count = 0;
    
    for (modeEl = root->FirstChildElement(); modeEl != NULL; modeEl = modeEl->NextSiblingElement()) {
        Theme* theme = NULL;

        if (strcmp(modeEl->Value(), "window") != 0) {
            continue;
        }

        const char* name = modeEl->Attribute("name");
        if (name == NULL) {
            name = "window";
        }

        int frame = 1;
        modeEl->QueryIntAttribute("frame", &frame);

        ThemeHandler themeHandler = TH_NORMAL;
        
        const char* handler = modeEl->Attribute("handler");
        if (handler != NULL) {
            if (0 == strcmp(handler, "keyboard-config")) {
                themeHandler = TH_KBDCONFIG;
            }
        }
        const char* type = modeEl->Attribute("type");
        if (type != NULL && 0 == strcmp(type, "multipage")) {
            TiXmlElement* pageEl;
            for (pageEl = modeEl->FirstChildElement(); pageEl != NULL; pageEl = pageEl->NextSiblingElement()) {
                int width  = 320;
                int height = 200;

                pageEl->QueryIntAttribute("width", &width);
                pageEl->QueryIntAttribute("height", &height);
                
                const char* pageName = pageEl->Attribute("name");
                if (pageName == NULL) {
                    pageName = name;
                }

                if (theme == NULL) {
                    theme = themeCreate(name);
                    themeSetHandler(theme, themeHandler);
                }

                ThemePage* themePage = loadThemePage(themeCollection, theme, pageEl, pageName, width, height, frame, 0, 0, 0);
                themeAddPage(theme, themePage);
            }
        }
        else {
            int   width  = 320;
            int   height = 200;

            modeEl->QueryIntAttribute("width", &width);
            modeEl->QueryIntAttribute("height", &height);
            
            if (theme == NULL) {
                theme = themeCreate(name);
                themeSetHandler(theme, themeHandler);
            }
            ThemePage* themePage = loadThemePage(themeCollection, theme, modeEl, name, width, height, frame, 0, 0, 0);
            themeAddPage(theme, themePage);
        }

        if (theme != NULL) {
            themeCollectionAddWindow(themeCollection, theme);
            count++;
        }
    }

    return count;
}

static Theme* loadMainTheme(ThemeCollection* themeCollection, TiXmlElement* root, ThemeInfo themeInfo) 
{
    Theme* theme = NULL;

    TiXmlElement* modeEl;
    
    for (modeEl = root->FirstChildElement(); modeEl != NULL; modeEl = modeEl->NextSiblingElement()) {
        int fullscreen = themeInfo == THEME_FULLSCREEN;
        int emuWidth  = themeDefaultInfo[themeInfo].width;
        int emuHeight = themeDefaultInfo[themeInfo].height;

        if (strcmp(modeEl->Value(), "theme") != 0) {
            continue;
        }

        const char* mode = modeEl->Attribute("mode");
        if (strcmp(mode, themeDefaultInfo[themeInfo].mode) != 0) {
            continue;
        }

        const char* name = themeDefaultInfo[themeInfo].mode;

        int frame  = 1;
        modeEl->QueryIntAttribute("frame", &frame);

        const char* type = modeEl->Attribute("type");
        if (type != NULL && 0 == strcmp(type, "multipage")) {
            TiXmlElement* pageEl;
            for (pageEl = modeEl->FirstChildElement(); pageEl != NULL; pageEl = pageEl->NextSiblingElement()) {
                int width  = themeDefaultInfo[themeInfo].width;
                int height = themeDefaultInfo[themeInfo].height;

                if (!fullscreen) {
                    pageEl->QueryIntAttribute("width", &width);
                    pageEl->QueryIntAttribute("height", &height);
                }
                
                if (theme == NULL) {
                    theme = themeCreate(name);
                }
                const char* pageName = pageEl->Attribute("name");
                if (pageName == NULL) {
                    pageName = name;
                }

                ThemePage* themePage = loadThemePage(themeCollection, theme, pageEl, pageName, width, height, frame, emuWidth, emuHeight, fullscreen);
                themeAddPage(theme, themePage);
            }
        }
        else {
            // Get width and height of main window
            int   width     = themeDefaultInfo[themeInfo].width;
            int   height    = themeDefaultInfo[themeInfo].height;

            if (!fullscreen) {
                modeEl->QueryIntAttribute("width", &width);
                modeEl->QueryIntAttribute("height", &height);
            }

            if (theme == NULL) {
                theme = themeCreate(name);
            }
            ThemePage* themePage = loadThemePage(themeCollection, theme, modeEl, name, width, height, frame, emuWidth, emuHeight, fullscreen);
            themeAddPage(theme, themePage);
        }
    }

    return theme;
}

/* Basename of themePath; fallback when XML has no <bluemsxtheme name=>. */
static const char* themePathToName(const char* themePath)
{
    const char* themeName = strrchr(themePath, '/');
    if (themeName == NULL) {
        themeName = strrchr(themePath, '\\');
    }
    if (themeName == NULL) {
        themeName = themePath - 1;
    }
    return themeName + 1;
}

/* Parse theme.xml; sets rootPath for loadBitmap's relative resolution.
   Returns NULL on failure. */
static TiXmlElement* themeOpenXml(const char* themePath, TiXmlDocument& doc)
{
    static char themeData[256 * 1024];

    strcpy(rootPath, themePath);

    FILE* f = fopen(fullPath("Theme.xml"), "rb");
    if (f == NULL) {
        return NULL;
    }
    int len = fread(themeData, 1, sizeof(themeData), f);
	fclose(f);
    if (len <= 0) {
        return NULL;
    }
    themeData[len] = 0;

    doc.Parse(themeData);
    if (doc.Error()) {
        return NULL;
    }
    TiXmlElement* root = doc.RootElement();
    if (root == NULL || strcmp(root->Value(), "bluemsxtheme") != 0) {
        return NULL;
    }
    return root;
}

/* Read just <bluemsxtheme name=> so picker / settings.themeName match
   without parsing the full XML. */
extern "C" ThemeCollection* themeLoadSkeleton(const char* themePath)
{
    TiXmlDocument doc;
    TiXmlElement* root = themeOpenXml(themePath, doc);
    if (root == NULL) {
        return NULL;
    }
    const char* xmlName = root->Attribute("name");

    ThemeCollection* tc = themeCollectionCreate();
    strncpy(tc->path, themePath, sizeof(tc->path) - 1);
    tc->path[sizeof(tc->path) - 1] = 0;
    /* Prefer XML's display name over directory name so settings.themeName
       from a prior session matches and doesn't fall back to Classic. */
    if (xmlName != NULL && *xmlName != 0) {
        strncpy(tc->name, xmlName, sizeof(tc->name) - 1);
    } else {
        strncpy(tc->name, themePathToName(themePath), sizeof(tc->name) - 1);
    }
    tc->name[sizeof(tc->name) - 1] = 0;
    tc->loaded = 0;
    return tc;
}

/* Idempotent: parse + populate zoom[1..4]/fullscreen/theme[]. */
extern "C" int themeCollectionEnsureLoaded(ThemeCollection* tc)
{
    if (tc == NULL || tc->loaded) {
        return tc != NULL;
    }
    if (tc->path[0] == 0) {
        /* Built-in defensive path; createThemeList already marks loaded=1. */
        tc->loaded = 1;
        return 1;
    }

    TiXmlDocument doc;
    TiXmlElement* root = themeOpenXml(tc->path, doc);
    if (root == NULL) {
        return 0;
    }

    const char* name = root->Attribute("name");
    if (name != NULL) {
        strncpy(tc->name, name, sizeof(tc->name) - 1);
        tc->name[sizeof(tc->name) - 1] = 0;
    }

    tc->zoom[1]    = loadMainTheme(tc, root, THEME_SMALL);
    tc->zoom[2]    = loadMainTheme(tc, root, THEME_NORMAL);
    tc->zoom[3]    = loadMainTheme(tc, root, THEME_TRIPLE);
    tc->zoom[4]    = loadMainTheme(tc, root, THEME_QUAD);
    tc->fullscreen = loadMainTheme(tc, root, THEME_FULLSCREEN);
    loadThemeWindows(tc, root);
    tc->loaded = 1;
    return 1;
}

/* Re-parse "normal" at scale=z/2.0 to synthesise zoom[z].  Caller must
   invoke themeCollectionEnsureLoaded() first. */
extern "C" int themeCollectionEnsureZoom(ThemeCollection* tc, int z)
{
    if (tc == NULL || z < 1 || z >= THEME_ZOOM_COUNT) {
        return 0;
    }
    if (tc->zoom[z] != NULL) {
        return 1;
    }
    if (tc->path[0] == 0 || tc->zoom[2] == NULL) {
        /* Built-in or no normal mode to scale from. */
        return 0;
    }

    TiXmlDocument doc;
    TiXmlElement* root = themeOpenXml(tc->path, doc);
    if (root == NULL) {
        return 0;
    }

    g_themeScale = (double)z / 2.0;
    tc->zoom[z]  = loadMainTheme(tc, root, THEME_NORMAL);
    g_themeScale = 1.0;
    return tc->zoom[z] != NULL;
}

/* Eager-load entry; most callers should prefer the skeleton + EnsureLoaded path. */
extern "C" ThemeCollection* themeLoad(const char* themePath)
{
    ThemeCollection* tc = themeLoadSkeleton(themePath);
    if (tc == NULL) {
        return NULL;
    }
    if (!themeCollectionEnsureLoaded(tc)) {
        themeCollectionDestroy(tc);
        return NULL;
    }
    /* Drop themes that loaded nothing meaningful. */
    if (tc->zoom[1] == NULL && tc->zoom[2] == NULL && tc->fullscreen == NULL) {
        int hasWindow = 0;
        for (int i = 0; i < THEME_MAX_WINDOWS; i++) {
            if (tc->theme[i] != NULL) { hasWindow = 1; break; }
        }
        if (!hasWindow) {
            themeCollectionDestroy(tc);
            return NULL;
        }
    }
    return tc;
}

extern "C" ThemeCollection* themeLoadAtScale(const char* themePath, double scale)
{
    double saved = g_themeScale;
    g_themeScale = scale;
    ThemeCollection* tc = themeLoad(themePath);
    g_themeScale = saved;
    return tc;
}

static ThemeCollection** currentWin32Theme = NULL;

extern "C" ThemeCollection** createThemeList(ThemeCollection** defaultThemes)
{
    const char* singleTheme = appConfigGetString("singletheme", NULL);
    ThemeCollection** themeList = (ThemeCollection**)calloc(1, 128 * sizeof(ThemeCollection*));
    int index = 0;

    /* Built-ins are fully loaded; mark loaded=1 so EnsureLoaded short-circuits. */
    while (defaultThemes != NULL && *defaultThemes != NULL) {
        (*defaultThemes)->loaded = 1;
        themeList[index++] = *defaultThemes;
        defaultThemes++;
    }

    if (singleTheme != NULL) {
        char themeName[64] = "Themes/";
        strcat(themeName, singleTheme);
        ThemeCollection* tc = themeLoadSkeleton(themeName);
        if (tc != NULL) {
            themeList[index++] = tc;
        }
    }
    else {
        ArchGlob* glob = archGlob("Themes/*", ARCH_GLOB_DIRS);
        if (glob != NULL) {
            for (int i = 0; i < glob->count; i++) {
                ThemeCollection* tc = themeLoadSkeleton(glob->pathVector[i]);
                if (tc != NULL) {
                    themeList[index++] = tc;
                }
            }
            archGlobFree(glob);
        }
    }
    themeList[index] = NULL;

    /* Skeletons only; EnsureLoaded does the parse on first selection to
       keep GDI handle usage bounded to the active theme + zoom. */

    currentWin32Theme = themeList;
    return themeList;
}

extern "C" ThemeCollection** themeGetAvailable()
{
    return currentWin32Theme;
}

extern "C" int getThemeListIndex(ThemeCollection** themeList, const char* name, int forceMatch)
{
    int index = 0;
    while (*themeList) {
        if (strcmpnocase(name, (*themeList)->name) == 0) {
            return index;
        }
        index++;
        themeList++;
    }

    if (forceMatch) {
        return 0;
    }

    return -1;
}
