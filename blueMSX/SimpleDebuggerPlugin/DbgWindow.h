/*****************************************************************************
** File:
**      DbgWindow.h
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
#ifndef DBG_WINDOW_H
#define DBG_WINDOW_H

#include <windows.h>
#include <string>

class DbgWindow {
public:
    DbgWindow(HINSTANCE hInstance, HWND owner, const std::string& name, const std::string& ,
              int defX, int defY, int defW, int defH, int defV);
    ~DbgWindow();

    void show();
    void hide();
    bool isVisible();
    
    virtual void enableEdit();
    virtual void disableEdit();

    HWND getOwner() { return owner; }

    virtual LRESULT wndProc(UINT iMsg, WPARAM wParam, LPARAM lParam) = 0;

    /* Called on every view when the shared output font size changes. */
    virtual void onFontChanged() {}

    void updateWindowPos(WINDOWPOS* windowPos);
    
    HWND   hwnd;
protected:
    bool   editEnabled;

    void init();

private:
    HINSTANCE hInstance;
    HWND      owner;
    std::string iniName;
    std::string winName;

    int x;
    int y;
    int width;
    int height;
    int visible;
};

/* Apply dark mode treatment (SetWindowTheme + WM_CTLCOLOR subclass) to a
** sub-view custom window. Safe to call from each view's WM_CREATE -- only
** has effect when the system is in dark mode. */
void darkSubWindow(HWND hwnd);

/* Output font size in points, shared by every sub-view and persisted in
** debugger.ini. Setting it re-fonts all open views. */
int  dbgFontPoints();
int  dbgFontPointsDefault();
void dbgSetFontPoints(int points);

/* Handle Ctrl+plus / Ctrl+minus / Ctrl+0 / Ctrl+wheel; non-zero if consumed. */
int  dbgFontZoomMessage(UINT iMsg, WPARAM wParam);

/* (Re)create the Courier New output font at the current size and re-measure
** the cell metrics. hFontBold may be NULL for views that don't use one. */
void dbgRebuildFont(HDC hMemdc, HFONT* hFont, HFONT* hFontBold,
                    int* textWidth, int* textHeight, int aveCharWidth);

/* Most updateScroll() implementations rewind to the top, which is wrong for a
** re-font. Bracket the call with these to keep the user's position. */
int  dbgGetScrollPos(HWND hwnd);
void dbgSetScrollPos(HWND hwnd, int pos);

#endif //CALLSTACK_H
