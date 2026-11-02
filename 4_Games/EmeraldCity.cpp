#include "EmeraldCity.h"
#include "Raster.h"
#include "RunAudio.h"
#include "HalfRes.h"
#include "Denslow.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>
#include <font/glcdfont.h>

using namespace Raster;

namespace EmeraldCity {

// ---------------------------------------------------------------------------
// The view. Everything is laid out in screen units (456 x 280) and drawn at
// half scale. World y runs upward from the ground; the camera follows the
// top of the tower so the floor on the hook always drops the same distance.
// ---------------------------------------------------------------------------

static const int W = HalfRes::W, H = HalfRes::H, FW = HalfRes::FW, FH = HalfRes::FH;
static const float STEP = 1.0f / 60;
static const float K = 0.5f;            // screen units -> frame pixels

static const float FLOOR_W = 108, FLOOR_H = 32;
static const float TOWER_X = 252;       // the base's centre
static const float TOP_SY = 150;        // screen y of the tower's top surface
static const float HANG_Y = 72;         // screen y of a hanging floor's top
static const float MAST_X = 34;         // the crane's mast
static const float JIB_TOP = 8, JIB_Y = 22;
static const float PERFECT = 4;         // a drop this close lines up exactly
static const float MISS = FLOOR_W * 0.6f;
static const float TIP = FLOOR_W * 0.42f; // the tower's balance limit
static const int MAX_FLOORS = 16;
static const int LANDS = 6;
static const int HEARTS = 3;
static const int CLOUD_H = 180;         // cloud texture rows (frame px), wraps

enum Style : uint8_t { ST_MUNCHKIN, ST_CORN, ST_TIN, ST_POPPY, ST_WINKIE, ST_EMERALD };
enum Twist : uint8_t { TW_NONE, TW_CROWS, TW_HEAVY, TW_DROWSY, TW_WIND, TW_FINALE };

struct Land {
  const char *name, *line1, *line2, *folk;
  uint32_t skyTop, skyLow, cloud, hills, ground1, ground2;
  uint32_t wall1, wall2, trim, trimDark, glass, lit;
  uint8_t floors;
  float swing, speed, swayGain;
  Twist twist;
  float night; // 0 day .. 1 night: windows glow, stars
};

static const Land LANDS_[LANDS] = {
    {"Munchkin Country", "The Munchkins' little blue", "houses all have round domes.", "Munchkins",
     0x6fc0ff, 0xdff4ff, 0xffffff, 0x9cc9f0, 0x86d070, 0x4f9a48,
     0x78b0ff, 0x3f6ad0, 0xffffff, 0x2a4a9a, 0x24407e, 0xffe9a0,
     7, 80, 2.1f, 1.0f, TW_NONE, 0},
    {"The Scarecrow's Field", "The Witch sends her crows -", "they love a swinging floor.", "farm folk",
     0x86ccff, 0xfff0c0, 0xffffff, 0xc9d98a, 0xd9c25a, 0xa88a2a,
     0xf0be62, 0xb07a2e, 0x7a4a1e, 0x4a2a10, 0x3a2a18, 0xffd890,
     8, 84, 2.25f, 1.1f, TW_CROWS, 0},
    {"The Great Forest", "Tin floors are heavy: slow", "to swing, but quick to sway.", "woodmen",
     0x96d6c4, 0xeaf6e0, 0xf4fff4, 0x5f9a6a, 0x4f8a48, 0x2f5a30,
     0xeef2f8, 0x8e99ab, 0x5a6578, 0x343c4c, 0x26344e, 0xfff0b0,
     9, 96, 1.75f, 1.6f, TW_HEAVY, 0},
    {"The Poppy Field", "The poppies make the crane", "drowsy - and then it wakes.", "field mice",
     0xffb49a, 0xfff0d0, 0xfff4ec, 0xd9a08a, 0x7aa848, 0x4f7a30,
     0xfff6ea, 0xf0d0b8, 0xd8283c, 0x8a1424, 0x3a2430, 0xffe0a0,
     9, 88, 2.2f, 1.1f, TW_DROWSY, 0.15f},
    {"The Winkie Country", "Out west the wind blows", "the crane about. Hold on!", "Winkies",
     0x5a4aa0, 0xffbe70, 0xffd8b8, 0x9a6a78, 0xe8c45a, 0xb08a2a,
     0xffd84a, 0xd9a21a, 0x6a3a8a, 0x3e1f56, 0x40244e, 0xffe890,
     10, 90, 2.35f, 1.2f, TW_WIND, 0.45f},
    {"The Emerald City", "Raise the great green tower", "and light up the whole city.", "citizens",
     0x060c26, 0x1f3a6a, 0x5a6a9a, 0x1a3448, 0x1f5a3a, 0x0f3a24,
     0x34d890, 0x0c7a4c, 0xf6c94a, 0xa8761a, 0x0d4a30, 0xc8ffe0,
     11, 104, 2.7f, 1.3f, TW_FINALE, 1.0f},
};

enum class Mode : uint8_t { Title, Ready, Play, Paused, Built, Cleared, Fail, Finale };

struct Floor { float x; uint8_t seed; };
struct Tumble { bool on, roof; float x, y, vx, vy, a, va; };
struct Popup { float x, y, t; char text[18]; uint32_t col; };
struct Spark { float x, y, vx, vy, life; uint32_t col; uint8_t kind; };
struct Flyer { bool on; float x, y, t; };

static bool s_active = false, s_exit = false, s_auto = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static uint16_t *s_frame = nullptr;
static uint8_t *s_clouds = nullptr;
static Preferences s_prefs;

static Mode s_mode = Mode::Title;
static int s_ch = 0, s_unlocked = 0, s_pick = 0;
static const Land *E = &LANDS_[0];
static float s_t = 0, s_modeT = 0;
static uint32_t s_rng = 1;
static uint32_t s_best[LANDS];

static Floor s_floors[MAX_FLOORS];
static int s_n = 0;                 // floors standing, the base included
static bool s_roofOn = false;
static float s_cam = FLOOR_H, s_camTarget = FLOOR_H;
static float s_phase = 0, s_trim = 0, s_wind = 0;
static float s_swayAmp = 0, s_swayPh = 0;
static float s_lower = 1;           // a new floor coming down on the hook, 0..1
static float s_spawnT = -1;
static bool s_hanging = false, s_falling = false;
static float s_fx = 0, s_fy = 0, s_fvx = 0, s_fvy = 0;
static float s_land = 0;            // squash after landing, 1 -> 0
static int s_hearts = HEARTS, s_combo = 0;
static uint32_t s_pop = 0;
static float s_litTo = 0;           // Built: windows light up floor by floor

static Tumble s_tumble[2];
static Popup s_pops[5];
static const int SPARKS = 90;
static Spark s_sparks[SPARKS];
static Flyer s_crow, s_monkey;
static float s_crowNext = 3, s_balloonX = 380;
static float s_hills[60];

static float frand() { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) / 16777216.0f; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static uint32_t shade(uint32_t c, float f) { return mixHex(c, 0x000000, f); }
static uint32_t tint(uint32_t c, float f) { return mixHex(c, 0xffffff, f); }

// --- Perlin noise, for the clouds and far hills ----------------------------

static uint8_t PERM[512];
static void seedNoise(uint32_t seed) {
  uint32_t r = seed * 2654435761u + 1;
  for (int i = 0; i < 256; i++) PERM[i] = i;
  for (int i = 255; i > 0; i--) {
    r = r * 1664525u + 1013904223u;
    int j = (r >> 8) % (i + 1);
    uint8_t t = PERM[i]; PERM[i] = PERM[j]; PERM[j] = t;
  }
  for (int i = 0; i < 256; i++) PERM[256 + i] = PERM[i];
}
static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static float grad2(int h, float x, float y) {
  switch (h & 7) {
    case 0: return x + y; case 1: return x - y; case 2: return -x + y; case 3: return -x - y;
    case 4: return x; case 5: return -x; case 6: return y; default: return -y;
  }
}
static float noise2(float x, float y) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float xf = x - xi, yf = y - yi;
  xi &= 255; yi &= 255;
  float u = fade(xf), v = fade(yf);
  int aa = PERM[PERM[xi] + yi], ab = PERM[PERM[xi] + yi + 1], ba = PERM[PERM[xi + 1] + yi], bb = PERM[PERM[xi + 1] + yi + 1];
  return lerpf(lerpf(grad2(aa, xf, yf), grad2(ba, xf - 1, yf), u), lerpf(grad2(ab, xf, yf - 1), grad2(bb, xf - 1, yf - 1), u), v);
}
static float fbm(float x, float y, int oct) {
  float s = 0, a = 0.5f, f = 1;
  for (int i = 0; i < oct; i++, a *= 0.5f, f *= 2) s += a * noise2(x * f, y * f);
  return s;
}

// --- text ---------------------------------------------------------------------

static const uint16_t SHADOW = 0x10a4;
static void glyph(int lx, int ly, unsigned char ch, int size, uint16_t col) {
  for (int cx = 0; cx < 5; cx++) {
    uint8_t bits = font[ch * 5 + cx];
    for (int cy = 0; cy < 8; cy++, bits >>= 1) {
      if (!(bits & 1)) continue;
      int x0 = lx + cx * size, y0 = ly + cy * size;
      for (int yy = y0; yy < y0 + size; yy++) {
        if (yy < 0 || yy >= FH) continue;
        for (int xx = x0; xx < x0 + size; xx++) if (xx >= 0 && xx < FW) s_frame[yy * FW + xx] = col;
      }
    }
  }
}
static int textW(const char *s, int size) { return (int)strlen(s) * 12 * size; }
static void txt(int x, int y, const char *s, int size, uint16_t col, bool shadow = true) {
  int lx = x / 2, ly = y / 2;
  for (const char *p = s; *p; p++, lx += 6 * size) {
    if (shadow) glyph(lx + 1, ly + 1, (unsigned char)*p, size, SHADOW);
    glyph(lx, ly, (unsigned char)*p, size, col);
  }
}
static void txtC(int cx, int y, const char *s, int size, uint16_t col, bool shadow = true) { txt(cx - textW(s, size) / 2, y, s, size, col, shadow); }

// Centred size-1 text, word-wrapped to maxChars a line. Returns the next line's y.
static int wrapC(int cx, int y, const char *s, int maxChars, uint16_t col) {
  char line[48];
  while (*s) {
    while (*s == ' ') s++;
    int len = strlen(s), take = len;
    if (len > maxChars) {
      take = maxChars;
      while (take > 0 && s[take] != ' ') take--;
      if (take == 0) take = maxChars;
    }
    if (take >= (int)sizeof(line)) take = sizeof(line) - 1;
    memcpy(line, s, take);
    line[take] = '\0';
    txtC(cx, y, line, 1, col, false);
    s += take;
    y += 17;
  }
  return y;
}

// A Denslow plate, tinted, at frame position (fx, fy) - the storybook cards.
static void plate(const uint8_t *data, int w, int h, int fx, int fy, uint32_t ink, uint32_t paper) {
  uint16_t lut[16];
  for (int i = 0; i < 16; i++) lut[i] = mix(ink, paper, i / 15.0f);
  for (int y = 0; y < h; y++) {
    int py = fy + y;
    if (py < 0 || py >= FH) continue;
    for (int x = 0; x < w; x++) {
      int px = fx + x;
      if (px < 0 || px >= FW) continue;
      int i = y * w + x;
      uint8_t b = data[i >> 1];
      s_frame[py * FW + px] = lut[(i & 1) ? (b & 15) : (b >> 4)];
    }
  }
}

static void pop(float x, float y, const char *t, uint32_t col) {
  for (Popup &p : s_pops) {
    if (p.t > 0) continue;
    p.x = x; p.y = y; p.t = 1.3f; p.col = col;
    strlcpy(p.text, t, sizeof(p.text));
    return;
  }
}

static void spark(float x, float y, float vx, float vy, float life, uint32_t col, uint8_t kind = 0) {
  for (Spark &s : s_sparks) {
    if (s.life > 0) continue;
    s = {x, y, vx, vy, life, col, kind};
    return;
  }
}

// A vertical gradient, in screen units.
static void vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bot) {
  int y0 = (int)(y * K), y1 = (int)((y + h) * K), x0 = (int)(x * K + 0.5f), x1 = (int)((x + w) * K + 0.5f);
  for (int yy = y0; yy < y1; yy++) {
    if (yy < 0 || yy >= FH) continue;
    span(yy, x0, x1, mix(top, bot, (yy - y0) / (float)(y1 - y0 > 1 ? y1 - y0 - 1 : 1)));
  }
}

// ---------------------------------------------------------------------------
// The tower: where things are
// ---------------------------------------------------------------------------

static float topWorld() { return s_n * FLOOR_H; }
static float worldToScreen(float wy) { return TOP_SY - (wy - s_cam); }
static float groundY() { return worldToScreen(0); }

// The sway: the top moves most, the base not at all.
static float swayAt(int i) {
  if (i <= 0 || s_n <= 1) return 0;
  float h = i / (float)(s_n > 3 ? s_n - 1 : 3);
  return s_swayAmp * sinf(s_swayPh) * powf(h, 1.3f);
}
static float floorX(int i) { return TOWER_X + s_floors[i].x + swayAt(i); }

static bool roofNext() { return s_n - 1 >= E->floors; }

// Where the crane's hook is: the floor sweeps across, the trolley leads it
// a little so the ropes lean like a pendulum.
static float hookX() { return TOWER_X + s_trim + s_wind + E->swing * sinf(s_phase); }
static float trolleyX() { return TOWER_X + s_trim + s_wind + E->swing * 0.82f * sinf(s_phase + 0.45f); }
static float hangY() { return HANG_Y - (1 - s_lower) * (1 - s_lower) * 90; }

// ---------------------------------------------------------------------------
// Drawing: the floors of each land
// ---------------------------------------------------------------------------

static void heart(float cx, float cy, float r, uint16_t c) {
  circle(cx - r * 0.5f, cy - r * 0.2f, r * 0.55f, c);
  circle(cx + r * 0.5f, cy - r * 0.2f, r * 0.55f, c);
  const float t[6] = {cx - r * 1.02f, cy, cx + r * 1.02f, cy, cx, cy + r * 1.05f};
  poly(t, 3, c);
}

static void gem(float cx, float cy, float r, uint32_t c) {
  const float d[8] = {cx, cy - r, cx + r * 0.8f, cy, cx, cy + r, cx - r * 0.8f, cy};
  poly(d, 4, rgb(shade(c, 0.35f)));
  const float d2[8] = {cx, cy - r * 0.7f, cx + r * 0.5f, cy, cx, cy + r * 0.55f, cx - r * 0.5f, cy};
  poly(d2, 4, rgb(c));
  circle(cx - r * 0.18f, cy - r * 0.3f, r * 0.2f, rgb(0xffffff), 200);
}

// A window: frame, glass (glowing when lit), optional arch and mullions.
static void window(float x, float y, float w, float h, uint32_t frame, uint32_t glass, float lit, bool arch, bool cross) {
  uint32_t g = mixHex(glass, E->lit, lit);
  if (lit > 0.05f) roundRect(x - 3, y - 3, w + 6, h + 6, 4, rgb(E->lit), (uint8_t)(70 * lit));
  if (arch) circle(x + w / 2, y + 1, w / 2 + 1.5f, rgb(frame));
  rect(x - 1.5f, y, w + 3, h + 1.5f, rgb(frame));
  if (arch) circle(x + w / 2, y + 1, w / 2 - 0.5f, rgb(g));
  rect(x + 0.5f, y + 1, w - 1, h - 1, rgb(g));
  rect(x + 1, y + 1.5f, w * 0.3f, h * 0.45f, rgb(0xffffff), (uint8_t)(70 - 50 * lit)); // a glint
  if (cross) {
    rect(x + w / 2 - 0.6f, y, 1.2f, h, rgb(frame));
    rect(x, y + h * 0.48f, w, 1.2f, rgb(frame));
  }
}

static void cornice(float x, float y, float w) {
  rect(x - 3, y, w + 6, 4, rgb(E->trim));
  rect(x - 3, y + 4, w + 6, 1.5f, rgb(E->trimDark));
  for (float dx = x + 2; dx < x + w - 2; dx += 7) rect(dx, y + 5.5f, 3, 2, rgb(E->trimDark), 180);
}

// One floor of the land's style, top-left at (x, y). `door` makes it the
// ground floor; `lit` is how much the windows glow.
static void drawFloor(float cx, float y, int seed, bool door, float lit) {
  const float w = FLOOR_W, h = FLOOR_H, x = cx - w / 2;
  rect(x + w, y + 4, 4, h - 4, rgb(0x000000), 45); // shadow on the floor below
  vgrad(x, y, w, h, E->wall1, E->wall2);
  rect(x + w - 8, y, 8, h, rgb(0x000000), 34);    // the turned side, for depth
  rect(x, y, 2, h, rgb(0xffffff), 60);
  rect(x - 1, y + h - 3, w + 2, 3, rgb(E->trimDark));

  float wy = y + 11, ww = 14, wh = 13;
  const float WX[3] = {cx - 38, cx - 7, cx + 24};
  switch ((Style)s_ch) {
    case ST_MUNCHKIN:
      for (int i = 0; i < 13; i++) circle(x + 4 + i * 8.4f, y + 7, 4.2f, rgb(i & 1 ? 0xffffff : 0xa8cbff)); // scalloped awning
      for (int i = 0; i < 3; i++) {
        if (door && i == 1) continue;
        float c = WX[i] + ww / 2;
        circle(c, wy + 7, 8, rgb(E->trim));
        circle(c, wy + 7, 6.2f, rgb(mixHex(E->glass, E->lit, lit)));
        rect(c - 0.6f, wy + 1, 1.2f, 12, rgb(E->trim));
        rect(c - 6, wy + 6.4f, 12, 1.2f, rgb(E->trim));
        rect(c - 8, wy + 15, 16, 3, rgb(0x6a4a2a));             // flower box
        for (int f = 0; f < 4; f++) circle(c - 6 + f * 4, wy + 14.5f, 1.8f, rgb((f + seed) & 1 ? 0xff5a7a : 0xffd84a));
      }
      break;
    case ST_CORN:
      for (float py = y + 7; py < y + h - 3; py += 5) rect(x + 1, py, w - 2, 1, rgb(0x7a4a1e), 70); // planks
      for (int i = 0; i < 3; i++) {
        if (door && i == 1) continue;
        rect(WX[i] - 5, wy - 1, 4, wh + 2, rgb(0x4f8a3a));      // shutters
        rect(WX[i] + ww + 1, wy - 1, 4, wh + 2, rgb(0x4f8a3a));
        window(WX[i], wy, ww, wh, E->trim, E->glass, lit, false, true);
      }
      line(cx - 27, y + 8, cx - 15, y + h - 4, 1.4f, rgb(E->trimDark), 160); // X braces
      line(cx - 15, y + 8, cx - 27, y + h - 4, 1.4f, rgb(E->trimDark), 160);
      line(cx + 15, y + 8, cx + 27, y + h - 4, 1.4f, rgb(E->trimDark), 160);
      line(cx + 27, y + 8, cx + 15, y + h - 4, 1.4f, rgb(E->trimDark), 160);
      for (int s = -1; s <= 1; s += 2) {                        // corn cobs at the corners
        float ox = cx + s * (w / 2 - 4);
        ellipse(ox, y + 18, 3, 7, rgb(0xffd84a));
        const float husk[6] = {ox - 4, y + 26, ox + 4, y + 26, ox, y + 15};
        poly(husk, 3, rgb(0x6aa83a));
      }
      break;
    case ST_TIN:
      for (float rx = x + 4; rx < x + w - 2; rx += 7) {         // rivets
        circle(rx, y + 8, 1.2f, rgb(0x5a6578));
        circle(rx, y + h - 6, 1.2f, rgb(0x5a6578));
      }
      for (int i = 0; i < 3; i++) {
        if (door && i == 1) continue;
        if (i == 1 && (seed & 1)) {                             // the Tin Woodman's heart
          heart(cx, wy + 6, 7, rgb(0xe0354a));
          circle(cx - 3, wy + 3, 1.5f, rgb(0xffffff), 160);
          continue;
        }
        float c = WX[i] + ww / 2;
        circle(c, wy + 7, 8.5f, rgb(E->trimDark));
        circle(c, wy + 7, 7, rgb(0xb8c2d2));
        circle(c, wy + 7, 5.4f, rgb(mixHex(E->glass, E->lit, lit)));
        circle(c - 2, wy + 5, 1.6f, rgb(0xffffff), 150);
      }
      rect(x + w - 6, y, 3, h, rgb(0xc0c8d6));                  // a pipe
      rect(x + w - 6.5f, y + 12, 4, 2, rgb(E->trimDark));
      break;
    case ST_POPPY:
      for (int i = 0; i < 9; i++) ellipse(x + 8 + i * 11.5f, y + 8, 3.4f, 1.6f, rgb(0x5f9a4a)); // leaf garland
      for (int i = 0; i < 3; i++) {
        if (door && i == 1) continue;
        window(WX[i], wy + 1, ww, wh - 1, E->trim, E->glass, lit, true, true);
        rect(WX[i] - 3, wy + wh + 0.5f, ww + 6, 3.5f, rgb(0x3f7a3a));  // window box
        for (int f = 0; f < 4; f++) {
          float fx = WX[i] - 1 + f * 5.3f;
          circle(fx, wy + wh, 2.6f, rgb(0xe8283c));
          circle(fx, wy + wh, 0.9f, rgb(0x2a1018));
        }
      }
      break;
    case ST_WINKIE:
      for (float py = y + 8; py < y + h - 3; py += 7) {         // stone courses
        rect(x + 1, py, w - 2, 1, rgb(0xa8780a), 90);
        int row = (int)((py - y) / 7);
        for (float px = x + (row & 1 ? 6 : 12); px < x + w - 2; px += 13) rect(px, py, 1, 7, rgb(0xa8780a), 80);
      }
      for (int i = 0; i < 3; i++) {
        if (door && i == 1) continue;
        window(WX[i] + 2, wy - 1, ww - 4, wh + 2, E->trim, E->glass, lit, true, false);
      }
      for (int s = -1; s <= 1; s += 2) {                        // purple banners
        float bx = cx + s * 23 - 4;
        rect(bx, y + 6, 8, 16, rgb(0x7a3aa0));
        rect(bx, y + 6, 8, 2, rgb(0xffd84a));
        const float notch[6] = {bx, y + 22, bx + 8, y + 22, bx + 4, y + 18};
        poly(notch, 3, rgb(shade(E->wall2, 0.1f)));
      }
      break;
    case ST_EMERALD:
      for (int i = 0; i < 4; i++) rect(cx - 47 + i * 31, y + 6, 3, h - 9, rgb(E->trim)); // gold pilasters
      for (int i = 0; i < 3; i++) {
        if (door && i == 1) continue;
        window(WX[i], wy, ww, wh, E->trim, E->glass, lit, true, false);
        uint16_t lat = rgb(shade(E->trim, 0.2f));
        line(WX[i], wy + 2, WX[i] + ww, wy + wh, 0.8f, lat, 150); // diamond lattice
        line(WX[i] + ww, wy + 2, WX[i], wy + wh, 0.8f, lat, 150);
      }
      break;
  }
  if (door) {
    float dw = 20, dh = 22, dx = cx - dw / 2, dy = y + h - 3 - dh;
    circle(cx, dy + 2, dw / 2 + 3, rgb(E->trim));
    rect(dx - 3, dy + 2, dw + 6, dh - 2, rgb(E->trim));
    circle(cx, dy + 2, dw / 2, rgb(shade(E->wall2, 0.55f)));
    rect(dx, dy + 2, dw, dh - 2, rgb(shade(E->wall2, 0.55f)));
    rect(cx - 0.5f, dy, 1, dh, rgb(shade(E->wall2, 0.75f)));
    circle(cx + 4, dy + dh * 0.55f, 1.4f, rgb(0xf6c94a));
    if (lit > 0.05f) rect(dx, dy + 6, dw, dh - 6, rgb(E->lit), (uint8_t)(90 * lit));
  }
  cornice(x, y, w);
  if (s_ch == ST_EMERALD)
    for (int i = 0; i < 6; i++) gem(x + 9 + i * 18, y + 2, 3.4f, 0x5dffb0); // gems along the cornice
}

// The land's roof, sitting on a floor top at y.
static void drawRoof(float cx, float y, float lit) {
  const float w = FLOOR_W;
  switch ((Style)s_ch) {
    case ST_MUNCHKIN: {
      float pts[32];
      for (int i = 0; i <= 14; i++) {
        float a = 3.14159f + i * 3.14159f / 14;
        pts[i * 2] = cx + cosf(a) * (w / 2 + 2);
        pts[i * 2 + 1] = y + sinf(a) * 46;
      }
      poly(pts, 15, rgb(0x4f86e8));
      ellipse(cx - 18, y - 28, 12, 9, rgb(0xa8cbff), 120);
      for (int i = -2; i <= 2; i++) line(cx + i * 20, y, cx + i * 7, y - 44, 1.2f, rgb(0x2a4a9a), 110);
      circle(cx, y - 18, 7, rgb(0xffffff));
      circle(cx, y - 18, 5.4f, rgb(mixHex(E->glass, E->lit, lit)));
      rect(cx - 1, y - 60, 2, 16, rgb(0xd9a441));
      circle(cx, y - 60, 4, rgb(0xf6c94a));
      rect(cx - w / 2 - 2, y - 3, w + 4, 4, rgb(0xffffff));
      break;
    }
    case ST_CORN: {
      const float tri[6] = {cx - w / 2 - 8, y + 2, cx + w / 2 + 8, y + 2, cx, y - 54};
      poly(tri, 3, rgb(0xe8c46a));
      for (int i = 0; i < 9; i++) {
        float t = i / 8.0f;
        line(cx, y - 52, cx - w / 2 - 6 + t * (w + 12), y, 1, rgb(0xb08a3a), 140);
      }
      for (float yy = y - 40; yy < y; yy += 9) {
        float half = (yy - (y - 54)) / 56 * (w / 2 + 8);
        rect(cx - half, yy, half * 2, 1.5f, rgb(0xa07a2a), 150);
      }
      line(cx, y - 52, cx, y - 72, 1.4f, rgb(0x3a2a18));
      line(cx - 8, y - 66, cx + 8, y - 66, 1.2f, rgb(0x3a2a18));
      ellipse(cx + 1, y - 76, 6, 3.5f, rgb(0x1a1a22));           // crow weathervane
      circle(cx + 6, y - 78, 2.6f, rgb(0x1a1a22));
      const float bk[6] = {cx + 8, y - 79, cx + 12, y - 78, cx + 8, y - 77};
      poly(bk, 3, rgb(0xf0c040));
      break;
    }
    case ST_TIN: {
      const float cone[6] = {cx - w / 2 - 4, y + 2, cx + w / 2 + 4, y + 2, cx, y - 58};
      poly(cone, 3, rgb(0xb8c2d2));
      const float lite[6] = {cx - w / 2 + 6, y, cx - 4, y, cx - 2, y - 52};
      poly(lite, 3, rgb(0xeef2f8), 160);
      for (int i = 1; i < 5; i++) {
        float yy = y - i * 11, half = (1 - i * 11 / 60.0f) * (w / 2 + 4);
        rect(cx - half, yy, half * 2, 1.4f, rgb(0x5a6578), 170);
        for (float rx = cx - half + 3; rx < cx + half - 2; rx += 8) circle(rx, yy + 3, 0.9f, rgb(0x5a6578));
      }
      line(cx, y - 56, cx, y - 74, 1.6f, rgb(0x5a6578));
      heart(cx, y - 80, 7, rgb(0xe0354a));                       // the heart weathervane
      circle(cx - 3, y - 83, 1.6f, rgb(0xffffff), 170);
      break;
    }
    case ST_POPPY: {
      rect(cx - 30, y - 16, 60, 17, rgb(0xfff6ea));
      for (int i = 0; i < 5; i++) window(cx - 26 + i * 11, y - 13, 6, 9, 0xd8283c, E->glass, lit, true, false);
      rect(cx - 33, y - 19, 66, 4, rgb(0xd8283c));
      circle(cx, y - 44, 26, rgb(0xd8283c));                    // the onion dome
      const float tip[6] = {cx - 20, y - 58, cx + 20, y - 58, cx, y - 86};
      poly(tip, 3, rgb(0xd8283c));
      for (int i = -1; i <= 1; i++) line(cx + i * 16, y - 22, cx + i * 4, y - 80, 2, rgb(0xfff4e8), 180);
      ellipse(cx - 10, y - 52, 6, 10, rgb(0xffffff), 70);
      rect(cx - 1, y - 104, 2, 20, rgb(0xd9a441));
      circle(cx, y - 104, 3, rgb(0xf6c94a));
      break;
    }
    case ST_WINKIE: {
      for (int i = 0; i < 7; i++) rect(cx - w / 2 - 2 + i * 17, y - 12, 10, 13, rgb(0xffd84a)); // battlements
      rect(cx - w / 2 - 2, y - 2, w + 4, 4, rgb(E->trim));
      vgrad(cx - 20, y - 46, 40, 46, 0xffd84a, 0xd9a21a);         // the turret
      window(cx - 5, y - 36, 10, 14, E->trim, E->glass, lit, true, false);
      const float cap[6] = {cx - 26, y - 44, cx + 26, y - 44, cx, y - 90};
      poly(cap, 3, rgb(0x6a3a8a));
      const float capl[6] = {cx - 22, y - 45, cx - 4, y - 45, cx - 1, y - 86};
      poly(capl, 3, rgb(0x9a6ac0), 140);
      line(cx, y - 88, cx, y - 108, 1.4f, rgb(0x3e1f56));
      float flap = sinf(s_t * 6) * 3 + s_wind * 0.15f;
      const float flag[8] = {cx, y - 108, cx + 18, y - 104 + flap * 0.3f, cx + 18, y - 98 + flap * 0.3f, cx, y - 99};
      poly(flag, 4, rgb(0xffd84a));
      break;
    }
    case ST_EMERALD: {
      vgrad(cx - 40, y - 22, 80, 22, 0x34d890, 0x0c7a4c);
      rect(cx - 44, y - 24, 88, 4, rgb(0xf6c94a));
      for (int i = 0; i < 4; i++) window(cx - 34 + i * 19, y - 18, 10, 13, 0xf6c94a, E->glass, lit, true, false);
      const float spire[6] = {cx - 36, y - 24, cx + 36, y - 24, cx, y - 118};
      poly(spire, 3, rgb(0x1fb070));
      const float lite[6] = {cx - 30, y - 25, cx - 6, y - 25, cx - 3, y - 110};
      poly(lite, 3, rgb(0x7dffc0), 120);
      for (int i = 1; i < 5; i++) {
        float yy = y - 24 - i * 18, half = (1 - i * 18 / 94.0f) * 36;
        rect(cx - half, yy, half * 2, 2.5f, rgb(0xf6c94a));
      }
      float glow = 0.6f + 0.4f * sinf(s_t * 3);
      circle(cx, y - 126, 16, rgb(0x5dffb0), (uint8_t)(70 * glow));
      circle(cx, y - 126, 10, rgb(0x9dffd0), (uint8_t)(120 * glow));
      gem(cx, y - 126, 8, 0x5dffb0);
      for (int i = 0; i < 6; i++) {
        float a = s_t * 0.8f + i * 1.047f;
        line(cx + cosf(a) * 12, y - 126 + sinf(a) * 12, cx + cosf(a) * 24, y - 126 + sinf(a) * 24, 1, rgb(0xc8ffe0), (uint8_t)(110 * glow));
      }
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// The crane, the Wizard, and passers-by
// ---------------------------------------------------------------------------

static const uint32_t BRASS = 0xd9a441, BRASS_D = 0x7a4f16, BRASS_L = 0xffe08a;

static void girder(float x0, float y0, float x1, float y1) {
  line(x0, y0, x1, y1, 3.2f, rgb(BRASS_D));
  line(x0, y0, x1, y1, 1.6f, rgb(BRASS));
}

static void drawWizard(float cx, float cy) {
  circle(cx, cy, 6, rgb(0xf2c9a0));                 // bald head
  ellipse(cx - 5, cy + 1, 2, 3, rgb(0xeeeeee));      // white fringe
  ellipse(cx + 5, cy + 1, 2, 3, rgb(0xeeeeee));
  circle(cx - 2, cy, 1.8f, rgb(0x2a2a2a));           // spectacles
  circle(cx + 2, cy, 1.8f, rgb(0x2a2a2a));
  circle(cx - 2, cy, 1, rgb(0xf2c9a0));
  circle(cx + 2, cy, 1, rgb(0xf2c9a0));
  rect(cx - 6, cy + 6, 12, 6, rgb(0x2a7a4a));        // green coat
  rect(cx - 1, cy + 6, 2, 6, rgb(0xffffff));
}

static void drawCrane() {
  // The mast: a lattice that slides past as the tower (and camera) rises.
  float gy = groundY();
  float bottom = gy < H ? gy : H;
  girder(MAST_X - 9, JIB_Y, MAST_X - 9, bottom);
  girder(MAST_X + 9, JIB_Y, MAST_X + 9, bottom);
  float off = fmodf(s_cam, 20);
  for (float yy = JIB_Y + 2 + off - 20; yy < bottom; yy += 20) {
    float a = yy < JIB_Y ? JIB_Y : yy, b = yy + 10 > bottom ? bottom : yy + 10;
    if (b > a) line(MAST_X - 9, a, MAST_X + 9, b, 1.2f, rgb(BRASS));
    if (yy + 20 < bottom && yy + 10 > JIB_Y) line(MAST_X + 9, yy + 10, MAST_X - 9, yy + 20 > bottom ? bottom : yy + 20, 1.2f, rgb(BRASS));
  }
  if (gy < H) rect(MAST_X - 18, gy - 3, 36, 8, rgb(0x8a8a96)); // the footing

  // The jib across the top, and the counter-jib's weight on the left.
  girder(0, JIB_TOP, W, JIB_TOP);
  girder(0, JIB_Y, W, JIB_Y);
  for (float xx = 0; xx < W; xx += 16) line(xx, JIB_Y, xx + 8, JIB_TOP, 1.1f, rgb(BRASS));
  for (float xx = 8; xx < W; xx += 16) line(xx, JIB_TOP, xx + 8, JIB_Y, 1.1f, rgb(BRASS));
  const float peak[6] = {MAST_X - 10, JIB_TOP, MAST_X + 10, JIB_TOP, MAST_X, -6};
  poly(peak, 3, rgb(BRASS_D));
  roundRect(0, JIB_Y + 1, 26, 22, 3, rgb(0x6a6a78));
  roundRect(2, JIB_Y + 3, 22, 18, 2, rgb(0x8a8a9a));
  gem(13, JIB_Y + 12, 5, 0x3ddc84);

  // The cab, with the Wizard at the levers.
  roundRect(MAST_X + 8, JIB_Y + 1, 40, 28, 5, rgb(0x1f8f5a));
  roundRect(MAST_X + 8, JIB_Y + 1, 40, 4, 2, rgb(0xf6c94a));
  roundRect(MAST_X + 12, JIB_Y + 7, 32, 17, 3, rgb(0xd8f4ff));
  drawWizard(MAST_X + 28, JIB_Y + 14);
  rect(MAST_X + 12, JIB_Y + 22, 32, 2, rgb(0xf6c94a));

  // A wind pennant at the peak (it streams out in the Winkie Country).
  float streamX = 22 + s_wind * 0.6f;
  float wave = sinf(s_t * 7) * 2;
  const float pen[6] = {MAST_X, -2, MAST_X, 6, MAST_X + streamX, 2 + wave};
  poly(pen, 3, rgb(0x2fbf6a));

  // The emerald lamp at the jib's tip.
  bool on = fmodf(s_t, 1.2f) < 0.6f;
  circle(W - 6, JIB_Y + 4, 7, rgb(0x5dffb0), on ? 90 : 30);
  circle(W - 6, JIB_Y + 4, 3.5f, rgb(on ? 0x9dffd0 : 0x2a8a5a));
}

static void drawHook(float tx, float bx, float by, float w) {
  float hy = by - 16;
  line(tx - 3, JIB_Y + 6, bx - 2, hy, 1.5f, rgb(0x2a2a30));
  line(tx + 3, JIB_Y + 6, bx + 2, hy, 1.5f, rgb(0x2a2a30));
  roundRect(tx - 10, JIB_Y - 2, 20, 9, 2, rgb(0x3a3a46));       // the trolley
  circle(tx - 6, JIB_Y - 2, 2.5f, rgb(0x1a1a22));
  circle(tx + 6, JIB_Y - 2, 2.5f, rgb(0x1a1a22));
  roundRect(bx - 6, hy - 4, 12, 8, 2, rgb(0xf6c94a));          // the hook block
  circle(bx, hy + 6, 3, rgb(0x5a5a66));
  if (w <= 0) return;
  line(bx, hy + 6, bx - w / 2 + 8, by, 1.5f, rgb(0x2a2a30));     // slings to the corners
  line(bx, hy + 6, bx + w / 2 - 8, by, 1.5f, rgb(0x2a2a30));
}

static void drawCrow(float x, float y, float flap) {
  ellipse(x, y, 7, 4, rgb(0x14141c));
  circle(x + 6, y - 3, 3.4f, rgb(0x14141c));
  circle(x + 7, y - 4, 0.9f, rgb(0xffffff));
  const float bk[6] = {x + 9, y - 4, x + 14, y - 2.5f, x + 9, y - 1.5f};
  poly(bk, 3, rgb(0xf0c040));
  const float wg[6] = {x - 6, y - 1, x + 3, y - 1, x - 2, y - 8 - flap * 6};
  poly(wg, 3, rgb(0x2a2a3a));
  const float tl[6] = {x - 6, y, x - 13, y - 2, x - 12, y + 3};
  poly(tl, 3, rgb(0x14141c));
}

static void drawMonkey(float x, float y, float flap) {
  const float wl[6] = {x - 3, y - 2, x - 22, y - 10 - flap * 8, x - 16, y + 4};
  const float wr[6] = {x + 3, y - 2, x + 22, y - 10 - flap * 8, x + 16, y + 4};
  poly(wl, 3, rgb(0x7a8ab8));
  poly(wr, 3, rgb(0x7a8ab8));
  ellipse(x, y + 2, 6, 8, rgb(0x7a4a2a));
  circle(x, y - 8, 5.5f, rgb(0x7a4a2a));
  circle(x, y - 7, 3.6f, rgb(0xe8c09a));
  circle(x - 5, y - 9, 2, rgb(0x7a4a2a));
  circle(x + 5, y - 9, 2, rgb(0x7a4a2a));
  rect(x - 3, y - 15, 6, 3, rgb(0xf6c94a)); // the Golden Cap's servant
}

static void drawBalloon(float x, float y, float s) {
  ellipse(x, y, 17 * s, 20 * s, rgb(0x2fbf6a));          // green silk, as in the book
  for (int i = -1; i <= 1; i++) ellipse(x + i * 9 * s, y, 3 * s, 19 * s, rgb(0x7de0a0), 110);
  rect(x - 12 * s, y + 10 * s, 24 * s, 3 * s, rgb(0xf6c94a));
  line(x - 10 * s, y + 13 * s, x - 5 * s, y + 26 * s, 0.8f, rgb(0x5a4a3a));
  line(x + 10 * s, y + 13 * s, x + 5 * s, y + 26 * s, 0.8f, rgb(0x5a4a3a));
  roundRect(x - 6 * s, y + 25 * s, 12 * s, 8 * s, 2, rgb(0x9a6a3a));
}

static void drawDorothy(float x, float gy) {
  ellipse(x, gy, 9, 2.5f, rgb(0x000000), 50);
  rect(x - 3, gy - 6, 2, 6, rgb(0xf2d2b0));
  rect(x + 1, gy - 6, 2, 6, rgb(0xf2d2b0));
  rect(x - 4, gy - 2, 3, 2, rgb(0xd8dce6)); // the silver shoes
  rect(x + 1, gy - 2, 3, 2, rgb(0xd8dce6));
  const float dress[8] = {x - 4, gy - 18, x + 4, gy - 18, x + 7, gy - 6, x - 7, gy - 6};
  poly(dress, 4, rgb(0x6aa0e8));
  for (int i = 0; i < 3; i++) rect(x - 5 + i * 4, gy - 14, 1.5f, 8, rgb(0xffffff), 140); // gingham
  rect(x - 4, gy - 16, 8, 1.5f, rgb(0xffffff), 140);
  circle(x, gy - 22, 4.5f, rgb(0xf2d2b0));
  ellipse(x, gy - 25, 5, 2.5f, rgb(0x7a4a22));
  ellipse(x - 5, gy - 20, 1.6f, 4, rgb(0x7a4a22));  // pigtails
  ellipse(x + 5, gy - 20, 1.6f, 4, rgb(0x7a4a22));
  // Toto
  ellipse(x + 13, gy - 4, 5, 3.2f, rgb(0x1a1a20));
  circle(x + 18, gy - 7, 3, rgb(0x1a1a20));
  const float ear[6] = {x + 16, gy - 9, x + 18, gy - 13, x + 19.5f, gy - 9};
  poly(ear, 3, rgb(0x1a1a20));
  rect(x + 9, gy - 2, 1.5f, 2, rgb(0x1a1a20));
  rect(x + 15, gy - 2, 1.5f, 2, rgb(0x1a1a20));
}

static void drawTree(float x, float gy, float s, uint32_t leaf) {
  rect(x - 2 * s, gy - 14 * s, 4 * s, 14 * s, rgb(0x5a3a22));
  circle(x, gy - 20 * s, 11 * s, rgb(shade(leaf, 0.2f)));
  circle(x - 6 * s, gy - 16 * s, 8 * s, rgb(leaf));
  circle(x + 6 * s, gy - 17 * s, 8 * s, rgb(tint(leaf, 0.1f)));
}

static void drawMunchkinHouse(float x, float gy, float s) {
  rect(x - 12 * s, gy - 14 * s, 24 * s, 14 * s, rgb(0x78b0ff));
  float pts[24];
  for (int i = 0; i <= 10; i++) {
    float a = 3.14159f + i * 0.314159f;
    pts[i * 2] = x + cosf(a) * 13 * s;
    pts[i * 2 + 1] = gy - 14 * s + sinf(a) * 12 * s;
  }
  poly(pts, 11, rgb(0x4f86e8));
  rect(x - 3 * s, gy - 9 * s, 6 * s, 9 * s, rgb(0x2a4a9a));
  circle(x + 7 * s, gy - 9 * s, 2.5f * s, rgb(0xffe9a0));
  circle(x, gy - 27 * s, 2 * s, rgb(0xf6c94a));
}

static void drawScarecrow(float x, float gy) {
  line(x, gy, x, gy - 34, 2, rgb(0x6a4a2a));
  line(x - 14, gy - 26, x + 14, gy - 26, 2, rgb(0x6a4a2a));
  rect(x - 6, gy - 28, 12, 13, rgb(0x3a5aa8));
  line(x - 6, gy - 26, x - 14, gy - 22, 2.5f, rgb(0x3a5aa8));
  line(x + 6, gy - 26, x + 14, gy - 22, 2.5f, rgb(0x3a5aa8));
  circle(x, gy - 33, 5, rgb(0xf0e0b0));
  circle(x - 2, gy - 34, 0.9f, rgb(0x2a2a2a));
  circle(x + 2, gy - 34, 0.9f, rgb(0x2a2a2a));
  ellipse(x, gy - 38, 8, 2, rgb(0xd9b84a));
  rect(x - 4, gy - 42, 8, 4, rgb(0xd9b84a));
}

static void drawLion(float x, float gy) {
  ellipse(x, gy - 6, 16, 6, rgb(0xd9a64a));
  circle(x - 15, gy - 9, 8, rgb(0x9a5a22));               // mane
  circle(x - 15, gy - 9, 5, rgb(0xe8b860));
  circle(x - 17, gy - 10, 0.8f, rgb(0x2a1a10));
  line(x + 14, gy - 6, x + 22, gy - 12, 1.5f, rgb(0xd9a64a));
  circle(x + 22, gy - 12, 2, rgb(0x9a5a22));
  float zt = fmodf(s_t, 2.0f);
  txt((int)(x - 10 + zt * 4), (int)(gy - 26 - zt * 8), "z", 1, rgb(0xffffff), false);
}

// ---------------------------------------------------------------------------
// Sky, distance and ground
// ---------------------------------------------------------------------------

static void makeClouds() {
  seedNoise(0x0251 + s_ch * 77);
  for (int y = 0; y < CLOUD_H; y++) {
    for (int x = 0; x < FW; x++) {
      float a = x / (float)FW * 6.2831853f; // wraps left to right
      float n = fbm(cosf(a) * 1.6f + 7, sinf(a) * 1.6f + y * 0.045f, 4);
      float band = 0.5f + 0.5f * sinf(y * 0.07f);           // clouds come in layers
      s_clouds[y * FW + x] = (uint8_t)(clampf((n + 0.05f) * 3.0f * band, 0, 1) * 255);
    }
  }
  for (int i = 0; i < 60; i++) s_hills[i] = fbm(i * 0.11f + 3, 1.7f, 3);
}

static void drawSky() {
  float alt = clampf((s_cam - FLOOR_H) / 420, 0, 1); // higher up, the sky deepens
  uint32_t top = shade(E->skyTop, 0.25f * alt), low = mixHex(E->skyLow, E->skyTop, 0.45f * alt);
  int scroll = (int)(s_cam * K * 0.35f);
  int drift = (int)(s_t * 2.5f);
  uint16_t cloudC = rgb(E->cloud);
  float cloudAmt = E->night > 0.8f ? 0.28f : 0.8f;
  for (int y = 0; y < FH; y++) {
    uint16_t base = mix(top, low, y / (float)(FH - 1));
    int ty = ((y - scroll) % CLOUD_H + CLOUD_H) % CLOUD_H;
    uint16_t *row = s_frame + y * FW;
    const uint8_t *cl = s_clouds + ty * FW;
    for (int x = 0; x < FW; x++) {
      uint8_t a = cl[(x + drift) % FW];
      row[x] = a ? blend(base, cloudC, (uint8_t)(a * cloudAmt)) : base;
    }
    if (E->night > 0.3f) // stars
      for (int x = (y * 37) % 23; x < FW; x += 23)
        if (((x * 7 + (y - scroll) * 13) & 63) == 0 && cl[(x + drift) % FW] < 40) row[x] = rgb(0xfff6d8);
  }
  // Sun or moon, sinking slowly as the tower rises.
  setScale(K);
  float sy = 70 + s_cam * 0.08f;
  if (E->night > 0.8f) {
    circle(380, sy, 14, rgb(0xfff6d0));
    circle(386, sy - 4, 12, rgb(shade(E->skyTop, 0.0f)));
  } else {
    uint32_t sun = E->night > 0.3f ? 0xffb050 : 0xfff2b0;
    circle(390, sy, 22, rgb(sun), 60);
    circle(390, sy, 14, rgb(sun));
  }
  // The Wizard's green balloon, drifting high above.
  float by = 112 + (s_cam - FLOOR_H) * 0.16f;
  if (by > -40 && by < H) drawBalloon(s_balloonX, by, 0.9f);
}

// The Emerald City far off: spires and domes, glinting. `haze` fades it
// into the sky.
static void drawSkyline(float cx, float hy, float s, float haze) {
  static const float SPIRES[9][3] = { // x offset, height, kind (0 spire, 1 dome, 2 gold spire)
      {-46, 22, 1}, {-34, 34, 0}, {-22, 28, 2}, {-10, 44, 0}, {0, 58, 0},
      {12, 40, 1}, {24, 30, 2}, {35, 36, 0}, {47, 20, 1}};
  uint32_t body = mixHex(0x2fbf7a, E->skyLow, haze), lite = mixHex(0x9dffd0, E->skyLow, haze * 0.8f);
  uint32_t gold = mixHex(0xf6c94a, E->skyLow, haze);
  rect(cx - 54 * s, hy - 10 * s, 108 * s, 12 * s, rgb(body)); // the city wall
  for (int i = 0; i < 9; i++) rect(cx - 54 * s + i * 13 * s, hy - 13 * s, 6 * s, 3 * s, rgb(body));
  for (const auto &sp : SPIRES) {
    float x = cx + sp[0] * s, h = sp[1] * s, w = 9 * s;
    if (sp[2] == 1) {
      rect(x - w / 2, hy - h * 0.6f, w, h * 0.6f, rgb(body));
      circle(x, hy - h * 0.6f, w * 0.62f, rgb(body));
      rect(x - 0.5f, hy - h * 0.6f - w, 1, w * 0.5f, rgb(gold));
    } else {
      rect(x - w / 2, hy - h * 0.7f, w, h * 0.7f, rgb(body));
      rect(x - w / 2, hy - h * 0.7f, w * 0.3f, h * 0.7f, rgb(lite), 90);
      const float cap[6] = {x - w / 2 - 1, hy - h * 0.7f, x + w / 2 + 1, hy - h * 0.7f, x, hy - h};
      poly(cap, 3, rgb(sp[2] == 2 ? gold : body));
      rect(x - w / 2, hy - h * 0.7f - 1, w, 1.5f, rgb(gold));
    }
    float tw = 0.5f + 0.5f * sinf(s_t * 2.3f + sp[0]);
    circle(x, hy - h - 1, 1.8f * s, rgb(lite), (uint8_t)(120 + 120 * tw)); // a glint at the top
  }
  if (E->night > 0.3f)
    for (int i = 0; i < 12; i++) circle(cx - 48 * s + i * 8.5f * s, hy - 5 * s, 1.4f, rgb(0xc8ffe0), 170);
}

// Far hills and the Emerald City on the horizon, with a little parallax.
static void drawDistance(float gy) {
  float hy = 168 + (gy - 182) * 0.55f;
  if (hy > H + 40) return;
  uint32_t far = mixHex(E->hills, E->skyLow, 0.35f);
  // The city's far skyline - in the last land it fills the horizon.
  if (s_ch == ST_EMERALD) {
    drawSkyline(110, hy, 1.5f, 0.15f);
    drawSkyline(400, hy, 1.2f, 0.25f);
  } else {
    drawSkyline(372, hy, 0.85f, 0.5f);
  }
  // Hills in front of it.
  float pts[8];
  for (int i = 0; i < 59; i++) {
    float x0 = i * (W / 58.0f), x1 = (i + 1) * (W / 58.0f);
    float h0 = 12 + s_hills[i] * 30, h1 = 12 + s_hills[i + 1] * 30;
    pts[0] = x0; pts[1] = hy - h0; pts[2] = x1; pts[3] = hy - h1; pts[4] = x1; pts[5] = hy + 4; pts[6] = x0; pts[7] = hy + 4;
    poly(pts, 4, rgb(far));
  }
  if (gy > hy) rect(0, hy + 2, W, gy - hy, rgb(mixHex(E->hills, E->ground1, 0.5f)));
}

static void drawGround(float gy) {
  if (gy >= H) return;
  vgrad(0, gy, W, H - gy + 2, E->ground1, E->ground2);
  // Scenery of the land, either side of the road.
  switch ((Style)s_ch) {
    case ST_MUNCHKIN:
      drawMunchkinHouse(330, gy + 22, 1.1f);
      drawMunchkinHouse(408, gy + 30, 1.3f);
      drawMunchkinHouse(120, gy + 18, 0.9f);
      drawTree(372, gy + 16, 0.8f, 0x4fa86a);
      for (int i = 0; i < 26; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        circle(270 + (h >> 8) % 180, gy + 8 + (h >> 20) % 64, 1.6f, rgb(i & 1 ? 0xffffff : 0xffd84a));
      }
      break;
    case ST_CORN:
      for (float x = 300; x < W; x += 9)
        for (int r = 0; r < 3; r++) {
          float y0 = gy + 12 + r * 18;
          line(x + r * 3, y0, x + r * 3, y0 - 16, 1.4f, rgb(0x5a9a3a));
          ellipse(x + r * 3 + 2, y0 - 10, 1.5f, 3.5f, rgb(0xffd84a));
        }
      drawScarecrow(360, gy + 44);
      for (float x = 90; x < 160; x += 9) {
        line(x, gy + 22, x, gy + 6, 1.4f, rgb(0x5a9a3a));
        ellipse(x + 2, gy + 12, 1.5f, 3.5f, rgb(0xffd84a));
      }
      break;
    case ST_TIN:
      drawTree(310, gy + 30, 1.6f, 0x2f7a4a);
      drawTree(360, gy + 22, 1.2f, 0x3f8a5a);
      drawTree(420, gy + 36, 1.9f, 0x2a6a40);
      drawTree(110, gy + 26, 1.4f, 0x3f8a5a);
      rect(372, gy + 34, 8, 9, rgb(0xc0c8d6));      // the Tin Woodman's oil can
      line(380, gy + 36, 388, gy + 30, 1.4f, rgb(0xc0c8d6));
      ellipse(376, gy + 34, 4, 1.5f, rgb(0x8e99ab));
      break;
    case ST_POPPY:
      for (int i = 0; i < 70; i++) {
        float x = (i * 53) % 456, y = gy + 6 + (i * 29) % 80;
        if (fabsf(x - (TOWER_X - (y - gy) * 0.9f)) < 30) continue; // keep off the road
        circle(x, y, 2.4f, rgb(0xe8283c));
        circle(x, y, 0.8f, rgb(0x2a1018));
      }
      drawLion(380, gy + 34); // the Cowardly Lion, fast asleep
      break;
    case ST_WINKIE:
      for (int i = 0; i < 24; i++) line((i * 41) % 456, gy + 10 + (i * 17) % 60, (i * 41) % 456 + 3, gy + 4 + (i * 17) % 60, 1, rgb(0xa8780a));
      drawTree(330, gy + 26, 1.1f, 0xc8a83a);
      drawTree(410, gy + 34, 1.4f, 0xb0902a);
      break;
    case ST_EMERALD:
      for (float x = 280; x < W; x += 40) {          // street lamps
        line(x, gy + 30, x, gy + 6, 1.6f, rgb(0x2a3a3a));
        circle(x, gy + 5, 6, rgb(0x5dffb0), 70);
        circle(x, gy + 5, 3, rgb(0xc8ffe0));
      }
      for (int i = 0; i < 6; i++) rect(270 + i * 32, gy + 40, 20, 1.5f, rgb(0x2a8a5a), 140);
      break;
  }
  // The Yellow Brick Road, from the front door down and away.
  float x0 = TOWER_X - 9, x1 = TOWER_X + 9;
  trap(x0, x1, gy, 110, 200, H + 2, rgb(0xf5d142));
  for (float yy = gy + 5, k = 0; yy < H; yy += 6, k++) {
    float t = (yy - gy) / (H - gy);
    float l = lerpf(x0, 110, t), r = lerpf(x1, 200, t);
    rect(l, yy, r - l, 1, rgb(0xc89a1a), 170);
    for (float bx = l + (((int)k) & 1 ? 4 : 9); bx < r; bx += 10 + t * 8) rect(bx, yy - 5, 1, 5, rgb(0xc89a1a), 120);
  }
  drawDorothy(TOWER_X + 70, gy + 26);
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

static void setMode(Mode m) { s_mode = m; s_modeT = 0; }

static float swingSpeed() {
  float w = E->speed * (1 + 0.03f * (s_n - 1));
  if (E->twist == TW_DROWSY) w *= 0.55f + 0.6f * (0.5f + 0.5f * sinf(s_t * 0.55f));
  if (s_crow.on && s_crow.t > 0.5f) w *= 1.2f;
  return w;
}

static void newHook() {
  s_hanging = true;
  s_lower = 0;
  s_spawnT = -1;
  s_phase = frand() * 6.28f;
  RunAudio::fx(RunAudio::Tick);
}

static void startLand(int ch) {
  s_ch = ch;
  E = &LANDS_[ch];
  s_rng = esp_random() | 1;
  if (s_clouds) makeClouds();
  s_n = 1;
  s_floors[0] = {0, 0};
  s_roofOn = false;
  s_cam = s_camTarget = topWorld();
  s_swayAmp = 0; s_trim = 0; s_wind = 0;
  s_hearts = HEARTS; s_combo = 0; s_pop = 0; s_land = 0; s_litTo = 0;
  s_falling = false;
  s_tumble[0].on = s_tumble[1].on = false;
  for (Popup &p : s_pops) p.t = 0;
  for (Spark &s : s_sparks) s.life = 0;
  s_crow.on = s_monkey.on = false;
  s_crowNext = 3 + frand() * 3;
  newHook();
}

static void tumble(float x, float y, float dir, bool roof) {
  Tumble &t = s_tumble[s_tumble[0].on ? 1 : 0];
  t = {true, roof, x, y, dir * 70, -40, 0, dir * 3.5f};
}

static void loseHeart(const char *why) {
  s_hearts--;
  s_combo = 0;
  RunAudio::fx(RunAudio::Thunder);
  pop(TOWER_X, TOP_SY - 40, why, 0xff5a6e);
  if (s_hearts <= 0) setMode(Mode::Fail);
}

static void landFloor() {
  int top = s_n - 1;
  float dx = s_fx - floorX(top);
  bool roof = roofNext();
  s_falling = false;
  if (fabsf(dx) > MISS) {
    tumble(s_fx, TOP_SY - FLOOR_H, dx > 0 ? 1 : -1, roof);
    loseHeart("MISSED!");
    s_spawnT = 0.6f;
    return;
  }
  bool perfect = fabsf(dx) <= PERFECT;
  float x = s_floors[top].x + (perfect ? 0 : dx);
  // Too far out of balance and the new floor topples off again.
  float com = 0;
  for (int i = 1; i <= top; i++) com += s_floors[i].x;
  com = (com + x) / s_n;
  if (fabsf(com) > TIP) {
    tumble(s_fx, TOP_SY - FLOOR_H, com > 0 ? 1 : -1, roof);
    s_swayAmp = fminf(26, s_swayAmp + 6);
    loseHeart("IT TIPPED!");
    s_spawnT = 0.6f;
    return;
  }
  s_floors[s_n] = {x, (uint8_t)(frand() * 255)};
  s_n++;
  s_land = 1;
  float q = 1 - fabsf(dx) / MISS;
  uint32_t got = (uint32_t)((8 + 3 * s_ch + s_n) * (0.4f + 0.6f * q) + 0.5f);
  char b[18];
  if (perfect) {
    s_combo++;
    got = got * 2 + 4 * s_combo;
    s_swayAmp *= 0.55f;
    RunAudio::fx(s_combo >= 3 ? RunAudio::DingTop : RunAudio::DingHigh);
    for (int i = 0; i < 16; i++) spark(floorX(top) + (frand() - 0.5f) * FLOOR_W, TOP_SY, (frand() - 0.5f) * 120, -40 - frand() * 90, 0.9f, 0x7dffc0, 1);
    snprintf(b, sizeof(b), "PERFECT +%u", (unsigned)got);
    pop(TOWER_X, TOP_SY - 50, b, 0x3ddc84);
    if (s_combo % 3 == 0) {
      // A streak brings the Winged Monkeys with more people.
      s_monkey = {true, -30, 60, 0};
      got += 25;
      RunAudio::fx(RunAudio::Squeak);
    }
  } else {
    s_combo = 0;
    s_swayAmp = fminf(30, s_swayAmp * 0.85f + fabsf(dx) * 0.22f * E->swayGain);
    RunAudio::fx(RunAudio::Bonk);
    snprintf(b, sizeof(b), "+%u", (unsigned)got);
    pop(TOWER_X, TOP_SY - 50, b, 0xffffff);
  }
  for (int i = 0; i < 6; i++) spark(s_fx + (frand() - 0.5f) * FLOOR_W, TOP_SY + 2, (frand() - 0.5f) * 60, -10 - frand() * 20, 0.5f, 0xe8e0d0, 2);
  s_pop += got;
  if (roof) {
    s_roofOn = true;
    s_hanging = false;
    RunAudio::fx(RunAudio::DingTop);
    setMode(Mode::Built);
    return;
  }
  s_camTarget = topWorld();
  s_spawnT = 0.35f;
}

static void drop() {
  if (s_mode != Mode::Play || !s_hanging || s_lower < 1) return;
  s_hanging = false;
  s_falling = true;
  s_fx = hookX();
  s_fy = hangY();
  s_fvx = 0; // straight down from where it is: what you see is where it lands
  s_fvy = 0;
  if (s_crow.on) { s_crow.on = false; RunAudio::fx(RunAudio::Gull); }
  RunAudio::fx(RunAudio::PopSoft);
}

static void finishLand() {
  if (s_pop > s_best[s_ch]) {
    s_best[s_ch] = s_pop;
    char k[4] = {'b', (char)('0' + s_ch), 0};
    s_prefs.putUInt(k, s_pop);
  }
  if (s_ch + 1 > s_unlocked && s_ch + 1 < LANDS) { s_unlocked = s_ch + 1; s_prefs.putUChar("unl", s_unlocked); }
  setMode(s_ch == LANDS - 1 ? Mode::Finale : Mode::Cleared);
}

static void update(float dt) {
  s_t += dt;
  s_modeT += dt;
  for (Popup &p : s_pops) if (p.t > 0) { p.t -= dt; p.y -= dt * 22; }
  for (Spark &s : s_sparks) {
    if (s.life <= 0) continue;
    s.life -= dt;
    s.x += s.vx * dt; s.y += s.vy * dt;
    s.vy += (s.kind == 3 ? 40 : 160) * dt;
  }
  for (Tumble &t : s_tumble) {
    if (!t.on) continue;
    t.vy += 900 * dt; t.x += t.vx * dt; t.y += t.vy * dt; t.a += t.va * dt;
    if (t.y > H + 80) t.on = false;
  }
  s_swayPh += dt * 2.1f;
  s_swayAmp = fmaxf(0, s_swayAmp - dt * 0.6f);
  if (s_land > 0) s_land = fmaxf(0, s_land - dt * 5);
  s_cam += (s_camTarget - s_cam) * fminf(1, dt * 4);
  s_balloonX -= dt * 6;
  if (s_balloonX < -40) s_balloonX = W + 40;
  if (s_monkey.on) {
    s_monkey.t += dt;
    s_monkey.x += dt * 170;
    s_monkey.y = 60 + sinf(s_monkey.t * 5) * 6;
    if (s_monkey.x > W + 40) s_monkey.on = false;
  }

  if (s_mode == Mode::Title || s_mode == Mode::Ready || s_mode == Mode::Play) {
    s_phase += swingSpeed() * dt;
    if (s_lower < 1) s_lower = fminf(1, s_lower + dt / 0.45f);
  }
  if (s_mode == Mode::Ready && s_modeT > 2.2f) setMode(Mode::Play);

  if (s_mode == Mode::Play) {
    if (E->twist == TW_WIND) s_wind = 24 * sinf(s_t * 0.33f) + 10 * sinf(s_t * 0.9f + 1);
    else if (E->twist == TW_FINALE) s_wind = 8 * sinf(s_t * 0.4f);
    if (s_spawnT >= 0) {
      s_spawnT -= dt;
      if (s_spawnT < 0 && s_hearts > 0) newHook();
    }
    // The Witch's crows: one lands on the swinging floor and rocks it.
    if (E->twist == TW_CROWS) {
      if (s_crow.on) s_crow.t += dt;
      else if (s_hanging && s_lower >= 1 && (s_crowNext -= dt) <= 0) {
        s_crow = {true, 0, 0, 0};
        s_crowNext = 4 + frand() * 4;
        RunAudio::fx(RunAudio::Gull);
      }
    }
    if (s_falling) {
      s_fvy += 1500 * dt;
      s_fy += s_fvy * dt;
      s_fx += (s_fvx + (E->twist == TW_WIND ? s_wind * 0.5f : 0)) * dt;
      if (s_fy + FLOOR_H >= TOP_SY) landFloor();
    }
    if (s_auto && s_hanging && s_lower >= 1) {
      static float lastD = 0;
      float d = hookX() - floorX(s_n - 1);
      if ((d > 0) != (lastD > 0) && fabsf(d) < 6) drop(); // as it swings past the middle
      lastD = d;
    }
  }

  if (s_mode == Mode::Built) {
    // The camera glides down the finished tower while every window lights.
    float t = clampf(s_modeT / 3.2f, 0, 1);
    s_camTarget = lerpf(topWorld() + 40, FLOOR_H * 3, t * t);
    s_litTo = t * s_n * 1.2f;
    if (fmodf(s_modeT, 0.25f) < dt) {
      float fx = 60 + frand() * (W - 120), fy = 40 + frand() * 90;
      uint32_t c = s_ch == ST_EMERALD ? 0x7dffc0 : (frand() < 0.5f ? 0xffd84a : 0xff8fb1);
      for (int i = 0; i < 18; i++) {
        float a = i * 0.349f;
        spark(fx, fy, cosf(a) * 70, sinf(a) * 70, 1.0f, c, 3);
      }
      RunAudio::fx(RunAudio::Pop);
    }
    if (s_modeT > 4.0f) finishLand();
  }
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

static void card(float x, float y, float w, float h) {
  roundRect(x, y, w, h, 16, rgb(0xfbf6e4), 240);
  roundRect(x, y, w, 4, 2, rgb(0x1f9a5a));
  roundRect(x, y + h - 4, w, 4, 2, rgb(0xf6c94a));
}

static void drawTumble(const Tumble &t) {
  float c = cosf(t.a), s = sinf(t.a), hw = FLOOR_W / 2, hh = FLOOR_H / 2;
  float cx = t.x, cy = t.y + hh;
  auto P = [&](float dx, float dy, float &ox, float &oy) { ox = cx + dx * c - dy * s; oy = cy + dx * s + dy * c; };
  float x1, y1, x2, y2, x3, y3, x4, y4;
  P(-hw, -hh, x1, y1); P(hw, -hh, x2, y2); P(hw, hh, x3, y3); P(-hw, hh, x4, y4);
  quad4(x1, y1, x2, y2, x3, y3, x4, y4, rgb(t.roof ? E->trim : E->wall1));
  P(-hw, -hh + 5, x4, y4); P(hw, -hh + 5, x3, y3);
  quad4(x1, y1, x2, y2, x3, y3, x4, y4, rgb(E->trim));
}

static void drawWorld() {
  target(s_frame, FW, FH);
  setScale(1);
  drawSky();
  setScale(K);
  float gy = groundY();
  drawDistance(gy);
  drawGround(gy);

  // The tower, from the bottom up (only what's on screen).
  float night = E->night;
  // (Once the roof is on, the last entry is the roof itself.)
  int floors = s_roofOn ? s_n - 1 : s_n;
  for (int i = 0; i < floors; i++) {
    float y = worldToScreen((i + 1) * FLOOR_H);
    if (y > H + 4 || y < -FLOOR_H - 130) continue;
    float lit = night * 0.85f;
    if (s_mode == Mode::Built || s_mode == Mode::Cleared || s_mode == Mode::Finale) lit = fmaxf(lit, clampf(s_litTo - i, 0, 1));
    float sq = (i == s_n - 1 && s_land > 0) ? s_land * 2 : 0; // the newest floor settles
    drawFloor(floorX(i), y + sq, s_floors[i].seed, i == 0, lit);
  }
  if (s_roofOn) drawRoof(floorX(s_n - 1), worldToScreen(floors * FLOOR_H), fmaxf(night, clampf(s_litTo - s_n, 0, 1)));

  drawCrane();
  if (s_mode == Mode::Title || s_mode == Mode::Ready || s_mode == Mode::Play || s_mode == Mode::Paused) {
    float tx = trolleyX();
    if (s_hanging) {
      float bx = hookX(), by = hangY();
      drawHook(tx, bx, by, FLOOR_W);
      if (roofNext()) drawRoof(bx, by + FLOOR_H, night);
      else drawFloor(bx, by, s_n * 37, false, night * 0.6f);
      if (s_crow.on) {
        float k = clampf(s_crow.t / 0.6f, 0, 1);
        float cx = lerpf(W + 20, bx + 24, k), cy = lerpf(40, by - 4, k);
        drawCrow(cx, cy, k < 1 ? fabsf(sinf(s_t * 18)) : 0);
      }
    } else {
      drawHook(tx, tx, HANG_Y - 4, 0);
    }
    if (s_falling) {
      if (roofNext()) drawRoof(s_fx, s_fy + FLOOR_H, night);
      else drawFloor(s_fx, s_fy, s_n * 37, false, night * 0.6f);
    }
  }
  for (const Tumble &t : s_tumble) if (t.on) drawTumble(t);
  if (s_monkey.on) drawMonkey(s_monkey.x, s_monkey.y, fabsf(sinf(s_monkey.t * 14)));

  for (const Spark &s : s_sparks) {
    if (s.life <= 0) continue;
    uint8_t a = (uint8_t)clampf(s.life * 300, 0, 255);
    if (s.kind == 1) gem(s.x, s.y, 2.5f, s.col);
    else circle(s.x, s.y, s.kind == 3 ? 2.2f : 2.6f, rgb(s.col), a);
  }
  setScale(1);
}

static void hud() {
  char b[24];
  setScale(K);
  const float Y = H - 28; // along the bottom, out of the crane's way
  roundRect(MAST_X + 16, Y, 132, 22, 11, rgb(0x0a2a1a), 170);
  for (int i = 0; i < HEARTS; i++) heart(MAST_X + 30 + i * 17, Y + 11, 6, rgb(i < s_hearts ? 0x3ddc84 : 0x3a5a4a));
  int built = s_n - 1; // the roof, once on, is the last entry
  roundRect(W - 136, Y, 128, 22, 11, rgb(0x0a2a1a), 170);
  setScale(1);
  snprintf(b, sizeof(b), "%d/%d", built, E->floors + 1);
  txt(MAST_X + 86, (int)Y + 5, b, 1, rgb(0xffffff), false);
  snprintf(b, sizeof(b), "%u", (unsigned)s_pop);
  txt(W - 124, (int)Y + 5, "POP", 1, rgb(0xf6c94a), false);
  txt(W - 78, (int)Y + 5, b, 1, rgb(0xffffff), false);
  if (s_combo >= 2) {
    snprintf(b, sizeof(b), "x%d STREAK", s_combo);
    txtC(W / 2, (int)Y - 22, b, 1, rgb(0x7dffc0));
  }
}

static void render() {
  drawWorld();
  for (const Popup &p : s_pops) if (p.t > 0) txtC((int)p.x, (int)p.y, p.text, 1, rgb(p.col));
  if (s_mode == Mode::Play || s_mode == Mode::Ready || s_mode == Mode::Paused || s_mode == Mode::Built) hud();

  char b[96];
  const uint16_t T_INK = rgb(0x173a28), T_GREEN = rgb(0x12804a), T_SOFT = rgb(0x6a7a6a), T_GOLD = rgb(0xa8761a);
  switch (s_mode) {
    case Mode::Title: {
      setScale(K);
      card(14, 20, W - 28, H - 40);
      setScale(1);
      plate(DENSLOW_FRIENDS, DENSLOW_FRIENDS_W, DENSLOW_FRIENDS_H, 12, (FH - DENSLOW_FRIENDS_H) / 2, 0x0d3b26, 0xfbf6e4);
      // The column right of the picture holds 17 characters a line.
      const int CX = 332;
      txtC(CX, 28, "RAISE THE", 1, T_GOLD, false);
      txtC(CX, 42, "EMERALD", 2, T_GREEN, false);
      txtC(CX, 74, "CITY", 2, T_GREEN, false);
      snprintf(b, sizeof(b), "<  %d of %d  >", s_pick + 1, LANDS);
      txtC(CX, 108, b, 1, T_SOFT, false);
      const Land &l = LANDS_[s_pick];
      int y = wrapC(CX, 126, l.name, 17, s_pick <= s_unlocked ? T_GREEN : T_SOFT) + 3;
      if (s_pick <= s_unlocked) {
        snprintf(b, sizeof(b), "%s %s", l.line1, l.line2);
        y = wrapC(CX, y, b, 17, T_INK);
        if (s_best[s_pick]) {
          snprintf(b, sizeof(b), "Best %u", (unsigned)s_best[s_pick]);
          txtC(CX, y + 2, b, 1, T_GOLD, false);
        }
      } else {
        wrapC(CX, y, "Build the land before first.", 17, T_SOFT);
      }
      txtC(CX, 244, "SELECT to build", 1, T_SOFT, false);
      break;
    }
    case Mode::Ready:
      setScale(K);
      roundRect(70, 176, W - 140, 66, 14, rgb(0x0a2a1a), 190);
      setScale(1);
      snprintf(b, sizeof(b), "%d. %s", s_ch + 1, E->name);
      txtC(W / 2, 184, b, 1, rgb(0xffffff));
      snprintf(b, sizeof(b), "%d floors and a roof", E->floors);
      txtC(W / 2, 204, b, 1, rgb(0xf6c94a));
      txtC(W / 2, 222, "SELECT drops the floor", 1, rgb(0xc8ffe0));
      break;
    case Mode::Paused:
      setScale(K);
      card(100, 80, W - 200, 110);
      setScale(1);
      txtC(W / 2, 100, "PAUSED", 2, T_GREEN, false);
      txtC(W / 2, 148, "SELECT go on  DOWN quit", 1, T_SOFT, false);
      break;
    case Mode::Cleared:
      setScale(K);
      card(56, 46, W - 112, 188);
      setScale(1);
      snprintf(b, sizeof(b), "%s IS BUILT!", E->name);
      wrapC(W / 2, 62, b, 26, T_GREEN);
      snprintf(b, sizeof(b), "%u %s moved in.", (unsigned)s_pop, E->folk);
      txtC(W / 2, 112, b, 1, T_INK, false);
      if (s_pop >= s_best[s_ch]) txtC(W / 2, 132, "A new best!", 1, T_GOLD, false);
      snprintf(b, sizeof(b), "Next: %s", LANDS_[s_ch + 1].name);
      txtC(W / 2, 166, b, 1, T_INK, false);
      txtC(W / 2, 200, "SELECT go on  UP menu", 1, T_SOFT, false);
      break;
    case Mode::Fail:
      setScale(K);
      card(14, 20, W - 28, H - 40);
      setScale(1);
      plate(DENSLOW_WIZARD, DENSLOW_WIZARD_W, DENSLOW_WIZARD_H, 16, (FH - DENSLOW_WIZARD_H) / 2, 0x0d3b26, 0xfbf6e4);
      txtC(292, 46, "THE TOWER", 2, T_GREEN, false);
      txtC(292, 76, "FELL", 2, T_GREEN, false);
      wrapC(292, 116, "\"I'm a very good man,\" says the Wizard, \"but a very bad Wizard.\"", 20, T_INK);
      txtC(292, 196, "Let's build it again.", 1, T_GOLD, false);
      txtC(292, 236, "SELECT again  UP menu", 1, T_SOFT, false);
      break;
    case Mode::Finale: {
      setScale(K);
      card(14, 6, W - 28, H - 12);
      setScale(1);
      plate(DENSLOW_CITY, DENSLOW_CITY_W, DENSLOW_CITY_H, (FW - DENSLOW_CITY_W) / 2, 6, 0x0d5a36, 0xeafbe8);
      setScale(K);
      roundRect(24, 196, W - 48, 76, 12, rgb(0xfbf6e4), 240);
      setScale(1);
      uint32_t total = 0;
      for (int i = 0; i < LANDS; i++) total += s_best[i];
      txtC(W / 2, 204, "THE EMERALD CITY SHINES AGAIN!", 1, T_GREEN, false);
      snprintf(b, sizeof(b), "%u people came home to Oz.", (unsigned)total);
      txtC(W / 2, 222, b, 1, T_INK, false);
      txtC(W / 2, 240, "There's no place like home.", 1, T_GOLD, false);
      txtC(W / 2, 258, "SELECT menu", 1, T_SOFT, false);
      break;
    }
    default: break;
  }
  setScale(1);
  HalfRes::present(s_panel, s_gfx->flipped(), s_frame);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

static void *bigAlloc(size_t bytes, bool preferInternal) {
  if (preferInternal && heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > bytes + 90 * 1024) {
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) return p;
  }
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel) {
  if (s_active) return true;
  s_gfx = gfx;
  s_panel = panel;
  s_frame = (uint16_t *)bigAlloc((size_t)FW * FH * 2, true);
  s_clouds = (uint8_t *)bigAlloc((size_t)FW * CLOUD_H, false);
  if (!s_frame || !s_clouds) {
    heap_caps_free(s_frame); s_frame = nullptr;
    heap_caps_free(s_clouds); s_clouds = nullptr;
    Serial.println("Emerald City: not enough memory");
    return false;
  }
  s_prefs.begin("ozcity", false);
  s_unlocked = s_prefs.getUChar("unl", 0);
  if (s_unlocked >= LANDS) s_unlocked = LANDS - 1;
  for (int i = 0; i < LANDS; i++) {
    char k[4] = {'b', (char)('0' + i), 0};
    s_best[i] = s_prefs.getUInt(k, 0);
  }
  s_pick = s_unlocked;
  startLand(s_pick); // the title shows this land's tower behind it
  setMode(Mode::Title);
  s_exit = false;
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
  heap_caps_free(s_frame); s_frame = nullptr;
  heap_caps_free(s_clouds); s_clouds = nullptr;
  s_active = false;
}

bool active() { return s_active; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }
const uint16_t *frameBuffer() { return s_frame; }
void benchUnlockAll() { s_unlocked = LANDS - 1; }
void benchAutoplay() { s_auto = !s_auto; }

void pause() {
  if (s_mode == Mode::Play || s_mode == Mode::Ready) setMode(Mode::Paused);
}

static void toTitle() {
  s_pick = s_ch;
  startLand(s_pick);
  setMode(Mode::Title);
}

void key(const InputEvent &ev) {
  if (!ev.pressed || ev.repeat) return;
  switch (s_mode) {
    case Mode::Title:
      if (ev.key == Key::Left && s_pick > 0) { s_pick--; startLand(s_pick); }
      else if (ev.key == Key::Right && s_pick < LANDS - 1) { s_pick++; startLand(s_pick); }
      else if (ev.key == Key::Select && s_pick <= s_unlocked) { startLand(s_pick); setMode(Mode::Ready); }
      else if (ev.key == Key::Up) s_exit = true;
      break;
    case Mode::Ready:
      if (ev.key == Key::Select) setMode(Mode::Play);
      else if (ev.key == Key::Down) pause();
      break;
    case Mode::Play:
      if (ev.key == Key::Select) drop();
      else if (ev.key == Key::Down) pause();
      break;
    case Mode::Paused:
      if (ev.key == Key::Select) setMode(Mode::Play);
      else if (ev.key == Key::Down) toTitle();
      break;
    case Mode::Cleared:
      if (ev.key == Key::Select) { startLand(s_ch + 1); setMode(Mode::Ready); }
      else if (ev.key == Key::Up) toTitle();
      break;
    case Mode::Fail:
      if (ev.key == Key::Select) { startLand(s_ch); setMode(Mode::Ready); }
      else if (ev.key == Key::Up) toTitle();
      break;
    case Mode::Finale:
      if (ev.key == Key::Select || ev.key == Key::Up) toTitle();
      break;
    case Mode::Built:
      break;
  }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  static uint32_t last = 0;
  static float acc = 0;
  uint32_t now = millis();
  float dt = last ? (now - last) / 1000.0f : STEP;
  last = now;
  if (dt > 0.1f) dt = 0.1f;

  if (s_mode == Mode::Play || s_mode == Mode::Ready) s_trim = clampf(s_trim + wheel * 3.0f, -36, 36);
  if (touch.justPressed()) {
    InputEvent tap = {Key::Select, true, false};
    key(tap);
  }

  acc += dt;
  while (acc >= STEP) { update(STEP); acc -= STEP; }
  render();
}

} // namespace EmeraldCity
