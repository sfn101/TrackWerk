// ACID BOX and REESE BASS (models 126 and 127, Studio > Synth > Vintage): the filter sweep of the acid line, its
// accent, the beating of the detuned saws, the sub, and the guard rails — measured through studio::Synth at 48 kHz.
#include "bass_synths.h"
#include "kiss_fftr.h"
#include "synth.h"
#include "synth_params.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace studio;

namespace {

constexpr float kRate = 48000.f;
constexpr int kFft = 8192;


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


double frequency(const std::vector<float>& x, size_t from, size_t len) {
    double first = -1, last = -1;
    int n = 0;
    for (size_t i = from + 1; i < from + len; ++i)
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = double(i - 1) + double(-x[i - 1]) / double(x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            ++n;
        }
    return n < 2 ? 0.0 : (n - 1) * kRate / (last - first);
}
double bright(const std::vector<float>& x, size_t from) { return range(spectrum(x, from), 600.f, 6000.f); }
double centroid(const std::vector<double>& p) {
    double s = 0, e = 0;
    for (size_t k = 1; k < p.size(); ++k) { s += p[k] * k; e += p[k]; }
    return s / e * kRate / kFft;
}




std::vector<float> note(int model, int a, int b, int c, const std::vector<std::pair<int, int>>& set, float hz, int frames) {
    Synth s;
    const int code = custom_code(model, a, b, c);
    auto params = default_synth_params();
    for (auto& kv : set) params[size_t(kv.first)] = kv.second;
    s.start(code);
    s.update(code, params);
    std::vector<float> out;
    for (int i = 0; i < frames; ++i) { const float v = s.next(hz, kRate); assert(std::isfinite(v)); out.push_back(v); }
    return out;
}
std::vector<float> slice(const std::vector<float>& x, size_t from, size_t n) { return std::vector<float>(x.begin() + long(from), x.begin() + long(from + n)); }

}  // namespace

int main() {
    assert(kAcidBoxModel == kSingFrogModel + 1 && kReeseBassModel == kAcidBoxModel + 1 && is_vintage_model(kReeseBassModel));
    assert(model_count() == kVintageFirstModel + kVintageModelCount && model_count() == 128);
    assert(std::string(model_name(kAcidBoxModel)) == "ACID BOX" && std::string(model_name(kReeseBassModel)) == "REESE BASS");
    assert(!synth_needs_strike(kAcidBoxModel) && !synth_needs_strike(kReeseBassModel));
    assert(macro_count(kAcidBoxModel) == 3 && std::string(macro_name(kAcidBoxModel, 2)) == "Env mod" &&
           std::string(macro_name(kReeseBassModel, 0)) == "Detune" && std::string(macro_name(kReeseBassModel, 2)) == "Movement");
    assert(synth_param_count(kAcidBoxModel) == 7 && synth_param_count(kReeseBassModel) == 7);
    assert(synth_param_value(kAcidBoxModel, 2, 32) == "Saw" && synth_param_value(kAcidBoxModel, 2, 96) == "Square" &&
           synth_param_value(kReeseBassModel, 2, 64) == "2 voices" && synth_param_value(kReeseBassModel, 2, 192) == "3 voices");
    assert(synth_param_adjust(kAcidBoxModel, 2, 32, 1, false) == 96 && synth_param_adjust(kReeseBassModel, 2, 64, 1, false) == 192 &&
           synth_param_adjust(kReeseBassModel, 2, 192, 1, false) == 192);
    assert(preset_name(preset_code(kAcidBoxModel, 0)) == "ACID BOX / Basic Line" && preset_name(preset_code(kReeseBassModel, 0)) == "REESE BASS / Neuro Growl");
    for (int m : {kAcidBoxModel, kReeseBassModel})
        for (int v = 0; v < 16; ++v) assert(valid_code(preset_code(m, v)));

    // ACID BOX: the pitch is where the note is; the cutoff sweeps down while the note rings.
    {
        const auto x = note(kAcidBoxModel, 100, 60, 100, {{3, 120}}, 110.f, 48000);
        const double f = frequency(x, 12000, 24000);
        std::printf("acid pitch %.1f Hz (110)\n", f);
        assert(f > 100.0 && f < 120.0);
        const double early = bright(x, 400), late = bright(x, 30000);
        std::printf("acid highs (0.6-6 kHz) early vs late: %.1f dB down\n", db(early / late));
        assert(early > 3.0 * late);
        // no envelope depth: no sweep
        const auto flat = note(kAcidBoxModel, 100, 60, 0, {{3, 120}}, 110.f, 48000);
        const double e2 = bright(flat, 400), l2 = bright(flat, 30000);
        assert(e2 < 1.5 * l2 && l2 < 1.5 * e2);
        // accent widens the sweep
        const auto accented = note(kAcidBoxModel, 100, 60, 100, {{3, 120}, {4, 255}}, 110.f, 48000);
        const auto plain = note(kAcidBoxModel, 100, 60, 100, {{3, 120}, {4, 0}}, 110.f, 48000);
        assert(bright(accented, 400) > 1.5 * bright(plain, 400));
        // the cutoff macro opens the filter; resonance puts a peak at the cutoff
        const auto closed = note(kAcidBoxModel, 10, 60, 0, {}, 110.f, 16000), open = note(kAcidBoxModel, 100, 60, 0, {}, 110.f, 16000);
        std::printf("acid cutoff macro: highs %.1f dB up\n", db(bright(open, 4000) / bright(closed, 4000)));
        assert(bright(open, 4000) > 10.0 * bright(closed, 4000));
        const auto lowRes = note(kAcidBoxModel, 35, 0, 0, {}, 55.f, 16000), highRes = note(kAcidBoxModel, 35, 125, 0, {}, 55.f, 16000);
        const double fc = AcidBox::cutoff_hz(35 / 127.f);
        assert(range(spectrum(highRes, 4000), fc * .8f, fc * 1.25f) > 2.0 * range(spectrum(lowRes, 4000), fc * .8f, fc * 1.25f));
        std::printf("acid sweep, accent, cutoff, resonance ok\n");
    }

    // REESE BASS: detuned saws beat; no detune, no beating. The sub adds energy an octave down.
    {
        auto beating = [](const std::vector<float>& x) {
            // spread of the 10 ms rectified-average envelope after a low-pass at the note
            std::vector<double> env;
            for (size_t i = 0; i + 480 <= x.size(); i += 240) { double e = 0; for (size_t j = 0; j < 480; ++j) e += std::fabs(x[i + j]); env.push_back(e / 480); }
            double mean = 0, var = 0;
            for (double v : env) mean += v;
            mean /= double(env.size());
            for (double v : env) var += (v - mean) * (v - mean);
            return std::sqrt(var / double(env.size())) / mean;
        };
        const auto still = note(kReeseBassModel, 0, 127, 0, {{3, 0}}, 55.f, 48000), wide = note(kReeseBassModel, 100, 127, 0, {{3, 0}}, 55.f, 48000);
        const double b0 = beating(slice(still, 4800, 40000)), b1 = beating(slice(wide, 4800, 40000));
        std::printf("reese beating depth: no detune %.3f, detuned %.3f\n", b0, b1);
        assert(b1 > 3.0 * b0 + .02);
        const auto noSub = note(kReeseBassModel, 50, 60, 0, {{3, 0}}, 110.f, 16000), withSub = note(kReeseBassModel, 50, 60, 0, {{3, 255}}, 110.f, 16000);
        assert(range(spectrum(withSub, 4000), 48.f, 62.f) > 2.0 * range(spectrum(noSub, 4000), 48.f, 62.f));
        const auto closed = note(kReeseBassModel, 50, 10, 0, {}, 55.f, 16000), open = note(kReeseBassModel, 50, 110, 0, {}, 55.f, 16000);
        std::printf("reese cutoff macro: highs %.1f dB up\n", db(bright(open, 4000) / bright(closed, 4000)));
        assert(bright(open, 4000) > 10.0 * bright(closed, 4000));
        std::printf("reese beating, sub, cutoff ok\n");
    }

    // Clean signal: no DC offset, and little that is not a harmonic of the note (aliasing folds a saw's upper
    // harmonics back between the real ones). Measured on a 1.76 kHz saw with the filter wide open.
    {
        for (int m : {kAcidBoxModel, kReeseBassModel}) {
            const auto x = m == kAcidBoxModel ? note(m, 255, 40, 0, {{2, 32}, {3, 0}}, 1760.f, 16384) : note(m, 60, 255, 0, {{2, 64}, {3, 0}}, 1760.f, 16384);
            double mean = 0;
            for (size_t i = 4096; i < x.size(); ++i) mean += x[i];
            mean /= double(x.size() - 4096);
            const auto p = spectrum(x, 8000);
            double total = 0, harmonic = 0;
            for (size_t k = 1; k < p.size(); ++k) total += p[k];
            for (int n = 1; n * 1760.f < 23000.f; ++n) harmonic += range(p, n * 1760.f - 40.f, n * 1760.f + 40.f);
            // the reese has three detuned voices: its lines sit up to 40 cents apart, so allow a wider window
            if (m == kReeseBassModel) { harmonic = 0; for (int n = 1; n * 1760.f < 23000.f; ++n) harmonic += range(p, n * 1760.f * .975f, n * 1760.f * 1.025f); }
            {   // where is the off-harmonic energy? (debug aid, printed once per model)
                double best = 0; int bestBin = 0;
                for (size_t k = 5; k < p.size(); ++k) {
                    const float hz = float(k) * kRate / kFft;
                    const float ratio = hz / 1760.f;
                    if (std::fabs(ratio - std::round(ratio)) * 1760.f < 40.f) continue;
                    if (p[k] > best) { best = p[k]; bestBin = int(k); }
                }
                std::printf("  strongest off-harmonic bin %.0f Hz (%.1f dB below total)\n", float(bestBin) * kRate / kFft, db(total / best));
            }
            std::printf("%s: DC %.5f, off-harmonic energy %.1f dB below the total\n", model_name(m), mean, db(total / std::max(total - harmonic, 1e-30)));
            assert(std::fabs(mean) < .004);
            assert(db(total / std::max(total - harmonic, 1e-30)) > 22.0);
        }
    }

    // ACID BOX Sub is a sine one octave below the note (found by review: it was at the note itself).
    {
        const auto none = note(kAcidBoxModel, 90, 20, 0, {{3, 120}, {8, 0}}, 110.f, 16000), sub = note(kAcidBoxModel, 90, 20, 0, {{3, 120}, {8, 0}, {6, 255}}, 110.f, 16000);
        const auto a = spectrum(none, 4000), b = spectrum(sub, 4000);
        std::printf("acid sub: 55 Hz line %.1f dB over the plain note\n", db(range(b, 50.f, 60.f) / std::max(range(a, 50.f, 60.f), 1e-30)));
        assert(range(b, 50.f, 60.f) > 20.0 * range(a, 50.f, 60.f));
    }

    // Every named sound plays, stays bounded, and none is silent or far louder than the rest.
    for (int m : {kAcidBoxModel, kReeseBassModel}) {
        double quietest = 1, loudest = 0;
        for (int v = 0; v < 16; ++v) {
            Synth s;
            const int code = preset_code(m, v);
            s.start(code);
            double e = 0, pk = 0;
            for (int i = 0; i < 48000; ++i) { const float y = s.next(65.4f, kRate); assert(std::isfinite(y)); e += double(y) * y; pk = std::max(pk, double(std::fabs(y))); }
            const double r = std::sqrt(e / 48000);
            assert(pk < 1.0);
            quietest = std::min(quietest, r);
            loudest = std::max(loudest, r);
        }
        std::printf("%s: 16 sounds rms %.4f ... %.4f\n", model_name(m), quietest, loudest);
        assert(quietest > .004 && loudest / quietest < 15.0);
    }

    // Nothing runs away.
    {
        int cases = 0;
        for (int m : {kAcidBoxModel, kReeseBassModel})
            for (int p2 : {32, 96, 64, 192})
                for (int p3 : {0, 255})
                    for (int p4 : {0, 255})
                        for (int p5 : {0, 255})
                            for (int ma : {0, 127})
                                for (int mb : {0, 127})
                                    for (int mc : {0, 127}) {
                                        for (float hz : {20.f, 55.f, 880.f, 4000.f}) {
                                            const auto x = note(m, ma, mb, mc, {{2, p2}, {3, p3}, {4, p4}, {5, p5}, {6, 255}}, hz, 2400);
                                            assert(peak(x) < 1.0);
                                        }
                                        ++cases;
                                    }
        std::printf("%d extreme settings x 4 pitches: finite and below 1.0\n", cases);
    }
    std::printf("BASS_SYNTHS_PASS\n");
}
