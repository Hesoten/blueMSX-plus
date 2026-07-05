/*****************************************************************************
** File:
**      EditControls.h
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
#ifndef EDIT_CONTROLS_H
#define EDIT_CONTROLS_H

#include "SymbolInfo.h"
#include <windows.h>
#include <string>
#include <list>
#include <map>

class CpuRegisters;

class InputDialog {
public:
    enum { EC_NEWVALUE = WM_USER + 7029, EC_KILLFOCUS = WM_USER + 7030 };

    InputDialog(HWND parent, int x, int y, int width, int height);
    ~InputDialog();

    void setPosition(int x, int y);
    void show();
    void hide();
    void setFocus();

protected:
    HWND hwnd;

    void initDialog();
    
    virtual BOOL dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam) = 0;

private:
    int wx;
    int wy;
    int wwidth;
    int wheight;
    HWND pparent;

    static std::map<HWND, InputDialog*> dialogMap;
    static int  richeditVersion;

    void initControl(HWND thisHwnd);

    static void initRichEditControlDll();
    static INT_PTR CALLBACK dlgStaticProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
};


class HexInputDialog : public InputDialog {
public:
    HexInputDialog(HWND parent, int x, int y, int width, int height, int numChars, 
                   bool returnNeeded = false, SymbolInfo* symInfo = NULL,
                   CpuRegisters* cpuRegs = NULL);
    ~HexInputDialog();

    void setValue(int value, bool setFocus = true);
    int  getValue();
    bool hasValue();

protected:
    virtual BOOL dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam);

private:
    int chars;
    bool needReturn;
    int  charCount;
    int  fastValue;
    SymbolInfo* symbolInfo;
    CpuRegisters* cpuRegisters;
};



class TextInputDialog : public InputDialog {
public:
    TextInputDialog(HWND parent, int x, int y, int width, int height, int numChars, bool returnNeeded = false);
    ~TextInputDialog();

    void setValue(const char* value, bool setFocus = true);
    const char* getValue();

protected:
    virtual BOOL dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam);
    
private:
    int chars;
    char text[512];
    int  charCount;
    bool needReturn;
};


#endif //EDIT_CONTROLS_H
