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
#ifndef LANGUAGE_CATALAN_H
#define LANGUAGE_CATALAN_H

#include "LanguageEnglish.h"

class LanguageCatalan : public LanguageEnglish
{
public:
    LanguageCatalan() {
        trainerName             = "Trainer";
        trainerCaption          = "blueMSX - Trainer";
        saveCheatCaption        = "Desa el truc";
        loadCheatCaption        = "Carrega el truc";
        pause                   = "Posa en pausa";
        run                     = "Executa";
        truncated               = "truncat...";
        newCheat                = "Nou truc";
        address                 = "Adreça";
        oldValue                = "Valor antic";
        newValue                = "Valor nou";
        change                  = "Canvia";
        disable                 = "Desactiva";
        enable                  = "Activa";
        description             = "Descripció";
        value                   = "Valor";
        activeCheats            = "Trucs actius ";
        findCheats              = "Cerca trucs ";
        removeAll               = "Elimina tots";
        remove                  = "Elimina";
        addCheat                = "Afegeix un truc";
        cheatFile               = "Fitxer de trucs:";
        ok                      = "D'acord";
        cancel                  = "Cancel·la";
        displayValueAs          = "Mostra el valor com ";
        decimal                 = "Decimal";
        hexadecimal             = "Hexadecimal";
        dataSize                = "Mida de les dades ";
        eightBit                = "8 bit";
        sixteenBit              = "16 bit";
        compareType             = "Tipus de comparació ";
        equal                   = "Igual";
        notEqual                = "No igual";
        lessThan                = "Menor de";
        lessOrEqual             = "Menor o igual";
        greaterThan             = "Major de";
        greaterOrEqual          = "Major de o igual";
        display                 = "Visualització ";
        compareNewValueWith     = "Compara el nou valor amb ";
        specificValue           = "Valor específic: ";
        snapshot                = "Instantània";
        search                  = "Cerca";
        undo                    = "Desfés";
    }
};

#endif //LANGUAGE_ENGLISH_H
