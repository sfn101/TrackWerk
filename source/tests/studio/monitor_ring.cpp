// SPDX-License-Identifier: GPL-3.0-or-later
// ptmonitor-ring-check: the input monitor's ring (shell/linux/monitor-ring.h), driven by a simulated
// capture worker and output callback on clocks that do not agree. The first block shows why the
// first version (ozkaoz, PR #6) could drop out; the rest checks the drift-following one.
#include "linux/monitor-ring.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

using ptshell::MonitorRing;

namespace {

constexpr double kPi = 3.14159265358979323846;

// The first version's consumer, kept as it was, to show the problem it had: play what is there, and
// when the ring runs dry end the block in silence and wait for two frames.
struct FirstVersion {
    static constexpr size_t CAP = 4096, MASK = CAP - 1;
    std::vector<float> ring = std::vector<float>(CAP * 2, 0.f);
    size_t read = 0, write = 0;
    double phase = 0;
    float cur[2] = {0, 0}, next[2] = {0, 0};
    bool primed = false;
    int rate = 48000, underruns = 0;
    void push(const unsigned char* pcm, size_t frames, int ch, int r) {
        rate = r;
        for (size_t i = 0; i < frames; ++i) {
            if (write - read >= CAP) break;
            auto smp = [](const unsigned char* q) { int v = q[0] | (q[1] << 8); if (v >= 32768) v -= 65536; return v / 32768.f; };
            const unsigned char* p = pcm + i * ch * 2;
            ring[(write & MASK) * 2] = smp(p);
            ring[(write & MASK) * 2 + 1] = ch >= 2 ? smp(p + 2) : smp(p);
            ++write;
        }
    }
    bool pop(float& l, float& r) {
        if (read == write) return false;
        l = ring[(read & MASK) * 2]; r = ring[(read & MASK) * 2 + 1]; ++read; return true;
    }
    void render(float* out, int frames, int outRate) {
        const double step = double(rate) / outRate;
        for (int i = 0; i < frames; ++i) {
            if (!primed) {
                if (!pop(cur[0], cur[1]) || !pop(next[0], next[1])) { primed = false; phase = 0; ++underruns; break; }
                primed = true; phase = 0;
            }
            while (phase >= 1.0) {
                cur[0] = next[0]; cur[1] = next[1];
                if (!pop(next[0], next[1])) { primed = false; phase = 0; ++underruns; break; }
                phase -= 1.0;
            }
            if (!primed) break;
            out[i * 2] += float(cur[0] + (next[0] - cur[0]) * phase);
            out[i * 2 + 1] += float(cur[1] + (next[1] - cur[1]) * phase);
            phase += step;
        }
    }
};

struct Sim {
    int inRate = 48000, outRate = 48000, chunk = 512, block = 512, channels = 2;
    double producerPpm = 0, consumerPpm = 0, jitterMs = 0, seconds = 60, freq = 1000, amp = .5;
    double stallAtS = -1, stallForS = 0;   // the producer pauses
    std::vector<float> left;               // the output, left channel
    uint32_t underruns = 0;
    size_t minFill = SIZE_MAX, maxFill = 0;
};

// Runs the simulation on one thread with exact event times; `Ring` is MonitorRing or FirstVersion.
template <class Ring>
void run(Sim& sim, Ring& ring, uint32_t (*countUnderruns)(Ring&), size_t (*fillOf)(Ring&)) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-sim.jitterMs, sim.jitterMs);
    const double pPeriod = sim.chunk / (sim.inRate * (1.0 + sim.producerPpm * 1e-6));
    const double cPeriod = sim.block / (sim.outRate * (1.0 + sim.consumerPpm * 1e-6));
    double nextP = 0, nextC = 0, phase = 0;
    uint64_t pk = 0, ck = 0;
    std::vector<unsigned char> pcm(size_t(sim.chunk) * sim.channels * 2);
    std::vector<float> out(size_t(sim.block) * 2);
    while (std::min(nextP, nextC) < sim.seconds) {
        if (nextP <= nextC) {
            const double t = nextP;
            if (!(sim.stallAtS >= 0 && t >= sim.stallAtS && t < sim.stallAtS + sim.stallForS)) {
                for (int i = 0; i < sim.chunk; ++i) {
                    const double v = sim.amp * std::sin(phase);
                    phase += 2 * kPi * sim.freq / sim.inRate;
                    const int q = int(std::lround(v * 32767));
                    for (int c = 0; c < sim.channels; ++c) {
                        pcm[(size_t(i) * sim.channels + c) * 2] = static_cast<unsigned char>(q & 255);
                        pcm[(size_t(i) * sim.channels + c) * 2 + 1] = static_cast<unsigned char>((q >> 8) & 255);
                    }
                }
                ring.push(pcm.data(), size_t(sim.chunk), sim.channels, sim.inRate);
            } else {
                phase += 2 * kPi * sim.freq / sim.inRate * sim.chunk;   // time passes on the source
            }
            ++pk;
            nextP = pk * pPeriod + jitter(rng) * 1e-3;
        } else {
            std::fill(out.begin(), out.end(), 0.f);
            ring.render(out.data(), sim.block, sim.outRate);
            for (int i = 0; i < sim.block; ++i) sim.left.push_back(out[size_t(i) * 2]);
            const size_t f = fillOf(ring);
            if (ck > 20) { sim.minFill = std::min(sim.minFill, f); sim.maxFill = std::max(sim.maxFill, f); }
            ++ck;
            nextC = ck * cPeriod;
        }
    }
    sim.underruns = countUnderruns(ring);
}

uint32_t under_new(MonitorRing& r) { return r.underruns(); }
size_t fill_new(MonitorRing& r) { return r.fill(); }
uint32_t under_old(FirstVersion& r) { return uint32_t(r.underruns); }
size_t fill_old(FirstVersion& r) { return r.write - r.read; }

// Largest step between neighbouring output samples after `skipSeconds`, and how many exceed `limit`.
struct Steps { double largest = 0; int over = 0; };
Steps steps(const std::vector<float>& x, double skipSeconds, int rate, double limit) {
    Steps s;
    for (size_t i = size_t(skipSeconds * rate) + 1; i < x.size(); ++i) {
        const double d = std::fabs(double(x[i]) - x[i - 1]);
        s.largest = std::max(s.largest, d);
        if (d > limit) ++s.over;
    }
    return s;
}
// Frequency from positive-going zero crossings in x[from, to).
double frequency(const std::vector<float>& x, size_t from, size_t to, int rate) {
    double first = -1, last = -1;
    int n = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = double(i - 1) + double(-x[i - 1]) / double(x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            ++n;
        }
    return n < 2 ? 0 : double(n - 1) * rate / (last - first);
}
double peak(const std::vector<float>& x, size_t from) {
    double m = 0;
    for (size_t i = from; i < x.size(); ++i) m = std::max(m, double(std::fabs(x[i])));
    return m;
}

}  // namespace

int main() {
    // A 1 kHz sine at amplitude 0.5 changes by at most 0.5·2π·1000/48000 = 0.0654 per sample; a click
    // or a cut to silence shows as a much larger step.
    const double kSlope = .5 * 2 * kPi * 1000 / 48000;
    const double kLimit = kSlope * 1.5;

    // 1 · The first version drops out on clocks that differ by a few hundred ppm with a little jitter.
    {
        Sim sim; sim.producerPpm = 600; sim.consumerPpm = -200; sim.jitterMs = 1.5; sim.seconds = 60;
        FirstVersion old;
        run<FirstVersion>(sim, old, under_old, fill_old);
        const Steps st = steps(sim.left, 2, 48000, kLimit);
        std::printf("first version, 60 s at +800 ppm, ±1.5 ms jitter: %u dropouts, %d clicks\n", sim.underruns, st.over);
        assert(sim.underruns > 0);
    }

    // 2 · The drift-following ring on the same clocks: no dropout, no click, cushion kept, pitch exact.
    for (int channels : {2, 1}) {
        Sim sim; sim.producerPpm = 600; sim.consumerPpm = -200; sim.jitterMs = 1.5; sim.seconds = 120; sim.channels = channels;
        MonitorRing ring; ring.enable();
        run<MonitorRing>(sim, ring, under_new, fill_new);
        const Steps st = steps(sim.left, 2, 48000, kLimit);
        const double f = frequency(sim.left, size_t(10 * 48000), size_t(100 * 48000), 48000);
        std::printf("new ring, %d ch, 120 s at +800 ppm, ±1.5 ms jitter: %u dropouts, largest step %.4f (sine %.4f), "
                    "cushion %zu…%zu frames, %.2f Hz\n", channels, sim.underruns, st.largest, kSlope, sim.minFill, sim.maxFill, f);
        assert(sim.underruns == 0 && st.over == 0);
        // The cushion before each callback: a block plus margin at the least, and bounded (latency) at the most.
        assert(sim.minFill >= 700 && sim.maxFill <= 2400);
        assert(std::fabs(f - 1000.0) < 1.5);                 // the trim moves the pitch by well under 0.2 %
    }

    // 3 · The other direction (input clock slower), several ppm settings and two block sizes.
    for (double ppm : {-1500.0, -300.0, 0.0, 300.0, 1500.0})
        for (int block : {256, 512, 1024}) {
            Sim sim; sim.producerPpm = ppm; sim.block = block; sim.jitterMs = 2.0; sim.seconds = 60;
            MonitorRing ring; ring.enable();
            run<MonitorRing>(sim, ring, under_new, fill_new);
            const Steps st = steps(sim.left, 2, 48000, kLimit);
            std::printf("  ppm %+6.0f block %4d: %u dropouts, %d clicks, cushion %zu…%zu\n", ppm, block, sim.underruns, st.over, sim.minFill, sim.maxFill);
            assert(sim.underruns == 0 && st.over == 0 && sim.minFill >= 64 && sim.maxFill <= size_t(192 + block + 2 * std::max(block, 512) + 256 + std::max(block, 512) + 512));
        }

    // 4 · A 44.1 kHz interface into a 48 kHz output: the interpolation keeps the pitch.
    {
        Sim sim; sim.inRate = 44100; sim.chunk = 441; sim.producerPpm = 300; sim.jitterMs = 1.0; sim.seconds = 60;
        MonitorRing ring; ring.enable();
        run<MonitorRing>(sim, ring, under_new, fill_new);
        const Steps st = steps(sim.left, 2, 48000, kLimit);
        const double f = frequency(sim.left, size_t(10 * 48000), size_t(55 * 48000), 48000);
        std::printf("44.1 kHz into 48 kHz: %u dropouts, largest step %.4f, %.2f Hz\n", sim.underruns, st.largest, f);
        assert(sim.underruns == 0 && st.over == 0 && std::fabs(f - 1000.0) < 1.5);
    }

    // 5 · The capture stalls for 200 ms (a USB hiccup): the monitor fades, counts it, and comes back.
    {
        Sim sim; sim.seconds = 20; sim.stallAtS = 10; sim.stallForS = .2;
        MonitorRing ring; ring.enable();
        run<MonitorRing>(sim, ring, under_new, fill_new);
        // After the stall: a clean sine again from 11 s on; the gap itself must not click.
        const Steps afterwards = steps(std::vector<float>(sim.left.begin() + 11 * 48000, sim.left.end()), 0, 48000, kLimit);
        const Steps whole = steps(sim.left, 2, 48000, kLimit * 1.5);
        std::printf("200 ms stall: %u underrun(s), largest step overall %.4f\n", sim.underruns, whole.largest);
        assert(sim.underruns >= 1 && sim.underruns <= 3 && afterwards.over == 0 && whole.over <= 4);
        assert(peak(sim.left, size_t(11 * 48000)) > .4);   // it really came back
    }

    // 6 · Enable / disable: nothing of an earlier session is heard, the ring is empty after a disable.
    {
        MonitorRing ring;
        std::vector<unsigned char> pcm(512 * 4, 0x40);
        std::vector<float> out(512 * 2, 0.f);
        ring.push(pcm.data(), 512, 2, 48000);   // disabled: nothing stored
        assert(ring.fill() == 0);
        ring.enable();
        for (int i = 0; i < 4; ++i) ring.push(pcm.data(), 512, 2, 48000);
        ring.disable();
        ring.render(out.data(), 512, 48000);
        for (float v : out) assert(v == 0.f);   // disabled: silent, and it does not consume
        ring.push(pcm.data(), 512, 2, 48000);   // the producer notices and empties the ring
        assert(ring.fill() == 0);
        ring.enable();
        ring.push(pcm.data(), 512, 2, 48000);   // old data was dropped by the flush
        ring.render(out.data(), 512, 48000);    // fewer frames than the cushion: still waiting
        for (float v : out) assert(std::fabs(v) < 1e-6f);
        // A take ends: nothing of it may be heard in the next one.
        for (int i = 0; i < 3; ++i) ring.push(pcm.data(), 512, 2, 48000);
        ring.producer_reset();
        assert(ring.fill() == 0);
    }

    // 7 · A backlog bigger than the ring keeps only its newest half; a stalled consumer never blocks.
    {
        MonitorRing ring; ring.enable();
        std::vector<unsigned char> pcm(20000 * 4, 0x10);
        ring.push(pcm.data(), 20000, 2, 48000);
        assert(ring.fill() <= MonitorRing::CAPACITY / 2);
        for (int i = 0; i < 50; ++i) ring.push(pcm.data(), 2000, 2, 48000);
        assert(ring.fill() <= MonitorRing::CAPACITY);
    }

    // 8 · Three real threads: the worker pushes, the callback renders, the UI toggles. No crash, nothing
    // non-finite, and the ring never reports more than its capacity.
    {
        MonitorRing ring;
        std::atomic<bool> run{true};
        std::atomic<bool> bad{false};
        std::thread producer([&] {
            std::vector<unsigned char> pcm(512 * 4, 0x20);
            while (run.load()) { ring.push(pcm.data(), 512, 2, 44100); std::this_thread::sleep_for(std::chrono::microseconds(900)); }
        });
        std::thread consumer([&] {
            std::vector<float> out(512 * 2);
            while (run.load()) {
                std::fill(out.begin(), out.end(), 0.f);
                ring.render(out.data(), 512, 48000);
                for (float v : out) if (!std::isfinite(v) || std::fabs(v) > 2.f) bad = true;
                if (ring.fill() > MonitorRing::CAPACITY) bad = true;
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
            }
        });
        for (int i = 0; i < 400; ++i) {
            if (i % 2) ring.enable(); else ring.disable();
            if (i % 50 == 0) ring.producer_reset();
            std::this_thread::sleep_for(std::chrono::microseconds(1500));
        }
        run = false;
        producer.join();
        consumer.join();
        assert(!bad.load());
    }
    std::printf("MONITOR_RING_PASS\n");
}
