/*****************************************************************************
** File:
**      Memory.h
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
#ifndef MEMORY_H
#define MEMORY_H

#include <windows.h>
#include <string>
#include <list>
#include "DbgWindow.h"
#include "CpuRegisters.h"
#include "ToolInterface.h"
#include "EditControls.h"

class Memory : public DbgWindow {
public:
    Memory(HINSTANCE hInstance, HWND owner, SymbolInfo* symInfo, CpuRegisters* cpuRegs);
    ~Memory();

    virtual void disableEdit();

    void updatePosition(RECT& rect);

    void updateContent(Snapshot* snapshot);
    void invalidateContent();

    bool writeToFile(const char* fileName);

    void findData(const char* text);

    virtual LRESULT wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam);
    LRESULT memWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
    BOOL toolDlgProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

private:
    struct MemoryItem {
        MemoryItem(const std::string t, MemoryBlock* m) : memBlock(m), size(m->size), title(t), flag(true) {
            memory = new UInt8[size];
            ref    = new UInt8[size];
            memcpy(memory, m->memory, size);
            memcpy(ref,    m->memory, size);
        }
        ~MemoryItem() {
            delete[] memory;
            delete[] ref;
        }
        MemoryBlock* memBlock;
        UInt8* memory;
        UInt8* ref;
        int    size;
        std::string title;
        bool flag;
    };

    typedef std::list<MemoryItem*> MemList;

    void showAddress(int addr);
    void scrollWindow(int sbAction);
    void updateScroll();
    void updateDropdown();
    void updateWindowPositions();
    void showEdit(InputDialog* dataInput, DWORD address);
    void setNewMemory(const std::string& title);
    void drawText(int top, int bottom);

    HWND   memHwnd;
    HWND   toolHwnd;
    HDC    hMemdc;
    HFONT  hFont;
    HBRUSH hBrushWhite;
    HBRUSH hBrushLtGray;
    HBRUSH hBrushDkGray;
    int    textHeight = 1;
    int    textWidth  = 1;

    int    memPerRow;
    int    lineCount;

    COLORREF colorBlack;
    COLORREF colorGray;
    COLORREF colorLtGray;
    COLORREF colorRed;

    int currentAddress;
    int currentEditAddress;

    MemList memList;
    MemoryItem* currentMemory;

    HexInputDialog* addressInput;
    TextInputDialog* dataInput1;
    HexInputDialog* dataInput2;
    SymbolInfo* symbolInfo;
    CpuRegisters* cpuRegisters;
};

#endif //MEMORY_H
