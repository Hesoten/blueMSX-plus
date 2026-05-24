/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32Dir.cpp,v $
**
** $Revision: 1.9 $
**
** $Date: 2008-06-06 23:55:57 $
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
#ifndef __WINCRYPT_H__
#define __WINCRYPT_H__
#endif

#include <windows.h> 
#include <shlobj.h> 
#include <objbase.h> 
#include "Win32TextUtf8.h"


static bool initialized = false;
static wchar_t wDefaultDirectory[MAX_PATH];

static int CALLBACK browseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    switch (uMsg) {
    case BFFM_INITIALIZED:
#if 0
        HWND cbohWnd = CreateWindowW(L"COMBOBOX", NULL, CBS_DROPDOWNLIST|WS_VSCROLL|CBS_AUTOHSCROLL|WS_CHILD|WS_VISIBLE,
            17, 30, 286, 150, hwnd, (HMENU)1005, (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        SendMessage(cbohWnd, CB_RESETCONTENT, 0, 0);
        ComboAddStringU(cbohWnd, "MSX 3.5\" DSDD");
        ComboAddStringU(cbohWnd, "MSX2 CP/M 3.0 DSDD");
        ComboAddStringU(cbohWnd, "MSX2 CP/M 3.0 SSDD");
        ComboAddStringU(cbohWnd, "SVI-328 CP/M 2.24 DSDD");
        ComboAddStringU(cbohWnd, "SVI-328 CP/M 2.24 SSDD");
        ComboAddStringU(cbohWnd, "SVI-328 Disk Basic DSDD");
        ComboAddStringU(cbohWnd, "SVI-328 Disk Basic SSDD");
        ComboAddStringU(cbohWnd, "SVI-328 Z-CPR3 DSDD");
        ComboAddStringU(cbohWnd, "SVI-738 CP/M 2.28 SSDD");
        SendMessage(cbohWnd, CB_SETCURSEL, 0, 0);

#endif
        if (wDefaultDirectory[0]) {
            SendMessageW(hwnd, BFFM_SETSELECTION, 1, (LPARAM)wDefaultDirectory);
        }
        break;
    }
    return 0;
}


extern "C" char* openDir(HWND hwnd, char* pTitle, char* defDir) {
    static char pFileName[MAX_PATH * 4];
    LPMALLOC pMalloc; 
    BROWSEINFOW bi;
    wchar_t wTitle[256];
    wchar_t wPath[MAX_PATH];
    LPITEMIDLIST pidl; 

    Utf8ToWide(defDir ? defDir : "", wDefaultDirectory, _countof(wDefaultDirectory));

	if (!initialized) {
		initialized = true;
	}

    /* Gets the Shell's default allocator */ 
    if (SHGetMalloc(&pMalloc) != NOERROR) { 
        return NULL; 
    } 

    Utf8ToWide(pTitle ? pTitle : "", wTitle, _countof(wTitle));
    
    bi.hwndOwner      = hwnd;
    bi.pidlRoot       = NULL;
    bi.pszDisplayName = wPath;
    bi.lpszTitle      = wTitle;
    bi.ulFlags        = BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn           = browseCallbackProc;
    bi.lParam         = 0;

    if ((pidl = SHBrowseForFolderW(&bi)) != NULL) {
        if (!SHGetPathFromIDListW(pidl, wPath)) {
            pMalloc->Free(pidl);
            pMalloc->Release();
            return NULL;
        } 
        WideToUtf8(wPath, pFileName, sizeof(pFileName));
        pMalloc->Free(pidl); 
    } else {
        pMalloc->Release();
        return NULL;
    } 

    pMalloc->Release(); 
    return pFileName;
}
