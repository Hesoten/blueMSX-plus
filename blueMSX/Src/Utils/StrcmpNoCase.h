/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Utils/StrcmpNoCase.h,v $
**
** $Revision: 1.4 $
**
** $Date: 2008-03-30 18:38:47 $
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
#ifndef STRCMP_NO_CASE_H
#define STRCMP_NO_CASE_H

int strcmpnocase(const char* str1, const char* str2);

#ifdef WIN32
/* POSIX strcasestr is provided as a fallback in Board/Machine.c on
   Windows.  Returns a pointer to the first case-insensitive occurrence
   of str2 inside str1, or NULL.  Declared here so callers don't
   implicit-declare it as int-returning (the x64 compiler would then
   truncate the returned pointer). */
char *strcasestr(const char *str1, const char *str2);
#endif

#endif
