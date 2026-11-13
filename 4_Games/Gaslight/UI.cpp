// Gaslight & Griffins - menus and the detective's tools: the casebook, the
// deduction board, interrogation (the weighing), accusation, team and bag.
#pragma GCC optimize("O2")
#include "Game.h"
#include "Raster.h"
#include "RunAudio.h"
#include <stdio.h>

namespace Gaslight {

int clueIndex(const char *id) {
  if (!id) return -1;
  int n = 0;
  while (id[n] && id[n] != ' ' && id[n] != ']' && id[n] != '\n') n++;
  for (int i = 0; i < N_CLUES; i++)
    if ((int)strlen(CLUES[i].id) == n && !strncmp(CLUES[i].id, id, n)) return i;
  return -1;
}
bool hasClue(int i) {
  if (i < 0) return false;
  char key[48];
  snprintf(key, sizeof(key), "clue:%s", CLUES[i].id);
  return flag(key);
}
static bool hasDeduction(const char *id) {
  char key[48];
  snprintf(key, sizeof(key), "ded:%s", id);
  return flag(key);
}

namespace UI {

static Panel s_p = P_NONE;
static bool s_fromScript = false, s_fromPause = false;
static int s_sel = 0, s_first = 0, s_pick = -1, s_step = 0;
static int s_sub = 0, s_subSel = 0;
static char s_arg[32];
static float s_t = 0;
static char s_toast[96];
static float s_toastT = 0;
static char s_note[160];
static float s_noteT = 0;
static int s_list[64];
static int s_nList = 0;
static int s_ans[4];

void toast(const char *msg) {
  snprintf(s_toast, sizeof(s_toast), "%s", msg);
  s_toastT = 2.6f;
}

static void note(const char *msg) {
  snprintf(s_note, sizeof(s_note), "%s", msg);
  s_noteT = 3.0f;
}

bool active() { return s_p != P_NONE; }
Panel current() { return s_p; }

static void buildClueList(int caseNo) {
  s_nList = 0;
  for (int i = 0; i < N_CLUES && s_nList < 64; i++)
    if (hasClue(i) && (caseNo < 0 || CLUES[i].caseNo == caseNo)) s_list[s_nList++] = i;
}

static const AccuseDef *accuseDef() {
  for (int i = 0; i < N_ACCUSES; i++) if (!strcmp(ACCUSES[i].id, s_arg)) return &ACCUSES[i];
  return nullptr;
}
static const Testimony *testimony() {
  for (int i = 0; i < N_TESTIMONIES; i++) if (!strcmp(TESTIMONIES[i].id, s_arg)) return &TESTIMONIES[i];
  return nullptr;
}
static bool broken(const Testimony *t, int i) {
  char key[48];
  snprintf(key, sizeof(key), "tb:%s:%d", t->id, i);
  return flag(key);
}

void open(Panel p, const char *arg) {
  s_fromScript = Script::active();
  s_fromPause = false;
  s_p = p;
  s_sel = 0; s_first = 0; s_pick = -1; s_step = 0; s_sub = 0; s_subSel = 0; s_t = 0;
  snprintf(s_arg, sizeof(s_arg), "%s", arg ? arg : "");
  if (p == P_CASEBOOK || p == P_BOARD) buildClueList(-1);
  sfx(RunAudio::Pop);
}

static void close(const char *label = nullptr) {
  s_p = P_NONE;
  if (s_fromScript || label) Script::resume(label);
}

void update(float dt) {
  s_t += dt;
  if (s_toastT > 0) s_toastT -= dt;
  if (s_noteT > 0) s_noteT -= dt;
}

// --- keys --------------------------------------------------------------------------------------------

static void moveSel(int d, int n, int rows) {
  if (n <= 0) return;
  s_sel = (s_sel + d + n) % n;
  if (s_sel < s_first) s_first = s_sel;
  if (s_sel >= s_first + rows) s_first = s_sel - rows + 1;
}

static const char *const PAUSE_OPTS[] = {"Team", "Casebook", "Deduction board", "Bag", "Detective", "Camera distance", "Save", "Back to the title"};
static const int N_PAUSE = 8;

static void boardTry(int a, int b) {
  const char *ia = CLUES[a].id, *ib = CLUES[b].id;
  for (int i = 0; i < N_LINKS; i++) {
    const LinkDef &l = LINKS[i];
    if ((!strcmp(l.a, ia) && !strcmp(l.b, ib)) || (!strcmp(l.a, ib) && !strcmp(l.b, ia))) {
      if (hasDeduction(l.deduction)) { note("You've already made that connection."); return; }
      char key[48];
      snprintf(key, sizeof(key), "ded:%s", l.deduction);
      setFlag(key);
      setVar("deductions", getVar("deductions") + 1);
      char ck[16];
      snprintf(ck, sizeof(ck), "ded_c%d", CLUES[a].caseNo);
      setVar(ck, getVar(ck) + 1);
      note(l.text);
      sfx(RunAudio::DingTop);
      g_flash = 0.3f;
      return;
    }
  }
  setVar("composure", clampi(getVar("composure") - 1, 0, 6));
  note("The string won't stretch between those. (Composure -1)");
  sfx(RunAudio::Bonk);
}

static const Btn NEXT = B_RIGHT;

// Down is back, everywhere: out of a sub-step, then out of the panel.
static void back() {
  switch (s_p) {
    case P_BOARD: if (s_pick >= 0) { s_pick = -1; return; } break;
    case P_ACCUSE: if (s_step > 0) { s_step--; s_sel = 0; s_first = 0; return; } break;
    case P_INTERRO: if (s_sub) { s_sub = 0; return; } break;
    default: break;
  }
  bool fromPause = s_fromPause;
  close();
  if (fromPause) { open(P_PAUSE); s_fromScript = false; }
}

void key(Btn b) {
  if (s_noteT > 0 && b == B_OK && s_p != P_BOARD) { s_noteT = 0; return; }
  if (b == B_DOWN) { sfx(RunAudio::Tick); back(); return; }
  switch (s_p) {
    case P_PAUSE:
      if (b == NEXT && false) {}
      if (b == B_UP) moveSel(-1, N_PAUSE, 8);
      else if (b == NEXT) moveSel(1, N_PAUSE, 8);
      else if (b == B_OK) {
        switch (s_sel) {
          case 0: open(P_TEAM); s_fromScript = false; s_fromPause = true; break;
          case 1: open(P_CASEBOOK); s_fromScript = false; s_fromPause = true; break;
          case 2: open(P_BOARD); s_fromScript = false; s_fromPause = true; break;
          case 3: open(P_BAG); s_fromScript = false; s_fromPause = true; break;
          case 4: open(P_SKILLS); s_fromScript = false; s_fromPause = true; break;
          case 5: worldCameraToggle(); close(); break;
          case 6: toast(saveGame() ? "The case is saved." : "Couldn't save!"); close(); break;
          case 7: s_p = P_NONE; g_mode = Mode::Title; g_fade = 1; break;
        }
      }
      break;
    case P_TEAM:
      if (b == B_UP) moveSel(-1, g_nParty, 6);
      else if (b == NEXT) moveSel(1, g_nParty, 6);
      else if (b == B_OK && s_sel > 0) {
        Mon t = g_party[0]; g_party[0] = g_party[s_sel]; g_party[s_sel] = t;
        toast("A new partner walks beside you.");
        s_sel = 0;
      }
      break;
    case P_BAG: {
      if (b == B_UP) moveSel(-1, I_COUNT, 7);
      else if (b == NEXT) moveSel(1, I_COUNT, 7);
      else if (b == B_OK && g_items[s_sel] && g_nParty) {
        int it = s_sel;
        if (it == I_TONIC || it == I_DRAUGHT || it == I_SALTS) {
          for (int i = 0; i < g_nParty; i++) {
            Mon &m = g_party[i];
            int mx = statsOf(m).hp;
            bool fainted = m.hp == 0;
            if (it == I_SALTS ? fainted : (!fainted && m.hp < mx)) {
              if (it == I_TONIC) m.hp = (uint16_t)(m.hp + 30 > mx ? mx : m.hp + 30);
              if (it == I_DRAUGHT) { m.hp = (uint16_t)mx; m.status = ST_OK; }
              if (it == I_SALTS) m.hp = (uint16_t)(mx / 2);
              g_items[it]--;
              char msg[64];
              snprintf(msg, sizeof(msg), "%s feels better.", SPECIES[m.sp].name);
              toast(msg);
              sfx(RunAudio::Ding);
              break;
            }
          }
        }
      }
      break;
    }
    case P_SKILLS:
      if (b == NEXT || b == B_LEFT || b == B_OK) close();
      break;
    case P_CASEBOOK:
      if (b == B_UP) moveSel(-1, s_nList, 7);
      else if (b == NEXT) moveSel(1, s_nList, 7);
      else if (b == B_LEFT || b == B_OK) { if (b == B_LEFT) close(); }
      break;
    case P_BOARD: {
      int cols = 3;
      if (b == B_LEFT) { if (s_sel % cols == 0) { if (s_pick >= 0) s_pick = -1; else close(); } else moveSel(-1, s_nList, 99); }
      else if (b == B_RIGHT) moveSel(1, s_nList, 99);
      else if (b == B_UP) { if (s_sel >= cols) s_sel -= cols; }
      else if (b == NEXT) { if (s_sel + cols < s_nList) s_sel += cols; else if (s_pick >= 0) s_pick = -1; else close(); }
      else if (b == B_OK && s_nList) {
        if (s_noteT > 0) { s_noteT = 0; return; }
        if (s_pick < 0) { s_pick = s_sel; sfx(RunAudio::Tick); }
        else if (s_pick == s_sel) s_pick = -1;
        else { boardTry(s_list[s_pick], s_list[s_sel]); s_pick = -1; }
      }
      break;
    }
    case P_ACCUSE: {
      const AccuseDef *a = accuseDef();
      if (!a) { close(); return; }
      int n = s_step == 0 ? a->nWho : s_step == 1 ? a->nWhy : s_step == 2 ? a->nHow : s_nList;
      if (b == B_UP) moveSel(-1, n, 5);
      else if (b == NEXT) moveSel(1, n, 5);

      else if (b == B_OK && n) {
        s_ans[s_step] = s_step == 3 ? s_list[s_sel] : s_sel;
        s_step++;
        s_sel = 0; s_first = 0;
        if (s_step == 3) buildClueList(-1);
        if (s_step == 4) {
          bool who = s_ans[0] == a->rightWho, why = s_ans[1] == a->rightWhy, how = s_ans[2] == a->rightHow,
               proof = !strcmp(CLUES[s_ans[3]].id, a->proof);
          char key[48];
          snprintf(key, sizeof(key), "accused_%s", a->id);
          setVar(key, s_ans[0] + 1);
          if (!who) { setVar("wrong_accusations", getVar("wrong_accusations") + 1); close(a->wrongLabel[s_ans[0]]); }
          else if (!(why && how && proof)) close(a->wrongLabel[a->rightWho]);
          else close(a->rightLabel);
        }
      }
      break;
    }
    case P_INTERRO: {
      const Testimony *t = testimony();
      if (!t) { close(); return; }
      if (s_sub == 0) {
        if (b == B_UP) moveSel(-1, t->n, 4);
        else if (b == NEXT) moveSel(1, t->n, 4);
        else if (b == B_OK) {
          if (broken(t, s_sel)) { note("That one's already come apart."); return; }
          s_sub = 1; buildClueList(-1); s_subSel = 0;
        }
      } else {
        if (b == B_UP) s_subSel = (s_subSel + s_nList - 1) % (s_nList ? s_nList : 1);
        else if (b == NEXT) s_subSel = (s_subSel + 1) % (s_nList ? s_nList : 1);
        else if (b == B_OK && s_nList) {
          const Statement &st = t->s[s_sel];
          if (!strcmp(CLUES[s_list[s_subSel]].id, st.breaker)) {
            char key[48];
            snprintf(key, sizeof(key), "tb:%s:%d", t->id, s_sel);
            setFlag(key);
            setVar("lies_caught", getVar("lies_caught") + 1);
            sfx(RunAudio::DingTop);
            g_flash = 0.3f;
            bool all = true;
            for (int i = 0; i < t->n; i++) all = all && broken(t, i);
            close(all && t->onDone ? t->onDone : st.onBreak);
          } else {
            setVar("composure", clampi(getVar("composure") - 1, 0, 6));
            note("That proves nothing here. They watch you fumble. (Composure -1)");
            sfx(RunAudio::Bonk);
            s_sub = 0;
          }
        }
      }
      break;
    }
    default: break;
  }
}

void wheel(int d) {
  switch (s_p) {
    case P_PAUSE: moveSel(d, N_PAUSE, 8); break;
    case P_TEAM: moveSel(d, g_nParty, 6); break;
    case P_BAG: moveSel(d, I_COUNT, 7); break;
    case P_CASEBOOK: moveSel(d, s_nList, 7); break;
    case P_BOARD: moveSel(d, s_nList, 99); break;
    case P_ACCUSE: {
      const AccuseDef *a = accuseDef();
      if (!a) break;
      int n = s_step == 0 ? a->nWho : s_step == 1 ? a->nWhy : s_step == 2 ? a->nHow : s_nList;
      moveSel(d, n, 5);
      break;
    }
    case P_INTERRO: {
      const Testimony *t = testimony();
      if (!t) break;
      if (s_sub == 0) moveSel(d, t->n, 4);
      else if (s_nList) s_subSel = (s_subSel + (d > 0 ? 1 : s_nList - 1)) % s_nList;
      break;
    }
    default: break;
  }
}

// --- drawing --------------------------------------------------------------------------------------------------
// Laid out for the large type: body 27 px, small 22 px a line.

static void backdrop() { fadeRect(0, 0, W, H, c565(0x0A0C10), 170); }

static void title(const char *t) {
  panel(8, 6, W - 16, 36, c565(0x161A22), 240, Col::BRASS);
  text(F_BOLD, 20, 10, t, Col::AMBER);
}

// Text cut short with "..." if it won't fit in maxW.
static void fit(const FontInfo &f, int x, int y, int maxW, const char *s, uint16_t col) {
  if (textW(f, s) <= maxW) { text(f, x, y, s, col); return; }
  char buf[80];
  int n = (int)strlen(s);
  if (n > 76) n = 76;
  while (n > 0) {
    snprintf(buf, sizeof(buf), "%.*s...", n, s);
    if (textW(f, buf) <= maxW) break;
    n--;
  }
  text(f, x, y, buf, col);
}

static void hpBar(float x, float y, float w, float frac) {
  Raster::roundRect(x, y, w, 7, 3, c565(0x1A1A1A));
  uint16_t c = frac > 0.5f ? c565(0x5AC85A) : frac > 0.2f ? c565(0xE8C03A) : c565(0xE04A3A);
  if (frac > 0) Raster::roundRect(x + 1, y + 1, (w - 2) * clampf(frac, 0, 1), 5, 2.5f, c);
}

static void drawPause() {
  int x = W - 236, y = 6, h = 12 + N_PAUSE * 32;
  panel(x, y, 228, h, c565(0x161A22), 236, Col::BRASS);
  for (int i = 0; i < N_PAUSE; i++) {
    bool sel = i == s_sel;
    if (sel) Raster::roundRect(x + 6, y + 6 + i * 32, 216, 30, 6, c565(0x34302A));
    fit(sel ? F_BOLD : F_BODY, x + 14, y + 8 + i * 32, 204, PAUSE_OPTS[i], sel ? Col::AMBER : Col::CREAM);
  }
  // a few facts down the left
  panel(8, 46, 200, 124, c565(0x161A22), 220, Col::BRASS);
  char line[48];
  fit(F_BOLD, 18, 52, 182, AREAS[g_area].name, Col::CREAM);
  snprintf(line, sizeof(line), "Shillings  %d", g_money);
  text(F_SMALL, 18, 84, line, Col::DIM);
  snprintf(line, sizeof(line), "Composure  %d", getVar("composure"));
  text(F_SMALL, 18, 106, line, getVar("composure") <= 1 ? Col::BAD : Col::DIM);
  snprintf(line, sizeof(line), "Favours  %d", getVar("favours"));
  text(F_SMALL, 18, 128, line, Col::DIM);
  snprintf(line, sizeof(line), "Pages  %d of 20", getVar("fragments"));
  text(F_SMALL, 18, 148, line, Col::DIM);
}

static void drawTeam() {
  backdrop();
  title("Your team");
  if (!g_nParty) { textC(F_BODY, W / 2, 120, "No creatures yet.", Col::DIM); return; }
  for (int i = 0; i < g_nParty; i++) {
    const Mon &m = g_party[i];
    int y = 46 + i * 38;
    bool sel = i == s_sel;
    panel(8, y, 198, 36, sel ? c565(0x34302A) : c565(0x161A22), 230, sel ? Col::AMBER : c565(0x4A4030));
    drawMon2D(m.sp, 26, y + 18, 26, m.seed);
    fit(F_SMALL, 44, y + 2, 110, SPECIES[m.sp].name, m.hp ? (sel ? Col::AMBER : Col::CREAM) : Col::DIM);
    char line[16];
    snprintf(line, sizeof(line), "Lv%d", m.lvl);
    text(F_SMALL, 160, y + 2, line, Col::DIM);
    hpBar(44, y + 26, 154, m.hp / (float)statsOf(m).hp);
  }
  // the one picked
  const Mon &m = g_party[s_sel];
  const Species &sp = SPECIES[m.sp];
  panel(212, 46, W - 220, 228, c565(0x161A22), 236, Col::BRASS);
  drawMon2D(m.sp, 246, 86, 54, m.seed);
  fit(F_BOLD, 284, 50, W - 296, sp.name, Col::CREAM);
  int tx = 284;
  for (int k = 0; k < 2; k++) {
    uint8_t t = k ? sp.t2 : sp.t1;
    if (t == T_NONE) continue;
    int w = textW(F_SMALL, TYPE_NAMES[t]) + 12;
    Raster::roundRect(tx, 80, w, 22, 6, c565(TYPE_COLS[t]));
    text(F_SMALL, tx + 6, 80, TYPE_NAMES[t], 0x0000);
    tx += w + 4;
  }
  MonStats st = statsOf(m);
  char line[64];
  snprintf(line, sizeof(line), "HP %d/%d   Lv %d", m.hp, st.hp, m.lvl);
  text(F_SMALL, 222, 112, line, Col::DIM);
  snprintf(line, sizeof(line), "Atk %d  Def %d  Spd %d", st.atk, st.def, st.spd);
  text(F_SMALL, 222, 134, line, Col::DIM);
  wrap(F_SMALL, 222, 158, W - 240, sp.dex, c565(0xC8C0B0), 0, 21, 2);
  for (int i = 0; i < 4; i++) {
    if (m.moves[i] == 255) continue;
    const MoveDef &mv = MOVES[m.moves[i]];
    int x = 218 + (i & 1) * 116, y = 204 + (i >> 1) * 33;
    Raster::roundRect(x, y, 112, 30, 6, c565(0x262A32));
    Raster::rect(x + 4, y + 5, 4, 20, c565(TYPE_COLS[mv.type]));
    fit(F_SMALL, x + 12, y + 4, 96, mv.name, Col::CREAM);
  }
}

static void drawBag() {
  backdrop();
  title("Bag");
  for (int i = 0; i < I_COUNT; i++) {
    int y = 48 + i * 32;
    bool sel = i == s_sel;
    if (sel) Raster::roundRect(8, y - 2, 222, 30, 6, c565(0x34302A));
    fit(sel ? F_BOLD : F_BODY, 16, y, 166, ITEM_NAMES[i], g_items[i] ? (sel ? Col::AMBER : Col::CREAM) : Col::DIM);
    char line[12];
    snprintf(line, sizeof(line), "x%d", g_items[i]);
    text(F_SMALL, 190, y + 4, line, Col::DIM);
  }
  panel(236, 46, W - 244, 170, c565(0x161A22), 236, Col::BRASS);
  wrap(F_BODY, 246, 54, W - 266, ITEM_DESC[s_sel], Col::CREAM, 0, 28, 5);
  char m[40];
  snprintf(m, sizeof(m), "Shillings: %d", g_money);
  text(F_BODY, 244, 236, m, Col::BRASS);
}

static void drawSkills() {
  backdrop();
  title("The detective");
  static const uint16_t SKC[SK_COUNT] = {0x6DDF, 0xFEA0, 0xA53F, 0xFB2C, 0xBC27, 0x4E68};
  for (int i = 0; i < SK_COUNT; i++) {
    int y = 50 + i * 32;
    text(F_SMALL, 14, y + 2, SKILL_NAMES[i], SKC[i]);
    int v = skill(i);
    for (int k = 0; k < 8; k++) Raster::roundRect(128 + k * 15, y + 6, 12, 14, 3, k < v ? SKC[i] : c565(0x2A2A2A));
  }
  char line[64];
  snprintf(line, sizeof(line), "Composure %d   Deductions %d", getVar("composure"), getVar("deductions"));
  text(F_SMALL, 14, 248, line, Col::DIM);
  panel(256, 46, W - 264, 196, c565(0x161A22), 230, Col::BRASS);
  text(F_BOLD, 266, 50, "Favours", Col::AMBER);
  static const char *const FAV[][2] = {{"sirens", "The Sisters"}, {"cyclops", "Old One-Eye"}, {"aeolus", "Aeolus"},
                                       {"sphinx", "The Sphinx"}, {"feather", "The Feather"}, {"knight", "Green Knight"},
                                       {"ravens", "Muninn"}, {"siduri", "Siduri"}};
  int y = 80;
  for (auto &f : FAV) {
    char key[24];
    snprintf(key, sizeof(key), "fav_%s", f[0]);
    fit(F_SMALL, 268, y, W - 284, f[1], flag(key) ? Col::CREAM : c565(0x4A4A4A));
    y += 20;
  }
}

static void clueCard(float x, float y, float w, float h, int ci, bool sel, bool picked) {
  uint16_t paper = picked ? c565(0xF8E8B0) : c565(0xEDE2C8);
  Raster::rect(x + 3, y + 3, w, h, 0x0000, 90);
  Raster::rect(x, y, w, h, paper);
  if (sel) { Raster::rect(x - 2, y - 2, w + 4, 2, Col::AMBER); Raster::rect(x - 2, y + h, w + 4, 2, Col::AMBER);
             Raster::rect(x - 2, y, 2, h, Col::AMBER); Raster::rect(x + w, y, 2, h, Col::AMBER); }
  Raster::circle(x + w / 2, y + 4, 4, c565(0xB03030));
  wrap(F_SMALL, (int)x + 6, (int)y + 9, (int)w - 12, CLUES[ci].title, c565(0x2A2018), 0, 20, 2);
}

static void drawBoard() {
  // cork
  Raster::rect(0, 0, W, H, c565(0x8A6A44));
  for (int i = 0; i < 40; i++) Raster::circle(hashf(i, 1) * W, hashf(i, 2) * H, 2 + hashf(i, 3) * 3, c565(0x7A5A38), 120);
  title("Deduction board");
  if (!s_nList) { textC(F_BODY, W / 2, 130, "No clues pinned yet.", Col::CREAM); return; }
  int cols = 3, cw = 136, ch = 54, gx = 10, gy = 52;
  int row = s_sel / cols, firstRow = row > 1 ? row - 1 : 0;
  auto pos = [&](int k, float &x, float &y) {
    x = gx + (k % cols) * (cw + 12) + ((k / cols) & 1) * 4;
    y = gy + (k / cols - firstRow) * (ch + 16);
  };
  // red string between made deductions
  for (int i = 0; i < N_LINKS; i++) {
    if (!hasDeduction(LINKS[i].deduction)) continue;
    int a = -1, b = -1;
    for (int k = 0; k < s_nList; k++) {
      if (!strcmp(CLUES[s_list[k]].id, LINKS[i].a)) a = k;
      if (!strcmp(CLUES[s_list[k]].id, LINKS[i].b)) b = k;
    }
    if (a < 0 || b < 0) continue;
    float ax, ay, bx, by;
    pos(a, ax, ay); pos(b, bx, by);
    Raster::line(ax + cw / 2, ay + 4, bx + cw / 2, by + 4, 2, c565(0xC02020));
  }
  for (int k = 0; k < s_nList; k++) {
    float x, y;
    pos(k, x, y);
    if (y < 44 || y > 190) continue;
    clueCard(x, y, cw, ch, s_list[k], k == s_sel, k == s_pick);
  }
  if (s_pick >= 0) {
    float x, y;
    pos(s_pick, x, y);
    float sx, sy;
    pos(s_sel, sx, sy);
    Raster::line(x + cw / 2, y + 4, sx + cw / 2, sy + 4, 1.5f, c565(0xE04040), 160);
  }
  if (s_noteT > 0) {
    panel(8, 192, W - 16, 82, c565(0x161A22), 240, Col::AMBER);
    wrap(F_SMALL, 18, 198, W - 36, s_note, Col::CREAM, Col::AMBER, 22, 3);
  } else {
    panel(8, 200, W - 16, 74, c565(0x161A22), 220, Col::BRASS);
    wrap(F_SMALL, 18, 205, W - 36, CLUES[s_list[s_sel]].text, Col::CREAM, Col::AMBER, 22, 3);
  }
}

static void drawCasebook() {
  backdrop();
  title("Casebook");
  if (!s_nList) { textC(F_BODY, W / 2, 120, "Nothing written down yet.", Col::DIM); return; }
  for (int r = 0; r < 7 && s_first + r < s_nList; r++) {
    int i = s_first + r, y = 48 + r * 32;
    bool sel = i == s_sel;
    if (sel) Raster::roundRect(6, y - 2, 182, 30, 6, c565(0x34302A));
    fit(F_SMALL, 12, y + 2, 172, CLUES[s_list[i]].title, sel ? Col::AMBER : Col::CREAM);
  }
  const ClueDef &c = CLUES[s_list[s_sel]];
  panel(192, 46, W - 198, 228, c565(0xEDE2C8), 250, c565(0x8A6A44));
  int lines = wrap(F_BOLD, 202, 50, W - 216, c.title, c565(0x2A2018), 0, 27, 2);
  wrap(F_SMALL, 202, 54 + lines * 27, W - 216, c.text, c565(0x3A2E24), c565(0x7A2430), 21, 9);
}

static void drawAccuse() {
  backdrop();
  const AccuseDef *a = accuseDef();
  if (!a) return;
  static const char *const steps[4] = {"Who did it?", "Why?", "How?", "What proves it?"};
  title("The accusation");
  text(F_BOLD, 16, 44, steps[s_step < 4 ? s_step : 3], Col::CREAM);
  const char *const *opts = s_step == 0 ? a->who : s_step == 1 ? a->why : a->how;
  int n = s_step == 0 ? a->nWho : s_step == 1 ? a->nWhy : s_step == 2 ? a->nHow : s_nList;
  for (int r = 0; r < 5 && s_first + r < n; r++) {
    int i = s_first + r, y = 76 + r * 36;
    bool sel = i == s_sel;
    panel(8, y, W - 16, 33, sel ? c565(0x34302A) : c565(0x161A22), 230, sel ? Col::AMBER : c565(0x4A4030));
    const char *t = s_step == 3 ? CLUES[s_list[i]].title : opts[i];
    if (textW(F_BODY, t) <= W - 40) text(sel ? F_BOLD : F_BODY, 18, y + 3, t, sel ? Col::AMBER : Col::CREAM);
    else fit(F_SMALL, 18, y + 6, W - 36, t, sel ? Col::AMBER : Col::CREAM);
  }
  text(F_SMALL, 16, 258, "Down: back a step", Col::DIM);
}

static void scale(float cx, float cy, int theirs, int yours) {
  float tilt = clampf((theirs - yours) * 0.12f, -0.4f, 0.4f);
  uint16_t g = c565(0xD8B060);
  Raster::rect(cx - 2, cy, 4, 36, g);
  poly3(cx - 12, cy + 40, cx + 12, cy + 40, cx, cy + 32, g);
  float lx = cx - 40 * cosf(tilt), ly = cy - 40 * sinf(tilt), rx = cx + 40 * cosf(tilt), ry = cy + 40 * sinf(tilt);
  Raster::line(lx, ly, rx, ry, 3, g);
  Raster::line(lx, ly, lx - 12, ly + 20, 1.5f, g); Raster::line(lx, ly, lx + 12, ly + 20, 1.5f, g);
  Raster::line(rx, ry, rx - 12, ry + 20, 1.5f, g); Raster::line(rx, ry, rx + 12, ry + 20, 1.5f, g);
  Raster::ellipse(lx, ly + 21, 15, 4, g);
  Raster::ellipse(rx, ry + 21, 15, 4, g);
  // the feather on one side, the weight of lies on the other
  Raster::ellipse(lx, ly + 13, 3, 8, c565(0xF0F0E0));
  for (int i = 0; i < theirs; i++) Raster::circle(rx - 8 + i * 8, ry + 15, 4, c565(0x8A2A2A));
}

static void drawInterro() {
  backdrop();
  const Testimony *t = testimony();
  if (!t) return;
  title("The weighing");
  drawPortrait(t->look, 6, 46, 108, 140, 2);
  fit(F_SMALL, 8, 188, 110, t->who, Col::AMBER);
  int nb = 0;
  for (int i = 0; i < t->n; i++) nb += broken(t, i);
  scale(60, 226, nb, 3 - getVar("composure") > 0 ? 3 - getVar("composure") : 0);
  if (s_sub == 0) {
    for (int i = 0; i < t->n; i++) {
      int y = 46 + i * 57;
      bool sel = i == s_sel, br = broken(t, i);
      panel(120, y, W - 126, 54, sel ? c565(0x34302A) : c565(0x161A22), 230, sel ? Col::AMBER : c565(0x4A4030));
      wrap(F_SMALL, 128, y + 4, W - 142, t->s[i].text, br ? Col::DIM : Col::CREAM, Col::AMBER, 22, 2);
      if (br) Raster::rect(128, y + 26, W - 142, 2, Col::BAD);
    }
  } else {
    panel(120, 46, W - 126, 54, c565(0x161A22), 230, Col::AMBER);
    wrap(F_SMALL, 128, 50, W - 142, t->s[s_sel].text, Col::CREAM, 0, 22, 2);
    int first = s_subSel > 4 ? s_subSel - 4 : 0;
    for (int r = 0; r < 5 && first + r < s_nList; r++) {
      int i = first + r, y = 106 + r * 34;
      bool sel = i == s_subSel;
      panel(120, y, W - 126, 31, sel ? c565(0x34302A) : c565(0x161A22), 230, sel ? Col::AMBER : c565(0x4A4030));
      fit(F_SMALL, 128, y + 4, W - 142, CLUES[s_list[i]].title, sel ? Col::AMBER : Col::CREAM);
    }
  }
  if (s_noteT > 0) {
    panel(30, 100, W - 60, 76, c565(0x161A22), 245, Col::BAD);
    wrap(F_SMALL, 42, 106, W - 84, s_note, Col::CREAM, 0, 22, 3);
  }
}

void draw() {
  switch (s_p) {
    case P_PAUSE: drawPause(); break;
    case P_TEAM: drawTeam(); break;
    case P_BAG: drawBag(); break;
    case P_SKILLS: drawSkills(); break;
    case P_CASEBOOK: drawCasebook(); break;
    case P_BOARD: drawBoard(); break;
    case P_ACCUSE: drawAccuse(); break;
    case P_INTERRO: drawInterro(); break;
    default: break;
  }
}

void drawToast() {
  if (s_toastT <= 0) return;
  float a = clampf(s_toastT * 3, 0, 1);
  const FontInfo &f = textW(F_BODY, s_toast) + 32 <= W - 16 ? F_BODY : F_SMALL;
  int w = textW(f, s_toast) + 32;
  if (w > W - 16) w = W - 16;
  panel(W / 2 - w / 2, 46, w, 36, c565(0x22303E), (uint8_t)(230 * a), Col::AMBER);
  textC(f, W / 2, &f == &F_BODY ? 50 : 53, s_toast, mix565(c565(0x22303E), Col::CREAM, (int)(256 * a)));
}

#ifdef PC_SIM
// --- the playtest bot's hand in the panels ------------------------------------------------------
static int s_botA = -1, s_botB = -1;
bool botLinkAvailable() {
  for (int l = 0; l < N_LINKS; l++) {
    if (hasDeduction(LINKS[l].deduction)) continue;
    int a = clueIndex(LINKS[l].a), b = clueIndex(LINKS[l].b);
    if (a >= 0 && b >= 0 && hasClue(a) && hasClue(b)) return true;
  }
  return false;
}
static Btn botMoveTo(int target, int cur) { return target > cur ? NEXT : B_UP; }
Btn botKey() {
  if (s_noteT > 0) { s_noteT = 0; return B_NONE; }
  switch (s_p) {
    case P_ACCUSE: {
      const AccuseDef *a = accuseDef();
      if (!a) return B_DOWN;
      int want = s_step == 0 ? a->rightWho : s_step == 1 ? a->rightWhy : s_step == 2 ? a->rightHow : -1;
      if (s_step == 3) {
        want = 0;
        for (int i = 0; i < s_nList; i++) if (!strcmp(CLUES[s_list[i]].id, a->proof)) want = i;
      }
      return s_sel == want ? B_OK : botMoveTo(want, s_sel);
    }
    case P_INTERRO: {
      const Testimony *t = testimony();
      if (!t) return B_DOWN;
      if (s_sub == 0) {
        for (int i = 0; i < t->n; i++) {
          if (broken(t, i)) continue;
          int ci = clueIndex(t->s[i].breaker);
          if (ci < 0 || !hasClue(ci)) continue;
          return s_sel == i ? B_OK : botMoveTo(i, s_sel);
        }
        return B_DOWN; // nothing we can break yet
      }
      for (int i = 0; i < s_nList; i++)
        if (!strcmp(CLUES[s_list[i]].id, t->s[s_sel].breaker)) return s_subSel == i ? B_OK : (i > s_subSel ? NEXT : B_UP);
      return B_DOWN;
    }
    case P_BOARD: {
      // the first link both of whose clues we hold and haven't made
      if (s_botA < 0) {
        for (int l = 0; l < N_LINKS && s_botA < 0; l++) {
          if (hasDeduction(LINKS[l].deduction)) continue;
          int a = -1, b = -1;
          for (int i = 0; i < s_nList; i++) {
            if (!strcmp(CLUES[s_list[i]].id, LINKS[l].a)) a = i;
            if (!strcmp(CLUES[s_list[i]].id, LINKS[l].b)) b = i;
          }
          if (a >= 0 && b >= 0) { s_botA = a; s_botB = b; }
        }
        if (s_botA < 0) return B_DOWN;
      }
      int want = s_pick < 0 ? s_botA : s_botB;
      if (s_sel == want) { if (s_pick >= 0) { s_botA = s_botB = -1; } return B_OK; }
      if (want > s_sel) return B_RIGHT;
      return s_sel % 3 == 0 ? B_UP : B_LEFT;
    }
    case P_PAUSE:
      // the board, when there's a connection waiting to be made
      if (!botLinkAvailable()) return B_DOWN;
      if (s_sel != 2) return s_sel < 2 ? NEXT : B_UP;
      return B_OK;
    default: return B_DOWN;
  }
}
Panel botPanel() { return s_p; }
#endif

} // namespace UI
} // namespace Gaslight
