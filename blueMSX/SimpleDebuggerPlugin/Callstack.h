/*****************************************************************************
** File:
**      Callstack.h
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
#ifndef CALLSTACK_H
#define CALLSTACK_H

#include "Disassembly.h"
#include "DbgWindow.h"
#include <windows.h>

class CallstackWindow : public DbgWindow {
public:
    CallstackWindow(HINSTANCE hInstance, HWND owner, Disassembly* disassembly);
    ~CallstackWindow();

    void refresh();
    
    int  getMostRecent();

    void updateContent(DWORD* callstack, int size);
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

    int    textHeight = 1;
    int    textWidth  = 1;

    struct LineInfo {
        WORD address;
        char text[48];
        int  textLength;
        char dataText[48];
        int  dataTextLength;
    };

    int      lineCount;
    int      currentLine;
    LineInfo lineInfo[256];
    int      linePos;
    
    DWORD backupCallstack[0x1000];
    WORD backupSize;

    Disassembly* disassembly;
};

#endif //CALLSTACK_H
