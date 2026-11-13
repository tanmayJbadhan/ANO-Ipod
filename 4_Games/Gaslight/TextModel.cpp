// Gaslight & Griffins - the chatter model: a small character GRU trained on
// the game's own dialogue (tools/textmodel), run in the background on the
// second core whenever it isn't drawing, filling a few lines per kind of
// speaker ahead of time so nobody ever waits. The story itself is never
// generated: only crowd chatter, rumours and battle taunts.
#pragma GCC optimize("O2")
#include "Game.h"
#include "GLText.h"
#include "GLWords.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdlib.h>

namespace Gaslight {

static const int CATS = 12, SLOTS = 2, LEN = 112;
static const char *const CAT_TAG[CATS] = {"fc", "sc", "gc", "kc", "uc", "fr", "sr", "gr", "kr", "ur", "ab", "av"};
struct Pool { char line[SLOTS][LEN]; volatile uint8_t ready[SLOTS]; };
static Pool *s_pool = nullptr;
static float *s_h = nullptr;        // hidden state
static float *s_scratch = nullptr;  // 3H gates + V logits
static uint32_t s_rng = 0x9E3779B9;
static volatile bool s_run = false;
static volatile bool s_inStep = false; // the worker is in the middle of a step
#ifndef PC_SIM
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#endif

static int charIndex(char c) {
  for (int i = 0; i < TM_V; i++) if (TM_CHARS[i] == c) return i;
  return -1;
}

static inline float sigm(float x) { return 1.0f / (1.0f + expf(-x)); }

// one step: feed character c, update the hidden state
static void stepChar(int c) {
  const int H = TM_H;
  float *gh = s_scratch;  // 3H
  for (int r = 0; r < 3 * H; r++) {
    const int8_t *w = TM_HQ + r * H;
    float acc = 0;
    for (int k = 0; k < H; k++) acc += w[k] * s_h[k];
    gh[r] = acc * TM_HS[r] + TM_HB[r];
  }
  const int8_t *xq = TM_XQ + c * 3 * H;
  for (int i = 0; i < H; i++) {
    float xr = xq[i] * TM_XS[c], xz = xq[H + i] * TM_XS[c], xn = xq[2 * H + i] * TM_XS[c];
    float r = sigm(xr + gh[i]), z = sigm(xz + gh[H + i]);
    float n = tanhf(xn + r * gh[2 * H + i]);
    s_h[i] = (1 - z) * n + z * s_h[i];
  }
}

static int sample(float temp) {
  float *lg = s_scratch + 3 * TM_H;
  float mx = -1e9f;
  for (int v = 0; v < TM_V; v++) {
    const int8_t *w = TM_OQ + v * TM_H;
    float acc = 0;
    for (int k = 0; k < TM_H; k++) acc += w[k] * s_h[k];
    lg[v] = (acc * TM_OS[v] + TM_OB[v]) / temp;
    if (lg[v] > mx) mx = lg[v];
  }
  // keep the eight likeliest, then draw
  float sum = 0;
  float cut = mx - 4.0f;
  for (int v = 0; v < TM_V; v++) { lg[v] = lg[v] < cut ? 0 : expf(lg[v] - mx); sum += lg[v]; }
  s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
  float pick = (s_rng >> 8) / 16777216.0f * sum;
  for (int v = 0; v < TM_V; v++) { pick -= lg[v]; if (pick <= 0) return v; }
  return TM_V - 1;
}

// A line for one tag, or false if it came out badly (it's then thrown away).
static bool goodLine(const char *out, int n);
static bool generate(int cat, char *out) {
  memset(s_h, 0, TM_H * sizeof(float));
  char tag[5] = {'<', CAT_TAG[cat][0], CAT_TAG[cat][1], '>', 0};
  int c = -1;
  for (int i = 0; i < 4; i++) { c = charIndex(tag[i]); if (c < 0) return false; stepChar(c); }
  int n = 0;
  for (int i = 0; i < LEN - 1; i++) {
    int v = sample(0.65f);
    char ch = TM_CHARS[v];
    if (ch == '\n') break;
    if (ch == '<' || ch == '>') return false;
    out[n++] = ch;
    stepChar(v);
  }
  out[n] = 0;
  return goodLine(out, n);
}

static int emptiest() {
  int best = -1, bestN = SLOTS;
  for (int c = 0; c < CATS; c++) {
    int n = 0;
    for (int s = 0; s < SLOTS; s++) n += s_pool[c].ready[s];
    if (n < bestN) { bestN = n; best = c; }
  }
  return best;
}

static void fillOne() __attribute__((unused));
static void fillOne() {
  int c = emptiest();
  if (c < 0) return;
  char tmp[LEN];
  if (!generate(c, tmp)) return;
  for (int s = 0; s < SLOTS; s++)
    if (!s_pool[c].ready[s]) {
#ifndef PC_SIM
      portENTER_CRITICAL(&s_mux);
#endif
      memcpy(s_pool[c].line[s], tmp, LEN);
      s_pool[c].ready[s] = 1;
#ifndef PC_SIM
      portEXIT_CRITICAL(&s_mux);
#endif
      return;
    }
}

// The drawing worker on the second core calls this while it waits for the
// next frame: a few characters of a line at a time, so a frame is never late.
static struct { int cat, n; char buf[LEN]; } s_gen = {-1, 0, {0}};
static bool knownWord(const char *w, int n) {
  char low[32];
  if (n <= 0 || n > 31) return n == 0;
  for (int i = 0; i < n; i++) low[i] = (w[i] >= 'A' && w[i] <= 'Z') ? w[i] + 32 : w[i];
  low[n] = 0;
  int lo = 0, hi = TM_NWORDS - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(TM_WORDS[mid], low);
    if (!c) return true;
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return false;
}
static bool goodLine(const char *out, int n) {
  if (n < 12 || n > LEN - 4) return false;
  char last = out[n - 1];
  if (last != '.' && last != '!' && last != '?') return false;
  if (!(out[0] >= 'A' && out[0] <= 'Z')) return false;
  // every word must be one the model was taught
  int start = -1;
  for (int i = 0; i <= n; i++) {
    char c = i < n ? out[i] : ' ';
    bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == 39 && start >= 0);
    if (letter && start < 0) start = i;
    if (!letter && start >= 0) {
      int e = i;
      while (e > start && out[e - 1] == 39) e--;
      if (!knownWord(out + start, e - start)) return false;
      start = -1;
    }
  }
  return true;
}
static volatile bool s_paused = false;
void chatterPause(bool p) {
  s_paused = p;
  for (int i = 0; i < 200 && p && s_inStep; i++) delay(1);
}
bool chatterWants() {
  if (s_paused) return false; return s_run && s_pool && (s_gen.cat >= 0 || emptiest() >= 0); }
static void chatterStepInner(int chars);
void chatterStep(int chars) {
  s_inStep = true;
  if (s_run && s_pool) chatterStepInner(chars);
  s_inStep = false;
}
static void chatterStepInner(int chars) {
  if (s_gen.cat < 0) {
    int c = emptiest();
    if (c < 0) return;
    memset(s_h, 0, TM_H * sizeof(float));
    char tag[5] = {'<', CAT_TAG[c][0], CAT_TAG[c][1], '>', 0};
    for (int i = 0; i < 4; i++) { int k = charIndex(tag[i]); if (k < 0) return; stepChar(k); }
    s_gen.cat = c;
    s_gen.n = 0;
    return;
  }
  for (int i = 0; i < chars; i++) {
    int v = sample(0.65f);
    char ch = TM_CHARS[v];
    if (ch == 10 || s_gen.n >= LEN - 2 || ch == '<' || ch == '>') {
      s_gen.buf[s_gen.n] = 0;
      if (ch == 10 && goodLine(s_gen.buf, s_gen.n)) {
        Pool &p = s_pool[s_gen.cat];
        for (int sl = 0; sl < SLOTS; sl++)
          if (!p.ready[sl]) {
#ifndef PC_SIM
            portENTER_CRITICAL(&s_mux);
#endif
            memcpy(p.line[sl], s_gen.buf, LEN);
            p.ready[sl] = 1;
#ifndef PC_SIM
            portEXIT_CRITICAL(&s_mux);
#endif
            break;
          }
      }
      s_gen.cat = -1;
      return;
    }
    s_gen.buf[s_gen.n++] = ch;
    stepChar(v);
  }
}

bool chatterStart() {
  if (!s_pool) s_pool = (Pool *)heap_caps_calloc(CATS, sizeof(Pool), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_h) s_h = (float *)heap_caps_calloc(TM_H, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_scratch) s_scratch = (float *)heap_caps_calloc(3 * TM_H + TM_V, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_pool || !s_h || !s_scratch) return false;
  s_rng ^= (uint32_t)micros();
  s_run = true;
#ifndef PC_SIM
#endif
  return true;
}

void chatterStop() {
  s_run = false;
  // let a step that's already running finish before its memory goes
  for (int i = 0; i < 200 && s_inStep; i++) delay(1);
#ifndef PC_SIM
#endif
  heap_caps_free(s_pool); heap_caps_free(s_h); heap_caps_free(s_scratch);
  s_pool = nullptr; s_h = nullptr; s_scratch = nullptr;
}

// Takes a ready line for a tag like "fc"; false if none is ready yet.
bool chatterTake(const char *tag, char *out, int cap) {
  if (!s_pool) return false;
  int c = -1;
  for (int i = 0; i < CATS; i++) if (CAT_TAG[i][0] == tag[0] && CAT_TAG[i][1] == tag[1]) c = i;
  if (c < 0) return false;
#ifdef PC_SIM
  for (int tries = 0; tries < 6 && !s_pool[c].ready[0]; tries++) {
    char tmp[LEN];
    if (generate(c, tmp)) { memcpy(s_pool[c].line[0], tmp, LEN); s_pool[c].ready[0] = 1; }
  }
#endif
  for (int s = 0; s < SLOTS; s++)
    if (s_pool[c].ready[s]) {
#ifndef PC_SIM
      portENTER_CRITICAL(&s_mux);
#endif
      snprintf(out, cap, "%s", s_pool[c].line[s]);
      s_pool[c].ready[s] = 0;
#ifndef PC_SIM
      portEXIT_CRITICAL(&s_mux);
#endif
      return true;
    }
  return false;
}

} // namespace Gaslight
