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

    /* followPc = true is for refreshes that renumber the lines (symbols coming
    ** and going), where the cached first visible line no longer means anything. */
    void refresh(bool followPc = false);

    static int dasm(SymbolInfo* symbolInfo, const UInt8* memory, WORD PC, char* dest);
    static UInt16 GetPc();

    WORD dasm(WORD PC, char* dest);
    
    void onWmKeyUp(int keyCode);

    /* followPc = false keeps the view and the focused window untouched, for
    ** refreshes that are not caused by the CPU actually moving. */
    void updateContent(BYTE* memory, WORD pc, bool followPc = true);
    void invalidateContent();

    /* The listing came out of the machine but the CPU has moved on. Keeps every
    ** line, so its addresses stay usable, and only says so on screen. */
    void markContentStale();

    /* Redraw what is already there. The breakpoint icons are read from
    ** Breakpoints at paint time, so showing one never needs a rebuild. */
    void repaint() { InvalidateRect(hwnd, NULL, TRUE); }

    /* False only while there is no listing at all, which is what the commands
    ** that read one have to test. */
    bool hasContent() { return contentState != CONTENT_NONE; }

    void updateScroll(int address = -1);
    void setCursor(WORD address);
    UInt16 getPc() { return backupPc; }
    const BYTE* getMemory() { return backupMemory; }
    /* Bounded against lineCount as well, so that asking whether there is a
    ** cursor and asking for its address can never disagree. */
    int getCurrentAddress() { return currentLine < 0 || currentLine >= lineCount ? -1 : lineInfo[currentLine].address; }


    bool isBpOnCcursor() { return getCurrentAddress() >= 0 && !Breakpoints::IsBreakpointUnset((WORD)getCurrentAddress()); }
    bool isCursorPresent()    { return getCurrentAddress() >= 0; }

    bool writeToFile(const char* fileName);

    virtual LRESULT wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam);
    virtual void onFontChanged();

private:

    void scrollWindow(int sbAction);
    void applyScroll();
    void drawText(int top, int bottom);

    /* The line holding `address`, or -1. The lines are ordered by address, so
    ** the last one at or below it is the instruction that contains it. */
    int  lineForAddress(int address);

    HDC    hMemdc;
    HFONT  hFont = NULL;
    HBRUSH hBrushWhite;
    HBRUSH hBrushLtGray;
    HBRUSH hBrushDkGray;
    HBRUSH hBrushBlack;
    
    COLORREF colorBlack;
    COLORREF colorGray;
    COLORREF colorWhite;
    COLORREF colorStale;

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

    /* NONE is the only state whose lines are fiction: nothing disassembled
    ** yet, so the backup is the zeroed buffer. STALE lines came out of the
    ** machine; only their decode and the PC marker are behind. */
    enum ContentState { CONTENT_NONE, CONTENT_STALE, CONTENT_FRESH };
    ContentState contentState;

    LineInfo lineInfo[0x20000];
    int      linePos;
    bool     hasKeyboardFocus;

    BYTE backupMemory[0x10000];
    WORD backupPc;

    SymbolInfo* symbolInfo;
    Breakpoints* breakpoints;
};


#endif //DISASSEMBLY_H
