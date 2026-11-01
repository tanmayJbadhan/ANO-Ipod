#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Lullaby Run - an 11-district pseudo-3D racer. Everything on screen is
// drawn from shapes each frame and the engine sound is synthesised (see
// RunAudio), so the whole game costs no image or audio assets.
//
// Opens on the district picker (with a demo race running behind it); from
// there the player races, pauses, and comes back. Beating a district in
// the top three unlocks the next. Best times, ducks found and the chosen
// car are kept in Preferences.
//
// Controls:  LEFT/RIGHT or the wheel  steer      DOWN    brake / switch car
//            SELECT                   boost      UP      pause (DOWN then quits) / back
//            touch: hold the left or right third to steer, tap the middle to boost
namespace LullabyRun {

// Allocates the game's buffers and starts the engine-sound stream. False
// if there isn't the memory (the caller stays where it was).
// The game draws straight to `panel`, bypassing the canvas.
bool enter(LandscapeGFX *gfx, Arduino_Canvas *canvas, Arduino_GFX *panel);
void exit();
bool active();

// One pass of the main loop: steps the simulation and draws a frame.
// `wheel` is the encoder delta since the last call.
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev); // presses and releases, no auto-repeat

// Pauses a race in progress (e.g. the screen going to sleep).
void pause();

// The player chose to leave the game for the Home screen.
bool wantsExit();

// The engine sound is running, so the DAC needs its 5V rail.
bool wantsAudioPower();

} // namespace LullabyRun
