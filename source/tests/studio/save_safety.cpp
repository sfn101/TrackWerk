// ptsave-safety-check: AUDIT.md findings 8, 12 and 19 (specs/audit-fixes-batch2.md).
// 8: a song file that can't be replaced is left alone and the song is saved as <name>-backup.ptp.
// 12: COLLECT keeps two different samples that share a name apart (kick.wav, kick_2.wav).
// 19: no autosave while a load is running.
// Builds with GCC and MSVC; no assert, so it also checks in a Release build.
// usage: ptsave-safety-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "ui/input_dispatcher.h"
#include "ui/project_actions.h"
#include "ui/std_filesystem.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace pt::ui;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
void put(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << bytes;
}
std::string get(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <new run directory>\n", argv[0]);
        return 2;
    }
    const fs::path dir = fs::path(argv[1]) / "data";
    fs::remove_all(dir);
    fs::create_directories(dir);

    auto engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    StdFileSystem files(dir.string());
    InputDispatcher dispatch(state, host, files);
    host.new_project();
    state.project = &host.edit_project();
    fs::create_directories(files.projects_directory());

    // ── 8 · the target can't be replaced: a non-empty folder with the song's name, on every OS ──
    host.edit_project().name = "LOCKED";
    const fs::path target = fs::path(files.projects_directory()) / "LOCKED.ptp";
    put(target / "keep.txt", "untouched");
    state.projectVersion = state.savedProjectVersion + 1;
    const ActionResult r = save_project(host, files, state);
    check(r.ok && r.message == "FILE LOCKED: SAVED AS LOCKED-backup.ptp", "8: SAVE says it saved the backup");
    check(fs::is_regular_file(fs::path(files.projects_directory()) / "LOCKED-backup.ptp"), "8: the backup is beside the song");
    check(get(target / "keep.txt") == "untouched", "8: the locked target is untouched");
    check(!fs::exists(fs::path(files.projects_directory()) / "LOCKED.ptp.tmp"), "8: no temp left behind");
    check(!state.project_dirty(), "8: the song counts as saved");

    // ── 12 · two different kick.wav files ──
    const fs::path a = dir / "outside" / "a" / "kick.wav";
    const fs::path b = dir / "outside" / "b" / "kick.wav";
    put(a, "AAAA");
    put(b, "BBBBBB");
    host.edit_project().instruments[0].sampleFilePath = a.generic_string();
    host.edit_project().instruments[1].sampleFilePath = b.generic_string();
    host.edit_project().instruments[2].sampleFilePath = a.generic_string();   // same file again: one copy
    const ActionResult c = collect_project(host, files, "TWOKICKS", nullptr);
    const fs::path songDir = fs::path(files.songs_directory()) / "TWOKICKS";
    check(c.ok, "12: COLLECT succeeds");
    check(get(songDir / "Samples" / "kick.wav") == "AAAA", "12: the first kick keeps its name");
    check(get(songDir / "Samples" / "kick_2.wav") == "BBBBBB", "12: the second kick is kick_2.wav");
    check(!fs::exists(songDir / "Samples" / "kick_3.wav"), "12: the same file twice is copied once");

    // ── 19 · a due autosave mid-load writes nothing ──
    fs::remove(files.autosave_file_path());
    state.projectVersion = state.savedProjectVersion + 1;
    state.loading.running = true;
    dispatch.flush_autosave();
    check(!fs::exists(files.autosave_file_path()), "19: no autosave while loading");
    state.loading.running = false;
    dispatch.flush_autosave();
    check(fs::exists(files.autosave_file_path()), "19: autosave works again after the load");

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
