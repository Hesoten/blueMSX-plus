#include <windows.h>
#include "ToolInterface.h"
#include "Resource.h"
#include "resrc1.h"
#include "Language.h"
#include "Win32TextUtf8.h"
#include <string>
#include <commctrl.h>
#include <list>
#include <sstream>
#include <iomanip>

/* Paint the listview's empty client area dark on WM_ERASEBKGND --
** LVM_SETBKCOLOR only colors rows, leaving COLOR_WINDOW white gaps. */
#define TRAINER_LV_DARK_SUBCLASS_ID 0xC4EAEC

static LRESULT CALLBACK trainerDarkListViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                UINT_PTR id, DWORD_PTR data)
{
    (void)data;
    if (msg == WM_ERASEBKGND && IsDarkMode()) {
        HDC hdc = (HDC)wp;
        RECT rc;
        HBRUSH br = GetDarkBgBrush();
        GetClientRect(hwnd, &rc);
        if (br) FillRect(hdc, &rc, br);
        return 1;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, trainerDarkListViewProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void darkenListView(HWND hLV)
{
    if (!hLV) return;
    if (IsDarkMode()) {
        ListView_SetBkColor    (hLV, GetDarkBg());
        ListView_SetTextBkColor(hLV, GetDarkBg());
        ListView_SetTextColor  (hLV, GetDarkFg());
        SetWindowSubclass(hLV, trainerDarkListViewProc, TRAINER_LV_DARK_SUBCLASS_ID, 0);
        InvalidateRect(hLV, NULL, TRUE);
    }
}

/* MS UI Gothic for file-path edits: renders 0x5C as the yen sign on
** Japanese Windows (matches File Explorer).  Sized ~20% larger to match
** Segoe UI's visual Latin height. */
static HFONT s_pathFont = NULL;
static HFONT getPathFont(HWND hRef)
{
    if (s_pathFont) return s_pathFont;
    HFONT hRefFont = (HFONT)SendMessageW(hRef, WM_GETFONT, 0, 0);
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    if (hRefFont) GetObjectW(hRefFont, sizeof(lf), &lf);
    lf.lfHeight = (LONG)(lf.lfHeight * 12 / 10);
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"MS UI Gothic");
    s_pathFont = CreateFontIndirectW(&lf);
    return s_pathFont;
}

/* Vertically center the enlarged path-edit glyphs by shrinking the
** client rect via WM_NCCALCSIZE -- a taller-than-text box leaves a gap. */
#define TRAINER_PATH_EDIT_SUBCLASS_ID 0xC4EAED

static LRESULT CALLBACK trainerPathEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                             UINT_PTR id, DWORD_PTR data)
{
    (void)data;
    if (msg == WM_NCCALCSIZE) {
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        RECT* rc = wp ? &((NCCALCSIZE_PARAMS*)lp)->rgrc[0] : (RECT*)lp;
        HDC hdc = GetDC(hwnd);
        HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        HFONT old = font ? (HFONT)SelectObject(hdc, font) : NULL;
        TEXTMETRICW tm = {0};
        GetTextMetricsW(hdc, &tm);
        if (old) SelectObject(hdc, old);
        ReleaseDC(hwnd, hdc);
        int boxH = rc->bottom - rc->top;
        int textH = tm.tmHeight;
        int extra = boxH - textH;
        if (extra > 0) {
            rc->top    += extra / 2;
            rc->bottom -= extra - extra / 2;
        }
        return r;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, trainerPathEditProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

enum CompareType
{
    CMP_EQUAL,
    CMP_NOTEQUAL,
    CMP_LESSTHAN,
    CMP_LESSOREQUAL,
    CMP_GRATERTHAN,
    CMP_GREATEROREQUAL,
};

enum DisplayType
{
    DPY_DECIMAL,
    DPY_HEXADECIMAL,
};

enum DataSize
{
    DATASIZE_8BIT,
    DATASIZE_16BIT,
};

enum CompareValue
{
    CMP_OLDVALUE,
    CMP_CHANGE,
    CMP_SPECIFIC,
};


using namespace std;

static HWND dbgHwnd = NULL;
static HWND hDlgSearch = NULL;
static HWND hDlgCheats = NULL;
static LanguageId langId = LID_ENGLISH;

static HBRUSH hBrush = NULL;

static CompareType  compareType    = CMP_EQUAL;
static DisplayType  displayType    = DPY_DECIMAL;
static DataSize     dataSize       = DATASIZE_8BIT;
static CompareValue compareValue   = CMP_SPECIFIC;
static Int32        cmpChangeVal   = 0;
static Int32        cmpSpecificVal = 0;

static bool canSearch   = false;
static bool canUndo     = false;
static bool canAddCheat = false;
static bool hasSnapshot = false;
static bool canCheat    = false;
static bool canRemoveCheat = false;
static bool canRemoveAllCheat = false;

static Int32 DataMask[]  = { 0xff, 0xffff };
static char* DpyFormat[] = { "%d", "%X" }; 
static char* DpySizeFormat[2][2]  = { { "%d", "%.2X" }, { "%d", "%.4X" } };

#define CHEAT_TIMER_ID 29

struct CheatInfo {
    char description[128];
    UInt32      address;
    Int32       value;
    DataSize    dataSize;
    DisplayType displayType;
    bool        enabled;

    CheatInfo(const char* d, UInt32 a, Int32 v, DataSize ds, DisplayType dt, bool e) : address(a), value(v), dataSize(ds), displayType(dt), enabled(e) {
        strncpy(description, d, sizeof(description));
        description[sizeof(description) - 1] = 0;
    }
};

bool showCheatDialog(CheatInfo* ci);
void addCheat(CheatInfo* ci);
static void updateCheatList();

static UInt16 getData(DataSize dataSize, UInt8* mem, Int32 offset)
{
    if (dataSize == DATASIZE_16BIT) {
        return mem[offset] + 256 * mem[(offset + 1) & 0xffff];
    }
    return mem[offset];
}

static void setBtCheck(HWND hDlg, int id, bool check, bool enable = true) {
    HWND hwnd = GetDlgItem(hDlg, id);

    if (check) {
        SendMessage(hwnd, BM_SETCHECK, BST_CHECKED, 0);
    }
    else {
        SendMessage(hwnd, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    if (!enable) {
        SendMessage(hwnd, BM_SETCHECK, BST_INDETERMINATE, 0);
    }
}

static int getBtCheck(HWND hDlg, int id) {
    HWND hwnd = GetDlgItem(hDlg, id);

    return BST_CHECKED == SendMessage(hwnd, BM_GETCHECK, 0, 0) ? 1 : 0;
}


struct SnapshotData
{
    UInt8 dataNew[0x10000];
    UInt8 dataOld[0x10000];
    UInt8 mask[0x10000];
    UInt8 maskRef[0x10000];
};

SnapshotData snapshotData;



#if 0
#define IDEDITCTL           100

#define MENU_FILE_EXIT              37100
#define MENU_FILE_LOG               37101
#define MENU_EDIT_SELECTALL         37200
#define MENU_EDIT_COPY              37201
#define MENU_EDIT_CLEAR             37202
#define MENU_HELP_ABOUT             37400

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
#endif

static char* cheatFileDialog(HWND hwndOwner, char* defExt, bool openForSave)
{
    static bool firstOpen = true;
    static char fileName[MAX_PATH] = {0};

    char cheatPath[512];
    strcpy(cheatPath, GetToolPath());
    strcat(cheatPath, "\\Cheats");

    if (!openForSave) {
        fileName[0] = 0; 
    }

    /* IFileDialog (newer host): UTF-8 paths, dark-aware navigation pane. */
    const char* title  = openForSave ? Language::saveCheatCaption : Language::loadCheatCaption;
    const char* filter = "Cheat files (*.mcf)\0*.MCF\0All files\0*.*\0\0";
    const char* dir    = firstOpen ? cheatPath : NULL;
    bool rv;
    if (openForSave) {
        rv = ShellSaveFileDialog(hwndOwner, title, filter, dir, "mcf", NULL,
                                 fileName, sizeof(fileName));
    }
    else {
        rv = ShellOpenFileDialog(hwndOwner, title, filter, dir, "mcf", NULL,
                                 fileName, sizeof(fileName));
    }
    firstOpen = false;

    if (!rv) {
        return NULL; 
    }

    if (defExt) {
        if (strlen(fileName) <= strlen(defExt)) {
            strcat(fileName, defExt);
        }
        else {
            char* pos = fileName + strlen(fileName) - strlen(defExt);
            int  len  = (int)strlen(defExt);
            while (len--) {
                if (toupper(pos[len]) != toupper(defExt[len])) {
                    break;
                }
            }
            if (len >= 0) {
                strcat(fileName, defExt);
            }
        }
    }

    return fileName;
}

void updateButtons()
{
    EnableWindow(GetDlgItem(hDlgSearch, IDC_MEMLIST),  GetEmulatorState() == EMULATOR_PAUSED);
    EnableWindow(GetDlgItem(hDlgSearch, IDC_SNAPSHOT), GetEmulatorState() == EMULATOR_PAUSED);
    EnableWindow(GetDlgItem(hDlgSearch, IDC_SEARCH),   GetEmulatorState() == EMULATOR_PAUSED && hasSnapshot);
    EnableWindow(GetDlgItem(hDlgSearch, IDC_UNDO),     GetEmulatorState() == EMULATOR_PAUSED && canUndo);
    EnableWindow(GetDlgItem(hDlgSearch, IDC_ADDCHEAT), GetEmulatorState() == EMULATOR_PAUSED && canAddCheat);

    EnableWindow(GetDlgItem(hDlgCheats, IDC_REMOVE),    canRemoveCheat);
    EnableWindow(GetDlgItem(hDlgCheats, IDC_REMOVEALL), canRemoveAllCheat);
    EnableWindow(GetDlgItem(hDlgCheats, IDC_ENABLE),    canRemoveCheat);
    EnableWindow(GetDlgItem(hDlgCheats, IDC_SAVE),      canRemoveAllCheat);

    bool running = GetEmulatorState() == EMULATOR_RUNNING;
    SetWindowTextU(GetDlgItem(hDlgCheats, IDC_RUNSTOP), running ? Language::pause : Language::run);
    SetWindowTextU(GetDlgItem(hDlgSearch, IDC_RUNSTOP), running ? Language::pause : Language::run);

}

static Snapshot* currentSnapshot = NULL;
MemoryBlock* memoryBlock = NULL;

void updateSnapshot()
{
    memoryBlock = NULL;

    bool isRunning = GetEmulatorState() == EMULATOR_RUNNING;

    if (isRunning) {
        EmulatorPause();
    }

    Snapshot* snapshot = SnapshotCreate();
    if (snapshot != NULL) {
        int deviceCount = SnapshotGetDeviceCount(snapshot);

        for (int i = 0; i < deviceCount; i++) {
            Device* device = SnapshotGetDevice(snapshot, i);
            int memCount = DeviceGetMemoryBlockCount(device);

            if (device->type == DEVTYPE_CPU && memCount > 0) {
                memoryBlock = DeviceGetMemoryBlock(device, 0);
            }
        }
    }

    if (currentSnapshot != NULL) {
        SnapshotDestroy(currentSnapshot);
    }

    currentSnapshot = snapshot;
    
    if (isRunning) {
        EmulatorRun();
    }
}

void createSnapshot(bool reset = false)
{
    if ((!hasSnapshot && !reset) || GetEmulatorState() == EMULATOR_RUNNING) {
        return;
    }

    Snapshot* snapshot = SnapshotCreate();
    if (snapshot != NULL) {
        int deviceCount = SnapshotGetDeviceCount(snapshot);

        for (int i = 0; i < deviceCount; i++) {
            Device* device = SnapshotGetDevice(snapshot, i);
            int memCount = DeviceGetMemoryBlockCount(device);

            if (device->type == DEVTYPE_CPU && memCount > 0) {
                MemoryBlock* mem = DeviceGetMemoryBlock(device, 0);
                if (mem->size == 0x10000) {
                    if (reset) {
                        memcpy(snapshotData.dataOld, mem->memory, 0x10000);
                        memset(snapshotData.mask,    1,           0x10000);
                        memset(snapshotData.maskRef, 1,           0x10000);
                    }
                    else {
                        memcpy(snapshotData.dataOld, snapshotData.dataNew, 0x10000);
                        memcpy(snapshotData.maskRef, snapshotData.mask,    0x10000);
                    }
                    memcpy(snapshotData.dataNew, mem->memory, 0x10000);
                    hasSnapshot = true;
                }
            }
        }
    }

    SnapshotDestroy(snapshot);
}


static void addAddress(HWND hwnd, int entry, UInt32 address, Int32 oldData, Int32 newData, Int32 diff) {
    char buffer[64];

    sprintf(buffer, "%.4X", address);
    ListViewInsertItemU(hwnd, entry, buffer);

    sprintf(buffer, DpySizeFormat[dataSize][displayType], oldData);
    ListViewSetItemTextU(hwnd, entry, 1, buffer);
    
    sprintf(buffer, DpySizeFormat[dataSize][displayType], newData);
    ListViewSetItemTextU(hwnd, entry, 2, buffer);
    
    sprintf(buffer, DpySizeFormat[dataSize][displayType], diff);
    ListViewSetItemTextU(hwnd, entry, 3, buffer);
}

void updateListView()
{
    HWND hwnd = GetDlgItem(hDlgSearch, IDC_MEMLIST);
    ListView_DeleteAllItems(hwnd);

    if (!hasSnapshot) {
        return;
    }

    int idx = 0;

    for (int i = 0; i < 0x10000; i++) {
        if (snapshotData.mask[i]) {
            Int32 oldData = getData(dataSize, snapshotData.dataOld, i);
            Int32 newData = getData(dataSize, snapshotData.dataNew, i);
            if (idx >= 1024) {
                ListViewInsertItemU(hwnd, idx, Language::truncated);
                break;
            }
            if (displayType == DPY_DECIMAL) {
                addAddress(hwnd, idx++, i, oldData, newData, newData - oldData);
            }
            else {
                addAddress(hwnd, idx++, i, oldData, newData, (newData - oldData) & DataMask[dataSize]);
            }
        }
    }
}

struct SearchInfo
{
    CompareType  compareType;
    DataSize     dataSize;
    CompareValue compareValue;
    Int32        cmpChangeVal;
    Int32        cmpSpecificVal;

    SearchInfo(CompareType ct, DataSize ds, CompareValue cv, Int32 ccv, Int32 csv)
    : compareType(ct), dataSize(ds), compareValue(cv), cmpChangeVal(ccv), cmpSpecificVal(csv) {}

    bool operator==(const SearchInfo& si) {
        return si.compareType    == compareType  &&
               si.dataSize       == dataSize     &&
               si.compareValue   == compareValue &&
               si.cmpChangeVal   == cmpChangeVal &&
               si.cmpSpecificVal == cmpSpecificVal;
    }
};

typedef std::list<SearchInfo> SearchInfoList;
SearchInfoList searchInfoList;

void clearSearch()
{
    while(!searchInfoList.empty()) {
        searchInfoList.pop_front();
    }
    canUndo = false;
}

bool doSearch(CompareType  compareType, 
              DataSize     dataSize,
              CompareValue compareValue,
              Int32        cmpChangeVal,
              Int32        cmpSpecificVal,
              bool         addToUndoList = true)
{
    SearchInfo searchInfo(compareType, dataSize, compareValue, cmpChangeVal, cmpSpecificVal);

    if (addToUndoList) {
        if (!searchInfoList.empty()) {
            SearchInfo lastSearch = searchInfoList.back();
            if (searchInfo == lastSearch) {
                return false;
            }
        }
    }

    bool modified = false;

    for (Int32 i = 0; i < 0x10000; i++) {
        if (snapshotData.mask[i] == 0) {
            continue;
        }
        Int32 cmp1 = 0;
        Int32 cmp2 = 0;
        switch (compareValue) {
        case CMP_OLDVALUE:
            cmp1 = getData(dataSize, snapshotData.dataOld, i);
            cmp2 = getData(dataSize, snapshotData.dataNew, i);
            break;
        case CMP_CHANGE:
            cmp1 = cmpChangeVal;
            cmp2 = getData(dataSize, snapshotData.dataNew, i) - getData(dataSize, snapshotData.dataOld, i);
            break;
        case CMP_SPECIFIC:
            cmp1 = cmpSpecificVal;
            cmp2 = getData(dataSize, snapshotData.dataNew, i);
            break;
        }

        UInt8 oldMask = snapshotData.mask[i];
        switch (compareType) {
        case CMP_EQUAL:
            snapshotData.mask[i] = (cmp2 == cmp1);
            break;
        case CMP_NOTEQUAL:
            snapshotData.mask[i] = (cmp2 != cmp1);
            break;
        case CMP_LESSTHAN:
            snapshotData.mask[i] = (cmp2 < cmp1);
            break;
        case CMP_LESSOREQUAL:
            snapshotData.mask[i] = (cmp2 <= cmp1);
            break;
        case CMP_GRATERTHAN:
            snapshotData.mask[i] = (cmp2 > cmp1);
            break;
        case CMP_GREATEROREQUAL:
            snapshotData.mask[i] = (cmp2 >= cmp1);
            break;
        }

        modified |= snapshotData.mask[i] != oldMask;
    }

    if (addToUndoList) {
        if (modified) {        
            searchInfoList.push_back(searchInfo);
            canUndo = true;
        }
    }
    return modified;
}

bool undoSearch()
{
    if (searchInfoList.empty()) {
        return false;
    }
    searchInfoList.pop_back();

    memcpy(snapshotData.mask, snapshotData.maskRef, 0x10000);

    for (SearchInfoList::iterator i = searchInfoList.begin(); i != searchInfoList.end(); ++i) {
        doSearch(i->compareType, i->dataSize, i->compareValue, i->cmpChangeVal, i->cmpSpecificVal, false);
    }

    canUndo = !searchInfoList.empty();
    return true;
}

void updateSearchFields(HWND hDlg)
{
    setBtCheck(hDlg, IDC_CMP_EQUAL,          compareType == CMP_EQUAL);
    setBtCheck(hDlg, IDC_CMP_NOTEQUAL,       compareType == CMP_NOTEQUAL);
    setBtCheck(hDlg, IDC_CMP_LESSTHAN,       compareType == CMP_LESSTHAN);
    setBtCheck(hDlg, IDC_CMP_LESSOREQUAL,    compareType == CMP_LESSOREQUAL);
    setBtCheck(hDlg, IDC_CMP_GREATERTHAN,    compareType == CMP_GRATERTHAN);
    setBtCheck(hDlg, IDC_CMP_GREATEROREQUAL, compareType == CMP_GREATEROREQUAL);
    
    setBtCheck(hDlg, IDC_VAL_DEC, displayType == DPY_DECIMAL);
    setBtCheck(hDlg, IDC_VAL_HEX, displayType == DPY_HEXADECIMAL);
    
    setBtCheck(hDlg, IDC_VAL_8BIT,  dataSize == DATASIZE_8BIT);
    setBtCheck(hDlg, IDC_VAL_16BIT, dataSize == DATASIZE_16BIT);
    
    setBtCheck(hDlg, IDC_CMP_OLDVAL,    compareValue == CMP_OLDVALUE);
    setBtCheck(hDlg, IDC_CMP_CHANGE,    compareValue == CMP_CHANGE);
    setBtCheck(hDlg, IDC_CMP_SPECIFIC,  compareValue == CMP_SPECIFIC);

    EnableWindow(GetDlgItem(hDlg, IDC_CMP_VALCHANGE),   compareValue == CMP_CHANGE);
    EnableWindow(GetDlgItem(hDlg, IDC_CMP_VALSPECIFIC), compareValue == CMP_SPECIFIC);

    char buffer[32];
    
    sprintf(buffer, DpyFormat[displayType], cmpChangeVal & DataMask[dataSize]);
    SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_VALCHANGE), buffer);

    sprintf(buffer, DpyFormat[displayType], cmpSpecificVal & DataMask[dataSize]);
    SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_VALSPECIFIC), buffer);
}

void checkNumberInput(HWND hDlg, int id, DisplayType displayType, DataSize dataSize, Int32* value)
{
    static bool isUpdating = false;

    if (isUpdating) {
        return;
    }

    isUpdating = true;
    char buffer[16];
    GetWindowText(GetDlgItem(hDlg, id), buffer, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;

    bool isEmpty = strlen(buffer) == 0;
    bool negOnly = strcmp(buffer, "-") == 0 || strcmp(buffer, "0-") == 0;

    if (negOnly) {
        strcpy(buffer, "-");
        *value = 0;
    }
    else if (isEmpty) {
        *value = 0;
    }
    else {
        bool valid = true;
        for (UInt32 i = 0; i < strlen(buffer); i++) {
            if (displayType == DPY_DECIMAL) {
                valid &= (buffer[i] >= '0' && buffer[i] <= '9') || 
                          buffer[0] == '-';
            }
            else {
                valid &= (buffer[i] >= '0' && buffer[i] <= '9') ||
                        (buffer[i] >= 'A' && buffer[i] <= 'F') ||
                        (buffer[i] >= 'a' && buffer[i] <= 'f');
            }
        }

        if (valid) {
            Int32 val;
            if (buffer[0] == '-') {
                sscanf(buffer + 1, DpyFormat[displayType], &val);
                if (val <= DataMask[dataSize]) {
                    *value = -val;
                }
            }
            else {
                sscanf(buffer, DpyFormat[displayType], &val);
                if (val <= DataMask[dataSize]) {
                    *value = val;
                }
            }
        }

        UInt32 selStart = 0;
        UInt32 selEnd = 0;
        SendDlgItemMessage(hDlg, id, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (*value < 0) {
            sprintf(buffer, DpyFormat[displayType], -1 * (-1 * *value & DataMask[dataSize]));
        }
        else {
            sprintf(buffer, DpyFormat[displayType], *value & DataMask[dataSize]);
        }
    }
    SetWindowTextU(GetDlgItem(hDlg, id), buffer);
    SendDlgItemMessage(hDlg, id, EM_SETSEL, 8, 8);
    isUpdating = false;
}

void prepareAndShowAddCheatDialog(HWND hDlg)
{
    HWND hwnd = GetDlgItem(hDlg, IDC_MEMLIST);

    if (ListView_GetSelectedCount(hwnd)) {
        int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);

        if (index >= 0) {
            char buffer[32];
            LVITEM lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.pszText = buffer;
            lvi.cchTextMax = sizeof(buffer) - 1;
            lvi.iItem = index;
            lvi.iSubItem = 0;

            if (ListView_GetItem(hwnd, &lvi)) {
                int address;
                if (1 == sscanf(buffer, "%x", &address)) {
                    CheatInfo ci(Language::newCheat, address, snapshotData.dataNew[address], 
                                 dataSize, displayType, true);
                    if (showCheatDialog(&ci)) {
                        addCheat(&ci);
                        updateCheatList();
                        updateButtons();
                    }
                }
            }
        }
    }
}

static INT_PTR CALLBACK searchProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static int currIndex;

    switch (iMsg) {
    case WM_INITDIALOG:
        {
            ApplyDarkMode(hDlg);
            currIndex = -1;
    
            HWND hwnd = GetDlgItem(hDlg, IDC_MEMLIST);

            ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT);

            /* Distribute the listview client width across the four columns
            ** (~28 / 24 / 24 / 24) so the table fills available space at any
            ** DPI / dialog size. */
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int total = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL) - 4;
                if (total < 200) total = 200;
                int wAddr = total * 28 / 100;
                int wOld  = total * 24 / 100;
                int wNew  = total * 24 / 100;
                int wChg  = total - wAddr - wOld - wNew;
                ListViewInsertColumnU(hwnd, 0, wAddr, Language::address);
                ListViewInsertColumnU(hwnd, 1, wOld,  Language::oldValue);
                ListViewInsertColumnU(hwnd, 2, wNew,  Language::newValue);
                ListViewInsertColumnU(hwnd, 3, wChg,  Language::change);
            }

            darkenListView(hwnd);
            
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMPTYPE), Language::compareType);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_EQUAL), Language::equal);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_NOTEQUAL), Language::notEqual);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_LESSTHAN), Language::lessThan);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_LESSOREQUAL), Language::lessOrEqual);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_GREATERTHAN), Language::greaterThan);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_GREATEROREQUAL), Language::greaterOrEqual);
            SetWindowTextU(GetDlgItem(hDlg, IDC_DISPLAY), Language::display);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_DEC), Language::decimal);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_HEX), Language::hexadecimal);
            SetWindowTextU(GetDlgItem(hDlg, IDC_DATASIZE), Language::dataSize);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_8BIT), Language::eightBit);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_16BIT), Language::sixteenBit);
            SetWindowTextU(GetDlgItem(hDlg, IDC_SEARCHTYPE), Language::compareNewValueWith);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_OLDVAL), Language::oldValue);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_CHANGE), Language::change);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_SPECIFIC), Language::specificValue);
            SetWindowTextU(GetDlgItem(hDlg, IDC_SNAPSHOT), Language::snapshot);
            SetWindowTextU(GetDlgItem(hDlg, IDC_SEARCH), Language::search);
            SetWindowTextU(GetDlgItem(hDlg, IDC_ADDCHEAT), Language::addCheat);
            SetWindowTextU(GetDlgItem(hDlg, IDC_UNDO), Language::undo);
            SetWindowTextU(GetDlgItem(hDlg, IDC_RUNSTOP), GetEmulatorState() == EMULATOR_RUNNING ? Language::pause : Language::run);
        }

        updateSearchFields(hDlg);

        return FALSE;

    case WM_COMMAND:
        {
            DisplayType  oldDisplayType = displayType;
            DataSize     oldDataSize    = dataSize;

            switch (LOWORD(wParam)) {
            case IDC_CMP_EQUAL:          compareType  = CMP_EQUAL;          break;
            case IDC_CMP_NOTEQUAL:       compareType  = CMP_NOTEQUAL;       break;
            case IDC_CMP_LESSTHAN:       compareType  = CMP_LESSTHAN;       break;
            case IDC_CMP_LESSOREQUAL:    compareType  = CMP_LESSOREQUAL;    break;
            case IDC_CMP_GREATERTHAN:    compareType  = CMP_GRATERTHAN;     break;
            case IDC_CMP_GREATEROREQUAL: compareType  = CMP_GREATEROREQUAL; break;
            case IDC_VAL_DEC:            displayType  = DPY_DECIMAL;        break;
            case IDC_VAL_HEX:            displayType  = DPY_HEXADECIMAL;    break;
            case IDC_VAL_8BIT:           dataSize     = DATASIZE_8BIT;      break;
            case IDC_VAL_16BIT:          dataSize     = DATASIZE_16BIT;     break;
            case IDC_CMP_OLDVAL:         compareValue = CMP_OLDVALUE;       break;
            case IDC_CMP_CHANGE:         compareValue = CMP_CHANGE;         break;
            case IDC_CMP_SPECIFIC:       compareValue = CMP_SPECIFIC;       break;
            case IDC_CMP_VALCHANGE:
                checkNumberInput(hDlg, IDC_CMP_VALCHANGE, displayType, dataSize, &cmpChangeVal);
                break;

            case IDC_CMP_VALSPECIFIC:
                checkNumberInput(hDlg, IDC_CMP_VALSPECIFIC, displayType, dataSize, &cmpSpecificVal);
                break;
            }

            if (oldDisplayType != displayType || oldDataSize != dataSize) {
                updateListView();
            }
        }

        switch (LOWORD(wParam)) {
        case IDC_VAL_DEC:
        case IDC_VAL_HEX:
        case IDC_VAL_8BIT:
        case IDC_VAL_16BIT:
        case IDC_CMP_OLDVAL:
        case IDC_CMP_CHANGE:
        case IDC_CMP_SPECIFIC:
            {
                EnableWindow(GetDlgItem(hDlg, IDC_CMP_VALCHANGE),   compareValue == CMP_CHANGE);
                EnableWindow(GetDlgItem(hDlg, IDC_CMP_VALSPECIFIC), compareValue == CMP_SPECIFIC);

                char buffer[32];
                
                sprintf(buffer, DpyFormat[displayType], cmpChangeVal & DataMask[dataSize]);
                SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_VALCHANGE), buffer);

                sprintf(buffer, DpyFormat[displayType], cmpSpecificVal & DataMask[dataSize]);
                SetWindowTextU(GetDlgItem(hDlg, IDC_CMP_VALSPECIFIC), buffer);
            }
            break;

        case IDC_SNAPSHOT:
            createSnapshot(true);
            clearSearch();
            updateListView();
            updateButtons();
            break;

        case IDC_SEARCH:
            if (doSearch(compareType, dataSize, compareValue, cmpChangeVal, cmpSpecificVal)) {
                updateListView();
            }
            updateButtons();
            break;

        case IDC_UNDO:
            if (undoSearch()) {
                updateListView();
            }
            updateButtons();
            break;

        case IDC_ADDCHEAT: 
            prepareAndShowAddCheatDialog(hDlg);
            break;

        case IDC_RUNSTOP:
            if (GetEmulatorState() == EMULATOR_RUNNING) {
                EmulatorPause();
            }
            else {
                EmulatorRun();
            }
            updateButtons();
            break;
        }
        return TRUE;

    case WM_NOTIFY:
        switch (wParam) {
        case IDC_MEMLIST:
            /* NM_CUSTOMDRAW: keep rows dark when disabled (EnableWindow FALSE
            ** otherwise reverts each row to COLOR_WINDOW); dim text when inactive. */
            if ((((NMHDR*)lParam)->code) == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lParam;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                    return TRUE;
                }
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    if (IsDarkMode()) {
                        BOOL en = IsWindowEnabled(cd->nmcd.hdr.hwndFrom);
                        cd->clrText   = en ? GetDarkFg() : RGB(128, 128, 128);
                        cd->clrTextBk = GetDarkBg();
                    }
                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                    return TRUE;
                }
            }
            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMCHANGED) {
                HWND hwnd = GetDlgItem(hDlg, IDC_MEMLIST);

                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);

                    if (currIndex == -1 && index != -1) {
                        canAddCheat = true;
                        updateButtons();
                    }
                    currIndex = index;
                }
                else {
                    if (currIndex != -1) {
                        canAddCheat = false;
                        updateButtons();
                    }
                    currIndex = -1;
                }
            }

            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMACTIVATE) {
                prepareAndShowAddCheatDialog(hDlg);
                return TRUE;
            }
        }
        break;

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_3DLIGHT));
        return (INT_PTR)hBrush;

    case WM_ERASEBKGND:
        return TRUE;
    }
    return FALSE;
}

////////////////////////////////////////////////////////////////////////

typedef std::list<CheatInfo> CheatList;

CheatList cheatList;

void clearAllCheats()
{
    while(!cheatList.empty()) {
        cheatList.pop_front();
    }

    canRemoveAllCheat = false;
}

void addCheat(char* description, UInt32 address, Int32 value, DataSize dataSize, DisplayType displayType, bool enabled)
{
    CheatInfo ci(description, address, value, dataSize, displayType, enabled);
    CheatList::iterator i = cheatList.begin();

    for (; i != cheatList.end(); ++i) {
        if (i->address > address) break;
    }
    cheatList.insert(i, ci);
    
    canRemoveAllCheat = !cheatList.empty();
}

void addCheat(CheatInfo* ci) 
{
    addCheat(ci->description, ci->address, ci->value, ci->dataSize, ci->displayType, ci->enabled);
}

void removeCheat(int idx) 
{
    for (CheatList::iterator i = cheatList.begin(); i != cheatList.end(); ++i) {
        if (idx-- == 0) {
            cheatList.erase(i);
            break;
        }
    }
    
    canRemoveAllCheat = !cheatList.empty();
}

void updateEnableCheat(int idx, bool enable)
{
    for (CheatList::iterator i = cheatList.begin(); i != cheatList.end(); ++i) {
        if (idx-- == 0) {
            (*i).enabled = enable;
            break;
        }
    }
}

void executeCheats()
{
    if (!canCheat) {
        return;
    }

    if (memoryBlock == NULL) {
        updateSnapshot();
    }

    if (memoryBlock == NULL) {
        return;
    }

    for (CheatList::iterator i = cheatList.begin(); i != cheatList.end(); ++i) {
        CheatInfo& ci = (*i);

        if (ci.enabled) {
            int size = 1;
            UInt8 data[2];
            data[0] = (UInt8)(ci.value & 0xff);
            if (ci.dataSize == DATASIZE_16BIT) {
                size = 2;
                data[1] = data[0];
                data[0] = (UInt8)(ci.value >> 8);
            }
            DeviceWriteMemoryBlockMemory(memoryBlock, data, ci.address, size);
        }
    }
}

static void addCheat(HWND hwnd, int entry, char* description, bool enable, UInt32 address, 
                     Int32 value, DataSize dataSize, DisplayType displayType)
{
    char buffer[64];
    
    ListViewInsertItemU(hwnd, entry, description);

    sprintf(buffer, "%.4X", address);
    ListViewSetItemTextU(hwnd, entry, 1, buffer);
    
    sprintf(buffer, DpySizeFormat[dataSize][displayType], value);
    ListViewSetItemTextU(hwnd, entry, 2, buffer);

    ListView_SetCheckState(hwnd, entry, enable);

    SetWindowTextU(GetDlgItem(hDlgCheats, IDC_ENABLE), enable ? Language::disable : Language::enable);
}

static void updateCheatList()
{
    HWND hwnd = GetDlgItem(hDlgCheats, IDC_CHEATLIST);
    ListView_DeleteAllItems(hwnd);
    
    canRemoveCheat = false;

    int idx = 0;
    for (CheatList::iterator i = cheatList.begin(); i != cheatList.end(); ++i) {
        addCheat(hwnd, idx++, i->description, i->enabled, i->address, i->value, i->dataSize, i->displayType);
    }
}

static CheatInfo* getCheat(int index)
{
    int idx = 0;
    for (CheatList::iterator i = cheatList.begin(); i != cheatList.end(); ++i) {
        if (idx++ == index) {
            return &(*i);
        }
    }
    return NULL;
}

static bool saveCheatFile(const char* filename)
{
    if (filename == NULL) {
        return false;
    }

    FILE* f = fopenU(filename, "w");
    if (f == NULL) {
        return FALSE;
    }

    const char* fname = filename + strlen(filename) - 1;
    while (fname >= filename && *fname != '/' && *fname != '\\') {
        fname--;
    }

    fprintf(f, "!cheats v2.0 for blueMSX: %s\n", fname);

    for (int index = 0; index < 2048; index++) {
        CheatInfo* ci = getCheat(index);
        if (ci == NULL) {
            break;
        }
        fprintf(f, "%.4X:%.2X:%d:%d:%s\n", ci->address, ci->value, ci->dataSize, ci->displayType, ci->description);
    }
    fclose(f);

    return true;
}

static bool loadCheatFile(const char* filename)
{
    if (filename == NULL) {
        return false;
    }

    FILE* f = fopenU(filename, "r");
    if (f == NULL) {
        return FALSE;
    }

    clearAllCheats();

    for (int i = 0; i < 1000; i++) {
        char buffer[1024];
        if (NULL == fgets(buffer, sizeof(buffer), f)) {
            break;
        }
        if (buffer[0] == '!') {
            continue;
        }
        int memType;
        int address;
        int value;
        int dummy;
        int dataSize = DATASIZE_8BIT;
        int displayType = DPY_HEXADECIMAL;

        int rv = sscanf(buffer, "%X:%X:%d:%d\n", &address, &value, &dataSize, &displayType);
        if (rv != 4) {
            rv = sscanf(buffer, "%d,%d,%d,%d", &memType, &address, &value, &dummy);
        }
        if (rv != 4) continue;
        char* desc = buffer + strlen(buffer) - 1;
        if (desc[0] == '\r' || desc[0] == '\n') desc[0] = 0;
        if (desc[-1] == '\r' || desc[-1] == '\n') desc[-1] = 0;
        desc = buffer;
        int commaCnt = 0;
        while (*desc && commaCnt < 4) {
            if (*desc == ',' || *desc == ':') commaCnt++;
            desc++;
        }

        addCheat(desc, address, value, (DataSize)dataSize, (DisplayType)displayType, false);
    }

    fclose(f);

    return true;
}


static INT_PTR CALLBACK cheatsProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static int currIndex;

    switch (iMsg) {
    case WM_INITDIALOG:
        {
            ApplyDarkMode(hDlg);
            HICON hIcon = LoadIcon(GetDllHinstance(), MAKEINTRESOURCE(IDI_OPEN));
            SendDlgItemMessage(hDlg, IDC_OPEN, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);

            hIcon = LoadIcon(GetDllHinstance(), MAKEINTRESOURCE(IDI_SAVE));
            SendDlgItemMessage(hDlg, IDC_SAVE, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);

            HWND hwnd = GetDlgItem(hDlg, IDC_CHEATLIST);

            ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

            /* DPI-scaled address/value column widths; description fills the
            ** rest minus SM_CXVSCROLL so it does not jump on scrollbar pop. */
            {
                HDC hdc = GetDC(hwnd);
                int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
                if (hdc) ReleaseDC(hwnd, hdc);
                if (dpi <= 0) dpi = 96;
                int wAddr = MulDiv(80, dpi, 96);
                int wVal  = MulDiv(60, dpi, 96);
                RECT rc;
                GetClientRect(hwnd, &rc);
                int total = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL) - 4;
                int wDesc = total - wAddr - wVal;
                if (wDesc < 100) wDesc = 100;
                ListViewInsertColumnU(hwnd, 0, wDesc, Language::description);
                ListViewInsertColumnU(hwnd, 1, wAddr, Language::address);
                ListViewInsertColumnU(hwnd, 2, wVal,  Language::value);
            }

            darkenListView(hwnd);

            SetWindowTextU(GetDlgItem(hDlg, IDC_REMOVEALL), Language::removeAll);
            SetWindowTextU(GetDlgItem(hDlg, IDC_ADDCHEAT), Language::addCheat);
            SetWindowTextU(GetDlgItem(hDlg, IDC_ENABLE), Language::enable);
            SetWindowTextU(GetDlgItem(hDlg, IDC_CHEATFILETEXT), Language::cheatFile);
            SetWindowTextU(GetDlgItem(hDlg, IDC_REMOVE), Language::remove);
            SetWindowTextU(GetDlgItem(hDlg, IDC_ACTIVECHEATS), Language::activeCheats);
            SetWindowTextU(GetDlgItem(hDlg, IDC_RUNSTOP), GetEmulatorState() == EMULATOR_RUNNING ? Language::pause : Language::run);

            /* Path edit: MS UI Gothic for yen-sign separators + subclass that
            ** shrinks the client rect to text height so the glyph sits in the
            ** middle of the box instead of top-aligned against the border. */
            {
                HWND hPath = GetDlgItem(hDlg, IDC_FILENAME);
                SendMessageW(hPath, WM_SETFONT, (WPARAM)getPathFont(hDlg),
                             MAKELPARAM(TRUE, 0));
                SetWindowSubclass(hPath, trainerPathEditProc,
                                  TRAINER_PATH_EDIT_SUBCLASS_ID, 0);
                /* Trigger WM_NCCALCSIZE recompute now that the new font /
                ** subclass are in place. */
                SetWindowPos(hPath, NULL, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        }

        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_OPEN:
            {
                char* filename = cheatFileDialog(hDlg, ".mcf", false);
                if (loadCheatFile(filename)) {
                    SetWindowTextU(GetDlgItem(hDlg, IDC_FILENAME), filename);
                    updateCheatList();
                    updateButtons();
                }
            }
            break;

        case IDC_REMOVEALL:
            SetWindowTextU(GetDlgItem(hDlg, IDC_FILENAME), "");
            clearAllCheats();
            updateCheatList();
            updateButtons();
            break;

        case IDC_REMOVE:
            {
                HWND hwnd = GetDlgItem(hDlg, IDC_CHEATLIST);

                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
                    removeCheat(index);
                    ListView_DeleteItem(hwnd, index);
                    ListView_SetItemState(hwnd, index, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
                    canRemoveCheat = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED) != -1;
                    updateButtons();
                }
            }
            break;

        case IDC_ADDCHEAT: 
            {
                CheatInfo ci(Language::newCheat, 0, 0, DATASIZE_8BIT, displayType, true);
                if (showCheatDialog(&ci)) {
                    addCheat(&ci);
                    updateCheatList();
                    updateButtons();
                }
            }
            break;

        case IDC_RUNSTOP:
            if (GetEmulatorState() == EMULATOR_RUNNING) {
                EmulatorPause();
            }
            else {
                EmulatorRun();
            }
            updateButtons();
            break;

        case IDC_ENABLE:
            {
                HWND hwnd = GetDlgItem(hDlg, IDC_CHEATLIST);

                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
                    bool enable = !ListView_GetCheckState(hwnd, index);
                    ListView_SetCheckState(hwnd, index, enable);
                    SetWindowTextU(GetDlgItem(hDlgCheats, IDC_ENABLE), enable ? Language::disable : Language::enable);
                    updateEnableCheat(index, enable);
                }
            }
            break;

        case IDC_SAVE:
            {
                char* filename = cheatFileDialog(hDlg, ".mcf", true);
                saveCheatFile(filename);
            }
            break;
        }
        break;

    case WM_NOTIFY:
        switch (wParam) {
        case IDC_CHEATLIST:
            if ((((NMHDR*)lParam)->code) == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lParam;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                    return TRUE;
                }
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    if (IsDarkMode()) {
                        BOOL en = IsWindowEnabled(cd->nmcd.hdr.hwndFrom);
                        cd->clrText   = en ? GetDarkFg() : RGB(128, 128, 128);
                        cd->clrTextBk = GetDarkBg();
                    }
                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                    return TRUE;
                }
            }
            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMCHANGED) {
                HWND hwnd = GetDlgItem(hDlg, IDC_CHEATLIST);

                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);

                    if (currIndex == -1 && index != -1) {
                        canRemoveCheat = true;
                        bool enable = ListView_GetCheckState(hwnd, index) ? true : false;
                        SetWindowTextU(GetDlgItem(hDlgCheats, IDC_ENABLE), enable ? Language::disable : Language::enable);
                        updateButtons();
                    }
                    currIndex = index;
                }
                else {
                    if (currIndex != -1) {
                        canRemoveCheat = false;
                        updateButtons();
                    }
                    currIndex = -1;
                }

                int index = ((NMLISTVIEW FAR *)lParam)->iItem;
                if (index != -1) {
                    bool enable = ListView_GetCheckState(hwnd, index) != 0;
                    SetWindowTextU(GetDlgItem(hDlgCheats, IDC_ENABLE), enable ? Language::disable : Language::enable);
                    updateEnableCheat(index, enable);
                }
            }

            if ((((NMHDR FAR *)lParam)->code) == LVN_ITEMACTIVATE) {
                HWND hwnd = GetDlgItem(hDlg, IDC_CHEATLIST);

                if (ListView_GetSelectedCount(hwnd)) {
                    int index = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
                    CheatInfo* ci = getCheat(index);
                    if (ci != NULL) {
                        if (showCheatDialog(ci)) {
                            CheatInfo newCi = *ci;
                            removeCheat(index);
                            addCheat(&newCi);
                            updateCheatList();
                            updateButtons();
                        }
                    }
                }
                return TRUE;
            }
        }
        break;

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_3DLIGHT));
        return (INT_PTR)hBrush;

    case WM_ERASEBKGND:
        return TRUE;
    }
    return FALSE;
}

////////////////////////////////////////////////////////////////////////
static INT_PTR CALLBACK cheatDialogProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static CheatInfo* ci;

    switch (iMsg) {
    case WM_INITDIALOG:
        {
            ApplyDarkMode(hDlg);
            char buffer[32];
            ci = (CheatInfo*)lParam;

            sprintf(buffer, "%.4X", ci->address);
            SetWindowTextU(GetDlgItem(hDlg, IDC_ADDRESSEDIT), buffer);
            sprintf(buffer, DpySizeFormat[ci->dataSize][ci->displayType], ci->value);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VALUEEDIT), buffer);
            SetWindowTextU(GetDlgItem(hDlg, IDC_DESCRIPTIONEDIT), ci->description);
            
            setBtCheck(hDlg, IDC_VAL_DEC, ci->displayType == DPY_DECIMAL);
            setBtCheck(hDlg, IDC_VAL_HEX, ci->displayType == DPY_HEXADECIMAL);            
            setBtCheck(hDlg, IDC_VAL_8BIT,  ci->dataSize == DATASIZE_8BIT);
            setBtCheck(hDlg, IDC_VAL_16BIT, ci->dataSize == DATASIZE_16BIT);

            SetWindowTextU(GetDlgItem(hDlg, IDOK), Language::ok);
            SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), Language::cancel);
            SetWindowTextU(GetDlgItem(hDlg, IDC_DESCRIPTIONTEXT), Language::description);
            SetWindowTextU(GetDlgItem(hDlg, IDC_ADDRESSTEXT), Language::address);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VALUETEXT), Language::value);
            SetWindowTextU(GetDlgItem(hDlg, IDC_DISPLAY), Language::displayValueAs);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_DEC), Language::decimal);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_HEX), Language::hexadecimal);
            SetWindowTextU(GetDlgItem(hDlg, IDC_DATASIZE), Language::dataSize);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_8BIT), Language::eightBit);
            SetWindowTextU(GetDlgItem(hDlg, IDC_VAL_16BIT), Language::sixteenBit);
        }
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_VAL_DEC:   ci->displayType  = DPY_DECIMAL;        break;
        case IDC_VAL_HEX:   ci->displayType  = DPY_HEXADECIMAL;    break;
        case IDC_VAL_8BIT:  ci->dataSize     = DATASIZE_8BIT;      break;
        case IDC_VAL_16BIT: ci->dataSize     = DATASIZE_16BIT;     break;

        case IDOK:
            EndDialog(hDlg, TRUE);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;

        case IDC_VALUEEDIT:
            checkNumberInput(hDlg, IDC_VALUEEDIT, ci->displayType, ci->dataSize, &ci->value);
            break;
            
        case IDC_ADDRESSEDIT:
            checkNumberInput(hDlg, IDC_ADDRESSEDIT, DPY_HEXADECIMAL, DATASIZE_16BIT, (Int32*)&ci->address);
            break;

        case IDC_DESCRIPTIONEDIT:
            GetWindowText(GetDlgItem(hDlg, IDC_DESCRIPTIONEDIT), ci->description, sizeof(ci->description) - 1);
            ci->description[sizeof(ci->description) - 1] = 0;
        }

        switch (LOWORD(wParam)) {
        case IDC_VAL_DEC:
        case IDC_VAL_HEX:
        case IDC_VAL_8BIT:
        case IDC_VAL_16BIT:
            {
                char buffer[32];
                sprintf(buffer, DpySizeFormat[ci->dataSize][ci->displayType], ci->value);
                SetWindowTextU(GetDlgItem(hDlg, IDC_VALUEEDIT), buffer);
            }
            break;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }
    return FALSE;
}

bool showCheatDialog(CheatInfo* ci)
{
    CheatInfo tmp = *ci;
    if (DialogBoxParam(GetDllHinstance(), MAKEINTRESOURCE(IDD_ADDCHEAT), 
                       dbgHwnd, cheatDialogProc, (LPARAM)&tmp))
    {
        *ci = tmp;
        ci->enabled = false;
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////

/* Stretch blueTrainer.bmp (432x63) to dialog width, shift IDC_TAB / grow
** the dialog to fit.  Caller frees the previous STM_SETIMAGE bitmap. */
static void scaleTrainerHeaderBitmap(HWND hDlg)
{
    const int origW = 432;
    const int origH = 63;

    HWND hHdr = GetDlgItem(hDlg, IDC_HEADERBMP);
    if (!hHdr) return;

    RECT clientRc;
    GetClientRect(hDlg, &clientRc);
    int targetW = clientRc.right;
    if (targetW <= 0) return;
    int targetH = MulDiv(targetW, origH, origW);

    /* Snapshot current static height (SS_BITMAP auto-fits to the bitmap's
    ** natural pixel size at this point) so we can compute the layout shift. */
    RECT bmpRc;
    GetWindowRect(hHdr, &bmpRc);
    int prevH = bmpRc.bottom - bmpRc.top;

    HBITMAP hSrc = LoadBitmap(GetDllHinstance(), MAKEINTRESOURCE(IDB_TRAINER));
    if (!hSrc) return;

    HDC hScreen = GetDC(NULL);
    HDC hSrcDc  = CreateCompatibleDC(hScreen);
    HDC hDstDc  = CreateCompatibleDC(hScreen);
    HBITMAP hDst = CreateCompatibleBitmap(hScreen, targetW, targetH);
    HGDIOBJ oldSrc = SelectObject(hSrcDc, hSrc);
    HGDIOBJ oldDst = SelectObject(hDstDc, hDst);
    SetStretchBltMode(hDstDc, HALFTONE);
    SetBrushOrgEx(hDstDc, 0, 0, NULL);
    StretchBlt(hDstDc, 0, 0, targetW, targetH, hSrcDc, 0, 0, origW, origH, SRCCOPY);
    SelectObject(hSrcDc, oldSrc);
    SelectObject(hDstDc, oldDst);
    DeleteDC(hSrcDc);
    DeleteDC(hDstDc);
    DeleteObject(hSrc);
    ReleaseDC(NULL, hScreen);

    HBITMAP hPrev = (HBITMAP)SendMessage(hHdr, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hDst);
    if (hPrev) DeleteObject(hPrev);

    SetWindowPos(hHdr, NULL, 0, 0, targetW, targetH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int deltaY = targetH - prevH;
    if (deltaY != 0) {
        HWND hTab = GetDlgItem(hDlg, IDC_TAB);
        if (hTab) {
            RECT tabRc;
            GetWindowRect(hTab, &tabRc);
            POINT pt = { tabRc.left, tabRc.top };
            ScreenToClient(hDlg, &pt);
            SetWindowPos(hTab, NULL, pt.x, pt.y + deltaY, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        RECT dlgRc;
        GetWindowRect(hDlg, &dlgRc);
        SetWindowPos(hDlg, NULL, 0, 0,
                     dlgRc.right - dlgRc.left,
                     (dlgRc.bottom - dlgRc.top) + deltaY,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static INT_PTR CALLBACK trainerProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg) {
    case WM_INITDIALOG:
        ApplyDarkMode(hDlg);
        if (hBrush == NULL) {
            hBrush = CreateSolidBrush(GetSysColor(COLOR_3DLIGHT));
        }

        scaleTrainerHeaderBitmap(hDlg);

        hDlgSearch  = CreateDialog(GetDllHinstance(), MAKEINTRESOURCE(IDD_SEARCH),  GetDlgItem(hDlg, IDC_TAB), searchProc);
        SetWindowPos(hDlgSearch,  NULL, 3, 24, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        hDlgCheats  = CreateDialog(GetDllHinstance(), MAKEINTRESOURCE(IDD_CHEATS),  GetDlgItem(hDlg, IDC_TAB), cheatsProc);
        SetWindowPos(hDlgCheats,  NULL, 3, 24, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ShowWindow(hDlgCheats, SW_NORMAL);

        {
            HWND hTab = GetDlgItem(hDlg, IDC_TAB);
            TabInsertItemU(hTab, 0, Language::activeCheats);
            TabInsertItemU(hTab, 1, Language::findCheats);
        }

        SetTimer(hDlg, CHEAT_TIMER_ID, 100, 0);

        return FALSE;

    case WM_TIMER:
        if (wParam == CHEAT_TIMER_ID) {
            executeCheats();
        }
        return 0;

    case WM_NOTIFY:
        switch (wParam) {
        case IDC_TAB:
            if ((((NMHDR FAR *)lParam)->code) == TCN_SELCHANGE) {
                ShowWindow(hDlgSearch,  SW_HIDE);
                ShowWindow(hDlgCheats,  SW_HIDE);
                switch(TabCtrl_GetCurSel(GetDlgItem(hDlg, IDC_TAB))) {
                case 0:
                default:
                    ShowWindow(hDlgCheats, SW_NORMAL);
                    break;
                case 1:
                    ShowWindow(hDlgSearch, SW_NORMAL);
                    break;
                }
            }
            break;
        }
        break;
        
    case WM_CLOSE:
        hasSnapshot = false;
        ShowWindow(dbgHwnd, SW_HIDE); 
        return TRUE;

    case WM_DESTROY:
        {
            HWND hHdr = GetDlgItem(hDlg, IDC_HEADERBMP);
            if (hHdr) {
                HBITMAP h = (HBITMAP)SendMessage(hHdr, STM_GETIMAGE, IMAGE_BITMAP, 0);
                if (h) DeleteObject(h);
            }
        }
        return FALSE;
    }
    return FALSE;
}


void OnCreateTool() {
}

void OnDestroyTool() {
    if (dbgHwnd != NULL) {
        DestroyWindow(dbgHwnd);
    }
}

void OnShowTool() {
    if (dbgHwnd == NULL) {
        dbgHwnd = CreateDialog(GetDllHinstance(), MAKEINTRESOURCE(IDD_TRAINER), NULL, trainerProc); 
        DWORD dw = GetLastError();
        dw = dw;
    }

    ShowWindow(dbgHwnd, SW_SHOW); 

    canAddCheat = false;
    
    updateListView();
    updateButtons();

    Language::SetLanguage(langId);
}

void OnEmulatorStart() {
    updateButtons();
    canCheat = true;
}

void OnEmulatorStop() {
    memoryBlock = NULL;
    updateButtons();
    canCheat = false;
}

void OnEmulatorPause() {
    createSnapshot();
    clearSearch();
    updateListView();
    updateButtons();
    canCheat = false;
}

void OnEmulatorResume() {
    updateButtons();
    canCheat = true;
}

void OnEmulatorReset() {
}

void OnEmulatorTrace(const char* message)
{
}

void OnEmulatorSetBreakpoint(UInt16 address) {
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 address) {
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address) {
}

const char* OnGetName() {
    return Language::trainerName;
}

void OnSetLanguage(LanguageId languageId)
{
    langId = languageId;
    Language::SetLanguage(langId);
}
