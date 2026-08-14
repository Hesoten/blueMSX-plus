/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Win32/Win32keyboard.c,v $
**
** $Revision: 1.35 $
**
** $Date: 2008-05-15 10:23:42 $
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
#define DIRECTINPUT_VERSION     0x0800
#include "Win32keyboard.h"
#include "Language.h"
#include "InputEvent.h"
#include "IniFileParser.h"
#include "JoystickPort.h"
#include "Properties.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <winioctl.h>
#include <dinput.h>
#include <xinput.h>
#include "Win32TextUtf8.h"


// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"

#define KBD_TABLE_LEN 512
#define KBD_TABLE_NUM 3

/* Cap per MSX key, and symmetrically the max MSX keys one PC key may
** fire. */
#define KBD_MAX_PER_EC 3

/* Per (table, EC): bound DIKs in insertion order, trailing zeros empty. */
static int bindingsDikForEc[KBD_TABLE_NUM][EC_KEYCOUNT][KBD_MAX_PER_EC];
static int bindingsBackup   [KBD_TABLE_NUM][EC_KEYCOUNT][KBD_MAX_PER_EC];

/* Entries the file left empty, so a save can tell "follow the built-in"
** from "the user cleared this". */
static char bindingsRewriteOnSave[KBD_TABLE_NUM][EC_KEYCOUNT];

/* Names for controls no attached device answers to, replayed when it
** arrives and written back out by a save. */
struct BindingsPending {
    short table;
    short ec;
    char  name[192];
};
static struct BindingsPending bindingsPending[64];
static int bindingsPendingCount;
static struct BindingsPending bindingsPendingBackup[64];
static int bindingsPendingBackupCount;

static int bindingsHasPending(int table, int ec)
{
    int k;
    for (k = 0; k < bindingsPendingCount; k++) {
        if (bindingsPending[k].table == table &&
            bindingsPending[k].ec == ec) return 1;
    }
    return 0;
}

/* Drop the pending name so a later arrival cannot undo a user edit. */
static void bindingsForgetPending(int table, int ec)
{
    int w = 0, k;
    for (k = 0; k < bindingsPendingCount; k++) {
        if (bindingsPending[k].table == table && bindingsPending[k].ec == ec) {
            continue;
        }
        bindingsPending[w++] = bindingsPending[k];
    }
    bindingsPendingCount = w;
}

/* One entry only: a hand-edited profile can name the same control twice
** on one event code. */
static void bindingsForgetPendingBackup(int table, int ec, const char* name)
{
    int w = 0, k, dropped = 0;
    for (k = 0; k < bindingsPendingBackupCount; k++) {
        if (!dropped && bindingsPendingBackup[k].table == table &&
            bindingsPendingBackup[k].ec == ec &&
            0 == strcmp(bindingsPendingBackup[k].name, name)) {
            dropped = 1;
            continue;
        }
        bindingsPendingBackup[w++] = bindingsPendingBackup[k];
    }
    bindingsPendingBackupCount = w;
}

/* Reverse index of bindingsDikForEc, maintained by every mutator. */
static int bindingsEcsForDik[KBD_TABLE_NUM][KBD_TABLE_LEN][KBD_MAX_PER_EC];

static int keyStatus[KBD_TABLE_NUM][KBD_TABLE_LEN];
static char dikStrings[KBD_TABLE_LEN][256];
static int selectedKey;
static int selectedDikKey;
static int selectedTable;
static int editEnabled;

/* The listing and the dropdown already refuse anything longer. */
#define KBD_CONFIGNAME_LEN 64

/* No larger: iniFileOpen copies the path it is given into a member of exactly
** this size, so a bigger buffer here would only move the overrun into that
** one. The directory is bounded where it is built, for the same reason. */
#define KBD_CONFIGPATH_LEN PROP_MAXPATH

/* PROP_MAXPATH rather than MAX_PATH: /rootdir accepts a longer path than 260. */
static char keyboardConfigDir[PROP_MAXPATH];
/* This one is only read: it is where the mappings the emulator ships with
** live. */
static char keyboardSharedDir[PROP_MAXPATH];

/* Profile written on a fresh install; the user's own file from then on. */
static char DefaultConfigName[] = "blueMSX";
static char JapaneseConfigName[] = "blueMSX Japanese";

/* Marker for a key the user cleared; an empty value means "use the
** default". */
static const char kUnassignedName[] = "(none)";

static char currentConfigFile[MAX_PATH];

static int bindingsSlotHas(int slots[KBD_MAX_PER_EC], int value) {
    int i;
    for (i = 0; i < KBD_MAX_PER_EC; i++) if (slots[i] == value) return 1;
    return 0;
}

static int bindingsSlotAppend(int slots[KBD_MAX_PER_EC], int value) {
    int i;
    for (i = 0; i < KBD_MAX_PER_EC; i++) {
        if (slots[i] == 0) { slots[i] = value; return 1; }
    }
    return 0;
}

static void bindingsSlotRemove(int slots[KBD_MAX_PER_EC], int value) {
    int i, w = 0;
    int keep[KBD_MAX_PER_EC];
    memset(keep, 0, sizeof(keep));
    for (i = 0; i < KBD_MAX_PER_EC; i++) {
        if (slots[i] != 0 && slots[i] != value) keep[w++] = slots[i];
    }
    for (i = 0; i < KBD_MAX_PER_EC; i++) slots[i] = keep[i];
}

static void bindingsResetAll(void) {
    memset(bindingsDikForEc,  0, sizeof(bindingsDikForEc));
    memset(bindingsEcsForDik, 0, sizeof(bindingsEcsForDik));
}

static int bindingsHasEdge(int table, int dik, int ec) {
    if (dik <= 0 || ec <= 0 || ec >= EC_KEYCOUNT) return 0;
    return bindingsSlotHas(bindingsDikForEc[table][ec], dik);
}

static int bindingsCountDiksForEc(int table, int ec) {
    int b, n = 0;
    if (ec <= 0 || ec >= EC_KEYCOUNT) return 0;
    for (b = 0; b < KBD_MAX_PER_EC; b++) {
        if (bindingsDikForEc[table][ec][b] != 0) n++;
    }
    return n;
}

static int bindingsCountEcsForDik(int table, int dik) {
    int b, n = 0;
    if (dik <= 0 || dik >= KBD_TABLE_LEN) return 0;
    for (b = 0; b < KBD_MAX_PER_EC; b++) {
        if (bindingsEcsForDik[table][dik][b] != 0) n++;
    }
    return n;
}

/* Slot 0 is populated iff the DIK is bound, since removal compacts. */
static int bindingsHasAnyEcForDik(int table, int dik) {
    if (dik <= 0 || dik >= KBD_TABLE_LEN) return 0;
    return bindingsEcsForDik[table][dik][0] != 0;
}

/* Reverse capacity is checked first so a rejected edge leaves both
** arrays untouched. */
static int bindingsAddEdge(int table, int dik, int ec) {
    int i, revHasSpace = 0;
    if (dik <= 0 || dik >= KBD_TABLE_LEN || ec <= 0 || ec >= EC_KEYCOUNT) return 0;
    if (bindingsSlotHas(bindingsDikForEc[table][ec], dik)) return 0;
    for (i = 0; i < KBD_MAX_PER_EC; i++) {
        if (bindingsEcsForDik[table][dik][i] == 0) { revHasSpace = 1; break; }
    }
    if (!revHasSpace) return 0;
    if (!bindingsSlotAppend(bindingsDikForEc[table][ec], dik)) return 0;
    bindingsSlotAppend(bindingsEcsForDik[table][dik], ec);
    return 1;
}

static int bindingsRemoveEdge(int table, int dik, int ec) {
    if (dik <= 0 || dik >= KBD_TABLE_LEN || ec <= 0 || ec >= EC_KEYCOUNT) return 0;
    if (!bindingsSlotHas(bindingsDikForEc[table][ec], dik)) return 0;
    bindingsSlotRemove(bindingsDikForEc[table][ec], dik);
    bindingsSlotRemove(bindingsEcsForDik[table][dik], ec);
    return 1;
}

static void bindingsClearEc(int table, int ec) {
    int b;
    int diks[KBD_MAX_PER_EC];
    if (ec <= 0 || ec >= EC_KEYCOUNT) return;
    memcpy(diks, bindingsDikForEc[table][ec], sizeof(diks));
    for (b = 0; b < KBD_MAX_PER_EC; b++) {
        if (diks[b] != 0) bindingsSlotRemove(bindingsEcsForDik[table][diks[b]], ec);
        bindingsDikForEc[table][ec][b] = 0;
    }
}

static int bindingsGetDiksForEc(int table, int ec, int out[KBD_MAX_PER_EC]) {
    int b, n = 0;
    for (b = 0; b < KBD_MAX_PER_EC; b++) out[b] = 0;
    if (ec <= 0 || ec >= EC_KEYCOUNT) return 0;
    for (b = 0; b < KBD_MAX_PER_EC; b++) {
        int cur = bindingsDikForEc[table][ec][b];
        if (cur != 0) out[n++] = cur;
    }
    return n;
}

/* inputEvent has no refcount, so one DIK releasing must not clear an EC
** another still holds. */
static int bindingsOtherDikHeld(int table, int ec, int dik) {
    int b;
    for (b = 0; b < KBD_MAX_PER_EC; b++) {
        int other = bindingsDikForEc[table][ec][b];
        if (other == 0 || other == dik) continue;
        if (keyStatus[table][other]) return 1;
    }
    return 0;
}

static void bindingsEmitDikEvents(int table, int dik, int pressed) {
    int b;
    if (dik <= 0 || dik >= KBD_TABLE_LEN) return;
    for (b = 0; b < KBD_MAX_PER_EC; b++) {
        int ec = bindingsEcsForDik[table][dik][b];
        if (ec == 0) continue;
        if (pressed) {
            inputEventSet(ec);
        }
        else if (!bindingsOtherDikHeld(table, ec, dik)) {
            inputEventUnset(ec);
        }
    }
}

/* The source already passed the capacity check, so append cannot fail. */
static void bindingsRebuildReverseIndex(void) {
    int t, ec, b, dik;
    memset(bindingsEcsForDik, 0, sizeof(bindingsEcsForDik));
    for (t = 0; t < KBD_TABLE_NUM; t++) {
        for (ec = 1; ec < EC_KEYCOUNT; ec++) {
            for (b = 0; b < KBD_MAX_PER_EC; b++) {
                dik = bindingsDikForEc[t][ec][b];
                if (dik > 0 && dik < KBD_TABLE_LEN) {
                    bindingsSlotAppend(bindingsEcsForDik[t][dik], ec);
                }
            }
        }
    }
}

static void bindingsBackupAll(void) {
    memcpy(bindingsBackup, bindingsDikForEc, sizeof(bindingsBackup));
    memcpy(bindingsPendingBackup, bindingsPending, sizeof(bindingsPendingBackup));
    bindingsPendingBackupCount = bindingsPendingCount;
}

static void bindingsRestoreAll(void) {
    memcpy(bindingsDikForEc, bindingsBackup, sizeof(bindingsBackup));
    memcpy(bindingsPending, bindingsPendingBackup, sizeof(bindingsPending));
    bindingsPendingCount = bindingsPendingBackupCount;
    bindingsRebuildReverseIndex();
}

/* Field by field: the bytes past each name's terminator are whatever an
** earlier profile left there. */
static int bindingsPendingDiffers(void) {
    int k;
    if (bindingsPendingCount != bindingsPendingBackupCount) return 1;
    for (k = 0; k < bindingsPendingCount; k++) {
        if (bindingsPending[k].table != bindingsPendingBackup[k].table ||
            bindingsPending[k].ec    != bindingsPendingBackup[k].ec) return 1;
        if (0 != strcmp(bindingsPending[k].name,
                        bindingsPendingBackup[k].name)) return 1;
    }
    return 0;
}

static int bindingsDiffersFromBackup(void) {
    return memcmp(bindingsDikForEc, bindingsBackup, sizeof(bindingsBackup)) != 0 ||
           bindingsPendingDiffers();
}

/* 28 buttons then the four directions fill each slot's 32 DIK codes. */
#define JOY_MAX_BUTTONS   28

#define KEY_CODE_BUTTON1  256
#define KEY_CODE_JOYUP    (256 + 28)
#define KEY_CODE_JOYDOWN  (256 + 29)
#define KEY_CODE_JOYLEFT  (256 + 30)
#define KEY_CODE_JOYRIGHT (256 + 31)

#define DIK_JOY1_BUTTON1  256
#define DIK_JOY1_BUTTON2  257
#define DIK_JOY1_BUTTON3  258
#define DIK_JOY1_BUTTON4  259
#define DIK_JOY1_BUTTON5  260
#define DIK_JOY1_BUTTON6  261
#define DIK_JOY1_WHEELA   262
#define DIK_JOY1_WHEELB   263
#define DIK_JOY1_UP       284
#define DIK_JOY1_DOWN     285
#define DIK_JOY1_LEFT     286
#define DIK_JOY1_RIGHT    287

#define DIK_JOY2_BUTTON1  288
#define DIK_JOY2_BUTTON2  289
#define DIK_JOY2_BUTTON3  290
#define DIK_JOY2_BUTTON4  291
#define DIK_JOY2_BUTTON5  292
#define DIK_JOY2_BUTTON6  293
#define DIK_JOY2_WHEELA   294
#define DIK_JOY2_WHEELB   295
#define DIK_JOY2_UP       316
#define DIK_JOY2_DOWN     317
#define DIK_JOY2_LEFT     318
#define DIK_JOY2_RIGHT    319

/* Joystick entries name their control instead of using a DIK: a slot
** number would follow whichever pad enumerates first. */
struct BindingsDefault {
    int table;
    int dik;
    int ec;
    const char* name;
};

static const struct BindingsDefault bindingsDefaults[] = {
    /* MSX keyboard (table 0) */
    { 0, DIK_0,           EC_0        }, { 0, DIK_1,           EC_1        },
    { 0, DIK_2,           EC_2        }, { 0, DIK_3,           EC_3        },
    { 0, DIK_4,           EC_4        }, { 0, DIK_5,           EC_5        },
    { 0, DIK_6,           EC_6        }, { 0, DIK_7,           EC_7        },
    { 0, DIK_8,           EC_8        }, { 0, DIK_9,           EC_9        },
    { 0, DIK_MINUS,       EC_NEG      }, { 0, DIK_EQUALS,      EC_CIRCFLX  },
    { 0, DIK_BACKSLASH,   EC_BKSLASH  }, { 0, DIK_LBRACKET,    EC_AT       },
    { 0, DIK_RBRACKET,    EC_LBRACK   }, { 0, DIK_SEMICOLON,   EC_SEMICOL  },
    { 0, DIK_APOSTROPHE,  EC_COLON    }, { 0, DIK_GRAVE,       EC_RBRACK   },
    { 0, DIK_COMMA,       EC_COMMA    }, { 0, DIK_PERIOD,      EC_PERIOD   },
    { 0, DIK_SLASH,       EC_DIV      }, { 0, DIK_RCONTROL,    EC_UNDSCRE  },
    { 0, DIK_A, EC_A }, { 0, DIK_B, EC_B }, { 0, DIK_C, EC_C },
    { 0, DIK_D, EC_D }, { 0, DIK_E, EC_E }, { 0, DIK_F, EC_F },
    { 0, DIK_G, EC_G }, { 0, DIK_H, EC_H }, { 0, DIK_I, EC_I },
    { 0, DIK_J, EC_J }, { 0, DIK_K, EC_K }, { 0, DIK_L, EC_L },
    { 0, DIK_M, EC_M }, { 0, DIK_N, EC_N }, { 0, DIK_O, EC_O },
    { 0, DIK_P, EC_P }, { 0, DIK_Q, EC_Q }, { 0, DIK_R, EC_R },
    { 0, DIK_S, EC_S }, { 0, DIK_T, EC_T }, { 0, DIK_U, EC_U },
    { 0, DIK_V, EC_V }, { 0, DIK_W, EC_W }, { 0, DIK_X, EC_X },
    { 0, DIK_Y, EC_Y }, { 0, DIK_Z, EC_Z },
    { 0, DIK_F1,          EC_F1       }, { 0, DIK_F2,          EC_F2       },
    { 0, DIK_F3,          EC_F3       }, { 0, DIK_F4,          EC_F4       },
    { 0, DIK_F5,          EC_F5       }, { 0, DIK_ESCAPE,      EC_ESC      },
    { 0, DIK_TAB,         EC_TAB      }, { 0, DIK_PRIOR,       EC_STOP     },
    { 0, DIK_BACK,        EC_BKSPACE  }, { 0, DIK_END,         EC_SELECT   },
    { 0, DIK_RETURN,      EC_RETURN   }, { 0, DIK_SPACE,       EC_SPACE    },
    { 0, DIK_HOME,        EC_CLS      }, { 0, DIK_INSERT,      EC_INS      },
    { 0, DIK_DELETE,      EC_DEL      }, { 0, DIK_LEFT,        EC_LEFT     },
    { 0, DIK_UP,          EC_UP       }, { 0, DIK_RIGHT,       EC_RIGHT    },
    { 0, DIK_DOWN,        EC_DOWN     },
    { 0, DIK_MULTIPLY,    EC_NUMMUL   }, { 0, DIK_ADD,         EC_NUMADD   },
    { 0, DIK_DIVIDE,      EC_NUMDIV   }, { 0, DIK_SUBTRACT,    EC_NUMSUB   },
    { 0, DIK_DECIMAL,     EC_NUMPER   }, { 0, DIK_NEXT,        EC_NUMCOM   },
    { 0, DIK_NUMPAD0,     EC_NUM0     }, { 0, DIK_NUMPAD1,     EC_NUM1     },
    { 0, DIK_NUMPAD2,     EC_NUM2     }, { 0, DIK_NUMPAD3,     EC_NUM3     },
    { 0, DIK_NUMPAD4,     EC_NUM4     }, { 0, DIK_NUMPAD5,     EC_NUM5     },
    { 0, DIK_NUMPAD6,     EC_NUM6     }, { 0, DIK_NUMPAD7,     EC_NUM7     },
    { 0, DIK_NUMPAD8,     EC_NUM8     }, { 0, DIK_NUMPAD9,     EC_NUM9     },
    { 0, DIK_LWIN,        EC_TORIKE   }, { 0, DIK_RWIN,        EC_JIKKOU   },
    { 0, DIK_LSHIFT,      EC_LSHIFT   }, { 0, DIK_RSHIFT,      EC_RSHIFT   },
    { 0, DIK_LCONTROL,    EC_CTRL     }, { 0, DIK_LMENU,       EC_GRAPH    },
    { 0, DIK_RMENU,       EC_CODE     }, { 0, DIK_CAPITAL,     EC_CAPS     },
    { 0, DIK_NUMPADENTER, EC_PAUSE    }, { 0, DIK_SYSRQ,       EC_PRINT    },

    /* Joystick 1 (table 1) -- XInput player 1 */
    { 1, 0, EC_JOY1_BUTTON1, "XInput 1 : A"     },
    { 1, 0, EC_JOY1_BUTTON2, "XInput 1 : B"     },
    { 1, 0, EC_JOY1_BUTTON3, "XInput 1 : X"     },
    { 1, 0, EC_JOY1_BUTTON4, "XInput 1 : Y"     },
    { 1, 0, EC_JOY1_WHEELA,  "XInput 1 : Back"  },
    { 1, 0, EC_JOY1_WHEELB,  "XInput 1 : Start" },
    { 1, 0, EC_JOY1_UP,      "XInput 1 : up"    },
    { 1, 0, EC_JOY1_DOWN,    "XInput 1 : down"  },
    { 1, 0, EC_JOY1_LEFT,    "XInput 1 : left"  },
    { 1, 0, EC_JOY1_RIGHT,   "XInput 1 : right" },
    { 1, DIK_0, EC_COLECO1_0 }, { 1, DIK_1, EC_COLECO1_1 },
    { 1, DIK_2, EC_COLECO1_2 }, { 1, DIK_3, EC_COLECO1_3 },
    { 1, DIK_4, EC_COLECO1_4 }, { 1, DIK_5, EC_COLECO1_5 },
    { 1, DIK_6, EC_COLECO1_6 }, { 1, DIK_7, EC_COLECO1_7 },
    { 1, DIK_8, EC_COLECO1_8 }, { 1, DIK_9, EC_COLECO1_9 },
    { 1, DIK_MINUS,        EC_COLECO1_STAR },
    { 1, DIK_EQUALS,       EC_COLECO1_HASH },

    /* Joystick 2 (table 2) -- XInput player 2 */
    { 2, 0, EC_JOY2_BUTTON1, "XInput 2 : A"     },
    { 2, 0, EC_JOY2_BUTTON2, "XInput 2 : B"     },
    { 2, 0, EC_JOY2_BUTTON3, "XInput 2 : X"     },
    { 2, 0, EC_JOY2_BUTTON4, "XInput 2 : Y"     },
    { 2, 0, EC_JOY2_WHEELA,  "XInput 2 : Back"  },
    { 2, 0, EC_JOY2_WHEELB,  "XInput 2 : Start" },
    { 2, 0, EC_JOY2_UP,      "XInput 2 : up"    },
    { 2, 0, EC_JOY2_DOWN,    "XInput 2 : down"  },
    { 2, 0, EC_JOY2_LEFT,    "XInput 2 : left"  },
    { 2, 0, EC_JOY2_RIGHT,   "XInput 2 : right" },
    { 2, DIK_NUMPAD0, EC_COLECO2_0 }, { 2, DIK_NUMPAD1, EC_COLECO2_1 },
    { 2, DIK_NUMPAD2, EC_COLECO2_2 }, { 2, DIK_NUMPAD3, EC_COLECO2_3 },
    { 2, DIK_NUMPAD4, EC_COLECO2_4 }, { 2, DIK_NUMPAD5, EC_COLECO2_5 },
    { 2, DIK_NUMPAD6, EC_COLECO2_6 }, { 2, DIK_NUMPAD7, EC_COLECO2_7 },
    { 2, DIK_NUMPAD8, EC_COLECO2_8 }, { 2, DIK_NUMPAD9, EC_COLECO2_9 },
    { 2, DIK_MULTIPLY,     EC_COLECO2_STAR },
    { 2, DIK_DIVIDE,       EC_COLECO2_HASH }
};

/* Also keeps ECs no attached device reads out of the conflict count. */
static int inputPortEcActive(int table, int ec)
{
    JoystickPortType type = joystickPortGetType(table - 1);
    int isBasic, isSuper, isKeypad;

    if (table == 1) {
        isBasic  = ec >= EC_JOY1_UP && ec <= EC_JOY1_BUTTON2;
        isSuper  = (ec >= EC_JOY1_BUTTON3 && ec <= EC_JOY1_BUTTON4) ||
                   ec == EC_JOY1_WHEELA || ec == EC_JOY1_WHEELB;
        isKeypad = ec >= EC_COLECO1_0 && ec <= EC_COLECO1_HASH;
    }
    else {
        isBasic  = ec >= EC_JOY2_UP && ec <= EC_JOY2_BUTTON2;
        isSuper  = (ec >= EC_JOY2_BUTTON3 && ec <= EC_JOY2_BUTTON4) ||
                   ec == EC_JOY2_WHEELA || ec == EC_JOY2_WHEELB;
        isKeypad = ec >= EC_COLECO2_0 && ec <= EC_COLECO2_HASH;
    }

    switch (type) {
    case JOYSTICK_PORT_JOYSTICK:       return isBasic;
    case JOYSTICK_PORT_COLECOJOYSTICK: return isBasic || isKeypad;
    case JOYSTICK_PORT_SUPERACTION:    return isBasic || isSuper || isKeypad;
    default:                           return 0;
    }
}

/* JIS overrides: each replaces the base DIK for the same EC, never adds
** a second one. */
static const struct BindingsDefault bindingsDefaultsJp[] = {
    { 0, DIK_AT,          EC_AT       },
    { 0, DIK_LBRACKET,    EC_LBRACK   },
    { 0, DIK_RBRACKET,    EC_RBRACK   },
    { 0, DIK_YEN,         EC_BKSLASH  },
    { 0, DIK_BACKSLASH,   EC_UNDSCRE  },
    { 0, DIK_PREVTRACK,   EC_CIRCFLX  },
    { 0, DIK_COLON,       EC_COLON    },
    { 0, DIK_KANA,        EC_CODE     },
    { 0, DIK_CONVERT,     EC_JIKKOU   },
    { 0, DIK_NOCONVERT,   EC_TORIKE   },
};

int inputKeyboardRegionIsJapanese(void)
{
    static int cached = -1;
    if (cached < 0) {
        char klId[KL_NAMELENGTH];
        cached = 0;
        if (GetKeyboardLayoutName(klId) && 0 == strcmp(klId + 4, "0411")) {
            cached = 1;
        }
        /* A US keyboard under a ja-JP region still wants the MSX JP
        ** layout. */
        if (PRIMARYLANGID(LANGIDFROMLCID(GetUserDefaultLCID())) == LANG_JAPANESE) {
            cached = 1;
        }
    }
    return cached;
}

static int bindingsJpOverride(int table, int ec, int jp)
{
    size_t i;
    if (table != 0 || !jp) return 0;
    for (i = 0; i < sizeof(bindingsDefaultsJp) / sizeof(bindingsDefaultsJp[0]); i++) {
        if (bindingsDefaultsJp[i].ec == ec) return bindingsDefaultsJp[i].dik;
    }
    return 0;
}

int inputResolveDikName(const char* token);
static int bindingsTableForEc(int ec);

static void bindingsFillDefaults(int table, int portDeviceOnly, int jp) {
    size_t i;
    for (i = 0; i < sizeof(bindingsDefaults) / sizeof(bindingsDefaults[0]); i++) {
        int dik;
        if (bindingsDefaults[i].table != table) continue;
        if (portDeviceOnly && table != 0 &&
            !inputPortEcActive(table, bindingsDefaults[i].ec)) continue;
        dik = bindingsJpOverride(table, bindingsDefaults[i].ec, jp);
        if (dik == 0) {
            dik = bindingsDefaults[i].name != NULL
                      ? inputResolveDikName(bindingsDefaults[i].name)
                      : bindingsDefaults[i].dik;
        }
        if (dik > 0) {
            bindingsAddEdge(table, dik, bindingsDefaults[i].ec);
        }
    }
}

static void bindingsLoadDefaultsForTable(int table, int jp) {
    bindingsFillDefaults(table, 0, jp);
}

static void bindingsLoadDefaultsForPortDevice(int table) {
    bindingsFillDefaults(table, 1, inputKeyboardRegionIsJapanese());
}

/* Existence only: must not depend on which controllers are attached at
** save time. */
static int bindingsDefaultExists(int table, int ec) {
    size_t i;
    for (i = 0; i < sizeof(bindingsDefaults) / sizeof(bindingsDefaults[0]); i++) {
        if (bindingsDefaults[i].table == table &&
            bindingsDefaults[i].ec == ec) return 1;
    }
    return 0;
}

/* Never scoped: load-time port type must not decide what a shared
** profile holds. */
static void bindingsLoadDefaultsForEc(int table, int ec) {
    size_t i;
    for (i = 0; i < sizeof(bindingsDefaults) / sizeof(bindingsDefaults[0]); i++) {
        int dik;
        if (bindingsDefaults[i].table != table) continue;
        if (bindingsDefaults[i].ec != ec) continue;
        dik = bindingsJpOverride(table, ec, inputKeyboardRegionIsJapanese());
        if (dik == 0) {
            dik = bindingsDefaults[i].name != NULL
                      ? inputResolveDikName(bindingsDefaults[i].name)
                      : bindingsDefaults[i].dik;
        }
        if (dik > 0 && bindingsCountEcsForDik(table, dik) == 0) {
            bindingsAddEdge(table, dik, ec);
        }
    }
}

/* jp: 1 lays in the JIS key positions, 0 the European ones. */
static void bindingsLoadDefaultsForLayout(int jp) {
    int t;
    bindingsResetAll();
    for (t = 0; t < KBD_TABLE_NUM; t++) {
        bindingsLoadDefaultsForTable(t, jp);
    }
}

static void bindingsLoadDefaults(void) {
    bindingsLoadDefaultsForLayout(inputKeyboardRegionIsJapanese());
}

int archKeyboardTableIsResettable(int table)
{
    if (table < 0 || table >= KBD_TABLE_NUM) return 0;
    if (table == 0) return 1;
    /* Every assignable port device reads the directions, so UP answers
    ** for the whole table. */
    return inputPortEcActive(table, table == 1 ? EC_JOY1_UP : EC_JOY2_UP);
}

/* Explicit user action only: a port or machine change never rewrites
** bindings. */
void archKeyboardResetTableDefaults(int table)
{
    int ec;
    if (!archKeyboardTableIsResettable(table)) return;
    for (ec = 1; ec < EC_KEYCOUNT; ec++) {
        /* Or resetting a 2-button pad would wipe the Coleco keypad set up
        ** for another device. */
        if (table != 0 && !inputPortEcActive(table, ec)) continue;
        bindingsClearEc(table, ec);
        bindingsForgetPending(table, ec);
    }
    bindingsLoadDefaultsForPortDevice(table);
    inputEventReset();
    selectedDikKey = 0;
}

int bindingsCountTargetsForDik(int dik) {
    int n, b, total = 0;
    if (dik <= 0 || dik >= KBD_TABLE_LEN) return 0;
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        for (b = 0; b < KBD_MAX_PER_EC; b++) {
            int ec = bindingsEcsForDik[n][dik][b];
            if (ec == 0) continue;
            if (n != 0 && !inputPortEcActive(n, ec)) continue;
            total++;
        }
    }
    return total;
}

/* excludeTable/excludeEc drop the entry the caller is describing. */
int bindingsDescribeTargetsForDik(int dik, int excludeTable, int excludeEc,
                                  char* out, int outLen) {
    int n, b, count = 0;
    if (outLen <= 0) return 0;
    out[0] = 0;
    if (dik <= 0 || dik >= KBD_TABLE_LEN) return 0;
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        for (b = 0; b < KBD_MAX_PER_EC; b++) {
            int ec = bindingsEcsForDik[n][dik][b];
            const char* name;
            if (ec == 0) continue;
            if (n == excludeTable && ec == excludeEc) continue;
            if (n != 0 && !inputPortEcActive(n, ec)) continue;
            name = inputEventCodeToString(ec);
            if (name == NULL || name[0] == 0) continue;
            if (out[0]) strncat(out, ", ", outLen - strlen(out) - 1);
            if (n == 0) {
                /* Through the translation, or the word lands after the key
                ** name in every language that puts it first. */
                char named[96];
                _snprintf(named, sizeof(named) - 1, langKeyboardKeyFormat(), name);
                named[sizeof(named) - 1] = 0;
                strncat(out, named, outLen - strlen(out) - 1);
            }
            else {
                strncat(out, name, outLen - strlen(out) - 1);
            }
            count++;
        }
    }
    return count;
}

/* Injected at startup by Win32ShortcutsConfig; this file cannot see the
** shortcut table. */
static ShortcutDikCounter    shortcutDikCounter    = NULL;
static ShortcutDikDescriber  shortcutDikDescriber  = NULL;
static ShortcutDeviceArrival shortcutDeviceArrival = NULL;

void inputSetShortcutDikCounter(ShortcutDikCounter fn) {
    shortcutDikCounter = fn;
}

void inputSetShortcutDeviceArrival(ShortcutDeviceArrival fn) {
    shortcutDeviceArrival = fn;
}

void inputSetShortcutDikDescriber(ShortcutDikDescriber fn) {
    shortcutDikDescriber = fn;
}

/* All tables are polled concurrently, so any second target double-fires. */
static int inputDikConflicts(int dik) {
    int total = bindingsCountTargetsForDik(dik);
    if (shortcutDikCounter) total += shortcutDikCounter(dik);
    return total > 1;
}

#define INIT_DIK(val) strcpy(dikStrings[DIK_##val], #val)

static void initDikStr()
{
    INIT_DIK(0);
    INIT_DIK(1);
    INIT_DIK(2);
    INIT_DIK(3);
    INIT_DIK(4);
    INIT_DIK(5);
    INIT_DIK(6);
    INIT_DIK(7);
    INIT_DIK(8);
    INIT_DIK(9);
    INIT_DIK(A);
    INIT_DIK(ABNT_C1);
    INIT_DIK(ABNT_C2);
    INIT_DIK(ADD);
    INIT_DIK(APOSTROPHE);
    INIT_DIK(APPS);
    INIT_DIK(AT);
    INIT_DIK(AX);
    INIT_DIK(B);
    INIT_DIK(BACK);
    INIT_DIK(BACKSLASH);
    INIT_DIK(C);
    INIT_DIK(CALCULATOR);
    INIT_DIK(CAPITAL);
    INIT_DIK(COLON);
    INIT_DIK(COMMA);
    INIT_DIK(CONVERT);
    INIT_DIK(D);
    INIT_DIK(DECIMAL);
    INIT_DIK(DELETE);
    INIT_DIK(DIVIDE);
    INIT_DIK(DOWN);
    INIT_DIK(E);
    INIT_DIK(END);
    INIT_DIK(EQUALS);
    INIT_DIK(ESCAPE);
    INIT_DIK(F);
    INIT_DIK(F1);
    INIT_DIK(F2);
    INIT_DIK(F3);
    INIT_DIK(F4);
    INIT_DIK(F5);
    INIT_DIK(F6);
    INIT_DIK(F7);
    INIT_DIK(F8);
    INIT_DIK(F9);
    INIT_DIK(F10);
    INIT_DIK(F11);
    INIT_DIK(F12);
    INIT_DIK(F13);
    INIT_DIK(F14);
    INIT_DIK(F15);
    INIT_DIK(G);
    INIT_DIK(GRAVE);
    INIT_DIK(H);
    INIT_DIK(HOME);
    INIT_DIK(I);
    INIT_DIK(INSERT);
    INIT_DIK(J);
    INIT_DIK(K);
    INIT_DIK(KANA);
    INIT_DIK(KANJI);
    INIT_DIK(L);
    INIT_DIK(LBRACKET);
    INIT_DIK(LCONTROL);
    INIT_DIK(LEFT);
    INIT_DIK(LMENU);
    INIT_DIK(LSHIFT);
    INIT_DIK(LWIN);
    INIT_DIK(M);
    INIT_DIK(MAIL);
    INIT_DIK(MEDIASELECT);
    INIT_DIK(MEDIASTOP);
    INIT_DIK(MINUS);
    INIT_DIK(MULTIPLY);
    INIT_DIK(MUTE);
    INIT_DIK(MYCOMPUTER);
    INIT_DIK(N);
    INIT_DIK(NEXT);
    INIT_DIK(NEXTTRACK);
    INIT_DIK(NOCONVERT);
    INIT_DIK(NUMLOCK);
    INIT_DIK(NUMPAD0);
    INIT_DIK(NUMPAD1);
    INIT_DIK(NUMPAD2);
    INIT_DIK(NUMPAD3);
    INIT_DIK(NUMPAD4);
    INIT_DIK(NUMPAD5);
    INIT_DIK(NUMPAD6);
    INIT_DIK(NUMPAD7);
    INIT_DIK(NUMPAD8);
    INIT_DIK(NUMPAD9);
    INIT_DIK(NUMPADCOMMA);
    INIT_DIK(NUMPADENTER);
    INIT_DIK(NUMPADEQUALS);
    INIT_DIK(O);
    INIT_DIK(OEM_102);
    INIT_DIK(P);
    INIT_DIK(PAUSE);
    INIT_DIK(PERIOD);
    INIT_DIK(PLAYPAUSE);
    INIT_DIK(POWER);
    INIT_DIK(PREVTRACK);
    INIT_DIK(PRIOR);
    INIT_DIK(Q);
    INIT_DIK(R);
    INIT_DIK(RBRACKET);
    INIT_DIK(RCONTROL);
    INIT_DIK(RETURN);
    INIT_DIK(RIGHT);
    INIT_DIK(RMENU);
    INIT_DIK(RSHIFT);
    INIT_DIK(RWIN);
    INIT_DIK(S);
    INIT_DIK(SCROLL);
    INIT_DIK(SEMICOLON);
    INIT_DIK(SLASH);
    INIT_DIK(SLEEP);
    INIT_DIK(SPACE);
    INIT_DIK(STOP);
    INIT_DIK(SUBTRACT);
    INIT_DIK(SYSRQ);
    INIT_DIK(T);
    INIT_DIK(TAB);
    INIT_DIK(U);
    INIT_DIK(UNDERLINE);
    INIT_DIK(UNLABELED);
    INIT_DIK(UP);
    INIT_DIK(V);
    INIT_DIK(VOLUMEDOWN);
    INIT_DIK(VOLUMEUP);
    INIT_DIK(W);
    INIT_DIK(WAKE);
    INIT_DIK(WEBBACK);
    INIT_DIK(WEBFAVORITES);
    INIT_DIK(WEBFORWARD);
    INIT_DIK(WEBHOME);
    INIT_DIK(WEBREFRESH);
    INIT_DIK(WEBSEARCH);
    INIT_DIK(WEBSTOP);
    INIT_DIK(X);
    INIT_DIK(Y);
    INIT_DIK(YEN);
    INIT_DIK(Z);
}

char* dik2str(int dikKey)
{
    if (dikKey < 0 || dikKey >= KBD_TABLE_LEN) {
        return "";
    }
    return dikStrings[dikKey];
}

/* Display only; saves must use the full name so tokens round-trip. */
char* dikNameToDisplay(char* full)
{
    enum { MAX_DEV_LEN = 8 };  /* including trailing '~' */
    static char buf[256];
    char name[MAX_DEV_LEN + 1];
    const char* sep;
    const char* s;
    const char* suffix;
    int len, xi, pos;

    sep = NULL;
    for (s = full; (s = strstr(s, " : ")) != NULL; s++) sep = s;
    if (sep == NULL) {
        return full;
    }
    len    = (int)(sep - full);
    suffix = sep + 3;

    pos = 0;
    if (sscanf(full, "XInput %d%n", &xi, &pos) == 1 && pos == len &&
        xi >= 1 && xi <= 4)
    {
        sprintf(name, "XInput%d", xi);
    }
    else if (len > MAX_DEV_LEN) {
        /* Back off to a character start: a cut UTF-8 sequence draws a
        ** replacement glyph. */
        int cut = MAX_DEV_LEN - 1;
        while (cut > 0 && ((unsigned char)full[cut] & 0xC0) == 0x80) cut--;
        memcpy(name, full, cut);
        name[cut] = '~';
        name[cut + 1] = 0;
    }
    else {
        memcpy(name, full, len);
        name[len] = 0;
    }

    if (0 == strncmp(suffix, "button ", 7)) {
        _snprintf(buf, sizeof(buf) - 1, "%s: btn %s", name, suffix + 7);
    }
    else {
        _snprintf(buf, sizeof(buf) - 1, "%s: %s", name, suffix);
    }
    buf[sizeof(buf) - 1] = 0;
    return buf;
}

char* dik2strDisplay(int dikKey)
{
    return dikNameToDisplay(dik2str(dikKey));
}

int str2dik(char* dikString)
{
    int i;
    /* An empty query must not match the first unnamed gap in the table. */
    if (dikString == NULL || dikString[0] == 0) {
        return 0;
    }
    for (i = 0; i < KBD_TABLE_LEN; i++) {
        if (dikStrings[i][0] == 0) continue;
        if (0 == strcmp(dikString, dikStrings[i])) {
            return i;
        }
    }
    return 0;
}


static LPDIRECTINPUT        dinput;
static int                  dinputVersion;
static LPDIRECTINPUTDEVICE  kbdDevice = NULL;
static LPDIRECTINPUTDEVICE2 kbdDevice2 = NULL;
static HWND                 dinputWindow;
static int                  kbdModifiers;
struct JoyInfo {
    LPDIRECTINPUTDEVICE  diDevice;
    LPDIRECTINPUTDEVICE2 diDevice2;
    GUID                 diGuidInstance;  /* dedupe key for DInput hot-plug */
    int                  numButtons;
    int                  buttonA;
    int                  buttonB;
    int                  isXInput;        /* 1 if XInput device, 0 if DInput */
    int                  xInputSlot;      /* XInput player index 0-3 */
    int                  xInputConnected; /* gate: polling empty XInput slots is expensive */
};
static struct JoyInfo joyInfo[INPUT_MAX_JOYSTICKS];
/* Written by the emulator thread's poll and read by the UI timer, so each
** slot is published in a single store and never blanked in passing. */
static volatile DWORD buttonStatePerJoy[INPUT_MAX_JOYSTICKS];

static int joyCount;

/* Hot-plug dirty flag.  Set by WM_DEVICECHANGE; cleared after a successful
** refresh inside inputRefreshDevicesIfDirty().  The Shortcut Config dialog
** consults the flag before populating its controller dropdown. */
static int inputDevicesDirty = 0;



#define STRUCTSIZE(x) ((dinputVersion == 0x0300) ? sizeof(x##_DX3) : sizeof(x))

static int foundInputDevices = 0;
static int tryBackground = 1;
static int useBackgroundInput = 0;

/* Detect if a DInput device is also an XInput device by matching VID/PID
   against HID device paths that contain "IG_" (XInput marker). */
static int isXInputDevice(const GUID* pGuidProduct)
{
    RAWINPUTDEVICELIST* pRIDL = NULL;
    UINT nDevices = 0;
    UINT i;
    int result = 0;
    char vidpid[32];

    sprintf(vidpid, "VID_%04X&PID_%04X",
            (unsigned)LOWORD(pGuidProduct->Data1),
            (unsigned)HIWORD(pGuidProduct->Data1));
    CharUpperA(vidpid);

    GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST));
    if (nDevices == 0) return 0;
    pRIDL = malloc(sizeof(RAWINPUTDEVICELIST) * nDevices);
    if (!pRIDL) return 0;
    GetRawInputDeviceList(pRIDL, &nDevices, sizeof(RAWINPUTDEVICELIST));

    for (i = 0; i < nDevices && !result; i++) {
        UINT cbSize = 0;
        char* pName;
        if (pRIDL[i].dwType != RIM_TYPEHID) continue;
        GetRawInputDeviceInfoA(pRIDL[i].hDevice, RIDI_DEVICENAME, NULL, &cbSize);
        if (cbSize == 0) continue;
        pName = malloc(cbSize + 1);
        if (!pName) continue;
        pName[cbSize] = 0;
        GetRawInputDeviceInfoA(pRIDL[i].hDevice, RIDI_DEVICENAME, pName, &cbSize);
        CharUpperA(pName);
        if (strstr(pName, "IG_") && strstr(pName, vidpid)) result = 1;
        free(pName);
    }

    free(pRIDL);
    return result;
}

static BOOL CALLBACK enumKeyboards(LPCDIDEVICEINSTANCE devInst, LPVOID ref)
{
    DIDEVCAPS kbdCaps;
    HRESULT rv;

    if (kbdDevice != NULL) {
        return DIENUM_CONTINUE;
    }

    rv = IDirectInput8_CreateDevice(dinput, &devInst->guidInstance, &kbdDevice, NULL);
    if (rv != DI_OK) {
        return DIENUM_CONTINUE;
    }

    IDirectInputDevice_QueryInterface(kbdDevice, &IID_IDirectInputDevice2, (void **)&kbdDevice2);

    kbdCaps.dwSize = STRUCTSIZE(DIDEVCAPS);
    rv = IDirectInputDevice_GetCapabilities(kbdDevice, &kbdCaps);
    if (rv == DI_OK) {
        rv = IDirectInputDevice_SetDataFormat(kbdDevice, &c_dfDIKeyboard);
        if (rv == DI_OK) {
            rv = IDirectInputDevice_SetCooperativeLevel(kbdDevice, dinputWindow,
                (tryBackground ? DISCL_BACKGROUND : DISCL_FOREGROUND) | DISCL_NONEXCLUSIVE);
            if (rv != DI_OK) {
                if (kbdDevice2 != NULL) {
                    IDirectInputDevice_Release(kbdDevice2);
                }
                IDirectInputDevice_Release(kbdDevice);
                kbdDevice = NULL;
                kbdDevice2 = NULL;
            }
        }
    }

    foundInputDevices = 1;

    return DIENUM_CONTINUE;
}

static BOOL CALLBACK enumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, void* pContext)
{
    DIPROPRANGE diprg;
    HRESULT rv;

    diprg.diph.dwSize       = sizeof(DIPROPRANGE);
    diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    diprg.diph.dwHow        = DIPH_BYOFFSET;
    diprg.diph.dwObj        = pdidoi->dwOfs;
    diprg.lMin              = -100;
    diprg.lMax              = +100;

    rv = IDirectInputDevice_SetProperty((LPDIRECTINPUTDEVICE)pContext, DIPROP_RANGE, &diprg.diph);

    if (rv != DI_OK) {
        return DIENUM_STOP;
    }

    return DIENUM_CONTINUE;
}

/* Names reach the saved profile, so every producer must clip
** identically. */
#define JOY_NAME_MAX 128

static void joyCopyDeviceName(char* dst, const char* src)
{
    _snprintf(dst, JOY_NAME_MAX - 1, "%s", src);
    dst[JOY_NAME_MAX - 1] = 0;
}

static void joyNameForDir(char* dst, const char* name, const char* dir)
{
    sprintf(dst, "%s : %s", name, dir);
}

/* A leftover name from the slot's previous occupant would resolve to a
** button that cannot fire. */
static void joyClearUnusedButtonNames(int slot, int numButtons)
{
    int i;
    for (i = numButtons; i < JOY_MAX_BUTTONS; i++) {
        dikStrings[KEY_CODE_BUTTON1 + i + 32 * slot][0] = 0;
    }
}

/* Control names for one slot; this text is the on-disk profile format. */
static void joyFillNames(int slot, const char* name, int numButtons)
{
    int i;
    joyNameForDir(dikStrings[KEY_CODE_JOYLEFT  + 32 * slot], name, "left");
    joyNameForDir(dikStrings[KEY_CODE_JOYRIGHT + 32 * slot], name, "right");
    joyNameForDir(dikStrings[KEY_CODE_JOYUP    + 32 * slot], name, "up");
    joyNameForDir(dikStrings[KEY_CODE_JOYDOWN  + 32 * slot], name, "down");
    for (i = 0; i < numButtons; i++) {
        sprintf(dikStrings[KEY_CODE_BUTTON1 + i + 32 * slot], "%s : button %d", name, i + 1);
    }
    joyClearUnusedButtonNames(slot, numButtons);
}

static int inputRegisterDInputDevice(const DIDEVICEINSTANCE* pdidInstance, int slot)
{
    DIDEVCAPS diDevCaps;
    LPDIRECTINPUTDEVICE  dev;
    LPDIRECTINPUTDEVICE2 dev2 = NULL;
    HRESULT rv;
    char devName[JOY_NAME_MAX];

    rv = IDirectInput_CreateDevice(dinput, &pdidInstance->guidInstance, &dev, NULL);
    if (rv != DI_OK) {
        return 0;
    }

    IDirectInputDevice_QueryInterface(dev, &IID_IDirectInputDevice2, (void **)&dev2);

    rv = IDirectInputDevice_SetDataFormat(dev, &c_dfDIJoystick);
    if (rv == DI_OK) {
        rv = IDirectInputDevice_SetCooperativeLevel(dev, dinputWindow,
                DISCL_NONEXCLUSIVE | (tryBackground ? DISCL_BACKGROUND : DISCL_FOREGROUND));
    }
    if (rv == DI_OK) {
        rv = IDirectInputDevice_EnumObjects(dev, enumAxesCallback, (void*)dev, DIDFT_AXIS);
    }
    if (rv == DI_OK) {
        diDevCaps.dwSize = sizeof(diDevCaps);
        rv = IDirectInputDevice_GetCapabilities(dev, &diDevCaps);
    }
    if (rv != DI_OK) {
        if (dev2 != NULL) {
            IDirectInputDevice_Release(dev2);
        }
        IDirectInputDevice_Release(dev);
        return 0;
    }

    /* Over 28 buttons and the names and state bits run into the next
    ** slot. */
    joyInfo[slot].numButtons      = diDevCaps.dwButtons > JOY_MAX_BUTTONS
                                        ? JOY_MAX_BUTTONS
                                        : (int)diDevCaps.dwButtons;
    joyInfo[slot].isXInput        = 0;
    joyInfo[slot].xInputSlot      = 0;
    joyInfo[slot].xInputConnected = 0;
    joyInfo[slot].diGuidInstance  = pdidInstance->guidInstance;

    joyCopyDeviceName(devName, pdidInstance->tszInstanceName);

    joyFillNames(slot, devName, joyInfo[slot].numButtons);

    /* Publish the device pointers last: the lock-free poll loop gates on
    ** diDevice, so the slot must be fully set up before it goes live. */
    joyInfo[slot].diDevice2 = dev2;
    joyInfo[slot].diDevice  = dev;

    return 1;
}

static BOOL CALLBACK enumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, void* pContext)
{
    /* XInput devices are handled separately via XInput API. Skip them here
       to avoid double-registering the same physical controller. */
    if (isXInputDevice(&pdidInstance->guidProduct)) {
        return DIENUM_CONTINUE;
    }

    if (!inputRegisterDInputDevice(pdidInstance, joyCount)) {
        return DIENUM_CONTINUE;
    }

    foundInputDevices = 1;

    if (++joyCount == INPUT_MAX_JOYSTICKS) {
        return DIENUM_STOP;
    }

    return DIENUM_CONTINUE;
}

/* Departed pads keep their slot and revive on re-acquire, so known GUIDs
** are skipped. */
static BOOL CALLBACK enumJoysticksHotPlugCallback(const DIDEVICEINSTANCE* pdidInstance, void* pContext)
{
    int slot;

    if (isXInputDevice(&pdidInstance->guidProduct)) {
        return DIENUM_CONTINUE;
    }

    for (slot = 0; slot < joyCount; slot++) {
        if (joyInfo[slot].isXInput) continue;
        if (IsEqualGUID(&joyInfo[slot].diGuidInstance, &pdidInstance->guidInstance)) {
            return DIENUM_CONTINUE;
        }
    }

    if (joyCount >= INPUT_MAX_JOYSTICKS) {
        return DIENUM_STOP;
    }
    if (inputRegisterDInputDevice(pdidInstance, joyCount)) {
        joyCount++;
    }
    return DIENUM_CONTINUE;
}

static void inputFillXInputSlot(int slot, int xi)
{
    static const char* xinputBtnNames[12] = {
        "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS",
        "LT", "RT"
    };
    XINPUT_STATE xs;
    int j;

    memset(&joyInfo[slot], 0, sizeof(joyInfo[0]));
    joyInfo[slot].isXInput   = 1;
    joyInfo[slot].xInputSlot = xi;
    joyInfo[slot].numButtons = 12;
    joyInfo[slot].buttonA    = 0;  /* A button */
    joyInfo[slot].buttonB    = 1;  /* B button */
    joyInfo[slot].xInputConnected = XInputGetState(xi, &xs) == ERROR_SUCCESS;

    sprintf(dikStrings[KEY_CODE_JOYLEFT  + 32 * slot], "XInput %d : left",  xi + 1);
    sprintf(dikStrings[KEY_CODE_JOYRIGHT + 32 * slot], "XInput %d : right", xi + 1);
    sprintf(dikStrings[KEY_CODE_JOYUP    + 32 * slot], "XInput %d : up",    xi + 1);
    sprintf(dikStrings[KEY_CODE_JOYDOWN  + 32 * slot], "XInput %d : down",  xi + 1);
    for (j = 0; j < 12; j++) {
        sprintf(dikStrings[KEY_CODE_BUTTON1 + j + 32 * slot],
                "XInput %d : %s", xi + 1, xinputBtnNames[j]);
    }
    joyClearUnusedButtonNames(slot, 12);
}

int inputReset(HWND hwnd)
{
    DIPROPDWORD dipdw = { { sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, 256 };
    HRESULT rv   = 234;
    int i;

    if (useBackgroundInput) {
        return 1;
    }

    joyCount     = 0;
    kbdModifiers = 0;

    foundInputDevices = 0;
    tryBackground = 1;

    for (i = 0; i < 2; i++) {
        dinputWindow = hwnd;
        dinputVersion = DIRECTINPUT_VERSION;
	    rv = DirectInput8Create(GetModuleHandle(NULL), dinputVersion, &IID_IDirectInput8, &dinput, NULL);
        if (rv != DI_OK) {
            /* break, not return: the XInput players below still need
            ** registering. */
            dinput = NULL;
            printf("Failed to initialize DirectInput\n");
            break;
        }

	    rv = IDirectInput_EnumDevices(dinput, DI8DEVTYPE_KEYBOARD, enumKeyboards, 0, DIEDFL_ATTACHEDONLY);
        if (rv != DI_OK) {
            /* Clear too: the device-change refresh gates on this pointer. */
            IDirectInput_Release(dinput);
            dinput = NULL;
            printf("Failed to find DirectInput device\n");
            break;
        }

        if (kbdDevice == NULL) {
            printf("Failed to create DirectInput device\n");
            break;
        }

	    rv = IDirectInputDevice_SetProperty(kbdDevice, DIPROP_BUFFERSIZE,&dipdw.diph);

        /* DI8DEVCLASS_GAMECTRL covers joysticks, gamepads, wheels and flight
        ** sticks alike; DI8DEVTYPE_JOYSTICK would drop devices classified
        ** as gamepad by their HID descriptor. */
        rv = IDirectInput_EnumDevices(dinput, DI8DEVCLASS_GAMECTRL, enumJoysticksCallback, 0, DIEDFL_ATTACHEDONLY);

        if (foundInputDevices) {
            // We found input devices that supports background input so lets not
            // do anymore tests in the future
            useBackgroundInput = 1;
            break;
        }
        tryBackground = 0;
    }

    /* All four slots, connected or not, so hot-plug never renumbers or
    ** renames a control. */
    {
        int xi;
        for (xi = 0; xi < 4 && joyCount < INPUT_MAX_JOYSTICKS; xi++) {
            inputFillXInputSlot(joyCount, xi);
            if (joyInfo[joyCount].xInputConnected) {
                foundInputDevices = 1;
            }
            joyCount++;
        }
    }

    /* A run that only reached the XInput players may retry DirectInput
    ** next call. */
    return useBackgroundInput;
}

void inputMarkDirty(void)
{
    inputDevicesDirty = 1;
}

/* Moves the backup with it, so an arrival is not read as an edit. */
static void keyboardAdoptArrivedDevices(void)
{
    int w = 0, k;
    for (k = 0; k < bindingsPendingCount; k++) {
        int table = bindingsPending[k].table;
        int ec    = bindingsPending[k].ec;
        int dik   = inputResolveDikName(bindingsPending[k].name);
        if (dik > 0 &&
            0 == memcmp(bindingsDikForEc[table][ec], bindingsBackup[table][ec],
                        sizeof(bindingsBackup[table][ec])) &&
            bindingsAddEdge(table, dik, ec)) {
            memcpy(bindingsBackup[table][ec], bindingsDikForEc[table][ec],
                   sizeof(bindingsBackup[table][ec]));
            bindingsForgetPendingBackup(table, ec, bindingsPending[k].name);
            continue;
        }
        bindingsPending[w++] = bindingsPending[k];
    }
    bindingsPendingCount = w;
}

void inputRefreshDevicesIfDirty(void)
{
    if (!inputDevicesDirty) return;
    inputDevicesDirty = 0;

    {
        XINPUT_STATE xs;
        int i;
        for (i = 0; i < joyCount; i++) {
            if (!joyInfo[i].isXInput) continue;
            joyInfo[i].xInputConnected =
                XInputGetState(joyInfo[i].xInputSlot, &xs) == ERROR_SUCCESS;
        }
    }

    if (dinput != NULL) {
        IDirectInput_EnumDevices(dinput, DI8DEVCLASS_GAMECTRL,
                                 enumJoysticksHotPlugCallback, 0, DIEDFL_ATTACHEDONLY);
    }

    /* Last: pending names only resolve once the slots and control names
    ** exist. */
    keyboardAdoptArrivedDevices();
    if (shortcutDeviceArrival) shortcutDeviceArrival();
}

/* A comma inside quotes does not split, so device names may contain one. */
const char* inputNextToken(const char* p, char* out, int outLen)
{
    int n = 0, quoted = 0;
    out[0] = 0;
    while (*p == ' ' || *p == ',') p++;
    while (*p) {
        if (*p == '"') {
            quoted = 1;
            p++;
            while (*p && *p != '"') {
                if (n < outLen - 1) out[n++] = *p;
                p++;
            }
            if (*p == '"') p++;
            continue;
        }
        if (*p == ',') break;
        if (n < outLen - 1) out[n++] = *p;
        p++;
    }
    /* Legacy values pad with spaces; a quoted name keeps them. */
    if (!quoted) {
        while (n > 0 && out[n - 1] == ' ') n--;
    }
    out[n] = 0;
    if (*p == ',') p++;
    return p;
}

/* Plain DIK names stay unquoted so files from earlier builds keep their
** spelling. */
void inputAppendToken(char* list, int listLen, const char* token)
{
    int quote = strchr(token, ',') != NULL || strchr(token, '"') != NULL ||
                token[0] == ' ';
    if (list[0]) strncat(list, ",", listLen - strlen(list) - 1);
    if (quote) strncat(list, "\"", listLen - strlen(list) - 1);
    strncat(list, token, listLen - strlen(list) - 1);
    if (quote) strncat(list, "\"", listLen - strlen(list) - 1);
}

/* J<n> BT <n> and J<n> UP named a slot rather than a device, so str2dik
** can never match one.  Live joystick names all carry " : ". */
static int bindingsIsLegacySlotName(const char* token)
{
    int slot, button, len = 0;
    const char* tail;

    if (sscanf(token, "J%d BT %d%n", &slot, &button, &len) == 2 &&
        len > 0 && token[len] == 0) {
        return 1;
    }
    len = 0;
    if (sscanf(token, "J%d %n", &slot, &len) != 1 || len == 0) {
        return 0;
    }
    tail = token + len;
    return 0 == strcmp(tail, "UP")   || 0 == strcmp(tail, "DOWN") ||
           0 == strcmp(tail, "LEFT") || 0 == strcmp(tail, "RIGHT");
}

/* Emits DIKs in insertion order, so a reload keeps the oldest to newest
** order the editor showed. */
static void bindingsFormatEc(int table, int ec, char* out, int outLen)
{
    int diks[KBD_MAX_PER_EC];
    int count, b;

    out[0] = 0;
    count = bindingsGetDiksForEc(table, ec, diks);
    for (b = 0; b < count; b++) {
        const char* one = dik2str(diks[b]);
        if (!one || !one[0]) continue;
        inputAppendToken(out, outLen, one);
    }
}

/* Names no attached device answered to.  The editor, the count and the
** save all read this, so the screen shows what a save would write. */
static int bindingsGhostsForEc(int table, int ec, const char* out[KBD_MAX_PER_EC])
{
    int room = KBD_MAX_PER_EC - bindingsCountDiksForEc(table, ec);
    int k, j, n = 0;

    for (k = 0; k < bindingsPendingCount && n < room; k++) {
        const char* name = bindingsPending[k].name;
        int dik;
        if (bindingsPending[k].table != table) continue;
        if (bindingsPending[k].ec != ec) continue;
        /* The device arrived and the editor bound it, so the name is
        ** already in the list. */
        dik = inputResolveDikName(name);
        if (dik > 0 && bindingsHasEdge(table, dik, ec)) continue;
        /* A hand-edited entry can name one control twice, and one control
        ** must not hold two slots. */
        for (j = 0; j < n; j++) if (0 == strcmp(out[j], name)) break;
        if (j < n) continue;
        out[n++] = name;
    }
    return n;
}

/* The name resolves to the DIK just unbound, so keeping it would put the
** binding straight back. */
static void bindingsForgetPendingDik(int table, int ec, int dik)
{
    int w = 0, k;
    for (k = 0; k < bindingsPendingCount; k++) {
        if (bindingsPending[k].table == table && bindingsPending[k].ec == ec &&
            inputResolveDikName(bindingsPending[k].name) == dik) {
            continue;
        }
        bindingsPending[w++] = bindingsPending[k];
    }
    bindingsPendingCount = w;
}

static int bindingsCountGhostsForEc(int table, int ec)
{
    const char* names[KBD_MAX_PER_EC];
    return bindingsGhostsForEc(table, ec, names);
}

static void bindingsAppendPending(int table, int ec, char* out, int outLen)
{
    const char* names[KBD_MAX_PER_EC];
    int count = bindingsGhostsForEc(table, ec, names);
    int k;

    for (k = 0; k < count; k++) {
        /* inputAppendToken truncates rather than refusing, and half a name
        ** would never find its device again. */
        if ((int)(strlen(out) + strlen(names[k])) + 4 >= outLen) break;
        inputAppendToken(out, outLen, names[k]);
    }
}

/* Answers 0 for a control no attached device has, since str2dik answers
** from stale names and the binding would fire for whoever claims the slot. */
int inputResolveDikName(const char* token)
{
    int dik = str2dik((char*)token);
    if (dik >= KEY_CODE_BUTTON1) {
        int slot   = (dik - KEY_CODE_BUTTON1) / 32;
        int offset = (dik - KEY_CODE_BUTTON1) % 32;
        if (slot >= joyCount) {
            return 0;
        }
        if (offset < JOY_MAX_BUTTONS && offset >= joyInfo[slot].numButtons) {
            return 0;
        }
    }
    return dik;
}

void inputDestroy(void)
{
    if (kbdDevice) {
        IDirectInputDevice_Release(kbdDevice);
        kbdDevice = NULL;
    }
    
    if (kbdDevice2) {
        IDirectInputDevice_Release(kbdDevice2);
        kbdDevice2 = NULL;
    }

    while (--joyCount >= 0) {
        if (joyInfo[joyCount].diDevice) {
    	    IDirectInputDevice_Unacquire(joyInfo[joyCount].diDevice);
            IDirectInputDevice_Release(joyInfo[joyCount].diDevice);
        }
        joyInfo[joyCount].diDevice = NULL;
        if (joyInfo[joyCount].diDevice2) {
            IDirectInputDevice_Release(joyInfo[joyCount].diDevice2);
            joyInfo[joyCount].diDevice2 = NULL;
        }
    }

    if (dinput) {
        IDirectInput_Release(dinput);
    }

    dinput = NULL;
}

static int joystickUpdateState(int index,  DWORD* buttonMask, int* stateKnown) {
    DIJOYSTATE js;
    HRESULT rv;
    int state = 0;
    DWORD bMask = 0;
    int i;
    Properties* pProperties = propGetGlobalProperties();

    *buttonMask = 0;
    *stateKnown = 1;
    if (index >= joyCount) {
        return 0;
    }

    if (joyInfo[index].isXInput) {
        static const WORD xinputBtns[10] = {
            XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
            XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
            XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
            XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB
        };
        XINPUT_STATE xs;
        XINPUT_GAMEPAD* gp;

        if (!joyInfo[index].xInputConnected) {
            return 0;
        }
        if (XInputGetState(joyInfo[index].xInputSlot, &xs) != ERROR_SUCCESS) {
            /* Unplugged: close the gate until WM_DEVICECHANGE re-probes. */
            joyInfo[index].xInputConnected = 0;
            return 0;
        }
        gp = &xs.Gamepad;

        /* D-pad */
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_UP)    state |= 0x01;
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  state |= 0x02;
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  state |= 0x04;
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) state |= 0x08;

        /* Left stick */
        if (gp->sThumbLY >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) state |= 0x01;
        if (gp->sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) state |= 0x02;
        if (gp->sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) state |= 0x04;
        if (gp->sThumbLX >  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) state |= 0x08;

        for (i = 0; i < 10; i++) {
            if (gp->wButtons & xinputBtns[i]) bMask |= (1 << i);
        }
        /* Analog triggers have no button bit; surface them as 11 and 12. */
        if (gp->bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) bMask |= (1 << 10);
        if (gp->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) bMask |= (1 << 11);

        if (bMask & (1 << joyInfo[index].buttonA)) state |= 0x10;
        if (bMask & (1 << joyInfo[index].buttonB)) state |= 0x20;

        *buttonMask = bMask;
        return state;
    }

    if (joyInfo[index].diDevice == NULL) {
        return 0;
    }

    if (joyInfo[index].diDevice2) {
        IDirectInputDevice2_Poll(joyInfo[index].diDevice2);
    }

    rv = IDirectInputDevice_GetDeviceState(joyInfo[index].diDevice, sizeof(DIJOYSTATE), &js);
    if (rv == DIERR_INPUTLOST || rv == DIERR_NOTACQUIRED) {
        /* Re-acquired means this tick knows nothing, not nothing-held; the
        ** next one reads it.  A failed re-acquire means the device is gone,
        ** and "nothing held" is what releases whatever was down. */
        rv = IDirectInputDevice_Acquire(joyInfo[index].diDevice);
        // dink: after acquire, return 0 instead of proceeding further
        /* SUCCEEDED, not DI_OK: a re-acquire by the other thread answers
        ** S_FALSE. */
        if (SUCCEEDED(rv)) {
            *stateKnown = 0;
        }
        return 0;
    }

    if (rv != DI_OK) {
        return 0;
    }
    
    if (!pProperties->joystick.disablePOV0Dpad) {
        state|=(((js.rgdwPOV[0]<=31500)&(js.rgdwPOV[0]>=22500))<<2);
        state|=(((js.rgdwPOV[0]<=13500)&(js.rgdwPOV[0]>=4500))<<3);
        state|=((js.rgdwPOV[0]<=4500)|((js.rgdwPOV[0]>=31500)&(js.rgdwPOV[0]<36000)));
        state|=(((js.rgdwPOV[0]<=22500)&(js.rgdwPOV[0]>=13500))<<1);
    }
    if (js.lX < -50) state |= 0x04;
    if (js.lX >  50) state |= 0x08;
    if (js.lY < -50) state |= 0x01;
    if (js.lY >  50) state |= 0x02;
    if (js.rgbButtons[joyInfo[index].buttonA]) state |= 0x10;
    if (js.rgbButtons[joyInfo[index].buttonB]) state |= 0x20;

    for (i = 0; i < joyInfo[index].numButtons; i++) {
        if (js.rgbButtons[i]) bMask |= 1 << i;
    }

    *buttonMask = bMask;

    return state;
}

int joystickNumButtons(int index) {
    return joyInfo[index].numButtons;
}

DWORD joystickGetButtonStatePerJoy(int index) {
    if (index < 0 || index >= INPUT_MAX_JOYSTICKS) return 0;
    return buttonStatePerJoy[index];
}

void joystickSetButtons(int index, int buttonA, int buttonB)
{
    joyInfo[index].buttonA = buttonA;
    joyInfo[index].buttonB = buttonB;
}

static void keyboardHanldeKeypress(int code, int pressed)
{
    int n;

    for (n = 0; n < KBD_TABLE_NUM; n++) {
        int wasPressed = keyStatus[n][code];
        int isEditing  = selectedTable == n && editEnabled && selectedKey != 0;
        keyStatus[n][code] = pressed;

        /* Cheap short-circuit first: most DIKs are unchanged each tick. */
        if (pressed == wasPressed) continue;
        if (!isEditing && !bindingsHasAnyEcForDik(n, code)) continue;

        if (pressed && isEditing) {
            /* DirectInput reports no release for Hankaku/Zenkaku: it reads
            ** pressed from its first press onwards, so a binding to it
            ** would hold its MSX key down for good. */
            if (code == DIK_KANJI) {
                continue;
            }
            /* Press again to unbind.  Release first, or the EC becomes
            ** unreachable while held. */
            if (bindingsHasEdge(n, code, selectedKey)) {
                bindingsEmitDikEvents(n, code, 0);
                bindingsRemoveEdge(n, code, selectedKey);
                bindingsForgetPendingDik(n, selectedKey, code);
                if (selectedDikKey == code) selectedDikKey = 0;
                continue;
            }
            /* Names no device answers to hold a slot too, or a save would
            ** have to drop one of them. */
            if (bindingsCountDiksForEc(n, selectedKey) +
                bindingsCountGhostsForEc(n, selectedKey) >= KBD_MAX_PER_EC) {
                continue;
            }
            /* n-to-n: adding here never unbinds the DIK from another MSX
            ** key. */
            if (bindingsAddEdge(n, code, selectedKey)) {
                selectedDikKey = code;
            }
            bindingsEmitDikEvents(n, code, 0);
        }
        else {
            bindingsEmitDikEvents(n, code, pressed);
        }
    }
}

static DWORD buttonState = 0;
static int hasFocus = 0;

void keyboardSetFocus(int handle, int focus) 
{
    if (focus) {
        hasFocus |= handle;
    }
    else {
        hasFocus &= ~handle;
    }
}

static void keyboardResetKbd()
{
    int n, i;
    kbdModifiers = 0;
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        for (i = 0; i < KBD_TABLE_LEN; i++) {
            if (keyStatus[n][i]) keyboardHanldeKeypress(i, 0);
        }
    }
    inputEventReset();
    buttonState = 0;
}

DWORD joystickGetButtonState()
{
    return buttonState;
}

void joystickUpdate()
{
    int i, j;
    DWORD joyMask = 0;

    buttonState = 0;

    for (i = 0; i < joyCount; i++) {
        DWORD mask;
        int stateKnown = 1;
        int state = joystickUpdateState(i, &mask, &stateKnown);
        /* Skip entirely, so a re-acquire is not read as every button being
        ** let go. */
        if (!stateKnown) {
            continue;
        }
        joyMask |= state;
        buttonState |= mask;
        /* Directions ride in bits 28-31, matching KEY_CODE_JOYUP..JOYRIGHT's
        ** offsets. */
        if (i < INPUT_MAX_JOYSTICKS) {
            buttonStatePerJoy[i] = mask | ((DWORD)(state & 0x0F) << 28);
        }

        for (j = 0; j < JOY_MAX_BUTTONS; j++) {
            keyboardHanldeKeypress(KEY_CODE_BUTTON1 + j + i * 32, mask & 1);
            mask >>= 1;
        }

        for (j = 0; j < 4; j++) {
            keyboardHanldeKeypress(KEY_CODE_JOYUP + j + i * 32, state & 1);
            state >>= 1;
        }
    }

    /* Clamped: inputDestroy's teardown loop leaves joyCount at -1. */
    for (i = joyCount > 0 ? joyCount : 0; i < INPUT_MAX_JOYSTICKS; i++) {
        buttonStatePerJoy[i] = 0;
    }
}

void keyboardEnable(int enable)
{
    int n, i;
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        for (i = 0; i < KBD_TABLE_LEN; i++) {
            if (keyStatus[n][i]) keyboardHanldeKeypress(i, 0);
        }
    }

    if (kbdDevice != NULL) {
        IDirectInputDevice_Unacquire(kbdDevice);
        if (enable) {
            IDirectInputDevice_Acquire(kbdDevice);
        }
    }
}

int keyboardGetModifiers()
{
    return kbdModifiers;
}

void keyboardUpdate() 
{ 
    if (!hasFocus) {
        keyboardResetKbd();
        return;
    }

    if (kbdDevice != NULL) {
        char buffer[256]; 
        HRESULT  rv; 
        
        if (kbdDevice2) {
			IDirectInputDevice2_Poll(kbdDevice2);
        }

        rv = IDirectInputDevice_GetDeviceState(kbdDevice, sizeof(buffer), (LPVOID)&buffer); 
        if (rv == DIERR_INPUTLOST || rv == DIERR_NOTACQUIRED) {
            rv = IDirectInputDevice_Acquire(kbdDevice);
            if (rv == DI_OK) {
                rv = IDirectInputDevice_GetDeviceState(kbdDevice, sizeof(buffer), (LPVOID)&buffer); 
            }
        }

        if (rv >= 0) { 
            kbdModifiers = ((buffer[DIK_LSHIFT]   & 0x80) >> 7) | ((buffer[DIK_RSHIFT]   & 0x80) >> 6) | 
                           ((buffer[DIK_LCONTROL] & 0x80) >> 5) | ((buffer[DIK_RCONTROL] & 0x80) >> 4) | 
                           ((buffer[DIK_LALT]     & 0x80) >> 3) | ((buffer[DIK_RALT]     & 0x80) >> 2) | 
                           ((buffer[DIK_LWIN]     & 0x80) >> 1) | ((buffer[DIK_RWIN]     & 0x80) >> 0);

            if (kbdModifiers &&
                  ((buffer[DIK_F6]  | buffer[DIK_F7]  | buffer[DIK_F8]  | buffer[DIK_F9]  | 
                    buffer[DIK_F10] | buffer[DIK_F11] | buffer[DIK_F12]) >> 7))
            {
                int i;
                for (i = 0; i < 256; i++) {
                    keyboardHanldeKeypress(i, 0);
                }
            }
            else {
                int i;
                for (i = 0; i < 256; i++) {
                    keyboardHanldeKeypress(i, buffer[i] >> 7);
                }
            }

            return;
        }
    }

    kbdModifiers = (GetAsyncKeyState(VK_LMENU)   > 1UL ? KBD_LALT     : 0) |
                   (GetAsyncKeyState(VK_MENU)    > 1UL ? KBD_LALT     : 0) |
                   (GetAsyncKeyState(VK_SHIFT)   > 1UL ? KBD_LSHIFT   : 0) |
                   (GetAsyncKeyState(VK_CONTROL) > 1UL ? KBD_LCTRL    : 0) |
                   (GetAsyncKeyState(VK_LWIN)    > 1UL ? KBD_LWIN     : 0) |
                   (GetAsyncKeyState(VK_RWIN)    > 1UL ? KBD_RWIN     : 0);
} 

/* Adds the names of the mappings in one directory, skipping any already there. */
static int keyboardAppendConfigs(const char* directory, char names[][KBD_CONFIGNAME_LEN], int index)
{
    char fileName[KBD_CONFIGPATH_LEN];
	HANDLE       handle;
	WIN32_FIND_DATAA wfd;
    BOOL cont = TRUE;

    if (directory[0] == 0) {
        return index;
    }

    sprintf(fileName, "%s/*.config", directory);

    handle = FindFirstFileU(fileName, &wfd);

    if (handle == INVALID_HANDLE_VALUE) {
        return index;
    }

    while (cont) {
        DWORD fa = wfd.dwFileAttributes;
        int length = (int)strlen(wfd.cFileName) - 7;
        /* Use wfd.dwFileAttributes (not GetFileAttributes on basename); same
        ** fix as Win32ShortcutsConfig.c::getProfileList. */
        if (!(fa & FILE_ATTRIBUTE_DIRECTORY) && length > 0 &&
            length < KBD_CONFIGNAME_LEN && index < 255) {
            int i;
            int seen = 0;
            for (i = 0; i < index; i++) {
                /* Names are matched the way the file system does, or
                ** blueMSX.config and bluemsx.config are both offered and both
                ** open the second. */
                if (_strnicmp(names[i], wfd.cFileName, length) == 0 &&
                    names[i][length] == 0) {
                    seen = 1;
                }
            }
            if (!seen) {
                memcpy(names[index], wfd.cFileName, length);
                names[index][length] = 0;
                index++;
            }
        }
        cont = FindNextFileU(handle, &wfd);
    }

	FindClose(handle);
    
    return index;
}

char** keyboardGetConfigs()
{
    static char* keyboardNames[256];
    static char  keyboardArray[256][KBD_CONFIGNAME_LEN];
    int index = 0;
    int i;

    index = keyboardAppendConfigs(keyboardConfigDir, keyboardArray, index);
    index = keyboardAppendConfigs(keyboardSharedDir, keyboardArray, index);

    for (i = 0; i < index; i++) {
        keyboardNames[i] = keyboardArray[i];
    }
    keyboardNames[index] = NULL;

    return keyboardNames;
}

/* 0 when the join does not fit, leaving the name empty: a shortened path
** would open some other file, so it must name none. */
static int keyboardConfigJoin(char* fileName, const char* dir, const char* name)
{
    if (strlen(dir) + strlen(name) + 9 > KBD_CONFIGPATH_LEN) {
        fileName[0] = 0;
        return 0;
    }
    sprintf(fileName, "%s/%s.config", dir, name);
    return 1;
}

/* ' */
static void keyboardConfigPath(char fileName[KBD_CONFIGPATH_LEN], const char* configName)
{
    FILE* file;

    keyboardConfigJoin(fileName, keyboardConfigDir, configName);

    if (keyboardSharedDir[0] == 0) {
        return;
    }

    file = fileName[0] != 0 ? fopen(fileName, "r") : NULL;
    if (file != NULL) {
        fclose(file);
        return;
    }

    keyboardConfigJoin(fileName, keyboardSharedDir, configName);
}

/* Either directory counts, though saving only ever goes to the writable
** one. */
static int keyboardConfigExists(const char* name)
{
    char path[KBD_CONFIGPATH_LEN];
    FILE* f;
    if (strlen(name) >= KBD_CONFIGNAME_LEN) return 0;
    keyboardConfigPath(path, name);
    f = fopen(path, "r");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

static int currentConfigIsSubstitute = 0;

/* The region's own pair first, so changing the Windows region moves
** nobody to an unrelated profile. */
static char* keyboardFallbackConfigName(const char* requested)
{
    char* generated[2];
    char** configs;
    int i;

    if (inputKeyboardRegionIsJapanese()) {
        generated[0] = JapaneseConfigName;
        generated[1] = DefaultConfigName;
    }
    else {
        generated[0] = DefaultConfigName;
        generated[1] = JapaneseConfigName;
    }
    for (i = 0; i < 2; i++) {
        if (0 == strcmp(generated[i], requested)) continue;
        if (keyboardConfigExists(generated[i])) return generated[i];
    }
    configs = keyboardGetConfigs();
    if (configs[0] != NULL && 0 != strcmp(configs[0], requested)) {
        return configs[0];
    }
    return NULL;
}

int keyboardLoadConfig(char* configName)
{
	IniFile *keyConfigFile;
    char fileName[KBD_CONFIGPATH_LEN];
    FILE* file;
    int i;
    int n;
    /* Which entries the file leaves to the built-ins; filled in a second
    ** pass so they can be tested against the whole file. */
    char wantsDefault[KBD_TABLE_NUM][EC_KEYCOUNT];

    memset(wantsDefault, 0, sizeof(wantsDefault));

    if (configName[0] == 0) {
        configName = inputKeyboardRegionIsJapanese() ? JapaneseConfigName
                                                     : DefaultConfigName;
    }
    /* Refused before anything is reset.  Clearing the name makes save a
    ** no-op, so the editor's OK cannot overwrite an unopened profile. */
    if (strlen(configName) >= KBD_CONFIGNAME_LEN) {
        currentConfigFile[0] = 0;
        currentConfigIsSubstitute = 1;
        return 0;
    }

    /* Closing the editor reloads the profile already in use, so only a
    ** request for a different name clears the stand-in. */
    if (0 != strcmp(configName, currentConfigFile)) {
        currentConfigIsSubstitute = 0;
    }

    memset(bindingsRewriteOnSave, 0, sizeof(bindingsRewriteOnSave));
    /* Dropped with the bindings, or the new profile adopts the old one's
    ** device. */
    bindingsPendingCount = 0;

    keyboardResetKbd();
    bindingsResetAll();
    keyboardConfigPath(fileName, configName);

    file = fopen(fileName, "r");
    if (file == NULL) {
        char* fallback = keyboardFallbackConfigName(configName);
        if (fallback != NULL) {
            configName = fallback;
            currentConfigIsSubstitute = 1;
            keyboardConfigPath(fileName, configName);
            file = fopen(fileName, "r");
        }
    }
    if (file == NULL) {
        /* Claim the name anyway, or the editor's save would do nothing. */
        bindingsLoadDefaults();
        sprintf(currentConfigFile, configName);
        bindingsBackupAll();
        return 0;
    }
    fclose(file);

    sprintf(currentConfigFile, configName);

    keyConfigFile = iniFileOpen(fileName);

    for (n = 0; n < KBD_TABLE_NUM; n++) {
        char profString[32];
        sprintf(profString, "Keymapping-%d", n);
        for (i = 0; i < EC_KEYCOUNT; i++) {
            const char* keyCode = inputEventCodeToString(i);
            if (keyCode != NULL && *keyCode != 0) {
                char dikNames[512];
                char key[32] = { 0 };
                char* p;
                int sawLegacy = 0;
                /* Older files put ECs in the wrong section; the editor
                ** could not clear those. */
                if (n != bindingsTableForEc(i)) {
                    continue;
                }
                strcat(key, keyCode);
                strcat(key, " ");
                iniFileGetString(keyConfigFile, profString, key, "",
                                 dikNames, sizeof(dikNames));
                /* Flagged for rewrite, or a later DIK clash would silence
                ** the key. */
                if (dikNames[0] == 0) {
                    wantsDefault[n][i] = 1;
                    bindingsRewriteOnSave[n][i] = 1;
                    continue;
                }
                /* Before the token loop, or the marker resolves as a
                ** device name. */
                if (0 == strcmp(dikNames, kUnassignedName)) {
                    continue;
                }
                p = dikNames;
                while (*p == ' ' || *p == ',') p++;
                while (*p) {
                    char token[192];
                    int dikKey;
                    p = (char*)inputNextToken(p, token, sizeof(token));
                    if (token[0] == 0) {
                        if (*p == 0) break;
                        continue;
                    }
                    dikKey = inputResolveDikName(token);
                    if (dikKey <= 0) {
                        if (bindingsIsLegacySlotName(token)) {
                            sawLegacy = 1;
                            continue;
                        }
                        if (bindingsPendingCount <
                                (int)(sizeof(bindingsPending) / sizeof(bindingsPending[0])) &&
                            strlen(token) < sizeof(bindingsPending[0].name)) {
                            bindingsPending[bindingsPendingCount].table = (short)n;
                            bindingsPending[bindingsPendingCount].ec    = (short)i;
                            strcpy(bindingsPending[bindingsPendingCount].name, token);
                            bindingsPendingCount++;
                        }
                    }
                    else {
                        bindingsAddEdge(n, dikKey, i);
                    }
                }
                /* Slot names were all the entry held, so it takes the
                ** built-in default the way an empty value does. */
                if (sawLegacy && bindingsCountDiksForEc(n, i) == 0 &&
                    !bindingsHasPending(n, i)) {
                    wantsDefault[n][i] = 1;
                    bindingsRewriteOnSave[n][i] = 1;
                }
            }
        }
    }
    /* Built-in fills come last so a default can see the whole file: an
    ** unversioned profile blanks a key whose DIK moved elsewhere, and
    ** restoring it would make one key drive two MSX targets. */
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        for (i = 0; i < EC_KEYCOUNT; i++) {
            if (wantsDefault[n][i]) {
                bindingsLoadDefaultsForEc(n, i);
            }
        }
    }
    iniFileClose(keyConfigFile);

    /* Re-baseline, or the editor diffs the new profile against the old. */
    bindingsBackupAll();

    return 1;
}

void keyboardSaveConfig(char* configName)
{
	IniFile *keyConfigFile;
    char fileName[KBD_CONFIGPATH_LEN];
    int i, n;

    /* The path is joined here rather than through keyboardConfigPath: a
    ** save only ever goes to the writable directory, so the bound on the
    ** name has to hold again. */
    if (configName[0] == 0 || strlen(configName) >= KBD_CONFIGNAME_LEN) {
        return;
    }

    if (!keyboardConfigJoin(fileName, keyboardConfigDir, configName)) {
        return;
    }

    keyConfigFile = iniFileOpen(fileName);
    for (n = 0; n < KBD_TABLE_NUM; n++) {
        char profString[32];
        sprintf(profString, "Keymapping-%d", n);
        for (i = 0; i < EC_KEYCOUNT; i++) {
            const char* keyCode = inputEventCodeToString(i);
            /* IniFileParser's readLine has no bound and writes into a
            ** 512-byte caller buffer, so "key =<value>" must fit. */
            char dikNames[448];
            char key[32] = { 0 };
            /* The reader skips the same rows, so a value in a section its
            ** table does not own could never be read back. */
            if (keyCode == NULL || *keyCode == 0 ||
                n != bindingsTableForEc(i)) {
                continue;
            }
            bindingsFormatEc(n, i, dikNames, sizeof(dikNames));
            bindingsAppendPending(n, i, dikNames, sizeof(dikNames));
            /* Counted, not tested on the text, which is empty for a nameless DIK too. */
            if (bindingsCountDiksForEc(n, i) == 0 &&
                bindingsDefaultExists(n, i) &&
                !bindingsHasPending(n, i) &&
                (bindingsBackup[n][i][0] != 0 || !bindingsRewriteOnSave[n][i])) {
                strcpy(dikNames, kUnassignedName);
                bindingsRewriteOnSave[n][i] = 0;
            }
            strcat(key, keyCode);
            strcat(key, " ");
            iniFileWriteString(keyConfigFile, profString, key, dikNames);
        }
    }
    /* Only mark memory clean once the write landed, or a later Cancel
    ** would revert to a state that does not match disk. */
    if (iniFileClose(keyConfigFile)) {
        bindingsBackupAll();
        sprintf(currentConfigFile, configName);
        memset(bindingsRewriteOnSave, 0, sizeof(bindingsRewriteOnSave));
    }
}

void keyboardSetDirectory(char* directory)
{
    strcpy(keyboardConfigDir, directory);
}

void keyboardSetSharedDirectory(char* directory)
{
    strcpy(keyboardSharedDir, directory);
}

char* keyboardGetCurrentConfig()
{
    return currentConfigFile;
}

int keyboardConfigIsSubstitute()
{
    return currentConfigIsSubstitute;
}

void inputInit()
{
    char fileName[KBD_CONFIGPATH_LEN];
    char startName[KBD_CONFIGNAME_LEN];
    int jp = inputKeyboardRegionIsJapanese();
    FILE* file;

    initDikStr();
    bindingsLoadDefaults();

    inputEventReset();

    strcpy(startName, jp ? JapaneseConfigName : DefaultConfigName);
    keyboardConfigPath(fileName, startName);
    file = fopen(fileName, "r");
    if (file != NULL) {
        fclose(file);
        strcpy(currentConfigFile, startName);
        return;
    }

    /* Claiming a name here would let OK save defaults over an unopened profile. */
    if (keyboardGetConfigs()[0] != NULL) {
        return;
    }

    /* Both layouts, so a user whose keyboard the region guessed wrong can
    ** switch.  Every device's defaults go in, so switching a port later
    ** needs no further action. */
    bindingsLoadDefaultsForLayout(!jp);
    keyboardSaveConfig(jp ? DefaultConfigName : JapaneseConfigName);

    /* This run's profile goes last, so it is the one a further save updates. */
    bindingsLoadDefaults();
    keyboardSaveConfig(startName);
}

char* archGetSelectedKey()
{
    if (selectedKey != 0) {
        char* keyCode = (char*)inputEventCodeToString(selectedKey);
        if (keyCode != NULL) {
            return keyCode;
        }
    }
    return "";
}

char* archGetMappedKey()
{
    /* The theme's native text member is this long, and a name the display
    ** form cannot shorten reaches here whole. */
    static char buf[128];
    int diks[KBD_MAX_PER_EC];
    const char* names[KBD_MAX_PER_EC];
    int count, b;
    buf[0] = 0;
    if (selectedKey == 0) return "";
    /* The theme draws this string as it stands, so a conflicting DIK is
    ** flagged with a leading '!' rather than with a colour. */
    count = bindingsGetDiksForEc(selectedTable, selectedKey, diks);
    for (b = 0; b < count; b++) {
        const char* one = dik2strDisplay(diks[b]);
        if (!one || !one[0]) continue;
        if (buf[0]) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
        if (inputDikConflicts(diks[b])) {
            strncat(buf, "!", sizeof(buf) - strlen(buf) - 1);
        }
        strncat(buf, one, sizeof(buf) - strlen(buf) - 1);
    }
    /* Unmarked: no attached device answers to these, so nothing the
    ** emulator polls can clash with them. */
    count = bindingsGhostsForEc(selectedTable, selectedKey, names);
    for (b = 0; b < count; b++) {
        const char* one = dikNameToDisplay((char*)names[b]);
        if (!one[0]) continue;
        if (buf[0]) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, one, sizeof(buf) - strlen(buf) - 1);
    }
    if (buf[0] == 0) {
        return langKeyboardMappedHint();
    }
    return buf;
}

int archKeyboardSelectedBindingCount(void)
{
    if (selectedKey == 0) return 0;
    return bindingsCountDiksForEc(selectedTable, selectedKey) +
           bindingsCountGhostsForEc(selectedTable, selectedKey);
}

/* keyStatus is not wiped: a held DIK would read as a fresh press and re-bind. */
void archKeyboardClearSelectedKey(void)
{
    if (selectedKey == 0) return;
    bindingsClearEc(selectedTable, selectedKey);
    bindingsForgetPending(selectedTable, selectedKey);
    inputEventReset();
    selectedDikKey = 0;
}

static int bindingsTableForEc(int ec)
{
    if (inputEventIsJoystick1(ec)) return 1;
    if (inputEventIsJoystick2(ec)) return 2;
    return 0;
}

void archKeyboardSetSelectedKey(int msxKeyCode) {
    int diks[KBD_MAX_PER_EC];
    selectedKey = msxKeyCode;
    selectedDikKey = 0;
    selectedTable = bindingsTableForEc(msxKeyCode);

    if (msxKeyCode > 0 && msxKeyCode < EC_KEYCOUNT) {
        bindingsGetDiksForEc(selectedTable, msxKeyCode, diks);
        selectedDikKey = diks[0];
    }
}

void archPollInput() {
    keyboardUpdate();
    joystickUpdate();
}

int archKeyboardIsKeySelected(int msxKeyCode)
{
    return editEnabled && (msxKeyCode == selectedKey);
}

/* Unbound and conflicting both return 0: the theme has one warning colour. */
int archKeyboardIsKeyConfigured(int msxKeyCode)
{
    int n, b;
    int diks[KBD_MAX_PER_EC];

    if (!editEnabled) {
        return 1;
    }

    for (n = 0; n < KBD_TABLE_NUM; n++) {
        int count = bindingsGetDiksForEc(n, msxKeyCode, diks);
        if (count == 0) continue;
        for (b = 0; b < count; b++) {
            if (inputDikConflicts(diks[b])) return 0;
        }
        return 1;
    }
    return 0;
}

char* archKeyboardConflictTextForKey(int msxKeyCode)
{
    static char buf[512];
    int diks[KBD_MAX_PER_EC];
    int count, b;
    int table;

    buf[0] = 0;
    if (msxKeyCode <= 0 || msxKeyCode >= EC_KEYCOUNT) return buf;
    table = bindingsTableForEc(msxKeyCode);

    count = bindingsGetDiksForEc(table, msxKeyCode, diks);
    for (b = 0; b < count; b++) {
        char targets[192];
        int msx = bindingsDescribeTargetsForDik(diks[b], table, msxKeyCode,
                                                targets, sizeof(targets));
        int sc  = shortcutDikCounter ? shortcutDikCounter(diks[b]) : 0;
        if (msx == 0 && sc == 0) continue;
        if (dik2strDisplay(diks[b])[0] == 0) continue;
        if (buf[0] == 0) {
            strncat(buf, langShortcutTooltipAlsoBound(), sizeof(buf) - strlen(buf) - 1);
        }
        strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, dik2strDisplay(diks[b]), sizeof(buf) - strlen(buf) - 1);
        strncat(buf, " > ", sizeof(buf) - strlen(buf) - 1);
        if (msx) {
            strncat(buf, targets, sizeof(buf) - strlen(buf) - 1);
        }
        if (sc) {
            char names[192];
            names[0] = 0;
            if (shortcutDikDescriber) {
                shortcutDikDescriber(diks[b], names, sizeof(names));
            }
            if (msx) {
                strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
            }
            if (names[0]) {
                strncat(buf, names, sizeof(buf) - strlen(buf) - 1);
            }
            else {
                char one[24];
                sprintf(one, "shortcut x%d", sc);
                strncat(buf, one, sizeof(buf) - strlen(buf) - 1);
            }
        }
    }
    return buf;
}

char* archKeyboardConflictText(void)
{
    return archKeyboardConflictTextForKey(selectedKey);
}

void keybardEnableEdit(int enable)
{
    editEnabled = enable;
    selectedKey = 0;
    selectedDikKey = 0;
}

void keyboardStartConfig()
{
    /* Pick up hot-plugged controllers before the user assigns keys. */
    inputRefreshDevicesIfDirty();
    bindingsBackupAll();
}

void keyboardCancelConfig()
{
    bindingsRestoreAll();
}

int keyboardConfigIsModified()
{
    return bindingsDiffersFromBackup();
}



char* archKeyconfigSelectedKeyTitle() {
    return langKeyconfigSelectedKey();
}

char* archKeyconfigMappedToTitle() {
    return langKeyconfigMappedTo();
}

char* archKeyconfigMappingSchemeTitle() {
    return langKeyconfigMappingScheme();
}

