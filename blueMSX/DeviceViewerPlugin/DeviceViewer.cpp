#include <windows.h>
#include "ToolInterface.h"
#include "Resource.h"
#include "resrc1.h"
#include "Win32TextUtf8.h"
#include <string>
#include <commctrl.h>
#include <sstream>
#include <iomanip>

using namespace std;

static HWND deviceViewerHwnd = NULL;
static LanguageId g_langId   = LID_ENGLISH;

/* Minimal localization table -- the plugin had no Language.h system, so
** keep it inline rather than wire up a class hierarchy for ~10 strings. */
struct DvLang {
    const char* refresh;
    const char* run;
    const char* stop;
    const char* pause;
    const char* step;
    const char* statusPrefix;
    const char* statRunning;
    const char* statPaused;
    const char* statStopped;
    const char* caption;
    const char* toolName;
};

static const DvLang DvEn = {
    "Refresh", ">", "O", "''", "]",
    "Status: ", "RUNNING", "PAUSED", "STOPPED",
    "blueMSX - Device Viewer", "Device Viewer"
};

static const DvLang DvJa = {
    "更新", ">", "O", "''", "]",
    "状態: ", "実行中", "一時停止", "停止",
    "blueMSX - デバイスビューア", "デバイスビューア"
};

static const DvLang& dvL()
{
    return g_langId == LID_JAPANESE ? DvJa : DvEn;
}

#define WM_STATUS (WM_USER + 0)

HTREEITEM addChild ( HWND hwnd, HTREEITEM parent, char *pszText, int imageIndex, int param = -1 )
{
	tagTVINSERTSTRUCTA insertStruct;
	tagTVITEMA treeItem;
 
	treeItem.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_PARAM;
	treeItem.cchTextMax			= strlen ( pszText );
	treeItem.pszText			= strdup ( pszText );
	treeItem.iImage				= imageIndex;
	treeItem.iSelectedImage		= imageIndex;
    treeItem.lParam             = param;

	insertStruct.hParent		= parent;
	insertStruct.hInsertAfter	= 0;
	insertStruct.item			= treeItem;
	
	// insert the item
	return TreeView_InsertItem ( hwnd, &insertStruct );
}

HTREEITEM updateChild ( HWND hwnd, HTREEITEM parent, char *pszText, int imageIndex, int param )
{
	tagTVITEMA treeItem;
 
    HTREEITEM child = TreeView_GetChild(hwnd, parent);

    while (child != NULL) {
	    treeItem.hItem = child;
	    treeItem.mask = TVIF_PARAM;
        TreeView_GetItem(hwnd, &treeItem);
        if (treeItem.lParam == param) {
    	    treeItem.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_PARAM;
	        treeItem.cchTextMax			= strlen ( pszText );
	        treeItem.pszText			= strdup ( pszText );
	        treeItem.iImage				= imageIndex;
	        treeItem.iSelectedImage		= imageIndex;	 
            treeItem.lParam             = param;

            TreeView_SetItem(hwnd, &treeItem);

            break;
        }

        child = TreeView_GetNextSibling(hwnd, child);
    }

    return child;
}

HTREEITEM addOrUpdateChild ( HWND hwnd, HTREEITEM parent, char *pszText, int imageIndex, int param )
{
    HTREEITEM child = updateChild(hwnd, parent, pszText, imageIndex, param);
    if (child == NULL) {
        child = addChild(hwnd, parent, pszText, imageIndex, param);
    }
    return child;
}

void SetDeviceInfoString2 ( HWND hDlg )
{
	
	HTREEITEM nodeDeviceName;

	HWND Tree;
	
	Tree = GetDlgItem ( hDlg, IDC_DEVICETREE );
	
	HTREEITEM nodeCpu = NULL;
	HTREEITEM nodeRam = NULL;
	HTREEITEM nodeBios = NULL;
	HTREEITEM nodeCart = NULL;
	HTREEITEM nodeVideo = NULL;
	HTREEITEM nodeAudio = NULL;
	HTREEITEM nodeIoPort = NULL;
	HTREEITEM nodeOther = NULL;
	
	Snapshot* snapshot = SnapshotCreate();
    if (snapshot == NULL) {
        TreeView_DeleteAllItems ( Tree );
        return;
    }

    int deviceCount = SnapshotGetDeviceCount(snapshot);

	for (int i = 0; i < deviceCount; i++) {
        Device* device = SnapshotGetDevice(snapshot, i);
        int j;

        HTREEITEM treeItem = NULL;

        switch (device->type) {
        default:
        case DEVTYPE_UNKNOWN:
		    if (nodeOther  == NULL) nodeOther  = addOrUpdateChild ( Tree, 0, "Other", 0, 0 );
            treeItem = nodeOther;
            break;
        case DEVTYPE_CPU:
		    if (nodeCpu    == NULL) nodeCpu    = addOrUpdateChild ( Tree, 0, "CPU", 0, 1 );
            treeItem = nodeCpu;
            break;
        case DEVTYPE_CART: 
		    if (nodeCart   == NULL) nodeCart   = addOrUpdateChild ( Tree, 0, "Cartridges", 0, 2 );
            treeItem = nodeCart;
            break;
        case DEVTYPE_BIOS: 
		    if (nodeBios   == NULL) nodeBios   = addOrUpdateChild ( Tree, 0, "BIOS", 0, 3 );
            treeItem = nodeBios;
            break;
        case DEVTYPE_RAM:
		    if (nodeRam    == NULL) nodeRam    = addOrUpdateChild ( Tree, 0, "RAM Memory", 0, 4 );
            treeItem = nodeRam;
            break;
        case DEVTYPE_AUDIO:
		    if (nodeAudio  == NULL) nodeAudio  = addOrUpdateChild ( Tree, 0, "Audio Devices", 0, 5 );
            treeItem = nodeAudio;
            break;
        case DEVTYPE_VIDEO:
		    if (nodeVideo  == NULL) nodeVideo  = addOrUpdateChild ( Tree, 0, "Video Devices", 0, 6 );
            treeItem = nodeVideo;
            break;
        case DEVTYPE_IOPORT:
		    if (nodeIoPort == NULL) nodeIoPort = addOrUpdateChild ( Tree, 0, "I/O Ports", 0, 7 );
            treeItem = nodeIoPort;
            break;
        }
    
        nodeDeviceName = addOrUpdateChild ( Tree, treeItem, device->name, 0, device->deviceId );

        int memCount = DeviceGetMemoryBlockCount(device);

        for (j = 0; j < memCount; j++) {
			
            MemoryBlock* mem = DeviceGetMemoryBlock(device, j);
		
			HTREEITEM nodeMemoryItem = addOrUpdateChild ( Tree, nodeDeviceName, mem->name, 0, j );

            char buffer [50];
			
			sprintf ( buffer, "Size: 0x%X", mem->size );
			HTREEITEM nodeMemorySize = addOrUpdateChild ( Tree, nodeMemoryItem, buffer, 0, 0 );
			sprintf ( buffer, "Start: 0x%X", mem->startAddress );
			HTREEITEM nodeMemoryAddress = addOrUpdateChild ( Tree, nodeMemoryItem, buffer, 0, 1 );
			
        }
        
        int regBankCount = DeviceGetRegisterBankCount(device);

        for (j = 0; j < regBankCount; j++) {
            RegisterBank* regBank = DeviceGetRegisterBank(device, j );
            
			HTREEITEM nodeRegsItem = addOrUpdateChild ( Tree, nodeDeviceName, regBank->name, 0, j + 10000 );

            for (UInt32 k = 0; k < regBank->count; k++) {
				char buffer [50];
                if (regBank->reg[k].width == 8) 
				    sprintf ( buffer, "%s: 0x%.2X", regBank->reg[k].name, regBank->reg[k].value );
                else if (regBank->reg[k].width == 16)
				    sprintf ( buffer, "%s: 0x%.4X", regBank->reg[k].name, regBank->reg[k].value );
                else
				    sprintf ( buffer, "%s: 0x%.8X", regBank->reg[k].name, regBank->reg[k].value );

				HTREEITEM nodeMemorySize = addOrUpdateChild ( Tree, nodeRegsItem, buffer, 0, k );
            }
        }
        
        int ioPortsCount = DeviceGetIoPortsCount(device);
        
        for (j = 0; j < ioPortsCount; j++) {
            IoPorts* ioPorts = DeviceGetIoPorts(device, j);
            
			HTREEITEM nodeRegsItem = addOrUpdateChild ( Tree, nodeDeviceName, ioPorts->name, 0, j + 20000 );

            for (UInt32 k = 0; k < ioPorts->count; k++) {
				char buffer [50];
				sprintf ( buffer, "Port %0X%2X: 0x%.2x", k, ioPorts->port[k].value );

				HTREEITEM nodeMemorySize = addOrUpdateChild ( Tree, nodeRegsItem, buffer, 0, k );
            }
        }
    }

    SnapshotDestroy(snapshot);
}

void SetDeviceInfoString(HWND hDlg)
{
    stringstream devInfo;
    
    Snapshot* snapshot = SnapshotCreate();
    if (snapshot != NULL) {
        int deviceCount = SnapshotGetDeviceCount(snapshot);

        for (int i = 0; i < deviceCount; i++) {
            Device* device = SnapshotGetDevice(snapshot, i);
            int j;

            devInfo << device->name << "\r\n";

            int memCount = DeviceGetMemoryBlockCount(device);
            for (j = 0; j < memCount; j++) {
                MemoryBlock* mem = DeviceGetMemoryBlock(device, j);
                devInfo << "   Memory: " << mem->name << " Size: 0x" << hex << mem->size << "\r\n";
            }

            int regBankCount = DeviceGetRegisterBankCount(device);
            for (j = 0; j < regBankCount; j++) {
                RegisterBank* regBank = DeviceGetRegisterBank(device, j);
                devInfo << "   Registers: " << regBank->name << "\r\n";

                for (UInt32 k = 0; k < regBank->count; k++) {
                    devInfo << "      " << regBank->reg[k].name << "  " << hex << setw(regBank->reg[k].width / 4) << setfill('0') << regBank->reg[k].value << "\r\n";
                }
            }

            int ioPortsCount = DeviceGetIoPortsCount(device);
            for (j = 0; j < ioPortsCount; j++) {
                IoPorts* ioPorts = DeviceGetIoPorts(device, j);
                devInfo << "   IO Ports: " << ioPorts->name << "\r\n";

                for (UInt32 k = 0; k < ioPorts->count; k++) {
                    devInfo << "      Port 0x" << hex << ioPorts->port[k].port << "  0x" << ioPorts->port[k].value << "\r\n";
                }
            }
        }

        SnapshotDestroy(snapshot);
    }

    SendDlgItemMessage(hDlg, ID_DEVICEINFO, WM_SETTEXT, 0, (LPARAM)devInfo.str().c_str());
}

void SetDeviceStatus(HWND hDlg) 
{
    const DvLang& L = dvL();
    string statusString = L.statusPrefix;
    switch (GetEmulatorState()) {
    case EMULATOR_STOPPED: statusString += L.statStopped; break;
    case EMULATOR_PAUSED:  statusString += L.statPaused;  break;
    case EMULATOR_RUNNING: statusString += L.statRunning; break;
    }

    SetWindowTextU(GetDlgItem(hDlg, IDC_APEARANCETHEMETEXT), statusString.c_str());
}

/* Measure a UTF-8 string with the given window's font. */
static SIZE MeasureText(HWND hWnd, const char* text)
{
    SIZE sz = { 0, 0 };
    HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
    HDC hdc = GetDC(hWnd);
    HFONT hOld = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen > 0) {
        wchar_t* wbuf = (wchar_t*)_alloca(wlen * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, wlen);
        GetTextExtentPoint32W(hdc, wbuf, wlen - 1, &sz);
    }
    if (hOld) SelectObject(hdc, hOld);
    ReleaseDC(hWnd, hdc);
    return sz;
}

static void SizeDialogItems(HWND hDlg)
{
    RECT r;
    GetClientRect(hDlg, &r);
    int height = r.bottom - r.top;
    int width  = r.right - r.left;

    UINT dpi = 96;
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        PFN_GetDpiForWindow pGetDpiForWindow =
            (PFN_GetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        if (pGetDpiForWindow) {
            UINT d = pGetDpiForWindow(hDlg);
            if (d) dpi = d;
        }
    }
    int padX = MulDiv(8,  dpi, 96);
    int padY = MulDiv(3,  dpi, 96);
    int gap  = MulDiv(4,  dpi, 96);
    int marginX = MulDiv(6, dpi, 96);
    int marginY = MulDiv(5, dpi, 96);
    
    /* Size Refresh by its actual text so the visible padding matches the
    ** font size -- the .rc-template height was way too tall with FONT 11. */
    const DvLang& L = dvL();
    HWND hRefresh = GetDlgItem(hDlg, ID_REFRESH);
    int btnH = MulDiv(22, dpi, 96);
    int refreshW = MulDiv(60, dpi, 96);
    if (hRefresh) {
        SIZE ts = MeasureText(hRefresh, L.refresh);
        if (ts.cy > 0) btnH = ts.cy + padY * 2;
        if (ts.cx > 0) refreshW = ts.cx + padX * 2;
    }

    int x = marginX;
    if (hRefresh) {
        SetWindowPos(hRefresh, NULL, x, marginY, refreshW, btnH, SWP_NOZORDER);
        x += refreshW + gap;
    }

    /* Icon-only buttons: square at the same height. */
    const int iconIds[] = { ID_RUN, ID_STOP, ID_PAUSE, ID_STEP };
    for (int i = 0; i < 4; i++) {
        HWND hBtn = GetDlgItem(hDlg, iconIds[i]);
        if (!hBtn) continue;
        SetWindowPos(hBtn, NULL, x, marginY, btnH, btnH, SWP_NOZORDER);
        x += btnH + gap;
    }

    int treeTop = marginY + btnH + MulDiv(6, dpi, 96);

    HWND hTree = GetDlgItem(hDlg, IDC_DEVICETREE);
    if (hTree) {
        SetWindowPos(hTree, NULL, 0, treeTop, width, height - treeTop, SWP_NOZORDER);
    }

    /* Status label: right-aligned (RTEXT style); size to actual text width
    ** plus generous slack so the rightmost glyph never clips. */
    HWND hLbl = GetDlgItem(hDlg, IDC_APEARANCETHEMETEXT);
    if (hLbl) {
        /* Save old rect (in dialog client coords) so we can invalidate
        ** the parent area exposed when the label shrinks -- otherwise
        ** the previous status text ghosts through. */
        RECT oldR;
        GetWindowRect(hLbl, &oldR);
        POINT pa = { oldR.left, oldR.top };
        POINT pb = { oldR.right, oldR.bottom };
        ScreenToClient(hDlg, &pa);
        ScreenToClient(hDlg, &pb);
        RECT oldClient = { pa.x, pa.y, pb.x, pb.y };

        char buf[128];
        GetWindowTextA(hLbl, buf, sizeof(buf));
        SIZE ts = MeasureText(hLbl, buf[0] ? buf : "Status: STOPPED");
        int slack = MulDiv(32, dpi, 96);
        int lblW = ts.cx + slack;
        int lblH = ts.cy + MulDiv(4, dpi, 96);
        int rightMargin = MulDiv(8, dpi, 96);
        int lblX = width - lblW - rightMargin;
        int lblY = marginY + (btnH - lblH) / 2;
        if (lblY < marginY) lblY = marginY;

        SetWindowPos(hLbl, NULL, lblX, lblY, lblW, lblH, SWP_NOZORDER);

        RECT newR = { lblX, lblY, lblX + lblW, lblY + lblH };
        RECT both;
        UnionRect(&both, &oldClient, &newR);
        InvalidateRect(hDlg, &both, TRUE);
        InvalidateRect(hLbl, NULL, TRUE);
    }
}

static INT_PTR CALLBACK dlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_REFRESH:
            //SetDeviceInfoString(hDlg);
			SetDeviceInfoString2 ( hDlg );
            return TRUE;

        case ID_RUN:
            EmulatorRun();
            return TRUE;
        case ID_STOP:
            EmulatorStop();
            return TRUE;
        case ID_PAUSE:
            EmulatorPause();
            return TRUE;
        case ID_STEP:
            EmulatorStep();
            return TRUE;
        }
        break;

    case WM_STATUS:
        SetDeviceStatus(hDlg);
		SetDeviceInfoString2 ( hDlg );
        //SetDeviceInfoString(hDlg);
        SizeDialogItems(hDlg);
        return 0;

    case WM_SIZE:
        SizeDialogItems(hDlg);
        return TRUE;

    case WM_CLOSE:
        deviceViewerHwnd = NULL;
        EndDialog(hDlg, TRUE);
		
        return TRUE;

    case WM_INITDIALOG: {
        ApplyDarkMode(hDlg);
        const DvLang& L = dvL();
        SetWindowTextU(hDlg, L.caption);
        SetDlgItemTextU(hDlg, ID_REFRESH, L.refresh);
        SetDlgItemTextU(hDlg, ID_RUN,     L.run);
        SetDlgItemTextU(hDlg, ID_STOP,    L.stop);
        SetDlgItemTextU(hDlg, ID_PAUSE,   L.pause);
        SetDlgItemTextU(hDlg, ID_STEP,    L.step);
        SetDeviceInfoString2(hDlg);
        SetDeviceStatus(hDlg);
        SizeDialogItems(hDlg);
        ShowWindow(hDlg, SW_SHOW);
        return FALSE;
    }
    }

    return FALSE;
}

void OnCreateTool() {
}

void OnDestroyTool() {
}

static void ShowError() {
    LPVOID lpMsgBuf;
    if (!FormatMessage( 
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM | 
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPTSTR) &lpMsgBuf,
        0,
        NULL ))
    {
        // Handle the error.
        return;
    }

    // Process any inserts in lpMsgBuf.
    // ...

    // Display the string.
    MessageBox( NULL, (LPCTSTR)lpMsgBuf, "Error", MB_OK | MB_ICONINFORMATION );

    // Free the buffer.
    LocalFree( lpMsgBuf );
}

void OnShowTool() {
    if (deviceViewerHwnd != NULL) {
        return;
    }

    deviceViewerHwnd = CreateDialog(GetDllHinstance(), MAKEINTRESOURCE(IDD_DEVICEVIEWER), NULL, dlgProc);
}

void OnEmulatorStart() {    
    if (deviceViewerHwnd != NULL) {
        SendMessage(deviceViewerHwnd, WM_STATUS, 0, 0);
    }
}

void OnEmulatorStop() {
    if (deviceViewerHwnd != NULL) {
        SendMessage(deviceViewerHwnd, WM_STATUS, 0, 0);
    }
}

void OnEmulatorPause() {
    if (deviceViewerHwnd != NULL) {
        SendMessage(deviceViewerHwnd, WM_STATUS, 0, 0);
    }
}

void OnEmulatorResume() {
    if (deviceViewerHwnd != NULL) {
        SendMessage(deviceViewerHwnd, WM_STATUS, 0, 0);
    }
}

const char* OnGetName() {
    return dvL().toolName;
}


void OnEmulatorReset() {
}

void OnEmulatorTrace(const char* message)
{
}

void OnEmulatorSetBreakpoint(UInt16 address)
{
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 address) {
}

void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address)
{
}

void OnSetLanguage(LanguageId languageId)
{
    g_langId = languageId;
}


