// Moving Lines / Moving Spiral - Resolume-style "Lines" generators for WLED (2D matrix).
// Written for WLED 16.0.1. See ../../README.md for build instructions.
//
// Controls (same for both effects, WLED UI -> effect sliders):
//   Speed        sx        how fast the lines travel (quadratic response)
//   Line width   ix        thickness of each line
//   Rotation     c1        0-255 -> 0-360 degrees
//   Colour       c2        rotates Colour 1 around the colour wheel (0 = Colour 1 as picked)
//   Gap          c3 (0-31) empty space between lines
//   Mirror X     check1    fold the pattern about the vertical centre line
//   Mirror Y     check2    fold the pattern about the horizontal centre line
//   Reverse      check3    run the lines the other way
// Colour 1 = line colour, Colour 2 = gap colour (set to black when the effect is picked).
// Selecting either effect turns the segment's own Mirror X / Mirror Y off.
//
// Config -> Usermods -> MovingLines: arch spacing and LED spacing (same unit).
// The maths uses these real-world proportions, so lines travel at right angles
// to themselves as seen on the portal, and 45 degrees looks like 45 degrees.
//
// Moving Lines draws on the segment's 2D canvas like any WLED 2D effect.
// Rotation is the direction of travel, clockwise from "up" on the canvas:
// 0 = horizontal lines moving up, 64 = vertical lines moving right.
//
// Moving Spiral ignores the matrix layout and maps each pixel back to
// (arch, LED index): every matrix panel is one arch (one row, LED 0 at the
// panel's left end, or right end if "starts on right"), so LED n is at the same
// height on every arch. Lines that cross the arches at an angle become spirals
// through the tunnel. The segment's Transpose / Reverse settings make no
// difference; Mirror X folds about the middle arch, Mirror Y about each arch's top.
#include "wled.h"
#include "moving_lines_core.h"

// Set from Config -> Usermods (see MovingLinesUsermod). Ratio is all that matters.
static float mlArchSpacing = 1.0f;
static float mlLedSpacing  = 1.0f;

// Grid pitch in units of one LED spacing.
static float mlines_arch_pitch() {
  return (mlArchSpacing > 0.0f && mlLedSpacing > 0.0f) ? mlArchSpacing / mlLedSpacing : 1.0f;
}

struct MLinesState {
  float    phase;
  uint32_t last;
};

// Shared start of both effects: advances the phase and works out the colours.
// Returns false (after filling the segment) if the effect can't run.
static bool mlines_begin(bool clockwiseFromUp, const MLinesGrid& grid, MLinesParams& p, float& phase,
                         uint32_t& lineCol, uint32_t& gapCol) {
  if (!strip.isMatrix || !SEGMENT.is2D() || !SEGENV.allocateData(sizeof(MLinesState))) {
    SEGMENT.fill(SEGCOLOR(0));                   // needs a 2D matrix
    return false;
  }
  MLinesState* st = reinterpret_cast<MLinesState*>(SEGENV.data);
  if (SEGENV.call == 0) {
    st->phase = 0.0f;
    st->last  = strip.now;
  }

  p = mlines_params(SEGMENT.speed, SEGMENT.intensity, SEGMENT.custom1, SEGMENT.custom3,
                    SEGMENT.check1, SEGMENT.check2, SEGMENT.check3, clockwiseFromUp, grid);

  float dt = (float)(uint32_t)(strip.now - st->last) * 0.001f;
  st->last = strip.now;
  if (dt > 0.5f) dt = 0.5f;                      // avoid a jump after a stall
  st->phase = mlines_advance(st->phase, p, dt);
  phase = st->phase;

  lineCol = mlines_hue_shift(SEGCOLOR(0), SEGMENT.custom2);
  gapCol  = SEGCOLOR(1);
  return true;
}

static void mode_moving_lines(void) {
  const int W = SEG_W;
  const int H = SEG_H;
  // Matrix rows are arches, columns are LEDs along an arch; Transpose swaps them on the canvas.
  const float arch = mlines_arch_pitch();
  const MLinesGrid grid = { SEGMENT.transpose ? arch : 1.0f, SEGMENT.transpose ? 1.0f : arch, W, H };
  MLinesParams p; float phase; uint32_t lineCol, gapCol;
  if (!mlines_begin(true, grid, p, phase, lineCol, gapCol)) return;

  const float cx = (W - 1) * 0.5f;
  const float cy = (H - 1) * 0.5f;
  for (int x = 0; x < W; x++) {
    for (int y = 0; y < H; y++) {
      SEGMENT.setPixelColorXY(x, y, mlines_is_line(p, phase, x - cx, y - cy) ? lineCol : gapCol);
    }
  }
}

static void mode_moving_spiral(void) {
  if (strip.panel.empty()) { SEGMENT.fill(SEGCOLOR(0)); return; }
  const int W = SEG_W;
  const int H = SEG_H;
  const int nArches = strip.panel.size();
  int maxLen = 0;
  for (const auto& pn : strip.panel) maxLen = max(maxLen, (int)pn.width);
  const MLinesGrid grid = { mlines_arch_pitch(), 1.0f, nArches, maxLen };
  MLinesParams p; float phase; uint32_t lineCol, gapCol;
  if (!mlines_begin(false, grid, p, phase, lineCol, gapCol)) return;

  const float pivotY  = (maxLen - 1) * 0.5f;            // common pivot: LED n level on every arch
  const float centreX = (Segment::maxWidth - 1) * 0.5f; // panel offsets put each arch's top on this column
  const int   g       = SEGMENT.groupLength();

  for (int c = 0; c < W; c++) {
    for (int r = 0; r < H; r++) {
      // Undo the segment's reverse/transpose (same order as WS2812FX::blendSegment)
      // to find the matrix cell this virtual pixel lands on.
      int x = c, y = r;
      if (SEGMENT.reverse)   x = W - x - 1;
      if (SEGMENT.reverse_y) y = H - y - 1;
      if (SEGMENT.transpose) std::swap(x, y);
      const int mx = SEGMENT.start  + x * g;
      const int my = SEGMENT.startY + y * g;

      uint32_t col = BLACK;                      // matrix cell with no LED behind it
      for (int a = 0; a < nArches; a++) {
        const auto& pn = strip.panel[a];
        if (my < pn.yOffset || my >= pn.yOffset + pn.height ||
            mx < pn.xOffset || mx >= pn.xOffset + pn.width) continue;
        int   led = mx - pn.xOffset;
        float top = centreX - pn.xOffset;
        if (pn.rightStart) { led = pn.width - 1 - led; top = pn.width - 1 - top; }
        col = mlines_is_line(p, phase, mlines_dx(a, nArches), mlines_dy(p, led, top, pivotY))
              ? lineCol : gapCol;
        break;
      }
      SEGMENT.setPixelColorXY(c, r, col);
    }
  }
}

static const char _data_FX_MODE_MOVING_SPIRAL[] PROGMEM =
  "Moving Spiral@Speed,Line width,Rotation,Colour,Gap,Mirror X,Mirror Y,Reverse;Line,Gap;;2;sx=80,ix=40,c1=0,c2=0,c3=8,mi=0,mY=0";
static const char _data_FX_MODE_MOVING_LINES[] PROGMEM =
  "Moving Lines@Speed,Line width,Rotation,Colour,Gap,Mirror X,Mirror Y,Reverse;Line,Gap;;2;sx=80,ix=40,c1=0,c2=0,c3=8,mi=0,mY=0";

class MovingLinesUsermod : public Usermod {
  uint8_t spiralId = 255, linesId = 255;

  // The UI sends {"seg":{"fx":N,"fxdef":true}} when an effect is picked from the
  // list. WLED has no per-effect default colours, so start the gap colour at
  // black here. Presets carry their own "col" and are left alone.
  void gapToBlack(JsonObject elem, int index) {
    if (!(elem["fxdef"] | false) || elem.containsKey("col")) return;
    const int fx = elem["fx"] | -1;
    if (fx < 0 || (fx != spiralId && fx != linesId)) return;
    const int id = elem["id"] | index;           // no id: all selected segments
    strip.suspend();                             // don't change segments while an effect runs
    strip.waitForIt();
    for (size_t s = 0; s < strip.getSegmentsNum(); s++) {
      Segment& sg = strip.getSegment(s);
      if (sg.mode != fx || (id < 0 ? !(sg.isActive() && sg.isSelected()) : (int)s != id)) continue;
      sg.setColor(1, BLACK);
    }
    strip.resume();
  }

 public:
  void setup() override {
    // Spiral first so it keeps the slot the original "Moving Lines" had.
    spiralId = strip.addEffect(255, &mode_moving_spiral, _data_FX_MODE_MOVING_SPIRAL);
    linesId  = strip.addEffect(255, &mode_moving_lines,  _data_FX_MODE_MOVING_LINES);
  }
  void loop() override {}

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject(F("MovingLines"));
    top[F("archSpacing")] = mlArchSpacing;
    top[F("ledSpacing")]  = mlLedSpacing;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root[F("MovingLines")];
    bool complete = !top.isNull();
    complete &= getJsonValue(top[F("archSpacing")], mlArchSpacing, 1.0f);
    complete &= getJsonValue(top[F("ledSpacing")],  mlLedSpacing,  1.0f);
    return complete;
  }

  void appendConfigData() override {
    oappend(F("addInfo('MovingLines:archSpacing',1,'distance between neighbouring arches');"));
    oappend(F("addInfo('MovingLines:ledSpacing',1,'distance between LEDs along an arch (same unit)');"));
  }

  void readFromJsonState(JsonObject& root) override {
    JsonVariant segVar = root["seg"];
    if (segVar.is<JsonObject>()) gapToBlack(segVar.as<JsonObject>(), -1);
    else if (segVar.is<JsonArray>()) { int i = 0; for (JsonObject elem : segVar.as<JsonArray>()) gapToBlack(elem, i++); }
  }
};

static MovingLinesUsermod moving_lines_usermod;
REGISTER_USERMOD(moving_lines_usermod);
