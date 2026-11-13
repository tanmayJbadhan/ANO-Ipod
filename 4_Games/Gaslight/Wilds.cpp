// Gaslight & Griffins - the Wilds.
// Behind every town a signpost points into open country that is never the same
// twice. The land is written by a small trained network (GLTiles.h, made by
// tools/worldmodel/train_tiles.py) one tile at a time; this file then cuts a
// trail through it, rings a den of standing stones at the far end, and hides
// a few caches along the way.
#include "Game.h"
#include "World.h"
#include "GLTiles.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Gaslight {

static const int WW = 28, WD = 30; // the ground texture holds up to 40 x 30
static char s_rows[WD][WW + 1];
static const char *s_rowPtr[WD];
static Exit s_exit[1];

// signposts in each town (the first five) and what is in the Wilds right now
SpotDef g_wildSpots[MAX_WILD_SPOTS];
int g_nWildSpots = 0;

struct Trail { uint8_t area; int8_t x, z; uint8_t biome, table; const char *name; };
static const Trail TRAILS[] = {
    {A_TOWN, 16, 27, 0, 1, "The Salt Downs"},
    {A_SAND, 15, 20, 1, 2, "The Dune Sea"},
    {A_GREEN, 16, 20, 2, 3, "The Wild Wood"},
    {A_SKJOLD, 15, 20, 3, 4, "The Ice Fields"},
    {A_UR, 25, 22, 4, 5, "The Reed Marsh"},
};
static const int N_TRAILS = sizeof(TRAILS) / sizeof(TRAILS[0]);
static const char *const CACHE_SCRIPTS[] = {"wcache0", "wcache1", "wcache2", "wcache3", "wcache4"};
static const char *const CACHE_FLAGS[] = {"!wc0", "!wc1", "!wc2", "!wc3", "!wc4"};

// a small private random stream, so a seed always gives the same land
static uint32_t s_rng;
static uint32_t wr() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static float wf() { return (wr() & 0xFFFFFF) / 16777216.0f; }

// --- the network -------------------------------------------------------------------------------
static void writeLand(int biome) {
  using namespace LandNet;
  float h[NH], lg[NV];
  uint8_t tok[WD][WW];
  for (int z = 0; z < WD; z++)
    for (int x = 0; x < WW; x++) {
      // biome, edge and the bias
      int md = x; if (WW - 1 - x < md) md = WW - 1 - x; if (z < md) md = z; if (WD - 1 - z < md) md = WD - 1 - z;
      float pf[NPOS] = {(float)(x == 0), (float)(x == WW - 1), (float)(z == 0), (float)(z == WD - 1), md < 4 ? md / 4.0f : 1.0f, z / (float)WD};
      for (int j = 0; j < NH; j++) {
        float v = PB[j] + BIO[biome * NH + j];
        for (int k = 0; k < NPOS; k++) v += pf[k] * PW[k * NH + j];
        h[j] = v;
      }
      // what has been written already, nearby
      for (int c = 0; c < NCTX; c++) {
        int xx = x + CTX[c][0], zz = z + CTX[c][1];
        int t = (xx >= 0 && xx < WW && zz >= 0 && zz < WD) ? tok[zz][xx] : NTOK - 1;
        const int8_t *e = EMB + (c * NTOK + t) * NH;
        for (int j = 0; j < NH; j++) h[j] += e[j] * SE;
      }
      float mx = -1e9f;
      for (int i = 0; i < NV; i++) {
        float v = B2[i];
        const int8_t *w = W2 + i * NH;
        float s = 0;
        for (int j = 0; j < NH; j++) if (h[j] > 0) s += w[j] * h[j];
        v += s * S2;
        lg[i] = v / 0.85f; // a little warmer than the training, for variety
        if (lg[i] > mx) mx = lg[i];
      }
      float tot = 0;
      for (int i = 0; i < NV; i++) { lg[i] = expf(lg[i] - mx); tot += lg[i]; }
      float r = wf() * tot;
      int pick = NV - 1;
      for (int i = 0; i < NV; i++) { r -= lg[i]; if (r <= 0) { pick = i; break; } }
      tok[z][x] = (uint8_t)pick;
      s_rows[z][x] = VOCAB[pick];
    }
}

// --- shaping it into a place ----------------------------------------------------------------------
static bool solid(char c) { return strchr("~^TRwO", c) != nullptr; }

static void carve(int x, int z, char path) {
  if (x < 1 || x >= WW - 1 || z < 1 || z >= WD - 1) return;
  char &c = s_rows[z][x];
  if (c == '~' || c == '=') c = '=';  // a plank ford over water
  else if (c != 'v' || ((x + z) & 1)) c = path; // a worn track; tall grass half-trampled
}

void wildsGenerate(int trail, uint32_t seed) {
  const Trail &t = TRAILS[trail];
  s_rng = seed * 2654435761u + 0x9E3779B9u;
  if (!s_rng) s_rng = 1;
  for (int z = 0; z < WD; z++) { s_rows[z][WW] = 0; s_rowPtr[z] = s_rows[z]; }
  writeLand(t.biome);

  const char edge = "^^T^w"[t.biome], path = t.biome == 4 ? '.' : ':';
  // bare ground is earth and sand out here, not cobbles (Ur keeps its old paving)
  if (t.biome != 4)
    for (int z = 0; z < WD; z++)
      for (int x = 0; x < WW; x++)
        if (s_rows[z][x] == '.') s_rows[z][x] = ':';
  // hemmed in all round
  for (int z = 0; z < WD; z++)
    for (int x = 0; x < WW; x++)
      if (x == 0 || x == WW - 1 || z == 0 || z == WD - 1) s_rows[z][x] = edge;

  // the way in, from the south
  int ex = WW / 2;
  s_rows[WD - 1][ex] = path;
  // the den, at the far end
  int hx = 5 + (int)(wr() % (WW - 10)), hz = 3;
  // a wandering trail between them, two tiles wide in places
  int x = ex, z = WD - 2;
  while (z > hz + 1 || x != hx) {
    carve(x, z, path);
    if (wr() % 3 == 0) carve(x + 1, z, path);
    float r = wf();
    if (z > hz + 1 && (r < 0.55f || x == hx)) z--;
    else if (x < hx) x += (r < 0.85f || z <= hz + 1) ? 1 : -1;
    else if (x > hx) x -= (r < 0.85f || z <= hz + 1) ? 1 : -1;
    else z--;
    if (x < 2) x = 2;
    if (x > WW - 3) x = WW - 3;
  }
  carve(x, z, path);
  // a clearing for the den, ringed by stones, open to the south
  for (int dz = -2; dz <= 2; dz++)
    for (int dx = -2; dx <= 2; dx++) {
      int cx = hx + dx, cz = hz + dz;
      if (cx < 1 || cx >= WW - 1 || cz < 1 || cz >= WD - 1) continue;
      int d2 = dx * dx + dz * dz;
      s_rows[cz][cx] = (d2 >= 4 && d2 <= 5 && dz <= 0) ? 'R' : (s_rows[cz][cx] == '~' ? '=' : path);
    }
  s_rows[WD - 2][ex] = path;
  s_rows[WD - 3][ex] = path;

  // which tiles can be reached from the way in
  static uint8_t dist[WD][WW];
  memset(dist, 255, sizeof(dist));
  static uint16_t q[WW * WD];
  int qh = 0, qt = 0;
  dist[WD - 2][ex] = 0; q[qt++] = (uint16_t)((WD - 2) * WW + ex);
  while (qh < qt) {
    int p = q[qh++], px = p % WW, pz = p / WW;
    static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; k++) {
      int nx = px + DX[k], nz = pz + DZ[k];
      if (nx < 1 || nx >= WW - 1 || nz < 1 || nz >= WD - 1 || dist[nz][nx] != 255) continue;
      if (solid(s_rows[nz][nx])) continue;
      dist[nz][nx] = (uint8_t)(dist[pz][px] + 1 > 254 ? 254 : dist[pz][px] + 1);
      q[qt++] = (uint16_t)(nz * WW + nx);
    }
  }
  // tall grass for creatures to hide in, if the land came out bare
  int grass = 0;
  for (int z2 = 1; z2 < WD - 1; z2++)
    for (int x2 = 1; x2 < WW - 1; x2++) grass += s_rows[z2][x2] == 'v' && dist[z2][x2] != 255;
  for (int tries = 0; grass < 40 && tries < 200; tries++) {
    int cx = 2 + (int)(wr() % (WW - 4)), cz = 6 + (int)(wr() % (WD - 12));
    if (dist[cz][cx] == 255) continue;
    for (int dz = -1; dz <= 1; dz++)
      for (int dx = -2; dx <= 2; dx++) {
        char &c = s_rows[cz + dz][cx + dx];
        if (c == ',' || c == ':' || c == '.') { c = 'v'; grass++; }
      }
  }

  // the town's signposts stay; everything else in the Wilds is replaced
  g_nWildSpots = N_TRAILS;
  // the den
  g_wildSpots[g_nWildSpots++] = {A_WILDS, (int8_t)hx, (int8_t)hz, "wild_heart", "", 4};
  // caches: reachable, off the trail, well apart
  int placed = 0;
  int8_t px[5], pz[5];
  for (int tries = 0; placed < 5 && tries < 400; tries++) {
    int cx = 2 + (int)(wr() % (WW - 4)), cz = 2 + (int)(wr() % (WD - 6));
    char c = s_rows[cz][cx];
    if (dist[cz][cx] == 255 || dist[cz][cx] < 6 || c == path || c == '=' || c == 'v') continue;
    if (abs(cx - hx) <= 2 && abs(cz - hz) <= 2) continue;
    int open = 0;
    for (int k = 0; k < 4; k++) {
      static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
      open += !solid(s_rows[cz + DZ[k]][cx + DX[k]]);
    }
    if (open < 3) continue;
    bool near = false;
    for (int i = 0; i < placed; i++) near |= abs(px[i] - cx) + abs(pz[i] - cz) < 6;
    if (near) continue;
    px[placed] = (int8_t)cx; pz[placed] = (int8_t)cz;
    g_wildSpots[g_nWildSpots++] = {A_WILDS, (int8_t)cx, (int8_t)cz, CACHE_SCRIPTS[placed], CACHE_FLAGS[placed], 2};
    placed++;
  }

  // the area itself: the town's sky, the town's creatures
  const AreaDef &town = AREAS[t.area];
  s_exit[0] = {(int8_t)ex, (int8_t)(WD - 1), t.area, t.x, (int8_t)(t.z - 1), F_S};
  AreaDef &a = AREAS[A_WILDS];
  a = town;
  a.name = t.name;
  a.w = WW; a.d = WD;
  a.rows = s_rowPtr;
  a.b = nullptr; a.nb = 0;
  a.ex = s_exit; a.nex = 1;
  a.indoor = 0;
  a.encounter = t.table;
  a.biome = t.biome;
  a.fogStart = 10; a.fogEnd = 30;
#ifdef PC_SIM
  printf("wilds %s seed %u:\n", t.name, (unsigned)seed);
  for (int z = 0; z < WD; z++) printf("  %s\n", s_rows[z]);
#endif
}

int wildsTrailAt(int area) {
  for (int i = 0; i < N_TRAILS; i++) if (TRAILS[i].area == area) return i;
  return -1;
}

void wildsInit() {
  g_nWildSpots = 0;
  for (int i = 0; i < N_TRAILS; i++)
    g_wildSpots[g_nWildSpots++] = {TRAILS[i].area, TRAILS[i].x, TRAILS[i].z, "wilds_sign", "has_partner", 3};
  wildsGenerate(0, 1);
}

// Into the Wilds from the town Wren is standing in: a new seed every time.
void wildsEnter() {
  int tr = wildsTrailAt(g_area);
  if (tr < 0) tr = 0;
  uint32_t seed = (uint32_t)(rnd() & 0x7FFFFFFF) | 1;
  setVar("wild_seed", (int)(seed & 0x7FFF));
  setVar("wild_trail", tr);
  for (int i = 0; i < 5; i++) setVar(CACHE_FLAGS[i] + 1, 0);
  setVar("wh", 0);
  wildsGenerate(tr, seed & 0x7FFF);
  setVar("wilds_visits", getVar("wilds_visits") + 1);
}

// After loading a save made in the Wilds: the same land again.
void wildsRestore() { wildsGenerate(clampi(getVar("wild_trail"), 0, N_TRAILS - 1), (uint32_t)getVar("wild_seed")); }

int wildsStartX() { return WW / 2; }
int wildsStartZ() { return WD - 2; }

// A cache: something useful, chosen by weight.
void wildsLoot() {
  static const uint8_t IT[] = {I_TONIC, I_LANTERN, I_BUN, I_DRAUGHT, I_SILVER, I_SALTS};
  static const uint8_t WT[] = {32, 26, 14, 12, 10, 6};
  int r = (int)(rnd() % 100), i = 0;
  while (i < 5 && r >= WT[i]) { r -= WT[i]; i++; }
  int n = (IT[i] == I_TONIC || IT[i] == I_LANTERN) ? 1 + (int)(rnd() % 2) : 1;
  g_items[IT[i]] = (uint8_t)clampi(g_items[IT[i]] + n, 0, 99);
  char msg[64];
  snprintf(msg, sizeof(msg), "Found %s x%d", ITEM_NAMES[IT[i]], n);
  UI::toast(msg);
  if (rnd() % 3 == 0) { int m = 10 + (int)(rnd() % 30); g_money += m; }
}

// The den's keeper: the strongest kind of the town's creatures, older.
void wildsBoss() {
  int tr = clampi(getVar("wild_trail"), 0, N_TRAILS - 1);
  int bestLvl = 0, bestSp = -1;
  for (int i = 0; i < 6; i++) {
    int lvl, sp = wildFor(TRAILS[tr].table, lvl);
    if (sp >= 0 && lvl >= bestLvl) { bestLvl = lvl; bestSp = sp; }
  }
  if (bestSp < 0) return;
  setVar("wh", 1);
  battleWild(bestSp, bestLvl + 5);
}

} // namespace Gaslight
