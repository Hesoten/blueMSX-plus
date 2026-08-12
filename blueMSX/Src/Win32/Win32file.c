/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32file.c,v $
**
** $Revision: 1.71 $
**
** $Date: 2009-04-30 03:53:28 $
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
#include <windows.h> 
#include <tchar.h>
#include "Win32file.h"
#include <stdlib.h>
#include <stdio.h>
#include "Resource.h"
#include "MediaDb.h"
#include "RomLoader.h"
#include "ziphelper.h"
#include "Win32Common.h"
#include "Win32ScreenShot.h"
#include "Win32TextUtf8.h"
#include "Win32FileDialog.h"
#include "Language.h"
#include "DiskFormat.h"
#include "Casette.h"
#include "IsFileExtension.h"
#include "RomTypeList.h"

/* After stdio.h: pkg_fopen overrides fopen for UTF-8 paths. */
#include "PacketFileSystem.h"

#define WM_DIALOGRESIZE (WM_USER + 1500)

RomType opendialog_getromtype(int i)
{
	return romTypeListMapperAt(i);
}

char* openRomFile(HWND hwndOwner, char* pTitle, char* pFilter, char* pDir, int mustExist, 
                  char* defExt, int* filterIndex, RomType* romType)
{ 
    static char pFileName[MAX_PATH * 4];
    int detectedRomType = ROM_UNKNOWN;
    FILE* file;
    (void)mustExist; (void)filterIndex;

    pFileName[0] = 0; 
    *romType = ROM_UNKNOWN;

    if (!ShellOpenRomFileDialog(hwndOwner, pTitle, pFilter, pDir,
                                pFileName, sizeof(pFileName), &detectedRomType)) {
        return NULL; 
    }

    if (pDir != NULL) {
        GetCurrentDirectoryU(MAX_PATH - 1, pDir);
    }

    /* Append default extension when the user typed a name without one. */
    file = fopen(pFileName, "r");
    if (file != NULL) {
        fclose(file);
    }
    else if (defExt) {
        size_t fnLen = strlen(pFileName);
        size_t exLen = strlen(defExt);
        int needAppend = 1;
        if (fnLen > exLen) {
            const char* tail = pFileName + fnLen - exLen;
            size_t i;
            needAppend = 0;
            for (i = 0; i < exLen; i++) {
                if (toupper((unsigned char)tail[i]) != toupper((unsigned char)defExt[i])) {
                    needAppend = 1;
                    break;
                }
            }
        }
        if (needAppend && fnLen + exLen < sizeof(pFileName)) {
            strcat(pFileName, defExt);
        }
        /* Skip fopen("a+") probing here: it would create empty stub files
        ** and mask missing-file errors.  Loaders surface those instead. */
    }

    *romType = (RomType)detectedRomType;
    return pFileName;
}

//////////////////////////////////////////////////////////////////////////////////////

char* openStateFile(HWND hwndOwner, char* pTitle, char* pFilter, char* pDir, 
                    int newFileSize, char* defExt, int* filterIndex, int* showPreview)
{
    static char pFileName[MAX_PATH * 4];
    int idx = filterIndex ? *filterIndex : 0;
    FILE* file;
    (void)newFileSize; (void)showPreview;

    pFileName[0] = 0; 

    if (!ShellOpenFileDialog(hwndOwner, pTitle, pFilter, pDir, defExt,
                             filterIndex ? &idx : NULL,
                             pFileName, sizeof(pFileName))) {
        return NULL; 
    }
    if (filterIndex) *filterIndex = idx;
    if (pDir != NULL) GetCurrentDirectoryU(MAX_PATH - 1, pDir);

    file = fopen(pFileName, "r");
    if (file != NULL) {
        fclose(file);
    }
    else if (defExt) {
        size_t fnLen = strlen(pFileName);
        size_t exLen = strlen(defExt);
        int needAppend = 1;
        if (fnLen > exLen) {
            const char* tail = pFileName + fnLen - exLen;
            size_t i;
            needAppend = 0;
            for (i = 0; i < exLen; i++) {
                if (toupper((unsigned char)tail[i]) != toupper((unsigned char)defExt[i])) {
                    needAppend = 1;
                    break;
                }
            }
        }
        if (needAppend && fnLen + exLen < sizeof(pFileName)) {
            strcat(pFileName, defExt);
        }
        /* Skip fopen("a+") probing here: it would create empty stub files
        ** and mask missing-file errors.  Loaders surface those instead. */
    }
    return pFileName; 
} 

char* saveStateFile(HWND hwndOwner, char* pTitle, char* pFilter, int* pFilterIndex, char* pDir, int* showPreview)
{
    static char pFileName[MAX_PATH * 4];
    int idx = pFilterIndex ? *pFilterIndex : 0;
    (void)showPreview;

    pFileName[0] = 0; 

    if (!ShellSaveFileDialog(hwndOwner, pTitle, pFilter, pDir, NULL,
                             pFilterIndex ? &idx : NULL,
                             pFileName, sizeof(pFileName))) {
        return NULL; 
    }
    if (pFilterIndex) *pFilterIndex = idx;
    if (pDir != NULL) GetCurrentDirectoryU(MAX_PATH - 1, pDir);

    return pFileName; 
}

//////////////////////////////////////////////////////////////////

char* openNewHdFile(HWND hwndOwner, char* pTitle, char* pFilter, char* pDir, 
                    char* defExt, int* filterIndex)
{
    static char pFileName[MAX_PATH * 4];
    Int64 hdSize = 0;
    FILE* file;
    (void)filterIndex;

    pFileName[0] = 0; 

    if (!ShellNewHdFileDialog(hwndOwner, pTitle, pFilter, pDir, defExt,
                              pFileName, sizeof(pFileName), &hdSize)) {
        return NULL;
    }
    if (pDir != NULL) GetCurrentDirectoryU(MAX_PATH - 1, pDir);

    /* IFileSaveDialog already raises FOS_OVERWRITEPROMPT for existing files;
    ** no need for the legacy hand-rolled MessageBox confirmation here. */

    if (defExt) {
        size_t fnLen = strlen(pFileName);
        size_t exLen = strlen(defExt);
        int needAppend = 1;
        if (fnLen > exLen) {
            const char* tail = pFileName + fnLen - exLen;
            size_t i;
            needAppend = 0;
            for (i = 0; i < exLen; i++) {
                if (toupper((unsigned char)tail[i]) != toupper((unsigned char)defExt[i])) {
                    needAppend = 1;
                    break;
                }
            }
        }
        if (needAppend && fnLen + exLen < sizeof(pFileName)) {
            strcat(pFileName, defExt);
        }
    }
    file = fopen(pFileName, "wb");
    if (file == NULL) {
        MessageBoxU(hwndOwner, langErrorCreateDiskImage(), langErrorTitle(), MB_ICONERROR | MB_OK);
        return NULL;
    }
    if (hdSize > 0) {
        if (_fseeki64(file, hdSize - 1, SEEK_SET) != 0 || fputc(0, file) == EOF) {
            fclose(file);
            MessageBoxU(hwndOwner, langErrorCreateDiskImage(), langErrorTitle(), MB_ICONERROR | MB_OK);
            return NULL;
        }
    }
    fclose(file);

    return pFileName; 
} 
//////////////////////////////////////////////////////////////////

#define ONEKB 1024

/* The shared matcher, minus its non const parameter */
static int hasExtension(const char* fileName, const char* ext)
{
    return isFileExtension(fileName, (char*)ext);
}

/* The shell dialog already appends the default extension, but a name typed
** with a foreign one comes back untouched, so make sure it ends the way the
** caller needs before the file is created. */
static void appendExtension(char* fileName, size_t cap, const char* ext)
{
    if (ext == NULL || *ext == 0 || hasExtension(fileName, ext)) {
        return;
    }
    if (strlen(fileName) + strlen(ext) < cap) {
        strcat(fileName, ext);
    }
}

static const struct {
    int size;
    char* (*translation)();
} dskFileSizes[] = {
    { 720 * ONEKB, langEnumDiskMsx35Dbl9Sect },
    { 640 * ONEKB, langEnumDiskMsx35Dbl8Sect },
    { 360 * ONEKB, langEnumDiskMsx35Sgl9Sect },
    { 320 * ONEKB, langEnumDiskMsx35Sgl8Sect },
    { 338 * ONEKB, langEnumDiskSvi525Dbl },
    { 168 * ONEKB, langEnumDiskSvi525Sgl },
    { 160 * ONEKB, langEnumDiskSf3Sgl },
    { 0, NULL }
};

/* Order matches DiskFormatType enum values so combobox index == enum value.
** label is a getter so translations resolve at open time (not module init). */
static const struct {
    DiskFormatType fmt;
    char*        (*label)(void);
} dskFormatChoices[] = {
    { DiskFormatUnformatted, langEnumDiskFormatUnformatted },
    { DiskFormatMsxDos1,     NULL },
    { DiskFormatMsxDos2,     NULL },
    { DiskFormatNextor,      NULL }
};

/* Proper-noun labels needing no translation. */
static const char* const dskFormatFixedLabels[] = {
    NULL,          /* Unformatted -> use getter */
    "MSX-DOS 1",
    "MSX-DOS 2",
    "Nextor"
};

char* openNewDskFile(HWND hwndOwner, char* pTitle, char* pFilter, char* pDir, 
                    char* defExt, int* filterIndex)
{
    static char pFileName[MAX_PATH * 4];
    static int  selectedSizeIdx = 0;  /* persists across opens */
    static int  selectedFmtIdx  = 0;
    int dskItemCount;
    ShellComboItem dskItems[16];
    ShellComboItem fmtItems[8];
    char labelBufs[16][64];
    int i;
    int writeBytes;
    DiskFormatType fmt;

    (void)filterIndex;

    for (dskItemCount = 0;
         dskFileSizes[dskItemCount].size && dskItemCount < (int)(sizeof(dskItems)/sizeof(dskItems[0]));
         dskItemCount++) {
        sprintf(labelBufs[dskItemCount], "%dkB - %s",
                dskFileSizes[dskItemCount].size / ONEKB,
                dskFileSizes[dskItemCount].translation());
        dskItems[dskItemCount].label = labelBufs[dskItemCount];
        dskItems[dskItemCount].bytes = dskFileSizes[dskItemCount].size;
    }
    for (i = 0; i < (int)(sizeof(dskFormatChoices)/sizeof(dskFormatChoices[0])); i++) {
        fmtItems[i].label = dskFormatChoices[i].label ? dskFormatChoices[i].label()
                                                     : dskFormatFixedLabels[i];
        fmtItems[i].bytes = (int)dskFormatChoices[i].fmt;
    }

    pFileName[0] = 0;
    if (!ShellNewDskFileDialog(hwndOwner, pTitle, pFilter, pDir, defExt,
                               dskItems, dskItemCount, &selectedSizeIdx,
                               fmtItems, (int)(sizeof(dskFormatChoices)/sizeof(dskFormatChoices[0])),
                               &selectedFmtIdx,
                               pFileName, sizeof(pFileName))) {
        return NULL; 
    }
    writeBytes = (selectedSizeIdx >= 0 && selectedSizeIdx < dskItemCount)
                 ? dskItems[selectedSizeIdx].bytes
                 : 720 * ONEKB;
    fmt = (selectedFmtIdx >= 0 &&
           selectedFmtIdx < (int)(sizeof(dskFormatChoices)/sizeof(dskFormatChoices[0])))
          ? dskFormatChoices[selectedFmtIdx].fmt
          : DiskFormatUnformatted;

    if (pDir != NULL) GetCurrentDirectoryU(MAX_PATH - 1, pDir);

    appendExtension(pFileName, sizeof(pFileName), defExt);

    if (!diskImageCreate(pFileName, writeBytes, fmt)) {
        MessageBoxU(hwndOwner, langErrorCreateDiskImage(), langErrorTitle(), MB_ICONERROR | MB_OK);
        return NULL;
    }
    return pFileName;
}

/* Order matches the filter list built by archFilenameGetNewCas, so the file
** type the dialog reports picks both the format and the extension. */
static const struct {
    TapeFormat  format;
    const char* ext;
} casNewChoices[] = {
    { TAPE_WAV,      ".wav" },
    { TAPE_FMSXDOS,  ".cas" }
};
#define CAS_NEW_COUNT ((int)(sizeof(casNewChoices) / sizeof(casNewChoices[0])))

char* openNewCasFile(HWND hwndOwner, char* pTitle, char* pFilter, char* pDir)
{
    static char pFileName[MAX_PATH * 4];
    static int  selectedIdx = 1;   /* 1 based, persists across opens */
    int i = (selectedIdx >= 1 && selectedIdx <= CAS_NEW_COUNT) ? selectedIdx - 1 : 0;

    pFileName[0] = 0;
    /* The default extension has to match the type the dialog opens on, or a
    ** bare name comes back carrying the other format's suffix. */
    if (!ShellSaveFileDialog(hwndOwner, pTitle, pFilter, pDir, casNewChoices[i].ext,
                             &selectedIdx, pFileName, sizeof(pFileName))) {
        return NULL;
    }
    if (pDir != NULL) GetCurrentDirectoryU(MAX_PATH - 1, pDir);

    /* An extension typed by hand outranks the file type combo, which only
    ** decides what a bare name becomes. Checking first also stops a ".cas"
    ** name from growing a second ".wav" on the end. */
    for (i = 0; i < CAS_NEW_COUNT; i++) {
        if (hasExtension(pFileName, casNewChoices[i].ext)) {
            break;
        }
    }
    if (i == CAS_NEW_COUNT) {
        i = (selectedIdx >= 1 && selectedIdx <= CAS_NEW_COUNT) ? selectedIdx - 1 : 0;
        appendExtension(pFileName, sizeof(pFileName), casNewChoices[i].ext);
    }

    if (!tapeImageCreate(pFileName, casNewChoices[i].format)) {
        MessageBoxU(hwndOwner, langErrorCreateTapeImage(), langErrorTitle(), MB_ICONERROR | MB_OK);
        return NULL;
    }
    return pFileName;
}

//////////////////////////////////////////////////////////////////////////////////////

char* openFile(HWND hwndOwner, char* pTitle, char* pFilter, char* pDir, 
               int newFileSize, char* defExt, int* filterIndex)
{ 
    static char pFileName[MAX_PATH * 4];
    int idx = filterIndex ? *filterIndex : 0;
    FILE* file;
    (void)newFileSize;

    pFileName[0] = 0; 

    if (!ShellOpenFileDialog(hwndOwner, pTitle, pFilter, pDir, defExt,
                             filterIndex ? &idx : NULL,
                             pFileName, sizeof(pFileName))) {
        return NULL; 
    }
    if (filterIndex) *filterIndex = idx;
    if (pDir != NULL) GetCurrentDirectoryU(MAX_PATH - 1, pDir);

    file = fopen(pFileName, "r");
    if (file != NULL) {
        fclose(file);
    }
    else if (defExt) {
        size_t fnLen = strlen(pFileName);
        size_t exLen = strlen(defExt);
        int needAppend = 1;
        if (fnLen > exLen) {
            const char* tail = pFileName + fnLen - exLen;
            size_t i;
            needAppend = 0;
            for (i = 0; i < exLen; i++) {
                if (toupper((unsigned char)tail[i]) != toupper((unsigned char)defExt[i])) {
                    needAppend = 1;
                    break;
                }
            }
        }
        if (needAppend && fnLen + exLen < sizeof(pFileName)) {
            strcat(pFileName, defExt);
        }
        /* Skip fopen("a+") probing here: it would create empty stub files
        ** and mask missing-file errors.  Loaders surface those instead. */
    }
    return pFileName; 
}

char* saveFile(HWND hwndOwner, char* pTitle, char* pFilter, int* pFilterIndex, char* pDir, char* defExt)
{
    static char pFileName[MAX_PATH * 4];
    int idx = pFilterIndex ? *pFilterIndex : 0;

    pFileName[0] = 0; 

    if (!ShellSaveFileDialog(hwndOwner, pTitle, pFilter, pDir, defExt,
                             pFilterIndex ? &idx : NULL,
                             pFileName, sizeof(pFileName))) {
        return NULL; 
    }
    if (pFilterIndex) *pFilterIndex = idx;
    if (pDir != NULL) GetCurrentDirectoryU(MAX_PATH - 1, pDir);

    appendExtension(pFileName, sizeof(pFileName), defExt);

    return pFileName;
}


///////////////////////////////////////////////////////////////////////////

typedef struct {
    char*  title;
    char*  description;
    char** itemList;
    char*  defaultName;
    char*  returnName;
} SaveAsDlgInfo;



static BOOL_DLG_RET CALLBACK saveAsProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{

    static SaveAsDlgInfo* sdi;
    char buffer[64];
    int i;

    switch (iMsg) {
    case WM_INITDIALOG:
        sdi = (SaveAsDlgInfo*)lParam;

        SetWindowTextU(hwnd, sdi->title);
        SetWindowTextU(GetDlgItem(hwnd, IDC_MACHINENAMETEXT), sdi->description);
        SetWindowTextU(GetDlgItem(hwnd, IDOK), langDlgSave());
        SetWindowTextU(GetDlgItem(hwnd, IDCANCEL), langDlgCancel());

        for (i = 0; sdi->itemList[i] != NULL; i++) {
            ListBoxAddStringU(GetDlgItem(hwnd, IDC_MACHINELIST), sdi->itemList[i]);
            if (0 == strcmpnocase(sdi->itemList[i], sdi->defaultName)) {
                SetWindowTextU(GetDlgItem(hwnd, IDC_MACHINENAME), sdi->defaultName);
                SendDlgItemMessage(hwnd, IDC_MACHINELIST, LB_SETCURSEL, i, 0);
                EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
            }
        }
        win32CommonApplyDark(hwnd);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MACHINELIST:
            if (HIWORD(wParam) == 1 || HIWORD(wParam) == 2) {
                char buffer[64];
                int index = (int)SendMessage(GetDlgItem(hwnd, IDC_MACHINELIST), LB_GETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hwnd, IDC_MACHINELIST), LB_GETTEXT, index, (LPARAM)buffer);
                SetWindowTextU(GetDlgItem(hwnd, IDC_MACHINENAME), buffer);
                if (HIWORD(wParam) == 2) {
                    SendMessage(hwnd, WM_COMMAND, IDOK, 0);
                }
            }
            return TRUE;

        case IDC_MACHINENAME:
            GetWindowTextU(GetDlgItem(hwnd, IDC_MACHINENAME), buffer, 63);

            EnableWindow(GetDlgItem(hwnd, IDOK), strlen(buffer) != 0);      

            SendDlgItemMessage(hwnd, IDC_MACHINELIST, LB_SETCURSEL, -1, 0);

            for (i = 0; sdi->itemList[i] != NULL; i++) {
                if (0 == strcmpnocase(sdi->itemList[i], buffer)) {
                    SendDlgItemMessage(hwnd, IDC_MACHINELIST, LB_SETCURSEL, i, 0);
                }
            }
            return TRUE;
        case IDOK:
            GetWindowTextU(GetDlgItem(hwnd, IDC_MACHINENAME), sdi->returnName, 63);
            EndDialog(hwnd, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, FALSE);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hwnd, FALSE);
        return TRUE;
    }

    return FALSE;
}

char* openConfigFile(HWND parent, char* title, char* description,
                     char** itemList, char* defaultName)
{
    static char returnName[MAX_PATH];
    SaveAsDlgInfo* sdi = (SaveAsDlgInfo*)calloc(1, sizeof(SaveAsDlgInfo));
    int rv;

    sdi->title       = title;
    sdi->description = description;
    sdi->itemList    = itemList;
    sdi->defaultName = defaultName;
    sdi->returnName  = returnName;

    rv = (int)DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CONF_SAVEAS), parent, saveAsProc, (LPARAM)sdi);
    free(sdi);

    return rv ? returnName : NULL;

}
