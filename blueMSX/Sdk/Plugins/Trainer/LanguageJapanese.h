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
        trainerName             = "トレーナー";
        trainerCaption          = "blueMSX+ - トレーナー";
        saveCheatCaption        = "チートを保存";
        loadCheatCaption        = "チートを読み込み";
        pause                   = "一時停止";
        run                     = "実行";
        truncated               = "省略...";
        newCheat                = "新規チート";
        address                 = "アドレス";
        oldValue                = "旧値";
        newValue                = "新値";
        change                  = "変化";
        disable                 = "無効化";
        enable                  = "有効化";
        description             = "説明";
        value                   = "値";
        activeCheats            = "有効チート ";
        findCheats              = "チート探索 ";
        removeAll               = "すべて削除";
        remove                  = "削除";
        addCheat                = "チートを追加";
        cheatFile               = "チートファイル:";
        ok                      = "OK";
        cancel                  = "キャンセル";
        displayValueAs          = "値の表示形式 ";
        decimal                 = "10進";
        hexadecimal             = "16進";
        dataSize                = "データサイズ ";
        eightBit                = "8 ビット";
        sixteenBit              = "16 ビット";
        compareType             = "比較条件 ";
        equal                   = "等しい";
        notEqual                = "等しくない";
        lessThan                = "より小さい";
        lessOrEqual             = "以下";
        greaterThan             = "より大きい";
        greaterOrEqual          = "以上";
        display                 = "表示 ";
        compareNewValueWith     = "新値の比較対象 ";
        specificValue           = "特定の値: ";
        snapshot                = "スナップショット";
        search                  = "検索";
        undo                    = "元に戻す";
    }
};

#endif //LANGUAGE_JAPANESE_H
