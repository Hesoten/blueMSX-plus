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
#include <dxgi1_6.h>     /* IDXGIOutput6, DXGI_OUTPUT_DESC1 (HDR colour space) */
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <math.h>

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
    float scanLinesBrightComp;  // multiplier (>=1.0) re-lifting average luminance after scanline mask
    float hdrMode;              // 0 SDR, 1 scRGB linear, 2 HDR10 PQ
    float hdrPaperWhiteNits;    // target SDR-white luminance in HDR mode (e.g. 200)
    float scanUvOffset;         // = uv.v0; trailing field for HLSL 16-byte align
    float darkBoostRatio;       // 1.0 = no boost; per-pixel HDR dark lift
    float scanUseAA;            // 0 LEGACY point-sample, 1 AA integration
    float scanShapeP;           // sin^p sharpness exponent [0, 4]
    float scalingFilter;        // 0 nearest, 1 sharp, 2 prescaled-bilinear, 3 bilinear (FAST)
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
        // FAST. 0/3 = plain point/linear Sample (sampler chosen CPU-side);
        // 1 = sharp bilinear (~1px screen seam); 2 = prescaled bilinear (fixed
        // 0.5-texel seam == forceHighRes 2x prescale + bilinear).  1/2 Load-based.
        if (scalingFilter > 0.5 && scalingFilter < 2.5) {
            float2 texSize = float2(texWf, texHf);
            int2   maxXY   = int2((int)srcWf - 1, (int)srcHf - 1);
            float2 uvTex   = p.uv * texSize - 0.5;
            float2 baseF   = floor(uvTex);
            float2 fracXY  = uvTex - baseF;
            // sharp: seam == 1 screen px; prescaled: fixed 0.5 texel.
            float2 seam    = (scalingFilter < 1.5) ? max(fwidth(uvTex), 1e-5) : float2(0.5, 0.5);
            float2 w       = clamp((fracXY - 0.5) / seam + 0.5, 0.0, 1.0);
            int2   b       = (int2)baseF;
            int2   b00 = clamp(b,             int2(0, 0), maxXY);
            int2   b10 = clamp(b + int2(1,0), int2(0, 0), maxXY);
            int2   b01 = clamp(b + int2(0,1), int2(0, 0), maxXY);
            int2   b11 = clamp(b + int2(1,1), int2(0, 0), maxXY);
            float3 cTop = lerp(texCur.Load(int3(b00,0)).rgb, texCur.Load(int3(b10,0)).rgb, w.x);
            float3 cBot = lerp(texCur.Load(int3(b01,0)).rgb, texCur.Load(int3(b11,0)).rgb, w.x);
            color = float4(lerp(cTop, cBot, w.y), 1.0);
        } else {
            color = texCur.Sample(samp, p.uv);
        }
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

)"
/* Split raw-string to dodge MSVC's 64K literal cap. */
R"(
    /* Pre-mask colour drives the HDR per-pixel dark boost below. */
    float3 preMaskColor = color.rgb;

    if (scanLinesEnable > 0.5) {
        /* AA mode (scanUseAA, pxPerRow >= 2.5) box-filter integrates sin
        ** over the source-row footprint; legacy mode is sin point-sample. */
        float srcRow = (p.uv.y - scanUvOffset) / scanUvPeriod;
        float beam;
        if (scanUseAA > 0.5) {
            float dy = abs(ddy(p.uv.y)) / scanUvPeriod;
            if (dy < 1e-3) {
                beam = sin(frac(srcRow) * 3.14159265);
            } else {
                /* Box-filter avg of sin(pi*frac(t)); closed form via
                ** antiderivative G(t)=floor(t)*(2/pi)+(1-cos(pi*frac(t)))/pi. */
                float a_self = srcRow - 0.5 * dy;
                float b_self = srcRow + 0.5 * dy;
                float Ga_s   = floor(a_self) * 0.6366197724 + (1.0 - cos(3.14159265 * frac(a_self))) * 0.3183098862;
                float Gb_s   = floor(b_self) * 0.6366197724 + (1.0 - cos(3.14159265 * frac(b_self))) * 0.3183098862;
                float beam_avg = (Gb_s - Ga_s) / dy;

                /* Use box-filter avg directly so it matches the C-side
                ** computeScanLinePaperWhiteBoost (2/pi at s=0). */
                beam = beam_avg;
            }
        } else {
            beam = sin(frac(srcRow) * 3.14159265);
        }
        /* Two-parameter mask: mask = lerp(s, 1, sin^p) where s = depth,
        ** p = shape (0..4). Linear-space pre-compensated by pow(mask, 1/2.2). */
        float maskShape = (scanShapeP > 1e-4) ? pow(saturate(beam), scanShapeP) : 1.0;
        float mask      = lerp(scanLinesIntensity, 1.0, maskShape);
        /* SDR groove-cap (min with 1.0) prevents mid-tone overshoot. */
        if (hdrMode <= 0.5) {
            float groove = min(mask * scanLinesBrightComp, 1.0);
            color.rgb    = preMaskColor * pow(max(groove, 0.0), 1.0/2.2);
        } else {
            color.rgb *= pow(max(mask, 0.0), 1.0/2.2);
        }
    }

    /* SDR: apply gamma in encoded space; HDR defers to final pow below. */
    if (hdrMode <= 0.5) {
        color.rgb = pow(saturate(color.rgb), float3(gammaExp, gammaExp, gammaExp));
    }
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

    /* HDR final encode: mode 1 = scRGB G10 P709, mode 2 = HDR10 PQ P2020.
    ** gammaExp folded into sRGB->linear decode (1.0 -> clean 2.2). */
    float3 hdrG = float3(2.2 * gammaExp, 2.2 * gammaExp, 2.2 * gammaExp);
    if (hdrMode > 1.5) {
        // sRGB-encoded -> linear -> absolute nits at SDR-white = paper-white setting.
        // Values >1.0 (scanline peak) propagate proportionally into HDR headroom.
        float3 lin   = pow(max(color.rgb, 0.0), hdrG);
        // BT.709 -> BT.2020 (P2020 swap chain expects BT.2020 primaries).
        {
            float3x3 bt709to2020 = float3x3(
                0.6274039, 0.3292831, 0.0433130,
                0.0690972, 0.9195404, 0.0113624,
                0.0163914, 0.0880133, 0.8955953);
            lin = mul(bt709to2020, lin);
            lin = max(lin, 0.0);
        }
        float3 nits  = lin * hdrPaperWhiteNits;
        /* Per-pixel dark boost: lifts darks, fades to 1.0 at white,
        ** further suppressed on saturated colours; no-op at ratio==1. */
        float preLumaPq = saturate(max(max(preMaskColor.r, preMaskColor.g), preMaskColor.b));
        float darkExtra = lerp(darkBoostRatio, 1.0, preLumaPq);
        float preMinPq  = min(min(preMaskColor.r, preMaskColor.g), preMaskColor.b);
        float chromaPq  = saturate(preLumaPq - max(preMinPq, 0.0));
        darkExtra = lerp(darkExtra, 1.0, chromaPq);
        nits *= darkExtra;
        // ST.2084 (PQ) OETF.
        float3 L     = nits / 10000.0;
        float3 Lm1   = pow(max(L, 0.0), float3(0.1593017578125, 0.1593017578125, 0.1593017578125));
        float3 num   = 0.8359375 + 18.8515625 * Lm1;
        float3 den   = 1.0 + 18.6875 * Lm1;
        float3 pq    = pow(max(num / den, 0.0), float3(78.84375, 78.84375, 78.84375));
        return float4(saturate(pq), 1.0);
    }
    if (hdrMode > 0.5) {
        // scRGB linear: 1.0 = 80 nits per spec, so paper-white-nits / 80 lifts the SDR ref.
        float scale = hdrPaperWhiteNits / 80.0;
        // Per-pixel dark boost matching the PQ branch (with the same
        // chromaticity suppression so pure single-channel colours
        // don't get the asymmetric max-channel boost).
        float preLumaSc   = saturate(max(max(preMaskColor.r, preMaskColor.g), preMaskColor.b));
        float darkExtraSc = lerp(darkBoostRatio, 1.0, preLumaSc);
        float preMinSc    = min(min(preMaskColor.r, preMaskColor.g), preMaskColor.b);
        float chromaSc    = saturate(preLumaSc - max(preMinSc, 0.0));
        darkExtraSc = lerp(darkExtraSc, 1.0, chromaSc);
        color.rgb = pow(max(color.rgb, 0.0), hdrG) * (scale * darkExtraSc);
        return float4(color.rgb, 1.0);
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
static ComPtr<ID3D12PipelineState>       g12_pso;       // RTV format = g12_swapFormat (live render)
static ComPtr<ID3D12PipelineState>       g12_psoRec;    // RTV format = BGRA8 (SDR recording)
static ComPtr<ID3D12PipelineState>       g12_psoRecHdr; // RTV format = R10G10B10A2_UNORM (HDR recording, PQ-encoded)

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
    float scanLinesBrightComp; // multiplier (>=1.0) re-lifting average luminance after scanline mask
    float hdrMode;             // 0 SDR, 1 scRGB linear, 2 HDR10 PQ
    float hdrPaperWhiteNits;   // target SDR-white luminance in HDR mode (e.g. 200)
    float scanUvOffset;        // = uv.v0; appended at end for HLSL 16-byte alignment
    float darkBoostRatio;      // per-pixel HDR dark-pixel lift (1.0 = no extra boost)
    float scanUseAA;           // 0 = LEGACY point-sample, 1 = AA integration
    float scanShapeP;          // sin^p sharpness exponent [0, 4]
    float scalingFilter;       // P_D3D_SCALE_* (FAST-path reconstruction)
};
static const UINT CB_SIZE = (sizeof(EffectCB12) + 255) & ~255u; // 256-byte aligned
static ComPtr<ID3D12Resource>            g12_cbuf[FRAME_COUNT];
static UINT8*                            g12_cbufPtr[FRAME_COUNT] = {};

static HWND g12_hwnd         = NULL;  // parent (emu) HWND passed by caller
static HWND g12_swapHwnd     = NULL;  // child of g12_hwnd; owns the DXGI swap chain
static int  g12_w            = 0;
static int  g12_h            = 0;
static bool g12_isFullscreen = false;  // last EnterFullscreen/Windowed transition
// HDR state -- set by D3D12_Init, cleared if no HDR format succeeded.
// Try order: HDR10 PQ R10G10B10A2 -> HDR10 PQ FP16 -> scRGB FP16.
static bool                  g12_hdrActive     = false;
static DXGI_FORMAT           g12_swapFormat    = DXGI_FORMAT_B8G8R8A8_UNORM;
static DXGI_COLOR_SPACE_TYPE g12_hdrColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

static void vApplyHdrMetadata(void);  /* defined alongside vD3D12RebuildBuffers */

// --- Recording-readback state (post-render BGRA32 capture for MP4) -----------
// Allocated on demand by D3D12RecordBegin and torn down by D3D12RecordEnd.
// The capture path is independent of the live render: it owns its own source
// texture + upload buffer + noise resources, builds its own CB (full-source-
// region quad with scanUvPeriod = 1/TEX_H), and runs on the emu thread so
// modal dialogs blocking the main thread don't stall it.
static ComPtr<ID3D12Resource>            g12_recRT;
static ComPtr<ID3D12DescriptorHeap>      g12_recRtvHeap;
static ComPtr<ID3D12Resource>            g12_recReadback;
static ComPtr<ID3D12CommandAllocator>    g12_recCmdAlloc;
static ComPtr<ID3D12GraphicsCommandList> g12_recCmdList;
static ComPtr<ID3D12Fence>               g12_recFence;
static HANDLE                            g12_recFenceEvent = NULL;
static UINT64                            g12_recFenceCtr = 0;
static ComPtr<ID3D12Resource>            g12_recVtxBuf;
static UINT8*                            g12_recVtxPtr  = nullptr;
static D3D12_VERTEX_BUFFER_VIEW          g12_recVtxView = {};
static ComPtr<ID3D12Resource>            g12_recCbuf;
static UINT8*                            g12_recCbufPtr = nullptr;
static int                               g12_recW = 0;
static int                               g12_recH = 0;
static UINT                              g12_recRowPitchAligned = 0;

// Capture-owned source/noise textures + upload buffers, kept independent of
// the live g12_texture / g12_noiseTexture state.
static ComPtr<ID3D12Resource>            g12_recSrcTexture;
static ComPtr<ID3D12Resource>            g12_recUploadBuf;
static UINT8*                            g12_recUploadPtr = nullptr;
static UINT                              g12_recUploadRowPitch = 0;
static ComPtr<ID3D12Resource>            g12_recNoiseTexture;
static ComPtr<ID3D12Resource>            g12_recNoiseUploadBuf;
static UINT8*                            g12_recNoiseUploadPtr = nullptr;
static UINT                              g12_recNoiseUploadRowPitch = 0;
static UINT32                            g12_recNoiseRndVal = 51;
static ComPtr<ID3D12DescriptorHeap>      g12_recSrvHeap;

// HDR offline-preview swap chain: a small R10G10B10A2 PQ swap chain bound to
// a child HWND inside the modal render dialog, fed by CopyResource from
// g12_recRT after each capture.  Lifetime is the dialog's lifetime; sized at
// recording resolution (1280x960) and DXGI-stretched to the dialog's preview
// rect (PREVIEW_W x PREVIEW_H).
static ComPtr<IDXGISwapChain3>           g12_previewSwap;
static ComPtr<ID3D12Resource>            g12_previewBackbuffers[2];
static ComPtr<ID3D12CommandAllocator>    g12_previewCmdAlloc;
static ComPtr<ID3D12GraphicsCommandList> g12_previewCmdList;
static ComPtr<ID3D12Fence>               g12_previewFence;
static HANDLE                            g12_previewFenceEvent = NULL;
static UINT64                            g12_previewFenceCtr = 0;
// Atomic + release/acquire ordering: Blit on emu thread vs End on UI thread.
// End must publish g12_previewActive=false BEFORE Resetting g12_previewSwap so
// a concurrent Blit cannot pass the guard and then deref a null ComPtr.
static std::atomic<bool>                 g12_previewActive{false};

static bool g12_ready        = false;
static bool g12_needCleanup  = false;
static int  g12_syncVblank   = -1;

// When true, vD3D12Cleanup tearing down a live device must transparently
// re-allocate the recorder's capture resources after the device comes back
// up (set by D3D12RecordBegin, cleared only by an explicit D3D12RecordEnd).
// Without this the SinkWriter would keep getting audio but no further
// frames after a syncVblank-mode change in the render loop -- the MP4
// looks like the video stream cuts off where the change happened.
static bool g12_recAutoRebind = false;
static int  g12_recSavedW     = 0;
static int  g12_recSavedH     = 0;
static int  g12_recSavedHdr   = 0;     /* 0 = BGRA8, 1 = R10G10B10A2_UNORM (HDR PQ) */

// Forward decls -- recorder helpers are referenced by vD3D12Cleanup and
// bD3D12Init below; the bodies live next to D3D12RecordBegin / End so the
// recorder lifecycle stays grouped at the bottom of the file.
static void releaseRecordResources(void);
static int  allocRecordResources(int width, int height, int hdr);

// Child window hosts the DXGI swap chain: presenting on the caller's HWND
// would leave a DWM redirection surface that blocks DDraw/GDI later.
static const char* k_d3d12SwapWndClass = "blueMSXD3D12Swap";
static bool g12_swapWndClassRegistered = false;

/* Forward mouse events to the parent (emuHwnd) so clicks on the D3D12
** presentation surface still reach emuWndProc for the capture handler.
** Also forward WM_SETCURSOR so emu-side lock/fade cursor state applies. */
static LRESULT CALLBACK swapWndProcForwardMouse(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_SETCURSOR:
        return SendMessageA(GetParent(hwnd), msg, wp, lp);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void registerSwapWindowClass()
{
    if (g12_swapWndClassRegistered) return;
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = swapWndProcForwardMouse;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = k_d3d12SwapWndClass;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    /* Class cursor: without this the OS keeps whatever was last set, which
    ** after ShowCursor(TRUE) leaves the arrow invisible over the swap area
    ** (parent's WM_SETCURSOR falls through to DefWindowProc otherwise). */
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    g12_swapWndClassRegistered = true;
}

// SDR brightness comp B_MAX: shader applies min(1, mask * B_MAX).  HDR
// folds this into paperWhite instead (computeScanLinePaperWhiteBoost).
static float computeScanLineBrightComp(const Video* v)
{
    if (!v || !v->scanLinesEnable) return 1.0f;
    if (v->scanLinesBrightAuto) {
        return 2.0f;
    }
    int p = v->scanLinesBrightPct;
    if (p < 100) p = 100;                     // 1.00x = no compensation
    if (p > 200) p = 200;                     // matches slider TBM_SETRANGE
    return (float)p / 100.0f;
}

// HDR scanline brightness comp as a paperWhite multiplier, capped at 3.0x.
static float computeScanLinePaperWhiteBoost(const Video* v, bool fullscreen,
                                            float pxPerScanRow)
{
    (void)fullscreen;  // unified path
    if (!v || !v->scanLinesEnable) return 1.0f;
    if (v->scanLinesBrightAuto) {
        float s = (float)v->scanLinesPct / 100.0f;
        if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f;

        // Integer-zoom special cases (tolerance 0.10 covers a window
        // sized "near" the integer multiple).
        int  intN  = (int)(pxPerScanRow + 0.5f);
        bool isInt = (pxPerScanRow > 0.5f)
                     && (fabsf(pxPerScanRow - (float)intN) < 0.10f);
        if (isInt && intN <= 1) {
            return 1.0f;
        }
        // Two-parameter mask: depth s (= scanLinesIntensity, 0..1)
        // and shape p (= scanlinesShapePct/100 * 4, 0..4).
        //   mask = lerp(s, 1, sin^p) = s + (1 - s) * sin^p
        double shapePctRaw = (double)v->scanlinesShapePct;
        if (shapePctRaw < 0.0)   shapePctRaw = 0.0;
        if (shapePctRaw > 100.0) shapePctRaw = 100.0;
        double p = shapePctRaw / 100.0 * 4.0;

        if (isInt && intN == 2) {
            // N=2 uniform mask: every pixel sees sin(pi/4) = 0.7071.
            double shape = (p > 1e-4) ? pow(0.7071068, p) : 1.0;
            double m     = (double)s + (1.0 - (double)s) * shape;
            if (m < 0.01) m = 0.01;
            return (float)(1.0 / m);
        }

        // Continuous Simpson 1/avgLin where avgLin = integral of
        // lerp(s, 1, sin^p)(pi*y) over a scanline period.
        const int N = 64;
        double sum = 0.0;
        for (int i = 0; i <= N; ++i) {
            double y     = (double)i / (double)N;
            double sn    = sin(3.14159265358979 * y);
            double shape = (p > 1e-4) ? pow(sn, p) : 1.0;
            double m     = (double)s + (1.0 - (double)s) * shape;
            double w     = (i == 0 || i == N) ? 1.0 : ((i & 1) ? 4.0 : 2.0);
            sum += w * m;
        }
        double avgLin = sum / (3.0 * (double)N);
        if (avgLin < 0.01) avgLin = 0.01;
        double boost = 1.0 / avgLin;
        // Perceptual dy comp: low pxPerRow looks brighter, so dim via
        // maxB^4 to keep perceived brightness uniform across zooms.
        double dy   = (pxPerScanRow > 1e-4) ? (1.0 / (double)pxPerScanRow) : 0.0;
        double maxB = (dy > 1e-4)
                    ? (2.0 * sin(3.14159265358979 * 0.5 * dy) / (3.14159265358979 * dy))
                    : 1.0;
        if (maxB > 1.0) maxB = 1.0;
        double dyComp = maxB * maxB * maxB * maxB;
        boost *= dyComp;
        if (boost < 1.0) boost = 1.0;
        if (boost > 3.0) boost = 3.0;
        return (float)boost;
    }
    int p = v->scanLinesBrightPct;
    if (p < 100) p = 100;
    if (p > 300) p = 300;                     // HDR manual range goes to 3.0x
    return (float)p / 100.0f;
}

// Per-pixel HDR dark-pixel boost: permanently disabled (returns 1.0).
// paperWhite alone handles restoration; mechanism kept for future
// re-engagement (e.g. manual cap mode).
static float computeScanLineDarkBoost(const Video* v, bool fullscreen,
                                      float pxPerScanRow)
{
    (void)v; (void)fullscreen; (void)pxPerScanRow;
    return 1.0f;
}

// --- AR / crop / UV helper (shared by live render + recording capture) -------
// AR-correct + cropped projection of srcW x srcH onto targetW x targetH;
// lets the recorder honour the same AR / crop settings as the live render.
struct UvRender12 {
    float u0, u1, v0, v1;
    /* AA-mode values (period = 1/TEX_H, offset = 0); the live render may
    ** override with legacy values before pushing to the cbuffer. */
    float scanUvPeriod;
    /* Crop-area UV bounds for the shader's outOfBounds test: AR-fit bars
    ** render as border colour, never as rows the crop removed. */
    float cropU0, cropU1, cropV0, cropV1;
};

static UvRender12 computeUvForRender(int targetW, int targetH,
                                     int srcW, int srcH,
                                     D3DProperties* props)
{
    UvRender12 o = {};

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

    const float fTexW = (float)TEX_W;
    const float fTexH = (float)TEX_H;
    const float fWs   = (float)targetW;
    const float fHs   = (float)targetH;

    // Use fixed logical dimensions (BASE_TEX_W=272, BASE_TEX_H=240) for the
    // aspect-ratio formula, matching D3D9's C_iTextureWidth/Height approach.
    const float fMSXW = (float)(BASE_TEX_W - iBorderLeft - iBorderRight);
    const float fMSXH = (float)(BASE_TEX_H - iBorderTop  - iBorderBottom);

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

    const float wUvScale = (float)srcW / ((float)BASE_TEX_W * fTexW);
    fWm *= wUvScale;

    const bool  isDeint = (srcH > BASE_TEX_H);
    const float hUvDiv  = isDeint ? (float)BASE_TEX_H : fTexH;
    fHm /= hUvDiv;

    const float fDx = (float)(iBorderLeft - iBorderRight) * 0.5f * wUvScale;
    const float fDy = (float)(iBorderTop  - iBorderBottom) * 0.5f / hUvDiv;

    const float uCtr = (float)srcW * 0.5f / fTexW;
    const float vCtr = isDeint ? 0.5f : (float)srcH * 0.5f / fTexH;

    o.u0 = uCtr - fWm * 0.5f + fDx;
    o.u1 = uCtr + fWm * 0.5f + fDx;
    o.v0 = vCtr - fHm * 0.5f + fDy;
    o.v1 = vCtr + fHm * 0.5f + fDy;

    /* Crop UV bounds: outside them the shader renders borderColor, so
    ** AR-fit bars never leak source rows the crop removed. */
    {
        const float fCropWuv = (float)fMSXW * wUvScale;
        const float fCropHuv = (float)fMSXH / hUvDiv;
        o.cropU0 = uCtr - fCropWuv * 0.5f + fDx;
        o.cropU1 = uCtr + fCropWuv * 0.5f + fDx;
        o.cropV0 = vCtr - fCropHuv * 0.5f + fDy;
        o.cropV1 = vCtr + fCropHuv * 0.5f + fDy;
    }

    /* scanUvPeriod pinned to 1/TEX_H (one sin period per texture row,
    ** independent of crop / AR).  Tying it to (v1-v0)/srcH made the
    ** period drift under AR letterbox and beat into a moire pattern. */
    o.scanUvPeriod = 1.0f / (float)TEX_H;

    return o;
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
    g12_psoRec.Reset();
    g12_psoRecHdr.Reset();
    g12_rootSig.Reset();
    g12_cmdList.Reset();
    g12_samplerHeap.Reset();
    g12_srvHeap.Reset();
    g12_rtvHeap.Reset();

    // Recording resources also tied to this device; release before the
    // device. Preserve g12_recAutoRebind / g12_recSavedW/H so bD3D12Init
    // can re-allocate the recorder's resources after the new device comes
    // up -- a live recording survives transparently.
    releaseRecordResources();

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
        /* HDR opt-in needs both properties.video.hdrEnable and
        ** OS-level HDR on the target output (via IDXGIOutput6 ColorSpace). */
        Properties* gp = propGetGlobalProperties();
        bool propWantsHdr = (gp && gp->video.hdrEnable) ? true : false;

        bool desktopIsHdr = false;
        if (propWantsHdr) {
            // Walk every adapter / output -- any HDR output is enough.
            ComPtr<IDXGIAdapter1> adapter;
            for (UINT a = 0; !desktopIsHdr && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
                ComPtr<IDXGIOutput> output;
                for (UINT o = 0; !desktopIsHdr && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
                    ComPtr<IDXGIOutput6> output6;
                    if (SUCCEEDED(output.As(&output6))) {
                        DXGI_OUTPUT_DESC1 odesc = {};
                        if (SUCCEEDED(output6->GetDesc1(&odesc))) {
                            if (odesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                                desktopIsHdr = true;
                            }
                        }
                    }
                }
            }
        }

        /* HDR attempt order: R10G10B10A2_UNORM/PQ, FP16/PQ, FP16/scRGB;
        ** SDR (B8G8R8A8) is the unconditional fallback. */
        struct HdrAttempt {
            DXGI_FORMAT          format;
            DXGI_COLOR_SPACE_TYPE colorSpace;
        };
        const HdrAttempt attempts[] = {
            { DXGI_FORMAT_R10G10B10A2_UNORM,  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 },
            { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 },
            { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709    },
        };
        const bool wantHdr = propWantsHdr && desktopIsHdr;
        g12_hdrActive  = false;
        g12_swapFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

        ComPtr<IDXGISwapChain1> sc1;
        if (wantHdr) {
            for (const auto& a : attempts) {
                DXGI_SWAP_CHAIN_DESC1 scd = {};
                scd.BufferCount  = FRAME_COUNT;
                scd.Width        = (UINT)w;
                scd.Height       = (UINT)h;
                scd.Format       = a.format;
                scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                scd.SampleDesc.Count = 1;
                ComPtr<IDXGISwapChain1> trySc;
                if (FAILED(factory->CreateSwapChainForHwnd(
                        g12_cmdQueue.Get(), g12_swapHwnd, &scd, nullptr, nullptr, &trySc))) {
                    continue;
                }
                ComPtr<IDXGISwapChain3> trySc3;
                if (FAILED(trySc.As(&trySc3))) continue;
                UINT csSupport = 0;
                if (FAILED(trySc3->CheckColorSpaceSupport(a.colorSpace, &csSupport)) ||
                    !(csSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
                    continue;
                }
                if (FAILED(trySc3->SetColorSpace1(a.colorSpace))) continue;
                sc1               = trySc;
                g12_hdrActive     = true;
                g12_swapFormat    = a.format;
                g12_hdrColorSpace = a.colorSpace;
                break;
            }
        }
        if (!g12_hdrActive) {
            DXGI_SWAP_CHAIN_DESC1 scd = {};
            scd.BufferCount  = FRAME_COUNT;
            scd.Width        = (UINT)w;
            scd.Height       = (UINT)h;
            scd.Format       = DXGI_FORMAT_B8G8R8A8_UNORM;
            scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            scd.SampleDesc.Count = 1;
            hr = factory->CreateSwapChainForHwnd(g12_cmdQueue.Get(), g12_swapHwnd, &scd, nullptr, nullptr, &sc1);
            if (FAILED(hr)) return false;
            g12_swapFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        }
        factory->MakeWindowAssociation(g12_swapHwnd, DXGI_MWA_NO_ALT_ENTER);
        sc1.As(&g12_swapChain);
        /* HDR mastering metadata (primaries + content peak nits). */
        vApplyHdrMetadata();
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
        psd.RTVFormats[0]         = g12_swapFormat;
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

        /* Recording capture PSOs: g12_psoRec = BGRA8 for SDR,
        ** g12_psoRecHdr = R10G10B10A2 (PQ 10-bit) for HDR. */
        psd.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        hr = g12_device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&g12_psoRec));
        if (FAILED(hr)) return false;
        psd.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
        hr = g12_device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&g12_psoRecHdr));
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

    // If a live recording was running before this device reset, transparently
    // re-allocate the capture-owned resources at the recorded dimensions so
    // the recorder doesn't see a gap (the MF SinkWriter session is unaware
    // of DX12 lifecycle and keeps producing the same MP4).
    if (g12_recAutoRebind && g12_recSavedW > 0 && g12_recSavedH > 0) {
        if (!allocRecordResources(g12_recSavedW, g12_recSavedH, g12_recSavedHdr)) {
            g12_recAutoRebind = false;
        }
    }
    return true;
}

// --- HDR mastering metadata ---------------------------------------------------

// Primaries follow g12_hdrColorSpace; MaxCLL = fixed 1200-nit ceiling
// (avoids tone-map adaptation on toggle); MaxFALL tracks paperWhite.
static void vApplyHdrMetadata(void)
{
    if (!g12_hdrActive) return;
    ComPtr<IDXGISwapChain4> sc4;
    if (FAILED(g12_swapChain.As(&sc4))) return;

    Properties* gp = propGetGlobalProperties();
    int pwInt = gp ? gp->video.hdrPaperWhiteNits : 200;
    if (pwInt < 80)  pwInt = 80;
    if (pwInt > 400) pwInt = 400;

    /* Fixed capability ceiling: max selectable paperWhite (400) * max
    ** scanline-comp boost (3.0).  MaxCLL stays constant across paperWhite
    ** and scanline changes so displays don't retune. */
    const int kMaxCll = 400 * 3;   /* 1200 nits */

    DXGI_HDR_METADATA_HDR10 m = {};
    if (g12_hdrColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
        /* BT.2020 primaries (in 0.00002 increments per spec). */
        m.RedPrimary[0]   = 35400;  m.RedPrimary[1]   = 14600;
        m.GreenPrimary[0] =  8500;  m.GreenPrimary[1] = 39850;
        m.BluePrimary[0]  =  6550;  m.BluePrimary[1]  =  2300;
    } else {
        /* scRGB G10 P709 fallback: BT.709 primaries. */
        m.RedPrimary[0]   = 32000;  m.RedPrimary[1]   = 16500;
        m.GreenPrimary[0] = 15000;  m.GreenPrimary[1] = 30000;
        m.BluePrimary[0]  =  7500;  m.BluePrimary[1]  =  3000;
    }
    m.WhitePoint[0]   = 15635;  m.WhitePoint[1]   = 16450;     /* D65 */
    m.MaxMasteringLuminance      = 1000 * 10000;               /* 1000 nits */
    m.MinMasteringLuminance      =          50;                /* 0.005 nits */
    m.MaxContentLightLevel       = (UINT16)kMaxCll;            /* fixed capability ceiling (1200) */
    m.MaxFrameAverageLightLevel  = (UINT16)pwInt;              /* avg ~= paperWhite (slider-tracked) */
    sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(m), &m);
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
                                  g12_swapFormat, 0);
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
    // Scanlines off for any interlaced source -- the alternate field fills
    // the gaps on real hardware.
    bool scanlinesActive = pVideo->scanLinesEnable
                           && !isInterlacedSource
                           && !fb->interlaceRaster;

    UvRender12 uv = computeUvForRender(w, h, srcW, srcH, props);
    /* AA mode (pxPerRow >= 2.5): period 1/TEX_H, offset 0; the shader
    ** integrates sin over each screen pixel's source-row footprint.
    ** Legacy (< 2.5): period (v1-v0)/srcH, offset v0, plain point-sample
    ** -- the screen can't resolve a scanline pattern there anyway. */
    float vSpanRender = uv.v1 - uv.v0;
    float pxPerRowRender = (vSpanRender > 1e-6f)
                         ? ((float)h / (vSpanRender * (float)TEX_H))
                         : 0.0f;
    bool useScanAA      = (pxPerRowRender >= 2.5f);
    float scanUvPeriod  = useScanAA
                          ? (1.0f / (float)TEX_H)
                          : ((srcH > 0) ? (vSpanRender / (float)srcH)
                                        : (1.0f / (float)TEX_H));
    float scanUvOffset  = useScanAA ? 0.0f : uv.v0;
    {
        Vtx12 verts[4] = {
            { -1.0f, +1.0f, uv.u0, uv.v0 },
            { +1.0f, +1.0f, uv.u1, uv.v0 },
            { -1.0f, -1.0f, uv.u0, uv.v1 },
            { +1.0f, -1.0f, uv.u1, uv.v1 },
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
        /* HDR uses a global paperWhite lift instead; shader skips SDR boost. */
        cb.scanLinesBrightComp = scanlinesActive
            ? computeScanLineBrightComp(pVideo)
            : 1.0f;
        /* hdrMode: 0=SDR, 1=scRGB linear, 2=HDR10 PQ (encoded in shader). */
        cb.hdrMode            = g12_hdrActive
            ? (g12_hdrColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? 2.0f : 1.0f)
            : 0.0f;
        {
            int pwn = gp ? gp->video.hdrPaperWhiteNits : 200;
            if (pwn < 80)  pwn = 80;
            if (pwn > 400) pwn = 400;
            float pw = (float)pwn;
            /* HDR: scanline brightness multiplies paperWhite (capped 800nits). */
            if (g12_hdrActive && scanlinesActive) {
                /* pxPerRowRender (= h / ((v1-v0)*TEX_H)) is mode-independent,
                ** unlike a value derived from scanUvPeriod. */
                pw *= computeScanLinePaperWhiteBoost(pVideo, g12_isFullscreen, pxPerRowRender);
                cb.darkBoostRatio = computeScanLineDarkBoost(pVideo, g12_isFullscreen, pxPerRowRender);
            } else if (g12_hdrActive
                       && pVideo->scanLinesEnable
                       && (isInterlacedSource || fb->interlaceRaster)) {
                /* Interlaced: real CRTs light gap rows via alternate field;
                ** bump paperWhite 1.20x in HDR (SDR has no headroom). */
                pw *= 1.20f;
                cb.darkBoostRatio = 1.0f;
            } else {
                cb.darkBoostRatio = 1.0f;
            }
            if (pw > 800.0f) pw = 800.0f;
            cb.hdrPaperWhiteNits = pw;
        }
        cb.scanUvPeriod       = scanUvPeriod;
        cb.scanUvOffset       = scanUvOffset;
        cb.scanUseAA          = useScanAA ? 1.0f : 0.0f;
        {
            int spct = pVideo->scanlinesShapePct;
            if (spct < 0)   spct = 0;
            if (spct > 100) spct = 100;
            cb.scanShapeP = (float)spct / 100.0f * 4.0f;
        }
        cb.scalingFilter = (float)props->scalingFilter;
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
           valid data region (= [0..srcW/TEX_W] x [0..srcH/TEX_H])
           after intersection with the source content layout. */
        cb.srcUvMinU = uv.cropU0;
        cb.srcUvMinV = uv.cropV0;
        cb.srcUvMaxU = uv.cropU1;
        cb.srcUvMaxV = uv.cropV1;

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

        // Select sampler: index 0 = point, index 1 = linear.  Sharp uses Load()
        // and ignores this; only plain BILINEAR needs the linear sampler.
        D3D12_GPU_DESCRIPTOR_HANDLE sampH = g12_samplerHeap->GetGPUDescriptorHandleForHeapStart();
        if (props->scalingFilter == P_D3D_SCALE_BILINEAR)
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

    // Re-apply each Present so DWM rebinding drops recover, and the
    // paperWhite slider propagates live.
    if (g12_hdrActive) {
        ComPtr<IDXGISwapChain3> sc3;
        if (SUCCEEDED(g12_swapChain.As(&sc3))) {
            sc3->SetColorSpace1(g12_hdrColorSpace);
        }
        vApplyHdrMetadata();
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

// --- windowing helpers --------------------------------------------------------

// Finds the monitor containing the parent window's centre and resizes
// parent + child to cover that monitor (borderless windowed fullscreen).
static void vSetFullscreen(HWND hWnd)
{
    DISPLAY_DEVICE  oDisplayDevice;
    WINDOWPLACEMENT oWindowPlacement;
    DEVMODE         oDevMode;
    int             iIndex;
    int             iX, iY;

    oDevMode.dmSize        = sizeof(oDevMode);
    oDevMode.dmDriverExtra = 0;
    oDisplayDevice.cb      = sizeof(oDisplayDevice);

    GetWindowPlacement(GetParent(hWnd), &oWindowPlacement);

    iX = (oWindowPlacement.rcNormalPosition.left + oWindowPlacement.rcNormalPosition.right) / 2;
    iY = (oWindowPlacement.rcNormalPosition.top  + oWindowPlacement.rcNormalPosition.bottom) / 2;
    iIndex = 0;

    while (EnumDisplayDevices(NULL, iIndex++, &oDisplayDevice, 0)) {
        if ((oDisplayDevice.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
            EnumDisplaySettings(oDisplayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &oDevMode)) {
            if ((oDevMode.dmPosition.x                            <= iX) &&
                (oDevMode.dmPosition.y                            <= iY) &&
                (oDevMode.dmPosition.x + (signed)oDevMode.dmPelsWidth  > iX) &&
                (oDevMode.dmPosition.y + (signed)oDevMode.dmPelsHeight > iY)) {
                SetWindowPos(GetParent(hWnd), HWND_TOPMOST,
                             oDevMode.dmPosition.x, oDevMode.dmPosition.y,
                             oDevMode.dmPelsWidth,  oDevMode.dmPelsHeight,
                             SWP_SHOWWINDOW);
                SetWindowPos(hWnd, NULL,
                             0, 0,
                             oDevMode.dmPelsWidth,  oDevMode.dmPelsHeight,
                             SWP_NOZORDER);
                return;
            }
        }
    }
}

extern "C" int D3D12IsHdrActive(void)
{
    return g12_hdrActive ? 1 : 0;
}

/* Returns 1 if any DXGI output reports HDR (= "Use HDR" is on in Windows). */
extern "C" int D3D12IsSystemHdrEnabled(void)
{
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 0;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            ComPtr<IDXGIOutput6> output6;
            if (SUCCEEDED(output.As(&output6))) {
                DXGI_OUTPUT_DESC1 odesc = {};
                if (SUCCEEDED(output6->GetDesc1(&odesc)) &&
                    odesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Returns 0=SDR, 1=HDR scRGB linear, 2=HDR10 PQ. Properties dialog uses this. */
extern "C" int D3D12HdrMode(void)
{
    if (!g12_hdrActive) return 0;
    if (g12_hdrColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) return 2;
    return 1;
}

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
    g12_isFullscreen = true;
    return 0; // DXE_OK
}

BOOL D3D12EnterWindowedMode(HWND /*hwnd*/, int /*width*/, int /*height*/,
                             int /*useVideoBackBuffer*/, int /*useSysMemBuffering*/)
{
    // No-op: windowed transitions are handled entirely by archUpdateWindow's
    // SetWindowPos in themeSet plus D3D12UpdateSurface's auto-resize.
    g12_isFullscreen = false;
    return 0; // DXE_OK
}

int D3D12UpdateWindowedMode(HWND /*hwnd*/, int width, int height,
                             int /*useVideoBackBuffer*/, int /*useSysMemBuffering*/)
{
    vD3D12Resize(width, height);
    return 0;
}

// Post-render recording readback: Begin/CaptureFromFrame/End allocate
// an offscreen RT + READBACK staging buffer, re-run the display PSO,
// and copy to the caller's BGRA32 destination.  We bypass the swap
// quad is full NDC (-1..1) with UVs spanning srcUvMin..srcUvMax from the cached
// CB. That means the recording RT sees the same MSX 4:3 native frame regardless
// of whatever aspect ratio the visible window is using.

extern "C" int D3D12RecordBegin(int width, int height, int hdr)
{
    if (!g12_ready || !g12_device) return 0;
    if (width <= 0 || height <= 0) return 0;

    // Remember dims so vD3D12Cleanup -> bD3D12Init can re-create resources
    // automatically after a host-driven device reset (e.g. zoom change /
    // syncVblank-mode flip in the render loop).
    g12_recSavedW     = width;
    g12_recSavedH     = height;
    g12_recSavedHdr   = hdr ? 1 : 0;
    g12_recAutoRebind = true;

    if (!allocRecordResources(width, height, g12_recSavedHdr)) {
        g12_recAutoRebind = false;
        return 0;
    }
    return 1;
}

static int allocRecordResources(int width, int height, int hdr)
{
    // If a previous recording session left state behind, clean it up first.
    releaseRecordResources();

    HRESULT hr;

    // ---- Render target (DEFAULT heap) -----
    // SDR: B8G8R8A8 -- the readback maps directly to MFVideoFormat_RGB32.
    // HDR: R10G10B10A2_UNORM -- the shader writes PQ-encoded values; the
    //      DXGI native bit layout (bits 0-9 R, 10-19 G, 20-29 B, 30-31 A)
    //      matches MFVideoFormat_A2R10G10B10's underlying D3DFMT_A2B10G10R10
    //      so the readback memory can be fed directly to MF without swizzle.
    DXGI_FORMAT recFormat = hdr ? DXGI_FORMAT_R10G10B10A2_UNORM
                                : DXGI_FORMAT_B8G8R8A8_UNORM;
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = (UINT64)width;
        rd.Height             = (UINT)height;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = recFormat;
        rd.SampleDesc.Count   = 1;
        rd.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv  = {};
        cv.Format             = recFormat;
        cv.Color[3]           = 1.0f;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &cv, IID_PPV_ARGS(&g12_recRT));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
    }

    // ---- RTV heap with one slot for the record RT -----
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = 1;
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hr = g12_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g12_recRtvHeap));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
        g12_device->CreateRenderTargetView(
            g12_recRT.Get(), nullptr,
            g12_recRtvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // ---- READBACK buffer big enough for one frame -----
    g12_recRowPitchAligned =
        (UINT)((width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
               & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = (UINT64)g12_recRowPitchAligned * (UINT64)height;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&g12_recReadback));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
    }

    // ---- Dedicated cmd allocator + cmd list -----
    hr = g12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&g12_recCmdAlloc));
    if (FAILED(hr)) { releaseRecordResources(); return 0; }
    hr = g12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       g12_recCmdAlloc.Get(), nullptr,
                                       IID_PPV_ARGS(&g12_recCmdList));
    if (FAILED(hr)) { releaseRecordResources(); return 0; }
    g12_recCmdList->Close();

    // ---- Dedicated fence -----
    hr = g12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g12_recFence));
    if (FAILED(hr)) { releaseRecordResources(); return 0; }
    g12_recFenceCtr   = 0;
    g12_recFenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g12_recFenceEvent) { releaseRecordResources(); return 0; }

    // ---- Vertex buffer (4 verts, full-NDC quad; UVs filled per-capture) -----
    {
        const UINT vtxSize = sizeof(Vtx12) * 4;
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = vtxSize;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&g12_recVtxBuf));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
        D3D12_RANGE rr = { 0, 0 };  // never read on CPU
        hr = g12_recVtxBuf->Map(0, &rr, (void**)&g12_recVtxPtr);
        if (FAILED(hr) || !g12_recVtxPtr) { releaseRecordResources(); return 0; }
        g12_recVtxView.BufferLocation = g12_recVtxBuf->GetGPUVirtualAddress();
        g12_recVtxView.SizeInBytes    = vtxSize;
        g12_recVtxView.StrideInBytes  = sizeof(Vtx12);
    }

    // ---- Record-specific CBV (CB_SIZE bytes, persistently CPU-mapped) -----
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = CB_SIZE;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&g12_recCbuf));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
        D3D12_RANGE rr = { 0, 0 };
        hr = g12_recCbuf->Map(0, &rr, (void**)&g12_recCbufPtr);
        if (FAILED(hr) || !g12_recCbufPtr) { releaseRecordResources(); return 0; }
    }

    // ---- Capture-owned source texture (TEX_W x TEX_H BGRA8) -----
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = TEX_W;
        rd.Height           = TEX_H;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&g12_recSrcTexture));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
    }

    // ---- Upload buffer for the source texture (CPU-mapped, 256-aligned rows) --
    g12_recUploadRowPitch =
        (UINT)((TEX_W * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
               & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = (UINT64)g12_recUploadRowPitch * TEX_H;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&g12_recUploadBuf));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
        D3D12_RANGE rr = { 0, 0 };
        hr = g12_recUploadBuf->Map(0, &rr, (void**)&g12_recUploadPtr);
        if (FAILED(hr) || !g12_recUploadPtr) { releaseRecordResources(); return 0; }
    }

    // ---- Capture-owned noise mask texture (R8) -----
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = TEX_W;
        rd.Height           = TEX_H;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_R8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&g12_recNoiseTexture));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
    }

    // ---- Noise upload buffer -----
    g12_recNoiseUploadRowPitch =
        (UINT)((TEX_W + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
               & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = (UINT64)g12_recNoiseUploadRowPitch * TEX_H;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = g12_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&g12_recNoiseUploadBuf));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }
        D3D12_RANGE rr = { 0, 0 };
        hr = g12_recNoiseUploadBuf->Map(0, &rr, (void**)&g12_recNoiseUploadPtr);
        if (FAILED(hr) || !g12_recNoiseUploadPtr) { releaseRecordResources(); return 0; }
    }

    // ---- Capture SRV heap: 3 slots matching the live root signature --------
    // slot 0: recSrcTexture (current frame source)
    // slot 1: recSrcTexture (dummy; recording always sets blendFrames=0 so the
    //         shader never samples this slot, but the descriptor must point at
    //         a real PSR resource for binding validity)
    // slot 2: recNoiseTexture (own PRNG state, advanced per capture)
    {
        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.NumDescriptors = 3;
        d.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = g12_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g12_recSrvHeap));
        if (FAILED(hr)) { releaseRecordResources(); return 0; }

        UINT srvSize = g12_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE h = g12_recSrvHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvBgra = {};
        srvBgra.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvBgra.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvBgra.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvBgra.Texture2D.MipLevels     = 1;

        g12_device->CreateShaderResourceView(g12_recSrcTexture.Get(), &srvBgra, h);
        h.ptr += srvSize;
        g12_device->CreateShaderResourceView(g12_recSrcTexture.Get(), &srvBgra, h);
        h.ptr += srvSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvR8 = {};
        srvR8.Format                  = DXGI_FORMAT_R8_UNORM;
        srvR8.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvR8.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvR8.Texture2D.MipLevels     = 1;
        g12_device->CreateShaderResourceView(g12_recNoiseTexture.Get(), &srvR8, h);
    }

    g12_recW = width;
    g12_recH = height;
    return 1;
}

extern "C" int D3D12RecordCaptureFromFrame(FrameBuffer* fb, Video* pVideo,
                                           D3DProperties* props,
                                           void* dstBgra32, int dstPitch)
{
    if (!g12_ready || !g12_device || !g12_recRT || !g12_recSrcTexture) return 0;
    // Mapped upload pointers must be valid -- a Map failure during alloc would
    // leave them null even with the resources non-null.
    if (!g12_recUploadPtr || !g12_recCbufPtr || !g12_recVtxPtr) return 0;
    if (!fb || !pVideo || !props || !dstBgra32) return 0;

    // 1) Source extent + deinterlace (mirrors live D3D12UpdateSurface).
    bool isInterlacedSource = (fb->interlace != INTERLACE_NONE);
    if (isInterlacedSource && pVideo->deInterlace)
        fb = frameBufferDeinterlace(fb);

    int  srcW             = fb->maxWidth;
    bool frameDoubleWidth = false;
    for (int y = 0; y < fb->lines; y++) {
        if (fb->line[y].doubleWidth) { srcW *= 2; frameDoubleWidth = true; break; }
    }
    int srcH = fb->lines;

    // 2) ARGB1555 -> BGRA8 into the capture-owned upload buffer.
    memset(g12_recUploadPtr, 0, (size_t)g12_recUploadRowPitch * (size_t)TEX_H);
    {
        int lines     = fb->lines;
        int startLine = (fb->interlace == INTERLACE_ODD) ? 1 : 0;
        for (int y = 0; y < lines; y++) {
            UINT8* dstRow = g12_recUploadPtr
                          + (UINT)((y + startLine) * (int)g12_recUploadRowPitch);
            const UINT16* srcRow = fb->line[y].buffer;
            int rowSrcW = fb->maxWidth;
            if (fb->line[y].doubleWidth) rowSrcW *= 2;
            // Mid-frame SM5->SM7 mix: duplicate single-width pixels so the
            // right half of the texture isn't memset(0)=black (same fix as
            // D3D12UpdateSurface above).
            int stride = (frameDoubleWidth && !fb->line[y].doubleWidth) ? 2 : 1;

            UINT32* d32 = (UINT32*)dstRow;
            for (int x = 0; x < rowSrcW; x++) {
                UINT16 p = srcRow[x];
                UINT32 r = (p >> 10) & 0x1F; r = (r << 3) | (r >> 2);
                UINT32 g = (p >>  5) & 0x1F; g = (g << 3) | (g >> 2);
                UINT32 b = (p >>  0) & 0x1F; b = (b << 3) | (b >> 2);
                UINT32 v = (0xFFu << 24) | (r << 16) | (g << 8) | b;
                d32[x * stride] = v;
                if (stride > 1) d32[x * stride + 1] = v;
            }
        }
    }

    // 3) Noise mask (only when palMode requests it). Same PRNG mechanic as
    //    live render, but advanced by a capture-local counter so the live
    //    path's noise pattern isn't perturbed by recording activity.
    bool needsNoise = (pVideo->palMode == VIDEO_PAL_SHARP_NOISE ||
                       pVideo->palMode == VIDEO_PAL_BLUR_NOISE);
    if (needsNoise) {
        g12_recNoiseRndVal *= 13;
        UINT32 rnd      = g12_recNoiseRndVal;
        UINT8* noiseDst = g12_recNoiseUploadPtr;
        int    nPitch   = (int)g12_recNoiseUploadRowPitch;
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

    // 4) Build CB. Most fields mirror live; recording-specific overrides:
    //    - blendFramesEnable = 0 (no prev-frame state across captures)
    //    - UV / scanUvPeriod come from the same AR + crop helper the live
    //      path uses, so the user's Properties->Video aspect-ratio setting
    //      (NTSC PAR / PAL PAR / 1:1 / Auto / Stretch) is honoured. The
    //      visible source region inside the record RT is letterboxed /
    //      pillarboxed by the shader's border-color clamp.
    UvRender12 uv = computeUvForRender(g12_recW, g12_recH, srcW, srcH, props);
    /* Mirror the live path's AA / legacy mode selection. */
    float vSpanRec = uv.v1 - uv.v0;
    float pxPerRowRec = (vSpanRec > 1e-6f)
                      ? ((float)g12_recH / (vSpanRec * (float)TEX_H))
                      : 0.0f;
    bool useScanAARec    = (pxPerRowRec >= 2.5f);
    float scanUvPeriodRec = useScanAARec
                            ? (1.0f / (float)TEX_H)
                            : ((srcH > 0) ? (vSpanRec / (float)srcH)
                                          : (1.0f / (float)TEX_H));
    float scanUvOffsetRec = useScanAARec ? 0.0f : uv.v0;
    {
        EffectCB12 cb = {};
        bool scanlinesActive = pVideo->scanLinesEnable
                               && !isInterlacedSource
                               && !fb->interlaceRaster;
        cb.scanLinesEnable    = scanlinesActive ? 1.0f : 0.0f;
        cb.scanLinesIntensity = (float)pVideo->scanLinesPct / 100.0f;
        /* Mirror the live path: HDR uses a global paperWhite lift
        ** (see hdrPaperWhiteNits below), SDR uses per-pixel comp. */
        cb.scanLinesBrightComp = scanlinesActive
            ? computeScanLineBrightComp(pVideo)
            : 1.0f;
        /* HDR mode: 2 = HDR10 PQ (BT.709 in BT.2020 metadata),
        ** 0 = SDR (BGRA8, no PQ encoding). */
        Properties* gpForHdr = propGetGlobalProperties();
        cb.hdrMode            = g12_recSavedHdr ? 2.0f : 0.0f;
        {
            float pw = gpForHdr ? (float)gpForHdr->video.hdrPaperWhiteNits : 200.0f;
            /* Pre-attenuate paperWhite 15% so HDR playback matches the live look. */
            if (g12_recSavedHdr) {
                pw *= 0.85f;
            }
            if (g12_recSavedHdr && scanlinesActive) {
                // Recording mirrors the live render's mode -- if the user
                // is in fullscreen the recording captures with the
                // fullscreen comp curve, and likewise for windowed.  The
                // recording goes to the user's expected look at capture
                // time rather than picking a "neutral" comp.
                pw *= computeScanLinePaperWhiteBoost(pVideo, g12_isFullscreen, pxPerRowRec);
                cb.darkBoostRatio = computeScanLineDarkBoost(pVideo, g12_isFullscreen, pxPerRowRec);
            } else if (g12_recSavedHdr
                       && pVideo->scanLinesEnable
                       && (isInterlacedSource || fb->interlaceRaster)) {
                // Interlace brightness lift, mirroring the live path: on
                // a real CRT, interlaced content lights the gap rows via
                // the alternate field and ends up perceptually brighter
                // than progressive.  HDR has the headroom for the lift;
                // SDR-recording leaves paperWhite alone.
                pw *= 1.20f;
                cb.darkBoostRatio = 1.0f;
            } else {
                cb.darkBoostRatio = 1.0f;
            }
            if (pw > 800.0f) pw = 800.0f;
            cb.hdrPaperWhiteNits = pw;
        }
        cb.scanUvPeriod       = scanUvPeriodRec;
        cb.scanUvOffset       = scanUvOffsetRec;
        cb.scanUseAA          = useScanAARec ? 1.0f : 0.0f;
        {
            int spct = pVideo->scanlinesShapePct;
            if (spct < 0)   spct = 0;
            if (spct > 100) spct = 100;
            cb.scanShapeP = (float)spct / 100.0f * 4.0f;
        }
        cb.scalingFilter = (float)props->scalingFilter;
        cb.blendFramesEnable  = 0.0f;
        cb.gammaExp           = (float)pVideo->gamma;
        cb.contrast           = (float)pVideo->contrast;
        cb.saturation         = (float)pVideo->saturation;
        cb.brightness         = (float)pVideo->brightness / 100.0f;
        cb.colorSatEnable     = pVideo->colorSaturationEnable ? 1.0f : 0.0f;
        float satW = (float)(pVideo->colorSaturationWidth < 1 ? 1 : pVideo->colorSaturationWidth);
        cb.colorSatWidth      = satW / (float)TEX_W;

        Properties* gp = propGetGlobalProperties();
        cb.monitorColor       = gp ? (float)gp->video.monitorColor : 0.0f;

        if (gp && gp->video.d3d.extendBorderColor && fb->line[0].buffer) {
            unsigned int b16 = (unsigned int)fb->line[0].buffer[0];
            cb.borderR = ((b16 >> 10) & 0x1F) / 31.0f;
            cb.borderG = ((b16 >>  5) & 0x1F) / 31.0f;
            cb.borderB = ((b16      ) & 0x1F) / 31.0f;
        } else {
            cb.borderR = cb.borderG = cb.borderB = 0.0f;
        }
        cb.borderA = 1.0f;

        /* Crop UV bounds (same as live path): letterbox area renders borderColor. */
        cb.srcUvMinU = uv.cropU0;
        cb.srcUvMinV = uv.cropV0;
        cb.srcUvMaxU = uv.cropU1;
        cb.srcUvMaxV = uv.cropV1;

        cb.palMode         = (float)(int)pVideo->palMode;
        cb.srcWf           = (float)srcW;
        cb.srcHf           = (float)fb->lines;
        cb.texWf           = (float)TEX_W;
        cb.texHf           = (float)TEX_H;
        cb.doubleWidthFlag = frameDoubleWidth ? 1.0f : 0.0f;

        memcpy(g12_recCbufPtr, &cb, sizeof(cb));

        // Vertex buffer: full-NDC quad with the AR-corrected UV bounds. UV
        // values outside [srcUvMinU,srcUvMaxU] x [srcUvMinV,srcUvMaxV] are
        // replaced with the border colour by the shader (same letterbox /
        // pillarbox behaviour as the live render).
        Vtx12 verts[4] = {
            { -1.0f, +1.0f, uv.u0, uv.v0 },
            { +1.0f, +1.0f, uv.u1, uv.v0 },
            { -1.0f, -1.0f, uv.u0, uv.v1 },
            { +1.0f, -1.0f, uv.u1, uv.v1 },
        };
        memcpy(g12_recVtxPtr, verts, sizeof(verts));
    }

    // 5) Build the command list.
    //    PSO selection: pick the PSO whose RTV format matches the record RT.
    //      g12_recSavedHdr=1 -> R10G10B10A2_UNORM RTV -> g12_psoRecHdr
    //      g12_recSavedHdr=0 + live HDR active -> BGRA8 RTV but live PSO has
    //                          FP16/PQ format -> need g12_psoRec
    //      g12_recSavedHdr=0 + live SDR -> live and record both BGRA8 ->
    //                          reuse g12_pso to avoid per-frame state switch
    ID3D12PipelineState* recPso = g12_recSavedHdr ? g12_psoRecHdr.Get()
                                                  : (g12_hdrActive ? g12_psoRec.Get()
                                                                   : g12_pso.Get());
    g12_recCmdAlloc->Reset();
    g12_recCmdList->Reset(g12_recCmdAlloc.Get(), recPso);

    auto barrier = [&](ID3D12Resource* res,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER rb = {};
        rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Transition.pResource   = res;
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb.Transition.StateBefore = before;
        rb.Transition.StateAfter  = after;
        g12_recCmdList->ResourceBarrier(1, &rb);
    };

    // -- Source texture: PSR -> COPY_DEST, upload, COPY_DEST -> PSR ---------
    barrier(g12_recSrcTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_DEST);
    {
        D3D12_TEXTURE_COPY_LOCATION dl = {};
        dl.pResource        = g12_recSrcTexture.Get();
        dl.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dl.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION sl = {};
        sl.pResource                          = g12_recUploadBuf.Get();
        sl.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sl.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_B8G8R8A8_UNORM;
        sl.PlacedFootprint.Footprint.Width    = TEX_W;
        sl.PlacedFootprint.Footprint.Height   = TEX_H;
        sl.PlacedFootprint.Footprint.Depth    = 1;
        sl.PlacedFootprint.Footprint.RowPitch = g12_recUploadRowPitch;
        g12_recCmdList->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    }
    barrier(g12_recSrcTexture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // -- Optional noise upload ----------------------------------------------
    if (needsNoise) {
        barrier(g12_recNoiseTexture.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dl = {};
        dl.pResource        = g12_recNoiseTexture.Get();
        dl.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dl.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION sl = {};
        sl.pResource                          = g12_recNoiseUploadBuf.Get();
        sl.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sl.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
        sl.PlacedFootprint.Footprint.Width    = TEX_W;
        sl.PlacedFootprint.Footprint.Height   = TEX_H;
        sl.PlacedFootprint.Footprint.Depth    = 1;
        sl.PlacedFootprint.Footprint.RowPitch = g12_recNoiseUploadRowPitch;
        g12_recCmdList->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
        barrier(g12_recNoiseTexture.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // -- Record RT: PSR -> RENDER_TARGET, draw, RT -> COPY_SOURCE -----------
    barrier(g12_recRT.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g12_recRtvHeap->GetCPUDescriptorHandleForHeapStart();
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g12_recCmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    g12_recCmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)g12_recW, (float)g12_recH, 0.0f, 1.0f };
    D3D12_RECT     sc = { 0, 0, (LONG)g12_recW, (LONG)g12_recH };
    g12_recCmdList->RSSetViewports(1, &vp);
    g12_recCmdList->RSSetScissorRects(1, &sc);

    g12_recCmdList->SetPipelineState(recPso);
    g12_recCmdList->SetGraphicsRootSignature(g12_rootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { g12_recSrvHeap.Get(), g12_samplerHeap.Get() };
    g12_recCmdList->SetDescriptorHeaps(2, heaps);
    g12_recCmdList->SetGraphicsRootDescriptorTable(
        0, g12_recSrvHeap->GetGPUDescriptorHandleForHeapStart());

    D3D12_GPU_DESCRIPTOR_HANDLE samp = g12_samplerHeap->GetGPUDescriptorHandleForHeapStart();
    if (props->scalingFilter == P_D3D_SCALE_BILINEAR) samp.ptr += g12_samplerSize;
    g12_recCmdList->SetGraphicsRootDescriptorTable(1, samp);

    g12_recCmdList->SetGraphicsRootConstantBufferView(
        2, g12_recCbuf->GetGPUVirtualAddress());

    g12_recCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g12_recCmdList->IASetVertexBuffers(0, 1, &g12_recVtxView);
    g12_recCmdList->DrawInstanced(4, 1, 0, 0);

    barrier(g12_recRT.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_SOURCE);

    // -- CopyTextureRegion into the readback buffer -------------------------
    // The destination footprint format must match the source RT format,
    // otherwise D3D12 validation rejects the copy and the GPU can TDR.
    {
        D3D12_TEXTURE_COPY_LOCATION sl = {};
        sl.pResource        = g12_recRT.Get();
        sl.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        sl.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dl = {};
        dl.pResource                          = g12_recReadback.Get();
        dl.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dl.PlacedFootprint.Footprint.Format   = g12_recSavedHdr
                                                ? DXGI_FORMAT_R10G10B10A2_UNORM
                                                : DXGI_FORMAT_B8G8R8A8_UNORM;
        dl.PlacedFootprint.Footprint.Width    = (UINT)g12_recW;
        dl.PlacedFootprint.Footprint.Height   = (UINT)g12_recH;
        dl.PlacedFootprint.Footprint.Depth    = 1;
        dl.PlacedFootprint.Footprint.RowPitch = g12_recRowPitchAligned;
        g12_recCmdList->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    }

    barrier(g12_recRT.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    g12_recCmdList->Close();

    // 6) Submit + wait. The shared g12_cmdQueue serialises against live
    //    render submissions; we use our own fence so live's WaitForFrame
    //    isn't perturbed.
    {
        ID3D12CommandList* lists[] = { g12_recCmdList.Get() };
        g12_cmdQueue->ExecuteCommandLists(1, lists);
    }

    ++g12_recFenceCtr;
    g12_cmdQueue->Signal(g12_recFence.Get(), g12_recFenceCtr);
    if (g12_recFence->GetCompletedValue() < g12_recFenceCtr) {
        g12_recFence->SetEventOnCompletion(g12_recFenceCtr, g12_recFenceEvent);
        DWORD wr = WaitForSingleObject(g12_recFenceEvent, 1000);
        if (wr != WAIT_OBJECT_0) return 0;
    }

    // 7) Map readback, memcpy bottom-up to the caller. MF SinkWriter's
    //    negative MF_MT_DEFAULT_STRIDE consumes the inverted layout.
    D3D12_RANGE readRange = { 0, (SIZE_T)g12_recRowPitchAligned * (SIZE_T)g12_recH };
    void* mapped = nullptr;
    HRESULT hr = g12_recReadback->Map(0, &readRange, &mapped);
    if (FAILED(hr)) return 0;

    const UINT rowBytes = (UINT)g12_recW * 4;
    UINT8*       dstRow = (UINT8*)dstBgra32;
    if (g12_recSavedHdr) {
        /* HDR: copy top-down (MF A2R10G10B10 input ignores negative stride). */
        const UINT8* srcRow = (const UINT8*)mapped;
        for (int y = 0; y < g12_recH; y++) {
            memcpy(dstRow, srcRow, rowBytes);
            srcRow += g12_recRowPitchAligned;
            dstRow += dstPitch;
        }
    } else {
        /* SDR: copy bottom-up (MF SinkWriter's negative stride takes inverted). */
        const UINT8* srcRow = (const UINT8*)mapped + (UINT)(g12_recH - 1) * g12_recRowPitchAligned;
        for (int y = 0; y < g12_recH; y++) {
            memcpy(dstRow, srcRow, rowBytes);
            srcRow -= g12_recRowPitchAligned;
            dstRow += dstPitch;
        }
    }

    D3D12_RANGE writeRange = { 0, 0 };
    g12_recReadback->Unmap(0, &writeRange);
    return 1;
}

extern "C" void D3D12RecordEnd(void)
{
    // User-explicit stop: release resources AND clear the auto-rebind hook so
    // a later device teardown / re-init doesn't zombie the recorder back in.
    g12_recAutoRebind = false;
    g12_recSavedW     = 0;
    g12_recSavedH     = 0;
    releaseRecordResources();
}

// --- HDR offline-preview swap chain ----------------------------------------
//
// Bound to a child HWND in the render-progress dialog so the user can see
// HDR-encoded captured frames as actual HDR (GDI's preview path is SDR-only).
// Sized at recording resolution (= g12_recRT) so a single CopyResource into
// the swap chain backbuffer is exact; DXGI_SCALING_STRETCH at Present time
// scales to whatever the visible HWND size is.

extern "C" int D3D12HdrPreviewBegin(HWND hwnd, int width, int height)
{
    if (!g12_ready || !g12_device || !hwnd) return 0;
    if (!g12_recRT || g12_recSavedHdr == 0) return 0;
    if (g12_previewActive.load(std::memory_order_acquire)) return 1;  /* already running */

    HRESULT hr;

    /* DXGI factory -- reuse the live swap chain's adapter via the device. */
    ComPtr<IDXGIFactory4> factory;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return 0;

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width            = (UINT)width;
    scd.Height           = (UINT)height;
    scd.Format           = DXGI_FORMAT_R10G10B10A2_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount      = 2;
    scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Scaling          = DXGI_SCALING_STRETCH;
    scd.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> sc1;
    hr = factory->CreateSwapChainForHwnd(g12_cmdQueue.Get(), hwnd,
                                         &scd, nullptr, nullptr, &sc1);
    if (FAILED(hr)) return 0;
    /* Disable Alt-Enter -- we don't want the preview hijacking fullscreen. */
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    hr = sc1.As(&g12_previewSwap);
    if (FAILED(hr)) { sc1.Reset(); return 0; }

    /* Set HDR10 PQ; fall back to default sRGB if the monitor / DWM rejects it. */
    UINT cssup = 0;
    if (SUCCEEDED(g12_previewSwap->CheckColorSpaceSupport(
            DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &cssup))
        && (cssup & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
        g12_previewSwap->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    }

    for (int i = 0; i < 2; i++) {
        hr = g12_previewSwap->GetBuffer(i, IID_PPV_ARGS(&g12_previewBackbuffers[i]));
        if (FAILED(hr)) { D3D12HdrPreviewEnd(); return 0; }
    }

    hr = g12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&g12_previewCmdAlloc));
    if (FAILED(hr)) { D3D12HdrPreviewEnd(); return 0; }
    hr = g12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       g12_previewCmdAlloc.Get(), nullptr,
                                       IID_PPV_ARGS(&g12_previewCmdList));
    if (FAILED(hr)) { D3D12HdrPreviewEnd(); return 0; }
    g12_previewCmdList->Close();

    hr = g12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g12_previewFence));
    if (FAILED(hr)) { D3D12HdrPreviewEnd(); return 0; }
    g12_previewFenceCtr   = 0;
    g12_previewFenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g12_previewFenceEvent) { D3D12HdrPreviewEnd(); return 0; }

    g12_previewActive.store(true, std::memory_order_release);
    return 1;
}

extern "C" int D3D12HdrPreviewBlit(void)
{
    /* Acquire-load pairs with End's release store; previewSwap check
    ** guards against a null-swap teardown that skips the flag flip. */
    if (!g12_previewActive.load(std::memory_order_acquire)
        || !g12_recRT || !g12_previewSwap) return 0;

    UINT idx = g12_previewSwap->GetCurrentBackBufferIndex();
    ID3D12Resource* back = g12_previewBackbuffers[idx].Get();
    if (!back) return 0;

    g12_previewCmdAlloc->Reset();
    g12_previewCmdList->Reset(g12_previewCmdAlloc.Get(), nullptr);

    auto barrier = [&](ID3D12Resource* res,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER rb = {};
        rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Transition.pResource   = res;
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb.Transition.StateBefore = before;
        rb.Transition.StateAfter  = after;
        g12_previewCmdList->ResourceBarrier(1, &rb);
    };

    /* g12_recRT was left in PIXEL_SHADER_RESOURCE by D3D12RecordCaptureFromFrame. */
    barrier(g12_recRT.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(back,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_COPY_DEST);

    g12_previewCmdList->CopyResource(back, g12_recRT.Get());

    barrier(back,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT);
    barrier(g12_recRT.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    g12_previewCmdList->Close();

    ID3D12CommandList* lists[] = { g12_previewCmdList.Get() };
    g12_cmdQueue->ExecuteCommandLists(1, lists);

    ++g12_previewFenceCtr;
    g12_cmdQueue->Signal(g12_previewFence.Get(), g12_previewFenceCtr);
    if (g12_previewFence->GetCompletedValue() < g12_previewFenceCtr) {
        g12_previewFence->SetEventOnCompletion(g12_previewFenceCtr, g12_previewFenceEvent);
        DWORD wr = WaitForSingleObject(g12_previewFenceEvent, 1000);
        if (wr != WAIT_OBJECT_0) return 0;
    }

    g12_previewSwap->Present(0, 0);
    return 1;
}

extern "C" void D3D12HdrPreviewEnd(void)
{
    /* Publish "not active" before teardown so concurrent Blit sees the
    ** flip and bails; release-store pairs with Blit's acquire-load. */
    g12_previewActive.store(false, std::memory_order_release);

    if (g12_previewFence && g12_previewFenceCtr > 0
        && g12_previewFence->GetCompletedValue() < g12_previewFenceCtr) {
        g12_previewFence->SetEventOnCompletion(g12_previewFenceCtr, g12_previewFenceEvent);
        WaitForSingleObject(g12_previewFenceEvent, 1000);
    }
    g12_previewCmdList.Reset();
    g12_previewCmdAlloc.Reset();
    for (int i = 0; i < 2; i++) g12_previewBackbuffers[i].Reset();
    g12_previewSwap.Reset();
    g12_previewFence.Reset();
    if (g12_previewFenceEvent) {
        CloseHandle(g12_previewFenceEvent);
        g12_previewFenceEvent = NULL;
    }
    g12_previewFenceCtr = 0;
}

static void releaseRecordResources(void)
{
    // Make sure no pending GPU work is referencing what we're about to free.
    // Capture's own internal fence wait already returns synchronously, so
    // GetCompletedValue should already match -- but if a Capture failed
    // mid-flight (e.g. Map() failed) the Signal may have raced ahead. Cap
    // the wait at 1 second to avoid a zombie record session hanging the
    // next "Render to video" attempt.
    if (g12_recFence && g12_recFenceCtr > 0
        && g12_recFence->GetCompletedValue() < g12_recFenceCtr) {
        g12_recFence->SetEventOnCompletion(g12_recFenceCtr, g12_recFenceEvent);
        WaitForSingleObject(g12_recFenceEvent, 1000);
    }

    g12_recVtxBuf.Reset();         g12_recVtxPtr  = nullptr;
    g12_recCbuf.Reset();           g12_recCbufPtr = nullptr;
    g12_recUploadBuf.Reset();      g12_recUploadPtr = nullptr;
    g12_recNoiseUploadBuf.Reset(); g12_recNoiseUploadPtr = nullptr;
    g12_recSrcTexture.Reset();
    g12_recNoiseTexture.Reset();
    g12_recSrvHeap.Reset();
    g12_recReadback.Reset();
    g12_recRT.Reset();
    g12_recRtvHeap.Reset();
    g12_recCmdList.Reset();
    g12_recCmdAlloc.Reset();
    if (g12_recFenceEvent) { CloseHandle(g12_recFenceEvent); g12_recFenceEvent = NULL; }
    g12_recFence.Reset();
    g12_recFenceCtr            = 0;
    g12_recRowPitchAligned     = 0;
    g12_recUploadRowPitch      = 0;
    g12_recNoiseUploadRowPitch = 0;
    g12_recNoiseRndVal         = 51;
    g12_recW = 0;
    g12_recH = 0;
}


