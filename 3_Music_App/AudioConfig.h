#pragma once

// PCM5122 DAC I2S wiring - left header, confirmed against the physical
// build (wire colours noted for tracing on the bench):
//   orange  HAT CLK   (BCK)  -> GPIO47
//   blue    HAT LRCLK (LRCK) -> GPIO48
//   yellow  HAT DAC   (DIN)  -> GPIO21
//   red     HAT SDA         -> GPIO7   (shared IIC bus)
//   green   HAT SCL         -> GPIO6   (shared IIC bus)
//   lightred HAT 5V         -> VBUS
//   black   HAT GND         -> GND
//
// 47/48/21 ARE exposed on this board - an earlier revision of this file
// claimed otherwise based on a third-party pinout summary and moved these
// to 39/40/41. LilyGo's own pinout card shows all three on the left
// header, and 39/40/41 now belong to the SD card (see SdConfig.h), so
// leaving them here would collide.
//
// A Waveshare PCM5122 HAT is designed for the Raspberry Pi's I2S bus,
// which has no MCLK line - it derives its master clock internally via PLL
// from BCK, so only these 3 signals + power/GND are needed.
#define I2S_BCLK_PIN 47  // bit clock (CLK)
#define I2S_LRCLK_PIN 48 // word select / left-right clock (LRCLK)
#define I2S_DOUT_PIN 21  // data out, ESP32 -> DAC (DAC/DIN on the HAT)
