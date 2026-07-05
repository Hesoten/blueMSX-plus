/*****************************************************************************
** File:
**      Language.h
**
** Copyright (C) 2003-2008 Daniel Vik
** Italian translation by Luca Chiodi (KdL)
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
#ifndef LANGUAGE_ITALIAN_H
#define LANGUAGE_ITALIAN_H

#include "LanguageEnglish.h"

class LanguageItalian : public LanguageEnglish
{
public:
    LanguageItalian() {
        trainerName             = "Trainer";
        trainerCaption          = "blueMSX+ - Trainer";
        saveCheatCaption        = "Salva il cheat con nome";
        loadCheatCaption        = "Carica un cheat";
        pause                   = "Sospendi";
        run                     = "Esegui";
        truncated               = "(troncato...)";
        newCheat                = "Nuovo cheat";
        address                 = "Indirizzo";
        oldValue                = "Vecchio valore";
        newValue                = "Nuovo valore";
        change                  = "Valore modificato";
        disable                 = "Disattiva";
        enable                  = "Attiva";
        description             = "Descrizione";
        value                   = "Valore";
        activeCheats            = "Cheat attivi ";
        findCheats              = "Trova cheat ";
        removeAll               = "Rimuovi tutto";
        remove                  = "Rimuovi";
        addCheat                = "Aggiungi";
        cheatFile               = "File cheat in uso:";
        ok                      = "OK";
        cancel                  = "Annulla";
        displayValueAs          = "Mostra il valore in ";
        decimal                 = "Decimale";
        hexadecimal             = "Esadecimale";
        dataSize                = "Formato dati ";
        eightBit                = "8 bit";
        sixteenBit              = "16 bit";
        compareType             = "Tipo di confronto ";
        equal                   = "Uguale a";
        notEqual                = "Diverso da";
        lessThan                = "Minore di";
        lessOrEqual             = "Minore o uguale a";
        greaterThan             = "Maggiore di";
        greaterOrEqual          = "Maggiore o uguale a";
        display                 = "Visualizza in ";
        compareNewValueWith     = "Confronta il nuovo valore al ";
        specificValue           = "Valore specifico ";
        snapshot                = "Acquisisci";
        search                  = "Cerca";
        undo                    = "Ripristina";
    }
};

#endif //LANGUAGE_ITALIAN_H
