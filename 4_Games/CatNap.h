#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Cat Nap - mini mines, with cats. Cats are asleep under some of the
// cushions; lift the others without waking any. A number tells how many
// cats sleep on the cushions around it; mark a cat's cushion with a ball of
// yarn. Lift every empty cushion and all the cats nap on. The first cushion
// is never a cat. Three sizes of household; best times are remembered.
//
// Controls:  arrows  move              SELECT  lift the cushion (on a number
//            wheel   yarn on / off              with its cats marked: lift the rest)
//            hold DOWN  pause          title: LEFT / RIGHT household, UP back
//            touch: tap a cushion to lift it
namespace CatNap {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
bool upSleepAllowed();         // UP moves the paw mid-game
const uint16_t *frameBuffer(); // 228 x 140 RGB565, for screenshots

} // namespace CatNap
