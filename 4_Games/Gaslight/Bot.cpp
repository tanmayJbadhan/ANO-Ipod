// Gaslight & Griffins - the playtest bot (PC simulator only).
// It plays from "A new case" to an ending through the same keys a player
// presses: in the world it walks (breadth-first paths) to every person and
// thing it hasn't tried since the story last moved on, then through doors and
// exits to places it hasn't been; in dialogue it takes the option it has
// tried least; on the deduction board, in interrogations and accusations it
// presses toward the answers the case tables hold; in battle it picks the
// strongest move, heals when low and catches what it can.
#ifdef PC_SIM
#include "Game.h"
#include "World.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace Gaslight {
Btn battleBotKey();
uint32_t varsDigest();
int varsUsed();
const char *titleSelText();
namespace UI { Btn botKey(); bool botLinkAvailable(); }
namespace Script { Btn botKey(); int botState(); const char *botSaying(); extern bool g_botWantRest; }

namespace Bot {

static uint32_t s_epoch = 0;          // the story's state, as a number
static int s_sinceProgress = 0;       // actions since it last changed
static int s_actions = 0;
// what has been tried since the story last moved on
static uint32_t s_tried[1024]; static int s_nTried = 0;
static uint8_t s_areaSeen[A_COUNT];   // visit order within this epoch (0 = not yet)
static int s_areaClock = 0;
static uint32_t s_areaLast[A_COUNT];  // global: when each area was last visited
static int s_lastArea = -1;
// a plan: walk a path, then press some keys
static uint8_t s_path[256]; static int s_pathN = 0, s_pathI = 0;
static Btn s_after[4]; static int s_afterN = 0, s_afterI = 0;
static uint32_t s_planKey = 0; static bool s_planTarget = false;
static int s_stuckWalk = 0;
static uint32_t s_wayKey = 0; static int s_wayFrom = -1; // the way last taken, and from where
static int s_battles = 0, s_wins = 0, s_losses = 0;
static bool s_inBattle = false;
static char s_log[64][96]; static int s_nLog = 0;

static void log(const char *fmt, const char *a = "", int b = 0) {
  char line[96];
  snprintf(line, sizeof(line), fmt, a, b);
  printf("[bot %5d] %s\n", s_actions, line);
}

static bool tried(uint32_t k) { for (int i = 0; i < s_nTried; i++) if (s_tried[i] == k) return true; return false; }
static void markTried(uint32_t k) { if (s_nTried < 1024) s_tried[s_nTried++] = k; }
static uint32_t keyOf(int area, int x, int z, const char *script) { return nameHash(script) ^ (area * 7919u) ^ (x * 131u) ^ (z * 31337u); }

static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {-1, 0, 1, 0}; // N E S W
static const Btn DIRKEY[4] = {B_UP, B_RIGHT, B_DOWN, B_LEFT};

static bool skipScript(const char *s) {
  return !strcmp(s, "wilds_sign") || !strncmp(s, "crowd", 5) || !strcmp(s, "door_locked");
}

// --- paths ------------------------------------------------------------------------------------------
static int16_t s_prev[64 * 64];
static bool bfs(int sx, int sz, bool (*goal)(int, int, void *), void *ctx, int &gx, int &gz) {
  const AreaDef &a = AREAS[g_area];
  static uint16_t q[64 * 64];
  for (int i = 0; i < a.w * a.d; i++) s_prev[i] = -2;
  int qh = 0, qt = 0;
  s_prev[sz * a.w + sx] = -1; q[qt++] = (uint16_t)(sz * a.w + sx);
  while (qh < qt) {
    int p = q[qh++], px = p % a.w, pz = p / a.w;
    if (goal(px, pz, ctx)) { gx = px; gz = pz; return true; }
    for (int d = 0; d < 4; d++) {
      int nx = px + DX[d], nz = pz + DZ[d];
      if (nx < 0 || nz < 0 || nx >= a.w || nz >= a.d || s_prev[nz * a.w + nx] != -2) continue;
      if (!walkable(nx, nz)) continue;
      s_prev[nz * a.w + nx] = (int16_t)p;
      q[qt++] = (uint16_t)(nz * a.w + nx);
    }
  }
  return false;
}
static void takePath(int gx, int gz) {
  const AreaDef &a = AREAS[g_area];
  int tmp[256], n = 0;
  int p = gz * a.w + gx;
  while (s_prev[p] >= 0 && n < 256) { tmp[n++] = p; p = s_prev[p]; }
  s_pathN = 0;
  for (int i = n - 1; i >= 0; i--) {
    int from = i == n - 1 ? p : tmp[i + 1], to = tmp[i];
    int dx = to % a.w - from % a.w, dz = to / a.w - from / a.w;
    for (int d = 0; d < 4; d++) if (DX[d] == dx && DZ[d] == dz) s_path[s_pathN++] = (uint8_t)d;
  }
  s_pathI = 0;
}

// --- targets: people and things to try -----------------------------------------------------------------
struct Target { int x, z; bool solid; uint32_t key; const char *script; };
static Target s_tg[96]; static int s_nTg = 0;

static void gatherTargets(bool travel) {
  s_nTg = 0;
  for (int i = 0; i < g_nActors && s_nTg < 96; i++) {
    const Actor &a = g_actor[i];
    if (!a.script || !showIfOk(a.showIf) || skipScript(a.script)) continue;
    bool isTravel = !strcmp(a.script, "travel");
    // the train: only once this town has had a fair go
    if (isTravel && !travel && s_sinceProgress < 1500) continue;
    if (!isTravel && travel) continue;
    uint32_t k = keyOf(g_area, (int)a.x, (int)a.z, a.script);
    if (tried(k)) continue;
    s_tg[s_nTg++] = {(int)a.x, (int)a.z, true, k, a.script};
  }
  if (travel) return;
  for (int i = 0; i < spotCount() && s_nTg < 96; i++) {
    const SpotDef &s = spotAt(i);
    if (s.area != g_area || !showIfOk(s.showIf) || skipScript(s.script)) continue;
    uint32_t k = keyOf(g_area, s.x, s.z, s.script);
    if (tried(k)) continue;
    s_tg[s_nTg++] = {s.x, s.z, !walkable(s.x, s.z), k, s.script};
  }
}

// A goal tile: a place from which one press (or a step then a press) faces a target.
static int s_goalT, s_goalD, s_goalMode;
static bool goalTarget(int x, int z, void *) {
  for (int t = 0; t < s_nTg; t++) {
    const Target &g = s_tg[t];
    for (int d = 0; d < 4; d++) {
      if (g.solid) {
        if (x + DX[d] == g.x && z + DZ[d] == g.z) { s_goalT = t; s_goalD = d; s_goalMode = 0; return true; }
        // across a counter
        char c = tileAt(g_area, x + DX[d], z + DZ[d]);
        if ((c == 'C' || c == 'K') && x + 2 * DX[d] == g.x && z + 2 * DZ[d] == g.z) { s_goalT = t; s_goalD = d; s_goalMode = 0; return true; }
      } else {
        // walkable: step in towards it, so we end facing it
        int ax = x + DX[d], az = z + DZ[d];
        if (ax + DX[d] == g.x && az + DZ[d] == g.z && walkable(ax, az)) { s_goalT = t; s_goalD = d; s_goalMode = 1; return true; }
      }
    }
  }
  return false;
}

// --- ways out: doors, exits, interior mats ------------------------------------------------------------------
struct Way { int x, z; int dest; Btn press; };
static Way s_ways[24]; static int s_nWays = 0;
static void gatherWays() {
  s_nWays = 0;
  const AreaDef &a = AREAS[g_area];
  for (int i = 0; i < a.nb && s_nWays < 24; i++) {
    const Building &b = a.b[i];
    if (b.doorX < 0 || b.dest >= A_COUNT) continue;
    s_ways[s_nWays++] = {b.doorX, b.z + b.d, b.dest, B_UP};
  }
  for (int i = 0; i < a.nex && s_nWays < 24; i++)
    if (a.ex[i].dest != A_WILDS) s_ways[s_nWays++] = {a.ex[i].x, a.ex[i].z, a.ex[i].dest, B_NONE};
  if (a.indoor)
    for (int z = 0; z < a.d; z++)
      for (int x = 0; x < a.w; x++)
        if (a.rows[z][x] == 'E' && s_nWays < 24) s_ways[s_nWays++] = {x, z, -1, B_DOWN};
}
static int s_wayPick;
static uint32_t wayKey(const Way &w) { return 0xABCD1234u ^ (g_area * 7919u) ^ (w.x * 131u) ^ (w.z * 31337u); }
static bool goalWay(int x, int z, void *) {
  const Way &w = s_ways[s_wayPick];
  return x == w.x && z == w.z;
}

// --- deciding ----------------------------------------------------------------------------------------------
static int s_grind = 0;          // wild battles still to fight before trying again
static int s_grindBattles = 0;
static bool goalGrass(int x, int z, void *) { return tileAt(g_area, x, z) == 'v'; }

static Btn grindStep(int px, int pz) {
  const AreaDef &a = AREAS[g_area];
  if (!a.encounter) return B_NONE;
  if (tileAt(g_area, px, pz) == 'v') {
    // pace about in the grass
    for (int k = 0; k < 4; k++) {
      int d = (s_actions + k) & 3;
      if (tileAt(g_area, px + DX[d], pz + DZ[d]) == 'v' && walkable(px + DX[d], pz + DZ[d])) return DIRKEY[d];
    }
    for (int d = 0; d < 4; d++) if (walkable(px + DX[d], pz + DZ[d])) return DIRKEY[d];
    return B_NONE;
  }
  int gx, gz;
  if (bfs(px, pz, goalGrass, nullptr, gx, gz)) { takePath(gx, gz); return B_NONE; }
  return B_NONE;
}

// --- resting: back to Mrs. Quill's or the town's inn when the team is worn out -----------------------------
static bool s_heal = false;
static int teamHealth() { // percent of the whole team's hit points
  int hp = 0, mx = 0;
  for (int i = 0; i < g_nParty; i++) { hp += g_party[i].hp; mx += statsOf(g_party[i]).hp; }
  return mx ? hp * 100 / mx : 100;
}
static int homeTown() {
  int t = getVar("town");
  static const uint8_t TOWNS[6] = {A_TOWN, A_TOWN, A_SAND, A_GREEN, A_SKJOLD, A_UR};
  return TOWNS[t < 0 ? 0 : t > 5 ? 5 : t];
}
static bool goalRester(int x, int z, void *);

// nothing in front to talk to or read (so Centre opens the pause menu)
static bool blockedNothing(int x, int z) {
  for (int i = 0; i < g_nActors; i++) if ((int)g_actor[i].x == x && (int)g_actor[i].z == z && g_actor[i].script) return false;
  for (int i = 0; i < spotCount(); i++) { const SpotDef &s = spotAt(i); if (s.area == g_area && s.x == x && s.z == z) return false; }
  char c = tileAt(g_area, x, z);
  return c != 'C' && c != 'K' && c != '#';
}
static Btn worldDecide() {
  if (worldBusy() || g_player.moving || g_fade > 0.5f) return B_NONE; // keys are ignored while the screen fades
  int px = (int)g_player.x, pz = (int)g_player.z;
  static bool dbg = getenv("GGBOT") != nullptr;
  if (dbg) printf("[w] area %d at %d,%d face %d path %d/%d after %d/%d" "%c", g_area, px, pz, g_player.face, s_pathI, s_pathN, s_afterI, s_afterN, 10);
  // carrying out a plan
  if (s_pathI < s_pathN) {
    int d = s_path[s_pathI];
    int nx = px + DX[d], nz = pz + DZ[d];
    if (!walkable(nx, nz) && !(s_pathI == s_pathN - 1)) { s_pathN = 0; s_afterN = 0; if (++s_stuckWalk > 50) { s_stuckWalk = 0; } return B_NONE; }
    s_pathI++;
    return DIRKEY[d];
  }
  if (s_afterI < s_afterN) { if (dbg) printf("   press %d (fade %.2f)%c", s_after[s_afterI], g_fade, 10); return s_after[s_afterI++]; }

  // worn out: go and rest
  if (!s_heal && g_nParty && teamHealth() < 45) { s_heal = true; s_pathN = s_afterN = 0; s_pathI = s_afterI = 0; }
  if (s_heal && teamHealth() >= 95) { s_heal = false; Script::g_botWantRest = false; }
  if (s_heal) {
    Script::g_botWantRest = true;
    // the one who lets you rest, if they're here
    s_nTg = 0;
    for (int i = 0; i < g_nActors; i++) {
      const Actor &a = g_actor[i];
      if (a.script && showIfOk(a.showIf) && (!strcmp(a.script, "quill") || !strcmp(a.script, "innkeeper")))
        s_tg[s_nTg++] = {(int)a.x, (int)a.z, true, 0, a.script};
    }
    int hx, hz;
    if (s_nTg && bfs(px, pz, goalTarget, nullptr, hx, hz)) {
      takePath(hx, hz);
      s_afterN = 0; s_afterI = 0;
      s_after[s_afterN++] = DIRKEY[s_goalD];
      s_after[s_afterN++] = B_OK;
      return B_NONE;
    }
    // otherwise the way that leads closest to them
    gatherWays();
    int best = -1, bs = 1 << 30, home = homeTown();
    for (int i = 0; i < s_nWays; i++) {
      int d = s_ways[i].dest, sc;
      if (d == A_QUILL || d == A_INN) sc = (home == A_TOWN) == (d == A_QUILL) ? 0 : 40;
      else if (d == home) sc = 1;
      else if (d < 0) sc = 2;
      else if (d == A_TOWN || d == A_CLIFFS) sc = home == A_TOWN ? 3 : 30;
      else sc = 30;
      s_wayPick = i;
      int wx, wz;
      if (sc < bs && bfs(px, pz, goalWay, nullptr, wx, wz)) { bs = sc; best = i; }
    }
    if (best >= 0 && bs < 30) {
      s_wayPick = best;
      int wx, wz;
      bfs(px, pz, goalWay, nullptr, wx, wz);
      takePath(wx, wz);
      s_afterN = 0; s_afterI = 0;
      if (s_ways[best].press != B_NONE) s_after[s_afterN++] = s_ways[best].press;
      return B_NONE;
    }
    // far from home (another town with no inn door here): carry on regardless
  }
  // a connection waiting on the board: open the pause menu (Centre, facing nothing) and make it
  if (UI::botLinkAvailable()) {
    int fx = px + DX[g_player.face], fz = pz + DZ[g_player.face];
    if (blockedNothing(fx, fz)) return B_OK;
    // something is in front: step aside first
    for (int d = 0; d < 4; d++) if (walkable(px + DX[d], pz + DZ[d]) && blockedNothing(px + 2 * DX[d], pz + 2 * DZ[d])) return DIRKEY[d];
  }
  // training: walk the grass until enough wild battles are fought
  if (s_grind > 0) {
    Btn g = grindStep(px, pz);
    if (g != B_NONE || s_pathI < s_pathN) return g;
    // no grass here: head for somewhere that has some
  }
  // a new plan: someone or something not yet tried here
  gatherTargets(false);
  int gx, gz;
  if (s_nTg && bfs(px, pz, goalTarget, nullptr, gx, gz)) {
    takePath(gx, gz);
    const Target &t = s_tg[s_goalT];
    if (dbg) printf("   target %s at %d,%d solid %d from %d,%d dir %d mode %d%c", t.script, t.x, t.z, t.solid, gx, gz, s_goalD, s_goalMode, 10);
    markTried(t.key);
    s_afterN = 0; s_afterI = 0;
    // one press: a step in beside it (now facing it), or a turn towards someone solid
    s_after[s_afterN++] = DIRKEY[s_goalD];
    s_after[s_afterN++] = B_OK;
    return B_NONE;
  }
  // everyone here is tried (or out of reach): mark the unreachable as tried
  for (int t = 0; t < s_nTg; t++) markTried(s_tg[t].key);
  // a way taken that left us where we were (a locked door, a story stop) counts as tried
  if (s_wayKey && s_wayFrom == g_area) markTried(s_wayKey);
  s_wayKey = 0;
  // somewhere not yet seen since the story moved on
  gatherWays();
  int best = -1; uint32_t bestScore = 0xFFFFFFFF;
  for (int i = 0; i < s_nWays; i++) {
    int dest = s_ways[i].dest;
    if (tried(wayKey(s_ways[i]))) { if (getenv("GGWAYS")) printf("   way %d tried%c", i, 10); continue; }
    uint32_t score;
    if (s_grind > 0) { score = dest >= 0 && AREAS[dest].encounter ? 0 : dest < 0 ? 1 : 2 + s_areaLast[dest]; }
    else if (dest < 0) score = 1;                                   // back outside: fine
    else if (!s_areaSeen[dest]) score = 2 + s_areaLast[dest];      // not this epoch: the longest ago first
    else score = 1000000 + s_areaLast[dest];                       // seen: only if nothing else
    s_wayPick = i;
    int wx, wz;
    if (!bfs(px, pz, goalWay, nullptr, wx, wz)) { if (getenv("GGWAYS")) printf("   way %d unreachable%c", i, 10); continue; }
    if (getenv("GGWAYS")) printf("   way %d -> %d at %d,%d score %u%c", i, dest, s_ways[i].x, s_ways[i].z, score, 10);
    if (score < bestScore) { bestScore = score; best = i; }
  }
  // all seen: try the station
  if (bestScore >= 1000000 && s_grind == 0 && s_sinceProgress > 300) {
    gatherTargets(true);
    if (s_nTg && bfs(px, pz, goalTarget, nullptr, gx, gz)) {
      takePath(gx, gz);
      markTried(s_tg[s_goalT].key);
      s_afterN = 0; s_afterI = 0;
      s_after[s_afterN++] = DIRKEY[s_goalD];
      s_after[s_afterN++] = B_OK;
      return B_NONE;
    }
  }
  // everything tried and every place seen: the team must be too weak. Train, then try it all again.
  if (bestScore >= 1000000 && s_grind == 0 && s_sinceProgress > 600) {
    s_grind = 8; s_grindBattles = s_battles;
    log("nothing left to try: training in the grass (lead level %s%d)", "", g_nParty ? g_party[0].lvl : 0);
  }
  if (best < 0) return B_DOWN; // nowhere at all: wander
  s_wayPick = best;
  int wx, wz;
  bfs(px, pz, goalWay, nullptr, wx, wz);
  takePath(wx, wz);
  s_wayKey = wayKey(s_ways[best]); s_wayFrom = g_area;
  s_afterN = 0; s_afterI = 0;
  if (s_ways[best].press != B_NONE) s_after[s_afterN++] = s_ways[best].press;
  return B_NONE;
}

static int s_endingDummy = 0;
static int s_ending = 0;
int ending() { return s_ending; }
int actions() { return s_actions; }
int sinceProgress() { return s_sinceProgress; }

void report() {
  printf("[bot] actions %d  epoch %08x  since progress %d  area %d (%s) at %d,%d  mode %d  ui %d  script %d  battles %d won %d lost %d  party %d lead lvl %d  vars %d\n",
         s_actions, s_epoch, s_sinceProgress, g_area, AREAS[g_area].name, (int)g_player.x, (int)g_player.z, (int)g_mode, UI::active(),
         Script::botState(), s_battles, s_wins, s_losses, g_nParty, g_nParty ? g_party[0].lvl : 0, varsUsed());
  printf("[bot] cases: c1 open %d closed %d | c2 %d %d | c3 %d %d | c4 %d %d | act1 %d | lies %d deductions %d fragments %d\n",
         getVar("case1_open"), getVar("case1_closed"), getVar("case2_open"), getVar("case2_closed"), getVar("case3_open"),
         getVar("case3_closed"), getVar("case4_open"), getVar("case4_closed"), getVar("act1_done"), getVar("lies_caught"),
         getVar("deductions"), getVar("fragments"));
}

// The next key to press, or B_NONE to let time pass.
Btn decide() {
  s_actions++;
  uint32_t e = varsDigest();
  if (e != s_epoch) {
    s_epoch = e; s_sinceProgress = 0; s_nTried = 0;
    memset(s_areaSeen, 0, sizeof(s_areaSeen));
  } else s_sinceProgress++;
  if (g_area != s_lastArea && g_mode == Mode::World) {
    s_lastArea = g_area;
    s_areaSeen[g_area] = 1;
    s_areaLast[g_area] = (uint32_t)++s_areaClock;
    s_pathN = s_afterN = 0;
    s_wayKey = 0; // that way worked
  }
  if (g_mode == Mode::Title) {
    if (getVar("ending_seen")) return B_NONE;
    const char *t = titleSelText();
    return t[0] == 'A' ? B_OK : B_DOWN;
  }
  if (g_mode == Mode::Ending) {
    if (!s_ending) { s_ending = getVar("card_now"); printf("[bot] reached ending %d\n", s_ending); report(); }
    return B_NONE;
  }
  if (g_mode == Mode::Battle) {
    if (!s_inBattle) {
      s_inBattle = true; s_battles++;
      if (s_grind > 0) s_sinceProgress = 0; // training is progress of a kind
      if (s_grind > 0 && --s_grind == 0) {
        // trained: everything is worth another try
        s_nTried = 0; memset(s_areaSeen, 0, sizeof(s_areaSeen)); s_sinceProgress = 0;
        log("training done (lead level %s%d)", "", g_nParty ? g_party[0].lvl : 0);
      }
    }
    return battleBotKey();
  }
  if (s_inBattle) {
    s_inBattle = false;
    s_pathN = s_afterN = 0;
  }
  if (UI::active()) return UI::botKey();
  if (Script::active()) { s_pathN = s_afterN = 0; return Script::botKey(); }
  if (g_mode == Mode::World) return worldDecide();
  return B_OK;
}

} // namespace Bot
} // namespace Gaslight
#endif
