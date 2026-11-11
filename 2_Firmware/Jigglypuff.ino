/*
 * Jigglypuff - a pocket music console for the T-Display-S3-AMOLED-1.64,
 * used in landscape (456x280 logical; see Landscape.h for why rotation is
 * done in software) with a "liquid glass" look (see Glass.h).
 *
 *   Home        three large glass tiles: Music, Games, Add songs
 *   Music       Artists / Albums / Playlists lists (see MusicBrowser.h)
 *   Now playing the player for the queued songs
 *   Games       two tiles: Lullaby Run, an 11-district racer (LullabyRun.h),
 *               Cypress Night, a garden maze chase (CypressNight.h), and
 *               Late for Tea, a 3D Wonderland dash (LateForTea.h)
 *   Add songs   web page on the home WiFi that uploads straight to the card
 *
 * Controls (ANO wheel):
 *   wheel / LEFT / RIGHT  move between tiles; in the Music lists, wheel =
 *                         scroll and LEFT/RIGHT = page; in Now playing,
 *                         wheel = volume, tap LEFT/RIGHT = previous/next
 *                         song, hold LEFT/RIGHT = rewind/fast-forward
 *   SELECT                open / play-pause
 *   DOWN                  on a song or in Now playing: add to playlist
 *   UP                    back one step
 *   hold DOWN ~0.8s       screen off right there (music keeps playing);
 *                         turn the wheel ~45 degrees to wake it on the same spot
 *   hold DOWN + wheel     screen brightness (never locks)
 *   hold SELECT ~1.2s     switch off; hold ~2s while off to switch on
 *   hold SELECT ~1s       in a game: quit to Home
 *
 * Files: this folder holds the main program and the inputs/battery; the
 * music player, game, web uploader and theme live in the sibling folders
 * 3_Music_App .. 6_Theme_Graphics (see platformio.ini and README.md).
 *
 * Battery: WiFi is powered only while Add Songs is open, and the clock
 * keeps the time it last synced there.
 * Touch works too: tap a tile, the player buttons, or "< Home".
 */

#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <Preferences.h>

#include "Input.h"
#include "TouchInput.h"
#include "Landscape.h"
#include "Glass.h"
#include "BatteryMonitor.h"
#include "TimeSync.h"
#include "Pcm5122.h"
#include "MusicPlayer.h"
#include "Library.h"
#include "Playlists.h"
#include "MusicBrowser.h"
#include "SongUpload.h"
#include "Organizer.h"
#include "LullabyRun.h"
#include "CypressNight.h"
#include "LateForTea.h"
#include "EmeraldCity.h"
#include "CatNap.h"
#include "Ariadne.h"
#include "Lamplighter.h"
#include "Crane.h"
#include "WifiConfig.h"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_GFX *panel = new Arduino_CO5300(bus, LCD_RST, 0, false, LCD_WIDTH, LCD_HEIGHT,
                                         20, 0, 0, 0);

Arduino_Canvas *canvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);
LandscapeGFX *gfx;

Input input;
TouchInput touch;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class Screen { Home, Music, NowPlaying, Games, AddSongs };
Screen screen = Screen::Home;

int homeSel = 0;           // focused tile on Home
int gamesSel = 0;          // focused tile in Games
bool gameFailed = false;   // the last game didn't have the memory to start

// The game running inside the Games screen, if any.
enum class Game : uint8_t { None, Run, Cypress, Tea, Oz, Cat, Thread, Lamp, Fly };
Game game = Game::None;
bool musicActive = false;  // the user wants music playing (drives auto-advance)
bool needsRedraw = true;
uint32_t volumeShownUntil = 0;

// Holding LEFT/RIGHT in Now playing scrubs through the song: the bar moves
// while held and the jump happens on release.
bool seeking = false;
int32_t seekTarget = 0; // seconds

// Screen brightness (hold DOWN and turn the wheel), kept across reboots.
Preferences prefs;
int brightness = 200;
uint32_t brightnessShownUntil = 0;
static const int BRIGHTNESS_MIN = 10; // never fully black - that's what sleep is for
static const int BRIGHTNESS_STEP = 15;

static const int16_t SCREEN_W = 456;
static const int16_t SCREEN_H = 280;

// Palette (filled in setup once Glass can build RGB565 values).
uint16_t C_TEXT, C_DIM, C_FAINT, C_PINK, C_GREEN, C_BLUE, C_RED, C_NEUTRAL;

// ---------------------------------------------------------------------------
// Text helpers - all text is drawn transparent over the glass
// ---------------------------------------------------------------------------

void text(int16_t x, int16_t y, const char *s, uint8_t size, uint16_t color) {
  gfx->drawText(x, y, s, size, color, Glass::CLEAR);
}

void textCenter(int16_t cx, int16_t y, const char *s, uint8_t size, uint16_t color) {
  text((int16_t)(cx - gfx->textWidth(s, size) / 2), y, s, size, color);
}

void textRight(int16_t rx, int16_t y, const char *s, uint8_t size, uint16_t color) {
  text((int16_t)(rx - gfx->textWidth(s, size)), y, s, size, color);
}

// Track title from its path: no leading slash, no extension, clipped to
// maxChars with "..." so it fits the panel.
void trackTitle(const char *path, char *out, size_t outLen, size_t maxChars) {
  const char *name = (path[0] == '/') ? path + 1 : path;
  size_t n = strlen(name);
  const char *dot = strrchr(name, '.');
  if (dot && dot != name) n = (size_t)(dot - name);
  if (n + 1 > outLen) n = outLen - 1;

  if (n > maxChars && maxChars > 3) {
    size_t keep = maxChars - 3;
    memcpy(out, name, keep);
    memcpy(out + keep, "...", 4);
  } else {
    memcpy(out, name, n);
    out[n] = '\0';
  }
}

// Copies s, cut to maxChars with "..." when it's longer.
void clipText(const char *s, char *out, size_t outLen, size_t maxChars) {
  size_t n = strlen(s);
  if (n > maxChars && maxChars > 3) {
    size_t keep = maxChars - 3;
    if (keep + 4 > outLen) keep = outLen - 4;
    memcpy(out, s, keep);
    memcpy(out + keep, "...", 4);
  } else {
    strlcpy(out, s, outLen);
  }
}

void formatTime(uint32_t sec, char *out, size_t len) {
  snprintf(out, len, "%lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

// ---------------------------------------------------------------------------
// Icons (drawn in solid white over the glass)
// ---------------------------------------------------------------------------

void iconMusic(int16_t cx, int16_t cy, int16_t s, uint16_t c) {
  // Two beamed eighth notes; s scales the whole glyph (10 = default size).
  auto S = [s](int v) { return (int16_t)(v * s / 10); };
  gfx->fillCircle(cx - S(16), cy + S(16), S(10), c);
  gfx->fillCircle(cx + S(16), cy + S(10), S(10), c);
  gfx->fillRect(cx - S(9), cy - S(22), S(5), S(38), c);
  gfx->fillRect(cx + S(23), cy - S(28), S(5), S(38), c);
  gfx->fillTriangle(cx - S(9), cy - S(22), cx + S(28), cy - S(28), cx + S(28), cy - S(19), c);
  gfx->fillTriangle(cx - S(9), cy - S(22), cx + S(28), cy - S(19), cx - S(9), cy - S(13), c);
}

void iconGames(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  gfx->fillRoundRect(cx - 38, cy - 20, 76, 40, 18, c);
  // D-pad and face buttons cut out of the body.
  gfx->fillRect(cx - 27, cy - 3, 18, 6, cut);
  gfx->fillRect(cx - 21, cy - 9, 6, 18, cut);
  gfx->fillCircle(cx + 15, cy - 5, 4, cut);
  gfx->fillCircle(cx + 24, cy + 4, 4, cut);
}

void iconAdd(int16_t cx, int16_t cy, uint16_t c) {
  // Arrow dropping into a tray.
  gfx->fillRect(cx - 3, cy - 28, 7, 28, c);
  gfx->fillTriangle(cx - 15, cy - 2, cx + 16, cy - 2, cx, cy + 14, c);
  gfx->fillRect(cx - 28, cy + 20, 57, 6, c);
  gfx->fillRect(cx - 28, cy + 6, 6, 20, c);
  gfx->fillRect(cx + 23, cy + 6, 6, 20, c);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void drawStatusBar(const char *back) {
  // Left: "< Home" (or "< Music") on sub-screens. (No WiFi indicator - WiFi
  // is off everywhere except Add Songs, which shows its own status.)
  if (back) text(24, 12, back, 2, C_DIM);

  // Centre: clock, or the volume/brightness while it's being changed.
  if (millis() < brightnessShownUntil) {
    char b[20];
    snprintf(b, sizeof(b), "Brightness %d%%", (brightness * 100 + 127) / 255);
    Glass::panel(128, 6, 200, 28, 14, C_BLUE, 90, 60);
    textCenter(SCREEN_W / 2, 12, b, 2, C_TEXT);
  } else if (millis() < volumeShownUntil) {
    char v[16];
    snprintf(v, sizeof(v), "Volume %u", (unsigned)MusicPlayer::volume());
    Glass::panel(158, 6, 140, 28, 14, C_PINK, 90, 60);
    textCenter(SCREEN_W / 2, 12, v, 2, C_TEXT);
  } else {
    char clock[8];
    if (!TimeSync::clockString(clock, sizeof(clock))) {
      uint32_t m = millis() / 60000;
      snprintf(clock, sizeof(clock), "%lu:%02lu", (unsigned long)(m / 60), (unsigned long)(m % 60));
    }
    textCenter(SCREEN_W / 2, 12, clock, 2, C_TEXT);
  }

  // Right: battery percentage and gauge. Green while charging.
  int pct = BatteryMonitor::percent();
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  bool charging = BatteryMonitor::onUsb();
  const int16_t bx = SCREEN_W - 24 - 32, by = 12, bw = 28, bh = 15;
  gfx->drawRoundRect(bx, by, bw, bh, 4, C_DIM);
  gfx->fillRect(bx + bw, by + 4, 3, bh - 8, C_DIM);
  int16_t fill = (int16_t)((bw - 4) * pct / 100);
  uint16_t fc = charging ? C_GREEN : (pct <= 15 ? C_RED : C_TEXT);
  if (fill > 0) gfx->fillRect(bx + 2, by + 2, fill, bh - 4, fc);

  char p[8];
  snprintf(p, sizeof(p), "%d%%", pct);
  textRight(bx - 8, 12, p, 2, charging ? C_GREEN : C_TEXT);
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

static const int16_t TILE_W = 120, TILE_H = 150, TILE_GAP = 20;
static const int16_t TILE_Y = 62;

int16_t tileX(int i) { return (int16_t)((SCREEN_W - (3 * TILE_W + 2 * TILE_GAP)) / 2 + i * (TILE_W + TILE_GAP)); }

void drawHome() {
  Glass::background();
  drawStatusBar(nullptr);

  const char *labels[3] = {"Music", "Games", "Add songs"};
  const uint16_t tints[3] = {C_PINK, C_GREEN, C_BLUE};

  for (int i = 0; i < 3; i++) {
    bool sel = (i == homeSel);
    int16_t x = tileX(i), y = TILE_Y, w = TILE_W, h = TILE_H;
    // The focused tile grows slightly and glows; the others stay quiet.
    if (sel) { x -= 6; y -= 6; w += 12; h += 12; }
    Glass::panel(x, y, w, h, 28, tints[i], sel ? 150 : 70, sel ? 140 : 0);

    int16_t cx = x + w / 2, cy = y + 62;
    if (i == 0) iconMusic(cx, cy, sel ? 12 : 10, C_TEXT);
    else if (i == 1) iconGames(cx, cy, C_TEXT, Glass::blend(tints[i], 0x0000, 90));
    else iconAdd(cx, cy, C_TEXT);

    textCenter(cx, y + h - 36, labels[i], 2, sel ? C_TEXT : C_DIM);
  }

  // Now-playing pill, so music carries on visibly while browsing.
  if (musicActive && MusicPlayer::queueLength() > 0) {
    char title[32], line[40];
    clipText(Library::track(MusicPlayer::currentTrack()).title, title, sizeof(title), 22);
    snprintf(line, sizeof(line), "%s %s", MusicPlayer::isPaused() ? "||" : ">", title);
    int16_t w = gfx->textWidth(line, 2) + 40;
    Glass::panel((SCREEN_W - w) / 2, 232, w, 32, 16, C_PINK, 70, 30);
    textCenter(SCREEN_W / 2, 240, line, 2, C_TEXT);
  }
}

// Player button geometry, shared by drawing and touch hit-testing.
static const int16_t BTN_Y = 220;
static const int16_t BTN_PREV_X = 262, BTN_PLAY_X = 318, BTN_NEXT_X = 374;

// The Music lists (Artists / Albums / Playlists and their songs).
void drawLibrary() {
  Glass::background();
  drawStatusBar("< Home");
  bool empty = !MusicPlayer::sdReady() || Library::count() == 0;
  if (empty || !MusicBrowser::atTop()) Glass::panel(14, 38, 428, 236, 26, C_NEUTRAL, 30, 40);

  if (empty) {
    iconMusic(SCREEN_W / 2, 110, 12, C_DIM);
    textCenter(SCREEN_W / 2, 170, MusicPlayer::sdReady() ? "No songs yet" : "No SD card", 2, C_TEXT);
    textCenter(SCREEN_W / 2, 196,
               MusicPlayer::sdReady() ? "Use Add songs to put music on" : "Check the card is pushed in",
               1, C_DIM);
    return;
  }
  MusicBrowser::draw();
}

void drawNowPlaying() {
  Glass::background();
  drawStatusBar("< Music");
  Glass::panel(24, 40, 408, 212, 28, C_NEUTRAL, 30, 40);

  if (MusicPlayer::queueLength() == 0) {
    iconMusic(SCREEN_W / 2, 110, 12, C_DIM);
    textCenter(SCREEN_W / 2, 170, "Nothing playing", 2, C_TEXT);
    textCenter(SCREEN_W / 2, 196, "Pick a song from Artists, Albums or Playlists", 1, C_DIM);
    return;
  }
  const Track &t = Library::track(MusicPlayer::currentTrack());

  // Artwork tile.
  Glass::panel(44, 60, 136, 136, 24, C_PINK, 160, 80);
  iconMusic(112, 126, 13, C_TEXT);

  // Title over up to two lines.
  char title[48];
  clipText(t.title, title, sizeof(title), 34);
  const size_t perLine = 18;
  if (strlen(title) > perLine) {
    char l1[20], l2[20];
    size_t cut = perLine;
    for (size_t i = perLine; i > 8; i--) {
      if (title[i] == ' ') { cut = i; break; }
    }
    memcpy(l1, title, cut);
    l1[cut] = '\0';
    const char *rest = title + cut + (title[cut] == ' ' ? 1 : 0);
    snprintf(l2, sizeof(l2), "%.18s", rest);
    text(200, 58, l1, 2, C_TEXT);
    text(200, 78, l2, 2, C_TEXT);
  } else {
    text(200, 66, title, 2, C_TEXT);
  }

  char artist[24], line[48], album[28];
  clipText(t.artist, artist, sizeof(artist), 17);
  text(200, 102, artist, 2, C_DIM);
  clipText(t.album, album, sizeof(album), 22);
  snprintf(line, sizeof(line), "%s  -  %u of %u", album, (unsigned)(MusicPlayer::queuePos() + 1),
           (unsigned)MusicPlayer::queueLength());
  text(200, 126, line, 1, C_DIM);

  // Progress (follows the scrub position while LEFT/RIGHT is held).
  uint32_t cur = seeking ? (uint32_t)seekTarget : MusicPlayer::currentTimeSec();
  uint32_t dur = MusicPlayer::durationSec();
  const int16_t px = 200, pw = 212, py = 150;
  gfx->fillRoundRect(px, py, pw, 6, 3, Glass::blend(C_FAINT, 0x0000, 60));
  if (dur > 0) {
    if (cur > dur) cur = dur;
    int16_t f = (int16_t)((uint32_t)pw * cur / dur);
    if (f > 0) gfx->fillRoundRect(px, py, f, 6, 3, C_PINK);
  }
  char a[12], b[12];
  formatTime(cur, a, sizeof(a));
  if (dur > 0) {
    char rem[10];
    formatTime(dur - cur, rem, sizeof(rem));
    snprintf(b, sizeof(b), "-%s", rem);
  } else {
    snprintf(b, sizeof(b), "--:--");
  }
  text(px, py + 14, a, 2, C_DIM);
  textRight(px + pw, py + 14, b, 2, C_DIM);

  // Transport buttons.
  bool playing = musicActive && !MusicPlayer::isPaused();
  Glass::disc(BTN_PREV_X, BTN_Y, 20, C_NEUTRAL, 40, 20);
  Glass::disc(BTN_PLAY_X, BTN_Y, 25, C_PINK, 150, 120);
  Glass::disc(BTN_NEXT_X, BTN_Y, 20, C_NEUTRAL, 40, 20);

  // prev: |<
  gfx->fillRect(BTN_PREV_X - 9, BTN_Y - 8, 3, 16, C_TEXT);
  gfx->fillTriangle(BTN_PREV_X + 8, BTN_Y - 8, BTN_PREV_X + 8, BTN_Y + 8, BTN_PREV_X - 5, BTN_Y, C_TEXT);
  // next: >|
  gfx->fillRect(BTN_NEXT_X + 7, BTN_Y - 8, 3, 16, C_TEXT);
  gfx->fillTriangle(BTN_NEXT_X - 8, BTN_Y - 8, BTN_NEXT_X - 8, BTN_Y + 8, BTN_NEXT_X + 5, BTN_Y, C_TEXT);
  // play / pause
  if (playing) {
    gfx->fillRect(BTN_PLAY_X - 8, BTN_Y - 10, 6, 20, C_TEXT);
    gfx->fillRect(BTN_PLAY_X + 3, BTN_Y - 10, 6, 20, C_TEXT);
  } else {
    gfx->fillTriangle(BTN_PLAY_X - 6, BTN_Y - 11, BTN_PLAY_X - 6, BTN_Y + 11, BTN_PLAY_X + 11, BTN_Y, C_TEXT);
  }

  // The DAC is deliberately unpowered while paused on battery, so only
  // complain if it's missing when sound is supposed to be coming out.
  if (musicWantsPower() && !Pcm5122::isResponding()) {
    text(44, 214, "Speaker offline", 1, C_RED);
  }

  textCenter(SCREEN_W / 2, 262, "SELECT play  </> skip (hold: seek)  DOWN add to playlist  WHEEL vol", 1, C_DIM);
}

// Games menu: one tile per game.
// Games menu: a row of tiles, three on screen, scrolling to keep the
// focused one in view.
static const int GAMES = 8, GAMES_SHOWN = 3;
static const int16_t GTILE_Y = 54, GTILE_W = 132, GTILE_H = 188, GTILE_GAP = 10;
int gamesFirst() { return constrain(gamesSel - 1, 0, GAMES - GAMES_SHOWN); }
int16_t gameSlotX(int slot) { return (int16_t)((SCREEN_W - (GAMES_SHOWN * GTILE_W + (GAMES_SHOWN - 1) * GTILE_GAP)) / 2 + slot * (GTILE_W + GTILE_GAP)); }

void iconTea(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  gfx->fillRoundRect(cx - 14, cy - 34, 9, 28, 4, c);    // bunny ears
  gfx->fillRoundRect(cx + 5, cy - 34, 9, 28, 4, c);
  gfx->fillRoundRect(cx - 11, cy - 30, 3, 20, 1, cut);
  gfx->fillRoundRect(cx + 8, cy - 30, 3, 20, 1, cut);
  gfx->fillRoundRect(cx - 24, cy - 4, 42, 28, 10, c);   // a teacup
  gfx->fillCircle(cx + 22, cy + 8, 9, c);
  gfx->fillCircle(cx + 22, cy + 8, 4, cut);
  gfx->fillRoundRect(cx - 32, cy + 24, 58, 6, 3, c);    // saucer
}

void iconCar(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  gfx->fillRoundRect(cx - 34, cy - 4, 68, 20, 8, c);   // body
  gfx->fillRoundRect(cx - 18, cy - 18, 36, 18, 8, c);  // cabin
  gfx->fillRoundRect(cx - 13, cy - 14, 26, 9, 3, cut); // window
  gfx->fillCircle(cx - 20, cy + 16, 8, c);
  gfx->fillCircle(cx + 20, cy + 16, 8, c);
  gfx->fillCircle(cx - 20, cy + 16, 3, cut);
  gfx->fillCircle(cx + 20, cy + 16, 3, cut);
}

void iconCrane(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  gfx->fillRect(cx - 30, cy - 32, 5, 64, c);              // mast
  gfx->fillRect(cx - 34, cy - 32, 66, 5, c);              // jib
  gfx->fillTriangle(cx - 32, cy - 32, cx - 23, cy - 32, cx - 28, cy - 42, c);
  gfx->drawLine(cx + 14, cy - 27, cx + 14, cy - 10, c);   // the rope
  gfx->fillRoundRect(cx + 2, cy - 10, 24, 11, 2, c);      // a floor on the hook
  gfx->fillRect(cx + 7, cy - 7, 4, 4, cut);
  gfx->fillRect(cx + 17, cy - 7, 4, 4, cut);
  gfx->fillRoundRect(cx - 12, cy + 6, 26, 12, 2, c);      // the tower
  gfx->fillRoundRect(cx - 14, cy + 20, 30, 12, 2, c);
  gfx->fillRect(cx - 7, cy + 9, 4, 5, cut);
  gfx->fillRect(cx + 5, cy + 9, 4, 5, cut);
  gfx->fillRect(cx - 2, cy + 23, 5, 9, cut);
}

void iconCat(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  gfx->fillRoundRect(cx - 30, cy + 4, 60, 22, 10, c);       // a cushion
  gfx->fillCircle(cx, cy + 15, 2, cut);
  gfx->fillCircle(cx - 4, cy - 4, 16, c);                   // a cat's head peeking out
  gfx->fillTriangle(cx - 19, cy - 8, cx - 13, cy - 30, cx - 5, cy - 16, c);
  gfx->fillTriangle(cx + 11, cy - 8, cx + 5, cy - 30, cx - 3, cy - 16, c);
  gfx->drawLine(cx - 12, cy - 4, cx - 7, cy - 3, cut);      // sleepy eyes
  gfx->drawLine(cx - 1, cy - 3, cx + 4, cy - 4, cut);
  gfx->fillCircle(cx - 4, cy + 2, 2, cut);
  gfx->fillCircle(cx + 24, cy - 22, 5, c);                  // a ball of yarn
}

void iconLabyrinth(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  for (int r = 30; r >= 10; r -= 8) {                         // rings of the labyrinth
    gfx->fillCircle(cx, cy, r, c);
    gfx->fillCircle(cx, cy, r - 3, cut);
  }
  gfx->fillRect(cx - 3, cy + 6, 6, 26, cut);                   // the way in
  gfx->fillCircle(cx, cy, 4, c);
  gfx->fillCircle(cx + 26, cy + 24, 7, c);                     // the ball of thread
  gfx->drawLine(cx + 26, cy + 24, cx, cy + 34, c);
}

void iconOrbit(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  for (int a = 0; a < 360; a += 30) {                        // the gravity field, dashed
    float r0 = a * DEG_TO_RAD, r1 = (a + 16) * DEG_TO_RAD;
    gfx->drawLine(cx + cosf(r0) * 32, cy + sinf(r0) * 32, cx + cosf(r1) * 32, cy + sinf(r1) * 32, c);
  }
  gfx->fillCircle(cx, cy, 13, c);                              // a world with its lamp
  gfx->fillCircle(cx + 4, cy + 4, 4, cut);
  gfx->fillRect(cx - 1, cy - 19, 3, 7, c);
  gfx->fillCircle(cx, cy - 21, 3, c);
  gfx->drawLine(cx, cy, cx + 22, cy - 22, c);                  // the tether
  gfx->fillTriangle(cx + 30, cy - 28, cx + 16, cy - 26, cx + 26, cy - 16, c); // the Moth
}

void iconOrigami(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  gfx->fillTriangle(cx - 4, cy + 2, cx + 6, cy + 2, cx + 1, cy - 8, c);      // the body
  gfx->fillTriangle(cx - 30, cy - 18, cx - 2, cy + 2, cx - 6, cy - 4, c);    // the wings
  gfx->fillTriangle(cx + 30, cy - 22, cx + 4, cy + 2, cx + 8, cy - 5, c);
  gfx->fillTriangle(cx - 6, cy - 4, cx - 2, cy + 2, cx - 30, cy - 18, cut);
  gfx->drawLine(cx + 4, cy, cx + 20, cy + 14, c);                           // the neck
  gfx->fillTriangle(cx + 18, cy + 12, cx + 24, cy + 13, cx + 20, cy + 17, c);
  gfx->drawLine(cx - 2, cy, cx - 18, cy + 12, c);                           // the tail
  gfx->fillTriangle(cx - 24, cy + 30, cx + 24, cy + 30, cx, cy + 20, c);    // a far island
  gfx->fillRect(cx + 14, cy + 18, 2, 10, cut);                              // and its torii
  gfx->fillRect(cx + 20, cy + 18, 2, 10, cut);
  gfx->fillRect(cx + 12, cy + 17, 12, 2, cut);
}

void iconCypress(int16_t cx, int16_t cy, uint16_t c) {
  gfx->fillCircle(cx + 24, cy - 22, 7, c);              // the moon
  gfx->fillTriangle(cx - 6, cy + 18, cx + 6, cy + 18, cx, cy - 30, c); // cypress
  gfx->fillCircle(cx, cy + 8, 8, c);
  gfx->fillRect(cx - 2, cy + 18, 4, 8, c);
  gfx->fillTriangle(cx - 30, cy + 22, cx - 20, cy + 22, cx - 25, cy - 6, c); // a smaller one
}

// Splits text for a game tile at the larger type size: one line if it fits,
// else two, broken at the space that best balances them. Returns the count.
int splitLine(const char *s, char *a, char *b) {
  int n = strlen(s), cut = -1;
  if (gfx->textWidth(s, 2) <= GTILE_W - 6) { snprintf(a, 24, "%s", s); return 1; }
  for (int k = 1; k < n; k++)
    if (s[k] == ' ' && (cut < 0 || abs(n - 2 * k) < abs(n - 2 * cut))) cut = k;
  if (cut < 0) { snprintf(a, 24, "%s", s); return 1; } // one long word: it still fits the tile
  snprintf(a, 24, "%.*s", cut, s);
  snprintf(b, 24, "%s", s + cut + 1);
  return 2;
}

void drawGames() {
  Glass::background();
  drawStatusBar("< Home");
  static const char *names[GAMES] = {"Lullaby Run", "Cypress Night", "Late for Tea", "Emerald City", "Cat Nap", "Ariadne", "Lamplighter", "White Heron"};
  static const char *subs[GAMES] = {"City night racer", "Owls in the garden", "Wonderland dash", "Build Oz", "Don't wake them", "The labyrinth", "Light the stars", "Fly the islands"};
  const uint16_t tints[GAMES] = {C_BLUE, C_GREEN, C_PINK, Glass::rgb(40, 190, 120), Glass::rgb(190, 110, 230), Glass::rgb(220, 70, 50), Glass::rgb(90, 90, 220), Glass::rgb(230, 120, 90)};
  int first = gamesFirst();
  for (int slot = 0; slot < GAMES_SHOWN; slot++) {
    int i = first + slot;
    bool sel = i == gamesSel;
    int16_t x = gameSlotX(slot), y = GTILE_Y, w = GTILE_W, h = GTILE_H;
    if (sel) { x -= 5; y -= 5; w += 10; h += 10; }
    Glass::panel(x, y, w, h, 24, tints[i], sel ? 150 : 70, sel ? 140 : 0);
    int16_t cx = x + w / 2;
    uint16_t cut = Glass::blend(tints[i], 0x0000, 90);
    int16_t iy = y + 58;
    if (i == 0) iconCar(cx, iy, C_TEXT, cut);
    else if (i == 1) iconCypress(cx, iy, C_TEXT);
    else if (i == 2) iconTea(cx, iy, C_TEXT, cut);
    else if (i == 3) iconCrane(cx, iy, C_TEXT, cut);
    else if (i == 4) iconCat(cx, iy, C_TEXT, cut);
    else if (i == 5) iconLabyrinth(cx, iy, C_TEXT, cut);
    else if (i == 6) iconOrbit(cx, iy, C_TEXT, cut);
    else iconOrigami(cx, iy, C_TEXT, cut);
    // Name and tagline, both in the big type, each split over two lines
    // when it won't fit on one; stacked up from the tile's foot.
    char nameA[24], nameB[24], subA[24], subB[24];
    int nl = splitLine(names[i], nameA, nameB), sl = splitLine(subs[i], subA, subB);
    const int16_t LH = 18;
    int16_t subTop = y + h - 12 - sl * LH, nameTop = subTop - 6 - nl * LH;
    uint16_t nameCol = sel ? C_TEXT : Glass::blend(C_TEXT, C_DIM, 70);
    uint16_t subCol = sel ? Glass::blend(C_TEXT, C_DIM, 90) : C_DIM;
    textCenter(cx, nameTop, nameA, 2, nameCol);
    if (nl == 2) textCenter(cx, nameTop + LH, nameB, 2, nameCol);
    textCenter(cx, subTop, subA, 2, subCol);
    if (sl == 2) textCenter(cx, subTop + LH, subB, 2, subCol);
  }
  // Dots under the row: where the view is among all the games.
  for (int i = 0; i < GAMES; i++) {
    int16_t dx = SCREEN_W / 2 + (i * 2 - (GAMES - 1)) * 7;
    gfx->fillCircle(dx, 256, i == gamesSel ? 3 : 2, i == gamesSel ? C_TEXT : C_FAINT);
  }
  if (first > 0) textCenter(8, GTILE_Y + GTILE_H / 2 - 8, "<", 2, C_DIM);
  if (first + GAMES_SHOWN < GAMES) textCenter(SCREEN_W - 8, GTILE_Y + GTILE_H / 2 - 8, ">", 2, C_DIM);
  if (gameFailed) textCenter(SCREEN_W / 2, 266, "Not enough memory for that game", 1, C_RED);
}

void drawAddSongs() {
  Glass::background();
  drawStatusBar("< Home");
  Glass::panel(24, 40, 408, 212, 28, C_BLUE, 60, 40);

  iconAdd(84, 100, C_TEXT);
  text(132, 66, "Add songs", 2, C_TEXT);

  if (strlen(WIFI_SSID) == 0) {
    text(132, 96, "WiFi isn't set up", 2, C_DIM);
    return;
  }
  if (!TimeSync::wifiConnected()) {
    text(132, 96, "Connecting to WiFi...", 2, C_DIM);
    return;
  }

  text(132, 96, "On your phone, open", 2, C_DIM);
  text(132, 122, "jigglypuff.local", 2, C_TEXT);
  char ip[40];
  snprintf(ip, sizeof(ip), "or http://%s", WiFi.localIP().toString().c_str());
  text(132, 146, ip, 2, C_DIM);

  // Status line along the bottom of the card.
  Glass::panel(44, 192, 368, 40, 20, C_NEUTRAL, 30, 20);
  char status[64];
  uint16_t sc = C_TEXT;
  if (SongUpload::busy()) {
    char name[24];
    trackTitle(SongUpload::currentName(), name, sizeof(name), 16);
    snprintf(status, sizeof(status), "Receiving %s  %.1f MB", name,
             SongUpload::bytesReceived() / 1048576.0f);
  } else if (*SongUpload::lastError()) {
    snprintf(status, sizeof(status), "%s", SongUpload::lastError());
    sc = C_RED;
  } else if (SongUpload::filesAdded() > 0) {
    snprintf(status, sizeof(status), "Added %lu song%s", (unsigned long)SongUpload::filesAdded(),
             SongUpload::filesAdded() == 1 ? "" : "s");
    sc = C_GREEN;
  } else {
    snprintf(status, sizeof(status), "Waiting for songs...");
    sc = C_DIM;
  }
  textCenter(SCREEN_W / 2, 204, status, 2, sc);
}

void render() {
  Glass::useHomeTheme(screen == Screen::Home);
  switch (screen) {
    case Screen::Home: drawHome(); break;
    case Screen::Music: drawLibrary(); break;
    case Screen::NowPlaying: drawNowPlaying(); break;
    case Screen::Games: drawGames(); break;
    case Screen::AddSongs: drawAddSongs(); break;
  }
  MusicBrowser::drawPicker(); // "Add to playlist", when open
  MusicBrowser::drawToast();
  gfx->flush();
  needsRedraw = false;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Audio power. The DAC runs from VBUS, which on battery only exists while
// the charger's 5V boost is on - and the boost is kept off whenever music
// isn't playing, both to save battery and because the charger can't see
// USB (so can't charge) while boosting. The DAC forgets its configuration
// every time its power drops, so it is set up again whenever it comes back.
// ---------------------------------------------------------------------------

bool musicWantsPower() {
  return (musicActive && !MusicPlayer::isPaused()) || LullabyRun::wantsAudioPower() ||
         CypressNight::wantsAudioPower() || LateForTea::wantsAudioPower() || EmeraldCity::wantsAudioPower() ||
         CatNap::wantsAudioPower() || Ariadne::wantsAudioPower() || Lamplighter::wantsAudioPower() ||
         Crane::wantsAudioPower();
}

// --- whichever game is running ------------------------------------------------

bool gameActive() {
  if (screen != Screen::Games) return false;
  return (game == Game::Run && LullabyRun::active()) || (game == Game::Cypress && CypressNight::active()) ||
         (game == Game::Tea && LateForTea::active()) || (game == Game::Oz && EmeraldCity::active()) ||
         (game == Game::Cat && CatNap::active()) || (game == Game::Thread && Ariadne::active()) ||
         (game == Game::Lamp && Lamplighter::active()) || (game == Game::Fly && Crane::active());
}

void gameExit() {
  if (game == Game::Run) LullabyRun::exit();
  else if (game == Game::Cypress) CypressNight::exit();
  else if (game == Game::Tea) LateForTea::exit();
  else if (game == Game::Oz) EmeraldCity::exit();
  else if (game == Game::Cat) CatNap::exit();
  else if (game == Game::Thread) Ariadne::exit();
  else if (game == Game::Lamp) Lamplighter::exit();
  else if (game == Game::Fly) Crane::exit();
  game = Game::None;
}

void gameKey(const InputEvent &ev) {
  if (game == Game::Run) LullabyRun::key(ev);
  else if (game == Game::Cypress) CypressNight::key(ev);
  else if (game == Game::Tea) LateForTea::key(ev);
  else if (game == Game::Oz) EmeraldCity::key(ev);
  else if (game == Game::Cat) CatNap::key(ev);
  else if (game == Game::Thread) Ariadne::key(ev);
  else if (game == Game::Lamp) Lamplighter::key(ev);
  else if (game == Game::Fly) Crane::key(ev);
}

void gameFrame(int32_t wheel) {
  if (game == Game::Run) LullabyRun::frame(input, touch, wheel);
  else if (game == Game::Cypress) CypressNight::frame(input, touch, wheel);
  else if (game == Game::Tea) LateForTea::frame(input, touch, wheel);
  else if (game == Game::Oz) EmeraldCity::frame(input, touch, wheel);
  else if (game == Game::Cat) CatNap::frame(input, touch, wheel);
  else if (game == Game::Thread) Ariadne::frame(input, touch, wheel);
  else if (game == Game::Lamp) Lamplighter::frame(input, touch, wheel);
  else if (game == Game::Fly) Crane::frame(input, touch, wheel);
}

void gamePause() {
  if (game == Game::Run) LullabyRun::pause();
  else if (game == Game::Cypress) CypressNight::pause();
  else if (game == Game::Tea) LateForTea::pause();
  else if (game == Game::Oz) EmeraldCity::pause();
  else if (game == Game::Cat) CatNap::pause();
  else if (game == Game::Thread) Ariadne::pause();
  else if (game == Game::Lamp) Lamplighter::pause();
  else if (game == Game::Fly) Crane::pause();
}

bool gameWantsExit() {
  return (game == Game::Run && LullabyRun::wantsExit()) || (game == Game::Cypress && CypressNight::wantsExit()) ||
         (game == Game::Tea && LateForTea::wantsExit()) || (game == Game::Oz && EmeraldCity::wantsExit()) ||
         (game == Game::Cat && CatNap::wantsExit()) || (game == Game::Thread && Ariadne::wantsExit()) ||
         (game == Game::Lamp && Lamplighter::wantsExit()) || (game == Game::Fly && Crane::wantsExit());
}

void recoverCodec() {
  static bool lost = false;
  if (!Pcm5122::isResponding()) {
    lost = true;
  } else if (lost) {
    lost = false;
    Pcm5122::begin();
    Serial.println("PCM5122 powered - reconfigured");
  }
}

// Brings the DAC up right away when playback starts or resumes, rather than
// waiting for the next periodic check (which would clip the first moments).
void powerAudioNow() {
  BatteryMonitor::setBoostWanted(musicWantsPower());
  bool wasOn = BatteryMonitor::boostOn();
  BatteryMonitor::managePowerPath();
  if (BatteryMonitor::boostOn() && !wasOn) {
    delay(40); // let VBUS settle before talking to the DAC
    Pcm5122::begin();
  }
}

// While boosting, a plugged-in wall charger is invisible (a computer is
// spotted automatically). Track changes are the one moment a short power
// blip goes unheard, so check there.
void checkUsbAtTrackChange() {
  if (!BatteryMonitor::boostOn()) return;
  BatteryMonitor::probeForUsb();
  delay(40);
  Pcm5122::begin();
}

void ensureCard() {
  if (!MusicPlayer::sdReady()) MusicPlayer::beginSd();
}

// Shown while the library scan reads tags (only slow the first time, or
// after lots of new songs - everything else comes from the card's cache).
void scanProgress(uint32_t found, uint32_t tagsRead) {
  Glass::background();
  drawStatusBar(nullptr);
  Glass::panel(78, 80, 300, 130, 28, C_PINK, 70, 50);
  iconMusic(SCREEN_W / 2, 120, 8, C_TEXT);
  textCenter(SCREEN_W / 2, 152, "Reading your music", 2, C_TEXT);
  char line[40];
  if (tagsRead > 0) snprintf(line, sizeof(line), "%lu songs  (%lu new)", (unsigned long)found, (unsigned long)tagsRead);
  else snprintf(line, sizeof(line), "%lu songs", (unsigned long)found);
  textCenter(SCREEN_W / 2, 180, line, 2, C_DIM);
  gfx->flush();
}

// Mounts the card and (re)builds the library if it's new or songs changed.
// Rescanning renumbers songs, so the queue and lists start over; it only
// happens when nothing is playing (first open, or after Add songs).
void ensureLibrary() {
  ensureCard();
  if (!MusicPlayer::sdReady()) return;
  if (Library::ready() && !Library::dirty()) return;
  MusicPlayer::stop();
  musicActive = false;
  MusicPlayer::clearQueue();
  scanProgress(0, 0);
  Library::scan(scanProgress);
  Playlists::load();
  MusicBrowser::reset();
}

void startMusic(uint16_t index) {
  if (MusicPlayer::playIndex(index)) musicActive = true;
  seeking = false;
  powerAudioNow();
}

// Starts game i from the Games menu. A game's sound needs the DAC, so the
// music stops while it runs.
void launchGame(int i) {
  MusicPlayer::stop();
  musicActive = false;
  bool ok = i == 0 ? LullabyRun::enter(gfx, canvas, panel)
          : i == 1 ? CypressNight::enter(gfx, panel)
          : i == 2 ? LateForTea::enter(gfx, panel)
          : i == 3 ? EmeraldCity::enter(gfx, panel)
          : i == 4 ? CatNap::enter(gfx, panel) : i == 5 ? Ariadne::enter(gfx, panel) : i == 6 ? Lamplighter::enter(gfx, panel) : Crane::enter(gfx, panel);
  gameFailed = !ok;
  if (ok) {
    game = i == 0 ? Game::Run : i == 1 ? Game::Cypress : i == 2 ? Game::Tea : i == 3 ? Game::Oz : i == 4 ? Game::Cat : i == 5 ? Game::Thread : i == 6 ? Game::Lamp : Game::Fly;
    Pcm5122::setMute(false); // a paused track leaves the DAC muted
    powerAudioNow();
  }
  needsRedraw = true;
}

// Back from a game to the Games menu.
void leaveGame() {
  gameExit();
  needsRedraw = true;
}

void goHome() {
  if (gameActive()) gameExit();
  game = Game::None;
  if (screen == Screen::AddSongs) {
    SongUpload::stop();
    TimeSync::wifiOff(); // WiFi only lives while Add Songs is open
    // Read new songs next time, unless the organizer already rescanned
    // after them. Its edits and playlist changes are already in place.
    if (SongUpload::filesAdded() != Organizer::uploadsAtLastScan()) Library::markDirty();
    MusicBrowser::reset();
  }
  seeking = false;
  screen = Screen::Home;
  needsRedraw = true;
}

void openApp(int i) {
  if (i == 0) {
    screen = Screen::Music;
    ensureLibrary();
  } else if (i == 1) {
    screen = Screen::Games; // the menu; music keeps playing until a game starts
    game = Game::None;
    gameFailed = false;
  } else {
    screen = Screen::AddSongs;
    // The audio task reads the card from the other core; the SD driver
    // can't be shared with uploads writing to it, so music stops here.
    MusicPlayer::stop();
    musicActive = false;
    ensureCard();
    TimeSync::wifiOn();  // the only place WiFi is ever switched on
    SongUpload::start(); // retried from loop() until WiFi is up
  }
  needsRedraw = true;
}

void playPause() {
  if (MusicPlayer::queueLength() == 0) return;
  if (!musicActive) {
    startMusic(MusicPlayer::queuePos());
  } else {
    MusicPlayer::togglePause();
    powerAudioNow(); // boost back on when resuming on battery
  }
  needsRedraw = true;
}

void changeVolume(int delta) {
  int v = (int)MusicPlayer::volume() + delta;
  if (v < 0) v = 0;
  if (v > 21) v = 21;
  MusicPlayer::setVolume((uint8_t)v);
  volumeShownUntil = millis() + 1500;
  brightnessShownUntil = 0;
  needsRedraw = true;
}

void changeBrightness(int delta) {
  brightness += delta * BRIGHTNESS_STEP;
  if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
  if (brightness > 255) brightness = 255;
  panel->Display_Brightness(brightness);
  brightnessShownUntil = millis() + 1500;
  volumeShownUntil = 0;
  needsRedraw = true;
}

void skip(int dir) {
  if (MusicPlayer::queueLength() == 0) return;
  checkUsbAtTrackChange();
  bool ok = dir > 0 ? MusicPlayer::next() : MusicPlayer::previous();
  if (ok) musicActive = true;
  powerAudioNow();
  needsRedraw = true;
}

// Scrubbing: each auto-repeat of a held LEFT/RIGHT (every ~120 ms) moves
// the target 2 s, then 6 s once it's been held for 2 s.
static uint32_t seekKeyDownAt = 0;

void seekStep(int dir) {
  uint32_t dur = MusicPlayer::durationSec();
  if (!musicActive || dur == 0) return;
  if (!seeking) {
    seeking = true;
    seekTarget = (int32_t)MusicPlayer::currentTimeSec();
  }
  int32_t step = (millis() - seekKeyDownAt > 2000) ? 6 : 2;
  seekTarget += dir * step;
  if (seekTarget < 0) seekTarget = 0;
  if (seekTarget > (int32_t)dur - 1) seekTarget = (int32_t)dur - 1;
  needsRedraw = true;
}

void seekFinish() {
  if (!seeking) return;
  seeking = false;
  MusicPlayer::seekTo((uint32_t)seekTarget);
  needsRedraw = true;
}

// Acts on what the Music lists or the picker asked for.
void browserAction(MusicBrowser::Action a) {
  switch (a) {
    case MusicBrowser::Action::None: return;
    case MusicBrowser::Action::Redraw: break;
    case MusicBrowser::Action::Exit: goHome(); return;
    case MusicBrowser::Action::Play:
      checkUsbAtTrackChange();
      startMusic(MusicBrowser::playIndex());
      screen = Screen::NowPlaying;
      break;
    case MusicBrowser::Action::ShowPlayer: screen = Screen::NowPlaying; break;
  }
  needsRedraw = true;
}

// ---------------------------------------------------------------------------
// Power: hold SELECT to switch off, hold again to switch on
// ---------------------------------------------------------------------------

// "Off" is deep sleep, not a power cut: the ANO talks I2C only, with no
// interrupt line to the ESP32, so nothing it does can wake the chip.
// Instead we wake twice a second, read the button, and go straight back to
// sleep unless it is being held.
#define SLEEP_POLL_US (500 * 1000UL)
#define WAKE_HOLD_TICKS 4 // ~2s held to switch on

RTC_DATA_ATTR bool rtcPoweredOff = false;
RTC_DATA_ATTR uint8_t rtcHoldTicks = 0;

static void sleepUntilNextPoll() {
  esp_sleep_enable_timer_wakeup(SLEEP_POLL_US);
  esp_deep_sleep_start();
}

void powerDown() {
  MusicPlayer::stop();
  SongUpload::stop();

  Glass::background();
  Glass::panel(128, 100, 200, 80, 30, C_NEUTRAL, 40, 60);
  textCenter(SCREEN_W / 2, 132, "Goodbye", 2, C_TEXT);
  gfx->flush();
  delay(700);

  panel->Display_Brightness(0);
  digitalWrite(LCD_EN, LOW);
  TimeSync::wifiOff();
  BatteryMonitor::enable5VBoost(false);

  rtcPoweredOff = true;
  rtcHoldTicks = 0;
  Serial.println("powering down - hold SELECT ~2s to switch back on");
  delay(50);
  sleepUntilNextPoll();
}

// Top of setup(): on a timer wake while "off", read the one button and
// either go back to sleep or carry on into a full boot. Never touches the
// display, to keep these wakes short.
static void handleWakeFromOff() {
  if (!rtcPoweredOff) return;
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    rtcPoweredOff = false; // reset or USB counts as switching on
    rtcHoldTicks = 0;
    return;
  }

  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  bool held = input.begin() && input.rawHeld(Key::Select);
  if (held) {
    if (++rtcHoldTicks >= WAKE_HOLD_TICKS) {
      rtcPoweredOff = false;
      rtcHoldTicks = 0;
      return;
    }
  } else {
    rtcHoldTicks = 0;
  }
  sleepUntilNextPoll();
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  handleWakeFromOff();
  delay(200);
  Serial.println("\nJigglypuff starting...");

  pinMode(LCD_EN, OUTPUT);
  digitalWrite(LCD_EN, HIGH);
  delay(100);

  // 40 MHz QSPI: the library's 8 MHz default takes ~75 ms to send a frame.
  if (!canvas->begin(40000000)) Serial.println("Canvas framebuffer allocation failed");
  canvas->fillScreen(BLACK);
  canvas->flush();
  prefs.begin("jiggly", false);
  brightness = constrain(prefs.getInt("bright", 200), BRIGHTNESS_MIN, 255);
  panel->Display_Brightness(brightness);

  gfx = new LandscapeGFX(canvas);
  gfx->setFlipped(true); // board sits the other way up in the case
  if (!gfx->begin()) Serial.println("Text stage allocation failed");
  if (!Glass::begin(gfx, canvas)) Serial.println("Glass background allocation failed");

  C_TEXT = 0xFFFF;
  C_DIM = Glass::rgb(200, 196, 230);
  C_FAINT = Glass::rgb(110, 106, 140);
  C_PINK = Glass::rgb(255, 45, 110);
  C_GREEN = Glass::rgb(48, 209, 120);
  C_BLUE = Glass::rgb(10, 132, 255);
  C_RED = Glass::rgb(255, 90, 100);
  C_NEUTRAL = Glass::rgb(220, 225, 255);

  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  delay(50);

  if (!input.begin()) Serial.println("ANO encoder not found");
  if (!touch.begin()) Serial.println("Touch controller not found - ANO only");
  if (BatteryMonitor::begin()) BatteryMonitor::managePowerPath();
  else Serial.println("SY6970 not found - no battery readout or 5V boost");

  if (Pcm5122::begin()) Serial.printf("PCM5122 ready at 0x%02X\n", Pcm5122::address());
  else Serial.println("PCM5122 not found - check its SDA/SCL wiring");

  TimeSync::begin();

  // The library itself is read the first time Music is opened.
  if (MusicPlayer::beginSd()) Serial.println("SD mounted");
  else Serial.printf("SD not mounted: %s\n", MusicPlayer::lastError());
  MusicPlayer::setVolume(17); // -14 dB: about the old default loudness
  MusicBrowser::begin(gfx, {C_TEXT, C_DIM, C_FAINT, C_PINK, C_GREEN, C_BLUE, C_RED, C_NEUTRAL});

  screen = Screen::Home;
  render();
}

// ---------------------------------------------------------------------------
// Screen sleep: holding DOWN on any screen (outside a game, where DOWN is a
// game control) turns just the display off, right where it is; music keeps
// playing. Only turning the wheel about 45° turns it back on - buttons and
// touch are ignored while it's off, and the waking turn does nothing else.
//
// The arrows sit on the wheel's ring, so pressing them often nudges the
// wheel a click; for a moment after sleeping nothing wakes it.
// ---------------------------------------------------------------------------

bool displayAsleep = false;
static uint32_t asleepSince = 0;
static int32_t sleepWheelAcc = 0;
static const uint32_t WAKE_GRACE_MS = 800;
static const int32_t WAKE_WHEEL_CLICKS = 3; // 24 clicks per turn -> 45°
// The waking turn usually carries on past 45°; the wheel is ignored for a
// second after waking, so the rest of it doesn't change the volume or scroll.
static const uint32_t WAKE_WHEEL_IGNORE_MS = 1000;
static uint32_t wheelIgnoredUntil = 0;

bool wakeAllowed() { return millis() - asleepSince >= WAKE_GRACE_MS; }

void sleepDisplay() {
  if (displayAsleep) return;
  displayAsleep = true;
  asleepSince = millis();
  sleepWheelAcc = 0;
  panel->Display_Brightness(0);
  panel->displayOff(); // AMOLED controller into sleep, not just dimmed
  Serial.println("display off");
}

void wakeDisplay() {
  if (!displayAsleep) return;
  displayAsleep = false;
  panel->displayOn();
  panel->Display_Brightness(brightness);
  needsRedraw = true;
  Serial.println("display on");
}

// SELECT is both "open/play" and "switch off", told apart by hold time, so
// its short action fires on release.
static uint32_t selectPressedAt = 0;
static bool selectArmed = false;
static const uint32_t POWER_OFF_HOLD_MS = 1200;
static const uint32_t GAME_QUIT_HOLD_MS = 1000;
static bool downUsedForBrightness = false;
// A long DOWN press, let go without turning the wheel, locks the screen
// where it is; turning the wheel wakes it there again. It acts on release,
// so a hold that's still waiting for a brightness turn never locks.
static uint32_t downPressedAt = 0;
static const uint32_t DOWN_LOCK_HOLD_MS = 800;

// In the game every button belongs to it, except that holding SELECT quits
// to Home (one way out that works in every game and every mode).
void handleGameKey(const InputEvent &ev) {
  if (ev.key == Key::Select && !ev.repeat) { // (auto-repeats would restart the hold)
    if (ev.pressed) {
      selectPressedAt = millis();
      selectArmed = true;
    } else {
      selectArmed = false;
    }
  }
  gameKey(ev);
  if (gameWantsExit()) leaveGame();
}

void handleKey(const InputEvent &ev) {
  if (gameActive()) {
    handleGameKey(ev);
    return;
  }
  bool picker = MusicBrowser::pickerOpen();
  bool leftRight = ev.key == Key::Left || ev.key == Key::Right;

  // Auto-repeat while held: LEFT/RIGHT scrub in Now playing and page
  // through the lists. Every other button acts once per press.
  if (ev.repeat) {
    if (!leftRight || picker) return;
    if (screen == Screen::NowPlaying) seekStep(ev.key == Key::Right ? 1 : -1);
    else if (screen == Screen::Music) browserAction(MusicBrowser::key(ev.key));
    return;
  }

  if (ev.key == Key::Select) {
    if (ev.pressed) {
      selectPressedAt = millis();
      selectArmed = true;
    } else if (selectArmed) {
      selectArmed = false;
      if (picker) browserAction(MusicBrowser::pickerKey(Key::Select));
      else if (screen == Screen::Home) openApp(homeSel);
      else if (screen == Screen::Games) launchGame(gamesSel);
      else if (screen == Screen::Music) browserAction(MusicBrowser::key(Key::Select));
      else if (screen == Screen::NowPlaying) playPause();
    }
    return;
  }

  // DOWN doubles as the brightness modifier (hold it and turn the wheel),
  // so its own action waits for release and is skipped if the wheel moved.
  if (ev.key == Key::Down) {
    if (ev.pressed) {
      downUsedForBrightness = false;
      downPressedAt = millis();
    } else if (downUsedForBrightness) {
      prefs.putInt("bright", brightness); // save once, not every click
    } else if (millis() - downPressedAt >= DOWN_LOCK_HOLD_MS) {
      Serial.println("DOWN held - lock screen");
      sleepDisplay();
    } else if (picker) {
      browserAction(MusicBrowser::pickerKey(Key::Down));
    } else if (screen == Screen::Music) {
      browserAction(MusicBrowser::down());
    } else if (screen == Screen::NowPlaying) {
      MusicBrowser::openPicker(MusicPlayer::currentTrack());
      needsRedraw = true;
    }
    return;
  }

  // LEFT/RIGHT in Now playing: a tap skips (on release, once it's clear it
  // wasn't a hold), a hold scrubs and jumps on release.
  if (screen == Screen::NowPlaying && leftRight && !picker) {
    if (ev.pressed) {
      seekKeyDownAt = millis();
    } else if (seeking) {
      seekFinish();
    } else {
      skip(ev.key == Key::Right ? 1 : -1);
    }
    return;
  }

  if (!ev.pressed) return;

  if (ev.key == Key::Up) {
    if (picker) browserAction(MusicBrowser::pickerKey(Key::Up));
    else if (screen == Screen::Music) browserAction(MusicBrowser::key(Key::Up));
    else if (screen == Screen::NowPlaying) { screen = Screen::Music; needsRedraw = true; }
    else if (screen != Screen::Home) goHome();
    return;
  }
  if (picker) {
    browserAction(MusicBrowser::pickerKey(ev.key));
    return;
  }
  switch (screen) {
    case Screen::Home:
      if (ev.key == Key::Left && homeSel > 0) { homeSel--; needsRedraw = true; }
      if (ev.key == Key::Right && homeSel < 2) { homeSel++; needsRedraw = true; }
      break;
    case Screen::Music:
      browserAction(MusicBrowser::key(ev.key));
      break;
    case Screen::NowPlaying:
      break;
    case Screen::Games:
      if (ev.key == Key::Left && gamesSel > 0) { gamesSel--; needsRedraw = true; }
      if (ev.key == Key::Right && gamesSel < GAMES - 1) { gamesSel++; needsRedraw = true; }
      break;
    case Screen::AddSongs:
      if (ev.key == Key::Left) goHome();
      break;
  }
}

void handleTouch() {
  if (!touch.justPressed()) return;
  int16_t x, y;
  gfx->touchToLandscape(touch.x(), touch.y(), x, y);

  if (MusicBrowser::pickerOpen()) {
    browserAction(MusicBrowser::pickerTap(x, y));
    return;
  }

  // "< Home" / "< Music" in the top-left corner.
  if (screen != Screen::Home && y < 40 && x < 140) {
    if (screen == Screen::NowPlaying) { screen = Screen::Music; needsRedraw = true; }
    else goHome();
    return;
  }

  if (screen == Screen::Home) {
    for (int i = 0; i < 3; i++) {
      if (x >= tileX(i) && x < tileX(i) + TILE_W && y >= TILE_Y && y < TILE_Y + TILE_H) {
        homeSel = i;
        openApp(i);
        return;
      }
    }
  } else if (screen == Screen::Music) {
    browserAction(MusicBrowser::tap(x, y));
  } else if (screen == Screen::Games) {
    for (int slot = 0; slot < GAMES_SHOWN; slot++) {
      if (x >= gameSlotX(slot) && x < gameSlotX(slot) + GTILE_W && y >= GTILE_Y && y < GTILE_Y + GTILE_H) {
        gamesSel = gamesFirst() + slot;
        launchGame(gamesSel);
        return;
      }
    }
  } else if (screen == Screen::NowPlaying) {
    auto near = [&](int16_t cx, int16_t r) {
      int16_t dx = x - cx, dy = y - BTN_Y;
      return dx * dx + dy * dy <= r * r;
    };
    if (near(BTN_PLAY_X, 32)) playPause();
    else if (near(BTN_PREV_X, 28)) skip(-1);
    else if (near(BTN_NEXT_X, 28)) skip(+1);
  }
}

// The wheel: tile focus on Home, scrolling in the Music lists and the
// picker, volume in Now playing; with DOWN held, brightness everywhere.
void handleWheel(int32_t delta) {
  static int32_t wheelAcc = 0;
  if (delta == 0) return;
  if (input.held(Key::Down)) {
    downUsedForBrightness = true;
    changeBrightness(delta > 0 ? 1 : -1);
  } else if (MusicBrowser::pickerOpen()) {
    browserAction(MusicBrowser::pickerWheel(delta));
  } else if (screen == Screen::Home || screen == Screen::Games) {
    int &sel = screen == Screen::Home ? homeSel : gamesSel;
    int maxSel = screen == Screen::Home ? 2 : GAMES - 1;
    wheelAcc += delta;
    while (wheelAcc >= 2) { wheelAcc -= 2; if (sel < maxSel) sel++; needsRedraw = true; }
    while (wheelAcc <= -2) { wheelAcc += 2; if (sel > 0) sel--; needsRedraw = true; }
  } else if (screen == Screen::Music) {
    browserAction(MusicBrowser::wheel(delta));
  } else if (screen == Screen::NowPlaying) {
    changeVolume(delta > 0 ? 1 : -1);
  }
}

// Bench commands over USB serial, for testing without hands on the
// buttons: g = open the game, m = open Music, h = Home, s/u/d/l/r = tap a
// button, L/R = hold LEFT/RIGHT for ~1.5 s, +/- = turn the wheel a click,
// w = wake the screen, P / p = hold down / let go of SELECT.
void serialCommands() {
  if (!Serial.available()) return;
  char c = Serial.read();
  Key k = c == 's' ? Key::Select : c == 'u' ? Key::Up : c == 'd' ? Key::Down
        : c == 'l' ? Key::Left : c == 'r' ? Key::Right : Key::None;
  if ((c == 'g' || c == 'c') && screen == Screen::Home) {
    openApp(1);
    launchGame(c == 'g' ? 0 : 1);
  } else if (c >= '0' && c < '0' + GAMES && screen == Screen::Home) {
    openApp(1);
    launchGame(c - '0');
  } else if (c == 'm' && screen == Screen::Home) openApp(0);
  else if (c == 'h') goHome();
  else if (c == 'w') wakeDisplay();
  else if (c == 'P' || c == 'p') input.debugHoldSelect(c == 'P'); // hold / let go of SELECT
  else if (gameActive() && k != Key::None) { // (screenshots etc. still work in a game)
    gameKey({k, true, false});
    gameKey({k, false, false});
    if (gameWantsExit()) leaveGame();
  } else if (k != Key::None) {
    handleKey({k, true, false});
    handleKey({k, false, false});
  } else if (c == 'L' || c == 'R') {
    Key h = c == 'L' ? Key::Left : Key::Right;
    handleKey({h, true, false});
    for (int i = 0; i < 12; i++) handleKey({h, true, true});
    handleKey({h, false, false});
  } else if (c == '+' || c == '-') {
    handleWheel(c == '+' ? 1 : -1);
  } else if (c == 'S') {
    // Screenshot: raw RGB565 of what's on screen - the maze game's half-res
    // frame, or the canvas (portrait, as the panel sees it).
    const uint16_t *fb = canvas->getFramebuffer();
    int w = LCD_WIDTH, h = LCD_HEIGHT;
    if (game == Game::Cypress && gameActive()) { fb = CypressNight::frameBuffer(); w = 228; h = 140; }
    if (game == Game::Tea && gameActive()) { fb = LateForTea::frameBuffer(); w = 228; h = 140; }
    if (game == Game::Run && gameActive()) { fb = LullabyRun::frameBuffer(); w = 228; h = 140; }
    if (game == Game::Oz && gameActive()) { fb = EmeraldCity::frameBuffer(); w = 228; h = 140; }
    if (game == Game::Cat && gameActive()) { fb = CatNap::frameBuffer(); w = 228; h = 140; }
    if (game == Game::Thread && gameActive()) { fb = Ariadne::frameBuffer(); w = 228; h = 140; }
    if (game == Game::Lamp && gameActive()) { fb = Lamplighter::frameBuffer(); w = 456; h = 280; } // full resolution
    if (!fb) { fb = canvas->getFramebuffer(); w = LCD_WIDTH; h = LCD_HEIGHT; }
    if (game == Game::Fly && gameActive()) { fb = Crane::frameBuffer(); w = 228; h = 140; }
    Serial.printf("\nSHOT %d %d %d\n", w, h, (int)gfx->flipped());
    Serial.write((const uint8_t *)fb, (size_t)w * h * 2);
    Serial.println();
  } else if (c == 'A' && game == Game::Tea) {
    LateForTea::benchUnlockAll();
  } else if (c == 'A' && game == Game::Oz) {
    EmeraldCity::benchUnlockAll();
  } else if (c == 'A' && game == Game::Thread) {
    Ariadne::benchUnlockAll();
  } else if (c == 'Y' && game == Game::Thread) {
    Ariadne::benchPlayAll();
  } else if (c == 'Z' && game == Game::Fly) {
    Crane::benchAutopilot();
  } else if (c == 'Z' && game == Game::Lamp) {
    Lamplighter::benchAutopilot();
  } else if (c == 'E' && game == Game::Lamp) {
    Lamplighter::benchEdge();
  } else if (c == 'T' && game == Game::Lamp) {
    Lamplighter::benchStats();
  } else if (c == 'Z' && game == Game::Oz) {
    EmeraldCity::benchAutoplay();
  } else if (c == 'Z' && game == Game::Tea) {
    LateForTea::benchLineup();
  } else if (c == 'I') {
    // The same rescan the web organizer runs (only sensible from Add songs).
    MusicPlayer::clearQueue();
    Library::scan();
    Playlists::load();
  } else if (c == 'T') {
    // Step the WiFi transmit power down (wraps back to full), to find the
    // level this board/case transmits cleanly at.
    static const wifi_power_t levels[] = {WIFI_POWER_19_5dBm, WIFI_POWER_15dBm, WIFI_POWER_11dBm,
                                          WIFI_POWER_8_5dBm, WIFI_POWER_5dBm};
    static int lv = 0;
    lv = (lv + 1) % 5;
    WiFi.setTxPower(levels[lv]);
    Serial.printf("WiFi TX power now %.1f dBm\n", WiFi.getTxPower() / 4.0f);
  } else if (c == 'i') {
    Serial.printf("info: screen %d, SD %d, library ready %d dirty %d, %u songs, %u playlists, "
                  "PSRAM free %u (largest %u), heap free %u, IP %s, WiFi signal %d dBm\n",
                  (int)screen, MusicPlayer::sdReady(), Library::ready(), Library::dirty(), Library::count(),
                  Playlists::count(), ESP.getFreePsram(), ESP.getMaxAllocPsram(), ESP.getFreeHeap(),
                  WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  }
}

void loop() {
  serialCommands();
  input.poll();
  touch.poll();
  MusicPlayer::loop(); // lifts the anti-click mute after track changes
  BatteryMonitor::poll();
  TimeSync::poll();

  // The 5V boost only runs while music is playing on battery - the DAC is
  // its only user. Checked often, since the gap between unplugging and the
  // boost starting is a DAC dropout.
  static uint32_t lastPowerCheck = 0;
  if (millis() - lastPowerCheck >= 250) {
    lastPowerCheck = millis();
    BatteryMonitor::setBoostWanted(musicWantsPower());
    BatteryMonitor::managePowerPath();
    recoverCodec();
  }

  InputEvent ev;
  while (input.next(ev)) {
    if (displayAsleep) {
      // Buttons do nothing while it's off; only the wheel wakes it.
      if (ev.pressed) sleepWheelAcc = 0; // an arrow's nudge isn't a wake turn
      if (ev.key == Key::Down && ev.pressed) downUsedForBrightness = true; // its release won't act once awake
      continue;
    }
    handleKey(ev); // it decides which buttons act on auto-repeat
  }

  // A held SELECT quits a game to Home, or anywhere else switches off. The
  // quit disarms it, so a hold carried on into Home doesn't also switch off.
  if (!displayAsleep && selectArmed && input.held(Key::Select)) {
    uint32_t held = millis() - selectPressedAt;
    if (gameActive() && held >= GAME_QUIT_HOLD_MS) {
      selectArmed = false;
      Serial.println("SELECT held - quit game to Home");
      goHome();
    } else if (!gameActive() && held >= POWER_OFF_HOLD_MS) {
      selectArmed = false;
      powerDown();
    }
  }

  if (displayAsleep) {
    touch.justPressed(); // touch doesn't wake the screen; just drop it
  } else if (!gameActive()) {
    handleTouch(); // the game reads touch itself
  }

  int32_t delta = input.encoderDelta();
  if (displayAsleep) {
    if (wakeAllowed()) sleepWheelAcc += delta;
    // A real turn (~45°) wakes the screen; a nudge from an arrow press doesn't.
    if (abs(sleepWheelAcc) >= WAKE_WHEEL_CLICKS) {
      wakeDisplay();
      wheelIgnoredUntil = millis() + WAKE_WHEEL_IGNORE_MS;
    }
    delta = 0;
  } else if ((int32_t)(millis() - wheelIgnoredUntil) < 0) {
    delta = 0;
  }
  if (gameActive()) {
    // The game owns the wheel (steering) and draws its own frames.
    if (!displayAsleep) gameFrame(delta);
    delta = 0;
  }
  handleWheel(delta);
  if (MusicBrowser::toastEnded()) needsRedraw = true;

  if (screen == Screen::AddSongs) {
    if (!SongUpload::running() && SongUpload::start()) {
      Serial.printf("Add songs: http://%s (or jigglypuff.local)\n", WiFi.localIP().toString().c_str());
    }
    SongUpload::poll();
  }

  static uint32_t lastTick = 0;
  uint32_t tickEvery = (screen == Screen::NowPlaying || SongUpload::busy()) ? 1000 : 5000;
  if (millis() - lastTick >= tickEvery) {
    lastTick = millis();

    // Advance to the next track when one ends.
    if (musicActive && !MusicPlayer::isPaused() && !MusicPlayer::isRunning() &&
        MusicPlayer::queueLength() > 0 && !seeking) {
      checkUsbAtTrackChange();
      MusicPlayer::next();
    }
    needsRedraw = true;
  }

  // Volume pill disappearing also needs a redraw.
  static bool volWasShown = false;
  bool volShown = millis() < volumeShownUntil || millis() < brightnessShownUntil;
  if (volWasShown && !volShown) needsRedraw = true;
  volWasShown = volShown;

  // Nothing is drawn while the screen sleeps; whatever piled up is drawn
  // once on waking.
  if (gameActive()) {
    needsRedraw = false;
    if (displayAsleep) delay(20);
    return; // the game paces itself: draw again straight away
  }
  if (needsRedraw && !displayAsleep) render();
  delay(displayAsleep ? 20 : 5);
}
