/*****************************************************************************
**
** File open / save dialogs (UTF-8 paths).
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
/* All path / title / filter strings are UTF-8. */
#ifndef WIN32_FILE_DIALOG_H
#define WIN32_FILE_DIALOG_H

#include "MsxTypes.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OFN-style filter (double-NUL terminated "Desc\0pat\0Desc\0pat\0\0") in UTF-8. */
typedef const char* shell_filter_t;

/* Generic open dialog. outPath receives UTF-8 path on success; returns
** FALSE on cancel/error. filterIndex (in/out, 1-based) may be NULL. */
BOOL ShellOpenFileDialog(HWND owner,
                         const char* title,
                         shell_filter_t filter,
                         const char* initialDir,
                         const char* defExt,
                         int* filterIndex,
                         char* outPath, int outPathCap);

/* Generic save dialog. */
BOOL ShellSaveFileDialog(HWND owner,
                         const char* title,
                         shell_filter_t filter,
                         const char* initialDir,
                         const char* defExt,
                         int* filterIndex,
                         char* outPath, int outPathCap);

/* Save dialog with default filename pre-fill (for capture / record As...). */
BOOL ShellSaveFileDialogEx(HWND owner,
                           const char* title,
                           shell_filter_t filter,
                           const char* initialDir,
                           const char* defExt,
                           const char* defaultName,
                           int* filterIndex,
                           char* outPath, int outPathCap);

/* ROM open dialog with ROM-type combobox: on selection the picker reads
** the file (peeking inside .zip), runs mediaDbLookupRom and pre-selects
** the match.  outRomType gets the final pick (RomType enum), or
** ROM_UNKNOWN while the combobox stayed disabled. */
BOOL ShellOpenRomFileDialog(HWND owner,
                            const char* title,
                            shell_filter_t filter,
                            const char* initialDir,
                            char* outPath, int outPathCap,
                            int* outRomType);

/* HD-image creation dialog with disk-size combobox.
** outHdSizeBytes receives the chosen size in bytes. */
BOOL ShellNewHdFileDialog(HWND owner,
                          const char* title,
                          shell_filter_t filter,
                          const char* initialDir,
                          const char* defExt,
                          char* outPath, int outPathCap,
                          Int64* outHdSizeBytes);

/* Folder picker -- replaces SHBrowseForFolder.  Uses IFileOpenDialog with
** FOS_PICKFOLDERS so the user gets the modern Explorer-style picker
** (DPI-aware, dark-mode-aware, address bar, drag-and-drop). */
BOOL ShellPickFolderDialog(HWND owner,
                           const char* title,
                           const char* initialDir,
                           char* outPath, int outPathCap);

/* FD/DSK creation dialog with a size + format combobox.  Label lists are
** UTF-8, selectedIndex is in/out (0-based, clamped to 0).  Either list may
** be NULL/empty to hide the combobox.  Returns FALSE on cancel. */
typedef struct {
    const char* label;   /* UTF-8 */
    int         bytes;   /* payload — size in bytes, or format enum value */
} ShellComboItem;

BOOL ShellNewDskFileDialog(HWND owner,
                           const char* title,
                           shell_filter_t filter,
                           const char* initialDir,
                           const char* defExt,
                           const ShellComboItem* sizeItems, int sizeItemCount,
                           int* sizeSelectedIndex,
                           const ShellComboItem* fmtItems, int fmtItemCount,
                           int* fmtSelectedIndex,
                           char* outPath, int outPathCap);

/* Folder picker with an added MSX-DOS format combobox (DOS1/DOS2/Nextor). */
BOOL ShellPickFolderWithFormatDialog(HWND owner,
                                     const char* title,
                                     const char* initialDir,
                                     const ShellComboItem* fmtItems,
                                     int fmtItemCount,
                                     int* fmtSelectedIndex,
                                     char* outPath, int outPathCap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WIN32_FILE_DIALOG_H */
