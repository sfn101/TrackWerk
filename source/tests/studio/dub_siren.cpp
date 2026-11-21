// DUB SIREN (model 123, Studio > Synth > Vintage): measured through studio::Synth at 48 kHz. Each
// check names the property a player of a dub siren would listen for: the pitch is where the note is,
// the LFO bends it by the stated amount, the sweep goes up or down over its time, the filter closes,
// the ping is a burst at the start - and nothing runs away.
#include "dub_siren.h"
#include "kiss_fftr.h"
#include "synth.h"
#include "synth_params.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace studio;

namespace {

constexpr float kRate = 48000.f;
constexpr int kFft = 8192;

// Parameter rows: 2 Wave, 3 LFO shape, 4 Cutoff, 5 Resonance, 6 Sweep time, 7 Fine tune, 8 Ping.
struct Patch {
    int wave = 0, lfo = 0, cutoff = 255, resonance = 0, sweepTime = 0, fine = 128, ping = 0;
    int rate = 128, depth = 0, sweep = 128;   // macros 0..255
};
SynthParams params(const Patch& p) {
    auto s = default_synth_params();
    s[2] = p.wave * 64 + 32;
    s[3] = p.lfo * 32 + 16;
    s[4] = p.cutoff;
    s[5] = p.resonance;
    s[6] = p.sweepTime;
    s[7] = p.fine;
    s[8] = p.ping;
    return s;
}
int code_of(const Patch& p) { return custom_code(kDubSirenModel, p.rate, p.depth, p.sweep); }
std::vector<float> render(const Patch& p, float hz, int frames, int skip = 0) {
    Synth s;
    const int code = code_of(p);
    s.start(code);
    s.update(code, params(p));
    std::vector<float> out;
    for (int i = 0; i < skip + frames; ++i) {
        const float v = s.next(hz, kRate);
        assert(std::isfinite(v));
        if (i >= skip) out.push_back(v);
    }
    return out;
}
// Frequency from the positive-going zero crossings of x[from, from+len), interpolated.
double frequency(const std::vector<float>& x, size_t from, size_t len) {
    double first = -1, last = -1;
    int n = 0;
    for (size_t i = from + 1; i < from + len; ++i) {
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = double(i - 1) + double(-x[i - 1]) / double(x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            ++n;
        }
    }
    return n < 2 ? 0.0 : (n - 1) * kRate / (last - first);
}
std::vector<double> spectrum(const std::vector<float>& x, size_t from = 0) {
    kiss_fftr_cfg cfg = kiss_fftr_alloc(kFft, 0, nullptr, nullptr);
    std::vector<float> w(kFft);
    for (int i = 0; i < kFft; ++i) w[size_t(i)] = x[from + size_t(i)] * (.5f - .5f * std::cos(6.2831853f * i / (kFft - 1)));
    std::vector<kiss_fft_cpx> bins(kFft / 2 + 1);
    kiss_fftr(cfg, w.data(), bins.data());
    kiss_fftr_free(cfg);
    std::vector<double> p(bins.size());
    for (size_t k = 0; k < bins.size(); ++k) p[k] = double(bins[k].r) * bins[k].r + double(bins[k].i) * bins[k].i;
    return p;
}
int bin_of(float hz) { return int(std::lround(hz * kFft / kRate)); }
double range(const std::vector<double>& p, float lo, float hi) {
    double e = 0;
    for (int k = std::max(0, bin_of(lo)); k <= std::min(int(p.size()) - 1, bin_of(hi)); ++k) e += p[size_t(k)];
    return e;
}
double harmonic(const std::vector<double>& p, float hz, int n) { return range(p, hz * n - 12.f, hz * n + 12.f); }
double db(double powerRatio) { return 10 * std::log10(std::max(powerRatio, 1e-30)); }
double rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return std::sqrt(e / x.size()); }
double peak(const std::vector<float>& x) { double m = 0; for (float v : x) m = std::max(m, double(std::fabs(v))); return m; }
bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance * std::fabs(b); }

}  // namespace

int main() {
    // Registration: the Vintage family's fourth model, three macros, nine parameters, sixteen sounds.
    assert(kDubSirenModel == kFmSysexModel + 1 && is_vintage_model(kDubSirenModel));
    assert(model_count() == kVintageFirstModel + kVintageModelCount && model_count() == 128);
    assert(std::string(model_name(kDubSirenModel)) == "DUB SIREN" && !synth_needs_strike(kDubSirenModel));
    assert(macro_count(kDubSirenModel) == 3 && std::string(macro_name(kDubSirenModel, 0)) == "LFO rate" &&
           std::string(macro_name(kDubSirenModel, 1)) == "LFO depth" && std::string(macro_name(kDubSirenModel, 2)) == "Sweep");
    assert(synth_param_count(kDubSirenModel) == 9 && std::string(synth_param_name(kDubSirenModel, 2)) == "Wave" &&
           std::string(synth_param_name(kDubSirenModel, 3)) == "LFO shape" && std::string(synth_param_name(kDubSirenModel, 8)) == "Ping");
    assert(synth_param_value(kDubSirenModel, 2, 32) == "Sine" && synth_param_value(kDubSirenModel, 2, 224) == "Triangle" &&
           synth_param_value(kDubSirenModel, 3, 16) == "Triangle" && synth_param_value(kDubSirenModel, 3, 240) == "Random S&H" &&
           synth_param_value(kDubSirenModel, 3, 144) == "Dual warble" && synth_param_value(kDubSirenModel, 7, 128) == "+0 cents" &&
           synth_param_value(kDubSirenModel, 2, -1) == "Factory");
    assert(synth_param_adjust(kDubSirenModel, 2, 32, 1, false) == 96 && synth_param_adjust(kDubSirenModel, 2, 224, 1, true) == 224 &&
           synth_param_adjust(kDubSirenModel, 3, 16, 1, false) == 48 && synth_param_adjust(kDubSirenModel, 3, 16, -1, false) == 16 &&
           synth_param_adjust(kDubSirenModel, 3, 240, 1, true) == 240);
    assert(preset_name(preset_code(kDubSirenModel, 0)) == "DUB SIREN / Sonar Ping" && variation_count() == 16);
    for (int v = 0; v < 16; ++v) assert(valid_code(preset_code(kDubSirenModel, v)));
    assert(uri_code(uri(preset_code(kDubSirenModel, 5)).c_str()) == preset_code(kDubSirenModel, 5));
    assert(uri_code(custom_uri(kDubSirenModel, 10, 20, 30).c_str()) == custom_code(kDubSirenModel, 10, 20, 30));
    assert(!valid_code(preset_code(kReeseBassModel + 1, 0)) && preset_code(kReeseBassModel + 1, 0) == -1);
    assert(std::fabs(DubSiren::lfo_hz(0.f) - .1f) < 1e-4f && std::fabs(DubSiren::lfo_hz(1.f) - 20.f) < .2f);

    // Every named sound plays, stays bounded and is neither silent nor quiet beside the others.
    {
        double quietest = 1, loudest = 0;
        for (int v = 0; v < variation_count(); ++v) {
            Synth s;
            const int code = preset_code(kDubSirenModel, v);
            s.start(code);
            s.update(code, default_synth_params());
            std::vector<float> x;
            for (int i = 0; i < 24000 + 9600; ++i) {
                const float y = s.next(196.f, kRate);
                assert(std::isfinite(y));
                if (i >= 9600) x.push_back(y);
            }
            assert(peak(x) < 1.0);
            quietest = std::min(quietest, rms(x));
            loudest = std::max(loudest, rms(x));
        }
        std::printf("16 sounds at G3: rms %.3f … %.3f (%.1f dB)\n", quietest, loudest, db(loudest * loudest / (quietest * quietest)));
        assert(quietest > .04 && db(loudest * loudest / (quietest * quietest)) < 12.0);
    }

    // Pitch: a flat sine (no LFO, no sweep, filter open) sits on the note and is a pure tone.
    {
        const auto x = render(Patch{}, 220.f, 24000, 4800);
        assert(near(frequency(x, 0, 24000), 220.0, .005));
        const auto p = spectrum(x, 4000);
        assert(db(harmonic(p, 220.f, 2) / harmonic(p, 220.f, 1)) < -50.0 && db(harmonic(p, 220.f, 3) / harmonic(p, 220.f, 1)) < -50.0);
        assert(peak(x) > .3 && peak(x) < .9);
        // Fine tune: +50 cents and -50 cents.
        Patch up; up.fine = 255;
        Patch down; down.fine = 0;
        const double fu = frequency(render(up, 220.f, 24000, 4800), 0, 24000), fd = frequency(render(down, 220.f, 24000, 4800), 0, 24000);
        assert(near(fu / 220.0, std::exp2(127.0 / 128 * 50 / 1200), .002) && near(fd / 220.0, std::exp2(-50.0 / 1200), .002));
    }

    // The four oscillators: sine has no overtones, square only odd ones (3rd ≈ −9.5 dB), the falling saw
    // all of them (2nd ≈ −6 dB), the triangle only weak odd ones (3rd ≈ −19 dB).
    {
        auto third = [](int wave) {
            Patch p; p.wave = wave;
            const auto x = render(p, 220.f, 16000, 4800);
            const auto s = spectrum(x, 3000);
            return std::array<double, 2>{db(harmonic(s, 220.f, 2) / harmonic(s, 220.f, 1)), db(harmonic(s, 220.f, 3) / harmonic(s, 220.f, 1))};
        };
        const auto sine = third(0), square = third(1), saw = third(2), tri = third(3);
        std::printf("harmonics 2nd/3rd (dB): sine %.1f/%.1f square %.1f/%.1f saw %.1f/%.1f triangle %.1f/%.1f\n",
                    sine[0], sine[1], square[0], square[1], saw[0], saw[1], tri[0], tri[1]);
        assert(sine[0] < -50 && square[0] < -40 && near(square[1], -9.5, .08) && near(saw[0], -6.0, .1) &&
               near(saw[1], -9.5, .08) && tri[0] < -40 && near(tri[1], -19.1, .06));
    }

    // LFO: depth 0.5 bends the pitch by ±0.75 octave (a triangle LFO at 4 Hz), depth 0 does not.
    {
        Patch p; p.depth = 127; p.rate = 160;   // 0.1·2^(7.64·rate/255) ≈ 4.2 Hz
        const auto x = render(p, 220.f, 96000, 4800);
        double lo = 1e9, hi = 0;
        for (size_t at = 0; at + 2400 <= x.size(); at += 1200) {
            const double f = frequency(x, at, 2400);
            if (f > 0) { lo = std::min(lo, f); hi = std::max(hi, f); }
        }
        std::printf("LFO depth 0.5: %.0f … %.0f Hz (ratio %.2f, ideal %.2f)\n", lo, hi, hi / lo, std::exp2(1.5));
        assert(hi / lo > 2.4 && hi / lo < 3.3);
        Patch flat; flat.rate = 160;
        const auto y = render(flat, 220.f, 48000, 4800);
        for (size_t at = 0; at + 2400 <= y.size(); at += 4800) assert(near(frequency(y, at, 2400), 220.0, .01));
    }

    // Sweep: macro C at the top rises an octave over the sweep time, at the bottom it falls an octave,
    // in the middle it stays; the ramp is half done at half the time.
    {
        Patch up; up.sweep = 254; up.sweepTime = 150;   // 0.05·2^(7·150/255) ≈ 0.87 s
        const double seconds = DubSiren::sweep_seconds(150 / 255.f);
        const auto x = render(up, 220.f, 144000);
        const double start = frequency(x, 0, 1200), mid = frequency(x, size_t(seconds * .5 * kRate) - 1200, 2400),
                     end = frequency(x, size_t((seconds + .5) * kRate), 4800);
        std::printf("sweep up over %.2f s: start %.0f mid %.0f end %.0f Hz\n", seconds, start, mid, end);
        assert(near(start, 220.0, .03) && near(mid, 220.0 * std::exp2(.5), .04) && near(end, 440.0, .01));
        Patch down = up; down.sweep = 0;
        const auto y = render(down, 220.f, 144000);
        assert(near(frequency(y, size_t((seconds + .5) * kRate), 4800), 110.0, .01));
        Patch flat = up; flat.sweep = 128;   // the neutral band: exactly flat
        assert(near(frequency(render(flat, 220.f, 144000), size_t((seconds + .5) * kRate), 4800), 220.0, .005));
    }

    // All LFO shapes: each moves the pitch, Random S&H holds several different levels, and a second
    // render of the same note is identical (the note start resets every state).
    {
        for (int shape = 0; shape < DubSiren::kLfoCount; ++shape) {
            Patch p; p.lfo = shape; p.depth = 127; p.rate = 150;
            const auto a = render(p, 220.f, 72000, 2400), b = render(p, 220.f, 72000, 2400);
            assert(a == b);
            double lo = 1e9, hi = 0;
            for (size_t at = 0; at + 2400 <= a.size(); at += 1200) {
                const double f = frequency(a, at, 2400);
                if (f > 0) { lo = std::min(lo, f); hi = std::max(hi, f); }
            }
            assert(hi / lo > 1.3);
        }
        Patch sh; sh.lfo = 7; sh.depth = 160; sh.rate = 220;   // ≈ 9 Hz: many held levels in 3 s
        const auto x = render(sh, 220.f, 144000, 0);
        std::vector<double> levels;
        for (size_t at = 0; at + 1800 <= x.size(); at += 2400) {
            const double f = frequency(x, at, 1800);
            if (f <= 0) continue;
            bool known = false;
            for (double l : levels) known = known || near(f, l, .03);
            if (!known) levels.push_back(f);
        }
        assert(levels.size() >= 6);
    }

    // Filter: closing the cutoff takes the saw's highs away; resonance puts a peak at the cutoff.
    {
        Patch open; open.wave = 2;
        Patch closed = open; closed.cutoff = 100;   // 30·2^(9.5·100/255) ≈ 400 Hz
        const auto a = spectrum(render(open, 220.f, 16000, 4800), 3000), b = spectrum(render(closed, 220.f, 16000, 4800), 3000);
        const double highOpen = range(a, 2000.f, 8000.f), highClosed = range(b, 2000.f, 8000.f);
        std::printf("saw highs 2–8 kHz: open vs cutoff 400 Hz: %.1f dB down\n", db(highOpen / highClosed));
        assert(db(highOpen / highClosed) > 30.0);
        Patch plain = closed, reso = closed; reso.resonance = 230;
        const auto c = spectrum(render(plain, 100.f, 16000, 4800), 3000), d = spectrum(render(reso, 100.f, 16000, 4800), 3000);
        assert(range(d, 330.f, 470.f) > 4.0 * range(c, 330.f, 470.f));
    }

    // Ping: a burst at the start of the note, gone after the first second.
    {
        Patch quiet, loud; loud.ping = 255;
        const auto q = render(quiet, 110.f, 56000), l = render(loud, 110.f, 56000);
        auto burst = [](const std::vector<float>& x, size_t from) { return spectrum(x, from); };
        const double first = range(burst(l, 0), 1500.f, 3500.f), firstQuiet = range(burst(q, 0), 1500.f, 3500.f),
                     later = range(burst(l, 40000), 1500.f, 3500.f);
        std::printf("ping 1.5–3.5 kHz: first 0.17 s %.1f dB over no ping, 0.83 s later %.1f dB below the start\n",
                    db(first / firstQuiet), db(first / later));
        assert(first > 1000.0 * firstQuiet && later < first * 1e-3);
    }

    // Nothing runs away: every combination of the extremes stays finite and under the limiter.
    {
        int cases = 0;
        for (int wave = 0; wave < 4; ++wave)
            for (int shape : {0, 1, 6, 7})
                for (int cutoff : {0, 120, 255})
                    for (int resonance : {0, 255})
                        for (int depth : {0, 254})
                            for (int sweep : {0, 254}) {
                                Patch p; p.wave = wave; p.lfo = shape; p.cutoff = cutoff; p.resonance = resonance;
                                p.depth = depth; p.sweep = sweep; p.rate = 254; p.sweepTime = 0; p.ping = 255; p.fine = 255;
                                for (float hz : {20.f, 440.f, 4000.f}) assert(peak(render(p, hz, 4800)) < 1.0);
                                ++cases;
                            }
        std::printf("%d extreme settings × 3 pitches: finite and below 1.0\n", cases);
    }
    std::printf("DUB_SIREN_PASS\n");
}
