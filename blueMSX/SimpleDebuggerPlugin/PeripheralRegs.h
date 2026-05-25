/*****************************************************************************
** File:
**      PeripheralRegs.h
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
#ifndef PERIPHEERAL_REGS_H
#define PERIPHEERAL_REGS_H

#include <windows.h>
#include <string>
#include <list>
#include "DbgWindow.h"
#include "ToolInterface.h"
#include "EditControls.h"

class PeripheralRegs : public DbgWindow {
public:
    PeripheralRegs(HINSTANCE hInstance, HWND owner);
    ~PeripheralRegs();

    virtual void disableEdit();

    void updatePosition(RECT& rect);

    void updateContent(Snapshot* snapshot);
    void invalidateContent();

    virtual LRESULT wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam);
    LRESULT regWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
    BOOL toolDlgProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

private:
    struct RegisterItem {
        RegisterItem(const std::string t, RegisterBank* r) : title(t), flag(true) {
            size = sizeof(RegisterBank) + r->count * sizeof(RegisterBank::Register);
            regBank = (RegisterBank*)malloc(size);
            refBank = (RegisterBank*)malloc(size);
            memcpy(regBank, r, size);
            memcpy(refBank, r, size);
        }
        ~RegisterItem() {
            free(regBank);
            free(refBank);
        }

        RegisterBank* regBank;
        RegisterBank* refBank;
        int size;

        std::string title;
        bool flag;
    };

    typedef std::list<RegisterItem*> MemList;

    void showAddress(int addr);
    void scrollWindow(int sbAction);
    void updateScroll();
    void updateDropdown();
    void updateWindowPositions();
    void setNewRegisters(const std::string& title);
    void drawText(int top, int bottom);

    HWND   regHwnd;
    HWND   toolHwnd;
    HDC    hMemdc;
    HFONT  hFont;
    HFONT  hFontBold;
    HBRUSH hBrushWhite;
    HBRUSH hBrushLtGray;
    HBRUSH hBrushDkGray;
    int    textHeight = 1;
    int    textWidth  = 1;

    int    regPerRow;
    int    lineCount;

    COLORREF colorBlack;
    COLORREF colorGray;
    COLORREF colorRed;

    int currentEditRegister;

    MemList regList;
    RegisterItem* currentRegs;

    HexInputDialog* dataInput2;
    HexInputDialog* dataInput4;
};

#endif //PERIPHEERAL_REGS_H
