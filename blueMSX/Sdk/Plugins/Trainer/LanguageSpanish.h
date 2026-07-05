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
#ifndef LANGUAGE_SPANISH_H
#define LANGUAGE_SPANISH_H

#include "LanguageEnglish.h"

class LanguageSpanish : public LanguageEnglish
{
public:
    LanguageSpanish() {
        trainerName             = "Trainer";
        trainerCaption          = "blueMSX+ - Trainer";
        saveCheatCaption        = "Guardar Cheat";
        loadCheatCaption        = "Cargar Cheat";
        pause                   = "Pausa";
        run                     = "Ejecutar";
        truncated               = "truncado...";
        newCheat                = "Nuevo Cheat";
        address                 = "Dirección";
        oldValue                = "Viejo Valor";
        newValue                = "Nuevo Valor";
        change                  = "Editar";
        disable                 = "Desactivar";
        enable                  = "Activar";
        description             = "Descripción";
        value                   = "Valor";
        activeCheats            = "Activos Cheats ";
        findCheats              = "Encontrar Cheats ";
        removeAll               = "Borrar Todos";
        remove                  = "Borrar";
        addCheat                = "Añadir Cheat";
        cheatFile               = "Cheat Archivo:";
        ok                      = "OK";
        cancel                  = "Cancelar";
        displayValueAs          = "Mostrar Valor ";
        decimal                 = "Decimal";
        hexadecimal             = "Hexadecimal";
        dataSize                = "Formato de los datos ";
        eightBit                = "8 bit";
        sixteenBit              = "16 bit";
        compareType             = "Método de comparación ";
        equal                   = "Igual";
        notEqual                = "No igual";
        lessThan                = "Menos que";
        lessOrEqual             = "Menos o igual";
        greaterThan             = "Mayor que";
        greaterOrEqual          = "Mayor o igual";
        display                 = "Mostrar ";
        compareNewValueWith     = "Comparar el nuevo valor con ";
        specificValue           = "Valor específico: ";
        snapshot                = "Snapshot";
        search                  = "Buscar";
        undo                    = "Cancelar";
    }
};

#endif //LANGUAGE_SPANISH_H
