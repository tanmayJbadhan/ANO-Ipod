#pragma once

#include <stdint.h>

// Procedural engine sound for the racing game, synthesised sample by
// sample and streamed straight to the PCM5122 - no audio files.
//   ICE: saw + sub-square at the firing frequency, pulsed, soft-clipped and
//        low-passed with a cutoff that opens with revs; a turbo whistle on boost.
//   EV:  motor whine rising with speed, a harmonic, a low hum and an inverter tone.
//   Both: band-passed noise for tyres and wind, plus one-shot effects.
//
// Music and the game never sound at once: start() stops the music player
// and borrows its I2S driver (or installs one if music never played).
namespace RunAudio {

bool start();
void stop();
bool running();

struct Params {
  float speed = 0;   // 0..~1.4, fraction of top speed
  float rpm = 0;     // ICE only
  bool ice = false;
  bool throttle = false;
  bool boosting = false;
  bool braking = false;
  bool paused = false;
};
void set(const Params &p);

// A music source mixed under everything else: called on the synth's core once per
// block to fill n mono samples (44.1 kHz). nullptr for none. Set it before start().
void setMusic(void (*fn)(float *out, int n));

enum Fx : uint8_t { Pop, PopSoft, Bonk, Squeak, Ding, DingHigh, DingTop, Thunder, Hoot, Tick,
                    Honk, Hop, Gull, Tock };
void fx(Fx f);

// Microseconds spent making sound since the last call (not counting the
// wait for the DAC to take it) - for bench CPU reports.
uint32_t takeBusyUs();
// Bytes of the synth task's stack never used (0 when it isn't running) - for bench reports.
uint32_t stackLeft();

} // namespace RunAudio
