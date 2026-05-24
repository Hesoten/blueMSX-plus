/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32properties.c,v $
**
** $Revision: 1.92 $
**
** $Date: 2008-05-19 19:56:59 $
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
#include <math.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "AppConfig.h"

#if _MSC_VER
#define snprintf _snprintf
#endif

#ifdef __GNUC__ // FIXME: Include is not available in gnu c
static HRESULT StringCchCopy(LPTSTR d, size_t l, LPCTSTR s) { strncpy(d, s, l); d[l-1]=0; return S_OK; }
static HRESULT StringCchLength(LPCTSTR s, size_t m, size_t *l) { *l = strlen(s); if (*l > m) *l = m; return S_OK; }
#define StringCchPrintf(s, l, f, a, b) sprintf(s, f, a, b)
#else
#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#endif

#include "Win32Properties.h"
#include "Win32TextUtf8.h"
#include "Win32DirectX.h"
#include "ThemeLoader.h"
#include "Win32keyboard.h"
#include "resource.h"
#include "Language.h"
#include "Machine.h"
#include "Board.h"
#include "Win32Midi.h"
#include "Win32Cdrom.h"
#include "Win32File.h"

/* From Win32D3D12.cpp; no header pulled in here to keep the C/C++
** boundary minimal. */
int  D3D12HdrMode(void);


#define WM_UPDATEPROPERTIES  (WM_USER + 0)
#define WM_CANCELUPDATEPROPERTIES (WM_USER + 1)
#define WM_VIDEO_DRIVER_CHANGED (WM_USER + 2)

static HWND hDlgDirectDraw = NULL;
static HWND hDlgDirect3d  = NULL;
static HWND hDlgGdi = NULL;
static HWND hDlgVideoSoftware = NULL;
static HWND hDlgVideo = NULL;
static HWND hDlgSound = NULL;
static int propModified = 0;
static Mixer* theMixer;
static Video* theVideo;
static int centered = 0;
extern void emulatorRestartSound();
extern void updateEmuWindow();

static const int C_iCropMax = 64;

static int openLogFile(HWND hwndOwner, char* fileName)
{
    OPENFILENAME ofn; 
    static char pFileName[MAX_PATH];
    char  curDir[MAX_PATH];
    BOOL rv;

    pFileName[0] = 0; 

    ofn.lStructSize = sizeof(OPENFILENAME); 
    ofn.hwndOwner = hwndOwner; 
    ofn.hInstance = GetModuleHandle(NULL);
    ofn.lpstrFilter = "*.*\0\0"; 
    ofn.lpstrCustomFilter = NULL; 
    ofn.nMaxCustFilter = 0;
    ofn.nFilterIndex = 0; 
    ofn.lpstrFile = pFileName; 
    ofn.nMaxFile = 1024; 
    ofn.lpstrFileTitle = NULL; 
    ofn.nMaxFileTitle = 0; 
    ofn.lpstrInitialDir = NULL; 
    ofn.lpstrTitle = langPropPortsOpenLogFile(); 
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT; 
    ofn.nFileOffset = 0; 
    ofn.nFileExtension = 0; 
    ofn.lpstrDefExt = NULL; 
    ofn.lCustData = 0; 
    ofn.lpfnHook = NULL; 
    ofn.lpTemplateName = NULL; 

    GetCurrentDirectoryU(MAX_PATH, curDir);

    rv = GetSaveFileName(&ofn); 

    SetCurrentDirectoryU(curDir);

    if (rv) {
        strcpy(fileName, pFileName);
    }

    return rv;
}


static char pVideoMonData[4][64];
static char* pVideoMon[] = {
    pVideoMonData[0],
    pVideoMonData[1],
    pVideoMonData[2],
    pVideoMonData[3],
    NULL
};

static char pVideoTypeData[2][64];
static char* pVideoVideoType[] = {
    pVideoTypeData[0], 
    pVideoTypeData[1],
    NULL
};

static char pVideoEmuData[9][64];
static char* pVideoPalEmu[] = {
    pVideoEmuData[0],
    pVideoEmuData[1],
    pVideoEmuData[2],
    pVideoEmuData[3],
    pVideoEmuData[4],
    pVideoEmuData[5],
    pVideoEmuData[6],
    pVideoEmuData[7],
    NULL
};

static char pVideoSizeData[6][64];
static char* pVideoMonSize[] = {
    pVideoSizeData[0],
    pVideoSizeData[1],
    pVideoSizeData[2],
    NULL
};

static char pVideoDriverData[4][64];
static char* pVideoDriver[] = {
    pVideoDriverData[0],
    pVideoDriverData[1],
    pVideoDriverData[2],
    pVideoDriverData[3],
    NULL
};

static char pVideoFrameSkipData[6][64];
static char* pVideoFrameSkip[] = {
    pVideoFrameSkipData[0],
    pVideoFrameSkipData[1],
    pVideoFrameSkipData[2],
    pVideoFrameSkipData[3],
    pVideoFrameSkipData[4],
    pVideoFrameSkipData[5],
    NULL
};

static char pSoundDriverData[3][64];
static char* pSoundDriver[] = {
    pSoundDriverData[0],
    pSoundDriverData[1],
    pSoundDriverData[2],
    NULL
};

static char pEmuSyncData[5][64];
static char* pEmuSync[] = {
    pEmuSyncData[0],
    pEmuSyncData[1],
    pEmuSyncData[2],
    pEmuSyncData[3],
    pEmuSyncData[4],
    NULL
};
static char* pEmuGdiSync[] = {
    pEmuSyncData[0],
    pEmuSyncData[1],
    NULL
};

static int soundBufSizes[] = { 10, 25, 50, 75, 100, 150, 200, 250, 300, 350 };

static char* pSoundBufferSize[] = {
    "10 ms",
    "25 ms",
    "50 ms",
    "75 ms",
    "100 ms",
    "150 ms",
    "200 ms",
    "250 ms",
    "300 ms",
    "350 ms",
    NULL
};

static void setButtonCheck(HWND hDlg, int id, int check, int enable) {
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

static int getButtonCheck(HWND hDlg, int id) {
    HWND hwnd = GetDlgItem(hDlg, id);

    return BST_CHECKED == SendMessage(hwnd, BM_GETCHECK, 0, 0) ? 1 : 0;
}

static void initDropList(HWND hDlg, int id, char** pList, int index) {
    while (*pList != NULL && **pList != 0) {
        ComboAddStringU(GetDlgItem(hDlg, id), *pList);
        pList++;
    }

    SendDlgItemMessage(hDlg, id, CB_SETCURSEL, index, 0);
}

static int getDropListIndex(HWND hDlg, int id, char** pList) {
    int index = 0;
    char s[64];

    GetDlgItemTextU(hDlg, id, s, 63);
    
    while (*pList != NULL) {
        if (0 == strcmp(s, *pList)) {
            return index;
        }
        index++;
        pList++;
    }

    return -1;
}

static char* strEmuSpeed(int logFrequency) {
    UInt32 frequency = (UInt32)(3579545 * pow(2.0, (logFrequency - 50) / 15.0515));
    static char buffer[32];

    sprintf(buffer, "%d.%03dMHz (%d%%)", frequency / 1000000, (frequency / 1000) % 1000, frequency * 10 / 357954);
    return buffer;
}

static BOOL_DLG_RET CALLBACK emulationDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;
    static int curSpeed;
    static char machineName[64];

    switch (iMsg) {
    case WM_INITDIALOG:    
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }

        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;
       
        SetDlgItemTextU(hDlg, IDC_SNDCHIPEMUGROUPBOX, langPropSndChipEmuGB());

        SetDlgItemTextU(hDlg, IDC_OVERSAMPLETEXT1, langPropSndOversampleText());
        SetDlgItemTextU(hDlg, IDC_OVERSAMPLETEXT2, langPropSndOversampleText());
        SetDlgItemTextU(hDlg, IDC_OVERSAMPLETEXT3, langPropSndOversampleText());
        SetWindowTextU(GetDlgItem(hDlg, IDC_ENABLEMSXMUSIC), langPropSndMsxMusic());
        SetWindowTextU(GetDlgItem(hDlg, IDC_ENABLEMSXAUDIO), langPropSndMsxAudio());
        SetWindowTextU(GetDlgItem(hDlg, IDC_ENABLEMOONSOUND), langPropSndMoonsound());

        SetDlgItemTextU(hDlg, IDC_EMUGENERALGROUPBOX, langPropEmuGeneralGB());
        SetDlgItemTextU(hDlg, IDC_EMUFAMILYTEXT, langPropEmuFamilyText());
        SetDlgItemTextU(hDlg, IDC_VDPFREQTEXT, langPropVideoFreqText());
        SetDlgItemTextU(hDlg, IDC_EMUSPEEDTEXT, langPropEmuSpeedText());
        SetDlgItemTextU(hDlg, IDC_EMUSPEEDGROUPBOX, langPropEmuSpeedGB());
        SetDlgItemTextU(hDlg, IDC_EMUFRONTSWITCHGROUPBOX, langPropEmuFrontSwitchGB());
        
        SetWindowTextU(GetDlgItem(hDlg, IDC_EMUFDCTIMING),   langPropEmuFdcTiming());
        SetWindowTextU(GetDlgItem(hDlg, IDC_NOSPRITELIMITS), langPropEmuNoSpriteLimits());
        SetWindowTextU(GetDlgItem(hDlg, IDC_ENABLEMSXKEYBOARDQUIRK), langPropEnableMsxKeyboardQuirk());
        SetWindowTextU(GetDlgItem(hDlg, IDC_EMUFRONTSWITCH), langPropEmuFrontSwitch());
        SetWindowTextU(GetDlgItem(hDlg, IDC_EMUPAUSESWITCH), langPropEmuPauseSwitch());
        SetWindowTextU(GetDlgItem(hDlg, IDC_EMUAUDIOSWITCH), langPropEmuAudioSwitch());
        SetWindowTextU(GetDlgItem(hDlg, IDC_EMUREVERSEPLAY), langPropEmuReversePlay());
        
        setButtonCheck(hDlg, IDC_ENABLEMSXMUSIC, pProperties->sound.chip.enableYM2413, 1);
        setButtonCheck(hDlg, IDC_ENABLEMSXAUDIO, pProperties->sound.chip.enableY8950, 1);
        setButtonCheck(hDlg, IDC_ENABLEMOONSOUND, pProperties->sound.chip.enableMoonsound, 1);

        setButtonCheck(hDlg, IDC_EMUFDCTIMING,   !pProperties->emulation.enableFdcTiming, 1);
        setButtonCheck(hDlg, IDC_NOSPRITELIMITS, pProperties->emulation.noSpriteLimits, 1);
        setButtonCheck(hDlg, IDC_ENABLEMSXKEYBOARDQUIRK, pProperties->keyboard.enableKeyboardQuirk, 1);
        setButtonCheck(hDlg, IDC_EMUFRONTSWITCH, pProperties->emulation.frontSwitch, 1);
        setButtonCheck(hDlg, IDC_EMUPAUSESWITCH, pProperties->emulation.pauseSwitch, 1);
        setButtonCheck(hDlg, IDC_EMUAUDIOSWITCH, pProperties->emulation.audioSwitch, 1);
        setButtonCheck(hDlg, IDC_EMUREVERSEPLAY, pProperties->emulation.reverseEnable, 1);

        curSpeed = pProperties->emulation.speed;

        ComboAddStringU(GetDlgItem(hDlg, IDC_VDPFREQ), langPropVideoFreqAuto());
        ComboAddStringU(GetDlgItem(hDlg, IDC_VDPFREQ), "50 Hz");
        ComboAddStringU(GetDlgItem(hDlg, IDC_VDPFREQ), "60 Hz");

        SendDlgItemMessage(hDlg, IDC_VDPFREQ, CB_SETCURSEL, pProperties->emulation.vdpSyncMode, 0);

        if (CB_ERRSPACE == SendMessage(GetDlgItem(hDlg, IDC_EMUFAMILY), CB_INITSTORAGE, (WPARAM)128, (LPARAM)64))
            MessageBoxU(NULL, "Error allocating machine names", "blueMSX Error", MB_OK |  MB_ICONERROR);

        machineName[0] = 0;

        {
            int index = 0;
            ArrayList *machineList;
            ArrayListIterator *iterator;

			machineList = arrayListCreate();
			machineFillAvailable(machineList, 1);

            iterator = arrayListCreateIterator(machineList);
            while (arrayListCanIterate(iterator)) {
                char *machineInList = (char *)arrayListIterate(iterator);
                char buffer[128];

                snprintf(buffer, sizeof(buffer) - 1, "%s", machineInList);

                ComboAddStringU(GetDlgItem(hDlg, IDC_EMUFAMILY), buffer);
                if (index == 0 || 0 == strcmp(machineInList, pProperties->emulation.machineName)) {
                    SendDlgItemMessage(hDlg, IDC_EMUFAMILY, CB_SETCURSEL, index, 0);
                    strcpy(machineName, machineInList);
                }
                index++;
            }
            arrayListDestroyIterator(iterator);

            arrayListDestroy(machineList);
        }

        SetDlgItemTextU(hDlg, IDC_EMUSPEEDCUR, strEmuSpeed(curSpeed));

        SendMessage(GetDlgItem(hDlg, IDC_EMUSPEED), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 100));
        SendMessage(GetDlgItem(hDlg, IDC_EMUSPEED), TBM_SETPOS,   1, (LPARAM)curSpeed);

        return FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_EMUFAMILY) {
            char buffer[64];
            int idx = SendMessage(GetDlgItem(hDlg, IDC_EMUFAMILY), CB_GETCURSEL, 0, 0);
            int rv = SendMessage(GetDlgItem(hDlg, IDC_EMUFAMILY), CB_GETLBTEXT, idx, (LPARAM)buffer);

            if (rv != CB_ERR) {
                if (strcmp(machineName, buffer)) {
                    strcpy(machineName, buffer);
                }
            }
            return TRUE;
        }
        return TRUE;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        if (wParam == IDC_EMUSPEED) {
            curSpeed = SendMessage(GetDlgItem(hDlg, IDC_EMUSPEED), TBM_GETPOS, 0, 0);
            SetDlgItemTextU(hDlg, IDC_EMUSPEEDCUR, strEmuSpeed(curSpeed));
            return TRUE;
        }

        if ((((NMHDR FAR *)lParam)->code) != PSN_APPLY) {
            return FALSE;
        }

        {
            int index = 0;
            char buffer[64];
            ArrayList *machineList;
			ArrayListIterator *iterator;

			machineList = arrayListCreate();
            machineFillAvailable(machineList, 1);

            pProperties->sound.chip.enableYM2413 = getButtonCheck(hDlg, IDC_ENABLEMSXMUSIC);
            pProperties->sound.chip.enableY8950 = getButtonCheck(hDlg, IDC_ENABLEMSXAUDIO);
            pProperties->sound.chip.enableMoonsound = getButtonCheck(hDlg, IDC_ENABLEMOONSOUND);
            index = SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMSXMUSIC, CB_GETCURSEL, 0, 0);
            pProperties->sound.chip.ym2413Oversampling = 1 << index;
            index = SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMSXAUDIO, CB_GETCURSEL, 0, 0);
            pProperties->sound.chip.y8950Oversampling = 1 << index;
            index = SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMOONSOUND, CB_GETCURSEL, 0, 0);
            pProperties->sound.chip.moonsoundOversampling = 1 << index;

            index = 0;
            pProperties->emulation.enableFdcTiming = !getButtonCheck(hDlg, IDC_EMUFDCTIMING);
            pProperties->emulation.noSpriteLimits = getButtonCheck(hDlg, IDC_NOSPRITELIMITS);
            pProperties->keyboard.enableKeyboardQuirk = getButtonCheck(hDlg, IDC_ENABLEMSXKEYBOARDQUIRK);
            pProperties->emulation.frontSwitch = getButtonCheck(hDlg, IDC_EMUFRONTSWITCH);
            pProperties->emulation.pauseSwitch = getButtonCheck(hDlg, IDC_EMUPAUSESWITCH);
            pProperties->emulation.audioSwitch = getButtonCheck(hDlg, IDC_EMUAUDIOSWITCH);
            pProperties->emulation.reverseEnable = getButtonCheck(hDlg, IDC_EMUREVERSEPLAY);

            pProperties->emulation.vdpSyncMode = SendMessage(GetDlgItem(hDlg, IDC_VDPFREQ), CB_GETCURSEL, 0, 0);

            GetDlgItemTextU(hDlg, IDC_EMUFAMILY, buffer, 63);
            
            iterator = arrayListCreateIterator(machineList);
            while (arrayListCanIterate(iterator)) {
                char *machineInList = (char *)arrayListIterate(iterator);
                if (0 == strcmp(buffer, machineInList)) {
                    strcpy(pProperties->emulation.machineName, buffer);
                    break;
                }
            }
            arrayListDestroyIterator(iterator);

            arrayListDestroy(machineList);
        }

        pProperties->emulation.speed        = curSpeed;

        propModified = 1;
        
        return TRUE;
    }

    return FALSE;
}

static BOOL_DLG_RET CALLBACK filesDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;
    int i;

    switch (iMsg) {
    case WM_INITDIALOG:  
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }
        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;
  
#ifndef NO_FILE_HISTORY
        if (appConfigGetInt("filehistory", 1) != 0) {
            SetDlgItemTextU(hDlg, IDC_SETINGSFILEHISTORYGOUPBOX, langPropSetFileHistoryGB());
            SetDlgItemTextU(hDlg, IDC_SETINGSHISTORYSIZETEXT, langPropSetFileHistorySize());
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSHISTORYCLEAR), langPropSetFileHistoryClear());

            {
                char buffer[32];
                sprintf(buffer, "%d", pProperties->filehistory.count);
                SetWindowTextU(GetDlgItem(hDlg, IDC_SETINGSHISTORYSIZE), buffer);
            }
        }
#endif
        for (i = 0; opendialog_getromtype(i) != ROM_UNKNOWN; i++) {
            ComboAddStringU(GetDlgItem(hDlg, IDC_SETTINGSROMTYPE), romTypeToString(opendialog_getromtype(i)));
            if (pProperties->cartridge.defaultType == opendialog_getromtype(i)) {
                SendDlgItemMessage(hDlg, IDC_SETTINGSROMTYPE, CB_SETCURSEL, i, 0);
            }
        }

        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSROMTYPEGB), langPropOpenRomGB());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSROMTYPETEXT), langPropDefaultRomType());

        ComboAddStringU(GetDlgItem(hDlg, IDC_SETTINGSROMTYPE), langPropGuessRomType());
        if (pProperties->cartridge.defaultType == opendialog_getromtype(i)) {
            SendDlgItemMessage(hDlg, IDC_SETTINGSROMTYPE, CB_SETCURSEL, i, 0);
        }

        {
            char text[64];

            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSDEFSLOTSGB), langPropSettDefSlotGB());
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSSLOTS), langPropSettDefSlots());

            sprintf(text, "%s 1", langPropSettDefSlot());
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSSLOT1), text);
            sprintf(text, "%s 2", langPropSettDefSlot());
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSSLOT2), text);
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSDRIVES), langPropSettDefDrives());
            sprintf(text, "%s A", langPropSettDefDrive());
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSDRIVEA), text);
            sprintf(text, "%s B", langPropSettDefDrive());
            SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSDRIVEB), text);
        }

        setButtonCheck(hDlg, IDC_SETTINGSSLOT1, pProperties->cartridge.quickStartDrive == 0, 1);
        setButtonCheck(hDlg, IDC_SETTINGSSLOT2, pProperties->cartridge.quickStartDrive == 1, 1);
        setButtonCheck(hDlg, IDC_SETTINGSDRIVEA, pProperties->diskdrive.quickStartDrive == 0, 1);
        setButtonCheck(hDlg, IDC_SETTINGSDRIVEB, pProperties->diskdrive.quickStartDrive == 1, 1);

        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SETTINGSHISTORYCLEAR:
#ifndef NO_FILE_HISTORY
            if (appConfigGetInt("filehistory", 1) != 0) {
                int rv = MessageBoxU(NULL, langPropClearFileHistory(), langWarningTitle(), MB_ICONWARNING | MB_OKCANCEL);
                if (rv == IDOK) {
                    int i;

                    for (i = 0; i < MAX_HISTORY; i++) {
                        pProperties->filehistory.cartridge[0][i][0] = 0;
                        pProperties->filehistory.cartridge[1][i][0] = 0;
                        pProperties->filehistory.diskdrive[0][i][0] = 0;
                        pProperties->filehistory.diskdrive[1][i][0] = 0;
                        pProperties->filehistory.cassette[0][i][0] = 0;
                    }

                    pProperties->filehistory.quicksave[0] = 0;
                    pProperties->filehistory.videocap[0] = 0;

                    EnableWindow(GetDlgItem(hDlg, IDC_SETTINGSHISTORYCLEAR), FALSE);
                }
            }
#endif
            break;

        case IDC_SETTINGSSLOT1:
            setButtonCheck(hDlg, IDC_SETTINGSSLOT1, 1, 1);
            setButtonCheck(hDlg, IDC_SETTINGSSLOT2, 0, 1);
            break;

        case IDC_SETTINGSSLOT2:
            setButtonCheck(hDlg, IDC_SETTINGSSLOT1, 0, 1);
            setButtonCheck(hDlg, IDC_SETTINGSSLOT2, 1, 1);
            break;

        case IDC_SETTINGSDRIVEA:
            setButtonCheck(hDlg, IDC_SETTINGSDRIVEA, 1, 1);
            setButtonCheck(hDlg, IDC_SETTINGSDRIVEB, 0, 1);
            break;

        case IDC_SETTINGSDRIVEB:
            setButtonCheck(hDlg, IDC_SETTINGSDRIVEA, 0, 1);
            setButtonCheck(hDlg, IDC_SETTINGSDRIVEB, 1, 1);
            break;
        }
        return TRUE;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        if ((((NMHDR FAR *)lParam)->code) != PSN_APPLY) {
            return FALSE;
        }
        
        i = SendMessage(GetDlgItem(hDlg, IDC_SETTINGSROMTYPE), CB_GETCURSEL, 0, 0);
        if (i != CB_ERR) {
            pProperties->cartridge.defaultType = opendialog_getromtype(i);
        }

#ifndef NO_FILE_HISTORY
        if (appConfigGetInt("filehistory", 1) != 0) {
            char buffer[64];

            GetDlgItemTextU(hDlg, IDC_SETINGSHISTORYSIZE, buffer, 63);

            if (isdigit(*buffer)) {
                int count = atoi(buffer);
                if (count < 0)  count = 0;
                if (count > 30) count = 30;

                pProperties->filehistory.count = count;
            }
        }
#endif
        pProperties->cartridge.quickStartDrive = getButtonCheck(hDlg, IDC_SETTINGSSLOT2) ? 1 : 0;
        pProperties->diskdrive.quickStartDrive = getButtonCheck(hDlg, IDC_SETTINGSDRIVEB) ? 1 : 0;

        propModified = 1;
        
        return TRUE;
    }

    return FALSE;
}

extern void archUpdateWindow();

static BOOL_DLG_RET CALLBACK settingsDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;
    static char oldTheme[128];

    switch (iMsg) {
    case WM_INITDIALOG:
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }
        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;
        strcpy(oldTheme, pProperties->settings.themeName);

        SetWindowTextU(GetDlgItem(hDlg, IDC_APEARANCETHEMEGB), langPropThemeGB());
        SetWindowTextU(GetDlgItem(hDlg, IDC_APEARANCETHEMETEXT), langPropTheme());

        {
            ThemeCollection** themeNames = themeGetAvailable();
            int index = 0;
            while (*themeNames != NULL) {
                ComboAddStringU(GetDlgItem(hDlg, IDC_APEARANCETHEME), (*themeNames)->name);

                if (index == 0 || 0 == strcmp((*themeNames)->name, pProperties->settings.themeName)) {
                    SendDlgItemMessage(hDlg, IDC_APEARANCETHEME, CB_SETCURSEL, index, 0);
                }
                themeNames++;
                index++;
            }
        }

        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSWINDOWSENV), langPropWindowsEnvGB());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSSCREENSAVER), langPropScreenSaver());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSFILETYPES), langPropFileTypes());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSDISABLEWINKEYS), langPropDisableWinKeys());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSPRIORITYBOOST), langPropPriorityBoost());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSSCREENSHOTPNG), langPropScreenshotPng());
        SetWindowTextU(GetDlgItem(hDlg, IDC_SETTINGSEJECTMEDIAONEXIT), langPropEjectMediaOnExit());

        setButtonCheck(hDlg, IDC_SETTINGSFILETYPES, pProperties->emulation.registerFileTypes, 1);
        setButtonCheck(hDlg, IDC_SETTINGSDISABLEWINKEYS, pProperties->emulation.disableWinKeys, 1);
        setButtonCheck(hDlg, IDC_SETTINGSPRIORITYBOOST, pProperties->emulation.priorityBoost, 1);
        setButtonCheck(hDlg, IDC_SETTINGSSCREENSAVER, pProperties->settings.disableScreensaver, 1);
        setButtonCheck(hDlg, IDC_SETTINGSSCREENSHOTPNG, pProperties->settings.usePngScreenshots, 1);
        setButtonCheck(hDlg, IDC_SETTINGSEJECTMEDIAONEXIT, pProperties->emulation.ejectMediaOnExit, 1);
    
        EnableWindow(GetDlgItem(hDlg, IDC_SETTINGSFILETYPES), !pProperties->settings.portable);

        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_APEARANCETHEME:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                char buffer[128];
                int idx = SendMessage(GetDlgItem(hDlg, IDC_APEARANCETHEME), CB_GETCURSEL, 0, 0);
                int rv = SendMessage(GetDlgItem(hDlg, IDC_APEARANCETHEME), CB_GETLBTEXT, idx, (LPARAM)buffer);

                if (rv != CB_ERR) {
                    strcpy(pProperties->settings.themeName, buffer);
                    archUpdateWindow();
                    SetFocus(GetParent(hDlg));
                }
            }
            break;
        }
        return 0;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        if ((((NMHDR FAR *)lParam)->code) == PSN_QUERYCANCEL) {
            strcpy(pProperties->settings.themeName, oldTheme);
            archUpdateWindow();
            return FALSE;
        }
        if ((((NMHDR FAR *)lParam)->code) == PSN_APPLY) {
            return FALSE;
        }

        pProperties->emulation.registerFileTypes = getButtonCheck(hDlg, IDC_SETTINGSFILETYPES);
        pProperties->emulation.disableWinKeys    = getButtonCheck(hDlg, IDC_SETTINGSDISABLEWINKEYS);
        pProperties->settings.disableScreensaver = getButtonCheck(hDlg, IDC_SETTINGSSCREENSAVER);
        pProperties->emulation.priorityBoost     = getButtonCheck(hDlg, IDC_SETTINGSPRIORITYBOOST);
        pProperties->settings.usePngScreenshots  = getButtonCheck(hDlg, IDC_SETTINGSSCREENSHOTPNG);
        pProperties->emulation.ejectMediaOnExit  = getButtonCheck(hDlg, IDC_SETTINGSEJECTMEDIAONEXIT);
        propModified = 1;
        
        return TRUE;
    }

    return FALSE;
}

static void updateFullscreenResList(HWND hDlg) {
    int count = DirectDrawGetAvailableDisplayModeCount();
    DxDisplayMode* curDdm = DirectDrawGetDisplayMode();
    int i;

    while (CB_ERR != SendDlgItemMessage(hDlg, IDC_PERFFULLSCREEN, CB_DELETESTRING, 0, 0));

    for (i = 0; i < count; i++) {
        char text[32];
        DxDisplayMode* ddm = DirectDrawGetAvailableDisplayMode(i);
        sprintf(text, "%d x %d - %d bit", ddm->width, ddm->height, ddm->bitCount);
        ComboAddStringU(GetDlgItem(hDlg, IDC_PERFFULLSCREEN), text);
        if (ddm->width == curDdm->width && ddm->height == curDdm->height && ddm->bitCount == curDdm->bitCount) {
            SendDlgItemMessage(hDlg, IDC_PERFFULLSCREEN, CB_SETCURSEL, i, 0);
        }
    }
}

static void getFullscreenResList(HWND hDlg, int* width, int* height, int* bitCount) 
{
    DxDisplayMode* ddm = NULL;
    int index = SendDlgItemMessage(hDlg, IDC_PERFFULLSCREEN, CB_GETCURSEL, 0, 0);

    if (index >= 0) {
        ddm = DirectDrawGetAvailableDisplayMode(index);
    }
    if (ddm == NULL) {
        ddm = DirectDrawGetDisplayMode();
    }
    *width    = ddm->width;
    *height   = ddm->height;
    *bitCount = ddm->bitCount;
}

static Properties* pCurrentProperties;

static BOOL_DLG_RET CALLBACK directDraWProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;

    switch (iMsg) {
    case WM_INITDIALOG:
        pProperties = pCurrentProperties;

        /* Init language specific dialog items */
        SetDlgItemTextU(hDlg, IDC_PERFFRAMESKIPTEXT, langPropPerfFrameSkipText());
        SetDlgItemTextU(hDlg, IDC_PERFSETTINGSGROUPBOX, langPropSettings());
        SetDlgItemTextU(hDlg, IDC_PERFSYNCMODETEXT, langPropPerfSyncModeText());
        SetDlgItemTextU(hDlg, IDC_PERFFULLSCREENTEXT, langPropFullscreenResText());
        
        SetDlgItemTextU(hDlg, IDC_D3D_CROPPINGGROUPBOX, langpropD3DCroppingGB());
        SetDlgItemTextU(hDlg, IDC_MONHORIZSTRETCH, langPropMonHorizStretch());
        SetDlgItemTextU(hDlg, IDC_MONVERTSTRETCH, langPropMonVertStretch());

        initDropList(hDlg, IDC_FRAMESKIP, pVideoFrameSkip, pProperties->video.frameSkip);
        initDropList(hDlg, IDC_EMUSYNC, pEmuSync, pProperties->emulation.syncMethodDirectX);
        
        setButtonCheck(hDlg, IDC_MONHORIZSTRETCH, pProperties->video.horizontalStretch, 1);
        setButtonCheck(hDlg, IDC_MONVERTSTRETCH, pProperties->video.verticalStretch, 1);

        updateFullscreenResList(hDlg);
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MONHORIZSTRETCH:
            pProperties->video.horizontalStretch = getButtonCheck(hDlg, IDC_MONHORIZSTRETCH);
            updateEmuWindow();
            break;

        case IDC_MONVERTSTRETCH:
            pProperties->video.verticalStretch   = getButtonCheck(hDlg, IDC_MONVERTSTRETCH);
            updateEmuWindow();
            break;
        }
        return TRUE;

    case WM_UPDATEPROPERTIES:
        getFullscreenResList(hDlg, 
                             &pProperties->video.fullscreen.width,
                             &pProperties->video.fullscreen.height,
                             &pProperties->video.fullscreen.bitDepth);
                                
        pProperties->video.frameSkip        = getDropListIndex(hDlg, IDC_FRAMESKIP, pVideoFrameSkip);
        pProperties->emulation.syncMethod   = getDropListIndex(hDlg, IDC_EMUSYNC, pEmuSync);
        pProperties->emulation.syncMethodDirectX   = getDropListIndex(hDlg, IDC_EMUSYNC, pEmuSync);

        DirectDrawSetDisplayMode(pProperties->video.fullscreen.width,
                                pProperties->video.fullscreen.height,
                                pProperties->video.fullscreen.bitDepth);

        return TRUE;
    }

    return FALSE;
}

static BOOL_DLG_RET CALLBACK gdiProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;

    switch (iMsg) {
    case WM_INITDIALOG:
        pProperties = pCurrentProperties;

        /* Init language specific dialog items */
        SetDlgItemTextU(hDlg, IDC_PERFFRAMESKIPTEXT, langPropPerfFrameSkipText());
        SetDlgItemTextU(hDlg, IDC_PERFSETTINGSGROUPBOX, langPropSettings());
        SetDlgItemTextU(hDlg, IDC_PERFSYNCMODETEXT, langPropPerfSyncModeText());
        
        initDropList(hDlg, IDC_FRAMESKIP, pVideoFrameSkip, pProperties->video.frameSkip);
        initDropList(hDlg, IDC_EMUSYNC, pEmuGdiSync, pProperties->emulation.syncMethodGdi);

        return FALSE;

    case WM_UPDATEPROPERTIES:
        pProperties->video.frameSkip        = getDropListIndex(hDlg, IDC_FRAMESKIP, pVideoFrameSkip);
        pProperties->emulation.syncMethodGdi   = getDropListIndex(hDlg, IDC_EMUSYNC, pEmuSync);
        pProperties->emulation.syncMethod      = getDropListIndex(hDlg, IDC_EMUSYNC, pEmuSync);

        return TRUE;
    }

    return FALSE;
}

static void D3DUpdateItems(HWND hDlg, Properties* pProperties)
{
    BOOL b = (pProperties->video.d3d.cropType == P_D3D_CROP_SIZE_CUSTOM) ? TRUE : FALSE;

    SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_LEFT), WM_ENABLE, b, 0); 
    SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_RIGHT), WM_ENABLE, b, 0); 
    SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_TOP), WM_ENABLE, b, 0); 
    SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_BOTTOM), WM_ENABLE, b, 0); 

    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_LEFTTEXT), b);
    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_RIGHTTEXT), b);
    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_TOPTEXT), b);
    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_BOTTOMTEXT), b);

    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_LEFTVALUETEXT), b);
    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_RIGHTVALUETEXT), b);
    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_TOPVALUETEXT), b);
    EnableWindow(GetDlgItem(hDlg, IDC_D3D_CROPPING_BOTTOMVALUETEXT), b);
}

static BOOL_DLG_RET CALLBACK direct3dProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;

    switch (iMsg) {
    case WM_INITDIALOG:
        pProperties = pCurrentProperties;

        /* Init language specific dialog items */
        SetDlgItemTextU(hDlg, IDC_PERFFRAMESKIPTEXT, langPropPerfFrameSkipText());
        SetDlgItemTextU(hDlg, IDC_PERFSETTINGSGROUPBOX, langPropSettings());
        SetDlgItemTextU(hDlg, IDC_PERFSYNCMODETEXT, langPropPerfSyncModeText());
        SetDlgItemTextU(hDlg, IDC_D3D_EXTENDBORDERCOLOR, langPropD3DExtendBorderColorText());
        SetDlgItemTextU(hDlg, IDC_D3D_FORCEHIGHRES, langPropD3DForceHighResText());
        
		setButtonCheck(hDlg, IDC_D3D_EXTENDBORDERCOLOR, pProperties->video.d3d.extendBorderColor, 1);
		setButtonCheck(hDlg, IDC_D3D_FORCEHIGHRES, pProperties->video.d3d.forceHighRes, 1);

        initDropList(hDlg, IDC_FRAMESKIP, pVideoFrameSkip, pProperties->video.frameSkip);
        initDropList(hDlg, IDC_EMUSYNC, pEmuSync, pProperties->emulation.syncMethodD3D);


        SetDlgItemTextU(hDlg, IDC_D3D_CROPPINGGROUPBOX, langpropD3DCroppingGB());

        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_ASPECTRATIO), langEnumD3DARAuto());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_ASPECTRATIO), langEnumD3DARStretch());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_ASPECTRATIO), langEnumD3DARPAL());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_ASPECTRATIO), langEnumD3DARNTSC());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_ASPECTRATIO), langEnumD3DAR11());
        SendDlgItemMessage(hDlg, IDC_D3D_ASPECTRATIO, CB_SETCURSEL, pProperties->video.d3d.aspectRatioType, 0);
        SetDlgItemTextU(hDlg, IDC_D3D_ASPECTRATIOTEXT, langPropD3DAspectRatioText());

        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_CROPPING_TYPE), langEnumD3DCropNone());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_CROPPING_TYPE), langEnumD3DCropMSX1());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_CROPPING_TYPE), langEnumD3DCropMSX1Plus8());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_CROPPING_TYPE), langEnumD3DCropMSX2());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_CROPPING_TYPE), langEnumD3DCropMSX2Plus8());
        ComboAddStringU(GetDlgItem(hDlg, IDC_D3D_CROPPING_TYPE), langEnumD3DCropCustom());
        SendDlgItemMessage(hDlg, IDC_D3D_CROPPING_TYPE, CB_SETCURSEL, pProperties->video.d3d.cropType, 0);
        SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_TYPETEXT, langpropD3DCroppingTypeText());

        SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_LEFTTEXT, langpropD3DCroppingLeftText());
        SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_RIGHTTEXT, langpropD3DCroppingRightText());
        SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_TOPTEXT, langpropD3DCroppingTopText());
        SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_BOTTOMTEXT, langpropD3DCroppingBottomText());

		SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_LEFT), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, C_iCropMax));
        SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_RIGHT), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, C_iCropMax));
        SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_TOP), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, C_iCropMax));
        SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_BOTTOM), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, C_iCropMax));

        SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_LEFT), TBM_SETPOS, 1,  pProperties->video.d3d.cropLeft);
        SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_RIGHT), TBM_SETPOS, 1, pProperties->video.d3d.cropRight);
        SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_TOP), TBM_SETPOS, 1, pProperties->video.d3d.cropTop);
        SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_BOTTOM), TBM_SETPOS, 1, pProperties->video.d3d.cropBottom);

        D3DUpdateItems(hDlg, pProperties);

        /* HDR enable persists but only takes effect on next D3D12_Init;
        ** paper-white slider is live (shader reads CB every frame). */
        SetDlgItemTextU(hDlg, IDC_HDRENABLE, langPropMonHdrEnable());
        SetDlgItemTextU(hDlg, IDC_HDRPAPERWHITELABEL, langPropMonHdrPaperWhite());
        SetDlgItemTextU(hDlg, IDC_HDRMODESTATICTEXT, langPropMonHdrSystemMode());
        setButtonCheck(hDlg, IDC_HDRENABLE, pProperties->video.hdrEnable, 1);
        {
            int pwn = pProperties->video.hdrPaperWhiteNits;
            if (pwn < 80)  pwn = 80;
            if (pwn > 400) pwn = 400;
            pProperties->video.hdrPaperWhiteNits = pwn;
            SendMessage(GetDlgItem(hDlg, IDC_HDRPAPERWHITESLIDE), TBM_SETRANGE, 0, (LPARAM)MAKELONG(80, 400));
            SendMessage(GetDlgItem(hDlg, IDC_HDRPAPERWHITESLIDE), TBM_SETPOS,   1, (LPARAM)pwn);
            {
                char buf[16]; sprintf(buf, "%d", pwn);
                SetDlgItemTextU(hDlg, IDC_HDRPAPERWHITEVALUE, buf);
            }
            EnableWindow(GetDlgItem(hDlg, IDC_HDRPAPERWHITESLIDE),
                         pProperties->video.hdrEnable);
            EnableWindow(GetDlgItem(hDlg, IDC_HDRPAPERWHITEVALUE),
                         pProperties->video.hdrEnable);
            EnableWindow(GetDlgItem(hDlg, IDC_HDRPAPERWHITELABEL),
                         pProperties->video.hdrEnable);
        }
        /* Shows the swap-chain colour space decided at D3D12_Init, not
        ** the requested setting; enable toggles need a restart anyway. */
        {
            int mode = D3D12HdrMode();
            const char* tag = (mode == 2) ? "[HDR PQ]"
                            : (mode == 1) ? "[HDR scRGB]"
                            : "[SDR]";
            SendMessage(GetDlgItem(hDlg, IDC_HDRMODELABEL), WM_SETTEXT, 0, (LPARAM)tag);
        }

        return FALSE;

    case WM_NOTIFY:
        {
            char acBuffer[32];
            int  cropMoved = 0;

            if (wParam == IDC_HDRPAPERWHITESLIDE) {
                int pwn = (int)SendMessage(GetDlgItem(hDlg, IDC_HDRPAPERWHITESLIDE), TBM_GETPOS, 0, 0);
                if (pwn < 80)  pwn = 80;
                if (pwn > 400) pwn = 400;
                pProperties->video.hdrPaperWhiteNits = pwn;
                sprintf(acBuffer, "%d", pwn);
                SetDlgItemTextU(hDlg, IDC_HDRPAPERWHITEVALUE, acBuffer);
                updateEmuWindow();
            }
            if (wParam == IDC_D3D_CROPPING_LEFT) {
                pProperties->video.d3d.cropLeft = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_LEFT), TBM_GETPOS, 0, 0);
                sprintf(acBuffer, "%d", pProperties->video.d3d.cropLeft);
                SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_LEFTVALUETEXT, acBuffer);
                cropMoved = 1;
            }
            if (wParam == IDC_D3D_CROPPING_RIGHT) {
                pProperties->video.d3d.cropRight = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_RIGHT), TBM_GETPOS, 0, 0);
                sprintf(acBuffer, "%d", pProperties->video.d3d.cropRight);
                SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_RIGHTVALUETEXT, acBuffer);
                cropMoved = 1;
            }
            if (wParam == IDC_D3D_CROPPING_TOP) {
                pProperties->video.d3d.cropTop = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_TOP), TBM_GETPOS, 0, 0);
                sprintf(acBuffer, "%d", pProperties->video.d3d.cropTop);
                SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_TOPVALUETEXT, acBuffer);
                cropMoved = 1;
            }
            if (wParam == IDC_D3D_CROPPING_BOTTOM) {
                pProperties->video.d3d.cropBottom = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_BOTTOM), TBM_GETPOS, 0, 0);
                sprintf(acBuffer, "%d", pProperties->video.d3d.cropBottom);
                SetDlgItemTextU(hDlg, IDC_D3D_CROPPING_BOTTOMVALUETEXT, acBuffer);
                cropMoved = 1;
            }
            /* Slider live-preview: D3D12 reads crop from pProperties
            ** every frame, so a redraw nudge is enough. */
            if (cropMoved) updateEmuWindow();
        }
        return TRUE;

    case WM_COMMAND:
        pProperties->video.d3d.extendBorderColor = getButtonCheck(hDlg, IDC_D3D_EXTENDBORDERCOLOR);
        pProperties->video.d3d.forceHighRes = getButtonCheck(hDlg, IDC_D3D_FORCEHIGHRES);
        /* Capture HDR enable on every WM_COMMAND so Apply/OK persists it;
        ** on user toggle that differs from live mode, prompt for restart. */
        {
            static int s_lastHdrEnableSeen = -1;
            int newHdr = getButtonCheck(hDlg, IDC_HDRENABLE);
            pProperties->video.hdrEnable = newHdr;
            if (LOWORD(wParam) == IDC_HDRENABLE) {
                int liveMode = D3D12HdrMode();           /* 0=SDR 1/2=HDR */
                int liveIsHdr = (liveMode != 0);
                if ((newHdr != 0) != liveIsHdr && newHdr != s_lastHdrEnableSeen) {
                    s_lastHdrEnableSeen = newHdr;
                    MessageBoxU(hDlg,
                        langPropMonHdrRestartHint(),
                        "blueMSX",
                        MB_OK | MB_ICONINFORMATION);
                }
            }
        }
        /* Re-grey the paper-white slider/value when HDR is toggled. */
        EnableWindow(GetDlgItem(hDlg, IDC_HDRPAPERWHITESLIDE), pProperties->video.hdrEnable);
        EnableWindow(GetDlgItem(hDlg, IDC_HDRPAPERWHITEVALUE), pProperties->video.hdrEnable);
        EnableWindow(GetDlgItem(hDlg, IDC_HDRPAPERWHITELABEL), pProperties->video.hdrEnable);

        pProperties->video.d3d.aspectRatioType = SendMessage(GetDlgItem(hDlg, IDC_D3D_ASPECTRATIO), CB_GETCURSEL, 0, 0);
        pProperties->video.d3d.cropType = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_TYPE), CB_GETCURSEL, 0, 0);

        pProperties->video.d3d.cropBottom = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_BOTTOM), TBM_GETPOS, 0, 0);
        pProperties->video.d3d.cropTop = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_TOP), TBM_GETPOS, 0, 0);
        pProperties->video.d3d.cropRight = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_RIGHT), TBM_GETPOS, 0, 0);
        pProperties->video.d3d.cropLeft = SendMessage(GetDlgItem(hDlg, IDC_D3D_CROPPING_LEFT), TBM_GETPOS, 0, 0);

        D3DUpdateItems(hDlg, pProperties);
        /* Combobox / checkbox live-preview: nudge the renderer after each
        ** WM_COMMAND so aspect ratio / crop type / extend-border /
        ** force-high-res reflect instantly, not only on Apply / OK. */
        updateEmuWindow();
        return TRUE;

    case WM_UPDATEPROPERTIES:
        pProperties->video.frameSkip        = getDropListIndex(hDlg, IDC_FRAMESKIP, pVideoFrameSkip);
        pProperties->emulation.syncMethod   = getDropListIndex(hDlg, IDC_EMUSYNC, pEmuSync);
        pProperties->emulation.syncMethodD3D   = getDropListIndex(hDlg, IDC_EMUSYNC, pEmuSync);

        return TRUE;
    }

    return FALSE;
}

static BOOL_DLG_RET CALLBACK performanceDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;

    switch (iMsg) {
    case WM_INITDIALOG:
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }
        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;
        pCurrentProperties = pProperties;

        {
            /* Align sub-panel groupbox with parent "Video Driver" groupbox via DLUs. */
            RECT subRect = {12, 47, 0, 0};
            MapDialogRect(hDlg, &subRect);
            hDlgDirectDraw = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PERF_DIRECTDRAW), hDlg, directDraWProc);
            SetWindowPos(hDlgDirectDraw, NULL, subRect.left, subRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            hDlgGdi = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PERF_GDI), hDlg, gdiProc);
            SetWindowPos(hDlgGdi,        NULL, subRect.left, subRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            hDlgDirect3d = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PERF_DIRECT3D), hDlg, direct3dProc);
            SetWindowPos(hDlgDirect3d,   NULL, subRect.left, subRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        /* Init language specific dialog items */
        SetDlgItemTextU(hDlg, IDC_PERFVIDEODRVGROUPBOX, langPropPerfVideoDrvGB());
        SetDlgItemTextU(hDlg, IDC_PERFDISPDRVTEXT, langPropPerfVideoDispDrvText());
        
        initDropList(hDlg, IDC_VIDEODRV, pVideoDriver, pProperties->video.driver);
        
        ShowWindow(hDlgDirectDraw,  pProperties->video.driver < 2 ? SW_NORMAL : SW_HIDE);
        ShowWindow(hDlgGdi,         pProperties->video.driver == 2 ? SW_NORMAL : SW_HIDE);
        ShowWindow(hDlgDirect3d,    pProperties->video.driver == P_VIDEO_DRVDIRECTX_D3D12 ? SW_NORMAL : SW_HIDE);

        return FALSE;


    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_VIDEODRV:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int index = getDropListIndex(hDlg, IDC_VIDEODRV, pVideoDriver);
                ShowWindow(hDlgDirectDraw, index < 2 ? SW_NORMAL : SW_HIDE);
                ShowWindow(hDlgGdi,        index == 2 ? SW_NORMAL : SW_HIDE);
                ShowWindow(hDlgDirect3d,   index == P_VIDEO_DRVDIRECTX_D3D12 ? SW_NORMAL : SW_HIDE);
                SendMessage(hDlgVideo, WM_VIDEO_DRIVER_CHANGED, index, 0);
                return TRUE;
            }
            break;
        }
        return FALSE;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        if ((((NMHDR FAR *)lParam)->code) != PSN_APPLY) {
            return FALSE;
        }
        
        pProperties->video.driver           = getDropListIndex(hDlg, IDC_VIDEODRV, pVideoDriver);

        switch (pProperties->video.driver) {
        case 0:
        case 1:
            SendMessage(hDlgDirectDraw,  WM_UPDATEPROPERTIES, 0, 0);
            break;
        case 2:
            SendMessage(hDlgGdi,  WM_UPDATEPROPERTIES, 0, 0);
            break;
        case 3:
            SendMessage(hDlgDirect3d,  WM_UPDATEPROPERTIES, 0, 0);
            break;
        }
            
        propModified = 1;
        
        return TRUE;
    }

    return FALSE;
}

static char* strPct(int value) {
    static char buffer[32];
    sprintf(buffer, "%d%%", value);
    return buffer;
}

static char* strDec(int value) {
    static char buffer[32];
    sprintf(buffer, "%d.%.2d", value / 100, value % 100);
    return buffer;
}

static char* strPt(int value) {
    static char buffer[32];
    sprintf(buffer, "%d.%d", value / 2, 5 * (value & 1));
    return buffer;
}

/* Format a multiplier x100 (e.g. 100 = 1.00x, 170 = 1.70x). */
static char* strMul100(int value) {
    static char buffer[32];
    sprintf(buffer, "%d.%02dx", value / 100, value % 100);
    return buffer;
}

/* Format scanline shape exponent p (0..100 slider -> p in [0, 4]). */
static char* strScanShapeP(int shapePct) {
    static char buffer[32];
    int p100 = shapePct * 4;  /* 0..400 representing p in [0, 4] */
    sprintf(buffer, "p=%d.%02d", p100 / 100, p100 % 100);
    return buffer;
}

/* Match (depth, shape) against the named presets so the combobox stays
** in sync with the sliders.  depthPct = raw scanlinesPct (not inverted). */
static int detectScanShapePreset(int depthPct, int shapePct) {
    if (depthPct == 30 && shapePct == 25)  return 0; /* Gentle */
    if (depthPct == 0  && shapePct == 50)  return 1; /* Standard */
    if (depthPct == 0  && shapePct == 75)  return 2; /* Sharp */
    if (depthPct == 0  && shapePct == 100) return 3; /* Trinitron */
    return 4; /* Custom */
}

/* Bright comp / Preset / Sharpness exist only in the DX12 backend, so
** grey them out on DDraw / GDI; the Depth slider works everywhere. */
static void updateScanlineDx12Controls(HWND hDlg, int dx12, int scanlinesEnable, int brightAuto)
{
    int en = dx12 && scanlinesEnable;
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESBRIGHTLABEL), en);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESBRIGHTAUTO),  en);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESBRIGHTSLIDE), en && !brightAuto);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESBRIGHTVALUE), en);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESHAPELABEL),   en);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESHAPEMODE),    en);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESHARPLABEL),   en);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESHAPESLIDE),   en);
    EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESHAPEVALUE),   en);
}

static BOOL_DLG_RET CALLBACK videoSoftwareDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;
    /* Live driver index, mirroring the General tab combobox.  Updated by
    ** WM_VIDEO_DRIVER_CHANGED; pProperties->video.driver only takes the
    ** new value on PSN_APPLY, so we can't rely on it for live grey-out. */
    static int liveDriver;
    static int monitorType;
    static int monitorColor;
    static int oldMonitorColor;
    static int oldScanlinesEnable;
    static int oldScanlinesPct;
    static int oldScanlinesBrightAuto;
    static int oldScanlinesBrightPct;
    static int oldScanlinesShapeMode;
    static int oldScanlinesShapePct;
    static int oldColorGhostingEnable;
    static int oldColorGhostingWidth;
    static int oldHoriz;
    static int oldVert;
    static int oldDeinterlace;
    static int brightness;
    static int saturation;
    static int contrast;
    static int gamma;
    int value;

    switch (iMsg) {
    case WM_INITDIALOG:
        pProperties = pCurrentProperties;
        liveDriver  = pProperties->video.driver;

        /* Init language specific dialog items */
        SetDlgItemTextU(hDlg, IDC_MONGROUPBOX, langPropMonMonGB());
        SetDlgItemTextU(hDlg, IDC_MONTYPETEXT, langPropMonTypeText());
        SetDlgItemTextU(hDlg, IDC_MONEMUTEXT, langPropMonEmuText());
        SetDlgItemTextU(hDlg, IDC_MONVIDEOTYPETEXT, langPropVideoTypeText());
        SetDlgItemTextU(hDlg, IDC_MONWINDOWSIZETEXT, langPropWindowSizeText());
        SetDlgItemTextU(hDlg, IDC_MONDEINTERLACE, langPropMonDeInterlace());
        SetDlgItemTextU(hDlg, IDC_MONBLENDFRAMES, langPropMonBlendFrames());
        SetDlgItemTextU(hDlg, IDC_EFFECTSGB, langPropMonEffectsGB());
        SetDlgItemTextU(hDlg, IDC_D3D_LINEARFILTERING, langPropD3DLinearFilteringText());

        setButtonCheck(hDlg, IDC_MONDEINTERLACE, pProperties->video.deInterlace, 1);
        setButtonCheck(hDlg, IDC_MONBLENDFRAMES, pProperties->video.blendFrames, 1);
        setButtonCheck(hDlg, IDC_D3D_LINEARFILTERING, pProperties->video.d3d.linearFiltering, 1);
        /* Linear filtering only takes effect on D3D-based drivers; grey out for
           DDraw / GDI so the user knows the checkbox is a no-op there. */
        EnableWindow(GetDlgItem(hDlg, IDC_D3D_LINEARFILTERING),
                     liveDriver == P_VIDEO_DRVDIRECTX_D3D12);
        
        /* Init dropdown lists */
        initDropList(hDlg, IDC_MONTYPE, pVideoMon, pProperties->video.monitorColor);
        initDropList(hDlg, IDC_PALEMU, pVideoPalEmu, pProperties->video.monitorType);
        initDropList(hDlg, IDC_MONSIZE, pVideoMonSize, pProperties->video.windowSize);

        monitorType             = pProperties->video.monitorType;
        monitorColor            = pProperties->video.monitorColor;
        oldMonitorColor         = pProperties->video.monitorColor;

        oldScanlinesEnable     = pProperties->video.scanlinesEnable;
        oldScanlinesPct        = pProperties->video.scanlinesPct;
        oldScanlinesBrightAuto = pProperties->video.scanlinesBrightAuto;
        oldScanlinesBrightPct  = pProperties->video.scanlinesBrightPct;
        oldScanlinesShapeMode  = pProperties->video.scanlinesShapeMode;
        oldScanlinesShapePct   = pProperties->video.scanlinesShapePct;
        oldColorGhostingEnable = pProperties->video.colorSaturationEnable;
        oldColorGhostingWidth  = pProperties->video.colorSaturationWidth;
        oldHoriz               = pProperties->video.horizontalStretch;
        oldVert                = pProperties->video.verticalStretch;
        oldDeinterlace         = pProperties->video.deInterlace;

        SetDlgItemTextU(hDlg, IDC_MONBRIGHTNESSTEXT, langPropMonBrightness());
        SetDlgItemTextU(hDlg, IDC_MONCONTRASTTEXT, langPropMonContrast());
        SetDlgItemTextU(hDlg, IDC_MONSATURATIONTEXT, langPropMonSaturation());
        SetDlgItemTextU(hDlg, IDC_MONGAMMATEXT, langPropMonGamma());
        SetDlgItemTextU(hDlg, IDC_SCANLINESENABLE, langPropMonScanlines());
        SetDlgItemTextU(hDlg, IDC_SCANLINESBRIGHTLABEL, langPropMonScanlinesBright());
        SetDlgItemTextU(hDlg, IDC_SCANLINESBRIGHTAUTO, langPropMonScanlinesBrightAuto());
        SetDlgItemTextU(hDlg, IDC_SCANLINESHAPELABEL,  langPropMonScanlinesShape());
        SetDlgItemTextU(hDlg, IDC_SCANLINESDEPTHLABEL, langPropMonScanlinesDepth());
        SetDlgItemTextU(hDlg, IDC_SCANLINESHARPLABEL,  langPropMonScanlinesSharpness());
        SetDlgItemTextU(hDlg, IDC_COLORGHOSTINGENABLE, langPropMonColorGhosting());
        

        contrast   = pProperties->video.contrast;
        brightness = pProperties->video.brightness;
        saturation = pProperties->video.saturation;
        gamma      = pProperties->video.gamma;

        setButtonCheck(hDlg, IDC_SCANLINESENABLE, pProperties->video.scanlinesEnable, 1);
        setButtonCheck(hDlg, IDC_SCANLINESBRIGHTAUTO, pProperties->video.scanlinesBrightAuto, 1);
        setButtonCheck(hDlg, IDC_COLORGHOSTINGENABLE, pProperties->video.colorSaturationEnable, 1);

        EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESSLIDEBAR), oldScanlinesEnable);
        EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESVALUE), oldScanlinesEnable);
        EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESDEPTHLABEL), oldScanlinesEnable);
        
        EnableWindow(GetDlgItem(hDlg, IDC_COLORGHOSTINGSLIDEBAR), oldColorGhostingEnable);
        EnableWindow(GetDlgItem(hDlg, IDC_COLORGHOSTINGVALUE), oldColorGhostingEnable);

        SendMessage(GetDlgItem(hDlg, IDC_SCANLINESSLIDEBAR), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 100));
        SendMessage(GetDlgItem(hDlg, IDC_SCANLINESSLIDEBAR), TBM_SETPOS,   1, (LPARAM)(100 - oldScanlinesPct));
        SetDlgItemTextU(hDlg, IDC_SCANLINESVALUE, strPct(100 - oldScanlinesPct));

        /* Manual slider = multiplier x100, clamped 100..300 (1.00x..3.00x).
        ** SDR clamps internally to 2.0x; HDR uses the full 3.0x range. */
        if (oldScanlinesBrightPct < 100) oldScanlinesBrightPct = 100;
        if (oldScanlinesBrightPct > 300) oldScanlinesBrightPct = 300;
        pProperties->video.scanlinesBrightPct = oldScanlinesBrightPct;
        SendMessage(GetDlgItem(hDlg, IDC_SCANLINESBRIGHTSLIDE), TBM_SETRANGE, 0, (LPARAM)MAKELONG(100, 300));
        SendMessage(GetDlgItem(hDlg, IDC_SCANLINESBRIGHTSLIDE), TBM_SETPOS,   1, (LPARAM)oldScanlinesBrightPct);
        SetDlgItemTextU(hDlg, IDC_SCANLINESBRIGHTVALUE, strMul100(oldScanlinesBrightPct));

        /* Preset snaps both depth + shape; manual edits auto-detect preset. */
        {
            HWND hCombo = GetDlgItem(hDlg, IDC_SCANLINESHAPEMODE);
            SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
            ComboAddStringU(hCombo, langEnumScanShapeGentle());
            ComboAddStringU(hCombo, langEnumScanShapeStandard());
            ComboAddStringU(hCombo, langEnumScanShapeSharp());
            ComboAddStringU(hCombo, langEnumScanShapeTrinitron());
            ComboAddStringU(hCombo, langEnumScanShapeCustom());
            if (oldScanlinesShapeMode < 0 || oldScanlinesShapeMode > 4) oldScanlinesShapeMode = 4;
            SendMessage(hCombo, CB_SETCURSEL, oldScanlinesShapeMode, 0);
        }
        if (oldScanlinesShapePct < 0)   oldScanlinesShapePct = 0;
        if (oldScanlinesShapePct > 100) oldScanlinesShapePct = 100;
        pProperties->video.scanlinesShapePct = oldScanlinesShapePct;
        SendMessage(GetDlgItem(hDlg, IDC_SCANLINESHAPESLIDE), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 100));
        SendMessage(GetDlgItem(hDlg, IDC_SCANLINESHAPESLIDE), TBM_SETPOS,   1, (LPARAM)oldScanlinesShapePct);
        SetDlgItemTextU(hDlg, IDC_SCANLINESHAPEVALUE, strScanShapeP(oldScanlinesShapePct));
        updateScanlineDx12Controls(hDlg,
                                   (liveDriver == P_VIDEO_DRVDIRECTX_D3D12),
                                   oldScanlinesEnable, oldScanlinesBrightAuto);

        SendMessage(GetDlgItem(hDlg, IDC_COLORGHOSTINGSLIDEBAR), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 4));
        SendMessage(GetDlgItem(hDlg, IDC_COLORGHOSTINGSLIDEBAR), TBM_SETPOS,   1, (LPARAM)oldColorGhostingWidth);
        SetDlgItemTextU(hDlg, IDC_COLORGHOSTINGVALUE, strPt(oldColorGhostingWidth));

        SendMessage(GetDlgItem(hDlg, IDC_MONSATURATIONSLIDE), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 200));
        SendMessage(GetDlgItem(hDlg, IDC_MONSATURATIONSLIDE), TBM_SETPOS,   1, (LPARAM)saturation);
        SetDlgItemTextU(hDlg, IDC_MONSATURATIONVALUE, strDec(saturation));

        SendMessage(GetDlgItem(hDlg, IDC_MONBRIGHTNESSSLIDE), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 200));
        SendMessage(GetDlgItem(hDlg, IDC_MONBRIGHTNESSSLIDE), TBM_SETPOS,   1, (LPARAM)brightness);
        SetDlgItemTextU(hDlg, IDC_MONBRIGHTNESSVALUE, strDec(brightness));

        SendMessage(GetDlgItem(hDlg, IDC_MONCONTRASTSLIDE), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 200));
        SendMessage(GetDlgItem(hDlg, IDC_MONCONTRASTSLIDE), TBM_SETPOS,   1, (LPARAM)contrast);
        SetDlgItemTextU(hDlg, IDC_MONCONTRASTVALUE, strDec(contrast));

        SendMessage(GetDlgItem(hDlg, IDC_MONGAMMASLIDE), TBM_SETRANGE, 0, (LPARAM)MAKELONG(0, 200));
        SendMessage(GetDlgItem(hDlg, IDC_MONGAMMASLIDE), TBM_SETPOS,   1, (LPARAM)gamma);
        SetDlgItemTextU(hDlg, IDC_MONGAMMAVALUE, strDec(gamma));

        EnableWindow(GetDlgItem(hDlg, IDC_MONSATURATIONSLIDE), monitorColor == P_VIDEO_COLOR);
        EnableWindow(GetDlgItem(hDlg, IDC_MONSATURATIONVALUE), monitorColor == P_VIDEO_COLOR);
        EnableWindow(GetDlgItem(hDlg, IDC_MONSATURATIONTEXT), monitorColor == P_VIDEO_COLOR);

        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SCANLINESENABLE:
            pProperties->video.scanlinesEnable = getButtonCheck(hDlg, IDC_SCANLINESENABLE);
            EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESSLIDEBAR), pProperties->video.scanlinesEnable);
            EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESVALUE), pProperties->video.scanlinesEnable);
            EnableWindow(GetDlgItem(hDlg, IDC_SCANLINESDEPTHLABEL), pProperties->video.scanlinesEnable);
            updateScanlineDx12Controls(hDlg,
                                       (liveDriver == P_VIDEO_DRVDIRECTX_D3D12),
                                       pProperties->video.scanlinesEnable,
                                       pProperties->video.scanlinesBrightAuto);
            
            videoSetScanLines(theVideo, pProperties->video.scanlinesEnable, pProperties->video.scanlinesPct);
            updateEmuWindow();
            break;

        case IDC_SCANLINESHAPEMODE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendMessage(GetDlgItem(hDlg, IDC_SCANLINESHAPEMODE), CB_GETCURSEL, 0, 0);
                if (sel < 0 || sel > 4) sel = 4;
                pProperties->video.scanlinesShapeMode = sel;
                if (sel < 4) {
                    /* Apply preset.  scanlinesPct = 100 - depth_pct (UI shows depth_pct). */
                    int newDepthPct = 100;
                    int newShapePct = 50;
                    switch (sel) {
                    case 0: newDepthPct = 30;  newShapePct = 25;  break; /* Gentle:    s=0.30, p=1.0 */
                    case 1: newDepthPct = 0;   newShapePct = 50;  break; /* Standard:  s=0,    p=2.0 */
                    case 2: newDepthPct = 0;   newShapePct = 75;  break; /* Sharp:     s=0,    p=3.0 */
                    case 3: newDepthPct = 0;   newShapePct = 100; break; /* Trinitron: s=0,    p=4.0 */
                    }
                    pProperties->video.scanlinesPct      = newDepthPct;
                    pProperties->video.scanlinesShapePct = newShapePct;
                    SendMessage(GetDlgItem(hDlg, IDC_SCANLINESSLIDEBAR), TBM_SETPOS, 1, (LPARAM)(100 - newDepthPct));
                    SetDlgItemTextU(hDlg, IDC_SCANLINESVALUE, strPct(100 - newDepthPct));
                    SendMessage(GetDlgItem(hDlg, IDC_SCANLINESHAPESLIDE), TBM_SETPOS, 1, (LPARAM)newShapePct);
                    SetDlgItemTextU(hDlg, IDC_SCANLINESHAPEVALUE, strScanShapeP(newShapePct));
                    videoSetScanLines(theVideo, pProperties->video.scanlinesEnable, pProperties->video.scanlinesPct);
                    videoSetScanLinesShape(theVideo, newShapePct);
                }
                updateEmuWindow();
            }
            break;

        case IDC_SCANLINESBRIGHTAUTO:
            /* Toggle does NOT touch the manual slider value -- when auto
            ** is on, the comp shader gets its own internal factor; when
            ** auto is off, the slider's last value takes over. */
            pProperties->video.scanlinesBrightAuto = getButtonCheck(hDlg, IDC_SCANLINESBRIGHTAUTO);
            updateScanlineDx12Controls(hDlg,
                                       (liveDriver == P_VIDEO_DRVDIRECTX_D3D12),
                                       pProperties->video.scanlinesEnable,
                                       pProperties->video.scanlinesBrightAuto);
            videoSetScanLinesBrightness(theVideo,
                pProperties->video.scanlinesBrightAuto, pProperties->video.scanlinesBrightPct);
            updateEmuWindow();
            break;

        case IDC_COLORGHOSTINGENABLE:
            pProperties->video.colorSaturationEnable = getButtonCheck(hDlg, IDC_COLORGHOSTINGENABLE);
            EnableWindow(GetDlgItem(hDlg, IDC_COLORGHOSTINGSLIDEBAR), pProperties->video.colorSaturationEnable);
            EnableWindow(GetDlgItem(hDlg, IDC_COLORGHOSTINGVALUE), pProperties->video.colorSaturationEnable);

            videoSetColorSaturation(theVideo, pProperties->video.colorSaturationEnable, pProperties->video.colorSaturationWidth);
            updateEmuWindow();
            break;

        case IDC_PALEMU:
            monitorType = getDropListIndex(hDlg, IDC_PALEMU, pVideoPalEmu);
            videoSetPalMode(theVideo, monitorType);
            updateEmuWindow();
            break;

        case IDC_MONTYPE:
            monitorColor = getDropListIndex(hDlg, IDC_MONTYPE, pVideoMon);
            /* Push to pProperties immediately so DX12 picks it up live;
            ** Cancel restores oldMonitorColor.  videoSetColorMode keeps
            ** the DirectDraw / GDI color tables in sync. */
            pProperties->video.monitorColor = monitorColor;
            switch (monitorColor) {
            case P_VIDEO_COLOR:
                videoSetColorMode(theVideo, VIDEO_COLOR);
                break;
            case P_VIDEO_BW:
                videoSetColorMode(theVideo, VIDEO_BLACKWHITE);
                break;
            case P_VIDEO_GREEN:
                videoSetColorMode(theVideo, VIDEO_GREEN);
                break;
            case P_VIDEO_AMBER:
                videoSetColorMode(theVideo, VIDEO_AMBER);
                break;
            }
            EnableWindow(GetDlgItem(hDlg, IDC_MONSATURATIONSLIDE), monitorColor == P_VIDEO_COLOR);
            EnableWindow(GetDlgItem(hDlg, IDC_MONSATURATIONVALUE), monitorColor == P_VIDEO_COLOR);
            EnableWindow(GetDlgItem(hDlg, IDC_MONSATURATIONTEXT), monitorColor == P_VIDEO_COLOR);
            updateEmuWindow();
            break;

        case IDC_MONDEINTERLACE:
            pProperties->video.deInterlace   = getButtonCheck(hDlg, IDC_MONDEINTERLACE);
            videoSetDeInterlace(theVideo, pProperties->video.deInterlace);
            updateEmuWindow();
            break;
        case IDC_MONBLENDFRAMES:
            pProperties->video.blendFrames   = getButtonCheck(hDlg, IDC_MONBLENDFRAMES);
            videoSetBlendFrames(theVideo, pProperties->video.blendFrames);
            updateEmuWindow();
            break;
        case IDC_D3D_LINEARFILTERING:
            pProperties->video.d3d.linearFiltering = getButtonCheck(hDlg, IDC_D3D_LINEARFILTERING);
            updateEmuWindow();
            break;

        }
        return TRUE;

    case WM_VIDEO_DRIVER_CHANGED:
        {
            int dx12;
            liveDriver = (int)wParam;
            dx12       = (liveDriver == P_VIDEO_DRVDIRECTX_D3D12);
            /* Grey out Linear Filter when switching to a non-D3D driver. */
            EnableWindow(GetDlgItem(hDlg, IDC_D3D_LINEARFILTERING), dx12);
            updateScanlineDx12Controls(hDlg, dx12,
                                       pProperties->video.scanlinesEnable,
                                       pProperties->video.scanlinesBrightAuto);
        }
        return TRUE;

    case WM_NOTIFY:
        switch (wParam) {
        case IDC_SCANLINESSLIDEBAR:
            pProperties->video.scanlinesPct = 100 - SendMessage(GetDlgItem(hDlg, IDC_SCANLINESSLIDEBAR), TBM_GETPOS, 0, 0);
            SetDlgItemTextU(hDlg, IDC_SCANLINESVALUE, strPct(100 - pProperties->video.scanlinesPct));

            /* Auto-detect preset (or Custom) from the new (depth, shape) pair. */
            {
                int newMode = detectScanShapePreset(pProperties->video.scanlinesPct, pProperties->video.scanlinesShapePct);
                if (newMode != pProperties->video.scanlinesShapeMode) {
                    pProperties->video.scanlinesShapeMode = newMode;
                    SendMessage(GetDlgItem(hDlg, IDC_SCANLINESHAPEMODE), CB_SETCURSEL, newMode, 0);
                }
            }

            videoSetScanLines(theVideo, pProperties->video.scanlinesEnable, pProperties->video.scanlinesPct);
            updateEmuWindow();
            break;

        case IDC_SCANLINESHAPESLIDE:
            pProperties->video.scanlinesShapePct = (int)SendMessage(GetDlgItem(hDlg, IDC_SCANLINESHAPESLIDE), TBM_GETPOS, 0, 0);
            SetDlgItemTextU(hDlg, IDC_SCANLINESHAPEVALUE, strScanShapeP(pProperties->video.scanlinesShapePct));

            /* Auto-detect preset (or Custom) from the new (depth, shape) pair. */
            {
                int newMode = detectScanShapePreset(pProperties->video.scanlinesPct, pProperties->video.scanlinesShapePct);
                if (newMode != pProperties->video.scanlinesShapeMode) {
                    pProperties->video.scanlinesShapeMode = newMode;
                    SendMessage(GetDlgItem(hDlg, IDC_SCANLINESHAPEMODE), CB_SETCURSEL, newMode, 0);
                }
            }

            videoSetScanLinesShape(theVideo, pProperties->video.scanlinesShapePct);
            updateEmuWindow();
            break;

        case IDC_SCANLINESBRIGHTSLIDE:
            pProperties->video.scanlinesBrightPct = (int)SendMessage(GetDlgItem(hDlg, IDC_SCANLINESBRIGHTSLIDE), TBM_GETPOS, 0, 0);
            SetDlgItemTextU(hDlg, IDC_SCANLINESBRIGHTVALUE, strMul100(pProperties->video.scanlinesBrightPct));
            videoSetScanLinesBrightness(theVideo,
                pProperties->video.scanlinesBrightAuto, pProperties->video.scanlinesBrightPct);
            updateEmuWindow();
            break;

            
        case IDC_COLORGHOSTINGSLIDEBAR:
            pProperties->video.colorSaturationWidth = SendMessage(GetDlgItem(hDlg, IDC_COLORGHOSTINGSLIDEBAR), TBM_GETPOS, 0, 0);
            SetDlgItemTextU(hDlg, IDC_COLORGHOSTINGVALUE, strPt(pProperties->video.colorSaturationWidth));

            videoSetColorSaturation(theVideo, pProperties->video.colorSaturationEnable, pProperties->video.colorSaturationWidth);
            updateEmuWindow();
            break;

        case IDC_MONSATURATIONSLIDE:
            value = SendMessage(GetDlgItem(hDlg, IDC_MONSATURATIONSLIDE), TBM_GETPOS, 0, 0);
            if (value != saturation) {
                saturation = value;
                SetDlgItemTextU(hDlg, IDC_MONSATURATIONVALUE, strDec(saturation));
                videoSetColors(theVideo, saturation, brightness, contrast, gamma);
                updateEmuWindow();
            }
            break;

        case IDC_MONBRIGHTNESSSLIDE:
            value = SendMessage(GetDlgItem(hDlg, IDC_MONBRIGHTNESSSLIDE), TBM_GETPOS, 0, 0);
            if (value != brightness) {
                brightness = value;
                SetDlgItemTextU(hDlg, IDC_MONBRIGHTNESSVALUE, strDec(brightness));
                videoSetColors(theVideo, saturation, brightness, contrast, gamma);
                updateEmuWindow();
            }
            break;

        case IDC_MONCONTRASTSLIDE:
            value = SendMessage(GetDlgItem(hDlg, IDC_MONCONTRASTSLIDE), TBM_GETPOS, 0, 0);
            if (value != contrast) {
                contrast = value;
                SetDlgItemTextU(hDlg, IDC_MONCONTRASTVALUE, strDec(contrast));
                videoSetColors(theVideo, saturation, brightness, contrast, gamma);
                updateEmuWindow();
            }
            break;

        case IDC_MONGAMMASLIDE:
            value = SendMessage(GetDlgItem(hDlg, IDC_MONGAMMASLIDE), TBM_GETPOS, 0, 0);
            if (value != gamma) {
                gamma = value;
                SetDlgItemTextU(hDlg, IDC_MONGAMMAVALUE, strDec(gamma));
                videoSetColors(theVideo, saturation, brightness, contrast, gamma);
                updateEmuWindow();
            }
            break;
        }
        return TRUE;

    case WM_CANCELUPDATEPROPERTIES:
        pProperties->video.horizontalStretch     = oldHoriz;
        pProperties->video.verticalStretch       = oldVert;
        pProperties->video.deInterlace           = oldDeinterlace;
        pProperties->video.scanlinesEnable       = oldScanlinesEnable;
        pProperties->video.scanlinesPct          = oldScanlinesPct;
        pProperties->video.scanlinesBrightAuto   = oldScanlinesBrightAuto;
        pProperties->video.scanlinesBrightPct    = oldScanlinesBrightPct;
        pProperties->video.scanlinesShapeMode    = oldScanlinesShapeMode;
        pProperties->video.scanlinesShapePct     = oldScanlinesShapePct;
        pProperties->video.colorSaturationEnable = oldColorGhostingEnable;
        pProperties->video.colorSaturationWidth  = oldColorGhostingWidth;
        pProperties->video.monitorColor          = oldMonitorColor;

        videoSetScanLines(theVideo, pProperties->video.scanlinesEnable, pProperties->video.scanlinesPct);
        videoSetScanLinesBrightness(theVideo, pProperties->video.scanlinesBrightAuto, pProperties->video.scanlinesBrightPct);
        videoSetScanLinesShape(theVideo, pProperties->video.scanlinesShapePct);

        videoSetColorSaturation(theVideo, pProperties->video.colorSaturationEnable, pProperties->video.colorSaturationWidth);
        videoSetPalMode(theVideo, pProperties->video.monitorType);
        videoSetColors(theVideo, pProperties->video.saturation, pProperties->video.brightness, 
                        pProperties->video.contrast, pProperties->video.gamma);
        switch (pProperties->video.monitorColor) {
        case P_VIDEO_COLOR:
            videoSetColorMode(theVideo, VIDEO_COLOR);
            break;
        case P_VIDEO_BW:
            videoSetColorMode(theVideo, VIDEO_BLACKWHITE);
            break;
        case P_VIDEO_GREEN:
            videoSetColorMode(theVideo, VIDEO_GREEN);
            break;
        case P_VIDEO_AMBER:
            videoSetColorMode(theVideo, VIDEO_AMBER);
            break;
        }
        updateEmuWindow();
        return TRUE;

    case WM_UPDATEPROPERTIES:
        pProperties->video.monitorColor      = monitorColor;
        pProperties->video.monitorType       = monitorType;
        pProperties->video.contrast          = contrast;
        pProperties->video.brightness        = brightness;
        pProperties->video.saturation        = saturation;
        pProperties->video.gamma             = gamma;
        propModified = 1;
        
        return TRUE;
    }

    return FALSE;
}

static BOOL_DLG_RET CALLBACK videoDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;

    switch (iMsg) {
    case WM_INITDIALOG:
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }
        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;
        pCurrentProperties = pProperties;

        hDlgVideo = hDlg;

        hDlgVideoSoftware = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_VIDEO_SOFTWARE), hDlg, videoSoftwareDlgProc);
        SetWindowPos(hDlgVideoSoftware,  NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        /* The dedicated DX9 sub-panel was removed along with the DX9 driver;
        ** the software panel covers every remaining driver. */
        ShowWindow(hDlgVideoSoftware, SW_NORMAL);

        return FALSE;

    case WM_VIDEO_DRIVER_CHANGED:
        /* Forward to children so e.g. Linear Filter checkbox enable state
           updates without needing the user to reopen Properties. */
        SendMessage(hDlgVideoSoftware, WM_VIDEO_DRIVER_CHANGED, wParam, 0);
        return TRUE;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        if ((((NMHDR FAR *)lParam)->code) != PSN_APPLY) {
            if ((((NMHDR FAR *)lParam)->code) == PSN_QUERYCANCEL) {
               SendMessage(hDlgVideoSoftware,  WM_CANCELUPDATEPROPERTIES, 0, 0);
            }
            return FALSE;
        }

        SendMessage(hDlgVideoSoftware,  WM_UPDATEPROPERTIES, 0, 0);
            
        propModified = 1;
        return TRUE;
    }

    return FALSE;
}

static void getMidiList(HWND hDlg, int id, Properties* pProperties) {
    char buffer[MAX_PATH];
    int idx = SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
    int rv = SendDlgItemMessage(hDlg, id, CB_GETLBTEXT, idx, (LPARAM)buffer);
    int*  midiType;
    char* drvName;
    char* drvDesc;
    int   noSupportFile;

    switch (id) {
    default:
    case IDC_MIDIOUT: 
        midiType = &pProperties->sound.MidiOut.type;
        drvName  = pProperties->sound.MidiOut.name;
        drvDesc  = pProperties->sound.MidiOut.desc;
        noSupportFile = 1;
        break;
    case IDC_MIDIIN: 
        midiType = &pProperties->sound.MidiIn.type;
        drvName  = pProperties->sound.MidiIn.name;
        drvDesc  = pProperties->sound.MidiIn.desc;
        noSupportFile = 1;
        break;
    case IDC_YKIN: 
        midiType = &pProperties->sound.YkIn.type;
        drvName  = pProperties->sound.YkIn.name;
        drvDesc  = pProperties->sound.YkIn.desc;
        noSupportFile = 1;
        break;
    }

    if (idx + noSupportFile < P_MIDI_HOST) {
        *midiType = idx;
    }
    else {
        char* name = buffer;
        // Find the printer name from string
        while (*name && (*name != '-' || name[1] != ' ')) {
            name++;
        }
        
        strcpy(drvName, buffer);
        drvName[name - buffer - 1] = 0;

        if (*name) name++;
        if (*name) name++;

        *midiType = P_COM_HOST;
        strcpy(drvDesc, name);
    }
}

static void updateMidiList(HWND hDlg, int id, Properties* pProperties)
{
    int   midiType;
    char* drvName;
    char* drvDesc;
    int   devNum;
    int   noSupportFile;
    int   i;

    switch (id) {
    default:
    case IDC_MIDIOUT: 
        midiType = pProperties->sound.MidiOut.type;
        drvName  = pProperties->sound.MidiOut.name;
        drvDesc  = pProperties->sound.MidiOut.desc;
        noSupportFile = 1;
        break;
    case IDC_MIDIIN: 
        midiType = pProperties->sound.MidiIn.type;
        drvName  = pProperties->sound.MidiIn.name;
        drvDesc  = pProperties->sound.MidiIn.desc;
        noSupportFile = 1;
        break;
    case IDC_YKIN: 
        midiType = pProperties->sound.YkIn.type;
        drvName  = pProperties->sound.YkIn.name;
        drvDesc  = pProperties->sound.YkIn.desc;
        noSupportFile = 1;
        break;
    }

    while (CB_ERR != SendDlgItemMessage(hDlg, id, CB_DELETESTRING, 0, 0));

    // Add NONE:
    ComboAddStringU(GetDlgItem(hDlg, id), langTextNone());
    SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 0, 0); // Set as default

    // Add FILE
    if (!noSupportFile) {
        ComboAddStringU(GetDlgItem(hDlg, id), langTextFile());
        if (midiType == P_MIDI_FILE) {
            SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 1, 0);
        }
    }

    devNum = id == IDC_MIDIOUT ? midiOutGetDeviceCount() : midiInGetDeviceCount();
    for (i = 0; i < devNum; i++) {
        char buf[512];
        const char* name = id == IDC_MIDIOUT ? midiOutGetDeviceIdString(i) : midiInGetDeviceIdString(i);
        const char* desc = id == IDC_MIDIOUT ? midiOutGetDeviceName(i)     : midiInGetDeviceName(i);

        sprintf(buf, "%s - %s", name, desc);

        ComboAddStringU(GetDlgItem(hDlg, id), buf);
        if (midiType == P_MIDI_HOST && 0 == strcmp(drvName, name)) {
            SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 2 - noSupportFile + i, 0);
        }
    }
}

static void getMidiChannelList(HWND hDlg, int id, Properties* pProperties) 
{
    int idx = SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);
    pProperties->sound.YkIn.channel = idx;
}

static void updateMidiChannelList(HWND hDlg, int id, Properties* pProperties)
{
    int i;

    while (CB_ERR != SendDlgItemMessage(hDlg, id, CB_DELETESTRING, 0, 0));

    // Add ALL:
    ComboAddStringU(GetDlgItem(hDlg, id), langPropSndMidiAll());
    SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 0, 0); // Set as default

    for (i = 1; i <= 16; i++) {
        char buf[32];
        sprintf(buf, "%d", i);

        ComboAddStringU(GetDlgItem(hDlg, id), buf);
        if (i == pProperties->sound.YkIn.channel) {
            SendDlgItemMessage(hDlg, id, CB_SETCURSEL, i, 0);
        }
    }
}



static BOOL_DLG_RET CALLBACK soundDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    static Properties* pProperties;
    int index;

    switch (iMsg) {
    case WM_INITDIALOG:
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }
        hDlgSound = hDlg;

        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;

        initDropList(hDlg, IDC_SNDDRIVER, pSoundDriver, pProperties->sound.driver);
        {
            int index = 0;
            while (pProperties->sound.bufSize > soundBufSizes[index]) {
                if (soundBufSizes[index] == 350) {
                    break;
                }
                index++;
            }
            initDropList(hDlg, IDC_SNDBUFSZ, pSoundBufferSize, index);
        }

        SetDlgItemTextU(hDlg, IDC_AUDIODRVGROUPBOX, langPropPerfAudioDrvGB());
        SetDlgItemTextU(hDlg, IDC_PERFSNDDRVTEXT, langPropPerfAudioDrvText());
        SetDlgItemTextU(hDlg, IDC_PERFSNDBUFSZTEXT, langPropPerfAudioBufSzText());
        SetDlgItemTextU(hDlg, IDC_YKINGROUPBOX, langPropSndYkInGB());
        SetDlgItemTextU(hDlg, IDC_YKINTEXT, langTextDevice());
        SetDlgItemTextU(hDlg, IDC_YKINCHANTEXT, langPropSndMidiChannel());
        SetDlgItemTextU(hDlg, IDC_MIDIINGROUPBOX, langPropSndMidiInGB());
        SetDlgItemTextU(hDlg, IDC_MIDIINTEXT, langTextDevice());
#if 0
        SetDlgItemTextU(hDlg, IDI_MIDIINFILENAMETEXT, langTextFilename());
#endif
        SetDlgItemTextU(hDlg, IDC_MIDIOUTGROUPBOX, langPropSndMidiOutGB());
        SetDlgItemTextU(hDlg, IDC_MIDIOUTTEXT, langTextDevice());
#if 0
        SetDlgItemTextU(hDlg, IDI_MIDIOUTFILENAMETEXT, langTextFilename());
#endif
        SetWindowTextU(GetDlgItem(hDlg, IDC_MIDIOUTMT32TOGM), langPropSndMt32ToGm());

        {
            int index = 0;
            int oversampling = 1;
            for (; index < 4; index++, oversampling <<= 1) {
                const char* modes[] = { "1 x", "2 x", "4 x", "8 x"} ;
                ComboAddStringU(GetDlgItem(hDlg, IDC_OVERSAMPLEMSXMUSIC), modes[index]);
                if (oversampling <= pProperties->sound.chip.ym2413Oversampling) {
                    SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMSXMUSIC, CB_SETCURSEL, index, 0);
                }
                
                ComboAddStringU(GetDlgItem(hDlg, IDC_OVERSAMPLEMSXAUDIO), modes[index]);
                if (oversampling <= pProperties->sound.chip.y8950Oversampling) {
                    SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMSXAUDIO, CB_SETCURSEL, index, 0);
                }
                
                ComboAddStringU(GetDlgItem(hDlg, IDC_OVERSAMPLEMOONSOUND), modes[index]);
                if (oversampling <= pProperties->sound.chip.moonsoundOversampling) {
                    SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMOONSOUND, CB_SETCURSEL, index, 0);
                }
            }
        }

        updateMidiList(hDlg, IDC_MIDIOUT, pProperties);
        updateMidiList(hDlg, IDC_MIDIIN,  pProperties);
        updateMidiList(hDlg, IDC_YKIN,  pProperties);
        updateMidiChannelList(hDlg, IDC_YKINCHAN, pProperties);

        setButtonCheck(hDlg, IDC_MIDIOUTMT32TOGM, pProperties->sound.MidiOut.mt32ToGm, 1);

        {
            int idx = SendDlgItemMessage(hDlg, IDC_MIDIOUT, CB_GETCURSEL, 0, 0) + 1;
#if 0
            EnableWindow(GetDlgItem(hDlg, IDC_MIDIOUTFILENAMEBROWSE), idx == P_MIDI_FILE);
            EnableWindow(GetDlgItem(hDlg, IDI_MIDIOUTFILENAME), idx == P_MIDI_FILE);
#endif
            EnableWindow(GetDlgItem(hDlg, IDC_MIDIOUTMT32TOGM), idx >= P_MIDI_HOST);

            idx = SendDlgItemMessage(hDlg, IDC_MIDIIN, CB_GETCURSEL, 0, 0);
#if 0
            EnableWindow(GetDlgItem(hDlg, IDC_MIDIINFILENAMEBROWSE), idx == P_MIDI_FILE);
            EnableWindow(GetDlgItem(hDlg, IDI_MIDIINFILENAME), idx == P_MIDI_FILE);
#endif

            idx = SendDlgItemMessage(hDlg, IDC_YKIN, CB_GETCURSEL, 0, 0) + 1;
            EnableWindow(GetDlgItem(hDlg, IDC_YKINCHANTEXT), idx >= P_MIDI_HOST);
            EnableWindow(GetDlgItem(hDlg, IDC_YKINCHAN), idx >= P_MIDI_HOST);
        }

#if 0
        SetWindowTextU(GetDlgItem(hDlg, IDI_MIDIOUTFILENAME), pProperties->sound.MidiOut.fileName);
        SetWindowTextU(GetDlgItem(hDlg, IDI_MIDIINFILENAME),  pProperties->sound.MidiIn.fileName);
#endif
        return FALSE;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDC_MIDIOUT:
            {
                int idx = SendDlgItemMessage(hDlg, IDC_MIDIOUT, CB_GETCURSEL, 0, 0) + 1;
#if 0
                EnableWindow(GetDlgItem(hDlg, IDC_MIDIOUTFILENAMEBROWSE), idx == P_MIDI_FILE);
                EnableWindow(GetDlgItem(hDlg, IDI_MIDIOUTFILENAME), idx == P_MIDI_FILE);
#endif
                EnableWindow(GetDlgItem(hDlg, IDC_MIDIOUTMT32TOGM), idx >= P_MIDI_HOST);
            }
            return TRUE;
        case IDC_MIDIIN:
            {
                int idx = SendDlgItemMessage(hDlg, IDC_MIDIIN, CB_GETCURSEL, 0, 0) + 1;
#if 0
                EnableWindow(GetDlgItem(hDlg, IDC_MIDIINFILENAMEBROWSE), idx == P_MIDI_FILE);
                EnableWindow(GetDlgItem(hDlg, IDI_MIDIINFILENAME), idx == P_MIDI_FILE);
#endif
            }
            return TRUE;
        case IDC_YKIN:
            {
                int idx = SendDlgItemMessage(hDlg, IDC_YKIN, CB_GETCURSEL, 0, 0) + 1;
                EnableWindow(GetDlgItem(hDlg, IDC_YKINCHANTEXT), idx >= P_MIDI_HOST);
                EnableWindow(GetDlgItem(hDlg, IDC_YKINCHAN), idx >= P_MIDI_HOST);
            }
            return TRUE;
#if 0
        case IDC_MIDIOUTFILENAMEBROWSE:
            {
                char fileName[MAX_PATH];
                GetWindowTextU(GetDlgItem(hDlg, IDI_MIDIOUTFILENAME), fileName, MAX_PATH - 1);
                if (openLogFile(hDlg, fileName)) {
                    SetWindowTextU(GetDlgItem(hDlg, IDI_MIDIOUTFILENAME), fileName);
                }
            }
            return TRUE;
        case IDC_MIDIINFILENAMEBROWSE:
            {
                char fileName[MAX_PATH];
                GetWindowTextU(GetDlgItem(hDlg, IDI_MIDIINFILENAME), fileName, MAX_PATH - 1);
                if (openLogFile(hDlg, pProperties->sound.MidiIn.fileName)) {
                    SetWindowTextU(GetDlgItem(hDlg, IDI_MIDIINFILENAME), fileName);
                }
            }
            return TRUE;
#endif
        case IDC_OVERSAMPLEMSXMUSIC:
            index = SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMSXMUSIC, CB_GETCURSEL, 0, 0);
            boardSetYm2413Oversampling(1 << index);
            break;
        case IDC_OVERSAMPLEMSXAUDIO:
            index = SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMSXAUDIO, CB_GETCURSEL, 0, 0);
            boardSetY8950Oversampling(1 << index);
            break;
        case IDC_OVERSAMPLEMOONSOUND:
            index = SendDlgItemMessage(hDlg, IDC_OVERSAMPLEMOONSOUND, CB_GETCURSEL, 0, 0);
            boardSetMoonsoundOversampling(1 << index);
            break;
        }
        break;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        switch (((NMHDR FAR *)lParam)->code) {
        case PSN_APPLY:
            pProperties->sound.driver           = getDropListIndex(hDlg, IDC_SNDDRIVER, pSoundDriver);
            pProperties->sound.bufSize          = soundBufSizes[getDropListIndex(hDlg, IDC_SNDBUFSZ, pSoundBufferSize)];

            getMidiList(hDlg, IDC_MIDIOUT, pProperties);
#if 0
            GetWindowTextU(GetDlgItem(hDlg, IDI_MIDIOUTFILENAME), pProperties->sound.MidiOut.fileName, MAX_PATH - 1);
#endif
            getMidiList(hDlg, IDC_MIDIIN, pProperties);
#if 0
            GetWindowTextU(GetDlgItem(hDlg, IDI_MIDIINFILENAME), pProperties->sound.MidiIn.fileName, MAX_PATH - 1);
#endif
            pProperties->sound.MidiOut.mt32ToGm = getButtonCheck(hDlg, IDC_MIDIOUTMT32TOGM);

            getMidiList(hDlg, IDC_YKIN, pProperties);
            getMidiChannelList(hDlg, IDC_YKINCHAN, pProperties);

            propModified = 1;
            return TRUE;

        case PSN_QUERYCANCEL:
            emulatorRestartSound();

            boardSetYm2413Oversampling(pProperties->sound.chip.ym2413Oversampling);
            boardSetY8950Oversampling(pProperties->sound.chip.y8950Oversampling);
            boardSetMoonsoundOversampling(pProperties->sound.chip.moonsoundOversampling);

            return FALSE;
        }
        break;
    }

    return FALSE;
}

static void updateCdromListIoctl(HWND hWnd, Properties* pProperties)
{
    int index;
    int drvidx = 0;
    const char* list;
    char str[8];

    SendMessage(hWnd, CB_RESETCONTENT, 0, 0);
    list = cdromGetDriveListIoctl();
    if (list && list[0]) {
        const char* p = list;
        while (*p) {
            sprintf(str, "%c:", *p);
            index = (int)ComboAddStringU(hWnd, str);
            SendMessage(hWnd, CB_SETITEMDATA, index, (LPARAM)*p);
            if (pProperties->diskdrive.cdromDrive == (int)*p) {
                drvidx = index;
            }
            p++;
        }
    }
    SendMessage(hWnd, CB_SETCURSEL, (WPARAM)drvidx, 0);
}

static void updateCdromListAspi(HWND hWnd, Properties* pProperties)
{
    int index;
    int drvidx = 0;
    const int* tbl = cdromGetDriveTblAspi();

    SendMessage(hWnd, CB_RESETCONTENT, 0, 0);
    if (tbl && *tbl) {
        const char* str;
        do {
            str = cdromGetDriveListAspi(*tbl);
            index = (int)ComboAddStringU(hWnd, str);
            SendMessage(hWnd, CB_SETITEMDATA, index, (LPARAM)*tbl);
            if (pProperties->diskdrive.cdromDrive == *tbl) {
                drvidx = index;
            }
            tbl++;
        } while(*tbl);
    }
    SendMessage(hWnd, CB_SETCURSEL, (WPARAM)drvidx, 0);
}

static BOOL_DLG_RET CALLBACK diskDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    HWND hMethod, hDrive;
    static Properties* pProperties;
    int index;
    INT_PTR data;
    const char* list;
    const int* tbl;
    int methodIdx[3];
    int method;

    switch (iMsg) {
    case WM_INITDIALOG:
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }
        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;
        SetWindowTextU(GetDlgItem(hDlg, IDC_CDROMGROUPBOX), langPropCdromGB());
        SetWindowTextU(GetDlgItem(hDlg, IDC_CDROMMETHODTEXT), langPropCdromMethod());
        SetWindowTextU(GetDlgItem(hDlg, IDC_CDROMDRIVETEXT), langPropCdromDrive());
        hMethod = GetDlgItem(hDlg, IDC_CDROMMETHODLIST);
        ComboAddStringU(hMethod, langPropCdromMethodNone());
        SendMessage(hMethod, CB_SETITEMDATA, 0, (LPARAM)P_CDROM_DRVNONE);

        memset(methodIdx, 0, sizeof(methodIdx));
        list = cdromGetDriveListIoctl();
        if (list && list[0]) {
            index = (int)ComboAddStringU(hMethod, langPropCdromMethodIoctl());
            SendMessage(hMethod, CB_SETITEMDATA, (WPARAM)index, (LPARAM)P_CDROM_DRVIOCTL);
            methodIdx[P_CDROM_DRVIOCTL] = index;
        }

        tbl = cdromGetDriveTblAspi();
        if (tbl && *tbl) {
            index = (int)ComboAddStringU(hMethod, langPropCdromMethodAspi());
            SendMessage(hMethod, CB_SETITEMDATA, (WPARAM)index, (LPARAM)P_CDROM_DRVASPI);
            methodIdx[P_CDROM_DRVASPI] = index;
        }

        method = pProperties->diskdrive.cdromMethod;
        index = 0;
        if (method == P_CDROM_DRVIOCTL || method == P_CDROM_DRVASPI) {
            index = methodIdx[method];
        }
        SendMessage(hMethod, CB_SETCURSEL, (WPARAM)index, 0);

        hDrive = GetDlgItem(hDlg, IDC_CDROMDRIVELIST);
        switch (method) {
        case P_CDROM_DRVIOCTL:
            updateCdromListIoctl(hDrive, pProperties);
            break;
        case P_CDROM_DRVASPI:
            updateCdromListAspi(hDrive, pProperties);
            break;
        default:
            EnableWindow(hDrive, FALSE);
        }

        if (SendMessage(hMethod, CB_GETCOUNT, 0, 0) < 2) {
            EnableWindow(hMethod, FALSE);
        }

        return FALSE;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDC_CDROMMETHODLIST:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                hMethod = (HWND)lParam;
                index = SendMessage(hMethod, CB_GETCURSEL, 0, 0);
                data  = (INT_PTR)SendMessage(hMethod, CB_GETITEMDATA, index, 0);
                hDrive = GetDlgItem(hDlg, IDC_CDROMDRIVELIST);
                EnableWindow(hDrive, data > 0);
                switch (data) {
                case P_CDROM_DRVIOCTL:
                    updateCdromListIoctl(hDrive, pProperties);
                    break;
                case P_CDROM_DRVASPI:
                    updateCdromListAspi(hDrive, pProperties);
                    break;
                }
                return TRUE;
            }
        }
        return FALSE;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        if ((((NMHDR FAR *)lParam)->code) != PSN_APPLY) {
            return FALSE;
        }

        index = SendDlgItemMessage(hDlg, IDC_CDROMMETHODLIST, CB_GETCURSEL, 0, 0);
        pProperties->diskdrive.cdromMethod = (int)SendDlgItemMessage(hDlg, IDC_CDROMMETHODLIST, CB_GETITEMDATA, index, 0);
        index = SendDlgItemMessage(hDlg, IDC_CDROMDRIVELIST, CB_GETCURSEL, 0, 0);
        pProperties->diskdrive.cdromDrive = (int)SendDlgItemMessage(hDlg, IDC_CDROMDRIVELIST, CB_GETITEMDATA, index, 0);

        propModified = 1;

        return TRUE;
    }

    return FALSE;
}


//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////


static void getPortsLptList(HWND hDlg, int id, Properties* pProperties) {
    int idx = SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);

    if (idx < P_LPT_HOST) {
        pProperties->ports.Lpt.type = idx;
        return;
    }

    /* Combo holds wide PrinterPort entries (CB_GETLBTEXT returns wide).
    ** Decode to UTF-8 and split "PortName - PrinterName". */
    wchar_t wbuf[MAX_PATH];
    SendDlgItemMessageW(hDlg, id, CB_GETLBTEXT, idx, (LPARAM)wbuf);
    char buffer[MAX_PATH * 4];
    WideToUtf8(wbuf, buffer, sizeof(buffer));

    /* Split at " - " separator. */
    char* sep = strstr(buffer, " - ");
    if (sep == NULL) {
        /* Malformed entry; treat as port-only. */
        strncpy(pProperties->ports.Lpt.portName, buffer, PROP_MAXPATH - 1);
        pProperties->ports.Lpt.portName[PROP_MAXPATH - 1] = 0;
        pProperties->ports.Lpt.name[0] = 0;
    } else {
        *sep = 0;
        const char* prnName = sep + 3;
        strncpy(pProperties->ports.Lpt.portName, buffer, PROP_MAXPATH - 1);
        pProperties->ports.Lpt.portName[PROP_MAXPATH - 1] = 0;
        strncpy(pProperties->ports.Lpt.name, prnName, sizeof(pProperties->ports.Lpt.name) - 1);
        pProperties->ports.Lpt.name[sizeof(pProperties->ports.Lpt.name) - 1] = 0;
    }
    pProperties->ports.Lpt.type = P_LPT_HOST;
}

static void getPortsLptEmulList(HWND hDlg, int id, Properties* pProperties) 
{
    int idx = SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);

    if (idx >= 0) {
        pProperties->ports.Lpt.emulation = idx;
    }
}

static BOOL updatePortsLptEmulList(HWND hDlg, int id, Properties* pProperties)
{
    while (CB_ERR != SendDlgItemMessage(hDlg, id, CB_DELETESTRING, 0, 0));

    ComboAddStringU(GetDlgItem(hDlg, id), langPropPortsNone());
    
    // Add MSX Printer
    ComboAddStringU(GetDlgItem(hDlg, id), "MSX Printer");
    
    // Add SVI Printer
    ComboAddStringU(GetDlgItem(hDlg, id), "SVI Printer");
    
    // Add Epson FX-80
    ComboAddStringU(GetDlgItem(hDlg, id), "Epson FX-80");

    SendDlgItemMessage(hDlg, id, CB_SETCURSEL, pProperties->ports.Lpt.emulation, 0);

    return TRUE;
}

static BOOL updatePortsLptList(HWND hDlg, int id, Properties* pProperties)
{
    /* EnumPrintersW + PRINTER_INFO_2W: pPrinterName / pPortName carry locale
    ** text on non-ASCII systems.  The ANSI variant returned ACP bytes
    ** which mojibake'd through ComboAddStringU. */
    PRINTER_INFO_2W* lpPrinterInfo = NULL;
    DWORD dwNeeded;
    DWORD dwReturned;
    DWORD dwItem;

    while (CB_ERR != SendDlgItemMessage(hDlg, id, CB_DELETESTRING, 0, 0));

    // Get buffer size
    EnumPrintersW(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS, NULL, 2, NULL, 0, &dwNeeded, &dwReturned);

    // Allocate memory
    lpPrinterInfo = (PRINTER_INFO_2W*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwNeeded);
    if (lpPrinterInfo == NULL)
        return FALSE;

    if (!EnumPrintersW(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS, NULL, 2, (LPBYTE)lpPrinterInfo, dwNeeded, &dwNeeded, &dwReturned)) {
        HeapFree(GetProcessHeap(), 0, lpPrinterInfo);
        return FALSE;
    }

    // Add NONE:
    ComboAddStringU(GetDlgItem(hDlg, id), langPropPortsNone());
    SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 0, 0); // Set as default

    // Add SiMPL/COVOX
    ComboAddStringU(GetDlgItem(hDlg, id), langPropPortsSimplCovox());
    if (pProperties->ports.Lpt.type == P_LPT_SIMPL) 
        SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 1, 0);

    // Add FILE
    ComboAddStringU(GetDlgItem(hDlg, id), langPropPortsFile());
    if (pProperties->ports.Lpt.type == P_LPT_FILE) 
        SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 2, 0);

    // Add printers 
    for (dwItem = 0; dwItem < dwReturned; dwItem++) {
        char utf8PortName[MAX_PATH];
        char utf8PrinterName[MAX_PATH];
        char sBuf[MAX_PATH * 2];
        WideToUtf8(lpPrinterInfo[dwItem].pPortName, utf8PortName, sizeof(utf8PortName));
        WideToUtf8(lpPrinterInfo[dwItem].pPrinterName, utf8PrinterName, sizeof(utf8PrinterName));
        snprintf(sBuf, sizeof(sBuf), "%s - %s", utf8PortName, utf8PrinterName);
        ComboAddStringU(GetDlgItem(hDlg, id), sBuf);
        if (pProperties->ports.Lpt.type == P_LPT_HOST &&
            0 == strcmp(pProperties->ports.Lpt.name, utf8PrinterName))
            SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 3 + dwItem, 0);
    }

    // Free memory
    HeapFree(GetProcessHeap(), 0, lpPrinterInfo);

    return TRUE;
}

static BOOL IsNumeric(LPCTSTR pszString, BOOL bIgnoreColon)
{
    BOOL bNumeric = TRUE;
    size_t cch;
    unsigned int i;

    if (!SUCCEEDED(StringCchLength(pszString, MAX_PATH-1, &cch)))
        return FALSE;
    if (cch == 0)
        return FALSE;

    for (i=0; i<cch && bNumeric; i++) {
        bNumeric = (isdigit(pszString[i]) != 0);
        if (bIgnoreColon && (pszString[i] == ':'))
            bNumeric = TRUE;
    }
    return bNumeric;
}

static void getPortsComList(HWND hDlg, int id, Properties* pProperties) {
    int idx = SendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0);

    if (idx < P_COM_HOST) {
        pProperties->ports.Com.type = idx;
        return;
    }

    /* Combo holds wide COM entries (CB_GETLBTEXT returns wide). Decode
    ** to UTF-8 and split "COM1 - Description"; only the port name is used. */
    wchar_t wbuf[MAX_PATH];
    SendDlgItemMessageW(hDlg, id, CB_GETLBTEXT, idx, (LPARAM)wbuf);
    char buffer[MAX_PATH * 4];
    WideToUtf8(wbuf, buffer, sizeof(buffer));

    char* p = buffer;
    while (*p && *p != ' ' && *p != '-') p++;
    *p = 0;

    strncpy(pProperties->ports.Com.portName, buffer, PROP_MAXPATH - 1);
    pProperties->ports.Com.portName[PROP_MAXPATH - 1] = 0;
    strncpy(pProperties->ports.Com.name, buffer, sizeof(pProperties->ports.Com.name) - 1);
    pProperties->ports.Com.name[sizeof(pProperties->ports.Com.name) - 1] = 0;
    pProperties->ports.Com.type = P_COM_HOST;
}

static BOOL updatePortsComList(HWND hDlg, int id, Properties* pProperties)
{
    /* EnumPortsW + PORT_INFO_2W: read pDescription as wide text and
    ** convert to UTF-8 (ANSI variant returned ACP, mojibake'd combo). */
    PORT_INFO_2W* lpPortInfo = NULL;
    DWORD dwNeeded;
    DWORD dwReturned;
    DWORD dwItem;

    while (CB_ERR != SendDlgItemMessage(hDlg, id, CB_DELETESTRING, 0, 0));

    // Get buffer size
    EnumPortsW(NULL, 2, NULL, 0, &dwNeeded, &dwReturned);

    // Allocate memory
    lpPortInfo = (PORT_INFO_2W*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwNeeded);
    if (lpPortInfo == NULL)
        return FALSE;

    if (!EnumPortsW(NULL, 2, (LPBYTE)lpPortInfo, dwNeeded, &dwNeeded, &dwReturned)) {
        HeapFree(GetProcessHeap(), 0, lpPortInfo);
        return FALSE;
    }

    // Add NONE:
    ComboAddStringU(GetDlgItem(hDlg, id), langPropPortsNone());
    SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 0, 0); // Set as default

    // Add FILE
    ComboAddStringU(GetDlgItem(hDlg, id), langPropPortsComFile());
    if (pProperties->ports.Com.type == P_COM_FILE) 
        SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 1, 0);


    // Add COM ports 
    for (dwItem = 0; dwItem < dwReturned; dwItem++) {
        char  utf8PortName[64];
        char  utf8Desc[MAX_PATH];
        char  sBuf[MAX_PATH];
        WideToUtf8(lpPortInfo[dwItem].pPortName, utf8PortName, sizeof(utf8PortName));
        if (strlen(utf8PortName) <= 3) continue;
        if (strncmp(utf8PortName, "COM", 3) != 0) continue;
        if (!IsNumeric(&utf8PortName[3], TRUE)) continue;

        WideToUtf8(lpPortInfo[dwItem].pDescription, utf8Desc, sizeof(utf8Desc));
        snprintf(sBuf, sizeof(sBuf), "%s - %s", utf8PortName, utf8Desc);
        ComboAddStringU(GetDlgItem(hDlg, id), sBuf);
        if (pProperties->ports.Com.type == P_COM_HOST &&
            0 == strcmp(pProperties->ports.Com.name, utf8PortName))
            SendDlgItemMessage(hDlg, id, CB_SETCURSEL, 2 + dwItem, 0);
    }

    // Free memory
    HeapFree(GetProcessHeap(), 0, lpPortInfo);

    return TRUE;
}


static BOOL_DLG_RET CALLBACK portsDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static Properties* pProperties;
    HWND hMethod, hDrive;
    int index;
    INT_PTR data;
    const char* list;
    const int* tbl;
    int methodIdx[3];
    int method;

    switch (iMsg) {
    case WM_INITDIALOG:
        if (!centered) {
            updateDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES, 0, 1);
            centered = 1;
        }

        SetDlgItemTextU(hDlg, IDC_PORTSLPTGROUPBOX, langPropPortsLptGB());
        SetDlgItemTextU(hDlg, IDC_PORTSCOMGROUPBOX, langPropPortsComGB());
        SetDlgItemTextU(hDlg, IDC_PORTSLPTTEXT, langPropPortsLptText());
        SetDlgItemTextU(hDlg, IDC_PORTSCOM1TEXT, langPropPortsCom1Text());
        SetWindowTextU(GetDlgItem(GetParent(hDlg), IDOK), langDlgOK());
        SetWindowTextU(GetDlgItem(GetParent(hDlg), IDCANCEL), langDlgCancel());
        SetWindowTextU(GetDlgItem(hDlg, IDC_LPTFILENAMETEXT), langTextFilename());
        SetWindowTextU(GetDlgItem(hDlg, IDC_COM1FILENAMETEXT), langTextFilename());
        SetWindowTextU(GetDlgItem(hDlg, IDC_LPTEMULATIONTEXT), langPropPortsEmulateMsxPrn());

        pProperties = (Properties*)((PROPSHEETPAGE*)lParam)->lParam;

        updatePortsLptList(hDlg, IDC_PORTSLPT, pProperties);
        updatePortsComList(hDlg, IDC_PORTSCOM1, pProperties);

        updatePortsLptEmulList(hDlg, IDC_LPTEMULATION, pProperties);

        {
            int idx = SendDlgItemMessage(hDlg, IDC_PORTSLPT, CB_GETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_LPTFILENAMEBROWSE), idx == P_LPT_FILE);
            EnableWindow(GetDlgItem(hDlg, IDC_LPTFILENAME), idx == P_LPT_FILE);
            EnableWindow(GetDlgItem(hDlg, IDC_LPTEMULATION), idx >= P_LPT_HOST);

            idx = SendDlgItemMessage(hDlg, IDC_PORTSCOM1, CB_GETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_COM1FILENAMEBROWSE), idx == P_COM_FILE);
            EnableWindow(GetDlgItem(hDlg, IDC_COM1FILENAME), idx == P_COM_FILE);
        }

        SetWindowTextU(GetDlgItem(hDlg, IDC_LPTFILENAME), pProperties->ports.Lpt.fileName);
        SetWindowTextU(GetDlgItem(hDlg, IDC_COM1FILENAME), pProperties->ports.Com.fileName);

        
        SetWindowTextU(GetDlgItem(hDlg, IDC_CDROMGROUPBOX), langPropCdromGB());
        SetWindowTextU(GetDlgItem(hDlg, IDC_CDROMMETHODTEXT), langPropCdromMethod());
        SetWindowTextU(GetDlgItem(hDlg, IDC_CDROMDRIVETEXT), langPropCdromDrive());
        hMethod = GetDlgItem(hDlg, IDC_CDROMMETHODLIST);
        ComboAddStringU(hMethod, langPropCdromMethodNone());
        SendMessage(hMethod, CB_SETITEMDATA, 0, (LPARAM)P_CDROM_DRVNONE);

        memset(methodIdx, 0, sizeof(methodIdx));
        list = cdromGetDriveListIoctl();
        if (list && list[0]) {
            index = (int)ComboAddStringU(hMethod, langPropCdromMethodIoctl());
            SendMessage(hMethod, CB_SETITEMDATA, (WPARAM)index, (LPARAM)P_CDROM_DRVIOCTL);
            methodIdx[P_CDROM_DRVIOCTL] = index;
        }

        tbl = cdromGetDriveTblAspi();
        if (tbl && *tbl) {
            index = (int)ComboAddStringU(hMethod, langPropCdromMethodAspi());
            SendMessage(hMethod, CB_SETITEMDATA, (WPARAM)index, (LPARAM)P_CDROM_DRVASPI);
            methodIdx[P_CDROM_DRVASPI] = index;
        }

        method = pProperties->diskdrive.cdromMethod;
        index = 0;
        if (method == P_CDROM_DRVIOCTL || method == P_CDROM_DRVASPI) {
            index = methodIdx[method];
        }
        SendMessage(hMethod, CB_SETCURSEL, (WPARAM)index, 0);

        hDrive = GetDlgItem(hDlg, IDC_CDROMDRIVELIST);
        switch (method) {
        case P_CDROM_DRVIOCTL:
            updateCdromListIoctl(hDrive, pProperties);
            break;
        case P_CDROM_DRVASPI:
            updateCdromListAspi(hDrive, pProperties);
            break;
        default:
            EnableWindow(hDrive, FALSE);
        }

        if (SendMessage(hMethod, CB_GETCOUNT, 0, 0) < 2) {
            EnableWindow(hMethod, FALSE);
        }

        return FALSE;
        
    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDC_PORTSLPT:
            {
                int idx = SendDlgItemMessage(hDlg, IDC_PORTSLPT, CB_GETCURSEL, 0, 0);
                EnableWindow(GetDlgItem(hDlg, IDC_LPTFILENAMEBROWSE), idx == P_LPT_FILE);
                EnableWindow(GetDlgItem(hDlg, IDC_LPTFILENAME), idx == P_LPT_FILE);
                EnableWindow(GetDlgItem(hDlg, IDC_LPTEMULATION), idx >= P_LPT_HOST);
            }
            return TRUE;

        case IDC_PORTSCOM1:
            {
                int idx = SendDlgItemMessage(hDlg, IDC_PORTSCOM1, CB_GETCURSEL, 0, 0);
                EnableWindow(GetDlgItem(hDlg, IDC_COM1FILENAMEBROWSE), idx == P_COM_FILE);
                EnableWindow(GetDlgItem(hDlg, IDC_COM1FILENAME), idx == P_COM_FILE);
            }
            return TRUE;

        case IDC_LPTFILENAMEBROWSE:
            if (openLogFile(hDlg, pProperties->ports.Lpt.fileName)) {
                SetWindowTextU(GetDlgItem(hDlg, IDC_LPTFILENAME), pProperties->ports.Lpt.fileName);
            }
            return TRUE;

        case IDC_COM1FILENAMEBROWSE:
            if (openLogFile(hDlg, pProperties->ports.Com.fileName)) {
                SetWindowTextU(GetDlgItem(hDlg, IDC_COM1FILENAMEBROWSE), pProperties->ports.Com.fileName);
            }
            return TRUE;
        case IDC_CDROMMETHODLIST:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                hMethod = (HWND)lParam;
                index = SendMessage(hMethod, CB_GETCURSEL, 0, 0);
                data  = (INT_PTR)SendMessage(hMethod, CB_GETITEMDATA, index, 0);
                hDrive = GetDlgItem(hDlg, IDC_CDROMDRIVELIST);
                EnableWindow(hDrive, data > 0);
                switch (data) {
                case P_CDROM_DRVIOCTL:
                    updateCdromListIoctl(hDrive, pProperties);
                    break;
                case P_CDROM_DRVASPI:
                    updateCdromListAspi(hDrive, pProperties);
                    break;
                }
                return TRUE;
            }
        }
        return FALSE;

    case WM_NOTIFY:
        if (((NMHDR FAR*)lParam)->code == PSN_APPLY || ((NMHDR FAR*)lParam)->code == PSN_QUERYCANCEL) {
            saveDialogPos(GetParent(hDlg), DLG_ID_PROPERTIES);
        }

        if ((((NMHDR FAR *)lParam)->code) != PSN_APPLY) {
            return FALSE;
        }

        getPortsLptList(hDlg, IDC_PORTSLPT, pProperties);
        getPortsLptEmulList(hDlg, IDC_LPTEMULATION, pProperties);
        GetWindowTextU(GetDlgItem(hDlg, IDC_LPTFILENAME), pProperties->ports.Lpt.fileName, MAX_PATH - 1);

        getPortsComList(hDlg, IDC_PORTSCOM1, pProperties);
        GetWindowTextU(GetDlgItem(hDlg, IDC_COM1FILENAME), pProperties->ports.Com.fileName, MAX_PATH - 1);

        index = SendDlgItemMessage(hDlg, IDC_CDROMMETHODLIST, CB_GETCURSEL, 0, 0);
        pProperties->diskdrive.cdromMethod = (int)SendDlgItemMessage(hDlg, IDC_CDROMMETHODLIST, CB_GETITEMDATA, index, 0);
        index = SendDlgItemMessage(hDlg, IDC_CDROMDRIVELIST, CB_GETCURSEL, 0, 0);
        pProperties->diskdrive.cdromDrive = (int)SendDlgItemMessage(hDlg, IDC_CDROMDRIVELIST, CB_GETITEMDATA, index, 0);

        return TRUE;
    }

    return FALSE;
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

int showProperties(Properties* pProperties, HWND hwndOwner, PropPage desiredStartPage, Mixer* mixer, Video* video) {
	HINSTANCE       hInst = (HINSTANCE)GetModuleHandle(NULL);
    PROPSHEETPAGEW   psp[9];
    PROPSHEETHEADERW psh;
    wchar_t          wTitle[9][64];
    wchar_t          wCaption[128];
    Properties oldProp = *pProperties;
    UINT startPage = -1;
    UINT curPage = 0;

    centered = 0;
    hDlgSound = NULL;
    theMixer = mixer;
    theVideo = video;
    
    /* Init language specific dropdown list data */
    sprintf(pVideoMon[0], "%s", langEnumVideoMonColor());
    sprintf(pVideoMon[1], "%s", langEnumVideoMonGrey());
    sprintf(pVideoMon[2], "%s", langEnumVideoMonGreen());
    sprintf(pVideoMon[3], "%s", langEnumVideoMonAmber());

    sprintf(pVideoVideoType[0], "%s", langEnumVideoTypePAL());
    sprintf(pVideoVideoType[1], "%s", langEnumVideoTypeNTSC());

    sprintf(pVideoPalEmu[0], "%s", langEnumVideoEmuNone());
    sprintf(pVideoPalEmu[1], "%s", langEnumVideoEmuMonitor());
    sprintf(pVideoPalEmu[2], "%s", langEnumVideoEmuYc());
    sprintf(pVideoPalEmu[3], "%s", langEnumVideoEmuYcBlur());
    sprintf(pVideoPalEmu[4], "%s", langEnumVideoEmuComp());
    sprintf(pVideoPalEmu[5], "%s", langEnumVideoEmuCompBlur());
    sprintf(pVideoPalEmu[6], "%s", langEnumVideoEmuScale2x());
    sprintf(pVideoPalEmu[7], "%s", langEnumVideoEmuHq2x());

    sprintf(pVideoMonSize[0], "%s", langEnumVideoSize1x());
    sprintf(pVideoMonSize[1], "%s", langEnumVideoSize2x());
    sprintf(pVideoMonSize[2], "%s", langEnumVideoSizeFullscreen());

    sprintf(pVideoDriver[0], "%s", langEnumVideoDrvDirectDrawHW());
    sprintf(pVideoDriver[1], "%s", langEnumVideoDrvDirectDraw());
    sprintf(pVideoDriver[2], "%s", langEnumVideoDrvGDI());
    sprintf(pVideoDriver[3], "Direct3D 12");

    sprintf(pVideoFrameSkip[0], "%s", langEnumVideoFrameskip0());
    sprintf(pVideoFrameSkip[1], "%s", langEnumVideoFrameskip1());
    sprintf(pVideoFrameSkip[2], "%s", langEnumVideoFrameskip2());
    sprintf(pVideoFrameSkip[3], "%s", langEnumVideoFrameskip3());
    sprintf(pVideoFrameSkip[4], "%s", langEnumVideoFrameskip4());
    sprintf(pVideoFrameSkip[5], "%s", langEnumVideoFrameskip5());

    sprintf(pSoundDriver[0], "%s", langEnumSoundDrvNone());
    sprintf(pSoundDriver[1], "%s", langEnumSoundDrvWMM());
    sprintf(pSoundDriver[2], "%s", langEnumSoundDrvDirectX());

    sprintf(pEmuSync[0], "%s", langEnumEmuSyncNone());
    sprintf(pEmuSync[1], "%s", langEnumEmuSyncAuto());
    sprintf(pEmuSync[2], "%s", langEnumEmuSync1ms());
    sprintf(pEmuSync[3], "%s", langEnumEmuSyncVblank());
    sprintf(pEmuSync[4], "%s", langEnumEmuAsyncVblank());


    if (appConfigGetInt("properties.emulation", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_EMULATION);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = emulationDlgProc;
        Utf8ToWide(langPropEmulation(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_EMULATION || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }

    if (appConfigGetInt("properties.performance", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_PERFORMANCE);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = performanceDlgProc;
        Utf8ToWide(langPropVideo(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_PERFORMANCE || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }

    if (appConfigGetInt("properties.video", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_VIDEO);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = videoDlgProc;
        Utf8ToWide(langPropEffects(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_VIDEO || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }

    if (appConfigGetInt("properties.sound", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_SOUND);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = soundDlgProc;
        Utf8ToWide(langPropSound(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_SOUND || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }

    if (appConfigGetInt("properties.ports", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_PORTS);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = portsDlgProc;
        Utf8ToWide(langPropPorts(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_PORTS || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }

    if (appConfigGetInt("properties.settings", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = filesDlgProc;
        Utf8ToWide(langPropFile(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_SETTINGS || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }

#if 0
    if (appConfigGetInt("properties.disk", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_DISKEMU);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = diskDlgProc;
        Utf8ToWide(langPropDisk(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_DISK || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }
#endif

    if (appConfigGetInt("properties.appearance", 1) != 0) {
        psp[curPage].dwSize = sizeof(PROPSHEETPAGEW);
        psp[curPage].dwFlags = PSP_USEICONID | PSP_USETITLE;
        psp[curPage].hInstance = hInst;
        psp[curPage].pszTemplate = MAKEINTRESOURCE(IDD_APEARANCE);
        psp[curPage].pszIcon = NULL;
        psp[curPage].pfnDlgProc = settingsDlgProc;
        Utf8ToWide(langPropSettings(), wTitle[curPage], _countof(wTitle[curPage]));
        psp[curPage].pszTitle = wTitle[curPage];
        psp[curPage].lParam = (LPARAM)pProperties;
        psp[curPage].pfnCallback = NULL;
        if (desiredStartPage == PROP_APEARANCE || startPage == -1) {
            startPage = curPage;
        }
        curPage++;
    }

    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_USEICONID | PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
    psh.hwndParent = hwndOwner;
    psh.hInstance = hInst;
    psh.pszIcon = NULL;
    Utf8ToWide(langPropTitle(), wCaption, _countof(wCaption));
    psh.pszCaption = wCaption;
    psh.nPages = curPage;
    psh.nStartPage = startPage;
    psh.ppsp = (LPCPROPSHEETPAGEW) &psp;
    psh.pfnCallback = NULL;

    propModified = 0;

    PropertySheetW(&psh);

    if (propModified) {
        propModified = memcmp(&oldProp, pProperties, sizeof(Properties));
    }

    return propModified;
}
