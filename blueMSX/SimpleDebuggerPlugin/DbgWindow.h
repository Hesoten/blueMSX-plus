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

#endif //CALLSTACK_H
