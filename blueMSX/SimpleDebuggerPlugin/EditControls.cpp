/*****************************************************************************
** File:        EditControls.cpp
**
** Author:      Daniel Vik
**
** Copyright (C) 2003-2004 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
******************************************************************************
*/
#include "EditControls.h"
#include "Resource.h"
#include "ToolInterface.h"
#include "CpuRegisters.h"
#include "DbgWindow.h"
#include <windows.h>
#include <stdio.h>
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif
#include <CommCtrl.h>
#include <RichEdit.h>

/////////////////////////////////////////////////////////
/// InputDialog
/////////////////////////////////////////////////////////


std::map<HWND, InputDialog*> InputDialog::dialogMap;
int InputDialog::richeditVersion = 0;

InputDialog::InputDialog(HWND parent, int x, int y, int width, int height) :
    charCount(0), navEnabled(false), pparent(parent), wx(x), wy(y), wwidth(width), wheight(height)
{
    seedText[0] = 0;
}

/* EM_GETTEXTEX reports the copied length; cb bounds it below the buffer. */
static int readBoxText(HWND dlg, char* buffer, int size)
{
    GETTEXTEX t = { (DWORD)size, GT_DEFAULT, CP_ACP, NULL, NULL };
    int len = (int)SendDlgItemMessage(dlg, IDC_ADDRESS, EM_GETTEXTEX, (WPARAM)&t, (LPARAM)buffer);
    buffer[len > 0 ? len : 0] = 0;
    return len > 0 ? len : 0;
}

void InputDialog::resetModified()
{
    charCount = 0;
    readBoxText(hwnd, seedText, sizeof(seedText));
}

void InputDialog::setBoxText(const char* text)
{
    SETTEXTEX t = { GT_DEFAULT, CP_ACP };
    CHARRANGE cr = { 0, -1 };
    SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_SETTEXTEX, (WPARAM)&t, (LPARAM)text);
    SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_EXSETSEL, 0, (LPARAM)&cr);
}

void InputDialog::restoreSeed()
{
    setBoxText(seedText);
    charCount = 0;
}

bool InputDialog::isModified()
{
    char text[sizeof(seedText)];
    /* The ASCII column seeds a '.' for every byte it cannot print, so typing
    ** a '.' over one leaves the text equal to the seed yet is a real edit. */
    if (charCount > 0) {
        return true;
    }
    readBoxText(hwnd, text, sizeof(text));
    return strcmp(text, seedText) != 0;
}

/* RichEdit swallows Tab and Escape before EN_MSGFILTER sees them, so catch
** those keys ahead of the control. */
static WNDPROC editOrigProc = NULL;

static LRESULT CALLBACK editSubProc(HWND hEdit, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (iMsg == WM_KEYDOWN && (wParam == VK_TAB || wParam == VK_ESCAPE)) {
        if (InputDialog::navigateFrom(GetParent(hEdit), (int)wParam)) {
            return 0;
        }
    }
    /* The view owns the font zoom, but it has no focus while a box is open. */
    if ((iMsg == WM_KEYDOWN || iMsg == WM_MOUSEWHEEL) && GetKeyState(VK_CONTROL) < 0) {
        if (dbgViewMessage(GetParent(GetParent(hEdit)), iMsg, wParam)) {
            return 0;
        }
    }
    return CallWindowProc(editOrigProc, hEdit, iMsg, wParam, lParam);
}

int InputDialog::navigateFrom(HWND dlg, int keyCode)
{
    std::map<HWND, InputDialog*>::iterator i = dialogMap.find(dlg);
    if (i == dialogMap.end()) {
        return 0;
    }
    return i->second->navigateKey(keyCode);
}

void InputDialog::initDialog()
{
    initRichEditControlDll();

    if (richeditVersion == 1) {
        hwnd = CreateDialogParam(GetDllHinstance(), MAKEINTRESOURCE(IDD_RICHEDITCTRL1), pparent, dlgStaticProc, (LPARAM)this);
    }
    else if (richeditVersion == 2) {
        hwnd = CreateDialogParam(GetDllHinstance(), MAKEINTRESOURCE(IDD_RICHEDITCTRL2), pparent, dlgStaticProc, (LPARAM)this);
    }
    
    show();
}

InputDialog::~InputDialog()
{
    std::map<HWND, InputDialog*>::iterator iter = dialogMap.find(hwnd);
    if (iter != dialogMap.end()) {
        dialogMap.erase(iter);
    }
    DestroyWindow(hwnd);
}

void InputDialog::setPosition(int x, int y)
{
    wx = x;
    wy = y;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

void InputDialog::setSize(int width, int height)
{
    wwidth  = width;
    wheight = height;
    SetWindowPos(hwnd, NULL, 0, 0, wwidth, wheight, SWP_NOZORDER | SWP_NOMOVE);
    RECT r;
    GetClientRect(hwnd, &r);
    SetWindowPos(GetDlgItem(hwnd, IDC_ADDRESS), NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER);
}

void InputDialog::setFont(HFONT font)
{
    HWND hEdit = GetDlgItem(hwnd, IDC_ADDRESS);
    if (hEdit == NULL) {
        return;
    }
    SendMessage(hEdit, WM_SETFONT, (WPARAM)font, TRUE);
    /* WM_SETFONT resets the richedit default character format, so the dark
    ** colours have to be re-applied on top of it. */
    applyDarkCharFormat(hEdit);
}

INT_PTR CALLBACK InputDialog::dlgStaticProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (iMsg == WM_INITDIALOG) {
        ((InputDialog*)lParam)->initControl(hwnd);
    }

    if (dialogMap.find(hwnd) == dialogMap.end()) {
        return FALSE;
    }
    return dialogMap[hwnd]->dlgProc(iMsg, wParam, lParam);
}

void InputDialog::initControl(HWND thisHwnd)
{
    hwnd = thisHwnd;

    dialogMap[hwnd] = this;
    dialogMap[hwnd]->hwnd = hwnd;

    SetWindowPos(hwnd, NULL, wx, wy, wwidth, wheight, SWP_NOZORDER);
    RECT r;
    GetClientRect(hwnd, &r);
    HWND hEdit = GetDlgItem(hwnd, IDC_ADDRESS);
    SetWindowPos(hEdit, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER);

    if (hEdit && navEnabled) {
        WNDPROC prev = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)editSubProc);
        if (prev != NULL && prev != editSubProc) {
            editOrigProc = prev;
        }
    }

    /* Apply dark background + caret to the richedit child so Memory's
    ** address textbox stops rendering as a white island. */
    if (IsDarkMode()) {
        ApplyDarkMode(hwnd);
        applyDarkCharFormat(GetDlgItem(hwnd, IDC_ADDRESS));
    }
}

void InputDialog::applyDarkCharFormat(HWND hEdit)
{
    if (hEdit == NULL || !IsDarkMode()) {
        return;
    }
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    cf.crTextColor = GetDarkFg();
    cf.crBackColor = GetDarkBg();
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    SendMessageW(hEdit, EM_SETBKGNDCOLOR, FALSE, GetDarkBg());
}

void InputDialog::initRichEditControlDll()
{
    static bool richeditinitialized = false;
    if (richeditinitialized) {
        InitCommonControls();
        return;
    }

    std::string richeditLibrary;

    OSVERSIONINFO osInfo;
    memset( &osInfo, 0, sizeof(OSVERSIONINFO) );
    osInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if ( GetVersionEx( &osInfo ) ) {
        if ( osInfo.dwPlatformId == VER_PLATFORM_WIN32_NT ) {
            richeditLibrary = "RICHED20.Dll";
            richeditVersion = 2;
        }
        else if ( osInfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ) { //Windows 9.x
            richeditLibrary = "RICHED32.DLL";
            richeditVersion = 1;
        }
    }       

    if  (NULL == LoadLibrary(richeditLibrary.c_str())) {
        richeditVersion = 0;
    }
}

/* Visibility is not the test: the Memory window keeps its goto box on screen
** the whole time. Only the box the caret sits in wants the editing keys. */
HWND InputDialog::focusedEdit()
{
    HWND focus = GetFocus();
    std::map<HWND, InputDialog*>::iterator i;

    if (focus == NULL) {
        return NULL;
    }
    for (i = dialogMap.begin(); i != dialogMap.end(); ++i) {
        if (focus == i->first || GetParent(focus) == i->first) {
            return GetDlgItem(i->first, IDC_ADDRESS);
        }
    }
    return NULL;
}

/* Replayed as keystrokes rather than handed to the control, so the box's own
** filters, its digit counter and its commit on the last character all apply
** exactly as if the text had been typed. */
bool InputDialog::pasteToFocused()
{
    HWND edit = focusedEdit();
    char text[128];
    int len = 0;
    int i;

    if (edit == NULL) {
        return false;
    }

    /* Copied out and the clipboard closed before a single key is replayed: a
    ** keystroke commits into the emulator, and holding the clipboard open over
    ** that locks every other process out of it for as long as it takes. */
    if (OpenClipboard(edit)) {
        HANDLE data = GetClipboardData(CF_TEXT);
        const char* clip = data != NULL ? (const char*)GlobalLock(data) : NULL;
        if (clip != NULL) {
            /* Bounded by the block as well as by the terminator: CF_TEXT is
            ** meant to carry one, but it comes from another process. */
            int avail = (int)GlobalSize(data);
            /* Stops at a newline, which would read as Enter and commit the box
            ** with the rest still unread. The length bounds the replay. */
            while (len < (int)sizeof(text) - 1 && len < avail && clip[len] != 0 &&
                   clip[len] != '\r' && clip[len] != '\n') {
                text[len] = clip[len];
                len++;
            }
            GlobalUnlock(data);
        }
        CloseClipboard();
    }
    text[len] = 0;

    for (i = 0; i < len; i++) {
        /* Committing can hand the caret back to the view, and the rest of the
        ** text belongs to whatever holds it then. */
        if (focusedEdit() != edit) {
            break;
        }
        SendMessage(edit, WM_CHAR, (WPARAM)(UInt8)text[i], 1);
    }
    return true;
}

void InputDialog::show() 
{
    ShowWindow(hwnd, TRUE);
}

void InputDialog::hide()
{
    /* Hiding the dialog leaves the keystrokes going to its now invisible
    ** richedit child, which both breaks the view hotkeys and lets an arrow
    ** key re-open the box. Hand focus back to the view. */
    HWND focus = GetFocus();
    ShowWindow(hwnd, FALSE);
    if (focus == hwnd || focus == GetDlgItem(hwnd, IDC_ADDRESS)) {
        static bool restoring = false;
        if (!restoring) {
            restoring = true;
            SetFocus(pparent);
            restoring = false;
        }
    }
}

void InputDialog::setFocus()
{
    SetFocus(hwnd);
}

int InputDialog::navigateKey(int keyCode)
{
    HWND hEdit = GetDlgItem(hwnd, IDC_ADDRESS);
    if (hEdit == NULL || !navEnabled || !IsWindowVisible(hwnd)) {
        return 0;
    }

    int nav = 0;

    switch (keyCode) {
    case VK_UP:     nav = NAV_UP;     break;
    case VK_DOWN:   nav = NAV_DOWN;   break;
    case VK_RETURN: nav = NAV_DONE;   break;
    case VK_ESCAPE: nav = NAV_CANCEL; break;
    case VK_TAB:    nav = GetKeyState(VK_SHIFT) < 0 ? NAV_PREV : NAV_NEXT; break;

    case VK_LEFT:
    case VK_RIGHT:
        {
            CHARRANGE cr;
            SendMessage(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            if (cr.cpMin != cr.cpMax) {
                return 0;
            }
            if (keyCode == VK_LEFT && cr.cpMin == 0) {
                nav = NAV_LEFT;
            }
            if (keyCode == VK_RIGHT && cr.cpMin >= GetWindowTextLength(hEdit)) {
                nav = NAV_RIGHT;
            }
        }
        break;
    }

    if (nav == 0) {
        return 0;
    }

    SendMessage(GetParent(hwnd), EC_NAVIGATE, (WPARAM)this, nav);
    return 1;
}



/////////////////////////////////////////////////////////
/// HexInputDialog
/////////////////////////////////////////////////////////

HexInputDialog::HexInputDialog(HWND parent, int x, int y, int width, int height, int numChars, 
                               bool returnNeeded, SymbolInfo* symInfo, CpuRegisters* cpuRegs) :
    InputDialog(parent, x, y, width, height), chars(numChars),
    needReturn(returnNeeded), fastValue(0),
    symbolInfo(symInfo), cpuRegisters(cpuRegs)
{
    navEnabled = !returnNeeded;
    initDialog();
}

HexInputDialog::~HexInputDialog() 
{   
}

/* The 8 leading zeroes are the padding; %x prints at most 8 more digits, so
** the buffer holds the longest value the field can ever carry. */
void HexInputDialog::setBoxValue()
{
    char text[24] = "00000000";
    sprintf(text + 8, "%x", fastValue);
    setBoxText(text + strlen(text) - chars);
}

void HexInputDialog::setValue(int value, bool setFocus)
{
    fastValue = value;
    setBoxValue();
    if (setFocus) {
        SetFocus(GetDlgItem(hwnd, IDC_ADDRESS));
    }
    resetModified();
}

bool HexInputDialog::hasValue() 
{
    GETTEXTEX t = {63, GT_DEFAULT, CP_ACP, NULL, NULL};
    char text[64];
    return SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETTEXTEX, (WPARAM)&t, (LPARAM)text) > 0;
}

int HexInputDialog::getValue() 
{
    GETTEXTEX t = {63, GT_DEFAULT, CP_ACP, NULL, NULL};
    char text[64];
    int len = (int)SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETTEXTEX, (WPARAM)&t, (LPARAM)text);
    text[len] = 0;

    int address = 0;
    sscanf(text, "%X", &address);

    WORD val = 0;
        if (cpuRegisters != NULL && toupper(text[0]) == 'R' && cpuRegisters->lookup(text + 1, &val)) {
        address = val;
    }
    if (symbolInfo != NULL && symbolInfo->rfind(text, &val)) {
        address = val;
    }

    return address;
}

BOOL HexInputDialog::dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_INITDIALOG:
        SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_SETEVENTMASK, 0, 
                           SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETEVENTMASK, 0, 0) | ENM_KEYEVENTS); 
        return FALSE;

    case WM_COMMAND:
        switch(HIWORD(wParam)) {
        case EN_KILLFOCUS:
            {
                GETTEXTEX t = {15, GT_DEFAULT, CP_ACP, NULL, NULL};
                char text[16];
                int len = (int)SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETTEXTEX, (WPARAM)&t, (LPARAM)text);
                text[len] = 0;
                WORD addr = 0;
                
                int address = 0;
                sscanf(text, "%X", &address);
                addr = (WORD)address;

                WORD val  = 0;
                if (cpuRegisters != NULL && toupper(text[0]) == 'R' && cpuRegisters->lookup(text + 1, &val)) {
                    addr = val;
                }
                if (symbolInfo != NULL && symbolInfo->rfind(text, &val)) {
                    addr = val;
                }
                SendMessage(GetParent(hwnd), EC_KILLFOCUS, (WPARAM)this, addr);
            }
            return FALSE;
        }
        return FALSE;

    case WM_NOTIFY:  
        switch (LOWORD(wParam)) { 
        
        case IDC_ADDRESS: 
            {
                MSGFILTER *keyfilter = (MSGFILTER *)lParam; 
                int keyCode;
                switch(keyfilter->nmhdr.code) { 
                case EN_MSGFILTER:
                    switch(keyfilter->msg) {
                    case WM_KEYDOWN:
                        if (navigateKey((int)keyfilter->wParam)) {
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                            return TRUE;
                        }
                        /* The whole text is selected, so the control's own
                        ** Delete would empty the box while fastValue kept the
                        ** digits, and an empty box reads back as 0 and writes 0. */
                        if (!needReturn && keyfilter->wParam == VK_DELETE) {
                            fastValue = 0;
                            restoreSeed();
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                            return TRUE;
                        }
                        return FALSE;

                    case WM_CHAR:
                        GETTEXTLENGTHEX tl = {GTL_DEFAULT, CP_ACP};
                        int len = (int)SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETTEXTLENGTHEX, (WPARAM)&tl, 0);
                        if (len == E_INVALIDARG) {
                            len = 0;
                        }

                        char dummyBuf[32];
                        int selLen = (int)SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETSELTEXT, 0, (LPARAM)dummyBuf);

                        keyCode = (int)keyfilter->wParam;

                        if (!needReturn) {
                            /* Digits are shifted into fastValue as they arrive,
                            ** so taking the character back takes the digit too;
                            ** with none left the box is the seed again. */
                            if (keyCode == VK_BACK) {
                                if (charCount > 0) {
                                    charCount--;
                                    fastValue /= 16;
                                }
                                if (charCount == 0) {
                                    restoreSeed();
                                }
                                else {
                                    setBoxValue();
                                }
                                SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                                return TRUE;
                            }
                            if ((keyCode >= '0' && keyCode <= '9') ||
                                (keyCode >= 'a' && keyCode <= 'f') ||
                                (keyCode >= 'A' && keyCode <= 'F'))
                            {
                                if (charCount == 0) {
                                    fastValue = 0;
                                }
                                if (keyCode >= '0' && keyCode <= '9') {
                                    fastValue = 16 * fastValue + keyCode - '0';
                                }
                                if (keyCode >= 'a' && keyCode <= 'f') {
                                    fastValue = 16 * fastValue + 10 + keyCode - 'a';
                                }
                                if (keyCode >= 'A' && keyCode <= 'F') {
                                    fastValue = 16 * fastValue + 10 + keyCode - 'A';
                                }

                                setBoxValue();

                                charCount++;

                                if (charCount == chars) {
                                    SendMessage(GetParent(hwnd), EC_NEWVALUE, (WPARAM)this, fastValue);
                                }
                            }
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                            return TRUE;
                        }

                        // Only check chars if symbolInfo is not available
                        if (symbolInfo == NULL && cpuRegisters == NULL && len - selLen < chars) {
                            if ((keyCode >= '0' && keyCode <= '9') ||
                                (keyCode >= 'a' && keyCode <= 'f') ||
                                (keyCode >= 'A' && keyCode <= 'F'))
                            {
                                if (keyCode >= 'a' && keyCode <= 'f') {
                                    keyfilter->wParam -= 'a' - 'A';
                                }
                                SetWindowLong(hwnd, DWLP_MSGRESULT, 0);
                                return TRUE;
                            }
                        }
                        if (keyCode == '\r' || keyCode == '\n') {
                            SendMessage(GetParent(hwnd), EC_NEWVALUE, (WPARAM)this, getValue());
                        }
                        else if (symbolInfo != NULL || cpuRegisters != NULL) {
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 0);
                            return TRUE;
                        }

                        SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                        return TRUE;
                    }
                }
            }
        }
        return FALSE;
    }
    return FALSE;
}



/////////////////////////////////////////////////////////
/// TextInputDialog
/////////////////////////////////////////////////////////

TextInputDialog::TextInputDialog(HWND parent, int x, int y, int width, int height, 
                                 int numChars, bool returnNeeded) :
    InputDialog(parent, x, y, width, height), chars(numChars), needReturn(returnNeeded)
{
    navEnabled = !returnNeeded;
    initDialog();
}

TextInputDialog::~TextInputDialog() 
{
}

void TextInputDialog::setValue(const char* value, bool setFocus)
{
    setBoxText(value);
    if (setFocus) {
        SetFocus(GetDlgItem(hwnd, IDC_ADDRESS));
    }
    resetModified();
}

const char* TextInputDialog::getValue()
{
    GETTEXTEX t = {(DWORD)(chars + 1), GT_DEFAULT, CP_ACP, NULL, NULL};
    int len = (int)SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETTEXTEX, (WPARAM)&t, (LPARAM)text);
    text[len] = 0;

    return text;
}

BOOL TextInputDialog::dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_INITDIALOG:
        SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_SETEVENTMASK, 0, 
                           SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETEVENTMASK, 0, 0) | ENM_KEYEVENTS); 
        return FALSE;

    case WM_COMMAND:
        switch(HIWORD(wParam)) {
        case EN_KILLFOCUS:
            SendMessage(GetParent(hwnd), EC_KILLFOCUS, (WPARAM)this, (LPARAM)getValue());
            return FALSE;
        }
        return FALSE;

    case WM_NOTIFY:  
        switch (LOWORD(wParam)) { 
        
        case IDC_ADDRESS: 
            {
                MSGFILTER *keyfilter = (MSGFILTER *)lParam; 
                int keyCode;
                switch(keyfilter->nmhdr.code) { 
                case EN_MSGFILTER:
                    switch(keyfilter->msg) {
                    case WM_KEYDOWN:
                        if (navigateKey((int)keyfilter->wParam)) {
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                            return TRUE;
                        }
                        /* The control's own Delete would empty the box behind
                        ** the buffer's back, and an empty box reads back as a
                        ** zero byte and writes one. Discard the entry instead. */
                        if (!needReturn && keyfilter->wParam == VK_DELETE) {
                            text[0] = 0;
                            restoreSeed();
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                            return TRUE;
                        }
                        return FALSE;

                    case WM_CHAR:
                        GETTEXTLENGTHEX tl = {GTL_DEFAULT, CP_ACP};
                        int len = (int)SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETTEXTLENGTHEX, (WPARAM)&tl, 0);
                        if (len == E_INVALIDARG) {
                            len = 0;
                        }

                        char dummyBuf[32];
                        int selLen = (int)SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_GETSELTEXT, 0, (LPARAM)dummyBuf);

                        keyCode = (int)keyfilter->wParam;

                        if (!needReturn) {
                            /* Takes the last character back off the buffer as
                            ** well as off the box; with none left the box is
                            ** the value it was seeded with again. */
                            if (keyCode == VK_BACK) {
                                if (charCount > 0) {
                                    charCount--;
                                    text[charCount] = 0;
                                }
                                if (charCount == 0) {
                                    restoreSeed();
                                }
                                else {
                                    setBoxText(text);
                                }
                                SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                                return TRUE;
                            }
                            /* Only printable bytes are data here. Tab is an
                            ** editing key, and has already moved the cursor on
                            ** by the time its WM_CHAR arrives. */
                            if (keyCode < ' ' || keyCode > '~') {
                                SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                                return TRUE;
                            }
                            text[charCount] = keyCode;
                            text[charCount + 1] = 0;

                            setBoxText(text);

                            charCount++;

                            if (charCount == chars) {
                                SendMessage(GetParent(hwnd), EC_NEWVALUE, (WPARAM)this, (LPARAM)text);
                            }
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                            return TRUE;
                        }


                        if (keyCode == '\r' || keyCode == '\n') {
                            SendMessage(GetParent(hwnd), EC_NEWVALUE, (WPARAM)this, (LPARAM)getValue());
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                            return TRUE;
                        }
                        
                        if (len - selLen < chars) {
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 0);
                        }
                        else {
                            SetWindowLong(hwnd, DWLP_MSGRESULT, 1);
                        }
                        return TRUE;
                    }
                }
            }
        }
        return FALSE;
    }
    return FALSE;
}

