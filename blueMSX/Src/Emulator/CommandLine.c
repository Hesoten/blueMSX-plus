/*****************************************************************************
** $Source: /cvsroot/bluemsx/blueMSX/Src/Emulator/CommandLine.c,v $
**
** $Revision: 1.35 $
**
** $Date: 2008/08/31 06:13:13 $
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
#include "CommandLine.h"
#include "RomTypeList.h"
#include "TokenExtract.h"
#include "IsFileExtension.h"
#include "MediaDb.h"
#include "ziphelper.h"
#include "Machine.h"
#include "Casette.h"
#include "Disk.h"
#include "FileHistory.h"
#include "LaunchFile.h"
#include "Emulator.h"
#include "StrcmpNoCase.h"
#include "AppConfig.h"
#include "ArchFile.h"
#include "SaveState.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/* The SRAM cartridges (MEGA-SCSI, ESE-RAM, ESE-SCC, WAVE-SCSI) are only ever
** named by their short string, so match on that as well. */
static RomType romTypeFromShortString(const char* name) {
    int i;

    for (i = ROM_STANDARD; i <= ROM_MAXROMID; i++) {
        const char* shortName = romTypeToShortString((RomType)i);
        if (shortName != NULL && strcmpnocase(shortName, "UNKNOWN") != 0 &&
            strcmpnocase(shortName, name) == 0) {
            return (RomType)i;
        }
    }

    return ROM_UNKNOWN;
}

static RomType romNameToType(char* name) {
    RomType romType = ROM_UNKNOWN;

    if (name == NULL) {
        return ROM_UNKNOWN;
    }

    romType = mediaDbStringToType(name);

    if (romType == ROM_UNKNOWN) {
        romType = romTypeFromShortString(name);
    }

    if (romType == ROM_UNKNOWN) {
        romType = atoi(name);
        if (romType < ROM_STANDARD || romType > ROM_MAXROMID) {
            romType = ROM_UNKNOWN;
        }
    }

    return romType;
}

static int isRomFileType(char* filename, char* inZip) {
    inZip[0] = 0;

    if (isFileExtension(filename, ".zip")) {
        int count;
        char* fileList;

        fileList = zipGetFileList(filename, ".rom", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".ri", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".mx1", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".mx2", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".sms", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".col", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".sg", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".sc", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        return 0;
    }

    return isFileExtension(filename, ".rom") ||
           isFileExtension(filename, ".ri")  ||
           isFileExtension(filename, ".mx1") ||
           isFileExtension(filename, ".mx2") ||
           isFileExtension(filename, ".sms") ||
           isFileExtension(filename, ".col") ||
           isFileExtension(filename, ".sg") ||
           isFileExtension(filename, ".sc");
}

static int isDskFileType(char* filename, char* inZip) {
    inZip[0] = 0;

    if (isFileExtension(filename, ".zip")) {
        int count;
        char* fileList;

        fileList = zipGetFileList(filename, ".dsk", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".di1", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".di2", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".360", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".720", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        fileList = zipGetFileList(filename, ".sf7", &count);
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }

        return 0;
    }

    return isFileExtension(filename, ".dsk") ||
           isFileExtension(filename, ".di1") ||
           isFileExtension(filename, ".di2") ||
           isFileExtension(filename, ".360") ||
           isFileExtension(filename, ".720") ||
           isFileExtension(filename, ".Sf7");
}

static int isCasFileType(char* filename, char* inZip) {
    inZip[0] = 0;

    if (isFileExtension(filename, ".zip")) {
        int count;
        char* fileList;

        fileList = zipGetFileList(filename, ".cas", &count);
        if (fileList == NULL) {
            fileList = zipGetFileList(filename, ".tsx", &count);
        }
        if (fileList == NULL) {
            fileList = zipGetFileList(filename, ".wav", &count);
        }
        if (fileList) {
            strcpy(inZip, fileList);
            free(fileList);
            return 1;
        }
        return 0;
    }

    return isFileExtension(filename, ".cas") || isFileExtension(filename, ".tsx") ||
           isFileExtension(filename, ".wav");
}

int emuNormalizeOneArg(const char* cmdLine, char* out, int outSize) {
    const char* rest;
    int len;

    if (outSize < 3 || 0 != strncmp(cmdLine, "/onearg ", 8)) {
        return 0;
    }

    rest = cmdLine + 8;
    while (*rest == ' ' || *rest == '\t') rest++;

    len = (int)strlen(rest);
    if (len > outSize - 3) {
        len = outSize - 3;
    }
    memcpy(out, rest, len);
    out[len] = 0;

    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t'
                    || out[len - 1] == '\r' || out[len - 1] == '\n')) {
        out[--len] = 0;
    }

    /* The shell hands over "%1" already quoted and a typed path bare, so strip
    ** whatever is there before putting exactly one pair back on. */
    if (len >= 2 && out[0] == '\"' && out[len - 1] == '\"') {
        memmove(out, out + 1, len - 2);
        len -= 2;
        out[len] = 0;
    }

    memmove(out + 1, out, len + 1);
    out[0] = '\"';
    out[len + 1] = '\"';
    out[len + 2] = 0;

    return 1;
}

int emuArgMatches(const char* arg, const char* value) {
    if (arg[0] != '/' && arg[0] != '-') {
        return 0;
    }

    /* The GNU style --name counts too, so the habit from other tools does not
    ** turn an option into an unknown one. */
    if (arg[0] == '-' && arg[1] == '-') {
        arg++;
    }

    return strcmpnocase(arg + 1, value) == 0;
}

/* extractToken hands back up to 511 characters and the fields it is copied
** into are no larger, so an overlong argument has to truncate here. */
static void copyArg(char* dest, int destSize, const char* src) {
    int len = (int)strlen(src);
    if (len > destSize - 1) {
        len = destSize - 1;
    }
    memcpy(dest, src, len);
    dest[len] = 0;
}

/* One entry per option. value is the placeholder for the token that has to
** follow, and group heads the run of entries that behave alike. */
typedef struct {
    const char* name;
    const char* value;
    const char* help;
    const char* group;
} CmdLineOption;

static const CmdLineOption cmdLineOptions[] = {
    { "help",          NULL,      "Show this list and exit",
      "Show information and exit. The emulator does not start:"                       },
    { "h",             NULL,      NULL                                               },
    { "?",             NULL,      NULL                                               },
    { "listmachines",  NULL,      "List the machines that can be booted"             },
    { "listthemes",    NULL,      "List the themes that can be chosen"               },
    { "listspecials",  NULL,      "List the built-in cartridges"                     },
    { "listromtypes",  NULL,      "List the mappers for a cartridge image"         },
    { "rom1",          "<file>",  "Insert a cartridge image in slot 1",
      "Load media at start. What is loaded is recorded in the history file,\r\n"
      "  the same as when it is chosen from the menu:"                                 },
    { "romtype1",      "<type>",  "Mapper of the /rom1 file; see /listromtypes"      },
    { "rom1zip",       "<name>",  "File inside the /rom1 zip, if it holds several"   },
    { "special1",      "<name>",  "Built-in cartridge in slot 1; see /listspecials"  },
    { "rom2",          "<file>",  "Insert a cartridge image in slot 2"               },
    { "romtype2",      "<type>",  "Mapper of the /rom2 file; see /listromtypes"      },
    { "rom2zip",       "<name>",  "File inside the /rom2 zip, if it holds several"   },
    { "special2",      "<name>",  "Built-in cartridge in slot 2; see /listspecials"  },
    { "diskA",         "<file>",  "Insert a diskette image in drive A"               },
    { "diskAzip",      "<name>",  "File inside the /diskA zip, if it holds several"  },
    { "diskB",         "<file>",  "Insert a diskette image in drive B"               },
    { "diskBzip",      "<name>",  "File inside the /diskB zip, if it holds several"  },
    { "cas",           "<file>",  "Insert a cassette image"                          },
    { "caszip",        "<name>",  "File inside the /cas zip, if it holds several"    },
    { "ide1primary",   "<file>",  "Attach a hard disk image as IDE 1 primary"        },
    { "ide1secondary", "<file>",  "Attach a hard disk image as IDE 1 secondary"      },
    { "state",         "<file>",  "Resume from a saved state instead of booting"     },
    { "scc",           NULL,      "Insert an SCC cartridge in a free slot"           },
    { "sccplus",       NULL,      "Insert an SCC-I cartridge in a free slot"         },
    { "fmpac",         NULL,      "Insert an FM-PAC cartridge in a free slot"        },
    { "pac",           NULL,      "Insert a PAC cartridge in a free slot"            },
    { "extram",        "<kB>",    "External RAM: 16 32 48 64 512 1024 2048 4096"     },
    /* No help text: the shell writes this one, nobody types it. */
    { "onearg",        "<file>",  NULL                                               },
    { "machine",       "<name>",  "Machine to boot; see /listmachines",
      "Override a setting for this run only. Not written to the settings\r\n"
      "  file unless you save the settings from the settings dialog:"                 },
    { "theme",         "<name>",  "Theme to start with; see /listthemes"             },
    { "language",      "<name>",  "Language to start with"                           },
    { "fullscreen",    NULL,      "Start in full screen"                             },
    { "nofullscreen",  NULL,      "Start windowed, overriding the settings file"     },
    { "windowsize",    "<1-8>",   "Window scale to start at"                         },
    { "windowpos",     "<x,y>",   "Position of the top left corner of the window"    },
    { "speed",         "<pct>",   "Emulation speed, 10 to 1000 percent of normal"    },
    { "vdpspeed",      "<pct>",   "VDP command engine timing, 0 to 100 percent"      },
    { "mute",          NULL,      "Start with the sound muted"                       },
    { "hidden",        NULL,      "Run without a window, sound or dialogs"           },
    { "msxmusic",      "<on|off>","Enable or disable MSX-Music (YM2413)"             },
    { "msxaudio",      "<on|off>","Enable or disable MSX-Audio (Y8950)"              },
    { "moonsound",     "<on|off>","Enable or disable MoonSound (OPL4)"               },
    { "debugger",      NULL,      "Open the debugger after the emulator starts",
      "File locations and other options:"                                             },
    { "rootdir",       "<dir>",   "Directory for the settings and everything written" },
    { "machinedir",    "<dir>",   "Directory the machine definitions are read from"  },
    { "inifile",       "<file>",  "Settings file to use instead of bluemsx.ini"      },
    { "reset",         NULL,      "Reset the settings file and start with it"        },
    { "resetregs",     NULL,      "Reset the settings file and exit without starting" },
    { NULL,            NULL,      NULL                                               }
};

/* Anything that does not fit is dropped rather than truncated mid line. */
static void helpAppend(char* out, int size, const char* text) {
    int used = (int)strlen(out);

    if (used + (int)strlen(text) < size - 1) {
        strcat(out, text);
    }
}

int emuCommandLineGetHelpText(char* out, int size) {
    const CmdLineOption* opt;
    char line[256];

    if (out == NULL || size <= 0) {
        return 0;
    }

    out[0] = 0;
    helpAppend(out, size,
        "blueMSX+ command line options\r\n"
        "\r\n"
        "Usage:\r\n"
        "\r\n"
        "  blueMSX+ <file>\r\n"
        "      Open one media file. This is the form used for a double\r\n"
        "      clicked file. If an instance is already running, the file is\r\n"
        "      opened in that instance instead of starting a new one.\r\n"
        "\r\n"
        "  blueMSX+ /resetregs [/rootdir <dir>] [/inifile <file>]\r\n"
        "      Write the built in settings to the settings file and exit. No\r\n"
        "      other option is allowed, because the emulator does not start.\r\n"
        "\r\n"
        "  blueMSX+ [<option> ...]\r\n"
        "      Start the emulator with the options below. A new instance is\r\n"
        "      always started, even if one is already running.\r\n"
        "\r\n"
        "  Options may be written /name, -name or --name, in any case. /help\r\n"
        "  is also -h and /?. Quote names that contain spaces (machine, theme,\r\n"
        "  language and cartridge names do). /rootdir, /machinedir and\r\n"
        "  /inifile take a path relative to the current directory; all other\r\n"
        "  paths are relative to the install directory.\r\n");

    for (opt = cmdLineOptions; opt->name != NULL; opt++) {
        if (opt->help == NULL) {
            continue;
        }
        if (opt->group != NULL) {
            sprintf(line, "\r\n  %s\r\n\r\n", opt->group);
            helpAppend(out, size, line);
        }
        sprintf(line, "  /%-14s %-9s %s\r\n", opt->name,
                opt->value != NULL ? opt->value : "", opt->help);
        helpAppend(out, size, line);
    }
    helpAppend(out, size, "\r\n");

    return (int)strlen(out);
}

static const CmdLineOption* findOption(const char* argument) {
    const CmdLineOption* opt;

    for (opt = cmdLineOptions; opt->name != NULL; opt++) {
        if (emuArgMatches(argument, opt->name)) {
            return opt;
        }
    }

    return NULL;
}

/* A setting the line replaced for this run only, and what the file had before. */
typedef struct {
    int*  intField;
    char* strField;
    int   savedInt;
    char  savedStr[CMDLINE_MAXOVERRIDE];
} CmdLineOverride;

/* Room for every option that can replace a setting, with margin. */
static CmdLineOverride overrides[32];
static int overrideCount = 0;

static CmdLineOverride* newOverride(void) {
    if (overrideCount >= (int)(sizeof(overrides) / sizeof(overrides[0]))) {
        return NULL;
    }
    return &overrides[overrideCount++];
}

/* The field is left alone when the override cannot be recorded, or the value
** would stay in the settings file after restore. */
void emuCommandLineOverrideInt(int* field, int value) {
    CmdLineOverride* ovr = newOverride();

    if (ovr == NULL) {
        return;
    }
    ovr->intField = field;
    ovr->strField = NULL;
    ovr->savedInt = *field;
    *field = value;
}

void emuCommandLineOverrideString(char* field, const char* previous) {
    CmdLineOverride* ovr = newOverride();

    if (ovr != NULL) {
        ovr->intField = NULL;
        ovr->strField = field;
        copyArg(ovr->savedStr, sizeof(ovr->savedStr), previous);
    }
}

void emuCommandLineRestoreOverrides(void) {
    int i;

    /* The order is reversed, because two options can land on one setting:
    ** undoing them in the order they were applied would leave the first value
    ** in place. */
    for (i = overrideCount - 1; i >= 0; i--) {
        CmdLineOverride* ovr = &overrides[i];
        if (ovr->intField != NULL) {
            *ovr->intField = ovr->savedInt;
        }
        if (ovr->strField != NULL) {
            strcpy(ovr->strField, ovr->savedStr);
        }
    }
}

void emuCommandLineDropOverrides(void) {
    overrideCount = 0;
}

static char cmdLineError[640];

static void errAppend(const char* text) {
    int len = (int)strlen(cmdLineError);
    int add = (int)strlen(text);

    if (add > (int)sizeof(cmdLineError) - 1 - len) {
        add = (int)sizeof(cmdLineError) - 1 - len;
    }
    memcpy(cmdLineError + len, text, add);
    cmdLineError[len + add] = 0;
}

/* Always returns 0 so a rejecting branch can be a single return statement. */
static int argError(const char* option, const char* reason, const char* detail) {
    cmdLineError[0] = 0;
    if (option != NULL) {
        errAppend(option);
        errAppend(": ");
    }
    errAppend(reason);
    if (detail != NULL) {
        errAppend(" ");
        errAppend(detail);
    }
    return 0;
}

const char* emuCommandLineGetError(void) {
    return cmdLineError;
}

const char* emuFirstOtherArgument(char* cmdLine, const char* const* allowed) {
    static char offending[512];
    char* argument;
    int i;
    int j;

    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        const CmdLineOption* opt = findOption(argument);
        int permitted = 0;

        for (j = 0; opt != NULL && allowed[j] != NULL; j++) {
            if (emuArgMatches(argument, allowed[j])) {
                permitted = 1;
            }
        }
        if (!permitted) {
            copyArg(offending, sizeof(offending), argument);
            return offending;
        }
        if (opt->value != NULL) {
            i++;
        }
    }

    return NULL;
}

/* Whether any token after the first names a real option. A leading dash will
** not do as the test: "Aleste 2 - Gaiden.rom" is a path. */
static int lineHasOption(char* line) {
    char* argument;
    int i;

    for (i = 1; (argument = extractToken(line, i)) != NULL; i++) {
        if (findOption(argument) != NULL) {
            return 1;
        }
    }

    return 0;
}


int emuCheckHelpArgument(char* cmdLine) {
    return emuCheckFlagArgument(cmdLine, "help") ||
           emuCheckFlagArgument(cmdLine, "h") ||
           emuCheckFlagArgument(cmdLine, "?");
}

int emuCheckFlagArgument(char* cmdLine, const char* name) {
    char* argument;
    int i;

    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        if (emuArgMatches(argument, name)) {
            return 1;
        }
    }

    return 0;
}

char* emuCheckValueArgument(char* cmdLine, const char* name) {
    char* argument;
    int i;

    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        if (emuArgMatches(argument, name)) {
            return extractToken(cmdLine, i + 1);
        }
    }

    return NULL;
}

int emuCheckResetArgument(char* cmdLine) {
    int i;
    int answer = 0;
    char*   argument;

    /* The whole line is scanned rather than stopping at the first of the two:
    ** a line carrying both /reset and /resetregs means /resetregs. */
    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        if (emuArgMatches(argument, "reset") && answer == 0) {
            answer = 1;
        }
        if (emuArgMatches(argument, "resetregs")) {
            return 2;
        }
    }

    return answer;
}



static RomType extRamType(int kilobytes) {
    switch (kilobytes) {
    case 16:   return ROM_EXTRAM16KB;
    case 32:   return ROM_EXTRAM32KB;
    case 48:   return ROM_EXTRAM48KB;
    case 64:   return ROM_EXTRAM64KB;
    case 512:  return ROM_EXTRAM512KB;
    case 1024: return ROM_EXTRAM1MB;
    case 2048: return ROM_EXTRAM2MB;
    case 4096: return ROM_EXTRAM4MB;
    }

    return ROM_UNKNOWN;
}

static int onOffValue(const char* text) {
    if (strcmpnocase(text, "on") == 0 || strcmpnocase(text, "1") == 0 ||
        strcmpnocase(text, "yes") == 0) {
        return 1;
    }
    if (strcmpnocase(text, "off") == 0 || strcmpnocase(text, "0") == 0 ||
        strcmpnocase(text, "no") == 0) {
        return 0;
    }
    return -1;
}

static int settingError(const char* name, const char* reason) {
    char option[64];

    option[0] = '/';
    copyArg(option + 1, sizeof(option) - 1, name);
    return argError(option, reason, NULL);
}

/* The token has to be a whole signed number and nothing else. atoi cannot tell
** a written 0 from a word. */
static int parseIntToken(const char* token, int* value) {
    char* end;
    long parsed;

    if (token == NULL || token[0] == 0) {
        return 0;
    }
    errno = 0;
    parsed = strtol(token, &end, 10);
    /* errno is checked as well as the text, because strtol answers LONG_MAX
    ** for a number too big to hold. */
    if (*end != 0 || errno != 0) {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

static int rangeArgument(char* cmdLine, const char* name, int lo, int hi,
                         int* field, int scaleToLog) {
    char* argument = emuCheckValueArgument(cmdLine, name);
    int value;

    if (argument == NULL) {
        if (emuCheckFlagArgument(cmdLine, name)) {
            return settingError(name, "needs a number");
        }
        return 1;
    }

    if (!parseIntToken(argument, &value)) {
        return settingError(name, "needs a number");
    }
    if (value < lo || value > hi) {
        return settingError(name, "is out of range");
    }

    emuCommandLineOverrideInt(field, scaleToLog ? emulatorPercentToLogFrequency(value) : value);
    return 1;
}

static int chipArgument(char* cmdLine, const char* name, int* field) {
    char* argument = emuCheckValueArgument(cmdLine, name);
    int value;

    if (argument == NULL) {
        if (emuCheckFlagArgument(cmdLine, name)) {
            return settingError(name, "needs on or off");
        }
        return 1;
    }

    value = onOffValue(argument);
    if (value < 0) {
        return settingError(name, "wants on or off");
    }

    emuCommandLineOverrideInt(field, value);
    return 1;
}

/* The scale is 1 based on the command line and 0 based in the property, so
** this cannot go through rangeArgument. */
static int windowSizeArgument(char* cmdLine, Properties* properties) {
    char* argument = emuCheckValueArgument(cmdLine, "windowsize");
    int scale;

    if (argument == NULL) {
        if (emuCheckFlagArgument(cmdLine, "windowsize")) {
            return settingError("windowsize", "needs a scale of 1 to 8");
        }
        return 1;
    }

    if (!parseIntToken(argument, &scale) || scale < 1 || scale > 8) {
        return settingError("windowsize", "wants a scale of 1 to 8");
    }

    emuCommandLineOverrideInt(&properties->video.windowSize, P_VIDEO_SIZEX1 + scale - 1);
    return 1;
}

static int windowPosArgument(char* cmdLine, Properties* properties) {
    char* argument = emuCheckValueArgument(cmdLine, "windowpos");
    const char* comma;
    char first[32];
    int length;
    int x;
    int y;

    if (argument == NULL) {
        if (emuCheckFlagArgument(cmdLine, "windowpos")) {
            return settingError("windowpos", "wants <x>,<y>");
        }
        return 1;
    }

    comma = strchr(argument, ',');
    if (comma == NULL) {
        return settingError("windowpos", "wants <x>,<y>");
    }

    /* The first number ends at the comma rather than at the end of the token,
    ** so it has to be cut out before it can be read on its own. */
    length = (int)(comma - argument);
    if (length >= (int)sizeof(first)) {
        return settingError("windowpos", "wants <x>,<y>");
    }
    memcpy(first, argument, length);
    first[length] = 0;

    if (!parseIntToken(first, &x) || !parseIntToken(comma + 1, &y)) {
        return settingError("windowpos", "wants <x>,<y>");
    }

    emuCommandLineOverrideInt(&properties->video.windowX, x);
    emuCommandLineOverrideInt(&properties->video.windowY, y);
    return 1;
}

int emuCheckSettingArguments(Properties* properties, char* cmdLine) {
    if (!rangeArgument(cmdLine, "speed", 10, 1000, &properties->emulation.speed, 1)) {
        return 0;
    }
    if (!rangeArgument(cmdLine, "vdpspeed", 0, 100, &properties->emulation.vdpCmdSpeed, 0)) {
        return 0;
    }
    if (!chipArgument(cmdLine, "msxmusic", &properties->sound.chip.enableYM2413) ||
        !chipArgument(cmdLine, "msxaudio", &properties->sound.chip.enableY8950) ||
        !chipArgument(cmdLine, "moonsound", &properties->sound.chip.enableMoonsound)) {
        return 0;
    }

    if (emuCheckFlagArgument(cmdLine, "mute")) {
        emuCommandLineOverrideInt(&properties->sound.masterEnable, 0);
    }

    if (emuCheckFlagArgument(cmdLine, "fullscreen")) {
        emuCommandLineOverrideInt(&properties->video.windowSize, P_VIDEO_SIZEFULLSCREEN);
    }
    if (!windowSizeArgument(cmdLine, properties)) {
        return 0;
    }
    /* This runs after the size, so a line that says both ends up windowed. */
    if (emuCheckFlagArgument(cmdLine, "nofullscreen") &&
        properties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        emuCommandLineOverrideInt(&properties->video.windowSize, P_VIDEO_SIZEX2);
    }
    if (!windowPosArgument(cmdLine, properties)) {
        return 0;
    }

    return 1;
}

/* The machine is a setting like any other, so /machine lasts for this run
** only. */
static void overrideMachineName(Properties* properties, const char* machineName) {
    char previous[CMDLINE_MAXOVERRIDE];

    if (!strlen(machineName)) {
        return;
    }
    copyArg(previous, sizeof(previous), properties->emulation.machineName);
    strcpy(properties->emulation.machineName, machineName);
    emuCommandLineOverrideString(properties->emulation.machineName, previous);
}

static int launchBareFile(Properties* properties, char* fileName) {
    int i;

    if (*fileName == '\"') fileName++;
    if (*fileName == 0) {
        return argError(NULL, "Empty file name", NULL);
    }

    /* This name is checked before the slots below are emptied, because the
    ** media options check theirs. */
    if (!archFileExists(fileName)) {
        return argError(NULL, "No such file:", fileName);
    }

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        properties->media.carts[i].fileName[0] = 0;
        properties->media.carts[i].fileNameInZip[0] = 0;
        properties->media.carts[i].type = ROM_UNKNOWN;
        updateExtendedRomName(i, properties->media.carts[i].fileName, properties->media.carts[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_DISKS; i++) {
        properties->media.disks[i].fileName[0] = 0;
        properties->media.disks[i].fileNameInZip[0] = 0;
        updateExtendedDiskName(i, properties->media.disks[i].fileName, properties->media.disks[i].fileNameInZip);
    }

    /* Only a plain refusal is reported: -1 means the user was asked and
    ** declined, and saying so again over the dialog would read as a fault. */
    if (tryLaunchUnknownFile(properties, fileName, 1) == 0) {
        return argError(NULL, "Cannot open:", fileName);
    }

    return 1;
}

/* The insert takes the entry name as it stands and reports success even when
** the zip holds no such entry. Checked after the whole line, because the file
** options refill these while they read a zip. */
static int checkZipEntry(char* cmdLine, const char* name, const char* entry,
                         const char* fileOption, const char* fileName) {
    char option[64];
    char* typed = emuCheckValueArgument(cmdLine, name);

    /* The value as typed is tested, because the matching file option may
    ** already have refilled the field. */
    if (typed == NULL || typed[0] == 0) {
        return 1;
    }

    option[0] = '/';
    copyArg(option + 1, sizeof(option) - 1, name);

    if (fileName[0] == 0) {
        return argError(option, "names an entry in the zip given with", fileOption);
    }
    if (!isFileExtension((char*)fileName, ".zip")) {
        return argError(option, "only applies to a zip file:", fileName);
    }
    /* A * followed by a three character extension stands for the name of the
    ** zip itself, so a shorter entry would index before the start of it. */
    if ((entry[0] == '*' && strlen(entry) < 4) || !zipFileExists(fileName, entry)) {
        return argError(option, "no such entry in the zip:", entry);
    }

    return 1;
}

static int emuStartWithArguments(Properties* properties, char* commandLine, char *gamedir) {
    int i;
    char    cmdLine[CMDLINE_MAXLEN];
    char*   argument;
    char    rom1[512] = "";
    char    rom2[512] = "";
    char    rom1zip[256] = "";
    char    rom2zip[256] = "";
    RomType romType1  = ROM_UNKNOWN;
    RomType romType2  = ROM_UNKNOWN;
    char    machineName[64] = "";
    char    diskA[512] = "";
    char    diskB[512] = "";
    char    diskAzip[256] = "";
    char    diskBzip[256] = "";
    char    ide1p[256] = "";
    char    ide1s[256] = "";
    char    cas[512] = "";
    char    caszip[256] = "";
    char    stateFile[512] = "";
    char    bareFile[512] = "";
    RomType builtins[PROP_MAX_CARTS];
    int     builtinCount = 0;
    RomType specialType1 = ROM_UNKNOWN;
    RomType specialType2 = ROM_UNKNOWN;
    int     romTypeGiven1 = 0;
    int     romTypeGiven2 = 0;
#ifdef WII
    int     startEmu = 1; // always start
#else
    int     startEmu = 0;
#endif

    cmdLine[0] = 0;

    /* The path is quoted so a name with spaces stays one token. A drive letter
    ** is not the test: a UNC or relative path has none. */
    if (commandLine[0] != 0 && commandLine[0] != '/' && commandLine[0] != '-' &&
        commandLine[0] != '\"' && !lineHasOption(commandLine)) {
        int len;
        cmdLine[0] = '\"';
        cmdLine[1] = 0;
        strncat(cmdLine, commandLine, sizeof(cmdLine) - 4);
        len = (int)strlen(cmdLine);
        while (len > 1 && (cmdLine[len - 1] == ' ' || cmdLine[len - 1] == '\t')) {
            cmdLine[--len] = 0;
        }
        cmdLine[len] = '\"';
        cmdLine[len + 1] = 0;
    }
    else {
        strncat(cmdLine, commandLine, sizeof(cmdLine) - 1);
    }

    // If one argument, assume it is a rom or disk to run
    if (!extractToken(cmdLine, 1)) {
        argument = extractToken(cmdLine, 0);

        /* The dash forms are options too, so a line that is only -mute is not
        ** a file. */
        if (argument && *argument != '/' && *argument != '-') {
            return launchBareFile(properties, argument);
        }
    }

    // If more than one argument, check arguments,
    // set configuration and then run

    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        const CmdLineOption* opt;
        RomType builtin = ROM_UNKNOWN;
        char option[64];

        if (argument[0] != '/' && argument[0] != '-') {
            if (bareFile[0]) {
                return argError(NULL, "More than one file to open:", argument);
            }
            copyArg(bareFile, sizeof(bareFile), argument);
            startEmu = 1;
            continue;
        }
        /* The name is kept because the branches below overwrite argument with
        ** the value, and an error about the value has to name its option. */
        copyArg(option, sizeof(option), argument);

        if (emuArgMatches(argument, "rom1")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isRomFileType(argument, rom1zip)) return argError(option, "not a ROM image:", argument);
            if (!archFileExists(argument)) return argError(option, "no such file:", argument);
            copyArg(rom1, sizeof(rom1), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "rom1zip")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs an entry name", NULL);
            copyArg(rom1zip, sizeof(rom1zip), argument);
            continue;
        }
        if (emuArgMatches(argument, "romtype1")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs a mapper name", NULL);
            romType1 = romNameToType(argument);
            if (romType1 == ROM_UNKNOWN) return argError(option, "unknown mapper:", argument);
            romTypeGiven1 = 1;
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "rom2")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isRomFileType(argument, rom2zip)) return argError(option, "not a ROM image:", argument);
            if (!archFileExists(argument)) return argError(option, "no such file:", argument);
            copyArg(rom2, sizeof(rom2), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "rom2zip")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs an entry name", NULL);
            copyArg(rom2zip, sizeof(rom2zip), argument);
            continue;
        }
        if (emuArgMatches(argument, "romtype2")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs a mapper name", NULL);
            romType2 = romNameToType(argument);
            if (romType2 == ROM_UNKNOWN) return argError(option, "unknown mapper:", argument);
            romTypeGiven2 = 1;
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "diskA")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isDskFileType(argument, diskAzip)) return argError(option, "not a disk image:", argument);
            if (!archFileExists(argument)) return argError(option, "no such file:", argument);
            copyArg(diskA, sizeof(diskA), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "diskAzip")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs an entry name", NULL);
            copyArg(diskAzip, sizeof(diskAzip), argument);
            continue;
        }
        if (emuArgMatches(argument, "diskB")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isDskFileType(argument, diskBzip)) return argError(option, "not a disk image:", argument);
            if (!archFileExists(argument)) return argError(option, "no such file:", argument);
            copyArg(diskB, sizeof(diskB), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "diskBzip")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs an entry name", NULL);
            copyArg(diskBzip, sizeof(diskBzip), argument);
            continue;
        }
        if (emuArgMatches(argument, "cas")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isCasFileType(argument, caszip)) return argError(option, "not a cassette image:", argument);
            if (!archFileExists(argument)) return argError(option, "no such file:", argument);
            copyArg(cas, sizeof(cas), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "caszip")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs an entry name", NULL);
            copyArg(caszip, sizeof(caszip), argument);
            continue;
        }
        if (emuArgMatches(argument, "ide1primary")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            copyArg(ide1p, sizeof(ide1p), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "ide1secondary")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            copyArg(ide1s, sizeof(ide1s), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "special1") || emuArgMatches(argument, "special2")) {
            int slot2 = emuArgMatches(argument, "special2");
            RomType special;
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs a cartridge name", NULL);
            special = romNameToType(argument);
            /* A mapper name resolves too, so the cartridge has to be one that
            ** exists without a file of its own. /listspecials prints the set. */
            if (special == ROM_UNKNOWN || romTypeListCartName(special) == NULL) {
                /* Quoting is mentioned because half of what /listspecials
                ** prints has a space. */
                return argError(option, "no built-in cartridge is called (quote a name with spaces):", argument);
            }
            if (slot2) { specialType2 = special; } else { specialType1 = special; }
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "state")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!archFileExists(argument)) return argError(option, "no such file:", argument);
            if (!saveStateFileIsState(argument)) {
                return argError(option, "not a saved state:", argument);
            }
            /* The name is copied out, because emulatorStart is reached long
            ** after this token buffer has been handed to somebody else. */
            copyArg(stateFile, sizeof(stateFile), argument);
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "extram")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs a size in kB", NULL);
            builtin = extRamType(atoi(argument));
            if (builtin == ROM_UNKNOWN) return argError(option, "no such size:", argument);
        }
        else if (emuArgMatches(argument, "scc"))     { builtin = ROM_SCC;     }
        else if (emuArgMatches(argument, "sccplus")) { builtin = ROM_SCCPLUS; }
        else if (emuArgMatches(argument, "fmpac"))   { builtin = ROM_FMPAC;   }
        else if (emuArgMatches(argument, "pac"))     { builtin = ROM_PAC;     }

        if (builtin != ROM_UNKNOWN) {
            /* This is left until the whole line is read, because which slot is
            ** free depends on what the rest of it names. */
            if (builtinCount >= (int)(sizeof(builtins) / sizeof(builtins[0]))) {
                return argError(option, "no cartridge slot left", NULL);
            }
            builtins[builtinCount++] = builtin;
            startEmu = 1;
            continue;
        }

        if (emuArgMatches(argument, "machine")) {
            argument = extractToken(cmdLine, ++i);
            if (argument == NULL) return argError(option, "needs a machine name", NULL);
            copyArg(machineName, sizeof(machineName), argument);
            /* machineIsValid with the roms unchecked separates a machine whose
            ** dumps are missing from an unknown name. */
            if (!machineIsValid(machineName, 1)) {
                if (machineIsValid(machineName, 0)) {
                    return argError(option, "rom files are not installed for:", machineName);
                }
                return argError(option, "unknown machine:", machineName);
            }
            startEmu = 1;
            continue;
        }
        /* Options another pass reads (/language, /theme, ...) are stepped over
        ** here, or their value would be taken for an option of its own. */
        opt = findOption(argument);
        if (opt == NULL) {
            return argError(NULL, "Unknown option:", argument);
        }
        if (opt->value != NULL && extractToken(cmdLine, ++i) == NULL) {
            return argError(option, "needs a value", NULL);
        }
    }

    /* Only the settled values are meaningful: one of these can be written
    ** before its file option clears and refills it. */
    if (!checkZipEntry(cmdLine, "rom1zip",  rom1zip,  "/rom1",  rom1)  ||
        !checkZipEntry(cmdLine, "rom2zip",  rom2zip,  "/rom2",  rom2)  ||
        !checkZipEntry(cmdLine, "diskAzip", diskAzip, "/diskA", diskA) ||
        !checkZipEntry(cmdLine, "diskBzip", diskBzip, "/diskB", diskB) ||
        !checkZipEntry(cmdLine, "caszip",   caszip,   "/cas",   cas)) {
        return 0;
    }

    if (!startEmu) {
        return 1;
    }

    /* It clears the slots and starts the emulator itself, so it cannot share
    ** the line with an option that puts something in one. */
    if (bareFile[0]) {
        if (strlen(rom1) || strlen(rom2) || strlen(diskA) || strlen(diskB) ||
            strlen(cas) || strlen(ide1p) || strlen(ide1s) || builtinCount ||
            strlen(stateFile) || romTypeGiven1 || romTypeGiven2 ||
            specialType1 != ROM_UNKNOWN || specialType2 != ROM_UNKNOWN) {
            return argError(NULL, "Cannot be combined with the other media:", bareFile);
        }
        overrideMachineName(properties, machineName);
        return launchBareFile(properties, bareFile);
    }

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        properties->media.carts[i].fileName[0] = 0;
        properties->media.carts[i].fileNameInZip[0] = 0;
        properties->media.carts[i].type = ROM_UNKNOWN;
        updateExtendedRomName(i, properties->media.carts[i].fileName, properties->media.carts[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_DISKS; i++) {
        properties->media.disks[i].fileName[0] = 0;
        properties->media.disks[i].fileNameInZip[0] = 0;
        updateExtendedDiskName(i, properties->media.disks[i].fileName, properties->media.disks[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_TAPES; i++) {
        properties->media.tapes[i].fileName[0] = 0;
        properties->media.tapes[i].fileNameInZip[0] = 0;
        updateExtendedCasName(i, properties->media.tapes[i].fileName, properties->media.tapes[i].fileNameInZip);
    }

    /* /special names a whole cartridge and /romtype only says how to read a
    ** file, so one slot cannot be given both. */
    if (specialType1 != ROM_UNKNOWN) {
        if (romTypeGiven1) return argError("/special1", "cannot be used with /romtype1", NULL);
        if (strlen(rom1))  return argError("/special1", "slot 1 already holds", rom1);
        romType1 = specialType1;
    }
    if (specialType2 != ROM_UNKNOWN) {
        if (romTypeGiven2) return argError("/special2", "cannot be used with /romtype2", NULL);
        if (strlen(rom2))  return argError("/special2", "slot 2 already holds", rom2);
        romType2 = specialType2;
    }

    if (romTypeGiven1 && !strlen(rom1)) {
        return argError("/romtype1", "describes the file in /rom1; for a built-in cartridge use /special1", NULL);
    }
    if (romTypeGiven2 && !strlen(rom2)) {
        return argError("/romtype2", "describes the file in /rom2; for a built-in cartridge use /special2", NULL);
    }

    /* The cartridges named by the line go into whichever slot it did not spell
    ** out. */
    for (i = 0; i < builtinCount; i++) {
        if (!strlen(rom1) && romType1 == ROM_UNKNOWN) {
            romType1 = builtins[i];
        }
        else if (!strlen(rom2) && romType2 == ROM_UNKNOWN) {
            romType2 = builtins[i];
        }
        else {
            return argError(NULL, "No free cartridge slot for", romTypeListCartName(builtins[i]));
        }
    }

    if (!strlen(rom1)) {
        const char* name = romTypeListCartName(romType1);
        if (name != NULL) copyArg(rom1, sizeof(rom1), name);
    }

    if (!strlen(rom2)) {
        const char* name = romTypeListCartName(romType2);
        if (name != NULL) copyArg(rom2, sizeof(rom2), name);
    }

    if (properties->cassette.rewindAfterInsert) tapeRewindNextInsert();

    if (strlen(rom1)  && !insertCartridge(properties, 0, rom1, *rom1zip ? rom1zip : NULL, romType1, -1)) return argError("/rom1", "cannot insert", rom1);
    if (strlen(rom2)  && !insertCartridge(properties, 1, rom2, *rom2zip ? rom2zip : NULL, romType2, -1)) return argError("/rom2", "cannot insert", rom2);
    if (strlen(diskA) && !insertDiskette(properties, 0, diskA, *diskAzip ? diskAzip : NULL, -1)) return argError("/diskA", "cannot insert", diskA);
    if (strlen(diskB) && !insertDiskette(properties, 1, diskB, *diskBzip ? diskBzip : NULL, -1)) return argError("/diskB", "cannot insert", diskB);
    if (strlen(ide1p) && !insertDiskette(properties, diskGetHdDriveId(0, 0), ide1p, NULL, -1)) return argError("/ide1primary", "cannot attach", ide1p);
    if (strlen(ide1s) && !insertDiskette(properties, diskGetHdDriveId(0, 1), ide1s, NULL, -1)) return argError("/ide1secondary", "cannot attach", ide1s);
    if (strlen(cas)   && !insertCassette(properties, 0, cas, *caszip ? caszip : NULL, -1)) return argError("/cas", "cannot insert", cas);

    overrideMachineName(properties, machineName);
#ifdef WII
    if (!strlen(machineName)) strcpy(properties->emulation.machineName, "MSX2 - No Moonsound"); /* If not specified, use MSX2 without moonsound as default */
#endif

    emulatorStop();
    emulatorStart(strlen(stateFile) ? stateFile : NULL);

    return 1;
}

int emuTryStartWithArguments(Properties* properties, char* cmdLine, char *gamedir) {
    cmdLineError[0] = 0;

    if (cmdLine == NULL || *cmdLine == 0) {
        if (appConfigGetInt("autostart", 0) != 0) {
            emulatorStop();
            emulatorStart(properties->filehistory.quicksave);
        }
        return 0;
    }

    if (*cmdLine) {
        char args[CMDLINE_MAXLEN];
        int success;
        if (emuNormalizeOneArg(cmdLine, args, sizeof(args))) {
            success = emuStartWithArguments(properties, args, gamedir);
        }
        else {
            success = emuStartWithArguments(properties, cmdLine, gamedir);
        }
        if (!success) {
            return -1;
        }
    }

    return 1;
}
