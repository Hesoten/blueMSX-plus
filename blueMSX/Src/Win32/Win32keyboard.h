/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32keyboard.h,v $
**
** $Revision: 1.17 $
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
#ifndef WIN32_KEYBOARD_H
#define WIN32_KEYBOARD_H
 
#include "ArchInput.h"

#include <windows.h>

#define KBD_LSHIFT 0x01
#define KBD_RSHIFT 0x02
#define KBD_LCTRL  0x04
#define KBD_RCTRL  0x08
#define KBD_LALT   0x10
#define KBD_RALT   0x20
#define KBD_LWIN   0x40
#define KBD_RWIN   0x80

#define INPUT_MAX_JOYSTICKS 8

void inputInit();
int inputReset(HWND hwnd);
void inputDestroy(void);

/* Hot-plug: WM_DEVICECHANGE marks dirty and re-probes the XInput
   connection gates; the Shortcut Config dialog also refreshes on open
   so no event is missed while blueMSX was in the background. */
void inputMarkDirty(void);
void inputRefreshDevicesIfDirty(void);

/* True for a JIS physical layout or a Japanese system locale. */
int inputKeyboardRegionIsJapanese(void);

void keyboardSetDirectory(char* directory);
void keyboardSetSharedDirectory(char* directory);

int keyboardLoadConfig(char* configName);
void keyboardSaveConfig(char* configName);
char* keyboardGetCurrentConfig();
char** keyboardGetConfigs();

// For configuration
void keyboardStartConfig();
void keyboardCancelConfig();
int  keyboardConfigIsModified();
void keybardEnableEdit(int enable);
int archKeyboardIsKeySelected(int msxKeyCode);
int archKeyboardIsKeyConfigured(int msxKeyCode);
void archKeyboardSetSelectedKey(int msxKeyCode);
void archKeyboardClearSelectedKey(void);
int  archKeyboardSelectedBindingCount(void);

/* Counts MSX target ECs bound to this DIK across every table;
** excludeTable/excludeEc drop the entry the caller is describing. */
int  bindingsCountTargetsForDik(int dik);
int  bindingsDescribeTargetsForDik(int dik, int excludeTable, int excludeEc,
                                   char* out, int outLen);
char* archKeyboardConflictText(void);
char* archKeyboardConflictTextForKey(int msxKeyCode);

/* Registered by Win32ShortcutsConfig so Bindings can count shortcut
** conflicts. */
typedef int (*ShortcutDikCounter)(int dik);
typedef int (*ShortcutDikDescriber)(int dik, char* out, int outLen);
void inputSetShortcutDikCounter(ShortcutDikCounter fn);
void inputSetShortcutDikDescriber(ShortcutDikDescriber fn);

/* Fired after enumeration so a shortcut held by name binds without a
** reload. */
typedef void (*ShortcutDeviceArrival)(void);
void inputSetShortcutDeviceArrival(ShortcutDeviceArrival fn);

void keyboardEnable(int enable);
void keyboardUpdate();

int keyboardGetModifiers();

void joystickUpdate();
DWORD joystickGetButtonState();
/* Zero for a slot with no device; joystickGetButtonState merges all
** slots. */
DWORD joystickGetButtonStatePerJoy(int index);

/* Name built at device enumeration, "XInput 1 : A"; "" for an unset
** slot.  The Display forms shorten it for the UI. */
char* dik2str(int dikKey);
char* dik2strDisplay(int dikKey);
/* The same shortening, for a name held without a DIK behind it. */
char* dikNameToDisplay(char* full);
int   inputResolveDikName(const char* token);
const char* inputNextToken(const char* p, char* out, int outLen);
void  inputAppendToken(char* list, int listLen, const char* token);
void keyboardSetFocus(int handle, int focus);
int joystickNumButtons(int index);
void joystickSetButtons(int index, int buttonA, int buttonB);

#endif

