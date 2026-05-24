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

extern "C" {
#include "VideoRender.h"
}

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

// PS order: PAL filter (DD palMode formula) -> colorSaturation -> blend ->
// scanlines (source-UV sinusoid) -> grading -> monitor color.
// cbuffer flags are float (0/1) to avoid HLSL int-packing.
static const char* g_psHlsl = R"(
Texture2D            texCur   : register(t0);
Texture2D            texPrev  : register(t1);
Texture2D<float>     texNoise : register(t2);   // R8 mask (top-3-bit DD PRNG / 255)
SamplerState         samp     : register(s0);

cbuffer EffectParams : register(b0) {
    float scanLinesEnable;
    float scanLinesIntensity;
    float scanUvPeriod;
    float blendFramesEnable;
    float gammaExp;
    float brightness;
    float contrast;
    float saturation;
    float colorSatEnable;
    float colorSatWidth;
    float monitorColor;
    float palMode;              // 0=FAST 1=MONITOR 2=SHARP 3=SHARP_NOISE 4=BLUR 5=BLUR_NOISE
    float4 borderColor;
    float2 srcUvMin;
    float2 srcUvMax;
    float srcWf;                // = (float)srcW (= maxWidth, or 2*maxWidth if doubleWidth)
    float srcHf;                // = (float)fb->lines
    float texWf;                // = (float)TEX_W (texture width)
    float texHf;                // = (float)TEX_H (texture height)
    float doubleWidthFlag;      // 0=non-doubleWidth (1 src->2 dst), 1=doubleWidth (1 src->1 dst)
    float pad1, pad2, pad3;
};

struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

// Matches DD's `& 0xf0f0f0` mask in BLUR/BLUR_NOISE (low-4 dropped twice).
// Without this float output is ~7.5/255 brighter than DD.
float3 quantize16(float3 c) {
    int3 c8 = int3(saturate(c) * 255.0 + 0.5);
    c8 &= 0xF0;
    return float3(c8) * (1.0 / 255.0);
}

// Matches DD's `& 0xfcfcfc` mask in SHARP/MONITOR (low-2 dropped twice;
// ~1.5/255 darkening — visible vs DD in side-by-side).
float3 quantize4(float3 c) {
    int3 c8 = int3(saturate(c) * 255.0 + 0.5);
    c8 &= 0xFC;
    return float3(c8) * (1.0 / 255.0);
}

// DD copy*PAL_2x2_32 has two paths: non-doubleWidth (1 src -> 2 dst,
// epsilon picks cross-blend weights) and doubleWidth (1:1, parity-keyed).
// Boot case (DD inits colPrev=colCur=src[0]) is covered by max(0,p-N).
float3 evalPalFilter(int p, int sy, int epsilon, int srcW) {
    int pmax = srcW - 1;
    p = clamp(p, 0, pmax);
    float3 src_p = texCur.Load(int3(p, sy, 0)).rgb;

    if (palMode < 0.5) {                       // FAST
        return src_p;
    }

    if (doubleWidthFlag > 0.5) {
        // doubleWidth: ignore epsilon, key off output position parity.
        int p1 = max(0, p - 1);
        int p2 = max(0, p - 2);
        float3 s1 = texCur.Load(int3(p1, sy, 0)).rgb;
        float3 s2 = texCur.Load(int3(p2, sy, 0)).rgb;

        if (palMode < 1.5) {                   // MONITOR  (dw)
            // even k: (src[k-2] + 3*src[k-1])/4
            // odd  k: (3*src[k-1] + src[k])/4
            // quantize4 to match DD's `& 0xfcfcfc` on inputs and result.
            float3 a0 = quantize4(src_p);
            float3 a1 = quantize4(s1);
            float3 a2 = quantize4(s2);
            float3 sum = ((p & 1) == 0) ? (a2 * 0.25 + a1 * 0.75)
                                        : (a1 * 0.75 + a0 * 0.25);
            return quantize4(sum);
        }
        if (palMode < 3.5) {                   // SHARP / SHARP_NOISE (dw)
            // 3-tap symmetric: (src[k-2] + 2*src[k-1] + src[k]) / 4
            float3 a0 = quantize4(src_p);
            float3 a1 = quantize4(s1);
            float3 a2 = quantize4(s2);
            float3 sum = (a2 + a1 * 2.0 + a0) * 0.25;
            return quantize4(sum);
        }
        // BLUR/BLUR_NOISE (dw): 5-tap /16, even=1,4,8,2,1 odd=1,2,8,4,1.
        // quantize16 on inputs+result matches DD's `& 0xf0f0f0`.
        int p3 = max(0, p - 3);
        int p4 = max(0, p - 4);
        float3 s3 = texCur.Load(int3(p3, sy, 0)).rgb;
        float3 s4 = texCur.Load(int3(p4, sy, 0)).rgb;
        float3 a0 = quantize16(src_p);
        float3 a1 = quantize16(s1);
        float3 a2 = quantize16(s2);
        float3 a3 = quantize16(s3);
        float3 a4 = quantize16(s4);
        float3 sum = ((p & 1) == 0)
            ? ((a4 + a3 * 4.0 + a2 * 8.0 + a1 * 2.0 + a0) / 16.0)
            : ((a4 + a3 * 2.0 + a2 * 8.0 + a1 * 4.0 + a0) / 16.0);
        return quantize16(sum);
    }

    // Non-doubleWidth path
    if (p <= 0) {
        // Boot column: DD collapses to quantized src[0].  Raw src_p would
        // leave the leftmost 1-2 columns off-colour vs DD.
        return (palMode > 3.5) ? quantize16(src_p) : quantize4(src_p);
    }
    float3 src_pm1 = texCur.Load(int3(p - 1, sy, 0)).rgb;

    if (palMode < 1.5) {                       // MONITOR
        // even col: (3*src[p-1] + src[p])/4 ; odd col: src[p]
        // quantize4 matches DD's `& 0xfcfcfc` on inputs and result.
        float3 a0 = quantize4(src_p);
        float3 a1 = quantize4(src_pm1);
        float3 sum = (epsilon == 0) ? (a1 * 0.75 + a0 * 0.25) : a0;
        return quantize4(sum);
    }
    if (palMode < 3.5) {                       // SHARP / SHARP_NOISE
        // even col: 0.75*src[p-1] + 0.25*src[p]
        // odd  col: 0.25*src[p-1] + 0.75*src[p]
        float3 a0 = quantize4(src_p);
        float3 a1 = quantize4(src_pm1);
        float3 sum = (epsilon == 0) ? (a1 * 0.75 + a0 * 0.25)
                                    : (a1 * 0.25 + a0 * 0.75);
        return quantize4(sum);
    }

    // BLUR / BLUR_NOISE -- 3-tap (prev2, prev1, cur) with weights 6/8/2 (even)
    // or 2/8/6 (odd), all /16.  Match DD's `& 0xf0f0f0` quantization on
    // inputs and result (otherwise float output is ~3% brighter than DD).
    int p2 = max(0, p - 2);
    float3 src_pm2 = texCur.Load(int3(p2, sy, 0)).rgb;
    float3 a0 = quantize16(src_p);
    float3 a1 = quantize16(src_pm1);
    float3 a2 = quantize16(src_pm2);
    float3 sum = (epsilon == 0) ? (a2 * 0.375 + a1 * 0.5 + a0 * 0.125)
                                : (a2 * 0.125 + a1 * 0.5 + a0 * 0.375);
    return quantize16(sum);
}

float4 main(PSIn p) : SV_TARGET {
    // Outside emu rect: source = borderColor (downstream effects still
    // run).  `>=` (not `>`) matches DD's clearBorders zero at dst[2*srcW].
    bool outOfBounds = any(p.uv < srcUvMin) || any(p.uv >= srcUvMax);

    float4 color;
    if (outOfBounds) {
        color = borderColor;
        /* Apply the same DD bit-truncation the emu path runs through
           evalPalFilter, otherwise borderColor channels not aligned to
           4 (SHARP/MONITOR) or 16 (BLUR) leave a faint colour step at
           the boundary. */
        if (palMode > 3.5) {           // BLUR / BLUR_NOISE
            color.rgb = quantize16(color.rgb);
        } else if (palMode > 0.5) {    // MONITOR / SHARP / SHARP_NOISE
            color.rgb = quantize4(color.rgb);
        }
        /* *_NOISE: stamp the per-row noise pattern over the border to match
           the emu area's grain (non-NOISE modes reduce to identity). */
        bool isNoiseMode = (palMode > 2.5 && palMode < 3.5)
                        || palMode > 4.5;
        if (isNoiseMode) {
            int srcW = (int)srcWf;
            int srcH = (int)srcHf;
            float r = p.uv.y * texHf;
            float c = p.uv.x * texWf;
            int dd_row_2x = (int)floor(r * 2.0);
            int sy        = clamp(dd_row_2x / 2, 0, srcH - 1);
            bool isNoiseRow = (dd_row_2x & 1) == 1;
            int dd_col_2x = (int)floor(c * 2.0);
            int idx = (doubleWidthFlag > 0.5) ? (int)floor(c) : (dd_col_2x / 2);
            /* Wrap (don't clamp) so border continues the per-column noise
               variation instead of repeating one column's value across
               every border pixel (which looked like strong row-banding). */
            int pn = idx % srcW;
            if (pn < 0) pn += srcW;
            if (isNoiseRow) {
                float n = texNoise.Load(int3(pn, sy, 0));
                color.rgb += float3(n, n, n);
            } else {
                int prev_sy = (sy > 0) ? (sy - 1) : sy;
                float n_prev = texNoise.Load(int3(pn, prev_sy, 0));
                float prevW = (palMode > 0.5 && palMode < 1.5) ? 0.25 : 0.5;
                color.rgb += prevW * float3(n_prev, n_prev, n_prev);
            }
        }
    } else if (palMode < 0.5) {
        // FAST mode: the trivial sampler path stays the same as the
        // pre-PAL baseline so non-PAL users see no behavioural change.
        color = texCur.Sample(samp, p.uv);
    } else {
        // Map UV onto DD's zoom=2 surface: non-dw = 1 src -> 2 dst
        // (epsilon 0/1), dw = 1:1 (epsilon unused).
        int srcW = (int)srcWf;
        int srcH = (int)srcHf;
        float c = p.uv.x * texWf;
        float r = p.uv.y * texHf;
        int dd_row_2x = (int)floor(r * 2.0);
        int sy        = clamp(dd_row_2x / 2, 0, srcH - 1);
        bool isNoiseRow = (dd_row_2x & 1) == 1;

        int p_for, epsilon;
        if (doubleWidthFlag > 0.5) {
            p_for   = (int)floor(c);   // 1:1 src->dst
            epsilon = 0;
        } else {
            int dd_col_2x = (int)floor(c * 2.0);
            p_for   = dd_col_2x / 2;
            epsilon = dd_col_2x & 1;
        }

        float3 colRgb = evalPalFilter(p_for, sy, epsilon, srcW);

        bool isNoiseMode = (palMode > 2.5 && palMode < 3.5)   // SHARP_NOISE
                        || palMode > 4.5;                      // BLUR_NOISE
        int  pn         = clamp(p_for, 0, srcW - 1);

        float3 result;
        if (isNoiseRow) {
            result = colRgb;
            if (isNoiseMode) {
                float n = texNoise.Load(int3(pn, sy, 0));
                result += float3(n, n, n);
            }
        } else {
            // Blend row: DD does (prev + cur)/2 once (SHARP/BLUR) or twice
            // (MONITOR); at sy==0 prev = the same row's noise output.
            int prev_sy = (sy > 0) ? (sy - 1) : sy;
            float3 prevColRgb = evalPalFilter(p_for, prev_sy, epsilon, srcW);
            if (isNoiseMode) {
                float n = texNoise.Load(int3(pn, prev_sy, 0));
                prevColRgb += float3(n, n, n);
            }
            float prevW = (palMode > 0.5 && palMode < 1.5) ? 0.25 : 0.5; // MONITOR vs others
            result = lerp(colRgb, prevColRgb, prevW);
        }
        color = float4(result, 1.0);
    }

    /* blendFrames / colorSat sample the textures, which hold 0 (not
       borderColor) outside the active rect, so gate them on !outOfBounds. */
    if (!outOfBounds && blendFramesEnable > 0.5) {
        float4 prev = texPrev.Sample(samp, p.uv);
        color.rgb = (color.rgb + prev.rgb) * 0.5;
    }

    if (!outOfBounds && colorSatEnable > 0.5) {
        float3 cl = texCur.Sample(samp, float2(p.uv.x - colorSatWidth, p.uv.y)).rgb;
        color.rgb = (cl + 3.0 * color.rgb) * 0.25;
    }

    if (scanLinesEnable > 0.5) {
        float srcRow = p.uv.y / scanUvPeriod;
        float beam   = sin(frac(srcRow) * 3.14159265);
        color.rgb *= lerp(scanLinesIntensity, 1.0, beam);
    }

    color.rgb = pow(saturate(color.rgb), float3(gammaExp, gammaExp, gammaExp));
    color.rgb = (color.rgb - 0.5) * contrast + 0.5 + brightness;
    float lum = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = lerp(float3(lum, lum, lum), color.rgb, saturation);

    // Properties.h: P_VIDEO_COLOR=0, P_VIDEO_BW=1, P_VIDEO_GREEN=2, P_VIDEO_AMBER=3
    if (monitorColor > 0.5) {
        lum = dot(color.rgb, float3(0.299, 0.587, 0.114));
        if      (monitorColor < 1.5) color.rgb = float3(lum, lum,        lum); // BW
        else if (monitorColor < 2.5) color.rgb = float3(0.0, lum,        0.0); // GREEN
        else                          color.rgb = float3(lum, lum * 0.69, 0.0); // AMBER
    }

    return float4(saturate(color.rgb), 1.0);
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
// Previous-frame copy used by the blendFrames effect (same size/format as g12_texture)
static ComPtr<ID3D12Resource>            g12_prevTexture;
static bool                              g12_prevTextureValid = false;
// Noise mask for SHARP_NOISE / BLUR_NOISE: R8 texture sized like g12_texture,
// each texel holds the top-3-bit DD PRNG value (= same noise that
// videoRender's copy*PAL_2x2_32 adds at that source pixel position).
static ComPtr<ID3D12Resource>            g12_noiseTexture;

// Per-frame upload & vertex buffers (CPU upload heap, persistently mapped)
static ComPtr<ID3D12Resource>            g12_uploadBuf[FRAME_COUNT];
static UINT8*                            g12_uploadPtr[FRAME_COUNT] = {};
static UINT                              g12_uploadRowPitch = 0;
// Noise mask upload buffers (one per frame slot, R8 packed)
static ComPtr<ID3D12Resource>            g12_noiseUploadBuf[FRAME_COUNT];
static UINT8*                            g12_noiseUploadPtr[FRAME_COUNT] = {};
static UINT                              g12_noiseUploadRowPitch = 0;

static ComPtr<ID3D12Resource>            g12_vtxBuf[FRAME_COUNT];
static UINT8*                            g12_vtxPtr[FRAME_COUNT] = {};
static D3D12_VERTEX_BUFFER_VIEW          g12_vtxView[FRAME_COUNT] = {};

// Per-frame constant buffer -- all float to match HLSL cbuffer (avoids int-packing issues)
struct EffectCB12 {
    float scanLinesEnable;
    float scanLinesIntensity;
    float scanUvPeriod;
    float blendFramesEnable;
    float gammaExp;
    float brightness;
    float contrast;
    float saturation;
    float colorSatEnable;
    float colorSatWidth;
    float monitorColor;        // 0=normal 1=green 2=amber 3=B&W
    float palMode;             // 0=FAST 1=MONITOR 2=SHARP 3=SHARP_NOISE 4=BLUR 5=BLUR_NOISE
    float borderR, borderG, borderB, borderA;
    float srcUvMinU, srcUvMinV;
    float srcUvMaxU, srcUvMaxV;
    float srcWf;               // = (float)srcW (= maxWidth, or 2*maxWidth if doubleWidth)
    float srcHf;               // = (float)fb->lines
    float texWf;               // = (float)TEX_W
    float texHf;               // = (float)TEX_H
    float doubleWidthFlag;     // 1.0 if any line in this frame has doubleWidth set
    float pad1, pad2, pad3;
};
static const UINT CB_SIZE = (sizeof(EffectCB12) + 255) & ~255u; // 256-byte aligned
static ComPtr<ID3D12Resource>            g12_cbuf[FRAME_COUNT];
static UINT8*                            g12_cbufPtr[FRAME_COUNT] = {};

static HWND g12_hwnd         = NULL;  // parent (emu) HWND passed by caller
static HWND g12_swapHwnd     = NULL;  // child of g12_hwnd; owns the DXGI swap chain
static int  g12_w            = 0;
static int  g12_h            = 0;
static bool g12_ready        = false;
static bool g12_needCleanup  = false;
static int  g12_syncVblank   = -1;

// Child window hosts the DXGI swap chain: presenting on the caller's HWND
// would leave a DWM redirection surface that blocks DDraw/GDI later.
static const char* k_d3d12SwapWndClass = "blueMSXD3D12Swap";
static bool g12_swapWndClassRegistered = false;

static void registerSwapWindowClass()
{
    if (g12_swapWndClassRegistered) return;
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = k_d3d12SwapWndClass;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);
    g12_swapWndClassRegistered = true;
}

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
        g12_noiseUploadBuf[i].Reset(); g12_noiseUploadPtr[i] = nullptr;
        g12_cbuf[i].Reset();      g12_cbufPtr[i]   = nullptr;
        g12_rt[i].Reset();
        g12_cmdAlloc[i].Reset();
        g12_fenceVal[i] = 0;
    }

    g12_prevTexture.Reset();
    g12_prevTextureValid = false;
    g12_noiseTexture.Reset();
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

    // DestroyWindow AFTER swap-chain release drops the FLIP redirection
    // surface; releasing the swap chain alone leaves it on the HWND.
    if (g12_swapHwnd) {
        DestroyWindow(g12_swapHwnd);
        g12_swapHwnd = NULL;
    }

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

    // -- Swap-chain host window -----------------------------------------------
    // Throwaway WS_CHILD covering caller's client area; swap chain attaches
    // to the child so DestroyWindow can drop the DWM redirection later.
    {
        registerSwapWindowClass();
        RECT rc = {};
        GetClientRect(hWnd, &rc);
        int cw = rc.right  - rc.left;
        int ch = rc.bottom - rc.top;
        if (cw <= 0) cw = w;
        if (ch <= 0) ch = h;
        g12_swapHwnd = CreateWindowExA(
            0,
            k_d3d12SwapWndClass, "",
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
            0, 0, cw, ch,
            hWnd, NULL, GetModuleHandle(NULL), NULL);
        if (!g12_swapHwnd) return false;
    }

    // -- Swap chain (attached to the child HWND, never the caller's) ---------
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
        hr = factory->CreateSwapChainForHwnd(g12_cmdQueue.Get(), g12_swapHwnd, &scd, nullptr, nullptr, &sc1);
        if (FAILED(hr)) return false;
        factory->MakeWindowAssociation(g12_swapHwnd, DXGI_MWA_NO_ALT_ENTER);
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

    // -- SRV heap (3 slots: 0 = current frame, 1 = previous frame, 2 = noise) -
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = 3;
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
    // Layout:
    //   param 0 : descriptor table -> 3 SRVs (t0 cur, t1 prev, t2 noise mask)
    //   param 1 : descriptor table -> 1 Sampler (s0)
    //   param 2 : root CBV          -> b0 effect parameters
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = 3;   // t0 cur, t1 prev, t2 noise mask
        srvRange.BaseShaderRegister = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE sampRange = {};
        sampRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampRange.NumDescriptors     = 1;
        sampRange.BaseShaderRegister = 0;
        sampRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges   = &srvRange;
        params[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &sampRange;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[2].Descriptor.ShaderRegister = 0;
        params[2].Descriptor.RegisterSpace  = 0;
        params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 3;
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

    // -- Previous-frame texture (for blendFrames effect) -----------------------
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

        // Start in COPY_DEST (we may initialize it first, or leave it until first copy).
        // After initial transition block below it will live in PIXEL_SHADER_RESOURCE.
        hr = g12_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                                  D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr, IID_PPV_ARGS(&g12_prevTexture));
        if (FAILED(hr)) return false;

        UINT srvStride = g12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE h = g12_srvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += srvStride; // slot 1 = previous frame

        D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvd.Texture2D.MipLevels       = 1;
        g12_device->CreateShaderResourceView(g12_prevTexture.Get(), &srvd, h);
        g12_prevTextureValid = false;
    }

    // -- Noise texture (R8 mask, used by SHARP_NOISE / BLUR_NOISE) ------------
    // Same TEX_W x TEX_H as colour, holds DD PRNG top-3-bits per (p,sy).
    {
        D3D12_RESOURCE_DESC td = {};
        td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width            = TEX_W;
        td.Height           = TEX_H;
        td.DepthOrArraySize = 1;
        td.MipLevels        = 1;
        td.Format           = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        hr = g12_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                                  D3D12_RESOURCE_STATE_COPY_DEST,
                                                  nullptr, IID_PPV_ARGS(&g12_noiseTexture));
        if (FAILED(hr)) return false;

        UINT srvStride = g12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE h = g12_srvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += srvStride * 2; // slot 2 = noise mask

        D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                    = DXGI_FORMAT_R8_UNORM;
        srvd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvd.Texture2D.MipLevels       = 1;
        g12_device->CreateShaderResourceView(g12_noiseTexture.Get(), &srvd, h);
    }

    // -- Constant buffers (CPU upload heap, persistently mapped) ---------------
    {
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width            = CB_SIZE;
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
                                                      nullptr, IID_PPV_ARGS(&g12_cbuf[i]));
            if (FAILED(hr)) return false;
            g12_cbuf[i]->Map(0, &readRange, (void**)&g12_cbufPtr[i]);
            memset(g12_cbufPtr[i], 0, CB_SIZE);
        }
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

    // -- Noise upload buffers (R8, one per frame slot) ------------------------
    {
        UINT rawPitch = TEX_W;  // R8 = 1 byte per texel
        g12_noiseUploadRowPitch = (rawPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                                & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        UINT noiseSize = g12_noiseUploadRowPitch * TEX_H;

        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width            = noiseSize;
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
                                                      nullptr, IID_PPV_ARGS(&g12_noiseUploadBuf[i]));
            if (FAILED(hr)) return false;
            g12_noiseUploadBuf[i]->Map(0, &readRange, (void**)&g12_noiseUploadPtr[i]);
            memset(g12_noiseUploadPtr[i], 0, noiseSize);
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

    // -- Transition textures to PIXEL_SHADER_RESOURCE --------------------------
    {
        g12_cmdAlloc[0]->Reset();
        g12_cmdList->Reset(g12_cmdAlloc[0].Get(), nullptr);

        D3D12_RESOURCE_BARRIER rb[3] = {};
        rb[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb[0].Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb[0].Transition.pResource   = g12_texture.Get();
        rb[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        rb[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        rb[1]                        = rb[0];
        rb[1].Transition.pResource   = g12_prevTexture.Get();
        rb[2]                        = rb[0];
        rb[2].Transition.pResource   = g12_noiseTexture.Get();
        g12_cmdList->ResourceBarrier(3, rb);
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

    // Keep the swap-chain host child window the same size as the parent's
    // client area, so the swap chain's HWND footprint matches what we ask
    // ResizeBuffers for.
    if (g12_swapHwnd) {
        SetWindowPos(g12_swapHwnd, NULL, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

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
    // Capture interlace state BEFORE deinterlace, to disable scanlines
    // on 480i content (matches VideoRender.c CPU path).
    bool isInterlacedSource = (fb->interlace != INTERLACE_NONE);
    if (isInterlacedSource && pVideo->deInterlace)
        fb = frameBufferDeinterlace(fb);

    // -- Data extent in the texture: maxWidth (or 2*maxWidth for any
    // doubleWidth line -- SCREEN6/7, SCREEN0 80-col, etc.).  Used by the
    // upload loop, noise generation, AR/UV math and the shader's clamp.
    int  srcW            = fb->maxWidth;
    bool frameDoubleWidth = false;
    for (int y = 0; y < fb->lines; y++) {
        if (fb->line[y].doubleWidth) { srcW *= 2; frameDoubleWidth = true; break; }
    }
    int srcH = fb->lines;

    // -- ARGB1555 -> BGRA8 -> upload buffer -------------------------------------
    // Clear unused buffer rows/cols; otherwise stale pixels persist from
    // the last frame in this slot (visible on deinterlace toggle).
    memset(g12_uploadPtr[g12_frameIndex], 0,
           (size_t)g12_uploadRowPitch * (size_t)TEX_H);
    {
        int lines     = fb->lines;
        int startLine = (fb->interlace == INTERLACE_ODD) ? 1 : 0;

        for (int y = 0; y < lines; y++) {
            UINT8*        dstRow = g12_uploadPtr[g12_frameIndex]
                                 + (UINT)((y + startLine) * (int)g12_uploadRowPitch);
            const UINT16* srcRow = fb->line[y].buffer;
            int srcW = fb->maxWidth;
            if (fb->line[y].doubleWidth) srcW *= 2;
            // SM5->SM7 mid-frame mixes single-width lines into a double-width
            // frame; duplicate them so the right half doesn't sample the
            // memset(0)=black tail and produce the upper-right black rectangle.
            int stride = (frameDoubleWidth && !fb->line[y].doubleWidth) ? 2 : 1;

            UINT32* dst = (UINT32*)dstRow;
            for (int x = 0; x < srcW; x++) {
                UINT16 p = srcRow[x];
                UINT32 r = (p >> 10) & 0x1F; r = (r << 3) | (r >> 2);
                UINT32 g = (p >>  5) & 0x1F; g = (g << 3) | (g >> 2);
                UINT32 b = (p >>  0) & 0x1F; b = (b << 3) | (b >> 2);
                // DXGI_FORMAT_B8G8R8A8_UNORM: memory [B,G,R,A]
                // as uint32 LE: A<<24 | R<<16 | G<<8 | B
                UINT32 v = (0xFFu << 24) | (r << 16) | (g << 8) | b;
                dst[x * stride] = v;
                if (stride > 1) dst[x * stride + 1] = v;
            }
        }
    }

    // -- Read blendFrames from global properties (not stored in Video struct) --
    // User-controlled only.  Mode 1 (software pseudo-interlace) is handled
    // upstream via VDP.c interlaceRaster=1 (scanlines suppressed).
    Properties* globalProps = propGetGlobalProperties();
    int blendFramesEnable  = (globalProps && globalProps->video.blendFrames) ? 1 : 0;

    // -- Noise mask generation ------------------------------------------------
    // Mirrors DD's `rndVal *= 13; rnd *= 23` PRNG.  rndVal advances every
    // frame so SHARP<->SHARP_NOISE toggle picks up a rolling state, not 51.
    static UINT32 g_dxNoiseRndVal = 51;
    g_dxNoiseRndVal *= 13;
    bool needsNoise = (pVideo->palMode == VIDEO_PAL_SHARP_NOISE ||
                       pVideo->palMode == VIDEO_PAL_BLUR_NOISE);
    if (needsNoise) {
        UINT32 rnd      = g_dxNoiseRndVal;
        UINT8* noiseDst = g12_noiseUploadPtr[g12_frameIndex];
        int    nPitch   = (int)g12_noiseUploadRowPitch;
        // DD advances rnd once per source PAIR.  non-dw: 1 src -> 2 dst,
        // both halves read noise[p].  dw: 2 src -> 2 dst, write noise twice
        // (same byte at p and p+1) so shader's p_for_formula=dst still hits.
        if (frameDoubleWidth) {
            for (int y = 0; y < srcH; y++) {
                UINT8* row = noiseDst + y * nPitch;
                for (int p = 0; p + 1 < srcW; p += 2) {
                    UINT8 n = (UINT8)((rnd >> 29) & 0x07);
                    row[p    ] = n;
                    row[p + 1] = n;
                    rnd *= 23;
                }
                if ((srcW & 1) != 0) {
                    /* dangling odd column at the very right edge */
                    row[srcW - 1] = (UINT8)((rnd >> 29) & 0x07);
                    rnd *= 23;
                }
            }
        } else {
            for (int y = 0; y < srcH; y++) {
                UINT8* row = noiseDst + y * nPitch;
                for (int p = 0; p < srcW; p++) {
                    row[p] = (UINT8)((rnd >> 29) & 0x07);
                    rnd *= 23;
                }
            }
        }
    }

    // -- Upload -> GPU texture; optionally snapshot previous frame before overwrite
    {
        // Transition current texture: PIXEL_SHADER_RESOURCE -> COPY_DEST
        // Transition prev texture (if blendFrames): PIXEL_SHADER_RESOURCE -> COPY_DEST
        // Helper lambda for a single transition barrier
        auto barrier = [&](ID3D12Resource* res,
                           D3D12_RESOURCE_STATES before,
                           D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER rb = {};
            rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            rb.Transition.pResource   = res;
            rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            rb.Transition.StateBefore = before;
            rb.Transition.StateAfter  = after;
            g12_cmdList->ResourceBarrier(1, &rb);
        };

        // Frame blend: save current frame to prev BEFORE uploading new.
        // blend+valid: PSR->COPY_SOURCE on texture, PSR->COPY_DEST on prev,
        // CopyResource, then prev back to PSR.  Otherwise: PSR->COPY_DEST only.
        if (blendFramesEnable && g12_prevTextureValid) {
            D3D12_RESOURCE_BARRIER rb2[2] = {};
            auto fill = [](D3D12_RESOURCE_BARRIER& r, ID3D12Resource* res,
                           D3D12_RESOURCE_STATES b, D3D12_RESOURCE_STATES a) {
                r.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                r.Transition.pResource   = res;
                r.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                r.Transition.StateBefore = b;
                r.Transition.StateAfter  = a;
            };
            fill(rb2[0], g12_texture.Get(),
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
            fill(rb2[1], g12_prevTexture.Get(),
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATE_COPY_DEST);
            g12_cmdList->ResourceBarrier(2, rb2);

            g12_cmdList->CopyResource(g12_prevTexture.Get(), g12_texture.Get());

            fill(rb2[0], g12_texture.Get(),
                 D3D12_RESOURCE_STATE_COPY_SOURCE,
                 D3D12_RESOURCE_STATE_COPY_DEST);
            fill(rb2[1], g12_prevTexture.Get(),
                 D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            g12_cmdList->ResourceBarrier(2, rb2);
        } else {
            barrier(g12_texture.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_DEST);
        }

        // Upload buffer -> g12_texture
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

        // Transition current back to PIXEL_SHADER_RESOURCE for the draw
        D3D12_RESOURCE_BARRIER rbBack = {};
        rbBack.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rbBack.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rbBack.Transition.pResource   = g12_texture.Get();
        rbBack.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rbBack.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        rbBack.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        g12_cmdList->ResourceBarrier(1, &rbBack);

        // After this frame, g12_prevTexture holds the previous frame's image
        // (if blendFramesEnable was on); it remains valid for subsequent frames.
        if (blendFramesEnable) g12_prevTextureValid = true;
    }

    // -- Noise mask upload (only when a *_NOISE palMode is selected) ----------
    // PIXEL_SHADER_RESOURCE -> COPY_DEST -> back; skipped for non-NOISE modes.
    if (needsNoise) {
        D3D12_RESOURCE_BARRIER rbN = {};
        rbN.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rbN.Transition.pResource   = g12_noiseTexture.Get();
        rbN.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rbN.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        rbN.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        g12_cmdList->ResourceBarrier(1, &rbN);

        D3D12_TEXTURE_COPY_LOCATION dstN = {};
        dstN.pResource        = g12_noiseTexture.Get();
        dstN.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstN.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcN = {};
        srcN.pResource                            = g12_noiseUploadBuf[g12_frameIndex].Get();
        srcN.Type                                 = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcN.PlacedFootprint.Offset               = 0;
        srcN.PlacedFootprint.Footprint.Format     = DXGI_FORMAT_R8_UNORM;
        srcN.PlacedFootprint.Footprint.Width      = TEX_W;
        srcN.PlacedFootprint.Footprint.Height     = TEX_H;
        srcN.PlacedFootprint.Footprint.Depth      = 1;
        srcN.PlacedFootprint.Footprint.RowPitch   = g12_noiseUploadRowPitch;
        g12_cmdList->CopyTextureRegion(&dstN, 0, 0, 0, &srcN, nullptr);

        rbN.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        rbN.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        g12_cmdList->ResourceBarrier(1, &rbN);
    }

    // -- UV / vertex + scanUvPeriod calculation --------------------------------
    // Scanlines are suppressed on interlaced raster timing (interlaceRaster):
    // real hardware fills the gaps with the alternate field, modes 1 and 3.
    bool scanlinesActive = pVideo->scanLinesEnable
                           && !isInterlacedSource
                           && !fb->interlaceRaster;
    float scanUvPeriod = 1.0f / (float)TEX_H; // fallback; overwritten below
    // Crop UV bounds (set inside the AR-fit block; consumed by the CB block
    // below for srcUvMin/Max).  Default fallback = full source rect when the
    // AR-fit branch doesn't execute.
    float cropU0 = 0.0f, cropU1 = (float)srcW / (float)TEX_W;
    float cropV0 = 0.0f, cropV1 = (float)srcH / (float)TEX_H;

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

        // srcW/srcH are computed once at function scope above so the CB
        // population block can also reference them for the border-bounds.
        float fTexW = (float)TEX_W;
        float fTexH = (float)TEX_H;
        float fWs   = (float)w;
        float fHs   = (float)h;

        // AR uses BASE_TEX_W/H (matches D3D9 C_iTextureW/H); raw srcH would
        // double after deinterlace and flip the AR condition.
        float fMSXW = (float)(BASE_TEX_W - iBorderLeft - iBorderRight);
        float fMSXH = (float)(BASE_TEX_H - iBorderTop  - iBorderBottom);

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

        // Vertical UV: deinterlaced (srcH>240) divides by BASE_TEX_H=240
        // (matches D3D9); otherwise by fTexH=480.
        bool isDeint = (srcH > BASE_TEX_H);
        float hUvDiv = isDeint ? (float)BASE_TEX_H : fTexH;
        fHm /= hUvDiv;

        // Crop asymmetry offset in the same UV units.
        float fDx = (float)(iBorderLeft - iBorderRight) * 0.5f * wUvScale;
        float fDy = (float)(iBorderTop  - iBorderBottom) * 0.5f / hUvDiv;

        // Center of the valid data region in UV space.
        // Deinterlaced: use 0.5 (full-texture center) like D3D9.
        // Non-deinterlaced: track the partial frame's actual position.
        float uCtr = (float)srcW * 0.5f / fTexW;
        float vCtr = isDeint ? 0.5f : (float)srcH * 0.5f / fTexH;

        float u0 = uCtr - fWm * 0.5f + fDx;
        float u1 = uCtr + fWm * 0.5f + fDx;
        float v0 = vCtr - fHm * 0.5f + fDy;
        float v1 = vCtr + fHm * 0.5f + fDy;

        // Crop UV bounds for srcUvMin/Max: AR-fit letterbox/pillarbox
        // outside this rect renders as borderColor (no source leak).
        float fCropWuv = (float)fMSXW * wUvScale;
        float fCropHuv = (float)fMSXH / hUvDiv;
        cropU0 = uCtr - fCropWuv * 0.5f + fDx;
        cropU1 = uCtr + fCropWuv * 0.5f + fDx;
        cropV0 = vCtr - fCropHuv * 0.5f + fDy;
        cropV1 = vCtr + fCropHuv * 0.5f + fDy;

        // -- Method A: UV period per source scanline (for sinusoidal scanlines) -
        // (v1-v0) = AR/crop-adjusted content height in UV; divide by visible
        // source line count for uniform scanline spacing.
        float visibleSrcLines = (float)srcH * (fMSXH / (float)BASE_TEX_H);
        scanUvPeriod = (visibleSrcLines > 0.0f)
                     ? ((v1 - v0) / visibleSrcLines)
                     : (1.0f / (float)TEX_H);

        Vtx12 verts[4] = {
            { -1.0f, +1.0f, u0, v0 },
            { +1.0f, +1.0f, u1, v0 },
            { -1.0f, -1.0f, u0, v1 },
            { +1.0f, -1.0f, u1, v1 },
        };
        memcpy(g12_vtxPtr[g12_frameIndex], verts, sizeof(verts));
    }

    // -- Populate constant buffer ----------------------------------------------
    {
        Properties* gp = propGetGlobalProperties();
        float satW = (float)(pVideo->colorSaturationWidth < 1 ? 1 : pVideo->colorSaturationWidth);

        EffectCB12 cb = {};
        cb.scanLinesEnable    = scanlinesActive ? 1.0f : 0.0f;
        cb.scanLinesIntensity = (float)pVideo->scanLinesPct / 100.0f;
        cb.scanUvPeriod       = scanUvPeriod;
        cb.blendFramesEnable  = blendFramesEnable ? 1.0f : 0.0f;
        cb.gammaExp           = (float)pVideo->gamma;
        cb.contrast           = (float)pVideo->contrast;
        cb.saturation         = (float)pVideo->saturation;
        cb.brightness         = (float)pVideo->brightness / 100.0f;
        cb.colorSatEnable     = pVideo->colorSaturationEnable ? 1.0f : 0.0f;
        cb.colorSatWidth      = satW / (float)TEX_W;
        cb.monitorColor       = gp ? (float)gp->video.monitorColor : 0.0f;

        /* Border colour: when extendBorderColor is set, use the MSX VDP
           border colour (frame's first pixel, RGB555 -> float).  Otherwise
           render outside-the-source-rect pixels as black, matching the
           default sampler behaviour. */
        if (gp && gp->video.d3d.extendBorderColor && fb && fb->line[0].buffer) {
            unsigned int b16 = (unsigned int)fb->line[0].buffer[0];
            cb.borderR = ((b16 >> 10) & 0x1F) / 31.0f;
            cb.borderG = ((b16 >>  5) & 0x1F) / 31.0f;
            cb.borderB = ((b16      ) & 0x1F) / 31.0f;
        } else {
            cb.borderR = cb.borderG = cb.borderB = 0.0f;
        }
        cb.borderA = 1.0f;

        /* UV bounds of the visible region after crop.  When AR fit
           extends u0..u1 / v0..v1 beyond the cropped area to fit the
           target aspect, pixels outside the crop region render as
           borderColor instead of leaking source rows that the crop was
           meant to remove.  No crop -> these match the texture's full
           valid data region (= [0..srcW/TEX_W] x [0..srcH/TEX_H]). */
        cb.srcUvMinU = cropU0;
        cb.srcUvMinV = cropV0;
        cb.srcUvMaxU = cropU1;
        cb.srcUvMaxV = cropV1;

        /* srcWf is the data extent (= 2*maxWidth in doubleWidth modes), not
           just maxWidth, so the shader's clamp doesn't repeat the rightmost
           pixel across the right half. */
        cb.palMode         = (float)(int)pVideo->palMode;
        cb.srcWf           = (float)srcW;
        cb.srcHf           = (float)fb->lines;
        cb.texWf           = (float)TEX_W;
        cb.texHf           = (float)TEX_H;
        cb.doubleWidthFlag = frameDoubleWidth ? 1.0f : 0.0f;

        memcpy(g12_cbufPtr[g12_frameIndex], &cb, sizeof(cb));
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

        // b0 effect-parameters CBV (root CBV)
        g12_cmdList->SetGraphicsRootConstantBufferView(2,
            g12_cbuf[g12_frameIndex]->GetGPUVirtualAddress());

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

extern "C" void D3D12ClearToBlack(HWND /*hwnd*/)
{
    // Called before SW_NORMAL so the re-exposed emu hwnd doesn't flash the
    // previous run: backbuffer + g12_texture/g12_prevTexture pixels +
    // g12_prevTextureValid all need wiping.
    if (!g12_ready) return;

    UINT idx = g12_frameIndex;
    WaitForFrame(idx);
    g12_cmdAlloc[idx]->Reset();
    g12_cmdList->Reset(g12_cmdAlloc[idx].Get(), nullptr);

    // Zero the textures via a zeroed upload-buffer copy.
    memset(g12_uploadPtr[idx], 0, (size_t)g12_uploadRowPitch * (size_t)TEX_H);

    auto trans = [&](ID3D12Resource* res,
                     D3D12_RESOURCE_STATES before,
                     D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER rb = {};
        rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb.Transition.pResource   = res;
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb.Transition.StateBefore = before;
        rb.Transition.StateAfter  = after;
        g12_cmdList->ResourceBarrier(1, &rb);
    };

    auto wipeTex = [&](ID3D12Resource* tex) {
        if (!tex) return;
        trans(tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = tex;
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource                            = g12_uploadBuf[idx].Get();
        src.Type                                 = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset               = 0;
        src.PlacedFootprint.Footprint.Format     = DXGI_FORMAT_B8G8R8A8_UNORM;
        src.PlacedFootprint.Footprint.Width      = TEX_W;
        src.PlacedFootprint.Footprint.Height     = TEX_H;
        src.PlacedFootprint.Footprint.Depth      = 1;
        src.PlacedFootprint.Footprint.RowPitch   = g12_uploadRowPitch;
        g12_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        trans(tex, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    };
    wipeTex(g12_texture.Get());
    wipeTex(g12_prevTexture.Get());

    // Skip blend-with-prev on the next frame so it doesn't sample old content.
    g12_prevTextureValid = false;

    // Clear the current backbuffer.
    trans(g12_rt[idx].Get(), D3D12_RESOURCE_STATE_PRESENT,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvH = g12_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvH.ptr += (SIZE_T)idx * g12_rtvSize;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g12_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
    trans(g12_rt[idx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_PRESENT);

    g12_cmdList->Close();
    ID3D12CommandList* lists[] = { g12_cmdList.Get() };
    g12_cmdQueue->ExecuteCommandLists(1, lists);

    g12_swapChain->Present(0, 0);

    ++g12_fenceCtr;
    g12_fenceVal[idx] = g12_fenceCtr;
    g12_cmdQueue->Signal(g12_fence.Get(), g12_fenceCtr);

    g12_frameIndex = g12_swapChain->GetCurrentBackBufferIndex();

    // Clear the second backbuffer too (FRAME_COUNT=2 flip keeps both alive).
    UINT idx2 = g12_frameIndex;
    WaitForFrame(idx2);
    g12_cmdAlloc[idx2]->Reset();
    g12_cmdList->Reset(g12_cmdAlloc[idx2].Get(), nullptr);
    trans(g12_rt[idx2].Get(), D3D12_RESOURCE_STATE_PRESENT,
                              D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtvH = g12_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvH.ptr += (SIZE_T)idx2 * g12_rtvSize;
    g12_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
    trans(g12_rt[idx2].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                              D3D12_RESOURCE_STATE_PRESENT);
    g12_cmdList->Close();
    g12_cmdQueue->ExecuteCommandLists(1, lists);
    g12_swapChain->Present(0, 0);
    ++g12_fenceCtr;
    g12_fenceVal[idx2] = g12_fenceCtr;
    g12_cmdQueue->Signal(g12_fence.Get(), g12_fenceCtr);
    g12_frameIndex = g12_swapChain->GetCurrentBackBufferIndex();

    // Block on both clears so SW_NORMAL never exposes a stale in-flight buffer.
    WaitForGpu();
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
