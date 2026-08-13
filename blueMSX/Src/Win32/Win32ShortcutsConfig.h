/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32ShortcutsConfig.h,v $
**
** $Revision: 1.15 $
**
** $Date: 2008-03-30 18:38:48 $
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
#ifndef WIN32_SHORTCUTSCONFIG_H
#define WIN32_SHORTCUTSCONFIG_H

#include <windows.h>
#include "Win32Properties.h"


#define HOTKEY_TYPE_NONE     0
#define HOTKEY_TYPE_KEYBOARD 1
#define HOTKEY_TYPE_JOYSTICK 2

#define SHORTCUT_MAX_BINDINGS 3

typedef struct {
    unsigned type : 8;
    unsigned mods : 8;
    unsigned key  : 16;
} ShotcutHotkey;

/* Empty slots have type HOTKEY_TYPE_NONE. */
typedef struct {
    ShotcutHotkey slots[SHORTCUT_MAX_BINDINGS];
} ShotcutHotkeySet;



typedef struct {
    ShotcutHotkeySet spritesEnable;
    ShotcutHotkeySet fdcTiming;
    ShotcutHotkeySet hddSdBoost;
    ShotcutHotkeySet noSpriteLimits;
    ShotcutHotkeySet msxKeyboardQuirk;
    ShotcutHotkeySet msxAudioSwitch;
    ShotcutHotkeySet frontSwitch;
    ShotcutHotkeySet pauseSwitch;
    ShotcutHotkeySet quit;
    ShotcutHotkeySet wavCapture;
    ShotcutHotkeySet wavCaptureStartAs;
    ShotcutHotkeySet videoCapLoad;
    ShotcutHotkeySet videoCapPlay;
    ShotcutHotkeySet videoCapRec;
    ShotcutHotkeySet videoCapRecAs;
    ShotcutHotkeySet videoCapStop;
    ShotcutHotkeySet videoCapSave;
    ShotcutHotkeySet recordVideoStart;
    ShotcutHotkeySet recordVideoStartAs;
    ShotcutHotkeySet recordVideoStop;
    ShotcutHotkeySet recordVideoToggle;
    ShotcutHotkeySet ym2413BackendCycle;
    ShotcutHotkeySet y8950BackendCycle;
    ShotcutHotkeySet screenCapture;
    ShotcutHotkeySet screenCaptureAs;
    ShotcutHotkeySet screenCaptureUnfilteredSmall;
    ShotcutHotkeySet screenCaptureUnfilteredLarge;
    ShotcutHotkeySet cpuStateLoad;
    ShotcutHotkeySet cpuStateSave;
    ShotcutHotkeySet cpuStateQuickLoad;
    ShotcutHotkeySet cpuStateQuickSave;
    ShotcutHotkeySet cpuStateQuickSaveUndo;

    ShotcutHotkeySet cartInsert[2];
    ShotcutHotkeySet cartSpecialMenu[2];
    ShotcutHotkeySet cartRemove[2];
    ShotcutHotkeySet cartAutoReset[2];

    ShotcutHotkeySet diskInsert[2];
    ShotcutHotkeySet diskDirInsert[2];
    ShotcutHotkeySet diskChange[2];
    ShotcutHotkeySet diskRemove[2];
    ShotcutHotkeySet diskAutoReset[2];

    ShotcutHotkeySet casInsert;
    ShotcutHotkeySet casRewind;
    ShotcutHotkeySet casSetPos;
    ShotcutHotkeySet casRemove;
    ShotcutHotkeySet casToggleReadonly;
    ShotcutHotkeySet casAutoRewind;
    ShotcutHotkeySet casSave;

    ShotcutHotkeySet prnFormFeed;
    ShotcutHotkeySet mouseLockToggle;
    ShotcutHotkeySet emulationRunPause;
    ShotcutHotkeySet emulationStop;
    ShotcutHotkeySet emuSpeedFull;
    ShotcutHotkeySet emuPlayReverse;
    ShotcutHotkeySet emuSpeedNormal;
    ShotcutHotkeySet emuSpeedInc;
    ShotcutHotkeySet emuSpeedDec;
    ShotcutHotkeySet emuSpeedToggle;
    ShotcutHotkeySet windowSize1x;
    ShotcutHotkeySet windowSize2x;
    ShotcutHotkeySet windowSize3x;
    ShotcutHotkeySet windowSize4x;
    ShotcutHotkeySet windowSize5x;
    ShotcutHotkeySet windowSize6x;
    ShotcutHotkeySet windowSize7x;
    ShotcutHotkeySet windowSize8x;
    ShotcutHotkeySet windowSizeMinimized;
    ShotcutHotkeySet windowSizeFullscreen;
    ShotcutHotkeySet windowSizeFullscreenToggle;
    ShotcutHotkeySet resetSoft;
    ShotcutHotkeySet resetHard;
    ShotcutHotkeySet resetClean;
    ShotcutHotkeySet volumeIncrease;
    ShotcutHotkeySet volumeDecrease;
    ShotcutHotkeySet volumeMute;
    ShotcutHotkeySet volumeStereo;
    ShotcutHotkeySet themeSwitch;
    ShotcutHotkeySet propShowEmulation;
    ShotcutHotkeySet propShowVideo;
    ShotcutHotkeySet propShowAudio;
    ShotcutHotkeySet propShowEffects;
    ShotcutHotkeySet propShowSettings;
    ShotcutHotkeySet propShowApearance;
    ShotcutHotkeySet propShowPorts;
    ShotcutHotkeySet optionsShowLanguage;
    ShotcutHotkeySet toolsShowMachineEditor;
    ShotcutHotkeySet toolsShowShorcutEditor;
    ShotcutHotkeySet toolsShowKeyboardEditor;
    ShotcutHotkeySet toolsShowMixer;
    ShotcutHotkeySet toolsShowDebugger;
    ShotcutHotkeySet toolsShowTrainer;
    ShotcutHotkeySet helpShowHelp;
    ShotcutHotkeySet helpShowAbout;
} Shortcuts;


Shortcuts* shortcutsCreateProfile(char* profileName);
void shortcutsDestroyProfile(Shortcuts* shortcuts);
int shortcutsShowDialog(HWND hwnd, Properties* pProperties);
char* shortcutsToString(ShotcutHotkey hotkey);
char* shortcutsSetToString(const ShotcutHotkeySet* set);
int shortcutSetHasHotkey(const ShotcutHotkeySet* set, ShotcutHotkey hotkey);

/* Both read the dialog's working copy while it is open, else the live
** profile. */
int shortcutsCountActionsUsingDik(int dik);
int shortcutsDescribeActionsUsingDik(int dik, char* out, int outLen);

void shortcutsSetDirectory(char* profileDir);
int shortcutsIsProfileValid(char* profileName);
int shortcutsGetAnyProfile(char* profileName);

#endif //WIN32_STATUSBAR_H
