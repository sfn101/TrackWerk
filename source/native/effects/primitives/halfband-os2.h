#pragma once

// Runs a memoryless curve (a drive's) at twice the sample rate: up ×2 through a 31-tap half-band
// filter, the curve on both samples, then back down through the same filter. Frequencies the curve
// creates above the base rate's Nyquist are filtered out instead of folding back as harsh,
// out-of-tune tones (specs/oversampled-drive.md). Adds DELAY frames of latency. One per channel.
struct HalfbandOS2 {
    static constexpr int DELAY = 15;   // base-rate frames, up and down together

    // The half-band's even taps (Blackman-windowed sinc, summing to 0.5); its centre tap is 0.5 and
    // its other odd taps are 0, so each phase needs only these 16.
    static constexpr float H[16] = {
        0.0f,            4.103266766e-04f, -2.230306205e-03f, 7.100922993e-03f,
        -1.791719660e-02f, 4.010778965e-02f, -9.010775783e-02f, 3.126362213e-01f,
        3.126362213e-01f, -9.010775783e-02f, 4.010778965e-02f, -1.791719660e-02f,
        7.100922993e-03f, -2.230306205e-03f, 4.103266766e-04f, 0.0f};

    float x[32] = {};      // the input, twice over so a run of 16 never wraps
    float vOdd[32] = {};   // the curve's odd-phase outputs, likewise
    float vEven[8] = {};   // its even-phase outputs: only the one 7 back is read
    int   n = 0;

    void reset() { *this = HalfbandOS2{}; }

    template <typename Curve>
    inline float process(float in, Curve curve) {
        n = (n + 1) & 15;
        x[n] = x[n + 16] = in;
        // Up: the even phase filters the input, the odd phase is the input 7 frames back.
        float up = 0.0f;
        const float* xs = &x[n + 16];
        for (int j = 0; j < 16; ++j) up += H[j] * xs[-j];
        up *= 2.0f;
        const float vE = curve(up);
        const float vO = curve(x[((n - 7) & 15)]);
        vEven[n & 7] = vE;
        vOdd[n] = vOdd[n + 16] = vO;
        // Down: the odd phase through the even taps, plus the centre tap on the even phase 7 back.
        float out = 0.5f * vEven[(n - 7) & 7];
        const float* vs = &vOdd[n + 16];
        for (int j = 0; j < 16; ++j) out += H[j] * vs[-j];
        return out;
    }
};
