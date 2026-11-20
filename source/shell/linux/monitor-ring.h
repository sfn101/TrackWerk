// Input monitoring: what the capture worker records is heard through the already-open output. The
// worker (the only producer) pushes PCM16 into a lock-free single-producer ring; the output callback
// (the only consumer) drains it. No lock crosses that boundary and nothing allocates after
// construction. Header-only so the host tests can drive it without SDL or ALSA.
//
// The two ends run on different clocks (an interface's crystal against the SoC's), and the producer
// delivers a chunk at a time, so the fill level is a sawtooth: it drops by one block per callback and
// jumps by one chunk per delivery. A plain "play what is there" consumer runs dry whenever a delivery
// is a little late and then goes silent for the rest of that block. This one instead
//   - starts only once a cushion has built up (two blocks plus kLowFrames),
//   - watches the LOWEST and the HIGHEST fill of the last 16 callbacks (the sawtooth's valley and
//     peak, which a single reading cannot tell from the clocks drifting) and does nothing while both
//     lie inside a band,
//   - outside the band it plays up to 0.5 % faster or slower, in proportion to how far outside (a
//     pitch change of a few cents that follows the clock drift instead of fighting it),
//   - and if it runs dry anyway, fades the last sample out in about a millisecond rather than cutting it.
// Contributed idea and first version: ozkaoz (PR #6); this is the drift-following revision.
#ifndef POCKETTRACKER_MONITOR_RING_H
#define POCKETTRACKER_MONITOR_RING_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ptshell {

class MonitorRing {
  public:
    static constexpr size_t CAPACITY = 8192;   // frames; about 170 ms at 48 kHz
    static constexpr size_t MASK = CAPACITY - 1;
    // The margin above one whole callback that the fill must keep after each callback (4 ms at 48 kHz).
    // A callback needs a block's worth of frames the moment it runs, and a delivery that is late by even
    // a moment means the next one finds only what was left, so the valley must stay a block plus this.
    static constexpr int kLowFrames = 192;
    static constexpr double kMaxRateTrim = 0.005;   // ±0.5 %
    static constexpr double kTrimPerFrame = kMaxRateTrim / 256.0;   // full trim 256 frames outside the band

    MonitorRing() : ring_(CAPACITY * 2, 0.0f) {}

    // ── Any thread ────────────────────────────────────────────────────────────────────────────────
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    // Starting always starts clean: the consumer drops whatever is in the ring before its next block.
    // ⚠️ This never touches the read/write indices: each belongs to exactly one thread.
    void enable() {
        flushRequested_.store(true, std::memory_order_relaxed);
        enabled_.store(true, std::memory_order_relaxed);
    }
    void disable() {
        enabled_.store(false, std::memory_order_relaxed);
        flushRequested_.store(true, std::memory_order_relaxed);
    }
    uint32_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    size_t fill() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire); }

    // ── Producer (the capture worker) ─────────────────────────────────────────────────────────────
    void push(const unsigned char* pcm, size_t frames, int channels, int captureRate) {
        rate_.store(captureRate, std::memory_order_relaxed);
        lastChunk_.store(static_cast<int>(std::min<size_t>(frames, CAPACITY / 2)), std::memory_order_relaxed);
        if (!enabled_.load(std::memory_order_relaxed)) {
            // Disabled: keep the ring empty from this side so a later enable starts from nothing.
            write_.store(read_.load(std::memory_order_acquire), std::memory_order_release);
            return;
        }
        // A chunk bigger than half the ring keeps its newest half: a device that hands over a backlog
        // is heard now, not half a second ago.
        size_t first = 0;
        if (frames > CAPACITY / 2) first = frames - CAPACITY / 2;
        size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        const size_t frameBytes = static_cast<size_t>(channels) * 2;
        auto sample = [](const unsigned char* q) {
            int value = q[0] | (static_cast<int>(q[1]) << 8);
            if (value >= 32768) value -= 65536;
            return static_cast<float>(value) / 32768.0f;
        };
        for (size_t i = first; i < frames; ++i) {
            if (w - r >= CAPACITY) break;   // consumer stalled: drop the new frames, never block
            const unsigned char* p = pcm + i * frameBytes;
            const float l = sample(p);
            const float rr = channels >= 2 ? sample(p + 2) : l;
            const size_t slot = w & MASK;
            ring_[slot * 2] = l;
            ring_[slot * 2 + 1] = rr;
            ++w;
        }
        write_.store(w, std::memory_order_release);
    }
    // The producer ends its take: nothing of it may be heard in the next one.
    void producer_reset() { write_.store(read_.load(std::memory_order_acquire), std::memory_order_release); }

    // ── Consumer (the output callback) ────────────────────────────────────────────────────────────
    // Adds the monitor to `stereo` (interleaved float, `frames` frames at `outputRate`).
    void render(float* stereo, int frames, int outputRate) {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        if (flushRequested_.exchange(false, std::memory_order_relaxed)) {
            read_.store(write_.load(std::memory_order_acquire), std::memory_order_relaxed);
            primed_ = false;
            phase_ = 0.0;
            hold_[0] = hold_[1] = 0.f;
            trim_ = 0.0;
            history_ = 0;
        }
        const int inRate = rate_.load(std::memory_order_relaxed);
        const double base = (inRate > 0 && outputRate > 0) ? double(inRate) / outputRate : 1.0;
        const size_t pre = fill();

        // The band for the valley and the peak of the sawtooth, in frames. The valley (fill after a
        // callback) must stay above one block plus kLowFrames; the peak (fill before one) may rise by
        // two blocks and two chunks above that, so the latency stays bounded.
        const int chunk = std::max(lastChunk_.load(std::memory_order_relaxed), 1);
        const double low = frames + kLowFrames;
        const double high = low + 2.0 * std::max(frames, chunk) + 256.0;

        // Trim: from the valley and the peak of the last few callbacks (none yet: no trim).
        double target = 0.0;
        if (history_ >= 4) {
            const int n = std::min(history_, kHistory);
            size_t valley = SIZE_MAX, peak = 0;
            for (int i = 0; i < n; ++i) {
                valley = std::min(valley, post_[i]);
                peak = std::max(peak, pre_[i]);
            }
            if (double(valley) < low) target = -std::min(kMaxRateTrim, (low - double(valley)) * kTrimPerFrame);
            else if (double(peak) > high) target = std::min(kMaxRateTrim, (double(peak) - high) * kTrimPerFrame);
        }
        trim_ += (target - trim_) * .2;   // changes over a few callbacks, never as a step
        const double step = base * (1.0 + trim_);

        for (int i = 0; i < frames; ++i) {
            if (!primed_) {
                // Wait for the cushion (the band's floor plus one callback); meanwhile the last sample
                // fades out instead of stopping dead.
                // (never ask for more than the ring can hold: a very large output block would never prime)
                if (fill() < std::min<size_t>(size_t(2 * frames + kLowFrames), CAPACITY - 64) || !pop(cur_[0], cur_[1]) || !pop(next_[0], next_[1])) {
                    fade(stereo, i, frames);
                    return;
                }
                primed_ = true;
                phase_ = 0.0;
                history_ = 0;
            }
            while (phase_ >= 1.0) {
                cur_[0] = next_[0];
                cur_[1] = next_[1];
                if (!pop(next_[0], next_[1])) {
                    // Ran dry in spite of the trim (a stalled producer): count it, fade, wait for the cushion.
                    underruns_.fetch_add(1, std::memory_order_relaxed);
                    primed_ = false;
                    phase_ = 0.0;
                    fade(stereo, i, frames);
                    return;
                }
                phase_ -= 1.0;
            }
            const float l = static_cast<float>(cur_[0] + (next_[0] - cur_[0]) * phase_);
            const float r = static_cast<float>(cur_[1] + (next_[1] - cur_[1]) * phase_);
            stereo[i * 2] += l;
            stereo[i * 2 + 1] += r;
            hold_[0] = l;
            hold_[1] = r;
            phase_ += step;
        }
        pre_[history_ % kHistory] = pre;
        post_[history_ % kHistory] = fill();
        ++history_;
    }

  private:
    bool pop(float& l, float& r) {
        const size_t r0 = read_.load(std::memory_order_relaxed);
        if (r0 == write_.load(std::memory_order_acquire)) return false;
        const size_t slot = r0 & MASK;
        l = ring_[slot * 2];
        r = ring_[slot * 2 + 1];
        read_.store(r0 + 1, std::memory_order_release);
        return true;
    }
    // The last output decays over the rest of the block (to 1 % in about 60 samples, 1.3 ms), so a gap
    // is a soft dip and not a click.
    void fade(float* stereo, int from, int frames) {
        constexpr float kDecay = .93f;
        for (int i = from; i < frames; ++i) {
            hold_[0] *= kDecay;
            hold_[1] *= kDecay;
            stereo[i * 2] += hold_[0];
            stereo[i * 2 + 1] += hold_[1];
        }
    }

    std::vector<float> ring_;
    std::atomic<size_t> read_{0}, write_{0};
    std::atomic<bool> enabled_{false}, flushRequested_{false};
    std::atomic<int> rate_{48000}, lastChunk_{512};
    std::atomic<uint32_t> underruns_{0};

    // Consumer-only state.
    static constexpr int kHistory = 16;   // callbacks (about 170 ms) the valley and peak are taken over
    size_t pre_[kHistory] = {}, post_[kHistory] = {};
    int history_ = 0;
    double phase_ = 0.0, trim_ = 0.0;
    float cur_[2] = {0, 0}, next_[2] = {0, 0}, hold_[2] = {0, 0};
    bool primed_ = false;
};

}  // namespace ptshell

#endif
