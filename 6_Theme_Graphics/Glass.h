#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"

// "Liquid glass" rendering in the spirit of Apple's design language: a
// soft, vivid background of blended colour blobs, with translucent panels
// floating over it. The panels aren't painted a flat colour - every pixel
// is mixed with the background pixel directly behind it, so they read as
// see-through, with a bright rim and a sheen across the top like real
// glass catching the light.
//
// The background is computed once at boot into a portrait-layout buffer in
// PSRAM. Clearing the screen is then a memcpy, and panels blend against
// that buffer rather than against whatever is already on screen.
namespace Glass {

bool begin(LandscapeGFX *gfx, Arduino_Canvas *canvas);

// Repaints the whole screen with the background.
void background();

// Switches to the Home background (Jigglypuff faded in behind the glass)
// or back to the plain one. Panels blend against whichever is active.
void useHomeTheme(bool on);

// A translucent rounded panel. `tint` is mixed in at `strength` (0..255);
// `lift` > 0 brightens the rim and sheen for a focused/raised look.
void panel(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
           uint16_t tint, uint8_t strength, uint8_t lift = 0);

// A translucent circle (round buttons).
void disc(int16_t cx, int16_t cy, int16_t r, uint16_t tint, uint8_t strength, uint8_t lift = 0);

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint16_t blend(uint16_t a, uint16_t b, uint8_t t); // t=0 -> a, t=255 -> b

// Colour key drawText treats as transparent, so text sits on the glass
// without a box behind it. Never use it as a real colour.
static const uint16_t CLEAR = 0x0821;

} // namespace Glass
