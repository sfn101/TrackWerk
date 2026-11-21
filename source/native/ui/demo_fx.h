#pragma once
// Demo-scene effects, drawn in the current theme's colours: starfield, plasma, copper bars, a sine
// scroller, and the attract screen that combines them (the screensaver after a few idle minutes).
//
// Cheap on purpose: the Miyoo Mini has one Cortex-A7 core and the audio always wins. Plasma works in
// 8-pixel cells, stars and bars are a few hundred rectangles. Every effect is a pure function of
// (theme, time, inputs): the same arguments draw the same pixels, which the checks rely on.
#include <cstdint>
#include <string>

#include "ui/theme.h"

namespace pt::ui {

class Canvas;
struct AppState;

namespace demo_fx {

// The theme's colours as a small gradient for the effects: ground → panel → accent → ink.
Argb ramp(const Theme&, int position /* 0…255 */);

// Stars flying out of the centre of the box; `speed` 0…1 (e.g. loudness), `ms` the clock.
void starfield(Canvas&, int x, int y, int w, int h, const Theme&, uint64_t ms, float speed, int stars = 140);
// A sine plasma in `cell`-pixel blocks, coloured through ramp().
void plasma(Canvas&, int x, int y, int w, int h, const Theme&, uint64_t ms, int cell = 8, float energy = 0.f);
// Horizontal copper bars swinging on sines; `energy[0…count)` (0…1) widens and brightens each bar.
void copper(Canvas&, int x, int y, int w, int h, const Theme&, uint64_t ms, const float* energy = nullptr,
            int count = 0);
// ── The styles after PLASMA (specs/more-visualizers.md, ui/demo_fx_styles.cpp) ──────────────────
// What FIRE and RIDGES keep between frames: fixed-size, one per visualizer (the strip and VI). They
// move only while the clock runs, so they hold still with it while stopped.
struct EffectMemory {
    static constexpr int kFireCols = 220, kFireRows = 100;   // 4 px cells of the widest box, 854 × 392
    uint8_t  fire[kFireCols * kFireRows] = {};
    int      fireCols = 0, fireRows = 0;
    uint64_t fireMs = 0;
    static constexpr int kRidgeRows = 24, kRidgePoints = 64;
    int8_t   ridges[kRidgeRows][kRidgePoints] = {};
    int      ridgeHead = 0;
    uint64_t ridgeMs = 0;
};
// TUNNEL and later: drawn by style().
bool is_style(VisualizerType);
// One of those styles in the box; `wave` the 620-sample waveform (null = silence), `energy` 0…1.
void style(Canvas&, int x, int y, int w, int h, const Theme&, VisualizerType, uint64_t ms,
           const float* wave, float energy, EffectMemory&);

// A text scrolling right to left along a sine, `size` a label size (12…28).
void scroller(Canvas&, int x, int y, int w, int amplitude, const std::string& text, const Theme&, uint64_t ms,
              int size = 28, int pixelsPerSecond = 90);

// ── Attract screen ───────────────────────────────────────────────────────────────────────────────
// After kAttractIdleMs without a key while stopped (and with Appearance > Motion on), the app shows
// a short demo in the active design until any key is pressed: three parts of 12 s each, in a loop.
inline constexpr uint64_t kAttractIdleMs = 3 * 60 * 1000;
inline constexpr uint64_t kAttractPartMs = 12000;
void draw_attract(Canvas&, const AppState&, uint64_t msSinceStart);

// ── Show view ────────────────────────────────────────────────────────────────────────────────────
// SELECT + START: the song as a show for a stage or a projector. The name as a scroller, one copper
// column per track rising with its level, position and tempo in large type.
// `peaks` (kept by TrackerLayout) gives each column a cap that holds the loudest moment and falls
// back slowly; without it, or without a clock, only the level line.
struct ShowPeaks { float peak[8] = {}; uint64_t held[8] = {}; uint64_t last = 0; };
void draw_show(Canvas&, const AppState&, ShowPeaks* peaks = nullptr);

}  // namespace demo_fx
}  // namespace pt::ui
