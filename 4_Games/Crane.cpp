#include "Crane.h"
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
#ifndef PC_SIM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

using namespace Raster;

namespace Crane {

// ---------------------------------------------------------------------------
// The frame, the camera, the world
// ---------------------------------------------------------------------------

// Everything in 3D draws straight into the half-resolution frame, in its
// own pixels (228 x 140).
static const int FW = HalfRes::FW, FH = HalfRes::FH;
static const float PI_F = 3.14159265f;

// The world: a 512 x 512 heightmap that wraps, one texel per world unit.
// Each texel packs its baked colour (RGB565, low 16 bits) and height
// (bits 16-23, 0..255).
static const int N = 512, MASK = N - 1;
static const int SEA = 72;           // the water level, in height units
static const float ZFAR = 520;       // draw distance
static const float TANF = 0.78f;     // tan of half the field of view (~76 degrees)
static const float SCALE = (FW / 2) / TANF;
static const float NEAR = 1.5f;

enum class Mode : uint8_t { Title, Story, Play, Paused, Dying, Over };

static bool s_active = false, s_exit = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static uint16_t *s_frame = nullptr;
static uint8_t *s_depth = nullptr; // per pixel: distance * 254 / ZFAR (255 = sky)
static uint32_t *s_map = nullptr;
static uint32_t *s_mip1 = nullptr, *s_mip2 = nullptr; // 256 and 128 square: the map, averaged
static Preferences s_prefs;

static Mode s_mode = Mode::Title;
static float s_t = 0, s_modeT = 0, s_clock = 0;
static bool s_storySeen = false, s_auto = false;
static int s_wishes = 0, s_rings = 0;
static uint32_t s_best[4];
static bool s_newBest = false;
static const char *s_why = "";
static char s_note[32];
static float s_noteT = 0;
static int s_world = 0, s_pick = 0; // the world made, the world chosen on the title

// The crane.
static float s_x, s_y, s_alt, s_yaw, s_bank = 0, s_vz = 0, s_speed, s_wind, s_flap = 0, s_dist = 0;
static float s_steer = 0, s_wheelSteer = 0;
static bool s_up = false, s_down = false;

// The camera (chases the crane).
struct Cam { float x, y, h, yaw, roll, horizon; };
static Cam s_cam;

// --- numbers and noise -------------------------------------------------------------

static uint32_t s_rng = 1;
static uint32_t rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static float frand() { return (rnd() >> 8) / 16777216.0f; }
static float frange(float a, float b) { return a + (b - a) * frand(); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smooth(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static float wrapAng(float a) { while (a > PI_F) a -= 2 * PI_F; while (a < -PI_F) a += 2 * PI_F; return a; }

static uint32_t s_seed = 1;
static float lattice(int x, int y, int salt) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)salt * 2246822519u + s_seed * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}
// Value noise that tiles with the given period (a power of two, in lattice
// cells), so the map wraps seamlessly at its edges.
static float noiseP(float x, float y, int period, int salt) {
  float fx = floorf(x), fy = floorf(y);
  int x0 = (int)fx, y0 = (int)fy, m = period - 1;
  float u = x - fx, v = y - fy;
  u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
  int xa = x0 & m, ya = y0 & m, xb = (xa + 1) & m, yb = (ya + 1) & m;
  float a = lattice(xa, ya, salt), b = lattice(xb, ya, salt), c = lattice(xa, yb, salt), d = lattice(xb, yb, salt);
  return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}
// Fractal sum, fbm(p) = sum 0.5^i n(2^i p) / sum 0.5^i, at map coordinates
// u, v in [0, 1).
static float fbmP(float u, float v, int cells, int oct, int salt) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int i = 0; i < oct; i++) {
    s += amp * noiseP(u * cells, v * cells, cells, salt + i);
    norm += amp;
    cells *= 2;
    amp *= 0.5f;
  }
  return s / norm;
}
// Ridged multifractal (Musgrave): r = (1 - |2n - 1|)^2 per octave, each
// octave weighted by the last - sharp crests, smooth valleys.
static float ridgedP(float u, float v, int cells, int oct, int salt) {
  float s = 0, amp = 0.5f, norm = 0, w = 1;
  for (int i = 0; i < oct; i++) {
    float n = 1 - fabsf(2 * noiseP(u * cells, v * cells, cells, salt + i) - 1);
    n *= n;
    s += n * amp * w;
    norm += amp;
    w = clampf(n * 1.6f, 0, 1);
    cells *= 2;
    amp *= 0.5f;
  }
  return s / norm;
}

static inline uint16_t rgb565(float r, float g, float b) {
  int R = r > 255 ? 255 : r < 0 ? 0 : (int)r, G = g > 255 ? 255 : g < 0 ? 0 : (int)g, B = b > 255 ? 255 : b < 0 ? 0 : (int)b;
  return (uint16_t)(((R & 0xf8) << 8) | ((G & 0xfc) << 3) | (B >> 3));
}
static void unhex(uint32_t c, float &r, float &g, float &b) { r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; }

// --- text ---------------------------------------------------------------------------

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
static int textW(const char *s, int size) { return (int)strlen(s) * 6 * size; }
static void txt(int x, int y, const char *s, int size, uint16_t col, bool shadow = true) {
  if (shadow)
    for (int k = 0; s[k]; k++) glyph(x + 1 + k * 6 * size, y + 1, (unsigned char)s[k], size, 0);
  for (int k = 0; s[k]; k++) glyph(x + k * 6 * size, y, (unsigned char)s[k], size, col);
}
static void txtC(int cx, int y, const char *s, int size, uint16_t col, bool shadow = true) { txt(cx - textW(s, size) / 2, y, s, size, col, shadow); }
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
    y += 10;
    s += n;
    while (*s == ' ') s++;
  }
  return y;
}
static void note(const char *t) { strlcpy(s_note, t, sizeof(s_note)); s_noteT = 1.8f; }

// ---------------------------------------------------------------------------
// The four worlds
// ---------------------------------------------------------------------------
//
// Each world is a seed for the same generator: how the land is shaped, what
// grows on it, what people built, and the light and air it is seen in.

struct World {
  const char *name, *line;
  // Shape: island mask threshold, fraction under water, hills, mountains,
  // terrace step (0 = none).
  float landAt, waterPct, hills, mountains, terrace;
  bool river, path, town, poplars, boats, sails;
  // Ground.
  uint32_t grassA, grassB, rockA, rockB, sand, shallow, deep, snow;
  float snowLine; // fraction of land height; above 1 = none
  // Trees: six crown colours, how thick, how big, cones or domes.
  uint32_t tree[6];
  float treeDensity, treeR, treeH;
  bool conifers;
  // Light and air.
  uint32_t skyTop, skyHorizon, haze, sun, shade, mist, cloud;
  float mistAmt, fogDensity, sunEl, sunAz, cloudAmt, reflect, ripple, lightK;
};

static const World WORLDS[4] = {
    {"Maple Hollow", "Autumn woods, a winding path",
     0.36f, 0.16f, 0.62f, 0.25f, 0, false, true, false, false, false, false,
     0x8a8a3a, 0xb09a48, 0x8a7a68, 0x6a5e52, 0xd8c8a0, 0x5a8a8a, 0x1a3a4a, 0xffffff, 9,
     {0xe8741c, 0xd8401e, 0xf2b22a, 0xb8321e, 0xf0d050, 0x7a8a30}, 0.82f, 3.0f, 2.6f, false,
     0x8aa0c0, 0xf4e8d8, 0xe8dccc, 0xffd8a0, 0x5a4a6a, 0xf4ecdc, 0xffffff,
     0.18f, 2.2f, 0.22f, 2.6f, 0.35f, 0.45f, 0.5f, 1.0f},
    {"River Citadel", "A river town under its fortress",
     0.40f, 0.10f, 0.45f, 0.42f, 0, true, false, true, false, true, false,
     0x5a8a38, 0x8aa648, 0x6a6a66, 0x505456, 0xc8c0a8, 0x6a9a9a, 0x2a4a5a, 0xffffff, 9,
     {0x355a26, 0x4a6e2c, 0x5e7a32, 0x8a7a2a, 0xa8742c, 0x2e4a24}, 0.45f, 2.8f, 2.4f, false,
     0x4a7ac8, 0xd0e2f4, 0xb8cadc, 0xfff6e0, 0x46506a, 0xdce6f0, 0xffffff,
     0.10f, 1.8f, 0.45f, 2.2f, 0.62f, 0.55f, 0.7f, 1.05f},
    {"Snow Lake", "Still water under snowy peaks",
     0.50f, 0.46f, 0.20f, 0.78f, 0, false, false, false, true, true, false,
     0x4a5a46, 0x66705a, 0x3a3e46, 0x2a2e36, 0xa8a8a0, 0x6a8098, 0x2a3a52, 0xf2f6fc, 0.18f,
     {0x1e3428, 0x24402e, 0x2a3a2c, 0x1a2a24, 0x34443a, 0x283a30}, 0.55f, 2.2f, 3.2f, true,
     0x7a90ac, 0xc8d2de, 0xb4c2d2, 0xf0f4ff, 0x3a4a6a, 0xd6dee8, 0xe8eef4,
     0.55f, 2.6f, 0.30f, 2.0f, 0.75f, 0.85f, 0.15f, 0.92f},
    {"White Cliffs", "Limestone, turquoise sea, sails",
     0.46f, 0.55f, 0.35f, 0.35f, 0.085f, false, false, false, false, false, true,
     0x6e7a40, 0x8a8a52, 0xece2c8, 0xcdbf9e, 0xf2dfb2, 0x40d4cc, 0x0a3a86, 0xffffff, 9,
     {0x4e5e30, 0x5e6a36, 0x6a6a3a, 0x445428, 0x7a7444, 0x56602e}, 0.35f, 1.6f, 1.2f, false,
     0x2e6ec0, 0xbcd8ee, 0xaecce6, 0xfffbe8, 0x4a5a80, 0xe4f0f8, 0xffffff,
     0.05f, 1.6f, 0.55f, 2.4f, 0.25f, 0.4f, 0.6f, 1.08f},
};
static const World *W = &WORLDS[0];

// ---------------------------------------------------------------------------
// Making a world
// ---------------------------------------------------------------------------
//
// 1. Height: an island mask c = fbm(4 cells) -> m = smoothstep(landAt,
//    landAt + 0.16, c); land = hills fbm(16) + mountains ridged(8)^1.5,
//    optionally terraced into plateaus with cliff faces,
//    t = h/step: h' = (floor t + smoothstep(0.72, 1, frac t)) step;
//    a river where |fbm - 0.5| is small carves a channel with flat banks.
//    Remapped so `waterPct` of the map lies under water.
// 2. Classes: water, sand, ground, a path where |fbm - 0.5| < 0.007.
// 3. Things: trees stamped as domes (h = H sqrt(1 - d^2/r^2)) or cones
//    (h = H (1 - d/r)) on a jittered 4-texel grid where a density noise
//    allows; poplar rows on the shore; houses and a hilltop fortress as
//    blocks with pale walls round their edge and dark roofs inside.
// 4. Light: a sweep along each row toward the sun keeps the horizon
//    height hs = max(hs - tan(el), h); a texel below it is in shadow
//    (soft over 5 units). Lambert from height differences, ambient
//    occlusion from a 13 x 13 local mean, shadow tinted the world's shade
//    colour, sunlight its sun colour, snow only where the slope lets it lie.

static inline int heightAt(int x, int y) { return (s_map[(y & MASK) * N + (x & MASK)] >> 16) & 255; }

// Bilinear ground height (water is flat), for flying and placing things.
static float groundAt(float x, float y) {
  float fx = floorf(x), fy = floorf(y);
  int x0 = (int)fx, y0 = (int)fy;
  float u = x - fx, v = y - fy;
  float h = lerpf(lerpf(heightAt(x0, y0), heightAt(x0 + 1, y0), u), lerpf(heightAt(x0, y0 + 1), heightAt(x0 + 1, y0 + 1), u), v);
  return h < SEA ? SEA : h;
}

static void loadingFrame(float p) {
  target(s_frame, FW, FH);
  setScale(1);
  for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++) s_frame[y * FW + x] = rgb565(12 + y * 0.2f, 14 + y * 0.25f, 34 + y * 0.4f);
  txtC(FW / 2, 38, W->name, 2, rgb565(250, 236, 214));
  txtC(FW / 2, 60, "Folding the sky...", 1, rgb565(200, 196, 230));
  rect(44, 76, FW - 88, 4, rgb(0x2a2a50));
  rect(44, 76, (FW - 88) * p, 4, rgb(0xff8a6a));
  HalfRes::present(s_panel, s_gfx->flipped(), s_frame);
}

// Texel classes while building: what to colour it as.
enum : uint8_t { C_WATER, C_SAND, C_GROUND, C_PATH, C_WALL, C_ROOF, C_DOME, C_TREE = 16 };

static uint8_t *s_cls = nullptr; // only while making the world

static inline void setH(int x, int y, int h) {
  uint32_t &m = s_map[(y & MASK) * N + (x & MASK)];
  m = (m & 0xffff) | ((uint32_t)(h < 0 ? 0 : h > 255 ? 255 : h) << 16);
}

// A tree: a dome or a cone of height H and radius r, crown colour k.
static void stampTree(float cx, float cy, float r, float H, uint8_t k, bool cone) {
  int base = heightAt((int)cx, (int)cy);
  for (int y = (int)(cy - r) - 1; y <= (int)(cy + r) + 1; y++)
    for (int x = (int)(cx - r) - 1; x <= (int)(cx + r) + 1; x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
      if (d2 > r * r) continue;
      float t = cone ? H * (1 - sqrtf(d2) / r) : H * sqrtf(1 - d2 / (r * r));
      int h = base + 1 + (int)t;
      if (h > heightAt(x, y)) { setH(x, y, h); s_cls[(y & MASK) * N + (x & MASK)] = C_TREE + k + (d2 > r * r * 0.55f ? 8 : 0); }
    }
}

// A building: pale walls round the edge, a roof inside (or a dome).
static void stampBlock(int x0, int y0, int w, int d, int hgt, bool dome, bool anywhere = false) {
  int base = 0;
  int lo = 255;
  for (int y = y0; y < y0 + d; y++)
    for (int x = x0; x < x0 + w; x++) { base = max(base, heightAt(x, y)); lo = min(lo, heightAt(x, y)); }
  if (base - lo > 4 && !anywhere) return; // too steep to build on
  for (int y = y0; y < y0 + d; y++)
    for (int x = x0; x < x0 + w; x++) {
      bool edge = x == x0 || y == y0 || x == x0 + w - 1 || y == y0 + d - 1;
      int h = base + hgt;
      if (dome && !edge) {
        float dx = (x - x0 - (w - 1) / 2.0f) / (w / 2.0f), dy = (y - y0 - (d - 1) / 2.0f) / (d / 2.0f);
        h += (int)(4 * sqrtf(fmaxf(0, 1 - dx * dx - dy * dy)));
      }
      setH(x, y, h);
      s_cls[(y & MASK) * N + (x & MASK)] = edge ? C_WALL : dome ? C_DOME : C_ROOF;
    }
}

// Two cores: the second runs the first half of a job while this one runs
// the second half. Jobs split by rows (making a world) or by screen
// columns (drawing it), so the halves never touch the same memory.
typedef void (*Job)(int a, int b);
#ifndef PC_SIM
static TaskHandle_t s_worker = nullptr;
static SemaphoreHandle_t s_go = nullptr, s_done = nullptr;
static Job s_job = nullptr;
static int s_jobN = 0;
static void workerTask(void *) {
  for (;;) {
    xSemaphoreTake(s_go, portMAX_DELAY);
    s_job(0, s_jobN / 2);
    xSemaphoreGive(s_done);
  }
}
#endif
static void parallel(Job j, int n) {
#ifndef PC_SIM
  if (s_worker) {
    s_job = j;
    s_jobN = n;
    xSemaphoreGive(s_go);
    j(n / 2, n);
    xSemaphoreTake(s_done, portMAX_DELAY);
    return;
  }
#endif
  j(0, n);
}

static uint8_t *s_avg = nullptr, *s_shadow = nullptr; // only while making the world

// A long job on the second core gives its idle task a moment now and then
// (else the watchdog resets the board).
static inline void breathe(int y, int y0) {
#ifndef PC_SIM
  if (y0 == 0 && (y & 7) == 0) vTaskDelay(1);
#endif
}

// Pass 1, rows [y0, y1): raw heights, kept as floats in the map's own memory.
static void heightRows(int y0, int y1) {
  float *raw = (float *)s_map;
  for (int y = y0; y < y1; y++) {
    if (y0 && ((y - y0) & 31) == 0) loadingFrame(0.35f * (y - y0) / (y1 - y0));
    breathe(y, y0);
    float v = y / (float)N;
    for (int x = 0; x < N; x++) {
      float u = x / (float)N;
      float c = fbmP(u, v, 4, 4, 11);
      float m = smooth(W->landAt, W->landAt + 0.16f, c);
      float r = ridgedP(u, v, 4, 4, 51);
      float land = 0.18f + W->hills * fbmP(u, v, 8, 4, 31) + W->mountains * r * sqrtf(r) * smooth(W->landAt + 0.06f, W->landAt + 0.32f, c);
      float h = m * land - (1 - m) * 0.15f * noiseP(u * 8, v * 8, 8, 71);
      if (W->river) { // a channel, with flat meadows either side
        float rv = fabsf(fbmP(u, v, 4, 3, 191) - 0.5f);
        float bank = smooth(0.012f, 0.05f, rv);
        h = lerpf(-0.05f, fmaxf(h, 0.05f) * smooth(0.02f, 0.12f, rv) + 0.03f, bank);
      }
      raw[y * N + x] = h;
    }
  }
}

static void colourRows(int y0, int y1);

// Pass 4c, rows [y0, y1): colour and light. The sun comes from -x (the
// shadow sweep's direction).
static void colourRows(int y0, int y1) {
  const float LX = -cosf(W->sunEl), LY = -0.25f, LZ = sinf(W->sunEl) + 0.25f;
  float shR, shG, shB, sunR, sunG, sunB;
  unhex(W->shade, shR, shG, shB);
  unhex(W->sun, sunR, sunG, sunB);
  for (int y = y0; y < y1; y++) {
    if (y0 && ((y - y0) & 31) == 0) loadingFrame(0.65f + 0.33f * (y - y0) / (y1 - y0));
    breathe(y, y0);
    float v = y / (float)N;
    for (int x = 0; x < N; x++) {
      float u = x / (float)N;
      int i = y * N + x, h = heightAt(x, y);
      uint8_t k = s_cls[i];
      float r, g, b;
      float speck = lattice(x, y, 7);
      if (k == C_WATER) {
        float d = powf((SEA - h) / (float)SEA, 0.5f);
        float sr, sg, sb2, dr, dg, db;
        unhex(W->shallow, sr, sg, sb2);
        unhex(W->deep, dr, dg, db);
        r = lerpf(sr, dr, d); g = lerpf(sg, dg, d); b = lerpf(sb2, db, d);
        if (W->sails && d < 0.25f) { float q = 1 - d * 4; r += q * 30; g += q * 34; b += q * 20; } // sand under clear water
        s_map[i] |= rgb565(r, g, b);
        continue;
      }
      float hx = heightAt(x + 1, y) - heightAt(x - 1, y), hy = heightAt(x, y + 1) - heightAt(x, y - 1);
      float slope = sqrtf(hx * hx + hy * hy);
      float nx = -hx, ny = -hy, nz = 2.0f, nl = sqrtf(nx * nx + ny * ny + nz * nz);
      float diff = fmaxf(0, (nx * LX + ny * LY + nz * LZ) / nl);
      float sh = s_shadow[i] / 255.0f;
      float ao = clampf((h - s_avg[i]) / 28.0f, -0.35f, 0.2f);
      float e = (h - SEA) / (50 + 110 * W->mountains);
      float var = fbmP(u, v, 32, 2, 91);
      switch (k) {
        case C_SAND: unhex(W->sand, r, g, b); r -= speck * 16; g -= speck * 16; b -= speck * 12; break;
        case C_PATH: r = 196 - speck * 24; g = 172 - speck * 22; b = 142 - speck * 18; break;
        case C_WALL: r = 236 - speck * 14; g = 230 - speck * 14; b = 216 - speck * 12; break;
        case C_ROOF: { float t = lattice(x / 3, y / 3, 18); r = t < 0.6f ? 110 : 150; g = t < 0.6f ? 108 : 92; b = t < 0.6f ? 112 : 70; break; }
        case C_DOME: r = 80; g = 160; b = 140; break;
        case C_GROUND: {
          float ar, ag, ab, br, bg, bb;
          unhex(W->grassA, ar, ag, ab);
          unhex(W->grassB, br, bg, bb);
          r = lerpf(ar, br, var) + speck * 12; g = lerpf(ag, bg, var) + speck * 12; b = lerpf(ab, bb, var) + speck * 8;
          break;
        }
        default: { // a tree crown (its rim a little darker)
          unhex(W->tree[(k - C_TREE) & 7], r, g, b);
          if (k - C_TREE >= 8) { r *= 0.78f; g *= 0.78f; b *= 0.78f; }
          break;
        }
      }
      if (k == C_GROUND || k == C_SAND) { // rock on steep or high ground
        float rock = smooth(5, 11, slope) + smooth(0.5f, 0.68f, e);
        if (W->terrace > 0) rock = fmaxf(rock, smooth(3.5f, 7, slope) * 1.2f);
        if (rock > 0) {
          float ar, ag, ab, br, bg, bb;
          unhex(W->rockA, ar, ag, ab);
          unhex(W->rockB, br, bg, bb);
          float band = W->terrace > 0 ? 0.5f + 0.5f * sinf(h * 1.3f + var * 3) : var; // limestone strata
          float q = clampf(rock, 0, 1);
          r = lerpf(r, lerpf(ar, br, band), q); g = lerpf(g, lerpf(ag, bg, band), q); b = lerpf(b, lerpf(ab, bb, band), q);
        }
      }
      if (W->snowLine < 1 && (k == C_GROUND || k == C_SAND || k >= C_TREE)) { // snow where it can lie
        float snow = smooth(W->snowLine, W->snowLine + 0.07f, e + var * 0.06f) * (1 - 0.8f * smooth(8, 15, slope));
        if (k >= C_TREE) snow = fmaxf(snow, 0.35f * smooth(0.5f, 1, speck)); // dusted conifers
        if (snow > 0) { float sr, sg, sb2; unhex(W->snow, sr, sg, sb2); r = lerpf(r, sr, snow); g = lerpf(g, sg, snow); b = lerpf(b, sb2, snow); }
      }
      // Sunlit parts take the sun's colour; shadow leans to the shade colour.
      float lit = (0.25f + 0.85f * diff * (1 - 0.8f * sh)) * (0.85f + ao) * W->lightK;
      float sk = clampf(1 - lit, 0, 1) * 0.45f;
      r = r * lit * (sunR / 255) * (1 - sk) + shR * sk;
      g = g * lit * (sunG / 255) * (1 - sk) + shG * sk;
      b = b * lit * (sunB / 255) * (1 - sk) + shB * sk;
      s_map[i] |= rgb565(r, g, b);
    }
  }
}

static void makeWorld(int which) {
  W = &WORLDS[which];
  s_world = which;
  s_seed = esp_random() | 1;
  s_cls = (uint8_t *)heap_caps_malloc(N * N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t *avg = s_avg = (uint8_t *)heap_caps_malloc(N * N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t *shadow = s_shadow = (uint8_t *)heap_caps_malloc(N * N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_cls || !avg || !shadow) {
    heap_caps_free(s_cls); heap_caps_free(avg); heap_caps_free(shadow);
    s_cls = nullptr;
    memset(s_map, 0, N * N * 4);
    return;
  }
  // 1. Raw heights.
  float *raw = (float *)s_map;
  static uint32_t hist[256];
  parallel(heightRows, N);
  // Smooth away texel-sized roughness: the renderer turns it into stripes.
  loadingFrame(0.36f);
  {
    static float line[N];
    for (int pass = 0; pass < 2; pass++) {
      for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
          float s5 = 0;
          for (int k = -2; k <= 2; k++) s5 += raw[y * N + ((x + k) & MASK)];
          line[x] = s5 / 5;
        }
        memcpy(raw + y * N, line, sizeof(line));
      }
      for (int x = 0; x < N; x++) {
        for (int y = 0; y < N; y++) {
          float s5 = 0;
          for (int k = -2; k <= 2; k++) s5 += raw[((y + k) & MASK) * N + x];
          line[y] = s5 / 5;
        }
        for (int y = 0; y < N; y++) raw[y * N + x] = line[y];
      }
    }
    if (W->terrace > 0) // plateaus with cliff faces: h' = (floor t + smoothstep(0.7, 1, frac t)) step
      for (int i = 0; i < N * N; i++) {
        float h = raw[i];
        if (h <= 0.02f) continue;
        float t = h / W->terrace, f = floorf(t);
        raw[i] = (f + smooth(0.7f, 1.0f, t - f)) * W->terrace;
      }
    memset(hist, 0, sizeof(hist));
    for (int i = 0; i < N * N; i++) hist[(int)clampf((raw[i] + 0.2f) * 200, 0, 255)]++;
  }
  int target = (int)(N * N * W->waterPct), acc = 0, sb = 0;
  for (; sb < 256 && acc + (int)hist[sb] < target; sb++) acc += hist[sb];
  float seaRaw = sb / 200.0f - 0.2f, lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < N * N; i++) { lo = fminf(lo, raw[i]); hi = fmaxf(hi, raw[i]); }
  for (int i = 0; i < N * N; i++) {
    float h = raw[i];
    int b;
    if (h < seaRaw) b = (int)(SEA * clampf((h - lo) / (seaRaw - lo + 1e-6f), 0, 0.999f));
    else b = SEA + (int)((50 + 110 * W->mountains) * powf(clampf((h - seaRaw) / (hi - seaRaw + 1e-6f), 0, 1), 1.15f));
    s_map[i] = (uint32_t)b << 16;
  }
  // 2. Classes: water, sand, ground, path.
  loadingFrame(0.4f);
  for (int y = 0; y < N; y++) {
    float v = y / (float)N;
    for (int x = 0; x < N; x++) {
      int i = y * N + x, h = (s_map[i] >> 16) & 255;
      uint8_t k = h < SEA ? C_WATER : h < SEA + 2 ? C_SAND : C_GROUND;
      if (W->path && k == C_GROUND && fabsf(fbmP(x / (float)N, v, 4, 3, 211) - 0.5f) < 0.007f) k = C_PATH;
      s_cls[i] = k;
    }
  }
  // 3. Trees on a jittered grid, where the density noise allows.
  loadingFrame(0.5f);
  const int G = 4;
  for (int gy = 0; gy < N; gy += G)
    for (int gx = 0; gx < N; gx += G) {
      float cx = gx + lattice(gx, gy, 3) * G, cy = gy + lattice(gx, gy, 4) * G;
      int ix = (int)cx, iy = (int)cy;
      if (s_cls[(iy & MASK) * N + (ix & MASK)] != C_GROUND) continue;
      int h = heightAt(ix, iy);
      float e = (h - SEA) / (50 + 110 * W->mountains);
      if (e > (W->snowLine < 1 ? W->snowLine + 0.18f : 0.62f)) continue; // the tree line
      float dens = fbmP(cx / N, cy / N, 16, 3, 101);
      if (dens < 1 - W->treeDensity) continue;
      int slope = abs(heightAt(ix + 2, iy) - heightAt(ix - 2, iy)) + abs(heightAt(ix, iy + 2) - heightAt(ix, iy - 2));
      if (slope > 22) continue;
      float r = W->treeR * (0.7f + 0.6f * lattice(gx, gy, 5));
      uint8_t k = (uint8_t)((int)(lattice(gx, gy, 6) * 6) % 6);
      stampTree(cx, cy, r, W->treeH * (0.7f + 0.6f * lattice(gx, gy, 8)), k, W->conifers);
    }
  // Poplars: tall thin cones in rows along the shore.
  if (W->poplars)
    for (int y = 0; y < N; y += 3)
      for (int x = 0; x < N; x += 2) {
        int i = y * N + x;
        if (s_cls[i] != C_SAND && !(s_cls[i] == C_GROUND && heightAt(x, y) < SEA + 4)) continue;
        if (lattice(x, y, 9) < 0.55f) continue;
        stampTree(x + 0.5f, y + 0.5f, 1.4f, 6 + lattice(x, y, 10) * 3, 2, true);
      }
  // A town along the river and the fortress on the highest hill.
  if (W->town) {
    loadingFrame(0.55f);
    int bestH = 0, fx = 0, fy = 0;
    for (int y = 0; y < N; y += 4)
      for (int x = 0; x < N; x += 4) {
        int h = heightAt(x, y);
        if (h > bestH && h < 200) { bestH = h; fx = x; fy = y; }
      }
    for (int gy = 0; gy < N; gy += 7)
      for (int gx = 0; gx < N; gx += 7) {
        int x = gx + (int)(lattice(gx, gy, 12) * 3), y = gy + (int)(lattice(gx, gy, 13) * 3);
        int h = heightAt(x, y);
        if (h < SEA + 2 || h > SEA + 30) continue;
        bool nearWater = false; // by the river, on gentle ground
        for (int k = 0; k < 8 && !nearWater; k++) {
          float a = k * 0.785f;
          nearWater = heightAt(x + (int)(cosf(a) * 16), y + (int)(sinf(a) * 16)) < SEA;
        }
        if (!nearWater || fbmP(x / (float)N, y / (float)N, 16, 2, 231) < 0.45f) continue;
        if (abs(heightAt(x + 4, y) - h) + abs(heightAt(x, y + 4) - h) > 6) continue;
        bool church = lattice(x, y, 14) > 0.9f;
        stampBlock(x, y, 4 + (int)(lattice(x, y, 15) * 3), 4 + (int)(lattice(x, y, 16) * 3), church ? 4 : 2 + (int)(lattice(x, y, 17) * 2), church);
        if (church) stampBlock(x + 1, y - 2, 2, 2, 8, false); // its tower
      }
    // The fortress: long white walls, a keep, towers.
    stampBlock(fx - 14, fy - 6, 28, 12, 6, false, true);
    stampBlock(fx - 6, fy - 4, 12, 8, 11, false, true);
    stampBlock(fx - 14, fy - 7, 4, 4, 10, false, true);
    stampBlock(fx + 10, fy - 7, 4, 4, 10, false, true);
    stampBlock(fx + 2, fy + 3, 4, 4, 13, true, true);
  }
  // 4a. Shadows: sweep each row (twice round, the map wraps) toward the sun.
  loadingFrame(0.6f);
  float drop = tanf(W->sunEl);
  for (int y = 0; y < N; y++) {
    float hs = 0;
    for (int x = 0; x < 2 * N; x++) {
      int h = heightAt(x, y);
      int hh = h < SEA ? SEA : h;
      hs = fmaxf(hs - drop, (float)hh);
      if (x >= N) shadow[y * N + (x & MASK)] = (uint8_t)(255 * clampf((hs - hh) / 5.0f, 0, 1));
    }
  }
  // 4b. Ambient occlusion: height against a 13 x 13 mean (two running sums).
  {
    static uint16_t col[N];
    for (int y = 0; y < N; y++) {
      int sum = 0;
      for (int k = -6; k <= 6; k++) sum += heightAt(k, y);
      for (int x = 0; x < N; x++) {
        avg[y * N + x] = (uint8_t)(sum / 13);
        sum += heightAt(x + 7, y) - heightAt(x - 6, y);
      }
    }
    for (int x = 0; x < N; x++) {
      int sum = 0;
      for (int k = -6; k <= 6; k++) sum += avg[(k & MASK) * N + x];
      for (int y = 0; y < N; y++) {
        col[y] = (uint16_t)(sum / 13);
        sum += avg[((y + 7) & MASK) * N + x] - avg[((y - 6) & MASK) * N + x];
      }
      for (int y = 0; y < N; y++) avg[y * N + x] = (uint8_t)col[y];
    }
  }
  // 4c. Colour and light.
  parallel(colourRows, N);
  heap_caps_free(s_cls); s_cls = nullptr;
  heap_caps_free(avg);
  heap_caps_free(shadow);
  s_avg = s_shadow = nullptr;
  // Mip levels for the distance: each texel the average of four below.
  auto down = [](const uint32_t *src, int n, uint32_t *dst) {
    int m = n / 2;
    for (int y = 0; y < m; y++)
      for (int x = 0; x < m; x++) {
        uint32_t a = src[(2 * y) * n + 2 * x], b = src[(2 * y) * n + 2 * x + 1], c = src[(2 * y + 1) * n + 2 * x], d = src[(2 * y + 1) * n + 2 * x + 1];
        uint32_t h = (((a >> 16) & 255) + ((b >> 16) & 255) + ((c >> 16) & 255) + ((d >> 16) & 255)) / 4;
        uint32_t R = (((a >> 11) & 31) + ((b >> 11) & 31) + ((c >> 11) & 31) + ((d >> 11) & 31)) / 4;
        uint32_t G = (((a >> 5) & 63) + ((b >> 5) & 63) + ((c >> 5) & 63) + ((d >> 5) & 63)) / 4;
        uint32_t B = ((a & 31) + (b & 31) + (c & 31) + (d & 31)) / 4;
        dst[y * m + x] = (h << 16) | (R << 11) | (G << 5) | B;
      }
  };
  down(s_map, N, s_mip1);
  down(s_mip1, N / 2, s_mip2);
}

// Clouds painted on the sky (a band all round, tiling in azimuth) and the
// water's ripples (a small tiling table), both from noise.
static const int CW = 256, CH = 40;
static uint8_t *s_clouds = nullptr;
static uint8_t s_ripple[64 * 64];

static void makeSkyTables() {
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++) {
      float d = fbmP(x / (float)CW, y / (float)CW, 8, 4, 151);
      float a = smooth(1 - W->cloudAmt, 1.15f - W->cloudAmt * 0.6f, d) * smooth(0, 9, y) * (1 - smooth(28, CH, y));
      s_clouds[y * CW + x] = (uint8_t)(a * 255);
    }
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) s_ripple[y * 64 + x] = (uint8_t)(255 * fbmP(x / 64.0f, y / 64.0f, 4, 2, 171));
}

// ---------------------------------------------------------------------------
// The sky
// ---------------------------------------------------------------------------

static int s_sunCol = -100;

// World direction -> screen x (or -1000 if behind).
static float azToX(float az) {
  float d = wrapAng(az - s_cam.yaw);
  if (fabsf(d) > 1.3f) return -1000;
  return FW / 2 + tanf(d) * SCALE;
}

static void drawSky() {
  float r0, g0, b0, r1, g1, b1;
  unhex(W->skyTop, r0, g0, b0);
  unhex(W->skyHorizon, r1, g1, b1);
  for (int y = 0; y < FH; y++) {
    float t = clampf((y - (s_cam.horizon - 80)) / 84.0f, 0, 1);
    t = sqrtf(t);
    uint16_t c = rgb565(lerpf(r0, r1, t), lerpf(g0, g1, t), lerpf(b0, b1, t));
    uint16_t *row = s_frame + y * FW;
    for (int x = 0; x < FW; x++) row[x] = c;
  }
  memset(s_depth, 254, FW * FH); // the sky: farthest, and even (odd marks water)
  // The sun, soft through the haze.
  float sx = azToX(W->sunAz);
  s_sunCol = -100;
  if (sx > -50 && sx < FW + 50) {
    float sy = s_cam.horizon - tanf(W->sunEl) * SCALE + (sx - FW / 2) * tanf(s_cam.roll);
    s_sunCol = (int)sx;
    uint16_t sc = rgb(W->sun);
    circle(sx, sy, 44, sc, 22);
    circle(sx, sy, 24, sc, 36);
    circle(sx, sy, 12, sc, 80);
    circle(sx, sy, 6, rgb(mixHex(W->sun, 0xffffff, 0.7f)));
  }
  // The cloud band: each column's azimuth picks the texture's x, the height
  // above the tilted horizon its y; lit on top, shaded beneath.
  float cr, cg, cb, sr, sg, sb;
  unhex(W->cloud, cr, cg, cb);
  unhex(mixHex(W->skyHorizon, W->shade, 0.35f), sr, sg, sb);
  float shear = tanf(s_cam.roll);
  for (int i = 0; i < FW; i++) {
    float az = s_cam.yaw + atanf((i + 0.5f - FW / 2) / SCALE) + s_t * 0.004f;
    int cx = ((int)(az / (2 * PI_F) * CW * 3 + 100000 * CW)) % CW;
    float hz = s_cam.horizon + (i + 0.5f - FW / 2) * shear;
    for (int y = (int)fmaxf(0, hz - 70); y < (int)fminf(FH, hz - 2); y++) {
      int cy = (int)((hz - y) * 0.56f);
      if (cy >= CH) continue;
      int a = s_clouds[cy * CW + cx];
      if (a < 8) continue;
      int a2 = cy + 2 < CH ? s_clouds[(cy + 2) * CW + cx] : 0; // more cloud above: the underside
      float k = clampf(0.62f + (a - a2) / 140.0f, 0.15f, 1);
      uint16_t col = rgb565(lerpf(sr, cr, k), lerpf(sg, cg, k), lerpf(sb, cb, k));
      uint16_t &p = s_frame[y * FW + i];
      p = blend(p, col, (uint8_t)(a * 0.92f));
    }
  }
}

// ---------------------------------------------------------------------------
// The terrain: a heightmap ray-caster
// ---------------------------------------------------------------------------
//
// All screen columns advance together, depth by depth: at distance z the
// rays cross the map along one line, L = C + f z -/+ r z tanF, so
// neighbouring columns read neighbouring texels. A texel at height h lands
// at y = horizon + roll shear + (camera height - h) SCALE / z and fills its
// column down to what nearer ground already covered (a y-buffer). Close up
// four texels are blended; each span shades from its own colour to the
// nearer sample's below it. Air: haze F = 1 - exp(-k z / zfar) toward the
// world's haze colour, plus mist that lies low, m = mist e^{-(h - sea)/16}
// growing with distance. Water marks its pixels for the reflections.

static uint32_t s_frameNo = 0;

// Per-frame constants shared by both halves of the screen.
struct TerrainFrame {
  float fx, fy, rx, ry, shear;
  int fR, fG, fB, mR, mG, mB, sR, sG, sB, glintLo, glintHi, ro, ro2, ripAmp;
  uint8_t mistTab[64];
};
static TerrainFrame s_tf;

// 565 -> a spread form with gaps between the channels, so all three blend
// with one multiply: x = (c | c << 16) & 0x07E0F81F.
static inline uint32_t spread(uint32_t c) { c &= 0xffff; return (c | (c << 16)) & 0x07E0F81F; }
static inline uint32_t lerpSpread(uint32_t a, uint32_t b, uint32_t w) { return ((a * (32 - w) + b * w) >> 5) & 0x07E0F81F; }

// Ray-cast screen columns [i0, i1). Fixed point throughout: positions in
// 1/256 texel, heights in 1/256 unit, colours blended spread.
static void drawTerrainCols(int i0, int i1) {
  const TerrainFrame &T = s_tf;
  const Cam &c = s_cam;
  int16_t ybuf[FW], pR[FW], pG[FW], pB[FW];
  float hcol[FW];
  for (int i = i0; i < i1; i++) { ybuf[i] = FH; pR[i] = -1; hcol[i] = c.horizon + (i + 0.5f - FW / 2) * T.shear; }
  const float OFF = 65536.0f;
  float z = 1.4f, dz = 0.42f;
  int open = i1 - i0;
  while (z < ZFAR && open > 0) {
    const float invz = SCALE / z, half = z * TANF;
    const float lx0 = c.x + T.fx * z - T.rx * half + OFF, ly0 = c.y + T.fy * z - T.ry * half + OFF;
    const float stx = T.rx * 2 * half / FW, sty = T.ry * 2 * half / FW;
    const int lvl = z > 250 ? 2 : z > 105 ? 1 : 0;
    const uint32_t *map = lvl == 2 ? s_mip2 : lvl == 1 ? s_mip1 : s_map;
    const int lm = (N >> lvl) - 1, lsh = 9 - lvl; // row shift: width = 2^lsh
    // Texel coordinates at this level, in 1/256 of a texel.
    int32_t px = (int32_t)((lx0 + stx * (i0 + 0.5f)) * (256 >> lvl)), py = (int32_t)((ly0 + sty * (i0 + 0.5f)) * (256 >> lvl));
    const int32_t dpx = (int32_t)(stx * (256 >> lvl)), dpy = (int32_t)(sty * (256 >> lvl));
    const float zf = z / ZFAR;
    const int fa = (int)(255 * (1 - expf(-W->fogDensity * zf)));
    const int mz = (int)(256 * powf(zf, 0.6f));
    const int wa = 40 + (int)(150 * zf);
    const uint8_t db = (uint8_t)(zf * 250) & 0xFE; // even; odd marks water
    const float camOff = c.h * invz, hk = invz / 256;
    open = 0;
    for (int i = i0; i < i1; i++, px += dpx, py += dpy) {
      int yb = ybuf[i];
      if (yb <= 0) continue;
      open++;
      int jx = px >> 8, jy = py >> 8, u = px & 255, v = py & 255;
      const uint32_t *r0 = map + ((jy & lm) << lsh), *r1 = map + (((jy + 1) & lm) << lsh);
      uint32_t m00 = r0[jx & lm], m10 = r0[(jx + 1) & lm], m01 = r1[jx & lm], m11 = r1[(jx + 1) & lm];
      int h00 = (m00 >> 16) & 255, h10 = (m10 >> 16) & 255, h01 = (m01 >> 16) & 255, h11 = (m11 >> 16) & 255;
      int ha = (h00 << 8) + (h10 - h00) * u, hb = (h01 << 8) + (h11 - h01) * u;
      int hf = ha + (((hb - ha) * v) >> 8); // height, 1/256 units
      uint32_t u5 = u >> 3, v5 = v >> 3;
      uint32_t cs = lerpSpread(lerpSpread(spread(m00), spread(m10), u5), lerpSpread(spread(m01), spread(m11), u5), v5);
      int R = (cs >> 11) & 31, G = (cs >> 21) & 63, B = cs & 31;
      bool water = hf < (SEA << 8);
      int top = (int)(hcol[i] + camOff - (water ? (SEA << 8) : hf) * hk);
      if (top >= yb) continue;
      if (top < 0) top = 0;
      int wx = jx << lvl, wy = jy << lvl; // level-0 texel, for ripples and glints
      if (water) {
        R += ((T.sR - R) * wa) >> 8; G += ((T.sG - G) * wa) >> 8; B += ((T.sB - B) * wa) >> 8;
        int rp = s_ripple[((wy + T.ro) & 63) * 64 + ((wx + T.ro2) & 63)] + s_ripple[((wy - T.ro2 + 17) & 63) * 64 + ((wx - T.ro + 31) & 63)] - 256;
        rp = rp * T.ripAmp >> 4;
        R += rp >> 5; G += rp >> 4; B += rp >> 5;
        if (i > T.glintLo && i < T.glintHi && ((((wx * 7 + wy * 13 + (int)(s_frameNo >> 2)) * 2654435761u) >> 27) == 0)) { R = 31; G = 62; B = 30; }
      }
      int hm = (hf >> 8) - SEA;
      int ma = (T.mistTab[hm < 0 ? 0 : hm > 63 ? 63 : hm] * mz) >> 8;
      R += ((T.mR - R) * ma) >> 8; G += ((T.mG - G) * ma) >> 8; B += ((T.mB - B) * ma) >> 8;
      R += ((T.fR - R) * fa) >> 8; G += ((T.fG - G) * fa) >> 8; B += ((T.fB - B) * fa) >> 8;
      R = R < 0 ? 0 : R > 31 ? 31 : R;
      G = G < 0 ? 0 : G > 63 ? 63 : G;
      B = B < 0 ? 0 : B > 31 ? 31 : B;
      uint16_t *p = s_frame + top * FW + i;
      uint8_t *d = s_depth + top * FW + i;
      int n = yb - top;
      uint8_t dw = water ? db | 1 : db;
      if (pR[i] < 0 || n < 2 || water) {
        uint16_t col = (uint16_t)((R << 11) | (G << 5) | B);
        for (int y = 0; y < n; y++, p += FW, d += FW) { *p = col; *d = dw; }
      } else {
        // Shade down the span to the nearer sample below: cliffs read as
        // surfaces, not stretched stripes.
        int r8 = R << 8, g8 = G << 8, b8 = B << 8;
        int dr = ((pR[i] << 8) - r8) / n, dg = ((pG[i] << 8) - g8) / n, dbb = ((pB[i] << 8) - b8) / n;
        for (int y = 0; y < n; y++, p += FW, d += FW, r8 += dr, g8 += dg, b8 += dbb) {
          *p = (uint16_t)(((r8 >> 8) << 11) | ((g8 >> 8) << 5) | (b8 >> 8));
          *d = dw;
        }
      }
      pR[i] = R; pG[i] = G; pB[i] = B;
      ybuf[i] = top;
    }
    z += dz;
    dz *= 1.018f;
  }
}


static void drawTerrain() {
  TerrainFrame &T = s_tf;
  T.shear = tanf(s_cam.roll);
  for (int k = 0; k < 64; k++) T.mistTab[k] = (uint8_t)(255 * W->mistAmt * expf(-k / 16.0f));
  T.fx = cosf(s_cam.yaw); T.fy = sinf(s_cam.yaw); T.rx = -T.fy; T.ry = T.fx;
  float fr, fg, fb, mr, mg, mb, sr, sg, sb;
  unhex(W->haze, fr, fg, fb);
  unhex(W->mist, mr, mg, mb);
  unhex(W->skyHorizon, sr, sg, sb);
  T.fR = (int)fr >> 3; T.fG = (int)fg >> 2; T.fB = (int)fb >> 3;
  T.mR = (int)mr >> 3; T.mG = (int)mg >> 2; T.mB = (int)mb >> 3;
  T.sR = (int)sr >> 3; T.sG = (int)sg >> 2; T.sB = (int)sb >> 3;
  T.glintLo = s_sunCol - 18; T.glintHi = s_sunCol + 18;
  T.ro = (int)(s_t * 6); T.ro2 = (int)(s_t * 4.3f);
  T.ripAmp = (int)(W->ripple * 16);
  parallel(drawTerrainCols, FW);
}

static inline bool isWater(int y, int i) { return s_depth[y * FW + i] & 1; }

// Reflections, in screen space: a water pixel shows the pixel as far above
// the top of its stretch of water as it is below it - the far shore, the
// mountains and the sky, mirrored - wavering with the ripples, stronger
// toward the distance (Fresnel), darkened a touch.
static void drawReflections() {
  static int8_t wob[FH];
  for (int y = 0; y < FH; y++) wob[y] = (int8_t)(sinf(y * 1.3f + s_t * 3.1f) * W->ripple * 2.2f);
  const int base = (int)(W->reflect * 255);
  for (int i = 0; i < FW; i++) {
    int top = -1;
    for (int y = 0; y < FH; y++) {
      if (!isWater(y, i)) { top = -1; continue; }
      if (top < 0) top = y;
      int src = 2 * top - y - 1;
      if (src < 0) src = 0;
      int sx = i + wob[y];
      sx = sx < 0 ? 0 : sx >= FW ? FW - 1 : sx;
      uint16_t s = s_frame[src * FW + sx];
      uint16_t dark = (uint16_t)((((s >> 11) * 7 >> 3) << 11) | ((((s >> 5) & 63) * 7 >> 3) << 5) | ((s & 31) * 7 >> 3));
      float depthK = clampf(1 - (y - s_cam.horizon) / (FH - s_cam.horizon + 1) * 0.55f, 0.35f, 1);
      int a = (int)(base * depthK);
      uint16_t &p = s_frame[y * FW + i];
      p = blend(p, dark, (uint8_t)(a > 255 ? 255 : a));
    }
  }
}

// ---------------------------------------------------------------------------
// Things in the air: projection, depth-tested
// ---------------------------------------------------------------------------

static bool project(float wx, float wy, float wz, float &sx, float &sy, float &zc) {
  float dx = wx - s_cam.x, dy = wy - s_cam.y;
  float fx = cosf(s_cam.yaw), fy = sinf(s_cam.yaw);
  zc = dx * fx + dy * fy;
  float xc = -dx * fy + dy * fx;
  if (zc < NEAR) return false;
  sx = FW / 2 + xc / zc * SCALE;
  sy = s_cam.horizon + (s_cam.h - wz) / zc * SCALE + (sx - FW / 2) * tanf(s_cam.roll);
  return true;
}

static inline uint8_t depthByte(float z) { return (uint8_t)clampf(z / ZFAR * 250, 0, 252); }
static inline bool visibleAt(float sx, float sy, float zc) {
  int ix = (int)sx, iy = (int)sy;
  if (ix < 0 || iy < 0 || ix >= FW || iy >= FH) return true;
  return depthByte(zc) < s_depth[iy * FW + ix];
}
static float hazeOf(float zc) { return 1 - expf(-W->fogDensity * zc / ZFAR); }

static uint16_t litColor(uint32_t base, float nx, float ny, float nz, float zc) {
  const float LX = -cosf(W->sunEl), LY = -0.25f, LZ = sinf(W->sunEl) + 0.25f;
  float lit = 0.45f + 0.65f * fmaxf(0, (nx * LX + ny * LY + nz * LZ) / sqrtf(LX * LX + LY * LY + LZ * LZ));
  float r, g, b, fr, fg, fb, sr, sg, sb;
  unhex(base, r, g, b);
  unhex(W->haze, fr, fg, fb);
  unhex(W->sun, sr, sg, sb);
  float f = hazeOf(zc);
  r *= lit * sr / 255; g *= lit * sg / 255; b *= lit * sb / 255;
  return rgb565(lerpf(r, fr, f), lerpf(g, fg, f), lerpf(b, fb, f));
}

// --- wind rings --------------------------------------------------------------------------

struct Ring { bool on, passed; float x, y, z, dir, passT; };
static const int NRG = 6;
static Ring s_ring[NRG];
static float s_lastRX, s_lastRY, s_lastRDir, s_lastRZ;
static const float RING_R = 10;

// A ring of wind: 32 segments round a circle standing across the way, a
// soft glow and a bright core, each segment hidden where land is nearer.
// Passed, it widens and fades.
static void drawRing(const Ring &g) {
  float dx = g.x - s_cam.x, dy = g.y - s_cam.y;
  if (dx * dx + dy * dy > ZFAR * ZFAR) return;
  float grow = g.passed ? 1 + g.passT * 1.5f : 1, fade = g.passed ? clampf(1 - g.passT * 1.6f, 0, 1) : 1;
  if (fade <= 0) return;
  float ux = -sinf(g.dir), uy = cosf(g.dir);
  float px = 0, py = 0, pz = 0;
  bool pok = false;
  uint32_t core = g.passed ? 0xc8ffd8 : 0xfff2c8, glow = g.passed ? 0x80ffb0 : 0xffc870;
  for (int k = 0; k <= 32; k++) {
    float a = k * 2 * PI_F / 32 + s_t * 0.3f;
    float wx = g.x + ux * cosf(a) * RING_R * grow, wy = g.y + uy * cosf(a) * RING_R * grow, wz = g.z + sinf(a) * RING_R * grow;
    float sx = 0, sy = 0, zc = 0;
    bool ok = project(wx, wy, wz, sx, sy, zc) && zc > 3;
    if (ok && pok && visibleAt((sx + px) / 2, (sy + py) / 2, (zc + pz) / 2)) {
      float w = clampf(1.3f * SCALE / zc, 0.7f, 3.5f);
      float f = 1 - hazeOf(zc) * 0.85f;
      line(px, py, sx, sy, w * 2.4f, rgb(glow), (uint8_t)(70 * f * fade));
      line(px, py, sx, sy, w * 0.8f, rgb(core), (uint8_t)(230 * f * fade));
    }
    px = sx; py = sy; pz = zc; pok = ok;
  }
}

// Where the next ring goes: a turn of up to half a radian, the choice of
// five that keeps lowest over the ground (so the way tends down valleys),
// raised clear of everything near it.
static void spawnRing(int k) {
  float bestDir = s_lastRDir, bestH = 1e9f;
  for (int c = 0; c < 5; c++) {
    float d = s_lastRDir + frange(-0.55f, 0.55f);
    float h = 0;
    for (float s = 40; s <= 150; s += 22) h = fmaxf(h, groundAt(s_lastRX + cosf(d) * s, s_lastRY + sinf(d) * s));
    if (h < bestH) { bestH = h; bestDir = d; }
  }
  float dist = frange(120, 165);
  Ring &g = s_ring[k];
  g.on = true; g.passed = false; g.passT = 0;
  g.dir = bestDir;
  g.x = s_lastRX + cosf(bestDir) * dist;
  g.y = s_lastRY + sinf(bestDir) * dist;
  float clear = 0;
  for (int i = -2; i <= 2; i++)
    for (int j = -2; j <= 2; j++) clear = fmaxf(clear, groundAt(g.x + i * 6, g.y + j * 6));
  g.z = clampf(fmaxf(clear + 30, SEA + 34) + frange(0, 18), 0, 240);
  s_lastRX = g.x; s_lastRY = g.y; s_lastRDir = bestDir; s_lastRZ = g.z;
}

// --- lanterns and boats ------------------------------------------------------------------

struct Lantern { bool on; float x, y, z, ph; };
static const int NL = 16;
static Lantern s_lan[NL];
struct Boat { bool on; float x, y, dir, ph; };
static const int NBT = 8;
static Boat s_boat[NBT];

static void spawnLanterns(float x0, float y0, float x1, float y1, float a0, float a1) {
  int n = 2 + (int)(rnd() % 3);
  for (int k = 0; k < n; k++) {
    float t = (k + 1) / (float)(n + 1);
    float x = lerpf(x0, x1, t) + frange(-8, 8), y = lerpf(y0, y1, t) + frange(-8, 8);
    float z = fmaxf(lerpf(a0, a1, t) + frange(-3, 8), groundAt(x, y) + 9);
    for (Lantern &l : s_lan)
      if (!l.on) { l = {true, x, y, z, frand() * 6}; break; }
  }
}

static void drawLantern(const Lantern &l) {
  float sx, sy, zc;
  float bob = sinf(s_t * 1.7f + l.ph) * 1.2f;
  if (!project(l.x, l.y, l.z + bob, sx, sy, zc) || zc > ZFAR || zc < 7) return;
  if (sx < -10 || sx > FW + 10 || sy < -10 || sy > FH + 10 || !visibleAt(sx, sy, zc)) return;
  float r = fminf(2.4f * SCALE / zc, 6.5f);
  float fog = 1 - 0.8f * hazeOf(zc), flick = 0.9f + 0.1f * sinf(s_t * 13 + l.ph * 3);
  circle(sx, sy, r * 3.6f, rgb(0xffa040), (uint8_t)(32 * fog * flick));
  circle(sx, sy, r * 2.0f, rgb(0xffc060), (uint8_t)(60 * fog * flick));
  if (r < 1.6f) { circle(sx, sy, 1.2f, rgb(0xffd080), (uint8_t)(255 * fog)); return; }
  ellipse(sx, sy, r * 0.82f, r, rgb(0xf07a28), (uint8_t)(255 * fog));
  ellipse(sx, sy + r * 0.1f, r * 0.5f, r * 0.72f, rgb(0xffc860), (uint8_t)(230 * fog * flick));
  rect(sx - r * 0.45f, sy - r * 1.05f, r * 0.9f, r * 0.22f, rgb(0x5a2a18), (uint8_t)(255 * fog));
  rect(sx - r * 0.5f, sy + r * 0.85f, r * 1.0f, r * 0.22f, rgb(0x5a2a18), (uint8_t)(255 * fog));
}

static void placeBoats() {
  for (Boat &b : s_boat) {
    if (b.on) continue;
    if (!W->boats && !W->sails) continue;
    for (int tries = 0; tries < 60; tries++) {
      float a = frand() * 2 * PI_F, d = frange(80, 420);
      float x = s_x + cosf(a) * d, y = s_y + sinf(a) * d;
      if (heightAt((int)x, (int)y) < SEA - 4) { b = {true, x, y, frand() * 2 * PI_F, frand() * 6}; break; }
    }
  }
}

// A boat on the water: a white sail over a dark hull, or a long painted
// shikara under its canopy - drawn upright at its spot, sized by distance.
static void drawBoat(const Boat &b) {
  float sx, sy, zc;
  if (!project(b.x, b.y, SEA, sx, sy, zc) || zc > ZFAR * 0.8f || zc < 6) return;
  if (sx < -20 || sx > FW + 20 || sy < 0 || sy > FH + 10 || !visibleAt(sx, sy - 1, zc)) return;
  float k = SCALE / zc, f = 1 - 0.85f * hazeOf(zc);
  float side = fabsf(sinf(b.dir - s_cam.yaw)) * 0.7f + 0.3f; // seen broadside, longer
  uint8_t a = (uint8_t)(255 * f);
  if (W->sails) {
    float hl = 2.2f * k * side;
    float hull[8] = {sx - hl, sy - 0.8f * k, sx + hl, sy - 0.8f * k, sx + hl * 0.7f, sy, sx - hl * 0.7f, sy};
    poly(hull, 4, rgb(0x283850), a);
    float sail[6] = {sx - 0.2f * k, sy - 0.9f * k, sx - 0.2f * k, sy - 5.5f * k, sx + 1.8f * k * side, sy - 1.1f * k};
    poly(sail, 3, rgb(0xfbfbf6), a);
    ellipse(sx, sy + 0.4f * k, hl * 1.5f, 0.4f * k, rgb(0xffffff), (uint8_t)(70 * f)); // its wake
  } else {
    float hl = 3.2f * k * side;
    float hull[8] = {sx - hl, sy - 1.2f * k, sx + hl, sy - 1.2f * k, sx + hl * 0.8f, sy, sx - hl * 0.8f, sy};
    poly(hull, 4, rgb(0xd8a028), a);
    rect(sx - hl * 0.8f, sy - 1.4f * k, hl * 1.6f, 0.35f * k, rgb(0xa83a20), a);
    rect(sx - hl * 0.45f, sy - 3.0f * k, hl * 0.9f, 0.5f * k, rgb(0x3a8ad8), a);
    rect(sx - 0.2f * k, sy - 2.6f * k, 0.5f * k, 1.4f * k, rgb(0x5a2a18), a);
  }
}

// --- the heron -------------------------------------------------------------------------

// The white heron, in its own frame (x forward, y right, z up), after
// photographs of herons in flight: broad wings in three parts (shoulder,
// elbow, wrist) with pale coverts, grey secondaries, a white wrist patch
// and five dark primaries fanned like fingers; a short body with a white
// belly; the neck folded back in an S, a black crown stripe and an orange
// bill; the legs trailing behind. Slow, deep wingbeats bend each wing at
// the elbow and the wrist and fold it a little on the upstroke.
static void drawHeron(float cx, float cy, float cz, float yaw, float bank, float pitch, float flap) {
  const float S = 1.12f;
  float b = sinf(flap);
  float wa = b * 0.9f, wb = b * 1.9f, wc = b * 2.8f;       // elbow, wrist, tip lift
  float fold = b > 0 ? 1 - 0.12f * b : 1;                   // a little in on the upstroke
  static float V[64][3];
  int nv = 0;
  auto add = [&](float x, float y, float z) { V[nv][0] = x; V[nv][1] = y; V[nv][2] = z; return nv++; };
  // Body and head.
  int C = add(1.5f, 0, 0.25f), Tp = add(-0.2f, 0, 0.9f), Rp = add(-2.3f, 0, 0.35f), Bl = add(-0.3f, 0, -0.5f);
  int SL = add(-0.2f, -0.8f, 0.2f), SR = add(-0.2f, 0.8f, 0.2f);
  int N1 = add(1.9f, 0, -0.35f), N2 = add(2.6f, 0, 0.3f), NL = add(2.2f, -0.32f, 0.25f), NR = add(2.2f, 0.32f, 0.25f);
  int Hd = add(2.95f, 0, 0.62f), HT = add(2.8f, 0, 1.02f), HL = add(2.95f, -0.24f, 0.78f), HR = add(2.95f, 0.24f, 0.78f);
  int BT = add(5.0f, 0, 0.5f), BU = add(3.2f, 0, 0.5f);
  int TL = add(-3.3f, -0.65f, 0.42f), TR = add(-3.3f, 0.65f, 0.42f), TC = add(-3.5f, 0, 0.5f);
  int HipL = add(-1.9f, -0.15f, 0.2f), HipR = add(-1.9f, 0.15f, 0.2f), FtL = add(-5.0f, -0.12f, 0.45f), FtR = add(-5.0f, 0.12f, 0.45f);
  // Wings.
  int Sh[2], Q1[2], El[2], Q2[2], Wr[2], Q3[2], M1[2], M2[2], M3[2], Pb[2][6], Pt[2][5];
  for (int w = 0; w < 2; w++) {
    float s = w ? 1.0f : -1.0f;
    Sh[w] = add(0.55f, s * 0.6f, 0.68f);
    Q1[w] = add(-1.8f, s * 0.65f, -0.1f);
    El[w] = add(0.2f, s * 3.4f, 0.8f + wa);
    Q2[w] = add(-2.4f, s * 3.5f, 0.0f + wa);
    Wr[w] = add(0.55f, s * 5.6f * fold, 0.9f + wb);
    Q3[w] = add(-2.1f, s * 5.9f * fold, 0.1f + wb);
    // Midpoints across the chord: pale coverts in front, grey behind.
    auto mid = [&](int a, int c) { return add((V[a][0] + V[c][0]) / 2, (V[a][1] + V[c][1]) / 2, (V[a][2] + V[c][2]) / 2); };
    M1[w] = mid(Sh[w], Q1[w]); M2[w] = mid(El[w], Q2[w]); M3[w] = mid(Wr[w], Q3[w]);
    // Five primaries: bases along the wrist's trailing line, tips fanned
    // from pointing outward to pointing back.
    for (int k = 0; k <= 5; k++) {
      float t = k / 5.0f;
      Pb[w][k] = add(lerpf(0.55f, -2.1f, t), s * lerpf(5.6f, 5.9f, t) * fold, lerpf(0.9f, 0.1f, t) + wb);
    }
    for (int k = 0; k < 5; k++) {
      float a = lerpf(0.18f, 1.15f, k / 4.0f), len = lerpf(3.3f, 2.3f, k / 4.0f);
      float bx = V[Pb[w][k]][0], by = V[Pb[w][k]][1];
      Pt[w][k] = add(bx - sinf(a) * len, by + s * cosf(a) * len * fold, V[Pb[w][k]][2] + (wc - wb) * (1 - k * 0.12f) - 0.15f);
    }
  }
  float sx[64], sy[64], sz[64], wx[64], wy[64], wz[64];
  float cbk = cosf(bank), sbk = sinf(bank), cp = cosf(pitch), sp = sinf(pitch), cyw = cosf(yaw), syw = sinf(yaw);
  for (int i = 0; i < nv; i++) {
    float x = V[i][0] * S, y = V[i][1] * S, z = V[i][2] * S;
    float y1 = y * cbk - z * sbk, z1 = y * sbk + z * cbk;
    float x2 = x * cp - z1 * sp, z2 = x * sp + z1 * cp;
    wx[i] = cx + x2 * cyw - y1 * syw;
    wy[i] = cy + x2 * syw + y1 * cyw;
    wz[i] = cz + z2;
    if (!project(wx[i], wy[i], wz[i], sx[i], sy[i], sz[i])) return;
  }
  // The legs first: they trail behind and below everything else.
  uint16_t leg = rgb(0x4a3426);
  for (int k = 0; k < 2; k++) {
    int h = k ? HipR : HipL, f = k ? FtR : FtL;
    float w = clampf(0.6f * SCALE / sz[h] / 6, 0.5f, 1.1f);
    line(sx[h], sy[h], sx[f], sy[f], w, leg);
    line(sx[f], sy[f], sx[f] + (sx[f] - sx[h]) * 0.06f, sy[f] + (sy[f] - sy[h]) * 0.06f + 0.6f, w * 1.4f, leg);
  }
  struct T { int a, b, c; uint32_t col; bool feather; float d; };
  const uint32_t COV = 0xe6eaf0, SEC = 0xa4b0c2, PRI = 0x4a5670, TOP = 0xd2d8e0, BELLY = 0xf4f4f2, NECK = 0xf2f2f0,
                 CROWN = 0x16181e, BILL = 0xf2a428, TAIL = 0x96a0b0, PATCH = 0xffffff;
  static T tri[96];
  int nt = 0;
  auto tr = [&](int a, int b2, int c, uint32_t col, bool f = false) { tri[nt++] = {a, b2, c, col, f, 0}; };
  for (int w = 0; w < 2; w++) {
    tr(Sh[w], El[w], M2[w], COV); tr(Sh[w], M2[w], M1[w], COV);    // inner wing, front
    tr(M1[w], M2[w], Q2[w], SEC); tr(M1[w], Q2[w], Q1[w], SEC);    // and back
    tr(El[w], Wr[w], M3[w], COV); tr(El[w], M3[w], M2[w], COV);    // outer wing, front
    tr(M2[w], M3[w], Q3[w], SEC); tr(M2[w], Q3[w], Q2[w], SEC);    // and back
    tr(Wr[w], Pb[w][1], El[w], PATCH);                              // the white wrist
    for (int k = 0; k < 5; k++) tr(Pb[w][k], Pb[w][k + 1], Pt[w][k], PRI, true);
  }
  tr(C, Tp, SL, TOP); tr(C, Tp, SR, TOP); tr(Tp, Rp, SL, TOP); tr(Tp, Rp, SR, TOP);
  tr(C, Bl, SL, BELLY); tr(C, Bl, SR, BELLY); tr(Bl, Rp, SL, BELLY); tr(Bl, Rp, SR, BELLY);
  tr(Rp, TL, TC, TAIL); tr(Rp, TR, TC, TAIL);
  tr(C, N1, NL, NECK); tr(C, N1, NR, NECK); tr(N1, N2, NL, NECK); tr(N1, N2, NR, NECK);
  tr(N2, Hd, NL, NECK); tr(N2, Hd, NR, NECK);
  tr(Hd, HT, HL, NECK); tr(Hd, HT, HR, NECK);
  tr(HT, HL, BU, CROWN); tr(HT, HR, BU, CROWN);                    // the crown stripe
  tr(HL, BT, Hd, BILL); tr(HR, BT, Hd, BILL);
  for (int i = 0; i < nt; i++) tri[i].d = sz[tri[i].a] + sz[tri[i].b] + sz[tri[i].c];
  for (int i = 1; i < nt; i++) // far to near
    for (int j = i; j > 0 && tri[j].d > tri[j - 1].d; j--) { T k = tri[j]; tri[j] = tri[j - 1]; tri[j - 1] = k; }
  uint16_t edge = rgb(0x2c3446);
  for (int i = 0; i < nt; i++) {
    const T &t = tri[i];
    float ax = wx[t.b] - wx[t.a], ay = wy[t.b] - wy[t.a], az = wz[t.b] - wz[t.a];
    float bx = wx[t.c] - wx[t.a], by = wy[t.c] - wy[t.a], bz = wz[t.c] - wz[t.a];
    float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx, nl = sqrtf(nx * nx + ny * ny + nz * nz) + 1e-6f;
    nx /= nl; ny /= nl; nz /= nl;
    if (nz < 0) { nx = -nx; ny = -ny; nz = -nz; }
    float p[6] = {sx[t.a], sy[t.a], sx[t.b], sy[t.b], sx[t.c], sy[t.c]};
    poly(p, 3, litColor(t.col, nx, ny, nz, 0));
    if (t.feather) line(p[0], p[1], p[4], p[5], 0.5f, edge, 110); // each primary's leading edge
  }
}

// ---------------------------------------------------------------------------
// Flight
// ---------------------------------------------------------------------------

static void setMode(Mode m) { s_mode = m; s_modeT = 0; }

static void newFlight() {
  // Start over the lowest place found (water, if there is much of it).
  float bestH = 1e9f, bx = 4096, by = 4096;
  for (int tries = 0; tries < 200; tries++) {
    float x = 4096 + frand() * N, y = 4096 + frand() * N;
    float h = groundAt(x, y);
    if (h < bestH) { bestH = h; bx = x; by = y; }
  }
  s_x = bx; s_y = by;
  s_yaw = frand() * 2 * PI_F;
  s_alt = bestH + 44;
  s_bank = 0; s_vz = 0; s_speed = 32; s_wind = 1; s_dist = 0;
  s_wishes = 0; s_rings = 0; s_clock = 0; s_newBest = false;
  memset(s_ring, 0, sizeof(s_ring));
  memset(s_lan, 0, sizeof(s_lan));
  memset(s_boat, 0, sizeof(s_boat));
  s_lastRX = s_x; s_lastRY = s_y; s_lastRDir = s_yaw; s_lastRZ = s_alt;
  for (int k = 0; k < NRG; k++) {
    float px = s_lastRX, py = s_lastRY, pz = s_lastRZ;
    spawnRing(k);
    spawnLanterns(px, py, s_ring[k].x, s_ring[k].y, pz, s_ring[k].z);
  }
  placeBoats();
  s_cam = {s_x - cosf(s_yaw) * 22, s_y - sinf(s_yaw) * 22, s_alt + 8, s_yaw, 0, FH * 0.30f};
}

static void crash(const char *why) {
  s_why = why;
  RunAudio::fx(RunAudio::Thunder);
  setMode(Mode::Dying);
  if (s_auto) return; // the bench pilot's flights aren't kept
  if ((uint32_t)s_wishes > s_best[s_world]) {
    s_best[s_world] = s_wishes;
    s_newBest = true;
    char k[3] = {'b', (char)('0' + s_world), 0};
    s_prefs.putUInt(k, s_wishes);
  }
}

// The pilot (title screen and bench): steer for the next ring, stay well
// clear of the ground ahead.
static void pilot(float &steer, bool &up, bool &down) {
  const Ring *g = nullptr;
  float best = 1e9f;
  for (const Ring &q : s_ring) {
    if (!q.on || q.passed) continue;
    float dx = q.x - s_x, dy = q.y - s_y, d = dx * dx + dy * dy;
    float ahead = dx * cosf(s_yaw) + dy * sinf(s_yaw);
    if (ahead > -5 && d < best) { best = d; g = &q; }
  }
  float want = s_alt;
  if (g) {
    steer = clampf(wrapAng(atan2f(g->y - s_y, g->x - s_x) - s_yaw) * 2.2f, -1, 1);
    want = g->z;
  } else {
    steer = 0;
  }
  float clear = 0;
  for (float s = 10; s <= 90; s += 16) clear = fmaxf(clear, groundAt(s_x + cosf(s_yaw) * s, s_y + sinf(s_yaw) * s));
  want = fmaxf(want, clear + 30);
  up = s_alt < want - 2;
  down = s_alt > want + 3;
}

static void updateFlight(float dt, bool control) {
  float steer = s_steer, wheel = s_wheelSteer;
  bool up = s_up, down = s_down;
  if (!control || s_auto) { pilot(steer, up, down); wheel = 0; }
  steer = clampf(steer + wheel, -1, 1);
  s_wheelSteer *= expf(-dt * 5);
  // Bank into the turn; the turn follows the bank, as on real wings.
  s_bank += (steer * 0.75f - s_bank) * fminf(1, dt * 4);
  s_yaw = wrapAng(s_yaw + s_bank * 1.5f * dt);
  float vzT = up ? 24 : down ? -28 : 0;
  if (s_wind <= 0 && vzT > -9) vzT = -9; // no wind: she can only glide down
  s_vz += (vzT - s_vz) * fminf(1, dt * 3);
  s_alt = fminf(s_alt + s_vz * dt, 245);
  s_flap += dt * (up ? 6.5f : 3.4f);
  float px = s_x, py = s_y;
  s_x += cosf(s_yaw) * s_speed * dt;
  s_y += sinf(s_yaw) * s_speed * dt;
  s_dist += s_speed * dt;
  s_wind = control ? fmaxf(0, s_wind - dt * 0.05f) : 1;

  // Rings: crossing a ring's plane inside it.
  for (Ring &g : s_ring) {
    if (!g.on) continue;
    if (g.passed) { g.passT += dt; continue; }
    float cv = cosf(g.dir), sv = sinf(g.dir);
    float v0 = (px - g.x) * cv + (py - g.y) * sv, v1 = (s_x - g.x) * cv + (s_y - g.y) * sv;
    if (v0 < 0 && v1 >= 0) {
      float u = -(s_x - g.x) * sv + (s_y - g.y) * cv, w = s_alt - g.z;
      g.passed = true;
      if (u * u + w * w < RING_R * RING_R) {
        s_rings++;
        s_wind = fminf(1, s_wind + 0.38f);
        s_speed = fminf(60, s_speed + 0.8f);
        RunAudio::fx(RunAudio::DingTop);
        note("On the wind!");
      } else {
        g.passT = 9; // missed: it simply goes
        RunAudio::fx(RunAudio::Bonk);
      }
    }
  }
  for (int k = 0; k < NRG; k++) {
    Ring &g = s_ring[k];
    float dx = g.x - s_x, dy = g.y - s_y;
    float ahead = dx * cosf(s_yaw) + dy * sinf(s_yaw);
    if (g.on && (ahead < -60 || dx * dx + dy * dy > 900 * 900)) {
      float px2 = s_lastRX, py2 = s_lastRY, pz = s_lastRZ;
      spawnRing(k);
      spawnLanterns(px2, py2, g.x, g.y, pz, g.z);
    }
  }
  for (Lantern &l : s_lan) {
    if (!l.on) continue;
    float dx = l.x - s_x, dy = l.y - s_y, dz = l.z - s_alt;
    if (dx * dx + dy * dy + dz * dz < 7.5f * 7.5f) {
      l.on = false;
      s_wishes++;
      s_wind = fminf(1, s_wind + 0.07f);
      RunAudio::fx(RunAudio::DingHigh);
    } else if (dx * dx + dy * dy > 700 * 700) {
      l.on = false;
    }
  }
  for (Boat &b : s_boat) {
    if (!b.on) continue;
    float nx = b.x + cosf(b.dir) * dt * 3, ny = b.y + sinf(b.dir) * dt * 3;
    if (heightAt((int)nx, (int)ny) < SEA - 3) { b.x = nx; b.y = ny; } else b.dir += 1.3f;
    float dx = b.x - s_x, dy = b.y - s_y;
    if (dx * dx + dy * dy > 520 * 520) b.on = false;
  }
  if ((s_frameNo & 63) == 0) placeBoats();
  if (control && s_mode == Mode::Play) {
    float g = groundAt(s_x, s_y);
    if (s_alt < g + 1.2f) crash(g <= SEA + 0.5f ? "She dipped into the water." : "She brushed the land.");
  }
}

static void updateCamera(float dt) {
  float k = fminf(1, dt * 3.2f);
  s_cam.yaw = s_cam.yaw + wrapAng(s_yaw - s_cam.yaw) * k;
  float tx = s_x - cosf(s_cam.yaw) * 22, ty = s_y - sinf(s_cam.yaw) * 22;
  s_cam.x += (tx - s_cam.x) * fminf(1, dt * 6);
  s_cam.y += (ty - s_cam.y) * fminf(1, dt * 6);
  float th = fmaxf(s_alt + 8, groundAt(s_cam.x, s_cam.y) + 3);
  s_cam.h += (th - s_cam.h) * fminf(1, dt * 5);
  s_cam.roll += (-s_bank * 0.45f - s_cam.roll) * fminf(1, dt * 4);
  s_cam.horizon += (FH * 0.30f + s_vz * 0.35f - s_cam.horizon) * fminf(1, dt * 3);
}

// ---------------------------------------------------------------------------
// Drawing a frame
// ---------------------------------------------------------------------------

static uint32_t s_usTerrain = 0, s_usThings = 0, s_usPresent = 0;

static void drawWorld() {
  uint32_t t0 = micros();
  drawSky();
  drawTerrain();
  drawReflections();
  uint32_t t1 = micros();
  for (const Boat &b : s_boat) if (b.on) drawBoat(b);
  for (const Ring &g : s_ring) if (g.on) drawRing(g);
  for (const Lantern &l : s_lan) if (l.on) drawLantern(l);
  float gz = groundAt(s_x, s_y), ssx, ssy, szc;
  if (s_mode != Mode::Dying && project(s_x, s_y, gz, ssx, ssy, szc)) {
    float r = 5.5f * SCALE / szc;
    float a = clampf(1 - (s_alt - gz) / 90, 0.15f, 0.6f);
    ellipse(ssx, ssy, r, r * 0.32f, rgb(0x000000), (uint8_t)(110 * a));
  }
  float tum = s_mode == Mode::Dying ? s_modeT : 0;
  drawHeron(s_x, s_y, s_alt - tum * tum * 8, s_yaw + tum * 3, s_bank + tum * 5, -s_vz * 0.012f + tum * 2, s_flap);
  s_usTerrain += t1 - t0;
  s_usThings += micros() - t1;
}

static void drawHud() {
  char b[32];
  rect(0, 0, FW, 13, rgb(0x000000), 80);
  circle(7, 6.5f, 4, rgb(0xffb050), 120);
  rect(5, 4, 4, 5, rgb(0xff8a30));
  snprintf(b, sizeof(b), "%d", s_wishes);
  txt(14, 3, b, 1, rgb(0xfff0d0), false);
  snprintf(b, sizeof(b), "%d rings", s_rings);
  txtC(FW / 2, 3, b, 1, rgb(0xfff0d0), false);
  rect(FW - 58, 4, 52, 5, rgb(0x203040), 200);
  rect(FW - 58, 4, 52 * s_wind, 5, rgb(s_wind < 0.2f && fmodf(s_t, 0.5f) < 0.25f ? 0xff6a5a : 0x9ae0ff));
  if (s_noteT > 0 && s_mode == Mode::Play) txtC(FW / 2, 18, s_note, 1, rgb(0xfff0c8));
  if (s_wind <= 0 && s_mode == Mode::Play) txtC(FW / 2, FH - 14, "No wind! Find a ring", 1, rgb(0xff9a8a));
}

static void card(int x, int y, int w, int h) {
  roundRect(x, y, w, h, 7, rgb(0x0a0a18), 190);
  roundRect(x, y, w, 2, 1, rgb(0xff6a5a), 200);
}

static void render() {
  target(s_frame, FW, FH);
  setScale(1);
  s_frameNo++;
  drawWorld();
  char b[48];
  switch (s_mode) {
    case Mode::Title: {
      txtC(FW / 2, 10, "WHITE", 2, rgb(0xfff4e4));
      txtC(FW / 2, 28, "HERON", 3, rgb(0xf2a428));
      const World &w = WORLDS[s_pick];
      roundRect(24, FH - 40, FW - 48, 30, 6, rgb(0x0a0a18), 150);
      snprintf(b, sizeof(b), "<  %s  >", w.name);
      txtC(FW / 2, FH - 36, b, 1, rgb(0xffd890));
      if (s_best[s_pick]) snprintf(b, sizeof(b), "best %lu wishes", (unsigned long)s_best[s_pick]);
      else snprintf(b, sizeof(b), "%s", w.line);
      txtC(FW / 2, FH - 24, b, 1, rgb(0xe8e0f0));
      break;
    }
    case Mode::Story: {
      card(12, 8, FW - 24, FH - 16);
      txtC(FW / 2, 14, "THE LONG WAY HOME", 1, rgb(0xf2a428));
      int y = wrapC(FW / 2, 28,
                    "Each year people light paper lanterns and let them go, each one a wish. The white heron "
                    "gathers them on her way home across four lands.", 34, rgb(0xf4ecdc));
      y = wrapC(FW / 2, y + 4, "Ride the rings of wind; they keep her aloft. Every lantern is a wish.", 34, rgb(0xc8d8f0));
      wrapC(FW / 2, y + 4, "LEFT/RIGHT turn, UP/DOWN climb and dive. Never touch water or land.", 34, rgb(0xc8d8f0));
      txtC(FW / 2, FH - 18, "SELECT go", 1, rgb(0xffd890));
      break;
    }
    case Mode::Play:
    case Mode::Dying:
      drawHud();
      break;
    case Mode::Paused:
      drawHud();
      card(60, 44, FW - 120, 52);
      txtC(FW / 2, 52, "PAUSED", 2, rgb(0xfff0d0));
      txtC(FW / 2, 72, "SELECT go on", 1, rgb(0xd8c8f0));
      txtC(FW / 2, 82, "UP menu", 1, rgb(0xd8c8f0));
      break;
    case Mode::Over:
      card(30, 18, FW - 60, FH - 36);
      txtC(FW / 2, 24, s_newBest ? "A NEW BEST!" : "THE HERON RESTS", 1, rgb(0xff8a7a));
      txtC(FW / 2, 36, s_why, 1, rgb(0xd8c8f0));
      snprintf(b, sizeof(b), "%d", s_wishes);
      txtC(FW / 2, 50, b, 3, rgb(0xffd890));
      txtC(FW / 2, 76, s_wishes == 1 ? "wish" : "wishes", 1, rgb(0xf4ecdc));
      snprintf(b, sizeof(b), "%d rings, %d ri flown", s_rings, (int)(s_dist / 40));
      txtC(FW / 2, 90, b, 1, rgb(0xc8d8f0));
      snprintf(b, sizeof(b), "%s best %lu", W->name, (unsigned long)s_best[s_world]);
      txtC(FW / 2, 100, b, 1, rgb(0xc8d8f0));
      txtC(FW / 2, 112, "SELECT again  UP menu", 1, rgb(0xffd890));
      break;
  }
  uint32_t t2 = micros();
  HalfRes::present(s_panel, s_gfx->flipped(), s_frame);
  s_usPresent += micros() - t2;
}

static void buildWorld(int which) {
  uint32_t t0 = millis();
  makeWorld(which);
  makeSkyTables();
  Serial.printf("Crane: %s made in %lu ms\n", W->name, (unsigned long)(millis() - t0));
  newFlight();
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
  s_depth = (uint8_t *)heap_caps_malloc((size_t)FW * FH, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_depth) s_depth = (uint8_t *)heap_caps_malloc((size_t)FW * FH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_map = (uint32_t *)heap_caps_malloc((size_t)N * N * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_clouds = (uint8_t *)heap_caps_malloc(CW * CH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_mip1 = (uint32_t *)heap_caps_malloc((size_t)N * N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_mip2 = (uint32_t *)heap_caps_malloc((size_t)N * N / 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_frame || !s_depth || !s_map || !s_clouds || !s_mip1 || !s_mip2) {
    heap_caps_free(s_mip1); s_mip1 = nullptr;
    heap_caps_free(s_mip2); s_mip2 = nullptr;
    heap_caps_free(s_frame); s_frame = nullptr;
    heap_caps_free(s_depth); s_depth = nullptr;
    heap_caps_free(s_map); s_map = nullptr;
    heap_caps_free(s_clouds); s_clouds = nullptr;
    Serial.println("Crane: not enough memory");
    return false;
  }
#ifndef PC_SIM
  if (!s_worker) {
    s_go = xSemaphoreCreateBinary();
    s_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(workerTask, "crane3d", 6144, nullptr, 2, &s_worker, 0);
  }
#endif
  s_prefs.begin("crane", false);
  for (int i = 0; i < 4; i++) {
    char k[3] = {'b', (char)('0' + i), 0};
    s_best[i] = s_prefs.getUInt(k, 0);
  }
  s_rng = esp_random() | 1;
  s_pick = s_prefs.getUChar("world", 0) & 3;
  buildWorld(s_pick);
  s_t = 0;
  s_auto = false;
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
  heap_caps_free(s_depth); s_depth = nullptr;
  heap_caps_free(s_map); s_map = nullptr;
  heap_caps_free(s_clouds); s_clouds = nullptr;
  heap_caps_free(s_mip1); s_mip1 = nullptr;
  heap_caps_free(s_mip2); s_mip2 = nullptr;
  s_active = false;
}

bool active() { return s_active; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }
const uint16_t *frameBuffer() { return s_frame; }

// Bench: first press, the pilot flies the world on show; each press after
// makes and flies the next world.
void benchAutopilot() {
  if (s_auto) { s_pick = (s_world + 1) & 3; buildWorld(s_pick); }
  s_auto = true;
  newFlight();
  setMode(Mode::Play);
}

void pause() {
  if (s_mode == Mode::Play) setMode(Mode::Paused);
}

void key(const InputEvent &ev) {
  if (!ev.pressed || ev.repeat) return;
  switch (s_mode) {
    case Mode::Title:
      if (ev.key == Key::Left) s_pick = (s_pick + 3) & 3;
      else if (ev.key == Key::Right) s_pick = (s_pick + 1) & 3;
      else if (ev.key == Key::Select) {
        if (s_pick != s_world) { buildWorld(s_pick); s_prefs.putUChar("world", s_pick); }
        if (s_storySeen) { newFlight(); setMode(Mode::Play); }
        else setMode(Mode::Story);
      } else if (ev.key == Key::Up) {
        s_exit = true;
      }
      break;
    case Mode::Story:
      if (ev.key == Key::Select) { s_storySeen = true; newFlight(); setMode(Mode::Play); }
      else if (ev.key == Key::Up) setMode(Mode::Title);
      break;
    case Mode::Play:
      if (ev.key == Key::Select) pause();
      break;
    case Mode::Paused:
      if (ev.key == Key::Select) setMode(Mode::Play);
      else if (ev.key == Key::Up) { newFlight(); setMode(Mode::Title); }
      break;
    case Mode::Over:
      if (s_modeT < 0.6f) break;
      if (ev.key == Key::Select) { newFlight(); setMode(Mode::Play); }
      else if (ev.key == Key::Up) { newFlight(); setMode(Mode::Title); }
      break;
    default:
      break;
  }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  static uint32_t last = 0, perfAt = 0, frames = 0;
  uint32_t now = millis();
  float dt = last ? (now - last) / 1000.0f : 0.03f;
  last = now;
  if (dt > 0.1f) dt = 0.1f;
  s_t += dt;
  s_modeT += dt;
  if (s_noteT > 0) s_noteT -= dt;

  bool play = s_mode == Mode::Play;
  s_steer = (input.held(Key::Right) ? 1.0f : 0) - (input.held(Key::Left) ? 1.0f : 0);
  s_up = input.held(Key::Up);
  s_down = input.held(Key::Down);
  if (play) s_wheelSteer = clampf(s_wheelSteer + wheel * 0.18f, -1, 1);
  if (!play && touch.justPressed()) {
    InputEvent tap = {Key::Select, true, false};
    key(tap);
  }
  if (play || s_mode == Mode::Title || s_mode == Mode::Story) {
    s_clock += dt;
    updateFlight(dt, play);
    updateCamera(dt);
  } else if (s_mode == Mode::Dying && s_modeT > 1.6f) {
    setMode(Mode::Over);
  }
  if (s_auto && s_mode == Mode::Over && s_modeT > 2) { newFlight(); setMode(Mode::Play); }
  render();

  frames++;
  if (now - perfAt > 4000) {
    if (perfAt)
      Serial.printf("Crane: %.1f fps  terrain %lu  things %lu  present %lu us\n", frames * 1000.0f / (now - perfAt),
                    (unsigned long)(s_usTerrain / frames), (unsigned long)(s_usThings / frames), (unsigned long)(s_usPresent / frames));
    perfAt = now; frames = 0; s_usTerrain = s_usThings = s_usPresent = 0;
  }
}

#ifdef PC_SIM
const uint32_t *debugMap() { return s_map; }
#endif

} // namespace Crane
