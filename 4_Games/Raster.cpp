// Speed matters more than size here: the shape loops run per pixel.
#pragma GCC optimize("O2")
#include "Raster.h"
#include <math.h>
#ifndef PC_SIM
#include <freertos/FreeRTOS.h>
#endif

namespace Raster {

// One drawing state per core (see Raster.h).
struct State {
  uint16_t *buf = nullptr;
  int16_t w = 0, h = 0, top = 0; // rows [top, top + h) of the picture
  float k = 1;                   // coordinate scale, see setScale()
  bool smooth = false;
};
static State s_st[2];

static inline State &st() {
#ifndef PC_SIM
  return s_st[xPortGetCoreID() & 1];
#else
  return s_st[0];
#endif
}

void setScale(float k) { st().k = k; }
void smooth(bool on) { st().smooth = on; }

void target(uint16_t *buf, int16_t w, int16_t h, int16_t top) {
  State &s = st();
  s.buf = buf;
  s.w = w;
  s.h = h;
  s.top = top;
}
uint16_t *buffer() { return st().buf; }
int16_t width() { return st().w; }
int16_t height() { return st().h; }
int16_t top() { return st().top; }
uint16_t *row(int y) {
  State &s = st();
  y -= s.top;
  return y < 0 || y >= s.h ? nullptr : s.buf + (int32_t)y * s.w;
}

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
  State &s = st();
  int32_t n = (int32_t)s.w * s.h;
  for (int32_t i = 0; i < n; i++) s.buf[i] = c;
}

static void spanS(State &s, int y, int x0, int x1, uint16_t c, uint8_t a) {
  y -= s.top;
  if (y < 0 || y >= s.h || a == 0) return;
  if (x0 < 0) x0 = 0;
  if (x1 > s.w) x1 = s.w;
  if (x1 <= x0) return;
  uint16_t *p = s.buf + (int32_t)y * s.w + x0, *e = p + (x1 - x0);
  if (a == 255) {
    while (p < e) *p++ = c;
  } else {
    for (; p < e; p++) *p = blend(*p, c, a);
  }
}

void span(int y, int x0, int x1, uint16_t c, uint8_t a) { spanS(st(), y, x0, x1, c, a); }

// Row yi of a smooth shape whose left edge crosses the row somewhere in
// [l0, l1] and its right edge in [r0, r1]. A pixel's coverage is where its
// centre sits on those two ramps, min(left, right): 0 outside, 1 inside.
// Ramps are at least a pixel wide, so a vertical edge still gets a soft step.
static void rampSpan(State &s, int yi, float l0, float l1, float r0, float r1, uint16_t c, float a) {
  if (a < 0.5f) return;
  int y = yi - s.top;
  if (y < 0 || y >= s.h) return;
  if (l1 - l0 < 1) { float m = (l0 + l1) * 0.5f; l0 = m - 0.5f; l1 = m + 0.5f; }
  if (r1 - r0 < 1) { float m = (r0 + r1) * 0.5f; r0 = m - 0.5f; r1 = m + 0.5f; }
  if (r1 < -1 || l0 > s.w + 1) return;
  int x0 = (int)floorf(l0), x1 = (int)ceilf(r1);
  if (x0 < 0) x0 = 0;
  if (x1 > s.w) x1 = s.w;
  if (x1 <= x0) return;
  // Pixels whose centres lie past both ramps are fully covered.
  int m0 = (int)ceilf(l1 - 0.5f), m1 = (int)floorf(r0 - 0.5f) + 1;
  if (m1 <= m0) m0 = m1 = x1;
  if (m0 < x0) m0 = x0;
  if (m0 > x1) m0 = x1;
  if (m1 < m0) m1 = m0;
  if (m1 > x1) m1 = x1;
  uint16_t *p = s.buf + (int32_t)y * s.w;
  float il = 1 / (l1 - l0), ir = 1 / (r1 - r0);
  auto edge = [&](int x) {
    float xc = x + 0.5f, cl = (xc - l0) * il, cr = (r1 - xc) * ir;
    float cov = cl < cr ? cl : cr;
    if (cov <= 0) return;
    if (cov > 1) cov = 1;
    int t = (int)(a * cov + 0.5f);
    if (t >= 255) p[x] = c;
    else if (t > 0) p[x] = blend(p[x], c, (uint8_t)t);
  };
  for (int x = x0; x < m0; x++) edge(x);
  uint8_t ai = a >= 254.5f ? 255 : (uint8_t)(a + 0.5f);
  if (ai == 255) for (int x = m0; x < m1; x++) p[x] = c;
  else for (int x = m0; x < m1; x++) p[x] = blend(p[x], c, ai);
  for (int x = m1; x < x1; x++) edge(x);
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
  State &s = st();
  x *= s.k; y *= s.k; w *= s.k; h *= s.k;
  if (w <= 0 || h <= 0) return;
  float xr = x + w;
  if (x > 30000 || xr < -30000) return;
  if (x < -1) x = -1;
  if (xr > 30000) xr = 30000;
  int x0, x1, y0, y1;
  cover(x, xr, x0, x1);
  cover(y, y + h, y0, y1);
  if (y0 < s.top) y0 = s.top;
  if (y1 > s.top + s.h) y1 = s.top + s.h;
  for (int yy = y0; yy < y1; yy++) spanS(s, yy, x0, x1, c, a);
}

// Left and right edges of a convex polygon at height y.
static bool edgesAt(const float *xy, int n, float y, float &xl, float &xr) {
  xl = 1e9f;
  xr = -1e9f;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    float x0 = xy[i * 2], y0 = xy[i * 2 + 1], x1 = xy[j * 2], y1 = xy[j * 2 + 1];
    if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y)) {
      float x = y1 == y0 ? (x0 < x1 ? x0 : x1) : x0 + (y - y0) * (x1 - x0) / (y1 - y0);
      if (y1 == y0) {
        if (x0 < xl) xl = x0;
        if (x1 < xl) xl = x1;
        if (x0 > xr) xr = x0;
        if (x1 > xr) xr = x1;
        continue;
      }
      if (x < xl) xl = x;
      if (x > xr) xr = x;
    }
  }
  return xr >= xl;
}

void poly(const float *in, int n, uint16_t c, uint8_t a) {
  if (n < 3 || n > 16) return;
  State &s = st();
  float xy[32];
  for (int i = 0; i < n * 2; i++) xy[i] = in[i] * s.k;
  float ymin = xy[1], ymax = xy[1];
  for (int i = 1; i < n; i++) {
    float y = xy[i * 2 + 1];
    if (y < ymin) ymin = y;
    if (y > ymax) ymax = y;
  }
  if (ymax < s.top || ymin >= s.top + s.h) return;
  if (s.smooth) {
    // Each row: the edges at its top and bottom (and any corner inside it)
    // give the ramps; a row the shape only partly spans is fainter.
    int r0 = (int)floorf(ymin), r1 = (int)floorf(ymax);
    if (r0 < s.top) r0 = s.top;
    if (r1 > s.top + s.h - 1) r1 = s.top + s.h - 1;
    for (int yi = r0; yi <= r1; yi++) {
      float ya = yi > ymin ? (float)yi : ymin, yb = yi + 1 < ymax ? (float)(yi + 1) : ymax;
      float fy = yb - ya;
      if (fy <= 0.002f) continue;
      float la, ra, lb, rb;
      if (!edgesAt(xy, n, ya + 0.001f, la, ra) || !edgesAt(xy, n, yb - 0.001f, lb, rb)) continue;
      float l0 = la < lb ? la : lb, l1 = la < lb ? lb : la, q0 = ra < rb ? ra : rb, q1 = ra < rb ? rb : ra;
      for (int i = 0; i < n; i++) {
        float vy = xy[i * 2 + 1];
        if (vy > ya && vy < yb) {
          float vx = xy[i * 2];
          if (vx < l0) l0 = vx;
          if (vx > q1) q1 = vx;
        }
      }
      if (l1 > q1) l1 = q1;
      if (q0 < l0) q0 = l0;
      rampSpan(s, yi, l0, l1, q0, q1, c, a * fy);
    }
    return;
  }
  int r0 = (int)ceilf(ymin - 0.5f), r1 = (int)floorf(ymax - 0.5f);
  if (r0 < s.top) r0 = s.top;
  if (r1 >= s.top + s.h) r1 = s.top + s.h - 1;
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
    spanS(s, yi, i0, i1, c, a);
  }
}

void quad4(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
          uint16_t c, uint8_t a) {
  float p[8] = {x1, y1, x2, y2, x3, y3, x4, y4};
  poly(p, 4, c, a);
}

void trap(float xl1, float xr1, float y1, float xl2, float xr2, float y2, uint16_t c, uint8_t a) {
  State &s = st();
  xl1 *= s.k; xr1 *= s.k; y1 *= s.k; xl2 *= s.k; xr2 *= s.k; y2 *= s.k;
  if (y1 == y2) return;
  if (y1 < y2) {
    float t;
    t = xl1; xl1 = xl2; xl2 = t;
    t = xr1; xr1 = xr2; xr2 = t;
    t = y1; y1 = y2; y2 = t;
  }
  // y2 is the top edge, y1 the bottom.
  int r0 = (int)ceilf(y2 - 0.5f), r1 = (int)ceilf(y1 - 0.5f);
  if (r0 < s.top) r0 = s.top;
  if (r1 > s.top + s.h) r1 = s.top + s.h;
  float inv = 1.0f / (y1 - y2);
  for (int yi = r0; yi < r1; yi++) {
    float t = (yi + 0.5f - y2) * inv;
    float xl = xl2 + (xl1 - xl2) * t, xr = xr2 + (xr1 - xr2) * t;
    if (xr < xl || xl > 30000 || xr < -30000) continue;
    if (xl < -1) xl = -1;
    if (xr > 30000) xr = 30000;
    int i0, i1;
    cover(xl, xr, i0, i1);
    spanS(s, yi, i0, i1, c, a);
  }
}

void ellipse(float cx, float cy, float rx, float ry, uint16_t c, uint8_t a) {
  State &s = st();
  cx *= s.k; cy *= s.k; rx *= s.k; ry *= s.k;
  if (cy + ry < s.top || cy - ry >= s.top + s.h) return;
  if (s.smooth) {
    if (rx <= 0.05f || ry <= 0.05f) return;
    // Per row, the chord is widest at the y nearest the centre and
    // narrowest at the farthest: those bound the edge ramps.
    int r0 = (int)floorf(cy - ry), r1 = (int)floorf(cy + ry);
    if (r0 < s.top) r0 = s.top;
    if (r1 > s.top + s.h - 1) r1 = s.top + s.h - 1;
    float iry = 1 / ry;
    for (int yi = r0; yi <= r1; yi++) {
      float ya = yi > cy - ry ? (float)yi : cy - ry, yb = yi + 1 < cy + ry ? (float)(yi + 1) : cy + ry;
      float fy = yb - ya;
      if (fy <= 0.002f) continue;
      float yn = cy < ya ? ya : cy > yb ? yb : cy;
      float yf = fabsf(ya - cy) > fabsf(yb - cy) ? ya : yb;
      float dn = (yn - cy) * iry, df = (yf - cy) * iry;
      float hwMax = rx * sqrtf(fmaxf(0, 1 - dn * dn)), hwMin = rx * sqrtf(fmaxf(0, 1 - df * df));
      rampSpan(s, yi, cx - hwMax, cx - hwMin, cx + hwMin, cx + hwMax, c, a * fy);
    }
    return;
  }
  if (rx < 0.3f || ry < 0.3f) {
    if (rx >= 0.15f && ry >= 0.15f) spanS(s, (int)floorf(cy), (int)floorf(cx), (int)floorf(cx) + 1, c, a);
    return;
  }
  int r0 = (int)ceilf(cy - ry - 0.5f), r1 = (int)floorf(cy + ry - 0.5f);
  if (r0 < s.top) r0 = s.top;
  if (r1 >= s.top + s.h) r1 = s.top + s.h - 1;
  for (int yi = r0; yi <= r1; yi++) {
    float dy = (yi + 0.5f - cy) / ry;
    float q = 1 - dy * dy;
    if (q <= 0) continue;
    float hw = rx * sqrtf(q);
    int i0, i1;
    cover(cx - hw, cx + hw, i0, i1);
    spanS(s, yi, i0, i1, c, a);
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
  State &s = st();
  float lo = (y0 < y1 ? y0 : y1) - w, hi = (y0 < y1 ? y1 : y0) + w;
  if (hi * s.k < s.top || lo * s.k >= s.top + s.h) return;
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) return;
  if (w < 1) {
    // Smooth: a hairline is a one-pixel line, fainter.
    if (s.smooth && w * s.k > 0.05f) a = (uint8_t)(a * w);
    w = 1;
  }
  float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
  quad4(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c, a);
}

void roundRect(float x, float y, float w, float h, float r, uint16_t c, uint8_t a) {
  State &s = st();
  x *= s.k; y *= s.k; w *= s.k; h *= s.k; r *= s.k;
  if (w <= 0 || h <= 0) return;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  int y0, y1;
  cover(y, y + h, y0, y1);
  if (y0 < s.top) y0 = s.top;
  if (y1 > s.top + s.h) y1 = s.top + s.h;
  for (int yi = y0; yi < y1; yi++) {
    float yc = yi + 0.5f, inset = 0;
    float d = 0;
    if (yc < y + r) d = y + r - yc;
    else if (yc > y + h - r) d = yc - (y + h - r);
    if (d > 0) inset = r - sqrtf(fmaxf(0, r * r - d * d));
    if (s.smooth) {
      rampSpan(s, yi, x + inset - 0.5f, x + inset + 0.5f, x + w - inset - 0.5f, x + w - inset + 0.5f, c, a);
      continue;
    }
    int i0, i1;
    cover(x + inset, x + w - inset, i0, i1);
    spanS(s, yi, i0, i1, c, a);
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
