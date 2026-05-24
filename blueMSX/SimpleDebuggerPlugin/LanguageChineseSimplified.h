/*****************************************************************************
** File:
**      LanguageChineseSimplified.h
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
#ifndef LANGUAGE_CHINESE_SIMPLIFIED_H
#define LANGUAGE_CHINESE_SIMPLIFIED_H

#include "LanguageEnglish.h"

class LanguageChineseSimplified : public LanguageEnglish
{
public:
    LanguageChineseSimplified() {
        genericOk                   = "确定";
        genericCancel               = "取消";

        toolbarResume               = "开始/继续";
        toolbarPause                = "全体中断";
        toolbarStop                 = "停止调试";
        toolbarRun                  = "重置";
        toolbarShowNext             = "显示下一条语句";
        toolbarStepIn               = "单步执行";
        toolbarStepOver             = "跳出";
        toolbarStepOut              = "逐过程";
        toolbarRunTo                = "运行到光标处";
        toolbarBpToggle             = "设定/移除断点";
        toolbarBpEnable             = "启用/禁用断点";
        toolbarBpEnableAll          = "启用所有断点";
        toolbarBpDisableAll         = "禁用所有断点";
        toolbarBpRemoveAll          = "移除所有断点";

        menuFile                    = "文件";
        menuFileLoadSymbolFile      = "读取符号文件";
        menuFileSaveDisassembly     = "保存反汇编";
        menuFileSaveMemory          = "保存内存";
        menuFileExit                = "退出";

        menuDebug                   = "调试";
        menuDebugStart              = "开始";
        menuDebugContinue           = "继续";
        menuDebugBreakAll           = "全体中断";
        menuDebugStop               = "停止调试";
        menuDebugRestart            = "重新开始";
        menuDebugStepIn             = "单步执行";
        menuDebugStepOver           = "跳出";
        menuDebugStepOut            = "逐过程";
        menuDebugRunTo              = "运行到光标处";
        menuDebugShowSymbols        = "显示符号信息";
        menuDebugGoto               = "转到";
        menuDebugFind               = "搜索";
        menuDebugBpAdd              = "添加新断点";
        menuDebugBpToggle           = "设定/移除 ";
        menuDebugEnable             = "启用/禁用断点";
        menuDebugRemoveAll          = "移除所有断点";
        menuDebugEnableAll          = "启用所有断点";
        menuDebugDisableAll         = "禁用所有断点";
        menuDebugShowAssemblyFlags  = "显示汇编标志";
        menuDebugFastVram           = "Break on too fast VRAM access";

        menuWindow                  = "窗口";

        menuHelp                    = "帮助";
        menuHelpAbout               = "关于";

        debuggerName                = "调试工具";
        windowDebugger              = "blueMSX - 调试工具";
        windowDisassembly           = "反汇编";
        windowDisassemblyUnavail    = "反汇编不可用。";
        windowCpuRegisters          = "CPU寄存器。";
        windowCpuRegistersFlags     = "旗标";
        windowStack                 = "栈";
        windowStackUnavail          = "栈不可用。";
        windowCallstack             = "调用栈";
        windowCallstackUnavail      = "调用栈不可用。";
        windowMemory                = "内存";
        windowPeripheralRegisters   = "外围寄存器";
        windowIoPorts               = "I/O 端口";

        memWindowRegisters          = "寄存器:";

        memWindowMemory             = "内存:";
        memWindowAddress            = "地址:";

        setBpWindowCaption          = "添加新断点";
        gotoWindowCaption           = "转到地址";
        gotoWindowText              = "地址或标签:";

        symbolWindowCaption         = "打开符号文件";
        symbolWindowText            = "覆盖旧的符号";

        popupOverwrite              = "文件已存在。要覆盖原有文件吗？";
        
        aboutBuilt                  = "创建:";
        aboutVisit                  = "更多详情请登陆：www.bluemsx.com ";
    }
};


#endif //LANGUAGE_CHINESE_SIMPLIFIED_H
