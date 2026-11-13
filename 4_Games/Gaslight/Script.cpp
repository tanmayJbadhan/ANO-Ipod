// Gaslight & Griffins - runs the story script (Story.cpp), one line at a time.
//
//   #label                    a place to jump to
//   name: text                someone speaks ("-" narrates, CAPS is an inner voice)
//   name(2): text             ...with a mood for the portrait
//   > [cond] text #label      a choice (consecutive lines form one menu)
//   > [check obs 9] text #pass #fail   a choice that rolls 2d6 + skill against 9
//   ? cond #label             jump if cond holds (flag, !flag, var>=n, var<n, clue:id, item:bun>=1)
//   +flag  -flag              set / clear
//   @command args             everything else (see exec())
#pragma GCC optimize("O2")
#include <Arduino.h>
#include "Game.h"
#include "World.h"
#include "Raster.h"
#include "RunAudio.h"
#include <stdio.h>
#include <stdlib.h>

namespace Gaslight {
namespace Script {

enum State : uint8_t { SS_IDLE, SS_RUN, SS_SAY, SS_CHOICE, SS_CHECK, SS_WAIT, SS_WALK, SS_EXTERNAL, SS_CARD, SS_CHAPTER };
static State s_state = SS_IDLE;
static const char *s_pc = nullptr;
static float s_t = 0;

// what's on screen
static char s_who[24];
static int s_look = -1, s_mood = -1, s_voice = -1;
static char s_text[320];
static float s_shown = 0;
static int s_pageOff = 0; // where the page on screen starts in s_text
static int pageEnd();
static int s_len = 0;

static const int MAXOPT = 6;
struct Opt { char text[120]; char pass[32]; char fail[32]; int skill, dc; };
static Opt s_opt[MAXOPT];
static int s_nOpt = 0, s_optSel = 0, s_optFirst = 0;

static int s_ckSkill, s_ckDc, s_ckD1, s_ckD2;
static bool s_ckPass;
static char s_ckPassL[32], s_ckFailL[32];
static float s_waitT = 0;
static Actor *s_walkWho = nullptr;
static int s_walkDir = 0, s_walkLeft = 0;
static char s_chapter[96];

static const char *const SKILL_KEYS[SK_COUNT] = {"obs", "logic", "memory", "charm", "disguise", "lore"};
static const uint16_t SKILL_COL[SK_COUNT] = {0x6DDF, 0xFEA0, 0xA53F, 0xFB2C, 0xBC27, 0x4E68};

// --- labels ---------------------------------------------------------------------------------------

static const char *findLabel(const char *name) {
  int n = 0;
  while (name[n] && name[n] != ' ' && name[n] != '\n' && name[n] != '\r') n++;
  for (int part = 0; part < N_STORY_PARTS; part++) {
    const char *base = STORY_PARTS[part], *p = base;
    while ((p = strchr(p, '#')) != nullptr) {
      if ((p == base || p[-1] == '\n') && !strncmp(p + 1, name, n) &&
          (p[1 + n] == '\n' || p[1 + n] == ' ' || p[1 + n] == '\r' || p[1 + n] == 0))
        return p;
      p++;
    }
  }
  Serial.printf("Gaslight: no label '%.*s'\n", n, name);
  return nullptr;
}

static bool jump(const char *label) {
  if (label[0] == '#') label++;
  const char *p = findLabel(label);
  if (!p) { s_state = SS_IDLE; return false; }
  // land on the line after the label
  while (*p && *p != 10) p++;
  if (*p == 10) p++;
  s_pc = p;
  return true;
}

// --- conditions ---------------------------------------------------------------------------------------

static int itemByName(const char *s) {
  static const char *const keys[I_COUNT] = {"lantern", "silver", "moon", "tonic", "draught", "salts", "bun"};
  for (int i = 0; i < I_COUNT; i++)
    if (!strncmp(s, keys[i], strlen(keys[i]))) return i;
  return -1;
}

static bool cond(const char *c) {
  while (*c == ' ') c++;
  bool neg = false;
  if (*c == '!') { neg = true; c++; }
  bool r;
  if (!strncmp(c, "clue:", 5)) {
    r = hasClue(clueIndex(c + 5));
  } else if (!strncmp(c, "item:", 5)) {
    int it = itemByName(c + 5);
    const char *op = strpbrk(c, "<>=");
    int v = op ? atoi(op + (op[1] == '=' ? 2 : 1)) : 1;
    int have = it >= 0 ? g_items[it] : 0;
    r = op && *op == '<' ? have < v : have >= v;
  } else if (!strncmp(c, "party", 5)) {
    r = g_nParty > 0;
  } else {
    const char *op = strpbrk(c, "<>=");
    if (op) {
      char name[40];
      int n = (int)(op - c);
      if (n > 39) n = 39;
      memcpy(name, c, n);
      name[n] = 0;
      while (n > 0 && name[n - 1] == ' ') name[--n] = 0;
      int have;
      int sk = -1;
      for (int i = 0; i < SK_COUNT; i++) if (!strcmp(name, SKILL_KEYS[i])) sk = i;
      have = sk >= 0 ? skill(sk) : !strcmp(name, "money") ? g_money : !strcmp(name, "party_n") ? g_nParty : getVar(name);
      bool ge = op[0] == '>' && op[1] == '=', gt = op[0] == '>' && op[1] != '=', lt = op[0] == '<' && op[1] != '=',
           le = op[0] == '<' && op[1] == '=';
      int v = atoi(op + ((ge || le || op[0] == '=') && op[1] == '=' ? 2 : 1));
      r = ge ? have >= v : gt ? have > v : lt ? have < v : le ? have <= v : have == v;
    } else {
      char name[48];
      int n = 0;
      while (c[n] && c[n] != ' ' && c[n] != ']' && n < 47) { name[n] = c[n]; n++; }
      name[n] = 0;
      r = flag(name);
    }
  }
  return neg ? !r : r;
}

// --- speakers -----------------------------------------------------------------------------------------------

struct Speaker { const char *key; int look; const char *name; };
static const Speaker SPEAKERS[] = {
    {"wren", L_WREN, "Wren"}, {"vane", L_VANE, "Prof. Vane"}, {"collector", L_VANEMASK, "The Collector"},
    {"juno", L_JUNO, "Juno"}, {"marisol", L_MARISOL, "Marisol"}, {"silas", L_SILAS, "Silas"},
    {"ottilie", L_OTTILIE, "Ottilie Brack"}, {"crewe", L_CREWE, "Augustin Crewe"}, {"pip", L_PIP, "Pip"},
    {"ysolde", L_YSOLDE, "Grandmother Ysolde"}, {"halloran", L_HALLORAN, "Capt. Halloran"}, {"quill", L_QUILL, "Mrs. Quill"},
    {"dobbs", L_DOBBS, "Constable Dobbs"}, {"lune", L_LUNE, "Dr. Lune"}, {"sailor", L_SAILOR, "Sailor"},
    {"salt", L_SAILOR2, "Old Salt"}, {"fishwife", L_FISHWIFE, "Fishwife"}, {"urchin", L_URCHIN, "Urchin"},
    {"gent", L_GENT, "Gentleman"}, {"lady", L_LADY, "Lady"}, {"curator", L_CURATOR, "Masked Curator"},
    {"bram", L_BRAM, "Insp. Castellane"}, {"tam", L_TRAINER_A, "Climber Tam"}, {"noor", L_TRAINER_B, "Birder Noor"},
    {"fenn", L_TRAINER_C, "Scholar Fenn"}, {"sirens", -2, "The Sisters"}, {"cyclops", -3, "Old One-Eye"},
    {"aeolus", -4, "Aeolus"}, {"heavy", L_SAILOR2, "Dockside Heavy"}, {"morrow", L_SAILOR2, "Old Salt Morrow"},
    {"conductor", L_CONDUCTOR, "Conductor"}, {"innkeeper", L_INNKEEP_S, "Innkeeper"}, {"museum", L_ASSISTANT, "Museum guard"},
    {"kamal", L_KAMAL, "Kamal Idris"}, {"sefa", L_SEFA, "Sefa Okon"}, {"neferet", L_NEFERET, "Lady Neferet"},
    {"tomas", L_TOMAS, "Tomas Reyes"}, {"merchant", L_MERCHANT, "Merchant"}, {"tally", L_TALLY, "Tally"},
    {"imani", L_IMANI, "Imani Sato"}, {"sphinx", -5, "The Sphinx"}, {"moth", L_MOTH, "Moth"},
    {"bertram", L_BERTRAM, "Lord Bertram"}, {"rosalind", L_ROSALIND, "Lady Rosalind"}, {"morgana", L_MORGANA, "Morgana Fay"},
    {"gareth", L_GARETH, "Gareth Lyle"}, {"ivy", L_IVY, "Ser Ivy"}, {"knight", L_GREENKNIGHT, "The Green Knight"},
    {"sigga", L_SIGGA, "Sigga"}, {"asta", L_ASTA, "Asta Brun"}, {"eirik", L_EIRIK, "Eirik Hallsson"},
    {"hilde", L_HILDE, "Hilde Ravnsdottir"}, {"villager", L_VILLAGER, "Villager"}, {"muninn", -6, "Muninn"},
    {"huginn", -6, "Huginn"}, {"norns", -7, "The Norns"}, {"siduri", L_SIDURI, "Siduri"}, {"serpent", -8, "The Serpent"},
    {"bearer", -9, "The Feather-Bearer"},
};

static void speak(const char *key, int keyLen, int mood, const char *text) {
  s_look = -1; s_voice = -1; s_mood = mood;
  s_who[0] = 0;
  if (keyLen == 1 && key[0] == '-') {
    // narration
  } else if (key[0] >= 'A' && key[0] <= 'Z') {
    for (int i = 0; i < SK_COUNT; i++)
      if ((int)strlen(SKILL_VOICE[i]) == keyLen && !strncmp(key, SKILL_VOICE[i], keyLen)) s_voice = i;
    snprintf(s_who, sizeof(s_who), "%.*s", keyLen, key);
  } else {
    for (const Speaker &sp : SPEAKERS)
      if ((int)strlen(sp.key) == keyLen && !strncmp(key, sp.key, keyLen)) {
        s_look = sp.look;
        snprintf(s_who, sizeof(s_who), "%s", sp.name);
      }
    if (!s_who[0]) snprintf(s_who, sizeof(s_who), "%.*s", keyLen, key);
  }
  // fill in $p (the partner) as we copy
  int o = 0;
  for (const char *p = text; *p && o < (int)sizeof(s_text) - 24; p++) {
    if (p[0] == '$' && p[1] == 'p') {
      const char *n = g_nParty ? SPECIES[g_party[0].sp].name : "your partner";
      o += snprintf(s_text + o, sizeof(s_text) - o, "%s", n);
      p++;
      continue;
    }
    s_text[o++] = *p;
  }
  s_text[o] = 0;
  s_len = o;
  s_shown = 0;
  s_pageOff = 0;
  s_state = SS_SAY;
}

// --- executing lines ------------------------------------------------------------------------------------------

static int readLine(char *buf, int cap) {
  // copies the current line (trimmed) and moves the pc past it
  while (*s_pc == ' ' || *s_pc == '\t') s_pc++;
  int n = 0;
  while (s_pc[n] && s_pc[n] != '\n') n++;
  int m = n;
  while (m > 0 && (s_pc[m - 1] == ' ' || s_pc[m - 1] == '\r')) m--;
  if (m > cap - 1) m = cap - 1;
  memcpy(buf, s_pc, m);
  buf[m] = 0;
  s_pc += n;
  if (*s_pc == '\n') s_pc++;
  return m;
}

static const char *word(const char *&p, char *out, int cap) {
  while (*p == ' ') p++;
  int n = 0;
  while (*p && *p != ' ' && n < cap - 1) out[n++] = *p++;
  out[n] = 0;
  return out;
}

static int areaByName(const char *s) {
  static const char *const names[A_COUNT] = {"falls", "town", "cliffs", "quill", "tavern", "harbour", "crewe",
                                             "police", "lighthouse", "cave", "library", "lodge", "rock",
                                             "sand", "museum", "kamal", "sefa", "glass", "inn", "green", "hall", "hut",
                                             "ivy", "skjold", "skald", "runehut", "forge", "seacave", "ur", "ziggurat",
                                             "sanctum", "siduri", "wilds"};
  for (int i = 0; i < A_COUNT; i++) if (!strcmp(s, names[i])) return i;
  return atoi(s);
}
static int dirByName(const char *s) { return s[0] == 'N' ? F_N : s[0] == 'E' ? F_E : s[0] == 'S' ? F_S : F_W; }
static Actor *actorByName(const char *s) { return !strcmp(s, "player") ? &g_player : actorNamed(s); }

static void collectChoices(const char *first) {
  s_nOpt = 0;
  s_optSel = 0;
  s_optFirst = 0;
  char line[300];
  const char *p = first;
  for (;;) {
    // peek: is this line a choice?
    const char *q = p;
    while (*q == ' ' || *q == '\t') q++;
    if (*q != '>') break;
    s_pc = p;
    readLine(line, sizeof(line));
    p = s_pc;
    const char *t = line + 1;
    while (*t == ' ') t++;
    Opt o;
    memset(&o, 0, sizeof(o));
    o.skill = -1;
    bool show = true;
    if (*t == '[') {
      const char *e = strchr(t, ']');
      if (!e) continue;
      char c[64];
      int n = (int)(e - t - 1);
      if (n > 63) n = 63;
      memcpy(c, t + 1, n);
      c[n] = 0;
      if (!strncmp(c, "check ", 6)) {
        const char *cp = c + 6;
        char sk[16], dc[8];
        word(cp, sk, sizeof(sk));
        word(cp, dc, sizeof(dc));
        for (int i = 0; i < SK_COUNT; i++) if (!strcmp(sk, SKILL_KEYS[i])) o.skill = i;
        o.dc = atoi(dc);
      } else {
        show = cond(c);
      }
      t = e + 1;
      while (*t == ' ') t++;
    }
    // text then labels
    const char *h = strchr(t, '#');
    int tl = h ? (int)(h - t) : (int)strlen(t);
    while (tl > 0 && t[tl - 1] == ' ') tl--;
    snprintf(o.text, sizeof(o.text), "%.*s", tl, t);
    if (h) {
      const char *lp = h;
      char l1[32] = "", l2[32] = "";
      word(lp, l1, sizeof(l1));
      word(lp, l2, sizeof(l2));
      snprintf(o.pass, sizeof(o.pass), "%s", l1 + (l1[0] == '#'));
      snprintf(o.fail, sizeof(o.fail), "%s", l2[0] == '#' ? l2 + 1 : l2);
    }
    if (show && s_nOpt < MAXOPT) s_opt[s_nOpt++] = o;
  }
  s_pc = p;
  if (s_nOpt == 0) { s_state = SS_RUN; return; }
  s_state = SS_CHOICE;
}

static void startCheck(int sk, int dc, const char *pass, const char *fail) {
  s_ckSkill = sk;
  s_ckDc = dc;
  s_ckD1 = 1 + (int)(rnd() % 6);
  s_ckD2 = 1 + (int)(rnd() % 6);
  int bonus = getVar("composure") <= 0 ? -1 : 0;
  s_ckPass = s_ckD1 + s_ckD2 + skill(sk) + bonus >= dc || (s_ckD1 == 6 && s_ckD2 == 6);
  if (s_ckD1 == 1 && s_ckD2 == 1) s_ckPass = false;
  snprintf(s_ckPassL, sizeof(s_ckPassL), "%s", pass);
  snprintf(s_ckFailL, sizeof(s_ckFailL), "%s", fail);
  s_t = 0;
  s_state = SS_CHECK;
  sfx(RunAudio::Pop);
}

static void teach(int mv) {
  if (!g_nParty) return;
  Mon &m = g_party[0];
  for (int k = 0; k < 4; k++) if (m.moves[k] == mv) return;
  for (int k = 0; k < 4; k++) if (m.moves[k] == 255) { m.moves[k] = (uint8_t)mv; return; }
  int weak = 0;
  for (int k = 1; k < 4; k++) if (MOVES[m.moves[k]].power < MOVES[m.moves[weak]].power) weak = k;
  m.moves[weak] = (uint8_t)mv;
}

static int moveByName(const char *s) {
  for (int i = 0; i < N_MOVES; i++) {
    const char *n = MOVES[i].name;
    // compare ignoring spaces and case
    const char *a = n, *b = s;
    bool ok = true;
    while (*a || *b) {
      if (*a == ' ') { a++; continue; }
      if (*b == '_') { b++; continue; }
      char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
      if (ca != cb) { ok = false; break; }
      a++; b++;
    }
    if (ok) return i;
  }
  return -1;
}

static void exec(char *line) {
  char w[48];
  const char *p = line + 1;
  word(p, w, sizeof(w));
  char a1[48] = "", a2[48] = "", a3[48] = "", a4[48] = "";
  const char *rest = p;
  while (*rest == ' ') rest++;
  word(p, a1, sizeof(a1)); word(p, a2, sizeof(a2)); word(p, a3, sizeof(a3)); word(p, a4, sizeof(a4));
  if (!strcmp(w, "end")) { s_state = SS_IDLE; return; }
  if (!strcmp(w, "goto")) { jump(a1); return; }
  if (!strcmp(w, "set")) { setVar(a1, atoi(a2)); return; }
  if (!strcmp(w, "add")) { setVar(a1, getVar(a1) + atoi(a2)); return; }
  if (!strcmp(w, "clue")) {
    int i = clueIndex(a1);
    if (i >= 0 && !hasClue(i)) {
      char key[48];
      snprintf(key, sizeof(key), "clue:%s", a1);
      setFlag(key);
      char msg[80];
      snprintf(msg, sizeof(msg), "Casebook: %s", CLUES[i].title);
      UI::toast(msg);
      sfx(RunAudio::DingHigh);
    }
    return;
  }
  if (!strcmp(w, "check")) {
    int sk = 0;
    for (int i = 0; i < SK_COUNT; i++) if (!strcmp(a1, SKILL_KEYS[i])) sk = i;
    startCheck(sk, atoi(a2), a3[0] == '#' ? a3 + 1 : a3, a4[0] == '#' ? a4 + 1 : a4);
    return;
  }
  if (!strcmp(w, "battle")) {
    s_state = SS_EXTERNAL;
    static char win[32], lose[32];
    snprintf(win, sizeof(win), "%s", a2[0] == '#' ? a2 + 1 : a2);
    snprintf(lose, sizeof(lose), "%s", a3[0] == '#' ? a3 + 1 : a3);
    battleStart(atoi(a1), win[0] ? win : nullptr, lose[0] ? lose : nullptr);
    return;
  }
  if (!strcmp(w, "wilds")) { wildsEnter(); worldLoad(A_WILDS, wildsStartX(), wildsStartZ(), F_N); g_fade = 1; return; }
  if (!strcmp(w, "wildloot")) { wildsLoot(); sfx(RunAudio::DingTop); return; }
  if (!strcmp(w, "wildboss")) { s_state = SS_IDLE; wildsBoss(); return; }
  if (!strcmp(w, "wild")) { s_state = SS_IDLE; battleWild(atoi(a1), atoi(a2)); return; }
  if (!strcmp(w, "warp")) { worldLoad(areaByName(a1), atoi(a2), atoi(a3), dirByName(a4)); g_fade = 1; return; }
  if (!strcmp(w, "walk") || !strcmp(w, "face")) {
    Actor *a = actorByName(a1);
    if (!a) return;
    a->face = (uint8_t)dirByName(a2);
    if (w[0] == 'f') return;
    s_walkWho = a; s_walkDir = a->face; s_walkLeft = a3[0] ? atoi(a3) : 1;
    s_state = SS_WALK;
    return;
  }
  if (!strcmp(w, "wait")) { s_waitT = (float)atof(a1); s_state = SS_WAIT; return; }
  if (!strcmp(w, "fade")) { if (a1[0] == 'o') { setVar("hold_fade", 1); g_fade = 1; } else { setVar("hold_fade", 0); } return; }
  if (!strcmp(w, "flash")) { g_flash = 1; return; }
  if (!strcmp(w, "shake")) { g_shake = 1; sfx(RunAudio::Thunder); return; }
  if (!strcmp(w, "card")) {
    setVar("card_now", atoi(a1));
    g_cardT0 = g_time;
    setVar("card_ret", (int)g_mode);
    g_mode = Mode::Card;
    s_state = SS_CARD;
    s_t = 0;
    return;
  }
  if (!strcmp(w, "give")) {
    int it = itemByName(a1);
    int n = a2[0] ? atoi(a2) : 1;
    if (it >= 0) {
      g_items[it] = (uint8_t)clampi(g_items[it] + n, 0, 99);
      char msg[64];
      snprintf(msg, sizeof(msg), n >= 0 ? "Received %s x%d" : "Gave %s", ITEM_NAMES[it], n);
      UI::toast(msg);
    }
    return;
  }
  if (!strcmp(w, "money")) { g_money += atoi(a1); return; }
  if (!strcmp(w, "mon")) {
    if (g_nParty < PARTY) {
      monInit(g_party[g_nParty], atoi(a1), atoi(a2), rnd());
      if (g_nParty == 0) setVar("starter", atoi(a1));
      g_nParty++;
    }
    return;
  }
  if (!strcmp(w, "heal")) {
    for (int i = 0; i < g_nParty; i++) { g_party[i].hp = (uint16_t)statsOf(g_party[i]).hp; g_party[i].status = ST_OK; }
    setVar("composure", 3 + getVar("composure_bonus"));
    return;
  }
  if (!strcmp(w, "board")) { s_state = SS_EXTERNAL; UI::open(UI::P_BOARD, a1[0] ? a1 : nullptr); return; }
  if (!strcmp(w, "casebook")) { s_state = SS_EXTERNAL; UI::open(UI::P_CASEBOOK); return; }
  if (!strcmp(w, "accuse")) { s_state = SS_EXTERNAL; UI::open(UI::P_ACCUSE, a1); return; }
  if (!strcmp(w, "interro")) { s_state = SS_EXTERNAL; UI::open(UI::P_INTERRO, a1); return; }
  if (!strcmp(w, "sfx")) { sfx(atoi(a1)); return; }
  if (!strcmp(w, "favour")) {
    char key[40];
    snprintf(key, sizeof(key), "fav_%s", a1);
    if (!flag(key)) {
      setFlag(key);
      setVar("favours", getVar("favours") + 1);
      int sk = 0;
      for (int i = 0; i < SK_COUNT; i++) if (!strcmp(a2, SKILL_KEYS[i])) sk = i;
      setVar(SKILL_NAMES[sk], skill(sk) + 1);
      char msg[80];
      snprintf(msg, sizeof(msg), "A favour is granted: %s +1", SKILL_NAMES[sk]);
      UI::toast(msg);
      sfx(RunAudio::DingTop);
      g_flash = 0.6f;
    }
    return;
  }
  if (!strcmp(w, "teach")) {
    int mv = moveByName(a1);
    if (mv >= 0 && g_nParty) {
      teach(mv);
      char msg[80];
      snprintf(msg, sizeof(msg), "%s learned %s!", SPECIES[g_party[0].sp].name, MOVES[mv].name);
      UI::toast(msg);
    }
    return;
  }
  if (!strcmp(w, "save")) { saveGame(); UI::toast("The case is saved."); return; }
  if (!strcmp(w, "chapter")) { snprintf(s_chapter, sizeof(s_chapter), "%s", rest); s_state = SS_CHAPTER; s_t = 0; return; }
  if (!strcmp(w, "toast")) { UI::toast(rest); return; }
  if (!strcmp(w, "composure")) { setVar("composure", clampi(getVar("composure") + atoi(a1), 0, 6)); return; }
  if (!strcmp(w, "skillup")) {
    int sk = 0;
    for (int i = 0; i < SK_COUNT; i++) if (!strcmp(a1, SKILL_KEYS[i])) sk = i;
    setVar(SKILL_NAMES[sk], skill(sk) + (a2[0] ? atoi(a2) : 1));
    return;
  }
  if (!strcmp(w, "ending")) {
    setVar("card_now", atoi(a1));
    g_cardT0 = g_time;
    g_mode = Mode::Ending;
    s_state = SS_CARD;
    return;
  }
  if (!strcmp(w, "title")) { s_state = SS_IDLE; g_mode = Mode::Title; g_fade = 1; return; }
  if (!strcmp(w, "team")) { s_state = SS_EXTERNAL; UI::open(UI::P_TEAM); return; }
  if (!strcmp(w, "chatter")) {
    // a generated line for this kind of speaker, or the written one after it
    static char line[120];
    const char *fallback = rest;
    for (int k = 0; k < 2 && *fallback; k++) { while (*fallback && *fallback != ' ') fallback++; while (*fallback == ' ') fallback++; }
    if (!chatterTake(a1, line, sizeof(line))) snprintf(line, sizeof(line), "%s", fallback);
    speak(a2, (int)strlen(a2), -1, line);
    return;
  }
  if (!strcmp(w, "town")) { setVar("town", atoi(a1)); return; }
  if (!strcmp(w, "inn")) { worldGoInn(); g_fade = 1; return; }
  Serial.printf("Gaslight: unknown command @%s\n", w);
}

static void run() {
  char line[320];
  int guard = 0;
  while (s_state == SS_RUN && s_pc && *s_pc && guard++ < 400) {
    const char *here = s_pc;
    int n = readLine(line, sizeof(line));
    if (!n || (line[0] == '/' && line[1] == '/')) continue;
    if (line[0] == '#') continue; // labels fall through; scenes end with @end
    if (line[0] == '>') { collectChoices(here); return; }
    if (line[0] == '?') {
      const char *h = strrchr(line, '#');
      if (!h) continue;
      char c[200];
      int cl = (int)(h - line - 1);
      if (cl > 199) cl = 199;
      memcpy(c, line + 1, cl);
      c[cl] = 0;
      if (cond(c)) jump(h + 1);
      continue;
    }
    if (line[0] == '+' && line[1] != ' ') { setFlag(line + 1, true); continue; }
    if (line[0] == '-' && line[1] != ' ' && line[1] != ':') { setFlag(line + 1, false); continue; }
    if (line[0] == '@') { exec(line); continue; }
    // speech: key[(mood)]: text
    const char *colon = strchr(line, ':');
    if (!colon) { speak("-", 1, -1, line); return; }
    int kl = (int)(colon - line), mood = -1;
    const char *paren = (const char *)memchr(line, '(', kl);
    if (paren) { mood = atoi(paren + 1); kl = (int)(paren - line); }
    const char *t = colon + 1;
    while (*t == ' ') t++;
    speak(line, kl, mood, t);
    return;
  }
  if (s_state == SS_RUN) s_state = SS_IDLE;
}

// --- the outside world -------------------------------------------------------------------------------------

void start(const char *label) {
#ifdef PC_SIM
  if (getenv("GGTRACE")) printf("[scene] %s\n", label);
#endif
  if (!jump(label)) return;
  s_state = SS_RUN;
  run();
}

void resume(const char *label) {
  if (label && label[0]) { start(label); return; }
  if (s_state == SS_EXTERNAL) { s_state = SS_RUN; run(); }
}

bool active() { return s_state != SS_IDLE; }
bool blocksWorld() { return s_state != SS_IDLE; }

void update(float dt) {
  s_t += dt;
  switch (s_state) {
    case SS_SAY: s_shown += dt * 75; break;
    case SS_WAIT:
      s_waitT -= dt;
      if (s_waitT <= 0) { s_state = SS_RUN; run(); }
      break;
    case SS_WALK:
      if (s_walkWho && !s_walkWho->moving) {
        if (s_walkLeft <= 0) { s_state = SS_RUN; run(); break; }
        static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {-1, 0, 1, 0};
        s_walkWho->tx = s_walkWho->x + DX[s_walkDir];
        s_walkWho->tz = s_walkWho->z + DZ[s_walkDir];
        s_walkWho->moving = true;
        s_walkLeft--;
      }
      break;
    case SS_RUN: run(); break;
    default: break;
  }
}

static void pickOption() {
  const Opt &o = s_opt[s_optSel];
  sfx(RunAudio::Tick);
  if (o.skill >= 0) { startCheck(o.skill, o.dc, o.pass, o.fail); return; }
  if (o.pass[0]) { start(o.pass); return; }
  s_state = SS_RUN;
  run();
}

void key(Btn b) {
#ifdef PC_SIM
  if (getenv("GGDEBUG")) printf("[script] key %d state %d shown %.0f/%d text '%.40s'\n", b, s_state, s_shown, s_len, s_text);
#endif
  switch (s_state) {
    case SS_SAY:
      if (b != B_OK) return;
      {
        int pe = pageEnd();
        if (s_shown < pe - s_pageOff) { s_shown = (float)(pe - s_pageOff); return; }
        if (s_text[pe]) { s_pageOff = pe; s_shown = 0; sfx(RunAudio::Tick); return; } // the next page
      }
      sfx(RunAudio::Tick);
      s_state = SS_RUN;
      run();
      break;
    case SS_CHOICE:
      if (b == B_UP) wheel(-1);
      else if (b == B_DOWN) wheel(1);
      else if (b == B_OK) pickOption();
      break;
    case SS_CHECK:
      if (b == B_OK && s_t > 1.4f) {
        if (!s_ckPass) setVar("composure", clampi(getVar("composure") - (s_ckDc >= 11 ? 1 : 0), 0, 6));
        start(s_ckPass ? s_ckPassL : s_ckFailL);
      }
      break;
    case SS_CARD:
      if (b == B_OK && s_t > 0.6f) {
        if (g_mode == Mode::Card && g_time - g_cardT0 < 3.2f) { g_cardT0 = g_time - 3.2f; return; } // finish the typing first
        if (g_mode == Mode::Card && cardHasMore(getVar("card_now"))) { g_cardPage++; g_cardT0 = g_time; return; }
        g_cardPage = 0;
        if (g_mode == Mode::Ending) { s_state = SS_IDLE; g_mode = Mode::Title; g_fade = 1; return; }
        g_mode = (Mode)getVar("card_ret");
        if (g_mode == Mode::Card) g_mode = Mode::World;
        g_fade = 0.6f;
        s_state = SS_RUN;
        run();
      }
      break;
    case SS_CHAPTER:
      if (b == B_OK && s_t > 1.0f) { s_state = SS_RUN; run(); }
      break;
    default: break;
  }
}

void wheel(int d) {
  if (s_state != SS_CHOICE || !s_nOpt) return;
  s_optSel = (s_optSel + (d > 0 ? 1 : s_nOpt - 1)) % s_nOpt;
  if (s_optSel < s_optFirst) s_optFirst = s_optSel;
  if (s_optSel >= s_optFirst + 4) s_optFirst = s_optSel - 3;
}

// --- drawing ------------------------------------------------------------------------------------------------

static int chancePct(int sk, int dc) {
  int need = dc - skill(sk), ok = 0;
  for (int a = 1; a <= 6; a++)
    for (int b = 1; b <= 6; b++)
      if ((a + b >= need || (a == 6 && b == 6)) && !(a == 1 && b == 1)) ok++;
  return ok * 100 / 36;
}

static void mythEmblem(int who, float x, float y, float w, float h) {
  Raster::roundRect(x, y, w, h, 8, c565(0x141B26));
  Raster::roundRect(x + 2, y + 2, w - 4, h - 4, 7, c565(0x22303E));
  float cx = x + w / 2, cy = y + h / 2;
  uint16_t gold = c565(0xD8B060);
  if (who == -2) { // three winged sisters
    for (int i = 0; i < 3; i++) {
      float sx = cx - 24 + i * 24;
      Raster::ellipse(sx, cy + 8, 7, 20, gold, 200);
      Raster::circle(sx, cy - 16, 6, gold);
      poly3(sx - 4, cy - 2, sx - 16, cy - 26, sx - 10, cy + 6, gold, 140);
    }
  } else if (who == -3) { // one great eye
    Raster::ellipse(cx, cy, 30, 18, gold);
    Raster::circle(cx, cy, 12, c565(0xFFE08A));
    Raster::circle(cx, cy, 6, 0x0000);
  } else if (who == -5) { // the Sphinx: a calm face in a striped headdress
    poly4(cx - 30, cy + 40, cx + 30, cy + 40, cx + 22, cy - 20, cx - 22, cy - 20, gold, 200);
    Raster::ellipse(cx, cy - 4, 16, 22, c565(0xE8C890));
    for (int i = 0; i < 4; i++) Raster::rect(cx - 28 + i * 3, cy - 18 + i * 14, 56 - i * 6, 4, c565(0x3A5A8A));
    Raster::line(cx - 8, cy - 8, cx - 3, cy - 8, 2, 0x0000); Raster::line(cx + 3, cy - 8, cx + 8, cy - 8, 2, 0x0000);
  } else if (who == -6) { // a raven
    Raster::ellipse(cx, cy + 6, 20, 26, c565(0x18181C));
    Raster::circle(cx + 6, cy - 22, 13, c565(0x18181C));
    poly3(cx + 16, cy - 24, cx + 34, cy - 18, cx + 16, cy - 16, c565(0x3A3A3A));
    Raster::circle(cx + 10, cy - 25, 3, c565(0xE8E0D0));
  } else if (who == -7) { // three threads of fate
    for (int i = 0; i < 3; i++) {
      float x0 = cx - 24 + i * 24;
      for (int k = 0; k < 8; k++) Raster::line(x0 + sinf(k * 0.8f + i) * 6, cy - 40 + k * 10, x0 + sinf((k + 1) * 0.8f + i) * 6, cy - 30 + k * 10, 2, gold);
    }
  } else if (who == -8) { // the serpent, coiled
    for (int k = 0; k < 12; k++) Raster::circle(cx + cosf(k * 0.7f) * (30 - k * 2), cy + sinf(k * 0.7f) * (30 - k * 2), 7 - k * 0.3f, c565(0x4A8A5A));
    Raster::circle(cx + 30, cy, 9, c565(0x5A9A6A));
  } else if (who == -9) { // a single white feather
    Raster::line(cx, cy + 40, cx, cy - 40, 2, gold);
    for (int k = 0; k < 12; k++) {
      float y = cy - 34 + k * 6;
      Raster::line(cx, y, cx - 16 + k * 0.6f, y - 8, 2, c565(0xF4F0E8));
      Raster::line(cx, y, cx + 16 - k * 0.6f, y - 8, 2, c565(0xF4F0E8));
    }
  } else { // winds
    for (int i = 0; i < 4; i++) Raster::line(cx - 30, cy - 18 + i * 12, cx + 26, cy - 24 + i * 14, 3, gold, 200);
    Raster::circle(cx + 28, cy - 2, 10, gold);
  }
}

static const int SAY_LINES = 4, SAY_LH = 28;
static bool sayPortrait() { return s_look >= 0 || s_look <= -2; }
static int sayWidth() { return sayPortrait() ? W - 118 - 34 : W - 62; }
static int pageEnd() { return s_pageOff + wrapLineStart(F_BODY, sayWidth(), s_text + s_pageOff, SAY_LINES); }

static void drawSay() {
  bool portrait = sayPortrait();
  int bx = 6, by = 136, bw = W - 12, bh = 138;
  panel(bx, by, bw, bh, c565(0x161A22), 232, s_voice >= 0 ? SKILL_COL[s_voice] : Col::BRASS);
  int tx = portrait ? 118 : 22, tw = sayWidth();
  if (portrait) {
    if (s_look >= 0) drawPortrait(s_look, 8, 112, 104, 160, s_mood);
    else mythEmblem(s_look, 8, 112, 104, 160);
  }
  if (s_who[0]) {
    uint16_t nc = s_voice >= 0 ? SKILL_COL[s_voice] : Col::AMBER;
    int nw = textW(F_BOLD, s_who) + 20;
    panel(tx - 6, by - 18, nw, 32, c565(0x161A22), 240, nc);
    text(F_BOLD, tx + 4, by - 15, s_who, nc);
  }
  uint16_t col = s_voice >= 0 ? mix565(Col::CREAM, SKILL_COL[s_voice], 90) : s_look < 0 && !s_who[0] ? c565(0xD8D0C0) : Col::CREAM;
  int pe = pageEnd();
  wrap(F_BODY, tx, by + 16, tw, s_text + s_pageOff, col, Col::AMBER, SAY_LH, SAY_LINES, (int)s_shown);
  if (s_shown >= pe - s_pageOff && fmodf(g_time, 1.0f) < 0.6f) {
    if (s_text[pe]) poly3((float)W - 30, (float)by + bh - 20, (float)W - 14, (float)by + bh - 20, (float)W - 22, (float)by + bh - 10, Col::AMBER);
    else Raster::rect(W - 30, by + bh - 18, 14, 6, Col::AMBER);
  }
}

static void drawChoice() {
  int n = s_nOpt < 4 ? s_nOpt : 4;
  int bh = 12 + n * 32, by = H - 6 - bh;
  // the last thing said stays above, small
  if (s_text[0]) {
    panel(6, by - 58, W - 12, 52, c565(0x161A22), 200, Col::BRASS);
    wrap(F_SMALL, 18, by - 54, W - 36, s_text + s_pageOff, Col::DIM, Col::AMBER, 22, 2);
  }
  panel(6, by, W - 12, bh, c565(0x161A22), 236, Col::BRASS);
  for (int r = 0; r < n; r++) {
    int i = s_optFirst + r;
    const Opt &o = s_opt[i];
    int y = by + 8 + r * 32;
    bool sel = i == s_optSel;
    if (sel) Raster::roundRect(12, y - 2, W - 24, 31, 6, c565(0x34302A));
    int x = 22;
    if (o.skill >= 0) {
      char tag[40];
      snprintf(tag, sizeof(tag), "[%s %d%%] ", SKILL_VOICE[o.skill], chancePct(o.skill, o.dc));
      x = text(F_SMALL, x, y + 4, tag, SKILL_COL[o.skill]);
    }
    const FontInfo &f = textW(sel ? F_BOLD : F_BODY, o.text) <= W - 30 - x ? (sel ? F_BOLD : F_BODY) : F_SMALL;
    char cut[96];
    snprintf(cut, sizeof(cut), "%s", o.text);
    if (textW(f, cut) > W - 30 - x) { // still too long: end it with dots
      int n = strlen(cut);
      while (n > 3 && textW(f, cut) > W - 30 - x) { n--; strcpy(cut + n - 3, "..."); cut[n] = 0; }
    }
    text(f, x, y + (&f == &F_SMALL ? 4 : 0), cut, sel ? Col::AMBER : Col::CREAM);
  }
  if (s_optFirst > 0) textC(F_SMALL, W - 24, by + 2, "^", Col::DIM);
  if (s_optFirst + 4 < s_nOpt) textC(F_SMALL, W - 24, by + bh - 22, "v", Col::DIM);
}

static void die(float x, float y, int v, float spin) {
  Raster::roundRect(x - 18, y - 18, 36, 36, 7, c565(0xF0E6D0));
  Raster::roundRect(x - 16, y - 16, 32, 32, 6, c565(0xFFF8E8));
  if (spin > 0) v = 1 + (int)(spin * 37) % 6;
  static const int pips[7][6][2] = {{}, {{0, 0}}, {{-1, -1}, {1, 1}}, {{-1, -1}, {0, 0}, {1, 1}},
                                    {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}, {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
                                    {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}}};
  for (int i = 0; i < v; i++) Raster::circle(x + pips[v][i][0] * 9, y + pips[v][i][1] * 9, 3.2f, c565(0x2A1E16));
}

static void drawCheck() {
  int sk = s_ckSkill;
  panel(40, 50, W - 80, 186, c565(0x161A22), 240, SKILL_COL[sk]);
  char head[64];
  snprintf(head, sizeof(head), "%s  vs  %d", SKILL_VOICE[sk], s_ckDc);
  textC(F_BOLD, W / 2, 60, head, SKILL_COL[sk]);
  float roll = clampf(1.0f - s_t / 1.0f, 0, 1);
  die(W / 2 - 30, 124, s_ckD1, roll > 0 ? s_t : 0);
  die(W / 2 + 30, 124, s_ckD2, roll > 0 ? s_t + 0.3f : 0);
  if (s_t > 1.0f) {
    int bonus = getVar("composure") <= 0 ? -1 : 0;
    char sum[64];
    snprintf(sum, sizeof(sum), "%d + %d + %s %d%s = %d", s_ckD1, s_ckD2, SKILL_NAMES[sk], skill(sk),
             bonus ? " - rattled 1" : "", s_ckD1 + s_ckD2 + skill(sk) + bonus);
    textC(F_SMALL, W / 2, 150, sum, Col::DIM);
    textC(F_TITLE, W / 2, 178, s_ckPass ? "Success" : "Failure", s_ckPass ? Col::GOOD : Col::BAD);
  }
}

static void drawChapter() {
  float a = clampf(s_t * 1.5f, 0, 1);
  fadeRect(0, 0, W, H, c565(0x0C0E12), (uint8_t)(255 * clampf(s_t * 3, 0, 1)));
  const char *bar = strchr(s_chapter, '|');
  char title[64];
  int tl = bar ? (int)(bar - s_chapter) : (int)strlen(s_chapter);
  snprintf(title, sizeof(title), "%.*s", tl, s_chapter);
  textC(F_TITLE, W / 2, 88, title, mix565(0, Col::CREAM, (int)(256 * a)));
  if (bar) textC(F_BODY, W / 2, 150, bar + 1, mix565(0, Col::BRASS, (int)(256 * a)));
  Raster::rect(W / 2 - 60 * a, 140, 120 * a, 1.5f, Col::BRASS);
}

void draw() {
  switch (s_state) {
    case SS_SAY: drawSay(); break;
    case SS_CHOICE: drawChoice(); break;
    case SS_CHECK: drawCheck(); break;
    default: break;
  }
}

void drawOver() {
  if (s_state == SS_CHAPTER) drawChapter();
}

#ifdef PC_SIM
// --- the playtest bot's hand in dialogue ------------------------------------------------------
static uint32_t s_botSeen[512]; static uint16_t s_botCount[512]; static int s_botN = 0;
static int botTimes(const char *t, bool bump) {
  uint32_t h = nameHash(t);
  for (int i = 0; i < s_botN; i++) if (s_botSeen[i] == h) { if (bump) s_botCount[i]++; return s_botCount[i]; }
  if (bump && s_botN < 512) { s_botSeen[s_botN] = h; s_botCount[s_botN++] = 1; return 1; }
  return 0;
}
bool g_botWantRest = false;
int botState() { return s_state; }
Btn botKey() {
  if (s_state == SS_WAIT || s_state == SS_WALK || s_state == SS_RUN || s_state == SS_EXTERNAL) return B_NONE;
  if (s_state == SS_CHAPTER && s_t < 1.2f) return B_NONE;
  if (s_state == SS_CARD && s_t < 0.7f) return B_NONE;
  if (s_state != SS_CHOICE) return B_OK;
  // never leave a scene just after arriving: prefer anything not tried yet, "goodbye"-ish options last
  int best = 0, bv = 1 << 30;
  if (g_botWantRest)
    for (int i = 0; i < s_nOpt; i++)
      if (!strncmp(s_opt[i].text, "Rest", 4)) { if (s_optSel != i) return i > s_optSel ? B_DOWN : B_UP; return B_OK; }
  for (int i = 0; i < s_nOpt; i++) {
    int v = botTimes(s_opt[i].text, false) * 4;
    const char *t = s_opt[i].text;
    if (strstr(t, "Never mind") || strstr(t, "Not now") || strstr(t, "Leave") || strstr(t, "Goodbye") || strstr(t, "later")) v += 3;
    if (v < bv) { bv = v; best = i; }
  }
  if (s_optSel != best) return best > s_optSel ? B_DOWN : B_UP;
  botTimes(s_opt[best].text, true);
  return B_OK;
}
const char *botSaying() { return s_state == SS_SAY ? s_text : s_state == SS_CHOICE && s_nOpt ? s_opt[s_optSel].text : ""; }
#endif

} // namespace Script
} // namespace Gaslight
