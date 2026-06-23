/*****************************************************************************
** File:        Stack.cpp
**
** Author:      Daniel Vik
**
** Copyright (C) 2003-2004 Daniel Vik
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
#include "Stack.h"
#include "ToolInterface.h"
#include "Resource.h"
#include "Language.h"
#include <stdio.h>

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

LRESULT StackWindow::wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
{
    switch (iMsg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        hMemdc = CreateCompatibleDC(hdc);
        ReleaseDC(hwnd, hdc);
        SetBkMode(hMemdc, TRANSPARENT);
        hFont = CreateFont(-MulDiv(10, GetDeviceCaps(hMemdc, LOGPIXELSY), 72), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");

        hBrushWhite  = CreateSolidBrush(RGB(255, 255, 255));
        hBrushLtGray = CreateSolidBrush(RGB(239, 237, 222));
        hBrushDkGray = CreateSolidBrush(RGB(232, 232, 232));
        
        colorRed   = RGB(255, 0, 0);
        colorBlack = RGB(0, 0, 0);
        colorGray  = RGB(160, 160, 160);

        SelectObject(hMemdc, hFont); 
        TEXTMETRIC tm;
        if (GetTextMetrics(hMemdc, &tm)) {
            textHeight = tm.tmHeight;
            textWidth = tm.tmMaxCharWidth;
        }
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


StackWindow::StackWindow(HINSTANCE hInstance, HWND owner) : 
    DbgWindow( hInstance, owner, 
        Language::windowStack, "Stack Window", 653, 3, 137, 417, 1),
    linePos(0), lineCount(0), currentLine(-1)
{
    memset(backupMemory, 0, 0x10000);
    backupSP = 0;

    init();
    invalidateContent();
}

StackWindow::~StackWindow()
{
}

void StackWindow::updatePosition(RECT& rect)
{
    SetWindowPos(hwnd, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER);
}


void StackWindow::invalidateContent()
{
    currentLine = -1;
    lineCount = 0;
    updateScroll();

    sprintf(lineInfo[lineCount].text, Language::windowStackUnavail);
    lineInfo[lineCount].textLength = strlen(lineInfo[lineCount].text);
    lineInfo[lineCount].dataText[0] = 0;
    lineInfo[lineCount].dataTextLength = 0;
    lineInfo[lineCount].modified = false;
    lineCount++;
    
    InvalidateRect(hwnd, NULL, TRUE);
}

void StackWindow::refresh()
{
    updateContent(backupMemory, backupSP);
}

void StackWindow::updateContent(BYTE* memory, WORD sp)
{
    currentLine = -1;
    lineCount = 0;

    for (int addr = sp; addr < 0x10000 && addr < sp + MAX_LINES; ) {
        WORD newValue = ((int)memory[addr + 1] << 8) | memory[addr];
        WORD oldValue = ((int)backupMemory[addr + 1] << 8) | backupMemory[addr];
        sprintf(lineInfo[lineCount].text, "%.4X: ", addr);
        lineInfo[lineCount].textLength = strlen(lineInfo[lineCount].text);
        sprintf(lineInfo[lineCount].dataText, "%.4X", newValue);
        lineInfo[lineCount].dataTextLength = strlen(lineInfo[lineCount].dataText);
        lineInfo[lineCount].address = addr;
        lineInfo[lineCount].modified = newValue != oldValue;

        lineCount++;
        addr += 2;
    }
    
    memcpy(backupMemory, memory, 0x10000);
    backupSP = sp;

    updateScroll();
    InvalidateRect(hwnd, NULL, TRUE);
}

void StackWindow::updateScroll() 
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
    si.nMax      = lineCount;
    si.nPage     = visibleLines;
    si.nPos      = 0;

    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    
    GetScrollInfo(hwnd, SB_VERT, &si);
    
    if (si.nPos != oldFirstLine) {
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

void StackWindow::scrollWindow(int sbAction)
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

void StackWindow::drawText(int top, int bottom)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS;
    GetScrollInfo (hwnd, SB_VERT, &si);
    int yPos = si.nPos;
    int FirstLine = max (0, yPos + top / textHeight);
    int LastLine = min (lineCount - 1, yPos + bottom / textHeight);

    RECT r = { 10, textHeight * (FirstLine - yPos), 300, textHeight };

    r.bottom += r.top;

    for (int i = FirstLine; i <= LastLine; i++) {
        if (i == currentLine) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            SelectObject(hMemdc, hBrushDkGray); 
            PatBlt(hMemdc, 0, r.top, rc.right, r.bottom - r.top, PATCOPY);
        }

        SetTextColor(hMemdc, lineInfo[i].modified ? colorRed : colorBlack);
        r.left += 6 * textWidth;
        DrawText(hMemdc, lineInfo[i].dataText, lineInfo[i].dataTextLength, &r, DT_LEFT);
        r.left -= 6 * textWidth;
        
        SetTextColor(hMemdc, colorGray);
        DrawText(hMemdc, lineInfo[i].text, lineInfo[i].textLength, &r, DT_LEFT);
        r.top += textHeight;
        r.bottom += textHeight;
    }
}
