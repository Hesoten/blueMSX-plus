/*****************************************************************************
**
** Control protocol limits and defaults.
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
#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H

/* The folder under %LOCALAPPDATA% where each emulator listening for agents writes its
** instance file, PREFIX<pid>SUFFIX, with its port and token. */
#define CTRL_AGENT_FOLDER        "blueMSXplus\\agent"
#define CTRL_INSTANCE_PREFIX     "instance-"
#define CTRL_INSTANCE_PREFIX_LEN (sizeof(CTRL_INSTANCE_PREFIX) - 1)
#define CTRL_INSTANCE_SUFFIX     ".json"

/* The longest request line, with its NUL; the MCP server builds none longer. */
#define CTRL_LINE_MAX      (64 * 1024)

/* Enough for a full 64kB hex dump, the largest single reply. */
#define CTRL_REPLY_MAX (1024 * 768)

/* Reply texts name the MCP tools to call next (bmsx_run how=pause): those names are part of
** this protocol. */

/* The most bytes one mem.write takes, in any data form however spaced. */
#define CTRL_MEM_WRITE_MAX 511

/* The longest "at", in bytes. */
#define CTRL_AT_MAX 255

/* A path in a request, in UTF-8 bytes: MAX_PATH characters at 3 bytes each, and the NUL. */
#define CTRL_PATH_MAX 1024

/* A number as the text a schema writes; the numbers below are plain decimal for this. */
#define CTRL_STR_(x) #x
#define CTRL_STR(x)  CTRL_STR_(x)

/* Each op's default, smallest and largest value of a number argument. */
#define CTRL_MEM_READ_COUNT_DEFAULT       256
#define CTRL_MEM_READ_COUNT_MIN           1
#define CTRL_MEM_READ_COUNT_MAX           65536
#define CTRL_RUN_COUNT_DEFAULT            1
#define CTRL_RUN_COUNT_MIN                1
#define CTRL_RUN_COUNT_MAX                100000
#define CTRL_RUN_UNTIL_TIMEOUT_MS_DEFAULT 5000
#define CTRL_RUN_UNTIL_TIMEOUT_MS_MIN     1
#define CTRL_RUN_UNTIL_TIMEOUT_MS_MAX     600000
/* The defaults of the text arguments that have one. */
#define CTRL_SCREEN_AS_DEFAULT            "auto"

/* What an argument of each kind must be, after "\"name\" must be ". */
#define CTRL_TEXT_WANT_INT            "a whole number, as in 16 or \"0x10\""
#define CTRL_TEXT_WANT_STRING         "a string in quotes, with valid escapes"
#define CTRL_TEXT_WANT_BOOL           "true or false"
/* Printed with CTRL_LINE_MAX - 1, the most bytes a line holds before its newline. */
#define CTRL_TEXT_LINE_TOO_LONG       "a request line is at most %d bytes; this one was longer, " \
                                      "so none of it was read"

#endif
