/*****************************************************************************
**
** Decoded image of a chip view and its display texture.
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
#ifndef VIEW_PICTURE_H
#define VIEW_PICTURE_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include "ImGuiTexture.h"
#include "ChipView.h"

/* FNV-1a over n words, for keys and change tests. */
inline unsigned fnvWords(const uint32_t* p, size_t n)
{
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* Decoded again only when the content or the key (how it is read) changed,
** so a paused machine is decoded once. */
struct ViewPicture {
    ImGuiDynamicTexture   tex;
    std::vector<uint32_t> img;     /* 0xAABBGGRR */
    int                   w = 0, h = 0;
    unsigned              version = ~0u;
    char                  key[64] = "";

    bool needsDecode(const char* k)
    {
        return needsDecodeAt(shell::pictureVersion(), k);
    }
    /* The same, for a picture of VRAM and palette alone, its registers in the key. */
    bool needsDecodeVram(const char* k)
    {
        return needsDecodeAt(shell::vramVersion(), k);
    }
    /* The same, for content versioned apart from VRAM, e.g. the drawn picture. */
    bool needsDecodeAt(unsigned ver, const char* k)
    {
        if (version == ver && strcmp(key, k) == 0) {
            return false;
        }
        version = ver;
        snprintf(key, sizeof(key), "%s", k);
        return true;
    }
    void upload(bool decoded)
    {
        if ((decoded || tex.id() == 0) && w > 0 && h > 0) {
            tex.update(img.data(), w, h);
        }
    }
    /* Uploads img when its hash differs from *last (or there is no texture yet); keeps the hash. */
    void uploadChanged(unsigned* last)
    {
        unsigned h = fnvWords(img.data(), img.size());
        upload(h != *last);
        *last = h;
    }
    void forget()
    {
        tex.release();
        version = ~0u;
        key[0] = 0;
    }
};

#endif
