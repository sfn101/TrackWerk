// PROJECT > INTERP (specs/sample-interpolation.md). Portable, no assert: CI runs it on Linux and
// Windows. Usage: ptsample-interp-check [scratch folder]
#include "audio-engine.h"
#include "sample-interp.h"
#include "sampler-voice.h"
#include "songcore/host.h"
#include "songcore/project_io.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

const double kPi = 3.14159265358979323846;

// A sine of `cycles` per frame, `len` frames long.
std::vector<float> sine(double cycles, int len) {
    std::vector<float> v(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) v[i] = static_cast<float>(std::sin(2 * kPi * cycles * i));
    return v;
}

// Plays `d` at `rate` frames per output frame; returns the RMS error against the true sine, or the
// RMS of the output when `cycles` is 0.
double play(const std::vector<float>& d, double cycles, double rate, int mode) {
    const int len = static_cast<int>(d.size());
    double err = 0.0;
    int n = 0;
    for (double pos = 64; pos < len - 64; pos += rate, ++n) {
        const int idx = static_cast<int>(pos);
        const float v = interp_read(d.data(), len, idx, static_cast<float>(pos - idx), mode, static_cast<float>(rate));
        const double want = cycles > 0 ? std::sin(2 * kPi * cycles * pos) : 0.0;
        err += (v - want) * (v - want);
    }
    return std::sqrt(err / n);
}

void reads() {
    std::vector<float> noise(4096);
    uint32_t r = 12345;
    for (float& x : noise) { r = r * 1664525u + 1013904223u; x = static_cast<int32_t>(r) / 2147483648.0f; }
    bool same = true;
    for (int idx = 0; idx < 4000; idx += 7)
        for (float frac : {0.0f, 0.25f, 0.5f, 0.999f})
            same &= interp_read(noise.data(), 4096, idx, frac, INTERP_LINEAR, 1.0f) == Playhead::read(noise.data(), idx, frac, 0);
    check(same, "LINEAR is today's read, bit for bit");
    check(interp_read(noise.data(), 4096, 10, 0.9f, INTERP_NONE, 1.0f) == noise[10], "NONE takes the frame itself");

    // Pitched down an octave, a 2 kHz tone at 48 kHz: the error against the true sine.
    const auto mid = sine(2000.0 / 48000.0, 20000);
    const double lin = play(mid, 2000.0 / 48000.0, 0.5, INTERP_LINEAR);
    const double cub = play(mid, 2000.0 / 48000.0, 0.5, INTERP_CUBIC);
    std::printf("      2 kHz pitched down, error: LINEAR %.5f  CUBIC %.5f\n", lin, cub);
    check(cub < lin / 4, "CUBIC beats LINEAR on a tone pitched down");
    // CUBIC is near exact on a low tone; on a bright one, where pitched-down samples go dull, SINC 8 wins.
    const auto bright = sine(12000.0 / 48000.0, 20000);
    const double bLin = play(bright, 12000.0 / 48000.0, 0.5, INTERP_LINEAR);
    const double bCub = play(bright, 12000.0 / 48000.0, 0.5, INTERP_CUBIC);
    const double bSnc = play(bright, 12000.0 / 48000.0, 0.5, INTERP_SINC8);
    std::printf("      12 kHz pitched down, error: LINEAR %.5f  CUBIC %.5f  SINC 8 %.5f\n", bLin, bCub, bSnc);
    check(bCub < bLin && bSnc < bCub, "on a bright tone SINC 8 beats CUBIC, which beats LINEAR");

    // Pitched up ×2, a 20 kHz tone lands above the output's Nyquist: all of it would alias.
    const auto high = sine(20000.0 / 48000.0, 20000);
    const double aliasLin = play(high, 0.0, 2.0, INTERP_LINEAR);
    const double aliasLp  = play(high, 0.0, 2.0, INTERP_SINC8_LP);
    std::printf("      pitched up, alias left: LINEAR %.4f  SINC 8 LP %.4f\n", aliasLin, aliasLp);
    check(aliasLp < aliasLin / 10, "SINC 8 LP removes at least 20 dB more aliasing than LINEAR");

    // The edges read inside the buffer.
    const float edge = interp_read(mid.data(), 20000, 0, 0.5f, INTERP_SINC8, 1.0f) +
                       interp_read(mid.data(), 20000, 19998, 0.5f, INTERP_SINC8, 1.0f);
    check(std::isfinite(edge), "SINC 8 reads safely at the first and last frames");
}

void songs_and_limit() {
    using namespace songcore;
    auto old = parse_project(json::parse("{\"version\":1}"));
    normalize_and_migrate(old);
    check(old.interpMode == INTERP_LINEAR, "a song without the field opens as LINEAR");

    auto engine = std::make_unique<AudioEngine>();
    engine->setDeviceSampleRate(48000);
    SongcoreHost host(engine.get(), 48000);
    host.new_project();
    check(host.project().interpMode == INTERP_CUBIC, "a new song starts on the factory default, CUBIC");
    const json saved = json::parse(serialize_project(host.project()));
    check(saved.value("interp", -1) == INTERP_CUBIC && saved["minReader"] == 3,
          "a song not on LINEAR saves its mode as format 3");
    check(check_format(saved, FileFormat{2, 2}) == FormatCheck::TOO_NEW, "an app before INTERP refuses it");
    host.set_new_song_interp(INTERP_SINC8_LP);
    host.new_project();
    check(host.project().interpMode == INTERP_SINC8_LP, "a new song follows INTERP DEF");

    engine->setInterpMode(INTERP_SINC8_LP);
    engine->setInterpLimit(INTERP_CUBIC);
    check(engine->interpLimited() && host.project().interpMode == INTERP_SINC8_LP,
          "above INTERP MAX the song plays limited and keeps its own mode");
    engine->setInterpLimit(INTERP_SINC8_LP);
    check(!engine->interpLimited(), "at or under INTERP MAX nothing is limited");
}
}  // namespace

int main() {
    reads();
    songs_and_limit();
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
