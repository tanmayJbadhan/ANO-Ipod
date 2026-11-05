#include "Ariadne.h"
#include "Raster.h"
#include "RunAudio.h"
#include "HalfRes.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>
#include <font/glcdfont.h>

using namespace Raster;

namespace Ariadne {

// ---------------------------------------------------------------------------
// Screen, palette, levels
// ---------------------------------------------------------------------------

static const int W = HalfRes::W, H = HalfRes::H, FW = HalfRes::FW, FH = HalfRes::FH;
static const float K = 0.5f;             // screen units -> frame pixels
static const float HUD_H = 24;

// Black-figure pottery and Knossos frescoes.
static const uint32_t C_INK = 0x15100e, C_TERRA = 0xdc7a3e, C_TERRA_D = 0xa9481f, C_CREAM = 0xf4e3c2;
static const uint32_t C_MRED = 0xc22a1f, C_ADDED = 0x8c1d1a, C_THREAD = 0xff2b3c, C_GOLD = 0xe9b94a;
static const uint32_t C_BRONZE = 0xc2893a, C_FBLUE = 0x3d7cba, C_STONE = 0x3b2723;

enum Kind : uint8_t { K_LOOM, K_MAZE, K_BOSS, K_YARN, K_END };
enum Vig : uint8_t { V_LOOM, V_GATE, V_FRESCO, V_DOORS, V_DARK, V_BULL, V_YARN, V_WATER, V_GUARD, V_LIGHT, V_STARS };

struct Level {
  const char *act, *name, *story;
  Kind kind;
  uint8_t n; // which maze / yarn level
  Vig vig;
};

static const int LEVELS = 11;
static const Level LV[LEVELS] = {
    {"PROLOGUE", "Ariadne's Loom", "Every nine years Athens sends seven youths and seven maidens to the Minotaur. Ariadne, King Minos's daughter, spins a thread to bring them home. Press SELECT as the shuttle crosses the gold.", K_LOOM, 0, V_LOOM},
    {"ACT I", "The Bull Gate", "Theseus ties the thread at the gate and steps into the dark. It unrolls behind him; walk back along it to wind it in. Find the stairs down.", K_MAZE, 0, V_GATE},
    {"ACT I", "The Hall of Frescoes", "Dolphins and lilies swim on the walls. \"Follow the double axes,\" Ariadne whispered. They mark the way.", K_MAZE, 1, V_FRESCO},
    {"ACT I", "Daedalus's Doors", "Daedalus built this place to confuse. Bronze doors swing open and shut on their own. The thread still remembers the way.", K_MAZE, 2, V_DOORS},
    {"ACT I", "The Breathing Dark", "The torch burns low. Something breathes in the dark and follows your scent. Reach the heart of the labyrinth.", K_MAZE, 3, V_DARK},
    {"ACT II", "The Heart", "The Minotaur paws the ground. Leap over his charge, let him crash into the columns, then strike while he is dazed.", K_BOSS, 0, V_BULL},
    {"ACT III", "The Thread Home", "The labyrinth shakes. Now you are the ball of thread: roll a path and Theseus follows. It can never cross itself. SELECT winds it back.", K_YARN, 0, V_YARN},
    {"ACT III", "The Flooded Cistern", "Water has broken in. Wet yarn is heavy: each wet step costs two lengths of thread.", K_YARN, 1, V_WATER},
    {"ACT III", "Minos's Guards", "Guards sweep the halls with lanterns. If the light finds the thread, it is pulled back.", K_YARN, 2, V_GUARD},
    {"ACT III", "The Last Gate", "The walls are falling. Bring everyone out before the roof comes down.", K_YARN, 3, V_LIGHT},
    {"FINALE", "The Crown of Stars", "They are free. Ariadne sails where she chooses, and the last of her thread rises into the night: a crown of stars, Corona Borealis.", K_END, 0, V_STARS},
};

enum class Mode : uint8_t { Title, Story, Play, Paused, Won, Lost };

static bool s_active = false, s_exit = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static uint16_t *s_frame = nullptr;
static Preferences s_prefs;
static Input *s_input = nullptr;
static uint8_t s_botHeld = 0; // bench self-play: the keys the bot holds down
static bool s_bench = false;  // bench self-play running: nothing is saved
static bool held(Key k) { return (s_botHeld & (1 << (int)k)) || (s_input && s_input->held(k)); }

static Mode s_mode = Mode::Title;
static int s_lv = 0, s_pick = 0, s_unlocked = 0;
static float s_t = 0, s_modeT = 0, s_clock = 0, s_shake = 0, s_downT = -1;
static float s_spin = 1.0f;           // thread from the loom, a multiplier
static uint32_t s_best[LEVELS];
static const char *s_lostWhy = "";

// --- random numbers and noise ----------------------------------------------------

static uint32_t s_rng = 1;
static uint32_t rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static float frand() { return (rnd() >> 8) / 16777216.0f; }
static int irand(int n) { return (int)(rnd() % (uint32_t)n); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

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
static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); } // 6t^5 - 15t^4 + 10t^3
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
// Fractal Brownian motion: sum_i 0.5^i * noise(2^i x).
static float fbm(float x, float y, int oct) {
  float s = 0, a = 0.5f, f = 1;
  for (int i = 0; i < oct; i++, a *= 0.5f, f *= 2) s += a * noise2(x * f, y * f);
  return s;
}
static uint32_t hash2(int a, int b) {
  uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// --- text ---------------------------------------------------------------------

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
static void txt(int x, int y, const char *s, int size, uint16_t col, uint16_t shadow = 0, bool shade = false) {
  int lx = x / 2, ly = y / 2;
  for (const char *p = s; *p; p++, lx += 6 * size) {
    if (shade) glyph(lx + 1, ly + 1, (unsigned char)*p, size, shadow);
    glyph(lx, ly, (unsigned char)*p, size, col);
  }
}
static void txtC(int cx, int y, const char *s, int size, uint16_t col) { txt(cx - textW(s, size) / 2, y, s, size, col); }
static void txtCS(int cx, int y, const char *s, int size, uint16_t col) { txt(cx - textW(s, size) / 2, y, s, size, col, rgb(0x000000), true); }

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
    txtC(cx, y, line, 1, col);
    s += take;
    y += 17;
  }
  return y;
}

// --- shared ornaments --------------------------------------------------------------

// A Greek key (meander) band, light pattern on a dark ground or the other way
// round. One repeat is a square spiral hook, 12 units wide.
static void meander(float x0, float y, float w, float h, uint32_t ground, uint32_t key) {
  rect(x0, y, w, h, rgb(ground));
  float u = h / 6; // stroke
  uint16_t c = rgb(key);
  rect(x0, y + u * 0.5f, w, u * 0.7f, c);
  rect(x0, y + h - u * 1.2f, w, u * 0.7f, c);
  for (float x = x0; x < x0 + w - 2 * h * 0.5f; x += h * 1.2f) {
    float s = h * 1.2f;
    rect(x, y + u * 1.2f, u, h - u * 2.4f, c);                 // up
    rect(x, y + u * 1.2f, s * 0.7f, u, c);                     // across the top
    rect(x + s * 0.7f - u, y + u * 1.2f, u, h * 0.5f, c);      // down
    rect(x + s * 0.35f, y + h * 0.5f + u * 0.2f, s * 0.35f, u, c); // back in
    rect(x + s * 0.35f, y + h * 0.5f - u * 1.3f, u, u * 1.6f, c);
  }
}

static void card(float x, float y, float w, float h) {
  roundRect(x, y, w, h, 10, rgb(C_TERRA));
  meander(x + 6, y + 4, w - 12, 12, C_INK, C_TERRA);
  meander(x + 6, y + h - 16, w - 12, 12, C_INK, C_TERRA);
}

// --- black-figure people ------------------------------------------------------------
// Silhouettes in glossy black, details "incised" back to the clay colour, a
// little added red - as on Attic vases.

static void bfTheseus(float x, float gy, float s, float run, int pose, float face, uint32_t ink, uint32_t inc) {
  // pose 0 stand/run, 1 leap, 2 strike
  float legA = pose == 1 ? 0.9f : sinf(run) * 0.6f, legB = pose == 1 ? -0.5f : -legA;
  float hip = gy - 26 * s;
  line(x, hip, x + sinf(legA) * 16 * s * face, hip + cosf(legA) * 24 * s, 4.5f * s, rgb(ink));
  line(x, hip, x + sinf(legB) * 16 * s * face, hip + cosf(legB) * 24 * s, 4.5f * s, rgb(ink));
  const float torso[8] = {x - 7 * s, hip - 26 * s, x + 7 * s, hip - 26 * s, x + 5 * s, hip + 2 * s, x - 5 * s, hip + 2 * s};
  poly(torso, 4, rgb(ink));
  rect(x - 7 * s, hip - 2 * s, 14 * s, 5 * s, rgb(ink));     // a short chiton's hem
  line(x - 5 * s, hip - 14 * s, x + 5 * s, hip - 14 * s, 0.9f, rgb(inc)); // belt, incised
  circle(x + 1 * s * face, hip - 33 * s, 6.5f * s, rgb(ink));  // head and helmet
  line(x - 6 * s * face, hip - 40 * s, x + 6 * s * face, hip - 42 * s, 2.6f * s, rgb(C_ADDED)); // crest
  line(x + 3 * s * face, hip - 33 * s, x + 6.5f * s * face, hip - 33 * s, 0.9f, rgb(inc)); // eye slit
  // Sword arm.
  float ax = x + 4 * s * face, ay = hip - 22 * s;
  float ang = pose == 2 ? 0.0f : (pose == 1 ? -0.9f : 0.6f);
  float hx = ax + cosf(ang) * 13 * s * face, hy = ay + sinf(ang) * 13 * s;
  line(ax, ay, hx, hy, 3.2f * s, rgb(ink));
  line(hx, hy, hx + cosf(ang) * 18 * s * face, hy + sinf(ang) * 18 * s, 1.6f * s, rgb(inc)); // the blade
  // Round shield on the other arm.
  float sx = x - 7 * s * face, sy = hip - 18 * s;
  circle(sx, sy, 10 * s, rgb(ink));
  circle(sx, sy, 7.5f * s, rgb(inc), 255);
  circle(sx, sy, 6.3f * s, rgb(ink));
  circle(sx, sy, 2 * s, rgb(C_ADDED));
}

static void bfMinotaur(float x, float gy, float s, float face, bool charge, float run, uint32_t ink, uint32_t inc) {
  float lean = charge ? 0.45f : 0.1f;
  float hip = gy - 34 * s;
  float la = sinf(run) * 0.5f;
  line(x - 4 * s * face, hip, x - 4 * s * face + sinf(la) * 14 * s, gy, 7 * s, rgb(ink));
  line(x + 5 * s * face, hip, x + 5 * s * face - sinf(la) * 14 * s, gy, 7 * s, rgb(ink));
  rect(x - 9 * s * face - 4 * s, gy - 3 * s, 9 * s, 3 * s, rgb(ink)); // hooves
  rect(x + 2 * s * face, gy - 3 * s, 9 * s, 3 * s, rgb(ink));
  float sx = x + sinf(lean) * 30 * s * face, sy = hip - cosf(lean) * 32 * s;
  const float body[8] = {x - 10 * s, hip, x + 10 * s, hip, sx + 16 * s, sy, sx - 16 * s, sy};
  poly(body, 4, rgb(ink));
  circle(sx, sy, 15 * s, rgb(ink));                               // great shoulders
  line(x - 6 * s, hip - 12 * s, sx - 8 * s, sy + 6 * s, 0.9f, rgb(inc)); // incised muscles
  line(x + 6 * s, hip - 12 * s, sx + 8 * s, sy + 6 * s, 0.9f, rgb(inc));
  float hx = sx + 16 * s * face, hy = sy + (charge ? 8 : -6) * s;
  ellipse(hx, hy, 11 * s, 9 * s, rgb(ink));                       // the bull's head
  ellipse(hx + 9 * s * face, hy + 4 * s, 6 * s, 5 * s, rgb(ink)); // muzzle
  circle(hx + 3 * s * face, hy - 2 * s, 2 * s, rgb(C_MRED));      // red eye
  // Horns, pale and curved.
  line(hx - 4 * s * face, hy - 7 * s, hx - 14 * s * face, hy - 14 * s, 2.4f * s, rgb(C_CREAM));
  line(hx - 14 * s * face, hy - 14 * s, hx - 12 * s * face, hy - 22 * s, 2 * s, rgb(C_CREAM));
  line(hx + 2 * s * face, hy - 8 * s, hx + 10 * s * face, hy - 17 * s, 2.4f * s, rgb(C_CREAM));
  line(hx + 10 * s * face, hy - 17 * s, hx + 6 * s * face, hy - 24 * s, 2 * s, rgb(C_CREAM));
  circle(hx + 12 * s * face, hy + 6 * s, 1.2f * s, rgb(inc));     // nostril
  // Arms.
  line(sx - 8 * s * face, sy + 6 * s, sx - 2 * s * face, sy + 26 * s, 6 * s, rgb(ink));
  line(sx + 10 * s * face, sy + 6 * s, sx + 16 * s * face, sy + 24 * s, 6 * s, rgb(ink));
}

static void bfAriadne(float x, float gy, float s, uint32_t ink, uint32_t inc) {
  const float robe[8] = {x - 5 * s, gy - 44 * s, x + 5 * s, gy - 44 * s, x + 12 * s, gy, x - 12 * s, gy};
  poly(robe, 4, rgb(ink));
  for (int i = 1; i < 4; i++) line(x - 10 * s + i * 5 * s, gy - 30 * s, x - 11 * s + i * 5.5f * s, gy - 2 * s, 0.9f, rgb(inc)); // folds
  circle(x, gy - 50 * s, 6.5f * s, rgb(ink));
  ellipse(x - 4 * s, gy - 44 * s, 3 * s, 8 * s, rgb(ink));       // hair falling down her back
  line(x - 4 * s, gy - 52 * s, x + 4 * s, gy - 55 * s, 1.5f * s, rgb(C_ADDED)); // fillet
  line(x + 4 * s, gy - 38 * s, x + 14 * s, gy - 30 * s, 3 * s, rgb(ink));
  circle(x + 16 * s, gy - 28 * s, 5 * s, rgb(C_THREAD));          // the ball of thread
  line(x + 16 * s, gy - 28 * s, x + 30 * s, gy - 10 * s, 1, rgb(C_THREAD));
}

static void yarnBall(float x, float y, float r, float roll) {
  circle(x + 1.5f, y + r * 0.8f, r * 0.9f, rgb(0x000000), 70);
  circle(x, y, r, rgb(C_THREAD));
  uint16_t d = rgb(0xa8121e);
  for (int i = 0; i < 3; i++) {
    float a = roll + i * 1.05f;
    float c = cosf(a), s = sinf(a);
    line(x - c * r * 0.9f, y - s * r * 0.9f, x + c * r * 0.9f, y + s * r * 0.9f, 1.1f, d);
  }
  ellipse(x - r * 0.3f, y - r * 0.35f, r * 0.35f, r * 0.22f, rgb(0xffc0c8), 170);
}

// A Minoan column: red shaft wider at the top, black cushion capital.
static void column(float x, float top, float bot, float w, uint32_t shaft) {
  trap(x - w * 0.5f, x + w * 0.5f, top + w * 0.5f, x - w * 0.36f, x + w * 0.36f, bot - 4, rgb(shaft));
  trap(x - w * 0.42f, x - w * 0.2f, top + w * 0.5f, x - w * 0.3f, x - w * 0.12f, bot - 4, rgb(0xffffff), 40);
  ellipse(x, top + w * 0.35f, w * 0.75f, w * 0.32f, rgb(C_INK));
  rect(x - w * 0.8f, top - 2, w * 1.6f, 5, rgb(C_INK));
  rect(x - w * 0.5f, bot - 5, w, 5, rgb(C_INK));
}

static void labrys(float x, float y, float s, uint32_t c) {
  line(x, y - 9 * s, x, y + 9 * s, 1.4f * s, rgb(0x6a4a2a));
  const float l[8] = {x - 1, y - 6 * s, x - 1, y + 2 * s, x - 8 * s, y + 5 * s, x - 8 * s, y - 9 * s};
  const float r[8] = {x + 1, y - 6 * s, x + 8 * s, y - 9 * s, x + 8 * s, y + 5 * s, x + 1, y + 2 * s};
  poly(l, 4, rgb(c));
  poly(r, 4, rgb(c));
}

static void flame(float x, float y, float s) {
  float f = 0.8f + 0.2f * sinf(s_t * 17 + x) + 0.1f * sinf(s_t * 31 + y);
  circle(x, y - 2 * s, 9 * s * f, rgb(0xffa040), 50);
  ellipse(x, y - 3 * s * f, 3 * s, 5 * s * f, rgb(0xff8a2a));
  ellipse(x, y - 2 * s * f, 1.6f * s, 3 * s * f, rgb(0xffe08a));
}

// ---------------------------------------------------------------------------
// Act I: the generated labyrinth
// ---------------------------------------------------------------------------
//
// makeMaze() builds each level from a seed in five steps:
//
// 1. Wilson's algorithm (1996): a uniform spanning tree of the cell grid by
//    loop-erased random walks. Each unvisited cell walks at random, remembering
//    only the last exit taken from each cell (which erases loops), until it
//    meets the tree; the walk is then carved in. Every perfect maze is
//    equally likely, so there is no bias to long corridors or many branches.
// 2. Noise-guided braiding: a dead end d is opened into a neighbour with
//    probability p(d) = p0 * (0.55 + 0.9 * max(0, fbm(x/3, y/3))), so loops
//    cluster in "rooms" of the fractal field instead of spreading evenly.
// 3. Search-based selection (Togelius et al. 2011): K candidates are made
//    and each scored with
//       F = 0.45 L/(2N) + 0.25 E/N + 0.20 H/2 + 0.10 (1 - |D - D*| / D*)
//    where L is the solution length in tiles, N the number of cells, E the
//    cells an A* search (Manhattan h) expands before finding the goal - the
//    agent-effort difficulty measure of Beukman, James & Cleghorn
//    (arXiv:2201.10334) - H = -sum_k p_k log2 p_k the entropy of the junction
//    degrees, and D the dead-end ratio against a target D*. The best is kept.
// 4. Decoration by dart throwing (Poisson-disk sampling, Cook 1986): fresco
//    and torch walls at least r tiles apart; on the frescoes level, double
//    axes on the walls of the BFS solution path.
// 5. Light: torches give a static light map; the player's torch adds
//    I(d) = (1 - (d/R)^2)^2 * (1 + 0.06 sin 13t + 0.04 sin 29t) where a
//    Bresenham ray from the player to the tile is unobstructed.

static const int MAXCW = 15, MAXCH = 13, MAXTW = MAXCW * 2 + 1, MAXTH = MAXCH * 2 + 1;
static const float TS = 20; // tile size, screen units
enum Tile : uint8_t { T_FLOOR, T_WALL, T_DOOR };
enum Deco : uint8_t { D_NONE, D_DOLPHIN, D_LILY, D_HORNS, D_ROSETTE, D_TORCH, D_LABRYS };

struct MazeSpec { uint8_t cw, ch; float braid, deadTarget, torch, light; bool chamber, doors, hints, beast; };
static const MazeSpec MS[4] = {
    {11, 9, 0.10f, 0.30f, 0.45f, 8.0f, false, false, false, false},
    {13, 9, 0.22f, 0.26f, 0.35f, 7.5f, false, false, true, false},
    {13, 11, 0.38f, 0.22f, 0.30f, 7.0f, false, true, false, false},
    {15, 13, 0.30f, 0.24f, 0.22f, 4.2f, true, false, false, true},
};

static uint8_t s_open[MAXCH][MAXCW]; // N 1, E 2, S 4, W 8
static uint8_t s_tile[MAXTH][MAXTW], s_deco[MAXTH][MAXTW], s_lightS[MAXTH][MAXTW], s_seen[MAXTH][MAXTW];
static uint8_t s_door[MAXTH][MAXTW]; // door group + 1
static int s_cw, s_chh, s_tw, s_th;
static int s_sx, s_sy, s_gx, s_gy;   // start and goal tiles
static const MazeSpec *M = &MS[0];

static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
static const uint8_t BIT[4] = {1, 2, 4, 8};

static void carve(int x, int y, int d) {
  s_open[y][x] |= BIT[d];
  s_open[y + DY[d]][x + DX[d]] |= BIT[(d + 2) & 3];
}
static int degree(int x, int y) {
  int n = 0;
  for (int d = 0; d < 4; d++) if (s_open[y][x] & BIT[d]) n++;
  return n;
}

// Wilson's loop-erased random walks. `tree` marks cells already in the tree.
static void wilson(int cw, int ch, bool *tree) {
  static int8_t next[MAXCH * MAXCW];
  static int16_t order[MAXCH * MAXCW];
  int n = cw * ch;
  for (int i = 0; i < n; i++) order[i] = i;
  for (int i = n - 1; i > 0; i--) { int j = irand(i + 1); int16_t t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int k = 0; k < n; k++) {
    int c = order[k];
    if (tree[c]) continue;
    int cur = c;
    while (!tree[cur]) {
      int x = cur % cw, y = cur / cw, d;
      do { d = irand(4); } while (x + DX[d] < 0 || x + DX[d] >= cw || y + DY[d] < 0 || y + DY[d] >= ch);
      next[cur] = d;
      cur = (y + DY[d]) * cw + x + DX[d];
    }
    cur = c;
    while (!tree[cur]) {
      int x = cur % cw, y = cur / cw, d = next[cur];
      carve(x, y, d);
      tree[cur] = true;
      cur = (y + DY[d]) * cw + x + DX[d];
    }
  }
}

// Cell-graph BFS distances from (sx, sy); returns the farthest cell.
static int bfsCells(int sx, int sy, int16_t *dist) {
  static int16_t q[MAXCH * MAXCW];
  int n = s_cw * s_chh, h = 0, t = 0, far = sy * s_cw + sx;
  for (int i = 0; i < n; i++) dist[i] = -1;
  dist[far] = 0;
  q[t++] = far;
  while (h < t) {
    int c = q[h++], x = c % s_cw, y = c / s_cw;
    if (dist[c] > dist[far]) far = c;
    for (int d = 0; d < 4; d++) {
      if (!(s_open[y][x] & BIT[d])) continue;
      int nc = (y + DY[d]) * s_cw + x + DX[d];
      if (dist[nc] >= 0) continue;
      dist[nc] = dist[c] + 1;
      q[t++] = nc;
    }
  }
  return far;
}

// A* with the Manhattan heuristic; returns how many cells it expanded.
static int astarEffort(int sx, int sy, int gx, int gy) {
  static int16_t g[MAXCH * MAXCW];
  static bool open[MAXCH * MAXCW], closed[MAXCH * MAXCW];
  int n = s_cw * s_chh;
  for (int i = 0; i < n; i++) { g[i] = 32000; open[i] = closed[i] = false; }
  int s = sy * s_cw + sx, goal = gy * s_cw + gx, expanded = 0;
  g[s] = 0; open[s] = true;
  for (;;) {
    int best = -1, bf = 1 << 30;
    for (int i = 0; i < n; i++) {
      if (!open[i]) continue;
      int f = g[i] + abs(i % s_cw - gx) + abs(i / s_cw - gy);
      if (f < bf) { bf = f; best = i; }
    }
    if (best < 0) return expanded;
    open[best] = false; closed[best] = true; expanded++;
    if (best == goal) return expanded;
    int x = best % s_cw, y = best / s_cw;
    for (int d = 0; d < 4; d++) {
      if (!(s_open[y][x] & BIT[d])) continue;
      int nc = (y + DY[d]) * s_cw + x + DX[d];
      if (closed[nc] || g[best] + 1 >= g[nc]) continue;
      g[nc] = g[best] + 1;
      open[nc] = true;
    }
  }
}

struct Candidate { uint8_t open[MAXCH][MAXCW]; uint8_t sx, sy, gx, gy; float f; };
static Candidate *s_cand = nullptr; // two of them, in PSRAM

static float makeCandidate(Candidate &c, int braidEdges[][3], int &nBraid) {
  memset(s_open, 0, sizeof(s_open));
  static bool tree[MAXCH * MAXCW];
  memset(tree, 0, sizeof(tree));
  int n = s_cw * s_chh;
  int cx = s_cw / 2, cy = s_chh / 2;
  if (M->chamber) {
    for (int y = cy - 1; y <= cy + 1; y++)
      for (int x = cx - 1; x <= cx + 1; x++) {
        tree[y * s_cw + x] = true;
        if (x < cx + 1) carve(x, y, 1);
        if (y < cy + 1) carve(x, y, 2);
      }
  } else {
    tree[irand(n)] = true;
  }
  wilson(s_cw, s_chh, tree);

  // Braid by the noise field.
  nBraid = 0;
  for (int y = 0; y < s_chh; y++)
    for (int x = 0; x < s_cw; x++) {
      if (degree(x, y) != 1) continue;
      float p = M->braid * (0.55f + 0.9f * fmaxf(0, fbm(x / 3.0f, y / 3.0f, 3) * 2));
      if (frand() >= p) continue;
      int opts[4], no = 0;
      for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (nx < 0 || ny < 0 || nx >= s_cw || ny >= s_chh || (s_open[y][x] & BIT[d])) continue;
        opts[no++] = d;
        if (degree(nx, ny) == 1) { opts[0] = d; no = 1; break; } // join two dead ends if we can
      }
      if (!no) continue;
      int d = opts[irand(no)];
      carve(x, y, d);
      if (nBraid < 64) { braidEdges[nBraid][0] = x; braidEdges[nBraid][1] = y; braidEdges[nBraid][2] = d; nBraid++; }
    }

  // Start and goal.
  static int16_t dist[MAXCH * MAXCW];
  int sx, sy, gx, gy;
  if (M->chamber) {
    sx = 0; sy = s_chh - 1; gx = cx; gy = cy;
  } else {
    int corner = irand(4);
    sx = corner & 1 ? s_cw - 1 : 0;
    sy = corner & 2 ? s_chh - 1 : 0;
    int far = bfsCells(sx, sy, dist);
    gx = far % s_cw; gy = far / s_cw;
  }
  bfsCells(sx, sy, dist);
  float L = dist[gy * s_cw + gx];
  float E = astarEffort(sx, sy, gx, gy);
  int cnt[5] = {0, 0, 0, 0, 0};
  for (int y = 0; y < s_chh; y++) for (int x = 0; x < s_cw; x++) cnt[degree(x, y)]++;
  float Hd = 0;
  for (int k = 1; k <= 4; k++) if (cnt[k]) { float p = cnt[k] / (float)n; Hd -= p * log2f(p); }
  float D = cnt[1] / (float)n;
  float f = 0.45f * L / (2.0f * n) * 2 + 0.25f * E / n + 0.20f * Hd / 2 +
            0.10f * (1 - clampf(fabsf(D - M->deadTarget) / M->deadTarget, 0, 1));
  memcpy(c.open, s_open, sizeof(s_open));
  c.sx = sx; c.sy = sy; c.gx = gx; c.gy = gy; c.f = f;
  return f;
}

// Bresenham line of sight between tiles.
static bool losClear(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  while (true) {
    if (x0 == x1 && y0 == y1) return true;
    if (!(x0 == x1 && y0 == y1)) {
      uint8_t t = s_tile[y0][x0];
      if (t == T_WALL || (t == T_DOOR && !(s_door[y0][x0] & 0x80))) return false;
    }
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// Door groups swing every few seconds; a door tile is passable while open.
static float s_doorClock = 0;
static bool doorOpen(int x, int y) { return s_door[y][x] & 0x80; }
static bool passable(int x, int y) {
  if (x < 0 || y < 0 || x >= s_tw || y >= s_th) return false;
  uint8_t t = s_tile[y][x];
  return t == T_FLOOR || (t == T_DOOR && doorOpen(x, y));
}

static void makeMaze(int which) {
  M = &MS[which];
  s_cw = M->cw; s_chh = M->ch;
  s_tw = s_cw * 2 + 1; s_th = s_chh * 2 + 1;
  seedNoise(esp_random());
  static int braid[64][3], bestBraid[64][3];
  int nb = 0, bestNb = 0;
  int best = 0;
  // Five candidates; keep the fittest (two buffers, swapped).
  s_cand[0].f = -1;
  for (int k = 0; k < 5; k++) {
    float f = makeCandidate(s_cand[1], braid, nb);
    if (f > s_cand[0].f) {
      s_cand[0] = s_cand[1];
      memcpy(bestBraid, braid, sizeof(braid));
      bestNb = nb;
    }
  }
  (void)best;
  memcpy(s_open, s_cand[0].open, sizeof(s_open));

  // Tiles.
  memset(s_tile, T_WALL, sizeof(s_tile));
  memset(s_deco, 0, sizeof(s_deco));
  memset(s_door, 0, sizeof(s_door));
  memset(s_seen, 0, sizeof(s_seen));
  for (int y = 0; y < s_chh; y++)
    for (int x = 0; x < s_cw; x++) {
      s_tile[2 * y + 1][2 * x + 1] = T_FLOOR;
      if (s_open[y][x] & 2) s_tile[2 * y + 1][2 * x + 2] = T_FLOOR;
      if (s_open[y][x] & 4) s_tile[2 * y + 2][2 * x + 1] = T_FLOOR;
    }
  if (M->chamber) // the heart: one open room, its posts gone
    for (int y = s_chh / 2 * 2 - 1; y <= s_chh / 2 * 2 + 3; y++)
      for (int x = s_cw / 2 * 2 - 1; x <= s_cw / 2 * 2 + 3; x++) s_tile[y][x] = T_FLOOR;
  if (M->doors)
    for (int i = 0; i < bestNb; i++) {
      int x = bestBraid[i][0], y = bestBraid[i][1], d = bestBraid[i][2];
      int tx = 2 * x + 1 + DX[d], ty = 2 * y + 1 + DY[d];
      s_tile[ty][tx] = T_DOOR;
      s_door[ty][tx] = 1 + (i & 1);
    }
  s_sx = 2 * s_cand[0].sx + 1; s_sy = 2 * s_cand[0].sy + 1;
  s_gx = 2 * s_cand[0].gx + 1; s_gy = 2 * s_cand[0].gy + 1;

  // Frescoes and torches, by dart throwing on the walls that face south.
  static int16_t px[200], py[200];
  int np = 0;
  for (int tries = 0; tries < 900 && np < 200; tries++) {
    int x = 1 + irand(s_tw - 2), y = irand(s_th - 1);
    if (s_tile[y][x] != T_WALL || s_tile[y + 1][x] != T_FLOOR) continue;
    bool ok = true;
    for (int i = 0; i < np && ok; i++) ok = (px[i] - x) * (px[i] - x) + (py[i] - y) * (py[i] - y) >= 9;
    if (!ok) continue;
    px[np] = x; py[np] = y; np++;
    s_deco[y][x] = frand() < M->torch ? D_TORCH : (uint8_t)(D_DOLPHIN + irand(4));
  }
  // The double axes along the true path (BFS parents over tiles).
  if (M->hints) {
    static int16_t par[MAXTH * MAXTW];
    static int16_t q[MAXTH * MAXTW];
    for (int i = 0; i < s_tw * s_th; i++) par[i] = -2;
    int h = 0, t = 0;
    q[t++] = s_sy * s_tw + s_sx; par[q[0]] = -1;
    while (h < t) {
      int c = q[h++], x = c % s_tw, y = c / s_tw;
      for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (nx < 0 || ny < 0 || nx >= s_tw || ny >= s_th || s_tile[ny][nx] == T_WALL) continue;
        int nc = ny * s_tw + nx;
        if (par[nc] != -2) continue;
        par[nc] = c; q[t++] = nc;
      }
    }
    int c = s_gy * s_tw + s_gx, k = 0;
    while (c >= 0) {
      int x = c % s_tw, y = c / s_tw;
      if (k++ % 4 == 2 && y > 0 && s_tile[y - 1][x] == T_WALL) s_deco[y - 1][x] = D_LABRYS;
      c = par[c];
    }
  }
  // Static torch light, with line of sight.
  memset(s_lightS, 0, sizeof(s_lightS));
  for (int ty = 0; ty < s_th; ty++)
    for (int tx = 0; tx < s_tw; tx++) {
      if (s_deco[ty][tx] != D_TORCH) continue;
      for (int y = ty - 4; y <= ty + 5; y++)
        for (int x = tx - 4; x <= tx + 4; x++) {
          if (x < 0 || y < 0 || x >= s_tw || y >= s_th) continue;
          float d = sqrtf((x - tx) * (x - tx) + (y - ty - 1) * (y - ty - 1)) / 4.2f;
          if (d >= 1 || !losClear(tx, ty + 1, x, y)) continue;
          float I = (1 - d * d) * (1 - d * d) * 0.85f;
          s_lightS[y][x] = (uint8_t)fmaxf(s_lightS[y][x], I * 255);
        }
    }
}

// --- the player in the maze ----------------------------------------------------------

static int s_px, s_py, s_tx, s_ty, s_face = 2;
static float s_move = 1;              // 0..1 between (px,py) and (tx,ty)
static int16_t s_path[MAXTH * MAXTW]; // the thread, tile indices
static int s_plen = 0, s_budget = 0;
static float s_walk = 0;
static char s_note[28];
static float s_noteT = 0;

static void note(const char *t) { strlcpy(s_note, t, sizeof(s_note)); s_noteT = 2.2f; }

// The Minotaur in the dark, and the scent it follows:
// u' = u + D (u_N + u_S + u_E + u_W - 4u) - lambda u, with u = 1 at Theseus.
static float s_scent[MAXTH][MAXTW];
static int s_mx, s_my, s_mtx, s_mty;
static float s_mmove = 1, s_scentT = 0, s_beatT = 0;

static void startMaze() {
  makeMaze(LV[s_lv].n);
  s_px = s_tx = s_sx; s_py = s_ty = s_sy; s_move = 1;
  s_plen = 0;
  s_path[s_plen++] = s_py * s_tw + s_px;
  // The thread is the solution's length, a margin, and what the loom spun.
  static int16_t dist[MAXCH * MAXCW];
  bfsCells((s_sx - 1) / 2, (s_sy - 1) / 2, dist);
  int sol = dist[((s_gy - 1) / 2) * s_cw + (s_gx - 1) / 2] * 2 + 1;
  s_budget = (int)(sol * (1.25f - 0.03f * M->cw / 5) * s_spin) + 10;
  s_doorClock = 0;
  memset(s_scent, 0, sizeof(s_scent));
  if (M->beast) {
    // It starts on the far side from Theseus.
    s_mx = s_mtx = s_tw - 2; s_my = s_mty = 1; s_mmove = 1;
  }
}

static bool tryMove(int d) {
  int nx = s_px + DX[d], ny = s_py + DY[d];
  s_face = d;
  if (!passable(nx, ny)) return false;
  int idx = ny * s_tw + nx;
  bool back = s_plen >= 2 && s_path[s_plen - 2] == idx;
  if (!back && s_plen >= s_budget) {
    note("No thread left - wind back");
    RunAudio::fx(RunAudio::Bonk);
    return false;
  }
  s_tx = nx; s_ty = ny; s_move = 0;
  return true;
}

static void arrive() {
  s_px = s_tx; s_py = s_ty;
  int idx = s_py * s_tw + s_px;
  if (s_plen >= 2 && s_path[s_plen - 2] == idx) s_plen--; // wound back in
  else s_path[s_plen++] = idx;
}

static void updateMaze(float dt) {
  // Doors: groups 1 and 2 take turns, three seconds each.
  if (M->doors) {
    s_doorClock += dt;
    int g = ((int)(s_doorClock / 3.0f) & 1) + 1;
    for (int y = 0; y < s_th; y++)
      for (int x = 0; x < s_tw; x++)
        if (s_door[y][x]) {
          bool open = (s_door[y][x] & 3) == g;
          if (open) s_door[y][x] |= 0x80; else s_door[y][x] &= 0x7f;
        }
  }
  // Movement: hold an arrow to keep walking.
  if (s_move < 1) {
    s_move += dt * 6.5f;
    s_walk += dt * 14;
    if (s_move >= 1) { s_move = 1; arrive(); }
  }
  if (s_move >= 1) {
    if (held(Key::Up)) tryMove(0);
    else if (held(Key::Right)) tryMove(1);
    else if (held(Key::Down)) tryMove(2);
    else if (held(Key::Left)) tryMove(3);
  }
  if (s_move >= 1 && s_px == s_gx && s_py == s_gy) {
    RunAudio::fx(RunAudio::DingTop);
    s_mode = Mode::Won; s_modeT = 0;
    return;
  }
  // The Minotaur.
  if (M->beast) {
    s_scentT += dt;
    while (s_scentT >= 0.1f) {
      s_scentT -= 0.1f;
      static float nu[MAXTH][MAXTW];
      for (int y = 1; y < s_th - 1; y++)
        for (int x = 1; x < s_tw - 1; x++) {
          if (!passable(x, y)) { nu[y][x] = 0; continue; }
          float sum = 0; int n = 0;
          for (int d = 0; d < 4; d++) if (passable(x + DX[d], y + DY[d])) { sum += s_scent[y + DY[d]][x + DX[d]]; n++; }
          nu[y][x] = s_scent[y][x] + 0.22f * (sum - n * s_scent[y][x]) - 0.004f * s_scent[y][x];
        }
      memcpy(s_scent, nu, sizeof(s_scent));
      s_scent[s_py][s_px] = 1;
    }
    if (s_mmove < 1) {
      s_mmove += dt * 3.4f;
      if (s_mmove >= 1) { s_mmove = 1; s_mx = s_mtx; s_my = s_mty; }
    } else {
      int best = -1; float bv = 1e-6f;
      for (int d = 0; d < 4; d++) {
        int nx = s_mx + DX[d], ny = s_my + DY[d];
        if (!passable(nx, ny)) continue;
        float v = s_scent[ny][nx] + frand() * 1e-6f;
        if (v > bv) { bv = v; best = d; }
      }
      if (best < 0) { // no scent yet: wander
        int d = irand(4);
        if (passable(s_mx + DX[d], s_my + DY[d])) best = d;
      }
      if (best >= 0) { s_mtx = s_mx + DX[best]; s_mty = s_my + DY[best]; s_mmove = 0; }
    }
    float mx = lerpf(s_mx, s_mtx, s_mmove), my = lerpf(s_my, s_mty, s_mmove);
    float px = lerpf(s_px, s_tx, s_move), py = lerpf(s_py, s_ty, s_move);
    float dist = sqrtf((mx - px) * (mx - px) + (my - py) * (my - py));
    s_shake = fmaxf(s_shake, clampf(1 - dist / 9, 0, 1) * 2.5f);
    s_beatT -= dt;
    if (dist < 9 && s_beatT <= 0) { RunAudio::fx(RunAudio::Bonk); s_beatT = 0.25f + dist * 0.08f; }
    if (dist < 0.7f) {
      s_lostWhy = "The Minotaur found you.";
      RunAudio::fx(RunAudio::Thunder);
      s_mode = Mode::Lost; s_modeT = 0;
    }
  }
}

// --- drawing the maze ----------------------------------------------------------------

static float s_camX = 0, s_camY = 0;

static void drawDeco(uint8_t d, float x, float y, float L) {
  // (x, y) is the top-left of the wall tile; the face is its lower part.
  float fx = x + TS / 2, fy = y + TS * 0.62f;
  switch (d) {
    case D_DOLPHIN: {
      ellipseRot(fx, fy, 7, 2.8f, -0.25f, mix(0x000000, C_FBLUE, L));
      const float tail[6] = {fx - 6, fy + 1, fx - 10, fy - 3, fx - 10, fy + 4};
      poly(tail, 3, mix(0x000000, C_FBLUE, L));
      line(fx - 4, fy + 1, fx + 5, fy, 0.8f, mix(0x000000, 0xf0e8d0, L));
      break;
    }
    case D_LILY:
      line(fx, fy + 6, fx, fy - 2, 1, mix(0x000000, 0x6a8a4a, L));
      ellipse(fx - 3, fy - 3, 2, 3.5f, mix(0x000000, 0xf4ecd8, L));
      ellipse(fx + 3, fy - 3, 2, 3.5f, mix(0x000000, 0xf4ecd8, L));
      ellipse(fx, fy - 5, 1.6f, 4, mix(0x000000, 0xfffaf0, L));
      break;
    case D_HORNS: // the horns of consecration
      rect(fx - 7, fy + 1, 14, 3, mix(0x000000, C_CREAM, L));
      line(fx - 6, fy + 1, fx - 7, fy - 6, 2, mix(0x000000, C_CREAM, L));
      line(fx + 6, fy + 1, fx + 7, fy - 6, 2, mix(0x000000, C_CREAM, L));
      break;
    case D_ROSETTE:
      circle(fx, fy, 4.5f, mix(0x000000, C_CREAM, L));
      circle(fx, fy, 2, mix(0x000000, C_MRED, L));
      break;
    case D_TORCH:
      rect(fx - 1, fy - 2, 2, 7, rgb(0x5a3a2a));
      flame(fx, fy - 3, 1.0f);
      break;
    case D_LABRYS:
      circle(fx, fy, 7, rgb(C_GOLD), (uint8_t)(50 + 30 * sinf(s_t * 3)));
      labrys(fx, fy, 0.7f, C_GOLD);
      break;
  }
}

static void drawMazeView() {
  float px = lerpf(s_px, s_tx, s_move), py = lerpf(s_py, s_ty, s_move);
  float viewH = H - HUD_H;
  float tx = (px + 0.5f) * TS - W / 2, ty = (py + 0.5f) * TS - viewH / 2;
  float mw = s_tw * TS, mh = s_th * TS;
  tx = mw <= W ? (mw - W) / 2 : clampf(tx, 0, mw - W);
  ty = mh <= viewH ? (mh - viewH) / 2 : clampf(ty, 0, mh - viewH);
  s_camX += (tx - s_camX) * 0.2f;
  s_camY += (ty - s_camY) * 0.2f;
  float shx = (frand() - 0.5f) * s_shake, shy = (frand() - 0.5f) * s_shake;
  float ox = -s_camX + shx, oy = HUD_H - s_camY + shy;

  float R = M->light;
  float flick = 1 + 0.06f * sinf(s_t * 13) + 0.04f * sinf(s_t * 29 + 1);
  int x0 = (int)floorf(s_camX / TS) - 1, y0 = (int)floorf(s_camY / TS) - 1;
  int x1 = x0 + (int)(W / TS) + 3, y1 = y0 + (int)(viewH / TS) + 3;
  int ipx = (int)roundf(px), ipy = (int)roundf(py);
  rect(0, HUD_H, W, viewH, rgb(0x000000));
  // The unexplored dark is glazed black with a faint key pattern, scrolling
  // with the view like a painted vase turning.
  for (float by = HUD_H + fmodf(-s_camY * 0.5f, 48) - 48; by < H; by += 48)
    meander(0, by + 18, W, 12, 0x000000, 0x2a0d09);
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y >= s_th) continue;
    for (int x = x0; x <= x1; x++) {
      if (x < 0 || x >= s_tw) continue;
      float d = sqrtf((x - px) * (x - px) + (y - py) * (y - py)) / R;
      float I = 0;
      if (d < 1 && losClear(ipx, ipy, x, y)) { I = fminf(1, 1.25f * (1 - d * d) * (1 - d * d) * flick); }
      I = fmaxf(I, s_lightS[y][x] / 255.0f);
      if (I > 0.25f) s_seen[y][x] = 1;
      if (s_seen[y][x]) I = fmaxf(I, M->beast ? 0.1f : 0.32f);
      if (I <= 0.01f) continue;
      I = clampf(I, 0, 1);
      float sx = ox + x * TS, sy = oy + y * TS;
      uint8_t t = s_tile[y][x];
      bool doorShut = t == T_DOOR && !doorOpen(x, y);
      if (t == T_FLOOR || (t == T_DOOR && !doorShut)) {
        uint32_t h = hash2(x, y);
        uint32_t base = mixHex(0xb8582f, 0xa24826, ((h >> 4) & 255) / 255.0f);
        rect(sx, sy, TS + 0.5f, TS + 0.5f, mix(0x000000, 0x5a2414, I));          // grout
        rect(sx + 1, sy + 1, TS - 1.5f, TS - 1.5f, mix(0x000000, base, I));
        if ((h & 7) == 0) circle(sx + 6 + (h >> 8) % 8, sy + 6 + (h >> 12) % 8, 1.4f, mix(0x000000, 0xe9c9a0, I * 0.8f)); // inlay
        if ((h & 15) == 3) line(sx + 3, sy + (h >> 16) % 14, sx + 12, sy + 4 + (h >> 20) % 12, 0.7f, mix(0x000000, 0x3a1a10, I));
        if (t == T_DOOR) { // an open bronze door, folded against the jamb
          rect(sx, sy, 2.5f, TS, mix(0x000000, C_BRONZE, I));
          rect(sx + TS - 2.5f, sy, 2.5f, TS, mix(0x000000, C_BRONZE, I));
        }
        if (x == s_gx && y == s_gy) {
          if (M->chamber) {
            circle(sx + TS / 2, sy + TS / 2, TS * 0.9f, rgb(C_MRED), (uint8_t)(80 + 60 * sinf(s_t * 3)));
            labrys(sx + TS / 2, sy + TS / 2, 1.0f, C_GOLD);
          } else { // the stairs down, glowing
            for (int k = 0; k < 4; k++) rect(sx + 2 + k * 1.5f, sy + 3 + k * 4, TS - 4 - k * 3, 3, mix(0x000000, mixHex(C_GOLD, 0x3a2010, k / 4.0f), fmaxf(I, 0.6f)));
            circle(sx + TS / 2, sy + TS / 2, TS * 0.8f, rgb(C_GOLD), (uint8_t)(40 + 30 * sinf(s_t * 4)));
          }
        }
      } else {
        bool face = y + 1 < s_th && (s_tile[y + 1][x] == T_FLOOR || s_tile[y + 1][x] == T_DOOR);
        if (doorShut) {
          rect(sx, sy, TS + 0.5f, TS + 0.5f, mix(0x000000, 0x8a5a24, I));
          rect(sx + 2, sy + 2, TS - 4, TS - 4, mix(0x000000, C_BRONZE, I));
          circle(sx + TS / 2, sy + TS / 2, 4, mix(0x000000, 0x6a4418, I));
          for (int k = 0; k < 6; k++) { float a = k * 1.047f + s_doorClock; circle(sx + TS / 2 + cosf(a) * 5, sy + TS / 2 + sinf(a) * 5, 1.4f, mix(0x000000, 0x6a4418, I)); }
          continue;
        }
        // Black walls: a cap seen from above, and a face with a red key band.
        rect(sx, sy, TS + 0.5f, TS + 0.5f, mix(0x000000, 0x463631, I));
        rect(sx + 3, sy + 3, TS - 6, TS - 6, mix(0x000000, 0x2e2320, I));     // a sunken panel on the cap
        // Red rims wherever the wall meets a corridor.
        uint16_t rim = mix(0x000000, C_MRED, I);
        if (y > 0 && s_tile[y - 1][x] != T_WALL) rect(sx, sy, TS + 0.5f, 2.5f, rim);
        if (x > 0 && s_tile[y][x - 1] != T_WALL) rect(sx, sy, 2.5f, TS + 0.5f, rim);
        if (x + 1 < s_tw && s_tile[y][x + 1] != T_WALL) rect(sx + TS - 2, sy, 2.5f, TS + 0.5f, rim);
        if (face) {
          float fy = sy + TS * 0.32f;
          rect(sx, fy, TS + 0.5f, TS * 0.68f + 0.5f, mix(0x000000, C_INK, I));
          rect(sx, fy + 2, TS + 0.5f, 2.5f, mix(0x000000, C_MRED, I));
          rect(sx, sy + TS - 3, TS + 0.5f, 2, mix(0x000000, C_MRED, I));
          // Key teeth between the stripes.
          for (float k = 0; k < TS; k += 5) rect(sx + k + ((x & 1) ? 2.5f : 0), fy + 5, 2.5f, 2.5f, mix(0x000000, C_MRED, I * 0.9f));
          if (s_deco[y][x]) drawDeco(s_deco[y][x], sx, sy, I);
        }
      }
    }
  }

  // The thread: a smooth Catmull-Rom curve through the tile centres.
  //   P(t) = 0.5 [ 2P1 + (P2 - P0) t + (2P0 - 5P1 + 4P2 - P3) t^2 + (3P1 - P0 - 3P2 + P3) t^3 ]
  auto C = [&](int i, float &x, float &y) {
    if (i < 0) i = 0;
    int idx = i >= s_plen ? -1 : s_path[i];
    float cx, cy;
    if (idx < 0) { cx = lerpf(s_px, s_tx, s_move); cy = lerpf(s_py, s_ty, s_move); }
    else { cx = idx % s_tw; cy = idx / s_tw; }
    x = ox + (cx + 0.5f) * TS + sinf(i * 1.7f) * 1.2f;
    y = oy + (cy + 0.62f) * TS + cosf(i * 2.3f) * 1.2f;
  };
  int nPts = s_plen + (s_move < 1 ? 1 : 0);
  float lx = 0, ly = 0;
  for (int i = 0; i + 1 < nPts; i++) {
    float x0p, y0p, x1p, y1p, x2p, y2p, x3p, y3p;
    C(i - 1, x0p, y0p); C(i, x1p, y1p); C(i + 1, x2p, y2p); C(i + 2 < nPts ? i + 2 : i + 1, x3p, y3p);
    if (x1p < -TS || x1p > W + TS || y1p < 0 || y1p > H + TS) continue;
    for (int k = 0; k <= 4; k++) {
      float t = k / 4.0f, t2 = t * t, t3 = t2 * t;
      float x = 0.5f * (2 * x1p + (x2p - x0p) * t + (2 * x0p - 5 * x1p + 4 * x2p - x3p) * t2 + (3 * x1p - x0p - 3 * x2p + x3p) * t3);
      float y = 0.5f * (2 * y1p + (y2p - y0p) * t + (2 * y0p - 5 * y1p + 4 * y2p - y3p) * t2 + (3 * y1p - y0p - 3 * y2p + y3p) * t3);
      if (k > 0) {
        line(lx, ly, x, y, 2.6f, rgb(C_THREAD));
        line(lx, ly - 0.8f, x, y - 0.8f, 0.8f, rgb(0xff9aa4), 160);
      }
      lx = x; ly = y;
    }
  }
  float tx0, ty0;
  C(0, tx0, ty0);
  circle(tx0, ty0, 3, rgb(C_GOLD)); // tied at the gate

  // Theseus, seen from above and behind.
  float cx = ox + (px + 0.5f) * TS, cy = oy + (py + 0.5f) * TS;
  ellipse(cx, cy + 7, 7, 2.5f, rgb(0x000000), 120);
  float step = s_move < 1 ? sinf(s_walk) * 2 : 0;
  rect(cx - 4, cy + 1 + step, 3, 6, rgb(0x8a5a3a));
  rect(cx + 1, cy + 1 - step, 3, 6, rgb(0x8a5a3a));
  ellipse(cx, cy, 6, 6.5f, rgb(0xf2ead6));            // white chiton
  rect(cx - 6, cy - 1, 12, 1.5f, rgb(C_MRED));
  circle(cx, cy - 7, 4.6f, rgb(C_BRONZE));             // bronze helmet
  rect(cx - 0.8f, cy - 13, 1.6f, 7, rgb(C_MRED));      // its crest
  float sdx = DX[s_face] * 7, sdy = DY[s_face] * 7;
  line(cx + 4, cy - 1, cx + 4 + sdx, cy - 1 + sdy, 1.4f, rgb(0xe8e8f0)); // sword
  circle(cx, cy - 2, R * TS * 0.4f, rgb(0xffa040), 18); // the torch's warmth

  if (M->beast) {
    float mx = ox + (lerpf(s_mx, s_mtx, s_mmove) + 0.5f) * TS, my = oy + (lerpf(s_my, s_mty, s_mmove) + 0.5f) * TS;
    int imx = (int)roundf(lerpf(s_mx, s_mtx, s_mmove)), imy = (int)roundf(lerpf(s_my, s_mty, s_mmove));
    float dd = sqrtf((imx - px) * (imx - px) + (imy - py) * (imy - py)) / R;
    bool lit = dd < 1 && losClear(ipx, ipy, imx, imy);
    if (lit) {
      ellipse(mx, my + 8, 10, 3, rgb(0x000000), 140);
      ellipse(mx, my, 9, 8, rgb(0x2a1a14));
      circle(mx, my - 6, 6, rgb(C_INK));
      line(mx - 4, my - 9, mx - 10, my - 15, 2, rgb(C_CREAM));
      line(mx + 4, my - 9, mx + 10, my - 15, 2, rgb(C_CREAM));
    }
    // In the dark, only the eyes.
    float glow = 0.6f + 0.4f * sinf(s_t * 5);
    circle(mx - 2.5f, my - 6, 1.6f, rgb(0xff3020), (uint8_t)(200 * glow));
    circle(mx + 2.5f, my - 6, 1.6f, rgb(0xff3020), (uint8_t)(200 * glow));
  }
}

// ---------------------------------------------------------------------------
// The prologue: Ariadne's loom
// ---------------------------------------------------------------------------

static const int THROWS = 12;
static int s_throw = 0, s_hits = 0;
static float s_shuttleT = 0, s_flash = 0;
static bool s_rowHit[THROWS];

static void levelDone();

static void startLoom() { s_throw = 0; s_hits = 0; s_shuttleT = 0; s_flash = 0; }

static float shuttleX() {
  float w = 1.6f + s_throw * 0.16f;
  return 240 + 74 * sinf(s_shuttleT * w);
}
static float zoneW() { return 30 - s_throw * 1.4f; }

static void loomThrow() {
  bool hit = fabsf(shuttleX() - 240) < zoneW() / 2;
  s_rowHit[s_throw] = hit;
  if (hit) { s_hits++; RunAudio::fx(RunAudio::DingHigh); } else RunAudio::fx(RunAudio::Bonk);
  s_flash = 0.3f;
  s_throw++;
  if (s_throw >= THROWS) {
    s_spin = 0.85f + 0.03f * s_hits;
    s_prefs.putUChar("spin", (uint8_t)(s_spin * 100));
    s_mode = Mode::Won; s_modeT = 0;
    RunAudio::fx(RunAudio::DingTop);
    levelDone();
  }
}

static void drawLoom() {
  // A vase-painting scene on clay.
  for (int y = (int)(HUD_H * K); y < FH; y++) span(y, 0, FW, mix(0xe48a4a, 0xc0602a, (y - HUD_H * K) / (FH - HUD_H * K)));
  meander(0, 236, W, 14, C_INK, C_TERRA);
  rect(0, 250, W, 30, rgb(C_INK));
  bfAriadne(96, 234, 1.6f, C_INK, C_TERRA);
  // The loom: two posts, a beam, warp threads weighted at the bottom.
  float lx0 = 160, lx1 = 320, ly0 = 52, ly1 = 232;
  rect(lx0 - 6, ly0 - 8, 6, ly1 - ly0 + 8, rgb(C_INK));
  rect(lx1, ly0 - 8, 6, ly1 - ly0 + 8, rgb(C_INK));
  rect(lx0 - 12, ly0 - 10, lx1 - lx0 + 24, 6, rgb(C_INK));
  for (float x = lx0 + 4; x < lx1; x += 8) {
    line(x, ly0, x, ly1 - 14, 1, rgb(C_CREAM), 200);
    circle(x, ly1 - 10, 2.6f, rgb(C_INK)); // loom weights
  }
  // The woven cloth so far: red where the shuttle was true.
  for (int i = 0; i < s_throw; i++) {
    float y = ly0 + 4 + i * 7;
    rect(lx0 + 2, y, lx1 - lx0 - 4, 5, rgb(s_rowHit[i] ? C_THREAD : 0x6a2a20));
    if (s_rowHit[i]) for (float x = lx0 + 6; x < lx1 - 4; x += 16) rect(x, y + 1.5f, 6, 2, rgb(C_GOLD));
  }
  // The gold zone and the shuttle.
  float zy = ly0 + 4 + s_throw * 7;
  if (s_throw < THROWS) {
    rect(240 - zoneW() / 2, zy - 3, zoneW(), 11, rgb(C_GOLD), (uint8_t)(90 + 50 * sinf(s_t * 6)));
    float sx = shuttleX();
    ellipse(sx, zy + 2.5f, 14, 4, rgb(0x6a4422));
    ellipse(sx, zy + 2.5f, 8, 2.6f, rgb(C_THREAD));
  }
  if (s_flash > 0) rect(lx0, zy - 10, lx1 - lx0, 18, rgb(0xffffff), (uint8_t)(s_flash * 400));
  // The ball of thread grows.
  yarnBall(380, 200, 10 + s_hits * 1.6f, s_t);
  char b[24];
  snprintf(b, sizeof(b), "%d / %d", s_hits, THROWS);
  setScale(1);
  txtC(380, 140, b, 1, rgb(C_INK));
  setScale(K);
}

// ---------------------------------------------------------------------------
// Act II: the Minotaur
// ---------------------------------------------------------------------------

static const float GROUND = 228, ARENA_L = 58, ARENA_R = 398;
enum class Bull : uint8_t { Paw, Charge, Dazed, Recover, Down };
static float s_hx, s_hy, s_hvy, s_hinv, s_hstrike, s_hrun;
static int s_hface = 1, s_hhp = 3;
static float s_bx, s_bt, s_bspeed, s_brun, s_bflash;
static int s_bface = -1, s_bhp = 6;
static bool s_bstruck = false;
static Bull s_bull = Bull::Paw;
struct Stone { bool on; float x, t; };
static Stone s_stones[4];

static int bossPhase() { return s_bhp > 4 ? 1 : s_bhp > 2 ? 2 : 3; }

static void startBoss() {
  s_hx = 120; s_hy = GROUND; s_hvy = 0; s_hinv = 0; s_hstrike = 0; s_hface = 1; s_hhp = 3;
  s_bx = 340; s_bt = 0; s_bface = -1; s_bhp = 6; s_bull = Bull::Paw; s_bflash = 0;
  for (Stone &s : s_stones) s.on = false;
}

static void hurtHero() {
  if (s_hinv > 0) return;
  s_hhp--;
  s_hinv = 1.3f;
  s_shake = 6;
  RunAudio::fx(RunAudio::Thunder);
  if (s_hhp <= 0) { s_lostWhy = "The Minotaur was too strong."; s_mode = Mode::Lost; s_modeT = 0; }
}

static void bossAction() {
  // SELECT: strike when he is dazed and close, otherwise leap.
  if (s_bull == Bull::Dazed && !s_bstruck && fabsf(s_bx - s_hx) < 64 && s_hy >= GROUND - 2) {
    s_bstruck = true;
    s_hstrike = 0.35f;
    s_hface = s_bx > s_hx ? 1 : -1;
    s_bhp--;
    s_bflash = 0.4f;
    RunAudio::fx(RunAudio::Pop);
    RunAudio::fx(RunAudio::Bonk);
    if (s_bhp <= 0) { s_bull = Bull::Down; s_bt = 0; RunAudio::fx(RunAudio::DingTop); }
    else { s_bull = Bull::Recover; s_bt = 0; }
  } else if (s_hy >= GROUND - 0.5f) {
    s_hvy = -440;
    RunAudio::fx(RunAudio::Hop);
  }
}

static void updateBoss(float dt) {
  // Theseus.
  float mv = 0;
  if (held(Key::Left)) mv -= 1;
  if (held(Key::Right)) mv += 1;
  if (mv) { s_hface = mv > 0 ? 1 : -1; s_hrun += dt * 12; }
  s_hx = clampf(s_hx + mv * 175 * dt, ARENA_L + 10, ARENA_R - 10);
  s_hvy += 1150 * dt;
  s_hy += s_hvy * dt;
  if (s_hy > GROUND) { s_hy = GROUND; s_hvy = 0; }
  if (s_hinv > 0) s_hinv -= dt;
  if (s_hstrike > 0) s_hstrike -= dt;
  if (s_bflash > 0) s_bflash -= dt;

  int ph = bossPhase();
  s_bt += dt;
  switch (s_bull) {
    case Bull::Paw:
      s_bface = s_hx > s_bx ? 1 : -1;
      if (s_bt > (ph == 3 ? 0.6f : 0.95f)) { s_bull = Bull::Charge; s_bt = 0; s_bspeed = ph == 1 ? 230 : ph == 2 ? 275 : 320; RunAudio::fx(RunAudio::Honk); }
      break;
    case Bull::Charge:
      s_bx += s_bface * s_bspeed * dt;
      s_brun += dt * 16;
      if (fabsf(s_bx - s_hx) < 30 && s_hy > GROUND - 46) hurtHero();
      if (s_bx < ARENA_L || s_bx > ARENA_R) {
        s_bx = clampf(s_bx, ARENA_L, ARENA_R);
        s_bull = Bull::Dazed; s_bt = 0; s_bstruck = false;
        s_shake = 8;
        RunAudio::fx(RunAudio::Thunder);
        if (ph >= 2) // the crash shakes stones loose from the ceiling
          for (int i = 0; i < (ph == 2 ? 2 : 4); i++) s_stones[i] = {true, ARENA_L + 20 + frand() * (ARENA_R - ARENA_L - 40), 0};
      }
      break;
    case Bull::Dazed:
      if (s_bt > 2.3f) { s_bull = Bull::Paw; s_bt = 0; }
      break;
    case Bull::Recover:
      // He staggers right back to the middle of the arena (Theseus struck
      // him at a wall), leaving room to see the next charge and leap it.
      {
        float mid = (ARENA_L + ARENA_R) / 2, step = 240 * dt;
        s_bx = fabsf(mid - s_bx) <= step ? mid : s_bx + (mid > s_bx ? step : -step);
      }
      if (s_bt > 0.9f) { s_bull = Bull::Paw; s_bt = 0; }
      break;
    case Bull::Down:
      if (s_bt > 2.2f) { s_mode = Mode::Won; s_modeT = 0; }
      break;
  }
  for (Stone &s : s_stones) {
    if (!s.on) continue;
    s.t += dt;
    if (s.t > 0.8f && s.t - dt <= 0.8f && fabsf(s.x - s_hx) < 16) hurtHero();
    if (s.t > 1.2f) s.on = false;
  }
}

static void drawBoss() {
  // The scene as a painted vase: clay ground, black figures, key bands.
  for (int y = (int)(HUD_H * K); y < FH; y++) span(y, 0, FW, mix(0xe9904e, 0xc8652e, (y - HUD_H * K) / (FH - HUD_H * K)));
  meander(0, HUD_H + 2, W, 14, C_INK, C_TERRA);
  // A fresco frieze of bull-leapers.
  for (float x = 50; x < W; x += 120) {
    float y = 74;
    uint16_t c = rgb(0x8a3a18);
    ellipse(x, y, 15, 6, c);                                      // a charging bull
    for (int l = 0; l < 4; l++) line(x - 10 + l * 6, y + 3, x - 13 + l * 7, y + 11, 1.6f, c);
    ellipse(x + 15, y - 2, 5, 4, c);
    line(x + 15, y - 5, x + 21, y - 11, 1.4f, c);
    line(x + 13, y - 5, x + 10, y - 11, 1.4f, c);
    line(x - 15, y - 2, x - 20, y + 4, 1, c);
    // The leaper, somersaulting over its back.
    float a = 0.6f;
    circle(x + 2, y - 22, 2.6f, c);
    line(x + 2, y - 20, x - 6, y - 14, 1.8f, c);
    line(x - 6, y - 14, x - 10 + cosf(a) * 4, y - 22, 1.4f, c);
    line(x - 6, y - 14, x - 14, y - 18, 1.4f, c);
  }
  for (float x : {58.0f, 168.0f, 288.0f, 398.0f}) column(x, 90, GROUND, 22, C_MRED);
  for (float x : {113.0f, 343.0f}) { rect(x - 1, 120, 2, 10, rgb(C_INK)); flame(x, 118, 1.4f); }
  meander(0, GROUND, W, 14, C_INK, C_TERRA);
  rect(0, GROUND + 14, W, H - GROUND, rgb(C_INK));

  float sh = (frand() - 0.5f) * s_shake;
  for (const Stone &s : s_stones) {
    if (!s.on) continue;
    if (s.t < 0.8f) ellipse(s.x, GROUND - 1, 12 * s.t, 3, rgb(0x000000), 120); // its shadow grows
    else rect(s.x - 7, GROUND - 14, 14, 12, rgb(0x6a5040));
    if (s.t < 0.8f) rect(s.x - 6, 90 + (GROUND - 104) * s.t * s.t / 0.64f, 12, 10, rgb(0x6a5040));
  }
  // The Minotaur.
  bool down = s_bull == Bull::Down;
  uint32_t mink = s_bflash > 0 && fmodf(s_t, 0.1f) < 0.05f ? 0xffffff : C_INK;
  if (down) {
    float k = clampf(s_bt / 0.8f, 0, 1);
    bfMinotaur(s_bx + sh, GROUND + k * 18, 1.25f, s_bface, true, 0, mink, C_TERRA);
  } else {
    bfMinotaur(s_bx + sh, GROUND, 1.25f, s_bface, s_bull == Bull::Charge, s_brun, mink, C_TERRA);
  }
  if (s_bull == Bull::Paw) { // dust from his pawing hoof
    for (int i = 0; i < 3; i++) circle(s_bx - s_bface * (14 + i * 6) + sinf(s_t * 20 + i) * 3, GROUND - 4 - i * 3, 3 + i, rgb(0xb07a50), 140);
    if (fmodf(s_t, 0.4f) < 0.25f) { setScale(1); txtCS((int)s_bx, 74, "!", 2, rgb(0xfff0c0)); setScale(K); }
  }
  if (s_bull == Bull::Dazed) for (int i = 0; i < 3; i++) {
    float a = s_t * 4 + i * 2.09f;
    star(s_bx + cosf(a) * 16, 104 + sinf(a) * 5, 4, rgb(0xfff0a0));
  }
  // Theseus.
  if (!(s_hinv > 0 && fmodf(s_t, 0.16f) < 0.08f)) {
    int pose = s_hstrike > 0 ? 2 : s_hy < GROUND - 1 ? 1 : 0;
    bfTheseus(s_hx + sh, s_hy, 1.15f, s_hrun, pose, s_hface, C_INK, C_TERRA);
  }
  // Life: Theseus's shields, the Minotaur's horns.
  setScale(K);
  for (int i = 0; i < 3; i++) { circle(22 + i * 18, HUD_H + 34, 6, rgb(i < s_hhp ? C_INK : 0x9a5a3a)); circle(22 + i * 18, HUD_H + 34, 2, rgb(C_ADDED)); }
  for (int i = 0; i < 6; i++) {
    float x = W - 24 - i * 16;
    uint16_t c = rgb(i < s_bhp ? C_INK : 0x9a5a3a);
    line(x - 4, HUD_H + 38, x - 6, HUD_H + 28, 2, c);
    line(x + 4, HUD_H + 38, x + 6, HUD_H + 28, 2, c);
  }
}

// ---------------------------------------------------------------------------
// Act III: the ball of thread
// ---------------------------------------------------------------------------
//
// Each yarn level is grown around one route out:
// 1. Start from a boustrophedon Hamiltonian path through every cell, then
//    apply ~3000 backbite moves (Mansfield 1982): pick an end e and a grid
//    neighbour v of e that isn't its path neighbour; if v = path[i], the
//    path becomes path[0..i] + reverse(path[i+1..n-1]) - still a
//    Hamiltonian path, with a new end. The moves connect all Hamiltonian
//    paths of a rectangular grid (Kazazi, arXiv:2601.06749), so the chain
//    wanders over all of them.
// 2. From the cell nearest the centre, follow the path until it first
//    touches the edge after at least Lmin steps: a self-avoiding route that
//    winds through the whole hall. That is the way out.
// 3. Every other cell joins it by Wilson's walks, so false branches hang off
//    the route - and the thread can't cross itself, so a wrong turn means
//    winding back.
// 4. Spindles (extra thread) by dart throwing on the branches; water by an
//    fbm threshold; guards on straight runs.

static const int YW = 18, YH = 10;
static const float YC = 24, YX = (W - YW * YC) / 2, YY = HUD_H + 8;
static uint8_t s_yopen[YH][YW];
static uint8_t s_ywater[YH][YW], s_yspindle[YH][YW], s_yused[YH][YW];
static int s_ystart, s_yexit, s_yexitDir;
static int16_t s_yarn[YW * YH];
static int s_ylen = 0, s_ybudget = 0, s_ydir = 1, s_ywant = 1;
static int s_yto = -1;
static float s_ymove = 1, s_yroll = 0, s_ylimit = 0, s_yspotT = 0;
static bool s_yrewind = false;
struct Guard { bool on; int x0, x1, y; float x; int dir; };
static Guard s_guards[2];

static void yCarve(int x, int y, int d) {
  s_yopen[y][x] |= BIT[d];
  s_yopen[y + DY[d]][x + DX[d]] |= BIT[(d + 2) & 3];
}

static void makeYarn(int which) {
  static int16_t path[YW * YH], idx[YW * YH];
  const int n = YW * YH;
  for (int y = 0, k = 0; y < YH; y++)
    for (int i = 0; i < YW; i++, k++) path[k] = y * YW + ((y & 1) ? YW - 1 - i : i);
  for (int k = 0; k < n; k++) idx[path[k]] = k;
  auto reverseRange = [&](int a, int b) {
    while (a < b) { int16_t t = path[a]; path[a] = path[b]; path[b] = t; idx[path[a]] = a; idx[path[b]] = b; a++; b--; }
  };
  for (int m = 0; m < 3000; m++) {
    if (rnd() & 1) reverseRange(0, n - 1); // backbite from either end
    int e = path[n - 1], ex = e % YW, ey = e / YW, d = irand(4);
    int vx = ex + DX[d], vy = ey + DY[d];
    if (vx < 0 || vy < 0 || vx >= YW || vy >= YH) continue;
    int v = vy * YW + vx, i = idx[v];
    if (i == n - 2) continue;
    reverseRange(i + 1, n - 1);
  }
  // The route out.
  s_ystart = (YH / 2) * YW + YW / 2;
  int si = idx[s_ystart];
  int lmin = 34 + which * 10, endI = -1, dir = 1;
  for (int tries = 0; tries < 2 && endI < 0; tries++) {
    for (int pass = 0; pass < 2 && endI < 0; pass++) {
      int step = pass ? -1 : 1;
      for (int j = si + step; j >= 0 && j < n; j += step) {
        int c = path[j], x = c % YW, y = c / YW;
        if (abs(j - si) >= lmin && (x == 0 || y == 0 || x == YW - 1 || y == YH - 1)) { endI = j; dir = step; break; }
      }
    }
    lmin /= 2;
  }
  if (endI < 0) { endI = dir > 0 ? n - 1 : 0; }
  memset(s_yopen, 0, sizeof(s_yopen));
  static bool tree[YW * YH];
  memset(tree, 0, sizeof(tree));
  for (int j = si; j != endI; j += dir) {
    int a = path[j], b = path[j + dir];
    tree[a] = tree[b] = true;
    int ax = a % YW, ay = a / YW;
    for (int d = 0; d < 4; d++) if (ax + DX[d] == b % YW && ay + DY[d] == b / YW) yCarve(ax, ay, d);
  }
  s_yexit = path[endI];
  int exx = s_yexit % YW, exy = s_yexit / YW;
  s_yexitDir = exx == 0 ? 3 : exx == YW - 1 ? 1 : exy == 0 ? 0 : 2;
  int solLen = abs(endI - si);
  // The branches (Wilson, with the route as the tree).
  {
    static int8_t next[YW * YH];
    for (int c0 = 0; c0 < n; c0++) {
      if (tree[c0]) continue;
      int cur = c0;
      while (!tree[cur]) {
        int x = cur % YW, y = cur / YW, d;
        do { d = irand(4); } while (x + DX[d] < 0 || x + DX[d] >= YW || y + DY[d] < 0 || y + DY[d] >= YH);
        next[cur] = d;
        cur = (y + DY[d]) * YW + x + DX[d];
      }
      cur = c0;
      while (!tree[cur]) {
        int x = cur % YW, y = cur / YW, d = next[cur];
        yCarve(x, y, d);
        tree[cur] = true;
        cur = (y + DY[d]) * YW + x + DX[d];
      }
    }
  }
  // Water, spindles, guards.
  seedNoise(esp_random());
  memset(s_ywater, 0, sizeof(s_ywater));
  memset(s_yspindle, 0, sizeof(s_yspindle));
  memset(s_yused, 0, sizeof(s_yused));
  if (which == 1)
    for (int y = 0; y < YH; y++)
      for (int x = 0; x < YW; x++) s_ywater[y][x] = fbm(x * 0.25f, y * 0.25f, 3) > 0.02f;
  int placed = 0;
  for (int tries = 0; tries < 400 && placed < 4; tries++) {
    int x = irand(YW), y = irand(YH), c = y * YW + x;
    if (c == s_ystart || c == s_yexit) continue;
    bool ok = true;
    for (int yy = y - 3; yy <= y + 3 && ok; yy++)
      for (int xx = x - 3; xx <= x + 3 && ok; xx++)
        if (xx >= 0 && yy >= 0 && xx < YW && yy < YH && s_yspindle[yy][xx]) ok = false;
    if (!ok) continue;
    s_yspindle[y][x] = 1;
    placed++;
  }
  // The route's cells, and what its thread costs (a wet step costs two).
  static bool onRoute[YW * YH];
  memset(onRoute, 0, sizeof(onRoute));
  int routeCost = 0;
  for (int j = si;; j += dir) {
    onRoute[path[j]] = true;
    if (j != si) routeCost += s_ywater[path[j] / YW][path[j] % YW] ? 2 : 1;
    if (j == endI) break;
  }
  // Guards patrol corridors the route crosses - one or two of its cells -
  // so there is always a moment to slip past. (A guard walking the route's
  // own corridor could never be passed: the ball can't overtake him.)
  s_guards[0].on = s_guards[1].on = false;
  if (which == 2) {
    int g = 0;
    for (int pass = 0; pass < 2 && g < 2; pass++)
      for (int tries = 0; tries < 400 && g < 2; tries++) {
        int y = irand(YH), x = irand(YW - 3);
        int len = 0;
        while (x + len + 1 < YW && (s_yopen[y][x + len] & 2)) len++;
        if (len < 3 || abs(y - YH / 2) < 1) continue;
        if (g == 1 && s_guards[0].y == y) continue;
        int cross = 0;
        bool gateOrStart = false;
        for (int k = x; k <= x + len; k++) {
          cross += onRoute[y * YW + k];
          gateOrStart |= y * YW + k == s_yexit || y * YW + k == s_ystart;
        }
        if (gateOrStart || cross > 2 || (pass == 0 && cross == 0)) continue; // 2nd pass: side halls do
        s_guards[g] = {true, x, x + len, y, (float)x, 1};
        g++;
      }
  }
  s_ybudget = routeCost + 1 + 10 - which * 2;
  s_ylimit = which == 3 ? solLen * 0.45f + 18 : 0;
  s_yspotT = 0;
  s_yarn[0] = s_ystart;
  s_ylen = 1;
  s_yused[s_ystart / YW][s_ystart % YW] = 1;
  s_ymove = 1; s_yto = -1; s_yrewind = false;
  s_ydir = s_ywant = 1;
}

static int yarnCost() {
  int c = 0;
  for (int i = 1; i < s_ylen; i++) c += s_ywater[s_yarn[i] / YW][s_yarn[i] % YW] ? 2 : 1;
  return c;
}

static bool yCanGo(int c, int d) {
  int x = c % YW, y = c / YW;
  if (!(s_yopen[y][x] & BIT[d])) return false;
  int nx = x + DX[d], ny = y + DY[d];
  if (s_yused[ny][nx]) return false;
  int cost = s_ywater[ny][nx] ? 2 : 1;
  return yarnCost() + cost <= s_ybudget;
}

static void yPop() {
  if (s_ylen <= 1) return;
  int c = s_yarn[--s_ylen];
  s_yused[c / YW][c % YW] = 0;
}

static void updateYarn(float dt) {
  int head = s_yarn[s_ylen - 1];
  if (s_ymove < 1) {
    s_ymove += dt * (s_yrewind ? 6.0f : 3.8f);
    s_yroll += dt * 9;
    if (s_ymove >= 1) {
      s_ymove = 1;
      if (s_yrewind) {
        yPop();
      } else if (s_yto >= 0) {
        s_yarn[s_ylen++] = s_yto;
        s_yused[s_yto / YW][s_yto % YW] = 1;
        if (s_yspindle[s_yto / YW][s_yto % YW] == 1) {
          s_yspindle[s_yto / YW][s_yto % YW] = 2;
          s_ybudget += 5;
          note("A spindle! +5 thread");
          RunAudio::fx(RunAudio::DingHigh);
        }
      }
      s_yto = -1;
    }
    return;
  }
  // Guards: their lanterns light the cell they stand on and the next. After
  // a pull the thread has a moment's grace, so it isn't pulled twice at once.
  if (s_yspotT > 0) s_yspotT -= dt;
  for (Guard &g : s_guards) {
    if (!g.on) continue;
    g.x += g.dir * dt * 1.4f;
    if (g.x >= g.x1) { g.x = g.x1; g.dir = -1; }
    if (g.x <= g.x0) { g.x = g.x0; g.dir = 1; }
    int gx = (int)roundf(g.x);
    head = s_yarn[s_ylen - 1];
    if (s_yspotT <= 0 && head / YW == g.y && (head % YW == gx || head % YW == gx + g.dir) && s_ylen > 1) {
      note("Spotted! The thread is pulled back");
      RunAudio::fx(RunAudio::Thunder);
      for (int i = 0; i < 4; i++) yPop();
      s_shake = 4;
      s_yspotT = 1.5f;
    }
  }
  head = s_yarn[s_ylen - 1];
  if (s_yrewind) {
    if (s_ylen > 1) { s_ymove = 0; return; }
    s_yrewind = false;
  }
  // Out through the gate?
  if (head == s_yexit && (s_ywant == s_yexitDir || s_ydir == s_yexitDir)) {
    RunAudio::fx(RunAudio::DingTop);
    s_mode = Mode::Won; s_modeT = 0;
    return;
  }
  // Steering back the way it came stops the ball (to wait for a guard);
  // any other way sets it rolling again.
  int d = -1;
  if (yCanGo(head, s_ywant)) d = s_ywant;
  else if (s_ywant != ((s_ydir + 2) & 3) && yCanGo(head, s_ydir)) d = s_ydir;
  if (d < 0) {
    // Stopped. If only the thread is lacking, say so.
    int x = head % YW, y = head / YW;
    for (int k : {s_ywant, s_ydir})
      if ((s_yopen[y][x] & BIT[k]) && !s_yused[y + DY[k]][x + DX[k]] && s_noteT <= 0) note("No thread left - SELECT winds back");
    return;
  }
  s_ydir = d;
  int x = head % YW, y = head / YW;
  s_yto = (y + DY[d]) * YW + x + DX[d];
  s_ymove = 0;
  RunAudio::fx(RunAudio::Tick);
}

static void yCenter(int c, float &x, float &y) { x = YX + (c % YW + 0.5f) * YC; y = YY + (c / YW + 0.5f) * YC; }

// A point a distance `back` cells behind the yarn's head, along the thread.
static void alongYarn(float back, float &x, float &y) {
  float hx, hy;
  int head = s_yarn[s_ylen - 1];
  yCenter(head, hx, hy);
  if (s_yto >= 0 || (s_yrewind && s_ylen > 1)) {
    int to = s_yrewind ? s_yarn[s_ylen - 2] : s_yto;
    float tx, ty;
    yCenter(to, tx, ty);
    hx = lerpf(hx, tx, s_ymove); hy = lerpf(hy, ty, s_ymove);
    back -= s_yrewind ? -s_ymove : s_ymove;
  }
  int i = s_ylen - 1;
  float px = hx, py = hy;
  if (s_yto >= 0) { yCenter(s_yarn[i], px, py); back = fmaxf(0, back); }
  while (back > 0 && i > 0) {
    float ax, ay, bx, by;
    yCenter(s_yarn[i], ax, ay);
    yCenter(s_yarn[i - 1], bx, by);
    float k = fminf(1, back);
    px = lerpf(ax, bx, k); py = lerpf(ay, by, k);
    back -= 1; i--;
  }
  if (back > 0 && i == 0) yCenter(s_yarn[0], px, py);
  x = px; y = py;
}

static void drawYarnLevel() {
  int which = LV[s_lv].n;
  float sh = (frand() - 0.5f) * s_shake;
  rect(0, HUD_H, W, H - HUD_H, rgb(C_INK));
  float ox = sh, oy = sh * 0.5f;
  // Floor.
  for (int y = 0; y < YH; y++)
    for (int x = 0; x < YW; x++) {
      float px = YX + x * YC + ox, py = YY + y * YC + oy;
      uint32_t h = hash2(x * 7, y * 3);
      uint32_t base = ((x + y) & 1) ? 0x46302a : 0x3e2a24;
      if (s_ywater[y][x]) base = mixHex(0x1f4a72, 0x2a5a8a, (h & 255) / 255.0f);
      rect(px, py, YC, YC, rgb(base));
      if (s_ywater[y][x]) {
        float w = sinf(s_t * 2 + x + y * 0.7f) * 2;
        line(px + 3, py + 8 + w, px + YC - 3, py + 8 - w, 0.8f, rgb(0x8ac0e8), 150);
        line(px + 5, py + 16 - w, px + YC - 5, py + 16 + w, 0.8f, rgb(0x8ac0e8), 120);
      } else if ((h & 3) == 0) {
        circle(px + YC / 2, py + YC / 2, 2, rgb(0xe9c9a0), 120);
      }
      if (s_yspindle[y][x] == 1) { // a wooden spindle wrapped in red
        line(px + 6, py + YC - 6, px + YC - 6, py + 6, 2, rgb(0x8a5a2a));
        ellipseRot(px + YC / 2, py + YC / 2, 6, 3.5f, -0.785f, rgb(C_THREAD));
        circle(px + YC / 2, py + YC / 2, 9, rgb(C_GOLD), (uint8_t)(40 + 30 * sinf(s_t * 4)));
      }
    }
  // The gate out, glowing with daylight.
  {
    float ex, ey;
    yCenter(s_yexit, ex, ey);
    ex += DX[s_yexitDir] * YC * 0.6f + ox; ey += DY[s_yexitDir] * YC * 0.6f + oy;
    circle(ex, ey, 18, rgb(0xfff0c0), (uint8_t)(90 + 50 * sinf(s_t * 3)));
    circle(ex, ey, 9, rgb(0xfffae8));
  }
  // Walls: black with red trim.
  for (int y = 0; y < YH; y++)
    for (int x = 0; x < YW; x++) {
      float px = YX + x * YC + ox, py = YY + y * YC + oy;
      uint8_t o = s_yopen[y][x];
      bool gate = y * YW + x == s_yexit;
      if (!(o & 1) && !(gate && s_yexitDir == 0)) { rect(px - 2, py - 2, YC + 4, 4, rgb(C_INK)); rect(px - 2, py + 1, YC + 4, 1, rgb(C_MRED)); }
      if (!(o & 8) && !(gate && s_yexitDir == 3)) { rect(px - 2, py - 2, 4, YC + 4, rgb(C_INK)); rect(px + 1, py - 2, 1, YC + 4, rgb(C_MRED)); }
      if (y == YH - 1 && !(gate && s_yexitDir == 2)) { rect(px - 2, py + YC - 2, YC + 4, 4, rgb(C_INK)); }
      if (x == YW - 1 && !(gate && s_yexitDir == 1)) { rect(px + YC - 2, py - 2, 4, YC + 4, rgb(C_INK)); }
    }
  // Guards and their lantern light.
  for (const Guard &g : s_guards) {
    if (!g.on) continue;
    float gx = YX + (g.x + 0.5f) * YC + ox, gy = YY + (g.y + 0.5f) * YC + oy;
    rect(gx - YC / 2 + (g.dir > 0 ? 0 : -YC), gy - YC / 2, YC * 2, YC, rgb(0xffe08a), 60);
    circle(gx + g.dir * 8, gy, 3, rgb(0xffe08a));
    ellipse(gx, gy + 6, 7, 2.5f, rgb(0x000000), 120);
    ellipse(gx, gy, 6, 7, rgb(0x6a3a2a));
    circle(gx, gy - 6, 5, rgb(C_BRONZE));
    rect(gx - 5, gy - 8, 10, 2, rgb(C_ADDED));
  }
  // The thread, then Theseus and the Athenians walking along it.
  for (int i = 0; i + 1 < s_ylen; i++) {
    float ax, ay, bx, by;
    yCenter(s_yarn[i], ax, ay); yCenter(s_yarn[i + 1], bx, by);
    line(ax + ox, ay + oy + 1, bx + ox, by + oy + 1, 3, rgb(C_THREAD));
    line(ax + ox, ay + oy, bx + ox, by + oy, 0.9f, rgb(0xff9aa4), 170);
  }
  float hx, hy;
  alongYarn(0, hx, hy);
  if (s_ylen > 0) {
    float ax, ay;
    yCenter(s_yarn[s_ylen - 1], ax, ay);
    line(ax + ox, ay + oy + 1, hx + ox, hy + oy + 1, 3, rgb(C_THREAD));
  }
  for (int k = 3; k >= 1; k--) {
    float fx, fy;
    alongYarn(1.6f + k * 1.1f, fx, fy);
    fx += ox; fy += oy;
    if (k == 1) { // Theseus
      ellipse(fx, fy + 6, 6, 2, rgb(0x000000), 120);
      ellipse(fx, fy, 5, 5.5f, rgb(0xf2ead6));
      circle(fx, fy - 6, 4, rgb(C_BRONZE));
      rect(fx - 0.7f, fy - 11, 1.4f, 6, rgb(C_MRED));
    } else { // youths and maidens of Athens
      ellipse(fx, fy + 5, 5, 2, rgb(0x000000), 100);
      ellipse(fx, fy, 4, 4.5f, rgb(k == 2 ? 0xd8c8f0 : 0xc8e0d0));
      circle(fx, fy - 5, 3, rgb(0x6a4a3a));
    }
  }
  float r = 8 - 3.5f * clampf(yarnCost() / (float)s_ybudget, 0, 1); // the ball shrinks as it unrolls
  yarnBall(hx + ox, hy + oy, r, s_yroll);
  // The last level: dust falling, the roof coming down.
  if (which == 3) {
    float left = s_ylimit - s_clock;
    for (int i = 0; i < 10; i++) {
      float x = fmodf(i * 53.7f + s_t * 3, W), y = fmodf(i * 37.1f + s_t * (90 + i * 7), H - HUD_H) + HUD_H;
      rect(x, y, 2, 3, rgb(0xb09080), 160);
    }
    if (left < 8) rect(0, HUD_H, W, H - HUD_H, rgb(0x000000), (uint8_t)clampf((8 - left) * 14, 0, 110));
  }
}

// ---------------------------------------------------------------------------
// Story cards, title, vignettes
// ---------------------------------------------------------------------------

// The classical (Cretan) seven-circuit labyrinth, drawn as its path - the
// figure on the coins of Knossos. Circuits are walked in the order
// 3 2 1 4 7 6 5, then the centre; each circuit sweeps almost all the way
// round, alternating direction, and steps between rings at the axis.
static void cretan(float cx, float cy, float R, float progress) {
  circle(cx, cy, R + 4, rgb(C_INK));
  static const int ORDER[8] = {3, 2, 1, 4, 7, 6, 5, 8};
  float dr = R / 8.6f;
  float total = 8 * 6.0f, drawn = progress * total, used = 0;
  float a0 = 1.5708f + 0.22f, a1 = 1.5708f + 6.2832f - 0.22f; // leave the axis at the bottom
  float lastX = cx, lastY = cy + R + 2;
  int dirSign = 1;
  for (int k = 0; k < 8 && used < drawn; k++) {
    float rr = R - (ORDER[k] - 0.5f) * dr;
    if (ORDER[k] == 8) rr = dr * 0.6f;
    float from = dirSign > 0 ? a0 : a1, to = dirSign > 0 ? a1 : a0;
    float sx = cx + cosf(from) * rr, sy = cy + sinf(from) * rr;
    line(lastX, lastY, sx, sy, dr * 0.55f, rgb(C_MRED));
    int steps = 40;
    float px = sx, py = sy;
    for (int i = 1; i <= steps && used < drawn; i++) {
      float a = from + (to - from) * i / steps;
      float x = cx + cosf(a) * rr, y = cy + sinf(a) * rr;
      line(px, py, x, y, dr * 0.55f, rgb(C_MRED));
      px = x; py = y;
      used += 6.0f / steps;
    }
    lastX = px; lastY = py;
    dirSign = -dirSign;
  }
  circle(cx, cy, dr * 0.9f, rgb(C_GOLD));
}

static void vignette(Vig v, float cx, float cy, float r) {
  circle(cx, cy, r + 4, rgb(C_INK));
  circle(cx, cy, r, rgb(v == V_DARK || v == V_STARS ? 0x1a1430 : C_TERRA));
  float gy = cy + r * 0.62f;
  switch (v) {
    case V_LOOM:
      bfAriadne(cx - 14, gy, 1.0f, C_INK, C_TERRA);
      rect(cx + 8, cy - 30, 3, 56, rgb(C_INK)); rect(cx + 38, cy - 30, 3, 56, rgb(C_INK));
      for (float x = cx + 13; x < cx + 38; x += 5) line(x, cy - 28, x, cy + 20, 0.8f, rgb(C_CREAM));
      for (int i = 0; i < 4; i++) rect(cx + 11, cy - 24 + i * 6, 27, 3, rgb(C_THREAD));
      break;
    case V_GATE:
      column(cx - 30, cy - 40, gy, 14, C_MRED);
      column(cx + 30, cy - 40, gy, 14, C_MRED);
      rect(cx - 42, cy - 46, 84, 8, rgb(C_INK));
      bfTheseus(cx, gy, 0.9f, 0, 0, 1, C_INK, C_TERRA);
      line(cx - 30, gy - 30, cx - 6, gy - 20, 1, rgb(C_THREAD));
      break;
    case V_FRESCO:
      for (int i = 0; i < 3; i++) {
        float x = cx - 26 + i * 26, y = cy - 18 + i * 14;
        ellipseRot(x, y, 14, 5, -0.3f, rgb(C_FBLUE));
        const float t[6] = {x - 12, y + 3, x - 20, y - 4, x - 20, y + 8};
        poly(t, 3, rgb(C_FBLUE));
      }
      labrys(cx + 26, cy + 26, 1.2f, C_GOLD);
      break;
    case V_DOORS:
      rect(cx - 26, cy - 34, 52, 64, rgb(0x8a5a24));
      rect(cx - 22, cy - 30, 22, 56, rgb(C_BRONZE));
      rect(cx + 1, cy - 30, 21, 56, rgb(C_BRONZE));
      for (int k = 0; k < 8; k++) { float a = k * 0.785f + s_t; circle(cx + cosf(a) * 14, cy + sinf(a) * 14, 3, rgb(0x6a4418)); }
      circle(cx, cy, 10, rgb(0x6a4418));
      break;
    case V_DARK:
      for (int i = 0; i < 2; i++) {
        float g = 0.6f + 0.4f * sinf(s_t * 4);
        circle(cx - 12 + i * 24, cy - 6, 4, rgb(0xff3020), (uint8_t)(255 * g));
      }
      flame(cx, cy + 34, 1.6f);
      break;
    case V_BULL:
      bfMinotaur(cx + 6, gy, 0.85f, -1, false, 0, C_INK, C_TERRA);
      break;
    case V_YARN:
      cretan(cx, cy, r * 0.75f, fmodf(s_t * 0.2f, 1.2f));
      yarnBall(cx + r * 0.5f, cy + r * 0.5f, 10, s_t * 2);
      break;
    case V_WATER:
      for (int i = 0; i < 4; i++) {
        float y = cy - 20 + i * 14;
        for (float x = cx - 40; x < cx + 40; x += 10) line(x, y + sinf(x * 0.2f + s_t * 2) * 3, x + 10, y + sinf((x + 10) * 0.2f + s_t * 2) * 3, 2, rgb(C_FBLUE));
      }
      yarnBall(cx, cy + 4, 9, s_t);
      break;
    case V_GUARD:
      rect(cx - 6, cy - 4, 40, 30, rgb(0xffe08a), 90);
      bfTheseus(cx - 10, gy, 0.85f, 0, 0, 1, C_INK, C_TERRA);
      circle(cx + 6, cy - 6, 4, rgb(0xffe08a));
      break;
    case V_LIGHT:
      circle(cx, cy - 6, 30, rgb(0xfff0c0));
      rect(cx - 30, cy - 6, 60, 50, rgb(0xfff0c0));
      for (int i = 0; i < 3; i++) bfTheseus(cx - 20 + i * 20, gy, 0.55f, i, 0, 1, C_INK, C_TERRA);
      break;
    case V_STARS: {
      // Corona Borealis: an arc of seven stars.
      for (int i = 0; i < 7; i++) {
        float a = 3.6f + i * 0.36f;
        star(cx + cosf(a) * r * 0.55f, cy - 4 + sinf(a) * r * 0.55f + r * 0.3f, i == 3 ? 5 : 3.5f, rgb(0xfff4c0));
      }
      // A ship on dark water.
      const float hull[8] = {cx - 30, cy + 30, cx + 30, cy + 30, cx + 22, cy + 40, cx - 22, cy + 40};
      poly(hull, 4, rgb(C_INK));
      line(cx, cy + 30, cx, cy + 4, 1.5f, rgb(C_INK));
      const float sail[6] = {cx + 1, cy + 6, cx + 18, cy + 26, cx + 1, cy + 26};
      poly(sail, 3, rgb(0xf4ecd8));
      break;
    }
  }
}

static void drawStory() {
  for (int y = 0; y < FH; y++) span(y, 0, FW, mix(0x2a1410, 0x140a08, y / (float)FH));
  card(10, 8, W - 20, H - 16);
  vignette(LV[s_lv].vig, 82, 140, 58);
  setScale(1);
  txtC(296, 28, LV[s_lv].act, 1, rgb(C_INK));
  txtC(296, 46, LV[s_lv].name, 1, rgb(0xfff4e0));
  wrapC(296, 72, LV[s_lv].story, 24, rgb(C_INK));
  txtC(296, 242, "SELECT begin", 1, rgb(0x5a2410));
  setScale(K);
}

static void drawTitle() {
  for (int y = 0; y < FH; y++) span(y, 0, FW, mix(0x2a1410, 0x0e0806, y / (float)FH));
  card(10, 8, W - 20, H - 16);
  cretan(112, 140, 92, fminf(1.0f, s_t * 0.35f));
  setScale(1);
  txtC(326, 30, "ARIADNE'S", 2, rgb(C_INK));
  txtC(326, 62, "THREAD", 2, rgb(0xfff4e0));
  char b[32];
  snprintf(b, sizeof(b), "<  %d of %d  >", s_pick + 1, LEVELS);
  txtC(326, 104, b, 1, rgb(C_INK));
  const Level &l = LV[s_pick];
  txtC(326, 126, l.act, 1, rgb(0x5a2410));
  bool open = s_pick <= s_unlocked;
  wrapC(326, 144, l.name, 18, rgb(open ? 0xfff4e0 : 0x8a4a2a));
  if (!open) txtC(326, 186, "Finish the one before", 1, rgb(0x5a2410));
  else if (s_best[s_pick]) {
    snprintf(b, sizeof(b), "Best %lu.%lus", (unsigned long)(s_best[s_pick] / 1000), (unsigned long)(s_best[s_pick] / 100 % 10));
    txtC(326, 186, b, 1, rgb(C_INK));
  }
  txtC(326, 240, "SELECT play", 1, rgb(0x5a2410));
  setScale(K);
}

static void drawHud() {
  rect(0, 0, W, HUD_H, rgb(C_INK));
  rect(0, HUD_H - 2, W, 2, rgb(C_MRED));
  char b[32];
  setScale(1);
  txt(8, 6, LV[s_lv].name, 1, rgb(0xf4d8b0));
  int secs = (int)s_clock;
  if (LV[s_lv].kind == K_YARN && LV[s_lv].n == 3) secs = (int)fmaxf(0, s_ylimit - s_clock);
  snprintf(b, sizeof(b), "%d:%02d", secs / 60, secs % 60);
  txt(W - 8 - textW(b, 1), 6, b, 1, rgb(LV[s_lv].kind == K_YARN && LV[s_lv].n == 3 && secs < 10 ? 0xff5a4a : 0xf4d8b0));
  setScale(K);
  // Thread left.
  int used = 0, budget = 1;
  if (LV[s_lv].kind == K_MAZE) { used = s_plen; budget = s_budget; }
  if (LV[s_lv].kind == K_YARN) { used = yarnCost(); budget = s_ybudget; }
  if (LV[s_lv].kind == K_MAZE || LV[s_lv].kind == K_YARN) {
    float bx = 300, bw = 84;
    yarnBall(bx - 12, 12, 6, s_t);
    roundRect(bx, 8, bw, 8, 4, rgb(0x3a2420));
    float left = clampf(1 - used / (float)budget, 0, 1);
    roundRect(bx, 8, bw * left, 8, 4, rgb(left < 0.15f ? 0xff7a4a : C_THREAD));
  }
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

static void setMode(Mode m) { s_mode = m; s_modeT = 0; }

static void beginLevel() {
  s_clock = 0; s_shake = 0; s_noteT = 0;
  switch (LV[s_lv].kind) {
    case K_LOOM: startLoom(); break;
    case K_MAZE: startMaze(); s_camX = (s_px + 0.5f) * TS - W / 2; s_camY = (s_py + 0.5f) * TS - (H - HUD_H) / 2; break;
    case K_BOSS: startBoss(); break;
    case K_YARN: makeYarn(LV[s_lv].n); break;
    case K_END: break;
  }
  setMode(LV[s_lv].kind == K_END ? Mode::Won : Mode::Play);
}

static void levelDone() {
  if (s_bench) return;
  uint32_t ms = (uint32_t)(s_clock * 1000);
  if (LV[s_lv].kind != K_END && LV[s_lv].kind != K_LOOM && (!s_best[s_lv] || ms < s_best[s_lv])) {
    s_best[s_lv] = ms;
    char k[4] = {'b', (char)('a' + s_lv), 0};
    s_prefs.putUInt(k, ms);
  }
  if (s_lv + 1 > s_unlocked && s_lv + 1 < LEVELS) { s_unlocked = s_lv + 1; s_prefs.putUChar("unl", s_unlocked); }
}

static void update(float dt) {
  s_t += dt;
  s_modeT += dt;
  s_shake = fmaxf(0, s_shake - dt * 12);
  if (s_noteT > 0) s_noteT -= dt;
  if (s_flash > 0) s_flash -= dt;
  if (s_mode != Mode::Play) return;
  s_clock += dt;
  switch (LV[s_lv].kind) {
    case K_LOOM: s_shuttleT += dt; break;
    case K_MAZE: updateMaze(dt); break;
    case K_BOSS: updateBoss(dt); break;
    case K_YARN:
      updateYarn(dt);
      if (LV[s_lv].n == 3 && s_mode == Mode::Play) {
        if (fmodf(s_t, 1.5f) < dt) { s_shake = 3; RunAudio::fx(RunAudio::Thunder); }
        if (s_clock > s_ylimit) { s_lostWhy = "The roof came down."; setMode(Mode::Lost); }
      }
      break;
    case K_END: break;
  }
  if (s_mode == Mode::Won) levelDone();
}

static void render() {
  target(s_frame, FW, FH);
  setScale(K);
  switch (s_mode) {
    case Mode::Title: drawTitle(); break;
    case Mode::Story: drawStory(); break;
    default:
      switch (LV[s_lv].kind) {
        case K_LOOM: drawLoom(); break;
        case K_MAZE: drawMazeView(); break;
        case K_BOSS: drawBoss(); break;
        case K_YARN: drawYarnLevel(); break;
        case K_END: drawStory(); break;
      }
      if (LV[s_lv].kind != K_END) drawHud();
      break;
  }
  setScale(1);
  if (s_noteT > 0 && s_mode == Mode::Play) txtCS(W / 2, H - 22, s_note, 1, rgb(0xfff0d8));

  setScale(K);
  char b[48];
  switch (s_mode) {
    case Mode::Paused:
      card(110, 80, W - 220, 116);
      setScale(1);
      txtC(W / 2, 104, "PAUSED", 2, rgb(0xfff4e0));
      txtC(W / 2, 142, "SELECT go on", 1, rgb(C_INK));
      txtC(W / 2, 160, "UP menu", 1, rgb(C_INK));
      break;
    case Mode::Won:
      if (LV[s_lv].kind == K_END) {
        card(10, 8, W - 20, H - 16);
        vignette(V_STARS, 82, 140, 58);
        setScale(1);
        txtC(296, 28, "FINALE", 1, rgb(C_INK));
        txtC(296, 46, LV[s_lv].name, 1, rgb(0xfff4e0));
        wrapC(296, 72, LV[s_lv].story, 24, rgb(C_INK));
        txtC(296, 242, "SELECT menu", 1, rgb(0x5a2410));
        break;
      }
      if (s_modeT < 0.6f) break;
      card(70, 60, W - 140, 156);
      setScale(1);
      if (LV[s_lv].kind == K_BOSS) {
        txtC(W / 2, 82, "THE MINOTAUR FALLS", 1, rgb(0xfff4e0));
        wrapC(W / 2, 104, "He whispers his true name: Asterion, the starry one.", 26, rgb(C_INK));
      } else if (LV[s_lv].kind == K_LOOM) {
        txtC(W / 2, 82, "THE THREAD IS SPUN", 1, rgb(0xfff4e0));
        snprintf(b, sizeof(b), "%d of %d true throws", s_hits, THROWS);
        txtC(W / 2, 110, b, 1, rgb(C_INK));
        snprintf(b, sizeof(b), "Thread for Act I: x%.2f", s_spin);
        txtC(W / 2, 130, b, 1, rgb(C_INK));
      } else {
        txtC(W / 2, 82, LV[s_lv].kind == K_YARN ? "THEY FOLLOW THE THREAD" : "ONWARD", 1, rgb(0xfff4e0));
        snprintf(b, sizeof(b), "%d.%ds", (int)s_clock, (int)(s_clock * 10) % 10);
        txtC(W / 2, 112, b, 2, rgb(C_INK));
      }
      txtC(W / 2, 184, "SELECT next  UP menu", 1, rgb(0x5a2410));
      break;
    case Mode::Lost:
      if (s_modeT < 0.5f) break;
      card(80, 70, W - 160, 132);
      setScale(1);
      txtC(W / 2, 94, "NOT THIS TIME", 1, rgb(0xfff4e0));
      wrapC(W / 2, 118, s_lostWhy, 24, rgb(C_INK));
      txtC(W / 2, 168, "SELECT again  UP menu", 1, rgb(0x5a2410));
      break;
    default: break;
  }
  setScale(1);
  HalfRes::present(s_panel, s_gfx->flipped(), s_frame);
}

// ---------------------------------------------------------------------------
// Bench self-play: a bot plays each level many times on fresh labyrinths
// (USB command only), to show every one can be finished.
// ---------------------------------------------------------------------------

// Act I: walk the shortest way to the stairs (doors count as open; it waits
// at a shut one). It doesn't dodge the Minotaur.
static void botMaze() {
  if (s_move < 1) return;
  static int16_t dist[MAXTH * MAXTW], q[MAXTH * MAXTW];
  for (int i = 0; i < s_tw * s_th; i++) dist[i] = -1;
  int h = 0, t = 0;
  dist[s_gy * s_tw + s_gx] = 0;
  q[t++] = s_gy * s_tw + s_gx;
  while (h < t) {
    int c = q[h++], x = c % s_tw, y = c / s_tw;
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (nx < 0 || ny < 0 || nx >= s_tw || ny >= s_th || s_tile[ny][nx] == T_WALL) continue;
      int nc = ny * s_tw + nx;
      if (dist[nc] >= 0) continue;
      dist[nc] = dist[c] + 1;
      q[t++] = nc;
    }
  }
  int here = dist[s_py * s_tw + s_px];
  for (int d = 0; d < 4; d++) {
    int nx = s_px + DX[d], ny = s_py + DY[d];
    if (nx < 0 || ny < 0 || nx >= s_tw || ny >= s_th) continue;
    if (dist[ny * s_tw + nx] == here - 1) { if (passable(nx, ny)) tryMove(d); return; }
  }
}

// Act II: dodge falling stones, leap the charge, strike while he's dazed.
static void botBoss() {
  s_botHeld = 0;
  float d = s_bx - s_hx;
  bool ground = s_hy >= GROUND - 0.5f;
  for (const Stone &s : s_stones)
    if (s.on && s.t < 0.8f && fabsf(s.x - s_hx) < 24) {
      bool goLeft = s.x > s_hx ? s_hx > ARENA_L + 40 : s_hx > ARENA_R - 40;
      s_botHeld = 1 << (int)(goLeft ? Key::Left : Key::Right);
      return;
    }
  if (s_bull == Bull::Charge) {
    bool toward = (s_bface > 0) == (d < 0);
    if (toward && ground && fabsf(d) < 30 + s_bspeed * 0.2f) bossAction();
  } else if (s_bull == Bull::Dazed && !s_bstruck) {
    if (fabsf(d) < 50 && ground) bossAction();
    else s_botHeld = 1 << (int)(d > 0 ? Key::Right : Key::Left);
  } else if (s_bull == Bull::Paw && fabsf(d) < 110) { // too close to leap in time: back off
    bool left = d > 0 ? s_hx > ARENA_L + 30 : s_hx > ARENA_R - 30;
    s_botHeld = 1 << (int)(left ? Key::Left : Key::Right);
  }
}

// Act III: roll along the one path to the gate; wind back if off it; stop
// and wait while a lantern sweeps the next cells.
static void botYarn() {
  if (s_ymove < 1) return;
  int head = s_yarn[s_ylen - 1];
  if (head == s_yexit) { s_ywant = s_yexitDir; s_yrewind = false; return; }
  static int8_t toward[YW * YH];
  static int16_t q[YW * YH];
  memset(toward, -1, sizeof(toward));
  int h = 0, t = 0;
  toward[s_yexit] = 4;
  q[t++] = s_yexit;
  while (h < t) {
    int c = q[h++], x = c % YW, y = c / YW;
    for (int d = 0; d < 4; d++) {
      if (!(s_yopen[y][x] & BIT[d])) continue;
      int nc = (y + DY[d]) * YW + x + DX[d];
      if (toward[nc] >= 0) continue;
      toward[nc] = (d + 2) & 3; // from nc, step this way to get nearer
      q[t++] = nc;
    }
  }
  int d = toward[head];
  int nx = head % YW + DX[d], ny = head / YW + DY[d];
  if (s_yused[ny][nx]) { s_yrewind = true; return; } // off the path: wind back
  s_yrewind = false;
  for (const Guard &g : s_guards) {
    if (!g.on || g.y != ny || head / YW == g.y) continue;
    float gap = nx - g.x; // wait while his lantern is on, or coming onto, the cell
    if (fabsf(gap) < 1.5f || (gap * g.dir > 0 && fabsf(gap) < 2.5f)) { s_ywant = (s_ydir + 2) & 3; return; }
  }
  s_ywant = d;
}

void benchPlayAll() {
  const int RUNS = 12;
  float spin = s_spin;
  s_spin = 0.85f; // the least thread the loom can give
  s_bench = true;
  Serial.println("Ariadne self-play: every level, fresh labyrinths, least thread");
  for (int lv = 1; lv < LEVELS - 1; lv++) {
    int won = 0, lost = 0, stuck = 0;
    float tsum = 0, tmax = 0;
    const char *why = "";
    for (int r = 0; r < RUNS; r++) {
      s_lv = lv;
      beginLevel();
      int step = 0;
      while (s_mode == Mode::Play && step < 60 * 300) {
        switch (LV[lv].kind) {
          case K_MAZE: botMaze(); break;
          case K_BOSS: botBoss(); break;
          case K_YARN: botYarn(); break;
          default: break;
        }
        update(1.0f / 60);
        if (++step % 300 == 0) delay(1);
      }
      if (s_mode == Mode::Won) { won++; tsum += s_clock; tmax = fmaxf(tmax, s_clock); }
      else if (s_mode == Mode::Lost) { lost++; why = s_lostWhy; }
      else {
        stuck++;
        if (LV[lv].kind == K_YARN) {
          int head = s_yarn[s_ylen - 1];
          Serial.printf("    stuck: head %d,%d exit %d,%d len %d cost %d/%d rewind %d want %d dir %d", head % YW, head / YW,
                        s_yexit % YW, s_yexit / YW, s_ylen, yarnCost(), s_ybudget, s_yrewind, s_ywant, s_ydir);
          for (const Guard &g : s_guards) if (g.on) Serial.printf("  guard row %d x %d-%d", g.y, g.x0, g.x1);
          Serial.println();
        }
      }
    }
    Serial.printf("  %-2d %-22s won %2d/%d  lost %d  stuck %d  avg %.1fs max %.1fs  %s\n", lv, LV[lv].name, won, RUNS,
                  lost, stuck, won ? tsum / won : 0.0f, tmax, lost ? why : "");
  }
  s_botHeld = 0;
  s_bench = false;
  s_spin = spin;
  s_pick = s_unlocked;
  setMode(Mode::Title);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel) {
  if (s_active) return true;
  s_gfx = gfx;
  s_panel = panel;
  s_frame = (uint16_t *)heap_caps_malloc((size_t)FW * FH * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_frame) s_frame = (uint16_t *)heap_caps_malloc((size_t)FW * FH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_cand = (Candidate *)heap_caps_malloc(sizeof(Candidate) * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_frame || !s_cand) {
    heap_caps_free(s_frame); s_frame = nullptr;
    heap_caps_free(s_cand); s_cand = nullptr;
    Serial.println("Ariadne: not enough memory");
    return false;
  }
  s_rng = esp_random() | 1;
  s_prefs.begin("ariadne", false);
  s_unlocked = s_prefs.getUChar("unl", 0);
  if (s_unlocked >= LEVELS) s_unlocked = LEVELS - 1;
  s_spin = s_prefs.getUChar("spin", 100) / 100.0f;
  for (int i = 0; i < LEVELS; i++) {
    char k[4] = {'b', (char)('a' + i), 0};
    s_best[i] = s_prefs.getUInt(k, 0);
  }
  s_pick = s_unlocked;
  s_t = 0;
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
  heap_caps_free(s_cand); s_cand = nullptr;
  s_active = false;
}

bool active() { return s_active; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }
bool upSleepAllowed() { return s_mode != Mode::Play; }
const uint16_t *frameBuffer() { return s_frame; }
void benchUnlockAll() { s_unlocked = LEVELS - 1; }

void pause() {
  if (s_mode == Mode::Play) setMode(Mode::Paused);
}

static void steer(int d) {
  if (LV[s_lv].kind != K_YARN) return;
  s_ywant = d;
  s_yrewind = false;
}

void key(const InputEvent &ev) {
  if (!ev.pressed) {
    if (ev.key == Key::Down) s_downT = -1;
    return;
  }
  Kind kind = LV[s_lv].kind;
  switch (s_mode) {
    case Mode::Title:
      if (ev.repeat) break;
      if (ev.key == Key::Left && s_pick > 0) { s_pick--; s_t = 0; }
      else if (ev.key == Key::Right && s_pick < LEVELS - 1) { s_pick++; s_t = 0; }
      else if (ev.key == Key::Select && s_pick <= s_unlocked) { s_lv = s_pick; setMode(Mode::Story); }
      else if (ev.key == Key::Up) s_exit = true;
      break;
    case Mode::Story:
      if (ev.repeat) break;
      if (ev.key == Key::Select) beginLevel();
      else if (ev.key == Key::Up) { s_pick = s_lv; setMode(Mode::Title); }
      break;
    case Mode::Play:
      if (ev.key == Key::Down && !ev.repeat) s_downT = 0;
      if (kind == K_LOOM && ev.key == Key::Select && !ev.repeat) loomThrow();
      else if (kind == K_MAZE && ev.key == Key::Select && !ev.repeat) pause();
      else if (kind == K_MAZE && !ev.repeat && s_move >= 1) {
        if (ev.key == Key::Up) tryMove(0);
        else if (ev.key == Key::Right) tryMove(1);
        else if (ev.key == Key::Down) tryMove(2);
        else if (ev.key == Key::Left) tryMove(3);
      } else if (kind == K_BOSS) {
        if (ev.key == Key::Select && !ev.repeat) bossAction();
        else if (ev.key == Key::Down && !ev.repeat) pause();
      } else if (kind == K_YARN && !ev.repeat) {
        if (ev.key == Key::Up) steer(0);
        else if (ev.key == Key::Right) steer(1);
        else if (ev.key == Key::Down) steer(2);
        else if (ev.key == Key::Left) steer(3);
        else if (ev.key == Key::Select) { s_yrewind = !s_yrewind; if (s_yrewind) RunAudio::fx(RunAudio::PopSoft); }
      }
      break;
    case Mode::Paused:
      if (ev.repeat) break;
      if (ev.key == Key::Select) setMode(Mode::Play);
      else if (ev.key == Key::Up) { s_pick = s_lv; setMode(Mode::Title); }
      break;
    case Mode::Won:
      if (ev.repeat || s_modeT < 0.6f) break;
      if (kind == K_END) { if (ev.key == Key::Select || ev.key == Key::Up) { s_pick = s_lv; setMode(Mode::Title); } break; }
      if (ev.key == Key::Select) { s_lv = s_lv + 1 < LEVELS ? s_lv + 1 : s_lv; setMode(Mode::Story); }
      else if (ev.key == Key::Up) { s_pick = s_lv; setMode(Mode::Title); }
      break;
    case Mode::Lost:
      if (ev.repeat || s_modeT < 0.5f) break;
      if (ev.key == Key::Select) beginLevel();
      else if (ev.key == Key::Up) { s_pick = s_lv; setMode(Mode::Title); }
      break;
  }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  s_input = &input;
  static uint32_t last = 0;
  uint32_t now = millis();
  float dt = last ? (now - last) / 1000.0f : 0.02f;
  last = now;
  if (dt > 0.1f) dt = 0.1f;

  if (s_mode == Mode::Play) {
    Kind kind = LV[s_lv].kind;
    // Holding DOWN pauses at the loom and in the yarn halls (a tap still
    // steers). In the mazes DOWN walks, and SELECT pauses instead.
    if ((kind == K_LOOM || kind == K_YARN) && s_downT >= 0 && input.held(Key::Down)) {
      s_downT += dt;
      if (s_downT >= 0.8f) { s_downT = -1; setMode(Mode::Paused); }
    }
    // The wheel: turns the ball of thread, or walks Theseus in the arena.
    static int32_t wacc = 0;
    wacc += wheel;
    if (kind == K_YARN) {
      while (wacc >= 2) { wacc -= 2; steer((s_ywant + 1) & 3); }
      while (wacc <= -2) { wacc += 2; steer((s_ywant + 3) & 3); }
    } else if (kind == K_BOSS) {
      s_hx = clampf(s_hx + wacc * 6, ARENA_L + 10, ARENA_R - 10);
      if (wacc) s_hface = wacc > 0 ? 1 : -1;
      wacc = 0;
    } else {
      wacc = 0;
    }
    if (touch.justPressed()) {
      int16_t lx, ly;
      s_gfx->touchToLandscape(touch.x(), touch.y(), lx, ly);
      if (ly < HUD_H) pause();
      else if (kind == K_LOOM) loomThrow();
      else if (kind == K_BOSS) bossAction();
    }
  } else if (touch.justPressed()) {
    InputEvent tap = {Key::Select, true, false};
    key(tap);
  }

  static float acc = 0;
  acc += dt;
  while (acc >= 1.0f / 60) { update(1.0f / 60); acc -= 1.0f / 60; }
  render();
}

} // namespace Ariadne
