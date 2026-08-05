/*****************************************************************************
** File:        PeripheralRegs.cpp
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
#include "PeripheralRegs.h"
#include "Resource.h"
#include "Language.h"
#include "Win32TextUtf8.h"
#include "ToolInterface.h"
#include <stdio.h>
#include <string>

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

static PeripheralRegs* periRegs = NULL;

static LRESULT CALLBACK regViewWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (dbgViewMessage(hwnd, iMsg, wParam)) {
        return 0;
    }
    if (periRegs != NULL) {
        return periRegs->regWndProc(hwnd, iMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

static INT_PTR CALLBACK wndToolProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    return periRegs->toolDlgProc(hwnd, iMsg, wParam, lParam);
}

void PeripheralRegs::updateDropdown()
{
    HWND hCombo = GetDlgItem(toolHwnd, IDC_REGISTERS);
    while (CB_ERR != SendMessageW(hCombo, CB_DELETESTRING, 0, 0));

    int index = 0;
    MemList::iterator it;
    for (it = regList.begin(); it != regList.end(); ++it) {
        RegisterItem* r = *it;
        ComboAddStringU(hCombo, r->title.c_str());
        if (index == 0 || (currentRegs && currentRegs->title == r->title)) {
            SendMessageW(hCombo, CB_SETCURSEL, index, 0);
        }
        index++;
    }
}

void PeripheralRegs::setNewRegisters(const std::string& title)
{
    MemList::iterator it;
    for (it = regList.begin(); it != regList.end(); ++it) {
        RegisterItem* r = *it;
        if (r->title == title) {
            currentRegs = r;
            updateScroll();
            InvalidateRect(regHwnd, NULL, TRUE);
            return;
        }
    }
}

BOOL PeripheralRegs::toolDlgProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_INITDIALOG:
        ApplyDarkMode(hwnd);
        SetDlgItemTextU(hwnd, IDC_TEXT_REGISTERS, Language::memWindowRegisters);
        return FALSE;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_REGISTERS:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                wchar_t wbuf[128];
                int idx = (int)SendDlgItemMessageW(hwnd, IDC_REGISTERS, CB_GETCURSEL, 0, 0);
                int rv  = (int)SendDlgItemMessageW(hwnd, IDC_REGISTERS, CB_GETLBTEXT, idx, (LPARAM)wbuf);
                if (rv != CB_ERR) {
                    char utf8[256];
                    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8, sizeof(utf8), NULL, NULL);
                    setNewRegisters(utf8);
                }
            }
            break;
        }
        return TRUE;

    case WM_CLOSE:
        return FALSE;
    }
    return FALSE;
}

void PeripheralRegs::updateWindowPositions()
{
    RECT r;
    GetWindowRect(toolHwnd, &r);
    int toolHeight = toolHwnd ? r.bottom - r.top : 0;
    GetClientRect(hwnd, &r);
    SetWindowPos(toolHwnd, NULL, 0, 0, r.right - r.left, toolHeight, SWP_NOZORDER);

    SetWindowPos(regHwnd, NULL, 0, toolHeight, r.right - r.left, r.bottom - r.top - toolHeight, SWP_NOZORDER);
}

LRESULT PeripheralRegs::wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE:
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

    case WM_SIZE:
        updateWindowPositions();
        break;

    case WM_DESTROY:
        if (hBrushWhite) { DeleteObject(hBrushWhite); hBrushWhite=NULL; }
        if (hBrushLtGray) { DeleteObject(hBrushLtGray); hBrushLtGray=NULL; }
        if (hBrushDkGray) { DeleteObject(hBrushDkGray); hBrushDkGray=NULL; }
        if (hMemdc) { DeleteDC(hMemdc); hMemdc=NULL; }
        break;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

LRESULT PeripheralRegs::regWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        hMemdc = CreateCompatibleDC(hdc);
        ReleaseDC(hwnd, hdc);
        BOOL dark = IsDarkMode();
        colorBlack = dark ? GetDarkFg()        : RGB(0, 0, 0);
        colorGray  = dark ? RGB(180, 180, 180) : RGB(128, 128, 128);
        colorRed   = dark ? RGB(255, 100, 100) : RGB(255, 0, 0);
        SetBkMode(hMemdc, TRANSPARENT);
        dbgRebuildFont(hMemdc, &hFont, &hFontBold, &textWidth, &textHeight, 0);
        
        hBrushWhite  = CreateSolidBrush(dark ? GetDarkBg()        : RGB(255, 255, 255));
        hBrushLtGray = CreateSolidBrush(dark ? RGB( 48,  48,  48) : RGB(239, 237, 222));
        hBrushDkGray = CreateSolidBrush(dark ? RGB( 70,  70,  70) : RGB(128, 128, 128));

        dataInput2 = new HexInputDialog(hwnd, -100, 0, InputDialog::boxWidth(2, textWidth), InputDialog::boxHeight(textHeight), 2);
        dataInput4 = new HexInputDialog(hwnd, -100, 0, InputDialog::boxWidth(4, textWidth), InputDialog::boxHeight(textHeight), 4);
        dataInput2->setFont(hFont);
        dataInput4->setFont(hFont);
        dataInput2->hide();
        dataInput4->hide();
        darkSubWindow(hwnd);
        return 0;
    }

    case WM_LBUTTONDOWN:
        {
            SetFocus(hwnd);

            if (currentRegs == NULL || !editEnabled) {
                return 0;
            }
        
            SCROLLINFO si;

            si.cbSize = sizeof (si);
            si.fMask  = SIF_POS;
            GetScrollInfo (hwnd, SB_VERT, &si);

            int row = HIWORD(lParam) / textHeight;

            if (row + si.nPos  >= lineCount) {
                return 0;
            }

            int x = (LOWORD(lParam) - 10) / textWidth;
            if (x >= 0 && x < 11 * regPerRow && x % 11 >= 5 && x % 11 < 9) {
                int col = x / 11;
                showEditRegister(col * lineCount + row + si.nPos);
            }


        }
        return 0;

    case HexInputDialog::EC_KILLFOCUS:
        if (navigating) {
            return FALSE;
        }
        /* fall through */
    case HexInputDialog::EC_NEWVALUE:
        if (currentEditRegister >= 0) {
            UInt32 regVal = (HexInputDialog*)wParam == dataInput2 ? (UInt8)lParam : (UInt16)lParam;
            if (currentRegs != NULL && currentRegs->regBank->reg[currentEditRegister].value != regVal) {
                DeviceWriteRegisterBankRegister(currentRegs->regBank, currentEditRegister, regVal);
                currentRegs->regBank->reg[currentEditRegister].value = regVal;
            }
            InvalidateRect(hwnd, NULL, TRUE);
        }
        endEdit();
        return FALSE;

    case InputDialog::EC_NAVIGATE:
        {
            HexInputDialog* input = (HexInputDialog*)wParam;

            /* A stray key with no box open must not start editing a register. */
            if (currentEditRegister < 0) {
                return FALSE;
            }

            if ((int)lParam == InputDialog::NAV_CANCEL) {
                endEdit();
                return FALSE;
            }

            if (currentRegs != NULL && input->isModified()) {
                int value = input->getValue();
                UInt32 regVal = input == dataInput2 ? (UInt8)value : (UInt16)value;
                if (currentRegs->regBank->reg[currentEditRegister].value != regVal) {
                    DeviceWriteRegisterBankRegister(currentRegs->regBank, currentEditRegister, regVal);
                    currentRegs->regBank->reg[currentEditRegister].value = regVal;
                }
                InvalidateRect(hwnd, NULL, TRUE);
            }

            int delta = 0;
            switch ((int)lParam) {
            case InputDialog::NAV_UP:
            case InputDialog::NAV_PREV:  delta = -1;        break;
            case InputDialog::NAV_DOWN:
            case InputDialog::NAV_NEXT:  delta =  1;        break;
            case InputDialog::NAV_LEFT:  delta = -lineCount; break;
            case InputDialog::NAV_RIGHT: delta =  lineCount; break;
            }

            if (delta != 0) {
                /* Stay in edit mode at the ends rather than dropping out. */
                showEditRegister(currentEditRegister + delta);
                return FALSE;
            }

            endEdit();
        }
        return FALSE;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        updateScroll();
        break;

    case WM_VSCROLL:
        endEdit();
        scrollWindow(LOWORD(wParam));
         return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdcw = GetWindowDC(NULL);
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT r;
            GetClientRect(hwnd, &r);
            int top    = ps.rcPaint.top;
            int height = ps.rcPaint.bottom - ps.rcPaint.top;

            HBITMAP hBitmap = CreateCompatibleBitmap(hdcw, r.right, r.bottom);
            HBITMAP hBitmapOrig = (HBITMAP)SelectObject(hMemdc, hBitmap);
            
            SelectObject(hMemdc, pageBrush(hBrushWhite)); 
            PatBlt(hMemdc, 0, top, r.right, height, PATCOPY);

            drawText(ps.rcPaint.top, ps.rcPaint.bottom);

            BitBlt(hdc, 0, top, r.right, height, hMemdc, 0, top, SRCCOPY);

            DeleteObject(SelectObject(hMemdc, hBitmapOrig));
            EndPaint(hwnd, &ps);
            ReleaseDC(NULL, hdcw);
        }
        return TRUE;

    case WM_DESTROY:
        if (hBrushWhite) { DeleteObject(hBrushWhite); hBrushWhite=NULL; }
        if (hBrushLtGray) { DeleteObject(hBrushLtGray); hBrushLtGray=NULL; }
        if (hBrushDkGray) { DeleteObject(hBrushDkGray); hBrushDkGray=NULL; }
        if (dataInput2) { delete dataInput2; dataInput2=NULL; }
        if (dataInput4) { delete dataInput4; dataInput4=NULL; }
        if (hFont) { DeleteObject(hFont); hFont=NULL; }
        if (hFontBold) { DeleteObject(hFontBold); hFontBold=NULL; }
        if (hMemdc) { DeleteDC(hMemdc); hMemdc=NULL; }
        break;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

PeripheralRegs::PeripheralRegs(HINSTANCE hInstance, HWND owner) : 
    DbgWindow( hInstance, owner, 
        Language::windowPeripheralRegisters, "Peripheral Regs Window", 23, 413, 507, 207, 0),
    lineCount(0), currentRegs(NULL), currentEditRegister(-1)
{
    periRegs = this;

    static WNDCLASSEX wndClass;

    wndClass.cbSize         = sizeof(wndClass);
    wndClass.style          = 0;
    wndClass.lpfnWndProc    = regViewWndProc;
    wndClass.cbClsExtra     = 0;
    wndClass.cbWndExtra     = 0;
    wndClass.hInstance      = hInstance;
    wndClass.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hIconSm        = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground  = NULL;
    wndClass.lpszMenuName   = NULL;
    wndClass.lpszClassName  = "msxperiRegsview";

    RegisterClassEx(&wndClass);

    init();

    regHwnd = CreateWindow("msxperiRegsview", "", 
                             WS_OVERLAPPED | WS_CLIPSIBLINGS | WS_CHILD, 
                             CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, hwnd, NULL, hInstance, NULL);

    toolHwnd = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_REGISTERSTOOLBAR), hwnd, wndToolProc);

    ShowWindow(regHwnd, TRUE);
    ShowWindow(toolHwnd, TRUE);

    updateWindowPositions();
    invalidateContent();
}

PeripheralRegs::~PeripheralRegs()
{
    periRegs = NULL;
}

void PeripheralRegs::disableEdit()
{
    endEdit();

    DbgWindow::disableEdit();
}

void PeripheralRegs::hideEdit()
{
    navigating = true;
    dataInput2->hide();
    dataInput4->hide();
    navigating = false;
}

/* Leave edit mode without committing. The register has to be cleared first, or
** the focus change inside hide() comes back as a confirmation. */
void PeripheralRegs::endEdit()
{
    currentEditRegister = -1;
    hideEdit();
}

void PeripheralRegs::showEditRegister(int reg)
{
    if (currentRegs == NULL || lineCount < 1 ||
        reg < 0 || reg >= (int)currentRegs->regBank->count)
    {
        return;
    }

    int width = currentRegs->refBank->reg[reg].width;
    if (width != 8 && width != 16) {
        return;
    }

    hideEdit();

    SCROLLINFO si;
    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS | SIF_PAGE;
    GetScrollInfo (regHwnd, SB_VERT, &si);

    int col  = reg / lineCount;
    int line = reg % lineCount;

    /* Bring the target line into view before placing the box. */
    int pos = si.nPos;
    if (pos > line) {
        pos = line;
    }
    if (si.nPage > 0 && line - pos >= (int)si.nPage) {
        pos = line - (int)si.nPage + 1;
    }
    if (pos < 0) {
        pos = 0;
    }
    if (pos != si.nPos) {
        scrollTo(pos);
        si.fMask = SIF_POS;
        GetScrollInfo (regHwnd, SB_VERT, &si);
    }

    currentEditRegister = reg;

    HexInputDialog* input = width == 16 ? dataInput4 : dataInput2;
    input->setPosition(10 + (11 * col + 5) * textWidth, (line - si.nPos) * textHeight - 2);
    input->setValue(currentRegs->regBank->reg[reg].value);
    input->show();
}

void PeripheralRegs::onFontChanged()
{
    int pos = dbgGetScrollPos(regHwnd);
    int reg = currentEditRegister;
    dbgRebuildFont(hMemdc, &hFont, &hFontBold, &textWidth, &textHeight, 0);

    dataInput2->setSize(InputDialog::boxWidth(2, textWidth), InputDialog::boxHeight(textHeight));
    dataInput4->setSize(InputDialog::boxWidth(4, textWidth), InputDialog::boxHeight(textHeight));
    dataInput2->setFont(hFont);
    dataInput4->setFont(hFont);

    endEdit();
    updateScroll();
    dbgSetScrollPos(regHwnd, pos);

    /* The grid re-flowed under the box, so place it again. */
    if (reg >= 0) {
        showEditRegister(reg);
    }
}

void PeripheralRegs::updatePosition(RECT& rect)
{
    endEdit();

    SetWindowPos(hwnd, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER);
}

void PeripheralRegs::invalidateContent()
{
    setContentStale(false);
    endEdit();

    MemList::iterator it;
    while(!regList.empty()) {
        delete regList.front();
        regList.pop_front();
    }

    currentRegs = NULL;
    updateScroll();

    updateDropdown();

    InvalidateRect(regHwnd, NULL, TRUE);
}

void PeripheralRegs::updateContent(Snapshot* snapshot)
{
    setContentStale(false);
    endEdit();

    bool devicesChanged = false;

    std::string currentRegsTitle;

    if (currentRegs != NULL) {
        currentRegsTitle = currentRegs->title;
    }
    /* Picked up again by title below. It must not survive the delete: a bank
    ** that goes away would leave this pointing into freed memory, and the
    ** fallback further down only triggers on NULL. */
    currentRegs = NULL;

    MemList::iterator it;
    for (it = regList.begin(); it != regList.end(); ++it) {
        RegisterItem* r = *it;
        memcpy(r->refBank, r->regBank, r->size);
        r->flag = false;
    }

    int deviceCount = SnapshotGetDeviceCount(snapshot);

    for (int i = 0; i < deviceCount; i++) {
        Device* device = SnapshotGetDevice(snapshot, i);
        int j;

        if (device->type == DEVTYPE_CPU) {
            continue;
        }

        int regCount = DeviceGetRegisterBankCount(device);

        for (j = 0; j < regCount; j++) {
            RegisterBank* reg = DeviceGetRegisterBank(device, j);

            char regName[64];

            sprintf(regName, "%d: %s - %s", device->deviceId, device->name, reg->name);

            for (it = regList.begin(); it != regList.end(); ++it) {
                RegisterItem* r = *it;
                if (r->title == regName && r->regBank->count == reg->count) {
                    memcpy(r->regBank, reg, r->size);
                    r->flag = true;
                    break;
                }
            }
            if (it == regList.end()) {
                regList.push_back(new RegisterItem(regName, reg));
                devicesChanged = true;
            }
        }
    }

    /* erase already hands back the next entry, so the loop must not advance
    ** again. */
    for (it = regList.begin(); it != regList.end(); ) {
        RegisterItem* r= *it;
        if (!r->flag) {
            devicesChanged = true;
            delete r;
            it = regList.erase(it);
        }
        else {
            ++it;
        }
    }

    for (it = regList.begin(); it != regList.end(); ++it) {
        RegisterItem* r = *it;
        if (r->title == currentRegsTitle) {
            currentRegs = r;
            break;
        }
    }

    if (currentRegs == NULL && regList.size() > 0) {
        currentRegs = regList.front();
        updateScroll();
    }

    if (devicesChanged) {
        updateDropdown();
    }

    InvalidateRect(regHwnd, NULL, TRUE);
}

void PeripheralRegs::updateScroll() 
{
    RECT r;
    GetClientRect(regHwnd, &r);
    int visibleLines = r.bottom / textHeight;

    r.right -= 10 + 4 * textWidth;
 
    regPerRow = 1;

    while (r.right > 12 * textWidth) {
        regPerRow++;
        r.right -= 12 * textWidth;
    }

    int regSize = currentRegs != NULL ? currentRegs->regBank->count : 0;
    lineCount = (regSize + regPerRow - 1) / regPerRow;

    SCROLLINFO si;
    si.cbSize    = sizeof(SCROLLINFO);
    si.fMask     = SIF_PAGE | SIF_POS | SIF_RANGE;
    si.nMin      = 0;
    si.nMax      = lineCount > 0 ? lineCount - 1 : 0;
    si.nPage     = visibleLines;
    si.nPos      = 0;

    SetScrollInfo(regHwnd, SB_VERT, &si, TRUE);
    
    InvalidateRect(regHwnd, NULL, TRUE);
}

void PeripheralRegs::scrollTo(int pos)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS;
    GetScrollInfo (regHwnd, SB_VERT, &si);
    int yPos = si.nPos;

    si.nPos = pos;
    SetScrollInfo (regHwnd, SB_VERT, &si, TRUE);
    GetScrollInfo (regHwnd, SB_VERT, &si);
    if (si.nPos != yPos) {
        ScrollWindow(regHwnd, 0, textHeight * (yPos - si.nPos), NULL, NULL);
        UpdateWindow (regHwnd);
    }
}

void PeripheralRegs::scrollWindow(int sbAction)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo (regHwnd, SB_VERT, &si);
    int pos = si.nPos;
    switch (sbAction) {
    case SB_TOP:
        pos = si.nMin;
        break;
    case SB_BOTTOM:
        pos = si.nMax;
        break;
    case SB_LINEUP:
        pos -= 1;
        break;
    case SB_LINEDOWN:
        pos += 1;
        break;
    case SB_PAGEUP:
        pos -= si.nPage;
        break;
    case SB_PAGEDOWN:
        pos += si.nPage;
        break;
    case SB_THUMBTRACK:
        pos = si.nTrackPos;
        break;
    default:
        break;
    }

    scrollTo(pos);
}

void PeripheralRegs::drawText(int top, int bottom)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS;
    GetScrollInfo (regHwnd, SB_VERT, &si);
    int yPos = si.nPos;
    int FirstLine = max (0, yPos + top / textHeight);
    int LastLine = min (lineCount - 1, yPos + bottom / textHeight);

    RECT rc;
    GetClientRect(regHwnd, &rc);

    for (int i = FirstLine; i <= LastLine; i++) {
        RECT r = { 10, textHeight * (i - yPos), rc.right, textHeight * (i + 1 - yPos) };
        for (int j = 0; j < regPerRow; j++) {
            UInt32 reg = j * lineCount + i;

            if (currentRegs == NULL || reg >= currentRegs->regBank->count) {
                continue;
            }

            int regValue    = currentRegs->regBank->reg[reg].value;
            int refRegValue = currentRegs->refBank->reg[reg].value;
            int regWidth    = currentRegs->refBank->reg[reg].width;
            char* regName   = currentRegs->refBank->reg[reg].name;

            SetTextColor(hMemdc, colorBlack);
            SelectObject(hMemdc, hFontBold);
            DrawTextU(hMemdc, regName, (int)strlen(regName), &r, DT_LEFT);
            SelectObject(hMemdc, hFont); 
            r.left  += 5 * textWidth;

            char text[5];
            if (regValue < 0) {
                SetTextColor(hMemdc, colorGray);
                sprintf(text, "???");
            }
            else {
                SetTextColor(hMemdc, refRegValue != regValue ? colorRed : colorGray);
                if (regWidth == 16) {
                    sprintf(text, "%.4X", regValue);
                }
                else {
                    sprintf(text, "%.2X", regValue);
                }
            }
            DrawTextU(hMemdc, text, (int)strlen(text), &r, DT_LEFT);
            r.left  += 6 * textWidth;
        }
    }
}
