#include "Pcm5122.h"
#include <Wire.h>

bool Pcm5122::_found = false;
uint8_t Pcm5122::_addr = 0;
uint8_t Pcm5122::_atten = 40; // -20 dB until told otherwise - never start at full blast in earbuds
bool Pcm5122::_muted = false;

// 0x3D/0x3E: 0x30 is 0 dB, each step above it is another -0.5 dB, 0xFF mute.
static uint8_t volumeReg(uint8_t atten) {
  return atten >= 0xFF - 0x30 ? 0xFF : (uint8_t)(0x30 + atten);
}

void Pcm5122::setAttenuation(uint8_t atten) {
  _atten = atten;
  if (!_found) return;
  writeReg(0x00, 0x00);
  writeReg(0x3D, volumeReg(_atten));
  writeReg(0x3E, volumeReg(_atten));
}

void Pcm5122::setMute(bool on) {
  _muted = on;
  if (!_found) return;
  writeReg(0x00, 0x00);
  writeReg(0x03, on ? 0x11 : 0x00);
}

bool Pcm5122::writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

uint8_t Pcm5122::readReg(uint8_t reg) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF; // keep bus, repeated start
  if (Wire.requestFrom((int)_addr, 1) != 1) return 0xFF;
  return Wire.read();
}

uint8_t Pcm5122::powerState() {
  writeReg(0x00, 0x00); // make sure we're on page 0 before reading 0x76
  return readReg(0x76) & 0x0F;
}

bool Pcm5122::isResponding() {
  // If the chip was unpowered when begin() first ran, we never learned its
  // address - check both strap options, or a DAC that was merely late to
  // power up would never be recovered.
  const uint8_t candidates[] = {0x4C, 0x4D};
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t a = _found ? _addr : candidates[i];
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return true;
    if (_found) break;
  }
  return false;
}

uint8_t Pcm5122::clockStatus() {
  writeReg(0x00, 0x00);
  return readReg(0x5F);
}

uint8_t Pcm5122::analogMuteMonitor() {
  writeReg(0x00, 0x00);
  return readReg(0x6C);
}

uint8_t Pcm5122::autoMuteFlags() {
  writeReg(0x00, 0x00);
  return readReg(0x78);
}

uint8_t Pcm5122::muteReg() {
  writeReg(0x00, 0x00);
  return readReg(0x03);
}

bool Pcm5122::begin() {
  const uint8_t candidates[] = {0x4C, 0x4D};
  _found = false;
  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) {
      _addr = candidates[i];
      _found = true;
      break;
    }
  }
  if (!_found) return false;

  // Stay muted while we configure clocking, matching how this chip is
  // meant to be brought up: mixer path -> exit standby/powerdown -> PLL
  // enabled, referenced off BCK, and (crucially) actually selected as the
  // DAC's clock SOURCE -> ignore the permanently-missing MCLK -> only then
  // unmute. Earlier version set the PLL reference but never pointed the
  // DAC's clock source register at the PLL output, so it had no valid
  // sample clock at all and produced silence despite every I2C write
  // succeeding.
  bool ok = true;
  ok &= writeReg(0x00, 0x00); // page 0
  ok &= writeReg(0x03, 0x11); // mute both channels while configuring
  ok &= writeReg(0x2A, 0x11); // mixer: L->ch1, R->ch2
  ok &= writeReg(0x02, 0x00); // exit standby/powerdown -> play
  ok &= writeReg(0x04, 0x01); // PLL enable
  ok &= writeReg(0x0D, 0x10); // PLL reference = BCK
  ok &= writeReg(0x0E, 0x10); // DAC clock source = PLL output
  ok &= writeReg(0x25, 0x08); // ignore missing MCLK

  // Volume is ours to set (see setAttenuation) - re-apply the remembered
  // level, so a codec that lost power and is being reconfigured comes back
  // at the listening level, not 0 dB. Auto-mute off, so a silence detector
  // can't be the reason nothing comes out the jack.
  ok &= writeReg(0x3D, volumeReg(_atten)); // digital volume left
  ok &= writeReg(0x3E, volumeReg(_atten)); // digital volume right
  ok &= writeReg(0x41, 0x00); // auto-mute off (both channels)
  ok &= writeReg(0x3B, 0x00); // auto-mute delay = shortest/disabled

  // Page 1 carries the analog block; force 0 dB analog gain too.
  ok &= writeReg(0x00, 0x01); // select page 1
  ok &= writeReg(0x02, 0x00); // analog gain = 0 dB, both channels
  ok &= writeReg(0x00, 0x00); // back to page 0

  ok &= writeReg(0x03, _muted ? 0x11 : 0x00); // unmute (unless we're mid track change)
  return ok;
}
