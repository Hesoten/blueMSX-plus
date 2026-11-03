/*****************************************************************************
**
** Win32 instance file.
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
#define _CRT_RAND_S
#include "ControlInstance.h"
#include <windows.h>
#include <shlobj.h>
#include "ControlClient.h"
#include "ControlProtocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char   sessionToken[33];
static char   instancePath[MAX_PATH];
/* Set and cleared where the socket starts and stops listening; the file is kept only while set. */
static volatile int published;
static int    publishedPort;

/* Must be unguessable: it is all that stops other local programs using this port. */
static void makeToken(void)
{
    static const char digits[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 32; i++) {
        unsigned int value = 0;
        if (rand_s(&value) != 0) {
            value = (unsigned int)GetTickCount() ^ (unsigned int)i;
        }
        sessionToken[i] = digits[value & 0x0f];
    }
    sessionToken[32] = 0;
}

/* Deliberately not under /rootdir: tools must find it without being told. */
int ctrlInstanceAgentFolder(char* out, int outLen)
{
    char appData[MAX_PATH];

    if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
        return 0;
    }
    snprintf(out, (size_t)outLen, "%s\\" CTRL_AGENT_FOLDER, appData);
    return 1;
}

static char currentMachine[128];
static int  writtenLevel = -1;

/* Deletes instance files left by killed emulators. The pid is taken from the
** file name so a corrupt file is still swept; any live pid keeps its file. */
static void sweepDeadInstances(void)
{
    char directory[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;

    if (!ctrlInstanceAgentFolder(directory, sizeof(directory))) {
        return;
    }
    snprintf(pattern, sizeof(pattern), "%s\\" CTRL_INSTANCE_PREFIX "*" CTRL_INSTANCE_SUFFIX,
             directory);

    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        unsigned long pid = strtoul(found.cFileName + CTRL_INSTANCE_PREFIX_LEN, NULL, 10);
        HANDLE process;

        if (pid == 0 || pid == GetCurrentProcessId()) {
            continue;
        }
        process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (process != NULL) {
            int alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
            if (alive) {
                continue;
            }
        }
        else if (GetLastError() == ERROR_ACCESS_DENIED) {
            /* Alive, owned by another user. */
            continue;
        }
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s\\%s", directory, found.cFileName);
            DeleteFileA(path);
        }
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static void writeInstanceFile(void)
{
    char directory[MAX_PATH];
    FILE* file;

    if (!ctrlInstanceAgentFolder(directory, sizeof(directory))) {
        return;
    }
    snprintf(instancePath, sizeof(instancePath),
             "%s\\" CTRL_INSTANCE_PREFIX "%lu" CTRL_INSTANCE_SUFFIX, directory,
             GetCurrentProcessId());

    CreateDirectoryA(directory, NULL);
    {
        char parent[MAX_PATH];
        char* cut;
        strcpy(parent, directory);
        cut = strrchr(parent, '\\');
        if (cut != NULL) {
            *cut = 0;
            CreateDirectoryA(parent, NULL);
            CreateDirectoryA(directory, NULL);
        }
    }

    file = fopen(instancePath, "w");
    if (file == NULL) {
        instancePath[0] = 0;
        return;
    }
    fprintf(file,
            "{\n"
            "  \"pid\": %lu,\n"
            "  \"port\": %d,\n"
            "  \"token\": \"%s\",\n"
            "  \"level\": \"%s\",\n"
            "  \"machine\": \"%s\",\n"
            "  \"protocol\": 1\n"
            "}\n",
            GetCurrentProcessId(), publishedPort, sessionToken,
            ctrlGateLevelText(ctrlGateLevel()), currentMachine);
    fclose(file);
    writtenLevel = ctrlGateLevel();
}

static void removeInstanceFile(void);

void ctrlInstanceSetMachine(const char* machineName)
{
    int changed = 0;

    if (machineName != NULL) {
        changed = strncmp(currentMachine, machineName, sizeof(currentMachine) - 1) != 0;
        strncpy(currentMachine, machineName, sizeof(currentMachine) - 1);
        currentMachine[sizeof(currentMachine) - 1] = 0;
    }

    /* Every start calls this, and a rewrite truncates the file a client may be reading. */
    if (published && (changed || writtenLevel != ctrlGateLevel())) {
        writeInstanceFile();
        /* A stop on the main thread may have removed the file while this one wrote it. */
        if (!published) {
            removeInstanceFile();
        }
    }
}

static void removeInstanceFile(void)
{
    if (instancePath[0] != 0) {
        DeleteFileA(instancePath);
        instancePath[0] = 0;
    }
}

void ctrlInstancePublish(int port)
{
    publishedPort = port;
    makeToken();
    sweepDeadInstances();
    writeInstanceFile();
    published = 1;
}

void ctrlInstanceRemove(void)
{
    published = 0;
    removeInstanceFile();
}

const char* ctrlInstanceToken(void)
{
    return sessionToken;
}
