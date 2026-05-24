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
        traceWindowName         = "Trace Logger";
        traceWindowCaption      = "blueMSX - Trace Logger";
        openWindowCaption       = "로그 파일 열기";

        menuFile                = "파일";
        menuFileLogToFile       = "로그를 파일로 기록";
        menuFileStopLogToFile   = "파일로 기록하기 중지";
        menuFileExit            = "끝내기";
        
        menuEdit                = "편집";
        menuEditSelectAll       = "모두 선택";
        menuEditCopy            = "복사";
        menuEditClearWindow     = "윈도우 클리어";

        menuHelp                = "도움말";
        menuHelpAbout           = "Trace Logger에 대해서";

        aboutBuilt              = "빌드:";
        aboutVisit              = "자세한 것은 http://www.bluemsx.com 을 방문하세요.";
    }
};

#endif //LANGUAGE_KOREAN_H
