#include "Glass.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include "JigglypuffArt.h"

namespace Glass {

static LandscapeGFX *s_gfx = nullptr;
static Arduino_Canvas *s_canvas = nullptr;
static uint16_t *s_bgPlain = nullptr; // portrait layout, same as the canvas framebuffer
static uint16_t *s_bgHome = nullptr;  // the same, with Jigglypuff faded in
static uint16_t *s_bg = nullptr;      // whichever of the two is in use
static int16_t s_pw = 0, s_ph = 0; // portrait dimensions
static const uint16_t WHITE565 = 0xFFFF;
static const uint16_t ART_OPACITY = 125; // of 256: clearly there, but behind the glass
static bool s_flat = false; // Home: no sheen or graded rim, just flat glass

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (((br - ar) * t) >> 8);
  int g = ag + (((bg - ag) * t) >> 8);
  int bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

struct Blob {
  float x, y, radius;
  uint8_t r, g, b;
  float strength;
};

// Deep night-sky base with three soft light sources, so there is colour
// for the glass to pick up wherever a panel lands.
static const Blob BLOBS[] = {
    {70, 40, 260, 40, 90, 255, 0.85f},   // blue, top left
    {420, 270, 280, 220, 40, 140, 0.80f}, // magenta, bottom right
    {330, 10, 190, 0, 200, 190, 0.55f},   // teal, top right
    {120, 290, 200, 255, 120, 40, 0.45f}, // amber, bottom left
};

bool begin(LandscapeGFX *gfx, Arduino_Canvas *canvas) {
  s_gfx = gfx;
  s_canvas = canvas;
  s_pw = canvas->width();
  s_ph = canvas->height();

  size_t bytes = (size_t)s_pw * s_ph * sizeof(uint16_t);
  s_bgPlain = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_bgPlain) s_bgPlain = (uint16_t *)malloc(bytes);
  if (!s_bgPlain) return false;
  s_bg = s_bgPlain;

  const int16_t W = gfx->width(), H = gfx->height();
  for (int16_t y = 0; y < H; y++) {
    // Vertical base gradient: near-black navy to deep violet.
    float t = (float)y / (H - 1);
    float r = 8 + 22 * t, g = 10 + 4 * t, b = 30 + 30 * t;

    for (int16_t x = 0; x < W; x++) {
      float cr = r, cg = g, cb = b;
      for (const Blob &bl : BLOBS) {
        float dx = x - bl.x, dy = y - bl.y;
        float d = sqrtf(dx * dx + dy * dy) / bl.radius;
        if (d >= 1.0f) continue;
        float f = (1.0f - d);
        f = f * f * bl.strength;
        cr += (bl.r - cr) * f;
        cg += (bl.g - cg) * f;
        cb += (bl.b - cb) * f;
      }
      int16_t px, py;
      gfx->pointToPortrait(x, y, px, py);
      s_bg[py * s_pw + px] = rgb((uint8_t)cr, (uint8_t)cg, (uint8_t)cb);
    }
  }

  // Home background: plain black - no gradient or glow - with Jigglypuff
  // faded in, centred. If there's no room for a second buffer, Home just
  // goes without.
  s_bgHome = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_bgHome) {
    memset(s_bgHome, 0, bytes);
    const int16_t ox = (W - JIGGLY_W) / 2, oy = (H - JIGGLY_H) / 2;
    for (int16_t y = 0; y < JIGGLY_H; y++) {
      for (int16_t x = 0; x < JIGGLY_W; x++) {
        int32_t i = (int32_t)y * JIGGLY_W + x;
        uint8_t a = (uint8_t)((JIGGLY_ALPHA[i] * ART_OPACITY) >> 8);
        if (!a) continue;
        int16_t px, py;
        gfx->pointToPortrait(ox + x, oy + y, px, py);
        uint16_t &dst = s_bgHome[py * s_pw + px];
        dst = blend(dst, JIGGLY_RGB[i], a);
      }
    }
  }
  return true;
}

void useHomeTheme(bool on) {
  s_bg = (on && s_bgHome) ? s_bgHome : s_bgPlain;
  s_flat = on;
}

void background() {
  if (!s_bg) return;
  memcpy(s_canvas->getFramebuffer(), s_bg, (size_t)s_pw * s_ph * sizeof(uint16_t));
}

// Shared per-pixel glass shading. `edge` = on the outer rim; `fy` = 0 at the
// top of the shape, 1 at the bottom.
static inline uint16_t shade(uint16_t base, uint16_t tint, uint8_t strength, uint8_t lift,
                             bool edge, float fy) {
  uint16_t c = blend(base, tint, strength);
  c = blend(c, WHITE565, 20 + lift / 4); // frost

  if (s_flat) {
    if (edge) c = blend(c, WHITE565, 110 + lift / 2);
    return c;
  }

  // Sheen: light catching the upper part of the glass, fading downwards.
  if (fy < 0.45f) {
    float s = (1.0f - fy / 0.45f) * (34 + lift / 3);
    c = blend(c, WHITE565, (uint8_t)s);
  }

  // Rim: brightest along the top edge, dimmer along the bottom.
  if (edge) {
    int a = (int)(170 - 110 * fy) + lift / 2;
    if (a > 240) a = 240;
    c = blend(c, WHITE565, (uint8_t)a);
  }
  return c;
}

static inline void put(int16_t sx, int16_t sy, uint16_t *fb, uint16_t tint, uint8_t strength,
                       uint8_t lift, bool edge, float fy) {
  int16_t px, py;
  s_gfx->pointToPortrait(sx, sy, px, py);
  if (px < 0 || py < 0 || px >= s_pw || py >= s_ph) return;
  int32_t i = (int32_t)py * s_pw + px;
  fb[i] = shade(s_bg[i], tint, strength, lift, edge, fy);
}

void panel(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
           uint16_t tint, uint8_t strength, uint8_t lift) {
  if (!s_bg || w <= 0 || h <= 0) return;
  uint16_t *fb = s_canvas->getFramebuffer();
  const int16_t W = s_gfx->width(), H = s_gfx->height();
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  const int32_t rOut = (int32_t)r * r;
  const int32_t rIn = (int32_t)(r - 1) * (r - 1);

  for (int16_t ly = 0; ly < h; ly++) {
    int16_t sy = y + ly;
    if (sy < 0 || sy >= H) continue;
    float fy = (float)ly / (h - 1);

    for (int16_t lx = 0; lx < w; lx++) {
      int16_t sx = x + lx;
      if (sx < 0 || sx >= W) continue;

      bool edge = (lx == 0 || lx == w - 1 || ly == 0 || ly == h - 1);

      // Rounded corners: distance from the corner circle's centre.
      int16_t cx = -1, cy = -1;
      if (lx < r) cx = r - 1 - lx;
      else if (lx >= w - r) cx = lx - (w - r);
      if (ly < r) cy = r - 1 - ly;
      else if (ly >= h - r) cy = ly - (h - r);
      if (cx >= 0 && cy >= 0) {
        int32_t d2 = (int32_t)cx * cx + (int32_t)cy * cy;
        if (d2 > rOut) continue;
        edge = d2 > rIn;
      }

      put(sx, sy, fb, tint, strength, lift, edge, fy);
    }
  }
}

void disc(int16_t cx, int16_t cy, int16_t r, uint16_t tint, uint8_t strength, uint8_t lift) {
  if (!s_bg || r <= 0) return;
  uint16_t *fb = s_canvas->getFramebuffer();
  const int16_t W = s_gfx->width(), H = s_gfx->height();
  const int32_t rOut = (int32_t)r * r;
  const int32_t rIn = (int32_t)(r - 1) * (r - 1);

  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t sy = cy + dy;
    if (sy < 0 || sy >= H) continue;
    float fy = (float)(dy + r) / (2 * r);
    for (int16_t dx = -r; dx <= r; dx++) {
      int16_t sx = cx + dx;
      if (sx < 0 || sx >= W) continue;
      int32_t d2 = (int32_t)dx * dx + (int32_t)dy * dy;
      if (d2 > rOut) continue;
      put(sx, sy, fb, tint, strength, lift, d2 > rIn, fy);
    }
  }
}

} // namespace Glass
