/*****************************************************************************
** File:        Toolbar.cpp
**
** Author:      Daniel Vik
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
#include "ToolBar.h"

#ifndef BTNS_BUTTON
#define BTNS_BUTTON     TBSTYLE_BUTTON
#endif
#ifndef BTNS_DROPDOWN
#define BTNS_DROPDOWN   TBSTYLE_DROPDOWN
#endif
#ifndef BTNS_SEP
#define BTNS_SEP        TBSTYLE_SEP
#endif

/* DPI-scale toolbar icons: 18-px source strips are too small at 150%/200% --
** stretch via CopyImage before pushing into the ImageList. */
static UINT toolbarDpi(HWND hRef)
{
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow pGetDpi = NULL;
    static BOOL resolved = FALSE;
    if (!resolved) {
        HMODULE h = GetModuleHandleW(L"user32.dll");
        if (h) pGetDpi = (PFN_GetDpiForWindow)GetProcAddress(h, "GetDpiForWindow");
        resolved = TRUE;
    }
    UINT dpi = (pGetDpi && hRef) ? pGetDpi(hRef) : 96;
    return dpi > 0 ? dpi : 96;
}

Toolbar::Toolbar(HINSTANCE hInstance, HWND owner, int bitmapId, COLORREF transparentColor, int backgroundId) :
    hBackground(NULL)
{
    INITCOMMONCONTROLSEX icex;
    HBITMAP   hBtn;
 
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    if (backgroundId != -1) {
        hBackground = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(backgroundId), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    }

    UINT dpi = toolbarDpi(owner);
    /* 27 = 18 source * 1.5 magnification; bump for visibility on modern
    ** 4K / 200% monitors where the original 18-px icons are too small. */
    int iconSz = MulDiv(27, dpi, 96);

    hImglBtn = ImageList_Create(iconSz, iconSz, ILC_COLOR24 | ILC_MASK, 3, 1);
    /* Probe native dimensions so we can compute the stretched size that
    ** preserves aspect ratio (source is 18-px tall, N icons across). */
    HBITMAP hProbe = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(bitmapId), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
    BITMAP bm = {0};
    if (hProbe) GetObject(hProbe, sizeof(bm), &bm);
    if (hProbe) DeleteObject(hProbe);
    int srcRows = bm.bmHeight ? bm.bmHeight : 18;
    int srcCols = bm.bmWidth  ? bm.bmWidth  : 18;
    int newH = iconSz;
    int newW = MulDiv(srcCols, iconSz, srcRows);

    /* cxDesired/cyDesired stretch via nearest-neighbor (BLACKONWHITE) for
    ** 24-bit bitmaps, preserving transparent pixels for ImageList_AddMasked. */
    hBtn = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(bitmapId), IMAGE_BITMAP,
                              newW, newH, LR_DEFAULTCOLOR);
    if (!hBtn) {
        /* Fall back to native size if stretching fails. */
        hBtn = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(bitmapId), IMAGE_BITMAP,
                                  0, 0, LR_DEFAULTCOLOR);
    }
    ImageList_AddMasked(hImglBtn, hBtn, transparentColor);
    DeleteObject(hBtn);

    hwnd = CreateWindowEx(0, TOOLBARCLASSNAME, (LPSTR) NULL, 
        WS_CHILD | TBSTYLE_TOOLTIPS | WS_BORDER | TBSTYLE_FLAT | CCS_ADJUSTABLE, 
        0, 0, 0, 0, owner, (HMENU)12029, hInstance, NULL); 

    SendMessage(hwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0); 
    SendMessage(hwnd, TB_SETSTYLE, 0, SendMessage(hwnd, TB_GETSTYLE, 0,0 ) & ~TBSTYLE_TRANSPARENT);

    /* Order matters: attach the ImageList before declaring the bitmap /
    ** button size, otherwise the toolbar autosize sees no image and stays
    ** at the text-only height. */
    SendMessage(hwnd, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hImglBtn);
    SendMessage(hwnd, TB_SETBITMAPSIZE, 0, MAKELONG(iconSz, iconSz));
    SendMessage(hwnd, TB_SETBUTTONSIZE, 0, MAKELONG(iconSz + 6, iconSz + 6));
    SendMessage(hwnd, TB_SETEXTENDEDSTYLE, (WPARAM)0, TBSTYLE_EX_DRAWDDARROWS );
    
    /* Switch the auto-created tooltip control to Unicode so the parent's
    ** TTN_GETDISPINFOW handler delivers UTF-8-converted Japanese strings
    ** instead of mojibaked CP932 from the ANSI default. */
    HWND hTip = (HWND)SendMessage(hwnd, TB_GETTOOLTIPS, 0, 0);
    if (hTip) SendMessage(hTip, CCM_SETUNICODEFORMAT, TRUE, 0);
}

Toolbar::~Toolbar()
{
    if (hBackground != NULL) {
        DeleteObject(hBackground);
    }
    ImageList_Destroy(hImglBtn);
    DestroyWindow(hwnd);
}

void Toolbar::addButton(int bitmap, int command, int dropdown, int insertBefore)
{
    if (insertBefore == -1) {
        /* Zero-init: leaving TBBUTTON.iString uninitialised makes the toolbar
        ** treat the random stack value as a string-table index and paint a
        ** garbage label under each icon. Force iString = -1 (no string). */
        TBBUTTON button = {0};
        button.iBitmap   = bitmap;
        button.idCommand = command; 
        button.fsState   = TBSTATE_ENABLED;
        button.fsStyle   = dropdown ? BTNS_DROPDOWN : 0;
        button.iString   = 0;
        buttons.push_back(button);
        SendMessage(hwnd, TB_INSERTBUTTON, buttons.size() - 1, (LPARAM)(LPTBBUTTON)&button);
    }
}

void Toolbar::addSeparator(int insertBefore)
{
    if (insertBefore == -1) {
        TBBUTTON button = {0};
        button.fsState = TBSTATE_ENABLED; 
        button.fsStyle = BTNS_SEP; 
        button.iString = -1;
        buttons.push_back(button);
        SendMessage(hwnd, TB_INSERTBUTTON, buttons.size() - 1, (LPARAM)(LPTBBUTTON)&button);
    }
}

void Toolbar::show()
{
    /* TB_AUTOSIZE only ran in the constructor before buttons existed, so the
    ** toolbar kept its initial (icon-clipping) height. Re-run autosize now
    ** that addButton has populated the strip. */
    SendMessage(hwnd, TB_AUTOSIZE, 0, 0L);
    ShowWindow(hwnd, true);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void Toolbar::hide()
{
    ShowWindow(hwnd, false);
}

int Toolbar::getHeight()
{
    if (!IsWindowVisible(hwnd)) {
        return 0;
    }
    RECT r;
    GetWindowRect(hwnd, &r);
    return r.bottom - r.top - 1;
}

void Toolbar::updatePosition()
{
    RECT parentRect;
    RECT rect;

    GetClientRect(GetParent(hwnd), &parentRect);
    GetWindowRect(hwnd, &rect);

    SetWindowPos(hwnd, NULL, 0, 0, parentRect.left - parentRect.right, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
}

void Toolbar::enableItem(int item, bool enable)
{
    SendMessage(hwnd, TB_DELETEBUTTON, item, 0);

    buttons[item].fsState = enable ? TBSTATE_ENABLED : 0;
    SendMessage(hwnd, TB_INSERTBUTTON, item, (LPARAM)(LPTBBUTTON)&(buttons[item]));
}

void Toolbar::disableItem(int item)
{
    enableItem(item, false);
}

void Toolbar::onWmNotify(LPARAM lParam)
{
    if (hBackground == NULL) {
        return;
    }

    LPNMTBCUSTOMDRAW lptbcd = (NMTBCUSTOMDRAW *)lParam;

    if (lptbcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
        HDC hdc = lptbcd->nmcd.hdc;
        BITMAP bm;
        HBITMAP hBitmap;
        RECT r;

        HDC hMemDC = CreateCompatibleDC(hdc);
        hBitmap = (HBITMAP)SelectObject(hMemDC, hBackground);
        GetObject(hBackground, sizeof(BITMAP), (PSTR)&bm);

        GetClientRect(hwnd, &r);
        int width = r.right - r.left;
        int height = r.bottom - r.top;
        for (int i = 0; i < width; i += bm.bmWidth) {
            BitBlt(hdc, i, 0, bm.bmWidth, height, hMemDC, 0, 0, SRCCOPY);
        }
        SelectObject(hMemDC, hBitmap);
        DeleteDC(hMemDC);
    }
}
