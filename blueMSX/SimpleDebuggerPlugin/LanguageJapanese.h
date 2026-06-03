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
        toolbarStepBack             = "ステップ バック";
        toolbarStepOver             = "ステップ オーバー";
        toolbarStepOut              = "ステップ アウト";
        toolbarRunTo                = "カーソルまで実行";
        toolbarBpToggle             = "ブレークポイント設定/除去";
        toolbarBpEnable             = "ブレークポイント有効/無効";
        toolbarBpEnableAll          = "ブレークポイント全有効化";
        toolbarBpDisableAll         = "ブレークポイント全無効化";
        toolbarBpRemoveAll          = "ブレークポイント全消去";

        menuFile                    = "ファイル";
        menuFileLoadSymbolFile      = "シンボルファイルのロード";
        menuFileSaveDisassembly     = "逆アセンブルを保存";
        menuFileSaveMemory          = "メモリを保存";
        menuFileExit                = "終了";

        menuDebug                   = "デバッグ";
        menuDebugStart              = "開始";
        menuDebugContinue           = "実行";
        menuDebugBreakAll           = "全て停止";
        menuDebugStop               = "デバッグ中止";
        menuDebugRestart            = "リスタート";
        menuDebugStepIn             = "ステップ イン";
        menuDebugStepBack           = "ステップ バック";
        menuDebugStepOver           = "ステップ オーバー";
        menuDebugStepOut            = "ステップ アウト";
        menuDebugRunTo              = "カーソルまで実行";
        menuDebugShowSymbols        = "シンボル情報表示";
        menuDebugGoto               = "移動";
        menuDebugFind               = "検索";
        menuDebugBpAdd              = "ブレークポイントを追加";
        menuDebugWpAdd              = "ウォッチポイントを追加";
        menuDebugBpToggle           = "ブレークポイントのセット/消去";
        menuDebugEnable             = "ブレークポイント有効/無効";
        menuDebugRemoveAll          = "ブレークポイント全消去";
        menuDebugEnableAll          = "ブレークポイント全有効化";
        menuDebugDisableAll         = "ブレークポイント全無効化";
        menuDebugShowAssemblyFlags  = "アセンブリフラグ表示";
        menuDebugFastVram           = "速過ぎる VRAM アクセス時にブレーク";

        menuWindow                  = "ウィンドウ";

        menuHelp                    = "ヘルプ";
        menuHelpAbout               = "デバッガについて";

        debuggerName                = "デバッガ";
        windowDebugger              = "blueMSX+ - デバッガ";
        windowDisassembly           = "逆アセンブル";
        windowDisassemblyUnavail    = "逆アセンブル: 利用不可";
        windowCpuRegisters          = "CPU レジスタ";
        windowCpuRegistersFlags     = "フラグ";
        windowStack                 = "スタック";
        windowStackUnavail          = "スタック: 利用不可";
        windowCallstack             = "コールスタック";
        windowBreakpoints           = "ブレークポイント";
        windowCallstackUnavail      = "コールスタック: 利用不可";
        windowMemory                = "メモリ";
        windowPeripheralRegisters   = "周辺レジスタ";
        windowIoPorts               = "I/O ポート";

        memWindowRegisters          = "レジスタ:";

        memWindowMemory             = "メモリ:";
        memWindowAddress            = "アドレス:";

        setBpWindowCaption          = "ブレークポイントを追加";
        setWpWindowCaption          = "ウォッチポイントを追加";
        gotoWindowCaption           = "アドレスに移動";
        gotoWindowText              = "アドレスまたはラベル:";
        findWindowCaption           = "検索";
        findWindowText              = "文字列または値:";

        symbolWindowCaption         = "シンボルファイルを開く";
        symbolWindowText            = "既存シンボルを置き換える";
        
        popupOverwrite              = "ファイルが既に存在します。上書きしますか?";

        statusRunning               = "実行中";
        statusPaused                = "一時停止";
        statusStopped               = "停止";
        
        aboutBuilt                  = "ビルド:";
        aboutVisit                  = "詳しい情報は http://www.bluemsx.com まで";
    }
};

#endif //LANGUAGE_JAPANESE_H
