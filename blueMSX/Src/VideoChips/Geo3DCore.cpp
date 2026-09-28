// This file is taken from the geo3d branch of github.com/alexmoncks/openMSX.
// The file has been modified to be built in the blueMSX environment.
//
// Modified 2026 by Hesoten for blueMSX+ fork.
// See https://github.com/Hesoten/blueMSX-plus for change history.

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alex Moncks

#include "Geo3DCore.h"

#include <algorithm>
#include <cstdlib>

// Python semantics: // rounds towards minus infinity, >> is arithmetic
// (guaranteed for signed types since C++20).
static int64_t floorDiv(int64_t a, int64_t b)
{
	int64_t q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
	return q;
}

static int64_t sat(int64_t v, int64_t lo, int64_t hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static int64_t sat18(int64_t v) { return sat(v, -131071, 131071); }

static int64_t signed16(unsigned x)
{
	x &= 0xFFFF;
	return (x & 0x8000) ? int64_t(x) - 0x10000 : int64_t(x);
}

void Geo3DCore::reset()
{
	cfg = {0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0,  256, 128, 106, 16, 256, 212};
	ivtx = {0, 0, 0};
	imm = {0, 0, 0, 0};
	for (auto& v : vmem) v = {0, 0, 0};
	for (auto& e : emem) e = {0, 0};
	for (auto& f : fmem) f = Face{{0, 0, 0, 0}, {0, 0, 0}, 0};
	for (auto& t : tmem) t = {};
	for (auto& p : pmem) p = {0, 0, 0, 0};
	widx = 0; rptr = 0x30; lo = 0;
	vaddr = eaddr = nvert = nedge = 0;
	color = 15; lop = 0; ypage = 0;
	faddr = nface = taddr = tstride = 0;
	texx = 0; texy = 512;
	light = {0, 0, -16384};
	vbuf.clear(); ebuf.clear(); fbuf.clear(); tbuf.clear();
	ctrlFace = ctrlTex = false;
	cmds.clear(); cmdPos = 0;
	running = false;
	cntSkip = cntDraw = cntCull = 0;
}

void Geo3DCore::writeIndex(uint8_t value)
{
	widx = value;
	rptr = value;
	vbuf.clear(); ebuf.clear(); fbuf.clear(); tbuf.clear();
}

void Geo3DCore::writeData(uint8_t b)
{
	uint8_t i = widx;
	if (i < 0x2A) {
		if ((i & 1) == 0) {
			lo = b;
		} else {
			unsigned word = (b << 8) | lo;
			unsigned wi = i >> 1;
			if (wi < 18) {
				cfg[wi] = (wi == 12) ? int64_t(word) : signed16(word);
			} else {
				ivtx[wi - 18] = signed16(word);
				if (wi == 20) {           // writing VZ computes the vertex
					imm = project(cfg, ivtx);
					rptr = 0x30;
				}
			}
		}
	} else {
		switch (i) {
		case 0x40: vaddr = b; break;
		case 0x41: eaddr = b; break;
		case 0x42: nvert = b; break;
		case 0x43: nedge = b; break;
		case 0x44: color = b; break;
		case 0x45: lop = b & 0xF; break;
		case 0x46: ypage = (ypage & 0x700) | b; break;
		case 0x47: ypage = (ypage & 0xFF) | ((b & 7) << 8); break;
		case 0x48:
			ctrlFace = (b & 2) != 0;
			ctrlTex  = (b & 4) != 0;
			if ((b & 1) && !running) startRun(b);  // RTL takes RUN only when idle
			break;
		case 0x50:
			vbuf.push_back(b);
			if (vbuf.size() == 6) {
				for (int k = 0; k < 3; ++k) {
					vmem[vaddr][k] = signed16(vbuf[2 * k] | (vbuf[2 * k + 1] << 8));
				}
				++vaddr;
				vbuf.clear();
			}
			break;
		case 0x51:
			ebuf.push_back(b);
			if (ebuf.size() == 2) {
				emem[eaddr] = {ebuf[0], ebuf[1]};
				++eaddr;
				ebuf.clear();
			}
			break;
		case 0x52:
			fbuf.push_back(b);
			if (fbuf.size() == 11) {
				auto& f = fmem[faddr];
				for (int k = 0; k < 4; ++k) f.i[k] = fbuf[k];
				for (int k = 0; k < 3; ++k) {
					f.n[k] = signed16(fbuf[4 + 2 * k] | (fbuf[5 + 2 * k] << 8));
				}
				f.base = fbuf[10];
				++faddr;
				fbuf.clear();
			}
			break;
		case 0x53:
			tbuf.push_back(b);
			if (tbuf.size() == 8) {
				std::copy(tbuf.begin(), tbuf.end(), tmem[taddr].begin());
				++taddr;
				tbuf.clear();
			}
			break;
		case 0x58: faddr = b; break;
		case 0x59: nface = b; break;
		case 0x5A: case 0x5C: case 0x5E: lo = b; break;
		case 0x5B: case 0x5D: case 0x5F:
			light[(i - 0x5B) / 2] = signed16((b << 8) | lo);
			break;
		case 0x60: texx = (texx & 0x100) | b; break;
		case 0x61: texx = (texx & 0xFF) | ((b & 1) << 8); break;
		case 0x62: texy = (texy & 0x1F00) | b; break;
		case 0x63: texy = (texy & 0xFF) | ((b & 0x1F) << 8); break;
		case 0x64: tstride = b; break;
		case 0x65: taddr = b; break;
		default: break;
		}
	}
	// index advance (next_idx in the RTL)
	if ((i >> 4) == 4) {
		widx = 0x40 | ((i + 1) & 0xF);
	} else if ((i >> 3) == 0x0B) {
		widx = 0x58 | ((i + 1) & 0x7);
	} else if ((i >> 3) == 0x0C) {
		widx = 0x60 | ((i + 1) & 0x7);
	} else if (i == 0x29) {
		widx = 0x24;
	} else if (i < 0x40) {
		widx = i + 1;
	}
}

uint8_t Geo3DCore::readStatus() const
{
	// bit0 RUN busy, bit2 edge phase (commands still to be issued);
	// the transform phase (bit1) and core busy (bit3) take no time here.
	uint8_t edge = (running && hasCommand()) ? 0x04 : 0x00;
	return uint8_t((imm.flags & 0xF0) | edge | (running ? 0x01 : 0x00));
}

uint8_t Geo3DCore::readReg(uint8_t idx) const
{
	switch (idx) {
	case 0x30: return uint8_t(imm.sx);
	case 0x31: return uint8_t(imm.sx >> 8);
	case 0x32: return uint8_t(imm.sy);
	case 0x33: return uint8_t(imm.sy >> 8);
	case 0x34: return uint8_t(imm.z);
	case 0x35: return uint8_t(imm.z >> 8);
	case 0x36: return uint8_t(imm.flags);
	case 0x40: return vaddr;
	case 0x41: return eaddr;
	case 0x42: return nvert;
	case 0x43: return nedge;
	case 0x44: return color;
	case 0x45: return lop;
	case 0x46: return uint8_t(ypage);
	case 0x47: return uint8_t(ypage >> 8);
	case 0x48: return readStatus();
	case 0x4A: return uint8_t(cntSkip);
	case 0x4B: return uint8_t(cntSkip >> 8);
	case 0x4C: return uint8_t(cntDraw);
	case 0x4D: return uint8_t(cntDraw >> 8);
	case 0x4E: return uint8_t(cntCull);
	case 0x4F: return uint8_t(cntCull >> 8);
	case 0x58: return faddr;
	case 0x59: return nface;
	case 0x65: return taddr;
	default:   return 0xFF;
	}
}

uint8_t Geo3DCore::peekData() const
{
	return readReg(rptr);
}

uint8_t Geo3DCore::readData()
{
	uint8_t v = readReg(rptr);
	if (rptr == 0x36) {
		rptr = 0x30;
	} else if ((rptr >> 4) == 4) {
		rptr = 0x40 | ((rptr + 1) & 0xF);
	} else if (rptr >= 0x30 && rptr < 0x36) {
		++rptr;
	}
	return v;
}

void Geo3DCore::finishRun()
{
	running = false;
	cmds.clear();
	cmdPos = 0;
}

// ---------------------------------------------------------------- state
namespace {

struct Writer {
	std::vector<uint8_t>& out;
	void u8(uint8_t v) { out.push_back(v); }
	void i64(int64_t v) {
		for (int k = 0; k < 8; ++k) out.push_back(uint8_t(uint64_t(v) >> (8 * k)));
	}
};

struct Reader {
	const std::vector<uint8_t>& in;
	size_t pos = 0;
	bool ok = true;
	uint8_t u8() {
		if (pos >= in.size()) { ok = false; return 0; }
		return in[pos++];
	}
	int64_t i64() {
		uint64_t v = 0;
		for (int k = 0; k < 8; ++k) v |= uint64_t(u8()) << (8 * k);
		return int64_t(v);
	}
};

} // namespace

// One field list for both directions (Writer or Reader).
template<typename IO, typename U8, typename I64>
void Geo3DCore::visitState(Geo3DCore& c, IO& io, U8 u8, I64 i64)
{
	for (auto& x : c.cfg) i64(io, x);
	for (auto& x : c.ivtx) i64(io, x);
	auto proj = [&](auto& p) {
		i64(io, p.sx); i64(io, p.sy); i64(io, p.z);
		int64_t f = p.flags; i64(io, f); p.flags = unsigned(f);
	};
	proj(c.imm);
	for (auto& v : c.vmem) for (auto& x : v) i64(io, x);
	for (auto& e : c.emem) for (auto& x : e) u8(io, x);
	for (auto& f : c.fmem) {
		for (auto& x : f.i) u8(io, x);
		for (auto& x : f.n) i64(io, x);
		u8(io, f.base);
	}
	for (auto& t : c.tmem) for (auto& x : t) u8(io, x);
	for (auto& p : c.pmem) proj(p);
	for (uint8_t* p : {&c.widx, &c.rptr, &c.lo, &c.vaddr, &c.eaddr, &c.nvert,
	                   &c.nedge, &c.color, &c.lop, &c.faddr, &c.nface, &c.taddr,
	                   &c.tstride}) {
		u8(io, *p);
	}
	for (unsigned* p : {&c.ypage, &c.texx, &c.texy}) {
		int64_t v = *p; i64(io, v); *p = unsigned(v);
	}
	for (auto& x : c.light) i64(io, x);
	for (auto* b : {&c.vbuf, &c.ebuf, &c.fbuf, &c.tbuf}) {
		int64_t n = int64_t(b->size()); i64(io, n);
		if (n < 0 || n > 16) n = 0;
		b->resize(size_t(n));
		for (auto& x : *b) u8(io, x);
	}
	for (bool* p : {&c.ctrlFace, &c.ctrlTex, &c.running}) {
		uint8_t v = *p; u8(io, v); *p = v != 0;
	}
	for (uint16_t* p : {&c.cntSkip, &c.cntDraw, &c.cntCull}) {
		int64_t v = *p; i64(io, v); *p = uint16_t(v);
	}
	int64_t pos = int64_t(c.cmdPos); i64(io, pos);
	int64_t n = int64_t(c.cmds.size()); i64(io, n);
	if (n < 0 || n > (1 << 24)) n = 0;
	c.cmds.resize(size_t(n));
	c.cmdPos = size_t(std::clamp<int64_t>(pos, 0, n));
	for (auto& cmd : c.cmds) {
		u8(io, cmd.size);
		for (auto& x : cmd.bytes) u8(io, x);
	}
}

std::vector<uint8_t> Geo3DCore::saveState() const
{
	std::vector<uint8_t> out;
	Writer w{out};
	auto& self = const_cast<Geo3DCore&>(*this);   // visitState only reads when writing
	visitState(self, w,
	           [](Writer& o, uint8_t& x) { o.u8(x); },
	           [](Writer& o, int64_t& x) { o.i64(x); });
	return out;
}

void Geo3DCore::loadState(const std::vector<uint8_t>& data)
{
	Geo3DCore tmp;
	Reader r{data};
	visitState(tmp, r,
	           [](Reader& i, uint8_t& x) { x = i.u8(); },
	           [](Reader& i, int64_t& x) { x = i.i64(); });
	if (r.ok && r.pos == data.size()) *this = tmp;
}

// ---------------------------------------------------------------- transform
Geo3DCore::Proj Geo3DCore::project(const std::array<int64_t, 18>& c,
                                   const std::array<int64_t, 3>& v)
{
	int64_t xyz[3];
	for (int r = 0; r < 3; ++r) {
		int64_t acc = c[3 * r] * v[0] + c[3 * r + 1] * v[1] + c[3 * r + 2] * v[2];
		xyz[r] = sat((acc >> 14) + c[9 + r], -131071, 131071);
	}
	int64_t f = c[12], cx = c[13], cy = c[14], znear = c[15], w = c[16], h = c[17];
	int64_t zc = xyz[2];
	int64_t zout = sat(zc, -32768, 32767);
	if (zc < znear || zc <= 0) {
		return {0, 0, zout, 0x01};
	}
	unsigned flags = 0;
	int64_t q[2];
	for (int axis = 0; axis < 2; ++axis) {
		int64_t cc = xyz[axis];
		int64_t a = std::abs(cc);
		int64_t num = a * f;
		int64_t qq;
		if (num >= (zc << 15)) {
			qq = 32767;
			flags |= 1u << (1 + axis);
		} else {
			qq = num / zc;               // both non-negative
		}
		q[axis] = (cc < 0) ? -qq : qq;
	}
	int64_t sx = sat(cx + q[0], -32768, 32767);
	int64_t sy = sat(cy - q[1], -32768, 32767);
	unsigned outc = (sx < 0) | ((sx >= w) << 1) | ((sy < 0) << 2) | ((sy >= h) << 3);
	flags |= outc << 4;
	return {sx, sy, zout, flags};
}

void Geo3DCore::startRun(uint8_t ctrl)
{
	cntSkip = cntDraw = cntCull = 0;
	for (unsigned vi = 0; vi < nvert; ++vi) {
		pmem[vi] = project(cfg, vmem[vi]);
	}
	if (nvert != 0) {
		// the engine streams the vertices through the same core, so the
		// immediate registers end up holding the last one
		ivtx = vmem[nvert - 1];
		imm = pmem[nvert - 1];
	}
	cmds.clear();
	cmdPos = 0;
	if (ctrl & 2) {
		renderFaces((ctrl & 4) != 0);
	} else {
		renderEdges();
	}
	running = true;
}

// ---------------------------------------------------------------- wireframe
namespace {

struct Pt { int64_t x, y; };

unsigned ocode(Pt p, int64_t w, int64_t h)
{
	return (p.x < 0) | ((p.x >= w) << 1) | ((p.y < 0) << 2) | ((p.y >= h) << 3);
}

Pt mid(Pt a, Pt b)
{
	return {(a.x + b.x) >> 1, (a.y + b.y) >> 1};   // floor, like the RTL
}

enum class Clip { SKIP, CULL, DRAW };

Clip clipEdge(Pt A, Pt B, int64_t w, int64_t h, Pt& e1, Pt& e2)
{
	unsigned ca = ocode(A, w, h), cb = ocode(B, w, h);
	if (ca & cb) return Clip::CULL;
	Pt p;
	if (ca == 0) {
		p = A;
	} else if (cb == 0) {
		p = B;
	} else {
		Pt lo = A, hi = B;
		bool found = false;
		for (int it = 0; it < 16; ++it) {
			Pt m = mid(lo, hi);
			unsigned cm = ocode(m, w, h);
			if (cm == 0) { p = m; found = true; break; }
			if (ocode(lo, w, h) & cm) {
				lo = m;
			} else if (cm & ocode(hi, w, h)) {
				hi = m;
			} else {
				return Clip::SKIP;
			}
		}
		if (!found) return Clip::SKIP;
	}
	auto bisect = [&](Pt inside, Pt outside) {
		Pt lo = inside, hi = outside;
		for (int it = 0; it < 16; ++it) {
			Pt m = mid(lo, hi);
			if (ocode(m, w, h) == 0) lo = m; else hi = m;
		}
		return lo;
	};
	e1 = (ca == 0) ? A : bisect(p, A);
	e2 = (cb == 0) ? B : bisect(p, B);
	return Clip::DRAW;
}

} // namespace

void Geo3DCore::renderEdges()
{
	int64_t w = cfg[16], h = cfg[17];
	for (unsigned e = 0; e < nedge; ++e) {
		const Proj& pa = pmem[emem[e][0]];
		const Proj& pb = pmem[emem[e][1]];
		if ((pa.flags & 7) || (pb.flags & 7)) {
			++cntSkip;
			continue;
		}
		Pt e1, e2;
		switch (clipEdge({pa.sx, pa.sy}, {pb.sx, pb.sy}, w, h, e1, e2)) {
		case Clip::SKIP: ++cntSkip; break;
		case Clip::CULL: ++cntCull; break;
		case Clip::DRAW: {
			int64_t dx = e2.x - e1.x, dy = e2.y - e1.y;
			int64_t ax = std::abs(dx), ay = std::abs(dy);
			bool maj = ay > ax;
			int64_t nx = maj ? ay : ax;
			int64_t ny = maj ? ax : ay;
			uint8_t arg = uint8_t(((dy < 0) << 3) | ((dx < 0) << 2) | int(maj));
			int64_t dyf = ((e1.y & 0x7FF) + ypage) & 0x7FF;
			Command c;
			c.size = 11;
			c.bytes = {uint8_t(e1.x & 0xFF), uint8_t((e1.x >> 8) & 1),
			           uint8_t(dyf & 0xFF), uint8_t(dyf >> 8),
			           uint8_t(nx & 0xFF), uint8_t((nx >> 8) & 7),
			           uint8_t(ny & 0xFF), uint8_t((ny >> 8) & 7),
			           color, arg, uint8_t(0x70 | lop)};
			cmds.push_back(c);
			++cntDraw;
			break;
		}
		}
	}
}

// ---------------------------------------------------------------- faces
void Geo3DCore::renderFaces(bool texOn)
{
	int64_t w = cfg[16], h = cfg[17];
	int64_t lm[3];
	for (int j = 0; j < 3; ++j) {
		int64_t s = 0;
		for (int i = 0; i < 3; ++i) s += cfg[3 * i + j] * light[i];
		lm[j] = sat18(s >> 14);
	}

	struct Vis { int64_t key; unsigned fidx; uint8_t col; int64_t lvl; };
	std::vector<Vis> vis;
	for (unsigned fidx = 0; fidx < nface; ++fidx) {
		const Face& f = fmem[fidx];
		const Proj* pv[4];
		bool near = false;
		for (int k = 0; k < 4; ++k) {
			pv[k] = &pmem[f.i[k]];
			if (pv[k]->flags & 7) near = true;
		}
		if (near) { ++cntSkip; continue; }
		int64_t x0 = pv[0]->sx, y0 = pv[0]->sy;
		int64_t x1 = pv[1]->sx, y1 = pv[1]->sy;
		int64_t x2 = pv[2]->sx, y2 = pv[2]->sy;
		int64_t area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
		if (area <= 0) { ++cntCull; continue; }
		int64_t shade = (lm[0] * f.n[0] + lm[1] * f.n[1] + lm[2] * f.n[2]) >> 14;
		int64_t lvl = (shade <= 0) ? 0 : std::min<int64_t>(6, (shade * 7) >> 14);
		int64_t key = pv[0]->z + pv[1]->z + pv[2]->z + pv[3]->z;
		vis.push_back({key, fidx, uint8_t((f.base + lvl) & 0xFF), lvl});
	}
	// painter's order: farthest first, ties by lower face index
	std::stable_sort(vis.begin(), vis.end(),
	                 [](const Vis& a, const Vis& b) { return a.key > b.key; });
	cntDraw = uint16_t(vis.size());

	struct Cand { int64_t x, u, v; };
	for (const auto& vf : vis) {
		const Face& f = fmem[vf.fidx];
		bool textured = texOn && (f.base & 0x80);
		static const std::array<uint8_t, 8> noUV = {};
		const auto& uv = textured ? tmem[vf.fidx] : noUV;
		int64_t xs[4], ys[4], us[4], vs[4];
		for (int k = 0; k < 4; ++k) {
			xs[k] = pmem[f.i[k]].sx;
			ys[k] = pmem[f.i[k]].sy;
			us[k] = uv[2 * k];
			vs[k] = uv[2 * k + 1];
		}
		int64_t ymin = std::max<int64_t>(0, std::min({ys[0], ys[1], ys[2], ys[3]}));
		int64_t ymax = std::min<int64_t>(h - 1, std::max({ys[0], ys[1], ys[2], ys[3]}));
		for (int64_t y = ymin; y <= ymax; ++y) {
			bool have = false;
			Cand L{}, R{};
			auto cand = [&](Cand c) {
				if (!have || c.x < L.x) L = c;
				if (!have || c.x > R.x) R = c;
				have = true;
			};
			for (int k = 0; k < 4; ++k) {
				int a = k, b = (k + 1) & 3;
				int64_t xa = xs[a], ya = ys[a], xb = xs[b], yb = ys[b];
				if (ya == yb) {
					if (y == ya) {
						cand({xa, us[a] * 256, vs[a] * 256});
						cand({xb, us[b] * 256, vs[b] * 256});
					}
				} else if (std::min(ya, yb) <= y && y <= std::max(ya, yb)) {
					int64_t dy_ = y - ya, den = yb - ya;
					int64_t x = xa + floorDiv(dy_ * (xb - xa), den);
					int64_t u = 0, v = 0;
					if (textured) {
						u = us[a] * 256 + floorDiv(dy_ * (us[b] - us[a]) * 256, den);
						v = vs[a] * 256 + floorDiv(dy_ * (vs[b] - vs[a]) * 256, den);
					}
					cand({x, u, v});
				}
			}
			if (!have) continue;
			int64_t xl = L.x, xr = R.x;
			int64_t cl = std::max<int64_t>(xl, 0), cr = std::min<int64_t>(xr, w - 1);
			if (cl > cr) continue;
			int64_t dy = (y + ypage) & 0x7FF;
			Command c;
			if (!textured) {
				int64_t nx = cr - cl;
				c.size = 11;
				c.bytes = {uint8_t(cl & 0xFF), uint8_t((cl >> 8) & 1),
				           uint8_t(dy & 0xFF), uint8_t(dy >> 8),
				           uint8_t(nx & 0xFF), uint8_t((nx >> 8) & 7), 0, 0,
				           vf.col, 0, uint8_t(0x70 | lop)};
				cmds.push_back(c);
				continue;
			}
			int64_t du, dv;
			if (xr == xl) {
				du = dv = 0;
			} else {
				int64_t d = sat18(xr - xl);
				du = sat(floorDiv(sat18(R.u - L.u), d), -32767, 32767);
				dv = sat(floorDiv(sat18(R.v - L.v), d), -32767, 32767);
			}
			int64_t off = sat18(cl - xl);
			int64_t su = L.u + off * du;
			int64_t sv = L.v + off * dv;
			int64_t sx = (int64_t(texx) + (su >> 8)) & 0xFFF;
			int64_t sy = (int64_t(texy) + vf.lvl * tstride + (sv >> 8)) & 0x1FFF;
			int64_t nxp = cr - cl + 1;
			c.size = 19;
			c.bytes = {uint8_t(sx & 0xFF), uint8_t(sx >> 8),
			           uint8_t(sy & 0xFF), uint8_t(sy >> 8),
			           uint8_t(cl & 0xFF), uint8_t((cl >> 8) & 1),
			           uint8_t(dy & 0xFF), uint8_t(dy >> 8),
			           uint8_t(nxp & 0xFF), uint8_t((nxp >> 8) & 7), 1, 0,
			           vf.col, 0,
			           uint8_t(du & 0xFF), uint8_t((du >> 8) & 0xFF),
			           uint8_t(dv & 0xFF), uint8_t((dv >> 8) & 0xFF),
			           uint8_t(0x30 | lop)};
			cmds.push_back(c);
		}
	}
}

