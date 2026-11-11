#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// White Heron - a 3D flight over four procedural lands.
//
// Each year people light paper lanterns and let them go, each one a wish.
// The white heron gathers them on her way home across four lands - Maple
// Hollow, River Citadel, Snow Lake and the White Cliffs - riding the rings
// of wind that keep her aloft, never touching water or land.
//
// Each land is a 512 x 512 procedural heightmap (fractal and ridged noise,
// stamped trees, a town and fortress, baked shadows and light), drawn by a
// heightmap ray-caster on both cores with haze, mist, reflections and a
// banking horizon; the wind rings and lanterns are depth-tested, the heron
// a jointed mesh with fingered primaries.
//
// Controls:  LEFT / RIGHT or wheel  turn        UP / DOWN  climb / dive
//            SELECT  pause                      title: SELECT fly, UP back
namespace Crane {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
const uint16_t *frameBuffer(); // 228 x 140 RGB565, for screenshots
void benchAutopilot();         // USB bench command only: a pilot flies (toggles)

} // namespace Crane
