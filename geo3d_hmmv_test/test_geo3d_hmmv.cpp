// Host test for the Geo3D fill-span (HMMV) conversion in blueMSX+.
// Links only Geo3D.cpp + Geo3DCore.cpp (the wrapper and the core), like
// GEO3D_V2.md's standalone core checks. Exit code 0 = all checks passed.
#include "Geo3D.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void idx(Geo3D* g, int i) { geo3dWriteIndex(g, (UInt8)i); }
static void dat(Geo3D* g, int b) { geo3dWriteData(g, (UInt8)b); }
static void dat16(Geo3D* g, int v) { dat(g, v & 0xFF); dat(g, (v >> 8) & 0xFF); }

// Identity projection: sx = 128 + x, sy = 100 - y, with z = 100, F = 100.
static void setupCommon(Geo3D* g, int lop)
{
    static const int cfg[18] = {16384,0,0, 0,16384,0, 0,0,16384,
                                0,0,0, 100, 128,100, 1, 256,212};
    idx(g, 0x00);
    for (int w = 0; w < 18; ++w) dat16(g, cfg[w]);
    idx(g, 0x40); dat(g, 0);            // vaddr = 0
    idx(g, 0x42); dat(g, 4);            // nvert = 4
    idx(g, 0x50);                       // vertices (world x, y, z = 100)
    static const int v[4][3] = {{-118,90,100},{-68,90,100},{-68,60,100},{-118,60,100}};
    for (auto& p : v) { dat16(g, p[0]); dat16(g, p[1]); dat16(g, p[2]); }
    idx(g, 0x45); dat(g, lop);          // lop
}

static void setupFace(Geo3D* g, int base)
{
    idx(g, 0x58); dat(g, 0);            // faddr = 0
    idx(g, 0x59); dat(g, 1);            // nface = 1
    idx(g, 0x52);                       // face: idx 0,1,2,3, normal (0,0,-16384), base
    dat(g, 0); dat(g, 1); dat(g, 2); dat(g, 3);
    dat16(g, 0); dat16(g, 0); dat16(g, -16384);
    dat(g, base);
}

struct Cmd { int size; int span; UInt8 regs[19]; UInt8 vals[19]; };
static std::vector<Cmd> drain(Geo3D* g)
{
    std::vector<Cmd> out;
    while (geo3dCommandsLeft(g) > 0) {
        Cmd c{}; c.span = geo3dPeekFillSpan(g);
        c.size = geo3dNextCommand(g, c.regs, c.vals);
        out.push_back(c);
    }
    return out;
}

int main()
{
    // ---- 1. untextured filled face: spans are tagged, bytes stay LINE ----
    Geo3D* g = geo3dCreate();
    setupCommon(g, 0);
    setupFace(g, 0x20);
    idx(g, 0x48); dat(g, 0x03);         // RUN + face mode
    auto cmds = drain(g);
    CHECK(cmds.size() == 31, "expected 31 spans, got %zu", cmds.size());
    for (size_t k = 0; k < cmds.size(); ++k) {
        const auto& c = cmds[k];
        CHECK(c.size == 11 && c.span == 1, "cmd %zu: size=%d span=%d", k, c.size, c.span);
        CHECK(c.vals[0] == 10 && c.vals[1] == 0, "cmd %zu DX", k);
        CHECK(c.vals[2] == (UInt8)(10 + k) && c.vals[3] == 0, "cmd %zu DY", k);
        CHECK(c.vals[4] == 50 && c.vals[5] == 0, "cmd %zu NX", k);
        CHECK(c.vals[6] == 0 && c.vals[7] == 0, "cmd %zu NY", k);
        CHECK(c.vals[8] == 0x26, "cmd %zu CLR=%02x", k, c.vals[8]);
        CHECK(c.vals[9] == 0, "cmd %zu ARG", k);
        CHECK(c.vals[10] == 0x70, "cmd %zu CMD=%02x", k, c.vals[10]);
        // conversion in SCREEN 8
        UInt8 v[19]; std::memcpy(v, c.vals, 19);
        CHECK(geo3dConvertFillSpan(v, c.size, c.span, 1) == 1, "cmd %zu not converted", k);
        CHECK(v[4] == 51 && v[5] == 0 && v[6] == 1 && v[7] == 0 && v[10] == 0xC0,
              "cmd %zu converted bytes wrong", k);
        CHECK(v[0] == c.vals[0] && v[2] == c.vals[2] && v[8] == c.vals[8] && v[9] == 0,
              "cmd %zu converted side bytes changed", k);
        // pixel equivalence: LINE draws DX..DX+NX (endpoint included),
        // the converted HMMV draws NX+1 pixels from DX on one row.
        std::set<std::pair<int,int>> a, b;
        for (int i = 0; i <= c.vals[4]; ++i) a.insert({c.vals[0] + i, c.vals[2]});
        int nx = v[4] | ((v[5] & 7) << 8);
        for (int i = 0; i < nx; ++i) b.insert({v[0] + i, v[2]});
        CHECK(a == b, "cmd %zu pixel sets differ", k);
        // not in SCREEN 8: no conversion, values untouched
        UInt8 w[19]; std::memcpy(w, c.vals, 19);
        CHECK(geo3dConvertFillSpan(w, c.size, c.span, 0) == 0 &&
              std::memcmp(w, c.vals, 19) == 0, "cmd %zu converted outside SCREEN 8", k);
    }
    geo3dDestroy(g);

    // ---- 2. wireframe edge with span-shaped bytes: NOT tagged ----
    g = geo3dCreate();
    setupCommon(g, 0);
    idx(g, 0x41); dat(g, 0);            // eaddr = 0
    idx(g, 0x43); dat(g, 1);            // nedge = 1
    idx(g, 0x51); dat(g, 0); dat(g, 1); // edge v0 -> v1 (horizontal, NY=0, ARG=0)
    idx(g, 0x48); dat(g, 0x01);         // RUN, edge mode
    cmds = drain(g);
    CHECK(cmds.size() == 1, "edge run: %zu commands", cmds.size());
    if (!cmds.empty()) {
        CHECK(cmds[0].span == 0, "wireframe LINE got a fill-span tag");
        UInt8 v[19]; std::memcpy(v, cmds[0].vals, 19);
        CHECK(geo3dConvertFillSpan(v, cmds[0].size, cmds[0].span, 1) == 0,
              "wireframe LINE was converted");
    }
    geo3dDestroy(g);

    // ---- 3. textured face: LRMM commands, NOT tagged ----
    g = geo3dCreate();
    setupCommon(g, 0);
    setupFace(g, 0xA0);                 // base bit7: textured
    idx(g, 0x48); dat(g, 0x07);         // RUN + face + texture
    cmds = drain(g);
    CHECK(!cmds.empty(), "textured run produced nothing");
    for (auto& c : cmds) {
        CHECK(c.size == 19 && c.span == 0, "textured cmd size=%d span=%d", c.size, c.span);
    }
    geo3dDestroy(g);

    // ---- 4. LOP != IMP: tagged, but the gate refuses ----
    g = geo3dCreate();
    setupCommon(g, 3);
    setupFace(g, 0x20);
    idx(g, 0x48); dat(g, 0x03);
    cmds = drain(g);
    CHECK(!cmds.empty(), "lop run produced nothing");
    if (!cmds.empty()) {
        CHECK(cmds[0].span == 1 && cmds[0].vals[10] == 0x73, "lop cmd tag/CMD");
        UInt8 v[19]; std::memcpy(v, cmds[0].vals, 19);
        CHECK(geo3dConvertFillSpan(v, cmds[0].size, 1, 1) == 0 &&
              std::memcmp(v, cmds[0].vals, 19) == 0, "LOP != IMP was converted");
    }
    geo3dDestroy(g);

    // ---- 5. gate conditions on raw values ----
    {
        UInt8 base[11] = {10,0, 20,0, 50,0, 0,0, 0x26, 0, 0x70};
        UInt8 v[19];
        auto tryConv = [&](void (*mut)(UInt8*)) {
            std::memcpy(v, base, 11); if (mut) mut(v);
            return geo3dConvertFillSpan(v, 11, 1, 1);
        };
        CHECK(tryConv(nullptr) == 1, "plain span refused");
        CHECK(tryConv([](UInt8* x){ x[1] = 1; }) == 0, "DX >= 256 converted");
        CHECK(tryConv([](UInt8* x){ x[6] = 1; }) == 0, "NY != 0 converted");
        CHECK(tryConv([](UInt8* x){ x[9] = 4; }) == 0, "ARG != 0 converted");
        CHECK(tryConv([](UInt8* x){ x[10] = 0x71; }) == 0, "LOP != IMP converted");
        // one-pixel span: LINE NX=0 draws its endpoint; HMMV NX=1 draws 1 pixel
        std::memcpy(v, base, 11); v[4] = 0;
        CHECK(geo3dConvertFillSpan(v, 11, 1, 1) == 1 && v[4] == 1 && v[6] == 1,
              "one-pixel span wrong");
    }

    // ---- 6. savestate round trip keeps the tags (layout 5) ----
    g = geo3dCreate();
    setupCommon(g, 0);
    setupFace(g, 0x20);
    idx(g, 0x48); dat(g, 0x03);
    for (int i = 0; i < 5; ++i) { UInt8 r[19], v[19]; geo3dNextCommand(g, r, v); }
    UInt32 sz = 0; UInt8* blob = geo3dSaveState(g, &sz);
    CHECK(blob != nullptr && sz > 0, "saveState failed");
    Geo3D* g2 = geo3dCreate();
    geo3dLoadState(g2, blob, sz, 5);
    auto rest = drain(g2);
    CHECK(rest.size() == 26, "rest after load: %zu", rest.size());
    for (auto& c : rest) CHECK(c.span == 1, "tag lost over savestate");
    geo3dDestroy(g); geo3dDestroy(g2);

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
