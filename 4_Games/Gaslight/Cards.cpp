// Gaslight & Griffins - myth scene cards: an illustration with the old
// story told underneath, and the end-of-act cards.
#pragma GCC optimize("O2")
#include "Game.h"
#include "Raster.h"
#include "Engravings.h"
#include <stdio.h>

namespace Gaslight {

// Engraving-style pictures, drawn as layered silhouettes in sepia.
static const uint16_t SEPIA0 = 0xE6D4, SEPIA1 = 0xB4EC, SEPIA2 = 0x7B67, SEPIA3 = 0x4204;

static void waves(float x0, float x1, float y, float amp, uint16_t c) {
  for (float x = x0; x < x1; x += 14) {
    Raster::line(x, y + sinf(x * 0.08f) * amp, x + 14, y + sinf((x + 14) * 0.08f) * amp, 1.6f, c);
    Raster::line(x + 4, y + 8 + sinf(x * 0.06f + 1) * amp, x + 16, y + 8 + sinf((x + 12) * 0.06f + 1) * amp, 1.2f, c, 160);
  }
}

static void winged(float x, float y, float s, uint16_t c) {
  Raster::ellipse(x, y + 14 * s, 6 * s, 16 * s, c);
  Raster::circle(x, y - 6 * s, 5 * s, c);
  poly3(x - 3 * s, y, x - 22 * s, y - 16 * s, x - 14 * s, y + 14 * s, c);
  poly3(x + 3 * s, y, x + 22 * s, y - 16 * s, x + 14 * s, y + 14 * s, c);
}

static void ship(float x, float y, float s, uint16_t c) {
  poly4(x - 40 * s, y, x + 44 * s, y, x + 32 * s, y + 14 * s, x - 30 * s, y + 14 * s, c);
  Raster::rect(x - 2 * s, y - 64 * s, 3 * s, 64 * s, c);
  poly3(x + 2 * s, y - 60 * s, x + 30 * s, y - 34 * s, x + 2 * s, y - 14 * s, c, 200);
  poly3(x - 3 * s, y - 56 * s, x - 26 * s, y - 30 * s, x - 3 * s, y - 16 * s, c, 170);
}

// An engraving from the book, inked in sepia on old paper.
static const int PIC_H = 112, PIC_SKIP = (150 - 112) / 2;
static bool engraving(int id, int x, int y) {
  if (id < 0 || id >= 24 || !ENGRAVINGS[id]) return false;
  static uint16_t lut[16];
  static bool made = false;
  if (!made) {
    for (int i = 0; i < 16; i++) {
      float k = i / 15.0f;
      int r = (int)(52 + (233 - 52) * k), g = (int)(36 + (214 - 36) * k), b = (int)(24 + (176 - 24) * k);
      lut[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    made = true;
  }
  const uint8_t *src = ENGRAVINGS[id];
  for (int j = 0; j < PIC_H; j++) {
    uint16_t *row = Raster::row(y + j);
    if (!row) continue;
    const uint8_t *s = src + (j + PIC_SKIP) * (ENGR_W / 2);
    uint16_t *d = row + x;
    for (int i = 0; i < ENGR_W / 2; i++) {
      d[2 * i] = lut[s[i] >> 4];
      d[2 * i + 1] = lut[s[i] & 15];
    }
  }
  return true;
}

static void picture(int id, float x, float y, float w, float h, float t) {
  if (engraving(id, (int)x, (int)y)) {
    Raster::rect(x, y, w, 3, SEPIA3); Raster::rect(x, y + h - 3, w, 3, SEPIA3);
    Raster::rect(x, y, 3, h, SEPIA3); Raster::rect(x + w - 3, y, 3, h, SEPIA3);
    return;
  }
  Raster::rect(x, y, w, h, SEPIA0);
  // a little hatching for the paper
  for (int i = 0; i < 26; i++) Raster::line(x + hashf(i, 7) * w, y + hashf(i, 8) * h, x + hashf(i, 7) * w + 10, y + hashf(i, 8) * h + 4, 1, SEPIA1, 50);
  float cx = x + w / 2;
  switch (id) {
    case 0:
      poly4(x + w * 0.62f, y + h * 0.62f, x + w * 0.98f, y + h * 0.62f, x + w * 0.94f, y + h * 0.3f, x + w * 0.7f, y + h * 0.35f, SEPIA2);
      for (int i = 0; i < 3; i++) winged(x + w * (0.72f + i * 0.08f), y + h * 0.28f, 1.0f, SEPIA3);
      ship(x + w * 0.3f, y + h * 0.66f + sinf(t * 1.5f) * 2, 1.2f, SEPIA3);
      Raster::rect(x + w * 0.3f - 4, y + h * 0.66f - 46, 9, 26, SEPIA2); // the man at the mast
      waves(x, x + w, y + h * 0.78f, 3, SEPIA2);
      break;
    case 1:
      Raster::ellipse(cx, y + h, w * 0.48f, h * 0.95f, SEPIA3);
      Raster::ellipse(cx, y + h, w * 0.4f, h * 0.8f, SEPIA2);
      Raster::ellipse(cx, y + h * 0.62f, 34, 44, SEPIA3);
      Raster::circle(cx, y + h * 0.38f, 22, SEPIA3);
      Raster::circle(cx, y + h * 0.36f, 8, c565(0xE8B050));
      Raster::circle(cx, y + h * 0.36f, 3, SEPIA3);
      for (int i = 0; i < 3; i++) Raster::ellipse(x + w * 0.2f + i * 18, y + h * 0.86f, 9, 6, SEPIA0);
      Raster::circle(x + w * 0.78f, y + h * 0.84f, 10 + sinf(t * 7) * 2, c565(0xE8A040), 200);
      break;
    case 2:
      ship(cx, y + h * 0.7f, 1.1f, SEPIA3);
      for (int i = 0; i < 5; i++) {
        float a = t * 0.6f + i * 1.25f, r = 46 + i * 6;
        float sx = cx + cosf(a) * r * 1.6f, sy = y + h * 0.35f + sinf(a) * r * 0.5f;
        Raster::circle(sx, sy, 9, SEPIA2);
        Raster::line(sx + 8, sy, sx + 30, sy - 4, 2, SEPIA2);
      }
      waves(x, x + w, y + h * 0.82f, 5, SEPIA2);
      break;
    case 3:
      poly4(cx - 70, y + h * 0.7f, cx + 70, y + h * 0.7f, cx + 40, y + h * 0.35f, cx - 30, y + h * 0.32f, SEPIA2);
      for (int i = 0; i < 3; i++) winged(cx - 30 + i * 30, y + h * 0.24f, 0.9f, SEPIA3);
      for (int i = 0; i < 4; i++) {
        float r = 50 + i * 22 + fmodf(t * 30, 22);
        Raster::line(cx + r * 0.7f, y + h * 0.3f - 6, cx + r * 0.9f, y + h * 0.3f + 8, 1.4f, SEPIA3, 160);
      }
      ship(x + w * 0.86f, y + h * 0.74f, 0.6f, SEPIA3);
      waves(x, x + w, y + h * 0.8f, 3, SEPIA2);
      break;
    case 11: { // the nick of the axe
      Raster::ellipse(cx - 40, y + h * 0.7f, 30, 40, SEPIA3);          // the kneeling knight
      Raster::circle(cx - 40, y + h * 0.35f, 16, SEPIA3);
      Raster::ellipse(cx - 40, y + h * 0.55f, 22, 5, c565(0x3A8A3A)); // the green girdle
      float sw = sinf(t * 2) * 0.2f;
      Raster::line(cx + 60, y + 10, cx - 10 + sw * 40, y + h * 0.4f, 4, SEPIA2); // the axe haft
      poly3(cx - 10 + sw * 40, y + h * 0.4f - 20, cx + 20 + sw * 40, y + h * 0.4f, cx - 10 + sw * 40, y + h * 0.4f + 20, SEPIA3);
      Raster::ellipse(cx + 80, y + h * 0.5f, 26, 60, c565(0x2E5A2E));  // the Green Knight
      break;
    }
    case 14: { // the Falls, remembered: a rope down the white water
      Raster::rect(cx - 30, y, 60, h, c565(0xC8C0B0));
      for (int i = 0; i < 8; i++) Raster::line(cx - 30 + i * 8, y, cx - 26 + i * 8, y + h, 2, SEPIA1, 120);
      Raster::rect(x + 6, y, cx - 36 - x, h, SEPIA2);
      Raster::rect(cx + 30, y, x + w - cx - 36, h, SEPIA2);
      Raster::line(cx - 40, y + 20, cx - 34, y + h * 0.7f, 1.5f, SEPIA3);   // the rope
      Raster::ellipse(cx - 34, y + h * 0.55f + sinf(t) * 4, 6, 14, SEPIA3);   // the Professor climbing
      Raster::ellipse(cx - 70, y + 24, 8, 16, c565(0x23324A));               // a man in a police coat
      break;
    }
    case 15: { // the lost midsummer: dancers on the shore, two ravens over
      waves(x, x + w, y + h * 0.8f, 3, SEPIA2);
      for (int i = 0; i < 7; i++) {
        float dx = x + 40 + i * 50, bob = sinf(t * 3 + i) * 6;
        Raster::ellipse(dx, y + h * 0.6f + bob, 8, 18, SEPIA3);
        Raster::circle(dx, y + h * 0.42f + bob, 6, SEPIA3);
      }
      Raster::circle(x + w * 0.85f, y + 30, 18, c565(0xE8C870));
      for (int i = 0; i < 2; i++) winged(x + w * 0.3f + i * 40 + sinf(t + i) * 10, y + 30, 0.5f, SEPIA3);
      break;
    }
    default:
      Raster::circle(cx, y + h / 2, 30, SEPIA2);
      break;
  }
  // frame
  Raster::rect(x, y, w, 3, SEPIA3); Raster::rect(x, y + h - 3, w, 3, SEPIA3);
  Raster::rect(x, y, 3, h, SEPIA3); Raster::rect(x + w - 3, y, 3, h, SEPIA3);
  Raster::rect(x + 6, y + 6, w - 12, 1, SEPIA2); Raster::rect(x + 6, y + h - 7, w - 12, 1, SEPIA2);
}

int g_cardPage = 0;
bool cardHasMore(int id) {
  if (id < 0 || id >= N_CARDS) return false;
  const char *s = CARD_TEXT[id];
  const char *nl = strchr(s, '\n');
  const char *body = nl ? nl + 1 : s;
  return body[wrapLineStart(F_SMALL, W - 40, body, g_cardPage * 5 + 5)] != 0;
}

void drawCard(int id, float t) {
  Raster::rect(0, 0, W, H, c565(0x1A1612));
  if (id >= 100) {
    static const char *const T[5][2] = {
        {"The first case is closed", "Fogmouth's Sisters are singing again."},
        {"The Long Night", "Kidu wakes hollow, and the old powers fall silent in four towns. Vane walks out into the dark with a "
                           "shadow that isn't his friend. You become the one who hunts him, for the rest of your life."},
        {"The Arrest", "Professor Vane goes to trial in Ur-Lumen and says nothing in his own defence. The Sisters sing, the Feather "
                       "rests, the moss is green and the ravens fly. Juno visits him in prison. So do you, on Sundays."},
        {"The Letting Go", "The Serpent took the plant. Kidu said goodbye. Vane served his sentence teaching street kids to read "
                           "footprints. And a small, warm light follows you on every case now, like a candle that never goes out."},
        {"The Serpent's Ending", "Every stolen power came home. Twenty old stories glow in your casebook. Vane teaches at Fogmouth "
                                 "Library on Tuesdays, and Dr. Lune makes him bring back the books. Two came. Two returned."},
    };
    int e = clampi(id - 100, 0, 4);
    float a = clampf(t * 0.5f, 0, 1);
    textC(F_TITLE, W / 2, 34, T[e][0], mix565(0, Col::CREAM, (int)(256 * a)));
    wrap(F_SMALL, 30, 88, W - 60, T[e][1], mix565(0, c565(0xD8D0C0), (int)(256 * a)), Col::AMBER, 22, 6);
    if (t > 3) {
      char line[80];
      snprintf(line, sizeof(line), "Cases %d of 4   Favours %d   Fragments %d of 20", getVar("case1") + getVar("case2") + getVar("case3") + getVar("case4"),
               getVar("favours"), getVar("fragments"));
      textC(F_SMALL, W / 2, 226, line, Col::BRASS);
      textC(F_SMALL, W / 2, 250, "Thank you for playing.  Centre: the title", Col::DIM);
    }
    return;
  }
  if (id < 0 || id >= N_CARDS) return;
  picture(id, 24, 6, ENGR_W, PIC_H, t);
  const char *s = CARD_TEXT[id];
  const char *nl = strchr(s, '\n');
  char ttl[64];
  snprintf(ttl, sizeof(ttl), "%.*s", nl ? (int)(nl - s) : 0, s);
  textC(F_BOLD, W / 2, 120, ttl, Col::AMBER);
  // five lines a page; Centre turns it
  const char *body = nl ? nl + 1 : s;
  int off = wrapLineStart(F_SMALL, W - 40, body, g_cardPage * 5);
  wrap(F_SMALL, 20, 150, W - 40, body + off, c565(0xE0D6C4), 0, 21, 5, (int)(t * 90));
  bool more = body[wrapLineStart(F_SMALL, W - 40, body, g_cardPage * 5 + 5)] != 0;
  if (t > 1.0f && fmodf(t, 1.0f) < 0.6f) {
    if (more) poly3(W - 30.0f, 262, W - 14.0f, 262, W - 22.0f, 272, Col::AMBER);
    else Raster::rect(W - 30, 264, 14, 6, Col::AMBER);
  }
}

} // namespace Gaslight
