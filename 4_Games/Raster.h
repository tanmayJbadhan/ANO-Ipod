#pragma once

#include <stdint.h>

// A tiny software rasteriser for the games: filled shapes straight into an
// RGB565 buffer laid out row by row in landscape (unlike the portrait
// canvas, so horizontal spans are contiguous memory). Coordinates are
// floats, sampled at pixel centres; everything clips.
//
// Each core has its own target, scale and edge mode, so two cores can draw
// different bands of one frame at the same time.
namespace Raster {

// Where drawing goes. The game draws its frame and its skyline layers
// through the same functions, so the target is switchable. A target can be
// a band of a taller picture: its first row is row `top` of the picture,
// and shapes are given (and clipped) in picture coordinates.
void target(uint16_t *buf, int16_t w, int16_t h, int16_t top = 0);
uint16_t *buffer();
// Multiplies every coordinate and size passed to the shape functions (not
// span(), which is raw pixels) - lets callers draw in screen units into a
// smaller buffer.
void setScale(float k);
int16_t width();
int16_t height();
int16_t top();
// The buffer row for picture row y, or nullptr when y is outside the band.
uint16_t *row(int y);
// Smooth (anti-aliased) edges for poly, circle/ellipse, line and quad4:
// edge pixels are blended by how much of them the shape covers.
void smooth(bool on);

uint16_t rgb(uint32_t hex);                          // 0xRRGGBB -> RGB565
uint16_t mix(uint32_t a, uint32_t b, float t);       // blend in 8-bit space, t=0 -> a
uint32_t mixHex(uint32_t a, uint32_t b, float t);
uint16_t blend(uint16_t a, uint16_t b, uint8_t t);   // t=0 -> a, 255 -> b

void clear(uint16_t c);
void span(int y, int x0, int x1, uint16_t c, uint8_t a = 255); // [x0, x1)
void rect(float x, float y, float w, float h, uint16_t c, uint8_t a = 255);
// Convex polygon, xy = {x0,y0,x1,y1,...}, up to 16 points.
void poly(const float *xy, int n, uint16_t c, uint8_t a = 255);
void quad4(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
          uint16_t c, uint8_t a = 255);
// Trapezoid with horizontal top and bottom - the road's building block.
void trap(float xl1, float xr1, float y1, float xl2, float xr2, float y2, uint16_t c, uint8_t a = 255);
void circle(float cx, float cy, float r, uint16_t c, uint8_t a = 255);
void ellipse(float cx, float cy, float rx, float ry, uint16_t c, uint8_t a = 255);
void ellipseRot(float cx, float cy, float rx, float ry, float ang, uint16_t c, uint8_t a = 255);
void line(float x0, float y0, float x1, float y1, float w, uint16_t c, uint8_t a = 255);
void roundRect(float x, float y, float w, float h, float r, uint16_t c, uint8_t a = 255);
void star(float cx, float cy, float r, uint16_t c);

} // namespace Raster
