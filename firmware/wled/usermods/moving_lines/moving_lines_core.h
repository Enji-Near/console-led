// moving_lines_core.h
// Pure maths for the "Moving Lines" and "Moving Spiral" effects. No WLED/Arduino
// dependencies so it can be unit-tested on a normal computer (see test/test_moving_lines.cpp).
//
// Moving Lines works on the segment's 2D canvas: the caller passes each pixel's
// offset from the canvas centre to mlines_is_line().
//
// Moving Spiral works in "arch space", not in WLED's matrix space:
//   X = arch number (C1 = 0 ... C4 = 6)
//   Y = LED index along that arch's strip (LED 0 = floor end)
// so a given LED index is at the same Y on every arch, whatever the matrix
// panel offsets are. The caller converts each pixel to an offset from the fold
// centre (mlines_dx / mlines_dy) and asks mlines_is_line() about it.
#pragma once
#include <math.h>
#include <stdint.h>

// ---- Geometry ------------------------------------------------------------------
// The maths runs in real-world proportions so that lines move at right angles to
// themselves as you see them on the portal, not just on the pixel grid. pitchX /
// pitchY are the physical distances between neighbouring pixels in X and Y in a
// common unit (the effect uses "one LED spacing" = 1, so the arch pitch is
// arch spacing / LED spacing; set in Config -> Usermods). nX / nY: grid size.
struct MLinesGrid {
  float pitchX, pitchY;
  int   nX, nY;
};

// Widest line / gap (slider at max) and fastest speed (units per second) as a
// fraction of the larger side of the display.
#define MLINES_MAX_WIDTH_FRAC 0.5f
#define MLINES_MAX_SPEED_FRAC 0.5f

struct MLinesParams {
  float cosA, sinA;     // line-normal direction (= direction of travel)
  float width;          // line width (units)
  float gap;            // gap width  (units)
  float period;         // width + gap
  float speed;          // units per second (signed: negative = reverse)
  float pitchX, pitchY;
  bool  mirrorX, mirrorY;
};

// Quadratic response: fine control at the low end where the arches are only a
// few pixels apart, long reach at the top.
static inline float mlines_curve(float v01) { return v01 * v01; }

// sx, ix, rot are 0-255, gap31 is 0-31.
// rot is the direction of travel, 0..358.6 deg (64 = 90, 128 = 180, 192 = 270):
//   clockwiseFromUp = false (Moving Spiral): 0 = +X, 64 = +Y
//   clockwiseFromUp = true  (Moving Lines, Y pointing down the canvas):
//                           0 = up (-Y), 64 = right (+X), 128 = down, 192 = left
static inline MLinesParams mlines_params(uint8_t sx, uint8_t ix, uint8_t rot,
                                         uint8_t gap31, bool mirrorX, bool mirrorY,
                                         bool reverse, bool clockwiseFromUp,
                                         const MLinesGrid& g) {
  MLinesParams p;
  const float PI_F = 3.14159265358979f;
  float ang = (float)rot * (2.0f * PI_F / 256.0f);
  float c = cosf(ang), s = sinf(ang);
  if ((rot & 63) == 0) {                       // exact values at 0/90/180/270 degrees so
    static const float C[4] = { 1.f, 0.f, -1.f, 0.f };   // lines are perfectly straight
    static const float S[4] = { 0.f, 1.f, 0.f, -1.f };
    c = C[rot >> 6];
    s = S[rot >> 6];
  }
  p.cosA = clockwiseFromUp ? s : c;
  p.sinA = clockwiseFromUp ? -c : s;
  // A hard-edged line too thin for the pixel grid would fall between pixels and
  // flicker or vanish as it moves, so the width never goes below what the grid
  // can show. A line stays visible if it covers a pixel on every column (needs
  // |sin| * pitchY), unless it crosses the columns so steeply that it jumps right
  // past the display between one column and the next; likewise for rows. Take
  // whichever way needs the thinner line: one LED for most angles, one arch
  // when the lines run (nearly) along the arches.
  const float ac = fabsf(p.cosA), as = fabsf(p.sinA);
  const float lenX = (g.nX - 1) * g.pitchX, lenY = (g.nY - 1) * g.pitchY;
  float viaColumns = fmaxf(as * g.pitchY, ac * g.pitchX - as * lenY);
  float viaRows    = fmaxf(ac * g.pitchX, as * g.pitchY - ac * lenX);
  float step = fminf(viaColumns, viaRows);
  float size = fmaxf(fmaxf(lenX, lenY), 1.0f);
  p.width  = fmaxf(mlines_curve(ix / 255.0f) * MLINES_MAX_WIDTH_FRAC * size, step);
  p.gap    = mlines_curve(gap31 / 31.0f) * MLINES_MAX_WIDTH_FRAC * size;
  p.period = p.width + p.gap;
  p.speed  = mlines_curve(sx / 255.0f) * MLINES_MAX_SPEED_FRAC * size * (reverse ? -1.0f : 1.0f);
  p.pitchX = g.pitchX;
  p.pitchY = g.pitchY;
  p.mirrorX = mirrorX;
  p.mirrorY = mirrorY;
  return p;
}

// Offset of an arch from the middle arch (Mirror X folds about the middle arch).
static inline float mlines_dx(int arch, int nArches) {
  return (float)arch - (nArches - 1) * 0.5f;
}

// Offset of an LED along its arch. Without Mirror Y every arch uses the same
// pivot, so LED n is at the same height on every arch. With Mirror Y the
// pattern folds about each arch's own top LED (archTop).
static inline float mlines_dy(const MLinesParams& p, int led, float archTop, float pivotY) {
  return (float)led - (p.mirrorY ? archTop : pivotY);
}

// Advance the animation phase by dt seconds and keep it inside one period.
static inline float mlines_advance(float phase, const MLinesParams& p, float dt) {
  phase += p.speed * dt;
  if (p.period > 0.0f) {
    phase = fmodf(phase, p.period);
    if (phase < 0.0f) phase += p.period;
  }
  return phase;
}

// true = this pixel is on a line, false = in the gap. dx / dy from mlines_dx / mlines_dy.
static inline bool mlines_is_line(const MLinesParams& p, float phase, float dx, float dy) {
  float px = dx * p.pitchX;
  float py = dy * p.pitchY;
  if (p.mirrorX) px = fabsf(px);
  if (p.mirrorY) py = fabsf(py);
  float t = px * p.cosA + py * p.sinA;           // distance along the line normal
  float s = fmodf(t - phase, p.period);
  if (s < 0.0f) s += p.period;
  return s < p.width;
}

// Rotate a 0xWWRRGGBB colour around the colour wheel by shift/256 of a turn,
// keeping its saturation and brightness. shift 0 returns the colour unchanged;
// grey / white / black have no hue and come back unchanged too.
static inline uint32_t mlines_hue_shift(uint32_t c, uint8_t shift) {
  if (shift == 0) return c;
  float r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
  float chroma = mx - mn;
  if (chroma <= 0.0f) return c;
  float h;                                       // hue in sextants, 0..6
  if (mx == r)      h = (g - b) / chroma;
  else if (mx == g) h = (b - r) / chroma + 2.0f;
  else              h = (r - g) / chroma + 4.0f;
  h = fmodf(h + shift * (6.0f / 256.0f) + 6.0f, 6.0f);
  float x = chroma * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
  float o[3];
  switch ((int)h) {
    case 0:  o[0] = chroma; o[1] = x;      o[2] = 0;      break;
    case 1:  o[0] = x;      o[1] = chroma; o[2] = 0;      break;
    case 2:  o[0] = 0;      o[1] = chroma; o[2] = x;      break;
    case 3:  o[0] = 0;      o[1] = x;      o[2] = chroma; break;
    case 4:  o[0] = x;      o[1] = 0;      o[2] = chroma; break;
    default: o[0] = chroma; o[1] = 0;      o[2] = x;      break;
  }
  uint32_t out = c & 0xFF000000u;                // keep the white channel
  for (int i = 0; i < 3; i++) out |= (uint32_t)lroundf(o[i] + mn) << (16 - 8 * i);
  return out;
}
