#include "LullabyRun.h"
#include "Raster.h"
#include "RunAudio.h"
#include "HalfRes.h"
#include "Glass.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <initializer_list>
#include <algorithm>
#include <soc/soc_memory_layout.h>
#include <font/glcdfont.h>

using namespace Raster;

namespace LullabyRun {

// ---------------------------------------------------------------------------
// Constants (same projection as the browser preview)
// ---------------------------------------------------------------------------

static const int W = 456, H = 280, HY = 118; // screen, flat-road horizon
static const float SEG = 200, ROADW = 2000, CAMH = 1000;
static const int RUMBLE = 3;
static const float DEPTH = 0.8390996f; // 1 / tan(50°)
static const float PLAYER_Y = H - 14;
static const float PLAYER_Z = DEPTH * CAMH * (W / 2) / (PLAYER_Y - HY);
static const int DRAW = 170;
static const float STEP = 1.0f / 60;
static const float MAXV = SEG * 60;
static const float CAR_W = 650;
static const int FW = W / 2, FH = H / 2; // the frame is drawn at half resolution
static const int LW = 912, LH = 120; // skyline layer size (full resolution)
static const uint16_t KEY = 0xF81F;  // transparent in skyline layers

static const int MAX_SEGS = 2400;
static const int MAX_SPRITES = 1800;
static const int MAX_CARS = 48;

// ---------------------------------------------------------------------------
// District data
// ---------------------------------------------------------------------------

enum LayerType : uint8_t { L_NONE, L_CITY, L_MOUNTAINS, L_DUNES, L_HILLS, L_SEA };
enum Prop : uint8_t {
  P_LAMP, P_POST, P_CRATE, P_STALL, P_SIGN, P_BUILDING, P_HOUSE, P_HAY, P_PINE, P_ROCK, P_GOAT,
  P_DUCK, P_CANDY, P_NOTE, P_TAKEOUT, P_CACTUS, P_TUMBLEWEED, P_PALM, P_BILLBOARD, P_PYLON, P_NONE
};
enum CarKind : uint8_t { K_CAR, K_VAN, K_TINY, K_TRUCK, K_POLICE, K_PUFF_EV, K_PUFF_ICE };
enum : uint8_t { FX_STEAM = 1, FX_RAIN = 2, FX_HEAT = 4, FX_SAND = 8, FX_SIREN = 16, FX_LIGHTNING = 32, FX_WIND = 64 };

struct TrackPart { // unset fields are zero
  int16_t len;
  float curve, hill;
  uint8_t tunnel;
};

struct District {
  const char *name = "", *rival = "", *taunt = "", *twist = "";
  CarKind rk = K_CAR;
  uint32_t rc = 0xffffff;
  uint32_t sky[3] = {0, 0, 0}, sky2[3] = {0, 0, 0};
  bool hasSky2 = false, stars = false;
  bool hasSun = false, moon = false, stripes = false, rise = false;
  uint32_t sunC = 0;
  float sunR = 0, sunX = 0, sunY = 0;
  LayerType far = L_NONE, near = L_NONE;
  uint32_t farCol = 0, farAccent = 0, nearCol = 0, nearAccent = 0;
  uint32_t grass[2] = {0, 0}, road[2] = {0, 0}, rumble[2] = {0, 0}, lane = 0, fog = 0;
  Prop props[6] = {P_NONE};
  uint8_t nProps = 0, propEvery = 7;
  uint32_t glow = 0xffd27a, neon[3] = {0xff5fa2, 0x5ff7ff, 0xffd84f};
  uint8_t nNeon = 3;
  uint32_t wall = 0x2a2a40, ceil = 0x1c1c2c, tlight = 0xffd27a, facade = 0x2a2140, tree = 0x1f5a3a;
  bool lamps = true, wet = false, twins = false, baron = false, police = false, selfie = false, nearMissX2 = false;
  uint8_t lanes = 3, traffic = 8, fx = 0;
  float diff = 0.8f;
  Prop hazard = P_NONE;
  const TrackPart *track = nullptr;
  uint8_t nTrack = 0;

  void setProps(std::initializer_list<Prop> l) {
    nProps = 0;
    for (Prop p : l) props[nProps++] = p;
  }
};

static void set3(uint32_t *d, uint32_t a, uint32_t b, uint32_t c) { d[0] = a; d[1] = b; d[2] = c; }
static void set2(uint32_t *d, uint32_t a, uint32_t b) { d[0] = a; d[1] = b; }

static const TrackPart T0[] = {{80}, {160, 2}, {120, 0, 12}, {180, -3, -12}, {100}, {200, 3, 16}, {160, -2, -16}, {140, 4}, {120}, {180, -3, 10}, {160, 0, -10}, {100, 2}};
static const TrackPart T1[] = {{60}, {140, -3}, {100}, {160, 4}, {120, 0, 8}, {180, -4, -8}, {100}, {160, 5}, {140, -2}, {180, 3, 10}, {160, -4, -10}, {80}};
static const TrackPart T2[] = {{80}, {180, 3}, {120, 0, 14}, {200, -4, -14}, {120}, {160, 5}, {200, -3, 10}, {140, 0, -10}, {160, 4}, {100}};
static const TrackPart T3[] = {{80}, {120, 2}, {300, 0, 0, 1}, {100, -3}, {160, 3, 10}, {280, -2, 0, 1}, {120, 0, -10}, {260, 3, 0, 1}, {120}, {160, -3}, {80}};
static const TrackPart T4[] = {{60}, {140, 3, 20}, {120, -4, 10}, {160, 5, -25}, {100, 0, 30}, {180, -5, -15}, {140, 4, 20}, {160, -3, -30}, {120, 6, 10}, {100, 0, -10}, {80}};
static const TrackPart T5[] = {{100}, {240, 1}, {200, -2, 8}, {260}, {200, 2, -8}, {240, -1}, {200, 3}, {200}, {100}};
static const TrackPart T6[] = {{100}, {300, 1}, {160, -2, 10}, {300, 0, -10}, {200, 2}, {260}, {160, -3}, {200, 1, 8}, {120, 0, -8}};
static const TrackPart T7[] = {{60}, {120, 4}, {100, -4}, {140, 0, 10}, {120, 5, -10}, {100, -5}, {160, 3}, {120, -4, 8}, {140, 4, -8}, {100}};
static const TrackPart T8[] = {{80}, {200, -3}, {140, 0, 12}, {200, 4, -12}, {160}, {200, -5}, {180, 3, 10}, {160, -2, -10}, {120}};
static const TrackPart T9[] = {{100}, {400, 0.6f}, {300, -0.6f, 6}, {400, 0, -6}, {300, 1}, {200}};
static const TrackPart T10[] = {{80}, {160, 2}, {120, -3, 10}, {220, 0, 0, 1}, {160, 4, -10}, {140, -4}, {160, 0, 20}, {160, 5, -20}, {200, -2}, {260, 1}, {160, -3, 12}, {140, 3, -12}, {100}};
#define TRACK(d, t) (d).track = t, (d).nTrack = sizeof(t) / sizeof(t[0])

static const int NUM_DIST = 11;
static District DIST[NUM_DIST];

static void defineDistricts() {
  District *d;

  d = &DIST[0];
  d->name = "Pier Pressure"; d->rival = "Milo"; d->rk = K_CAR; d->rc = 0x4fb3ff;
  d->taunt = "I've watched SO many racing videos."; d->twist = "Tutorial docks. Seagulls on every post.";
  set3(d->sky, 0x151a4d, 0x3d2c7c, 0xff8f7e); d->stars = true;
  d->hasSun = true; d->sunC = 0xffe2b0; d->sunR = 14; d->sunX = 340; d->sunY = 46; d->moon = true;
  d->far = L_CITY; d->farCol = 0x2a2463; d->farAccent = 0xffd27a; d->near = L_SEA; d->nearCol = 0x1c2f5e; d->nearAccent = 0xff9fb8;
  set2(d->grass, 0x1c3a5e, 0x193556); set2(d->road, 0x3b3552, 0x37314d); set2(d->rumble, 0xff5fa2, 0xf4ecff); d->lane = 0xfff1c2; d->fog = 0x3d2c7c;
  d->setProps({P_POST, P_CRATE, P_POST}); d->traffic = 8; d->diff = 0.80f; TRACK(*d, T0);

  d = &DIST[1];
  d->name = "Noodle Street"; d->rival = "Kiko"; d->rk = K_VAN; d->rc = 0xffd84f;
  d->taunt = "Hungry? Too bad. Deadline's in five."; d->twist = "Food stalls and steam. Kiko drops takeaway.";
  set3(d->sky, 0x13082b, 0x3f1054, 0xc93a74); d->stars = true;
  d->hasSun = true; d->sunC = 0xffe8f0; d->sunR = 11; d->sunX = 96; d->sunY = 34; d->moon = true;
  d->far = L_CITY; d->farCol = 0x2b0f45; d->farAccent = 0xff5fa2; d->near = L_CITY; d->nearCol = 0x1e0b33; d->nearAccent = 0xffd84f;
  set2(d->grass, 0x2b1739, 0x271433); set2(d->road, 0x302a3f, 0x2c263a); set2(d->rumble, 0xffd84f, 0x3a3250); d->lane = 0xffd84f; d->fog = 0x3f1054;
  d->setProps({P_STALL, P_SIGN, P_STALL, P_SIGN}); d->glow = 0xffb0d0; d->traffic = 14; d->diff = 0.82f; d->hazard = P_TAKEOUT; d->fx = FX_STEAM; TRACK(*d, T1);

  d = &DIST[2];
  d->name = "Puddle Market"; d->rival = "Grandma Sol"; d->rk = K_TINY; d->rc = 0xc7a6ff;
  d->taunt = "I've been doing this since before roads, dear."; d->twist = "Rain, wet road, less grip.";
  set3(d->sky, 0x0c1627, 0x1e3049, 0x40597a);
  d->far = L_CITY; d->farCol = 0x16253b; d->farAccent = 0x9fd2ff; d->near = L_HILLS; d->nearCol = 0x122033;
  set2(d->grass, 0x1a2b35, 0x172730); set2(d->road, 0x262d3a, 0x232a36); set2(d->rumble, 0x7fd3ff, 0xe9f4ff); d->lane = 0xcdeeff; d->fog = 0x2a3d57;
  d->setProps({P_STALL, P_STALL, P_SIGN, P_CRATE}); d->glow = 0x9fd2ff; d->traffic = 14; d->diff = 0.83f; d->wet = true; d->fx = FX_RAIN; TRACK(*d, T2);

  d = &DIST[3];
  d->name = "Tunnel Vision"; d->rival = "Vex & Vera"; d->rk = K_CAR; d->rc = 0x3dffb0; d->twins = true;
  d->taunt = "Which one of us are you racing? Exactly."; d->twist = "Long neon tunnels. Two rivals as a team.";
  set3(d->sky, 0x080c1c, 0x151a3c, 0x3b2b6c); d->stars = true;
  d->far = L_CITY; d->farCol = 0x151a3a; d->farAccent = 0x5ff7ff; d->near = L_HILLS; d->nearCol = 0x10142c;
  set2(d->grass, 0x1c1e30, 0x191b2b); set2(d->road, 0x2d2f42, 0x292b3d); set2(d->rumble, 0x3dffb0, 0x22243a); d->lane = 0x3dffb0; d->fog = 0x151a3c;
  d->setProps({P_SIGN, P_SIGN, P_CRATE}); d->glow = 0x5ff7ff; d->neon[0] = 0x3dffb0; d->neon[1] = 0x5ff7ff; d->nNeon = 2;
  d->wall = 0x24283f; d->ceil = 0x181a2b; d->tlight = 0x5ff7ff; d->facade = 0x2b2244; d->traffic = 12; d->diff = 0.85f; TRACK(*d, T3);

  d = &DIST[4];
  d->name = "Goat Mountain"; d->rival = "Rhea"; d->rk = K_CAR; d->rc = 0xff9a3d;
  d->taunt = "The goats and I go way back."; d->twist = "Steep hills, hairpins, goats on the road.";
  set3(d->sky, 0x2b1b4d, 0xd2577c, 0xffb56d);
  d->hasSun = true; d->sunC = 0xffdd8a; d->sunR = 34; d->sunX = 228; d->sunY = 104; d->stripes = true;
  d->far = L_MOUNTAINS; d->farCol = 0x7a3f74; d->farAccent = 0xffd0e0; d->near = L_MOUNTAINS; d->nearCol = 0x4a2a55;
  set2(d->grass, 0x3f6d3b, 0x396535); set2(d->road, 0x4b4559, 0x463f53); set2(d->rumble, 0xffffff, 0xd2577c); d->lane = 0xffffff; d->fog = 0xc06a86;
  d->setProps({P_PINE, P_PINE, P_ROCK, P_GOAT, P_PINE}); d->lamps = false; d->hazard = P_GOAT; d->traffic = 6; d->diff = 0.86f; TRACK(*d, T4);

  d = &DIST[5];
  d->name = "Freeway Frenzy"; d->rival = "Big Dutch"; d->rk = K_TRUCK; d->rc = 0xff4b4b;
  d->taunt = "Don't mind me. I'm just a truck."; d->twist = "Four lanes of traffic. Near misses pay double.";
  set3(d->sky, 0x0a0f25, 0x1b2452, 0x42316c); d->stars = true;
  d->far = L_CITY; d->farCol = 0x1a2048; d->farAccent = 0xffd27a; d->near = L_CITY; d->nearCol = 0x121735; d->nearAccent = 0xff9f6a;
  set2(d->grass, 0x20253a, 0x1d2235); set2(d->road, 0x35384b, 0x313446); set2(d->rumble, 0xffcf3d, 0x35384b); d->lane = 0xf0f0f0; d->fog = 0x1b2452;
  d->setProps({P_BILLBOARD, P_SIGN, P_BILLBOARD}); d->lanes = 4; d->traffic = 42; d->diff = 0.87f; d->nearMissX2 = true; TRACK(*d, T5);

  d = &DIST[6];
  d->name = "Sandy Pants Desert"; d->rival = "Saffron"; d->rk = K_CAR; d->rc = 0xffe14f;
  d->taunt = "Hold on, getting my angle... okay, bye!"; d->twist = "Heat haze, tumbleweeds, selfie stops.";
  set3(d->sky, 0xff8c3f, 0xffb664, 0xffe3a6);
  d->hasSun = true; d->sunC = 0xfff6d8; d->sunR = 20; d->sunX = 110; d->sunY = 36;
  d->far = L_DUNES; d->farCol = 0xe89a5a; d->near = L_DUNES; d->nearCol = 0xd7884a;
  set2(d->grass, 0xe2aa5c, 0xdaa154); set2(d->road, 0x8d6b53, 0x86654e); set2(d->rumble, 0xfff3d6, 0xc45b3b); d->lane = 0xfff3d6; d->fog = 0xffd49a;
  d->setProps({P_CACTUS, P_CACTUS, P_ROCK, P_TUMBLEWEED}); d->lamps = false; d->traffic = 8; d->diff = 0.88f; d->fx = FX_HEAT | FX_SAND; d->selfie = true; TRACK(*d, T6);

  d = &DIST[7];
  d->name = "Bumper Town"; d->rival = "The Baron"; d->rk = K_CAR; d->rc = 0x9b5cff; d->baron = true;
  d->taunt = "Mwahaha! Mind the paintwork. Yours, not mine."; d->twist = "Bumping is on. Shove the Baron into the hay.";
  set3(d->sky, 0x2b1731, 0x6d3b4b, 0xdb8c5b);
  d->hasSun = true; d->sunC = 0xffc58a; d->sunR = 18; d->sunX = 350; d->sunY = 96;
  d->far = L_CITY; d->farCol = 0x4a2a3c; d->farAccent = 0xffd27a; d->near = L_HILLS; d->nearCol = 0x3a2432;
  set2(d->grass, 0x5b4b3b, 0x554637); set2(d->road, 0x5b5661, 0x56515b); set2(d->rumble, 0xdbb65b, 0x5b5661); d->lane = 0xe9ddc1; d->fog = 0x6d3b4b;
  d->setProps({P_HOUSE, P_HAY, P_HOUSE, P_HAY}); d->glow = 0xffcf8a; d->traffic = 10; d->diff = 0.89f; TRACK(*d, T7);

  d = &DIST[8];
  d->name = "Siren Song"; d->rival = "Officer Hale"; d->rk = K_POLICE; d->rc = 0xf2f2f2; d->police = true;
  d->taunt = "License and registration... after the finish line."; d->twist = "A police chase. Stay ahead of the lights.";
  set3(d->sky, 0x070a1d, 0x151c40, 0x2b306c); d->stars = true;
  d->hasSun = true; d->sunC = 0xe0e6ff; d->sunR = 13; d->sunX = 390; d->sunY = 34; d->moon = true;
  d->far = L_CITY; d->farCol = 0x141a3c; d->farAccent = 0x9fb6ff; d->near = L_CITY; d->nearCol = 0x0e1230; d->nearAccent = 0xffe08a;
  set2(d->grass, 0x1a1e31, 0x171b2c); set2(d->road, 0x2f3245, 0x2b2e41); set2(d->rumble, 0xff3d5a, 0x3d7bff); d->lane = 0xe0e6ff; d->fog = 0x151c40;
  d->setProps({P_BUILDING, P_SIGN, P_BUILDING}); d->glow = 0xcfd8ff; d->traffic = 18; d->diff = 0.90f; d->fx = FX_SIREN; TRACK(*d, T8);

  d = &DIST[9];
  d->name = "Thunder Bridge"; d->rival = "Nova"; d->rk = K_CAR; d->rc = 0x5ff7ff;
  d->taunt = "Page 47 of my notebook: you. Underlined. Twice."; d->twist = "Lightning, wind gusts, an endless bridge.";
  set3(d->sky, 0x0b0f1b, 0x202839, 0x3a475f);
  d->far = L_MOUNTAINS; d->farCol = 0x232c3e; d->near = L_SEA; d->nearCol = 0x1a2333; d->nearAccent = 0x9fb6d8;
  set2(d->grass, 0x1b2a3b, 0x182636); set2(d->road, 0x3b3e4b, 0x373a46); set2(d->rumble, 0xf2f2f2, 0xff5fa2); d->lane = 0xf2f2f2; d->fog = 0x2b3547;
  d->setProps({P_PYLON}); d->propEvery = 9; d->lamps = false; d->traffic = 12; d->diff = 0.92f; d->wet = true; d->fx = FX_RAIN | FX_LIGHTNING | FX_WIND; TRACK(*d, T9);

  d = &DIST[10];
  d->name = "The Golden Hubcap"; d->rival = "Lullaby"; d->rk = K_CAR; d->rc = 0xffd36b;
  d->taunt = "Shh... try not to fall asleep."; d->twist = "Every district at once, racing into sunrise.";
  set3(d->sky, 0x120c2e, 0x2a1d5a, 0x5a3a8a); set3(d->sky2, 0x3b3f8f, 0xff8a7a, 0xffd58a); d->hasSky2 = true; d->stars = true;
  d->hasSun = true; d->sunC = 0xffe0a0; d->sunR = 26; d->sunX = 228; d->sunY = 150; d->stripes = true; d->rise = true;
  d->far = L_CITY; d->farCol = 0x2a2060; d->farAccent = 0xffd27a; d->near = L_HILLS; d->nearCol = 0x1d1640;
  set2(d->grass, 0x2b2850, 0x27244a); set2(d->road, 0x3a3552, 0x36314d); set2(d->rumble, 0xffd36b, 0xff5fa2); d->lane = 0xfff1c2; d->fog = 0x2a1d5a;
  d->setProps({P_PALM, P_SIGN, P_STALL, P_PINE, P_HOUSE, P_BILLBOARD}); d->traffic = 14; d->diff = 0.95f; TRACK(*d, T10);
}

static const char *TITLES[] = {"Certified Menace", "Speedy Marshmallow", "Pretty Zoomy", "Road Snack", "Road Snack", "Road Snack"};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct Seg {
  float curve, y1, y2;
  uint8_t flags;
  uint16_t spr0, sprN;
};
enum : uint8_t { SG_TUNNEL = 1, SG_PORTAL = 2, SG_FINISH = 4, SG_COLOR = 8 };

struct Sprite {
  uint16_t seg;
  uint8_t kind;
  uint8_t flags;
  float o, seed;
};
enum : uint8_t { SP_HAZARD = 1, SP_TAKEN = 2 };

struct Car {
  bool rival, main, baron, police;
  CarKind kind;
  uint32_t col;
  float dist, offset, speed, base0, phase, sleep, flash, prevGap;
  bool hasPrev;
};

struct Proj {
  float x1, y1, w1, s1, cy1;
  float x2, y2, w2, s2, cy2;
  float camz1, fog;
  uint16_t idx;
};

struct Pop {
  char text[16];
  uint16_t col;
  float t, x;
};
struct Particle {
  float x, y, vx, vy, life;
};

static LandscapeGFX *s_gfx = nullptr;
static Arduino_GFX *s_panel = nullptr;
static Input *s_input = nullptr;
static Arduino_Canvas *s_canvas = nullptr;
static uint16_t *s_frame = nullptr;
static uint16_t *s_layerFar = nullptr, *s_layerNear = nullptr;
static int16_t s_layerTopFar = 0, s_layerTopNear = 0;
static Seg *s_segs = nullptr;
static Sprite *s_sprites = nullptr;
static int s_N = 0, s_nSprites = 0;
static float s_trackLength = 0;
static Car s_cars[MAX_CARS];
static int s_nCars = 0;
static Proj s_proj[DRAW];
static bool s_active = false, s_exit = false;
static Preferences s_prefs;

static int s_dIndex = 0;
static const District *D = &DIST[0];
static bool s_ev = true; // Marshmallow (EV) or Grumbler (ICE)
static int s_unlocked = 0;
static float s_best[NUM_DIST];
static uint8_t s_bestDucks[NUM_DIST];

enum class Mode : uint8_t { Pick, Race, Paused };
static Mode s_mode = Mode::Pick;
static bool s_demo = true;
static float s_t = 0;

static struct {
  float x, speed, dist, boost, boosting, spin, rpm, flame, shiftLag, bounce, steer;
  int gear, ducks;
  bool braking, throttle;
} P;

static struct {
  float countdown, time, skyOff, hillOff, note, flash, nextBolt, boltLife;
  bool finished, newBest, unlockedNext;
  int place;
  Pop pops[6];
  int nPops;
  Particle trail[40];
  int nTrail;
  float boltX[16], boltY[16];
  int boltN;
  float rainX[130], rainY[130], rainV[130];
  float sandX[50], sandY[50], sandV[50];
  float starX[46], starY[46], starP[46];
  uint8_t starS[46];
} S;

// Input latched between frames.
static float s_wheel = 0;
static bool s_boostTap = false;
static int s_lastCount = 4;

// ---------------------------------------------------------------------------
// Small utils
// ---------------------------------------------------------------------------

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed) {}
  float operator()() {
    s += 0x6D2B79F5u;
    uint32_t t = s;
    t = (t ^ (t >> 15)) * (1 | t);
    t = (t + ((t ^ (t >> 7)) * (61 | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) / 16777216.0f;
  }
};
static Rng s_rand(0xC0FFEE);
static inline float frand() { return s_rand(); }

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float wrapf(float v, float m) {
  float r = fmodf(v, m);
  return r < 0 ? r + m : r;
}
static inline uint32_t shadeHex(uint32_t c, float t) { return mixHex(c, 0x000000, t); }

static inline Seg &segAt(float z) {
  int i = (int)(wrapf(z, s_trackLength) / SEG);
  if (i >= s_N) i = s_N - 1;
  return s_segs[i];
}
static inline int segIndex(const Seg &s) { return (int)(&s - s_segs); }

static void pop(const char *text, uint32_t col) {
  if (S.nPops >= 6) {
    memmove(&S.pops[0], &S.pops[1], sizeof(Pop) * 5);
    S.nPops = 5;
  }
  Pop &p = S.pops[S.nPops++];
  strncpy(p.text, text, sizeof(p.text) - 1);
  p.text[sizeof(p.text) - 1] = 0;
  p.col = rgb(col);
  p.t = 0;
  p.x = W / 2 + (frand() - 0.5f) * 60;
}

// ---------------------------------------------------------------------------
// Track
// ---------------------------------------------------------------------------

static float lastY() { return s_N ? s_segs[s_N - 1].y2 : 0; }

static void addSeg(float curve, float y, bool tun) {
  if (s_N >= MAX_SEGS) return;
  Seg &s = s_segs[s_N];
  s.curve = curve;
  s.y1 = lastY();
  s.y2 = y;
  s.flags = (tun ? SG_TUNNEL : 0) | (((s_N / RUMBLE) % 2) ? SG_COLOR : 0);
  s.spr0 = s.sprN = 0;
  s_N++;
}

static float easeIn(float a, float b, float p) { return a + (b - a) * p * p; }
static float easeInOut(float a, float b, float p) { return a + (b - a) * ((-cosf(p * 3.14159265f) / 2) + 0.5f); }

static void addRoad(int enter, int hold, int leave, float curve, float hill, bool tun) {
  float sy = lastY(), ey = sy + hill * SEG;
  int tot = enter + hold + leave;
  for (int n = 0; n < enter; n++) addSeg(easeIn(0, curve, (float)n / enter), easeInOut(sy, ey, (float)n / tot), tun);
  for (int n = 0; n < hold; n++) addSeg(curve, easeInOut(sy, ey, (float)(enter + n) / tot), tun);
  for (int n = 0; n < leave; n++) addSeg(easeInOut(curve, 0, (float)n / leave), easeInOut(sy, ey, (float)(enter + hold + n) / tot), tun);
}

static void addSprite(int n, uint8_t kind, float o, float seed = 0, uint8_t flags = 0) {
  if (s_nSprites >= MAX_SPRITES) return;
  Sprite &sp = s_sprites[s_nSprites++];
  sp.seg = (uint16_t)(n % s_N);
  sp.kind = kind;
  sp.o = o;
  sp.seed = seed;
  sp.flags = flags;
}

static bool roadside(int n) {
  return !(s_segs[n].flags & SG_TUNNEL) && !(s_segs[n + 6 < s_N ? n + 6 : s_N - 1].flags & SG_TUNNEL) &&
         !(s_segs[n - 6 > 0 ? n - 6 : 0].flags & SG_TUNNEL);
}

static void buildTrack() {
  s_N = 0;
  for (int i = 0; i < D->nTrack; i++) {
    const TrackPart &tp = D->track[i];
    int e = (int)lroundf(tp.len / 4.0f);
    addRoad(e, tp.len - 2 * e, e, tp.curve, tp.hill, tp.tunnel);
  }
  addRoad(30, 40, 30, 0, -lastY() / SEG, false); // back to the start height so the loop joins
  s_trackLength = s_N * SEG;
  for (int i = 0; i < s_N; i++) {
    Seg &s = s_segs[i];
    const Seg &prev = s_segs[(i - 1 + s_N) % s_N];
    if ((s.flags & SG_TUNNEL) && !(prev.flags & SG_TUNNEL)) s.flags |= SG_PORTAL;
    if (i == 4 || i == 5) s.flags |= SG_FINISH;
  }

  // Roadside sprites, collected unsorted then bucketed by segment.
  s_nSprites = 0;
  Rng r(1000 + s_dIndex * 77);
  if (D->lamps)
    for (int n = 12; n < s_N; n += 16)
      if (roadside(n)) {
        addSprite(n, P_LAMP, -1.18f);
        addSprite(n + 8, P_LAMP, 1.18f);
      }
  for (int n = 10; n < s_N; n += D->propEvery + (int)(r() * 4)) {
    if (!roadside(n)) continue;
    Prop k = D->props[(int)(r() * D->nProps)];
    int side = (D->nProps == 1 || r() < 0.5f) ? -1 : 1;
    float o = side * (1.45f + r() * 1.9f);
    if (k == P_BUILDING) o = side * (2.2f + r() * 2.2f);
    if (k == P_PYLON) {
      addSprite(n, k, -1.35f, r());
      addSprite(n, k, 1.35f, r());
      continue;
    }
    addSprite(n, k, o, r());
    if (D->nProps > 1 && r() < 0.5f) {
      Prop k2 = D->props[(int)(r() * D->nProps)];
      float o2 = -side * (1.5f + r() * 2);
      addSprite(n + 3, k2, o2, r());
    }
  }
  // The three hidden ducks, close to the kerb.
  const float duckAt[3] = {0.27f, 0.56f, 0.83f};
  for (int i = 0; i < 3; i++) {
    int n = (int)(s_N * duckAt[i]);
    while (s_segs[n].flags & SG_TUNNEL) n++;
    addSprite(n, P_DUCK, (i % 2 ? 1 : -1) * 1.1f);
  }
  for (int n = 150; n < s_N - 40; n += 170 + (int)(r() * 60)) addSprite(n, P_CANDY, ((int)(r() * 3) - 1) * 0.6f);
  for (int n = 420; n < s_N - 40; n += 520) addSprite(n, P_NOTE, (r() - 0.5f) * 1.2f);
  if (D->hazard != P_NONE)
    for (int n = 220; n < s_N - 60; n += 130 + (int)(r() * 90))
      if (!(s_segs[n].flags & SG_TUNNEL)) {
        float o = (r() - 0.5f) * 1.4f;
        addSprite(n, D->hazard, o, r(), SP_HAZARD);
      }

  // Sorted by segment so each segment owns a contiguous run.
  std::stable_sort(s_sprites, s_sprites + s_nSprites, [](const Sprite &a, const Sprite &b) { return a.seg < b.seg; });
  for (int i = 0; i < s_N; i++) s_segs[i].sprN = 0;
  for (int i = s_nSprites - 1; i >= 0; i--) {
    Seg &s = s_segs[s_sprites[i].seg];
    s.spr0 = (uint16_t)i;
    s.sprN++;
  }
}

// ---------------------------------------------------------------------------
// Skyline layers (tile horizontally)
// ---------------------------------------------------------------------------

static void layerRect(float x, float y, float w, float h, uint16_t c) {
  rect(x, y, w, h, c);
  if (x + w > LW) rect(x - LW, y, w, h, c);
}

static void makeLayer(uint16_t *buf, int16_t &top, LayerType type, uint32_t col, uint32_t accent, uint32_t seed, bool far) {
  target(buf, LW, LH);
  setScale(1);
  for (int i = 0; i < LW * LH; i++) buf[i] = KEY;
  Rng r(seed);
  uint16_t c = rgb(col);
  if (type == L_CITY) {
    float px = 0;
    while (px < LW) {
      float bw = 18 + r() * (far ? 30 : 46), bh = (far ? 22 : 30) + r() * (far ? 60 : 80);
      layerRect(px, LH - bh, bw, bh, c);
      if (r() < 0.25f) layerRect(px + bw / 2 - 1, LH - bh - 10, 2, 10, c);
      if (accent) {
        uint16_t wc = mix(accent, col, far ? 0.5f : 0.15f);
        for (float wy = LH - bh + 5; wy < LH - 4; wy += 6)
          for (float wx = px + 3; wx < px + bw - 3; wx += 5)
            if (r() < (far ? 0.18f : 0.28f)) layerRect(wx, wy, 2, 2, wc);
      }
      px += bw + r() * 4;
    }
  } else if (type == L_MOUNTAINS) {
    int peaks = far ? 7 : 10, n = peaks * 2 + 1;
    float px[32], py[32];
    for (int i = 0; i < n; i++) {
      px[i] = (float)i * LW / (peaks * 2);
      py[i] = (i % 2) ? LH - (far ? 60 : 40) - r() * (far ? 50 : 40) : LH - 20 - r() * 20;
    }
    py[n - 1] = py[0];
    for (int x = 0; x < LW; x++) {
      int i = (int)(x * (peaks * 2) / LW);
      if (i >= n - 1) i = n - 2;
      float t = (x - px[i]) / (px[i + 1] - px[i]);
      float y = lerpf(py[i], py[i + 1], t);
      for (int yy = (int)y; yy < LH; yy++) buf[yy * LW + x] = c;
    }
    if (accent) {
      uint16_t a = rgb(accent);
      for (int i = 1; i < n; i += 2) {
        float t[6] = {px[i], py[i], px[i] - 9, py[i] + 10, px[i] + 9, py[i] + 10};
        poly(t, 3, a);
      }
    }
  } else if (type == L_DUNES || type == L_HILLS) {
    float a = far ? 26 : 18, base = far ? 54 : 34;
    for (int x = 0; x < LW; x++) {
      float y = LH - base - a * sinf((float)x / LW * 6.2831853f * (far ? 3 : 5) + seed) * 0.6f -
                a * 0.4f * sinf((float)x / LW * 6.2831853f * (far ? 7 : 9));
      for (int yy = (int)y; yy < LH; yy++)
        if (yy >= 0) buf[yy * LW + x] = c;
    }
  } else if (type == L_SEA) {
    rect(0, LH - 16, LW, 16, c);
    if (accent) {
      uint16_t a = rgb(accent);
      for (int i = 0; i < 60; i++) {
        float x = r() * LW, y = LH - 14 + r() * 12, w = 4 + r() * 12;
        layerRect(x, y, w, 1, a);
      }
    }
  }
  top = LH;
  for (int y = 0; y < LH && top == LH; y++)
    for (int x = 0; x < LW; x++)
      if (buf[y * LW + x] != KEY) { top = y; break; }
}

static void drawLayer(const uint16_t *buf, int16_t top, float off, int y0) {
  if (!buf) return;
  int sx0 = (int)(wrapf(off, 1) * LW) % LW;
  for (int ry = 0; ry < FH; ry++) {
    int ly = ry * 2 - y0;
    if (ly < top || ly >= LH) continue;
    const uint16_t *src = buf + ly * LW;
    uint16_t *dst = s_frame + ry * FW;
    int sx = sx0;
    for (int x = 0; x < FW; x++) {
      uint16_t p = src[sx];
      if (p != KEY) dst[x] = p;
      sx += 2;
      if (sx >= LW) sx -= LW;
    }
  }
}

// ---------------------------------------------------------------------------
// Cars and race setup
// ---------------------------------------------------------------------------

static void makeCars() {
  s_nCars = 0;
  Rng r(500 + s_dIndex);
  struct R { CarKind kind; uint32_t col; bool main, baron, police; };
  R racers[5];
  int nr = 0;
  if (D->twins) {
    racers[nr++] = {K_CAR, 0x3dffb0, true, false, false};
    racers[nr++] = {K_CAR, 0x2fd6ff, true, false, false};
  } else {
    racers[nr++] = {D->rk, D->rc, true, D->baron, D->police};
  }
  int others[NUM_DIST], no = 0;
  for (int i = 0; i < NUM_DIST - 1; i++)
    if (i != s_dIndex) others[no++] = i;
  while (nr < 5 && no > 0) {
    int pick = (int)(r() * no);
    const District &o = DIST[others[pick]];
    for (int j = pick; j < no - 1; j++) others[j] = others[j + 1];
    no--;
    racers[nr++] = {o.rk == K_POLICE ? K_CAR : o.rk, o.rc, false, false, false};
  }
  for (int i = 0; i < nr; i++) {
    Car &c = s_cars[s_nCars++];
    memset(&c, 0, sizeof(c));
    c.rival = true;
    c.main = racers[i].main;
    c.baron = racers[i].baron;
    c.police = racers[i].police;
    c.kind = racers[i].kind;
    c.col = racers[i].col;
    c.dist = PLAYER_Z + (i + 1) * SEG * 4;
    if (c.police) c.dist = PLAYER_Z - SEG * 25; // Hale starts behind and chases
    c.offset = i % 2 ? 0.45f : -0.45f;
    c.base0 = MAXV * D->diff * (c.main ? 1.02f : 0.93f + r() * 0.06f);
    c.phase = r() * 6;
  }
  static const uint32_t tcols[] = {0x7d8aa8, 0xa3a0b8, 0x8f7f9e, 0x6f8f8a, 0xa88d7a, 0xb0b8c8};
  for (int i = 0; i < D->traffic && s_nCars < MAX_CARS; i++) {
    Car &c = s_cars[s_nCars++];
    memset(&c, 0, sizeof(c));
    int lane = (int)(r() * D->lanes);
    float k = r();
    c.kind = k < 0.2f ? K_VAN : r() < 0.1f ? K_TRUCK : K_CAR;
    c.col = tcols[(int)(r() * 6)];
    c.dist = (0.08f + r() * 0.9f) * s_trackLength + SEG * 40;
    c.offset = -1 + (lane + 0.5f) * 2 / D->lanes;
    c.speed = MAXV * (0.25f + r() * 0.22f);
    c.base0 = c.speed;
  }
}

static void restart() {
  buildTrack();
  makeCars();
  memset(&P, 0, sizeof(P));
  P.boost = 0.35f;
  P.rpm = 900;
  P.gear = 1;
  float sx[46], sy[46], spp[46];
  uint8_t ss[46];
  memcpy(sx, S.starX, sizeof(sx)); memcpy(sy, S.starY, sizeof(sy)); memcpy(spp, S.starP, sizeof(spp)); memcpy(ss, S.starS, sizeof(ss));
  memset(&S, 0, sizeof(S));
  memcpy(S.starX, sx, sizeof(sx)); memcpy(S.starY, sy, sizeof(sy)); memcpy(S.starP, spp, sizeof(spp)); memcpy(S.starS, ss, sizeof(ss));
  S.countdown = s_demo ? 0 : 3.2f;
  S.place = 6;
  S.nextBolt = 3 + frand() * 4;
  for (int i = 0; i < 130; i++) { S.rainX[i] = frand() * W; S.rainY[i] = frand() * H; S.rainV[i] = 380 + frand() * 200; }
  for (int i = 0; i < 50; i++) { S.sandX[i] = frand() * W; S.sandY[i] = HY + frand() * (H - HY); S.sandV[i] = 120 + frand() * 200; }
  s_lastCount = 4;
}

static void setDistrict(int i) {
  s_dIndex = i;
  D = &DIST[i];
  makeLayer(s_layerFar, s_layerTopFar, D->far, D->farCol, D->farAccent, 31 + i, true);
  makeLayer(s_layerNear, s_layerTopNear, D->near, D->nearCol, D->nearAccent, 97 + i, false);
  Rng r(7 + i);
  for (int k = 0; k < 46; k++) {
    S.starX[k] = r() * W;
    S.starY[k] = r() * HY * 0.7f;
    S.starP[k] = r() * 6;
    S.starS[k] = r() < 0.15f ? 2 : 1;
  }
  restart();
}

static void saveProgress() {
  s_prefs.putUChar("car", s_ev ? 0 : 1);
  s_prefs.putUChar("dist", (uint8_t)s_dIndex);
  s_prefs.putUChar("unl", (uint8_t)s_unlocked);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

static const float GEARS[] = {0, 0.16f, 0.32f, 0.5f, 0.68f, 0.86f, 1.5f};

static float hazardOffset(const Sprite &spr) {
  if (spr.kind == P_GOAT) return spr.o + sinf(s_t * 0.5f + spr.seed * 9) * 0.5f;
  return spr.o;
}

static void finishRace() {
  S.finished = true;
  pop(S.place == 1 ? "WINNER!" : "FINISH", 0xffffff);
  RunAudio::fx(RunAudio::DingTop);
  if (s_demo) return;
  int i = s_dIndex;
  if (s_best[i] <= 0 || S.time < s_best[i]) {
    s_best[i] = S.time;
    S.newBest = true;
    char k[8];
    snprintf(k, sizeof(k), "b%d", i);
    s_prefs.putFloat(k, S.time);
  }
  if (P.ducks > s_bestDucks[i]) {
    s_bestDucks[i] = P.ducks;
    char k[8];
    snprintf(k, sizeof(k), "d%d", i);
    s_prefs.putUChar(k, (uint8_t)P.ducks);
  }
  if (S.place <= 3 && i == s_unlocked && s_unlocked < NUM_DIST - 1) {
    s_unlocked++;
    S.unlockedNext = true;
    saveProgress();
  }
}

static void update(float dt, float steerIn, bool brakeIn) {
  s_t += dt;
  if (s_mode == Mode::Paused) return;
  float pz = P.dist + PLAYER_Z;
  Seg &pseg = segAt(pz);
  if (S.countdown > 0) {
    S.countdown -= dt;
    int n = (int)ceilf(S.countdown);
    if (n != s_lastCount && n >= 1 && n <= 3) RunAudio::fx(RunAudio::Ding);
    s_lastCount = n;
    if (S.countdown <= 0) {
      pop("GO!", 0xffffff);
      RunAudio::fx(RunAudio::DingTop);
    }
  }
  bool racing = S.countdown <= 0;
  if (racing && !S.finished) S.time += dt;
  float sp = P.speed / MAXV;

  // ---- input (or autopilot) ----
  float steer = 0;
  bool throttle = false, brake = false, wantBoost = false;
  if (s_demo || S.finished) {
    float target = 0;
    for (int i = 0; i < s_nCars; i++) {
      const Car &c = s_cars[i];
      float gap = wrapf(c.dist - pz, s_trackLength);
      if (gap > 0 && gap < SEG * 14 && fabsf(c.offset - target) < 0.4f && c.speed < P.speed)
        target = c.offset > 0 ? c.offset - 0.6f : c.offset + 0.6f;
    }
    for (int n = 1; n < 10; n++) {
      Seg &s = segAt(pz + n * SEG);
      for (int j = 0; j < s.sprN; j++) {
        const Sprite &spr = s_sprites[s.spr0 + j];
        if (spr.kind == P_CANDY && !(spr.flags & SP_TAKEN)) target = spr.o;
      }
    }
    target = clampf(target - pseg.curve * 0.05f, -0.8f, 0.8f);
    steer = clampf((target - P.x) * 3, -1, 1);
    if (fabsf(steer) < 0.15f) steer = 0;
    throttle = S.finished ? sp < 0.5f : true;
    wantBoost = P.boost > 0.6f && fabsf(pseg.curve) < 1;
  } else {
    steer = steerIn;
    brake = brakeIn;
    throttle = !brake; // auto-throttle: there's no spare button for the gas
    wantBoost = s_boostTap;
  }
  s_boostTap = false;
  if (!racing) { throttle = false; steer = 0; }
  P.steer = steer;
  P.throttle = throttle;
  P.braking = brake && P.speed > 0;

  // ---- car feel ----
  bool ev = s_ev;
  float accel = (MAXV / 5) * (ev ? 1.25f : 0.9f) * (P.shiftLag > 0 ? 0.25f : 1);
  float top = MAXV * (ev ? 0.96f : 1.0f) * (P.boosting > 0 ? 1.35f : 1) * (P.spin > 0 ? 0.55f : 1);
  if (wantBoost && P.boost >= 0.4f && P.boosting <= 0 && racing) {
    P.boost -= 0.4f;
    P.boosting = 1.6f;
    pop("BOOST!", 0xff5fa2);
  }
  if (P.boosting > 0) {
    P.boosting -= dt;
    P.speed += accel * 1.4f * dt;
    if (ev) P.boost = fmaxf(0, P.boost - dt * 0.05f * sp);
  }

  // Steering has to beat the bends' outward pull at any speed, or holding
  // LEFT/RIGHT feels ignored: at full speed on the sharpest bend (curve 6)
  // the pull is 2*0.18*6 = 2.2 road-widths/s against 3.2 of steering. Some
  // steering at low speed too, so the car can always be lined up.
  float grip = D->wet ? 0.82f : 1;
  float dx = dt * 2 * sp;
  P.x += steer * dt * (1.2f + 2.0f * sp) * grip * (P.spin > 0 ? 0.3f : 1);
  P.x -= dx * sp * pseg.curve * 0.18f;
  if (D->fx & FX_WIND) P.x += sinf(s_t * 0.7f) * sinf(s_t * 0.23f) * 0.35f * dt * sp;
  if (throttle) P.speed += accel * dt;
  else if (brake) {
    P.speed -= MAXV * dt;
    if (ev) P.boost = fminf(1, P.boost + dt * 0.32f * sp);
  } else P.speed -= (MAXV / 5) * dt;
  if (fabsf(P.x) > 1.05f && P.speed > MAXV / 4) {
    P.speed -= MAXV / 2 * dt;
    P.bounce = (frand() - 0.5f) * 3;
  } else P.bounce *= 0.8f;
  P.x = clampf(P.x, -2.2f, 2.2f);
  P.speed = clampf(P.speed, 0, top);
  if (P.spin > 0) P.spin -= dt;
  if (P.shiftLag > 0) P.shiftLag -= dt;
  if (P.flame > 0) P.flame -= dt;
  P.boost = fminf(1, P.boost + dt * 0.012f);
  P.dist += P.speed * dt;
  S.skyOff += 0.0011f * pseg.curve * sp * dt * 60;
  S.hillOff += 0.0022f * pseg.curve * sp * dt * 60;

  // ---- engine model (ICE gears) ----
  if (!ev) {
    int gear = 1;
    while (gear < 6 && sp >= GEARS[gear]) gear++;
    float lo = GEARS[gear - 1], hi = GEARS[gear];
    float target = 1000 + clampf((sp - lo) / (hi - lo), 0, 1) * 6200 + (throttle ? 300 : 0);
    if (gear > P.gear && throttle) {
      P.shiftLag = 0.14f;
      P.flame = 0.16f;
      RunAudio::fx(RunAudio::Pop);
    }
    P.gear = gear;
    P.rpm += (target - P.rpm) * fminf(1, dt * 10);
    if (!throttle && P.rpm > 3200 && frand() < 0.05f) {
      P.flame = 0.08f;
      RunAudio::fx(RunAudio::PopSoft);
    }
  } else P.rpm = 0;

  // ---- other cars ----
  if (S.note > 0) S.note -= dt;
  for (int i = 0; i < s_nCars; i++) {
    Car &c = s_cars[i];
    if (!racing) { c.speed = 0; continue; }
    float curve = segAt(c.dist).curve;
    float v = c.base0;
    if (c.rival) {
      float gap = c.dist - pz;
      if (gap > SEG * 30) v *= 0.9f;
      else if (gap < -SEG * 45) v *= 1.08f;
      v *= 1 - fminf(0.15f, fabsf(curve) * 0.02f);
      if (D->selfie && c.main && sinf(s_t * 0.45f + c.phase) > 0.92f) {
        v *= 0.35f;
        if (frand() < 0.02f) c.flash = 0.15f;
      }
      if (c.police) v = MAXV * (gap < 0 ? 1.04f : 0.93f);
      c.offset += sinf(s_t * 0.6f + c.phase) * 0.12f * dt;
      for (int j = 0; j < s_nCars; j++) {
        const Car &o = s_cars[j];
        if (j == i || o.rival) continue;
        float gg = wrapf(o.dist - c.dist, s_trackLength);
        if (gg < SEG * 8 && fabsf(o.offset - c.offset) < 0.35f) c.offset += (c.offset > o.offset ? 1 : -1) * dt * 1.2f;
      }
      if (c.baron && fabsf(gap) < SEG * 3) {
        c.offset += (P.x > c.offset ? 1 : -1) * dt * 0.7f;
        if (frand() < 0.006f) pop("Mwahaha!", 0xc9a2ff);
      }
      c.offset = clampf(c.offset, -0.85f, 0.85f);
    }
    if (S.note > 0) {
      float gap = c.dist - pz;
      if (gap > -SEG * 10 && gap < SEG * 60) { v *= 0.5f; c.sleep = 0.5f; }
    }
    if (c.sleep > 0) c.sleep -= dt;
    if (c.flash > 0) c.flash -= dt;
    c.speed += (v - c.speed) * fminf(1, dt * 1.5f);
    c.dist += c.speed * dt;
  }

  // ---- collisions, near misses ----
  if (racing)
    for (int i = 0; i < s_nCars; i++) {
      Car &c = s_cars[i];
      float gap = wrapf(c.dist - pz, s_trackLength);
      if (gap > s_trackLength / 2) gap -= s_trackLength;
      float lat = fabsf(c.offset - P.x);
      float w = c.kind == K_TRUCK ? 0.36f : 0.3f;
      if (gap > 0 && gap < SEG * 0.9f && lat < w && P.speed > c.speed) {
        if (c.rival) {
          c.offset = clampf(c.offset + (c.offset >= P.x ? 0.35f : -0.35f), -1.4f, 1.4f);
          c.speed *= 0.92f;
          P.speed = fmaxf(c.speed * 0.95f, P.speed * 0.9f);
          pop(c.baron ? "Take THAT!" : "Rude!", 0xffd84f);
          RunAudio::fx(RunAudio::Bonk);
        } else {
          bool hard = (P.speed - c.speed) > MAXV * 0.35f;
          P.speed = c.speed * 0.85f;
          P.dist -= SEG * 0.3f;
          if (hard && P.spin <= 0) {
            P.spin = 0.9f;
            pop("Bonk!", 0xff8a8a);
            RunAudio::fx(RunAudio::Squeak);
          } else {
            pop("Oof", 0xff8a8a);
            RunAudio::fx(RunAudio::Bonk);
          }
        }
      }
      if (c.hasPrev && c.prevGap > 0 && gap <= 0 && gap > -SEG * 3 && !c.rival && lat >= w && lat < w + 0.18f && P.speed > MAXV * 0.4f) {
        pop(frand() < 0.5f ? "Ooh!" : "Close!", 0x5ff7ff);
        P.boost = fminf(1, P.boost + (ev ? 0.06f : 0.2f) * (D->nearMissX2 ? 2 : 1));
      }
      c.prevGap = gap;
      c.hasPrev = true;
    }

  // ---- pickups & hazards ----
  for (int n = 0; n < 2; n++) {
    Seg &s = segAt(pz + n * SEG);
    for (int j = 0; j < s.sprN; j++) {
      Sprite &spr = s_sprites[s.spr0 + j];
      bool hz = spr.flags & SP_HAZARD;
      float o = hz ? hazardOffset(spr) : spr.o;
      if ((spr.flags & SP_TAKEN) || fabsf(o - P.x) > (hz ? 0.26f : 0.28f)) continue;
      if (spr.kind == P_CANDY) {
        spr.flags |= SP_TAKEN;
        P.boost = fminf(1, P.boost + 0.35f);
        pop("Candy!", 0xff9fd0);
        RunAudio::fx(RunAudio::Ding);
      } else if (spr.kind == P_NOTE) {
        spr.flags |= SP_TAKEN;
        S.note = 3;
        pop("Lullaby...", 0x7fffd4);
        RunAudio::fx(RunAudio::DingHigh);
      } else if (spr.kind == P_DUCK) {
        spr.flags |= SP_TAKEN;
        P.ducks++;
        char b[16];
        snprintf(b, sizeof(b), "Quack! %d/3", P.ducks);
        pop(b, 0xffd84f);
        RunAudio::fx(RunAudio::DingTop);
      } else if (hz && racing && P.spin <= 0 && P.speed > MAXV * 0.25f) {
        spr.flags |= SP_TAKEN;
        P.spin = 0.8f;
        P.speed *= 0.6f;
        pop(spr.kind == P_GOAT ? "Baaa!" : "Splat!", 0xffffff);
        RunAudio::fx(RunAudio::Squeak);
      }
    }
  }

  // ---- place & finish ----
  int ahead = 0;
  for (int i = 0; i < s_nCars; i++)
    if (s_cars[i].rival && s_cars[i].dist > pz) ahead++;
  if (!S.finished) S.place = ahead + 1;
  if (!S.finished && P.dist >= s_trackLength) finishRace();

  // ---- effects ----
  int k = 0;
  for (int i = 0; i < S.nPops; i++) {
    S.pops[i].t += dt;
    if (S.pops[i].t < 1.2f) S.pops[k++] = S.pops[i];
  }
  S.nPops = k;
  if (P.boosting > 0 && frand() < 0.9f && S.nTrail < 40)
    S.trail[S.nTrail++] = {W / 2 + (frand() - 0.5f) * 50, PLAYER_Y - 10, (frand() - 0.5f) * 60, 40 + frand() * 50, 0.5f};
  k = 0;
  for (int i = 0; i < S.nTrail; i++) {
    Particle &p = S.trail[i];
    p.life -= dt;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.life > 0) S.trail[k++] = p;
  }
  S.nTrail = k;
  if (D->fx & FX_LIGHTNING) {
    S.nextBolt -= dt;
    S.flash = fmaxf(0, S.flash - dt * 2.5f);
    if (S.nextBolt <= 0) {
      S.nextBolt = 4 + frand() * 6;
      S.flash = 1;
      float bx = 60 + frand() * (W - 120), yy = 0, xx = bx;
      S.boltN = 0;
      S.boltX[S.boltN] = bx;
      S.boltY[S.boltN++] = 0;
      while (yy < HY - 10 && S.boltN < 16) {
        yy += 8 + frand() * 12;
        xx += (frand() - 0.5f) * 22;
        S.boltX[S.boltN] = xx;
        S.boltY[S.boltN++] = yy;
      }
      S.boltLife = 0.18f;
      RunAudio::fx(RunAudio::Thunder);
    }
    if (S.boltLife > 0) S.boltLife -= dt;
  }
  if (D->fx & FX_SAND)
    for (int i = 0; i < 50; i++) {
      S.sandX[i] -= (S.sandV[i] + P.speed / MAXV * 200) * dt;
      if (S.sandX[i] < 0) { S.sandX[i] = W; S.sandY[i] = HY + frand() * (H - HY); }
    }
  if (D->fx & FX_RAIN)
    for (int i = 0; i < 130; i++) {
      S.rainY[i] += S.rainV[i] * dt;
      S.rainX[i] -= (40 + P.speed / MAXV * 120) * dt;
      if (S.rainY[i] > H) { S.rainY[i] = -10; S.rainX[i] = frand() * W * 1.2f; }
      if (S.rainX[i] < 0) S.rainX[i] += W;
    }

  // The picker's demo race just goes round again.
  if (s_demo && S.finished && P.dist > s_trackLength + SEG * 60) restart();
}

// ---------------------------------------------------------------------------
// Rendering: sky, road, props, cars
// ---------------------------------------------------------------------------

static uint32_t g_fogHex = 0;
static float g_fogT = 0;
static inline uint16_t fogged(uint32_t c) { return mix(c, g_fogHex, g_fogT); }

static void skyColors(uint32_t *out) {
  if (D->hasSky2) {
    float p = clampf(P.dist / s_trackLength, 0, 1);
    for (int i = 0; i < 3; i++) out[i] = mixHex(D->sky[i], D->sky2[i], p);
  } else {
    for (int i = 0; i < 3; i++) out[i] = D->sky[i];
  }
}

static void drawSky(bool inTunnel) {
  if (inTunnel) {
    clear(rgb(D->ceil));
    return;
  }
  uint32_t sk[3];
  skyColors(sk);
  uint16_t a = rgb(sk[0]), b = rgb(sk[1]), c = rgb(sk[2]);
  int y1 = (int)lroundf(HY * 0.42f), y2 = (int)lroundf(HY * 0.74f);
  rect(0, 0, W, y1, a);
  rect(0, y1, W, y2 - y1, b);
  rect(0, y2, W, H - y2, c);
  for (int i = 0; i < 3; i++) {
    rect(0, y1 - 3 - i * 4, W, 1, b);
    rect(0, y2 - 3 - i * 4, W, 1, c);
  }
  if (D->stars) {
    float fade = D->hasSky2 ? 1 - clampf(P.dist / s_trackLength * 1.4f, 0, 1) : 1;
    for (int i = 0; i < 46; i++) {
      float al = fade * (0.4f + 0.6f * fabsf(sinf(s_t * 1.3f + S.starP[i])));
      rect(S.starX[i], S.starY[i], S.starS[i], S.starS[i], 0xFFFF, (uint8_t)(al * 255));
    }
  }
  if (D->hasSun) {
    float sy = D->sunY;
    if (D->rise) sy = lerpf(HY + 30, HY - 46, clampf(P.dist / s_trackLength, 0, 1));
    float sx = D->sunX - wrapf(S.skyOff, 1) * 60;
    circle(sx, sy, D->sunR, rgb(D->sunC));
    if (D->moon) circle(sx + D->sunR * 0.45f, sy - D->sunR * 0.25f, D->sunR * 0.85f, a);
    if (D->stripes)
      for (int i = 0; i < 5; i++) rect(sx - D->sunR, sy + i * D->sunR * 0.18f, D->sunR * 2, 1 + i * 0.7f, c);
  }
  if (S.boltLife > 0)
    for (int i = 1; i < S.boltN; i++) line(S.boltX[i - 1], S.boltY[i - 1], S.boltX[i], S.boltY[i], 2, 0xFFFF);
  drawLayer(s_layerFar, s_layerTopFar, S.skyOff, HY + 4 - LH);
  drawLayer(s_layerNear, s_layerTopNear, S.hillOff, HY + 6 - LH);
  if ((D->fx & FX_LIGHTNING) && S.flash > 0) rect(0, 0, W, HY + 8, rgb(0xdce6ff), (uint8_t)(S.flash * 0.35f * 255));
  rect(0, HY + 4, W, H - HY - 4, rgb(D->fog));
}

static void project(float worldY, float worldZ, float camX, float camY, float camZ, float &sx, float &sy, float &sw, float &sc, float &cy) {
  float cz = worldZ - camZ;
  if (cz < 0.01f) cz = 0.01f;
  sc = DEPTH / cz;
  cy = worldY - camY;
  sx = W / 2 + sc * -camX * W / 2;
  sy = HY - sc * cy * W / 2;
  sw = sc * ROADW * W / 2;
}

static void drawSegment(const Seg &s, const Proj &p) {
  float x1 = p.x1, y1 = p.y1, w1 = p.w1, x2 = p.x2, y2 = p.y2 - 0.6f, w2 = p.w2;
  bool tunnel = s.flags & SG_TUNNEL;
  int col = (s.flags & SG_COLOR) ? 1 : 0;
  g_fogHex = tunnel ? shadeHex(D->ceil, 0.3f) : D->fog;
  g_fogT = p.fog;
  if (y2 < y1) {
    uint16_t gc = tunnel ? fogged(shadeHex(D->wall, col ? 0.2f : 0.3f)) : fogged(D->grass[col]);
    rect(0, y2, W, y1 - y2 + 1, gc);
    float rw1 = w1 * 1.12f, rw2 = w2 * 1.12f;
    trap(x1 - rw1, x1 + rw1, y1, x2 - rw2, x2 + rw2, y2, fogged(D->rumble[col]));
    trap(x1 - w1, x1 + w1, y1, x2 - w2, x2 + w2, y2, fogged(D->road[col]));
    if (s.flags & SG_FINISH) {
      uint16_t fc = fogged(0xf4f4f4);
      int idx = segIndex(s);
      for (int i = 0; i < 8; i++) {
        float a = -1 + i / 4.0f, b = -1 + (i + 1) / 4.0f;
        if ((i + idx) % 2) trap(x1 + w1 * a, x1 + w1 * b, y1, x2 + w2 * a, x2 + w2 * b, y2, fc);
      }
    } else if (col == 0) {
      int L = D->lanes;
      float lw1 = w1 * 0.022f, lw2 = w2 * 0.022f;
      uint16_t lc = fogged(D->lane);
      for (int l = 1; l < L; l++) {
        float lx1 = x1 - w1 + l * 2 * w1 / L, lx2 = x2 - w2 + l * 2 * w2 / L;
        trap(lx1 - lw1, lx1 + lw1, y1, lx2 - lw2, lx2 + lw2, y2, lc);
      }
    }
    if (D->wet) {
      uint8_t a = (uint8_t)(0.1f * (1 - p.fog) * 255);
      uint16_t wc = rgb(0xcfe6ff);
      trap(x1 - w1 * 0.5f, x1 - w1 * 0.3f, y1, x2 - w2 * 0.5f, x2 - w2 * 0.3f, y2, wc, a);
      trap(x1 + w1 * 0.3f, x1 + w1 * 0.5f, y1, x2 + w2 * 0.3f, x2 + w2 * 0.5f, y2, wc, a);
    }
  }
  if (tunnel) {
    const float TH = 2600;
    float c1 = y1 - p.s1 * TH * W / 2, c2 = p.y2 - p.s2 * TH * W / 2;
    float xl1 = x1 - w1 * 1.25f, xl2 = x2 - w2 * 1.25f, xr1 = x1 + w1 * 1.25f, xr2 = x2 + w2 * 1.25f;
    uint16_t wc = fogged(shadeHex(D->wall, col ? 0 : 0.12f));
    quad4(xl1, y1, xl2, p.y2, xl2, c2, xl1, c1, wc);
    quad4(xr1, y1, xr2, p.y2, xr2, c2, xr1, c1, wc);
    quad4(xl1, c1, xr1, c1, xr2, c2, xl2, c2, fogged(D->ceil));
    if (segIndex(s) % 6 == 0) {
      uint16_t tl = fogged(D->tlight);
      quad4(x1 - w1 * 0.08f, c1, x1 + w1 * 0.08f, c1, x2 + w2 * 0.08f, c2, x2 - w2 * 0.08f, c2, tl);
      float sh = (y1 - c1) * 0.55f;
      quad4(xl1, y1 - sh, xl2, p.y2 - (p.y2 - c2) * 0.55f, xl2, p.y2 - (p.y2 - c2) * 0.6f, xl1, y1 - sh * 1.09f, tl);
      quad4(xr1, y1 - sh, xr2, p.y2 - (p.y2 - c2) * 0.55f, xr2, p.y2 - (p.y2 - c2) * 0.6f, xr1, y1 - sh * 1.09f, tl);
    }
    if (s.flags & SG_PORTAL) {
      uint16_t fc = fogged(D->facade);
      rect(0, 0, W, c1, fc);
      rect(0, c1, xl1, y1 - c1, fc);
      rect(xr1, c1, W - xr1, y1 - c1, fc);
      float lw = fmaxf(1, p.s1 * 120 * W / 2);
      uint16_t tl = fogged(D->tlight);
      line(xl1, y1, xl1, c1, lw, tl);
      line(xl1, c1, xr1, c1, lw, tl);
      line(xr1, c1, xr1, y1, lw, tl);
    }
  }
}

static void strokeRect(float x, float y, float w, float h, float lw, uint16_t c, uint8_t a = 255) {
  rect(x - lw / 2, y - lw / 2, w + lw, lw, c, a);
  rect(x - lw / 2, y + h - lw / 2, w + lw, lw, c, a);
  rect(x - lw / 2, y + lw / 2, lw, h - lw, c, a);
  rect(x + w - lw / 2, y + lw / 2, lw, h - lw, c, a);
}

static void arcLine(float cx, float cy, float r, float a0, float a1, float lw, uint16_t c, int segs = 5) {
  float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
  for (int i = 1; i <= segs; i++) {
    float a = a0 + (a1 - a0) * i / segs, nx = cx + cosf(a) * r, ny = cy + sinf(a) * r;
    line(px, py, nx, ny, lw, c);
    px = nx;
    py = ny;
  }
}

static void heart(float cx, float cy, float r, uint16_t c) {
  circle(cx - r * 0.6f, cy - r * 0.2f, r * 0.7f, c);
  circle(cx + r * 0.6f, cy - r * 0.2f, r * 0.7f, c);
  quad4(cx - r * 1.25f, cy, cx + r * 1.25f, cy, cx + 0.1f, cy + r * 1.3f, cx - 0.1f, cy + r * 1.3f, c);
}

// (x, y) = ground point, k = pixels per world unit.
static void drawProp(const Sprite &spr, float x, float y, float k, int side, float f) {
  auto P2 = [k](float n) { return fmaxf(1, n * k); };
  g_fogHex = D->fog;
  g_fogT = f * 0.85f;
  const float t = s_t;
  switch (spr.kind) {
    case P_LAMP: {
      float h = 2400 * k, armL = 520 * k * -side;
      uint16_t pc = fogged(0x5b5375);
      rect(x - P2(40) / 2, y - h, P2(40), h, pc);
      rect(fminf(x, x + armL), y - h, fabsf(armL), P2(35), pc);
      circle(x + armL, y - h + 60 * k, fminf(40, 190 * k), rgb(D->glow), (uint8_t)(0.16f * (1 - f) * 255));
      circle(x + armL, y - h + 50 * k, P2(60), fogged(0xfff7d6));
      break;
    }
    case P_POST: {
      rect(x - 70 * k, y - 750 * k, 140 * k, 750 * k, fogged(0x6b4a3a));
      uint16_t wc = fogged(0xf4f4f4);
      ellipse(x, y - 820 * k, 170 * k, 75 * k, wc);
      circle(x + 150 * k * side, y - 880 * k, 70 * k, wc);
      rect(x + 200 * k * side - (side < 0 ? 70 * k : 0), y - 885 * k, 70 * k, 30 * k, fogged(0xff9a3d));
      ellipse(x - 40 * k * side, y - 830 * k, 110 * k, 45 * k, fogged(0x9aa3b5));
      break;
    }
    case P_CRATE: {
      float s = 520 * k;
      rect(x - s / 2, y - s, s, s, fogged(0xa8743f));
      uint16_t ec = fogged(0x6e4824);
      float lw = P2(40);
      strokeRect(x - s / 2, y - s, s, s, lw, ec);
      line(x - s / 2, y - s, x + s / 2, y, lw, ec);
      line(x + s / 2, y - s, x - s / 2, y, lw, ec);
      break;
    }
    case P_STALL: {
      float w = 1100 * k, h = 1000 * k;
      rect(x - w / 2, y - h * 0.45f, w, h * 0.45f, fogged(0x3a2230));
      rect(x - w / 2, y - h * 0.5f, w, h * 0.06f, fogged(0xffcf8a));
      uint16_t pc = fogged(0x2a1820);
      rect(x - w * 0.46f, y - h, w * 0.05f, h * 0.5f, pc);
      rect(x + w * 0.41f, y - h, w * 0.05f, h * 0.5f, pc);
      uint32_t c0 = spr.seed > 0.5f ? 0xff5fa2 : 0xffd84f, c1 = spr.seed > 0.5f ? 0xfff1f6 : 0xd6402f;
      uint16_t a0 = fogged(c0), a1 = fogged(c1);
      for (int i = 0; i < 6; i++)
        quad4(x - w * 0.6f + i * w * 1.2f / 6, y - h * 0.85f, x - w * 0.6f + (i + 1) * w * 1.2f / 6, y - h * 0.85f,
             x - w * 0.5f + (i + 1) * w / 6, y - h * 1.05f, x - w * 0.5f + i * w / 6, y - h * 1.05f, i % 2 ? a1 : a0);
      circle(x, y - h * 0.72f, 160 * k, rgb(0xff6a4a), (uint8_t)(0.3f * (1 - f) * 255));
      circle(x, y - h * 0.72f, P2(70), fogged(0xff6a4a));
      if (D->fx & FX_STEAM)
        for (int i = 0; i < 3; i++) {
          float ph = wrapf(t * 0.5f + i / 3.0f + spr.seed, 1);
          circle(x + sinf(ph * 6 + i) * 120 * k, y - h * 1.1f - ph * 900 * k, (140 + ph * 220) * k, 0xFFFF,
                 (uint8_t)(0.28f * (1 - ph) * (1 - f) * 255));
        }
      break;
    }
    case P_SIGN: {
      float ph = 1500 * k, bw = 950 * k, bh = 520 * k;
      uint32_t colHex = D->neon[(int)(spr.seed * D->nNeon) % D->nNeon];
      rect(x - P2(50) / 2, y - ph, P2(50), ph, fogged(0x3a3350));
      rect(x - bw / 2, y - ph - bh, bw, bh, fogged(0x120b20));
      float flick = sinf(t * 9 + spr.seed * 40) > -0.95f ? 1 : 0.3f;
      uint16_t col = rgb(colHex);
      strokeRect(x - bw / 2, y - ph - bh, bw, bh, P2(110), col, (uint8_t)(0.3f * flick * (1 - f) * 255));
      uint16_t cf = mix(colHex, D->fog, f * 0.7f);
      if (flick < 1) cf = mix(colHex, 0x120b20, 0.7f);
      strokeRect(x - bw / 2, y - ph - bh, bw, bh, P2(36), cf);
      if (spr.seed > 0.66f) heart(x, y - ph - bh / 2, bh * 0.2f, cf);
      else
        for (int i = 0; i < 3; i++) rect(x - bw * 0.35f, y - ph - bh * (0.75f - i * 0.24f), bw * (0.7f - (i == 1 ? 0.25f : 0)), P2(40), cf);
      break;
    }
    case P_BUILDING: {
      Rng r((uint32_t)(spr.seed * 1e6f));
      float w = (1800 + spr.seed * 1400) * k, h = (3200 + r() * 3600) * k;
      rect(x - w / 2, y - h, w, h, fogged(shadeHex(D->farCol, 0.1f)));
      if (w > 24) {
        uint16_t lit = fogged(D->farAccent ? D->farAccent : 0xffd27a);
        for (float wy = y - h + h * 0.06f; wy < y - h * 0.08f; wy += h * 0.09f)
          for (float wx = x - w * 0.4f; wx < x + w * 0.38f; wx += w * 0.16f)
            if (r() < 0.45f) rect(wx, wy, w * 0.08f, h * 0.04f, lit);
      }
      break;
    }
    case P_HOUSE: {
      float w = 1700 * k, h = 1300 * k;
      rect(x - w / 2, y - h, w, h, fogged(spr.seed > 0.5f ? 0xc9a27a : 0xb5806a));
      quad4(x - w * 0.6f, y - h, x + w * 0.6f, y - h, x + w * 0.2f, y - h * 1.6f, x - w * 0.2f, y - h * 1.6f, fogged(0x7a3b3b));
      rect(x - w * 0.1f, y - h * 0.5f, w * 0.2f, h * 0.5f, fogged(0x4a2a22));
      uint16_t wc = fogged(0xffd27a);
      rect(x - w * 0.38f, y - h * 0.75f, w * 0.18f, h * 0.22f, wc);
      rect(x + w * 0.2f, y - h * 0.75f, w * 0.18f, h * 0.22f, wc);
      break;
    }
    case P_HAY: {
      float w = 900 * k, h = 560 * k;
      roundRect(x - w / 2, y - h, w, h, h * 0.25f, fogged(0xe2b84a));
      uint16_t bc = fogged(0xb88a2a);
      for (int i = 1; i < 4; i++) rect(x - w / 2, y - h + i * h / 4, w, P2(25), bc);
      break;
    }
    case P_PINE: {
      uint16_t tc = fogged(D->tree);
      rect(x - 70 * k, y - 420 * k, 140 * k, 420 * k, fogged(0x5a3a2a));
      for (int i = 0; i < 3; i++) {
        float by = y - 350 * k - i * 420 * k, bw = (1000 - i * 230) * k;
        quad4(x - bw / 2, by, x + bw / 2, by, x + 1, by - 720 * k, x - 1, by - 720 * k, tc);
      }
      break;
    }
    case P_ROCK: {
      float w = 900 * k * (0.7f + spr.seed * 0.6f), h = 560 * k;
      float pts[10] = {x - w / 2, y, x - w * 0.4f, y - h * 0.6f, x - w * 0.1f, y - h, x + w * 0.3f, y - h * 0.8f, x + w / 2, y};
      poly(pts, 5, fogged(0x6d6478));
      quad4(x - w * 0.1f, y - h, x + w * 0.3f, y - h * 0.8f, x + w * 0.15f, y - h * 0.55f, x - w * 0.15f, y - h * 0.7f, fogged(0x8a8296));
      break;
    }
    case P_GOAT: {
      float bob = fabsf(sinf(t * 7 + spr.seed * 9)) * 30 * k, by = y - 430 * k - bob;
      int face = cosf(t * 0.5f + spr.seed * 9) > 0 ? 1 : -1;
      uint16_t lc = fogged(0x3a3340);
      const float legs[4] = {-220, -100, 100, 220};
      for (float lx : legs) rect(x + lx * k - 25 * k, by + 100 * k, P2(50), 300 * k + bob, lc);
      uint16_t bc = fogged(0xf4f0e6);
      ellipse(x, by, 340 * k, 190 * k, bc);
      circle(x + face * 330 * k, by - 150 * k, 130 * k, bc);
      arcLine(x + face * 300 * k, by - 280 * k, 90 * k, 3.14159f, 3.14159f * 1.9f, P2(40), fogged(0x9a8a70), 4);
      circle(x + face * 380 * k, by - 170 * k, P2(22), fogged(0x222222));
      break;
    }
    case P_DUCK: {
      if (spr.flags & SP_TAKEN) break;
      float bob = sinf(t * 4) * 30 * k;
      uint16_t yc = fogged(0xffd84f);
      ellipse(x, y - 180 * k + bob, 270 * k, 170 * k, yc);
      circle(x - 130 * k * side, y - 390 * k + bob, 120 * k, yc);
      ellipse(x - 250 * k * side, y - 380 * k + bob, 80 * k, 35 * k, fogged(0xff8a1f));
      circle(x - 160 * k * side, y - 420 * k + bob, P2(20), rgb(0x222222));
      if (sinf(t * 5) > 0.6f) {
        rect(x + 200 * k, y - 560 * k, P2(30), P2(120), 0xFFFF);
        rect(x + 155 * k, y - 515 * k, P2(120), P2(30), 0xFFFF);
      }
      break;
    }
    case P_CANDY: {
      if (spr.flags & SP_TAKEN) break;
      float cy = y - 300 * k - fabsf(sinf(t * 5)) * 80 * k, r = 140 * k;
      uint16_t wc = fogged(0xffffff);
      quad4(x - r * 0.8f, cy, x - r * 2, cy - r * 0.7f, x - r * 2, cy + r * 0.7f, x - r * 0.8f, cy, wc);
      quad4(x + r * 0.8f, cy, x + r * 2, cy - r * 0.7f, x + r * 2, cy + r * 0.7f, x + r * 0.8f, cy, wc);
      circle(x, cy, r, fogged(0xff5fa2));
      arcLine(x, cy, r * 0.55f, 0, 3.14159f * 1.4f, P2(35), wc, 6);
      break;
    }
    case P_NOTE: {
      if (spr.flags & SP_TAKEN) break;
      float cy = y - 380 * k + sinf(t * 3) * 60 * k;
      uint16_t nc = fogged(0x7fffd4);
      circle(x, cy - 100 * k, 320 * k, rgb(0x7fffd4), 64);
      ellipse(x - 60 * k, cy, 130 * k, 95 * k, nc);
      rect(x + 50 * k, cy - 420 * k, P2(45), 420 * k, nc);
      quad4(x + 50 * k, cy - 420 * k, x + 240 * k, cy - 320 * k, x + 240 * k, cy - 250 * k, x + 50 * k, cy - 340 * k, nc);
      break;
    }
    case P_TAKEOUT: {
      if (spr.flags & SP_TAKEN) break;
      float w = 380 * k, h = 330 * k;
      quad4(x - w / 2, y - h, x + w / 2, y - h, x + w * 0.38f, y, x - w * 0.38f, y, fogged(0xf4f4f4));
      rect(x - w * 0.42f, y - h * 0.62f, w * 0.84f, h * 0.18f, fogged(0xd6402f));
      uint16_t sc = fogged(0xc99a5a);
      line(x - w * 0.1f, y - h, x + w * 0.2f, y - h * 1.6f, P2(25), sc);
      line(x + w * 0.05f, y - h, x + w * 0.35f, y - h * 1.5f, P2(25), sc);
      break;
    }
    case P_CACTUS: {
      uint16_t c = fogged(0x3f9a52);
      roundRect(x - 110 * k, y - 1300 * k, 220 * k, 1300 * k, 110 * k, c);
      roundRect(x - 430 * k, y - 950 * k, 150 * k, 420 * k, 75 * k, c);
      rect(x - 430 * k, y - 600 * k, 340 * k, 120 * k, c);
      roundRect(x + 280 * k, y - 1100 * k, 150 * k, 420 * k, 75 * k, c);
      rect(x + 90 * k, y - 760 * k, 340 * k, 120 * k, c);
      rect(x - 30 * k, y - 1250 * k, P2(30), 1150 * k, fogged(0x5cc070));
      break;
    }
    case P_TUMBLEWEED: {
      float ox = sinf(t * 0.8f + spr.seed * 10) * 900 * k, r = 260 * k;
      float by = y - r - fabsf(sinf(t * 5 + spr.seed)) * 180 * k;
      uint16_t c = fogged(0x9a6a3a);
      float lw = P2(30);
      for (int i = 0; i < 4; i++) {
        float ang = t * 3 + i, ca = cosf(ang), sa = sinf(ang);
        float px = 0, py = 0;
        for (int j = 0; j <= 10; j++) {
          float a = j * 0.6283185f, ex = cosf(a) * r, ey = sinf(a) * r * 0.6f;
          float nx = x + ox + ex * ca - ey * sa, ny = by + ex * sa + ey * ca;
          if (j) line(px, py, nx, ny, lw, c);
          px = nx;
          py = ny;
        }
      }
      break;
    }
    case P_PALM: {
      uint16_t tc = fogged(0x8a5a3a);
      float lw = P2(110);
      float x0 = x, y0 = y, cx = x - side * 300 * k, cy = y - 1400 * k, x2 = x - side * 150 * k, y2 = y - 2600 * k;
      float px = x0, py = y0;
      for (int i = 1; i <= 5; i++) {
        float u = i / 5.0f, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
        float nx = a * x0 + b * cx + c * x2, ny = a * y0 + b * cy + c * y2;
        line(px, py, nx, ny, lw, tc);
        px = nx;
        py = ny;
      }
      uint16_t lc = fogged(0x2f8a5a);
      for (int i = 0; i < 7; i++) {
        float a = -3.14159f + i * 3.14159f / 6 + sinf(t * 1.5f + i) * 0.04f;
        ellipseRot(x2 + cosf(a) * 450 * k, y2 + sinf(a) * 260 * k + 120 * k, 520 * k, 110 * k, a, lc);
      }
      break;
    }
    case P_BILLBOARD: {
      float w = 2100 * k, h = 950 * k, py = y - 1500 * k;
      uint16_t pc = fogged(0x3a3350);
      rect(x - w * 0.3f, py, P2(60), 1500 * k, pc);
      rect(x + w * 0.3f, py, P2(60), 1500 * k, pc);
      uint16_t pink = fogged(0xff5fa2);
      rect(x - w / 2, py - h, w, h, pink);
      rect(x - w / 2 + w * 0.03f, py - h + h * 0.06f, w * 0.94f, h * 0.88f, fogged(0x2a0f22));
      if (spr.seed > 0.5f) {
        heart(x, py - h / 2 - h * 0.06f, h * 0.22f, pink);
      } else {
        for (int i = 0; i < 3; i++) {
          float cx = x - w * 0.22f + i * w * 0.18f, cy = py - h / 2;
          line(cx - h * 0.12f, cy - h * 0.22f, cx + h * 0.12f, cy, P2(70), pink);
          line(cx + h * 0.12f, cy, cx - h * 0.12f, cy + h * 0.22f, P2(70), pink);
        }
      }
      break;
    }
    case P_PYLON: {
      float h = 6500 * k;
      rect(x - 140 * k, y - h, 280 * k, h, fogged(0xc9cfdd));
      uint16_t cc = fogged(0xe8ecf5);
      for (int i = 1; i <= 5; i++) line(x, y - h + 200 * k, x - side * 2200 * k * i / 5, y - 900 * k, P2(22), cc);
      circle(x, y - h - 60 * k, P2(70), sinf(t * 4) > 0 ? rgb(0xff3d5a) : fogged(0x5a2030));
      break;
    }
  }
}

struct CarOpts {
  bool baron = false, brake = false;
  float sleep = 0, flash = 0, flame = 0, cab = 0;
};

static void drawCar(float x, float y, float w, CarKind kind, uint32_t colHex, const CarOpts &o) {
  uint16_t col = fogged(colHex), dk = fogged(shadeHex(colHex, 0.35f));
  ellipse(x, y, w * 0.56f, w * 0.06f, 0x0000, 89);
  uint16_t tyre = fogged(0x111111);
  if (kind == K_TRUCK) {
    rect(x - w * 0.46f, y - w * 0.14f, w * 0.22f, w * 0.14f, tyre);
    rect(x + w * 0.24f, y - w * 0.14f, w * 0.22f, w * 0.14f, tyre);
    roundRect(x - w / 2, y - w * 1.05f, w, w * 0.92f, w * 0.04f, col);
    rect(x - 1, y - w, fmaxf(1, w * 0.012f), w * 0.84f, dk);
    rect(x - w * 0.46f, y - w * 0.98f, w * 0.92f, w * 0.03f, dk);
    uint16_t tl = fogged(0xff3355);
    rect(x - w * 0.46f, y - w * 0.24f, w * 0.1f, w * 0.05f, tl);
    rect(x + w * 0.36f, y - w * 0.24f, w * 0.1f, w * 0.05f, tl);
    rect(x - w / 2, y - w * 0.16f, w, w * 0.04f, fogged(0x222222));
    return;
  }
  bool ev = kind == K_PUFF_EV, ice = kind == K_PUFF_ICE, tiny = kind == K_TINY, van = kind == K_VAN;
  float bw = tiny ? w * 0.82f : w;
  rect(x - bw * 0.46f, y - w * 0.15f, bw * 0.2f, w * 0.15f, tyre);
  rect(x + bw * 0.26f, y - w * 0.15f, bw * 0.2f, w * 0.15f, tyre);
  float top = van ? 0.82f : tiny ? 0.72f : ev ? 0.6f : 0.62f;
  float cab = o.cab;
  float ti = tiny ? 0.36f : 0.31f, ti2 = tiny ? 0.31f : 0.27f;
  quad4(x - bw * 0.42f, y - w * 0.38f, x + bw * 0.42f, y - w * 0.38f, x + bw * ti + cab, y - w * top, x - bw * ti + cab, y - w * top, dk);
  quad4(x - bw * 0.36f, y - w * 0.4f, x + bw * 0.36f, y - w * 0.4f, x + bw * ti2 + cab, y - w * (top - 0.04f), x - bw * ti2 + cab, y - w * (top - 0.04f), fogged(0x1b2238));
  quad4(x - bw * 0.1f, y - w * 0.4f, x + bw * 0.02f, y - w * 0.4f, x + bw * 0.16f + cab, y - w * (top - 0.04f), x + bw * 0.04f + cab, y - w * (top - 0.04f), 0xFFFF, 46);
  roundRect(x - bw / 2, y - w * 0.42f, bw, w * 0.29f, w * (ev || tiny ? 0.11f : 0.06f), col);
  if (ice) {
    rect(x - w * 0.08f, y - w * 0.42f, w * 0.04f, w * 0.29f, 0xFFFF);
    rect(x + w * 0.04f, y - w * 0.42f, w * 0.04f, w * 0.29f, 0xFFFF);
  }
  if (ev) {
    roundRect(x - bw * 0.42f, y - w * 0.36f, bw * 0.84f, w * 0.04f, w * 0.02f, rgb(o.brake ? 0xff6a8a : 0xff2f5f));
    if (o.brake) roundRect(x - bw * 0.46f, y - w * 0.39f, bw * 0.92f, w * 0.1f, w * 0.04f, rgb(0xff2f5f), 77);
    float hx = x + bw * 0.28f, hy = y - w * 0.26f, hr = w * 0.025f;
    heart(hx, hy + hr * 0.3f, hr * 1.6f, 0xFFFF);
  } else {
    uint16_t tl = fogged(o.brake ? 0xff7a8a : 0xff3355);
    rect(x - bw * 0.45f, y - w * 0.37f, bw * 0.17f, w * 0.06f, tl);
    rect(x + bw * 0.28f, y - w * 0.37f, bw * 0.17f, w * 0.06f, tl);
    if (o.brake) {
      rect(x - bw * 0.48f, y - w * 0.4f, bw * 0.23f, w * 0.12f, rgb(0xff3355), 89);
      rect(x + bw * 0.25f, y - w * 0.4f, bw * 0.23f, w * 0.12f, rgb(0xff3355), 89);
    }
  }
  rect(x - w * 0.09f, y - w * 0.27f, w * 0.18f, w * 0.06f, fogged(0xe8e8e8));
  rect(x - bw * 0.5f, y - w * 0.17f, bw, w * 0.045f, fogged(0x1a1a22));
  if (ice) {
    uint16_t pc = rgb(0xc9c9d4), hc = rgb(0x222222);
    circle(x - w * 0.2f, y - w * 0.14f, w * 0.035f, pc);
    circle(x + w * 0.2f, y - w * 0.14f, w * 0.035f, pc);
    circle(x - w * 0.2f, y - w * 0.14f, w * 0.02f, hc);
    circle(x + w * 0.2f, y - w * 0.14f, w * 0.02f, hc);
    rect(x - w * 0.47f, y - w * 0.5f, w * 0.94f, w * 0.045f, dk);
    rect(x - w * 0.36f, y - w * 0.46f, w * 0.03f, w * 0.05f, dk);
    rect(x + w * 0.33f, y - w * 0.46f, w * 0.03f, w * 0.05f, dk);
    if (o.flame > 0) {
      const float exs[2] = {-0.2f, 0.2f};
      for (float ex : exs) {
        float fl = w * (0.08f + frand() * 0.08f);
        ellipse(x + w * ex, y - w * 0.14f + fl * 0.6f, w * 0.035f, fl, rgb(0xffb347));
        ellipse(x + w * ex, y - w * 0.14f + fl * 0.3f, w * 0.018f, fl * 0.5f, rgb(0xfff2a8));
      }
    }
  }
  if (kind == K_POLICE) {
    bool on = sinf(s_t * 14) > 0;
    rect(x - w * 0.22f, y - w * (top + 0.05f), w * 0.2f, w * 0.05f, rgb(on ? 0xff3d5a : 0x5a1d28));
    rect(x + w * 0.02f, y - w * (top + 0.05f), w * 0.2f, w * 0.05f, rgb(on ? 0x3a4a8a : 0x3d7bff));
  }
  if (o.baron) {
    float lw = fmaxf(1, w * 0.025f);
    arcLine(x - w * 0.07f, y - w * 0.48f, w * 0.07f, 0.1f * 3.14159f, 0.95f * 3.14159f, lw, 0x0000, 4);
    arcLine(x + w * 0.07f, y - w * 0.48f, w * 0.07f, 0.05f * 3.14159f, 0.9f * 3.14159f, lw, 0x0000, 4);
  }
  if (o.sleep > 0 && w > 10) {
    float zs = fmaxf(4, w * 0.12f), zx = x + w * 0.3f, zy = y - w * (top + 0.1f) - fmodf(s_t * 20, 10) - zs;
    uint16_t zc = rgb(0x7fffd4);
    line(zx, zy, zx + zs, zy, 1.5f, zc);
    line(zx + zs, zy, zx, zy + zs, 1.5f, zc);
    line(zx, zy + zs, zx + zs, zy + zs, 1.5f, zc);
  }
  if (o.flash > 0) circle(x - w * 0.2f, y - w * (top + 0.05f), w * 0.12f, 0xFFFF);
}

static void renderWorld() {
  float posZ = wrapf(P.dist, s_trackLength);
  Seg &baseSeg = segAt(P.dist);
  int baseIdx = segIndex(baseSeg);
  float basePct = wrapf(P.dist, SEG) / SEG;
  float pz = P.dist + PLAYER_Z;
  Seg &pseg = segAt(pz);
  float pPct = wrapf(pz, SEG) / SEG;
  float playerY = lerpf(pseg.y1, pseg.y2, pPct);
  drawSky(pseg.flags & SG_TUNNEL);

  // Cars bucketed by how far ahead their segment is.
  static int8_t head[DRAW];
  static int8_t nextCar[MAX_CARS];
  memset(head, -1, sizeof(head));
  for (int i = 0; i < s_nCars; i++) {
    int n = (segIndex(segAt(s_cars[i].dist)) - baseIdx + s_N) % s_N;
    if (n >= DRAW) continue;
    // Keep each bucket ordered farthest first.
    int8_t *pp = &head[n];
    while (*pp >= 0 && wrapf(s_cars[*pp].dist, s_trackLength) > wrapf(s_cars[i].dist, s_trackLength)) pp = &nextCar[*pp];
    nextCar[i] = *pp;
    *pp = (int8_t)i;
  }

  float x = 0, dx = -(baseSeg.curve * basePct);
  float camY = playerY + CAMH;
  for (int n = 0; n < DRAW; n++) {
    int idx = (baseIdx + n) % s_N;
    Seg &s = s_segs[idx];
    Proj &p = s_proj[n];
    p.idx = (uint16_t)idx;
    bool looped = idx < baseIdx;
    float camZ = posZ - (looped ? s_trackLength : 0);
    float fn = (float)n / DRAW;
    p.fog = 1 - expf(-(fn * fn) * 4.2f);
    p.camz1 = idx * SEG - camZ;
    project(s.y1, idx * SEG, P.x * ROADW - x, camY, camZ, p.x1, p.y1, p.w1, p.s1, p.cy1);
    project(s.y2, (idx + 1) * SEG, P.x * ROADW - x - dx, camY, camZ, p.x2, p.y2, p.w2, p.s2, p.cy2);
    x += dx;
    dx += s.curve;
  }

  for (int n = DRAW - 1; n >= 0; n--) {
    const Proj &p = s_proj[n];
    if (p.camz1 <= DEPTH * 2) continue;
    const Seg &s = s_segs[p.idx];
    drawSegment(s, p);
    float k = p.s1 * W / 2;
    for (int j = 0; j < s.sprN; j++) {
      const Sprite &spr = s_sprites[s.spr0 + j];
      float o = (spr.flags & SP_HAZARD) ? hazardOffset(spr) : spr.o;
      float sx = p.x1 + k * o * ROADW;
      if (sx < -400 || sx > W + 400) continue;
      if (k * 2600 < 1.5f) continue; // too far to see anything
      drawProp(spr, sx, p.y1, k, spr.o < 0 ? -1 : 1, p.fog);
    }
    for (int8_t ci = head[n]; ci >= 0; ci = nextCar[ci]) {
      const Car &c = s_cars[ci];
      float pct = wrapf(c.dist, SEG) / SEG;
      float sc = lerpf(p.s1, p.s2, pct);
      float cx = lerpf(p.x1, p.x2, pct) + sc * c.offset * ROADW * W / 2;
      float cy = lerpf(p.y1, p.y2, pct);
      float w = sc * CAR_W * W / 2 * (c.kind == K_TRUCK ? 1.15f : 1);
      if (w < 1.5f || cx < -w || cx > W + w) continue;
      g_fogHex = D->fog;
      g_fogT = p.fog * 0.6f;
      CarOpts o;
      o.baron = c.baron;
      o.sleep = c.sleep;
      o.flash = c.flash;
      o.brake = c.rival && fabsf(segAt(c.dist).curve) > 3;
      drawCar(cx, cy, w, c.kind, c.col, o);
    }
  }

  // ---- player ----
  int pn = (segIndex(pseg) - baseIdx + s_N) % s_N;
  if (pn >= DRAW) pn = 0;
  const Proj &pp = s_proj[pn];
  float pk = DEPTH / PLAYER_Z * W / 2;
  float carY = HY - (DEPTH / PLAYER_Z) * lerpf(pp.cy1, pp.cy2, pPct) * W / 2 + P.bounce;
  for (int i = 0; i < S.nTrail; i++) {
    const Particle &p = S.trail[i];
    circle(p.x, p.y, 3 + (0.5f - p.life) * 10, rgb(p.life > 0.3f ? 0xff9fd0 : 0xff5fa2), (uint8_t)clampf(p.life * 1.6f * 255, 0, 255));
  }
  float wob = P.spin > 0 ? sinf(P.spin * 30) * 0.25f : 0;
  g_fogT = 0;
  CarOpts o;
  o.brake = P.braking;
  o.flame = P.flame + (P.boosting > 0 ? 0.1f : 0);
  o.cab = -P.steer * pk * CAR_W * 0.04f;
  float carX = W / 2 + wob * 40 + P.steer * (P.speed > 0 ? 2 : 0);
  drawCar(carX, carY, pk * CAR_W, s_ev ? K_PUFF_EV : K_PUFF_ICE, s_ev ? 0xff6fae : 0xe8407f, o);
  if (P.spin > 0)
    for (int i = 0; i < 4; i++) {
      float a = s_t * 8 + i * 1.5707963f;
      star(W / 2 + cosf(a) * 40, carY - pk * CAR_W * 0.7f + sinf(a) * 9, 4, rgb(0xffd84f));
    }
}

static void renderFx() {
  uint8_t fx = D->fx;
  if (fx & FX_HEAT) {
    static uint16_t row[FW];
    const int y0 = (HY - 4) / 2, h = 20;
    for (int y = 0; y < h; y++) {
      int sh = (int)lroundf(sinf(y * 1.1f + s_t * 7) * 0.9f * (1 - (float)y / h));
      if (!sh) continue;
      uint16_t *r = s_frame + (y0 + y) * FW;
      memcpy(row, r, sizeof(row));
      for (int x = 0; x < FW; x++) {
        int sx = x + sh;
        r[x] = row[sx < 0 ? 0 : sx >= FW ? FW - 1 : sx];
      }
    }
  }
  if (fx & FX_SAND) {
    uint16_t c = rgb(0xffe6b4);
    for (int i = 0; i < 50; i++) rect(S.sandX[i], S.sandY[i], 3, 1, c, 140);
  }
  if (fx & FX_RAIN) {
    uint16_t c = rgb(0xaac8ff);
    for (int i = 0; i < 130; i++) {
      for (int j = 0; j < 5; j++) {
        int px = (int)(S.rainX[i] * 0.5f + j * 0.3f), py = (int)(S.rainY[i] * 0.5f - j);
        if (px >= 0 && px < FW && py >= 0 && py < FH) {
          uint16_t &d = s_frame[py * FW + px];
          d = blend(d, c, 115);
        }
      }
    }
  }
  if (fx & FX_SIREN) {
    bool ph = sinf(s_t * 8) > 0;
    float nearV = 0.4f;
    for (int i = 0; i < s_nCars; i++)
      if (s_cars[i].police) nearV = clampf(1 - fabsf(s_cars[i].dist - (P.dist + PLAYER_Z)) / (SEG * 60), 0.25f, 1);
    uint8_t a = (uint8_t)(0.25f * nearV * 255);
    rect(0, 0, 18, H, rgb(ph ? 0xff2244 : 0x2255ff), a);
    rect(W - 18, 0, 18, H, rgb(ph ? 0x2255ff : 0xff2244), a);
  }
  if (S.note > 0) rect(0, 0, W, H, rgb(0x7fffd4), (uint8_t)(fminf(1, S.note) * 0.12f * 255));
  if (P.boosting > 0) {
    for (int i = 0; i < 14; i++) {
      float a = frand() * 6.2831853f, r1 = 170 + frand() * 60, cx = W / 2, cy = HY + 20;
      line(cx + cosf(a) * r1, cy + sinf(a) * r1 * 0.7f, cx + cosf(a) * (r1 + 60), cy + sinf(a) * (r1 + 60) * 0.7f, 2, 0xFFFF, 140);
    }
  }
}

// ---------------------------------------------------------------------------
// HUD. Coordinates are in screen pixels (456x280) like everything else;
// text is the built-in 5x7 font drawn into the half-resolution frame, so
// size 1 is 12x16 on screen and size 2 is 24x32.
// ---------------------------------------------------------------------------

static void glass(float x, float y, float w, float h, float r = 10) {
  roundRect(x, y, w, h, r, rgb(0x140c24), 150); // darken first so text reads over anything
  roundRect(x, y, w, h, r, 0xFFFF, 26);
  uint8_t ea = 110;
  rect(x + r, y, w - 2 * r, 2, 0xFFFF, ea);
  rect(x + r, y + h - 2, w - 2 * r, 2, 0xFFFF, ea);
  rect(x, y + r, 2, h - 2 * r, 0xFFFF, ea);
  rect(x + w - 2, y + r, 2, h - 2 * r, 0xFFFF, ea);
}

static void duckIcon(float dx, float y, bool lit) {
  uint8_t a = lit ? 255 : 77;
  ellipse(dx, y + 6, 7, 5, rgb(0xffd84f), a);
  circle(dx - 4, y - 1, 4, rgb(0xffd84f), a);
  rect(dx - 11, y - 2, 4, 2, rgb(0xff8a1f), a);
}

static const char *ord(int n) {
  static char b[6];
  snprintf(b, sizeof(b), "%d%s", n, n == 1 ? "st" : n == 2 ? "nd" : n == 3 ? "rd" : "th");
  return b;
}

static void fmtTime(float t, char *out, size_t n) {
  int m = (int)(t / 60);
  float s = t - m * 60;
  snprintf(out, n, "%d:%05.2f", m, s);
}

static const uint16_t SHADOW = 0x1062; // deep plum

static int textW(const char *s, int size) { return (int)strlen(s) * 12 * size; }

// One glyph into the half-res frame at low-res (lx, ly), `size` low-res px per dot.
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

static void txt(int x, int y, const char *s, int size, uint16_t col) {
  int lx = x / 2, ly = y / 2;
  for (const char *p = s; *p; p++, lx += 6 * size) {
    glyph(lx + 1, ly + 1, (unsigned char)*p, size, SHADOW);
    glyph(lx, ly, (unsigned char)*p, size, col);
  }
}
static void txtC(int cx, int y, const char *s, int size, uint16_t col) { txt(cx - textW(s, size) / 2, y, s, size, col); }
// Size 2 if it fits in maxW, else size 1.
static void txtFit(int cx, int y, const char *s, int maxW, uint16_t col) { txtC(cx, y, s, textW(s, 2) <= maxW ? 2 : 1, col); }
static void upper(const char *in, char *out, size_t n) {
  size_t i = 0;
  for (; in[i] && i < n - 1; i++) out[i] = (char)toupper((unsigned char)in[i]);
  out[i] = 0;
}
// Word-wraps centred size-1 text. Returns the next line's y.
static int wrapC(int cx, int y, const char *s, int maxChars, uint16_t col) {
  char line[64];
  const char *p = s;
  while (*p) {
    int len = strlen(p);
    int take = len;
    if (len > maxChars) {
      take = maxChars;
      while (take > 0 && p[take] != ' ') take--;
      if (take == 0) take = maxChars;
    }
    memcpy(line, p, take);
    line[take] = 0;
    txtC(cx, y, line, 1, col);
    y += 18;
    p += take;
    while (*p == ' ') p++;
  }
  return y;
}

static void hud() {
  const uint16_t TXT_WHITE = 0xFFFF, TXT_DIM = rgb(0xcfc3ea), TXT_PINK = rgb(0xff9fd0);
  char b[64], u[40];

  if (s_mode == Mode::Pick) {
    txtC(W / 2, 6, "LULLABY RUN", 2, rgb(0xff5fa2));
    glass(12, 42, W - 24, 192, 14);
    bool locked = s_dIndex > s_unlocked;
    snprintf(b, sizeof(b), "< DISTRICT %d/%d >", s_dIndex + 1, NUM_DIST);
    txtC(W / 2, 50, b, 1, TXT_DIM);
    upper(D->name, u, sizeof(u));
    txtFit(W / 2, 70, u, W - 40, TXT_WHITE);
    upper(D->rival, u, sizeof(u));
    snprintf(b, sizeof(b), "VS %s", u);
    txtC(W / 2, 106, b, 1, rgb(D->rc));
    snprintf(b, sizeof(b), "\"%s\"", D->taunt);
    int y = wrapC(W / 2, 128, b, 34, TXT_PINK);
    y = y < 168 ? 168 : y + 2;
    if (locked) {
      snprintf(b, sizeof(b), "LOCKED: top 3 in %d opens it", s_unlocked + 1);
      txtC(W / 2, y, b, 1, rgb(0xff8a8a));
    } else if (s_best[s_dIndex] > 0) {
      char tb[16];
      fmtTime(s_best[s_dIndex], tb, sizeof(tb));
      snprintf(b, sizeof(b), "BEST %s  DUCKS %d/3", tb, s_bestDucks[s_dIndex]);
      txtC(W / 2, y, b, 1, rgb(0xffd84f));
    } else {
      txtC(W / 2, y, "Not raced yet", 1, TXT_DIM);
    }
    snprintf(b, sizeof(b), "CAR: %s", s_ev ? "MARSHMALLOW EV" : "GRUMBLER ICE");
    txtC(W / 2, 208, b, 1, rgb(s_ev ? 0x5ff7ff : 0xffb347));
    for (int i = 0; i < NUM_DIST; i++) {
      float cx = W / 2 - 110 + i * 22;
      uint16_t c = i == s_dIndex ? rgb(0xff5fa2) : i <= s_unlocked ? 0xFFFF : rgb(0x5a5070);
      circle(cx, 244, i == s_dIndex ? 6 : 3, c);
    }
    txtC(W / 2, 258, "SELECT race  DOWN car  UP home", 1, TXT_WHITE);
    return;
  }

  // Position.
  glass(6, 6, 112, 44);
  txt(14, 12, ord(S.place), 2, S.place == 1 ? rgb(0xffd84f) : TXT_WHITE);
  txt(88, 28, "/6", 1, TXT_DIM);

  // Progress strip.
  const float bx = 140, bw = 168, by = 26;
  glass(bx - 10, 6, bw + 20, 36);
  rect(bx, by, bw, 2, 0xFFFF, 64);
  for (int i = 0; i < 4; i++) rect(bx + bw - 2, by - 6 + i * 4, 4, 4, i % 2 ? rgb(0x111111) : 0xFFFF);
  for (int i = 0; i < s_nCars; i++) {
    const Car &c = s_cars[i];
    if (!c.rival) continue;
    float p = clampf((c.dist - PLAYER_Z) / s_trackLength, 0, 1);
    circle(bx + p * bw, by + 1, 4, rgb(c.col));
  }
  float pp = clampf(P.dist / s_trackLength, 0, 1);
  circle(bx + pp * bw, by + 1, 7, 0xFFFF);
  circle(bx + pp * bw, by + 1, 5, rgb(0xff5fa2));

  // Ducks.
  glass(W - 82, 6, 76, 36);
  for (int i = 0; i < 3; i++) duckIcon(W - 60 + i * 22, 20, i < P.ducks);

  // Speed.
  glass(6, H - 50, 144, 44);
  int kmh = (int)lroundf(P.speed / MAXV * 240);
  snprintf(b, sizeof(b), "%3d", kmh);
  txt(10, H - 44, b, 2, TXT_WHITE);
  txt(88, H - 46, "KMH", 1, TXT_DIM);
  if (s_ev) txt(88, H - 28, P.braking ? "REGN" : "VOLT", 1, rgb(0x5ff7ff));
  else {
    snprintf(b, sizeof(b), "G%d", P.gear > 6 ? 6 : P.gear);
    txt(88, H - 28, b, 1, rgb(0xffb347));
  }

  // Boost.
  glass(W - 150, H - 50, 144, 44);
  bool ready = P.boost >= 0.4f;
  txt(W - 140, H - 46, ready ? "BOOST!" : "BOOST", 1, ready && sinf(s_t * 6) > 0 ? TXT_PINK : TXT_DIM);
  roundRect(W - 140, H - 24, 124, 12, 6, 0xFFFF, 46);
  if (P.boost > 0.02f) roundRect(W - 140, H - 24, 124 * P.boost, 12, 6, rgb(ready ? 0xff5fa2 : 0xa0476f));
  rect(W - 140 + 124 * 0.4f, H - 24, 2, 12, 0xFFFF, 153);

  for (int i = 0; i < S.nPops; i++) {
    const Pop &p = S.pops[i];
    if (p.t > 1.0f && fmodf(p.t, 0.1f) < 0.05f) continue; // flicker out
    txtC((int)p.x, (int)(HY + 16 - p.t * 40), p.text, 2, p.col);
  }

  if (S.countdown > 0) {
    glass(10, 54, W - 20, 124, 14);
    snprintf(b, sizeof(b), "DISTRICT %d", s_dIndex + 1);
    txtC(W / 2, 62, b, 1, TXT_DIM);
    upper(D->name, u, sizeof(u));
    txtFit(W / 2, 80, u, W - 30, TXT_WHITE);
    upper(D->rival, u, sizeof(u));
    snprintf(b, sizeof(b), "VS %s", u);
    txtC(W / 2, 116, b, 1, rgb(D->rc));
    snprintf(b, sizeof(b), "\"%s\"", D->taunt);
    wrapC(W / 2, 138, b, 35, TXT_PINK);
    if (S.countdown < 3) {
      snprintf(b, sizeof(b), "%d", (int)ceilf(S.countdown));
      txtC(W / 2, 184, b, 3, TXT_WHITE);
    }
  }

  if (S.finished && !s_demo) {
    glass(16, 46, W - 32, 188, 14);
    snprintf(b, sizeof(b), "%s%s", ord(S.place), S.place == 1 ? "!" : "");
    txtC(W / 2, 54, b, 3, S.place == 1 ? rgb(0xffd84f) : TXT_WHITE);
    upper(TITLES[S.place - 1], u, sizeof(u));
    txtC(W / 2, 108, u, 1, TXT_PINK);
    char tb[16];
    fmtTime(S.time, tb, sizeof(tb));
    snprintf(b, sizeof(b), "%s  DUCKS %d/3", tb, P.ducks);
    txtC(W / 2, 130, b, 1, TXT_WHITE);
    int y = 152;
    if (s_dIndex == NUM_DIST - 1 && S.place == 1) {
      txtC(W / 2, y, "The Golden Hubcap is yours!", 1, rgb(0xffd84f));
      txtC(W / 2, y + 18, "...until a seagull stole it", 1, TXT_DIM);
      y += 36;
    } else if (S.unlockedNext) {
      snprintf(b, sizeof(b), "District %d unlocked!", s_dIndex + 2);
      txtC(W / 2, y, b, 1, rgb(0x7fffd4));
      y += 18;
    } else if (S.place > 3) {
      txtC(W / 2, y, "Top 3 opens the next one", 1, TXT_DIM);
      y += 18;
    }
    if (S.newBest && y < 200) txtC(W / 2, y, "NEW BEST TIME", 1, rgb(0xffd84f));
    bool canNext = s_dIndex + 1 < NUM_DIST && s_dIndex + 1 <= s_unlocked;
    txtC(W / 2, 208, canNext ? "SELECT next DOWN retry UP menu" : "SELECT again   UP menu", 1, TXT_DIM);
  }

  if (s_mode == Mode::Paused) {
    glass(W / 2 - 130, H / 2 - 50, 260, 100, 14);
    txtC(W / 2, H / 2 - 40, "PAUSED", 2, TXT_WHITE);
    txtC(W / 2, H / 2 + 2, "SELECT resume", 1, TXT_DIM);
    txtC(W / 2, H / 2 + 22, "DOWN quit", 1, TXT_DIM);
  }
}

// Half-res landscape frame -> panel, straight from internal RAM.
static void present() { HalfRes::present(s_panel, s_gfx->flipped(), s_frame); }

static uint32_t s_us[6]; // time spent per render stage, for the fps report

static void render() {
  uint32_t t0 = micros();
  target(s_frame, FW, FH);
  setScale(0.5f);
  renderWorld();
  uint32_t t1 = micros();
  if (s_input) s_input->poll(); // a slow frame shouldn't swallow a quick tap
  renderFx();
  uint32_t t2 = micros();
  hud();
  setScale(1);
  uint32_t t3 = micros();
  present();
  uint32_t t4 = micros();
  s_us[0] += t1 - t0; s_us[1] += t2 - t1; s_us[2] += t3 - t2; s_us[3] += t4 - t3;
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

static void freeAll() {
  heap_caps_free(s_frame); s_frame = nullptr;
  heap_caps_free(s_layerFar); s_layerFar = nullptr;
  heap_caps_free(s_layerNear); s_layerNear = nullptr;
  heap_caps_free(s_segs); s_segs = nullptr;
  heap_caps_free(s_sprites); s_sprites = nullptr;
}

bool enter(LandscapeGFX *gfx, Arduino_Canvas *canvas, Arduino_GFX *panel) {
  if (s_active) return true;
  s_gfx = gfx;
  s_panel = panel;
  s_canvas = canvas;
  static bool defined = false;
  if (!defined) { defineDistricts(); defined = true; }

  // The frame buffer is written several times over per frame, so it goes in
  // internal RAM when there's comfortably enough; the rest lives in PSRAM.
  s_frame = (uint16_t *)bigAlloc((size_t)FW * FH * 2, true);
  s_layerFar = (uint16_t *)bigAlloc((size_t)LW * LH * 2, false);
  s_layerNear = (uint16_t *)bigAlloc((size_t)LW * LH * 2, false);
  s_segs = (Seg *)bigAlloc(sizeof(Seg) * MAX_SEGS, false);
  s_sprites = (Sprite *)bigAlloc(sizeof(Sprite) * MAX_SPRITES, false);
  if (!s_frame || !s_layerFar || !s_layerNear || !s_segs || !s_sprites) {
    freeAll();
    Serial.println("Lullaby Run: not enough memory");
    return false;
  }
  Serial.printf("Lullaby Run: frame in %s RAM\n", esp_ptr_external_ram(s_frame) ? "PSRAM" : "internal");

  s_prefs.begin("lullaby", false);
  s_ev = s_prefs.getUChar("car", 0) == 0;
  s_unlocked = s_prefs.getUChar("unl", 0);
  if (s_unlocked >= NUM_DIST) s_unlocked = NUM_DIST - 1;
  int d = s_prefs.getUChar("dist", 0);
  if (d > s_unlocked) d = s_unlocked;
  for (int i = 0; i < NUM_DIST; i++) {
    char k[8];
    snprintf(k, sizeof(k), "b%d", i);
    s_best[i] = s_prefs.getFloat(k, 0);
    snprintf(k, sizeof(k), "d%d", i);
    s_bestDucks[i] = s_prefs.getUChar(k, 0);
  }

  s_mode = Mode::Pick;
  s_demo = true;
  s_exit = false;
  s_wheel = 0;
  setDistrict(d);
  RunAudio::start();
  s_active = true;
  return true;
}

void exit() {
  if (!s_active) return;
  RunAudio::stop();
  saveProgress();
  s_prefs.end();
  freeAll();
  s_active = false;
}

bool active() { return s_active; }
const uint16_t *frameBuffer() { return s_frame; }
bool wantsExit() { return s_exit; }
bool wantsAudioPower() { return s_active && RunAudio::running(); }

void pause() {
  if (s_active && s_mode == Mode::Race && !S.finished) s_mode = Mode::Paused;
}

static void startRace(int district) {
  if (district > s_unlocked) return;
  s_demo = false;
  s_mode = Mode::Race;
  if (district != s_dIndex) setDistrict(district);
  else restart();
  saveProgress();
}

static void toPicker() {
  s_mode = Mode::Pick;
  s_demo = true;
  restart();
}

void key(const InputEvent &ev) {
  if (!s_active || !ev.pressed) return;
  switch (s_mode) {
    case Mode::Pick:
      if (ev.key == Key::Left && s_dIndex > 0) setDistrict(s_dIndex - 1);
      else if (ev.key == Key::Right && s_dIndex < NUM_DIST - 1) setDistrict(s_dIndex + 1);
      else if (ev.key == Key::Down) { s_ev = !s_ev; saveProgress(); }
      else if (ev.key == Key::Select) startRace(s_dIndex);
      else if (ev.key == Key::Up) s_exit = true;
      break;
    case Mode::Race:
      if (S.finished) {
        bool canNext = s_dIndex + 1 < NUM_DIST && s_dIndex + 1 <= s_unlocked;
        if (ev.key == Key::Select) startRace(canNext ? s_dIndex + 1 : s_dIndex);
        else if (ev.key == Key::Down) startRace(s_dIndex);
        else if (ev.key == Key::Up) toPicker();
      } else {
        if (ev.key == Key::Select) s_boostTap = true;
        else if (ev.key == Key::Up) s_mode = Mode::Paused;
      }
      break;
    case Mode::Paused:
      // UP doesn't quit here: triple-tapping UP turns the screen off, and
      // that shouldn't throw the race away on its second tap.
      if (ev.key == Key::Select) s_mode = Mode::Race;
      else if (ev.key == Key::Down) toPicker();
      break;
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

  // Steering: arrows held, plus the wheel as a self-centring nudge.
  float steer = (input.held(Key::Left) ? -1.0f : 0) + (input.held(Key::Right) ? 1.0f : 0);
  if (s_mode == Mode::Race) {
    s_wheel = clampf(s_wheel + wheel * 0.8f, -1.5f, 1.5f);
  } else if (s_mode == Mode::Pick && wheel != 0) {
    static int32_t wacc = 0;
    wacc += wheel;
    while (wacc >= 2) { wacc -= 2; if (s_dIndex < NUM_DIST - 1) setDistrict(s_dIndex + 1); }
    while (wacc <= -2) { wacc += 2; if (s_dIndex > 0) setDistrict(s_dIndex - 1); }
  }
  float decay = dt * 3;
  s_wheel = fabsf(s_wheel) <= decay ? 0 : s_wheel - (s_wheel > 0 ? decay : -decay);
  steer = clampf(steer + s_wheel, -1, 1);
  if (touch.pressed() && s_mode == Mode::Race) {
    int16_t lx, ly;
    s_gfx->touchToLandscape(touch.x(), touch.y(), lx, ly);
    if (lx < W / 3) steer = -1;
    else if (lx > W * 2 / 3) steer = 1;
    else if (touch.justPressed()) s_boostTap = true;
  }
  bool brake = input.held(Key::Down);

  acc += dt;
  while (acc >= STEP) {
    update(STEP, steer, brake);
    acc -= STEP;
  }

  RunAudio::Params ap;
  ap.speed = P.speed / MAXV;
  ap.rpm = P.rpm;
  ap.ice = !s_ev;
  ap.throttle = P.throttle;
  ap.boosting = P.boosting > 0;
  ap.braking = P.braking;
  ap.paused = s_mode != Mode::Race; // the picker's demo race stays silent
  RunAudio::set(ap);

  render();

  static uint32_t frames = 0, fpsAt = 0;
  frames++;
  if (now - fpsAt >= 5000) {
    if (fpsAt && frames)
      Serial.printf("Lullaby Run: %.1f fps  world %lu  fx %lu  hud %lu  present %lu us\n",
                    frames * 1000.0f / (now - fpsAt), s_us[0] / frames, s_us[1] / frames, s_us[2] / frames,
                    s_us[3] / frames);
    memset(s_us, 0, sizeof(s_us));
    fpsAt = now;
    frames = 0;
  }
}

} // namespace LullabyRun
