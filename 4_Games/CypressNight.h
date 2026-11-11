#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Cypress Night - a maze chase in a Persian garden at night. A writer in
// round glasses gathers the pages of his manuscript from the paths between
// rows of cypress trees, while four owls hunt him from their roosts in the
// corners. A lantern dazzles the owls for a few seconds: they turn pale and
// flee, and catching one sends it back to its roost. Clear every page to
// reach the next, faster night. Drawn from shapes, sounds synthesised (the
// racing game's RunAudio), best score kept in Preferences.
//
// Controls:  arrows (or the wheel: turn left / right)  walk
//            SELECT  start / pause    DOWN (paused) quit the run
//            UP      back (on the title screen)
//            touch: tap the side of the writer to head that way
namespace CypressNight {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
// The last frame drawn (228 x 140 RGB565, landscape), for screenshots.
const uint16_t *frameBuffer();

} // namespace CypressNight
