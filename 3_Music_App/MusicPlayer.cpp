#include "MusicPlayer.h"
#include "AudioConfig.h"
#include "SdConfig.h"
#include "Pcm5122.h"
#include "Library.h"
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <stdlib.h>
#include <new>

// Audio's constructor itself calls i2s_driver_install() unconditionally
// (not just setPinout()) - a plain global `static Audio audio;` would
// therefore claim I2S_NUM_0 at C++ static-init time, before setup() even
// runs. Constructing it lazily on first real play() is what actually
// keeps this class's I2S ownership deferred as intended.
//
// It must be built on ZEROED memory, hence calloc + placement new rather
// than a plain `new Audio()`. Audio::m_filterBuff (the IIR/EQ state) has
// no initialiser, so on ordinary heap it starts as garbage floats; any
// NaN in there propagates through every biquad forever and each sample
// casts back to 0, so the decoder runs perfectly while the output is
// digital silence. A file-scope `static Audio` happens to avoid this
// only because static storage is zero-initialised.
static Audio *audio = nullptr;

// Decoding runs on its own task pinned to core 0. The Arduino loop (core
// 1, per ARDUINO_RUNNING_CORE) also polls the encoder, touch panel,
// battery and clock and ends with delay(1) - servicing the decoder only
// once per pass through all that starves it, which is audible as
// stuttering and dropouts. Giving audio its own core lets it be fed
// continuously regardless of what the UI is doing.
static TaskHandle_t audioTask = nullptr;
static SemaphoreHandle_t audioMutex = nullptr;

// Guards structural changes to the Audio object (construction, opening a
// new stream, stopping) against the task that is calling loop() on it.
// isRunning() deliberately skips the lock: it reads a single bool, and
// the task can hold this mutex through a blocking i2s_write, which the
// once-a-second UI poll should never wait on.
static void audioTaskFn(void *) {
  for (;;) {
    if (xSemaphoreTake(audioMutex, portMAX_DELAY) == pdTRUE) {
      if (audio) audio->loop();
      xSemaphoreGive(audioMutex);
    }
    vTaskDelay(1);
  }
}

uint32_t MusicPlayer::currentTimeSec() {
  if (!_i2sReady || !audio) return 0;
  return audio->getAudioCurrentTime();
}

static Audio *createAudio() {
  void *mem = calloc(1, sizeof(Audio));
  if (!mem) return nullptr;
  Audio *a = new (mem) Audio();

  if (!audioMutex) audioMutex = xSemaphoreCreateMutex();
  if (audioMutex && !audioTask) {
    // Priority 2 keeps it above the Arduino loop (1) so decoding wins
    // when both are runnable; the MP3 decoder needs a generous stack.
    xTaskCreatePinnedToCore(audioTaskFn, "audio", 10240, nullptr, 2, &audioTask, 0);
  }
  return a;
}

// The SD card gets its own SPI peripheral. Arduino's global `SPI` object
// is FSPI (SPI2_HOST) on the S3, which is exactly the host the AMOLED's
// QSPI databus claims (QSPI_SPI_HOST in Arduino_ESP32QSPI.h) - sharing it
// meant the first screen redraw after mounting knocked the card off the
// bus ("ff_sd_status(): Check status failed"). HSPI maps to SPI3_HOST,
// which nothing else on this board uses.
static SPIClass sdSPI(HSPI);

bool MusicPlayer::_sdReady = false;
bool MusicPlayer::_i2sReady = false;
bool MusicPlayer::_paused = false;
uint8_t MusicPlayer::_volume = 17; // 0..21

// The decoder's own gain stays at unity; volume lives in the DAC.
static const uint8_t LIB_FULL_VOLUME = 21;
static const uint8_t VOL_CEILING_HALF_DB = 12; // volume 21 = -6 dB
static const uint8_t VOL_STEP_HALF_DB = 4;     // 2 dB per wheel click

// Soft-mute around track changes and resume: the DAC ramps down before the
// stream is cut and back up once new audio is flowing, instead of clicking.
static const uint32_t MUTE_RAMP_MS = 15;   // let the ramp-down finish
static const uint32_t UNMUTE_DELAY_MS = 120;
static uint32_t s_unmuteAt = 0;

static void muteForChange() {
  Pcm5122::setMute(true);
  delay(MUTE_RAMP_MS);
}

static void unmuteSoon() {
  s_unmuteAt = millis() + UNMUTE_DELAY_MS;
  if (s_unmuteAt == 0) s_unmuteAt = 1;
}
uint16_t MusicPlayer::_queue[MusicPlayer::MAX_QUEUE];
uint16_t MusicPlayer::_queueLen = 0;
uint16_t MusicPlayer::_pos = 0;
const char *MusicPlayer::_lastError = "";

// The Audio library logs its own diagnostics through this weak-linked
// callback (codec detected, bitrate, decode errors, etc.) - routing it to
// Serial is the easiest way to see why a file failed without adding our
// own parsing on top of it.
void audio_info(const char *info) {
  Serial.printf("[Audio] %s\n", info);
}

// NOTE: audio_process_i2s / audio_process_extern hooks were used to trace
// the silent-output bug and have been removed - audio_process_i2s in
// particular runs once per sample on the audio task, which is far too hot
// a path to leave instrumented.

bool MusicPlayer::beginSd() {
  sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  // Room for 10 open files: the library scan holds one directory per
  // folder level plus the song whose tags it is reading.
  _sdReady = SD.begin(SD_CS_PIN, sdSPI, 4000000, "/sd", 10);
  if (!_sdReady) {
    _lastError = "SD.begin failed - check wiring/card";
  }
  return _sdReady;
}

bool MusicPlayer::playSd(const char *path) {
  if (!_sdReady) {
    _lastError = "SD not mounted";
    return false;
  }
  if (!SD.exists(path)) {
    _lastError = "file not found on SD card";
    return false;
  }
  if (!_i2sReady) {
    audio = createAudio();
    if (!audio) { _lastError = "out of memory for decoder"; return false; }
    audio->setPinout(I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_DOUT_PIN);
    _i2sReady = true;
  }

  muteForChange();
  bool ok;
  if (audioMutex) xSemaphoreTake(audioMutex, portMAX_DELAY);
  ok = audio->connecttoFS(SD, path);
  // Volume goes on after the stream opens: the library re-derives its
  // gain table when a new stream starts.
  if (ok) audio->setVolume(LIB_FULL_VOLUME);
  if (audioMutex) xSemaphoreGive(audioMutex);

  unmuteSoon(); // even on failure, so the DAC isn't left silent
  if (!ok) {
    _lastError = "connecttoFS(SD) failed";
    return false;
  }
  return true;
}

void MusicPlayer::setQueue(const uint16_t *ids, uint16_t n) {
  if (n > MAX_QUEUE) n = MAX_QUEUE;
  memcpy(_queue, ids, n * sizeof(uint16_t));
  _queueLen = n;
  _pos = 0;
}

void MusicPlayer::clearQueue() {
  _queueLen = 0;
  _pos = 0;
}

uint16_t MusicPlayer::currentTrack() {
  return _queueLen ? _queue[_pos] : Library::NONE;
}

bool MusicPlayer::playIndex(uint16_t i) {
  if (_queueLen == 0) {
    _lastError = "nothing queued";
    return false;
  }
  _pos = i % _queueLen;
  _paused = false;
  return playSd(Library::track(_queue[_pos]).path);
}

bool MusicPlayer::next() {
  if (_queueLen == 0) return false;
  return playIndex((_pos + 1) % _queueLen);
}

bool MusicPlayer::previous() {
  if (_queueLen == 0) return false;
  return playIndex((_pos + _queueLen - 1) % _queueLen);
}

bool MusicPlayer::togglePause() {
  if (!_i2sReady || !audio) return false;
  if (!_paused) muteForChange(); // fade out rather than stop mid-waveform
  if (audioMutex) xSemaphoreTake(audioMutex, portMAX_DELAY);
  audio->pauseResume();
  if (audioMutex) xSemaphoreGive(audioMutex);
  _paused = !_paused;
  if (!_paused) unmuteSoon();
  return _paused;
}

uint32_t MusicPlayer::durationSec() {
  if (!_i2sReady || !audio) return 0;
  return audio->getAudioFileDuration();
}

bool MusicPlayer::seekTo(uint32_t sec) {
  // Duration is 0 whenever no file is open, so there's nothing to seek in.
  // (Paused is fine: the jump takes effect on resume.) The library's own
  // seek used to deadlock its decoder - see JIGGLYPUFF PATCH in setFilePos.
  uint32_t dur = durationSec();
  if (dur == 0) return false;
  if (sec >= dur) sec = dur - 1;
  Serial.printf("seek to %lu of %lu s\n", (unsigned long)sec, (unsigned long)dur);
  if (!_paused) muteForChange(); // hide the jump
  if (audioMutex) xSemaphoreTake(audioMutex, portMAX_DELAY);
  bool ok = audio->setAudioPlayPosition((uint16_t)sec);
  if (audioMutex) xSemaphoreGive(audioMutex);
  if (!_paused) unmuteSoon();
  return ok;
}

void MusicPlayer::stop() {
  if (!_i2sReady || !audio) return;
  if (audioMutex) xSemaphoreTake(audioMutex, portMAX_DELAY);
  audio->stopSong();
  if (audioMutex) xSemaphoreGive(audioMutex);
}

void MusicPlayer::setVolume(uint8_t vol) {
  if (vol > 21) vol = 21;
  _volume = vol;
  Pcm5122::setAttenuation(vol == 0 ? 255 : VOL_CEILING_HALF_DB + (21 - vol) * VOL_STEP_HALF_DB);
}

bool MusicPlayer::isRunning() {
  return _i2sReady && audio && audio->isRunning();
}

void MusicPlayer::loop() {
  if (s_unmuteAt && (int32_t)(millis() - s_unmuteAt) >= 0) {
    s_unmuteAt = 0;
    if (!_paused) Pcm5122::setMute(false);
  }
}
