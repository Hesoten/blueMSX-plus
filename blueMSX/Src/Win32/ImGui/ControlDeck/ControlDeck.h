/*****************************************************************************
**
** The Control Deck: run, pause, frame advance, stop, reset and speed in a
** small window that never takes the keyboard from the emulator.
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
#ifndef CONTROL_DECK_H
#define CONTROL_DECK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the deck, or brings it to the front without activating it. */
void controlDeckShow(void);
void controlDeckClose(void);
int  controlDeckIsOpen(void);
/* Lets go of what the deck's buttons hold until they are pressed again: for a modal loop
** that suspends the machine, or a deck hidden with the main window that draws no frame. */
void controlDeckDropHolds(void);
/* Call before the program closes its windows: the deck then opens again at
** the next start, as it was open when the program quit. */
void controlDeckAppExit(void);

#ifdef __cplusplus
}
#endif

#endif
