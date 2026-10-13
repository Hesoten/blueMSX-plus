/*****************************************************************************
**
** Carries the changes the MSX made to a directory volume over to the host directory.
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
#include "DirWriteBack.h"
#include "DirDiskPrivate.h"
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32

DirWbStatus dirWriteBackPlan(DirDisk* disk, DirPlan** plan, DirChanges* changes)
{
    (void)disk;
    *plan = NULL;
    memset(changes, 0, sizeof(*changes));
    return DIR_WB_UNSUPPORTED;
}

DirWbStatus dirWriteBackApply(DirDisk* disk, DirPlan* plan, int* failed, char* session, int sessionSize)
{
    (void)disk;
    (void)plan;
    (void)sessionSize;
    *failed = 0;
    session[0] = 0;
    return DIR_WB_UNSUPPORTED;
}

void dirWriteBackFree(DirPlan* plan)
{
    (void)plan;
}

#else

#include <windows.h>

#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN        0x00040000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif

#define MAX_DEPTH       16
#define MAX_ENTRIES     65536
#define MASS_MINIMUM    10
#define PATH_SIZE       1024

/* What the MSX has in a directory now. */
typedef struct Entry {
    UInt8   name[11];
    UInt8   attr;
    UInt32  first;
    UInt32  size;
    int     paired;
    struct Entry* child;
    int     childCount;
} Entry;

/* ITEM_SKIPPED is a change left out because the host file is no longer the
** one the volume was built from; ITEM_FAILED is one that could not be read. */
typedef enum { ITEM_DELETE, ITEM_RMDIR, ITEM_MKDIR, ITEM_CHANGED, ITEM_NEW, ITEM_CONFLICT, ITEM_SKIPPED, ITEM_FAILED } ItemKind;

/* target is the host path; relative is that path below the mounted directory. */
typedef struct {
    ItemKind kind;
    char*   target;
    const char* relative;
    UInt32  first;
    UInt32  size;
    UInt32  hostSize;
    Int64   hostTime;
    int     staged;
    int     undone;
} Item;

struct DirPlan {
    DirDisk* disk;
    UInt8*  fat;
    UInt8*  claimed;
    UInt8*  dirty;
    Entry   root;
    int     entryCount;
    Item*   item;
    int     itemCount;
    int     itemCapacity;
    int     failed;
    int     hostRefused;
};

/* --------------------------- File system ---------------------------- */

static int testBit(const UInt8* bits, UInt32 index)
{
    return (bits[index >> 3] >> (index & 7)) & 1;
}

static void setBit(UInt8* bits, UInt32 index)
{
    bits[index >> 3] |= (UInt8)(1 << (index & 7));
}

static UInt32 fatEntry(const DirPlan* plan, UInt32 cluster)
{
    const UInt8* fat = plan->fat;

    if (plan->disk->layout.fat16) {
        return fat[cluster * 2] | ((UInt32)fat[cluster * 2 + 1] << 8);
    }
    else {
        UInt32 pair = fat[cluster + cluster / 2] | ((UInt32)fat[cluster + cluster / 2 + 1] << 8);

        return (cluster & 1) ? pair >> 4 : pair & 0xfff;
    }
}

static int isEnd(const DirPlan* plan, UInt32 value)
{
    return value >= (plan->disk->layout.fat16 ? 0xfff8u : 0xff8u);
}

static int inData(const DirPlan* plan, UInt32 cluster)
{
    return cluster >= 2 && cluster < plan->disk->layout.clusters + 2;
}

/* Walks a chain that nothing else may share; the number of clusters, or -1 when it is not sound. */
static int claimChain(DirPlan* plan, UInt32 first)
{
    int count = 0;
    UInt32 cluster = first;

    while (!isEnd(plan, cluster)) {
        if (!inData(plan, cluster) || testBit(plan->claimed, cluster)) {
            return -1;
        }
        setBit(plan->claimed, cluster);
        count++;
        cluster = fatEntry(plan, cluster);
    }
    return count;
}

static int readCluster(DirPlan* plan, UInt32 cluster, UInt8* buffer)
{
    const DirLayout* layout = &plan->disk->layout;
    UInt32 sector = plan->disk->dataStart + (cluster - 2) * layout->spc;
    UInt32 i;

    for (i = 0; i < layout->spc; i++) {
        if (!dirDiskReadSector(plan->disk, sector + i, buffer + i * DIR_SECTOR)) {
            return 0;
        }
    }
    return 1;
}

static void freeEntries(Entry* entry)
{
    int i;

    for (i = 0; i < entry->childCount; i++) {
        freeEntries(&entry->child[i]);
    }
    free(entry->child);
}

/* The layout must be the one the volume was built with, or the clusters mean something else. */
static int sameLayout(DirPlan* plan)
{
    UInt8 boot[DIR_SECTOR];

    if (!dirDiskReadSector(plan->disk, 1, boot)) {
        return 0;
    }
    return memcmp(boot + 0x0b, plan->disk->boot + 0x0b, 13) == 0 &&
           (!plan->disk->layout.fat16 || memcmp(boot + 0x20, plan->disk->boot + 0x20, 4) == 0);
}

static int loadFat(DirPlan* plan)
{
    const DirLayout* layout = &plan->disk->layout;
    UInt32 i;

    plan->fat = (UInt8*)malloc((size_t)layout->fatSectors * DIR_SECTOR + 2);
    plan->claimed = (UInt8*)calloc(layout->clusters / 8 + 2, 1);
    plan->dirty = (UInt8*)calloc(layout->clusters / 8 + 2, 1);
    if (plan->fat == NULL || plan->claimed == NULL || plan->dirty == NULL) {
        return 0;
    }
    for (i = 0; i < layout->fatSectors; i++) {
        if (!dirDiskReadSector(plan->disk, 2 + i, plan->fat + i * DIR_SECTOR)) {
            return 0;
        }
    }
    for (i = 0; i < DIR_OVERLAY_HASH; i++) {
        UInt32 key = plan->disk->overlayKey[i];

        if (key > plan->disk->dataStart && key - 1 < layout->total + 1) {
            UInt32 cluster = 2 + (key - 1 - plan->disk->dataStart) / layout->spc;

            if (inData(plan, cluster)) {
                setBit(plan->dirty, cluster);
            }
        }
    }
    return 1;
}

/* Reads the directory that starts at first (the root when zero) into dir. Zero when it is not sound. */
static int parseDirectory(DirPlan* plan, Entry* dir, UInt32 first, UInt32 parentFirst, int depth)
{
    const DirLayout* layout = &plan->disk->layout;
    UInt32 clusterBytes = layout->spc * DIR_SECTOR;
    UInt8* raw;
    UInt32 bytes = 0;
    UInt32 offset;
    int capacity = 0;
    int ok = 1;
    int i;

    if (depth > MAX_DEPTH) {
        return 0;
    }
    if (first == 0) {
        UInt32 rootStart = 2 + 2 * layout->fatSectors;

        bytes = layout->rootEntries * 32;
        raw = (UInt8*)malloc(bytes);
        for (offset = 0; raw != NULL && offset < bytes; offset += DIR_SECTOR) {
            if (!dirDiskReadSector(plan->disk, rootStart + offset / DIR_SECTOR, raw + offset)) {
                free(raw);
                return 0;
            }
        }
    }
    else {
        UInt32 cluster;
        UInt32 count = 0;

        /* The chain was claimed and found sound by the caller. */
        for (cluster = first; !isEnd(plan, cluster); cluster = fatEntry(plan, cluster)) {
            count++;
        }
        if ((UInt64)count * clusterBytes > (UInt64)MAX_ENTRIES * 32 + clusterBytes) {
            return 0;
        }
        bytes = count * clusterBytes;
        raw = (UInt8*)malloc(bytes ? bytes : 1);
        for (cluster = first, offset = 0; raw != NULL && !isEnd(plan, cluster); cluster = fatEntry(plan, cluster), offset += clusterBytes) {
            if (!readCluster(plan, cluster, raw + offset)) {
                free(raw);
                return 0;
            }
        }
    }
    if (raw == NULL) {
        return 0;
    }

    for (offset = 0; ok && offset + 32 <= bytes && raw[offset] != 0; offset += 32) {
        const UInt8* e = raw + offset;
        UInt32 entryFirst = e[26] | ((UInt32)e[27] << 8);
        UInt32 entrySize = e[28] | ((UInt32)e[29] << 8) | ((UInt32)e[30] << 16) | ((UInt32)e[31] << 24);
        Entry* entry;

        if (e[0] == 0xe5 || e[11] == 0x0f || (e[11] & DIR_ATTR_LABEL)) {
            continue;
        }
        if (e[0] == '.') {
            /* Only the two entries a directory starts with may carry such a name. */
            int self = first != 0 && offset == 0 && memcmp(e, ".          ", 11) == 0 && entryFirst == first;
            int up = first != 0 && offset == 32 && memcmp(e, "..         ", 11) == 0 && entryFirst == parentFirst;

            ok = self || up;
            continue;
        }
        if (dir->childCount == capacity) {
            Entry* grown;

            capacity = capacity ? capacity * 2 : 32;
            grown = (Entry*)realloc(dir->child, capacity * sizeof(Entry));
            if (grown == NULL) {
                ok = 0;
                break;
            }
            dir->child = grown;
        }
        if (++plan->entryCount > MAX_ENTRIES) {
            ok = 0;
            break;
        }
        entry = &dir->child[dir->childCount++];
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->name, e, 11);
        if (entry->name[0] == 0x05) {
            entry->name[0] = 0xe5;
        }
        entry->attr  = e[11];
        entry->first = entryFirst;
        entry->size  = entrySize;
        for (i = 0; i < dir->childCount - 1; i++) {
            if (memcmp(dir->child[i].name, entry->name, 11) == 0) {
                ok = 0;
            }
        }
        if (entry->attr & DIR_ATTR_DIRECTORY) {
            ok = ok && entry->first != 0 && claimChain(plan, entry->first) > 0;
        }
        else if (entry->first == 0) {
            ok = ok && entry->size == 0;
        }
        else {
            int count = claimChain(plan, entry->first);

            ok = ok && count > 0 && (UInt64)count * clusterBytes >= entry->size;
        }
    }
    free(raw);

    for (i = 0; ok && i < dir->childCount; i++) {
        if (dir->child[i].attr & DIR_ATTR_DIRECTORY) {
            ok = parseDirectory(plan, &dir->child[i], dir->child[i].first, first, depth + 1);
        }
    }
    return ok;
}

/* ------------------------------ Names ------------------------------- */

static char* copyText(const char* text)
{
    char* copy = (char*)malloc(strlen(text) + 1);

    if (copy != NULL) {
        strcpy(copy, text);
    }
    return copy;
}

static char* joinPath(const char* directory, const char* name)
{
    char* path = (char*)malloc(strlen(directory) + strlen(name) + 2);

    if (path != NULL) {
        sprintf(path, "%s/%s", directory, name);
    }
    return path;
}

static int reservedName(const wchar_t* name)
{
    static const wchar_t* const devices[] = { L"CON", L"PRN", L"AUX", L"NUL", L"CLOCK$" };
    wchar_t base[16];
    int i;

    for (i = 0; i < 15 && name[i] != 0 && name[i] != L'.'; i++) {
        base[i] = name[i];
    }
    base[i] = 0;
    for (i = 0; i < (int)(sizeof(devices) / sizeof(devices[0])); i++) {
        if (_wcsicmp(base, devices[i]) == 0) {
            return 1;
        }
    }
    return (_wcsnicmp(base, L"COM", 3) == 0 || _wcsnicmp(base, L"LPT", 3) == 0) &&
           base[3] >= L'0' && base[3] <= L'9' && base[4] == 0;
}

/* The host name for a directory entry. Zero when the host cannot carry the
** name; text then holds a spelled out form to keep the file under. */
static int hostName(const UInt8* name, char* text, int size)
{
    char bytes[16];
    wchar_t wide[32];
    int length = 0;
    int count;
    int i;

    int baseLength = 8;
    int extLength = 3;

    while (baseLength > 0 && name[baseLength - 1] == ' ') {
        baseLength--;
    }
    while (extLength > 0 && name[8 + extLength - 1] == ' ') {
        extLength--;
    }
    memcpy(bytes, name, baseLength);
    length = baseLength;
    if (extLength > 0) {
        bytes[length++] = '.';
        memcpy(bytes + length, name + 8, extLength);
        length += extLength;
    }
    count = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes, length, wide, 31);
    wide[count > 0 ? count : 0] = 0;
    for (i = 0; i < count; i++) {
        if (wide[i] < 0x20 || wcschr(L"\\/:*?\"<>|", wide[i]) != NULL) {
            count = 0;
        }
    }
    if (count > 0 && wide[0] != L' ' && wide[count - 1] != L'.' && wide[count - 1] != L' ' && !reservedName(wide) &&
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, size, NULL, NULL) > 0) {
        return 1;
    }
    strcpy(text, "name-");
    for (i = 0; i < 11; i++) {
        sprintf(text + strlen(text), "%02X", name[i]);
    }
    return 0;
}

/* ------------------------------- Plan ------------------------------- */

static Item* addItem(DirPlan* plan, ItemKind kind, char* target)
{
    Item* item;

    if (target == NULL) {
        plan->failed = 1;
        return NULL;
    }
    if (plan->itemCount == plan->itemCapacity) {
        int capacity = plan->itemCapacity ? plan->itemCapacity * 2 : 64;
        Item* grown = (Item*)realloc(plan->item, capacity * sizeof(Item));

        if (grown == NULL) {
            free(target);
            plan->failed = 1;
            return NULL;
        }
        plan->item = grown;
        plan->itemCapacity = capacity;
    }
    item = &plan->item[plan->itemCount++];
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->target = target;
    item->relative = target + strlen(plan->disk->directory) + 1;
    return item;
}

/* What the host has under path now; zero when there is no plain file. */
static int hostFile(const char* path, UInt32* size, Int64* time, DWORD* attributes)
{
    wchar_t wide[PATH_SIZE];
    WIN32_FIND_DATAW data;
    HANDLE find;

    dirDiskWidePath(path, wide, PATH_SIZE);
    find = wide[0] != 0 ? FindFirstFileW(wide, &data) : INVALID_HANDLE_VALUE;
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    FindClose(find);
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.nFileSizeHigh != 0) {
        return 0;
    }
    *size = data.nFileSizeLow;
    *time = ((Int64)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
    *attributes = data.dwFileAttributes;
    return 1;
}

/* Whether the host file is still the one the volume was built from and can be read without fetching it. */
static int hostUnchanged(const DirNode* node)
{
    UInt32 size;
    Int64 time;
    DWORD attributes;

    return hostFile(node->path, &size, &time, &attributes) && size == node->size && time == node->hostTime &&
           !(attributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN));
}

static void planDelete(DirPlan* plan, const DirNode* node)
{
    int i;

    if (node->dropped) {
        return;
    }
    if (!node->isDir) {
        Item* item;

        item = addItem(plan, hostUnchanged(node) ? ITEM_DELETE : ITEM_SKIPPED, copyText(node->path));
        if (item != NULL) {
            item->hostSize = node->size;
            item->hostTime = node->hostTime;
        }
        return;
    }
    for (i = 0; i < node->childCount; i++) {
        planDelete(plan, node->child[i]);
    }
    addItem(plan, ITEM_RMDIR, copyText(node->path));
}

static void planNew(DirPlan* plan, const Entry* entry, const char* hostDirectory, int conflict)
{
    char name[64];
    int usable = hostName(entry->name, name, sizeof(name));
    Item* item;
    int i;

    if (entry->attr & DIR_ATTR_DIRECTORY) {
        char* path = joinPath(hostDirectory, name);

        if (path == NULL) {
            plan->failed = 1;
            return;
        }
        /* What is inside a directory the host cannot name is kept apart as a whole. */
        if (usable && !conflict) {
            addItem(plan, ITEM_MKDIR, copyText(path));
        }
        for (i = 0; i < entry->childCount; i++) {
            planNew(plan, &entry->child[i], path, conflict || !usable);
        }
        free(path);
        return;
    }
    item = addItem(plan, usable && !conflict ? ITEM_NEW : ITEM_CONFLICT, joinPath(hostDirectory, name));
    if (item != NULL) {
        item->first = entry->first;
        item->size = entry->size;
    }
}

/* Whether the file still lies where the volume put it and no sector of it was written. */
static int untouched(const DirPlan* plan, const DirNode* node, const Entry* entry)
{
    UInt32 i;

    if (entry->first != node->first || entry->size != node->size) {
        return 0;
    }
    for (i = 0; i < node->clusters; i++) {
        UInt32 next = fatEntry(plan, node->first + i);

        if (testBit(plan->dirty, node->first + i) ||
            (i + 1 < node->clusters ? next != node->first + i + 1 : !isEnd(plan, next))) {
            return 0;
        }
    }
    return 1;
}

static void planDirectory(DirPlan* plan, const DirNode* node, Entry* dir)
{
    int i;
    int j;

    for (i = 0; i < node->childCount; i++) {
        const DirNode* child = node->child[i];
        Entry* entry = NULL;

        if (child->dropped) {
            continue;
        }
        for (j = 0; j < dir->childCount; j++) {
            if (memcmp(dir->child[j].name, child->name, 11) == 0) {
                entry = &dir->child[j];
            }
        }
        if (entry == NULL || !!(entry->attr & DIR_ATTR_DIRECTORY) != !!child->isDir) {
            planDelete(plan, child);
            continue;
        }
        entry->paired = 1;
        if (child->isDir) {
            planDirectory(plan, child, entry);
        }
        else if (!untouched(plan, child, entry)) {
            Item* item;

            /* Its unwritten parts would have to come from a host file that is no longer the same. */
            item = addItem(plan, hostUnchanged(child) ? ITEM_CHANGED : ITEM_SKIPPED, copyText(child->path));
            if (item != NULL) {
                item->first = entry->first;
                item->size = entry->size;
                item->hostSize = child->size;
                item->hostTime = child->hostTime;
            }
        }
    }
    for (j = 0; j < dir->childCount; j++) {
        if (!dir->child[j].paired) {
            planNew(plan, &dir->child[j], node->path, 0);
        }
    }
}

static int countMountFiles(const DirNode* node)
{
    int count = node->isDir || node->dropped ? 0 : 1;
    int i;

    for (i = 0; !node->dropped && i < node->childCount; i++) {
        count += countMountFiles(node->child[i]);
    }
    return count;
}

DirWbStatus dirWriteBackPlan(DirDisk* disk, DirPlan** result, DirChanges* changes)
{
    DirPlan* plan = (DirPlan*)calloc(1, sizeof(DirPlan));
    int i;

    *result = NULL;
    memset(changes, 0, sizeof(*changes));
    if (plan == NULL) {
        return DIR_WB_MEMORY;
    }
    plan->disk = disk;
    if (!loadFat(plan)) {
        dirWriteBackFree(plan);
        return DIR_WB_MEMORY;
    }
    if (!sameLayout(plan) || !parseDirectory(plan, &plan->root, 0, 0, 0)) {
        dirWriteBackFree(plan);
        return DIR_WB_INVALID;
    }
    planDirectory(plan, disk->root, &plan->root);
    if (plan->failed) {
        dirWriteBackFree(plan);
        return DIR_WB_MEMORY;
    }
    for (i = 0; i < plan->itemCount; i++) {
        switch (plan->item[i].kind) {
        case ITEM_NEW:      changes->created++; break;
        case ITEM_CHANGED:  changes->changed++; break;
        case ITEM_DELETE:   changes->deleted++; break;
        case ITEM_CONFLICT: changes->skipped++; break;
        case ITEM_SKIPPED:  changes->skipped++; break;
        default:            break;
        }
    }
    changes->mass = changes->deleted >= MASS_MINIMUM && changes->deleted * 2 >= countMountFiles(disk->root);
    if (plan->itemCount == 0) {
        dirWriteBackFree(plan);
        return DIR_WB_NOTHING;
    }
    *result = plan;
    return DIR_WB_OK;
}

void dirWriteBackFree(DirPlan* plan)
{
    int i;

    if (plan == NULL) {
        return;
    }
    for (i = 0; i < plan->itemCount; i++) {
        free(plan->item[i].target);
    }
    free(plan->item);
    freeEntries(&plan->root);
    free(plan->fat);
    free(plan->claimed);
    free(plan->dirty);
    free(plan);
}

/* ------------------------------ Apply ------------------------------- */

static int makeDir(const char* path)
{
    wchar_t wide[PATH_SIZE];

    dirDiskWidePath(path, wide, PATH_SIZE);
    if (wide[0] == 0) {
        return 0;
    }
    if (CreateDirectoryW(wide, NULL)) {
        return 1;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS && (GetFileAttributesW(wide) & FILE_ATTRIBUTE_DIRECTORY) &&
           GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
}

/* Makes the directories that path lies in, below the mounted directory. */
static int makeParents(const DirPlan* plan, const char* path)
{
    char parent[PATH_SIZE];
    char* slash;

    if (strlen(path) >= sizeof(parent)) {
        return 0;
    }
    strcpy(parent, path);
    slash = strrchr(parent, '/');
    if (slash == NULL || (size_t)(slash - parent) <= strlen(plan->disk->directory)) {
        return 1;
    }
    *slash = 0;
    return makeParents(plan, parent) && makeDir(parent);
}

/* Moves within the mounted directory; what already carries the new name is never replaced. */
static int moveFile(const DirPlan* plan, const char* from, const char* to)
{
    wchar_t wideFrom[PATH_SIZE];
    wchar_t wideTo[PATH_SIZE];

    dirDiskWidePath(from, wideFrom, PATH_SIZE);
    dirDiskWidePath(to, wideTo, PATH_SIZE);
    return wideFrom[0] != 0 && wideTo[0] != 0 && makeParents(plan, to) && MoveFileExW(wideFrom, wideTo, 0);
}

static void removeFile(const char* path)
{
    wchar_t wide[PATH_SIZE];

    dirDiskWidePath(path, wide, PATH_SIZE);
    if (wide[0] != 0) {
        DeleteFileW(wide);
    }
}

/* Removes the directory when it is empty. */
static int removeDir(const char* path)
{
    wchar_t wide[PATH_SIZE];

    dirDiskWidePath(path, wide, PATH_SIZE);
    return wide[0] != 0 && RemoveDirectoryW(wide);
}

static void stagedPath(const char* session, int index, char* path)
{
    sprintf(path, "%s/staging/%06d.tmp", session, index);
}

/* Writes the file as the MSX has it to its place in the staging directory. */
static int stageItem(DirPlan* plan, const Item* item, const char* path)
{
    UInt32 clusterBytes = plan->disk->layout.spc * DIR_SECTOR;
    UInt8* buffer = (UInt8*)malloc(clusterBytes);
    wchar_t wide[PATH_SIZE];
    HANDLE file;
    UInt32 left = item->size;
    UInt32 cluster = item->first;
    int ok = buffer != NULL;

    dirDiskWidePath(path, wide, PATH_SIZE);
    file = wide[0] != 0 ? CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE || buffer == NULL) {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        free(buffer);
        plan->hostRefused = 1;
        return 0;
    }
    while (ok && left > 0) {
        DWORD wanted = left < clusterBytes ? left : clusterBytes;
        DWORD written = 0;
        UInt32 sector = plan->disk->dataStart + (cluster - 2) * plan->disk->layout.spc;
        UInt32 i;

        ok = inData(plan, cluster);
        for (i = 0; ok && i * DIR_SECTOR < wanted; i++) {
            ok = dirDiskReadSector(plan->disk, sector + i, buffer + i * DIR_SECTOR);
        }
        if (ok && !(WriteFile(file, buffer, wanted, &written, NULL) && written == wanted)) {
            plan->hostRefused = 1;
            ok = 0;
        }
        left -= wanted;
        cluster = ok ? fatEntry(plan, cluster) : cluster;
    }
    if (ok && !FlushFileBuffers(file)) {
        plan->hostRefused = 1;
        ok = 0;
    }
    CloseHandle(file);
    free(buffer);
    return ok;
}

/* A name below the session directory for what was at relative, clear of anything already there. */
static void keptPath(const char* session, const char* kind, const char* relative, char* path)
{
    wchar_t wide[PATH_SIZE];
    int n;

    snprintf(path, PATH_SIZE, "%s/%s/%s", session, kind, relative);
    for (n = 2; n < 1000; n++) {
        dirDiskWidePath(path, wide, PATH_SIZE);
        if (wide[0] == 0 || GetFileAttributesW(wide) == INVALID_FILE_ATTRIBUTES) {
            return;
        }
        snprintf(path, PATH_SIZE, "%s/%s/%s.%d", session, kind, relative, n);
    }
}

static int makeSession(const DirPlan* plan, char* session, int size)
{
    char trash[PATH_SIZE];
    wchar_t wide[PATH_SIZE];
    SYSTEMTIME now;
    int n;

    if (snprintf(trash, sizeof(trash), "%s/%s", plan->disk->directory, DIR_TRASH_NAME) >= (int)sizeof(trash) ||
        !makeDir(trash)) {
        return 0;
    }
    GetLocalTime(&now);
    for (n = 1; n < 100; n++) {
        char suffix[8] = "";

        if (n > 1) {
            sprintf(suffix, "-%d", n);
        }
        if (snprintf(session, size, "%s/%04d-%02d-%02d_%02d%02d%02d%s", trash, now.wYear, now.wMonth, now.wDay,
                     now.wHour, now.wMinute, now.wSecond, suffix) >= size) {
            return 0;
        }
        dirDiskWidePath(session, wide, PATH_SIZE);
        if (wide[0] != 0 && CreateDirectoryW(wide, NULL)) {
            return 1;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return 0;
        }
    }
    return 0;
}

static const char* kindName(ItemKind kind)
{
    switch (kind) {
    case ITEM_DELETE:   return "deleted";
    case ITEM_RMDIR:    return "directory removed";
    case ITEM_MKDIR:    return "directory made";
    case ITEM_CHANGED:  return "changed";
    case ITEM_NEW:      return "new";
    case ITEM_SKIPPED:  return "left out, the file changed on this computer";
    case ITEM_FAILED:   return "left out, could not be read";
    default:            return "kept apart";
    }
}

/* Lists what is about to be done, so that an interrupted run can be finished by hand;
** with undone set it adds the items that were then not done. */
static int writeLog(const DirPlan* plan, const char* session, int undone)
{
    char path[PATH_SIZE];
    wchar_t wide[PATH_SIZE];
    HANDLE file;
    int ok = 1;
    int i;

    snprintf(path, sizeof(path), "%s/log.txt", session);
    dirDiskWidePath(path, wide, PATH_SIZE);
    file = wide[0] != 0 ? CreateFileW(wide, undone ? FILE_APPEND_DATA : GENERIC_WRITE, 0, NULL, undone ? OPEN_EXISTING : CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    for (i = 0; ok && i < plan->itemCount; i++) {
        const Item* item = &plan->item[i];
        char line[PATH_SIZE + 64];
        DWORD written;
        int length;

        if (undone) {
            if (!item->undone) {
                continue;
            }
            length = snprintf(line, sizeof(line), "not done\t\t%s\r\n", item->relative);
        }
        else if (item->staged) {
            length = snprintf(line, sizeof(line), "%s\tstaging/%06d.tmp\t%s\r\n", kindName(item->kind), i, item->relative);
        }
        else {
            length = snprintf(line, sizeof(line), "%s\t\t%s\r\n", kindName(item->kind), item->relative);
        }
        /* A name too long for the line is cut short. */
        if (length >= (int)sizeof(line)) {
            length = (int)sizeof(line) - 1;
            line[length - 2] = '\r';
            line[length - 1] = '\n';
        }
        ok = length > 0 && WriteFile(file, line, (DWORD)length, &written, NULL);
    }
    ok = ok && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

/* Whether the host still has the file the item was planned against. */
static int itemUnchanged(const Item* item)
{
    UInt32 size;
    Int64 time;
    DWORD attributes;

    return hostFile(item->target, &size, &time, &attributes) && size == item->hostSize && time == item->hostTime;
}

static void clearReadOnly(const char* path)
{
    wchar_t wide[PATH_SIZE];
    DWORD attributes;

    dirDiskWidePath(path, wide, PATH_SIZE);
    attributes = wide[0] != 0 ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(wide, attributes & ~FILE_ATTRIBUTE_READONLY);
    }
}

DirWbStatus dirWriteBackApply(DirDisk* disk, DirPlan* plan, int* failed, char* session, int sessionSize)
{
    char path[PATH_SIZE];
    char kept[PATH_SIZE];
    int keptAny = 0;
    int ok = 1;
    int i;

    *failed = 0;
    session[0] = 0;
    if (plan != NULL) {
        plan->hostRefused = 0;
    }
    if (plan == NULL || plan->disk != disk || sessionSize < PATH_SIZE || !makeSession(plan, session, PATH_SIZE - 64)) {
        session[0] = 0;
        return DIR_WB_HOST;
    }
    snprintf(path, sizeof(path), "%s/staging", session);
    ok = makeDir(path);

    /* Everything is read out first: reading goes through the host files that are about to move. */
    for (i = 0; ok && i < plan->itemCount; i++) {
        Item* item = &plan->item[i];

        if (item->kind == ITEM_CHANGED || item->kind == ITEM_NEW || item->kind == ITEM_CONFLICT) {
            stagedPath(session, i, path);
            if (stageItem(plan, item, path)) {
                item->staged = 1;
            }
            else {
                removeFile(path);
                /* A host that takes no more files stops it all here, with nothing touched yet. */
                if (plan->hostRefused) {
                    ok = 0;
                    break;
                }
                item->kind = ITEM_FAILED;
                (*failed)++;
            }
        }
        if (item->kind == ITEM_SKIPPED || item->kind == ITEM_FAILED) {
            keptAny = 1;
        }
    }
    dirDiskReleaseHostFile(disk);
    ok = ok && writeLog(plan, session, 0);
    if (!ok) {
        for (i = 0; i < plan->itemCount; i++) {
            if (plan->item[i].staged) {
                stagedPath(session, i, path);
                removeFile(path);
            }
        }
        snprintf(path, sizeof(path), "%s/log.txt", session);
        removeFile(path);
        snprintf(path, sizeof(path), "%s/staging", session);
        removeDir(path);
        removeDir(session);
        snprintf(path, sizeof(path), "%s/%s", disk->directory, DIR_TRASH_NAME);
        removeDir(path);
        session[0] = 0;
        *failed = 0;
        return DIR_WB_HOST;
    }

    for (i = 0; i < plan->itemCount; i++) {
        Item* item = &plan->item[i];

        if (item->kind == ITEM_DELETE) {
            keptPath(session, "deleted", item->relative, kept);
            if (itemUnchanged(item) && moveFile(plan, item->target, kept)) {
                keptAny = 1;
            }
            else {
                item->undone = 1;
                (*failed)++;
            }
        }
        else if (item->kind == ITEM_RMDIR) {
            removeDir(item->target);
        }
    }
    for (i = 0; i < plan->itemCount; i++) {
        Item* item = &plan->item[i];

        if (item->kind == ITEM_MKDIR) {
            makeDir(item->target);
            continue;
        }
        if (!item->staged) {
            continue;
        }
        stagedPath(session, i, path);
        if (item->kind == ITEM_CHANGED && itemUnchanged(item)) {
            keptPath(session, "replaced", item->relative, kept);
            clearReadOnly(item->target);
            if (!moveFile(plan, item->target, kept)) {
                item->undone = 1;
                (*failed)++;
                continue;
            }
            keptAny = 1;
            if (moveFile(plan, path, item->target)) {
                continue;
            }
            /* The old file goes back; the new one stays in the staging directory. */
            moveFile(plan, kept, item->target);
            item->undone = 1;
            (*failed)++;
            continue;
        }
        if (item->kind == ITEM_NEW && moveFile(plan, path, item->target)) {
            continue;
        }
        keptPath(session, "conflict", item->relative, kept);
        if (moveFile(plan, path, kept)) {
            keptAny = 1;
        }
        else {
            item->undone = 1;
            (*failed)++;
        }
    }
    if (*failed > 0) {
        writeLog(plan, session, 1);
    }

    snprintf(path, sizeof(path), "%s/staging", session);
    removeDir(path);
    if (*failed == 0 && !keptAny) {
        snprintf(path, sizeof(path), "%s/log.txt", session);
        removeFile(path);
        removeDir(session);
        snprintf(path, sizeof(path), "%s/%s", disk->directory, DIR_TRASH_NAME);
        removeDir(path);
        session[0] = 0;
    }
    return DIR_WB_OK;
}

#endif
