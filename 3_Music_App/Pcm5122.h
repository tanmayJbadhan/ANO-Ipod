#pragma once

#include <stdint.h>

// Register-level bring-up for the PCM5122 codec on the Waveshare DAC HAT.
//
// Unlike simpler I2S DACs (e.g. PCM5102A), the PCM5122 boots powered down
// and muted and stays that way until told otherwise over I2C - correct I2S
// wiring alone produces silence. This uses the same shared I2C bus as
// touch/ANO/SY6970 (IIC_SDA/IIC_SCL, already brought up via Wire.begin()
// before this runs).
class Pcm5122 {
public:
  // Probes the chip at its two possible strap addresses (0x4C/0x4D),
  // then powers it up, unmutes it, and sets it to derive its PLL from BCK
  // (this HAT has no MCLK line). Returns false if no chip acked either
  // address - check the SDA/SCL wiring in that case.
  static bool begin();
  static bool found() { return _found; }

  // Volume is applied here, in the DAC's own digital attenuator (0.5 dB
  // steps, done at high internal resolution), rather than by scaling
  // 16-bit samples on the ESP32 - which throws away resolution exactly
  // when listening quietly. `atten` is in 0.5 dB steps below 0 dB; 255 is
  // mute. Remembered, and re-applied whenever begin() reconfigures the chip.
  static void setAttenuation(uint8_t atten);

  // Soft mute: the DAC ramps the level down/up instead of cutting, which is
  // what keeps track changes and pause from clicking. Also remembered.
  static void setMute(bool on);
  static uint8_t address() { return _addr; }

  // Reg 0x76 bits[3:0]: live power/DSP state machine value - the
  // authoritative way to tell "clocks look valid and audio is flowing"
  // (5 = RUN_PLAYING) apart from "chip is waiting on a clock it never
  // sees" (0/1/8) without needing to hear anything.
  static uint8_t powerState();

  // Does the chip still ACK on the bus? It drops off entirely when VBUS
  // dies, and comes back in its reset state (powered down and muted) -
  // so this is how we notice we have to configure it all over again.
  static bool isResponding();

  // Status registers worth watching when audio goes quiet while the
  // decoder is still running: 0x5F clock status, 0x6C analog-mute
  // monitor, 0x78 auto-mute flags, 0x03 the mute register we wrote.
  static uint8_t clockStatus();
  static uint8_t analogMuteMonitor();
  static uint8_t autoMuteFlags();
  static uint8_t muteReg();

private:
  static bool _found;
  static uint8_t _addr;
  static uint8_t _atten;
  static bool _muted;
  static bool writeReg(uint8_t reg, uint8_t val);
  static uint8_t readReg(uint8_t reg);
};
