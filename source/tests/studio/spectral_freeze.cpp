// SP FREEZE (model 117): freezes a moment of a sample slot as a playable spectrum.
// Measured through studio::Synth at 48 kHz with an 8192-point Hann FFT, and once through the real
// engine path (a synth slot freezing another instrument's sample).
#include "audio-engine.h"
#include "common/platform_memory.h"
#include "kiss_fftr.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/synth.h"
#include "studio/synth_params.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <sys/mman.h>
#include <vector>

namespace { std::atomic<bool> audit{false}; std::atomic<int> allocations{0}; }
void* operator new(std::size_t n) { if (audit.load()) ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace studio;

namespace {
constexpr float kRate = 48000.f, kC4 = 261.6256f;
constexpr int kFft = 8192;

std::vector<float> tone(float hz, int frames, float level = .5f) {
    std::vector<float> x(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) x[size_t(i)] = level * std::sin(6.2831853f * hz * i / kRate);
    return x;
}

std::vector<float> render(Synth& s, float hz, int frames, const std::vector<float>* src, uint32_t gen = 1) {
    std::vector<float> out;
    for (int i = 0; i < frames; i += 256) {   // the engine lends the source once per piece
        const Synth::SourceLoan loan(s, src ? src->data() : nullptr, src ? int(src->size()) : 0, gen, 48000);
        for (int j = 0; j < 256 && i + j < frames; ++j) { const float v = s.next(hz, kRate); assert(std::isfinite(v)); out.push_back(v); }
    }
    return out;
}

std::vector<double> spectrum(const std::vector<float>& x, size_t at) {
    kiss_fftr_cfg cfg = kiss_fftr_alloc(kFft, 0, nullptr, nullptr);
    std::vector<float> w(kFft);
    for (int i = 0; i < kFft; ++i) w[size_t(i)] = x[at + size_t(i)] * (.5f - .5f * std::cos(6.2831853f * i / (kFft - 1)));
    std::vector<kiss_fft_cpx> b(kFft / 2 + 1);
    kiss_fftr(cfg, w.data(), b.data());
    kiss_fftr_free(cfg);
    std::vector<double> p(b.size());
    for (size_t k = 0; k < b.size(); ++k) p[k] = double(b[k].r) * b[k].r + double(b[k].i) * b[k].i;
    return p;
}
float peak_hz(const std::vector<double>& p) {
    size_t best = 1; for (size_t k = 1; k < p.size(); ++k) if (p[k] > p[best]) best = k;
    return float(best) * kRate / kFft;
}
double rms(const std::vector<float>& x, size_t from, size_t n) { double e = 0; for (size_t i = from; i < from + n; ++i) e += double(x[i]) * x[i]; return std::sqrt(e / n); }
int code(int a, int b, int c) { return custom_code(kSpectralFreezeModel, a, b, c); }
SynthParams jitter(int v) { auto p = default_synth_params(); p[3] = v; return p; }

void wav(const std::filesystem::path& path, const std::vector<float>& x, uint32_t rate = 48000) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(x.size() * 2);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    f.write("data", 4); u32(bytes);
    for (float v : x) { const int16_t s = int16_t(std::lround(std::fmax(-1.f, std::fmin(1.f, v)) * 32767)); f.write(reinterpret_cast<const char*>(&s), 2); }
}
// The tone on the left and inverted on the right of a 16-bit stereo WAV: identical sides would load
// as mono (specs/fake-stereo-mono.md).
void wav_stereo(const std::filesystem::path& path, const std::vector<float>& x, uint32_t rate = 48000) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(x.size() * 4);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    for (float v : x) { const int16_t s = int16_t(std::lround(std::fmax(-1.f, std::fmin(1.f, v)) * 32767)); const int16_t r = int16_t(-s); f.write(reinterpret_cast<const char*>(&s), 2); f.write(reinterpret_cast<const char*>(&r), 2); }
}
}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // Registration.
    assert(kSpectralFirstModel + kSpectralModelCount == kVoiceFirstModel && model_count() > kVoiceFirstModel && std::string(model_name(kSpectralFreezeModel)) == "SP FREEZE");
    assert(std::string(macro_name(kSpectralFreezeModel, 0)) == "Position" && std::string(macro_name(kSpectralFreezeModel, 1)) == "Smear" &&
           std::string(macro_name(kSpectralFreezeModel, 2)) == "Tilt");
    assert(synth_param_count(kSpectralFreezeModel) == 4 && synth_param_value(kSpectralFreezeModel, kFreezeSourceParam, 5) == "Slot 05");
    assert(preset_name(preset_code(kSpectralFreezeModel, 0)) == "SP FREEZE / First Breath" && !synth_needs_strike(kSpectralFreezeModel));

    const auto a440 = tone(440.f, 96000);
    // C4 plays the source at its own pitch; an octave up doubles it.
    {
        Synth s; const int c = code(128, 0, 128); s.start(c); s.update(c, jitter(0));
        const auto x = render(s, kC4, 48000, &a440);
        assert(std::fabs(peak_hz(spectrum(x, 24000)) - 440.f) < 8.f);
        Synth up; up.start(c); up.update(c, jitter(0));
        const auto y = render(up, 2 * kC4, 48000, &a440);
        assert(std::fabs(peak_hz(spectrum(y, 24000)) - 880.f) < 12.f);
        // Steady: with no jitter the frozen partial does not beat (phase-vocoder frequencies).
        double lo = 1e9, hi = 0;
        for (size_t at = 12000; at + 2048 <= x.size(); at += 2048) { const double r = rms(x, at, 2048); lo = std::min(lo, r); hi = std::max(hi, r); }
        assert(hi / lo < 1.15 && lo > .03);
    }
    // Position chooses the moment: 440 Hz in the first half, 660 Hz in the second.
    {
        auto two = tone(440.f, 48000); const auto second = tone(660.f, 48000); two.insert(two.end(), second.begin(), second.end());
        for (int a : {0, 255}) {
            Synth s; const int c = code(a, 0, 128); s.start(c); s.update(c, jitter(0));
            const auto x = render(s, kC4, 36000, &two);
            assert(std::fabs(peak_hz(spectrum(x, 24000)) - (a ? 660.f : 440.f)) < 8.f);
        }
        // A different sample in the slot (new generation) is frozen afresh.
        Synth s; const int c = code(128, 0, 128); s.start(c); s.update(c, jitter(0));
        render(s, kC4, 24000, &a440, 1);
        const auto e330 = tone(330.f, 96000);
        const auto x = render(s, kC4, 36000, &e330, 2);
        assert(std::fabs(peak_hz(spectrum(x, 24000)) - 330.f) < 8.f);
    }
    // No usable source (none, too short): the built-in vowel speaks, never silence or garbage.
    {
        const std::vector<float> tiny(100, .3f);
        for (const std::vector<float>* src : {static_cast<const std::vector<float>*>(nullptr), &tiny}) {
            Synth s; const int c = preset_code(kSpectralFreezeModel, 1); s.start(c); s.update(c, default_synth_params());
            const auto x = render(s, kC4, 24000, src);
            assert(rms(x, 8000, 12000) > .03);
        }
    }
    // Smear lets a changing source breathe; at 0 the freeze is still.
    {
        std::vector<float> sweep(96000);
        for (int i = 0; i < 96000; ++i) sweep[size_t(i)] = .5f * std::sin(6.2831853f * (200.f * i / kRate + 400.f * i * i / (2.f * kRate * 96000.f)));
        auto motion = [&](int smear) {
            Synth s; const int c = code(128, smear, 128); s.start(c); s.update(c, jitter(0));
            const auto x = render(s, kC4, 72000, &sweep);
            return std::fabs(peak_hz(spectrum(x, 20000)) - peak_hz(spectrum(x, 60000)));
        };
        assert(motion(0) < 8.f && motion(255) > 15.f);
    }
    // Tilt moves the balance: brighter tilt, higher spectral centroid.
    {
        std::vector<float> rich(96000);
        for (int i = 0; i < 96000; ++i) { float v = 0; for (int h = 1; h <= 20; ++h) v += std::sin(6.2831853f * 220.f * h * i / kRate) / h; rich[size_t(i)] = .3f * v; }
        auto centroid = [&](int tilt) {
            Synth s; const int c = code(128, 0, tilt); s.start(c); s.update(c, jitter(0));
            const auto p = spectrum(render(s, kC4, 36000, &rich), 24000);
            double num = 0, den = 0; for (size_t k = 1; k < p.size(); ++k) { num += k * p[k]; den += p[k]; }
            return num / den;
        };
        assert(centroid(255) > 1.5 * centroid(0));
    }
    // Real time: once running, rendering allocates nothing (hops, FFTs and recaptures included).
    {
        Synth s; const int c = code(100, 200, 128); s.start(c); s.update(c, jitter(80));
        render(s, kC4, 4096, &a440);
        float sink = 0;
        allocations = 0; audit = true;
        for (int i = 0; i < 48000; i += 256) {
            const Synth::SourceLoan loan(s, a440.data(), int(a440.size()), 1, 48000);
            for (int j = 0; j < 256; ++j) sink += s.next(kC4, kRate);
        }
        audit = false;
        assert(std::isfinite(sink));
        assert(allocations == 0);
    }

    // Codex round 2 · short sources: a held note follows Position across a 5000-frame source (904
    // frames of travel, less than a hop) and settles on the same spectrum as a fresh strike there.
    {
        std::vector<float> shortSrc(5000);
        for (int i = 0; i < 5000; ++i) shortSrc[size_t(i)] = (i < 904 ? .8f : i > 4095 ? .4f : 0.f) * std::sin(6.2831853f * (i < 904 ? 440.f : 660.f) * i / kRate);
        Synth held, fresh;
        held.start(code(0, 0, 128)); held.update(code(0, 0, 128), jitter(0));
        const float first = peak_hz(spectrum(render(held, kC4, 24000, &shortSrc), 12000));
        held.update(code(255, 0, 128), jitter(0));
        const float moved = peak_hz(spectrum(render(held, kC4, 36000, &shortSrc), 24000));
        fresh.start(code(255, 0, 128)); fresh.update(code(255, 0, 128), jitter(0));
        const float reference = peak_hz(spectrum(render(fresh, kC4, 36000, &shortSrc), 24000));
        assert(std::fabs(moved - reference) < 8.f && std::fabs(first - reference) > 80.f);
    }
    // Codex round 2 · the loan: after it ends, no PCM is read — even when Position moves and the voice
    // keeps rendering. The buffer is made unreadable (mprotect); a read would stop this process.
    {
        constexpr int frames = 32768;
        const size_t bytes = size_t(frames) * sizeof(float);
        auto* pcm = static_cast<float*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
        assert(pcm != MAP_FAILED);
        for (int i = 0; i < frames; ++i) pcm[i] = .3f * std::sin(6.2831853f * 440.f * i / kRate);
        Synth s; s.start(code(0, 120, 128)); s.update(code(0, 120, 128), jitter(0));
        float sum = 0;
        { const Synth::SourceLoan loan(s, pcm, frames, 1, 48000); for (int i = 0; i < 8192; ++i) sum += s.next(kC4, kRate); }
        assert(mprotect(pcm, bytes, PROT_NONE) == 0);
        s.update(code(255, 255, 128), jitter(0));               // a new position and full smear, no loan
        for (int i = 0; i < 8192; ++i) sum += s.next(kC4, kRate);
        assert(std::isfinite(sum));                              // still the earlier freeze, nothing read
        munmap(pcm, bytes);
    }

    // The engine path: instrument 0 is SP FREEZE reading slot 1, where instrument 1 holds a WAV.
    {
        const auto dir = std::filesystem::absolute(argv[1]);
        std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
        for (int sourceRate : {32000, 44100, 48000, 96000}) {
        const auto sample = dir / ("a440-" + std::to_string(sourceRate) + ".wav");
        wav(sample, tone(440.f * 48000.f / float(sourceRate), sourceRate * 2, .6f), uint32_t(sourceRate));   // 440 Hz at that rate
        auto engine = std::make_unique<AudioEngine>();
        engine->setDeviceSampleRate(48000);
        songcore::SongcoreHost host(engine.get(), 48000);
        auto p = songcore::make_default_project();
        p.instruments[1].sampleFilePath = sample.string();
        p.instruments[0].sampleFilePath = custom_uri(kSpectralFreezeModel, 128, 0, 128);
        p.instruments[0].loopMode = "fwd";
        p.instruments[0].studioSynthParams = default_synth_params();
        assert(host.push_project(songcore::serialize_project(p)));
        assert(host.load_media("").failed == 0);
        const int sourceSlot = host.project().instruments[1].sampleId;
        auto& ins = host.edit_project().instruments[0];
        ins.studioSynthParams[kFreezeSourceParam] = sourceSlot; ins.studioSynthParams[3] = 0;
        host.push_params();
        assert(engine->setLiveStudioSynth(ins.sampleId, custom_code(kSpectralFreezeModel, 128, 0, 128), ins.studioSynthParams));
        host.preview_instrument(0);
        std::vector<float> mono; float block[512];
        for (int i = 0; i < 160; ++i) { engine->processLiveBlock(block, 256, 2, 48000); for (int j = 0; j < 256; ++j) mono.push_back(block[2 * j]); }
        assert(rms(mono, 20000, 12000) > 1e-3);
        const float measured = peak_hz(spectrum(mono, 24000));
        if (std::fabs(measured - 440.f) >= 8.f) std::printf("source %d Hz -> %.1f Hz\n", sourceRate, measured);
        assert(std::fabs(measured - 440.f) < 8.f);   // C4 keeps the source pitch at every source rate
        }

        // Source changes between blocks, through the real paths: the Source slot moves from 1 to 2 by
        // the K4 row (code and parameters together), then slot 2's sample is replaced (new generation).
        {
            const auto a = dir / "src-440.wav", e = dir / "src-660.wav", g = dir / "src-330.wav";
            wav(a, tone(440.f, 96000, .6f)); wav(e, tone(660.f, 96000, .6f)); wav(g, tone(330.f, 96000, .6f));
            auto engine = std::make_unique<AudioEngine>();
            engine->setDeviceSampleRate(48000);
            songcore::SongcoreHost host(engine.get(), 48000);
            auto p = songcore::make_default_project();
            p.instruments[1].sampleFilePath = a.string();
            p.instruments[2].sampleFilePath = e.string();
            p.instruments[0].sampleFilePath = custom_uri(kSpectralFreezeModel, 128, 0, 128);
            p.instruments[0].loopMode = "fwd";
            assert(host.push_project(songcore::serialize_project(p)));
            assert(host.load_media("").failed == 0);
            const int slot0 = host.project().instruments[0].sampleId;
            const int c = custom_code(kSpectralFreezeModel, 128, 0, 128);
            auto params = default_synth_params(); params[3] = 0;
            params[kFreezeSourceParam] = host.project().instruments[1].sampleId;
            host.edit_project().instruments[0].studioSynthParams = params;
            host.push_params();
            assert(engine->setLiveStudioSynth(slot0, c, params));
            host.preview_instrument(0);
            auto play = [&](int blocks) {
                std::vector<float> mono; float block[512];
                for (int i = 0; i < blocks; ++i) { engine->processLiveBlock(block, 256, 2, 48000); for (int j = 0; j < 256; ++j) mono.push_back(block[2 * j]); }
                return peak_hz(spectrum(mono, mono.size() - kFft));
            };
            assert(std::fabs(play(120) - 440.f) < 8.f);
            params[kFreezeSourceParam] = host.project().instruments[2].sampleId;   // the K4 row
            assert(engine->setLiveStudioSynth(slot0, c, params));
            assert(std::fabs(play(120) - 660.f) < 8.f);
            assert(host.load_sample(2, g.string()));                                  // new generation
            assert(engine->setLiveStudioSynth(slot0, c, params));
            host.preview_instrument(0);
            assert(std::fabs(play(120) - 330.f) < 8.f);
        }
        // Codex patch-17 re-check · edited rates: RATE steps, a copy of a stepped slot, Undo to a backup
        // taken at another rate, and a WAV saved at a step and loaded again — the freeze stays at 440 Hz.
        {
            const auto a = dir / "rate-440.wav";
            wav(a, tone(440.f, 96000, .6f));
            auto engine = std::make_unique<AudioEngine>();
            engine->setDeviceSampleRate(48000);
            songcore::SongcoreHost host(engine.get(), 48000);
            auto p = songcore::make_default_project();
            p.instruments[1].sampleFilePath = a.string();
            p.instruments[0].sampleFilePath = custom_uri(kSpectralFreezeModel, 128, 0, 128);
            p.instruments[0].loopMode = "fwd";
            assert(host.push_project(songcore::serialize_project(p)));
            assert(host.load_media("").failed == 0);
            const int slot0 = host.project().instruments[0].sampleId, slot1 = host.project().instruments[1].sampleId;
            const int c = custom_code(kSpectralFreezeModel, 128, 0, 128);
            auto params = default_synth_params(); params[3] = 0;
            auto freezeFrom = [&](int slot) {
                params[kFreezeSourceParam] = slot;
                host.edit_project().instruments[0].studioSynthParams = params; host.push_params();
                assert(engine->setLiveStudioSynth(slot0, c, params));
                host.preview_instrument(0);
                std::vector<float> mono; float block[512];
                for (int i = 0; i < 120; ++i) { engine->processLiveBlock(block, 256, 2, 48000); for (int j = 0; j < 256; ++j) mono.push_back(block[2 * j]); }
                return peak_hz(spectrum(mono, mono.size() - kFft));
            };
            for (int factor : {1, 2, 4, 1}) {
                host.apply_rate_and_bits(slot1, factor, 16);
                assert(std::fabs(freezeFrom(slot1) - 440.f) < 8.f);
            }
            host.apply_rate_and_bits(slot1, 2, 16);
            engine->prepareSourcePreview(7, slot1, 2);                 // a copy of the stepped slot
            assert(std::fabs(freezeFrom(7) - 440.f) < 8.f);
            host.apply_rate_and_bits(slot1, 2, 16);
            const auto saved = dir / "saved-step2.wav";
            assert(host.save_sample_wav(slot1, saved.string(), {}, 0, false, 16));
            assert(host.load_sample(5, saved.string()));
            assert(std::fabs(freezeFrom(host.project().instruments[5].sampleId) - 440.f) < 8.f);
            host.apply_rate_and_bits(slot1, 1, 16);
            host.apply_rate_and_bits(slot1, 4, 16);
            assert(engine->backupSample(slot1));                        // backup at 1/4 rate
            host.apply_rate_and_bits(slot1, 1, 16);                     // HIGH again
            host.undo_sample(slot1);                                    // back to the 1/4-rate buffer (UI path)
            assert(engine->getSampleLength(slot1) == 24000);
            assert(std::fabs(freezeFrom(slot1) - 440.f) < 8.f);
            // The plain sampler and a save follow the restored rate too (pre-existing host routing fault).
            host.stop_preview(true);
            host.preview_instrument(1);
            {
                std::vector<float> mono; float block[512];
                for (int i = 0; i < 60; ++i) { engine->processLiveBlock(block, 256, 2, 48000); for (int j = 0; j < 256; ++j) mono.push_back(block[2 * j]); }
                const float hz = peak_hz(spectrum(mono, mono.size() - kFft));
                if (std::fabs(hz - 440.f) >= 8.f) std::printf("sampler after Undo -> %.1f Hz\n", hz);
                assert(std::fabs(hz - 440.f) < 8.f);
            }
            host.stop_preview(true);
            assert(host.sample_rate_of(slot1) == 12000);
            const auto undone = dir / "saved-after-undo.wav";
            assert(host.save_sample_wav(slot1, undone.string(), {}, 0, false, 16));
            {
                std::ifstream in(undone, std::ios::binary); char head[28] = {};
                in.read(head, 28); uint32_t rate = 0; std::memcpy(&rate, head + 24, 4);
                assert(rate == 12000);
            }
        }
        // Codex check 3 · the RATE basis travels with Undo, an edit off HIGH becomes the original, and
        // a save writes the buffer's real rate (no 8 kHz floor).
        {
            auto fresh = [&](const std::filesystem::path& file) {
                auto engine = std::make_unique<AudioEngine>();
                engine->setDeviceSampleRate(48000);
                auto host = std::make_unique<songcore::SongcoreHost>(engine.get(), 48000);
                auto p = songcore::make_default_project();
                p.instruments[1].sampleFilePath = file.string();
                p.instruments[1].loopMode = "fwd";
                assert(host->push_project(songcore::serialize_project(p)));
                assert(host->load_media("").failed == 0);
                return std::make_pair(std::move(engine), std::move(host));
            };
            auto samplerHz = [](AudioEngine& engine, songcore::SongcoreHost& host) {
                host.stop_preview(true);
                host.preview_instrument(1);
                std::vector<float> mono; float block[512];
                for (int i = 0; i < 60; ++i) { engine.processLiveBlock(block, 256, 2, 48000); for (int j = 0; j < 256; ++j) mono.push_back(block[2 * j]); }
                host.stop_preview(true);
                return peak_hz(spectrum(mono, mono.size() - kFft));
            };
            const auto a = dir / "basis-440.wav";
            wav(a, tone(440.f, 96000, .6f));
            {   // LOFI → backup → HIGH → Undo → NORM: NORM steps from the 48 kHz original again.
                auto [engine, host] = fresh(a); const int slot = host->project().instruments[1].sampleId;
                host->apply_rate_and_bits(slot, 4, 16); assert(host->backup_sample(slot));
                host->apply_rate_and_bits(slot, 1, 16); host->undo_sample(slot);
                assert(engine->getSampleRateFactor(slot) == 4 && host->sample_rate_of(slot) == 12000);
                host->apply_rate_and_bits(slot, 2, 16);
                assert(engine->getSampleLength(slot) == 48000 && host->sample_rate_of(slot) == 24000);
                assert(std::fabs(samplerHz(*engine, *host) - 440.f) < 8.f);
            }
            {   // backup → Pitch +12 → LOFI → Undo → HIGH: the undone shift never comes back.
                auto [engine, host] = fresh(a); const int slot = host->project().instruments[1].sampleId;
                assert(host->backup_sample(slot)); host->pitch_shift_sample(slot, 12.f);
                host->apply_rate_and_bits(slot, 4, 16); host->undo_sample(slot);
                host->apply_rate_and_bits(slot, 1, 16);
                assert(engine->getSampleLength(slot) == 96000 && std::fabs(samplerHz(*engine, *host) - 440.f) < 8.f);
            }
            {   // Crop on NORM, then HIGH: the cut stays (the cropped buffer is the original now).
                auto [engine, host] = fresh(a); const int slot = host->project().instruments[1].sampleId;
                host->apply_rate_and_bits(slot, 2, 16);
                assert(host->backup_sample(slot)); host->crop_sample(slot, 0, 24000);
                host->apply_rate_and_bits(slot, 1, 16);
                assert(engine->getSampleLength(slot) == 24000 && host->sample_rate_of(slot) == 24000);
                assert(std::fabs(samplerHz(*engine, *host) - 440.f) < 8.f);
                host->undo_sample(slot);   // and Undo brings back the NORM buffer on its 48 kHz basis
                assert(engine->getSampleLength(slot) == 48000 && engine->getSampleRateFactor(slot) == 2);
                host->apply_rate_and_bits(slot, 1, 16);
                assert(engine->getSampleLength(slot) == 96000 && std::fabs(samplerHz(*engine, *host) - 440.f) < 8.f);
            }
            {   // A fade on LOFI, then HIGH: the fade stays.
                auto [engine, host] = fresh(a); const int slot = host->project().instruments[1].sampleId;
                host->apply_rate_and_bits(slot, 4, 16);
                host->silence_region(slot, 0, 24000);
                host->apply_rate_and_bits(slot, 1, 16);
                assert(engine->getSampleLength(slot) == 24000 && host->sample_level_peak(slot, 0, 24000) == 0.f);
            }
            {   // 22.05 kHz at LOFI is a 5.5 kHz buffer: the WAV says so and replays at 440 Hz.
                const auto b = dir / "basis-22050.wav";
                wav(b, tone(440.f * 48000.f / 22050.f, 44100, .6f), 22050);
                auto [engine, host] = fresh(b); const int slot = host->project().instruments[1].sampleId;
                host->apply_rate_and_bits(slot, 4, 16);
                const float before = samplerHz(*engine, *host);
                assert(host->edit_sample_level(slot, 0, 1024, .5f)); host->undo_sample(slot);   // level Undo
                assert(samplerHz(*engine, *host) == before);                                   // ratio kept
                const auto out = dir / "lofi-22050.wav";
                assert(host->save_sample_wav(slot, out.string(), {}, 0, false, 16));
                std::ifstream in(out, std::ios::binary); char head[28] = {};
                in.read(head, 28); uint32_t rate = 0; std::memcpy(&rate, head + 24, 4);
                assert(rate >= 5512 && rate <= 5513);
                assert(host->load_sample(1, out.string()));   // reimport
                assert(std::fabs(samplerHz(*engine, *host) - 440.f) < 8.f);
            }
            // Codex check 4 · a backup's RATE original is counted, bounded by the budget when Undo
            // brings it back, and freed with the backup on reload and clear — int16 and float
            // originals (a level edit promotes to float), mono and stereo.
            constexpr int64_t kLong = 480000, kQuarter = kLong / 4;   // 10 s: Undo needs more than 1 MiB
            const auto lm = dir / "basis-long.wav", st = dir / "basis-stereo.wav";
            wav(lm, tone(440.f, int(kLong), .6f)); wav_stereo(st, tone(440.f, int(kLong), .6f));
            for (const bool stereo : {false, true}) for (const bool asFloat : {false, true}) {
                auto [engine, host] = fresh(stereo ? st : lm); const int slot = host->project().instruments[1].sampleId;
                const int64_t channels = stereo ? 2 : 1;
                host->apply_rate_and_bits(slot, 4, 16);   // kQuarter frames on a kLong original
                if (asFloat) { assert(host->edit_sample_level(slot, 0, 64, .9f)); host->free_sample_undo(slot); }
                const int64_t originalBytes = kLong * channels * (asFloat ? 4 : 2);
                const int64_t before = engine->audio_memory_bytes();
                if (before != kQuarter * channels * 4 + originalBytes) std::printf("memory %lld (stereo %d float %d)\n", (long long)before, stereo, asFloat);
                assert(before == kQuarter * channels * 4 + originalBytes);
                assert(host->backup_sample(slot));
                const int64_t withBackup = engine->audio_memory_bytes();
                assert(withBackup == kQuarter * channels * 4 * 2 + originalBytes * 2);   // the backup and its original
                host->apply_rate_and_bits(slot, 1, 16);                                  // HIGH: kLong float frames
                const int64_t high = engine->audio_memory_bytes();
                // Undo needs the 1/4 buffer and its original again: refused under a budget below that.
                const int64_t need = kQuarter * channels * 4 + originalBytes;
                assert(need > 1024 * 1024);
                // The test budget takes only a whole positive number, and only ever lowers the budget.
                const int64_t machine = pt::load_budget_bytes();
                for (const char* bad : {"1garbage", " 1", "+1", "0", "-5", "9223372036854775807"}) {
                    setenv("PT_LOAD_BUDGET_MB", bad, 1);
                    assert(pt::load_budget_bytes() == machine);
                }
                setenv("PT_LOAD_BUDGET_MB", "1", 1);
                assert(pt::load_budget_bytes() == (machine > 0 ? std::min<int64_t>(machine, 1 << 20) : 1 << 20));
                unsetenv("PT_LOAD_BUDGET_MB");
                {
                    setenv("PT_LOAD_BUDGET_MB", "1", 1);   // the budget itself, whatever the machine
                    host->undo_sample(slot);
                    unsetenv("PT_LOAD_BUDGET_MB");
                    assert(engine->getSampleLength(slot) == kLong && engine->getSampleRateFactor(slot) == 1 &&
                           host->sample_rate_of(slot) == 48000 && engine->audio_memory_bytes() == high);
                }
                host->undo_sample(slot);   // with room: the 1/4 buffer and its basis are back
                assert(engine->getSampleLength(slot) == kQuarter && engine->getSampleRateFactor(slot) == 4 && host->sample_rate_of(slot) == 12000);
                // Reloading the slot drops the backup with its original; so does clearing it.
                assert(host->backup_sample(slot));
                assert(host->load_sample(1, (stereo ? st : lm).string()));
                assert(engine->audio_memory_bytes() == kLong * channels * 4);
                host->apply_rate_and_bits(slot, 4, 16); assert(host->backup_sample(slot));
                engine->clearSample(slot);
                assert(engine->audio_memory_bytes() == 0);
            }
            {   // Refused steps keep everything: LOFI on a 3-frame buffer, a delete of the whole buffer.
                auto [engine, host] = fresh(a); const int slot = host->project().instruments[1].sampleId;
                host->crop_sample(slot, 0, 3);
                const uint32_t generation = engine->sampleGeneration(slot);
                host->apply_rate_and_bits(slot, 4, 16);
                assert(engine->getSampleLength(slot) == 3 && engine->getSampleRateFactor(slot) == 1 && host->sample_rate_of(slot) == 48000);
                assert(engine->sampleGeneration(slot) == generation);   // refused before the slot is touched
                host->apply_rate_and_bits(slot, 1, 16);
                assert(host->sample_rate_of(slot) == 48000);
                auto [engine2, host2] = fresh(a); const int slot2 = host2->project().instruments[1].sampleId;
                host2->apply_rate_and_bits(slot2, 4, 16);
                host2->delete_sample_region(slot2, 0, 24000);   // refused by the engine
                assert(engine2->getSampleLength(slot2) == 24000 && engine2->getSampleRateFactor(slot2) == 4);
                host2->apply_rate_and_bits(slot2, 1, 16);
                assert(engine2->getSampleLength(slot2) == 96000 && host2->sample_rate_of(slot2) == 48000);
                // A preview copy replaces the slot's audio, so its old backup goes; the source keeps its own.
                const int preview = 254;
                engine2->prepareSourcePreview(preview, slot2, 0);
                host2->apply_rate_and_bits(preview, 4, 16); assert(engine2->backupSample(preview));
                assert(engine2->backupSample(slot2));
                engine2->prepareSourcePreview(preview, slot2, 0);
                assert(!engine2->undoSample(preview) && engine2->getSampleLength(preview) == 96000);
                host2->apply_rate_and_bits(preview, 4, 16); assert(engine2->backupSample(preview));
                assert(engine2->prepareSourcePreviewRange(preview, slot2, 0, 0, 1024));
                assert(!engine2->undoSample(preview) && engine2->getSampleLength(preview) == 1024);
                host2->apply_rate_and_bits(slot2, 4, 16);
                assert(engine2->undoSample(slot2) && engine2->getSampleLength(slot2) == 96000);
            }
        }
        std::filesystem::remove_all(dir);
    }

    // Codex patch-17 re-check · the first hop of a new note already uses its loan's rate: identical PCM
    // lent at 48 and 96 kHz must differ from the first samples on.
    {
        std::vector<float> pcm(12000);
        for (int i = 0; i < 12000; ++i) pcm[size_t(i)] = .5f * std::sin(6.2831853f * 440.f * i / 96000.f);
        Synth a, b; const int c = code(128, 0, 128);
        for (Synth* v : {&a, &b}) { v->start(c); v->update(c, jitter(0)); }
        int differences = 0; double energy = 0;
        for (int block = 0; block < 8; ++block) {
            const Synth::SourceLoan la(a, pcm.data(), int(pcm.size()), 1, 48000), lb(b, pcm.data(), int(pcm.size()), 1, 96000);
            for (int i = 0; i < 128; ++i) { const float x = a.next(kC4, kRate), y = b.next(kC4, kRate); differences += x != y; energy += double(x) * x; }
        }
        assert(energy > 0 && differences > 900);
    }

    // Codex check 3 · a new note with no loan speaks the built-in vowel, exactly as a fresh voice:
    // the previous note's spectrum is not carried over (and its PCM, protected here, is never read).
    {
        constexpr int frames = 16384; constexpr size_t bytes = frames * sizeof(float);
        auto* pcm = static_cast<float*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
        assert(pcm != MAP_FAILED);
        for (int i = 0; i < frames; ++i) pcm[i] = .3f * std::sin(6.2831853f * 440.f * i / 48000.f);
        Synth used, fresh; const int c = code(0, 0, 128);
        used.start(c);
        { const Synth::SourceLoan loan(used, pcm, frames, 1, 48000); for (int i = 0; i < 4096; ++i) used.next(kC4, kRate); }
        assert(mprotect(pcm, bytes, PROT_NONE) == 0);
        used.start(c); fresh.start(c);
        double error = 0, energy = 0;
        for (int i = 0; i < 8192; ++i) { const float x = used.next(kC4, kRate), y = fresh.next(kC4, kRate); error += double(x - y) * (x - y); energy += double(y) * y; }
        assert(energy > 0 && error == 0);
        munmap(pcm, bytes);
    }

    std::puts("PASS: SP FREEZE registered, C4 = source pitch and octave tracking, steady frozen partial, position picks the moment, new sample re-frozen, built-in vowel without source, smear breathes, tilt brightens, allocation-free, short-source Position follows live, no PCM read after the loan, real engine path at 32/44.1/48/96 kHz source rates, source slot switched by the K4 row and its sample replaced between blocks, RATE steps/copy/undo/save keep the pitch, first hop uses the loan's rate, RATE basis travels with Undo, edits off HIGH stay, LOFI of 22.05 kHz saves at 5.5 kHz, a new note without a loan starts fresh, Undo bounded by the budget incl. its RATE original, backups counted and freed on reload/clear, refused steps keep the state and generation, preview copies drop their old backup, the test budget takes only a whole positive number");
    return 0;
}
