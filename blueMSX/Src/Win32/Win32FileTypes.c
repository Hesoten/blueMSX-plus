/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32FileTypes.c,v $
**
** $Revision: 1.3 $
**
** $Date: 2008-03-30 18:38:48 $
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
#include "Win32FileTypes.h"
#include <stdio.h>
#include <shlobj.h>

/* HKCU\Software\Classes (no elevation); OpenWithProgids instead of
** overwriting the extension default so existing handlers stay live. */

static const char REG_CLASSES_ROOT[] = "Software\\Classes";

static LONG openClassesSub(const char* sub, REGSAM access, HKEY* out)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s", REG_CLASSES_ROOT, sub);
    return RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL,
                           REG_OPTION_NON_VOLATILE, access,
                           NULL, out, NULL);
}

static LONG writeStringValue(HKEY parent, const char* sub,
                             const char* valueName, const char* data)
{
    HKEY hKey;
    LONG rv = openClassesSub(sub, KEY_WRITE, &hKey);
    if (rv != ERROR_SUCCESS) {
        return rv;
    }
    rv = RegSetValueExA(hKey, valueName, 0, REG_SZ, (const BYTE*)data,
                       (DWORD)(strlen(data) + 1));
    RegCloseKey(hKey);
    return rv;
}

/* Strip any leftover blueMSX* ProgID values from <ext>\OpenWithProgids
** so renamed ProgIDs don't linger in the registry. */
static void purgeStaleBlueMsxProgIds(const char* extension)
{
    char path[MAX_PATH];
    HKEY hKey;
    char namesToDelete[16][256];
    int delCount = 0;
    DWORD i = 0;

    snprintf(path, sizeof(path), "%s\\OpenWithProgids", extension);
    if (openClassesSub(path, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return;
    }

    for (;;) {
        char valueName[256];
        DWORD valueLen = sizeof(valueName);
        if (RegEnumValueA(hKey, i, valueName, &valueLen, NULL, NULL, NULL, NULL)
                != ERROR_SUCCESS) {
            break;
        }
        if (strncmp(valueName, "blueMSX", 7) == 0 && delCount < 16) {
            strcpy(namesToDelete[delCount++], valueName);
        }
        i++;
    }

    {
        int j;
        for (j = 0; j < delCount; j++) {
            RegDeleteValueA(hKey, namesToDelete[j]);
        }
    }
    RegCloseKey(hKey);
}

static void deleteClassesSub(const char* sub)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s", REG_CLASSES_ROOT, sub);
    RegDeleteTreeA(HKEY_CURRENT_USER, path);
}

BOOL registerFileType(char* extension, char* appName, char* description, int iconIndex)
{
    char path[MAX_PATH];
    char fileName[MAX_PATH];
    char buffer[MAX_PATH + 32];

    GetModuleFileNameA(GetModuleHandle(NULL), fileName, MAX_PATH);

    /* ProgID class: HKCU\Software\Classes\blueMSXdsk */
    writeStringValue(HKEY_CURRENT_USER, appName, NULL, description);

    snprintf(path, sizeof(path), "%s\\DefaultIcon", appName);
    snprintf(buffer, sizeof(buffer), "%s,%d", fileName, iconIndex);
    writeStringValue(HKEY_CURRENT_USER, path, NULL, buffer);

    snprintf(path, sizeof(path), "%s\\Shell\\Open\\command", appName);
    /* Quote %1 so spaces in the path survive; /onearg accepts both. */
    snprintf(buffer, sizeof(buffer), "\"%s\" /onearg \"%%1\"", fileName);
    writeStringValue(HKEY_CURRENT_USER, path, NULL, buffer);

    /* Strip stale blueMSX* ProgIDs before adding the current one. */
    purgeStaleBlueMsxProgIds(extension);

    /* Non-destructive extension registration: list our ProgID under
    ** the extension's OpenWithProgids subkey. The extension's own
    ** default value is left alone so existing handlers keep working. */
    snprintf(path, sizeof(path), "%s\\OpenWithProgids", extension);
    writeStringValue(HKEY_CURRENT_USER, path, appName, "");

    return TRUE;
}

BOOL unregisterFileType(char* extension, char* appName, char* description, int iconIndex)
{
    (void)description;
    (void)iconIndex;

    /* Strip every blueMSX* ProgID, not just appName, so older
    ** renamed ProgIDs are scrubbed too. */
    purgeStaleBlueMsxProgIds(extension);

    /* Drop the ProgID class entirely. */
    deleteClassesSub(appName);

    return TRUE;
}

/* Returns the bare exe name (e.g. "blueMSX+.exe") of the running
** process. Caller-owned static buffer. */
static const char* exeBaseName(void)
{
    static char baseName[MAX_PATH];
    char fullPath[MAX_PATH];
    char* slash;

    GetModuleFileNameA(GetModuleHandle(NULL), fullPath, MAX_PATH);
    slash = strrchr(fullPath, '\\');
    strcpy(baseName, slash ? slash + 1 : fullPath);
    return baseName;
}

/* Register the exe under HKCU\Software\Classes\Applications so the
** Open With dialog finds it with the right /onearg command line plus
** Friendly App Name and SupportedTypes list. */
void registerApplicationOpenWith(void)
{
    const char* exe = exeBaseName();
    char fullExe[MAX_PATH];
    char appPath[MAX_PATH];
    char path[MAX_PATH];
    char buffer[MAX_PATH + 32];
    static const char* const supported[] = {
        ".dsk", ".di1", ".di2", ".360", ".720", ".sf7",
        ".rom", ".ri",  ".mx1", ".mx2",
        ".sms", ".sg",  ".sc",  ".col",
        ".cas", ".tsx", ".sta", ".cap", NULL
    };
    int i;

    GetModuleFileNameA(GetModuleHandle(NULL), fullExe, MAX_PATH);
    snprintf(appPath, sizeof(appPath), "Applications\\%s", exe);

    snprintf(path, sizeof(path), "%s\\shell\\open\\command", appPath);
    snprintf(buffer, sizeof(buffer), "\"%s\" /onearg \"%%1\"", fullExe);
    writeStringValue(HKEY_CURRENT_USER, path, NULL, buffer);

    snprintf(path, sizeof(path), "%s\\FriendlyAppName", appPath);
    writeStringValue(HKEY_CURRENT_USER, path, NULL, "blueMSX+");

    snprintf(path, sizeof(path), "%s\\SupportedTypes", appPath);
    for (i = 0; supported[i] != NULL; i++) {
        writeStringValue(HKEY_CURRENT_USER, path, supported[i], "");
    }
}

void unregisterApplicationOpenWith(void)
{
    char appPath[MAX_PATH];
    snprintf(appPath, sizeof(appPath), "Applications\\%s", exeBaseName());
    deleteClassesSub(appPath);
}

/* Tell the shell that file associations changed so the "Open with"
** menu and icon cache pick up the new entries without a logoff. */
void fileTypesNotifyShell(void)
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}
