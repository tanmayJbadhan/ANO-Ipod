#pragma once

#include <stdint.h>
#include "Arduino_GFX_Library.h"

// Both games draw a half-resolution landscape frame (228 x 140, RGB565,
// row by row) and send it to the panel with every pixel doubled - four
// times fewer pixels to draw, at the cost of chunkier graphics.
namespace HalfRes {

static const int W = 456, H = 280;     // screen, landscape
static const int FW = W / 2, FH = H / 2; // the frame

// Frame -> panel, rotated to the panel's portrait layout (upside down when
// `flipped`, as the board sits in the case).
void present(Arduino_GFX *panel, bool flipped, const uint16_t *frame);

} // namespace HalfRes
