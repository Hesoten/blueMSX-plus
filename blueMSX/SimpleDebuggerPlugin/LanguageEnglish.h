/*****************************************************************************
** File:
**      LanguageEnglish.h
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
#ifndef LANGUAGE_ENGLISH_H
#define LANGUAGE_ENGLISH_H

#include "Language.h"

class LanguageEnglish : public Language
{
public:
    LanguageEnglish() {
        genericOk                   = "OK";
        genericCancel               = "Cancel";

        toolbarResume               = "Start/Continue";
        toolbarPause                = "Break All";
        toolbarStop                 = "Stop Debugging";
        toolbarRun                  = "Restart";
        toolbarShowNext             = "Show Next Statement";
        toolbarStepIn               = "Step Into";
        toolbarStepBack             = "Step Back";
        toolbarStepOver             = "Step Over";
        toolbarStepOut              = "Step Out";
        toolbarRunTo                = "Run To Cursor";
        toolbarBpToggle             = "Set/Remove Breakpoint";
        toolbarBpEnable             = "Enable/Disable Breakpoint";
        toolbarBpEnableAll          = "Enable All Breakpoints";
        toolbarBpDisableAll         = "Disable All Breakpoints";
        toolbarBpRemoveAll          = "Remove All Breakpoints";

        menuFile                    = "File";
        menuFileLoadSymbolFile      = "Load Symbol File";
        menuFileSaveDisassembly     = "Save Disassembly";
        menuFileSaveMemory          = "Save Memory";
        menuFileExit                = "Exit";

        menuDebug                   = "Debug";
        menuDebugStart              = "Start";
        menuDebugContinue           = "Continue";
        menuDebugBreakAll           = "Break All";
        menuDebugStop               = "Stop Debugging";
        menuDebugRestart            = "Restart";
        menuDebugStepIn             = "Step Into";
        menuDebugStepBack           = "Step Back";
        menuDebugStepOver           = "Step Over";
        menuDebugStepOut            = "Step Out";
        menuDebugRunTo              = "Run To Cursor";
        menuDebugShowSymbols        = "Show Symbol Information";
        menuDebugGoto               = "Go To";
        menuDebugFind               = "Find";
        menuDebugBpAdd              = "Add New Breakpoint";
        menuDebugWpAdd              = "Add New Watchpoint";
        menuDebugBpToggle           = "Set/Remove Breakpoint";
        menuDebugEnable             = "Enable/Disable Breakpoint";
        menuDebugRemoveAll          = "Remove All Breakpoints";
        menuDebugEnableAll          = "Enable All Breakpoints";
        menuDebugDisableAll         = "Disable All Breakpoints";
        menuDebugShowAssemblyFlags  = "Show Assembly Flags";
        menuDebugFastVram           = "Break on too fast VRAM access";
        menuWindow                  = "Window";

        menuHelp                    = "Help";
        menuHelpAbout               = "About";

        debuggerName                = "Debugger";
        windowDebugger              = "blueMSX - Debugger";
        windowDisassembly           = "Disassembly";
        windowDisassemblyUnavail    = "Disassembly unavailable.";
        windowCpuRegisters          = "CPU Registers";
        windowCpuRegistersFlags     = "Flags";
        windowStack                 = "Stack";
        windowStackUnavail          = "Stack unavailable.";
        windowCallstack             = "Callstack";
        windowBreakpoints           = "Breakpoints";
        windowCallstackUnavail      = "Callstack unavailable.";
        windowMemory                = "Memory";
        windowPeripheralRegisters   = "Peripheral Registers";
        windowIoPorts               = "I/O Ports";

        memWindowRegisters          = "Registers:";

        memWindowMemory             = "Memory:";
        memWindowAddress            = "Address:";
        
        setBpWindowCaption          = "Add New Breakpoint";
        setWpWindowCaption          = "Add New Watchpoint";
        gotoWindowCaption           = "Go To Address";
        gotoWindowText              = "Address or Label:";
        findWindowCaption           = "Find";
        findWindowText              = "String or Value:";

        symbolWindowCaption         = "Open Symbol File";
        symbolWindowText            = "Replace old symbols";
        
        popupOverwrite              = "File already exists. Do you want to overwrite it?";
        
        statusRunning               = "Running";
        statusPaused                = "Paused";
        statusStopped               = "Stopped";

        aboutBuilt                  = "Built:";
        aboutVisit                  = "Visit http://www.bluemsx.com for details";
    }
};


#endif //LANGUAGE_ENGLISH_H
