#pragma once

#include <stdint.h>
#include <stddef.h>

// Thin wrapper around schreibfaul1/ESP32-audioI2S's Audio class for playing
// MP3/WAV/M4A/FLAC files from the SD card out over the PCM5122. (The
// LittleFS path below is from early bring-up and is no longer used.) The
// racing game's synth borrows the same I2S peripheral - see RunAudio.
class MusicPlayer {
public:
  // Mounts LittleFS (formats on first boot if needed). Does NOT touch I2S
  // yet - the Audio library claims the peripheral lazily on first play()
  // so it never collides with AudioOutput's own (already-finished)
  // bring-up test.
  static bool begin();

  // path is an absolute LittleFS path, e.g. "/song1.mp3".
  static bool play(const char *path);

  // Brings up the SPI bus on SdConfig.h's pins and mounts the microSD
  // card. Independent of begin()/LittleFS - call once before playSd().
  static bool beginSd();
  static bool sdReady() { return _sdReady; }
  // path is an absolute path on the SD card's FAT filesystem, e.g. "/song1.mp3".
  static bool playSd(const char *path);

  // Writes the absolute path of the first playable track in the card's
  // root directory into out (leading slash included). Lets the test
  // screen work with whatever the user dropped on the card instead of
  // one hardcoded filename. Returns false if nothing playable is there.
  static bool firstTrackOnSd(char *out, size_t outLen);

  // --- playlist over the card's root directory ---
  static const size_t MAX_TRACKS = 64;
  static const size_t MAX_PATH = 128;

  // Builds the track list from the SD root. Returns how many it found.
  static size_t scanTracks();
  static size_t trackCount() { return _trackCount; }
  static const char *trackPath(size_t i);
  static size_t currentIndex() { return _current; }

  // Plays by playlist index, wrapping at both ends.
  static bool playIndex(size_t i);
  static bool next();
  static bool previous();

  // Toggles pause. Returns true when playback is paused afterwards.
  static bool togglePause();
  static bool isPaused() { return _paused; }

  // Seconds elapsed / total for the current track (0 if unknown).
  static uint32_t durationSec();

  static void stop();
  // True once the decoder has installed the I2S driver (it never uninstalls
  // it), so the racing game's synth knows to borrow it rather than install one.
  static bool i2sClaimed() { return _i2sReady; }
  static bool isRunning();

  // 0..21, applied in the PCM5122 (the decoder always runs at full level,
  // so no resolution is lost). 2 dB per step, 21 = -6 dB - the most wired
  // earbuds want from this line-level output before it starts to strain.
  static void setVolume(uint8_t vol);
  static uint8_t volume() { return _volume; }

  // Call from the main loop: lifts the soft mute used to hide the click of
  // a track change or resume once the new audio is flowing. (Decoding has
  // its own task; this only touches the DAC over I2C, which belongs to the
  // main loop.)
  static void loop();

  // TEMPORARY diagnostics for the stalling investigation.
  static uint32_t debugTaskTicks();
  static uint32_t currentTimeSec();
  static uint32_t freeHeap();

  static const char *lastError() { return _lastError; }

  // TEMPORARY diagnostics: peak absolute sample amplitude seen since the
  // last call (0..32767), and how many stereo frames have been handed to
  // I2S. Distinguishes a silent decoder from a silent output path.

private:
  static bool _fsReady;
  static bool _sdReady;
  static bool _i2sReady;
  static bool _paused;
  static uint8_t _volume;
  static size_t _trackCount;
  static size_t _current;
  static char _tracks[MAX_TRACKS][MAX_PATH];
  static const char *_lastError;
};
