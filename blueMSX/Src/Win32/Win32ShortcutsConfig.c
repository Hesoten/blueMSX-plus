/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32ShortcutsConfig.c,v $
**
** $Revision: 1.33 $
**
** $Date: 2008-05-06 17:48:55 $
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
#include <windows.h>
#include <math.h>
#include <commctrl.h>
#include <stdlib.h>
#include <direct.h>
#include <stdio.h>
#include <string.h>
 
#include "Win32ShortcutsConfig.h"
#include "Win32Common.h"
#include "Win32keyboard.h"
#include "Win32TextUtf8.h"
#include "IniFileParser.h"
#include "Resource.h"



// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"


#define WM_INITIALIZE       (WM_USER + 1634)
#define WM_SET_HOTKEYSET    (WM_USER + 1637)
#define WM_GET_HOTKEYSET    (WM_USER + 1638)
#define WM_CLEAR_HOTKEYSET  (WM_USER + 1639)

static char virtualKeys[256][32] = {
    "",
    "", //"LBUTTON", 
    "", //"RBUTTON",
    "Cancel",
    "", //"MBUTTON",
    "", //"XBUTTON1",
    "", //"XBUTTON2",
    "",
    "Backspace",
    "Tab",
    "",
    "",
    "Clear",
    "Enter",
    "",
    "",
    "", // SHIFT
    "", // CTRL
    "", // ALT
    "Pause",
    "CapsLk",
    "Kana",
    "",
    "Junja",
    "Final",
    "Kanji",
    "",
    "esc",
    "Conv",
    "NoConv",
    "Accept",
    "ModeCh",
    "Space",
    "PgUp",
    "PgDown",
    "End",
    "Home",
    "Left",
    "Up",
    "Right",
    "Down",
    "Select",
    "Print",
    "Exec",
    "PrScr",
    "Ins",
    "Del",
    "Help",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "LWIN",
    "RWIN",
    "APPS",
    "",
    "Sleep",
    "Num0",
    "Num1",
    "Num2",
    "Num3",
    "Num4",
    "Num5",
    "Num6",
    "Num7",
    "Num8",
    "Num9",
    "Num*",
    "Num+",
    "Num,",
    "Num-",
    "Num.",
    "Num/",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",
    "F13",
    "F14",
    "F15",
    "F16",
    "F17",
    "F18",
    "F19",
    "F20",
    "F21",
    "F22",
    "F23",
    "F24",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "NumLk",
    "ScrLk",
    "Oem1",
    "Oem2",
    "Oem3",
    "Oem4",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "LShift",
    "RShift",
    "LCTRL",
    "RCONTROL",
    "LALT",
    "RALT",
    "BrBack",
    "BrForward",
    "BrRefresh",
    "BrStop",
    "BrSearch",
    "BrFavorites",
    "BrHome",
    "VolMute",
    "VolDown",
    "VolUp",
    "NextTrk",
    "PrevTrk",
    "MdStop",
    "MdPlay",
    "Mail",
    "MdSelect",
    "App1",
    "App2",
    "",
    "",
    ";",
    "+",
    ",",
    "-",
    ".",
    "?",
    "~",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "[",
    "\\",
    "]",
    "\"",
    "Oem5",
    "",
    "Oem6",
    "Oem7",
    "Oem8",
    "Oem9",
    "Process",
    "Oem10",
    "",
    "",
    "Oem11",
    "Oem12",
    "Oem13",
    "Oem14",
    "Oem15",
    "Oem16",
    "Oem17",
    "Oem18",
    "Oem19",
    "Oem20",
    "Oem21",
    "Oem22",
    "Oem23",
    "Addn",
    "CrSel",
    "ExSel",
    "Ereof",
    "Play",
    "Zoom",
    "",
    "PA1",
    "Clear",
    ""
};

static char       profileDir[] = "Shortcut Profiles";
static char       shortcutProfile[128];
static char       tmpShortcutProfile[128];
static WNDPROC    baseHotkeyCtrlProc = NULL;
static HWND       baseHwnd;
static Shortcuts* shortcuts;
static Shortcuts* shortcutsRef;
/* Save As moves shortcutProfile ahead of the snapshot, so track it here. */
static char shortcutsRefProfile[128];
static ShotcutHotkeySet* hotkeyList[128];

/* hotkeyList[] points into the dialog-scoped copy; 0 once it closes. */
static int hotkeyListValid = 0;

/* Owned by Win32.c; the conflict counter falls back to it when the dialog is shut. */
static Shortcuts* liveShortcuts = NULL;

/* Runtime only: on disk a joystick binding is stored by device name. */
#define SHORTCUTS_MAX_JOY           8
#define SHORTCUTS_JOY_DIK_BASE      256
#define SHORTCUTS_JOY_MAX_CONTROL   32
#define SHORTCUTS_JOY_SLOT(k)       (((k) >> 8) & 0xFF)
#define SHORTCUTS_JOY_BUTTON(k)     ((k) & 0xFF)
#define SHORTCUTS_JOY_ENCODE(s, b)  ((unsigned)(((s) + 1) << 8) | (unsigned)(b))

/* A slot past the live range: the device the profile names is not attached. */
#define SHORTCUTS_JOY_REMEMBERED    (SHORTCUTS_MAX_JOY + 1)
#define SHORTCUTS_REMEMBERED_BASE   (SHORTCUTS_JOY_REMEMBERED << 8)
/* LEN must stay under one[] in shortcutSaveHotkeySet. */
#define SHORTCUTS_REMEMBERED_MAX    512
#define SHORTCUTS_REMEMBERED_LEN    240

static char rememberedNames[SHORTCUTS_REMEMBERED_MAX][SHORTCUTS_REMEMBERED_LEN];
static int  rememberedCount = 0;

/* Interning: equal names must give equal keys, the tests compare the key. */
static unsigned shortcutRememberName(const char* name)
{
    int i;
    for (i = 0; i < rememberedCount; i++) {
        if (0 == strcmp(rememberedNames[i], name)) break;
    }
    if (i == rememberedCount) {
        if (rememberedCount == SHORTCUTS_REMEMBERED_MAX) return 0;
        if (strlen(name) >= SHORTCUTS_REMEMBERED_LEN) return 0;
        strcpy(rememberedNames[i], name);
        rememberedCount++;
    }
    return (unsigned)(SHORTCUTS_REMEMBERED_BASE + i);
}

static char* shortcutRememberedName(ShotcutHotkey h)
{
    int i;
    if (h.type != HOTKEY_TYPE_JOYSTICK) return "";
    i = (int)h.key - SHORTCUTS_REMEMBERED_BASE;
    if (i < 0 || i >= rememberedCount) return "";
    return rememberedNames[i];
}

/* Copied from <dinput.h>, which this unit does not include. */
#define SHORTCUTS_DIK_NUMLOCK 0x45
#define SHORTCUTS_DIK_PAUSE   0xC5

static int hotkeyToDik(ShotcutHotkey h) {
    UINT sc;
    if (h.type == HOTKEY_TYPE_NONE) return 0;
    if (h.type == HOTKEY_TYPE_JOYSTICK) {
        int slot   = SHORTCUTS_JOY_SLOT(h.key);
        int button = SHORTCUTS_JOY_BUTTON(h.key);
        if (slot < 1 || slot > SHORTCUTS_MAX_JOY) return 0;
        if (button < 1 || button > SHORTCUTS_JOY_MAX_CONTROL) return 0;
        return SHORTCUTS_JOY_DIK_BASE + (slot - 1) * 32 + button - 1;
    }
    if (h.type != HOTKEY_TYPE_KEYBOARD) return 0;
    /* MSX bindings have no modifiers, so a modified hotkey never collides. */
    if (h.mods != 0) return 0;
    /* MapVirtualKey collapses Pause's E1 prefix onto Numlock's 0x45, so
    ** both are short-circuited. */
    if (h.key == VK_PAUSE)   return SHORTCUTS_DIK_PAUSE;
    if (h.key == VK_NUMLOCK) return SHORTCUTS_DIK_NUMLOCK;
    /* MAPVK_VK_TO_VSC_EX flags extended keys via 0xE0 in the high byte;
    ** DirectInput encodes the same as bit 7 of the DIK code
    ** (DIK_HOME = 0xC7 = 0x80 | 0x47), so we OR it back in. */
    sc = MapVirtualKey(h.key, MAPVK_VK_TO_VSC_EX);
    if (sc == 0) return 0;
    if ((sc & 0xFF00) == 0xE000) return 0x80 | (int)(sc & 0xFF);
    return (int)(sc & 0xFF);
}

static int dikToHotkey(int dik, ShotcutHotkey* out) {
    UINT sc, vk;
    out->mods = 0;
    out->key  = 0;
    out->type = HOTKEY_TYPE_NONE;
    if (dik <= 0) return 0;
    if (dik >= SHORTCUTS_JOY_DIK_BASE) {
        int offset = dik - SHORTCUTS_JOY_DIK_BASE;
        int slot   = offset / 32;
        int button = (offset % 32) + 1;
        if (button < 1 || button > SHORTCUTS_JOY_MAX_CONTROL) return 0;
        out->type = HOTKEY_TYPE_JOYSTICK;
        out->key  = SHORTCUTS_JOY_ENCODE(slot, button);
        return 1;
    }
    if (dik == SHORTCUTS_DIK_PAUSE) {
        out->type = HOTKEY_TYPE_KEYBOARD;
        out->key  = VK_PAUSE;
        return 1;
    }
    if (dik == SHORTCUTS_DIK_NUMLOCK) {
        out->type = HOTKEY_TYPE_KEYBOARD;
        out->key  = VK_NUMLOCK;
        return 1;
    }
    if (dik & 0x80) sc = 0xE000 | (UINT)(dik & 0x7F);
    else            sc = (UINT)dik;
    vk = MapVirtualKey(sc, MAPVK_VSC_TO_VK_EX);
    if (vk == 0) return 0;
    out->type = HOTKEY_TYPE_KEYBOARD;
    out->key  = (unsigned)vk;
    return 1;
}

static int shortcutSetMatchesDik(const ShotcutHotkeySet* set, ShotcutHotkey probe) {
    return shortcutSetHasHotkey(set, probe);
}

int shortcutsCountActionsUsingDik(int dik) {
    ShotcutHotkey probe;
    int i, count = 0;
    if (!dikToHotkey(dik, &probe)) return 0;
    if (hotkeyListValid) {
        for (i = 0; i < (int)(sizeof(hotkeyList) / sizeof(void*)); i++) {
            if (!hotkeyList[i]) continue;
            if (shortcutSetMatchesDik(hotkeyList[i], probe)) count++;
        }
    }
    else if (liveShortcuts) {
        const ShotcutHotkeySet* sets = (const ShotcutHotkeySet*)liveShortcuts;
        int n = (int)(sizeof(Shortcuts) / sizeof(ShotcutHotkeySet));
        for (i = 0; i < n; i++) {
            if (shortcutSetMatchesDik(&sets[i], probe)) count++;
        }
    }
    return count;
}


static DWORD hotkey2int(ShotcutHotkey hotkey) {
    return (hotkey.key<<16)|(hotkey.mods<<8)|hotkey.type;
}

static ShotcutHotkey int2hotkey(DWORD* hotkey) {
    return *(ShotcutHotkey*)hotkey;
}

static int hotkeyIsBound(ShotcutHotkey h) {
    return h.type != HOTKEY_TYPE_NONE;
}

int shortcutSetHasHotkey(const ShotcutHotkeySet* set, ShotcutHotkey hotkey)
{
    int b;
    if (!set || !hotkeyIsBound(hotkey)) return 0;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        if (hotkeyIsBound(set->slots[b]) &&
            hotkey2int(set->slots[b]) == hotkey2int(hotkey)) {
            return 1;
        }
    }
    return 0;
}

char* shortcutsSetToString(const ShotcutHotkeySet* set)
{
    static char buf[192];
    int b;
    buf[0] = 0;
    if (!set) return buf;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        char* one;
        if (!hotkeyIsBound(set->slots[b])) continue;
        one = shortcutsToString(set->slots[b]);
        if (!one || one[0] == 0) continue;
        if (buf[0]) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, one, sizeof(buf) - strlen(buf) - 1);
    }
    return buf;
}

static char** getProfileList() 
{
    static char profileArray[128][64];
    static char* profileList[128];
    char fileName[MAX_PATH];
	HANDLE handle;
	WIN32_FIND_DATAA wfd;
    int index = 0;
    BOOL cont;
    
    sprintf(fileName, "%s/*.shortcuts", profileDir);

    handle = FindFirstFileU(fileName, &wfd);
    
    cont = handle != INVALID_HANDLE_VALUE;
    
    while (cont) {
		DWORD fa = wfd.dwFileAttributes;

        /* FILE_ATTRIBUTE_NORMAL is rarely set in practice (ARCHIVE wins);
        ** accept anything that isn't a directory. */
        if (!(fa & FILE_ATTRIBUTE_DIRECTORY)) {
            char buffer[128];
            int length = (int)strlen(wfd.cFileName) - 10;
            strcpy(buffer, wfd.cFileName);
            buffer[length] = 0;
            strcpy(profileArray[index], buffer);
            profileList[index] = (char*)profileArray[index];
            index++;
        }   
        cont = FindNextFileU(handle, &wfd);
    }
    
    if (handle != INVALID_HANDLE_VALUE) FindClose(handle);
    profileList[index] = NULL;

    return profileList;
}

/* Set by IDC_SAVE before showing saveProc: 0 for overwrite-existing,
** 1 for create-new. Switches the prompt body text. */
static int s_savePromptIsCreate = 0;

static BOOL_DLG_RET CALLBACK saveProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg) {        
    case WM_INITDIALOG:
        {
            char buffer[128];
            SetWindowTextU(hDlg, langShortcutSaveConfig());

            sprintf(buffer, "%s\n\n    \"%s\" ?",
                    s_savePromptIsCreate ? langShortcutCreateConfig() : langShortcutOverwriteConfig(),
                    shortcutProfile);

            SetWindowTextU(GetDlgItem(hDlg, IDC_CONF_SAVEDLG_TEXT), buffer);
            SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgOK());
            SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());
        }
        win32CommonApplyDark(hDlg);
        return FALSE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

static BOOL_DLG_RET CALLBACK closeProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg) {        
    case WM_INITDIALOG:
        SetWindowTextU(hDlg, langShortcutExitConfig());
        SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgOK());
        SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());
        SetWindowTextU(GetDlgItem(hDlg, IDC_CONF_SAVEDLG_TEXT), langShortcutDiscardConfig());

        win32CommonApplyDark(hDlg);
        return FALSE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

static BOOL_DLG_RET CALLBACK discardProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg) {        
    case WM_INITDIALOG:
        SetWindowTextU(hDlg, langShortcutConfigTitle());
        SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgOK());
        SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());
        SetWindowTextU(GetDlgItem(hDlg, IDC_CONF_SAVEDLG_TEXT), langShortcutDiscardConfig());
        win32CommonApplyDark(hDlg);
        return FALSE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

#include "Win32machineConfig.h"

static BOOL_DLG_RET CALLBACK saveAsProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg) {        
    case WM_INITDIALOG:
        SetWindowTextU(hDlg, langShortcutSaveConfigAs());
        SetWindowTextU(GetDlgItem(hDlg, IDC_MACHINENAMETEXT), langShortcutConfigName());
        SetWindowTextU(GetDlgItem(hDlg, IDOK), langDlgSave());
        SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), langDlgCancel());

        {
            char** profileList = getProfileList();
            int index = 0;

            EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
                    
            while (profileList[index] != NULL) {
                ListBoxAddStringU(GetDlgItem(hDlg, IDC_MACHINELIST), profileList[index]);
                if (0 == strcmpnocase(profileList[index], shortcutProfile) && strcmp(shortcutProfile, langShortcutNewProfile())) {
                    SetWindowTextU(GetDlgItem(hDlg, IDC_MACHINENAME), shortcutProfile);
                    SendDlgItemMessage(hDlg, IDC_MACHINELIST, LB_SETCURSEL, index, 0);
                    EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
                }
                index++;
            }
        }

        win32CommonApplyDark(hDlg);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MACHINELIST:
            if (HIWORD(wParam) == 1 || HIWORD(wParam) == 2) {
                char buffer[64];
                int index = (int)SendMessage(GetDlgItem(hDlg, IDC_MACHINELIST), LB_GETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hDlg, IDC_MACHINELIST), LB_GETTEXT, index, (LPARAM)buffer);
                SetWindowTextU(GetDlgItem(hDlg, IDC_MACHINENAME), buffer);
                if (HIWORD(wParam) == 2) {
                    SendMessage(hDlg, WM_COMMAND, IDOK, 0);
                }
            }
            return TRUE;

        case IDC_MACHINENAME:
            {
                char sel[64];

                GetWindowTextU(GetDlgItem(hDlg, IDC_MACHINENAME), sel, 63);

                EnableWindow(GetDlgItem(hDlg, IDOK), strlen(sel) != 0);      

                SendDlgItemMessage(hDlg, IDC_MACHINELIST, LB_SETCURSEL, -1, 0);

                if (strlen(sel)) {
                    char** profileList = getProfileList();
                    int index = 0;

                    while (profileList[index] != NULL) {
                        if (0 == strcmpnocase(sel, profileList[index])) {
                            SendDlgItemMessage(hDlg, IDC_MACHINELIST, LB_SETCURSEL, index, 0);
                        }
                        index++;
                    }
                }
            }
            return TRUE;

        case IDOK:
            GetWindowTextU(GetDlgItem(hDlg, IDC_MACHINENAME), tmpShortcutProfile, 63);
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

static void captureBufferCompact(ShotcutHotkeySet* buf)
{
    int b, w;
    for (w = 0, b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        if (buf->slots[b].type != HOTKEY_TYPE_NONE) {
            buf->slots[w++] = buf->slots[b];
        }
    }
    for (; w < SHORTCUT_MAX_BINDINGS; w++) {
        buf->slots[w].type = HOTKEY_TYPE_NONE;
        buf->slots[w].mods = 0;
        buf->slots[w].key  = 0;
    }
}

/* Re-pressing a key drops just that one, so a bad binding can be fixed
** without re-entering the others. */
static void captureBufferToggle(ShotcutHotkeySet* buf, ShotcutHotkey h)
{
    int b;
    if (h.type == HOTKEY_TYPE_NONE) return;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        if (buf->slots[b].type != HOTKEY_TYPE_NONE &&
            hotkey2int(buf->slots[b]) == hotkey2int(h)) {
            buf->slots[b].type = HOTKEY_TYPE_NONE;
            buf->slots[b].mods = 0;
            buf->slots[b].key  = 0;
            captureBufferCompact(buf);
            return;
        }
    }
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        if (buf->slots[b].type == HOTKEY_TYPE_NONE) {
            buf->slots[b] = h;
            return;
        }
    }
}

static int captureHotkeyConflicts(ShotcutHotkey probe, int selfIndex)
{
    int i;
    int dik;
    if (probe.type == HOTKEY_TYPE_NONE) return 0;
    for (i = 0; i < (int)(sizeof(hotkeyList) / sizeof(void*)); i++) {
        if (i == selfIndex) continue;
        if (!hotkeyList[i]) continue;
        if (shortcutSetHasHotkey(hotkeyList[i], probe)) return 1;
    }
    dik = hotkeyToDik(probe);
    if (dik > 0 && bindingsCountTargetsForDik(dik) > 0) return 1;
    return 0;
}

static int captureClearRect(HWND hwnd, RECT* out)
{
    RECT r;
    int side;
    GetClientRect(hwnd, &r);
    side = r.bottom - r.top - 2;
    if (side < 12) side = 12;
    if (r.right - r.left < side + 4) return 0;
    out->right  = r.right - 2;
    out->left   = out->right - side;
    out->top    = r.top + (r.bottom - r.top - side) / 2;
    out->bottom = out->top + side;
    return 1;
}

static LRESULT CALLBACK hotkeyCtrlProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static ShotcutHotkeySet captureBuffer;
    static int captureSelfIndex = -1;
    static int modifiers;
    static int virtKey;
    static DWORD joyPrevState[SHORTCUTS_MAX_JOY];
    static int keycount;

    switch (iMsg) {
    case WM_INITIALIZE:
        {
            int s;
            memset(&captureBuffer, 0, sizeof(captureBuffer));
            captureSelfIndex = -1;
            modifiers = 0;
            virtKey = 0;
            keycount = 0;
            SetTimer(hwnd, 154, 100, NULL);
            joystickUpdate();
            for (s = 0; s < SHORTCUTS_MAX_JOY; s++) {
                joyPrevState[s] = joystickGetButtonStatePerJoy(s);
            }
        }
        return 0;

    case WM_SET_HOTKEYSET:
        {
            const ShotcutHotkeySet* src = (const ShotcutHotkeySet*)lParam;
            if (src) captureBuffer = *src;
            else memset(&captureBuffer, 0, sizeof(captureBuffer));
            captureSelfIndex = (int)wParam;
            modifiers = 0;
            virtKey = 0;
            keycount = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

    case WM_KILLFOCUS:
        /* A release after focus moves never reaches us, so keycount would
        ** stay above zero and block every later capture. */
        if (virtKey != 0) {
            ShotcutHotkey h;
            h.type = HOTKEY_TYPE_KEYBOARD; h.mods = modifiers; h.key = virtKey;
            captureBufferToggle(&captureBuffer, h);
        }
        keycount = 0;
        modifiers = 0;
        virtKey = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        break;

    case WM_GET_HOTKEYSET:
        {
            ShotcutHotkeySet* dst = (ShotcutHotkeySet*)lParam;
            /* Assign can arrive before the key is released, so fold the
            ** pending one into a copy. */
            ShotcutHotkeySet snap = captureBuffer;
            if (virtKey != 0) {
                ShotcutHotkey h;
                h.type = HOTKEY_TYPE_KEYBOARD; h.mods = modifiers; h.key = virtKey;
                captureBufferToggle(&snap, h);
            }
            if (dst) *dst = snap;
            return 0;
        }

    case WM_CLEAR_HOTKEYSET:
        memset(&captureBuffer, 0, sizeof(captureBuffer));
        modifiers = 0;
        virtKey = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_TIMER:
        if (wParam == 154) {
            int slot, bit;
            int captured = 0;
            /* Unfocused, idle sticks and triggers would fill the buffer
            ** on their own. */
            if (!IsWindowEnabled(hwnd) || GetFocus() != hwnd) {
                for (slot = 0; slot < SHORTCUTS_MAX_JOY; slot++) {
                    joyPrevState[slot] = joystickGetButtonStatePerJoy(slot);
                }
                return 0;
            }
            joystickUpdate();
            if (keycount == 0) {
                for (slot = 0; slot < SHORTCUTS_MAX_JOY && !captured; slot++) {
                    DWORD newState = joystickGetButtonStatePerJoy(slot);
                    DWORD rising   = newState & ~joyPrevState[slot];
                    for (bit = 0; bit < 32 && rising; bit++, rising >>= 1) {
                        if (rising & 1) {
                            ShotcutHotkey h;
                            h.type = HOTKEY_TYPE_JOYSTICK;
                            h.mods = 0;
                            h.key  = SHORTCUTS_JOY_ENCODE(slot, bit + 1);
                            modifiers = 0;
                            virtKey = 0;
                            captureBufferToggle(&captureBuffer, h);
                            InvalidateRect(hwnd, NULL, FALSE);
                            captured = 1;
                            break;
                        }
                    }
                }
            }
            for (slot = 0; slot < SHORTCUTS_MAX_JOY; slot++) {
                joyPrevState[slot] = joystickGetButtonStatePerJoy(slot);
            }
            return 0;
        }
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_WANTARROWS;

    case WM_SETCURSOR:
        {
            RECT xr;
            POINT p;
            if (IsWindowEnabled(hwnd) && captureClearRect(hwnd, &xr)) {
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                if (PtInRect(&xr, p)) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
            }
        }
        break;

    case WM_LBUTTONDOWN:
        {
            RECT xr;
            POINT p = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
            if (IsWindowEnabled(hwnd) && captureClearRect(hwnd, &xr) && PtInRect(&xr, p)) {
                memset(&captureBuffer, 0, sizeof(captureBuffer));
                modifiers = 0;
                virtKey = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        /* Auto-repeat (lParam bit 30) would push keycount past what the
        ** releases undo. */
        if (!(lParam & (1 << 30))) keycount++;
        keyboardUpdate();
        modifiers = keyboardGetModifiers();
        virtKey = wParam & 0xff;
        if (!virtualKeys[virtKey][0]) {
            virtKey = 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        /* Tabbing in delivers only the release: the dialog manager took the
        ** press, so committing here would bind the navigation key. */
        if (keycount == 0) {
            return 0;
        }
        keycount--;
        if (virtKey == 0) {
            virtKey = wParam & 0xff;
            if (!virtualKeys[virtKey][0]) {
                virtKey = 0;
            }
        }
        if (virtKey == 0) {
            keyboardUpdate();
            modifiers = keyboardGetModifiers();
        }
        if (keycount == 0 && virtKey != 0) {
            ShotcutHotkey h;
            h.type = HOTKEY_TYPE_KEYBOARD; h.mods = modifiers; h.key = virtKey;
            captureBufferToggle(&captureBuffer, h);
            modifiers = 0;
            virtKey = 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 154);
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT r, xr;
            HFONT hFont;
            BOOL dark = win32CommonIsDarkMode();
            int prevBkMode;
            COLORREF prevText;
            COLORREF normalFg = dark ? win32CommonDarkFg() : GetSysColor(COLOR_WINDOWTEXT);
            COLORREF conflictFg = RGB(220, 60, 60);
            COLORREF placeholderFg = dark ? RGB(140, 140, 140) : GetSysColor(COLOR_GRAYTEXT);
            int x = 2;
            int y = 1;
            int b;
            int painted = 0;
            int hasPending = (virtKey != 0);
            int textRightLimit;
            int hasClear;

            GetClientRect(hwnd, &r);
            hasClear = captureClearRect(hwnd, &xr);
            textRightLimit = hasClear ? xr.left - 3 : r.right - 2;

            /* msctls_hotkey32 misses WM_CTLCOLOR* so the dialog dark subclass
            ** cannot tint the background. Paint it directly to match. */
            FillRect(hdc, &r, dark ? win32CommonDarkBgBrush() : (HBRUSH)GetStockObject(WHITE_BRUSH));

            hFont = SelectObject(hdc, (HFONT)SendMessage(baseHwnd, WM_GETFONT, 0, 0));
            prevBkMode = SetBkMode(hdc, TRANSPARENT);
            prevText = SetTextColor(hdc, normalFg);

            /* The clear chip has no background of its own, so clip overflow. */
            SaveDC(hdc);
            IntersectClipRect(hdc, r.left, r.top, textRightLimit, r.bottom);

            for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
                SIZE sz;
                char* one;
                COLORREF fg;
                if (captureBuffer.slots[b].type == HOTKEY_TYPE_NONE) continue;
                one = shortcutsToString(captureBuffer.slots[b]);
                if (!one || !one[0]) continue;
                if (painted) {
                    const char* sep = ", ";
                    SetTextColor(hdc, normalFg);
                    TextOutU(hdc, x, y, (char*)sep, 2);
                    GetTextExtentPoint32A(hdc, sep, 2, &sz);
                    x += sz.cx;
                }
                fg = captureHotkeyConflicts(captureBuffer.slots[b], captureSelfIndex)
                        ? conflictFg : normalFg;
                SetTextColor(hdc, fg);
                TextOutU(hdc, x, y, one, (int)strlen(one));
                GetTextExtentPoint32A(hdc, one, (int)strlen(one), &sz);
                x += sz.cx;
                painted++;
            }

            if (hasPending) {
                ShotcutHotkey pending;
                char* buf;
                pending.type = HOTKEY_TYPE_KEYBOARD;
                pending.mods = modifiers;
                pending.key  = virtKey;
                buf = shortcutsToString(pending);
                if (buf[0]) {
                    SIZE sz;
                    if (painted) {
                        const char* sep = ", ";
                        SetTextColor(hdc, normalFg);
                        TextOutU(hdc, x, y, (char*)sep, 2);
                        GetTextExtentPoint32A(hdc, sep, 2, &sz);
                        x += sz.cx;
                    }
                    SetTextColor(hdc, placeholderFg);
                    TextOutU(hdc, x, y, buf, (int)strlen(buf));
                    painted++;
                }
            }

            RestoreDC(hdc, -1);

            if (!painted) {
                char* hint = langShortcutHotkeyHint();
                SetTextColor(hdc, placeholderFg);
                if (hint) TextOutU(hdc, x, y, hint, (int)strlen(hint));
            }

            /* Zero fill colours: the chip stays hollow over the field. */
            if (hasClear && IsWindowEnabled(hwnd) && (painted || hasPending)) {
                int side = xr.right - xr.left;
                COLORREF glyphCol = dark ? RGB(160, 160, 160) : RGB(130, 130, 130);
                win32PaintClearChip(hdc, &xr, 36, 100, side >= 28 ? 2 : 1,
                                    0, 0, glyphCol);
            }

            SetBkMode(hdc, prevBkMode);
            SetTextColor(hdc, prevText);
            SelectObject(hdc, hFont);
            EndPaint(hwnd, &ps);

            return 0;
        }
    }

    return CallWindowProc(baseHotkeyCtrlProc, hwnd, iMsg, wParam, lParam);
}

/* Entries are matched by prefix, so "recordVideoStart" would otherwise
** read "recordVideoStartAs"'s value.  Attaching the '=' makes it exact;
** the writer appends its own, so this form is for lookups only. */
static void shortcutLookupKey(char* out, int outLen, const char* keyName)
{
    _snprintf(out, outLen - 1, "%s=", keyName);
    out[outLen - 1] = 0;
}

/* Format: comma-separated 8-digit hex words, joystick ones followed by
** @"<device> : <button>". */
static void shortcutLoadHotkeySet(IniFile* iniFile, const char* keyName,
                                  ShotcutHotkeySet* set)
{
    char buffer[512];
    char lookup[128];
    const char* p;
    int b;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        set->slots[b].type = HOTKEY_TYPE_NONE;
        set->slots[b].mods = 0;
        set->slots[b].key  = 0;
    }
    shortcutLookupKey(lookup, sizeof(lookup), keyName);
    iniFileGetString(iniFile, "Shortcuts", lookup, "0", buffer, sizeof(buffer));
    p = buffer;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS && *p; ) {
        char slot[256];
        char* at;
        DWORD value = 0;
        ShotcutHotkey h;

        p = inputNextToken(p, slot, sizeof(slot));
        if (slot[0] == 0) {
            if (*p == 0) break;
            continue;
        }
        at = strchr(slot, '@');
        if (at) *at = 0;
        if (sscanf(slot, "%X", &value) != 1) continue;
        h = int2hotkey(&value);

        /* virtualKeys[] has 256 entries; a hand-edited file must not index
        ** past it. */
        if (h.type == HOTKEY_TYPE_KEYBOARD && h.key > 0xFF) continue;

        if (h.type == HOTKEY_TYPE_JOYSTICK) {
            int dik;
            /* No device name: an old bare number meant whichever pad
            ** enumerated first, so it is dropped rather than guessed at. */
            if (at == NULL) continue;
            dik = inputResolveDikName(at + 1);
            if (dik > 0) {
                if (!dikToHotkey(dik, &h)) continue;
            }
            else {
                /* Device absent: keep the name so the entry survives a
                ** save, with no DIK behind it so nothing can fire it. */
                h.mods = 0;
                h.key  = shortcutRememberName(at + 1);
                if (h.key == 0) continue;
            }
            /* Two spellings can name one control once resolved. */
            if (shortcutSetHasHotkey(set, h)) continue;
        }
        set->slots[b++] = h;
    }
}

/* ref is this entry as last read from the same file; unchanged entries
** are left alone. */
static void shortcutSaveHotkeySet(IniFile* iniFile, const char* keyName,
                                  const ShotcutHotkeySet* set,
                                  const ShotcutHotkeySet* ref)
{
    char buffer[512];
    int b;

    if (ref != NULL && 0 == memcmp(set, ref, sizeof(*set))) {
        /* Only an entry the file already has may be skipped: an absent one
        ** has to keep being appended in this function's fixed order, or a
        ** prefix of a later key would come to sit after it. */
        char probe[512];
        char lookup[128];
        shortcutLookupKey(lookup, sizeof(lookup), keyName);
        iniFileGetString(iniFile, "Shortcuts", lookup, "\x01",
                         probe, sizeof(probe));
        if (probe[0] != '\x01') {
            return;
        }
    }

    buffer[0] = 0;
    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
        char one[256];
        ShotcutHotkey h = set->slots[b];
        if (h.type == HOTKEY_TYPE_NONE) continue;
        if (h.type == HOTKEY_TYPE_JOYSTICK) {
            /* Zero the key below: the quoted name, not the slot,
            ** identifies device and button. */
            int dik = hotkeyToDik(h);
            const char* name = dik > 0 ? dik2str(dik)
                                       : shortcutRememberedName(h);
            if (name[0] == 0) continue;
            h.key = 0;
            sprintf(one, "%.8X@\"", hotkey2int(h));
            strncat(one, name, sizeof(one) - strlen(one) - 2);
            strcat(one, "\"");
        }
        else {
            sprintf(one, "%.8X", hotkey2int(h));
        }
        if (buffer[0]) strncat(buffer, ",", sizeof(buffer) - strlen(buffer) - 1);
        strncat(buffer, one, sizeof(buffer) - strlen(buffer) - 1);
    }
    if (buffer[0] == 0) sprintf(buffer, "%.8X", 0u);
    iniFileWriteString(iniFile, "Shortcuts", keyName, buffer);
}

#define LOAD_SHORTCUT(iniFile, hotkey) shortcutLoadHotkeySet(iniFile, #hotkey, &shortcuts->hotkey)
#define SAVE_SHORTCUT(iniFile, hotkey) \
    shortcutSaveHotkeySet(iniFile, #hotkey, &shortcuts->hotkey, \
                          saveRef ? &saveRef->hotkey : NULL)


static Shortcuts* loadShortcuts(char* profileName)
{
    char fileName[MAX_PATH];
    /* Zeroed, not malloc'd: a few sets have no LOAD_SHORTCUT line, and the
    ** cross-domain conflict counter walks the struct as a flat array. */
    Shortcuts* shortcuts = (Shortcuts*)calloc(1, sizeof(Shortcuts));
	IniFile *shortcutFile;

    sprintf(fileName, "%s/%s.shortcuts", profileDir, profileName);

    shortcutFile = iniFileOpen(fileName);

    LOAD_SHORTCUT(shortcutFile, msxAudioSwitch);
    LOAD_SHORTCUT(shortcutFile, spritesEnable);
    LOAD_SHORTCUT(shortcutFile, fdcTiming);
    LOAD_SHORTCUT(shortcutFile, hddSdBoost);
    LOAD_SHORTCUT(shortcutFile, noSpriteLimits);
    LOAD_SHORTCUT(shortcutFile, msxKeyboardQuirk);
    LOAD_SHORTCUT(shortcutFile, frontSwitch);
    LOAD_SHORTCUT(shortcutFile, pauseSwitch);
    LOAD_SHORTCUT(shortcutFile, quit);
    LOAD_SHORTCUT(shortcutFile, wavCapture);
    LOAD_SHORTCUT(shortcutFile, wavCaptureStartAs);
    LOAD_SHORTCUT(shortcutFile, videoCapLoad);
    LOAD_SHORTCUT(shortcutFile, videoCapPlay);
    LOAD_SHORTCUT(shortcutFile, videoCapRec);
    LOAD_SHORTCUT(shortcutFile, videoCapRecAs);
    LOAD_SHORTCUT(shortcutFile, videoCapStop);
    LOAD_SHORTCUT(shortcutFile, videoCapSave);
    LOAD_SHORTCUT(shortcutFile, recordVideoStart);
    LOAD_SHORTCUT(shortcutFile, recordVideoStartAs);
    LOAD_SHORTCUT(shortcutFile, recordVideoStop);
    LOAD_SHORTCUT(shortcutFile, recordVideoToggle);
    LOAD_SHORTCUT(shortcutFile, ym2413BackendCycle);
    LOAD_SHORTCUT(shortcutFile, y8950BackendCycle);
    LOAD_SHORTCUT(shortcutFile, screenCapture);
    LOAD_SHORTCUT(shortcutFile, screenCaptureAs);
    LOAD_SHORTCUT(shortcutFile, screenCaptureUnfilteredSmall);
    LOAD_SHORTCUT(shortcutFile, screenCaptureUnfilteredLarge);
    LOAD_SHORTCUT(shortcutFile, cpuStateLoad);
    LOAD_SHORTCUT(shortcutFile, cpuStateSave);
    LOAD_SHORTCUT(shortcutFile, cpuStateQuickLoad);
    LOAD_SHORTCUT(shortcutFile, cpuStateQuickSave);
    LOAD_SHORTCUT(shortcutFile, cpuStateQuickSaveUndo);
    
    LOAD_SHORTCUT(shortcutFile, cartInsert[0]);
    LOAD_SHORTCUT(shortcutFile, cartInsert[1]);
    LOAD_SHORTCUT(shortcutFile, cartSpecialMenu[0]);
    LOAD_SHORTCUT(shortcutFile, cartSpecialMenu[1]);
    LOAD_SHORTCUT(shortcutFile, cartRemove[0]);
    LOAD_SHORTCUT(shortcutFile, cartRemove[1]);
    LOAD_SHORTCUT(shortcutFile, cartAutoReset[0]);
    
    LOAD_SHORTCUT(shortcutFile, diskInsert[0]);
    LOAD_SHORTCUT(shortcutFile, diskInsert[1]);
    LOAD_SHORTCUT(shortcutFile, diskDirInsert[0]);
    LOAD_SHORTCUT(shortcutFile, diskDirInsert[1]);
    LOAD_SHORTCUT(shortcutFile, diskRemove[0]);
    LOAD_SHORTCUT(shortcutFile, diskRemove[1]);
    LOAD_SHORTCUT(shortcutFile, diskChange[0]);
    LOAD_SHORTCUT(shortcutFile, diskAutoReset[0]);

    LOAD_SHORTCUT(shortcutFile, casInsert);
    LOAD_SHORTCUT(shortcutFile, casRewind);
    LOAD_SHORTCUT(shortcutFile, casSetPos);
    LOAD_SHORTCUT(shortcutFile, casRemove);
    
    LOAD_SHORTCUT(shortcutFile, prnFormFeed);
    LOAD_SHORTCUT(shortcutFile, mouseLockToggle);
    LOAD_SHORTCUT(shortcutFile, emulationRunPause);
    LOAD_SHORTCUT(shortcutFile, emulationStop);
    LOAD_SHORTCUT(shortcutFile, emuSpeedFull);
    LOAD_SHORTCUT(shortcutFile, emuPlayReverse);
    LOAD_SHORTCUT(shortcutFile, emuSpeedToggle);
    LOAD_SHORTCUT(shortcutFile, emuSpeedNormal);
    LOAD_SHORTCUT(shortcutFile, emuSpeedInc);
    LOAD_SHORTCUT(shortcutFile, emuSpeedDec);
    LOAD_SHORTCUT(shortcutFile, windowSize1x);
    LOAD_SHORTCUT(shortcutFile, windowSize2x);
    LOAD_SHORTCUT(shortcutFile, windowSize3x);
    LOAD_SHORTCUT(shortcutFile, windowSize4x);
    LOAD_SHORTCUT(shortcutFile, windowSize5x);
    LOAD_SHORTCUT(shortcutFile, windowSize6x);
    LOAD_SHORTCUT(shortcutFile, windowSize7x);
    LOAD_SHORTCUT(shortcutFile, windowSize8x);
    LOAD_SHORTCUT(shortcutFile, windowSizeFullscreen);
    LOAD_SHORTCUT(shortcutFile, windowSizeMinimized);
    LOAD_SHORTCUT(shortcutFile, windowSizeFullscreenToggle);
    LOAD_SHORTCUT(shortcutFile, resetSoft);
    LOAD_SHORTCUT(shortcutFile, resetHard);
    LOAD_SHORTCUT(shortcutFile, resetClean);
    LOAD_SHORTCUT(shortcutFile, volumeIncrease);
    LOAD_SHORTCUT(shortcutFile, volumeDecrease);
    LOAD_SHORTCUT(shortcutFile, volumeMute);
    LOAD_SHORTCUT(shortcutFile, volumeStereo);
    LOAD_SHORTCUT(shortcutFile, themeSwitch);
    LOAD_SHORTCUT(shortcutFile, casToggleReadonly);
    LOAD_SHORTCUT(shortcutFile, casAutoRewind);
    LOAD_SHORTCUT(shortcutFile, casSave);
    LOAD_SHORTCUT(shortcutFile, propShowEmulation);
    LOAD_SHORTCUT(shortcutFile, propShowVideo);
    LOAD_SHORTCUT(shortcutFile, propShowAudio);
    LOAD_SHORTCUT(shortcutFile, propShowEffects);
    LOAD_SHORTCUT(shortcutFile, propShowSettings);
    LOAD_SHORTCUT(shortcutFile, propShowApearance);
    LOAD_SHORTCUT(shortcutFile, propShowPorts);
    LOAD_SHORTCUT(shortcutFile, optionsShowLanguage);
    LOAD_SHORTCUT(shortcutFile, toolsShowMachineEditor);
    LOAD_SHORTCUT(shortcutFile, toolsShowShorcutEditor);
    LOAD_SHORTCUT(shortcutFile, toolsShowKeyboardEditor);
    LOAD_SHORTCUT(shortcutFile, toolsShowMixer);
    LOAD_SHORTCUT(shortcutFile, toolsShowDebugger);
    LOAD_SHORTCUT(shortcutFile, toolsShowTrainer);
    LOAD_SHORTCUT(shortcutFile, helpShowHelp);
    LOAD_SHORTCUT(shortcutFile, helpShowAbout);

    iniFileClose(shortcutFile);

    return shortcuts;
}

static void saveShortcuts(char* profileName, Shortcuts* shortcuts)
{
    char fileName[MAX_PATH];
    IniFile *shortcutFile;
    int closeRc;
    const Shortcuts* saveRef;

    /* mkdir is a no-op if it exists; first save would otherwise silently fail. */
    mkdirU(profileDir);

    sprintf(fileName, "%s/%s.shortcuts", profileDir, profileName);

    shortcutFile = iniFileOpen(fileName);

    saveRef = (shortcutsRef != NULL &&
               0 == strcmp(profileName, shortcutsRefProfile) &&
               iniFileHasSection(shortcutFile, "Shortcuts")) ? shortcutsRef : NULL;

    SAVE_SHORTCUT(shortcutFile, msxAudioSwitch);
    SAVE_SHORTCUT(shortcutFile, spritesEnable);
    SAVE_SHORTCUT(shortcutFile, fdcTiming);
    SAVE_SHORTCUT(shortcutFile, hddSdBoost);
    SAVE_SHORTCUT(shortcutFile, noSpriteLimits);
    SAVE_SHORTCUT(shortcutFile, msxKeyboardQuirk);
    SAVE_SHORTCUT(shortcutFile, frontSwitch);
    SAVE_SHORTCUT(shortcutFile, pauseSwitch);
    SAVE_SHORTCUT(shortcutFile, quit);
    SAVE_SHORTCUT(shortcutFile, wavCapture);
    SAVE_SHORTCUT(shortcutFile, wavCaptureStartAs);
    SAVE_SHORTCUT(shortcutFile, videoCapLoad);
    SAVE_SHORTCUT(shortcutFile, videoCapPlay);
    SAVE_SHORTCUT(shortcutFile, videoCapRec);
    SAVE_SHORTCUT(shortcutFile, videoCapRecAs);
    SAVE_SHORTCUT(shortcutFile, videoCapStop);
    SAVE_SHORTCUT(shortcutFile, videoCapSave);
    SAVE_SHORTCUT(shortcutFile, recordVideoStart);
    SAVE_SHORTCUT(shortcutFile, recordVideoStartAs);
    SAVE_SHORTCUT(shortcutFile, recordVideoStop);
    SAVE_SHORTCUT(shortcutFile, recordVideoToggle);
    SAVE_SHORTCUT(shortcutFile, ym2413BackendCycle);
    SAVE_SHORTCUT(shortcutFile, y8950BackendCycle);
    SAVE_SHORTCUT(shortcutFile, screenCapture);
    SAVE_SHORTCUT(shortcutFile, screenCaptureAs);
    SAVE_SHORTCUT(shortcutFile, screenCaptureUnfilteredSmall);
    SAVE_SHORTCUT(shortcutFile, screenCaptureUnfilteredLarge);
    SAVE_SHORTCUT(shortcutFile, cpuStateLoad);
    SAVE_SHORTCUT(shortcutFile, cpuStateSave);
    SAVE_SHORTCUT(shortcutFile, cpuStateQuickLoad);
    SAVE_SHORTCUT(shortcutFile, cpuStateQuickSave);
    SAVE_SHORTCUT(shortcutFile, cpuStateQuickSaveUndo);

    SAVE_SHORTCUT(shortcutFile, cartInsert[0]);
    SAVE_SHORTCUT(shortcutFile, cartInsert[1]);
    SAVE_SHORTCUT(shortcutFile, cartSpecialMenu[0]);
    SAVE_SHORTCUT(shortcutFile, cartSpecialMenu[1]);
    SAVE_SHORTCUT(shortcutFile, cartRemove[0]);
    SAVE_SHORTCUT(shortcutFile, cartRemove[1]);
    SAVE_SHORTCUT(shortcutFile, cartAutoReset[0]);
    
    SAVE_SHORTCUT(shortcutFile, diskInsert[0]);
    SAVE_SHORTCUT(shortcutFile, diskInsert[1]);
    SAVE_SHORTCUT(shortcutFile, diskDirInsert[0]);
    SAVE_SHORTCUT(shortcutFile, diskDirInsert[1]);
    SAVE_SHORTCUT(shortcutFile, diskRemove[0]);
    SAVE_SHORTCUT(shortcutFile, diskRemove[1]); 
    SAVE_SHORTCUT(shortcutFile, diskChange[0]);   
    SAVE_SHORTCUT(shortcutFile, diskAutoReset[0]);

    SAVE_SHORTCUT(shortcutFile, casInsert);
    SAVE_SHORTCUT(shortcutFile, casRewind);
    SAVE_SHORTCUT(shortcutFile, casSetPos);
    SAVE_SHORTCUT(shortcutFile, casToggleReadonly);
    SAVE_SHORTCUT(shortcutFile, casAutoRewind);
    SAVE_SHORTCUT(shortcutFile, casSave);
    SAVE_SHORTCUT(shortcutFile, casRemove);

    SAVE_SHORTCUT(shortcutFile, prnFormFeed);
    SAVE_SHORTCUT(shortcutFile, mouseLockToggle);
    SAVE_SHORTCUT(shortcutFile, emulationRunPause);
    SAVE_SHORTCUT(shortcutFile, emulationStop);
    SAVE_SHORTCUT(shortcutFile, emuSpeedFull);
    SAVE_SHORTCUT(shortcutFile, emuPlayReverse);
    SAVE_SHORTCUT(shortcutFile, emuSpeedToggle);
    SAVE_SHORTCUT(shortcutFile, emuSpeedNormal);
    SAVE_SHORTCUT(shortcutFile, emuSpeedInc);
    SAVE_SHORTCUT(shortcutFile, emuSpeedDec);
    SAVE_SHORTCUT(shortcutFile, windowSize1x);
    SAVE_SHORTCUT(shortcutFile, windowSize2x);
    SAVE_SHORTCUT(shortcutFile, windowSize3x);
    SAVE_SHORTCUT(shortcutFile, windowSize4x);
    SAVE_SHORTCUT(shortcutFile, windowSize5x);
    SAVE_SHORTCUT(shortcutFile, windowSize6x);
    SAVE_SHORTCUT(shortcutFile, windowSize7x);
    SAVE_SHORTCUT(shortcutFile, windowSize8x);
    SAVE_SHORTCUT(shortcutFile, windowSizeFullscreen);
    SAVE_SHORTCUT(shortcutFile, windowSizeMinimized);
    SAVE_SHORTCUT(shortcutFile, windowSizeFullscreenToggle);
    SAVE_SHORTCUT(shortcutFile, resetSoft);
    SAVE_SHORTCUT(shortcutFile, resetHard);
    SAVE_SHORTCUT(shortcutFile, resetClean);
    SAVE_SHORTCUT(shortcutFile, volumeIncrease);
    SAVE_SHORTCUT(shortcutFile, volumeDecrease);
    SAVE_SHORTCUT(shortcutFile, volumeMute);
    SAVE_SHORTCUT(shortcutFile, volumeStereo);
    SAVE_SHORTCUT(shortcutFile, themeSwitch);
    SAVE_SHORTCUT(shortcutFile, propShowEmulation);
    SAVE_SHORTCUT(shortcutFile, propShowVideo);
    SAVE_SHORTCUT(shortcutFile, propShowAudio);
    SAVE_SHORTCUT(shortcutFile, propShowEffects);
    SAVE_SHORTCUT(shortcutFile, propShowSettings);
    SAVE_SHORTCUT(shortcutFile, propShowApearance);
    SAVE_SHORTCUT(shortcutFile, propShowPorts);
    SAVE_SHORTCUT(shortcutFile, optionsShowLanguage);
    SAVE_SHORTCUT(shortcutFile, toolsShowMachineEditor);
    SAVE_SHORTCUT(shortcutFile, toolsShowShorcutEditor);
    SAVE_SHORTCUT(shortcutFile, toolsShowKeyboardEditor);
    SAVE_SHORTCUT(shortcutFile, toolsShowMixer);
    SAVE_SHORTCUT(shortcutFile, toolsShowDebugger);
    SAVE_SHORTCUT(shortcutFile, toolsShowTrainer);
    SAVE_SHORTCUT(shortcutFile, helpShowHelp);
    SAVE_SHORTCUT(shortcutFile, helpShowAbout);

    closeRc = iniFileClose(shortcutFile);
    if (!closeRc) {
        char msg[MAX_PATH + 64];
        sprintf(msg, "Failed to write shortcut profile:\n%s", fileName);
        MessageBoxU(NULL, msg, "blueMSX+", MB_OK | MB_ICONERROR);
    }
    else if (shortcutsRef != NULL) {
        /* Only after the write landed: advancing on failure would make the
        ** retry find nothing to write and report success. */
        memcpy(shortcutsRef, shortcuts, sizeof(Shortcuts));
        _snprintf(shortcutsRefProfile, sizeof(shortcutsRefProfile) - 1, "%s", profileName);
        shortcutsRefProfile[sizeof(shortcutsRefProfile) - 1] = 0;
    }
}

static void addShortcutEntry(HWND hwnd, int entry, char* description, const ShotcutHotkeySet* set) {
    /* Listview is in Unicode mode (LVM_SETUNICODEFORMAT). Source strings are
       UTF-8 (/utf-8 build flag). */
    wchar_t wbuf[512];
    LVITEMW lviw = {0};

    lviw.mask    = LVIF_TEXT;
    lviw.iItem   = entry;
    lviw.pszText = wbuf;

    Utf8ToWide(description, wbuf, _countof(wbuf));
    SendMessageW(hwnd, LVM_INSERTITEMW, 0, (LPARAM)&lviw);

    lviw.iSubItem++;
    Utf8ToWide(shortcutsSetToString(set), wbuf, _countof(wbuf));
    SendMessageW(hwnd, LVM_SETITEMW, 0, (LPARAM)&lviw);
}

/* One list: the dialog listview and the conflict describers both walk it. */
typedef void (*ShortcutActionVisitor)(void* ref, const char* name,
                                      ShotcutHotkeySet* set);

#define ADD_SHORTCUT(hotkey, destcription)                                      \
do {                                                                            \
    visit(ref, destcription, &s->hotkey);                                       \
} while(0)

#define ADD_SHORTCUTSEPARATOR()                                                 \
do {                                                                            \
    visit(ref, "", NULL);                                                       \
} while(0)

static void shortcutsForEachAction(Shortcuts* s, ShortcutActionVisitor visit, void* ref)
{
    ADD_SHORTCUT(cartInsert[0], langShortcutCartInsert1());
    ADD_SHORTCUT(cartRemove[0], langShortcutCartRemove1());
    ADD_SHORTCUT(cartSpecialMenu[0], langShortcutCartSpecialMenu1());
    ADD_SHORTCUT(cartInsert[1], langShortcutCartInsert2());
    ADD_SHORTCUT(cartRemove[1], langShortcutCartRemove2());
    ADD_SHORTCUT(cartSpecialMenu[1], langShortcutCartSpecialMenu2());
    ADD_SHORTCUT(cartAutoReset[0], langShortcutCartAutoReset());

    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(diskInsert[0], langShortcutDiskInsertA());
    ADD_SHORTCUT(diskDirInsert[0], langShortcutDiskDirInsertA());
    ADD_SHORTCUT(diskRemove[0], langShortcutDiskRemoveA());
    ADD_SHORTCUT(diskChange[0], langShortcutDiskChangeA());
    ADD_SHORTCUT(diskAutoReset[0], langShortcutDiskAutoResetA());
    ADD_SHORTCUT(diskInsert[1], langShortcutDiskInsertB());
    ADD_SHORTCUT(diskDirInsert[1], langShortcutDiskDirInsertB());
    ADD_SHORTCUT(diskRemove[1], langShortcutDiskRemoveB());

    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(casInsert, langShortcutCasInsert());
    ADD_SHORTCUT(casRemove, langShortcutCasEject());
    ADD_SHORTCUT(casAutoRewind, langShortcutCasAutorewind());
    ADD_SHORTCUT(casToggleReadonly, langShortcutCasReadOnly());
    ADD_SHORTCUT(casSetPos, langShortcutCasSetPosition());
    ADD_SHORTCUT(casRewind, langShortcutCasRewind());
    ADD_SHORTCUT(casSave, langShortcutCasSave());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(prnFormFeed, langShortcutPrnFormFeed());
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(cpuStateLoad, langShortcutCpuStateLoad());
    ADD_SHORTCUT(cpuStateSave, langShortcutCpuStateSave());
    ADD_SHORTCUT(cpuStateQuickLoad, langShortcutCpuStateQload());
    ADD_SHORTCUT(cpuStateQuickSave, langShortcutCpuStateQsave());
    ADD_SHORTCUT(cpuStateQuickSaveUndo, "Quick save CPU state UNDO");
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(wavCapture, langShortcutAudioCapture());
    ADD_SHORTCUT(wavCaptureStartAs, langShortcutAudioCaptureAs());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(videoCapLoad, langShortcutVideoLoad());
    ADD_SHORTCUT(videoCapPlay, langShortcutVideoPlay());
    ADD_SHORTCUT(videoCapRec,  langShortcutVideoRecord());
    ADD_SHORTCUT(videoCapRecAs, langShortcutVideoRecordAs());
    ADD_SHORTCUT(videoCapStop, langShortcutVideoStop());
    ADD_SHORTCUT(videoCapSave, langShortcutVideoRender());
    ADD_SHORTCUTSEPARATOR();
    ADD_SHORTCUT(recordVideoStart,  langShortcutRecordVideoStart());
    ADD_SHORTCUT(recordVideoStartAs, langShortcutRecordVideoStartAs());
    ADD_SHORTCUT(recordVideoStop,   langShortcutRecordVideoStop());
    ADD_SHORTCUT(recordVideoToggle, langShortcutRecordVideoToggle());

    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(ym2413BackendCycle, langShortcutYm2413BackendCycle());
    ADD_SHORTCUT(y8950BackendCycle,  langShortcutY8950BackendCycle());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(screenCapture, langShortcutScreenshotOrig());
    ADD_SHORTCUT(screenCaptureAs, langShortcutScreenshotAs());
    ADD_SHORTCUT(screenCaptureUnfilteredSmall, langShortcutScreenshotSmall());
    ADD_SHORTCUT(screenCaptureUnfilteredLarge, langShortcutScreenshotLarge());
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(quit, langShortcutQuit());
    ADD_SHORTCUT(emulationRunPause, langShortcutRunPause());
    ADD_SHORTCUT(emulationStop, langShortcutStop());
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(resetHard, langShortcutResetHard());
    ADD_SHORTCUT(resetSoft, langShortcutResetSoft());
    ADD_SHORTCUT(resetClean, langShortcutResetClean());
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(windowSize1x, langShortcutSize1x());
    ADD_SHORTCUT(windowSize2x, langShortcutSize2x());
    ADD_SHORTCUT(windowSize3x, langShortcutSize3x());
    ADD_SHORTCUT(windowSize4x, langShortcutSize4x());
    ADD_SHORTCUT(windowSize5x, langShortcutSize5x());
    ADD_SHORTCUT(windowSize6x, langShortcutSize6x());
    ADD_SHORTCUT(windowSize7x, langShortcutSize7x());
    ADD_SHORTCUT(windowSize8x, langShortcutSize8x());
    ADD_SHORTCUT(windowSizeFullscreen, langShortcutSizeFullscreen());
    ADD_SHORTCUT(windowSizeMinimized, langShortcutSizeMinimized());
    ADD_SHORTCUT(windowSizeFullscreenToggle, langShortcutToggleFullscren());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(volumeIncrease, langShortcutVolumeIncrease());
    ADD_SHORTCUT(volumeDecrease, langShortcutVolumeDecrease());
    ADD_SHORTCUT(volumeMute, langShortcutVolumeMute());
    ADD_SHORTCUT(volumeStereo, langShortcutVolumeStereo());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(msxAudioSwitch, langShortcutSwitchMsxAudio());
    ADD_SHORTCUT(frontSwitch, langShortcutSwitchFront());
    ADD_SHORTCUT(pauseSwitch, langShortcutSwitchPause());
    ADD_SHORTCUT(mouseLockToggle, langShortcutToggleMouseLock());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(emuSpeedFull, langShortcutEmuSpeedMax());
    ADD_SHORTCUT(emuSpeedToggle, langShortcutEmuSpeedMaxToggle());
    ADD_SHORTCUT(emuSpeedNormal, langShortcutEmuSpeedNormal());
    ADD_SHORTCUT(emuSpeedInc, langShortcutEmuSpeedInc());
    ADD_SHORTCUT(emuSpeedDec, langShortcutEmuSpeedDec());
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(emuPlayReverse, langShortcutEmuPlayReverse());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(themeSwitch, langShortcutThemeSwitch());
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(propShowEmulation, langShortcutShowEmuProp());
    ADD_SHORTCUT(propShowVideo, langShortcutShowVideoProp());
    ADD_SHORTCUT(propShowAudio, langShortcutShowAudioProp());
    ADD_SHORTCUT(propShowEffects, langShortcutShowEffectsProp());
    ADD_SHORTCUT(propShowSettings, langShortcutShowFiles());
    ADD_SHORTCUT(propShowApearance, langShortcutShowSettProp());
    ADD_SHORTCUT(propShowPorts, langShortcutShowPorts());
    ADD_SHORTCUT(optionsShowLanguage, langShortcutShowLanguage());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(toolsShowMachineEditor, langShortcutShowMachines());
    ADD_SHORTCUT(toolsShowShorcutEditor, langShortcutShowShortcuts());
    ADD_SHORTCUT(toolsShowKeyboardEditor, langShortcutShowKeyboard());
    ADD_SHORTCUT(toolsShowMixer, langShortcutShowMixer());
    ADD_SHORTCUT(toolsShowDebugger, langShortcutShowDebugger());
    ADD_SHORTCUT(toolsShowTrainer, langShortcutShowTrainer());
    
    ADD_SHORTCUTSEPARATOR();

    ADD_SHORTCUT(helpShowHelp, langShortcutShowHelp());
    ADD_SHORTCUT(helpShowAbout, langShortcutShowAbout());
    
    ADD_SHORTCUTSEPARATOR();
    
    ADD_SHORTCUT(spritesEnable, langShortcutToggleSpriteEnable());
    ADD_SHORTCUT(fdcTiming,     langShortcutToggleFdcTiming());
    ADD_SHORTCUT(hddSdBoost,    langShortcutToggleHddSdBoost());
    ADD_SHORTCUT(noSpriteLimits,     langShortcutToggleNoSpriteLimits());
    ADD_SHORTCUT(msxKeyboardQuirk,     langShortcutEnableMsxKeyboardQuirk());
}

struct ShortcutEntryAddCtx { HWND hwnd; int entry; };

/* Row index and hotkeyList[] index are the same, so one bound covers both. */
static void shortcutEntryAddVisitor(void* ref, const char* name, ShotcutHotkeySet* set)
{
    struct ShortcutEntryAddCtx* ctx = (struct ShortcutEntryAddCtx*)ref;
    if (ctx->entry >= (int)(sizeof(hotkeyList) / sizeof(hotkeyList[0]))) return;
    hotkeyList[ctx->entry] = set;
    addShortcutEntry(ctx->hwnd, ctx->entry++, (char*)name, set);
}

static void updateShortcutEntries(HWND hDlg)
{
    struct ShortcutEntryAddCtx ctx;
    ctx.hwnd  = GetDlgItem(hDlg, IDC_SCUTLIST);
    ctx.entry = 0;

    ListView_DeleteAllItems(ctx.hwnd);
    memset(hotkeyList, 0, sizeof(hotkeyList));
    shortcutsForEachAction(shortcuts, shortcutEntryAddVisitor, &ctx);

    hotkeyListValid = 1;
}

struct ShortcutNameByIndexCtx { int want; int index; char* out; int outLen; int found; };

static void shortcutNameByIndexVisitor(void* ref, const char* name, ShotcutHotkeySet* set)
{
    struct ShortcutNameByIndexCtx* ctx = (struct ShortcutNameByIndexCtx*)ref;
    if (ctx->index++ != ctx->want) return;
    strncpy(ctx->out, name, ctx->outLen - 1);
    ctx->out[ctx->outLen - 1] = 0;
    ctx->found = name[0] != 0;
}

static int shortcutsGetActionName(int index, char* out, int outLen)
{
    struct ShortcutNameByIndexCtx ctx;
    if (outLen <= 0) return 0;
    out[0] = 0;
    if (shortcuts == NULL) return 0;
    ctx.want   = index;
    ctx.index  = 0;
    ctx.out    = out;
    ctx.outLen = outLen;
    ctx.found  = 0;
    shortcutsForEachAction(shortcuts, shortcutNameByIndexVisitor, &ctx);
    return ctx.found;
}

struct ShortcutDikNamesCtx { ShotcutHotkey probe; char* out; int outLen; int count; };

static void shortcutDikNamesVisitor(void* ref, const char* name, ShotcutHotkeySet* set)
{
    struct ShortcutDikNamesCtx* ctx = (struct ShortcutDikNamesCtx*)ref;
    if (set == NULL || name[0] == 0) return;
    if (!shortcutSetMatchesDik(set, ctx->probe)) return;
    if (ctx->out[0]) strncat(ctx->out, ", ", ctx->outLen - strlen(ctx->out) - 1);
    strncat(ctx->out, name, ctx->outLen - strlen(ctx->out) - 1);
    ctx->count++;
}

int shortcutsDescribeActionsUsingDik(int dik, char* out, int outLen)
{
    struct ShortcutDikNamesCtx ctx;
    Shortcuts* src = hotkeyListValid ? shortcuts : liveShortcuts;

    if (outLen <= 0) return 0;
    out[0] = 0;
    if (src == NULL || !dikToHotkey(dik, &ctx.probe)) return 0;
    ctx.out    = out;
    ctx.outLen = outLen;
    ctx.count  = 0;
    shortcutsForEachAction(src, shortcutDikNamesVisitor, &ctx);
    return ctx.count;
}



static void updateShortcutEntry(HWND hwnd, int entry, const ShotcutHotkeySet* set) {
    wchar_t wbuf[512];
    LVITEMW lviw = {0};

    lviw.mask       = LVIF_TEXT;
    lviw.iItem      = entry;
    lviw.pszText    = wbuf;
    lviw.cchTextMax = _countof(wbuf);
    lviw.iSubItem   = 1;

    Utf8ToWide(shortcutsSetToString(set), wbuf, _countof(wbuf));
    SendMessageW(hwnd, LVM_SETITEMW, 0, (LPARAM)&lviw);
}

/* Overlap with other actions is deliberate: conflicts are highlighted,
** not unbound. */
static void updateHotkeys(HWND hDlg, int index, const ShotcutHotkeySet* newSet)
{
    HWND hwnd = GetDlgItem(hDlg, IDC_SCUTLIST);
    int b;

    if (index < 0 || hotkeyList[index] == NULL) return;

    if (newSet) {
        for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
            hotkeyList[index]->slots[b] = newSet->slots[b];
        }
    }
    else {
        memset(hotkeyList[index]->slots, 0, sizeof(hotkeyList[index]->slots));
    }
    updateShortcutEntry(hwnd, index, hotkeyList[index]);
}

/* Reads the editable combobox text as the save target.  Returns 1 when
** the text is a usable profile name (non-empty, not the placeholder). */
static int shortcutsGetEditName(HWND hDlg, char* out, int outCap)
{
    char buf[128];
    GetDlgItemTextU(hDlg, IDC_SCUTCONFIGS, buf, (int)sizeof(buf));
    if (buf[0] == 0) return 0;
    if (strcmp(buf, langShortcutNewProfile()) == 0) return 0;
    if (out) {
        strncpy(out, buf, outCap);
        out[outCap - 1] = 0;
    }
    return 1;
}

static int shortcutsSaveEnabled(HWND hDlg)
{
    char effective[128];
    if (!shortcutsGetEditName(hDlg, effective, (int)sizeof(effective))) return 0;
    /* Different name from the loaded profile -> Save creates / overwrites a
    ** different file. Always actionable. */
    if (strcmp(effective, shortcutProfile) != 0) return 1;
    /* Same name -> only actionable when shortcuts have been modified. */
    return memcmp(shortcutsRef, shortcuts, sizeof(Shortcuts)) != 0;
}

static updateShortcutsList(HWND hDlg) 
{
    char** profileList = getProfileList();
    int index = 0;
    int indexMod = 0;

    while (CB_ERR != SendDlgItemMessage(hDlg, IDC_SCUTCONFIGS, CB_DELETESTRING, 0, 0));

    if (0 == strcmp(shortcutProfile, langShortcutNewProfile())) {
        ComboAddStringU(GetDlgItem(hDlg, IDC_SCUTCONFIGS), langShortcutNewProfile());
        SendDlgItemMessage(hDlg, IDC_SCUTCONFIGS, CB_SETCURSEL, index, 0);
        indexMod = 1;
    }
    
    while (profileList[index]) {
        ComboAddStringU(GetDlgItem(hDlg, IDC_SCUTCONFIGS), profileList[index]);
        
        if (index + indexMod == 0 || 0 == strcmp(profileList[index], shortcutProfile)) {
            SendDlgItemMessage(hDlg, IDC_SCUTCONFIGS, CB_SETCURSEL, index + indexMod, 0);
        }
        index++;
    }
}

static BOOL_DLG_RET CALLBACK shortcutsProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static int currIndex;
    static HWND hwnd;

    switch (iMsg) {
    case WM_INITDIALOG:
        /* Pick up any controller hot-plugged since the last time the
        ** dialog was opened so the assignment list reflects the live
        ** input device set. */
        inputRefreshDevicesIfDirty();
        SetWindowTextU(hDlg, langShortcutConfigTitle());
        SetWindowTextU(GetDlgItem(hDlg, IDC_OK), langDlgOK());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SAVE), langDlgSave());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SAVEAS), langDlgSaveAs());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SCUTASSIGN), langShortcutAssign());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SCUTHOTKEYTEXT), langShortcutPressText());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SCUTCONFIGTEXT), langShortcutScheme());
        {
            char buffer[32];
            
//            inputReset(hDlg);
            baseHwnd = hDlg;
            baseHotkeyCtrlProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hDlg, IDC_SCUTHOTKEY), GWLP_WNDPROC, (LONG_PTR)hotkeyCtrlProc);
            SendDlgItemMessage(hDlg, IDC_SCUTHOTKEY, WM_INITIALIZE, 0, 0);

            currIndex = -1;
            hwnd = GetDlgItem(hDlg, IDC_SCUTLIST);

            ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
            
            /* Source strings are UTF-8 (/utf-8 build flag); use Unicode-mode
               ListView + explicit UTF-8 -> UTF-16 conversion. */
            SendMessageW(hwnd, LVM_SETUNICODEFORMAT, TRUE, 0);
            {
                LVCOLUMNW lvcw = {0};
                wchar_t wbuf[64];
                lvcw.mask    = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
                lvcw.fmt     = LVCFMT_LEFT;
                lvcw.pszText = wbuf;

                int totalW;
                {
                    RECT lr;
                    GetClientRect(hwnd, &lr);
                    totalW = lr.right - lr.left - GetSystemMetrics(SM_CXVSCROLL);
                    if (totalW < 200) totalW = 200;
                }
                int col0W = totalW / 2;
                int col1W = totalW - col0W;

                sprintf(buffer, langShortcutKey());
                Utf8ToWide(buffer, wbuf, _countof(wbuf));
                lvcw.cx = col0W;
                SendMessageW(hwnd, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvcw);

                sprintf(buffer, langShortcutDescription());
                Utf8ToWide(buffer, wbuf, _countof(wbuf));
                lvcw.cx = col1W;
                SendMessageW(hwnd, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvcw);
            }

            updateShortcutsList(hDlg);
            updateShortcutEntries(hDlg);
            EnableWindow(GetDlgItem(hDlg, IDC_SCUTHOTKEY), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_SCUTASSIGN), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_SAVE), shortcutsSaveEnabled(hDlg));
        }
        win32CommonApplyDark(hDlg);
        win32CommonCenterOnOwner(hDlg);
        return FALSE;

    case WM_ACTIVATE:
        keyboardSetFocus(4, LOWORD(wParam) != WA_INACTIVE);
        if (LOWORD(wParam) != WA_INACTIVE) {
            inputReset(hwnd);
        }
        break;

    case WM_DESTROY:
        keyboardSetFocus(4, 0);
        /* Callbacks stay registered; they fall back to liveShortcuts. */
        hotkeyListValid = 0;
        memset(hotkeyList, 0, sizeof(hotkeyList));
        break;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDC_SCUTCONFIGS:
            {
                static volatile int isCheckingConfigs = 0;

                if (isCheckingConfigs == 0 && HIWORD(wParam) == CBN_SELCHANGE) {
                    char profileSel[64];
                    int idx;
                    int rv;

                    isCheckingConfigs = 1;

                    idx = (int)SendMessage(GetDlgItem(hDlg, IDC_SCUTCONFIGS), CB_GETCURSEL, 0, 0);
                    rv = (int)SendMessage(GetDlgItem(hDlg, IDC_SCUTCONFIGS), CB_GETLBTEXT, idx, (LPARAM)profileSel);
                
                    if (rv != CB_ERR) {
                        if (strcmp(profileSel, shortcutProfile)) {
                            int modified = memcmp(shortcutsRef, shortcuts, sizeof(Shortcuts));

                            if (modified) {
                                modified = !DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SAVEDLG), hDlg, discardProc);
                            }

                            if (modified == 0) {
                                strcpy(shortcutProfile, profileSel);
                                free(shortcuts);
                                shortcuts = loadShortcuts(shortcutProfile);
                                memcpy(shortcutsRef, shortcuts, sizeof(Shortcuts));
                                _snprintf(shortcutsRefProfile, sizeof(shortcutsRefProfile) - 1,
                                          "%s", shortcutProfile);
                                shortcutsRefProfile[sizeof(shortcutsRefProfile) - 1] = 0;
                                updateShortcutEntries(hDlg);
                                EnableWindow(GetDlgItem(hDlg, IDC_SCUTHOTKEY), FALSE);
                                EnableWindow(GetDlgItem(hDlg, IDC_SCUTASSIGN), FALSE);
                            }

                            updateShortcutsList(hDlg);
                        }
                    }

                    EnableWindow(GetDlgItem(hDlg, IDC_SAVE), shortcutsSaveEnabled(hDlg));

                    isCheckingConfigs = 0;
                }
                else if (HIWORD(wParam) == CBN_EDITCHANGE) {
                    /* User typed a name into the editable combobox while on the
                    ** new-profile slot -- update Save state so it can fire
                    ** without going through the Save As dialog. */
                    EnableWindow(GetDlgItem(hDlg, IDC_SAVE), shortcutsSaveEnabled(hDlg));
                }

                return TRUE;
            }

        case IDC_SAVE:
            {
                char effective[128];
                int rv;
                if (!shortcutsGetEditName(hDlg, effective, (int)sizeof(effective))) {
                    return TRUE;  /* button should not be enabled in this state */
                }

                if (strcmp(effective, shortcutProfile) != 0) {
                    /* Edit-text differs from loaded profile: Save-As path. */
                    char fileName[MAX_PATH];
                    char savedProfile[128];
                    FILE* file;
                    sprintf(fileName, "%s/%s.shortcuts", profileDir, effective);
                    file = fopen(fileName, "r");
                    strcpy(savedProfile, shortcutProfile);
                    strcpy(shortcutProfile, effective);
                    s_savePromptIsCreate = (file == NULL);
                    if (file != NULL) fclose(file);
                    rv = (int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SAVEDLG), hDlg, saveProc);
                    if (rv) {
                        saveShortcuts(shortcutProfile, shortcuts);
                        updateShortcutsList(hDlg);
                        updateShortcutEntries(hDlg);
                    }
                    else {
                        strcpy(shortcutProfile, savedProfile);
                    }
                }
                else {
                    s_savePromptIsCreate = 0;
                    rv = (int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SAVEDLG), hDlg, saveProc);
                    if (rv) {
                        saveShortcuts(shortcutProfile, shortcuts);
                    }
                }

                EnableWindow(GetDlgItem(hDlg, IDC_SAVE), shortcutsSaveEnabled(hDlg));
            }
            return TRUE;

        case IDC_SAVEAS:
            {
                int rv = (int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CONF_SAVEAS), hDlg, saveAsProc);
                if (rv) {
                    FILE* file;
                    char fileName[MAX_PATH];

                    sprintf(fileName, "%s/%s.shortcuts", profileDir, tmpShortcutProfile);
                    file = fopen(fileName, "r");
                    if (file != NULL) {
                        rv = (int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SAVEDLG), hDlg, saveProc);
                        fclose(file);
                    }

                    if (rv) {
                        strcpy(shortcutProfile, tmpShortcutProfile);
                        saveShortcuts(shortcutProfile, shortcuts);
                        updateShortcutsList(hDlg);
                        updateShortcutEntries(hDlg);
                    }
                }
                EnableWindow(GetDlgItem(hDlg, IDC_SAVE), shortcutsSaveEnabled(hDlg));
            }
            return TRUE;

        case IDC_SCUTASSIGN:
            if (currIndex >= 0) {
                ShotcutHotkeySet set;
                memset(&set, 0, sizeof(set));
                SendDlgItemMessage(hDlg, IDC_SCUTHOTKEY, WM_GET_HOTKEYSET, 0, (LPARAM)&set);
                updateHotkeys(hDlg, currIndex, &set);
                SendDlgItemMessage(hDlg, IDC_SCUTHOTKEY, WM_SET_HOTKEYSET,
                    (WPARAM)currIndex, (LPARAM)hotkeyList[currIndex]);
                ListView_SetItemState(hwnd, currIndex, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
                /* Any row's conflict colour can change, so repaint the
                ** whole list. */
                InvalidateRect(hwnd, NULL, TRUE);
            }
            EnableWindow(GetDlgItem(hDlg, IDC_SAVE), shortcutsSaveEnabled(hDlg));
            return TRUE;

        case IDC_OK:
            {
                int rv = 1;
                if (memcmp(shortcutsRef, shortcuts, sizeof(Shortcuts))) {
                    rv = (int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SAVEDLG), hDlg, closeProc);
                }
                if (rv) {
                    EndDialog(hDlg, TRUE);
                }
            }
            return TRUE;
        }
        return FALSE;

    case WM_NOTIFY:
        switch (wParam) {
        case IDC_SCUTLIST:
            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMCHANGED) {
                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);

                    if (currIndex != index && index != -1) {
                        EnableWindow(GetDlgItem(hDlg, IDC_SCUTHOTKEY), TRUE);
                        EnableWindow(GetDlgItem(hDlg, IDC_SCUTASSIGN), TRUE);
                        SendDlgItemMessage(hDlg, IDC_SCUTHOTKEY, WM_SET_HOTKEYSET,
                            (WPARAM)index, (LPARAM)hotkeyList[index]);
                    }
                    currIndex = index;
                }
                else {
                    if (currIndex != -1) {
                        SendDlgItemMessage(hDlg, IDC_SCUTHOTKEY, WM_CLEAR_HOTKEYSET, 0, 0);
                        EnableWindow(GetDlgItem(hDlg, IDC_SCUTHOTKEY), FALSE);
                        EnableWindow(GetDlgItem(hDlg, IDC_SCUTASSIGN), FALSE);
                    }
                    currIndex = -1;
                }
            }
            else if (((NMHDR FAR *)lParam)->code == LVN_GETINFOTIPW) {
                NMLVGETINFOTIPW* tip = (NMLVGETINFOTIPW*)lParam;
                int idx = tip->iItem;
                if (idx >= 0 && idx < (int)(sizeof(hotkeyList)/sizeof(void*))
                    && hotkeyList[idx] && tip->pszText && tip->cchTextMax > 32) {
                    /* Same "<binding> > <targets>" layout as the Controller
                    ** and Keyboard tips. */
                    char body[512];
                    wchar_t out[600];
                    int b, i;
                    body[0] = 0;
                    for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
                        ShotcutHotkey h = hotkeyList[idx]->slots[b];
                        char targets[320];
                        int dik;
                        if (h.type == HOTKEY_TYPE_NONE) continue;
                        targets[0] = 0;
                        for (i = 0; i < (int)(sizeof(hotkeyList)/sizeof(void*)); i++) {
                            char rowText[128];
                            if (i == idx || !hotkeyList[i]) continue;
                            if (!shortcutSetMatchesDik(hotkeyList[i], h)) continue;
                            if (!shortcutsGetActionName(i, rowText, sizeof(rowText))) continue;
                            if (targets[0]) strncat(targets, ", ", sizeof(targets) - strlen(targets) - 1);
                            strncat(targets, rowText, sizeof(targets) - strlen(targets) - 1);
                        }
                        dik = hotkeyToDik(h);
                        if (dik > 0) {
                            char one[192];
                            if (bindingsDescribeTargetsForDik(dik, -1, -1, one, sizeof(one)) > 0) {
                                if (targets[0]) strncat(targets, ", ", sizeof(targets) - strlen(targets) - 1);
                                strncat(targets, one, sizeof(targets) - strlen(targets) - 1);
                            }
                        }
                        if (targets[0] == 0) continue;
                        if (shortcutsToString(h)[0] == 0) continue;
                        if (body[0] == 0) {
                            strncat(body, langShortcutTooltipAlsoBound(), sizeof(body) - strlen(body) - 1);
                        }
                        strncat(body, "\n", sizeof(body) - strlen(body) - 1);
                        strncat(body, shortcutsToString(h), sizeof(body) - strlen(body) - 1);
                        strncat(body, " > ", sizeof(body) - strlen(body) - 1);
                        strncat(body, targets, sizeof(body) - strlen(body) - 1);
                    }
                    if (body[0]) {
                        MultiByteToWideChar(CP_UTF8, 0, body, -1, out, _countof(out));
                        wcsncpy(tip->pszText, out, tip->cchTextMax - 1);
                        tip->pszText[tip->cchTextMax - 1] = 0;
                    }
                }
                return 0;
            }
            else if (((NMHDR FAR *)lParam)->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lParam;
                LRESULT rv = CDRF_DODEFAULT;
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    rv = CDRF_NOTIFYITEMDRAW;
                    break;
                case CDDS_ITEMPREPAINT:
                    rv = CDRF_NOTIFYSUBITEMDRAW;
                    break;
                case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
                    if (cd->iSubItem == 1) {
                        int idx = (int)cd->nmcd.dwItemSpec;
                        int conflict = 0;
                        if (idx >= 0 && idx < (int)(sizeof(hotkeyList)/sizeof(void*)) && hotkeyList[idx]) {
                            int b;
                            for (b = 0; b < SHORTCUT_MAX_BINDINGS && !conflict; b++) {
                                ShotcutHotkey h = hotkeyList[idx]->slots[b];
                                if (h.type == HOTKEY_TYPE_NONE) continue;
                                if (captureHotkeyConflicts(h, idx)) conflict = 1;
                            }
                        }
                        cd->clrText = conflict ? RGB(220, 60, 60)
                                               : (win32CommonIsDarkMode() ? win32CommonDarkFg()
                                                                          : GetSysColor(COLOR_WINDOWTEXT));
                        rv = CDRF_NEWFONT;
                    }
                    break;
                }
                SetWindowLongPtr(hDlg, DWLP_MSGRESULT, rv);
                return TRUE;
            }
        }
        break;

    case WM_CLOSE:
        {
            int rv = 1;

            if (memcmp(shortcutsRef, shortcuts, sizeof(Shortcuts))) {
                rv = (int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SAVEDLG), hDlg, closeProc);
            }
            if (rv) {
                EndDialog(hDlg, FALSE);
            }
        }
        return TRUE;
    }

    return FALSE;
}

char* shortcutsToString(ShotcutHotkey hotkey) 
{
    static char buf[64];
        
    buf[0] = 0;

    switch (hotkey.type) {
    case HOTKEY_TYPE_KEYBOARD:
        if (hotkey.mods & KBD_LCTRL)    { strcat(buf, *buf ? "+" : ""); strcat(buf, "LCtrl"); }
        if (hotkey.mods & KBD_RCTRL)    { strcat(buf, *buf ? "+" : ""); strcat(buf, "RCtrl"); }
        if (hotkey.mods & KBD_LSHIFT)   { strcat(buf, *buf ? "+" : ""); strcat(buf, "LShift"); }
        if (hotkey.mods & KBD_RSHIFT)   { strcat(buf, *buf ? "+" : ""); strcat(buf, "RShift"); }
        if (hotkey.mods & KBD_LALT)     { strcat(buf, *buf ? "+" : ""); strcat(buf, "LAlt"); }
        if (hotkey.mods & KBD_RALT)     { strcat(buf, *buf ? "+" : ""); strcat(buf, "RAlt"); }
        if (hotkey.mods & KBD_LWIN)     { strcat(buf, *buf ? "+" : ""); strcat(buf, "LWin"); }
        if (hotkey.mods & KBD_RWIN)     { strcat(buf, *buf ? "+" : ""); strcat(buf, "RWin"); }
        if (hotkey.key < 256 && virtualKeys[hotkey.key][0]) { strcat(buf, *buf ? "+" : ""); strcat(buf, virtualKeys[hotkey.key]); }
        break;
    case HOTKEY_TYPE_JOYSTICK:
        {
            int dik = hotkeyToDik(hotkey);
            /* Same spelling as the Controller editor, e.g. "XInput1: A". */
            const char* name =
                dik > 0 ? dik2strDisplay(dik)
                        : dikNameToDisplay(shortcutRememberedName(hotkey));
            if (name[0]) {
                strncpy(buf, name, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = 0;
            }
        }
        break;
    }

    return buf;
}


int shortcutsShowDialog(HWND hwnd, Properties* pProperties) {
    BOOL rv;
    FILE* file;
    char fileName[MAX_PATH];

    strcpy(shortcutProfile, pProperties->emulation.shortcutProfile);

    sprintf(fileName, "%s/%s.shortcuts", profileDir, shortcutProfile);
    file = fopen(fileName, "r");
    if (file == NULL) {
        strcpy(shortcutProfile, langShortcutNewProfile());
    }
    else {   
        fclose(file);
    }

    shortcuts = loadShortcuts(shortcutProfile);
    shortcutsRef = calloc(1, sizeof(Shortcuts));
    memcpy(shortcutsRef, shortcuts, sizeof(Shortcuts));
    _snprintf(shortcutsRefProfile, sizeof(shortcutsRefProfile) - 1, "%s", shortcutProfile);
    shortcutsRefProfile[sizeof(shortcutsRefProfile) - 1] = 0;

//    inputDestroy();
    rv = (int)DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SHORTCUTSCONFIG), hwnd, shortcutsProc);
    if (rv) {
        strcpy(pProperties->emulation.shortcutProfile, shortcutProfile);
    }

//    inputDestroy();

    free(shortcuts);
    free(shortcutsRef);
    /* saveShortcuts tests this pointer, so it must not stay dangling. */
    shortcutsRef = NULL;
    shortcutsRefProfile[0] = 0;

    SetFocus(hwnd);
//    inputReset(hwnd);
    keyboardUpdate();

    return rv;
}

int shortcutsIsProfileValid(char* profileName) 
{
    FILE* file;
    char fileName[MAX_PATH];

    sprintf(fileName, "%s/%s.shortcuts", profileDir, profileName);
    file = fopen(fileName, "r");
    if (file != NULL) {
        fclose(file);
        return 1;
    }

    return 0;
}

int shortcutsGetAnyProfile(char* profileName)
{
    profileName[0] = 0;

    if (shortcutsIsProfileValid("blueMSX")) {
        strcat(profileName, "blueMSX");
        return 1;
    }
    else {
        char** profileList = getProfileList();

        if (profileList[0] != NULL) {
            strcat(profileName, profileList[0]);
            return 1;
        }
    }

    return 0;
}

/* Only liveShortcuts is adopted: the dialog re-reads the file on open, and
** shifting its copy would read as an unsaved edit. */
static void shortcutsAdoptArrivedDevices(void)
{
    ShotcutHotkeySet* sets;
    int n, i, b;

    if (liveShortcuts == NULL) return;
    sets = (ShotcutHotkeySet*)liveShortcuts;
    n    = (int)(sizeof(Shortcuts) / sizeof(ShotcutHotkeySet));

    for (i = 0; i < n; i++) {
        for (b = 0; b < SHORTCUT_MAX_BINDINGS; b++) {
            ShotcutHotkey h = sets[i].slots[b];
            int dik;
            if (h.type != HOTKEY_TYPE_JOYSTICK) continue;
            if (shortcutRememberedName(h)[0] == 0) continue;
            dik = inputResolveDikName(shortcutRememberedName(h));
            if (dik <= 0 || !dikToHotkey(dik, &h)) continue;
            /* Two names can meet on one control; the later one gives way. */
            if (shortcutSetHasHotkey(&sets[i], h)) {
                h.type = HOTKEY_TYPE_NONE;
                h.mods = 0;
                h.key  = 0;
            }
            sets[i].slots[b] = h;
        }
    }
}

Shortcuts* shortcutsCreateProfile(char* profileName)
{
    liveShortcuts = loadShortcuts(profileName);
    inputSetShortcutDikCounter(shortcutsCountActionsUsingDik);
    inputSetShortcutDikDescriber(shortcutsDescribeActionsUsingDik);
    inputSetShortcutDeviceArrival(shortcutsAdoptArrivedDevices);
    return liveShortcuts;
}

void shortcutsDestroyProfile(Shortcuts* shortcuts)
{
    if (shortcuts == liveShortcuts) {
        liveShortcuts = NULL;
    }
    free(shortcuts);
}
