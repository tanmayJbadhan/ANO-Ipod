// Gaslight & Griffins - the world: tile maps turned into lit low-poly 3D,
// walking on the grid, doors, people, wild creatures in the long grass.
#pragma GCC optimize("O2")
#include "World.h"
#include "Raster.h"
#include "RunAudio.h"
#include <esp_heap_caps.h>
#include <Arduino.h>
#include <stdio.h>

namespace Gaslight {

Actor g_actor[MAX_ACTORS];
int g_nActors = 0;
Actor g_player;
uint8_t g_area = A_TOWN;

static MeshBuf s_static, s_dyn;
static const int CH = 8; // chunk size in tiles
struct Chunk { int x0, z0, x1, z1; int v0, v1, f0, f1; };
static Chunk s_chunk[64];
static int s_nChunks = 0;
static int s_always0v, s_always1v, s_always0f, s_always1f; // sea and far scenery
struct Glow { float x, y, z, r; uint16_t c; };
static Glow s_glow[48];
static int s_nGlow = 0;
static PointLight s_lights[48];
static int s_nLights = 0;
static float s_bannerT = 0;
static const char *s_banner = nullptr;
static Actor s_follow;
// the player's own camera: turned by the wheel or a sideways drag, pulled in
// close by a drag up or down (0 = the usual view, 1 = right beside Wren)
static float s_yaw = 0, s_yawGoal = 0, s_zoom = 0, s_zoomGoal = 0;
static int s_retArea = -1, s_retX = 0, s_retZ = 0; // the door to go back out by
static bool s_retFresh = false;
static int s_outArea = -1, s_outX = 0, s_outZ = 0; // last place outdoors, for interiors no door leads to // set by a door (or the inn) just before the load that uses it

void worldTurn(float rad) {
  if (AREAS[g_area].indoor) s_yawGoal = clampf(s_yawGoal + rad, -0.55f, 0.55f); // rooms have no front wall
  else s_yawGoal += rad;
}
void worldZoom(float d) { s_zoomGoal = clampf(s_zoomGoal + d, 0, 1); }
void worldCameraReset() { s_yawGoal = 0; s_zoomGoal = 0; }
void worldGoInn() {
  int town = getVar("town");
  static const uint8_t TOWNS[6] = {A_TOWN, A_TOWN, A_SAND, A_GREEN, A_SKJOLD, A_UR};
  if (town <= 1) { worldLoad(A_QUILL, 5, 3, F_S); return; }
  int ta = TOWNS[town > 5 ? 5 : town];
  const AreaDef &a = AREAS[ta];
  for (int i = 0; i < a.nb; i++)
    if (a.b[i].dest == A_INN) { s_retArea = ta; s_retX = a.b[i].doorX; s_retZ = a.b[i].z + a.b[i].d; s_retFresh = true; }
  worldLoad(A_INN, 4, 3, F_S);
}
void worldCameraToggle() { s_zoomGoal = s_zoomGoal > 0.5f ? 0 : 1; }
float worldYaw() { return s_yaw; }

// Which way on the map a screen direction means at the camera's angle
static int quarter() {
  float q = s_yaw / (PI_F * 0.5f);
  return ((int)floorf(q + 0.5f)) & 3;
}
static bool s_followOn = false;

// a warp in progress (fade out, load, fade in)
static int s_warpArea = -1, s_warpX, s_warpZ, s_warpFace;
static float s_warpT = 0;

// a trainer who has seen you
static int s_spotter = -1;
static float s_spotT = 0;

bool groundTexAlloc();
void groundTexFree();
void buildGroundTex(int area, const PointLight *lights, int nLights);

bool worldAlloc() {
  bool ok = groundTexAlloc();
  if (!s_static.v) {
    ok = meshAlloc(s_static, 16000, 10000) && ok;
    s_static.away = (uint8_t *)heap_caps_calloc((10000 + 7) / 8, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (!s_dyn.v) ok = meshAlloc(s_dyn, 5000, 3500) && ok;
  return ok;
}
void worldFree() { heap_caps_free(s_static.away); s_static.away = nullptr; meshFree(s_static); meshFree(s_dyn); groundTexFree(); }

// --- tiles --------------------------------------------------------------------------------

static const AreaDef &A() { return AREAS[g_area]; }

char tileAt(int area, int x, int z) {
  const AreaDef &a = AREAS[area];
  if (x < 0 || z < 0 || x >= a.w || z >= a.d) return '^';
  return a.rows[z][x];
}

static int buildingAt(int x, int z) {
  const AreaDef &a = A();
  for (int i = 0; i < a.nb; i++) {
    const Building &b = a.b[i];
    if (x >= b.x && x < b.x + b.w && z >= b.z && z < b.z + b.d) return i;
  }
  return -1;
}

static bool blockedChar(char c) { return strchr("~^#LTbBNgwMPRotcKCFYXGOZ", c) != nullptr; }

static Actor *actorAt(int x, int z) {
  for (int i = 0; i < g_nActors; i++) {
    Actor &a = g_actor[i];
    if (!showIfOk(a.showIf)) continue;
    if ((int)a.x == x && (int)a.z == z) return &a;
    if (a.moving && (int)a.tx == x && (int)a.tz == z) return &a;
  }
  return nullptr;
}

bool walkable(int x, int z) {
  char c = tileAt(g_area, x, z);
  if (blockedChar(c)) return false;
  if (buildingAt(x, z) >= 0) return false;
  if (actorAt(x, z)) return false;
  // signposts, dens and caches stand in the way
  for (int i = N_SPOTS + N_SPOTS_MORE; i < spotCount(); i++) {
    const SpotDef &s = spotAt(i);
    if (s.area == g_area && s.x == x && s.z == z && showIfOk(s.showIf)) return false;
  }
  return true;
}

bool showIfOk(const char *cond) {
  if (!cond || !cond[0]) return true;
  if (cond[0] == '=') return getVar("town") == atoi(cond + 1);
  if (cond[0] == '!') return !flag(cond + 1);
  return flag(cond);
}

// --- building the static scene ------------------------------------------------------------------

static uint32_t mixHex(uint32_t a, uint32_t b, float t) {
  int ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255;
  int rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
  return ((uint32_t)(ra + (rb - ra) * t) << 16) | ((uint32_t)(ga + (gb - ga) * t) << 8) | (uint32_t)(ba + (bb - ba) * t);
}
static uint32_t shade(uint32_t c, float k) {
  int r = clampi((int)(((c >> 16) & 255) * k), 0, 255), g = clampi((int)(((c >> 8) & 255) * k), 0, 255),
      b = clampi((int)((c & 255) * k), 0, 255);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t groundCol(char c, bool indoor) {
  if (indoor == 1 && c != '~' && c != ':' && c != '^') return 0x6A4A30; // floorboards
  switch (c) {
    case '~': return 0x1E3F52;
    case ',': case 'T': case 'g': case 'N': return 0x4C6A3E;
    case 'v': return 0x55703A;
    case ':': return 0x8E8574;
    case '^': return 0x55574F;
    case '=': return 0x5A4632;
    case 'E': return indoor ? 0x6A4A30 : 0x77746A;
    default: return 0x77746A; // cobbles
  }
}

static float groundY(int vx, int vz) {
  // A vertex is under water only if every tile touching it is water.
  int water = 0, n = 0;
  for (int dz = -1; dz <= 0; dz++)
    for (int dx = -1; dx <= 0; dx++) {
      char c = tileAt(g_area, vx + dx, vz + dz);
      if (vx + dx < 0 || vz + dz < 0 || vx + dx >= A().w || vz + dz >= A().d) c = tileAt(g_area, clampi(vx + dx, 0, A().w - 1), clampi(vz + dz, 0, A().d - 1));
      n++;
      water += c == '~';
    }
  return water == n ? -0.38f : 0.0f;
}

static void addLamp(float x, float z) {
  MeshBuf &m = s_static;
  addCyl(m, x, 0, z, 0.06f, 2.05f, 0.05f, 0x2A2E33, 4);
  addBox(m, x - 0.16f, 2.05f, z - 0.16f, x + 0.16f, 2.42f, z + 0.16f, 0xFFD27A, 0, 0, 0, 0.9f);
  addCyl(m, x, 2.42f, z, 0.21f, 2.6f, 0.0f, 0x23272B, 4);
  if (s_nGlow < 48) s_glow[s_nGlow++] = {x, 2.25f, z, 1.0f, c565(0xFFB451)};
  if (s_nLights < 48) s_lights[s_nLights++] = {x, 1.6f, z, 3.4f, 9.0f, 14.0f, 2.5f};
}

static int biome() { return AREAS[g_area].biome; }

static void addTree(float x, float z, uint32_t seed, bool windswept) {
  MeshBuf &m = s_static;
  float s = 0.8f + (seed & 255) / 255.0f * 0.5f;
  switch (biome()) {
    case 1: { // a date palm: a leaning trunk and a crown of fronds
      float lx = ((seed >> 4) & 1) ? 0.25f : -0.25f;
      addCyl(m, x, 0, z, 0.1f, 1.4f * s, 0.08f, 0x8A6A4A, 5);
      addCyl(m, x + lx * 0.4f, 1.4f * s, z, 0.08f, 2.6f * s, 0.07f, 0x7A5A3A, 5);
      float tx = x + lx, ty = 2.6f * s;
      for (int i = 0; i < 6; i++) {
        float a = i * 1.047f + (seed & 7) * 0.2f, cx = cosf(a), cz = sinf(a);
        float q[12] = {tx, ty, z, tx + cx * 0.3f - cz * 0.25f, ty + 0.15f, z + cz * 0.3f + cx * 0.25f,
                       tx + cx * 1.3f, ty - 0.55f, z + cz * 1.3f, tx + cx * 0.3f + cz * 0.25f, ty + 0.15f, z + cz * 0.3f - cx * 0.25f};
        addQuad(m, q, i & 1 ? 0x4A7A3A : 0x5A8A42);
        float r[12] = {q[9], q[10], q[11], q[6], q[7], q[8], q[3], q[4], q[5], q[0], q[1], q[2]};
        addQuad(m, r, 0x3A6A2E);
      }
      return;
    }
    case 2: { // an old oak: a broad trunk and a cloud of leaves
      addCyl(m, x, 0, z, 0.2f * s, 1.3f * s, 0.15f * s, 0x4A3A2A, 6);
      uint32_t leaf = (seed >> 8) & 1 ? 0x3A5A2E : 0x46682F;
      addBlob(m, x, 2.0f * s, z, 1.1f * s, 0.85f * s, 1.1f * s, leaf, 7, 4);
      addBlob(m, x + 0.5f * s, 2.6f * s, z - 0.3f * s, 0.7f * s, 0.6f * s, 0.7f * s, 0x527A38, 6, 3);
      return;
    }
    case 3: { // a pine under snow
      addCyl(m, x, 0, z, 0.12f * s, 0.6f * s, 0.1f * s, 0x4A3826, 5);
      addCone(m, x, 0.4f * s, z, 0.9f * s, 1.8f * s, 0x2A4A3A, 7);
      addCone(m, x, 1.1f * s, z, 0.7f * s, 2.5f * s, 0xD8E0E8, 7);
      addCone(m, x, 1.7f * s, z, 0.45f * s, 3.1f * s, 0xE8EEF4, 6);
      return;
    }
    case 4: // a cypress, tall and thin
      addCyl(m, x, 0, z, 0.1f, 0.5f, 0.08f, 0x4A3826, 5);
      addCyl(m, x, 0.4f, z, 0.35f * s, 3.4f * s, 0.0f, 0x2A4A32, 7);
      return;
    default: break;
  }
  addCyl(m, x, 0, z, 0.12f * s, 0.9f * s, 0.09f * s, 0x4A3826, 5);
  uint32_t leaf = (seed >> 8) & 1 ? 0x2F4F3A : 0x36553C;
  float lean = windswept ? 0.35f : 0;
  addCone(m, x + lean * 0.3f, 0.6f * s, z, 0.85f * s, 2.0f * s, leaf, 7);
  addCone(m, x + lean * 0.6f, 1.3f * s, z, 0.62f * s, 2.8f * s, shade(leaf, 1.12f), 7);
}

static void addBoat(float x, float z, uint32_t seed) {
  MeshBuf &m = s_static;
  uint32_t hull = (seed & 1) ? 0x5A3A2A : 0x2E4A5A;
  addBox(m, x - 0.45f, -0.35f, z - 1.1f, x + 0.45f, 0.15f, z + 1.1f, hull);
  addBox(m, x - 0.32f, 0.15f, z - 0.9f, x + 0.32f, 0.22f, z + 0.9f, 0x7A6248);
  addCyl(m, x, 0.15f, z - 0.2f, 0.04f, 2.3f, 0.03f, 0x3A2E24, 4);
  float sail[12] = {x, 0.6f, z - 0.15f, x, 0.6f, z + 0.9f, x, 2.1f, z - 0.15f, x, 2.1f, z - 0.2f};
  addQuad(m, sail, 0xD8CBB0);
  float sail2[12] = {x, 0.6f, z + 0.9f, x, 0.6f, z - 0.15f, x, 2.1f, z - 0.2f, x, 2.1f, z - 0.15f};
  addQuad(m, sail2, 0xC8BBA0);
}

static void addStatue(float x, float z) {
  MeshBuf &m = s_static;
  switch (biome()) {
    case 1: { // the Sphinx of Sandmere: a lion's body, a calm face
      uint32_t st = 0xD8B880;
      addBox2(m, x - 1.0f, 0, z - 1.0f, x + 1.0f, 0.5f, z + 1.0f, 0xB89868, 0xC8A878);
      addBlob(m, x, 0.95f, z - 0.15f, 0.55f, 0.5f, 0.85f, st, 8, 5);
      addBox(m, x - 0.45f, 0.5f, z + 0.35f, x - 0.2f, 0.75f, z + 0.95f, st);
      addBox(m, x + 0.2f, 0.5f, z + 0.35f, x + 0.45f, 0.75f, z + 0.95f, st);
      addBox(m, x - 0.32f, 1.2f, z + 0.25f, x + 0.32f, 2.0f, z + 0.75f, st);
      addBox(m, x - 0.42f, 1.55f, z + 0.2f, x + 0.42f, 2.15f, z + 0.6f, 0x3A5A8A); // the striped headdress
      addBox(m, x - 0.25f, 1.25f, z + 0.74f, x + 0.25f, 1.85f, z + 0.78f, 0xE0C890);
      return;
    }
    case 2: { // the Green Chapel: a grassy barrow with a dark mouth
      addBlob(m, x, 0, z, 1.2f, 1.3f, 1.1f, 0x46682F, 8, 5);
      addBox(m, x - 0.35f, 0, z + 0.85f, x + 0.35f, 0.9f, z + 1.02f, 0x141410);
      addBox(m, x - 0.5f, 0, z + 0.9f, x - 0.35f, 1.1f, z + 1.05f, 0x6A6A60);
      addBox(m, x + 0.35f, 0, z + 0.9f, x + 0.5f, 1.1f, z + 1.05f, 0x6A6A60);
      addBox(m, x - 0.55f, 1.0f, z + 0.9f, x + 0.55f, 1.2f, z + 1.05f, 0x6A6A60);
      return;
    }
    case 3: { // the Norns' ash: a vast trunk and a crown that fills the sky
      addCyl(m, x, 0, z, 0.75f, 3.0f, 0.5f, 0x5A4A3A, 8);
      for (int i = 0; i < 5; i++) {
        float a = i * 1.257f;
        addBox(m, x + cosf(a) * 0.4f, 0, z + sinf(a) * 0.4f, x + cosf(a) * 1.1f, 0.35f, z + sinf(a) * 1.1f + 0.2f, 0x4A3A2A, a, x, z);
      }
      addBlob(m, x, 4.2f, z, 2.2f, 1.4f, 2.0f, 0x5A7A5A, 8, 5);
      addBlob(m, x + 0.8f, 5.0f, z - 0.4f, 1.3f, 0.9f, 1.2f, 0xE0E8EE, 7, 4);
      return;
    }
    case 4: { // a winged bull of the city gate (each statue is one tile here)
      return;
    }
    default: break;
  }
  // The Three Sisters of Wailing Rock, in old verdigris bronze.
  addBox2(m, x - 0.95f, 0, z - 0.95f, x + 0.95f, 0.7f, z + 0.95f, 0x8A877C, 0x9C988C);
  for (int i = 0; i < 3; i++) {
    float a = i * 2.094f + 0.5f, sx = x + cosf(a) * 0.38f, sz = z + sinf(a) * 0.38f;
    uint32_t br = 0x4F8A78;
    addCyl(m, sx, 0.7f, sz, 0.2f, 1.6f, 0.12f, br, 6);
    addBlob(m, sx, 1.75f, sz, 0.13f, 0.15f, 0.13f, shade(br, 1.1f), 6, 4);
    // wings folded up behind
    float wx = cosf(a), wz = sinf(a);
    float w1[12] = {sx, 1.0f, sz, sx + wx * 0.45f - wz * 0.2f, 1.2f, sz + wz * 0.45f + wx * 0.2f,
                    sx + wx * 0.5f, 2.1f, sz + wz * 0.5f, sx, 1.7f, sz};
    addQuad(m, w1, shade(br, 0.9f));
    float w2[12] = {sx, 1.7f, sz, sx + wx * 0.5f, 2.1f, sz + wz * 0.5f, sx + wx * 0.45f - wz * 0.2f, 1.2f,
                    sz + wz * 0.45f + wx * 0.2f, sx, 1.0f, sz};
    addQuad(m, w2, shade(br, 0.8f));
  }
}

static void addWindow(float x0, float y0, float x1, float y1, float z, bool south, bool lit) {
  // a lit pane slightly proud of the wall, with a dark frame
  float o = south ? 0.03f : -0.03f;
  uint32_t glass = lit ? 0xFFC56A : 0x3A4652;
  float fr[12] = {x0 - 0.05f, y0 - 0.05f, z + o * 0.5f, x1 + 0.05f, y0 - 0.05f, z + o * 0.5f,
                  x1 + 0.05f, y1 + 0.05f, z + o * 0.5f, x0 - 0.05f, y1 + 0.05f, z + o * 0.5f};
  float pn[12] = {x0, y0, z + o, x1, y0, z + o, x1, y1, z + o, x0, y1, z + o};
  if (!south) {
    float t[12] = {fr[3], fr[4], fr[5], fr[0], fr[1], fr[2], fr[9], fr[10], fr[11], fr[6], fr[7], fr[8]};
    memcpy(fr, t, sizeof(t));
    float u[12] = {pn[3], pn[4], pn[5], pn[0], pn[1], pn[2], pn[9], pn[10], pn[11], pn[6], pn[7], pn[8]};
    memcpy(pn, u, sizeof(u));
  }
  addQuad(s_static, fr, 0x2A2420);
  addQuad(s_static, pn, glass, lit ? 0.85f : 0);
}

static void addSideWindow(float x, float y0, float z0, float y1, float z1, bool east, bool lit) {
  float o = east ? 0.03f : -0.03f;
  uint32_t glass = lit ? 0xFFC56A : 0x3A4652;
  if (east) {
    float p[12] = {x + o, y0, z1, x + o, y0, z0, x + o, y1, z0, x + o, y1, z1};
    addQuad(s_static, p, glass, lit ? 0.85f : 0);
  } else {
    float p[12] = {x + o, y0, z0, x + o, y0, z1, x + o, y1, z1, x + o, y1, z0};
    addQuad(s_static, p, glass, lit ? 0.85f : 0);
  }
}

static void addBuilding(const Building &b, int idx) {
  MeshBuf &m = s_static;
  float x0 = b.x + 0.05f, z0 = b.z + 0.05f, x1 = b.x + b.w - 0.05f, z1 = b.z + b.d - 0.05f;
  uint32_t seed = hash32(b.x, b.z);
  if (b.style == 2) { // lighthouse
    float cx = b.x + b.w * 0.5f, cz = b.z + b.d * 0.5f;
    addCyl(m, cx, 0, cz, 1.45f, 0.5f, 1.4f, 0x8A877C, 8);
    for (int i = 0; i < 4; i++) {
      float y0 = 0.5f + i * 1.6f, y1 = y0 + 1.6f, r0 = 1.25f - i * 0.15f, r1 = r0 - 0.15f;
      addCyl(m, cx, y0, cz, r0, y1, r1, i & 1 ? 0x9A2A2E : 0xE6E0CE, 8);
    }
    float top = 0.5f + 4 * 1.6f;
    addCyl(m, cx, top, cz, 0.95f, top + 0.15f, 0.95f, 0x2A2E33, 8);
    bool lampOn = !flag("light_dark");
    addCyl(m, cx, top + 0.15f, cz, 0.55f, top + 0.95f, 0.55f, lampOn ? 0xFFE6A0 : 0x3A4452, 8, lampOn ? 1.0f : 0);
    addCyl(m, cx, top + 0.95f, cz, 0.7f, top + 1.5f, 0.0f, 0x7A2430, 8);
    if (lampOn && s_nGlow < 48) s_glow[s_nGlow++] = {cx, top + 0.55f, cz, 3.2f, c565(0xFFE3A0)};
    // door
    float d[12] = {b.doorX + 0.2f, 0, cz + 1.29f, b.doorX + 0.8f, 0, cz + 1.29f, b.doorX + 0.8f, 1.3f, cz + 1.29f,
                   b.doorX + 0.2f, 1.3f, cz + 1.29f};
    addQuad(m, d, 0x3A2A1E);
    return;
  }
  if (b.style == 3) { // the Tide Lodge: an old ship hauled up on blocks
    float cx = (x0 + x1) * 0.5f;
    addBox(m, x0 + 0.4f, 0, z0 + 0.6f, x0 + 1.0f, 0.7f, z1 - 0.6f, 0x4A4A44);
    addBox(m, x1 - 1.0f, 0, z0 + 0.6f, x1 - 0.4f, 0.7f, z1 - 0.6f, 0x4A4A44);
    addBox(m, x0, 0.7f, z0 + 0.3f, x1, 2.0f, z1 - 0.3f, 0x4E3A2A);
    addBox(m, x0 + 0.2f, 2.0f, z0 + 0.5f, x1 - 0.2f, 2.15f, z1 - 0.5f, 0x7A6248);
    // bow and stern
    float bow[12] = {x1, 0.7f, z1 - 0.3f, x1 + 1.2f, 1.4f, (z0 + z1) * 0.5f, x1 + 1.2f, 2.1f, (z0 + z1) * 0.5f, x1, 2.0f, z1 - 0.3f};
    addQuad(m, bow, 0x5A4432);
    float bow2[12] = {x1 + 1.2f, 1.4f, (z0 + z1) * 0.5f, x1, 0.7f, z0 + 0.3f, x1, 2.0f, z0 + 0.3f, x1 + 1.2f, 2.1f, (z0 + z1) * 0.5f};
    addQuad(m, bow2, 0x4A3828);
    addBox(m, x0 + 0.5f, 2.15f, z0 + 0.8f, x0 + 2.3f, 3.1f, z1 - 0.8f, 0x5E4A36);
    addRoof(m, x0 + 0.5f, z0 + 0.8f, x0 + 2.3f, z1 - 0.8f, 3.1f, 3.6f, 0x2E2620, true);
    addCyl(m, cx + 0.8f, 2.15f, (z0 + z1) * 0.5f, 0.09f, 6.0f, 0.06f, 0x3A2E24, 5);
    addBox(m, cx - 0.4f, 4.6f, (z0 + z1) * 0.5f - 0.05f, cx + 2.0f, 4.7f, (z0 + z1) * 0.5f + 0.05f, 0x3A2E24);
    for (int i = 0; i < 4; i++) addWindow(x0 + 1.4f + i * 1.5f, 1.2f, x0 + 1.75f + i * 1.5f, 1.55f, z1 - 0.3f, true, true);
    // gangway to the door
    float dx = b.doorX + 0.5f;
    float g[12] = {dx - 0.4f, 0.02f, z1 + 0.9f, dx + 0.4f, 0.02f, z1 + 0.9f, dx + 0.4f, 0.75f, z1 - 0.3f, dx - 0.4f, 0.75f, z1 - 0.3f};
    addQuad(m, g, 0x6A5238);
    float d[12] = {dx - 0.35f, 0.75f, z1 - 0.28f, dx + 0.35f, 0.75f, z1 - 0.28f, dx + 0.35f, 1.85f, z1 - 0.28f, dx - 0.35f, 1.85f, z1 - 0.28f};
    addQuad(m, d, 0x2A1E16);
    return;
  }
  if (b.style == 6 || b.style == 7 || b.style == 9) {
    // flat-roofed: adobe houses, a temple with columns, the ziggurat
    if (b.style == 9) {
      float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
      for (int t = 0; t < 4; t++) {
        float in = t * 1.1f, y0 = t * 1.0f;
        addBox2(m, x0 + in, y0, z0 + in * 0.6f, x1 - in, y0 + 1.0f, z1 - in * 0.6f, shade(b.wall, 1 - t * 0.06f), shade(b.wall, 1.05f));
      }
      addBox(m, cx - 0.6f, 4.0f, cz - 0.6f, cx + 0.6f, 5.0f, cz + 0.6f, 0xFFD27A, 0, 0, 0, 0.9f); // the shrine at the top
      if (s_nGlow < 48) s_glow[s_nGlow++] = {cx, 4.5f, cz, 2.2f, c565(0xFFC870)};
      // the stair up the front
      float dx = b.doorX + 0.5f;
      for (int st = 0; st < 8; st++)
        addBox(m, dx - 0.7f, 0, z1 - 0.1f - st * 0.36f, dx + 0.7f, 0.25f + st * 0.5f, z1 + 0.3f - st * 0.36f, 0xB89870);
      float d[12] = {dx - 0.4f, 0, z1 + 0.32f, dx + 0.4f, 0, z1 + 0.32f, dx + 0.4f, 1.3f, z1 + 0.32f, dx - 0.4f, 1.3f, z1 + 0.32f};
      addQuad(m, d, 0x1A1410);
      return;
    }
    addBox2(m, x0, 0, z0, x1, b.h, z1, b.wall, shade(b.wall, 1.08f));
    addBox(m, x0 - 0.05f, b.h, z0 - 0.05f, x1 + 0.05f, b.h + 0.25f, z0 + 0.15f, shade(b.wall, 0.9f)); // parapet
    addBox(m, x0 - 0.05f, b.h, z1 - 0.15f, x1 + 0.05f, b.h + 0.25f, z1 + 0.05f, shade(b.wall, 0.9f));
    if (b.style == 7) {
      // a portico of painted columns along the front
      for (float cxp = x0 + 0.5f; cxp < x1; cxp += 1.4f) {
        addCyl(m, cxp, 0, z1 + 0.6f, 0.2f, b.h, 0.17f, 0xE0D0A8, 6);
        addBox(m, cxp - 0.25f, b.h - 0.3f, z1 + 0.35f, cxp + 0.25f, b.h, z1 + 0.85f, 0x3A5A8A);
      }
      addBox(m, x0 - 0.1f, b.h, z1, x1 + 0.1f, b.h + 0.35f, z1 + 0.95f, shade(b.wall, 0.95f));
    }
    float dx = b.doorX + 0.5f, dz = z1 + 0.02f;
    float door[12] = {dx - 0.38f, 0, dz, dx + 0.38f, 0, dz, dx + 0.38f, 1.55f, dz, dx - 0.38f, 1.55f, dz};
    addQuad(m, door, 0x2A1E16);
    for (float wx = x0 + 0.6f; wx < x1 - 0.5f; wx += 1.3f) {
      if (fabsf(wx + 0.2f - dx) < 0.8f) continue;
      addWindow(wx, 1.0f, wx + 0.4f, 1.5f, z1, true, hash32((int)(wx * 7), b.z) % 3 != 0);
    }
    if (b.sign && s_nGlow < 48) s_glow[s_nGlow++] = {dx + 0.6f, 1.7f, dz + 0.1f, 0.6f, c565(0xFFB451)};
    return;
  }
  if (b.style == 8) {
    // a longhouse: low walls under a great steep roof, carved gable posts
    addBox(m, x0, 0, z0, x1, b.h * 0.6f, z1, b.wall);
    addRoof(m, x0 - 0.2f, z0 - 0.2f, x1 + 0.2f, z1 + 0.2f, b.h * 0.5f, b.h + 1.6f, b.roof, true);
    addBox(m, x0 - 0.2f, b.h * 0.5f, z0 - 0.3f, x1 + 0.2f, b.h * 0.5f + 0.12f, z1 + 0.3f, 0xE8EEF4); // snow on the eaves
    float zm = (z0 + z1) * 0.5f;
    addBox(m, x0 - 0.15f, b.h + 1.4f, zm - 0.1f, x0 + 0.05f, b.h + 2.2f, zm + 0.1f, 0x3A2A1E, 0.4f, x0, zm);
    addBox(m, x1 - 0.05f, b.h + 1.4f, zm - 0.1f, x1 + 0.15f, b.h + 2.2f, zm + 0.1f, 0x3A2A1E, -0.4f, x1, zm);
    float dx = b.doorX + 0.5f, dz = z1 + 0.02f;
    float door[12] = {dx - 0.42f, 0, dz, dx + 0.42f, 0, dz, dx + 0.42f, 1.3f, dz, dx - 0.42f, 1.3f, dz};
    addQuad(m, door, 0x2A1E16);
    addBox(m, dx + 0.55f, 1.0f, dz, dx + 0.75f, 1.25f, dz + 0.2f, 0xFFD27A, 0, 0, 0, 0.9f);
    if (s_nGlow < 48) s_glow[s_nGlow++] = {dx + 0.65f, 1.12f, dz + 0.1f, 0.7f, c565(0xFFB451)};
    if (s_nLights < 48) s_lights[s_nLights++] = {dx + 0.65f, 1.1f, dz + 0.6f, 2.4f, 7, 9, 2};
    return;
  }
  float h = b.h;
  addBox(m, x0, 0, z0, x1, h, z1, b.wall);
  // a stone plinth line
  addBox(m, x0 - 0.04f, 0, z0 - 0.04f, x1 + 0.04f, 0.3f, z1 + 0.04f, shade(b.wall, 0.7f));
  float rh = b.style == 4 ? 2.2f : 1.35f;
  addRoof(m, x0, z0, x1, z1, h, h + rh, b.roof, true);
  // chimney
  if (b.style != 4) {
    float chx = x0 + 0.6f + (seed & 3) * 0.4f, chz = z0 + 0.5f;
    addBox(m, chx, h + 0.4f, chz, chx + 0.4f, h + rh + 0.5f, chz + 0.4f, shade(b.wall, 0.8f));
  } else {
    // chapel bell-cote
    float cx = (x0 + x1) * 0.5f, cz = z1 - 0.8f;
    addBox(m, cx - 0.4f, h + rh - 0.6f, cz - 0.4f, cx + 0.4f, h + rh + 0.8f, cz + 0.4f, b.wall);
    addRoof(m, cx - 0.45f, cz - 0.45f, cx + 0.45f, cz + 0.45f, h + rh + 0.8f, h + rh + 1.6f, b.roof, false);
  }
  // door on the south face
  if (b.doorX >= 0) {
    float dx = b.doorX + 0.5f, dz = z1 + 0.02f;
    float frame[12] = {dx - 0.42f, 0, dz, dx + 0.42f, 0, dz, dx + 0.42f, 1.65f, dz, dx - 0.42f, 1.65f, dz};
    addQuad(m, frame, 0x2A2420);
    float door[12] = {dx - 0.33f, 0, dz + 0.01f, dx + 0.33f, 0, dz + 0.01f, dx + 0.33f, 1.5f, dz + 0.01f, dx - 0.33f, 1.5f, dz + 0.01f};
    uint32_t dc = (seed >> 4) & 1 ? 0x5E2A24 : 0x2E4A44;
    addQuad(m, door, dc);
    // step
    addBox(m, dx - 0.5f, 0, dz, dx + 0.5f, 0.1f, dz + 0.3f, 0x6A675E);
    // sign board above, and a lantern for the tavern
    if (b.style == 5) {
      addBox(m, dx + 0.5f, 1.9f, dz, dx + 0.56f, 1.96f, dz + 0.7f, 0x2A2420);
      addBox(m, dx + 0.42f, 1.15f, dz + 0.45f, dx + 0.64f, 1.85f, dz + 0.65f, 0x6A4A2A);
      addBox(m, dx - 0.65f, 1.65f, dz + 0.05f, dx - 0.45f, 1.95f, dz + 0.25f, 0xFFD27A, 0, 0, 0, 0.9f);
      if (s_nGlow < 48) s_glow[s_nGlow++] = {dx - 0.55f, 1.8f, dz + 0.15f, 0.8f, c565(0xFFB451)};
      if (s_nLights < 48) s_lights[s_nLights++] = {dx - 0.55f, 1.4f, dz + 0.6f, 2.6f, 7, 10, 2};
    } else {
      addBox(m, dx - 0.7f, 1.8f, dz, dx + 0.7f, 2.15f, dz + 0.06f, 0x3A2E24);
    }
  }
  // windows: lit at dusk, more on shops
  int cols = (int)((x1 - x0) / 1.1f);
  for (int i = 0; i < cols; i++) {
    float wx = x0 + 0.45f + i * ((x1 - x0 - 0.9f) / (cols > 1 ? cols - 1 : 1)) - 0.22f;
    if (b.doorX >= 0 && fabsf(wx + 0.22f - (b.doorX + 0.5f)) < 0.7f) continue;
    bool lit = hash32(b.x * 31 + i, b.z) % 5 != 0;
    float wy0 = b.style == 1 && i % 2 == 0 ? 0.55f : 0.95f;
    addWindow(wx, wy0, wx + 0.44f, 1.65f, z1, true, lit);
    if (h > 2.7f) addWindow(wx, 2.0f, wx + 0.44f, 2.45f, z1, true, hash32(i, b.x) % 3 != 0);
  }
  for (int i = 0; i < (int)(z1 - z0) - 1; i++) {
    float wz = z0 + 0.8f + i * 1.1f;
    addSideWindow(x1, 0.95f, wz, 1.6f, wz + 0.45f, true, hash32(i, b.z) % 3 != 0);
    addSideWindow(x0, 0.95f, wz, 1.6f, wz + 0.45f, false, hash32(i + 7, b.z) % 3 != 0);
  }
  (void)idx;
}

static void addFurniture(char c, float x, float z, uint32_t seed) {
  MeshBuf &m = s_static;
  switch (c) {
    case 't':
      addBox(m, x + 0.1f, 0.6f, z + 0.1f, x + 0.9f, 0.7f, z + 0.9f, 0x6E4A2E);
      addBox(m, x + 0.45f, 0, z + 0.45f, x + 0.55f, 0.6f, z + 0.55f, 0x4A3220);
      if (seed & 1) addCyl(m, x + 0.35f, 0.7f, z + 0.4f, 0.06f, 0.85f, 0.06f, 0xD8CBB0, 5);
      if (seed & 2) addBox(m, x + 0.55f, 0.7f, z + 0.5f, x + 0.75f, 0.9f, z + 0.65f, 0xFFD27A, 0, 0, 0, 0.8f);
      break;
    case 'c':
      addBox(m, x + 0.25f, 0.38f, z + 0.25f, x + 0.75f, 0.45f, z + 0.75f, 0x5A3A26);
      addBox(m, x + 0.25f, 0, z + 0.25f, x + 0.32f, 0.38f, z + 0.32f, 0x4A3220);
      addBox(m, x + 0.68f, 0, z + 0.68f, x + 0.75f, 0.38f, z + 0.75f, 0x4A3220);
      addBox(m, x + 0.25f, 0.45f, z + 0.25f, x + 0.75f, 1.0f, z + 0.32f, 0x5A3A26);
      break;
    case 'K': {
      addBox(m, x + 0.05f, 0, z + 0.1f, x + 0.95f, 2.1f, z + 0.6f, 0x4A3220);
      static const uint32_t books[5] = {0x7A2430, 0x2E4A5A, 0x4F6B3A, 0xB9853A, 0x5A3A6A};
      for (int s = 0; s < 3; s++)
        for (int k = 0; k < 4; k++) {
          float bx = x + 0.12f + k * 0.2f, by = 0.2f + s * 0.65f;
          addBox(m, bx, by, z + 0.58f, bx + 0.16f, by + 0.42f - ((seed >> (k + s)) & 1) * 0.1f, z + 0.66f,
                 books[(seed >> (k * 2 + s)) % 5]);
        }
      break;
    }
    case 'C': addBox2(m, x, 0, z + 0.15f, x + 1.0f, 1.05f, z + 0.85f, 0x5A3A26, 0x7A5A3A); break;
    case 'F':
      addBox(m, x, 0, z, x + 1.0f, 1.6f, z + 0.5f, 0x6A675E);
      addBox(m, x + 0.2f, 0.1f, z + 0.48f, x + 0.8f, 0.6f, z + 0.52f, 0xFF9A3A, 0, 0, 0, 1.0f);
      if (s_nLights < 48) s_lights[s_nLights++] = {x + 0.5f, 0.6f, z + 1.2f, 3.6f, 6, 5, 0.5f};
      if (s_nGlow < 48) s_glow[s_nGlow++] = {x + 0.5f, 0.35f, z + 0.6f, 1.2f, c565(0xFF8A30)};
      break;
    case 'Y':
      addBox(m, x + 0.05f, 0, z + 0.05f, x + 0.95f, 0.45f, z + 0.95f, 0x5A3A26);
      addBox(m, x + 0.1f, 0.45f, z + 0.1f, x + 0.9f, 0.55f, z + 0.9f, 0xD8CBB0);
      addBox(m, x + 0.1f, 0.55f, z + 0.12f, x + 0.9f, 0.62f, z + 0.35f, 0xEFE6D4);
      addBox(m, x + 0.1f, 0.45f, z + 0.4f, x + 0.9f, 0.6f, z + 0.92f, 0x7A2430);
      break;
    case 'X': addCyl(m, x + 0.5f, 0, z + 0.5f, 0.32f, 0.85f, 0.32f, 0x6A4A2E, 7); break;
    case 'G':
      addBox(m, x + 0.1f, 0, z + 0.2f, x + 0.9f, 0.75f, z + 0.8f, 0x5A3A26);
      addBlob(m, x + 0.5f, 1.0f, z + 0.5f, 0.2f, 0.2f, 0.2f, 0x4F8A78, 7, 5);
      break;
    case 'S': {
      for (int i = 0; i < 4; i++)
        addBox(m, x + 0.1f, -0.05f - i * 0.12f, z + 0.1f + i * 0.2f, x + 0.9f, 0.0f - i * 0.12f, z + 0.3f + i * 0.2f, 0x3A3630);
      break;
    }
    default: break;
  }
}

static void addProps(char c, int tx, int tz, bool indoor) {
  MeshBuf &m = s_static;
  float x = (float)tx, z = (float)tz;
  uint32_t seed = hash32(tx * 73 + g_area, tz);
  float cx = x + 0.5f, cz = z + 0.5f;
  switch (c) {
    case 'L': addLamp(cx, cz); break;
    case 'T': addTree(cx + ((seed & 15) - 7.5f) * 0.02f, cz, seed, g_area == A_CLIFFS); break;
    case 'b': {
      int n = 1 + seed % 3;
      for (int i = 0; i < n; i++) {
        float s = 0.36f + ((seed >> (i * 3)) & 3) * 0.04f, ox = ((seed >> (i * 5)) & 7) * 0.05f - 0.15f;
        float y0 = i == 2 ? 0.6f : 0, oz = i == 1 ? 0.3f : -0.15f;
        addBox2(m, cx + ox - s, y0, cz + oz - s, cx + ox + s, y0 + 2 * s * 0.85f, cz + oz + s, 0x7A5A3A, 0x8A6A46);
      }
      break;
    }
    case 'o': addCyl(m, cx, 0, cz, 0.16f, 0.55f, 0.14f, 0x2A2E33, 6); break;
    case 'B':
      addBox(m, x + 0.1f, 0.4f, z + 0.3f, x + 0.9f, 0.48f, z + 0.7f, 0x6A4A2E);
      addBox(m, x + 0.1f, 0.48f, z + 0.3f, x + 0.9f, 0.85f, z + 0.36f, 0x6A4A2E);
      addBox(m, x + 0.15f, 0, z + 0.4f, x + 0.22f, 0.4f, z + 0.6f, 0x2A2E33);
      addBox(m, x + 0.78f, 0, z + 0.4f, x + 0.85f, 0.4f, z + 0.6f, 0x2A2E33);
      break;
    case 'N': {
      addCyl(m, x + 0.1f, 0, cz, 0.05f, 1.5f, 0.05f, 0x4A3826, 4);
      addCyl(m, x + 0.9f, 0, cz, 0.05f, 1.5f, 0.05f, 0x4A3826, 4);
      float n1[12] = {x + 0.1f, 0.3f, cz, x + 0.9f, 0.3f, cz, x + 0.9f, 1.45f, cz, x + 0.1f, 1.45f, cz};
      addQuad(m, n1, 0x3E4A3A);
      float n2[12] = {x + 0.9f, 0.3f, cz, x + 0.1f, 0.3f, cz, x + 0.1f, 1.45f, cz, x + 0.9f, 1.45f, cz};
      addQuad(m, n2, 0x36402F);
      break;
    }
    case 'g': {
      float h = 0.55f + (seed & 3) * 0.08f;
      addBox(m, cx - 0.22f, 0, cz - 0.06f, cx + 0.22f, h, cz + 0.06f, 0x8A877C, ((seed >> 4) & 7) * 0.03f - 0.1f, cx, cz);
      if (seed & 32) addBox(m, cx - 0.04f, h, cz - 0.04f, cx + 0.04f, h + 0.25f, cz + 0.04f, 0x8A877C);
      break;
    }
    case 'w': addBox2(m, x, 0, z + 0.2f, x + 1, 0.65f, z + 0.8f, 0x6A675E, 0x7E7A70); break;
    case 'M':
      if (biome() == 4) {
        // a winged bull guarding the gate, one per column of 'M'
        if (tileAt(g_area, tx, tz - 1) == 'M') break;
        float bx = cx, bz = z + 1.0f;
        addBox2(m, bx - 0.45f, 0, bz - 0.9f, bx + 0.45f, 0.6f, bz + 0.9f, 0x8A7A68, 0x9A8A78);
        addBox(m, bx - 0.35f, 0.6f, bz - 0.7f, bx + 0.35f, 1.6f, bz + 0.7f, 0xB89A70);
        addBox(m, bx - 0.3f, 1.4f, bz + 0.55f, bx + 0.3f, 2.3f, bz + 1.0f, 0xB89A70);
        addBox(m, bx - 0.32f, 2.3f, bz + 0.6f, bx + 0.32f, 2.6f, bz + 0.95f, 0x6A4A2A);
        float wing[12] = {bx + 0.36f, 1.0f, bz - 0.6f, bx + 0.36f, 1.0f, bz + 0.5f, bx + 0.36f, 2.4f, bz + 0.2f, bx + 0.36f, 2.2f, bz - 0.7f};
        addQuad(m, wing, 0xC8A878);
        float wing2[12] = {bx - 0.36f, 1.0f, bz + 0.5f, bx - 0.36f, 1.0f, bz - 0.6f, bx - 0.36f, 2.2f, bz - 0.7f, bx - 0.36f, 2.4f, bz + 0.2f};
        addQuad(m, wing2, 0xC8A878);
        break;
      }
      if (tileAt(g_area, tx - 1, tz) != 'M' && tileAt(g_area, tx, tz - 1) != 'M') addStatue(x + 1, z + 1);
      break;
    case 'Z': // a sarcophagus
      addBox2(m, x + 0.05f, 0, z + 0.1f, x + 0.95f, 0.7f, z + 0.9f, 0xC8A870, 0xD8B880);
      addBox(m, x + 0.3f, 0.7f, z + 0.25f, x + 0.7f, 0.82f, z + 0.75f, 0x3A5A8A);
      break;
    case 'O': // a painted column
      addCyl(m, cx, 0, cz, 0.32f, 0.2f, 0.32f, 0xB89868, 6);
      addCyl(m, cx, 0.2f, cz, 0.24f, 2.4f, 0.22f, indoor ? 0xC8B080 : 0xD8C8A0, 7);
      addBox(m, cx - 0.33f, 2.4f, cz - 0.33f, cx + 0.33f, 2.6f, cz + 0.33f, 0x3A5A8A);
      break;
    case 'P': addBoat(cx, cz, seed); break;
    case 'R': addBlob(m, cx, -0.2f, cz, 0.9f, 1.3f, 0.8f, 0x4A4C48, 6, 4); break;
    case '^': {
      if (indoor) {
        // only the back wall and the sides: the front is open to the camera
        if (tz == A().d - 1) break;
        bool side = tz > 0 && (tx == 0 || tx == A().w - 1);
        float h = indoor == 2 ? (side ? 1.0f : 2.0f) + (seed & 3) * 0.3f : side ? 1.1f : 2.7f;
        uint32_t wc = indoor == 2 ? 0x3A3C38 : (tz == 0 ? 0x8A7A62 : 0x7A6A52);
        addBox2(m, x, 0, z, x + 1, h, z + 1, wc, shade(wc, 0.8f));
        if (indoor == 1 && tz == 0) addBox(m, x, 0, z + 0.98f, x + 1, 0.9f, z + 1.02f, 0x4A3220); // wainscot
      } else {
        float h = 0.9f + (seed & 7) * 0.12f;
        bool sea = tileAt(g_area, tx, tz - 1) == '~' || tileAt(g_area, tx, tz + 1) == '~';
        addBox2(m, x - 0.02f, sea ? -0.4f : 0, z - 0.02f, x + 1.02f, h, z + 1.02f, 0x5A5850, 0x5E7046);
      }
      break;
    }
    case '=': {
      if (tileAt(g_area, tx - 1, tz) == '~' || tileAt(g_area, tx + 1, tz) == '~')
        addCyl(m, (tileAt(g_area, tx - 1, tz) == '~' ? x + 0.08f : x + 0.92f), -0.4f, cz, 0.08f, 0.25f, 0.08f, 0x3A2E24, 5);
      break;
    }
    case 'v': {
      for (int k = 0; k < 4; k++) {
        float gx = x + 0.2f + ((seed >> (k * 4)) & 7) * 0.09f, gz = z + 0.2f + ((seed >> (k * 4 + 2)) & 7) * 0.09f;
        addCone(m, gx, 0, gz, 0.17f, 0.45f + ((seed >> k) & 3) * 0.06f, k & 1 ? 0x6A8A42 : 0x587A38, 3);
      }
      break;
    }
    case ':':
      if (!indoor && (seed & 7) == 0) addBlob(m, cx, 0.02f, cz, 0.12f, 0.06f, 0.1f, 0x9A9384, 5, 3);
      break;
    case 'E':
      if (indoor) addBox(m, x + 0.15f, 0, z + 0.1f, x + 0.85f, 0.03f, z + 0.6f, 0x7A2430);
      break;
    default:
      if (indoor) addFurniture(c, x, z, seed);
      break;
  }
}

static void buildGround(int x0, int z0, int x1, int z1) {
  MeshBuf &m = s_static;
  bool indoor = A().indoor != 0;
  int base = m.nv, nx = x1 - x0 + 1;
  for (int vz = z0; vz <= z1; vz++)
    for (int vx = x0; vx <= x1; vx++) {
      float r = 0, g = 0, b = 0;
      for (int dz = -1; dz <= 0; dz++)
        for (int dx = -1; dx <= 0; dx++) {
          uint32_t c = groundCol(tileAt(g_area, clampi(vx + dx, 0, A().w - 1), clampi(vz + dz, 0, A().d - 1)), A().indoor == 1);
          r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
        }
      float n = 0.88f + hashf(vx, vz, g_area * 13) * 0.24f;
      uint32_t c = ((uint32_t)clampi((int)(r / 4 * n), 0, 255) << 16) | ((uint32_t)clampi((int)(g / 4 * n), 0, 255) << 8) |
                   (uint32_t)clampi((int)(b / 4 * n), 0, 255);
      float y = indoor ? 0 : groundY(vx, vz);
      addVert(m, (float)vx, y, (float)vz, c, g_light.amb + g_light.dif * 0.75f);
    }
  for (int tz = z0; tz < z1; tz++)
    for (int tx = x0; tx < x1; tx++) {
      int a = base + (tz - z0) * nx + (tx - x0), b = a + 1, c = a + nx + 1, d = a + nx;
      // counter-clockwise from above: a(x0,z0) d(x0,z1) c(x1,z1) b(x1,z0)
      addTri(m, a, d, c);
      addTri(m, a, c, b);
    }
}

static void buildArea() {
  meshReset(s_static);
  s_nGlow = 0;
  s_nLights = 0;
  s_nChunks = 0;
  const AreaDef &a = A();
  bool outdoor = !a.indoor;
  // light: dusk outdoors, a warm room indoors, cold blue in the cave
  if (outdoor) {
    g_light.sx = -0.55f; g_light.sy = 0.55f; g_light.sz = 0.62f;
    g_light.amb = 0.48f; g_light.dif = 0.55f; g_light.tint = g_area == A_FALLS ? 0xE6E8F0 : 0xFFE2C4;
    switch (a.biome) {
      case 1: g_light.amb = 0.56f; g_light.dif = 0.62f; g_light.tint = 0xFFE8C4; g_light.sx = -0.3f; g_light.sy = 0.85f; g_light.sz = 0.4f; break;
      case 2: g_light.amb = 0.46f; g_light.dif = 0.55f; g_light.tint = 0xE0F0C8; break;
      case 3: g_light.amb = 0.58f; g_light.dif = 0.5f; g_light.tint = 0xDCE8FF; break;
      case 4: g_light.amb = 0.44f; g_light.dif = 0.56f; g_light.tint = 0xFFC8A8; break;
      default: break;
    }
  } else if (a.indoor == 2) {
    g_light.sx = 0.2f; g_light.sy = 0.9f; g_light.sz = 0.4f;
    g_light.amb = 0.32f; g_light.dif = 0.45f; g_light.tint = 0xA8C0D8;
  } else {
    g_light.sx = 0.3f; g_light.sy = 0.85f; g_light.sz = 0.45f;
    g_light.amb = 0.52f; g_light.dif = 0.5f; g_light.tint = 0xFFE4C8;
  }
  // far scenery first (always drawn): open sea and the Rock offshore
  s_always0v = s_static.nv; s_always0f = s_static.nf;
  if (outdoor) {
    // land and sea beyond the edges of the map
    bool seaNorth = strchr(a.rows[0], '~') != nullptr;
    // the ground plane runs on past the map: sea to the north, land elsewhere
    float lit = g_light.amb + g_light.dif * 0.75f;
    auto litc = [&](uint32_t c) {
      int r = clampi((int)(((c >> 16) & 255) * lit), 0, 255), g = clampi((int)(((c >> 8) & 255) * lit), 0, 255),
          b = clampi((int)((c & 255) * lit), 0, 255);
      return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    };
    static const uint32_t LAND[5] = {0x3E5A34, 0xC8A870, 0x2E4A28, 0xD8E0E8, 0x8A7A68};
    g_ground.outNorth = litc(seaNorth ? 0x1E3F52 : LAND[a.biome]);
    g_ground.outRest = litc(LAND[a.biome]);
    g_ground.northSky = g_area == A_FALLS;
    g_ground.restSky = false;
    if (g_area == A_TOWN || g_area == A_CLIFFS) {
      float rx = g_area == A_TOWN ? 12 : 24, rz = -16;
      addBlob(s_static, rx, 0.0f, rz, 2.6f, 3.4f, 2.0f, 0x2E3238, 7, 5);
      addBlob(s_static, rx + 2.2f, -0.3f, rz + 0.8f, 1.4f, 1.6f, 1.2f, 0x34383E, 6, 4);
      addBlob(s_static, rx - 2.0f, -0.2f, rz + 0.5f, 1.2f, 2.2f, 1.1f, 0x2A2E34, 6, 4);
    }
    if (g_area == A_FALLS) {
      // the water falling away at the top of the map, and the far valley
      float fall[12] = {4.0f, -8.0f, 0.2f, 9.0f, -8.0f, 0.2f, 9.0f, -0.3f, 0.6f, 4.0f, -0.3f, 0.6f};
      addQuad(s_static, fall, 0xC8D8E0, 0.4f);
      float valley[12] = {-20, -9, -60, -20, -9, 0, 34, -9, 0, 34, -9, -60};
      addQuad(s_static, valley, 0x4A5A48);
    }
  }
  if (!outdoor) { g_ground.northSky = true; g_ground.restSky = true; }
  s_always1v = s_static.nv; s_always1f = s_static.nf;
  // the map, chunk by chunk
  for (int cz = 0; cz < a.d; cz += CH)
    for (int cx = 0; cx < a.w; cx += CH) {
      Chunk &c = s_chunk[s_nChunks++];
      c.x0 = cx; c.z0 = cz; c.x1 = cx + CH < a.w ? cx + CH : a.w; c.z1 = cz + CH < a.d ? cz + CH : a.d;
      c.v0 = s_static.nv; c.f0 = s_static.nf;
      for (int tz = c.z0; tz < c.z1; tz++)
        for (int tx = c.x0; tx < c.x1; tx++) addProps(a.rows[tz][tx], tx, tz, a.indoor);
      for (int i = 0; i < a.nb; i++)
        if (a.b[i].x >= c.x0 && a.b[i].x < c.x1 && a.b[i].z >= c.z0 && a.b[i].z < c.z1) addBuilding(a.b[i], i);
      c.v1 = s_static.nv; c.f1 = s_static.nf;
    }
  bakeLights(s_static, 0, s_static.nv, s_lights, s_nLights);
  buildGroundTex(g_area, s_lights, s_nLights);
  markAway(s_static, a.indoor ? 0.98f : 0.84f, 0);
  setSky(a.sky0, a.sky1);
  g_fog.col = a.fog;
  g_fog.start = a.fogStart;
  g_fog.end = a.fogEnd;
  g_fog.max = a.indoor ? 0.9f : 0.82f;
}

// --- loading an area ---------------------------------------------------------------------------

static void loadActors() {
  g_nActors = 0;
  for (int i = 0; i < npcCount() && g_nActors < MAX_ACTORS; i++) {
    const NpcDef &n = npcAt(i);
    if (n.area != g_area) continue;
    Actor &a = g_actor[g_nActors++];
    memset(&a, 0, sizeof(a));
    a.look = n.look; a.area = n.area;
    a.x = a.tx = n.x; a.z = a.tz = n.z;
    a.face = n.face; a.script = n.script; a.showIf = n.showIf;
    a.species = n.species; a.trainer = n.trainer;
  }
}



void worldLoad(int area, int x, int z, int face) {
  // Only a door remembers where to come back out. A story warp indoors (the
  // prologue's jump from the Falls to Mrs. Quill's, say) must not, or the way
  // out would lead back to wherever the story was before.
  if (!s_retFresh) s_retArea = -1;
  s_retFresh = false;
  if (g_area < A_COUNT && !AREAS[g_area].indoor && AREAS[area].indoor) { s_outArea = g_area; s_outX = (int)g_player.x; s_outZ = (int)g_player.z; }
  g_area = (uint8_t)area;
  chatterPause(true);
  buildArea();
  chatterPause(false);
  loadActors();
  g_player.x = g_player.tx = (float)x;
  g_player.z = g_player.tz = (float)z;
  g_player.face = (uint8_t)face;
  g_player.moving = false;
  g_player.look = L_WREN;
  s_follow = g_player;
  s_followOn = g_nParty > 0 && !A().indoor;
  g_cam.tx = x + 0.5f;
  g_cam.tz = z + 0.5f;
  if (A().indoor) { s_yawGoal = clampf(s_yawGoal, -0.55f, 0.55f); s_yaw = clampf(s_yaw, -0.55f, 0.55f); }
  s_banner = A().name;
  s_bannerT = 0;
  s_spotter = -1;
  setVar("area", area);
}

// --- walking ---------------------------------------------------------------------------------------

static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {-1, 0, 1, 0};

static void warp(int area, int x, int z, int face) {
  s_warpArea = area; s_warpX = x; s_warpZ = z; s_warpFace = face;
  s_warpT = 0;
  sfx(RunAudio::Tock);
}

static bool tryExit(int x, int z) {
  const AreaDef &a = A();
  for (int i = 0; i < a.nex; i++)
    if (a.ex[i].x == x && a.ex[i].z == z) {
      if (a.ex[i].dest == A_CAVE && !flag("cave_known")) { Script::start("cave_locked"); return true; }
      if (a.ex[i].dest == A_SANCTUM && !flag("curators_beaten")) { Script::start("sanctum_locked"); return true; }
      warp(a.ex[i].dest, a.ex[i].tx, a.ex[i].tz, a.ex[i].face);
      return true;
    }
  if (a.indoor && tileAt(g_area, x, z) == 'E' && s_retArea >= 0) {
    warp(s_retArea, s_retX, s_retZ, F_S);
    return true;
  }
  if (a.indoor && tileAt(g_area, x, z) == 'E') {
    // out through the door we came in by
    for (int ar = 0; ar < A_COUNT; ar++)
      for (int i = 0; i < AREAS[ar].nb; i++)
        if (AREAS[ar].b[i].dest == g_area) {
          const Building &b = AREAS[ar].b[i];
          warp(ar, b.doorX, b.z + b.d, F_S);
          return true;
        }
    // no door leads here (a cave reached by the story): back to where Wren last stood outside
    if (s_outArea >= 0) { warp(s_outArea, s_outX, s_outZ, F_S); return true; }
  }
  return false;
}

static void enterDoor(int x, int z) {
  int bi = buildingAt(x, z);
  if (bi < 0) return;
  const Building &b = A().b[bi];
  if (b.doorX != x || z != b.z + b.d - 1) return;
  if (b.dest >= A_COUNT) { Script::start("door_locked"); return; }
  // arrive just inside: one tile north of the interior's exit mat
  const AreaDef &in = AREAS[b.dest];
  s_retArea = g_area; s_retX = x; s_retZ = z + 1; s_retFresh = true;
  for (int iz = 0; iz < in.d; iz++)
    for (int ix = 0; ix < in.w; ix++)
      if (in.rows[iz][ix] == 'E') { warp(b.dest, ix, iz - 1, F_N); return; }
}

static bool step(int screenDir) {
  int dir = (screenDir - quarter()) & 3;
  Actor &p = g_player;
  p.face = (uint8_t)dir;
  int nx = (int)p.x + DX[dir], nz = (int)p.z + DZ[dir];
  // doors: walking up into a building's door
  if (dir == F_N && buildingAt(nx, nz) >= 0) { enterDoor(nx, nz); return false; }
  // leaving an interior by walking down off the mat
  if (A().indoor && tileAt(g_area, (int)p.x, (int)p.z) == 'E' && dir == F_S) { tryExit((int)p.x, (int)p.z); return false; }
  if (!walkable(nx, nz)) return false;
  if (s_followOn) { s_follow.tx = p.x; s_follow.tz = p.z; s_follow.moving = true; }
  p.tx = (float)nx; p.tz = (float)nz;
  p.moving = true;
  return true;
}

static void arrive() {
  int x = (int)g_player.x, z = (int)g_player.z;
  if (tryExit(x, z)) return;
  char c = tileAt(g_area, x, z);
  if (c == 'v' && A().encounter && g_nParty > 0 && (rnd() % 100) < 11) {
    int lvl;
    int sp = wildFor(A().encounter, lvl);
    if (sp >= 0) battleWild(sp, lvl);
  }
}

static void moveActor(Actor &a, float dt, float speed) {
  if (!a.moving) return;
  float dx = a.tx - a.x, dz = a.tz - a.z, d = sqrtf(dx * dx + dz * dz), s = speed * dt;
  a.walkT += dt * speed * 1.7f;
  if (d <= s) { a.x = a.tx; a.z = a.tz; a.moving = false; }
  else { a.x += dx / d * s; a.z += dz / d * s; }
}

Actor *actorNamed(const char *script) {
  for (int i = 0; i < g_nActors; i++)
    if (g_actor[i].script && !strcmp(g_actor[i].script, script)) return &g_actor[i];
  // a short name: "vane" finds "vane_falls"
  size_t n = strlen(script);
  for (int i = 0; i < g_nActors; i++)
    if (g_actor[i].script && !strncmp(g_actor[i].script, script, n) && g_actor[i].script[n] == '_') return &g_actor[i];
  return nullptr;
}

bool worldBusy() {
  if (s_warpArea >= 0 || g_player.moving) return true;
  for (int i = 0; i < g_nActors; i++)
    if (g_actor[i].moving) return true;
  return false;
}

static void interact() {
  Actor &p = g_player;
  int fx = (int)p.x + DX[p.face], fz = (int)p.z + DZ[p.face];
  Actor *a = actorAt(fx, fz);
  // talk across a counter
  if (!a && strchr("CK", tileAt(g_area, fx, fz))) a = actorAt(fx + DX[p.face], fz + DZ[p.face]);
  if (a && a->script) {
    a->face = (uint8_t)((p.face + 2) & 3);
    sfx(RunAudio::Tick);
    Script::start(a->script);
    return;
  }
  for (int i = 0; i < spotCount(); i++) {
    const SpotDef &s = spotAt(i);
    if (s.area == g_area && s.x == fx && s.z == fz && showIfOk(s.showIf)) {
      sfx(RunAudio::Tick);
      Script::start(s.script);
      return;
    }
  }
  // a building's sign, read from its door
  int bi = buildingAt(fx, fz);
  if (bi >= 0 && A().b[bi].sign) { UI::toast(A().b[bi].sign); return; }
  UI::open(UI::P_PAUSE);
}

void worldKey(Btn b) {
  if (s_warpArea >= 0 || s_spotter >= 0) return;
  if (b == B_OK) {
    if (!g_player.moving) interact();
    return;
  }
  int dir = b == B_UP ? F_N : b == B_RIGHT ? F_E : b == B_DOWN ? F_S : b == B_LEFT ? F_W : -1;
  if (dir < 0 || g_player.moving) return;
  step(dir);
}

static void checkTrainers() {
  if (s_spotter >= 0 || Script::active()) return;
  for (int i = 0; i < g_nActors; i++) {
    Actor &a = g_actor[i];
    if (!a.trainer || !showIfOk(a.showIf) || a.moving) continue;
    char key[40];
    snprintf(key, sizeof(key), "beat_%s", a.script);
    if (flag(key)) continue;
    for (int d = 1; d <= 4; d++) {
      int x = (int)a.x + DX[a.face] * d, z = (int)a.z + DZ[a.face] * d;
      if ((int)g_player.x == x && (int)g_player.z == z) {
        s_spotter = i;
        s_spotT = 0;
        sfx(RunAudio::Hop);
        return;
      }
      if (blockedChar(tileAt(g_area, x, z)) || buildingAt(x, z) >= 0) break;
    }
  }
}

void worldUpdate(float dt) {
  s_bannerT += dt;
  // a warp: fade out, swap the place, fade back in
  if (s_warpArea >= 0) {
    s_warpT += dt;
    g_fade = clampf(s_warpT / 0.25f, 0, 1);
    if (s_warpT >= 0.25f) {
      worldLoad(s_warpArea, s_warpX, s_warpZ, s_warpFace);
      s_warpArea = -1;
      g_fade = 1;
    }
    return;
  }
  bool frozen = (Script::active() && Script::blocksWorld()) || UI::active() || g_mode != Mode::World;
  if (g_mode == Mode::Title) {
    // drift the camera along the quay
    float t = g_time * 0.15f;
    g_cam.tx = 18 + sinf(t) * 7;
    g_cam.tz = 11 + cosf(t * 0.7f) * 2;
    for (int i = 0; i < g_nActors; i++) moveActor(g_actor[i], dt, 2.2f);
    return;
  }
  // the player
  Actor &p = g_player;
  if (p.moving) {
    moveActor(p, dt, 4.6f);
    if (!p.moving) {
      arrive();
      // keep walking while a direction is held
      if (s_warpArea < 0 && g_mode == Mode::World && !frozen) {
        int dir = held(B_UP) ? F_N : held(B_DOWN) ? F_S : held(B_LEFT) ? F_W : held(B_RIGHT) ? F_E : -1;
        if (dir >= 0) step(dir);
      }
    }
  } else if (!frozen && s_spotter < 0) {
    int dir = held(B_UP) ? F_N : held(B_DOWN) ? F_S : held(B_LEFT) ? F_W : held(B_RIGHT) ? F_E : -1;
    if (dir >= 0) step(dir);
  }
  if (!s_followOn && g_nParty > 0 && !A().indoor) {
    // a partner has just joined: it appears at your heels
    s_followOn = true;
    s_follow = p;
    s_follow.moving = false;
  }
  if (s_followOn) {
    moveActor(s_follow, dt, 4.6f);
    if (!s_follow.moving) {
      // face where the player is
      float dx = p.x - s_follow.x, dz = p.z - s_follow.z;
      if (fabsf(dx) + fabsf(dz) > 0.5f) s_follow.face = fabsf(dx) > fabsf(dz) ? (dx > 0 ? F_E : F_W) : (dz > 0 ? F_S : F_N);
    }
  }
  for (int i = 0; i < g_nActors; i++) moveActor(g_actor[i], dt, 3.6f);
  // trainers who've seen you walk over
  if (!frozen && !p.moving) checkTrainers();
  if (s_spotter >= 0) {
    s_spotT += dt;
    Actor &a = g_actor[s_spotter];
    if (s_spotT > 0.7f && !a.moving) {
      int ax = (int)a.x + DX[a.face], az = (int)a.z + DZ[a.face];
      if (ax == (int)p.x && az == (int)p.z) {
        p.face = (uint8_t)((a.face + 2) & 3);
        const char *s = a.script;
        s_spotter = -1;
        Script::start(s);
      } else {
        a.tx = (float)ax; a.tz = (float)az; a.moving = true;
      }
    }
  }
  // the camera turns and leans in smoothly
  float ke = 1 - expf(-dt * 8);
  s_yaw += (s_yawGoal - s_yaw) * ke;
  s_zoom += (s_zoomGoal - s_zoom) * ke;
  // camera follows, a little ahead
  float cx = p.x + 0.5f, cz = p.z + 0.5f;
  float k = 1 - expf(-dt * 6);
  g_cam.tx += (cx - g_cam.tx) * k;
  g_cam.tz += (cz - g_cam.tz) * k;
}

// --- drawing ---------------------------------------------------------------------------------------

static float facingAngle(int f) { return f == F_N ? PI_F : f == F_E ? -PI_F * 0.5f : f == F_S ? 0 : PI_F * 0.5f; }

volatile uint32_t g_prof[4];

void worldPrepare() {
  uint32_t pt0 = micros();
  const AreaDef &a = A();
  bool in = a.indoor != 0;
  // close in, the camera drops lower and looks along the street
  g_cam.dist = lerpf(in ? 9.0f : 12.5f, in ? 4.2f : 4.6f, s_zoom);
  g_cam.pitch = lerpf(in ? 0.98f : 0.84f, in ? 0.55f : 0.42f, s_zoom);
  g_cam.yaw = s_yaw;
  g_cam.focal = 400;
  g_cam.ty = lerpf(0.5f, 1.0f, s_zoom);
  if (g_mode == Mode::Title) { g_cam.dist = 15; g_cam.pitch = 0.62f; g_cam.yaw = 0; g_cam.ty = 0.5f; }
  if (g_shake > 0) { g_cam.tx += sinf(g_time * 60) * g_shake * 0.08f; }
  // indoors, keep the room in view rather than chasing the player off its edges
  float camX = g_cam.tx, camZ = g_cam.tz;
  if (in) {
    g_cam.tx = a.w * 0.5f + (g_cam.tx - a.w * 0.5f) * 0.35f;
    g_cam.tz = clampf(g_cam.tz, 2.5f, a.d - 1.5f);
  }
  float fwdX = -sinf(g_cam.yaw), fwdZ = -cosf(g_cam.yaw);
  float lookX = camX + fwdX * 6, lookZ = camZ + fwdZ * 6, reach = 17 - s_zoom * 6;
  meshReset(s_dyn);
  // people
  for (int i = 0; i < g_nActors; i++) {
    const Actor &ac = g_actor[i];
    if (!showIfOk(ac.showIf)) continue;
    if (fabsf(ac.x + 0.5f - lookX) > reach - 3 || fabsf(ac.z + 0.5f - lookZ) > reach - 3) continue;
    if (ac.look >= L_COUNT) continue; // myth beings are drawn by their scenes
    if (ac.look == L_RAVEN) {
      float x = ac.x + 0.5f, z = ac.z + 0.5f, hop = fabsf(sinf(g_time * 3)) * 0.1f;
      if (!flag("muninn_free")) { addCyl(s_dyn, x, 0, z, 0.55f, 1.5f, 0.55f, 0x3A3A44, 8); }
      addBlob(s_dyn, x, 0.5f + hop, z, 0.22f, 0.24f, 0.32f, 0x141418, 6, 4);
      addBlob(s_dyn, x, 0.78f + hop, z + 0.2f, 0.14f, 0.14f, 0.14f, 0x18181C, 6, 3);
      addBlob(s_dyn, x, 0.76f + hop, z + 0.36f, 0.05f, 0.04f, 0.1f, 0x3A3A3A, 4, 2);
      continue;
    }
    buildFigure(s_dyn, ac.look, ac.x + 0.5f, ac.z + 0.5f, facingAngle(ac.face), ac.walkT, ac.moving);
  }
  if (g_mode != Mode::Title) {
    buildFigure(s_dyn, L_WREN, g_player.x + 0.5f, g_player.z + 0.5f, facingAngle(g_player.face), g_player.walkT, g_player.moving);
    if (s_followOn && g_nParty > 0)
      buildMon3D(s_dyn, g_party[0].sp, s_follow.x + 0.5f, 0, s_follow.z + 0.5f, facingAngle(s_follow.face), g_time,
                 0.55f, g_party[0].seed);
  }
  // myth beings that stand in their places
  if (g_area == A_CAVE) {
    // Old One-Eye, sitting by his pool
    float x = 6.5f, z = 3.2f;
    addBlob(s_dyn, x, 1.4f, z, 1.3f, 1.5f, 1.1f, 0x6A6E64, 8, 5);
    addBlob(s_dyn, x, 3.1f, z + 0.1f, 0.85f, 0.8f, 0.8f, 0x74786C, 8, 5);
    addBlob(s_dyn, x, 3.2f, z + 0.82f, 0.3f, 0.3f, 0.12f, flag("cyclops_healed") ? 0xFFD27A : 0xC89A50, 7, 4, 0.9f);
    addBlob(s_dyn, x - 1.2f, 1.3f, z + 0.5f, 0.35f, 0.9f, 0.35f, 0x6A6E64, 6, 4);
    addBlob(s_dyn, x + 1.2f, 1.3f, z + 0.5f, 0.35f, 0.9f, 0.35f, 0x6A6E64, 6, 4);
    addBox(s_dyn, x - 1.0f, 0.6f, z + 0.6f, x + 1.0f, 1.1f, z + 1.1f, 0xC8BBA0); // sailcloth bandage
  }
  if (g_area == A_ROCK) {
    for (int i = 0; i < 3; i++) {
      float x = 4.5f + i * 1.5f, z = 3.0f + (i == 1 ? -0.6f : 0);
      bool sing = flag("sirens_freed");
      addCyl(s_dyn, x, 0, z, 0.32f, 1.3f, 0.2f, 0x8A9A9A, 7);
      addBlob(s_dyn, x, 1.55f, z, 0.22f, 0.26f, 0.22f, 0xD8C8B0, 7, 5);
      addBlob(s_dyn, x, 1.6f, z - 0.12f, 0.3f, 0.32f, 0.2f, 0x3E6A5A, 6, 4);
      float flap = sing ? sinf(g_time * 3 + i) * 0.3f : 0;
      float w1[12] = {x - 0.1f, 0.6f, z - 0.1f, x - 0.9f, 1.2f + flap, z - 0.2f, x - 1.0f, 2.2f + flap, z - 0.3f, x - 0.1f, 1.5f, z - 0.1f};
      addQuad(s_dyn, w1, 0xC8C8C0);
      float w2[12] = {x + 0.1f, 1.5f, z - 0.1f, x + 1.0f, 2.2f + flap, z - 0.3f, x + 0.9f, 1.2f + flap, z - 0.2f, x + 0.1f, 0.6f, z - 0.1f};
      addQuad(s_dyn, w2, 0xB8B8B0);
    }
  }
  // story fragments: a turning page of light
  for (int i = 0; i < spotCount(); i++) {
    const SpotDef &s = spotAt(i);
    if (s.area != g_area || !s.glow || !showIfOk(s.showIf)) continue;
    if (s.glow == 3) { // a signpost
      float x = s.x + 0.5f, z = s.z + 0.5f;
      addBox2(s_dyn, x - 0.08f, 0, z - 0.08f, x + 0.08f, 2.1f, z + 0.08f, 0x5A3E26, 0x6A4A2E);
      addBox2(s_dyn, x - 0.55f, 1.55f, z - 0.06f, x + 0.75f, 1.95f, z + 0.06f, 0xC8A060, 0xD8B070);
      addBox2(s_dyn, x - 0.75f, 1.05f, z - 0.06f, x + 0.5f, 1.4f, z + 0.06f, 0xA87A48, 0xB88A58);
      continue;
    }
    if (s.glow == 4) { // the den: a tall old stone, humming
      float x = s.x + 0.5f, z = s.z + 0.5f;
      bool woke = flag("wh");
      addBox2(s_dyn, x - 0.32f, 0, z - 0.22f, x + 0.32f, 2.1f, z + 0.22f, woke ? 0x6A6A66 : 0x7A7A84, woke ? 0x80807A : 0x9A90B0);
      addBox2(s_dyn, x - 0.12f, 1.1f, z + 0.22f, x + 0.12f, 1.5f, z + 0.25f, woke ? 0x4A4A48 : 0xC8A8FF, woke ? 0x4A4A48 : 0xC8A8FF);
      continue;
    }
    if (s.glow == 2) { // a cache: a turning shard of light
      float x = s.x + 0.5f, z = s.z + 0.5f, y = 0.55f + sinf(g_time * 2.4f + i) * 0.08f, r = g_time * 2.0f;
      float c = cosf(r) * 0.16f, sn = sinf(r) * 0.16f;
      float d1[12] = {x - c, y, z - sn, x, y - 0.25f, z, x + c, y, z + sn, x, y + 0.25f, z};
      addQuad(s_dyn, d1, 0x9FE8FF, 1.0f);
      float d2[12] = {x + c, y, z + sn, x, y - 0.25f, z, x - c, y, z - sn, x, y + 0.25f, z};
      addQuad(s_dyn, d2, 0x7FC8F0, 1.0f);
      continue;
    }
    float x = s.x + 0.5f, z = s.z + 0.5f, y = 0.6f + sinf(g_time * 2 + i) * 0.1f, r = g_time * 1.5f;
    float c = cosf(r) * 0.22f, sn = sinf(r) * 0.22f;
    float pg[12] = {x - c, y - 0.2f, z - sn, x + c, y - 0.2f, z + sn, x + c, y + 0.2f, z + sn, x - c, y + 0.2f, z - sn};
    addQuad(s_dyn, pg, 0xFFF0C0, 1.0f);
    float pb[12] = {x + c, y - 0.2f, z + sn, x - c, y - 0.2f, z - sn, x - c, y + 0.2f, z - sn, x + c, y + 0.2f, z + sn};
    addQuad(s_dyn, pb, 0xFFF0C0, 1.0f);
  }
  uint32_t pt1 = micros();
  g_prof[0] += pt1 - pt0;
  sceneBegin();
  // faces known to point away were worked out for the usual view only
  uint8_t *away = s_static.away;
  if (fabsf(g_cam.yaw) > 0.03f || s_zoom > 0.03f || g_mode == Mode::Title) s_static.away = nullptr;
  sceneAdd(s_static, s_always0f, s_always1f, s_always0v, s_always1v);
  // scenery between the camera and Wren is cut away
  g_nearCut = g_mode == Mode::Title ? 0 : g_cam.dist * 0.62f;
  for (int i = 0; i < s_nChunks; i++) {
    const Chunk &c = s_chunk[i];
    if (c.x1 < lookX - reach || c.x0 > lookX + reach || c.z1 < lookZ - reach || c.z0 > lookZ + reach) continue;
    sceneAdd(s_static, c.f0, c.f1, c.v0, c.v1);
  }
  s_static.away = away;
  g_nearCut = 0;
  uint32_t pt2 = micros();
  g_prof[1] += pt2 - pt1;
  sceneAdd(s_dyn);
  sceneEnd();
  g_prof[2] += micros() - pt2;
  g_cam.tx = camX;
  g_cam.tz = camZ;
}

void worldOverlay() {
  // glows around lamps and lit things
  for (int i = 0; i < s_nGlow; i++) {
    const Glow &gl = s_glow[i];
    float sx, sy, d;
    if (!g_cam.project(gl.x, gl.y, gl.z, sx, sy, d)) continue;
    float r = gl.r * g_cam.focal / d * 0.9f;
    if (sx + r < 0 || sx - r > W || sy + r < Raster::top() - 2 || sy - r > Raster::top() + SH + 2) continue;
    glow(sx, sy, r, gl.c, 150);
  }
  // fragments glint
  for (int i = 0; i < spotCount(); i++) {
    const SpotDef &s = spotAt(i);
    if (s.area != g_area || !s.glow || s.glow == 3 || !showIfOk(s.showIf)) continue;
    if (s.glow == 4 && flag("wh")) continue;
    float sx, sy, d;
    if (!g_cam.project(s.x + 0.5f, s.glow == 4 ? 1.3f : 0.6f, s.z + 0.5f, sx, sy, d)) continue;
    float r = (s.glow == 4 ? 40 : 28) * 12 / d;
    glow(sx, sy, r, c565(s.glow == 2 ? 0x9FE8FF : s.glow == 4 ? 0xC8A8FF : 0xFFE9A0), 200);
  }
  if (g_mode == Mode::Title) return;
  // the "!" over a trainer who has seen you
  if (s_spotter >= 0) {
    const Actor &a = g_actor[s_spotter];
    float sx, sy, d;
    if (g_cam.project(a.x + 0.5f, 2.6f, a.z + 0.5f, sx, sy, d)) {
      Raster::roundRect(sx - 9, sy - 22, 18, 22, 5, Col::CREAM);
      textC(F_BOLD, (int)sx, (int)sy - 21, "!", Col::OXRED);
    }
  }
  // the place's name, on arrival
  if (s_banner && s_bannerT < 2.6f) {
    float a = clampf(s_bannerT * 3, 0, 1) * clampf((2.6f - s_bannerT) * 2, 0, 1);
    int w = textW(F_BOLD, s_banner) + 40;
    fadeRect(W / 2 - w / 2, 8, w, 36, c565(Pal::INK), (uint8_t)(170 * a));
    if (a > 0.3f) textC(F_BOLD, W / 2, 12, s_banner, mix565(c565(Pal::INK), Col::CREAM, (int)(256 * a)));
  }
}

} // namespace Gaslight
