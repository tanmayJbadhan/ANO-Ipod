// Speed matters more than size here: the drawing loops run per pixel.
#pragma GCC optimize("O2")
#include "Lamplighter.h"
#include "Raster.h"
#include "RunAudio.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#ifndef PC_SIM
#include <soc/soc_memory_layout.h>
#endif
#include <math.h>
#include <string.h>
#include <font/glcdfont.h>

using namespace Raster;

namespace Lamplighter {

// ---------------------------------------------------------------------------
// The sky
// ---------------------------------------------------------------------------

// Drawn at the screen's full resolution, 456 x 280, a strip of STRIP_H rows
// at a time (see "Drawing on two cores" below).
static const int W = 456, H = 280;
static const int STRIP_H = 20, STRIPS = H / STRIP_H;
static const float SHIP_X = 150;     // where the camera keeps the ship across the screen
static const float BAND = 290;       // worlds lie within +-BAND of the lane's middle
static const float EDGE = 370;       // past this the aurora turns the ship back
static const float REGION_LEN = 3200;
static const float PI_F = 3.14159265f;

struct Region { const char *name; uint32_t sky0, sky1, neb0, neb1, accent; };
static const Region REG[5] = {
    {"The Amber Shallows", 0x120a07, 0x22120a, 0x6a3412, 0xd88a3a, 0xffc878},
    {"The Ring Gardens", 0x05100e, 0x0a221e, 0x12584a, 0x5ac8a4, 0x9af4d4},
    {"The Comet Nursery", 0x050916, 0x0b142e, 0x1a3a7e, 0x6a9ee8, 0xb0d6ff},
    {"The Rose Nebula", 0x12050c, 0x280b1a, 0x7c1a4a, 0xe86a9c, 0xffb4d4},
    {"The Deep", 0x030205, 0x090712, 0x281a4a, 0x6a4aa8, 0xc8b0ff},
};

enum class Mode : uint8_t { Title, Story, Play, Paused, Dying, Over };
enum Kind : uint8_t { B_ROCK, B_GAS, B_RING, B_ICE, B_LAVA, B_SUN, B_HOLE };

struct Body {
  bool on, lit, moon;
  uint8_t kind;
  float x, y, r, field, pull; // pull: gravity strength, see gravity()
  uint32_t c0, c1, atm;
  float spin, spinW, seed, bands, ringA, litT;
  float moonR, moonA, moonW, moonS;
  float crX[3], crY[3], crR[3];
};
struct Rock { bool on, comet; float x, y, vx, vy, r, a, w; uint8_t shape[8]; };
struct Mote { bool on; float x, y, t; };
struct Spark { float x, y, vx, vy, life; uint32_t c; };

static const int NB = 12, NR = 28, NM = 28, NS = 56, TRAIL = 44;

static bool s_active = false, s_exit = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static uint16_t *s_land[2] = {nullptr, nullptr};          // a strip as drawn, one per core
static const int SLOTS = 4;
static uint16_t *s_port[SLOTS] = {};                      // strips turned for the panel
static uint16_t *s_shot = nullptr;                        // a whole frame, only for screenshots
static uint16_t *s_neb = nullptr; // the nebula behind everything
static const int NT_W = 160, NT_H = 100;
static Preferences s_prefs;

static Mode s_mode = Mode::Title;
static float s_t = 0, s_modeT = 0, s_clock = 0;
static bool s_storySeen = false;

static Body s_b[NB];
static Rock s_rock[NR];
static Mote s_mote[NM];
static Spark s_spark[NS];
static float s_lastX = 0;

// The Moth.
static float s_x, s_y, s_ang, s_v;
static int s_orbit = -1, s_lastBody = -1;
static float s_oa, s_or, s_orT, s_osign, s_releasedT = 9, s_orbitT = 0;
static float s_trailX[TRAIL], s_trailY[TRAIL], s_trailT = 0;
static int s_trailN = 0, s_trailHead = 0;
static float s_maxX = 0, s_hushX = 0, s_camX = 0, s_camY = 0, s_shake = 0;
static int s_lamps = 0, s_motes = 0, s_region = -1, s_lap = 0;
static float s_bannerT = 0, s_warnT = 0;
static bool s_hold = false;
static const char *s_why = "";
static uint32_t s_best = 0, s_bestLamps = 0;
static bool s_newBest = false;
static bool s_auto = false;
static int32_t s_steer = 0; // wheel clicks waiting to be applied

// --- random numbers and noise ---------------------------------------------------

static uint32_t s_rng = 1;
static uint32_t rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static float frand() { return (rnd() >> 8) / 16777216.0f; }
static float frange(float a, float b) { return a + (b - a) * frand(); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smooth(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static float wrapAng(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }

// The noise takes its seed as an argument (both cores draw at once, so
// there's no shared "current seed"); s_noiseSeed is the run's own.
static uint32_t s_noiseSeed = 0;
static uint32_t hash32(uint32_t a, uint32_t b, uint32_t seed) {
  uint32_t h = a * 374761393u + b * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float hashf(int x, int y, uint32_t seed) { return (hash32((uint32_t)x, (uint32_t)y, seed) & 0xffff) / 65535.0f; }
// Value noise with a smoothstep blend, and its fractal sum
// fbm(p) = sum_i 0.5^i noise(2^i p) / sum_i 0.5^i.
static float vnoise(float x, float y, uint32_t seed) {
  float fx = floorf(x), fy = floorf(y);
  int xi = (int)fx, yi = (int)fy;
  float u = x - fx, v = y - fy;
  u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
  float a = hashf(xi, yi, seed), b = hashf(xi + 1, yi, seed), c = hashf(xi, yi + 1, seed), d = hashf(xi + 1, yi + 1, seed);
  return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}
static float fbm(float x, float y, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int i = 0; i < 4; i++) { s += amp * vnoise(x, y, seed); norm += amp; x *= 2.03f; y *= 2.03f; amp *= 0.5f; }
  return s / norm;
}

static void unpack(uint32_t c, float &r, float &g, float &b) { r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; }
static inline uint16_t pack(float r, float g, float b) {
  int R = r > 255 ? 255 : r < 0 ? 0 : (int)r, G = g > 255 ? 255 : g < 0 ? 0 : (int)g, B = b > 255 ? 255 : b < 0 ? 0 : (int)b;
  return (uint16_t)(((R & 0xf8) << 8) | ((G & 0xfc) << 3) | (B >> 3));
}

// --- text ---------------------------------------------------------------------

// The 5x7 font with each dot 2*size pixels square, into the current strip.
static void glyph(int x, int y, unsigned char ch, int size, uint16_t col) {
  int p = 2 * size;
  if (y + 8 * p <= top() || y >= top() + height()) return;
  for (int cx = 0; cx < 5; cx++) {
    uint8_t bits = font[ch * 5 + cx];
    int x0 = x + cx * p, x1 = x0 + p;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    for (int cy = 0; cy < 8; cy++, bits >>= 1) {
      if (!(bits & 1)) continue;
      for (int yy = y + cy * p; yy < y + cy * p + p; yy++) {
        uint16_t *r = row(yy);
        if (r) for (int xx = x0; xx < x1; xx++) r[xx] = col;
      }
    }
  }
}
static int textW(const char *s, int size) { return (int)strlen(s) * 12 * size; }
static void txt(int x, int y, const char *s, int size, uint16_t col, bool shadow = false) {
  if (y + 16 * size + 2 <= top() || y >= top() + height()) return;
  if (shadow)
    for (const char *p = s; *p; p++) glyph(x + 2 + (int)(p - s) * 12 * size, y + 2, (unsigned char)*p, size, 0);
  for (const char *p = s; *p; p++, x += 12 * size) glyph(x, y, (unsigned char)*p, size, col);
}
static void txtC(int cx, int y, const char *s, int size, uint16_t col, bool shadow = false) {
  txt(cx - textW(s, size) / 2, y, s, size, col, shadow);
}
// Word-wrapped, centred lines; returns the y below the last one.
static int wrapC(int cx, int y, const char *s, int maxChars, uint16_t col) {
  char line[48];
  while (*s) {
    int n = (int)strlen(s);
    if (n > maxChars) {
      n = maxChars;
      while (n > 0 && s[n] != ' ') n--;
      if (n == 0) n = maxChars;
    }
    memcpy(line, s, n);
    line[n] = 0;
    txtC(cx, y, line, 1, col);
    y += 16;
    s += n;
    while (*s == ' ') s++;
  }
  return y;
}

// ---------------------------------------------------------------------------
// Worlds, rocks and stardust
// ---------------------------------------------------------------------------

static int regionAt(float x) { return x < 0 ? 0 : ((int)(x / REGION_LEN)) % 5; }
static int lapAt(float x) { return x < 0 ? 0 : (int)(x / (REGION_LEN * 5)); }

// Gravity: a = pull * v^2 * r / d^2 toward the centre, inside the field. It
// scales with v^2 so a world bends the path into the same shapes however
// fast the Moth goes; only the direction changes - her speed is fixed.
static float speedNow() { return 125 + fminf(s_maxX / 110.0f, 100) + s_lap * 10; }

static void spawnRocks(float x0, float x1, int reg, int lap);

static void spawnBody(bool first) {
  int slot = -1;
  for (int i = 0; i < NB; i++) if (!s_b[i].on) { slot = i; break; }
  if (slot < 0) { // the farthest behind gives way
    slot = 0;
    for (int i = 1; i < NB; i++) if (s_b[i].x < s_b[slot].x) slot = i;
  }
  Body &b = s_b[slot];
  memset(&b, 0, sizeof(b));
  float x = first ? s_lastX + 280 : s_lastX + frange(170, 280);
  int reg = regionAt(x), lap = lapAt(x);
  float p = frand();
  uint8_t k;
  switch (reg) {
    case 0: k = p < 0.45f ? B_ROCK : p < 0.7f ? B_LAVA : B_GAS; break;
    case 1: k = p < 0.5f ? B_RING : p < 0.8f ? B_GAS : B_ICE; break;
    case 2: k = p < 0.45f ? B_ICE : p < 0.75f ? B_ROCK : B_GAS; break;
    case 3: k = p < 0.3f ? B_SUN : p < 0.65f ? B_GAS : B_RING; break;
    default: k = p < 0.25f ? B_HOLE : p < 0.5f ? B_ICE : p < 0.75f ? B_ROCK : B_SUN; break;
  }
  if (first) k = B_ROCK;
  b.kind = k;
  switch (k) {
    case B_ROCK: b.r = frange(17, 23); break;
    case B_LAVA: b.r = frange(17, 22); break;
    case B_ICE: b.r = frange(18, 25); break;
    case B_GAS: b.r = frange(26, 34); break;
    case B_RING: b.r = frange(20, 26); break;
    case B_SUN: b.r = frange(29, 36); break;
    default: b.r = frange(12, 15); break;
  }
  b.field = k == B_HOLE ? b.r * 5.5f + 30 : k == B_SUN ? b.r * 2.1f + 44 : b.r * 2.3f + 36;
  b.pull = k == B_HOLE ? 3.2f : k == B_SUN ? 1.25f : k == B_GAS ? 1.0f : 0.85f;
  // Where: far enough from its neighbours that one field never swallows
  // another world.
  float y = 0;
  for (int tries = 0; tries < 30; tries++) {
    y = first ? frange(-50, 50) : frange(-BAND, BAND);
    bool ok = true;
    for (int i = 0; i < NB && ok; i++) {
      if (i == slot || !s_b[i].on) continue;
      float dx = s_b[i].x - x, dy = s_b[i].y - y, d = sqrtf(dx * dx + dy * dy);
      ok = d > fmaxf(s_b[i].field + b.r, b.field + s_b[i].r) + 30;
    }
    if (ok) break;
    if (tries == 29) x += 80;
  }
  b.x = x; b.y = y;
  b.on = true;
  b.seed = frand() * 50;
  b.spin = frand() * 6;
  b.spinW = frange(0.05f, 0.18f) * (rnd() & 1 ? 1 : -1);
  b.bands = frange(7, 13);
  b.ringA = frange(-0.5f, 0.5f);
  static const uint32_t GAS[5][2] = {{0xf0c890, 0xb06a34}, {0xa0dcc8, 0x2e8a76}, {0xd8a0e0, 0x6e3e96}, {0x96b8f0, 0x3454a8}, {0xf0e0b0, 0xa88a50}};
  static const uint32_t ROCKC[3][2] = {{0xa09080, 0x5a4a3e}, {0xc08868, 0x6a3a28}, {0x9aa0a8, 0x4a4e58}};
  int pick = rnd() % 5;
  switch (k) {
    case B_GAS: case B_RING: b.c0 = GAS[pick][0]; b.c1 = GAS[pick][1]; b.atm = GAS[pick][0]; break;
    case B_ROCK: b.c0 = ROCKC[pick % 3][0]; b.c1 = ROCKC[pick % 3][1]; b.atm = 0x8090a0; break;
    case B_ICE: b.c0 = 0xe8f6ff; b.c1 = 0x6aa6d6; b.atm = 0xbfe8ff; break;
    case B_LAVA: b.c0 = 0x3a221c; b.c1 = 0x140c0a; b.atm = 0xff6a20; break;
    case B_SUN:
      if (pick < 2) { b.c0 = 0xff9a30; b.c1 = 0xfff6d8; }
      else if (pick < 4) { b.c0 = 0xff5a3a; b.c1 = 0xffd8b0; }
      else { b.c0 = 0x5a8cff; b.c1 = 0xeef6ff; }
      b.atm = b.c0;
      break;
    default: b.c0 = 0xff9a3a; b.c1 = 0xfff0c8; b.atm = 0xff9a3a; break;
  }
  for (int i = 0; i < 3; i++) {
    float a = frand() * 6.283f, d = frand() * 0.6f;
    b.crX[i] = cosf(a) * d; b.crY[i] = sinf(a) * d; b.crR[i] = frange(0.16f, 0.3f);
  }
  // A moon circling inside the field: in the way of a careless orbit.
  if (!first && reg + lap >= 1 && (k == B_GAS || k == B_RING || k == B_ROCK || k == B_ICE) && frand() < 0.35f + 0.1f * lap) {
    b.moon = true;
    b.moonS = frange(4, 6);
    b.moonR = b.r + 20 + frand() * (b.field - b.r - 30);
    b.moonA = frand() * 6.283f;
    b.moonW = frange(0.5f, 0.9f) * (rnd() & 1 ? 1 : -1);
  }
  // Stardust strung across the gap from the last world.
  if (!first) {
    float x0 = s_lastX, y0 = 0;
    for (int i = 0; i < NB; i++) if (s_b[i].on && i != slot && fabsf(s_b[i].x - s_lastX) < 1) y0 = s_b[i].y;
    int n = 3 + (int)(rnd() % 3);
    float bow = frange(-60, 60);
    for (int j = 0; j < n; j++) {
      float t = 0.3f + 0.4f * j / (n - 1);
      float mx = lerpf(x0, x, t), my = lerpf(y0, y, t) + bow * sinf(t * PI_F);
      for (int m = 0; m < NM; m++)
        if (!s_mote[m].on) { s_mote[m] = {true, mx, my, frand() * 6}; break; }
    }
    spawnRocks(x0 + 40, x - 40, reg, lap);
  }
  s_lastX = x;
}

static bool clearOfWorlds(float x, float y, float r) {
  for (int i = 0; i < NB; i++) {
    if (!s_b[i].on) continue;
    float dx = s_b[i].x - x, dy = s_b[i].y - y;
    if (dx * dx + dy * dy < (s_b[i].r + r + 22) * (s_b[i].r + r + 22)) return false;
  }
  return true;
}

static void addRock(float x, float y, float vx, float vy, float r, bool comet) {
  for (int i = 0; i < NR; i++) {
    if (s_rock[i].on) continue;
    Rock &k = s_rock[i];
    k.on = true; k.comet = comet;
    k.x = x; k.y = y; k.vx = vx; k.vy = vy; k.r = r;
    k.a = frand() * 6.283f; k.w = frange(-1.5f, 1.5f);
    for (int j = 0; j < 8; j++) k.shape[j] = (uint8_t)(rnd() % 64);
    return;
  }
}

static void spawnRocks(float x0, float x1, int reg, int lap) {
  if (x1 <= x0) return;
  static const float PER[5] = {0.7f, 1.2f, 1.3f, 1.8f, 2.4f};
  float want = PER[reg] + lap * 0.6f;
  int n = (int)want + (frand() < want - (int)want ? 1 : 0);
  for (int i = 0; i < n; i++)
    for (int tries = 0; tries < 8; tries++) {
      float x = frange(x0, x1), y = frange(-BAND - 30, BAND + 30), r = frange(7, 13);
      if (!clearOfWorlds(x, y, r)) continue;
      addRock(x, y, frange(-14, 14), frange(-14, 14), r, false);
      break;
    }
  // Comets cross the Nursery (and later laps) from ahead.
  if ((reg == 2 && frand() < 0.5f) || (lap > 0 && frand() < 0.25f))
    addRock(x1 + 260, frange(-BAND, BAND), -frange(70, 110), frange(-25, 25), 7, true);
}

static void spark(float x, float y, int n, uint32_t c, float speed) {
  for (int k = 0; k < n; k++) {
    for (int i = 0; i < NS; i++) {
      if (s_spark[i].life > 0) continue;
      float a = frand() * 6.283f, v = speed * (0.3f + frand());
      s_spark[i] = {x, y, cosf(a) * v, sinf(a) * v, frange(0.4f, 1.0f), c};
      break;
    }
  }
}

// The nebula for a region: two layers of fractal noise in the region's
// colours, made seamless left to right by blending f(x) with f(x - width):
// T(x) = ((w - x) f(x) + x f(x - w)) / w.
static void makeNebula(int reg) {
  if (!s_neb) return;
  const Region &R = REG[reg];
  uint32_t seed = 101 + reg * 7919;
  const float U = NT_W * 0.045f;
  for (int y = 0; y < NT_H; y++) {
    uint32_t sky = mixHex(R.sky0, R.sky1, y / (float)(NT_H - 1));
    for (int x = 0; x < NT_W; x++) {
      float fx = x / (float)NT_W, u = x * 0.045f, v = y * 0.06f;
      float n = (1 - fx) * fbm(u, v, seed) + fx * fbm(u - U, v, seed);
      float n2 = (1 - fx) * fbm(u * 2.1f + 40, v * 2.1f, seed) + fx * fbm((u - U) * 2.1f + 40, v * 2.1f, seed);
      float a = smooth(0.40f, 0.74f, n);
      uint32_t c = mixHex(sky, R.neb0, a * 0.8f);
      c = mixHex(c, R.neb1, smooth(0.55f, 0.85f, n2) * a * 0.65f);
      s_neb[y * NT_W + x] = rgb(c);
    }
  }
}

// ---------------------------------------------------------------------------
// A run
// ---------------------------------------------------------------------------

static void setMode(Mode m) { s_mode = m; s_modeT = 0; }

static void newRun(float startX = 0) {
  memset(s_b, 0, sizeof(s_b));
  memset(s_rock, 0, sizeof(s_rock));
  memset(s_mote, 0, sizeof(s_mote));
  for (Spark &p : s_spark) p.life = 0;
  s_noiseSeed = esp_random();
  s_x = startX; s_y = 0; s_ang = 0; s_maxX = startX; s_lap = lapAt(startX);
  s_v = speedNow();
  s_orbit = -1; s_lastBody = -1; s_releasedT = 9; s_orbitT = 0;
  s_hushX = startX - 330;
  s_lamps = 0; s_motes = 0;
  s_trailN = 0; s_trailHead = 0;
  s_camX = s_x - SHIP_X; s_camY = s_y - H / 2;
  s_lastX = startX;
  spawnBody(true);
  while (s_lastX < s_camX + W + 420) spawnBody(false);
  if (s_region != regionAt(startX)) { s_region = regionAt(startX); makeNebula(s_region); }
  s_bannerT = 3;
  s_clock = 0;
  s_shake = 0;
  s_newBest = false;
}

static uint32_t score() { return (uint32_t)s_lamps * 25 + (uint32_t)s_motes * 5 + (uint32_t)(s_maxX / 50); }

static void crash(const char *why) {
  s_why = why;
  spark(s_x, s_y, 34, 0xffd8a0, 120);
  spark(s_x, s_y, 14, 0xff6a3a, 70);
  s_shake = 9;
  RunAudio::fx(RunAudio::Thunder);
  s_orbit = -1;
  setMode(Mode::Dying);
  if (s_auto) return; // the bench autopilot's runs aren't kept
  uint32_t sc = score();
  if (sc > s_best) { s_best = sc; s_newBest = true; s_prefs.putUInt("best", s_best); }
  if ((uint32_t)s_lamps > s_bestLamps) { s_bestLamps = s_lamps; s_prefs.putUInt("lamps", s_bestLamps); }
}

static void capture(int i) {
  Body &b = s_b[i];
  float dx = s_x - b.x, dy = s_y - b.y;
  float d = sqrtf(dx * dx + dy * dy);
  s_orbit = i;
  s_oa = atan2f(dy, dx);
  s_or = d;
  s_orT = clampf(d, b.r + 13, b.field - 4);
  float vx = cosf(s_ang), vy = sinf(s_ang);
  s_osign = dx * vy - dy * vx >= 0 ? 1 : -1;
  s_orbitT = 0;
  RunAudio::fx(RunAudio::Tick);
  if (!b.lit) {
    b.lit = true;
    b.litT = 0;
    s_lamps++;
    spark(b.x, b.y - b.r - 4, 16, 0xffe08a, 60);
    RunAudio::fx(RunAudio::DingHigh);
  }
}

// The autopilot (bench only): hold to catch each world ahead, let go when
// the orbit points the Moth past the next one's edge.
static bool pilot() {
  if (s_orbit >= 0) {
    const Body &b = s_b[s_orbit];
    int tgt = -1;
    float best = 1e9f;
    for (int j = 0; j < NB; j++) {
      if (!s_b[j].on || j == s_orbit || s_b[j].x < b.x + 30) continue;
      float dx = s_b[j].x - b.x, dy = s_b[j].y - b.y, d = dx * dx + dy * dy;
      if (d < best) { best = d; tgt = j; }
    }
    if (tgt < 0 || s_orbitT < 0.2f) return true;
    const Body &t = s_b[tgt];
    float dx = t.x - s_x, dy = t.y - s_y, d = sqrtf(dx * dx + dy * dy);
    float pass = (t.r + t.field) * 0.5f;
    float ax = t.x - dy / d * pass * s_osign, ay = t.y + dx / d * pass * s_osign;
    float err = wrapAng(atan2f(ay - s_y, ax - s_x) - s_ang);
    return !(fabsf(err) < 0.09f || s_orbitT > 6);
  }
  for (int i = 0; i < NB; i++) {
    const Body &b = s_b[i];
    if (!b.on) continue;
    float dx = s_x - b.x, dy = s_y - b.y, d2 = dx * dx + dy * dy;
    if (d2 > b.field * b.field) continue;
    if (i != s_lastBody && b.x > s_x - b.field * 0.5f) return true;
    if (d2 < (b.r + 26) * (b.r + 26)) return true; // falling in: hold on
  }
  return false;
}

static void update(float dt) {
  s_t += dt;
  s_modeT += dt;
  s_shake = fmaxf(0, s_shake - dt * 14);
  if (s_bannerT > 0) s_bannerT -= dt;
  for (Spark &p : s_spark) {
    if (p.life <= 0) continue;
    p.life -= dt;
    p.x += p.vx * dt; p.y += p.vy * dt;
    p.vx *= 1 - dt * 1.5f; p.vy *= 1 - dt * 1.5f;
  }
  for (int i = 0; i < NB; i++) {
    Body &b = s_b[i];
    if (!b.on) continue;
    b.spin += b.spinW * dt;
    if (b.moon) b.moonA += b.moonW * dt;
    if (b.lit) b.litT += dt;
  }
  for (Rock &k : s_rock) {
    if (!k.on) continue;
    k.x += k.vx * dt; k.y += k.vy * dt; k.a += k.w * dt;
    if (k.x < s_camX - 300 || k.x < s_hushX - 200 || fabsf(k.y) > EDGE + 300) k.on = false;
  }
  if (s_mode == Mode::Dying) {
    if (s_modeT > 1.3f) setMode(Mode::Over);
    return;
  }
  if (s_mode != Mode::Play) return;
  s_clock += dt;
  s_v = speedNow();
  bool hold = s_hold || (s_auto && pilot());
  s_releasedT += dt;

  if (s_orbit >= 0) {
    Body &b = s_b[s_orbit];
    s_orbitT += dt;
    if (!hold || !b.on) {
      s_lastBody = s_orbit;
      s_orbit = -1;
      s_releasedT = 0;
      RunAudio::fx(RunAudio::Hop);
    } else {
      // Circular orbit: the angle advances at v / r; the wheel moves the
      // orbit in or out, and the radius eases toward it.
      if (s_steer) { s_orT = clampf(s_orT + s_steer * 5, b.r + 13, b.field - 4); s_steer = 0; }
      s_or += (s_orT - s_or) * fminf(1, dt * 5);
      s_oa += s_osign * s_v / s_or * dt;
      s_x = b.x + cosf(s_oa) * s_or;
      s_y = b.y + sinf(s_oa) * s_or;
      s_ang = s_oa + s_osign * PI_F / 2;
    }
  }
  if (s_orbit < 0) {
    if (hold) { // catch the nearest field we're inside
      int best = -1;
      float bd = 1e9f;
      for (int i = 0; i < NB; i++) {
        const Body &b = s_b[i];
        if (!b.on || (i == s_lastBody && s_releasedT < 0.35f)) continue;
        float dx = s_x - b.x, dy = s_y - b.y, d2 = dx * dx + dy * dy;
        if (d2 < b.field * b.field && d2 < bd) { bd = d2; best = i; }
      }
      if (best >= 0) capture(best);
    }
  }
  if (s_orbit < 0) {
    // Free flight: gravity bends the path, the wheel steers a little, the
    // aurora at the lane's edges turns her back.
    float vx = cosf(s_ang) * s_v, vy = sinf(s_ang) * s_v, ax = 0, ay = 0;
    for (int i = 0; i < NB; i++) {
      const Body &b = s_b[i];
      if (!b.on) continue;
      float dx = b.x - s_x, dy = b.y - s_y, d2 = dx * dx + dy * dy;
      if (d2 > b.field * b.field) continue;
      float d = sqrtf(d2), a = b.pull * s_v * s_v * b.r / fmaxf(d2, b.r * b.r);
      ax += dx / d * a; ay += dy / d * a;
    }
    if (fabsf(s_y) > EDGE) ay -= (s_y > 0 ? 1 : -1) * (fabsf(s_y) - EDGE) * 9;
    vx += ax * dt; vy += ay * dt;
    s_ang = atan2f(vy, vx) + s_steer * 0.06f;
    s_steer = 0;
    s_x += cosf(s_ang) * s_v * dt;
    s_y += sinf(s_ang) * s_v * dt;
  }
  s_maxX = fmaxf(s_maxX, s_x);
  s_lap = lapAt(s_maxX);

  // Collisions.
  for (int i = 0; i < NB; i++) {
    const Body &b = s_b[i];
    if (!b.on) continue;
    float dx = b.x - s_x, dy = b.y - s_y, d2 = dx * dx + dy * dy;
    float hit = b.kind == B_SUN ? b.r * 1.08f + 4 : b.r + 3;
    if (d2 < hit * hit) {
      crash(b.kind == B_SUN ? "Too close to a star." : b.kind == B_HOLE ? "Lost in the dark star." : "The Moth crashed into a world.");
      return;
    }
    if (b.moon) {
      float mx = b.x + cosf(b.moonA) * b.moonR - s_x, my = b.y + sinf(b.moonA) * b.moonR - s_y;
      if (mx * mx + my * my < (b.moonS + 4) * (b.moonS + 4)) { crash("A moon was in the way."); return; }
    }
  }
  for (Rock &k : s_rock) {
    if (!k.on) continue;
    float dx = k.x - s_x, dy = k.y - s_y, rr = k.r * 0.85f + 4;
    if (dx * dx + dy * dy < rr * rr) { crash(k.comet ? "A comet hit the Moth." : "The Moth hit a rock."); return; }
  }
  for (Mote &m : s_mote) {
    if (!m.on) continue;
    m.t += dt;
    float dx = m.x - s_x, dy = m.y - s_y;
    if (dx * dx + dy * dy < 14 * 14) {
      m.on = false;
      s_motes++;
      spark(m.x, m.y, 6, 0xfff0b0, 40);
      RunAudio::fx(RunAudio::PopSoft);
    } else if (m.x < s_hushX - 100) {
      m.on = false;
    }
  }

  // The Hush: never far behind. Close, it creeps at half her speed; left far
  // behind, it rushes to catch up - so lingering in an orbit costs ground.
  float gap = s_x - s_hushX;
  float k = 0.5f + 0.06f * fminf(s_lap, 3) + clampf((gap - 230) / 320, 0, 0.7f);
  s_hushX += s_v * k * dt;
  if (gap < 170) {
    if (s_warnT <= 0) RunAudio::fx(RunAudio::Hoot);
    s_warnT = 3;
  } else if (s_warnT > 0) {
    s_warnT -= dt;
  }
  if (s_x < s_hushX + 6) { crash("The Hush caught the Moth."); return; }

  // Regions.
  int reg = regionAt(s_x);
  if (reg != s_region) {
    s_region = reg;
    makeNebula(reg);
    s_bannerT = 3.5f;
    RunAudio::fx(RunAudio::DingTop);
  }

  // The sky ahead, and forgetting what's behind.
  while (s_lastX < s_camX + W + 420) spawnBody(false);
  for (Body &b : s_b)
    if (b.on && b.x + b.field < fminf(s_camX - 120, s_hushX - 60) && &b - s_b != s_orbit) b.on = false;

  // The trail, sampled every few hundredths of a second.
  s_trailT += dt;
  if (s_trailT >= 0.03f) {
    s_trailT = 0;
    s_trailX[s_trailHead] = s_x; s_trailY[s_trailHead] = s_y;
    s_trailHead = (s_trailHead + 1) % TRAIL;
    if (s_trailN < TRAIL) s_trailN++;
  }

  // The camera leads a little in the direction of flight.
  float tx = s_x - SHIP_X + cosf(s_ang) * 50, ty = s_y - H / 2 + sinf(s_ang) * 30;
  s_camX += (tx - s_camX) * fminf(1, dt * 3);
  s_camY += (ty - s_camY) * fminf(1, dt * 3);
  s_camY = clampf(s_camY, -EDGE - 90, EDGE + 90 - H);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// The frame is drawn a strip at a time: every function below draws only
// the rows of the current Raster target, and reads state that prepare()
// fixed for the frame without changing any of it - so the two cores can
// each draw a different strip at once.
static float s_ox = 0, s_oy = 0;     // world -> screen: x - s_ox
static float s_skyX = 0, s_skyY = 0; // the camera the sky's layers scroll with
static float s_shipX = 0, s_shipY = 0, s_shipAng = 0; // where the Moth is drawn (world)
static Body s_home;                  // the title screen's world

// RGB565 spread out as (c | c << 16) & 0x07E0F81F leaves a gap above each
// channel, so a blend (a (32 - f) + b f) >> 5 does all three at once.
static const uint32_t SPREAD = 0x07E0F81F;
static inline uint32_t spread(uint16_t c) { return ((uint32_t)c | (uint32_t)c << 16) & SPREAD; }
static inline uint16_t unspread(uint32_t c) { return (uint16_t)(c | c >> 16); }
static inline uint32_t lerp5(uint32_t a, uint32_t b, uint32_t f) { return ((a * (32 - f) + b * f) >> 5) & SPREAD; }

// Bench profile: microseconds per section, per core (see benchStats).
static volatile uint32_t s_sec[2][8];
#ifndef PC_SIM
static inline int coreNo() { return xPortGetCoreID() & 1; }
#else
static inline int coreNo() { return 0; }
#endif
struct SecTimer {
  int i; uint32_t t0;
  SecTimer(int i_) : i(i_), t0(micros()) {}
  ~SecTimer() { s_sec[coreNo()][i] += micros() - t0; }
};

// sin from a 1024-step table, linearly interpolated: plenty for drawing,
// and many times cheaper than sinf where it runs per pixel or per column.
static float s_sinTab[1025];
static void makeSinTab() {
  for (int i = 0; i <= 1024; i++) s_sinTab[i] = sinf(i * 2 * PI_F / 1024);
}
static inline float fsin(float x) {
  float t = x * (1024 / (2 * PI_F));
  float fl = floorf(t);
  int i = (int)fl & 1023;
  return s_sinTab[i] + (s_sinTab[i + 1] - s_sinTab[i]) * (t - fl);
}
static inline float fcos(float x) { return fsin(x + PI_F / 2); }

// Per frame, from prepare(): the stars on screen and the aurora's wave.
struct SkyStar { float x, y, r, halo; uint16_t c; uint8_t a; };
static SkyStar s_stars[88];
static int s_starN = 0;
static int16_t s_aurEdge[2][W]; // each curtain's edge per column, in 1/8 px
static uint8_t s_aurRay[2][W];  // and its rays' brightness
static bool s_aurOn[2];
static uint8_t s_aurAlpha[24 + 56 * 8]; // brightness by depth into the curtain, -24..447 (1/8 px)

static void prepareSky() {
  s_starN = 0;
  for (int layer = 0; layer < 2; layer++) {
    float par = layer ? 0.32f : 0.12f, tw = W * 1.6f, th = H * 2.2f;
    int n = layer ? 28 : 60;
    for (int i = 0; i < n; i++) {
      float hx = (hash32(i, layer * 3 + 1, 7) & 0xffff) / 65535.0f * tw, hy = (hash32(i, layer * 3 + 2, 7) & 0xffff) / 65535.0f * th;
      float sx = fmodf(hx - s_skyX * par, tw), sy = fmodf(hy - s_skyY * par, th);
      if (sx < 0) sx += tw;
      if (sy < 0) sy += th;
      sy -= (th - H) / 2;
      if (sx >= W + 4 || sy < -4 || sy >= H + 4) continue;
      float b = 0.55f + 0.45f * fsin(s_t * (1.5f + (i % 5)) + i);
      bool big = layer && (i % 4 == 0);
      SkyStar &s = s_stars[s_starN++];
      s.x = sx; s.y = sy;
      s.r = big ? 1.5f : layer ? 1.1f : 0.85f;
      s.halo = big ? b * 55 : 0;
      s.c = rgb(i % 7 == 0 ? 0xffd8a8 : i % 5 == 0 ? 0xb8d8ff : 0xffffff);
      s.a = (uint8_t)(b * (layer ? 255 : 190));
    }
  }
  if (!s_aurAlpha[24]) // (fixed: bright at the edge, a soft fringe inside the lane)
    for (int d = -24; d < 56 * 8; d++) s_aurAlpha[d + 24] = (uint8_t)(d < 0 ? (24 + d) * 95 / 24 : 95 - d * 95 / (56 * 8));
  for (int k = 0; k < 2; k++) {
    int side = k ? 1 : -1;
    float base = side * (EDGE + 40) - s_oy;
    s_aurOn[k] = base > -100 && base < H + 100;
    if (!s_aurOn[k]) continue;
    for (int x = 0; x < W; x++) {
      float wx = x + s_skyX;
      s_aurEdge[k][x] = (int16_t)((base + fsin(wx * 0.021f + s_t * 1.3f) * 9 + fsin(wx * 0.05f - s_t) * 4) * 8);
      s_aurRay[k][x] = (uint8_t)(200 + 55 * fsin(wx * 0.13f + s_t * 0.8f) * fsin(wx * 0.031f - s_t * 0.5f));
    }
  }
}

// The nebula, scrolling slowly (far away), sampled bilinearly: each row
// first blends its two texture rows, then the screen. The texture is
// stretched about 4x, so every other pixel is sampled and the ones between
// are the average of their neighbours.
static void drawNebula() {
  SecTimer tm(0);
  int y0 = top(), y1 = top() + height();
  float ox = s_skyX * 0.05f, oy = s_skyY * 0.04f;
  const float uScale = NT_W / (W * 1.5f);
  float u0 = fmodf(ox * uScale, (float)NT_W);
  if (u0 < 0) u0 += NT_W;
  const uint32_t du = (uint32_t)(uScale * 2 * 65536), wrap = (uint32_t)NT_W << 16;
  uint32_t texRow[NT_W + 1];
  for (int py = y0; py < y1; py++) {
    float tyf = (py + oy + 60) * NT_H / (float)(H + 120) - 0.5f;
    if (tyf < 0) tyf = 0;
    if (tyf > NT_H - 1) tyf = NT_H - 1;
    int ty = (int)tyf, ty1 = ty + 1 < NT_H ? ty + 1 : ty;
    uint32_t fy = (uint32_t)((tyf - ty) * 32);
    const uint16_t *a = s_neb + ty * NT_W, *b = s_neb + ty1 * NT_W;
    for (int x = 0; x < NT_W; x++) texRow[x] = lerp5(spread(a[x]), spread(b[x]), fy);
    texRow[NT_W] = texRow[0];
    uint16_t *r = row(py);
    uint32_t u = (uint32_t)(u0 * 65536);
    uint32_t prev = lerp5(texRow[u >> 16], texRow[(u >> 16) + 1], (u >> 11) & 31);
    for (int px = 0; px < W; px += 2) {
      r[px] = unspread(prev);
      u += du;
      if (u >= wrap) u -= wrap;
      uint32_t next = lerp5(texRow[u >> 16], texRow[(u >> 16) + 1], (u >> 11) & 31);
      r[px + 1] = unspread(((prev + next) >> 1) & SPREAD);
      prev = next;
    }
  }
}

// Two layers of stars at different depths, twinkling; the near ones that
// are big carry a halo.
static void drawStars() {
  SecTimer tm(1);
  float y0 = top() - 4, y1 = top() + height() + 4;
  for (int i = 0; i < s_starN; i++) {
    const SkyStar &s = s_stars[i];
    if (s.y < y0 || s.y >= y1) continue;
    if (s.halo > 0) circle(s.x, s.y, 3.4f, s.c, (uint8_t)s.halo);
    circle(s.x, s.y, s.r, s.c, s.a);
  }
}

// The aurora that walls the lane: a curtain hanging off a wavy edge,
// brightest at the edge, fading away from the lane, combed with faint rays.
static void drawAurora() {
  SecTimer tm(2);
  int y0 = top(), y1 = top() + height();
  for (int k = 0; k < 2; k++) {
    if (!s_aurOn[k]) continue;
    int side = k ? 1 : -1;
    float base = side * (EDGE + 40) - s_oy;
    float lo = side < 0 ? base - 76 : base - 18, hi = side < 0 ? base + 18 : base + 76;
    if (hi < y0 || lo >= y1) continue;
    uint32_t c = spread(rgb(side < 0 ? 0x3affa8 : 0xff7ad0));
    const int16_t *edge = s_aurEdge[k];
    const uint8_t *ray = s_aurRay[k];
    int ya = (int)lo < y0 ? y0 : (int)lo, yb = (int)hi + 1 > y1 ? y1 : (int)hi + 1;
    for (int py = ya; py < yb; py++) {
      uint16_t *r = row(py);
      int py8 = py * 8 + 4;
      for (int x = 0; x < W; x++) {
        int d = side < 0 ? edge[x] - py8 : py8 - edge[x]; // depth into the curtain
        if (d <= -24 || d >= 56 * 8) continue;
        uint32_t a5 = (s_aurAlpha[d + 24] * ray[x]) >> 11; // 0..32
        if (a5) r[x] = unspread(lerp5(spread(r[x]), c, a5));
      }
    }
  }
}

static void drawSky() {
  drawNebula();
  drawStars();
  drawAurora();
}

static const float LX = -0.55f, LY = -0.5f, LZ = 0.67f; // light from the upper left

// A world, shaded per pixel: z = sqrt(1 - d^2) on the unit disc, Lambert
// light I = max(0, n.L), a pseudo-longitude u = dx / (z + 0.35) + spin that
// slides surface detail round as it turns, and a rim of atmosphere. The
// edge is smoothed: a pixel the limb crosses is blended by how far inside
// it its centre lies, (1 - d) R + 1/2.
static void drawWorld(const Body &b, float cx, float cy) {
  float R = b.r, iR = 1 / R;
  int y0 = (int)floorf(cy - R - 1), y1 = (int)ceilf(cy + R + 1);
  int x0 = (int)floorf(cx - R - 1), x1 = (int)ceilf(cx + R + 1);
  if (y0 < top()) y0 = top();
  if (y1 > top() + height()) y1 = top() + height();
  if (x0 < 0) x0 = 0;
  if (x1 > W) x1 = W;
  if (y1 <= y0 || x1 <= x0) return;
  float r0, g0, b0, r1, g1, b1, ra, ga, ba;
  unpack(b.c0, r0, g0, b0); unpack(b.c1, r1, g1, b1); unpack(b.atm, ra, ga, ba);
  uint32_t seed = (uint32_t)(b.seed * 1000);
  float inner = (1 - 1.5f * iR) * (1 - 1.5f * iR), outer = (1 + 0.5f * iR) * (1 + 0.5f * iR);
  for (int py = y0; py < y1; py++) {
    float dy = (py + 0.5f - cy) * iR;
    uint16_t *row_ = row(py);
    for (int px = x0; px < x1; px++) {
      float dx = (px + 0.5f - cx) * iR, d2 = dx * dx + dy * dy;
      if (d2 >= outer) continue;
      float cov = 1;
      if (d2 > inner) {
        cov = (1 - sqrtf(d2)) * R + 0.5f;
        if (cov <= 0) continue;
        if (cov > 1) cov = 1;
        if (d2 > 0.999f) d2 = 0.999f;
      }
      float z = sqrtf(1 - d2);
      float diff = dx * LX + dy * LY + z * LZ;
      float lit = 0.1f + 0.95f * (diff > 0 ? diff : 0);
      float u = dx / (z + 0.35f) + b.spin;
      float t = 0, glow = 0;
      switch (b.kind) {
        case B_GAS:
        case B_RING:
          t = 0.5f + 0.5f * fsin(dy * b.bands + 0.9f * fsin(u * 1.6f + dy * 3 + b.seed));
          break;
        case B_ICE:
          t = vnoise(u * 3 + b.seed, dy * 2.2f, seed) * 0.7f;
          if (fabsf(dy) > 0.72f) t = 0; // polar caps
          break;
        case B_LAVA: {
          float n = vnoise(u * 2.6f + b.seed, dy * 2.6f, seed);
          t = n;
          float e = fabsf(n - 0.5f);
          if (e < 0.06f) glow = 1 - e / 0.06f;
          break;
        }
        default: {
          t = vnoise(u * 2.4f + b.seed, dy * 2.8f, seed);
          for (int i = 0; i < 3; i++) {
            float qx = dx - b.crX[i], qy = dy - b.crY[i], q = (qx * qx + qy * qy) / (b.crR[i] * b.crR[i]);
            if (q < 1) t = q > 0.7f ? t * 0.6f : t + 0.35f; // floor darker (t up = darker colour), rim lit
          }
          break;
        }
      }
      t = clampf(t, 0, 1);
      float cr = lerpf(r0, r1, t) * lit + 6 * (1 - lit), cg = lerpf(g0, g1, t) * lit + 8 * (1 - lit), cb = lerpf(b0, b1, t) * lit + 22 * (1 - lit);
      if (d2 > 0.72f) { float rim = (d2 - 0.72f) / 0.28f * (0.25f + lit * 0.6f); cr += ra * rim * 0.5f; cg += ga * rim * 0.5f; cb += ba * rim * 0.5f; }
      if (glow > 0) { cr += 255 * glow; cg += 120 * glow; cb += 30 * glow; }
      uint16_t c = pack(cr, cg, cb);
      row_[px] = cov >= 1 ? c : blend(row_[px], c, (uint8_t)(cov * 255));
    }
  }
}

// A star: layered glow, then a limb-darkened disc with boiling granules.
// Unlit, it is a sleeping ember; lit, it blazes.
static void drawSun(const Body &b, float sx, float sy) {
  float wake = b.lit ? clampf(b.litT * 1.5f, 0, 1) : 0;
  uint32_t edge = mixHex(0x6a3020, b.c0, 0.35f + 0.65f * wake), core = mixHex(0xa86040, b.c1, 0.3f + 0.7f * wake);
  float fl = 1 + 0.05f * fsin(s_t * 7 + b.seed) * wake;
  circle(sx, sy, b.r * 2.3f * fl, rgb(edge), (uint8_t)(18 + 22 * wake));
  circle(sx, sy, b.r * 1.7f * fl, rgb(edge), (uint8_t)(30 + 40 * wake));
  circle(sx, sy, b.r * 1.3f * fl, rgb(core), (uint8_t)(40 + 60 * wake));
  for (int i = 0; i < 10 && wake > 0; i++) { // corona streamers
    float a = i * 0.628f + s_t * 0.15f + b.seed, len = b.r * (1.35f + 0.25f * fsin(s_t * 3 + i * 1.7f));
    line(sx + fcos(a) * b.r, sy + fsin(a) * b.r, sx + fcos(a) * len, sy + fsin(a) * len, 1.6f, rgb(core), (uint8_t)(120 * wake));
  }
  float R = b.r, iR = 1 / R;
  int y0 = (int)floorf(sy - R - 1), y1 = (int)ceilf(sy + R + 1);
  int x0 = (int)floorf(sx - R - 1), x1 = (int)ceilf(sx + R + 1);
  if (y0 < top()) y0 = top();
  if (y1 > top() + height()) y1 = top() + height();
  if (x0 < 0) x0 = 0;
  if (x1 > W) x1 = W;
  if (y1 <= y0 || x1 <= x0) return;
  float r0, g0, b0, r1, g1, b1;
  unpack(edge, r0, g0, b0); unpack(core, r1, g1, b1);
  uint32_t seed = (uint32_t)(b.seed * 1000);
  float inner = (1 - 1.5f * iR) * (1 - 1.5f * iR), outer = (1 + 0.5f * iR) * (1 + 0.5f * iR);
  for (int py = y0; py < y1; py++) {
    float dy = (py + 0.5f - sy) * iR;
    uint16_t *row_ = row(py);
    for (int px = x0; px < x1; px++) {
      float dx = (px + 0.5f - sx) * iR, d2 = dx * dx + dy * dy;
      if (d2 >= outer) continue;
      float cov = 1;
      if (d2 > inner) {
        cov = (1 - sqrtf(d2)) * R + 0.5f;
        if (cov <= 0) continue;
        if (cov > 1) cov = 1;
      }
      float g = vnoise(dx * 4 + s_t * 0.6f, dy * 4 - s_t * 0.4f, seed) * 0.35f;
      float t = clampf(d2 * d2 + g, 0, 1);
      uint16_t c = pack(lerpf(r1, r0, t), lerpf(g1, g0, t), lerpf(b1, b0, t));
      row_[px] = cov >= 1 ? c : blend(row_[px], c, (uint8_t)(cov * 255));
    }
  }
}

// A dark star: the accretion disc behind, the black heart with its bright
// photon ring, then the near half of the disc across it.
static void diskHalf(const Body &b, float sx, float sy, bool front, float glow) {
  for (int ring = 0; ring < 3; ring++) {
    float rr = b.r * (1.7f + ring * 0.45f);
    uint32_t c = ring == 0 ? 0xfff0c8 : ring == 1 ? 0xffa040 : 0xc04a20;
    float px = 0, py = 0;
    for (int i = 0; i <= 24; i++) {
      float a = (front ? 0 : PI_F) + i * PI_F / 24;
      float ex = fcos(a) * rr, ey = fsin(a) * rr * 0.28f;
      float x = sx + ex * fcos(b.ringA) - ey * fsin(b.ringA), y = sy + ex * fsin(b.ringA) + ey * fcos(b.ringA);
      if (i) line(px, py, x, y, 2.4f - ring * 0.4f, rgb(c), (uint8_t)(150 * glow + 60));
      px = x; py = y;
    }
  }
}

static void drawHole(const Body &b, float sx, float sy) {
  float glow = b.lit ? 1 : 0.55f;
  circle(sx, sy, b.r * 3, rgb(0x2a1440), 50);
  diskHalf(b, sx, sy, false, glow);
  circle(sx, sy, b.r + 2, rgb(0xffd8a0), (uint8_t)(200 * glow));
  circle(sx, sy, b.r, rgb(0x000000));
  diskHalf(b, sx, sy, true, glow);
}

// Rings: the far half before the world, the near half after.
static void ringHalf(const Body &b, float sx, float sy, bool front) {
  for (int band = 0; band < 3; band++) {
    float rr = b.r * (1.45f + band * 0.28f);
    uint32_t c = mixHex(b.c0, 0xfff4e0, 0.3f + band * 0.15f);
    float px = 0, py = 0;
    for (int i = 0; i <= 20; i++) {
      float a = (front ? 0 : PI_F) + i * PI_F / 20;
      float ex = fcos(a) * rr, ey = fsin(a) * rr * 0.3f;
      float x = sx + ex * fcos(b.ringA) - ey * fsin(b.ringA), y = sy + ex * fsin(b.ringA) + ey * fcos(b.ringA);
      if (i) line(px, py, x, y, band == 1 ? 3.2f : 2, rgb(c), front ? 230 : 150);
      px = x; py = y;
    }
  }
}

// Each world's lamp: a little tower on top; lit, it glows and sweeps a
// lighthouse beam across the dark.
static void drawLamp(const Body &b, float sx, float sy) {
  if (b.kind == B_SUN || b.kind == B_HOLE) return;
  float lx = sx, ly = sy - b.r;
  rect(lx - 1.5f, ly - 7, 3, 7, rgb(0x2a2420));
  if (b.lit) {
    float a = b.litT * 1.6f + b.seed, len = 80;
    float tri[6] = {lx, ly - 8, lx + fcos(a - 0.12f) * len, ly - 8 + fsin(a - 0.12f) * len, lx + fcos(a + 0.12f) * len, ly - 8 + fsin(a + 0.12f) * len};
    poly(tri, 3, rgb(0xffe8a0), 40);
    circle(lx, ly - 9, 7 + fsin(s_t * 5) * 1.2f, rgb(0xffd060), 70);
    circle(lx, ly - 9, 3, rgb(0xfff4c8));
  } else {
    circle(lx, ly - 9, 2.6f, rgb(0x5a5048));
  }
}

static void drawBody(const Body &b) {
  float sx = b.x - s_ox, sy = b.y - s_oy;
  if (sx < -b.field - 10 || sx > W + b.field + 10 || sy < -b.field - 10 || sy > H + b.field + 10) return;
  // Nothing of it reaches this strip: its field, glow, rings, lamp beam, moon.
  float reach = fmaxf(fmaxf(b.field, b.r * 3.1f), b.r + 95) + 12;
  if (sy + reach < top() || sy - reach > top() + height()) return;
  // The gravity field: a ring of dashes turning slowly, brighter when she's in it.
  float dx = s_shipX - b.x, dy = s_shipY - b.y;
  bool inside = dx * dx + dy * dy < b.field * b.field;
  bool held = s_mode != Mode::Title && s_orbit >= 0 && &b == &s_b[s_orbit];
  uint32_t ac = REG[regionAt(b.x)].accent;
  int dashes = (int)(b.field / 5);
  for (int i = 0; i < dashes; i++) {
    float a0 = i * 6.283f / dashes + s_t * 0.25f * (b.spinW > 0 ? 1 : -1), a1 = a0 + 3.0f / dashes;
    line(sx + fcos(a0) * b.field, sy + fsin(a0) * b.field, sx + fcos(a1) * b.field, sy + fsin(a1) * b.field, held ? 1.6f : 1.1f,
         rgb(held ? 0xfff0c0 : ac), held ? 230 : inside ? 170 : 85);
  }
  if (b.lit && b.kind != B_SUN && b.kind != B_HOLE) circle(sx, sy, b.r * 1.35f, rgb(0xffd890), (uint8_t)(34 + 10 * fsin(s_t * 3)));
  if (b.kind == B_SUN) { drawSun(b, sx, sy); return; }
  if (b.kind == B_HOLE) { drawHole(b, sx, sy); return; }
  if (b.kind == B_RING) ringHalf(b, sx, sy, false);
  drawWorld(b, sx, sy);
  if (b.kind == B_RING) ringHalf(b, sx, sy, true);
  drawLamp(b, sx, sy);
  if (b.moon) {
    float mx = sx + fcos(b.moonA) * b.moonR, my = sy + fsin(b.moonA) * b.moonR;
    circle(mx, my, b.moonS, rgb(0x8a8478));
    circle(mx - b.moonS * 0.3f, my - b.moonS * 0.3f, b.moonS * 0.6f, rgb(0xc8c0b0));
    circle(mx + b.moonS * 0.25f, my + b.moonS * 0.1f, b.moonS * 0.25f, rgb(0x6a645a));
  }
}

static void drawRock(const Rock &k) {
  float sx = k.x - s_ox, sy = k.y - s_oy;
  if (sx < -40 || sx > W + 40 || sy < -40 || sy > H + 40) return;
  if (k.comet) { // a bright head and a long tail streaming behind it
    float sp = sqrtf(k.vx * k.vx + k.vy * k.vy), ux = -k.vx / sp, uy = -k.vy / sp;
    if (sy + uy * 48 + 12 < top() && sy - 12 < top()) return;
    for (int i = 8; i >= 1; i--) circle(sx + ux * i * 6, sy + uy * i * 6, k.r * (1 - i * 0.08f), rgb(0x8ad0ff), (uint8_t)(150 - i * 16));
    circle(sx, sy, k.r * 1.5f, rgb(0xd8f0ff), 90);
    circle(sx, sy, k.r, rgb(0xf4fbff));
    return;
  }
  if (sy + k.r * 1.3f + 2 < top() || sy - k.r * 1.3f - 2 > top() + height()) return;
  float p[16], q[16];
  for (int j = 0; j < 8; j++) {
    float a = k.a + j * 0.785f, rr = k.r * (0.8f + k.shape[j] / 210.0f);
    p[j * 2] = sx + fcos(a) * rr; p[j * 2 + 1] = sy + fsin(a) * rr;
    q[j * 2] = sx - k.r * 0.18f + fcos(a) * rr * 0.6f; q[j * 2 + 1] = sy - k.r * 0.18f + fsin(a) * rr * 0.6f;
  }
  for (int j = 0; j < 8; j++) { p[j * 2] += 1.5f; p[j * 2 + 1] += 1.5f; }
  poly(p, 8, rgb(0x0a0806), 120); // shadow
  for (int j = 0; j < 8; j++) { p[j * 2] -= 1.5f; p[j * 2 + 1] -= 1.5f; }
  poly(p, 8, rgb(0x5a4c40));
  poly(q, 8, rgb(0x8a7a68));
  circle(sx + fcos(k.a + 1) * k.r * 0.35f, sy + fsin(k.a + 1) * k.r * 0.35f, k.r * 0.2f, rgb(0x3a3028));
}

static void drawShip(float sx, float sy, float ang, float flame) {
  if (sy + 26 < top() || sy - 26 > top() + height()) return;
  float ca = fcos(ang) * 1.3f, sa = fsin(ang) * 1.3f;
  auto P = [&](float lx, float ly, float *o) { o[0] = sx + lx * ca - ly * sa; o[1] = sy + lx * sa + ly * ca; };
  if (flame > 0) {
    float f = 7 + flame * (4 + 3 * fsin(s_t * 40));
    float fl[6];
    P(-4, -2.5f, fl); P(-4 - f, 0, fl + 2); P(-4, 2.5f, fl + 4);
    poly(fl, 3, rgb(0xff8a3a));
    P(-4, -1.2f, fl); P(-4 - f * 0.55f, 0, fl + 2); P(-4, 1.2f, fl + 4);
    poly(fl, 3, rgb(0xfff0a0));
  }
  float body[12];
  P(11, 0, body); P(2, -4, body + 2); P(-5, -4, body + 4); P(-5, 4, body + 6); P(2, 4, body + 8);
  poly(body, 5, rgb(0xf4ecdc));
  float wing[6];
  P(1, -3, wing); P(-7, -9, wing + 2); P(-5, -3, wing + 4);
  poly(wing, 3, rgb(0xe86a5a));
  P(1, 3, wing); P(-7, 9, wing + 2); P(-5, 3, wing + 4);
  poly(wing, 3, rgb(0xe86a5a));
  float c[2];
  P(4, 0, c);
  circle(c[0], c[1], 2.4f, rgb(0x3a7ad8));
  circle(c[0] - 0.6f, c[1] - 0.6f, 0.9f, rgb(0xd8f0ff));
}

static void drawTrail() {
  // Two neon threads behind the Moth, fading with age.
  for (int pass = 0; pass < 2; pass++) {
    uint32_t c = pass ? 0x5af0ff : 0xff5ad8;
    float off = pass ? 1.6f : -1.6f;
    for (int i = 1; i < s_trailN; i++) {
      int a = (s_trailHead - i + TRAIL) % TRAIL, b = (s_trailHead - i - 1 + TRAIL) % TRAIL;
      if (i == s_trailN - 1) break;
      float ax = s_trailX[a], ay = s_trailY[a], bx = s_trailX[b], by = s_trailY[b];
      float dx = ax - bx, dy = ay - by, d = sqrtf(dx * dx + dy * dy) + 0.01f;
      float nx = -dy / d * off, ny = dx / d * off;
      float age = i / (float)s_trailN;
      line(ax + nx - s_ox, ay + ny - s_oy, bx + nx - s_ox, by + ny - s_oy, 1.6f, rgb(c), (uint8_t)(220 * (1 - age)));
    }
  }
}

// The left edge of the dark, x = e on each row: solid up to it, the pixel
// it crosses by coverage, then a violet glow fading over `glowW` pixels.
static void hushRow(int py, float e, float glowW, int glowA) {
  uint16_t *r = row(py);
  if (!r || e < -glowW) return;
  uint16_t dark = rgb(0x050309), glow = rgb(0x2a1450);
  int edge = (int)floorf(e);
  for (int x = 0; x < edge && x < W; x++) r[x] = dark;
  if (edge >= 0 && edge < W) r[edge] = blend(r[edge], dark, (uint8_t)((e - edge) * 255));
  for (int x = edge + 1 < 0 ? 0 : edge + 1; x < W && x < e + glowW; x++) {
    int a = (int)(glowA * (1 - (x + 0.5f - e) / glowW));
    if (a > 0) r[x] = blend(r[x], glow, (uint8_t)a);
  }
}

// The Hush: a wall of dark with a ragged, breathing edge.
static void drawHush() {
  float hx = s_hushX - s_ox;
  if (hx < -70) return;
  for (int py = top(); py < top() + height(); py++) {
    float wy = py + s_camY;
    hushRow(py, hx + (vnoise(wy * 0.02f, s_t * 0.7f, s_noiseSeed) - 0.5f) * 50 + fsin(wy * 0.05f + s_t * 2) * 6, 40, 150);
  }
  for (int i = 0; i < 8; i++) { // stars it has put to sleep, drifting in it
    float x = hx - 20 - fmodf(i * 37.0f + s_t * 9, 160), y = fmodf(i * 71.0f + s_t * 5, (float)H);
    if (x > -4) circle(x, y, 1.5f, rgb(0x6a5a8a), 160);
  }
}

static void drawHud() {
  bool warn = s_warnT > 0 && s_mode == Mode::Play && fmodf(s_t, 0.6f) < 0.4f;
  if (top() >= 142 && !warn) return;
  rect(0, 0, W, 22, rgb(0x000000), 120);
  char b[40];
  circle(14, 11, 7, rgb(0xffd060), 70);
  circle(14, 11, 3.5f, rgb(0xfff4c8));
  snprintf(b, sizeof(b), "%d", s_lamps);
  txt(26, 4, b, 1, rgb(0xfff0d0));
  snprintf(b, sizeof(b), "%d ly", (int)(s_maxX / 100));
  txt(W - 8 - textW(b, 1), 4, b, 1, rgb(0xfff0d0));
  const Region &R = REG[s_region < 0 ? 0 : s_region];
  if (s_bannerT <= 0) txtC(W / 2, 4, R.name, 1, rgb(R.accent));
  if (s_bannerT > 0 && s_mode == Mode::Play) {
    float a = clampf(s_bannerT, 0, 1);
    rect(0, 92, W, 50, rgb(0x000000), (uint8_t)(110 * a));
    char lap[24];
    if (s_lap) snprintf(lap, sizeof(lap), "Round %d", s_lap + 1);
    txtC(W / 2, 100, R.name, 2, rgb(R.accent), true);
    if (s_lap) txtC(W / 2, 126, lap, 1, rgb(0xfff0d0));
  }
  if (s_warnT > 0 && s_mode == Mode::Play && fmodf(s_t, 0.6f) < 0.4f) {
    for (int py = top() < 22 ? 22 : top(); py < top() + height(); py++) { // a violet glow down the left edge
      uint16_t *r = row(py), c = rgb(0x8a2aff);
      for (int x = 0; x < 36; x++) r[x] = blend(r[x], c, (uint8_t)(90 - x * 5 / 2));
    }
    txt(14, 30, "THE HUSH!", 1, rgb(0xd8b0ff), true);
  }
}

static void drawWorldScene() {
  drawSky();
  { SecTimer t(3); for (const Body &b : s_b) if (b.on) drawBody(b); }
  SecTimer t(4);
  for (const Mote &m : s_mote) {
    if (!m.on) continue;
    float sx = m.x - s_ox, sy = m.y - s_oy;
    if (sx < -10 || sx > W + 10 || sy < top() - 10 || sy > top() + height() + 10) continue;
    circle(sx, sy, 6 + fsin(m.t * 4) * 1.5f, rgb(0xffe8a0), 60);
    star(sx, sy, 4.5f, rgb(0xfff6d0));
  }
  for (const Rock &k : s_rock) if (k.on) drawRock(k);
  if (s_mode != Mode::Dying && s_mode != Mode::Over) {
    drawTrail();
    if (s_orbit >= 0) { // the tether of gravity
      const Body &b = s_b[s_orbit];
      line(s_shipX - s_ox, s_shipY - s_oy, b.x - s_ox, b.y - s_oy, 1.2f, rgb(0xfff0c0), 170);
      circle(b.x - s_ox, b.y - s_oy, 3, rgb(0xfff0c0));
    }
    drawShip(s_shipX - s_ox, s_shipY - s_oy, s_shipAng, s_orbit >= 0 ? 0.4f : 1);
  }
  for (const Spark &p : s_spark) if (p.life > 0) circle(p.x - s_ox, p.y - s_oy, 1.5f + p.life * 1.5f, rgb(p.c), (uint8_t)(255 * fminf(1, p.life * 1.5f)));
  drawHush();
}

static void card(float x, float y, float w, float h) {
  roundRect(x, y, w, h, 14, rgb(0x08060e), 205);
  roundRect(x, y, w, 3, 2, rgb(0xffd890), 160);
}

static void drawTitle() {
  // A lit ringed world, the Moth circling it, the Hush at the edge of sight.
  drawSky();
  drawBody(s_home);
  // Its trail: an arc behind.
  float a = s_t * 1.1f;
  for (int i = 1; i < 18; i++) {
    float a0 = a - i * 0.08f, a1 = a - (i + 1) * 0.08f;
    for (int pass = 0; pass < 2; pass++) {
      float rr = 62 + (pass ? 1.6f : -1.6f);
      line(s_home.x + fcos(a0) * rr, s_home.y + fsin(a0) * rr, s_home.x + fcos(a1) * rr, s_home.y + fsin(a1) * rr, 1.6f,
           rgb(pass ? 0x5af0ff : 0xff5ad8), (uint8_t)(220 - i * 12));
    }
  }
  line(s_shipX, s_shipY, s_home.x, s_home.y, 1.2f, rgb(0xfff0c0), 150);
  drawShip(s_shipX, s_shipY, s_shipAng, 0.4f);
  for (int py = top(); py < top() + height(); py++) // the Hush, waiting at the left
    hushRow(py, 28 + (vnoise(py * 0.025f, s_t * 0.7f, s_noiseSeed) - 0.5f) * 30, 14, 120);
  txtC(W / 2, 14, "LAMPLIGHTER", 3, rgb(0xffe4a0), true);
  txtC(W / 2, 70, "Light the stars before the Hush", 1, rgb(0xd8c8f0), true);
  char b[40];
  if (s_best) { snprintf(b, sizeof(b), "Best %lu   %lu lamps", (unsigned long)s_best, (unsigned long)s_bestLamps); txtC(124, 228, b, 1, rgb(0xfff0d0), true); }
  txtC(124, 250, "SELECT fly", 1, rgb(0xffd890), true);
}

static void drawStory() {
  drawSky();
  card(24, 16, W - 48, H - 32);
  txtC(W / 2, 28, "THE HUSH IS COMING", 1, rgb(0xffd890));
  int y = wrapC(W / 2, 52,
                "A tide of quiet dark, the Hush, is putting the stars to sleep. Wren keeps the lamps of the sky. "
                "Her ship, the Moth, has no fuel - only the pull of worlds.",
                34, rgb(0xf4ecdc));
  y = wrapC(W / 2, y + 8, "Hold any arrow to orbit a world. Let go to fly on. Each world you circle lights its lamp.", 34, rgb(0xc8d8f0));
  wrapC(W / 2, y + 8, "Wheel steers. Mind rocks, moons.", 34, rgb(0xc8d8f0));
  txtC(W / 2, H - 40, "SELECT go", 1, rgb(0xffd890));
}

// Everything the drawing reads that changes from frame to frame, fixed
// once before the first strip: the camera with its shake, and the title's
// orbiting world and Moth.
static void prepare() {
  switch (s_mode) {
    case Mode::Title: {
      if (!s_home.on) {
        memset(&s_home, 0, sizeof(s_home));
        s_home.on = true; s_home.kind = B_RING; s_home.lit = true;
        s_home.x = 322; s_home.y = 178; s_home.r = 30; s_home.field = 104;
        s_home.c0 = 0xa0dcc8; s_home.c1 = 0x2e8a76; s_home.atm = 0xa0dcc8;
        s_home.seed = 3; s_home.bands = 9; s_home.ringA = -0.3f; s_home.spinW = 0.1f;
      }
      s_home.spin = s_t * 0.1f;
      s_home.litT = s_t;
      s_ox = 0; s_oy = 0;
      s_skyX = s_t * 20; s_skyY = 0;
      float a = s_t * 1.1f;
      s_shipX = s_home.x + cosf(a) * 62; s_shipY = s_home.y + sinf(a) * 62; s_shipAng = a + PI_F / 2;
      break;
    }
    case Mode::Story:
      s_ox = 0; s_oy = 0;
      s_skyX = s_t * 20; s_skyY = 0;
      break;
    default:
      s_ox = s_camX + (frand() - 0.5f) * s_shake;
      s_oy = s_camY + (frand() - 0.5f) * s_shake;
      s_skyX = s_camX; s_skyY = s_camY;
      s_shipX = s_x; s_shipY = s_y; s_shipAng = s_ang;
      break;
  }
  prepareSky();
}

// The whole picture, clipped to the current strip.
static void drawPicture() {
  switch (s_mode) {
    case Mode::Title: drawTitle(); break;
    case Mode::Story: drawStory(); break;
    default:
      drawWorldScene();
      { SecTimer t(5); drawHud(); }
      break;
  }
  char b[48];
  if (s_mode == Mode::Paused) {
    card(128, 86, W - 256, 104);
    txtC(W / 2, 104, "PAUSED", 2, rgb(0xfff0d0));
    txtC(W / 2, 142, "SELECT go on", 1, rgb(0xd8c8f0));
    txtC(W / 2, 160, "UP menu", 1, rgb(0xd8c8f0));
  } else if (s_mode == Mode::Over && s_modeT > 0.3f) {
    card(78, 44, W - 156, 192);
    txtC(W / 2, 58, s_newBest ? "A NEW BEST!" : "THE LAMPS GO ON", 1, rgb(0xffd890));
    wrapC(W / 2, 80, s_why, 26, rgb(0xd8c8f0));
    snprintf(b, sizeof(b), "%lu", (unsigned long)score());
    txtC(W / 2, 100, b, 3, rgb(0xfff0d0), true);
    snprintf(b, sizeof(b), "%d lamps lit  %d stardust", s_lamps, s_motes);
    txtC(W / 2, 158, b, 1, rgb(0xf4ecdc));
    snprintf(b, sizeof(b), "%d light-years   best %lu", (int)(s_maxX / 100), (unsigned long)s_best);
    txtC(W / 2, 176, b, 1, rgb(0xc8d8f0));
    txtC(W / 2, 206, "SELECT again   UP menu", 1, rgb(0xffd890));
  }
}

// ---------------------------------------------------------------------------
// Drawing on two cores
//
// The panel is portrait, so a landscape strip of 20 rows is a 20-pixel-wide
// column of it, all 456 pixels tall. Each strip is drawn into its core's
// `land` buffer with smooth edges, turned into one of SLOTS `port` buffers
// and sent. Both cores take the next undrawn strip from a shared counter;
// this one also sends the finished strips in order between its own, so
// neither sits idle and sending overlaps drawing. A strip waits for its
// slot until the strip SLOTS before it has gone out.
// ---------------------------------------------------------------------------

// Bench timings (see benchStats), in microseconds, summed over a report.
static struct Stats {
  bool on;
  uint32_t since, frames, frameSum, frameMax, draw, send, wait, steps[4];
  volatile uint32_t wDraw;
  float maxY;
} s_st;

static void drawStrip(int strip, uint16_t *land, uint16_t *port) {
  target(land, W, STRIP_H, strip * STRIP_H);
  setScale(1);
  Raster::smooth(true);
  drawPicture();
  Raster::smooth(false);
  if (!port) return;
  SecTimer t(6);
  // Landscape (x, y) -> portrait row 455 - x, column y (upside down when
  // flipped: row x, column 279 - y).
  bool fl = s_gfx->flipped();
  for (int j = 0; j < STRIP_H; j++) {
    const uint16_t *src = land + j * W;
    uint16_t *d = port + (fl ? STRIP_H - 1 - j : j);
    if (fl) for (int py = 0; py < W; py++) d[py * STRIP_H] = src[py];
    else for (int py = 0; py < W; py++) d[py * STRIP_H] = src[W - 1 - py];
  }
}

static void sendStrip(int strip) {
  int px = s_gfx->flipped() ? H - (strip + 1) * STRIP_H : strip * STRIP_H;
  s_panel->draw16bitRGBBitmap(px, 0, s_port[strip % SLOTS], STRIP_H, W);
}

static int s_nextStrip = 0;          // the next strip to take (atomic)
static volatile int s_sent = 0;      // strips sent so far
static volatile uint8_t s_ready[STRIPS];

static inline int takeStrip() { return __atomic_fetch_add(&s_nextStrip, 1, __ATOMIC_ACQ_REL); }
static inline bool isReady(int k) { return __atomic_load_n(&s_ready[k], __ATOMIC_ACQUIRE); }

static void drawTaken(int k, uint16_t *land) {
  drawStrip(k, land, s_port[k % SLOTS]);
  __atomic_store_n(&s_ready[k], 1, __ATOMIC_RELEASE);
}

#ifndef PC_SIM
static TaskHandle_t s_worker = nullptr;
static SemaphoreHandle_t s_go = nullptr, s_done = nullptr;
static void workerTask(void *) {
  for (;;) {
    xSemaphoreTake(s_go, portMAX_DELAY);
    for (;;) {
      int k = takeStrip();
      if (k >= STRIPS) break;
      while (k - SLOTS >= s_sent) {} // its slot is still waiting to go out
      uint32_t t0 = micros();
      drawTaken(k, s_land[1]);
      s_st.wDraw += micros() - t0;
    }
    xSemaphoreGive(s_done);
  }
}
#endif

static void render() {
  prepare();
  memset((void *)s_ready, 0, sizeof(s_ready));
  s_sent = 0;
  __atomic_store_n(&s_nextStrip, 0, __ATOMIC_RELEASE);
#ifndef PC_SIM
  if (s_worker) xSemaphoreGive(s_go);
#endif
  int sent = 0;
  auto sendNext = [&]() {
    uint32_t t0 = micros();
    while (!isReady(sent)) {} // the other core is finishing it
    uint32_t t1 = micros();
    sendStrip(sent);
    s_sent = ++sent;
    s_st.wait += t1 - t0;
    s_st.send += micros() - t1;
  };
  while (sent < STRIPS) {
    if (isReady(sent)) { sendNext(); continue; }
    int k = __atomic_load_n(&s_nextStrip, __ATOMIC_ACQUIRE) < STRIPS ? takeStrip() : STRIPS;
    if (k >= STRIPS) { sendNext(); continue; }
    while (k - SLOTS >= sent) sendNext(); // free its slot first
    uint32_t t0 = micros();
    drawTaken(k, s_land[0]);
    s_st.draw += micros() - t0;
  }
#ifndef PC_SIM
  if (s_worker) xSemaphoreTake(s_done, portMAX_DELAY);
#endif
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

// Strip buffers in fast internal memory if there's room.
static uint16_t *stripAlloc() {
  size_t n = (size_t)W * STRIP_H * 2;
  uint16_t *p = (uint16_t *)heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) p = (uint16_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p;
}

static void freeBuffers() {
  for (uint16_t *&p : s_land) { heap_caps_free(p); p = nullptr; }
  for (uint16_t *&p : s_port) { heap_caps_free(p); p = nullptr; }
  heap_caps_free(s_shot); s_shot = nullptr;
  heap_caps_free(s_neb); s_neb = nullptr;
}

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel) {
  if (s_active) return true;
  s_gfx = gfx;
  s_panel = panel;
  bool ok = true;
  for (uint16_t *&p : s_land) ok = (p = stripAlloc()) && ok;
  for (uint16_t *&p : s_port) ok = (p = stripAlloc()) && ok;
  s_neb = (uint16_t *)heap_caps_malloc((size_t)NT_W * NT_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ok || !s_neb) {
    freeBuffers();
    Serial.println("Lamplighter: not enough memory");
    return false;
  }
#ifndef PC_SIM
  int inPsram = 0;
  for (uint16_t *q : s_land) inPsram += !esp_ptr_internal(q);
  for (uint16_t *q : s_port) inPsram += !esp_ptr_internal(q);
  Serial.printf("Lamplighter: %d of %d strip buffers in PSRAM, internal heap free %u\n", inPsram, 2 + SLOTS,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  if (!s_worker) {
    s_go = xSemaphoreCreateBinary();
    s_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(workerTask, "lamplight", 8192, nullptr, 2, &s_worker, 0);
  }
#endif
  makeSinTab();
  s_prefs.begin("lamplighter", false);
  s_best = s_prefs.getUInt("best", 0);
  s_bestLamps = s_prefs.getUInt("lamps", 0);
  s_rng = esp_random() | 1;
  s_region = -1;
  newRun();
  s_t = 0;
  setMode(Mode::Title);
  s_exit = false;
  s_auto = false;
  RunAudio::start();
  RunAudio::Params quiet;
  quiet.paused = true; // effects only
  RunAudio::set(quiet);
  s_active = true;
  return true;
}

void exit() {
  if (!s_active) return;
  RunAudio::stop();
  s_prefs.end();
  freeBuffers();
  s_active = false;
}

bool active() { return s_active; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }
// The whole current frame, drawn again strip by strip into PSRAM.
const uint16_t *frameBuffer() {
  if (!s_shot) s_shot = (uint16_t *)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_shot) return nullptr;
  for (int i = 0; i < STRIPS; i++) {
    drawStrip(i, s_land[0], nullptr);
    memcpy(s_shot + i * STRIP_H * W, s_land[0], (size_t)W * STRIP_H * 2);
  }
  return s_shot;
}

void benchEdge() {
  s_auto = false;
  newRun(10);
  setMode(Mode::Play);
  s_y = 120; s_ang = 1.15f; // heading steeply down at the lane's floor
  s_bannerT = 0;
}

void benchStats() {
  bool on = !s_st.on;
  memset((void *)&s_st, 0, sizeof(s_st));
  s_st.on = on;
  s_st.since = millis();
}

void benchAutopilot() {
  // First press: the pilot flies from the start. Each press after: a fresh
  // run from the start of the next region.
  static int reg = 0;
  reg = s_auto ? (reg + 1) % 5 : 0;
  s_auto = true;
  newRun(reg * REGION_LEN + 10);
  setMode(Mode::Play);
}

void pause() {
  if (s_mode == Mode::Play) setMode(Mode::Paused);
}

void key(const InputEvent &ev) {
  if (!ev.pressed || ev.repeat) return;
  switch (s_mode) {
    case Mode::Title:
      if (ev.key == Key::Select) {
        if (s_storySeen) { newRun(); setMode(Mode::Play); }
        else setMode(Mode::Story);
      } else if (ev.key == Key::Up) {
        s_exit = true;
      }
      break;
    case Mode::Story:
      if (ev.key == Key::Select) { s_storySeen = true; newRun(); setMode(Mode::Play); }
      else if (ev.key == Key::Up) setMode(Mode::Title);
      break;
    case Mode::Play:
      if (ev.key == Key::Select) pause();
      break;
    case Mode::Paused:
      if (ev.key == Key::Select) setMode(Mode::Play);
      else if (ev.key == Key::Up) setMode(Mode::Title);
      break;
    case Mode::Over:
      if (s_modeT < 0.6f) break;
      if (ev.key == Key::Select) { newRun(); setMode(Mode::Play); }
      else if (ev.key == Key::Up) setMode(Mode::Title);
      break;
    case Mode::Dying:
      break;
  }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  static uint32_t last = 0;
  uint32_t now = millis(), nowUs = micros();
  float dt = last ? (nowUs - last) * 1e-6f : 0.02f;
  last = nowUs;
  if (dt > 0.1f) dt = 0.1f;

  if (s_mode == Mode::Play) {
    s_hold = input.held(Key::Up) || input.held(Key::Down) || input.held(Key::Left) || input.held(Key::Right) || touch.pressed();
    s_steer += wheel;
  } else {
    s_hold = false;
    if (touch.justPressed()) {
      InputEvent tap = {Key::Select, true, false};
      key(tap);
    }
  }
  if (s_auto && s_mode == Mode::Over && s_modeT > 2) { newRun(); setMode(Mode::Play); }

  // The game moves by the real time since the last frame, in equal steps
  // of at most 1/60 s - so motion is even whatever the frame rate (fixed
  // 1/60 s steps would give some frames 2 and some 3, a visible judder).
  int steps = (int)ceilf(dt * 60 - 0.001f);
  if (steps < 1) steps = 1;
  for (int i = 0; i < steps; i++) update(dt / steps);
  render();

  if (s_st.on) {
    static uint32_t prev = 0;
    uint32_t us = micros();
    if (prev) {
      uint32_t f = us - prev;
      s_st.frames++; s_st.frameSum += f;
      if (f > s_st.frameMax) s_st.frameMax = f;
      s_st.steps[steps > 3 ? 3 : steps]++;
      if (fabsf(s_y) > s_st.maxY) s_st.maxY = fabsf(s_y);
    }
    prev = us;
    if (now - s_st.since >= 2000 && s_st.frames) {
      uint32_t n = s_st.frames;
      Serial.printf("LL fps %.1f  frame avg %lu max %lu ms  main draw %lu send %lu wait %lu  core0 draw %lu (ms/frame)  steps 0:%lu 1:%lu 2:%lu 3+:%lu  y %.0f max|y| %.0f\n",
                    n * 1e6f / s_st.frameSum, (unsigned long)(s_st.frameSum / n / 1000), (unsigned long)(s_st.frameMax / 1000),
                    (unsigned long)(s_st.draw / n / 1000), (unsigned long)(s_st.send / n / 1000), (unsigned long)(s_st.wait / n / 1000),
                    (unsigned long)(s_st.wDraw / n / 1000), (unsigned long)s_st.steps[0], (unsigned long)s_st.steps[1],
                    (unsigned long)s_st.steps[2], (unsigned long)s_st.steps[3], s_y, s_st.maxY);
      static const char *names[7] = {"nebula", "stars", "aurora", "worlds", "rest", "hud", "turn"};
      Serial.printf("   per frame (ms, core1/core0):");
      for (int i = 0; i < 7; i++) {
        Serial.printf(" %s %.1f/%.1f", names[i], s_sec[1][i] / 1000.0f / n, s_sec[0][i] / 1000.0f / n);
      }
      Serial.printf("\n");
      memset((void *)s_sec, 0, sizeof(s_sec));
      memset((void *)&s_st, 0, sizeof(s_st));
      s_st.on = true;
      s_st.since = now;
    }
  }
}

} // namespace Lamplighter
