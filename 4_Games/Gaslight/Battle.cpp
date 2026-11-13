#include <stdlib.h>
// Gaslight & Griffins - creature battles: turn-based, team of six, four
// moves each, a type chart, status, catching with lanterns, XP and growing.
#pragma GCC optimize("O2")
#include "Game.h"
#include "Raster.h"
#include "RunAudio.h"
#include "Tune.h"
#include "GLMusic.h"
#include <string.h>
#include <esp_heap_caps.h>
#include <stdarg.h>
#include <stdio.h>

namespace Gaslight {

enum Fx : uint8_t { E_NONE, E_BURN, E_SOAK, E_CHARM, E_HAUNT, E_RUST, E_SLEEP, E_STUN, E_ATKUP, E_DEFUP, E_SPDUP,
                    E_ATKDN, E_DEFDN, E_SPDDN, E_HEAL, E_DRAIN, E_FIRST, E_RECOIL, E_CRIT, E_LURE, E_LOCK, E_TRUTH,
                    E_ATKUP2, E_DEFUP2, E_SPDUP2, E_FATE };

// --- trainers ----------------------------------------------------------------------------------
struct TeamDef { const char *name; uint8_t look; uint8_t n; uint8_t sp[4], lvl[4]; int money; };
static const TeamDef TEAMS[] = {
    {"Climber Tam", L_TRAINER_A, 2, {16, 9}, {5, 6}, 60},
    {"Birder Noor", L_TRAINER_B, 2, {16, 20}, {6, 7}, 70},
    {"Scholar Fenn", L_TRAINER_C, 2, {14, 11}, {7, 7}, 80},
    {"Juno", L_JUNO, 2, {3, 22}, {9, 8}, 150},            // starter replaced by the counter to yours
    {"Dockside Heavy", L_SAILOR2, 2, {9, 10}, {9, 11}, 120},
    {"Capt. Halloran", L_HALLORAN, 3, {18, 17, 4}, {11, 12, 14}, 400},
    {"Masked Curator", L_CURATOR, 2, {12, 19}, {12, 13}, 250},
    {"Old Salt Morrow", L_SAILOR2, 1, {18}, {8}, 50},
    {"Augustin Crewe", L_CREWE, 2, {10, 19}, {12, 13}, 300},
    {"Juno", L_JUNO, 1, {3}, {5}, 80},                    // the first meeting
    {"Aeolus's thief", L_COUNT, 1, {16}, {8}, 0},         // the gull with the bag of winds
    // 11..15 Sandmere
    {"Dune Runner Asha", L_TRAINER_A, 2, {24, 29}, {15, 16}, 220},
    {"Egyptologist Pell", L_TRAINER_B, 2, {31, 33}, {16, 17}, 240},
    {"Imani Sato", L_IMANI, 3, {29, 25, 34}, {19, 20, 22}, 700},
    {"Juno", L_JUNO, 2, {3, 22}, {19, 18}, 400},
    {"Masked Curator", L_CURATOR, 2, {27, 28}, {19, 21}, 450},
    // 16..20 Greenwold
    {"Forester Rowan", L_TRAINER_C, 2, {35, 37}, {22, 23}, 300},
    {"Huntress Wyn", L_TRAINER_A, 2, {43, 41}, {23, 24}, 320},
    {"Ser Ivy Marchbank", L_IVY, 3, {39, 42, 40}, {26, 27, 29}, 900},
    {"The Green Knight", L_GREENKNIGHT, 2, {40, 38}, {31, 30}, 0},
    {"Juno", L_JUNO, 2, {3, 23}, {27, 26}, 500},
    // 21..24 Skjold
    {"Skier Brenna", L_TRAINER_B, 2, {47, 45}, {30, 31}, 380},
    {"Smith's lad Orm", L_TRAINER_C, 2, {49, 51}, {31, 32}, 400},
    {"Hilde Ravnsdottir", L_HILDE, 3, {48, 17, 46}, {34, 34, 36}, 1200},
    {"Insp. Castellane", L_BRAM, 3, {23, 52, 19}, {36, 37, 38}, 0},
    // 25..30 Ur-Lumen
    {"Canal Guard Ennu", L_TRAINER_A, 2, {53, 55}, {38, 38}, 500},
    {"Curator of Sound", L_CURATOR, 2, {13, 19}, {40, 41}, 300},
    {"Curator of Scales", L_CURATOR, 2, {28, 26}, {40, 41}, 300},
    {"Curator of Thorns", L_CURATOR, 2, {40, 58}, {41, 42}, 300},
    {"Curator of Wings", L_CURATOR, 2, {48, 56}, {41, 42}, 300},
    {"The Collector", L_VANEMASK, 4, {13, 54, 56, 59}, {42, 42, 43, 45}, 0},
    {"Juno", L_JUNO, 2, {3, 23}, {38, 37}, 0},
};

// --- state -----------------------------------------------------------------------------------------
struct Side {
  Mon *team; int n; int act;
  int8_t stage[3];   // atk, def, spd
  uint8_t lastMove; bool locked; uint8_t fate; uint8_t charmT;
};
static Mon s_foe[6];
static Side S[2];
static bool s_wild = false;
static int s_teamId = -1;
static const char *s_winLabel = nullptr, *s_loseLabel = nullptr;
static bool s_over = false, s_won = false, s_caught = false;
static float s_t = 0;
static int s_sel = 0, s_moveSel = 0, s_teamSel = 0, s_bagSel = 0;
static int s_returnArea = 0;

enum Phase : uint8_t { PH_MENU, PH_MOVES, PH_TEAM, PH_BAG, PH_BEATS, PH_FORCE };
static Phase s_phase = PH_BEATS;

enum Anim : uint8_t { AN_NONE, AN_LUNGE, AN_HIT, AN_FAINT, AN_IN, AN_THROW, AN_SHAKE, AN_CAUGHT, AN_UP, AN_DOWN, AN_HEAL, AN_GROW };
struct Beat { char text[80]; uint8_t anim, who; int8_t act[2]; int16_t hp[2]; bool vis[2]; uint8_t type; };
static const int MAXB = 40;
static Beat s_beat[MAXB];
static int s_nb = 0, s_cur = 0;
static float s_beatT = 0;
static float s_hpShow[2];
static int s_showAct[2];
static bool s_vis[2];
static MeshBuf s_mesh;

static int maxHp(const Mon &m) { return statsOf(m).hp; }
static Mon &act(int s) { return S[s].team[S[s].act]; }
static const char *nm(int s) { return SPECIES[act(s).sp].name; }

static void beat(uint8_t anim, uint8_t who, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void beat(uint8_t anim, uint8_t who, const char *fmt, ...) {
  if (s_nb >= MAXB) return;
  Beat &b = s_beat[s_nb++];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b.text, sizeof(b.text), fmt, ap);
  va_end(ap);
  b.anim = anim; b.who = who;
  for (int s = 0; s < 2; s++) {
    b.act[s] = (int8_t)S[s].act;
    b.hp[s] = (int16_t)act(s).hp;
    b.vis[s] = act(s).hp > 0 || (anim == AN_FAINT && who == s);
  }
  if (s_caught) b.vis[1] = false;
  if (anim == AN_THROW || anim == AN_SHAKE) b.vis[1] = true;
  b.type = 0;
}

static void startBeats() {
  s_cur = 0;
  s_beatT = 0;
  s_phase = PH_BEATS;
}

// --- set-up ------------------------------------------------------------------------------------------

bool battleAlloc() {
  if (s_mesh.v) return true;
  return meshAlloc(s_mesh, 4000, 2600);
}

static int firstAlive(int s) {
  for (int i = 0; i < S[s].n; i++)
    if (S[s].team[i].hp > 0) return i;
  return -1;
}

static void begin() {
  battleAlloc();
  s_returnArea = g_area;
  S[0].team = g_party; S[0].n = g_nParty; S[0].act = firstAlive(0) < 0 ? 0 : firstAlive(0);
  S[1].team = s_foe; S[1].act = 0;
  for (auto &sd : S) { memset(sd.stage, 0, 3); sd.lastMove = 255; sd.locked = false; sd.fate = 0; sd.charmT = 0; }
  s_over = s_won = s_caught = false;
  s_nb = 0;
  s_t = 0;
  s_sel = 0; s_moveSel = 0;
  for (int s = 0; s < 2; s++) { s_hpShow[s] = act(s).hp; s_showAct[s] = S[s].act; s_vis[s] = true; }
  g_mode = Mode::Battle;
  g_flash = 1;
  sfx(RunAudio::Thunder);
}

// The big fights get the boss music: rich trainers, the Curators, the Knight, the Collector.
bool battleIsBoss() {
  if (s_wild || s_teamId < 0) return false;
  const TeamDef &t = TEAMS[s_teamId];
  return t.money >= 250 || strstr(t.name, "Curator") || strstr(t.name, "Collector") || strstr(t.name, "Knight");
}

void battleWild(int sp, int lvl) {
  monInit(s_foe[0], sp, lvl, rnd());
  S[1].n = 1;
  s_wild = true;
  s_teamId = -1;
  s_winLabel = s_loseLabel = nullptr;
  begin();
  beat(AN_IN, 1, "A wild %s comes out of the long grass!", nm(1));
  beat(AN_IN, 0, "Go on, %s!", nm(0));
  startBeats();
}

void battleStart(int team, const char *winLabel, const char *loseLabel) {
  const TeamDef &t = TEAMS[team];
  for (int i = 0; i < t.n; i++) {
    int sp = t.sp[i];
    if ((team == 3 || team == 9 || team == 14 || team == 20 || team == 31) && i == 0) {
      int st = getVar("starter");
      int line = (st / 3 + 1) % 3 * 3;
      sp = line + (t.lvl[i] >= 34 ? 2 : t.lvl[i] >= 16 ? 1 : 0);
    }
    monInit(s_foe[i], sp, t.lvl[i], hash32(team, i));
  }
  S[1].n = t.n;
  s_wild = false;
  s_teamId = team;
  s_winLabel = winLabel;
  s_loseLabel = loseLabel;
  begin();
  beat(AN_NONE, 1, "%s wants to battle!", t.name);
  beat(AN_IN, 1, "%s sends out %s!", t.name, nm(1));
  beat(AN_IN, 0, "Go on, %s!", nm(0));
  startBeats();
}

// --- the rules ------------------------------------------------------------------------------------------

static float stageMul(int s) { return s >= 0 ? (2.0f + s) / 2.0f : 2.0f / (2.0f - s); }

static int effStat(int side, int which) {
  Mon &m = act(side);
  MonStats st = statsOf(m);
  int v = which == 0 ? st.atk : which == 1 ? st.def : st.spd;
  float k = stageMul(S[side].stage[which]);
  if (which == 0 && m.status == ST_BURN) k *= 0.75f;
  if (which == 1 && m.status == ST_RUST) k *= 0.7f;
  if (which == 2 && (m.status == ST_SOAK)) k *= 0.67f;
  if (which == 2 && (m.status == ST_STUN)) k *= 0.5f;
  return (int)(v * k);
}

static bool hasType(const Mon &m, uint8_t t) { return SPECIES[m.sp].t1 == t || SPECIES[m.sp].t2 == t; }

static void setStage(int side, int which, int d) {
  static const char *const names[3] = {"attack", "defence", "speed"};
  int8_t &s = S[side].stage[which];
  int old = s;
  s = (int8_t)clampi(s + d, -4, 4);
  if (s == old) { beat(AN_NONE, side, "%s's %s won't go any %s.", nm(side), names[which], d > 0 ? "higher" : "lower"); return; }
  beat(d > 0 ? AN_UP : AN_DOWN, side, "%s's %s %s%s.", nm(side), names[which], d > 0 ? "rose" : "fell", d > 1 || d < -1 ? " sharply" : "");
}

static bool giveStatus(int side, uint8_t st) {
  Mon &m = act(side);
  if (m.status != ST_OK || m.hp == 0) return false;
  if (st == ST_BURN && hasType(m, T_FIRE)) return false;
  if (st == ST_SOAK && hasType(m, T_WATER)) return false;
  if (st == ST_CHARM && hasType(m, T_IRON)) return false;
  if (st == ST_STUN && hasType(m, T_SPIRIT)) return false;
  m.status = st;
  m.statusT = st == ST_SLEEP ? (uint8_t)(1 + rnd() % 3) : st == ST_CHARM ? (uint8_t)(2 + rnd() % 3) : 0;
  static const char *const say[8] = {"", "was burned!", "is soaked through!", "is charmed and muddled!", "is haunted!",
                                     "is rusting!", "fell asleep!", "is stunned!"};
  beat(AN_DOWN, side, "%s %s", nm(side), say[st]);
  return true;
}

static int damage(int a, int d, const MoveDef &mv, bool &crit, float &eff, bool truth) {
  Mon &A = act(a), &D = act(d);
  const Species &sa = SPECIES[A.sp], &sd = SPECIES[D.sp];
  int atk = truth ? statsOf(A).atk : effStat(a, 0), def = truth ? statsOf(D).def : effStat(d, 1);
  float base = ((2.0f * A.lvl / 5 + 2) * mv.power * atk / (float)(def < 1 ? 1 : def)) / 50 + 2;
  float stab = (sa.t1 == mv.type || sa.t2 == mv.type) ? 1.5f : 1.0f;
  eff = typeMult(mv.type, sd.t1) * (sd.t2 != T_NONE ? typeMult(mv.type, sd.t2) : 1.0f);
  if (D.status == ST_SOAK && mv.type == T_STONE) eff *= 1.5f;
  int critChance = mv.effect == E_CRIT ? 4 : 16;
  crit = S[a].fate > 0 || (rnd() % critChance) == 0;
  float r = 0.85f + frand() * 0.15f;
  float dmg = base * stab * eff * (crit ? 1.5f : 1.0f) * r;
  if (eff > 0 && dmg < 1) dmg = 1;
  return (int)dmg;
}

static void faintCheck(int side) {
  if (act(side).hp == 0) {
    beat(AN_FAINT, side, "%s%s fainted!", side == 1 && s_wild ? "The wild " : "", nm(side));
    sfx(RunAudio::Bonk);
  }
}

static void useMove(int a, int mvId) {
  int d = 1 - a;
  Mon &A = act(a), &D = act(d);
  if (A.hp == 0) return;
  const MoveDef &mv = MOVES[mvId];
  // status that stops a turn
  if (A.status == ST_SLEEP) {
    if (A.statusT > 0) A.statusT--;
    if (A.statusT == 0) { A.status = ST_OK; beat(AN_NONE, a, "%s woke up!", nm(a)); }
    else { beat(AN_NONE, a, "%s is fast asleep.", nm(a)); return; }
  }
  if (A.status == ST_STUN && rnd() % 4 == 0) { beat(AN_NONE, a, "%s is stunned and can't move!", nm(a)); return; }
  if (A.status == ST_CHARM) {
    if (A.statusT > 0) A.statusT--;
    if (A.statusT == 0) { A.status = ST_OK; beat(AN_NONE, a, "%s shook off the glamour.", nm(a)); }
    else if (rnd() % 3 == 0) {
      int self = statsOf(A).hp / 8 + 1;
      A.hp = (uint16_t)(A.hp > self ? A.hp - self : 0);
      beat(AN_HIT, a, "%s is muddled and hurt itself!", nm(a));
      faintCheck(a);
      return;
    }
  }
  if (S[a].locked && S[a].lastMove == mvId) {
    beat(AN_NONE, a, "%s can't answer the riddle twice!", nm(a));
    S[a].locked = false;
    return;
  }
  S[a].locked = false;
  S[a].lastMove = (uint8_t)mvId;
  beat(AN_LUNGE, a, "%s%s used %s!", a == 1 && s_wild ? "The wild " : "", nm(a), mv.name);
  s_beat[s_nb - 1].type = mv.type;
  if (rnd() % 100 >= mv.acc) { beat(AN_NONE, a, "But it missed!"); return; }
  if (mv.power > 0) {
    bool crit;
    float eff;
    int dmg = damage(a, d, mv, crit, eff, mv.effect == E_TRUTH);
    if (eff == 0) { beat(AN_NONE, d, "It doesn't touch %s at all...", nm(d)); return; }
    D.hp = (uint16_t)(D.hp > dmg ? D.hp - dmg : 0);
    beat(AN_HIT, d, "%s", crit ? "A telling blow!" : eff >= 2 ? "It's very effective!" : eff < 1 ? "It's not very effective..." : "");
    sfx(eff >= 2 ? RunAudio::Thunder : RunAudio::Bonk);
    if (!s_beat[s_nb - 1].text[0]) snprintf(s_beat[s_nb - 1].text, 80, "%s takes %d damage.", nm(d), dmg);
    else if (crit && eff >= 2) snprintf(s_beat[s_nb - 1].text, 80, "A telling blow! It's very effective!");
    if (S[a].fate > 0) S[a].fate--;
    if (mv.effect == E_DRAIN && dmg > 0) {
      int h = dmg / 2 + 1, mx = maxHp(A);
      A.hp = (uint16_t)(A.hp + h > mx ? mx : A.hp + h);
      beat(AN_HEAL, a, "%s drank some of its strength.", nm(a));
    }
    if (mv.effect == E_LOCK) { S[d].locked = true; }
    faintCheck(d);
    if (D.hp == 0) return;
  }
  // effects
  bool hit = mv.power == 0 || (rnd() % 100) < mv.chance;
  if (!hit) return;
  switch (mv.effect) {
    case E_BURN: giveStatus(d, ST_BURN); break;
    case E_SOAK: giveStatus(d, ST_SOAK); break;
    case E_CHARM: if (!giveStatus(d, ST_CHARM) && mv.power == 0) beat(AN_NONE, d, "%s isn't fooled.", nm(d)); break;
    case E_HAUNT: giveStatus(d, ST_HAUNT); break;
    case E_RUST: giveStatus(d, ST_RUST); break;
    case E_SLEEP: if (!giveStatus(d, ST_SLEEP)) beat(AN_NONE, d, "It had no effect."); break;
    case E_STUN: giveStatus(d, ST_STUN); break;
    case E_ATKUP: setStage(a, 0, 1); break;
    case E_ATKUP2: setStage(a, 0, 2); break;
    case E_DEFUP: setStage(a, 1, 1); break;
    case E_DEFUP2: setStage(a, 1, 2); break;
    case E_SPDUP: setStage(a, 2, 1); break;
    case E_SPDUP2: setStage(a, 2, 2); break;
    case E_ATKDN: setStage(d, 0, -1); break;
    case E_DEFDN: setStage(d, 1, -1); break;
    case E_SPDDN: setStage(d, 2, -1); break;
    case E_LURE: if (!giveStatus(d, ST_CHARM)) {} setStage(d, 0, -1); break;
    case E_HEAL: {
      int mx = maxHp(A), h = mx / 2;
      if (A.hp >= mx) { beat(AN_NONE, a, "%s is already well.", nm(a)); break; }
      A.hp = (uint16_t)(A.hp + h > mx ? mx : A.hp + h);
      beat(AN_HEAL, a, "%s looks much better.", nm(a));
      break;
    }
    case E_FATE: S[a].fate = 2; beat(AN_UP, a, "The thread of fate pulls tight around %s.", nm(a)); break;
    default: break;
  }
}

static void endOfTurn(int s) {
  Mon &m = act(s);
  if (m.hp == 0) return;
  int mx = maxHp(m);
  if (m.status == ST_BURN) {
    int d = mx / 16 + 1;
    m.hp = (uint16_t)(m.hp > d ? m.hp - d : 0);
    beat(AN_HIT, s, "%s is hurt by its burn.", nm(s));
    faintCheck(s);
  } else if (m.status == ST_HAUNT) {
    int d = mx / 10 + 1;
    m.hp = (uint16_t)(m.hp > d ? m.hp - d : 0);
    beat(AN_HIT, s, "Something cold clings to %s.", nm(s));
    faintCheck(s);
  }
}

static int foeChoose() {
  Mon &F = act(1), &P = act(0);
  int best = 0;
  float bestScore = -1;
  for (int i = 0; i < 4; i++) {
    int mv = F.moves[i];
    if (mv == 255) continue;
    const MoveDef &m = MOVES[mv];
    float sc;
    if (m.power > 0) {
      float eff = typeMult(m.type, SPECIES[P.sp].t1) * (SPECIES[P.sp].t2 != T_NONE ? typeMult(m.type, SPECIES[P.sp].t2) : 1);
      sc = m.power * eff * (SPECIES[F.sp].t1 == m.type || SPECIES[F.sp].t2 == m.type ? 1.5f : 1) * m.acc / 100.0f;
    } else {
      sc = (m.effect == E_HEAL && F.hp < maxHp(F) / 2) ? 120 : 35;
      if (m.effect >= E_ATKUP && m.effect <= E_SPDDN && S[1].stage[0] > 1) sc = 5;
    }
    if (S[1].locked && S[1].lastMove == mv) sc = 0;
    sc *= 0.7f + frand() * 0.6f;
    if (sc > bestScore) { bestScore = sc; best = mv; }
  }
  return best;
}

static void grantXp() {
  Mon &F = act(1);
  Mon &P = act(0);
  if (P.hp == 0) return;
  int xp = SPECIES[F.sp].xpYield * F.lvl / 6 * (s_wild ? 1 : 3) / 2 + 1;
  beat(AN_NONE, 0, "%s gains %d experience.", nm(0), xp);
  int total = P.xp + xp;
  P.xp = (uint16_t)(total > 65535 ? 65535 : total);
  while (P.lvl < 60 && P.xp >= xpForLevel(P.lvl + 1)) {
    int oldMax = maxHp(P);
    P.lvl++;
    int newMax = maxHp(P);
    P.hp = (uint16_t)(P.hp + (newMax - oldMax));
    beat(AN_GROW, 0, "%s grew to level %d!", nm(0), P.lvl);
    sfx(RunAudio::DingTop);
    const Species &sp = SPECIES[P.sp];
    for (int i = 0; i < 6; i++) {
      if (sp.learn[i][0] != P.lvl) continue;
      int mv = sp.learn[i][1];
      bool have = false;
      int slot = -1;
      for (int k = 0; k < 4; k++) { have = have || P.moves[k] == mv; if (P.moves[k] == 255 && slot < 0) slot = k; }
      if (have) continue;
      if (slot < 0) {
        // forget the weakest move to make room
        int weak = 0;
        for (int k = 1; k < 4; k++)
          if (MOVES[P.moves[k]].power < MOVES[P.moves[weak]].power) weak = k;
        beat(AN_NONE, 0, "%s forgot %s...", nm(0), MOVES[P.moves[weak]].name);
        slot = weak;
      }
      P.moves[slot] = (uint8_t)mv;
      beat(AN_UP, 0, "...and learned %s!", MOVES[mv].name);
    }
  }
}

static bool partyAlive() { return firstAlive(0) >= 0; }

static void finish(bool won) {
  s_over = true;
  s_won = won;
  if (won && !s_wild && s_teamId >= 0) {
    Tune::play(Songs::M_VICTORY);
    g_money += TEAMS[s_teamId].money;
    beat(AN_NONE, 0, "You win %d shillings.", TEAMS[s_teamId].money);
  }
  if (!won) beat(AN_NONE, 0, "You have no one left who can fight...");
  // growing into a new form happens as the battle ends
  if (won) {
    for (int i = 0; i < g_nParty; i++) {
      Mon &m = g_party[i];
      const Species &sp = SPECIES[m.sp];
      if (sp.evolveAt && m.lvl >= sp.evolveAt && m.hp > 0) {
        int oldMax = maxHp(m);
        const char *from = sp.name;
        m.sp = sp.evolveTo;
        m.hp = (uint16_t)(m.hp + maxHp(m) - oldMax);
        int keep = S[0].act;
        S[0].act = i;
        beat(AN_GROW, 0, "%s is changing... it grew into %s!", from, SPECIES[m.sp].name);
        S[0].act = keep;
        setFlag("ever_evolved");
      }
    }
  }
}

static void afterFaints() {
  // foe side
  if (act(1).hp == 0 && !s_caught) {
    grantXp();
    int nx = firstAlive(1);
    if (nx < 0) { finish(true); return; }
    S[1].act = nx;
    memset(S[1].stage, 0, 3);
    beat(AN_IN, 1, "%s sends out %s!", TEAMS[s_teamId].name, nm(1));
  }
  if (act(0).hp == 0) {
    if (!partyAlive()) { finish(false); return; }
    s_phase = PH_FORCE;
    s_teamSel = firstAlive(0);
  }
}

static void doTurn(int kind, int arg) {
  // kind: 0 move, 1 switch, 2 item, 3 run, 4 lantern
  s_nb = 0;
  int pMove = kind == 0 ? arg : -1;
  int fMove = foeChoose();
  if (kind == 1) {
    beat(AN_NONE, 0, "%s, come back!", nm(0));
    S[0].act = arg;
    memset(S[0].stage, 0, 3);
    beat(AN_IN, 0, "Go on, %s!", nm(0));
  } else if (kind == 2) {
    Mon &m = act(0);
    int mx = maxHp(m);
    if (arg == I_TONIC) { m.hp = (uint16_t)(m.hp + 30 > mx ? mx : m.hp + 30); g_items[I_TONIC]--; beat(AN_HEAL, 0, "%s drinks a tonic.", nm(0)); }
    if (arg == I_DRAUGHT) { m.hp = (uint16_t)mx; m.status = ST_OK; g_items[I_DRAUGHT]--; beat(AN_HEAL, 0, "%s is fully restored.", nm(0)); }
    if (arg == I_SALTS)
      for (int i = 0; i < S[0].n; i++)
        if (S[0].team[i].hp == 0) {
          S[0].team[i].hp = (uint16_t)(maxHp(S[0].team[i]) / 2);
          g_items[I_SALTS]--;
          beat(AN_NONE, 0, "%s comes round.", SPECIES[S[0].team[i].sp].name);
          break;
        }
  } else if (kind == 3) {
    int ps = effStat(0, 2), fs = effStat(1, 2);
    if (ps >= fs || rnd() % 100 < 50 + (ps - fs)) {
      beat(AN_NONE, 0, "You got away safely.");
      s_over = true; s_won = true;
      startBeats();
      return;
    }
    beat(AN_NONE, 0, "Couldn't get away!");
  } else if (kind == 4) {
    g_items[arg]--;
    Mon &F = act(1);
    int mx = maxHp(F);
    float bonus = arg == I_SILVER ? 1.6f : arg == I_MOON ? 255.0f : 1.0f;
    float rate = (3.0f * mx - 2.0f * F.hp) * SPECIES[F.sp].catchRate * bonus / (3.0f * mx);
    if (F.status == ST_SLEEP || F.status == ST_STUN) rate *= 1.8f;
    else if (F.status) rate *= 1.3f;
    bool ok = rate >= 255 || (rnd() % 255) < rate;
    int shakes = ok ? 3 : (int)(rnd() % 3);
    beat(AN_THROW, 1, "You hold up the %s...", ITEM_NAMES[arg]);
    for (int i = 0; i < shakes; i++) beat(AN_SHAKE, 1, "%s", i == 0 ? "...the wick flickers..." : i == 1 ? "...flickers..." : "...and holds!");
    if (ok) {
      s_caught = true;
      Mon caught = F;
      caught.status = ST_OK;
      beat(AN_CAUGHT, 1, "%s is caught!", nm(1));
      sfx(RunAudio::DingTop);
      grantXp();
      if (g_nParty < PARTY) { g_party[g_nParty++] = caught; S[0].n = g_nParty; beat(AN_NONE, 0, "%s joins your team.", SPECIES[caught.sp].name); }
      else if (g_nBox < BOX) { g_box[g_nBox++] = caught; beat(AN_NONE, 0, "%s goes to Mrs. Quill's.", SPECIES[caught.sp].name); }
      setFlag("ever_caught");
      s_over = true; s_won = true;
      startBeats();
      return;
    }
    beat(AN_NONE, 1, "Oh! It slipped free!");
  }
  // who goes first
  bool pFirst;
  if (pMove < 0) pFirst = true;
  else {
    bool pPri = MOVES[pMove].effect == E_FIRST, fPri = MOVES[fMove].effect == E_FIRST;
    if (pPri != fPri) pFirst = pPri;
    else {
      int ps = effStat(0, 2), fs = effStat(1, 2);
      pFirst = ps > fs || (ps == fs && (rnd() & 1));
    }
  }
  if (pMove >= 0) {
    if (pFirst) { useMove(0, pMove); if (act(1).hp > 0 && act(0).hp > 0) useMove(1, fMove); }
    else { useMove(1, fMove); if (act(0).hp > 0 && act(1).hp > 0) useMove(0, pMove); }
  } else {
    useMove(1, fMove);
  }
  if (act(0).hp > 0) endOfTurn(0);
  if (act(1).hp > 0) endOfTurn(1);
  afterFaints();
  if (s_phase != PH_FORCE) startBeats();
  else { s_cur = 0; s_beatT = 0; }
}

// --- the flow -----------------------------------------------------------------------------------------------

static void leave() {
  g_mode = Mode::World;
  g_fade = 1;
  bool won = s_won;
  if (!won) {
    // everyone is carried back to Mrs. Quill's
    for (int i = 0; i < g_nParty; i++) { g_party[i].hp = (uint16_t)maxHp(g_party[i]); g_party[i].status = ST_OK; }
    g_money = g_money * 3 / 4;
    if (s_loseLabel) { Script::resume(s_loseLabel); return; }
    worldGoInn();
    Script::start("blackout");
    return;
  }
  for (int i = 0; i < g_nParty; i++) if (g_party[i].status == ST_SLEEP || g_party[i].status == ST_CHARM) g_party[i].status = ST_OK;
  if (s_teamId >= 0) {
    char key[24];
    snprintf(key, sizeof(key), "beat_team%d", s_teamId);
    setFlag(key);
  }
  if (s_winLabel) Script::resume(s_winLabel);
}

static void nextBeat() {
  s_cur++;
  s_beatT = 0;
  if (s_cur < s_nb) return;
  if (s_over) { leave(); return; }
  if (s_phase == PH_FORCE) return;
  s_phase = PH_MENU;
  s_sel = 0;
}

void battleUpdate(float dt) {
  s_t += dt;
  // HP bars catch up with the beat being shown
  for (int s = 0; s < 2; s++) {
    float target = s_phase == PH_BEATS || s_phase == PH_FORCE ? (s_cur < s_nb ? s_beat[s_cur].hp[s] : act(s).hp) : act(s).hp;
    float d = target - s_hpShow[s];
    float step = dt * fmaxf(20.0f, fabsf(d) * 3);
    if (fabsf(d) <= step) s_hpShow[s] = target; else s_hpShow[s] += d > 0 ? step : -step;
  }
  if (s_cur < s_nb && (s_phase == PH_BEATS || s_phase == PH_FORCE)) {
    const Beat &b = s_beat[s_cur];
    if (s_beatT == 0) {
      for (int s = 0; s < 2; s++) {
        if (s_showAct[s] != b.act[s]) s_hpShow[s] = b.hp[s];
        s_showAct[s] = b.act[s];
        s_vis[s] = b.vis[s];
      }
      if (b.anim == AN_HIT) g_shake = 0.4f;
      if (b.anim == AN_GROW) g_flash = 0.8f;
    }
    s_beatT += dt;
    if (s_beatT > 1.6f) nextBeat();
  } else if (s_phase == PH_FORCE && s_cur >= s_nb) {
    // choose who comes out next
  }
}

static void tryLeaveFromForce(int idx) {
  if (idx < 0 || idx >= S[0].n || S[0].team[idx].hp == 0) return;
  S[0].act = idx;
  memset(S[0].stage, 0, 3);
  s_nb = 0;
  beat(AN_IN, 0, "Go on, %s!", nm(0));
  startBeats();
}

void battleKey(Btn b) {
  if (s_phase == PH_BEATS) {
    if (b == B_OK && s_beatT > 0.25f) nextBeat();
    return;
  }
  if (s_phase == PH_FORCE) {
    if (s_cur < s_nb) { if (b == B_OK && s_beatT > 0.25f) nextBeat(); return; }
    if (b == B_UP) s_teamSel = (s_teamSel + S[0].n - 1) % S[0].n;
    if (b == B_DOWN) s_teamSel = (s_teamSel + 1) % S[0].n;
    if (b == B_OK) tryLeaveFromForce(s_teamSel);
    return;
  }
  if (s_phase == PH_MENU) {
    if (b == B_LEFT || b == B_RIGHT) s_sel ^= 1;
    if (b == B_UP || b == B_DOWN) s_sel ^= 2;
    if (b == B_OK) {
      sfx(RunAudio::Tick);
      if (s_sel == 0) { s_phase = PH_MOVES; s_moveSel = 0; }
      else if (s_sel == 1) { s_phase = PH_TEAM; s_teamSel = S[0].act; }
      else if (s_sel == 2) { s_phase = PH_BAG; s_bagSel = 0; }
      else if (s_wild) doTurn(3, 0);
      else { s_nb = 0; beat(AN_NONE, 0, "There's no running from a trainer's battle."); startBeats(); }
    }
    return;
  }
  if (b == B_DOWN) { s_phase = PH_MENU; sfx(RunAudio::Tick); return; }
  if (s_phase == PH_MOVES) {
    Mon &m = act(0);
    if (b == B_LEFT || b == B_RIGHT) s_moveSel ^= 1;
    if (b == B_UP) s_moveSel ^= 2;
    if (b == B_OK && m.moves[s_moveSel] != 255) doTurn(0, m.moves[s_moveSel]);
    return;
  }
  if (s_phase == PH_TEAM) {
    if (b == B_UP) s_teamSel = (s_teamSel + S[0].n - 1) % S[0].n;
    if (b == B_RIGHT) s_teamSel = (s_teamSel + 1) % S[0].n;
    if (b == B_LEFT) s_teamSel = (s_teamSel + S[0].n - 1) % S[0].n;
    if (b == B_OK) {
      if (s_teamSel == S[0].act || S[0].team[s_teamSel].hp == 0) return;
      doTurn(1, s_teamSel);
    }
    return;
  }
  if (s_phase == PH_BAG) {
    static const uint8_t usable[6] = {I_LANTERN, I_SILVER, I_MOON, I_TONIC, I_DRAUGHT, I_SALTS};
    if (b == B_UP) s_bagSel = (s_bagSel + 5) % 6;
    if (b == B_RIGHT) s_bagSel = (s_bagSel + 1) % 6;
    if (b == B_LEFT) s_bagSel = (s_bagSel + 5) % 6;
    if (b == B_OK) {
      int it = usable[s_bagSel];
      if (!g_items[it]) return;
      if (it <= I_MOON) {
        if (!s_wild) { s_nb = 0; beat(AN_NONE, 0, "You can't catch another trainer's creature!"); startBeats(); return; }
        doTurn(4, it);
      } else if (it == I_SALTS) {
        for (int i = 0; i < S[0].n; i++)
          if (S[0].team[i].hp == 0) { doTurn(2, I_SALTS); return; }
      } else doTurn(2, it);
    }
  }
}

void battleWheel(int d) {
  if (s_phase == PH_MENU) s_sel = (s_sel + (d > 0 ? 1 : 3)) & 3;
  else if (s_phase == PH_MOVES) s_moveSel = (s_moveSel + (d > 0 ? 1 : 3)) & 3;
  else if (s_phase == PH_TEAM || (s_phase == PH_FORCE && s_cur >= s_nb)) s_teamSel = (s_teamSel + (d > 0 ? 1 : S[0].n - 1)) % S[0].n;
  else if (s_phase == PH_BAG) s_bagSel = (s_bagSel + (d > 0 ? 1 : 5)) % 6;
}

// --- drawing -------------------------------------------------------------------------------------------------

static const float PX = -1.4f, PZ = 1.2f, FX = 1.8f, FZ = -1.6f;

void battlePrepare() {
  const AreaDef &a = AREAS[s_returnArea];
  meshReset(s_mesh);
  g_light.sx = -0.5f; g_light.sy = 0.7f; g_light.sz = 0.5f; g_light.amb = 0.5f; g_light.dif = 0.55f;
  g_light.tint = a.indoor ? 0xFFD8B0 : 0xFFE6CC;
  uint32_t floor = a.indoor ? 0x5A4030 : (s_returnArea == A_CLIFFS ? 0x4C6A3E : 0x6E6A60);
  addCyl(s_mesh, 0, -0.05f, 0, 9.0f, 0.0f, 9.0f, floor, 12);
  addCyl(s_mesh, PX, 0.0f, PZ, 1.25f, 0.04f, 1.25f, 0x8A8270, 10);
  addCyl(s_mesh, FX, 0.0f, FZ, 1.25f, 0.04f, 1.25f, 0x8A8270, 10);
  if (!a.indoor) {
    float sea[12] = {-30, -0.3f, -40, -30, -0.3f, -8, 30, -0.3f, -8, 30, -0.3f, -40};
    addQuad(s_mesh, sea, 0x1E3F52);
    addBlob(s_mesh, -4, 0, -18, 3, 4, 2, 0x2E3238, 7, 5);
    for (int i = 0; i < 5; i++) addCone(s_mesh, -8.0f + i * 4.2f, 0, -7.5f - (i & 1), 0.9f, 2.6f + (i % 3) * 0.4f, 0x2F4F3A, 6);
  } else {
    float wall[12] = {-12, 0, -6, 12, 0, -6, 12, 6, -6, -12, 6, -6};
    addQuad(s_mesh, wall, 0x5A4632);
  }
  // the two creatures
  float faceP = atan2f(-(FX - PX), FZ - PZ), faceF = atan2f(-(PX - FX), PZ - FZ);
  for (int s = 0; s < 2; s++) {
    if (!s_vis[s]) continue;
    const Mon &m = S[s].team[s_showAct[s]];
    float x = s == 0 ? PX : FX, z = s == 0 ? PZ : FZ, y = 0.04f, k = 1.15f;
    float flash = 0;
    if (s_cur < s_nb && (s_phase == PH_BEATS || s_phase == PH_FORCE)) {
      const Beat &b = s_beat[s_cur];
      float t = s_beatT;
      if (b.who == s) {
        if (b.anim == AN_LUNGE && t < 0.5f) { float p = sinf(t / 0.5f * PI_F) * 0.9f; x += (s == 0 ? FX - PX : PX - FX) * p * 0.25f; z += (s == 0 ? FZ - PZ : PZ - FZ) * p * 0.25f; }
        if (b.anim == AN_HIT && t < 0.6f) { flash = fmodf(t, 0.15f) < 0.075f ? 0.9f : 0; x += sinf(t * 70) * 0.06f; }
        if (b.anim == AN_FAINT) { y -= clampf(t * 2.0f, 0, 1.5f); k *= 1 - clampf(t * 1.2f, 0, 0.9f); }
        if (b.anim == AN_IN && t < 0.4f) { k *= t / 0.4f; flash = 1 - t / 0.4f; }
        if (b.anim == AN_GROW && t < 1.0f) { flash = 1 - t; }
        if (b.anim == AN_HEAL && t < 0.6f) { flash = 0.4f; }
        if ((b.anim == AN_THROW || b.anim == AN_SHAKE) && s == 1) { k *= b.anim == AN_THROW ? 1 - clampf((t - 0.5f) * 2, 0, 1) : 0; }
        if (b.anim == AN_CAUGHT) k = 0;
      }
    }
    if (k > 0.02f) buildMon3D(s_mesh, m.sp, x, y, z, s == 0 ? faceP : faceF, s_t, k, m.seed, flash);
  }
  // the lantern
  if (s_cur < s_nb && (s_beat[s_cur].anim == AN_THROW || s_beat[s_cur].anim == AN_SHAKE || s_beat[s_cur].anim == AN_CAUGHT)) {
    const Beat &b = s_beat[s_cur];
    float t = clampf(s_beatT / 0.5f, 0, 1);
    float lx = b.anim == AN_THROW ? lerpf(PX, FX, t) : FX, lz = b.anim == AN_THROW ? lerpf(PZ, FZ, t) : FZ;
    float ly = b.anim == AN_THROW ? 0.5f + sinf(t * PI_F) * 1.6f : 0.25f;
    float wob = b.anim == AN_SHAKE ? sinf(s_beatT * 18) * 0.12f : 0;
    addBox(s_mesh, lx - 0.18f + wob, ly, lz - 0.18f, lx + 0.18f + wob, ly + 0.42f, lz + 0.18f, 0xFFD27A, 0, 0, 0, 0.9f);
    addCyl(s_mesh, lx + wob, ly + 0.42f, lz, 0.22f, ly + 0.55f, 0.0f, 0xB9853A, 5);
  }
  g_cam.tx = 0.3f; g_cam.ty = -0.3f; g_cam.tz = -0.3f;
  g_cam.dist = 7.2f; g_cam.pitch = 0.30f; g_cam.yaw = -0.15f; g_cam.focal = 380;
  if (g_shake > 0) g_cam.tx += sinf(s_t * 60) * g_shake * 0.15f;
  setSky(a.indoor ? 0x1A1410 : a.sky0, a.indoor ? 0x3A2A1E : a.sky1);
  g_fog.col = a.indoor ? 0x2A2018 : a.fog;
  g_fog.start = 9; g_fog.end = 30; g_fog.max = 0.75f;
  sceneBegin();
  sceneAdd(s_mesh);
  sceneEnd();
}

static void hpBar(float x, float y, float w, float frac) {
  Raster::roundRect(x, y, w, 7, 3, c565(0x1A1A1A));
  uint16_t c = frac > 0.5f ? c565(0x5AC85A) : frac > 0.2f ? c565(0xE8C03A) : c565(0xE04A3A);
  if (frac > 0) Raster::roundRect(x + 1, y + 1, (w - 2) * clampf(frac, 0, 1), 5, 2.5f, c);
}

static void fitText(const FontInfo &f, int x, int y, int maxW, const char *t, uint16_t col) {
  if (textW(f, t) <= maxW) { text(f, x, y, t, col); return; }
  text(F_SMALL, x, y + 3, t, col);
}

static void monPanel(int s, float x, float y, bool big) {
  const Mon &m = S[s].team[s_showAct[s]];
  int mx = maxHp(m);
  panel(x, y, 208, big ? 66 : 48, c565(0x1E222A), 215, Col::BRASS);
  fitText(F_BOLD, (int)x + 8, (int)y + 2, 140, SPECIES[m.sp].name, Col::CREAM);
  char lv[12];
  snprintf(lv, sizeof(lv), "Lv%d", m.lvl);
  text(F_SMALL, (int)x + 160, (int)y + 5, lv, Col::DIM);
  if (m.status) {
    int sx = (int)x + 112;
    Raster::roundRect(sx, y + 30, 50, 16, 4, c565(0x7A2430));
    textC(F_SMALL, sx + 25, (int)y + 28, STATUS_NAMES[m.status], Col::CREAM);
  }
  hpBar(x + 8, y + 34, m.status ? 98 : 192, s_hpShow[s] / (float)mx);
  if (big) {
    char hp[20];
    snprintf(hp, sizeof(hp), "%d / %d", (int)(s_hpShow[s] + 0.5f), mx);
    text(F_SMALL, (int)x + 8, (int)y + 42, hp, Col::DIM);
    // XP toward the next level
    int lo = xpForLevel(m.lvl), hi = xpForLevel(m.lvl + 1);
    float f = clampf((m.xp - lo) / (float)(hi - lo), 0, 1);
    Raster::rect(x + 104, y + 54, 96, 4, c565(0x2A2A2A));
    Raster::rect(x + 104, y + 54, 96 * f, 4, c565(0x5A9AE8));
  }
}

void battleOverlay() {
  // type-coloured burst on a hit
  if (s_cur < s_nb && (s_phase == PH_BEATS || s_phase == PH_FORCE)) {
    const Beat &b = s_beat[s_cur];
    if (b.anim == AN_HIT && s_beatT < 0.4f) {
      int prev = s_cur > 0 ? s_beat[s_cur - 1].type : 0;
      float sx, sy, d;
      float x = b.who == 0 ? PX : FX, z = b.who == 0 ? PZ : FZ;
      if (g_cam.project(x, 0.7f, z, sx, sy, d)) {
        float r = 30 + s_beatT * 120;
        Raster::circle(sx, sy, r, c565(TYPE_COLS[prev]), (uint8_t)(160 * (1 - s_beatT / 0.4f)));
      }
    }
  }
  monPanel(1, 8, 8, false);
  monPanel(0, W - 216, 104, true);
  // the box along the bottom
  panel(6, 176, W - 12, 98, c565(0x1A1E26), 228, Col::BRASS);
  if ((s_phase == PH_BEATS || s_phase == PH_FORCE) && s_cur < s_nb) {
    const char *t = s_beat[s_cur].text;
    int n = (int)(s_beatT * 70);
    wrap(F_BODY, 18, 184, W - 36, t, Col::CREAM, Col::AMBER, 28, 3, n);
    return;
  }
  if (s_phase == PH_MENU) {
    char q[48];
    snprintf(q, sizeof(q), "What will %s do?", nm(0));
    wrap(F_BODY, 18, 186, 200, q, Col::CREAM, 0, 28, 3);
    static const char *const opts[4] = {"Fight", "Team", "Bag", "Run"};
    for (int i = 0; i < 4; i++) {
      int x = 232 + (i & 1) * 108, y = 186 + (i >> 1) * 42;
      bool sel = i == s_sel;
      if (sel) Raster::roundRect(x - 6, y - 3, 104, 36, 7, c565(0x3A3A2A));
      text(sel ? F_BOLD : F_BODY, x + 8, y + 2, opts[i], (i == 3 && !s_wild) ? Col::DIM : sel ? Col::AMBER : Col::CREAM);
    }
    return;
  }
  if (s_phase == PH_MOVES) {
    const Mon &m = act(0);
    for (int i = 0; i < 4; i++) {
      int x = 12 + (i & 1) * 220, y = 182 + (i >> 1) * 44;
      bool sel = i == s_moveSel;
      if (m.moves[i] == 255) { text(F_BODY, x + 8, y + 4, "-", Col::DIM); continue; }
      const MoveDef &mv = MOVES[m.moves[i]];
      Raster::roundRect(x, y, 212, 40, 7, sel ? c565(0x3A3A2A) : c565(0x262A32));
      Raster::roundRect(x + 5, y + 6, 6, 28, 3, c565(TYPE_COLS[mv.type]));
      fitText(sel ? F_BOLD : F_BODY, x + 16, y + 6, 156, mv.name, sel ? Col::AMBER : Col::CREAM);
      char pw[16];
      if (mv.power) snprintf(pw, sizeof(pw), "%d", mv.power); else snprintf(pw, sizeof(pw), "--");
      text(F_SMALL, x + 178, y + 10, pw, Col::DIM);
    }
    return;
  }
  if (s_phase == PH_TEAM || s_phase == PH_FORCE) {
    if (s_phase == PH_FORCE) text(F_SMALL, 18, 152, "Who comes out next?", Col::AMBER);
    int first = s_teamSel > 2 ? s_teamSel - 2 : 0;
    for (int r = 0; r < 3 && first + r < S[0].n; r++) {
      int i = first + r;
      const Mon &m = S[0].team[i];
      int y = 184 + r * 30;
      bool sel = i == s_teamSel;
      if (sel) Raster::roundRect(14, y - 2, W - 28, 28, 6, c565(0x3A3A2A));
      char line[64];
      snprintf(line, sizeof(line), "%s  Lv %d", SPECIES[m.sp].name, m.lvl);
      text(F_SMALL, 24, y + 1, line, m.hp == 0 ? Col::DIM : sel ? Col::AMBER : Col::CREAM);
      hpBar(256, y + 10, 120, m.hp / (float)maxHp(m));
      if (i == S[0].act) text(F_SMALL, 388, y + 1, "out", Col::DIM);
    }
    return;
  }
  if (s_phase == PH_BAG) {
    static const uint8_t usable[6] = {I_LANTERN, I_SILVER, I_MOON, I_TONIC, I_DRAUGHT, I_SALTS};
    int first = s_bagSel > 2 ? s_bagSel - 2 : 0;
    for (int r = 0; r < 3; r++) {
      int i = first + r;
      if (i >= 6) break;
      int it = usable[i];
      int y = 184 + r * 30;
      bool sel = i == s_bagSel;
      if (sel) Raster::roundRect(14, y - 2, W - 28, 28, 6, c565(0x3A3A2A));
      char line[48];
      snprintf(line, sizeof(line), "%s  x%d", ITEM_NAMES[it], g_items[it]);
      text(sel ? F_BOLD : F_BODY, 24, y - 1, line, g_items[it] ? (sel ? Col::AMBER : Col::CREAM) : Col::DIM);
    }
  }
}

#ifdef PC_SIM
// --- the playtest bot's hand in a battle ------------------------------------------------------
static int botBestMove() {
  Mon &m = act(0), &f = act(1);
  const Species &me = SPECIES[m.sp], &foe = SPECIES[f.sp];
  int best = 0; float bv = -1;
  for (int i = 0; i < 4; i++) {
    if (m.moves[i] == 255) continue;
    const MoveDef &mv = MOVES[m.moves[i]];
    float v = mv.power * typeMult(mv.type, foe.t1) * (foe.t2 == T_NONE ? 1.0f : typeMult(mv.type, foe.t2));
    if (mv.type == me.t1 || mv.type == me.t2) v *= 1.5f;
    v *= mv.acc / 100.0f;
    if (mv.power == 0) v = 0.1f;
    if (v > bv) { bv = v; best = i; }
  }
  return best;
}
Btn battleBotKey() {
  static bool dbg = getenv("GGBATTLE") != nullptr;
  static int lastCur = -1;
  if (dbg && s_phase == PH_BEATS && s_cur != lastCur && s_cur < s_nb) { lastCur = s_cur; printf("   ~ %s%c", s_beat[s_cur].text, 10); }
  if (s_phase == PH_BEATS) return B_OK;
  if (s_phase == PH_FORCE) {
    if (s_cur < s_nb) return B_OK;
    if (S[0].team[s_teamSel].hp > 0 && s_teamSel != S[0].act) return B_OK;
    return B_DOWN;
  }
  Mon &m = act(0), &f = act(1);
  int mx = maxHp(m), fmx = maxHp(f);
  // what to do this turn
  int want = 0;        // 0 fight, 2 bag
  int item = -1;       // index in the battle bag
  bool newKind = true;
  for (int i = 0; i < g_nParty; i++) newKind = newKind && g_party[i].sp != f.sp;
  if (s_wild && g_nParty < 3 && newKind && f.hp * 2 < fmx && (g_items[I_LANTERN] || g_items[I_SILVER])) { want = 2; item = g_items[I_SILVER] ? 1 : 0; }
  else if (m.hp * 4 < mx && (g_items[I_DRAUGHT] || g_items[I_TONIC])) { want = 2; item = g_items[I_DRAUGHT] ? 4 : 3; }
  if (s_phase == PH_MENU) {
    if (s_sel != want) return (s_sel ^ want) & 1 ? B_LEFT : B_UP;
    return B_OK;
  }
  if (s_phase == PH_BAG) {
    if (want != 2) return B_DOWN;
    if (s_bagSel != item) return B_RIGHT;
    return B_OK;
  }
  if (s_phase == PH_MOVES) {
    if (want == 2) return B_DOWN;
    int b = botBestMove();
    if (s_moveSel != b) return (s_moveSel ^ b) & 1 ? B_LEFT : B_UP;
    return B_OK;
  }
  return B_DOWN;
}
#endif

} // namespace Gaslight

namespace Gaslight {
void battleDebug() {
  printf("  battle phase %d beat %d/%d over %d won %d  hp %d/%d  act %d/%d  sel %d move %d team %d text '%s'\n", s_phase, s_cur, s_nb,
         s_over, s_won, S[0].team ? act(0).hp : -1, S[1].team ? act(1).hp : -1, S[0].act, S[1].act, s_sel, s_moveSel, s_teamSel,
         s_cur < s_nb ? s_beat[s_cur].text : "");
}
}
