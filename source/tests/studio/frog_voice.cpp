// CROAKER (model 124, Studio > Synth > Vintage): measured through studio::Synth at 48 kHz. A frog
// croaks: the croak envelope chops the tone into pulses at the croak rate, the species decide where the
// formants sit, the throat moves them, and nothing runs away.
#include "frog_voice.h"
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

// Parameter rows: 2 Species, 3 Pulses, 4 Croak depth, 5 Throat, 6 Chirp, 7 Bubble.
struct Patch {
    int species = 0, pulses = 64, depth = 255, throat = 128, chirp = 0, bubble = 0;
    int rate = 128, throatMacro = 128, dip = 0;   // macros 0..255
};
SynthParams params(const Patch& p) {
    auto s = default_synth_params();
    s[2] = p.species * 64 + 32;
    s[3] = p.pulses;
    s[4] = p.depth;
    s[5] = p.throat;
    s[6] = p.chirp;
    s[7] = p.bubble;
    return s;
}
std::vector<float> render(const Patch& p, float hz, int frames, int skip = 0) {
    Synth s;
    const int code = custom_code(kFrogVoiceModel, p.rate, p.throatMacro, p.dip);
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
// Loudness humps: 20 ms rectified-average windows, counted at rising crossings of the middle level.
int humps(const std::vector<float>& x) {
    const size_t w = 960;
    std::vector<double> env;
    for (size_t i = 0; i + w <= x.size(); i += w / 2) {
        double e = 0;
        for (size_t j = 0; j < w; ++j) e += std::fabs(x[i + j]);
        env.push_back(e / w);
    }
    double hi = 0, lo = 1e9;
    for (double v : env) { hi = std::max(hi, v); lo = std::min(lo, v); }
    const double mid = lo + .4 * (hi - lo);
    int n = 0;
    for (size_t i = 1; i < env.size(); ++i) if (env[i - 1] < mid && env[i] >= mid) ++n;
    return n;
}
double centroid(const std::vector<double>& p) {
    double s = 0, e = 0;
    for (size_t k = 1; k < p.size(); ++k) { s += p[k] * k; e += p[k]; }
    return s / e * kRate / kFft;
}

}  // namespace

int main() {
    assert(kFrogVoiceModel == kDubSirenModel + 1 && is_vintage_model(kFrogVoiceModel));
    assert(model_count() == kVintageFirstModel + kVintageModelCount && model_count() == 128);
    assert(std::string(model_name(kFrogVoiceModel)) == "CROAKER" && !synth_needs_strike(kFrogVoiceModel));
    assert(macro_count(kFrogVoiceModel) == 3 && std::string(macro_name(kFrogVoiceModel, 0)) == "Croak" &&
           std::string(macro_name(kFrogVoiceModel, 1)) == "Throat" && std::string(macro_name(kFrogVoiceModel, 2)) == "Dip");
    assert(synth_param_count(kFrogVoiceModel) == 8 && std::string(synth_param_name(kFrogVoiceModel, 2)) == "Species" &&
           std::string(synth_param_name(kFrogVoiceModel, 7)) == "Bubble");
    assert(synth_param_value(kFrogVoiceModel, 2, 32) == "Tree frog" && synth_param_value(kFrogVoiceModel, 2, 224) == "Peeper" &&
           synth_param_value(kFrogVoiceModel, 3, 0) == "1 pulse" && synth_param_value(kFrogVoiceModel, 3, 200) == "4 pulses");
    assert(synth_param_adjust(kFrogVoiceModel, 2, 32, 1, false) == 96 && synth_param_adjust(kFrogVoiceModel, 3, 224, 1, false) == 224);
    assert(preset_name(preset_code(kFrogVoiceModel, 0)) == "CROAKER / Pond Starter");
    for (int v = 0; v < 16; ++v) assert(valid_code(preset_code(kFrogVoiceModel, v)));
    assert(uri_code(uri(preset_code(kFrogVoiceModel, 5)).c_str()) == preset_code(kFrogVoiceModel, 5));

    // Every named sound plays, stays bounded, is not silent, and the sixteen are not all alike.
    {
        double quietest = 1, loudest = 0;
        std::vector<double> centroids;
        for (int v = 0; v < 16; ++v) {
            Synth s;
            const int code = preset_code(kFrogVoiceModel, v);
            s.start(code);
            std::vector<float> x;
            for (int i = 0; i < 24000; ++i) { const float y = s.next(110.f, kRate); assert(std::isfinite(y)); x.push_back(y); }
            const double r = rms(x);
            assert(peak(x) < 1.0);
            quietest = std::min(quietest, r);
            loudest = std::max(loudest, r);
            centroids.push_back(centroid(spectrum(x, 4000)));
        }
        std::printf("16 sounds: rms %.4f ... %.4f, centroid %.0f ... %.0f Hz\n", quietest, loudest,
                    *std::min_element(centroids.begin(), centroids.end()), *std::max_element(centroids.begin(), centroids.end()));
        assert(quietest > .005 && loudest / quietest < 12.0);
        assert(*std::max_element(centroids.begin(), centroids.end()) > 1.5 * *std::min_element(centroids.begin(), centroids.end()));
    }

    // Croak: full depth chops the tone into pulses at rate x pulses; zero depth does not.
    {
        Patch p; p.rate = 128;   // 0.5 * 2^(5*128/255) = 2.84 croaks per second
        for (int pulses : {1, 2, 3}) {
            p.pulses = (pulses - 1) * 64;
            const int n = humps(render(p, 110.f, 96000, 4800));   // two seconds
            const double expected = 2.0 * 2.84 * pulses;
            std::printf("%d pulse(s): %d humps in 2 s, expected about %.1f\n", pulses, n, expected);
            assert(std::fabs(n - expected) <= 2.0 + .15 * expected);
        }
        Patch flat = p; flat.pulses = 64; flat.depth = 0;
        const auto x = render(flat, 110.f, 48000, 4800);
        double hi = 0, lo = 1e9;
        for (size_t i = 0; i + 960 <= x.size(); i += 960) { double e = 0; for (size_t j = 0; j < 960; ++j) e += std::fabs(x[i + j]); e /= 960; hi = std::max(hi, e); lo = std::min(lo, e); }
        std::printf("depth 0: loudness window ratio %.2f\n", hi / lo);
        assert(lo > 0 && hi / lo < 1.3);
    }

    // Species: a bullfrog is darker than a peeper, a big throat darker than a small one.
    {
        Patch bull, peep; bull.species = 1; peep.species = 3; bull.depth = peep.depth = 0;
        const double cb = centroid(spectrum(render(bull, 110.f, 16000, 4800), 3000)), cp = centroid(spectrum(render(peep, 110.f, 16000, 4800), 3000));
        std::printf("centroid bullfrog %.0f Hz, peeper %.0f Hz\n", cb, cp);
        assert(cp > 2.0 * cb);
        Patch small = bull, big = bull; small.throatMacro = 0; big.throatMacro = 255;
        const double cs = centroid(spectrum(render(small, 110.f, 16000, 4800), 3000)), cg = centroid(spectrum(render(big, 110.f, 16000, 4800), 3000));
        std::printf("throat macro 0 %.0f Hz, 255 %.0f Hz\n", cs, cg);
        assert(cg > 1.3 * cs);
    }

    // Dip: the pitch bends down within a pulse; without it the tone stays where it is.
    {
        Patch steady, dipped; steady.depth = dipped.depth = 0; dipped.dip = 255; dipped.rate = 100; steady.rate = 100;
        const auto a = render(steady, 220.f, 24000, 4800), b = render(dipped, 220.f, 24000, 4800);
        // The formants dominate the zero crossings, so the pitch is read from the fundamental's own band:
        // a steady tone puts its power in one narrow line at 220 Hz, a bending one smears it away.
        const double steadyLine = range(spectrum(a, 2000), 214.f, 226.f), dippedLine = range(spectrum(b, 2000), 214.f, 226.f);
        std::printf("line at 220 Hz: with full dip %.1f dB below steady\n", db(steadyLine / dippedLine));
        assert(db(steadyLine / dippedLine) > 3.0);
    }

    // Nothing runs away.
    {
        int cases = 0;
        for (int species = 0; species < 4; ++species)
            for (int pulses : {0, 64, 128, 192})
                for (int depth : {0, 255})
                    for (int throat : {0, 255})
                        for (int bubble : {0, 255})
                            for (int rate : {0, 255})
                                for (int dip : {0, 255}) {
                                    Patch p; p.species = species; p.pulses = pulses; p.depth = depth; p.throat = throat; p.bubble = bubble;
                                    p.rate = rate; p.dip = dip; p.throatMacro = throat; p.chirp = 255;
                                    for (float hz : {20.f, 440.f, 4000.f}) assert(peak(render(p, hz, 4800)) < 1.0);
                                    ++cases;
                                }
        std::printf("%d extreme settings x 3 pitches: finite and below 1.0\n", cases);
    }
    std::printf("FROG_VOICE_PASS\n");
}
