/*****************************************************************************
**
** A window with one row per sound mixer channel instance.
** Copyright (C) 2026 madscient
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
#include <commctrl.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "Win32MixerDevices.h"
#include "Win32TextUtf8.h"
#include "AudioMixer.h"
#include "Properties.h"
#include "Language.h"

/* The theme mixer is where the types are set; this window sets the
** instances on top of them, grouped under the same type names the theme's
** strips carry. */

#define MAX_ROWS       128
#define TIMER_REFRESH  1
#define REFRESH_MS     50

#define CTRL_BASE      100
#define CTRL_ENABLE    0
#define CTRL_VOLUME    1
#define CTRL_PAN       2
#define CTRL_METER     3
#define CTRL_PER_ROW   4

/* Layout in pixels at 96 dpi. */
#define MARGIN         8
#define HEADER_H       20
#define ROW_H          28
#define CONTROL_H      24
#define LABEL_X        20
#define LABEL_W        170
#define ENABLE_X       194
#define ENABLE_W       44
#define VOLUME_X       242
#define VOLUME_W       160
#define PAN_X          408
#define PAN_W          110
#define METER_X        526
#define METER_W        64
#define METER_H        12
#define CLIENT_W       (METER_X + METER_W + MARGIN)
#define MAX_START_H    480

typedef struct {
    Int32 handle;
    Int32 type;
    char  id[MIXER_CHANNEL_ID_LENGTH];
    int   enable;
    int   volume;
    int   pan;
    int   levelLeft;
    int   levelRight;
    HWND  meter;
} Row;

static HWND   window;
static HFONT  font;
static HFONT  boldFont;
static int    dpi = 96;
static Row    rows[MAX_ROWS];
static int    rowCount;
static UInt32 generation;
static int    contentHeight;
static int    scrollPos;

static int scale(int v)
{
    return MulDiv(v, dpi, 96);
}

static const char* typeLabel(int type)
{
    switch (type) {
    case MIXER_CHANNEL_PSG:        return "PSG";
    case MIXER_CHANNEL_SCC:        return "SCC";
    case MIXER_CHANNEL_MSXMUSIC:   return "MSX-MUSIC";
    case MIXER_CHANNEL_MSXAUDIO:   return "MSX-AUDIO";
    case MIXER_CHANNEL_MOONSOUND:  return "MoonSound";
    case MIXER_CHANNEL_YAMAHA_SFG: return "SFG";
    case MIXER_CHANNEL_KEYBOARD:   return "Keyboard";
    case MIXER_CHANNEL_PCM:        return "PCM";
    case MIXER_CHANNEL_IO:         return "I/O";
    case MIXER_CHANNEL_CASSETTE:   return "Cassette";
    }
    return propGetMixerTypeName(type) ? propGetMixerTypeName(type) : "?";
}

static void locationLabel(const MixerChannelInfo* info, char* text, int size)
{
    int n;

    if (info->location == MIXER_LOCATION_SLOT) {
        n = _snprintf(text, size, "%s %d-%d", langMixerDevicesSlot(), info->slot, info->sslot);
    }
    else if (info->port >= 0) {
        n = _snprintf(text, size, "%s I/O %02Xh", langMixerDevicesBoard(), info->port);
    }
    else {
        n = _snprintf(text, size, "%s", langMixerDevicesBoard());
    }
    if (n < 0 || n >= size) {
        text[size - 1] = 0;
        return;
    }
    if (info->name[0] && n + 1 + (int)strlen(info->name) < size) {
        const char* c;
        text[n++] = ' ';
        for (c = info->name; *c; c++) {
            text[n++] = (char)toupper((unsigned char)*c);
        }
        text[n] = 0;
    }
    if (info->ordinal > 1) {
        _snprintf(text + n, size - n, " #%d", info->ordinal);
        text[size - 1] = 0;
    }
}

static HWND addControl(const char* className, const char* text, DWORD style,
                       int x, int y, int w, int h, int id, HFONT controlFont)
{
    HWND control = CreateWindowExA(0, className, "", WS_CHILD | WS_VISIBLE | style,
                                   scale(x), y, scale(w), h, window,
                                   (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    if (control != NULL) {
        SendMessage(control, WM_SETFONT, (WPARAM)controlFont, FALSE);
        if (text != NULL) {
            SetWindowTextU(control, text);
        }
    }
    return control;
}

static HWND addTrackbar(int x, int y, int w, int id, int value, int center)
{
    HWND bar = addControl(TRACKBAR_CLASSA, NULL,
                          TBS_HORZ | (center ? TBS_AUTOTICKS : TBS_NOTICKS),
                          x, y, w, scale(CONTROL_H), id, font);
    if (bar != NULL) {
        SendMessage(bar, TBM_SETRANGE, FALSE, MAKELONG(0, 100));
        SendMessage(bar, TBM_SETPAGESIZE, 0, 10);
        if (center) {
            SendMessage(bar, TBM_SETTICFREQ, 50, 0);
        }
        SendMessage(bar, TBM_SETPOS, TRUE, value);
    }
    return bar;
}

static void updateScrollBar(void)
{
    SCROLLINFO si;
    RECT rc;

    GetClientRect(window, &rc);
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = contentHeight - 1;
    si.nPage  = rc.bottom;
    si.nPos   = scrollPos;
    SetScrollInfo(window, SB_VERT, &si, TRUE);
}

static int maxScroll(void)
{
    RECT rc;
    GetClientRect(window, &rc);
    return contentHeight > rc.bottom ? contentHeight - rc.bottom : 0;
}

static void scrollTo(int pos)
{
    int limit = maxScroll();
    if (pos > limit) pos = limit;
    if (pos < 0) pos = 0;
    if (pos != scrollPos) {
        ScrollWindowEx(window, 0, scrollPos - pos, NULL, NULL, NULL, NULL,
                       SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        scrollPos = pos;
    }
    updateScrollBar();
}

/* Registration order within a type, types in their mixer order. */
static void sortByType(MixerChannelInfo* info, int count)
{
    int i, j;
    for (i = 1; i < count; i++) {
        MixerChannelInfo entry = info[i];
        for (j = i; j > 0 && info[j - 1].type > entry.type; j--) {
            info[j] = info[j - 1];
        }
        info[j] = entry;
    }
}

static void rebuild(void)
{
    static MixerChannelInfo info[MAX_ROWS];
    Mixer* mixer = mixerGetGlobalMixer();
    HWND child;
    int count = 0;
    int groups = 0;
    int lastType;
    int i;
    int y;

    generation = mixer != NULL ? mixerGetChannelGeneration(mixer) : 0;
    if (mixer != NULL) {
        count = mixerGetChannelInfo(mixer, info, MAX_ROWS);
    }
    if (count > MAX_ROWS) {
        count = MAX_ROWS;
    }
    sortByType(info, count);

    for (i = 0, lastType = -1; i < count; i++) {
        if (info[i].type != lastType) {
            groups++;
            lastType = info[i].type;
        }
    }
    contentHeight = scale(MARGIN + HEADER_H + groups * HEADER_H + count * ROW_H + MARGIN);
    if (scrollPos > maxScroll()) {
        scrollPos = maxScroll();
    }

    SendMessage(window, WM_SETREDRAW, FALSE, 0);
    while ((child = GetWindow(window, GW_CHILD)) != NULL) {
        DestroyWindow(child);
    }
    rowCount = 0;

    y = scale(MARGIN) - scrollPos;
    if (count == 0) {
        addControl("STATIC", langMixerDevicesNone(), SS_LEFT,
                   MARGIN, y, CLIENT_W - 2 * MARGIN, scale(HEADER_H), -1, font);
    }
    else {
        addControl("STATIC", langMixerDevicesEnable(), SS_CENTER, ENABLE_X, y, ENABLE_W, scale(HEADER_H), -1, font);
        addControl("STATIC", langMixerDevicesVolume(), SS_CENTER, VOLUME_X, y, VOLUME_W, scale(HEADER_H), -1, font);
        addControl("STATIC", langMixerDevicesPan(),    SS_CENTER, PAN_X,    y, PAN_W,    scale(HEADER_H), -1, font);
        addControl("STATIC", langMixerDevicesLevel(),  SS_CENTER, METER_X,  y, METER_W,  scale(HEADER_H), -1, font);
    }
    y += scale(HEADER_H);

    for (i = 0, lastType = -1; i < count; i++) {
        Row* row = rows + rowCount;
        int id = CTRL_BASE + rowCount * CTRL_PER_ROW;
        int controlY = y + (scale(ROW_H) - scale(CONTROL_H)) / 2;
        char label[64];
        HWND enable;

        if (info[i].type != lastType) {
            addControl("STATIC", typeLabel(info[i].type), SS_LEFT,
                       MARGIN, y + scale(HEADER_H) / 4, LABEL_X + LABEL_W - MARGIN, scale(HEADER_H), -1, boldFont);
            y += scale(HEADER_H);
            controlY += scale(HEADER_H);
            lastType = info[i].type;
        }

        row->handle     = info[i].handle;
        row->type       = info[i].type;
        row->enable     = info[i].enable;
        row->volume     = info[i].volume;
        row->pan        = info[i].pan;
        row->levelLeft  = 0;
        row->levelRight = 0;
        strcpy(row->id, info[i].id);

        locationLabel(info + i, label, sizeof(label));
        addControl("STATIC", label, SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                   LABEL_X, controlY, LABEL_W, scale(CONTROL_H), -1, font);
        enable = addControl("BUTTON", NULL, BS_AUTOCHECKBOX,
                            ENABLE_X + ENABLE_W / 2 - 7, controlY, 16, scale(CONTROL_H), id + CTRL_ENABLE, font);
        SendMessage(enable, BM_SETCHECK, row->enable ? BST_CHECKED : BST_UNCHECKED, 0);
        addTrackbar(VOLUME_X, controlY, VOLUME_W, id + CTRL_VOLUME, row->volume, 0);
        addTrackbar(PAN_X, controlY, PAN_W, id + CTRL_PAN, row->pan, 1);
        row->meter = addControl("STATIC", NULL, SS_OWNERDRAW,
                                METER_X, y + (scale(ROW_H) - scale(METER_H)) / 2, METER_W, scale(METER_H),
                                id + CTRL_METER, font);

        y += scale(ROW_H);
        rowCount++;
    }

    SendMessage(window, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(window, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    updateScrollBar();
}

static Row* rowOfControl(HWND control, int* kind)
{
    int id = GetDlgCtrlID(control) - CTRL_BASE;
    if (id < 0 || id / CTRL_PER_ROW >= rowCount) {
        return NULL;
    }
    *kind = id % CTRL_PER_ROW;
    return rows + id / CTRL_PER_ROW;
}

static void apply(Row* row)
{
    Mixer* mixer = mixerGetGlobalMixer();

    if (mixer != NULL) {
        mixerEnableChannel(mixer, row->handle, row->enable);
        mixerSetChannelVolume(mixer, row->handle, row->volume);
        mixerSetChannelPan(mixer, row->handle, row->pan);
    }
    propSetMixerInstance(propGetGlobalProperties(), row->type, row->id,
                         row->enable, row->volume, row->pan);
}

static void refreshMeters(void)
{
    Mixer* mixer = mixerGetGlobalMixer();
    int i;

    if (mixer == NULL) {
        return;
    }
    if (mixerGetChannelGeneration(mixer) != generation) {
        rebuild();
        return;
    }
    for (i = 0; i < rowCount; i++) {
        int left  = mixerMeterGetChannelPercent(mixer, rows[i].handle, MIXER_CHANNEL_LEFT);
        int right = mixerMeterGetChannelPercent(mixer, rows[i].handle, MIXER_CHANNEL_RIGHT);
        if (left != rows[i].levelLeft || right != rows[i].levelRight) {
            rows[i].levelLeft  = left;
            rows[i].levelRight = right;
            InvalidateRect(rows[i].meter, NULL, FALSE);
        }
    }
}

static void drawMeter(const DRAWITEMSTRUCT* item)
{
    int kind;
    Row* row = rowOfControl(item->hwndItem, &kind);
    RECT rc = item->rcItem;
    RECT bar;
    HBRUSH back = CreateSolidBrush(RGB(32, 32, 32));
    HBRUSH lit  = CreateSolidBrush(RGB(0, 200, 64));
    int width = rc.right - rc.left;
    int half  = (rc.bottom - rc.top) / 2;

    FillRect(item->hDC, &rc, back);
    if (row != NULL) {
        bar = rc;
        bar.bottom = rc.top + half - 1;
        bar.right  = rc.left + width * (row->levelLeft > 100 ? 100 : row->levelLeft) / 100;
        FillRect(item->hDC, &bar, lit);
        bar = rc;
        bar.top   = rc.top + half + 1;
        bar.right = rc.left + width * (row->levelRight > 100 ? 100 : row->levelRight) / 100;
        FillRect(item->hDC, &bar, lit);
    }
    DeleteObject(back);
    DeleteObject(lit);
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_HSCROLL:
        if (lParam != 0) {
            int kind;
            Row* row = rowOfControl((HWND)lParam, &kind);
            if (row != NULL) {
                int pos = (int)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
                if (kind == CTRL_VOLUME) row->volume = pos;
                if (kind == CTRL_PAN)    row->pan    = pos;
                apply(row);
            }
        }
        return 0;

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED && lParam != 0) {
            int kind;
            Row* row = rowOfControl((HWND)lParam, &kind);
            if (row != NULL && kind == CTRL_ENABLE) {
                row->enable = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                apply(row);
            }
        }
        return 0;

    case WM_DRAWITEM:
        drawMeter((const DRAWITEMSTRUCT*)lParam);
        return TRUE;

    case WM_TIMER:
        if (wParam == TIMER_REFRESH) {
            refreshMeters();
        }
        return 0;

    case WM_VSCROLL:
        {
            SCROLLINFO si;
            int pos = scrollPos;
            si.cbSize = sizeof(si);
            si.fMask  = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            switch (LOWORD(wParam)) {
            case SB_LINEUP:        pos -= scale(ROW_H); break;
            case SB_LINEDOWN:      pos += scale(ROW_H); break;
            case SB_PAGEUP:        pos -= si.nPage;     break;
            case SB_PAGEDOWN:      pos += si.nPage;     break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = si.nTrackPos;  break;
            case SB_TOP:           pos = 0;             break;
            case SB_BOTTOM:        pos = contentHeight; break;
            }
            scrollTo(pos);
        }
        return 0;

    case WM_MOUSEWHEEL:
        scrollTo(scrollPos - GET_WHEEL_DELTA_WPARAM(wParam) * 3 * scale(ROW_H) / WHEEL_DELTA);
        return 0;

    case WM_SIZE:
        scrollTo(scrollPos);
        return 0;

    case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            RECT rc = { 0, 0, 0, 0 };
            rc.right  = scale(CLIENT_W) + GetSystemMetrics(SM_CXVSCROLL);
            rc.bottom = scale(MARGIN + HEADER_H + ROW_H);
            AdjustWindowRectEx(&rc, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
            mmi->ptMinTrackSize.x = rc.right - rc.left;
            mmi->ptMaxTrackSize.x = rc.right - rc.left;
            mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_REFRESH);
        window   = NULL;
        rowCount = 0;
        DeleteObject(font);
        DeleteObject(boldFont);
        font = boldFont = NULL;
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void mixerDevicesShow(HWND owner)
{
    static const char className[] = "blueMSXMixerDevices";
    static int registered;
    NONCLIENTMETRICSA ncm;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_VSCROLL;
    RECT rc;
    HDC hdc;

    if (window != NULL) {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        return;
    }

    if (!registered) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = windowProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        wc.hIcon         = owner != NULL ? (HICON)GetClassLongPtr(owner, GCLP_HICON) : NULL;
        registered = RegisterClassExA(&wc) != 0;
    }

    hdc = GetDC(NULL);
    dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    font = CreateFontIndirectA(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    boldFont = CreateFontIndirectA(&ncm.lfMessageFont);

    window = CreateWindowExA(0, className, "", style, CW_USEDEFAULT, CW_USEDEFAULT,
                             scale(CLIENT_W), scale(MAX_START_H), owner, NULL,
                             GetModuleHandle(NULL), NULL);
    if (window == NULL) {
        DeleteObject(font);
        DeleteObject(boldFont);
        font = boldFont = NULL;
        return;
    }
    SetWindowTextU(window, langMixerDevicesTitle());

    scrollPos = 0;
    rebuild();

    rc.left   = 0;
    rc.top    = 0;
    rc.right  = scale(CLIENT_W) + GetSystemMetrics(SM_CXVSCROLL);
    rc.bottom = contentHeight < scale(MAX_START_H) ? contentHeight : scale(MAX_START_H);
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    SetWindowPos(window, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

    SetTimer(window, TIMER_REFRESH, REFRESH_MS, NULL);
    ShowWindow(window, SW_SHOW);
}
