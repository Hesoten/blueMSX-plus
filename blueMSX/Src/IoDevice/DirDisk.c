/*****************************************************************************
**
** A host directory presented as a FAT disk.
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
#include "DirDiskPrivate.h"
#include "DiskFormat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define DIR_TRASH_NAME_WIDE L".blueMSX-trash"
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN        0x00040000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#define SECTOR              DIR_SECTOR
#define MAX_DEPTH           8
#define ROOT_SPARE          16
#define DOS1_ROOT_MAX       240
#define ROOT_MAX            512
#define RESERVE_BYTES       (2 * 1024 * 1024)
#define OVERLAY_SECTORS     DIR_OVERLAY_SECTORS
#define OVERLAY_BLOCK       DIR_OVERLAY_BLOCK
#define OVERLAY_HASH        DIR_OVERLAY_HASH
#define ATTR_READONLY       DIR_ATTR_READONLY
#define ATTR_DIRECTORY      DIR_ATTR_DIRECTORY
#define ATTR_ARCHIVE        DIR_ATTR_ARCHIVE
#define ATTR_LABEL          DIR_ATTR_LABEL

typedef DirNode Node;
typedef DirLayout Layout;

/* ---------------------------- Host access --------------------------- */

#ifdef _WIN32
void dirDiskWidePath(const char* path, wchar_t* wide, int capacity)
{
    wchar_t given[1024];
    int offset = 0;
    DWORD length;

    wide[0] = 0;
    if (capacity < 8 || MultiByteToWideChar(CP_UTF8, 0, path, -1, given, 1024) == 0) {
        return;
    }
    /* The long form lifts the 260 character limit but takes a tidy path only. */
    if (isalpha((unsigned char)path[0]) && path[1] == ':') {
        wcscpy(wide, L"\\\\?\\");
        offset = 4;
    }
    length = GetFullPathNameW(given, capacity - offset, wide + offset, NULL);
    if (length == 0 || length >= (DWORD)(capacity - offset)) {
        wide[0] = 0;
    }
}

/* Leaves the host free to change, rename or delete the file meanwhile. */
static FILE* openHostFile(const char* path)
{
    wchar_t wide[1024];
    HANDLE handle;
    FILE* file;
    int descriptor;

    dirDiskWidePath(path, wide, 1024);
    if (wide[0] == 0) {
        return NULL;
    }
    handle = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
    if (descriptor < 0) {
        CloseHandle(handle);
        return NULL;
    }
    file = _fdopen(descriptor, "rb");
    if (file == NULL) {
        _close(descriptor);
    }
    return file;
}

static int seekHostFile(FILE* file, UInt32 offset)
{
    return _fseeki64(file, (__int64)offset, SEEK_SET) == 0;
}

static Int64 hostFileSize(FILE* file)
{
    return (Int64)_filelengthi64(_fileno(file));
}
#else
static FILE* openHostFile(const char* path)
{
    return fopen(path, "rb");
}

static int seekHostFile(FILE* file, UInt32 offset)
{
    return fseeko(file, (off_t)offset, SEEK_SET) == 0;
}

static Int64 hostFileSize(FILE* file)
{
    struct stat info;

    return fstat(fileno(file), &info) == 0 ? (Int64)info.st_size : -1;
}
#endif

/* ------------------------------- Tree ------------------------------- */

static void freeNode(Node* node)
{
    int i;

    if (node == NULL) {
        return;
    }
    for (i = 0; i < node->childCount; i++) {
        freeNode(node->child[i]);
    }
    free(node->child);
    free(node->data);
    free(node->path);
    free(node->hostName);
    free(node);
}

static char* copyText(const char* text)
{
    char* copy = (char*)malloc(strlen(text) + 1);

    if (copy != NULL) {
        strcpy(copy, text);
    }
    return copy;
}

static Node* newNode(const char* directory, const char* hostName, int isDir)
{
    Node* node = (Node*)calloc(1, sizeof(Node));

    if (node == NULL) {
        return NULL;
    }
    node->isDir    = isDir;
    node->attr     = isDir ? ATTR_DIRECTORY : ATTR_ARCHIVE;
    node->hostName = copyText(hostName);
    node->path     = (char*)malloc(strlen(directory) + strlen(hostName) + 2);
    if (node->hostName == NULL || node->path == NULL) {
        freeNode(node);
        return NULL;
    }
    if (hostName[0] != 0) {
        sprintf(node->path, "%s/%s", directory, hostName);
    }
    else {
        strcpy(node->path, directory);
    }
    return node;
}

static int addChild(Node* parent, Node* child)
{
    if (parent->childCount == parent->childCapacity) {
        int capacity = parent->childCapacity ? parent->childCapacity * 2 : 16;
        Node** grown = (Node**)realloc(parent->child, capacity * sizeof(Node*));

        if (grown == NULL) {
            return 0;
        }
        parent->child = grown;
        parent->childCapacity = capacity;
    }
    parent->child[parent->childCount++] = child;
    return 1;
}

static void setDosTime(Node* node, int year, int month, int day, int hour, int minute, int second)
{
    if (year < 1980) {
        year = 1980; month = 1; day = 1; hour = 0; minute = 0; second = 0;
    }
    if (year > 2107) {
        year = 2107;
    }
    node->date = (UInt16)(((year - 1980) << 9) | (month << 5) | day);
    node->time = (UInt16)((hour << 11) | (minute << 5) | (second / 2));
}

/* Zero when the directory itself cannot be read. */
static int enumerate(DirDisk* disk, Node* parent, int depth, int allowSubdirs);

#ifdef _WIN32
static int enumerate(DirDisk* disk, Node* parent, int depth, int allowSubdirs)
{
    wchar_t pattern[1024];
    WIN32_FIND_DATAW found;
    HANDLE find;
    size_t length;

    dirDiskWidePath(parent->path, pattern, 1020);
    length = wcslen(pattern);
    if (length == 0) {
        return 0;
    }
    wcscpy(pattern + length, pattern[length - 1] == L'\\' ? L"*" : L"\\*");
    find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    do {
        DWORD attributes = found.dwFileAttributes;
        int isDir = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        char name[1024];
        FILETIME local;
        SYSTEMTIME when;
        Node* node;

        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0 ||
            _wcsicmp(found.cFileName, DIR_TRASH_NAME_WIDE) == 0) {
            continue;
        }
        if ((attributes & FILE_ATTRIBUTE_HIDDEN) && (attributes & FILE_ATTRIBUTE_SYSTEM)) {
            continue;
        }
        if (isDir && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        if (isDir && (!allowSubdirs || depth >= MAX_DEPTH)) {
            disk->skipped.subDirs++;
            continue;
        }
        /* Reading such a file would start a download. */
        if (!isDir && (attributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN))) {
            disk->skipped.onlineOnly++;
            continue;
        }
        if (!isDir && found.nFileSizeHigh != 0) {
            disk->skipped.tooLarge++;
            continue;
        }
        if (WideCharToMultiByte(CP_UTF8, 0, found.cFileName, -1, name, sizeof(name), NULL, NULL) == 0) {
            continue;
        }
        node = newNode(parent->path, name, isDir);
        if (node == NULL || !addChild(parent, node)) {
            freeNode(node);
            break;
        }
        node->size = isDir ? 0 : found.nFileSizeLow;
        node->hostTime = ((Int64)found.ftLastWriteTime.dwHighDateTime << 32) | found.ftLastWriteTime.dwLowDateTime;
        if (attributes & FILE_ATTRIBUTE_READONLY) {
            node->attr |= ATTR_READONLY;
        }
        if (FileTimeToLocalFileTime(&found.ftLastWriteTime, &local) && FileTimeToSystemTime(&local, &when)) {
            setDosTime(node, when.wYear, when.wMonth, when.wDay, when.wHour, when.wMinute, when.wSecond);
        }
        else {
            setDosTime(node, 1980, 1, 1, 0, 0, 0);
        }
        if (isDir) {
            enumerate(disk, node, depth + 1, allowSubdirs);
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);
    return 1;
}
#else
static int enumerate(DirDisk* disk, Node* parent, int depth, int allowSubdirs)
{
    DIR* dir = opendir(parent->path);
    struct dirent* entry;

    if (dir == NULL) {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[1024];
        struct stat info;
        struct tm* when;
        Node* node;
        int isDir;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcasecmp(entry->d_name, DIR_TRASH_NAME) == 0) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", parent->path, entry->d_name) >= (int)sizeof(path) ||
            stat(path, &info) != 0) {
            continue;
        }
        isDir = S_ISDIR(info.st_mode);
        if (!isDir && !S_ISREG(info.st_mode)) {
            continue;
        }
        if (isDir && (!allowSubdirs || depth >= MAX_DEPTH)) {
            disk->skipped.subDirs++;
            continue;
        }
        if (!isDir && (Int64)info.st_size > 0xffffffffLL) {
            disk->skipped.tooLarge++;
            continue;
        }
        node = newNode(parent->path, entry->d_name, isDir);
        if (node == NULL || !addChild(parent, node)) {
            freeNode(node);
            break;
        }
        node->size = isDir ? 0 : (UInt32)info.st_size;
        node->hostTime = (Int64)info.st_mtime;
        when = localtime(&info.st_mtime);
        if (when != NULL) {
            setDosTime(node, when->tm_year + 1900, when->tm_mon + 1, when->tm_mday, when->tm_hour, when->tm_min, when->tm_sec);
        }
        else {
            setDosTime(node, 1980, 1, 1, 0, 0, 0);
        }
        if (isDir) {
            enumerate(disk, node, depth + 1, allowSubdirs);
        }
    }
    closedir(dir);
    return 1;
}
#endif

/* A volume label comes first, then the directories, then the files. */
static int nodeRank(const Node* node)
{
    return (node->attr & ATTR_LABEL) ? 0 : (node->attr & ATTR_DIRECTORY) ? 1 : 2;
}

static int compareNodes(const void* a, const void* b)
{
    const unsigned char* x = (const unsigned char*)(*(Node* const*)a)->hostName;
    const unsigned char* y = (const unsigned char*)(*(Node* const*)b)->hostName;
    int rankA = nodeRank(*(Node* const*)a);
    int rankB = nodeRank(*(Node* const*)b);

    if (rankA != rankB) {
        return rankA < rankB ? -1 : 1;
    }

    while (*x != 0 && toupper(*x) == toupper(*y)) {
        x++;
        y++;
    }
    if (toupper(*x) != toupper(*y)) {
        return toupper(*x) < toupper(*y) ? -1 : 1;
    }
    return strcmp((*(Node* const*)a)->hostName, (*(Node* const*)b)->hostName);
}

/* ---------------------------- Short names --------------------------- */

/* The host name in the character codes the MSX uses; what has no code becomes '_'. */
static int nameBytes(const char* hostName, UInt8* out, int capacity)
{
    int length = 0;
#ifdef _WIN32
    wchar_t wide[512];
    int count = MultiByteToWideChar(CP_UTF8, 0, hostName, -1, wide, 512);
    int i;

    for (i = 0; i + 1 < count && length + 2 < capacity; i++) {
        char bytes[4];
        BOOL lost = FALSE;
        int made = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, &wide[i], 1, bytes, sizeof(bytes), NULL, &lost);

        if (made < 1 || made > 2 || lost) {
            /* A surrogate pair is one character. */
            if (wide[i] >= 0xd800 && wide[i] < 0xdc00 && i + 2 < count) {
                i++;
            }
            out[length++] = '_';
            continue;
        }
        memcpy(out + length, bytes, made);
        length += made;
    }
#else
    for (; *hostName != 0 && length + 1 < capacity; hostName++) {
        out[length++] = (UInt8)((unsigned char)*hostName < 0x80 ? *hostName : '_');
    }
#endif
    out[length] = 0;
    return length;
}

/* The number of bytes the character at text takes: two for a kanji. */
static int unitLength(const UInt8* text)
{
    return ((text[0] >= 0x81 && text[0] <= 0x9f) || (text[0] >= 0xe0 && text[0] <= 0xfc)) && text[1] != 0 ? 2 : 1;
}

static int shortNameUnit(const UInt8* text)
{
    UInt8 c = text[0];

    if (unitLength(text) == 2 || (c >= 0xa1 && c <= 0xdf)) {
        return 1;
    }
    return c > 0x20 && c < 0x7f && strchr("\"*+,./:;<=>?[\\]|", c) == NULL;
}

/* Where the extension starts, or the end: at the last dot that is not the first character. */
static int extensionAt(const UInt8* name, int length)
{
    int dot = length;
    int i;

    for (i = 0; i < length; i += unitLength(name + i)) {
        if (name[i] == '.' && i > 0) {
            dot = i;
        }
    }
    return dot;
}

static int fitsShortName(const UInt8* name, int length)
{
    int dot = extensionAt(name, length);
    int i;

    if (dot == 0 || dot > 8 || length - dot > 4) {
        return 0;
    }
    for (i = 0; i < length; i += unitLength(name + i)) {
        if (i != dot && !shortNameUnit(name + i)) {
            return 0;
        }
    }
    return 1;
}

/* Copies whole characters while they fit in room bytes. */
static void copyUnits(const UInt8* from, int length, UInt8* to, int room)
{
    int used = 0;
    int i;

    for (i = 0; i < length; i += unitLength(from + i)) {
        int size = unitLength(from + i);

        if (used + size > room) {
            break;
        }
        if (!shortNameUnit(from + i)) {
            to[used++] = '_';
        }
        else if (size == 2) {
            to[used++] = from[i];
            to[used++] = from[i + 1];
        }
        else {
            to[used++] = (UInt8)(from[i] < 0x80 ? toupper(from[i]) : from[i]);
        }
    }
}

/* tilde 0 keeps the name as it is; otherwise the base ends in ~<tilde>. */
static void makeShortName(const UInt8* name, int length, int tilde, UInt8* out)
{
    int dot = extensionAt(name, length);
    char tag[12] = "";
    UInt8 base[8];
    int used;

    memset(out, ' ', 11);
    memset(base, ' ', 8);
    if (tilde > 0) {
        sprintf(tag, "~%d", tilde);
    }
    copyUnits(name, dot, base, 8 - (int)strlen(tag));
    for (used = 8; used > 0 && base[used - 1] == ' '; used--) {
    }
    memcpy(out, base, used);
    memcpy(out + used, tag, strlen(tag));
    if (dot < length) {
        copyUnits(name + dot + 1, length - dot - 1, out + 8, 3);
    }
}

/* Where name sits in the table of children already named, or the free place for it. */
static int* nameSlot(const Node* parent, int* table, int capacity, const UInt8* name)
{
    UInt32 hash = 2166136261u;
    int i;

    for (i = 0; i < 11; i++) {
        hash = (hash ^ name[i]) * 16777619u;
    }
    for (i = (int)(hash & (UInt32)(capacity - 1)); table[i] >= 0; i = (i + 1) & (capacity - 1)) {
        if (memcmp(parent->child[table[i]]->name, name, 11) == 0) {
            break;
        }
    }
    return &table[i];
}

static void nameChildren(Node* parent)
{
    UInt8 previousFirst[11];
    int previousTilde = 0;
    int capacity = 16;
    int* table;
    int i;

    qsort(parent->child, parent->childCount, sizeof(Node*), compareNodes);
    while (capacity < parent->childCount * 2) {
        capacity *= 2;
    }
    table = (int*)malloc(capacity * sizeof(int));
    if (table == NULL) {
        while (parent->childCount > 0) {
            freeNode(parent->child[--parent->childCount]);
        }
        return;
    }
    memset(table, 0xff, capacity * sizeof(int));
    for (i = 0; i < parent->childCount; i++) {
        Node* node = parent->child[i];
        UInt8 bytes[1024];
        int length = nameBytes(node->hostName, bytes, sizeof(bytes));
        int tilde = fitsShortName(bytes, length) ? 0 : 1;
        int* slot;

        /* A run of names that shorten alike carries on from the number the last one got. */
        if (tilde > 0) {
            makeShortName(bytes, length, 1, node->name);
            if (previousTilde > 0 && memcmp(node->name, previousFirst, 11) == 0) {
                tilde = previousTilde + 1;
            }
            memcpy(previousFirst, node->name, 11);
        }
        for (;;) {
            makeShortName(bytes, length, tilde, node->name);
            slot = nameSlot(parent, table, capacity, node->name);
            if (*slot < 0 || tilde >= 999999) {
                break;
            }
            tilde++;
        }
        if (tilde > 0) {
            previousTilde = tilde;
        }
        *slot = i;
        if (node->isDir) {
            nameChildren(node);
        }
    }
    free(table);
}

/* ------------------------------ Layout ------------------------------ */

/* The numbers a Nextor partitioning tool arrives at for a partition of sizeK. */
static void fat12Layout(UInt32 sizeK, UInt32 rootEntries, Layout* layout)
{
    UInt32 rootSectors = (rootEntries + 15) / 16;
    UInt32 shift = sizeK <= 2048 ? 0 : sizeK <= 4096 ? 1 : sizeK <= 8192 ? 2 : sizeK <= 16384 ? 3 : 4;
    UInt32 maxClusters = 4084;
    UInt32 maxFat = 12;
    UInt32 data;
    UInt32 clusters;
    UInt32 fat;

    if (sizeK <= 16384) {
        maxClusters = 1021;
        maxFat = 3;
        shift += 2;
    }
    data = sizeK * 2 - rootSectors - 1;
    clusters = (data >> shift) & 0xffff;
    fat = ((clusters + 2) * 3) & 0xffff;
    fat = (fat >> 10) + ((fat & 0x3ff) ? 1 : 0);
    clusters = ((data - 2 * fat) >> shift) & 0xffff;
    data = (clusters << shift) & 0xffff;
    if (clusters > maxClusters) {
        data -= (clusters - maxClusters) << shift;
        clusters = maxClusters;
        fat = maxFat;
    }
    layout->fat16       = 0;
    layout->sizeK       = sizeK;
    layout->spc         = 1u << shift;
    layout->fatSectors  = fat;
    layout->clusters    = clusters;
    layout->rootEntries = rootSectors * 16;
    layout->total       = data + 1 + 2 * fat + rootSectors;
}

static void fat16Layout(UInt32 sizeK, UInt32 rootEntries, Layout* layout)
{
    UInt32 rootSectors = (rootEntries + 15) / 16;
    UInt32 sizeM = sizeK >> 10;
    UInt32 shift = sizeM <= 128 ? 2 : sizeM <= 256 ? 3 : sizeM <= 512 ? 4 : sizeM <= 1024 ? 5 : sizeM <= 2048 ? 6 : 7;
    UInt32 data = sizeK * 2 - rootSectors - 1;
    UInt32 clusters = data >> shift;
    UInt32 fat = ((clusters + 2) >> 8) + (((clusters + 2) & 0xff) ? 1 : 0);

    clusters = (data - 2 * fat) >> shift;
    data = clusters << shift;
    if (clusters > 65524) {
        data -= (clusters - 65524) << shift;
        clusters = 65524;
        fat = 256;
    }
    layout->fat16       = 1;
    layout->sizeK       = sizeK;
    layout->spc         = 1u << shift;
    layout->fatSectors  = fat;
    layout->clusters    = clusters;
    layout->rootEntries = rootSectors * 16;
    layout->total       = data + 1 + 2 * fat + rootSectors;
}

static UInt32 clustersFor(UInt32 bytes, UInt32 clusterBytes)
{
    return bytes / clusterBytes + ((bytes % clusterBytes) ? 1 : 0);
}

static UInt32 directoryClusters(const Node* node, UInt32 clusterBytes)
{
    return clustersFor((UInt32)(node->childCount + 2) * 32, clusterBytes);
}

static Int64 clustersNeeded(const Node* node, UInt32 clusterBytes, int isRoot)
{
    Int64 needed = isRoot ? 0 : directoryClusters(node, clusterBytes);
    int i;

    for (i = 0; i < node->childCount; i++) {
        const Node* child = node->child[i];
        needed += child->isDir ? clustersNeeded(child, clusterBytes, 0) : clustersFor(child->size, clusterBytes);
    }
    return needed;
}

static void layoutFor(int format, UInt32 sizeK, int rootCount, Layout* layout)
{
    if (sizeK > 32768) {
        fat16Layout(sizeK, ROOT_MAX, layout);
    }
    else {
        UInt32 limit = format == DiskFormatMsxDos1 ? DOS1_ROOT_MAX : ROOT_MAX;
        UInt32 entries = (UInt32)rootCount + ROOT_SPARE;

        if (entries < 112) {
            entries = 112;
        }
        fat12Layout(sizeK, entries > limit ? limit : entries, layout);
    }
}

/* The smallest size that holds everything with some room left, or the largest the format allows. */
static void chooseLayout(DirDisk* disk)
{
    UInt32 largest = disk->format == DiskFormatMsxDos1 ? 16384 : 4194304;
    UInt32 rootLimit = disk->format == DiskFormatMsxDos1 ? DOS1_ROOT_MAX : ROOT_MAX;
    UInt32 sizeK;

    for (sizeK = 16384; ; sizeK *= 2) {
        Layout* layout = &disk->layout;
        UInt32 clusterBytes;

        layoutFor(disk->format, sizeK, disk->root->childCount, layout);
        clusterBytes = layout->spc * SECTOR;
        if (sizeK >= largest ||
            (((UInt32)disk->root->childCount <= layout->rootEntries || layout->rootEntries >= rootLimit) &&
             clustersNeeded(disk->root, clusterBytes, 1) + RESERVE_BYTES / clusterBytes <= (Int64)layout->clusters)) {
            return;
        }
    }
}

/* ----------------------------- Building ----------------------------- */

static int countFiles(const Node* node)
{
    int count = node->isDir ? 0 : 1;
    int i;

    for (i = 0; i < node->childCount; i++) {
        count += countFiles(node->child[i]);
    }
    return count;
}

static int addExtent(DirDisk* disk, Node* node)
{
    if (disk->extentCount == disk->extentCapacity) {
        int capacity = disk->extentCapacity ? disk->extentCapacity * 2 : 256;
        Node** grown = (Node**)realloc(disk->extent, capacity * sizeof(Node*));

        if (grown == NULL) {
            return 0;
        }
        disk->extent = grown;
        disk->extentCapacity = capacity;
    }
    disk->extent[disk->extentCount++] = node;
    return 1;
}

/* Hands out clusters in directory order; what no longer fits is left out. */
static void allocate(DirDisk* disk, Node* parent, UInt32* next, int isRoot)
{
    UInt32 clusterBytes = disk->layout.spc * SECTOR;
    UInt32 end = disk->layout.clusters + 2;
    UInt32 placed = 0;
    int i;

    for (i = 0; i < parent->childCount; i++) {
        Node* node = parent->child[i];
        UInt32 clusters = node->isDir ? directoryClusters(node, clusterBytes) : clustersFor(node->size, clusterBytes);

        if ((isRoot && placed >= disk->layout.rootEntries) || clusters > end - *next ||
            (clusters > 0 && !addExtent(disk, node))) {
            node->dropped = 1;
            disk->skipped.tooLarge += countFiles(node);
            continue;
        }
        placed++;
        node->clusters = clusters;
        node->first = clusters > 0 ? *next : 0;
        *next += clusters;
        if (node->isDir) {
            allocate(disk, node, next, 0);
        }
    }
}

static void setFat(DirDisk* disk, UInt32 cluster, UInt32 value)
{
    if (disk->layout.fat16) {
        disk->fat[cluster * 2]     = (UInt8)value;
        disk->fat[cluster * 2 + 1] = (UInt8)(value >> 8);
    }
    else {
        UInt32 offset = cluster + cluster / 2;

        if (cluster & 1) {
            disk->fat[offset]     = (UInt8)((disk->fat[offset] & 0x0f) | ((value << 4) & 0xf0));
            disk->fat[offset + 1] = (UInt8)(value >> 4);
        }
        else {
            disk->fat[offset]     = (UInt8)value;
            disk->fat[offset + 1] = (UInt8)((disk->fat[offset + 1] & 0xf0) | ((value >> 8) & 0x0f));
        }
    }
}

static void writeEntry(UInt8* entry, const UInt8* name, const Node* node, UInt32 first)
{
    memcpy(entry, name, 11);
    /* E5 in the first place marks a deleted entry, so a name starting with it is stored with 05. */
    if (entry[0] == 0xe5) {
        entry[0] = 0x05;
    }
    entry[11] = node->attr;
    entry[22] = (UInt8)node->time;
    entry[23] = (UInt8)(node->time >> 8);
    entry[24] = (UInt8)node->date;
    entry[25] = (UInt8)(node->date >> 8);
    entry[26] = (UInt8)first;
    entry[27] = (UInt8)(first >> 8);
    entry[28] = (UInt8)node->size;
    entry[29] = (UInt8)(node->size >> 8);
    entry[30] = (UInt8)(node->size >> 16);
    entry[31] = (UInt8)(node->size >> 24);
}

/* Fills the directory of node and the allocation chains of everything in it. */
static int buildDirectory(DirDisk* disk, Node* node, const Node* parent)
{
    UInt32 bytes = parent == NULL ? disk->layout.rootEntries * 32 : node->clusters * disk->layout.spc * SECTOR;
    UInt8* entry;
    UInt32 c;
    int i;

    node->data = (UInt8*)calloc(1, bytes);
    if (node->data == NULL) {
        return 0;
    }
    entry = node->data;
    if (parent != NULL) {
        writeEntry(entry, (const UInt8*)".          ", node, node->first);
        entry += 32;
        writeEntry(entry, (const UInt8*)"..         ", node, parent == disk->root ? 0 : parent->first);
        entry += 32;
    }
    for (i = 0; i < node->childCount; i++) {
        Node* child = node->child[i];

        if (child->dropped) {
            continue;
        }
        writeEntry(entry, child->name, child, child->first);
        entry += 32;
        for (c = 0; c < child->clusters; c++) {
            setFat(disk, child->first + c, c + 1 < child->clusters ? child->first + c + 1 : 0xffff);
        }
        if (child->isDir && !buildDirectory(disk, child, node)) {
            return 0;
        }
    }
    return 1;
}

static UInt32 volumeId(const char* text)
{
    UInt32 hash = 2166136261u;

    while (*text != 0) {
        hash = (hash ^ (unsigned char)*text++) * 16777619u;
    }
    return hash;
}

static void put16(UInt8* at, UInt32 value)
{
    at[0] = (UInt8)value;
    at[1] = (UInt8)(value >> 8);
}

static void put32(UInt8* at, UInt32 value)
{
    put16(at, value);
    put16(at + 2, value >> 16);
}

static int buildSystemSectors(DirDisk* disk)
{
    const Layout* layout = &disk->layout;
    /* The MSX-DOS 1 floppy boot code refuses a hard disk; the MSX-DOS 2 one starts either system. */
    const unsigned char* bootTemplate = diskFormatBootTemplate(
        disk->format == DiskFormatMsxDos1 ? DiskFormatMsxDos2 : (DiskFormatType)disk->format, layout->fat16);
    UInt32 id = volumeId(disk->directory);

    if (bootTemplate == NULL) {
        return 0;
    }
    memcpy(disk->mbr, "\xeb\xfe\x90NEXTOR20", 11);
    disk->mbr[446 + 4] = layout->fat16 ? 0x06 : 0x01;
    put32(disk->mbr + 446 + 8, 1);
    put32(disk->mbr + 446 + 12, layout->sizeK * 2 - 1);
    disk->mbr[510] = 0x55;
    disk->mbr[511] = 0xaa;

    memcpy(disk->boot, bootTemplate, SECTOR);
    put16(disk->boot + 11, SECTOR);
    disk->boot[13] = (UInt8)layout->spc;
    put16(disk->boot + 14, 1);
    disk->boot[16] = 2;
    put16(disk->boot + 17, layout->rootEntries);
    put16(disk->boot + 19, layout->total < 65536 ? layout->total : 0);
    disk->boot[21] = 0xf0;
    put16(disk->boot + 22, layout->fatSectors);
    memset(disk->boot + 24, 0, 6);
    if (layout->fat16) {
        put32(disk->boot + 32, layout->total);
        put32(disk->boot + 39, id);
    }
    else if (memcmp(disk->boot + 0x20, "VOL_ID", 6) == 0) {
        put32(disk->boot + 0x27, id);
    }
    disk->boot[510] = 0x55;
    disk->boot[511] = 0xaa;
    return 1;
}

DirDisk* dirDiskCreate(const char* directory, int format)
{
    DirDisk* disk = (DirDisk*)calloc(1, sizeof(DirDisk));
    UInt32 next = 2;

    if (disk == NULL) {
        return NULL;
    }
    if (format != DiskFormatMsxDos1 && format != DiskFormatNextor) {
        format = DiskFormatMsxDos2;
    }
    disk->format      = format;
    disk->directory   = copyText(directory);
    disk->root        = newNode(directory, "", 1);
    disk->overlayKey  = (UInt32*)calloc(OVERLAY_HASH, sizeof(UInt32));
    disk->overlaySlot = (UInt32*)calloc(OVERLAY_HASH, sizeof(UInt32));
    if (disk->directory == NULL || disk->root == NULL || disk->overlayKey == NULL || disk->overlaySlot == NULL) {
        dirDiskDestroy(disk);
        return NULL;
    }
    setDosTime(disk->root, 1980, 1, 1, 0, 0, 0);
    if (!enumerate(disk, disk->root, 0, format != DiskFormatMsxDos1)) {
        dirDiskDestroy(disk);
        return NULL;
    }
    nameChildren(disk->root);
    chooseLayout(disk);
    allocate(disk, disk->root, &next, 1);

    disk->dataStart = 1 + 1 + 2 * disk->layout.fatSectors + disk->layout.rootEntries / 16;
    disk->fat = (UInt8*)calloc(disk->layout.fatSectors, SECTOR);
    if (disk->fat == NULL) {
        dirDiskDestroy(disk);
        return NULL;
    }
    setFat(disk, 0, 0xfff0);
    setFat(disk, 1, 0xffff);
    if (!buildDirectory(disk, disk->root, NULL) || !buildSystemSectors(disk)) {
        dirDiskDestroy(disk);
        return NULL;
    }
    return disk;
}

void dirDiskDestroy(DirDisk* disk)
{
    int i;

    if (disk == NULL) {
        return;
    }
    if (disk->openFile != NULL) {
        fclose(disk->openFile);
    }
    for (i = 0; i < OVERLAY_SECTORS / OVERLAY_BLOCK; i++) {
        free(disk->overlayBlock[i]);
    }
    free(disk->overlayKey);
    free(disk->overlaySlot);
    free(disk->extent);
    free(disk->fat);
    freeNode(disk->root);
    free(disk->directory);
    free(disk);
}

Int64 dirDiskGetSize(DirDisk* disk)
{
    return (Int64)disk->layout.sizeK * 2 * SECTOR;
}

const char* dirDiskGetDirectory(DirDisk* disk)
{
    return disk->directory;
}

int dirDiskIsDirPath(const char* path)
{
#ifdef _WIN32
    wchar_t wide[1024];
    DWORD attributes;

    dirDiskWidePath(path, wide, 1024);
    attributes = wide[0] != 0 ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat s;

    return stat(path, &s) == 0 && (s.st_mode & S_IFDIR) != 0;
#endif
}

int dirDiskGetFormat(DirDisk* disk)
{
    return disk->format;
}

void dirDiskGetSkipped(DirDisk* disk, DirSkipped* skipped)
{
    *skipped = disk->skipped;
}

void dirDiskDisarm(DirDisk* disk)
{
    disk->disarmed = 1;
}

int dirDiskIsDisarmed(DirDisk* disk)
{
    return disk->disarmed;
}

int dirDiskWriteBackArmed(DirDisk* disk)
{
    return !disk->disarmed;
}

int dirDiskIsDirty(DirDisk* disk)
{
    return disk->overlayCount > 0;
}

void dirDiskReleaseHostFile(DirDisk* disk)
{
    if (disk->openFile != NULL) {
        fclose(disk->openFile);
    }
    disk->openFile = NULL;
    disk->openNode = NULL;
}

/* ------------------------------ Overlay ----------------------------- */

/* The slot that holds sector, or where it would go; NULL when the table is full. */
static UInt32* overlayFind(DirDisk* disk, UInt32 sector)
{
    UInt32 index = (sector * 2654435761u) >> 14;
    UInt32 probes;

    for (probes = 0; probes < OVERLAY_HASH; probes++) {
        UInt32* key = &disk->overlayKey[(index + probes) & (OVERLAY_HASH - 1)];

        if (*key == 0 || *key == sector + 1) {
            return key;
        }
    }
    return NULL;
}

static UInt8* overlayData(DirDisk* disk, const UInt32* key)
{
    UInt32 slot = disk->overlaySlot[key - disk->overlayKey];

    return disk->overlayBlock[slot / OVERLAY_BLOCK] + (slot % OVERLAY_BLOCK) * SECTOR;
}

static int overlayWrite(DirDisk* disk, UInt32 sector, const UInt8* buffer)
{
    UInt32* key = overlayFind(disk, sector);

    if (key == NULL) {
        return 0;
    }
    if (*key == 0) {
        UInt32 slot = disk->overlayCount;

        if (slot >= OVERLAY_SECTORS) {
            return 0;
        }
        if (disk->overlayBlock[slot / OVERLAY_BLOCK] == NULL) {
            disk->overlayBlock[slot / OVERLAY_BLOCK] = (UInt8*)malloc(OVERLAY_BLOCK * SECTOR);
            if (disk->overlayBlock[slot / OVERLAY_BLOCK] == NULL) {
                return 0;
            }
        }
        disk->overlaySlot[key - disk->overlayKey] = slot;
        disk->overlayCount++;
        *key = sector + 1;
    }
    memcpy(overlayData(disk, key), buffer, SECTOR);
    return 1;
}

/* ------------------------------ Sectors ----------------------------- */

static Node* extentAt(DirDisk* disk, UInt32 cluster)
{
    int low = 0;
    int high = disk->extentCount - 1;

    while (low <= high) {
        int middle = low + (high - low) / 2;
        Node* node = disk->extent[middle];

        if (cluster < node->first) {
            high = middle - 1;
        }
        else if (cluster >= node->first + node->clusters) {
            low = middle + 1;
        }
        else {
            return node;
        }
    }
    return NULL;
}

/* Zero when the host file is gone or is no longer the size it had at mount. */
static int readFileSector(DirDisk* disk, Node* node, UInt32 offset, UInt8* buffer)
{
    UInt32 wanted;

    memset(buffer, 0, SECTOR);
    if (offset >= node->size) {
        return 1;
    }
    if (disk->openNode != node) {
        if (disk->openFile != NULL) {
            fclose(disk->openFile);
        }
        disk->openNode = NULL;
        disk->openFile = openHostFile(node->path);
        if (disk->openFile == NULL) {
            return 0;
        }
        disk->openNode = node;
    }
    if (hostFileSize(disk->openFile) != (Int64)node->size) {
        return 0;
    }
    wanted = node->size - offset < SECTOR ? node->size - offset : SECTOR;
    return seekHostFile(disk->openFile, offset) && fread(buffer, 1, wanted, disk->openFile) == wanted;
}

int dirDiskReadSector(DirDisk* disk, UInt32 sector, UInt8* buffer)
{
    const Layout* layout = &disk->layout;
    const UInt32* key = overlayFind(disk, sector);
    UInt32 rootStart = 2 + 2 * layout->fatSectors;

    if (key != NULL && *key != 0) {
        memcpy(buffer, overlayData(disk, key), SECTOR);
        return 1;
    }
    memset(buffer, 0, SECTOR);
    if (sector == 0) {
        memcpy(buffer, disk->mbr, SECTOR);
    }
    else if (sector == 1) {
        memcpy(buffer, disk->boot, SECTOR);
    }
    else if (sector < rootStart) {
        memcpy(buffer, disk->fat + ((sector - 2) % layout->fatSectors) * SECTOR, SECTOR);
    }
    else if (sector < disk->dataStart) {
        memcpy(buffer, disk->root->data + (sector - rootStart) * SECTOR, SECTOR);
    }
    else if (sector < layout->total + 1) {
        UInt32 cluster = 2 + (sector - disk->dataStart) / layout->spc;
        Node* node = extentAt(disk, cluster);

        if (node != NULL) {
            UInt64 offset = ((UInt64)(cluster - node->first) * layout->spc + (sector - disk->dataStart) % layout->spc) * SECTOR;

            if (!node->isDir) {
                return offset >= node->size || readFileSector(disk, node, (UInt32)offset, buffer);
            }
            memcpy(buffer, node->data + offset, SECTOR);
        }
    }
    return 1;
}

static int inRange(DirDisk* disk, Int64 offset, int length)
{
    return disk != NULL && offset >= 0 && length >= 0 && offset % SECTOR == 0 && length % SECTOR == 0 &&
           offset + length <= dirDiskGetSize(disk);
}

int dirDiskRead(void* ref, Int64 offset, UInt8* buffer, int length)
{
    DirDisk* disk = (DirDisk*)ref;
    int done;

    if (!inRange(disk, offset, length)) {
        return 0;
    }
    for (done = 0; done < length; done += SECTOR) {
        if (!dirDiskReadSector(disk, (UInt32)((offset + done) / SECTOR), buffer + done)) {
            return 0;
        }
    }
    return 1;
}

int dirDiskWrite(void* ref, Int64 offset, const UInt8* buffer, int length)
{
    DirDisk* disk = (DirDisk*)ref;
    int done;

    /* The partition table stays as it is, so the MSX cannot repartition the volume. */
    if (!inRange(disk, offset, length) || offset == 0) {
        return 0;
    }
    for (done = 0; done < length; done += SECTOR) {
        if (!overlayWrite(disk, (UInt32)((offset + done) / SECTOR), buffer + done)) {
            return 0;
        }
    }
    return 1;
}
