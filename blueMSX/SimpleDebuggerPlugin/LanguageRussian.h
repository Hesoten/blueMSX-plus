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

#include "Language.h"

class LanguageRussian : public Language
{
public:
    LanguageRussian() {
        genericOk                   = "OK";
        genericCancel               = "Отмена";

        toolbarResume               = "Начать/Продолжить";
        toolbarPause                = "Приостановить все";
        toolbarStop                 = "Остановить отладку";
        toolbarRun                  = "Перезапуск";
        toolbarShowNext             = "Показать следующего оператора";
        toolbarStepIn               = "Следующий шаг";
        toolbarStepOver             = "Следующий шаг и выполнение кода";
        toolbarStepOut              = "Выйти и продолжить эмуляцию";
        toolbarRunTo                = "Запустить эмуляцию до курсора";
        toolbarBpToggle             = "Поставить/Убрать прерывание";
        toolbarBpEnable             = "Включить/Выключить прерывание";
        toolbarBpEnableAll          = "Включить все прерывания";
        toolbarBpDisableAll         = "Отключить все прерывания";
        toolbarBpRemoveAll          = "Убрать все прерывания";

        menuFile                    = "Файл";
        menuFileLoadSymbolFile      = "Открыть символьный файл";
        menuFileSaveDisassembly     = "Сохранить дизассемблирование";
        menuFileSaveMemory          = "Сохранить память";
        menuFileExit                = "Выход";

        menuDebug                   = "Отладка";
        menuDebugStart              = "Начать";
        menuDebugContinue           = "Продолжить";
        menuDebugBreakAll           = "Приостановить все";
        menuDebugStop               = "Остановить отладку";
        menuDebugRestart            = "Перезапуск";
        menuDebugStepIn             = "Следующий шаг";
        menuDebugStepOver           = "Следующий шаг и выполнение кода";
        menuDebugStepOut            = "Выйти и продолжить эмуляцию";
        menuDebugRunTo              = "Запустить эмуляцию до курсора";
        menuDebugShowSymbols        = "Показать символьную информацию";
        menuDebugGoto               = "Перейти к";
        menuDebugFind               = "Найти";
        menuDebugBpAdd              = "Добавить новое прерывание";
        menuDebugBpToggle           = "Поставить/Убрать прерывание";
        menuDebugEnable             = "Включить/Выключить прерывание";
        menuDebugRemoveAll          = "Убрать все прерывания";
        menuDebugEnableAll          = "Включить все прерывания";
        menuDebugDisableAll         = "Отключить все прерывания";
        menuDebugShowAssemblyFlags  = "Показать метки ассемблера";
        menuDebugFastVram           = "Break on too fast VRAM access";

        menuWindow                  = "Окно";

        menuHelp                    = "Помощь";
        menuHelpAbout               = "О программе";

        debuggerName                = "Дебаггер";
        windowDebugger              = "blueMSX - Дебаггер";
        windowDisassembly           = "Дизассемблирование";
        windowDisassemblyUnavail    = "Дизассемблирование невозможно.";
        windowCpuRegisters          = "CPU регистры";
        windowCpuRegistersFlags     = "Метки";
        windowStack                 = "Стек";
        windowStackUnavail          = "Стек недоступен.";
        windowCallstack             = "Стек вызовов";
        windowCallstackUnavail      = "Стек вызовов недоступен.";
        windowMemory                = "Память";
        windowPeripheralRegisters   = "Регистры оборудования";
        windowIoPorts               = "Порты ввода/вывода";

        memWindowRegisters          = "Регистры:";

        memWindowMemory             = "Память:";
        memWindowAddress            = "Адрес:";

        setBpWindowCaption          = "Добавить новое прерывание";
        gotoWindowCaption           = "Перейти к адресу";
        gotoWindowText              = "Адрес или метка:";
        findWindowCaption           = "Найти";
        findWindowText              = "Строка или значение:";

        symbolWindowCaption         = "Открыть символьный файл";
        symbolWindowText            = "Заменить старые символы";
        
        popupOverwrite              = "Файл уже существует. Хотите перезаписать его?";
        
        aboutBuilt                  = "Сборка:";
        aboutVisit                  = "Посетите www.bluemsx.com для подробностей";
    }
};


#endif //LANGUAGE_RUSSIAN_H
