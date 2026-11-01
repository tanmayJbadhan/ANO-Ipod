#pragma once

#include <Adafruit_seesaw.h>
#include <stdint.h>

// ANO Rotary Navigation Encoder (seesaw, PID 5740) wired on the shared
// touch/power I2C bus (IIC_SDA/IIC_SCL from pin_config.h).
#define ANO_SEESAW_ADDR 0x49
#define ANO_PIN_SELECT 1
#define ANO_PIN_UP 2
#define ANO_PIN_LEFT 3
#define ANO_PIN_DOWN 4
#define ANO_PIN_RIGHT 5

enum class Key : uint8_t { None, Up, Down, Left, Right, Select };

struct InputEvent {
  Key key;
  bool pressed; // true = just pressed, false = just released
  bool repeat;  // auto-repeat fire while held (pressed is always true for these)
};

class Input {
public:
  bool begin();
  bool ready() const { return _found; }

  // Call every loop iteration. Debounces, detects presses/releases/auto-repeat
  // and enqueues InputEvents for next() to drain.
  void poll();

  bool next(InputEvent &out);
  bool held(Key k) const;

  // Undebounced single read straight off the seesaw. Used by the wake
  // path, which runs before the main loop exists and so has no debounce
  // history to consult.
  bool rawHeld(Key k);

  // TEMPORARY: drive an arbitrary seesaw GPIO, to find out which pin (if
  // any) the board's green LED sits on.
  void debugDrivePin(uint8_t pin, bool level);

  // Edge-triggered: true exactly once per long-press of SELECT (used as the
  // universal "back to menu" gesture from any app).
  bool selectLongPress();

  int32_t encoderDelta(); // consumes accumulated scroll-wheel delta since last call

private:
  static constexpr uint8_t NUM_KEYS = 5;
  static constexpr uint16_t DEBOUNCE_MS = 25;
  static constexpr uint16_t REPEAT_DELAY_MS = 450;
  static constexpr uint16_t REPEAT_RATE_MS = 120;
  static constexpr uint16_t LONG_PRESS_MS = 700;

  struct KeyState {
    uint8_t pin;
    Key key;
    bool raw;
    bool debounced;
    unsigned long lastChangeMs;
    unsigned long pressedAtMs;
    unsigned long lastRepeatMs;
  };

  Adafruit_seesaw _ss;
  bool _found = false;
  KeyState _keys[NUM_KEYS];
  int32_t _lastEncoderPos = 0;
  int32_t _encoderAccum = 0;
  bool _selectLongFired = false;

  static constexpr uint8_t QUEUE_SIZE = 16;
  InputEvent _queue[QUEUE_SIZE];
  uint8_t _queueHead = 0;
  uint8_t _queueTail = 0;

  void push(Key key, bool pressed, bool repeat);
};
