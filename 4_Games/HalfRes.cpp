#include "HalfRes.h"
#include <string.h>
#include <esp_heap_caps.h>

namespace HalfRes {

static uint16_t *s_rows = nullptr; // 16 portrait rows, in fast internal RAM while a game runs

void release() {
  heap_caps_free(s_rows);
  s_rows = nullptr;
}

// Each frame column becomes two identical portrait rows of 280 pixels, each
// pixel doubled; rows go out in batches of 16 to keep the bus busy.
void present(Arduino_GFX *panel, bool flipped, const uint16_t *frame) {
  if (!s_rows) s_rows = (uint16_t *)heap_caps_malloc(16 * H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_rows) s_rows = (uint16_t *)heap_caps_malloc(16 * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_rows) return;
  uint16_t *rows = s_rows;
  const int PW = H; // portrait row length, 280
  for (int py0 = 0; py0 < W; py0 += 16) {
    int n = W - py0 < 16 ? W - py0 : 16; // 456 isn't a multiple of 16: the last batch is 8
    for (int r = 0; r < n; r += 2) {
      int py = py0 + r;
      int lx = flipped ? py / 2 : (W - 1 - py) / 2;
      uint16_t *d = rows + r * PW;
      const uint16_t *col = frame + lx;
      if (flipped) {
        // portrait px -> landscape y = 279 - px
        for (int ly = FH - 1; ly >= 0; ly--) {
          uint16_t p = col[ly * FW];
          *d++ = p;
          *d++ = p;
        }
      } else {
        for (int ly = 0; ly < FH; ly++) {
          uint16_t p = col[ly * FW];
          *d++ = p;
          *d++ = p;
        }
      }
      memcpy(rows + (r + 1) * PW, rows + r * PW, PW * 2);
    }
    panel->draw16bitRGBBitmap(0, py0, rows, PW, n);
  }
}

} // namespace HalfRes
