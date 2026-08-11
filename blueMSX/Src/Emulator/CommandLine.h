/*****************************************************************************
** $Source: /cvsroot/bluemsx/blueMSX/Src/Emulator/CommandLine.h,v $
**
** $Revision: 1.7 $
**
** $Date: 2008/03/30 18:38:40 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************
*/
#ifndef COMMAND_LINE_H
#define COMMAND_LINE_H

#include "Properties.h"

/* Holds a whole command line. cmd.exe stops at 8191 characters. */
#define CMDLINE_MAXLEN 8192

/* Longest string setting a one-shot override can put back. */
#define CMDLINE_MAXOVERRIDE PROP_MAXPATH

/* Replaces a setting for this run only. */
void emuCommandLineOverrideInt(int* field, int value);

/* The same for a string field. previous is the value it held before it was
** overwritten. */
void emuCommandLineOverrideString(char* field, const char* previous);

/* Puts back everything the line replaced. Call before the settings are saved. */
void emuCommandLineRestoreOverrides(void);

/* Forgets them all, for when the user saves the settings themselves. */
void emuCommandLineDropOverrides(void);

/* Why the last call refused the line, or "" when it did not refuse one. */
const char* emuCommandLineGetError(void);

/* Whether a token names the given option, in the / or - form and whatever the
** case. */
int emuArgMatches(const char* arg, const char* value);
int emuNormalizeOneArg(const char* cmdLine, char* out, int outSize);

/* Whether the line names the given option, whether or not a value follows. */
int emuCheckFlagArgument(char* cmdLine, const char* name);

/* Whether the line asks for the option list, in any of its spellings. */
int emuCheckHelpArgument(char* cmdLine);

/* The text /help answers with, rendered from the option table. */
int emuCommandLineGetHelpText(char* out, int size);

/* The token after the named option, or NULL. The result is the token buffer,
** so copy it before asking for another. */
char* emuCheckValueArgument(char* cmdLine, const char* name);

/* The first thing on the line outside the allowed option names, or NULL. The
** result is a static buffer. */
const char* emuFirstOtherArgument(char* cmdLine, const char* const* allowed);

int emuCheckResetArgument(char* szLine);

/* Applies the options that only change a setting for this run. 0 means the
** line was refused, and emuCommandLineGetError() says why. */
int emuCheckSettingArguments(Properties* properties, char* szLine);
int emuTryStartWithArguments(Properties* properties, char* cmdLine, char *gamedir);

#endif

