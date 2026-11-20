// ptload-save-prompts-check: AUDIT.md findings 1 and 2, driven through the real buttons on PROJECT.
// LOAD with unsaved work asks first and B keeps the song; SAVE onto Projects/<name>.ptp that holds a
// different song asks first and B leaves that file alone; re-saving the open song does not ask.
// A damaged song (specs/repair-damaged-song.md): unsaved changes are asked about first, then
// FILE DAMAGED - REPAIR?; B keeps the open song, A loads it repaired and unsaved, and its first SAVE
// asks before overwriting. A damaged recovery file asks for the repair under RESUME = ASK and AUTO.
// Builds with GCC and MSVC; no assert, so it also checks in a Release build.
// usage: ptload-save-prompts-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/render.h"   // project_file_name
#include "ui/button_mapper.h"
#include "ui/input_dispatcher.h"
#include "ui/settings_row_layout.h"
#include "ui/settings_store.h"
#include "ui/modules/confirm_dialog.h"
#include "ui/std_filesystem.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <vector>
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
    // A on PROJECT's SAVE (1), COPY (2) or LOAD (3) cell.
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
    r.project_cell(3);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::LOAD_PROJECT, "dirty LOAD asks first");
    check(r.state.currentScreen != ScreenType::FILE_BROWSER, "...and the browser waits for the answer");
    r.tap(Button::B);
    check(!r.state.confirm.is_open() && r.state.project_dirty() && r.host.project().name == "MINE",
          "B keeps the song and its unsaved work");
    check(r.state.currentScreen == ScreenType::PROJECT, "...and stays on PROJECT");
    r.project_cell(3);
    r.tap(Button::A);
    check(r.state.currentScreen == ScreenType::FILE_BROWSER, "A opens the browser");
    r.tap(Button::B);   // leave it

    r.state.savedProjectVersion = r.state.projectVersion;   // clean now
    r.project_cell(3);
    check(r.state.currentScreen == ScreenType::FILE_BROWSER && !r.state.confirm.is_open(),
          "clean LOAD opens the browser at once");
    r.tap(Button::B);

    // ── 2 · SAVE onto another song's file ──
    const std::string other = r.fs.projects_directory() + "/" + songcore::project_file_name("OTHER") + ".ptp";
    r.fs.write_file(other, "{\"name\":\"OTHER\"}");
    r.host.edit_project().name = "OTHER";
    r.state.projectPath.clear();   // the open song never came from that file
    r.project_cell(1);
    // Upstream's three-way question, the cursor on CANCEL: a double press decides nothing.
    check(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_EXISTS &&
          r.state.confirm.arg == ConfirmDialogState::COLLECT_CANCEL, "SAVE onto another song asks first, on CANCEL");
    r.tap(Button::A);   // CANCEL
    check(!r.state.confirm.is_open() && read(r.fs, other) == "{\"name\":\"OTHER\"}" &&
          r.host.project().name == "OTHER", "CANCEL leaves the other file and the song alone");
    r.project_cell(1);
    r.tap(Button::DPAD_LEFT);
    r.tap(Button::DPAD_LEFT);   // → REPLACE
    r.tap(Button::A);
    check(read(r.fs, other) != "{\"name\":\"OTHER\"}" && r.state.projectPath == other, "REPLACE overwrites it");

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

    // ── 5 · LOAD a damaged song: unsaved changes first, then the repair ──
    const std::string damaged = r.fs.projects_directory() + "/DAMAGED.ptp";
    const std::string damagedText = "{\"version\":1,\"name\":\"DAMAGED\",\"tempo\":5000}";
    r.fs.write_file(damaged, damagedText);
    auto pick = [&](const std::string& path) {   // put the browser cursor on `path`, press A
        auto& items = r.state.fileBrowser.items;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].path == path || items[i].displayName == "DAMAGED") r.state.fileBrowser.cursor = static_cast<int>(i);
        r.tap(Button::A);
    };
    r.host.edit_project().name = "MINE2";
    r.state.projectVersion++;   // unsaved work again
    r.project_cell(3);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::LOAD_PROJECT, "damaged LOAD: unsaved changes asked first");
    r.tap(Button::A);
    pick(damaged);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::REPAIR_SONG, "a damaged song opens the repair box");
    r.tap(Button::B);
    check(!r.state.confirm.is_open() && r.host.project().name == "MINE2" && r.state.project_dirty(),
          "B keeps the open song and its unsaved changes");
    check(r.state.fileBrowser.statusMessage == "LOAD FAILED: FILE DAMAGED", "...and says LOAD FAILED: FILE DAMAGED");
    pick(damaged);
    r.tap(Button::A);
    check(r.host.project().name == "DAMAGED" && r.host.project().tempo == 999, "A loads it repaired");
    check(r.state.project_dirty() && r.state.projectPath == damaged, "...unsaved, keeping its path");
    check(r.state.statusMessage == "REPAIRED: 1 FIX", "...and says REPAIRED: 1 FIX");
    r.project_cell(1);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_EXISTS, "its first SAVE asks before overwriting");
    r.tap(Button::A);   // CANCEL
    check(read(r.fs, damaged) == damagedText, "CANCEL leaves the damaged file on disk");
    r.project_cell(1);
    r.tap(Button::DPAD_LEFT);
    r.tap(Button::DPAD_LEFT);   // → REPLACE
    r.tap(Button::A);
    check(read(r.fs, damaged) != damagedText && !r.state.project_dirty(), "REPLACE saves the repaired song");
    r.state.projectVersion++;
    r.project_cell(1);
    check(!r.state.confirm.is_open(), "later saves don't ask");

    // ── 6 · RESUME a damaged recovery file ──
    const std::string autosave = r.fs.autosave_file_path();
    r.state.settings.autosaveResumeAuto = false;   // ASK
    r.fs.write_file(autosave, damagedText);
    check(r.dispatch.boot_recovery() == InputDispatcher::BootRecovery::ASKED &&
          r.state.confirm.kind == ConfirmDialogState::Kind::RECOVER, "ASK: RECOVER WORK? comes first");
    r.tap(Button::A);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::REPAIR_SONG, "ASK: then the repair box");
    r.tap(Button::B);
    check(!r.fs.file_exists(autosave) && !r.state.project_dirty() && r.host.project().name != "DAMAGED",
          "B starts an empty song and deletes the recovery file");
    r.state.settings.autosaveResumeAuto = true;    // AUTO
    r.fs.write_file(autosave, damagedText);
    check(r.dispatch.boot_recovery() == InputDispatcher::BootRecovery::ASKED &&
          r.state.confirm.kind == ConfirmDialogState::Kind::REPAIR_SONG, "AUTO: straight to the repair box");
    r.tap(Button::A);
    check(r.host.project().name == "DAMAGED" && r.state.project_dirty() && r.state.projectPath.empty() &&
          r.state.statusMessage == "REPAIRED: 1 FIX", "A brings the work back repaired and unsaved");

    // ── 7 · A song opened from another folder saves back there, not into Projects ──
    const std::string elsewhere = (dir / "Elsewhere").generic_string();
    std::filesystem::create_directories(elsewhere);
    const std::string away = elsewhere + "/AWAY.ptp";
    r.fs.write_file(away, "{\"version\":1,\"name\":\"AWAY\"}");
    r.host.edit_project().name = "AWAY";
    r.state.projectPath = away;   // as if LOAD had opened it there
    r.state.repairedUnsaved = false;
    r.state.projectVersion++;
    r.project_cell(1);
    check(!r.state.confirm.is_open() && read(r.fs, away) != "{\"version\":1,\"name\":\"AWAY\"}" &&
          !r.fs.file_exists(r.fs.projects_directory() + "/AWAY.ptp"),
          "a song opened elsewhere saves back into its own folder");
    check(r.state.projectPath == away, "...and keeps that path");

    // ── 7b · A song from a newer TrackWerk (specs/refuse-newer-files.md) ──
    const std::string newer = r.fs.projects_directory() + "/NEWER.ptp";
    const std::string newerText = "{\"version\":2,\"minReader\":99,\"app\":\"9.9.9\",\"name\":\"NEWER\"}";
    const char* newerMessage = "MADE WITH TRACKWERK 9.9.9 - UPDATE TO OPEN";
    r.fs.write_file(newer, newerText);
    r.project_cell(3);
    for (size_t i = 0; i < r.state.fileBrowser.items.size(); ++i)
        if (r.state.fileBrowser.items[i].path == newer) r.state.fileBrowser.cursor = static_cast<int>(i);
    r.tap(Button::A);
    check(!r.state.confirm.is_open() && r.host.project().name == "AWAY", "LOAD refuses a newer song, no repair offered");
    check(r.state.fileBrowser.statusMessage == newerMessage, "...says MADE WITH TRACKWERK 9.9.9 - UPDATE TO OPEN");
    check(read(r.fs, newer) == newerText, "...and leaves the file as it was");
    r.tap(Button::B);
    r.state.settings.autosaveResumeAuto = true;    // AUTO
    r.fs.write_file(autosave, newerText);
    check(r.dispatch.boot_recovery() == InputDispatcher::BootRecovery::KEPT && r.host.project().name == "AWAY" &&
          r.state.statusMessage == newerMessage && read(r.fs, autosave) == newerText,
          "AUTO: a newer autosave is not loaded and the file is kept");
    r.state.settings.autosaveResumeAuto = false;   // ASK
    check(r.dispatch.boot_recovery() == InputDispatcher::BootRecovery::ASKED, "ASK: RECOVER WORK? as for any autosave");
    r.tap(Button::A);
    check(r.host.project().name == "AWAY" && read(r.fs, autosave) == newerText,
          "ASK: A refuses it and keeps the file");
    r.fs.write_file(newer, "{\"version\":99,\"minReader\":1,\"name\":\"OLDREADER\"}");
    check(r.host.load_project_file(newer, "") && r.host.project().name == "OLDREADER",
          "a newer song this format can read (minReader 1) opens");

    // ── 9 · Save under a new name (specs/save-under-new-name.md) ──
    // The keyboard is filled in as typed, then START applies it: the real apply path.
    const auto type_save = [&](QwertyContext context, const std::string& dir, const std::string& text) {
        r.state.qwerty = QwertyKeyboardState{};
        r.state.qwerty.isOpen = true;
        r.state.qwerty.context = context;
        r.state.qwerty.contextExtra = dir;
        r.state.qwerty.text = text;
        r.state.qwerty.textCursor = static_cast<int>(text.size());
        r.tap(Button::START);
    };
    const auto exists = [&](const std::string& path) { return r.fs.file_exists(path); };
    struct Kind {
        QwertyContext context; std::string dir, ext, thing;
        std::function<std::string()> current;   // the name the slot, row or theme shows now
    };
    const songcore::Project& p = r.host.project();
    const std::vector<Kind> kinds = {
        {QwertyContext::INSTRUMENT_SAVE, r.fs.instruments_directory(), "pti", "INSTRUMENT",
         [&] { return p.instruments[static_cast<size_t>(r.state.currentInstrument)].name; }},
        {QwertyContext::THEME_SAVE, r.fs.themes_directory(), "ptt", "THEME", [&] { return r.state.theme.name; }},
        {QwertyContext::SCALE_SAVE, r.fs.scales_directory(), "pts", "SCALE",
         [&] { return p.scales[static_cast<size_t>(r.state.currentScale)].name; }},
        {QwertyContext::GROOVE_SAVE, r.fs.grooves_directory(), "ptg", "GROOVE",
         [&] { return p.grooves[static_cast<size_t>(r.state.currentGroove)].name; }},
        {QwertyContext::REVERB_SAVE, r.fs.reverbs_directory(), "ptr", "REVERB", [&] { return p.reverbPreset; }},
        {QwertyContext::DELAY_SAVE, r.fs.delays_directory(), "ptd", "DELAY", [&] { return p.delayPreset; }},
    };
    for (const Kind& k : kinds) {
        const std::string what = k.thing + ": ";
        const std::string file = k.dir + "/TN." + k.ext, copy = k.dir + "/TN_COPY." + k.ext,
                          copy2 = k.dir + "/TN_COPY_2." + k.ext;
        type_save(k.context, k.dir, "TN");
        check(exists(file) && !r.state.confirm.is_open(), (what + "a free name saves at once").c_str());
        type_save(k.context, k.dir, "TN");
        check(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_OVERWRITE &&
                  confirm_dialog_title(r.state.confirm.kind, r.state.confirm.arg) == k.thing + " EXISTS - OVERWRITE?",
              (what + "a taken name asks " + k.thing + " EXISTS - OVERWRITE?").c_str());
        r.tap(Button::A);
        check(!r.state.confirm.is_open() && !r.state.qwerty.isOpen && r.state.statusSuccess,
              (what + "A overwrites").c_str());
        type_save(k.context, k.dir, "TN");
        r.tap(Button::B);
        check(r.state.qwerty.isOpen && r.state.qwerty.context == k.context && r.state.qwerty.text == "TN-COPY",
              (what + "B offers TN-COPY").c_str());
        r.tap(Button::START);
        check(exists(copy) && k.current() == "TN-COPY", (what + "the copy is saved and becomes current").c_str());
        type_save(k.context, k.dir, "TN");
        r.tap(Button::B);
        check(r.state.qwerty.text == "TN-COPY-2", (what + "then B offers TN-COPY-2").c_str());
        r.state.qwerty.text = "TN";   // typed over the suggestion: a taken name
        r.tap(Button::START);
        check(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_OVERWRITE,
              (what + "a typed taken name asks again").c_str());
        r.tap(Button::B);
        r.dispatch.on_select_b();
        check(!r.state.qwerty.isOpen && !r.state.confirm.is_open() && !exists(copy2) && k.current() == "TN-COPY",
              (what + "cancelling the keyboard saves and changes nothing").c_str());
    }
    r.state.themeEditor.isOpen = false;   // THEME's save reopens its editor
    type_save(QwertyContext::INSTRUMENT_SAVE, r.fs.instruments_directory(), "A/B");
    check(exists(r.fs.instruments_directory() + "/A_B.pti"), "an instrument name with / saves as a safe file name");

    // COPY: a copy beside the song, which becomes the song's file; the old file stays.
    r.host.edit_project().name = "ASONG";
    r.state.projectPath.clear();
    r.project_cell(1);
    const std::string asong = r.fs.projects_directory() + "/ASONG.ptp";
    const std::string asongText = read(r.fs, asong);
    r.project_cell(2);
    check(r.state.qwerty.isOpen && r.state.qwerty.text == "ASONG-COPY", "COPY opens the keyboard on ASONG-COPY");
    r.dispatch.on_select_b();
    check(!r.state.qwerty.isOpen && r.host.project().name == "ASONG" && r.state.projectPath == asong,
          "cancelling COPY changes nothing");
    r.project_cell(2);
    r.tap(Button::START);
    const std::string acopy = r.fs.projects_directory() + "/ASONG-COPY.ptp";
    check(exists(acopy) && r.host.project().name == "ASONG-COPY" && r.state.projectPath == acopy &&
              !r.state.project_dirty() && read(r.fs, asong) == asongText,
          "COPY saves the copy, makes it the song's file and leaves the old one");
    r.project_cell(2);
    r.state.qwerty.text = "ASONG";
    r.tap(Button::START);
    check(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_OVERWRITE &&
              confirm_dialog_title(r.state.confirm.kind, r.state.confirm.arg) == "SONG EXISTS - OVERWRITE?",
          "COPY onto a taken name asks SONG EXISTS - OVERWRITE?");
    r.tap(Button::B);
    r.dispatch.on_select_b();

    // FILE RENAME never overwrites: NAME TAKEN, and the keyboard comes back with the typed name.
    const std::string renameA = r.fs.projects_directory() + "/RA.ptp", renameB = r.fs.projects_directory() + "/RB.ptp";
    r.fs.write_file(renameA, "{\"name\":\"RA\"}");
    r.fs.write_file(renameB, "{\"name\":\"RB\"}");
    type_save(QwertyContext::FILE_RENAME, renameA, "RB");
    check(r.state.fileBrowser.statusMessage == "NAME TAKEN" && r.state.qwerty.isOpen && r.state.qwerty.text == "RB" &&
              read(r.fs, renameA) == "{\"name\":\"RA\"}" && read(r.fs, renameB) == "{\"name\":\"RB\"}",
          "renaming onto a taken name says NAME TAKEN and reopens the keyboard");
    r.dispatch.on_select_b();

    // ── 7c · settings.json keeps a key only a newer TrackWerk knows ──
    r.fs.write_file(r.fs.settings_path(), "{\"futureKey\":42,\"metronome\":true}");
    SettingsValues values;
    Theme theme = r.state.theme;
    check(load_settings(r.fs, values, theme) && save_settings(r.fs, values, theme) &&
          read(r.fs, r.fs.settings_path()).find("\"futureKey\": 42") != std::string::npos,
          "an unknown settings.json key survives a load and save");

#ifdef _WIN32
    // ── 8 · Above a drive's root the browser lists the drives ──
    const std::string system = std::filesystem::path(dir).root_path().generic_string();   // e.g. C:/
    const std::string drives = r.fs.parent_path(system);
    check(!drives.empty() && r.fs.is_directory(drives), "a drive root has a parent: the drive list");
    bool listed = false;
    for (const auto& f : r.fs.list_files(drives))
        if (f.isDirectory && f.path == system) listed = true;
    check(listed, "the drive list holds this drive");
    check(r.fs.parent_path(drives).empty(), "nothing above the drive list");
#endif

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
