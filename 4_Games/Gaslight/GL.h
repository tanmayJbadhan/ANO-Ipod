#pragma once
// Gaslight & Griffins - internals shared by the game's files (not part of
// the console's interface; that's Gaslight.h).

#include <stdint.h>
#include <math.h>
#include <string.h>
#include "GLFont.h"
#include "Raster.h"

namespace Gaslight {

static const int W = 456, H = 280;           // landscape frame
static const int SH = 20, NSTRIP = H / SH;    // 14 strips of 20 rows

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static const float PI_F = 3.14159265f;
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
inline uint16_t c565(uint32_t hex) {
  return (uint16_t)(((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex & 0xFF) >> 3));
}
uint16_t mix565(uint16_t a, uint16_t b, int t256); // t=0 -> a, 256 -> b
uint32_t rnd();
float frand();
uint32_t hash32(uint32_t a, uint32_t b = 0);
float hashf(int a, int b, uint32_t seed = 0);

// ---------------------------------------------------------------------------
// 3D: meshes in world space with baked vertex colour (0..31 / 0..63 / 0..31)
// ---------------------------------------------------------------------------
struct Vtx { float x, y, z, r, g, b; };
struct Face { uint16_t a, b, c; };
struct MeshBuf {
  Vtx *v = nullptr;
  Face *f = nullptr;
  int nv = 0, nf = 0, capV = 0, capF = 0;
  uint8_t *away = nullptr; // optional: a bit per face that never faces the camera
};
// Marks faces of a still mesh that point away from a camera with this pitch
// and yaw from anywhere it can stand (needs the mesh's away bits allocated).
void markAway(MeshBuf &m, float pitch, float yaw);
bool meshAlloc(MeshBuf &m, int capV, int capF);
void meshFree(MeshBuf &m);
inline void meshReset(MeshBuf &m) { m.nv = m.nf = 0; }

// Light from the sky (a direction) plus ambient; the town sets these.
struct Lighting {
  float sx = -0.4f, sy = 0.75f, sz = 0.5f;  // toward the light, normalised
  float amb = 0.45f, dif = 0.6f;
  uint32_t tint = 0xFFFFFF;                 // light colour
};
extern Lighting g_light;

// Each builder lights its faces by their normal; colours are 0xRRGGBB.
int addVert(MeshBuf &m, float x, float y, float z, uint32_t col, float bright);
void addTri(MeshBuf &m, int a, int b, int c);
// Quad from 4 corners (counter-clockwise seen from the lit side), flat-lit.
void addQuad(MeshBuf &m, const float *p, uint32_t col, float glow = 0);
// Axis box, optionally turned by `ang` about the vertical through (cx, cz).
void addBox(MeshBuf &m, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t col,
            float ang = 0, float cx = 0, float cz = 0, float glow = 0);
// Box with separate top colour (cliffs, crates).
void addBox2(MeshBuf &m, float x0, float y0, float z0, float x1, float y1, float z1, uint32_t side, uint32_t top);
// Gabled roof over [x0,x1]x[z0,z1] from height y0 to ridge y1 (ridge along x).
void addRoof(MeshBuf &m, float x0, float z0, float x1, float z1, float y0, float y1, uint32_t col, bool ridgeX);
// Low-poly ellipsoid (seg around, rings up).
void addBlob(MeshBuf &m, float cx, float cy, float cz, float rx, float ry, float rz, uint32_t col, int seg = 8,
             int rings = 5, float glow = 0);
// Upright prism/cylinder with n sides, radii at bottom and top.
void addCyl(MeshBuf &m, float cx, float y0, float cz, float r0, float y1, float r1, uint32_t col, int n = 8,
            float glow = 0);
// Cone (n sides) - trees.
inline void addCone(MeshBuf &m, float cx, float y0, float cz, float r, float y1, uint32_t col, int n = 7) {
  addCyl(m, cx, y0, cz, r, y1, 0.0f, col, n);
}
// Rotated sub-box helper for limbs: centre, half sizes, yaw.
void addLimb(MeshBuf &m, float cx, float cy, float cz, float hx, float hy, float hz, float yaw, uint32_t col,
             float pitch = 0);

// A flat disc on the ground (shadows); an upright panel facing `yaw` (eyes, signs).
void addDisc(MeshBuf &m, float cx, float y, float cz, float r, uint32_t col, int n = 6);
void addPanel(MeshBuf &m, float cx, float cy, float cz, float hw, float hh, float yaw, uint32_t col, float glow = 0);
// Warm point lights added into static vertices after building (lamps).
struct PointLight { float x, y, z, rad, r, g, b; };
void bakeLights(MeshBuf &m, int v0, int v1, const PointLight *L, int n);

// Camera: looks at (tx, ty, tz) from `dist` away, tilted down by `pitch`,
// turned by `yaw` about the vertical.
struct Camera {
  float tx = 0, ty = 0, tz = 0, dist = 13, pitch = 0.92f, yaw = 0, focal = 390;
  // derived
  float ex, ey, ez, rx, ry, rz, ux, uy, uz, fx, fy, fz;
  void update();
  // world -> screen; false if behind the camera
  bool project(float x, float y, float z, float &sx, float &sy, float &depth) const;
};
extern Camera g_cam;

struct Fog { uint32_t col = 0x3E5566; float start = 10, end = 30, max = 0.8f; };
extern Fog g_fog;

// Collects what's visible this frame. Call begin, add meshes, end.
void sceneBegin();
void sceneAdd(const MeshBuf &m, int f0 = 0, int f1 = -1, int v0 = 0, int v1 = -1);
void sceneEnd();
extern float g_nearCut; // sceneAdd drops triangles with a corner nearer than this
int sceneTris();
// Draws the scene's triangles into the current strip (colour + depth).
void drawScene3D(uint16_t *col, uint16_t *depth, int top);
// The ground plane: a texture laid over y = 0, `tpu` texels per world unit.
// Texels outside it show the sky (other meshes can fill the distance).
struct Ground {
  const uint16_t *tex = nullptr; int tw = 0, th = 0, tpu = 16; float y = 0;
  const uint16_t *mip[3] = {}; // level 0 = tex, then half and quarter size
  // beyond the texture: north of it, and everywhere else (sky if transparent)
  uint16_t outNorth = 0, outRest = 0;
  bool northSky = true, restSky = true;
};
extern Ground g_ground;
// Sky colours by screen row, used where nothing is drawn.
extern uint16_t g_sky[H];
void setSky(uint32_t top, uint32_t bottom);

// ---------------------------------------------------------------------------
// 2D drawing into the current strip (uses Raster's per-core target)
// ---------------------------------------------------------------------------
int textW(const FontInfo &f, const char *s, int n = -1);
// Draws text with its line box's top at y. `{...}` is drawn in `hi` colour.
int text(const FontInfo &f, int x, int y, const char *s, uint16_t col, uint16_t hi = 0, int n = -1);
void textC(const FontInfo &f, int cx, int y, const char *s, uint16_t col);
void textShadow(const FontInfo &f, int x, int y, const char *s, uint16_t col);
// Word wraps into maxW; returns the number of lines. Draws only lines
// [skip, skip+maxLines) and only `chars` characters in all (typewriter).
int wrap(const FontInfo &f, int x, int y, int maxW, const char *s, uint16_t col, uint16_t hi, int lineH,
         int maxLines = 99, int chars = 100000, bool draw = true);
int wrapLineStart(const FontInfo &f, int maxW, const char *s, int line);
void panel(float x, float y, float w, float h, uint16_t fill, uint8_t alpha, uint16_t edge);
void fadeRect(float x, float y, float w, float h, uint16_t c, uint8_t a);
// Additive soft light: brightest at the centre, fading to nothing at r.
void glow(float cx, float cy, float r, uint16_t c, int strength);
// Profiling counters (microseconds), printed by the bench stats.
extern volatile uint32_t g_prof[4];

// Filled triangle / quad from loose coordinates
inline void poly3(float a, float b, float c, float d, float e, float f, uint16_t col, uint8_t al = 255) {
  float p[6] = {a, b, c, d, e, f};
  Raster::poly(p, 3, col, al);
}
inline void poly4(float a, float b, float c, float d, float e, float f, float g, float h, uint16_t col, uint8_t al = 255) {
  float p[8] = {a, b, c, d, e, f, g, h};
  Raster::poly(p, 4, col, al);
}

// Palette shared by UI and art
namespace Pal {
static const uint32_t FOG = 0x3E5566, FOG2 = 0x9DB4B8, AMBER = 0xE8A33D, BRASS = 0xB9853A, OXBLOOD = 0x7A2430,
                      VERDIGRIS = 0x4F8A78, INK = 0x141B26, CREAM = 0xF3E3C0, PAPER = 0xE9DCC0;
}

} // namespace Gaslight
