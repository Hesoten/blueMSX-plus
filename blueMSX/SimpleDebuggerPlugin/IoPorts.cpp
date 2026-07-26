/*****************************************************************************
** File:        IoPorts.cpp
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
#include "IoPorts.h"
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


LRESULT IoPortWindow::wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam) 
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
        colorGray  = RGB(160, 160, 160);
        colorRed   = dark ? RGB(255, 100, 100) : RGB(255, 0, 0);

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


IoPortWindow::IoPortWindow(HINSTANCE hInstance, HWND owner) : 
    DbgWindow( hInstance, owner, 
               Language::windowIoPorts, "IO Ports Window", 253, 113, 277, 507, 0),
    linePos(0), lineCount(256), currentLine(-1)
{
    init();
    invalidateContent();
}

IoPortWindow::~IoPortWindow()
{
}

void IoPortWindow::invalidateContent()
{
    currentLine = -1;
    updateScroll();

    memset(ioPortValues, 0xff, 256);
    memset(backupIoPortValues, 0xff, 256);

    for (int i = 0; i < 256; i++) {
        sprintf(lineInfo[i].port, "%.2X", i);
        lineInfo[i].portLength = 2;
        lineInfo[i].nameLength = 0;
        lineInfo[i].valueLength = 0;
        lineInfo[i].readWriteLength = 0;
    }
    
    InvalidateRect(hwnd, NULL, TRUE);
}

void IoPortWindow::onFontChanged()
{
    int pos = dbgGetScrollPos(hwnd);
    dbgRebuildFont(hMemdc, &hFont, NULL, &textWidth, &textHeight, 0);
    updateScroll();
    dbgSetScrollPos(hwnd, pos);
    /* updateScroll only repaints when the scroll position moves, which it
    ** does not here. */
    InvalidateRect(hwnd, NULL, TRUE);
}

void IoPortWindow::refresh()
{
    memcpy(backupIoPortValues, ioPortValues, 256);
    for (int i = 0; i < 256; i++) {
        if (lineInfo[i].valueLength != 0) {
            sprintf(lineInfo[i].value, "%.2x", backupIoPortValues[i]);
            lineInfo[i].portLength = 2;
        }
    }
    currentLine = -1;
    updateScroll();
    InvalidateRect(hwnd, NULL, TRUE);
}

void IoPortWindow::updateContent(Snapshot* snapshot)
{
    static char* readWrite[4] = { (char*)"", (char*)"In", (char*)"Out", (char*)"I/O" };

    currentLine = -1;
    for (int i = 0; i < 256; i++) {
        sprintf(lineInfo[i].port, "%.2X", i);
        lineInfo[i].portLength = 2;
        lineInfo[i].nameLength = 0;
        lineInfo[i].valueLength = 0;
        lineInfo[i].readWriteLength = 0;
    }
    memcpy(backupIoPortValues, ioPortValues, 256);

    int deviceCount = SnapshotGetDeviceCount(snapshot);
    for (int i = 0; i < deviceCount; i++) {
        Device* device = SnapshotGetDevice(snapshot, i);
        int j;

        int ioCount = DeviceGetIoPortsCount(device);

        for (j = 0; j < ioCount; j++) {
            IoPorts* ioPorts = DeviceGetIoPorts(device, j);

            for (UInt32 k = 0; k < ioPorts->count; k++) {
                int port = ioPorts->port[k].port;
                ioPortValues[port] = ioPorts->port[k].value;

                sprintf(lineInfo[port].port, "%.2X", port);
                lineInfo[port].portLength = 2;

                sprintf(lineInfo[port].name, ioPorts->name);
                lineInfo[port].nameLength = (int)strlen(lineInfo[port].name);

                if (ioPorts->port[k].direction == IO_PORT_READ ||
                    ioPorts->port[k].direction == IO_PORT_READWRITE)
                {
                    sprintf(lineInfo[port].value, "%.2x", ioPorts->port[k].value);
                }
                else {
                    sprintf(lineInfo[port].value, "n/a");
                }
                lineInfo[port].valueLength = (int)strlen(lineInfo[port].value);

                strcpy(lineInfo[port].readWrite, readWrite[ioPorts->port[k].direction]);
                lineInfo[port].readWriteLength = (int)strlen(lineInfo[port].readWrite);
            }
        }
    }
    
    updateScroll();
    InvalidateRect(hwnd, NULL, TRUE);
}

void IoPortWindow::updateScroll() 
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

void IoPortWindow::scrollWindow(int sbAction)
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

void IoPortWindow::drawText(int top, int bottom)
{
    SCROLLINFO si;

    si.cbSize = sizeof (si);
    si.fMask  = SIF_POS;
    GetScrollInfo (hwnd, SB_VERT, &si);
    int yPos = si.nPos;
    int FirstLine = max (0, yPos + top / textHeight);
    int LastLine = min (lineCount - 1, yPos + bottom / textHeight);

    RECT r = { 4, textHeight * (FirstLine - yPos), 400, textHeight };

    r.bottom += r.top;

    for (int i = FirstLine; i <= LastLine; i++) {
        if (i < 0 || i >= 256) {
            continue;
        }

        r.left += 1 * textWidth;
        SetTextColor(hMemdc, colorBlack);
        DrawTextU(hMemdc, lineInfo[i].port, lineInfo[i].portLength, &r, DT_LEFT);

        r.left += 3 * textWidth;
        SetTextColor(hMemdc, colorGray);
        DrawTextU(hMemdc, lineInfo[i].name, lineInfo[i].nameLength, &r, DT_LEFT);

        r.left += 15 * textWidth;
        SetTextColor(hMemdc, backupIoPortValues[i] == ioPortValues[i] ? colorBlack : colorRed);
        DrawTextU(hMemdc, lineInfo[i].value, lineInfo[i].valueLength, &r, DT_LEFT);

        r.left += 4 * textWidth;
        SetTextColor(hMemdc, colorBlack);
        DrawTextU(hMemdc, lineInfo[i].readWrite, lineInfo[i].readWriteLength, &r, DT_LEFT);

        r.left -= 23 * textWidth;
        r.top += textHeight;
        r.bottom += textHeight;
    }
}
