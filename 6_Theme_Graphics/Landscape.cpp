#include "Landscape.h"
#include <string.h>

LandscapeGFX::LandscapeGFX(Arduino_GFX *portrait) : _panel(portrait) {
  _portraitW = portrait->width();  // 280
  _portraitH = portrait->height(); // 456
}

bool LandscapeGFX::begin() {
  _textStage = new Arduino_Canvas(STAGE_W, STAGE_H, nullptr);
  return _textStage->begin();
}

// Flipped = the normal mapping applied to (W-1-x, H-1-y), i.e. the
// landscape image rotated a further 180°.
void LandscapeGFX::pointToPortrait(int16_t x, int16_t y, int16_t &px, int16_t &py) const {
  if (_flipped) {
    px = _portraitW - 1 - y;
    py = x;
  } else {
    px = y;
    py = _portraitH - 1 - x;
  }
}

void LandscapeGFX::rectToPortrait(int16_t x, int16_t y, int16_t w, int16_t h,
                                   int16_t &px, int16_t &py, int16_t &pw, int16_t &ph) const {
  if (_flipped) {
    px = _portraitW - y - h;
    py = x;
  } else {
    px = y;
    py = _portraitH - x - w;
  }
  pw = h;
  ph = w;
}

void LandscapeGFX::fillScreen(uint16_t color) { _panel->fillScreen(color); }

void LandscapeGFX::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  int16_t px, py, pw, ph;
  rectToPortrait(x, y, w, h, px, py, pw, ph);
  _panel->fillRect(px, py, pw, ph, color);
}

void LandscapeGFX::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  int16_t px, py, pw, ph;
  rectToPortrait(x, y, w, h, px, py, pw, ph);
  _panel->drawRect(px, py, pw, ph, color);
}

void LandscapeGFX::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  int16_t px, py, pw, ph;
  rectToPortrait(x, y, w, h, px, py, pw, ph);
  _panel->fillRoundRect(px, py, pw, ph, r, color);
}

void LandscapeGFX::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  int16_t px, py, pw, ph;
  rectToPortrait(x, y, w, h, px, py, pw, ph);
  _panel->drawRoundRect(px, py, pw, ph, r, color);
}

void LandscapeGFX::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  int16_t px, py;
  pointToPortrait(x, y, px, py);
  _panel->fillCircle(px, py, r, color);
}

void LandscapeGFX::drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  int16_t px, py;
  pointToPortrait(x, y, px, py);
  _panel->drawCircle(px, py, r, color);
}

void LandscapeGFX::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
  int16_t p0x, p0y, p1x, p1y, p2x, p2y;
  pointToPortrait(x0, y0, p0x, p0y);
  pointToPortrait(x1, y1, p1x, p1y);
  pointToPortrait(x2, y2, p2x, p2y);
  _panel->fillTriangle(p0x, p0y, p1x, p1y, p2x, p2y, color);
}

void LandscapeGFX::fillArc(int16_t cx, int16_t cy, int16_t outerR, int16_t innerR, float startDeg, float endDeg, uint16_t color) {
  int16_t px, py;
  pointToPortrait(cx, cy, px, py);
  // The rotation shifts angle by a constant offset; sign/direction to be
  // confirmed against the real panel (see LANDSCAPE TEST note).
  float off = _flipped ? 270 : 90;
  _panel->fillArc(px, py, outerR, innerR, startDeg + off, endDeg + off, color);
}

void LandscapeGFX::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  int16_t p0x, p0y, p1x, p1y;
  pointToPortrait(x0, y0, p0x, p0y);
  pointToPortrait(x1, y1, p1x, p1y);
  _panel->drawLine(p0x, p0y, p1x, p1y, color);
}

void LandscapeGFX::drawText(int16_t x, int16_t y, const char *text, uint8_t size, uint16_t color, uint16_t bg) {
  if (!_textStage) return;

  int16_t tw = textWidth(text, size);
  int16_t th = 8 * size;
  if (tw > STAGE_W) tw = STAGE_W;
  if (th > STAGE_H) th = STAGE_H;
  if (tw <= 0 || th <= 0) return;

  _textStage->fillRect(0, 0, tw, th, bg);
  _textStage->setTextSize(size);
  _textStage->setTextColor(color, bg);
  _textStage->setCursor(0, 0);
  _textStage->print(text);

  uint16_t *buf = _textStage->getFramebuffer();
  for (int16_t ty = 0; ty < th; ty++) {
    for (int16_t tx = 0; tx < tw; tx++) {
      uint16_t p = buf[ty * STAGE_W + tx];
      if (p != bg) {
        int16_t dx, dy;
        pointToPortrait((int16_t)(x + tx), (int16_t)(y + ty), dx, dy);
        _panel->drawPixel(dx, dy, p);
      }
    }
  }
}

void LandscapeGFX::flush() { _panel->flush(); }

void LandscapeGFX::touchToLandscape(int16_t touchX, int16_t touchY, int16_t &lx, int16_t &ly) const {
  if (_flipped) {
    lx = touchY;
    ly = _portraitW - 1 - touchX;
  } else {
    lx = _portraitH - 1 - touchY;
    ly = touchX;
  }
}
