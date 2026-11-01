#pragma once

#include <Arduino_GFX_Library.h>

// The CO5300 panel driver explicitly does not support hardware rotation
// (see Arduino_CO5300::setRotation - it only flips scan-axis bits, never
// swaps width/height), and Arduino_GFX's own text/shape drawing doesn't
// rotate glyphs either. So: landscape mode is done entirely in software
// here. `_panel` stays a normal portrait (280w x 456h) Arduino_Canvas,
// completely unaware anything is rotated. This class exposes a 456x280
// logical landscape surface on top of it, transforming every draw call
// with a fixed 90-degree rotation, and composites TEXT via a small
// offscreen staging canvas (glyphs are drawn upright there, then blitted
// pixel-by-pixel into the rotated position - there's no way to make the
// library's font renderer emit rotated glyphs directly).
class LandscapeGFX {
public:
  LandscapeGFX(Arduino_GFX *portrait);
  bool begin(); // allocates the text-staging canvas; call once after portrait->begin()

  int16_t width() const { return _portraitH; }  // 456
  int16_t height() const { return _portraitW; } // 280

  // Turns the whole landscape image (and touch mapping) through 180°, for
  // when the board is mounted the other way up in its case.
  void setFlipped(bool flipped) { _flipped = flipped; }
  bool flipped() const { return _flipped; }

  // Landscape point -> index into the portrait canvas framebuffer, for code
  // that blends pixels directly instead of going through draw calls.
  void pointToPortrait(int16_t x, int16_t y, int16_t &px, int16_t &py) const;

  void fillScreen(uint16_t color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
  void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
  void fillArc(int16_t cx, int16_t cy, int16_t outerR, int16_t innerR, float startDeg, float endDeg, uint16_t color);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

  // Composited text - no custom font support, just the built-in font at
  // integer sizes, which is all this UI uses. bg is a real fill color
  // (not transparent) so callers should pass the surrounding background.
  void drawText(int16_t x, int16_t y, const char *text, uint8_t size, uint16_t color, uint16_t bg);
  int16_t textWidth(const char *text, uint8_t size) const { return (int16_t)strlen(text) * 6 * size; }

  void flush();

  // Touch coordinates come from the panel in physical/portrait space;
  // convert to logical landscape space before using them.
  void touchToLandscape(int16_t touchX, int16_t touchY, int16_t &lx, int16_t &ly) const;

  Arduino_GFX *portrait() { return _panel; }

private:
  Arduino_GFX *_panel;
  Arduino_Canvas *_textStage = nullptr;
  int16_t _portraitW; // 280
  int16_t _portraitH; // 456
  bool _flipped = false;
  static constexpr int16_t STAGE_W = 456; // generous - full row of text at size 2
  static constexpr int16_t STAGE_H = 40;

  void rectToPortrait(int16_t x, int16_t y, int16_t w, int16_t h, int16_t &px, int16_t &py, int16_t &pw, int16_t &ph) const;
};
