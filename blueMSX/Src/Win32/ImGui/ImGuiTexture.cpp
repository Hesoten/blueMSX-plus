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
#include <windows.h>
#include <d3d11.h>
#include <string.h>
#include "ImGuiTexture.h"
#include "ImGuiHost.h"

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

void ImGuiDynamicTexture::release()
{
    if (srv) { srv->Release(); srv = nullptr; }
    if (tex) { tex->Release(); tex = nullptr; }
    w = h = 0;
    generation = 0;
}

ImTextureID ImGuiDynamicTexture::id() const
{
    if (srv == nullptr || imguiHostDevice() == nullptr || generation != imguiHostDeviceGeneration()) {
        return (ImTextureID)0;
    }
    return (ImTextureID)(intptr_t)srv;
}

bool ImGuiDynamicTexture::update(const uint32_t* rgba, int width, int height)
{
    ID3D11Device*        device  = imguiHostDevice();
    ID3D11DeviceContext* context = imguiHostContext();
    if (device == nullptr || context == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (tex && (width != w || height != h || generation != imguiHostDeviceGeneration())) {
        release();
    }
    if (tex == nullptr) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = (UINT)width;
        desc.Height           = (UINT)height;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DYNAMIC;
        desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateTexture2D(&desc, NULL, &tex))) {
            tex = nullptr;
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
        view.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(tex, &view, &srv))) {
            srv = nullptr;
            release();
            return false;
        }
        w = width;
        h = height;
        generation = imguiHostDeviceGeneration();
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    for (int y = 0; y < height; y++) {
        memcpy((char*)mapped.pData + (size_t)y * mapped.RowPitch, rgba + (size_t)y * width, (size_t)width * 4);
    }
    context->Unmap(tex, 0);
    return true;
}
