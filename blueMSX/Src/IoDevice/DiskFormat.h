/*****************************************************************************
**
** Host-side FAT12 disk-image creation with pre-formatted MSX-DOS boot sector.
** Copyright (C) 2026 Hesoten
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** Boot sector byte arrays and FAT layout logic are adapted from openMSX
** (src/fdc/BootBlocks.cc and DiskImageUtils.cc); those originate from real
** MSX/Nextor hardware dumps.  Redistributed under GPL v2+ per openMSX terms.
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
#ifndef DISK_FORMAT_H
#define DISK_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DiskFormatUnformatted = 0,
    DiskFormatMsxDos1     = 1,
    DiskFormatMsxDos2     = 2,
    DiskFormatNextor      = 3
} DiskFormatType;

/* Create sizeBytes at path; Unformatted zero-fills (SVI 338/168KB use 0xE5),
** MSX-DOS 1/2/Nextor writes a real FAT12 boot sector + FATs + empty root.
** Non-floppy geometries fall back to zero-fill.  Returns 1 on success. */
int diskImageCreate(const char* path, int sizeBytes, DiskFormatType fmt);

#ifdef __cplusplus
}
#endif

#endif /* DISK_FORMAT_H */
