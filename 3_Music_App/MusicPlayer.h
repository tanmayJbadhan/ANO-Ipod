#pragma once

#include <stdint.h>
#include <stddef.h>

// Thin wrapper around schreibfaul1/ESP32-audioI2S's Audio class for playing
// MP3/WAV/M4A/FLAC files from the SD card out over the PCM5122. The racing
// game's synth borrows the same I2S peripheral - see RunAudio.
//
// What plays is a queue of Library song ids: picking a song from an artist,
// album or playlist queues that whole list and starts at the chosen song.
class MusicPlayer {
public:
  // Brings up the SPI bus on SdConfig.h's pins and mounts the microSD card.
  static bool beginSd();
  static bool sdReady() { return _sdReady; }
  // path is an absolute path on the SD card's FAT filesystem, e.g. "/song1.mp3".
  static bool playSd(const char *path);

  // --- play queue ---
  static const uint16_t MAX_QUEUE = 2000;

  static void setQueue(const uint16_t *ids, uint16_t n);
  static void clearQueue(); // after a library rescan, when the ids go stale
  static uint16_t queueLength() { return _queueLen; }
  static uint16_t queuePos() { return _pos; }
  static uint16_t currentTrack(); // Library id, or Library::NONE

  // Plays by queue position, wrapping at both ends.
  static bool playIndex(uint16_t i);
  static bool next();
  static bool previous();

  // Toggles pause. Returns true when playback is paused afterwards.
  static bool togglePause();
  static bool isPaused() { return _paused; }

  // Seconds elapsed / total for the current track (0 if unknown).
  static uint32_t currentTimeSec();
  static uint32_t durationSec();
  // Jumps to a point in the current track.
  static bool seekTo(uint32_t sec);

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

  static const char *lastError() { return _lastError; }

private:
  static bool _sdReady;
  static bool _i2sReady;
  static bool _paused;
  static uint8_t _volume;
  static uint16_t _queue[MAX_QUEUE];
  static uint16_t _queueLen;
  static uint16_t _pos;
  static const char *_lastError;
};
