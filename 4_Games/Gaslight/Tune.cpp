// Gaslight & Griffins - the music tracker. Plays the pieces in GLMusic.h
// (written by tools/make_music.py) with a handful of small synthesised
// instruments, a block at a time, mixed under the sound effects by RunAudio.
// The same code renders WAV previews in the PC simulator.
#pragma GCC optimize("O2")
#include "Tune.h"
#include "GLMusic.h"
#include <math.h>
#include <string.h>
#include <esp_heap_caps.h>

namespace Gaslight {
namespace Tune {

using namespace Songs;
// The music is made at half the output rate and smoothed up to 44.1 kHz: it has almost
// nothing above 10 kHz, and this halves what it costs the core the renderer shares.
static const float RATE = 22050.0f;
static const float TWO_PI = 6.2831853f;

// --- instruments --------------------------------------------------------------------------------
enum Wave : uint8_t { W_SINE, W_MBOX, W_HARP, W_PAD, W_BASS, W_REED, W_BELL, W_PLUCK, W_DRUM, W_LEAD, W_STR, W_OUD };
struct Inst {
  Wave w;
  float a, d, s, r;   // attack (s), decay time to sustain (s), sustain level, release (s)
  float cut;          // low-pass cutoff in Hz (0 = none)
  float cutEnv;       // how far the cutoff falls over the decay (0 none, 1 all the way)
  float vib, vibHz;   // vibrato depth (fraction of pitch) and rate
  float gain;
  float gate;         // fraction of the note's length before release
};
//                          wave    attack decay sus   release cut     cutEnv vib     vibHz gain  gate
static const Inst INST[] = {
    /* FLUTE   */ {W_SINE,  0.04f, 0.3f,  0.75f, 0.12f, 0,      0,     0.006f, 5.2f, 1.0f, 0.88f},
    /* MBOX    */ {W_MBOX,  0.002f,1.1f,  0.0f,  0.4f,  0,      0,     0,      0,    0.8f, 1.0f},
    /* HARP    */ {W_HARP,  0.003f,1.4f,  0.0f,  0.6f,  2600,   0.7f,  0,      0,    1.0f, 1.0f},
    /* PAD     */ {W_PAD,   0.35f, 0.6f,  0.8f,  0.6f,  1300,   0,     0.003f, 4.0f, 0.55f,0.97f},
    /* BASS    */ {W_BASS,  0.006f,0.5f,  0.55f, 0.08f, 900,    0.4f,  0,      0,    1.15f,0.9f},
    /* REED    */ {W_REED,  0.03f, 0.25f, 0.8f,  0.1f,  2300,   0.2f,  0.008f, 5.8f, 0.55f,0.86f},
    /* BELL    */ {W_BELL,  0.002f,2.2f,  0.0f,  1.2f,  0,      0,     0,      0,    0.55f,1.0f},
    /* PIZZ    */ {W_PLUCK, 0.002f,0.35f, 0.0f,  0.1f,  2000,   0.85f, 0,      0,    1.0f, 1.0f},
    /* KICKS   */ {W_DRUM,  0,     0,     0,     0,     0,      0,     0,      0,    1.0f, 1.0f},
    /* HATS    */ {W_DRUM,  0,     0,     0,     0,     0,      0,     0,      0,    1.0f, 1.0f},
    /* LEAD    */ {W_LEAD,  0.01f, 0.2f,  0.7f,  0.08f, 2600,   0.3f,  0.006f, 5.5f, 0.6f, 0.85f},
    /* STRINGS */ {W_STR,   0.18f, 0.5f,  0.85f, 0.35f, 2200,   0,     0.009f, 5.0f, 0.55f,0.95f},
    /* OUD     */ {W_OUD,   0.002f,0.45f, 0.0f,  0.08f, 2400,   0.8f,  0,      0,    0.9f, 1.0f},
};

static float *s_sin = nullptr;   // 2048 entries, in PSRAM (internal RAM is scarce in this game)
static inline float sn(uint32_t ph) { return s_sin[ph >> 21]; }
static inline float saw(uint32_t ph) { return (float)ph * (1.0f / 2147483648.0f) - 1.0f; }
static inline float tri(uint32_t ph) { float s = saw(ph); return 2.0f * fabsf(s) - 1.0f; }
static uint32_t s_rng = 0x2468ACE;
static inline float noise() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return (float)(int32_t)s_rng * (1.0f / 2147483648.0f); }
static inline uint32_t incOf(float hz) { return (uint32_t)(hz * (4294967296.0f / RATE)); }

// --- channels --------------------------------------------------------------------------------
struct Chan {
  const Ch *def;
  uint16_t pos;            // next event
  int32_t ticksLeft;       // of the current event
  int32_t gateTicks;       // when to release
  bool on;                 // a note is sounding
  int stage;               // 0 attack 1 decay/sustain 2 release
  float env, aInc, dK, sus, rK;
  uint32_t ph, ph2, inc, inc2;
  float freq, lp, cut, cutK, vibPh, gain;
  int drum;                // which drum, for the kit
  float dEnv, dK2, dFreq, dSweep;
  float bp1, bp2;          // a small band-pass for snares and hats
};
static Chan s_ch[8];
static const Song *s_song = nullptr;
static int s_nch = 0;
static float s_spt = 0, s_acc = 0;     // samples per tick, and the running count
static volatile int s_req = -1, s_cur = -1;
static int s_resume = -1;              // what to go back to after a one-shot
static float s_level = 0, s_target = 0, s_volume = 0.55f;
static bool s_inited = false;

static void init() {
  if (!s_sin) s_sin = (float *)heap_caps_malloc(2048 * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_sin) return;
  for (int i = 0; i < 2048; i++) s_sin[i] = sinf(i * TWO_PI / 2048);
  s_inited = true;
}

static float lpK(float hz) { return hz <= 0 ? 1.0f : 1.0f - expf(-TWO_PI * hz / RATE); }

static void noteOn(Chan &c, int note, int ticks) {
  const Inst &I = INST[c.def->inst];
  c.gain = c.def->vol / 15.0f * I.gain;
  if (I.w == W_DRUM) {
    c.drum = note; c.on = true;
    c.dEnv = 1; c.ph = 0;
    float dur = note == 36 ? 0.28f : note == 38 ? 0.17f : note == 42 ? 0.045f : note == 46 ? 0.22f : note == 39 ? 0.22f : 0.03f;
    c.dK2 = expf(logf(0.001f) / (dur * RATE));
    c.dFreq = note == 36 ? 140 : note == 39 ? 190 : note == 38 ? 190 : 1800;
    c.dSweep = note == 36 ? expf(logf(45.0f / 140) / (0.12f * RATE)) : note == 39 ? expf(logf(110.0f / 190) / (0.15f * RATE)) : 1.0f;
    c.bp1 = c.bp2 = 0;
    return;
  }
  c.freq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
  c.inc = incOf(c.freq);
  c.inc2 = I.w == W_PAD || I.w == W_STR ? incOf(c.freq * 1.006f) : I.w == W_BELL ? incOf(c.freq * 2.76f)
         : I.w == W_REED ? incOf(c.freq * 1.004f) : I.w == W_BASS ? incOf(c.freq * 0.5f) : c.inc;
  c.on = true; c.stage = 0;
  c.env = I.a <= 0.002f ? 1.0f : 0.0f;
  c.aInc = I.a <= 0.002f ? 1.0f : 1.0f / (I.a * RATE);
  c.sus = I.s;
  c.dK = expf(logf(0.001f) / (fmaxf(0.01f, I.d) * RATE));
  c.rK = expf(logf(0.001f) / (fmaxf(0.01f, I.r) * RATE));
  c.cut = I.cut;
  c.cutK = lpK(I.cut);
  c.gateTicks = (int32_t)(ticks * I.gate);
  if (c.gateTicks < 1) c.gateTicks = 1;
  if (I.w == W_PLUCK || I.w == W_HARP || I.w == W_OUD) c.ph = 0;
}

static void startSong(int id) {
  s_cur = id;
  if (id < 0 || id >= M_COUNT) { s_song = nullptr; s_nch = 0; return; }
  s_song = &SONGS[id];
  s_nch = s_song->nch;
  s_spt = RATE * 60.0f / (s_song->bpm * 12.0f);
  s_acc = 0;
  for (int i = 0; i < s_nch; i++) {
    Chan &c = s_ch[i];
    memset(&c, 0, sizeof(c));
    c.def = &s_song->ch[i];
    c.pos = 0;
    c.ticksLeft = 0;   // the first tick starts the first event
  }
}

// One tick: each channel moves on to its next event when the current one ends.
static void tick() {
  bool allEnded = true;
  for (int i = 0; i < s_nch; i++) {
    Chan &c = s_ch[i];
    if (c.ticksLeft <= 0) {
      if (c.pos >= c.def->n) {
        if (!s_song->loop) { c.ticksLeft = 1 << 30; continue; }
        c.pos = 0;
      }
      uint8_t note = c.def->ev[c.pos * 2], t = c.def->ev[c.pos * 2 + 1];
      c.pos++;
      c.ticksLeft = t;
      if (note) noteOn(c, note, t);
      else if (c.on && c.stage < 2) c.stage = 2;
    }
    c.ticksLeft--;
    if (INST[c.def->inst].w != W_DRUM && c.on && c.stage < 2 && --c.gateTicks <= 0) c.stage = 2;
    if (c.pos < c.def->n || c.ticksLeft < (1 << 29)) allEnded = false;
  }
  if (!s_song->loop && allEnded) {
    // a one-shot is over: back to what was playing
    s_req = s_resume;
  }
}

static inline float voice(Chan &c) {
  const Inst &I = INST[c.def->inst];
  if (I.w == W_DRUM) {
    if (c.dEnv < 0.0005f) return 0;
    float s;
    if (c.drum == 36 || c.drum == 39) {          // kick / hand drum: a falling sine
      s = sn(c.ph) * 1.3f;
      c.ph += incOf(c.dFreq);
      c.dFreq *= c.dSweep;
    } else if (c.drum == 38) {                    // snare: noise and a body
      float n = noise();
      c.bp1 += 0.35f * (n - c.bp1);
      s = (n - c.bp1) * 0.7f + sn(c.ph) * 0.35f;
      c.ph += incOf(c.dFreq);
    } else if (c.drum == 37) {                    // a clock's tick
      s = tri(c.ph) * 0.6f;
      c.ph += incOf(2200);
    } else {                                      // hats: high noise
      float n = noise();
      c.bp1 += 0.6f * (n - c.bp1);
      s = (n - c.bp1) * 0.45f;
    }
    s *= c.dEnv;
    c.dEnv *= c.dK2;
    return s * c.gain;
  }
  if (!c.on) return 0;
  // envelope
  if (c.stage == 0) { c.env += c.aInc; if (c.env >= 1) { c.env = 1; c.stage = 1; } }
  else if (c.stage == 1) c.env = c.sus + (c.env - c.sus) * c.dK;
  else { c.env *= c.rK; if (c.env < 0.0003f) { c.on = false; return 0; } }
  // vibrato
  uint32_t inc = c.inc, inc2 = c.inc2;
  if (I.vib > 0) {
    c.vibPh += I.vibHz / RATE;
    if (c.vibPh > 1) c.vibPh -= 1;
    float v = 1.0f + I.vib * s_sin[(int)(c.vibPh * 2047)] * fminf(1.0f, c.env * 1.5f);
    inc = (uint32_t)(inc * v); inc2 = (uint32_t)(inc2 * v);
  }
  float s;
  switch (I.w) {
    case W_SINE: s = sn(c.ph) + 0.12f * sn(c.ph * 2); break;
    case W_MBOX: s = sn(c.ph) + 0.3f * sn(c.ph * 2) * c.env + 0.12f * sn(c.ph * 5) * c.env * c.env; break;
    case W_HARP: s = tri(c.ph) * 0.8f + 0.25f * sn(c.ph * 2); break;
    case W_PAD: s = (saw(c.ph) + saw(c.ph2)) * 0.5f; break;
    case W_STR: s = (saw(c.ph) + saw(c.ph2)) * 0.5f; break;
    case W_BASS: s = tri(c.ph) + 0.35f * sn(c.ph2); break;
    case W_REED: s = ((c.ph < 0x50000000u ? 1.0f : -0.45f) + (c.ph2 < 0x50000000u ? 0.6f : -0.27f)) * 0.6f; break;
    case W_BELL: s = sn(c.ph) + 0.5f * sn(c.ph2) * c.env; break;
    case W_PLUCK: s = saw(c.ph); break;
    case W_OUD: s = saw(c.ph) * 0.7f + 0.3f * sn(c.ph * 2); break;
    case W_LEAD: s = (c.ph < 0x80000000u ? 0.8f : -0.8f) * 0.6f + saw(c.ph) * 0.3f; break;
    default: s = 0;
  }
  c.ph += inc; c.ph2 += inc2;
  if (I.cut > 0) {
    // the cutoff closes as the note decays (plucks), then a one-pole low-pass
    float k = c.cutK;
    if (I.cutEnv > 0) k *= (1.0f - I.cutEnv) + I.cutEnv * c.env;
    c.lp += k * (s - c.lp);
    s = c.lp;
  }
  return s * c.env * c.gain;
}

static float one();
void render(float *out, int n) {
  if (!s_inited) init();
  if (!s_inited) { memset(out, 0, n * sizeof(float)); return; }
  static float prev = 0;
  for (int i = 0; i + 1 < n; i += 2) {
    float s = one();
    out[i] = (prev + s) * 0.5f;
    out[i + 1] = s;
    prev = s;
  }
}

// One sample at the music's own rate.
static float one() {
  float out1 = 0;
  float *out = &out1;
  for (int i = 0; i < 1; i++) {
    // changing piece: fade out, swap, fade in
    int want = s_req;
    if (want != s_cur) {
      s_target = 0;
      if (s_level < 0.002f) {
        startSong(want);
        s_target = s_song ? 1.0f : 0.0f;
      }
    } else s_target = s_song ? 1.0f : 0.0f;
    s_level += (s_target - s_level) * (s_target > s_level ? 0.00024f : 0.00016f);
    if (!s_song) { out[i] = 0; continue; }
    s_acc += 1;
    if (s_acc >= s_spt) { s_acc -= s_spt; tick(); }
    float mix = 0;
    for (int c = 0; c < s_nch; c++) mix += voice(s_ch[c]);
    mix *= 0.16f * s_level * s_volume;
    // a soft limiter so chords never clip
    if (mix > 0.6f) mix = 0.6f + (mix - 0.6f) * 0.3f;
    if (mix < -0.6f) mix = -0.6f + (mix + 0.6f) * 0.3f;
    out[i] = mix;
  }
  return out1;
}

void play(int song) {
  if (song == s_req) return;
  // a fanfare plays to its end; what's asked for meanwhile comes after it
  bool oneShotNow = s_req >= 0 && s_req < M_COUNT && !SONGS[s_req].loop;
  if (oneShotNow && (song < 0 || SONGS[song].loop)) { s_resume = song; return; }
  if (song >= 0 && song < M_COUNT && !SONGS[song].loop) {
    // a one-shot: remember what to come back to
    if (s_req >= 0 && s_req < M_COUNT && SONGS[s_req].loop) s_resume = s_req;
  }
  s_req = song;
}
int playing() { return s_req; }
void setVolume(float v) { s_volume = v; }

} // namespace Tune
} // namespace Gaslight
