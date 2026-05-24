/*****************************************************************************
** File:
**      LanguageJapanese.h
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
#ifndef LANGUAGE_JAPANESE_H
#define LANGUAGE_JAPANESE_H

#include "LanguageEnglish.h"

class LanguageJapanese : public LanguageEnglish
{
public:
    LanguageJapanese() {
        traceWindowName         = "Trace Logger";
        traceWindowCaption      = "blueMSX - Trace Logger";
        openWindowCaption       = "Open Log File";

        menuFile                = "File";
        menuFileLogToFile       = "Log to File";
        menuFileExit            = "Exit";
        
        menuEdit                = "Edit";
        menuEditSelectAll       = "Select All";
        menuEditCopy            = "Copy";
        menuEditClearWindow     = "Clear Window";

        menuHelp                = "Help";
        menuHelpAbout           = "About";

        aboutBuilt              = "ビルド:";
        aboutVisit              = "詳しい情報は http://www.bluemsx.com まで";
    }
};

#endif //LANGUAGE_JAPANESE_H
