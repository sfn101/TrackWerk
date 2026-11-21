#pragma once
// PitchTracker: the pitch of a voice or an instrument heard through the microphone, for FROGGY's "Mic" pitch source.
// A YIN estimator (de Cheveigne and Kawahara, 2002; the method is public, this code is our own): the input is
// reduced to about 12 kHz, every 21 ms a 512-sample window is compared with itself shifted by lags that cover
// 40 … 500 Hz, the first dip of the cumulative-mean-normalised difference below the threshold is taken and refined by
// a parabola. A window that is quiet or has no clear dip reports 0 (no pitch). Fixed buffers, no allocation after
// construction; one hop costs about 140 k multiply-adds, i.e. 6 to 7 MFLOPS in all.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace studio {

class PitchTracker {
public:
    static constexpr int kWindow = 512, kMaxLag = 300, kMinLag = 24, kHop = 256;
    static constexpr float kRate = 12000.f;

    struct Result { float hz = 0.f, confidence = 0.f, level = 0.f; bool fresh = false; };

    PitchTracker() : ring_(kWindow + kMaxLag, 0.f), lin_(kWindow + kMaxLag, 0.f), diff_(kMaxLag + 2, 0.f), cmnd_(kMaxLag + 2, 0.f) {}

    // Feeds mono float samples at `rate` (Hz). Returns the newest result; `fresh` is true when a new window was
    // analysed during this call.
    Result feed(const float* mono, size_t n, int rate) {
        const double step = double(rate) / kRate;   // input samples per reduced sample
        const float a = alpha(rate);
        result_.fresh = false;
        for (size_t i = 0; i < n; ++i) {
            // two one-pole stages (about 3 kHz at 12 kHz output) before dropping samples
            lp1_ += (mono[i] - lp1_) * a;
            lp2_ += (lp1_ - lp2_) * a;
            phase_ += 1.0;
            if (phase_ >= step) {
                phase_ -= step;
                push(lp2_);
            }
        }
        return result_;
    }
    const Result& last() const { return result_; }
    void reset() { std::fill(ring_.begin(), ring_.end(), 0.f); head_ = 0; fill_ = 0; sinceHop_ = 0; lp1_ = lp2_ = 0.f; phase_ = 0.0; result_ = Result{}; }

private:
    static float alpha(int rate) { return 1.f - std::exp(-6.2831853f * 3000.f / float(rate)); }
    void push(float v) {
        ring_[size_t(head_)] = v;                       // a circular buffer: no memmove per sample
        head_ = (head_ + 1) % int(ring_.size());
        if (fill_ < int(ring_.size())) ++fill_;
        if (fill_ == int(ring_.size()) && ++sinceHop_ >= kHop) { sinceHop_ = 0; analyse(); }
    }
    void analyse() {
        for (size_t k = 0; k < lin_.size(); ++k) lin_[k] = ring_[(size_t(head_) + k) % ring_.size()];   // oldest first, once per hop
        const float* x = lin_.data();
        double energy = 0;
        for (int j = 0; j < kWindow; ++j) energy += double(x[j]) * x[j];
        result_.level = float(std::sqrt(energy / kWindow));
        result_.fresh = true;
        if (result_.level < .008f) { result_.hz = 0.f; result_.confidence = 0.f; return; }
        for (int tau = 1; tau <= kMaxLag; ++tau) {
            double d = 0;
            for (int j = 0; j < kWindow; ++j) { const float e = x[j] - x[j + tau]; d += double(e) * e; }
            diff_[size_t(tau)] = float(d);
        }
        cmnd_[0] = 1.f;
        double running = 0;
        for (int tau = 1; tau <= kMaxLag; ++tau) {
            running += diff_[size_t(tau)];
            cmnd_[size_t(tau)] = running > 0 ? float(diff_[size_t(tau)] * tau / running) : 1.f;
        }
        int best = -1;
        for (int tau = kMinLag; tau <= kMaxLag - 1; ++tau) {
            if (cmnd_[size_t(tau)] < .15f) {
                while (tau + 1 <= kMaxLag - 1 && cmnd_[size_t(tau + 1)] < cmnd_[size_t(tau)]) ++tau;   // down to the bottom of the dip
                best = tau;
                break;
            }
        }
        if (best < 0) { result_.hz = 0.f; result_.confidence = 0.f; return; }
        const float a = cmnd_[size_t(best - 1)], b = cmnd_[size_t(best)], c = cmnd_[size_t(best + 1)];
        const float denom = a - 2.f * b + c;
        const float shift = std::fabs(denom) > 1e-9f ? .5f * (a - c) / denom : 0.f;
        const float lag = float(best) + std::clamp(shift, -1.f, 1.f);
        result_.hz = kRate / lag;
        result_.confidence = std::clamp(1.f - b, 0.f, 1.f);
    }

    std::vector<float> ring_, lin_, diff_, cmnd_;
    int head_ = 0, fill_ = 0, sinceHop_ = 0;
    float lp1_ = 0.f, lp2_ = 0.f;
    double phase_ = 0.0;
    Result result_;
};

}  // namespace studio
