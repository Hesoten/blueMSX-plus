/*****************************************************************************
**
** The Control Deck: run, pause, frame advance, stop, reset and speed in a
** small window that never takes the keyboard from the emulator.
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
#include <math.h>
#include <stdio.h>
#include "imgui.h"
#include "ImGuiWindow.h"
#include "ImGuiTheme.h"
#include "ImGuiWidgets.h"
#include "ControlDeck.h"
#include "ToolText.h"
extern "C" {
#include "../../Win32Common.h"
#include "../../Win32Menu.h"
#include "ArchMenu.h"
#include "Actions.h"
#include "Emulator.h"
#include "Language.h"
#include "Properties.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

/* Segoe Fluent Icons glyphs, as UTF-8. */
#define ICON_PLAY   "\xEE\x9D\xA8"  /* U+E768 Play */
#define ICON_PAUSE  "\xEE\x9D\xA9"  /* U+E769 Pause */
#define ICON_STOP   "\xEE\x9C\x9A"  /* U+E71A Stop */
#define ICON_PREV   "\xEE\xA2\x92"  /* U+E892 Previous */
#define ICON_NEXT   "\xEE\xA2\x93"  /* U+E893 Next */
#define ICON_RESET  "\xEE\x9F\xA8"  /* U+E7E8 PowerButton */
#define ICON_REWIND "\xEE\xAE\x9E"  /* U+EB9E Rewind */
#define ICON_FAST   "\xEE\xAE\x9D"  /* U+EB9D FastForward */

#define DECK_ID     "controldeck"
/* The deck's holder bit for held actions, clear of the shortcut key slots. */
#define DECK_HOLD_BIT (1u << 31)

/* Buttons in px at 96 DPI. */
static const ImVec2 BUTTON_SIZE(32, 28);
static const float  ICON_EM = 16;

static ImGuiToolWindow* deck;
static bool             appExiting;
static bool             holding[2];   /* by HeldAction: the deck holds it */

/* What a control does, or why it cannot now. */
static const char* tipOf(char* buf, size_t size, const char* what, const char* whyNot)
{
    if (whyNot == NULL) {
        return what;
    }
    textf(buf, size, ts(TS_DECK_UNAVAILABLE), what, whyNot);
    return buf;
}

/* Why an action refuses, as the tip of its greyed-out control. */
static const char* refusalText(ActionRefusal why)
{
    switch (why) {
    case ACTION_REFUSED_DIR_GATE:      return ts(TS_DECK_WHILE_FOLDER);
    case ACTION_REFUSED_STOPPED:       return ts(TS_DECK_WHILE_STOPPED);
    case ACTION_REFUSED_REVERSE:       return ts(TS_DECK_WHILE_REVERSE);
    case ACTION_REFUSED_STEPPING_BACK: return ts(TS_DECK_WHILE_STEPPING_BACK);
    case ACTION_REFUSED_REWIND_OFF:    return ts(TS_DECK_WHILE_REVERSE_OFF);
    case ACTION_REFUSED_HOST_MEDIUM:   return ts(TS_DECK_WHILE_HOST_MEDIUM);
    case ACTION_REFUSED_PACED:         return ts(TS_DECK_WHILE_PACED);
    /* A frame step under way: the button stays live, lit, and says it waits. */
    case ACTION_REFUSED_WAITING:
    default:                           return NULL;
    }
}

/* Two frame-advance triangles, the left one in front, then the bar: run on to a new picture;
** back, its mirror image. In 400ths of the em, with the stroke and bar of the icon font's
** Next; the triangles are narrower than its, so the pair is not too wide. */
static void drawStepIcon(ImDrawList* dl, ImVec2 c, float em, ImU32 col, bool back)
{
    /* Sized and placed as the Next or Previous glyph is drawn: its bar runs from 34 to 365. */
    float u = em / 400, oy = c.y - 200 * u;
    ui::PushFont(UI_FONT_ICON, ICON_EM);
    const ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyphNoFallback(back ? 0xE892 : 0xE893);
    if (g != NULL && g->Y1 > g->Y0) {
        u  = (g->Y1 - g->Y0) / 331;
        oy = floorf(c.y - ImGui::GetFontSize() / 2) + g->Y0 - 34 * u;
    }
    ui::PopFont();
    const float th = 25 * u, r = th / 2;
    const float left = 37.5f, tip = 255, top = 66.5f, bottom = 332.5f, mid = 199.5f, shift = 138.5f;
    const float bar = tip + shift + 78.5f;
    float ox = c.x - (left + bar) / 2 * u;
    auto P = [&](float x, float y) { return ImVec2(ox + (back ? left + bar - x : x) * u, oy + y * u); };
    /* The back triangle's left edge shows only outside the front one. */
    float hidden = (mid - top) * (1 - shift / (tip - left)) + th;
    dl->AddLine(P(left + shift, top), P(tip + shift, mid), col, th);
    dl->AddLine(P(tip + shift, mid), P(left + shift, bottom), col, th);
    dl->AddLine(P(left + shift, top), P(left + shift, mid - hidden), col, th);
    dl->AddLine(P(left + shift, mid + hidden), P(left + shift, bottom), col, th);
    dl->AddLine(P(left, top), P(tip, mid), col, th);
    dl->AddLine(P(tip, mid), P(left, bottom), col, th);
    dl->AddLine(P(left, bottom), P(left, top), col, th);
    dl->AddLine(P(bar, 46.5f), P(bar, 352.5f), col, th);
    const ImVec2 round[6] = { P(left, top), P(tip, mid), P(left, bottom), P(tip + shift, mid),
                              P(bar, 46.5f), P(bar, 352.5f) };
    for (const ImVec2& v : round) dl->AddCircleFilled(v, r, col, 8);
}

static void drawChangeIcon(ImDrawList* dl, ImVec2 c, float em, ImU32 col)
{
    drawStepIcon(dl, c, em, col, false);
}

static void drawBackChangeIcon(ImDrawList* dl, ImVec2 c, float em, ImU32 col)
{
    drawStepIcon(dl, c, em, col, true);
}

/* Greyed out with whyNot as its tip when that is not NULL. True when clicked. */
static bool deckButton(const char* id, const char* glyph, const char* fallback, const char* what,
                       const char* whyNot, bool checked = false, ImVec2 size = BUTTON_SIZE,
                       float em = ICON_EM)
{
    char tip[512];
    ImGui::BeginDisabled(whyNot != NULL);
    bool pressed = ui::IconButton(id, glyph, fallback, tipOf(tip, sizeof(tip), what, whyNot), checked,
                                  size, em);
    ImGui::EndDisabled();
    return pressed && whyNot == NULL;
}

/* deckButton with art drawn by draw in place of a glyph. */
static bool deckDrawnButton(const char* id, ui::IconDrawFn draw, const char* what, const char* whyNot,
                            bool checked)
{
    char tip[512];
    ImGui::BeginDisabled(whyNot != NULL);
    bool pressed = ui::DrawnIconButton(id, draw, tipOf(tip, sizeof(tip), what, whyNot), checked,
                                       BUTTON_SIZE, ICON_EM);
    ImGui::EndDisabled();
    return pressed && whyNot == NULL;
}

static void speedButton(int pct, const char* whyNot)
{
    char label[32], what[128], tip[512];
    bool on = propGetGlobalProperties()->emulation.speed == emulatorPercentToLogFrequency(pct);
    textf(label, sizeof(label), "%d%%###speed%d", pct, pct);
    textf(what, sizeof(what), ts(TS_DECK_SPEED_TIP), pct);
    ImGui::BeginDisabled(whyNot != NULL);
    bool pressed = ui::SubtleButton(label, on, ImVec2(0, ui::Px(BUTTON_SIZE.y)));
    ImGui::EndDisabled();
    ui::ItemTooltip(tipOf(tip, sizeof(tip), what, whyNot));
    if (pressed && whyNot == NULL) {
        actionEmuSpeedPercent(pct);
    }
}

static void releaseHold(HeldAction action)
{
    if (holding[action]) {
        holding[action] = false;
        heldActionRelease(action, DECK_HOLD_BIT);
    }
}

/* Holds action while the mouse holds down a press begun here; a mouse-up, capture going
** elsewhere, the button turning disabled or controlDeckDropHolds lets it go until the next
** press, so a button ImGui still thinks is down cannot start it again. */
static void holdButton(const char* id, const char* glyph, const char* fallback, const char* what,
                       const char* whyNot, HeldAction action)
{
    char tip[512];
    ImGui::BeginDisabled(whyNot != NULL);
    ui::IconButton(id, glyph, fallback, tipOf(tip, sizeof(tip), what, whyNot), holding[action],
                   BUTTON_SIZE, ICON_EM);
    bool down = whyNot == NULL && ImGui::IsItemActive() && (holding[action] || ImGui::IsItemActivated());
    ImGui::EndDisabled();
    if (down) {
        /* Every frame, as a held key repeats: max speed can be switched off elsewhere. */
        holding[action] = true;
        heldActionPress(action, DECK_HOLD_BIT);
    }
    else {
        releaseHold(action);
    }
}

/* The state and the frame count, each in the width of its widest text so the
** window does not change size as they change. */
static void readout(bool stopped, bool paused)
{
    const UiTokens& t = uiTokens();
    const char* words[5] = { ts(TS_DECK_RUNNING), ts(TS_STATE_PAUSED), ts(TS_STATE_STOPPED), ts(TS_DECK_NO_CHANGE),
                             ts(TS_DECK_NO_BACK) };
    /* Paused by a step that saw no change in its frames, or by a step back with nothing older. */
    bool noChange = paused && (emulatorChangeStepGaveUp() || emulatorBackStepGaveUp() == 2);
    bool noBack = paused && emulatorBackStepGaveUp() == 1;
    int shown = stopped ? 2 : noChange ? 3 : noBack ? 4 : paused ? 1 : 0;
    ImU32 dotCol = stopped ? t.textSecondary : paused ? t.caution : t.success;
    float rowH = ui::Px(BUTTON_SIZE.y), d = ui::Px(8), gap = ui::Px(6), group = ui::Px(12);
    float wordW = 0;
    for (int i = 0; i < 5; i++) {
        wordW = fmaxf(wordW, ImGui::CalcTextSize(words[i]).x);
    }
    char frame[64], widest[64];
    textf(frame, sizeof(frame), ts(TS_DECK_FRAME_N), (unsigned)emulatorGetMachineFrame());
    textf(widest, sizeof(widest), ts(TS_DECK_FRAME_N), 8888888u);
    ui::PushFont(UI_FONT_MONO);
    float frameW = fmaxf(ImGui::CalcTextSize(widest).x, ImGui::CalcTextSize(frame).x);
    ui::PopFont();

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(d + gap + wordW + group + frameW, rowH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = floorf(p.y + rowH / 2);
    dl->AddCircleFilled(ImVec2(p.x + d / 2, cy), d / 2, ImGui::GetColorU32(dotCol));
    float th = ImGui::GetTextLineHeight();
    dl->AddText(ImVec2(p.x + d + gap, floorf(cy - th / 2)), ImGui::GetColorU32(t.text), words[shown]);
    ui::PushFont(UI_FONT_MONO);
    th = ImGui::GetTextLineHeight();
    dl->AddText(ImVec2(p.x + d + gap + wordW + group, floorf(cy - th / 2)), ImGui::GetColorU32(t.textSecondary), frame);
    ui::PopFont();
}

/* One row, sized by what it holds: nothing here may take its width from the window. */
static void draw(ImGuiToolWindow*, void*)
{
    EmuState state = emulatorGetState();
    bool stopped = state == EMU_STOPPED;
    bool paused = state == EMU_PAUSED;
    bool changing = emulatorChangeStepArmed() != 0;
    bool waiting = emulatorFrameStopPending() && !changing;
    int backing = emulatorBackStepPending();
    HWND mainWnd = getMainHwnd();
    /* A modal dialog disables the main window; the deck must not act behind it. */
    const char* modal = mainWnd && !IsWindowEnabled(mainWnd) ? ts(TS_DECK_WHILE_DIALOG) : NULL;
    const char* gate = modal ? modal : actionDirGateOpen() ? ts(TS_DECK_WHILE_FOLDER) : NULL;
    float gap = ui::Px(2), group = ui::Px(12);

    bool running = !stopped && !paused;
    char none[128], backNone[256];
    if (deckButton("##run", running ? ICON_PAUSE : ICON_PLAY, running ? "||" : ">",
                   ts(running ? TS_DECK_PAUSE_TIP : TS_DECK_RUN_TIP), gate)) {
        menuPostRunCommand(MENU_RUN_TOGGLE_PAUSE);
    }
    ImGui::SameLine(0, gap);

    const char* frameNot = modal ? modal : refusalText(actionFrameAdvanceRefusal());
    const char* changeNot = modal ? modal : refusalText(actionAdvanceToChangeRefusal());
    const char* backNot = modal ? modal : refusalText(actionBackStepRefusal());
    const char* holdNot = modal ? modal : refusalText(actionHoldRefusal(0));
    const char* rewindNot = modal ? modal : refusalText(actionHoldRefusal(1));
    const char* backTip = ts(TS_DECK_BACK_TIP);
    const char* backChangeTip = ts(TS_DECK_BACK_CHANGE_TIP);
    if (backing == 2) {
        backChangeTip = ts(TS_DECK_BACK_CHANGE_WAITING_TIP);
    }
    else if (paused && emulatorBackStepGaveUp() == 2) {
        textf(backNone, sizeof(backNone), ts(TS_DECK_CHANGE_NONE), emulatorChangeStepLimit());
        backChangeTip = backNone;
    }
    else if (paused && emulatorBackStepGaveUp() == 1) {
        backTip = backChangeTip = ts(TS_DECK_BACK_GAVE_UP);
    }

    /* Each step back beside the step forward it mirrors. */
    if (deckButton("##backFrame", ICON_PREV, "|<", backTip, backNot, backing == 1)) {
        actionEmuBackFrame();
    }
    ImGui::SameLine(0, gap);
    if (deckButton("##frame", ICON_NEXT, ">|", ts(waiting ? TS_DECK_FRAME_WAITING_TIP : TS_DECK_FRAME_TIP),
                   frameNot, waiting)) {
        actionEmuFrameAdvance();
    }
    ImGui::SameLine(0, gap);

    if (deckDrawnButton("##backChange", drawBackChangeIcon, backChangeTip, backNot, backing == 2)) {
        actionEmuBackToChange();
    }
    ImGui::SameLine(0, gap);

    const char* changeTip = ts(TS_DECK_CHANGE_TIP);
    if (changing) {
        changeTip = ts(TS_DECK_CHANGE_WAITING_TIP);
    }
    else if (paused && emulatorChangeStepGaveUp()) {
        textf(none, sizeof(none), ts(TS_DECK_CHANGE_NONE), emulatorChangeStepLimit());
        changeTip = none;
    }
    if (deckDrawnButton("##change", drawChangeIcon, changeTip, changeNot, changing)) {
        actionEmuAdvanceToChange();
    }
    ImGui::SameLine(0, gap);

    if (deckButton("##stop", ICON_STOP, "[]", ts(TS_DECK_STOP_TIP),
                   modal ? modal : stopped ? ts(TS_DECK_WHILE_STOPPED) : NULL)) {
        menuPostRunCommand(MENU_RUN_STOP);
    }

    ImGui::SameLine(0, group);
    holdButton("##rewind", ICON_REWIND, "<<", ts(TS_DECK_REWIND_TIP), rewindNot, HELD_ACTION_REVERSE);
    ImGui::SameLine(0, gap);
    holdButton("##fast", ICON_FAST, ">>", ts(TS_DECK_FAST_TIP), holdNot, HELD_ACTION_MAX_SPEED);

    static const int speeds[] = { 100, 25, 10 };
    for (int i = 0; i < 3; i++) {
        ImGui::SameLine(0, i == 0 ? group : gap);
        speedButton(speeds[i], modal);
    }

    ImGui::SameLine(0, group);
    readout(stopped, paused);

    /* Last, apart from the rest: the readout keeps a fixed width, so this stays put. */
    ImGui::SameLine(0, group);
    if (deckButton("##reset", ICON_RESET, "R", ts(TS_DECK_RESET_TIP), modal)) {
        menuPostRunCommand(MENU_RUN_RESET_HARD);
    }
    ImGui::SameLine(0, 0);
    if (deckButton("##resetMore", UI_ICON_CHEVRON_DOWN, "v", ts(TS_DECK_RESET_MORE_TIP), modal, false,
                   ImVec2(16, BUTTON_SIZE.y), 10)) {
        /* Under the button, in screen pixels: ImGui draws in client pixels. */
        POINT pt = { (LONG)ImGui::GetItemRectMin().x, (LONG)ImGui::GetItemRectMax().y };
        ClientToScreen((HWND)ImGui::GetMainViewport()->PlatformHandleRaw, &pt);
        imguiToolHideTip();
        menuShowResetChoices(pt.x, pt.y);
    }
}

static void closed(void*)
{
    /* Gone with a lost device, not by the user's choice: it opens again at the next start. */
    bool lost = deck != NULL && imguiToolLostDevice(deck);
    deck = NULL;
    controlDeckDropHolds();
    if (appExiting) {
        return;
    }
    if (!lost) {
        propGetGlobalProperties()->controlDeck.open = 0;
    }
    archUpdateMenu(0);
}

extern "C" void controlDeckShow(void)
{
    ImGuiToolDesc desc = {};
    desc.id        = DECK_ID;
    desc.title     = langMenuToolsControlDeck();
    desc.width     = 520;
    desc.height    = 44;
    desc.flags     = IMGUI_TOOL_NO_ACTIVATE | IMGUI_TOOL_FIT_CONTENT | IMGUI_TOOL_TIPS_OUTSIDE;
    desc.draw      = draw;
    desc.closed    = closed;
    desc.placement = &propGetGlobalProperties()->controlDeck.window;
    ImGuiToolWindow* w = imguiToolOpen(&desc);
    if (w != NULL) {
        deck = w;
        propGetGlobalProperties()->controlDeck.open = 1;
        archUpdateMenu(0);
    }
}

extern "C" void controlDeckClose(void)
{
    if (deck != NULL) {
        imguiToolClose(deck);
    }
}

extern "C" int controlDeckIsOpen(void)
{
    return imguiToolIsOpen(DECK_ID);
}

extern "C" void controlDeckDropHolds(void)
{
    releaseHold(HELD_ACTION_MAX_SPEED);
    releaseHold(HELD_ACTION_REVERSE);
}

extern "C" void controlDeckAppExit(void)
{
    appExiting = true;
}
