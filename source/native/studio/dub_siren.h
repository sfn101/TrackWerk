#pragma once
// DUB SIREN — Vintage family, model 123 (TrackWerk, Kid Bunka, GPL-3.0).
//
// A sound-system siren in the dub tradition: one oscillator (sine, square, falling saw or triangle)
// whose pitch is bent by an LFO of eight shapes — from a smooth wail to a stepped or random one —
// and by a slow sweep up or down; a resonant low-pass; and an optional sonar ping at the start of
// the note. Our own design from the well-known function of such sirens: no code, samples, names or
// artwork from any product, plugin or website.
//   macro A  Rate    LFO rate, 0.1 … 20 Hz (exponential)
//   macro B  Depth   LFO depth, 0 … 1.5 octaves of pitch
//   macro C  Sweep   0.5 is flat; below it the pitch falls, above it rises, by up to an octave
//   params   Wave, LFO shape, Cutoff, Resonance, Sweep time, Fine tune, Ping
// The oscillator's jumps (square edges, saw wrap) are smoothed by a two-sample PolyBLEP. The filter is
// a zero-delay-feedback state-variable low-pass with the cutoff glided between blocks, followed by a
// soft-knee limiter (transparent below half scale), so no setting can run away. Fixed size, no allocation, no locks.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace studio {

class DubSiren {
public:
    static constexpr float kRate = 48000.f;
    static constexpr int kWaveCount = 4;
    static constexpr int kLfoCount = 8;
    static constexpr const char* kWaveNames[kWaveCount] = {"Sine", "Square", "Saw down", "Triangle"};
    static constexpr const char* kLfoNames[kLfoCount] = {"Triangle", "Square",       "Saw up",     "Saw down",
                                                         "Dual warble", "Narrow pulse", "Staircase", "Random S&H"};
    static constexpr float kMaxDepthOctaves = 1.5f;

    struct Settings {
        int wave = 0, lfoShape = 0;
        float cutoff = 1.f, resonance = .2f, sweepSeconds = 1.5f, fineCents = 0.f, ping = 0.f;
    };

    // LFO rate in Hz for macro A (0..1).
    static float lfo_hz(float rate01) { return .1f * std::exp2(7.64f * std::clamp(rate01, 0.f, 1.f)); }
    // Filter cutoff in Hz for 0..1: 30 Hz … about 22 kHz.
    static float cutoff_hz(float cutoff01) { return 30.f * std::exp2(9.5f * std::clamp(cutoff01, 0.f, 1.f)); }
    // Sweep time in seconds for 0..1: 0.05 … 6.4 s.
    static float sweep_seconds(float v) { return .05f * std::exp2(7.f * std::clamp(v, 0.f, 1.f)); }
    // The LFO shape at phase 0..1 as -1..1 (Random S&H is the held value, not a function of phase).
    static float lfo_shape(int shape, float p, float held) {
        const float pi2 = 6.2831853f;
        switch (std::clamp(shape, 0, kLfoCount - 1)) {
            case 0: return p < .5f ? 4.f * p - 1.f : 3.f - 4.f * p;
            case 1: return p < .5f ? 1.f : -1.f;
            case 2: return 2.f * p - 1.f;
            case 3: return 1.f - 2.f * p;
            case 4: return .6f * std::sin(pi2 * p) + .4f * std::sin(2.f * pi2 * p);
            case 5: return p < .1f ? 1.f : -1.f;
            case 6: return float(int(p * 6.f)) * (2.f / 5.f) - 1.f;
            default: return held;
        }
    }

    DubSiren() { start(); }

    void start() {
        phase_ = 0.f;
        lfoPhase_ = 0.f;
        sweepTime_ = 0.f;
        ic1_ = ic2_ = 0.f;
        pingPhase_ = 0.f;
        pingEnv_ = 1.f;
        noise_ = 0x9E3779B9u;
        held_ = next_random();
        cutoffSmooth_ = -1.f;
    }

    void render(float* out, int frames, float hz, float rate01, float depth01, float sweep01, const Settings& s) {
        hz = std::clamp(hz, 8.f, 8000.f);
        const float lfoStep = lfo_hz(rate01) / kRate;
        const float depthOct = std::clamp(depth01, 0.f, 1.f) * kMaxDepthOctaves;
        // -1 … +1 octave; a small neutral band around the centre is exactly flat (macros are 7-bit).
        const float centred = std::clamp(sweep01, 0.f, 1.f) - .5f;
        const float sweepTarget = std::fabs(centred) < .012f ? 0.f : centred * 2.f;
        const float sweepSeconds = std::max(s.sweepSeconds, .02f);
        const float fine = s.fineCents / 1200.f;

        // The cutoff glides over a few blocks; the first block of a note starts on its value.
        const float cutoffTarget = std::clamp(s.cutoff, 0.f, 1.f);
        cutoffSmooth_ = cutoffSmooth_ < 0.f ? cutoffTarget : cutoffSmooth_ + (cutoffTarget - cutoffSmooth_) * .25f;
        const float fc = std::min(cutoff_hz(cutoffSmooth_), kRate * .45f);
        const float g = std::tan(3.14159265f * fc / kRate);
        const float k = 2.f - 1.96f * std::clamp(s.resonance, 0.f, 1.f);
        const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float pingGain = std::clamp(s.ping, 0.f, 1.f) * .6f;
        const float pingDecay = std::exp(-1.f / (.12f * kRate));
        const float pingHz = std::clamp(4.f * hz, 600.f, 3000.f);

        for (int i = 0; i < frames; ++i) {
            lfoPhase_ += lfoStep;
            if (lfoPhase_ >= 1.f) {
                lfoPhase_ -= std::floor(lfoPhase_);
                held_ = next_random();
            }
            const float lfo = lfo_shape(s.lfoShape, lfoPhase_, held_);
            const float ramp = std::min(sweepTime_ / sweepSeconds, 1.f);
            sweepTime_ += 1.f / kRate;
            const float octaves = depthOct * lfo + sweepTarget * ramp + fine;
            const float f = std::clamp(hz * std::exp2(octaves), 8.f, 12000.f);
            const float dt = std::min(f / kRate, .45f);
            phase_ += dt;
            phase_ -= std::floor(phase_);

            float v;
            switch (std::clamp(s.wave, 0, kWaveCount - 1)) {
                case 0: v = std::sin(6.2831853f * phase_); break;
                case 1: v = (phase_ < .5f ? 1.f : -1.f) + blep(phase_, dt) - blep(wrap(phase_ + .5f), dt); break;
                case 2: v = 1.f - 2.f * phase_ + blep(phase_, dt); break;
                default: v = phase_ < .5f ? 4.f * phase_ - 1.f : 3.f - 4.f * phase_; break;
            }
            float x = .5f * v;
            if (pingGain > 0.f && pingEnv_ > 1e-4f) {
                pingPhase_ += pingHz / kRate;
                pingPhase_ -= std::floor(pingPhase_);
                x += pingGain * pingEnv_ * std::sin(6.2831853f * pingPhase_);
                pingEnv_ *= pingDecay;
            }

            // Zero-delay-feedback state-variable low-pass (Zavalishin).
            const float v3 = x - ic2_;
            const float v1 = a1 * ic1_ + a2 * v3;
            const float v2 = ic2_ + a2 * ic1_ + a3 * v3;
            ic1_ = 2.f * v1 - ic1_;
            ic2_ = 2.f * v2 - ic2_;
            out[i] = limit(v2);
        }
        if (!std::isfinite(ic1_) || !std::isfinite(ic2_)) ic1_ = ic2_ = 0.f;
    }

private:
    static float wrap(float p) { return p - std::floor(p); }
    // Transparent up to 0.5, then a smooth knee towards 0.9: a plain tone is not distorted at all, and a
    // resonance peak or the ping on top of it cannot run away.
    static float limit(float v) {
        const float m = std::fabs(v);
        if (m <= .5f) return v;
        const float y = .5f + .4f * std::tanh((m - .5f) / .4f);
        return v < 0.f ? -y : y;
    }
    static float blep(float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }
    float next_random() {
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        return float(int32_t(noise_)) * (1.f / 2147483648.f);
    }

    float phase_ = 0.f, lfoPhase_ = 0.f, sweepTime_ = 0.f, ic1_ = 0.f, ic2_ = 0.f;
    float pingPhase_ = 0.f, pingEnv_ = 1.f, held_ = 0.f, cutoffSmooth_ = -1.f;
    uint32_t noise_ = 0x9E3779B9u;
};

}  // namespace studio
