/*****************************************************************************
**
** Sprites tab and sprite cards of the VDP Viewer in V9968 sprite mode 3.
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
#include <string.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiTheme.h"
#include "VdpSprite3View.h"
#include "VdpParts.h"
#include "ToolText.h"

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

namespace {

/* A plane drawn on at least one displayed line, not dropped on all of them. */
bool shows(const Sp3Plane& p)
{
    return (p.state == SP3_ST_DRAWN || p.state == SP3_ST_DROPPED) && p.drawnLines > 0;
}

void boxEdge(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, bool dashed)
{
    if (dashed) parts::dashedSegment(dl, a, b, col, th, ui::Px(4));
    else dl->AddLine(a, b, col, th);
}

/* The count band left of the placement. */
float placeBandW()
{
    return ui::Px(44);
}

/* The line labels' column left of the magnified sprite. */
float magLabelW()
{
    return ui::Px(30);
}

/* A pattern page's label line. */
float pageLabelH()
{
    return ImGui::GetTextLineHeight() + ui::Px(2);
}

/* The line row under the attribute tiles: caption, the 16 drawn, the fence, the dropped. */
float lineRowH(int N)
{
    float lh = ImGui::GetTextLineHeight(), rowH = 16.0f * N + lh + ui::Px(2);
    return ui::Px(4) + lh + ui::Px(4) + rowH + lh + ui::Px(2) + rowH;
}

/* Screen px a dot of the magnifier for a plane of rows pattern rows at zoom N. */
float magScale(int rows, int N)
{
    int m = 128 * N / rows;
    return (float)(m > 2 ? m : 2);
}

}

Sp3Panel::Sp3Panel()
{
    cv.shared = &shell::zoom();
    cv.frameless = true;
    memset(regs, 0, sizeof(regs));
    memset(order, 0, sizeof(order));
    memset(atlasAny, 0, sizeof(atlasAny));
    memset(fitKey, 0, sizeof(fitKey));
    memset(scrRow, 0, sizeof(scrRow));
    memset(scrOrg, 0, sizeof(scrOrg));
    memset(scrState, 0, sizeof(scrState));
    memset(scrDrop, 0, sizeof(scrDrop));
}

void Sp3Panel::track(const VdpDebugState* s, Focus& f)
{
    if (!sp3Active(s)) {
        pin = -1;
        return;
    }
    if (f.pinned && f.kind == FOCUS_SPRITE) {
        f.pinned = false;
        f.kind = FOCUS_NONE;
    }
    if (f.pinned && f.kind != FOCUS_NONE) pin = -1;
    /* Its attribute bytes stay lit on the VRAM map, as a mode 1/2 pin's do; a hover replaces them. */
    if (pin >= 0 && !f.pinned) {
        f.space = FOCUS_PHYS;
        f.addr = (UInt32)frame(s).p[pin].attr;
        f.len = 8;
        f.kind = FOCUS_VRAM;
        f.a = f.b = 0;
    }
}

void Sp3Panel::escape()
{
    lineFixed = -1;
}

void Sp3Panel::forget()
{
    placePic.forget();
    sheetPic.forget();
    atlasPic.forget();
    ver = ~0u;
    pin = hot = hotNext = -1;
    lineFixed = rowPin = -1;
    fitN = 0;
}

const Sp3Frame& Sp3Panel::frame(const VdpDebugState* s, bool scanAll)
{
    if (!fr) fr.reset(new Sp3Frame());
    /* Under SPS the scan order moves every frame while VRAM may not. */
    const char* text = ts(TS_SPR_OFF_BLANKED);
    if (ver == shell::pictureVersion() && memcmp(regs, s->regs, sizeof(regs)) == 0 && mode == s->screenMode &&
        screenOn == s->screenOn && lang == text && memcmp(order, s->sprPlaneOrder, sizeof(order)) == 0 &&
        (frScanAll == scanAll || fr->off == NULL)) return *fr;
    ver = shell::pictureVersion();
    memcpy(regs, s->regs, sizeof(regs));
    memcpy(order, s->sprPlaneOrder, sizeof(order));
    mode = s->screenMode;
    screenOn = s->screenOn;
    lang = text;
    frScanAll = scanAll;
    built++;
    sp3Eval(s, fr.get(), scanAll);
    if (pin >= 0 && !fr->on) pin = -1;
    return *fr;
}

void Sp3Panel::pinPlane(int n, Focus& f)
{
    pin = pin == n ? -1 : n;
    /* The card shows one pin: this one takes it from the view's. */
    if (pin >= 0 && f.pinned) {
        f.pinned = false;
        f.kind = FOCUS_NONE;
    }
}

/* The pages the shown planes' patterns cover, a tall one running on into the next. */
void Sp3Panel::choosePages()
{
    const Sp3Frame& f = *fr;
    bool used[9] = {};
    for (int n = 0; n < SP3_PLANES; n++) {
        const Sp3Plane& p = f.p[n];
        if (!shows(p) && n != pin) continue;
        int r0 = (p.pattern >> 4) * 16;
        used[p.page] = true;
        used[p.page + ((r0 + p.rows - 1) >> 8)] = true;
    }
    pageCount = 0;
    for (int q = 0; q < 9; q++) {
        if (used[q]) pages[pageCount++] = q;
    }
    sheetPs = pin >= 0 ? f.p[pin].ps : 0;
}

int Sp3Panel::sheetY(int i) const
{
    return i * 256;
}

/* The plane whose pattern box holds sheet pixel x, y, and its pattern row; -1 for none. */
int Sp3Panel::sheetHit(int x, int y, int* row) const
{
    const Sp3Frame& f = *fr;
    if (x < 0 || y < sheetY(0) || pageCount == 0) return -1;
    int i = (y - sheetY(0)) / 256;
    if (i >= pageCount) return -1;
    int q = pages[i], rowIn = y - sheetY(i);
    for (int pass = 0; pass < 2; pass++) {
        /* The pinned plane first, as it is drawn on top. */
        for (int step = 0; step < SP3_PLANES; step++) {
            int n = pass == 0 ? pin : f.order[step];
            if (n < 0) break;
            const Sp3Plane& p = f.p[n];
            if (!shows(p) && n != pin) continue;
            int r = (q - p.page) * 256 + rowIn - (p.pattern >> 4) * 16;
            int x0 = (p.pattern & 15) * 16;
            if (r >= 0 && r < p.rows && x >= x0 && x < x0 + 16) {
                *row = r;
                return n;
            }
            if (pass == 0) break;
        }
    }
    return -1;
}

/* The first plane of the scan whose box covers X x of displayed line d, dropped or not; only that one when only >= 0. */
int Sp3Panel::boxAt(int x, int d, int only) const
{
    const Sp3Frame& f = *fr;
    if (d < 0 || d >= f.lines) return -1;
    for (int k = 0; k < f.count[d]; k++) {
        if (only >= 0 && f.id[d][k] != only) continue;
        const Sp3Plane& p = f.p[f.id[d][k]];
        int x0[2], x1[2], n = sp3XRuns(p, x0, x1);
        for (int i = 0; i < n; i++) {
            if (x >= x0[i] && x < x1[i]) return f.id[d][k];
        }
    }
    return -1;
}

void Sp3Panel::stateText(int n, char* out, size_t size, bool lead) const
{
    const Sp3Frame& f = *fr;
    const Sp3Plane& p = f.p[n];
    char what[160];
    /* With the sprites off, why they are off, as the tab's note says. */
    if (f.off != NULL && p.state != SP3_ST_AFTER_STOP) textf(what, sizeof(what), ts(TS_SPR_ST_OFF), f.off);
    else switch (p.state) {
    case SP3_ST_AFTER_STOP:
        if (p.step == f.stopStep) textf(what, sizeof(what), ts(TS_SPR_TERMINATOR), p.y);
        else textf(what, sizeof(what), "%s", ts(TS_SPR_AFTER_TERMINATOR));
        break;
    case SP3_ST_OFF_SCREEN: textf(what, sizeof(what), "%s", ts(TS_SPR_OFF_SCREEN)); break;
    case SP3_ST_NO_WIDTH:   textf(what, sizeof(what), "%s", ts(TS_SP3_ST_NO_WIDTH)); break;
    case SP3_ST_DROPPED:    textf(what, sizeof(what), ts(TS_SP3_ST_DROPPED), p.droppedLines); break;
    default:                textf(what, sizeof(what), "%s", ts(TS_SPR_SHOWN)); break;
    }
    /* Not lead: what it is and whose attributes it reads, for a row of the selection. */
    size_t at = lead ? (size_t)textf(out, size, ts(TS_SP3_OV_TIP), n, what) : (size_t)textf(out, size, "%s", what);
    if (p.alias != n && at < size) at += (size_t)textf(out + at, size - at, ts(TS_SP3_ST_ALIAS), p.alias);
    if (lead && f.shuffle && at < size) textf(out + at, size - at, ts(TS_SP3_ST_STEP), p.step);
}

void Sp3Panel::drawBoxes(ImDrawList* dl, const ui::CanvasXf& xf) const
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& f = *fr;
    float th = fmaxf(1.0f, ui::Px(1));
    /* Back to front: the plane the scan reaches first wins a dot, so its box goes on top;
    ** then the hovered and the pinned one over all. */
    for (int pass = 0; pass < 3; pass++) {
        for (int step = SP3_PLANES - 1; step >= 0; step--) {
            int n = pass == 0 ? f.order[step] : pass == 1 ? hot : pin;
            if (n < 0) break;
            const Sp3Plane& p = f.p[n];
            bool mine = n == hot || n == pin;
            if ((pass == 0 && (mine || !boxes)) || p.step >= f.stopStep || p.firstLine < 0 || p.mgx == 0) {
                if (pass > 0) break;
                continue;
            }
            ImU32 col = n == pin ? t.accent : n == hot ? t.accentHover : p.droppedLines > 0 ? t.critical : ui::WithAlpha(t.text, 170);
            float w = n == pin ? th * 2 : th;
            int x0[2], x1[2], nx = sp3XRuns(p, x0, x1);
            for (int d = p.firstLine; d <= p.lastLine;) {
                if (sp3SourceRow(f, n, d) < 0) {
                    d++;
                    continue;
                }
                int e = d;
                while (e + 1 <= p.lastLine && sp3SourceRow(f, n, e + 1) >= 0) e++;
                for (int i = 0; i < nx; i++) {
                    ImVec2 a = xf.toScreen((float)x0[i], (float)d), b = xf.toScreen((float)x1[i], (float)(e + 1));
                    if (p.alias != n) parts::dashedRect(dl, a, b, col, w);
                    else dl->AddRect(a, b, col, 0, w);
                }
                d = e + 1;
            }
            if (pass > 0) break;
        }
    }
}

void Sp3Panel::drawPatternBoxes(ImDrawList* dl, const ui::CanvasXf& xf, int page) const
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& f = *fr;
    float th = fmaxf(1.0f, ui::Px(1));
    int labelled[SP3_PLANES], nl = 0, nc = 0;
    ImRect chips[SP3_PLANES];
    ui::PushFont(UI_FONT_BODY, 10);
    for (int pass = 0; pass < 2; pass++) {
        for (int step = 0; step < SP3_PLANES; step++) {
            int n = pass == 0 ? f.order[step] : pin;
            if (n < 0) break;
            const Sp3Plane& p = f.p[n];
            if ((pass == 0 && n == pin) || (!shows(p) && n != pin)) {
                if (pass > 0) break;
                continue;
            }
            ImU32 col = n == pin ? t.accent : n == hot ? t.accentHover : ui::WithAlpha(t.text, 170);
            int r0 = (p.pattern >> 4) * 16, x0 = (p.pattern & 15) * 16;
            int key = p.page << 16 | p.pattern << 8 | p.sizeCode, k = 0;
            while (k < nl && labelled[k] != key) k++;
            bool label = k == nl || n == pin;
            if (k == nl) labelled[nl++] = key;
            /* A tall pattern goes on past its page into the next, shown under it if used. */
            for (int i = 0; i < pageCount; i++) {
                if (page >= 0 && i != page) continue;
                int top = (pages[i] - p.page) * 256;
                int a0 = r0 - top, a1 = r0 + p.rows - top;
                if (a1 <= 0 || a0 >= 256) continue;
                a0 = a0 < 0 ? 0 : a0;
                a1 = a1 > 256 ? 256 : a1;
                ImVec2 a = xf.toScreen((float)x0, (float)(sheetY(i) + a0)), b = xf.toScreen((float)(x0 + 16), (float)(sheetY(i) + a1));
                dl->AddRect(a, b, col, 0, n == pin ? th * 2 : th);
                if (!label || a0 != r0 - top) continue;
                char num[8];
                snprintf(num, sizeof(num), "%d", n);
                ImVec2 sz = ImGui::CalcTextSize(num);
                /* Only where it fits and no other number is there already. */
                ImRect chip(ImVec2(a.x + th, a.y + th), ImVec2(a.x + sz.x + ui::Px(4), a.y + sz.y + ui::Px(1)));
                bool free = b.x - a.x >= sz.x + ui::Px(4) && b.y - a.y >= sz.y + ui::Px(2);
                for (int c = 0; free && n != pin && c < nc; c++) free = !chips[c].Overlaps(chip);
                if (free) {
                    if (nc < SP3_PLANES) chips[nc++] = chip;
                    dl->AddRectFilled(chip.Min, chip.Max, ui::WithAlpha(t.layer, 200));
                    dl->AddText(ImVec2(a.x + ui::Px(2), a.y + th), n == pin ? t.accent : t.text, num);
                }
            }
            if (pass > 0) break;
        }
    }
    ui::PopFont();
}

/* Sprites in range on each line: "n/16" where a line is tall enough, else a bar, red past 16. */
void Sp3Panel::drawGutter(ImDrawList* dl, const ui::CanvasXf& xf) const
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& f = *fr;
    float gx0 = xf.gutterMin.x + ui::Px(2), gw = xf.gutterMax.x - xf.gutterMin.x - ui::Px(6);
    float th = fmaxf(1.0f, ui::Px(1));
    dl->AddRectFilled(xf.gutterMin, xf.gutterMax, t.controlFillPressed);
    ui::PushFont(UI_FONT_BODY, 10);
    float fh = ImGui::GetFontSize();
    bool text = xf.scale.y >= fh;
    for (int d = 0; d < f.lines; d++) {
        int c = f.count[d];
        ImVec2 a = xf.toScreen(0, (float)d), b = xf.toScreen(0, (float)(d + 1));
        if (c == 0 || b.y < xf.gutterMin.y || a.y > xf.gutterMax.y) continue;
        ImU32 col = c > SP3_LIMIT ? t.critical : t.textSecondary;
        if (text) {
            char s[16];
            snprintf(s, sizeof(s), "%d/%d", c, SP3_LIMIT);
            ImVec2 sz = ImGui::CalcTextSize(s);
            dl->AddText(ImVec2(floorf(gx0 + gw - sz.x), floorf((a.y + b.y - sz.y) / 2)), col, s);
        }
        else {
            float len = (float)(c < 2 * SP3_LIMIT ? c : 2 * SP3_LIMIT) * gw / (2 * SP3_LIMIT);
            dl->AddRectFilled(ImVec2(gx0, a.y), ImVec2(gx0 + len, fmaxf(b.y, a.y + 1)), col);
        }
    }
    if (!text) {
        float tx = floorf(gx0 + gw / 2);
        ImVec2 e = xf.toScreen(0, (float)f.lines);
        dl->AddLine(ImVec2(tx, xf.toScreen(0, 0).y), ImVec2(tx, e.y), t.textTertiary, th);
    }
    ui::PopFont();
}

/* Each plane's pattern in a 16 x 128 cell, 16 planes a row. It reads VRAM and the palette
** alone; what the frame did with each plane goes over it as marks. */
void Sp3Panel::ensureAtlas(const VdpDebugState* s)
{
    const Sp3Frame& fm = *fr;
    unsigned pal = fnvWords(s->paletteExt, 256);
    char key[64];
    snprintf(key, sizeof(key), "%X/%X/%X/%X/%08X", fm.attrBase, fm.patBase, fm.aliasMask, (unsigned)s->vramMask, pal);
    bool decoded = atlasPic.needsDecodeVram(key);
    if (decoded) {
        atlasPic.w = 16 * 16;
        atlasPic.h = 4 * 128;
        atlasPic.img.assign((size_t)atlasPic.w * atlasPic.h, 0);
        for (int n = 0; n < SP3_PLANES; n++) {
            atlasAny[n] = sp3RenderTile(s, fm, n, &atlasPic.img[(size_t)(n >> 4) * 128 * atlasPic.w + (n & 15) * 16], atlasPic.w);
        }
    }
    atlasPic.upload(decoded);
}

/* Plane n's tile from the atlas at a, px screen px a dot, over a checker. */
void Sp3Panel::atlasImage(ImDrawList* dl, int n, ImVec2 a, float px) const
{
    int rows = fr->p[n].rows;
    ImVec2 b(a.x + 16 * px, a.y + rows * px);
    parts::drawCheckerRect(dl, a, b);
    if (!atlasPic.tex.id()) return;
    float u = (float)((n & 15) * 16) / 256, v = (float)((n >> 4) * 128) / 512;
    ui::ImageNearest(dl, atlasPic.tex.id(), a, b, ImVec2(u, v), ImVec2(u + 16.0f / 256, v + rows / 512.0f));
}

/* The 16 colours of palette set ps from a, colour 0 clear. */
void Sp3Panel::swatchRow(ImDrawList* dl, const VdpDebugState* s, int ps, ImVec2 a) const
{
    const UiTokens& t = uiTokens();
    float sw = ui::Px(12);
    for (int c = 0; c < 16; c++) {
        ImVec2 c0(a.x + c * (sw + ui::Px(2)), a.y), c1(c0.x + sw, c0.y + sw);
        if (c == 0) parts::drawCheckerRect(dl, c0, c1);
        else dl->AddRectFilled(c0, c1, (ImU32)vdpRgbToRgba(s->paletteExt[((ps & 15) << 4) | c]));
        dl->AddRect(c0, c1, t.controlStroke);
    }
}

/* The rows of the tallest plane in row r of the attribute table, 0-3. */
int Sp3Panel::attrRows(int r) const
{
    int h = 16;
    for (int n = r * 16; n < r * 16 + 16; n++) h = fr->p[n].rows > h ? fr->p[n].rows : h;
    return h;
}

int Sp3Panel::tallest() const
{
    int h = 16;
    for (int r = 0; r < 4; r++) h = attrRows(r) > h ? attrRows(r) : h;
    return h;
}

/* The tables, then the notes on one line, the rest of them on hover. */
void Sp3Panel::header(const VdpDebugState* s)
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = *fr;
    char head[256];
    size_t at = (size_t)textf(head, sizeof(head), ts(TS_SP3_HEAD), SP3_LIMIT, fm.attrBase & s->vramMask, fm.patBase & s->vramMask);
    if (fm.shuffle && at < sizeof(head)) textf(head + at, sizeof(head) - at, "%s", ts(TS_SP3_HEAD_SPS));
    ui::TextFitItem(head);
    char notes[4][384];
    int n = 0;
    if (fm.off != NULL) textf(notes[n++], sizeof(notes[0]), ts(TS_SPR_NONE_SHOWN), fm.off);
    if (!vdpGetSpritesEnable()) textf(notes[n++], sizeof(notes[0]), "%s", ts(TS_SPR_NOTE_HIDDEN));
    if (vdpGetNoSpritesLimit()) textf(notes[n++], sizeof(notes[0]), "%s", ts(TS_SPR_TAB_NO_LIMIT));
    /* One line, kept even when empty so nothing below moves. */
    char line[512] = " ", all[2048] = "";
    int k = n > 0 ? textf(line, sizeof(line), "%s", notes[0]) : 0;
    if (n > 1 && k > 0 && k < (int)sizeof(line)) textf(line + k, sizeof(line) - k, ts(TS_SPR_TAB_MORE_NOTES), n - 1);
    size_t al = 0;
    for (int i = 0; i < n && al < sizeof(all); i++) al += (size_t)textf(all + al, sizeof(all) - al, "%s%s", i ? "\n" : "", notes[i]);
    ui::TextFitItem(line, t.caution, n == 1);
    if (n > 1 && ImGui::IsItemHovered()) parts::steadyTooltip(notesTip, ImGui::GetID("##sp3notes"), all);
}

/* A part's size at zoom N, its title included. */
ImVec2 Sp3Panel::partSize(int part, int N) const
{
    float lh = ImGui::GetTextLineHeight(), title = lh + ui::Px(6), px = (float)N;
    if (part == PART_PLACE) return ImVec2(placeBandW() + 256 * px, title + fr->lines * px);
    if (part == PART_ATTRS) {
        float h = title;
        for (int r = 0; r < 4; r++) h += lh + attrRows(r) * px + ui::Px(6);
        return ImVec2(16 * (16 * px + ui::Px(4)) - ui::Px(4), h + lineRowH(N));
    }
    if (part == PART_MAG) {
        float gh = 0;
        for (int rows = 16; rows <= 128; rows *= 2) gh = fmaxf(gh, rows * magScale(rows, N));
        return ImVec2(fmaxf(magLabelW() + 16 * magScale(16, N), 16 * ui::Px(14)), title + gh + ui::Px(8) + ui::Px(12));
    }
    return ImVec2(256 * px, lh * 2 + ui::Px(6) + pageCount * (pageLabelH() + 256 * px));
}

/* The parts at zoom N in the widest tier that fits W: three columns, two, or one. */
Sp3Panel::Layout Sp3Panel::layoutAt(float W, int N) const
{
    /* Columns of parts side by side; the patterns go under the placement, the short column. */
    static const int tiers[3][3][4] = {
        { { PART_PLACE, PART_PATS, -1 }, { PART_ATTRS, -1 }, { PART_MAG, -1 } },
        { { PART_PLACE, PART_MAG, PART_PATS, -1 }, { PART_ATTRS, -1 }, { -1 } },
        { { PART_PLACE, PART_ATTRS, PART_MAG, PART_PATS }, { -1 }, { -1 } },
    };
    Layout L = {};
    float gap = ui::Px(12);
    for (int p = 0; p < PART_COUNT; p++) {
        L.size[p] = partSize(p, N);
        L.scale[p] = (float)N;
    }
    int magN = hot >= 0 ? hot : pin;
    L.scale[PART_MAG] = magScale(magN >= 0 ? fr->p[magN].rows : 16, N);
    float wide = fmaxf(L.size[PART_PLACE].x, L.size[PART_PATS].x);
    L.tier = wide + gap + L.size[PART_ATTRS].x + gap + L.size[PART_MAG].x <= W ? 0 :
             fmaxf(wide, L.size[PART_MAG].x) + gap + L.size[PART_ATTRS].x <= W ? 1 : 2;
    float x = 0;
    for (int i = 0; i < 3 && tiers[L.tier][i][0] >= 0; i++) {
        float y = 0, w = 0;
        for (int j = 0; j < 4 && tiers[L.tier][i][j] >= 0; j++) {
            int p = tiers[L.tier][i][j];
            L.at[p] = ImVec2(x, y);
            y += L.size[p].y + gap;
            w = fmaxf(w, L.size[p].x);
        }
        L.height = fmaxf(L.height, y - gap);
        x += w + gap;
    }
    L.width = x - gap;
    /* What Fit fits: every other part and the patterns' first row of tiles. */
    for (int p = 0; p < PART_COUNT; p++) {
        float bottom = p == PART_PATS ? L.at[p].y + ImGui::GetTextLineHeight() * 2 + ui::Px(6) + pageLabelH() + 16.0f * N : L.at[p].y + L.size[p].y;
        L.fitH = fmaxf(L.fitH, bottom);
    }
    return L;
}

/* Fit: the largest whole zoom whose layout fits W and H, else the largest that fits W;
** looked for again only when what the layouts depend on changed. */
int Sp3Panel::fitZoom(float W, float H)
{
    float key[8] = { W, H, (float)fr->lines, (float)attrRows(0), (float)attrRows(1), (float)(attrRows(2) * 256 + attrRows(3)),
                     (float)pageCount, ImGui::GetTextLineHeight() * 1000 + ui::Px(100) };
    if (fitN > 0 && memcmp(key, fitKey, sizeof(key)) == 0) return fitN;
    memcpy(fitKey, key, sizeof(key));
    int n = 0;
    for (int pass = 0; pass < 2 && n == 0; pass++) {
        for (int N = 32; N > 1 && n == 0; N--) {
            Layout L = layoutAt(W, N);
            if (L.width <= W && (pass == 1 || L.fitH + ui::Px(8) <= H)) n = N;
        }
    }
    fitN = n > 0 ? n : 1;
    return fitN;
}

void Sp3Panel::spriteStatus(int n, bool droppedHere) const
{
    const Sp3Plane& p = fr->p[n];
    char text[512];
    textf(text, sizeof(text), ts(TS_SP3_STATUS_SPRITE), n, p.y, p.x, p.page, p.pattern, p.mgx, p.height, p.attr,
          droppedHere ? ts(TS_STATUS_SPRITE_DROPPED) : "");
    ui::StatusText("%s", text);
}

/* The count band and the placement at N px a dot; a click beside the sprites fixes the line row's line. */
void Sp3Panel::drawPlace(const VdpDebugState* s, Focus& f, ImVec2 at, int N)
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = *fr;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight(), px = (float)N;
    ui::PushFont(UI_FONT_STRONG);
    dl->AddText(at, t.text, ts(TS_SPR_TAB_PLACE));
    float tw = ImGui::CalcTextSize(ts(TS_SPR_TAB_PLACE)).x;
    ui::PopFont();
    dl->AddText(ImVec2(at.x + tw + ui::Px(8), at.y), t.textSecondary, ts(TS_SPR_TAB_PLACE_SUB));
    int lines = fm.lines;
    float y0 = floorf(at.y + lh + ui::Px(6));
    ImVec2 img(at.x + placeBandW(), y0), end(img.x + 256 * px, y0 + lines * px);
    char key[64];
    snprintf(key, sizeof(key), "%d/%d/%08X", lines, dropped, t.critical);
    bool decoded = placePic.needsDecodeAt(built, key);
    if (decoded) {
        placePic.w = 256;
        placePic.h = lines;
        placePic.img.assign((size_t)256 * lines, 0);
        sp3RenderPlacement(s, fm, dropped ? (t.critical | IM_COL32_A_MASK) : 0, placePic.img.data());
    }
    placePic.upload(decoded);

    ImGui::SetCursorScreenPos(ImVec2(at.x, y0));
    ImGui::InvisibleButton("##sp3place", ImVec2(end.x - at.x, fmaxf(1.0f, lines * px)));
    bool hovered = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImVec2 m = ImGui::GetIO().MousePos;
    int d = (int)floorf((m.y - y0) / px), dot = (int)floorf((m.x - img.x) / px);
    if (hovered && d >= 0 && d < lines) {
        int n = -1;
        placeLine = d;
        if (m.x < img.x) {
            int c = fm.count[d];
            char drop[256] = "", tail[300] = "";
            size_t k = 0;
            for (int i = SP3_LIMIT; i < c && k < sizeof(drop); i++) {
                k += (size_t)snprintf(drop + k, sizeof(drop) - k, "%s%d", i > SP3_LIMIT ? ", " : "", fm.id[d][i]);
            }
            if (c > SP3_LIMIT) textf(tail, sizeof(tail), ts(TS_SP3_GUTTER_DROPPED), drop);
            ui::StatusText(ts(TS_SP3_GUTTER), d, c, c < SP3_LIMIT ? c : SP3_LIMIT, tail);
        }
        else {
            n = sp3PlaneAt(s, fm, dot, d);
            if (n < 0 && boxes) n = boxAt(dot, d);
        }
        if (n >= 0) {
            int k = 0;
            while (k < fm.count[d] && fm.id[d][k] != n) k++;
            hotNext = n;
            spriteStatus(n, k >= SP3_LIMIT);
            if (clicked) pinPlane(n, f);
        }
        else if (clicked) {
            shell::releaseSelection();
            lineFixed = lineFixed == d ? -1 : d;
        }
    }
    parts::drawCheckerRect(dl, img, end);
    if (placePic.tex.id()) ui::ImageNearest(dl, placePic.tex.id(), img, end);
    parts::canvasGrid(cv, dl, img, end, px, px);
    ui::CanvasXf xf = {};
    xf.origin = img;
    xf.scale = ImVec2(px, px);
    xf.clipMin = img;
    xf.clipMax = end;
    xf.gutterMin = ImVec2(at.x, y0);
    xf.gutterMax = ImVec2(img.x - ui::Px(4), end.y);
    dl->PushClipRect(img, end, true);
    drawBoxes(dl, xf);
    if (placeLine >= 0) {
        float hy = y0 + (placeLine + 0.5f) * px;
        dl->AddLine(ImVec2(img.x, hy), ImVec2(end.x, hy), ui::WithAlpha(t.accent, 160), th);
    }
    dl->PopClipRect();
    drawGutter(dl, xf);
    if (lineFixed >= 0 && lineFixed < lines) {
        float fy = floorf(y0 + (lineFixed + 0.5f) * px);
        dl->AddLine(ImVec2(at.x, fy), ImVec2(img.x, fy), t.accent, th * 2);
    }
}

/* What the frame did with plane n, over its atlas tile at a..b. */
void Sp3Panel::tileMarks(ImDrawList* dl, int n, ImVec2 a, ImVec2 b, float px) const
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = *fr;
    const Sp3Plane& p = fm.p[n];
    float th = fmaxf(1.0f, ui::Px(1));
    ImVec2 in(b.x - th, b.y - th);
    dl->PushClipRect(a, b, true);
    ui::PushFont(UI_FONT_BODY, 10);
    float tagH = ImGui::GetTextLineHeight(), ty = a.y;
    /* Tags stacked from the top on a backing, so none covers another. */
    auto tag = [&](const char* text, ImU32 col) {
        ImVec2 sz = ImGui::CalcTextSize(text);
        dl->AddRectFilled(ImVec2(a.x, ty), ImVec2(a.x + sz.x + ui::Px(2), ty + tagH), ui::WithAlpha(t.layer, 190));
        dl->AddText(ImVec2(a.x + ui::Px(1), ty), col, text);
        ty += tagH;
    };
    bool off = fm.off == NULL && p.state == SP3_ST_OFF_SCREEN;
    if (p.step >= fm.stopStep) {
        dl->AddRectFilled(a, b, ui::WithAlpha(t.layer, 170));
    }
    else if (fm.off == NULL && !off) {
        /* Rows dropped on some line hatched, rows no line shows dim. */
        for (int r = 0; r < p.rows;) {
            int st = fm.rowSt[n][r], e = r;
            while (e + 1 < p.rows && fm.rowSt[n][e + 1] == st) e++;
            ImVec2 ra(a.x, a.y + r * px), rb(b.x, a.y + (e + 1) * px);
            if (st & 2) {
                dl->AddRectFilled(ra, rb, ui::WithAlpha(t.critical, 70));
                parts::hatch(dl, ra, rb, ui::WithAlpha(t.critical, 200));
            }
            else if (st == 0) {
                dl->AddRectFilled(ra, rb, ui::WithAlpha(t.layer, 150));
            }
            r = e + 1;
        }
    }
    bool live = p.step < fm.stopStep;
    if (live && !atlasAny[n]) parts::dashedRect(dl, a, in, t.textTertiary, th);
    if (live && p.mgx == 0) parts::dashedRect(dl, a, in, t.caution, th);
    /* A plane folded onto one of 0-15 by R#5: a corner mark, under the tags. */
    if (p.alias != n) {
        float k = fmaxf(3.0f, (b.x - a.x) * 0.3f);
        dl->AddTriangleFilled(ImVec2(b.x - k, a.y), ImVec2(b.x, a.y), ImVec2(b.x, a.y + k), t.caution);
    }
    if (p.step == fm.stopStep) {
        /* The terminator: a red bar, "End" and its Y. */
        char yv[16];
        snprintf(yv, sizeof(yv), "Y=%d", p.y);
        dl->AddRectFilled(ImVec2(b.x - ui::Px(3), a.y), b, t.critical);
        tag(ts(TS_SPR_TAB_END), t.critical);
        tag(yv, t.textTertiary);
    }
    if (live) {
        if (off) tag(ts(TS_SPR_OFF_SCREEN), t.textSecondary);
        if (p.mgx == 0) tag("MGX 0", t.caution);
        if (!atlasAny[n]) tag(ts(TS_SP3_TAB_EMPTY), t.textTertiary);
        if (p.weight < 4 && b.y - tagH >= ty) dl->AddText(ImVec2(b.x - ImGui::CalcTextSize("TP").x - ui::Px(1), b.y - tagH), t.textSecondary, "TP");
    }
    ui::PopFont();
    dl->PopClipRect();
}

/* The 64 planes' tiles, 16 a row, each as tall as its plane; the line row under them. */
void Sp3Panel::drawAttrs(Focus& f, ImVec2 at, int N, float partW)
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = *fr;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight(), px = (float)N, tile = 16 * px, cellW = tile + ui::Px(4);
    ui::PushFont(UI_FONT_STRONG);
    dl->AddText(at, t.text, ts(TS_SPR_TAB_ATTRS));
    ui::PopFont();
    float y = floorf(at.y + lh + ui::Px(6));
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 16; c++) {
            int n = r * 16 + c;
            const Sp3Plane& p = fm.p[n];
            float x = at.x + c * cellW;
            ImVec2 a(x, y + lh), b(x + tile, y + lh + p.rows * px);
            bool isHot = n == hot, isPin = n == pin;
            char num[8];
            snprintf(num, sizeof(num), "%d", n);
            dl->AddText(ImVec2(x, y), isHot || isPin ? t.accent : p.step >= fm.stopStep ? t.textTertiary : t.textSecondary, num);
            atlasImage(dl, n, a, px);
            parts::canvasGrid(cv, dl, a, b, px, px);
            tileMarks(dl, n, a, b, px);
            int lit = isPin ? 2 : isHot ? 1 : 0;
            if (lit) dl->AddRect(ImVec2(a.x - th * lit, a.y - th * lit), ImVec2(b.x + th * lit, b.y + th * lit), t.accent, 0, th * lit);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(n);
            ImGui::InvisibleButton("##sp3tile", ImVec2(tile, b.y - a.y));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
                char tip[256];
                stateText(n, tip, sizeof(tip));
                ui::ItemTooltip(tip);
                hotNext = n;
                spriteStatus(n, false);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) pinPlane(n, f);
            }
        }
        y += lh + attrRows(r) * px + ui::Px(6);
    }
    drawLineRow(f, ImVec2(at.x, floorf(y + ui::Px(4))), N, partW);
}

namespace {
enum { WHY_HOVER, WHY_FIXED, WHY_PIN_DROP, WHY_PIN, WHY_OVER, WHY_BUSIEST };
}

/* The line row's line and why: the placement line under the mouse, else the one a click fixed,
** else the pinned plane's first dropped or first line, else the busiest line. */
int Sp3Panel::lineRowPick(int* why) const
{
    const Sp3Frame& fm = *fr;
    if (placeLine >= 0) {
        *why = WHY_HOVER;
        return placeLine;
    }
    if (lineFixed >= 0 && lineFixed < fm.lines) {
        *why = WHY_FIXED;
        return lineFixed;
    }
    if (pin >= 0 && fm.p[pin].firstLine >= 0) {
        const Sp3Plane& p = fm.p[pin];
        *why = p.firstDropped >= 0 ? WHY_PIN_DROP : WHY_PIN;
        return p.firstDropped >= 0 ? p.firstDropped : p.firstLine;
    }
    int line = -1, most = 0;
    for (int d = 0; d < fm.lines; d++) {
        if (fm.count[d] > most) {
            most = fm.count[d];
            line = d;
        }
    }
    *why = most > SP3_LIMIT ? WHY_OVER : WHY_BUSIEST;
    return line;
}

/* The planes in range of the line lineRowPick gives, in scan order, a fence at 16. */
void Sp3Panel::drawLineRow(Focus& f, ImVec2 at, int N, float partW)
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = *fr;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight(), px = (float)N;
    int why = WHY_BUSIEST, line = lineRowPick(&why);
    if (line < 0) return;
    static const ToolStringId whyText[] = { TS_SPR_TAB_WHY_HOVER, TS_SPR_TAB_WHY_FIXED, TS_SPR_TAB_WHY_PIN_DROP, TS_SPR_TAB_WHY_PIN,
                                            TS_SPR_TAB_WHY_OVER, TS_SPR_TAB_WHY_BUSIEST };
    int count = fm.count[line];
    char cap[256];
    int cl = textf(cap, sizeof(cap), ts(TS_SPR_TAB_LINE), line, count);
    if (cl > 0 && cl < (int)sizeof(cap)) textf(cap + cl, sizeof(cap) - cl, "%s", ts(whyText[why]));
    bool overCap = ImGui::ItemAdd(ImRect(at, ImVec2(at.x + partW, at.y + lh)), 0) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    ui::TextFit(dl, at, at.x + partW, cap, t.textSecondary, overCap);
    /* Two rows of 16 cells as wide as the attribute tiles: the 16 drawn, a fence, then the dropped.
    ** Each tile is the 16 rows about the row the line samples, so every cell is as tall. */
    float tile = 16 * px, cellW = tile + ui::Px(4), rowH = tile + lh + ui::Px(2);
    float y1 = floorf(at.y + lh + ui::Px(4)), y2 = floorf(y1 + rowH + lh + ui::Px(2));
    int dropShown = count - SP3_LIMIT < SP3_LIMIT ? count - SP3_LIMIT : SP3_LIMIT;
    char more[16] = "";
    if (count > 2 * SP3_LIMIT) {
        dropShown = SP3_LIMIT - 1;
        snprintf(more, sizeof(more), "+%d", count - SP3_LIMIT - dropShown);
    }
    ui::PushFont(UI_FONT_BODY, 10);
    for (int k = 0; k < count && k < SP3_LIMIT + (dropShown > 0 ? dropShown : 0); k++) {
        int n = fm.id[line][k];
        const Sp3Plane& p = fm.p[n];
        bool drop = k >= SP3_LIMIT;
        float x = at.x + (k % SP3_LIMIT) * cellW, y = drop ? y2 : y1;
        ImVec2 a(x, y), b(x + tile, y + tile);
        int row = sp3SourceRow(fm, n, line), r = row < 0 ? 0 : p.rvy ? p.rows - 1 - row : row;
        int w0 = r - 8 < 0 ? 0 : r - 8 > p.rows - 16 ? p.rows - 16 : r - 8;
        parts::drawCheckerRect(dl, a, b);
        if (atlasPic.tex.id()) {
            float u = (float)((n & 15) * 16) / 256, v = (float)((n >> 4) * 128 + w0) / 512;
            ui::ImageNearest(dl, atlasPic.tex.id(), a, b, ImVec2(u, v), ImVec2(u + 16.0f / 256, v + 16.0f / 512));
        }
        parts::canvasGrid(cv, dl, a, b, px, px);
        if (row >= 0) {
            float ry = a.y + (r - w0 + 0.5f) * px;
            dl->AddLine(ImVec2(a.x, ry), ImVec2(b.x, ry), t.accent, th);
        }
        if (n == pin || n == hot) dl->AddRect(ImVec2(a.x - th, a.y - th), ImVec2(b.x + th, b.y + th), t.accent, 0, th);
        if (drop) {
            dl->AddLine(a, b, t.critical, th * 1.5f);
            dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), t.critical, th * 1.5f);
        }
        char num[16];
        snprintf(num, sizeof(num), "#%d", n);
        if (ImGui::CalcTextSize(num).x <= cellW) dl->AddText(ImVec2(x, b.y + ui::Px(1)), drop ? t.critical : t.textSecondary, num);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(100 + k);
        ImGui::InvisibleButton("##sp3ltile", ImVec2(tile, tile));
        ImGui::PopID();
        if (ImGui::IsItemHovered()) {
            hotNext = n;
            spriteStatus(n, drop);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) pinPlane(n, f);
        }
    }
    ui::PopFont();
    if (more[0]) dl->AddText(ImVec2(at.x + dropShown * cellW, floorf(y2 + (tile - lh) / 2)), t.critical, more);
    if (count > SP3_LIMIT) {
        /* The fence: the VDP stops drawing here; what is under it is dropped. */
        float fy = floorf(y1 + rowH + ui::Px(1));
        char lim[64];
        textf(lim, sizeof(lim), ts(TS_SPR_TAB_FENCE), SP3_LIMIT);
        dl->AddLine(ImVec2(at.x, fy), ImVec2(at.x + SP3_LIMIT * cellW - ui::Px(4), fy), t.critical, th * 2);
        dl->AddText(ImVec2(at.x, fy + th), t.critical, lim);
    }
}

/* The hovered plane, else the pinned one, row by row: the line at each change of state,
** dropped rows hatched, rows no line shows dim; its palette set under it. */
void Sp3Panel::drawMag(const VdpDebugState* s, ImVec2 at, int N, float partW)
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = *fr;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = fmaxf(1.0f, ui::Px(1)), lh = ImGui::GetTextLineHeight();
    int n = hot >= 0 ? hot : pin;
    if (n < 0) {
        ImGui::SetCursorScreenPos(at);
        ImGui::PushTextWrapPos(at.x + partW);
        parts::note(ts(TS_SPR_TAB_MAG_HINT));
        ImGui::PopTextWrapPos();
        return;
    }
    const Sp3Plane& p = fm.p[n];
    char head[256];
    if (p.firstLine >= 0) textf(head, sizeof(head), ts(TS_SP3_TAB_MAG), n, p.page, p.pattern, p.x, p.firstLine, p.lastLine, p.mgx, p.height);
    else textf(head, sizeof(head), ts(TS_SP3_TAB_MAG_OFF), n, p.page, p.pattern, p.x, p.mgx, p.height);
    ui::PushFont(UI_FONT_STRONG);
    ui::TextFit(dl, at, at.x + partW, head, t.text, false);
    ui::PopFont();
    float m = magScale(p.rows, N), y0 = floorf(at.y + lh + ui::Px(6)), gx = floorf(at.x + magLabelW());
    ImVec2 ga(gx, y0), gb(gx + 16 * m, y0 + p.rows * m);
    atlasImage(dl, n, ga, m);
    /* The first display line each row, as the tile shows it, is in range on. */
    int rowLine[128];
    for (int r = 0; r < 128; r++) rowLine[r] = -1;
    for (int d = p.firstLine; d >= 0 && d <= p.lastLine; d++) {
        int row = sp3SourceRow(fm, n, d);
        int r = row < 0 ? -1 : p.rvy ? p.rows - 1 - row : row;
        if (r >= 0 && rowLine[r] < 0) rowLine[r] = d;
    }
    bool live = p.step < fm.stopStep && fm.off == NULL && p.state != SP3_ST_OFF_SCREEN;
    if (p.step >= fm.stopStep) dl->AddRectFilled(ga, gb, ui::WithAlpha(t.layer, 170));
    int prevSt = -1;
    float labelEnd = -1e9f;
    for (int r = 0; live && r < p.rows; r++) {
        int st = fm.rowSt[n][r];
        float ry = y0 + r * m, ly = floorf(ry + (m - lh) / 2);
        if (st & 2) parts::hatch(dl, ImVec2(gx, ry), ImVec2(gb.x, ry + m), ui::WithAlpha(t.critical, 200));
        else if (st == 0) dl->AddRectFilled(ImVec2(gx, ry), ImVec2(gb.x, ry + m), ui::WithAlpha(t.layer, 150));
        if (st != prevSt && rowLine[r] >= 0 && ly >= labelEnd) {
            char num[16];
            snprintf(num, sizeof(num), "%d", rowLine[r]);
            dl->AddText(ImVec2(at.x, ly), (st & 2) ? t.critical : t.textSecondary, num);
            labelEnd = ly + lh;
        }
        prevSt = st;
    }
    parts::canvasGrid(cv, dl, ga, gb, m, m);
    dl->AddRect(ga, gb, t.controlStrongStroke, 0, th);
    swatchRow(dl, s, p.ps, ImVec2(gx, gb.y + ui::Px(8)));
    /* Held while the mouse is over it, so it does not fall back to the pin. */
    ImGui::SetCursorScreenPos(ga);
    ImGui::InvisibleButton("##sp3mag", ImVec2(gb.x - ga.x, gb.y - ga.y));
    if (ImGui::IsItemHovered()) {
        int r = (int)floorf((ImGui::GetIO().MousePos.y - y0) / m);
        hotNext = n;
        if (r >= 0 && r < p.rows) {
            int row = p.rvy ? p.rows - 1 - r : r;
            ui::StatusText(ts(TS_SP3_STATUS_PATTERN), n, p.page, p.pattern, row, p.rows,
                           sp3PatternAddr(fm, p.page, p.pattern, row) & s->vramMask);
        }
    }
}

/* The pattern pages in use at N px a dot, one under another, each plane's pattern boxed. */
void Sp3Panel::drawPats(const VdpDebugState* s, Focus& f, ImVec2 at, int N, float partW)
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = *fr;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lh = ImGui::GetTextLineHeight(), px = (float)N;
    ui::PushFont(UI_FONT_STRONG);
    dl->AddText(at, t.text, ts(TS_SPR_TAB_PATS));
    ui::PopFont();
    char sub[256];
    textf(sub, sizeof(sub), ts(TS_SP3_TAB_PATS_SUB), sheetPs);
    ui::TextFit(dl, ImVec2(at.x, at.y + lh + ui::Px(2)), at.x + partW, sub, t.textSecondary, false);
    float y = floorf(at.y + lh * 2 + ui::Px(6));
    /* Each page under its own label, the sheet N px a dot. */
    for (int i = 0; i < pageCount; i++) {
        char lab[64];
        int q = pages[i];
        textf(lab, sizeof(lab), ts(TS_SP3_PAGE), q & 7, (fm.patBase + (q << 15)) & fm.vramMask);
        dl->AddText(ImVec2(at.x, y), t.textSecondary, lab);
        ImVec2 a(at.x, floorf(y + pageLabelH())), b(a.x + 256 * px, a.y + 256 * px);
        ui::CanvasXf xf = {};
        xf.origin = ImVec2(a.x, a.y - sheetY(i) * px);
        xf.scale = ImVec2(px, px);
        xf.clipMin = a;
        xf.clipMax = b;
        xf.gutterMin = xf.gutterMax = a;
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##sp3page", ImVec2(b.x - a.x, b.y - a.y));
        ImGui::PopID();
        if (ImGui::IsItemHovered()) {
            ImVec2 m = ImGui::GetIO().MousePos;
            int row, n = sheetHit((int)floorf((m.x - a.x) / px), sheetY(i) + (int)floorf((m.y - a.y) / px), &row);
            if (n >= 0) {
                const Sp3Plane& p = fm.p[n];
                hotNext = n;
                ui::StatusText(ts(TS_SP3_STATUS_PATTERN), n, p.page, p.pattern, row, p.rows,
                               sp3PatternAddr(fm, p.page, p.pattern, row) & s->vramMask);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) pinPlane(n, f);
            }
        }
        parts::drawCheckerRect(dl, a, b);
        if (sheetPic.tex.id()) {
            ui::ImageNearest(dl, sheetPic.tex.id(), a, b, ImVec2(0, (float)i / pageCount), ImVec2(1, (float)(i + 1) / pageCount));
        }
        parts::canvasGrid(cv, dl, a, b, px, px);
        dl->PushClipRect(a, b, true);
        drawPatternBoxes(dl, xf, i);
        dl->PopClipRect();
        y = b.y;
    }
}

void Sp3Panel::drawTab(const VdpDebugState* s, Focus& f)
{
    const Sp3Frame& fm = frame(s);
    hot = hotNext;
    hotNext = -1;
    placeLine = -1;
    /* A new pin shows its own line. */
    if (pin != rowPin && pin >= 0) lineFixed = -1;
    rowPin = pin;
    cv.shared = &shell::zoom();
    ui::BeginCommandBar("bar");
    parts::barToggle("##sp3drop", UI_ICON_DROPPED, ts(TS_SPR_DROPPED), ts(TS_SPR_DROPPED_TIP), &dropped);
    parts::barArtToggle("##sp3boxes", ui::DECOR_FRAME, ts(TS_BOXES_TIP), &boxes);
    ui::GridToggle(cv);
    ui::CommandBarRight(ui::ZoomControlsWidth());
    ui::ZoomControls("zoom", cv);
    ui::EndCommandBar();

    choosePages();
    header(s);
    ensureAtlas(s);
    /* The sheets read VRAM, R#6 and the palette set alone, so they wait for those to change. */
    unsigned pal = fnvWords(s->paletteExt + (sheetPs << 4), 16);
    char key[64];
    size_t k = (size_t)snprintf(key, sizeof(key), "%X/%X/%08X/", fm.patBase & s->vramMask, s->vramMask, pal);
    for (int i = 0; i < pageCount && k < sizeof(key); i++) k += (size_t)snprintf(key + k, sizeof(key) - k, "%d", pages[i]);
    bool decoded = pageCount > 0 && sheetPic.needsDecodeVram(key);
    if (decoded) {
        sheetPic.w = 256;
        sheetPic.h = 256 * pageCount;
        sheetPic.img.resize((size_t)256 * sheetPic.h);
        for (int i = 0; i < pageCount; i++) {
            sp3RenderPage(s, fm, pages[i], sheetPs, sheetPic.img.data() + (size_t)i * 256 * 256);
        }
    }
    if (pageCount > 0) sheetPic.upload(decoded);

    /* As the mode 1/2 tab: the parts in a child, a scrollbar only for what does not fit. */
    ImVec2 avail = ImGui::GetContentRegionAvail(), childAt = ImGui::GetCursorScreenPos();
    float W = avail.x - ImGui::GetStyle().ScrollbarSize - ui::Px(8);
    ui::CanvasZoom& zs = cv.z();
    zs.zoom = zs.zoom < 1 ? 1 : zs.zoom > 32 ? 32 : zs.zoom;
    /* Ctrl+wheel keeps the dot under the mouse where it is, or the part's corner in view when it moves. */
    int z = ui::CanvasWheelZoom(cv), anchor = -1;
    ImVec2 m = ImGui::GetIO().MousePos, dot(0, 0);
    if (z > 0) {
        for (int p = 0; p < PART_COUNT && anchor < 0; p++) {
            ImVec2 a(lastO.x + lastL.at[p].x, lastO.y + lastL.at[p].y);
            if (lastL.scale[p] > 0 && m.x >= a.x && m.y >= a.y && m.x < a.x + lastL.size[p].x && m.y < a.y + lastL.size[p].y) {
                anchor = p;
                dot = ImVec2((m.x - a.x) / lastL.scale[p], (m.y - a.y) / lastL.scale[p]);
            }
        }
        zs.zoom = z;
        zs.fit = false;
    }
    int N = zs.fit ? fitZoom(W, avail.y) : zs.zoom;
    Layout L = layoutAt(W, N);
    float bar = ImGui::GetStyle().ScrollbarSize;
    bool over = L.height + ui::Px(8) > avail.y, overX = false;
    for (int i = 0; i < 2; i++) {
        overX = L.width > avail.x - (over ? bar : 0);
        over = L.height + ui::Px(8) > avail.y - (overX ? bar : 0);
    }
    ImVec2 content(fmaxf(W, L.width), L.height + ui::Px(8));
    ImVec2 scroll(overX ? -1.0f : 0.0f, over ? -1.0f : 0.0f);
    if (anchor >= 0) {
        ImVec2 a(L.at[anchor].x, L.at[anchor].y + ui::Px(4));
        bool moved = L.tier != lastL.tier;
        ImVec2 tgt = moved ? a : ImVec2(a.x + dot.x * L.scale[anchor], a.y + dot.y * L.scale[anchor]);
        ImVec2 off = moved ? ImVec2(0, 0) : ImVec2(m.x - childAt.x, m.y - childAt.y);
        if (overX) scroll.x = fmaxf(0, tgt.x - off.x);
        if (over) scroll.y = fmaxf(0, tgt.y - off.y);
        ImGui::SetNextWindowContentSize(content);
    }
    if (scroll.x >= 0 || scroll.y >= 0) ImGui::SetNextWindowScroll(scroll);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags bars = (over ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_NoScrollbar) |
                            (overX ? ImGuiWindowFlags_AlwaysHorizontalScrollbar : 0);
    ImGui::BeginChild("sp3", avail, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | bars);
    ImGui::PopStyleVar();
    ImVec2 o = ImGui::GetCursorScreenPos();
    o.y += ui::Px(4);
    lastL = L;
    lastO = ImVec2(o.x, o.y - ui::Px(4));
    /* Each part from a whole pixel, so its dots come out even. */
    auto partAt = [&](int p) { return ImVec2(floorf(o.x + L.at[p].x), floorf(o.y + L.at[p].y)); };
    drawPlace(s, f, partAt(PART_PLACE), N);
    drawAttrs(f, partAt(PART_ATTRS), N, L.size[PART_ATTRS].x);
    drawMag(s, partAt(PART_MAG), N, L.size[PART_MAG].x);
    drawPats(s, f, partAt(PART_PATS), N, L.size[PART_PATS].x);
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y - ui::Px(4)));
    ImGui::Dummy(content);
    bool hovered = ImGui::IsWindowHovered();
    ImGui::EndChild();
    ui::CanvasShown(cv, (float)N, hovered);

    /* The hovered plane's attributes light up on the VRAM map. */
    if (hotNext >= 0 && !f.pinned) {
        f.space = FOCUS_PHYS;
        f.addr = (UInt32)fm.p[hotNext].attr;
        f.len = 8;
        f.kind = FOCUS_VRAM;
        f.a = f.b = 0;
    }
}

bool Sp3Panel::layer(const VdpDebugState* s, int lines, bool noLimit, uint32_t* out, const char** offAtEnd)
{
    /* Each line's own band says whether it has sprites, so scan them all. */
    const Sp3Frame& fm = frame(s, true);
    if (fm.lines != lines) return false;
    sp3RenderPlacement(s, fm, 0, out, noLimit ? SP3_PLANES : SP3_LIMIT, true);
    *offAtEnd = fm.off;
    return true;
}

void Sp3Panel::screenBegin(const VdpDebugState* s, bool boxes, bool counts)
{
    /* Each line's own band says whether it has sprites, so scan them all. */
    const Sp3Frame& fm = frame(s, true);
    hot = hotNext;
    hotNext = -1;
    scrBoxes = boxes;
    scrCounts = counts;
    scrLimit = vdpGetNoSpritesLimit() ? SP3_PLANES : SP3_LIMIT;
    memset(scrState, SP3_LINE_OFF, sizeof(scrState));
    for (int d = 0; d < SP3_LINES; d++) {
        scrDrop[d] = 0;
        for (int k = scrLimit; d < fm.lines && k < fm.count[d]; k++) scrDrop[d] |= 1ull << fm.id[d][k];
    }
}

void Sp3Panel::screenLine(int d, int row, int org, const VdpBand* b)
{
    if (!fr || d < 0 || d >= fr->lines || row < 0 || b == NULL || b->prev || b->unknown) return;
    int st = sp3LineState(regs, b->mode, b->regs);
    scrRow[d] = row;
    scrOrg[d] = org;
    scrState[d] = (uint8_t)st;
}

/* The displayed line shown at image row y that screenLine kept, or -1. */
int Sp3Panel::screenLineOfRow(int y) const
{
    for (int d = 0; fr && d < fr->lines; d++) {
        if (scrState[d] != SP3_LINE_OFF && scrRow[d] == y) return d;
    }
    return -1;
}

/* Plane n's boxes over the lines that show the frame end, drawn as the mode 1/2 boxes. */
void Sp3Panel::screenBox(ImDrawList* dl, const ui::CanvasXf& xf, int n, bool me) const
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& f = *fr;
    const Sp3Plane& p = f.p[n];
    float th = fmaxf(1.0f, ui::Px(1)), lw = me ? th * 2 : th;
    int x0[2], x1[2];
    if (p.step >= f.stopStep || p.firstLine < 0 || sp3XRuns(p, x0, x1) == 0) return;
    bool dashed = p.alias != n && !me;
    int end = -2;
    for (int d = p.firstLine; d <= p.lastLine;) {
        if (scrState[d] != SP3_LINE_SAME || sp3SourceRow(f, n, d) < 0) {
            d++;
            continue;
        }
        int e = d;
        while (e < p.lastLine && scrState[e + 1] == SP3_LINE_SAME && sp3SourceRow(f, n, e + 1) >= 0 &&
               scrRow[e + 1] == scrRow[e] + 1 && scrOrg[e + 1] == scrOrg[d]) e++;
        float xa = (float)(scrOrg[d] + 2 * x0[0]), xb = (float)(scrOrg[d] + 2 * x1[0]);
        ImVec2 a = xf.toScreen(xa, (float)scrRow[d]), b = xf.toScreen(xb, (float)(scrRow[e] + 1));
        ImU32 col = 0;
        dl->AddRect(a, b, ui::WithAlpha(t.layer, 200), 0, lw + th * 2);
        /* One outline; each part, dropped or not, draws its sides, the first the top and the last the bottom. */
        for (int g = d; g <= e;) {
            bool drop = ((scrDrop[g] >> n) & 1) != 0;
            int h = g;
            while (h < e && (((scrDrop[h + 1] >> n) & 1) != 0) == drop) h++;
            ImU32 c = me ? t.accent : drop ? t.critical : ui::WithAlpha(t.text, 190);
            ImVec2 pa = xf.toScreen(xa, (float)scrRow[g]), pb = xf.toScreen(xb, (float)(scrRow[h] + 1));
            if (g == d) col = c;
            if (drop) parts::hatch(dl, pa, pb, ui::WithAlpha(t.critical, 150));
            if (g == d && h == e && dashed) parts::dashedRect(dl, pa, pb, c, lw);
            else if (g == d && h == e) dl->AddRect(pa, pb, c, 0, lw);
            else {
                boxEdge(dl, pa, ImVec2(pa.x, pb.y), c, lw, dashed);
                boxEdge(dl, ImVec2(pb.x, pa.y), pb, c, lw, dashed);
                if (g == d) boxEdge(dl, pa, ImVec2(pb.x, pa.y), c, lw, dashed);
                if (h == e) boxEdge(dl, ImVec2(pa.x, pb.y), pb, c, lw, dashed);
            }
            g = h + 1;
        }
        /* One number a plane: a box that moved sideways right under another of it goes without. */
        if (scrRow[d] != end + 1) {
            char num[8];
            snprintf(num, sizeof(num), "%d", n);
            ImVec2 sz = ImGui::CalcTextSize(num);
            if (b.x - a.x >= sz.x + ui::Px(4) && b.y - a.y >= sz.y) {
                dl->AddRectFilled(ImVec2(a.x + th, a.y + th), ImVec2(a.x + th + sz.x + ui::Px(2), a.y + th + sz.y), ui::WithAlpha(t.layer, 170));
                dl->AddText(ImVec2(a.x + th + ui::Px(1), a.y + th), col, num);
            }
        }
        end = scrRow[e];
        d = e + 1;
    }
}

void Sp3Panel::screenOverlay(ImDrawList* dl, const ui::CanvasXf& xf, float marksW) const
{
    if (!fr) return;
    const UiTokens& t = uiTokens();
    const Sp3Frame& f = *fr;
    int focus = pin >= 0 ? pin : hot;
    dl->PushClipRect(xf.clipMin, xf.clipMax, true);
    /* The plane the scan reaches first wins a dot, so its box goes on top; the focused one over all. */
    for (int step = SP3_PLANES - 1; scrBoxes && step >= 0; step--) {
        if (f.order[step] != focus) screenBox(dl, xf, f.order[step], false);
    }
    if (focus >= 0 && (scrBoxes || focus == pin)) screenBox(dl, xf, focus, true);
    dl->PopClipRect();
    if (!scrCounts || xf.gutterMax.x <= xf.gutterMin.x) return;
    float th = fmaxf(1.0f, ui::Px(1)), gx0 = xf.gutterMin.x;
    float per = (xf.gutterMax.x - xf.gutterMin.x - marksW) / (2 * SP3_LIMIT);
    for (int d = 0; d < f.lines; d++) {
        int c = f.count[d];
        if (scrState[d] != SP3_LINE_SAME || c == 0) continue;
        ImVec2 a = xf.toScreen(0, (float)scrRow[d]), b = xf.toScreen(0, (float)(scrRow[d] + 1));
        float len = (float)(c < 2 * SP3_LIMIT ? c : 2 * SP3_LIMIT) * per;
        /* Red past the VDP's 16, at the tick, also while the emulator draws them all. */
        dl->AddRectFilled(ImVec2(gx0, a.y), ImVec2(gx0 + len, fmaxf(b.y, a.y + 1)), c > SP3_LIMIT ? t.critical : t.textSecondary);
    }
    float tx = floorf(gx0 + per * SP3_LIMIT);
    dl->AddLine(ImVec2(tx, xf.gutterMin.y), ImVec2(tx, xf.gutterMax.y), t.textTertiary, th);
}

bool Sp3Panel::screenHover(const VdpDebugState* s, Focus& f, const ui::CanvasHit& h)
{
    if (!fr) return false;
    const Sp3Frame& fm = *fr;
    if (h.gutterY >= 0) {
        int d = scrCounts ? screenLineOfRow(h.gutterY) : -1;
        if (d < 0) return false;
        if (scrState[d] == SP3_LINE_DIFFERS) {
            ui::StatusText(ts(TS_SP3_SCREEN_DIFFERS), d);
            return true;
        }
        int c = fm.count[d];
        char drop[256] = "", tail[300] = "";
        size_t n = 0;
        for (int i = scrLimit; i < c && n < sizeof(drop); i++) {
            n += (size_t)snprintf(drop + n, sizeof(drop) - n, "%s%d", i > scrLimit ? ", " : "", fm.id[d][i]);
        }
        if (c > scrLimit) textf(tail, sizeof(tail), ts(TS_SP3_GUTTER_DROPPED), drop);
        ui::StatusText(ts(TS_SP3_GUTTER), d, c, c < scrLimit ? c : scrLimit, tail);
        return true;
    }
    if (!h.valid) return false;
    int d = screenLineOfRow(h.y);
    if (d < 0 || scrState[d] != SP3_LINE_SAME || h.x < scrOrg[d]) return false;
    int dot = (h.x - scrOrg[d]) >> 1, n = -1;
    if (dot >= 256) return false;
    /* As with the mode 1/2 boxes: with them off, only the pinned plane answers. */
    if (scrBoxes) {
        n = sp3PlaneAt(s, fm, dot, d);
        if (n < 0) n = boxAt(dot, d);
    }
    else if (pin >= 0) {
        n = boxAt(dot, d, pin);
    }
    if (n < 0) return false;
    const Sp3Plane& p = fm.p[n];
    int k = 0;
    while (k < fm.count[d] && fm.id[d][k] != n) k++;
    hotNext = n;
    char text[512];
    textf(text, sizeof(text), ts(TS_SP3_STATUS_SPRITE), n, p.y, p.x, p.page, p.pattern, p.mgx, p.height, p.attr,
          k >= scrLimit ? ts(TS_STATUS_SPRITE_DROPPED) : "");
    ui::StatusText("%s", text);
    if (h.clicked) pinPlane(n, f);
    /* The hovered plane's attributes light up on the VRAM map. */
    if (!f.pinned) {
        f.space = FOCUS_PHYS;
        f.addr = (UInt32)p.attr;
        f.len = 8;
        f.kind = FOCUS_VRAM;
        f.a = f.b = 0;
    }
    return true;
}

void Sp3Panel::card(const VdpDebugState* s, Focus&)
{
    const Sp3Frame& fm = frame(s);
    const UInt8* st = s->status;
    /* In the mode 1/2 card's order; every row on every picture, so none jumps. The why on hover. */
    if (ui::BeginKeyValue("sp3")) {
        bool hidden = !vdpGetSpritesEnable(), noLimit = vdpGetNoSpritesLimit() != 0;
        char value[192];
        ui::KeyValue(ts(TS_KV_SPRITE_MODE), "3");
        ui::KeyValue(ts(TS_KV_SIZE), "%s", ts(TS_SP3_KV_SIZE_ALL));
        if (fm.off != NULL) textf(value, sizeof(value), ts(TS_KV_SHOWN_NO), fm.off);
        else textf(value, sizeof(value), "%s", ts(TS_YES));
        ui::KeyValueTip(ts(TS_KV_SHOWN), hidden ? ts(TS_SPR_NOTE_HIDDEN) : NULL, "%s%s", value, hidden ? ts(TS_SPR_HIDDEN_SUFFIX) : "");
        if (fm.shuffle) ui::KeyValueTip(ts(TS_KV_ACTIVE), ts(TS_SP3_ACTIVE_SPS_TIP), "%s", ts(TS_SP3_ACTIVE_SPS));
        else if (fm.stopStep < SP3_PLANES) ui::KeyValue(ts(TS_KV_ACTIVE), ts(TS_KV_ACTIVE_VALUE), fm.stopStep, fm.p[fm.order[fm.stopStep]].y);
        else ui::KeyValue(ts(TS_KV_ACTIVE), "%s", ts(TS_SP3_ACTIVE_NONE));
        textf(value, sizeof(value), ts(TS_KV_OVER_LIMIT_VALUE), fm.overLines, SP3_LIMIT);
        ui::KeyValueTip(ts(TS_KV_OVER_LIMIT), noLimit ? ts(TS_SPR_NOTE_NO_LIMIT) : NULL, "%s%s", value, noLimit ? ts(TS_SPR_NO_LIMIT_SUFFIX) : "");
        if (fm.firstOverLine >= 0) {
            ui::KeyValue(ts(TS_KV_FIRST_DROPPED), ts(TS_KV_FIRST_DROPPED_VALUE), fm.id[fm.firstOverLine][SP3_LIMIT], fm.firstOverLine);
            ui::KeyValueMono(ts(TS_KV_S0_WOULD_READ), "&H%02X", 0x40 | fm.fifthPlane);
        }
        else {
            ui::KeyValue(ts(TS_KV_FIRST_DROPPED), "%s", ts(TS_KV_NONE));
            ui::KeyValueMono(ts(TS_KV_S0_WOULD_READ), "\xE2\x80\x94");
        }
        ui::KeyValueMono(ts(TS_KV_ATTRIBUTES), "%05X", fm.attrBase & s->vramMask);
        ui::KeyValueMono(ts(TS_KV_PATTERNS), "%05X", fm.patBase & s->vramMask);
        ui::KeyValueMonoTip("S#0", ts(TS_SP3_STATUS_NOTE), "&H%02X", st[0]);
        ui::KeyValueMonoTip("S#3-S#6", ts(TS_SP3_STATUS_NOTE), "%02X %02X %02X %02X", st[3], st[4], st[5], st[6]);
        ui::EndKeyValue();
    }
}

void Sp3Panel::summary(const VdpDebugState* s, char* out, size_t size)
{
    const Sp3Frame& fm = frame(s);
    int shown = 0;
    for (int n = 0; n < SP3_PLANES; n++) shown += shows(fm.p[n]);
    if (fm.off != NULL) textf(out, size, ts(TS_SP3_SUM_OFF), fm.off);
    else textf(out, size, ts(TS_SP3_SUM), SP3_LIMIT, shown);
}

/* The pinned plane: a header that lets go of it, its picture beside its attributes. */
void Sp3Panel::selection(const VdpDebugState* s, Focus& f)
{
    /* Its rows come and go by frame: held, the card keeps its height while one plane is pinned. */
    float y0 = ImGui::GetCursorPosY();
    if (pin != selHoldPin) {
        selHold = ui::HeightHold();
        selHoldPin = pin;
    }
    selectionBody(s, f);
    ui::HoldHeight(selHold, y0);
}

void Sp3Panel::selectionBody(const VdpDebugState* s, Focus&)
{
    const UiTokens& t = uiTokens();
    const Sp3Frame& fm = frame(s);
    if (pin < 0) return;
    const Sp3Plane& p = fm.p[pin];
    char head[64], caption[256];
    textf(head, sizeof(head), ts(TS_SEL_HEAD_SPRITE), pin);
    char why[96];
    bool off = fm.off != NULL && p.state != SP3_ST_AFTER_STOP;
    if (off) textf(why, sizeof(why), ts(TS_SPR_ST_OFF), fm.off);
    size_t at = p.firstLine >= 0 && fm.off == NULL
        ? (size_t)textf(caption, sizeof(caption), ts(TS_SEL_CAPTION_SPRITE), p.firstLine, p.lastLine, vdpModeName(s->screenMode))
        : (size_t)textf(caption, sizeof(caption), "%s \xC2\xB7 %s", off ? why : ts(TS_SPR_OFF_SCREEN), vdpModeName(s->screenMode));
    if (fm.shuffle && at < sizeof(caption)) textf(caption + at, sizeof(caption) - at, ts(TS_SP3_ST_STEP), p.step);

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float h = ui::Px(24), right = p0.x + ImGui::GetContentRegionAvail().x, x = p0.x, cy = floorf(p0.y + h / 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ui::HasIcons()) {
        ui::DrawPixelArt(dl, ImVec2(x + ui::Px(8), cy), ui::UI_ART_SHIP, ui::DECOR_NONE, t.text);
        x += ui::Px(24);
    }
    ui::PushFont(UI_FONT_STRONG);
    ImVec2 hs = ImGui::CalcTextSize(head);
    bool overHead = ImGui::ItemAdd(ImRect(ImVec2(x, p0.y), ImVec2(right - ui::Px(36), p0.y + h)), 0) &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    ui::TextFit(dl, ImVec2(x, floorf(cy - hs.y / 2)), right - ui::Px(36), head, t.text, overHead);
    ui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(right - ui::Px(28), p0.y));
    if (ui::IconButton("##sp3pin", UI_ICON_PINNED, "\xC3\x97", ts(TS_PIN_RELEASE_TIP), false, ImVec2(28, 24), 14)) pin = -1;
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(right - p0.x, h));
    ui::PushFont(UI_FONT_BODY, 12);
    ui::TextFitItem(caption, t.textSecondary);
    ui::PopFont();
    if (pin < 0) return;

    /* As the mode 1/2 card: the picture, RVX and RVY applied, MGX and MGY not. */
    const Sp3Plane& q = fm.p[pin];
    ensureAtlas(s);
    float cell = floorf(fmaxf(ui::Px(2), ui::Px(96) / q.rows));
    ImVec2 a = ImGui::GetCursorScreenPos();
    atlasImage(dl, pin, a, cell);
    dl->AddRect(a, ImVec2(a.x + 16 * cell, a.y + q.rows * cell), t.controlStrongStroke);
    ImGui::Dummy(ImVec2(16 * cell, q.rows * cell));
    ImGui::SameLine(0, ui::Px(12));
    ImGui::BeginGroup();
    if (ui::BeginKeyValue("sp3sel")) {
        char mgx[32], mgy[32];
        if (q.mgx) textf(mgx, sizeof(mgx), "%d", q.mgx);
        else textf(mgx, sizeof(mgx), "%s", ts(TS_SP3_MGX_ZERO));
        if (q.mgy) textf(mgy, sizeof(mgy), "%d", q.mgy);
        else textf(mgy, sizeof(mgy), "256 (0)");
        ui::KeyValue(ts(TS_KV_SPRITE), "%d", pin);
        char pxy[32], state[256];
        textf(pxy, sizeof(pxy), "PX %d, PY %d", q.pattern & 15, q.pattern >> 4);
        stateText(pin, state, sizeof(state), false);
        ui::KeyValue(ts(TS_SP3_KV_STATE), "%s", state);
        ui::KeyValueMono("Y, X", "%d, %d", q.y, q.x);
        ui::KeyValueMonoTip(ts(TS_KV_PATTERN), pxy, ts(TS_KV_PATTERN_AT), q.pattern, sp3PatternAddr(fm, q.page, q.pattern, 0) & s->vramMask);
        ui::KeyValue(ts(TS_SP3_KV_PAGE), "%d", q.page);
        ui::KeyValueMono(ts(TS_KV_ATTRIBUTES), "%05X", q.attr);
        ui::KeyValue(ts(TS_KV_SIZE), ts(TS_SP3_KV_SIZE_VALUE), q.rows, q.sizeCode);
        ui::KeyValue("MGX, MGY", "%s, %s", mgx, mgy);
        ui::KeyValue("RVX, RVY", "%s, %s", ts(q.rvx ? TS_ON : TS_OFF), ts(q.rvy ? TS_ON : TS_OFF));
        ui::KeyValue("TP", "%d%% (TP %d)", q.weight * 25, 4 - q.weight);
        ui::KeyValue("PS", "%d", q.ps);
        ui::KeyValue(ts(TS_KV_SHOWN), ts(TS_LINES_N), q.drawnLines);
        if (q.droppedLines > 0) ui::KeyValue(ts(TS_KV_DROPPED), ts(TS_KV_LINES_RANGE), q.firstDropped, q.lastDropped);
        if (fm.shuffle) ui::KeyValue(ts(TS_SP3_KV_STEP), "%d", q.step);
        ui::EndKeyValue();
    }
    ImGui::EndGroup();
    /* The 16 colours of its palette set, colour 0 clear. */
    swatchRow(dl, s, q.ps, ImGui::GetCursorScreenPos());
    ImGui::Dummy(ImVec2(16 * (ui::Px(12) + ui::Px(2)), ui::Px(12)));
}
