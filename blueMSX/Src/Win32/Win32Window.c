/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32Window.c,v $
**
** $Revision: 1.23 $
**
** $Date: 2008-05-09 17:21:04 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************
*/
#define DIRECTINPUT_VERSION     0x0700

#include <windows.h>
#include <commctrl.h>     /* TRACKMOUSEEVENT for slider hover tooltip */
#include "MsxTypes.h"
#include "Win32Common.h"
#include "Win32Keyboard.h"
#include "Win32File.h"
#include "Win32Menu.h"
#include "Win32TextUtf8.h"
#include "Theme.h"
#include "Machine.h"
#include "Properties.h"
#include "ArchNotifications.h"
#include "ArchMenu.h"
#include "Language.h"
#include "Resource.h"
#include "InputEvent.h"
#include "JoystickPort.h"

// Set current window for handling of minimize events, close events, ...
extern void SetCurrentWindow(HWND hwnd);

// Timer ID's
#define TIMER_STATUSBAR_UPDATE              10
#define TIMER_POLL_INPUT                    11
#define TIMER_THEME                         17
#define TIMER_CLIP_REGION                   19


// Custom Window Control Messages

#define WM_UPDATE                   (WM_USER + 1245)

#define WM_OBJECT_BASE              (WM_USER + 1300)

#define WM_DROPDOWN_KEYBOARDCONFIG  (WM_OBJECT_BASE + 31)
#define WM_DROPDOWN_THEMEPAGES      (WM_OBJECT_BASE + 32)
#define WM_DROPDOWN_MACHINECONFIG   (WM_OBJECT_BASE + 33)

#define WM_BUTTON_OK                (WM_OBJECT_BASE + 1)
#define WM_BUTTON_CANCEL            (WM_OBJECT_BASE + 2)
#define WM_BUTTON_SAVE              (WM_OBJECT_BASE + 3)
#define WM_BUTTON_SAVEAS            (WM_OBJECT_BASE + 4)
#define WM_BUTTON_CLOSE             (WM_OBJECT_BASE + 5)

#define WM_CLOSE_RESULT_OK      0xefdf0012
#define WM_CLOSE_RESULT_CANCEL  0xefdf0013

#define WM_OBJECT_CONTOL_BASE       (WM_USER + 1400)

#define WM_OBJECT_UPDATE            (WM_OBJECT_CONTOL_BASE + 1)
#define WM_OBJECT_SHOW              (WM_OBJECT_CONTOL_BASE + 2)
#define WM_OBJECT_ENABLE            (WM_OBJECT_CONTOL_BASE + 3)
#define WM_OBJECT_GET               (WM_OBJECT_CONTOL_BASE + 4)


static void objectShow(HWND parent, int notifyId, int show);
static void objectEnable(HWND parent, int notifyId, int enable);
static void objectUpdate(HWND parent, int notifyId, LPARAM arg);
static LRESULT objectGet(HWND parent, int notifyId);


//////////////////////////////////////////////////////////////////////////
// Methods to manage object window data
//
typedef struct {
    HWND  hwnd;
    int   id;
    void* data;
} WindowData;

#define WINDOW_DATA_NO 1024

WindowData windowData[WINDOW_DATA_NO];


//////////////////////////////////////////////////////////////////////////
/// Function:
///     windowDataSet
///
/// Description:
///     Associates a pointer with a windows handle. If the data pointer
///     is NULL, the association for the window handle is removed.
//////////////////////////////////////////////////////////////////////////
static void windowDataSet(HWND hwnd, int id, void* data)
{
    if (id != 0) {
        int i;
        for (i = 0; i < WINDOW_DATA_NO - 1; i++) {
            if (windowData[i].hwnd == hwnd || windowData[i].hwnd == NULL) {
                windowData[i].hwnd = hwnd;
                windowData[i].id   = id;
                windowData[i].data = data;
                return;
            }
        }
    }
    else {
        int i;
        for (i = 0; windowData[i].hwnd != NULL; i++) {
            if (windowData[i].hwnd == hwnd) {
                do {
                    windowData[i] = windowData[i + 1];
                    i++;
                } while (windowData[i].hwnd != NULL);
                return;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// Function:
///     windowDataGet
///
/// Description:
///     Gets a pointer associated with a window handle. If no association
///     exist for the handle, NULL is returned.
//////////////////////////////////////////////////////////////////////////
static void* windowDataGet(HWND hwnd)
{
    int i;
    for (i = 0; windowData[i].hwnd != NULL; i++) {
        if (windowData[i].hwnd == hwnd) {
            return windowData[i].data;
        }
    }
    return NULL;
}


//////////////////////////////////////////////////////////////////////////
/// Struct:
///     WindowInfo
///
/// Description:
///     Contains window specific data
//////////////////////////////////////////////////////////////////////////
typedef struct WindowInfo {
    HWND hwnd;
    int  isMinimized;
    int  isMoving;
    Theme* theme;
    
    HBITMAP hBitmap;
    HDC hdc;
    
    HRGN     hrgn;
    int      clipAlways;
    int      rgnSize;
    RGNDATA* rgnData;
    int      rgnEnable;

    HWND     hwndSliderTip;   /* lazily created on first slider hover */
} WindowInfo;

/* AdjustWindowRectExForDpi-based frame metrics; SM_CXFIXEDFRAME under-
   counts on Win10/11 PerMonitor DPI for WS_DLGFRAME, clipping the
   theme bitmap. */
static void windowFrameMetrics(HWND hwnd, int clientW, int clientH,
                               int* offX, int* offY,
                               int* outerW, int* outerH)
{
    DWORD style   = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    RECT  rc      = { 0, 0, clientW, clientH };

    typedef BOOL (WINAPI *PFN_AdjustForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
    typedef UINT (WINAPI *PFN_GetDpi)(HWND);
    static PFN_AdjustForDpi pAdjust = (PFN_AdjustForDpi)(LONG_PTR)-1;
    static PFN_GetDpi       pGetDpi = (PFN_GetDpi)(LONG_PTR)-1;
    if (pAdjust == (PFN_AdjustForDpi)(LONG_PTR)-1) {
        HMODULE u32 = GetModuleHandleA("user32.dll");
        pAdjust = u32 ? (PFN_AdjustForDpi)GetProcAddress(u32, "AdjustWindowRectExForDpi") : NULL;
        pGetDpi = u32 ? (PFN_GetDpi)GetProcAddress(u32, "GetDpiForWindow") : NULL;
    }

    if (pAdjust && pGetDpi) {
        UINT dpi = pGetDpi(hwnd);
        if (!dpi) dpi = 96;
        pAdjust(&rc, style, FALSE, exStyle, dpi);
    } else {
        AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    }
    if (offX)   *offX   = -rc.left;
    if (offY)   *offY   = -rc.top;
    if (outerW) *outerW = rc.right - rc.left;
    if (outerH) *outerH = rc.bottom - rc.top;
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     windowSetClipRegion
///
/// Description:
///     Enables/disables the clip region for a window
//////////////////////////////////////////////////////////////////////////
static void windowSetClipRegion(WindowInfo* wi, int enable) 
{
    if (wi->rgnEnable == enable) {
        return;
    }
    if ((!enable || wi->rgnData == NULL || wi->isMoving) && !wi->clipAlways) {
        SetWindowRgn(wi->hwnd, NULL, TRUE);
        wi->rgnEnable = 0;
    }
    else {
        HRGN hrgn = ExtCreateRegion(NULL, wi->rgnSize, wi->rgnData);
        SetWindowRgn(wi->hwnd, hrgn, TRUE);
        wi->rgnEnable = 1;
    }
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     windowUpdateClipRegion
///
/// Description:
///     Updates the clip region
//////////////////////////////////////////////////////////////////////////
static void windowUpdateClipRegion(WindowInfo* wi) 
{
    if (wi->rgnData != NULL && wi->hrgn != NULL) {
        POINT pt;
        RECT r;

        GetCursorPos(&pt);
        GetWindowRect(wi->hwnd, &r);
        windowSetClipRegion(wi, !PtInRegion(wi->hrgn, pt.x - r.left, pt.y - r.top));
    }
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     windowCheckClipRegion
///
/// Description:
///     Checks the mouse position relative to the clip region and updates
///     the clip region timer if necessary
//////////////////////////////////////////////////////////////////////////
static void windowCheckClipRegion(WindowInfo* wi) 
{
    if (wi->rgnData != NULL && wi->hrgn != NULL && !wi->clipAlways) {
        POINT pt;
        RECT r;

        GetCursorPos(&pt);
        GetWindowRect(wi->hwnd, &r);
        if (wi->rgnEnable == !PtInRegion(wi->hrgn, pt.x - r.left, pt.y - r.top)) {
            SetTimer(wi->hwnd, TIMER_CLIP_REGION, 500, NULL);
        }
    }
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     windowCreateClipRegion
///
/// Description:
///     Creates a clip region for a window based on the theme config
//////////////////////////////////////////////////////////////////////////
static void windowCreateClipRegion(WindowInfo* wi)
{
    ThemePage* themePage = themeGetCurrentPage(wi->theme);
    int clipCount = themePage->clipPoint.count;
    wi->clipAlways = themePage->noFrame;

    if (clipCount > 0 || wi->clipAlways) {
        int i;
        HRGN hrgn;
        POINT pt[512];
        int dx, dy;
        windowFrameMetrics(wi->hwnd, themePage->width, themePage->height,
                           &dx, &dy, NULL, NULL);

        if (clipCount == 0) {
            pt[0].x = 0 + dx;
            pt[0].y = 0 + dy;
            pt[1].x = themePage->width + dx;
            pt[1].y = 0 + dy;
            pt[2].x = themePage->width + dx;
            pt[2].y = themePage->height + dy;
            pt[3].x = 0 + dx;
            pt[3].y = themePage->height + dy;
            clipCount = 4;
        }
        else {
            for (i = 0; i < clipCount; i++) {
                ClipPoint cp = themePage->clipPoint.list[i];
                pt[i].x = cp.x + dx;
                pt[i].y = cp.y + dy;
            }
        }

        hrgn = CreatePolygonRgn(pt, clipCount, WINDING);
        wi->rgnSize = 0;
        if (hrgn != NULL) {
            wi->rgnSize = GetRegionData(hrgn, 0, NULL);
            if (wi->rgnSize > 0) {
                wi->rgnData = malloc(wi->rgnSize);
                wi->rgnSize = GetRegionData(hrgn, wi->rgnSize, wi->rgnData);
                if (wi->rgnSize == 0) {
                    free(wi->rgnData);
                    wi->rgnData = NULL;
                }
            }
            if (wi->rgnSize == 0) {
                wi->rgnData = NULL;
            }
            else {
                int width, height;
                windowFrameMetrics(wi->hwnd, themePage->width, themePage->height,
                                   NULL, NULL, &width, &height);

                if (wi->hrgn) { DeleteObject(wi->hrgn); wi->hrgn=NULL; }
                wi->hrgn = CreateRectRgn(0, 0, width, height);
                CombineRgn(wi->hrgn, wi->hrgn, hrgn, RGN_XOR);
            }
            DeleteObject(hrgn);
        }
    }

    wi->rgnEnable = -1;
    windowSetClipRegion(wi, clipCount > 0);

    if (wi->rgnData == NULL) {
        KillTimer(wi->hwnd, TIMER_CLIP_REGION);
    }
    else {
        SetTimer(wi->hwnd, TIMER_CLIP_REGION, 500, NULL);
    }
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     keyboardDlgProc
///
/// Description:
///     Specialized window handler for keyboard configuration windows
//////////////////////////////////////////////////////////////////////////
static LRESULT CALLBACK keyboardDlgProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    WindowInfo* wi = windowDataGet(hwnd);

    switch (iMsg) {
    case WM_CREATE:
        keyboardStartConfig();
        objectUpdate(hwnd, WM_DROPDOWN_KEYBOARDCONFIG, (LPARAM)keyboardGetCurrentConfig());
        SetTimer(hwnd, TIMER_POLL_INPUT, 500, NULL);
        return 0;

    case WM_TIMER:
        switch(wParam) {
        case TIMER_POLL_INPUT:
            objectEnable(hwnd, WM_BUTTON_SAVE, !keyboardIsCurrentConfigDefault() && keyboardConfigIsModified());
            break;
        }
        break;

    case WM_BUTTON_CLOSE:
        SendMessage(hwnd, WM_CLOSE, 0, 0);
        break;

    case WM_DROPDOWN_KEYBOARDCONFIG:
        {
            char* name = (char*)lParam;
            if (name != NULL) {
                keyboardLoadConfig(name);
            }
        }
        break;

    case WM_BUTTON_OK:
        if (keyboardConfigIsModified()) {
            keyboardSaveConfig(keyboardGetCurrentConfig());
        }
        
        SendMessage(hwnd, WM_CLOSE, 0, 0);
        break;

    case WM_ACTIVATE:
        keyboardSetFocus(2, LOWORD(wParam) != WA_INACTIVE);
        keybardEnableEdit(LOWORD(wParam) != WA_INACTIVE);
        if (LOWORD(wParam) != WA_INACTIVE) {
            inputReset(hwnd);
        }
        break;

    case WM_BUTTON_CANCEL:
        SendMessage(hwnd, WM_CLOSE, 0, 0);
        break;

    case WM_CLOSE:
        if (keyboardConfigIsModified()) {
            if (IDNO == MessageBoxU(NULL, langWarningDiscardChanges(), langWarningTitle(), MB_ICONWARNING | MB_YESNO)) {
                return WM_CLOSE_RESULT_CANCEL;
            }
        }
        KillTimer(hwnd, TIMER_POLL_INPUT);
        keyboardCancelConfig();
        keybardEnableEdit(0);
        keyboardSetFocus(2, 0);
        {
            char* name = (char*)objectGet(hwnd, WM_DROPDOWN_KEYBOARDCONFIG);
            if (name != NULL) {
                keyboardLoadConfig(name);
            }
        }
        return WM_CLOSE_RESULT_OK;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     windowProc
///
/// Description:
///     Generic window handler for a themed window
//////////////////////////////////////////////////////////////////////////
static LRESULT CALLBACK windowProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    WindowInfo* wi = windowDataGet(hwnd);
    LRESULT rv = 0;

    switch (iMsg) {
    case WM_CREATE:
        {
            ThemePage* themePage;
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;

            wi = (WindowInfo*)cs->lpCreateParams;
            windowDataSet(hwnd, 1, wi);

            wi->hwnd = hwnd;
            themePage = themeGetCurrentPage(wi->theme);
            SendMessage(hwnd, WM_UPDATE, 0, 0);

            ShowWindow(hwnd, TRUE); 
            
            SetTimer(hwnd, TIMER_STATUSBAR_UPDATE, 100, NULL);
        }
        break;

    case WM_DROPDOWN_THEMEPAGES:
        if (wi != NULL) {
            themeSetPageFromHash(wi->theme, themeGetNameHash((char*)lParam));
            SendMessage(hwnd, WM_UPDATE, 0, 0);
        }
        return 0;

    case WM_CLOSE:
        // Special handling at the end
        break;

    case WM_ENTERSIZEMOVE:
        if (wi != NULL) {
            wi->isMoving = 1;
        }
        break;

    case WM_EXITSIZEMOVE:
        if (wi != NULL) {
            wi->isMoving = 0;
            windowUpdateClipRegion(wi);
        }
        break;

    case WM_TIMER:
        if (wi != NULL) {
            switch(wParam) {
            case TIMER_STATUSBAR_UPDATE: {
                HDC hdc = GetDC(hwnd);
                themePageUpdate(themeGetCurrentPage(wi->theme), hdc);
                ReleaseDC(hwnd, hdc);
                break;
            }
            case TIMER_CLIP_REGION:
                windowUpdateClipRegion(wi);
                break;
            case TIMER_THEME:
                if (!wi->isMinimized) {
                    POINT pt;
                    RECT r;
                    HDC hdc;

                    GetCursorPos(&pt);
                    GetWindowRect(hwnd, &r);

                    if (!PtInRect(&r, pt)) {
                        KillTimer(hwnd, TIMER_THEME);
                    }

                    ScreenToClient(hwnd, &pt);

                    hdc = GetDC(hwnd);
                    themePageMouseMove(themeGetCurrentPage(wi->theme), hdc, pt.x, pt.y);
                    ReleaseDC(hwnd, hdc);
                }
                break;
            }
        }
        break;

    case WM_ACTIVATE:
        if (wi != NULL) {
            HDC hdc = GetDC(hwnd);
            ThemePage* themePage = themeGetCurrentPage(wi->theme);
            themePageSetActive(themePage, hdc, LOWORD(wParam) != WA_INACTIVE);
            ReleaseDC(hwnd, hdc);
        }
        if (LOWORD(wParam) != WA_INACTIVE) {
            inputReset(hwnd);
        }
        break;

    case WM_UPDATE:
        if (wi != NULL) {
            ThemePage* themePage = themeGetCurrentPage(wi->theme);
            int width;
            int height;

            windowFrameMetrics(hwnd, themePage->width, themePage->height,
                               NULL, NULL, &width, &height);
            
            if (wi->hBitmap) { DeleteObject(wi->hBitmap); wi->hBitmap=NULL; }
            if (wi->hdc) { ReleaseDC(hwnd,wi->hdc); wi->hdc=NULL; }
            wi->hdc=GetDC(hwnd);
            wi->hBitmap = CreateCompatibleBitmap(wi->hdc, width, height);
            themePageActivate(themePage, NULL);

            SetWindowPos(hwnd, NULL, 0, 0, width, height, 
                         SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);

            themePageActivate(themePage, hwnd);
            windowCreateClipRegion(wi);

            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_COMMAND:
        if (menuCommand(propGetGlobalProperties(), LOWORD(wParam))) {
            archUpdateMenu(0);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;

    case WM_NCMOUSEMOVE:
        if (wi != NULL) {
            windowCheckClipRegion(wi);
        }
        break;
        
    case WM_MOUSEMOVE:
        archWindowMove();
        if (wi != NULL) {
            ThemePage* themePage = themeGetCurrentPage(wi->theme);
            HDC hdc = GetDC(hwnd);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            themePageMouseMove(themePage, hdc, pt.x, pt.y);
            win32SliderTooltipUpdate(&wi->hwndSliderTip, hwnd,
                                     themePageHoverSliderPercent(themePage, pt.x, pt.y));
            ReleaseDC(hwnd, hdc);
            windowCheckClipRegion(wi);
            /* Request WM_MOUSELEAVE so the slider tooltip is hidden when
               the cursor exits the window. */
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }
        SetTimer(hwnd, TIMER_THEME, 250, NULL);

        break;

    case WM_MOUSELEAVE:
        if (wi != NULL) {
            win32SliderTooltipUpdate(&wi->hwndSliderTip, hwnd, -1);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (wi != NULL) {
            ThemePage* themePage = themeGetCurrentPage(wi->theme);
            HDC hdc = GetDC(hwnd);
            POINT pt;
            SetCapture(hwnd);
            SetCurrentWindow(hwnd);
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            themePageMouseButtonDown(themePage, hdc, pt.x, pt.y);
            ReleaseDC(hwnd, hdc);
        }
        break;

    case WM_LBUTTONUP:
        if (wi != NULL) {
            ThemePage* themePage = themeGetCurrentPage(wi->theme);
            HDC hdc = GetDC(hwnd);
            POINT pt;
            
            ReleaseCapture();
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            themePageMouseButtonUp(themePage, hdc, pt.x, pt.y);
            ReleaseDC(hwnd, hdc);
            SetCurrentWindow(NULL);
        }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps); 
            if (wi != NULL) {
                HDC hMemDC = CreateCompatibleDC(hdc);
                HBITMAP hBitmap = (HBITMAP)SelectObject(hMemDC, wi->hBitmap);
                ThemePage* themePage = themeGetCurrentPage(wi->theme);
                themePageUpdate(themePage, hMemDC); //OWN DC
                themePageDraw(themePage, hMemDC, NULL);

                BitBlt(hdc, 0, 0, themePage->width, themePage->height, hMemDC, 0, 0, SRCCOPY);
                SelectObject(hMemDC, hBitmap);
                DeleteDC(hMemDC);                
            }            

            EndPaint(hwnd, &ps);
        }
        return 0;
    }

    wi = windowDataGet(hwnd);
    if (wi && wi->theme->themeHandler == TH_KBDCONFIG) {
        rv = keyboardDlgProc(hwnd, iMsg, wParam, lParam);
    }
    else {
        rv = iMsg == WM_CLOSE ? 0 : DefWindowProc(hwnd, iMsg, wParam, lParam);
    }

    if (iMsg == WM_CLOSE) {
        if (rv != WM_CLOSE_RESULT_CANCEL) {
            KillTimer(hwnd, TIMER_STATUSBAR_UPDATE);
            windowDataSet(hwnd, 0, NULL);
            themePageActivate(themeGetCurrentPage(wi->theme), NULL);
            if (wi->hrgn) { DeleteObject(wi->hrgn); wi->hrgn=NULL; }
            if (wi->hBitmap) { DeleteObject(wi->hBitmap); wi->hBitmap=NULL; }
            if (wi->hdc) { ReleaseDC(hwnd,wi->hdc); wi->hdc=NULL; }
            wi->theme->reference = NULL;
            free(wi);
            wi = NULL;
            DestroyWindow(hwnd);
            SetActiveWindow(getMainHwnd());
        }
        rv = 0;
    }

    return iMsg == WM_CREATE ? 0 : rv;
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     archWindowCreate
///
/// Description:
///     Creates a window based on the theme configuration
///
///     Always created unowned; archWindowApplyOwnership applies the
///     mode-appropriate owner.  childWindow is kept for ABI but ignored.
//////////////////////////////////////////////////////////////////////////
void* archWindowCreate(Theme* theme, int childWindow) 
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WindowInfo* wi;
    wchar_t wTitle[128];

    (void)childWindow;

    static int initialized = 0;
    if (!initialized) {
        static WNDCLASSEXW wndClass;
        wndClass.cbSize         = sizeof(wndClass);
        wndClass.style          = CS_OWNDC;
        wndClass.lpfnWndProc    = windowProc;
        wndClass.cbClsExtra     = 0;
        wndClass.cbWndExtra     = 0;
        wndClass.hInstance      = hInstance;
        wndClass.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
        wndClass.hIconSm        = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BLUEMSX));
        wndClass.hCursor        = LoadCursor(NULL, IDC_ARROW);
        wndClass.hbrBackground  = NULL;
        wndClass.lpszMenuName   = NULL;
        wndClass.lpszClassName  = L"blueMSX Popup";

        RegisterClassExW(&wndClass);

        initialized = 1;
    }

    wi = calloc(1, sizeof(WindowInfo));
    wi->theme = theme;
    Utf8ToWide(theme->name, wTitle, _countof(wTitle));
    return CreateWindowW(L"blueMSX Popup", wTitle,
                        WS_OVERLAPPED | WS_CLIPCHILDREN | WS_BORDER | WS_DLGFRAME |
                        WS_SYSMENU | WS_MINIMIZEBOX,
                        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL, NULL,
                        hInstance, wi);
}

void archWindowApplyOwnership(void* p)
{
    HWND hwnd = (HWND)p;
    Properties* pProperties;
    HWND newOwner;
    HWND currentOwner;
    HWND zPos;
    BOOL isFullscreen;

    if (hwnd == NULL) {
        return;
    }
    pProperties = propGetGlobalProperties();
    isFullscreen = (pProperties != NULL &&
                    pProperties->video.windowSize == P_VIDEO_SIZEFULLSCREEN);
    /* Fullscreen: owned by main so the aux floats above it; windowed:
    ** unowned independent window. */
    newOwner = isFullscreen ? getMainHwnd() : NULL;
    /* The fullscreen main is HWND_TOPMOST, so the aux must join the same
    ** topmost group or it sinks below the main (= invisible). */
    zPos = isFullscreen ? HWND_TOPMOST : HWND_NOTOPMOST;

    currentOwner = (HWND)GetWindowLongPtr(hwnd, GWLP_HWNDPARENT);
    if (currentOwner != newOwner) {
        /* GWLP_HWNDPARENT only settles reliably while the window is
        ** hidden; the hide/show dance is skipped on no-change so mass
        ** refresh doesn't flicker every open aux window. */
        BOOL wasVisible = IsWindowVisible(hwnd);
        if (wasVisible) {
            ShowWindow(hwnd, SW_HIDE);
        }
        SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, (LONG_PTR)newOwner);
        if (wasVisible) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
    }

    SetWindowPos(hwnd, zPos, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static BOOL CALLBACK reownEnumProc(HWND hwnd, LPARAM lParam)
{
    wchar_t className[64];
    (void)lParam;
    /* All aux theme windows share the "blueMSX Popup" class; the class
    ** filter is what makes a desktop-wide EnumWindows walk safe. */
    if (GetClassNameW(hwnd, className, _countof(className)) > 0 &&
        wcscmp(className, L"blueMSX Popup") == 0) {
        archWindowApplyOwnership(hwnd);
    }
    return TRUE;
}

void archWindowApplyOwnershipAll(void)
{
    EnumWindows(reownEnumProc, 0);
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     objectUpdate
///
/// Description:
///     Updates the child windows of parent with type notifyId 
//////////////////////////////////////////////////////////////////////////
static void objectUpdate(HWND parent, int notifyId, LPARAM arg)
{
    int i;
    for (i = 0; windowData[i].hwnd != NULL; i++) {
        if (GetParent(windowData[i].hwnd) == parent && windowData[i].id == notifyId) {
            SendMessage(windowData[i].hwnd, WM_OBJECT_UPDATE, 0, arg);
        }
    }
}

static LRESULT objectGet(HWND parent, int notifyId)
{
    /* Returns LRESULT so callers that cast the result to char* / void*
    ** (via DWLP_MSGRESULT, e.g. keyboardDlgProc::WM_CLOSE) keep the
    ** upper 32 bits intact on x64. */
    int i;
    for (i = 0; windowData[i].hwnd != NULL; i++) {
        if (GetParent(windowData[i].hwnd) == parent && windowData[i].id == notifyId) {
            return SendMessage(windowData[i].hwnd, WM_OBJECT_GET, 0, 0);
        }
    }
    return 0;
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     objectShow
///
/// Description:
///     Shows/hides the child windows of parent with type notifyId 
//////////////////////////////////////////////////////////////////////////
static void objectShow(HWND parent, int notifyId, int show)
{
    int i;
    for (i = 0; windowData[i].hwnd != NULL; i++) {
        if (GetParent(windowData[i].hwnd) == parent && windowData[i].id == notifyId) {
            SendMessage(windowData[i].hwnd, WM_OBJECT_SHOW, 0, show);
        }
    }
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     objectEnable
///
/// Description:
///     Enables/disables the child windows of parent with type notifyId 
//////////////////////////////////////////////////////////////////////////
static void objectEnable(HWND parent, int notifyId, int enable)
{
    int i;
    for (i = 0; windowData[i].hwnd != NULL; i++) {
        if (GetParent(windowData[i].hwnd) == parent && windowData[i].id == notifyId) {
            SendMessage(windowData[i].hwnd, WM_OBJECT_ENABLE, 0, enable);
        }
    }
}



//////////////////////////////////////////////////////////////////////////
/// Struct:
///     DropdownInfo
///
/// Description:
///     Contains dropdown menu specific data
//////////////////////////////////////////////////////////////////////////
typedef struct {
    int x;
    int y;
    int width;
    int height;
    int notifyId;
    Theme* theme;
    char text[64];
} DropdownInfo;


//////////////////////////////////////////////////////////////////////////
/// Function:
///     dropdownProc
///
/// Description:
///     Window handler for a dropdown menu controls
//////////////////////////////////////////////////////////////////////////
static BOOL_DLG_RET CALLBACK dropdownProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    DropdownInfo* oi;

    switch (iMsg) {
    case WM_INITDIALOG:
        oi = (DropdownInfo*)malloc(sizeof(DropdownInfo));
        *oi = *(DropdownInfo*)lParam;
        SetWindowPos(hwnd, NULL, oi->x, oi->y, oi->width, oi->height, SWP_NOZORDER | SWP_SHOWWINDOW);
        SetWindowPos(GetDlgItem(hwnd, IDC_CONTROL), NULL, 0, 0, oi->width, 96, SWP_NOZORDER);
        windowDataSet(hwnd, oi->notifyId, oi);
        SendMessage(hwnd, WM_OBJECT_UPDATE, 0, 0);
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CONTROL) {
            static int isChanging = 0;
            if (isChanging == 0 && HIWORD(wParam) == CBN_SELCHANGE) {
                char sel[64];
                int idx;
                int rv;

                isChanging = 1;

                idx = SendMessage(GetDlgItem(hwnd, IDC_CONTROL), CB_GETCURSEL, 0, 0);
                rv = SendMessage(GetDlgItem(hwnd, IDC_CONTROL), CB_GETLBTEXT, idx, (LPARAM)sel);
                if (rv != CB_ERR) {
                    oi = (DropdownInfo*)windowDataGet(hwnd);
                    SendMessage(GetParent(hwnd), (UINT)oi->notifyId, 0, (LPARAM)sel);
                }
                isChanging = 0;
            }
        }
        return TRUE;
    case WM_CLOSE:
        oi = (DropdownInfo*)windowDataGet(hwnd);
        windowDataSet(hwnd, 0, NULL);
        free(oi);
        break;

    case WM_OBJECT_GET:
        {
            static char buffer[512];
            int idx = SendDlgItemMessage(hwnd, IDC_CONTROL, CB_GETCURSEL, 0, 0);
            int rv = SendDlgItemMessage(hwnd, IDC_CONTROL, CB_GETLBTEXT, idx, (LPARAM)buffer);
            if (rv != CB_ERR) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, (LRESULT)(LPVOID)buffer);
                return TRUE;
            }
        }
        SetWindowLongPtr(hwnd, DWLP_MSGRESULT, 0);
        return TRUE;

    case WM_OBJECT_UPDATE:
        while (CB_ERR != SendDlgItemMessage(hwnd, IDC_CONTROL, CB_DELETESTRING, 0, 0));

        oi = (DropdownInfo*)windowDataGet(hwnd);
        if (lParam != 0) {
            strcpy(oi->text, (char*)lParam);
        }
        {
            char** items = { NULL };
            int index = 0;

            switch (oi->notifyId) {
            case WM_DROPDOWN_MACHINECONFIG:
                {
                    ArrayList *machineList;
					ArrayListIterator *iterator;

					machineList = arrayListCreate();
                    machineFillAvailable(machineList, 1);

                    iterator = arrayListCreateIterator(machineList);
                    while (arrayListCanIterate(iterator)) {
                        char *machineInList = (char *)arrayListIterate(iterator);
                        ComboAddStringU(GetDlgItem(hwnd, IDC_CONTROL), machineInList);

                        if (index == 0 || 0 == strcmp(machineInList, oi->text))
                            SendDlgItemMessage(hwnd, IDC_CONTROL, CB_SETCURSEL, index, 0);
                        index++;
                    }
                    arrayListDestroyIterator(iterator);

                    arrayListDestroy(machineList);
                }
                break;
            case WM_DROPDOWN_KEYBOARDCONFIG:
                items = keyboardGetConfigs();
                break;
            case WM_DROPDOWN_THEMEPAGES:
                items = themeGetPageNames((Theme*)oi->theme);
                break;
            }

            while (*items != NULL) {
                ComboAddStringU(GetDlgItem(hwnd, IDC_CONTROL), *items);

                if (index == 0 || 0 == strcmp(*items, oi->text)) {
                    SendDlgItemMessage(hwnd, IDC_CONTROL, CB_SETCURSEL, index, 0);
                }
                items++;
                index++;
            }
        }
        break;
    case WM_OBJECT_SHOW:
        ShowWindow(hwnd, lParam);
        break;
    case WM_OBJECT_ENABLE:
        EnableWindow(hwnd, lParam);
        break;
    }
    return FALSE;
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     objectDropdownCreate
///
/// Description:
///     Function to create dropdown menu controls (from within themes)
//////////////////////////////////////////////////////////////////////////
static void* objectDropdownCreate(HWND hwnd, char* id, int x, int y, int width, int height, LONG_PTR arg1, LONG_PTR arg2)
{
    DropdownInfo oi = { x, y, width, height, 0, 0 };

    if (0 == strcmp(id, "dropdown-keyconfigs")) {
        oi.text[0] = 0;
        oi.notifyId = WM_DROPDOWN_KEYBOARDCONFIG;
    }
    
    if (0 == strcmp(id, "dropdown-machineconfigs")) {
        oi.text[0] = 0;
        oi.notifyId = WM_DROPDOWN_MACHINECONFIG;
    }

    if (0 == strcmp(id, "dropdown-themepages")) {
        Theme* theme = (Theme*)arg1;
        strcpy(oi.text, themeGetPageName(theme, themeGetCurrentPageIndex(theme)));
        oi.notifyId = WM_DROPDOWN_THEMEPAGES;
        oi.theme    = (Theme*)arg1;
    }
    
    if (oi.notifyId == 0) {
        return NULL;
    }

    return CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DROPDOWN), hwnd, dropdownProc, (LPARAM)&oi);
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     objectDropdownDestroy
///
/// Description:
///     Function to destroy dropdown menu controls
//////////////////////////////////////////////////////////////////////////
static void objectDropdownDestroy(void* object) 
{
    DestroyWindow((HWND)object);
}



//////////////////////////////////////////////////////////////////////////
/// Struct:
///     ButtonInfo
///
/// Description:
///     Contains button control specific data
//////////////////////////////////////////////////////////////////////////
typedef struct {
    int x;
    int y;
    int width;
    int height;
    char* text;
    int notifyId;
} ButtonInfo;


//////////////////////////////////////////////////////////////////////////
/// Function:
///     dropdownProc
///
/// Description:
///     Window handler for a button controls
//////////////////////////////////////////////////////////////////////////
static BOOL_DLG_RET CALLBACK buttonProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static ButtonInfo* oi;

    switch (iMsg) {
    case WM_INITDIALOG:
        oi = (ButtonInfo*)lParam;
        SetWindowPos(hwnd, NULL, oi->x, oi->y, oi->width, oi->height, SWP_NOZORDER | SWP_SHOWWINDOW);
        SetWindowPos(GetDlgItem(hwnd, IDC_CONTROL), NULL, 0, 0, oi->width, oi->height, SWP_NOZORDER);
        SetWindowTextU(GetDlgItem(hwnd, IDC_CONTROL), oi->text);
        /* Stash notifyId in the void* slot.  Cast through UINT_PTR so x64
        ** does not warn about int<->pointer size mismatch (the message id
        ** is always small enough to fit). */
        windowDataSet(hwnd, oi->notifyId, (void*)(UINT_PTR)oi->notifyId);
        return FALSE;
    case WM_COMMAND:
        if (wParam == IDC_CONTROL) {
            SendMessage(GetParent(hwnd), (UINT)(UINT_PTR)windowDataGet(hwnd), 0, 0);
        }
        return TRUE;
    case WM_CLOSE:
        windowDataSet(hwnd, 0, NULL);
        break;
    case WM_OBJECT_SHOW:
        ShowWindow(hwnd, lParam);
        break;
    case WM_OBJECT_ENABLE:
        EnableWindow(GetDlgItem(hwnd, IDC_CONTROL), lParam);
        break;
    }
    return FALSE;
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     objectButtonCreate
///
/// Description:
///     Function to create button controls (from within themes)
//////////////////////////////////////////////////////////////////////////
static void* objectButtonCreate(HWND hwnd, char* id, int x, int y, int width, int height)
{
    ButtonInfo oi = { x, y, width, height, NULL, 0};
    
    if (0 == strcmp(id, "button-close")) {
        oi.text     = langDlgClose();
        oi.notifyId = WM_BUTTON_CLOSE;
    }
    if (0 == strcmp(id, "button-ok")) {
        oi.text     = langDlgOK();
        oi.notifyId = WM_BUTTON_OK;
    }
    if (0 == strcmp(id, "button-cancel")) {
        oi.text = langDlgCancel();
        oi.notifyId = WM_BUTTON_CANCEL;
    }
    if (0 == strcmp(id, "button-save")) {
        oi.text = langDlgSave();
        oi.notifyId = WM_BUTTON_SAVE;
    }
    if (0 == strcmp(id, "button-saveas")) {
        oi.text = langDlgSaveAs();
        oi.notifyId = WM_BUTTON_SAVEAS;
    }
    
    if (oi.notifyId == 0) {
        return NULL;
    }

    return CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_BUTTON), hwnd, buttonProc, (LPARAM)&oi);
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     objectButtonDestroy
///
/// Description:
///     Function to destroy button controls
//////////////////////////////////////////////////////////////////////////
static void objectButtonDestroy(void* object) 
{
    DestroyWindow((HWND)object);
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     archObjectCreate
///
/// Description:
///     Creates a control based on the id string. The method is used to
///     create host specific controls from the themes.
//////////////////////////////////////////////////////////////////////////
void* archObjectCreate(char* id, void* window, int x, int y, int width, int height, LONG_PTR arg1, LONG_PTR arg2)
{
    if (0 == strncmp(id, "button-", 7)) {
        return objectButtonCreate(window, id, x, y, width, height);
    }
    if (0 == strncmp(id, "dropdown-", 9)) {
        return objectDropdownCreate(window, id, x, y, width, height, arg1, arg2);
    }
    return NULL;
}


//////////////////////////////////////////////////////////////////////////
/// Function:
///     archObjectDestroy
///
/// Description:
///     Destroys a control based on the id string.
//////////////////////////////////////////////////////////////////////////
void archObjectDestroy(char* id, void* object)
{
    if (0 == strncmp(id, "button-", 7)) {
        objectButtonDestroy(object);
    }
    if (0 == strncmp(id, "dropdown-", 9)) {
        objectDropdownDestroy(object);
    }
}

