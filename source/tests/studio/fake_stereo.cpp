// Fake stereo loads as mono (specs/fake-stereo-mono.md). Portable, no assert: CI runs it on Linux and
// Windows. Usage: ptfake-stereo-check <scratch folder>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/wav_writer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

constexpr int kFrames = 4000;

std::vector<float> tone() {
    std::vector<float> v(kFrames);
    for (int i = 0; i < kFrames; ++i) v[i] = 0.6f * std::sin(0.045f * i) + 0.2f * std::sin(0.31f * i);
    return v;
}

int wav_channels(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    unsigned char head[24] = {};
    f.read(reinterpret_cast<char*>(head), sizeof(head));
    return f ? head[22] | (head[23] << 8) : -1;
}

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    std::unique_ptr<songcore::SongcoreHost> host;
    Rig() { host = std::make_unique<songcore::SongcoreHost>(engine.get(), 48000); }
};

// Pan, filter, drive, crush and both sends: every per-channel path of the chain.
std::vector<float> render(Rig& r, int slot) {
    auto& inst = r.host->edit_project().instruments[slot];
    inst.pan = 0x30; inst.filterType = "lp"; inst.filterCut = 0x90; inst.filterRes = 0x40;
    inst.drive = 0x50; inst.crush = 0x3; inst.reverbSend = 0x60; inst.delaySend = 0x50;
    r.host->push_instrument(slot);
    r.host->preview_instrument(slot);
    std::vector<float> out;
    float block[1024];
    for (int b = 0; b < 16; ++b) {
        r.engine->processLiveBlock(block, 512, 2, 48000);
        out.insert(out.end(), block, block + 1024);
    }
    r.host->stop_preview(true);
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "ptfake-stereo-check";
    std::filesystem::create_directories(dir);
    const auto same = (dir / "same-sides.wav").string();
    const auto oneOff = (dir / "one-off.wav").string();
    const auto saved = (dir / "saved.wav").string();

    const auto left = tone();
    auto right = left;
    check(songcore::write_wav(same, left, right, 48000, {}, 2), "wrote the identical-sides WAV");
    right[kFrames / 2] += 1.0f / 32768.0f * 4;   // a few 16-bit steps on one frame
    check(songcore::write_wav(oneOff, left, right, 48000, {}, 2), "wrote the one-value-off WAV");

    // Load
    Rig a;
    check(a.host->load_sample(1, same), "identical sides load");
    check(!a.engine->hasStereoData(1) && a.engine->isFoldedStereo(1), "identical sides become MONO (L=R)");
    check(a.engine->getSampleLength(1) == kFrames, "the length is kept");

    Rig b;
    check(b.host->load_sample(1, oneOff), "one-off file loads");
    check(b.engine->hasStereoData(1) && !b.engine->isFoldedStereo(1), "sides one value apart stay stereo");

    // Render: the same audio held as stereo (edits never fold) and as mono must sound identical.
    check(b.engine->cropSample(1, 0, kFrames / 2) && a.engine->cropSample(1, 0, kFrames / 2), "both cropped");
    check(b.engine->hasStereoData(1) && !a.engine->hasStereoData(1), "one stereo, one mono");
    check(!a.engine->isFoldedStereo(1), "an edit clears the L=R mark");
    const auto mono = render(a, 1);
    const auto stereo = render(b, 1);
    double energy = 0;
    for (float v : mono) energy += double(v) * v;
    check(energy > 1e-6, "the render is not silent");
    check(mono.size() == stereo.size() &&
              std::memcmp(mono.data(), stereo.data(), mono.size() * sizeof(float)) == 0,
          "mono and identical stereo render bit for bit the same");

    // Save: what is in memory, a 1-channel WAV.
    Rig c;
    check(c.host->load_sample(2, same), "reload for save");
    check(c.host->save_sample_wav(2, saved, {}, 0, c.engine->hasStereoData(2)), "SAVE writes");
    check(wav_channels(saved) == 1, "SAVE writes a 1-channel WAV");
    check(wav_channels(same) == 2, "the original file is untouched");

    std::printf(failures ? "FAILED: %d\n" : "PASS: fake stereo loads as mono\n", failures);
    return failures ? 1 : 0;
}
