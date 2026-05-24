/*****************************************************************************
** File:
**      LanguageKorean.h
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
#ifndef LANGUAGE_KOREAN_H
#define LANGUAGE_KOREAN_H

#include "LanguageEnglish.h"

class LanguageKorean : public LanguageEnglish
{
public:
    LanguageKorean() {
        trainerName             = "트레이너";
        trainerCaption          = "blueMSX - 트레이너";
        saveCheatCaption        = "치트 저장하기";
        loadCheatCaption        = "치트 읽어오기";
        pause                   = "일시정지";
        run                     = "시작";
        truncated               = "생략...";
        newCheat                = "새로운 치트";
        address                 = "주소";
        oldValue                = "이전 값";
        newValue                = "새로운 값";
        change                  = "변화";
        disable                 = "비활성";
        enable                  = "활성";
        description             = "설명";
        value                   = "값";
        activeCheats            = "사용가능 치트 ";
        findCheats              = "치트 찾기 ";
        removeAll               = "모두 제거";
        remove                  = "제거";
        addCheat                = "치트 추가";
        cheatFile               = "치트 파일:";
        ok                      = "확인";
        cancel                  = "취소";
        displayValueAs          = "값 표시 방법 ";
        decimal                 = "10 진수";
        hexadecimal             = "16 진수";
        dataSize                = "데이터 크기 ";
        eightBit                = "8 비트";
        sixteenBit              = "16 비트";
        compareType             = "비교 방법 ";
        equal                   = "같은";
        notEqual                = "같지 않은";
        lessThan                = "작은";
        lessOrEqual             = "작거나 같은";
        greaterThan             = "큰";
        greaterOrEqual          = "크거나 같은";
        display                 = "표시 방법 ";
        compareNewValueWith     = "새로운 값과 비교 ";
        specificValue           = "특정 값: ";
        snapshot                = "스냅샷";
        search                  = "찾기";
        undo                    = "실행 취소";
    }
};

#endif //LANGUAGE_KOREAN_H
