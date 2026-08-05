/*****************************************************************************
** File:
**      Breakpoints.h
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
#ifndef BREAKPOINTS_H
#define BREAKPOINTS_H

#include <windows.h>
#include <string>
#include <vector>
#include "DbgWindow.h"
#include "ToolBar.h"
#include "SymbolInfo.h"
#include "CpuRegisters.h"
#include "ToolInterface.h"

class Breakpoints : public DbgWindow {
public:

    struct BreakpointInfo {
        enum Type {
            UNINITIALIZED,
            BREAKPOINT,
            WATCHPOINT_MEM,
            WATCHPOINT_VRAM,
            WATCHPOINT_IO
        };

        enum Condition {
            CONDITION_ANY,
            CONDITION_EQUALS,
            CONDITION_NOT_EQUALS,
            CONDITION_LESS_THAN,
            CONDITION_GREATER_THAN
        };

        Type type;
        bool breakpointHit;
        int address;
        char label[32];
        WatchpointCondition condition;
        int referenceValue;
        int size;
        bool enabled;

        /* Every field, since these are copied whole between sessions and a
        ** stale breakpointHit would pin the selected row for good. */
        BreakpointInfo() : type(UNINITIALIZED), breakpointHit(false), address(-1),
                           condition(WATCHPOINT_ANY), referenceValue(0), size(1),
                           enabled(false) {
            label[0] = 0;
        }
        bool operator>(const BreakpointInfo& other) {
            if (condition < other.condition) return true;
            if (condition > other.condition) return false;
            return address > other.address;
        }

        bool operator==(const BreakpointInfo& other) {
            return type == other.type && address == other.address &&
                breakpointHit == other.breakpointHit && strcmp(label, other.label) == 0 &&
                condition == other.condition && referenceValue == other.referenceValue &&
                size == other.size;
        }

        DeviceType getTypeAsDeviceType() const {
            switch (type) {
            case WATCHPOINT_MEM: return DeviceType::DEVTYPE_CPU;
            case WATCHPOINT_VRAM: return DeviceType::DEVTYPE_VIDEO;
            case WATCHPOINT_IO: return DeviceType::DEVTYPE_IOPORT;
            }
            return DeviceType::DEVTYPE_UNKNOWN;
        }

        const char* getTypeAsString() const {
            switch (type) {
            case BREAKPOINT: return "BP";
            case WATCHPOINT_MEM: return "MEM";
            case WATCHPOINT_VRAM: return "VRAM";
            case WATCHPOINT_IO: return "IO";
            }
            return "";
        }

        const char* getSizeAsString() const {
            static char buffer[32];
            if (size == 1) {
                return "1 byte";
            }
            else {
                sprintf(buffer, "%d bytes", size);
            }
            return buffer;
        }

        const char* getConditionAsString() const {
            switch(condition) {
            case CONDITION_ANY: return "";
            case CONDITION_EQUALS: return "=";
            case CONDITION_NOT_EQUALS: return "!=";
            case CONDITION_LESS_THAN: return "<";
            case CONDITION_GREATER_THAN: return ">";
            }
            return "";
        }

        const char* getReferenceValueAsString() const {
            if (condition == CONDITION_ANY) return "";
            char formatString[32];
            sprintf(formatString, "%%.%dXh", size * 2);
            static char buffer[32];
            sprintf(buffer, formatString, referenceValue);
            return buffer;    
        }
    };

    Breakpoints(HINSTANCE hInstance, HWND owner, SymbolInfo* symbolInfo);
    ~Breakpoints();

    virtual void disableEdit();

    void updatePosition(RECT& rect);

    void updateContent();
    void invalidateContent();
    
    // Breakpoint access methods (not for watchpoints)
    static void SetBreakpoint(int address);
    static void ClearBreakpoint(int address);
    static void ToggleBreakpointEnable(int address);
    static bool IsBreakpointUnset(int address);
    static bool IsBreakpointSet(int address);
    static bool IsBreakpointDisabled(int address);

    void enableAllBreakpoints();
    void disableAllBreakpoints();
    void clearAllBreakpoints();

    /* The CPU keeps whatever is armed when the window closes, so hand the list
    ** on to the next debugger session instead of dropping it. */
    void keepBreakpoints();
    void restoreBreakpoints();
    void discardRuntoBreakpoint();

    void isBreakpointSet(const BreakpointInfo& breakpoint);
    void setBreakpoint(const BreakpointInfo& breakpoint);
    void clearBreakpoint(const BreakpointInfo& breakpoint);
    void toggleBreakpointEnable(const BreakpointInfo& breakpoint);
 
    void setWatchpoint(BreakpointInfo& breakpoint);

    void updateBreakpoints();
    bool setStepOverBreakpoint(const UInt8* memory, UInt16 address, bool intEnabled);
    int  getEnabledBpCount();
    int  getDisabledBpCount();

    // Support for temporary disassembly breakpoints
    bool setRuntoBreakpoint(int address);
    void clearRuntoBreakpoint();

    virtual LRESULT wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam);
    virtual void onFontChanged();
    LRESULT breakpointsWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
    BOOL toolDlgProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
    BOOL breakpointsDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam);

private:
    static const BreakpointInfo& MakeBreakpoint(int address);
    
    BreakpointInfo* find(const BreakpointInfo& breakpointInfo);
    void toggleBreakpointEnable(Breakpoints::BreakpointInfo* bi);

    /* selectedLine is an index, and inserting or erasing moves the rows past it,
    ** so anything that does either names the row before and restores it after. */
    BreakpointInfo* selectedRow();
    void selectRow(const BreakpointInfo* row);

    void initializeToolbar(HWND owner);
    void updateToolbar();
    void scrollWindow(int sbAction);
    void updateScroll();
    void updateWindowPositions();
    void drawText(int top, int bottom);
    
    int      runtoBreakpoint;

    std::vector<BreakpointInfo*> breakpoints;

    HWND   breakpointsHwnd;
    Toolbar* toolbar;
    HDC    hMemdc;
    HFONT  hFont     = NULL;
    HFONT  hFontBold = NULL;
    HBRUSH hBrushWhite;
    HBRUSH hBrushLtGray;
    HBRUSH hBrushDkGray;
    HBRUSH hBrushBlack;
    int    textHeight = 1;
    int    textWidth  = 1;
    int    numBreakpoints;
    int    selectedLine;

    COLORREF colorBlack;
    COLORREF colorGray;
    COLORREF colorRed;
    COLORREF colorWhite;

    SymbolInfo* symbolInfo;
};

#endif //BREAKPOINTS_H
