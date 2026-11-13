// Gaslight & Griffins - paints each place's ground into one texture when you
// arrive: cobbles, grass, water with a line of foam at the shore, planks,
// pools of lamplight, and the shadows of the buildings. The renderer lays
// it flat over y = 0 and draws it a screen row at a time.
#pragma GCC optimize("O2")
#include "World.h"
#include <esp_heap_caps.h>
#include <Arduino.h>

namespace Gaslight {

static const int TPT = 12;                 // texels per tile
static const int MAXW = 40 * TPT, MAXH = 30 * TPT;
static uint16_t *s_tex = nullptr, *s_mip1 = nullptr, *s_mip2 = nullptr;

enum Kind : uint8_t { K_COBBLE, K_GRASS, K_LONG, K_WATER, K_SAND, K_PLANK, K_ROCK, K_FLOOR, K_CAVE, K_VOID };

static Kind kindOf(int area, int tx, int tz) {
  const AreaDef &a = AREAS[area];
  if (tx < 0 || tz < 0 || tx >= a.w || tz >= a.d) return K_VOID;
  char c = a.rows[tz][tx];
  if (a.indoor == 2) {
    if (c == '~') return K_WATER;
    if (c == '^') return K_ROCK;
    return K_CAVE;
  }
  if (a.indoor) return c == '^' ? K_ROCK : K_FLOOR;
  switch (c) {
    case ',': case 'T': case 'g': case 'N': return K_GRASS;
    case 'v': return K_LONG;
    case 'R': // sea stacks in town; boulders on dry land in the Wilds
      if (area == A_WILDS) return a.biome == 1 ? K_SAND : a.biome == 4 ? K_COBBLE : K_GRASS;
      return K_WATER;
    case '~': case 'P': return K_WATER;
    case ':': return K_SAND;
    case '=': return K_PLANK;
    case '^': return K_ROCK;
    default: return K_COBBLE;
  }
}

static uint8_t *s_noise = nullptr; // 128 x 128 random bytes
static inline float hf(int x, int y, int s) {
  return s_noise[((y + s * 37) & 127) * 128 + ((x * 3 + s * 91) & 127)] * (1.0f / 255);
}

static int s_biome = 0;
static void baseColour(Kind k, int gx, int gy, bool cave, float &r, float &g, float &b) {
  float n = hf(gx, gy, 1);
  uint32_t c;
  float m = 1.0f;
  switch (k) {
    case K_COBBLE: {
      int row = gy / 4, off = (row & 1) * 2, cx = (gx + off) / 5;
      bool mortar = gy % 4 == 0 || (gx + off) % 5 == 0;
      if (mortar) { c = 0x45433C; m = 0.9f + n * 0.15f; }
      else {
        c = 0x7E7A70;
        m = 0.8f + hf(cx, row, 7) * 0.32f + (n - 0.5f) * 0.08f;
        if (gy % 4 == 1 || (gx + off) % 5 == 1) m *= 1.1f; // the worn top edge catches light
      }
      break;
    }
    case K_GRASS:
      c = 0x4E6C3E;
      m = 0.82f + n * 0.3f;
      if (hf(gx, gy, 3) < 0.07f) { c = 0x6C8C4A; m = 1.0f; }
      if (hf(gx, gy, 4) < 0.003f) { c = 0xE0D8A8; m = 1.0f; }
      break;
    case K_LONG:
      c = 0x46643A;
      m = 0.75f + n * 0.35f;
      if ((gx + (gy >> 1)) % 5 == 0) m *= 0.8f;
      break;
    case K_WATER: {
      c = cave ? 0x0E1E2A : 0x1E3F52;
      float w = sinf(gx * 0.35f + sinf(gy * 0.21f) * 2.2f) + (n - 0.5f) * 0.6f;
      m = 0.92f + n * 0.1f;
      if (w > 0.92f) { c = cave ? 0x2A4A5A : 0x3E6A80; m = 1.0f; }
      break;
    }
    case K_SAND:
      c = 0x8E8574;
      m = 0.85f + n * 0.25f;
      if (hf(gx, gy, 5) < 0.1f) m *= 0.7f;
      if (hf(gx, gy, 6) < 0.06f) m *= 1.25f;
      break;
    case K_PLANK:
    case K_FLOOR: {
      int board = gy / 3, seg = (gx + board * 5) / 17;
      bool seam = gy % 3 == 0 || (gx + board * 5) % 17 == 0;
      c = k == K_FLOOR ? 0x6E4C32 : 0x6A5034;
      m = seam ? 0.55f : 0.85f + hf(board, seg, 9) * 0.25f + (hf(gx >> 2, gy, 2) - 0.5f) * 0.08f;
      break;
    }
    case K_ROCK: c = 0x55574F; m = 0.85f + n * 0.25f; break;
    case K_CAVE: {
      c = 0x3E4044;
      m = 0.8f + n * 0.25f;
      if (hf(gx / 3, gy / 3, 8) < 0.12f) m *= 0.75f;
      break;
    }
    default: c = 0x202020; break;
  }
  // the same patterns, in each place's own stone and soil
  switch (s_biome) {
    case 1: // desert: sandstone paving, golden sand, sparse dry scrub
      if (k == K_COBBLE) c = c == 0x45433C ? 0x8A7050 : 0xD8C098;
      if (k == K_SAND) c = 0xD8B880;
      if (k == K_GRASS || k == K_LONG) c = 0xA89860;
      break;
    case 2: // forest: earth paths, deep moss
      if (k == K_COBBLE) c = c == 0x45433C ? 0x4A3A28 : 0x7A6448;
      if (k == K_GRASS) c = (c == 0x4E6C3E) ? 0x46642F : c;
      break;
    case 3: // fjord: snow, trodden paths, dark stone
      if (k == K_GRASS) c = (c == 0x4E6C3E) ? 0xE4EAF0 : 0xD0D8E0;
      if (k == K_LONG) c = 0xC8D4DC;
      if (k == K_COBBLE) c = c == 0x45433C ? 0x5A5A5E : 0x9A9AA0;
      break;
    case 4: // the city: pale limestone and brick
      if (k == K_COBBLE) c = c == 0x45433C ? 0x6A5A48 : 0xC0A888;
      break;
    default: break;
  }
  r = ((c >> 16) & 255) * m; g = ((c >> 8) & 255) * m; b = (c & 255) * m;
}

static bool solidTile(int area, int tx, int tz) {
  const AreaDef &a = AREAS[area];
  if (tx < 0 || tz < 0 || tx >= a.w || tz >= a.d) return false;
  char c = a.rows[tz][tx];
  if (c == '^' || c == '#') return true;
  for (int i = 0; i < a.nb; i++) {
    const Building &b = a.b[i];
    if (tx >= b.x && tx < b.x + b.w && tz >= b.z && tz < b.z + b.d) return true;
  }
  return false;
}

bool groundTexAlloc() {
  if (!s_noise) {
    s_noise = (uint8_t *)heap_caps_malloc(128 * 128, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_noise) for (int i = 0; i < 128 * 128; i++) s_noise[i] = (uint8_t)(hash32(i, 77) >> 8);
  }
  if (!s_noise) return false;
  if (!s_tex) s_tex = (uint16_t *)heap_caps_malloc((size_t)MAXW * MAXH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_mip1) s_mip1 = (uint16_t *)heap_caps_malloc((size_t)MAXW * MAXH / 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_mip2) s_mip2 = (uint16_t *)heap_caps_malloc((size_t)MAXW * MAXH / 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return s_tex && s_mip1 && s_mip2;
}
void groundTexFree() {
  heap_caps_free(s_tex); heap_caps_free(s_mip1); heap_caps_free(s_mip2);
  s_tex = s_mip1 = s_mip2 = nullptr;
  g_ground.tex = nullptr;
}

static uint8_t s_kind[32 * 42], s_solid[32 * 42]; // (d+2) x (w+2), with a border
static int s_kw = 0;
static inline Kind kindAt(int tx, int tz) { return (Kind)s_kind[(tz + 1) * s_kw + tx + 1]; }
static inline bool solidAt(int tx, int tz) { return s_solid[(tz + 1) * s_kw + tx + 1]; }

void buildGroundTex(int area, const PointLight *lights, int nLights) {
  const AreaDef &a = AREAS[area];
  int tw = a.w * TPT, th = a.d * TPT;
  if (!s_tex || tw > MAXW || th > MAXH) { g_ground.tex = nullptr; return; }
  uint32_t t0 = micros();
  s_biome = a.indoor ? 0 : a.biome;
  s_kw = a.w + 2;
  for (int tz = -1; tz <= a.d; tz++)
    for (int tx = -1; tx <= a.w; tx++) {
      s_kind[(tz + 1) * s_kw + tx + 1] = kindOf(area, tx, tz);
      s_solid[(tz + 1) * s_kw + tx + 1] = solidTile(area, tx, tz);
    }
  auto kindT = [&](int gx, int gy) -> Kind {
    if (gx < 0 || gy < 0 || gx >= tw || gy >= th) return K_VOID;
    return kindAt(gx / TPT, gy / TPT);
  };
  bool cave = a.indoor == 2;
  float lr = ((g_light.tint >> 16) & 255) / 255.0f, lg = ((g_light.tint >> 8) & 255) / 255.0f, lb = (g_light.tint & 255) / 255.0f;
  float lit = g_light.amb + g_light.dif * 0.75f;
  // 1. base colour, lit from above, with ragged edges between soft kinds
  // Tile by tile: most tiles have the same ground all round and no wall
  // beside them, so the ragged edges, the foam and the wall shadow are only
  // worked out where they can show.
  for (int tz = 0; tz < a.d; tz++)
    for (int tx = 0; tx < a.w; tx++) {
      Kind kt = kindAt(tx, tz);
      bool mixed = false, walls = false;
      for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
          mixed = mixed || kindAt(tx + dx, tz + dz) != kt;
          walls = walls || ((dx || dz) && solidAt(tx + dx, tz + dz));
        }
      for (int gy = tz * TPT; gy < tz * TPT + TPT; gy++)
        for (int gx = tx * TPT; gx < tx * TPT + TPT; gx++) {
      Kind k = kt;
      if (mixed) {
        int jx = (int)(hf(gx, gy, 11) * 4.99f) - 2, jy = (int)(hf(gx, gy, 12) * 4.99f) - 2;
        Kind kj = kindT(gx + jx, gy + jy);
        auto soft = [](Kind q) { return q == K_GRASS || q == K_LONG || q == K_SAND || q == K_WATER || q == K_COBBLE || q == K_ROCK; };
        if (kj != K_VOID && soft(k) && soft(kj)) k = kj;
      }
      float r, g, b;
      baseColour(k, gx, gy, cave, r, g, b);
      // foam where water meets land
      if (k == K_WATER && !cave && mixed) {
        bool shore = false;
        for (int d = 2; d <= 4 && !shore; d += 2) {
          Kind n1 = kindT(gx + d, gy), n2 = kindT(gx - d, gy), n3 = kindT(gx, gy + d), n4 = kindT(gx, gy - d);
          auto land = [](Kind q) { return q != K_WATER && q != K_VOID && q != K_PLANK; };
          shore = land(n1) || land(n2) || land(n3) || land(n4);
        }
        if (shore && hf(gx, gy, 13) < 0.6f) { r = r * 0.4f + 200 * 0.6f; g = g * 0.4f + 214 * 0.6f; b = b * 0.4f + 214 * 0.6f; }
      }
      r *= lit * lr; g *= lit * lg; b *= lit * lb;
      // contact shadow next to walls and buildings
      float fx = (gx + 0.5f) / TPT, fz = (gy + 0.5f) / TPT;
      float near = 1.0f;
      if (walls)
      for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
          if (!dx && !dz) continue;
          if (!solidAt(tx + dx, tz + dz)) continue;
          float ex = dx < 0 ? fx - tx : dx > 0 ? tx + 1 - fx : 1.0f;
          float ez = dz < 0 ? fz - tz : dz > 0 ? tz + 1 - fz : 1.0f;
          float d = fminf(dx ? ex : 9, dz ? ez : 9);
          if (d < 0.45f) near = fminf(near, 0.55f + d);
        }
      r *= near; g *= near; b *= near;
      int R = clampi((int)r, 0, 255), G = clampi((int)g, 0, 255), B = clampi((int)b, 0, 255);
      s_tex[gy * tw + gx] = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
        }
    }
  // 2. building shadows, cast away from the light
  if (!a.indoor && g_light.sy > 0.1f) {
    float ox = -g_light.sx / g_light.sy, oz = -g_light.sz / g_light.sy;
    for (int i = 0; i < a.nb; i++) {
      const Building &bd = a.b[i];
      float h = bd.style == 2 ? 3.0f : bd.h + 0.6f;
      float x0 = bd.x, x1 = bd.x + bd.w, z0 = bd.z, z1 = bd.z + bd.d;
      if (bd.style == 2) { float c = bd.x + bd.w * 0.5f, cz = bd.z + bd.d * 0.5f; x0 = c - 1.3f; x1 = c + 1.3f; z0 = cz - 1.3f; z1 = cz + 1.3f; }
      float sx = ox * h, sz = oz * h;
      int gx0 = (int)((fminf(x0, x0 + sx)) * TPT), gx1 = (int)((fmaxf(x1, x1 + sx)) * TPT);
      int gy0 = (int)((fminf(z0, z0 + sz)) * TPT), gy1 = (int)((fmaxf(z1, z1 + sz)) * TPT);
      for (int gy = clampi(gy0, 0, th - 1); gy <= clampi(gy1, 0, th - 1); gy++)
        for (int gx = clampi(gx0, 0, tw - 1); gx <= clampi(gx1, 0, tw - 1); gx++) {
          float px = (gx + 0.5f) / TPT, pz = (gy + 0.5f) / TPT;
          if (px >= x0 && px < x1 && pz >= z0 && pz < z1) continue; // under the building itself
          // is p inside the footprint swept along (sx, sz)?
          float t0 = 0, t1 = 1;
          if (fabsf(sx) > 1e-4f) {
            float a1 = (px - x1) / sx, a2 = (px - x0) / sx;
            t0 = fmaxf(t0, fminf(a1, a2)); t1 = fminf(t1, fmaxf(a1, a2));
          } else if (px < x0 || px >= x1) continue;
          if (fabsf(sz) > 1e-4f) {
            float b1 = (pz - z1) / sz, b2 = (pz - z0) / sz;
            t0 = fmaxf(t0, fminf(b1, b2)); t1 = fminf(t1, fmaxf(b1, b2));
          } else if (pz < z0 || pz >= z1) continue;
          if (t0 > t1) continue;
          uint16_t &c = s_tex[gy * tw + gx];
          c = (uint16_t)(((c >> 1) & 0x7BEF) + ((c >> 2) & 0x39E7)); // three quarters
        }
    }
  }
  // 3. pools of lamplight
  for (int i = 0; i < nLights; i++) {
    const PointLight &L = lights[i];
    float rad = L.rad * 1.1f;
    int gx0 = (int)((L.x - rad) * TPT), gx1 = (int)((L.x + rad) * TPT), gy0 = (int)((L.z - rad) * TPT), gy1 = (int)((L.z + rad) * TPT);
    float r8 = L.r * 8.2f, g8 = L.g * 4.05f, b8 = L.b * 8.2f;
    for (int gy = clampi(gy0, 0, th - 1); gy <= clampi(gy1, 0, th - 1); gy++)
      for (int gx = clampi(gx0, 0, tw - 1); gx <= clampi(gx1, 0, tw - 1); gx++) {
        float dx = (gx + 0.5f) / TPT - L.x, dz = (gy + 0.5f) / TPT - L.z, dy = L.y;
        float d2 = dx * dx + dy * dy + dz * dz, r2 = rad * rad;
        if (d2 >= r2) continue;
        float f = 1 - d2 / r2;
        f *= f * 1.2f;
        uint16_t &c = s_tex[gy * tw + gx];
        int R = (c >> 11) * 255 / 31 + (int)(r8 * f), G = ((c >> 5) & 63) * 255 / 63 + (int)(g8 * f), B = (c & 31) * 255 / 31 + (int)(b8 * f);
        R = clampi(R, 0, 255); G = clampi(G, 0, 255); B = clampi(B, 0, 255);
        c = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
      }
  }
  Serial.printf("Gaslight: ground for %s painted in %lu ms\n", a.name, (unsigned long)((micros() - t0) / 1000));
  // smaller copies for the distance: each texel the average of four
  auto halve = [](const uint16_t *src, int sw, int sh, uint16_t *dst) {
    for (int y = 0; y < sh / 2; y++)
      for (int x = 0; x < sw / 2; x++) {
        uint16_t a = src[(2 * y) * sw + 2 * x], b = src[(2 * y) * sw + 2 * x + 1], c = src[(2 * y + 1) * sw + 2 * x],
                 d = src[(2 * y + 1) * sw + 2 * x + 1];
        int R = ((a >> 11) + (b >> 11) + (c >> 11) + (d >> 11)) >> 2;
        int G = (((a >> 5) & 63) + ((b >> 5) & 63) + ((c >> 5) & 63) + ((d >> 5) & 63)) >> 2;
        int B = ((a & 31) + (b & 31) + (c & 31) + (d & 31)) >> 2;
        dst[y * (sw / 2) + x] = (uint16_t)((R << 11) | (G << 5) | B);
      }
  };
  halve(s_tex, tw, th, s_mip1);
  halve(s_mip1, tw / 2, th / 2, s_mip2);
  g_ground.mip[0] = s_tex; g_ground.mip[1] = s_mip1; g_ground.mip[2] = s_mip2;
  g_ground.tex = s_tex;
  g_ground.tw = tw;
  g_ground.th = th;
  g_ground.tpu = TPT;
  g_ground.y = 0;
}

} // namespace Gaslight
