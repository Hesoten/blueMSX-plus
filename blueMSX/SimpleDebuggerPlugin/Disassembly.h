/*****************************************************************************
** File:
**      Disassembly.h
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
#ifndef DISASSEMBLY_H
#define DISASSEMBLY_H

#include "DbgWindow.h"
#include "SymbolInfo.h"
#include "Breakpoints.h"
#include <windows.h>
#include <map>
#include <vector>

class Disassembly : public DbgWindow {
public:
    Disassembly(HINSTANCE hInstance, HWND owner, SymbolInfo* symInfo, Breakpoints* breakpoints);
    ~Disassembly();

    void refresh();
    
    static int dasm(SymbolInfo* symbolInfo, const UInt8* memory, WORD PC, char* dest);
    static UInt16 GetPc();

    WORD dasm(WORD PC, char* dest);
    
    void onWmKeyUp(int keyCode);

    void updateContent(BYTE* memory, WORD pc);
    void invalidateContent();
    void updateScroll(int address = -1);
    void setCursor(WORD address);
    UInt16 getPc() { return backupPc; }
    const BYTE* getMemory() { return backupMemory; }
    int getCurrentAddress() { return currentLine < 0 ? -1 : lineInfo[currentLine].address; }


    bool isBpOnCcursor() { return currentLine >= 0 && !Breakpoints::IsBreakpointUnset(lineInfo[currentLine].address); }
    bool isCursorPresent()    { return currentLine >= 0; }

    bool writeToFile(const char* fileName);

    virtual LRESULT wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam);

private:

    void scrollWindow(int sbAction);
    void drawText(int top, int bottom);

    HDC    hMemdc;
    HFONT  hFont;
    HBRUSH hBrushWhite;
    HBRUSH hBrushLtGray;
    HBRUSH hBrushDkGray;
    HBRUSH hBrushBlack;
    
    COLORREF colorBlack;
    COLORREF colorGray;
    COLORREF colorWhite;

    int    textHeight = 1;
    int    textWidth  = 1;
    
    struct LineInfo {
        WORD address;
        bool isLabel;
        bool haspc;
        char text[128];
        int  textLength;
        char addr[48];
        int  addrLength;
        char dataText[48];
        int  dataTextLength;
    };

    int      programCounter;
    int      firstVisibleLine;
    int      lineCount;
    int      currentLine;
    LineInfo lineInfo[0x20000];
    int      linePos;
    bool     hasKeyboardFocus;

    BYTE backupMemory[0x10000];
    WORD backupPc;

    SymbolInfo* symbolInfo;
    Breakpoints* breakpoints;
};


#endif //DISASSEMBLY_H
