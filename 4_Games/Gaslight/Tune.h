#pragma once
// Gaslight & Griffins - the music tracker (Tune.cpp). Pieces are in GLMusic.h.
namespace Gaslight {
namespace Tune {
void play(int song);          // a Songs::SongId; -1 for silence. Changes fade over about half a second.
int playing();
void setVolume(float v);      // 0..1, under the sound effects
void render(float *out, int n); // n mono samples at 44.1 kHz, added under the effects by RunAudio
} // namespace Tune
} // namespace Gaslight
