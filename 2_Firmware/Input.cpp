#include "Input.h"

bool Input::begin() {
  // The ANO is mounted turned 90° counter-clockwise relative to the screen,
  // so each arrow's seesaw pin name is one quarter-turn off from where it
  // physically sits: the "UP" pin is the button on the left, "LEFT" is at
  // the bottom, "DOWN" on the right and "RIGHT" at the top. Map each pin
  // to the direction it actually points on the console.
  _keys[0] = {ANO_PIN_SELECT, Key::Select, false, false, 0, 0, 0};
  _keys[1] = {ANO_PIN_UP, Key::Left, false, false, 0, 0, 0};
  _keys[2] = {ANO_PIN_DOWN, Key::Right, false, false, 0, 0, 0};
  _keys[3] = {ANO_PIN_LEFT, Key::Down, false, false, 0, 0, 0};
  _keys[4] = {ANO_PIN_RIGHT, Key::Up, false, false, 0, 0, 0};

  _found = _ss.begin(ANO_SEESAW_ADDR);
  if (!_found) return false;

  for (auto &k : _keys) {
    _ss.pinMode(k.pin, INPUT_PULLUP);
  }
  _lastEncoderPos = _ss.getEncoderPosition();
  return true;
}

bool Input::rawHeld(Key k) {
  if (!_found) return false;
  for (auto &ks : _keys) {
    if (ks.key == k) return !_ss.digitalRead(ks.pin); // active-low
  }
  return false;
}

void Input::debugDrivePin(uint8_t pin, bool level) {
  if (!_found) return;
  _ss.pinMode(pin, OUTPUT);
  _ss.digitalWrite(pin, level ? HIGH : LOW);
}

void Input::debugHoldSelect(bool down) {
  if (!_found) return;
  if (down) {
    _ss.pinMode(ANO_PIN_SELECT, OUTPUT);
    _ss.digitalWrite(ANO_PIN_SELECT, LOW);
  } else {
    _ss.pinMode(ANO_PIN_SELECT, INPUT_PULLUP);
  }
}

void Input::push(Key key, bool pressed, bool repeat) {
  uint8_t next = (uint8_t)((_queueTail + 1) % QUEUE_SIZE);
  if (next == _queueHead) return; // queue full, drop oldest-pending event
  _queue[_queueTail] = {key, pressed, repeat};
  _queueTail = next;
}

void Input::poll() {
  if (!_found) return;

  unsigned long now = millis();

  for (auto &k : _keys) {
    bool raw = !_ss.digitalRead(k.pin); // seesaw buttons are active-low (pulled up)
    if (raw != k.raw) {
      k.raw = raw;
      k.lastChangeMs = now;
    }
    if (now - k.lastChangeMs >= DEBOUNCE_MS && k.debounced != k.raw) {
      k.debounced = k.raw;
      if (k.debounced) {
        k.pressedAtMs = now;
        k.lastRepeatMs = now;
        push(k.key, true, false);
        if (k.key == Key::Select) _selectLongFired = false;
      } else {
        push(k.key, false, false);
      }
    }
    if (k.debounced) {
      unsigned long held = now - k.pressedAtMs;
      if (held >= REPEAT_DELAY_MS && now - k.lastRepeatMs >= REPEAT_RATE_MS) {
        k.lastRepeatMs = now;
        push(k.key, true, true);
      }
      if (k.key == Key::Select && !_selectLongFired && held >= LONG_PRESS_MS) {
        _selectLongFired = true;
      }
    }
  }

  int32_t pos = _ss.getEncoderPosition();
  _encoderAccum += (pos - _lastEncoderPos);
  _lastEncoderPos = pos;
}

bool Input::next(InputEvent &out) {
  if (_queueHead == _queueTail) return false;
  out = _queue[_queueHead];
  _queueHead = (uint8_t)((_queueHead + 1) % QUEUE_SIZE);
  return true;
}

bool Input::held(Key k) const {
  for (auto &ks : _keys) {
    if (ks.key == k) return ks.debounced;
  }
  return false;
}

bool Input::selectLongPress() {
  if (_selectLongFired) {
    _selectLongFired = false; // edge-triggered, consume it
    return true;
  }
  return false;
}

int32_t Input::encoderDelta() {
  int32_t d = _encoderAccum;
  _encoderAccum = 0;
  return d;
}
