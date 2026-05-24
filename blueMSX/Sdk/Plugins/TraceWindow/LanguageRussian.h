/*****************************************************************************
** File:
**      LanguageRussian.h
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
#ifndef LANGUAGE_RUSSIAN_H
#define LANGUAGE_RUSSIAN_H

#include "ToolInterface.h"

class LanguageRussian : public Language
{
public:
    LanguageRussian() {
        traceWindowName         = "Логгер клавиатуры";
        traceWindowCaption      = "blueMSX - Логгер клавиатуры";
        openWindowCaption       = "Открыть лог-файл";

        menuFile                = "Файл";
        menuFileLogToFile       = "Записывать в файл";
        menuFileStopLogToFile   = "Остановить записывание";
        menuFileExit            = "Выход";
        
        menuEdit                = "Правка";
        menuEditSelectAll       = "Выделить все";
        menuEditCopy            = "Копировать";
        menuEditClearWindow     = "Очистить окно";

        menuHelp                = "Помощь";
        menuHelpAbout           = "О программе";

        aboutBuilt              = "Сборка:";
        aboutVisit              = "Посетите www.bluemsx.com для подробностей";
    }
};

#endif //LANGUAGE_RUSSIAN_H
