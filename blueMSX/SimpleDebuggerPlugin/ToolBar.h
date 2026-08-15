/*****************************************************************************
** File:
**      ToolBar.h
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
#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <windows.h>
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif
#include <CommCtrl.h>

class Toolbar {
public:
    Toolbar(HINSTANCE hInstance, HWND owner, int bitmapId, COLORREF transparentColor, int backgroundId = -1);
    ~Toolbar();

    void show();
    void hide();
    int getHeight();
    
    void updatePosition();

    void addButton(int bitmap, int command, int dropdown = false, int insertBefore = -1);
    void addSeparator(int insertBefore = -1);

    void onWmNotify(LPARAM lParam);

    void enableCommand(int command, bool enable = true);

private:
    int buttonCount;
    HBITMAP hBackground;
    HIMAGELIST hImglBtn;
    HWND hwnd;
};

#endif //TOOLBAR_H
