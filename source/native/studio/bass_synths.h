#pragma once
// ACID BOX and REESE BASS — Vintage family (TrackWerk, Kid Bunka, GPL-3.0). Two bass engines of our own design,
// from the general ideas of the sounds they are named after: no code, samples, names or artwork from any product.
//
// ACID BOX: one band-limited oscillator (saw or square) through a resonant four-pole low-pass whose cutoff is
//   swept by a decay envelope on every note; Accent makes the envelope sweep wider and the note louder.
//   macro A Cutoff · macro B Resonance · macro C Env mod
//   params Wave, Decay, Accent, Drive, Sub
// REESE BASS: three detuned saws that beat against each other, a sine sub, a low-pass whose cutoff drifts slowly,
//   and a soft saturation.
//   macro A Detune · macro B Cutoff · macro C Movement
//   params Voices (2 or 3), Sub, Resonance, Drive, Drift speed
// Both: fixed size, no allocation, no locks; a soft-knee limiter keeps every setting bounded.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace studio {

namespace bass_detail {
// A rational tanh, good to about 2 % up to |x| = 3 and cheap on the handhelds' small cores.
inline float fast_tanh(float x) {
    x = std::clamp(x, -3.f, 3.f);
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}
// Soft clipper with first-order antiderivative anti-aliasing (ADAA): the saturation of a saw would otherwise fold its
// own harmonics back into the audible range. f(x) = 1.5x - 0.5x^3 inside +-1, then flat; F is its integral, so the
// output is the average of f over the step between two samples: no oversampling, one division per sample.
struct SoftClipAdaa {
    float xp = 0.f;
    void reset() { xp = 0.f; }
    static float f(float x) { x = std::clamp(x, -1.f, 1.f); return x * (1.5f - .5f * x * x) * (2.f / 3.f) * 1.f; }
    static float integral(float x) {
        const float a = std::fabs(x);
        const float v = a <= 1.f ? (.75f * a * a - .125f * a * a * a * a) * (2.f / 3.f) : (a - .375f) * (2.f / 3.f);
        return v;
    }
    float process(float x) {
        const float d = x - xp;
        const float y = std::fabs(d) < 1e-5f ? f(.5f * (x + xp)) : (integral(x) - integral(xp)) / d;
        xp = x;
        return y * 1.5f;   // unity gain for small signals, a ceiling of 1
    }
};
// DC blocker (about 4 Hz): a bass note must not carry an offset into the limiter and the mix.
struct DcBlock {
    float x1 = 0.f, y1 = 0.f;
    void reset() { x1 = y1 = 0.f; }
    float process(float x) {
        const float y = x - x1 + .9995f * y1;
        x1 = x;
        y1 = std::fabs(y) < 1e-15f ? 0.f : y;   // no denormal tail after a note
        return y;
    }
};
inline float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}
inline float limit(float v) {
    const float m = std::fabs(v);
    if (m <= .5f) return v;
    const float y = .5f + .4f * std::tanh((m - .5f) / .4f);
    return v < 0.f ? -y : y;
}
// Zero-delay-feedback state-variable low-pass (Zavalishin), one pole pair.
struct Svf {
    float ic1 = 0.f, ic2 = 0.f;
    void reset() { ic1 = ic2 = 0.f; }
    float low(float x, float a1, float a2, float a3) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return v2;
    }
    // NaN guard and denormal flush: a decaying state that falls into the denormal range costs an ARM core
    // many times the normal price per operation.
    void scrub() {
        if (!std::isfinite(ic1) || !std::isfinite(ic2)) ic1 = ic2 = 0.f;
        if (std::fabs(ic1) < 1e-15f) ic1 = 0.f;
        if (std::fabs(ic2) < 1e-15f) ic2 = 0.f;
    }
};
}  // namespace bass_detail

class AcidBox {
public:
    static constexpr float kRate = 48000.f;
    static constexpr int kWaveCount = 2;
    static constexpr const char* kWaveNames[kWaveCount] = {"Saw", "Square"};
    struct Settings { int wave = 0; float decay = .4f, accent = 0.f, drive = .3f, sub = 0.f; };

    static float cutoff_hz(float v01) { return 60.f * std::exp2(6.f * std::clamp(v01, 0.f, 1.f)); }   // 60 Hz … 3.8 kHz
    static float decay_seconds(float v01) { return .06f * std::exp2(5.f * std::clamp(v01, 0.f, 1.f)); }   // 0.06 … 1.9 s

    AcidBox() { start(); }
    void start() { phase_ = 0.f; subPhase_ = 0.f; env_ = 1.f; f1_.reset(); f2_.reset(); dc_.reset(); clip_.reset(); }

    void render(float* out, int frames, float hz, float cutoff01, float res01, float mod01, const Settings& s) {
        using namespace bass_detail;
        hz = std::clamp(hz, 20.f, 4000.f);
        const float dt = std::min(hz / kRate, .45f);
        const float k = 2.f - 1.9f * std::clamp(res01, 0.f, 1.f);
        const float k1 = 1.414f;   // the first pole pair is plain; only the second one resonates
        const float accent = std::clamp(s.accent, 0.f, 1.f);
        const float envDecay = std::exp(-float(frames) / (decay_seconds(s.decay) * kRate));
        const float base = std::log2(cutoff_hz(cutoff01));
        const float sweep = (1.f + accent) * std::clamp(mod01, 0.f, 1.f) * 4.f;   // octaves
        const float driveGain = 1.f + 4.f * std::clamp(s.drive, 0.f, 1.f);
        const float level = (.8f + .4f * accent) / (1.f + .6f * std::clamp(s.drive, 0.f, 1.f));
        // one cutoff per call (24 samples): the envelope moves slowly against that
        const float fc = std::min(std::exp2(base + sweep * env_), kRate * .42f);
        env_ *= envDecay;
        const float g = std::tan(3.14159265f * fc / kRate);
        const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float b1 = 1.f / (1.f + g * (g + k1)), b2 = g * b1, b3 = g * b2;
        for (int i = 0; i < frames; ++i) {
            phase_ += dt;
            phase_ -= std::floor(phase_);
            float v;
            if (s.wave == 0) v = 1.f - 2.f * phase_ + blep(phase_, dt);
            else {
                float p2 = phase_ + .5f;
                p2 -= std::floor(p2);
                v = (phase_ < .5f ? 1.f : -1.f) + blep(phase_, dt) - blep(p2, dt);
            }
            subPhase_ += dt * .5f;   // a sine an octave below
            subPhase_ -= std::floor(subPhase_);
            if (s.sub > 0.f) v = v * (1.f - .4f * s.sub) + s.sub * .7f * std::sin(6.2831853f * subPhase_);
            const float x = clip_.process(driveGain * .6f * v);
            // four poles: a plain stage, a soft clip (the squelch), then the resonant stage
            const float y1 = fast_tanh(1.2f * f1_.low(x, b1, b2, b3));
            const float y2 = f2_.low(y1, a1, a2, a3);
            out[i] = limit(.27f * level * dc_.process(y2 * (1.f + .5f * (2.f - k) / 2.f)));
        }
        f1_.scrub();
        f2_.scrub();
    }

private:
    float phase_ = 0.f, subPhase_ = 0.f, env_ = 1.f;
    bass_detail::Svf f1_, f2_;
    bass_detail::DcBlock dc_;
    bass_detail::SoftClipAdaa clip_;
};

class ReeseBass {
public:
    static constexpr float kRate = 48000.f;
    struct Settings { int voices = 3; float sub = .5f, resonance = .15f, drive = .3f, drift = .3f; };

    static float cutoff_hz(float v01) { return 80.f * std::exp2(5.5f * std::clamp(v01, 0.f, 1.f)); }   // 80 Hz … 3.6 kHz
    static float drift_hz(float v01) { return .05f * std::exp2(6.f * std::clamp(v01, 0.f, 1.f)); }   // 0.05 … 3.2 Hz

    ReeseBass() { start(); }
    void start() {
        for (int i = 0; i < 3; ++i) phase_[i] = float(i) * .31f;
        subPhase_ = 0.f;
        lfo_ = 0.f;
        f1_.reset();
        f2_.reset();
        dc_.reset();
        clip_.reset();
    }

    void render(float* out, int frames, float hz, float detune01, float cutoff01, float move01, const Settings& s) {
        using namespace bass_detail;
        hz = std::clamp(hz, 20.f, 2000.f);
        const float lfoStep = drift_hz(s.drift) / kRate;
        const float spread = std::clamp(detune01, 0.f, 1.f) * 40.f / 1200.f;   // up to 40 cents either side
        const float move = std::clamp(move01, 0.f, 1.f);
        const int voices = std::clamp(s.voices, 2, 3);
        const float k = 2.f - 1.85f * std::clamp(s.resonance, 0.f, 1.f);
        const float driveGain = 1.f + 3.f * std::clamp(s.drive, 0.f, 1.f);
        const float sub = std::clamp(s.sub, 0.f, 1.f);
        for (int i = 0; i < frames; ++i) {
            lfo_ += lfoStep;
            lfo_ -= std::floor(lfo_);
            // the detune breathes with the drift (the beating slows and quickens): a slow movement, evaluated every 8 samples
            if ((i & 7) == 0) {
                l_ = std::sin(6.2831853f * lfo_);
                const float wide = spread * (1.f + .6f * move * l_);
                ratio_[0] = std::exp2(-wide);
                ratio_[1] = std::exp2(wide);
            }
            const float l = l_;
            const float ratios[3] = {ratio_[0], ratio_[1], 1.f};
            float v = 0.f;
            for (int o = 0; o < voices; ++o) {
                const float f = hz * (o == 2 ? 1.f : ratios[o]);
                const float dt = std::min(f / kRate, .45f);
                phase_[o] += dt;
                phase_[o] -= std::floor(phase_[o]);
                v += 1.f - 2.f * phase_[o] + blep(phase_[o], dt);
            }
            v *= voices == 3 ? .34f : .5f;
            subPhase_ += hz * .5f / kRate;
            subPhase_ -= std::floor(subPhase_);
            v += sub * .5f * std::sin(6.2831853f * subPhase_);
            // cutoff: moved by the drift, one value per sample is affordable only at control rate; use a
            // coarse update every 8 samples
            if ((i & 7) == 0) {
                const float fc = std::min(cutoff_hz(cutoff01) * std::exp2(1.4f * move * l), kRate * .42f);
                const float g = std::tan(3.14159265f * fc / kRate);
                a1_ = 1.f / (1.f + g * (g + k));
                a2_ = g * a1_;
                a3_ = g * a2_;
            }
            const float x = clip_.process(driveGain * v * .8f);
            const float y = f2_.low(f1_.low(x, a1_, a2_, a3_), a1_, a2_, a3_);
            out[i] = limit(.3f * dc_.process(y));
        }
        f1_.scrub();
        f2_.scrub();
    }

private:
    float phase_[3] = {0, .31f, .62f}, subPhase_ = 0.f, lfo_ = 0.f, l_ = 0.f, ratio_[2] = {1.f, 1.f};
    float a1_ = .5f, a2_ = .2f, a3_ = .05f;
    bass_detail::Svf f1_, f2_;
    bass_detail::DcBlock dc_;
    bass_detail::SoftClipAdaa clip_;
};

}  // namespace studio
