/*****************************************************************************
** File:        CpuRegisters.cpp
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
#include "CpuRegisters.h"
#include "Resource.h"
#include "Language.h"
#include "Win32TextUtf8.h"
#include "ToolInterface.h"
#include <stdio.h>

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

namespace {

const char regName[20][8] = {
    "AF ",
    "BC ",
    "DE ",
    "HL ",
    "AF'",
    "BC'",
    "DE'",
    "HL'",
    "IX ",
    "IY ",
    "SP ",
    "PC ",
    "I  ",
    "R  ",
    "IM ",
    "IF1",
    "IF2",
    "CLH",
    "CLL",
    "CNT"
};

}


LRESULT CpuRegisters::wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        hMemdc = CreateCompatibleDC(hdc);
        ReleaseDC(hwnd, hdc);
        BOOL dark = IsDarkMode();
        colorBlack  = dark ? GetDarkFg()        : RGB(0, 0, 0);
        colorLtGray = dark ? RGB( 60,  60,  60) : RGB(240, 240, 240);
        colorGray   = dark ? RGB(180, 180, 180) : RGB(64, 64, 64);
        colorRed    = dark ? RGB(255, 100, 100) : RGB(255, 0, 0);
        SetBkMode(hMemdc, TRANSPARENT);
        hFont = CreateFont(-MulDiv(12, GetDeviceCaps(hMemdc, LOGPIXELSY), 72), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
        hFontBold = CreateFont(-MulDiv(12, GetDeviceCaps(hMemdc, LOGPIXELSY), 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");

        hBrushWhite  = CreateSolidBrush(dark ? GetDarkBg()        : RGB(255, 255, 255));
        hBrushLtGray = CreateSolidBrush(dark ? RGB( 48,  48,  48) : RGB(239, 237, 222));
        hBrushDkGray = CreateSolidBrush(dark ? RGB( 70,  70,  70) : RGB(128, 128, 128));

        SelectObject(hMemdc, hFont); 
        TEXTMETRIC tm;
        if (GetTextMetrics(hMemdc, &tm)) {
            textHeight = tm.tmHeight;
            textWidth = tm.tmMaxCharWidth;
        }

        dataInput2 = new HexInputDialog(hwnd, -100,0,23,22,2);
        dataInput4 = new HexInputDialog(hwnd, -100,0,45,22,4);
        dataInput2->hide();
        dataInput4->hide();
        darkSubWindow(hwnd);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN:
        {
            SetFocus(hwnd);

            if (regValue[0] < 0 || !editEnabled) {
                return 0;
            }
        
            SCROLLINFO si;

            si.cbSize = sizeof (si);
            si.fMask  = SIF_POS;
            GetScrollInfo (hwnd, SB_VERT, &si);

            int row = HIWORD(lParam) / textHeight;

            if (row == 0) {
                if (flagMode == FM_CPU) {
                    int flag = (LOWORD(lParam) - 12 - textWidth * 6) / (textWidth + 4);
                    if (flag >= 0 && flag < 8) {
                        if (currentRegBank != NULL) {
                            regValue[0] ^= (1 << (7 - flag));
                            DeviceWriteRegisterBankRegister(currentRegBank, 0, regValue[0]);
                        }
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                }
                
                if (flagMode == FM_ASM) {
                    int flag = (LOWORD(lParam) - 12 - textWidth * 6) / (2 * textWidth + 7);
                    switch (flag) {
                    case 0: 
                        regValue[0] ^= 0x40; 
                        DeviceWriteRegisterBankRegister(currentRegBank, 0, regValue[0]);
                        break;
                    case 1: 
                        regValue[0] ^= 0x01; 
                        DeviceWriteRegisterBankRegister(currentRegBank, 0, regValue[0]);
                        break;
                    case 2: 
                        regValue[0] ^= 0x04; 
                        DeviceWriteRegisterBankRegister(currentRegBank, 0, regValue[0]);
                        break;
                    case 3: 
                        regValue[0] ^= 0x80;
                        DeviceWriteRegisterBankRegister(currentRegBank, 0, regValue[0]);
                        break;
                    case 4:
                        regValue[15] = regValue[15] > 0 ? 0 : 2;
                        regValue[16] = regValue[15] > 0 ? 1 : 0;
                        DeviceWriteRegisterBankRegister(currentRegBank, 15, regValue[15]);
                        DeviceWriteRegisterBankRegister(currentRegBank, 16, regValue[16]);
                        break;
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                return 0;
            }

            if (row + si.nPos  >= lineCount) {
                return 0;
            }

            int x = (LOWORD(lParam) - 10) / textWidth;
            if (x >= 0 && x < 10 * registersPerRow && x % 10 >= 4 && x % 10 < 8) {
                int col = x / 10;
                int reg = col * (lineCount - 1) + row + si.nPos - 1;

                if (reg < 12) {
                    currentEditRegister = reg;
                    dataInput4->setPosition(10 + (10 * col + 4) * textWidth, row * textHeight - 2);
                    dataInput4->setValue(regValue[reg]);
                    dataInput4->show();
                }
                else if (reg < 15) {
                    currentEditRegister = reg;
                    dataInput2->setPosition(10 + (10 * col + 4) * textWidth, row * textHeight - 2);
                    dataInput2->setValue(regValue[reg]);
                    dataInput2->show();
                }
            }
        }
        return 0;

    case HexInputDialog::EC_KILLFOCUS:
    case HexInputDialog::EC_NEWVALUE:
        if (currentEditRegister >= 0) {
            UInt32 regVal = (HexInputDialog*)wParam == dataInput2 ? (UInt8)lParam : (UInt16)lParam;
            if (currentRegBank != NULL && regValue[currentEditRegister] != regVal) {
                DeviceWriteRegisterBankRegister(currentRegBank, currentEditRegister, regVal);
            }
            regValue[currentEditRegister] = regVal;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        currentEditRegister = -1;
        dataInput2->hide();
        dataInput4->hide();
        return FALSE;

    case WM_SIZE:
        updateScroll();
        break;

    case WM_VSCROLL:
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
        if (dataInput2) { delete dataInput2; dataInput2=NULL; }
        if (dataInput4) { delete dataInput4; dataInput4=NULL; }
        if (hFont) { DeleteObject(hFont); hFont=NULL; }
        if (hFontBold) { DeleteObject(hFontBold); hFontBold=NULL; }
        if (hMemdc) { DeleteDC(hMemdc); hMemdc=NULL; }
        break;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}


CpuRegisters::CpuRegisters(HINSTANCE hInstance, HWND owner) : 
    DbgWindow( hInstance, owner, 
               Language::windowCpuRegisters, "CPU Registers Window", 437, 3, 214, 191, 1),
    lineCount(0), flagMode(FM_ASM), currentEditRegister(-1)
{
    for (int i = 0; i < 20; i++) {
        regValue[i] = -1;
    }

    init();
    invalidateContent();
}

CpuRegisters::~CpuRegisters()
{
}

void CpuRegisters::disableEdit()
{
    dataInput2->hide();
    dataInput4->hide();
    DbgWindow::disableEdit();
}

void CpuRegisters::setFlagMode(CpuRegisters::FlagMode mode)
{
    flagMode = mode;
    InvalidateRect(hwnd, NULL, TRUE);
}

CpuRegisters::FlagMode CpuRegisters::getFlagMode()
{
    return flagMode;
}

BOOL CpuRegisters::lookup(const char* name, WORD* addr)
{
    if (strlen(name) > 3) {
        return FALSE;
    }

    char text[4] = "   ";
    for (UInt32 i = 0; i < strlen(name); i++) {
        text[i] = toupper(name[i]);
    }

    for (int i = 0; i < sizeof(regName) / sizeof(regName[0]); i++) {
        if (strcmp(text, regName[i]) == 0) {
            *addr = regValue[i];
            return TRUE;
        }
    }
    return FALSE;
}

void CpuRegisters::invalidateContent()
{
    currentRegBank = NULL;

    dataInput2->hide();
    dataInput4->hide();
    for (int i = 0; i < 20; i++) {
        refRegValue[i] = -1;
        regValue[i] = -1;
    }
    
    InvalidateRect(hwnd, NULL, TRUE);
}

void CpuRegisters::updateContent(RegisterBank* regBank)
{
    currentRegBank = regBank;

    dataInput2->hide();
    dataInput4->hide();

    for (int i = 0; i < 20; i++) {
        int val = regBank->reg[i].value;
        refRegValue[i] = regValue[i];
        regValue[i] = val;
    }
    
    InvalidateRect(hwnd, NULL, TRUE);
}

void CpuRegisters::updateScroll() 
{
    RECT r;
    GetClientRect(hwnd, &r);
    int visibleLines = r.bottom / textHeight;

    r.right -= 10 + 8 * textWidth;
 
    registersPerRow = 1;

    while (r.right > 11 * textWidth) {
        registersPerRow++;
        r.right -= 11 * textWidth;
    }

    lineCount = 1 + (16 + registersPerRow) / registersPerRow;

    SCROLLINFO si;
    si.cbSize    = sizeof(SCROLLINFO);
    
    GetScrollInfo(hwnd, SB_VERT, &si);
    int oldFirstLine = si.nPos;

    si.fMask     = SIF_PAGE | SIF_POS | SIF_RANGE;
    si.nMin      = 0;
    si.nMax      = lineCount;
    si.nPage     = visibleLines;
    si.nPos      = 0;

    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    
    InvalidateRect(hwnd, NULL, TRUE);
}

void CpuRegisters::scrollWindow(int sbAction)
{
    int yPos;
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo (hwnd, SB_VERT, &si);
    yPos = si.nPos;
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
    SetScrollInfo (hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo (hwnd, SB_VERT, &si);
    if (si.nPos != yPos) {                    
        ScrollWindow(hwnd, 0, textHeight * (yPos - si.nPos), NULL, NULL);
        UpdateWindow (hwnd);
    }
}

void CpuRegisters::drawText(int top, int bottom)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS;
    GetScrollInfo (hwnd, SB_VERT, &si);
    int yPos = si.nPos;
    int FirstLine = max (0, yPos + top / textHeight);
    int LastLine = min (lineCount - 1, yPos + bottom / textHeight);

    for (int i = FirstLine; i <= LastLine; i++) {
        if (i == 0) {
            SelectObject(hMemdc, hBrushLtGray); 
            PatBlt(hMemdc, 0, 0, 300, textHeight + 1, PATCOPY);

            SelectObject(hMemdc, hBrushWhite); 
            WORD regVal = regValue[0];

            SelectObject(hMemdc, hFontBold);
            SetTextColor(hMemdc, colorBlack);
            RECT r = { 10, 1, textWidth * 8, textHeight };
            DrawTextU(hMemdc, Language::windowCpuRegistersFlags, -1, &r, DT_LEFT);
            SelectObject(hMemdc, hFont);

            if (flagMode == FM_CPU) {
                for (int j = 0; j < 8; j++) {
                    int x = 12 + textWidth * 6 + j * (textWidth + 4);
                    PatBlt(hMemdc, x, 3, textWidth + 1, textHeight - 5, PATCOPY);
                    if (regValue[0] >= 0) {
                        RECT r = { x + 1, 1, x + textWidth + 2, textHeight };
                        SetTextColor(hMemdc, (regVal & 0x80) ? colorBlack : colorLtGray);
                        DrawTextU(hMemdc, "SZYHXPNC" + j, 1, &r, DT_LEFT);
                        regVal <<= 1;
                    }
                }
            }
            if (flagMode == FM_ASM) {
                SetTextColor(hMemdc, colorBlack);
                for (int j = 0; j < 5; j++) {
                    int x = 12 + textWidth * 6 + j * (2 * textWidth + 7);
                    PatBlt(hMemdc, x, 3, 2 * textWidth + 1, textHeight - 5, PATCOPY);
                    if (regValue[0] >= 0) {
                        RECT r = { x + 1, 1, x + 2 * textWidth + 2, textHeight };
                        const char* txt = "";
                        switch (j) {
                        case 0: txt = (regVal & 0x40) ? " Z" : "NZ"; break;
                        case 1: txt = (regVal & 0x01) ? " C" : "NC"; break;
                        case 2: txt = (regVal & 0x04) ? "PE" : "PO"; break;
                        case 3: txt = (regVal & 0x80) ? " M" : " P"; break;
                        case 4: txt = regValue[15] > 0 ? "EI" : "DI"; break;
                        }
                        DrawTextU(hMemdc, txt, 2, &r, DT_LEFT);
                    }
                }
            }
            continue;
        }

        RECT r = { 10, textHeight * (i - yPos), 100, textHeight * (i + 1 - yPos) };
        for (int j = 0; j < registersPerRow; j++) {
            int reg = j * (lineCount - 1) + i - 1;
            if (reg > 14) {
                reg += 2;
            }
            if (reg >= 20) {
                continue;
            }

            SetTextColor(hMemdc, colorBlack);
            SelectObject(hMemdc, hFontBold);
            DrawTextU(hMemdc, regName[reg], (int)strlen(regName[reg]), &r, DT_LEFT);
            SelectObject(hMemdc, hFont); 
            r.left  += 4 * textWidth;
            r.right += 4 * textWidth;
            
            char text[5];
            if (regValue[reg] < 0) {
                SetTextColor(hMemdc, colorGray);
                sprintf(text, "???");
            }
            else {
                SetTextColor(hMemdc, refRegValue[reg] != regValue[reg] ? colorRed : colorGray);
                if (reg < 12 || reg > 14) {
                    sprintf(text, "%.4X", regValue[reg]);
                }
                else {
                    sprintf(text, "%.2X", regValue[reg]);
                }
            }
            DrawTextU(hMemdc, text, (int)strlen(text), &r, DT_LEFT);
            r.left  += 6 * textWidth;
            r.right += 6 * textWidth;
        }
    }
}
