/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32Bitmap.c,v $
**
** $Revision: 1.8 $
**
** $Date: 2008-03-30 18:38:48 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
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
#include "ArchBitmap.h"
#include "ArchText.h"
#include <windows.h>
#include <stdio.h>
#include "Win32TextUtf8.h"

// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"

struct ArchText {
    HFONT hFont;
    int height;
    int right;
    int color;
};

int archRGB(int r, int g, int b)
{
    return RGB(r, g, b);
}

ArchText* archTextCreate(int height, int color, int rightAligned)
{
    ArchText* text = malloc(sizeof(ArchText));
    LOGFONT lf = { 0 };

    lf.lfHeight  = height;
    lf.lfCharSet = DEFAULT_CHARSET;

    text->height = height;
    text->color  = color;
    text->right  = rightAligned;
    text->hFont  = CreateFontIndirect(&lf);

    return text;
}

void archTextDestroy(ArchText* text)
{
    DeleteObject(text->hFont);
    free(text);
}

void archTextDraw(ArchText* text, void* dcDest, int xDest, int yDest, int width, int height, char* string)
{
    HDC hOldFont;

    SetBkMode(dcDest, TRANSPARENT);
    SetTextColor(dcDest, text->color);
    
    if (hOldFont = SelectObject(dcDest, text->hFont)) {
        TEXTMETRIC tm;
        if (GetTextMetrics(dcDest, &tm)) {
            text->height = tm.tmHeight;
        }
        {
            RECT rect = { xDest, yDest, xDest + width, yDest + text->height };
            DrawTextU(dcDest, string, (int)strlen(string), &rect, text->right ? DT_RIGHT : DT_LEFT);
            SelectObject(dcDest, hOldFont); 
        }
    }
}

struct ArchBitmap {
    HBITMAP hBitmapOrig;
    HBITMAP hBitmap;
    HDC hMemDC;
    int width;
    int height;
};

static ArchBitmap* bitmapCreate(void* bitmap) 
{
    ArchBitmap* bm;
    BITMAP bmp;
    HDC screenDC;
    
    /* NULL HBITMAP (typically GDI quota exhausted) -> NULL ArchBitmap*
       so callers can recover instead of AV'ing on next access. */
    if (bitmap == NULL) {
        return NULL;
    }

    bm = malloc(sizeof(ArchBitmap));
    if (bm == NULL) {
        DeleteObject((HBITMAP)bitmap);
        return NULL;
    }
    bm->hBitmap = bitmap;

    /* Pair GetWindowDC with ReleaseDC; theme rebuilds otherwise leak DCs
       until the GDI quota is exhausted. */
    screenDC = GetWindowDC(NULL);
    bm->hMemDC = CreateCompatibleDC(screenDC);
    ReleaseDC(NULL, screenDC);
    if (bm->hMemDC == NULL) {
        DeleteObject((HBITMAP)bitmap);
        free(bm);
        return NULL;
    }
    bm->hBitmapOrig = (HBITMAP)SelectObject(bm->hMemDC, bm->hBitmap);
    GetObject(bm->hBitmap, sizeof(BITMAP), (PSTR)&bmp);
    bm->width  = bmp.bmWidth;
    bm->height = bmp.bmHeight;

    return bm;
}

ArchBitmap* archBitmapCreate(int width, int height)
{
    HDC screenDC = GetWindowDC(NULL);
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, width, height);
    ReleaseDC(NULL, screenDC);
    return bitmapCreate(hBitmap);
}

ArchBitmap* archBitmapCreateFromFile(const char* filename)
{
    HBITMAP hBitmap;

    if (pkg_file_exists(filename)) {
        // TODO: Make better bitmap loader for bitmaps in packages
        char* bitmap = NULL;
        int size = 0;
        FILE* f = fopen(filename, "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            if (size > 0) {
                bitmap = malloc(size);
                if (bitmap != NULL) {
                    fread(bitmap, 1, size, f);
                }
            }
            fclose(f);
        }
        if (bitmap != NULL) {
            FILE* f = fopen("tmp.bmp", "wb");
            if (f != NULL) {
                fwrite(bitmap, 1, size, f);
                fclose(f);
            }
            free(bitmap);
        }
        hBitmap = (HBITMAP)LoadImage(NULL, "tmp.bmp", IMAGE_BITMAP, 0, 0,
                                     LR_CREATEDIBSECTION | LR_DEFAULTSIZE | LR_LOADFROMFILE);
    }
    else {
        hBitmap = (HBITMAP)LoadImage(NULL, filename, IMAGE_BITMAP, 0, 0,
                                     LR_CREATEDIBSECTION | LR_DEFAULTSIZE | LR_LOADFROMFILE);
    }

    return bitmapCreate(hBitmap);
}

ArchBitmap* archBitmapCreateFromId(int id)
{
    HBITMAP hBitmap = (HBITMAP)LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(id));

    return bitmapCreate(hBitmap);
}

static ArchBitmap* bitmapCreateScaledCopyMode(ArchBitmap* src, int dstWidth, int dstHeight, int mode)
{
    HDC screenDC;
    HBITMAP hDst;
    ArchBitmap* dst;

    if (src == NULL || dstWidth <= 0 || dstHeight <= 0) {
        return NULL;
    }

    screenDC = GetDC(NULL);
    hDst = CreateCompatibleBitmap(screenDC, dstWidth, dstHeight);
    ReleaseDC(NULL, screenDC);
    if (hDst == NULL) {
        return NULL;
    }

    dst = bitmapCreate(hDst);
    if (dst == NULL) {
        DeleteObject(hDst);
        return NULL;
    }

    SetStretchBltMode(dst->hMemDC, mode);
    if (mode == HALFTONE) {
        /* HALFTONE requires SetBrushOrgEx for tiled output, otherwise
           subsequent fills can land on a fractional grid origin. */
        SetBrushOrgEx(dst->hMemDC, 0, 0, NULL);
    }
    StretchBlt(dst->hMemDC, 0, 0, dstWidth, dstHeight,
               src->hMemDC,  0, 0, src->width, src->height,
               SRCCOPY);
    return dst;
}

ArchBitmap* archBitmapCreateScaledCopy(ArchBitmap* src, int dstWidth, int dstHeight)
{
    /* COLORONCOLOR = nearest-neighbour stretch -- keeps the pixel-art look
       crisp at non-integer multiples (e.g. x6 from x2 base). */
    return bitmapCreateScaledCopyMode(src, dstWidth, dstHeight, COLORONCOLOR);
}

ArchBitmap* archBitmapCreateScaledCopySmooth(ArchBitmap* src, int dstWidth, int dstHeight)
{
    return bitmapCreateScaledCopyMode(src, dstWidth, dstHeight, HALFTONE);
}

void archBitmapDestroy(ArchBitmap* bm)
{
    if (bm == NULL) return;
    DeleteObject(SelectObject(bm->hMemDC, bm->hBitmapOrig));
    DeleteDC(bm->hMemDC);
    free(bm);
}

int archBitmapGetWidth(ArchBitmap* bm)
{
    return bm ? bm->width : 0;
}

int archBitmapGetHeight(ArchBitmap* bm)
{
    return bm ? bm->height : 0;
}

void archBitmapDraw(ArchBitmap* bm, void* dcDest, int xDest, int yDest, int xSrc, int ySrc, int width, int height)
{
    if (bm == NULL || dcDest == NULL) return;
    BitBlt(dcDest, xDest, yDest, width, height, bm->hMemDC, xSrc, ySrc, SRCCOPY);
}

void archBitmapCopy(ArchBitmap* dst, int xDest, int yDest, ArchBitmap* src, int xSrc, int ySrc, int width, int height)
{
    if (dst == NULL || src == NULL) return;
    archBitmapDraw(src, dst->hMemDC, xDest, yDest, xSrc, ySrc, width, height);
}

