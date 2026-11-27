// PROJECT > LIMITER (specs/master-limiter.md). Portable, no assert: CI runs it on Linux and Windows.
// Usage: ptmaster-limiter-check [scratch folder]
#include "effects/modules/limiter-module.h"
#include "songcore/project_io.h"
#include "songcore/project_ops.h"
#include "songcore/render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

constexpr int kRate = 48000;

// Interleaved stereo: left `l(i)`, right `r(i)`.
template <typename L, typename R>
std::vector<float> stereo(int frames, L l, R r) {
    std::vector<float> v(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) { v[i * 2] = l(i); v[i * 2 + 1] = r(i); }
    return v;
}

void clean_limiter() {
    LimiterModule lim;
    lim.prepare(kRate);
    lim.setMode(1);
    const int la = lim.latencyFrames();
    check(la == 72, "CLEAN's look-ahead is 1.5 ms (72 frames at 48 kHz)");

    // Hot: kicks and bass up to 4× full scale, in blocks of odd sizes.
    auto hot = stereo(kRate * 2,
        [](int i) { return 4.0f * std::sin(0.004f * i) * (i % 9000 < 600 ? 1.0f : 0.3f); },
        [](int i) { return 2.5f * std::sin(0.011f * i + 1.0f); });
    for (size_t at = 0; at < hot.size() / 2;) {
        const int n = std::min<int>(37 + static_cast<int>(at % 200), static_cast<int>(hot.size() / 2 - at));
        lim.process(&hot[at * 2], n, 2);
        at += static_cast<size_t>(n);
    }
    float peak = 0.0f;
    for (float s : hot) peak = std::max(peak, std::fabs(s));
    check(peak <= CleanLimiter::CEILING, "a hot signal never passes -0.3 dBFS");
    check(peak > 0.95f, "and is limited, not muted");

    // Quiet: unchanged, only delayed.
    lim.prepare(kRate);
    auto quiet = stereo(4000, [](int i) { return 0.5f * std::sin(0.03f * i); },
                        [](int i) { return -0.4f * std::sin(0.07f * i); });
    const auto in = quiet;
    lim.process(quiet.data(), 4000, 2);
    bool same = true;
    for (int i = 0; i < 4000; ++i)
        for (int c = 0; c < 2; ++c)
            same &= quiet[i * 2 + c] == (i < la ? 0.0f : in[(i - la) * 2 + c]);
    check(same, "below the ceiling the output is the input, bit for bit, 72 frames later");

    // Linked: right is half the left, so it must stay half after limiting.
    lim.prepare(kRate);
    auto linked = stereo(kRate, [](int i) { return 3.0f * std::sin(0.01f * i); },
                         [](int i) { return 1.5f * std::sin(0.01f * i); });
    lim.process(linked.data(), kRate, 2);
    float worst = 0.0f;
    for (int i = 0; i < kRate; ++i) worst = std::max(worst, std::fabs(linked[i * 2 + 1] - 0.5f * linked[i * 2]));
    check(worst < 1e-6f, "both channels get the same gain");
}

void classic_unchanged() {
    LimiterModule lim;
    lim.prepare(kRate);
    daisysp::Limiter l, r;
    l.Init();
    r.Init();
    auto a = stereo(8000, [](int i) { return 2.0f * std::sin(0.02f * i); },
                    [](int i) { return 0.7f * std::sin(0.05f * i); });
    auto b = a;
    lim.setPreGain(1.5f);
    lim.process(a.data(), 8000, 2);
    for (int i = 0; i < 8000; ++i) {
        l.ProcessBlock(&b[i * 2], 1, 1.5f);
        r.ProcessBlock(&b[i * 2 + 1], 1, 1.5f);
    }
    check(a == b, "CLASSIC is the DaisySP limiter, bit for bit");
    check(lim.latencyFrames() == 0, "CLASSIC adds no delay");
}

void song_files() {
    using namespace songcore;
    auto old = parse_project(json::parse("{\"version\":1,\"tempo\":120}"));
    normalize_and_migrate(old);
    check(old.limiterMode == 0, "a song without the field opens as CLASSIC");
    const json oldSaved = json::parse(serialize_project(old));
    check(!oldSaved.contains("limiter") && oldSaved["version"] == 1 && oldSaved["minReader"] == 1,
          "a CLASSIC song saves as format 1, with no field");

    Project fresh;
    new_project(fresh);
    check(fresh.limiterMode == 1, "a new song starts on CLEAN");
    const std::string text = serialize_project(fresh);
    const json saved = json::parse(text);
    check(saved.value("limiter", 0) == 1 && saved["version"] == 2 && saved["minReader"] == 2,
          "a CLEAN song saves its field as format 2");
    check(check_format(saved, FileFormat{1, 1}) == FormatCheck::TOO_NEW, "an older app refuses a CLEAN song");
    check(check_format(oldSaved, FileFormat{1, 1}) == FormatCheck::OK, "an older app opens a CLASSIC song");
    auto back = parse_project(saved);
    normalize_and_migrate(back);
    check(back.limiterMode == 1 && serialize_project(back) == text, "CLEAN survives SAVE and LOAD");
    auto junk = parse_project(json::parse("{\"version\":2,\"limiter\":77}"));
    check(junk.limiterMode == 0, "an unknown limiter value opens as CLASSIC");
}

// An engine whose output is an impulse at song frame 0, delayed by the limiter it reports.
struct DelayedClick {
    int latency = 0;
    int64_t frame = 0;
    int getSampleRate() const { return kRate; }
    int getLimiterLatencyFrames() const { return latency; }
    // The master bus setters render_to_wav may call; this engine has none.
    void setStudioControl(int, int) {}
    void setMasterFx(int) {}
    void setOttDepthForRender(int) {}
    void setDustDepthForRender(int) {}
    void setLimiterPreGain(int) {}
    void setLimiterMode(int) {}
    void setMasterEqSlot(int) {}
    void setStemsMode(int) {}
    void scheduleNoteOffAll(int64_t) {}
    void renderOffline(int n, float* out, int) {
        for (int i = 0; i < n; ++i, ++frame) out[i * 2] = out[i * 2 + 1] = frame == latency ? 0.5f : 0.0f;
    }
};

int click_at(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    for (size_t i = 44; i + 1 < b.size(); i += 4)
        if (static_cast<int16_t>(static_cast<uint8_t>(b[i]) | (static_cast<uint8_t>(b[i + 1]) << 8)) != 0)
            return static_cast<int>((i - 44) / 4);
    return -1;
}

void renders_aligned(const std::string& dir) {
    songcore::Project p;
    songcore::RenderOptions opts;
    opts.applyMasterBus = false;
    int at[2] = {};
    for (int lat : {0, 72}) {
        DelayedClick e;
        e.latency = lat;
        const std::string path = dir + "/click-" + std::to_string(lat) + ".wav";
        songcore::render_to_wav(e, p, 1000, path, opts);
        at[lat ? 1 : 0] = click_at(path);
    }
    check(at[0] == 0 && at[1] == 0, "an export starts on time with or without CLEAN's delay");
}
}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : (std::filesystem::temp_directory_path() / "ptmaster-limiter-check").string();
    std::filesystem::create_directories(dir);
    clean_limiter();
    classic_unchanged();
    song_files();
    renders_aligned(dir);
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
