/*****************************************************************************
**
** UTF-8 versions of Win32 text APIs.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
/* With /utf-8 set, char* literals are UTF-8; convert and call the *W
** variants. */
#ifndef WIN32_TEXT_UTF8_H
#define WIN32_TEXT_UTF8_H

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <shlobj.h>

#include "Utf8Conv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 -> wide with stack fast-path + malloc overflow; the fixed-size
** MBTWC pattern truncates silently when dst is too small. */
static __inline wchar_t* Utf8ToWideAlloc(const char* utf8, wchar_t* stack, int stackCap)
{
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, NULL, 0);
    if (needed <= 0) { if (stackCap > 0) stack[0] = 0; return stack; }
    if (needed <= stackCap) {
        MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, stack, stackCap);
        return stack;
    }
    {
        wchar_t* heap = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)needed);
        if (!heap) {
            MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, stack, stackCap);
            return stack;
        }
        MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, heap, needed);
        return heap;
    }
}

static __inline void FreeWideMaybe(wchar_t* buf, const wchar_t* stack)
{
    if (buf != stack) free(buf);
}

/* GetWindowText / GetDlgItemText return UTF-8. dstCap counts bytes. */
static __inline int GetWindowTextU(HWND hwnd, char* dst, int dstCap)
{
    wchar_t buf[1024];
    int wlen = GetWindowTextW(hwnd, buf, _countof(buf));
    if (wlen <= 0) { if (dstCap > 0) dst[0] = 0; return 0; }
    return WideToUtf8(buf, dst, dstCap);
}

static __inline int GetDlgItemTextU(HWND hDlg, int id, char* dst, int dstCap)
{
    return GetWindowTextU(GetDlgItem(hDlg, id), dst, dstCap);
}

static __inline BOOL SetWindowTextU(HWND hwnd, const char* utf8)
{
    wchar_t stack[1024];
    wchar_t* w = Utf8ToWideAlloc(utf8, stack, (int)_countof(stack));
    BOOL ret = SetWindowTextW(hwnd, w);
    FreeWideMaybe(w, stack);
    return ret;
}

static __inline BOOL SetDlgItemTextU(HWND hDlg, int id, const char* utf8)
{
    return SetWindowTextU(GetDlgItem(hDlg, id), utf8);
}

static __inline LRESULT SendWmSetTextU(HWND hwnd, const char* utf8)
{
    wchar_t stack[1024];
    wchar_t* w = Utf8ToWideAlloc(utf8, stack, (int)_countof(stack));
    LRESULT ret = SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)w);
    FreeWideMaybe(w, stack);
    return ret;
}

static __inline int MessageBoxU(HWND hwnd, const char* text, const char* caption, UINT type)
{
    wchar_t wtext[2048];
    wchar_t wcap[256];
    Utf8ToWide(text,    wtext, _countof(wtext));
    Utf8ToWide(caption, wcap,  _countof(wcap));
    return MessageBoxW(hwnd, wtext, wcap, type);
}

/* Menu helpers forward NULL/MF_SEPARATOR/MF_BITMAP unchanged. */
static __inline BOOL AppendMenuU(HMENU hMenu, UINT flags, UINT_PTR id, const char* utf8)
{
    wchar_t buf[512];
    if (utf8 == NULL) return AppendMenuW(hMenu, flags, id, NULL);
    Utf8ToWide(utf8, buf, _countof(buf));
    return AppendMenuW(hMenu, flags, id, buf);
}

static __inline BOOL InsertMenuU(HMENU hMenu, UINT pos, UINT flags, UINT_PTR id, const char* utf8)
{
    wchar_t buf[512];
    if (utf8 == NULL) return InsertMenuW(hMenu, pos, flags, id, NULL);
    Utf8ToWide(utf8, buf, _countof(buf));
    return InsertMenuW(hMenu, pos, flags, id, buf);
}

static __inline BOOL ModifyMenuU(HMENU hMenu, UINT pos, UINT flags, UINT_PTR id, const char* utf8)
{
    wchar_t buf[512];
    if (utf8 == NULL) return ModifyMenuW(hMenu, pos, flags, id, NULL);
    Utf8ToWide(utf8, buf, _countof(buf));
    return ModifyMenuW(hMenu, pos, flags, id, buf);
}

/* For TextOut / GetTextExtentPoint32 the caller passes UTF-8 byte length.
** We convert to UTF-16 (count code units, not bytes) and forward to the W API. */
static __inline BOOL TextOutU(HDC hdc, int x, int y, const char* utf8, int utf8Bytes)
{
    wchar_t buf[512];
    int wlen = Utf8ToWideN(utf8, utf8Bytes, buf, _countof(buf));
    if (wlen <= 0) return FALSE;
    return TextOutW(hdc, x, y, buf, wlen);
}

static __inline BOOL GetTextExtentPoint32U(HDC hdc, const char* utf8, int utf8Bytes, LPSIZE size)
{
    wchar_t buf[512];
    int wlen = Utf8ToWideN(utf8, utf8Bytes, buf, _countof(buf));
    if (wlen <= 0) { size->cx = 0; size->cy = 0; return FALSE; }
    return GetTextExtentPoint32W(hdc, buf, wlen, size);
}

static __inline int DrawTextU(HDC hdc, const char* utf8, int utf8Bytes, LPRECT rect, UINT format)
{
    wchar_t buf[1024];
    int wlen = Utf8ToWideN(utf8, utf8Bytes, buf, _countof(buf));
    if (wlen <= 0) return 0;
    /* DrawTextW: pass -1 for NUL-terminated (when caller passed -1) or exact wlen-1 when NUL is included. */
    if (utf8Bytes < 0) {
        /* MultiByteToWideChar with srcBytes=-1 includes the NUL in wlen; pass -1 to DrawTextW. */
        return DrawTextW(hdc, buf, -1, rect, format);
    }
    return DrawTextW(hdc, buf, wlen, rect, format);
}

/* Combobox / listbox helpers. Input UTF-8 string -> CB_ADDSTRING / LB_ADDSTRING. */
static __inline LRESULT ComboAddStringU(HWND hCombo, const char* utf8)
{
    wchar_t buf[512];
    Utf8ToWide(utf8, buf, _countof(buf));
    return SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)buf);
}

static __inline LRESULT ComboInsertStringU(HWND hCombo, int index, const char* utf8)
{
    wchar_t buf[512];
    Utf8ToWide(utf8, buf, _countof(buf));
    return SendMessageW(hCombo, CB_INSERTSTRING, (WPARAM)index, (LPARAM)buf);
}

static __inline LRESULT ListBoxAddStringU(HWND hList, const char* utf8)
{
    wchar_t buf[512];
    Utf8ToWide(utf8, buf, _countof(buf));
    return SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
}

static __inline LRESULT ListBoxInsertStringU(HWND hList, int index, const char* utf8)
{
    wchar_t buf[512];
    Utf8ToWide(utf8, buf, _countof(buf));
    return SendMessageW(hList, LB_INSERTSTRING, (WPARAM)index, (LPARAM)buf);
}

/* Tab control: insert a wide TCITEMW with converted text. */
static __inline LRESULT TabInsertItemU(HWND hTab, int index, const char* utf8)
{
    wchar_t buf[256];
    TCITEMW tci = {0};
    Utf8ToWide(utf8, buf, _countof(buf));
    tci.mask = TCIF_TEXT;
    tci.pszText = buf;
    return SendMessageW(hTab, TCM_INSERTITEMW, (WPARAM)index, (LPARAM)&tci);
}

/* OPENFILENAMEA wrapper: convert UTF-8 -> UTF-16, call GetOpenFileNameW, write
** the chosen path back as UTF-8. */
static __inline BOOL GetOpenFileNameU(LPOPENFILENAMEA ofnA)
{
    OPENFILENAMEW ofnW;
    wchar_t wTitle[256], wInitDir[MAX_PATH], wDefExt[16];
    wchar_t wFile[2048];
    wchar_t wFilter[2048];
    BOOL rv;

    memset(&ofnW, 0, sizeof(ofnW));
    ofnW.lStructSize = sizeof(OPENFILENAMEW);
    ofnW.hwndOwner = ofnA->hwndOwner;
    ofnW.hInstance = ofnA->hInstance;
    ofnW.nFilterIndex = ofnA->nFilterIndex;
    ofnW.nMaxFile = (DWORD)_countof(wFile);
    ofnW.Flags = ofnA->Flags;
    ofnW.lCustData = ofnA->lCustData;
    ofnW.lpfnHook = (LPOFNHOOKPROC)ofnA->lpfnHook;
    ofnW.lpTemplateName = (LPCWSTR)ofnA->lpTemplateName;

    /* Filter is double-NUL terminated UTF-8; convert pair-by-pair. */
    if (ofnA->lpstrFilter) {
        const char* p = ofnA->lpstrFilter;
        wchar_t* w = wFilter;
        wchar_t* wEnd = wFilter + _countof(wFilter) - 2;
        while (*p && w < wEnd) {
            int n = MultiByteToWideChar(CP_UTF8, 0, p, -1, w, (int)(wEnd - w));
            if (n <= 0) break;
            w += n;       /* includes terminating NUL */
            p += strlen(p) + 1;
        }
        *w = 0;           /* second NUL closes the filter list */
        ofnW.lpstrFilter = wFilter;
    }
    if (ofnA->lpstrTitle) {
        Utf8ToWide(ofnA->lpstrTitle, wTitle, _countof(wTitle));
        ofnW.lpstrTitle = wTitle;
    }
    if (ofnA->lpstrInitialDir) {
        Utf8ToWide(ofnA->lpstrInitialDir, wInitDir, _countof(wInitDir));
        ofnW.lpstrInitialDir = wInitDir;
    }
    if (ofnA->lpstrDefExt) {
        Utf8ToWide(ofnA->lpstrDefExt, wDefExt, _countof(wDefExt));
        ofnW.lpstrDefExt = wDefExt;
    }
    if (ofnA->lpstrFile && ofnA->nMaxFile > 0) {
        /* Initial value, if any, is UTF-8; bring it across so OFN_OVERWRITEPROMPT
        ** style preselection still works. */
        Utf8ToWide(ofnA->lpstrFile, wFile, _countof(wFile));
        ofnW.lpstrFile = wFile;
    } else {
        wFile[0] = 0;
        ofnW.lpstrFile = wFile;
    }

    rv = GetOpenFileNameW(&ofnW);
    if (rv) {
        if (ofnA->lpstrFile && ofnA->nMaxFile > 0) {
            /* Return path as UTF-8 -- downstream file I/O is wrapped (fopenU /
            ** GetFileAttributesU) to convert UTF-8 -> wide before hitting the
            ** kernel. Display sites use the path directly via SetWindowTextU. */
            WideCharToMultiByte(CP_UTF8, 0, wFile, -1,
                                ofnA->lpstrFile, (int)ofnA->nMaxFile, NULL, NULL);
        }
        ofnA->nFilterIndex   = ofnW.nFilterIndex;
        ofnA->nFileOffset    = ofnW.nFileOffset;
        ofnA->nFileExtension = ofnW.nFileExtension;
    }
    return rv;
}

static __inline BOOL GetSaveFileNameU(LPOPENFILENAMEA ofnA)
{
    /* GetOpenFileName and GetSaveFileName share the OPENFILENAME contract; reuse
    ** the open path with a different terminal call. */
    OPENFILENAMEW ofnW;
    wchar_t wTitle[256], wInitDir[MAX_PATH], wDefExt[16];
    wchar_t wFile[2048];
    wchar_t wFilter[2048];
    BOOL rv;

    memset(&ofnW, 0, sizeof(ofnW));
    ofnW.lStructSize = sizeof(OPENFILENAMEW);
    ofnW.hwndOwner = ofnA->hwndOwner;
    ofnW.hInstance = ofnA->hInstance;
    ofnW.nFilterIndex = ofnA->nFilterIndex;
    ofnW.nMaxFile = (DWORD)_countof(wFile);
    ofnW.Flags = ofnA->Flags;
    ofnW.lCustData = ofnA->lCustData;
    ofnW.lpfnHook = (LPOFNHOOKPROC)ofnA->lpfnHook;
    ofnW.lpTemplateName = (LPCWSTR)ofnA->lpTemplateName;

    if (ofnA->lpstrFilter) {
        const char* p = ofnA->lpstrFilter;
        wchar_t* w = wFilter;
        wchar_t* wEnd = wFilter + _countof(wFilter) - 2;
        while (*p && w < wEnd) {
            int n = MultiByteToWideChar(CP_UTF8, 0, p, -1, w, (int)(wEnd - w));
            if (n <= 0) break;
            w += n;
            p += strlen(p) + 1;
        }
        *w = 0;
        ofnW.lpstrFilter = wFilter;
    }
    if (ofnA->lpstrTitle) {
        Utf8ToWide(ofnA->lpstrTitle, wTitle, _countof(wTitle));
        ofnW.lpstrTitle = wTitle;
    }
    if (ofnA->lpstrInitialDir) {
        Utf8ToWide(ofnA->lpstrInitialDir, wInitDir, _countof(wInitDir));
        ofnW.lpstrInitialDir = wInitDir;
    }
    if (ofnA->lpstrDefExt) {
        Utf8ToWide(ofnA->lpstrDefExt, wDefExt, _countof(wDefExt));
        ofnW.lpstrDefExt = wDefExt;
    }
    if (ofnA->lpstrFile && ofnA->nMaxFile > 0) {
        Utf8ToWide(ofnA->lpstrFile, wFile, _countof(wFile));
        ofnW.lpstrFile = wFile;
    } else {
        wFile[0] = 0;
        ofnW.lpstrFile = wFile;
    }

    rv = GetSaveFileNameW(&ofnW);
    if (rv) {
        if (ofnA->lpstrFile && ofnA->nMaxFile > 0) {
            /* Return path as UTF-8 (file I/O wrapped via fopenU / etc.). */
            WideCharToMultiByte(CP_UTF8, 0, wFile, -1,
                                ofnA->lpstrFile, (int)ofnA->nMaxFile, NULL, NULL);
        }
        ofnA->nFilterIndex   = ofnW.nFilterIndex;
        ofnA->nFileOffset    = ofnW.nFileOffset;
        ofnA->nFileExtension = ofnW.nFileExtension;
    }
    return rv;
}

/* File I/O wrappers: convert UTF-8 paths -> wchar_t and call the W variant.
** PathToWide falls back to CP_ACP for legacy ACP-encoded INI / history paths. */

static __inline FILE* fopenU(const char* path, const char* mode)
{
    wchar_t wPath[1024];
    wchar_t wMode[16];
    if (!path || !mode) return NULL;
    PathToWide(path, wPath, _countof(wPath));
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wMode, _countof(wMode));
    return _wfopen(wPath, wMode);
}

static __inline DWORD GetFileAttributesU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return GetFileAttributesW(wPath);
}

static __inline BOOL DeleteFileU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return DeleteFileW(wPath);
}

static __inline BOOL MoveFileU(const char* from, const char* to)
{
    wchar_t wFrom[1024], wTo[1024];
    PathToWide(from, wFrom, _countof(wFrom));
    PathToWide(to,   wTo,   _countof(wTo));
    return MoveFileW(wFrom, wTo);
}

static __inline BOOL CopyFileU(const char* from, const char* to, BOOL failIfExists)
{
    wchar_t wFrom[1024], wTo[1024];
    PathToWide(from, wFrom, _countof(wFrom));
    PathToWide(to,   wTo,   _countof(wTo));
    return CopyFileW(wFrom, wTo, failIfExists);
}

static __inline HANDLE CreateFileU(const char* path, DWORD access, DWORD share,
                                   LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                   DWORD flags, HANDLE templ)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return CreateFileW(wPath, access, share, sa, disp, flags, templ);
}

static __inline int unlinkU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wunlink(wPath);
}

static __inline int removeU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wremove(wPath);
}

static __inline int renameU(const char* from, const char* to)
{
    wchar_t wFrom[1024], wTo[1024];
    PathToWide(from, wFrom, _countof(wFrom));
    PathToWide(to,   wTo,   _countof(wTo));
    return _wrename(wFrom, wTo);
}

static __inline int mkdirU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wmkdir(wPath);
}

static __inline int rmdirU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wrmdir(wPath);
}

static __inline int chdirU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wchdir(wPath);
}

static __inline char* getcwdU(char* buf, int bufSize)
{
    wchar_t wbuf[1024];
    if (!_wgetcwd(wbuf, _countof(wbuf))) return NULL;
    if (WideToUtf8(wbuf, buf, bufSize) <= 0) return NULL;
    return buf;
}

static __inline int statU(const char* path, struct _stat* st)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wstat(wPath, st);
}

static __inline int statU64(const char* path, struct __stat64* st)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wstat64(wPath, st);
}

static __inline int openU(const char* path, int flags, int mode)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return _wopen(wPath, flags, mode);
}

/* WIN32_FIND_DATA: return UTF-8-encoded cFileName / cAlternateFileName so
** filename-based filtering and display work through the wrapped path. */
static __inline HANDLE FindFirstFileU(const char* pattern, WIN32_FIND_DATAA* fd)
{
    wchar_t wPattern[1024];
    WIN32_FIND_DATAW fdw;
    HANDLE h;
    PathToWide(pattern, wPattern, _countof(wPattern));
    h = FindFirstFileW(wPattern, &fdw);
    if (h != INVALID_HANDLE_VALUE && fd) {
        fd->dwFileAttributes = fdw.dwFileAttributes;
        fd->ftCreationTime   = fdw.ftCreationTime;
        fd->ftLastAccessTime = fdw.ftLastAccessTime;
        fd->ftLastWriteTime  = fdw.ftLastWriteTime;
        fd->nFileSizeHigh    = fdw.nFileSizeHigh;
        fd->nFileSizeLow     = fdw.nFileSizeLow;
        fd->dwReserved0      = fdw.dwReserved0;
        fd->dwReserved1      = fdw.dwReserved1;
        WideToUtf8(fdw.cFileName,         fd->cFileName,         sizeof(fd->cFileName));
        WideToUtf8(fdw.cAlternateFileName,fd->cAlternateFileName,sizeof(fd->cAlternateFileName));
    }
    return h;
}

static __inline BOOL FindNextFileU(HANDLE h, WIN32_FIND_DATAA* fd)
{
    WIN32_FIND_DATAW fdw;
    BOOL r = FindNextFileW(h, &fdw);
    if (r && fd) {
        fd->dwFileAttributes = fdw.dwFileAttributes;
        fd->ftCreationTime   = fdw.ftCreationTime;
        fd->ftLastAccessTime = fdw.ftLastAccessTime;
        fd->ftLastWriteTime  = fdw.ftLastWriteTime;
        fd->nFileSizeHigh    = fdw.nFileSizeHigh;
        fd->nFileSizeLow     = fdw.nFileSizeLow;
        fd->dwReserved0      = fdw.dwReserved0;
        fd->dwReserved1      = fdw.dwReserved1;
        WideToUtf8(fdw.cFileName,         fd->cFileName,         sizeof(fd->cFileName));
        WideToUtf8(fdw.cAlternateFileName,fd->cAlternateFileName,sizeof(fd->cAlternateFileName));
    }
    return r;
}

static __inline DWORD GetCurrentDirectoryU(DWORD bufSize, char* buf)
{
    wchar_t wbuf[1024];
    DWORD n = GetCurrentDirectoryW(_countof(wbuf), wbuf);
    if (n == 0) return 0;
    return (DWORD)WideToUtf8(wbuf, buf, (int)bufSize);
}

static __inline BOOL SetCurrentDirectoryU(const char* path)
{
    wchar_t wPath[1024];
    PathToWide(path, wPath, _countof(wPath));
    return SetCurrentDirectoryW(wPath);
}

static __inline UINT GetPrivateProfileStringU(const char* section, const char* key,
                                              const char* defaultValue, char* buf,
                                              UINT bufSize, const char* iniPath)
{
    wchar_t wSection[256], wKey[256], wDefault[1024], wIni[1024];
    wchar_t wOut[4096];
    UINT n;
    PathToWide(iniPath, wIni, _countof(wIni));
    Utf8ToWide(section ? section : "", wSection, _countof(wSection));
    Utf8ToWide(key ? key : "", wKey, _countof(wKey));
    Utf8ToWide(defaultValue ? defaultValue : "", wDefault, _countof(wDefault));
    n = GetPrivateProfileStringW(wSection, wKey, wDefault, wOut, _countof(wOut), wIni);
    if (buf && bufSize > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wOut, -1, buf, (int)bufSize, NULL, NULL);
    }
    return n;
}

static __inline BOOL WritePrivateProfileStringU(const char* section, const char* key,
                                                const char* value, const char* iniPath)
{
    wchar_t wSection[256], wKey[256], wValue[1024], wIni[1024];
    PathToWide(iniPath, wIni, _countof(wIni));
    Utf8ToWide(section ? section : "", wSection, _countof(wSection));
    Utf8ToWide(key ? key : "", wKey, _countof(wKey));
    if (value) {
        Utf8ToWide(value, wValue, _countof(wValue));
        return WritePrivateProfileStringW(wSection, wKey, wValue, wIni);
    }
    return WritePrivateProfileStringW(wSection, wKey, NULL, wIni);
}

#ifdef __cplusplus
}
#endif

#endif /* WIN32_TEXT_UTF8_H */
