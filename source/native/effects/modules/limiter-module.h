#pragma once
#include "../primitives/daisysp/limiter.h"
#include "../primitives/clean-limiter.h"

// The master bus's limiter, PROJECT > LIMITER. CLASSIC (0) is the DaisySP soft limiter, one per
// channel, run a sample at a time on the interleaved buffer; CLEAN (1) is CleanLimiter.
// `preGain` above 1 drives either harder.
struct LimiterModule {
    daisysp::Limiter limL, limR;
    CleanLimiter     clean;
    float preGain = 1.0f;
    int   mode    = 0;

    void reset() {
        limL.Init();
        limR.Init();
    }

    // CLEAN's buffers follow the rate; the master chain calls this on every reset.
    void prepare(float sampleRate) {
        reset();
        clean.prepare(sampleRate);
    }

    void setPreGain(float g) { preGain = g; }

    // A switch starts the new limiter from rest.
    void setMode(int m) {
        if (m == mode) return;
        mode = m;
        reset();
        clean.reset();
    }

    // The output delay this limiter adds, in frames.
    int latencyFrames() const { return mode == 1 ? clean.lookahead : 0; }

    void process(float* buf, int numFrames, int channelCount) {
        if (mode == 1) {
            clean.process(buf, numFrames, channelCount, preGain);
            return;
        }
        for (int i = 0; i < numFrames; i++) {
            limL.ProcessBlock(&buf[i * channelCount],     1, preGain);
            limR.ProcessBlock(&buf[i * channelCount + 1], 1, preGain);
        }
    }
};
