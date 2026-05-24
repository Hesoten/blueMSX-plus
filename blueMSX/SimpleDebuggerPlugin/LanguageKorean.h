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
#ifndef LANGUAGE_KOERAN_H
#define LANGUAGE_KOERAN_H

#include "LanguageEnglish.h"

class LanguageKorean : public LanguageEnglish
{
public:
    LanguageKorean() {
        genericOk                   = "확인";
        genericCancel               = "취소";

        toolbarResume               = "시작/계속";
        toolbarPause                = "모두 중단";
        toolbarStop                 = "디버깅 중지";
        toolbarRun                  = "다시 시작";
        toolbarShowNext             = "다음 상태 보여주기";
        toolbarStepIn               = "프로시저 안으로";
        toolbarStepOver             = "프로시저 단위 실행";
        toolbarStepOut              = "프로시저 나가기";
        toolbarRunTo                = "커서까지 실행";
        toolbarBpToggle             = "중단점 설정/제거";
        toolbarBpEnable             = "중단점 활성/비활성";
        toolbarBpEnableAll          = "모든 중단점 활성";
        toolbarBpDisableAll         = "모든 중단점 비활성";
        toolbarBpRemoveAll          = "모든 중단점 제거";

        menuFile                    = "파일";
        menuFileLoadSymbolFile      = "심볼 파일 로드";
        menuFileSaveDisassembly     = "디스 어셈블리 저장";
        menuFileSaveMemory          = "메모리 저장";
        menuFileExit                = "끝내기";

        menuDebug                   = "디버그";
        menuDebugStart              = "시작";
        menuDebugContinue           = "계속";
        menuDebugBreakAll           = "모두 중단";
        menuDebugStop               = "디버깅 중지";
        menuDebugRestart            = "다시 시작";
        menuDebugStepIn             = "프로시저 안으로";
        menuDebugStepOver           = "프로시저 단위 실행";
        menuDebugStepOut            = "프로시저 나가기";
        menuDebugRunTo              = "커서까지 실행";
        menuDebugShowSymbols        = "심볼 정보 보여주기";
        menuDebugGoto               = "지정주소로 가기";
        menuDebugFind               = "찾기";
        menuDebugBpAdd              = "중단점 추가";
        menuDebugBpToggle           = "중단점 설정/제거";
        menuDebugEnable             = "중단점 활성/비활성";
        menuDebugRemoveAll          = "모든 중단점 제거";
        menuDebugEnableAll          = "모든 중단점 활성";
        menuDebugDisableAll         = "모든 중단점 비활성";
        menuDebugShowAssemblyFlags  = "어셈블리 플래그 보여주기";
        menuDebugFastVram           = "Break on too fast VRAM access";

        menuWindow                  = "윈도우";

        menuHelp                    = "도움말";
        menuHelpAbout               = "Debugger에 대하여";

        debuggerName                = "Debugger";
        windowDebugger              = "blueMSX - Debugger";
        windowDisassembly           = "디스어셈블리";
        windowDisassemblyUnavail    = "Disassembly unavailable.";
        windowCpuRegisters          = "CPU 레지스터";
        windowCpuRegistersFlags     = "Flags";
        windowStack                 = "스택";
        windowStackUnavail          = "Stack unavailable.";
        windowCallstack             = "호출 스택";
        windowCallstackUnavail      = "Callstack unavailable.";
        windowMemory                = "메모리";
        windowPeripheralRegisters   = "주변장치 레지스터";
        windowIoPorts               = "입출력 포트";

		memWindowRegisters          = "Registers:";

        memWindowMemory             = "Memory:";
        memWindowAddress            = "Address:";

        setBpWindowCaption          = "새로운 중단점 추가";
        gotoWindowCaption           = "주소로 가기";
        gotoWindowText              = "주소 혹은 레이블:";

        symbolWindowCaption         = "심볼 파일 열기";
        symbolWindowText            = "이전 심볼 교체";

        popupOverwrite              = "파일이 이미 있습니다. 덮어 쓰시겠습니까?";
        
        aboutBuilt                  = "빌드:";
        aboutVisit                  = "자세한 것은 http://www.bluemsx.com 을 방문하세요.";
    }
};


#endif //LANGUAGE_KOERAN_H
