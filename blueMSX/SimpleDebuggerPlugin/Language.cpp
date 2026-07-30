/*****************************************************************************
** File:        Language.cpp
**
** Author:      Daniel Vik
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
#include "Language.h"
#include "LanguageChineseSimplified.h"
#include "LanguageChineseTraditional.h"
#include "LanguageEnglish.h"
//#include "LanguageFinnish.h"
#include "LanguageJapanese.h"
#include "LanguageRussian.h"
#include "LanguageKorean.h"
#include <stdlib.h>

namespace {
    Language* ls = new LanguageEnglish;
}


void Language::SetLanguage(LanguageId langId) 
{
    delete ls;
    switch (langId) {
    case LID_CHINESETRAD:
        ls  = new LanguageChineseTraditional;
        break;
    case LID_CHINESESIMP:
        ls  = new LanguageChineseSimplified;
        break;
    case LID_JAPANESE:
        ls = new LanguageJapanese;
        break;
    case LID_RUSSIAN:
        ls = new LanguageRussian;
        break;
//    case LID_FINNISH:
//        ls = new LanguageFinnish;
//        break;
    case LID_KOREAN:
        ls = new LanguageKorean;
        break;
    case LID_ENGLISH:
    default:
        ls = new LanguageEnglish;
        break;
    }
}

const char* Language::genericOk;
const char* Language::genericCancel;

const char* Language::toolbarResume;
const char* Language::toolbarPause;
const char* Language::toolbarStop;
const char* Language::toolbarRun;
const char* Language::toolbarShowNext;
const char* Language::toolbarStepIn;
const char* Language::toolbarStepBack;
const char* Language::toolbarStepOver;
const char* Language::toolbarStepOut;
const char* Language::toolbarRunTo;
const char* Language::toolbarBpToggle;
const char* Language::toolbarBpEnable;
const char* Language::toolbarBpEnableAll;
const char* Language::toolbarBpDisableAll;
const char* Language::toolbarBpRemoveAll;

const char* Language::menuFile;
const char* Language::menuFileLoadSymbolFile;
const char* Language::menuFileSaveDisassembly;
const char* Language::menuFileSaveMemory;
const char* Language::menuFileExit;

const char* Language::menuDebug;
const char* Language::menuDebugStart;
const char* Language::menuDebugContinue;
const char* Language::menuDebugBreakAll;
const char* Language::menuDebugStop;
const char* Language::menuDebugRestart;
const char* Language::menuDebugStepIn;
const char* Language::menuDebugStepBack;
const char* Language::menuDebugStepOver;
const char* Language::menuDebugStepOut;
const char* Language::menuDebugRunTo;
const char* Language::menuDebugShowSymbols;
const char* Language::menuDebugGoto;
const char* Language::menuDebugFind;
const char* Language::menuDebugBpAdd;
const char* Language::menuDebugWpAdd;
const char* Language::menuDebugBpToggle;
const char* Language::menuDebugEnable;
const char* Language::menuDebugRemoveAll;
const char* Language::menuDebugEnableAll;
const char* Language::menuDebugDisableAll;
const char* Language::menuDebugShowAssemblyFlags;
const char* Language::menuDebugFastVram;

const char* Language::menuWindow;
const char* Language::menuHelp;
const char* Language::menuHelpAbout;

const char* Language::debuggerName;
const char* Language::windowDebugger;
const char* Language::windowDisassembly;
const char* Language::windowDisassemblyUnavail;
const char* Language::windowCpuRegisters;
const char* Language::windowCpuRegistersFlags;
const char* Language::windowStack;
const char* Language::windowStackUnavail;
const char* Language::windowCallstack;
const char* Language::windowBreakpoints;
const char* Language::windowCallstackUnavail;
const char* Language::windowMemory;
const char* Language::windowPeripheralRegisters;
const char* Language::windowIoPorts;

const char* Language::memWindowRegisters;

const char* Language::memWindowMemory;
const char* Language::memWindowAddress;

const char* Language::setBpWindowCaption;
const char* Language::setWpWindowCaption;
const char* Language::gotoWindowCaption;
const char* Language::gotoWindowText;
const char* Language::findWindowCaption;
const char* Language::findWindowText;

const char* Language::symbolWindowCaption;
const char* Language::symbolWindowText;

const char* Language::popupOverwrite;
const char* Language::popupRemoveBreakpoints;

const char* Language::statusRunning;
const char* Language::statusPaused;
const char* Language::statusStopped;

const char* Language::aboutBuilt;
const char* Language::aboutVisit;
