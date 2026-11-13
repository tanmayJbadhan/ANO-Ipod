// Gaslight & Griffins - text and panels, drawn into the current strip.
#pragma GCC optimize("O2")
#include "GL.h"
#include "Raster.h"

namespace Gaslight {

static inline const GlyphInfo *glyphOf(const FontInfo &f, char ch) {
  unsigned c = (unsigned char)ch;
  if (c < 32 || c > 126) c = '?';
  return &f.g[c - 32];
}

int textW(const FontInfo &f, const char *s, int n) {
  int w = 0;
  for (int i = 0; s[i] && (n < 0 || i < n); i++) {
    if (s[i] == '{' || s[i] == '}') continue;
    w += glyphOf(f, s[i])->adv;
  }
  return w;
}

static void glyph(const FontInfo &f, int x, int y, char ch, uint16_t col) {
  const GlyphInfo *g = glyphOf(f, ch);
  if (!g->w) return;
  int top = Raster::top(), h = Raster::height();
  if (y + f.height <= top || y >= top + h) return;
  int stride = (g->w + 1) / 2;
  const uint8_t *d = f.data + g->off;
  int gx = x + g->xoff;
  for (int j = 0; j < f.height; j++) {
    uint16_t *r = Raster::row(y + j);
    if (!r) continue;
    const uint8_t *src = d + j * stride;
    for (int i = 0; i < g->w; i++) {
      int a = (src[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
      if (!a) continue;
      int px = gx + i;
      if (px < 0 || px >= W) continue;
      r[px] = a == 15 ? col : mix565(r[px], col, a * 17);
    }
  }
}

int text(const FontInfo &f, int x, int y, const char *s, uint16_t col, uint16_t hi, int n) {
  uint16_t c = col;
  for (int i = 0; s[i] && (n < 0 || i < n); i++) {
    if (s[i] == '{') { c = hi ? hi : col; continue; }
    if (s[i] == '}') { c = col; continue; }
    glyph(f, x, y, s[i], c);
    x += glyphOf(f, s[i])->adv;
  }
  return x;
}

void textC(const FontInfo &f, int cx, int y, const char *s, uint16_t col) { text(f, cx - textW(f, s) / 2, y, s, col); }

void textShadow(const FontInfo &f, int x, int y, const char *s, uint16_t col) {
  text(f, x + 1, y + 1, s, 0);
  text(f, x, y, s, col);
}

int wrap(const FontInfo &f, int x, int y, int maxW, const char *s, uint16_t col, uint16_t hi, int lineH, int maxLines,
         int chars, bool draw) {
  int line = 0, used = 0;
  bool inHi = false;
  while (*s) {
    // take as many words as fit
    int n = 0, lastBreak = -1, w = 0;
    while (s[n] && s[n] != '\n') {
      if (s[n] != '{' && s[n] != '}') {
        w += glyphOf(f, s[n])->adv;
        if (w > maxW) break;
      }
      if (s[n] == ' ') lastBreak = n;
      n++;
    }
    int take = n;
    if (s[n] && s[n] != '\n' && lastBreak > 0) take = lastBreak;
    if (draw && line < maxLines) {
      int show = chars - used;
      if (show > 0) {
        uint16_t c0 = inHi ? (hi ? hi : col) : col;
        // carry a highlight across the line break
        int xx = x;
        uint16_t c = c0;
        for (int i = 0; i < take && i < show; i++) {
          if (s[i] == '{') { c = hi ? hi : col; continue; }
          if (s[i] == '}') { c = col; continue; }
          glyph(f, xx, y + line * lineH, s[i], c);
          xx += glyphOf(f, s[i])->adv;
        }
      }
    }
    for (int i = 0; i < take; i++) {
      if (s[i] == '{') inHi = true;
      if (s[i] == '}') inHi = false;
    }
    used += take;
    s += take;
    while (*s == ' ') { s++; used++; }
    if (*s == '\n') { s++; used++; }
    line++;
  }
  return line;
}

void fadeRect(float x, float y, float w, float h, uint16_t c, uint8_t a) { Raster::rect(x, y, w, h, c, a); }

// Where line `line` of the wrapped text starts (a character index), or the
// text's length if it has fewer lines - for showing long text a page at a time.
int wrapLineStart(const FontInfo &f, int maxW, const char *s, int line) {
  const char *start = s;
  for (int l = 0; l < line && *s; l++) {
    int n = 0, lastBreak = -1, w = 0;
    while (s[n] && s[n] != 10) {
      if (s[n] != '{' && s[n] != '}') {
        w += glyphOf(f, s[n])->adv;
        if (w > maxW) break;
      }
      if (s[n] == ' ') lastBreak = n;
      n++;
    }
    int take = n;
    if (s[n] && s[n] != 10 && lastBreak > 0) take = lastBreak;
    s += take;
    while (*s == ' ') s++;
    if (*s == 10) s++;
  }
  return (int)(s - start);
}

void glow(float cx, float cy, float r, uint16_t c, int strength) {
  int top = Raster::top(), h = Raster::height();
  int y0 = (int)(cy - r), y1 = (int)(cy + r) + 1;
  if (y0 < top) y0 = top;
  if (y1 > top + h) y1 = top + h;
  if (y0 >= y1 || r < 1) return;
  int cr = c >> 11, cg = (c >> 5) & 63, cb = c & 31;
  float inv = 1.0f / (r * r);
  for (int y = y0; y < y1; y++) {
    uint16_t *row = Raster::row(y);
    if (!row) continue;
    float dy = y + 0.5f - cy, dy2 = dy * dy;
    float span = r * r - dy2;
    if (span <= 0) continue;
    float hw = sqrtf(span);
    int x0 = (int)(cx - hw), x1 = (int)(cx + hw) + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    for (int x = x0; x < x1; x++) {
      float dx = x + 0.5f - cx;
      float f = 1.0f - (dx * dx + dy2) * inv;
      if (f <= 0) continue;
      int k = (int)(f * f * strength); // 0..256
      uint16_t d = row[x];
      int R = (d >> 11) + ((cr * k) >> 8), G2 = ((d >> 5) & 63) + ((cg * k) >> 8), B = (d & 31) + ((cb * k) >> 8);
      if (R > 31) R = 31;
      if (G2 > 63) G2 = 63;
      if (B > 31) B = 31;
      row[x] = (uint16_t)((R << 11) | (G2 << 5) | B);
    }
  }
}

void panel(float x, float y, float w, float h, uint16_t fill, uint8_t alpha, uint16_t edge) {
  Raster::roundRect(x, y, w, h, 7, edge, alpha > 200 ? 255 : (uint8_t)(alpha + 40 > 255 ? 255 : alpha + 40));
  Raster::roundRect(x + 1.5f, y + 1.5f, w - 3, h - 3, 6, fill, alpha);
}

} // namespace Gaslight
