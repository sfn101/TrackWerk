// ptload-save-prompts-check: AUDIT.md findings 1 and 2, driven through the real buttons on PROJECT.
// LOAD with unsaved work asks first and B keeps the song; SAVE onto Projects/<name>.ptp that holds a
// different song asks first and B leaves that file alone; re-saving the open song does not ask.
// Builds with GCC and MSVC; no assert, so it also checks in a Release build.
// usage: ptload-save-prompts-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/render.h"   // project_file_name
#include "ui/button_mapper.h"
#include "ui/input_dispatcher.h"
#include "ui/settings_row_layout.h"
#include "ui/std_filesystem.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace pt::ui;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    StdFileSystem fs;
    InputDispatcher dispatch;
    MapperState mapper;
    uint64_t now = 1000;
    explicit Rig(const std::string& dir) : fs(dir), dispatch(state, host, fs) {
        host.new_project();
        state.project = &host.edit_project();
    }
    void send(Button b, ButtonAction a) {
        now += 400;   // a human pause: two A presses within 300 ms are the A,A double-tap
        dispatch.set_now(static_cast<long long>(now));
        handle_button(ButtonEvent{b, a, ButtonMods{}}, dispatch, mapper, now);
    }
    void tap(Button b) { send(b, ButtonAction::PRESSED); send(b, ButtonAction::RELEASED); }
    // A on PROJECT's SAVE (1) or LOAD (2) cell.
    void project_cell(int column) {
        state.currentScreen = ScreenType::PROJECT;
        state.projectCursorRow = static_cast<int>(ProjectRow::PROJECT);
        state.projectCursorColumn = column;
        tap(Button::A);
    }
};

std::string read(StdFileSystem& fs, const std::string& path) {
    std::string out;
    fs.read_file(path, out);
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <new run directory>\n", argv[0]);
        return 2;
    }
    const std::filesystem::path dir = std::filesystem::path(argv[1]) / "data";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Rig r(dir.string());
    std::filesystem::create_directories(r.fs.projects_directory());

    // ── 1 · LOAD with unsaved work ──
    r.host.edit_project().name = "MINE";
    r.state.projectVersion = 1;   // an edit nobody saved
    r.project_cell(2);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::LOAD_DISCARD, "dirty LOAD asks first");
    check(r.state.currentScreen != ScreenType::FILE_BROWSER, "...and the browser waits for the answer");
    r.tap(Button::B);
    check(!r.state.confirm.is_open() && r.state.project_dirty() && r.host.project().name == "MINE",
          "B keeps the song and its unsaved work");
    check(r.state.currentScreen == ScreenType::PROJECT, "...and stays on PROJECT");
    r.project_cell(2);
    r.tap(Button::A);
    check(r.state.currentScreen == ScreenType::FILE_BROWSER, "A opens the browser");
    r.tap(Button::B);   // leave it

    r.state.savedProjectVersion = r.state.projectVersion;   // clean now
    r.project_cell(2);
    check(r.state.currentScreen == ScreenType::FILE_BROWSER && !r.state.confirm.is_open(),
          "clean LOAD opens the browser at once");
    r.tap(Button::B);

    // ── 2 · SAVE onto another song's file ──
    const std::string other = r.fs.projects_directory() + "/" + songcore::project_file_name("OTHER") + ".ptp";
    r.fs.write_file(other, "{\"name\":\"OTHER\"}");
    r.host.edit_project().name = "OTHER";
    r.state.projectPath.clear();   // the open song never came from that file
    r.project_cell(1);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_OVERWRITE, "SAVE onto another song asks first");
    r.tap(Button::B);
    check(read(r.fs, other) == "{\"name\":\"OTHER\"}", "B leaves the other file alone");
    r.project_cell(1);
    r.tap(Button::A);
    check(read(r.fs, other) != "{\"name\":\"OTHER\"}" && r.state.projectPath == other, "A overwrites it");

    // ── 3 · re-saving the open song does not ask ──
    r.state.projectVersion++;
    r.project_cell(1);
    check(!r.state.confirm.is_open() && !r.state.project_dirty(), "SAVE of the open song saves at once");

    // ── 4 · a new name saves at once ──
    r.host.edit_project().name = "FRESH";
    r.project_cell(1);
    check(!r.state.confirm.is_open() && r.fs.file_exists(r.fs.projects_directory() + "/" +
                                                         songcore::project_file_name("FRESH") + ".ptp"),
          "SAVE under a free name saves at once");

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
