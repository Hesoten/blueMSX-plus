/*****************************************************************************
**
** Control socket work handoff to the window thread.
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
#ifndef CONTROL_WINDOW_THREAD_H
#define CONTROL_WINDOW_THREAD_H

/* What became of work handed to the window's thread. */
#define CTRL_WINDOW_THREAD_DONE       0
#define CTRL_WINDOW_THREAD_DIALOG     1  /* a dialog or menu is open; the work was not run       */
#define CTRL_WINDOW_THREAD_NOT_TAKEN  2  /* the window did not take it in time; it will not run  */
#define CTRL_WINDOW_THREAD_UNFINISHED 3  /* it started and has not ended, as when it shows a box */
#define CTRL_WINDOW_THREAD_BUSY       4  /* an earlier one has not ended; this one was not run   */

/* Why the window's thread refused work (CTRL_WINDOW_THREAD_DIALOG), as an agent is told it. */
#define CTRL_WINDOW_THREAD_DIALOG_TEXT \
    "the emulator window is busy (a dialog or menu is open, or the window is being moved or " \
    "resized), so nothing was done; ask the person to finish that, then call again."

/* Serving thread only: runs work(arg) on the window's thread, where the menu's own
** actions run, and waits up to timeoutMs. The wait also ends when control is cut off
** or the client goes. One at a time: while one is unfinished, the next is refused. */
int  ctrlWindowThreadRun(void (*work)(int arg), int arg, int timeoutMs);
/* The window's side: call on its thread for the message the above posts. ready is
** 0 inside a dialog or menu loop, as running the work then would nest it in that loop. */
void ctrlWindowThreadRunPosted(int ready);

#endif
