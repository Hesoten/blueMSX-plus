/*****************************************************************************
**
** Emulator instance selection for the MCP server.
** Copyright (C) 2026 Hesoten
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
#include "McpInstance.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ControlJson.h"
#include "ControlProtocol.h"

static char pinnedInstance[64];

static int instanceDirectory(char* out, int outLen)
{
    char appData[MAX_PATH];

    if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
        return 0;
    }
    _snprintf(out, (size_t)outLen, "%s\\" CTRL_AGENT_FOLDER, appData);
    out[outLen - 1] = 0;
    return 1;
}

static int processIsAlive(int pid)
{
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    DWORD  wait;

    if (handle == NULL) {
        return 0;
    }
    wait = WaitForSingleObject(handle, 0);
    CloseHandle(handle);
    return wait == WAIT_TIMEOUT;
}

/* Reads the live instance files into list, at most max of them; returns how many are live. */
static int listInstances(McpInstance* list, int max, int* stored)
{
    char directory[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    int    count = 0;

    *stored = 0;
    if (!instanceDirectory(directory, sizeof(directory))) {
        return 0;
    }
    _snprintf(pattern, sizeof(pattern), "%s\\" CTRL_INSTANCE_PREFIX "*" CTRL_INSTANCE_SUFFIX,
              directory);

    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        char  path[MAX_PATH];
        char  contents[1024];
        FILE* file;
        size_t read;
        Int32 filePid = 0;
        Int32 filePort = 0;
        char  fileToken[64];
        char  fileMachine[128];

        _snprintf(path, sizeof(path), "%s\\%s", directory, found.cFileName);
        file = fopen(path, "rb");
        if (file == NULL) {
            continue;
        }
        read = fread(contents, 1, sizeof(contents) - 1, file);
        fclose(file);
        contents[read] = 0;

        if (!jsonGetInt(contents, "pid", &filePid) ||
            !jsonGetInt(contents, "port", &filePort) ||
            !jsonGetString(contents, "token", fileToken, sizeof(fileToken)))
        {
            continue;
        }
        fileMachine[0] = 0;
        jsonGetString(contents, "machine", fileMachine, sizeof(fileMachine));

        /* Stale file left by an emulator that was killed rather than closed. */
        if (!processIsAlive((int)filePid)) {
            DeleteFileA(path);
            continue;
        }

        count++;
        if (*stored < max) {
            McpInstance* entry = &list[(*stored)++];
            entry->pid  = (int)filePid;
            entry->port = (int)filePort;
            strcpy(entry->token, fileToken);
            strcpy(entry->machine, fileMachine);
        }
    } while (FindNextFileA(search, &found));

    FindClose(search);
    return count;
}

static int pinIsPid(void)
{
    int i;

    for (i = 0; pinnedInstance[i] != 0; i++) {
        if (pinnedInstance[i] < '0' || pinnedInstance[i] > '9') {
            return 0;
        }
    }
    return i > 0;
}

/* The instances --instance names, ignoring case: all digits is a pid or a whole
** machine name; anything else a whole name, or else a part of one. Returns how many. */
static int pickInstances(const McpInstance* list, int stored, int* picked)
{
    char pin[64];
    int  count = 0;
    int  i;

    for (i = 0; i < stored; i++) {
        if (pinnedInstance[0] == 0 || _stricmp(list[i].machine, pinnedInstance) == 0 ||
            (pinIsPid() && list[i].pid == atoi(pinnedInstance)))
        {
            picked[count++] = i;
        }
    }
    if (count > 0 || pinIsPid()) {
        return count;
    }
    strcpy(pin, pinnedInstance);
    _strlwr(pin);
    for (i = 0; i < stored; i++) {
        char name[128];
        strcpy(name, list[i].machine);
        if (strstr(_strlwr(name), pin) != NULL) {
            picked[count++] = i;
        }
    }
    return count;
}

/* An emulator names its machine in the instance file only once one has started. */
const char* mcpInstanceMachineShown(const char* machine)
{
    return machine[0] != 0 ? machine : "no machine has started";
}

/* One escaped row per picked instance, whole rows only, then how many did not fit. */
static void listRows(const McpInstance* list, const int* picked, int count, int unlisted,
                     char* out, int outLen)
{
    int shown;

    out[0] = 0;
    for (shown = 0; shown < count; shown++) {
        char machineText[800];
        char row[900];
        int  rowLength;

        jsonEscape(mcpInstanceMachineShown(list[picked[shown]].machine), machineText, sizeof(machineText));
        rowLength = _snprintf(row, sizeof(row), "  pid %d  %s\\n",
                              list[picked[shown]].pid, machineText);
        /* Room is kept for the line saying how many more there are. */
        if (rowLength <= 0 || (int)strlen(out) + rowLength + 40 >= outLen) {
            break;
        }
        strcat(out, row);
    }
    if (count - shown + unlisted > 0) {
        char more[40];
        sprintf(more, "  (%d more not shown)\\n", count - shown + unlisted);
        strcat(out, more);
    }
}

/* blueMSX+.exe is one folder up from Tools\bluemsx-mcp.exe, or beside it in a build folder. */
static int emulatorPath(char* out, int outLen)
{
    static const wchar_t* const tries[] = { L"..\\blueMSX+.exe", L"blueMSX+.exe" };
    wchar_t self[MAX_PATH];
    wchar_t full[MAX_PATH];
    wchar_t* leaf;
    int i;

    if (GetModuleFileNameW(NULL, self, MAX_PATH) == 0 || (leaf = wcsrchr(self, L'\\')) == NULL) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        if (leaf + 1 + wcslen(tries[i]) >= self + MAX_PATH) {
            continue;
        }
        wcscpy(leaf + 1, tries[i]);
        if (GetFullPathNameW(self, MAX_PATH, full, NULL) != 0 &&
            GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES)
        {
            return WideCharToMultiByte(CP_UTF8, 0, full, -1, out, outLen, NULL, NULL) > 0;
        }
    }
    return 0;
}

/* The live instances --instance picks. Returns how many were picked; live and stored as
** listInstances gives. */
int mcpInstancePick(McpInstance* list, int* picked, int* live, int* stored)
{
    *live = listInstances(list, MCP_INSTANCE_MAX, stored);
    return pickInstances(list, *stored, picked);
}

/* No instance to attach to: what is running instead, and how to start one. */
void mcpInstanceExplainNone(const McpInstance* list, int* picked, int live, int stored,
                            const char* pinText, char* problem, int problemLen)
{
    char listing[2048];

    if (pinIsPid() && processIsAlive(atoi(pinnedInstance))) {
        _snprintf(problem, (size_t)problemLen,
                  "pid %s is running but is not accepting agent connections: it is "
                  "closing, or Agent Control is Off in it (or it is not blueMSX+).\\n"
                  "fix: if it has a window, choose Tools, Agent Control, Read Only or Full "
                  "there. If it is a blueMSX+ started with --hidden, there is no menu: "
                  "close it (taskkill /PID %s, without /F) and start it again with "
                  "--agentcontrol full.", pinText, pinText);
    }
    else if (pinnedInstance[0] != 0 && live > 0) {
        int i;
        for (i = 0; i < stored; i++) {
            picked[i] = i;
        }
        listRows(list, picked, stored, live - stored, listing, sizeof(listing));
        _snprintf(problem, (size_t)problemLen,
                  "No blueMSX+ matching --instance \\\"%s\\\" is accepting agent "
                  "connections. These are:\\n%s"
                  "fix: change --instance in the MCP client's settings. A row that says "
                  "no machine has started matches by pid only.",
                  pinText, listing);
    }
    else {
        char exe[MAX_PATH * 3];
        char exeText[MAX_PATH * 6];
        int  found = emulatorPath(exe, sizeof(exe));

        jsonEscape(found ? exe : "C:\\full\\path\\blueMSX+.exe", exeText, sizeof(exeText));
        _snprintf(problem, (size_t)problemLen,
                  "No blueMSX+ is accepting agent connections.\\n"
                  "fix: start it as a background process (it runs until it is closed, so "
                  "do not wait for it to exit), with the full path of the ROM:\\n"
                  "  \\\"%s\\\" --machine \\\"MSX2 - C-BIOS\\\" "
                  "--rom1 \\\"C:\\\\full\\\\path\\\\game.rom\\\" --agentcontrol full\\n"
                  "In PowerShell put & before the quoted path, or use Start-Process. "
                  "%s--listmachines lists the machines and --help the other options. If "
                  "the person already has blueMSX+ open, ask them to choose Tools, Agent "
                  "Control, Read Only or Full there instead.", exeText,
                  found ? "" : "blueMSX+.exe is not beside this server, so ask the person "
                               "for its full path. ");
    }
}

/* More than one instance to attach to: lists them, and how to pick one. */
void mcpInstanceExplainMany(const McpInstance* list, const int* picked, int count, int live,
                            int stored, const char* pinText, char* problem, int problemLen)
{
    char listing[2048];
    int  sameMachine = 1;
    int  i;

    for (i = 1; i < count; i++) {
        if (_stricmp(list[picked[i]].machine, list[picked[0]].machine) != 0) {
            sameMachine = 0;
        }
    }
    listRows(list, picked, count, pinnedInstance[0] == 0 ? live - stored : 0,
             listing, sizeof(listing));
    if (pinnedInstance[0] != 0) {
        _snprintf(problem, (size_t)problemLen,
                  "%d blueMSX+ instances match --instance \\\"%s\\\":\\n%sfix: %s",
                  count, pinText, listing,
                  sameMachine ? "they run the same machine, so only --instance <pid> "
                                "tells them apart."
                              : "use more of the machine name, or the pid.");
    }
    else {
        _snprintf(problem, (size_t)problemLen,
                  "%d blueMSX+ instances are accepting agent connections:\\n%s"
                  "fix: this server talks to one at a time. Ask the person to close the "
                  "one not meant, or to set Tools, Agent Control, Off in it; if you started "
                  "more than one yourself, close the extra ones. To pin one for good, the "
                  "MCP client's settings can start this server with --instance %s.",
                  live, listing,
                  sameMachine ? "<pid>" : "<machine name or pid>");
    }
}

void mcpInstancePin(const char* pin, size_t length)
{
    if (length > sizeof(pinnedInstance) - 1) {
        length = sizeof(pinnedInstance) - 1;
    }
    memcpy(pinnedInstance, pin, length);
    pinnedInstance[length] = 0;
}

const char* mcpInstancePinned(void)
{
    return pinnedInstance;
}
