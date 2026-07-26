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
    pparent(parent), wx(x), wy(y), wwidth(width), wheight(height)
{
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
    SetWindowPos(GetDlgItem(hwnd, IDC_ADDRESS), NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER);

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

void InputDialog::show() 
{
    ShowWindow(hwnd, TRUE);
}

void InputDialog::hide() 
{
    ShowWindow(hwnd, FALSE);
}

void InputDialog::setFocus() 
{
    SetFocus(hwnd);
}



/////////////////////////////////////////////////////////
/// HexInputDialog
/////////////////////////////////////////////////////////

HexInputDialog::HexInputDialog(HWND parent, int x, int y, int width, int height, int numChars, 
                               bool returnNeeded, SymbolInfo* symInfo, CpuRegisters* cpuRegs) :
    InputDialog(parent, x, y, width, height), chars(numChars), 
    needReturn(returnNeeded), charCount(0), fastValue(0), 
    symbolInfo(symInfo), cpuRegisters(cpuRegs)
{
    initDialog();
}

HexInputDialog::~HexInputDialog() 
{   
}

void HexInputDialog::setValue(int value, bool setFocus)
{
    char text[16] = "00000000";
    sprintf(text + 8, "%x", value);
    SETTEXTEX t = { GT_DEFAULT, CP_ACP };
    SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_SETTEXTEX, (WPARAM)&t, (LPARAM)(text + strlen(text) - chars));
    CHARRANGE cr = { 0, chars };
    SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_EXSETSEL, 0, (LPARAM)&cr);
    if (setFocus) {
        SetFocus(GetDlgItem(hwnd, IDC_ADDRESS));
    }
    charCount = 0;
    fastValue = value;
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
                                
                                char text[16] = "00000000";
                                sprintf(text + 8, "%x", fastValue);
                                SETTEXTEX t = { GT_DEFAULT, CP_ACP };
                                SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_SETTEXTEX, (WPARAM)&t, (LPARAM)(text + strlen(text) - chars));
                                CHARRANGE cr = { 0, chars };
                                SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_EXSETSEL, 0, (LPARAM)&cr);

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
    InputDialog(parent, x, y, width, height), chars(numChars), charCount(0), needReturn(returnNeeded)
{
    initDialog();
}

TextInputDialog::~TextInputDialog() 
{
}

void TextInputDialog::setValue(const char* value, bool setFocus)
{
    SETTEXTEX t = { GT_DEFAULT, CP_ACP };
    SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_SETTEXTEX, (WPARAM)&t, (LPARAM)value);
    CHARRANGE cr = { 0, chars };
    SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_EXSETSEL, 0, (LPARAM)&cr);
    if (setFocus) {
        SetFocus(GetDlgItem(hwnd, IDC_ADDRESS));
    }
    charCount = 0;
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
                            text[charCount] = keyCode;
                            text[charCount + 1] = 0;

                            SETTEXTEX t = { GT_DEFAULT, CP_ACP };
                            SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_SETTEXTEX, (WPARAM)&t, (LPARAM)(text + strlen(text) - chars));
                            CHARRANGE cr = { 0, chars };
                            SendDlgItemMessage(hwnd, IDC_ADDRESS, EM_EXSETSEL, 0, (LPARAM)&cr);

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

