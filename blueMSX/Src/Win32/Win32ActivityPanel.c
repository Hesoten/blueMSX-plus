/*****************************************************************************
**
** Agent Activity window.
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
#include "Win32ActivityPanel.h"
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>
#include <string.h>

#include "ControlClient.h"
#include "ControlSocket.h"
#include "ControlAddress.h"
#include "Properties.h"
#include "Win32Common.h"
#include "Win32TextUtf8.h"
#include "Language.h"

#define PANEL_CLASS      L"blueMSXActivityPanel"
#define WM_APP_ACTIVITY  (WM_APP + 1)
#define LIST_SUBCLASS_ID 1
#define REFRESH_TIMER    1
/* At most one refresh this often, so a stream of entries does not take the window's thread. */
#define REFRESH_MS       80

#define FONT_MIN      6
#define FONT_MAX     24
#define FONT_DEFAULT 10

#define COLUMN_TIME      0
#define COLUMN_CLIENT    1
#define COLUMN_OPERATION 2
#define COLUMN_TARGET    3
#define COLUMN_RESULT    4
#define COLUMN_COUNT     5

#define CELL_MAX   256
#define STATUS_MAX 512

typedef char* (*LangText)();

/* Indexed by CTRL_ACT_*; a verb added there needs its name here. DENIED shows its op name. */
static const LangText verbNames[CTRL_ACT_VERB_COUNT] = {
    NULL,
    langActivityVerbRead, langActivityVerbWrite, langActivityVerbRun, langActivityVerbPause,
    langActivityVerbStep, langActivityVerbReset, langActivityVerbBreakSet,
    langActivityVerbBreakClear, langActivityVerbWatchSet, langActivityVerbWatchClear,
    langActivityVerbHuntStart, langActivityVerbHuntRefine, langActivityVerbHuntList,
    langActivityVerbHuntEnd, langActivityVerbConnect, langActivityVerbDisconnect,
    NULL, langActivityVerbRevoked, langActivityVerbKeyTap,
    langActivityVerbLoadSymbols, langActivityVerbUndo, langActivityVerbSnapshot,
    langActivityVerbRegWrite, langActivityVerbBankSwitch, langActivityVerbScreenSave,
    langActivityVerbType, langActivityVerbResetSoft, langActivityVerbKeyPress,
    langActivityVerbKeyRelease, langActivityVerbSnapshotRestore, langActivityVerbStepOver,
    langActivityVerbStepOut, langActivityVerbRunUntilStop, langActivityVerbBreakClearAll,
    langActivityVerbScreenText
};

static const LangText columnTitles[COLUMN_COUNT] = {
    langActivityColTime, langActivityColClient, langActivityColOperation,
    langActivityColTarget, langActivityColResult
};

/* Written only on the window's thread; the logger's listener reads panelWindow once per call. */
static HWND  panelWindow;
static HWND  statusText;
static HWND  logList;
static HFONT panelFont;
static int   fontPoints;
static int   fontHeight;
static char  statusShown[STATUS_MAX];
/* Set by the listener and kept set until the refresh it asked for has run, so a burst posts once. */
static volatile LONG refreshPosted;
static DWORD lastRefresh;

/* Row i shows the entry with seq baseSeq + i. */
static UInt32 baseSeq;
static int    rowCount;

/* The last row formatted, so the cells of one row cost one copy out of the log. */
static CtrlActivityEntry cachedEntry;
static UInt32 cachedSeq;

/* Result as wide as it was dragged, when that is past the right edge; 0 to just fill to it. */
static int resultWanted;
static int fittingResult;

/* ---- the cells ------------------------------------------------------------ */

static void formatTime(const CtrlActivityEntry* entry, char* out, int len)
{
    FILETIME   utc;
    FILETIME   local;
    SYSTEMTIME at;

    utc.dwLowDateTime  = (DWORD)entry->loggedAt;
    utc.dwHighDateTime = (DWORD)(entry->loggedAt >> 32);
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &at)) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)len, "%02u:%02u:%02u.%03u",
             at.wHour, at.wMinute, at.wSecond, at.wMilliseconds);
}

/* A client by its own name and the number the emulator gave it: "mcp #1". */
static int clientName(const char* label, int client, char* out, int len)
{
    return label[0] != 0 ? snprintf(out, (size_t)len, "%s #%d", label, client)
                         : snprintf(out, (size_t)len, "#%d", client);
}

static void formatOperation(const CtrlActivityEntry* entry, char* out, int len)
{
    const char* verb = entry->verb < CTRL_ACT_VERB_COUNT && verbNames[entry->verb] != NULL
                       ? verbNames[entry->verb]() : entry->detail;

    if (entry->count > 1) {
        snprintf(out, (size_t)len, "%s x%u", verb, (unsigned)entry->count);
    }
    else {
        snprintf(out, (size_t)len, "%s", verb);
    }
}

/* The storage first, then the Z80 address it was reached at: slot3.2:seg0+0x0030 (Z80 0xC030). */
static void formatPlace(const CtrlActivityEntry* entry, char* out, int len)
{
    char   start[64];
    UInt32 span = entry->rangeEnd > entry->addr.offset + 1 ? entry->rangeEnd - 1 - entry->addr.offset : 0;
    int    used;

    if (entry->addr.space == CTRL_SPACE_CPU) {
        snprintf(start, sizeof(start), "Z80 0x%04X", (unsigned)entry->addr.offset);
    }
    else {
        ctrlAddrPrint(&entry->addr, start, sizeof(start));
    }
    if (span > 0) {
        used = snprintf(out, (size_t)len, "%s-0x%04X", start, (unsigned)(entry->addr.offset + span));
    }
    else {
        used = snprintf(out, (size_t)len, "%s", start);
    }
    if (entry->z80 < 0 || used < 0 || used >= len) {
        return;
    }
    if (span > 0) {
        snprintf(out + used, (size_t)(len - used), " (Z80 0x%04X-0x%04X)",
                 (unsigned)entry->z80, (unsigned)(entry->z80 + span));
    }
    else {
        snprintf(out + used, (size_t)(len - used), " (Z80 0x%04X)", (unsigned)entry->z80);
    }
}

static void formatTarget(const CtrlActivityEntry* entry, char* out, int len)
{
    if (entry->hasAddr) {
        formatPlace(entry, out, len);
    }
    else if (entry->verb == CTRL_ACT_STEP && entry->amount > 1) {
        snprintf(out, (size_t)len, langActivityInstructions(), (unsigned)entry->amount);
    }
    else {
        snprintf(out, (size_t)len, "%s", entry->target);
    }
}

/* Hex bytes, with "..." when the write was longer than the bytes kept. */
static void formatBytes(const UInt8* bytes, int count, int more, char* out, int len)
{
    int used = 0;
    int i;

    out[0] = 0;
    for (i = 0; i < count && used + 4 < len; i++) {
        used += snprintf(out + used, (size_t)(len - used), i > 0 ? " %02X" : "%02X", bytes[i]);
    }
    if (more && used + 4 < len) {
        strcpy(out + used, " ...");
    }
}

/* The bytes before and as read back after; either side is left out when it could not be read. */
static void formatChange(const CtrlActivityEntry* entry, char* out, int len)
{
    char before[40];
    char after[40];
    char change[CELL_MAX];
    int  more = entry->bytes > CTRL_ACT_PEEK;

    formatBytes(entry->before, entry->beforeLen, more, before, sizeof(before));
    formatBytes(entry->after, entry->afterLen, more, after, sizeof(after));
    if (entry->outcome == CTRL_ACT_NOT_KEPT_ROM ||
        (entry->outcome == CTRL_ACT_NOT_KEPT && entry->beforeLen > 0 &&
         entry->beforeLen == entry->afterLen &&
         memcmp(entry->before, entry->after, entry->beforeLen) == 0))
    {
        snprintf(out, (size_t)len, entry->outcome == CTRL_ACT_NOT_KEPT_ROM
                                   ? langActivityNotKeptRom() : langActivityNotKeptSame(), before);
        return;
    }
    if (entry->beforeLen > 0 && entry->afterLen > 0) {
        snprintf(change, sizeof(change), langActivityWasNow(), before, after);
    }
    else if (entry->afterLen > 0) {
        snprintf(change, sizeof(change), langActivityNow(), after);
    }
    else {
        snprintf(change, sizeof(change), entry->beforeLen > 0 ? langActivityWas() : "%s", before);
    }
    snprintf(out, (size_t)len, entry->outcome == CTRL_ACT_NOT_KEPT ? langActivityNotKeptChange()
                                                                  : "%s", change);
}

/* What it did, before any failure: never a parameter of the action, which the Operation and
** Target show. */
static void formatOutcome(const CtrlActivityEntry* entry, char* out, int len)
{
    switch (entry->verb) {
    case CTRL_ACT_WRITE:
    case CTRL_ACT_UNDO:
        formatChange(entry, out, len);
        return;
    case CTRL_ACT_HUNT_START:
    case CTRL_ACT_HUNT_REFINE:
        /* A failed one may have counted nothing. */
        if (entry->outcome == CTRL_ACT_FAILED) {
            out[0] = 0;
            return;
        }
        snprintf(out, (size_t)len, entry->verb == CTRL_ACT_HUNT_START ? langActivityCandidates()
                                                                      : langActivityLeft(),
                 (unsigned)entry->amount);
        return;
    case CTRL_ACT_CONNECT:
    case CTRL_ACT_DISCONNECT:
    case CTRL_ACT_REVOKED:
    case CTRL_ACT_DENIED:
        /* Their detail is the client's name, or the op name, which Client or Operation shows. */
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)len, "%s", entry->outcome == CTRL_ACT_TIMED_OUT
                                      ? langActivityTimedOut() : entry->detail);
}

/* Why a request failed, from its error code: the common ones in words, else the code. */
static const char* failureReason(const char* code)
{
    static const struct { const char* code; LangText reason; } reasons[] = {
        { "no_machine", langActivityWhyNoMachine },    { "not_paused", langActivityWhyNotPaused },
        { "busy", langActivityWhyBusy },               { "dialog_open", langActivityWhyBusy },
        { "bad_address", langActivityWhyBadAddress },  { "bad_at", langActivityWhyBadAddress },
        { "out_of_range", langActivityWhyOutOfRange }, { "read_only", langActivityWhyReadOnly },
        { NULL, NULL }
    };
    int i;

    for (i = 0; reasons[i].code != NULL; i++) {
        if (strcmp(code, reasons[i].code) == 0) {
            return reasons[i].reason();
        }
    }
    return code;
}

static void formatResult(const CtrlActivityEntry* entry, char* out, int len)
{
    int used;

    if (entry->outcome == CTRL_ACT_READ_ONLY || entry->outcome == CTRL_ACT_CUT_OFF) {
        snprintf(out, (size_t)len, langActivityRefused(), entry->outcome == CTRL_ACT_READ_ONLY
                                                        ? langActivityWhyReadOnly()
                                                        : langActivityWhyCutOff());
        return;
    }
    formatOutcome(entry, out, len);
    used = (int)strlen(out);
    if (entry->outcome != CTRL_ACT_FAILED || used + 2 >= len) {
        return;
    }
    if (used > 0) {
        strcpy(out + used, "  ");
        used += 2;
    }
    if (entry->code[0] != 0) {
        snprintf(out + used, (size_t)(len - used), langActivityFailedWhy(),
                 failureReason(entry->code));
    }
    else {
        snprintf(out + used, (size_t)(len - used), "%s", langActivityFailed());
    }
}

static void formatCell(const CtrlActivityEntry* entry, int column, char* out, int len)
{
    switch (column) {
    case COLUMN_TIME:      formatTime(entry, out, len);      break;
    case COLUMN_CLIENT:    clientName(entry->label, (int)entry->client, out, len); break;
    case COLUMN_OPERATION: formatOperation(entry, out, len); break;
    case COLUMN_TARGET:    formatTarget(entry, out, len);    break;
    default:               formatResult(entry, out, len);    break;
    }
}

/* The entry row shows, from the log; 0 when it has been dropped since the last refresh. */
static const CtrlActivityEntry* rowEntry(int row)
{
    UInt32 seq = baseSeq + (UInt32)row;

    if (row < 0 || row >= rowCount) {
        return NULL;
    }
    if (cachedSeq != seq) {
        if (!ctrlActivityEntryBySeq(seq, &cachedEntry)) {
            return NULL;
        }
        cachedSeq = seq;
    }
    return &cachedEntry;
}

static void copyCellText(const char* utf8, wchar_t* out, int outLen)
{
    if (outLen <= 0) {
        return;
    }
    if (Utf8ToWide(utf8, out, outLen) == 0) {
        out[0] = 0;
    }
    out[outLen - 1] = 0;
}

static void onGetDispInfo(NMLVDISPINFOW* info)
{
    const CtrlActivityEntry* entry;
    char cell[CELL_MAX];

    if (!(info->item.mask & LVIF_TEXT) || info->item.cchTextMax <= 0) {
        return;
    }
    entry = rowEntry(info->item.iItem);
    cell[0] = 0;
    if (entry != NULL) {
        formatCell(entry, info->item.iSubItem, cell, sizeof(cell));
    }
    copyCellText(cell, info->item.pszText, info->item.cchTextMax);
}

/* The Target and Result in full: a column too narrow cuts them. */
static void onGetInfoTip(NMLVGETINFOTIPW* tip)
{
    const CtrlActivityEntry* entry = rowEntry(tip->iItem);
    char target[CELL_MAX];
    char result[CELL_MAX];
    char text[2 * CELL_MAX + 4];

    if (entry == NULL || tip->cchTextMax <= 0) {
        return;
    }
    formatTarget(entry, target, sizeof(target));
    formatResult(entry, result, sizeof(result));
    snprintf(text, sizeof(text), "%s%s%s", target, target[0] && result[0] ? "\r\n" : "", result);
    copyCellText(text, tip->pszText, tip->cchTextMax);
}

/* ---- the status line ------------------------------------------------------ */

static const char* gateMenuText(int level)
{
    switch (level) {
    case CTRL_GATE_READONLY: return langMenuAgentReadOnly();
    case CTRL_GATE_FULL:     return langMenuAgentFull();
    default:                 return langMenuAgentOff();
    }
}

/* Each client still connected by name and number, and whether it was cut off. */
static void formatClients(char* out, int len)
{
    CtrlClientInfo clients[CTRL_CLIENT_MAX];
    char names[STATUS_MAX];
    int  count = ctrlClientList(clients, CTRL_CLIENT_MAX);
    int  used = 0;
    int  i;

    if (count == 0) {
        snprintf(out, (size_t)len, "%s", langActivityNoTool());
        return;
    }
    names[0] = 0;
    for (i = 0; i < count && used >= 0 && used < (int)sizeof(names) - 2; i++) {
        if (i > 0) {
            used += snprintf(names + used, sizeof(names) - (size_t)used, ", ");
        }
        used += clientName(clients[i].label, (int)clients[i].client, names + used,
                           (int)sizeof(names) - used);
        if (clients[i].revoked && used >= 0 && used < (int)sizeof(names)) {
            used += snprintf(names + used, sizeof(names) - (size_t)used, " (%s)",
                             langActivityRevokedTag());
        }
    }
    snprintf(out, (size_t)len, langActivityConnected(), names);
}

static void formatStatus(char* out, int len)
{
    char   clients[STATUS_MAX];
    UInt32 first;
    UInt32 last;
    int    used;

    if (ctrlSocketIsListening()) {
        used = snprintf(out, (size_t)len, langActivityListening(),
                        ctrlSocketPort(), gateMenuText(ctrlGateLevel()));
    }
    else {
        used = snprintf(out, (size_t)len, "%s", ctrlSocketOrphanPending()
                        ? langActivityStillClosing() : langActivityNotListening());
    }
    formatClients(clients, sizeof(clients));
    if (used >= 0 && used < len) {
        used += snprintf(out + used, (size_t)(len - used), "\r\n%s", clients);
    }
    if (ctrlActivitySeqRange(&first, &last) && first > 1 && used >= 0 && used + 2 < len) {
        strcpy(out + used, "\r\n");
        snprintf(out + used + 2, (size_t)(len - used - 2), langActivityDropped(),
                 (unsigned)(first - 1));
    }
}

static int textWidth(HDC dc, const char* text)
{
    SIZE extent;

    return GetTextExtentPoint32U(dc, text, (int)strlen(text), &extent) ? extent.cx : 0;
}

/* The width left for Result; negative when the other columns already pass the edge. */
static int resultFill(void)
{
    RECT client;
    int  fill;
    int  column;

    GetClientRect(logList, &client);
    fill = client.right;
    for (column = 0; column < COLUMN_RESULT; column++) {
        fill -= ListView_GetColumnWidth(logList, column);
    }
    return fill;
}

/* Result reaches the right edge, or past it when dragged wider; past it, the list scrolls. */
static void fitResult(void)
{
    fittingResult = 1;
    ListView_SetColumnWidth(logList, COLUMN_RESULT, resultWanted > 0 && resultWanted > resultFill()
                                                    ? resultWanted : LVSCW_AUTOSIZE_USEHEADER);
    fittingResult = 0;
}

/* A column the user resized; Result is held back to the right edge when made narrower. */
static void onColumnResized(const NMHEADERW* change)
{
    if (fittingResult || change->pitem == NULL || !(change->pitem->mask & HDI_WIDTH)) {
        return;
    }
    if (change->iItem == COLUMN_RESULT) {
        resultWanted = change->pitem->cxy > resultFill() ? change->pitem->cxy : 0;
    }
    fitResult();
}

/* The status text as tall as it wraps to, the list under it. */
static void layout(HWND window)
{
    RECT  client;
    RECT  text;
    HDC   dc;
    HFONT old;
    int   margin = fontHeight / 3;
    int   height;

    GetClientRect(window, &client);
    SetRect(&text, 0, 0, client.right - 2 * margin, 0);
    dc  = GetDC(window);
    old = (HFONT)SelectObject(dc, panelFont);
    DrawTextU(dc, statusShown, (int)strlen(statusShown), &text,
              DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    height = text.bottom - text.top;

    MoveWindow(statusText, margin, margin, client.right - 2 * margin, height, TRUE);
    /* A resize keeps the old pixels, so a new font or wrap would leave stale text. */
    InvalidateRect(statusText, NULL, TRUE);
    MoveWindow(logList, 0, height + 2 * margin, client.right,
               client.bottom - height - 2 * margin, TRUE);
    fitResult();
}

static void showStatus(void)
{
    char status[STATUS_MAX];

    formatStatus(status, sizeof(status));
    if (strcmp(status, statusShown) == 0) {
        return;
    }
    strcpy(statusShown, status);
    SetWindowTextU(statusText, statusShown);
    layout(panelWindow);
}

/* ---- the rows ------------------------------------------------------------- */

/* The rows moved up by shift: the selection and focus go with the entries they were on. */
static void moveSelection(int shift)
{
    static int selected[CTRL_ACT_RING];
    int count = 0;
    int row = -1;
    int focus = ListView_GetNextItem(logList, -1, LVNI_FOCUSED);
    int i;

    while (count < CTRL_ACT_RING &&
           (row = ListView_GetNextItem(logList, row, LVNI_SELECTED)) >= 0) {
        selected[count++] = row;
    }
    if (count == 0 && focus < 0) {
        return;
    }
    ListView_SetItemState(logList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (i = 0; i < count; i++) {
        if (selected[i] >= shift) {
            ListView_SetItemState(logList, selected[i] - shift, LVIS_SELECTED, LVIS_SELECTED);
        }
    }
    if (focus >= shift) {
        ListView_SetItemState(logList, focus - shift, LVIS_FOCUSED, LVIS_FOCUSED);
    }
}

static int rowHeight(void)
{
    RECT row;

    return rowCount > 0 && ListView_GetItemRect(logList, 0, &row, LVIR_BOUNDS)
           ? row.bottom - row.top : fontHeight;
}

/* Follows the newest entry when the last row was in view; otherwise keeps the same entries in view. */
static void refreshRows(int following)
{
    UInt32 first;
    UInt32 last;
    int    count = ctrlActivitySeqRange(&first, &last) ? (int)(last - first + 1) : 0;
    int    shift = rowCount > 0 && count > 0 ? (int)(first - baseSeq) : 0;

    baseSeq   = first;
    rowCount  = count;
    cachedSeq = 0;
    if (shift > 0) {
        moveSelection(shift);
    }
    ListView_SetItemCountEx(logList, count, LVSICF_NOSCROLL);
    /* The vertical scroll bar may have come, so Result is fitted again. */
    fitResult();
    if (following && count > 0) {
        ListView_EnsureVisible(logList, count - 1, FALSE);
    }
    else if (shift > 0) {
        ListView_Scroll(logList, 0, -shift * rowHeight());
    }
    /* Every row may show another entry now, or the last one may have folded in more. */
    InvalidateRect(logList, NULL, FALSE);
}

/* Taken before the status line, whose height can change the rows in view. */
static int followingNewest(void)
{
    return rowCount == 0 ||
           ListView_GetTopIndex(logList) + ListView_GetCountPerPage(logList) >= rowCount;
}

static void refresh(void)
{
    int following = followingNewest();

    lastRefresh = GetTickCount();
    showStatus();
    refreshRows(following);
}

static void refreshNow(HWND window)
{
    KillTimer(window, REFRESH_TIMER);
    InterlockedExchange(&refreshPosted, 0);
    /* A hidden panel catches up when it is shown again. */
    if (IsWindowVisible(window)) {
        refresh();
    }
}

/* A change soon after the last refresh waits on a one-shot timer; the posted flag stays set
** until then, so the logger posts nothing more meanwhile. */
static void onActivity(HWND window)
{
    if (GetTickCount() - lastRefresh < REFRESH_MS) {
        SetTimer(window, REFRESH_TIMER, REFRESH_MS, NULL);
        return;
    }
    refreshNow(window);
}

/* On whichever thread logged the entry, with the log locked: only posts. */
static void activityChanged(void)
{
    HWND window = panelWindow;

    if (window != NULL && InterlockedExchange(&refreshPosted, 1) == 0 &&
        !PostMessageW(window, WM_APP_ACTIVITY, 0, 0))
    {
        InterlockedExchange(&refreshPosted, 0);
    }
}

/* ---- font and columns ----------------------------------------------------- */

static int columnSample(HDC dc, int column)
{
    char sample[64];
    int  width = textWidth(dc, columnTitles[column]());
    int  verb;

    switch (column) {
    case COLUMN_TIME:
        return max(width, textWidth(dc, "00:00:00.000"));
    case COLUMN_CLIENT:
        return max(width, textWidth(dc, "bluemsx-mcp #99"));
    case COLUMN_OPERATION:
        for (verb = 1; verb < CTRL_ACT_VERB_COUNT; verb++) {
            if (verbNames[verb] == NULL) {
                continue;
            }
            snprintf(sample, sizeof(sample), "%s x99", verbNames[verb]());
            width = max(width, textWidth(dc, sample));
        }
        return width;
    default:
        return max(width, textWidth(dc, "slot3.2:seg12+0x1FF0-0x1FFF (Z80 0xDFF0-0xDFFF)"));
    }
}

static void createColumns(HWND window)
{
    HDC   dc = GetDC(window);
    HFONT old = (HFONT)SelectObject(dc, panelFont);
    int   column;

    for (column = 0; column < COLUMN_COUNT; column++) {
        ListViewInsertColumnU(logList, column, columnSample(dc, column) + fontHeight,
                              columnTitles[column]());
    }
    SelectObject(dc, old);
    ReleaseDC(window, dc);
}

static void setColumnTitles(void)
{
    wchar_t   title[64];
    LVCOLUMNW column;
    int       i;

    for (i = 0; i < COLUMN_COUNT; i++) {
        memset(&column, 0, sizeof(column));
        Utf8ToWide(columnTitles[i](), title, (int)(sizeof(title) / sizeof(title[0])));
        column.mask    = LVCF_TEXT;
        column.pszText = title;
        SendMessageW(logList, LVM_SETCOLUMNW, (WPARAM)i, (LPARAM)&column);
    }
}

/* The dialogs' face, Segoe UI, at the panel's own size; columns keep their width in text. */
static void rebuildFont(HWND window)
{
    HFONT font = CreateFontW(-MulDiv(fontPoints, (int)GetDpiForWindow(window), 72), 0, 0, 0,
                             FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
    TEXTMETRICW metrics;
    HDC   dc = GetDC(window);
    HFONT old = (HFONT)SelectObject(dc, font);
    int   oldHeight = fontHeight;
    int   column;

    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    fontHeight = metrics.tmHeight;

    SendMessageW(statusText, WM_SETFONT, (WPARAM)font, FALSE);
    SendMessageW(logList, WM_SETFONT, (WPARAM)font, TRUE);
    if (panelFont != NULL) {
        DeleteObject(panelFont);
    }
    panelFont = font;
    for (column = 0; oldHeight > 0 && column < COLUMN_RESULT; column++) {
        ListView_SetColumnWidth(logList, column,
                                MulDiv(ListView_GetColumnWidth(logList, column), fontHeight, oldHeight));
    }
    if (oldHeight > 0) {
        resultWanted = MulDiv(resultWanted, fontHeight, oldHeight);
    }
    layout(window);
}

static void setFontPoints(HWND window, int points)
{
    if (points < FONT_MIN) points = FONT_MIN;
    if (points > FONT_MAX) points = FONT_MAX;
    if (points != fontPoints) {
        fontPoints = points;
        propGetGlobalProperties()->emulation.activityFontSize = points;
        rebuildFont(window);
    }
}

/* Ctrl+plus, Ctrl+minus, Ctrl+0 and Ctrl+wheel, as in the debugger windows; non-zero if consumed. */
static int fontZoomMessage(UINT message, WPARAM wParam)
{
    if (message == WM_MOUSEWHEEL) {
        if (!(GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)) {
            return 0;
        }
        setFontPoints(panelWindow, fontPoints + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1));
        return 1;
    }
    if (message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0) {
        switch (wParam) {
        case VK_OEM_PLUS:
        case VK_ADD:
            setFontPoints(panelWindow, fontPoints + 1);
            return 1;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            setFontPoints(panelWindow, fontPoints - 1);
            return 1;
        case '0':
        case VK_NUMPAD0:
            setFontPoints(panelWindow, FONT_DEFAULT);
            return 1;
        }
    }
    return 0;
}

/* The list has the keyboard, so the zoom keys are taken here before it sees them. */
static LRESULT CALLBACK listProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR id, DWORD_PTR data)
{
    (void)id;
    (void)data;
    if (fontZoomMessage(message, wParam)) {
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(list, listProc, LIST_SUBCLASS_ID);
    }
    return DefSubclassProc(list, message, wParam, lParam);
}

/* ---- the window ----------------------------------------------------------- */

static void createChildren(HWND window)
{
    HINSTANCE instance = GetModuleHandle(NULL);
    INITCOMMONCONTROLSEX controls;

    controls.dwSize = sizeof(controls);
    controls.dwICC  = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    statusText = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                 0, 0, 0, 0, window, NULL, instance, NULL);
    logList = CreateWindowExW(0, WC_LISTVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
                              LVS_SHOWSELALWAYS,
                              0, 0, 0, 0, window, NULL, instance, NULL);
    ListView_SetExtendedListViewStyle(logList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                               LVS_EX_INFOTIP);
    SetWindowSubclass(logList, listProc, LIST_SUBCLASS_ID, 0);

    fontPoints = propGetGlobalProperties()->emulation.activityFontSize;
    if (fontPoints < FONT_MIN || fontPoints > FONT_MAX) {
        fontPoints = FONT_DEFAULT;
    }
    rebuildFont(window);
    createColumns(window);
}

/* What did not happen as asked: refused, failed, or a write the memory did not keep. */
static int wentWrong(const CtrlActivityEntry* entry)
{
    switch (entry->outcome) {
    case CTRL_ACT_FAILED:
    case CTRL_ACT_NOT_KEPT:
    case CTRL_ACT_NOT_KEPT_ROM:
    case CTRL_ACT_READ_ONLY:
    case CTRL_ACT_CUT_OFF:
        return 1;
    default:
        return 0;
    }
}

/* What went wrong is drawn in amber, so it stands out from what was done. */
static LRESULT onCustomDraw(NMLVCUSTOMDRAW* draw)
{
    const CtrlActivityEntry* entry;

    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        entry = rowEntry((int)draw->nmcd.dwItemSpec);
        if (entry != NULL && wentWrong(entry)) {
            draw->clrText = win32CommonIsDarkMode() ? RGB(255, 196, 80) : RGB(150, 80, 0);
        }
    }
    return CDRF_DODEFAULT;
}

static LRESULT onNotify(const NMHDR* header)
{
    /* The list passes on its header's notices; A and W lay out the width alike. */
    if (header->code == HDN_ITEMCHANGEDW || header->code == HDN_ITEMCHANGEDA) {
        if (GetParent(header->hwndFrom) == logList) {
            onColumnResized((const NMHEADERW*)header);
        }
        return 0;
    }
    if (header->hwndFrom != logList) {
        return 0;
    }
    if (header->code == NM_CUSTOMDRAW) {
        return onCustomDraw((NMLVCUSTOMDRAW*)header);
    }
    if (header->code == LVN_GETDISPINFOW) {
        onGetDispInfo((NMLVDISPINFOW*)header);
    }
    else if (header->code == LVN_GETINFOTIPW) {
        onGetInfoTip((NMLVGETINFOTIPW*)header);
    }
    return 0;
}

static void onDpiChanged(HWND window, const RECT* suggested)
{
    SetWindowPos(window, NULL, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    rebuildFont(window);
}

/* Dark or light as the other windows; the row tooltip follows too. */
static void applyTheme(HWND window)
{
    win32CommonApplyDark(window);
    SetWindowTheme(ListView_GetToolTips(logList),
                   win32CommonIsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

/* The outer width that shows every column and an eight-byte write not kept. */
static int contentWidth(HWND window, UINT dpi)
{
    static const UInt8 sampleBytes[CTRL_ACT_PEEK] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    char  bytes[40];
    char  change[CELL_MAX];
    char  wide[CELL_MAX];
    HDC   dc = GetDC(window);
    HFONT old = (HFONT)SelectObject(dc, panelFont);
    RECT  frame = { 0, 0, 0, 0 };
    int   width;
    int   column;

    formatBytes(sampleBytes, CTRL_ACT_PEEK, 1, bytes, sizeof(bytes));
    snprintf(change, sizeof(change), langActivityWasNow(), bytes, bytes);
    snprintf(wide, sizeof(wide), langActivityNotKeptChange(), change);
    width = textWidth(dc, wide) + 2 * fontHeight + GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    for (column = 0; column < COLUMN_RESULT; column++) {
        width += ListView_GetColumnWidth(logList, column);
    }
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    return width + frame.right - frame.left;
}

/* The emulator's window width at this window's DPI; 0 when it is hidden, minimized or full. */
static int emulatorWidth(HWND window)
{
    HWND emulator = getMainHwnd();
    RECT rect;
    MONITORINFO monitor;

    if (emulator == NULL || !IsWindowVisible(emulator) || IsIconic(emulator) ||
        IsZoomed(emulator) || !GetWindowRect(emulator, &rect))
    {
        return 0;
    }
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfo(MonitorFromWindow(emulator, MONITOR_DEFAULTTONEAREST), &monitor) &&
        rect.right - rect.left >= monitor.rcMonitor.right - monitor.rcMonitor.left)
    {
        return 0;
    }
    return MulDiv(rect.right - rect.left, (int)GetDpiForWindow(window), (int)GetDpiForWindow(emulator));
}

/* As wide as the emulator's window, but not too narrow to read; columns past the edge scroll. */
static void setInitialSize(HWND window)
{
    UINT  dpi = GetDpiForWindow(window);
    int   content = contentWidth(window, dpi);
    int   width = emulatorWidth(window);
    RECT  placed;
    MONITORINFO monitor;

    width = width > 0 ? max(width, min(content, MulDiv(600, (int)dpi, 96))) : content;
    GetWindowRect(window, &placed);
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        width = min(width, monitor.rcWork.right - monitor.rcWork.left);
        placed.left = min(placed.left, monitor.rcWork.right - width);
    }
    SetWindowPos(window, NULL, placed.left, placed.top, width, MulDiv(520, (int)dpi, 96),
                 SWP_NOZORDER);
}

static LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (fontZoomMessage(message, wParam)) {
        return 0;
    }

    switch (message) {
    case WM_CREATE:
        panelWindow = window;
        createChildren(window);
        return 0;

    case WM_APP_ACTIVITY:
        onActivity(window);
        return 0;

    case WM_TIMER:
        refreshNow(window);
        return 0;

    case WM_SIZE:
        layout(window);
        return 0;

    case WM_NOTIFY:
        return onNotify((const NMHDR*)lParam);

    case WM_DPICHANGED:
        onDpiChanged(window, (const RECT*)lParam);
        return 0;

    case WM_SETTINGCHANGE:
        if (lParam != 0 && wcscmp((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) {
            win32InvalidateDarkModeCache();
            applyTheme(window);
        }
        break;

    case WM_ERASEBKGND:
        {
            RECT client;
            GetClientRect(window, &client);
            FillRect((HDC)wParam, &client, win32CommonIsDarkMode() ? win32CommonDarkBgBrush()
                                                                   : GetSysColorBrush(COLOR_BTNFACE));
        }
        return 1;

    case WM_CLOSE:
        /* Hidden, not destroyed: the columns, the scroll position and the listener stay. */
        ShowWindow(window, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(window, REFRESH_TIMER);
        ctrlActivitySetListener(NULL);
        ctrlSocketSetStatusListener(NULL);
        panelWindow = NULL;
        statusText = logList = NULL;
        if (panelFont != NULL) {
            DeleteObject(panelFont);
            panelFont = NULL;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static HWND createPanel(void)
{
    static int registered;
    HWND window;

    if (!registered) {
        WNDCLASSEXW wc;

        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = panelProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = PANEL_CLASS;
        RegisterClassExW(&wc);
        registered = 1;
    }

    /* Unowned, so it stays visible on another monitor while the emulator runs full screen. */
    rowCount = 0;
    resultWanted = 0;
    baseSeq = 0;
    statusShown[0] = 0;
    window = CreateWindowExW(0, PANEL_CLASS, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
                             NULL, NULL, GetModuleHandle(NULL), NULL);
    if (window == NULL) {
        return NULL;
    }
    setInitialSize(window);
    SetWindowTextU(window, langActivityTitle());
    /* After the children exist, so the list and its header take the dark colours too. */
    applyTheme(window);
    refreshPosted = 0;
    ctrlActivitySetListener(activityChanged);
    ctrlSocketSetStatusListener(activityPanelStatusChanged);
    return window;
}

void activityPanelShow(void)
{
    if (panelWindow == NULL && createPanel() == NULL) {
        return;
    }
    refresh();
    ShowWindow(panelWindow, IsIconic(panelWindow) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(panelWindow);
}

void activityPanelDestroy(void)
{
    if (panelWindow != NULL) {
        DestroyWindow(panelWindow);
    }
}

void activityPanelStatusChanged(void)
{
    if (panelWindow != NULL && IsWindowVisible(panelWindow)) {
        refresh();
    }
}

void activityPanelSettingsChanged(void)
{
    if (panelWindow == NULL) {
        return;
    }
    /* The panel's size is its own; a Properties Cancel must not take it back. */
    propGetGlobalProperties()->emulation.activityFontSize = fontPoints;
    SetWindowTextU(panelWindow, langActivityTitle());
    setColumnTitles();
    statusShown[0] = 0;
    refresh();
}
