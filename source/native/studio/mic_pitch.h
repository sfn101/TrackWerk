#pragma once
// The pitch the microphone currently hears, handed from the capture worker (shell) to the synth (audio thread).
// One lock-free float; 0 means "no pitch" (silence, noise, or the recorder is not listening). Written by the capture
// thread only, read by the audio thread; a stale value is cleared when the take ends.
#include <atomic>

namespace studio {
inline std::atomic<float> kMicPitchHz{0.f};
inline void mic_pitch_publish(float hz) { kMicPitchHz.store(hz, std::memory_order_relaxed); }
inline float mic_pitch_read() { return kMicPitchHz.load(std::memory_order_relaxed); }
}  // namespace studio
