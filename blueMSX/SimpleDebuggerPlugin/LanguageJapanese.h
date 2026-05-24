/*****************************************************************************
** File:
**      LanguageJapanese.h
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
#ifndef LANGUAGE_JAPANESE_H
#define LANGUAGE_JAPANESE_H

#include "LanguageEnglish.h"

class LanguageJapanese : public LanguageEnglish
{
public:
    LanguageJapanese() {
        genericOk                   = "OK";
        genericCancel               = "キャンセル";

        toolbarResume               = "開始/実行";
        toolbarPause                = "全て停止";
        toolbarStop                 = "デバッグ中止";
        toolbarRun                  = "リスタート";
        toolbarShowNext             = "次のステートメント表示";
        toolbarStepIn               = "ステップ イン";
        toolbarStepOver             = "ステップ オーバー";
        toolbarStepOut              = "ステップ アウト";
        toolbarRunTo                = "カーソルまで実行";
        toolbarBpToggle             = "ブレイクポイント/除去";
        toolbarBpEnable             = "ブレイクポイント有/無";
        toolbarBpEnableAll          = "ブレイクポイント全有効化";
        toolbarBpDisableAll         = "ブレイクポイント全無効化";
        toolbarBpRemoveAll          = "ブレイクポイント全消去";

        menuFile                    = "ファイル";
        menuFileLoadSymbolFile      = "シンボルファイルのロード";
        menuFileSaveDisassembly     = "Save Disassembly";
        menuFileSaveMemory          = "Save Memory";
        menuFileExit                = "終了";

        menuDebug                   = "デバッグ";
        menuDebugStart              = "開始";
        menuDebugContinue           = "実行";
        menuDebugBreakAll           = "全て停止";
        menuDebugStop               = "デバッグ中止";
        menuDebugRestart            = "リスタート";
        menuDebugStepIn             = "ステップ イン";
        menuDebugStepOver           = "ステップ オーバー";
        menuDebugStepOut            = "ステップ アウト";
        menuDebugRunTo              = "カーソルまで実行";
        menuDebugShowSymbols        = "シンボル情報表示";
        menuDebugGoto               = "移動";
        menuDebugBpAdd              = "ブレイクポイントを追加";
        menuDebugBpToggle           = "ブレイクポイントのセット/消去";
        menuDebugEnable             = "ブレイクポイント有/無";
        menuDebugRemoveAll          = "ブレイクポイント全消去";
        menuDebugEnableAll          = "ブレイクポイント全有効化";
        menuDebugDisableAll         = "ブレイクポイント全無効化";
        menuDebugShowAssemblyFlags  = "アセンブリフラッグ表示";
        menuDebugFastVram           = "Break on too fast VRAM access";

        menuWindow                  = "ウィンドウ";

        menuHelp                    = "ヘルプ";
        menuHelpAbout               = "デバッガについて";

        debuggerName                = "デバッガ";
        windowDebugger              = "blueMSX - デバッガ";
        windowDisassembly           = "逆アセンブル";
        windowDisassemblyUnavail    = "Disassembly unavailable.";
        windowCpuRegisters          = "CPU レジスタ";
        windowCpuRegistersFlags     = "Flags";
        windowStack                 = "スタック";
        windowStackUnavail          = "Stack unavailable.";
        windowCallstack             = "コールスタック";
        windowCallstackUnavail      = "Callstack unavailable.";
        windowMemory                = "メモリ";
        windowPeripheralRegisters   = "Peripheral Registers";
        windowIoPorts               = "I/O Ports";

        memWindowRegisters          = "Registers:";

        memWindowMemory             = "メモリ:";
        memWindowAddress            = "アドレス:";

        setBpWindowCaption          = "ブレイクポイントを追加";
        gotoWindowCaption           = "アドレスに移動";
        gotoWindowText              = "アドレスまたはラベル:";

        symbolWindowCaption         = "シンボルファイルのロード";
        symbolWindowText            = "シンボルの入れ替え";
        
        popupOverwrite              = "File already exists. Do you want to overwrite it?";
        
        aboutBuilt                  = "ビルド:";
        aboutVisit                  = "詳しい情報は http://www.bluemsx.com まで";
    }
};

#endif //LANGUAGE_JAPANESE_H
