/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/IoDevice/MsxPPI.h,v $
**
** $Revision: 1.5 $
**
** $Date: 2008-03-30 18:38:40 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
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
#ifndef MSX_PPI_H
#define MSX_PPI_H

#include "MsxTypes.h"
#include "InputEvent.h"

/* The keyboard matrix: one row per line, most significant bit first, EC_NONE
** where no key is wired. Row 6 bit 0 answers to both shift keys, and the right
** shift key, having no bit of its own, is added where the rows are read. */
#define MSX_KEY_MATRIX(X) \
    X( 0, EC_7,       EC_6,      EC_5,       EC_4,       EC_3,      EC_2,      EC_1,      EC_0      ) \
    X( 1, EC_SEMICOL, EC_LBRACK, EC_AT,      EC_BKSLASH, EC_CIRCFLX,EC_NEG,    EC_9,      EC_8      ) \
    X( 2, EC_B,       EC_A,      EC_UNDSCRE, EC_DIV,     EC_PERIOD, EC_COMMA,  EC_RBRACK, EC_COLON  ) \
    X( 3, EC_J,       EC_I,      EC_H,       EC_G,       EC_F,      EC_E,      EC_D,      EC_C      ) \
    X( 4, EC_R,       EC_Q,      EC_P,       EC_O,       EC_N,      EC_M,      EC_L,      EC_K      ) \
    X( 5, EC_Z,       EC_Y,      EC_X,       EC_W,       EC_V,      EC_U,      EC_T,      EC_S      ) \
    X( 6, EC_F3,      EC_F2,     EC_F1,      EC_CODE,    EC_CAPS,   EC_GRAPH,  EC_CTRL,   EC_LSHIFT ) \
    X( 7, EC_RETURN,  EC_SELECT, EC_BKSPACE, EC_STOP,    EC_TAB,    EC_ESC,    EC_F5,     EC_F4     ) \
    X( 8, EC_RIGHT,   EC_DOWN,   EC_UP,      EC_LEFT,    EC_DEL,    EC_INS,    EC_CLS,    EC_SPACE  ) \
    X( 9, EC_NUM4,    EC_NUM3,   EC_NUM2,    EC_NUM1,    EC_NUM0,   EC_NUMDIV, EC_NUMADD, EC_NUMMUL ) \
    X(10, EC_NUMPER,  EC_NUMCOM, EC_NUMSUB,  EC_NUM9,    EC_NUM8,   EC_NUM7,   EC_NUM6,   EC_NUM5   ) \
    X(11, EC_NONE,    EC_NONE,   EC_NONE,    EC_NONE,    EC_TORIKE, EC_NONE,   EC_JIKKOU, EC_NONE   )

void msxPPICreate(int ignoreKeyboard);

#endif

