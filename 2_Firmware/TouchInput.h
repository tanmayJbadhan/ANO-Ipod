#pragma once

#include <stdint.h>

// Wraps the FT3168 capacitive touch controller (Arduino_DriveBus library)
// into simple polled single-finger state, so the UI can be driven by touch
// as an alternative to the ANO encoder. Coordinates are in panel pixels
// (0..LCD_WIDTH-1, 0..LCD_HEIGHT-1).
class TouchInput {
public:
  bool begin();
  void poll(); // call every loop iteration

  bool ready() const { return _found; }
  bool pressed() const { return _pressed; }
  bool justPressed() const { return _pressed && !_prevPressed; }
  bool justReleased() const { return !_pressed && _prevPressed; }
  int16_t x() const { return _x; }
  int16_t y() const { return _y; }

private:
  bool _found = false;
  bool _pressed = false;
  bool _prevPressed = false;
  int16_t _x = 0;
  int16_t _y = 0;
};
