#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Gaslight & Griffins - a detective RPG in a gaslamp world of partner
// creatures and old myths. See 4_Games/Gaslight/DESIGN.md.
//
// Controls:  arrows walk / move the cursor     wheel  scroll choices
//            SELECT (centre) talk, confirm      DOWN   back (in menus)
//            on the road: SELECT with nothing in front opens the casebook menu
//            hold SELECT ~1 s: back to Home (the console's own gesture)
namespace Gaslight {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
const uint16_t *frameBuffer(); // 456 x 280 RGB565, for screenshots
void benchStats();             // USB bench: frame timings every 2 s (toggles)
void benchSkip();              // USB bench: skip ahead through the story (debug)

} // namespace Gaslight
