/*****************************************************************************
** File:
**      IoPorts.h
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
#ifndef IOPORTS_H
#define IOPORTS_H

#include "DbgWindow.h"
#include "BlueMSXToolInterface.h"
#include <windows.h>

class IoPortWindow : public DbgWindow {
public:
    IoPortWindow(HINSTANCE hInstance, HWND owner);
    ~IoPortWindow();

    void refresh();

    void updateContent(Snapshot* snapshot);
    void invalidateContent();
    void updateScroll();

    virtual LRESULT wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam);

private:

    void scrollWindow(int sbAction);
    void drawText(int top, int bottom);

    HDC    hMemdc;
    HFONT  hFont;
    HBRUSH hBrushWhite;
    HBRUSH hBrushLtGray;
    HBRUSH hBrushDkGray;
    
    COLORREF colorBlack;
    COLORREF colorGray;
    COLORREF colorRed;

    int    textHeight = 1;
    int    textWidth  = 1;

    struct LineInfo {
        char port[4];
        int  portLength;
        char name[32];
        int  nameLength;
        char value[4];
        int  valueLength;
        char readWrite[4];
        int  readWriteLength;
    };

    int      lineCount;
    int      currentLine;
    LineInfo lineInfo[256];
    int      linePos;

    BYTE     ioPortValues[256];
    BYTE     backupIoPortValues[256];
};

#endif //IOPORTS_H
