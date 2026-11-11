#include "CypressNight.h"
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

namespace CypressNight {

// ---------------------------------------------------------------------------
// The garden: 28 x 15 tiles of 16 units, shown 1.5x bigger than would fit,
// so a camera follows the writer. Below a 36 px band of night sky.
// ---------------------------------------------------------------------------

static const int W = HalfRes::W, H = HalfRes::H, FW = HalfRes::FW, FH = HalfRes::FH;
static const int MW = 28, MH = 15, TILE = 16;  // garden units: 16 per tile
static const float ZOOM = 1.5f;                // screen px per garden unit
static const float K = 0.5f * ZOOM;            // frame px per garden unit (frame is half-res)
static const int SKY = 36, SKY_ROWS = SKY / 2; // the sky band, screen px / frame rows
static const int BW = (int)(MW * TILE * K), BH = (int)(MH * TILE * K); // whole garden: 336 x 180
static const int VIEW_ROWS = FH - SKY_ROWS;    // frame rows showing the garden
static const float VIEW_W = FW / K, VIEW_H = VIEW_ROWS / K; // garden units in view
static const float SKY_U = SKY / ZOOM;         // the sky band in garden units
static const float STEP = 1.0f / 60;

//  # cypress   ~ pool   . page   o lantern   1-4 owl roosts   P the writer
static const char *const MAZE[MH] = {
    "############################",
    "#1.........#.o..#.........2#",
    "#.###.####.#....#.####.###.#",
    "#............##............#",
    "#.#.###.###..##..###.###.#.#",
    "#.#......................#.#",
    "#...#.###..~~~~~~..###.#...#",
    "#o..#......~~~~~~......#..o#",
    "#...#.###..~~~~~~..###.#...#",
    "#.#..........P...........#.#",
    "#.#.###.###..##..###.###.#.#",
    "#............##............#",
    "#.###.####.#....#.####.###.#",
    "#3.........#..o.#.........4#",
    "############################",
};

enum Cell : uint8_t { PATH, TREE, POOL };
enum Item : uint8_t { NOTHING, PAGE, LANTERN };
enum Dir : int8_t { NONE = -1, RIGHT = 0, DOWN = 1, LEFT = 2, UP = 3 };
static const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
static Dir opposite(Dir d) { return d == NONE ? NONE : (Dir)((d + 2) & 3); }

enum class Mode : uint8_t { Title, Ready, Play, Dying, Cleared, Paused, Over };
enum OwlState : uint8_t { ASLEEP, HUNT, DAZZLED, ROOST };

// Grid movement: on tile (tx, ty), `prog` of the way to the next tile in `dir`.
struct Actor {
  int8_t tx, ty;
  Dir dir;
  float prog;
};

struct Owl {
  Actor a;
  OwlState state;
  float timer;      // ASLEEP: until it wakes; DAZZLED / ROOST: until it hunts again
  int8_t homeX, homeY;
  uint8_t patrol;   // the pool owl's next corner
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static bool s_active = false, s_exit = false;
static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static Input *s_input = nullptr;
static uint16_t *s_frame = nullptr; // internal RAM when it fits: drawn into every frame
static uint16_t *s_bg = nullptr;    // the whole garden, drawn once a night (BW x BH)
static uint16_t *s_sky = nullptr;   // the sky band (FW x SKY_ROWS)
static float s_camX = 0, s_camY = 0;       // top-left of the view, garden units (pixel-aligned)
static float s_camRawX = 0, s_camRawY = 0; // ...before aligning, so it glides
static bool s_camSnap = true;
static Preferences s_prefs;

static uint8_t s_cell[MH][MW];
static uint8_t s_item[MH][MW];
static int s_itemsLeft = 0, s_pagesTaken = 0;
static int8_t s_startX = 13, s_startY = 9;

static Mode s_mode = Mode::Title, s_pausedFrom = Mode::Play;
static float s_t = 0, s_modeT = 0;
static Actor s_me;
static Dir s_want = NONE;
static Owl s_owls[4];
static int s_level = 1, s_lives = 3;
static uint32_t s_score = 0, s_best = 0, s_nextLife = 10000;
static int s_combo = 0;
static float s_fruitT = 0; // pomegranate on show while > 0
static int s_fruitsShown = 0;

struct Popup { float x, y, t; char text[8]; };
static Popup s_pops[4];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool open(int x, int y) { return x >= 0 && y >= 0 && x < MW && y < MH && s_cell[y][x] == PATH; }
static float posX(const Actor &a) { return a.tx + (a.dir == NONE ? 0 : DX[a.dir] * a.prog); }
static float posY(const Actor &a) { return a.ty + (a.dir == NONE ? 0 : DY[a.dir] * a.prog); }
static float wx(float tx) { return tx * TILE + TILE / 2; } // tile -> garden units (centre)
static float wy(float ty) { return ty * TILE + TILE / 2; }
// Tile -> where it is in view, in garden units (drawn with scale K).
static float sx(float tx) { return wx(tx) - s_camX; }
static float sy(float ty) { return wy(ty) - s_camY + SKY_U; }
static uint32_t rnd(uint32_t n) { return esp_random() % n; }

// The camera keeps the writer in the middle, stopping at the garden's edges.
static void aimCamera(float dt) {
  float tx = wx(posX(s_me)) - VIEW_W / 2, ty = wy(posY(s_me)) - VIEW_H / 2;
  float maxX = MW * TILE - VIEW_W, maxY = MH * TILE - VIEW_H;
  tx = tx < 0 ? 0 : tx > maxX ? maxX : tx;
  ty = ty < 0 ? 0 : ty > maxY ? maxY : ty;
  if (s_camSnap) {
    s_camRawX = tx;
    s_camRawY = ty;
    s_camSnap = false;
  } else {
    float k = 1 - expf(-dt * 7);
    s_camRawX += (tx - s_camRawX) * k;
    s_camRawY += (ty - s_camRawY) * k;
  }
  // Whole frame pixels only, so the characters sit exactly on the garden.
  s_camX = roundf(s_camRawX * K) / K;
  s_camY = roundf(s_camRawY * K) / K;
}

static void pop(float tx, float ty, uint32_t pts) {
  for (Popup &p : s_pops) {
    if (p.t > 0) continue;
    p.x = wx(tx);
    p.y = wy(ty) - 6;
    p.t = 1.0f;
    snprintf(p.text, sizeof(p.text), "%lu", (unsigned long)pts);
    return;
  }
}

static void addScore(uint32_t pts) {
  s_score += pts;
  if (s_score >= s_nextLife) {
    s_nextLife += 10000;
    if (s_lives < 6) s_lives++;
    RunAudio::fx(RunAudio::DingTop);
  }
}

// --- text, straight into the half-res frame (glcdfont, with a shadow) -------

static const uint16_t SHADOW = 0x0841;

static void glyph(int lx, int ly, unsigned char ch, int size, uint16_t col) {
  for (int cx = 0; cx < 5; cx++) {
    uint8_t bits = font[ch * 5 + cx];
    for (int cy = 0; cy < 8; cy++, bits >>= 1) {
      if (!(bits & 1)) continue;
      int x0 = lx + cx * size, y0 = ly + cy * size;
      for (int yy = y0; yy < y0 + size; yy++) {
        if (yy < 0 || yy >= FH) continue;
        for (int xx = x0; xx < x0 + size; xx++)
          if (xx >= 0 && xx < FW) s_frame[yy * FW + xx] = col;
      }
    }
  }
}

static int textW(const char *s, int size) { return (int)strlen(s) * 12 * size; }

static void txt(int x, int y, const char *s, int size, uint16_t col) {
  int lx = x / 2, ly = y / 2;
  for (const char *p = s; *p; p++, lx += 6 * size) {
    glyph(lx + 1, ly + 1, (unsigned char)*p, size, SHADOW);
    glyph(lx, ly, (unsigned char)*p, size, col);
  }
}
static void txtC(int cx, int y, const char *s, int size, uint16_t col) { txt(cx - textW(s, size) / 2, y, s, size, col); }

// ---------------------------------------------------------------------------
// The garden, drawn once per night into s_bg
// ---------------------------------------------------------------------------

static uint32_t hash(int x, int y) {
  uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
  h ^= h >> 13;
  return h * 0x5bd1e995;
}

static void drawCypress(float cx, float cy, uint32_t h) {
  static const uint32_t greens[3] = {0x1d5a3b, 0x195236, 0x226a43};
  uint32_t g = greens[h % 3];
  ellipse(cx + 1.5f, cy + 7, 6, 2.5f, rgb(0x0b1a14), 140);   // shadow on the ground
  rect(cx - 1, cy + 4, 2, 4, rgb(0x4b3221));                 // trunk
  ellipse(cx, cy - 2, 6.2f, 10, rgb(g));                     // the tall flame of the tree
  ellipse(cx - 2, cy - 3, 2.4f, 7, rgb(mixHex(g, 0x6fbf7f, 0.35f)), 200); // moonlit side
  ellipse(cx + 2.5f, cy, 2.2f, 6, rgb(mixHex(g, 0x000000, 0.35f)), 160);  // shade
}

// Night sky band with stars and a crescent moon (screen px, half-res).
static void drawSky() {
  target(s_sky, FW, SKY_ROWS);
  setScale(0.5f);
  for (int y = 0; y < SKY; y++) rect(0, y, W, 1, mix(0x070b24, 0x1a1d46, y / (float)SKY));
  for (int i = 0; i < 40; i++) {
    uint32_t h = hash(i, 7);
    circle((float)(h % W), (float)((h >> 9) % (SKY - 4)) + 2, (h >> 20) % 3 == 0 ? 1.2f : 0.7f, rgb(0xfff6d8));
  }
  circle(W - 120, 17, 9, rgb(0xfff2c4));
  circle(W - 115, 14, 8, mix(0x070b24, 0x1a1d46, 0.4f));
  setScale(1);
}

// The whole garden at zoomed size, in garden units (the camera picks the
// part that's shown).
static void drawGarden() {
  target(s_bg, BW, BH);
  setScale(K);
  // Ground: grass between the paths, raked gravel on them.
  rect(0, 0, MW * TILE, MH * TILE, rgb(0x10261f));
  for (int y = 0; y < MH; y++) {
    for (int x = 0; x < MW; x++) {
      if (s_cell[y][x] != PATH) continue;
      roundRect(x * TILE - 1, y * TILE - 1, TILE + 2, TILE + 2, 4, rgb(0x2a3a33));
    }
  }

  // The pool (hoz): stone rim, water, the moon's reflection.
  float px = 11 * TILE, py = 6 * TILE, pw = 6 * TILE, ph = 3 * TILE;
  roundRect(px + 1, py + 1, pw - 2, ph - 2, 6, rgb(0xc9b99a));
  roundRect(px + 4, py + 4, pw - 8, ph - 8, 4, rgb(0x1d5f7a));
  for (int i = 0; i < 4; i++) rect(px + 10 + i * 20, py + 14 + (i & 1) * 12, 12, 1.5f, rgb(0x3aa0b8), 180);
  ellipse(px + pw - 26, py + ph / 2, 6, 3, rgb(0xe8e2c0), 140);

  // Cypresses, top to bottom so the lower ones overlap those behind.
  for (int y = 0; y < MH; y++)
    for (int x = 0; x < MW; x++)
      if (s_cell[y][x] == TREE) drawCypress(wx(x), wy(y), hash(x, y));
  setScale(1);
}

// ---------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------

// At half resolution every screen pixel here is half a real one, so the
// glasses are drawn big and well apart - they're what makes him him.
static void drawWriter(float cx, float cy, Dir d, float spin) {
  float lx = d == NONE ? 0 : DX[d] * 1.0f, ly = d == NONE ? 0 : DY[d] * 0.8f;
  if (spin > 0) { lx = sinf(spin * 18) * 2.5f; cy += spin * 4; }
  float bob = (s_mode == Mode::Play && s_me.dir != NONE) ? fabsf(sinf(s_t * 14)) * 1.2f : 0;
  cy -= bob;
  roundRect(cx - 8, cy + 3, 16, 9, 4, rgb(0x2b2d3a));            // dark coat
  rect(cx - 1.5f, cy + 3, 3, 5, rgb(0xe8e4dc));                  // collar
  circle(cx, cy - 3, 8.5f, rgb(0xe9c4a0));                       // face
  ellipse(cx - 0.5f, cy - 10, 8.5f, 3.5f, rgb(0x1a1a1a));        // hair, combed back
  float gy = cy - 3.5f + ly;
  for (int s = -1; s <= 1; s += 2) {                             // the round glasses
    circle(cx + s * 4.3f + lx, gy, 3.6f, rgb(0x101010));
    circle(cx + s * 4.3f + lx, gy, 2.1f, rgb(0xd8eef6));
  }
  rect(cx - 1 + lx, gy - 0.7f, 2, 1.4f, rgb(0x101010));          // bridge
  ellipse(cx + lx * 0.8f, cy + 2.2f + ly * 0.5f, 3.4f, 1.3f, rgb(0x1a1a1a)); // moustache
}

static const uint32_t OWL_BODY[4] = {0x8a6440, 0x8c8c92, 0xe4e2da, 0xa5552e};
static const uint32_t OWL_BELLY[4] = {0xc9a477, 0xc4c4c8, 0xffffff, 0xd99a6a};

static void drawOwl(const Owl &o, int i) {
  float cx = sx(posX(o.a)), cy = sy(posY(o.a));
  bool pale = o.state == DAZZLED && !(o.timer < 1.5f && fmodf(s_t, 0.3f) < 0.15f);
  uint32_t body = pale ? 0xbcd6ff : OWL_BODY[i], belly = pale ? 0xe8f0ff : OWL_BELLY[i];
  if (o.state == HUNT || o.state == DAZZLED) cy += sinf(s_t * 10 + i) * 1.0f; // wings beating
  float t1[] = {cx - 7, cy - 5, cx - 6, cy - 12, cx - 2, cy - 7};
  float t2[] = {cx + 7, cy - 5, cx + 6, cy - 12, cx + 2, cy - 7};
  poly(t1, 3, rgb(body));                                       // ear tufts
  poly(t2, 3, rgb(body));
  ellipse(cx, cy, 7.5f, 8.5f, rgb(body));
  ellipse(cx, cy + 3, 4.5f, 5, rgb(belly));
  bool closed = o.state == ASLEEP || o.state == ROOST;
  float lx = 0, ly = 0;
  if (o.state == HUNT) {
    float dx = posX(s_me) - posX(o.a), dy = posY(s_me) - posY(o.a), d = sqrtf(dx * dx + dy * dy) + 0.01f;
    lx = dx / d * 1.2f, ly = dy / d * 1.2f;
  }
  for (int s = -1; s <= 1; s += 2) {
    float ex = cx + s * 3.2f, ey = cy - 3;
    if (closed || pale) {
      rect(ex - 2.2f, ey, 4.4f, 1.2f, rgb(0x2a1a10));            // shut / squinting
    } else {
      circle(ex, ey, 3, rgb(0xfff4c2));
      circle(ex + lx, ey + ly, 1.5f, rgb(0x121212));
    }
  }
  float beak[] = {cx - 1.4f, cy - 0.8f, cx + 1.4f, cy - 0.8f, cx, cy + 2};
  poly(beak, 3, rgb(0xe0a030));
}

static void drawLantern(float cx, float cy) {
  float glow = 6 + sinf(s_t * 5) * 1.5f;
  circle(cx, cy, glow + 2, rgb(0xffb347), 60);
  circle(cx, cy, glow - 1, rgb(0xffc76b), 90);
  roundRect(cx - 2.5f, cy - 3.5f, 5, 7, 1.5f, rgb(0xffe08a));
  rect(cx - 3, cy - 5, 6, 1.5f, rgb(0x5a3b1a));
  rect(cx - 3, cy + 3.5f, 6, 1.5f, rgb(0x5a3b1a));
}

static void drawPomegranate(float cx, float cy) {
  circle(cx, cy + 1, 6, rgb(0xc0283a));
  float crown[] = {cx - 2.5f, cy - 4, cx, cy - 8, cx + 2.5f, cy - 4};
  poly(crown, 3, rgb(0x7a1020));
  circle(cx - 2, cy - 1, 1.6f, rgb(0xff8a8a), 170);
}

static void drawGlassesIcon(float cx, float cy, uint16_t c) {
  circle(cx - 4, cy, 3.2f, c);
  circle(cx + 4, cy, 3.2f, c);
  circle(cx - 4, cy, 1.8f, rgb(0x141838));
  circle(cx + 4, cy, 1.8f, rgb(0x141838));
  rect(cx - 1, cy - 0.6f, 2, 1.2f, c);
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

static void resetItems() {
  s_itemsLeft = 0;
  s_pagesTaken = 0;
  s_fruitsShown = 0;
  s_fruitT = 0;
  for (int y = 0; y < MH; y++) {
    for (int x = 0; x < MW; x++) {
      char c = MAZE[y][x];
      s_cell[y][x] = c == '#' ? TREE : c == '~' ? POOL : PATH;
      s_item[y][x] = c == '.' ? PAGE : c == 'o' ? LANTERN : NOTHING;
      if (s_item[y][x]) s_itemsLeft++;
      if (c == 'P') { s_startX = x; s_startY = y; }
      if (c >= '1' && c <= '4') {
        Owl &o = s_owls[c - '1'];
        o.homeX = x;
        o.homeY = y;
      }
    }
  }
}

static void resetActors() {
  s_me = {s_startX, s_startY, NONE, 0};
  s_want = NONE;
  // The owls wake one after another; sooner on later nights.
  float gap = fmaxf(1.5f, 4.0f - (s_level - 1) * 0.5f);
  for (int i = 0; i < 4; i++) {
    Owl &o = s_owls[i];
    o.a = {o.homeX, o.homeY, NONE, 0};
    o.state = ASLEEP;
    o.timer = 1.0f + i * gap;
    o.patrol = i;
  }
  s_camSnap = true; // jump straight to him, don't glide from the old spot
  s_combo = 0;
  for (Popup &p : s_pops) p.t = 0;
}

static void setMode(Mode m) {
  s_mode = m;
  s_modeT = 0;
}

static void newGame() {
  s_level = 1;
  s_lives = 3;
  s_score = 0;
  s_nextLife = 10000;
  resetItems();
  drawGarden();
  resetActors();
  setMode(Mode::Ready);
}

static float meSpeed() { return fminf(7.5f, 6.0f * (1 + 0.05f * (s_level - 1))); }
static float owlSpeed(const Owl &o) {
  if (o.state == DAZZLED) return 3.2f;
  return fminf(7.2f, 5.0f * (1 + 0.07f * (s_level - 1)));
}
static float dazzleTime() { return fmaxf(2.5f, 7.0f - (s_level - 1) * 0.6f); }

static void eat(int x, int y) {
  uint8_t &it = s_item[y][x];
  if (it == PAGE) {
    addScore(10);
    s_pagesTaken++;
    RunAudio::fx(RunAudio::Tick);
    // A pomegranate appears under the pool twice a night.
    if ((s_pagesTaken == 70 || s_pagesTaken == 160) && s_fruitsShown < 2) {
      s_fruitsShown++;
      s_fruitT = 9;
    }
  } else if (it == LANTERN) {
    addScore(50);
    RunAudio::fx(RunAudio::DingHigh);
    s_combo = 0;
    for (Owl &o : s_owls) {
      if (o.state == HUNT || o.state == DAZZLED) {
        if (o.state == HUNT && o.a.dir != NONE && o.a.prog > 0) { // turn round on the spot
          o.a.tx += DX[o.a.dir];
          o.a.ty += DY[o.a.dir];
          o.a.prog = 1 - o.a.prog;
          o.a.dir = opposite(o.a.dir);
        }
        o.state = DAZZLED;
        o.timer = dazzleTime();
      }
    }
  } else {
    return;
  }
  it = NOTHING;
  if (--s_itemsLeft == 0) {
    RunAudio::fx(RunAudio::Ding);
    setMode(Mode::Cleared);
  }
}

// Where each owl is heading. They hunt in their own ways.
static void owlTarget(const Owl &o, int i, int &tx, int &ty) {
  int px = s_me.tx, py = s_me.ty;
  int dist = abs(px - o.a.tx) + abs(py - o.a.ty);
  switch (i) {
    case 0: tx = px; ty = py; break;                         // the hunter: straight at him
    case 1: {                                                // the ambusher: where he's going
      Dir d = s_me.dir != NONE ? s_me.dir : s_want;
      tx = px + (d == NONE ? 0 : DX[d] * 4);
      ty = py + (d == NONE ? 0 : DY[d] * 4);
      break;
    }
    case 2: {                                                // circles the pool until he's near
      static const int8_t PX[4] = {10, 17, 17, 10}, PY[4] = {5, 5, 9, 9};
      if (dist < 6) { tx = px; ty = py; break; }
      tx = PX[o.patrol & 3];
      ty = PY[o.patrol & 3];
      break;
    }
    default:                                                 // the wanderer: only hunts up close
      if (dist < 5) { tx = px; ty = py; }
      else { tx = -1; ty = -1; }
      break;
  }
}

static Dir choose(Actor &a, int who) {
  if (who < 0) { // the writer: the way he wants, else straight on, else stop
    if (s_want != NONE && open(a.tx + DX[s_want], a.ty + DY[s_want])) return s_want;
    if (a.dir != NONE && open(a.tx + DX[a.dir], a.ty + DY[a.dir])) return a.dir;
    return NONE;
  }
  Owl &o = s_owls[who];
  if (who == 2 && o.state == HUNT) {
    static const int8_t PX[4] = {10, 17, 17, 10}, PY[4] = {5, 5, 9, 9};
    if (a.tx == PX[o.patrol & 3] && a.ty == PY[o.patrol & 3]) o.patrol++;
  }
  Dir opts[4];
  int n = 0;
  for (int d = 0; d < 4; d++) {
    if ((Dir)d == opposite(a.dir)) continue; // owls don't double back...
    if (open(a.tx + DX[d], a.ty + DY[d])) opts[n++] = (Dir)d;
  }
  if (n == 0) return opposite(a.dir); // ...unless they must
  int tx, ty;
  if (o.state == DAZZLED) tx = ty = -1;
  else owlTarget(o, who, tx, ty);
  if (tx < 0) return opts[rnd(n)];
  Dir best = opts[0];
  int bestD = 1 << 30;
  for (int k = 0; k < n; k++) {
    int nx = a.tx + DX[opts[k]], ny = a.ty + DY[opts[k]];
    int d2 = (nx - tx) * (nx - tx) + (ny - ty) * (ny - ty);
    if (d2 < bestD) { bestD = d2; best = opts[k]; }
  }
  return best;
}

// Moves an actor `dist` tiles, deciding at each tile centre.
static void walk(Actor &a, float dist, int who) {
  while (dist > 0) {
    if (a.dir == NONE) {
      a.dir = choose(a, who);
      a.prog = 0;
      if (a.dir == NONE) return;
    }
    float left = 1 - a.prog;
    if (dist < left) {
      a.prog += dist;
      return;
    }
    dist -= left;
    a.tx += DX[a.dir];
    a.ty += DY[a.dir];
    a.prog = 0;
    if (who < 0) {
      eat(a.tx, a.ty);
      if (s_mode != Mode::Play) return;
    }
    Dir nd = choose(a, who);
    if (nd == NONE || !open(a.tx + DX[nd], a.ty + DY[nd])) {
      a.dir = NONE;
      return;
    }
    a.dir = nd;
  }
}

static void update(float dt) {
  s_t += dt;
  s_modeT += dt;
  aimCamera(dt);
  for (Popup &p : s_pops) if (p.t > 0) { p.t -= dt; p.y -= dt * 14; }

  switch (s_mode) {
    case Mode::Ready:
      if (s_modeT > 1.8f) setMode(Mode::Play);
      return;
    case Mode::Dying:
      if (s_modeT > 1.6f) {
        if (--s_lives <= 0) {
          if (s_score > s_best) { s_best = s_score; s_prefs.putUInt("best", s_best); }
          setMode(Mode::Over);
        } else {
          resetActors();
          setMode(Mode::Ready);
        }
      }
      return;
    case Mode::Cleared:
      if (s_modeT > 2.2f) {
        s_level++;
        resetItems();
        drawGarden();
        resetActors();
        setMode(Mode::Ready);
      }
      return;
    case Mode::Play: break;
    default: return;
  }

  // The writer. He can turn round at any moment.
  if (s_want != NONE && s_want == opposite(s_me.dir) && s_me.prog > 0) {
    s_me.tx += DX[s_me.dir];
    s_me.ty += DY[s_me.dir];
    s_me.prog = 1 - s_me.prog;
    s_me.dir = s_want;
  }
  walk(s_me, meSpeed() * dt, -1);
  if (s_mode != Mode::Play) return;

  if (s_fruitT > 0) {
    s_fruitT -= dt;
    if (s_me.tx == 13 && s_me.ty == 5 && s_me.prog < 0.5f) {
      uint32_t pts = 100 + s_level * 100;
      addScore(pts);
      pop(13, 5, pts);
      RunAudio::fx(RunAudio::Squeak);
      s_fruitT = 0;
    }
  }

  // The owls.
  for (int i = 0; i < 4; i++) {
    Owl &o = s_owls[i];
    if (o.state == ASLEEP || o.state == ROOST) {
      o.timer -= dt;
      if (o.timer <= 0) {
        o.state = HUNT;
        RunAudio::fx(RunAudio::Hoot);
      }
      continue;
    }
    if (o.state == DAZZLED) {
      o.timer -= dt;
      if (o.timer <= 0) o.state = HUNT;
    }
    walk(o.a, owlSpeed(o) * dt, i);

    float dx = posX(o.a) - posX(s_me), dy = posY(o.a) - posY(s_me);
    if (dx * dx + dy * dy < 0.55f * 0.55f) {
      if (o.state == DAZZLED) {
        uint32_t pts = 200u << (s_combo < 3 ? s_combo : 3);
        s_combo++;
        addScore(pts);
        pop(posX(o.a), posY(o.a), pts);
        RunAudio::fx(RunAudio::DingTop);
        o.a = {o.homeX, o.homeY, NONE, 0}; // back to its roost for a rest
        o.state = ROOST;
        o.timer = 3;
      } else if (o.state == HUNT) {
        RunAudio::fx(RunAudio::Bonk);
        RunAudio::fx(RunAudio::Hoot);
        setMode(Mode::Dying);
        return;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing a frame
// ---------------------------------------------------------------------------

static void overlay(float y0, float h) {
  roundRect(60, y0, W - 120, h, 18, rgb(0x0b0f2a), 200);
  roundRect(60, y0, W - 120, 2, 1, rgb(0xffb347), 160);
}

static void render() {
  // The part of the garden in view, row by row out of the whole garden.
  int ix = (int)lroundf(s_camX * K), iy = (int)lroundf(s_camY * K);
  for (int r = 0; r < VIEW_ROWS; r++)
    memcpy(s_frame + (SKY_ROWS + r) * FW, s_bg + (iy + r) * BW + ix, FW * 2);
  target(s_frame, FW, FH);
  setScale(K);

  // Pages and lanterns.
  for (int y = 0; y < MH; y++) {
    for (int x = 0; x < MW; x++) {
      if (s_item[y][x] == PAGE) {
        rect(sx(x) - 2, sy(y) - 2.5f, 4, 5, rgb(0xefe6cf));
      } else if (s_item[y][x] == LANTERN) {
        drawLantern(sx(x), sy(y));
      }
    }
  }
  if (s_fruitT > 0 && (s_fruitT > 2 || fmodf(s_t, 0.3f) < 0.18f)) drawPomegranate(sx(13), sy(5));

  if (s_input) s_input->poll(); // a slow frame shouldn't swallow a quick tap

  if (s_mode != Mode::Title) {
    for (int i = 0; i < 4; i++) drawOwl(s_owls[i], i);
    if (s_mode != Mode::Over) {
      float spin = s_mode == Mode::Dying ? s_modeT : 0;
      drawWriter(sx(posX(s_me)), sy(posY(s_me)), s_me.dir != NONE ? s_me.dir : s_want, spin);
    }
  }
  setScale(1);

  for (const Popup &p : s_pops)
    if (p.t > 0) txtC((int)((p.x - s_camX) * ZOOM), (int)((p.y - s_camY) * ZOOM) + SKY, p.text, 1, rgb(0xffe08a));

  // The sky band goes on last, over anything that strayed up into it.
  memcpy(s_frame, s_sky, (size_t)FW * SKY_ROWS * 2);

  // The sky band: score, night, lives.
  char b[32];
  snprintf(b, sizeof(b), "%lu", (unsigned long)s_score);
  txt(14, 10, b, 1, rgb(0xfff6d8));
  if (s_mode != Mode::Title) {
    snprintf(b, sizeof(b), "NIGHT %d", s_level);
    txtC(W / 2, 10, b, 1, rgb(0xc9c4f0));
    setScale(0.5f);
    for (int i = 0; i < s_lives && i < 6; i++) drawGlassesIcon(W - 22 - i * 20, 18, rgb(0xe9c4a0));
    setScale(1);
  } else {
    snprintf(b, sizeof(b), "BEST %lu", (unsigned long)s_best);
    txtC(W / 2, 10, b, 1, rgb(0xc9c4f0));
  }

  setScale(0.5f);
  switch (s_mode) {
    case Mode::Title:
      overlay(70, 150);
      setScale(1);
      txtC(W / 2, 88, "CYPRESS NIGHT", 2, rgb(0xffe08a));
      txtC(W / 2, 128, "Gather the pages.", 1, rgb(0xfff6d8));
      txtC(W / 2, 146, "Beware the owls.", 1, rgb(0xfff6d8));
      txtC(W / 2, 184, "SELECT start   UP back", 1, rgb(0xc9c4f0));
      break;
    case Mode::Ready:
      setScale(1);
      snprintf(b, sizeof(b), "NIGHT %d", s_level);
      txtC(W / 2, 120, b, 2, rgb(0xffe08a));
      txtC(W / 2, 160, "READY", 1, rgb(0xfff6d8));
      break;
    case Mode::Cleared:
      setScale(1);
      txtC(W / 2, 130, "DAWN!", 2, rgb(0xffe08a));
      break;
    case Mode::Paused:
      overlay(90, 100);
      setScale(1);
      txtC(W / 2, 108, "PAUSED", 2, rgb(0xffe08a));
      txtC(W / 2, 152, "SELECT resume   DOWN quit", 1, rgb(0xc9c4f0));
      break;
    case Mode::Over:
      overlay(70, 150);
      setScale(1);
      txtC(W / 2, 88, "THE OWLS WIN", 2, rgb(0xffe08a));
      snprintf(b, sizeof(b), "SCORE %lu", (unsigned long)s_score);
      txtC(W / 2, 128, b, 1, rgb(0xfff6d8));
      snprintf(b, sizeof(b), "BEST %lu", (unsigned long)s_best);
      txtC(W / 2, 146, b, 1, rgb(0xfff6d8));
      txtC(W / 2, 184, "SELECT again   UP back", 1, rgb(0xc9c4f0));
      break;
    default: break;
  }
  setScale(1);

  HalfRes::present(s_panel, s_gfx->flipped(), s_frame);
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

static void *bigAlloc(size_t bytes, bool preferInternal) {
  if (preferInternal && heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > bytes + 90 * 1024) {
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) return p;
  }
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel) {
  if (s_active) return true;
  s_gfx = gfx;
  s_panel = panel;
  s_frame = (uint16_t *)bigAlloc((size_t)FW * FH * 2, true);
  s_bg = (uint16_t *)bigAlloc((size_t)BW * BH * 2, false);
  s_sky = (uint16_t *)bigAlloc((size_t)FW * SKY_ROWS * 2, false);
  if (!s_frame || !s_bg || !s_sky) {
    heap_caps_free(s_frame); s_frame = nullptr;
    heap_caps_free(s_bg); s_bg = nullptr;
    heap_caps_free(s_sky); s_sky = nullptr;
    Serial.println("Cypress Night: not enough memory");
    return false;
  }
  s_prefs.begin("cypress", false);
  s_best = s_prefs.getUInt("best", 0);
  s_level = 1;
  s_score = 0;
  s_lives = 3;
  resetItems();
  drawSky();
  drawGarden();
  resetActors();
  setMode(Mode::Title);
  s_exit = false;
  RunAudio::start();
  RunAudio::Params quiet;
  quiet.paused = true; // effects only, no engine
  RunAudio::set(quiet);
  s_active = true;
  return true;
}

void exit() {
  if (!s_active) return;
  RunAudio::stop();
  if (s_score > s_best) s_prefs.putUInt("best", s_score);
  s_prefs.end();
  heap_caps_free(s_frame); s_frame = nullptr;
  heap_caps_free(s_bg); s_bg = nullptr;
  heap_caps_free(s_sky); s_sky = nullptr;
  s_active = false;
}

bool active() { return s_active; }
const uint16_t *frameBuffer() { return s_frame; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }

void pause() {
  if (s_mode == Mode::Play || s_mode == Mode::Ready) {
    s_pausedFrom = s_mode;
    setMode(Mode::Paused);
  }
}

void key(const InputEvent &ev) {
  if (!ev.pressed || ev.repeat) return;
  Dir d = ev.key == Key::Up ? UP : ev.key == Key::Down ? DOWN : ev.key == Key::Left ? LEFT
        : ev.key == Key::Right ? RIGHT : NONE;
  switch (s_mode) {
    case Mode::Title:
      if (ev.key == Key::Select) newGame();
      else if (ev.key == Key::Up) s_exit = true;
      break;
    case Mode::Over:
      if (ev.key == Key::Select) newGame();
      else if (ev.key == Key::Up) setMode(Mode::Title);
      break;
    case Mode::Paused:
      if (ev.key == Key::Select) setMode(s_pausedFrom);
      else if (ev.key == Key::Down) {
        if (s_score > s_best) { s_best = s_score; s_prefs.putUInt("best", s_best); }
        setMode(Mode::Title);
      }
      break;
    case Mode::Play:
    case Mode::Ready:
      if (ev.key == Key::Select) pause();
      else if (d != NONE) s_want = d;
      break;
    default: break;
  }
}

void frame(Input &input, TouchInput &touch, int32_t wheel) {
  if (!s_active) return;
  s_input = &input;
  static uint32_t last = 0;
  static float acc = 0;
  uint32_t now = millis();
  float dt = last ? (now - last) / 1000.0f : STEP;
  last = now;
  if (dt > 0.1f) dt = 0.1f;

  if (s_mode == Mode::Play || s_mode == Mode::Ready) {
    // The wheel turns him: a click clockwise = turn right, anticlockwise = left.
    static int32_t wacc = 0;
    wacc += wheel;
    Dir base = s_want != NONE ? s_want : (s_me.dir != NONE ? s_me.dir : LEFT);
    while (wacc >= 2) { wacc -= 2; s_want = base = (Dir)((base + 1) & 3); }
    while (wacc <= -2) { wacc += 2; s_want = base = (Dir)((base + 3) & 3); }
    // An arrow still held keeps steering (handy after a missed press).
    if (input.held(Key::Left)) s_want = LEFT;
    else if (input.held(Key::Right)) s_want = RIGHT;
    else if (input.held(Key::Up)) s_want = UP;
    else if (input.held(Key::Down)) s_want = DOWN;
    if (touch.justPressed()) {
      int16_t lx, ly;
      s_gfx->touchToLandscape(touch.x(), touch.y(), lx, ly);
      float dx = lx - sx(posX(s_me)) * ZOOM, dy = ly - sy(posY(s_me)) * ZOOM; // screen px
      if (fabsf(dx) > fabsf(dy)) s_want = dx > 0 ? RIGHT : LEFT;
      else s_want = dy > 0 ? DOWN : UP;
    }
  } else if (touch.justPressed()) {
    InputEvent tap = {Key::Select, true, false}; // tap = SELECT on the menus
    key(tap);
  }

  acc += dt;
  while (acc >= STEP) {
    update(STEP);
    acc -= STEP;
  }
  render();
}

} // namespace CypressNight
