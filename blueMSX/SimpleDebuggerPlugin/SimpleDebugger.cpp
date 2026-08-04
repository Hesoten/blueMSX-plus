#include <windows.h>
#include "ToolInterface.h"
#include "Resource.h"
#include "StatusBar.h"
#include "Toolbar.h"
#include "Disassembly.h"
#include "Callstack.h"
#include "Breakpoints.h"
#include "Stack.h"
#include "CpuRegisters.h"
#include "SymbolInfo.h"
#include "Memory.h"
#include "PeripheralRegs.h"
#include "IoPorts.h"
#include "Language.h"
#include "IniFileParser.h"
#include "InputDialogs.h"
#include "resrc1.h"
#include "Win32TextUtf8.h"
#include <string>
#include <commctrl.h>
#include <sstream>
#include <iomanip>

using namespace std;

/* DPI scaling helper: returns the input pixel value scaled for the window's
** monitor DPI. Falls back to 96 DPI on pre-1607 Windows. */
static int dpiScale(HWND hRef, int px96)
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
    if (dpi <= 0) dpi = 96;
    return MulDiv(px96, dpi, 96);
}

static int x;
static int y;
static int width;
static int height;
static HWND dbgHwnd = NULL;
static HWND viewHwnd = NULL;
static StatusBar* statusBar = NULL;
static Toolbar* toolBar = NULL;
static Disassembly* disassembly = NULL;
static StackWindow* stack = NULL;
static CallstackWindow* callstack = NULL;
static Breakpoints* breakpoints = NULL;
static CpuRegisters* cpuRegisters = NULL;
static InputDialogs* inputDialogs = NULL;
static PeripheralRegs* periRegisters = NULL;
static IoPortWindow* ioPorts = NULL;
static SymbolInfo* symbolInfo = NULL;
static Memory* memory = NULL;
static LanguageId langId = LID_ENGLISH;
static int vramCheckAccess = 0;

#define WM_STATUS (WM_USER + 1797)

#define TB_RESUME    37000
#define TB_PAUSE     37001
#define TB_STOP      37002
#define TB_RUN       37003
#define TB_SHOWNEXT  37004
#define TB_STEPIN    37005
#define TB_STEPOUT   37006
#define TB_STEPOVER  37007
#define TB_RUNTO     37008
#define TB_BPTOGGLE  37009
#define TB_BPENABLE  37010
#define TB_BPENALL   37011
#define TB_BPDISALL  37012
#define TB_BPREMALL  37013
#define TB_STEPBACK  37014

HWND getRootHwnd() {
    return dbgHwnd;
}

void handleKeyboardInput(WPARAM wParam)
{
    HWND hwnd = dbgHwnd;
    if (hwnd == NULL) {
        return;
    }
    UInt32 mod = ( GetAsyncKeyState(VK_MENU)    > 1 ? MOD_ALT     : 0 ) | 
                 ( GetAsyncKeyState(VK_SHIFT)   > 1 ? MOD_SHIFT   : 0 ) | 
                 ( GetAsyncKeyState(VK_CONTROL) > 1 ? MOD_CONTROL : 0 );
    UInt32 key = wParam & 0xff;
    
    if ( mod ==  0                        && key == VK_F5)     SendMessage(hwnd, WM_HOTKEY,  1, 0);
    if ( mod == (MOD_CONTROL | MOD_ALT)   && key == VK_CANCEL) SendMessage(hwnd, WM_HOTKEY,  2, 0);
    if ( mod ==  MOD_SHIFT                && key == VK_F5)     SendMessage(hwnd, WM_HOTKEY,  3, 0);
    if ( mod == (MOD_CONTROL | MOD_SHIFT) && key == VK_F5)     SendMessage(hwnd, WM_HOTKEY,  4, 0);
    if ( mod ==  0                        && key == VK_F11)    SendMessage(hwnd, WM_HOTKEY,  5, 0);
    if ( mod ==  0                        && key == VK_F10)    SendMessage(hwnd, WM_HOTKEY,  6, 0);
    if ( mod ==  MOD_SHIFT                && key == VK_F11)    SendMessage(hwnd, WM_HOTKEY,  7, 0);
    if ( mod ==  MOD_SHIFT                && key == VK_F10)    SendMessage(hwnd, WM_HOTKEY,  8, 0);
    if ( mod ==  0                        && key == VK_F9)     SendMessage(hwnd, WM_HOTKEY,  9, 0);
    if ( mod ==  MOD_SHIFT                && key == VK_F9)     SendMessage(hwnd, WM_HOTKEY, 10, 0);
    if ( mod == (MOD_CONTROL | MOD_SHIFT) && key == VK_F9)     SendMessage(hwnd, WM_HOTKEY, 11, 0);
    if ( mod ==  0                        && key == VK_F8)     SendMessage(hwnd, WM_HOTKEY, 12, 0);
    if ( mod ==  MOD_CONTROL              && key == 'G')       SendMessage(hwnd, WM_HOTKEY, 13, 0);
    if ( mod ==  MOD_CONTROL              && key == 'M')       SendMessage(hwnd, WM_HOTKEY, 14, 0);
    if ( mod ==  MOD_CONTROL              && key == 'B')       SendMessage(hwnd, WM_HOTKEY, 15, 0);
    if ( mod ==  MOD_CONTROL              && key == 'F')       SendMessage(hwnd, WM_HOTKEY, 16, 0);
    if ( mod ==  0                        && key == VK_F3)     SendMessage(hwnd, WM_HOTKEY, 17, 0);
    if ( mod ==  0                        && key == VK_HOME)   SendMessage(hwnd, WM_HOTKEY, 18, 0);
    if ( mod ==  MOD_CONTROL              && key == 'V')       SendMessage(hwnd, WM_HOTKEY, 19, 0);
    if ( mod ==  MOD_CONTROL              && key == VK_F11)    SendMessage(hwnd, WM_HOTKEY, 20, 0);
    if ( mod ==  MOD_CONTROL              && key == 'W')       SendMessage(hwnd, WM_HOTKEY, 21, 0);
}

/* Translations are data, never a printf format -- a stray %s in any language
** file would otherwise read a non-existent argument. */
static void updateTooltip(int id, char* str)
{
    switch (id) {
    case TB_RESUME:   sprintf(str, "%s", Language::toolbarResume);        break;
    case TB_PAUSE:    sprintf(str, "%s", Language::toolbarPause);         break;
    case TB_STOP:     sprintf(str, "%s", Language::toolbarStop);          break;
    case TB_RUN:      sprintf(str, "%s", Language::toolbarRun);           break;
    case TB_SHOWNEXT: sprintf(str, "%s", Language::toolbarShowNext);      break;
    case TB_STEPIN:   sprintf(str, "%s", Language::toolbarStepIn);        break;
    case TB_STEPBACK: sprintf(str, "%s", Language::toolbarStepBack);      break;
    case TB_STEPOVER: sprintf(str, "%s", Language::toolbarStepOver);      break;
    case TB_STEPOUT:  sprintf(str, "%s", Language::toolbarStepOut);       break;
    case TB_RUNTO:    sprintf(str, "%s", Language::toolbarRunTo);         break;
    case TB_BPTOGGLE: sprintf(str, "%s", Language::toolbarBpToggle);      break;
    case TB_BPENABLE: sprintf(str, "%s", Language::toolbarBpEnable);      break;
    case TB_BPENALL:  sprintf(str, "%s", Language::toolbarBpEnableAll);   break;
    case TB_BPDISALL: sprintf(str, "%s", Language::toolbarBpDisableAll);  break;
    case TB_BPREMALL: sprintf(str, "%s", Language::toolbarBpRemoveAll);   break;
    }
}

static Toolbar* initializeToolbar(HWND owner)
{
    Toolbar* toolBar = new Toolbar(GetDllHinstance(), owner, IDB_TOOLBAR, RGB(239, 237, 222));

    toolBar->addSeparator();
    toolBar->addButton(0,  TB_RESUME);
    toolBar->addButton(1,  TB_PAUSE);
    toolBar->addButton(2,  TB_STOP);
    toolBar->addButton(3,  TB_RUN);
    toolBar->addSeparator();
    toolBar->addButton(4,  TB_SHOWNEXT);
    toolBar->addButton(15, TB_STEPBACK);
    toolBar->addButton(5,  TB_STEPIN);
    toolBar->addButton(6,  TB_STEPOVER);
    toolBar->addButton(8,  TB_STEPOUT);
    toolBar->addButton(7,  TB_RUNTO);
    toolBar->addSeparator();
    toolBar->addButton(12, TB_BPTOGGLE);
    toolBar->addButton(13, TB_BPENABLE);
    toolBar->addButton(11, TB_BPENALL);
    toolBar->addButton(10, TB_BPDISALL);
    toolBar->addButton(9,  TB_BPREMALL);

    return toolBar;
}

static void updateToolBar()
{
    if (toolBar == NULL) {
        return;
    }

    EmulatorState state = GetEmulatorState();
    
    toolBar->enableCommand(TB_RESUME,   state != EMULATOR_RUNNING);
    toolBar->enableCommand(TB_PAUSE,    state == EMULATOR_RUNNING);
    toolBar->enableCommand(TB_STOP,     state != EMULATOR_STOPPED);
    toolBar->enableCommand(TB_RUN,      true);

    toolBar->enableCommand(TB_SHOWNEXT, state == EMULATOR_PAUSED);
    toolBar->enableCommand(TB_STEPBACK, state == EMULATOR_PAUSED);
    toolBar->enableCommand(TB_STEPIN,   state == EMULATOR_PAUSED);
    toolBar->enableCommand(TB_STEPOVER, state == EMULATOR_PAUSED);
    toolBar->enableCommand(TB_STEPOUT,  state == EMULATOR_PAUSED && callstack->getReturnAddress() >= 0);
    toolBar->enableCommand(TB_RUNTO,    state == EMULATOR_PAUSED && disassembly->isCursorPresent());

    toolBar->enableCommand(TB_BPTOGGLE, state != EMULATOR_STOPPED && disassembly->isCursorPresent());
    toolBar->enableCommand(TB_BPENABLE, state != EMULATOR_STOPPED && disassembly->isBpOnCcursor());
    toolBar->enableCommand(TB_BPENALL,  breakpoints->getDisabledBpCount() > 0);
    toolBar->enableCommand(TB_BPDISALL, breakpoints->getEnabledBpCount() > 0);
    toolBar->enableCommand(TB_BPREMALL, breakpoints->getEnabledBpCount() || breakpoints->getDisabledBpCount());
}

static void updateStatusBar()
{
    if (statusBar == NULL) {
        return;
    }

    switch (GetEmulatorState()) {
    case EMULATOR_RUNNING:
        statusBar->setField(1, Language::statusRunning);
        break;
    case EMULATOR_PAUSED:
        statusBar->setField(1, Language::statusPaused);
        break;
    case EMULATOR_STOPPED:
        statusBar->setField(1, Language::statusStopped);
        break;
    }
}

#define MENU_FILE_EXIT              37100
#define MENU_FILE_LOADSYM           37101
#define MENU_FILE_SAVEDASM          37102
#define MENU_FILE_SAVEMEM           37103
#define MENU_FILE_REPLACESYM        37104

#define MENU_DEBUG_CONTINUE         37200
#define MENU_DEBUG_BREAKALL         37201
#define MENU_DEBUG_STOP             37202
#define MENU_DEBUG_RESTART          37203
#define MENU_DEBUG_STEP             37204
#define MENU_DEBUG_STEP_OVER        37205
#define MENU_DEBUG_STEP_OUT         37206
#define MENU_DEBUG_RUNTO            37207
#define MENU_DEBUG_SHOWSYMBOLS      37208
#define MENU_DEBUG_GOTO             37209
#define MENU_DEBUG_BPTOGGLE         37210
#define MENU_DEBUG_BPENABLE         37211
#define MENU_DEBUG_BPREMOVEALL      37212
#define MENU_DEBUG_BPENABLEALL      37213
#define MENU_DEBUG_BPDISABLEALL     37214
#define MENU_DEBUG_FLAGMODE         37215
#define MENU_DEBUG_SETBP            37216
#define MENU_DEBUG_FIND             37217
#define MENU_DEBUG_FINDNEXT         37218
#define MENU_DEBUG_CHECK_VRAM       37219
#define MENU_DEBUG_STEP_BACK        37220
#define MENU_DEBUG_SETWP            37221

#define MENU_WINDOW_DISASSEMBLY     37300
#define MENU_WINDOW_CPUREGISTERS    37301
#define MENU_WINDOW_STACK           37302
#define MENU_WINDOW_CALLSTACK       37303
#define MENU_WINDOW_MEMORY          37304
#define MENU_WINDOW_PERIREGISTERS   37305
#define MENU_WINDOW_IOPORTS         37306
#define MENU_WINDOW_BREAKPOINTS     37307

#define MENU_HELP_ABOUT             37400

/* Sticky for the session: whether loading a symbol file discards the symbols
** already in memory or adds to them. */
static BOOL replaceSymbols = TRUE;

static void updateWindowMenu()
{
    static char buf[128];

    EmulatorState state = GetEmulatorState();

    HMENU hMenuFile = CreatePopupMenu();

    sprintf(buf, "%s", Language::menuFileLoadSymbolFile);
    AppendMenuU(hMenuFile, MF_STRING, MENU_FILE_LOADSYM, buf);

    /* Was a checkbox on the old-style open dialog; the shared shell dialog
    ** takes no custom controls, so the option lives in the menu now. */
    sprintf(buf, "%s", Language::symbolWindowText);
    AppendMenuU(hMenuFile, MF_STRING | (replaceSymbols ? MF_CHECKED : 0), MENU_FILE_REPLACESYM, buf);

    AppendMenuU(hMenuFile, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s", Language::menuFileSaveDisassembly);
    AppendMenuU(hMenuFile, MF_STRING, MENU_FILE_SAVEDASM, buf);

    sprintf(buf, "%s", Language::menuFileSaveMemory);
    AppendMenuU(hMenuFile, MF_STRING, MENU_FILE_SAVEMEM, buf);
    
    AppendMenuU(hMenuFile, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s", Language::menuFileExit);
    AppendMenuU(hMenuFile, MF_STRING, MENU_FILE_EXIT, buf);

    HMENU hMenuDebug = CreatePopupMenu();
    if (state == EMULATOR_STOPPED) {
        sprintf(buf, "%s\tF5", Language::menuDebugStart);
        AppendMenuU(hMenuDebug, MF_STRING, MENU_DEBUG_CONTINUE, buf);
    }
    else {
        sprintf(buf, "%s\tF5", Language::menuDebugContinue);
        AppendMenuU(hMenuDebug, MF_STRING | (state != EMULATOR_RUNNING ? 0 : MF_GRAYED), MENU_DEBUG_CONTINUE, buf);
        sprintf(buf, "%s\tCtrl+Alt+Break", Language::menuDebugBreakAll);
        AppendMenuU(hMenuDebug, MF_STRING | (state == EMULATOR_RUNNING ? 0 : MF_GRAYED), MENU_DEBUG_BREAKALL, buf);
        sprintf(buf, "%s\tShift+F5", Language::menuDebugStop);
        AppendMenuU(hMenuDebug, MF_STRING                                              , MENU_DEBUG_STOP,     buf);
        sprintf(buf, "%s\tCtrl+Shift+F5", Language::menuDebugRestart);
        AppendMenuU(hMenuDebug, MF_STRING                                              , MENU_DEBUG_RESTART,  buf);
    }
    sprintf(buf, "%s\tCtrl+F11", Language::menuDebugStepBack);
    AppendMenuU(hMenuDebug, MF_STRING | (state == EMULATOR_PAUSED                  ? 0 : MF_GRAYED), MENU_DEBUG_STEP_BACK, buf);
    sprintf(buf, "%s\tF11", Language::menuDebugStepIn);
    AppendMenuU(hMenuDebug, MF_STRING | (state == EMULATOR_PAUSED                  ? 0 : MF_GRAYED), MENU_DEBUG_STEP, buf);
    sprintf(buf, "%s\tF10", Language::menuDebugStepOver);
    AppendMenuU(hMenuDebug, MF_STRING | (state == EMULATOR_PAUSED                  ? 0 : MF_GRAYED), MENU_DEBUG_STEP_OVER, buf);
    sprintf(buf, "%s\tShift+F11", Language::menuDebugStepOut);
    AppendMenuU(hMenuDebug, MF_STRING | (state == EMULATOR_PAUSED && callstack->getReturnAddress() >= 0? 0 : MF_GRAYED), MENU_DEBUG_STEP_OUT, buf);

    sprintf(buf, "%s\tShift+F10", Language::menuDebugRunTo);
    AppendMenuU(hMenuDebug, MF_STRING | (state == EMULATOR_PAUSED && disassembly->isCursorPresent() ? 0 : MF_GRAYED), MENU_DEBUG_RUNTO, buf);

    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s\tF8", Language::menuDebugShowSymbols);
    AppendMenuU(hMenuDebug, MF_STRING | (symbolInfo->getShowStatus() ? MF_CHECKED : 0), MENU_DEBUG_SHOWSYMBOLS, buf);

    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s\tCtrl+F", Language::menuDebugFind);
    AppendMenuU(hMenuDebug, MF_STRING | (1 ? 0 : MF_GRAYED), MENU_DEBUG_FIND, buf);

    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s\tCtrl+G", Language::menuDebugGoto);
    AppendMenuU(hMenuDebug, MF_STRING | (1 ? 0 : MF_GRAYED), MENU_DEBUG_GOTO, buf);

    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);
    
    sprintf(buf, "%s\tCtrl+B", Language::menuDebugBpAdd);
    AppendMenuU(hMenuDebug, MF_STRING | (state != EMULATOR_STOPPED && disassembly->isCursorPresent() ? 0 : MF_GRAYED), MENU_DEBUG_SETBP, buf);
    
    sprintf(buf, "%s\tF9", Language::menuDebugBpToggle);
    AppendMenuU(hMenuDebug, MF_STRING | (state != EMULATOR_STOPPED && disassembly->isCursorPresent() ? 0 : MF_GRAYED), MENU_DEBUG_BPTOGGLE, buf);
    sprintf(buf, "%s\tShift+F9", Language::menuDebugEnable);
    AppendMenuU(hMenuDebug, MF_STRING | (state != EMULATOR_STOPPED && disassembly->isBpOnCcursor() ? 0 : MF_GRAYED), MENU_DEBUG_BPENABLE, buf);

    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s\tCtrl+W", Language::menuDebugWpAdd);
    AppendMenuU(hMenuDebug, MF_STRING | (state != EMULATOR_STOPPED && disassembly->isCursorPresent() ? 0 : MF_GRAYED), MENU_DEBUG_SETWP, buf);

    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s\tCtrl+Shift+F9", Language::menuDebugRemoveAll);
    AppendMenuU(hMenuDebug, MF_STRING | (breakpoints->getEnabledBpCount() || breakpoints->getDisabledBpCount() ? 0 : MF_GRAYED), MENU_DEBUG_BPREMOVEALL, buf);
    sprintf(buf, "%s", Language::menuDebugEnableAll);
    AppendMenuU(hMenuDebug, MF_STRING | (breakpoints->getDisabledBpCount() ? 0 : MF_GRAYED), MENU_DEBUG_BPENABLEALL, buf);
    sprintf(buf, "%s", Language::menuDebugDisableAll);
    AppendMenuU(hMenuDebug, MF_STRING | (breakpoints->getEnabledBpCount() ? 0 : MF_GRAYED), MENU_DEBUG_BPDISABLEALL, buf);


    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s\tCtrl+V", Language::menuDebugFastVram);
    AppendMenuU(hMenuDebug, MF_STRING | (vramCheckAccess ? MF_CHECKED : 0), MENU_DEBUG_CHECK_VRAM, buf);

    AppendMenuU(hMenuDebug, MF_SEPARATOR, 0, NULL);

    sprintf(buf, "%s\tCtrl+M", Language::menuDebugShowAssemblyFlags);
    AppendMenuU(hMenuDebug, MF_STRING | (cpuRegisters->getFlagMode() == CpuRegisters::FM_CPU ? 0 : MF_CHECKED), MENU_DEBUG_FLAGMODE, buf);

    
    HMENU hMenuWindow = CreatePopupMenu();

    sprintf(buf, "%s", Language::windowDisassembly);
    AppendMenuU(hMenuWindow, MF_STRING | (disassembly  && disassembly->isVisible()  ? MFS_CHECKED : 0), MENU_WINDOW_DISASSEMBLY, buf);
    sprintf(buf, "%s", Language::windowCpuRegisters);
    AppendMenuU(hMenuWindow, MF_STRING | (cpuRegisters && cpuRegisters->isVisible() ? MFS_CHECKED : 0), MENU_WINDOW_CPUREGISTERS, buf);
    sprintf(buf, "%s", Language::windowStack);
    AppendMenuU(hMenuWindow, MF_STRING | (stack        && stack->isVisible()        ? MFS_CHECKED : 0), MENU_WINDOW_STACK, buf);
    sprintf(buf, "%s", Language::windowCallstack);
    AppendMenuU(hMenuWindow, MF_STRING | (callstack    && callstack->isVisible()    ? MFS_CHECKED : 0), MENU_WINDOW_CALLSTACK, buf);
    sprintf(buf, "%s", Language::windowBreakpoints);
    AppendMenuU(hMenuWindow, MF_STRING | (breakpoints  && breakpoints->isVisible()  ? MFS_CHECKED : 0), MENU_WINDOW_BREAKPOINTS, buf);
    sprintf(buf, "%s", Language::windowMemory);
    AppendMenuU(hMenuWindow, MF_STRING | (memory       && memory->isVisible()       ? MFS_CHECKED : 0), MENU_WINDOW_MEMORY, buf);
    sprintf(buf, "%s", Language::windowPeripheralRegisters);
    AppendMenuU(hMenuWindow, MF_STRING | (periRegisters&& periRegisters->isVisible()       ? MFS_CHECKED : 0), MENU_WINDOW_PERIREGISTERS, buf);
    sprintf(buf, "%s", Language::windowIoPorts);
    AppendMenuU(hMenuWindow, MF_STRING | (ioPorts      && ioPorts->isVisible()       ? MFS_CHECKED : 0), MENU_WINDOW_IOPORTS, buf);
    
    HMENU hMenuHelp = CreatePopupMenu();

    sprintf(buf, "%s", Language::menuHelpAbout);
    AppendMenuU(hMenuHelp, MF_STRING, MENU_HELP_ABOUT, buf);

    static HMENU hMenu = NULL;
    if (hMenu != NULL) {
        DestroyMenu(hMenu);
    }

    hMenu = CreateMenu();
    AppendMenuU(hMenu, MF_POPUP, (UINT_PTR)hMenuFile, Language::menuFile);
    AppendMenuU(hMenu, MF_POPUP, (UINT_PTR)hMenuDebug, Language::menuDebug);
    AppendMenuU(hMenu, MF_POPUP, (UINT_PTR)hMenuWindow, Language::menuWindow);
    AppendMenuU(hMenu, MF_POPUP, (UINT_PTR)hMenuHelp, Language::menuHelp);
    
    SetMenu(dbgHwnd, hMenu);
}


void loadSymbolFile(HWND hwndOwner)
{
    static char pFileName[MAX_PATH];
    static char buffer[0x20000];

    pFileName[0] = 0;

    if (!ShellOpenFileDialog(hwndOwner, Language::symbolWindowCaption,
                             "Symbol Files   (*.SYM)\0*.SYM\0All Files   (*.*)\0*.*\0\0",
                             NULL, "sym", NULL, pFileName, sizeof(pFileName)))
    {
        return;
    }

    FILE* file = fopenU(pFileName, "r");
    if (file == NULL) {
        return;
    }

    /* The buffer is static, so terminate at the byte count actually read. */
    size_t symbolLen = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[symbolLen] = 0;
    if (replaceSymbols) {
        symbolInfo->clear();
    }
    
    std::string strBuffer(buffer);
    symbolInfo->append(strBuffer);
    symbolInfo->show();
    disassembly->refresh(true);
    callstack->refresh();
    stack->refresh();
    fclose(file);
}

void saveDisassembly(HWND hwndOwner)
{
    static char pFileName[MAX_PATH];
    static char buffer[0x20000];

    pFileName[0] = 0;

    if (!ShellSaveFileDialog(hwndOwner, Language::menuFileSaveDisassembly,
                             "*.ASM\0*.*\0\0", NULL, "asm", NULL,
                             pFileName, sizeof(pFileName)))
    {
        return;
    }

    int len = (int)strlen(pFileName);
    if (len < 4 || (pFileName[len - 2] != '.' && pFileName[len - 3] != '.' && pFileName[len - 4] != '.')) {
        strcat(pFileName, ".asm");
    }

    FILE* f = fopenU(pFileName, "r");
    if (f != NULL) {
        fclose(f);
        int rv = ShowMessageBox(hwndOwner, Language::popupOverwrite, Language::windowDebugger, MB_YESNO | MB_ICONWARNING);
        if (rv != IDYES) {
            return;
        }
    }

    disassembly->writeToFile(pFileName);
}

void saveMemory(HWND hwndOwner)
{
    static char pFileName[MAX_PATH];
    static char buffer[0x20000];

    pFileName[0] = 0;

    if (!ShellSaveFileDialog(hwndOwner, Language::menuFileSaveMemory,
                             "*.BIN\0*.*\0\0", NULL, "bin", NULL,
                             pFileName, sizeof(pFileName)))
    {
        return;
    }

    int len = (int)strlen(pFileName);
    if (len < 4 || (pFileName[len - 2] != '.' && pFileName[len - 3] != '.' && pFileName[len - 4] != '.')) {
        strcat(pFileName, ".bin");
    }

    FILE* f = fopenU(pFileName, "r");
    if (f != NULL) {
        fclose(f);
        int rv = ShowMessageBox(hwndOwner, Language::popupOverwrite, Language::windowDebugger, MB_YESNO | MB_ICONWARNING);
        if (rv != IDYES) {
            return;
        }
    }
    
    memory->writeToFile(pFileName);
}

static bool inUpdate = false;
void DebuggerUpdate()
{
    if (inUpdate) return;
    inUpdate = true;
    updateToolBar();
    updateWindowMenu();
    if (disassembly != NULL) {
        disassembly->refresh();
    }
    if (breakpoints != NULL) {
        breakpoints->updateContent();
    }
    inUpdate = false;
}


static Snapshot* currentSnapshot = NULL;

void updateDeviceState()
{
    bool disassemblyUpdated = false;

    if (GetEmulatorState() == EMULATOR_RUNNING) {
        return;
    }

    Snapshot* snapshot = SnapshotCreate();
    if (snapshot != NULL) {
        int deviceCount = SnapshotGetDeviceCount(snapshot);

        for (int i = 0; i < deviceCount; i++) {
            Device* device = SnapshotGetDevice(snapshot, i);
            int j;

            int memCount = DeviceGetMemoryBlockCount(device);

            for (j = 0; j < memCount; j++) {
                MemoryBlock* mem = DeviceGetMemoryBlock(device, j);
            }

            int regBankCount = DeviceGetRegisterBankCount(device);
            for (j = 0; j < regBankCount; j++) {
                RegisterBank* regBank = DeviceGetRegisterBank(device, j);
            }

            int ioPortsCount = DeviceGetIoPortsCount(device);
            for (j = 0; j < ioPortsCount; j++) {
                IoPorts* ioPorts = DeviceGetIoPorts(device, j);
            }

            if (device->type == DEVTYPE_CPU && memCount > 0) {
                UInt16 pc = 0;
                UInt16 sp = 0;

                RegisterBank* regBank = DeviceGetRegisterBank(device, 0);
                for (UInt32 k = 0; k < regBank->count; k++) {
                    if (0 == strcmp("PC", regBank->reg[k].name)) {
                        pc = (UInt16)regBank->reg[k].value;
                    }
                    if (0 == strcmp("SP", regBank->reg[k].name)) {
                        sp = (UInt16)regBank->reg[k].value;
                    }
                }

                cpuRegisters->updateContent(regBank);

                MemoryBlock* mem = DeviceGetMemoryBlock(device, 0);
                if (mem->size == 0x10000) {
                    disassembly->updateContent(mem->memory, pc);
                    stack->updateContent(mem->memory, sp);
                    disassemblyUpdated = true;
                }

                Callstack* stack = DeviceGetCallstack(device, 0);
                if (stack != NULL) {
                    callstack->updateContent(stack->callstack, stack->size);
                }
            }
        }

        memory->updateContent(snapshot);
        periRegisters->updateContent(snapshot);
        ioPorts->updateContent(snapshot);
    }

    if (currentSnapshot != NULL) {
        SnapshotDestroy(currentSnapshot);
    }

    currentSnapshot = snapshot;

    if (!disassemblyUpdated) {
        disassembly->invalidateContent();
        cpuRegisters->invalidateContent();
        callstack->invalidateContent();
        stack->invalidateContent();
    }
    DebuggerUpdate();
}


static LRESULT CALLBACK wndProcView(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    if (dbgViewMessage(hwnd, iMsg, wParam)) {
        return 0;
    }

    switch (iMsg) {
    case WM_CREATE:
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT r;
            GetClientRect(hwnd, &r);
            HBRUSH hBrush = (HBRUSH)SelectObject(hdc, GetStockObject(GRAY_BRUSH)); 
            PatBlt(hdc, 0, 0, r.right, r.bottom, PATCOPY);
            SelectObject(hdc, hBrush);
            EndPaint(hwnd, &ps);
        }
        return 0;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

static void updateWindowPositions()
{    
    if (statusBar != NULL) {
        statusBar->updatePosition();
    }
    if (toolBar != NULL) {
        toolBar->updatePosition();
    }
    if (viewHwnd != NULL) {
        RECT r;
        GetClientRect(dbgHwnd, &r);

        if (statusBar != NULL) {
            r.bottom -= statusBar->getHeight();
        }
        if (toolBar != NULL) {
            r.top    += toolBar->getHeight();
            r.bottom -= toolBar->getHeight();
        }

        SetWindowPos(viewHwnd, NULL, r.left, r.top, r.right, r.bottom, SWP_NOZORDER);
    }
}

/* RegisterHotKey grabs the key desktop wide, so these may only be held while
** this window really is the foreground one. GetForegroundWindow lags the
** activation messages, so DBG_HOTKEY_TIMER stays on as a backstop. */
#define DBG_HOTKEY_COUNT 21
#define DBG_HOTKEY_TIMER 1
#define DBG_HOTKEY_POLL  250

/* Ctrl+V reaches the in-place richedit from the handler rather than by being
** released here, so that the box that has focus is read at the keystroke. */
#define DBG_HOTKEY_PASTE 19

static void setDebuggerHotkeys(HWND hwnd, BOOL hold)
{
    static BOOL held = FALSE;
    int i;

    if (hold == held) {
        return;
    }
    held = hold;

    if (!hold) {
        for (i = 1; i <= DBG_HOTKEY_COUNT; i++) {
            UnregisterHotKey(hwnd, i);
        }
        return;
    }

    RegisterHotKey(hwnd, 1,  0, VK_F5);
    RegisterHotKey(hwnd, 2,  MOD_CONTROL | MOD_ALT, VK_CANCEL);
    RegisterHotKey(hwnd, 3,  MOD_SHIFT, VK_F5);
    RegisterHotKey(hwnd, 4,  MOD_CONTROL | MOD_SHIFT, VK_F5);
    RegisterHotKey(hwnd, 5,  0, VK_F11);
    RegisterHotKey(hwnd, 6,  0, VK_F10);
    RegisterHotKey(hwnd, 7,  MOD_SHIFT, VK_F11);
    RegisterHotKey(hwnd, 8,  MOD_SHIFT, VK_F10);
    RegisterHotKey(hwnd, 9,  0, VK_F9);
    RegisterHotKey(hwnd, 10, MOD_SHIFT, VK_F9);
    RegisterHotKey(hwnd, 11, MOD_CONTROL | MOD_SHIFT, VK_F9);
    RegisterHotKey(hwnd, 12, 0, VK_F8);
    RegisterHotKey(hwnd, 13, MOD_CONTROL, 'G');
    RegisterHotKey(hwnd, 14, MOD_CONTROL, 'M');
    RegisterHotKey(hwnd, 15, MOD_CONTROL, 'B');
    RegisterHotKey(hwnd, 16, MOD_CONTROL, 'F');
    RegisterHotKey(hwnd, 17, 0, VK_F3);
    RegisterHotKey(hwnd, 18, 0, VK_HOME);
    RegisterHotKey(hwnd, DBG_HOTKEY_PASTE, MOD_CONTROL, 'V');
    RegisterHotKey(hwnd, 20, MOD_CONTROL, VK_F11);
    RegisterHotKey(hwnd, 21, MOD_CONTROL, 'W');
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static BOOL isActive = FALSE;
    static BOOL appActive = FALSE;

    if (dbgViewMessage(hwnd, iMsg, wParam)) {
        return 0;
    }

    switch (iMsg) {
    case WM_CREATE:
        {
            /* No activation message has arrived yet, so ask once whether this
            ** process holds the foreground. Every later change is a message. */
            DWORD fgPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
            appActive = fgPid == GetCurrentProcessId();
            /* Shared with the window that stood here before, and a window is
            ** not active until its own WM_ACTIVATE says so. */
            isActive = FALSE;
        }
        SetTimer(hwnd, DBG_HOTKEY_TIMER, DBG_HOTKEY_POLL, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == DBG_HOTKEY_TIMER) {
            /* By now the query has caught up with the messages, so this is the
            ** backstop for a transition they did not describe between them. */
            setDebuggerHotkeys(hwnd, GetForegroundWindow() == hwnd);
            return 0;
        }
        break;

    case WM_ACTIVATEAPP:
        appActive = wParam != 0;
        setDebuggerHotkeys(hwnd, appActive && isActive);
        break;

    case WM_ACTIVATE:
        /* Minimized counts as inactive: activation can land on a window the user
        ** cannot see, and the keys this gates are taken desktop wide. */
        isActive = LOWORD(wParam) != WA_INACTIVE && HIWORD(wParam) == 0;

        if (toolBar != NULL) {
            static int minimizedState = 0;
            if (minimizedState && HIWORD(wParam) == 0) {
                delete toolBar;
                toolBar = initializeToolbar(hwnd);
                toolBar->show();
                /* addButton makes every button enabled, so a rebuilt toolbar
                ** offers commands the emulator state forbids until the next
                ** state change repaints it. The creation path pairs the two. */
                updateToolBar();
            }
            minimizedState = HIWORD(wParam);
        }

        setDebuggerHotkeys(hwnd, appActive && isActive);
        break;

    case WM_HOTKEY:
        if (isActive) {
            switch (wParam) {
            case 1:
                SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_CONTINUE, 0);
                break;
            case 2:
                if (GetEmulatorState() == EMULATOR_RUNNING)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_BREAKALL, 0);
                break;
            case 3:
                if (GetEmulatorState() != EMULATOR_STOPPED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_STOP, 0);
                break;
            case 4:
                if (GetEmulatorState() == EMULATOR_PAUSED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_RESTART, 0);
                break;
            case 5:
                if (GetEmulatorState() == EMULATOR_PAUSED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_STEP, 0);
                break;
            case 6:
                if (GetEmulatorState() == EMULATOR_PAUSED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_STEP_OVER, 0);
                break;
            case 7:
                if (GetEmulatorState() == EMULATOR_PAUSED && callstack->getReturnAddress() >= 0)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_STEP_OUT, 0);
                break;
            case 8:
                if (GetEmulatorState() == EMULATOR_PAUSED && disassembly->isCursorPresent())
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_RUNTO, 0);
                break;
            case 9:
                if (GetEmulatorState() != EMULATOR_STOPPED && disassembly->isCursorPresent())
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_BPTOGGLE, 0);
                break;
            case 10:
                if (GetEmulatorState() != EMULATOR_STOPPED && disassembly->isBpOnCcursor())
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_BPENABLE, 0);
                break;
            case 11:
                if (breakpoints->getEnabledBpCount() || breakpoints->getDisabledBpCount())
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_BPREMOVEALL, 0);
                break;
            case 12:
                SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_SHOWSYMBOLS, 0);
                break;
            case 13:
                if (GetEmulatorState() != EMULATOR_STOPPED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_GOTO, 0);
                break;
            case 14:
                SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_FLAGMODE, 0);
                break;
            case 15:
                if (GetEmulatorState() != EMULATOR_STOPPED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_SETBP, 0);
                break;
            case 16:
                if (GetEmulatorState() != EMULATOR_STOPPED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_FIND, 0);
                break;
            case 17:
                if (GetEmulatorState() != EMULATOR_STOPPED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_FINDNEXT, 0);
                break;
            case 18:
                disassembly->updateScroll();
                break;
            case DBG_HOTKEY_PASTE:
                /* Asked here, when the key actually arrives, so the answer
                ** cannot be stale by the time it is used. */
                if (!InputDialog::pasteToFocused()) {
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_CHECK_VRAM, 0);
                }
                break;
            case 20:
                if (GetEmulatorState() == EMULATOR_PAUSED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_STEP_BACK, 0);
                break;
            case 21:
                if (GetEmulatorState() != EMULATOR_STOPPED)
                    SendMessage(hwnd, WM_COMMAND, MENU_DEBUG_SETWP, 0);
                break;
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case MENU_FILE_EXIT:
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;

        case MENU_FILE_LOADSYM:
            loadSymbolFile(hwnd);
            return 0;

        case MENU_FILE_REPLACESYM:
            replaceSymbols = !replaceSymbols;
            updateWindowMenu();
            return 0;

        case MENU_FILE_SAVEDASM:
            saveDisassembly(hwnd);
            return 0;

        case MENU_FILE_SAVEMEM:
            saveMemory(hwnd);
            return 0;

        case MENU_HELP_ABOUT:
            {
                char text[256];
                sprintf(text, "%s\r\n\r\n%s: " __DATE__ "\r\n\r\n%s    \r\n\r\n\r\n",
                    Language::windowDebugger, Language::aboutBuilt, Language::aboutVisit);
                ShowMessageBox(hwnd, text, Language::windowDebugger, MB_ICONINFORMATION | MB_OK);
            }
            return 0;

        case MENU_WINDOW_DISASSEMBLY:
            if (disassembly->isVisible()) {
                disassembly->hide(); 
            }
            else {
                disassembly->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_WINDOW_CPUREGISTERS:
            if (cpuRegisters->isVisible()) {
                cpuRegisters->hide(); 
            }
            else {
                cpuRegisters->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_WINDOW_PERIREGISTERS:
            if (periRegisters->isVisible()) {
                periRegisters->hide(); 
            }
            else {
                periRegisters->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_WINDOW_IOPORTS:
            if (ioPorts->isVisible()) {
                ioPorts->hide(); 
            }
            else {
                ioPorts->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_WINDOW_STACK:
            if (stack->isVisible()) {
                stack->hide(); 
            }
            else {
                stack->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_WINDOW_CALLSTACK:
            if (callstack->isVisible()) {
                callstack->hide(); 
            }
            else {
                callstack->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_WINDOW_BREAKPOINTS:
            if (breakpoints->isVisible()) {
                breakpoints->hide(); 
            }
            else {
                breakpoints->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_WINDOW_MEMORY:
            if (memory->isVisible()) {
                memory->hide(); 
            }
            else {
                memory->show();
            }
            updateWindowMenu();
            return 0;

        case MENU_DEBUG_CONTINUE:
        case TB_RESUME:
            EmulatorRun();
            return 0;
            
        case MENU_DEBUG_BREAKALL:
        case TB_PAUSE:
            EmulatorPause();
            return 0;
            
        case MENU_DEBUG_STOP:
        case TB_STOP:
            EmulatorStop();
            return 0;
            
        case MENU_DEBUG_RESTART:
        case TB_RUN:
            EmulatorStop();
            EmulatorRun();
            return 0;
            
        case TB_SHOWNEXT:
            disassembly->updateScroll();
            return 0;
            
        case MENU_DEBUG_STEP:
        case TB_STEPIN:
            EmulatorStep();
            return 0;
            
        case MENU_DEBUG_STEP_BACK:
        case TB_STEPBACK:
            EmulatorStepBack();
            return 0;

        case MENU_DEBUG_STEP_OVER:
        case TB_STEPOVER:
            {
                bool step = breakpoints->setStepOverBreakpoint(disassembly->getMemory(), disassembly->getPc(),
                                                              cpuRegisters->interruptsEnabled());
                if (step) {
                    EmulatorStep();
                }
                else {
                    EmulatorRun();
                }
            }
            return 0;

        case MENU_DEBUG_STEP_OUT:
        case TB_STEPOUT:
            if (breakpoints->setRuntoBreakpoint(callstack->getReturnAddress())) {
                EmulatorRun();
            }
            return 0;

        case MENU_DEBUG_RUNTO:
        case TB_RUNTO:
            if (breakpoints->setRuntoBreakpoint(disassembly->getCurrentAddress())) {
                EmulatorRun();
            }
            return 0;

        case MENU_DEBUG_SETBP:
            InputDialogs::NewBreakpoint();
            return 0;

        case MENU_DEBUG_SETWP:
            InputDialogs::NewWatchpoint();
            return 0;

        case MENU_DEBUG_GOTO:
            InputDialogs::GotoAddress();
            return 0;

        case MENU_DEBUG_CHECK_VRAM:
            vramCheckAccess ^= 1;
            EnableVramAccessCheck(vramCheckAccess);
            updateWindowMenu();
            return 0;

        case MENU_DEBUG_FIND:
            InputDialogs::FindText();
            return 0;

        case MENU_DEBUG_FINDNEXT:
            InputDialogs::FindNextText();
            return 0;

        case MENU_DEBUG_SHOWSYMBOLS:
            if (symbolInfo->getShowStatus()) {
                symbolInfo->hide();
            }
            else {
                symbolInfo->show();
            }
            disassembly->refresh(true);
            callstack->refresh();
            stack->refresh();
            updateWindowMenu();
            return 0;
            
        case MENU_DEBUG_BPTOGGLE:
        case TB_BPTOGGLE:
            if (Breakpoints::IsBreakpointSet(disassembly->getCurrentAddress())) {
                Breakpoints::ClearBreakpoint(disassembly->getCurrentAddress());
            }
            else {
                Breakpoints::SetBreakpoint(disassembly->getCurrentAddress());
            }
            return 0;
            
        case MENU_DEBUG_BPENABLE:
        case TB_BPENABLE:
            Breakpoints::ToggleBreakpointEnable(disassembly->getCurrentAddress());
            return 0;
            
        case MENU_DEBUG_BPREMOVEALL:
        case TB_BPREMALL:
            breakpoints->clearAllBreakpoints();
            return 0;

        case MENU_DEBUG_BPENABLEALL:
        case TB_BPENALL:
            breakpoints->enableAllBreakpoints();
            return 0;

        case MENU_DEBUG_BPDISABLEALL:
        case TB_BPDISALL:
            breakpoints->disableAllBreakpoints();
            return 0;

        case MENU_DEBUG_FLAGMODE:
            if (cpuRegisters->getFlagMode() == CpuRegisters::FM_ASM) {
                cpuRegisters->setFlagMode(CpuRegisters::FM_CPU);
            }
            else {
                cpuRegisters->setFlagMode(CpuRegisters::FM_ASM);
            }
            updateWindowMenu();
            return 0;
        }
        break;

    case WM_STATUS:
        if (statusBar != NULL) {
            updateStatusBar();
        }
        if (toolBar != NULL) {
            updateToolBar();
        }
        updateWindowMenu();
        updateDeviceState();
        return 0;

    case WM_SIZE:
        updateWindowPositions();
        break;
        
    case WM_NOTIFY:
        switch(((LPNMHDR)lParam)->code){
        case NM_CUSTOMDRAW:
            if (toolBar != NULL) {
                toolBar->onWmNotify(lParam);
            }
            return 0;

        case TTN_GETDISPINFOW: {
            /* Toolbar tooltip configured for Unicode (TTM_SETUNICODEFORMAT) so
            ** the UTF-8 Language strings render without CP932 mojibake. */
            LPNMTTDISPINFOW lpttt = (LPNMTTDISPINFOW)lParam;
            static wchar_t wbuf[256];
            char buf[256] = {0};
            updateTooltip((int)lpttt->hdr.idFrom, buf);
            MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, (int)_countof(wbuf));
            lpttt->hinst    = NULL;
            lpttt->lpszText = wbuf;
            break;
        }

        case TTN_GETDISPINFOA:
            /* Some controls still send the ANSI variant; fall through with
            ** a CP932 round-trip so we never put UTF-8 bytes into szText. */
            { 
                LPNMTTDISPINFOA lpttt = (LPNMTTDISPINFOA)lParam;
                char buf[256] = {0};
                wchar_t wbuf[256];
                updateTooltip((int)lpttt->hdr.idFrom, buf);
                if (MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, (int)_countof(wbuf)) > 0) {
                    WideCharToMultiByte(CP_ACP, 0, wbuf, -1, lpttt->szText,
                                        (int)_countof(lpttt->szText), NULL, NULL);
                }
                lpttt->hinst = NULL;
            }
            break;
        }
        break;

    case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS* windowPos = (WINDOWPOS*)lParam;
            x       = windowPos->x;
            y       = windowPos->y;
            width   = windowPos->cx;
            height  = windowPos->cy;
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
        if (vramCheckAccess) {
            EnableVramAccessCheck(0);
        }
        /* Closing the window does not disarm anything, so the emulator would
        ** keep stopping at addresses the user can no longer see. Ask -- but
        ** only about the armed ones, since disabled rows stop nothing. */
        if (breakpoints != NULL && breakpoints->getEnabledBpCount() > 0) {
            int rv = ShowMessageBox(hwnd, Language::popupRemoveBreakpoints,
                                    Language::windowDebugger, MB_YESNO | MB_ICONQUESTION);
            /* The prompt runs a message loop of its own, so the tool can have
            ** been torn down and `breakpoints` deleted while it was up. */
            if (rv == IDYES && breakpoints != NULL) {
                breakpoints->clearAllBreakpoints();
            }
        }
        if (dbgHwnd != NULL) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, DBG_HOTKEY_TIMER);
        setDebuggerHotkeys(hwnd, FALSE);
        iniFileWriteInt( "Main Window", "x",       x);
        iniFileWriteInt( "Main Window", "y",       y);
        iniFileWriteInt( "Main Window", "width",   width);
        iniFileWriteInt( "Main Window", "height",  height);
        iniFileWriteInt( "Main Window", "check vram access",  vramCheckAccess);
        iniFileWriteInt( "Main Window", "font size",  dbgFontPoints());

        breakpoints->discardRuntoBreakpoint();
        breakpoints->keepBreakpoints();
        dbgHwnd = NULL;
        delete statusBar;
        statusBar = NULL;
        delete toolBar;
        toolBar = NULL;
        viewHwnd = NULL;
        
        if (disassembly) { DestroyWindow(disassembly->hwnd); delete disassembly; disassembly = NULL; }
        if (cpuRegisters) {DestroyWindow(cpuRegisters->hwnd); delete cpuRegisters; cpuRegisters = NULL; }
        if (inputDialogs) {delete inputDialogs; inputDialogs = NULL; }
        if (periRegisters) {DestroyWindow(periRegisters->hwnd); delete periRegisters; periRegisters = NULL; }
        if (ioPorts) {DestroyWindow(ioPorts->hwnd); delete ioPorts; ioPorts = NULL; }
        if (callstack) {DestroyWindow(callstack->hwnd); delete callstack; callstack = NULL; }
        if (breakpoints) {DestroyWindow(breakpoints->hwnd); delete breakpoints; breakpoints = NULL; }
        if (stack) {DestroyWindow(stack->hwnd); delete stack; stack = NULL; }
        if (memory) {DestroyWindow(memory->hwnd); delete memory; memory = NULL; }
        
        delete symbolInfo;
        symbolInfo = NULL;
        
        iniFileClose();

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
    wndClass.lpszClassName  = "msxdebugger";

    RegisterClassEx(&wndClass);
    
    wndClass.lpfnWndProc    = wndProcView;
    wndClass.lpszClassName  = "msxdebuggerview";
    wndClass.style          = 0;
    wndClass.lpszMenuName   = NULL;

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

    iniFileOpen( "debugger.ini" );

    Language::SetLanguage(langId);

    x       = iniFileGetInt( "Main Window", "x", CW_USEDEFAULT );
    y       = iniFileGetInt( "Main Window", "y", CW_USEDEFAULT );
    /* Default size: 800x740 logical at 96 DPI -- DPI-scale at runtime so
    ** initial window is comfortable on 150% / 200% monitors. Saved INI
    ** values are stored in actual pixels and used as-is. */
    width   = iniFileGetInt( "Main Window", "width",  dpiScale(NULL, 800) );
    height  = iniFileGetInt( "Main Window", "height", dpiScale(NULL, 740) );
    vramCheckAccess = iniFileGetInt( "Main Window", "check vram access", 0 );
    dbgSetFontPoints( iniFileGetInt( "Main Window", "font size", dbgFontPointsDefault() ) );
    
    if (vramCheckAccess) {
        EnableVramAccessCheck(1);
    }

    /* Clamped to the monitor the saved rectangle lives on. The primary metrics
    ** would drag a window parked on a secondary monitor back every launch, and
    ** the virtual screen is a bounding box with coordinates on no monitor. */
    if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) {
        RECT saved;
        MONITORINFO mi;
        HMONITOR hMon;
        SetRect(&saved, x, y, x + width, y + height);
        hMon = MonitorFromRect(&saved, MONITOR_DEFAULTTONEAREST);
        mi.cbSize = sizeof(mi);
        if (hMon != NULL && GetMonitorInfo(hMon, &mi)) {
            if (x + 200 > mi.rcWork.right)  { x = mi.rcWork.right - 200; }
            if (y + 200 > mi.rcWork.bottom) { y = mi.rcWork.bottom - 200; }
            if (x < mi.rcWork.left) { x = mi.rcWork.left; }
            if (y < mi.rcWork.top)  { y = mi.rcWork.top; }
        }
    }

    dbgHwnd = CreateWindow("msxdebugger", NULL,
                           WS_OVERLAPPEDWINDOW, 
                           x, y, width, height, NULL, NULL, GetDllHinstance(), NULL);
    SetWindowTextU(dbgHwnd, Language::windowDebugger);

    viewHwnd = CreateWindow("msxdebuggerview", "", 
                            WS_OVERLAPPED | WS_CHILD | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            dpiScale(dbgHwnd, 600), dpiScale(dbgHwnd, 500),
                            dbgHwnd, NULL, GetDllHinstance(), NULL);

    ShowWindow(dbgHwnd, TRUE);
    ShowWindow(viewHwnd, TRUE);

    /* Dark titlebar / frame / scrollbars on the debugger main window. */
    ApplyDarkMode(dbgHwnd);

    symbolInfo = new SymbolInfo;

    std::vector<int> fieldVector;
    fieldVector.push_back(0);
    /* 110px wide center field fits the JP "PAUSED" 4-char label plus
    ** padding; DPI-scale so it still fits at 200% monitors. */
    fieldVector.push_back(dpiScale(dbgHwnd, 110));
    fieldVector.push_back(dpiScale(dbgHwnd,  30));
    statusBar = new StatusBar(GetDllHinstance(), dbgHwnd, fieldVector);
    updateStatusBar();
    statusBar->show();
    toolBar = initializeToolbar(dbgHwnd);
    toolBar->show();
    
    breakpoints   = new Breakpoints(GetDllHinstance(), viewHwnd, symbolInfo);
    disassembly   = new Disassembly(GetDllHinstance(), viewHwnd, symbolInfo, breakpoints);
    cpuRegisters  = new CpuRegisters(GetDllHinstance(), viewHwnd);
    callstack     = new CallstackWindow(GetDllHinstance(), viewHwnd, disassembly);
    stack         = new StackWindow(GetDllHinstance(), viewHwnd);
    memory        = new Memory(GetDllHinstance(), viewHwnd, symbolInfo, cpuRegisters);
    inputDialogs  = new InputDialogs(GetDllHinstance(), viewHwnd, disassembly, symbolInfo, cpuRegisters, memory, breakpoints);
    periRegisters = new PeripheralRegs(GetDllHinstance(), viewHwnd);
    ioPorts       = new IoPortWindow(GetDllHinstance(), viewHwnd);

    /* Last: this re-arms the kept breakpoints, and a hit calls back into every
    ** view, so they all have to exist first. */
    breakpoints->restoreBreakpoints();

    updateWindowPositions();
    
    if (GetEmulatorState() == EMULATOR_PAUSED) {
        OnEmulatorPause();
    }

    updateToolBar();
    updateWindowMenu();
}

void OnEmulatorStart() {
    if (dbgHwnd != NULL) {
        disassembly->invalidateContent();
        cpuRegisters->invalidateContent();
        callstack->invalidateContent();
        stack->invalidateContent();
        periRegisters->invalidateContent();
        memory->invalidateContent();
        ioPorts->invalidateContent();

        breakpoints->updateBreakpoints();
        disassembly->disableEdit();
        cpuRegisters->disableEdit();
        periRegisters->disableEdit();
        ioPorts->disableEdit();
        callstack->disableEdit();
        breakpoints->disableEdit();
        stack->disableEdit();
        memory->disableEdit();
        SendMessage(dbgHwnd, WM_STATUS, 0, 0);
    }
}

void OnEmulatorStop() {
    if (dbgHwnd != NULL) {
        disassembly->invalidateContent();
        cpuRegisters->invalidateContent();
        callstack->invalidateContent();
        stack->invalidateContent();
        periRegisters->invalidateContent();
        memory->invalidateContent();
        ioPorts->invalidateContent();

        disassembly->disableEdit();
        cpuRegisters->disableEdit();
        periRegisters->disableEdit();
        ioPorts->disableEdit();
        callstack->disableEdit();
        breakpoints->disableEdit();
        stack->disableEdit();
        memory->disableEdit();
        SendMessage(dbgHwnd, WM_STATUS, 0, 0);
    }
}

void OnEmulatorPause() {
    if (dbgHwnd != NULL) {
        disassembly->enableEdit();
        cpuRegisters->enableEdit();
        periRegisters->enableEdit();
        ioPorts->enableEdit();
        callstack->enableEdit();
        breakpoints->enableEdit();
        stack->enableEdit();
        memory->enableEdit();
        SendMessage(dbgHwnd, WM_STATUS, 0, 0);
    }
}

void OnEmulatorResume() {
    if (dbgHwnd != NULL) {
        /* What the run leaves behind, so drop it. The device views keep theirs:
        ** they carry the reference copy the next stop colours its changes
        ** against, which is worth more than dropping it. */
        disassembly->invalidateContent();
        callstack->invalidateContent();
        stack->invalidateContent();

        disassembly->disableEdit();
        cpuRegisters->disableEdit();
        periRegisters->disableEdit();
        ioPorts->disableEdit();
        callstack->disableEdit();
        breakpoints->disableEdit();
        stack->disableEdit();
        memory->disableEdit();
        SendMessage(dbgHwnd, WM_STATUS, 0, 0);
    }
}

void OnEmulatorReset() {
}

void OnEmulatorTrace(const char* message)
{
}

void OnEmulatorSetBreakpoint(UInt16 address)
{
    Breakpoints::SetBreakpoint(address);
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 address) {
    OnEmulatorSetBreakpoint(address);
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address)
{
    OnEmulatorSetBreakpoint(address);
}

void OnSetLanguage(LanguageId languageId)
{
    langId = languageId;
    Language::SetLanguage(langId);
}

const char* OnGetName() {
    return Language::debuggerName;
}
