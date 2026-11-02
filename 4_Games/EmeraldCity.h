#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Raise the Emerald City - a tower-stacking game from The Wonderful Wizard
// of Oz. The cyclone scattered the city; the Wizard runs a brass crane, and
// every floor swinging from its hook has to be dropped square onto the one
// below. The better it lines up, the more people move in; a sloppy drop
// makes the tower lean and sway, and one that misses the edge falls (three
// hearts a land). Six lands, each with its own floors and roof:
//
//   1 Munchkin Country      blue domed houses
//   2 The Scarecrow's Field the Witch's crows land on the swinging floor
//   3 The Great Forest      heavy tin floors: slow swing, more sway
//   4 The Poppy Field       the crane grows drowsy, then wakes
//   5 The Winkie Country    the west wind pushes the crane
//   6 The Emerald City      the great green tower, at night
//
// Everything in play is drawn from shapes; the title and story cards show
// W. W. Denslow's 1900 illustrations (public domain, see Denslow.h).
//
// Controls:  SELECT  drop the floor      wheel  nudge the crane's trolley
//            DOWN    pause               title: LEFT / RIGHT choose a land, UP back
//            touch: tap to drop
namespace EmeraldCity {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
const uint16_t *frameBuffer(); // 228 x 140 RGB565, for screenshots
void benchUnlockAll();         // USB bench command only: every land playable (not saved)
void benchAutoplay();          // USB bench command only: toggle perfect drops

} // namespace EmeraldCity
