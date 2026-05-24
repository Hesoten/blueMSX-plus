/*****************************************************************************
**
** Direct3D 12 video output (windowed / fullscreen, HDR, scanlines).
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
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string.h>
#include <stdio.h>

#include "Win32D3D12.h"
#include "FrameBuffer.h"
#include "AppConfig.h"

using Microsoft::WRL::ComPtr;

// --- constants ---------------------------------------------------------------
static const UINT  FRAME_COUNT = 2;
static const UINT  TEX_W       = 544;   // matches FB_MAX_LINE_WIDTH
static const UINT  TEX_H       = 480;   // matches FB_MAX_LINES
static const float NTSC_AR     = 1.151f;
static const float PAL_AR      = 1.151f * 60.0f / 50.0f;
// D3D9 used a 272x240 base texture; crop values are expressed in those units
static const int   BASE_TEX_W  = 272;
static const int   BASE_TEX_H  = 240;

// --- vertex ------------------------------------------------------------------
struct Vtx12 { float x, y, u, v; };

// --- HLSL (compiled at init time by D3DCompile) -------------------------------
static const char* g_vsHlsl = R"(
struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut main(VSIn v) {
    VSOut o;
    o.pos = float4(v.pos, 0.0f, 1.0f);
    o.uv  = v.uv;
    return o;
}
)";

static const char* g_psHlsl = R"(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
float4 main(PSIn p) : SV_TARGET {
    return tex.Sample(samp, p.uv);
}
)";

// --- D3D12 state -------------------------------------------------------------
static ComPtr<ID3D12Device>              g12_device;
static ComPtr<ID3D12CommandQueue>        g12_cmdQueue;
static ComPtr<IDXGISwapChain3>           g12_swapChain;

static ComPtr<ID3D12DescriptorHeap>      g12_rtvHeap;
static ComPtr<ID3D12DescriptorHeap>      g12_srvHeap;
static ComPtr<ID3D12DescriptorHeap>      g12_samplerHeap;
static UINT                              g12_rtvSize     = 0;
static UINT                              g12_samplerSize = 0;

static ComPtr<ID3D12Resource>            g12_rt[FRAME_COUNT];
static ComPtr<ID3D12CommandAllocator>    g12_cmdAlloc[FRAME_COUNT];
static ComPtr<ID3D12Fence>               g12_fence;
static HANDLE                            g12_fenceEvent  = NULL;
static UINT64                            g12_fenceVal[FRAME_COUNT] = {};
static UINT64                            g12_fenceCtr    = 0;

static ComPtr<ID3D12GraphicsCommandList> g12_cmdList;
static UINT                              g12_frameIndex  = 0;

static ComPtr<ID3D12RootSignature>       g12_rootSig;
static ComPtr<ID3D12PipelineState>       g12_pso;

// Texture (GPU default heap; lives in PIXEL_SHADER_RESOURCE between frames)
static ComPtr<ID3D12Resource>            g12_texture;

// Per-frame upload & vertex buffers (CPU upload heap, persistently mapped)
static ComPtr<ID3D12Resource>            g12_uploadBuf[FRAME_COUNT];
static UINT8*                            g12_uploadPtr[FRAME_COUNT] = {};
static UINT                              g12_uploadRowPitch = 0;

static ComPtr<ID3D12Resource>            g12_vtxBuf[FRAME_COUNT];
static UINT8*                            g12_vtxPtr[FRAME_COUNT] = {};
static D3D12_VERTEX_BUFFER_VIEW          g12_vtxView[FRAME_COUNT] = {};

static HWND g12_hwnd         = NULL;
static int  g12_w            = 0;
static int  g12_h            = 0;
static bool g12_ready        = false;
static bool g12_needCleanup  = false;
static int  g12_syncVblank   = -1;

// --- synchronization helpers -------------------------------------------------

static void WaitForFrame(UINT idx)
{
    if (g12_fence->GetCompletedValue() < g12_fenceVal[idx]) {
        g12_fence->SetEventOnCompletion(g12_fenceVal[idx], g12_fenceEvent);
        WaitForSingleObject(g12_fenceEvent, INFINITE);
    }
}

static void WaitForGpu()
{
    ++g12_fenceCtr;
    g12_cmdQueue->Signal(g12_fence.Get(), g12_fenceCtr);
    g12_fence->SetEventOnCompletion(g12_fenceCtr, g12_fenceEvent);
    WaitForSingleObject(g12_fenceEvent, INFINITE);
}

// --- cleanup -----------------------------------------------------------------

static void vD3D12Cleanup()
{
    if (!g12_ready && !g12_device) return;

    if (g12_device && g12_cmdQueue && g12_fence && g12_fenceEvent)
        WaitForGpu();

    for (UINT i = 0; i < FRAME_COUNT; i++) {
        g12_vtxBuf[i].Reset();    g12_vtxPtr[i]    = nullptr;
        g12_uploadBuf[i].Reset(); g12_uploadPtr[i] = nullptr;
        g12_rt[i].Reset();
        g12_cmdAlloc[i].Reset();
        g12_fenceVal[i] = 0;
    }

    g12_texture.Reset();
    g12_pso.Reset();
    g12_rootSig.Reset();
    g12_cmdList.Reset();
    g12_samplerHeap.Reset();
    g12_srvHeap.Reset();
    g12_rtvHeap.Reset();

    if (g12_fenceEvent) { CloseHandle(g12_fenceEvent); g12_fenceEvent = NULL; }
    g12_fence.Reset();
    g12_swapChain.Reset();
    g12_cmdQueue.Reset();
    g12_device.Reset();

    g12_fenceCtr    = 0;
    g12_frameIndex  = 0;
    g12_ready       = false;
    g12_needCleanup = false;
}

// --- initialization -----------------------------------------------------------

static bool bD3D12Init(HWND hWnd, int w, int h, int syncVblank)
{
    vD3D12Cleanup();
    HRESULT hr;

    // -- DXGI factory ---------------------------------------------------------
    ComPtr<IDXGIFactory4> factory;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    // -- Hardware device (first non-software adapter) --------------------------
    {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; }
            hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g12_device));
            if (SUCCEEDED(hr)) break;
            adapter.Reset();
        }
    }
    if (!g12_device) return false;

    // -- Command queue ---------------------------------------------------------
    {
        D3D12_COMMAND_QUEUE_DESC cqd = {};
        cqd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        cqd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        hr = g12_device->CreateCommandQueue(&cqd, IID_PPV_ARGS(&g12_cmdQueue));
        if (FAILED(hr)) return false;
    }

    // -- Swap chain ------------------------------------------------------------
    {
        DXGI_SWAP_CHAIN_DESC1 scd = {};
        scd.BufferCount  = FRAME_COUNT;
        scd.Width        = (UINT)w;
        scd.Height       = (UINT)h;
        scd.Format       = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> sc1;
        hr = factory->CreateSwapChainForHwnd(g12_cmdQueue.Get(), hWnd, &scd, nullptr, nullptr, &sc1);
        if (FAILED(hr)) return false;
        factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        sc1.As(&g12_swapChain);
        g12_frameIndex = g12_swapChain->GetCurrentBackBufferIndex();
    }

    // -- RTV heap -------------------------------------------------------------
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = FRAME_COUNT;
        d.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = g12_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g12_rtvHeap));
        if (FAILED(hr)) return false;
        g12_rtvSize = g12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // -- Render targets --------------------------------------------------------
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = g12_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < FRAME_COUNT; i++) {
            g12_swapChain->GetBuffer(i, IID_PPV_ARGS(&g12_rt[i]));
            g12_device->CreateRenderTargetView(g12_rt[i].Get(), nullptr, h);
            h.ptr += g12_rtvSize;
        }
    }

    // -- SRV heap (1 slot: the MSX texture) -----------------------------------
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = 1;
        d.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = g12_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g12_srvHeap));
        if (FAILED(hr)) return false;
    }

    // -- Sampler heap (2 slots: index 0 = point, index 1 = linear) ------------
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = 2;
        d.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = g12_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g12_samplerHeap));
        if (FAILED(hr)) return false;

        g12_samplerSize = g12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        D3D12_CPU_DESCRIPTOR_HANDLE sh = g12_samplerHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SAMPLER_DESC sd = {};
        sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        // border color black (default, float zeros)
        sd.MaxAnisotropy  = 1;
        sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sd.MinLOD         = 0.0f;
        sd.MaxLOD         = D3D12_FLOAT32_MAX;

        sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        g12_device->CreateSampler(&sd, sh);
        sh.ptr += g12_samplerSize;

        sd.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        g12_device->CreateSampler(&sd, sh);
    }

    // -- Command allocators ----------------------------------------------------
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        hr = g12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&g12_cmdAlloc[i]));
        if (FAILED(hr)) return false;
    }

    // -- Command list ----------------------------------------------------------
    hr = g12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       g12_cmdAlloc[0].Get(), nullptr,
                                       IID_PPV_ARGS(&g12_cmdList));
    if (FAILED(hr)) return false;
    g12_cmdList->Close();

    // -- Fence + event ---------------------------------------------------------
    hr = g12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g12_fence));
    if (FAILED(hr)) return false;
    g12_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!g12_fenceEvent) return false;

    // -- Root signature --------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE sampRange = {};
        sampRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampRange.NumDescriptors     = 1;
        sampRange.BaseShaderRegister = 0;
        sampRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges   = &srvRange;
        params[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &sampRange;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 2;
        rsd.pParameters   = params;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> sigBlob, errBlob;
        hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &sigBlob, &errBlob);
        if (FAILED(hr)) return false;
        hr = g12_device->CreateRootSignature(0,
                                              sigBlob->GetBufferPointer(),
                                              sigBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&g12_rootSig));
        if (FAILED(hr)) return false;
    }

    // -- Compile shaders -------------------------------------------------------
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    {
        hr = D3DCompile(g_vsHlsl, strlen(g_vsHlsl), "VS", nullptr, nullptr,
                        "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        &vsBlob, &errBlob);
        if (FAILED(hr)) {
#ifdef _DEBUG
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
#endif
            return false;
        }
        hr = D3DCompile(g_psHlsl, strlen(g_psHlsl), "PS", nullptr, nullptr,
                        "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        &psBlob, &errBlob);
        if (FAILED(hr)) {
#ifdef _DEBUG
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
#endif
            return false;
        }
    }

    // -- PSO -------------------------------------------------------------------
    {
        D3D12_INPUT_ELEMENT_DESC ied[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  8,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
        psd.pRootSignature        = g12_rootSig.Get();
        psd.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psd.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psd.InputLayout           = { ied, 2 };
        psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psd.NumRenderTargets      = 1;
        psd.RTVFormats[0]         = DXGI_FORMAT_B8G8R8A8_UNORM;
        psd.SampleDesc.Count      = 1;
        psd.SampleMask            = UINT_MAX;

        psd.BlendState.RenderTarget[0].BlendEnable        = FALSE;
        psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        psd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        psd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        psd.RasterizerState.DepthClipEnable = TRUE;

        psd.DepthStencilState.DepthEnable   = FALSE;
        psd.DepthStencilState.StencilEnable = FALSE;

        hr = g12_device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&g12_pso));
        if (FAILED(hr)) return false;
    }

    // -- GPU texture (544x480, BGRA8) ------------------------------------------
    {
        D3D12_RESOURCE_DESC td = {};
        td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width            = TEX_W;
        td.Height           = TEX_H;
        td.DepthOrArraySize = 1;
        td.MipLevels        = 1;
        td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        hr = g12_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                                  D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr, IID_PPV_ARGS(&g12_texture));
        if (FAILED(hr)) return false;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvd.Texture2D.MipLevels       = 1;
        g12_device->CreateShaderResourceView(g12_texture.Get(), &srvd,
                                              g12_srvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // -- Upload buffers --------------------------------------------------------
    {
        UINT rawPitch = TEX_W * 4;
        g12_uploadRowPitch = (rawPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                           & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        UINT uploadSize = g12_uploadRowPitch * TEX_H;

        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width            = uploadSize;
        bd.Height           = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels        = 1;
        bd.Format           = DXGI_FORMAT_UNKNOWN;
        bd.SampleDesc.Count = 1;
        bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RANGE readRange = { 0, 0 };

        for (UINT i = 0; i < FRAME_COUNT; i++) {
            hr = g12_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ,
                                                      nullptr, IID_PPV_ARGS(&g12_uploadBuf[i]));
            if (FAILED(hr)) return false;
            g12_uploadBuf[i]->Map(0, &readRange, (void**)&g12_uploadPtr[i]);
            // Initialize to black so unwritten edge pixels are not garbage
            memset(g12_uploadPtr[i], 0, uploadSize);
        }
    }

    // -- Vertex buffers --------------------------------------------------------
    {
        UINT vtxSize = sizeof(Vtx12) * 4;

        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width            = vtxSize;
        bd.Height           = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels        = 1;
        bd.Format           = DXGI_FORMAT_UNKNOWN;
        bd.SampleDesc.Count = 1;
        bd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RANGE readRange = { 0, 0 };

        for (UINT i = 0; i < FRAME_COUNT; i++) {
            hr = g12_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ,
                                                      nullptr, IID_PPV_ARGS(&g12_vtxBuf[i]));
            if (FAILED(hr)) return false;
            g12_vtxBuf[i]->Map(0, &readRange, (void**)&g12_vtxPtr[i]);

            g12_vtxView[i].BufferLocation = g12_vtxBuf[i]->GetGPUVirtualAddress();
            g12_vtxView[i].SizeInBytes    = vtxSize;
            g12_vtxView[i].StrideInBytes  = sizeof(Vtx12);
        }
    }

    // -- Transition texture to PIXEL_SHADER_RESOURCE ---------------------------
    {
        g12_cmdAlloc[0]->Reset();
        g12_cmdList->Reset(g12_cmdAlloc[0].Get(), nullptr);

        D3D12_RESOURCE_BARRIER rb = {};
        rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb.Transition.pResource   = g12_texture.Get();
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        g12_cmdList->ResourceBarrier(1, &rb);
        g12_cmdList->Close();

        ID3D12CommandList* lists[] = { g12_cmdList.Get() };
        g12_cmdQueue->ExecuteCommandLists(1, lists);
        WaitForGpu();
    }

    g12_hwnd       = hWnd;
    g12_w          = w;
    g12_h          = h;
    g12_syncVblank = syncVblank;
    g12_ready      = true;
    return true;
}

// --- swap chain resize --------------------------------------------------------

static void vD3D12Resize(int w, int h)
{
    if (!g12_ready || (w == g12_w && h == g12_h)) return;

    WaitForGpu();

    for (UINT i = 0; i < FRAME_COUNT; i++) g12_rt[i].Reset();

    g12_swapChain->ResizeBuffers(FRAME_COUNT, (UINT)w, (UINT)h,
                                  DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    g12_frameIndex = g12_swapChain->GetCurrentBackBufferIndex();

    D3D12_CPU_DESCRIPTOR_HANDLE rtvH = g12_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        g12_swapChain->GetBuffer(i, IID_PPV_ARGS(&g12_rt[i]));
        g12_device->CreateRenderTargetView(g12_rt[i].Get(), nullptr, rtvH);
        rtvH.ptr += g12_rtvSize;
    }

    g12_w = w;
    g12_h = h;
}

// --- main render entry point --------------------------------------------------

int D3D12UpdateSurface(HWND hWnd, Video* pVideo, int syncVblank, D3DProperties* props)
{
    /* No cleanup on syncVblank change: device setup doesn't depend on
    ** it, and recreating g12_swapHwnd (every menu/dialog open flips
    ** syncVblank) shows up as a black flash. */
    if (g12_needCleanup)
        vD3D12Cleanup();

    RECT sr;
    GetWindowRect(hWnd, &sr);
    int w = sr.right  - sr.left;
    int h = sr.bottom - sr.top;
    if (w <= 0 || h <= 0) return 0;

    if (!g12_ready) {
        if (!bD3D12Init(hWnd, w, h, syncVblank)) {
            g12_needCleanup = true;
            return 0;
        }
    }

    /* Track the latest syncVblank for callers / introspection. The mix-mode
    ** is read from the call argument (line below), not this cached value. */
    g12_syncVblank = syncVblank;

    if (w != g12_w || h != g12_h)
        vD3D12Resize(w, h);

    // -- Wait for previous use of this frame slot ------------------------------
    WaitForFrame(g12_frameIndex);
    g12_cmdAlloc[g12_frameIndex]->Reset();
    g12_cmdList->Reset(g12_cmdAlloc[g12_frameIndex].Get(), g12_pso.Get());

    // -- Acquire framebuffer ---------------------------------------------------
    FrameBuffer* fb = frameBufferFlipViewFrame(syncVblank);
    if (!fb) fb = frameBufferGetWhiteNoiseFrame();
    if (fb->interlace != INTERLACE_NONE && pVideo->deInterlace)
        fb = frameBufferDeinterlace(fb);

    // -- ARGB1555 -> BGRA8 -> upload buffer -------------------------------------
    {
        int lines     = fb->lines;
        int startLine = (fb->interlace == INTERLACE_ODD) ? 1 : 0;

        for (int y = 0; y < lines; y++) {
            UINT8*        dstRow = g12_uploadPtr[g12_frameIndex]
                                 + (UINT)((y + startLine) * (int)g12_uploadRowPitch);
            const UINT16* srcRow = fb->line[y].buffer;
            int srcW = fb->maxWidth;
            if (fb->line[y].doubleWidth) srcW *= 2;

            UINT32* dst = (UINT32*)dstRow;
            for (int x = 0; x < srcW; x++) {
                UINT16 p = srcRow[x];
                UINT32 r = (p >> 10) & 0x1F; r = (r << 3) | (r >> 2);
                UINT32 g = (p >>  5) & 0x1F; g = (g << 3) | (g >> 2);
                UINT32 b = (p >>  0) & 0x1F; b = (b << 3) | (b >> 2);
                // DXGI_FORMAT_B8G8R8A8_UNORM: memory [B,G,R,A]
                // as uint32 LE: A<<24 | R<<16 | G<<8 | B
                dst[x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    // -- Upload -> GPU texture --------------------------------------------------
    {
        D3D12_RESOURCE_BARRIER rb = {};
        rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb.Transition.pResource   = g12_texture.Get();
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        g12_cmdList->ResourceBarrier(1, &rb);

        D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
        dst_loc.pResource        = g12_texture.Get();
        dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src_loc = {};
        src_loc.pResource                            = g12_uploadBuf[g12_frameIndex].Get();
        src_loc.Type                                 = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint.Offset               = 0;
        src_loc.PlacedFootprint.Footprint.Format     = DXGI_FORMAT_B8G8R8A8_UNORM;
        src_loc.PlacedFootprint.Footprint.Width      = TEX_W;
        src_loc.PlacedFootprint.Footprint.Height     = TEX_H;
        src_loc.PlacedFootprint.Footprint.Depth      = 1;
        src_loc.PlacedFootprint.Footprint.RowPitch   = g12_uploadRowPitch;

        g12_cmdList->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

        rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        g12_cmdList->ResourceBarrier(1, &rb);
    }

    // -- UV / vertex calculation (same logic as D3D9 D3DUpdateSurface) ---------
    {
        int iBorderLeft = 0, iBorderTop = 0, iBorderRight = 0, iBorderBottom = 0;
        switch (props->cropType) {
        case P_D3D_CROP_SIZE_MSX1:
            iBorderLeft = iBorderRight  = (BASE_TEX_W - 256) / 2;
            iBorderTop  = iBorderBottom = (BASE_TEX_H - 192) / 2;
            break;
        case P_D3D_CROP_SIZE_MSX1_PLUS_8:
            iBorderLeft = iBorderRight  = (BASE_TEX_W - 256 - 16) / 2;
            iBorderTop  = iBorderBottom = (BASE_TEX_H - 192 - 16) / 2;
            break;
        case P_D3D_CROP_SIZE_MSX2:
            iBorderLeft = iBorderRight  = (BASE_TEX_W - 256) / 2;
            iBorderTop  = iBorderBottom = (BASE_TEX_H - 212) / 2;
            break;
        case P_D3D_CROP_SIZE_MSX2_PLUS_8:
            iBorderLeft = iBorderRight  = (BASE_TEX_W - 256 - 16) / 2;
            iBorderTop  = iBorderBottom = (BASE_TEX_H - 212 - 16) / 2;
            break;
        case P_D3D_CROP_SIZE_CUSTOM:
            iBorderLeft   = props->cropLeft;
            iBorderRight  = props->cropRight;
            iBorderTop    = props->cropTop;
            iBorderBottom = props->cropBottom;
            break;
        }

        // Actual texture pixel width; doubled in doubleWidth modes
        // (SCREEN7, SCREEN0 MODE80).
        int srcW = fb->maxWidth;
        for (int y = 0; y < fb->lines; y++) {
            if (fb->line[y].doubleWidth) { srcW *= 2; break; }
        }
        int srcH = fb->lines;

        float fTexW = (float)TEX_W;
        float fTexH = (float)TEX_H;
        float fWs   = (float)w;
        float fHs   = (float)h;

        // AR formula uses logical width BASE_TEX_W (272), not srcW:
        // doubleWidth (544) would double fHm and halve the image height.
        float fMSXW = (float)(BASE_TEX_W - iBorderLeft - iBorderRight);
        float fMSXH = (float)(srcH  - iBorderTop  - iBorderBottom);

        float fAR;
        switch (props->aspectRatioType) {
        case P_D3D_AR_NTSC:    fAR = NTSC_AR; break;
        case P_D3D_AR_PAL:     fAR = PAL_AR;  break;
        case P_D3D_AR_1:       fAR = 1.0f;    break;
        case P_D3D_AR_AUTO:    fAR = (vdpGetRefreshRate() == 60) ? NTSC_AR : PAL_AR; break;
        case P_D3D_AR_STRETCH: fAR = (fMSXH * fWs) / (fMSXW * fHs); break;
        default:               fAR = NTSC_AR; break;
        }

        float fWm = fMSXW;
        float fHm = fMSXH;
        if (fWm * fHs * fAR > fWs * fHm)
            fHm = fHs * fWm * fAR / fWs;
        else
            fWm = fWs * fHm / (fHs * fAR);

        // Convert from logical-pixel space to UV fraction.
        // fWm is in units of BASE_TEX_W logical pixels; scale to the actual
        // data extent within the 544-wide texture (srcW/fTexW).
        float wUvScale = (float)srcW / ((float)BASE_TEX_W * fTexW);
        fWm *= wUvScale;
        fHm /= fTexH;

        // Crop asymmetry offset in the same UV units.
        // iBorderLeft/Right are in logical pixels (BASE_TEX_W reference), same
        // unit as fMSXW, so multiply by the same wUvScale to get UV units.
        float fDx = (float)(iBorderLeft - iBorderRight) * 0.5f * wUvScale;
        float fDy = (float)(iBorderTop  - iBorderBottom) * 0.5f / fTexH;

        // Center of the valid data region in UV space
        float uCtr = (float)srcW * 0.5f / fTexW;
        float vCtr = (float)srcH * 0.5f / fTexH;

        float u0 = uCtr - fWm * 0.5f + fDx;
        float u1 = uCtr + fWm * 0.5f + fDx;
        float v0 = vCtr - fHm * 0.5f + fDy;
        float v1 = vCtr + fHm * 0.5f + fDy;

        Vtx12 verts[4] = {
            { -1.0f, +1.0f, u0, v0 },
            { +1.0f, +1.0f, u1, v0 },
            { -1.0f, -1.0f, u0, v1 },
            { +1.0f, -1.0f, u1, v1 },
        };
        memcpy(g12_vtxPtr[g12_frameIndex], verts, sizeof(verts));
    }

    // -- Render pass -----------------------------------------------------------
    {
        D3D12_RESOURCE_BARRIER rb = {};
        rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb.Transition.pResource   = g12_rt[g12_frameIndex].Get();
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g12_cmdList->ResourceBarrier(1, &rb);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvH = g12_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvH.ptr += (SIZE_T)g12_frameIndex * g12_rtvSize;

        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        g12_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
        g12_cmdList->OMSetRenderTargets(1, &rtvH, FALSE, nullptr);

        D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        D3D12_RECT     scissor = { 0, 0, (LONG)w, (LONG)h };
        g12_cmdList->RSSetViewports(1, &vp);
        g12_cmdList->RSSetScissorRects(1, &scissor);

        g12_cmdList->SetPipelineState(g12_pso.Get());
        g12_cmdList->SetGraphicsRootSignature(g12_rootSig.Get());

        ID3D12DescriptorHeap* heaps[] = { g12_srvHeap.Get(), g12_samplerHeap.Get() };
        g12_cmdList->SetDescriptorHeaps(2, heaps);

        g12_cmdList->SetGraphicsRootDescriptorTable(0,
            g12_srvHeap->GetGPUDescriptorHandleForHeapStart());

        // Select sampler: index 0 = point, index 1 = linear
        D3D12_GPU_DESCRIPTOR_HANDLE sampH = g12_samplerHeap->GetGPUDescriptorHandleForHeapStart();
        if (props->linearFiltering)
            sampH.ptr += g12_samplerSize;
        g12_cmdList->SetGraphicsRootDescriptorTable(1, sampH);

        g12_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g12_cmdList->IASetVertexBuffers(0, 1, &g12_vtxView[g12_frameIndex]);
        g12_cmdList->DrawInstanced(4, 1, 0, 0);

        rb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        g12_cmdList->ResourceBarrier(1, &rb);
    }

    g12_cmdList->Close();

    // -- Submit & present ------------------------------------------------------
    {
        ID3D12CommandList* lists[] = { g12_cmdList.Get() };
        g12_cmdQueue->ExecuteCommandLists(1, lists);
    }

    // Present(1, 0): wait for 1 VBlank (vsync).  No separate syncVblank=1
    // (manual raster check) path -- Present(1) covers both sync modes.
    g12_swapChain->Present(1, 0);

    // Signal fence for this frame slot
    ++g12_fenceCtr;
    g12_fenceVal[g12_frameIndex] = g12_fenceCtr;
    g12_cmdQueue->Signal(g12_fence.Get(), g12_fenceCtr);

    g12_frameIndex = g12_swapChain->GetCurrentBackBufferIndex();

    if (!syncVblank)
        archPollInput();

    return 1;
}

// --- windowing helpers (match D3D9 interface) ---------------------------------

// Defined in Win32D3D.cpp: finds the monitor containing the window and
// resizes parent+child to cover it (borderless windowed fullscreen).
extern void vSetFullscreen(HWND hWnd);

void D3D12ExitFullscreenMode()
{
    // Tear down now: caller may switch driver, in which case no further
    // UpdateSurface fires to consume a deferred g12_needCleanup and the
    // swap chain would stay bound to the HWND.
    vD3D12Cleanup();
}

extern "C" int D3D12EnsureReady(HWND hwnd, int syncVblank)
{
    /* See D3D12UpdateSurface: syncVblank change alone must not tear down
    ** the device (causes black flashes on menu / dialog open/close). */
    if (g12_needCleanup) vD3D12Cleanup();

    if (g12_ready) {
        g12_syncVblank = syncVblank;
        return 1;
    }

    RECT sr;
    GetWindowRect(hwnd, &sr);
    int w = sr.right  - sr.left;
    int h = sr.bottom - sr.top;
    if (w <= 0 || h <= 0) return 0;

    if (!bD3D12Init(hwnd, w, h, syncVblank)) {
        g12_needCleanup = true;
        return 0;
    }
    return 1;
}

int D3D12EnterFullscreenMode(HWND hwnd, int /*useVideoBackBuffer*/, int /*useSysMemBuffering*/)
{
    // Resize only; the swap chain auto-resizes in D3D12UpdateSurface.
    // Forcing g12_needCleanup per mode flip crashed the AMD driver (CFF AV).
    vSetFullscreen(hwnd);
    return 0; // DXE_OK
}

BOOL D3D12EnterWindowedMode(HWND /*hwnd*/, int /*width*/, int /*height*/,
                             int /*useVideoBackBuffer*/, int /*useSysMemBuffering*/)
{
    // No-op: windowed transitions are handled entirely by archUpdateWindow's
    // SetWindowPos in themeSet plus D3D12UpdateSurface's auto-resize.
    return 0; // DXE_OK
}

int D3D12UpdateWindowedMode(HWND /*hwnd*/, int width, int height,
                             int /*useVideoBackBuffer*/, int /*useSysMemBuffering*/)
{
    vD3D12Resize(width, height);
    return 0;
}
