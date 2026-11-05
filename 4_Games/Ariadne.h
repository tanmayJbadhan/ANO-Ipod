#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"
#include "Landscape.h"
#include "Input.h"
#include "TouchInput.h"

// Ariadne's Thread - the myth of the Cretan labyrinth in ten levels, drawn
// like Greek black-figure pottery and Minoan frescoes.
//
//   Prologue  Ariadne's Loom      spin the thread (more thread for Act I)
//   Act I     Into the Labyrinth  four mazes as Theseus, unrolling the thread
//   Act II    The Heart           the Minotaur: leap his charges, strike
//   Act III   The Thread Home     four mazes as the ball of yarn, leading out
//   Finale    the crown of stars
//
// Every labyrinth is generated (see the comment above makeMaze): Wilson's
// uniform spanning trees, noise-guided braiding and a search over candidates
// scored by path length, dead ends, junction entropy and A* effort; the
// yarn levels grow a self-avoiding route by backbite moves on a
// Hamiltonian path. Lighting is torch falloff with line of sight; the
// Minotaur follows a diffusing scent.
//
// Controls:  arrows / wheel  move or steer    SELECT  leap, strike, rewind the yarn
//            pause: SELECT in the mazes, DOWN in the arena, hold DOWN elsewhere
//            title: LEFT / RIGHT level, UP back
namespace Ariadne {

bool enter(LandscapeGFX *gfx, Arduino_GFX *panel); // false if out of memory
void exit();
bool active();
void frame(Input &input, TouchInput &touch, int32_t wheel);
void key(const InputEvent &ev);
void pause();
bool wantsExit();
bool wantsAudioPower();
bool upSleepAllowed();         // UP moves mid-level
const uint16_t *frameBuffer(); // 228 x 140 RGB565, for screenshots
void benchUnlockAll();         // USB bench command only: every level playable (not saved)
void benchPlayAll();           // USB bench command only: a bot plays every level, results on serial

} // namespace Ariadne
