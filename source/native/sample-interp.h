#pragma once
#include <algorithm>
#include <cmath>

// PROJECT > INTERP (specs/sample-interpolation.md): how a sample is read between its frames.
// ⚠️ The numbers are what songs and settings.json store: append, never reorder.
enum InterpMode { INTERP_NONE = 0, INTERP_LINEAR = 1, INTERP_CUBIC = 2, INTERP_SINC8 = 3, INTERP_SINC8_LP = 4 };
inline constexpr int INTERP_MODE_COUNT = 5;
inline const char* interp_name(int m) {
    static const char* const names[INTERP_MODE_COUNT] = {"NONE", "LINEAR", "CUBIC", "SINC 8", "SINC 8 LP"};
    return names[std::clamp(m, 0, INTERP_MODE_COUNT - 1)];
}

// The 8-tap windowed-sinc tables: PHASES fractional positions × 8 taps, one table per cutoff. Table 0
// is SINC 8 (cutoff just under Nyquist); SINC 8 LP picks a lower cutoff as a sample is pitched up, so
// the frequencies that would fold back (alias) are removed first. Built once, before audio starts.
struct SincTables {
    static constexpr int PHASES = 512, TAPS = 8, CUTOFFS = 4;
    float k[CUTOFFS][PHASES + 1][TAPS];

    static SincTables& get() {
        static SincTables t;   // built by the first call, which AudioEngine's constructor makes
        return t;
    }

    // The table for a playback rate (source frames per output frame).
    static int table_for(float rate) {
        rate = std::fabs(rate);
        return rate <= 1.25f ? 0 : rate <= 1.75f ? 1 : rate <= 2.5f ? 2 : 3;
    }

private:
    SincTables() {
        const double cutoffs[CUTOFFS] = {0.95, 0.95 / 1.5, 0.95 / 2.0, 0.95 / 3.0};
        const double pi = 3.14159265358979323846;
        for (int c = 0; c < CUTOFFS; ++c)
            for (int p = 0; p <= PHASES; ++p) {
                const double frac = static_cast<double>(p) / PHASES;
                double sum = 0.0, w[TAPS];
                for (int t = 0; t < TAPS; ++t) {
                    const double x = (t - 3) - frac;   // taps sit at idx-3 … idx+4
                    const double s = std::fabs(x) < 1e-9 ? 1.0 : std::sin(pi * cutoffs[c] * x) / (pi * cutoffs[c] * x);
                    // Blackman window over ±4 frames.
                    const double n = (x + 4.0) / 8.0;
                    const double win = 0.42 - 0.5 * std::cos(2 * pi * n) + 0.08 * std::cos(4 * pi * n);
                    w[t] = s * win;
                    sum += w[t];
                }
                for (int t = 0; t < TAPS; ++t) k[c][p][t] = static_cast<float>(w[t] / sum);   // unity gain at DC
            }
    }
};

// The read at `idx` + `frac` of a `len`-frame buffer. Frames outside it count as its first or last.
// shortcut: a loop's seam is read with the frames beyond it, not wrapped to the loop's other end, so a
// wide mode can soften a click there by a few frames; wrap them if loop seams ever sound wrong.
inline float interp_read(const float* d, int len, int idx, float frac, int mode, float rate) {
    const auto at = [&](int i) { return d[std::clamp(i, 0, len - 1)]; };
    switch (mode) {
        case INTERP_NONE:
            return d[idx];
        case INTERP_CUBIC: {   // Catmull-Rom through the four nearest frames
            const float y0 = at(idx - 1), y1 = d[idx], y2 = at(idx + 1), y3 = at(idx + 2);
            const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c = -0.5f * y0 + 0.5f * y2;
            return ((a * frac + b) * frac + c) * frac + y1;
        }
        case INTERP_SINC8:
        case INTERP_SINC8_LP: {
            const SincTables& t = SincTables::get();
            const int table = mode == INTERP_SINC8_LP ? SincTables::table_for(rate) : 0;
            const float* w = t.k[table][static_cast<int>(frac * SincTables::PHASES + 0.5f)];
            float sum = 0.0f;
            if (idx >= 3 && idx + 4 < len) {
                const float* p = d + idx - 3;
                for (int i = 0; i < SincTables::TAPS; ++i) sum += p[i] * w[i];
            } else {
                for (int i = 0; i < SincTables::TAPS; ++i) sum += at(idx - 3 + i) * w[i];
            }
            return sum;
        }
        default: {   // LINEAR: today's read, unchanged
            const float a = d[idx], b = d[idx + 1];
            return a + (b - a) * frac;
        }
    }
}
