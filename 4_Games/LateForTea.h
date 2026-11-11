#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Late for Tea - a 3D dash behind the White Rabbit, down winding paths
// through ten places from Wonderland, to the Mad Hatter and the March
// Hare's tea party before four o'clock. Tortoises on skateboards race him
// for the Linzer cookies (if one gets there first, they eat them all),
// Alice chases him (catch him three times and she shares the cookies out
// to everyone), geese block the way, candy and Easter eggs speed him up,
// and seagulls swoop down: a yellow one carries him ahead, a purple one
// back. Win all ten and the cookies are his - and he shares them anyway.
//
// Everything in the race is drawn from shapes, with Perlin noise for the
// clouds and far hills; the title and story cards show John Tenniel's 1865
// illustrations (public domain, see Tenniel.h).
//
// Controls:  LEFT / RIGHT (or the wheel)  change path   SELECT  hop
//            hold UP  sprint (uses his puff)            DOWN    pause
//            title: LEFT / RIGHT choose a place, SELECT run, UP back
//            touch: left / right half = change path, top = hop
namespace LateForTea {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
const uint16_t *frameBuffer(); // 228 x 140 RGB565, for screenshots
void benchUnlockAll();         // USB bench command only: every place playable (not saved)
void benchLineup();            // USB bench command only: toggle a close-up of the cast

} // namespace LateForTea
