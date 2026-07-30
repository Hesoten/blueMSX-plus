/*****************************************************************************
** File:        Callstack.cpp
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
#include "Callstack.h"
#include "ToolInterface.h"
#include "Resource.h"
#include "Language.h"
#include "Win32TextUtf8.h"
#include <stdio.h>

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

LRESULT CallstackWindow::wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        hMemdc = CreateCompatibleDC(hdc);
        ReleaseDC(hwnd, hdc);
        SetBkMode(hMemdc, TRANSPARENT);
        dbgRebuildFont(hMemdc, &hFont, NULL, &textWidth, &textHeight, 0);

        BOOL dark = IsDarkMode();
        hBrushWhite  = CreateSolidBrush(dark ? GetDarkBg()        : RGB(255, 255, 255));
        hBrushLtGray = CreateSolidBrush(dark ? RGB( 48,  48,  48) : RGB(239, 237, 222));
        hBrushDkGray = CreateSolidBrush(dark ? RGB( 70,  70,  70) : RGB(232, 232, 232));
        
        colorBlack = dark ? GetDarkFg()        : RGB(0, 0, 0);
        colorGray  = dark ? RGB(160, 160, 160) : RGB(160, 160, 160);

        darkSubWindow(hwnd);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        updateScroll();
        break;

    case WM_VSCROLL:
        scrollWindow(LOWORD(wParam));
         return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

    case WM_LBUTTONUP:
        {
            SCROLLINFO si;
            si.cbSize = sizeof (si);
            si.fMask  = SIF_POS;
            GetScrollInfo (hwnd, SB_VERT, &si);
            int row = si.nPos + HIWORD(lParam) / textHeight;
            if (row < lineCount) {
                disassembly->setCursor(lineInfo[row].address);
                currentLine = row;
            }
            InvalidateRect(hwnd, NULL, TRUE);
        }
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
        if (hFont) { DeleteObject(hFont); hFont=NULL; }
        if (hMemdc) { DeleteDC(hMemdc); hMemdc=NULL; }
        break;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}


CallstackWindow::CallstackWindow(HINSTANCE hInstance, HWND owner, Disassembly* disassembly_) : 
    DbgWindow( hInstance, owner, 
               Language::windowCallstack, "Callstack Window", 437, 195, 213, 225, 1),
    linePos(0), lineCount(0), currentLine(-1),  
    disassembly(disassembly_), backupSize(0)
{
    init();
    invalidateContent();
}

CallstackWindow::~CallstackWindow()
{
}

void CallstackWindow::invalidateContent()
{
    /* getReturnAddress feeds a real breakpoint now, so the frames have to go
    ** with the display: a kept one belongs to a machine state that is gone. */
    backupSize = 0;
    currentLine = -1;
    lineCount = 0;
    updateScroll();

    sprintf(lineInfo[lineCount].text, "%s", Language::windowCallstackUnavail);
    lineInfo[lineCount].textLength = (int)strlen(lineInfo[lineCount].text);
    lineInfo[lineCount].dataText[0] = 0;
    lineInfo[lineCount].dataTextLength = 0;
    lineCount++;
    
    InvalidateRect(hwnd, NULL, TRUE);
}

void CallstackWindow::refresh()
{
    updateContent(backupCallstack, backupSize);
}

void CallstackWindow::onFontChanged()
{
    int pos = dbgGetScrollPos(hwnd);
    dbgRebuildFont(hMemdc, &hFont, NULL, &textWidth, &textHeight, 0);
    updateScroll();
    dbgSetScrollPos(hwnd, pos);
    /* updateScroll only repaints when the scroll position moves, which it
    ** does not here. */
    InvalidateRect(hwnd, NULL, TRUE);
}

/* The entries are return addresses, which is what step out breaks on.
** updateContent subtracts one only to name the call that produced them. */
int CallstackWindow::getReturnAddress()
{
    if (backupSize > 0) {
        return backupCallstack[backupSize - 1] & 0xffff;
    }
    return -1;
}

void CallstackWindow::updateContent(DWORD* callstack, int size)
{
    currentLine = -1;
    lineCount = 0;

    memcpy(backupCallstack, callstack, size * sizeof(DWORD));
    backupSize = size;

    for (int index = size - 1; index >= 0; index--) {
        UInt16 addr = (UInt16)callstack[index];
        char text[128];
        addr = disassembly->dasm(addr - 1, text);
        sprintf(lineInfo[lineCount].text, "%.4X: %s", addr, text);
        lineInfo[lineCount].textLength = (int)strlen(lineInfo[lineCount].text);
        lineInfo[lineCount].address = addr;

        lineInfo[lineCount].dataText[0] = 0;
        lineInfo[lineCount].dataTextLength = 0;

        lineCount++;
    }
    updateScroll();
    InvalidateRect(hwnd, NULL, TRUE);
}

void CallstackWindow::updateScroll() 
{
    RECT r;
    GetClientRect(hwnd, &r);
    int visibleLines = r.bottom / textHeight;

    SCROLLINFO si;
    si.cbSize    = sizeof(SCROLLINFO);
    
    GetScrollInfo(hwnd, SB_VERT, &si);
    int oldFirstLine = si.nPos;

    si.fMask     = SIF_PAGE | SIF_POS | SIF_RANGE | (visibleLines >= lineCount ? 0 : 0);
    si.nMin      = 0;
    si.nMax      = lineCount > 0 ? lineCount - 1 : 0;
    si.nPage     = visibleLines;
    si.nPos      = 0;

    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    
    GetScrollInfo(hwnd, SB_VERT, &si);
    
    if (si.nPos != oldFirstLine) {
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

void CallstackWindow::scrollWindow(int sbAction)
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

void CallstackWindow::drawText(int top, int bottom)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS;
    GetScrollInfo (hwnd, SB_VERT, &si);
    int yPos = si.nPos;
    int FirstLine = max (0, yPos + top / textHeight);
    int LastLine = min (lineCount - 1, yPos + bottom / textHeight);

    RECT rc;
    GetClientRect(hwnd, &rc);
    RECT r = { 10, textHeight * (FirstLine - yPos), rc.right, textHeight };

    r.bottom += r.top;

    for (int i = FirstLine; i <= LastLine; i++) {
        if (i == currentLine) {
            SelectObject(hMemdc, hBrushDkGray); 
            PatBlt(hMemdc, 0, r.top, rc.right, r.bottom - r.top, PATCOPY);
        }

        int address = lineInfo[i].address;
        SetTextColor(hMemdc, colorGray);
        r.left += 6 * textWidth;
        DrawTextU(hMemdc, lineInfo[i].dataText, lineInfo[i].dataTextLength, &r, DT_LEFT);
        r.left -= 6 * textWidth;
        SetTextColor(hMemdc, colorBlack);

        DrawTextU(hMemdc, lineInfo[i].text, lineInfo[i].textLength, &r, DT_LEFT);
        r.top += textHeight;
        r.bottom += textHeight;
    }
}
