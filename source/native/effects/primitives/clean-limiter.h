#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

// PROJECT > LIMITER CLEAN (specs/master-limiter.md): one gain for both channels, a 1.5 ms look-ahead,
// a −0.3 dBFS sample-peak ceiling and a release that lengthens on dense material. Below the ceiling it
// passes the input unchanged, delayed by `lookahead` frames. Fixed buffers: nothing is allocated.
//
// How the gain stays under the ceiling: `need` is the gain each input frame needs. The gain used is
// the average, over the last L+1 frames, of the minimum of `need` over the last L+1 frames. Every one
// of those windows holds the frame now leaving the L-frame delay, so the average never exceeds what
// that frame needs, and it eases down over L frames instead of jumping. Release only moves the gain up
// toward that average, never above it.
struct CleanLimiter {
    static constexpr int   MAX_LOOKAHEAD = 511;           // 1.5 ms up to 340 kHz
    static constexpr int   RING          = MAX_LOOKAHEAD + 1;
    static constexpr float CEILING       = 0.96605088f;   // −0.3 dBFS

    int   lookahead = 72;   // frames
    float delayL[RING] = {}, delayR[RING] = {};
    float needRing[RING] = {};   // `need` per frame
    float minRing[RING]  = {};   // the window minima, for their average
    int64_t queue[RING + 1] = {};   // frames whose `need` rises from head to tail: the window minimum
    int   qHead = 0, qTail = 0;
    int64_t frame = 0;
    double minSum = 0.0;
    float gain = 1.0f, density = 0.0f;
    float releaseFast = 0.0f, releaseSlow = 0.0f, densityCoef = 0.0f;

    static int lookahead_frames(float sampleRate) {
        return std::clamp(static_cast<int>(std::lround(sampleRate * 0.0015f)), 1, MAX_LOOKAHEAD);
    }

    void prepare(float sampleRate) {
        lookahead = lookahead_frames(sampleRate);
        // Release 50 ms on isolated peaks, up to 500 ms once limiting is constant; `density` is the
        // share of frames being limited over about a second.
        releaseFast = 1.0f - std::exp(-1.0f / (0.050f * sampleRate));
        releaseSlow = 1.0f - std::exp(-1.0f / (0.500f * sampleRate));
        densityCoef = 1.0f - std::exp(-1.0f / sampleRate);
        reset();
    }

    void reset() {
        std::fill(std::begin(delayL), std::end(delayL), 0.0f);
        std::fill(std::begin(delayR), std::end(delayR), 0.0f);
        std::fill(std::begin(needRing), std::end(needRing), 1.0f);
        std::fill(std::begin(minRing), std::end(minRing), 1.0f);
        qHead = qTail = 0;
        frame = 0;
        minSum = lookahead + 1;
        gain = 1.0f;
        density = 0.0f;
    }

    // Interleaved, in place; `preGain` is the mixer's LIMITER drive, as CLASSIC takes it.
    void process(float* buf, int numFrames, int channelCount, float preGain) {
        const int size = lookahead + 1;
        for (int i = 0; i < numFrames; i++, frame++) {
            float* const s = &buf[i * channelCount];
            const float inL = s[0] * preGain;
            const float inR = (channelCount > 1 ? s[1] : s[0]) * preGain;
            const float peak = std::max(std::fabs(inL), std::fabs(inR));
            const float need = peak > CEILING ? CEILING / peak : 1.0f;
            const int slot = static_cast<int>(frame % size);

            // Window minimum: drop candidates no lower than this frame, then the one that left.
            needRing[slot] = need;
            while (qTail != qHead && needRing[queue[(qTail + RING) % (RING + 1)] % size] >= need)
                qTail = (qTail + RING) % (RING + 1);
            queue[qTail] = frame;
            qTail = (qTail + 1) % (RING + 1);
            if (queue[qHead] <= frame - size) qHead = (qHead + 1) % (RING + 1);
            const float windowMin = needRing[queue[qHead] % size];

            minSum += static_cast<double>(windowMin) - minRing[slot];
            minRing[slot] = windowMin;
            float target = static_cast<float>(minSum / size);
            if (target > 0.999999f) target = 1.0f;   // rounding left in the sum must not touch quiet audio

            density += ((need < 1.0f ? 1.0f : 0.0f) - density) * densityCoef;
            if (target < gain) gain = target;
            else gain += (target - gain) * (releaseFast + (releaseSlow - releaseFast) * density);
            if (gain > 0.999999f) gain = 1.0f;

            // The frame `lookahead` back sits in the next slot, which this frame overwrites next turn.
            delayL[slot] = inL;
            delayR[slot] = inR;
            const int out = (slot + 1) % size;
            // The clamp only catches float rounding: the gain already keeps every frame under it.
            s[0] = std::clamp(delayL[out] * gain, -CEILING, CEILING);
            if (channelCount > 1) s[1] = std::clamp(delayR[out] * gain, -CEILING, CEILING);
        }
    }
};
