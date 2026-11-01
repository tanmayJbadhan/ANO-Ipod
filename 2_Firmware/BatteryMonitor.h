#pragma once

// Reads real battery voltage from the SY6970 charging IC (I2C, same bus as
// touch/ANO) and converts it to a percentage via a single-cell LiPo
// discharge-curve approximation - there's no fuel-gauge IC here, so this
// is "good enough for a status icon" accuracy, not lab-precise, and reads
// will be unstable/meaningless if no battery is plugged into the JST
// connector (the chip's own datasheet note, not a bug here).
class BatteryMonitor {
public:
  static bool begin();
  static void poll(); // call every loop(); throttles actual I2C reads internally
  static int percent() { return _lastPct; }
  static bool ready() { return _found; }

  // Real power-off: opens the SY6970's battery FET so the board stops
  // drawing from the cell entirely. Only takes effect on battery - with
  // USB attached the charger keeps the rail up, so this returns and the
  // caller should fall back to deep sleep. Does not return on success.
  static void powerOff();

  // Turns on the charger's OTG boost, which drives 5V onto VBUS from the
  // battery. That's what keeps the PCM5122 HAT alive once USB is
  // unplugged - the HAT needs a 5V supply and VBUS is otherwise dead on
  // battery. The chip ignores this while USB is supplying VBUS, so it is
  // safe to leave enabled. Returns false if the charger isn't present.
  static bool enable5VBoost(bool on);

  // VBUS (bus/input) voltage in millivolts - the rail the PCM5122 HAT
  // runs from. ~5000 means the boost is doing its job on battery; ~0
  // means VBUS is dead and the DAC is unpowered.
  static int busVoltageMv();
  static bool otgFault();

  // OTG boost and charging share a register on the SY6970 and OTG wins,
  // and while boosting the chip can no longer see USB at all. So the boost
  // runs only when actually needed: on battery, while music is playing
  // (the DAC is its only consumer). Call managePowerPath() often.
  static void setBoostWanted(bool want) { _boostWanted = want; }
  static bool managePowerPath(); // returns true when on USB
  static bool boostOn() { return _boostOn; }

  // While boosting, a computer is spotted automatically, but a wall
  // charger isn't. This briefly stops the boost to check - call it at a
  // moment a short audio gap won't be noticed, like a track change.
  // The DAC will need reconfiguring afterwards.
  static bool probeForUsb();
  static bool onUsb() { return _onUsb; }
  static bool isCharging();

private:
  static bool _found;
  static bool _onUsb;
  static bool _boostOn;
  static bool _boostWanted;
  static int _lastPct;
  static unsigned long _lastReadMs;
  static int voltageToPercent(float mv);
};
