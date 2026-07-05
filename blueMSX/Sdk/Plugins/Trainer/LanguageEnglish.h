/*****************************************************************************
** File:
**      Language.h
**
** Copyright (C) 2003-2004 Daniel Vik
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

#include "ToolInterface.h"

class LanguageEnglish : public Language
{
public:
    LanguageEnglish() {
        trainerName             = "Trainer";
        trainerCaption          = "blueMSX+ - Trainer";
        saveCheatCaption        = "Save Cheat";
        loadCheatCaption        = "Load Cheat";
        pause                   = "Pause";
        run                     = "Run";
        truncated               = "truncated...";
        newCheat                = "New cheat";
        address                 = "Address";
        oldValue                = "Old Value";
        newValue                = "New Value";
        change                  = "Change";
        disable                 = "Disable";
        enable                  = "Enable";
        description             = "Description";
        value                   = "Value";
        activeCheats            = "Active Cheats ";
        findCheats              = "Find Cheats ";
        removeAll               = "Remove All";
        remove                  = "Remove";
        addCheat                = "Add Cheat";
        cheatFile               = "Cheat File:";
        ok                      = "OK";
        cancel                  = "Cancel";
        displayValueAs          = "Display value as ";
        decimal                 = "Decimal";
        hexadecimal             = "Hexadecimal";
        dataSize                = "Data size ";
        eightBit                = "8 bit";
        sixteenBit              = "16 bit";
        compareType             = "Compare type ";
        equal                   = "Equal";
        notEqual                = "Not equal";
        lessThan                = "Less than";
        lessOrEqual             = "Less or equal";
        greaterThan             = "Greater than";
        greaterOrEqual          = "Greater or equal";
        display                 = "Display ";
        compareNewValueWith     = "Compare new value with ";
        specificValue           = "Specific value: ";
        snapshot                = "Snapshot";
        search                  = "Search";
        undo                    = "Undo";
    }
};

#endif //LANGUAGE_ENGLISH_H
