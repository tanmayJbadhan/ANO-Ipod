#include "Raster.h"
#include <math.h>

namespace Raster {

static uint16_t *s_buf = nullptr;
static int16_t s_w = 0, s_h = 0;
static float s_k = 1; // coordinate scale, see setScale()

void setScale(float k) { s_k = k; }

void target(uint16_t *buf, int16_t w, int16_t h) {
  s_buf = buf;
  s_w = w;
  s_h = h;
}
uint16_t *buffer() { return s_buf; }
int16_t width() { return s_w; }
int16_t height() { return s_h; }

uint16_t rgb(uint32_t hex) {
  return (uint16_t)(((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex & 0xFF) >> 3));
}

uint32_t mixHex(uint32_t a, uint32_t b, float t) {
  if (t <= 0.004f) return a;
  if (t >= 1) return b;
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

uint16_t mix(uint32_t a, uint32_t b, float t) { return rgb(mixHex(a, b, t)); }

uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (((br - ar) * t) >> 8);
  int g = ag + (((bg - ag) * t) >> 8);
  int bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void clear(uint16_t c) {
  int32_t n = (int32_t)s_w * s_h;
  for (int32_t i = 0; i < n; i++) s_buf[i] = c;
}

void span(int y, int x0, int x1, uint16_t c, uint8_t a) {
  if (y < 0 || y >= s_h || a == 0) return;
  if (x0 < 0) x0 = 0;
  if (x1 > s_w) x1 = s_w;
  if (x1 <= x0) return;
  uint16_t *p = s_buf + (int32_t)y * s_w + x0, *e = p + (x1 - x0);
  if (a == 255) {
    while (p < e) *p++ = c;
  } else {
    for (; p < e; p++) *p = blend(*p, c, a);
  }
}

// Pixel range covered by [a, b) when sampling at pixel centres, but never
// less than one pixel for anything at least ~1/3 px wide (thin poles and
// lines would otherwise flicker in and out as they move).
static inline void cover(float a, float b, int &i0, int &i1) {
  i0 = (int)lroundf(a);
  i1 = (int)lroundf(b);
  if (i1 <= i0 && b - a > 0.35f) i1 = i0 + 1;
}

void rect(float x, float y, float w, float h, uint16_t c, uint8_t a) {
  x *= s_k; y *= s_k; w *= s_k; h *= s_k;
  if (w <= 0 || h <= 0) return;
  float xr = x + w;
  if (x > 30000 || xr < -30000) return;
  if (x < -1) x = -1;
  if (xr > 30000) xr = 30000;
  int x0, x1, y0, y1;
  cover(x, xr, x0, x1);
  cover(y, y + h, y0, y1);
  if (y0 < 0) y0 = 0;
  if (y1 > s_h) y1 = s_h;
  for (int yy = y0; yy < y1; yy++) span(yy, x0, x1, c, a);
}

void poly(const float *in, int n, uint16_t c, uint8_t a) {
  if (n < 3 || n > 16) return;
  float xy[32];
  for (int i = 0; i < n * 2; i++) xy[i] = in[i] * s_k;
  float ymin = xy[1], ymax = xy[1];
  for (int i = 1; i < n; i++) {
    float y = xy[i * 2 + 1];
    if (y < ymin) ymin = y;
    if (y > ymax) ymax = y;
  }
  int r0 = (int)ceilf(ymin - 0.5f), r1 = (int)floorf(ymax - 0.5f);
  if (r0 < 0) r0 = 0;
  if (r1 >= s_h) r1 = s_h - 1;
  for (int yi = r0; yi <= r1; yi++) {
    float yc = yi + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      float x0 = xy[i * 2], y0 = xy[i * 2 + 1], x1 = xy[j * 2], y1 = xy[j * 2 + 1];
      if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) {
        float x = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
        if (x < xl) xl = x;
        if (x > xr) xr = x;
      }
    }
    if (xr < xl || xl > 30000 || xr < -30000) continue;
    if (xl < -1) xl = -1;
    if (xr > 30000) xr = 30000;
    int i0, i1;
    cover(xl, xr, i0, i1);
    span(yi, i0, i1, c, a);
  }
}

void quad4(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
          uint16_t c, uint8_t a) {
  float p[8] = {x1, y1, x2, y2, x3, y3, x4, y4};
  poly(p, 4, c, a);
}

void trap(float xl1, float xr1, float y1, float xl2, float xr2, float y2, uint16_t c, uint8_t a) {
  xl1 *= s_k; xr1 *= s_k; y1 *= s_k; xl2 *= s_k; xr2 *= s_k; y2 *= s_k;
  if (y1 == y2) return;
  if (y1 < y2) {
    float t;
    t = xl1; xl1 = xl2; xl2 = t;
    t = xr1; xr1 = xr2; xr2 = t;
    t = y1; y1 = y2; y2 = t;
  }
  // y2 is the top edge, y1 the bottom.
  int r0 = (int)ceilf(y2 - 0.5f), r1 = (int)ceilf(y1 - 0.5f);
  if (r0 < 0) r0 = 0;
  if (r1 > s_h) r1 = s_h;
  float inv = 1.0f / (y1 - y2);
  for (int yi = r0; yi < r1; yi++) {
    float t = (yi + 0.5f - y2) * inv;
    float xl = xl2 + (xl1 - xl2) * t, xr = xr2 + (xr1 - xr2) * t;
    if (xr < xl || xl > 30000 || xr < -30000) continue;
    if (xl < -1) xl = -1;
    if (xr > 30000) xr = 30000;
    int i0, i1;
    cover(xl, xr, i0, i1);
    span(yi, i0, i1, c, a);
  }
}

void ellipse(float cx, float cy, float rx, float ry, uint16_t c, uint8_t a) {
  cx *= s_k; cy *= s_k; rx *= s_k; ry *= s_k;
  if (rx < 0.3f || ry < 0.3f) {
    if (rx >= 0.15f && ry >= 0.15f) span((int)floorf(cy), (int)floorf(cx), (int)floorf(cx) + 1, c, a);
    return;
  }
  int r0 = (int)ceilf(cy - ry - 0.5f), r1 = (int)floorf(cy + ry - 0.5f);
  if (r0 < 0) r0 = 0;
  if (r1 >= s_h) r1 = s_h - 1;
  for (int yi = r0; yi <= r1; yi++) {
    float dy = (yi + 0.5f - cy) / ry;
    float q = 1 - dy * dy;
    if (q <= 0) continue;
    float hw = rx * sqrtf(q);
    int i0, i1;
    cover(cx - hw, cx + hw, i0, i1);
    span(yi, i0, i1, c, a);
  }
}

void circle(float cx, float cy, float r, uint16_t c, uint8_t a) { ellipse(cx, cy, r, r, c, a); }

void ellipseRot(float cx, float cy, float rx, float ry, float ang, uint16_t c, uint8_t a) {
  float p[24], ca = cosf(ang), sa = sinf(ang);
  for (int i = 0; i < 12; i++) {
    float t = i * 0.5235988f, ex = cosf(t) * rx, ey = sinf(t) * ry;
    p[i * 2] = cx + ex * ca - ey * sa;
    p[i * 2 + 1] = cy + ex * sa + ey * ca;
  }
  poly(p, 12, c, a);
}

void line(float x0, float y0, float x1, float y1, float w, uint16_t c, uint8_t a) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) return;
  if (w < 1) w = 1;
  float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
  quad4(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c, a);
}

void roundRect(float x, float y, float w, float h, float r, uint16_t c, uint8_t a) {
  x *= s_k; y *= s_k; w *= s_k; h *= s_k; r *= s_k;
  if (w <= 0 || h <= 0) return;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  int y0, y1;
  cover(y, y + h, y0, y1);
  if (y0 < 0) y0 = 0;
  if (y1 > s_h) y1 = s_h;
  for (int yi = y0; yi < y1; yi++) {
    float yc = yi + 0.5f, inset = 0;
    float d = 0;
    if (yc < y + r) d = y + r - yc;
    else if (yc > y + h - r) d = yc - (y + h - r);
    if (d > 0) inset = r - sqrtf(fmaxf(0, r * r - d * d));
    int i0, i1;
    cover(x + inset, x + w - inset, i0, i1);
    span(yi, i0, i1, c, a);
  }
}

void star(float cx, float cy, float r, uint16_t c) {
  float inner[10];
  for (int i = 0; i < 5; i++) {
    float ai = (i * 2 + 1) * 0.6283185f - 1.5707963f;
    inner[i * 2] = cx + cosf(ai) * r * 0.45f;
    inner[i * 2 + 1] = cy + sinf(ai) * r * 0.45f;
  }
  poly(inner, 5, c);
  for (int i = 0; i < 5; i++) {
    float a = i * 2 * 0.6283185f - 1.5707963f;
    int j = (i + 4) % 5;
    float t[6] = {cx + cosf(a) * r, cy + sinf(a) * r, inner[j * 2], inner[j * 2 + 1], inner[i * 2], inner[i * 2 + 1]};
    poly(t, 3, c);
  }
}

} // namespace Raster
