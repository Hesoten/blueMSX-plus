/*****************************************************************************
**
** SD / MMC card emulator in SPI mode.
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
#include "SdCard.h"
#include "Disk.h"
#include "../Board/Board.h"
#include "SaveState.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SD SPI mode response widths (excludes the leading 0xFF idle filler). */
#define R1_LEN  1
#define R3_LEN  5       /* R1 + 4 byte OCR */
#define R7_LEN  5       /* R1 + 4 byte cmd echo (CMD8) */

/* R1 status bit flags (bit 7 is always 0 in valid R1). */
#define R1_IDLE         0x01
#define R1_ERASE_RESET  0x02
#define R1_ILLEGAL_CMD  0x04
#define R1_CRC_ERROR    0x08
#define R1_ERASE_SEQ    0x10
#define R1_ADDR_ERROR   0x20
#define R1_PARAM_ERROR  0x40

/* Data transfer tokens. */
#define TOK_START_BLOCK     0xFE    /* CMD17 / CMD18 / CMD24 block start */
#define TOK_START_MULTI     0xFC    /* CMD25 block start (continuation) */
#define TOK_STOP_TRAN       0xFD    /* CMD25 stop transmission */
#define TOK_DATA_ACCEPTED   0x05    /* low 5 bits of write data response */
#define DATA_RESP_MASK      0x1F

/* Block size.  SDHC always reports 512; CMD16 is silently honoured but
** ignored (we lock to 512 like real SDHC cards do). */
#define BLOCK_LEN 512

/* CMD0  GO_IDLE_STATE       -> R1
** CMD8  SEND_IF_COND        -> R7 (v2 cards only)
** CMD9  SEND_CSD            -> R1 + 16-byte CSD block
** CMD10 SEND_CID            -> R1 + 16-byte CID block
** CMD12 STOP_TRANSMISSION   -> R1b (we just ack)
** CMD13 SEND_STATUS         -> R2 (2 bytes)
** CMD16 SET_BLOCKLEN        -> R1
** CMD17 READ_SINGLE_BLOCK   -> R1 + data block
** CMD18 READ_MULTI_BLOCK    -> R1 + data blocks until CMD12
** CMD24 WRITE_BLOCK         -> R1, then accept data block, R1, busy
** CMD25 WRITE_MULTI_BLOCK   -> R1, then blocks separated by 0xFC tokens, terminated by 0xFD
** CMD55 APP_CMD             -> R1 (next cmd is ACMD)
** CMD58 READ_OCR            -> R3
** ACMD41 SD_SEND_OP_COND    -> R1
** ACMD51 SEND_SCR           -> R1 + 8-byte SCR block
*/

typedef enum {
    PS_IDLE = 0,        /* awaiting next command byte */
    PS_RECV_CMD,        /* collecting bytes 1..5 of a command */
    PS_RESPOND,         /* clocking response bytes out */
    PS_READ_TOKEN,      /* about to send data start token */
    PS_READ_DATA,       /* clocking 512 data bytes out */
    PS_READ_CRC,        /* clocking 2 dummy CRC bytes out */
    PS_MULTI_GAP,       /* CMD18 inter-block: next clock either starts new block (mosi=0xFF) or new cmd */
    PS_WRITE_WAIT,      /* awaiting start-of-write token */
    PS_WRITE_DATA,      /* receiving 512 data bytes */
    PS_WRITE_CRC,       /* receiving 2 CRC bytes */
    PS_WRITE_DELAY,     /* 1-byte gap after CRC, before response */
    PS_WRITE_RESP,      /* sending data response byte */
    PS_WRITE_BUSY       /* asserting busy (return 0x00) until written */
} ProtoState;

struct SdCard {
    int diskId;
    int csActive;       /* 1 = chip selected (CS low) */

    ProtoState state;

    UInt8 cmdBuf[6];
    int   cmdIdx;

    UInt8 respBuf[20];
    int   respLen;
    int   respPos;

    UInt8 dataBuf[BLOCK_LEN];
    int   dataPos;
    int   dataLen;                  /* 16 for CSD/CID, 8 for SCR, 512 for sector reads */
    UInt32 dataSector;

    /* Card init progress.  CMD0 puts the card into IDLE; ACMD41 with
    ** HCS=1 sets initialized=1.  Many host drivers retry ACMD41 in a
    ** loop while R1.idle is asserted; we report ready on the first
    ** call (no need to fake a delay). */
    int initialized;
    int expectAcmd;     /* CMD55 just received -- next cmd is an A-form */

    /* Last-known card status, bit-encoded as R2 second byte. */
    UInt8 status;

    /* Cached register blocks built once at create-time. */
    UInt8 cid[16];
    UInt8 csd[16];
    UInt8 ocr[4];
    UInt8 scr[8];

    UInt32 totalSectors;

    /* 2-byte busy delay between the last command byte and
    ** the first R1 byte.  The MFR SCC+ SD driver firmware reads
    ** the response slot at a fixed offset past the command write and
    ** treats the first non-0xFF byte as R1; an immediate 0x01 in that
    ** slot is interpreted as "card busy / hardware glitch" and the
    ** card is skipped. */
    int responseDelay;
};


/* ----------------------------- Helpers ---------------------------- */

static UInt32 readSectorCount(int diskId)
{
    /* No public helper exposes the image size, so probe via binary
    ** search of diskReadSector return codes.  blueMSX's Disk layer
    ** uses 1-based sector numbers internally; we keep the rest of
    ** this file in SDHC's 0-based convention and translate only here
    ** and in CMD17/24.
    **
    ** The number is advisory -- only populates CSD.C_SIZE.  Nextor
    ** reads the partition table for the real volume extent, so an
    ** approximate value is sufficient. */
    UInt8 buf[BLOCK_LEN];
    UInt32 lo = 0;        /* highest *1-based* sector confirmed readable */
    UInt32 hi = 1;        /* first 1-based sector confirmed unreadable */

    /* Exponential hunt for an unreadable bound. */
    while (hi <= 0x10000000u &&
           diskReadSector(diskId, buf, (int)hi, 0, 0, 0, NULL) == DSKE_OK) {
        lo = hi;
        hi *= 2;
    }
    /* Binary search between lo (good) and hi (bad). */
    while (lo + 1 < hi) {
        UInt32 mid = lo + (hi - lo) / 2;
        if (diskReadSector(diskId, buf, (int)mid, 0, 0, 0, NULL) == DSKE_OK) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;            /* lo is now the 1-based sector count */
}

static void buildCid(SdCard* sd)
{
    /* CID identification values chosen so the MFR SCC+ SD firmware
    ** accepts the card. */
    memset(sd->cid, 0, sizeof(sd->cid));
    sd->cid[0]  = 0xAA;   /* MID */
    sd->cid[1]  = 'o';
    sd->cid[2]  = 'p';
    sd->cid[3]  = 'e';
    sd->cid[4]  = 'n';
    sd->cid[5]  = 'M';
    sd->cid[6]  = 'S';
    sd->cid[7]  = 'X';
    sd->cid[8]  = 0x01;   /* product revision */
    sd->cid[9]  = 0x12;
    sd->cid[10] = 0x34;
    sd->cid[11] = 0x56;
    sd->cid[12] = 0x78;
    sd->cid[13] = 0x00;
    sd->cid[14] = 0xE6;
    sd->cid[15] = 0x01;
}

static void buildCsd(SdCard* sd)
{
    /* CSD v2.0 (SDHC).  C_SIZE encodes (capacity / 512KB) - 1.
    ** Layout fixed per SD spec except for C_SIZE which we
    ** derive from the attached image. */
    UInt32 cSize = sd->totalSectors / 1024;
    if (cSize > 0) cSize -= 1;

    memset(sd->csd, 0, sizeof(sd->csd));
    sd->csd[0]  = 0x40;       /* CSD_STRUCTURE = 1 (v2.0) */
    sd->csd[1]  = 0x0E;       /* TAAC */
    sd->csd[2]  = 0x00;       /* NSAC */
    sd->csd[3]  = 0x32;       /* TRAN_SPEED */
    sd->csd[4]  = 0x5B;       /* CCC high 8 bits  -- classes 0,1,2,3,4,6,7 advertised */
    sd->csd[5]  = 0x59;       /* CCC low 4 bits | READ_BL_LEN = 9 (512B) */
    sd->csd[6]  = 0x00;       /* DSR_IMP / READ_BLK_PARTIAL / WRITE_BLK_MISALIGN flags */
    sd->csd[7]  = (UInt8)((cSize >> 16) & 0x3F);
    sd->csd[8]  = (UInt8)((cSize >> 8) & 0xFF);
    sd->csd[9]  = (UInt8)(cSize & 0xFF);
    sd->csd[10] = 0x7F;       /* ERASE_BLK_EN=1, SECTOR_SIZE upper bits */
    sd->csd[11] = 0x80;       /* SECTOR_SIZE low bit, WP_GRP_SIZE */
    sd->csd[12] = 0x0A;       /* WP_GRP_ENABLE=0, R2W_FACTOR=2 (4x), WRITE_BL_LEN=9 */
    sd->csd[13] = 0x40;       /* WRITE_BL_LEN low bit, WRITE_BL_PARTIAL=0, FILE_FORMAT_GRP=1 */
    sd->csd[14] = 0x00;       /* COPY=0, PERM_WRITE_PROTECT=0, TMP_WRITE_PROTECT=0, FILE_FORMAT=0 */
    sd->csd[15] = 0x01;       /* CRC + stop bit */
}

static void buildOcr(SdCard* sd)
{
    /* Bit 31 = card power up status (ready when 1).
    ** Bit 30 = CCS (1 = SDHC/SDXC, block addressing).
    ** Bits 23..15 = supported voltage windows.  We claim 2.7-3.6 V. */
    sd->ocr[0] = 0xC0;      /* ready + CCS=1 */
    sd->ocr[1] = 0xFF;
    sd->ocr[2] = 0x80;
    sd->ocr[3] = 0x00;
}

static void buildScr(SdCard* sd)
{
    /* SD Configuration Register: report Physical Spec v2.0 + 1-bit bus
    ** width supported (SPI mode never uses the 4-bit bus). */
    memset(sd->scr, 0, sizeof(sd->scr));
    sd->scr[0] = 0x02;      /* SCR_STRUCTURE = 0, SD_SPEC = 2 */
    sd->scr[1] = 0x35;      /* SD_SECURITY = 3, SD_BUS_WIDTHS = 5 */
}

static void enqueueResp(SdCard* sd, const UInt8* bytes, int len)
{
    if (len > (int)sizeof(sd->respBuf)) len = (int)sizeof(sd->respBuf);
    memcpy(sd->respBuf, bytes, len);
    sd->respLen = len;
    sd->respPos = 0;
    sd->responseDelay = 2;        /* 2 idle bytes before R1 */
    sd->state   = PS_RESPOND;
}

static void enqueueR1(SdCard* sd, UInt8 r1)
{
    enqueueResp(sd, &r1, 1);
}

/* Queue the standard R1 + then prime a data block to send out (after
** an 0xFE start-block token).  Used by CMD9 / CMD10 / CMD17 / ACMD51.
** dataLen tracks the actual register block size so CSD/CID transfers
** stop after 16 bytes + 2 CRC instead of stretching to 512 -- some
** simple drivers (notably MFR SCC+ SD) issue the next CMD
** immediately after the short block and assume the SPI state machine
** is back to idle. */
static void enqueueReadBlock(SdCard* sd, UInt8 r1, const UInt8* block, int blockLen)
{
    if (blockLen > (int)sizeof(sd->dataBuf)) blockLen = (int)sizeof(sd->dataBuf);
    enqueueR1(sd, r1);
    memcpy(sd->dataBuf, block, blockLen);
    sd->dataPos = 0;
    sd->dataLen = blockLen;
    /* After R1 byte is consumed by RESPOND state, we'll transition to
    ** READ_TOKEN below in stepRespondEnd(). */
}


/* --------------------------- Command parser ----------------------- */

static void execCommand(SdCard* sd)
{
    UInt8 cmd  = sd->cmdBuf[0] & 0x3F;
    UInt32 arg = ((UInt32)sd->cmdBuf[1] << 24) |
                 ((UInt32)sd->cmdBuf[2] << 16) |
                 ((UInt32)sd->cmdBuf[3] << 8)  |
                  (UInt32)sd->cmdBuf[4];
    /* R1 selection: each command returns a fixed R1
    ** byte regardless of card state.  Commands that only acknowledge
    ** (CMD0, 12, 16, 55) return R1_IDLE (bit 0 set).  Commands that
    ** carry a result (CSD/CID block, OCR, sector data, ACMD41 done)
    ** return 0x00 -- the convention the MFR SCC+ SD driver
    ** firmware relies on. */
    int wasAcmd = sd->expectAcmd;
    sd->expectAcmd = 0;

    if (wasAcmd) {
        switch (cmd) {
        case 41: /* ACMD41 -- init complete. */
            sd->initialized = 1;
            enqueueR1(sd, 0x00);
            return;
        case 51: /* ACMD51 -- SCR register, 8 bytes. */
            enqueueReadBlock(sd, 0x00, sd->scr, 8);
            return;
        default:
            enqueueR1(sd, R1_IDLE | R1_ILLEGAL_CMD);
            return;
        }
    }

    switch (cmd) {
    case 0:  /* CMD0 GO_IDLE_STATE */
        enqueueR1(sd, R1_IDLE);
        return;
    case 8:  /* CMD8 SEND_IF_COND -- v2 cards.  Echo the check pattern. */
        {
            UInt8 r7[5];
            r7[0] = R1_IDLE;
            r7[1] = 0;
            r7[2] = 0;
            r7[3] = (UInt8)((arg >> 8) & 0x0F);
            r7[4] = (UInt8)(arg & 0xFF);
            enqueueResp(sd, r7, 5);
        }
        return;
    case 9:  /* CMD9 SEND_CSD
              *
              * Re-probe the image size on every CSD read.  The cart
              * mapper creates SdCard instances before any image is
              * attached, so totalSectors at create-time is 0; the
              * driver firmware tends to cache the very first CSD it
              * reads, so we must reflect the up-to-date capacity here.
              */
        sd->totalSectors = diskPresent(sd->diskId) ? readSectorCount(sd->diskId) : 0;
        buildCsd(sd);
        enqueueReadBlock(sd, 0x00, sd->csd, 16);
        return;
    case 10: /* CMD10 SEND_CID */
        enqueueReadBlock(sd, 0x00, sd->cid, 16);
        return;
    case 12: /* CMD12 STOP_TRANSMISSION */
        enqueueR1(sd, R1_IDLE);
        return;
    case 13: /* CMD13 SEND_STATUS -- 2-byte R2. */
        {
            UInt8 r2[2];
            r2[0] = 0x00;
            r2[1] = sd->status;
            enqueueResp(sd, r2, 2);
        }
        return;
    case 16: /* CMD16 SET_BLOCKLEN -- accepted silently, card stays in idle. */
        enqueueR1(sd, R1_IDLE);
        return;
    case 17: /* CMD17 READ_SINGLE_BLOCK */
        {
            UInt8 sect[BLOCK_LEN];
            /* SDHC = block-addressed: arg IS the 0-based sector number.
            ** Disk layer is 1-based, so add 1 on the way in. */
            boardSetHddSdActive();
            if (diskReadSector(sd->diskId, sect, (int)(arg + 1), 0, 0, 0, NULL) != DSKE_OK) {
                enqueueR1(sd, R1_PARAM_ERROR);
                return;
            }
            enqueueReadBlock(sd, 0x00, sect, BLOCK_LEN);
        }
        return;
    case 18: /* CMD18 READ_MULTIPLE_BLOCK
              *
              * Same as CMD17 for the first block, but after each block's
              * CRC bytes are clocked out we transparently load the next
              * sector and emit another start token, continuing until the
              * host issues CMD12 STOP_TRANSMISSION (or deasserts CS).
              * The MFR SCC+ SD driver uses CMD18 for normal reads
              * including post-write verify, so this path is hot. */
        {
            UInt8 sect[BLOCK_LEN];
            boardSetHddSdActive();
            if (diskReadSector(sd->diskId, sect, (int)(arg + 1), 0, 0, 0, NULL) != DSKE_OK) {
                enqueueR1(sd, R1_PARAM_ERROR);
                return;
            }
            sd->dataSector = arg + 1;     /* next sector to fetch after this one */
            enqueueReadBlock(sd, 0x00, sect, BLOCK_LEN);
        }
        return;
    case 24: /* CMD24 WRITE_BLOCK */
        sd->dataSector = arg;
        sd->dataPos = 0;
        enqueueR1(sd, 0x00);
        return;
    case 25: /* CMD25 WRITE_MULTIPLE_BLOCK
              *
              * Like CMD24 but the host streams several blocks back-to-
              * back, each preceded by a 0xFC token; the host terminates
              * the burst with a 0xFD token instead of a fresh 0xFC.
              * The first sector to write is in arg. */
        sd->dataSector = arg;
        sd->dataPos = 0;
        enqueueR1(sd, 0x00);
        return;
    case 55: /* CMD55 APP_CMD */
        sd->expectAcmd = 1;
        enqueueR1(sd, R1_IDLE);
        return;
    case 58: /* CMD58 READ_OCR */
        {
            UInt8 r3[5];
            r3[0] = 0x00;
            memcpy(r3 + 1, sd->ocr, 4);
            enqueueResp(sd, r3, 5);
        }
        return;
    default:
        enqueueR1(sd, R1_IDLE | R1_ILLEGAL_CMD);
        return;
    }
}

/* When the response queue drains, set up whatever data phase follows. */
static void stepRespondEnd(SdCard* sd)
{
    UInt8 cmd = sd->cmdBuf[0] & 0x3F;
    int wasAcmd = (sd->cmdBuf[0] & 0x80) != 0;  /* hijacked bit marker */

    /* Read-block-producing commands: continue with token + data + CRC. */
    if (!wasAcmd) {
        switch (cmd) {
        case 9: case 10: case 17: case 18:
            sd->state = PS_READ_TOKEN;
            return;
        case 24: case 25:
            sd->state = PS_WRITE_WAIT;
            return;
        default:
            break;
        }
    }
    if (wasAcmd && cmd == 51) {
        sd->state = PS_READ_TOKEN;
        return;
    }

    sd->state = PS_IDLE;
}


/* ------------------------- SPI byte handler ----------------------- */

UInt8 sdCardTransfer(SdCard* sd, UInt8 mosi)
{
    /* CS deselected: tristate, host should see 0xFF. */
    if (!sd->csActive) {
        return 0xFF;
    }
    /* No card in slot. */
    if (!diskPresent(sd->diskId)) {
        return 0xFF;
    }

    /* CMD18 STOP detection.  Real cards detect a new command even
    ** while emitting block data so the host can abort a CMD18 stream
    ** with CMD12 at any moment.  In our cart wiring MOSI is the value
    ** the host wrote -- a host READ always clocks with MOSI=0xFF, so a
    ** non-0xFF MOSI with the command bit pattern (top 2 bits = 01) is
    ** an unambiguous CMD start.  This guard only triggers in read-side
    ** states; write-side states process MOSI bytes as data normally. */
    if (mosi != 0xFF && (mosi & 0xC0) == 0x40 &&
        (sd->state == PS_READ_TOKEN || sd->state == PS_READ_DATA ||
         sd->state == PS_READ_CRC   || sd->state == PS_MULTI_GAP)) {
        sd->cmdBuf[0] = mosi;
        sd->cmdIdx = 1;
        sd->state = PS_RECV_CMD;
        return 0xFF;
    }

    switch (sd->state) {
    case PS_IDLE:
        /* Start of a new command: byte must have bit pattern 01xxxxxx. */
        if ((mosi & 0xC0) == 0x40) {
            /* Stash the original command byte (preserve ACMD marker via
            ** bit 7 since real CMD index only uses low 6 bits). */
            sd->cmdBuf[0] = mosi;
            if (sd->expectAcmd) {
                sd->cmdBuf[0] |= 0x80;
            }
            sd->cmdIdx = 1;
            sd->state  = PS_RECV_CMD;
        }
        return 0xFF;

    case PS_RECV_CMD:
        sd->cmdBuf[sd->cmdIdx++] = mosi;
        if (sd->cmdIdx >= 6) {
            execCommand(sd);
        }
        return 0xFF;

    case PS_RESPOND:
        if (sd->responseDelay > 0) {
            sd->responseDelay--;
            return 0xFF;
        }
        {
            UInt8 b = sd->respBuf[sd->respPos++];
            if (sd->respPos >= sd->respLen) {
                stepRespondEnd(sd);
            }
            return b;
        }

    case PS_READ_TOKEN:
        sd->state = PS_READ_DATA;
        sd->dataPos = 0;
        return TOK_START_BLOCK;

    case PS_READ_DATA:
        {
            UInt8 b = sd->dataBuf[sd->dataPos++];
            if (sd->dataPos >= sd->dataLen) {
                sd->state = PS_READ_CRC;
                sd->dataPos = 0;
            }
            return b;
        }

    case PS_READ_CRC:
        sd->dataPos++;
        if (sd->dataPos >= 2) {
            /* If we are mid CMD18 (multi-block read), defer the next-block
            ** decision by one clock so we can spot a CMD12 STOP from the
            ** host.  Otherwise drop back to idle. */
            UInt8 origCmd = sd->cmdBuf[0] & 0x3F;
            sd->state = (origCmd == 18) ? PS_MULTI_GAP : PS_IDLE;
        }
        return 0xFF;

    case PS_MULTI_GAP:
        /* One gap byte between CMD18 blocks.  CMD12 detection is
        ** handled by the top-of-function guard, so by the time we get
        ** here the host has clocked an idle 0xFF and wants more data:
        ** preload the next sector and transition to the token phase. */
        {
            UInt8 sect[BLOCK_LEN];
            boardSetHddSdActive();
            if (diskReadSector(sd->diskId, sect, (int)(sd->dataSector + 1), 0, 0, 0, NULL) == DSKE_OK) {
                memcpy(sd->dataBuf, sect, BLOCK_LEN);
                sd->dataPos = 0;
                sd->dataLen = BLOCK_LEN;
                sd->dataSector++;
                sd->state = PS_READ_TOKEN;
            } else {
                sd->state = PS_IDLE;
            }
        }
        return 0xFF;

    case PS_WRITE_WAIT:
        /* Real cards accept 0xFE (CMD24 / first CMD25 block / continuation),
        ** 0xFC (subsequent CMD25 blocks; same encoding as 0xFE here), and
        ** 0xFD (CMD25 terminator).  Treat 0xFE and 0xFC identically; 0xFD
        ** ends the multi-write stream. */
        if (mosi == TOK_START_BLOCK || mosi == TOK_START_MULTI) {
            sd->state = PS_WRITE_DATA;
            sd->dataPos = 0;
        } else if (mosi == TOK_STOP_TRAN) {
            sd->state = PS_IDLE;
        }
        return 0xFF;

    case PS_WRITE_DATA:
        sd->dataBuf[sd->dataPos++] = mosi;
        if (sd->dataPos >= BLOCK_LEN) {
            sd->state = PS_WRITE_CRC;
            sd->dataPos = 0;
        }
        return 0xFF;

    case PS_WRITE_CRC:
        sd->dataPos++;
        if (sd->dataPos >= 2) {
            sd->state = PS_WRITE_DELAY;
        }
        return 0xFF;

    case PS_WRITE_DELAY:
        /* Emit one 0xFF byte between the last CRC byte and the
        ** data response token (matches typical SD card timing after a write
        ** commit).  Without this gap the Nextor MFR SCC+ SD driver
        ** reads the response token in the slot where it expects the
        ** delay, then sees 0xFF in the response slot and reports the
        ** write as failed. */
        sd->state = PS_WRITE_RESP;
        return 0xFF;

    case PS_WRITE_RESP:
        /* Disk layer note: diskReadSector is 1-based (CMD17 / CMD18
        ** pass arg+1), diskWrite is 0-based so we pass dataSector raw.
        ** Response token uses the plain 0x05 / 0x0D form (high bits 0)
        ** as observed on real SD cards. */
        {
            UInt8 origCmd = sd->cmdBuf[0] & 0x3F;
            UInt8 rv;
            boardSetHddSdActive();
            rv = diskWrite(sd->diskId, sd->dataBuf, (int)sd->dataSector);
            if (origCmd == 25) {
                /* Streaming write: advance sector and wait for next 0xFC / 0xFD. */
                sd->dataSector++;
                sd->state = PS_WRITE_WAIT;
            } else {
                sd->state = PS_IDLE;
            }
            return rv ? (UInt8)TOK_DATA_ACCEPTED : (UInt8)0x0D;
        }

    case PS_WRITE_BUSY:
        /* Unused -- kept for save-state compatibility. */
        sd->state = PS_IDLE;
        return 0xFF;
    }

    return 0xFF;
}


/* ----------------------------- Lifecycle -------------------------- */

SdCard* sdCardCreate(int diskId)
{
    SdCard* sd = calloc(1, sizeof(SdCard));
    sd->diskId = diskId;
    sd->csActive = 0;
    sd->state = PS_IDLE;

    sd->totalSectors = diskPresent(diskId) ? readSectorCount(diskId) : 0;

    buildCid(sd);
    buildCsd(sd);
    buildOcr(sd);
    buildScr(sd);

    return sd;
}

void sdCardDestroy(SdCard* sd)
{
    if (!sd) return;
    free(sd);
}

void sdCardReset(SdCard* sd)
{
    sd->state = PS_IDLE;
    sd->cmdIdx = 0;
    sd->respLen = 0;
    sd->respPos = 0;
    sd->dataPos = 0;
    sd->initialized = 0;
    sd->expectAcmd = 0;
    sd->status = 0;

    /* If the image was swapped while the card was inserted, refresh the
    ** capacity-derived register. */
    sd->totalSectors = diskPresent(sd->diskId) ? readSectorCount(sd->diskId) : 0;
    buildCsd(sd);
}

UInt8 sdCardTransferCs(SdCard* sd, UInt8 mosi, int csHigh)
{
    sdCardSetCs(sd, csHigh ? 1 : 0);
    return sdCardTransfer(sd, mosi);
}

void sdCardSetCs(SdCard* sd, int csActiveLow)
{
    int newCs = csActiveLow ? 0 : 1;
    if (newCs != sd->csActive) {
        /* Rising edge -- finish whatever was in flight.  Falling edge
        ** also resets to IDLE so a fresh command can start without
        ** stale state from a previous, possibly-aborted, transfer. */
        sd->state = PS_IDLE;
        sd->cmdIdx = 0;
        sd->respLen = 0;
        sd->respPos = 0;
        sd->dataPos = 0;
        sd->csActive = newCs;
    }
}


/* ----------------------------- SaveState -------------------------- */

void sdCardSaveState(SdCard* sd, const char* tagPrefix)
{
    char tag[64];
    SaveState* state;

    snprintf(tag, sizeof(tag), "%s_sdcard", tagPrefix);
    state = saveStateOpenForWrite(tag);

    saveStateSet(state, "csActive",    sd->csActive);
    saveStateSet(state, "state",       sd->state);
    saveStateSet(state, "cmdIdx",      sd->cmdIdx);
    saveStateSet(state, "respLen",     sd->respLen);
    saveStateSet(state, "respPos",     sd->respPos);
    saveStateSet(state, "dataPos",     sd->dataPos);
    saveStateSet(state, "dataLen",     sd->dataLen);
    saveStateSet(state, "dataSector",  sd->dataSector);
    saveStateSet(state, "initialized", sd->initialized);
    saveStateSet(state, "expectAcmd",  sd->expectAcmd);
    saveStateSet(state, "status",      sd->status);
    saveStateSet(state, "respDelay",   sd->responseDelay);
    saveStateSetBuffer(state, "cmdBuf",  sd->cmdBuf,  sizeof(sd->cmdBuf));
    saveStateSetBuffer(state, "respBuf", sd->respBuf, sizeof(sd->respBuf));
    saveStateSetBuffer(state, "dataBuf", sd->dataBuf, sizeof(sd->dataBuf));

    saveStateClose(state);
}

void sdCardLoadState(SdCard* sd, const char* tagPrefix)
{
    char tag[64];
    SaveState* state;

    snprintf(tag, sizeof(tag), "%s_sdcard", tagPrefix);
    state = saveStateOpenForRead(tag);

    sd->csActive    = saveStateGet(state, "csActive",    0);
    sd->state       = (ProtoState)saveStateGet(state, "state", PS_IDLE);
    sd->cmdIdx      = saveStateGet(state, "cmdIdx",      0);
    sd->respLen     = saveStateGet(state, "respLen",     0);
    sd->respPos     = saveStateGet(state, "respPos",     0);
    sd->dataPos     = saveStateGet(state, "dataPos",     0);
    /* dataLen defaults to BLOCK_LEN so old state files (which omitted the
    ** key) still complete a full-sector read after restore -- the common
    ** mid-flight case for CMD17/CMD18.  CSD/CID/SCR reads are short-lived
    ** so the legacy default rarely hits them. */
    sd->dataLen     = saveStateGet(state, "dataLen",     BLOCK_LEN);
    sd->dataSector  = saveStateGet(state, "dataSector",  0);
    sd->initialized = saveStateGet(state, "initialized", 0);
    sd->expectAcmd  = saveStateGet(state, "expectAcmd",  0);
    sd->status      = (UInt8)saveStateGet(state, "status", 0);
    sd->responseDelay = saveStateGet(state, "respDelay", 0);
    saveStateGetBuffer(state, "cmdBuf",  sd->cmdBuf,  sizeof(sd->cmdBuf));
    saveStateGetBuffer(state, "respBuf", sd->respBuf, sizeof(sd->respBuf));
    saveStateGetBuffer(state, "dataBuf", sd->dataBuf, sizeof(sd->dataBuf));

    saveStateClose(state);

    /* Re-derive capacity-dependent registers in case the image was
    ** resized between save and load. */
    sd->totalSectors = diskPresent(sd->diskId) ? readSectorCount(sd->diskId) : 0;
    buildCid(sd);
    buildCsd(sd);
    buildOcr(sd);
    buildScr(sd);
}
