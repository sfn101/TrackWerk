// ptcorrupt-files-check: every file in tests/corrupt-files goes through the real loaders and must be
// refused cleanly: no crash, no hang, and the open song left exactly as it was (AUDIT.md findings
// 3-7, 16, 17). Builds with GCC and MSVC; no assert, so it also checks in a Release build.
// usage: ptcorrupt-files-check <path to tests/corrupt-files>
#include "audio-engine.h"
#include "soundfont-trim.h"
#include "songcore/host.h"
#include "songcore/project_io.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    std::fflush(stdout);   // a hang shows which file it was on
    if (!ok) ++failures;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <tests/corrupt-files>\n", argv[0]);
        return 2;
    }
    auto engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host(engine.get(), 48000);
    host.new_project();
    host.edit_project().name = "KEEP";
    const std::string before = songcore::serialize_project(host.project());

    int seen = 0;
    for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
        const std::string path = entry.path().string();
        const std::string name = entry.path().filename().string();
        const std::string ext  = entry.path().extension().string();
        if (ext == ".ptp") {
            ++seen;
            std::printf("--  %s\n", name.c_str());
            const bool loaded = host.load_project_file(path, "");
            check(!loaded && host.last_project_damaged(), name + ": refused as damaged");
            check(songcore::serialize_project(host.project()) == before, name + ": open song untouched");
        } else if (ext == ".wav") {
            ++seen;
            std::printf("--  %s\n", name.c_str());
            check(engine->loadSampleFromWavFile(0, path.c_str()) == 0, name + ": refused");
        } else if (ext == ".sf2") {
            ++seen;
            std::printf("--  %s\n", name.c_str());
            std::vector<pt::SfPreset> presets;
            check(!pt::sf_read_preset_list(path.c_str(), presets), name + ": no preset list");
            check(engine->loadSoundfont(0, path.c_str(), 0, 0) < 0, name + ": refused");
        }
    }
    check(seen >= 9, "all " + std::to_string(seen) + " test files were found");

    // The control: a sound song still loads, so the checks above are not refusing everything.
    check(host.push_project(before) && !host.last_project_damaged(), "a sound song still loads");

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
