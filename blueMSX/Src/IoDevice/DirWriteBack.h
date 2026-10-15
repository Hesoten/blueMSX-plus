/*****************************************************************************
**
** Carries the changes the MSX made to a directory volume over to the host directory.
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
#ifndef DIR_WRITE_BACK_H
#define DIR_WRITE_BACK_H

#include "DirDisk.h"

typedef struct {
    int created;    /* files the MSX made */
    int changed;    /* files the MSX wrote to */
    int deleted;    /* files the MSX removed; they move to the trash directory */
    int skipped;    /* changes left out because the host file is no longer as it was */
    int mass;       /* so much was deleted that the volume was probably formatted */
} DirChanges;

typedef enum {
    DIR_WB_OK,
    DIR_WB_NOTHING,        /* the MSX changed nothing that needs carrying over */
    DIR_WB_INVALID,        /* the volume is not a consistent file system as it stands */
    DIR_WB_MEMORY,
    DIR_WB_HOST,           /* the host directory could not be read or written; it is as it was */
    DIR_WB_UNSUPPORTED
} DirWbStatus;

typedef struct DirPlan DirPlan;

/* Works out what changed. Nothing on the host is touched. */
DirWbStatus dirWriteBackPlan(DirDisk* disk, DirPlan** plan, DirChanges* changes);

/* Carries the plan out. failed counts the items left undone, which log.txt in
** the directory named by session lists; session is empty when nothing was kept.
** The volume must not be used by the MSX afterwards. */
DirWbStatus dirWriteBackApply(DirDisk* disk, DirPlan* plan, int* failed, char* session, int sessionSize);

void dirWriteBackFree(DirPlan* plan);

#endif
