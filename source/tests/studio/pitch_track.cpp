// PitchTracker (FROGGY's Mic pitch source): tones with harmonics at both usual sample rates are found within 1.5 %,
// a quiet input and white noise report no pitch, and a voice-like glide is followed.
#include "pitch_track.h"
#include "mic_pitch.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using studio::PitchTracker;

namespace {
std::vector<float> tone(float hz, int rate, int n, float amp = .4f, int harmonics = 4) {
    std::vector<float> x(size_t(n), 0.f);
    for (int h = 1; h <= harmonics; ++h)
        for (int i = 0; i < n; ++i) x[size_t(i)] += amp / float(h) * std::sin(6.2831853f * hz * float(h) * float(i) / float(rate));
    return x;
}
float measure(const std::vector<float>& x, int rate, float* confidence = nullptr) {
    PitchTracker t;
    PitchTracker::Result r;
    float sum = 0; int count = 0;
    for (size_t i = 0; i < x.size(); i += 512) {
        r = t.feed(x.data() + i, std::min<size_t>(512, x.size() - i), rate);
        if (r.fresh && i > x.size() / 2 && r.hz > 0) { sum += r.hz; ++count; if (confidence) *confidence = r.confidence; }
    }
    return count ? sum / float(count) : 0.f;
}
}  // namespace

int main() {
    for (int rate : {44100, 48000})
        for (float hz : {82.4f, 110.f, 146.8f, 196.f, 261.6f, 330.f, 440.f}) {
            float conf = 0;
            const float got = measure(tone(hz, rate, rate), rate, &conf);
            std::printf("%d Hz: %.1f Hz -> %.2f (conf %.2f)\n", rate, hz, got, conf);
            assert(got > 0 && std::fabs(got - hz) < .015f * hz && conf > .85f);
        }
    // quiet: nothing
    assert(measure(tone(196.f, 48000, 48000, .001f), 48000) == 0.f);
    // white noise: no clear pitch (an occasional false hit is allowed, a stable one is not)
    {
        std::vector<float> n(48000);
        unsigned s = 12345;
        for (auto& v : n) { s = s * 1664525u + 1013904223u; v = float(int(s >> 9) - (1 << 22)) / float(1 << 22) * .3f; }
        PitchTracker t;
        int hits = 0, windows = 0;
        for (size_t i = 0; i < n.size(); i += 512) { const auto r = t.feed(n.data() + i, std::min<size_t>(512, n.size() - i), 48000); if (r.fresh) { ++windows; if (r.hz > 0) ++hits; } }
        std::printf("noise: %d of %d windows gave a pitch\n", hits, windows);
        assert(hits * 10 < windows);
    }
    // a glide 150 -> 300 Hz over one second is followed
    {
        std::vector<float> g(48000);
        double phase = 0;
        for (int i = 0; i < 48000; ++i) { const double f = 150.0 + 150.0 * i / 48000.0; phase += 6.2831853 * f / 48000.0; g[size_t(i)] = float(.4 * std::sin(phase) + .2 * std::sin(2 * phase)); }
        PitchTracker t;
        float firstHz = 0, lastHz = 0;
        for (size_t i = 0; i < g.size(); i += 512) {
            const auto r = t.feed(g.data() + i, std::min<size_t>(512, g.size() - i), 48000);
            if (r.fresh && r.hz > 0) { if (firstHz == 0 && i > 6000) firstHz = r.hz; lastHz = r.hz; }
        }
        std::printf("glide: first %.0f Hz, last %.0f Hz\n", firstHz, lastHz);
        assert(firstHz > 140 && firstHz < 185 && lastHz > 270 && lastHz < 310);
    }
    studio::mic_pitch_publish(220.f);
    assert(studio::mic_pitch_read() == 220.f);
    studio::mic_pitch_publish(0.f);
    std::printf("PITCH_TRACK_PASS\n");
}
