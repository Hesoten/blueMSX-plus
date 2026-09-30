// This file is taken from the geo3d branch of github.com/alexmoncks/openMSX.
// The file has been modified to be built in the blueMSX environment.
//
// Modified 2026 by Hesoten for blueMSX+ fork.
// See https://github.com/Hesoten/blueMSX-plus for change history.

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alex Moncks
//
// Geo3DCore: bit-exact model of the geo3d coprocessor (geo3d_engine.v in the
// V9968_Cartridge repository), with no dependency on the rest of openMSX.
//
// Input:  Z80 writes/reads on the two geo3d ports (index/status, data).
// Output: the V9968 command list of one RUN (LINE = 11 bytes for R#36..R#46,
//         LRMM = 19 bytes, see Command), the SKIPPED/DRAWN/CULLED counters and
//         the status byte.
//
// The algorithms are a 1:1 port of the Python reference model
// (sim/gen_vectors.py model(), sim/gen_scenes.py replay(), render(),
// render_faces()), which the RTL matches bit for bit. All arithmetic is done
// in int64_t with explicit saturations and masks, floor division and
// arithmetic right shifts, like Python integers.
//
// After a RUN the whole command list is computed at once, each command
// stamped with the engine clock (ec, 2 per 21.477 MHz tick, from RUN) at which
// the RTL's producer would have it ready if it never stalled on the holding
// register; endClock() is where it would reach R_DRAIN. The caller issues the
// commands (nextCommand) on its own timeline and calls finishRun() once the
// last one has completed, which clears the RUN busy status bit.

#ifndef GEO3DCORE_H
#define GEO3DCORE_H

#include <array>
#include <cstdint>
#include <vector>

class Geo3DCore
{
public:
	struct Command {
		// LINE: bytes[0..10] = R#36..R#46 (DX lo/hi, DY lo/hi, NX lo/hi,
		//       NY lo/hi, CLR, ARG, CMD).
		// LRMM: bytes[0..18] in log order: [0..13] = R#32..R#45,
		//       [14..17] = R#47..R#50 (VX, VY), [18] = R#46 (CMD, last).
		std::array<uint8_t, 19> bytes = {};
		uint8_t size = 0;
		uint32_t ready = 0;        // producer clock (ec) at E_BUILD / D_SPAN

		[[nodiscard]] bool isLrmm() const { return size == 19; }
		// VDP register number for byte k, in the order the RTL writes them.
		[[nodiscard]] unsigned regNum(unsigned k) const {
			if (!isLrmm()) return 36 + k;
			return (k < 14) ? 32 + k : (k < 18) ? 33 + k : 46;
		}
	};

	Geo3DCore() { reset(); }

	void reset();

	// Port base+5 (SEL=0) and base+7 (SEL=1).
	void writeIndex(uint8_t value);
	void writeData(uint8_t value);
	// ec: engine clocks since RUN; edgeEnd: when the producer reaches
	// R_DRAIN, as the caller's timeline has it. The defaults show neither phase.
	[[nodiscard]] uint8_t readStatus(uint32_t ec = UINT32_MAX, uint32_t edgeEnd = 0) const;
	uint8_t readData(uint32_t ec = UINT32_MAX, uint32_t edgeEnd = 0);   // advances the read pointer
	[[nodiscard]] uint8_t peekData(uint32_t ec = UINT32_MAX, uint32_t edgeEnd = 0) const;

	// RUN handling, driven by the caller.
	[[nodiscard]] bool isRunning() const { return running; }
	[[nodiscard]] bool hasCommand() const { return cmdPos < cmds.size(); }
	const Command& nextCommand() { return cmds[cmdPos++]; }
	[[nodiscard]] const Command& peekCommand() const { return cmds[cmdPos]; }
	[[nodiscard]] uint32_t endClock() const { return endClk; }
	[[nodiscard]] uint32_t transformEndClock() const { return xfEndClk; }
	[[nodiscard]] size_t commandsLeft() const { return cmds.size() - cmdPos; }
	void finishRun();

	[[nodiscard]] uint16_t getSkipped() const { return cntSkip; }
	[[nodiscard]] uint16_t getDrawn()   const { return cntDraw; }
	[[nodiscard]] uint16_t getCulled()  const { return cntCull; }

	// Whole state as bytes (savestates / reverse), no openMSX dependency.
	[[nodiscard]] std::vector<uint8_t> saveState() const;
	// Layout 1 has no timing, 2 no transform end, 3 is the current one.
	static constexpr int STATE_LAYOUT = 3;
	void loadState(const std::vector<uint8_t>& data, int layout = STATE_LAYOUT);   // ignores bad data

	// Projection of one vertex (sim/gen_vectors.py model()).
	struct Proj { int64_t sx, sy, z; unsigned flags; };
	[[nodiscard]] static Proj project(const std::array<int64_t, 18>& cfg,
	                                  const std::array<int64_t, 3>& v);

private:
	template<typename IO, typename U8, typename I64>
	static void visitState(Geo3DCore& c, IO& io, U8 u8, I64 i64, int layout);

	void startRun(uint8_t ctrl);
	void renderEdges();
	void renderFaces(bool texOn);
	[[nodiscard]] uint8_t readReg(uint8_t idx, uint32_t ec, uint32_t edgeEnd) const;

	// configuration words M00..M22, TX, TY, TZ, F, CX, CY, ZNEAR, W, H
	std::array<int64_t, 18> cfg;
	std::array<int64_t, 3> ivtx;           // immediate vertex (0x24..0x29)
	Proj imm;                              // immediate results (0x30..0x36)

	struct Face { uint8_t i[4]; int64_t n[3]; uint8_t base; };
	std::array<std::array<int64_t, 3>, 256> vmem;
	std::array<std::array<uint8_t, 2>, 256> emem;
	std::array<Face, 256> fmem;
	std::array<std::array<uint8_t, 8>, 256> tmem;
	std::array<Proj, 256> pmem;            // projected vertices (kept between RUNs)

	uint8_t widx, rptr, lo;
	uint8_t vaddr, eaddr, nvert, nedge, color, lop;
	unsigned ypage;                        // 11 bits
	uint8_t faddr, nface, taddr, tstride;
	unsigned texx, texy;                   // 9 and 13 bits
	std::array<int64_t, 3> light;
	std::vector<uint8_t> vbuf, ebuf, fbuf, tbuf;
	bool ctrlFace, ctrlTex;

	std::vector<Command> cmds;
	size_t cmdPos;
	uint32_t clk;                          // producer clock during a RUN (ec)
	uint32_t endClk;                       // producer clock at R_DRAIN
	uint32_t xfEndClk;                     // producer clock when the transform ends
	bool running;
	uint16_t cntSkip, cntDraw, cntCull;
};


#endif
