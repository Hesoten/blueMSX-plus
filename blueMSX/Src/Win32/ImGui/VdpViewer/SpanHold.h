/*****************************************************************************
**
** Smoothed view length that grows at once and shrinks after a delay.
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
#ifndef SPAN_HOLD_H
#define SPAN_HOLD_H

#include <math.h>
#include <algorithm>
#include <utility>
#include <vector>
#include "imgui.h"

/* A length a view keeps still while what it needs moves: it grows at once; once the need has
** stayed under it for 1.5 s it drops to the most of those 1.5 s and glides there. Unlike
** ui::HeightHold it glides, and it holds while the emulator is paused. */
struct SpanHold {
    double target = 0, shown = 0, since = -1;
    int    frame = -1;      /* ImGui frame of the last step */
    std::vector<std::pair<double, double>> recent;   /* the shorter needs of the last 1.5 s */

    /* Holds v from now, as if stepped last frame. */
    void reset(double v)
    {
        target = shown = v;
        since = -1;
        recent.clear();
        frame = ImGui::GetFrameCount() - 1;
    }
    /* Not stepped last frame: what it holds is stale. */
    bool stale() const { return frame != ImGui::GetFrameCount() - 1; }
    /* Drawn without a step this frame: still fresh next frame. restart: what it saw so far
    ** no longer counts toward a drop. */
    void touch(bool restart = false)
    {
        if (frame == ImGui::GetFrameCount() - 1) frame = ImGui::GetFrameCount();
        if (restart) {
            recent.clear();
            since = -1;
        }
    }
    /* Once a frame: v the length needed, live counts toward a drop, running lets it glide; it
    ** drops only to at most ratio of what it holds, to margin times the most, landing within
    ** 0.5% or a pixel of px across (none when px is 0). Returns what to show, maybe under v. */
    double step(double v, bool live, bool running, float px, double ratio = 1, double margin = 1)
    {
        double now = ImGui::GetTime();
        bool old = stale();
        if (live && (old || v >= target)) {
            target = v;
            recent.clear();
            since = -1;
            if (old || shown < target) shown = target;
        }
        else if (live) {
            if (since < 0) since = now;
            recent.push_back(std::make_pair(now, v));
            size_t gone = 0;
            while (gone < recent.size() && recent[gone].first < now - 1.5) gone++;
            recent.erase(recent.begin(), recent.begin() + gone);
            if (now - since >= 1.5) {
                double most = 0;
                for (const auto& r : recent) most = std::max(most, r.second);
                if (most <= target * ratio) target = most * margin;
            }
        }
        /* Paused it keeps what it held; the countdown starts over on resuming. */
        else if (!running) {
            recent.clear();
            since = -1;
        }
        if (running && shown > target) {
            double dt = std::min(std::max((double)ImGui::GetIO().DeltaTime, 0.0), 0.1);
            shown += (target - shown) * (1 - exp(-dt / 0.35));
            double land = std::max(target * 0.005, px > 0 ? shown / px : 0.0);
            if (shown - target <= land) shown = target;
        }
        frame = ImGui::GetFrameCount();
        return shown;
    }
};

#endif
