/*****************************************************************************
**
** geo3d 3D coprocessor: C interface to Geo3DCore.
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
#include "Geo3D.h"
#include "Geo3DCore.h"
#include <cstdlib>
#include <cstring>

struct Geo3D {
    Geo3DCore core;
};

extern "C" Geo3D* geo3dCreate()
{
    return new Geo3D;
}

extern "C" void geo3dDestroy(Geo3D* geo3d)
{
    delete geo3d;
}

extern "C" void geo3dReset(Geo3D* geo3d)
{
    geo3d->core.reset();
}

extern "C" void geo3dWriteIndex(Geo3D* geo3d, UInt8 value)
{
    geo3d->core.writeIndex(value);
}

extern "C" void geo3dWriteData(Geo3D* geo3d, UInt8 value)
{
    geo3d->core.writeData(value);
}

extern "C" UInt8 geo3dReadStatus(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd)
{
    return geo3d->core.readStatus(ec, edgeEnd);
}

extern "C" UInt8 geo3dReadData(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd)
{
    return geo3d->core.readData(ec, edgeEnd);
}

extern "C" UInt8 geo3dPeekData(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd)
{
    return geo3d->core.peekData(ec, edgeEnd);
}

extern "C" int geo3dIsRunning(Geo3D* geo3d)
{
    return geo3d->core.isRunning() ? 1 : 0;
}

extern "C" int geo3dFacesOn(Geo3D* geo3d)
{
    return geo3d->core.facesOn() ? 1 : 0;
}

extern "C" int geo3dNextCommand(Geo3D* geo3d, UInt8* regs, UInt8* values)
{
    if (!geo3d->core.hasCommand()) {
        return 0;
    }
    const Geo3DCore::Command& c = geo3d->core.nextCommand();
    for (unsigned k = 0; k < c.size; k++) {
        regs[k]   = (UInt8)c.regNum(k);
        values[k] = c.bytes[k];
    }
    return c.size;
}

extern "C" void geo3dFinishRun(Geo3D* geo3d)
{
    geo3d->core.finishRun();
}

extern "C" int geo3dPeekCommand(Geo3D* geo3d, UInt32* ready)
{
    if (!geo3d->core.hasCommand()) {
        return 0;
    }
    const Geo3DCore::Command& c = geo3d->core.peekCommand();
    *ready = c.ready;
    return c.size;
}

extern "C" UInt32 geo3dEndClock(Geo3D* geo3d)
{
    return geo3d->core.endClock();
}

extern "C" int geo3dCommandsLeft(Geo3D* geo3d)
{
    return (int)geo3d->core.commandsLeft();
}

/* No exception may cross into the C callers: a state that cannot be made is
** saved empty, and one that cannot be read leaves the core reset. */
extern "C" UInt8* geo3dSaveState(Geo3D* geo3d, UInt32* size)
{
    UInt8* data = NULL;

    *size = 0;
    try {
        std::vector<uint8_t> state = geo3d->core.saveState();
        data = (UInt8*)malloc(state.size());
        if (data != NULL) {
            memcpy(data, state.data(), state.size());
            *size = (UInt32)state.size();
        }
    }
    catch (...) {
    }
    return data;
}

extern "C" void geo3dLoadState(Geo3D* geo3d, const UInt8* data, UInt32 size, int layout)
{
    geo3d->core.reset();
    try {
        geo3d->core.loadState(std::vector<uint8_t>(data, data + size), layout);
    }
    catch (...) {
        geo3d->core.reset();
    }
}

static_assert(Geo3DCore::ENTRIES == GEO3D_DEBUG_ENTRIES, "model RAM size");
static_assert(Geo3DCore::DEBUG_RUNS >= GEO3D_DEBUG_RUNS, "RUN ring");
static_assert(Geo3DCore::ITEM_DRAWN == GEO3D_ITEM_DRAWN && Geo3DCore::ITEM_NEAR == GEO3D_ITEM_NEAR &&
              Geo3DCore::ITEM_BACK == GEO3D_ITEM_BACK && Geo3DCore::ITEM_OFFSCREEN == GEO3D_ITEM_OFFSCREEN &&
              Geo3DCore::ITEM_UNCUT == GEO3D_ITEM_UNCUT, "item outcomes");

static Geo3DDebugProj debugProj(const Geo3DCore::Proj& p)
{
    Geo3DDebugProj d;
    d.sx    = (Int16)p.sx;
    d.sy    = (Int16)p.sy;
    d.z     = (Int16)p.z;
    d.flags = (UInt8)p.flags;
    d.pad   = 0;
    return d;
}

static UInt16 clamp16(UInt32 v)
{
    return (UInt16)(v > 0xffff ? 0xffff : v);
}

extern "C" UInt32 geo3dDebugLastSeq(Geo3D* geo3d)
{
    return geo3d->core.debugLastSeq();
}

extern "C" int geo3dDebugHasRun(Geo3D* geo3d, UInt32 seq)
{
    return geo3d->core.debugRun(seq) != nullptr;
}

extern "C" int geo3dDebugRunRange(Geo3D* geo3d, UInt32 seq, UInt32* items, UInt32* verts,
                                  UInt32* xfEndClock)
{
    const Geo3DCore::DebugRun* r = geo3d->core.debugRun(seq);

    if (r == nullptr) {
        return 0;
    }
    *items      = r->first | ((UInt32)r->count << 16);
    *verts      = r->vfirst | ((UInt32)r->nvert << 16);
    *xfEndClock = r->xfEndClk;
    return 1;
}

extern "C" int geo3dDebugRunYpage(Geo3D* geo3d, UInt32 seq)
{
    const Geo3DCore::DebugRun* r = geo3d->core.debugRun(seq);

    return r != nullptr ? r->ypage : -1;
}

static_assert(GEO3D_DEBUG_CORE_ITEMS == Geo3DCore::DEBUG_ITEMS && GEO3D_DEBUG_CORE_CMDS == Geo3DCore::DEBUG_CMDS,
              "the core's rings");

/* Copies what is left of one RUN's items and commands into the room after
** *itemAt in item[itemRoom] and *cmdAt in cmd[cmdRoom]. */
static void debugCopyRun(const Geo3DCore& c, const Geo3DCore::DebugRun& r, Geo3DDebugRun* o,
                         Geo3DDebugItem* item, int itemRoom, int* itemAt,
                         Geo3DDebugCmd* cmd, int cmdRoom, int* cmdAt)
{
    o->seq        = r.seq;
    o->faces      = r.faces;
    o->tex        = r.tex;
    o->vfirst     = r.vfirst;
    o->nvert      = r.nvert;
    o->first      = r.first;
    o->count      = r.count;
    o->ypage      = r.ypage;
    o->color      = r.color;
    o->lop        = r.lop;
    o->skipped    = r.cntSkip;
    o->drawn      = r.cntDraw;
    o->culled     = r.cntCull;
    o->texx       = r.texx;
    o->texy       = r.texy;
    o->tstride    = r.tstride;
    for (int j = 0; j < 18; j++) {
        o->cfg[j] = r.cfg[j];
    }
    for (int j = 0; j < 3; j++) {
        o->lm[j] = r.lm[j];
    }
    o->xfEndClock = r.xfEndClk;
    o->endClock   = r.endClk;
    o->itemTotal  = r.itemCount;
    o->cmdTotal   = r.cmdCount;

    /* The commands still in the ring and that fit, as one span [lo, hi) of
    ** the RUN; like the ring, a cut keeps the last ones. */
    UInt32 kept = c.debugCommandsPushed() - r.cmdFirst;
    UInt32 hi = r.cmdCount;
    UInt32 lo = kept > Geo3DCore::DEBUG_CMDS ? kept - Geo3DCore::DEBUG_CMDS : 0;
    UInt32 room = (UInt32)(cmdRoom - *cmdAt);
    lo = lo < hi ? lo : hi;
    lo = hi - lo > room ? hi - room : lo;
    o->cmdFirst = (UInt16)*cmdAt;
    for (UInt32 k = lo; k < hi; k++) {
        const Geo3DCore::Command* from = c.debugCommand(r.cmdFirst + k);
        Geo3DDebugCmd& d = cmd[(*cmdAt)++];
        memset(&d, 0, sizeof(d));
        if (from != nullptr) {
            d.size = from->size;
            memcpy(d.bytes, from->bytes.data(), sizeof(d.bytes));
        }
    }
    o->cmdCount = (UInt16)(hi - lo);

    o->itemFirst = (UInt16)*itemAt;
    for (UInt32 k = 0; k < r.itemCount && *itemAt < itemRoom; k++) {
        const Geo3DCore::DebugItem* it = c.debugItem(r.itemFirst + k);
        if (it == nullptr) {
            continue;
        }
        Geo3DDebugItem& d = item[(*itemAt)++];
        d.index    = it->index;
        d.outcome  = it->outcome;
        d.textured = it->textured;
        d.col      = it->col;
        d.lvl      = it->lvl;
        d.order    = it->order;
        for (int j = 0; j < 4; j++) {
            d.v[j]  = it->v[j];
            d.sx[j] = it->sx[j];
            d.sy[j] = it->sy[j];
            d.cx[j] = it->cx[j];
            d.cz[j] = it->cz[j];
        }
        d.ymin     = it->ymin;
        d.ymax     = it->ymax;
        /* the part of its commands inside [lo, hi) */
        UInt32 a = it->cmdFirst - r.cmdFirst;
        UInt32 b = a + it->cmdCount;
        UInt32 from = a > lo ? a : lo;
        UInt32 to = b < hi ? b : hi;
        d.cmdFirst = (UInt16)(o->cmdFirst + (from < to ? from - lo : 0));
        d.cmdCount = (UInt16)(from < to ? to - from : 0);
        d.cmdTotal = clamp16(it->cmdCount);
    }
    o->itemCount = (UInt16)(*itemAt - o->itemFirst);
}

extern "C" void geo3dDebugCopy(Geo3D* geo3d, UInt32 ec, UInt32 edgeEnd,
                               const UInt32* seqs, int count, int seen, Geo3DDebugState* out)
{
    const Geo3DCore& c = geo3d->core;
    const UInt8* r = out->regs;
    int i;

    for (i = 0; i < (int)sizeof(out->regs); i++) {
        out->regs[i] = c.peekReg((uint8_t)i, ec, edgeEnd);
    }
    auto r10 = [&](int lo, int hi) { return (UInt16)(r[lo] | ((r[hi] & 3) << 8)); };
    out->ext     = r[0x6f] & 1;
    out->entries = GEO3D_DEBUG_ENTRIES;
    for (i = 0; i < 18; i++) {
        out->cfg[i] = (Int32)c.getConfig()[i];
    }
    for (i = 0; i < 3; i++) {
        out->ivtx[i]  = (Int32)c.getVertexIn()[i];
        out->light[i] = (Int32)c.getLight()[i];
    }
    out->imm     = debugProj(c.getImmediate());
    out->vaddr   = r10(0x40, 0x68);
    out->eaddr   = r10(0x41, 0x69);
    out->faddr   = r10(0x58, 0x6c);
    out->taddr   = r10(0x65, 0x6e);
    out->nvert   = r10(0x42, 0x6a);
    out->nedge   = r10(0x43, 0x6b);
    out->nface   = r10(0x59, 0x6d);
    out->vfirst  = r10(0x54, 0x55);
    out->efirst  = r10(0x66, 0x67);
    out->ffirst  = r10(0x56, 0x57);
    out->ypage   = (UInt16)(r[0x46] | ((r[0x47] & 7) << 8));
    out->texx    = (UInt16)c.getTexX();
    out->texy    = (UInt16)c.getTexY();
    out->color   = r[0x44];
    out->lop     = r[0x45];
    out->tstride = c.getTexStride();
    out->widx    = c.getIndex();
    out->rptr    = c.getReadIndex();
    out->status  = r[0x48];
    out->running = c.isRunning();
    out->faces   = c.facesOn();
    out->tex     = c.texOn();
    out->skipped = c.getSkipped();
    out->drawn   = c.getDrawn();
    out->culled  = c.getCulled();
    out->runSeq  = c.debugLastSeq();
    out->cmdsTotal  = (int)c.commandsTotal();
    out->cmdsIssued = (int)c.commandsIssued();

    for (i = 0; i < GEO3D_DEBUG_ENTRIES; i++) {
        const auto& f = c.getFace(i);
        for (int k = 0; k < 3; k++) {
            out->vmem[i][k]    = (Int16)c.getVertex(i)[k];
            out->fmem[i].n[k]  = (Int16)f.n[k];
        }
        out->emem[i][0] = c.getEdge(i)[0];
        out->emem[i][1] = c.getEdge(i)[1];
        for (int k = 0; k < 4; k++) {
            out->fmem[i].i[k] = f.i[k];
        }
        out->fmem[i].base = f.base;
        out->fmem[i].pad  = 0;
        memcpy(out->tmem[i], c.getTexture(i).data(), sizeof(out->tmem[i]));
        out->pmem[i] = debugProj(c.getProjected(i));
    }

    int itemAt = 0;
    int cmdAt = 0;
    count = count < GEO3D_DEBUG_RUNS ? count : GEO3D_DEBUG_RUNS;
    seen = seen > 0 && seen < count ? seen : 0;
    for (int n = 0; n < count; n++) {
        i = (seen + n) % count;
        const Geo3DCore::DebugRun* run = c.debugRun(seqs[i]);
        memset(&out->run[i], 0, sizeof(out->run[i]));
        if (run != nullptr) {
            debugCopyRun(c, *run, &out->run[i], out->item, GEO3D_DEBUG_ITEMS, &itemAt,
                         out->cmd, GEO3D_DEBUG_CMDS, &cmdAt);
        }
    }
    out->runCount  = count < 0 ? 0 : count;
    out->itemCount = itemAt;
    out->cmdCount  = cmdAt;
}

extern "C" int geo3dDebugCopyRun(Geo3D* geo3d, UInt32 seq, Geo3DDebugRun* run,
                                 Geo3DDebugItem* items, int itemRoom, Geo3DDebugCmd* cmds, int cmdRoom)
{
    const Geo3DCore::DebugRun* r = geo3d->core.debugRun(seq);
    int itemAt = 0;
    int cmdAt = 0;

    memset(run, 0, sizeof(*run));
    if (r == nullptr) {
        return 0;
    }
    debugCopyRun(geo3d->core, *r, run, items, itemRoom, &itemAt, cmds, cmdRoom, &cmdAt);
    return 1;
}

extern "C" Geo3DDebugProj geo3dDebugProject(const Int32* cfg, const Int16* v)
{
    std::array<int64_t, 18> c;
    std::array<int64_t, 3> x = { v[0], v[1], v[2] };
    for (int i = 0; i < 18; i++) {
        c[i] = cfg[i];
    }
    return debugProj(Geo3DCore::project(c, x));
}
