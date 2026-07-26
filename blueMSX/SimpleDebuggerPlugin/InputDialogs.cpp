/*****************************************************************************
** File:        InputDialogs.cpp
**
** Author:      Daniel Vik
**
** Copyright (C) 2003-2012 Daniel Vik
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
#include "InputDialogs.h"
#include "Language.h"
#include "Resource.h"
#include "ToolInterface.h"
#include "Win32TextUtf8.h"

extern HWND getRootHwnd();

static InputDialogs* inputDialogsInstance;

InputDialogs::InputDialogs(HINSTANCE hInstance, HWND rootHwnd, Disassembly* disassembly,
        SymbolInfo* symbolInfo, CpuRegisters* cpuRegisters, Memory* memory, Breakpoints* breakpoints) : 
        addressInput(NULL), refValueInput(NULL) {  
    this->hInstance = hInstance;
    this->rootHwnd = rootHwnd;
    this->disassembly = disassembly;
    this->symbolInfo = symbolInfo;
    this->cpuRegisters = cpuRegisters;
    this->memory = memory;
    this->breakpoints = breakpoints;

    currentSearchText[0] = 0;

    inputDialogsInstance = this;
}

InputDialogs::~InputDialogs() {
    inputDialogsInstance = NULL;
}

struct AddrProcData {
    AddrProcData(const char* c) : caption(c), address(0) {}
    const char* caption;
    WORD address;
};

struct WatchpointProcData {
    WatchpointProcData(const char* c) : caption(c) {}
    const char* caption;
    Breakpoints::BreakpointInfo breakpointInfo;
};

struct FindProcData {
    FindProcData(const char* c) : caption(c) { value[0] = 0; }
    const char* caption;
    char        value[128];
};

static INT_PTR CALLBACK staticFindDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (inputDialogsInstance == NULL) {
        return FALSE;
    }
    return inputDialogsInstance->findDialogProc(hDlg, iMsg, wParam, lParam);
}

BOOL InputDialogs::findDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    static TextInputDialog* dataInput = NULL;
    static FindProcData * procData = NULL;

    switch (iMsg) {  
    case WM_INITDIALOG:
        ApplyDarkMode(hDlg);
        procData = (FindProcData*)lParam;
        SetWindowTextU(hDlg, procData->caption);
        SetDlgItemTextU(hDlg, IDC_TEXT_ADDRESS, Language::findWindowText);
        SetDlgItemTextU(hDlg, IDOK, Language::genericOk);
        SetDlgItemTextU(hDlg, IDCANCEL, Language::genericCancel);
        if (dataInput) {
            delete dataInput;
        }
        dataInput = new TextInputDialog(hDlg, 10, 30, 249, 22, 128, true);
        dataInput->setFocus();
        
        return FALSE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            strcpy(procData->value, dataInput->getValue());
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
        
    case TextInputDialog::EC_NEWVALUE:
        strcpy(procData->value, dataInput->getValue());
        EndDialog(hDlg, TRUE);
        return FALSE;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

static INT_PTR CALLBACK staticAddressDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (inputDialogsInstance == NULL) {
        return FALSE;
    }
    return inputDialogsInstance->addressDialogProc(hDlg, iMsg, wParam, lParam);
}

BOOL InputDialogs::addressDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    static HexInputDialog* addressInput = NULL;
    static AddrProcData * procData = NULL;

    switch (iMsg) {  
    case WM_INITDIALOG:
        ApplyDarkMode(hDlg);
        procData = (AddrProcData*)lParam;
        SetWindowTextU(hDlg, procData->caption);
        SetDlgItemTextU(hDlg, IDC_TEXT_ADDRESS, Language::gotoWindowText);
        SetDlgItemTextU(hDlg, IDOK, Language::genericOk);
        SetDlgItemTextU(hDlg, IDCANCEL, Language::genericCancel);
        if (addressInput) {
            delete addressInput;
        }
        addressInput = new HexInputDialog(hDlg, 10,30,249,22,6, true, symbolInfo, cpuRegisters);
        addressInput->setFocus();
        
        return FALSE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            procData->address = (WORD)addressInput->getValue();
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        break;
        
    case HexInputDialog::EC_NEWVALUE:
        procData->address = (WORD)addressInput->getValue();
        EndDialog(hDlg, TRUE);
        return FALSE;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

void InputDialogs::setButtonCheck(HWND hDlg, int id, int check, int enable) {
    HWND hwnd = GetDlgItem(hDlg, id);

    if (check) {
        SendMessage(hwnd, BM_SETCHECK, BST_CHECKED, 0);
    }
    else {
        SendMessage(hwnd, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    ShowWindow(hwnd, enable);
}

void InputDialogs::updateWatchpointControls(HWND hDlg, const Breakpoints::BreakpointInfo& bi) {
    int memtype = 0;
    if (bi.type == Breakpoints::BreakpointInfo::WATCHPOINT_VRAM) memtype = 1;
    if (bi.type == Breakpoints::BreakpointInfo::WATCHPOINT_IO) memtype = 2;
    int mutableSize = memtype == 0 || memtype == 1;
    setButtonCheck(hDlg, IDC_RADIO_SIZE_8, bi.size == 1 || !mutableSize, 1);
    setButtonCheck(hDlg, IDC_RADIO_SIZE_16, bi.size == 2, mutableSize);
    setButtonCheck(hDlg, IDC_RADIO_SIZE_32, bi.size == 4, mutableSize);

    SendDlgItemMessage(hDlg, IDC_CONDITION, CB_SETCURSEL, (WPARAM)bi.condition, 0);
    SendDlgItemMessage(hDlg, IDC_MEMTYPE, CB_SETCURSEL, (WPARAM)memtype, 0);
    if (bi.condition == Breakpoints::BreakpointInfo::CONDITION_ANY) 
        refValueInput->hide();
    else
        refValueInput->show();
}

static INT_PTR CALLBACK staticWatchpointDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (inputDialogsInstance == NULL) {
        return FALSE;
    }
    return inputDialogsInstance->watchpointDialogProc(hDlg, iMsg, wParam, lParam);
}

BOOL InputDialogs::watchpointDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static WatchpointProcData* procData = NULL;

    switch (iMsg) {
    case WM_INITDIALOG:
        ApplyDarkMode(hDlg);
        procData = (WatchpointProcData*)lParam;
        
        procData->breakpointInfo.size = 1;
        procData->breakpointInfo.type = Breakpoints::BreakpointInfo::WATCHPOINT_MEM;
        procData->breakpointInfo.condition = WatchpointCondition::WATCHPOINT_ANY;

        SetWindowTextU(hDlg, procData->caption);
        SetDlgItemTextU(hDlg, IDC_TEXT_ADDRESS, Language::gotoWindowText);
        SetDlgItemTextU(hDlg, IDOK, Language::genericOk);
        SetDlgItemTextU(hDlg, IDCANCEL, Language::genericCancel);

        SendDlgItemMessage(hDlg, IDC_MEMTYPE, CB_ADDSTRING, 0, (LPARAM)"Visible Memory");
        SendDlgItemMessage(hDlg, IDC_MEMTYPE, CB_ADDSTRING, 0, (LPARAM)"VRAM");
        SendDlgItemMessage(hDlg, IDC_MEMTYPE, CB_ADDSTRING, 0, (LPARAM)"IO Ports");

        SendDlgItemMessage(hDlg, IDC_CONDITION, CB_ADDSTRING, 0, (LPARAM)"None");
        SendDlgItemMessage(hDlg, IDC_CONDITION, CB_ADDSTRING, 0, (LPARAM)"Equals");
        SendDlgItemMessage(hDlg, IDC_CONDITION, CB_ADDSTRING, 0, (LPARAM)"Not equals");
        SendDlgItemMessage(hDlg, IDC_CONDITION, CB_ADDSTRING, 0, (LPARAM)"Less than");
        SendDlgItemMessage(hDlg, IDC_CONDITION, CB_ADDSTRING, 0, (LPARAM)"Greater than");

        if (addressInput) delete addressInput;
        addressInput = new HexInputDialog(hDlg, 150,35,199,22,6, true, symbolInfo, cpuRegisters);
        addressInput->setFocus();
        if (refValueInput) refValueInput;
        refValueInput = new HexInputDialog(hDlg, 268,93,80,22,1, true, symbolInfo, cpuRegisters);
        refValueInput->setValue(0);

        updateWatchpointControls(hDlg, procData->breakpointInfo);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            procData->breakpointInfo.address = addressInput->getValue();
            procData->breakpointInfo.referenceValue = refValueInput->getValue();
            if (procData->breakpointInfo.type == Breakpoints::BreakpointInfo::WATCHPOINT_IO) procData->breakpointInfo.size = 1;
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        case IDC_RADIO_SIZE_8:
            procData->breakpointInfo.size = 1;
            updateWatchpointControls(hDlg, procData->breakpointInfo);
            break;
        case IDC_RADIO_SIZE_16:
            procData->breakpointInfo.size = 2;
            updateWatchpointControls(hDlg, procData->breakpointInfo);
            break;
        case IDC_RADIO_SIZE_32:
            procData->breakpointInfo.size = 4;
            updateWatchpointControls(hDlg, procData->breakpointInfo);
            break;
        case IDC_CONDITION:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                procData->breakpointInfo.condition = 
                    (WatchpointCondition)SendDlgItemMessage(hDlg, IDC_CONDITION, CB_GETCURSEL, 0, 0);
                updateWatchpointControls(hDlg, procData->breakpointInfo);
            }
        case IDC_MEMTYPE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int idx = (int)SendDlgItemMessage(hDlg, IDC_MEMTYPE, CB_GETCURSEL, 0, 0);
                if (idx == 0) procData->breakpointInfo.type = Breakpoints::BreakpointInfo::WATCHPOINT_MEM;
                if (idx == 1) procData->breakpointInfo.type = Breakpoints::BreakpointInfo::WATCHPOINT_VRAM;
                if (idx == 2) procData->breakpointInfo.type = Breakpoints::BreakpointInfo::WATCHPOINT_IO;
                updateWatchpointControls(hDlg, procData->breakpointInfo);
            }
        }
        break;
        
    case HexInputDialog::EC_NEWVALUE:
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }

    return FALSE;
}

void InputDialogs::NewBreakpoint() {
    if (inputDialogsInstance != NULL) {
        inputDialogsInstance->newBreakpoint();
    }
}

void InputDialogs::newBreakpoint() {
    int address;
    bool rv = showAddressDialog(Language::setBpWindowCaption, address);
    if (rv) {
        Breakpoints::SetBreakpoint(address);
    }
}

void InputDialogs::GotoAddress() {
    if (inputDialogsInstance != NULL) {
        inputDialogsInstance->gotoAddress();
    }
}

void InputDialogs::gotoAddress() {
    int address;
    bool rv = showAddressDialog(Language::gotoWindowCaption, address);
    if (rv) {
        disassembly->setCursor(address);
    }
}

void InputDialogs::NewWatchpoint() {
    if (inputDialogsInstance != NULL) {
        inputDialogsInstance->newWatchpoint();
    }
}

void InputDialogs::newWatchpoint() {
    Breakpoints::BreakpointInfo breakpointInfo;
    bool rv = showWatchpointDialog(Language::setWpWindowCaption, breakpointInfo);
    if (rv) {
        breakpoints->setBreakpoint(breakpointInfo);
    }
}

void InputDialogs::FindText() {
    if (inputDialogsInstance != NULL) {
        inputDialogsInstance->findText();
    }
}

void InputDialogs::FindNextText() {
    if (inputDialogsInstance != NULL) {
        inputDialogsInstance->memory->findData(inputDialogsInstance->currentSearchText);
    }
}

void InputDialogs::findText() {
    currentSearchText[0] = 0;
    bool rv = showFindTextDialog(Language::findWindowCaption, currentSearchText);
    if (rv) {
        memory->findData(currentSearchText);
    }
}

bool InputDialogs::showAddressDialog(const char* caption, int& address) {
    if (rootHwnd == NULL) {
        return false;
    }
    AddrProcData procData(caption);
    BOOL rv = (BOOL)DialogBoxParam(GetDllHinstance(), MAKEINTRESOURCE(IDD_SETBP), rootHwnd, staticAddressDialogProc, (LPARAM)&procData);
    if (rv) {
        address = procData.address;
        return true;
    }
    return false;
}

bool InputDialogs::showWatchpointDialog(const char* caption, Breakpoints::BreakpointInfo& breakpointInfo) {
    if (rootHwnd == NULL) {
        return false;
    }
    WatchpointProcData procData(caption);
    BOOL rv = (BOOL)DialogBoxParam(GetDllHinstance(), MAKEINTRESOURCE(IDD_WATCHPOINT_ENTRY), rootHwnd, staticWatchpointDialogProc, (LPARAM)&procData);
    if (rv) {
        breakpointInfo = procData.breakpointInfo;
        return true;
    }
    return false;
}

bool InputDialogs::showFindTextDialog(const char* caption, char* enteredText) {
    if (rootHwnd == NULL) {
        return false;
    }

    FindProcData procData(caption);
    BOOL rv = (BOOL)DialogBoxParam(GetDllHinstance(), MAKEINTRESOURCE(IDD_FIND), rootHwnd, staticFindDialogProc, (LPARAM)&procData);
    if (rv) {
        strcpy(enteredText, procData.value);
        return true;
    }
    return false;
}