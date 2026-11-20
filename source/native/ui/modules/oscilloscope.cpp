#include "ui/modules/oscilloscope.h"

#include <algorithm>  // std::max — libstdc++ does not pull it in
#include <cmath>
#include <iterator>

#include "ui/demo_fx.h"
#include "ui/helpers.h"
#include "ui/scene_animation.h"

namespace pt::ui {

namespace {
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void OscilloscopeModule::draw_sized(Canvas& c, int x, int y, int w, int h, const OscilloscopeState& s) {
    const Theme& t = s.theme;
    w_ = std::min(w, int(MAX_WIDTH));   // the phosphor history holds at most MAX_WIDTH/N blocks
    h_ = h;
    if (s.playing) demoMs_ = s.animationMs;   // stopped: COPPER, STARFIELD and PLASMA hold still

    // OCTA fills the strip with `background`, so the gaps between lanes read as it, then paints each
    // lane's `vizBackground`; every other mode fills with `vizBackground`.
    const bool isOcta =
        (t.visualizerType == VisualizerType::OCTA || t.visualizerType == VisualizerType::OCTA_FULL);
    c.fill_rect(x, y, w_, h_, isOcta ? t.background : t.vizBackground);
    // The afterglow belongs to one SCOPE strip at one place: another mode or position drops it.
    if (t.visualizerType != VisualizerType::SCOPE || x != phosphorX_ || y != phosphorY_) phosphorFrames_ = 0;
    phosphorX_ = x;
    phosphorY_ = y;
    const int family = scene_animation::family_of(t);
    if (s.playing && family == scene_animation::RASTER)
        scene_animation::raster_bars(c, x, y, w_, h_, t, s.animationMs, s.bpm);

    switch (t.visualizerType) {
        case VisualizerType::SCOPE:
            draw_scope(c, x, y, s.waveform, t);
            break;
        case VisualizerType::FLAT:
            draw_flat(c, x, y, t);
            break;
        case VisualizerType::OCTA:
        case VisualizerType::OCTA_FULL:
            draw_octa(c, x, y, s.trackWaveforms, s.activeTrackMask, t);
            break;
        case VisualizerType::SPECTRUM:
            draw_bar_amps(c, x, y, s.spectrum, t, /*peak_mode=*/false);
            break;
        case VisualizerType::SPECTRUM_PEAKS:
            draw_bar_amps(c, x, y, s.spectrum, t, /*peak_mode=*/true);
            break;
        case VisualizerType::COPPER:
        case VisualizerType::STARFIELD:
        case VisualizerType::PLASMA:
            draw_demo(c, x, y, s);
            break;
        default:   // TUNNEL and the other styles after PLASMA
            if (demo_fx::is_style(t.visualizerType)) draw_demo(c, x, y, s);
            break;
    }
    // On the one of every bar a bright scanline sweeps once down the strip (Motion on, playing).
    if (s.playing && s.bpm > 0) {
        const uint64_t barMs = 240000ull / static_cast<uint64_t>(s.bpm);
        const uint64_t into = s.animationMs % barMs, sweep = std::min<uint64_t>(barMs / 4, 220);
        if (into < sweep) {
            const int line = y + static_cast<int>(into * (h_ - 2) / sweep);
            const float fade = 1.f - float(into) / float(sweep);
            c.fill_rect(x, line, w_, 2, with_alpha(t.vizWave, .45f * fade));
            c.fill_rect(x, std::max(y, line - 6), w_, std::min(6, line - y), with_alpha(t.vizWave, .10f * fade));
        }
    }
    // Along the foot of the strip, clear of the status line at its top.
    if (s.playing && family == scene_animation::CRACKTRO)
        scene_animation::sine_scroller(c, x + 2, y + h_ - 30, w_ - 4, 28, t, s.animationMs);
}

namespace {
// Loudness of a waveform window, 0…1 (RMS, lifted so a quiet mix still moves the effect).
float energy(const float* wave) {
    if (!wave) return 0.f;
    double sum = 0;
    for (int i = 0; i < WAVEFORM_SIZE; i += 2) sum += double(wave[i]) * wave[i];
    return std::min(1.f, float(std::sqrt(sum / (WAVEFORM_SIZE / 2))) * 3.f);
}
}  // namespace

// The demo visualizers. Inside the strip only; the animation clock moves them while playing.
void OscilloscopeModule::draw_demo(Canvas& c, int x, int y, const OscilloscopeState& s) {
    const Theme& t = s.theme;
    Canvas::ClipScope clip(c, x, y, w_, h_);
    c.fill_rect(x, y, w_, h_, t.vizBackground);
    const uint64_t ms = demoMs_;
    switch (t.visualizerType) {
        case VisualizerType::COPPER: {
            float bars[8] = {};
            int count = 0;
            for (int track = 0; track < 8; ++track)
                if (s.trackWaveforms && ((s.activeTrackMask >> track) & 1))
                    bars[count++] = energy(s.trackWaveforms + static_cast<size_t>(track) * WAVEFORM_SIZE);
            if (count == 0) { bars[0] = .1f; count = 1; }
            demo_fx::copper(c, x, y, w_, h_, t, ms, bars, count);
            break;
        }
        case VisualizerType::STARFIELD:
            // The strip's density: 170 stars per 620×70, so a bigger area is not left empty.
            demo_fx::starfield(c, x, y, w_, h_, t, ms, energy(s.waveform),
                               static_cast<int>(170LL * w_ * h_ / (WIDTH * HEIGHT)));
            break;
        case VisualizerType::PLASMA:
            demo_fx::plasma(c, x, y, w_, h_, t, ms, 6, energy(s.waveform));
            break;
        default:
            demo_fx::style(c, x, y, w_, h_, t, t.visualizerType, ms, s.waveform, energy(s.waveform), effects_);
            break;
    }
    // Faint scanlines, and a dark band along the top where the status line is written.
    for (int py = y + 1; py < y + h_; py += 2) c.fill_rect(x, py, w_, 1, 0x30000000u);
    if (!strip()) return;
    c.fill_rect(x, y, w_, 26, with_alpha(t.vizBackground, .72f));
    c.fill_rect(x, y + 26, w_, 3, with_alpha(t.vizBackground, .35f));
}

void OscilloscopeModule::draw_scope(Canvas& c, int x, int y, const float* wave, const Theme& t) {
    const int centerY      = y + h_ / 2;
    const int maxAmplitude = (h_ / 2) - 4;

    if (t.sceneChrome > 0) draw_graticule(c, x, y, t);
    c.fill_rect(x, centerY, w_, 1, t.vizCenterLine);
    if (!wave || t.sceneChrome == 0) {
        phosphorFrames_ = 0;
        draw_wave_dots(c, x, w_, wave, centerY, maxAmplitude, t.vizWave);
        return;
    }
    // Oldest first, dimmer: the afterglow sits under the live trace.
    constexpr int N = SCOPE_PIXEL_BLOCK;
    for (int age = phosphorFrames_ - 1; age >= 0; --age) {
        const Argb glow = with_alpha(t.vizWave, age == 0 ? .30f : .12f);
        for (int b = 0; b < w_ / N; ++b) c.fill_rect(x + b * N, y + phosphor_[age][b], N, N, glow);
    }
    int16_t live[SCOPE_BLOCKS];
    draw_wave_dots(c, x, w_, wave, centerY, maxAmplitude, t.vizWave, live);
    std::copy(std::begin(phosphor_[0]), std::end(phosphor_[0]), std::begin(phosphor_[1]));
    for (int b = 0; b < w_ / N; ++b) phosphor_[0][b] = static_cast<int16_t>(live[b] - y);
    phosphorFrames_ = std::min(phosphorFrames_ + 1, 2);
}

// A bench scope's graticule, Scenes only: ten divisions across, dotted quarter lines, and ticks on
// the centre line. Static, under the trace, faint enough that a stopped strip still reads as empty.
void OscilloscopeModule::draw_graticule(Canvas& c, int x, int y, const Theme& t) const {
    const Argb dot = with_alpha(t.vizCenterLine, .55f);
    const Argb tick = with_alpha(t.vizCenterLine, .85f);
    const int centerY = y + h_ / 2;
    constexpr int kDivisions = 10;
    for (int d = 1; d < kDivisions; ++d) {
        const int gx = x + d * w_ / kDivisions;
        for (int gy = y + 4; gy < y + h_ - 3; gy += 4) c.fill_rect(gx, gy, 1, 1, dot);
        c.fill_rect(gx, centerY - 3, 1, 7, tick);
    }
    for (int q : {1, 3}) {
        const int gy = y + q * h_ / 4;
        for (int gx = x + 4; gx < x + w_ - 3; gx += 4) c.fill_rect(gx, gy, 1, 1, dot);
    }
    for (int gx = x + w_ / (kDivisions * 5); gx < x + w_; gx += w_ / (kDivisions * 5))
        c.fill_rect(gx, centerY - 1, 1, 3, dot);
}

void OscilloscopeModule::draw_wave_dots(Canvas& c, int scope_x, int scope_w, const float* wave,
                                        int center_y, int max_amplitude, Argb color,
                                        int16_t* record) const {
    // Time zoom: the centred WAVEFORM_SIZE/Z samples across the strip.
    constexpr int N          = SCOPE_PIXEL_BLOCK;
    const int     windowSize = WAVEFORM_SIZE / SCOPE_TIME_ZOOM;
    const int     windowBase = (WAVEFORM_SIZE - windowSize) / 2;

    // One dot per N-wide block on an N-px grid.
    int prevY = center_y;
    for (int bx = 0; bx < scope_w; bx += N) {
        const int blockW = (bx + N <= scope_w) ? N : (scope_w - bx);
        const int col    = bx + blockW / 2;
        const int srcIdx = windowBase + static_cast<int>((static_cast<long long>(col) * windowSize) /
                                                         scope_w);
        // A null buffer is silence: every block on the centre line.
        const float sample = wave ? clampf(wave[srcIdx] * WAVEFORM_GAIN, -1.0f, 1.0f) : 0.0f;
        int         dotY   = center_y + static_cast<int>(sample * static_cast<float>(max_amplitude));
        // Snap the block to an N-px grid aligned on the centre line.
        dotY = center_y + ((dotY - center_y) / N) * N;
        c.fill_rect(scope_x + bx, dotY, blockW, N, color);
        // Full size only: at this height neighbouring dots sit far apart, so a short stroke joins
        // them into a line. The strip is left exactly as it was.
        if (!strip() && bx > 0 && dotY != prevY) {
            const int top = std::min(dotY, prevY), bottom = std::max(dotY, prevY);
            c.fill_rect(scope_x + bx, top, 1, bottom - top, color);
        }
        prevY = dotY;
        if (record && bx / N < SCOPE_BLOCKS) record[bx / N] = static_cast<int16_t>(dotY);
    }
}

void OscilloscopeModule::draw_flat(Canvas& c, int x, int y, const Theme& t) const {
    c.fill_rect(x, y + h_ - 1, w_, 1, t.vizCenterLine);
}

void OscilloscopeModule::draw_octa(Canvas& c, int x, int y, const float* tracks, int mask,
                                   const Theme& t) const {
    // The strip puts lanes side by side; a tall area (VI) stacks them as rows, each as wide as the
    // area, so a lane keeps the strip's wide-and-short shape instead of a narrow column.
    const bool rows = !strip();
    const auto lane_rect = [&](int idx, int count, int& lx, int& ly, int& lw, int& lh) {
        const int along = rows ? h_ : w_;
        const int size  = (along - OCTA_TRACK_GAP * (count - 1)) / count;
        const int at    = idx * (size + OCTA_TRACK_GAP);
        lx = rows ? x : x + at;   lw = rows ? w_ : size;
        ly = rows ? y + at : y;   lh = rows ? size : h_;
    };

    if (!tracks || mask == 0) {
        // Nothing scheduled: the eight lanes at rest, faint, with their track numbers along the foot,
        // so the area reads as the visualizer it becomes on START rather than as an empty box.
        for (int lane = 0; lane < 8; ++lane) {
            int lx, ly, lw, lh;
            lane_rect(lane, 8, lx, ly, lw, lh);
            c.fill_rect(lx, ly, lw, lh, t.vizBackground);
            c.fill_rect(lx + 4, ly + lh / 2, lw - 8, 1, with_alpha(t.vizCenterLine, .6f));
            const std::string number = std::to_string(lane + 1);
            c.draw_label(number, lx + lw - 6 - Canvas::label_width(number, 11), ly + lh - 15,
                         with_alpha(t.textEmpty, .8f), 11);
        }
        return;
    }

    // Lane 8 is the preview (set only when stopped). OCTA_FULL forces all eight on, so the layout
    // never reflows mid-song.
    int activeTracks[TRACK_WAVEFORM_COUNT];
    int count = 0;
    for (int i = 0; i < TRACK_WAVEFORM_COUNT; ++i) {
        if ((mask >> i) & 1) activeTracks[count++] = i;
    }
    if (count == 0) return;

    for (int idx = 0; idx < count; ++idx) {
        int lx, ly, lw, lh;
        lane_rect(idx, count, lx, ly, lw, lh);
        const float* wave = tracks + static_cast<size_t>(activeTracks[idx]) * WAVEFORM_SIZE;
        c.fill_rect(lx, ly, lw, lh, t.vizBackground);
        c.fill_rect(lx, ly + lh / 2, lw, 1, t.vizCenterLine);
        draw_wave_dots(c, lx, lw, wave, ly + lh / 2, lh / 2 - 4, t.vizWave);
    }
}

void OscilloscopeModule::draw_bar_amps(Canvas& c, int x, int y, const float* amps, const Theme& t,
                                       bool peak_mode) {
    const int   barBottom = y + h_ - 2;   // 2px bottom margin
    const float maxAmp    = static_cast<float>(h_ - 4);
    // The strip's 14 px bars and 2 px LEDs; a bigger area gets wider bars and taller LEDs, not stretched.
    const int barW    = (w_ - 2 * BAR_START_OFFSET) / NUM_BARS - BAR_GAP;
    const int segH    = std::max(int(SEGMENT_H), h_ / 35);
    const int segStep = segH + std::max(int(SEG_GAP), segH / 3);

    for (int i = 0; i < NUM_BARS; ++i) {
        const int barX = x + BAR_START_OFFSET + i * (barW + BAR_GAP);
        // Under one LED cell reads as silence.
        const float rawIn = amps ? amps[i] : 0.0f;
        const float raw   = (rawIn * maxAmp >= static_cast<float>(segH)) ? rawIn : 0.0f;

        // Instant attack, exponential decay; a bar as fast as the audio would strobe.
        if (raw > barSmoothed_[i]) {
            barSmoothed_[i] = raw;
        } else {
            barSmoothed_[i] *= BAR_DECAY;
            // ⚠️ Snapped to zero under one LED cell, or the decay holds `bars_at_rest` (and 60 Hz
            // redraws) for ~1000 frames.
            if (barSmoothed_[i] * maxAmp < static_cast<float>(segH)) barSmoothed_[i] = 0.0f;
        }

        const int barH = static_cast<int>(barSmoothed_[i] * maxAmp);

        for (int dy = 0; dy + segH <= barH; dy += segStep) {
            c.fill_rect(barX, barBottom - dy - segH + 1, barW, segH, t.vizWave);
        }

        if (!peak_mode) {
            // SPECTRUM draws no dot, so it keeps none (a SPECT P theme's would hold `bars_at_rest`).
            peakValues_[i]        = 0.0f;
            peakDecayCounters_[i] = 0;
            continue;
        }

        if (barSmoothed_[i] > peakValues_[i]) {
            peakValues_[i]        = barSmoothed_[i];
            peakDecayCounters_[i] = 0;
        } else {
            peakDecayCounters_[i]++;
            if (peakDecayCounters_[i] > PEAK_HOLD_FRAMES) {
                peakValues_[i] =
                    std::max(0.0f, peakValues_[i] - static_cast<float>(segStep) / maxAmp);
            }
        }

        // Snapped to a segment boundary, drawn once it clears the bar.
        const int peakH  = static_cast<int>(peakValues_[i] * maxAmp);
        const int peakDy = (peakH / segStep) * segStep;
        if (peakDy > barH) {
            c.fill_rect(barX, barBottom - peakDy - segH + 1, barW, segH,
                        recess(t.background,t.vizWave, 0.55f));
        }
    }
}

bool OscilloscopeModule::bars_at_rest() const {
    for (int i = 0; i < NUM_BARS; ++i) {
        if (barSmoothed_[i] > 0.0f || peakValues_[i] > 0.0f) return false;
    }
    return true;
}

}  // namespace pt::ui
