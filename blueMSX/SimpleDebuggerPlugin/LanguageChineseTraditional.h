/*****************************************************************************
** File:
**      LanguageChineseTraditional.h
**
** Copyright (C) 2003-2004 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
******************************************************************************
*/
#ifndef LANGUAGE_CHINESE_TRADITIONAL_H
#define LANGUAGE_CHINESE_TRADITIONAL_H

#include "LanguageEnglish.h"

class LanguageChineseTraditional : public LanguageEnglish
{
public:
    LanguageChineseTraditional() {
        genericOk                   = "確定";
        genericCancel               = "取消";

        toolbarResume               = "開始/繼續";
        toolbarPause                = "全部中斷";
        toolbarStop                 = "停止偵錯";
        toolbarRun                  = "重新啟動";
        toolbarShowNext             = "顯示下一個陳述式";
        toolbarStepIn               = "逐步執行";
        toolbarStepOver             = "不進入函式";
        toolbarStepOut              = "跳離函式";
        toolbarRunTo                = "執行到指標";
        toolbarBpToggle             = "設定/移除中斷點";
        toolbarBpEnable             = "啟用/停用中斷點";
        toolbarBpEnableAll          = "啟用所有中斷點";
        toolbarBpDisableAll         = "停用所有中斷點";
        toolbarBpRemoveAll          = "移除所有中斷點";

        menuFile                    = "檔案";
        menuFileLoadSymbolFile      = "載入符號檔案";
        menuFileSaveDisassembly     = "儲存反組譯";
        menuFileSaveMemory          = "儲存記憶體";
        menuFileExit                = "結束";

        menuDebug                   = "偵錯";
        menuDebugStart              = "開始";
        menuDebugContinue           = "繼續";
        menuDebugBreakAll           = "全部中斷";
        menuDebugStop               = "停止偵錯";
        menuDebugRestart            = "重新啟動";
        menuDebugStepIn             = "逐步執行";
        menuDebugStepOver           = "不進入函式";
        menuDebugStepOut            = "跳離函式";
        menuDebugRunTo              = "執行到指標";
        menuDebugShowSymbols        = "顯示符號資訊";
        menuDebugGoto               = "前往";
        menuDebugFind               = "尋找";
        menuDebugBpAdd              = "加入新的中斷點";
        menuDebugBpToggle           = "設定/移除中斷點";
        menuDebugEnable             = "啟用/停用中斷點";
        menuDebugRemoveAll          = "移除所有中斷點";
        menuDebugEnableAll          = "啟用所有中斷點";
        menuDebugDisableAll         = "停用所有中斷點";
        menuDebugShowAssemblyFlags  = "顯示組合旗標";
        menuDebugFastVram           = "Break on too fast VRAM access";

        menuWindow                  = "視窗";

        menuHelp                    = "說明";
        menuHelpAbout               = "關於";

        debuggerName                = "偵錯工具";
        windowDebugger              = "blueMSX+ - 偵錯工具";
        windowDisassembly           = "反組譯碼";
        windowDisassemblyUnavail    = "反組譯碼不可使用。";
        windowCpuRegisters          = "CPU 暫存器";
        windowCpuRegistersFlags     = "旗標";
        windowStack                 = "堆疊";
        windowStackUnavail          = "堆疊不可使用。";
        windowCallstack             = "呼叫堆疊";
        windowCallstackUnavail      = "呼叫堆疊不可使用。";
        windowMemory                = "記憶體";
        windowPeripheralRegisters   = "周邊暫存器";
        windowIoPorts               = "I/O 連接埠";

        memWindowRegisters          = "暫存器:";

        memWindowMemory             = "記憶體:";
        memWindowAddress            = "位址:";

        setBpWindowCaption          = "加入新的中斷點";
        gotoWindowCaption           = "前往位址";
        gotoWindowText              = "位址或標籤:";

        symbolWindowCaption         = "開啟符號檔案";
        symbolWindowText            = "取代舊的符號";

        popupOverwrite              = "檔案已經存在。 您確定要覆寫它嗎？";
        
        aboutBuilt                  = "組建:";
        aboutVisit                  = "請訪問 http://www.bluemsx.com 取得更多細節";
    }
};


#endif //LANGUAGE_CHINESE_TRADITIONAL_H
