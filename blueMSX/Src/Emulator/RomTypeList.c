/*****************************************************************************
**
** Tables of the rom types: the mappers and the built-in cartridges.
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
#include "RomTypeList.h"
#include "Properties.h"

#include <string.h>

/* One heading per group, in the order the groups are printed. */
static const char* const groupNames[ROMCAT_COUNT] = {
    NULL,
    "Plain roms with no mapper",
    "Common mappers",
    "Mappers unique to one game or program",
    "Multi game cartridges",
    "Flash cartridges",
    "Memory expansions",
    "Sound cartridges",
    "Disk and storage interfaces",
    "Network interfaces",
    "Kanji and dictionary roms",
    "Other hardware",
    "Non-MSX systems"
};

/* There are two tables because a rom type is used in two ways. Some types are
** in both: ASCII16-X can map an image and can also be inserted as a cartridge. */
typedef struct {
    RomType         romType;
    RomTypeCategory category;
} RomTypeMapper;

/* A hidden row is not listed, but its name is still accepted. */
typedef struct {
    RomType         romType;
    const char*     cartName;
    RomTypeCategory category;
    int             hidden;
} RomTypeCart;

/* The mappers that can be applied to a rom image. Only what MegaromCartridge.c
** can build from an image belongs here. */
static const RomTypeMapper mapperList[] = {
    { ROM_BASIC,                 ROMCAT_PLAIN         },
    { ROM_PLAIN,                 ROMCAT_PLAIN         },
    { ROM_0x4000,                ROMCAT_PLAIN         },
    { ROM_0xC000,                ROMCAT_PLAIN         },

    { ROM_ASCII8,                ROMCAT_COMMON        },
    { ROM_ASCII8SRAM,            ROMCAT_COMMON        },
    { ROM_ASCII16,               ROMCAT_COMMON        },
    { ROM_ASCII16SRAM,           ROMCAT_COMMON        },
    { ROM_ASCII16X,              ROMCAT_COMMON        },
    { ROM_KOEI,                  ROMCAT_COMMON        },
    { ROM_KONAMI4,               ROMCAT_COMMON        },
    { ROM_KONAMI4NF,             ROMCAT_COMMON        },
    { ROM_KONAMI5,               ROMCAT_COMMON        },
    { ROM_NEO8,                  ROMCAT_COMMON        },
    { ROM_NEO16,                 ROMCAT_COMMON        },
    { ROM_STANDARD,              ROMCAT_COMMON        },
    { ROM_YAMANOOTO,             ROMCAT_COMMON        },

    { ROM_CROSSBLAIM,            ROMCAT_ONEGAME       },
    { ROM_DOOLY,                 ROMCAT_ONEGAME       },
    { ROM_GAMEMASTER2,           ROMCAT_ONEGAME       },
    { ROM_HALNOTE,               ROMCAT_ONEGAME       },
    { ROM_HAMARAJANIGHT,         ROMCAT_ONEGAME       },
    { ROM_HARRYFOX,              ROMCAT_ONEGAME       },
    { ROM_HOLYQURAN,             ROMCAT_ONEGAME       },
    { ROM_NETTOUYAKYUU,          ROMCAT_ONEGAME       },
    { ROM_KONAMKBDMAS,           ROMCAT_ONEGAME       },
    { ROM_MAJUTSUSHI,            ROMCAT_ONEGAME       },
    { ROM_KONAMISYNTH,           ROMCAT_ONEGAME       },
    { ROM_KONWORDPRO,            ROMCAT_ONEGAME       },
    { ROM_LODERUNNER,            ROMCAT_ONEGAME       },
    { ROM_MANBOW2,               ROMCAT_ONEGAME       },
    { ROM_MANBOW2_V2,            ROMCAT_ONEGAME       },
    { ROM_MATRAINK,              ROMCAT_ONEGAME       },
    { ROM_ARC,                   ROMCAT_ONEGAME       },
    { ROM_RTYPE,                 ROMCAT_ONEGAME       },
    { ROM_PLAYBALL,              ROMCAT_ONEGAME       },
    { ROM_ASCII16NF,             ROMCAT_ONEGAME       },

    { ROM_KOREAN80,              ROMCAT_MULTIGAME     },
    { ROM_KOREAN90,              ROMCAT_MULTIGAME     },
    { ROM_KOREAN126,             ROMCAT_MULTIGAME     },

    { ROM_FLASHROMSCC,           ROMCAT_FLASH         },
    { ROM_MEGAFLSHSCC,           ROMCAT_FLASH         },
    { ROM_MEGAFLSHSCCPLUS,       ROMCAT_FLASH         },

    { ROM_FMPAC,                 ROMCAT_SOUND         },
    { ROM_FMPAK,                 ROMCAT_SOUND         },
    { ROM_MOONSOUND,             ROMCAT_SOUND         },
    { ROM_MSXAUDIO,              ROMCAT_SOUND         },
    { ROM_MUPACK,                ROMCAT_SOUND         },
    { ROM_YAMAHASFG01,           ROMCAT_SOUND         },
    { ROM_YAMAHASFG05,           ROMCAT_SOUND         },

    { ROM_BEERIDE,               ROMCAT_STORAGE       },
    { ROM_GOUDASCSI,             ROMCAT_STORAGE       },
    { SRAM_MEGASCSI,             ROMCAT_STORAGE       },
    { ROM_MICROSOL,              ROMCAT_STORAGE       },
    { ROM_MSXDOS2,               ROMCAT_STORAGE       },
    { ROM_NATIONALFDC,           ROMCAT_STORAGE       },
    { ROM_DISKPATCH,             ROMCAT_STORAGE       },
    { ROM_NOWIND,                ROMCAT_STORAGE       },
    { ROM_PHILIPSFDC,            ROMCAT_STORAGE       },
    { ROM_SUNRISEIDE,            ROMCAT_STORAGE       },
    { ROM_SVI707FDC,             ROMCAT_STORAGE       },
    { ROM_SVI738FDC,             ROMCAT_STORAGE       },
    { ROM_TC8566AF,              ROMCAT_STORAGE       },
    { ROM_TC8566AF_TR,           ROMCAT_STORAGE       },
    { SRAM_WAVESCSI,             ROMCAT_STORAGE       },

    { ROM_OBSONET,               ROMCAT_NETWORK       },
    { ROM_YAMAHANET,             ROMCAT_NETWORK       },

    { ROM_FMDAS,                 ROMCAT_KANJI         },
    { ROM_KANJI,                 ROMCAT_KANJI         },
    { ROM_KANJI12,               ROMCAT_KANJI         },

    { ROM_MICROSOL80,            ROMCAT_OTHER         },
    { ROM_SONYHBIV1,             ROMCAT_OTHER         },
    { ROM_SVI727COL80,           ROMCAT_OTHER         },

    { ROM_COLECO,                ROMCAT_NONMSX        },
    { ROM_ACTIVISIONPCB,         ROMCAT_NONMSX        },
    { ROM_ACTIVISIONPCB_2K,      ROMCAT_NONMSX        },
    { ROM_ACTIVISIONPCB_16K,     ROMCAT_NONMSX        },
    { ROM_ACTIVISIONPCB_256K,    ROMCAT_NONMSX        },
    { ROM_CVMEGACART,            ROMCAT_NONMSX        },
    { ROM_SC3000,                ROMCAT_NONMSX        },
    { ROM_SEGABASIC,             ROMCAT_NONMSX        },
    { ROM_SG1000_RAMEXPANDER_A,  ROMCAT_NONMSX        },
    { ROM_SG1000_RAMEXPANDER_B,  ROMCAT_NONMSX        },
    { ROM_SF7000IPL,             ROMCAT_NONMSX        },
    { ROM_SG1000,                ROMCAT_NONMSX        },
    { ROM_SG1000CASTLE,          ROMCAT_NONMSX        },
    { ROM_SVI328CART,            ROMCAT_NONMSX        },

    { ROM_UNKNOWN,               ROMCAT_NONE          }
};

/* The cartridges that can be inserted without an image. cartName is stored in
** the settings and the history, so it must never change. */
static const RomTypeCart cartList[] = {
    { ROM_ASCII16X,              CARTNAME_ASCII16X,           ROMCAT_FLASH,      0 },
    { SRAM_ESERAM128,            CARTNAME_ESERAM128,          ROMCAT_FLASH,      0 },
    { SRAM_ESERAM256,            CARTNAME_ESERAM256,          ROMCAT_FLASH,      0 },
    { SRAM_ESERAM512,            CARTNAME_ESERAM512,          ROMCAT_FLASH,      0 },
    { SRAM_ESERAM1MB,            CARTNAME_ESERAM1MB,          ROMCAT_FLASH,      0 },
    { SRAM_ESESCC128,            CARTNAME_ESESCC128,          ROMCAT_FLASH,      0 },
    { SRAM_ESESCC256,            CARTNAME_ESESCC256,          ROMCAT_FLASH,      0 },
    { SRAM_ESESCC512,            CARTNAME_ESESCC512,          ROMCAT_FLASH,      0 },
    { ROM_FLASHROMSCC,           CARTNAME_FLASHROMSCC,        ROMCAT_FLASH,      0 },
    { ROM_MEGAFLSHSCC,           CARTNAME_MEGAFLSHSCC,        ROMCAT_FLASH,      0 },
    { ROM_MEGAFLSHSCCPLUS,       CARTNAME_MEGAFLSHSCCPLUS,    ROMCAT_FLASH,      0 },
    { ROM_MEGARAM128,            CARTNAME_MEGARAM128,         ROMCAT_FLASH,      0 },
    { ROM_MEGARAM256,            CARTNAME_MEGARAM256,         ROMCAT_FLASH,      0 },
    { ROM_MEGARAM512,            CARTNAME_MEGARAM512,         ROMCAT_FLASH,      0 },
    { ROM_MEGARAM768,            CARTNAME_MEGARAM768,         ROMCAT_FLASH,      0 },
    { ROM_MEGARAM2M,             CARTNAME_MEGARAM2M,          ROMCAT_FLASH,      0 },
    { ROM_YAMANOOTO,             CARTNAME_YAMANOOTO,          ROMCAT_FLASH,      0 },

    { ROM_EXTRAM16KB,            CARTNAME_EXTRAM16KB,         ROMCAT_MEMORY,     0 },
    { ROM_EXTRAM32KB,            CARTNAME_EXTRAM32KB,         ROMCAT_MEMORY,     0 },
    { ROM_EXTRAM48KB,            CARTNAME_EXTRAM48KB,         ROMCAT_MEMORY,     0 },
    { ROM_EXTRAM64KB,            CARTNAME_EXTRAM64KB,         ROMCAT_MEMORY,     0 },
    { ROM_EXTRAM512KB,           CARTNAME_EXTRAM512KB,        ROMCAT_MEMORY,     0 },
    { ROM_EXTRAM1MB,             CARTNAME_EXTRAM1MB,          ROMCAT_MEMORY,     0 },
    { ROM_EXTRAM2MB,             CARTNAME_EXTRAM2MB,          ROMCAT_MEMORY,     0 },
    { ROM_EXTRAM4MB,             CARTNAME_EXTRAM4MB,          ROMCAT_MEMORY,     0 },

    { ROM_FMPAC,                 CARTNAME_FMPAC,              ROMCAT_SOUND,      0 },
    { ROM_JOYREXPSG,             CARTNAME_JOYREXPSG,          ROMCAT_SOUND,      0 },
    { ROM_PAC,                   CARTNAME_PAC,                ROMCAT_SOUND,      0 },
    { ROM_SCC,                   CARTNAME_SCC,                ROMCAT_SOUND,      0 },
    { ROM_SCCPLUS,               CARTNAME_SCCPLUS,            ROMCAT_SOUND,      0 },
    /* The same device as SCC above. Older settings files still use this name. */
    { ROM_SCCEXTENDED,           CARTNAME_SCCEXPANDED,        ROMCAT_SOUND,      1 },
    { ROM_SCCMIRRORED,           CARTNAME_SCCMIRRORED,        ROMCAT_SOUND,      0 },
    { ROM_SDSNATCHER,            CARTNAME_SDSNATCHER,         ROMCAT_SOUND,      0 },
    { ROM_SNATCHER,              CARTNAME_SNATCHER,           ROMCAT_SOUND,      0 },

    { ROM_BEERIDE,               CARTNAME_BEERIDE,            ROMCAT_STORAGE,    0 },
    { ROM_GIDE,                  CARTNAME_GIDE,               ROMCAT_STORAGE,    0 },
    { ROM_GOUDASCSI,             CARTNAME_GOUDASCSI,          ROMCAT_STORAGE,    0 },
    { ROM_MEGAFLSHSCCPLUS_SD,    CARTNAME_MEGAFLSHSCCPLUS_SD, ROMCAT_STORAGE,    0 },
    { SRAM_MEGASCSI128,          CARTNAME_MEGASCSI128,        ROMCAT_STORAGE,    0 },
    { SRAM_MEGASCSI256,          CARTNAME_MEGASCSI256,        ROMCAT_STORAGE,    0 },
    { SRAM_MEGASCSI512,          CARTNAME_MEGASCSI512,        ROMCAT_STORAGE,    0 },
    { SRAM_MEGASCSI1MB,          CARTNAME_MEGASCSI1MB,        ROMCAT_STORAGE,    0 },
    { ROM_NOWIND,                CARTNAME_NOWINDDOS1,         ROMCAT_STORAGE,    0 },
    /* The same device as the row above with the other boot rom. Hidden here,
    ** but the menu still offers it. */
    { ROM_NOWIND,                CARTNAME_NOWINDDOS2,         ROMCAT_STORAGE,    1 },
    { ROM_SUNRISEIDE,            CARTNAME_SUNRISEIDE,         ROMCAT_STORAGE,    0 },
    { SRAM_WAVESCSI128,          CARTNAME_WAVESCSI128,        ROMCAT_STORAGE,    0 },
    { SRAM_WAVESCSI256,          CARTNAME_WAVESCSI256,        ROMCAT_STORAGE,    0 },
    { SRAM_WAVESCSI512,          CARTNAME_WAVESCSI512,        ROMCAT_STORAGE,    0 },
    { SRAM_WAVESCSI1MB,          CARTNAME_WAVESCSI1MB,        ROMCAT_STORAGE,    0 },

    { ROM_GAMEREADER,            CARTNAME_GAMEREADER,         ROMCAT_OTHER,      0 },
    { ROM_SONYHBI55,             CARTNAME_SONYHBI55,          ROMCAT_OTHER,      0 },
    { ROM_NMS1210,               CARTNAME_NMS1210,            ROMCAT_OTHER,      0 },

    { ROM_UNKNOWN,               NULL,                        ROMCAT_NONE,       0 }
};

const char* romTypeListGroupName(int category)
{
    if (category <= (int)ROMCAT_NONE || category >= (int)ROMCAT_COUNT) {
        return NULL;
    }

    return groupNames[category];
}

const char* romTypeListCartGroupName(int category)
{
    /* ASCII16-X and Yamanooto are common mappers for an image and flash
    ** cartridges when inserted, so their group differs between the two tables. */
    if (category == (int)ROMCAT_FLASH) {
        return "Flash, SRAM and MegaRAM cartridges";
    }

    return romTypeListGroupName(category);
}

RomType romTypeListMapperAt(int index)
{
    if (index < 0 || index >= (int)(sizeof(mapperList) / sizeof(mapperList[0])) - 1) {
        return ROM_UNKNOWN;
    }

    return mapperList[index].romType;
}

RomTypeCategory romTypeListMapperCategory(RomType romType)
{
    int i;

    for (i = 0; mapperList[i].romType != ROM_UNKNOWN; i++) {
        if (mapperList[i].romType == romType) {
            return mapperList[i].category;
        }
    }

    return ROMCAT_NONE;
}

RomType romTypeListCartAt(int index)
{
    if (index < 0 || index >= (int)(sizeof(cartList) / sizeof(cartList[0])) - 1) {
        return ROM_UNKNOWN;
    }

    return cartList[index].romType;
}

RomTypeCategory romTypeListCartCategory(RomType romType)
{
    int i;

    for (i = 0; cartList[i].romType != ROM_UNKNOWN; i++) {
        if (cartList[i].romType == romType) {
            return cartList[i].category;
        }
    }

    return ROMCAT_NONE;
}

/* Indexed by row rather than by type, because two rows can name the same type. */
int romTypeListCartIsHiddenAt(int index)
{
    if (index < 0 || index >= (int)(sizeof(cartList) / sizeof(cartList[0])) - 1) {
        return 0;
    }

    return cartList[index].hidden;
}

const char* romTypeListCartName(RomType romType)
{
    int i;

    for (i = 0; cartList[i].romType != ROM_UNKNOWN; i++) {
        if (cartList[i].romType == romType) {
            return cartList[i].cartName;
        }
    }

    return NULL;
}

RomType romTypeListCartFromName(const char* cartName)
{
    int i;

    if (cartName == NULL) {
        return ROM_UNKNOWN;
    }
    for (i = 0; cartList[i].romType != ROM_UNKNOWN; i++) {
        if (0 == strcmp(cartList[i].cartName, cartName)) {
            return cartList[i].romType;
        }
    }

    return ROM_UNKNOWN;
}
