// This file is taken from the openMSX project.
// The file has been modified to be built in the blueMSX environment.
//
// Modified 2026 by Hesoten for blueMSX+ fork.
// See https://github.com/Hesoten/blueMSX-plus for change history.

// $Id: OpenMsxYMF278.cpp,v 1.6 2008/03/31 22:07:05 hap-hap Exp $

#include "OpenMsxYMF278.h"

#include <stdio.h>
#include <cmath>
#include <cstring>
#include <stdlib.h>

extern "C" {
#include "SaveState.h"
}

const int EG_SH = 16;	// 16.16 fixed point (EG timing)
const unsigned int EG_TIMER_OVERFLOW = 1 << EG_SH;

// envelope output entries
const int ENV_BITS      = 10;
const int ENV_LEN       = 1 << ENV_BITS;
const DoubleT ENV_STEP   = 128.0 / ENV_LEN;
// envelope steps are 0.09375 dB (4x finer than the 0.375 dB TL levels);
// the envelope stops at -60 dB, matching recordings of the actual chip
const int MAX_ATT_INDEX = 0x280;
const int MIN_ATT_INDEX = 0;

// Envelope Generator phases
const int EG_ATT = 4;
const int EG_DEC = 3;
const int EG_SUS = 2;
const int EG_REL = 1;
const int EG_OFF = 0;

// these two only appear in old savestates (converted to EG_REL on load)
const int EG_REV = 5;	//pseudo reverb
const int EG_DMP = 6;	//damp

// Pan values, units are -3dB, i.e. 8.
const int pan_left[16]  = {
	0, 8, 16, 24, 32, 40, 48, 256, 256,   0,  0,  0,  0,  0,  0, 0
};
const int pan_right[16] = {
	0, 0,  0,  0,  0,  0,  0,   0, 256, 256, 48, 40, 32, 24, 16, 8
};

// Mixing levels, units are -3dB; setting 0 is 0 dB, setting 7 is mute.
const int mix_level[8] = {
	0, 8, 16, 24, 32, 40, 48, 256
};

// decay level table (3dB per step, 0x20 envelope steps per 3 dB)
// 0 - 15: 0, 3, 6, 9,12,15,18,21,24,27,30,33,36,39,42,93 (dB)
#define SC(db) (unsigned int)(db * 0x20)
const unsigned int dl_tab[16] = {
 SC( 0), SC( 1), SC( 2), SC(3 ), SC(4 ), SC(5 ), SC(6 ), SC( 7),
 SC( 8), SC( 9), SC(10), SC(11), SC(12), SC(13), SC(14), SC(31)
};
#undef SC

const byte RATE_STEPS = 8;
const byte eg_inc[15 * RATE_STEPS] = {
//cycle:0 1  2 3  4 5  6 7
	0, 1,  0, 1,  0, 1,  0, 1, //  0  rates 00..12 0 (increment by 0 or 1)
	0, 1,  0, 1,  1, 1,  0, 1, //  1  rates 00..12 1
	0, 1,  1, 1,  0, 1,  1, 1, //  2  rates 00..12 2
	0, 1,  1, 1,  1, 1,  1, 1, //  3  rates 00..12 3

	1, 1,  1, 1,  1, 1,  1, 1, //  4  rate 13 0 (increment by 1)
	1, 1,  1, 2,  1, 1,  1, 2, //  5  rate 13 1
	1, 2,  1, 2,  1, 2,  1, 2, //  6  rate 13 2
	1, 2,  2, 2,  1, 2,  2, 2, //  7  rate 13 3

	2, 2,  2, 2,  2, 2,  2, 2, //  8  rate 14 0 (increment by 2)
	2, 2,  2, 4,  2, 2,  2, 4, //  9  rate 14 1
	2, 4,  2, 4,  2, 4,  2, 4, // 10  rate 14 2
	2, 4,  4, 4,  2, 4,  4, 4, // 11  rate 14 3

	4, 4,  4, 4,  4, 4,  4, 4, // 12  rates 15 0, 15 1, 15 2, 15 3 for decay
	8, 8,  8, 8,  8, 8,  8, 8, // 13  rates 15 0, 15 1, 15 2, 15 3 for attack (zero time)
	0, 0,  0, 0,  0, 0,  0, 0, // 14  infinity rates for attack and decay(s)
};

#define O(a) (a * RATE_STEPS)
const byte eg_rate_select[64] = {
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 0),O( 1),O( 2),O( 3),
	O( 4),O( 5),O( 6),O( 7),
	O( 8),O( 9),O(10),O(11),
	O(12),O(12),O(12),O(12),
};
#undef O

//rate  0,    1,    2,    3,   4,   5,   6,  7,  8,  9,  10, 11, 12, 13, 14, 15
//shift 12,   11,   10,   9,   8,   7,   6,  5,  4,  3,  2,  1,  0,  0,  0,  0
//mask  4095, 2047, 1023, 511, 255, 127, 63, 31, 15, 7,  3,  1,  0,  0,  0,  0
#define O(a) (a)
const byte eg_rate_shift[64] = {
	O(12),O(12),O(12),O(12),
	O(11),O(11),O(11),O(11),
	O(10),O(10),O(10),O(10),
	O( 9),O( 9),O( 9),O( 9),
	O( 8),O( 8),O( 8),O( 8),
	O( 7),O( 7),O( 7),O( 7),
	O( 6),O( 6),O( 6),O( 6),
	O( 5),O( 5),O( 5),O( 5),
	O( 4),O( 4),O( 4),O( 4),
	O( 3),O( 3),O( 3),O( 3),
	O( 2),O( 2),O( 2),O( 2),
	O( 1),O( 1),O( 1),O( 1),
	O( 0),O( 0),O( 0),O( 0),
	O( 0),O( 0),O( 0),O( 0),
	O( 0),O( 0),O( 0),O( 0),
	O( 0),O( 0),O( 0),O( 0),
};
#undef O


//number of steps to take in quarter of lfo frequency
//TODO check if frequency matches real chip
#define O(a) ((int)((EG_TIMER_OVERFLOW / a) / 6))
const int lfo_period[8] = {
	O(0.168), O(2.019), O(3.196), O(4.206),
	O(5.215), O(5.888), O(6.224), O(7.066)
};
#undef O


#define O(a) ((int)(a * 65536))
const int vib_depth[8] = {
	O(0),	   O(3.378),  O(5.065),  O(6.750),
	O(10.114), O(20.170), O(40.106), O(79.307)
};
#undef O


#define SC(db) (unsigned int) (db * (2.0 / ENV_STEP))
const int am_depth[8] = {
	SC(0),	   SC(1.781), SC(2.906), SC(3.656),
	SC(4.406), SC(5.906), SC(7.406), SC(11.91)
};
#undef SC


YMF278Slot::YMF278Slot()
{
	reset();
}

void YMF278Slot::reset()
{
	wave = FN = OCT = PRVB = DAMP = LD = TL = TLdest = pan = lfo = vib = AM = 0;
	AR = D1R = DL = D2R = RC = RR = 0;
	step = stepptr = 0;
	bits = startaddr = loopaddr = endaddr = 0;
	env_vol = MAX_ATT_INDEX;
	//env_vol_step = env_vol_lim = 0;

	lfo_active = false;
	lfo_cnt = lfo_step = 0;
	lfo_max = lfo_period[0];

	state = EG_OFF;
	active = false;
}

int YMF278Slot::compute_rate(int val)
{
	if (val == 0) {
		return 0;
	} else if (val == 15) {
		return 63;
	}
	int res = val * 4;
	if (RC != 15) {
		int oct = OCT;
		if (oct & 8) {
			oct |= -8;
		}
		// the intermediate rate correction term is clamped to 0..15
		// before it is added (verified on hardware)
		int corr = oct + RC;
		if (corr < 0) {
			corr = 0;
		} else if (corr > 15) {
			corr = 15;
		}
		res += corr * 2 + (FN & 0x200 ? 1 : 0);
	}
	if (res > 63) {
		res = 63;
	}
	return res;
}

int YMF278Slot::compute_decay_rate(int val)
{
	if (DAMP) {
		// damping: 0 to -12 dB at rate 48, then rate 63 down to -96 dB;
		// rate correction is ignored (verified on hardware)
		return ((unsigned int)env_vol < dl_tab[4]) ? 48 : 63;
	}
	if (PRVB && ((unsigned int)env_vol >= dl_tab[6])) {
		// pseudo reverb from -18 dB on: D1R/D2R/RR are replaced by
		// reverb rate 5; rate correction is ignored (4 * 5 = 20)
		return 20;
	}
	return compute_rate(val);
}

int YMF278Slot::compute_vib()
{
	return (((lfo_step << 8) / lfo_max) * vib_depth[(int)vib]) >> 24;
}


int YMF278Slot::compute_am()
{
	if (lfo_active && AM) {
		return (((lfo_step << 8) / lfo_max) * am_depth[(int)AM]) >> 12;
	} else {
		return 0;
	}
}

void YMF278Slot::set_lfo(int newlfo)
{
	lfo_step = (((lfo_step << 8) / lfo_max) * newlfo) >> 8;
	lfo_cnt  = (((lfo_cnt  << 8) / lfo_max) * newlfo) >> 8;

	lfo = newlfo;
	lfo_max = lfo_period[(int)lfo];
}


void YMF278::advance()
{
	eg_timer += eg_timer_add;

    if (eg_timer > 4 * EG_TIMER_OVERFLOW) {
        eg_timer = EG_TIMER_OVERFLOW;
    }

	while (eg_timer >= EG_TIMER_OVERFLOW) {
		eg_timer -= EG_TIMER_OVERFLOW;
		eg_cnt++;

		// volume interpolation: one TL step down every 27 samples,
		// one TL step up every 13.5 samples (measured on hardware)
		unsigned int tl_int_cnt  = eg_cnt % 9;
		unsigned int tl_int_step = (eg_cnt / 9) % 3;

		for (int i = 0; i < 24; i++) {
			YMF278Slot &op = slots[i];

			if (tl_int_cnt == 0) {
				if (tl_int_step == 0) {
					if (op.TL < op.TLdest) ++op.TL;
				} else {
					if (op.TL > op.TLdest) --op.TL;
				}
			}

			if (op.lfo_active) {
				op.lfo_cnt++;
				if (op.lfo_cnt < op.lfo_max) {
					op.lfo_step++;
				} else if (op.lfo_cnt < (op.lfo_max * 3)) {
					op.lfo_step--;
				} else {
					op.lfo_step++;
					if (op.lfo_cnt == (op.lfo_max * 4)) {
						op.lfo_cnt = 0;
					}
				}
			}

			// Envelope Generator
			switch(op.state) {
			case EG_ATT: {	// attack phase
				byte rate = op.compute_rate(op.AR);
				if (rate < 4) {
					break;
				}
				// rate 15 is instant at key on (see keyOnHelper);
				// raising AR to 15 mid-attack freezes the envelope
				if (rate >= 63) {
					break;
				}
				byte shift = eg_rate_shift[rate];
				if (!(eg_cnt & ((1 << shift) -1))) {
					byte select = eg_rate_select[rate];
					// >>4 makes the attack shape match the actual chip
					op.env_vol += (~op.env_vol * eg_inc[select + ((eg_cnt >> shift) & 7)]) >> 4;
					if (op.env_vol <= MIN_ATT_INDEX) {
						op.env_vol = MIN_ATT_INDEX;
                        if (op.DL == 0) {
    						op.state = EG_SUS;
                        }
                        else {
    						op.state = EG_DEC;
                        }
					}
				}
				break;
			}
			case EG_DEC: {	// decay phase
				byte rate = op.compute_decay_rate(op.D1R);
				if (rate < 4) {
					break;
				}
				byte shift = eg_rate_shift[rate];
				if (!(eg_cnt & ((1 << shift) -1))) {
					byte select = eg_rate_select[rate];
					op.env_vol += eg_inc[select + ((eg_cnt >> shift) & 7)];

					if (op.env_vol >= op.DL) {
						if (op.env_vol < MAX_ATT_INDEX) {
							op.state = EG_SUS;
						} else {
							op.env_vol = MAX_ATT_INDEX;
							op.state = EG_OFF;
							op.active = false;
							checkMute();
						}
					}
				}
				break;
			}
			case EG_SUS: {	// sustain phase
				byte rate = op.compute_decay_rate(op.D2R);
				if (rate < 4) {
					break;
				}
				byte shift = eg_rate_shift[rate];
				if (!(eg_cnt & ((1 << shift) -1))) {
					byte select = eg_rate_select[rate];
					op.env_vol += eg_inc[select + ((eg_cnt >> shift) & 7)];

					if (op.env_vol >= MAX_ATT_INDEX) {
						op.env_vol = MAX_ATT_INDEX;
						op.state = EG_OFF;
						op.active = false;
						checkMute();
					}
				}
				break;
			}
			case EG_REL: {	// release phase
				byte rate = op.compute_decay_rate(op.RR);
				if (rate < 4) {
					break;
				}
				byte shift = eg_rate_shift[rate];
				if (!(eg_cnt & ((1 << shift) -1))) {
					byte select = eg_rate_select[rate];
					op.env_vol += eg_inc[select + ((eg_cnt >> shift) & 7)];

					if (op.env_vol >= MAX_ATT_INDEX) {
						op.env_vol = MAX_ATT_INDEX;
						op.state = EG_OFF;
						op.active = false;
						checkMute();
					}
				}
				break;
			}
			case EG_OFF:
				// nothing
				break;

			default:
				break;
			}
		}
	}
}

short YMF278::getSample(YMF278Slot &op)
{
	short sample;
	switch (op.bits) {
	case 0: {
		// 8 bit
		sample = readMem(op.startaddr + op.pos) << 8;
		break;
	}
	case 1: {
		// 12 bit
		int addr = op.startaddr + ((op.pos / 2) * 3);
		if (op.pos & 1) {
			sample = readMem(addr + 2) << 8 |
				 ((readMem(addr + 1) << 4) & 0xF0);
		} else {
			sample = readMem(addr + 0) << 8 |
				 (readMem(addr + 1) & 0xF0);
		}
		break;
	}
	case 2: {
		// 16 bit
		int addr = op.startaddr + (op.pos * 2);
		sample = (readMem(addr + 0) << 8) |
			 (readMem(addr + 1));
		break;
	}
	default:
		// TODO unspecified
		sample = 0;
	}
	return sample;
}

void YMF278::checkMute()
{
	setInternalMute(!anyActive());
}

bool YMF278::anyActive()
{
	for (int i = 0; i < 24; i++) {
		if (slots[i].active) {
			return true;
		}
	}
	return false;
}

int* YMF278::updateBuffer(int length)
{
	if (isInternalMuted()) {
		return NULL;
	}

	int vl = mix_level[pcm_l];
	int vr = mix_level[pcm_r];
	int *buf = buffer;
	while (length--) {
		int left = 0;
		int right = 0;
        int cnt = oplOversampling;
        while (cnt--) {
		    for (int i = 0; i < 24; i++) {
			    YMF278Slot &sl = slots[i];
			    if (!sl.active) {
				    continue;
			    }

			    short sample = (sl.sample1 * (0x10000 - sl.stepptr) +
			                    sl.sample2 * sl.stepptr) >> 16;
			    int vol = sl.TL + (sl.env_vol >> 2) + sl.compute_am();

			    int volLeft  = vol + pan_left [(int)sl.pan] + vl;
			    int volRight = vol + pan_right[(int)sl.pan] + vr;

			    // TODO prob doesn't happen in real chip
			    if (volLeft < 0) {
				    volLeft = 0;
			    }
			    if (volRight < 0) {
				    volRight = 0;
			    }

			    left  += (sample * volume[volLeft] ) >> 10;
			    right += (sample * volume[volRight]) >> 10;

			    if (sl.lfo_active && sl.vib) {
				    int oct = sl.OCT;
				    if (oct & 8) {
					    oct |= -8;
				    }
				    oct += 5;
				    sl.stepptr += (oct >= 0 ? ((sl.FN | 1024) + sl.compute_vib()) << oct
					               : ((sl.FN | 1024) + sl.compute_vib()) >> -oct) / oplOversampling;
			    } else {
				    sl.stepptr += sl.step / oplOversampling;
			    }

                int count = (sl.stepptr >> 16) & 0x0f;
                sl.stepptr &= 0xffff;
			    while (count--) {
				    sl.sample1 = sl.sample2;
				    sl.pos++;
				    if (sl.pos >= sl.endaddr) {
					    sl.pos = sl.loopaddr;
				    }
				    sl.sample2 = getSample(sl);
			    }
		    }
		    advance();
        }
		*buf++ = left / oplOversampling;
		*buf++ = right / oplOversampling;
	}
	return buffer;
}

void YMF278::keyOnHelper(YMF278Slot& slot)
{
	slot.active = true;
	setInternalMute(false);

	// the envelope level restarts from silence on every key on
	slot.env_vol = MAX_ATT_INDEX;
	if (slot.compute_rate(slot.AR) < 63) {
		slot.state = EG_ATT;
	} else {
		// attack rate 15 takes zero time
		slot.env_vol = MIN_ATT_INDEX;
		slot.state = slot.DL ? EG_DEC : EG_SUS;
	}

	int oct = slot.OCT;
	if (oct & 8) {
		oct |= -8;
	}
	oct += 5;
	slot.step = oct >= 0 ? (slot.FN | 1024) << oct : (slot.FN | 1024) >> -oct;
	slot.stepptr = 0;
	slot.pos = 0;
	slot.sample1 = getSample(slot);
	slot.pos = 1;
	slot.sample2 = getSample(slot);
}

void YMF278::writeRegOPL4(byte reg, byte data, const EmuTime &time)
{
	BUSY_Time = time + 88 * 6 / 9;

	// Handle slot registers specifically
	if (reg >= 0x08 && reg <= 0xF7) {
		int snum = (reg - 8) % 24;
		YMF278Slot& slot = slots[snum];
		switch ((reg - 8) / 24) {
		case 0: {
			LD_Time = time;
			slot.wave = (slot.wave & 0x100) | data;
			int base = (slot.wave < 384 || !wavetblhdr) ?
			           (slot.wave * 12) :
			           (wavetblhdr * 0x80000 + ((slot.wave - 384) * 12));
			byte buf[12];
			for (int i = 0; i < 12; i++) {
				buf[i] = readMem(base + i);
			}
			slot.bits = (buf[0] & 0xC0) >> 6;
			slot.set_lfo((buf[7] >> 3) & 7);
			slot.vib  = buf[7] & 7;
			slot.AR   = buf[8] >> 4;
			slot.D1R  = buf[8] & 0xF;
			slot.DL   = dl_tab[buf[9] >> 4];
			slot.D2R  = buf[9] & 0xF;
			slot.RC   = buf[10] >> 4;
			slot.RR   = buf[10] & 0xF;
			slot.AM   = buf[11] & 7;
			slot.startaddr = buf[2] | (buf[1] << 8) |
			                 ((buf[0] & 0x3F) << 16);
			slot.loopaddr = buf[4] + (buf[3] << 8);
			slot.endaddr  = (((buf[6] + (buf[5] << 8)) ^ 0xFFFF) + 1);
			// retrigger if KEY ON is set (register 4 rows up, not reg + 4);
			// otherwise only the sample position restarts
			if (regs[reg + 4 * 24] & 0x80) {
				keyOnHelper(slot);
			} else {
				slot.stepptr = 0;
				slot.pos = 0;
				slot.sample1 = getSample(slot);
				slot.pos = 1;
				slot.sample2 = getSample(slot);
			}
			break;
		}
		case 1: {
			slot.wave = (slot.wave & 0xFF) | ((data & 0x1) << 8);
			slot.FN = (slot.FN & 0x380) | (data >> 1);
			int oct = slot.OCT;
			if (oct & 8) {
				oct |= -8;
			}
	        oct += 5;
	        slot.step = oct >= 0 ? (slot.FN | 1024) << oct : (slot.FN | 1024) >> -oct;
			break;
		}
		case 2: {
			slot.FN = (slot.FN & 0x07F) | ((data & 0x07) << 7);
			slot.PRVB = ((data & 0x08) >> 3);
			slot.OCT =  ((data & 0xF0) >> 4);
			int oct = slot.OCT;
			if (oct & 8) {
				oct |= -8;
			}
	        oct += 5;
	        slot.step = oct >= 0 ? (slot.FN | 1024) << oct : (slot.FN | 1024) >> -oct;
            break;
		}
		case 3:
			// TL register value 0x7F maps to the internal level 0xFF
			// (near silence), verified on hardware
			slot.TLdest = ((data >> 1) != 0x7F) ? (data >> 1) : 0xFF;
			slot.LD = data & 0x1;
			if (slot.LD) {
				// level direct: change the volume immediately
				slot.TL = slot.TLdest;
			}
			// otherwise TL interpolates towards TLdest (see advance)
			break;
		case 4:
			if (data & 0x10) {
				// output to DO1 pin:
				// this pin is not used in moonsound
				// we emulate this by muting the sound
				slot.pan = 8; // both left/right -inf dB
			} else {
				slot.pan = data & 0x0F;
			}

			if (data & 0x020) {
				// LFO reset
				slot.lfo_active = false;
				slot.lfo_cnt = 0;
				slot.lfo_max = lfo_period[(int)slot.vib];
				slot.lfo_step = 0;
			} else {
				// LFO activate
				slot.lfo_active = true;
			}

			// damp is not a separate envelope state: it only makes the
			// decay rate computation use the damping rates
			slot.DAMP = (data & 0x40) ? 1 : 0;

			if (data & 0x80) {
				if (!(regs[reg] & 0x80)) {
					keyOnHelper(slot);
				}
			} else {
				if (regs[reg] & 0x80) {
					slot.state = EG_REL;
				}
			}
			break;
		case 5:
			slot.vib = data & 0x7;
			slot.set_lfo((data >> 3) & 0x7);
			break;
		case 6:
			slot.AR  = data >> 4;
			slot.D1R = data & 0xF;
			break;
		case 7:
			slot.DL  = dl_tab[data >> 4];
			slot.D2R = data & 0xF;
			break;
		case 8:
			slot.RC = data >> 4;
			slot.RR = data & 0xF;
			break;
		case 9:
			slot.AM = data & 0x7;
			break;
		}
	} else {
		// All non-slot registers
		switch (reg) {
		case 0x00:    	// TEST
		case 0x01:
			break;

		case 0x02:
			wavetblhdr = (data >> 2) & 0x7;
			memmode = data & 1;
			break;

		case 0x03:
			memadr = (memadr & 0x00FFFF) | (data << 16);
			break;

		case 0x04:
			memadr = (memadr & 0xFF00FF) | (data << 8);
			break;

		case 0x05:
			memadr = (memadr & 0xFFFF00) | data;
			break;

		case 0x06:  // memory data
			BUSY_Time += 28 * 6 / 9;
			writeMem(memadr, data);
			memadr = (memadr + 1) & 0xFFFFFF;
			break;

		case 0xF8:
			// MIX CONTROL (FM): the level is applied to the YMF262
			// output stage (see Moonsound.cpp / YMF262::setMixLevel).
			fm_l = data & 0x7;
			fm_r = (data >> 3) & 0x7;
			break;

		case 0xF9:
			pcm_l = data & 0x7;
			pcm_r = (data >> 3) & 0x7;
			break;
		}
	}

	regs[reg] = data;
}

byte YMF278::peekRegOPL4(byte reg, const EmuTime &time)
{
	BUSY_Time = time;

	byte result;
	switch(reg) {
		case 2: // 3 upper bits are device ID
			result = (regs[2] & 0x1F) | 0x20;
			break;

		case 6: // Memory Data Register
			result = readMem(memadr);
			break;

		default:
			result = regs[reg];
			break;
	}
	return result;
}

byte YMF278::readRegOPL4(byte reg, const EmuTime &time)
{
	BUSY_Time = time;

	byte result;
	switch(reg) {
		case 2: // 3 upper bits are device ID
			result = (regs[2] & 0x1F) | 0x20;
			break;

		case 6: // Memory Data Register
			BUSY_Time += 38 * 6 / 9;
			result = readMem(memadr);
			memadr = (memadr + 1) & 0xFFFFFF;
			break;

		default:
			result = regs[reg];
			break;
	}
	return result;
}

byte YMF278::peekStatus(const EmuTime &time)
{
	byte result = 0;
	if (time - BUSY_Time < 88 * 6 / 9) {
		result |= 0x01;
	}
	if (time - LD_Time < 10000 * 6 / 9) {
		result |= 0x02;
	}
	return result;
}

byte YMF278::readStatus(const EmuTime &time)
{
	byte result = 0;
	if (time - BUSY_Time < 88 * 6 / 9) {
		result |= 0x01;
	}
	if (time - LD_Time < 10000 * 6 / 9) {
		result |= 0x02;
	}
	return result;
}

YMF278::YMF278(short volume, int ramSize, void* romData, int romSize,
               const EmuTime &time)
{
    LD_Time = 0;
    BUSY_Time = 0;
	memadr = 0;	// avoid UMR
	endRom = romSize;
	ramSize *= 1024;	// in kb

    this->ramSize = ramSize;
	rom = (byte*)romData;
    ram = (byte*)calloc(1, ramSize);

    oplOversampling = 1;

	endRam = endRom + ramSize;

	reset(time);
}

YMF278::~YMF278()
{
	free(ram);
	free(rom);
}

void YMF278::reset(const EmuTime &time)
{
	eg_timer = 0;
	eg_cnt   = 0;

    int i;
	for (i = 0; i < 24; i++) {
		slots[i].reset();
	}
	for (i = 255; i >= 0; i--) { // reverse order to avoid UMR
		writeRegOPL4(i, 0, time);
	}
	setInternalMute(true);
	wavetblhdr = memmode = memadr = 0;
	pcm_l = pcm_r = 0;
	// The FM mix control resets to 0x1B (-9 dB); the level itself is
	// applied on the YMF262 side (see YMF262::reset).
	regs[0xF8] = 0x1B;
	fm_l = fm_r = 3;
	BUSY_Time = time;
	LD_Time = time;
}

void YMF278::setSampleRate(int sampleRate, int Oversampling)
{
    oplOversampling = Oversampling;
	eg_timer_add = (unsigned int)((1 << EG_SH) / oplOversampling);
}

void YMF278::setInternalVolume(short newVolume)
{
    newVolume /= 32;
	// Volume table, 1 = -0.375dB, 8 = -3dB, 256 = -96dB
    int i;
	for (i = 0; i < 256; i++) {
		volume[i] = (int)(4.0 * (DoubleT)newVolume * pow(2.0, (-0.375 / 6) * i));
	}
	for (i = 256; i < 256 * 4; i++) {
		volume[i] = 0;
	}
}

byte YMF278::readMem(unsigned int address)
{
	if (address < endRom) {
		return rom[address];
	} else if (address < endRam) {
		return ram[address - endRom];
	} else {
		return 255;	// TODO check
	}
}

void YMF278::writeMem(unsigned int address, byte value)
{
	if (address < endRom) {
		// can't write to ROM
	} else if (address < endRam) {
		ram[address - endRom] = value;
	} else {
		// can't write to unmapped memory
	}
}


//REMEMBER TO SAVE RAM
void YMF278::loadState()
{
    SaveState* state = saveStateOpenForRead("ymf278");

    ramSize           = saveStateGet(state, "ramSize",           0);
    eg_cnt            = saveStateGet(state, "eg_cnt",            0);
    eg_timer          = saveStateGet(state, "eg_timer",          0);
    eg_timer_add      = saveStateGet(state, "eg_timer_add",      0);
    eg_timer_overflow = saveStateGet(state, "eg_timer_overflow", 0);

    wavetblhdr        = (char)saveStateGet(state, "wavetblhdr",        0);
    memmode           = (char)saveStateGet(state, "memmode",           0);
    memadr            = saveStateGet(state, "memadr",            0);

    fm_l              = saveStateGet(state, "fm_l",              0);
    fm_r              = saveStateGet(state, "fm_r",              0);
    pcm_l             = saveStateGet(state, "pcm_l",             0);
    pcm_r             = saveStateGet(state, "pcm_r",             0);

    endRom            = saveStateGet(state, "endRom",            0);
    endRam            = saveStateGet(state, "endRam",            0);

    LD_Time           = saveStateGet(state, "LD_Time",           0);
    BUSY_Time         = saveStateGet(state, "BUSY_Time",         0);

    saveStateGetBuffer(state, "regs", regs, sizeof(regs));
    saveStateGetBuffer(state, "ram", ram, ramSize);

    for (int i = 0; i < 24; i++) {
        char tag[32];

        sprintf(tag, "wave%d", i);
        slots[i].wave = (short)saveStateGet(state, tag, 0);

        sprintf(tag, "FN%d", i);
        slots[i].FN = (short)saveStateGet(state, tag, 0);

        sprintf(tag, "OCT%d", i);
        slots[i].OCT = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "PRVB%d", i);
        slots[i].PRVB = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "DAMP%d", i);
        slots[i].DAMP = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "LD%d", i);
        slots[i].LD = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "TL%d", i);
        slots[i].TL = saveStateGet(state, tag, 0);

        sprintf(tag, "TLdest%d", i);
        slots[i].TLdest = saveStateGet(state, tag, slots[i].TL);

        sprintf(tag, "pan%d", i);
        slots[i].pan = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "lfo%d", i);
        slots[i].lfo = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "vib%d", i);
        slots[i].vib = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "AM%d", i);
        slots[i].AM = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "AR%d", i);
        slots[i].AR = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "D1R%d", i);
        slots[i].D1R = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "DL%d", i);
        slots[i].DL = saveStateGet(state, tag, 0);

        sprintf(tag, "D2R%d", i);
        slots[i].D2R = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "RC%d", i);
        slots[i].RC = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "RR%d", i);
        slots[i].RR = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "step%d", i);
        slots[i].step = saveStateGet(state, tag, 0);

        sprintf(tag, "stepptr%d", i);
        slots[i].stepptr = saveStateGet(state, tag, 0);

        sprintf(tag, "pos%d", i);
        slots[i].pos = saveStateGet(state, tag, 0);

        sprintf(tag, "sample1%d", i);
        slots[i].sample1 = (short)saveStateGet(state, tag, 0);

        sprintf(tag, "sample2%d", i);
        slots[i].sample2 = (short)saveStateGet(state, tag, 0);

        sprintf(tag, "active%d", i);
        slots[i].active = saveStateGet(state, tag, 0) != 0;

        sprintf(tag, "bits%d", i);
        slots[i].bits = (char)saveStateGet(state, tag, 0);

        sprintf(tag, "startaddr%d", i);
        slots[i].startaddr = saveStateGet(state, tag, 0);

        sprintf(tag, "loopaddr%d", i);
        slots[i].loopaddr = saveStateGet(state, tag, 0);

        sprintf(tag, "endaddr%d", i);
        slots[i].endaddr = saveStateGet(state, tag, 0);

        sprintf(tag, "state%d", i);
        slots[i].state = (char)saveStateGet(state, tag, 0);
        if (slots[i].state == EG_REV || slots[i].state == EG_DMP) {
            // legacy states from older savestates
            slots[i].state = EG_REL;
        }

        sprintf(tag, "env_vol%d", i);
        slots[i].env_vol = saveStateGet(state, tag, 0);

        sprintf(tag, "env_vol_step%d", i);
        slots[i].env_vol_step = saveStateGet(state, tag, 0);

        sprintf(tag, "env_vol_lim%d", i);
        slots[i].env_vol_lim = saveStateGet(state, tag, 0);

        sprintf(tag, "lfo_active%d", i);
        slots[i].lfo_active = saveStateGet(state, tag, 0) != 0;

        sprintf(tag, "lfo_cnt%d", i);
        slots[i].lfo_cnt = saveStateGet(state, tag, 0);

        sprintf(tag, "lfo_step%d", i);
        slots[i].lfo_step = saveStateGet(state, tag, 0);

        sprintf(tag, "lfo_max%d", i);
        slots[i].lfo_max = saveStateGet(state, tag, 0);
    }

    saveStateClose(state);
}

void YMF278::saveState()
{
    SaveState* state = saveStateOpenForWrite("ymf278");

    saveStateSet(state, "ramSize",           ramSize);
    saveStateSet(state, "eg_cnt",            eg_cnt);
    saveStateSet(state, "eg_timer",          eg_timer);
    saveStateSet(state, "eg_timer_add",      eg_timer_add);
    saveStateSet(state, "eg_timer_overflow", eg_timer_overflow);

    saveStateSet(state, "wavetblhdr",        wavetblhdr);
    saveStateSet(state, "memmode",           memmode);
    saveStateSet(state, "memadr",            memadr);

    saveStateSet(state, "fm_l",              fm_l);
    saveStateSet(state, "fm_r",              fm_r);
    saveStateSet(state, "pcm_l",             pcm_l);
    saveStateSet(state, "pcm_r",             pcm_r);

    saveStateSet(state, "endRom",            endRom);
    saveStateSet(state, "endRam",            endRam);

    saveStateSet(state, "LD_Time",           LD_Time);
    saveStateSet(state, "BUSY_Time",         BUSY_Time);

    saveStateSetBuffer(state, "regs", regs, sizeof(regs));
    saveStateSetBuffer(state, "ram", ram, ramSize);

    for (int i = 0; i < 24; i++) {
        char tag[32];

        sprintf(tag, "wave%d", i);
        saveStateSet(state, tag, slots[i].wave);

        sprintf(tag, "FN%d", i);
        saveStateSet(state, tag, slots[i].FN);

        sprintf(tag, "OCT%d", i);
        saveStateSet(state, tag, slots[i].OCT);

        sprintf(tag, "PRVB%d", i);
        saveStateSet(state, tag, slots[i].PRVB);

        sprintf(tag, "DAMP%d", i);
        saveStateSet(state, tag, slots[i].DAMP);

        sprintf(tag, "LD%d", i);
        saveStateSet(state, tag, slots[i].LD);

        sprintf(tag, "TL%d", i);
        saveStateSet(state, tag, slots[i].TL);

        sprintf(tag, "TLdest%d", i);
        saveStateSet(state, tag, slots[i].TLdest);

        sprintf(tag, "pan%d", i);
        saveStateSet(state, tag, slots[i].pan);

        sprintf(tag, "lfo%d", i);
        saveStateSet(state, tag, slots[i].lfo);

        sprintf(tag, "vib%d", i);
        saveStateSet(state, tag, slots[i].vib);

        sprintf(tag, "AM%d", i);
        saveStateSet(state, tag, slots[i].AM);

        sprintf(tag, "AR%d", i);
        saveStateSet(state, tag, slots[i].AR);

        sprintf(tag, "D1R%d", i);
        saveStateSet(state, tag, slots[i].D1R);

        sprintf(tag, "DL%d", i);
        saveStateSet(state, tag, slots[i].DL);

        sprintf(tag, "D2R%d", i);
        saveStateSet(state, tag, slots[i].D2R);

        sprintf(tag, "RC%d", i);
        saveStateSet(state, tag, slots[i].RC);

        sprintf(tag, "RR%d", i);
        saveStateSet(state, tag, slots[i].RR);

        sprintf(tag, "step%d", i);
        saveStateSet(state, tag, slots[i].step);

        sprintf(tag, "stepptr%d", i);
        saveStateSet(state, tag, slots[i].stepptr);

        sprintf(tag, "pos%d", i);
        saveStateSet(state, tag, slots[i].pos);

        sprintf(tag, "sample1%d", i);
        saveStateSet(state, tag, slots[i].sample1);

        sprintf(tag, "sample2%d", i);
        saveStateSet(state, tag, slots[i].sample2);

        sprintf(tag, "active%d", i);
        saveStateSet(state, tag, slots[i].active);

        sprintf(tag, "bits%d", i);
        saveStateSet(state, tag, slots[i].bits);

        sprintf(tag, "startaddr%d", i);
        saveStateSet(state, tag, slots[i].startaddr);

        sprintf(tag, "loopaddr%d", i);
        saveStateSet(state, tag, slots[i].loopaddr);

        sprintf(tag, "endaddr%d", i);
        saveStateSet(state, tag, slots[i].endaddr);

        sprintf(tag, "state%d", i);
        saveStateSet(state, tag, slots[i].state);

        sprintf(tag, "env_vol%d", i);
        saveStateSet(state, tag, slots[i].env_vol);

        sprintf(tag, "env_vol_step%d", i);
        saveStateSet(state, tag, slots[i].env_vol_step);

        sprintf(tag, "env_vol_lim%d", i);
        saveStateSet(state, tag, slots[i].env_vol_lim);

        sprintf(tag, "lfo_active%d", i);
        saveStateSet(state, tag, slots[i].lfo_active);

        sprintf(tag, "lfo_cnt%d", i);
        saveStateSet(state, tag, slots[i].lfo_cnt);

        sprintf(tag, "lfo_step%d", i);
        saveStateSet(state, tag, slots[i].lfo_step);

        sprintf(tag, "lfo_max%d", i);
        saveStateSet(state, tag, slots[i].lfo_max);
    }

    saveStateClose(state);
}
