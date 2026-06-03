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
        traceWindowName         = "跟踪记录工具";
        traceWindowCaption      = "blueMSX+ - 跟踪记录工具";
        openWindowCaption       = "打开日志文件";

        menuFile                = "文件";
        menuFileLogToFile       = "保存日志到文本";
        menuFileExit            = "退出";
        
        menuEdit                = "编辑";
        menuEditSelectAll       = "全选";
        menuEditCopy            = "复制";
        menuEditClearWindow     = "清空窗口";

        menuHelp                = "帮助";
        menuHelpAbout           = "关于";

        aboutBuilt              = "创建:";
        aboutVisit              = "更多详情请登陆：www.bluemsx.com ";
    }
};

#endif //LANGUAGE_CHINESE_SIMPLIFIED_H
