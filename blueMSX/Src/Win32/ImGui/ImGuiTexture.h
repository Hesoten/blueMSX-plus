/*****************************************************************************
**
** CPU-filled textures for the tool windows, e.g. refilled every frame.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
******************************************************************************
*/
#ifndef IMGUI_TEXTURE_H
#define IMGUI_TEXTURE_H

#include <stdint.h>
#include "imgui.h"

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

/* Lives on the host's shared device, so any tool window can draw it. */
class ImGuiDynamicTexture {
public:
    ImGuiDynamicTexture() {}
    ~ImGuiDynamicTexture() { release(); }

    /* Pixels are 0xAABBGGRR, w per row. Makes the texture anew when the size
    ** or the device changed. False while there is no device. */
    bool        update(const uint32_t* rgba, int w, int h);
    /* 0 until update succeeded on the current device. */
    ImTextureID id() const;
    int         width() const  { return w; }
    int         height() const { return h; }
    void        release();

private:
    ImGuiDynamicTexture(const ImGuiDynamicTexture&) = delete;
    ImGuiDynamicTexture& operator=(const ImGuiDynamicTexture&) = delete;

    ID3D11Texture2D*          tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int                       w = 0;
    int                       h = 0;
    unsigned                  generation = 0;
};

#endif
