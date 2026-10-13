/*****************************************************************************
**
** Removable drives of the host inserted as hard disks.
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
#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#include "Win32PhysicalDisk.h"
#include "Win32Common.h"
#include "Win32TextUtf8.h"
#include "Resource.h"
#include "Disk.h"
#include "Actions.h"
#include "Board.h"
#include "Emulator.h"
#include "Properties.h"
#include "FileHistory.h"
#include "Language.h"
#include "ArchMenu.h"

#define HELPER_OPTION      "/physicaldrive"
#define PIPE_PREFIX        "\\\\.\\pipe\\bluemsxplus-physicaldrive-"
#define PROTOCOL_MAGIC     0x31445042
#define HELPER_WAIT_MS     60000
#define LOCK_ATTEMPTS      10
#define LOCK_RETRY_MS      300
#define MAX_DISK_NUMBER    32
#define MAX_VOLUMES        16

typedef struct {
    int    number;
    Int64  size;
    char   product[64];
    char   letters[32];
} DiskInfo;

typedef struct {
    UInt32 magic;
    UInt32 parentPid;
    UInt32 diskNumber;
    Int64  size;
    char   product[64];
} Request;

/* Handle values are those of the receiving process. */
typedef struct {
    UInt32 error;
    UInt32 inUse;
    UInt32 volumeCount;
    UInt64 disk;
    UInt64 volumes[MAX_VOLUMES];
} Reply;

typedef struct {
    HANDLE disk;
    HANDLE volumes[MAX_VOLUMES];
    int    volumeCount;
    int    diskNumber;
} Attachment;

static Attachment attachments[MAXDRIVES];
static DWORD uiThreadId;
static int inserting;

/* ------------------------------ Disks ------------------------------ */

/* Fills what the handle says about its disk; zero unless it is a removable
** disk with 512 byte sectors. */
static int queryDisk(HANDLE disk, DiskInfo* info)
{
    STORAGE_PROPERTY_QUERY query;
    BYTE buffer[1024];
    STORAGE_DEVICE_DESCRIPTOR* descriptor = (STORAGE_DEVICE_DESCRIPTOR*)buffer;
    DISK_GEOMETRY_EX geometry;
    DWORD length;
    char* product;
    int i;

    memset(&query, 0, sizeof(query));
    memset(buffer, 0, sizeof(buffer));
    query.PropertyId = StorageDeviceProperty;
    query.QueryType  = PropertyStandardQuery;
    if (!DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer) - 1, &length, NULL) ||
        !descriptor->RemovableMedia) {
        return 0;
    }
    if (!DeviceIoControl(disk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geometry, sizeof(geometry), &length, NULL) ||
        geometry.Geometry.BytesPerSector != 512 || geometry.DiskSize.QuadPart < 512 ||
        geometry.DiskSize.QuadPart / 512 > 0x7fffffff) {
        return 0;
    }
    info->size = geometry.DiskSize.QuadPart;

    product = descriptor->ProductIdOffset != 0 && descriptor->ProductIdOffset < sizeof(buffer) ? (char*)buffer + descriptor->ProductIdOffset : "";
    while (*product == ' ') {
        product++;
    }
    /* The name ends up in file names and in the settings, so it keeps plain
    ** characters only. */
    for (i = 0; product[i] != 0 && i < (int)sizeof(info->product) - 1; i++) {
        char c = product[i];
        int plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' || c == '-';
        info->product[i] = plain ? c : '_';
    }
    while (i > 0 && info->product[i - 1] == ' ') {
        i--;
    }
    info->product[i] = 0;
    if (i == 0) {
        strcpy(info->product, "Drive");
    }
    return 1;
}

/* The number of the disk a volume lies on, or -1 when it is not on exactly one disk. */
static int volumeDiskNumber(HANDLE volume)
{
    BYTE buffer[sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT)];
    VOLUME_DISK_EXTENTS* extents = (VOLUME_DISK_EXTENTS*)buffer;
    DWORD length;

    if (!DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, buffer, sizeof(buffer), &length, NULL) ||
        extents->NumberOfDiskExtents != 1) {
        return -1;
    }
    return (int)extents->Extents[0].DiskNumber;
}

static int letterDiskNumber(char letter)
{
    char name[] = "\\\\.\\A:";
    HANDLE volume;
    int number;

    name[4] = letter;
    volume = CreateFileA(name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (volume == INVALID_HANDLE_VALUE) {
        return -1;
    }
    number = volumeDiskNumber(volume);
    CloseHandle(volume);
    return number;
}

static int systemDiskNumber(void)
{
    char directory[MAX_PATH];

    if (GetWindowsDirectoryA(directory, sizeof(directory)) == 0 || directory[1] != ':') {
        return -1;
    }
    return letterDiskNumber(directory[0]);
}

static HANDLE openDisk(int number, DWORD access)
{
    char name[64];

    sprintf(name, "\\\\.\\PhysicalDrive%d", number);
    return CreateFileA(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

/* Lists the removable disks other than the one Windows runs from. */
static int listDisks(DiskInfo* list, int capacity)
{
    DWORD drives = GetLogicalDrives();
    int letterDisk[26];
    int systemDisk = systemDiskNumber();
    int count = 0;
    int number;
    int i;

    for (i = 0; i < 26; i++) {
        char root[] = "A:\\";

        root[0] = (char)('A' + i);
        letterDisk[i] = (drives & (1u << i)) && GetDriveTypeA(root) == DRIVE_REMOVABLE ? letterDiskNumber(root[0]) : -1;
    }
    for (number = 0; number < MAX_DISK_NUMBER && count < capacity; number++) {
        DiskInfo* info = &list[count];
        HANDLE disk = openDisk(number, 0);
        int ok;

        if (disk == INVALID_HANDLE_VALUE) {
            continue;
        }
        memset(info, 0, sizeof(*info));
        ok = queryDisk(disk, info);
        CloseHandle(disk);
        if (!ok || systemDisk < 0 || number == systemDisk) {
            continue;
        }
        info->number = number;
        for (i = 0; i < 26; i++) {
            if (letterDisk[i] == number && strlen(info->letters) + 4 < sizeof(info->letters)) {
                sprintf(info->letters + strlen(info->letters), "%s%c:", info->letters[0] ? " " : "", 'A' + i);
            }
        }
        count++;
    }
    return count;
}

static void describeSize(Int64 size, char* text)
{
    if (size >= (Int64)1000 * 1000 * 1000) {
        sprintf(text, "%d GB", (int)(size / (1000 * 1000 * 1000)));
    }
    else {
        sprintf(text, "%d MB", (int)(size / (1000 * 1000)));
    }
}

/* ----------------------------- Medium ------------------------------ */

static int transfer(Attachment* attachment, int write, Int64 offset, UInt8* buffer, int length)
{
    LARGE_INTEGER position;
    DWORD done = 0;

    position.QuadPart = offset;
    if (!SetFilePointerEx(attachment->disk, position, NULL, FILE_BEGIN)) {
        return 0;
    }
    if (write) {
        return WriteFile(attachment->disk, buffer, (DWORD)length, &done, NULL) && done == (DWORD)length;
    }
    return ReadFile(attachment->disk, buffer, (DWORD)length, &done, NULL) && done == (DWORD)length;
}

static int mediumRead(void* ref, Int64 offset, UInt8* buffer, int length)
{
    return transfer((Attachment*)ref, 0, offset, buffer, length);
}

static int mediumWrite(void* ref, Int64 offset, const UInt8* buffer, int length)
{
    return transfer((Attachment*)ref, 1, offset, (UInt8*)buffer, length);
}

static const DiskHostMedium medium = { mediumRead, mediumWrite };

static void closeAttachment(Attachment* attachment)
{
    DWORD length;
    int i;

    if (attachment->disk == NULL) {
        return;
    }
    FlushFileBuffers(attachment->disk);
    /* Windows reads the partition table again, which the MSX may have rewritten. */
    DeviceIoControl(attachment->disk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &length, NULL);
    for (i = 0; i < attachment->volumeCount; i++) {
        CloseHandle(attachment->volumes[i]);
    }
    CloseHandle(attachment->disk);
    memset(attachment, 0, sizeof(*attachment));
}

/* Takes the medium out of the drive before its handles go away. */
static void release(int driveId)
{
    Properties* properties = propGetGlobalProperties();
    int running = emulatorGetState() == EMU_RUNNING;

    if (attachments[driveId].disk == NULL) {
        return;
    }
    if (running) {
        emulatorSuspend();
    }
    diskSetHostMedium(driveId, NULL, NULL, 0);
    closeAttachment(&attachments[driveId]);
    if (diskIsHostMediumName(properties->media.disks[driveId].fileName)) {
        properties->media.disks[driveId].fileName[0] = 0;
        properties->media.disks[driveId].fileNameInZip[0] = 0;
        updateExtendedDiskName(driveId, properties->media.disks[driveId].fileName, properties->media.disks[driveId].fileNameInZip);
    }
    if (running) {
        emulatorResume();
    }
}

int physicalDiskAllowed(int driveId)
{
    HdType type = HD_NONE;

    if (driveId >= MAX_FDC_COUNT && driveId < MAXDRIVES) {
        type = boardGetHdType((driveId - MAX_FDC_COUNT) / MAX_DRIVES_PER_HD);
    }
    return type == HD_MFRSD || type == HD_ROOKIEDRIVE;
}

void physicalDiskSync(void)
{
    Properties* properties = propGetGlobalProperties();
    int driveId;

    if (uiThreadId == 0 || GetCurrentThreadId() != uiThreadId) {
        return;
    }
    for (driveId = 0; driveId < MAXDRIVES; driveId++) {
        if (attachments[driveId].disk != NULL &&
            (!diskIsHostMediumName(properties->media.disks[driveId].fileName) || !diskIsHostMediumOffered(driveId) ||
             (emulatorGetState() != EMU_STOPPED && !physicalDiskAllowed(driveId)))) {
            release(driveId);
        }
    }
}

void physicalDiskEjectAll(void)
{
    int driveId;

    for (driveId = 0; driveId < MAXDRIVES; driveId++) {
        release(driveId);
    }
}

/* --------------------------- Elevated side -------------------------- */

static int sameImageAsSelf(DWORD processId)
{
    wchar_t self[1024];
    wchar_t other[1024];
    DWORD size = _countof(other);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    int same;

    if (process == NULL) {
        return 0;
    }
    same = QueryFullProcessImageNameW(process, 0, other, &size) &&
           GetModuleFileNameW(NULL, self, _countof(self)) != 0 && _wcsicmp(self, other) == 0;
    CloseHandle(process);
    return same;
}

/* Locks and dismounts every volume on the disk, so that Windows neither
** blocks nor disturbs the sectors. */
static DWORD lockVolumes(int diskNumber, HANDLE* volumes, int* count, UInt32* inUse)
{
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, _countof(name));
    DWORD error = 0;

    *count = 0;
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        size_t length = wcslen(name);
        HANDLE volume;
        DWORD done;
        int attempt;
        int locked = 0;

        /* The device is opened by the name without its trailing backslash. */
        if (length == 0 || name[length - 1] != L'\\') {
            continue;
        }
        name[length - 1] = 0;
        volume = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (volume == INVALID_HANDLE_VALUE) {
            continue;
        }
        if (volumeDiskNumber(volume) != diskNumber) {
            CloseHandle(volume);
            continue;
        }
        if (*count == MAX_VOLUMES) {
            CloseHandle(volume);
            error = ERROR_TOO_MANY_OPEN_FILES;
            break;
        }
        for (attempt = 0; attempt < LOCK_ATTEMPTS && !locked; attempt++) {
            if (attempt > 0) {
                Sleep(LOCK_RETRY_MS);
            }
            locked = DeviceIoControl(volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &done, NULL);
        }
        if (!locked) {
            error = GetLastError();
            *inUse = 1;
            CloseHandle(volume);
            break;
        }
        DeviceIoControl(volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &done, NULL);
        volumes[(*count)++] = volume;
    } while (FindNextVolumeW(find, name, _countof(name)));
    FindVolumeClose(find);
    return error;
}

static void serve(HANDLE pipe)
{
    Request request;
    Reply reply;
    DiskInfo info;
    HANDLE volumes[MAX_VOLUMES];
    HANDLE disk = INVALID_HANDLE_VALUE;
    HANDLE parent = NULL;
    HANDLE copy;
    ULONG serverPid = 0;
    DWORD length;
    int volumeCount = 0;
    int systemDisk;
    int i;

    memset(&reply, 0, sizeof(reply));
    memset(&info, 0, sizeof(info));
    reply.error = ERROR_INVALID_PARAMETER;
    if (!ReadFile(pipe, &request, sizeof(request), &length, NULL) || length != sizeof(request) ||
        request.magic != PROTOCOL_MAGIC) {
        goto done;
    }
    /* Only the program this one was started from may ask, and only for a
    ** removable disk other than the one Windows runs from. */
    reply.error = ERROR_ACCESS_DENIED;
    systemDisk = systemDiskNumber();
    if (!GetNamedPipeServerProcessId(pipe, &serverPid) || serverPid != request.parentPid || !sameImageAsSelf(serverPid) ||
        systemDisk < 0 || (int)request.diskNumber == systemDisk) {
        goto done;
    }
    disk = openDisk((int)request.diskNumber, GENERIC_READ | GENERIC_WRITE);
    if (disk == INVALID_HANDLE_VALUE) {
        reply.error = GetLastError();
        goto done;
    }
    request.product[sizeof(request.product) - 1] = 0;
    reply.error = ERROR_BAD_DEVICE;
    if (!queryDisk(disk, &info) || info.size != request.size || strcmp(info.product, request.product) != 0) {
        goto done;
    }
    reply.error = lockVolumes((int)request.diskNumber, volumes, &volumeCount, &reply.inUse);
    if (reply.error != 0) {
        goto done;
    }
    parent = OpenProcess(PROCESS_DUP_HANDLE, FALSE, request.parentPid);
    if (parent == NULL || !DuplicateHandle(GetCurrentProcess(), disk, parent, &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        reply.error = GetLastError();
        goto done;
    }
    reply.disk = (UInt64)(UINT_PTR)copy;
    for (i = 0; i < volumeCount; i++) {
        if (!DuplicateHandle(GetCurrentProcess(), volumes[i], parent, &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            reply.error = GetLastError();
            break;
        }
        reply.volumes[reply.volumeCount++] = (UInt64)(UINT_PTR)copy;
    }

done:
    if (!WriteFile(pipe, &reply, sizeof(reply), &length, NULL) && parent != NULL) {
        /* Nobody will learn of the handles, so they are closed where they were put. */
        if (reply.disk != 0) {
            DuplicateHandle(parent, (HANDLE)(UINT_PTR)reply.disk, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
        }
        for (i = 0; i < (int)reply.volumeCount; i++) {
            DuplicateHandle(parent, (HANDLE)(UINT_PTR)reply.volumes[i], NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
        }
    }
    FlushFileBuffers(pipe);
    for (i = 0; i < volumeCount; i++) {
        CloseHandle(volumes[i]);
    }
    if (disk != INVALID_HANDLE_VALUE) {
        CloseHandle(disk);
    }
    if (parent != NULL) {
        CloseHandle(parent);
    }
}

int physicalDiskHelperCommand(const char* commandLine, int* exitCode)
{
    const char* option = strstr(commandLine, HELPER_OPTION " " PIPE_PREFIX);
    char name[128];
    HANDLE pipe;
    int i;

    if (option == NULL) {
        return 0;
    }
    option += strlen(HELPER_OPTION) + 1;
    for (i = 0; option[i] != 0 && option[i] != ' ' && option[i] != '"' && i < (int)sizeof(name) - 1; i++) {
        name[i] = option[i];
    }
    name[i] = 0;

    *exitCode = 1;
    pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe != INVALID_HANDLE_VALUE) {
        serve(pipe);
        CloseHandle(pipe);
        *exitCode = 0;
    }
    return 1;
}

/* -------------------------- Unelevated side ------------------------- */

/* Waits for an overlapped pipe operation, giving up when the helper ends first or takes too long. */
static int finish(HANDLE pipe, OVERLAPPED* overlapped, BOOL started, HANDLE helper, DWORD expected)
{
    HANDLE handles[2];
    DWORD length = 0;

    handles[0] = overlapped->hEvent;
    handles[1] = helper;
    if (!started) {
        DWORD error = GetLastError();

        if (error == ERROR_PIPE_CONNECTED) {
            return 1;
        }
        if (error != ERROR_IO_PENDING) {
            return 0;
        }
        if (WaitForMultipleObjects(2, handles, FALSE, HELPER_WAIT_MS) != WAIT_OBJECT_0) {
            /* The helper may have answered just before it ended. */
            if (WaitForSingleObject(overlapped->hEvent, 0) != WAIT_OBJECT_0) {
                CancelIo(pipe);
                GetOverlappedResult(pipe, overlapped, &length, TRUE);
                return 0;
            }
        }
    }
    return GetOverlappedResult(pipe, overlapped, &length, TRUE) && length == expected;
}

/* Has an elevated copy of this program open the disk and hand its handles over. */
static DWORD requestHandles(HWND owner, const DiskInfo* info, Attachment* attachment, int* inUse)
{
    char pipeName[128];
    wchar_t exe[1024];
    wchar_t parameters[256];
    SHELLEXECUTEINFOW execute;
    OVERLAPPED overlapped;
    Request request;
    Reply reply;
    HANDLE pipe;
    DWORD error = ERROR_GEN_FAILURE;
    ULONG clientPid = 0;
    UInt32 i;

    memset(&reply, 0, sizeof(reply));
    memset(&overlapped, 0, sizeof(overlapped));
    sprintf(pipeName, PIPE_PREFIX "%lu-%lu", GetCurrentProcessId(), GetTickCount());
    pipe = CreateNamedPipeA(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            1, sizeof(Request), sizeof(Reply), 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (overlapped.hEvent == NULL) {
        error = GetLastError();
        CloseHandle(pipe);
        return error;
    }

    memset(&execute, 0, sizeof(execute));
    GetModuleFileNameW(NULL, exe, _countof(exe));
    _snwprintf(parameters, _countof(parameters), L"%hs %hs", HELPER_OPTION, pipeName);
    execute.cbSize       = sizeof(execute);
    execute.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    execute.hwnd         = owner;
    execute.lpVerb       = L"runas";
    execute.lpFile       = exe;
    execute.lpParameters = parameters;
    execute.nShow        = SW_HIDE;
    if (!ShellExecuteExW(&execute) || execute.hProcess == NULL) {
        error = GetLastError();
        CloseHandle(overlapped.hEvent);
        CloseHandle(pipe);
        return error != 0 ? error : ERROR_GEN_FAILURE;
    }

    memset(&request, 0, sizeof(request));
    request.magic      = PROTOCOL_MAGIC;
    request.parentPid  = GetCurrentProcessId();
    request.diskNumber = (UInt32)info->number;
    request.size       = info->size;
    strcpy(request.product, info->product);

    if (finish(pipe, &overlapped, ConnectNamedPipe(pipe, &overlapped), execute.hProcess, 0) &&
        GetNamedPipeClientProcessId(pipe, &clientPid) && clientPid == GetProcessId(execute.hProcess) &&
        finish(pipe, &overlapped, WriteFile(pipe, &request, sizeof(request), NULL, &overlapped), execute.hProcess, sizeof(request)) &&
        finish(pipe, &overlapped, ReadFile(pipe, &reply, sizeof(reply), NULL, &overlapped), execute.hProcess, sizeof(reply))) {
        error = reply.error;
    }
    WaitForSingleObject(execute.hProcess, HELPER_WAIT_MS);
    CloseHandle(execute.hProcess);
    CloseHandle(overlapped.hEvent);
    CloseHandle(pipe);

    *inUse = reply.inUse != 0;
    attachment->disk = (HANDLE)(UINT_PTR)reply.disk;
    attachment->diskNumber = info->number;
    for (i = 0; i < reply.volumeCount && i < MAX_VOLUMES; i++) {
        attachment->volumes[attachment->volumeCount++] = (HANDLE)(UINT_PTR)reply.volumes[i];
    }
    if (error == 0 && attachment->disk != NULL) {
        DiskInfo received;

        /* The handle itself has to belong to the disk that was chosen. */
        memset(&received, 0, sizeof(received));
        if (!queryDisk(attachment->disk, &received) || received.size != info->size || strcmp(received.product, info->product) != 0) {
            error = ERROR_BAD_DEVICE;
        }
    }
    else if (error == 0) {
        error = ERROR_GEN_FAILURE;
    }
    if (error != 0) {
        closeAttachment(attachment);
    }
    return error;
}

/* ----------------------------- Dialogs ------------------------------ */

typedef struct {
    DiskInfo* list;
    int       count;
    int       selected;
} PickInfo;

/* In dark mode the themed list paints white text on a white selection, so the rows are drawn here. */
static void drawPickRow(const DRAWITEMSTRUCT* item)
{
    int dark = win32CommonIsDarkMode();
    int selected = (item->itemState & ODS_SELECTED) != 0;
    COLORREF back = selected ? (dark ? RGB(240, 240, 240) : GetSysColor(COLOR_HIGHLIGHT))
                             : (dark ? RGB(48, 48, 48) : GetSysColor(COLOR_WINDOW));
    COLORREF text = selected ? (dark ? RGB(32, 32, 32) : GetSysColor(COLOR_HIGHLIGHTTEXT))
                             : (dark ? RGB(240, 240, 240) : GetSysColor(COLOR_WINDOWTEXT));
    HBRUSH brush = CreateSolidBrush(back);
    RECT rect = item->rcItem;
    wchar_t row[256];

    FillRect(item->hDC, &rect, brush);
    DeleteObject(brush);
    if (item->itemID != (UINT)-1 && SendMessageW(item->hwndItem, LB_GETTEXTLEN, item->itemID, 0) < (LRESULT)_countof(row)) {
        SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, (LPARAM)row);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, text);
        rect.left += 3;
        DrawTextW(item->hDC, row, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    if (item->itemState & ODS_FOCUS) {
        DrawFocusRect(item->hDC, &item->rcItem);
    }
}

static BOOL_DLG_RET CALLBACK pickProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    static PickInfo* pick;
    int i;

    switch (iMsg) {
    case WM_INITDIALOG:
        pick = (PickInfo*)lParam;
        SetWindowTextU(hDlg, langDlgPhysDriveTitle());
        SetDlgItemTextU(hDlg, IDC_PHYSDRIVE_TEXT, langDlgPhysDriveChoose());
        SetDlgItemTextU(hDlg, IDOK, langDlgOK());
        SetDlgItemTextU(hDlg, IDCANCEL, langDlgCancel());
        {
            /* The rows were measured before the dialog font was set. */
            HWND list = GetDlgItem(hDlg, IDC_PHYSDRIVE_LIST);
            HDC dc = GetDC(list);
            HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessage(list, WM_GETFONT, 0, 0));
            TEXTMETRIC metrics;

            GetTextMetrics(dc, &metrics);
            SelectObject(dc, old);
            ReleaseDC(list, dc);
            SendMessage(list, LB_SETITEMHEIGHT, 0, metrics.tmHeight + metrics.tmExternalLeading + 4);
        }
        for (i = 0; i < pick->count; i++) {
            char size[32];
            char row[160];

            describeSize(pick->list[i].size, size);
            sprintf(row, "%s   %s   %s", pick->list[i].product, size, pick->list[i].letters);
            ListBoxAddStringU(GetDlgItem(hDlg, IDC_PHYSDRIVE_LIST), row);
        }
        SendDlgItemMessage(hDlg, IDC_PHYSDRIVE_LIST, LB_SETCURSEL, 0, 0);
        win32CommonApplyDark(hDlg);
        return TRUE;

    case WM_DRAWITEM:
        if (wParam == IDC_PHYSDRIVE_LIST) {
            drawPickRow((const DRAWITEMSTRUCT*)lParam);
            return TRUE;
        }
        return FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PHYSDRIVE_LIST && HIWORD(wParam) != LBN_DBLCLK) {
            return FALSE;
        }
        switch (LOWORD(wParam)) {
        case IDC_PHYSDRIVE_LIST:
        case IDOK:
            pick->selected = (int)SendDlgItemMessage(hDlg, IDC_PHYSDRIVE_LIST, LB_GETCURSEL, 0, 0);
            EndDialog(hDlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

static void shiftControl(HWND hDlg, int id, int dy)
{
    HWND control = GetDlgItem(hDlg, id);
    RECT rect;

    GetWindowRect(control, &rect);
    MapWindowPoints(NULL, hDlg, (POINT*)&rect, 2);
    SetWindowPos(control, NULL, rect.left, rect.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Makes the dialog as tall as its text needs in the current language and font. */
static void fitWarningText(HWND hDlg)
{
    HWND text = GetDlgItem(hDlg, IDC_PHYSDRIVE_TEXT);
    wchar_t message[4096];
    MONITORINFO monitor;
    RECT have;
    RECT want;
    RECT window;
    RECT icon;
    HFONT oldFont;
    HDC dc;
    int top;
    int dy;

    GetClientRect(text, &have);
    want = have;
    GetWindowTextW(text, message, _countof(message));
    dc = GetDC(text);
    oldFont = (HFONT)SelectObject(dc, (HFONT)SendMessage(text, WM_GETFONT, 0, 0));
    DrawTextW(dc, message, -1, &want, DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    ReleaseDC(text, dc);

    /* The text is never shorter than the icon beside it. */
    GetWindowRect(GetDlgItem(hDlg, IDC_PHYSDRIVE_ICON), &icon);
    if (want.bottom < icon.bottom - icon.top) {
        want.bottom = icon.bottom - icon.top;
    }
    dy = want.bottom - have.bottom;
    SetWindowPos(text, NULL, 0, 0, have.right, want.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    shiftControl(hDlg, IDC_PHYSDRIVE_NOWARNING, dy);
    shiftControl(hDlg, IDOK, dy);
    shiftControl(hDlg, IDC_PHYSDRIVE_OTHER, dy);
    shiftControl(hDlg, IDCANCEL, dy);
    GetWindowRect(hDlg, &window);
    top = window.top - dy / 2;
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfo(MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST), &monitor) && top < monitor.rcWork.top) {
        top = monitor.rcWork.top;
    }
    SetWindowPos(hDlg, NULL, window.left, top, window.right - window.left, window.bottom - window.top + dy,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

typedef struct {
    const char* text;
    const char* checkLabel;
    int*        checked;
    const char* yesLabel;
    const char* otherLabel;
} WarningInfo;

/* Spreads the three buttons over the width the two took. */
static void placeThreeButtons(HWND hDlg)
{
    RECT yes;
    RECT cancel;
    RECT client;
    int width;
    int gap;
    int left;

    GetWindowRect(GetDlgItem(hDlg, IDOK), &yes);
    GetWindowRect(GetDlgItem(hDlg, IDCANCEL), &cancel);
    MapWindowPoints(NULL, hDlg, (POINT*)&yes, 2);
    MapWindowPoints(NULL, hDlg, (POINT*)&cancel, 2);
    GetClientRect(hDlg, &client);
    width = yes.right - yes.left;
    gap = cancel.left - yes.right;
    left = (client.right - 3 * width - 2 * gap) / 2;
    SetWindowPos(GetDlgItem(hDlg, IDOK), NULL, left, yes.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hDlg, IDC_PHYSDRIVE_OTHER), NULL, left + width + gap, yes.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hDlg, IDCANCEL), NULL, left + 2 * (width + gap), yes.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static BOOL_DLG_RET CALLBACK warningProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    const WarningInfo* info = (const WarningInfo*)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (iMsg) {
    case WM_INITDIALOG:
        info = (const WarningInfo*)lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, lParam);
        SetWindowTextU(hDlg, langWarningTitle());
        SendDlgItemMessage(hDlg, IDC_PHYSDRIVE_ICON, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_WARNING), 0);
        SetDlgItemTextU(hDlg, IDC_PHYSDRIVE_TEXT, info->text);
        fitWarningText(hDlg);
        if (info->checkLabel != NULL) {
            SetDlgItemTextU(hDlg, IDC_PHYSDRIVE_NOWARNING, info->checkLabel);
        }
        else {
            ShowWindow(GetDlgItem(hDlg, IDC_PHYSDRIVE_NOWARNING), SW_HIDE);
        }
        SetDlgItemTextU(hDlg, IDOK, info->yesLabel);
        SetDlgItemTextU(hDlg, IDCANCEL, langDlgCancel());
        if (info->otherLabel != NULL) {
            SetDlgItemTextU(hDlg, IDC_PHYSDRIVE_OTHER, info->otherLabel);
            placeThreeButtons(hDlg);
            ShowWindow(GetDlgItem(hDlg, IDC_PHYSDRIVE_OTHER), SW_SHOW);
        }
        win32CommonApplyDark(hDlg);
        SetFocus(GetDlgItem(hDlg, IDCANCEL));
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (info->checked != NULL) {
                *info->checked = info->checkLabel != NULL && IsDlgButtonChecked(hDlg, IDC_PHYSDRIVE_NOWARNING) == BST_CHECKED;
            }
            EndDialog(hDlg, 1);
            return TRUE;
        case IDC_PHYSDRIVE_OTHER:
            EndDialog(hDlg, 2);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, 0);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

int win32WarningDialog(HWND owner, const char* text, const char* checkLabel, int* checked, const char* yesLabel, const char* otherLabel)
{
    WarningInfo info;
    INT_PTR answer;

    info.text       = text;
    info.checkLabel = checkLabel;
    info.checked    = checked;
    info.yesLabel   = yesLabel;
    info.otherLabel = otherLabel;
    answer = DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PHYSDRIVE_WARNING), owner, warningProc, (LPARAM)&info);
    if (answer == -1) {
        /* The dialog could not be made; the question still has to be put. */
        int choice = MessageBoxU(owner, text, langWarningTitle(), (otherLabel != NULL ? MB_YESNOCANCEL : MB_OKCANCEL) | MB_ICONWARNING);

        return choice == IDYES || choice == IDOK ? 1 : choice == IDNO ? 2 : 0;
    }
    return answer == 1 || answer == 2 ? (int)answer : 0;
}

/* ------------------------------ Insert ------------------------------ */

/* Asks which disk and whether to go on; zero when the user backs out. */
static int choose(HWND owner, Properties* properties, DiskInfo* chosen)
{
    DiskInfo list[MAX_DISK_NUMBER];
    PickInfo pick;

    pick.list     = list;
    pick.count    = listDisks(list, MAX_DISK_NUMBER);
    pick.selected = -1;
    if (pick.count == 0) {
        MessageBoxU(owner, langDlgPhysDriveNone(), langDlgPhysDriveTitle(), MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PHYSDRIVE), owner, pickProc, (LPARAM)&pick) != TRUE ||
        pick.selected < 0 || pick.selected >= pick.count) {
        return 0;
    }
    if (properties->diskdrive.physicalDriveWarning) {
        int noWarning = 0;

        if (win32WarningDialog(owner, langDlgPhysDriveWarning(), langDlgPhysDriveNoWarning(), &noWarning, langDlgPhysDriveContinue(), NULL) != 1) {
            return 0;
        }
        if (noWarning) {
            properties->diskdrive.physicalDriveWarning = 0;
        }
    }
    *chosen = list[pick.selected];
    return 1;
}

void physicalDiskInsert(int driveId)
{
    Properties* properties = propGetGlobalProperties();
    HWND owner = getMainHwnd();
    Attachment attachment;
    DiskInfo info;
    DWORD error;
    int inUse = 0;
    int other;
    int chosen;

    if (inserting || !physicalDiskAllowed(driveId) || !actionHarddiskCanChange(driveId, 0)) {
        return;
    }
    inserting = 1;
    uiThreadId = GetCurrentThreadId();

    emulatorSuspend();
    enterDialogShow();
    chosen = choose(owner, properties, &info) && actionDirWriteBack(driveId);
    if (chosen) {
        actionHarddiskEjectDir(driveId);
    }
    if (chosen && attachments[driveId].disk != NULL && attachments[driveId].diskNumber == info.number) {
        chosen = 0;
    }
    if (chosen) {
        /* A disk is in one drive at a time. */
        for (other = 0; other < MAXDRIVES; other++) {
            if (attachments[other].disk != NULL && attachments[other].diskNumber == info.number) {
                release(other);
            }
        }
        memset(&attachment, 0, sizeof(attachment));
        error = requestHandles(owner, &info, &attachment, &inUse);
        if (error == 0) {
            char size[32];
            char* name = properties->media.disks[driveId].fileName;

            release(driveId);
            attachments[driveId] = attachment;
            diskSetHostMedium(driveId, &medium, &attachments[driveId], info.size);
            describeSize(info.size, size);
            sprintf(name, DISK_HOSTMEDIUM_PREFIX "%s %s", info.product, size);
            properties->media.disks[driveId].fileNameInZip[0] = 0;
            updateExtendedDiskName(driveId, name, properties->media.disks[driveId].fileNameInZip);
            if (emulatorGetState() != EMU_STOPPED) {
                boardChangeDiskette(driveId, name, NULL);
            }
        }
        else if (error != ERROR_CANCELLED) {
            MessageBoxU(owner, inUse ? langDlgPhysDriveInUse() : langDlgPhysDriveFailed(), langDlgPhysDriveTitle(), MB_OK | MB_ICONERROR);
        }
    }
    exitDialogShow();
    emulatorResume();
    inserting = 0;
    archUpdateMenu(0);
}
