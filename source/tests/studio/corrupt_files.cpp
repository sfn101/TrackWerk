// ptcorrupt-files-check: every file in tests/corrupt-files goes through the real loaders and must be
// refused cleanly: no crash, no hang, and the open song left exactly as it was (AUDIT.md findings
// 3-7, 16, 17). Each damaged song must also load when a repair is asked for, with one fix per bad
// value (specs/repair-damaged-song.md). Builds with GCC and MSVC; no assert, so it also checks in a
// Release build.
// usage: ptcorrupt-files-check <path to tests/corrupt-files>
#include "audio-engine.h"
#include "soundfont-trim.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/groove_io.h"
#include "ui/scale_io.h"
#include "ui/studio_preset_io.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
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
std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
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
        const bool newer = name.rfind("22-newer-", 0) == 0;   // from a newer TrackWerk: refused, not damaged
        if (newer) {
            ++seen;
            std::printf("--  %s\n", name.c_str());
            const std::string text = read_text(path);
            const songcore::FileFormat format = ext == ".ptp"   ? songcore::SONG_FORMAT
                                                : ext == ".pti" ? songcore::INSTRUMENT_FORMAT
                                                : ext == ".pts" ? songcore::SCALE_FORMAT
                                                : ext == ".ptg" ? songcore::GROOVE_FORMAT
                                                                : songcore::STUDIO_PRESET_FORMAT;
            const std::string message = songcore::newer_file_message(text, format);
            check(message == (ext == ".ptg" ? "MADE WITH A NEWER TRACKWERK"
                                            : "MADE WITH TRACKWERK 9.9.9 - UPDATE TO OPEN"),
                  name + ": says " + message);
            bool refused = false;
            if (ext == ".ptp") {
                refused = !host.load_project_file(path, "") && !host.last_project_repairable();
            } else if (ext == ".pti") {
                refused = !host.load_instrument_preset(0, path);
            } else if (ext == ".pts") {
                songcore::Scale scale = host.project().scales[0];
                refused = !pt::ui::parse_scale_text(text, scale);
            } else if (ext == ".ptg") {
                songcore::Groove groove = host.project().grooves[0];
                refused = !pt::ui::parse_groove_text(text, groove);
            } else {
                std::array<int, studio::COUNT> values{};
                refused = !pt::ui::parse_studio_preset(true, text, values);
            }
            check(refused, name + ": refused");
            check(songcore::serialize_project(host.project()) == before, name + ": open song untouched");
            check(read_text(path) == text, name + ": file untouched");
        } else if (ext == ".ptp") {
            ++seen;
            std::printf("--  %s\n", name.c_str());
            const bool loaded = host.load_project_file(path, "");
            check(!loaded && host.last_project_damaged(), name + ": refused as damaged");
            check(host.last_project_repairable(), name + ": offered a repair");
            check(songcore::serialize_project(host.project()) == before, name + ": open song untouched");

            // The repair: it loads, counts one fix per bad value, and the result loads clean.
            const int expected = name == "18-several-damages.ptp" ? 4 : 1;
            check(host.load_project_file(path, "", /*repair=*/true) && !host.last_project_damaged(),
                  name + ": loads repaired");
            check(host.last_repair_count() == expected,
                  name + ": " + std::to_string(host.last_repair_count()) + " fixes (want " +
                      std::to_string(expected) + ")");
            const std::string repaired = songcore::serialize_project(host.project());
            check(host.push_project(repaired) && !host.last_project_damaged() && host.last_repair_count() == 0,
                  name + ": the repaired song saves and loads clean");
            host.push_project(before);   // back to the control song for the next file
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
    check(seen >= 16, "all " + std::to_string(seen) + " test files were found");
    // Not a song at all: nothing to repair.
    check(!host.push_project("not json") && host.last_project_damaged() && !host.last_project_repairable(),
          "unreadable JSON is refused with no repair offered");

    // The control: a sound song still loads, so the checks above are not refusing everything.
    check(host.push_project(before) && !host.last_project_damaged(), "a sound song still loads");

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
