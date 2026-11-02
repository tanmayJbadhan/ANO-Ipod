#include "CatNap.h"
#include "Raster.h"
#include "RunAudio.h"
#include "HalfRes.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>
#include <font/glcdfont.h>

using namespace Raster;

namespace CatNap {

// Laid out in screen units (456 x 280), drawn at half scale.
static const int W = HalfRes::W, H = HalfRes::H, FW = HalfRes::FW, FH = HalfRes::FH;
static const float K = 0.5f;
static const int COLS = 16, ROWS = 9;
static const float CELL = 26;
static const float BX = (W - COLS * CELL) / 2, BY = 38;
static const float PAUSE_HOLD = 0.8f; // seconds of DOWN

struct Household { const char *name, *blurb; uint8_t cats; };
static const Household HOUSES[3] = {
    {"Kitten", "A few sleepy kittens.", 14},
    {"Cat cafe", "A cafe full of cats.", 22},
    {"Cat lady", "Cats under everything!", 30},
};

enum class Mode : uint8_t { Title, Play, Paused, Lost, Won };

struct Cell { bool cat, open, yarn; uint8_t n, look; };

static bool s_active = false, s_exit = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static uint16_t *s_frame = nullptr;
static Preferences s_prefs;

static Mode s_mode = Mode::Title;
static int s_house = 0;
static Cell s_b[ROWS][COLS];
static bool s_laid = false;
static int s_cx = COLS / 2, s_cy = ROWS / 2;
static int s_woke = -1;             // the cushion that woke a cat (row * COLS + col)
static int s_opened = 0, s_yarns = 0;
static float s_t = 0, s_modeT = 0, s_clock = 0, s_downT = -1;
static uint32_t s_best[3];
static uint32_t s_rng = 1;

static uint32_t rnd() { s_rng = s_rng * 1664525u + 1013904223u; return s_rng >> 8; }

// --- text ---------------------------------------------------------------------

static void glyph(int lx, int ly, unsigned char ch, int size, uint16_t col) {
  for (int cx = 0; cx < 5; cx++) {
    uint8_t bits = font[ch * 5 + cx];
    for (int cy = 0; cy < 8; cy++, bits >>= 1) {
      if (!(bits & 1)) continue;
      int x0 = lx + cx * size, y0 = ly + cy * size;
      for (int yy = y0; yy < y0 + size; yy++) {
        if (yy < 0 || yy >= FH) continue;
        for (int xx = x0; xx < x0 + size; xx++) if (xx >= 0 && xx < FW) s_frame[yy * FW + xx] = col;
      }
    }
  }
}
static int textW(const char *s, int size) { return (int)strlen(s) * 12 * size; }
static void txt(int x, int y, const char *s, int size, uint16_t col) {
  int lx = x / 2, ly = y / 2;
  for (const char *p = s; *p; p++, lx += 6 * size) glyph(lx, ly, (unsigned char)*p, size, col);
}
static void txtC(int cx, int y, const char *s, int size, uint16_t col) { txt(cx - textW(s, size) / 2, y, s, size, col); }

// --- the board ------------------------------------------------------------------

static bool inside(int x, int y) { return x >= 0 && x < COLS && y >= 0 && y < ROWS; }

// Cats go in after the first lift, never on or around it.
static void layCats(int fx, int fy) {
  memset(s_b, 0, sizeof(s_b));
  int want = HOUSES[s_house].cats;
  while (want > 0) {
    int x = rnd() % COLS, y = rnd() % ROWS;
    if (s_b[y][x].cat || (abs(x - fx) <= 1 && abs(y - fy) <= 1)) continue;
    s_b[y][x].cat = true;
    want--;
  }
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++) {
      Cell &c = s_b[y][x];
      c.look = rnd() & 255;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
          if (inside(x + dx, y + dy) && s_b[y + dy][x + dx].cat) c.n++;
    }
  s_laid = true;
}

static void newBoard() {
  memset(s_b, 0, sizeof(s_b));
  for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) s_b[y][x].look = rnd() & 255;
  s_laid = false;
  s_woke = -1;
  s_opened = s_yarns = 0;
  s_clock = 0;
  s_cx = COLS / 2; s_cy = ROWS / 2;
}

static void setMode(Mode m) { s_mode = m; s_modeT = 0; }

// Lifts a cushion; empty ones open their whole quiet patch.
static void flood(int x, int y) {
  static int16_t stack[COLS * ROWS];
  int sp = 0;
  stack[sp++] = y * COLS + x;
  while (sp) {
    int i = stack[--sp], cx = i % COLS, cy = i / COLS;
    Cell &c = s_b[cy][cx];
    if (c.open || c.yarn) continue;
    c.open = true;
    s_opened++;
    if (c.n) continue;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (inside(cx + dx, cy + dy) && !s_b[cy + dy][cx + dx].open) stack[sp++] = (cy + dy) * COLS + cx + dx;
  }
}

static void checkWin() {
  if (s_opened < COLS * ROWS - HOUSES[s_house].cats) return;
  setMode(Mode::Won);
  RunAudio::fx(RunAudio::DingTop);
  uint32_t ms = (uint32_t)(s_clock * 1000);
  if (!s_best[s_house] || ms < s_best[s_house]) {
    s_best[s_house] = ms;
    char k[3] = {'b', (char)('0' + s_house), 0};
    s_prefs.putUInt(k, ms);
  }
}

static void lift(int x, int y) {
  if (!s_laid) layCats(x, y);
  Cell &c = s_b[y][x];
  if (c.yarn) return;
  if (c.open) {
    // A number whose cats are all marked: lift everything else around it.
    int marks = 0;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (inside(x + dx, y + dy) && s_b[y + dy][x + dx].yarn) marks++;
    if (!c.n || marks != c.n) return;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if (!inside(nx, ny) || s_b[ny][nx].open || s_b[ny][nx].yarn) continue;
        if (s_b[ny][nx].cat) { s_woke = ny * COLS + nx; break; }
        flood(nx, ny);
      }
  } else if (c.cat) {
    s_woke = y * COLS + x;
  } else {
    flood(x, y);
  }
  if (s_woke >= 0) {
    RunAudio::fx(RunAudio::Squeak);
    RunAudio::fx(RunAudio::Bonk);
    setMode(Mode::Lost);
    return;
  }
  RunAudio::fx(RunAudio::Tick);
  checkWin();
}

static void toggleYarn() {
  Cell &c = s_b[s_cy][s_cx];
  if (c.open) return;
  c.yarn = !c.yarn;
  s_yarns += c.yarn ? 1 : -1;
  RunAudio::fx(c.yarn ? RunAudio::PopSoft : RunAudio::Tick);
}

// --- drawing --------------------------------------------------------------------

static const uint32_t FUR[5] = {0xf2a65a, 0x9aa0ac, 0x3a3440, 0xf6efe6, 0xe0b48a};
static const uint32_t NUM[8] = {0x3a78d8, 0x2f9a5a, 0xe04a5a, 0x7a4ac0, 0xa8323e, 0x1f8a8a, 0x3a3440, 0x7a7a8a};

static void ear(float x, float y, float s, float dir, uint32_t c) {
  const float t[6] = {x - 3 * s * dir, y, x + 3 * s * dir, y, x + 1 * s * dir, y - 5 * s};
  poly(t, 3, rgb(c));
}

// A cat curled up asleep, centred on (x, y).
static void sleepingCat(float x, float y, float s, uint32_t look) {
  uint32_t fur = FUR[look % 5], dark = mixHex(fur, 0x000000, 0.35f);
  ellipse(x + 1 * s, y + 2 * s, 8 * s, 5.5f * s, rgb(fur));                 // body
  line(x + 7 * s, y + 4 * s, x - 5 * s, y + 6.5f * s, 2.4f * s, rgb(dark)); // tail wrapped round
  circle(x - 4 * s, y - 0.5f * s, 4.4f * s, rgb(fur));                       // head
  ear(x - 6.5f * s, y - 3 * s, s * 0.8f, -1, fur);
  ear(x - 1.5f * s, y - 3.5f * s, s * 0.8f, 1, fur);
  if (look % 5 == 4) circle(x + 3 * s, y, 2.4f * s, rgb(0x3a3440));          // calico patch
  line(x - 6.4f * s, y - 0.5f * s, x - 4.8f * s, y - 0.2f * s, 0.8f, rgb(0x2a2030)); // closed eyes
  line(x - 3.2f * s, y - 0.2f * s, x - 1.6f * s, y - 0.5f * s, 0.8f, rgb(0x2a2030));
  circle(x - 4 * s, y + 1.2f * s, 0.7f * s, rgb(0xff8fa8));
}

// The cat that woke: back arched, eyes wide.
static void angryCat(float x, float y, float s, uint32_t look) {
  uint32_t fur = FUR[look % 5];
  ellipse(x, y + 1 * s, 8 * s, 4.5f * s, rgb(fur));
  ellipse(x, y - 1 * s, 6 * s, 4.5f * s, rgb(fur));                          // arched back
  line(x + 7 * s, y, x + 9 * s, y - 8 * s, 2.2f * s, rgb(fur));              // tail straight up
  circle(x - 6 * s, y - 2 * s, 4.2f * s, rgb(fur));
  ear(x - 8.5f * s, y - 5 * s, s * 0.8f, -1, fur);
  ear(x - 3.5f * s, y - 5.5f * s, s * 0.8f, 1, fur);
  circle(x - 7.5f * s, y - 2.5f * s, 1.3f * s, rgb(0xfff27a));
  circle(x - 4.5f * s, y - 2.5f * s, 1.3f * s, rgb(0xfff27a));
  circle(x - 7.5f * s, y - 2.5f * s, 0.5f * s, rgb(0x000000));
  circle(x - 4.5f * s, y - 2.5f * s, 0.5f * s, rgb(0x000000));
}

static void yarn(float x, float y, float r, uint32_t c) {
  line(x + r * 0.6f, y + r * 0.6f, x + r * 1.5f, y + r * 1.1f, 1, rgb(c));   // the loose end
  circle(x, y, r, rgb(c));
  uint16_t d = rgb(mixHex(c, 0x000000, 0.3f));
  line(x - r * 0.8f, y - r * 0.3f, x + r * 0.5f, y + r * 0.8f, 1, d);
  line(x - r * 0.4f, y - r * 0.8f, x + r * 0.8f, y + r * 0.3f, 1, d);
  line(x - r * 0.9f, y + r * 0.3f, x + r * 0.2f, y - r * 0.9f, 1, d);
  circle(x - r * 0.35f, y - r * 0.35f, r * 0.25f, rgb(0xffffff), 140);
}

static void drawCell(int x, int y) {
  const Cell &c = s_b[y][x];
  float px = BX + x * CELL, py = BY + y * CELL;
  bool over = s_mode == Mode::Lost || s_mode == Mode::Won;
  if (!c.open && !(over && c.cat)) {
    // A cushion: two soft tones, a highlight and a tufted button.
    uint32_t base = ((x + y) & 1) ? 0xf7b6cf : 0xe9b8f0;
    roundRect(px + 1, py + 1, CELL - 2, CELL - 2, 6, rgb(mixHex(base, 0x6a3a6a, 0.35f)));
    roundRect(px + 1, py + 1, CELL - 3, CELL - 4, 6, rgb(base));
    roundRect(px + 4, py + 3, CELL - 12, 4, 2, rgb(0xffffff), 110);
    circle(px + CELL / 2, py + CELL / 2, 1.6f, rgb(mixHex(base, 0x6a3a6a, 0.45f)));
    if (c.yarn) yarn(px + CELL / 2, py + CELL / 2 - 1, 6.5f, 0x5aa8f0);
    if (over && c.yarn && !c.cat) line(px + 5, py + 5, px + CELL - 5, py + CELL - 5, 2, rgb(0xe04a5a)); // wrong guess
    return;
  }
  bool woke = s_woke == y * COLS + x;
  uint32_t floor = woke ? 0xff8a8a : (((x + y) & 1) ? 0xfdf3e4 : 0xf6e8d4);
  rect(px + 0.5f, py + 0.5f, CELL - 1, CELL - 1, rgb(floor));
  if (c.cat) {
    if (woke) angryCat(px + CELL / 2 + 1, py + CELL / 2 + 1, 1.1f, c.look);
    else sleepingCat(px + CELL / 2 + 1, py + CELL / 2, 1.1f, c.look);
    if (c.yarn) yarn(px + CELL - 6, py + 6, 3.5f, 0x5aa8f0);
  } else if (c.n) {
    char b[2] = {(char)('0' + c.n), 0};
    txt((int)(px + CELL / 2 - 5), (int)(py + CELL / 2 - 7), b, 1, rgb(NUM[c.n - 1]));
  }
}

static void drawBoard() {
  rect(BX - 6, BY - 6, COLS * CELL + 12, ROWS * CELL + 12, rgb(0x8a5a3a)); // a wooden frame
  rect(BX - 3, BY - 3, COLS * CELL + 6, ROWS * CELL + 6, rgb(0xb07a4a));
  for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) drawCell(x, y);
  if (s_mode == Mode::Play || s_mode == Mode::Paused) {
    // The paw: a pulsing outline and a little paw print.
    float px = BX + s_cx * CELL, py = BY + s_cy * CELL;
    uint8_t a = (uint8_t)(170 + 80 * sinf(s_t * 6));
    roundRect(px - 1, py - 1, CELL + 2, 3, 1, rgb(0xff4f8a), a);
    roundRect(px - 1, py + CELL - 2, CELL + 2, 3, 1, rgb(0xff4f8a), a);
    roundRect(px - 1, py - 1, 3, CELL + 2, 1, rgb(0xff4f8a), a);
    roundRect(px + CELL - 2, py - 1, 3, CELL + 2, 1, rgb(0xff4f8a), a);
    circle(px + CELL - 5, py + CELL - 5, 2.2f, rgb(0xff4f8a));
    circle(px + CELL - 9, py + CELL - 8, 1, rgb(0xff4f8a));
    circle(px + CELL - 6, py + CELL - 10, 1, rgb(0xff4f8a));
    circle(px + CELL - 2.5f, py + CELL - 9, 1, rgb(0xff4f8a));
  }
}

static void card(float x, float y, float w, float h) {
  roundRect(x, y, w, h, 16, rgb(0xfff6fa), 242);
  roundRect(x, y, w, 4, 2, rgb(0xff8fb1));
}

static void render() {
  target(s_frame, FW, FH);
  setScale(1);
  // A soft rug of a background.
  for (int y = 0; y < FH; y++) {
    uint16_t c = mix(0xffe2ee, 0xe6d6ff, y / (float)FH);
    uint16_t *row = s_frame + y * FW;
    for (int x = 0; x < FW; x++) row[x] = ((x + y) % 10 == 0) ? blend(c, 0xffff, 120) : c;
  }
  setScale(K);
  drawBoard();

  char b[40];
  const uint16_t T_INK = rgb(0x5a2a4a), T_PINK = rgb(0xd23a72), T_SOFT = rgb(0x8a5a7a);
  // Top bar: cats left to find, the household, the clock.
  setScale(1);
  int left = HOUSES[s_house].cats - s_yarns;
  snprintf(b, sizeof(b), "%d", left);
  setScale(K);
  sleepingCat(BX + 10, 18, 1.0f, 0);
  setScale(1);
  txt((int)BX + 24, 10, b, 1, T_INK);
  txtC(W / 2, 10, HOUSES[s_house].name, 1, T_PINK);
  int secs = (int)s_clock;
  snprintf(b, sizeof(b), "%d:%02d", secs / 60, secs % 60);
  txt((int)(BX + COLS * CELL) - textW(b, 1), 10, b, 1, T_INK);

  setScale(K);
  switch (s_mode) {
    case Mode::Title: {
      card(70, 44, W - 140, 196);
      for (int i = 0; i < 3; i++) sleepingCat(W / 2 - 50 + i * 50, 92, 2.2f, i * 2 + 1);
      setScale(1);
      txtC(W / 2, 54, "CAT NAP", 2, T_PINK);
      txtC(W / 2, 124, "Lift the cushions - but", 1, T_INK);
      txtC(W / 2, 140, "don't wake the cats!", 1, T_INK);
      snprintf(b, sizeof(b), "<  %s  >", HOUSES[s_house].name);
      txtC(W / 2, 166, b, 1, T_PINK);
      txtC(W / 2, 182, HOUSES[s_house].blurb, 1, T_SOFT);
      if (s_best[s_house]) {
        snprintf(b, sizeof(b), "Best %lu.%lus", (unsigned long)(s_best[s_house] / 1000), (unsigned long)(s_best[s_house] / 100 % 10));
        txtC(W / 2, 198, b, 1, T_SOFT);
      }
      txtC(W / 2, 218, "SELECT play  UP back", 1, T_SOFT);
      break;
    }
    case Mode::Paused:
      card(100, 80, W - 200, 110);
      setScale(1);
      txtC(W / 2, 100, "PAUSED", 2, T_PINK);
      txtC(W / 2, 140, "SELECT go on", 1, T_SOFT);
      txtC(W / 2, 158, "UP new  DOWN quit", 1, T_SOFT);
      break;
    case Mode::Lost:
      if (s_modeT > 1.0f) {
        card(96, 92, W - 192, 96);
        setScale(1);
        txtC(W / 2, 104, "HISSS!", 2, rgb(0xe04a5a));
        txtC(W / 2, 140, "You woke a cat.", 1, T_INK);
        txtC(W / 2, 160, "SELECT again  UP menu", 1, T_SOFT);
      }
      break;
    case Mode::Won:
      if (s_modeT > 0.8f) {
        card(86, 86, W - 172, 108);
        for (int i = 0; i < 5; i++) {
          float hx = W / 2 - 60 + i * 30, hy = 108 + sinf(s_t * 3 + i) * 3;
          circle(hx - 2.5f, hy, 3.4f, rgb(0xff6f9a));
          circle(hx + 2.5f, hy, 3.4f, rgb(0xff6f9a));
          const float t[6] = {hx - 6, hy + 1, hx + 6, hy + 1, hx, hy + 8};
          poly(t, 3, rgb(0xff6f9a));
        }
        setScale(1);
        txtC(W / 2, 124, "ALL CATS NAPPING", 1, T_PINK);
        snprintf(b, sizeof(b), "Done in %d.%ds", (int)s_clock, (int)(s_clock * 10) % 10);
        txtC(W / 2, 144, b, 1, T_INK);
        txtC(W / 2, 168, "SELECT again  UP menu", 1, T_SOFT);
      }
      break;
    default: break;
  }
  setScale(1);
  HalfRes::present(s_panel, s_gfx->flipped(), s_frame);
}

// --- public -----------------------------------------------------------------------

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel) {
  if (s_active) return true;
  s_gfx = gfx;
  s_panel = panel;
  s_frame = (uint16_t *)heap_caps_malloc((size_t)FW * FH * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_frame) s_frame = (uint16_t *)heap_caps_malloc((size_t)FW * FH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_frame) {
    Serial.println("Cat Nap: not enough memory");
    return false;
  }
  s_prefs.begin("catnap", false);
  for (int i = 0; i < 3; i++) {
    char k[3] = {'b', (char)('0' + i), 0};
    s_best[i] = s_prefs.getUInt(k, 0);
  }
  s_rng = esp_random() | 1;
  newBoard();
  setMode(Mode::Title);
  s_exit = false;
  RunAudio::start();
  RunAudio::Params quiet;
  quiet.paused = true; // effects only
  RunAudio::set(quiet);
  s_active = true;
  return true;
}

void exit() {
  if (!s_active) return;
  RunAudio::stop();
  s_prefs.end();
  heap_caps_free(s_frame);
  s_frame = nullptr;
  s_active = false;
}

bool active() { return s_active; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }
bool upSleepAllowed() { return s_mode != Mode::Play; }
const uint16_t *frameBuffer() { return s_frame; }

void pause() {
  if (s_mode == Mode::Play) setMode(Mode::Paused);
}

static void move(int dx, int dy) {
  s_cx = (s_cx + dx + COLS) % COLS;
  s_cy = (s_cy + dy + ROWS) % ROWS;
}

void key(const InputEvent &ev) {
  if (!ev.pressed) {
    if (ev.key == Key::Down) s_downT = -1;
    return;
  }
  switch (s_mode) {
    case Mode::Title:
      if (ev.repeat) break;
      if (ev.key == Key::Left) s_house = (s_house + 2) % 3;
      else if (ev.key == Key::Right) s_house = (s_house + 1) % 3;
      else if (ev.key == Key::Select) { newBoard(); setMode(Mode::Play); }
      else if (ev.key == Key::Up) s_exit = true;
      break;
    case Mode::Play:
      if (ev.key == Key::Left) move(-1, 0);
      else if (ev.key == Key::Right) move(1, 0);
      else if (ev.key == Key::Up) move(0, -1);
      else if (ev.key == Key::Down) { move(0, 1); if (!ev.repeat) s_downT = 0; }
      else if (ev.key == Key::Select && !ev.repeat) lift(s_cx, s_cy);
      break;
    case Mode::Paused:
      if (ev.repeat) break;
      if (ev.key == Key::Select) setMode(Mode::Play);
      else if (ev.key == Key::Up) { newBoard(); setMode(Mode::Play); }
      else if (ev.key == Key::Down) setMode(Mode::Title);
      break;
    case Mode::Lost:
    case Mode::Won:
      if (ev.repeat) break;
      if (ev.key == Key::Select) { newBoard(); setMode(Mode::Play); }
      else if (ev.key == Key::Up) setMode(Mode::Title);
      break;
  }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  static uint32_t last = 0;
  uint32_t now = millis();
  float dt = last ? (now - last) / 1000.0f : 0.02f;
  last = now;
  if (dt > 0.1f) dt = 0.1f;
  s_t += dt;
  s_modeT += dt;

  if (s_mode == Mode::Play) {
    if (s_laid) s_clock += dt;
    // Two wheel clicks put yarn on (or take it off) the cushion under the paw.
    static int32_t wacc = 0;
    wacc += wheel;
    if (wacc >= 2 || wacc <= -2) { wacc = 0; toggleYarn(); }
    // Holding DOWN pauses (a press still moves the paw first).
    if (s_downT >= 0 && input.held(Key::Down)) {
      s_downT += dt;
      if (s_downT >= PAUSE_HOLD) { s_downT = -1; s_cy = (s_cy + ROWS - 1) % ROWS; setMode(Mode::Paused); }
    }
    if (touch.justPressed()) {
      int16_t lx, ly;
      s_gfx->touchToLandscape(touch.x(), touch.y(), lx, ly);
      int x = (int)((lx - BX) / CELL), y = (int)((ly - BY) / CELL);
      if (lx >= BX && ly >= BY && inside(x, y)) { s_cx = x; s_cy = y; lift(x, y); }
      else if (ly < BY) pause();
    }
  } else if (touch.justPressed()) {
    InputEvent tap = {Key::Select, true, false};
    key(tap);
  }
  render();
}

} // namespace CatNap
