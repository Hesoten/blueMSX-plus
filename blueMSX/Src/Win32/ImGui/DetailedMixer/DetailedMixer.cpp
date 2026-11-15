/*****************************************************************************
**
** Detailed mixer window: volume, mute, solo and meters per chip and voice.
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
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "imgui.h"
#include "ImGuiWindow.h"
#include "DetailedMixer.h"
#include "ToolText.h"
extern "C" {
#include "AudioMixer.h"
#include "Properties.h"
#include "Language.h"
}

#ifndef BLUEMSX_IMGUI_CONFIG
#error "Build ImGui files with IMGUI_USER_CONFIG set to imconfig_bluemsx.h"
#endif

/* Sizes are in pixels at 96 DPI and scaled when drawn. */
#define SEGS        18
#define TRACK_H     90.0f
#define STRIP_W     44.0f
#define STRIP_STEP  46.0f
#define ALL_W       72.0f
#define GROUP_GAP   10.0f
#define PANEL_GAP   5.5f
#define HEADER_H    27.0f
#define FOOTER_H    8.0f
#define TITLE_X     9.0f
#define TOPBAR_H    32.0f
#define TOP_BTN_H   24.0f
#define TOP_BTN_PAD 20.0f   /* added to the label's width */
#define MASTER_W    110.0f
#define FRAME       3.0f
#define MARGIN      10.0f
#define FOLD_W      26.0f

/* A strip from its top: name, colour bar, meter and fader, buttons. */
#define BAR_Y       21.0f
#define TRACK_Y     27.0f
#define BUTTON_Y    (TRACK_Y + TRACK_H + 6)
#define BUTTON_H    22.0f
#define STRIP_H     (BUTTON_Y + BUTTON_H)
#define STRIPS_Y    27.0f   /* below the group label */
#define WELL_H      (6 + STRIPS_Y + STRIP_H + 6)
#define PANEL_H     (HEADER_H + WELL_H + FOOTER_H)

/* Text sizes. */
#define F_TOP_BTN   16.0f
#define F_TITLE     20.0f
#define F_HEAD      16.0f
#define F_NAME      17.0f
#define F_BTN       16.0f
#define F_ALL_BTN   17.0f
#define F_MARK      15.0f

enum { CLASS_MAIN, CLASS_RHYTHM, CLASS_WAVE, CLASS_ADPCM };

struct Voice {
    char name[16];
    int  live;
    int  idle;   /* unused in the chip's current mode */
    int  volume, mute, solo;
    int  meter;
};

struct Group {
    const char* label;
    ImU32       color;
    int         from, to;
};

struct Chip {
    MixerInstanceInfo  info;
    char               title[64];
    char               shortTitle[32];   /* when only ALL shows */
    const char*        engine;
    ImU32              color;
    int                volume, mute, solo;
    int                meterL, meterR;
    int                pan;      /* 0..100 as heard */
    bool               shared;   /* another chip of the same type is present */
    bool               open;
    float              titleW, engineW;     /* header text, 96 DPI units */
    std::vector<Voice> voices;
    std::vector<Group> groups;
};

struct Piece {
    int  group;
    int  from, to;
    bool cont;
};

struct Seg {
    int                chip;
    bool               first;
    std::vector<Piece> pieces;
};

static float                S = 1.0f;
static std::vector<Chip>    chips;
static std::map<long, bool> folded;
static float                contW;

static inline float px(float v) { return v * S; }
template <class T> static inline T maxOf(T a, T b) { return a > b ? a : b; }
template <class T> static inline T minOf(T a, T b) { return a < b ? a : b; }

static long chipKey(const MixerInstanceInfo& i)
{
    return ((long)i.type << 24) | ((long)(i.slot & 0xff) << 16) | ((long)(i.sslot & 0xff) << 8) | (i.index & 0xff);
}

static ImU32 rgb(unsigned int hex, int alpha = 255)
{
    return IM_COL32((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff, alpha);
}

/* ---- names and colours ------------------------------------------------ */

static const char* typeName(int type)
{
    switch (type) {
    case MIXER_CHANNEL_PSG:        return "PSG";
    case MIXER_CHANNEL_SCC:        return "SCC";
    case MIXER_CHANNEL_MSXMUSIC:   return "MSX-MUSIC";
    case MIXER_CHANNEL_MSXAUDIO:   return "MSX-AUDIO";
    case MIXER_CHANNEL_MOONSOUND:  return "MoonSound";
    case MIXER_CHANNEL_YAMAHA_SFG: return "SFG";
    case MIXER_CHANNEL_KEYBOARD:   return "KEYCLICK";
    case MIXER_CHANNEL_PCM:        return "PCM";
    case MIXER_CHANNEL_IO:         return "I/O";
    case MIXER_CHANNEL_MIDI:       return "MIDI";
    case MIXER_CHANNEL_CASSETTE:   return "CAS";
    case MIXER_CHANNEL_OPNA_FM:    return "OPNA";
    case MIXER_CHANNEL_OPNA_SSG:   return "OPNA";
    case MIXER_CHANNEL_Y8960:      return "Y8960";
    }
    return "?";
}

/* For a panel showing only ALL, too narrow for the full name. */
static const char* shortName(int type)
{
    switch (type) {
    case MIXER_CHANNEL_MSXMUSIC:  return "MUSIC";
    case MIXER_CHANNEL_MSXAUDIO:  return "AUDIO";
    case MIXER_CHANNEL_MOONSOUND: return "MOON";
    }
    return typeName(type);
}

static unsigned int typeColor(int type)
{
    switch (type) {
    case MIXER_CHANNEL_PSG:        return 0x5fe39a;
    case MIXER_CHANNEL_SCC:        return 0x4cc9ff;
    case MIXER_CHANNEL_MSXMUSIC:   return 0xff8a65;
    case MIXER_CHANNEL_MSXAUDIO:   return 0xf2d05a;
    case MIXER_CHANNEL_MOONSOUND:  return 0xffb547;
    case MIXER_CHANNEL_YAMAHA_SFG: return 0x7fa8ff;
    case MIXER_CHANNEL_KEYBOARD:   return 0xc5ccd6;
    case MIXER_CHANNEL_PCM:        return 0x3fd8c8;
    case MIXER_CHANNEL_IO:         return 0x9fb4cc;
    case MIXER_CHANNEL_MIDI:       return 0xe08cff;
    case MIXER_CHANNEL_CASSETTE:   return 0xd9a066;
    case MIXER_CHANNEL_OPNA_FM:    return 0xff7a7a;
    case MIXER_CHANNEL_OPNA_SSG:   return typeColor(MIXER_CHANNEL_PSG);
    }
    return 0xc5ccd6;
}

/* A Y8960 block takes the colours and voice headings of the chip it stands in for. */
static int lookType(const MixerInstanceInfo& i)
{
    static const struct { const char* name; int type; } blocks[] = {
        { "SCC", MIXER_CHANNEL_SCC }, { "OPLL", MIXER_CHANNEL_MSXMUSIC }, { "OPL2", MIXER_CHANNEL_MSXAUDIO },
        { "SSG", MIXER_CHANNEL_PSG }, { "OPM", MIXER_CHANNEL_YAMAHA_SFG }
    };
    for (size_t k = 0; i.type == MIXER_CHANNEL_Y8960 && k < sizeof(blocks) / sizeof(blocks[0]); k++) {
        if (!strcmp(i.name, blocks[k].name)) {
            return blocks[k].type;
        }
    }
    return i.type;
}

static unsigned int chipColor(const MixerInstanceInfo& i)
{
    return typeColor(lookType(i));
}

static const char* engineName(int type)
{
    Properties* p = propGetGlobalProperties();

    if (p == NULL) {
        return "";
    }
    if (type == MIXER_CHANNEL_MSXMUSIC) {
        switch (p->sound.chip.ym2413BackendActive) {
        case PROP_YM2413_BACKEND_OPENMSX:   return "openMSX";
        case PROP_YM2413_BACKEND_OPENMSX_2: return "original blueMSX";
        case PROP_YM2413_BACKEND_EMU2413:   return "emu2413";
        case PROP_YM2413_BACKEND_NUKED:     return "Nuked OPLL";
        }
    }
    if (type == MIXER_CHANNEL_MSXAUDIO) {
        switch (p->sound.chip.y8950BackendActive) {
        case PROP_Y8950_BACKEND_FMOPL:   return "original blueMSX";
        case PROP_Y8950_BACKEND_EMU8950: return "emu8950";
        case PROP_Y8950_BACKEND_OPENMSX: return "openMSX";
        }
    }
    return "";
}

static int voiceClass(const char* n)
{
    if (!strcmp(n, "BD") || !strcmp(n, "HH") || !strcmp(n, "SD") || !strcmp(n, "TOM") || !strcmp(n, "CYM")) {
        return CLASS_RHYTHM;
    }
    if (n[0] == 'W' && n[1] >= '0' && n[1] <= '9') {
        return CLASS_WAVE;
    }
    if (!strcmp(n, "ADPCM")) {
        return CLASS_ADPCM;
    }
    return CLASS_MAIN;
}

/* The chip's own grouping where it gives one, else one read from the name. */
static int voiceClassOf(const MixerVoiceInfo& v)
{
    switch (v.group) {
    case MIXER_VOICE_RHYTHM: return CLASS_RHYTHM;
    case MIXER_VOICE_WAVE:   return CLASS_WAVE;
    case MIXER_VOICE_ADPCM:  return CLASS_ADPCM;
    case -1:                 return voiceClass(v.name);
    }
    return CLASS_MAIN;
}

static const char* className(int type, int cls)
{
    switch (cls) {
    case CLASS_RHYTHM: return "RHYTHM";
    case CLASS_WAVE:   return "WAVE";
    case CLASS_ADPCM:  return "ADPCM";
    }
    switch (type) {
    case MIXER_CHANNEL_PSG:      return "TONE";
    case MIXER_CHANNEL_OPNA_SSG: return "TONE";
    case MIXER_CHANNEL_SCC:      return "WAVETABLE";
    }
    return "FM";
}

static unsigned int classColor(int type, int cls)
{
    switch (cls) {
    case CLASS_RHYTHM: return 0xff6fb5;
    case CLASS_WAVE:   return 0xa990ff;
    case CLASS_ADPCM:  return 0x3fd8c8;
    }
    return typeColor(type);
}

/* ---- volume scale ----------------------------------------------------- */

/* The engine's curve: 100 is unity and each step is a third of a dB, with
** the bottom pulled to silence. */
static double volumeGain(int v)
{
    double bottom = pow(10.0, -100 / 60.0);
    return v >= 100 ? 1.0 : (pow(10.0, (v - 100) / 60.0) - bottom) / (1.0 - bottom);
}

static void volumeText(int v, char* buf, size_t size)
{
    double g = volumeGain(v);
    if (g <= 0.0) {
        snprintf(buf, size, "-inf");
    }
    else {
        snprintf(buf, size, "%.1f", 20.0 * log10(g));
    }
}

static int volumeForDb(double db)
{
    double bottom = pow(10.0, -100 / 60.0);
    return (int)floor(100 + 60 * log10(pow(10.0, db / 20.0) * (1.0 - bottom) + bottom) + 0.5);
}

/* ---- meter colours ---------------------------------------------------- */

struct Ramp {
    ImU32 edge[SEGS], core[SEGS], glow[SEGS], off[SEGS];
};

static ImU32 hsl(float h, float s, float l, int alpha = 255)
{
    float c = (1 - fabsf(2 * l - 1)) * s;
    float hp = fmodf(h, 360.0f) / 60.0f;
    float x = c * (1 - fabsf(fmodf(hp, 2.0f) - 1));
    float r = 0, g = 0, b = 0;
    if      (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else             { r = c; b = x; }
    float m = l - c / 2;
    return IM_COL32((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255), alpha);
}

static float hueOf(unsigned int hex)
{
    float r = ((hex >> 16) & 0xff) / 255.0f, g = ((hex >> 8) & 0xff) / 255.0f, b = (hex & 0xff) / 255.0f;
    float mx = maxOf(r, maxOf(g, b)), mn = minOf(r, minOf(g, b)), d = mx - mn, h = 0;
    if (d > 0) {
        h = mx == r ? fmodf((g - b) / d, 6.0f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    }
    return fmodf(h * 60 + 360, 360.0f);
}

/* The chip's hue, cooler and darker at the bottom and warmer and brighter
** at the top like the original skin's LED column; the top one warns red. */
static const Ramp& rampFor(unsigned int hex)
{
    static std::map<unsigned int, Ramp> cache;
    std::map<unsigned int, Ramp>::iterator it = cache.find(hex);
    if (it != cache.end()) {
        return it->second;
    }
    Ramp& r = cache[hex];
    float h = hueOf(hex);
    for (int k = 0; k < SEGS; k++) {
        float f = k / (float)(SEGS - 1);
        float hue = fmodf(h + 40 - 65 * f + 360, 360.0f);
        float l = 0.46f + 0.28f * f;
        if (k == SEGS - 1) {
            hue = 355;
            l = 0.66f;
        }
        r.edge[k] = hsl(hue, 0.9f, l - 0.10f);
        r.core[k] = hsl(hue, 1.0f, minOf(0.94f, l + 0.22f));
        r.glow[k] = hsl(hue, 1.0f, l, 70);
        r.off[k]  = hsl(hue, 0.28f, 0.17f + 0.05f * f);
    }
    return r;
}

static void drawMeter(ImDrawList* dl, ImVec2 p, float w, unsigned int color, int percent)
{
    const Ramp& r = rampFor(color);
    int lit = (percent * SEGS + 50) / 100;
    float step = px(TRACK_H) / SEGS;
    float h = step - px(1);

    for (int k = 0; k < SEGS; k++) {
        float y1 = p.y + px(TRACK_H) - k * step;
        ImVec2 a(p.x, y1 - h), b(p.x + w, y1);
        if (k < lit) {
            float mid = (a.x + b.x) / 2;
            dl->AddRectFilled(ImVec2(a.x - px(1), a.y - px(1)), ImVec2(b.x + px(1), b.y + px(1)), r.glow[k], px(2));
            dl->AddRectFilledMultiColor(a, ImVec2(mid, b.y), r.edge[k], r.core[k], r.core[k], r.edge[k]);
            dl->AddRectFilledMultiColor(ImVec2(mid, a.y), b, r.core[k], r.edge[k], r.edge[k], r.core[k]);
        }
        else {
            dl->AddRectFilled(a, b, r.off[k], px(1));
        }
    }
}

/* ---- gathering the chips ---------------------------------------------- */


/* The music chips in type order, then PCM and the machine's own noises. */
static int typeRank(int type)
{
    switch (type) {
    case MIXER_CHANNEL_PCM:      return MIXER_CHANNEL_TYPE_COUNT;
    case MIXER_CHANNEL_KEYBOARD: return MIXER_CHANNEL_TYPE_COUNT + 1;
    case MIXER_CHANNEL_IO:       return MIXER_CHANNEL_TYPE_COUNT + 2;
    case MIXER_CHANNEL_CASSETTE: return MIXER_CHANNEL_TYPE_COUNT + 3;
    default:                     return type;
    }
}

static bool chipBefore(const Chip& a, const Chip& b)
{
    if (a.info.type != b.info.type)   return typeRank(a.info.type) < typeRank(b.info.type);
    if (a.info.slot != b.info.slot)   return a.info.slot < b.info.slot;
    if (a.info.sslot != b.info.sslot) return a.info.sslot < b.info.sslot;
    return a.info.index < b.info.index;
}

static float textWidth(const char* s, float size);
static const char* foldLabel(const Chip& c, char* buf, size_t size);

static void gather(Mixer* mixer)
{
    MixerInstanceInfo info[MAX_CHANNELS];
    int count = mixerGetInstances(mixer, info, MAX_CHANNELS, NULL);

    contW = textWidth(ts(TS_MIXER_CONTINUED), F_TITLE);
    chips.clear();
    /* The slot only tells chips of one type apart, so it is left out of
    ** the title of a type that has just one. */
    int perType[MIXER_CHANNEL_TYPE_COUNT] = { 0 };
    for (int i = 0; i < count && i < MAX_CHANNELS; i++) {
        if (info[i].index >= 1 && info[i].type >= 0 && info[i].type < MIXER_CHANNEL_TYPE_COUNT) {
            perType[info[i].type]++;
        }
    }
    for (int i = 0; i < count && i < MAX_CHANNELS; i++) {
        if (info[i].index < 1) {
            continue;
        }
        Chip c;
        c.info   = info[i];
        c.engine = engineName(info[i].type);
        c.color  = chipColor(info[i]);
        c.shared = info[i].type >= 0 && info[i].type < MIXER_CHANNEL_TYPE_COUNT && perType[info[i].type] > 1;
        /* A chip named within its device needs the slot only beside another
        ** such device; the name tells it from the rest of its own. */
        const char* name = info[i].name;
        bool slotted = info[i].slot >= 0 && c.shared;
        if (name[0]) {
            slotted = false;
            for (int k = 0; k < count && k < MAX_CHANNELS; k++) {
                if (k != i && info[k].type == info[i].type && !strcmp(info[k].name, name)) {
                    slotted = info[i].slot >= 0;
                }
            }
        }
        char kind[48];
        snprintf(kind, sizeof(kind), name[0] ? "%s %s" : "%s", typeName(info[i].type), name);
        if (slotted) {
            snprintf(c.title, sizeof(c.title), "%s (Slot %d-%d)", kind, info[i].slot, info[i].sslot);
        }
        else {
            snprintf(c.title, sizeof(c.title), "%s", kind);
        }
        if (info[i].index > 1 && !name[0]) {
            size_t len = strlen(c.title);
            snprintf(c.title + len, sizeof(c.title) - len, " %d", info[i].index);
        }
        snprintf(c.shortTitle, sizeof(c.shortTitle), "%s", name[0] ? name : shortName(info[i].type));
        if (slotted) {
            size_t len = strlen(c.shortTitle);
            snprintf(c.shortTitle + len, sizeof(c.shortTitle) - len, " %d-%d", info[i].slot, info[i].sslot);
        }
        if (info[i].index > 1 && !name[0]) {
            size_t len = strlen(c.shortTitle);
            snprintf(c.shortTitle + len, sizeof(c.shortTitle) - len, " %d", info[i].index);
        }
        mixerGetInstanceSetting(mixer, info[i].type, info[i].slot, info[i].sslot, info[i].index, -1,
                                &c.volume, &c.mute, &c.solo);
        {
            Properties* prop = propGetGlobalProperties();
            c.pan = prop != NULL && info[i].type >= 0 && info[i].type < MIXER_CHANNEL_TYPE_COUNT ?
                    prop->sound.mixerChannel[info[i].type].pan : 50;
        }
        int l = mixerMeterGetInstancePercent(mixer, info[i].handle, 0);
        int r = mixerMeterGetInstancePercent(mixer, info[i].handle, 1);
        c.meterL = l;
        c.meterR = r;
        c.open  = !folded[chipKey(info[i])];

        MixerVoiceInfo vi[MIXER_MAX_VOICES];
        int n = mixerGetVoices(mixer, info[i].handle, vi, MIXER_MAX_VOICES);
        for (int v = 0; v < n; v++) {
            Voice voice;
            strncpy(voice.name, vi[v].name, sizeof(voice.name) - 1);
            voice.name[sizeof(voice.name) - 1] = 0;
            voice.live  = vi[v].live;
            voice.idle  = vi[v].idle;
            voice.meter = voice.live ? mixerMeterGetVoicePercent(mixer, info[i].handle, v) : 0;
            mixerGetInstanceSetting(mixer, info[i].type, info[i].slot, info[i].sslot, info[i].index, v,
                                    &voice.volume, &voice.mute, &voice.solo);
            int cls = voiceClassOf(vi[v]);
            if (c.groups.empty() || voiceClassOf(vi[c.groups.back().from]) != cls) {
                Group g;
                g.label = className(lookType(info[i]), cls);
                g.color = classColor(lookType(info[i]), cls);
                g.from  = v;
                g.to    = v + 1;
                c.groups.push_back(g);
            }
            else {
                c.groups.back().to = v + 1;
            }
            c.voices.push_back(voice);
        }
        c.titleW  = textWidth(c.title, F_TITLE);
        c.engineW = textWidth(c.engine, F_HEAD);
        chips.push_back(c);
    }
    /* By type (see typeRank), then by slot. */
    std::stable_sort(chips.begin(), chips.end(), chipBefore);
}

static void setChip(Mixer* mixer, const Chip& c, int volume, int mute, int solo)
{
    mixerSetInstance(mixer, c.info.type, c.info.slot, c.info.sslot, c.info.index, volume, mute, solo);
}

static void setVoice(Mixer* mixer, const Chip& c, int v, int volume, int mute, int solo)
{
    mixerSetVoice(mixer, c.info.type, c.info.slot, c.info.sslot, c.info.index, v, volume, mute, solo);
}

/* The Sound Mixer's mute for a whole type, saved with it. */
static bool typeMuted(int type)
{
    Properties* p = propGetGlobalProperties();
    return p != NULL && type >= 0 && type < MIXER_CHANNEL_TYPE_COUNT && !p->sound.mixerChannel[type].enable;
}

static void setTypeMute(Mixer* mixer, int type, bool mute)
{
    Properties* p = propGetGlobalProperties();
    if (p != NULL && type >= 0 && type < MIXER_CHANNEL_TYPE_COUNT) {
        p->sound.mixerChannel[type].enable = !mute;
        mixerEnableChannelType(mixer, type, !mute);
    }
}

/* A chip's pan is the Sound Mixer's for its type, saved with it. */
static void setTypePan(Mixer* mixer, int type, int pan)
{
    Properties* p = propGetGlobalProperties();
    if (p != NULL && type >= 0 && type < MIXER_CHANNEL_TYPE_COUNT) {
        p->sound.mixerChannel[type].pan = pan;
        mixerSetChannelTypePan(mixer, type, pan);
    }
}

static bool chipMuted(const Chip& c)
{
    return c.mute || typeMuted(c.info.type);
}

/* The only chip of its type mutes the type, as the Sound Mixer does. One of
** several mutes itself for this run only; unmuting it while the whole type
** is muted keeps the others of the type silent. */
static void toggleChipMute(Mixer* mixer, Chip& c)
{
    if (!chipMuted(c)) {
        if (c.shared) {
            setChip(mixer, c, c.volume, 1, c.solo);
            c.mute = 1;
        }
        else {
            setTypeMute(mixer, c.info.type, true);
        }
        return;
    }
    if (typeMuted(c.info.type)) {
        /* Their solos go too, voices included: a muted solo would still
        ** silence this one. */
        for (size_t i = 0; i < chips.size(); i++) {
            Chip& o = chips[i];
            if (&o == &c || o.info.type != c.info.type) {
                continue;
            }
            for (size_t v = 0; v < o.voices.size(); v++) {
                if (o.voices[v].solo) {
                    setVoice(mixer, o, (int)v, o.voices[v].volume, o.voices[v].mute, 0);
                    o.voices[v].solo = 0;
                }
            }
            if (!o.mute || o.solo) {
                setChip(mixer, o, o.volume, 1, 0);
                o.mute = 1;
                o.solo = 0;
            }
        }
        setTypeMute(mixer, c.info.type, false);
    }
    if (c.mute) {
        setChip(mixer, c, c.volume, 0, c.solo);
        c.mute = 0;
    }
}

/* ---- row layout ------------------------------------------------------- */

static float textWidth(const char* s, float size)
{
    /* Measured at the size it is drawn, in 96 DPI units. */
    return ImGui::GetFont()->CalcTextSizeA(px(size), FLT_MAX, 0, s).x / S;
}

static const char* foldLabel(const Chip& c, char* buf, size_t size)
{
    if (c.open) {
        snprintf(buf, size, "%s", ts(TS_MIXER_COLLAPSE_TIP));
    }
    else {
        snprintf(buf, size, ts(TS_MIXER_EXPAND_TIP), (int)c.voices.size());
    }
    return buf;
}

/* Shrink a title to fit, down to three quarters, then cut it with an
** ellipsis. Returns the size to draw it at. */
static float fitText(char* buf, size_t size, float fontSize, float room)
{
    static const char ellipsis[] = { (char)0xe2, (char)0x80, (char)0xa6, 0 };   /* U+2026 */
    float w = textWidth(buf, fontSize);
    if (w <= room) {
        return fontSize;
    }
    float least = fontSize * 0.75f;
    if (fontSize * room / w >= least) {
        return fontSize * room / w;
    }
    size_t len = strlen(buf);
    while (len > 0) {
        /* Step back over a whole UTF-8 character. */
        do { len--; } while (len > 0 && ((unsigned char)buf[len] & 0xc0) == 0x80);
        if (len + 4 > size) {
            continue;
        }
        memcpy(buf + len, ellipsis, sizeof(ellipsis));
        if (textWidth(buf, least) <= room) {
            break;
        }
    }
    return least;
}

static float pieceWidth(const Piece& p)
{
    return STRIP_STEP * (p.to - p.from) + 4;
}

static float segWidth(const Seg& s)
{
    const Chip& c = chips[s.chip];
    float w = 26 + (s.first ? ALL_W : 0) + (s.first && !s.pieces.empty() ? 8 : 0);
    for (size_t i = 0; i < s.pieces.size(); i++) {
        w += pieceWidth(s.pieces[i]) + (i ? GROUP_GAP : 0);
    }
    /* A panel showing only ALL is always that narrow; its title shrinks. */
    if (s.first && s.pieces.empty()) {
        return w;
    }
    float head = TITLE_X + 9 + c.titleW + (s.first ? 0 : contW);
    if (s.first) {
        head += 8 + c.engineW;
        if (!c.voices.empty()) {
            head += 8 + FOLD_W + 7;
        }
    }
    return maxOf(w, head);
}

static Seg headSeg(int chip, bool first)
{
    Seg s;
    s.chip  = chip;
    s.first = first;
    return s;
}

static Seg wholeSeg(int chip)
{
    Seg s = headSeg(chip, true);
    if (chips[chip].open) {
        for (size_t g = 0; g < chips[chip].groups.size(); g++) {
            Piece p = { (int)g, chips[chip].groups[g].from, chips[chip].groups[g].to, false };
            s.pieces.push_back(p);
        }
    }
    return s;
}

/* The least of a group that may stand alone: all of a group of up to
** eight voices, otherwise four. */
static Piece minPiece(const Piece& g, int from, bool cont)
{
    Piece p = { g.group, from, g.to, cont };
    if (g.to - g.from > 8) {
        p.to = minOf(g.to, from + 4);
    }
    return p;
}

/* Narrowest a row can be without a panel sticking out of it: a later part
** must hold eight voices, the most a long group can leave after a cut. */
static float minRowWidth()
{
    float m = 0;
    for (int i = 0; i < (int)chips.size(); i++) {
        Seg whole = wholeSeg(i);
        if (whole.pieces.empty()) {
            m = maxOf(m, segWidth(whole));
            continue;
        }
        Seg first = headSeg(i, true);
        first.pieces.push_back(minPiece(whole.pieces[0], whole.pieces[0].from, false));
        m = maxOf(m, segWidth(first));
        for (size_t g = 0; g < whole.pieces.size(); g++) {
            Seg cont = headSeg(i, false);
            Piece p = whole.pieces[g];
            p.to   = minOf(p.to, p.from + 8);
            p.cont = true;
            cont.pieces.push_back(p);
            m = maxOf(m, segWidth(cont));
        }
    }
    return m;
}

typedef std::vector<std::vector<Seg> > Rows;

static void put(Rows& rows, float& w, const Seg& s)
{
    if (!rows.back().empty()) w += PANEL_GAP;
    rows.back().push_back(s);
    w += segWidth(s);
}

static void newRow(Rows& rows, float& w)
{
    if (!rows.back().empty()) {
        rows.push_back(std::vector<Seg>());
        w = 0;
    }
}

static float room(const Rows& rows, float w, float t)
{
    return rows.back().empty() ? t : t - w - PANEL_GAP;
}

/* Fill rows up to width t. A chip wider than a row is cut between its
** groups, and a group of more than eight voices into runs of four or more;
** eager starts it in what is left of a row. Returns the number of cuts. */
static int layout(float t, bool eager, Rows& rows, float& widest)
{
    float w = 0;
    rows.assign(1, std::vector<Seg>());

    for (int ci = 0; ci < (int)chips.size(); ci++) {
        Seg whole = wholeSeg(ci);
        float full = segWidth(whole);
        if (full <= room(rows, w, t)) { put(rows, w, whole); continue; }
        if (full <= t || whole.pieces.empty()) { newRow(rows, w); put(rows, w, whole); continue; }

        Seg cur = headSeg(ci, true);
        cur.pieces.push_back(minPiece(whole.pieces[0], whole.pieces[0].from, false));
        if (!eager || segWidth(cur) > room(rows, w, t)) newRow(rows, w);
        cur.pieces.clear();

        for (size_t gi = 0; gi < whole.pieces.size(); gi++) {
            const Piece& g = whole.pieces[gi];
            int off = g.from, guard = 0;
            while (off < g.to && guard++ < 100) {
                Piece p = { g.group, off, g.to, off != g.from };
                cur.pieces.push_back(p);
                if (segWidth(cur) <= room(rows, w, t)) { off = g.to; continue; }
                int rest = g.to - off, k = 0;
                if (rest > 8) {
                    for (k = rest - 4; k >= 4; k--) {
                        cur.pieces.back().to = off + k;
                        if (segWidth(cur) <= room(rows, w, t)) break;
                    }
                    if (k < 4) k = 0;
                }
                if (k == 0 && rows.back().empty() && !cur.first && cur.pieces.size() == 1) {
                    k = rest;   /* nothing fits an empty row: let it stick out */
                }
                if (k > 0) { cur.pieces.back().to = off + k; off += k; }
                else cur.pieces.pop_back();
                if (!cur.pieces.empty() || cur.first) put(rows, w, cur);
                newRow(rows, w);
                cur = headSeg(ci, false);
            }
        }
        if (!cur.pieces.empty()) put(rows, w, cur);
    }
    if (rows.back().empty() && rows.size() > 1) rows.pop_back();

    int cuts = -(int)chips.size();
    widest = 0;
    for (size_t r = 0; r < rows.size(); r++) {
        float rw = 0;
        for (size_t i = 0; i < rows[r].size(); i++) {
            rw += segWidth(rows[r][i]) + (i ? PANEL_GAP : 0);
            cuts++;
            for (size_t k = 0; k < rows[r][i].pieces.size(); k++) cuts += rows[r][i].pieces[k].cont;
        }
        widest = maxOf(widest, rw);
    }
    return cuts;
}

/* The fewest rows, then the fewest cuts, then the narrowest width, so the
** rows come out about the same length. Kept until the chips or the width
** change. Returns the widest row. */
static float balance(float cap, Rows& rows)
{
    static Rows        keptRows;
    static float       keptWidest;
    static std::string keptKey;

    char buf[96];
    std::string key;
    snprintf(buf, sizeof(buf), "%.0f %.3f|", cap, S);
    key = buf;
    for (size_t i = 0; i < chips.size(); i++) {
        snprintf(buf, sizeof(buf), "%ld %d %d %s|", chipKey(chips[i].info), (int)chips[i].open,
                 (int)chips[i].voices.size(), chips[i].engine);
        key += buf;
    }
    if (key == keptKey) {
        rows = keptRows;
        return keptWidest;
    }

    float least = minRowWidth(), widest, bestWidest = 0;
    int bestOver = 0, bestRows = 0, bestCuts = 0;
    cap = maxOf(cap, least);
    for (int eager = 0; eager < 2; eager++) {
        for (float t = least; t < cap + 6; t += 6) {
            Rows r;
            int cuts = layout(minOf(t, cap), eager != 0, r, widest);
            int over = widest > cap, n = (int)r.size();
            bool better;
            if (!bestRows)              better = true;
            else if (over != bestOver)  better = over < bestOver;
            else if (n != bestRows)     better = n < bestRows;
            else if (cuts != bestCuts)  better = cuts < bestCuts;
            else                        better = widest < bestWidest;
            if (better) {
                bestOver   = over;
                bestRows   = n;
                bestCuts   = cuts;
                bestWidest = widest;
                rows = r;
            }
        }
    }
    keptKey    = key;
    keptRows   = rows;
    keptWidest = bestWidest;
    return bestWidest;
}

/* ---- drawing helpers -------------------------------------------------- */

static void text(ImDrawList* dl, ImVec2 p, float size, ImU32 col, const char* s)
{
    dl->AddText(ImGui::GetFont(), px(size), p, col, s);
}

static void textCentered(ImDrawList* dl, float cx, float y, float size, ImU32 col, const char* s)
{
    text(dl, ImVec2(cx - px(textWidth(s, size)) / 2, y), size, col, s);
}

static void panelBody(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    dl->AddRectFilled(ImVec2(a.x, a.y + px(2)), ImVec2(b.x, b.y + px(2)), IM_COL32(0, 0, 0, 90), px(6));
    dl->AddRectFilled(a, b, rgb(0x444c56), px(6));
    dl->AddRectFilledMultiColor(ImVec2(a.x + px(3), a.y + px(1)), ImVec2(b.x - px(3), a.y + px(70)),
                                rgb(0x6a7380), rgb(0x6a7380), rgb(0x444c56), rgb(0x444c56));
    dl->AddRect(a, b, rgb(0x22272c), px(6));
    dl->AddLine(ImVec2(a.x + px(6), a.y + px(1)), ImVec2(b.x - px(6), a.y + px(1)), IM_COL32(255, 255, 255, 56));
}

static void well(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    dl->AddRectFilled(a, ImVec2(b.x, b.y + px(1)), IM_COL32(255, 255, 255, 30), px(6));
    dl->AddRectFilled(a, b, rgb(0x23272c), px(6));
    dl->AddRectFilledMultiColor(ImVec2(a.x + px(3), a.y), ImVec2(b.x - px(3), a.y + px(4)),
                                IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
}

/* A push button drawn in the skin's style; returns true when clicked. A dim
** button is on but overridden by something else. */
static bool button(ImDrawList* dl, const char* id, const char* label, ImVec2 p, ImVec2 size, float fontSize,
                   bool on, unsigned int onBg, unsigned int onFg, bool enabled = true, bool dim = false)
{
    ImGui::SetCursorScreenPos(p);
    bool clicked = ImGui::InvisibleButton(id, size) && enabled;
    bool hover = enabled && ImGui::IsItemHovered();
    ImVec2 b(p.x + size.x, p.y + size.y);
    if (on) {
        dl->AddRectFilled(p, b, rgb(onBg, dim ? 90 : 255), px(3));
    }
    else {
        unsigned int top = hover ? 0x59626d : 0x4a525c, bottom = hover ? 0x474f59 : 0x3a4048;
        dl->AddRectFilledMultiColor(ImVec2(p.x + px(1), p.y + px(1)), ImVec2(b.x - px(1), b.y - px(1)),
                                    rgb(top), rgb(top), rgb(bottom), rgb(bottom));
    }
    dl->AddRect(p, b, on ? IM_COL32(0, 0, 0, 90) : rgb(0x22272c), px(3));
    textCentered(dl, (p.x + b.x) / 2, p.y + (size.y - px(fontSize)) / 2 - px(1), fontSize,
                 on && !dim ? rgb(onFg) : rgb(0xd5dbe2), label);
    return clicked;
}

/* A tooltip beside a control rather than under the pointer, which hides it.
** Left of the control first, away from the arrow that hangs down-right of
** the pointer; right of it when there is no room; always inside the window. */
static void tipBeside(float left, float right, float y, const char* text)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 pad = ImGui::GetStyle().WindowPadding;
    ImVec2 size = ImGui::CalcTextSize(text);
    float w = size.x + pad.x * 2, h = size.y + pad.y * 2;
    float x = left - px(8) - w;
    if (x < vp->Pos.x) {
        x = right + px(8);
    }
    x = maxOf(vp->Pos.x, minOf(x, vp->Pos.x + vp->Size.x - w));
    float top = maxOf(vp->Pos.y, minOf(y - h / 2, vp->Pos.y + vp->Size.y - h));
    ImGui::SetNextWindowPos(ImVec2(x, top), ImGuiCond_Always);
    ImGui::SetTooltip("%s", text);
}

/* A vertical fader over 0..100 of height h, left out when shorter than two
** thumbs; a double click sets reset. Returns true when the value changed. */
static bool fader(ImDrawList* dl, const char* id, ImVec2 p, float w, float h, float thumbH, int* value, bool enabled,
                  int reset = 100, const char* note = NULL)
{
    if (h < thumbH * 2) {
        return false;
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton(id, ImVec2(w, h));
    /* After a double click resets the fader, the held button must not drag. */
    static ImGuiID resetHeld = 0;
    ImGuiID item = ImGui::GetItemID();
    int old = *value;
    if (resetHeld == item && !ImGui::IsItemActive()) {
        resetHeld = 0;
    }
    if (enabled) {
        bool ownWheel = ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (ImGui::IsItemActive() && resetHeld != item) {
            float y = ImGui::GetIO().MousePos.y - p.y - thumbH / 2;
            *value = (int)floorf(100.0f * (1.0f - y / (h - thumbH)) + 0.5f);
        }
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                *value = reset;
                resetHeld = item;
            }
            float wheel = ImGui::GetIO().MouseWheel;
            if (ownWheel && wheel != 0) {
                *value += wheel > 0 ? 3 : -3;
            }
        }
        *value = maxOf(0, minOf(100, *value));
    }
    float cx = p.x + w / 2;
    dl->AddRectFilled(ImVec2(cx - px(2), p.y), ImVec2(cx + px(2), p.y + h), rgb(0x0e1012), px(2));
    dl->AddRect(ImVec2(cx - px(2), p.y), ImVec2(cx + px(2), p.y + h), rgb(0x3a4048), px(2));
    float ty = p.y + (1.0f - *value / 100.0f) * (h - thumbH);
    dl->AddRectFilled(ImVec2(p.x, ty + px(1)), ImVec2(p.x + w, ty + thumbH + px(2)), IM_COL32(0, 0, 0, 160), px(2));
    dl->AddRectFilled(ImVec2(p.x, ty), ImVec2(p.x + w, ty + thumbH), rgb(0xb4bcc6), px(2));
    for (float ly = ty + px(1); ly < ty + thumbH - px(1); ly += px(3)) {
        dl->AddRectFilled(ImVec2(p.x + px(1), ly), ImVec2(p.x + w - px(1), ly + px(2)), rgb(0xf1f4f7));
    }
    dl->AddRect(ImVec2(p.x, ty), ImVec2(p.x + w, ty + thumbH), rgb(0x6c7480), px(2));
    if (enabled && (ImGui::IsItemHovered() || ImGui::IsItemActive())) {
        char db[16], tip[160];
        volumeText(*value, db, sizeof(db));
        snprintf(tip, sizeof(tip), note != NULL ? "%s dB\n%s" : "%s dB", db, note);
        tipBeside(p.x, p.x + w, ty + thumbH / 2, tip);
    }
    return *value != old;
}

/* A pan knob over 0..100, 50 in the middle; drag sideways or up and down,
** or use the wheel. Returns 1 when turned and 2 on a double click. */
static int knob(ImDrawList* dl, const char* id, ImVec2 c, float r, int* value, ImU32 mark)
{
    static float dragged;
    ImGuiIO& io = ImGui::GetIO();
    int result = 0;
    int old = *value;

    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    ImGui::InvisibleButton(id, ImVec2(2 * r, 2 * r));
    bool ownWheel = ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    /* After a double click resets the knob, the held button must not turn
    ** it back. */
    static ImGuiID resetHeld = 0;
    ImGuiID item = ImGui::GetItemID();
    if (resetHeld == item && !ImGui::IsItemActive()) {
        resetHeld = 0;
    }
    if (ImGui::IsItemActivated()) {
        dragged = (float)*value;
    }
    if (ImGui::IsItemActive() && resetHeld != item) {
        dragged += (io.MouseDelta.x - io.MouseDelta.y) / S * 0.6f;
        dragged = maxOf(0.0f, minOf(100.0f, dragged));
        *value = (int)floorf(dragged + 0.5f);
    }
    if (ImGui::IsItemHovered()) {
        if (ownWheel && io.MouseWheel != 0) {
            *value = maxOf(0, minOf(100, *value + (io.MouseWheel > 0 ? 2 : -2)));
        }
        if (ImGui::IsMouseDoubleClicked(0)) {
            result = 2;
            resetHeld = item;
        }
    }
    if (result == 0 && *value != old) {
        result = 1;
    }

    dl->AddCircleFilled(ImVec2(c.x, c.y + px(1)), r, IM_COL32(0, 0, 0, 110));
    dl->AddCircleFilled(c, r, rgb(0x59626d));
    dl->AddCircleFilled(c, r - px(2), rgb(0x3a4048));
    dl->AddCircle(c, r, rgb(0x22272c), 0, px(1));
    float ang = (-135.0f + 270.0f * *value / 100.0f) * 3.14159265f / 180.0f;
    ImVec2 tip(c.x + sinf(ang) * (r - px(2)), c.y - cosf(ang) * (r - px(2)));
    dl->AddLine(ImVec2(c.x + sinf(ang) * px(2), c.y - cosf(ang) * px(2)), tip, mark, px(2.5f));
    return result;
}

static void panText(int pan, char* buf, size_t size)
{
    if (pan == 50) {
        snprintf(buf, size, "PAN C");
    }
    else {
        snprintf(buf, size, "PAN %c%d", pan < 50 ? 'L' : 'R', (pan < 50 ? 50 - pan : pan - 50) * 2);
    }
}

/* ---- strips ----------------------------------------------------------- */

static void voiceStrip(Mixer* mixer, ImDrawList* dl, Chip& c, int v, unsigned int color, ImVec2 p)
{
    Voice& voice = c.voices[v];
    bool dead = !voice.live;
    /* Idle voices are dimmed but stay adjustable: games switch the mode
    ** often, and the setting must hold when the voice comes back. */
    bool idle = !dead && voice.idle;
    int alpha = dead ? 80 : idle ? 120 : 255;
    float cx = p.x + px(STRIP_W) / 2;
    char id[16];

    ImGui::PushID(v);
    /* A long name such as ADPCM shrinks to the strip. */
    float nameSize = F_NAME, nameW = textWidth(voice.name, F_NAME);
    if (nameW > STRIP_W - 2) {
        nameSize = F_NAME * (STRIP_W - 2) / nameW;
    }
    textCentered(dl, cx, p.y + px(1) + px(F_NAME - nameSize) / 2, nameSize, rgb(0xe8ecf1, alpha), voice.name);
    dl->AddRectFilled(ImVec2(cx - px(16), p.y + px(BAR_Y)), ImVec2(cx + px(16), p.y + px(BAR_Y + 2)), rgb(color, alpha), px(1));

    ImVec2 track(p.x + px(7), p.y + px(TRACK_Y));
    drawMeter(dl, track, px(8), color, voice.meter);
    if (dead || idle) {
        dl->AddRectFilled(track, ImVec2(track.x + px(8), track.y + px(TRACK_H)), IM_COL32(35, 39, 44, 170));
    }
    int vol = voice.volume;
    if (fader(dl, "f", ImVec2(p.x + px(21), track.y), px(16), px(TRACK_H), px(11), &vol, !dead, 100,
              idle ? ts(TS_MIXER_VOICE_IDLE_TIP) : NULL)) {
        setVoice(mixer, c, v, vol, voice.mute, voice.solo);
        voice.volume = vol;
    }
    if (dead && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", ts(TS_MIXER_NO_VOICE_OUTPUT_TIP));
    }

    snprintf(id, sizeof(id), "m");
    if (button(dl, id, "M", ImVec2(p.x + px(2), p.y + px(BUTTON_Y)), ImVec2(px(19), px(BUTTON_H)), F_BTN,
               voice.mute != 0, 0xff5f6d, 0xffffff, !dead || voice.mute)) {
        setVoice(mixer, c, v, voice.volume, !voice.mute, voice.solo);
    }
    /* A solo on the whole chip already lets every voice through. */
    bool overridden = voice.solo && c.solo;
    snprintf(id, sizeof(id), "s");
    if (button(dl, id, "S", ImVec2(p.x + px(23), p.y + px(BUTTON_Y)), ImVec2(px(19), px(BUTTON_H)), F_BTN,
               voice.solo != 0, 0xffd34e, 0x2a2100, !dead || voice.solo, overridden)) {
        setVoice(mixer, c, v, voice.volume, voice.mute, !voice.solo);
    }
    if (overridden && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", ts(TS_MIXER_SOLO_OVERRIDDEN_TIP));
    }
    ImGui::PopID();
}

static void allStrip(Mixer* mixer, ImDrawList* dl, Chip& c, ImVec2 a)
{
    ImVec2 p(a.x, a.y + px(STRIPS_Y));
    ImVec2 b(a.x + px(ALL_W), p.y + px(STRIP_H + 4));
    float cx = (a.x + b.x) / 2;

    dl->AddRectFilled(a, b, rgb(0x2c3137), px(5));
    dl->AddRect(a, b, rgb(0x3d444d), px(5));
    /* Pan above, where the voice strips have their group label. */
    {
        ImVec2 kc(cx, a.y + px(15));
        float r = px(11);
        text(dl, ImVec2(kc.x - r - px(5) - px(textWidth("L", F_MARK)), kc.y - px(F_MARK / 2) - px(1)), F_MARK, rgb(0x8a939e), "L");
        text(dl, ImVec2(kc.x + r + px(5), kc.y - px(F_MARK / 2) - px(1)), F_MARK, rgb(0x8a939e), "R");
        int pan = c.pan;
        int turned = knob(dl, "pan", kc, r, &pan, rgb(c.color));
        if (turned != 0) {
            c.pan = turned == 2 ? 50 : pan;
            setTypePan(mixer, c.info.type, c.pan);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            char pt[24];
            panText(c.pan, pt, sizeof(pt));
            tipBeside(kc.x - r, kc.x + r, kc.y, pt);
        }
    }
    textCentered(dl, cx, p.y + px(1), F_NAME, rgb(c.color), "ALL");
    dl->AddRectFilled(ImVec2(cx - px(24), p.y + px(BAR_Y)), ImVec2(cx + px(24), p.y + px(BAR_Y + 2)), rgb(c.color), px(1));

    ImVec2 track(a.x + px(28), p.y + px(TRACK_Y));
    static const struct { const char* t; double db; } marks[] = {
        { "0", 0 }, { "-6", -6 }, { "-12", -12 }, { "-24", -24 }
    };
    for (int i = 0; i < 4; i++) {
        int v = volumeForDb(marks[i].db);
        float y = track.y + (1.0f - v / 100.0f) * (px(TRACK_H) - px(12)) + px(6);
        float tw = px(textWidth(marks[i].t, F_MARK));
        text(dl, ImVec2(a.x + px(25) - tw, y - px(F_MARK / 2)), F_MARK, rgb(0x8a939e), marks[i].t);
    }
    /* Left and right side by side. */
    drawMeter(dl, track, px(7), c.color, c.meterL);
    drawMeter(dl, ImVec2(track.x + px(8), track.y), px(7), c.color, c.meterR);
    int vol = c.volume;
    if (fader(dl, "all", ImVec2(a.x + px(48), track.y), px(18), px(TRACK_H), px(12), &vol, true)) {
        setChip(mixer, c, vol, c.mute, c.solo);
        c.volume = vol;
    }
    if (button(dl, "allm", "M", ImVec2(a.x + px(6), p.y + px(BUTTON_Y)), ImVec2(px(29), px(BUTTON_H)), F_ALL_BTN,
               chipMuted(c), 0xff5f6d, 0xffffff)) {
        toggleChipMute(mixer, c);
    }
    if (button(dl, "alls", "S", ImVec2(a.x + px(37), p.y + px(BUTTON_Y)), ImVec2(px(29), px(BUTTON_H)), F_ALL_BTN,
               c.solo != 0, 0xffd34e, 0x2a2100)) {
        setChip(mixer, c, c.volume, c.mute, !c.solo);
    }
}

static void drawSeg(Mixer* mixer, ImDrawList* dl, const Seg& s, ImVec2 a, float w)
{
    Chip& c = chips[s.chip];
    ImVec2 b(a.x + w, a.y + px(PANEL_H));
    char buf[96];

    ImGui::PushID((int)chipKey(c.info));
    ImGui::PushID(s.first ? 0 : s.pieces.empty() ? 1 : 2 + s.pieces[0].from);
    panelBody(dl, a, b);

    /* header */
    bool narrow = s.first && s.pieces.empty();
    snprintf(buf, sizeof(buf), "%s%s", narrow ? c.shortTitle : c.title, s.first ? "" : ts(TS_MIXER_CONTINUED));
    if (narrow && ImGui::IsMouseHoveringRect(a, ImVec2(b.x - px(7 + FOLD_W), a.y + px(HEADER_H)))) {
        ImGui::SetTooltip("%s", c.title);
    }
    float room = (b.x - a.x) / S - TITLE_X - 7 - (s.first && !c.voices.empty() ? FOLD_W + 4 : 0);
    float titleSize = fitText(buf, sizeof(buf), F_TITLE, room);
    text(dl, ImVec2(a.x + px(TITLE_X), a.y + px(3) + px(F_TITLE - titleSize) / 2), titleSize, rgb(0xe8ecf1), buf);
    if (s.first && titleSize == F_TITLE && textWidth(buf, F_TITLE) + 8 + textWidth(c.engine, F_HEAD) <= room) {
        float tx = a.x + px(TITLE_X) + px(textWidth(buf, F_TITLE)) + px(8);
        text(dl, ImVec2(tx, a.y + px(7)), F_HEAD, rgb(0xc3cad3), c.engine);
    }
    if (s.first) {
        if (!c.voices.empty()) {
            /* A double chevron: pointing left folds the channels into ALL,
            ** pointing right opens them again. */
            char fl[32];
            ImVec2 fp(b.x - px(7 + FOLD_W), a.y + px(3));
            if (button(dl, "fold", "", fp, ImVec2(px(FOLD_W), px(22)), F_HEAD, false, 0, 0)) {
                folded[chipKey(c.info)] = c.open;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", foldLabel(c, fl, sizeof(fl)));
            }
            float dir = c.open ? -1.0f : 1.0f, mx = fp.x + px(FOLD_W) / 2, my = fp.y + px(11);
            for (int k = 0; k < 2; k++) {
                float tip = mx + dir * px(k == 0 ? 4.5f : -0.5f), back = tip - dir * px(4);
                ImVec2 pts[3] = { ImVec2(back, my - px(4.5f)), ImVec2(tip, my), ImVec2(back, my + px(4.5f)) };
                dl->AddPolyline(pts, 3, rgb(0xd5dbe2), px(1.6f));
            }
        }
    }

    /* well */
    ImVec2 wa(a.x + px(7), a.y + px(HEADER_H)), wb(b.x - px(7), a.y + px(HEADER_H + WELL_H));
    well(dl, wa, wb);
    float x = wa.x + px(6), y = wa.y + px(6);
    if (s.first) {
        allStrip(mixer, dl, c, ImVec2(x, y));
        x += px(ALL_W + 8);
    }
    for (size_t i = 0; i < s.pieces.size(); i++) {
        const Piece& p = s.pieces[i];
        const Group& g = c.groups[p.group];
        float gw = px(pieceWidth(p));
        snprintf(buf, sizeof(buf), "%s%s", g.label, p.cont ? ts(TS_MIXER_CONTINUED) : "");
        text(dl, ImVec2(x + px(5), y + px(2)), F_NAME, rgb(g.color), buf);
        float lx = x + px(5) + px(textWidth(buf, F_NAME)) + px(6);
        dl->AddLine(ImVec2(lx, y + px(12)), ImVec2(x + gw - px(4), y + px(12)), rgb(0x3d444d));
        for (int v = p.from; v < p.to; v++) {
            voiceStrip(mixer, dl, c, v, g.color, ImVec2(x + px(3) + (v - p.from) * px(STRIP_STEP), y + px(STRIPS_Y)));
        }
        x += gw + px(GROUP_GAP);
    }

    ImGui::PopID();
    ImGui::PopID();
}

static void drawMaster(Mixer* mixer, ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    panelBody(dl, a, b);
    ImVec2 wa(a.x + px(7), a.y + px(HEADER_H)), wb(b.x - px(7), b.y - px(FOOTER_H));
    well(dl, wa, wb);
    float cx = (wa.x + wb.x) / 2;
    textCentered(dl, cx, a.y + px(3), F_TITLE, rgb(0xe8ecf1), "MASTER");
    /* Meters on the left, the Sound Mixer's master volume on the right. */
    float mx = cx - px(16);
    text(dl, ImVec2(mx - px(18), wa.y + px(5)), F_HEAD, rgb(0xc3cad3), "L");
    text(dl, ImVec2(mx + px(10), wa.y + px(5)), F_HEAD, rgb(0xc3cad3), "R");
    /* The Sound Mixer's master mute, saved with it. */
    Properties* prop = propGetGlobalProperties();
    if (prop != NULL && wb.y - px(BUTTON_H + 6) >= wa.y + px(24) &&
        button(dl, "masterm", "M", ImVec2(cx - px(29), wb.y - px(BUTTON_H + 6)), ImVec2(px(58), px(BUTTON_H)),
               F_ALL_BTN, !prop->sound.masterEnable, 0xff5f6d, 0xffffff)) {
        prop->sound.masterEnable = !prop->sound.masterEnable;
        mixerEnableMaster(mixer, prop->sound.masterEnable);
    }
    float scale = (wb.y - wa.y - px(32) - px(BUTTON_H + 6)) / px(TRACK_H);
    if (scale <= 0) {
        return;
    }
    int vol = prop != NULL ? prop->sound.masterVolume : 0;
    if (prop != NULL && fader(dl, "master", ImVec2(cx + px(18), wa.y + px(24)), px(18), px(TRACK_H) * scale, px(12),
                              &vol, true, 75)) {
        prop->sound.masterVolume = vol;
        mixerSetMasterVolume(mixer, vol);
    }
    for (int side = 0; side < 2; side++) {
        const Ramp& r = rampFor(0x59b7ff);
        int lit = (mixerMeterGetMasterPercent(mixer, side) * SEGS + 50) / 100;
        float step = px(TRACK_H) * scale / SEGS;
        float x0 = side == 0 ? mx - px(20) : mx + px(6);
        for (int k = 0; k < SEGS; k++) {
            float y1 = wa.y + px(24) + px(TRACK_H) * scale - k * step;
            ImVec2 p0(x0, y1 - step + px(2)), p1(x0 + px(14), y1);
            if (k < lit) {
                float mid = (p0.x + p1.x) / 2;
                dl->AddRectFilled(ImVec2(p0.x - px(1), p0.y - px(1)), ImVec2(p1.x + px(1), p1.y + px(1)), r.glow[k], px(2));
                dl->AddRectFilledMultiColor(p0, ImVec2(mid, p1.y), r.edge[k], r.core[k], r.core[k], r.edge[k]);
                dl->AddRectFilledMultiColor(ImVec2(mid, p0.y), p1, r.core[k], r.edge[k], r.edge[k], r.core[k]);
            }
            else {
                dl->AddRectFilled(p0, p1, r.off[k], px(1));
            }
        }
    }
}

/* ---- the window ------------------------------------------------------- */

static void draw(ImGuiToolWindow*, void*)
{
    Mixer* mixer = mixerGetGlobalMixer();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetWindowDrawList();
    ImVec2 o = vp->Pos, e(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);

    S = ImGui::GetStyle().FontScaleDpi;
    if (mixer == NULL) {
        return;
    }
    gather(mixer);
    /* Draw over the whole window, padding included. */
    ImGui::PushClipRect(o, e, false);

    int soloCount = 0;
    for (size_t i = 0; i < chips.size(); i++) {
        soloCount += chips[i].solo != 0;
        for (size_t v = 0; v < chips[i].voices.size(); v++) soloCount += chips[i].voices[v].solo != 0;
    }

    /* frame and top bar */
    bg->AddRectFilledMultiColor(o, e, rgb(0xa3adba), rgb(0xa3adba), rgb(0x7d8794), rgb(0x7d8794));
    ImVec2 ia(o.x + px(FRAME), o.y + px(FRAME)), ib(e.x - px(FRAME), e.y - px(FRAME));
    bg->AddRectFilled(ia, ib, rgb(0x33393f), px(4));
    ImVec2 ta = ia, tb(ib.x, ia.y + px(TOPBAR_H));
    bg->AddRectFilledMultiColor(ta, tb, rgb(0x8f99a6), rgb(0x8f99a6), rgb(0x5d6774), rgb(0x5d6774));
    bg->AddLine(ImVec2(ta.x, tb.y), tb, rgb(0x2b3036));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float bx = tb.x - px(14);
    float by = ta.y + px((TOPBAR_H - TOP_BTN_H) / 2);
    {
        const char* label;
        bool anyFolded = false;
        for (size_t i = 0; i < chips.size(); i++) anyFolded |= !chips[i].voices.empty() && !chips[i].open;
        label = anyFolded ? ts(TS_MIXER_EXPAND_ALL) : ts(TS_MIXER_COLLAPSE_ALL);
        float w = px(textWidth(label, F_TOP_BTN) + TOP_BTN_PAD);
        bx -= w;
        if (button(dl, "toggleAll", label, ImVec2(bx, by), ImVec2(w, px(TOP_BTN_H)), F_TOP_BTN, false, 0, 0)) {
            for (size_t i = 0; i < chips.size(); i++) folded[chipKey(chips[i].info)] = !anyFolded;
        }
        const char* clear = ts(TS_MIXER_CLEAR_SOLO);
        w = px(textWidth(clear, F_TOP_BTN) + TOP_BTN_PAD);
        bx -= w + px(8);
        if (button(dl, "clearSolo", clear, ImVec2(bx, by), ImVec2(w, px(TOP_BTN_H)), F_TOP_BTN, false, 0, 0, soloCount > 0)) {
            for (size_t i = 0; i < chips.size(); i++) {
                Chip& c = chips[i];
                if (c.solo) setChip(mixer, c, c.volume, c.mute, 0);
                for (size_t v = 0; v < c.voices.size(); v++) {
                    if (c.voices[v].solo) setVoice(mixer, c, (int)v, c.voices[v].volume, c.voices[v].mute, 0);
                }
            }
        }
        if (soloCount > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), ts(TS_MIXER_SOLO_COUNT), soloCount);
            float sw = px(textWidth(buf, F_TOP_BTN) + 16);
            bx -= sw + px(10);
            bg->AddRectFilled(ImVec2(bx, by), ImVec2(bx + sw, by + px(TOP_BTN_H)), rgb(0xffd34e), px(TOP_BTN_H / 2));
            text(bg, ImVec2(bx + px(8), by + px((TOP_BTN_H - F_TOP_BTN) / 2) - px(1)), F_TOP_BTN, rgb(0x3a2a00), buf);
        }
    }

    /* rows of chip panels, then the master on the right */
    ImVec2 ra(ia.x + px(MARGIN), tb.y + px(MARGIN));
    ImVec2 rb(ib.x - px(MARGIN + MASTER_W + MARGIN), ib.y - px(MARGIN));
    Rows rows;
    float widest = balance(maxOf(100.0f, (rb.x - ra.x) / S - 14), rows);

    /* As tall as the rows of panels beside it, not the whole window. */
    float masterH = maxOf(1, (int)rows.size()) * px(PANEL_H + PANEL_GAP) - px(PANEL_GAP);
    drawMaster(mixer, dl, ImVec2(rb.x + px(MARGIN), ra.y), ImVec2(ib.x - px(MARGIN), minOf(rb.y, ra.y + masterH)));

    ImGui::SetCursorScreenPos(ra);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("rows", ImVec2(rb.x - ra.x, rb.y - ra.y), 0,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* cl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    for (size_t r = 0; r < rows.size(); r++) {
        float x = origin.x;
        float y = origin.y + r * px(PANEL_H + PANEL_GAP);
        for (size_t i = 0; i < rows[r].size(); i++) {
            float w = px(segWidth(rows[r][i]));
            drawSeg(mixer, cl, rows[r][i], ImVec2(x, y), w);
            x += w + px(PANEL_GAP);
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(px(widest), rows.size() * px(PANEL_H + PANEL_GAP)));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopClipRect();
}

static void closed(void*)
{
    Mixer* mixer = mixerGetGlobalMixer();
    if (mixer != NULL) {
        mixerEnableDetailMeters(mixer, 0);
    }
}

extern "C" void detailedMixerShow(void)
{
    ImGuiToolDesc desc = {};
    desc.id     = "mixer";
    desc.title  = langMenuToolsMixerDetailed();
    desc.width  = 1480;
    desc.height = 960;
    desc.flags  = IMGUI_TOOL_ALWAYS_DARK;
    desc.draw   = draw;
    desc.closed = closed;
    desc.placement = &propGetGlobalProperties()->detailedMixer.window;
    if (imguiToolOpen(&desc) != NULL && mixerGetGlobalMixer() != NULL) {
        mixerEnableDetailMeters(mixerGetGlobalMixer(), 1);
    }
}
