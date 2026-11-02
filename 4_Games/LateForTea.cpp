#include "LateForTea.h"
#include "Raster.h"
#include "RunAudio.h"
#include "HalfRes.h"
#include "Tenniel.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <font/glcdfont.h>

using namespace Raster;

namespace LateForTea {

// ---------------------------------------------------------------------------
// The view: a camera behind the bunny, looking down a winding garden path.
// World units are metres: x across, y up, z along the path.
// ---------------------------------------------------------------------------

static const int W = HalfRes::W, H = HalfRes::H, FW = HalfRes::FW, FH = HalfRes::FH;
static const float STEP = 1.0f / 60;
static const float HY = 112;           // horizon, screen px
static const float FOCAL = 300;        // screen px per metre at 1 m
static const float CAM_H = 2.2f;       // camera height
static const float CAM_BACK = 4.8f;    // camera distance behind the bunny
static const float FAR = 85;           // draw distance
static const float LANE_X[3] = {-1.3f, 0, 1.3f};
static const float PATH_HALF = 2.15f;
static const int SKY_ROWS = (int)(HY / 2);
static const int SKY_W = FW * 2;
static const float CRUISE = 9.5f, SPRINT = 11.6f;
static const int CHAPTERS = 10;
static const int CATCHES_TO_LOSE = 3;

// ---------------------------------------------------------------------------
// The ten places on the way to tea
// ---------------------------------------------------------------------------

enum Prop : uint8_t {
  PR_ROSES, PR_GARDENER, PR_FLAMINGO, PR_WICKET, PR_TREE, PR_MUSHROOM, PR_REEDS, PR_LILY,
  PR_TWISTY, PR_PEPPER, PR_CAULDRON, PR_ROOK, PR_PAWN, PR_LOBSTER, PR_PARASOL, PR_TEAPOT, PR_CHAIR, PR_HAT,
};
enum Fx : uint8_t { FX_NONE, FX_PETALS, FX_FIREFLIES, FX_SMOKE, FX_TEARS, FX_GRINS, FX_PEPPER };

struct Place {
  const char *name, *line1, *line2;
  uint32_t skyTop, skyLow, fog, side1, side2, path1, path2, edge, hills;
  Prop props[3];
  Fx fx;
  bool night, checker;
};

static const Place PLACES[CHAPTERS] = {
    {"The Garden of Roses", "Some roses are white, some red,", "and some are white, painted red.",
     0xa9dcff, 0xffe3f1, 0xffeef6, 0x8fd3a4, 0x86cc9c, 0xfdf0dc, 0xf7e6cc, 0xffb3c7, 0x7fc796,
     {PR_ROSES, PR_GARDENER, PR_ROSES}, FX_PETALS, false, false},
    {"The Cricket Pitch", "The Queen's court plays cricket,", "with flamingos for bats.",
     0x9fd8ff, 0xe8f6ff, 0xeef8ff, 0x8fd88f, 0x7fcc80, 0xe9f7d8, 0xd9efc4, 0xffffff, 0x8fcf9a,
     {PR_FLAMINGO, PR_WICKET, PR_FLAMINGO}, FX_NONE, false, false},
    {"The Firefly Forest", "The path goes into the wood,", "lit only by fireflies.",
     0x141a3a, 0x3a3270, 0x2a2850, 0x23413a, 0x1e3a33, 0x4a4466, 0x433d5e, 0x9ad9b5, 0x16302a,
     {PR_TREE, PR_TREE, PR_TREE}, FX_FIREFLIES, true, false},
    {"The Caterpillar's Mushrooms", "Who... are... YOU?", "asks a voice in the blue smoke.",
     0xc9b8ff, 0xffe0f0, 0xf2e6ff, 0x9bc9a0, 0x8fbf95, 0xf4e4d4, 0xebd8c4, 0xb5a6ff, 0x9ab8c8,
     {PR_MUSHROOM, PR_MUSHROOM, PR_MUSHROOM}, FX_SMOKE, false, false},
    {"The Pool of Tears", "A causeway over a pool", "of Alice's own tears.",
     0x9fd0ff, 0xd8f0ff, 0xe4f4ff, 0x6fb8e0, 0x64acd6, 0xd8d2c8, 0xcac4ba, 0xffffff, 0x7fb0d0,
     {PR_REEDS, PR_LILY, PR_REEDS}, FX_TEARS, false, false},
    {"The Cheshire Woods", "Most everyone's mad here,", "grins a cat who isn't there.",
     0x3a2460, 0xc77aa8, 0x6a4a80, 0x4a3a5a, 0x433452, 0xb89ab0, 0xab8ca4, 0xff9ed8, 0x2e2240,
     {PR_TWISTY, PR_TWISTY, PR_TWISTY}, FX_GRINS, true, false},
    {"The Duchess's Kitchen", "Too much pepper in the air -", "everyone is sneezing!",
     0xffc98f, 0xfff0d8, 0xfff2e0, 0xd8b088, 0xccA27a, 0xf4e2c8, 0xe9d4b6, 0xe8803a, 0xc89870,
     {PR_PEPPER, PR_CAULDRON, PR_PEPPER}, FX_PEPPER, false, false},
    {"The Looking-Glass Board", "Across the giant chessboard,", "past red and white pieces.",
     0xbfe0ff, 0xf6f0ff, 0xf4f0ff, 0x9fd0a0, 0x94c495, 0xffffff, 0xe85d75, 0xfff1a8, 0xa0c0d8,
     {PR_ROOK, PR_PAWN, PR_ROOK}, FX_NONE, false, true},
    {"The Lobster Quadrille", "Down by the sea the lobsters", "dance - and the seagulls nest.",
     0x8fd4ff, 0xe8f8ff, 0xeaf8ff, 0xf3dfaa, 0xeed59a, 0xfff4dc, 0xf7e9c8, 0x6fb8e0, 0x5aa8d8,
     {PR_LOBSTER, PR_PARASOL, PR_LOBSTER}, FX_NONE, false, false},
    {"The Mad Tea Party", "The table is set and the cookies", "wait. It's nearly four!",
     0xb5e0ff, 0xffe9f2, 0xfff0f6, 0x9edaa8, 0x92d09c, 0xfdf0dc, 0xf7e6cc, 0xff8fb1, 0x8fcf9a,
     {PR_TEAPOT, PR_CHAIR, PR_HAT}, FX_NONE, false, false},
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum Kind : uint8_t { CANDY, EGG, GOOSE };
enum class Mode : uint8_t { Title, Ready, Race, Paused, Won, Turtles, AliceWins, Late, Finale };

struct Thing { float z; uint8_t kind, lane, col; bool gone; };
struct Tortoise { float z, v, phase, laneF; uint8_t lane, col; };
struct Gull { bool on, yellow, used; float z, h, t; uint8_t lane; };
struct Popup { float x, y, t; char text[14]; uint32_t col; };

static const int MAX_THINGS = 130;
static const int TORTOISES = 4;

static bool s_active = false, s_exit = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static Input *s_input = nullptr;
static uint16_t *s_frame = nullptr, *s_sky = nullptr;
static Preferences s_prefs;

static Mode s_mode = Mode::Title;
static int s_ch = 0, s_unlocked = 0, s_pick = 0;
static const Place *E = &PLACES[0];
static float s_t = 0, s_modeT = 0, s_clock = 60, s_course = 700, s_timeLimit = 70;
static uint32_t s_seed = 1, s_rng = 1;

static float s_z = 0, s_v = CRUISE, s_laneF = 1, s_hopT = -1, s_puff = 1;
static int s_lane = 1;
static bool s_sprint = false;
static float s_boostV = 0, s_boostT = 0, s_stumbleT = 0, s_caughtT = 0, s_bumpT = 0;
static float s_carryT = -1, s_carryFrom = 0, s_carryBy = 0;
static bool s_carryYellow = true;
static float s_alice = -40;
static int s_catches = 0;

static Thing s_things[MAX_THINGS];
static int s_nThings = 0;
static Tortoise s_turtles[TORTOISES];
static Gull s_gull;
static float s_gullNext = 8;
static int s_lastTock = -1, s_place = 1;
static Popup s_pops[5];
static bool s_lineup = false; // bench: show the cast close up

// ---------------------------------------------------------------------------
// Noise (Perlin) for clouds and the far hills
// ---------------------------------------------------------------------------

static uint8_t PERM[512];
static void seedNoise(uint32_t seed) {
  for (int i = 0; i < 256; i++) PERM[i] = i;
  for (int i = 255; i > 0; i--) {
    seed = seed * 1664525u + 1013904223u;
    int j = (seed >> 8) % (i + 1);
    uint8_t t = PERM[i]; PERM[i] = PERM[j]; PERM[j] = t;
  }
  for (int i = 0; i < 256; i++) PERM[256 + i] = PERM[i];
}
static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float grad2(int h, float x, float y) {
  switch (h & 7) {
    case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
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
  for (int i = 0; i < oct; i++) { s += a * noise2(x * f, y * f); a *= 0.5f; f *= 2; }
  return s;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float frand() { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) / 16777216.0f; }
static uint32_t hashi(int a, int b) {
  uint32_t h = ((uint32_t)a * 73856093u) ^ ((uint32_t)b * 19349663u) ^ (uint32_t)(s_ch * 2654435761u);
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return h;
}
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static const uint32_t PASTEL[6] = {0xffb3c7, 0xb5e3ff, 0xfff1a8, 0xc9f2c4, 0xdcc3ff, 0xffd2a6};

// The path winds: its centre's x at distance z.
static float curve(float z) { return 3.2f * sinf(z * 0.011f + s_ch) + 1.4f * sinf(z * 0.029f); }
static float camZ() { return s_z - CAM_BACK; }
static float camXw() { return curve(camZ()) * 0.85f + curve(s_z) * 0.15f + (LANE_X[0] + (LANE_X[2] - LANE_X[0]) * s_laneF / 2) * 0.55f; }

// World -> screen. Returns false behind the camera / too far.
static bool project(float x, float y, float z, float &sx, float &sy, float &s) {
  float dz = z - camZ();
  if (dz < 0.6f || dz > FAR) return false;
  s = FOCAL / dz;
  sx = W / 2 + (x - camXw()) * s;
  sy = HY + (CAM_H - y) * s;
  return true;
}
static float laneX(float laneF, float z) { return curve(z) + LANE_X[0] + (LANE_X[2] - LANE_X[0]) * laneF / 2; }
static float fogK(float dz) { return clampf((dz - 20) / (FAR - 20), 0, 1); }

static void pop(float sx, float sy, const char *t, uint32_t col) {
  for (Popup &p : s_pops) {
    if (p.t > 0) continue;
    p.x = sx; p.y = sy; p.t = 1.1f; p.col = col;
    strlcpy(p.text, t, sizeof(p.text));
    return;
  }
}

// --- text ---------------------------------------------------------------------

static const uint16_t SHADOW = 0x4108;
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

// A Tenniel engraving, tinted, at frame position (fx, fy) - the storybook cards.
static void tenniel(const uint8_t *data, int w, int h, int fx, int fy, uint32_t ink, uint32_t paper) {
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

// ---------------------------------------------------------------------------
// Sky and ground
// ---------------------------------------------------------------------------

static void drawSky() {
  for (int y = 0; y < SKY_ROWS; y++) {
    float t = y / (float)(SKY_ROWS - 1);
    uint32_t base = mixHex(E->skyTop, E->skyLow, t);
    for (int x = 0; x < SKY_W; x++) {
      float a = x / (float)SKY_W * 6.2831853f;
      float n = fbm(cosf(a) * 2.0f + 10, sinf(a) * 2.0f + y * 0.07f, 4);
      float c = clampf((n + 0.08f) * 2.4f, 0, 1) * (1 - t * 0.5f);
      uint32_t cloud = E->night ? mixHex(E->skyTop, 0x8080c0, 0.5f) : 0xffffff;
      uint16_t px = mix(base, cloud, c * (E->night ? 0.35f : 0.85f));
      if (E->night && ((x * 7 + y * 13) % 97 == 0) && c < 0.2f) px = rgb(0xfff6d8); // stars
      s_sky[y * SKY_W + x] = px;
    }
  }
}

static void drawWorldBack() {
  // Sky, shifting as the path turns.
  float turn = curve(camZ() + 60) - camXw();
  int off = (((int)(turn * 6)) % SKY_W + SKY_W) % SKY_W;
  for (int y = 0; y < SKY_ROWS; y++) {
    uint16_t *dst = s_frame + y * FW;
    const uint16_t *src = s_sky + y * SKY_W;
    int first = SKY_W - off < FW ? SKY_W - off : FW;
    memcpy(dst, src + off, first * 2);
    if (first < FW) memcpy(dst + first, src, (FW - first) * 2);
  }
  // Far Perlin hills along the horizon.
  for (int fx = 0; fx < FW; fx++) {
    float h = fbm((fx * 2 + turn * 14) * 0.006f + s_ch * 3, s_ch * 1.7f, 3);
    int top = (int)((HY - 14 - 22 * (h + 0.5f)) / 2);
    uint16_t c = rgb(E->hills), c2 = mix(E->hills, E->fog, 0.45f);
    for (int y = top; y < (int)(HY / 2); y++) s_frame[y * FW + fx] = y < top + 2 ? c2 : c;
  }

  // The ground, row by row from the horizon down: grass (or water, sand...)
  // either side of the path, stripes passing underfoot to show the speed.
  float cx0 = camXw();
  for (int fy = (int)(HY / 2) + 1; fy < FH; fy++) {
    float sy = fy * 2 + 1;
    float dz = CAM_H * FOCAL / (sy - HY);
    float z = camZ() + dz;
    float s = FOCAL / dz;
    float fog = fogK(dz);
    int band = ((int)floorf(z / 3.0f)) & 1;
    uint16_t side = mix(band ? E->side1 : E->side2, E->fog, fog);
    float pc = W / 2 + (curve(z) - cx0) * s;
    float half = PATH_HALF * s;
    int l = (int)((pc - half) / 2), r = (int)((pc + half) / 2);
    span(fy, 0, FW, side);
    if (E->checker) {
      // The chessboard: squares across the path too.
      int cols = 6;
      for (int c = 0; c < cols; c++) {
        int a = (int)((pc - half + c * 2 * half / cols) / 2), b = (int)((pc - half + (c + 1) * 2 * half / cols) / 2);
        uint32_t col = ((c + band) & 1) ? E->path1 : E->path2;
        span(fy, a, b, mix(col, E->fog, fog));
      }
    } else {
      span(fy, l, r, mix(band ? E->path1 : E->path2, E->fog, fog));
    }
    int edge = (int)(0.12f * s / 2) + 1;
    span(fy, l - edge, l, mix(E->edge, E->fog, fog));
    span(fy, r, r + edge, mix(E->edge, E->fog, fog));
    // Lane markings: dashes of petals between the paths.
    if (((int)floorf(z / 1.5f) & 1) && dz < 45) {
      for (int k = 0; k < 2; k++) {
        float lx = pc + (LANE_X[k] + LANE_X[k + 1]) / 2 * s;
        int w = (int)(0.05f * s / 2) + 1;
        span(fy, (int)(lx / 2) - w / 2, (int)(lx / 2) + w / 2 + 1, mix(E->edge, E->fog, fog), 150);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Things to draw at (sx, sy) = ground point on screen, s = px per metre
// ---------------------------------------------------------------------------

static void groundShadow(float sx, float sy, float rx, float s) { ellipse(sx, sy, rx * s, rx * s * 0.28f, rgb(0x40304a), 70); }

static void drawRoseBush(float x, float y, float s, uint32_t h) {
  groundShadow(x, y, 0.9f, s);
  for (int i = 0; i < 5; i++) circle(x + (i - 2) * 0.32f * s, y - (0.55f + (i & 1) * 0.25f) * s, 0.42f * s, rgb(i & 1 ? 0x3f8f5a : 0x4a9a63));
  for (int i = 0; i < 6; i++) {
    float rx = x + ((int)((h >> (i * 3)) % 9) - 4) * 0.17f * s, ry = y - (0.5f + ((h >> (i * 2)) % 5) * 0.12f) * s;
    int kind = (h >> (i * 4)) % 3; // white, red, or white being painted red
    circle(rx, ry, 0.12f * s, rgb(kind == 1 ? 0xd8233c : 0xfffaf2));
    if (kind == 2) circle(rx + 0.05f * s, ry, 0.08f * s, rgb(0xd8233c));
  }
}

static void drawCardMan(float x, float y, float s, uint32_t suit, bool brush) {
  groundShadow(x, y, 0.4f, s);
  rect(x - 0.12f * s, y - 0.3f * s, 0.06f * s, 0.3f * s, rgb(0x303040));
  rect(x + 0.06f * s, y - 0.3f * s, 0.06f * s, 0.3f * s, rgb(0x303040));
  roundRect(x - 0.32f * s, y - 1.2f * s, 0.64f * s, 0.9f * s, 0.06f * s, rgb(0xffffff));
  for (int i = 0; i < 3; i++) circle(x, y - (1.05f - i * 0.28f) * s, 0.07f * s, rgb(suit));
  circle(x, y - 1.35f * s, 0.17f * s, rgb(0xf6d2b8));
  if (brush) {
    line(x + 0.3f * s, y - 0.9f * s, x + 0.75f * s, y - 1.15f * s, 0.05f * s, rgb(0x9a6a3a));
    circle(x + 0.78f * s, y - 1.17f * s, 0.07f * s, rgb(0xd8233c));
  }
}

static void drawFlamingoBat(float x, float y, float s, uint32_t h) {
  drawCardMan(x, y, s, (h & 1) ? 0x202020 : 0xd8233c, false);
  // The flamingo, held by the legs, swung like a bat.
  float sw = sinf(s_t * 3 + h) * 0.4f;
  float hx = x + 0.35f * s, hy = y - 0.9f * s;
  float ex = hx + cosf(-0.6f + sw) * 0.9f * s, ey = hy + sinf(-0.6f + sw) * 0.9f * s;
  line(hx, hy, ex, ey, 0.04f * s, rgb(0xf08a9a));
  ellipse(ex, ey, 0.28f * s, 0.16f * s, rgb(0xff9ab0));
  line(ex, ey, ex + 0.1f * s, ey + 0.45f * s, 0.05f * s, rgb(0xff9ab0));
  circle(ex + 0.1f * s, ey + 0.5f * s, 0.08f * s, rgb(0xff9ab0));
}

static void drawWicket(float x, float y, float s) {
  for (int i = -1; i <= 1; i++) rect(x + i * 0.12f * s - 0.025f * s, y - 0.75f * s, 0.05f * s, 0.75f * s, rgb(0xf3e3c3));
  rect(x - 0.17f * s, y - 0.78f * s, 0.34f * s, 0.04f * s, rgb(0xd9b48a));
  circle(x + 0.5f * s, y - 0.12f * s, 0.12f * s, rgb(0x8a6a50)); // a hedgehog for the ball
}

static void drawTree(float x, float y, float s, uint32_t h) {
  groundShadow(x, y, 1.0f, s);
  rect(x - 0.15f * s, y - 1.6f * s, 0.3f * s, 1.6f * s, rgb(E->night ? 0x3a2a3a : 0x6a4a3a));
  uint32_t g = E->night ? 0x1f4a3e : 0x3f8f5a;
  circle(x, y - 2.2f * s, 0.9f * s, rgb(g));
  circle(x - 0.5f * s, y - 1.8f * s, 0.6f * s, rgb(mixHex(g, 0x000000, 0.15f)));
  circle(x + 0.55f * s, y - 1.9f * s, 0.65f * s, rgb(mixHex(g, 0xffffff, 0.08f)));
  if (E->fx == FX_FIREFLIES)
    for (int i = 0; i < 3; i++) {
      float a = s_t * (0.8f + i * 0.3f) + h + i * 2;
      float fx = x + cosf(a) * 0.9f * s, fy = y - (1.6f + sinf(a * 1.3f) * 0.6f) * s;
      circle(fx, fy, 0.12f * s, rgb(0xd8ff7a), 90);
      circle(fx, fy, 0.05f * s, rgb(0xf8ffc0));
    }
}

static void drawMushroom(float x, float y, float s, uint32_t h) {
  groundShadow(x, y, 0.9f, s);
  float k = 0.8f + (h % 5) * 0.12f;
  roundRect(x - 0.18f * s * k, y - 1.2f * s * k, 0.36f * s * k, 1.2f * s * k, 0.1f * s, rgb(0xfdf3e3));
  uint32_t cap = (h >> 4) & 1 ? 0xe85d75 : 0xb5a6ff;
  ellipse(x, y - 1.25f * s * k, 0.95f * s * k, 0.45f * s * k, rgb(cap));
  for (int i = 0; i < 4; i++) circle(x + (i - 1.5f) * 0.4f * s * k, y - (1.35f + (i & 1) * 0.12f) * s * k, 0.09f * s * k, rgb(0xffffff));
}

static void drawReeds(float x, float y, float s, uint32_t h) {
  for (int i = 0; i < 5; i++) {
    float rx = x + (i - 2) * 0.15f * s, top = y - (0.9f + ((h >> i) % 4) * 0.15f) * s;
    line(rx, y, rx + 0.05f * s, top, 0.04f * s, rgb(0x5f9a5a));
    ellipse(rx + 0.05f * s, top, 0.04f * s, 0.12f * s, rgb(0x8a5a3a));
  }
}

static void drawLily(float x, float y, float s) {
  ellipse(x, y, 0.5f * s, 0.15f * s, rgb(0x5fb070));
  circle(x + 0.1f * s, y - 0.1f * s, 0.12f * s, rgb(0xffc0d8));
}

static void drawTwisty(float x, float y, float s, uint32_t h) {
  groundShadow(x, y, 0.8f, s);
  line(x, y, x - 0.2f * s, y - 1.0f * s, 0.22f * s, rgb(0x3a2a4a));
  line(x - 0.2f * s, y - 1.0f * s, x + 0.3f * s, y - 1.8f * s, 0.16f * s, rgb(0x3a2a4a));
  line(x + 0.3f * s, y - 1.8f * s, x + 0.9f * s, y - 2.0f * s, 0.1f * s, rgb(0x3a2a4a));
  line(x - 0.1f * s, y - 1.4f * s, x - 0.7f * s, y - 1.9f * s, 0.1f * s, rgb(0x3a2a4a));
  // A grin with no cat, fading in and out.
  float a = 0.5f + 0.5f * sinf(s_t * 1.3f + h);
  if (a > 0.3f) {
    uint8_t al = (uint8_t)(a * 230);
    ellipse(x + 0.5f * s, y - 2.3f * s, 0.35f * s, 0.12f * s, rgb(0xffffff), al);
    ellipse(x + 0.5f * s, y - 2.36f * s, 0.32f * s, 0.08f * s, rgb(0x6a2a6a), al);
    circle(x + 0.35f * s, y - 2.6f * s, 0.06f * s, rgb(0xffe86a), al);
    circle(x + 0.65f * s, y - 2.6f * s, 0.06f * s, rgb(0xffe86a), al);
  }
}

static void drawPepper(float x, float y, float s) {
  groundShadow(x, y, 0.5f, s);
  roundRect(x - 0.35f * s, y - 1.3f * s, 0.7f * s, 1.3f * s, 0.15f * s, rgb(0xfdf8f0));
  roundRect(x - 0.3f * s, y - 1.55f * s, 0.6f * s, 0.3f * s, 0.12f * s, rgb(0xb0b0bc));
  for (int i = 0; i < 3; i++) circle(x + (i - 1) * 0.15f * s, y - 1.48f * s, 0.03f * s, rgb(0x404048));
  rect(x - 0.35f * s, y - 0.7f * s, 0.7f * s, 0.12f * s, rgb(0xe8803a));
}

static void drawCauldron(float x, float y, float s) {
  groundShadow(x, y, 0.7f, s);
  ellipse(x, y - 0.45f * s, 0.6f * s, 0.45f * s, rgb(0x3a3a44));
  ellipse(x, y - 0.8f * s, 0.55f * s, 0.12f * s, rgb(0xe8a060));
  for (int i = 0; i < 2; i++) circle(x + (i ? 0.2f : -0.15f) * s, y - (1.0f + fmodf(s_t * 0.6f + i * 0.5f, 1.0f)) * s, 0.12f * s, rgb(0xffffff), 120);
}

static void drawChess(float x, float y, float s, bool rook, uint32_t h) {
  uint32_t c = (h & 1) ? 0xe85d75 : 0xfff8f8;
  groundShadow(x, y, 0.6f, s);
  roundRect(x - 0.5f * s, y - 0.25f * s, 1.0f * s, 0.25f * s, 0.08f * s, rgb(c));
  if (rook) {
    rect(x - 0.32f * s, y - 1.5f * s, 0.64f * s, 1.3f * s, rgb(c));
    for (int i = -1; i <= 1; i++) rect(x + i * 0.24f * s - 0.08f * s, y - 1.75f * s, 0.16f * s, 0.28f * s, rgb(c));
  } else {
    float body[] = {x - 0.3f * s, y - 0.2f * s, x + 0.3f * s, y - 0.2f * s, x + 0.12f * s, y - 0.9f * s, x - 0.12f * s, y - 0.9f * s};
    poly(body, 4, rgb(c));
    circle(x, y - 1.05f * s, 0.24f * s, rgb(c));
  }
}

static void drawLobster(float x, float y, float s) {
  groundShadow(x, y, 0.5f, s);
  float d = sinf(s_t * 4) * 0.15f;
  ellipse(x, y - 0.55f * s, 0.22f * s, 0.45f * s, rgb(0xe8503a));
  line(x - 0.15f * s, y - 0.8f * s, x - 0.45f * s, y - (1.1f + d) * s, 0.06f * s, rgb(0xe8503a));
  line(x + 0.15f * s, y - 0.8f * s, x + 0.45f * s, y - (1.1f - d) * s, 0.06f * s, rgb(0xe8503a));
  circle(x - 0.47f * s, y - (1.15f + d) * s, 0.1f * s, rgb(0xe8503a));
  circle(x + 0.47f * s, y - (1.15f - d) * s, 0.1f * s, rgb(0xe8503a));
  line(x - 0.1f * s, y - 0.15f * s, x - 0.2f * s, y, 0.04f * s, rgb(0xe8503a));
  line(x + 0.1f * s, y - 0.15f * s, x + 0.2f * s, y, 0.04f * s, rgb(0xe8503a));
}

static void drawParasol(float x, float y, float s, uint32_t h) {
  line(x, y, x, y - 1.6f * s, 0.05f * s, rgb(0xfdfdfd));
  float top[] = {x - 0.9f * s, y - 1.5f * s, x, y - 1.95f * s, x + 0.9f * s, y - 1.5f * s};
  poly(top, 3, rgb(PASTEL[h % 6]));
}

static void drawTeapot(float x, float y, float s, uint32_t h) {
  groundShadow(x, y, 0.6f, s);
  uint32_t c = PASTEL[h % 6];
  ellipse(x, y - 0.5f * s, 0.55f * s, 0.45f * s, rgb(c));
  line(x + 0.5f * s, y - 0.5f * s, x + 0.85f * s, y - 0.8f * s, 0.1f * s, rgb(c));
  circle(x, y - 0.98f * s, 0.1f * s, rgb(mixHex(c, 0x000000, 0.2f)));
  ellipse(x - 0.55f * s, y - 0.5f * s, 0.12f * s, 0.2f * s, rgb(mixHex(c, 0x000000, 0.2f)));
}

static void drawChair(float x, float y, float s, uint32_t h) {
  uint32_t c = PASTEL[(h >> 3) % 6];
  rect(x - 0.35f * s, y - 0.55f * s, 0.7f * s, 0.1f * s, rgb(c));
  rect(x - 0.35f * s, y - 1.3f * s, 0.1f * s, 1.3f * s, rgb(c));
  rect(x + 0.25f * s, y - 0.55f * s, 0.1f * s, 0.55f * s, rgb(c));
  roundRect(x - 0.35f * s, y - 1.35f * s, 0.7f * s, 0.25f * s, 0.1f * s, rgb(c));
}

static void drawHat(float x, float y, float s) {
  groundShadow(x, y, 0.6f, s);
  rect(x - 0.4f * s, y - 1.1f * s, 0.8f * s, 1.0f * s, rgb(0x5aa86a));
  ellipse(x, y - 0.1f * s, 0.65f * s, 0.15f * s, rgb(0x4a9858));
  rect(x - 0.4f * s, y - 0.45f * s, 0.8f * s, 0.15f * s, rgb(0xe85d75));
  rect(x + 0.1f * s, y - 0.75f * s, 0.25f * s, 0.18f * s, rgb(0xfffaf0)); // 10/6
}

static void drawProp(Prop p, float x, float y, float s, uint32_t h) {
  switch (p) {
    case PR_ROSES: drawRoseBush(x, y, s, h); break;
    case PR_GARDENER: drawCardMan(x, y, s, (h & 1) ? 0x202020 : 0xd8233c, true); break;
    case PR_FLAMINGO: drawFlamingoBat(x, y, s, h); break;
    case PR_WICKET: drawWicket(x, y, s); break;
    case PR_TREE: drawTree(x, y, s, h); break;
    case PR_MUSHROOM: drawMushroom(x, y, s, h); break;
    case PR_REEDS: drawReeds(x, y, s, h); break;
    case PR_LILY: drawLily(x, y, s); break;
    case PR_TWISTY: drawTwisty(x, y, s, h); break;
    case PR_PEPPER: drawPepper(x, y, s); break;
    case PR_CAULDRON: drawCauldron(x, y, s); break;
    case PR_ROOK: drawChess(x, y, s, true, h); break;
    case PR_PAWN: drawChess(x, y, s, false, h); break;
    case PR_LOBSTER: drawLobster(x, y, s); break;
    case PR_PARASOL: drawParasol(x, y, s, h); break;
    case PR_TEAPOT: drawTeapot(x, y, s, h); break;
    case PR_CHAIR: drawChair(x, y, s, h); break;
    case PR_HAT: drawHat(x, y, s); break;
  }
}

// --- characters ---------------------------------------------------------------

// The White Rabbit, seen from behind: waistcoat, white tail, tall ears.
static void drawBunny(float x, float y, float s, float run, float lift) {
  float hop = fabsf(sinf(run)) * 0.08f * s;
  groundShadow(x, y, 0.4f, s * (1 - clampf(lift / (2 * s), 0, 0.6f)));
  float by = y - hop - lift;
  float k = sinf(run);
  ellipse(x - 0.14f * s, by - (0.05f + (k > 0 ? 0.06f : 0)) * s, 0.1f * s, 0.07f * s, rgb(0xeeeef6)); // feet
  ellipse(x + 0.14f * s, by - (0.05f + (k < 0 ? 0.06f : 0)) * s, 0.1f * s, 0.07f * s, rgb(0xeeeef6));
  ellipse(x, by - 0.32f * s, 0.24f * s, 0.25f * s, rgb(0xfbfbff));            // body
  ellipse(x, by - 0.36f * s, 0.2f * s, 0.18f * s, rgb(0xd23a52));             // waistcoat back
  rect(x - 0.01f * s, by - 0.5f * s, 0.02f * s, 0.3f * s, rgb(0xa82a40));     // its seam
  circle(x, by - 0.16f * s, 0.08f * s, rgb(0xffffff));                       // tail
  circle(x, by - 0.62f * s, 0.17f * s, rgb(0xfbfbff));                       // head
  float e = sinf(run * 0.5f) * 0.04f;
  ellipse(x - 0.08f * s - e * s, by - 0.92f * s, 0.055f * s, 0.2f * s, rgb(0xfbfbff)); // ears
  ellipse(x + 0.08f * s + e * s, by - 0.92f * s, 0.055f * s, 0.2f * s, rgb(0xfbfbff));
  ellipse(x - 0.08f * s - e * s, by - 0.92f * s, 0.025f * s, 0.14f * s, rgb(0xf6e4ec));
  ellipse(x + 0.08f * s + e * s, by - 0.92f * s, 0.025f * s, 0.14f * s, rgb(0xf6e4ec));
  float arm = k * 0.08f * s;
  ellipse(x - 0.25f * s, by - 0.35f * s + arm, 0.06f * s, 0.1f * s, rgb(0xfbfbff)); // arms swinging
  ellipse(x + 0.25f * s, by - 0.35f * s - arm, 0.06f * s, 0.1f * s, rgb(0xfbfbff));
}

// A six-sided shell plate.
static void hexPlate(float cx, float cy, float rx, float ry, uint16_t c) {
  float p[12];
  for (int i = 0; i < 6; i++) {
    float a = i * 1.0472f + 0.5236f;
    p[i * 2] = cx + cosf(a) * rx;
    p[i * 2 + 1] = cy + sinf(a) * ry;
  }
  poly(p, 6, c);
}

// A tortoise on a skateboard, seen from behind: domed shell with plates,
// stubby legs gripping the board, a little helmet and a scarf flying back.
static void drawTortoise(const Tortoise &t, float x, float y, float s) {
  static const uint32_t SHELL[4] = {0x6fbf73, 0xb08fd8, 0xe8a070, 0x6fb0e0};
  static const uint32_t GEAR[4] = {0xffb3c7, 0xfff1a8, 0xb5e3ff, 0xffd2a6};
  uint32_t c = SHELL[t.col & 3], gear = GEAR[t.col & 3];
  uint32_t skin = 0x9ccc7a, skinDark = 0x7aa85a;
  groundShadow(x, y, 0.5f, s);
  float wob = sinf(s_t * 9 + t.phase) * 0.015f * s;

  // Skateboard: deck with kicktails, wheels.
  circle(x - 0.24f * s, y - 0.06f * s, 0.065f * s, rgb(gear));
  circle(x + 0.24f * s, y - 0.06f * s, 0.065f * s, rgb(gear));
  circle(x - 0.24f * s, y - 0.06f * s, 0.025f * s, rgb(0x6b6b78));
  circle(x + 0.24f * s, y - 0.06f * s, 0.025f * s, rgb(0x6b6b78));
  roundRect(x - 0.36f * s, y - 0.17f * s, 0.72f * s, 0.07f * s, 0.035f * s, rgb(0x3a3a48));
  roundRect(x - 0.36f * s, y - 0.19f * s, 0.72f * s, 0.04f * s, 0.02f * s, rgb(0xe85d75));

  // Stubby legs gripping the board.
  for (int i = -1; i <= 1; i += 2) {
    ellipse(x + i * 0.27f * s, y - 0.22f * s, 0.075f * s, 0.06f * s, rgb(skin));
    ellipse(x + i * 0.17f * s, y - 0.21f * s, 0.07f * s, 0.055f * s, rgb(skinDark));
  }
  circle(x, y - 0.2f * s, 0.035f * s, rgb(skinDark)); // tail

  // Head over the top of the shell, in a helmet, scarf streaming behind.
  float hy = y - 0.74f * s + wob;
  float sw = sinf(s_t * 11 + t.phase) * 0.06f * s;
  float scarf[] = {x - 0.05f * s, hy + 0.1f * s, x + 0.05f * s, hy + 0.1f * s,
                   x + 0.2f * s + sw, hy + 0.3f * s, x + 0.1f * s + sw, hy + 0.33f * s};
  poly(scarf, 4, rgb(mixHex(gear, 0xe85d75, 0.45f)));
  circle(x, hy + 0.04f * s, 0.11f * s, rgb(skin));
  ellipse(x, hy - 0.02f * s, 0.13f * s, 0.1f * s, rgb(gear));       // helmet
  rect(x - 0.13f * s, hy - 0.01f * s, 0.26f * s, 0.025f * s, rgb(0xffffff));

  // The shell: rim, dome, plates, a highlight.
  float sy = y - 0.42f * s + wob;
  ellipse(x, sy + 0.07f * s, 0.4f * s, 0.2f * s, rgb(mixHex(c, 0x000000, 0.35f)));
  ellipse(x, sy, 0.38f * s, 0.27f * s, rgb(c));
  uint16_t plate = rgb(mixHex(c, 0xffffff, 0.22f)), seam = rgb(mixHex(c, 0x000000, 0.25f));
  hexPlate(x, sy - 0.05f * s, 0.11f * s, 0.09f * s, seam);
  hexPlate(x, sy - 0.05f * s, 0.09f * s, 0.075f * s, plate);
  for (int i = -1; i <= 1; i += 2) {
    hexPlate(x + i * 0.2f * s, sy, 0.1f * s, 0.085f * s, seam);
    hexPlate(x + i * 0.2f * s, sy, 0.08f * s, 0.07f * s, plate);
  }
  hexPlate(x, sy + 0.12f * s, 0.09f * s, 0.06f * s, seam);
  hexPlate(x, sy + 0.12f * s, 0.07f * s, 0.045f * s, plate);
  ellipse(x - 0.12f * s, sy - 0.16f * s, 0.1f * s, 0.04f * s, rgb(0xffffff), 110);
}

// A Canada goose standing on the path facing him: brown body, long black
// neck, white cheek strap, black beak - wings up when it honks. Nothing
// like the (always flying, white-headed) seagulls.
static void drawGoose(float x, float y, float s, bool honk) {
  groundShadow(x, y, 0.42f, s);
  line(x - 0.1f * s, y - 0.25f * s, x - 0.12f * s, y, 0.05f * s, rgb(0x2a2a2a)); // dark legs
  line(x + 0.1f * s, y - 0.25f * s, x + 0.12f * s, y, 0.05f * s, rgb(0x2a2a2a));
  ellipse(x - 0.12f * s, y, 0.08f * s, 0.03f * s, rgb(0x2a2a2a));
  ellipse(x + 0.12f * s, y, 0.08f * s, 0.03f * s, rgb(0x2a2a2a));
  // Wings: tucked, or flung up and out when honking.
  float up = honk ? 0.35f : 0;
  float wl[] = {x - 0.2f * s, y - 0.5f * s, x - (0.45f + up * 0.4f) * s, y - (0.42f + up) * s, x - 0.32f * s, y - 0.28f * s};
  float wr[] = {x + 0.2f * s, y - 0.5f * s, x + (0.45f + up * 0.4f) * s, y - (0.42f + up) * s, x + 0.32f * s, y - 0.28f * s};
  poly(wl, 3, rgb(0x6a5a4a));
  poly(wr, 3, rgb(0x6a5a4a));
  ellipse(x, y - 0.4f * s, 0.3f * s, 0.24f * s, rgb(0x8a7a66));                  // brown-grey body
  ellipse(x, y - 0.33f * s, 0.2f * s, 0.14f * s, rgb(0xe8e0d4));                 // pale breast
  rect(x - 0.055f * s, y - 1.05f * s, 0.11f * s, 0.55f * s, rgb(0x1e1e22));      // long black neck
  ellipse(x, y - 1.1f * s, 0.11f * s, 0.12f * s, rgb(0x1e1e22));                 // head
  // The white chinstrap: under the chin and up both cheeks, black crown above.
  ellipse(x, y - 1.04f * s, 0.105f * s, 0.07f * s, rgb(0xffffff));
  ellipse(x, y - 1.15f * s, 0.105f * s, 0.075f * s, rgb(0x1e1e22));
  rect(x - 0.03f * s, y - 1.12f * s, 0.06f * s, 0.09f * s, rgb(0x1e1e22));      // black between the cheeks
  ellipse(x, y - (honk ? 1.04f : 1.06f) * s, 0.035f * s, (honk ? 0.05f : 0.03f) * s, rgb(0x101010)); // beak
  circle(x - 0.055f * s, y - 1.15f * s, 0.016f * s, rgb(0xffffff));
  circle(x + 0.055f * s, y - 1.15f * s, 0.016f * s, rgb(0xffffff));
}

static void drawCandy(float x, float y, float s, uint32_t c) {
  float l[] = {x - 0.12f * s, y, x - 0.24f * s, y - 0.09f * s, x - 0.24f * s, y + 0.09f * s};
  float r[] = {x + 0.12f * s, y, x + 0.24f * s, y - 0.09f * s, x + 0.24f * s, y + 0.09f * s};
  poly(l, 3, rgb(mixHex(c, 0xffffff, 0.3f)));
  poly(r, 3, rgb(mixHex(c, 0xffffff, 0.3f)));
  ellipse(x, y, 0.15f * s, 0.11f * s, rgb(c));
  line(x - 0.07f * s, y - 0.07f * s, x + 0.03f * s, y + 0.07f * s, 0.03f * s, rgb(0xffffff));
}

static void drawEgg(float x, float y, float s, uint32_t c) {
  ellipse(x, y, 0.15f * s, 0.2f * s, rgb(c));
  uint16_t band = rgb(mixHex(c, 0xffffff, 0.6f));
  for (int i = 0; i < 4; i++)
    line(x - 0.15f * s + i * 0.075f * s, y - (i & 1 ? 0.03f : -0.03f) * s, x - 0.075f * s + i * 0.075f * s, y + (i & 1 ? 0.03f : -0.03f) * s, 0.03f * s, band);
}

// A seagull, always in the air: wide M-shaped wings with black tips, white
// head, yellow beak with its red spot. The wings are its colour - yellow
// carries him ahead, purple carries him back - with a glow to match.
static void drawSeagull(float x, float y, float s, bool yellow) {
  uint32_t c = yellow ? 0xffd23a : 0xa66be0;
  circle(x, y, 0.55f * s, rgb(c), 60);                                  // glow
  float f = sinf(s_t * 10) * 0.22f * s;                                 // flapping
  for (int side = -1; side <= 1; side += 2) {
    float ex = x + side * 0.35f * s, ey = y - 0.18f * s - f;            // elbow, up
    float tx = x + side * 0.8f * s, ty = y + 0.05f * s - f * 0.4f;      // wingtip, down
    float inner[] = {x + side * 0.08f * s, y - 0.02f * s, ex, ey, ex, ey + 0.12f * s, x + side * 0.08f * s, y + 0.08f * s};
    float outer[] = {ex, ey, tx, ty, ex, ey + 0.12f * s};
    poly(inner, 4, rgb(c));
    poly(outer, 3, rgb(mixHex(c, 0x000000, 0.1f)));
    float tip[] = {tx, ty, tx - side * 0.16f * s, ty - 0.05f * s, tx - side * 0.12f * s, ty + 0.04f * s};
    poly(tip, 3, rgb(0x1e1e22));                                        // black wingtips
  }
  ellipse(x, y + 0.02f * s, 0.12f * s, 0.16f * s, rgb(0xffffff));       // white body
  circle(x, y - 0.14f * s, 0.09f * s, rgb(0xffffff));                   // white head
  float bk[] = {x - 0.035f * s, y - 0.1f * s, x + 0.035f * s, y - 0.1f * s, x, y + 0.0f * s};
  poly(bk, 3, rgb(0xffc21a));
  circle(x, y - 0.04f * s, 0.015f * s, rgb(0xe03030));                  // red spot
  circle(x - 0.04f * s, y - 0.16f * s, 0.016f * s, rgb(0x202020));
  circle(x + 0.04f * s, y - 0.16f * s, 0.016f * s, rgb(0x202020));
}

// Alice from behind, running after him: golden hair, black band, blue
// dress, the white bow of her apron.
static void drawAlice(float x, float y, float s, float run) {
  groundShadow(x, y, 0.35f, s);
  float k = sinf(run);
  line(x - 0.08f * s, y - 0.45f * s, x - 0.1f * s, y - (0.05f + (k > 0 ? 0.08f : 0)) * s, 0.07f * s, rgb(0xffffff));
  line(x + 0.08f * s, y - 0.45f * s, x + 0.1f * s, y - (0.05f + (k < 0 ? 0.08f : 0)) * s, 0.07f * s, rgb(0xffffff));
  ellipse(x - 0.1f * s, y - (0.04f + (k > 0 ? 0.08f : 0)) * s, 0.07f * s, 0.04f * s, rgb(0x202020));
  ellipse(x + 0.1f * s, y - (0.04f + (k < 0 ? 0.08f : 0)) * s, 0.07f * s, 0.04f * s, rgb(0x202020));
  float dress[] = {x - 0.14f * s, y - 1.0f * s, x + 0.14f * s, y - 1.0f * s, x + 0.36f * s, y - 0.42f * s, x - 0.36f * s, y - 0.42f * s};
  poly(dress, 4, rgb(0x6fa8dc));
  float bl[] = {x, y - 0.75f * s, x - 0.16f * s, y - 0.83f * s, x - 0.16f * s, y - 0.67f * s};
  float br[] = {x, y - 0.75f * s, x + 0.16f * s, y - 0.83f * s, x + 0.16f * s, y - 0.67f * s};
  poly(bl, 3, rgb(0xffffff));
  poly(br, 3, rgb(0xffffff));
  line(x - 0.18f * s, y - 0.9f * s, x - 0.32f * s, y - (0.7f - k * 0.08f) * s, 0.06f * s, rgb(0xf6d2b8));
  line(x + 0.18f * s, y - 0.9f * s, x + 0.32f * s, y - (0.7f + k * 0.08f) * s, 0.06f * s, rgb(0xf6d2b8));
  ellipse(x, y - 1.05f * s, 0.2f * s, 0.32f * s, rgb(0xf2d16b));              // long hair
  circle(x, y - 1.25f * s, 0.17f * s, rgb(0xf2d16b));
  rect(x - 0.17f * s, y - 1.38f * s, 0.34f * s, 0.05f * s, rgb(0x202020));   // hairband
}

static void drawArch(float x, float y, float s) {
  float w = PATH_HALF * 2.2f * s;
  rect(x - w / 2, y - 3.0f * s, 0.25f * s, 3.0f * s, rgb(0xfff7fb));
  rect(x + w / 2 - 0.25f * s, y - 3.0f * s, 0.25f * s, 3.0f * s, rgb(0xfff7fb));
  roundRect(x - w / 2, y - 3.5f * s, w, 0.6f * s, 0.2f * s, rgb(0xff8fb1));
  for (int i = 0; i < 6; i++) circle(x - w / 2 + (i + 0.5f) * w / 6, y - 3.5f * s, 0.18f * s, rgb(PASTEL[i]));
}

// A Linzer cookie: round, scalloped, a jam heart in the middle.
static void drawLinzer(float x, float y, float r) {
  for (int i = 0; i < 8; i++) circle(x + cosf(i * 0.785f) * r * 0.85f, y + sinf(i * 0.785f) * r * 0.85f, r * 0.3f, rgb(0xe8c08a));
  circle(x, y, r * 0.9f, rgb(0xf0cc96));
  circle(x - r * 0.18f, y - r * 0.12f, r * 0.22f, rgb(0xc8203c));
  circle(x + r * 0.18f, y - r * 0.12f, r * 0.22f, rgb(0xc8203c));
  float heart[] = {x - r * 0.38f, y - r * 0.05f, x + r * 0.38f, y - r * 0.05f, x, y + r * 0.4f};
  poly(heart, 3, rgb(0xc8203c));
}

// ---------------------------------------------------------------------------
// The race
// ---------------------------------------------------------------------------

static bool gooseNear(int lane, float z, float range) {
  for (int i = 0; i < s_nThings; i++)
    if (!s_things[i].gone && s_things[i].kind == GOOSE && s_things[i].lane == lane && fabsf(s_things[i].z - z) < range) return true;
  return false;
}

static void layCourse() {
  s_nThings = 0;
  float z = 30, goose = 0.26f + s_ch * 0.015f;
  while (z < s_course - 25 && s_nThings < MAX_THINGS) {
    Thing t = {z, CANDY, (uint8_t)(frand() * 3), (uint8_t)(frand() * 6), false};
    float r = frand();
    if (r < goose) t.kind = GOOSE;
    else if (r < goose + 0.14f) t.kind = EGG;
    if (t.kind == GOOSE) {
      int blocked = 0;
      for (int l = 0; l < 3; l++) if (l != t.lane && gooseNear(l, z, 6)) blocked++;
      if (blocked >= 2) t.kind = CANDY; // never all three paths at once
    }
    s_things[s_nThings++] = t;
    z += 6.5f + frand() * 7.5f;
  }
}

static void startChapter(int ch) {
  s_ch = ch;
  E = &PLACES[ch];
  s_seed = esp_random();
  s_rng = s_seed | 1;
  seedNoise(0x51ce + ch * 977);
  drawSky();
  s_course = 640 + ch * 45;
  // Needs sprinting and boosts, not just cruising (9.5 m/s): a good run
  // averages ~10.9, so the first place leaves room to learn and the last
  // asks for a tidy run.
  s_timeLimit = s_course / (10.2f + ch * 0.06f);
  s_clock = s_timeLimit;
  s_z = 0; s_v = CRUISE; s_lane = 1; s_laneF = 1; s_hopT = -1; s_puff = 1;
  s_boostV = s_boostT = s_stumbleT = s_caughtT = s_bumpT = 0;
  s_carryT = -1;
  s_alice = -38;
  s_catches = 0;
  s_lastTock = -1;
  s_gull.on = false;
  s_gullNext = 5 + frand() * 4;
  for (int i = 0; i < TORTOISES; i++) {
    Tortoise &t = s_turtles[i];
    t.z = 10 + (TORTOISES - 1 - i) * 9;    // the fastest starts nearest, so it's a fair race
    t.v = 9.35f + ch * 0.08f + i * 0.18f + frand() * 0.25f;
    t.phase = frand() * 6.28f;
    t.lane = i % 3;
    t.laneF = t.lane;
    t.col = i;
  }
  layCourse();
  for (Popup &p : s_pops) p.t = 0;
}

static void setMode(Mode m) { s_mode = m; s_modeT = 0; }

static void endRace(Mode why) {
  s_place = 1;
  for (const Tortoise &t : s_turtles) if (t.z >= s_course) s_place++;
  if (why == Mode::Won) {
    if (s_ch + 1 > s_unlocked && s_ch + 1 < CHAPTERS) { s_unlocked = s_ch + 1; s_prefs.putUChar("unl", s_unlocked); }
    RunAudio::fx(RunAudio::DingTop);
    if (s_ch == CHAPTERS - 1) why = Mode::Finale;
  } else {
    RunAudio::fx(why == Mode::Late ? RunAudio::Thunder : RunAudio::Bonk);
  }
  setMode(why);
}

static void update(float dt) {
  s_t += dt;
  s_modeT += dt;
  for (Popup &p : s_pops) if (p.t > 0) { p.t -= dt; p.y -= dt * 22; }
  if (s_mode == Mode::Ready) { if (s_modeT > 2.4f) setMode(Mode::Race); return; }
  if (s_mode != Mode::Race) return;

  s_clock -= dt;
  int sec = (int)ceilf(s_clock);
  if (sec != s_lastTock && sec <= 10 && sec > 0) { s_lastTock = sec; RunAudio::fx(RunAudio::Tock); }
  if (s_clock <= 0) { s_clock = 0; endRace(Mode::Late); return; }

  // --- the bunny ---
  float target = CRUISE;
  bool sprint = s_sprint && s_puff > 0.02f;
  if (sprint) { target = SPRINT; s_puff = fmaxf(0, s_puff - dt * 0.26f); }
  else s_puff = fminf(1, s_puff + dt * 0.12f);
  if (s_boostT > 0) { s_boostT -= dt; target += s_boostV; }
  if (s_stumbleT > 0) { s_stumbleT -= dt; target = 3; }
  if (s_caughtT > 0) { s_caughtT -= dt; target = 0; }
  if (s_bumpT > 0) s_bumpT -= dt;
  s_v += (target - s_v) * (1 - expf(-dt * (target < s_v ? 6 : 2.5f)));

  bool flying = s_carryT >= 0;
  if (flying) {
    s_carryT += dt;
    float d = 1.8f, u = clampf(s_carryT / d, 0, 1), e = u * u * (3 - 2 * u);
    s_z = s_carryFrom + s_carryBy * e;
    if (u >= 1) { s_carryT = -1; s_v = CRUISE; }
  } else {
    s_z += s_v * dt;
  }
  s_laneF += clampf(s_lane - s_laneF, -dt * 6, dt * 6);
  if (s_hopT >= 0) { s_hopT += dt; if (s_hopT > 0.6f) s_hopT = -1; }
  float hop = s_hopT >= 0 ? sinf(s_hopT / 0.6f * 3.14159f) : 0;
  int me = (int)lroundf(s_laneF);

  for (int i = 0; i < s_nThings; i++) {
    Thing &t = s_things[i];
    if (t.gone || t.lane != me || fabsf(t.z - s_z) > 0.8f || flying) continue;
    if (t.kind == GOOSE) {
      if (hop > 0.4f) continue; // over it
      t.gone = true;
      s_stumbleT = 1.1f;
      s_boostT = 0;
      RunAudio::fx(RunAudio::Honk);
      pop(W / 2, 150, "HONK!", 0xffffff);
    } else {
      t.gone = true;
      bool egg = t.kind == EGG;
      s_boostV = egg ? 6.0f : 3.5f;
      s_boostT = egg ? 2.2f : 1.4f;
      s_puff = fminf(1, s_puff + 0.15f);
      RunAudio::fx(egg ? RunAudio::DingHigh : RunAudio::Tick);
      pop(W / 2, 150, egg ? "EGG BOOST!" : "+SPEED", egg ? 0xff6fa0 : 0xffd84d);
    }
  }

  for (Tortoise &t : s_turtles) {
    if (t.z < s_course + 30) t.z += (t.v + sinf(s_t * 0.4f + t.phase) * 0.6f) * dt;
    if (gooseNear(t.lane, t.z + 3, 3))
      for (int l = 0; l < 3; l++) if (l != t.lane && !gooseNear(l, t.z + 3, 4)) { t.lane = l; break; }
    t.laneF += clampf(t.lane - t.laneF, -dt * 4, dt * 4);
    if (!flying && hop < 0.4f && (int)lroundf(t.laneF) == me && fabsf(t.z - s_z) < 1.0f && s_bumpT <= 0 && s_v > t.v) {
      s_v = t.v - 1.5f;
      s_bumpT = 0.8f;
      RunAudio::fx(RunAudio::Bonk);
      pop(W / 2, 150, "BONK!", 0xffffff);
    }
    if (t.z >= s_course && s_z < s_course) { endRace(Mode::Turtles); return; } // a tortoise got there first
  }

  // Seagulls swoop down: yellow carries him ahead, purple back.
  float gullEvery = s_ch == 8 ? 0.55f : 1.0f; // the seaside is full of them
  if (!s_gull.on) {
    s_gullNext -= dt;
    if (s_gullNext <= 0) {
      s_gull = {true, frand() < 0.55f, false, s_z + 40, 6, 0, (uint8_t)(frand() * 3)};
      RunAudio::fx(RunAudio::Gull);
    }
  } else {
    Gull &g = s_gull;
    g.t += dt;
    if (g.t < 1.6f) g.h = 6 * (1 - g.t / 1.6f) + 0.75f;
    else if (g.t < 4.2f) g.h = 0.75f + sinf(g.t * 5) * 0.08f; // low enough to grab him, still flying
    else g.h += 4 * dt;
    if (!g.used && !flying && g.t > 1.3f && g.t < 4.2f && g.lane == me && fabsf(g.z - s_z) < 1.1f) {
      g.used = true;
      s_carryT = 0;
      s_carryFrom = s_z;
      s_carryYellow = g.yellow;
      s_carryBy = g.yellow ? 50 : -30;
      RunAudio::fx(RunAudio::Gull);
      pop(W / 2, 120, g.yellow ? "WHEEE!" : "OH NO!", g.yellow ? 0xffd84d : 0xb287e6);
    }
    if (g.used && s_carryT < 0) g.on = false;
    if (!g.used && (g.t > 7 || g.z < camZ())) g.on = false;
    if (!g.on) s_gullNext = (8 + frand() * 6) * gullEvery;
  }

  // Alice.
  float gap = s_z - s_alice;
  float av = 10.3f + s_ch * 0.06f + (gap > 50 ? 2.4f : 0) + (s_stumbleT > 0 ? 1 : 0);
  if (s_caughtT <= 0) s_alice += av * dt;
  if (flying && s_alice > s_z - 1) s_alice = s_z - 1;
  if (!flying && s_caughtT <= 0 && s_alice >= s_z - 0.3f) {
    s_catches++;
    s_caughtT = 1.8f;
    s_alice = s_z - 26;
    RunAudio::fx(RunAudio::Bonk);
    pop(W / 2, 120, "CAUGHT!", 0x6fa8dc);
    if (s_catches >= CATCHES_TO_LOSE) { endRace(Mode::AliceWins); return; }
  }

  if (s_z >= s_course) endRace(Mode::Won);
}

// ---------------------------------------------------------------------------
// Drawing a frame
// ---------------------------------------------------------------------------

enum DrawKind : uint8_t { D_PROP, D_THING, D_TURTLE, D_BUNNY, D_ALICE, D_GULL, D_ARCH };
struct Drawable { float dz; uint8_t kind; int idx; float x; uint32_t h; };
static Drawable s_list[96];

static void addDraw(int &n, float dz, uint8_t kind, int idx, float x = 0, uint32_t h = 0) {
  if (n < (int)(sizeof(s_list) / sizeof(s_list[0]))) s_list[n++] = {dz, kind, idx, x, h};
}

static void screenFx() {
  switch (E->fx) {
    case FX_PETALS:
      for (int i = 0; i < 14; i++) {
        float x = fmodf(i * 67 + s_t * 30 + sinf(s_t + i) * 20, W), y = fmodf(i * 41 + s_t * (35 + i * 3), H);
        ellipse(x, y, 3, 1.8f, rgb(i & 1 ? 0xffb3c7 : 0xd8233c), 200);
      }
      break;
    case FX_SMOKE:
      for (int i = 0; i < 5; i++) {
        float t = fmodf(s_t * 0.25f + i * 0.2f, 1);
        float x = 60 + i * 80 + sinf(s_t + i) * 10, y = HY + 20 - t * 110;
        circle(x, y, 6 + t * 14, rgb(0xa9b8ff), (uint8_t)(150 * (1 - t)));
        circle(x, y, 3 + t * 10, rgb(E->skyLow), (uint8_t)(150 * (1 - t)));
      }
      break;
    case FX_TEARS:
      for (int i = 0; i < 12; i++) {
        float x = fmodf(i * 97 + 13, W), y = fmodf(i * 53 + s_t * 160, H);
        ellipse(x, y, 1.5f, 3, rgb(0xbfe6ff), 200);
      }
      break;
    case FX_GRINS:
      for (int i = 0; i < 10; i++) {
        float a = 0.5f + 0.5f * sinf(s_t * 2 + i * 1.7f);
        circle(fmodf(i * 113 + 7, W), fmodf(i * 37 + 11, HY + 60), 1.5f, rgb(0xffd8f8), (uint8_t)(a * 255));
      }
      break;
    case FX_PEPPER:
      for (int i = 0; i < 26; i++) {
        float x = fmodf(i * 59 + s_t * 50, W), y = fmodf(i * 31 + sinf(s_t * 2 + i) * 15 + s_t * 12, H);
        circle(x, y, 1.2f, rgb(0x6a4a2a), 180);
      }
      break;
    case FX_FIREFLIES:
      for (int i = 0; i < 10; i++) {
        float x = fmodf(i * 89 + sinf(s_t * 0.7f + i) * 30 + 400, W), y = HY + 10 + fmodf(i * 23 + cosf(s_t * 0.5f + i) * 20 + 100, 140);
        float a = 0.5f + 0.5f * sinf(s_t * 3 + i);
        circle(x, y, 3, rgb(0xd8ff7a), (uint8_t)(a * 120));
        circle(x, y, 1.2f, rgb(0xf8ffc0), (uint8_t)(a * 255));
      }
      break;
    default: break;
  }
}

static const char *ord(int n) { return n == 1 ? "1st" : n == 2 ? "2nd" : n == 3 ? "3rd" : n == 4 ? "4th" : "5th"; }

static void drawWatch(float cx, float cy, float frac, bool urgent) {
  circle(cx, cy, 13, rgb(0xe8c25a));
  circle(cx, cy, 11, rgb(urgent && fmodf(s_t, 0.5f) < 0.25f ? 0xffd0d8 : 0xfffaf0));
  rect(cx - 2, cy - 17, 4, 4, rgb(0xe8c25a));
  float a = -1.5708f + (1 - frac) * 6.2832f;
  line(cx, cy, cx + cosf(a) * 9, cy + sinf(a) * 9, 1.6f, rgb(0x2a1830));
  line(cx, cy, cx, cy - 6, 1.6f, rgb(0x2a1830));
  circle(cx, cy, 1.5f, rgb(0xd23a52));
}

static void card(float x, float y, float w, float h) {
  roundRect(x, y, w, h, 16, rgb(0xfff4fa), 238);
  roundRect(x, y, w, 3, 2, rgb(0xff8fb1));
}

static void drawWorld() {
  target(s_frame, FW, FH);
  setScale(0.5f);
  drawWorldBack();

  // Gather everything in view and draw it far to near.
  int n = 0;
  float cz = camZ();
  float spacing = 5.5f;
  for (int i = (int)floorf((cz + 1) / spacing); i * spacing < cz + FAR; i++) {
    for (int side = -1; side <= 1; side += 2) {
      uint32_t h = hashi(i, side + 5);
      if (h % 4 == 0) continue; // gaps
      float z = i * spacing + (h % 100) / 50.0f;
      float off = PATH_HALF + 0.9f + ((h >> 8) % 100) / 100.0f * 2.6f;
      addDraw(n, z - cz, D_PROP, (h >> 16) % 3, curve(z) + side * off, h);
    }
  }
  for (int i = 0; i < s_nThings; i++) {
    const Thing &t = s_things[i];
    if (!t.gone && t.z > cz && t.z < cz + FAR) addDraw(n, t.z - cz, D_THING, i);
  }
  for (int i = 0; i < TORTOISES; i++) if (s_turtles[i].z > cz && s_turtles[i].z < cz + FAR) addDraw(n, s_turtles[i].z - cz, D_TURTLE, i);
  if (s_course > cz && s_course < cz + FAR) addDraw(n, s_course - cz, D_ARCH, 0);
  if (s_mode != Mode::Title) addDraw(n, s_z - cz, D_BUNNY, 0);
  if (s_alice > cz + 0.7f && s_mode != Mode::Title) addDraw(n, s_alice - cz, D_ALICE, 0);
  if (s_gull.on) addDraw(n, (s_gull.used && s_carryT >= 0 ? s_z : s_gull.z) - cz - 0.01f, D_GULL, 0);
  std::sort(s_list, s_list + n, [](const Drawable &a, const Drawable &b) { return a.dz > b.dz; });

  if (s_input) s_input->poll();

  float hopLift = s_hopT >= 0 ? sinf(s_hopT / 0.6f * 3.14159f) * 0.7f : 0;
  float carryLift = s_carryT >= 0 ? sinf(clampf(s_carryT / 1.8f, 0, 1) * 3.14159f) * 2.4f + 0.3f : 0;
  for (int k = 0; k < n; k++) {
    const Drawable &d = s_list[k];
    float sx, sy, s, z = cz + d.dz;
    switch (d.kind) {
      case D_PROP:
        if (project(d.x, 0, z, sx, sy, s)) drawProp(E->props[d.idx], sx, sy, s, d.h);
        break;
      case D_THING: {
        const Thing &t = s_things[d.idx];
        if (!project(laneX(t.lane, z), 0, z, sx, sy, s)) break;
        if (t.kind == GOOSE) drawGoose(sx, sy, s, fmodf(s_t + t.z, 1.6f) < 0.3f);
        else {
          groundShadow(sx, sy, 0.15f, s);
          float bob = (0.45f + sinf(s_t * 4 + t.z) * 0.06f) * s;
          if (t.kind == EGG) drawEgg(sx, sy - bob, s, PASTEL[t.col]);
          else drawCandy(sx, sy - bob, s, PASTEL[t.col]);
        }
        break;
      }
      case D_TURTLE: {
        const Tortoise &t = s_turtles[d.idx];
        if (project(laneX(t.laneF, z), 0, z, sx, sy, s)) drawTortoise(t, sx, sy, s);
        break;
      }
      case D_ARCH:
        if (project(curve(z), 0, z, sx, sy, s)) drawArch(sx, sy, s);
        break;
      case D_BUNNY:
        if (project(laneX(s_laneF, z), 0, z, sx, sy, s))
          drawBunny(sx, sy, s, s_caughtT > 0 ? 0 : s_z * 1.5f, (hopLift + carryLift) * s);
        break;
      case D_ALICE:
        if (project(laneX(1 + sinf(s_t * 0.7f) * 0.6f, z), 0, z, sx, sy, s)) drawAlice(sx, sy, s, s_alice * 1.7f);
        break;
      case D_GULL: {
        bool carrying = s_gull.used && s_carryT >= 0;
        float gz = carrying ? s_z : s_gull.z, lane = carrying ? s_laneF : s_gull.lane;
        float h = carrying ? carryLift + 1.15f : s_gull.h + 0.4f;
        if (!carrying && project(laneX(lane, gz), 0, gz, sx, sy, s)) groundShadow(sx, sy, 0.45f, s); // it's up in the air
        if (project(laneX(lane, gz), h, gz, sx, sy, s)) drawSeagull(sx, sy, s, s_gull.yellow);
        break;
      }
    }
  }
  screenFx();
  setScale(1);
}

static void hud() {
  setScale(0.5f);
  drawWatch(26, 26, s_clock / s_timeLimit, s_clock < 10);
  float bx0 = 120, bx1 = 320, by = 18;
  roundRect(bx0, by - 3, bx1 - bx0, 6, 3, rgb(0xffffff), 200);
  auto mark = [&](float z, uint32_t c, float r) { circle(bx0 + (bx1 - bx0) * clampf(z / s_course, 0, 1), by, r, rgb(c)); };
  for (const Tortoise &t : s_turtles) mark(t.z, 0x5fb87a, 3);
  mark(s_alice, 0x3a78c8, 3.5f);
  mark(s_z, 0xffffff, 4.5f);
  drawLinzer(bx1 + 12, by, 7);
  roundRect(120, 30, 80, 5, 2, rgb(0xffffff), 160);
  roundRect(120, 30, 80 * s_puff, 5, 2, rgb(s_puff > 0.25f ? 0xff8fb1 : 0xff5a6e));
  for (int i = 0; i < CATCHES_TO_LOSE; i++) circle(W - 20 - i * 14, 40, 4.5f, rgb(i < s_catches ? 0x3a78c8 : 0xffffff), i < s_catches ? 255 : 120);
  setScale(1);
  char b[16];
  int left = (int)ceilf(s_clock);
  snprintf(b, sizeof(b), "%d:%02d", left / 60, left % 60);
  uint16_t ink = E->night ? rgb(0xfff4fa) : rgb(0x5a2a4a); // readable on a night sky too
  txt(46, 18, b, 1, s_clock < 10 ? rgb(0xff5a6e) : ink, false);
  int place = 1;
  for (const Tortoise &t : s_turtles) if (t.z > s_z) place++;
  txt(W - 58, 12, ord(place), 1, ink, false);
  float gap = s_z - s_alice;
  if (gap < CAM_BACK + 0.7f && gap > 0 && fmodf(s_t, 0.5f) < 0.3f) txtC(W / 2, 64, "ALICE IS RIGHT BEHIND YOU!", 1, rgb(0x3a78c8));
}

static void render() {
  drawWorld();
  for (const Popup &p : s_pops) if (p.t > 0) txtC((int)p.x, (int)p.y, p.text, 1, rgb(p.col));
  if (s_mode == Mode::Race || s_mode == Mode::Ready || s_mode == Mode::Paused) hud();

  setScale(0.5f);
  char b[96]; // fits both story lines of a place
  const uint16_t TXT_INK = rgb(0x5a2a4a), TXT_RED = rgb(0xd23a52), TXT_SOFT = rgb(0x8a5a7a);
  switch (s_mode) {
    case Mode::Title: {
      card(14, 20, W - 28, H - 40);
      setScale(1);
      tenniel(TENNIEL_RABBIT, TENNIEL_RABBIT_W, TENNIEL_RABBIT_H, 14, 12, 0x4a1e4a, 0xfff4e6);
      // The column right of the picture holds 19 characters a line.
      txtC(318, 26, "LATE FOR", 2, TXT_RED, false);
      txtC(318, 56, "TEA", 2, TXT_RED, false);
      snprintf(b, sizeof(b), "<  %d of %d  >", s_pick + 1, CHAPTERS);
      txtC(318, 92, b, 1, TXT_SOFT, false);
      const Place &p = PLACES[s_pick];
      int y = wrapC(318, 112, p.name, 19, s_pick <= s_unlocked ? TXT_RED : TXT_SOFT) + 6;
      if (s_pick <= s_unlocked) {
        snprintf(b, sizeof(b), "%s %s", p.line1, p.line2);
        wrapC(318, y, b, 19, TXT_INK);
      } else {
        wrapC(318, y, "Win the place before to get here.", 19, TXT_SOFT);
      }
      txtC(318, 238, "SELECT run  UP back", 1, TXT_SOFT, false);
      break;
    }
    case Mode::Ready:
      setScale(1);
      snprintf(b, sizeof(b), "%d. %s", s_ch + 1, E->name);
      txtC(W / 2, 80, b, 1, rgb(0xffffff));
      txtC(W / 2, 104, "TEA IS AT FOUR!", 2, TXT_RED);
      txtC(W / 2, 144, "Beat the tortoises. Dodge Alice.", 1, rgb(0xffffff));
      break;
    case Mode::Paused:
      card(90, 80, W - 180, 110);
      setScale(1);
      txtC(W / 2, 100, "PAUSED", 2, TXT_RED, false);
      txtC(W / 2, 148, "SELECT go on  DOWN quit", 1, TXT_SOFT, false);
      break;
    case Mode::Won:
      card(60, 50, W - 120, 180);
      setScale(0.5f);
      for (int i = 0; i < 5; i++) drawLinzer(W / 2 - 80 + i * 40, 110, 14);
      setScale(1);
      txtC(W / 2, 64, "FIRST - AND ON TIME!", 1, TXT_RED, false);
      txtC(W / 2, 140, "The Linzer cookies are safe.", 1, TXT_INK, false);
      snprintf(b, sizeof(b), "%.1fs to spare", s_clock);
      txtC(W / 2, 160, b, 1, TXT_SOFT, false);
      snprintf(b, sizeof(b), "Next: %s", PLACES[s_ch + 1].name);
      txtC(W / 2, 186, b, 1, TXT_INK, false);
      txtC(W / 2, 208, "SELECT go on  UP menu", 1, TXT_SOFT, false);
      break;
    case Mode::Turtles: {
      card(40, 50, W - 80, 180);
      setScale(0.5f);
      Tortoise t = s_turtles[0];
      for (int i = 0; i < 3; i++) { t.col = i; drawTortoise(t, W / 2 - 70 + i * 70, 128, 70); }
      setScale(1);
      txtC(W / 2, 64, "THE TORTOISES GOT THERE FIRST", 1, TXT_RED, false);
      txtC(W / 2, 148, "...and gobbled every single", 1, TXT_INK, false);
      txtC(W / 2, 164, "Linzer cookie. Crumbs!", 1, TXT_INK, false);
      txtC(W / 2, 200, "SELECT try again  UP menu", 1, TXT_SOFT, false);
      break;
    }
    case Mode::AliceWins:
      card(40, 50, W - 80, 180);
      setScale(0.5f);
      drawAlice(W / 2, 130, 70, 0);
      for (int i = 0; i < 4; i++) drawLinzer(W / 2 - 90 + i * 60 + (i > 1 ? 60 : 0), 110, 10);
      setScale(1);
      txtC(W / 2, 64, "ALICE CAUGHT YOU THREE TIMES", 1, TXT_RED, false);
      txtC(W / 2, 148, "She shares the cookies out: to the", 1, TXT_INK, false);
      txtC(W / 2, 164, "seagulls, the tortoises - and you.", 1, TXT_INK, false);
      txtC(W / 2, 200, "SELECT try again  UP menu", 1, TXT_SOFT, false);
      break;
    case Mode::Late:
      card(14, 20, W - 28, H - 40);
      setScale(1);
      tenniel(TENNIEL_QUEEN, TENNIEL_QUEEN_W, TENNIEL_QUEEN_H, 14, 12, 0x5a1020, 0xffeef0);
      txtC(290, 52, "TOO LATE!", 2, TXT_RED, false);
      wrapC(290, 100, "It's four o'clock and the Queen is furious.", 21, TXT_INK);
      txtC(290, 152, "\"Off with his ears!\"", 1, TXT_RED, false);
      txtC(290, 236, "SELECT again  UP menu", 1, TXT_SOFT, false);
      break;
    case Mode::Finale:
      card(14, 8, W - 28, H - 16);
      setScale(1);
      tenniel(TENNIEL_TEAPARTY, TENNIEL_TEAPARTY_W, TENNIEL_TEAPARTY_H, (FW - TENNIEL_TEAPARTY_W) / 2, 6, 0x4a1e4a, 0xfff4e6);
      setScale(0.5f);
      roundRect(24, 196, W - 48, 76, 12, rgb(0xfff4fa), 240);
      setScale(1);
      txtC(W / 2, 204, "IN TIME FOR TEA! The cookies were all his,", 1, TXT_RED, false);
      txtC(W / 2, 222, "but he shared them with everyone -", 1, TXT_INK, false);
      txtC(W / 2, 240, "the nicest of them all.", 1, TXT_INK, false);
      txtC(W / 2, 258, "SELECT menu", 1, TXT_SOFT, false);
      break;
    default: break;
  }
  if (s_lineup) {
    // Bench close-up of the cast (USB command only).
    setScale(0.5f);
    card(10, 60, W - 20, 200);
    Tortoise t = s_turtles[0];
    t.col = 1;
    drawTortoise(t, 70, 240, 160);
    drawGoose(190, 240, 140, fmodf(s_t, 1.2f) < 0.5f);
    drawSeagull(300, 150, 120, true);
    drawSeagull(400, 150, 120, false);
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
  s_sky = (uint16_t *)bigAlloc((size_t)SKY_W * SKY_ROWS * 2, false);
  if (!s_frame || !s_sky) {
    heap_caps_free(s_frame); s_frame = nullptr;
    heap_caps_free(s_sky); s_sky = nullptr;
    Serial.println("Late for Tea: not enough memory");
    return false;
  }
  s_prefs.begin("latetea", false);
  s_unlocked = s_prefs.getUChar("unl", 0);
  if (s_unlocked >= CHAPTERS) s_unlocked = CHAPTERS - 1;
  s_pick = s_unlocked;
  startChapter(s_pick); // the title screen shows this chapter's world behind it
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
  heap_caps_free(s_sky); s_sky = nullptr;
  s_active = false;
}

bool active() { return s_active; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }
const uint16_t *frameBuffer() { return s_frame; }
void benchUnlockAll() { s_unlocked = CHAPTERS - 1; }
void benchLineup() { s_lineup = !s_lineup; }
bool upSleepAllowed() { return s_mode != Mode::Race && s_mode != Mode::Ready; }

void pause() {
  if (s_mode == Mode::Race || s_mode == Mode::Ready) setMode(Mode::Paused);
}

static void changeLane(int d) {
  if (s_carryT >= 0) return;
  int l = s_lane + d;
  if (l >= 0 && l <= 2) s_lane = l;
}

static void hop() {
  if (s_hopT < 0 && s_carryT < 0 && s_caughtT <= 0) { s_hopT = 0; RunAudio::fx(RunAudio::Hop); }
}

static void toTitle() {
  s_pick = s_ch;
  setMode(Mode::Title);
}

void key(const InputEvent &ev) {
  if (!ev.pressed || ev.repeat) return;
  switch (s_mode) {
    case Mode::Title:
      if (ev.key == Key::Left && s_pick > 0) { s_pick--; startChapter(s_pick); }
      else if (ev.key == Key::Right && s_pick < CHAPTERS - 1) { s_pick++; startChapter(s_pick); }
      else if (ev.key == Key::Select && s_pick <= s_unlocked) { startChapter(s_pick); setMode(Mode::Ready); }
      else if (ev.key == Key::Up) s_exit = true;
      break;
    case Mode::Won:
      if (ev.key == Key::Select) { startChapter(s_ch + 1); setMode(Mode::Ready); }
      else if (ev.key == Key::Up) toTitle();
      break;
    case Mode::Turtles:
    case Mode::AliceWins:
    case Mode::Late:
      if (ev.key == Key::Select) { startChapter(s_ch); setMode(Mode::Ready); }
      else if (ev.key == Key::Up) toTitle();
      break;
    case Mode::Finale:
      if (ev.key == Key::Select || ev.key == Key::Up) toTitle();
      break;
    case Mode::Paused:
      if (ev.key == Key::Select) setMode(Mode::Race);
      else if (ev.key == Key::Down) toTitle();
      break;
    case Mode::Race:
    case Mode::Ready:
      if (ev.key == Key::Left) changeLane(-1);
      else if (ev.key == Key::Right) changeLane(1);
      else if (ev.key == Key::Select) hop();
      else if (ev.key == Key::Down) pause();
      break;
  }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  s_input = &input;
  static uint32_t last = 0;
  static float acc = 0;
  uint32_t now = millis();
  float dt = last ? (now - last) / 1000.0f : STEP;
  last = now;
  if (dt > 0.1f) dt = 0.1f;

  s_sprint = input.held(Key::Up);
  if (s_mode == Mode::Race || s_mode == Mode::Ready) {
    static int32_t wacc = 0;
    wacc += wheel;
    while (wacc >= 2) { wacc -= 2; changeLane(1); }
    while (wacc <= -2) { wacc += 2; changeLane(-1); }
    if (touch.justPressed()) {
      int16_t lx, ly;
      s_gfx->touchToLandscape(touch.x(), touch.y(), lx, ly);
      if (ly < HY + 40) hop();
      else changeLane(lx < W / 2 ? -1 : 1);
    }
  } else if (touch.justPressed()) {
    InputEvent tap = {Key::Select, true, false};
    key(tap);
  }

  acc += dt;
  while (acc >= STEP) { update(STEP); acc -= STEP; }
  render();
}

} // namespace LateForTea
