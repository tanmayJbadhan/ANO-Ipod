/*
 * Jigglypuff - a pocket music console for the T-Display-S3-AMOLED-1.64,
 * used in landscape (456x280 logical; see Landscape.h for why rotation is
 * done in software) with a "liquid glass" look (see Glass.h).
 *
 *   Home      three large glass tiles: Music, Games, Add songs
 *   Music     player for whatever is on the SD card
 *   Games     Lullaby Run, an 11-district racer (see LullabyRun.h)
 *   Add songs web page on the home WiFi that uploads straight to the card
 *
 * Controls (ANO wheel):
 *   wheel / LEFT / RIGHT  move between tiles; in Music, wheel = volume and
 *                         LEFT/RIGHT = previous/next track
 *   SELECT                open / play-pause
 *   UP                    back to Home
 *   UP three times fast   screen off (music keeps playing); double-tap any
 *                         arrow or turn the wheel ~45 degrees to wake it
 *   hold DOWN + wheel     screen brightness
 *   hold SELECT ~1.2s     switch off; hold ~2s while off to switch on
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
#include "SongUpload.h"
#include "LullabyRun.h"
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

enum class Screen { Home, Music, Games, AddSongs };
Screen screen = Screen::Home;

int homeSel = 0;           // focused tile on Home
bool musicActive = false;  // the user wants music playing (drives auto-advance)
bool needsRedraw = true;
uint32_t volumeShownUntil = 0;

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

void drawStatusBar(bool showBack) {
  // Left: "< Home" on sub-screens. (No WiFi indicator - WiFi is off
  // everywhere except Add Songs, which shows its own status.)
  if (showBack) text(24, 12, "< Home", 2, C_DIM);

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
  drawStatusBar(false);

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
  if (musicActive && MusicPlayer::trackCount() > 0) {
    char title[32], line[40];
    trackTitle(MusicPlayer::trackPath(MusicPlayer::currentIndex()), title, sizeof(title), 22);
    snprintf(line, sizeof(line), "%s %s", MusicPlayer::isPaused() ? "||" : ">", title);
    int16_t w = gfx->textWidth(line, 2) + 40;
    Glass::panel((SCREEN_W - w) / 2, 232, w, 32, 16, C_PINK, 70, 30);
    textCenter(SCREEN_W / 2, 240, line, 2, C_TEXT);
  }
}

// Player button geometry, shared by drawing and touch hit-testing.
static const int16_t BTN_Y = 220;
static const int16_t BTN_PREV_X = 262, BTN_PLAY_X = 318, BTN_NEXT_X = 374;

void drawMusic() {
  Glass::background();
  drawStatusBar(true);
  Glass::panel(24, 40, 408, 212, 28, C_NEUTRAL, 30, 40);

  if (!MusicPlayer::sdReady() || MusicPlayer::trackCount() == 0) {
    iconMusic(SCREEN_W / 2, 110, 12, C_DIM);
    textCenter(SCREEN_W / 2, 170, MusicPlayer::sdReady() ? "No songs yet" : "No SD card", 2, C_TEXT);
    textCenter(SCREEN_W / 2, 196,
               MusicPlayer::sdReady() ? "Use Add songs to put music on" : "Check the card is pushed in",
               1, C_DIM);
    return;
  }

  // Artwork tile.
  Glass::panel(44, 60, 136, 136, 24, C_PINK, 160, 80);
  iconMusic(112, 126, 13, C_TEXT);

  // Title over up to two lines.
  char title[48];
  trackTitle(MusicPlayer::trackPath(MusicPlayer::currentIndex()), title, sizeof(title), 34);
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
    text(200, 64, l1, 2, C_TEXT);
    text(200, 86, l2, 2, C_TEXT);
  } else {
    text(200, 72, title, 2, C_TEXT);
  }

  char pos[24];
  snprintf(pos, sizeof(pos), "%u of %u", (unsigned)(MusicPlayer::currentIndex() + 1),
           (unsigned)MusicPlayer::trackCount());
  text(200, 116, pos, 2, C_DIM);

  // Progress.
  uint32_t cur = MusicPlayer::currentTimeSec();
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

  textCenter(SCREEN_W / 2, 262, "UP home   SELECT play   LEFT/RIGHT skip   WHEEL volume", 1, C_DIM);
}

void drawGames() {
  Glass::background();
  drawStatusBar(true);
  Glass::panel(24, 40, 408, 212, 28, C_GREEN, 60, 40);
  iconGames(SCREEN_W / 2, 112, C_TEXT, Glass::blend(C_GREEN, 0x0000, 90));
  textCenter(SCREEN_W / 2, 158, "Games", 2, C_TEXT);
  textCenter(SCREEN_W / 2, 186, "Not enough memory", 2, C_DIM); // the game failed to start
}

void drawAddSongs() {
  Glass::background();
  drawStatusBar(true);
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
    case Screen::Music: drawMusic(); break;
    case Screen::Games: drawGames(); break;
    case Screen::AddSongs: drawAddSongs(); break;
  }
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
  return (musicActive && !MusicPlayer::isPaused()) || LullabyRun::wantsAudioPower();
}

bool gameActive() { return screen == Screen::Games && LullabyRun::active(); }

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
  if (MusicPlayer::sdReady() && MusicPlayer::trackCount() == 0) MusicPlayer::scanTracks();
}

void startMusic(size_t index) {
  if (MusicPlayer::playIndex(index)) musicActive = true;
  powerAudioNow();
}

void goHome() {
  if (gameActive()) LullabyRun::exit();
  if (screen == Screen::AddSongs) {
    SongUpload::stop();
    TimeSync::wifiOff(); // WiFi only lives while Add Songs is open
    MusicPlayer::scanTracks(); // pick up whatever was just added
  }
  screen = Screen::Home;
  needsRedraw = true;
}

void openApp(int i) {
  if (i == 0) {
    screen = Screen::Music;
    ensureCard();
    // Opening Music starts playback if nothing is playing yet.
    if (!musicActive && MusicPlayer::trackCount() > 0) startMusic(MusicPlayer::currentIndex());
  } else if (i == 1) {
    screen = Screen::Games;
    // The engine sound needs the DAC, so the music stops for the race.
    MusicPlayer::stop();
    musicActive = false;
    if (LullabyRun::enter(gfx, canvas, panel)) {
      Pcm5122::setMute(false); // a paused track leaves the DAC muted
      powerAudioNow();
    }
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
  if (MusicPlayer::trackCount() == 0) return;
  if (!musicActive) {
    startMusic(MusicPlayer::currentIndex());
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
  if (MusicPlayer::trackCount() == 0) return;
  checkUsbAtTrackChange();
  bool ok = dir > 0 ? MusicPlayer::next() : MusicPlayer::previous();
  if (ok) musicActive = true;
  powerAudioNow();
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

  if (MusicPlayer::beginSd()) {
    Serial.printf("SD mounted, %u track(s)\n", (unsigned)MusicPlayer::scanTracks());
  } else {
    Serial.printf("SD not mounted: %s\n", MusicPlayer::lastError());
  }
  MusicPlayer::setVolume(17); // -14 dB: about the old default loudness

  screen = Screen::Home;
  render();
}

// ---------------------------------------------------------------------------
// Screen sleep: triple-clicking UP turns just the display off (music
// keeps playing). Only a double-click on any arrow, or turning the
// wheel about 45°, turns it back on - single presses, the centre button
// and touch are ignored while it's off, so it can't wake by accident in a
// pocket. The waking input does nothing else.
//
// The arrows sit on the wheel's ring, so pressing them often nudges the
// wheel a click; for a moment after sleeping nothing wakes it.
// ---------------------------------------------------------------------------

bool displayAsleep = false;
static uint32_t asleepSince = 0;
static int32_t sleepWheelAcc = 0;
static const uint32_t DOUBLE_TAP_MS = 400;
static const uint32_t WAKE_GRACE_MS = 800;
static const int32_t WAKE_WHEEL_CLICKS = 3; // 24 clicks per turn -> 45°

static Key lastTapKey = Key::Select;
static uint32_t lastTapAt = 0;
static uint8_t tapCount = 0;

// Counts quick repeated presses of one button: returns 1 for a fresh
// press, 2 for the second within DOUBLE_TAP_MS of the last, and so on.
uint8_t countTap(Key k) {
  uint32_t now = millis();
  bool chained = tapCount > 0 && k == lastTapKey && now - lastTapAt < DOUBLE_TAP_MS;
  tapCount = chained ? tapCount + 1 : 1;
  lastTapKey = k;
  lastTapAt = now;
  return tapCount;
}

void resetTaps() { tapCount = 0; }

// True when this press is the second of a quick double-click on one arrow.
bool isDoubleTap(Key k) {
  if (countTap(k) < 2) return false;
  resetTaps();
  return true;
}

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
static bool downUsedForBrightness = false;

// In the game every button belongs to it, except that holding SELECT still
// switches off and triple-clicking UP still turns the screen off.
void handleGameKey(const InputEvent &ev) {
  if (ev.key == Key::Select) {
    if (ev.pressed) {
      selectPressedAt = millis();
      selectArmed = true;
    } else {
      selectArmed = false;
    }
  }
  if (ev.pressed) {
    if (ev.key == Key::Up) {
      if (countTap(Key::Up) >= 3) {
        resetTaps();
        LullabyRun::pause();
        sleepDisplay();
        return;
      }
    } else {
      resetTaps();
    }
  }
  LullabyRun::key(ev);
  if (LullabyRun::wantsExit()) goHome();
}

void handleKey(const InputEvent &ev) {
  if (gameActive()) {
    handleGameKey(ev);
    return;
  }
  if (ev.key == Key::Select) {
    if (ev.pressed) {
      selectPressedAt = millis();
      selectArmed = true;
    } else if (selectArmed) {
      selectArmed = false;
      if (screen == Screen::Home) openApp(homeSel);
      else if (screen == Screen::Music) playPause();
    }
    return;
  }

  // DOWN doubles as the brightness modifier (hold it and turn the wheel),
  // so its own action waits for release and is skipped if the wheel moved.
  if (ev.key == Key::Down) {
    if (ev.pressed) {
      downUsedForBrightness = false;
    } else if (downUsedForBrightness) {
      prefs.putInt("bright", brightness); // save once, not every click
    } else if (screen == Screen::Music) {
      changeVolume(-1);
    }
  }
  if (!ev.pressed) return;

  if (ev.key == Key::Up) {
    // Triple-click UP is the only way to turn the screen off.
    if (countTap(Key::Up) >= 3) {
      resetTaps();
      sleepDisplay();
      return;
    }
    if (screen != Screen::Home) goHome();
    return;
  }
  resetTaps();

  switch (screen) {
    case Screen::Home:
      if (ev.key == Key::Left && homeSel > 0) { homeSel--; needsRedraw = true; }
      if (ev.key == Key::Right && homeSel < 2) { homeSel++; needsRedraw = true; }
      break;
    case Screen::Music:
      if (ev.key == Key::Left) skip(-1);
      if (ev.key == Key::Right) skip(+1);
      break;
    case Screen::Games:
    case Screen::AddSongs:
      if (ev.key == Key::Left) goHome();
      break;
  }
}

void handleTouch() {
  if (!touch.justPressed()) return;
  int16_t x, y;
  gfx->touchToLandscape(touch.x(), touch.y(), x, y);

  if (screen != Screen::Home && y < 40 && x < 140) {
    goHome();
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
    auto near = [&](int16_t cx, int16_t r) {
      int16_t dx = x - cx, dy = y - BTN_Y;
      return dx * dx + dy * dy <= r * r;
    };
    if (near(BTN_PLAY_X, 32)) playPause();
    else if (near(BTN_PREV_X, 28)) skip(-1);
    else if (near(BTN_NEXT_X, 28)) skip(+1);
  }
}

// Bench commands over USB serial, for testing without hands on the
// buttons: g = open the game, h = Home, s/u/d/l/r = tap a button in it.
void serialCommands() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'g' && screen == Screen::Home) openApp(1);
  else if (c == 'h') goHome();
  else if (gameActive()) {
    Key k = c == 's' ? Key::Select : c == 'u' ? Key::Up : c == 'd' ? Key::Down
          : c == 'l' ? Key::Left : c == 'r' ? Key::Right : Key::None;
    if (k != Key::None) {
      LullabyRun::key({k, true, false});
      LullabyRun::key({k, false, false});
    }
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
      // Only an arrow double-click wakes it; the press does nothing else.
      if (ev.pressed && !ev.repeat && ev.key != Key::Select && wakeAllowed() &&
          isDoubleTap(ev.key)) {
        wakeDisplay();
      }
      continue;
    }
    if (!ev.repeat) handleKey(ev); // one action per press, even when held
  }

  // Switch off as soon as SELECT has been held long enough.
  if (!displayAsleep && selectArmed && input.held(Key::Select) && millis() - selectPressedAt >= POWER_OFF_HOLD_MS) {
    selectArmed = false;
    powerDown();
  }

  if (displayAsleep) {
    touch.justPressed(); // touch doesn't wake the screen; just drop it
  } else if (!gameActive()) {
    handleTouch(); // the game reads touch itself
  }

  // The wheel: tile focus on Home, volume in Music.
  static int32_t wheelAcc = 0;
  int32_t delta = input.encoderDelta();
  if (displayAsleep) {
    // A real turn (~45°) wakes the screen; a nudge from an arrow press doesn't.
    if (wakeAllowed()) sleepWheelAcc += delta;
    if (abs(sleepWheelAcc) >= WAKE_WHEEL_CLICKS) wakeDisplay();
    delta = 0;
  }
  if (gameActive()) {
    // The game owns the wheel (steering) and draws its own frames.
    if (!displayAsleep) LullabyRun::frame(input, touch, delta);
    delta = 0;
  }
  if (delta != 0 && input.held(Key::Down)) {
    // Holding DOWN turns the wheel into a brightness dial, on any screen.
    downUsedForBrightness = true;
    changeBrightness(delta > 0 ? 1 : -1);
  } else if (delta != 0) {
    if (screen == Screen::Home) {
      wheelAcc += delta;
      while (wheelAcc >= 2) { wheelAcc -= 2; if (homeSel < 2) homeSel++; needsRedraw = true; }
      while (wheelAcc <= -2) { wheelAcc += 2; if (homeSel > 0) homeSel--; needsRedraw = true; }
    } else if (screen == Screen::Music) {
      changeVolume(delta > 0 ? 1 : -1);
    }
  }

  if (screen == Screen::AddSongs) {
    if (!SongUpload::running()) SongUpload::start();
    SongUpload::poll();
  }

  static uint32_t lastTick = 0;
  uint32_t tickEvery = (screen == Screen::Music || SongUpload::busy()) ? 1000 : 5000;
  if (millis() - lastTick >= tickEvery) {
    lastTick = millis();

    // Advance to the next track when one ends.
    if (musicActive && !MusicPlayer::isPaused() && !MusicPlayer::isRunning() &&
        MusicPlayer::trackCount() > 0) {
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
