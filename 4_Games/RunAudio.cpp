#include "RunAudio.h"
#include "AudioConfig.h"
#include "MusicPlayer.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

namespace RunAudio {

static const uint32_t RATE = 44100;
static const int BLOCK = 256;          // frames per i2s_write (~5.8ms)
static const float MASTER = 0.9f;
static const float TWO_PI_F = 6.2831853f;

static TaskHandle_t s_task = nullptr;
static volatile bool s_run = false;     // accepting sound
static volatile bool s_stopReq = false; // fading out, then the task ends
static volatile bool s_done = true;
static bool s_ownDriver = false;
static Params s_p;

// One-shot events, single producer (game loop) / single consumer (synth task).
static volatile uint8_t s_evq[16];
static volatile uint8_t s_evHead = 0, s_evTail = 0;

void set(const Params &p) { s_p = p; }

void fx(Fx f) {
  if (!s_run) return;
  uint8_t n = (s_evHead + 1) & 15;
  if (n == s_evTail) return; // full: drop it
  s_evq[s_evHead] = f;
  s_evHead = n;
}

bool running() { return s_run; }

static volatile uint32_t s_busyUs = 0;
uint32_t takeBusyUs() {
  uint32_t b = s_busyUs;
  s_busyUs = 0;
  return b;
}

// ---------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------

static float s_sin[1024];

static inline float sinp(uint32_t ph) { return s_sin[ph >> 22]; }
static inline float sawp(uint32_t ph) { return (float)ph * (1.0f / 2147483648.0f) - 1.0f; }
static inline float sqrp(uint32_t ph) { return ph < 0x80000000u ? 1.0f : -1.0f; }
static inline float trip(uint32_t ph) {
  float s = sawp(ph);
  return 2.0f * fabsf(s) - 1.0f;
}
static inline uint32_t inc(float hz) { return (uint32_t)(hz * (4294967296.0f / RATE)); }

static uint32_t s_rng = 0x1234567;
static inline float noise() {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return (float)(int32_t)s_rng * (1.0f / 2147483648.0f);
}

// Chamberlin state-variable filter: low- and band-pass outputs.
struct Svf {
  float lp = 0, bp = 0, f = 0.1f, q = 1.0f;
  void tune(float hz, float Q) {
    if (hz > RATE / 6.5f) hz = RATE / 6.5f;
    f = 2.0f * sinf(3.14159265f * hz / RATE);
    q = 1.0f / Q;
  }
  inline void run(float in) {
    lp += f * bp;
    float hp = in - lp - q * bp;
    bp += f * hp;
  }
};

// Values the browser version eased with setTargetAtTime: each block moves
// a fraction of the way to its target.
struct Smooth {
  float v = 0, target = 0;
  void step(float k) { v += (target - v) * k; }
};

struct Voice {
  bool on = false, burst = false, tri = false;
  int32_t delay = 0, left = 0;
  float env = 0, decay = 1, freq = 0, sweep = 1;
  uint32_t ph = 0;
  Svf lp;
};
static Voice s_voices[8];

static void startVoice(bool burst, float f0, float f1, float dur, float vol, int delayMs = 0, bool tri = false) {
  for (Voice &v : s_voices) {
    if (v.on) continue;
    v.on = true;
    v.burst = burst;
    v.tri = tri;
    v.delay = (int32_t)(delayMs * RATE / 1000);
    v.left = (int32_t)(dur * RATE);
    v.env = vol;
    v.decay = expf(logf(0.001f) / (dur * RATE));
    v.freq = f0;
    v.sweep = expf(logf(f1 / f0) / (dur * RATE));
    v.ph = 0;
    v.lp = Svf();
    v.lp.tune(f0, 0.707f);
    return;
  }
}

static void handleEvent(uint8_t e) {
  switch (e) {
    case Pop: startVoice(true, 700, 700, 0.07f, 0.5f); break;
    case PopSoft: startVoice(true, 700, 700, 0.07f, 0.25f); break;
    case Bonk: startVoice(false, 220, 90, 0.15f, 0.3f, 0, true); break;
    case Squeak:
      startVoice(false, 800, 1800, 0.09f, 0.18f);
      startVoice(false, 1800, 500, 0.22f, 0.18f, 90);
      break;
    case Ding:
    case DingHigh:
    case DingTop: {
      float m = e == Ding ? 1.0f : e == DingHigh ? 1.5f : 2.0f;
      startVoice(false, 880 * m, 880 * m, 0.25f, 0.12f);
      startVoice(false, 1320 * m, 1320 * m, 0.3f, 0.1f, 80);
      break;
    }
    case Thunder: startVoice(true, 180, 180, 1.8f, 0.6f); break;
    case Hoot: // an owl: two soft falling "hoo"s
      startVoice(false, 440, 400, 0.30f, 0.16f, 0, true);
      startVoice(false, 410, 360, 0.45f, 0.16f, 380, true);
      break;
    case Tick: startVoice(false, 1700, 1500, 0.035f, 0.05f, 0, true); break;
    case Honk: // a goose: two blaring nasal notes
      startVoice(false, 330, 300, 0.16f, 0.22f);
      startVoice(false, 360, 280, 0.22f, 0.22f, 190);
      break;
    case Hop: startVoice(false, 420, 900, 0.12f, 0.12f, 0, true); break;
    case Gull: // a seagull's cry, falling
      startVoice(false, 1500, 900, 0.18f, 0.12f);
      startVoice(false, 1400, 800, 0.26f, 0.10f, 170);
      break;
    case Tock: startVoice(false, 900, 850, 0.05f, 0.09f, 0, true); break;
  }
}

// ---------------------------------------------------------------------------
// The synth task
// ---------------------------------------------------------------------------

static void synthTask(void *) {
  static int16_t buf[BLOCK * 2];
  uint32_t p1 = 0, p2 = 0, p3 = 0, plfo = 0, ptw = 0, pe1 = 0, pe2 = 0, pe3 = 0, pe4 = 0;
  Svf tyre, iceLp;
  float hpPrevIn = 0, hpOut = 0;
  float master = 0;

  Smooth ng, nbpF, iceG, fire, ilpF, isumG, twG, twF, evG, e1f, e2f, e3f, e4f, e1g, e2g, e3g, e4g;
  nbpF.v = 300;
  fire.v = 26;
  ilpF.v = 300;
  e1f.v = 80;
  e3f.v = 40;
  e4f.v = 700;
  const float blockSec = (float)BLOCK / RATE;
  const float kNorm = 1.0f - expf(-blockSec / 0.04f); // the browser's default 40ms
  const float kFast = 1.0f - expf(-blockSec / 0.02f);

  for (;;) {
    uint32_t busyFrom = micros();
    if (s_stopReq && master < 0.0005f) break;
    while (s_evTail != s_evHead) {
      handleEvent(s_evq[s_evTail]);
      s_evTail = (s_evTail + 1) & 15;
    }

    // --- targets, exactly as the preview's audio.update() ---
    const Params p = s_p;
    float sp = p.speed < 0 ? 0 : p.speed > 1.4f ? 1.4f : p.speed;
    bool thr = p.throttle && !p.paused;
    ng.target = p.paused ? 0 : 0.015f + sp * sp * 0.16f;
    nbpF.target = 300 + sp * 1500;
    if (p.paused) {
      iceG.target = 0;
      evG.target = 0;
      twG.target = 0;
    } else if (p.ice) {
      evG.target = 0;
      iceG.target = 0.42f;
      float f = fmaxf(800, p.rpm) / 60 * 2;
      fire.target = f;
      ilpF.target = 220 + (thr ? 1 : 0.35f) * (p.rpm / 7500) * 2600;
      isumG.target = thr ? 0.55f : 0.28f;
      twG.target = p.boosting ? 0.04f : 0;
      twF.target = 2200 + sp * 2400;
    } else {
      iceG.target = 0;
      twG.target = 0;
      evG.target = 0.5f;
      float f = 80 + sp * 1600 * (p.boosting ? 1.12f : 1);
      e1f.target = f;
      e2f.target = f * 2.01f;
      e3f.target = 40 + sp * 110;
      e4f.target = 700 + sp * 2600;
      e1g.target = 0.08f + (thr ? 0.09f : 0.02f) + (p.boosting ? 0.05f : 0);
      e2g.target = 0.03f + sp * 0.04f;
      e3g.target = thr ? 0.1f : 0.04f;
      e4g.target = (thr ? 0.03f : 0.008f) * (1 - fminf(1, sp) * 0.6f) + (p.boosting ? 0.03f : 0) +
                   (p.braking ? 0.02f : 0);
    }
    Smooth *norm[] = {&ng, &nbpF, &iceG, &ilpF, &isumG, &twG, &twF, &evG, &e1f, &e2f, &e3f, &e4f, &e1g, &e2g, &e3g, &e4g};
    for (Smooth *s : norm) s->step(kNorm);
    fire.step(kFast);

    tyre.tune(nbpF.v, 0.7f);
    iceLp.tune(ilpF.v, 3.0f);
    uint32_t i1 = inc(fire.v), i2 = inc(fire.v / 2), i3 = inc(fire.v * 2), ilfo = inc(fire.v / 2);
    uint32_t itw = inc(twF.v), ie1 = inc(e1f.v), ie2 = inc(e2f.v), ie3 = inc(e3f.v), ie4 = inc(e4f.v);
    const float hpA = 1.0f / (1.0f + TWO_PI_F * 50.0f / RATE); // 50 Hz high-pass

    for (int n = 0; n < BLOCK; n++) {
      // Tyres and wind.
      tyre.run(noise());
      float out = tyre.bp * ng.v;

      // ICE.
      if (iceG.v > 0.001f) {
        float sum = (sawp(p1) + 0.45f * sqrp(p2) + 0.16f * sawp(p3)) * (isumG.v + 0.3f * sinp(plfo));
        if (sum > 1) sum = 1;
        if (sum < -1) sum = -1;
        float shaped = tanhf(sum * 2.6f);
        iceLp.run(shaped);
        out += iceLp.lp * iceG.v;
      }
      if (twG.v > 0.0005f) out += sinp(ptw) * twG.v;
      p1 += i1; p2 += i2; p3 += i3; plfo += ilfo; ptw += itw;

      // EV.
      if (evG.v > 0.001f) {
        float e = sinp(pe1) * e1g.v + sinp(pe2) * e2g.v + trip(pe3) * e3g.v + sinp(pe4) * e4g.v;
        hpOut = hpA * (hpOut + e - hpPrevIn);
        hpPrevIn = e;
        out += hpOut * evG.v;
      }
      pe1 += ie1; pe2 += ie2; pe3 += ie3; pe4 += ie4;

      // One-shots.
      for (Voice &v : s_voices) {
        if (!v.on) continue;
        if (v.delay > 0) { v.delay--; continue; }
        float s;
        if (v.burst) {
          v.lp.run(noise());
          s = v.lp.lp;
        } else {
          s = v.tri ? trip(v.ph) : sinp(v.ph);
          v.ph += inc(v.freq);
          v.freq *= v.sweep;
        }
        out += s * v.env;
        v.env *= v.decay;
        if (--v.left <= 0) v.on = false;
      }

      // Fade in on start and out on stop, so neither clicks.
      master += ((s_stopReq ? 0 : MASTER) - master) * 0.0005f;
      float o = out * master * 32767.0f;
      if (o > 32767) o = 32767;
      if (o < -32767) o = -32767;
      buf[n * 2] = buf[n * 2 + 1] = (int16_t)o;
    }

    size_t written;
    s_busyUs += micros() - busyFrom;
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
  }

  // A short silent tail flushes the DMA so nothing stale repeats.
  memset(buf, 0, sizeof(buf));
  size_t written;
  for (int i = 0; i < 8; i++) i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
  s_done = true;
  vTaskDelete(nullptr);
}

static bool installDriver() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_LRCLK_PIN;
  pins.data_out_num = I2S_DOUT_PIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  return true;
}

bool start() {
  if (s_run) return true;
  if (!s_done) return false; // previous task still draining

  static bool tables = false;
  if (!tables) {
    for (int i = 0; i < 1024; i++) s_sin[i] = sinf(i * TWO_PI_F / 1024);
    tables = true;
  }

  if (MusicPlayer::i2sClaimed()) {
    // The decoder's driver stays installed after stopSong(); just retune it.
    MusicPlayer::stop();
    s_ownDriver = false;
    i2s_set_sample_rates(I2S_NUM_0, RATE);
  } else {
    if (!installDriver()) return false;
    s_ownDriver = true;
  }

  for (Voice &v : s_voices) v.on = false;
  s_evHead = s_evTail = 0;
  s_p = Params();
  s_stopReq = false;
  s_run = true;
  s_done = false;
  // Core 0 beside the (idle) music task; above it so the stream never starves.
  if (xTaskCreatePinnedToCore(synthTask, "runaudio", 4096, nullptr, 3, &s_task, 0) != pdPASS) {
    s_run = false;
    s_done = true;
    if (s_ownDriver) i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  return true;
}

void stop() {
  if (!s_run) return;
  s_run = false;
  s_stopReq = true;
  uint32_t t0 = millis();
  while (!s_done && millis() - t0 < 800) delay(2);
  if (s_ownDriver && s_done) {
    i2s_driver_uninstall(I2S_NUM_0); // so the music decoder can install its own later
    s_ownDriver = false;
  }
}

} // namespace RunAudio
