#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Isolated V9968 palette regression checks (no BIOS or game assets).

Run from a developer shell with `python scripts/test-v9968-palette.py`.
Uses the production palette function, state field assignments and SaveState.c
scalar serialization routines. The ZIP container and complete machine restore
are outside this isolated test; field streams follow the real tag encoding.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    pos = opening + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=None)
    parser.add_argument('--source', type=Path, help='alternate VDP.c for regression checks')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    vdp = (args.source or root / 'blueMSX/Src/VideoChips/VDP.c').read_text(encoding='utf-8')
    state = (root / 'blueMSX/Src/Utils/SaveState.c').read_text(encoding='utf-8')
    fields = ['vdpDataLatch', 'palKey', 'paletteLatch', 'paletteExtRedLatch']
    save = []
    load = []
    for field in fields:
        a = re.findall(r'^\s*saveStateSet\(state, "' + field + r'",[^\n]+;', vdp, re.M)
        b = re.findall(r'^\s*vdp->' + field + r'\s*= [^\n]*saveStateGet\(state,[^\n]+;', vdp, re.M)
        if not a or not b:
            raise RuntimeError('Missing production state field: ' + field)
        save.append(a[-1].strip())
        load.append(b[-1].strip())
    prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef struct {
    UInt32 allocSize, size, offset;
    UInt32 *buffer;
    char fileName[64];
} SaveState;
#define ALLOC_BLOCK_SIZE 256
#define checkTag(state, tagName) ((void)0)
typedef struct {
    UInt8 vdpDataLatch, paletteLatch, paletteExtRedLatch;
    int palKey, type;
    UInt8 vdpRegs[64];
    UInt16 paletteReg[256], paletteExtReg[256];
} VDP;
static int vdpIsExtPalette(VDP *v) { return v->type == 2 && (v->vdpRegs[20] & 0x10); }
static unsigned boardSystemTime(void) { return 0; }
static void sync(VDP *v, unsigned t) { (void)v; (void)t; }
static void updateExtPalette(VDP *v, int i, int r, int g, int b) {
    v->paletteExtReg[i] = (UInt16)((r << 10) | (g << 5) | b);
}
static void updatePalette(VDP *v, int i, int r, int g, int b) {
    (void)v; (void)i; (void)r; (void)g; (void)b;
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
'''
    helpers = '\n'.join(function(state, sig) for sig in [
        'static UInt32 tagFromName(', 'static void stateExtendBuffer(',
        'void saveStateSet(', 'UInt32 saveStateGet('])
    palette = function(vdp, 'static void writePaletteLatch(')
    reset = re.findall(r'^\s*vdp->paletteExtRedLatch\s*=\s*0;', vdp, re.M)
    if not reset:
        raise RuntimeError('Missing production reset for EPAL latch')
    palette += '\nstatic void resetRed(VDP *vdp) {' + reset[-1].strip() + '}\n'
    harness = r'''
static VDP init(int type, int epal) {
    VDP v; memset(&v, 0, sizeof(v)); v.type = type; v.vdpRegs[20] = epal ? 0x10 : 0;
    v.vdpRegs[16] = 3; v.paletteExtRedLatch = 17; return v;
}
static void saveFields(SaveState *state, VDP *vdp, int legacy) {
@SAVE@
    saveStateSet(state, "r16", vdp->vdpRegs[16]);
}
static void loadFields(SaveState *state, VDP *vdp) {
@LOAD@
    vdp->vdpRegs[16] = (UInt8)saveStateGet(state, "r16", 0);
}
int main(void) {
    unsigned type, rb, g, b, r, phase, injected, standard = 0, extended = 0;
    unsigned legacyCases = 0, roundtrips = 0;
    { VDP v = init(2, 1); resetRed(&v); CHECK(v.paletteExtRedLatch == 0); }
    for (type = 0; type < 3; ++type) for (rb = 0; rb < 256; ++rb)
    for (g = 0; g < 256; ++g) for (injected = 0; injected < 2; ++injected) {
        VDP v = init(type, 0);
        writePaletteLatch(&v, 0x9a, (UInt8)rb);
        CHECK(v.palKey == 1 && v.vdpDataLatch == rb);
        if (injected) v.vdpDataLatch = 0; /* R15 control-port first byte */
        writePaletteLatch(&v, 0x9a, (UInt8)g);
        CHECK(v.paletteReg[3] == ((g & 7) << 8 | ((injected ? 0 : rb) & 0x77)));
        CHECK(v.vdpRegs[16] == 4 && v.palKey == 0);
        CHECK(v.paletteExtRedLatch == 17); /* Standard path untouched. */
        ++standard;
    }
    for (r = 0; r < 32; ++r) for (g = 0; g < 32; ++g) for (b = 0; b < 32; ++b)
    for (phase = 1; phase <= 2; ++phase) {
        VDP v = init(2, 1);
        v.vdpDataLatch = 0x5a;
        writePaletteLatch(&v, 0x9a, (UInt8)r);
        CHECK(v.vdpDataLatch == 0x5a);
        if (phase == 1) v.vdpDataLatch = 0;
        writePaletteLatch(&v, 0x9a, (UInt8)g);
        if (phase == 2) v.vdpDataLatch = 0;
        writePaletteLatch(&v, 0x9a, (UInt8)b);
        CHECK(v.paletteExtReg[3] == (r << 10 | g << 5 | b));
        CHECK(v.palKey == 0 && v.vdpRegs[16] == 4);
        ++extended;
    }
    for (r = 0; r < 32; ++r) for (g = 0; g < 32; ++g) for (b = 0; b < 32; ++b)
    for (phase = 0; phase <= 2; ++phase) {
        SaveState state = {0}; VDP old = init(2, 1), loaded = init(2, 1);
        /* Legacy EPAL writer saved pending R in vdpDataLatch. */
        old.palKey = (int)phase; old.vdpDataLatch = (UInt8)r; old.paletteLatch = (UInt8)g;
        saveFields(&state, &old, 1);
        state.offset = 0; loadFields(&state, &loaded);
        CHECK(loaded.paletteExtRedLatch == r && loaded.palKey == (int)phase);
        loaded.vdpDataLatch = 0; /* Register access after loading. */
        if (phase == 0) writePaletteLatch(&loaded, 0x9a, (UInt8)r);
        if (phase < 2) writePaletteLatch(&loaded, 0x9a, (UInt8)g);
        writePaletteLatch(&loaded, 0x9a, (UInt8)b);
        CHECK(loaded.paletteExtReg[3] == (r << 10 | g << 5 | b));
        free(state.buffer); ++legacyCases;
    }
    for (r = 0; r < 32; ++r) for (g = 0; g < 32; ++g) for (b = 0; b < 32; ++b)
    for (phase = 1; phase <= 2; ++phase) {
        SaveState state = {0}; VDP v = init(2, 1), loaded = init(2, 1);
        writePaletteLatch(&v, 0x9a, (UInt8)r);
        if (phase == 2) writePaletteLatch(&v, 0x9a, (UInt8)g);
        v.vdpDataLatch = 0; /* Control latch differs from saved R. */
        saveFields(&state, &v, 0); state.offset = 0; loadFields(&state, &loaded);
        CHECK(loaded.vdpDataLatch == 0 && loaded.paletteExtRedLatch == r);
        if (phase == 1) writePaletteLatch(&loaded, 0x9a, (UInt8)g);
        writePaletteLatch(&loaded, 0x9a, (UInt8)b);
        CHECK(loaded.paletteExtReg[3] == (r << 10 | g << 5 | b));
        free(state.buffer); ++roundtrips;
    }
    for (type = 0; type < 3; ++type) for (rb = 0; rb < 256; ++rb) {
        SaveState state = {0}; VDP v = init(type, 0), loaded = init(type, 0);
        writePaletteLatch(&v, 0x9a, (UInt8)rb);
        saveFields(&state, &v, 1); state.offset = 0; loadFields(&state, &loaded);
        writePaletteLatch(&loaded, 0x9a, 5);
        CHECK(loaded.paletteReg[3] == (0x500 | (rb & 0x77)));
        free(state.buffer);
    }
    printf("PASS production EPAL latch reset\n");
    printf("PASS standard palette: %u cases (V9938/V9958/V9968 EPAL off)\n", standard);
    printf("PASS extended RGB/control interleavings: %u\n", extended);
    printf("PASS legacy scalar state streams, phases 0/1/2: %u\n", legacyCases);
    printf("PASS new scalar state roundtrips, phases 1/2: %u\n", roundtrips);
    printf("PASS legacy standard-palette partial entries: 768\n");
    return 0;
}
'''
    save[-1] = 'if (!legacy) ' + save[-1]
    harness = harness.replace('@SAVE@', '\n'.join(save)).replace('@LOAD@', '\n'.join(load))
    compiler = args.compiler or shutil.which('cl') or shutil.which('cc') or shutil.which('gcc')
    if not compiler:
        raise RuntimeError('Run in a C developer shell, or pass --compiler')
    with tempfile.TemporaryDirectory(prefix='v9968-palette-') as directory:
        work = Path(directory)
        source = work / 'palette.c'
        source.write_text(prefix + helpers + palette + harness, encoding='utf-8')
        exe = work / ('palette.exe' if os.name == 'nt' else 'palette')
        if Path(compiler).stem.lower() == 'cl':
            command = [compiler, '/nologo', '/TC', '/O2', str(source), '/Fe:' + str(exe)]
        else:
            command = [compiler, '-std=c99', '-O2', str(source), '-o', str(exe)]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(exe)], cwd=work, check=True)


if __name__ == '__main__':
    main()
