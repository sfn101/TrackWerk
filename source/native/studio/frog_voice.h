#pragma once
// CROAKER — Vintage family (TrackWerk, Kid Bunka, GPL-3.0).
//
// A funny vocal synth: a frog croaking through a throat. A buzzing glottal source (band-limited saw)
// feeds three resonant band-pass formants — the throat and mouth of the animal — while a croak
// envelope chops the sound into one to four pulses per croak ("ri-bbit"), and each pulse dips the
// pitch a little. A noise "bubble" blows through the first formant. Four species set the formants:
// tree frog, bullfrog, toad and peeper. Our own design from the general idea of formant synthesis:
// no code, samples, names or artwork from any product, plugin, app or website.
//   macro A  Croak     croak rate, 0.5 … 16 per second (exponential)
//   macro B  Throat    scales all formants, 0.6 … 1.6 (small throat to big throat)
//   macro C  Dip       how far each pulse bends the pitch down, 0 … 7 semitones
//   params   Species, Pulses, Croak depth, Throat, Chirp, Bubble
// Fixed size, no allocation, no locks. A soft-knee limiter keeps every setting finite and bounded.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace studio {

class FrogVoice {
public:
    static constexpr float kRate = 48000.f;
    static constexpr int kSpeciesCount = 4;
    static constexpr const char* kSpeciesNames[kSpeciesCount] = {"Tree frog", "Bullfrog", "Toad", "Peeper"};

    struct Settings {
        int species = 0, pulses = 2;
        float depth = .8f, throat = .5f, chirp = .3f, bubble = .2f;
    };

    static float croak_hz(float rate01) { return .5f * std::exp2(5.f * std::clamp(rate01, 0.f, 1.f)); }
    // Formant scale for macro B / parameter Throat (0..1): 0.6 … 1.6.
    static float throat_scale(float v) { return .6f * std::exp2(1.415f * std::clamp(v, 0.f, 1.f)); }

    FrogVoice() { start(); }

    void start() {
        phase_ = 0.f;
        croakPhase_ = 0.f;
        for (int f = 0; f < 3; ++f) ic1_[f] = ic2_[f] = 0.f;
        noise_ = 0x2545F491u;
        env_ = 0.f;
    }

    void render(float* out, int frames, float hz, float rate01, float throatMacro, float dip01, const Settings& s) {
        hz = std::clamp(hz, 20.f, 2000.f);
        static constexpr float formants[kSpeciesCount][3] = {
            {700.f, 1800.f, 3200.f}, {220.f, 520.f, 1400.f}, {350.f, 900.f, 2200.f}, {2800.f, 3800.f, 5200.f}};
        static constexpr float qs[3] = {7.f, 9.f, 11.f};
        const int sp = std::clamp(s.species, 0, kSpeciesCount - 1);
        const float scale = throat_scale(throatMacro) * throat_scale(s.throat) * (1.f / .96f);
        float a1[3], a2[3], a3[3], k[3];
        for (int f = 0; f < 3; ++f) {
            const float fc = std::clamp(formants[sp][f] * scale, 60.f, kRate * .42f);
            const float g = std::tan(3.14159265f * fc / kRate);
            k[f] = 1.f / qs[f];
            a1[f] = 1.f / (1.f + g * (g + k[f]));
            a2[f] = g * a1[f];
            a3[f] = g * a2[f];
        }
        const int pulses = std::clamp(s.pulses, 1, 4);
        const float croakStep = croak_hz(rate01) / kRate;
        const float depth = std::clamp(s.depth, 0.f, 1.f);
        const float dipOct = std::clamp(dip01, 0.f, 1.f) * 7.f / 12.f;
        const float chirpOct = std::clamp(s.chirp, 0.f, 1.f) * .5f;
        const float bubble = std::clamp(s.bubble, 0.f, 1.f);
        const float weights[3] = {1.f, .8f, .6f};

        // The hump (a sine) and the pitch dip (an exp2) are slow: evaluated every 8 samples and interpolated between,
        // which spares the small ARM cores about a third of the work and cannot be heard.
        float tA = 0.f, tB = 0.f, hA = 0.f, hB = 0.f, mA = 1.f, mB = 1.f;
        const auto control = [&](float cph, float& target, float& h, float& m) {
            const float p = cph * float(pulses);
            const float frac = p - std::floor(p);
            h = std::sin(3.14159265f * frac);
            h *= h;
            const int index = std::min(int(p), pulses - 1);
            const float rise = pulses == 1 ? 1.f : .72f + .28f * float(index) / float(pulses - 1);
            target = h * rise;
            m = std::exp2(-(dipOct + chirpOct) * (1.f - h));
        };
        for (int i = 0; i < frames; ++i) {
            croakPhase_ += croakStep;
            croakPhase_ -= std::floor(croakPhase_);
            if (i == 0) control(croakPhase_, tB, hB, mB);
            if ((i & 7) == 0) {
                tA = tB; hA = hB; mA = mB;
                float cp = croakPhase_ + 8.f * croakStep;
                cp -= std::floor(cp);
                control(cp, tB, hB, mB);
            }
            const float tFrac = float(i & 7) * .125f;
            const float target = tA + (tB - tA) * tFrac;
            env_ += (target - env_) * .02f;
            const float amp = 1.f - depth + depth * env_;

            const float f = std::clamp(hz * (mA + (mB - mA) * tFrac), 20.f, 4000.f);
            const float dt = std::min(f / kRate, .45f);
            phase_ += dt;
            phase_ -= std::floor(phase_);
            const float saw = 1.f - 2.f * phase_ + blep(phase_, dt);
            const float n = next_random();

            float sum = 0.f;
            for (int q = 0; q < 3; ++q) {
                const float x = q == 0 ? saw + bubble * 1.5f * n * env_ : saw;
                const float v3 = x - ic2_[q];
                const float v1 = a1[q] * ic1_[q] + a2[q] * v3;
                const float v2 = ic2_[q] + a2[q] * ic1_[q] + a3[q] * v3;
                ic1_[q] = 2.f * v1 - ic1_[q];
                ic2_[q] = 2.f * v2 - ic2_[q];
                sum += weights[q] * k[q] * v1;
            }
            out[i] = limit(.9f * amp * sum);
        }
        for (int q = 0; q < 3; ++q)
            if (!std::isfinite(ic1_[q]) || !std::isfinite(ic2_[q]) || (std::fabs(ic1_[q]) < 1e-15f && std::fabs(ic2_[q]) < 1e-15f)) ic1_[q] = ic2_[q] = 0.f;
    }

private:
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

    float phase_ = 0.f, croakPhase_ = 0.f, env_ = 0.f;
    float ic1_[3] = {0, 0, 0}, ic2_[3] = {0, 0, 0};
    uint32_t noise_ = 0x2545F491u;
};

}  // namespace studio
