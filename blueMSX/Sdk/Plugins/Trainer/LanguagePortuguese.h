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
#ifndef LANGUAGE_PORTUGUESE_H
#define LANGUAGE_PORTUGUESE_H

#include "LanguageEnglish.h"

class LanguagePortuguese : public LanguageEnglish
{
public:
    LanguagePortuguese() {
        trainerName             = "Trainer";
        trainerCaption          = "blueMSX+ - Trainer";
        saveCheatCaption        = "Salvar Cheat";
        loadCheatCaption        = "Carregar Cheat";
        pause                   = "Pausa";
        run                     = "Executar";
        truncated               = "truncado...";
        newCheat                = "Novo cheat";
        address                 = "Endereço";
        oldValue                = "Velho Valor";
        newValue                = "Novo Valor";
        change                  = "Editar";
        disable                 = "Desativar";
        enable                  = "Activar";
        description             = "Descrição";
        value                   = "Valor";
        activeCheats            = "Ativas Cheats ";
        findCheats              = "Encontrar Cheats ";
        removeAll               = "Remover Todo";
        remove                  = "Remover";
        addCheat                = "Adicionar Cheat";
        cheatFile               = "Cheat Arquivo:";
        ok                      = "OK";
        cancel                  = "Cancelar";
        displayValueAs          = "Mostrar valor ";
        decimal                 = "Decimal";
        hexadecimal             = "Hexadecimal";
        dataSize                = "Formato dos dados ";
        eightBit                = "8 bit";
        sixteenBit              = "16 bit";
        compareType             = "Modo de comparação ";
        equal                   = "Igual";
        notEqual                = "Não igual";
        lessThan                = "Menos que";
        lessOrEqual             = "Menos ou igual";
        greaterThan             = "Mais grande que";
        greaterOrEqual          = "Mais grande ou igual";
        display                 = "Mostrar ";
        compareNewValueWith     = "Comparar o novo valor com ";
        specificValue           = "Valor específico: ";
        snapshot                = "Snapshot";
        search                  = "Buscar";
        undo                    = "Cancelar";
    }
};

#endif //LANGUAGE_PORTUGUESE_H
