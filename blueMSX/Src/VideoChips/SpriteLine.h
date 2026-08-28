/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/VideoChips/SpriteLine.h,v $
**
** $Revision: 1.36 $
**
** $Date: 2009-04-19 19:53:16 $
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
#ifndef SPRITE_LINE_H
#define SPRITE_LINE_H



typedef struct {
    int horizontalPos;
    int color;
    UInt16 pattern;
} SpriteAttribute;

static UInt8 colChckBuf[384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static UInt8 lineBufferNull[512];

#define nullSpritesLine() lineBufferNull

static UInt8 lineBuffer[2][384];
static UInt8* lineBufs[2] = { nullSpritesLine(), nullSpritesLine() };
static UInt8* lineBuf = nullSpritesLine();
static int nonVisibleLine = -1;


UInt8* spritesLine(VDP* vdp, int line) {
    int bufIndex;
    UInt8 collisionBuf[384];
    UInt8* attrib;
    UInt8* attribBase;
    UInt8* attribTable[32];
    int spriteLine[33];
    UInt8 patternMask;
    int idx;
    int step;
    int size;
    int scale;
    int visibleCnt;
    int collision;
    int dispLine;
    int solidColor;
    int color0Collides;

    idx = line;

    bufIndex = line & 1;

    line -= vdp->firstLine;

//    vdp->vdpStatus[0] &= 0xbf;

    if (idx == 0) {
        lineBufs[bufIndex] = nullSpritesLine();
        return nullSpritesLine();
    }

    if (!vdp->screenOn || (vdp->vdpStatus[2] & 0x40) || vdpIsSpritesOff(vdp->vdpRegs) || vdp->sprMode3) {
        lineBufs[bufIndex] = nullSpritesLine();
        return lineBufs[bufIndex ^ 1];
    }

    attribBase = &vdp->vram[vdp->sprTabBase & (-1 << 7)];
    size   = vdpIsSprites16x16(vdp->vdpRegs) ? 16 : 8;
    scale  = vdpIsSpritesBig(vdp->vdpRegs) ? 2 : 1;
    solidColor = vdpIsColor0Solid(vdp->vdpRegs) ? 1 : 0;
    /* A transparent color 0 sprite still collides on the TMS99x8 family, but not on the V99x8. */
    color0Collides = vdpIsMsx1Vdp(vdp);
    /* The line built here is the one shown next. */
    dispLine = line + 1;
    line   = (line + vdpSpriteVScroll(vdp)) & 0xff;
    
	patternMask = vdpIsSprites16x16(vdp->vdpRegs) ? 0xfc : 0xff;
    visibleCnt = 0;
    collision = 0;
    /* Find visible sprites on current line */
    for (step = 0; step < 32; step++) {
        idx    = vdpSpritePlane(vdp, step, 31);
        attrib = attribBase + 4 * idx;
        if (attrib[0] == 208 && !vdpIsSpriteShuffle(vdp)) {
            break;
        }

        spriteLine[visibleCnt] = ((line - attrib[0]) & 0xff) / scale;
		if (spriteLine[visibleCnt] >= size) {
#if 1
            // Disable sprite mirroring for now...
            continue;
#else
            if ((vdp->vdpRegs[3] & 0x40) == 0 && (vdp->vdpRegs[4] & 0x01) == 0 &&
                vdp->screenMode == 2 &&
                (vdp->vdpVersion == VDP_TMS9929A || vdp->vdpVersion == VDP_TMS99x8A || vdp->vdpVersion == VDP_TMS9918A)) 
            {
                if (line < 56) {
                    continue;
                }
                spriteLine[visibleCnt] = ((line - 64 - attrib[0]) & 0xff) / scale;
		        if (spriteLine[visibleCnt] >= size) {
                    if (line < 120) {
                        continue;
                    }
                    spriteLine[visibleCnt] = ((line - 128 - attrib[0]) & 0xff) / scale;
    		        if (spriteLine[visibleCnt] >= size) {
                        continue;
                    }
                }
            }
            else {
                continue;
            }
#endif
        }
        
        if (visibleCnt == vdpSpritesPerLine(vdp, 4)) {
			if ((vdp->vdpStatus[0] & 0xc0) == 0) {
				vdp->vdpStatus[0] = (vdp->vdpStatus[0] & 0xe0) | 0x40 | idx;
			}
			if (!noSpriteLimits)
				break;
        }

        attribTable[visibleCnt++] = attrib;
    }

    if (visibleCnt == 0) {
        lineBufs[bufIndex] = nullSpritesLine();
        return lineBufs[bufIndex ^ 1];
    }

	if ((vdp->vdpStatus[0] & 0xc0) == 0) {
		vdp->vdpStatus[0] = (vdp->vdpStatus[0] & 0xe0) | (idx < 32 ? idx : 31);
	}
    
    lineBuf = lineBuffer[bufIndex];
    memset(lineBuf, 0, 384);
    memset(collisionBuf, 0, 384);

    collision = 0;
    
    while (visibleCnt--) {
        UInt8  color;
        UInt8  rawColor;
        UInt8* patternPtr;
        UInt8  pattern;
        UInt8* linePtr;
        UInt8* colPtr;
        UInt8* colChck;
        int    colOffset;

        attrib     = attribTable[visibleCnt];

        colOffset = ((int)attrib[1] + 32 - ((attrib[3] >> 2) & 0x20));
        colPtr     = collisionBuf + colOffset;
        colChck    = colChckBuf   + colOffset;
        linePtr    = lineBuf      + colOffset;
        rawColor   = attrib[3] & 0x0f;
        /* Bit 0 marks the dot as painted so color 0 stays distinct from an empty line buffer. */
        color      = (rawColor << 1) | solidColor;
        patternPtr = &vdp->vram[(vdp->sprGenBase & (-1 << 11)) + ((int)(attrib[2] & patternMask) << 3) + spriteLine[visibleCnt]];

        if (!solidColor && rawColor == 0) {
            if (!color0Collides) {
                continue;
            }

            if (scale == 1) {
                pattern = patternPtr[0]; 
                if (pattern) {
                    if (pattern & 0x80) { collision |= colPtr[0]; colPtr[0] += colChck[0]; }
                    if (pattern & 0x40) { collision |= colPtr[1]; colPtr[1] += colChck[1]; }
                    if (pattern & 0x20) { collision |= colPtr[2]; colPtr[2] += colChck[2]; }
                    if (pattern & 0x10) { collision |= colPtr[3]; colPtr[3] += colChck[3]; }
                    if (pattern & 0x08) { collision |= colPtr[4]; colPtr[4] += colChck[4]; }
                    if (pattern & 0x04) { collision |= colPtr[5]; colPtr[5] += colChck[5]; }
                    if (pattern & 0x02) { collision |= colPtr[6]; colPtr[6] += colChck[6]; }
                    if (pattern & 0x01) { collision |= colPtr[7]; colPtr[7] += colChck[7]; }
                }

                if (vdpIsSprites16x16(vdp->vdpRegs)) {
                    pattern = patternPtr[16];

                    if (pattern) {
                        if (pattern & 0x80) { collision |= colPtr[8];  colPtr[8] += colChck[8]; }
                        if (pattern & 0x40) { collision |= colPtr[9];  colPtr[9] += colChck[9]; }
                        if (pattern & 0x20) { collision |= colPtr[10]; colPtr[10] += colChck[10]; }
                        if (pattern & 0x10) { collision |= colPtr[11]; colPtr[11] += colChck[11]; }
                        if (pattern & 0x08) { collision |= colPtr[12]; colPtr[12] += colChck[12]; }
                        if (pattern & 0x04) { collision |= colPtr[13]; colPtr[13] += colChck[13]; }
                        if (pattern & 0x02) { collision |= colPtr[14]; colPtr[14] += colChck[14]; }
                        if (pattern & 0x01) { collision |= colPtr[15]; colPtr[15] += colChck[15]; }
                    }
                }
            }
            else {
                pattern = patternPtr[0];
                if (pattern) {
                    if (pattern & 0x80) { collision |= colPtr[0];  colPtr[0] += colChck[0];  collision |= colPtr[1];  colPtr[1] += colChck[1]; }
                    if (pattern & 0x40) { collision |= colPtr[2];  colPtr[2] += colChck[2];  collision |= colPtr[3];  colPtr[3] += colChck[3]; }
                    if (pattern & 0x20) { collision |= colPtr[4];  colPtr[4] += colChck[4];  collision |= colPtr[5];  colPtr[5] += colChck[5]; }
                    if (pattern & 0x10) { collision |= colPtr[6];  colPtr[6] += colChck[6];  collision |= colPtr[7];  colPtr[7] += colChck[7]; }
                    if (pattern & 0x08) { collision |= colPtr[8];  colPtr[8] += colChck[8];  collision |= colPtr[9];  colPtr[9] += colChck[9]; }
                    if (pattern & 0x04) { collision |= colPtr[10]; colPtr[10] += colChck[10]; collision |= colPtr[11]; colPtr[11] += colChck[11]; }
                    if (pattern & 0x02) { collision |= colPtr[12]; colPtr[12] += colChck[12]; collision |= colPtr[13]; colPtr[13] += colChck[13]; }
                    if (pattern & 0x01) { collision |= colPtr[14]; colPtr[14] += colChck[14]; collision |= colPtr[15]; colPtr[15] += colChck[15]; }
                }
                if (vdpIsSprites16x16(vdp->vdpRegs)) {
                    pattern = patternPtr[16];

                    if (pattern) {
                        if (pattern & 0x80) { collision |= colPtr[16]; colPtr[16] += colChck[16]; collision |= colPtr[17]; colPtr[17] += colChck[17]; }
                        if (pattern & 0x40) { collision |= colPtr[18]; colPtr[18] += colChck[18]; collision |= colPtr[19]; colPtr[19] += colChck[19]; }
                        if (pattern & 0x20) { collision |= colPtr[20]; colPtr[20] += colChck[20]; collision |= colPtr[21]; colPtr[21] += colChck[21]; }
                        if (pattern & 0x10) { collision |= colPtr[22]; colPtr[22] += colChck[22]; collision |= colPtr[23]; colPtr[23] += colChck[23]; }
                        if (pattern & 0x08) { collision |= colPtr[24]; colPtr[24] += colChck[24]; collision |= colPtr[25]; colPtr[25] += colChck[25]; }
                        if (pattern & 0x04) { collision |= colPtr[26]; colPtr[26] += colChck[26]; collision |= colPtr[27]; colPtr[27] += colChck[27]; }
                        if (pattern & 0x02) { collision |= colPtr[28]; colPtr[28] += colChck[28]; collision |= colPtr[29]; colPtr[29] += colChck[29]; }
                        if (pattern & 0x01) { collision |= colPtr[30]; colPtr[30] += colChck[30]; collision |= colPtr[31]; colPtr[31] += colChck[31]; }
                    }
                }
            }
        }
        else {
            if (scale == 1) {
                pattern = patternPtr[0]; 
                if (pattern) {
                    if (pattern & 0x80) { linePtr[0] = color; collision |= colPtr[0]; colPtr[0] += colChck[0]; }
                    if (pattern & 0x40) { linePtr[1] = color; collision |= colPtr[1]; colPtr[1] += colChck[1]; }
                    if (pattern & 0x20) { linePtr[2] = color; collision |= colPtr[2]; colPtr[2] += colChck[2]; }
                    if (pattern & 0x10) { linePtr[3] = color; collision |= colPtr[3]; colPtr[3] += colChck[3]; }
                    if (pattern & 0x08) { linePtr[4] = color; collision |= colPtr[4]; colPtr[4] += colChck[4]; }
                    if (pattern & 0x04) { linePtr[5] = color; collision |= colPtr[5]; colPtr[5] += colChck[5]; }
                    if (pattern & 0x02) { linePtr[6] = color; collision |= colPtr[6]; colPtr[6] += colChck[6]; }
                    if (pattern & 0x01) { linePtr[7] = color; collision |= colPtr[7]; colPtr[7] += colChck[7]; }
                }

                if (vdpIsSprites16x16(vdp->vdpRegs)) {
                    pattern = patternPtr[16];

                    if (pattern) {
                        if (pattern & 0x80) { linePtr[8]  = color; collision |= colPtr[8];  colPtr[8] += colChck[8]; }
                        if (pattern & 0x40) { linePtr[9]  = color; collision |= colPtr[9];  colPtr[9] += colChck[9]; }
                        if (pattern & 0x20) { linePtr[10] = color; collision |= colPtr[10]; colPtr[10] += colChck[10]; }
                        if (pattern & 0x10) { linePtr[11] = color; collision |= colPtr[11]; colPtr[11] += colChck[11]; }
                        if (pattern & 0x08) { linePtr[12] = color; collision |= colPtr[12]; colPtr[12] += colChck[12]; }
                        if (pattern & 0x04) { linePtr[13] = color; collision |= colPtr[13]; colPtr[13] += colChck[13]; }
                        if (pattern & 0x02) { linePtr[14] = color; collision |= colPtr[14]; colPtr[14] += colChck[14]; }
                        if (pattern & 0x01) { linePtr[15] = color; collision |= colPtr[15]; colPtr[15] += colChck[15]; }
                    }
                }
            }
            else {
                pattern = patternPtr[0];
                if (pattern) {
                    if (pattern & 0x80) { linePtr[0]  = linePtr[1]  = color; collision |= colPtr[0];  colPtr[0] += colChck[0];  collision |= colPtr[1];  colPtr[1] += colChck[1]; }
                    if (pattern & 0x40) { linePtr[2]  = linePtr[3]  = color; collision |= colPtr[2];  colPtr[2] += colChck[2];  collision |= colPtr[3];  colPtr[3] += colChck[3]; }
                    if (pattern & 0x20) { linePtr[4]  = linePtr[5]  = color; collision |= colPtr[4];  colPtr[4] += colChck[4];  collision |= colPtr[5];  colPtr[5] += colChck[5]; }
                    if (pattern & 0x10) { linePtr[6]  = linePtr[7]  = color; collision |= colPtr[6];  colPtr[6] += colChck[6];  collision |= colPtr[7];  colPtr[7] += colChck[7]; }
                    if (pattern & 0x08) { linePtr[8]  = linePtr[9]  = color; collision |= colPtr[8];  colPtr[8] += colChck[8];  collision |= colPtr[9];  colPtr[9] += colChck[9]; }
                    if (pattern & 0x04) { linePtr[10] = linePtr[11] = color; collision |= colPtr[10]; colPtr[10] += colChck[10]; collision |= colPtr[11]; colPtr[11] += colChck[11]; }
                    if (pattern & 0x02) { linePtr[12] = linePtr[13] = color; collision |= colPtr[12]; colPtr[12] += colChck[12]; collision |= colPtr[13]; colPtr[13] += colChck[13]; }
                    if (pattern & 0x01) { linePtr[14] = linePtr[15] = color; collision |= colPtr[14]; colPtr[14] += colChck[14]; collision |= colPtr[15]; colPtr[15] += colChck[15]; }
                }
                if (vdpIsSprites16x16(vdp->vdpRegs)) {
                    pattern = patternPtr[16];

                    if (pattern) {
                        if (pattern & 0x80) { linePtr[16] = linePtr[17] = color; collision |= colPtr[16]; colPtr[16] += colChck[16]; collision |= colPtr[17]; colPtr[17] += colChck[17]; }
                        if (pattern & 0x40) { linePtr[18] = linePtr[19] = color; collision |= colPtr[18]; colPtr[18] += colChck[18]; collision |= colPtr[19]; colPtr[19] += colChck[19]; }
                        if (pattern & 0x20) { linePtr[20] = linePtr[21] = color; collision |= colPtr[20]; colPtr[20] += colChck[20]; collision |= colPtr[21]; colPtr[21] += colChck[21]; }
                        if (pattern & 0x10) { linePtr[22] = linePtr[23] = color; collision |= colPtr[22]; colPtr[22] += colChck[22]; collision |= colPtr[23]; colPtr[23] += colChck[23]; }
                        if (pattern & 0x08) { linePtr[24] = linePtr[25] = color; collision |= colPtr[24]; colPtr[24] += colChck[24]; collision |= colPtr[25]; colPtr[25] += colChck[25]; }
                        if (pattern & 0x04) { linePtr[26] = linePtr[27] = color; collision |= colPtr[26]; colPtr[26] += colChck[26]; collision |= colPtr[27]; colPtr[27] += colChck[27]; }
                        if (pattern & 0x02) { linePtr[28] = linePtr[29] = color; collision |= colPtr[28]; colPtr[28] += colChck[28]; collision |= colPtr[29]; colPtr[29] += colChck[29]; }
                        if (pattern & 0x01) { linePtr[30] = linePtr[31] = color; collision |= colPtr[30]; colPtr[30] += colChck[30]; collision |= colPtr[31]; colPtr[31] += colChck[31]; }
                    }
                }
            }
        }

    }

    if (collision && (vdp->vdpStatus[0] & 0x20) == 0) {
        int xCol;
        // Leftmost pixel covered by two collidable sprites on this line.
        for (xCol = 0; xCol < 288 && collisionBuf[xCol + 32] < 2; xCol++);
        vdp->vdpStatus[0] |= 0x20;
        vdp->vdpStatus[3] = (UInt8)(xCol + 12);
        vdp->vdpStatus[4] = (UInt8)((xCol + 12) >> 8);
        vdp->vdpStatus[5] = (UInt8)(dispLine + 8);
        vdp->vdpStatus[6] = (UInt8)((dispLine + 8) >> 8);
    }

    lineBufs[bufIndex] = lineBuf + 32;

    if (!spritesEnable) {
        return nullSpritesLine();
    }

    return lineBufs[bufIndex ^ 1];
}


void spriteLineInvalidate(VDP* vdp, int line) {
    nonVisibleLine = line - vdp->firstLine;
}

UInt8* colorSpritesLine(VDP* vdp, int line, int scr6) {
    int solidColor;
    int bufIndex;
    UInt8 collisionBuf[384];
    SpriteAttribute attribTable[32];
    UInt8 patternMask;
    int   attribBase;
    int   attribOffset;
    int   sprite;
    int   step;
    int   size;
    int   scale;
    int   visibleCnt;
    int   collision;
    int   idx;
    int   dispLine;
    int   first;

    idx = line;

    bufIndex = line & 1;

    line -= vdp->firstLine;

//    vdp->vdpStatus[0] &= 0x80;

    if (line == 0xffffffff) {
        nonVisibleLine = -1000;
    }

    if (idx == 0 || nonVisibleLine == line) {
        lineBufs[bufIndex] = nullSpritesLine();
        return nullSpritesLine();
    }

    if (!vdp->screenOn || (vdp->vdpStatus[2] & 0x40) || vdpIsSpritesOff(vdp->vdpRegs) || vdp->sprMode3) {
        lineBufs[bufIndex] = nullSpritesLine();
        return lineBufs[bufIndex ^ 1];
    }

    solidColor   = vdpIsColor0Solid(vdp->vdpRegs) ? 1 : 0;
    attribBase   = vdp->sprTabBase & 0x3fe00;
    size         = vdpIsSprites16x16(vdp->vdpRegs) ? 16 : 8;
    scale        = vdpIsSpritesBig(vdp->vdpRegs) ? 2 : 1;
	patternMask  = vdpIsSprites16x16(vdp->vdpRegs) ? 0xfc : 0xff;
    visibleCnt   = 0;
    collision    = 0;
    /* The line built here is the one shown next. */
    dispLine     = line + 1;
    line         = (line + vdpSpriteVScroll(vdp)) & 0xff;

    /* Find visible sprites on current line */
    for (step = 0; step < 32; step++) {
        int spriteLine;
        int offset;
        int color;

        sprite       = vdpSpritePlane(vdp, step, 31);
        attribOffset = attribBase + 4 * sprite;

        spriteLine = *MAP_VRAM(vdp, attribOffset);
        if (spriteLine == 216 && !vdpIsSpriteShuffle(vdp)) {
            break;
        }
       
        spriteLine = ((line - spriteLine) & 0xff) / scale;
		if (spriteLine >= size) {
            continue;
        }

        if (visibleCnt == vdpSpritesPerLine(vdp, 8)) {
//            printf("%d\t%d\t%d\t####\n", idx, line, boardSystemTime());
			if ((vdp->vdpStatus[0] & 0xc0) == 0) {
				vdp->vdpStatus[0] = (vdp->vdpStatus[0] & 0xe0) | 0x40 | sprite;
			}
			if (!noSpriteLimits)
				break;
        }

        offset = (vdp->sprGenBase & 0x3f800) + ((int)(*MAP_VRAM(vdp, attribOffset + 2) & patternMask) << 3) + spriteLine;
        color  = *MAP_VRAM(vdp, vdp->sprTabBase & ((-1 << 10) | (sprite * 16 + spriteLine)));

        attribTable[visibleCnt].color         = color;
        attribTable[visibleCnt].horizontalPos = (int)*MAP_VRAM(vdp, attribOffset + 1) + 24 - ((attribTable[visibleCnt].color >> 2) & 0x20);
        attribTable[visibleCnt].pattern       = *MAP_VRAM(vdp, offset);
        if (vdpIsSprites16x16(vdp->vdpRegs)) {
            attribTable[visibleCnt].pattern = (attribTable[visibleCnt].pattern << 8) | *MAP_VRAM(vdp, offset + 16);
            attribTable[visibleCnt].horizontalPos += 8;
        }
        visibleCnt++;
    }

    if (visibleCnt == 0) {
        lineBufs[bufIndex] = nullSpritesLine();
        return lineBufs[bufIndex ^ 1];
    }

	if ((vdp->vdpStatus[0] & 0xc0) == 0) {
		vdp->vdpStatus[0] = (vdp->vdpStatus[0] & 0xe0) | (sprite < 32 ? sprite : 31);
	}
    
    lineBuf = lineBuffer[bufIndex];
    memset(lineBuf, 0, 384);
    memset(collisionBuf, 0, 384);

    /* A CC sprite is shown only from the first CC=0 sprite of the line onwards. */
    for (first = 0; first < visibleCnt && (attribTable[first].color & 0x40); first++);

    /* Draw the visible sprites */
    for (idx = visibleCnt - 1; idx >= first; idx--) {
        SpriteAttribute* attrib = &attribTable[idx];
        UInt8* linePtr;
        UInt8* colPtr;
        UInt8* colChck;
        UInt16 pattern;
        UInt8 color;
        int offset;
        int idx2;

        if (scr6) {
            /* Tiled dot pair: bits 3-2 even, bits 1-0 odd, bits 3 and 0 mark both halves painted. */
            color = (attrib->color & 0x0f) || solidColor
                  ? ((attrib->color & 0x0c) << 2) | ((attrib->color & 0x03) << 1) | 0x09
                  : 0;
        }
        else {
            color = ((attrib->color & 0x0f) << 1) | solidColor;
        }
        if (color == 0) {
            continue;
        }

        colPtr  = collisionBuf + attrib->horizontalPos;
        colChck = colChckBuf   + attrib->horizontalPos;

        linePtr = lineBuf + attrib->horizontalPos;
        pattern = attrib->pattern;
        offset  = scale * (size - 1) + (16 - size);

        if (attrib->color & 0x60) {
            if (scale == 2) {
                while (pattern) {
                    if (pattern & 1) {
                        linePtr[offset] = color;
                        linePtr[offset + 1] = color;
                    }
                    offset -= 2;
                    pattern >>= 1;
                }
            }
            else {
                while (pattern) {
                    if (pattern & 1) {
                        linePtr[offset] = color;
                    }
                    offset--;
                    pattern >>= 1;
                }
            }
        }
        else {
            if (scale == 2) {
                while (pattern) {
                    if (pattern & 1) {
                        linePtr[offset] = color;
                        linePtr[offset + 1] = color;
                        collision |= colPtr[offset]; 
                        colPtr[offset] += colChck[offset];
                        collision |= colPtr[offset + 1]; 
                        colPtr[offset + 1] += colChck[offset + 1];
                    }
                    offset -= 2;
                    pattern >>= 1;
                }
            }
            else {
                while (pattern) {
                    if (pattern & 1) {
                        linePtr[offset] = color;
                        collision |= colPtr[offset]; 
                        colPtr[offset] += colChck[offset];
                    }
                    offset--;
                    pattern >>= 1;
                }
            }
        }

#if 0
        /* Skip CC sprites for now */
        if (attrib->color & 0x40) {
            continue;
        }
#endif
        /* Draw CC sprites */
        for (idx2 = idx + 1; idx2 < visibleCnt; idx2++) {
            SpriteAttribute* attrib = &attribTable[idx2];

            if (!(attrib->color & 0x40)) {
                break;
            }
                
            if (scr6) {
                color = (attrib->color & 0x0f) || solidColor
                      ? ((attrib->color & 0x0c) << 2) | ((attrib->color & 0x03) << 1) | 0x09
                      : 0;
            }
            else {
                color = ((attrib->color & 0x0f) << 1) | solidColor;
            }
            linePtr = lineBuf + attrib->horizontalPos;
            pattern = attrib->pattern;
            offset  = scale * (size - 1) + (16 - size);
            
            if (scale == 2) {
                while (pattern) {
                    if (pattern & 1) {
                        linePtr[offset] |= color;
                        linePtr[offset + 1] |= color;
                    }
                    offset -= 2;
                    pattern >>= 1;
                }
            }
            else {
                while (pattern) {
                    if (pattern & 1) {
                        linePtr[offset] |= color;
                    }
                    offset--;
                    pattern >>= 1;
                }
            }
        }
    }

    if (collision && (vdp->vdpStatus[0] & 0x20) == 0) {
        int xCol;
        // Leftmost pixel covered by two collidable sprites on this line.
        for (xCol = 0; xCol < 288 && collisionBuf[xCol + 32] < 2; xCol++);
        vdp->vdpStatus[0] |= 0x20;
        vdp->vdpStatus[3] = (UInt8)(xCol + 12);
        vdp->vdpStatus[4] = (UInt8)((xCol + 12) >> 8);
        vdp->vdpStatus[5] = (UInt8)(dispLine + 8);
        vdp->vdpStatus[6] = (UInt8)((dispLine + 8) >> 8);
    }

    lineBufs[bufIndex] = lineBuf + 32;

    if (!spritesEnable) {
        return nullSpritesLine();
    }

    return lineBufs[bufIndex ^ 1];
}


/* Sixty four planes of eight bytes, sixteen to a line. A plane can be
** translucent, so a dot cannot be a palette index: this leaves a colour and a
** weight for the caller to blend over the finished scanline. */
#define SPRITE_M3_WIDTH 256

static Pixel sprM3Pixel[SPRITE_M3_WIDTH];
static UInt8 sprM3Weight[SPRITE_M3_WIDTH];   /* 0 clear, else sprite share of four */

/* Resizing divides by a normalised reciprocal, not exactly: the magnification
** is shifted until its top bit is bit 7, multiplied by 65536 / (128 + index),
** then shifted back. The difference from exact division is part of the picture. */
static int spriteM3Sample(int offset, int magnify, int sizeShift)
{
    int limit = (16 << sizeShift) - 1;
    int exp = 0;
    int value;

    if (magnify == 0) {
        value = ((offset << 3) << sizeShift) >> 7;
    }
    else {
        while ((magnify >> (exp + 1)) != 0) {
            exp++;
        }
        value = 65536 / (128 + ((magnify << (7 - exp)) & 0x7f));
        value = (((value * offset) >> 5) << sizeShift) >> exp;
    }

    return value > limit ? limit : value;
}

/* Patterns are laid out as a SCREEN 5 bitmap, so a pattern is the 16 wide tile
** at ((n & 15) * 16, (n >> 4) * 16) of a 32kB page. */
static int spriteM3PatternByte(VDP* vdp, int page, int pattern, int row, int column)
{
    int addr = (vdp->sprGenBase & 0x3f800) + (page << 15) + ((pattern >> 4) << 11) + (row << 7) +
               ((pattern & 0x0f) << 3) + (column >> 1);

    return *MAP_VRAM(vdp, addr);
}

int spritesLineMode3(VDP* vdp, int Y)
{
    /* A sprite is scanned one line before the one it appears on, so an
    ** attribute Y reaches the screen a line below it. */
    int row  = Y - vdp->firstLine;
    int line = row - 1;
    int base = (((int)vdp->vdpRegs[11] & 0x07) << 15) | ((int)vdp->vdpRegs[5] << 7);
    int planeMaskHigh = (base >> 7) & 0x03;
    int visible = 0;
    int step;
    int i;

    for (i = 0; i < SPRITE_M3_WIDTH; i++) {
        sprM3Weight[i] = 0;
    }

    if (!vdp->screenOn || vdpIsSpritesOff(vdp->vdpRegs) || row < 0) {
        return 0;
    }

    /* The R#23 offset is added in eight bits, so the scan position wraps. */
    line = vdpIsSpriteVScrollOff(vdp) ? (line & 0x3ff)
                                      : ((line + vdpSpriteVScroll(vdp)) & 0xff);

    for (step = 0; step < 64; step++) {
        int plane  = vdpSpritePlane(vdp, step, 63);
        int attrib = (base & ~0x1ff) | ((planeMaskHigh & (plane >> 4)) << 7) | ((plane & 0x0f) << 3);
        int y    = *MAP_VRAM(vdp, attrib);
        int b1   = *MAP_VRAM(vdp, attrib + 1);
        int mgy  = *MAP_VRAM(vdp, attrib + 2);
        int mode = *MAP_VRAM(vdp, attrib + 3);
        int x    = *MAP_VRAM(vdp, attrib + 4);
        int b5   = *MAP_VRAM(vdp, attrib + 5);
        int mgx  = *MAP_VRAM(vdp, attrib + 6);
        int pattern = *MAP_VRAM(vdp, attrib + 7);
        int rows, dy, srcY, weight, set, page, dx;

        /* The whole ten bit Y ends the table, so a sprite parked below the
        ** screen is not mistaken for the marker by its low byte alone. */
        y |= (b1 & 0x03) << 8;
        if (y == 216 && !vdpIsSpriteShuffle(vdp)) {
            break;
        }

        x |= (b5 & 0x03) << 8;
        rows = 16 << (b1 >> 6);

        dy = (line - y) & 0x3ff;
        if (dy >= (mgy ? mgy : 256)) {
            continue;
        }

        if (visible == 16 && !noSpriteLimits) {
            break;
        }
        visible++;

        srcY = spriteM3Sample(dy, mgy, b1 >> 6);
        if (mode & 0x20) {
            srcY = rows - 1 - srcY;
        }

        weight = 4 - (mode >> 6);
        set    = mode & 0x0f;
        page   = (b5 >> 4) & 0x07;

        /* An MGX of zero is no dots at all, not 256 the way an MGY of zero is. */
        for (dx = 0; dx < mgx; dx++) {
            int screenX = (x + dx) & 0x3ff;
            int srcX;
            int colour;

            if (screenX >= SPRITE_M3_WIDTH || sprM3Weight[screenX]) {
                continue;
            }

            srcX = spriteM3Sample(dx, mgx, 0);
            if (mode & 0x10) {
                srcX = 15 - srcX;
            }

            colour = spriteM3PatternByte(vdp, page, pattern, srcY, srcX);
            colour = (srcX & 1) ? (colour & 0x0f) : (colour >> 4);
            if (colour == 0) {
                continue;
            }

            sprM3Pixel[screenX]  = vdp->paletteExt[(set << 4) | colour];
            sprM3Weight[screenX] = (UInt8)weight;
        }
    }

    return 1;
}

#if defined(WII)
#define SPR_M3_R(p) (((p) >> 11) & 0x1f)
#define SPR_M3_G(p) (((p) >>  6) & 0x1f)
#define SPR_M3_B(p) ( (p)        & 0x1f)
#define SPR_M3_MAKE(r, g, b) (Pixel)(((r) << 11) | ((g) << 6) | (b))
#elif defined(VIDEO_COLOR_TYPE_RGB565)
#define SPR_M3_R(p) (((p) >> 11) & 0x1f)
#define SPR_M3_G(p) (((p) >>  5) & 0x3f)
#define SPR_M3_B(p) ( (p)        & 0x1f)
#define SPR_M3_MAKE(r, g, b) (Pixel)(((r) << 11) | ((g) << 5) | (b))
#elif defined(VIDEO_COLOR_TYPE_RGBA5551)
#define SPR_M3_R(p) (((p) >> 11) & 0x1f)
#define SPR_M3_G(p) (((p) >>  6) & 0x1f)
#define SPR_M3_B(p) (((p) >>  1) & 0x1f)
#define SPR_M3_MAKE(r, g, b) (Pixel)(((r) << 11) | ((g) << 6) | ((b) << 1))
#else
#define SPR_M3_R(p) (((p) >> 10) & 0x1f)
#define SPR_M3_G(p) (((p) >>  5) & 0x1f)
#define SPR_M3_B(p) ( (p)        & 0x1f)
#define SPR_M3_MAKE(r, g, b) (Pixel)(((r) << 10) | ((g) << 5) | (b))
#endif

#define SPR_M3_MIX(s, d, w) (((s) * (w) + (d) * (4 - (w))) >> 2)

/* A sprite dot always spans one MSX dot, which is one or two Pixels of the
** line depending on what the renderer laid down, and startDot is where the
** left edge mask the renderer already applied ends. */
void spritesOverlayMode3(VDP* vdp, int Y, Pixel* origin, int dotStep, int startDot)
{
    int x;

    for (x = startDot; x < SPRITE_M3_WIDTH; x++) {
        Pixel* dst = origin + x * dotStep;
        int w = sprM3Weight[x];
        Pixel s, d;

        if (w == 0) {
            continue;
        }

        s = sprM3Pixel[x];
        /* A superimpose key holds no colour to blend against, so the dot takes
        ** the sprite whole rather than a share of black. */
        if (w != 4 && !(dst[0] & BKMODE_TRANSPARENT)) {
            d = dst[0];
            s = SPR_M3_MAKE(SPR_M3_MIX(SPR_M3_R(s), SPR_M3_R(d), w),
                            SPR_M3_MIX(SPR_M3_G(s), SPR_M3_G(d), w),
                            SPR_M3_MIX(SPR_M3_B(s), SPR_M3_B(d), w));
        }

        dst[0] = s;
        if (dotStep == 2) {
            dst[1] = s;
        }
    }
}

UInt8* getSpritesLine(VDP* vdp, int line) {
    if (!spritesEnable) {
        return nullSpritesLine();
    }

    return lineBufs[(line & 1) ^ 1];
}

#endif
