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
        trainerName             = "Коды";
        trainerCaption          = "blueMSX - Коды";
        saveCheatCaption        = "Сохранить код";
        loadCheatCaption        = "Загрузить код";
        pause                   = "Пауза";
        run                     = "Запуск";
        truncated               = "искаженный...";
        newCheat                = "Новый код";
        address                 = "Адрес";
        oldValue                = "Старое значение";
        newValue                = "Новое значение";
        change                  = "Изменить";
        disable                 = "Отключить";
        enable                  = "Включить";
        description             = "Описание";
        value                   = "Значение";
        activeCheats            = "Активные коды ";
        findCheats              = "Найти код ";
        removeAll               = "Убрать все";
        remove                  = "Убрать";
        addCheat                = "Добавить код";
        cheatFile               = "Файл кода:";
        ok                      = "OK";
        cancel                  = "Отмена";
        displayValueAs          = "Показывать значение как ";
        decimal                 = "Двоичное";
        hexadecimal             = "Шестнадцатеричное";
        dataSize                = "Размер данных ";
        eightBit                = "8 бит";
        sixteenBit              = "16 бит";
        compareType             = "Сравнительный тип ";
        equal                   = "Равный";
        notEqual                = "Не равный";
        lessThan                = "Меньше чем";
        lessOrEqual             = "Меньше или равен";
        greaterThan             = "Больше чем";
        greaterOrEqual          = "Больше или равен";
        display                 = "Показать ";
        compareNewValueWith     = "Сравнить новое значение с ";
        specificValue           = "Особым значением: ";
        snapshot                = "Снимок";
        search                  = "Поиск";
        undo                    = "Отмена";
    }
};

#endif //LANGUAGE_RUSSIAN_H
