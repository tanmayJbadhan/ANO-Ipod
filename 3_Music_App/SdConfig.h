#pragma once

// microSD breakout wiring - SPI mode, right header, matching the Adafruit
// "microSD SPI or SDIO" board's own SPI-mode pad names. Wire colours noted
// for tracing on the bench (positions counted from the bottom of the
// header; position 2 is BAT and is deliberately left empty):
//   green   CLK       -> GPIO38  (pos 1)
//   blue    3V        -> 3V3     (pos 3)
//   white   GND       -> GND     (pos 4)
//   grey    CMD (SI)  -> GPIO39  (pos 5)
//   purple  D0  (SO)  -> GPIO40  (pos 6)
//   black   D3  (CS)  -> GPIO41  (pos 7)
// D1/DAT(D2)/DET are left unconnected (SDIO-only / optional).
#define SD_SCK_PIN 38  // board pad: CLK
#define SD_MISO_PIN 40 // board pad: D0 (SO)
#define SD_MOSI_PIN 39 // board pad: CMD (SI)
#define SD_CS_PIN 41   // board pad: D3 (CS)

// SPI clock for the card. Lower it (e.g. 10000000) if songs ever skip or
// the card errors - the wires to the breakout are short jumpers.
#define SD_SPI_HZ 20000000
