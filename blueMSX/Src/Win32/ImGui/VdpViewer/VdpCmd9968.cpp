/*****************************************************************************
**
** V9968 command registers on the VDP Viewer's Command card and Bitmap tab.
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
#include "ImGuiTheme.h"
#include "VdpCmd9968.h"
#include "ToolText.h"

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace {

/* R#21 bit 0 V58 clear: LRMM, the window, VX/VY, XHR and FG4 are in use. */
bool native(const VdpDebugState* s)
{
    return s->version == VDP_V9968 && !(s->regs[21] & 1);
}

int signExtend(int v, int bits)
{
    int m = 1 << (bits - 1);
    return ((v & ((m << 1) - 1)) ^ m) - m;
}

/* What LRMM reads: SX signed 12 bit, SY signed 13 bit, or 12 with XHR, which counts half lines. */
int lrmmSX(const UInt8* c) { return signExtend(c[0] | (c[1] << 8), 12); }
int lrmmSY(const UInt8* c) { return signExtend(c[2] | (c[3] << 8), (c[13] & 0x40) ? 12 : 13); }
int word(const UInt8* c, int i) { return c[i] | (c[i + 1] << 8); }

/* The bitmap mode the command engine works in, or -1: FG4 makes it SCREEN 5, YJK the
** mode R#0 bit 2 picks, SCREEN 8 or 7. */
int engineMode(const VdpDebugState* s)
{
    if (native(s) && (s->cmdRegs[13] & 0x80)) return 5;
    int m = s->screenMode;
    if (m == 10 || m == 12) m = (s->regs[0] & 4) ? 8 : 7;
    return m >= 5 && m <= 8 ? m : -1;
}

/* Cuts a..b down to the box 0..w by 0..h; false when none of it is inside. */
bool clipToBox(ImVec2* a, ImVec2* b, float w, float h)
{
    float t0 = 0, t1 = 1, dx = b->x - a->x, dy = b->y - a->y;
    float p[4] = { -dx, dx, -dy, dy }, q[4] = { a->x, w - a->x, a->y, h - a->y };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return false;
            continue;
        }
        float r = q[i] / p[i];
        if (p[i] < 0) t0 = fmaxf(t0, r);
        else t1 = fminf(t1, r);
        if (t0 > t1) return false;
    }
    *b = ImVec2(a->x + dx * t1, a->y + dy * t1);
    *a = ImVec2(a->x + dx * t0, a->y + dy * t0);
    return true;
}

struct PageMapper {
    const ui::CanvasXf* xf;
    CmdPageMap          g;

    /* Line a..b of command coordinates, in steps that each keep to one page; off the pages, nothing. */
    void line(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, bool dashed) const
    {
        if (!clipToBox(&a, &b, (float)g.tileW, g.pages * 256.0f)) return;
        float len = fmaxf(fabsf(b.x - a.x), fabsf(b.y - a.y));
        int   n = len > 4 ? (int)ceilf(len / 4) : 1;
        for (int i = 0; i < n; i++) {
            if (dashed && (i & 1)) continue;
            ImVec2 p = ImVec2(a.x + (b.x - a.x) * i / n, a.y + (b.y - a.y) * i / n);
            ImVec2 q = ImVec2(a.x + (b.x - a.x) * (i + 1) / n, a.y + (b.y - a.y) * (i + 1) / n);
            float  mid = (p.y + q.y) / 2;
            int page = (int)(mid / 256);
            if (page >= g.pages) page = g.pages - 1;
            int ox = (page % g.cols) * (g.tileW + g.gap), oy = (page / g.cols) * (g.tileH + g.gap);
            float top = page * 256.0f;
            dl->AddLine(xf->toScreen(ox + p.x, oy + fminf(fmaxf(p.y - top, 0), 256)),
                        xf->toScreen(ox + q.x, oy + fminf(fmaxf(q.y - top, 0), 256)), col, th);
        }
    }

    bool at(ImVec2 p, ImVec2* out) const
    {
        if (p.x < 0 || p.y < 0 || p.x > g.tileW) return false;
        int page = (int)(p.y / 256);
        if (page >= g.pages) return false;
        int ox = (page % g.cols) * (g.tileW + g.gap), oy = (page / g.cols) * (g.tileH + g.gap);
        *out = xf->toScreen(ox + p.x, oy + p.y - page * 256.0f);
        return true;
    }

    void tag(ImDrawList* dl, ImVec2 p, ImU32 col, const char* text) const
    {
        ImVec2 at;
        if (!this->at(p, &at)) return;
        const UiTokens& t = uiTokens();
        ui::PushFont(UI_FONT_BODY, 11);
        ImVec2 ts = ImGui::CalcTextSize(text);
        ImVec2 a = ImVec2(at.x + ui::Px(3), at.y + ui::Px(3));
        dl->AddRectFilled(a, ImVec2(a.x + ts.x + ui::Px(8), a.y + ts.y + ui::Px(2)),
                          ui::WithAlpha(t.layer, 200));
        dl->AddRect(a, ImVec2(a.x + ts.x + ui::Px(8), a.y + ts.y + ui::Px(2)), col);
        dl->AddText(ImVec2(a.x + ui::Px(4), a.y + ui::Px(1)), t.text, text);
        ui::PopFont();
    }

    void quad(ImDrawList* dl, const ImVec2* c, ImU32 col, float th, bool dashed) const
    {
        for (int i = 0; i < 4; i++) line(dl, c[i], c[(i + 1) & 3], col, th, dashed);
    }
};

}

void cmdKeyValues(const VdpDebugState* s)
{
    const UInt8* c = s->cmdRegs;
    if (s->version != VDP_V9968) {
        ui::KeyValueMono("SX, SY", "%d, %d", c[0] | ((c[1] & 1) << 8), c[2] | ((c[3] & 3) << 8));
        ui::KeyValueMono("DX, DY", "%d, %d", c[4] | ((c[5] & 1) << 8), c[6] | ((c[7] & 3) << 8));
        ui::KeyValueMono("NX, NY", "%d, %d", c[8] | ((c[9] & 1) << 8), c[10] | ((c[11] & 3) << 8));
        return;
    }
    /* The widths the engine narrows them to as a command starts. */
    int  hi = s->vramA17 && native(s) ? 7 : 3;
    /* R#46 as written, so a finished LRMM reads as the Bitmap tab draws it. */
    bool lrmm = native(s) && (s->regs[46] >> 4) == 3;
    if (lrmm) ui::KeyValueMono("SX, SY", "%d, %d", lrmmSX(c), lrmmSY(c));
    else ui::KeyValueMono("SX, SY", "%d, %d", c[0] | ((c[1] & 1) << 8), c[2] | ((c[3] & hi) << 8));
    ui::KeyValueMono("DX, DY", "%d, %d", c[4] | ((c[5] & 1) << 8), c[6] | ((c[7] & hi) << 8));
    ui::KeyValueMono("NX, NY", "%d, %d", c[8] | ((c[9] & 3) << 8), c[10] | ((c[11] & hi) << 8));
    /* Out of use under V58 but kept, so the card holds still when a program switches. */
    if (!native(s)) {
        ui::KeyValueMono("VX, VY", "\xE2\x80\x94");
        ui::KeyValueMono(ts(TS_KV_CMD_WINDOW), "\xE2\x80\x94");
        return;
    }
    ui::KeyValueMono("VX, VY", "%+.3f, %+.3f", (Int16)word(c, 15) / 256.0, (Int16)word(c, 17) / 256.0);
    ui::KeyValueMono(ts(TS_KV_CMD_WINDOW), "%d-%d, %d-%d", word(c, 19), word(c, 23), word(c, 21), word(c, 25));
}

void cmdLrmmOverlay(ImDrawList* dl, const ui::CanvasXf& xf, const VdpDebugState* s, int readMode,
                    const CmdPageMap& g)
{
    const UInt8* c = s->cmdRegs;
    int rm = readMode == 10 || readMode == 12 ? 8 : readMode;
    /* The command written last, as the CPU wrote it: the engine clears CM once done. */
    if (!native(s) || (s->regs[46] >> 4) != 3 || engineMode(s) != rm || g.pages <= 0) return;
    const UiTokens& t = uiTokens();
    PageMapper m = { &xf, g };
    float th = fmaxf(1.0f, ui::Px(1)), th2 = fmaxf(2.0f, ui::Px(2));
    bool  wide = s->vramA17 != 0;
    int   lines = g.pages * 256;

    /* The read window, unless it takes in every page. */
    int wsx = word(c, 19), wsy = word(c, 21), wex = word(c, 23), wey = word(c, 25);
    if (wsx > 0 || wsy > 0 || wex < g.tileW - 1 || wey < lines - 1) {
        ImVec2 w[4] = { ImVec2((float)wsx, (float)wsy), ImVec2((float)wex + 1, (float)wsy),
                        ImVec2((float)wex + 1, (float)wey + 1), ImVec2((float)wsx, (float)wey + 1) };
        m.quad(dl, w, t.caution, th, false);
        m.tag(dl, w[3], t.caution, ts(TS_CMD_TAG_WINDOW));
    }

    /* Where it writes: DY and NY as written, since the engine counts them down. */
    int nx = word(c, 8) & 0x3ff, dx = word(c, 4) & 0x1ff;
    int dy = (s->regs[38] | (s->regs[39] << 8)) & (wide ? 0x7ff : 0x3ff);
    int ny = (s->regs[42] | (s->regs[43] << 8)) & (wide ? 0x7ff : 0x3ff);
    if (ny == 0) ny = wide ? 2048 : 1024;
    int arg = s->regs[45];
    if (nx > 0) {
        float x0 = (arg & 4) ? dx - nx + 1.0f : (float)dx, y0 = (arg & 8) ? dy - ny + 1.0f : (float)dy;
        ImVec2 d[4] = { ImVec2(x0, y0), ImVec2(x0 + nx, y0), ImVec2(x0 + nx, y0 + ny), ImVec2(x0, y0 + ny) };
        m.quad(dl, d, t.accent, th2, false);
        m.tag(dl, d[0], t.accent, ts(TS_CMD_TAG_DEST));
    }

    /* Where it reads: per dot (VX, VY / step), per row (-VY * step, VX), in 1/256 dots. */
    float step = (arg & 0x40) ? 2.0f : 1.0f;
    float vx = (Int16)word(c, 15) / 256.0f, vy = (Int16)word(c, 17) / 256.0f;
    ImVec2 o = ImVec2((float)lrmmSX(c), (float)lrmmSY(c));
    if (nx > 0) {
        ImVec2 dot = ImVec2(vx * nx, vy / step * nx), row = ImVec2(-vy * step * ny, vx * ny);
        ImVec2 q[4] = { o, ImVec2(o.x + dot.x, o.y + dot.y), ImVec2(o.x + dot.x + row.x, o.y + dot.y + row.y),
                        ImVec2(o.x + row.x, o.y + row.y) };
        m.quad(dl, q, t.accent, th2, true);
    }
    ImVec2 at;
    if (m.at(o, &at)) dl->AddCircleFilled(at, ui::Px(3), t.accent);
    m.tag(dl, o, t.accent, ts(nx > 0 ? TS_CMD_TAG_SOURCE : TS_CMD_TAG_SOURCE_NX0));
}

int cmdEngineMode(const VdpDebugState* s)
{
    return engineMode(s);
}

bool cmdClipToBox(ImVec2* a, ImVec2* b, float w, float h)
{
    return clipToBox(a, b, w, h);
}
