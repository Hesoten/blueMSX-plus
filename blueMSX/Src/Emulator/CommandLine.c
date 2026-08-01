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
#include <stdlib.h>
#include <string.h>

static RomType romNameToType(char* name) {
    RomType romType = ROM_UNKNOWN;

    if (name == NULL) {
        return ROM_UNKNOWN;
    }

    romType = mediaDbStringToType(name);

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
    { "rom1",          "<file>",  "Insert a cartridge image in slot 1",
      "Load media at start. What is loaded is recorded in the history file,\r\n"
      "  the same as when it is chosen from the menu:"                                 },
    { "romtype1",      "<type>",  "Mapper of the /rom1 file"                         },
    { "rom1zip",       "<name>",  "File inside the /rom1 zip, if it holds several"   },
    { "rom2",          "<file>",  "Insert a cartridge image in slot 2"               },
    { "romtype2",      "<type>",  "Mapper of the /rom2 file"                         },
    { "rom2zip",       "<name>",  "File inside the /rom2 zip, if it holds several"   },
    { "diskA",         "<file>",  "Insert a diskette image in drive A"               },
    { "diskAzip",      "<name>",  "File inside the /diskA zip, if it holds several"  },
    { "diskB",         "<file>",  "Insert a diskette image in drive B"               },
    { "diskBzip",      "<name>",  "File inside the /diskB zip, if it holds several"  },
    { "cas",           "<file>",  "Insert a cassette image"                          },
    { "caszip",        "<name>",  "File inside the /cas zip, if it holds several"    },
    { "ide1primary",   "<file>",  "Attach a hard disk image as IDE 1 primary"        },
    { "ide1secondary", "<file>",  "Attach a hard disk image as IDE 1 secondary"      },
    /* No help text: the shell writes this one, nobody types it. */
    { "onearg",        "<file>",  NULL                                               },
    { "machine",       "<name>",  "Machine to boot, as named under Machines",
      "Change a setting. The new value is written to the settings file when\r\n"
      "  the emulator exits:"                                                          },
    { "theme",         "<name>",  "Theme to start with"                              },
    { "language",      "<name>",  "Language to start with"                           },
    { "fullscreen",    NULL,      "Start in full screen"                             },
    { "reset",         NULL,      "Reset the settings file and start with it",
      "Other options:"                                                                },
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
        "blueMSX+ command line options\r\n\r\n"
        "  Options may be written /name, -name or --name, in any case. /help\r\n"
        "  is also -h and /?. Quote names that contain spaces (machine, theme\r\n"
        "  and language names do).\r\n"
        "  A line that is nothing but a file name opens that file.\r\n\r\n");

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
    char*   argument;

    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        if (emuArgMatches(argument, "reset")) {
            return 1;
        }
        if (emuArgMatches(argument, "resetregs")) {
            return 2;
        }
    }

    return 0;
}


void emuCheckFullscreenArgument(Properties* properties, char* cmdLine){
    int i;
    char* argument;

    if (NULL == extractToken(cmdLine, 0)) {
        return;
    }

//    properties->video.windowSize = P_VIDEO_SIZEX2;

    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        if (emuArgMatches(argument, "fullscreen")) {
            properties->video.windowSize = P_VIDEO_SIZEFULLSCREEN;
        }
    }
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

        if (argument && *argument != '/') {
            if (*argument == '\"') argument++;

            if (*argument) {
                int i;

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

                if (!tryLaunchUnknownFile(properties, argument, 1)) {
                    return argError(NULL, "Cannot open:", argument);
                }
                return 1;
            }
            return argError(NULL, "Empty file name", NULL);
        }
    }

    // If more than one argument, check arguments,
    // set configuration and then run

    for (i = 0; (argument = extractToken(cmdLine, i)) != NULL; i++) {
        const CmdLineOption* opt;
        char option[64];

        if (argument[0] != '/' && argument[0] != '-') {
            return argError(NULL, "Not an option:", argument);
        }
        /* The name is kept because the branches below overwrite argument with
        ** the value, and an error about the value has to name its option. */
        copyArg(option, sizeof(option), argument);

        if (emuArgMatches(argument, "rom1")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isRomFileType(argument, rom1zip)) return argError(option, "not a ROM image:", argument);
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
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "rom2")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isRomFileType(argument, rom2zip)) return argError(option, "not a ROM image:", argument);
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
            startEmu = 1;
            continue;
        }
        if (emuArgMatches(argument, "diskA")) {
            argument = extractTokenEx(cmdLine, ++i, gamedir);
            if (argument == NULL) return argError(option, "needs a file name", NULL);
            if (!isDskFileType(argument, diskAzip)) return argError(option, "not a disk image:", argument);
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

    if (!startEmu) {
        return 1;
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

    if (!strlen(rom1)) {
        switch (romType1) {
        case ROM_SCC:         strcat(rom1, CARTNAME_SCC); romType1 = ROM_SCC; break;
        case ROM_SCCPLUS:     strcat(rom1, CARTNAME_SCCPLUS); romType1 = ROM_SCCPLUS; break;
        case ROM_SNATCHER:    strcat(rom1, CARTNAME_SNATCHER); break;
        case ROM_SDSNATCHER:  strcat(rom1, CARTNAME_SDSNATCHER); break;
        case ROM_SCCMIRRORED: strcat(rom1, CARTNAME_SCCMIRRORED); break;
        case ROM_SCCEXTENDED: strcat(rom1, CARTNAME_SCCEXPANDED); break;
        case ROM_FMPAC:       strcat(rom1, CARTNAME_FMPAC); break;
        case ROM_PAC:         strcat(rom1, CARTNAME_PAC); break;
        case ROM_GAMEREADER:  strcat(rom1, CARTNAME_GAMEREADER); break;
        case ROM_SUNRISEIDE:  strcat(rom1, CARTNAME_SUNRISEIDE); break;
        case ROM_NOWIND:      strcat(rom1, CARTNAME_NOWINDDOS1); break;
        case ROM_BEERIDE:     strcat(rom1, CARTNAME_BEERIDE); break;
        case ROM_GIDE:        strcat(rom1, CARTNAME_GIDE); break;
        case ROM_GOUDASCSI:   strcat(rom1, CARTNAME_GOUDASCSI); break;
        case ROM_NMS1210:     strcat(rom1, CARTNAME_NMS1210); break;
        case ROM_SONYHBI55:   strcat(rom1, CARTNAME_SONYHBI55); break;
        case ROM_MEGAFLSHSCC: strcat(rom1, CARTNAME_MEGAFLSHSCC); break;
        case ROM_MEGAFLSHSCCPLUS:   strcat(rom1, CARTNAME_MEGAFLSHSCCPLUS); break;
        case ROM_MEGAFLSHSCCPLUS_SD: strcat(rom1, CARTNAME_MEGAFLSHSCCPLUS_SD); break;
        case ROM_ASCII16X:    strcat(rom1, CARTNAME_ASCII16X); break;
        case ROM_YAMANOOTO:   strcat(rom1, CARTNAME_YAMANOOTO); break;
        case ROM_FLASHROMSCC: strcat(rom1, CARTNAME_FLASHROMSCC); break;
        }
    }

    if (!strlen(rom2)) {
        switch (romType2) {
        case ROM_SCC:         strcat(rom2, CARTNAME_SCC); romType2 = ROM_SCC; break;
        case ROM_SCCPLUS:     strcat(rom2, CARTNAME_SCCPLUS); romType2 = ROM_SCCPLUS; break;
        case ROM_SNATCHER:    strcat(rom2, CARTNAME_SNATCHER); break;
        case ROM_SDSNATCHER:  strcat(rom2, CARTNAME_SDSNATCHER); break;
        case ROM_SCCMIRRORED: strcat(rom2, CARTNAME_SCCMIRRORED); break;
        case ROM_SCCEXTENDED: strcat(rom2, CARTNAME_SCCEXPANDED); break;
        case ROM_FMPAC:       strcat(rom2, CARTNAME_FMPAC); break;
        case ROM_PAC:         strcat(rom2, CARTNAME_PAC); break;
        case ROM_GAMEREADER:  strcat(rom2, CARTNAME_GAMEREADER); break;
        case ROM_SUNRISEIDE:  strcat(rom2, CARTNAME_SUNRISEIDE); break;
        case ROM_NOWIND:      strcat(rom2, CARTNAME_NOWINDDOS1); break;
        case ROM_BEERIDE:     strcat(rom2, CARTNAME_BEERIDE); break;
        case ROM_GIDE:        strcat(rom2, CARTNAME_GIDE); break;
        case ROM_GOUDASCSI:   strcat(rom2, CARTNAME_GOUDASCSI); break;
        case ROM_NMS1210:     strcat(rom2, CARTNAME_NMS1210); break;
        case ROM_SONYHBI55:   strcat(rom2, CARTNAME_SONYHBI55); break;
        case ROM_MEGAFLSHSCC: strcat(rom2, CARTNAME_MEGAFLSHSCC); break;
        case ROM_MEGAFLSHSCCPLUS:   strcat(rom2, CARTNAME_MEGAFLSHSCCPLUS); break;
        case ROM_MEGAFLSHSCCPLUS_SD: strcat(rom2, CARTNAME_MEGAFLSHSCCPLUS_SD); break;
        case ROM_ASCII16X:    strcat(rom2, CARTNAME_ASCII16X); break;
        case ROM_YAMANOOTO:   strcat(rom2, CARTNAME_YAMANOOTO); break;
        case ROM_FLASHROMSCC: strcat(rom2, CARTNAME_FLASHROMSCC); break;
        }
    }

    if (properties->cassette.rewindAfterInsert) tapeRewindNextInsert();

    if (strlen(rom1)  && !insertCartridge(properties, 0, rom1, *rom1zip ? rom1zip : NULL, romType1, -1)) return argError("/rom1", "cannot insert", rom1);
    if (strlen(rom2)  && !insertCartridge(properties, 1, rom2, *rom2zip ? rom2zip : NULL, romType2, -1)) return argError("/rom2", "cannot insert", rom2);
    if (strlen(diskA) && !insertDiskette(properties, 0, diskA, *diskAzip ? diskAzip : NULL, -1)) return argError("/diskA", "cannot insert", diskA);
    if (strlen(diskB) && !insertDiskette(properties, 1, diskB, *diskBzip ? diskBzip : NULL, -1)) return argError("/diskB", "cannot insert", diskB);
    if (strlen(ide1p) && !insertDiskette(properties, diskGetHdDriveId(0, 0), ide1p, NULL, -1)) return argError("/ide1primary", "cannot attach", ide1p);
    if (strlen(ide1s) && !insertDiskette(properties, diskGetHdDriveId(0, 1), ide1s, NULL, -1)) return argError("/ide1secondary", "cannot attach", ide1s);
    if (strlen(cas)   && !insertCassette(properties, 0, cas, *caszip ? caszip : NULL, -1)) return argError("/cas", "cannot insert", cas);

    if (strlen(machineName)) strcpy(properties->emulation.machineName, machineName);
#ifdef WII
    else strcpy(properties->emulation.machineName, "MSX2 - No Moonsound"); /* If not specified, use MSX2 without moonsound as default */
#endif

    emulatorStop();
    emulatorStart(NULL);

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
