#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Lamplighter - an endless gravity-hopping flight through space.
//
// The Hush, a tide of quiet dark, is putting the stars to sleep. Wren keeps
// the lamps of the sky; her little ship, the Moth, has no fuel left - only
// the pull of worlds. Catch a world's gravity, swing around it, let go to
// fly on, and every world she circles lights its lamp again. Rocks drift
// in the gaps, moons circle some worlds, and the Hush is always behind.
// Five regions repeat, faster each time round.
//
// Controls:  hold any arrow  catch the nearest world's pull and orbit it
//            (let go to fly off along the orbit)
//            wheel  steer a little; while orbiting, move the orbit in / out
//            SELECT pause            title: SELECT fly, UP back
//            touch: hold the screen to orbit
namespace Lamplighter {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
bool upSleepAllowed();         // UP is an orbit button mid-flight
const uint16_t *frameBuffer(); // 228 x 140 RGB565, for screenshots
void benchAutopilot();         // USB bench command only: a pilot flies (toggles)

} // namespace Lamplighter
