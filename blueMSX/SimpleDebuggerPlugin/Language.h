/*****************************************************************************
** File:
**      Language.h
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
#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "ToolInterface.h"

class Language 
{
public:
    static void  SetLanguage(LanguageId langId);
    virtual ~Language() {}

    static const char* genericOk;
    static const char* genericCancel;

    static const char* toolbarResume;
    static const char* toolbarPause;
    static const char* toolbarStop;
    static const char* toolbarRun;
    static const char* toolbarShowNext;
    static const char* toolbarStepIn;
    static const char* toolbarStepBack;
    static const char* toolbarStepOver;
    static const char* toolbarStepOut;
    static const char* toolbarRunTo;
    static const char* toolbarBpToggle;
    static const char* toolbarBpEnable;
    static const char* toolbarBpEnableAll;
    static const char* toolbarBpDisableAll;
    static const char* toolbarBpRemoveAll;

    static const char* menuFile;
    static const char* menuFileLoadSymbolFile;
    static const char* menuFileSaveDisassembly;
    static const char* menuFileSaveMemory;
    static const char* menuFileExit;

    static const char* menuDebug;
    static const char* menuDebugStart;
    static const char* menuDebugContinue;
    static const char* menuDebugBreakAll;
    static const char* menuDebugStop;
    static const char* menuDebugRestart;
    static const char* menuDebugStepIn;
    static const char* menuDebugStepBack;
    static const char* menuDebugStepOver;
    static const char* menuDebugStepOut;
    static const char* menuDebugRunTo;
    static const char* menuDebugShowSymbols;
    static const char* menuDebugGoto;
    static const char* menuDebugFind;
    static const char* menuDebugBpAdd;
    static const char* menuDebugWpAdd;
    static const char* menuDebugBpToggle;
    static const char* menuDebugEnable;
    static const char* menuDebugRemoveAll;
    static const char* menuDebugEnableAll;
    static const char* menuDebugDisableAll;
    static const char* menuDebugShowAssemblyFlags;
    static const char* menuDebugFastVram;

    static const char* menuWindow;
    static const char* menuHelp;
    static const char* menuHelpAbout;

    static const char* debuggerName;
    static const char* windowDebugger;
    static const char* windowDisassembly;
    static const char* windowDisassemblyUnavail;
    static const char* windowCpuRegisters;
    static const char* windowCpuRegistersFlags;
    static const char* windowStack;
    static const char* windowStackUnavail;
    static const char* windowCallstack;
    static const char* windowBreakpoints;
    static const char* windowCallstackUnavail;
    static const char* windowMemory;
    static const char* windowPeripheralRegisters;
    static const char* windowIoPorts;

    static const char* memWindowMemory;
    static const char* memWindowAddress;

    static const char* memWindowRegisters;
    
    static const char* setBpWindowCaption;
    static const char* setWpWindowCaption;
    static const char* gotoWindowCaption;
    static const char* gotoWindowText;
    static const char* findWindowCaption;
    static const char* findWindowText;

    static const char* symbolWindowCaption;
    static const char* symbolWindowText;

    static const char* popupOverwrite;

    static const char* statusRunning;
    static const char* statusPaused;
    static const char* statusStopped;

    static const char* aboutBuilt;
    static const char* aboutVisit;

    Language() {}
};

#endif //LANGUAGE_H
