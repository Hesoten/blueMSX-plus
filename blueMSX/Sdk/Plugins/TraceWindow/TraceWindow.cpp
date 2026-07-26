#include <windows.h>
#include "ToolInterface.h"
#include "Resource.h"
#include "Language.h"
#include "Win32TextUtf8.h"
#include <string>
#include <commctrl.h>
#include <sstream>
#include <iomanip>

using namespace std;

static HWND dbgHwnd = NULL;
static HWND hwndEdit = NULL;
static FILE* logFile = NULL;
static DWORD charCount = 0;
static LanguageId langId = LID_ENGLISH;

#define IDEDITCTL           100

#define MENU_FILE_EXIT              37100
#define MENU_FILE_LOG               37101
#define MENU_EDIT_SELECTALL         37200
#define MENU_EDIT_COPY              37201
#define MENU_EDIT_CLEAR             37202
#define MENU_HELP_ABOUT             37400

#define FONT_MIN      6
#define FONT_MAX     24
#define FONT_DEFAULT 10

static int   fontPoints = FONT_DEFAULT;
static HFONT hFont = NULL;
static WNDPROC editWndProcOrig = NULL;

/* The profile API resolves a bare file name against the Windows directory,
** so build an explicit path in the working directory instead. */
static const char* traceIniPath()
{
    static char path[MAX_PATH] = "";

    if (path[0] == 0) {
        /* On overflow the call returns the required size and writes nothing,
        ** so a non-zero result is only usable when it fits the buffer. */
        DWORD len = GetCurrentDirectoryA(sizeof(path) - 32, path);
        if (len == 0 || len >= sizeof(path) - 32) {
            strcpy(path, ".");
        }
        strcat(path, "\\tracewindow.ini");
    }
    return path;
}

static void applyFont()
{
    if (hwndEdit == NULL) {
        return;
    }

    HDC hdc = GetDC(hwndEdit);
    int height = -MulDiv(fontPoints, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(hwndEdit, hdc);

    HFONT hNew = CreateFont(height, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
    SendMessage(hwndEdit, WM_SETFONT, (WPARAM)hNew, TRUE);
    if (hFont) DeleteObject(hFont);
    hFont = hNew;
}

static void setFontPoints(int points)
{
    if (points < FONT_MIN) points = FONT_MIN;
    if (points > FONT_MAX) points = FONT_MAX;
    if (points == fontPoints) {
        return;
    }
    fontPoints = points;
    applyFont();
}

/* Handle Ctrl+plus / Ctrl+minus / Ctrl+0 / Ctrl+wheel; non-zero if consumed. */
static int fontZoomMessage(UINT iMsg, WPARAM wParam)
{
    if (GetKeyState(VK_CONTROL) >= 0) {
        return 0;
    }

    if (iMsg == WM_MOUSEWHEEL) {
        setFontPoints(fontPoints + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1));
        return 1;
    }

    if (iMsg == WM_KEYDOWN) {
        switch (wParam) {
        case VK_OEM_PLUS:
        case VK_ADD:
            setFontPoints(fontPoints + 1);
            return 1;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            setFontPoints(fontPoints - 1);
            return 1;
        case '0':
        case VK_NUMPAD0:
            setFontPoints(FONT_DEFAULT);
            return 1;
        }
    }
    return 0;
}

static LRESULT CALLBACK editWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (fontZoomMessage(iMsg, wParam)) {
        return 0;
    }
    return CallWindowProc(editWndProcOrig, hwnd, iMsg, wParam, lParam);
}

static void updateWindowMenu()
{
    HMENU hMenuFile = CreatePopupMenu();
    
    AppendMenuU(hMenuFile, MF_STRING, MENU_FILE_LOG, logFile == NULL ? Language::menuFileLogToFile : Language::menuFileStopLogToFile);
    AppendMenuU(hMenuFile, MF_SEPARATOR, 0, NULL);
    AppendMenuU(hMenuFile, MF_STRING, MENU_FILE_EXIT, Language::menuFileExit);
    
    HMENU hMenuEdit = CreatePopupMenu();
    AppendMenuU(hMenuEdit, MF_STRING, MENU_EDIT_SELECTALL, Language::menuEditSelectAll);
    AppendMenuU(hMenuEdit, MF_STRING, MENU_EDIT_COPY, Language::menuEditCopy);
    AppendMenuU(hMenuEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuU(hMenuEdit, MF_STRING, MENU_EDIT_CLEAR, Language::menuEditClearWindow);

    HMENU hMenuHelp = CreatePopupMenu();
    AppendMenuU(hMenuHelp, MF_STRING, MENU_HELP_ABOUT, Language::menuHelpAbout);

    static HMENU hMenu = NULL;
    if (hMenu != NULL) {
        DestroyMenu(hMenu);
    }

    hMenu = CreateMenu();
    AppendMenuU(hMenu, MF_POPUP, (UINT_PTR)hMenuFile, Language::menuFile);
    AppendMenuU(hMenu, MF_POPUP, (UINT_PTR)hMenuEdit, Language::menuEdit);
    AppendMenuU(hMenu, MF_POPUP, (UINT_PTR)hMenuHelp, Language::menuHelp);
    
    SetMenu(dbgHwnd, hMenu);
}

void openLogFile(HWND hwndOwner)
{
    char pFileName[MAX_PATH];
    pFileName[0] = 0; 

    /* IFileDialog via newer host: UTF-8 paths, dark-mode-aware. */
    if (!ShellSaveFileDialog(hwndOwner, Language::openWindowCaption,
                             "Text Files (*.txt)\0*.TXT\0All Files\0*.*\0\0",
                             NULL, "txt", NULL, pFileName, sizeof(pFileName))) {
        return; 
    }

    logFile = fopenU(pFileName, "wb");
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static HBRUSH hBrush = NULL;

    if (fontZoomMessage(iMsg, wParam)) {
        return 0;
    }

    switch (iMsg) {
    case WM_CREATE:
        if (hBrush == NULL) {
            hBrush = CreateSolidBrush(RGB(255, 255, 255));
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case MENU_FILE_EXIT:
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;

        case MENU_HELP_ABOUT:
            {
                char text[512];
                sprintf(text, "%s\r\n\r\n%s: " __DATE__ "\r\n\r\n%s    \r\n\r\n\r\n",
                    Language::traceWindowCaption, Language::aboutBuilt, Language::aboutVisit);
                MessageBoxU(NULL, text, Language::traceWindowCaption, MB_ICONINFORMATION | MB_OK);
            }
            return 0;

        case MENU_EDIT_CLEAR:
            charCount = 0;
            SendMessage(hwndEdit, EM_SETSEL, 0, INT_MAX-1);
            SendMessage(hwndEdit, EM_REPLACESEL, 0, (LPARAM)"");
            return 0;

        case MENU_EDIT_SELECTALL:
            SendMessage(hwndEdit, EM_SETSEL, 0, INT_MAX-1);
            return 0;

        case MENU_EDIT_COPY:
            SendMessage(hwndEdit, WM_COPY, 0, 0);
            return 0;

        case MENU_FILE_LOG:
            if (logFile != NULL) {
                fclose(logFile);
            }
            else {
                openLogFile(hwnd);
            }
            updateWindowMenu();
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
        return (LRESULT)hBrush;

    case WM_SIZE:
        if (hwndEdit != NULL) {
            RECT r;
            GetClientRect(dbgHwnd, &r);
            SetWindowPos(hwndEdit, NULL, r.left, r.top, r.right, r.bottom, SWP_NOZORDER);
        }
        break;
        
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        {
            char text[16];
            sprintf(text, "%d", fontPoints);
            WritePrivateProfileStringA("Trace Window", "font size", text, traceIniPath());
        }
        if (logFile != NULL) {
            fclose(logFile);
        }
        if (hFont) { DeleteObject(hFont); hFont = NULL; }
        logFile = NULL;
        hwndEdit = NULL;
        dbgHwnd = NULL;
		break;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

void OnCreateTool() {
    WNDCLASSEX wndClass;

    wndClass.cbSize         = sizeof(wndClass);
    wndClass.style          = 0;
    wndClass.lpfnWndProc    = wndProc;
    wndClass.cbClsExtra     = 0;
    wndClass.cbWndExtra     = 0;
    wndClass.hInstance      = GetDllHinstance();
    wndClass.hIcon          = NULL;
    wndClass.hIconSm        = NULL;
    wndClass.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground  = NULL;
    wndClass.lpszMenuName   = NULL;
    wndClass.lpszClassName  = "TraceWindow";

    RegisterClassEx(&wndClass);
    }

void OnDestroyTool() {
    if (dbgHwnd != NULL) {
        DestroyWindow(dbgHwnd);
    }
}

void OnShowTool() {
    if (dbgHwnd != NULL) {
        if (IsIconic(dbgHwnd)) {
            ShowWindow(dbgHwnd, SW_RESTORE);
        }
        SetForegroundWindow(dbgHwnd);
        return;
    }

    Language::SetLanguage(langId);

    charCount = 0;

    dbgHwnd = CreateWindow("TraceWindow", NULL,
                           WS_OVERLAPPEDWINDOW, 
                           CW_USEDEFAULT, CW_USEDEFAULT, 600, 440, NULL, NULL, GetDllHinstance(), NULL);
    SetWindowTextU(dbgHwnd, Language::traceWindowCaption);

    void* hEditDS = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, 256L);
    if (hEditDS == NULL) {
        hEditDS = GetDllHinstance();
    }
   
    hwndEdit = CreateWindow("edit", NULL,
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_HSCROLL | WS_VSCROLL | ES_MULTILINE |
        ES_AUTOHSCROLL | ES_READONLY | ES_AUTOVSCROLL,
        10, 10, 250, 200, dbgHwnd, (HMENU)IDEDITCTL, (HINSTANCE)hEditDS, NULL);

    SendMessage(hwndEdit, EM_LIMITTEXT, 0, 0);

    editWndProcOrig = (WNDPROC)SetWindowLongPtr(hwndEdit, GWLP_WNDPROC, (LONG_PTR)editWndProc);
    fontPoints = GetPrivateProfileIntA("Trace Window", "font size", FONT_DEFAULT, traceIniPath());
    if (fontPoints < FONT_MIN) fontPoints = FONT_MIN;
    if (fontPoints > FONT_MAX) fontPoints = FONT_MAX;
    applyFont();

    /* Default to a comfortable trace-friendly size, DPI-scaled so it fills
    ** roughly the same visual footprint on 100% / 150% / 200% monitors. */
    {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        HMODULE huser32 = GetModuleHandleW(L"user32.dll");
        PFN_GetDpiForWindow pGetDpi = huser32
            ? (PFN_GetDpiForWindow)GetProcAddress(huser32, "GetDpiForWindow")
            : NULL;
        UINT dpi = pGetDpi ? pGetDpi(dbgHwnd) : 96;
        int w = MulDiv(900, dpi, 96);
        int h = MulDiv(640, dpi, 96);
        SetWindowPos(dbgHwnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
    }

    /* ApplyDarkMode after children are in place so EnumChildWindows finds
    ** hwndEdit and applies SetWindowTheme(DarkMode_Explorer) -- that is what
    ** darkens the edit-control scrollbars on Win10/11. */
    ApplyDarkMode(dbgHwnd);

    ShowWindow(dbgHwnd, TRUE);

    updateWindowMenu();
}

void OnEmulatorStart() {
}

void OnEmulatorStop() {
}

void OnEmulatorPause() {
}

void OnEmulatorResume() {
}

void OnEmulatorReset() {
}

void OnEmulatorTrace(const char* message)
{
    // Convert buffer from unix to dos format for printing
    char buffer[256];
    int index = 0;
    for (; message[0] != 0 && index < 254; message++) {
        if (message[0] == '\r' && message[1] == '\n') {
            continue; // Add \r later
        }
        if (message[0] == '\n') {
            buffer[index++] = '\r';
        }
        buffer[index++] = message[0];
    }
    buffer[index] = 0;
    
    if (logFile != NULL) {
        fwrite(buffer, 1, strlen(buffer), logFile);
    }

    if (hwndEdit != NULL) {
        DWORD start;
        DWORD end;
        SendMessage(hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);

        if (charCount >= 32000) {
            SendMessage(hwndEdit, EM_SETSEL, 0, 1000);
            SendMessage(hwndEdit, EM_REPLACESEL, 0, (LPARAM)"");
            SendMessage(hwndEdit, EM_SETSEL, 0, INT_MAX-1);
            SendMessage(hwndEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&charCount);
        }

        charCount += index;

        SendMessage(hwndEdit, EM_SETSEL, INT_MAX, INT_MAX-1);
        SendMessage(hwndEdit, EM_REPLACESEL, 0, (LPARAM)buffer);
        SendMessage(hwndEdit, EM_SETSEL, start, end);
    }
}

void OnEmulatorSetBreakpoint(UInt16 address) {
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 address) {
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address) {
}

const char* OnGetName() {
    return Language::traceWindowName;
}

void OnSetLanguage(LanguageId languageId)
{
    langId = languageId;
    Language::SetLanguage(langId);
}
