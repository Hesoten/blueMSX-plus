/*****************************************************************************
**
** File open / save dialogs (UTF-8 paths).
** Copyright (C) 2026 Hesoten
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
/* Per-dialog add-ons via IFileDialogCustomize; no dialog templates. */
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <new>

#include "Win32FileDialog.h"
#include "../Utils/Utf8Conv.h"

extern "C" {
#include "RomLoader.h"
#include "MediaDb.h"
#include "ziphelper.h"
#include "IsFileExtension.h"
#include "Win32Common.h"
#include "Language.h"

/* Provided by Win32file.c. Returns RomType for the i-th item, or ROM_UNKNOWN. */
RomType opendialog_getromtype(int i);
}

/* ------------------------------------------------------------------------- */
/* Centering helper -- IFileDialog has no API for the host position, so we   */
/* hook the CBT activation of the dialog window and reposition once.         */
/* ------------------------------------------------------------------------- */
namespace {

static HWND  s_pendingOwner = NULL;   /* one-shot per Show() call           */
static HHOOK s_centerHook   = NULL;

/* Posted after HCBT_ACTIVATE once window positioning has settled. */
#define WM_BLUEMSX_CENTER_FILE_DIALOG (WM_APP + 0x4321)

/* Guards against re-entrant centering from our own SetWindowPos. */
static __declspec(thread) int s_inCentering = 0;

static LRESULT CALLBACK fileDialogSubclassProc(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam,
                                               UINT_PTR uIdSubclass,
                                               DWORD_PTR dwRefData)
{
    (void)dwRefData;
    if (msg == WM_BLUEMSX_CENTER_FILE_DIALOG) {
        s_inCentering = 1;
        win32CommonCenterOnOwner(hwnd);
        s_inCentering = 0;
        return 0;
    }
    /* Re-center on shell WM_WINDOWPOSCHANGED up to 10 times so the dialog
    ** tracks its final restored size, then disengage. */
    if (msg == WM_WINDOWPOSCHANGED && !s_inCentering) {
        static const wchar_t* kProp = L"blueMSXFDCounter";
        UINT_PTR count = (UINT_PTR)GetPropW(hwnd, kProp);
        if (count < 10) {
            SetPropW(hwnd, kProp, (HANDLE)(count + 1));
            s_inCentering = 1;
            win32CommonCenterOnOwner(hwnd);
            s_inCentering = 0;
        } else {
            RemovePropW(hwnd, kProp);
            RemoveWindowSubclass(hwnd, fileDialogSubclassProc, uIdSubclass);
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK fileDialogCenterHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_ACTIVATE && s_pendingOwner) {
        HWND hwnd = (HWND)wParam;
        if (GetWindow(hwnd, GW_OWNER) == s_pendingOwner) {
            /* Subclass to re-center on shell SetWindowPos after activation. */
            SetWindowSubclass(hwnd, fileDialogSubclassProc, 0xF11E, 0);
            PostMessageW(hwnd, WM_BLUEMSX_CENTER_FILE_DIALOG, 0, 0);
            s_pendingOwner = NULL;   /* one-shot */
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/* Calls IFileDialog::Show(owner), centering the dialog on the owner window
** on first activation.  Pair with Show() everywhere we currently have it. */
static HRESULT showCentered(IFileDialog* fd, HWND owner)
{
    s_pendingOwner = owner;
    s_centerHook = SetWindowsHookExW(WH_CBT, fileDialogCenterHook,
                                     NULL, GetCurrentThreadId());
    HRESULT hr = fd->Show(owner);
    if (s_centerHook) {
        UnhookWindowsHookEx(s_centerHook);
        s_centerHook = NULL;
    }
    s_pendingOwner = NULL;
    return hr;
}

} /* anonymous namespace */


/* Local HD size table scoped to the IFileDialog New-HD path. */
namespace {
struct HdSizeEntry { int bytes; const wchar_t* label; };
const HdSizeEntry kHdSizes[] = {
    {   5 * 1024 * 1024, L"5 MB"   },
    {  10 * 1024 * 1024, L"10 MB"  },
    {  20 * 1024 * 1024, L"20 MB"  },
    {  50 * 1024 * 1024, L"50 MB"  },
    { 100 * 1024 * 1024, L"100 MB" },
    { 200 * 1024 * 1024, L"200 MB" },
};
const size_t kHdSizesCount = sizeof(kHdSizes) / sizeof(kHdSizes[0]);
}

/* std::wstring / std::string adapters over Utf8Conv.h primitives. */
static std::wstring utf8ToWide(const char* s)
{
    if (!s || !*s) return std::wstring();
    int n = Utf8ToWide(s, NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n - 1, L'\0');
    Utf8ToWide(s, &w[0], n);
    return w;
}

static std::string wideToUtf8(LPCWSTR w)
{
    if (!w || !*w) return std::string();
    int n = WideToUtf8(w, NULL, 0);
    if (n <= 0) return std::string();
    std::string s(n - 1, '\0');
    WideToUtf8(w, &s[0], n);
    return s;
}

/* Convert OFN-style "Desc\0pat\0Desc\0pat\0\0" UTF-8 filter to a vector of
** COMDLG_FILTERSPEC. The wide strings live in the holder vector so the
** specs stay valid until Show returns. */
static std::vector<COMDLG_FILTERSPEC> parseFilter(const char* filter,
                                                  std::vector<std::wstring>& holder)
{
    std::vector<COMDLG_FILTERSPEC> out;
    if (!filter) return out;
    const char* p = filter;
    while (*p) {
        std::wstring desc = utf8ToWide(p);
        p += strlen(p) + 1;
        if (!*p) break;
        std::wstring pat = utf8ToWide(p);
        p += strlen(p) + 1;
        holder.push_back(std::move(desc));
        holder.push_back(std::move(pat));
    }
    /* Build spec array in a second pass; vector growth invalidates c_str(). */
    for (size_t i = 0; i + 1 < holder.size(); i += 2) {
        COMDLG_FILTERSPEC s;
        s.pszName = holder[i].c_str();
        s.pszSpec = holder[i + 1].c_str();
        out.push_back(s);
    }
    return out;
}

/* Scoped CoInitializeEx helper. */
class ScopedCoInit {
    HRESULT hr_;
public:
    ScopedCoInit() : hr_(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ScopedCoInit() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
};



/* ------------------------------------------------------------------------- */
/* Common dialog-setup helper used by both Open and Save flavours. */
/* ------------------------------------------------------------------------- */

static HRESULT applyCommonOptions(IFileDialog* fd,
                                  const char* title,
                                  shell_filter_t filter,
                                  const char* initialDir,
                                  const char* defExt,
                                  int filterIndex1Based,
                                  std::vector<std::wstring>& filterHolder,
                                  std::vector<COMDLG_FILTERSPEC>& specs)
{
    if (title && *title) {
        std::wstring wTitle = utf8ToWide(title);
        fd->SetTitle(wTitle.c_str());
    }

    if (filter && *filter) {
        specs = parseFilter(filter, filterHolder);
        if (!specs.empty()) {
            fd->SetFileTypes((UINT)specs.size(), specs.data());
            UINT idx = (filterIndex1Based > 0) ? (UINT)filterIndex1Based : 1u;
            if (idx > specs.size()) idx = 1u;
            fd->SetFileTypeIndex(idx);
        }
    }

    if (defExt && *defExt) {
        std::wstring wExt = utf8ToWide(defExt);
        fd->SetDefaultExtension(wExt.c_str());
    }

    if (initialDir && *initialDir) {
        std::wstring wDir = utf8ToWide(initialDir);
        IShellItem* psiFolder = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(wDir.c_str(), NULL,
                                                   IID_PPV_ARGS(&psiFolder)))) {
            fd->SetFolder(psiFolder);
            psiFolder->Release();
        }
    }

    return S_OK;
}

/* Fetch chosen path and sync CWD to its folder (legacy "remember last folder"
** -- IFileDialog sets FOS_NOCHANGEDIR by default). */
static BOOL fetchPath(IFileDialog* fd, char* outPath, int outPathCap, int* filterIndexOut)
{
    if (filterIndexOut) {
        UINT idx = 0;
        if (SUCCEEDED(fd->GetFileTypeIndex(&idx)) && idx > 0) {
            *filterIndexOut = (int)idx;
        }
    }

    IShellItem* psi = NULL;
    if (FAILED(fd->GetResult(&psi)) || !psi) return FALSE;
    PWSTR pszFilePath = NULL;
    HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr) && pszFilePath) {
        /* Update the process CWD to the folder of the selected file. */
        wchar_t folder[MAX_PATH * 2];
        wcsncpy_s(folder, _countof(folder), pszFilePath, _TRUNCATE);
        PathRemoveFileSpecW(folder);
        if (folder[0]) SetCurrentDirectoryW(folder);

        std::string utf8 = wideToUtf8(pszFilePath);
        if (outPath && outPathCap > 0) {
            int n = (int)utf8.size();
            if (n > outPathCap - 1) n = outPathCap - 1;
            memcpy(outPath, utf8.data(), n);
            outPath[n] = 0;
        }
        CoTaskMemFree(pszFilePath);
        ok = TRUE;
    }
    psi->Release();
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Generic open / save (no customization). */
/* ------------------------------------------------------------------------- */

static BOOL showSimple(REFCLSID clsid, HWND owner,
                       const char* title, shell_filter_t filter,
                       const char* initialDir, const char* defExt,
                       int* filterIndex,
                       char* outPath, int outPathCap)
{
    ScopedCoInit coinit;
    if (!coinit.ok()) return FALSE;

    IFileDialog* fd = NULL;
    if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fd)))) return FALSE;

    std::vector<std::wstring>      filterHolder;
    std::vector<COMDLG_FILTERSPEC> specs;
    applyCommonOptions(fd, title, filter, initialDir, defExt,
                       filterIndex ? *filterIndex : 0,
                       filterHolder, specs);

    HRESULT hr = showCentered(fd, owner);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr)) {
        ok = fetchPath(fd, outPath, outPathCap, filterIndex);
    }
    fd->Release();
    return ok;
}

extern "C" BOOL ShellOpenFileDialog(HWND owner,
                                    const char* title,
                                    shell_filter_t filter,
                                    const char* initialDir,
                                    const char* defExt,
                                    int* filterIndex,
                                    char* outPath, int outPathCap)
{
    return showSimple(CLSID_FileOpenDialog, owner,
                      title, filter, initialDir, defExt, filterIndex,
                      outPath, outPathCap);
}

extern "C" BOOL ShellSaveFileDialog(HWND owner,
                                    const char* title,
                                    shell_filter_t filter,
                                    const char* initialDir,
                                    const char* defExt,
                                    int* filterIndex,
                                    char* outPath, int outPathCap)
{
    return showSimple(CLSID_FileSaveDialog, owner,
                      title, filter, initialDir, defExt, filterIndex,
                      outPath, outPathCap);
}

extern "C" BOOL ShellSaveFileDialogEx(HWND owner,
                                      const char* title,
                                      shell_filter_t filter,
                                      const char* initialDir,
                                      const char* defExt,
                                      const char* defaultName,
                                      int* filterIndex,
                                      char* outPath, int outPathCap)
{
    /* Build the filter spec pair on stack-local wstrings; a
    ** std::vector<COMDLG_FILTERSPEC> combined with SetFileName crashes
    ** in vector _Tidy at teardown. */
    ScopedCoInit coinit;
    if (!coinit.ok()) return FALSE;

    IFileDialog* fd = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fd)))) return FALSE;

    /* Title. */
    std::wstring wTitle;
    if (title && *title) {
        wTitle = utf8ToWide(title);
        fd->SetTitle(wTitle.c_str());
    }

    /* Walk OFN-style "Desc\0pat\0\0" filter; static spec array (max 8). */
    std::wstring  specDesc[8];
    std::wstring  specPat[8];
    COMDLG_FILTERSPEC specs[8];
    UINT nSpecs = 0;
    if (filter && *filter) {
        const char* p = filter;
        while (*p && nSpecs < 8) {
            specDesc[nSpecs] = utf8ToWide(p);
            p += strlen(p) + 1;
            if (!*p) break;
            specPat[nSpecs] = utf8ToWide(p);
            p += strlen(p) + 1;
            specs[nSpecs].pszName = specDesc[nSpecs].c_str();
            specs[nSpecs].pszSpec = specPat[nSpecs].c_str();
            ++nSpecs;
        }
        if (nSpecs > 0) {
            fd->SetFileTypes(nSpecs, specs);
            UINT idx = (filterIndex && *filterIndex > 0) ? (UINT)*filterIndex : 1u;
            if (idx > nSpecs) idx = 1u;
            fd->SetFileTypeIndex(idx);
        }
    }

    /* Default extension. */
    std::wstring wExt;
    if (defExt && *defExt) {
        wExt = utf8ToWide(defExt);
        fd->SetDefaultExtension(wExt.c_str());
    }

    /* Initial folder. */
    std::wstring wDir;
    if (initialDir && *initialDir) {
        wDir = utf8ToWide(initialDir);
        IShellItem* psiFolder = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(wDir.c_str(), NULL,
                                                   IID_PPV_ARGS(&psiFolder)))) {
            fd->SetFolder(psiFolder);
            psiFolder->Release();
        }
    }

    /* Default filename pre-fill (wName outlives the dialog). */
    std::wstring wName;
    if (defaultName && *defaultName) {
        wName = utf8ToWide(defaultName);
        fd->SetFileName(wName.c_str());
    }

    HRESULT hr = showCentered(fd, owner);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr)) {
        ok = fetchPath(fd, outPath, outPathCap, filterIndex);
    }
    fd->Release();
    return ok;
}

/* ------------------------------------------------------------------------- */
/* ROM open dialog: customised with a ROM-type combobox and selection-driven */
/* mediaDb auto-detect.                                                      */
/* ------------------------------------------------------------------------- */

static const DWORD kRomTypeComboId = 1001;

/* Decide a ROM type for the file at fileName: handle .zip by peeking inside,
** call mediaDbLookupRom on the buffer. Returns ROM_UNKNOWN on failure. */
static int detectRomTypeForFile(const char* fileName)
{
    if (!fileName || !*fileName) return ROM_UNKNOWN;

    UInt8* buf = NULL;
    int size = 0;

    if (isFileExtension(fileName, ".zip")) {
        /* Zip entries can hide the ROM under any of these extensions. */
        static const char* exts[] = { ".rom", ".ri", ".mx1", ".mx2",
                                       ".sms", ".col", ".sg",  ".sc" };
        for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
            int count = 0;
            char* list = zipGetFileList(fileName, exts[i], &count);
            if (list && count == 1) {
                buf = romLoad(fileName, list, &size);
                free(list);
                if (buf) break;
            } else if (list) {
                free(list);
            }
        }
    } else {
        buf = romLoad(fileName, NULL, &size);
    }

    if (!buf) return ROM_UNKNOWN;
    /* SHA1 lookup first; fall back to mediaDbGuessRom (size + content
    ** heuristics, matches MegaromCartridge.c) for patched / hacked ROMs. */
    MediaType* mt = mediaDbLookupRom(buf, size);
    if (!mt) mt = mediaDbGuessRom(buf, size);
    int rt = mt ? (int)mediaDbGetRomType(mt) : (int)ROM_UNKNOWN;
    free(buf);
    return rt;
}

/* Map a RomType value to the ROM-combobox item index (the order produced by
** opendialog_getromtype). */
static int romTypeToComboIndex(int romType)
{
    int i = 0;
    while (1) {
        RomType rt = opendialog_getromtype(i);
        if ((int)rt == romType) return i;
        if (rt == ROM_UNKNOWN) return i; /* hit sentinel before match */
        i++;
    }
}

/* ROM dialog events: mediaDb auto-detect on selection change. */
class RomDialogEvents : public IFileDialogEvents {
    LONG  m_ref;
    DWORD m_comboId;
public:
    RomDialogEvents(DWORD comboId) : m_ref(1), m_comboId(comboId) {}
    virtual ~RomDialogEvents() {}

    IFACEMETHODIMP_(ULONG) AddRef()  { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppv = static_cast<IFileDialogEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP OnFileOk(IFileDialog*) { return S_OK; }
    IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) { return S_OK; }
    IFACEMETHODIMP OnFolderChange(IFileDialog*) { return S_OK; }
    IFACEMETHODIMP OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE*) { return S_OK; }
    IFACEMETHODIMP OnTypeChange(IFileDialog*) { return S_OK; }
    IFACEMETHODIMP OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE*) { return S_OK; }

    IFACEMETHODIMP OnSelectionChange(IFileDialog* fd) {
        IShellItem* psi = NULL;
        if (FAILED(fd->GetCurrentSelection(&psi)) || !psi) return S_OK;
        PWSTR wPath = NULL;
        HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &wPath);
        if (SUCCEEDED(hr) && wPath) {
            std::string utf8 = wideToUtf8(wPath);
            int rt = detectRomTypeForFile(utf8.c_str());
            IFileDialogCustomize* fdc = NULL;
            if (SUCCEEDED(fd->QueryInterface(IID_PPV_ARGS(&fdc)))) {
                /* Always update -- on UNKNOWN, prior detected type would
                ** otherwise persist and mis-select mapper. */
                int idx = romTypeToComboIndex(rt);
                fdc->SetSelectedControlItem(m_comboId, (DWORD)idx);
                fdc->SetControlState(m_comboId, CDCS_VISIBLE | CDCS_ENABLED);
                fdc->Release();
            }
            CoTaskMemFree(wPath);
        }
        psi->Release();
        return S_OK;
    }
};

extern "C" BOOL ShellOpenRomFileDialog(HWND owner,
                                       const char* title,
                                       shell_filter_t filter,
                                       const char* initialDir,
                                       char* outPath, int outPathCap,
                                       int* outRomType)
{
    if (outRomType) *outRomType = ROM_UNKNOWN;
    if (outPath && outPathCap > 0) outPath[0] = 0;

    ScopedCoInit coinit;
    if (!coinit.ok()) return FALSE;

    IFileOpenDialog* fd = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fd)))) return FALSE;

    std::vector<std::wstring>      filterHolder;
    std::vector<COMDLG_FILTERSPEC> specs;
    applyCommonOptions(fd, title, filter, initialDir, NULL, 0, filterHolder, specs);

    /* Populate the ROM-type combobox via IFileDialogCustomize. */
    IFileDialogCustomize* fdc = NULL;
    if (SUCCEEDED(fd->QueryInterface(IID_PPV_ARGS(&fdc)))) {
        fdc->StartVisualGroup(0, L"ROM type");
        fdc->AddComboBox(kRomTypeComboId);
        for (int i = 0; ; i++) {
            RomType rt = opendialog_getromtype(i);
            std::wstring label = utf8ToWide(romTypeToString(rt));
            fdc->AddControlItem(kRomTypeComboId, (DWORD)i, label.c_str());
            if (rt == ROM_UNKNOWN) break;
        }
        fdc->SetControlState(kRomTypeComboId, CDCS_VISIBLE);
        fdc->EndVisualGroup();
        fdc->Release();
    }

    /* Drive auto-detect via OnSelectionChange. */
    DWORD cookie = 0;
    RomDialogEvents* events = new (std::nothrow) RomDialogEvents(kRomTypeComboId);
    if (events) {
        fd->Advise(events, &cookie);
    }

    HRESULT hr = showCentered(fd, owner);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr)) {
        ok = fetchPath(fd, outPath, outPathCap, NULL);
        if (ok && outRomType) {
            IFileDialogCustomize* fdc2 = NULL;
            if (SUCCEEDED(fd->QueryInterface(IID_PPV_ARGS(&fdc2)))) {
                DWORD idx = 0;
                if (SUCCEEDED(fdc2->GetSelectedControlItem(kRomTypeComboId, &idx))) {
                    *outRomType = (int)opendialog_getromtype((int)idx);
                }
                fdc2->Release();
            }
        }
    }

    if (events) {
        fd->Unadvise(cookie);
        events->Release();
    }
    fd->Release();
    return ok;
}

/* ------------------------------------------------------------------------- */
/* HD new file dialog: customised with a disk-size combobox.                 */
/* ------------------------------------------------------------------------- */

static const DWORD kHdSizeComboId = 1101;

extern "C" BOOL ShellNewHdFileDialog(HWND owner,
                                     const char* title,
                                     shell_filter_t filter,
                                     const char* initialDir,
                                     const char* defExt,
                                     char* outPath, int outPathCap,
                                     int* outHdSizeBytes)
{
    if (outHdSizeBytes) *outHdSizeBytes = 0;
    if (outPath && outPathCap > 0) outPath[0] = 0;

    ScopedCoInit coinit;
    if (!coinit.ok()) return FALSE;

    IFileSaveDialog* fd = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fd)))) return FALSE;

    std::vector<std::wstring>      filterHolder;
    std::vector<COMDLG_FILTERSPEC> specs;
    applyCommonOptions(fd, title, filter, initialDir, defExt, 0, filterHolder, specs);

    std::wstring wSizeLabel = utf8ToWide(langEnumDiskSize());

    IFileDialogCustomize* fdc = NULL;
    if (SUCCEEDED(fd->QueryInterface(IID_PPV_ARGS(&fdc)))) {
        fdc->StartVisualGroup(0, wSizeLabel.c_str());
        fdc->AddComboBox(kHdSizeComboId);
        for (size_t i = 0; i < kHdSizesCount; i++) {
            fdc->AddControlItem(kHdSizeComboId, (DWORD)i, kHdSizes[i].label);
        }
        fdc->SetSelectedControlItem(kHdSizeComboId, 0);
        fdc->EndVisualGroup();
        fdc->Release();
    }

    HRESULT hr = showCentered(fd, owner);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr)) {
        ok = fetchPath(fd, outPath, outPathCap, NULL);
        if (ok && outHdSizeBytes) {
            IFileDialogCustomize* fdc2 = NULL;
            if (SUCCEEDED(fd->QueryInterface(IID_PPV_ARGS(&fdc2)))) {
                DWORD idx = 0;
                if (SUCCEEDED(fdc2->GetSelectedControlItem(kHdSizeComboId, &idx)) &&
                    idx < kHdSizesCount) {
                    *outHdSizeBytes = kHdSizes[idx].bytes;
                }
                fdc2->Release();
            }
        }
    }

    fd->Release();
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Folder picker.  IFileOpenDialog + FOS_PICKFOLDERS replaces                */
/* SHBrowseForFolder, which has no DPI / dark-mode handling and no address   */
/* bar / drag-and-drop / breadcrumb.                                         */
/* ------------------------------------------------------------------------- */

extern "C" BOOL ShellPickFolderDialog(HWND owner,
                                      const char* title,
                                      const char* initialDir,
                                      char* outPath, int outPathCap)
{
    if (outPath && outPathCap > 0) outPath[0] = 0;

    ScopedCoInit coinit;
    if (!coinit.ok()) return FALSE;

    IFileOpenDialog* fd = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fd)))) return FALSE;

    DWORD flags = 0;
    fd->GetOptions(&flags);
    fd->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    if (title && *title) {
        std::wstring wTitle = utf8ToWide(title);
        fd->SetTitle(wTitle.c_str());
    }
    if (initialDir && *initialDir) {
        std::wstring wDir = utf8ToWide(initialDir);
        IShellItem* psiFolder = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(wDir.c_str(), NULL,
                                                   IID_PPV_ARGS(&psiFolder)))) {
            fd->SetFolder(psiFolder);
            psiFolder->Release();
        }
    }

    HRESULT hr = showCentered(fd, owner);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr)) {
        ok = fetchPath(fd, outPath, outPathCap, NULL);
    }
    fd->Release();
    return ok;
}

/* ------------------------------------------------------------------------- */
/* FD/DSK new-image dialog: customised with a preset-size combobox supplied  */
/* by the caller (Win32file.c owns the size table + label translation).     */
/* ------------------------------------------------------------------------- */

static const DWORD kDskSizeComboId = 1102;

extern "C" BOOL ShellNewDskFileDialog(HWND owner,
                                      const char* title,
                                      shell_filter_t filter,
                                      const char* initialDir,
                                      const char* defExt,
                                      const ShellComboItem* items, int itemCount,
                                      int* selectedIndex,
                                      char* outPath, int outPathCap)
{
    if (outPath && outPathCap > 0) outPath[0] = 0;

    ScopedCoInit coinit;
    if (!coinit.ok()) return FALSE;

    IFileSaveDialog* fd = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&fd)))) return FALSE;

    std::vector<std::wstring>      filterHolder;
    std::vector<COMDLG_FILTERSPEC> specs;
    applyCommonOptions(fd, title, filter, initialDir, defExt, 0, filterHolder, specs);

    std::wstring wSizeLabel = utf8ToWide(langEnumDiskSize());

    /* Wide labels need to outlive AddControlItem; matches the HD case style. */
    std::vector<std::wstring> labelHolder;
    labelHolder.reserve(itemCount);

    IFileDialogCustomize* fdc = NULL;
    if (items && itemCount > 0 &&
        SUCCEEDED(fd->QueryInterface(IID_PPV_ARGS(&fdc)))) {
        fdc->StartVisualGroup(0, wSizeLabel.c_str());
        fdc->AddComboBox(kDskSizeComboId);
        for (int i = 0; i < itemCount; i++) {
            labelHolder.push_back(utf8ToWide(items[i].label ? items[i].label : ""));
            fdc->AddControlItem(kDskSizeComboId, (DWORD)i, labelHolder.back().c_str());
        }
        DWORD initial = 0;
        if (selectedIndex && *selectedIndex >= 0 && *selectedIndex < itemCount) {
            initial = (DWORD)*selectedIndex;
        }
        fdc->SetSelectedControlItem(kDskSizeComboId, initial);
        fdc->EndVisualGroup();
        fdc->Release();
    }

    HRESULT hr = showCentered(fd, owner);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr)) {
        ok = fetchPath(fd, outPath, outPathCap, NULL);
        if (ok && selectedIndex) {
            IFileDialogCustomize* fdc2 = NULL;
            if (SUCCEEDED(fd->QueryInterface(IID_PPV_ARGS(&fdc2)))) {
                DWORD idx = 0;
                if (SUCCEEDED(fdc2->GetSelectedControlItem(kDskSizeComboId, &idx)) &&
                    (int)idx < itemCount) {
                    *selectedIndex = (int)idx;
                }
                fdc2->Release();
            }
        }
    }

    fd->Release();
    return ok;
}
