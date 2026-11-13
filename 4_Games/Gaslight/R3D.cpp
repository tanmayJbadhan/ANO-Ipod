// Gaslight & Griffins - the 3D part: building low-poly meshes with baked
// light, a fixed-angle camera, and a rasteriser that draws one 20-row strip
// at a time (colour + 16-bit depth), so both cores can share a frame.
#pragma GCC optimize("O2")
#include "GL.h"
#include <esp_heap_caps.h>
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>

namespace Gaslight {

Lighting g_light;
Camera g_cam;
Fog g_fog;
uint16_t g_sky[H];

// --- helpers -----------------------------------------------------------------

static float s_sinTab[1025];
static bool s_sinReady = false;
static void sinInit() {
  for (int i = 0; i <= 1024; i++) s_sinTab[i] = sinf(i * (2 * PI_F / 1024));
  s_sinReady = true;
}
static inline float fsin(float x) {
  float t = x * (1024 / (2 * PI_F));
  int i = (int)floorf(t);
  float f = t - i;
  i &= 1023;
  return s_sinTab[i] + (s_sinTab[i + 1] - s_sinTab[i]) * f;
}
static inline float fcos(float x) { return fsin(x + PI_F * 0.5f); }

static uint32_t s_rng = 0x1234567;
uint32_t rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
float frand() { return (rnd() >> 8) / 16777216.0f; }
uint32_t hash32(uint32_t a, uint32_t b) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
  return h;
}
float hashf(int a, int b, uint32_t seed) { return (hash32((uint32_t)a * 7919u + seed, (uint32_t)b) & 0xFFFF) / 65535.0f; }

uint16_t mix565(uint16_t a, uint16_t b, int t) {
  int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
  int rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  int r = ra + (((rb - ra) * t) >> 8), g = ga + (((gb - ga) * t) >> 8), bl = ba + (((bb - ba) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

bool meshAlloc(MeshBuf &m, int capV, int capF) {
  if (!s_sinReady) sinInit();
  m.v = (Vtx *)heap_caps_malloc(sizeof(Vtx) * capV, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  m.f = (Face *)heap_caps_malloc(sizeof(Face) * capF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!m.v) m.v = (Vtx *)malloc(sizeof(Vtx) * capV);
  if (!m.f) m.f = (Face *)malloc(sizeof(Face) * capF);
  m.capV = capV; m.capF = capF; m.nv = m.nf = 0;
  return m.v && m.f;
}
void meshFree(MeshBuf &m) {
  heap_caps_free(m.v); heap_caps_free(m.f);
  m.v = nullptr; m.f = nullptr; m.capV = m.capF = m.nv = m.nf = 0;
}

// --- building -----------------------------------------------------------------

int addVert(MeshBuf &m, float x, float y, float z, uint32_t col, float bright) {
  if (m.nv >= m.capV) return -1;
  float tr = ((g_light.tint >> 16) & 255) / 255.0f, tg = ((g_light.tint >> 8) & 255) / 255.0f,
        tb = (g_light.tint & 255) / 255.0f;
  Vtx &v = m.v[m.nv];
  v.x = x; v.y = y; v.z = z;
  v.r = ((col >> 16) & 255) * (31.0f / 255.0f) * bright * tr;
  v.g = ((col >> 8) & 255) * (63.0f / 255.0f) * bright * tg;
  v.b = (col & 255) * (31.0f / 255.0f) * bright * tb;
  return m.nv++;
}

void addTri(MeshBuf &m, int a, int b, int c) {
  if (a < 0 || b < 0 || c < 0 || m.nf >= m.capF) return;
  m.f[m.nf++] = {(uint16_t)a, (uint16_t)b, (uint16_t)c};
}

static float lightFor(float nx, float ny, float nz) {
  float l = sqrtf(nx * nx + ny * ny + nz * nz);
  if (l < 1e-9f) return g_light.amb;
  float d = (nx * g_light.sx + ny * g_light.sy + nz * g_light.sz) / l;
  // a little light wraps round, so shaded sides aren't flat black
  float w = (d + 0.25f) / 1.25f;
  return g_light.amb + g_light.dif * (w > 0 ? w : 0);
}

void addQuad(MeshBuf &m, const float *p, uint32_t col, float glow) {
  float ux = p[3] - p[0], uy = p[4] - p[1], uz = p[5] - p[2];
  float vx = p[6] - p[0], vy = p[7] - p[1], vz = p[8] - p[2];
  float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
  float br = lightFor(nx, ny, nz) + glow;
  int a = addVert(m, p[0], p[1], p[2], col, br);
  int b = addVert(m, p[3], p[4], p[5], col, br);
  int c = addVert(m, p[6], p[7], p[8], col, br);
  int d = addVert(m, p[9], p[10], p[11], col, br);
  addTri(m, a, b, c);
  addTri(m, a, c, d);
}

static void addTri3(MeshBuf &m, const float *p, uint32_t col, float glow) {
  float ux = p[3] - p[0], uy = p[4] - p[1], uz = p[5] - p[2];
  float vx = p[6] - p[0], vy = p[7] - p[1], vz = p[8] - p[2];
  float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
  float br = lightFor(nx, ny, nz) + glow;
  int a = addVert(m, p[0], p[1], p[2], col, br);
  int b = addVert(m, p[3], p[4], p[5], col, br);
  int c = addVert(m, p[6], p[7], p[8], col, br);
  addTri(m, a, b, c);
}

static inline void rot(float &x, float &z, float ang, float cx, float cz) {
  if (ang == 0) return;
  float s = fsin(ang), c = fcos(ang), dx = x - cx, dz = z - cz;
  x = cx + dx * c - dz * s;
  z = cz + dx * s + dz * c;
}

static void boxFaces(MeshBuf &m, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t side,
                     uint32_t top, float ang, float cx, float cz, float glow) {
  // corners: 0..3 bottom (x0z0, x1z0, x1z1, x0z1), 4..7 top
  float P[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                   {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
  for (auto &p : P) rot(p[0], p[2], ang, cx, cz);
  auto q = [&](int a, int b, int c, int d, uint32_t col) {
    float pts[12] = {P[a][0], P[a][1], P[a][2], P[b][0], P[b][1], P[b][2],
                     P[c][0], P[c][1], P[c][2], P[d][0], P[d][1], P[d][2]};
    addQuad(m, pts, col, glow);
  };
  q(4, 7, 6, 5, top);  // top
  q(3, 2, 6, 7, side); // +z
  q(1, 0, 4, 5, side); // -z
  q(2, 1, 5, 6, side); // +x
  q(0, 3, 7, 4, side); // -x
}

void addBox(MeshBuf &m, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t col, float ang,
            float cx, float cz, float glow) {
  boxFaces(m, x0, y0, z0, x1, y1, z1, col, col, ang, cx, cz, glow);
}
void addBox2(MeshBuf &m, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t side, uint32_t top) {
  boxFaces(m, x0, y0, z0, x1, y1, z1, side, top, 0, 0, 0, 0);
}

void addLimb(MeshBuf &m, float cx, float cy, float cz, float hx, float hy, float hz, float yaw, uint32_t col,
             float pitch) {
  // A box hanging from its top centre, swung forward by `pitch` (radians).
  float P[8][3];
  int k = 0;
  for (int yy = 0; yy < 2; yy++)
    for (int i = 0; i < 4; i++) {
      float x = (i == 1 || i == 2) ? hx : -hx, z = (i >= 2) ? hz : -hz, y = yy ? 0 : -2 * hy;
      // swing about the x axis at the top
      float sp = fsin(pitch), cp = fcos(pitch);
      float y2 = y * cp - z * sp, z2 = y * sp + z * cp;
      // turn about the vertical
      float sy = fsin(yaw), cyw = fcos(yaw);
      float x3 = x * cyw - z2 * sy, z3 = x * sy + z2 * cyw;
      P[k][0] = cx + x3; P[k][1] = cy + y2; P[k][2] = cz + z3;
      k++;
    }
  auto q = [&](int a, int b, int c, int d) {
    float pts[12] = {P[a][0], P[a][1], P[a][2], P[b][0], P[b][1], P[b][2],
                     P[c][0], P[c][1], P[c][2], P[d][0], P[d][1], P[d][2]};
    addQuad(m, pts, col, 0);
  };
  // the four sides only: the ends are hidden by whatever the limb hangs from
  q(3, 2, 6, 7); q(1, 0, 4, 5); q(2, 1, 5, 6); q(0, 3, 7, 4);
}

void addDisc(MeshBuf &m, float cx, float y, float cz, float r, uint32_t col, int n) {
  int c = addVert(m, cx, y, cz, col, lightFor(0, 1, 0));
  int first = m.nv;
  for (int i = 0; i < n; i++) {
    float a = i * 2 * PI_F / n;
    addVert(m, cx + fcos(a) * r, y, cz - fsin(a) * r, col, lightFor(0, 1, 0));
  }
  for (int i = 0; i < n; i++) addTri(m, c, first + i, first + (i + 1) % n);
}

void addPanel(MeshBuf &m, float cx, float cy, float cz, float hw, float hh, float yaw, uint32_t col, float glow) {
  // an upright rectangle facing along the yaw's forward direction
  float c = fcos(yaw), s = fsin(yaw);
  float rx = c * hw, rz = s * hw;
  float p[12] = {cx - rx, cy - hh, cz - rz, cx + rx, cy - hh, cz + rz, cx + rx, cy + hh, cz + rz, cx - rx, cy + hh, cz - rz};
  addQuad(m, p, col, glow);
}

void addRoof(MeshBuf &m, float x0, float z0, float x1, float z1, float y0, float y1, uint32_t col, bool ridgeX) {
  const float ov = 0.18f;
  uint32_t gable = col;
  if (ridgeX) {
    float zm = (z0 + z1) * 0.5f;
    float s[12] = {x0 - ov, y0, z1 + ov, x1 + ov, y0, z1 + ov, x1 + ov, y1, zm, x0 - ov, y1, zm};
    addQuad(m, s, col);
    float n[12] = {x1 + ov, y0, z0 - ov, x0 - ov, y0, z0 - ov, x0 - ov, y1, zm, x1 + ov, y1, zm};
    addQuad(m, n, col);
    float e[9] = {x1, y0, z1, x1, y0, z0, x1, y1, zm};
    addTri3(m, e, gable, 0);
    float w[9] = {x0, y0, z0, x0, y0, z1, x0, y1, zm};
    addTri3(m, w, gable, 0);
  } else {
    float xm = (x0 + x1) * 0.5f;
    float e[12] = {x1 + ov, y0, z1 + ov, x1 + ov, y0, z0 - ov, xm, y1, z0 - ov, xm, y1, z1 + ov};
    addQuad(m, e, col);
    float w[12] = {x0 - ov, y0, z0 - ov, x0 - ov, y0, z1 + ov, xm, y1, z1 + ov, xm, y1, z0 - ov};
    addQuad(m, w, col);
    float s[9] = {x0, y0, z1, x1, y0, z1, xm, y1, z1};
    addTri3(m, s, gable, 0);
    float n[9] = {x1, y0, z0, x0, y0, z0, xm, y1, z0};
    addTri3(m, n, gable, 0);
  }
}

void addCyl(MeshBuf &m, float cx, float y0, float cz, float r0, float y1, float r1, uint32_t col, int n, float glow) {
  for (int i = 0; i < n; i++) {
    float a0 = i * 2 * PI_F / n, a1 = (i + 1) * 2 * PI_F / n;
    float c0 = fcos(a0), s0 = -fsin(a0), c1 = fcos(a1), s1 = -fsin(a1);
    if (r1 > 0.001f) {
      float q[12] = {cx + c0 * r0, y0, cz + s0 * r0, cx + c1 * r0, y0, cz + s1 * r0,
                     cx + c1 * r1, y1, cz + s1 * r1, cx + c0 * r1, y1, cz + s0 * r1};
      addQuad(m, q, col, glow);
      float t[9] = {cx, y1, cz, cx + c0 * r1, y1, cz + s0 * r1, cx + c1 * r1, y1, cz + s1 * r1};
      addTri3(m, t, col, glow);
    } else {
      float t[9] = {cx + c0 * r0, y0, cz + s0 * r0, cx + c1 * r0, y0, cz + s1 * r0, cx, y1, cz};
      addTri3(m, t, col, glow);
    }
  }
}

void addBlob(MeshBuf &m, float cx, float cy, float cz, float rx, float ry, float rz, uint32_t col, int seg, int rings,
             float glow) {
  int base = m.nv;
  for (int j = 0; j <= rings; j++) {
    float lat = -PI_F / 2 + PI_F * j / rings, cl = fcos(lat), sl = fsin(lat);
    for (int i = 0; i < seg; i++) {
      float a = i * 2 * PI_F / seg, nx = fcos(a) * cl, nz = -fsin(a) * cl, ny = sl;
      addVert(m, cx + nx * rx, cy + ny * ry, cz + nz * rz, col, lightFor(nx, ny, nz) + glow);
    }
  }
  if (m.nv - base != (rings + 1) * seg) return; // out of room
  for (int j = 0; j < rings; j++)
    for (int i = 0; i < seg; i++) {
      int i1 = (i + 1) % seg;
      int a = base + j * seg + i, b = base + j * seg + i1, c = base + (j + 1) * seg + i1, d = base + (j + 1) * seg + i;
      if (j > 0) addTri(m, a, b, c);
      if (j < rings - 1) addTri(m, a, c, d);
    }
}

void bakeLights(MeshBuf &m, int v0, int v1, const PointLight *L, int n) {
  for (int i = v0; i < v1; i++) {
    Vtx &v = m.v[i];
    for (int k = 0; k < n; k++) {
      float dx = v.x - L[k].x, dy = v.y - L[k].y, dz = v.z - L[k].z;
      float d2 = dx * dx + dy * dy + dz * dz, r2 = L[k].rad * L[k].rad;
      if (d2 >= r2) continue;
      float f = 1 - d2 / r2;
      f *= f;
      v.r += L[k].r * f; v.g += L[k].g * f; v.b += L[k].b * f;
    }
  }
}

void markAway(MeshBuf &m, float pitch, float yaw) {
  if (!m.away) return;
  // the direction back toward the camera
  float cp = cosf(pitch), sp = sinf(pitch);
  float bx = sinf(yaw) * cp, by = sp, bz = cosf(yaw) * cp;
  memset(m.away, 0, (m.capF + 7) / 8);
  for (int i = 0; i < m.nf; i++) {
    const Vtx &a = m.v[m.f[i].a], &b = m.v[m.f[i].b], &c = m.v[m.f[i].c];
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l < 1e-9f) continue;
    // the view spreads about 35 degrees either way, so allow for that
    if ((nx * bx + ny * by + nz * bz) / l < -0.62f) m.away[i >> 3] |= (uint8_t)(1 << (i & 7));
  }
}

// --- camera -------------------------------------------------------------------

void Camera::update() {
  float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
  ex = tx + dist * sy * cp; ey = ty + dist * sp; ez = tz + dist * cy * cp;
  fx = -sy * cp; fy = -sp; fz = -cy * cp;
  // right = forward x up(0,1,0) = (-fz, 0, fx) normalised
  float rl = sqrtf(fz * fz + fx * fx);
  rx = -fz / rl; ry = 0; rz = fx / rl;
  // up = right x forward
  ux = ry * fz - rz * fy; uy = rz * fx - rx * fz; uz = rx * fy - ry * fx;
}

static const float NEAR_Z = 0.6f;

bool Camera::project(float x, float y, float z, float &sx, float &sy, float &depth) const {
  float dx = x - ex, dy = y - ey, dz = z - ez;
  float zv = dx * fx + dy * fy + dz * fz;
  if (zv < NEAR_Z) return false;
  float xv = dx * rx + dy * ry + dz * rz, yv = dx * ux + dy * uy + dz * uz;
  float inv = focal / zv;
  sx = W * 0.5f + xv * inv;
  sy = H * 0.5f - yv * inv;
  depth = zv;
  return true;
}

void setSky(uint32_t top, uint32_t bottom) {
  int r0 = (top >> 16) & 255, g0 = (top >> 8) & 255, b0 = top & 255;
  int r1 = (bottom >> 16) & 255, g1 = (bottom >> 8) & 255, b1 = bottom & 255;
  for (int y = 0; y < H; y++) {
    int t = y * 256 / (H - 1);
    // a touch of ordered dither keeps the gradient from banding
    int d = ((y * 7) & 3) - 1;
    int r = clampi(r0 + ((r1 - r0) * t >> 8) + d * 2, 0, 255), g = clampi(g0 + ((g1 - g0) * t >> 8) + d, 0, 255),
        b = clampi(b0 + ((b1 - b0) * t >> 8) + d * 2, 0, 255);
    g_sky[y] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
}

// --- scene: triangles set up for drawing ----------------------------------------

struct STri {
  float x0, y0, x1, y1, x2, y2;   // sorted top to bottom
  float s01, s02, s12;            // dx/dy of each edge
  float z0, zx, zy;               // 1/z plane (scaled)
  float r0, rx, ry, g0, gx, gy, b0, bx, by; // only for shaded triangles
  int16_t ya, yb;                 // rows covered
  uint16_t flat;                  // the colour, when the triangle is one colour
  uint8_t isFlat;
};

static const int MAX_TRIS = 3600, MAX_BIN = 16000, MAX_SV = 9000;
static STri *s_tri = nullptr;
static int s_nt = 0;
static uint16_t *s_bin = nullptr;
static int s_binStart[NSTRIP + 1];
static int s_binCount[NSTRIP];
struct SV { float x, y, z, r, g, b; bool ok; };
static const int SVN = 512;            // cache slots (internal RAM)
static SV *s_sv = nullptr;
static uint32_t *s_svTag = nullptr;    // (generation << 16) | vertex index
static uint32_t s_gen = 0;

// the ground: one textured plane at y = 0, drawn a whole screen row at a time
Ground g_ground;
struct GroundRow { int32_t u, du, v, dv; uint16_t depth; uint8_t fog; uint8_t on; uint8_t lvl; };
static GroundRow s_row[H];

static bool sceneMem() {
  if (s_tri) return true;
  s_tri = (STri *)heap_caps_malloc(sizeof(STri) * MAX_TRIS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_bin = (uint16_t *)heap_caps_malloc(2 * MAX_BIN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_sv = (SV *)heap_caps_malloc(sizeof(SV) * SVN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_sv) s_sv = (SV *)heap_caps_malloc(sizeof(SV) * SVN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_svTag = (uint32_t *)heap_caps_calloc(SVN, 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_svTag) s_svTag = (uint32_t *)heap_caps_calloc(SVN, 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return s_tri && s_bin && s_sv && s_svTag;
}

void sceneFreeMem() {
  heap_caps_free(s_tri); heap_caps_free(s_bin); heap_caps_free(s_sv); heap_caps_free(s_svTag);
  s_tri = nullptr; s_bin = nullptr; s_sv = nullptr; s_svTag = nullptr;
}

static const float ZSCALE = 0.6f * 65000.0f; // depth value = ZSCALE / z (near plane -> 65000)

static void groundRows() {
  const Camera &c = g_cam;
  float fspan = 1.0f / (g_fog.end - g_fog.start);
  float k = (float)g_ground.tpu * 65536.0f;
  for (int y = 0; y < H; y++) {
    GroundRow &r = s_row[y];
    r.on = 0;
    if (!g_ground.tex) continue;
    float b = (H * 0.5f - (y + 0.5f)) / c.focal;
    // ray through this row: f + a*r + b*u, with a = (x - W/2) / focal
    float dy = c.fy + b * c.uy; // the right vector is level, so x doesn't matter
    if (dy >= -1e-4f) continue;  // at or above the horizon
    float t = (g_ground.y - c.ey) / dy;
    if (t < NEAR_Z || t > 90) continue;
    float a0 = (0.5f - W * 0.5f) / c.focal, da = 1.0f / c.focal;
    float px = c.ex + t * (c.fx + a0 * c.rx + b * c.ux), pz = c.ez + t * (c.fz + a0 * c.rz + b * c.uz);
    float sx = t * da * c.rx, sz = t * da * c.rz;
    r.u = (int32_t)(px * k); r.du = (int32_t)(sx * k);
    r.v = (int32_t)(pz * k); r.dv = (int32_t)(sz * k);
    // how many texels one pixel steps over picks the texture size
    float step = sqrtf(sx * sx + sz * sz) * g_ground.tpu;
    r.lvl = step < 1.1f ? 0 : step < 2.2f ? 1 : 2;
    if (!g_ground.mip[r.lvl]) r.lvl = 0;
    r.u >>= r.lvl; r.du >>= r.lvl; r.v >>= r.lvl; r.dv >>= r.lvl;
    float z = ZSCALE / t;
    r.depth = (uint16_t)(z > 65000 ? 65000 : z);
    r.fog = (uint8_t)(clampf((t - g_fog.start) * fspan, 0, 1) * g_fog.max * 32 + 0.5f);
    r.on = 1;
  }
}

void sceneBegin() {
  sceneMem();
  s_nt = 0;
  g_cam.update();
  groundRows();
}

int sceneTris() { return s_nt; }

float g_nearCut = 0; // extra: drop anything nearer than this (cutaway of scenery in front of Wren)
void sceneAdd(const MeshBuf &m, int f0, int f1, int v0, int v1) {
  if (!s_tri) return;
  if (f1 < 0) f1 = m.nf;
  if (v1 < 0) v1 = m.nv;
  float fr = ((g_fog.col >> 16) & 255) * 31 / 255.0f, fg = ((g_fog.col >> 8) & 255) * 63 / 255.0f,
        fb = (g_fog.col & 255) * 31 / 255.0f;
  float fspan = 1.0f / (g_fog.end - g_fog.start), fmax = g_fog.max, fstart = g_fog.start;
  const Camera &cm = g_cam;
  float nearZ = g_nearCut > NEAR_Z ? g_nearCut : NEAR_Z;
  g_prof[3] += f1 - f0;
  uint32_t gen = (++s_gen & 0xFFFF) << 16;
  if (!gen) { memset(s_svTag, 0, SVN * 4); gen = (++s_gen & 0xFFFF) << 16; }
  // A vertex is transformed the first time a face asks for it; faces sit
  // right after their own vertices, so nearly every lookup hits.
  auto get = [&](int i) -> const SV * {
    SV &o = s_sv[i & (SVN - 1)];
    uint32_t &tag = s_svTag[i & (SVN - 1)];
    if (tag == (gen | (uint32_t)i)) return &o;
    tag = gen | (uint32_t)i;
    const Vtx &v = m.v[i];
    float dx = v.x - cm.ex, dy = v.y - cm.ey, dz = v.z - cm.ez;
    float d = dx * cm.fx + dy * cm.fy + dz * cm.fz;
    if (d < nearZ) { o.ok = false; return &o; }
    o.ok = true;
    float inv = cm.focal / d;
    o.x = W * 0.5f + (dx * cm.rx + dz * cm.rz) * inv;
    o.y = H * 0.5f - (dx * cm.ux + dy * cm.uy + dz * cm.uz) * inv;
    o.z = ZSCALE / d;
    float f = (d - fstart) * fspan;
    if (f <= 0) { o.r = v.r; o.g = v.g; o.b = v.b; }
    else {
      if (f > 1) f = 1;
      f *= fmax;
      o.r = v.r + (fr - v.r) * f; o.g = v.g + (fg - v.g) * f; o.b = v.b + (fb - v.b) * f;
    }
    if (o.r > 31.4f) o.r = 31.4f;
    if (o.g > 63.4f) o.g = 63.4f;
    if (o.b > 31.4f) o.b = 31.4f;
    return &o;
  };
  for (int i = f0; i < f1 && s_nt < MAX_TRIS; i++) {
    const Face &fc = m.f[i];
    if (m.away && m.away[i >> 3] & (1 << (i & 7))) continue;
    if (fc.a < v0 || fc.b < v0 || fc.c < v0 || fc.a >= v1 || fc.b >= v1 || fc.c >= v1) continue;
    const SV *a = get(fc.a);
    if (!a->ok) continue;
    const SV *b = get(fc.b);
    if (!b->ok) continue;
    const SV *c = get(fc.c);
    if (!c->ok) continue;
    // three distinct slots, or a collision overwrote one: fetch again
    if (((fc.a ^ fc.b) & (SVN - 1)) == 0 || ((fc.a ^ fc.c) & (SVN - 1)) == 0 || ((fc.b ^ fc.c) & (SVN - 1)) == 0) continue;
    float area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (area >= -0.01f) continue; // facing away (or edge-on)
    float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    if (maxx < 0 || minx > W || maxy < 0 || miny > H) continue;
    STri &t = s_tri[s_nt];
    float inv = 1.0f / area;
    float dx1 = b->x - a->x, dy1 = b->y - a->y, dx2 = c->x - a->x, dy2 = c->y - a->y;
    auto plane = [&](float va, float vb, float vc, float &o, float &gx, float &gy) {
      float d1 = vb - va, d2 = vc - va;
      gx = (d1 * dy2 - d2 * dy1) * inv;
      gy = (d2 * dx1 - d1 * dx2) * inv;
      o = va - gx * a->x - gy * a->y;
    };
    plane(a->z, b->z, c->z, t.z0, t.zx, t.zy);
    // one colour, or shaded across?
    float dr = fmaxf(fabsf(a->r - b->r), fabsf(a->r - c->r)), dg = fmaxf(fabsf(a->g - b->g), fabsf(a->g - c->g)),
          db = fmaxf(fabsf(a->b - b->b), fabsf(a->b - c->b));
    if (dr < 0.8f && dg < 1.6f && db < 0.8f) {
      t.isFlat = 1;
      int R = (int)((a->r + b->r + c->r) * (1.0f / 3) + 0.5f), G = (int)((a->g + b->g + c->g) * (1.0f / 3) + 0.5f),
          B = (int)((a->b + b->b + c->b) * (1.0f / 3) + 0.5f);
      t.flat = (uint16_t)((clampi(R, 0, 31) << 11) | (clampi(G, 0, 63) << 5) | clampi(B, 0, 31));
    } else {
      t.isFlat = 0;
      plane(a->r, b->r, c->r, t.r0, t.rx, t.ry);
      plane(a->g, b->g, c->g, t.g0, t.gx, t.gy);
      plane(a->b, b->b, c->b, t.b0, t.bx, t.by);
    }
    const SV *p[3] = {a, b, c};
    if (p[1]->y < p[0]->y) { const SV *q = p[0]; p[0] = p[1]; p[1] = q; }
    if (p[2]->y < p[1]->y) { const SV *q = p[1]; p[1] = p[2]; p[2] = q; }
    if (p[1]->y < p[0]->y) { const SV *q = p[0]; p[0] = p[1]; p[1] = q; }
    t.x0 = p[0]->x; t.y0 = p[0]->y; t.x1 = p[1]->x; t.y1 = p[1]->y; t.x2 = p[2]->x; t.y2 = p[2]->y;
    t.s01 = t.y1 > t.y0 ? (t.x1 - t.x0) / (t.y1 - t.y0) : 0;
    t.s02 = t.y2 > t.y0 ? (t.x2 - t.x0) / (t.y2 - t.y0) : 0;
    t.s12 = t.y2 > t.y1 ? (t.x2 - t.x1) / (t.y2 - t.y1) : 0;
    int ya = (int)ceilf(t.y0 - 0.5f), yb = (int)ceilf(t.y2 - 0.5f) - 1;
    if (ya < 0) ya = 0;
    if (yb > H - 1) yb = H - 1;
    if (yb < ya) continue;
    t.ya = (int16_t)ya; t.yb = (int16_t)yb;
    s_nt++;
  }
}

void sceneEnd() {
  // Bin the triangles by strip: count, prefix sum, fill.
  memset(s_binCount, 0, sizeof(s_binCount));
  for (int i = 0; i < s_nt; i++)
    for (int s = s_tri[i].ya / SH; s <= s_tri[i].yb / SH; s++) s_binCount[s]++;
  int tot = 0;
  for (int s = 0; s < NSTRIP; s++) { s_binStart[s] = tot; tot += s_binCount[s]; }
  s_binStart[NSTRIP] = tot;
  if (tot > MAX_BIN) { // too many: keep the first ones that fit
    for (int s = 0; s <= NSTRIP; s++) s_binStart[s] = s_binStart[s] * MAX_BIN / tot;
  }
  int fill[NSTRIP];
  for (int s = 0; s < NSTRIP; s++) fill[s] = s_binStart[s];
  for (int i = 0; i < s_nt; i++)
    for (int s = s_tri[i].ya / SH; s <= s_tri[i].yb / SH; s++)
      if (fill[s] < s_binStart[s + 1]) s_bin[fill[s]++] = (uint16_t)i;
  for (int s = 0; s < NSTRIP; s++) s_binCount[s] = fill[s] - s_binStart[s];
}

static const uint32_t SPREAD = 0x07E0F81F;
static inline uint32_t spread(uint16_t c) { return ((uint32_t)c | (uint32_t)c << 16) & SPREAD; }

volatile uint32_t g_prof2[5];
IRAM_ATTR void drawScene3D(uint16_t *col, uint16_t *depth, int top) {
  uint32_t tg0 = micros();
  int s = top / SH;
  uint32_t fogS = spread(c565(g_fog.col));
  // ground (or sky) and its depth, row by row
  for (int j = 0; j < SH; j++) {
    const GroundRow &gr = s_row[top + j];
    uint16_t *r = col + j * W, *dp = depth + j * W;
    uint16_t sky = g_sky[top + j];
    if (!gr.on) {
      for (int x = 0; x < W; x++) r[x] = sky;
      memset(dp, 0, W * 2);
      continue;
    }
    const uint16_t *tex = gr.lvl ? g_ground.mip[gr.lvl] : g_ground.tex;
    int tw = g_ground.tw >> gr.lvl, th = g_ground.th >> gr.lvl;
    // two pixels per texel lookup, written as one 32-bit word each
    int32_t u = gr.u, v = gr.v, du = gr.du * 2, dv = gr.dv * 2;
    uint32_t dval = gr.depth | ((uint32_t)gr.depth << 16), sky2 = sky | ((uint32_t)sky << 16);
    uint32_t f = gr.fog;
    // what lies past the edges, already fogged for this row
    auto edge = [&](uint16_t c) -> uint32_t {
      if (f) { uint32_t a = spread(c); a = ((a * (32 - f) + fogS * f) >> 5) & SPREAD; c = (uint16_t)(a | a >> 16); }
      return c | ((uint32_t)c << 16);
    };
    uint32_t north2 = g_ground.northSky ? sky2 : edge(g_ground.outNorth), rest2 = g_ground.restSky ? sky2 : edge(g_ground.outRest);
    uint32_t northD = g_ground.northSky ? 0 : dval, restD = g_ground.restSky ? 0 : dval;
    uint32_t *r32 = (uint32_t *)r, *d32 = (uint32_t *)dp;
    for (int x = 0; x < W / 2; x++, u += du, v += dv) {
      int tu = u >> 16, tv = v >> 16;
      if ((unsigned)tu < (unsigned)tw && (unsigned)tv < (unsigned)th) {
        uint32_t c = tex[tv * tw + tu];
        if (f) {
          uint32_t a = spread((uint16_t)c);
          a = ((a * (32 - f) + fogS * f) >> 5) & SPREAD;
          c = (a | a >> 16) & 0xFFFF;
        }
        r32[x] = c | (c << 16);
        d32[x] = dval;
      } else if (tv < 0) {
        r32[x] = north2;
        d32[x] = northD;
      } else {
        r32[x] = rest2;
        d32[x] = restD;
      }
    }
  }
  uint32_t tg = micros();
  g_prof2[0] += tg - tg0;
  if (!s_tri) return;
  int b0 = s_binStart[s], n = s_binCount[s];
  auto fx16 = [](float v) -> int32_t { return (int32_t)(clampf(v, -30000.0f, 30000.0f) * 65536.0f); };
  for (int k = 0; k < n; k++) {
    const STri &t = s_tri[s_bin[b0 + k]];
    g_prof2[4]++;
    int ya = t.ya < top ? top : t.ya, yb = t.yb > top + SH - 1 ? top + SH - 1 : t.yb;
    float yc0 = ya + 0.5f;
    // edges and depth stepped in fixed point, one add per row
    int32_t xl = fx16(t.x0 + (yc0 - t.y0) * t.s02), sl = fx16(t.s02);
    int ySwitch = (int)ceilf(t.y1 - 0.5f); // first row on the lower short edge
    bool upper = ya < ySwitch;
    int32_t xs = upper ? fx16(t.x0 + (yc0 - t.y0) * t.s01) : fx16(t.x1 + (yc0 - t.y1) * t.s12);
    int32_t ss = upper ? fx16(t.s01) : fx16(t.s12);
    int32_t zrow = (int32_t)((t.z0 + t.zy * yc0 + t.zx * 0.5f) * 256), zy = (int32_t)(t.zy * 256), dz = (int32_t)(t.zx * 256);
    for (int y = ya; y <= yb; y++, xl += sl, xs += ss, zrow += zy) {
      if (upper && y >= ySwitch) {
        upper = false;
        xs = fx16(t.x1 + (y + 0.5f - t.y1) * t.s12);
        ss = fx16(t.s12);
      }
      int32_t a0 = xl, a1 = xs;
      if (a1 < a0) { a0 = xs; a1 = xl; }
      int ix0 = (a0 + 0x8000) >> 16, ix1 = (a1 + 0x8000) >> 16;
      if (ix0 < 0) ix0 = 0;
      if (ix1 > W) ix1 = W;
      int nn = ix1 - ix0;
      if (nn <= 0) continue;
      g_prof2[2] += nn; g_prof2[3]++;
      int32_t z = zrow + dz * ix0;
      uint16_t *cr = col + (y - top) * W + ix0;
      uint16_t *dp = depth + (y - top) * W + ix0;
      if (t.isFlat) {
        uint16_t c = t.flat;
        for (int i = 0; i < nn; i++, z += dz) {
          int32_t zz = z >> 8;
          if (zz > dp[i]) { dp[i] = (uint16_t)(zz > 65535 ? 65535 : zz); cr[i] = c; }
        }
        continue;
      }
      float yc = y + 0.5f, px = ix0 + 0.5f, pe = ix1 - 0.5f;
      float r0 = clampf(t.r0 + t.rx * px + t.ry * yc, 0, 31.4f), r1 = clampf(t.r0 + t.rx * pe + t.ry * yc, 0, 31.4f);
      float g0 = clampf(t.g0 + t.gx * px + t.gy * yc, 0, 63.4f), g1 = clampf(t.g0 + t.gx * pe + t.gy * yc, 0, 63.4f);
      float c0 = clampf(t.b0 + t.bx * px + t.by * yc, 0, 31.4f), c1 = clampf(t.b0 + t.bx * pe + t.by * yc, 0, 31.4f);
      float inv = nn > 1 ? 1.0f / (nn - 1) : 0;
      int32_t r = (int32_t)(r0 * 65536), dr = (int32_t)((r1 - r0) * inv * 65536);
      int32_t g = (int32_t)(g0 * 65536), dg = (int32_t)((g1 - g0) * inv * 65536);
      int32_t bl = (int32_t)(c0 * 65536), db = (int32_t)((c1 - c0) * inv * 65536);
      for (int i = 0; i < nn; i++) {
        int32_t zz = z >> 8;
        if (zz > dp[i]) {
          dp[i] = (uint16_t)(zz > 65535 ? 65535 : zz);
          cr[i] = (uint16_t)(((r >> 16) << 11) | ((g >> 16) << 5) | (bl >> 16));
        }
        z += dz; r += dr; g += dg; bl += db;
      }
    }
  }
  g_prof2[1] += micros() - tg;
}

} // namespace Gaslight
