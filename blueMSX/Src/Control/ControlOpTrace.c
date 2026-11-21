/*****************************************************************************
**
** Debug trace op handlers.
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
#include "ControlOp.h"
#include "ControlAddress.h"
#include "ControlProtocol.h"
#include "DebugTrace.h"
#include <string.h>

#define READ_CHUNK 4096
/* A record waits here for its CONT, which can come after records of other sources. */
#define QUEUE_MAX  16
/* The most one line takes, and what the header and footer take. */
#define LINE_ROOM  512
#define TEXT_ROOM  1024

static const struct { const char* name; int classId; } classTable[] = {
    { "geo3d",  DEBUG_TRACE_GEO3D  },
    { "vdpcmd", DEBUG_TRACE_VDPCMD },
    { "vdpreg", DEBUG_TRACE_VDPREG },
    { "cpu",    DEBUG_TRACE_CPU    },
    { "io",     DEBUG_TRACE_IO     },
};

#define CLASS_COUNT ((int)(sizeof(classTable) / sizeof(classTable[0])))
#define IO_BIT      DEBUG_TRACE_BIT(DEBUG_TRACE_IO)

static void addClassNames(CtrlText* text, UInt32 mask);

/* The mask "class" names, CTRL_TRACE_CLASS_DEFAULT when it is left out. */
static int readClasses(CtrlCall* call, UInt32* mask)
{
    const char* list = ctrlCallText(call, "class");
    const char* at;
    int i = 0;

    if (list == NULL) {
        list = CTRL_TRACE_CLASS_DEFAULT;
    }
    *mask = 0;
    for (at = list; i < CLASS_COUNT; at++) {
        const char* end;
        int length;

        while (*at == ' ') {
            at++;
        }
        for (end = at; *end != 0 && *end != ',' && *end != ' '; end++) {
        }
        length = (int)(end - at);
        for (i = 0; i < CLASS_COUNT; i++) {
            if ((int)strlen(classTable[i].name) == length &&
                strncmp(classTable[i].name, at, (size_t)length) == 0)
            {
                *mask |= DEBUG_TRACE_BIT(classTable[i].classId);
                break;
            }
        }
        for (at = end; *at == ' '; at++) {
        }
        if (*at != ',') {
            break;
        }
    }
    if (i == CLASS_COUNT || *at != 0) {
        ctrlTextAdd(&call->text, "\"class\" takes ");
        addClassNames(&call->text, 0xFFFFFFFF);
        ctrlTextAdd(&call->text, ", any of them joined with commas");
        return ctrlReplyTextAsError(call, "bad_arg");
    }
    return 0;
}

static const char* skipSpaces(const char* at)
{
    while (at != NULL && *at == ' ') {
        at++;
    }
    return at;
}

#define DIR_OUT 1
#define DIR_IN  2

/* "dir", out, in or both, as DIR_ bits; OUT when it is left out. */
static int readDir(CtrlCall* call, int* dir)
{
    const char* text = ctrlCallText(call, "dir");

    *dir = text == NULL || strcmp(text, "out") == 0 ? DIR_OUT :
           strcmp(text, "in") == 0                  ? DIR_IN :
           strcmp(text, "both") == 0                ? DIR_OUT | DIR_IN : 0;
    return *dir != 0 ? 0 : ctrlReplyError(call, "bad_arg", "\"dir\" takes out, in or both");
}

/* "ports", such as 0x98,0xA0-0xA2, as bits, in the halves of the directions dir names. */
static int readPorts(CtrlCall* call, UInt8* bits, int dir)
{
    const char* at = ctrlCallText(call, "ports");

    memset(bits, 0, DEBUG_TRACE_PORT_BYTES);
    if (at == NULL) {
        return 0;
    }
    for (;;) {
        UInt32 first;
        UInt32 last;
        UInt32 port;

        at = skipSpaces(ctrlAddrScanNumber(skipSpaces(at), &first));
        last = first;
        if (at != NULL && *at == '-') {
            at = skipSpaces(ctrlAddrScanNumber(skipSpaces(at + 1), &last));
        }
        if (at == NULL || (*at != 0 && *at != ',') || first > last) {
            break;
        }
        if (last > 0xFF) {
            return ctrlReplyError(call, "bad_arg", "\"ports\" are 0x00 to 0xFF: the CPU selects "
                                  "a port by the low address byte only");
        }
        for (port = first; port <= last; port++) {
            if (dir & DIR_OUT) {
                bits[port >> 3] |= (UInt8)(1 << (port & 7));
            }
            if (dir & DIR_IN) {
                bits[(port | DEBUG_TRACE_PORT_IN) >> 3] |= (UInt8)(1 << (port & 7));
            }
        }
        if (*at == 0) {
            return 0;
        }
        at++;
    }
    return ctrlReplyError(call, "bad_arg", "\"ports\" takes I/O ports and rising ranges joined "
                          "with commas, such as 0x98,0xA0-0xA2");
}

/* The ports of one half after sep and the half's direction; nothing when it is empty.
** Returns whether it added any. */
static int addPortNames(CtrlText* text, const UInt8* bits, int half, const char* sep)
{
    int any = 0;
    int port;

    for (port = 0; port < 256; port++) {
        int last = port;

        if (!DEBUG_TRACE_PORT_HELD(bits, half | port)) {
            continue;
        }
        while (last < 255 && DEBUG_TRACE_PORT_HELD(bits, half | (last + 1))) {
            last++;
        }
        if (!any) {
            ctrlTextAdd(text, "%s%s ", sep, half ? "in" : "out");
        }
        ctrlTextAdd(text, last > port ? "%s0x%02X-0x%02X" : "%s0x%02X", any ? "," : "", port, last);
        any  = 1;
        port = last;
    }
    return any;
}

static void addClassNames(CtrlText* text, UInt32 mask)
{
    int any = 0;
    int i;

    for (i = 0; i < CLASS_COUNT; i++) {
        if (mask & DEBUG_TRACE_BIT(classTable[i].classId)) {
            ctrlTextAdd(text, "%s%s", any ? "," : "", classTable[i].name);
            any = 1;
        }
    }
    if (!any) {
        ctrlTextAdd(text, "none");
    }
}

static void describeArms(CtrlText* text, const CtrlSession* session)
{
    ctrlTextAdd(text, "this connection holds: ");
    addClassNames(text, session->traceMask);
    if (session->traceMask & IO_BIT) {
        int out;

        ctrlTextAdd(text, " (io ");
        out = addPortNames(text, session->tracePorts, 0, "");
        addPortNames(text, session->tracePorts, DEBUG_TRACE_PORT_IN, out ? "; " : "");
        ctrlTextAdd(text, ")");
    }
    ctrlTextAdd(text, "\narmed by anyone: ");
    addClassNames(text, debugTraceArmed);
    ctrlTextAdd(text, "\n");
}

int ctrlOpTraceArm(CtrlCall* call)
{
    CtrlSession* session = call->session;
    UInt8  ports[DEBUG_TRACE_PORT_BYTES];
    UInt32 mask;
    UInt32 add;
    UInt32 seq;
    int    dir;
    int    refused;

    if ((refused = readClasses(call, &mask)) != 0) {
        return refused;
    }
    if (!ctrlCallGiven(call, "ports") != !(mask & IO_BIT)) {
        return ctrlReplyError(call, "bad_arg", (mask & IO_BIT) ?
                              "class io needs \"ports\", the I/O ports whose OUTs or INs to "
                              "record, such as 0x98,0xA0-0xA2" : "\"ports\" goes with class io");
    }
    if (ctrlCallGiven(call, "dir") && !(mask & IO_BIT)) {
        return ctrlReplyError(call, "bad_arg", "\"dir\" goes with class io");
    }
    if ((refused = readDir(call, &dir)) != 0 || (refused = readPorts(call, ports, dir)) != 0) {
        return refused;
    }
    /* The ports go in before the class, so its first OUTs and INs are not missed. */
    if (mask & IO_BIT) {
        debugTracePortsHold(session->tracePorts, ports);
    }
    add = mask & ~session->traceMask;
    if (add != 0) {
        seq = debugTraceSeq();
        debugTraceArm(add, 1);
        /* The ring is made by the first arm; without it nothing is armed. */
        if ((debugTraceArmed & add) != add) {
            debugTraceArm(add, 0);
            ctrlSessionTraceDrop(session, 0);
            return ctrlReplyError(call, "no_memory",
                                  "the trace ring could not be allocated, so nothing was armed");
        }
        if (session->traceMask == 0) {
            session->traceArmSeq    = seq;
            session->traceEverArmed = 1;
        }
        session->traceMask |= add;
    }
    describeArms(&call->text, session);
    ctrlTextAdd(&call->text, "read starts after seq %u unless after is given\n",
                (unsigned)session->traceArmSeq);
    if (mask & IO_BIT) {
        int out = DEBUG_TRACE_PORT_HELD(ports, 0x98);
        int in  = DEBUG_TRACE_PORT_HELD(ports, 0x98 | DEBUG_TRACE_PORT_IN);

        if (out || in) {
            ctrlTextAdd(&call->text, "0x98 takes %s for every VRAM byte the CPU %s, which can flood "
                        "the ring (shared with the Geo3D Perf window) and drop records; for how "
                        "much the CPU moves to and from VRAM, the vdpreg class's cpu vram lines "
                        "count it once a frame\n",
                        out && in ? "an OUT or an IN" : out ? "an OUT" : "an IN",
                        out && in ? "writes or reads" : out ? "writes" : "reads");
        }
    }
    if ((mask & IO_BIT) && DEBUG_TRACE_PORT_HELD(ports, 0x99 | DEBUG_TRACE_PORT_IN)) {
        ctrlTextAdd(&call->text, "IN 0x99 reads the VDP status, which a program may poll in a "
                    "tight loop and flood the ring; the vdpcmd class's wait lines already count "
                    "the polling of a command's CE\n");
    }
    return ctrlReplyDone(call);
}

int ctrlOpTraceDisarm(CtrlCall* call)
{
    CtrlSession* session = call->session;
    UInt32 mask = session->traceMask;
    int    refused;

    if (ctrlCallGiven(call, "ports") || ctrlCallGiven(call, "dir")) {
        return ctrlReplyError(call, "bad_arg", "disarm gives back all of io's ports, both ways; "
                              "arm io again with the ones to keep");
    }
    if (ctrlCallGiven(call, "class") && (refused = readClasses(call, &mask)) != 0) {
        return refused;
    }
    mask = ctrlSessionTraceDrop(session, mask);
    ctrlTextAdd(&call->text, "disarmed: ");
    addClassNames(&call->text, mask);
    ctrlTextAdd(&call->text, "\n");
    describeArms(&call->text, session);
    return ctrlReplyDone(call);
}

typedef struct {
    DebugTraceRecord rec;
    DebugTraceRecord cont;
    UInt32 seq;
    int    hasCont;
    int    closed;
} Pending;

typedef struct {
    CtrlText*        text;
    UInt32           mask;
    int              count;
    int              lines;
    Pending          queue[QUEUE_MAX];
    int              queued;
    DebugTraceSource sources[DEBUG_TRACE_SOURCES];
    UInt8            known[DEBUG_TRACE_SOURCES];
    UInt8            ports[DEBUG_TRACE_PORT_BYTES];
} TraceReader;

static const DebugTraceSource* sourceOf(TraceReader* r, UInt16 source)
{
    if (source >= DEBUG_TRACE_SOURCES) {
        return NULL;
    }
    if (r->known[source] == 0) {
        r->known[source] = debugTraceSourceGet(source, &r->sources[source]) ? 1 : 2;
    }
    return r->known[source] == 1 ? &r->sources[source] : NULL;
}

static int shown(TraceReader* r, const DebugTraceRecord* rec)
{
    const DebugTraceSource* src = sourceOf(r, rec->source);

    if (src == NULL ||
        (src->classId != DEBUG_TRACE_CORE && (r->mask & DEBUG_TRACE_BIT(src->classId)) == 0))
    {
        return 0;
    }
    /* The ring holds the ports of other connections too. */
    return src->classId != DEBUG_TRACE_IO ||
           DEBUG_TRACE_PORT_HELD(r->ports, (rec->a & 0xFF) |
                                 ((rec->kind & ~DEBUG_TRACE_PHASE_MASK) == DEBUG_TRACE_IO_IN ?
                                  DEBUG_TRACE_PORT_IN : 0));
}

static void emit(TraceReader* r, const Pending* p)
{
    const DebugTraceSource* src = sourceOf(r, p->rec.source);
    char line[LINE_ROOM - 96];

    debugTraceFormat(src, &p->rec, p->hasCont ? &p->cont : NULL, line, sizeof(line));
    /* The core source is named as its class, which the line already starts with. */
    ctrlTextAdd(r->text, "%u t=%llu %s%s%s\n", (unsigned)p->seq, (unsigned long long)p->rec.time,
                p->rec.source != 0 ? src->name : "", p->rec.source != 0 ? ": " : "", line);
    r->lines++;
}

/* Prints the closed records at the front, in seq order; force closes them all. */
static void drain(TraceReader* r, int force)
{
    int n = 0;

    while (n < r->queued && (force || r->queue[n].closed)) {
        emit(r, &r->queue[n]);
        n++;
    }
    memmove(r->queue, r->queue + n, (size_t)(r->queued - n) * sizeof(Pending));
    r->queued -= n;
}

/* Takes one record; 0 when it would print more than count lines, or more than the text
** holds, so it is left for the next read. */
static int take(TraceReader* r, const DebugTraceRecord* rec, UInt32 seq)
{
    int i;

    if ((rec->kind & DEBUG_TRACE_PHASE_MASK) == DEBUG_TRACE_CONT) {
        for (i = r->queued - 1; i >= 0 && r->queue[i].rec.source != rec->source; i--) {
        }
        /* Joined only to the source's last record while it is open: a later record of the
        ** source, a gap or the queue's end closes it. Any other CONT is left out. */
        if (i >= 0 && !r->queue[i].closed) {
            r->queue[i].cont    = *rec;
            r->queue[i].hasCont = 1;
            r->queue[i].closed  = 1;
            drain(r, 0);
        }
        return 1;
    }
    if (!shown(r, rec)) {
        return 1;
    }
    if (r->lines + r->queued + 1 > r->count ||
        r->text->capacity - r->text->length < (r->queued + 1) * LINE_ROOM + TEXT_ROOM)
    {
        return 0;
    }
    for (i = 0; i < r->queued; i++) {
        if (r->queue[i].rec.source == rec->source) {
            r->queue[i].closed = 1;
        }
    }
    if (r->queued == QUEUE_MAX) {
        r->queue[0].closed = 1;
    }
    drain(r, 0);
    r->queue[r->queued].rec     = *rec;
    r->queue[r->queued].seq     = seq;
    r->queue[r->queued].hasCont = 0;
    r->queue[r->queued].closed  = !debugTraceHasCont(sourceOf(r, rec->source), rec);
    r->queued++;
    drain(r, 0);
    return 1;
}

int ctrlOpTraceRead(CtrlCall* call)
{
    static DebugTraceRecord buf[READ_CHUNK];
    static char lineBuffer[CTRL_REPLY_MAX / 2];
    static TraceReader reader;
    CtrlSession* session = call->session;
    CtrlText lineText;
    Int64  after64 = 0;
    UInt32 after;
    UInt32 head = debugTraceSeq();
    UInt32 cursor;
    UInt32 firstRead = 0;
    UInt32 droppedTotal = 0;
    int    more = 0;
    int    count = ctrlCallInt(call, "count", CTRL_TRACE_COUNT_DEFAULT);
    int    refused;

    /* Left out, the classes this connection holds, so a plain read shows what it armed. */
    if (!ctrlCallGiven(call, "class") && session->traceMask != 0) {
        reader.mask = session->traceMask;
    }
    else if ((refused = readClasses(call, &reader.mask)) != 0) {
        return refused;
    }
    if (ctrlCallGiven(call, "ports") && !(reader.mask & IO_BIT)) {
        return ctrlReplyError(call, "bad_arg", "\"ports\" goes with class io");
    }
    if (ctrlCallGiven(call, "dir")) {
        return ctrlReplyError(call, "bad_arg", "\"dir\" goes with arm");
    }
    if ((refused = readPorts(call, reader.ports, DIR_OUT | DIR_IN)) != 0) {
        return refused;
    }
    if (!ctrlCallGiven(call, "ports")) {
        memcpy(reader.ports, session->tracePorts, sizeof(reader.ports));
        if ((reader.mask & IO_BIT) && (session->traceMask & IO_BIT) == 0) {
            return ctrlReplyError(call, "bad_arg", "class io needs \"ports\" on read while this "
                                  "connection holds no io ports (disarm gives them back)");
        }
    }
    if (count < CTRL_TRACE_COUNT_MIN || count > CTRL_TRACE_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be %d to %d",
                               CTRL_TRACE_COUNT_MIN, CTRL_TRACE_COUNT_MAX);
    }
    if (ctrlCallInt64(call, "after", &after64)) {
        if (after64 < CTRL_TRACE_AFTER_MIN || after64 > 0xFFFFFFFF) {
            return ctrlReplyError(call, "out_of_range",
                                  "after must be a seq, 0 to 0xFFFFFFFF");
        }
        after = (UInt32)after64;
        if ((Int32)(head - after) < 0) {
            return ctrlReplyErrorf(call, "out_of_range", "after %u is past the newest record %u; "
                                   "if blueMSX+ restarted since, arm again and read without after",
                                   (unsigned)after, (unsigned)head);
        }
    }
    else if (session->traceEverArmed) {
        after = session->traceArmSeq;
    }
    else {
        return ctrlReplyError(call, "not_armed",
                              "this connection has armed nothing to read from: arm first "
                              "(op=arm), or give after, the next of an earlier read");
    }

    ctrlTextInit(&lineText, lineBuffer, call->text.capacity < (int)sizeof(lineBuffer) ?
                                        call->text.capacity : (int)sizeof(lineBuffer));
    reader.text   = &lineText;
    reader.count  = count;
    reader.lines  = 0;
    reader.queued = 0;
    memset(reader.known, 0, sizeof(reader.known));

    /* One pass up to the head as it was at the start, so a running machine cannot keep it
    ** going. */
    cursor = after;
    while ((Int32)(head - cursor) > 0 && !more) {
        UInt32 first;
        UInt32 dropped;
        int    n = debugTraceRead(cursor, buf, READ_CHUNK, &first, &dropped);
        int    i;

        droppedTotal += dropped;
        /* Nothing before a gap can take a CONT from after it. */
        if (dropped != 0) {
            drain(&reader, 1);
        }
        if (n == 0) {
            break;
        }
        if (cursor == after) {
            firstRead = first;
        }
        for (i = 0; i < n && (Int32)(head - (first + i)) >= 0; i++) {
            if (!take(&reader, &buf[i], first + (UInt32)i)) {
                more = 1;
                break;
            }
            cursor = first + (UInt32)i;
        }
        if (i < n) {
            break;
        }
    }
    /* Stopped at count or room, or at the head with a CONT still to come: what waits for
    ** its CONT is read again whole next time. Only a read that printed nothing prints it
    ** without, so every read moves on. */
    if (reader.queued > 0 && reader.lines > 0) {
        cursor = reader.queue[0].seq - 1;
        reader.queued = 0;
        more = 1;
    }
    drain(&reader, 1);

    ctrlTextAdd(&call->text, "trace of ");
    addClassNames(&call->text, reader.mask);
    if ((Int32)(cursor - after) > 0) {
        ctrlTextAdd(&call->text, " (core lines always): seq %u to %u read (newest %u), %u dropped\n",
                    (unsigned)firstRead, (unsigned)cursor, (unsigned)head, (unsigned)droppedTotal);
    }
    else {
        ctrlTextAdd(&call->text, ": nothing after seq %u (newest %u), %u dropped\n",
                    (unsigned)after, (unsigned)head, (unsigned)droppedTotal);
    }
    if ((debugTraceArmed & reader.mask) == 0) {
        ctrlTextAdd(&call->text, "none of these classes is armed now (op=arm)\n");
    }
    if (droppedTotal != 0) {
        ctrlTextAdd(&call->text, "the ring moved past %u records before they were read: pause, "
                    "or step frames with bmsx_run how=frame, between reads\n",
                    (unsigned)droppedTotal);
    }
    ctrlTextAdd(&call->text, "%s", lineBuffer);
    if (more) {
        ctrlTextAdd(&call->text, "more: read again with after=%u\n", (unsigned)cursor);
    }
    jsonOutUInt(&call->out, "next", cursor);
    jsonOutUInt(&call->out, "head", head);
    jsonOutUInt(&call->out, "dropped", droppedTotal);
    jsonOutBool(&call->out, "more", more);
    return ctrlReplyDone(call);
}
