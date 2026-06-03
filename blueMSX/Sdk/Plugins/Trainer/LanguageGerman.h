/*****************************************************************************
** File:
**      LanguageGerman.h
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
#ifndef LANGUAGE_GERMAN_H
#define LANGUAGE_GERMAN_H

#include "LanguageEnglish.h"

class LanguageGerman : public LanguageEnglish
{
public:
    LanguageGerman() {
        trainerName             = "Trainer";
        trainerCaption          = "blueMSX+ - Trainer";
        saveCheatCaption        = "Speichere Cheat";
        loadCheatCaption        = "Lade Cheat";
        pause                   = "Pause";
        run                     = "Run";
        truncated               = "abgeschnitten ...";
        newCheat                = "Neuer cheat";
        address                 = "Adresse";
        oldValue                = "Alter Wert";
        newValue                = "Neuer Wert";
        change                  = "Ändern";
        disable                 = "Sperren";
        enable                  = "Freigeben";
        description             = "Beschreibung";
        value                   = "Wert";
        activeCheats            = "Aktive Cheats ";
        findCheats              = "Finde Cheats ";
        removeAll               = "Entferne alles";
        remove                  = "Entfernen";
        addCheat                = "Cheat hinzufügen";
        cheatFile               = "Cheat-File:";
        ok                      = "OK";
        cancel                  = "Zurücknehmen";
        displayValueAs          = "Werte darstellen als ";
        decimal                 = "dezimal";
        hexadecimal             = "hexadezimal";
        dataSize                = "Datengröße ";
        eightBit                = "8-Bit";
        sixteenBit              = "16-Bit";
        compareType             = "Vergleiche Typ ";
        equal                   = "gleich";
        notEqual                = "ungleich";
        lessThan                = "kleiner als";
        lessOrEqual             = "kleiner gleich";
        greaterThan             = "größer als";
        greaterOrEqual          = "größer gleich";
        display                 = "Display ";
        compareNewValueWith     = "Vergleiche neuen Wert mit ";
        specificValue           = "Spezifischer Wert: ";
        snapshot                = "Snapshot";
        search                  = "Suche";
        undo                    = "Rückgängig";
    }
};

#endif //LANGUAGE_GERMAN_H
