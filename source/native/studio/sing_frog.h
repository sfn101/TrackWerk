#pragma once
// FROGGY — Vintage family (TrackWerk, Kid Bunka, GPL-3.0): a small singing voice that sings the words
// you type. Each note sings the next syllable of the instrument's lyric (sing_text.h).
//
// Light formant voice, sized for handhelds (about 60 flops a sample, one voice): a glottal flow pulse
// (raised-cosine opening and closing, with jitter and shimmer) drives four parallel resonators set from
// the vowel's formants; consonants are noise bands (fricatives), bursts (stops), a low voice bar and a
// nasal murmur, with the formants gliding between targets. The frog is a "Voice" choice that lowers the
// throat and adds the croak (amplitude chop and a pitch dip). Design idea and vowel data from Chimera Vocal
// Synth by Jameson Hodge (MIT, see docs/THIRD-PARTY-CHIMERA.md); the engine itself is our own light version
// (Chimera's airway network runs at 96 kHz and is too heavy for the handhelds).
//   macro A  Mouth    jaw: scales the first formant 0.8 … 1.3
//   macro B  Breath   a whisper of aspiration noise: 0 … 100 %
//   macro C  Throat   scales every formant 0.7 … 1.4
//   params   Voice, Croak, Vibrato, Glide, Coda, Syllable (and the Lyrics row)
#include "sing_text.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace studio {

class SingFrog {
public:
    static constexpr float kRate = 48000.f;
    static constexpr int kVoiceCount = 4;
    static constexpr const char* kVoiceNames[kVoiceCount] = {"Frog", "Man", "Woman", "Child"};

    struct Settings {
        int voice = 0;
        float croak = .5f, vibrato = .3f, glideMs = 30.f, codaMs = 500.f;   // codaMs 0 = no final consonants
    };

    static float glide_ms(float v01) { return 8.f * std::exp2(4.f * std::clamp(v01, 0.f, 1.f)); }   // 8 … 128 ms
    // Coda delay for 0..1: 0 is off, then 0.15 … 2.4 s.
    static float coda_ms(float v01) { return v01 <= .02f ? 0.f : 150.f * std::exp2(4.f * std::clamp(v01, 0.f, 1.f)); }

    SingFrog() { start(sing::Syllable{}, Settings{}); }

    void start(const sing::Syllable& syl, const Settings& s) {
        syl_ = syl;
        phase_ = 0.f;
        gPrev_ = 0.f;
        t_ = 0;
        vib_ = 0.f;
        croakPhase_ = 0.f;
        tilt_ = 0.f;
        for (int i = 0; i < 4; ++i) ic1_[i] = ic2_[i] = 0.f;
        nic1_ = nic2_ = 0.f;
        noise_ = 0x1234ABCDu;
        shimmer_ = 1.f;
        for (int i = 0; i < 3; ++i) f_[i] = 0.f;
        build_plan(s);
        first_ = true;
    }

    void render(float* out, int frames, float hz, float mouth01, float breath01, float throat01, const Settings& s) {
        hz = std::clamp(hz, 30.f, 1500.f);
        // ── control rate: where in the plan are we, and what do the segments ask for ────────────────
        Target tg = target_at(t_);
        const float voiceScale = voice_scale(s.voice);
        const float throat = voiceScale * (.7f * std::exp2(std::clamp(throat01, 0.f, 1.f))) * (1.f / .95f);
        const float jaw = .8f + .5f * std::clamp(mouth01, 0.f, 1.f);
        const float want[3] = {tg.f1 * jaw * throat, tg.f2 * throat, tg.f3 * throat};
        const float step = 1.f - std::exp(-float(frames) * 1000.f / (kRate * std::max(s.glideMs, 4.f)));
        if (first_) {
            for (int i = 0; i < 3; ++i) f_[i] = want[i];
            amp_ = 0.f;
            voiced_ = tg.voiced;
            first_ = false;
        }
        for (int i = 0; i < 3; ++i) f_[i] += (want[i] - f_[i]) * step;
        const float fast = 1.f - std::exp(-float(frames) * 1000.f / (kRate * 6.f));
        amp_ += (tg.amp - amp_) * fast;
        voiced_ += (tg.voiced - voiced_) * fast;
        asp_ += (tg.asp + std::clamp(breath01, 0.f, 1.f) * .5f * tg.amp - asp_) * fast;
        nz_ += (tg.noise - nz_) * fast;
        nzHz_ += (tg.noiseHz - nzHz_) * fast;

        const float f4 = 3400.f * throat, f5 = 4400.f * throat;
        const float fc[4] = {f_[0], f_[1], f_[2], f4};
        const float bw[4] = {70.f + .06f * f_[0], 90.f + .05f * f_[1], 130.f + .04f * f_[2], 220.f};
        float a1[4], a2[4], a3[4], k[4];
        static constexpr float gains[4] = {1.f, .75f, .5f, .3f};
        for (int i = 0; i < 4; ++i) {
            const float c = std::clamp(fc[i], 90.f, kRate * .42f);
            const float g = std::tan(3.14159265f * c / kRate);
            k[i] = bw[i] / c;
            a1[i] = 1.f / (1.f + g * (g + k[i]));
            a2[i] = g * a1[i];
            a3[i] = g * a2[i];
        }
        (void)f5;
        // noise band for fricatives and bursts
        const float ng = std::tan(3.14159265f * std::clamp(nzHz_, 400.f, 18000.f) / kRate);
        const float nk = 1.f / 2.6f;
        const float na1 = 1.f / (1.f + ng * (ng + nk)), na2 = ng * na1, na3 = ng * na2;

        // vibrato grows in over the first 300 ms; the croak chops the level and bends the pitch
        const float vibDepth = std::clamp(s.vibrato, 0.f, 1.f) * 45.f / 1200.f;   // up to 45 cents
        const float tSec = float(t_) / kRate;
        const float vibIn = std::min(tSec / .3f, 1.f);
        const bool frog = s.voice == 0;
        const float croakAmt = std::clamp(s.croak, 0.f, 1.f) * (frog ? 1.f : .5f);
        const float oq = frog ? .72f : s.voice == 1 ? .6f : .64f;
        const float tiltCoef = frog ? .42f : s.voice == 1 ? .5f : s.voice == 2 ? .6f : .7f;

        // The croak hump and the pitch bend (a cosine, a sine and an exp2) are slow: they are evaluated every 8 samples
        // and interpolated between, which is inaudible and spares the small ARM cores about a third of the work.
        float hA = 0.f, hB = 0.f, bA = 1.f, bB = 1.f;
        const auto control = [&](float cph, float vph, float& h, float& b) {
            h = .5f - .5f * std::cos(6.2831853f * cph);   // 0..1
            b = std::exp2(-croakAmt * .35f * (1.f - h) + vibDepth * vibIn * std::sin(6.2831853f * vph));
        };
        for (int i = 0; i < frames; ++i) {
            vib_ += 5.4f / kRate;
            vib_ -= std::floor(vib_);
            croakPhase_ += 6.f / kRate;
            croakPhase_ -= std::floor(croakPhase_);
            if (i == 0) control(croakPhase_, vib_, hB, bB);
            if ((i & 7) == 0) {
                hA = hB; bA = bB;
                float cp = croakPhase_ + 8.f * 6.f / kRate, vp = vib_ + 8.f * 5.4f / kRate;
                cp -= std::floor(cp); vp -= std::floor(vp);
                control(cp, vp, hB, bB);
            }
            const float tFrac = float(i & 7) * .125f;
            const float hump = hA + (hB - hA) * tFrac;
            const float bend = bA + (bB - bA) * tFrac;
            const float f = std::clamp(hz * bend, 20.f, 3000.f);
            float dt = std::min(f / kRate, .4f);
            // jitter (small random walk of the period) and shimmer (level of each period)
            phase_ += dt * (1.f + .003f * next_random());
            if (phase_ >= 1.f) {
                phase_ -= std::floor(phase_);
                shimmer_ = 1.f + .03f * next_random();
            }
            const float g = flow(phase_, oq);
            const float rawSrc = (g - gPrev_) / std::max(dt, 1e-4f) * .22f * shimmer_;
            gPrev_ = g;
            // spectral tilt: the sharp closing edge is softened, more for the frog and the man, less for the child
            tilt_ += (rawSrc - tilt_) * tiltCoef;
            const float src = tilt_;
            const float chop = 1.f - croakAmt * .75f * (1.f - hump) * (1.f - hump);
            const float n = next_random();
            const float x = voiced_ * src + asp_ * n * .6f;
            float sum = 0.f;
            for (int q = 0; q < 4; ++q) {
                const float v3 = x - ic2_[q];
                const float v1 = a1[q] * ic1_[q] + a2[q] * v3;
                const float v2 = ic2_[q] + a2[q] * ic1_[q] + a3[q] * v3;
                ic1_[q] = 2.f * v1 - ic1_[q];
                ic2_[q] = 2.f * v2 - ic2_[q];
                sum += gains[q] * k[q] * v1 * (q == 0 ? nasalGain_ : 1.f);
            }
            // the noise band (fricatives, bursts)
            const float nv3 = n * nz_ - nic2_;
            const float nv1 = na1 * nic1_ + na2 * nv3;
            const float nv2 = nic2_ + na2 * nic1_ + na3 * nv3;
            nic1_ = 2.f * nv1 - nic1_;
            nic2_ = 2.f * nv2 - nic2_;
            const float hiss = nk * nv1 * 2.5f;
            out[i] = limit(1.6f * amp_ * chop * (sum + hiss));
        }
        t_ += uint32_t(frames);
        for (int q = 0; q < 4; ++q)
            if (!std::isfinite(ic1_[q]) || !std::isfinite(ic2_[q])) ic1_[q] = ic2_[q] = 0.f;
        if (!std::isfinite(nic1_) || !std::isfinite(nic2_)) nic1_ = nic2_ = 0.f;
        for (int q = 0; q < 4; ++q) {   // flush denormals: they cost an ARM core many times the normal price
            if (std::fabs(ic1_[q]) < 1e-15f) ic1_[q] = 0.f;
            if (std::fabs(ic2_[q]) < 1e-15f) ic2_[q] = 0.f;
        }
        if (std::fabs(nic1_) < 1e-15f) nic1_ = 0.f;
        if (std::fabs(nic2_) < 1e-15f) nic2_ = 0.f;
    }

    // Plan length in samples until the last sound ends (0 if the vowel is held for ever).
    uint32_t plan_end() const { return planEnd_; }
    // For tests: where the plan puts the vowel.
    uint32_t vowel_start() const { return vowelStart_; }
    uint32_t coda_start() const { return codaStart_; }

private:
    struct Target {
        float f1 = 500, f2 = 1500, f3 = 2500, amp = 0, voiced = 0, asp = 0, noise = 0, noiseHz = 5000;
    };
    struct Seg {
        uint8_t phone = 0;
        uint32_t start = 0, len = 0;   // samples; len 0 for the held vowel
        bool vowel = false, coda = false;
    };

    static float voice_scale(int voice) {
        switch (std::clamp(voice, 0, kVoiceCount - 1)) {
            case 0: return .86f;
            case 1: return 1.f;
            case 2: return 1.17f;
            default: return 1.3f;
        }
    }
    static float flow(float p, float oq) {
        const float tp = .62f * oq;
        if (p >= oq) return 0.f;
        if (p < tp) return .5f - .5f * std::cos(3.14159265f * p / tp);
        return std::cos(1.5707963f * (p - tp) / (oq - tp));
    }
    static float limit(float v) {
        const float m = std::fabs(v);
        if (m <= .5f) return v;
        const float y = .5f + .4f * std::tanh((m - .5f) / .4f);
        return v < 0.f ? -y : y;
    }
    float next_random() {
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        return float(int32_t(noise_)) * (1.f / 2147483648.f);
    }

    // Vowel formants (Hz): male means of Hillenbrand et al. (1995).
    struct Fmt { float f1, f2, f3; };
    static Fmt vowel_formants(uint8_t v) {
        switch (v) {
            case sing::V_I: return {342, 2322, 3000};
            case sing::V_IH: return {427, 2034, 2684};
            case sing::V_E: return {476, 2089, 2691};
            case sing::V_EH: return {580, 1799, 2605};
            case sing::V_AE: return {640, 1760, 2500};
            case sing::V_AA: return {768, 1333, 2522};
            case sing::V_AO: return {652, 997, 2538};
            case sing::V_O: return {497, 910, 2459};
            case sing::V_UH: return {469, 1122, 2434};
            case sing::V_U: return {378, 997, 2343};
            case sing::V_AH: return {623, 1200, 2550};
            case sing::V_ER: return {474, 1379, 1710};
            default: return {500, 1450, 2450};
        }
    }

    // What a consonant looks like: type and numbers.
    enum Kind { STOP, FRIC, NASAL, LIQUID, AFFR, ASPIR };
    struct Cons {
        Kind kind; bool voiced; float f1, f2, f3, noiseHz, noiseGain; float ms;
    };
    static Cons cons_spec(uint8_t p) {
        switch (p) {
            case sing::C_P: return {STOP, false, 400, 1000, 2200, 1200, .5f, 70};
            case sing::C_B: return {STOP, true, 300, 900, 2200, 1200, .35f, 55};
            case sing::C_T: return {STOP, false, 400, 1800, 2600, 4800, .6f, 70};
            case sing::C_D: return {STOP, true, 300, 1700, 2600, 4200, .4f, 55};
            case sing::C_K: return {STOP, false, 400, 1500, 2400, 2200, .55f, 75};
            case sing::C_G: return {STOP, true, 300, 1500, 2300, 2000, .4f, 55};
            case sing::C_F: return {FRIC, false, 400, 1200, 2500, 6500, .22f, 100};
            case sing::C_V: return {FRIC, true, 300, 1200, 2500, 6000, .16f, 80};
            case sing::C_TH: return {FRIC, false, 400, 1400, 2500, 7000, .16f, 100};
            case sing::C_DH: return {FRIC, true, 300, 1400, 2500, 6500, .12f, 70};
            case sing::C_S: return {FRIC, false, 400, 1500, 2600, 7200, .7f, 110};
            case sing::C_Z: return {FRIC, true, 300, 1500, 2600, 6800, .5f, 90};
            case sing::C_SH: return {FRIC, false, 400, 1500, 2300, 3400, .75f, 120};
            case sing::C_ZH: return {FRIC, true, 300, 1500, 2300, 3200, .55f, 100};
            case sing::C_H: return {ASPIR, false, 500, 1500, 2500, 3000, 0.f, 70};
            case sing::C_CH: return {AFFR, false, 400, 1500, 2300, 3400, .75f, 120};
            case sing::C_JH: return {AFFR, true, 300, 1500, 2300, 3200, .55f, 100};
            case sing::C_M: return {NASAL, true, 250, 1100, 2200, 0, 0.f, 80};
            case sing::C_N: return {NASAL, true, 250, 1700, 2600, 0, 0.f, 70};
            case sing::C_NG: return {NASAL, true, 250, 1500, 2400, 0, 0.f, 90};
            case sing::C_L: return {LIQUID, true, 360, 1300, 2800, 0, 0.f, 70};
            case sing::C_R: return {LIQUID, true, 310, 1060, 1380, 0, 0.f, 70};
            case sing::C_W: return {LIQUID, true, 300, 610, 2200, 0, 0.f, 70};
            default: return {LIQUID, true, 260, 2070, 3020, 0, 0.f, 70};   // j
        }
    }

    void build_plan(const Settings& s) {
        nseg_ = 0;
        uint32_t t = 0;
        auto ms = [&](float v) { return uint32_t(v * kRate / 1000.f); };
        for (int i = 0; i < 3 && syl_.onset[i]; ++i) {
            Seg g; g.phone = syl_.onset[i]; g.start = t; g.len = ms(cons_spec(g.phone).ms);
            t += g.len; seg_[nseg_++] = g;
        }
        vowelStart_ = t;
        Seg v; v.phone = syl_.vowel ? syl_.vowel : uint8_t(sing::V_SCHWA); v.start = t; v.vowel = true; seg_[nseg_++] = v;
        int codaCount = 0;
        for (int i = 0; i < 3 && syl_.coda[i]; ++i) ++codaCount;
        codaStart_ = 0;
        planEnd_ = 0;
        if (codaCount > 0 && s.codaMs > 0.f) {
            uint32_t c = std::max(ms(s.codaMs), vowelStart_ + ms(150.f));
            codaStart_ = c;
            for (int i = 0; i < codaCount; ++i) {
                Seg g; g.phone = syl_.coda[i]; g.start = c; g.len = ms(cons_spec(g.phone).ms); g.coda = true;
                c += g.len; seg_[nseg_++] = g;
            }
            planEnd_ = c;
        }
    }

    Target target_at(uint32_t t) {
        Target r;
        nasalGain_ = 1.f;
        const uint8_t vp = syl_.vowel ? syl_.vowel : uint8_t(sing::V_SCHWA);
        Fmt v1 = vowel_formants(vp), v2 = syl_.vowel2 ? vowel_formants(syl_.vowel2) : v1;
        // diphthong: the second vowel is approached from 200 ms after the vowel starts, over 250 ms
        const float into = syl_.vowel2 && t > vowelStart_ + 9600u ? std::min(float(t - vowelStart_ - 9600u) / 12000.f, 1.f) : 0.f;
        const Fmt vow = {v1.f1 + (v2.f1 - v1.f1) * into, v1.f2 + (v2.f2 - v1.f2) * into, v1.f3 + (v2.f3 - v1.f3) * into};
        r.f1 = vow.f1; r.f2 = vow.f2; r.f3 = vow.f3; r.amp = 1.f; r.voiced = 1.f;
        // before the first segment the voice is closed
        const Seg* active = nullptr;
        for (int i = 0; i < nseg_; ++i) {
            const Seg& s = seg_[i];
            if (s.vowel) { if (t >= s.start && (codaStart_ == 0 || t < codaStart_)) active = &s; }
            else if (t >= s.start && t < s.start + s.len) active = &s;
        }
        if (!active) {
            // after the last consonant of a word the sound is over; before the plan, silence
            if (planEnd_ && t >= planEnd_) r.amp = 0.f;
            return r;
        }
        if (active->vowel) return r;
        const Cons c = cons_spec(active->phone);
        const float local = float(t - active->start) / std::max(float(active->len), 1.f);   // 0..1 in the segment
        const bool onset = !active->coda;
        // a consonant leans towards the vowel's formants for stops and fricatives
        const Fmt neighbour = vow;
        switch (c.kind) {
            case STOP: {
                r.f1 = neighbour.f1; r.f2 = neighbour.f2; r.f3 = neighbour.f3;
                if (local < .5f) { r.amp = c.voiced ? .18f : 0.f; r.voiced = c.voiced ? 1.f : 0.f; r.f1 = 220; }
                else if (local < .65f) { r.amp = 1.f; r.voiced = 0.f; r.noise = c.noiseGain; r.noiseHz = c.noiseHz; }
                else { r.amp = c.voiced ? 0.f : .7f; r.voiced = 0.f; r.asp = c.voiced ? 0.f : .35f; }
                if (!onset && local >= .65f) r.amp = 0.f;
                break;
            }
            case AFFR: {
                if (local < .3f) { r.amp = 0.f; }
                else { r.amp = 1.f; r.voiced = c.voiced ? .6f : 0.f; r.noise = c.noiseGain; r.noiseHz = c.noiseHz; }
                r.f1 = neighbour.f1; r.f2 = neighbour.f2; r.f3 = neighbour.f3;
                break;
            }
            case FRIC:
                r.f1 = neighbour.f1; r.f2 = neighbour.f2; r.f3 = neighbour.f3;
                r.amp = 1.f; r.voiced = c.voiced ? .55f : 0.f; r.noise = c.noiseGain; r.noiseHz = c.noiseHz;
                break;
            case ASPIR:
                r.f1 = neighbour.f1; r.f2 = neighbour.f2; r.f3 = neighbour.f3;
                r.amp = .8f; r.voiced = .15f; r.asp = .9f;
                break;
            case NASAL:
                r.f1 = c.f1; r.f2 = c.f2; r.f3 = c.f3; r.amp = .75f; r.voiced = 1.f;
                nasalGain_ = 1.f;
                break;
            case LIQUID:
                r.f1 = c.f1; r.f2 = c.f2; r.f3 = c.f3; r.amp = .95f; r.voiced = 1.f;
                break;
        }
        return r;
    }

    sing::Syllable syl_{};
    Seg seg_[8];
    int nseg_ = 0;
    uint32_t vowelStart_ = 0, codaStart_ = 0, planEnd_ = 0, t_ = 0;
    float phase_ = 0.f, gPrev_ = 0.f, vib_ = 0.f, croakPhase_ = 0.f, shimmer_ = 1.f, tilt_ = 0.f;
    float f_[3] = {0, 0, 0}, amp_ = 0.f, voiced_ = 0.f, asp_ = 0.f, nz_ = 0.f, nzHz_ = 5000.f, nasalGain_ = 1.f;
    float ic1_[4] = {0, 0, 0, 0}, ic2_[4] = {0, 0, 0, 0}, nic1_ = 0.f, nic2_ = 0.f;
    uint32_t noise_ = 0x1234ABCDu;
    bool first_ = true;
};

}  // namespace studio
