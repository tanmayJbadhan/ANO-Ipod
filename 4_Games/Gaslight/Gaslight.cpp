// Gaslight & Griffins - the console's entry points, the frame loop, and the
// strip renderer that shares each frame between both cores (after
// Lamplighter's): 14 strips of 20 rows, drawn landscape, turned to portrait
// and sent while the next is drawn.
#pragma GCC optimize("O2")
#include "Gaslight.h"
#include "Game.h"
#include "Raster.h"
#include "RunAudio.h"
#include "Tune.h"
#include "GLMusic.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#ifndef PC_SIM
#include <soc/soc_memory_layout.h>
#endif

namespace Gaslight {

uint8_t g_held = 0;
float g_time = 0;
Mode g_mode = Mode::Title;
float g_fade = 0, g_flash = 0, g_shake = 0, g_cardT0 = 0;

static bool s_active = false, s_exit = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static const int SLOTS = 4;
static uint16_t *s_land[2] = {}, *s_depth[2] = {}, *s_port[SLOTS] = {}, *s_shot = nullptr;
static Preferences s_prefs;

// --- flags and counters ---------------------------------------------------------
struct VarSlot { uint32_t h; int16_t v; };
static const int NVARS = 384;
static VarSlot s_vars[NVARS];

uint32_t nameHash(const char *s, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; s[i] && (n < 0 || i < n); i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
  return h ? h : 1;
}
int getVarH(uint32_t h) {
  for (int i = 0; i < NVARS; i++) {
    if (s_vars[i].h == h) return s_vars[i].v;
    if (!s_vars[i].h) return 0;
  }
  return 0;
}
void setVarH(uint32_t h, int v) {
  for (int i = 0; i < NVARS; i++) {
    if (s_vars[i].h == h || !s_vars[i].h) { s_vars[i].h = h; s_vars[i].v = (int16_t)v; return; }
  }
}
int getVar(const char *name) { return getVarH(nameHash(name)); }
void setVar(const char *name, int v) { setVarH(nameHash(name), v); }
void varsClear() { memset(s_vars, 0, sizeof(s_vars)); }

// --- save -----------------------------------------------------------------------
struct SaveHead { uint32_t magic; uint8_t area, face; int8_t x, z; int32_t money; uint8_t nParty, nBox; uint8_t items[I_COUNT]; };
static const uint32_t SAVE_MAGIC = 0x47414C31; // "GAL1"

bool hasSave() {
  SaveHead h;
  return s_prefs.getBytes("head", &h, sizeof(h)) == sizeof(h) && h.magic == SAVE_MAGIC;
}
bool saveGame() {
  SaveHead h;
  h.magic = SAVE_MAGIC;
  h.area = g_area; h.face = g_player.face; h.x = (int8_t)g_player.x; h.z = (int8_t)g_player.z;
  h.money = g_money; h.nParty = (uint8_t)g_nParty; h.nBox = (uint8_t)g_nBox;
  memcpy(h.items, g_items, sizeof(h.items));
  bool ok = s_prefs.putBytes("vars", s_vars, sizeof(s_vars)) == sizeof(s_vars);
  ok = s_prefs.putBytes("party", g_party, sizeof(g_party)) == sizeof(g_party) && ok;
  ok = s_prefs.putBytes("box", g_box, sizeof(g_box)) == sizeof(g_box) && ok;
  ok = s_prefs.putBytes("head", &h, sizeof(h)) == sizeof(h) && ok;
  return ok;
}
bool loadGame() {
  SaveHead h;
  if (s_prefs.getBytes("head", &h, sizeof(h)) != sizeof(h) || h.magic != SAVE_MAGIC) return false;
  s_prefs.getBytes("vars", s_vars, sizeof(s_vars));
  s_prefs.getBytes("party", g_party, sizeof(g_party));
  s_prefs.getBytes("box", g_box, sizeof(g_box));
  g_money = h.money; g_nParty = h.nParty; g_nBox = h.nBox;
  memcpy(g_items, h.items, sizeof(g_items));
  if (h.area == A_WILDS) wildsRestore(); // the same land, from its seed
  worldLoad(h.area, h.x, h.z, h.face);
  return true;
}

// --- sound -----------------------------------------------------------------------
void sfx(int id) { RunAudio::fx((RunAudio::Fx)id); }

// --- title screen -------------------------------------------------------------------
static int s_titleSel = 0;
static bool s_canContinue = false;
static float s_titleT = 0;

static void titleOptions(const char **opt, int &n) {
  n = 0;
  if (s_canContinue) opt[n++] = "Continue the case";
  opt[n++] = "A new case";
  opt[n++] = "Back to the console";
}

static void startNew() {
  varsClear();
  g_nParty = 0; g_nBox = 0; g_money = 120;
  memset(g_items, 0, sizeof(g_items));
  g_items[I_TONIC] = 2;
  setVar("composure", 3);
  for (int s = 0; s < SK_COUNT; s++) setVar(SKILL_NAMES[s], 2);
  worldLoad(A_FALLS, 3, 13, F_N);
  g_mode = Mode::World;
  g_fade = 1;
  Script::start("prologue");
}

static void titleKey(Btn b) {
  const char *opt[3];
  int n;
  titleOptions(opt, n);
  if (b == B_UP) s_titleSel = (s_titleSel + n - 1) % n;
  if (b == B_DOWN) s_titleSel = (s_titleSel + 1) % n;
  if (b != B_OK) return;
  sfx(RunAudio::Ding);
  const char *o = opt[s_titleSel];
  if (o[0] == 'C') {
    if (loadGame()) { g_mode = Mode::World; g_fade = 1; UI::toast("The case continues."); }
  } else if (o[0] == 'A') {
    startNew();
  } else {
    s_exit = true;
  }
}

static void titleDraw() {
  // a slow drift over Fogmouth at dusk is drawn by the world; this is the card
  float a = clampf(s_titleT * 0.8f, 0, 1);
  int y0 = 46;
  fadeRect(0, y0 - 18, W, 100, c565(Pal::INK), (uint8_t)(150 * a));
  textC(F_TITLE, W / 2 + 2, y0 + 2, "Gaslight & Griffins", 0);
  textC(F_TITLE, W / 2, y0, "Gaslight & Griffins", mix565(c565(Pal::INK), Col::CREAM, (int)(256 * a)));
  textC(F_SMALL, W / 2, y0 + 46, "A case in four towns and one old story", mix565(c565(Pal::INK), Col::BRASS, (int)(256 * a)));
  if (s_titleT < 1.2f) return;
  const char *opt[3];
  int n;
  titleOptions(opt, n);
  for (int i = 0; i < n; i++) {
    int y = 156 + i * 34;
    bool sel = i == s_titleSel;
    int w = textW(F_BOLD, opt[i]) + 36;
    if (sel) panel(W / 2 - w / 2, y - 3, w, 33, c565(0x2A3442), 210, Col::BRASS);
    textC(sel ? F_BOLD : F_BODY, W / 2, y, opt[i], sel ? Col::CREAM : Col::DIM);
  }
  textC(F_SMALL, W / 2, 256, "Wheel to choose, centre to confirm", Col::DIM);
}

// --- strips ---------------------------------------------------------------------------
static bool s3D = true;

static volatile uint32_t s_sec[2][3]; // per core: 3D, 2D, turning
#ifndef PC_SIM
static inline int coreNo() { return xPortGetCoreID() & 1; }
#else
static inline int coreNo() { return 0; }
#endif

static void drawStrip(int k, uint16_t *land, uint16_t *depth, uint16_t *port) {
  int top = k * SH, cn = coreNo();
  uint32_t t0 = micros();
  Raster::target(land, W, SH, top);
  Raster::setScale(1);
  if (s3D) drawScene3D(land, depth, top);
  else for (int i = 0; i < W * SH; i++) land[i] = 0;
  uint32_t t1 = micros();
  s_sec[cn][0] += t1 - t0;
  Raster::smooth(true);
  switch (g_mode) {
    case Mode::Title: worldOverlay(); titleDraw(); break;
    case Mode::World: worldOverlay(); break;
    case Mode::Battle: battleOverlay(); break;
    case Mode::Card:
    case Mode::Ending: drawCard(getVar("card_now"), g_time - g_cardT0); break;
  }
  if (g_mode != Mode::Title) {
    Script::draw();
    UI::draw();
    UI::drawToast();
  }
  if (g_flash > 0) fadeRect(0, top, W, SH, 0xFFFF, (uint8_t)(clampf(g_flash, 0, 1) * 255));
  if (g_fade > 0) fadeRect(0, top, W, SH, 0, (uint8_t)(clampf(g_fade, 0, 1) * 255));
  Script::drawOver();
  Raster::smooth(false);
  uint32_t t2 = micros();
  s_sec[cn][1] += t2 - t1;
  if (!port) return;
  bool fl = s_gfx->flipped();
  for (int j = 0; j < SH; j++) {
    const uint16_t *src = land + j * W;
    uint16_t *d = port + (fl ? SH - 1 - j : j);
    if (fl) for (int py = 0; py < W; py++) d[py * SH] = src[py];
    else for (int py = 0; py < W; py++) d[py * SH] = src[W - 1 - py];
  }
  s_sec[cn][2] += micros() - t2;
}

static void sendStrip(int k) {
  int px = s_gfx->flipped() ? H - (k + 1) * SH : k * SH;
  s_panel->draw16bitRGBBitmap(px, 0, s_port[k % SLOTS], SH, W);
}

static int s_nextStrip = 0;
static volatile int s_sent = 0;
static volatile uint8_t s_ready[NSTRIP];
static inline int takeStrip() { return __atomic_fetch_add(&s_nextStrip, 1, __ATOMIC_ACQ_REL); }
static inline bool isReady(int k) { return __atomic_load_n(&s_ready[k], __ATOMIC_ACQUIRE); }
static void drawTaken(int k, int core) {
  drawStrip(k, s_land[core], s_depth[core], s_port[k % SLOTS]);
  __atomic_store_n(&s_ready[k], 1, __ATOMIC_RELEASE);
}

static struct Stats { bool on; uint32_t since, frames, sum, max, prep, draw, wDraw, upd; } s_st;

#ifndef PC_SIM
static TaskHandle_t s_worker = nullptr;
static SemaphoreHandle_t s_go = nullptr, s_done = nullptr;
void chatterStep(int chars);
bool chatterWants();
static void workerTask(void *) {
  for (;;) {
    // between frames, write a little chatter; wait a tick each time so the
    // core's idle task still gets to run
    if (chatterWants()) {
      if (xSemaphoreTake(s_go, 1) != pdTRUE) { chatterStep(6); continue; }
    } else {
      xSemaphoreTake(s_go, portMAX_DELAY);
    }
    for (;;) {
      int k = takeStrip();
      if (k >= NSTRIP) break;
      while (k - SLOTS >= s_sent) {}
      uint32_t t0 = micros();
      drawTaken(k, 1);
      s_st.wDraw += micros() - t0;
    }
    xSemaphoreGive(s_done);
  }
}
#endif

static void prepareFrame() {
  s3D = true;
  switch (g_mode) {
    case Mode::Title:
    case Mode::World: worldPrepare(); break;
    case Mode::Battle: battlePrepare(); break;
    default: s3D = false; break;
  }
}

#ifdef PC_SIM
bool g_simNoRender = false; // the playtest bot runs without drawing
const char *titleSelText() { const char *o[3]; int n; titleOptions(o, n); return o[s_titleSel]; }
uint32_t varsDigest() {
  // everything that marks progress (where Wren stands and passing numbers left out)
  static const char *const skip[] = {"area", "card_now", "hold_fade", "wild_seed", "wild_trail", "wilds_visits", "composure", "town", "money",
                                     "wh", "wc0", "wc1", "wc2", "wc3", "wc4"};
  uint32_t sk[sizeof(skip) / sizeof(skip[0])];
  for (unsigned i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) sk[i] = nameHash(skip[i]);
  uint32_t d = 0;
  for (int i = 0; i < NVARS && s_vars[i].h; i++) {
    bool s = false;
    for (unsigned k = 0; k < sizeof(sk) / sizeof(sk[0]); k++) s |= sk[k] == s_vars[i].h;
    if (!s) d += (s_vars[i].h * 2654435761u) ^ (uint32_t)(s_vars[i].v * 40503u);
  }
  return d;
}
int varsUsed() { int n = 0; while (n < NVARS && s_vars[n].h) n++; return n; }
#endif
// --- music: the piece for wherever Wren is and whatever is happening -------------------------
static int musicFor() {
  using namespace Songs;
  if (g_mode == Mode::Battle) return battleIsBoss() ? M_BOSS : M_BATTLE;
  if (g_mode == Mode::Ending) return M_FALLS;
  if (UI::active() && (UI::current() == UI::P_INTERRO || UI::current() == UI::P_ACCUSE)) return M_MYSTERY;
  int a = g_area;
  if (a == A_WILDS) return M_WILDS;
  if (a == A_FALLS) return M_FALLS;
  // homely rooms keep the town's own tune; other rooms are for investigating
  int town = getVar("town");
  static const uint8_t TOWN_SONG[6] = {M_FOGMOUTH, M_FOGMOUTH, M_SANDMERE, M_GREENWOLD, M_SKJOLD, M_URLUMEN};
  int townSong = TOWN_SONG[town < 0 ? 0 : town > 5 ? 5 : town];
  if (a == A_QUILL || a == A_INN || a == A_TAVERN || a == A_SIDURI) return townSong;
  if (AREAS[a].indoor) return M_MYSTERY;
  if (a <= A_ROCK) return M_FOGMOUTH;
  if (a <= A_INN) return M_SANDMERE;
  if (a <= A_IVY) return M_GREENWOLD;
  if (a <= A_SEACAVE) return M_SKJOLD;
  return M_URLUMEN;
}

static void render() {
  uint32_t t0 = micros();
  prepareFrame();
  s_st.prep += micros() - t0;
  memset((void *)s_ready, 0, sizeof(s_ready));
  s_sent = 0;
  __atomic_store_n(&s_nextStrip, 0, __ATOMIC_RELEASE);
#ifndef PC_SIM
  if (s_worker) xSemaphoreGive(s_go);
#endif
  int sent = 0;
  auto sendNext = [&]() {
    while (!isReady(sent)) {}
    sendStrip(sent);
    s_sent = ++sent;
  };
  while (sent < NSTRIP) {
    if (isReady(sent)) { sendNext(); continue; }
    int k = __atomic_load_n(&s_nextStrip, __ATOMIC_ACQUIRE) < NSTRIP ? takeStrip() : NSTRIP;
    if (k >= NSTRIP) { sendNext(); continue; }
    while (k - SLOTS >= sent) sendNext();
    uint32_t t1 = micros();
    drawTaken(k, 0);
    s_st.draw += micros() - t1;
  }
#ifndef PC_SIM
  if (s_worker) xSemaphoreTake(s_done, portMAX_DELAY);
#endif
}

// --- public ------------------------------------------------------------------------------

static uint16_t *stripAlloc(bool preferInternal) {
  size_t n = (size_t)W * SH * 2;
  uint16_t *p = nullptr;
  if (preferInternal) p = (uint16_t *)heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) p = (uint16_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p;
}

void sceneFreeMem();
extern volatile uint32_t g_prof2[5];

static void freeBuffers() {
  for (auto &p : s_land) { heap_caps_free(p); p = nullptr; }
  for (auto &p : s_depth) { heap_caps_free(p); p = nullptr; }
  for (auto &p : s_port) { heap_caps_free(p); p = nullptr; }
  heap_caps_free(s_shot); s_shot = nullptr;
  worldFree();
  sceneFreeMem();
}

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel) {
  if (s_active) return true;
  s_gfx = gfx;
  s_panel = panel;
  bool ok = true;
  // Fastest memory to the buffers touched most: what's drawn, then depth.
  for (auto &p : s_land) ok = (p = stripAlloc(true)) && ok;
  for (auto &p : s_depth) ok = (p = stripAlloc(true)) && ok;
  for (auto &p : s_port) ok = (p = stripAlloc(true)) && ok;
  ok = worldAlloc() && ok;
  if (!ok) {
    freeBuffers();
    Serial.println("Gaslight: not enough memory");
    return false;
  }
#ifndef PC_SIM
  int inPsram = 0;
  for (auto *q : s_land) inPsram += !esp_ptr_internal(q);
  for (auto *q : s_depth) inPsram += !esp_ptr_internal(q);
  for (auto *q : s_port) inPsram += !esp_ptr_internal(q);
  Serial.printf("Gaslight: %d of 8 strip buffers in PSRAM, internal free %u\n", inPsram,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  if (!s_worker) {
    s_go = xSemaphoreCreateBinary();
    s_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(workerTask, "gaslight", 8192, nullptr, 2, &s_worker, 0);
  }
#endif
  areasInit();
  s_prefs.begin("gaslight", false);
  s_canContinue = hasSave();
  s_titleSel = 0;
  s_titleT = 0;
  g_time = 0;
  g_fade = 1;
  g_flash = 0;
  g_mode = Mode::Title;
  worldLoad(A_TOWN, 19, 13, F_N);
  chatterStart();
  RunAudio::setMusic(Tune::render);
  RunAudio::start();
  RunAudio::Params quiet;
  quiet.paused = true;
  RunAudio::set(quiet);
  s_exit = false;
  s_active = true;
  return true;
}

void exit() {
  if (!s_active) return;
  chatterStop();
  Tune::play(-1);
  RunAudio::stop();
  RunAudio::setMusic(nullptr);
  s_prefs.end();
#ifndef PC_SIM
  // the worker may be between frames: make sure it isn't drawing before buffers go
  if (s_worker) { vTaskDelete(s_worker); vSemaphoreDelete(s_go); vSemaphoreDelete(s_done); s_worker = nullptr; s_go = s_done = nullptr; }
#endif
  freeBuffers();
#ifndef PC_SIM
  if (s_worker) {
    vTaskDelete(s_worker);
    vSemaphoreDelete(s_go);
    vSemaphoreDelete(s_done);
    s_worker = nullptr;
    s_go = s_done = nullptr;
  }
#endif
  s_active = false;
}

bool active() { return s_active; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }

const uint16_t *frameBuffer() {
  if (!s_shot) s_shot = (uint16_t *)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_shot) return nullptr;
  prepareFrame();
  for (int k = 0; k < NSTRIP; k++) {
    drawStrip(k, s_land[0], s_depth[0], nullptr);
    memcpy(s_shot + k * SH * W, s_land[0], (size_t)W * SH * 2);
  }
  return s_shot;
}

void benchStats() {
  bool on = !s_st.on;
  memset(&s_st, 0, sizeof(s_st));
  s_st.on = on;
  s_st.since = millis();
}

void benchSkip() {
  if (g_mode == Mode::Title) startNew();
  Script::start("debug_skip");
}

void pause() {}

static Btn toBtn(Key k) {
  switch (k) {
    case Key::Up: return B_UP;
    case Key::Down: return B_DOWN;
    case Key::Left: return B_LEFT;
    case Key::Right: return B_RIGHT;
    case Key::Select: return B_OK;
    default: return B_NONE;
  }
}

static void dispatch(Btn b) {
  if (g_mode == Mode::Title) { if (s_titleT > 1.2f) titleKey(b); return; }
  if (UI::active()) { UI::key(b); return; }
  if (g_mode == Mode::Battle) { if (g_fade < 0.6f) battleKey(b); return; }
  if (Script::active() && Script::blocksWorld()) { Script::key(b); return; }
  if (g_fade > 0.6f) return;
  if (g_mode == Mode::World) worldKey(b);
}

void key(const InputEvent &ev) {
  if (!ev.pressed) return;
  Btn b = toBtn(ev.key);
  if (b == B_NONE) return;
  // auto-repeat only moves cursors and walks
  if (ev.repeat && b == B_OK) return;
  dispatch(b);
}

static void wheelTurn(int d) {
  if (!d) return;
  if (g_mode == Mode::Title) {
    if (s_titleT > 1.2f) titleKey(d > 0 ? B_DOWN : B_UP);
    return;
  }
  if (UI::active()) { UI::wheel(d); return; }
  if (g_mode == Mode::Battle) { battleWheel(d); return; }
  if (Script::active() && Script::blocksWorld()) { Script::wheel(d); return; }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  static uint32_t last = 0;
  uint32_t nowUs = micros();
  float dt = last ? (nowUs - last) * 1e-6f : 0.02f;
  last = nowUs;
  if (dt > 0.1f) dt = 0.1f;
  g_held = 0;
  if (input.held(Key::Up)) g_held |= 1 << B_UP;
  if (input.held(Key::Down)) g_held |= 1 << B_DOWN;
  if (input.held(Key::Left)) g_held |= 1 << B_LEFT;
  if (input.held(Key::Right)) g_held |= 1 << B_RIGHT;
  if (input.held(Key::Select)) g_held |= 1 << B_OK;
  // Touch: a quick tap is Centre; in the world, dragging sideways turns the
  // camera and dragging up or down brings it in close.
  static bool tDown = false, tDrag = false;
  static int16_t tx0, ty0, tlx, tly;
  static uint32_t tAt = 0;
  bool freeWorld = g_mode == Mode::World && !UI::active() && !Script::active();
  if (touch.pressed()) {
    int16_t lx, ly;
    s_gfx->touchToLandscape(touch.x(), touch.y(), lx, ly);
    if (!tDown) { tDown = true; tDrag = false; tx0 = tlx = lx; ty0 = tly = ly; tAt = millis(); }
    else {
      if (abs(lx - tx0) + abs(ly - ty0) > 10) tDrag = true;
      if (tDrag && freeWorld) {
        worldTurn((lx - tlx) * 0.012f);
        worldZoom((ly - tly) * 0.006f);
      }
      tlx = lx; tly = ly;
    }
  } else if (tDown) {
    tDown = false;
    if (!tDrag && millis() - tAt < 500) dispatch(B_OK);
  }
  // In the world, the wheel turns the camera (with Centre held, it zooms).
  if (freeWorld && wheel) {
    worldTurn(wheel * 0.13f); // (holding Centre would quit to Home, so zoom is by touch or the menu)
    wheel = 0;
  }
  // the wheel moves one step per two clicks, so a small turn is one line
  static int32_t acc = 0;
  acc += wheel;
  while (acc >= 2) { wheelTurn(1); acc -= 2; }
  while (acc <= -2) { wheelTurn(-1); acc += 2; }

  uint32_t tu = micros();
  int steps = (int)ceilf(dt * 60 - 0.001f);
  if (steps < 1) steps = 1;
  for (int i = 0; i < steps; i++) {
    float h = dt / steps;
    g_time += h;
    if (g_flash > 0) g_flash -= h * 3;
    if (g_shake > 0) g_shake -= h * 2;
    if (g_mode == Mode::Title) {
      s_titleT += h;
      if (g_fade > 0) g_fade -= h * 0.7f;
      worldUpdate(h);
      continue;
    }
    if (g_fade > 0 && !getVar("hold_fade")) g_fade -= h * 1.6f;
    if (g_fade < 0) g_fade = 0;
    UI::update(h);
    Script::update(h);
    if (g_mode == Mode::World) worldUpdate(h);
    else if (g_mode == Mode::Battle) battleUpdate(h);
  }
  Tune::play(musicFor());
  s_st.upd += micros() - tu;
#ifdef PC_SIM
  if (!g_simNoRender)
#endif
  render();

  if (s_st.on) {
    static uint32_t prev = 0;
    uint32_t us = micros();
    if (prev) { uint32_t f = us - prev; s_st.frames++; s_st.sum += f; if (f > s_st.max) s_st.max = f; }
    prev = us;
    if (millis() - s_st.since >= 2000 && s_st.frames) {
      uint32_t n = s_st.frames;
      Serial.printf("GG fps %.1f  frame avg %lu max %lu ms  update %lu prepare %lu draw(c1) %lu draw(c0) %lu ms  tris %d\n",
                    n * 1e6f / s_st.sum, (unsigned long)(s_st.sum / n / 1000), (unsigned long)(s_st.max / 1000),
                    (unsigned long)(s_st.upd / n / 1000), (unsigned long)(s_st.prep / n / 1000),
                    (unsigned long)(s_st.draw / n / 1000), (unsigned long)(s_st.wDraw / n / 1000), sceneTris());
      Serial.printf("   per frame ms (core1/core0): 3D %.1f/%.1f  2D %.1f/%.1f  turn %.1f/%.1f\n",
                    s_sec[1][0] / 1000.0f / n, s_sec[0][0] / 1000.0f / n, s_sec[1][1] / 1000.0f / n, s_sec[0][1] / 1000.0f / n,
                    s_sec[1][2] / 1000.0f / n, s_sec[0][2] / 1000.0f / n);
      memset((void *)s_sec, 0, sizeof(s_sec));
      Serial.printf("   prepare ms: build people %.1f  static scene %.1f  people scene %.1f\n", g_prof[0] / 1000.0f / n,
                    g_prof[1] / 1000.0f / n, g_prof[2] / 1000.0f / n);
      Serial.printf("   faces looked at %lu per frame\n", (unsigned long)(g_prof[3] / n));
      memset((void *)g_prof, 0, sizeof(g_prof));
      Serial.printf("   3D split ms (both cores): ground %.1f  triangles %.1f\n", g_prof2[0] / 1000.0f / n, g_prof2[1] / 1000.0f / n);
      Serial.printf("   raster: %lu pixels, %lu spans, %lu triangle visits per frame\n", (unsigned long)(g_prof2[2] / n), (unsigned long)(g_prof2[3] / n), (unsigned long)(g_prof2[4] / n));
      memset((void *)g_prof2, 0, sizeof(g_prof2));
#ifndef PC_SIM
      Serial.printf("   ram internal free %u KB  psram free %u KB\n",
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
#endif
      bool on = s_st.on;
      memset(&s_st, 0, sizeof(s_st));
      s_st.on = on;
      s_st.since = millis();
    }
  }
}

} // namespace Gaslight
