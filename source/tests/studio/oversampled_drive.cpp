// The oversampled drive (specs/oversampled-drive.md). Portable, no assert: CI runs it on Linux and
// Windows. Usage: ptoversampled-drive-check
#include "effects/modules/drive-module.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}
const double kPi = 3.14159265358979323846;
constexpr int kRate = 48000, kLen = 48000;

// The level of `hz` in `v` (a single-bin DFT), skipping the first 1000 frames.
double level_at(const std::vector<float>& v, double hz) {
    double re = 0, im = 0;
    int n = 0;
    for (size_t i = 1000; i < v.size(); ++i, ++n) {
        re += v[i] * std::cos(2 * kPi * hz * i / kRate);
        im += v[i] * std::sin(2 * kPi * hz * i / kRate);
    }
    return 2 * std::sqrt(re * re + im * im) / n;
}

std::vector<float> drive(bool oversample, double hz) {
    DriveModule d;
    d.reset();
    d.setDrive(255);
    d.setOversample(oversample);
    std::vector<float> out(kLen);
    for (int i = 0; i < kLen; ++i) out[i] = d.processMono(0.8f * static_cast<float>(std::sin(2 * kPi * hz * i / kRate)));
    return out;
}
}  // namespace

int main() {
    // Off: today's drive exactly.
    {
        DriveModule d;
        d.reset();
        d.setDrive(200);
        daisysp::Overdrive ref;
        ref.Init();
        ref.SetDrive(0.3f + (200 / 255.0f) * 0.3f);
        bool same = true;
        for (int i = 0; i < 4000; ++i) {
            const float x = 0.9f * static_cast<float>(std::sin(0.07 * i));
            same &= d.processMono(x) == ref.Process(x);
        }
        check(same, "without oversampling the drive is today's, bit for bit");
    }

    // A 15 kHz note through full drive: its 3rd harmonic (45 kHz) folds back to 3 kHz at 48 kHz.
    const auto plain = drive(false, 15000), os = drive(true, 15000);
    const double aliasPlain = level_at(plain, 3000), aliasOs = level_at(os, 3000);
    const double notePlain = level_at(plain, 15000), noteOs = level_at(os, 15000);
    std::printf("      15 kHz driven: alias at 3 kHz %.5f plain, %.5f at 2x; the note %.3f / %.3f\n",
                aliasPlain, aliasOs, notePlain, noteOs);
    check(aliasOs < aliasPlain / 5.6, "2x drive leaves at least 15 dB less of the folded-back tone");
    check(std::fabs(noteOs - notePlain) < 0.15 * notePlain, "and keeps the note itself at the same level");

    // A low note: the 2x drive sounds like today's (same level), only cleaner.
    const auto lowPlain = drive(false, 220), lowOs = drive(true, 220);
    check(std::fabs(level_at(lowOs, 220) - level_at(lowPlain, 220)) < 0.02, "a low note keeps its level at 2x");

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
