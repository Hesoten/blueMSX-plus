/*****************************************************************************
** File:        Memory.cpp
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
#include "Memory.h"
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

static Memory* memory = NULL;

static LRESULT CALLBACK memViewWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (dbgViewMessage(hwnd, iMsg, wParam)) {
        return 0;
    }
    if (memory != NULL) {
        return memory->memWndProc(hwnd, iMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

static INT_PTR CALLBACK wndToolProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    return memory->toolDlgProc(hwnd, iMsg, wParam, lParam);
}

void Memory::updateDropdown()
{
    HWND hCombo = GetDlgItem(toolHwnd, IDC_MEMORY);
    while (CB_ERR != SendMessageW(hCombo, CB_DELETESTRING, 0, 0));

    int index = 0;
    MemList::iterator it;
    for (it = memList.begin(); it != memList.end(); ++it) {
        MemoryItem* mi = *it;
        ComboAddStringU(hCombo, mi->title.c_str());
        if (index == 0 || (currentMemory && currentMemory->title == mi->title)) {
            SendMessageW(hCombo, CB_SETCURSEL, index, 0);
        }
    }
}

void Memory::setNewMemory(const std::string& title)
{
    MemList::iterator it;
    for (it = memList.begin(); it != memList.end(); ++it) {
        MemoryItem* mi = *it;
        if (mi->title == title) {
            currentMemory = mi;
            updateScroll();
            InvalidateRect(memHwnd, NULL, TRUE);
            return;
        }
    }
}

BOOL Memory::toolDlgProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_INITDIALOG:
        ApplyDarkMode(hwnd);
        SetDlgItemTextU(hwnd, IDC_TEXT_ADDRESS, Language::memWindowAddress);
        SetDlgItemTextU(hwnd, IDC_TEXT_MEMORY,  Language::memWindowMemory);
        /* Anchor the hex input to the label's actual right edge -- the old
        ** hard-coded x=330 (96-DPI) overlaps the memory dropdown when scaled. */
        {
            HWND lbl = GetDlgItem(hwnd, IDC_TEXT_ADDRESS);
            RECT lblR;
            GetWindowRect(lbl, &lblR);
            POINT pt = { lblR.right, lblR.top };
            ScreenToClient(hwnd, &pt);
            int inH = lblR.bottom - lblR.top + 6;
            addressInput = new HexInputDialog(hwnd, pt.x + 5, pt.y - 2, 75, inH, 6, true, symbolInfo, cpuRegisters);
        }
        return FALSE;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MEMORY:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                wchar_t wbuf[128];
                int idx = (int)SendDlgItemMessageW(hwnd, IDC_MEMORY, CB_GETCURSEL, 0, 0);
                int rv  = (int)SendDlgItemMessageW(hwnd, IDC_MEMORY, CB_GETLBTEXT, idx, (LPARAM)wbuf);
                if (rv != CB_ERR) {
                    char utf8[256];
                    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8, sizeof(utf8), NULL, NULL);
                    setNewMemory(utf8);
                }
            }
            break;
        }
        return TRUE;

    case WM_CLOSE:
        delete addressInput;
        return FALSE;

    case HexInputDialog::EC_NEWVALUE:
        if (addressInput == (HexInputDialog*)wParam) {
            showAddress((int)lParam);
        }
        return FALSE;
    }
    return FALSE;
}

void Memory::updateWindowPositions()
{
    RECT r;
    GetWindowRect(toolHwnd, &r);
    int toolHeight = toolHwnd ? r.bottom - r.top : 0;
    GetClientRect(hwnd, &r);
    SetWindowPos(toolHwnd, NULL, 0, 0, r.right - r.left, toolHeight, SWP_NOZORDER);

    SetWindowPos(memHwnd, NULL, 0, toolHeight, r.right - r.left, r.bottom - r.top - toolHeight, SWP_NOZORDER);
}

LRESULT Memory::wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
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

LRESULT Memory::memWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        hMemdc = CreateCompatibleDC(hdc);
        ReleaseDC(hwnd, hdc);
        BOOL dark = IsDarkMode();
        colorBlack  = dark ? GetDarkFg()        : RGB(0, 0, 0);
        colorGray   = dark ? RGB(180, 180, 180) : RGB(128, 128, 128);
        colorLtGray = dark ? RGB(140, 140, 140) : RGB(192, 192, 192);
        colorRed    = dark ? RGB(255, 100, 100) : RGB(255, 0, 0);
        SetBkMode(hMemdc, TRANSPARENT);
        dbgRebuildFont(hMemdc, &hFont, NULL, &textWidth, &textHeight, 0);

        hBrushWhite  = CreateSolidBrush(dark ? GetDarkBg()        : RGB(255, 255, 255));
        hBrushLtGray = CreateSolidBrush(dark ? RGB( 48,  48,  48) : RGB(239, 237, 222));
        hBrushDkGray = CreateSolidBrush(dark ? RGB( 70,  70,  70) : RGB(128, 128, 128));

        dataInput1 = new TextInputDialog(hwnd, -100, 0, InputDialog::boxWidth(1, textWidth), InputDialog::boxHeight(textHeight), 1);
        dataInput2 = new HexInputDialog(hwnd, -100, 0, InputDialog::boxWidth(2, textWidth), InputDialog::boxHeight(textHeight), 2);
        dataInput1->setFont(hFont);
        dataInput2->setFont(hFont);
        dataInput1->hide();
        dataInput2->hide();
        darkSubWindow(hwnd);
        return 0;
    }

    case WM_LBUTTONDOWN:
        {
            SetFocus(hwnd);
            if (currentMemory == NULL || !editEnabled) {
                return 0;
            }

            SCROLLINFO si;

            si.cbSize = sizeof (si);
            si.fMask  = SIF_POS;
            GetScrollInfo (memHwnd, SB_VERT, &si);

            int row = HIWORD(lParam) / textHeight;

            if (row > 0) {
                int x = (LOWORD(lParam) - 10) / textWidth - 8;
                if (x >= 0 && x < 3 * memPerRow && x % 3 != 2) {
                    int col = x / 3;
                    int addr = (row - 1 + si.nPos) * memPerRow + col;
                    if (addr < currentMemory->size) {
                        addressInput->setValue(addr, false);
                        currentEditAddress = addr;
                        dataInput2->setPosition(10 + (3 * col + 8) * textWidth, row * textHeight - 2);
                        dataInput2->setValue(currentMemory->memory[addr]);
                        dataInput2->show();
                    }
                }

                if (x >= 3 * memPerRow + 1 && x < 4 * memPerRow + 1) {
                    int col = x - (3 * memPerRow + 1);
                    int addr = (row - 1 + si.nPos) * memPerRow + col;
                    if (addr < currentMemory->size) {
                        addressInput->setValue(addr, false);
                        currentEditAddress = addr;
                        dataInput1->setPosition(9 + (col + 8 + (3 * memPerRow + 1)) * textWidth, row * textHeight - 2);
                        char text[2] = { (char)currentMemory->memory[addr] , 0 };
                        dataInput1->setValue(text);
                        dataInput1->show();
                    }
                }
            }
        }
        return 0;

    case HexInputDialog::EC_KILLFOCUS:
        if (navigating) {
            return FALSE;
        }
        /* fall through */
    case HexInputDialog::EC_NEWVALUE:
        if (wParam == (WPARAM)dataInput2) {
            if (currentMemory != 0 && currentEditAddress >= 0 && currentEditAddress < currentMemory->size) {
                UInt8 value = (UInt8)lParam;
                bool success = false;
                if (currentMemory->memory[currentEditAddress] != value) {
                    success = DeviceWriteMemoryBlockMemory(currentMemory->memBlock, &value, currentEditAddress, 1);
                }
                if (success) {
                    currentMemory->memory[currentEditAddress] = value;
                }
                InvalidateRect(memHwnd, NULL, TRUE);

                currentEditAddress++;
                
                if (currentEditAddress < currentMemory->size && iMsg == HexInputDialog::EC_NEWVALUE) {
                    showEdit(dataInput2, currentEditAddress);
                    return FALSE;
                }
            }

            endEdit();
        }

        if (wParam == (WPARAM)dataInput1) {
            if (currentMemory != 0 && currentEditAddress >= 0 && currentEditAddress < currentMemory->size) {
                UInt8 value = *(UInt8*)lParam;
                bool success = false;
                if (currentMemory->memory[currentEditAddress] != value) {
                    success = DeviceWriteMemoryBlockMemory(currentMemory->memBlock, &value, currentEditAddress, 1);
                }
                if (success) {
                    currentMemory->memory[currentEditAddress] = value;
                }
                InvalidateRect(memHwnd, NULL, TRUE);

                currentEditAddress++;
                
                if (currentEditAddress < currentMemory->size && iMsg == HexInputDialog::EC_NEWVALUE) {
                    showEdit(dataInput1, currentEditAddress);
                    return FALSE;
                }
            }

            endEdit();
        }

        return FALSE;

    case InputDialog::EC_NAVIGATE:
        {
            InputDialog* input = (InputDialog*)wParam;

            /* A stray key with no box open must not start editing somewhere. */
            if (currentEditAddress < 0) {
                return FALSE;
            }

            if ((int)lParam == InputDialog::NAV_CANCEL) {
                endEdit();
                return FALSE;
            }

            if (currentMemory != 0 && currentEditAddress < currentMemory->size) {
                UInt8 value = input == (InputDialog*)dataInput2
                            ? (UInt8)dataInput2->getValue()
                            : (UInt8)dataInput1->getValue()[0];
                bool success = false;
                if (currentMemory->memory[currentEditAddress] != value) {
                    success = DeviceWriteMemoryBlockMemory(currentMemory->memBlock, &value, currentEditAddress, 1);
                }
                if (success) {
                    currentMemory->memory[currentEditAddress] = value;
                }
                InvalidateRect(memHwnd, NULL, TRUE);
            }

            int delta = 0;
            switch ((int)lParam) {
            case InputDialog::NAV_UP:    delta = -memPerRow; break;
            case InputDialog::NAV_DOWN:  delta =  memPerRow; break;
            case InputDialog::NAV_LEFT:
            case InputDialog::NAV_PREV:  delta = -1;         break;
            case InputDialog::NAV_RIGHT:
            case InputDialog::NAV_NEXT:  delta =  1;         break;
            }

            if (delta != 0) {
                /* Stay in edit mode at the ends rather than dropping out. */
                int address = currentEditAddress + delta;
                if (currentMemory != 0 && address >= 0 && address < currentMemory->size) {
                    showEdit(input, address);
                }
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
            
            SelectObject(hMemdc, hBrushWhite); 
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
        if (dataInput1) { delete dataInput1; dataInput1=NULL; }
        if (dataInput2) { delete dataInput2; dataInput2=NULL; }
        if (hFont) { DeleteObject(hFont); hFont=NULL; }
        if (hMemdc) { DeleteDC(hMemdc); hMemdc=NULL; }
        break;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

Memory::Memory(HINSTANCE hInstance, HWND owner, SymbolInfo* symInfo, CpuRegisters* cpuRegs) : 
    DbgWindow( hInstance, owner, 
        Language::windowMemory, "Memory Window", 3, 422, 714, 210, 1),
    lineCount(0), currentAddress(0), currentMemory(NULL), currentEditAddress(-1), symbolInfo(symInfo), cpuRegisters(cpuRegs)
{
    memory = this;

    static WNDCLASSEX wndClass;

    wndClass.cbSize         = sizeof(wndClass);
    wndClass.style          = 0;
    wndClass.lpfnWndProc    = memViewWndProc;
    wndClass.cbClsExtra     = 0;
    wndClass.cbWndExtra     = 0;
    wndClass.hInstance      = hInstance;
    wndClass.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hIconSm        = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
    wndClass.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground  = NULL;
    wndClass.lpszMenuName   = NULL;
    wndClass.lpszClassName  = "msxmemoryview";

    RegisterClassEx(&wndClass);

    init();

    memHwnd = CreateWindow("msxmemoryview", "", 
                             WS_OVERLAPPED | WS_CLIPSIBLINGS | WS_CHILD, 
                             CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, hwnd, NULL, hInstance, NULL);

    toolHwnd = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_MEMORYTOOLBAR), hwnd, wndToolProc);

    ShowWindow(memHwnd, TRUE);
    ShowWindow(toolHwnd, TRUE);

    updateWindowPositions();
    invalidateContent();
}

Memory::~Memory()
{
    memory = NULL;
}

void Memory::disableEdit()
{
    endEdit();

    DbgWindow::disableEdit();
}

void Memory::onFontChanged()
{
    /* Keep an address, not a row -- memPerRow is derived from the font. */
    int topAddress  = dbgGetScrollPos(memHwnd) * memPerRow;
    int editAddress = currentEditAddress;
    InputDialog* input = activeInput();

    dbgRebuildFont(hMemdc, &hFont, NULL, &textWidth, &textHeight, 0);

    dataInput1->setSize(InputDialog::boxWidth(1, textWidth), InputDialog::boxHeight(textHeight));
    dataInput2->setSize(InputDialog::boxWidth(2, textWidth), InputDialog::boxHeight(textHeight));
    dataInput1->setFont(hFont);
    dataInput2->setFont(hFont);

    endEdit();
    updateScroll();
    dbgSetScrollPos(memHwnd, topAddress / memPerRow);

    /* The grid re-flowed under the box, so place it again. */
    if (input != NULL && editAddress >= 0) {
        showEdit(input, editAddress);
    }
}

void Memory::updatePosition(RECT& rect)
{
    endEdit();

    SetWindowPos(hwnd, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER);
}

void Memory::hideEdit()
{
    navigating = true;
    dataInput1->hide();
    dataInput2->hide();
    navigating = false;
}

/* Leave edit mode without committing. The address has to be cleared first, or
** the focus change inside hide() comes back as a confirmation. */
void Memory::endEdit()
{
    currentEditAddress = -1;
    hideEdit();
}

InputDialog* Memory::activeInput()
{
    if (dataInput1->isVisible()) {
        return dataInput1;
    }
    if (dataInput2->isVisible()) {
        return dataInput2;
    }
    return NULL;
}

void Memory::showEdit(InputDialog* dataInput, DWORD address)
{
    currentEditAddress = address;

    SCROLLINFO si;
    int col = currentEditAddress % memPerRow;
    int row = 0;

    /* Scroll the target row into view. Bounded so a row that cannot be
    ** reached (list shorter than the view) does not spin. */
    for (int guard = 0; guard < 256; guard++) {
        si.cbSize = sizeof (si);
        si.fMask  = SIF_POS | SIF_PAGE;
        GetScrollInfo (memHwnd, SB_VERT, &si);

        row = currentEditAddress / memPerRow - si.nPos + 1;
        if (row >= 1 && row < (int)si.nPage) {
            break;
        }

        /* ScrollWindow blits what is on screen, so a visible edit box would be
        ** smeared down the column one copy per scrolled line. */
        hideEdit();

        int before = si.nPos;
        scrollWindow(row < 1 ? SB_LINEUP : SB_LINEDOWN);

        si.fMask = SIF_POS;
        GetScrollInfo (memHwnd, SB_VERT, &si);
        if (si.nPos == before) {
            row = currentEditAddress / memPerRow - si.nPos + 1;
            break;
        }
    }

    if (dataInput == dataInput1) {
        addressInput->setValue(currentEditAddress, false);
        dataInput1->setPosition(9 + (col + 8 + (3 * memPerRow + 1)) * textWidth, row * textHeight - 2);
        char text[2] = { (char)currentMemory->memory[currentEditAddress], 0 };
        dataInput1->setValue(text);
        dataInput1->show();
    }
    
    if (dataInput == dataInput2) {
        addressInput->setValue(currentEditAddress, false);
        dataInput2->setPosition(10 + (3 * col + 8) * textWidth, row * textHeight - 2);
        dataInput2->setValue(currentMemory->memory[currentEditAddress]);
        dataInput2->show();
    }
}                

bool Memory::writeToFile(const char* fileName)
{
    FILE* f = fopenU(fileName, "wb+");
    if (f == NULL) {
        return false;
    }

    if (currentMemory != NULL) {
        fwrite(currentMemory->memory, 1, currentMemory->size, f);
    }

    fclose(f);

    return true;
}

void Memory::invalidateContent()
{
    endEdit();

    MemList::iterator it;
    while(!memList.empty()) {
        delete memList.front();
        memList.pop_front();
    }

    currentMemory = NULL;

    updateScroll();

    updateDropdown();

    InvalidateRect(memHwnd, NULL, TRUE);
}

void Memory::updateContent(Snapshot* snapshot)
{
    endEdit();

    bool devicesChanged = false;

    std::string currentMemoryTitle;

    if (currentMemory != NULL) {
        currentMemoryTitle = currentMemory->title;
    }

    MemList::iterator it;
    for (it = memList.begin(); it != memList.end(); ++it) {
        MemoryItem* mi = *it;
        memcpy(mi->ref, mi->memory, mi->size);
        mi->flag = false;
    }

    int deviceCount = SnapshotGetDeviceCount(snapshot);

    for (int i = 0; i < deviceCount; i++) {
        Device* device = SnapshotGetDevice(snapshot, i);
        int j;

        int memCount = DeviceGetMemoryBlockCount(device);
        
        for (j = 0; j < memCount; j++) {
            MemoryBlock* mem = DeviceGetMemoryBlock(device, j);

            char memName[64];

            if (mem->writeProtected) {
                sprintf(memName, "%d: %s - %s (read only)", device->deviceId, device->name, mem->name);
            }
            else {
                sprintf(memName, "%d: %s - %s", device->deviceId, device->name, mem->name);
            }

            for (it = memList.begin(); it != memList.end(); ++it) {
                MemoryItem* mi = *it;
                if (mi->title == memName && mi->size == mem->size) {
                    memcpy(mi->memory, mem->memory, mi->size);
                    mi->flag = true;
                    mi->memBlock = mem;
                    break;
                }
            }
            if (it == memList.end()) {
                memList.push_back(new MemoryItem(memName, mem));
                devicesChanged = true;
            }
        }
    }

    for (it = memList.begin(); it != memList.end(); ++it) {
        MemoryItem* mi = *it;
        if (!mi->flag) {
            devicesChanged = true;
            delete mi;
            it = memList.erase(it);
        }
    }

    for (it = memList.begin(); it != memList.end(); ++it) {
        MemoryItem* mi = *it;
        if (mi->title == currentMemoryTitle) {
            currentMemory = mi;
            break;
        }
    }

    if (currentMemory == NULL && memList.size() > 0) {
        currentMemory = memList.front();
        updateScroll();
    }

    if (devicesChanged) {
        updateDropdown();
    }

    InvalidateRect(memHwnd, NULL, TRUE);
}

void Memory::findData(const char* text)
{
    if (currentMemory == NULL ) {
        return;
    }
    int len = (int)strlen(text);
    int startAddr = currentEditAddress >= 0 ? currentEditAddress + 1 : 0;
    int endAddr = currentMemory->size - len;
    if (startAddr >= endAddr) {
        startAddr = 0;
    }

    for (int i = startAddr; i < endAddr; i++)
    {
        if (memcmp(text, currentMemory->memory + i, len) == 0)
        {
            showAddress(i);
            showEdit(dataInput2, i);
            return;
        }
    }
    
    for (int i = 0; i < startAddr; i++)
    {
        if (memcmp(text, currentMemory->memory + i, len) == 0)
        {
            showAddress(i);
            showEdit(dataInput2, i);
            return;
        }
    }
}

void Memory::showAddress(int addr)
{
    RECT r;
    GetClientRect(memHwnd, &r);
    int visibleLines = r.bottom / textHeight;

    int memSize = currentMemory != NULL ? currentMemory->size : 0;

    int maxAddr = memSize - visibleLines * memPerRow;

    if (addr > maxAddr) {
        addr = maxAddr;
    }
    if (addr < 0) {
        addr = 0;
    }

    currentAddress = addr;
    updateScroll();
}

void Memory::updateScroll() 
{
    RECT r;
    GetClientRect(memHwnd, &r);
    int visibleLines = r.bottom / textHeight;

    r.right -= 20 + 13 * textWidth;
 
    memPerRow = 1;

    while (r.right > 4 * textWidth) {
        memPerRow++;
        r.right -= 4 * textWidth;
    }

    int memSize = currentMemory != NULL ? currentMemory->size : 0;
    lineCount = 1 + (memSize + memPerRow - 1) / memPerRow;

    SCROLLINFO si;
    si.cbSize    = sizeof(SCROLLINFO);
    
    GetScrollInfo(memHwnd, SB_VERT, &si);
    int oldFirstLine = si.nPos;

    si.fMask     = SIF_PAGE | SIF_POS | SIF_RANGE;
    si.nMin      = 0;
    si.nMax      = lineCount;
    si.nPage     = visibleLines;
    si.nPos      = currentAddress / memPerRow;

    SetScrollInfo(memHwnd, SB_VERT, &si, TRUE);
    
    InvalidateRect(memHwnd, NULL, TRUE);
}

void Memory::scrollWindow(int sbAction)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo (memHwnd, SB_VERT, &si);
    int yPos = si.nPos;
    switch (sbAction) {
    case SB_TOP:
        si.nPos = si.nMin;
        break;
    case SB_BOTTOM:
        si.nPos = si.nMax;
        break;
    case SB_LINEUP:
        si.nPos -= 1;
        break;
    case SB_LINEDOWN:
        si.nPos += 1;
        break;
    case SB_PAGEUP:
        si.nPos -= si.nPage;
        break;
    case SB_PAGEDOWN:
        si.nPos += si.nPage;
        break;
    case SB_THUMBTRACK:
        si.nPos = si.nTrackPos;
        break;              
    default:
        break; 
    }

    si.fMask = SIF_POS;
    SetScrollInfo (memHwnd, SB_VERT, &si, TRUE);
    GetScrollInfo (memHwnd, SB_VERT, &si);
    if (si.nPos != yPos) {
        RECT r;
        GetClientRect(memHwnd, &r);
        r.top = textHeight;
        ScrollWindow(memHwnd, 0, textHeight * (yPos - si.nPos), &r, &r);
        UpdateWindow (memHwnd);
    }
}

void Memory::drawText(int top, int bottom)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS;
    GetScrollInfo (memHwnd, SB_VERT, &si);
    int yPos = si.nPos;
    int FirstLine = max (0, yPos + top / textHeight);
    int LastLine = min (lineCount - 1, yPos + bottom / textHeight);

    if (currentMemory == NULL) {
        return;
    }
    int memSize = currentMemory != NULL ? currentMemory->size : 0;

    for (int i = FirstLine; i <= LastLine; i++) {
        if  (i == yPos) {
            SelectObject(hMemdc, hBrushWhite); 
            PatBlt(hMemdc, 0, 0, 1024, textHeight + 1, PATCOPY);
            SetTextColor(hMemdc, colorLtGray);

            RECT r = { 10 + textWidth * 8, 0, 100 + textWidth * 8, textHeight };
            int j;
            char addrText[16];
            for (j = 0; j < memPerRow; j++) {
                sprintf(addrText, "+%.1X", j & 15);
                DrawTextU(hMemdc, addrText, (int)strlen(addrText), &r, DT_LEFT);
                
                r.left  += textWidth * 3;
                r.right += textWidth * 3;
            }

            r.left  += textWidth * 1;
            r.right += textWidth * 1;

            for (j = 0; j < memPerRow; j++) {
                sprintf(addrText, "%.1X", j & 15);
                DrawTextU(hMemdc, addrText, (int)strlen(addrText), &r, DT_LEFT);
                r.left  += textWidth * 1;
                r.right += textWidth * 1;
            }
            continue;
        }
        RECT r = { 10, textHeight * (i - yPos), 100, textHeight * (i + 1 - yPos) };
        
        int addr = (i - 1) * memPerRow;

        char addrText[16];
        sprintf(addrText, "%.6X", addr);

        SetTextColor(hMemdc, colorGray);
        DrawTextU(hMemdc, addrText, (int)strlen(addrText), &r, DT_LEFT);

        r.left  += textWidth * 8;
        r.right += textWidth * 8;

        int j;
        for (j = 0; j < memPerRow; j++) {
            if (addr + j >= memSize) {
                continue;
            }
            
            UInt32 val = currentMemory->memory[addr + j];
            UInt32 ref = currentMemory->ref[addr + j];

            if (val == ref) {
                SetTextColor(hMemdc, colorBlack);
            }
            else {
                SetTextColor(hMemdc, colorRed);
            }
            
            sprintf(addrText, "%.2x", val);
            DrawTextU(hMemdc, addrText, (int)strlen(addrText), &r, DT_LEFT);
            
            r.left  += textWidth * 3;
            r.right += textWidth * 3;
        }
        
        r.left  += textWidth * 1;
        r.right += textWidth * 1;

        for (j = 0; j < memPerRow; j++) {
            if (addr >= memSize) {
                continue;
            }
            
            UInt32 val = currentMemory->memory[addr + j];
            UInt32 ref = currentMemory->ref[addr + j];

            if (val == ref) {
                SetTextColor(hMemdc, colorBlack);
            }
            else {
                SetTextColor(hMemdc, colorRed);
            }

            /* Replace non-printable bytes (control chars + high-bit) with '.'
            ** so the ASCII column shows readable glyphs instead of system
            ** "missing glyph" boxes / CP932 lead-byte mojibake. */
            char ch = (val >= 0x20 && val < 0x7F) ? (char)val : '.';
            sprintf(addrText, "%c", ch);
            
            DrawTextU(hMemdc, addrText, (int)strlen(addrText), &r, DT_LEFT);
            
            r.left  += textWidth * 1;
            r.right += textWidth * 1;
        }
    }
}
